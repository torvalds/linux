//===- IPDBRawSymbol.h - base interface for PDB symbol types ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBRAWSYMBOL_H
#define LLVM_DEBUGINFO_PDB_IPDBRAWSYMBOL_H

#include "PDBTypes.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include <memory>

namespace llvm {
class raw_ostream;

namespace pdb {

class IPDBSession;
class PDBSymbolTypeVTable;
class PDBSymbolTypeVTableShape;

enum class PdbSymbolIdField : uint32_t {
  None = 0,
  SymIndexId = 1 << 0,
  LexicalParent = 1 << 1,
  ClassParent = 1 << 2,
  Type = 1 << 3,
  UnmodifiedType = 1 << 4,
  All = 0xFFFFFFFF,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ All)
};

void dumpSymbolIdField(raw_ostream &OS, StringRef Name, SymIndexId Value,
                       int Indent, const IPDBSession &Session,
                       PdbSymbolIdField FieldId, PdbSymbolIdField ShowFlags,
                       PdbSymbolIdField RecurseFlags);

/// IPDBRawSymbol defines an interface used to represent an arbitrary symbol.
/// It exposes a monolithic interface consisting of accessors for the union of
/// all properties that are valid for any symbol type.  This interface is then
/// wrapped by a concrete class which exposes only those set of methods valid
/// for this particular symbol type.  See PDBSymbol.h for more details.
class IPDBRawSymbol {
public:
  virtual ~IPDBRawSymbol();

