//===- DIARawSymbol.cpp - DIA implementation of IPDBRawSymbol ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIARawSymbol.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/PDB/DIA/DIAEnumLineNumbers.h"
#include "llvm/DebugInfo/PDB/DIA/DIAEnumSymbols.h"
#include "llvm/DebugInfo/PDB/DIA/DIALineNumber.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTableShape.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::pdb;

namespace {
Variant VariantFromVARIANT(const VARIANT &V) {
  Variant Result;
  switch (V.vt) {
  case VT_I1:
    Result.Value.Int8 = V.cVal;
    Result.Type = PDB_VariantType::Int8;
    break;
  case VT_I2:
    Result.Value.Int16 = V.iVal;
    Result.Type = PDB_VariantType::Int16;
    break;
  case VT_I4:
    Result.Value.Int32 = V.intVal;
    Result.Type = PDB_VariantType::Int32;
    break;
  case VT_I8:
    Result.Value.Int64 = V.llVal;
    Result.Type = PDB_VariantType::Int64;
    break;
  case VT_UI1:
    Result.Value.UInt8 = V.bVal;
    Result.Type = PDB_VariantType::UInt8;
    break;
  case VT_UI2:
    Result.Value.UInt16 = V.uiVal;
    Result.Type = PDB_VariantType::UInt16;
    break;
  case VT_UI4:
    Result.Value.UInt32 = V.uintVal;
    Result.Type = PDB_VariantType::UInt32;
    break;
  case VT_UI8:
    Result.Value.UInt64 = V.ullVal;
    Result.Type = PDB_VariantType::UInt64;
    break;
  case VT_BOOL:
    Result.Value.Bool = (V.boolVal == VARIANT_TRUE) ? true : false;
    Result.Type = PDB_VariantType::Bool;
    break;
  case VT_R4:
    Result.Value.Single = V.fltVal;
    Result.Type = PDB_VariantType::Single;
    break;
  case VT_R8:
    Result.Value.Double = V.dblVal;
    Result.Type = PDB_VariantType::Double;
    break;
  case VT_BSTR: {
    const char *SrcBytes = reinterpret_cast<const char *>(V.bstrVal);
    llvm::ArrayRef<char> SrcByteArray(SrcBytes, SysStringByteLen(V.bstrVal));
    std::string Result8;
    if (!llvm::convertUTF16ToUTF8String(SrcByteArray, Result8))
      Result.Value.String = nullptr;
    Result.Value.String = new char[Result8.length() + 1];
    ::strcpy(Result.Value.String, Result8.c_str());
    Result.Type = PDB_VariantType::String;
    break;
  }
  default:
    Result.Type = PDB_VariantType::Unknown;
    break;
  }
  return Result;
}

template <typename ArgType>
ArgType PrivateGetDIAValue(IDiaSymbol *Symbol,
                           HRESULT (__stdcall IDiaSymbol::*Method)(ArgType *)) {
  ArgType Value;
  if (S_OK == (Symbol->*Method)(&Value))
    return static_cast<ArgType>(Value);

  return ArgType();
}

template <typename ArgType, typename RetType>
RetType PrivateGetDIAValue(IDiaSymbol *Symbol,
                           HRESULT (__stdcall IDiaSymbol::*Method)(ArgType *)) {
  ArgType Value;
  if (S_OK == (Symbol->*Method)(&Value))
    return static_cast<RetType>(Value);

  return RetType();
}

std::string
PrivateGetDIAValue(IDiaSymbol *Symbol,
                   HRESULT (__stdcall IDiaSymbol::*Method)(BSTR *)) {
  return invokeBstrMethod(*Symbol, Method);
}

codeview::GUID
PrivateGetDIAValue(IDiaSymbol *Symbol,
                   HRESULT (__stdcall IDiaSymbol::*Method)(GUID *)) {
  GUID Result;
  if (S_OK != (Symbol->*Method)(&Result))
    return codeview::GUID();

  static_assert(sizeof(codeview::GUID) == sizeof(GUID),
                "GUID is the wrong size!");
  codeview::GUID IdResult;
  ::memcpy(&IdResult, &Result, sizeof(GUID));
  return IdResult;
}

template <typename PrintType, typename ArgType>
void DumpDIAValueAs(llvm::raw_ostream &OS, int Indent, StringRef Name,
                    IDiaSymbol *Symbol,
                    HRESULT (__stdcall IDiaSymbol::*Method)(ArgType *)) {
  ArgType Value;
  if (S_OK == (Symbol->*Method)(&Value))
    dumpSymbolField(OS, Name, static_cast<PrintType>(Value), Indent);
}

void DumpDIAIdValue(llvm::raw_ostream &OS, int Indent, StringRef Name,
                    IDiaSymbol *Symbol,
                    HRESULT (__stdcall IDiaSymbol::*Method)(DWORD *),
                    const IPDBSession &Session, PdbSymbolIdField FieldId,
                    PdbSymbolIdField ShowFlags, PdbSymbolIdField RecurseFlags) {
  DWORD Value;
  if (S_OK == (Symbol->*Method)(&Value))
    dumpSymbolIdField(OS, Name, Value, Indent, Session, FieldId, ShowFlags,
                      RecurseFlags);
}

template <typename ArgType>
void DumpDIAValue(llvm::raw_ostream &OS, int Indent, StringRef Name,
                  IDiaSymbol *Symbol,
                  HRESULT (__stdcall IDiaSymbol::*Method)(ArgType *)) {
  ArgType Value;
  if (S_OK == (Symbol->*Method)(&Value))
    dumpSymbolField(OS, Name, Value, Indent);
}

void DumpDIAValue(llvm::raw_ostream &OS, int Indent, StringRef Name,
                  IDiaSymbol *Symbol,
                  HRESULT (__stdcall IDiaSymbol::*Method)(BSTR *)) {
  BSTR Value = nullptr;
  if (S_OK != (Symbol->*Method)(&Value))
    return;
  const char *Bytes = reinterpret_cast<const char *>(Value);
  ArrayRef<char> ByteArray(Bytes, ::SysStringByteLen(Value));
  std::string Result;
  if (llvm::convertUTF16ToUTF8String(ByteArray, Result))
    dumpSymbolField(OS, Name, Result, Indent);
  ::SysFreeString(Value);
}

void DumpDIAValue(llvm::raw_ostream &OS, int Indent, StringRef Name,
                  IDiaSymbol *Symbol,
                  HRESULT (__stdcall IDiaSymbol::*Method)(VARIANT *)) {
  VARIANT Value;
  Value.vt = VT_EMPTY;
  if (S_OK != (Symbol->*Method)(&Value))
    return;
  Variant V = VariantFromVARIANT(Value);

  dumpSymbolField(OS, Name, V, Indent);
}
} // namespace

