//===- BTFDebug.cpp - BTF Generator ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing BTF debug info.
//
//===----------------------------------------------------------------------===//

#include "BTFDebug.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include <fstream>
#include <sstream>

using namespace llvm;

static const char *BTFKindStr[] = {
#define HANDLE_BTF_KIND(ID, NAME) "BTF_KIND_" #NAME,
#include "BTF.def"
};

/// Emit a BTF common type.
void BTFTypeBase::emitType(MCStreamer &OS) {
  OS.AddComment(std::string(BTFKindStr[Kind]) + "(id = " + std::to_string(Id) +
                ")");
  OS.EmitIntValue(BTFType.NameOff, 4);
  OS.AddComment("0x" + Twine::utohexstr(BTFType.Info));
  OS.EmitIntValue(BTFType.Info, 4);
  OS.EmitIntValue(BTFType.Size, 4);
}

BTFTypeDerived::BTFTypeDerived(const DIDerivedType *DTy, unsigned Tag)
    : DTy(DTy) {
  switch (Tag) {
  case dwarf::DW_TAG_pointer_type:
    Kind = BTF::BTF_KIND_PTR;
    break;
  case dwarf::DW_TAG_const_type:
    Kind = BTF::BTF_KIND_CONST;
    break;
  case dwarf::DW_TAG_volatile_type:
    Kind = BTF::BTF_KIND_VOLATILE;
    break;
  case dwarf::DW_TAG_typedef:
    Kind = BTF::BTF_KIND_TYPEDEF;
    break;
  case dwarf::DW_TAG_restrict_type:
    Kind = BTF::BTF_KIND_RESTRICT;
    break;
  default:
    llvm_unreachable("Unknown DIDerivedType Tag");
  }
  BTFType.Info = Kind << 24;
}

void BTFTypeDerived::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(DTy->getName());

  // The base type for PTR/CONST/VOLATILE could be void.
  const DIType *ResolvedType = DTy->getBaseType().resolve();
  if (!ResolvedType) {
    assert((Kind == BTF::BTF_KIND_PTR || Kind == BTF::BTF_KIND_CONST ||
            Kind == BTF::BTF_KIND_VOLATILE) &&
           "Invalid null basetype");
    BTFType.Type = 0;
  } else {
    BTFType.Type = BDebug.getTypeId(ResolvedType);
  }
}

void BTFTypeDerived::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

/// Represent a struct/union forward declaration.
BTFTypeFwd::BTFTypeFwd(StringRef Name, bool IsUnion) : Name(Name) {
  Kind = BTF::BTF_KIND_FWD;
  BTFType.Info = IsUnion << 31 | Kind << 24;
  BTFType.Type = 0;
}

void BTFTypeFwd::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeFwd::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

BTFTypeInt::BTFTypeInt(uint32_t Encoding, uint32_t SizeInBits,
                       uint32_t OffsetInBits, StringRef TypeName)
    : Name(TypeName) {
  // Translate IR int encoding to BTF int encoding.
  uint8_t BTFEncoding;
  switch (Encoding) {
  case dwarf::DW_ATE_boolean:
    BTFEncoding = BTF::INT_BOOL;
    break;
  case dwarf::DW_ATE_signed:
  case dwarf::DW_ATE_signed_char:
    BTFEncoding = BTF::INT_SIGNED;
    break;
  case dwarf::DW_ATE_unsigned:
  case dwarf::DW_ATE_unsigned_char:
    BTFEncoding = 0;
    break;
  default:
    llvm_unreachable("Unknown BTFTypeInt Encoding");
  }

  Kind = BTF::BTF_KIND_INT;
  BTFType.Info = Kind << 24;
  BTFType.Size = roundupToBytes(SizeInBits);
  IntVal = (BTFEncoding << 24) | OffsetInBits << 16 | SizeInBits;
}

void BTFTypeInt::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeInt::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.AddComment("0x" + Twine::utohexstr(IntVal));
  OS.EmitIntValue(IntVal, 4);
}

