//===- llvm/Function.h - Class to represent a single function ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Function class, which represents a
// single function/procedure in LLVM.
//
// A function basically consists of a list of basic blocks, a list of arguments,
// and a symbol table.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FUNCTION_H
#define LLVM_IR_FUNCTION_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

namespace Intrinsic {
enum ID : unsigned;
}

class AssemblyAnnotationWriter;
class Constant;
class DISubprogram;
class LLVMContext;
class Module;
template <typename T> class Optional;
class raw_ostream;
class Type;
class User;

class Function : public GlobalObject, public ilist_node<Function> {
public:
  using BasicBlockListType = SymbolTableList<BasicBlock>;

  // BasicBlock iterators...
  using iterator = BasicBlockListType::iterator;
  using const_iterator = BasicBlockListType::const_iterator;

  using arg_iterator = Argument *;
  using const_arg_iterator = const Argument *;

private:
  // Important things that make up a function!
  BasicBlockListType BasicBlocks;         ///< The basic blocks
  mutable Argument *Arguments = nullptr;  ///< The formal arguments
  size_t NumArgs;
  std::unique_ptr<ValueSymbolTable>
      SymTab;                             ///< Symbol table of args/instructions
  AttributeList AttributeSets;            ///< Parameter attributes

  /*
   * Value::SubclassData
   *
   * bit 0      : HasLazyArguments
   * bit 1      : HasPrefixData
   * bit 2      : HasPrologueData
   * bit 3      : HasPersonalityFn
   * bits 4-13  : CallingConvention
   * bits 14    : HasGC
   * bits 15 : [reserved]
   */

  /// Bits from GlobalObject::GlobalObjectSubclassData.
  enum {
    /// Whether this function is materializable.
    IsMaterializableBit = 0,
  };

  friend class SymbolTableListTraits<Function>;

  /// hasLazyArguments/CheckLazyArguments - The argument list of a function is
  /// built on demand, so that the list isn't allocated until the first client
  /// needs it.  The hasLazyArguments predicate returns true if the arg list
  /// hasn't been set up yet.
public:
  bool hasLazyArguments() const {
    return getSubclassDataFromValue() & (1<<0);
  }

private:
  void CheckLazyArguments() const {
    if (hasLazyArguments())
      BuildLazyArguments();
  }

  void BuildLazyArguments() const;

  void clearArguments();

  /// Function ctor - If the (optional) Module argument is specified, the
  /// function is automatically inserted into the end of the function list for
  /// the module.
  ///
  Function(FunctionType *Ty, LinkageTypes Linkage, unsigned AddrSpace,
           const Twine &N = "", Module *M = nullptr);

public:
  Function(const Function&) = delete;
  void operator=(const Function&) = delete;
  ~Function();

