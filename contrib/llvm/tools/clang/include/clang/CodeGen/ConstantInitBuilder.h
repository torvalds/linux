//===- ConstantInitBuilder.h - Builder for LLVM IR constants ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class provides a convenient interface for building complex
// global initializers of the sort that are frequently required for
// language ABIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_CONSTANTINITBUILDER_H
#define LLVM_CLANG_CODEGEN_CONSTANTINITBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "clang/AST/CharUnits.h"
#include "clang/CodeGen/ConstantInitFuture.h"

#include <vector>

namespace clang {
namespace CodeGen {

class CodeGenModule;

/// A convenience builder class for complex constant initializers,
/// especially for anonymous global structures used by various language
/// runtimes.
///
/// The basic usage pattern is expected to be something like:
///    ConstantInitBuilder builder(CGM);
///    auto toplevel = builder.beginStruct();
///    toplevel.addInt(CGM.SizeTy, widgets.size());
///    auto widgetArray = builder.beginArray();
///    for (auto &widget : widgets) {
///      auto widgetDesc = widgetArray.beginStruct();
///      widgetDesc.addInt(CGM.SizeTy, widget.getPower());
///      widgetDesc.add(CGM.GetAddrOfConstantString(widget.getName()));
///      widgetDesc.add(CGM.GetAddrOfGlobal(widget.getInitializerDecl()));
///      widgetDesc.finishAndAddTo(widgetArray);
///    }
///    widgetArray.finishAndAddTo(toplevel);
///    auto global = toplevel.finishAndCreateGlobal("WIDGET_LIST", Align,
///                                                 /*constant*/ true);
class ConstantInitBuilderBase {
  struct SelfReference {
    llvm::GlobalVariable *Dummy;
    llvm::SmallVector<llvm::Constant*, 4> Indices;

    SelfReference(llvm::GlobalVariable *dummy) : Dummy(dummy) {}
  };
  CodeGenModule &CGM;
  llvm::SmallVector<llvm::Constant*, 16> Buffer;
  std::vector<SelfReference> SelfReferences;
  bool Frozen = false;

  friend class ConstantInitFuture;
  friend class ConstantAggregateBuilderBase;
  template <class, class>
  friend class ConstantAggregateBuilderTemplateBase;

protected:
  explicit ConstantInitBuilderBase(CodeGenModule &CGM) : CGM(CGM) {}

  ~ConstantInitBuilderBase() {
    assert(Buffer.empty() && "didn't claim all values out of buffer");
    assert(SelfReferences.empty() && "didn't apply all self-references");
  }

private:
  llvm::GlobalVariable *createGlobal(llvm::Constant *initializer,
                                     const llvm::Twine &name,
                                     CharUnits alignment,
                                     bool constant = false,
                                     llvm::GlobalValue::LinkageTypes linkage
                                       = llvm::GlobalValue::InternalLinkage,
                                     unsigned addressSpace = 0);

  ConstantInitFuture createFuture(llvm::Constant *initializer);

  void setGlobalInitializer(llvm::GlobalVariable *GV,
                            llvm::Constant *initializer);

  void resolveSelfReferences(llvm::GlobalVariable *GV);

  void abandon(size_t newEnd);
};

/// A concrete base class for struct and array aggregate
/// initializer builders.
class ConstantAggregateBuilderBase {
protected:
  ConstantInitBuilderBase &Builder;
  ConstantAggregateBuilderBase *Parent;
  size_t Begin;
  mutable size_t CachedOffsetEnd = 0;
  bool Finished = false;
  bool Frozen = false;
  bool Packed = false;
  mutable CharUnits CachedOffsetFromGlobal;

  llvm::SmallVectorImpl<llvm::Constant*> &getBuffer() {
    return Builder.Buffer;
  }

  const llvm::SmallVectorImpl<llvm::Constant*> &getBuffer() const {
    return Builder.Buffer;
  }