namespace llvm {
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const GUID &G) {
  StringRef GuidBytes(reinterpret_cast<const char *>(&G), sizeof(G));
  codeview::detail::GuidAdapter A(GuidBytes);
  A.format(OS, "");
  return OS;
}
} // namespace llvm

DIARawSymbol::DIARawSymbol(const DIASession &PDBSession,
                           CComPtr<IDiaSymbol> DiaSymbol)
    : Session(PDBSession), Symbol(DiaSymbol) {}

#define RAW_ID_METHOD_DUMP(Stream, Method, Session, FieldId, ShowFlags,        \
                           RecurseFlags)                                       \
  DumpDIAIdValue(Stream, Indent, StringRef{#Method}, Symbol,                   \
                 &IDiaSymbol::get_##Method, Session, FieldId, ShowFlags,       \
                 RecurseFlags);

#define RAW_METHOD_DUMP(Stream, Method)                                        \
  DumpDIAValue(Stream, Indent, StringRef{#Method}, Symbol,                     \
               &IDiaSymbol::get_##Method);

#define RAW_METHOD_DUMP_AS(Stream, Method, Type)                               \
  DumpDIAValueAs<Type>(Stream, Indent, StringRef{#Method}, Symbol,             \
                       &IDiaSymbol::get_##Method);

void DIARawSymbol::dump(raw_ostream &OS, int Indent,
                        PdbSymbolIdField ShowIdFields,
                        PdbSymbolIdField RecurseIdFields) const {
  RAW_ID_METHOD_DUMP(OS, symIndexId, Session, PdbSymbolIdField::SymIndexId,
                     ShowIdFields, RecurseIdFields);
  RAW_METHOD_DUMP_AS(OS, symTag, PDB_SymType);

  RAW_METHOD_DUMP(OS, access);
  RAW_METHOD_DUMP(OS, addressOffset);
  RAW_METHOD_DUMP(OS, addressSection);
  RAW_METHOD_DUMP(OS, age);
  RAW_METHOD_DUMP(OS, arrayIndexTypeId);
  RAW_METHOD_DUMP(OS, backEndMajor);
  RAW_METHOD_DUMP(OS, backEndMinor);
  RAW_METHOD_DUMP(OS, backEndBuild);
  RAW_METHOD_DUMP(OS, backEndQFE);
  RAW_METHOD_DUMP(OS, baseDataOffset);
  RAW_METHOD_DUMP(OS, baseDataSlot);
  RAW_METHOD_DUMP(OS, baseSymbolId);
  RAW_METHOD_DUMP_AS(OS, baseType, PDB_BuiltinType);
  RAW_METHOD_DUMP(OS, bitPosition);
  RAW_METHOD_DUMP_AS(OS, callingConvention, PDB_CallingConv);
  RAW_ID_METHOD_DUMP(OS, classParentId, Session, PdbSymbolIdField::ClassParent,
                     ShowIdFields, RecurseIdFields);
  RAW_METHOD_DUMP(OS, compilerName);
  RAW_METHOD_DUMP(OS, count);
  RAW_METHOD_DUMP(OS, countLiveRanges);
  RAW_METHOD_DUMP(OS, frontEndMajor);
  RAW_METHOD_DUMP(OS, frontEndMinor);
  RAW_METHOD_DUMP(OS, frontEndBuild);
  RAW_METHOD_DUMP(OS, frontEndQFE);
  RAW_ID_METHOD_DUMP(OS, lexicalParentId, Session,
                     PdbSymbolIdField::LexicalParent, ShowIdFields,
                     RecurseIdFields);
  RAW_METHOD_DUMP(OS, libraryName);
  RAW_METHOD_DUMP(OS, liveRangeStartAddressOffset);
  RAW_METHOD_DUMP(OS, liveRangeStartAddressSection);
  RAW_METHOD_DUMP(OS, liveRangeStartRelativeVirtualAddress);
  RAW_METHOD_DUMP(OS, localBasePointerRegisterId);
  RAW_METHOD_DUMP(OS, lowerBoundId);
  RAW_METHOD_DUMP(OS, memorySpaceKind);
  RAW_METHOD_DUMP(OS, name);
  RAW_METHOD_DUMP(OS, numberOfAcceleratorPointerTags);
  RAW_METHOD_DUMP(OS, numberOfColumns);
  RAW_METHOD_DUMP(OS, numberOfModifiers);
  RAW_METHOD_DUMP(OS, numberOfRegisterIndices);
  RAW_METHOD_DUMP(OS, numberOfRows);
  RAW_METHOD_DUMP(OS, objectFileName);
  RAW_METHOD_DUMP(OS, oemId);
  RAW_METHOD_DUMP(OS, oemSymbolId);
  RAW_METHOD_DUMP(OS, offsetInUdt);
  RAW_METHOD_DUMP(OS, platform);
  RAW_METHOD_DUMP(OS, rank);
  RAW_METHOD_DUMP(OS, registerId);
  RAW_METHOD_DUMP(OS, registerType);
  RAW_METHOD_DUMP(OS, relativeVirtualAddress);
  RAW_METHOD_DUMP(OS, samplerSlot);
  RAW_METHOD_DUMP(OS, signature);
  RAW_METHOD_DUMP(OS, sizeInUdt);
  RAW_METHOD_DUMP(OS, slot);
  RAW_METHOD_DUMP(OS, sourceFileName);
  RAW_METHOD_DUMP(OS, stride);
  RAW_METHOD_DUMP(OS, subTypeId);
  RAW_METHOD_DUMP(OS, symbolsFileName);
  RAW_METHOD_DUMP(OS, targetOffset);
  RAW_METHOD_DUMP(OS, targetRelativeVirtualAddress);
  RAW_METHOD_DUMP(OS, targetVirtualAddress);
  RAW_METHOD_DUMP(OS, targetSection);
  RAW_METHOD_DUMP(OS, textureSlot);
  RAW_METHOD_DUMP(OS, timeStamp);
  RAW_METHOD_DUMP(OS, token);
  RAW_ID_METHOD_DUMP(OS, typeId, Session, PdbSymbolIdField::Type, ShowIdFields,
                     RecurseIdFields);
  RAW_METHOD_DUMP(OS, uavSlot);
  RAW_METHOD_DUMP(OS, undecoratedName);
  RAW_ID_METHOD_DUMP(OS, unmodifiedTypeId, Session,
                     PdbSymbolIdField::UnmodifiedType, ShowIdFields,
                     RecurseIdFields);
  RAW_METHOD_DUMP(OS, upperBoundId);
  RAW_METHOD_DUMP(OS, virtualBaseDispIndex);
  RAW_METHOD_DUMP(OS, virtualBaseOffset);
  RAW_METHOD_DUMP(OS, virtualTableShapeId);
  RAW_METHOD_DUMP_AS(OS, dataKind, PDB_DataKind);
  RAW_METHOD_DUMP(OS, guid);
  RAW_METHOD_DUMP(OS, offset);
  RAW_METHOD_DUMP(OS, thisAdjust);
  RAW_METHOD_DUMP(OS, virtualBasePointerOffset);
  RAW_METHOD_DUMP_AS(OS, locationType, PDB_LocType);
  RAW_METHOD_DUMP(OS, machineType);
  RAW_METHOD_DUMP(OS, thunkOrdinal);
  RAW_METHOD_DUMP(OS, length);
  RAW_METHOD_DUMP(OS, liveRangeLength);
  RAW_METHOD_DUMP(OS, virtualAddress);
  RAW_METHOD_DUMP_AS(OS, udtKind, PDB_UdtType);
  RAW_METHOD_DUMP(OS, constructor);
  RAW_METHOD_DUMP(OS, customCallingConvention);
  RAW_METHOD_DUMP(OS, farReturn);
  RAW_METHOD_DUMP(OS, code);
  RAW_METHOD_DUMP(OS, compilerGenerated);
  RAW_METHOD_DUMP(OS, constType);
  RAW_METHOD_DUMP(OS, editAndContinueEnabled);
  RAW_METHOD_DUMP(OS, function);
  RAW_METHOD_DUMP(OS, stride);
  RAW_METHOD_DUMP(OS, noStackOrdering);
  RAW_METHOD_DUMP(OS, hasAlloca);
  RAW_METHOD_DUMP(OS, hasAssignmentOperator);
  RAW_METHOD_DUMP(OS, isCTypes);
  RAW_METHOD_DUMP(OS, hasCastOperator);
  RAW_METHOD_DUMP(OS, hasDebugInfo);
  RAW_METHOD_DUMP(OS, hasEH);
  RAW_METHOD_DUMP(OS, hasEHa);
  RAW_METHOD_DUMP(OS, hasInlAsm);
  RAW_METHOD_DUMP(OS, framePointerPresent);
  RAW_METHOD_DUMP(OS, inlSpec);
  RAW_METHOD_DUMP(OS, interruptReturn);
  RAW_METHOD_DUMP(OS, hasLongJump);
  RAW_METHOD_DUMP(OS, hasManagedCode);
  RAW_METHOD_DUMP(OS, hasNestedTypes);
  RAW_METHOD_DUMP(OS, noInline);
  RAW_METHOD_DUMP(OS, noReturn);
  RAW_METHOD_DUMP(OS, optimizedCodeDebugInfo);
  RAW_METHOD_DUMP(OS, overloadedOperator);
  RAW_METHOD_DUMP(OS, hasSEH);
  RAW_METHOD_DUMP(OS, hasSecurityChecks);
  RAW_METHOD_DUMP(OS, hasSetJump);
  RAW_METHOD_DUMP(OS, strictGSCheck);
  RAW_METHOD_DUMP(OS, isAcceleratorGroupSharedLocal);
  RAW_METHOD_DUMP(OS, isAcceleratorPointerTagLiveRange);
  RAW_METHOD_DUMP(OS, isAcceleratorStubFunction);
  RAW_METHOD_DUMP(OS, isAggregated);
  RAW_METHOD_DUMP(OS, intro);
  RAW_METHOD_DUMP(OS, isCVTCIL);
  RAW_METHOD_DUMP(OS, isConstructorVirtualBase);
  RAW_METHOD_DUMP(OS, isCxxReturnUdt);
  RAW_METHOD_DUMP(OS, isDataAligned);
  RAW_METHOD_DUMP(OS, isHLSLData);
  RAW_METHOD_DUMP(OS, isHotpatchable);
  RAW_METHOD_DUMP(OS, indirectVirtualBaseClass);
  RAW_METHOD_DUMP(OS, isInterfaceUdt);
  RAW_METHOD_DUMP(OS, intrinsic);
  RAW_METHOD_DUMP(OS, isLTCG);
  RAW_METHOD_DUMP(OS, isLocationControlFlowDependent);
  RAW_METHOD_DUMP(OS, isMSILNetmodule);
  RAW_METHOD_DUMP(OS, isMatrixRowMajor);
  RAW_METHOD_DUMP(OS, managed);
  RAW_METHOD_DUMP(OS, msil);
  RAW_METHOD_DUMP(OS, isMultipleInheritance);
  RAW_METHOD_DUMP(OS, isNaked);
  RAW_METHOD_DUMP(OS, nested);
  RAW_METHOD_DUMP(OS, isOptimizedAway);
  RAW_METHOD_DUMP(OS, packed);
  RAW_METHOD_DUMP(OS, isPointerBasedOnSymbolValue);
  RAW_METHOD_DUMP(OS, isPointerToDataMember);
  RAW_METHOD_DUMP(OS, isPointerToMemberFunction);
  RAW_METHOD_DUMP(OS, pure);
  RAW_METHOD_DUMP(OS, RValueReference);
  RAW_METHOD_DUMP(OS, isRefUdt);
  RAW_METHOD_DUMP(OS, reference);
  RAW_METHOD_DUMP(OS, restrictedType);
  RAW_METHOD_DUMP(OS, isReturnValue);
  RAW_METHOD_DUMP(OS, isSafeBuffers);
  RAW_METHOD_DUMP(OS, scoped);
  RAW_METHOD_DUMP(OS, isSdl);
  RAW_METHOD_DUMP(OS, isSingleInheritance);
  RAW_METHOD_DUMP(OS, isSplitted);
  RAW_METHOD_DUMP(OS, isStatic);
  RAW_METHOD_DUMP(OS, isStripped);
  RAW_METHOD_DUMP(OS, unalignedType);
  RAW_METHOD_DUMP(OS, notReached);
  RAW_METHOD_DUMP(OS, isValueUdt);
  RAW_METHOD_DUMP(OS, virtual);
  RAW_METHOD_DUMP(OS, virtualBaseClass);
  RAW_METHOD_DUMP(OS, isVirtualInheritance);
  RAW_METHOD_DUMP(OS, volatileType);
  RAW_METHOD_DUMP(OS, wasInlined);
  RAW_METHOD_DUMP(OS, unused);
  RAW_METHOD_DUMP(OS, value);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findChildren(PDB_SymType Type) const {
  enum SymTagEnum EnumVal = static_cast<enum SymTagEnum>(Type);

  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK !=
      Symbol->findChildrenEx(EnumVal, nullptr, nsNone, &DiaEnumerator)) {
    if (S_OK != Symbol->findChildren(EnumVal, nullptr, nsNone, &DiaEnumerator))
      return nullptr;
  }

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findChildren(PDB_SymType Type, StringRef Name,
                           PDB_NameSearchFlags Flags) const {
  llvm::SmallVector<UTF16, 32> Name16;
  llvm::convertUTF8ToUTF16String(Name, Name16);

  enum SymTagEnum EnumVal = static_cast<enum SymTagEnum>(Type);
  DWORD CompareFlags = static_cast<DWORD>(Flags);
  wchar_t *Name16Str = reinterpret_cast<wchar_t *>(Name16.data());

  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK !=
      Symbol->findChildrenEx(EnumVal, Name16Str, CompareFlags, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findChildrenByAddr(PDB_SymType Type, StringRef Name,
                                 PDB_NameSearchFlags Flags, uint32_t Section,
                                 uint32_t Offset) const {
  llvm::SmallVector<UTF16, 32> Name16;
  llvm::convertUTF8ToUTF16String(Name, Name16);

  enum SymTagEnum EnumVal = static_cast<enum SymTagEnum>(Type);

  DWORD CompareFlags = static_cast<DWORD>(Flags);
  wchar_t *Name16Str = reinterpret_cast<wchar_t *>(Name16.data());

  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findChildrenExByAddr(EnumVal, Name16Str, CompareFlags,
                                           Section, Offset, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findChildrenByVA(PDB_SymType Type, StringRef Name,
                               PDB_NameSearchFlags Flags, uint64_t VA) const {
  llvm::SmallVector<UTF16, 32> Name16;
  llvm::convertUTF8ToUTF16String(Name, Name16);

  enum SymTagEnum EnumVal = static_cast<enum SymTagEnum>(Type);

  DWORD CompareFlags = static_cast<DWORD>(Flags);
  wchar_t *Name16Str = reinterpret_cast<wchar_t *>(Name16.data());

  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findChildrenExByVA(EnumVal, Name16Str, CompareFlags, VA,
                                         &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findChildrenByRVA(PDB_SymType Type, StringRef Name,
                                PDB_NameSearchFlags Flags, uint32_t RVA) const {
  llvm::SmallVector<UTF16, 32> Name16;
  llvm::convertUTF8ToUTF16String(Name, Name16);

  enum SymTagEnum EnumVal = static_cast<enum SymTagEnum>(Type);
  DWORD CompareFlags = static_cast<DWORD>(Flags);
  wchar_t *Name16Str = reinterpret_cast<wchar_t *>(Name16.data());

  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findChildrenExByRVA(EnumVal, Name16Str, CompareFlags, RVA,
                                          &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findInlineFramesByAddr(uint32_t Section, uint32_t Offset) const {
  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findInlineFramesByAddr(Section, Offset, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findInlineFramesByRVA(uint32_t RVA) const {
  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findInlineFramesByRVA(RVA, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumSymbols>
DIARawSymbol::findInlineFramesByVA(uint64_t VA) const {
  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  if (S_OK != Symbol->findInlineFramesByVA(VA, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumSymbols>(Session, DiaEnumerator);
}

std::unique_ptr<IPDBEnumLineNumbers> DIARawSymbol::findInlineeLines() const {
  CComPtr<IDiaEnumLineNumbers> DiaEnumerator;
  if (S_OK != Symbol->findInlineeLines(&DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumLineNumbers>(DiaEnumerator);
}

std::unique_ptr<IPDBEnumLineNumbers>
DIARawSymbol::findInlineeLinesByAddr(uint32_t Section, uint32_t Offset,
                                     uint32_t Length) const {
  CComPtr<IDiaEnumLineNumbers> DiaEnumerator;
  if (S_OK !=
      Symbol->findInlineeLinesByAddr(Section, Offset, Length, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumLineNumbers>(DiaEnumerator);
}

std::unique_ptr<IPDBEnumLineNumbers>
DIARawSymbol::findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const {
  CComPtr<IDiaEnumLineNumbers> DiaEnumerator;
  if (S_OK != Symbol->findInlineeLinesByRVA(RVA, Length, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumLineNumbers>(DiaEnumerator);
}

std::unique_ptr<IPDBEnumLineNumbers>
DIARawSymbol::findInlineeLinesByVA(uint64_t VA, uint32_t Length) const {
  CComPtr<IDiaEnumLineNumbers> DiaEnumerator;
  if (S_OK != Symbol->findInlineeLinesByVA(VA, Length, &DiaEnumerator))
    return nullptr;

  return llvm::make_unique<DIAEnumLineNumbers>(DiaEnumerator);
}

void DIARawSymbol::getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes) const {
  bytes.clear();

  DWORD DataSize = 0;
  Symbol->get_dataBytes(0, &DataSize, nullptr);
  if (DataSize == 0)
    return;

  bytes.resize(DataSize);
  Symbol->get_dataBytes(DataSize, &DataSize, bytes.data());
}

std::string DIARawSymbol::getUndecoratedNameEx(PDB_UndnameFlags Flags) const {
  CComBSTR Result16;
  if (S_OK != Symbol->get_undecoratedNameEx((DWORD)Flags, &Result16))
    return std::string();

  const char *SrcBytes = reinterpret_cast<const char *>(Result16.m_str);
  llvm::ArrayRef<char> SrcByteArray(SrcBytes, Result16.ByteLength());
  std::string Result8;
  if (!llvm::convertUTF16ToUTF8String(SrcByteArray, Result8))
    return std::string();
  return Result8;
}

PDB_MemberAccess DIARawSymbol::getAccess() const {
  return PrivateGetDIAValue<DWORD, PDB_MemberAccess>(Symbol,
                                                     &IDiaSymbol::get_access);
}

uint32_t DIARawSymbol::getAddressOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_addressOffset);
}

uint32_t DIARawSymbol::getAddressSection() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_addressSection);
}

uint32_t DIARawSymbol::getAge() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_age);
}

SymIndexId DIARawSymbol::getArrayIndexTypeId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_arrayIndexTypeId);
}

void DIARawSymbol::getBackEndVersion(VersionInfo &Version) const {
  Version.Major = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_backEndMajor);
  Version.Minor = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_backEndMinor);
  Version.Build = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_backEndBuild);
  Version.QFE = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_backEndQFE);
}

uint32_t DIARawSymbol::getBaseDataOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_baseDataOffset);
}

uint32_t DIARawSymbol::getBaseDataSlot() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_baseDataSlot);
}

SymIndexId DIARawSymbol::getBaseSymbolId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_baseSymbolId);
}

PDB_BuiltinType DIARawSymbol::getBuiltinType() const {
  return PrivateGetDIAValue<DWORD, PDB_BuiltinType>(Symbol,
                                                    &IDiaSymbol::get_baseType);
}

uint32_t DIARawSymbol::getBitPosition() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_bitPosition);
}

PDB_CallingConv DIARawSymbol::getCallingConvention() const {
  return PrivateGetDIAValue<DWORD, PDB_CallingConv>(
      Symbol, &IDiaSymbol::get_callingConvention);
}

SymIndexId DIARawSymbol::getClassParentId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_classParentId);
}

std::string DIARawSymbol::getCompilerName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_compilerName);
}

uint32_t DIARawSymbol::getCount() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_count);
}