  virtual void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
                    PdbSymbolIdField RecurseIdFields) const = 0;

  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const = 0;

  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type, StringRef Name,
               PDB_NameSearchFlags Flags) const = 0;
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByAddr(PDB_SymType Type, StringRef Name,
                     PDB_NameSearchFlags Flags,
                     uint32_t Section, uint32_t Offset) const = 0;
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
                   uint64_t VA) const = 0;
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByRVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
                    uint32_t RVA) const = 0;

  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByAddr(uint32_t Section, uint32_t Offset) const = 0;
  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByRVA(uint32_t RVA) const = 0;
  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByVA(uint64_t VA) const = 0;

  virtual std::unique_ptr<IPDBEnumLineNumbers> findInlineeLines() const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByAddr(uint32_t Section, uint32_t Offset,
                         uint32_t Length) const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const = 0;

  virtual void getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes) const = 0;
  virtual void getBackEndVersion(VersionInfo &Version) const = 0;
  virtual PDB_MemberAccess getAccess() const = 0;
  virtual uint32_t getAddressOffset() const = 0;
  virtual uint32_t getAddressSection() const = 0;
  virtual uint32_t getAge() const = 0;
  virtual SymIndexId getArrayIndexTypeId() const = 0;
  virtual uint32_t getBaseDataOffset() const = 0;
  virtual uint32_t getBaseDataSlot() const = 0;
  virtual SymIndexId getBaseSymbolId() const = 0;
  virtual PDB_BuiltinType getBuiltinType() const = 0;
  virtual uint32_t getBitPosition() const = 0;
  virtual PDB_CallingConv getCallingConvention() const = 0;
  virtual SymIndexId getClassParentId() const = 0;
  virtual std::string getCompilerName() const = 0;
  virtual uint32_t getCount() const = 0;
  virtual uint32_t getCountLiveRanges() const = 0;
  virtual void getFrontEndVersion(VersionInfo &Version) const = 0;
  virtual PDB_Lang getLanguage() const = 0;
  virtual SymIndexId getLexicalParentId() const = 0;
  virtual std::string getLibraryName() const = 0;
  virtual uint32_t getLiveRangeStartAddressOffset() const = 0;
  virtual uint32_t getLiveRangeStartAddressSection() const = 0;
  virtual uint32_t getLiveRangeStartRelativeVirtualAddress() const = 0;
  virtual codeview::RegisterId getLocalBasePointerRegisterId() const = 0;
  virtual SymIndexId getLowerBoundId() const = 0;
  virtual uint32_t getMemorySpaceKind() const = 0;
  virtual std::string getName() const = 0;
  virtual uint32_t getNumberOfAcceleratorPointerTags() const = 0;
  virtual uint32_t getNumberOfColumns() const = 0;
  virtual uint32_t getNumberOfModifiers() const = 0;
  virtual uint32_t getNumberOfRegisterIndices() const = 0;
  virtual uint32_t getNumberOfRows() const = 0;
  virtual std::string getObjectFileName() const = 0;
  virtual uint32_t getOemId() const = 0;
  virtual SymIndexId getOemSymbolId() const = 0;
  virtual uint32_t getOffsetInUdt() const = 0;
  virtual PDB_Cpu getPlatform() const = 0;
  virtual uint32_t getRank() const = 0;
  virtual codeview::RegisterId getRegisterId() const = 0;
  virtual uint32_t getRegisterType() const = 0;
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  virtual uint32_t getSamplerSlot() const = 0;
  virtual uint32_t getSignature() const = 0;
  virtual uint32_t getSizeInUdt() const = 0;
  virtual uint32_t getSlot() const = 0;
  virtual std::string getSourceFileName() const = 0;
  virtual std::unique_ptr<IPDBLineNumber>
  getSrcLineOnTypeDefn() const = 0;
  virtual uint32_t getStride() const = 0;
  virtual SymIndexId getSubTypeId() const = 0;
  virtual std::string getSymbolsFileName() const = 0;
  virtual SymIndexId getSymIndexId() const = 0;
  virtual uint32_t getTargetOffset() const = 0;
  virtual uint32_t getTargetRelativeVirtualAddress() const = 0;
  virtual uint64_t getTargetVirtualAddress() const = 0;
  virtual uint32_t getTargetSection() const = 0;
  virtual uint32_t getTextureSlot() const = 0;
  virtual uint32_t getTimeStamp() const = 0;
  virtual uint32_t getToken() const = 0;
  virtual SymIndexId getTypeId() const = 0;
  virtual uint32_t getUavSlot() const = 0;
  virtual std::string getUndecoratedName() const = 0;
  virtual std::string getUndecoratedNameEx(PDB_UndnameFlags Flags) const = 0;
  virtual SymIndexId getUnmodifiedTypeId() const = 0;
  virtual SymIndexId getUpperBoundId() const = 0;
  virtual Variant getValue() const = 0;
  virtual uint32_t getVirtualBaseDispIndex() const = 0;
  virtual uint32_t getVirtualBaseOffset() const = 0;
  virtual std::unique_ptr<PDBSymbolTypeBuiltin>
  getVirtualBaseTableType() const = 0;
  virtual SymIndexId getVirtualTableShapeId() const = 0;
  virtual PDB_DataKind getDataKind() const = 0;
  virtual PDB_SymType getSymTag() const = 0;
  virtual codeview::GUID getGuid() const = 0;
  virtual int32_t getOffset() const = 0;
  virtual int32_t getThisAdjust() const = 0;
  virtual int32_t getVirtualBasePointerOffset() const = 0;
  virtual PDB_LocType getLocationType() const = 0;
  virtual PDB_Machine getMachineType() const = 0;
  virtual codeview::ThunkOrdinal getThunkOrdinal() const = 0;
  virtual uint64_t getLength() const = 0;
  virtual uint64_t getLiveRangeLength() const = 0;
  virtual uint64_t getVirtualAddress() const = 0;
  virtual PDB_UdtType getUdtKind() const = 0;
  virtual bool hasConstructor() const = 0;
  virtual bool hasCustomCallingConvention() const = 0;
  virtual bool hasFarReturn() const = 0;
  virtual bool isCode() const = 0;
  virtual bool isCompilerGenerated() const = 0;
  virtual bool isConstType() const = 0;
  virtual bool isEditAndContinueEnabled() const = 0;
  virtual bool isFunction() const = 0;
  virtual bool getAddressTaken() const = 0;
  virtual bool getNoStackOrdering() const = 0;
  virtual bool hasAlloca() const = 0;
  virtual bool hasAssignmentOperator() const = 0;
  virtual bool hasCTypes() const = 0;
  virtual bool hasCastOperator() const = 0;
  virtual bool hasDebugInfo() const = 0;
  virtual bool hasEH() const = 0;
  virtual bool hasEHa() const = 0;
  virtual bool hasFramePointer() const = 0;
  virtual bool hasInlAsm() const = 0;
  virtual bool hasInlineAttribute() const = 0;
  virtual bool hasInterruptReturn() const = 0;
  virtual bool hasLongJump() const = 0;
  virtual bool hasManagedCode() const = 0;
  virtual bool hasNestedTypes() const = 0;
  virtual bool hasNoInlineAttribute() const = 0;
  virtual bool hasNoReturnAttribute() const = 0;
  virtual bool hasOptimizedCodeDebugInfo() const = 0;
  virtual bool hasOverloadedOperator() const = 0;
  virtual bool hasSEH() const = 0;
  virtual bool hasSecurityChecks() const = 0;
  virtual bool hasSetJump() const = 0;
  virtual bool hasStrictGSCheck() const = 0;
  virtual bool isAcceleratorGroupSharedLocal() const = 0;
  virtual bool isAcceleratorPointerTagLiveRange() const = 0;
  virtual bool isAcceleratorStubFunction() const = 0;
  virtual bool isAggregated() const = 0;
  virtual bool isIntroVirtualFunction() const = 0;
  virtual bool isCVTCIL() const = 0;
  virtual bool isConstructorVirtualBase() const = 0;
  virtual bool isCxxReturnUdt() const = 0;
  virtual bool isDataAligned() const = 0;
  virtual bool isHLSLData() const = 0;
  virtual bool isHotpatchable() const = 0;
  virtual bool isIndirectVirtualBaseClass() const = 0;
  virtual bool isInterfaceUdt() const = 0;
  virtual bool isIntrinsic() const = 0;
  virtual bool isLTCG() const = 0;
  virtual bool isLocationControlFlowDependent() const = 0;
  virtual bool isMSILNetmodule() const = 0;
  virtual bool isMatrixRowMajor() const = 0;
  virtual bool isManagedCode() const = 0;
  virtual bool isMSILCode() const = 0;
  virtual bool isMultipleInheritance() const = 0;
  virtual bool isNaked() const = 0;
  virtual bool isNested() const = 0;
  virtual bool isOptimizedAway() const = 0;
  virtual bool isPacked() const = 0;
  virtual bool isPointerBasedOnSymbolValue() const = 0;
  virtual bool isPointerToDataMember() const = 0;
  virtual bool isPointerToMemberFunction() const = 0;
  virtual bool isPureVirtual() const = 0;
  virtual bool isRValueReference() const = 0;
  virtual bool isRefUdt() const = 0;
  virtual bool isReference() const = 0;
  virtual bool isRestrictedType() const = 0;
  virtual bool isReturnValue() const = 0;
  virtual bool isSafeBuffers() const = 0;
  virtual bool isScoped() const = 0;
  virtual bool isSdl() const = 0;
  virtual bool isSingleInheritance() const = 0;
  virtual bool isSplitted() const = 0;
  virtual bool isStatic() const = 0;
  virtual bool hasPrivateSymbols() const = 0;
  virtual bool isUnalignedType() const = 0;
  virtual bool isUnreached() const = 0;
  virtual bool isValueUdt() const = 0;
  virtual bool isVirtual() const = 0;
  virtual bool isVirtualBaseClass() const = 0;
  virtual bool isVirtualInheritance() const = 0;
  virtual bool isVolatileType() const = 0;
  virtual bool wasInlined() const = 0;
  virtual std::string getUnused() const = 0;
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

} // namespace pdb
} // namespace llvm

#endif
