//===-- PdbUtil.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PdbUtil.h"

#include "DWARFLocationExpression.h"
#include "PdbIndex.h"
#include "PdbSymUid.h"

#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

static Variable::RangeList
MakeRangeList(const PdbIndex &index, const LocalVariableAddrRange &range,
              llvm::ArrayRef<LocalVariableAddrGap> gaps) {
  lldb::addr_t start =
      index.MakeVirtualAddress(range.ISectStart, range.OffsetStart);
  lldb::addr_t end = start + range.Range;

  Variable::RangeList result;
  while (!gaps.empty()) {
    const LocalVariableAddrGap &gap = gaps.front();

    lldb::addr_t size = gap.GapStartOffset - start;
    result.Append(start, size);
    start += gap.Range;
    gaps = gaps.drop_front();
  }

  result.Append(start, end);
  return result;
}

CVTagRecord CVTagRecord::create(CVType type) {
  assert(IsTagRecord(type) && "type is not a tag record!");
  switch (type.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE: {
    ClassRecord cr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(type, cr));
    return CVTagRecord(std::move(cr));
  }
  case LF_UNION: {
    UnionRecord ur;
    llvm::cantFail(TypeDeserializer::deserializeAs<UnionRecord>(type, ur));
    return CVTagRecord(std::move(ur));
  }
  case LF_ENUM: {
    EnumRecord er;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(type, er));
    return CVTagRecord(std::move(er));
  }
  default:
    llvm_unreachable("Unreachable!");
  }
}

CVTagRecord::CVTagRecord(ClassRecord &&c)
    : cvclass(std::move(c)),
      m_kind(cvclass.Kind == TypeRecordKind::Struct ? Struct : Class) {}
CVTagRecord::CVTagRecord(UnionRecord &&u)
    : cvunion(std::move(u)), m_kind(Union) {}
CVTagRecord::CVTagRecord(EnumRecord &&e) : cvenum(std::move(e)), m_kind(Enum) {}

PDB_SymType lldb_private::npdb::CVSymToPDBSym(SymbolKind kind) {
  switch (kind) {
  case S_COMPILE3:
  case S_OBJNAME:
    return PDB_SymType::CompilandDetails;
  case S_ENVBLOCK:
    return PDB_SymType::CompilandEnv;
  case S_THUNK32:
  case S_TRAMPOLINE:
    return PDB_SymType::Thunk;
  case S_COFFGROUP:
    return PDB_SymType::CoffGroup;
  case S_EXPORT:
    return PDB_SymType::Export;
  case S_LPROC32:
  case S_GPROC32:
  case S_LPROC32_DPC:
    return PDB_SymType::Function;
  case S_PUB32:
    return PDB_SymType::PublicSymbol;
  case S_INLINESITE:
    return PDB_SymType::InlineSite;
  case S_LOCAL:
  case S_BPREL32:
  case S_REGREL32:
  case S_MANCONSTANT:
  case S_CONSTANT:
  case S_LDATA32:
  case S_GDATA32:
  case S_LMANDATA:
  case S_GMANDATA:
  case S_LTHREAD32:
  case S_GTHREAD32:
    return PDB_SymType::Data;
  case S_BLOCK32:
    return PDB_SymType::Block;
  case S_LABEL32:
    return PDB_SymType::Label;
  case S_CALLSITEINFO:
    return PDB_SymType::CallSite;
  case S_HEAPALLOCSITE:
    return PDB_SymType::HeapAllocationSite;
  case S_CALLEES:
    return PDB_SymType::Callee;
  case S_CALLERS:
    return PDB_SymType::Caller;
  default:
    lldbassert(false && "Invalid symbol record kind!");
  }
  return PDB_SymType::None;
}

PDB_SymType lldb_private::npdb::CVTypeToPDBType(TypeLeafKind kind) {
  switch (kind) {
  case LF_ARRAY:
    return PDB_SymType::ArrayType;
  case LF_ARGLIST:
    return PDB_SymType::FunctionSig;
  case LF_BCLASS:
    return PDB_SymType::BaseClass;
  case LF_BINTERFACE:
    return PDB_SymType::BaseInterface;
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE:
  case LF_UNION:
    return PDB_SymType::UDT;
  case LF_POINTER:
    return PDB_SymType::PointerType;
  case LF_ENUM:
    return PDB_SymType::Enum;
  case LF_PROCEDURE:
    return PDB_SymType::FunctionSig;
  case LF_BITFIELD:
    return PDB_SymType::BuiltinType;
  default:
    lldbassert(false && "Invalid type record kind!");
  }
  return PDB_SymType::None;
}