uint32_t DIARawSymbol::getCountLiveRanges() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_countLiveRanges);
}

void DIARawSymbol::getFrontEndVersion(VersionInfo &Version) const {
  Version.Major = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_frontEndMajor);
  Version.Minor = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_frontEndMinor);
  Version.Build = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_frontEndBuild);
  Version.QFE = PrivateGetDIAValue(Symbol, &IDiaSymbol::get_frontEndQFE);
}

PDB_Lang DIARawSymbol::getLanguage() const {
  return PrivateGetDIAValue<DWORD, PDB_Lang>(Symbol, &IDiaSymbol::get_language);
}

SymIndexId DIARawSymbol::getLexicalParentId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_lexicalParentId);
}

std::string DIARawSymbol::getLibraryName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_libraryName);
}

uint32_t DIARawSymbol::getLiveRangeStartAddressOffset() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_liveRangeStartAddressOffset);
}

uint32_t DIARawSymbol::getLiveRangeStartAddressSection() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_liveRangeStartAddressSection);
}

uint32_t DIARawSymbol::getLiveRangeStartRelativeVirtualAddress() const {
  return PrivateGetDIAValue(
      Symbol, &IDiaSymbol::get_liveRangeStartRelativeVirtualAddress);
}

codeview::RegisterId DIARawSymbol::getLocalBasePointerRegisterId() const {
  return PrivateGetDIAValue<DWORD, codeview::RegisterId>(
      Symbol, &IDiaSymbol::get_localBasePointerRegisterId);
}