  ConstantAggregateBuilderBase(ConstantInitBuilderBase &builder,
                               ConstantAggregateBuilderBase *parent)
      : Builder(builder), Parent(parent), Begin(builder.Buffer.size()) {
    if (parent) {
      assert(!parent->Frozen && "parent already has child builder active");
      parent->Frozen = true;
    } else {
      assert(!builder.Frozen && "builder already has child builder active");
      builder.Frozen = true;
    }
  }

  ~ConstantAggregateBuilderBase() {
    assert(Finished && "didn't finish aggregate builder");
  }

  void markFinished() {
    assert(!Frozen && "child builder still active");
    assert(!Finished && "builder already finished");
    Finished = true;
    if (Parent) {
      assert(Parent->Frozen &&
             "parent not frozen while child builder active");
      Parent->Frozen = false;
    } else {
      assert(Builder.Frozen &&
             "builder not frozen while child builder active");
      Builder.Frozen = false;
    }
  }

public:
  // Not copyable.
  ConstantAggregateBuilderBase(const ConstantAggregateBuilderBase &) = delete;
  ConstantAggregateBuilderBase &operator=(const ConstantAggregateBuilderBase &)
    = delete;

  // Movable, mostly to allow returning.  But we have to write this out
  // properly to satisfy the assert in the destructor.
  ConstantAggregateBuilderBase(ConstantAggregateBuilderBase &&other)
    : Builder(other.Builder), Parent(other.Parent), Begin(other.Begin),
      CachedOffsetEnd(other.CachedOffsetEnd),
      Finished(other.Finished), Frozen(other.Frozen), Packed(other.Packed),
      CachedOffsetFromGlobal(other.CachedOffsetFromGlobal) {
    other.Finished = true;
  }
  ConstantAggregateBuilderBase &operator=(ConstantAggregateBuilderBase &&other)
    = delete;

  /// Return the number of elements that have been added to
  /// this struct or array.
  size_t size() const {
    assert(!this->Finished && "cannot query after finishing builder");
    assert(!this->Frozen && "cannot query while sub-builder is active");
    assert(this->Begin <= this->getBuffer().size());
    return this->getBuffer().size() - this->Begin;
  }

  /// Return true if no elements have yet been added to this struct or array.
  bool empty() const {
    return size() == 0;
  }

  /// Abandon this builder completely.
  void abandon() {
    markFinished();
    Builder.abandon(Begin);
  }

  /// Add a new value to this initializer.
  void add(llvm::Constant *value) {
    assert(value && "adding null value to constant initializer");
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.push_back(value);
  }

  /// Add an integer value of type size_t.
  void addSize(CharUnits size);

  /// Add an integer value of a specific type.
  void addInt(llvm::IntegerType *intTy, uint64_t value,
              bool isSigned = false) {
    add(llvm::ConstantInt::get(intTy, value, isSigned));
  }

  /// Add a null pointer of a specific type.
  void addNullPointer(llvm::PointerType *ptrTy) {
    add(llvm::ConstantPointerNull::get(ptrTy));
  }

  /// Add a bitcast of a value to a specific type.
  void addBitCast(llvm::Constant *value, llvm::Type *type) {
    add(llvm::ConstantExpr::getBitCast(value, type));
  }

  /// Add a bunch of new values to this initializer.
  void addAll(llvm::ArrayRef<llvm::Constant *> values) {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.append(values.begin(), values.end());
  }

  /// Add a relative offset to the given target address, i.e. the
  /// static difference between the target address and the address
  /// of the relative offset.  The target must be known to be defined
  /// in the current linkage unit.  The offset will have the given
  /// integer type, which must be no wider than intptr_t.  Some
  /// targets may not fully support this operation.
  void addRelativeOffset(llvm::IntegerType *type, llvm::Constant *target) {
    add(getRelativeOffset(type, target));
  }