bool lldb_private::npdb::SymbolHasAddress(const CVSymbol &sym) {
  switch (sym.kind()) {
  case S_GPROC32:
  case S_LPROC32:
  case S_GPROC32_ID:
  case S_LPROC32_ID:
  case S_LPROC32_DPC:
  case S_LPROC32_DPC_ID:
  case S_THUNK32:
  case S_TRAMPOLINE:
  case S_COFFGROUP:
  case S_BLOCK32:
  case S_LABEL32:
  case S_CALLSITEINFO:
  case S_HEAPALLOCSITE:
  case S_LDATA32:
  case S_GDATA32:
  case S_LMANDATA:
  case S_GMANDATA:
  case S_LTHREAD32:
  case S_GTHREAD32:
    return true;
  default:
    return false;
  }
}

bool lldb_private::npdb::SymbolIsCode(const CVSymbol &sym) {
  switch (sym.kind()) {
  case S_GPROC32:
  case S_LPROC32:
  case S_GPROC32_ID:
  case S_LPROC32_ID:
  case S_LPROC32_DPC:
  case S_LPROC32_DPC_ID:
  case S_THUNK32:
  case S_TRAMPOLINE:
  case S_COFFGROUP:
  case S_BLOCK32:
    return true;
  default:
    return false;
  }
}

template <typename RecordT> RecordT createRecord(const CVSymbol &sym) {
  RecordT record(static_cast<SymbolRecordKind>(sym.kind()));
  cantFail(SymbolDeserializer::deserializeAs<RecordT>(sym, record));
  return record;
}

template <typename RecordT>
static SegmentOffset GetSegmentAndOffset(const CVSymbol &sym) {
  RecordT record = createRecord<RecordT>(sym);
  return {record.Segment, record.CodeOffset};
}

template <>
SegmentOffset GetSegmentAndOffset<TrampolineSym>(const CVSymbol &sym) {
  TrampolineSym record = createRecord<TrampolineSym>(sym);
  return {record.ThunkSection, record.ThunkOffset};
}

template <> SegmentOffset GetSegmentAndOffset<Thunk32Sym>(const CVSymbol &sym) {
  Thunk32Sym record = createRecord<Thunk32Sym>(sym);
  return {record.Segment, record.Offset};
}

template <>
SegmentOffset GetSegmentAndOffset<CoffGroupSym>(const CVSymbol &sym) {
  CoffGroupSym record = createRecord<CoffGroupSym>(sym);
  return {record.Segment, record.Offset};
}

template <> SegmentOffset GetSegmentAndOffset<DataSym>(const CVSymbol &sym) {
  DataSym record = createRecord<DataSym>(sym);
  return {record.Segment, record.DataOffset};
}

template <>
SegmentOffset GetSegmentAndOffset<ThreadLocalDataSym>(const CVSymbol &sym) {
  ThreadLocalDataSym record = createRecord<ThreadLocalDataSym>(sym);
  return {record.Segment, record.DataOffset};
}

SegmentOffset lldb_private::npdb::GetSegmentAndOffset(const CVSymbol &sym) {
  switch (sym.kind()) {
  case S_GPROC32:
  case S_LPROC32:
  case S_GPROC32_ID:
  case S_LPROC32_ID:
  case S_LPROC32_DPC:
  case S_LPROC32_DPC_ID:
    return ::GetSegmentAndOffset<ProcSym>(sym);
  case S_THUNK32:
    return ::GetSegmentAndOffset<Thunk32Sym>(sym);
    break;
  case S_TRAMPOLINE:
    return ::GetSegmentAndOffset<TrampolineSym>(sym);
    break;
  case S_COFFGROUP:
    return ::GetSegmentAndOffset<CoffGroupSym>(sym);
    break;
  case S_BLOCK32:
    return ::GetSegmentAndOffset<BlockSym>(sym);
    break;
  case S_LABEL32:
    return ::GetSegmentAndOffset<LabelSym>(sym);
    break;
  case S_CALLSITEINFO:
    return ::GetSegmentAndOffset<CallSiteInfoSym>(sym);
    break;
  case S_HEAPALLOCSITE:
    return ::GetSegmentAndOffset<HeapAllocationSiteSym>(sym);
    break;
  case S_LDATA32:
  case S_GDATA32:
  case S_LMANDATA:
  case S_GMANDATA:
    return ::GetSegmentAndOffset<DataSym>(sym);
    break;
  case S_LTHREAD32:
  case S_GTHREAD32:
    return ::GetSegmentAndOffset<ThreadLocalDataSym>(sym);
    break;
  default:
    lldbassert(false && "Record does not have a segment/offset!");
  }
  return {0, 0};
}