SymIndexId DIARawSymbol::getLowerBoundId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_lowerBoundId);
}

uint32_t DIARawSymbol::getMemorySpaceKind() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_memorySpaceKind);
}

std::string DIARawSymbol::getName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_name);
}

uint32_t DIARawSymbol::getNumberOfAcceleratorPointerTags() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_numberOfAcceleratorPointerTags);
}

uint32_t DIARawSymbol::getNumberOfColumns() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_numberOfColumns);
}

uint32_t DIARawSymbol::getNumberOfModifiers() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_numberOfModifiers);
}

uint32_t DIARawSymbol::getNumberOfRegisterIndices() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_numberOfRegisterIndices);
}

uint32_t DIARawSymbol::getNumberOfRows() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_numberOfRows);
}

std::string DIARawSymbol::getObjectFileName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_objectFileName);
}

uint32_t DIARawSymbol::getOemId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_oemId);
}

SymIndexId DIARawSymbol::getOemSymbolId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_oemSymbolId);
}

uint32_t DIARawSymbol::getOffsetInUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_offsetInUdt);
}

PDB_Cpu DIARawSymbol::getPlatform() const {
  return PrivateGetDIAValue<DWORD, PDB_Cpu>(Symbol, &IDiaSymbol::get_platform);
}

uint32_t DIARawSymbol::getRank() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_rank);
}

