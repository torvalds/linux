//===-- llvm/GlobalVariable.h - GlobalVariable class ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the GlobalVariable class, which
// represents a single global variable (or constant) in the VM.
//
// Global variables are constant pointers that refer to hunks of space that are
// allocated by either the VM, or by the linker in a static compiler.  A global
// variable may have an initial value, which is copied into the executables .data
// area.  Global Constants are required to have initializers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALVARIABLE_H
#define LLVM_IR_GLOBALVARIABLE_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"
#include <cassert>
#include <cstddef>

namespace llvm {

class Constant;
class Module;

template <typename ValueSubClass> class SymbolTableListTraits;
class DIGlobalVariable;
class DIGlobalVariableExpression;

class GlobalVariable : public GlobalObject, public ilist_node<GlobalVariable> {
  friend class SymbolTableListTraits<GlobalVariable>;

  AttributeSet Attrs;
  bool isConstantGlobal : 1;                   // Is this a global constant?
  bool isExternallyInitializedConstant : 1;    // Is this a global whose value
                                               // can change from its initial
                                               // value before global
                                               // initializers are run?

public:
  /// GlobalVariable ctor - If a parent module is specified, the global is
  /// automatically inserted into the end of the specified modules global list.
  GlobalVariable(Type *Ty, bool isConstant, LinkageTypes Linkage,
                 Constant *Initializer = nullptr, const Twine &Name = "",
                 ThreadLocalMode = NotThreadLocal, unsigned AddressSpace = 0,
                 bool isExternallyInitialized = false);
  /// GlobalVariable ctor - This creates a global and inserts it before the
  /// specified other global.
  GlobalVariable(Module &M, Type *Ty, bool isConstant,
                 LinkageTypes Linkage, Constant *Initializer,
                 const Twine &Name = "", GlobalVariable *InsertBefore = nullptr,
                 ThreadLocalMode = NotThreadLocal, unsigned AddressSpace = 0,
                 bool isExternallyInitialized = false);
  GlobalVariable(const GlobalVariable &) = delete;
  GlobalVariable &operator=(const GlobalVariable &) = delete;

  ~GlobalVariable() {
    dropAllReferences();
  }

  // allocate space for exactly one operand
  void *operator new(size_t s) {
    return User::operator new(s, 1);
  }