template <typename RecordT>
SegmentOffsetLength GetSegmentOffsetAndLength(const CVSymbol &sym) {
  RecordT record = createRecord<RecordT>(sym);
  return {record.Segment, record.CodeOffset, record.CodeSize};
}

template <>
SegmentOffsetLength
GetSegmentOffsetAndLength<TrampolineSym>(const CVSymbol &sym) {
  TrampolineSym record = createRecord<TrampolineSym>(sym);
  return {record.ThunkSection, record.ThunkOffset, record.Size};
}

template <>
SegmentOffsetLength GetSegmentOffsetAndLength<Thunk32Sym>(const CVSymbol &sym) {
  Thunk32Sym record = createRecord<Thunk32Sym>(sym);
  return SegmentOffsetLength{record.Segment, record.Offset, record.Length};
}

template <>
SegmentOffsetLength
GetSegmentOffsetAndLength<CoffGroupSym>(const CVSymbol &sym) {
  CoffGroupSym record = createRecord<CoffGroupSym>(sym);
  return SegmentOffsetLength{record.Segment, record.Offset, record.Size};
}

SegmentOffsetLength
lldb_private::npdb::GetSegmentOffsetAndLength(const CVSymbol &sym) {
  switch (sym.kind()) {
  case S_GPROC32:
  case S_LPROC32:
  case S_GPROC32_ID:
  case S_LPROC32_ID:
  case S_LPROC32_DPC:
  case S_LPROC32_DPC_ID:
    return ::GetSegmentOffsetAndLength<ProcSym>(sym);
  case S_THUNK32:
    return ::GetSegmentOffsetAndLength<Thunk32Sym>(sym);
    break;
  case S_TRAMPOLINE:
    return ::GetSegmentOffsetAndLength<TrampolineSym>(sym);
    break;
  case S_COFFGROUP:
    return ::GetSegmentOffsetAndLength<CoffGroupSym>(sym);
    break;
  case S_BLOCK32:
    return ::GetSegmentOffsetAndLength<BlockSym>(sym);
    break;
  default:
    lldbassert(false && "Record does not have a segment/offset/length triple!");
  }
  return {0, 0, 0};
}

bool lldb_private::npdb::IsForwardRefUdt(CVType cvt) {
  ClassRecord cr;
  UnionRecord ur;
  EnumRecord er;
  switch (cvt.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE:
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, cr));
    return cr.isForwardRef();
  case LF_UNION:
    llvm::cantFail(TypeDeserializer::deserializeAs<UnionRecord>(cvt, ur));
    return ur.isForwardRef();
  case LF_ENUM:
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, er));
    return er.isForwardRef();
  default:
    return false;
  }
}

bool lldb_private::npdb::IsTagRecord(llvm::codeview::CVType cvt) {
  switch (cvt.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_UNION:
  case LF_ENUM:
    return true;
  default:
    return false;
  }
}

bool lldb_private::npdb::IsClassStructUnion(llvm::codeview::CVType cvt) {
  switch (cvt.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_UNION:
    return true;
  default:
    return false;
  }
}

bool lldb_private::npdb::IsForwardRefUdt(const PdbTypeSymId &id,
                                         TpiStream &tpi) {
  if (id.is_ipi || id.index.isSimple())
    return false;
  return IsForwardRefUdt(tpi.getType(id.index));
}

bool lldb_private::npdb::IsTagRecord(const PdbTypeSymId &id, TpiStream &tpi) {
  if (id.is_ipi || id.index.isSimple())
    return false;
  return IsTagRecord(tpi.getType(id.index));
}

lldb::AccessType
lldb_private::npdb::TranslateMemberAccess(MemberAccess access) {
  switch (access) {
  case MemberAccess::Private:
    return lldb::eAccessPrivate;
  case MemberAccess::Protected:
    return lldb::eAccessProtected;
  case MemberAccess::Public:
    return lldb::eAccessPublic;
  case MemberAccess::None:
    return lldb::eAccessNone;
  }
  llvm_unreachable("unreachable");
}