codeview::RegisterId DIARawSymbol::getRegisterId() const {
  return PrivateGetDIAValue<DWORD, codeview::RegisterId>(
      Symbol, &IDiaSymbol::get_registerId);
}

uint32_t DIARawSymbol::getRegisterType() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_registerType);
}

uint32_t DIARawSymbol::getRelativeVirtualAddress() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_relativeVirtualAddress);
}

uint32_t DIARawSymbol::getSamplerSlot() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_samplerSlot);
}

uint32_t DIARawSymbol::getSignature() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_signature);
}

uint32_t DIARawSymbol::getSizeInUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_sizeInUdt);
}

uint32_t DIARawSymbol::getSlot() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_slot);
}

std::string DIARawSymbol::getSourceFileName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_sourceFileName);
}

std::unique_ptr<IPDBLineNumber> DIARawSymbol::getSrcLineOnTypeDefn() const {
  CComPtr<IDiaLineNumber> LineNumber;
  if (FAILED(Symbol->getSrcLineOnTypeDefn(&LineNumber)) || !LineNumber)
    return nullptr;

  return llvm::make_unique<DIALineNumber>(LineNumber);
}

uint32_t DIARawSymbol::getStride() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_stride);
}

SymIndexId DIARawSymbol::getSubTypeId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_subTypeId);
}