BTFTypeEnum::BTFTypeEnum(const DICompositeType *ETy, uint32_t VLen) : ETy(ETy) {
  Kind = BTF::BTF_KIND_ENUM;
  BTFType.Info = Kind << 24 | VLen;
  BTFType.Size = roundupToBytes(ETy->getSizeInBits());
}

void BTFTypeEnum::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(ETy->getName());

  DINodeArray Elements = ETy->getElements();
  for (const auto Element : Elements) {
    const auto *Enum = cast<DIEnumerator>(Element);

    struct BTF::BTFEnum BTFEnum;
    BTFEnum.NameOff = BDebug.addString(Enum->getName());
    // BTF enum value is 32bit, enforce it.
    BTFEnum.Val = static_cast<uint32_t>(Enum->getValue());
    EnumValues.push_back(BTFEnum);
  }
}

void BTFTypeEnum::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Enum : EnumValues) {
    OS.EmitIntValue(Enum.NameOff, 4);
    OS.EmitIntValue(Enum.Val, 4);
  }
}

BTFTypeArray::BTFTypeArray(const DICompositeType *ATy) : ATy(ATy) {
  Kind = BTF::BTF_KIND_ARRAY;
  BTFType.Info = Kind << 24;
}

/// Represent a BTF array. BTF does not record array dimensions,
/// so conceptually a BTF array is a one-dimensional array.
void BTFTypeArray::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(ATy->getName());
  BTFType.Size = 0;

  auto *BaseType = ATy->getBaseType().resolve();
  ArrayInfo.ElemType = BDebug.getTypeId(BaseType);

  // The IR does not really have a type for the index.
  // A special type for array index should have been
  // created during initial type traversal. Just
  // retrieve that type id.
  ArrayInfo.IndexType = BDebug.getArrayIndexTypeId();

  // Get the number of array elements.
  // If the array size is 0, set the number of elements as 0.
  // Otherwise, recursively traverse the base types to
  // find the element size. The number of elements is
  // the totoal array size in bits divided by
  // element size in bits.
  uint64_t ArraySizeInBits = ATy->getSizeInBits();
  if (!ArraySizeInBits) {
    ArrayInfo.Nelems = 0;
  } else {
    uint32_t BaseTypeSize = BaseType->getSizeInBits();
    while (!BaseTypeSize) {
      const auto *DDTy = cast<DIDerivedType>(BaseType);
      BaseType = DDTy->getBaseType().resolve();
      assert(BaseType);
      BaseTypeSize = BaseType->getSizeInBits();
    }
    ArrayInfo.Nelems = ATy->getSizeInBits() / BaseTypeSize;
  }
}

void BTFTypeArray::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.EmitIntValue(ArrayInfo.ElemType, 4);
  OS.EmitIntValue(ArrayInfo.IndexType, 4);
  OS.EmitIntValue(ArrayInfo.Nelems, 4);
}

/// Represent either a struct or a union.
BTFTypeStruct::BTFTypeStruct(const DICompositeType *STy, bool IsStruct,
                             bool HasBitField, uint32_t Vlen)
    : STy(STy), HasBitField(HasBitField) {
  Kind = IsStruct ? BTF::BTF_KIND_STRUCT : BTF::BTF_KIND_UNION;
  BTFType.Size = roundupToBytes(STy->getSizeInBits());
  BTFType.Info = (HasBitField << 31) | (Kind << 24) | Vlen;
}

void BTFTypeStruct::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(STy->getName());

  // Add struct/union members.
  const DINodeArray Elements = STy->getElements();
  for (const auto *Element : Elements) {
    struct BTF::BTFMember BTFMember;
    const auto *DDTy = cast<DIDerivedType>(Element);

    BTFMember.NameOff = BDebug.addString(DDTy->getName());
    if (HasBitField) {
      uint8_t BitFieldSize = DDTy->isBitField() ? DDTy->getSizeInBits() : 0;
      BTFMember.Offset = BitFieldSize << 24 | DDTy->getOffsetInBits();
    } else {
      BTFMember.Offset = DDTy->getOffsetInBits();
    }
    BTFMember.Type = BDebug.getTypeId(DDTy->getBaseType().resolve());
    Members.push_back(BTFMember);
  }
}

