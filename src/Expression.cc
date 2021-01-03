#include "DebugString.h"
#include "ToString.h"
#include "syntax_nodes.h"
#include <iostream>
namespace {

NameSpace kBuiltInTypes;
Declaration* kTypeDecls[2] = {new TypeDeclaration("int", new IntType()),
                              new TypeDeclaration("string", new StringType())};

NameSpace kBuiltInFunctions;
void AddDecl(const FunctionDeclaration* f) { kBuiltInFunctions[f->Id()] = f; }

class BuiltInBody : public Expression {
public:
  bool Accept(ExpressionVisitor&) const override { return true; }
};
void AddProc(std::string_view id, std::vector<TypeField> params) {
  AddDecl(new FunctionDeclaration(id, std::move(params), new BuiltInBody()));
}
void AddFun(std::string_view id, std::vector<TypeField> params,
            std::string_view type_id) {
  AddDecl(new FunctionDeclaration(id, std::move(params), type_id,
                                  new BuiltInBody()));
}

// Type of expressions that lack a value, e.g. the `break` expression.
std::string kNoneType = "none";
// Marker value when type of expression could not be inferred, e.g. undefined
// variable reference.
std::string kUnknownType = "???";

std::string kUnsetType = "unset";

} // namespace

// Sets inferred_type for all nodex with values, except for Nil. Nil requires a
// separate visitor. This is used in combination with a SyntaxTreeVisitor, which
// should ensure that types of all child expression have already been set.
// Refrains from type checking.
struct TypeSetter : public ExpressionVisitor, LValueVisitor {
  TypeSetter(const Expression& expr) : expr_(expr) {}
  bool VisitStringConstant(const std::string& text) override {
    return SetType(kTypeDecls[1]->Id());
  }
  bool VisitIntegerConstant(int value) override {
    return SetType(kTypeDecls[0]->Id());
  }
  // Nil requires a more complex traversal
  bool VisitNil() override { return false; }
  bool VisitLValue(const LValue& value) override {
    value.Accept(*(LValueVisitor*)this);
    return false;
  }
  bool VisitNegated(const Expression& value) override {
    return SetType(value.GetType());
  }
  bool VisitBinary(const Expression& left, BinaryOp op,
                   const Expression& right) override {
    return SetType(right.GetType());
  }
  bool VisitAssignment(const LValue& value, const Expression& expr) override {
    return SetType(kNoneType);
  }
  bool VisitFunctionCall(
      const std::string& id,
      const std::vector<std::shared_ptr<Expression>>& args) override {
    if (auto d = expr_.non_types_->Lookup(id); d) {
      if (auto vt = (*d)->GetValueType(); vt) {
        return SetType(**vt);
      }
    }
    return SetType(kUnknownType);
  }
  bool
  VisitBlock(const std::vector<std::shared_ptr<Expression>>& exprs) override {
    return SetType(exprs.empty() ? kNoneType : (*exprs.rbegin())->GetType());
  }
  bool VisitRecord(const std::string& type_id,
                   const std::vector<FieldValue>& field_values) override {
    return SetType(type_id);
  }
  bool VisitArray(const std::string& type_id, const Expression& size,
                  const Expression& value) override {
    return SetType(type_id);
  }
  bool VisitIfThen(const Expression& condition,
                   const Expression& expr) override {
    return SetType(kNoneType);
  }
  bool VisitIfThenElse(const Expression& condition, const Expression& then_expr,
                       const Expression& else_expr) override {
    return SetType(then_expr.GetType());
  }
  bool VisitWhile(const Expression& condition,
                  const Expression& body) override {
    return SetType(kNoneType);
  }
  bool VisitFor(const std::string& id, const Expression& first,
                const Expression& last, const Expression& body) override {
    return SetType(kNoneType);
  }
  bool VisitBreak() override { return SetType(kNoneType); }
  bool VisitLet(const std::vector<std::shared_ptr<Declaration>>& declarations,
                const std::vector<std::shared_ptr<Expression>>& body) override {
    return SetType(body.empty() ? kNoneType : (*body.rbegin())->GetType());
  }
  bool VisitId(const std::string& id) override {
    if (auto found = expr_.non_types_->Lookup(id); found) {
      std::cout << "found " << id << std::endl;
      return SetType(**(*found)->GetValueType());
    }
    std::cout << "not found " << id << " in " << *expr_.non_types_ << std::endl;
    return SetType(kUnknownType);
  }
  bool VisitField(const LValue& value, const std::string& id) override {
    if (auto d = expr_.types_->Lookup(value.GetType()); d) {
      if (auto type = (*d)->GetType(); type) {
        if (auto field_type = (*type)->GetFieldType(id); field_type) {
          return SetType(**field_type);
        }
      }
    }
    return SetType(kUnknownType);
  }
  bool VisitIndex(const LValue& value, const Expression& expr) override {
    if (auto d = expr_.types_->Lookup(value.GetType()); d) {
      if (auto type = (*d)->GetType(); type) {
        if (auto element_type = (*type)->GetElementType(); element_type) {
          return SetType(**element_type);
        }
      }
    }
    return SetType(kUnknownType);
  }
  bool SetType(const std::string& type) {
    expr_.type_ = &type;
    return false;
  }
  const Expression& expr_;
};

struct TreeTypeSetter : SyntaxTreeVisitor {
  bool VisitChild(Expression& child) override { return SetType(child); }
  bool AfterChildren(Expression& parent) override { return SetType(parent); }
  static bool SetType(Expression& e) {
    TypeSetter setter(e);
    e.Accept(setter);
    std::cout << "SetType " << DebugString(e) << std::endl;
    return true;
  }
};

void Expression::SetNameSpacesBelow(Expression& root) {
  for (auto* d : kTypeDecls) kBuiltInTypes[d->Id()] = d;
  AddProc("print", {{"s", "string"}});
  AddProc("print", {{"i", "int"}});
  AddProc("flush", {});
  AddFun("getchar", {}, "string");
  AddFun("ord", {{"s", "string"}}, "int");
  AddFun("chr", {{"i", "int"}}, "string");
  AddFun("size", {{"s", "string"}}, "int");
  AddFun("substring", {{"s", "string"}, {"f", "int"}, {"n", "int"}}, "string");
  AddFun("concat", {{"s1", "string"}, {"s2", "string"}}, "string");
  AddFun("not", {{"i", "int"}}, "int");
  AddProc("exit", {{"i", "int"}});
  root.SetNameSpacesBelow(&kBuiltInTypes, &kBuiltInFunctions);
}

void Expression::SetTypesBelow(Expression& root) {
  TreeTypeSetter children_setter;
  root.Accept(children_setter);
  TypeSetter root_setter(root);
  root.Accept(root_setter);
  std::cout << "SetTypesBelow " << DebugString(root) << std::endl;
}

Expression::Expression() : type_(&kUnsetType) {}