std::string DIARawSymbol::getSymbolsFileName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_symbolsFileName);
}

SymIndexId DIARawSymbol::getSymIndexId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_symIndexId);
}

uint32_t DIARawSymbol::getTargetOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_targetOffset);
}

uint32_t DIARawSymbol::getTargetRelativeVirtualAddress() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_targetRelativeVirtualAddress);
}

uint64_t DIARawSymbol::getTargetVirtualAddress() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_targetVirtualAddress);
}

uint32_t DIARawSymbol::getTargetSection() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_targetSection);
}

uint32_t DIARawSymbol::getTextureSlot() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_textureSlot);
}

uint32_t DIARawSymbol::getTimeStamp() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_timeStamp);
}

uint32_t DIARawSymbol::getToken() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_token);
}

SymIndexId DIARawSymbol::getTypeId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_typeId);
}

uint32_t DIARawSymbol::getUavSlot() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_uavSlot);
}

std::string DIARawSymbol::getUndecoratedName() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_undecoratedName);
}

SymIndexId DIARawSymbol::getUnmodifiedTypeId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_unmodifiedTypeId);
}

SymIndexId DIARawSymbol::getUpperBoundId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_upperBoundId);
}

Variant DIARawSymbol::getValue() const {
  VARIANT Value;
  Value.vt = VT_EMPTY;
  if (S_OK != Symbol->get_value(&Value))
    return Variant();

  return VariantFromVARIANT(Value);
}