void BTFTypeStruct::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Member : Members) {
    OS.EmitIntValue(Member.NameOff, 4);
    OS.EmitIntValue(Member.Type, 4);
    OS.AddComment("0x" + Twine::utohexstr(Member.Offset));
    OS.EmitIntValue(Member.Offset, 4);
  }
}

/// The Func kind represents both subprogram and pointee of function
/// pointers. If the FuncName is empty, it represents a pointee of function
/// pointer. Otherwise, it represents a subprogram. The func arg names
/// are empty for pointee of function pointer case, and are valid names
/// for subprogram.
BTFTypeFuncProto::BTFTypeFuncProto(
    const DISubroutineType *STy, uint32_t VLen,
    const std::unordered_map<uint32_t, StringRef> &FuncArgNames)
    : STy(STy), FuncArgNames(FuncArgNames) {
  Kind = BTF::BTF_KIND_FUNC_PROTO;
  BTFType.Info = (Kind << 24) | VLen;
}

void BTFTypeFuncProto::completeType(BTFDebug &BDebug) {
  DITypeRefArray Elements = STy->getTypeArray();
  auto RetType = Elements[0].resolve();
  BTFType.Type = RetType ? BDebug.getTypeId(RetType) : 0;
  BTFType.NameOff = 0;

  // For null parameter which is typically the last one
  // to represent the vararg, encode the NameOff/Type to be 0.
  for (unsigned I = 1, N = Elements.size(); I < N; ++I) {
    struct BTF::BTFParam Param;
    auto Element = Elements[I].resolve();
    if (Element) {
      Param.NameOff = BDebug.addString(FuncArgNames[I]);
      Param.Type = BDebug.getTypeId(Element);
    } else {
      Param.NameOff = 0;
      Param.Type = 0;
    }
    Parameters.push_back(Param);
  }
}

void BTFTypeFuncProto::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Param : Parameters) {
    OS.EmitIntValue(Param.NameOff, 4);
    OS.EmitIntValue(Param.Type, 4);
  }
}

BTFTypeFunc::BTFTypeFunc(StringRef FuncName, uint32_t ProtoTypeId)
    : Name(FuncName) {
  Kind = BTF::BTF_KIND_FUNC;
  BTFType.Info = Kind << 24;
  BTFType.Type = ProtoTypeId;
}

void BTFTypeFunc::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeFunc::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

uint32_t BTFStringTable::addString(StringRef S) {
  // Check whether the string already exists.
  for (auto &OffsetM : OffsetToIdMap) {
    if (Table[OffsetM.second] == S)
      return OffsetM.first;
  }
  // Not find, add to the string table.
  uint32_t Offset = Size;
  OffsetToIdMap[Offset] = Table.size();
  Table.push_back(S);
  Size += S.size() + 1;
  return Offset;
}

BTFDebug::BTFDebug(AsmPrinter *AP)
    : DebugHandlerBase(AP), OS(*Asm->OutStreamer), SkipInstruction(false),
      LineInfoGenerated(false), SecNameOff(0), ArrayIndexTypeId(0) {
  addString("\0");
}

void BTFDebug::addType(std::unique_ptr<BTFTypeBase> TypeEntry,
                       const DIType *Ty) {
  TypeEntry->setId(TypeEntries.size() + 1);
  DIToIdMap[Ty] = TypeEntry->getId();
  TypeEntries.push_back(std::move(TypeEntry));
}

uint32_t BTFDebug::addType(std::unique_ptr<BTFTypeBase> TypeEntry) {
  TypeEntry->setId(TypeEntries.size() + 1);
  uint32_t Id = TypeEntry->getId();
  TypeEntries.push_back(std::move(TypeEntry));
  return Id;
}

void BTFDebug::visitBasicType(const DIBasicType *BTy) {
  // Only int types are supported in BTF.
  uint32_t Encoding = BTy->getEncoding();
  if (Encoding != dwarf::DW_ATE_boolean && Encoding != dwarf::DW_ATE_signed &&
      Encoding != dwarf::DW_ATE_signed_char &&
      Encoding != dwarf::DW_ATE_unsigned &&
      Encoding != dwarf::DW_ATE_unsigned_char)
    return;

  // Create a BTF type instance for this DIBasicType and put it into
  // DIToIdMap for cross-type reference check.
  auto TypeEntry = llvm::make_unique<BTFTypeInt>(
      Encoding, BTy->getSizeInBits(), BTy->getOffsetInBits(), BTy->getName());
  addType(std::move(TypeEntry), BTy);
}