  // delete space for exactly one operand as created in the corresponding new operator
  void operator delete(void *ptr){
    assert(ptr != nullptr && "must not be nullptr");
    User *Obj = static_cast<User *>(ptr);
    // Number of operands can be set to 0 after construction and initialization. Make sure
    // that number of operands is reset to 1, as this is needed in User::operator delete
    Obj->setGlobalVariableNumOperands(1);
    User::operator delete(Obj);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Definitions have initializers, declarations don't.
  ///
  inline bool hasInitializer() const { return !isDeclaration(); }

  /// hasDefinitiveInitializer - Whether the global variable has an initializer,
  /// and any other instances of the global (this can happen due to weak
  /// linkage) are guaranteed to have the same initializer.
  ///
  /// Note that if you want to transform a global, you must use
  /// hasUniqueInitializer() instead, because of the *_odr linkage type.
  ///
  /// Example:
  ///
  /// @a = global SomeType* null - Initializer is both definitive and unique.
  ///
  /// @b = global weak SomeType* null - Initializer is neither definitive nor
  /// unique.
  ///
  /// @c = global weak_odr SomeType* null - Initializer is definitive, but not
  /// unique.
  inline bool hasDefinitiveInitializer() const {
    return hasInitializer() &&
      // The initializer of a global variable may change to something arbitrary
      // at link time.
      !isInterposable() &&
      // The initializer of a global variable with the externally_initialized
      // marker may change at runtime before C++ initializers are evaluated.
      !isExternallyInitialized();
  }

  /// hasUniqueInitializer - Whether the global variable has an initializer, and
  /// any changes made to the initializer will turn up in the final executable.
  inline bool hasUniqueInitializer() const {
    return
        // We need to be sure this is the definition that will actually be used
        isStrongDefinitionForLinker() &&
        // It is not safe to modify initializers of global variables with the
        // external_initializer marker since the value may be changed at runtime
        // before C++ initializers are evaluated.
        !isExternallyInitialized();
  }

  /// getInitializer - Return the initializer for this global variable.  It is
  /// illegal to call this method if the global is external, because we cannot
  /// tell what the value is initialized to!
  ///
  inline const Constant *getInitializer() const {
    assert(hasInitializer() && "GV doesn't have initializer!");
    return static_cast<Constant*>(Op<0>().get());
  }
  inline Constant *getInitializer() {
    assert(hasInitializer() && "GV doesn't have initializer!");
    return static_cast<Constant*>(Op<0>().get());
  }
  /// setInitializer - Sets the initializer for this global variable, removing
  /// any existing initializer if InitVal==NULL.  If this GV has type T*, the
  /// initializer must have type T.
  void setInitializer(Constant *InitVal);

  /// If the value is a global constant, its value is immutable throughout the
  /// runtime execution of the program.  Assigning a value into the constant
  /// leads to undefined behavior.
  ///
  bool isConstant() const { return isConstantGlobal; }
  void setConstant(bool Val) { isConstantGlobal = Val; }

  bool isExternallyInitialized() const {
    return isExternallyInitializedConstant;
  }
  void setExternallyInitialized(bool Val) {
    isExternallyInitializedConstant = Val;
  }

  /// copyAttributesFrom - copy all additional attributes (those not needed to
  /// create a GlobalVariable) from the GlobalVariable Src to this one.
  void copyAttributesFrom(const GlobalVariable *Src);

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  void eraseFromParent();

  /// Drop all references in preparation to destroy the GlobalVariable. This
  /// drops not only the reference to the initializer but also to any metadata.
  void dropAllReferences();

  /// Attach a DIGlobalVariableExpression.
  void addDebugInfo(DIGlobalVariableExpression *GV);

  /// Fill the vector with all debug info attachements.
  void getDebugInfo(SmallVectorImpl<DIGlobalVariableExpression *> &GVs) const;

  /// Add attribute to this global.
  void addAttribute(Attribute::AttrKind Kind) {
    Attrs = Attrs.addAttribute(getContext(), Kind);
  }

  /// Add attribute to this global.
  void addAttribute(StringRef Kind, StringRef Val = StringRef()) {
    Attrs = Attrs.addAttribute(getContext(), Kind, Val);
  }

  /// Return true if the attribute exists.
  bool hasAttribute(Attribute::AttrKind Kind) const {
    return Attrs.hasAttribute(Kind);
  }

  /// Return true if the attribute exists.
  bool hasAttribute(StringRef Kind) const {
    return Attrs.hasAttribute(Kind);
  }

  /// Return true if any attributes exist.
  bool hasAttributes() const {
    return Attrs.hasAttributes();
  }

  /// Return the attribute object.
  Attribute getAttribute(Attribute::AttrKind Kind) const {
    return Attrs.getAttribute(Kind);
  }

  /// Return the attribute object.
  Attribute getAttribute(StringRef Kind) const {
    return Attrs.getAttribute(Kind);
  }

  /// Return the attribute set for this global
  AttributeSet getAttributes() const {
    return Attrs;
  }

  /// Return attribute set as list with index.
  /// FIXME: This may not be required once ValueEnumerators
  /// in bitcode-writer can enumerate attribute-set.
  AttributeList getAttributesAsList(unsigned index) const {
    if (!hasAttributes())
      return AttributeList();
    std::pair<unsigned, AttributeSet> AS[1] = {{index, Attrs}};
    return AttributeList::get(getContext(), AS);
  }

  /// Set attribute list for this global
  void setAttributes(AttributeSet A) {
    Attrs = A;
  }

  /// Check if section name is present
  bool hasImplicitSection() const {
    return getAttributes().hasAttribute("bss-section") ||
           getAttributes().hasAttribute("data-section") ||
           getAttributes().hasAttribute("rodata-section");
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalVariableVal;
  }
};

template <>
struct OperandTraits<GlobalVariable> :
  public OptionalOperandTraits<GlobalVariable> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalVariable, Value)

} // end namespace llvm

#endif // LLVM_IR_GLOBALVARIABLE_H