uint32_t DIARawSymbol::getVirtualBaseDispIndex() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualBaseDispIndex);
}

uint32_t DIARawSymbol::getVirtualBaseOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualBaseOffset);
}

SymIndexId DIARawSymbol::getVirtualTableShapeId() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualTableShapeId);
}

std::unique_ptr<PDBSymbolTypeBuiltin>
DIARawSymbol::getVirtualBaseTableType() const {
  CComPtr<IDiaSymbol> TableType;
  if (FAILED(Symbol->get_virtualBaseTableType(&TableType)) || !TableType)
    return nullptr;

  auto RawVT = llvm::make_unique<DIARawSymbol>(Session, TableType);
  auto Pointer =
      PDBSymbol::createAs<PDBSymbolTypePointer>(Session, std::move(RawVT));
  return unique_dyn_cast<PDBSymbolTypeBuiltin>(Pointer->getPointeeType());
}

PDB_DataKind DIARawSymbol::getDataKind() const {
  return PrivateGetDIAValue<DWORD, PDB_DataKind>(Symbol,
                                                 &IDiaSymbol::get_dataKind);
}

PDB_SymType DIARawSymbol::getSymTag() const {
  return PrivateGetDIAValue<DWORD, PDB_SymType>(Symbol,
                                                &IDiaSymbol::get_symTag);
}

codeview::GUID DIARawSymbol::getGuid() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_guid);
}

int32_t DIARawSymbol::getOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_offset);
}

int32_t DIARawSymbol::getThisAdjust() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_thisAdjust);
}

int32_t DIARawSymbol::getVirtualBasePointerOffset() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualBasePointerOffset);
}

PDB_LocType DIARawSymbol::getLocationType() const {
  return PrivateGetDIAValue<DWORD, PDB_LocType>(Symbol,
                                                &IDiaSymbol::get_locationType);
}

PDB_Machine DIARawSymbol::getMachineType() const {
  return PrivateGetDIAValue<DWORD, PDB_Machine>(Symbol,
                                                &IDiaSymbol::get_machineType);
}

codeview::ThunkOrdinal DIARawSymbol::getThunkOrdinal() const {
  return PrivateGetDIAValue<DWORD, codeview::ThunkOrdinal>(
      Symbol, &IDiaSymbol::get_thunkOrdinal);
}

uint64_t DIARawSymbol::getLength() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_length);
}

uint64_t DIARawSymbol::getLiveRangeLength() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_liveRangeLength);
}

uint64_t DIARawSymbol::getVirtualAddress() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualAddress);
}

PDB_UdtType DIARawSymbol::getUdtKind() const {
  return PrivateGetDIAValue<DWORD, PDB_UdtType>(Symbol,
                                                &IDiaSymbol::get_udtKind);
}

bool DIARawSymbol::hasConstructor() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_constructor);
}

bool DIARawSymbol::hasCustomCallingConvention() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_customCallingConvention);
}

bool DIARawSymbol::hasFarReturn() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_farReturn);
}

bool DIARawSymbol::isCode() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_code);
}

bool DIARawSymbol::isCompilerGenerated() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_compilerGenerated);
}

bool DIARawSymbol::isConstType() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_constType);
}

bool DIARawSymbol::isEditAndContinueEnabled() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_editAndContinueEnabled);
}

bool DIARawSymbol::isFunction() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_function);
}

bool DIARawSymbol::getAddressTaken() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_addressTaken);
}

bool DIARawSymbol::getNoStackOrdering() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_noStackOrdering);
}

bool DIARawSymbol::hasAlloca() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasAlloca);
}

bool DIARawSymbol::hasAssignmentOperator() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasAssignmentOperator);
}

bool DIARawSymbol::hasCTypes() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isCTypes);
}

bool DIARawSymbol::hasCastOperator() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasCastOperator);
}

bool DIARawSymbol::hasDebugInfo() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasDebugInfo);
}

bool DIARawSymbol::hasEH() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasEH);
}

bool DIARawSymbol::hasEHa() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasEHa);
}

bool DIARawSymbol::hasInlAsm() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasInlAsm);
}

bool DIARawSymbol::hasInlineAttribute() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_inlSpec);
}