/// Handle subprogram or subroutine types.
void BTFDebug::visitSubroutineType(
    const DISubroutineType *STy, bool ForSubprog,
    const std::unordered_map<uint32_t, StringRef> &FuncArgNames,
    uint32_t &TypeId) {
  DITypeRefArray Elements = STy->getTypeArray();
  uint32_t VLen = Elements.size() - 1;
  if (VLen > BTF::MAX_VLEN)
    return;

  // Subprogram has a valid non-zero-length name, and the pointee of
  // a function pointer has an empty name. The subprogram type will
  // not be added to DIToIdMap as it should not be referenced by
  // any other types.
  auto TypeEntry = llvm::make_unique<BTFTypeFuncProto>(STy, VLen, FuncArgNames);
  if (ForSubprog)
    TypeId = addType(std::move(TypeEntry)); // For subprogram
  else
    addType(std::move(TypeEntry), STy); // For func ptr

  // Visit return type and func arg types.
  for (const auto Element : Elements) {
    visitTypeEntry(Element.resolve());
  }
}

/// Handle structure/union types.
void BTFDebug::visitStructType(const DICompositeType *CTy, bool IsStruct) {
  const DINodeArray Elements = CTy->getElements();
  uint32_t VLen = Elements.size();
  if (VLen > BTF::MAX_VLEN)
    return;

  // Check whether we have any bitfield members or not
  bool HasBitField = false;
  for (const auto *Element : Elements) {
    auto E = cast<DIDerivedType>(Element);
    if (E->isBitField()) {
      HasBitField = true;
      break;
    }
  }

  auto TypeEntry =
      llvm::make_unique<BTFTypeStruct>(CTy, IsStruct, HasBitField, VLen);
  addType(std::move(TypeEntry), CTy);

  // Visit all struct members.
  for (const auto *Element : Elements)
    visitTypeEntry(cast<DIDerivedType>(Element));
}

void BTFDebug::visitArrayType(const DICompositeType *CTy) {
  auto TypeEntry = llvm::make_unique<BTFTypeArray>(CTy);
  addType(std::move(TypeEntry), CTy);

  // The IR does not have a type for array index while BTF wants one.
  // So create an array index type if there is none.
  if (!ArrayIndexTypeId) {
    auto TypeEntry = llvm::make_unique<BTFTypeInt>(dwarf::DW_ATE_unsigned, 32,
                                                   0, "__ARRAY_SIZE_TYPE__");
    ArrayIndexTypeId = addType(std::move(TypeEntry));
  }

  // Visit array element type.
  visitTypeEntry(CTy->getBaseType().resolve());
}

void BTFDebug::visitEnumType(const DICompositeType *CTy) {
  DINodeArray Elements = CTy->getElements();
  uint32_t VLen = Elements.size();
  if (VLen > BTF::MAX_VLEN)
    return;

  auto TypeEntry = llvm::make_unique<BTFTypeEnum>(CTy, VLen);
  addType(std::move(TypeEntry), CTy);
  // No need to visit base type as BTF does not encode it.
}

/// Handle structure/union forward declarations.
void BTFDebug::visitFwdDeclType(const DICompositeType *CTy, bool IsUnion) {
  auto TypeEntry = llvm::make_unique<BTFTypeFwd>(CTy->getName(), IsUnion);
  addType(std::move(TypeEntry), CTy);
}