  // This is here to help easily convert from FunctionT * (Function * or
  // MachineFunction *) in BlockFrequencyInfoImpl to Function * by calling
  // FunctionT->getFunction().
  const Function &getFunction() const { return *this; }

  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          unsigned AddrSpace, const Twine &N = "",
                          Module *M = nullptr) {
    return new Function(Ty, Linkage, AddrSpace, N, M);
  }

  // TODO: remove this once all users have been updated to pass an AddrSpace
  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          const Twine &N = "", Module *M = nullptr) {
    return new Function(Ty, Linkage, static_cast<unsigned>(-1), N, M);
  }

  /// Creates a new function and attaches it to a module.
  ///
  /// Places the function in the program address space as specified
  /// by the module's data layout.
  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          const Twine &N, Module &M);

  // Provide fast operand accessors.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Returns the number of non-debug IR instructions in this function.
  /// This is equivalent to the sum of the sizes of each basic block contained
  /// within this function.
  unsigned getInstructionCount() const;

  /// Returns the FunctionType for me.
  FunctionType *getFunctionType() const {
    return cast<FunctionType>(getValueType());
  }

  /// Returns the type of the ret val.
  Type *getReturnType() const { return getFunctionType()->getReturnType(); }

  /// getContext - Return a reference to the LLVMContext associated with this
  /// function.
  LLVMContext &getContext() const;

  /// isVarArg - Return true if this function takes a variable number of
  /// arguments.
  bool isVarArg() const { return getFunctionType()->isVarArg(); }

  bool isMaterializable() const {
    return getGlobalObjectSubClassData() & (1 << IsMaterializableBit);
  }
  void setIsMaterializable(bool V) {
    unsigned Mask = 1 << IsMaterializableBit;
    setGlobalObjectSubClassData((~Mask & getGlobalObjectSubClassData()) |
                                (V ? Mask : 0u));
  }

  /// getIntrinsicID - This method returns the ID number of the specified
  /// function, or Intrinsic::not_intrinsic if the function is not an
  /// intrinsic, or if the pointer is null.  This value is always defined to be
  /// zero to allow easy checking for whether a function is intrinsic or not.
  /// The particular intrinsic functions which correspond to this value are
  /// defined in llvm/Intrinsics.h.
  Intrinsic::ID getIntrinsicID() const LLVM_READONLY { return IntID; }

  /// isIntrinsic - Returns true if the function's name starts with "llvm.".
  /// It's possible for this function to return true while getIntrinsicID()
  /// returns Intrinsic::not_intrinsic!
  bool isIntrinsic() const { return HasLLVMReservedName; }

  static Intrinsic::ID lookupIntrinsicID(StringRef Name);

  /// Recalculate the ID for this function if it is an Intrinsic defined
  /// in llvm/Intrinsics.h.  Sets the intrinsic ID to Intrinsic::not_intrinsic
  /// if the name of this function does not match an intrinsic in that header.
  /// Note, this method does not need to be called directly, as it is called
  /// from Value::setName() whenever the name of this function changes.
  void recalculateIntrinsicID();

  /// getCallingConv()/setCallingConv(CC) - These method get and set the
  /// calling convention of this function.  The enum values for the known
  /// calling conventions are defined in CallingConv.h.
  CallingConv::ID getCallingConv() const {
    return static_cast<CallingConv::ID>((getSubclassDataFromValue() >> 4) &
                                        CallingConv::MaxID);
  }
  void setCallingConv(CallingConv::ID CC) {
    auto ID = static_cast<unsigned>(CC);
    assert(!(ID & ~CallingConv::MaxID) && "Unsupported calling convention");
    setValueSubclassData((getSubclassDataFromValue() & 0xc00f) | (ID << 4));
  }

  /// Return the attribute list for this Function.
  AttributeList getAttributes() const { return AttributeSets; }

  /// Set the attribute list for this Function.
  void setAttributes(AttributeList Attrs) { AttributeSets = Attrs; }

  /// Add function attributes to this function.
  void addFnAttr(Attribute::AttrKind Kind) {
    addAttribute(AttributeList::FunctionIndex, Kind);
  }

  /// Add function attributes to this function.
  void addFnAttr(StringRef Kind, StringRef Val = StringRef()) {
    addAttribute(AttributeList::FunctionIndex,
                 Attribute::get(getContext(), Kind, Val));
  }

  /// Add function attributes to this function.
  void addFnAttr(Attribute Attr) {
    addAttribute(AttributeList::FunctionIndex, Attr);
  }

  /// Remove function attributes from this function.
  void removeFnAttr(Attribute::AttrKind Kind) {
    removeAttribute(AttributeList::FunctionIndex, Kind);
  }

  /// Remove function attribute from this function.
  void removeFnAttr(StringRef Kind) {
    setAttributes(getAttributes().removeAttribute(
        getContext(), AttributeList::FunctionIndex, Kind));
  }

  enum ProfileCountType { PCT_Invalid, PCT_Real, PCT_Synthetic };

  /// Class to represent profile counts.
  ///
  /// This class represents both real and synthetic profile counts.
  class ProfileCount {
  private:
    uint64_t Count;
    ProfileCountType PCT;
    static ProfileCount Invalid;

  public:
    ProfileCount() : Count(-1), PCT(PCT_Invalid) {}
    ProfileCount(uint64_t Count, ProfileCountType PCT)
        : Count(Count), PCT(PCT) {}
    bool hasValue() const { return PCT != PCT_Invalid; }
    uint64_t getCount() const { return Count; }
    ProfileCountType getType() const { return PCT; }
    bool isSynthetic() const { return PCT == PCT_Synthetic; }
    explicit operator bool() { return hasValue(); }
    bool operator!() const { return !hasValue(); }
    // Update the count retaining the same profile count type.
    ProfileCount &setCount(uint64_t C) {
      Count = C;
      return *this;
    }
    static ProfileCount getInvalid() { return ProfileCount(-1, PCT_Invalid); }
  };

  /// Set the entry count for this function.
  ///
  /// Entry count is the number of times this function was executed based on
  /// pgo data. \p Imports points to a set of GUIDs that needs to
  /// be imported by the function for sample PGO, to enable the same inlines as
  /// the profiled optimized binary.
  void setEntryCount(ProfileCount Count,
                     const DenseSet<GlobalValue::GUID> *Imports = nullptr);

  /// A convenience wrapper for setting entry count
  void setEntryCount(uint64_t Count, ProfileCountType Type = PCT_Real,
                     const DenseSet<GlobalValue::GUID> *Imports = nullptr);

  /// Get the entry count for this function.
  ///
  /// Entry count is the number of times the function was executed based on
  /// pgo data.
  ProfileCount getEntryCount() const;

  /// Return true if the function is annotated with profile data.
  ///
  /// Presence of entry counts from a profile run implies the function has
  /// profile annotations.
  bool hasProfileData() const { return getEntryCount().hasValue(); }

  /// Returns the set of GUIDs that needs to be imported to the function for
  /// sample PGO, to enable the same inlines as the profiled optimized binary.
  DenseSet<GlobalValue::GUID> getImportGUIDs() const;

  /// Set the section prefix for this function.
  void setSectionPrefix(StringRef Prefix);

  /// Get the section prefix for this function.
  Optional<StringRef> getSectionPrefix() const;

  /// Return true if the function has the attribute.
  bool hasFnAttribute(Attribute::AttrKind Kind) const {
    return AttributeSets.hasFnAttribute(Kind);
  }

  /// Return true if the function has the attribute.
  bool hasFnAttribute(StringRef Kind) const {
    return AttributeSets.hasFnAttribute(Kind);
  }

  /// Return the attribute for the given attribute kind.
  Attribute getFnAttribute(Attribute::AttrKind Kind) const {
    return getAttribute(AttributeList::FunctionIndex, Kind);
  }

  /// Return the attribute for the given attribute kind.
  Attribute getFnAttribute(StringRef Kind) const {
    return getAttribute(AttributeList::FunctionIndex, Kind);
  }

  /// Return the stack alignment for the function.
  unsigned getFnStackAlignment() const {
    if (!hasFnAttribute(Attribute::StackAlignment))
      return 0;
    return AttributeSets.getStackAlignment(AttributeList::FunctionIndex);
  }

  /// hasGC/getGC/setGC/clearGC - The name of the garbage collection algorithm
  ///                             to use during code generation.
  bool hasGC() const {
    return getSubclassDataFromValue() & (1<<14);
  }
  const std::string &getGC() const;
  void setGC(std::string Str);
  void clearGC();

  /// adds the attribute to the list of attributes.
  void addAttribute(unsigned i, Attribute::AttrKind Kind);

  /// adds the attribute to the list of attributes.
  void addAttribute(unsigned i, Attribute Attr);

  /// adds the attributes to the list of attributes.
  void addAttributes(unsigned i, const AttrBuilder &Attrs);

  /// adds the attribute to the list of attributes for the given arg.
  void addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind);

  /// adds the attribute to the list of attributes for the given arg.
  void addParamAttr(unsigned ArgNo, Attribute Attr);

  /// adds the attributes to the list of attributes for the given arg.
  void addParamAttrs(unsigned ArgNo, const AttrBuilder &Attrs);

  /// removes the attribute from the list of attributes.
  void removeAttribute(unsigned i, Attribute::AttrKind Kind);

  /// removes the attribute from the list of attributes.
  void removeAttribute(unsigned i, StringRef Kind);

  /// removes the attributes from the list of attributes.
  void removeAttributes(unsigned i, const AttrBuilder &Attrs);

  /// removes the attribute from the list of attributes.
  void removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind);

  /// removes the attribute from the list of attributes.
  void removeParamAttr(unsigned ArgNo, StringRef Kind);

  /// removes the attribute from the list of attributes.
  void removeParamAttrs(unsigned ArgNo, const AttrBuilder &Attrs);

  /// check if an attributes is in the list of attributes.
  bool hasAttribute(unsigned i, Attribute::AttrKind Kind) const {
    return getAttributes().hasAttribute(i, Kind);
  }

  /// check if an attributes is in the list of attributes.
  bool hasParamAttribute(unsigned ArgNo, Attribute::AttrKind Kind) const {
    return getAttributes().hasParamAttribute(ArgNo, Kind);
  }

  /// gets the attribute from the list of attributes.
  Attribute getAttribute(unsigned i, Attribute::AttrKind Kind) const {
    return AttributeSets.getAttribute(i, Kind);
  }

  /// gets the attribute from the list of attributes.
  Attribute getAttribute(unsigned i, StringRef Kind) const {
    return AttributeSets.getAttribute(i, Kind);
  }

  /// adds the dereferenceable attribute to the list of attributes.
  void addDereferenceableAttr(unsigned i, uint64_t Bytes);

  /// adds the dereferenceable attribute to the list of attributes for
  /// the given arg.
  void addDereferenceableParamAttr(unsigned ArgNo, uint64_t Bytes);

  /// adds the dereferenceable_or_null attribute to the list of
  /// attributes.
  void addDereferenceableOrNullAttr(unsigned i, uint64_t Bytes);

  /// adds the dereferenceable_or_null attribute to the list of
  /// attributes for the given arg.
  void addDereferenceableOrNullParamAttr(unsigned ArgNo, uint64_t Bytes);

  /// Extract the alignment for a call or parameter (0=unknown).
  unsigned getParamAlignment(unsigned ArgNo) const {
    return AttributeSets.getParamAlignment(ArgNo);
  }

  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  /// @param i AttributeList index, referring to a return value or argument.
  uint64_t getDereferenceableBytes(unsigned i) const {
    return AttributeSets.getDereferenceableBytes(i);
  }

  /// Extract the number of dereferenceable bytes for a parameter.
  /// @param ArgNo Index of an argument, with 0 being the first function arg.
  uint64_t getParamDereferenceableBytes(unsigned ArgNo) const {
    return AttributeSets.getParamDereferenceableBytes(ArgNo);
  }

  /// Extract the number of dereferenceable_or_null bytes for a call or
  /// parameter (0=unknown).
  /// @param i AttributeList index, referring to a return value or argument.
  uint64_t getDereferenceableOrNullBytes(unsigned i) const {
    return AttributeSets.getDereferenceableOrNullBytes(i);
  }

  /// Extract the number of dereferenceable_or_null bytes for a
  /// parameter.
  /// @param ArgNo AttributeList ArgNo, referring to an argument.
  uint64_t getParamDereferenceableOrNullBytes(unsigned ArgNo) const {
    return AttributeSets.getParamDereferenceableOrNullBytes(ArgNo);
  }

  /// Determine if the function does not access memory.
  bool doesNotAccessMemory() const {
    return hasFnAttribute(Attribute::ReadNone);
  }
  void setDoesNotAccessMemory() {
    addFnAttr(Attribute::ReadNone);
  }

  /// Determine if the function does not access or only reads memory.
  bool onlyReadsMemory() const {
    return doesNotAccessMemory() || hasFnAttribute(Attribute::ReadOnly);
  }
  void setOnlyReadsMemory() {
    addFnAttr(Attribute::ReadOnly);
  }

  /// Determine if the function does not access or only writes memory.
  bool doesNotReadMemory() const {
    return doesNotAccessMemory() || hasFnAttribute(Attribute::WriteOnly);
  }
  void setDoesNotReadMemory() {
    addFnAttr(Attribute::WriteOnly);
  }

  /// Determine if the call can access memmory only using pointers based
  /// on its arguments.
  bool onlyAccessesArgMemory() const {
    return hasFnAttribute(Attribute::ArgMemOnly);
  }
  void setOnlyAccessesArgMemory() { addFnAttr(Attribute::ArgMemOnly); }

  /// Determine if the function may only access memory that is
  ///  inaccessible from the IR.
  bool onlyAccessesInaccessibleMemory() const {
    return hasFnAttribute(Attribute::InaccessibleMemOnly);
  }
  void setOnlyAccessesInaccessibleMemory() {
    addFnAttr(Attribute::InaccessibleMemOnly);
  }

  /// Determine if the function may only access memory that is
  ///  either inaccessible from the IR or pointed to by its arguments.
  bool onlyAccessesInaccessibleMemOrArgMem() const {
    return hasFnAttribute(Attribute::InaccessibleMemOrArgMemOnly);
  }
  void setOnlyAccessesInaccessibleMemOrArgMem() {
    addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
  }

  /// Determine if the function cannot return.
  bool doesNotReturn() const {
    return hasFnAttribute(Attribute::NoReturn);
  }
  void setDoesNotReturn() {
    addFnAttr(Attribute::NoReturn);
  }

  /// Determine if the function should not perform indirect branch tracking.
  bool doesNoCfCheck() const { return hasFnAttribute(Attribute::NoCfCheck); }

  /// Determine if the function cannot unwind.
  bool doesNotThrow() const {
    return hasFnAttribute(Attribute::NoUnwind);
  }
  void setDoesNotThrow() {
    addFnAttr(Attribute::NoUnwind);
  }

  /// Determine if the call cannot be duplicated.
  bool cannotDuplicate() const {
    return hasFnAttribute(Attribute::NoDuplicate);
  }
  void setCannotDuplicate() {
    addFnAttr(Attribute::NoDuplicate);
  }

  /// Determine if the call is convergent.
  bool isConvergent() const {
    return hasFnAttribute(Attribute::Convergent);
  }
  void setConvergent() {
    addFnAttr(Attribute::Convergent);
  }
  void setNotConvergent() {
    removeFnAttr(Attribute::Convergent);
  }

  /// Determine if the call has sideeffects.
  bool isSpeculatable() const {
    return hasFnAttribute(Attribute::Speculatable);
  }
  void setSpeculatable() {
    addFnAttr(Attribute::Speculatable);
  }

  /// Determine if the function is known not to recurse, directly or
  /// indirectly.
  bool doesNotRecurse() const {
    return hasFnAttribute(Attribute::NoRecurse);
  }
  void setDoesNotRecurse() {
    addFnAttr(Attribute::NoRecurse);
  }

  /// True if the ABI mandates (or the user requested) that this
  /// function be in a unwind table.
  bool hasUWTable() const {
    return hasFnAttribute(Attribute::UWTable);
  }
  void setHasUWTable() {
    addFnAttr(Attribute::UWTable);
  }

  /// True if this function needs an unwind table.
  bool needsUnwindTableEntry() const {
    return hasUWTable() || !doesNotThrow() || hasPersonalityFn();
  }

  /// Determine if the function returns a structure through first
  /// or second pointer argument.
  bool hasStructRetAttr() const {
    return AttributeSets.hasParamAttribute(0, Attribute::StructRet) ||
           AttributeSets.hasParamAttribute(1, Attribute::StructRet);
  }

  /// Determine if the parameter or return value is marked with NoAlias
  /// attribute.
  bool returnDoesNotAlias() const {
    return AttributeSets.hasAttribute(AttributeList::ReturnIndex,
                                      Attribute::NoAlias);
  }
  void setReturnDoesNotAlias() {
    addAttribute(AttributeList::ReturnIndex, Attribute::NoAlias);
  }

  /// Optimize this function for minimum size (-Oz).
  bool optForMinSize() const { return hasFnAttribute(Attribute::MinSize); }

  /// Optimize this function for size (-Os) or minimum size (-Oz).
  bool optForSize() const {
    return hasFnAttribute(Attribute::OptimizeForSize) || optForMinSize();
  }

  /// copyAttributesFrom - copy all additional attributes (those not needed to
  /// create a Function) from the Function Src to this one.
  void copyAttributesFrom(const Function *Src);

  /// deleteBody - This method deletes the body of the function, and converts
  /// the linkage to external.
  ///
  void deleteBody() {
    dropAllReferences();
    setLinkage(ExternalLinkage);
  }

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  void eraseFromParent();

  /// Steal arguments from another function.
  ///
  /// Drop this function's arguments and splice in the ones from \c Src.
  /// Requires that this has no function body.
  void stealArgumentListFrom(Function &Src);

  /// Get the underlying elements of the Function... the basic block list is
  /// empty for external functions.
  ///
  const BasicBlockListType &getBasicBlockList() const { return BasicBlocks; }
        BasicBlockListType &getBasicBlockList()       { return BasicBlocks; }

  static BasicBlockListType Function::*getSublistAccess(BasicBlock*) {
    return &Function::BasicBlocks;
  }

  const BasicBlock       &getEntryBlock() const   { return front(); }
        BasicBlock       &getEntryBlock()         { return front(); }

  //===--------------------------------------------------------------------===//
  // Symbol Table Accessing functions...

  /// getSymbolTable() - Return the symbol table if any, otherwise nullptr.
  ///
  inline ValueSymbolTable *getValueSymbolTable() { return SymTab.get(); }
  inline const ValueSymbolTable *getValueSymbolTable() const {
    return SymTab.get();
  }

  //===--------------------------------------------------------------------===//
  // BasicBlock iterator forwarding functions
  //
  iterator                begin()       { return BasicBlocks.begin(); }
  const_iterator          begin() const { return BasicBlocks.begin(); }
  iterator                end  ()       { return BasicBlocks.end();   }
  const_iterator          end  () const { return BasicBlocks.end();   }

  size_t                   size() const { return BasicBlocks.size();  }
  bool                    empty() const { return BasicBlocks.empty(); }
  const BasicBlock       &front() const { return BasicBlocks.front(); }
        BasicBlock       &front()       { return BasicBlocks.front(); }
  const BasicBlock        &back() const { return BasicBlocks.back();  }
        BasicBlock        &back()       { return BasicBlocks.back();  }