TypeIndex lldb_private::npdb::GetFieldListIndex(CVType cvt) {
  switch (cvt.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE: {
    ClassRecord cr;
    cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, cr));
    return cr.FieldList;
  }
  case LF_UNION: {
    UnionRecord ur;
    cantFail(TypeDeserializer::deserializeAs<UnionRecord>(cvt, ur));
    return ur.FieldList;
  }
  case LF_ENUM: {
    EnumRecord er;
    cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, er));
    return er.FieldList;
  }
  default:
    llvm_unreachable("Unreachable!");
  }
}

TypeIndex lldb_private::npdb::LookThroughModifierRecord(CVType modifier) {
  lldbassert(modifier.kind() == LF_MODIFIER);
  ModifierRecord mr;
  llvm::cantFail(TypeDeserializer::deserializeAs<ModifierRecord>(modifier, mr));
  return mr.ModifiedType;
}

llvm::StringRef lldb_private::npdb::DropNameScope(llvm::StringRef name) {
  return MSVCUndecoratedNameParser::DropScope(name);
}

VariableInfo lldb_private::npdb::GetVariableNameInfo(CVSymbol sym) {
  VariableInfo result;

  if (sym.kind() == S_REGREL32) {
    RegRelativeSym reg(SymbolRecordKind::RegRelativeSym);
    cantFail(SymbolDeserializer::deserializeAs<RegRelativeSym>(sym, reg));
    result.type = reg.Type;
    result.name = reg.Name;
    return result;
  }

  if (sym.kind() == S_REGISTER) {
    RegisterSym reg(SymbolRecordKind::RegisterSym);
    cantFail(SymbolDeserializer::deserializeAs<RegisterSym>(sym, reg));
    result.type = reg.Index;
    result.name = reg.Name;
    return result;
  }

  if (sym.kind() == S_LOCAL) {
    LocalSym local(SymbolRecordKind::LocalSym);
    cantFail(SymbolDeserializer::deserializeAs<LocalSym>(sym, local));
    result.type = local.Type;
    result.name = local.Name;
    return result;
  }

  if (sym.kind() == S_GDATA32 || sym.kind() == S_LDATA32) {
    DataSym data(SymbolRecordKind::DataSym);
    cantFail(SymbolDeserializer::deserializeAs<DataSym>(sym, data));
    result.type = data.Type;
    result.name = data.Name;
    return result;
  }

  if (sym.kind() == S_GTHREAD32 || sym.kind() == S_LTHREAD32) {
    ThreadLocalDataSym data(SymbolRecordKind::ThreadLocalDataSym);
    cantFail(SymbolDeserializer::deserializeAs<ThreadLocalDataSym>(sym, data));
    result.type = data.Type;
    result.name = data.Name;
    return result;
  }

  if (sym.kind() == S_CONSTANT) {
    ConstantSym constant(SymbolRecordKind::ConstantSym);
    cantFail(SymbolDeserializer::deserializeAs<ConstantSym>(sym, constant));
    result.type = constant.Type;
    result.name = constant.Name;
    return result;
  }

  lldbassert(false && "Invalid variable record kind!");
  return {};
}