/// Handle structure, union, array and enumeration types.
void BTFDebug::visitCompositeType(const DICompositeType *CTy) {
  auto Tag = CTy->getTag();
  if (Tag == dwarf::DW_TAG_structure_type || Tag == dwarf::DW_TAG_union_type) {
    // Handle forward declaration differently as it does not have members.
    if (CTy->isForwardDecl())
      visitFwdDeclType(CTy, Tag == dwarf::DW_TAG_union_type);
    else
      visitStructType(CTy, Tag == dwarf::DW_TAG_structure_type);
  } else if (Tag == dwarf::DW_TAG_array_type)
    visitArrayType(CTy);
  else if (Tag == dwarf::DW_TAG_enumeration_type)
    visitEnumType(CTy);
}

/// Handle pointer, typedef, const, volatile, restrict and member types.
void BTFDebug::visitDerivedType(const DIDerivedType *DTy) {
  unsigned Tag = DTy->getTag();

  if (Tag == dwarf::DW_TAG_pointer_type || Tag == dwarf::DW_TAG_typedef ||
      Tag == dwarf::DW_TAG_const_type || Tag == dwarf::DW_TAG_volatile_type ||
      Tag == dwarf::DW_TAG_restrict_type) {
    auto TypeEntry = llvm::make_unique<BTFTypeDerived>(DTy, Tag);
    addType(std::move(TypeEntry), DTy);
  } else if (Tag != dwarf::DW_TAG_member) {
    return;
  }

  // Visit base type of pointer, typedef, const, volatile, restrict or
  // struct/union member.
  visitTypeEntry(DTy->getBaseType().resolve());
}

void BTFDebug::visitTypeEntry(const DIType *Ty) {
  if (!Ty || DIToIdMap.find(Ty) != DIToIdMap.end())
    return;

  uint32_t TypeId;
  if (const auto *BTy = dyn_cast<DIBasicType>(Ty))
    visitBasicType(BTy);
  else if (const auto *STy = dyn_cast<DISubroutineType>(Ty))
    visitSubroutineType(STy, false, std::unordered_map<uint32_t, StringRef>(),
                        TypeId);
  else if (const auto *CTy = dyn_cast<DICompositeType>(Ty))
    visitCompositeType(CTy);
  else if (const auto *DTy = dyn_cast<DIDerivedType>(Ty))
    visitDerivedType(DTy);
  else
    llvm_unreachable("Unknown DIType");
}

/// Read file contents from the actual file or from the source
std::string BTFDebug::populateFileContent(const DISubprogram *SP) {
  auto File = SP->getFile();
  std::string FileName;

  if (File->getDirectory().size())
    FileName = File->getDirectory().str() + "/" + File->getFilename().str();
  else
    FileName = File->getFilename();

  // No need to populate the contends if it has been populated!
  if (FileContent.find(FileName) != FileContent.end())
    return FileName;

  std::vector<std::string> Content;
  std::string Line;
  Content.push_back(Line); // Line 0 for empty string

  auto Source = File->getSource();
  if (Source) {
    std::istringstream InputString(Source.getValue());
    while (std::getline(InputString, Line))
      Content.push_back(Line);
  } else {
    std::ifstream InputFile(FileName);
    while (std::getline(InputFile, Line))
      Content.push_back(Line);
  }

  FileContent[FileName] = Content;
  return FileName;
}

void BTFDebug::constructLineInfo(const DISubprogram *SP, MCSymbol *Label,
                                 uint32_t Line, uint32_t Column) {
  std::string FileName = populateFileContent(SP);
  BTFLineInfo LineInfo;

  LineInfo.Label = Label;
  LineInfo.FileNameOff = addString(FileName);
  // If file content is not available, let LineOff = 0.
  if (Line < FileContent[FileName].size())
    LineInfo.LineOff = addString(FileContent[FileName][Line]);
  else
    LineInfo.LineOff = 0;
  LineInfo.LineNum = Line;
  LineInfo.ColumnNum = Column;
  LineInfoTable[SecNameOff].push_back(LineInfo);
}

void BTFDebug::emitCommonHeader() {
  OS.AddComment("0x" + Twine::utohexstr(BTF::MAGIC));
  OS.EmitIntValue(BTF::MAGIC, 2);
  OS.EmitIntValue(BTF::VERSION, 1);
  OS.EmitIntValue(0, 1);
}