bool DIARawSymbol::hasInterruptReturn() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_interruptReturn);
}

bool DIARawSymbol::hasFramePointer() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_framePointerPresent);
}

bool DIARawSymbol::hasLongJump() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasLongJump);
}

bool DIARawSymbol::hasManagedCode() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasManagedCode);
}

bool DIARawSymbol::hasNestedTypes() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasNestedTypes);
}

bool DIARawSymbol::hasNoInlineAttribute() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_noInline);
}

bool DIARawSymbol::hasNoReturnAttribute() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_noReturn);
}

bool DIARawSymbol::hasOptimizedCodeDebugInfo() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_optimizedCodeDebugInfo);
}

bool DIARawSymbol::hasOverloadedOperator() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_overloadedOperator);
}

bool DIARawSymbol::hasSEH() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasSEH);
}

bool DIARawSymbol::hasSecurityChecks() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasSecurityChecks);
}

bool DIARawSymbol::hasSetJump() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_hasSetJump);
}

bool DIARawSymbol::hasStrictGSCheck() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_strictGSCheck);
}

bool DIARawSymbol::isAcceleratorGroupSharedLocal() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_isAcceleratorGroupSharedLocal);
}

bool DIARawSymbol::isAcceleratorPointerTagLiveRange() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_isAcceleratorPointerTagLiveRange);
}

bool DIARawSymbol::isAcceleratorStubFunction() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isAcceleratorStubFunction);
}

bool DIARawSymbol::isAggregated() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isAggregated);
}

bool DIARawSymbol::isIntroVirtualFunction() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_intro);
}

bool DIARawSymbol::isCVTCIL() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isCVTCIL);
}

bool DIARawSymbol::isConstructorVirtualBase() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isConstructorVirtualBase);
}

bool DIARawSymbol::isCxxReturnUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isCxxReturnUdt);
}

bool DIARawSymbol::isDataAligned() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isDataAligned);
}

bool DIARawSymbol::isHLSLData() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isHLSLData);
}

bool DIARawSymbol::isHotpatchable() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isHotpatchable);
}

bool DIARawSymbol::isIndirectVirtualBaseClass() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_indirectVirtualBaseClass);
}

bool DIARawSymbol::isInterfaceUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isInterfaceUdt);
}

bool DIARawSymbol::isIntrinsic() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_intrinsic);
}

bool DIARawSymbol::isLTCG() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isLTCG);
}

bool DIARawSymbol::isLocationControlFlowDependent() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_isLocationControlFlowDependent);
}

bool DIARawSymbol::isMSILNetmodule() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isMSILNetmodule);
}

bool DIARawSymbol::isMatrixRowMajor() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isMatrixRowMajor);
}

bool DIARawSymbol::isManagedCode() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_managed);
}

bool DIARawSymbol::isMSILCode() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_msil);
}

bool DIARawSymbol::isMultipleInheritance() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isMultipleInheritance);
}

bool DIARawSymbol::isNaked() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isNaked);
}

bool DIARawSymbol::isNested() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_nested);
}

bool DIARawSymbol::isOptimizedAway() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isOptimizedAway);
}

bool DIARawSymbol::isPacked() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_packed);
}

bool DIARawSymbol::isPointerBasedOnSymbolValue() const {
  return PrivateGetDIAValue(Symbol,
                            &IDiaSymbol::get_isPointerBasedOnSymbolValue);
}

bool DIARawSymbol::isPointerToDataMember() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isPointerToDataMember);
}

bool DIARawSymbol::isPointerToMemberFunction() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isPointerToMemberFunction);
}

bool DIARawSymbol::isPureVirtual() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_pure);
}

bool DIARawSymbol::isRValueReference() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_RValueReference);
}

bool DIARawSymbol::isRefUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isRefUdt);
}

bool DIARawSymbol::isReference() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_reference);
}

bool DIARawSymbol::isRestrictedType() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_restrictedType);
}

bool DIARawSymbol::isReturnValue() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isReturnValue);
}

bool DIARawSymbol::isSafeBuffers() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isSafeBuffers);
}

bool DIARawSymbol::isScoped() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_scoped);
}

bool DIARawSymbol::isSdl() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isSdl);
}

bool DIARawSymbol::isSingleInheritance() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isSingleInheritance);
}

bool DIARawSymbol::isSplitted() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isSplitted);
}

bool DIARawSymbol::isStatic() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isStatic);
}

bool DIARawSymbol::hasPrivateSymbols() const {
  // hasPrivateSymbols is the opposite of isStripped, but we expose
  // hasPrivateSymbols as a more intuitive interface.
  return !PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isStripped);
}

bool DIARawSymbol::isUnalignedType() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_unalignedType);
}

bool DIARawSymbol::isUnreached() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_notReached);
}

bool DIARawSymbol::isValueUdt() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isValueUdt);
}

bool DIARawSymbol::isVirtual() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtual);
}

bool DIARawSymbol::isVirtualBaseClass() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_virtualBaseClass);
}

bool DIARawSymbol::isVirtualInheritance() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_isVirtualInheritance);
}

bool DIARawSymbol::isVolatileType() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_volatileType);
}

bool DIARawSymbol::wasInlined() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_wasInlined);
}

std::string DIARawSymbol::getUnused() const {
  return PrivateGetDIAValue(Symbol, &IDiaSymbol::get_unused);
}