/// @name Function Argument Iteration
/// @{

  arg_iterator arg_begin() {
    CheckLazyArguments();
    return Arguments;
  }
  const_arg_iterator arg_begin() const {
    CheckLazyArguments();
    return Arguments;
  }

  arg_iterator arg_end() {
    CheckLazyArguments();
    return Arguments + NumArgs;
  }
  const_arg_iterator arg_end() const {
    CheckLazyArguments();
    return Arguments + NumArgs;
  }

  iterator_range<arg_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  iterator_range<const_arg_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }

/// @}

  size_t arg_size() const { return NumArgs; }
  bool arg_empty() const { return arg_size() == 0; }

  /// Check whether this function has a personality function.
  bool hasPersonalityFn() const {
    return getSubclassDataFromValue() & (1<<3);
  }

  /// Get the personality function associated with this function.
  Constant *getPersonalityFn() const;
  void setPersonalityFn(Constant *Fn);

  /// Check whether this function has prefix data.
  bool hasPrefixData() const {
    return getSubclassDataFromValue() & (1<<1);
  }

  /// Get the prefix data associated with this function.
  Constant *getPrefixData() const;
  void setPrefixData(Constant *PrefixData);

  /// Check whether this function has prologue data.
  bool hasPrologueData() const {
    return getSubclassDataFromValue() & (1<<2);
  }

  /// Get the prologue data associated with this function.
  Constant *getPrologueData() const;
  void setPrologueData(Constant *PrologueData);

  /// Print the function to an output stream with an optional
  /// AssemblyAnnotationWriter.
  void print(raw_ostream &OS, AssemblyAnnotationWriter *AAW = nullptr,
             bool ShouldPreserveUseListOrder = false,
             bool IsForDebug = false) const;

  /// viewCFG - This function is meant for use from the debugger.  You can just
  /// say 'call F->viewCFG()' and a ghostview window should pop up from the
  /// program, displaying the CFG of the current function with the code for each
  /// basic block inside.  This depends on there being a 'dot' and 'gv' program
  /// in your path.
  ///
  void viewCFG() const;

  /// viewCFGOnly - This function is meant for use from the debugger.  It works
  /// just like viewCFG, but it does not include the contents of basic blocks
  /// into the nodes, just the label.  If you are only interested in the CFG
  /// this can make the graph smaller.
  ///
  void viewCFGOnly() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal;
  }

  /// dropAllReferences() - This method causes all the subinstructions to "let
  /// go" of all references that they are maintaining.  This allows one to
  /// 'delete' a whole module at a time, even though there may be circular
  /// references... first all references are dropped, and all use counts go to
  /// zero.  Then everything is deleted for real.  Note that no operations are
  /// valid on an object that has "dropped all references", except operator
  /// delete.
  ///
  /// Since no other object in the module can have references into the body of a
  /// function, dropping all references deletes the entire body of the function,
  /// including any contained basic blocks.
  ///
  void dropAllReferences();

  /// hasAddressTaken - returns true if there are any uses of this function
  /// other than direct calls or invokes to it, or blockaddress expressions.
  /// Optionally passes back an offending user for diagnostic purposes.
  ///
  bool hasAddressTaken(const User** = nullptr) const;

  /// isDefTriviallyDead - Return true if it is trivially safe to remove
  /// this function definition from the module (because it isn't externally
  /// visible, does not have its address taken, and has no callers).  To make
  /// this more accurate, call removeDeadConstantUsers first.
  bool isDefTriviallyDead() const;

  /// callsFunctionThatReturnsTwice - Return true if the function has a call to
  /// setjmp or other function that gcc recognizes as "returning twice".
  bool callsFunctionThatReturnsTwice() const;

  /// Set the attached subprogram.
  ///
  /// Calls \a setMetadata() with \a LLVMContext::MD_dbg.
  void setSubprogram(DISubprogram *SP);

  /// Get the attached subprogram.
  ///
  /// Calls \a getMetadata() with \a LLVMContext::MD_dbg and casts the result
  /// to \a DISubprogram.
  DISubprogram *getSubprogram() const;

  /// Returns true if we should emit debug info for profiling.
  bool isDebugInfoForProfiling() const;

  /// Check if null pointer dereferencing is considered undefined behavior for
  /// the function.
  /// Return value: false => null pointer dereference is undefined.
  /// Return value: true =>  null pointer dereference is not undefined.
  bool nullPointerIsDefined() const;

private:
  void allocHungoffUselist();
  template<int Idx> void setHungoffOperand(Constant *C);

  /// Shadow Value::setValueSubclassData with a private forwarding method so
  /// that subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
  void setValueSubclassDataBit(unsigned Bit, bool On);
};

/// Check whether null pointer dereferencing is considered undefined behavior
/// for a given function or an address space.
/// Null pointer access in non-zero address space is not considered undefined.
/// Return value: false => null pointer dereference is undefined.
/// Return value: true =>  null pointer dereference is not undefined.
bool NullPointerIsDefined(const Function *F, unsigned AS = 0);

template <>
struct OperandTraits<Function> : public HungoffOperandTraits<3> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(Function, Value)

} // end namespace llvm

#endif // LLVM_IR_FUNCTION_H
