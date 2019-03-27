//===-- llvm/GlobalObject.h - Class to represent global objects -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This represents an independent object. That is, a function or a global
// variable, but not an alias.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALOBJECT_H
#define LLVM_IR_GLOBALOBJECT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include <string>
#include <utility>

namespace llvm {

class Comdat;
class MDNode;
class Metadata;

class GlobalObject : public GlobalValue {
protected:
  GlobalObject(Type *Ty, ValueTy VTy, Use *Ops, unsigned NumOps,
               LinkageTypes Linkage, const Twine &Name,
               unsigned AddressSpace = 0)
      : GlobalValue(Ty, VTy, Ops, NumOps, Linkage, Name, AddressSpace),
        ObjComdat(nullptr) {
    setGlobalValueSubClassData(0);
  }

  Comdat *ObjComdat;
  enum {
    LastAlignmentBit = 4,
    HasMetadataHashEntryBit,
    HasSectionHashEntryBit,

    GlobalObjectBits,
  };
  static const unsigned GlobalObjectSubClassDataBits =
      GlobalValueSubClassDataBits - GlobalObjectBits;

private:
  static const unsigned AlignmentBits = LastAlignmentBit + 1;
  static const unsigned AlignmentMask = (1 << AlignmentBits) - 1;
  static const unsigned GlobalObjectMask = (1 << GlobalObjectBits) - 1;

public:
  GlobalObject(const GlobalObject &) = delete;

  unsigned getAlignment() const {
    unsigned Data = getGlobalValueSubClassData();
    unsigned AlignmentData = Data & AlignmentMask;
    return (1u << AlignmentData) >> 1;
  }
  void setAlignment(unsigned Align);

  unsigned getGlobalObjectSubClassData() const {
    unsigned ValueData = getGlobalValueSubClassData();
    return ValueData >> GlobalObjectBits;
  }

  void setGlobalObjectSubClassData(unsigned Val) {
    unsigned OldData = getGlobalValueSubClassData();
    setGlobalValueSubClassData((OldData & GlobalObjectMask) |
                               (Val << GlobalObjectBits));
    assert(getGlobalObjectSubClassData() == Val && "representation error");
  }

  /// Check if this global has a custom object file section.
  ///
  /// This is more efficient than calling getSection() and checking for an empty
  /// string.
  bool hasSection() const {
    return getGlobalValueSubClassData() & (1 << HasSectionHashEntryBit);
  }

  /// Get the custom section of this global if it has one.
  ///
  /// If this global does not have a custom section, this will be empty and the
  /// default object file section (.text, .data, etc) will be used.
  StringRef getSection() const {
    return hasSection() ? getSectionImpl() : StringRef();
  }

  /// Change the section for this global.
  ///
  /// Setting the section to the empty string tells LLVM to choose an
  /// appropriate default object file section.
  void setSection(StringRef S);

  bool hasComdat() const { return getComdat() != nullptr; }
  const Comdat *getComdat() const { return ObjComdat; }
  Comdat *getComdat() { return ObjComdat; }
  void setComdat(Comdat *C) { ObjComdat = C; }

  /// Check if this has any metadata.
  bool hasMetadata() const { return hasMetadataHashEntry(); }

  /// Check if this has any metadata of the given kind.
  bool hasMetadata(unsigned KindID) const {
    return getMetadata(KindID) != nullptr;
  }
  bool hasMetadata(StringRef Kind) const {
    return getMetadata(Kind) != nullptr;
  }

  /// Get the current metadata attachments for the given kind, if any.
  ///
  /// These functions require that the function have at most a single attachment
  /// of the given kind, and return \c nullptr if such an attachment is missing.
  /// @{
  MDNode *getMetadata(unsigned KindID) const;
  MDNode *getMetadata(StringRef Kind) const;
  /// @}

  /// Appends all attachments with the given ID to \c MDs in insertion order.
  /// If the global has no attachments with the given ID, or if ID is invalid,
  /// leaves MDs unchanged.
  /// @{
  void getMetadata(unsigned KindID, SmallVectorImpl<MDNode *> &MDs) const;
  void getMetadata(StringRef Kind, SmallVectorImpl<MDNode *> &MDs) const;
  /// @}

  /// Set a particular kind of metadata attachment.
  ///
  /// Sets the given attachment to \c MD, erasing it if \c MD is \c nullptr or
  /// replacing it if it already exists.
  /// @{
  void setMetadata(unsigned KindID, MDNode *MD);
  void setMetadata(StringRef Kind, MDNode *MD);
  /// @}

  /// Add a metadata attachment.
  /// @{
  void addMetadata(unsigned KindID, MDNode &MD);
  void addMetadata(StringRef Kind, MDNode &MD);
  /// @}

  /// Appends all attachments for the global to \c MDs, sorting by attachment
  /// ID. Attachments with the same ID appear in insertion order.
  void
  getAllMetadata(SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const;

  /// Erase all metadata attachments with the given kind.
  ///
  /// \returns true if any metadata was removed.
  bool eraseMetadata(unsigned KindID);

  /// Copy metadata from Src, adjusting offsets by Offset.
  void copyMetadata(const GlobalObject *Src, unsigned Offset);

  void addTypeMetadata(unsigned Offset, Metadata *TypeID);

protected:
  void copyAttributesFrom(const GlobalObject *Src);

public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal ||
           V->getValueID() == Value::GlobalVariableVal;
  }

  void clearMetadata();

private:
  void setGlobalObjectFlag(unsigned Bit, bool Val) {
    unsigned Mask = 1 << Bit;
    setGlobalValueSubClassData((~Mask & getGlobalValueSubClassData()) |
                               (Val ? Mask : 0u));
  }

  bool hasMetadataHashEntry() const {
    return getGlobalValueSubClassData() & (1 << HasMetadataHashEntryBit);
  }
  void setHasMetadataHashEntry(bool HasEntry) {
    setGlobalObjectFlag(HasMetadataHashEntryBit, HasEntry);
  }

  StringRef getSectionImpl() const;
};

} // end namespace llvm

#endif // LLVM_IR_GLOBALOBJECT_H