void BTFDebug::emitBTFSection() {
  MCContext &Ctx = OS.getContext();
  OS.SwitchSection(Ctx.getELFSection(".BTF", ELF::SHT_PROGBITS, 0));

  // Emit header.
  emitCommonHeader();
  OS.EmitIntValue(BTF::HeaderSize, 4);

  uint32_t TypeLen = 0, StrLen;
  for (const auto &TypeEntry : TypeEntries)
    TypeLen += TypeEntry->getSize();
  StrLen = StringTable.getSize();

  OS.EmitIntValue(0, 4);
  OS.EmitIntValue(TypeLen, 4);
  OS.EmitIntValue(TypeLen, 4);
  OS.EmitIntValue(StrLen, 4);

  // Emit type table.
  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->emitType(OS);

  // Emit string table.
  uint32_t StringOffset = 0;
  for (const auto &S : StringTable.getTable()) {
    OS.AddComment("string offset=" + std::to_string(StringOffset));
    OS.EmitBytes(S);
    OS.EmitBytes(StringRef("\0", 1));
    StringOffset += S.size() + 1;
  }
}

void BTFDebug::emitBTFExtSection() {
  MCContext &Ctx = OS.getContext();
  OS.SwitchSection(Ctx.getELFSection(".BTF.ext", ELF::SHT_PROGBITS, 0));

  // Emit header.
  emitCommonHeader();
  OS.EmitIntValue(BTF::ExtHeaderSize, 4);

  // Account for FuncInfo/LineInfo record size as well.
  uint32_t FuncLen = 4, LineLen = 4;
  for (const auto &FuncSec : FuncInfoTable) {
    FuncLen += BTF::SecFuncInfoSize;
    FuncLen += FuncSec.second.size() * BTF::BPFFuncInfoSize;
  }
  for (const auto &LineSec : LineInfoTable) {
    LineLen += BTF::SecLineInfoSize;
    LineLen += LineSec.second.size() * BTF::BPFLineInfoSize;
  }

  OS.EmitIntValue(0, 4);
  OS.EmitIntValue(FuncLen, 4);
  OS.EmitIntValue(FuncLen, 4);
  OS.EmitIntValue(LineLen, 4);

  // Emit func_info table.
  OS.AddComment("FuncInfo");
  OS.EmitIntValue(BTF::BPFFuncInfoSize, 4);
  for (const auto &FuncSec : FuncInfoTable) {
    OS.AddComment("FuncInfo section string offset=" +
                  std::to_string(FuncSec.first));
    OS.EmitIntValue(FuncSec.first, 4);
    OS.EmitIntValue(FuncSec.second.size(), 4);
    for (const auto &FuncInfo : FuncSec.second) {
      Asm->EmitLabelReference(FuncInfo.Label, 4);
      OS.EmitIntValue(FuncInfo.TypeId, 4);
    }
  }

  // Emit line_info table.
  OS.AddComment("LineInfo");
  OS.EmitIntValue(BTF::BPFLineInfoSize, 4);
  for (const auto &LineSec : LineInfoTable) {
    OS.AddComment("LineInfo section string offset=" +
                  std::to_string(LineSec.first));
    OS.EmitIntValue(LineSec.first, 4);
    OS.EmitIntValue(LineSec.second.size(), 4);
    for (const auto &LineInfo : LineSec.second) {
      Asm->EmitLabelReference(LineInfo.Label, 4);
      OS.EmitIntValue(LineInfo.FileNameOff, 4);
      OS.EmitIntValue(LineInfo.LineOff, 4);
      OS.AddComment("Line " + std::to_string(LineInfo.LineNum) + " Col " +
                    std::to_string(LineInfo.ColumnNum));
      OS.EmitIntValue(LineInfo.LineNum << 10 | LineInfo.ColumnNum, 4);
    }
  }
}