  /// Add a relative offset to the target address, plus a small
  /// constant offset.  This is primarily useful when the relative
  /// offset is known to be a multiple of (say) four and therefore
  /// the tag can be used to express an extra two bits of information.
  void addTaggedRelativeOffset(llvm::IntegerType *type,
                               llvm::Constant *address,
                               unsigned tag) {
    llvm::Constant *offset = getRelativeOffset(type, address);
    if (tag) {
      offset = llvm::ConstantExpr::getAdd(offset,
                                          llvm::ConstantInt::get(type, tag));
    }
    add(offset);
  }

  /// Return the offset from the start of the initializer to the
  /// next position, assuming no padding is required prior to it.
  ///
  /// This operation will not succeed if any unsized placeholders are
  /// currently in place in the initializer.
  CharUnits getNextOffsetFromGlobal() const {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    return getOffsetFromGlobalTo(Builder.Buffer.size());
  }

  /// An opaque class to hold the abstract position of a placeholder.
  class PlaceholderPosition {
    size_t Index;
    friend class ConstantAggregateBuilderBase;
    PlaceholderPosition(size_t index) : Index(index) {}
  };

  /// Add a placeholder value to the structure.  The returned position
  /// can be used to set the value later; it will not be invalidated by
  /// any intermediate operations except (1) filling the same position or
  /// (2) finishing the entire builder.
  ///
  /// This is useful for emitting certain kinds of structure which
  /// contain some sort of summary field, generally a count, before any
  /// of the data.  By emitting a placeholder first, the structure can
  /// be emitted eagerly.
  PlaceholderPosition addPlaceholder() {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.push_back(nullptr);
    return Builder.Buffer.size() - 1;
  }

  /// Add a placeholder, giving the expected type that will be filled in.
  PlaceholderPosition addPlaceholderWithSize(llvm::Type *expectedType);

  /// Fill a previously-added placeholder.
  void fillPlaceholderWithInt(PlaceholderPosition position,
                              llvm::IntegerType *type, uint64_t value,
                              bool isSigned = false) {
    fillPlaceholder(position, llvm::ConstantInt::get(type, value, isSigned));
  }