VariableInfo lldb_private::npdb::GetVariableLocationInfo(
    PdbIndex &index, PdbCompilandSymId var_id, lldb::ModuleSP module) {

  CVSymbol sym = index.ReadSymbolRecord(var_id);

  VariableInfo result = GetVariableNameInfo(sym);

  if (sym.kind() == S_REGREL32) {
    RegRelativeSym reg(SymbolRecordKind::RegRelativeSym);
    cantFail(SymbolDeserializer::deserializeAs<RegRelativeSym>(sym, reg));
    result.location =
        MakeRegRelLocationExpression(reg.Register, reg.Offset, module);
    result.ranges.emplace();
    return result;
  }

  if (sym.kind() == S_REGISTER) {
    RegisterSym reg(SymbolRecordKind::RegisterSym);
    cantFail(SymbolDeserializer::deserializeAs<RegisterSym>(sym, reg));
    result.location = MakeEnregisteredLocationExpression(reg.Register, module);
    result.ranges.emplace();
    return result;
  }

  if (sym.kind() == S_LOCAL) {
    LocalSym local(SymbolRecordKind::LocalSym);
    cantFail(SymbolDeserializer::deserializeAs<LocalSym>(sym, local));

    PdbCompilandSymId loc_specifier_id(var_id.modi,
                                       var_id.offset + sym.RecordData.size());
    CVSymbol loc_specifier_cvs = index.ReadSymbolRecord(loc_specifier_id);
    if (loc_specifier_cvs.kind() == S_DEFRANGE_FRAMEPOINTER_REL) {
      DefRangeFramePointerRelSym loc(
          SymbolRecordKind::DefRangeFramePointerRelSym);
      cantFail(SymbolDeserializer::deserializeAs<DefRangeFramePointerRelSym>(
          loc_specifier_cvs, loc));
      // FIXME: The register needs to come from the S_FRAMEPROC symbol.
      result.location =
          MakeRegRelLocationExpression(RegisterId::RSP, loc.Offset, module);
      result.ranges = MakeRangeList(index, loc.Range, loc.Gaps);
    } else {
      // FIXME: Handle other kinds
    }
    return result;
  }
  llvm_unreachable("Symbol is not a local variable!");
  return result;
}

lldb::BasicType
lldb_private::npdb::GetCompilerTypeForSimpleKind(SimpleTypeKind kind) {
  switch (kind) {
  case SimpleTypeKind::Boolean128:
  case SimpleTypeKind::Boolean16:
  case SimpleTypeKind::Boolean32:
  case SimpleTypeKind::Boolean64:
  case SimpleTypeKind::Boolean8:
    return lldb::eBasicTypeBool;
  case SimpleTypeKind::Byte:
  case SimpleTypeKind::UnsignedCharacter:
    return lldb::eBasicTypeUnsignedChar;
  case SimpleTypeKind::NarrowCharacter:
    return lldb::eBasicTypeChar;
  case SimpleTypeKind::SignedCharacter:
  case SimpleTypeKind::SByte:
    return lldb::eBasicTypeSignedChar;
  case SimpleTypeKind::Character16:
    return lldb::eBasicTypeChar16;
  case SimpleTypeKind::Character32:
    return lldb::eBasicTypeChar32;
  case SimpleTypeKind::Complex80:
    return lldb::eBasicTypeLongDoubleComplex;
  case SimpleTypeKind::Complex64:
    return lldb::eBasicTypeDoubleComplex;
  case SimpleTypeKind::Complex32:
    return lldb::eBasicTypeFloatComplex;
  case SimpleTypeKind::Float128:
  case SimpleTypeKind::Float80:
    return lldb::eBasicTypeLongDouble;
  case SimpleTypeKind::Float64:
    return lldb::eBasicTypeDouble;
  case SimpleTypeKind::Float32:
    return lldb::eBasicTypeFloat;
  case SimpleTypeKind::Float16:
    return lldb::eBasicTypeHalf;
  case SimpleTypeKind::Int128:
    return lldb::eBasicTypeInt128;
  case SimpleTypeKind::Int64:
  case SimpleTypeKind::Int64Quad:
    return lldb::eBasicTypeLongLong;
  case SimpleTypeKind::Int32:
    return lldb::eBasicTypeInt;
  case SimpleTypeKind::Int16:
  case SimpleTypeKind::Int16Short:
    return lldb::eBasicTypeShort;
  case SimpleTypeKind::UInt128:
    return lldb::eBasicTypeUnsignedInt128;
  case SimpleTypeKind::UInt64:
  case SimpleTypeKind::UInt64Quad:
    return lldb::eBasicTypeUnsignedLongLong;
  case SimpleTypeKind::HResult:
  case SimpleTypeKind::UInt32:
    return lldb::eBasicTypeUnsignedInt;
  case SimpleTypeKind::UInt16:
  case SimpleTypeKind::UInt16Short:
    return lldb::eBasicTypeUnsignedShort;
  case SimpleTypeKind::Int32Long:
    return lldb::eBasicTypeLong;
  case SimpleTypeKind::UInt32Long:
    return lldb::eBasicTypeUnsignedLong;
  case SimpleTypeKind::Void:
    return lldb::eBasicTypeVoid;
  case SimpleTypeKind::WideCharacter:
    return lldb::eBasicTypeWChar;
  default:
    return lldb::eBasicTypeInvalid;
  }
}