void BTFDebug::beginFunctionImpl(const MachineFunction *MF) {
  auto *SP = MF->getFunction().getSubprogram();
  auto *Unit = SP->getUnit();

  if (Unit->getEmissionKind() == DICompileUnit::NoDebug) {
    SkipInstruction = true;
    return;
  }
  SkipInstruction = false;

  // Collect all types locally referenced in this function.
  // Use RetainedNodes so we can collect all argument names
  // even if the argument is not used.
  std::unordered_map<uint32_t, StringRef> FuncArgNames;
  for (const DINode *DN : SP->getRetainedNodes()) {
    if (const auto *DV = dyn_cast<DILocalVariable>(DN)) {
      visitTypeEntry(DV->getType().resolve());

      // Collect function arguments for subprogram func type.
      uint32_t Arg = DV->getArg();
      if (Arg)
        FuncArgNames[Arg] = DV->getName();
    }
  }

  // Construct subprogram func proto type.
  uint32_t ProtoTypeId;
  visitSubroutineType(SP->getType(), true, FuncArgNames, ProtoTypeId);

  // Construct subprogram func type
  auto FuncTypeEntry =
      llvm::make_unique<BTFTypeFunc>(SP->getName(), ProtoTypeId);
  uint32_t FuncTypeId = addType(std::move(FuncTypeEntry));

  // Construct funcinfo and the first lineinfo for the function.
  MCSymbol *FuncLabel = Asm->getFunctionBegin();
  BTFFuncInfo FuncInfo;
  FuncInfo.Label = FuncLabel;
  FuncInfo.TypeId = FuncTypeId;
  if (FuncLabel->isInSection()) {
    MCSection &Section = FuncLabel->getSection();
    const MCSectionELF *SectionELF = dyn_cast<MCSectionELF>(&Section);
    assert(SectionELF && "Null section for Function Label");
    SecNameOff = addString(SectionELF->getSectionName());
  } else {
    SecNameOff = addString(".text");
  }
  FuncInfoTable[SecNameOff].push_back(FuncInfo);
}

void BTFDebug::endFunctionImpl(const MachineFunction *MF) {
  SkipInstruction = false;
  LineInfoGenerated = false;
  SecNameOff = 0;
}

void BTFDebug::beginInstruction(const MachineInstr *MI) {
  DebugHandlerBase::beginInstruction(MI);

  if (SkipInstruction || MI->isMetaInstruction() ||
      MI->getFlag(MachineInstr::FrameSetup))
    return;

  if (MI->isInlineAsm()) {
    // Count the number of register definitions to find the asm string.
    unsigned NumDefs = 0;
    for (; MI->getOperand(NumDefs).isReg() && MI->getOperand(NumDefs).isDef();
         ++NumDefs)
      ;

    // Skip this inline asm instruction if the asmstr is empty.
    const char *AsmStr = MI->getOperand(NumDefs).getSymbolName();
    if (AsmStr[0] == 0)
      return;
  }

  // Skip this instruction if no DebugLoc or the DebugLoc
  // is the same as the previous instruction.
  const DebugLoc &DL = MI->getDebugLoc();
  if (!DL || PrevInstLoc == DL) {
    // This instruction will be skipped, no LineInfo has
    // been generated, construct one based on function signature.
    if (LineInfoGenerated == false) {
      auto *S = MI->getMF()->getFunction().getSubprogram();
      MCSymbol *FuncLabel = Asm->getFunctionBegin();
      constructLineInfo(S, FuncLabel, S->getLine(), 0);
      LineInfoGenerated = true;
    }

    return;
  }

  // Create a temporary label to remember the insn for lineinfo.
  MCSymbol *LineSym = OS.getContext().createTempSymbol();
  OS.EmitLabel(LineSym);

  // Construct the lineinfo.
  auto SP = DL.get()->getScope()->getSubprogram();
  constructLineInfo(SP, LineSym, DL.getLine(), DL.getCol());

  LineInfoGenerated = true;
  PrevInstLoc = DL;
}

void BTFDebug::endModule() {
  // Collect all types referenced by globals.
  const Module *M = MMI->getModule();
  for (const DICompileUnit *CUNode : M->debug_compile_units()) {
    for (const auto *GVE : CUNode->getGlobalVariables()) {
      DIGlobalVariable *GV = GVE->getVariable();
      visitTypeEntry(GV->getType().resolve());
    }
  }

  // Complete BTF type cross refereences.
  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->completeType(*this);

  // Emit BTF sections.
  emitBTFSection();
  emitBTFExtSection();
}