  /// Fill a previously-added placeholder.
  void fillPlaceholder(PlaceholderPosition position, llvm::Constant *value) {
    assert(!Finished && "cannot change values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    llvm::Constant *&slot = Builder.Buffer[position.Index];
    assert(slot == nullptr && "placeholder already filled");
    slot = value;
  }

  /// Produce an address which will eventually point to the next
  /// position to be filled.  This is computed with an indexed
  /// getelementptr rather than by computing offsets.
  ///
  /// The returned pointer will have type T*, where T is the given
  /// position.
  llvm::Constant *getAddrOfCurrentPosition(llvm::Type *type);

  llvm::ArrayRef<llvm::Constant*> getGEPIndicesToCurrentPosition(
                           llvm::SmallVectorImpl<llvm::Constant*> &indices) {
    getGEPIndicesTo(indices, Builder.Buffer.size());
    return indices;
  }

protected:
  llvm::Constant *finishArray(llvm::Type *eltTy);
  llvm::Constant *finishStruct(llvm::StructType *structTy);

private:
  void getGEPIndicesTo(llvm::SmallVectorImpl<llvm::Constant*> &indices,
                       size_t position) const;

  llvm::Constant *getRelativeOffset(llvm::IntegerType *offsetType,
                                    llvm::Constant *target);

  CharUnits getOffsetFromGlobalTo(size_t index) const;
};

template <class Impl, class Traits>
class ConstantAggregateBuilderTemplateBase
    : public Traits::AggregateBuilderBase {
  using super = typename Traits::AggregateBuilderBase;
public:
  using InitBuilder = typename Traits::InitBuilder;
  using ArrayBuilder = typename Traits::ArrayBuilder;
  using StructBuilder = typename Traits::StructBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

protected:
  ConstantAggregateBuilderTemplateBase(InitBuilder &builder,
                                       AggregateBuilderBase *parent)
    : super(builder, parent) {}

  Impl &asImpl() { return *static_cast<Impl*>(this); }

public:
  ArrayBuilder beginArray(llvm::Type *eltTy = nullptr) {
    return ArrayBuilder(static_cast<InitBuilder&>(this->Builder), this, eltTy);
  }

  StructBuilder beginStruct(llvm::StructType *ty = nullptr) {
    return StructBuilder(static_cast<InitBuilder&>(this->Builder), this, ty);
  }

  /// Given that this builder was created by beginning an array or struct
  /// component on the given parent builder, finish the array/struct
  /// component and add it to the parent.
  ///
  /// It is an intentional choice that the parent is passed in explicitly
  /// despite it being redundant with information already kept in the
  /// builder.  This aids in readability by making it easier to find the
  /// places that add components to a builder, as well as "bookending"
  /// the sub-builder more explicitly.
  void finishAndAddTo(AggregateBuilderBase &parent) {
    assert(this->Parent == &parent && "adding to non-parent builder");
    parent.add(asImpl().finishImpl());
  }

  /// Given that this builder was created by beginning an array or struct
  /// directly on a ConstantInitBuilder, finish the array/struct and
  /// create a global variable with it as the initializer.
  template <class... As>
  llvm::GlobalVariable *finishAndCreateGlobal(As &&...args) {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.createGlobal(asImpl().finishImpl(),
                                      std::forward<As>(args)...);
  }

  /// Given that this builder was created by beginning an array or struct
  /// directly on a ConstantInitBuilder, finish the array/struct and
  /// set it as the initializer of the given global variable.
  void finishAndSetAsInitializer(llvm::GlobalVariable *global) {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.setGlobalInitializer(global, asImpl().finishImpl());
  }

  /// Given that this builder was created by beginning an array or struct
  /// directly on a ConstantInitBuilder, finish the array/struct and
  /// return a future which can be used to install the initializer in
  /// a global later.
  ///
  /// This is useful for allowing a finished initializer to passed to
  /// an API which will build the global.  However, the "future" preserves
  /// a dependency on the original builder; it is an error to pass it aside.
  ConstantInitFuture finishAndCreateFuture() {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.createFuture(asImpl().finishImpl());
  }
};

template <class Traits>
class ConstantArrayBuilderTemplateBase
  : public ConstantAggregateBuilderTemplateBase<typename Traits::ArrayBuilder,
                                                Traits> {
  using super =
    ConstantAggregateBuilderTemplateBase<typename Traits::ArrayBuilder, Traits>;

public:
  using InitBuilder = typename Traits::InitBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

private:
  llvm::Type *EltTy;

  template <class, class>
  friend class ConstantAggregateBuilderTemplateBase;

protected:
  ConstantArrayBuilderTemplateBase(InitBuilder &builder,
                                   AggregateBuilderBase *parent,
                                   llvm::Type *eltTy)
    : super(builder, parent), EltTy(eltTy) {}

private:
  /// Form an array constant from the values that have been added to this
  /// builder.
  llvm::Constant *finishImpl() {
    return AggregateBuilderBase::finishArray(EltTy);
  }
};

/// A template class designed to allow other frontends to
/// easily customize the builder classes used by ConstantInitBuilder,
/// and thus to extend the API to work with the abstractions they
/// prefer.  This would probably not be necessary if C++ just
/// supported extension methods.
template <class Traits>
class ConstantStructBuilderTemplateBase
  : public ConstantAggregateBuilderTemplateBase<typename Traits::StructBuilder,
                                                Traits> {
  using super =
    ConstantAggregateBuilderTemplateBase<typename Traits::StructBuilder,Traits>;

public:
  using InitBuilder = typename Traits::InitBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

private:
  llvm::StructType *StructTy;

  template <class, class>
  friend class ConstantAggregateBuilderTemplateBase;

protected:
  ConstantStructBuilderTemplateBase(InitBuilder &builder,
                                    AggregateBuilderBase *parent,
                                    llvm::StructType *structTy)
    : super(builder, parent), StructTy(structTy) {
    if (structTy) this->Packed = structTy->isPacked();
  }

public:
  void setPacked(bool packed) {
    this->Packed = packed;
  }

  /// Use the given type for the struct if its element count is correct.
  /// Don't add more elements after calling this.
  void suggestType(llvm::StructType *structTy) {
    if (this->size() == structTy->getNumElements()) {
      StructTy = structTy;
    }
  }

private:
  /// Form an array constant from the values that have been added to this
  /// builder.
  llvm::Constant *finishImpl() {
    return AggregateBuilderBase::finishStruct(StructTy);
  }
};

/// A template class designed to allow other frontends to
/// easily customize the builder classes used by ConstantInitBuilder,
/// and thus to extend the API to work with the abstractions they
/// prefer.  This would probably not be necessary if C++ just
/// supported extension methods.
template <class Traits>
class ConstantInitBuilderTemplateBase : public ConstantInitBuilderBase {
protected:
  ConstantInitBuilderTemplateBase(CodeGenModule &CGM)
    : ConstantInitBuilderBase(CGM) {}

public:
  using InitBuilder = typename Traits::InitBuilder;
  using ArrayBuilder = typename Traits::ArrayBuilder;
  using StructBuilder = typename Traits::StructBuilder;