size_t lldb_private::npdb::GetTypeSizeForSimpleKind(SimpleTypeKind kind) {
  switch (kind) {
  case SimpleTypeKind::Boolean128:
  case SimpleTypeKind::Int128:
  case SimpleTypeKind::UInt128:
  case SimpleTypeKind::Float128:
    return 16;
  case SimpleTypeKind::Complex80:
  case SimpleTypeKind::Float80:
    return 10;
  case SimpleTypeKind::Boolean64:
  case SimpleTypeKind::Complex64:
  case SimpleTypeKind::UInt64:
  case SimpleTypeKind::UInt64Quad:
  case SimpleTypeKind::Float64:
  case SimpleTypeKind::Int64:
  case SimpleTypeKind::Int64Quad:
    return 8;
  case SimpleTypeKind::Boolean32:
  case SimpleTypeKind::Character32:
  case SimpleTypeKind::Complex32:
  case SimpleTypeKind::Float32:
  case SimpleTypeKind::Int32:
  case SimpleTypeKind::Int32Long:
  case SimpleTypeKind::UInt32Long:
  case SimpleTypeKind::HResult:
  case SimpleTypeKind::UInt32:
    return 4;
  case SimpleTypeKind::Boolean16:
  case SimpleTypeKind::Character16:
  case SimpleTypeKind::Float16:
  case SimpleTypeKind::Int16:
  case SimpleTypeKind::Int16Short:
  case SimpleTypeKind::UInt16:
  case SimpleTypeKind::UInt16Short:
  case SimpleTypeKind::WideCharacter:
    return 2;
  case SimpleTypeKind::Boolean8:
  case SimpleTypeKind::Byte:
  case SimpleTypeKind::UnsignedCharacter:
  case SimpleTypeKind::NarrowCharacter:
  case SimpleTypeKind::SignedCharacter:
  case SimpleTypeKind::SByte:
    return 1;
  case SimpleTypeKind::Void:
  default:
    return 0;
  }
}

PdbTypeSymId lldb_private::npdb::GetBestPossibleDecl(PdbTypeSymId id,
                                                     TpiStream &tpi) {
  if (id.index.isSimple())
    return id;

  CVType cvt = tpi.getType(id.index);

  // Only tag records have a best and a worst record.
  if (!IsTagRecord(cvt))
    return id;

  // Tag records that are not forward decls are full decls, hence they are the
  // best.
  if (!IsForwardRefUdt(cvt))
    return id;

  return llvm::cantFail(tpi.findFullDeclForForwardRef(id.index));
}

template <typename RecordType> static size_t GetSizeOfTypeInternal(CVType cvt) {
  RecordType record;
  llvm::cantFail(TypeDeserializer::deserializeAs<RecordType>(cvt, record));
  return record.getSize();
}

size_t lldb_private::npdb::GetSizeOfType(PdbTypeSymId id,
                                         llvm::pdb::TpiStream &tpi) {
  if (id.index.isSimple()) {
    switch (id.index.getSimpleMode()) {
    case SimpleTypeMode::Direct:
      return GetTypeSizeForSimpleKind(id.index.getSimpleKind());
    case SimpleTypeMode::NearPointer32:
    case SimpleTypeMode::FarPointer32:
      return 4;
    case SimpleTypeMode::NearPointer64:
      return 8;
    case SimpleTypeMode::NearPointer128:
      return 16;
    default:
      break;
    }
    return 0;
  }

  TypeIndex index = id.index;
  if (IsForwardRefUdt(index, tpi))
    index = llvm::cantFail(tpi.findFullDeclForForwardRef(index));

  CVType cvt = tpi.getType(index);
  switch (cvt.kind()) {
  case LF_MODIFIER:
    return GetSizeOfType({LookThroughModifierRecord(cvt)}, tpi);
  case LF_ENUM: {
    EnumRecord record;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, record));
    return GetSizeOfType({record.UnderlyingType}, tpi);
  }
  case LF_POINTER:
    return GetSizeOfTypeInternal<PointerRecord>(cvt);
  case LF_ARRAY:
    return GetSizeOfTypeInternal<ArrayRecord>(cvt);
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE:
    return GetSizeOfTypeInternal<ClassRecord>(cvt);
  case LF_UNION:
    return GetSizeOfTypeInternal<UnionRecord>(cvt);
  default:
    break;
  }
  return 0;
}