  ArrayBuilder beginArray(llvm::Type *eltTy = nullptr) {
    return ArrayBuilder(static_cast<InitBuilder&>(*this), nullptr, eltTy);
  }

  StructBuilder beginStruct(llvm::StructType *structTy = nullptr) {
    return StructBuilder(static_cast<InitBuilder&>(*this), nullptr, structTy);
  }
};

class ConstantInitBuilder;
class ConstantStructBuilder;
class ConstantArrayBuilder;

struct ConstantInitBuilderTraits {
  using InitBuilder = ConstantInitBuilder;
  using AggregateBuilderBase = ConstantAggregateBuilderBase;
  using ArrayBuilder = ConstantArrayBuilder;
  using StructBuilder = ConstantStructBuilder;
};

/// The standard implementation of ConstantInitBuilder used in Clang.
class ConstantInitBuilder
    : public ConstantInitBuilderTemplateBase<ConstantInitBuilderTraits> {
public:
  explicit ConstantInitBuilder(CodeGenModule &CGM) :
    ConstantInitBuilderTemplateBase(CGM) {}
};

/// A helper class of ConstantInitBuilder, used for building constant
/// array initializers.
class ConstantArrayBuilder
    : public ConstantArrayBuilderTemplateBase<ConstantInitBuilderTraits> {
  template <class Traits>
  friend class ConstantInitBuilderTemplateBase;

  // The use of explicit qualification is a GCC workaround.
  template <class Impl, class Traits>
  friend class CodeGen::ConstantAggregateBuilderTemplateBase;

  ConstantArrayBuilder(ConstantInitBuilder &builder,
                       ConstantAggregateBuilderBase *parent,
                       llvm::Type *eltTy)
    : ConstantArrayBuilderTemplateBase(builder, parent, eltTy) {}
};

/// A helper class of ConstantInitBuilder, used for building constant
/// struct initializers.
class ConstantStructBuilder
    : public ConstantStructBuilderTemplateBase<ConstantInitBuilderTraits> {
  template <class Traits>
  friend class ConstantInitBuilderTemplateBase;

  // The use of explicit qualification is a GCC workaround.
  template <class Impl, class Traits>
  friend class CodeGen::ConstantAggregateBuilderTemplateBase;

  ConstantStructBuilder(ConstantInitBuilder &builder,
                        ConstantAggregateBuilderBase *parent,
                        llvm::StructType *structTy)
    : ConstantStructBuilderTemplateBase(builder, parent, structTy) {}
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
