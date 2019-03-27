//===-- TypeDumpVisitor.cpp - CodeView type info dumper ----------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeDumpVisitor.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace llvm::codeview;

static const EnumEntry<TypeLeafKind> LeafTypeNames[] = {
#define CV_TYPE(enum, val) {#enum, enum},
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
};

#define ENUM_ENTRY(enum_class, enum)                                           \
  { #enum, std::underlying_type < enum_class > ::type(enum_class::enum) }

static const EnumEntry<uint16_t> ClassOptionNames[] = {
    ENUM_ENTRY(ClassOptions, Packed),
    ENUM_ENTRY(ClassOptions, HasConstructorOrDestructor),
    ENUM_ENTRY(ClassOptions, HasOverloadedOperator),
    ENUM_ENTRY(ClassOptions, Nested),
    ENUM_ENTRY(ClassOptions, ContainsNestedClass),
    ENUM_ENTRY(ClassOptions, HasOverloadedAssignmentOperator),
    ENUM_ENTRY(ClassOptions, HasConversionOperator),
    ENUM_ENTRY(ClassOptions, ForwardReference),
    ENUM_ENTRY(ClassOptions, Scoped),
    ENUM_ENTRY(ClassOptions, HasUniqueName),
    ENUM_ENTRY(ClassOptions, Sealed),
    ENUM_ENTRY(ClassOptions, Intrinsic),
};

static const EnumEntry<uint8_t> MemberAccessNames[] = {
    ENUM_ENTRY(MemberAccess, None), ENUM_ENTRY(MemberAccess, Private),
    ENUM_ENTRY(MemberAccess, Protected), ENUM_ENTRY(MemberAccess, Public),
};

static const EnumEntry<uint16_t> MethodOptionNames[] = {
    ENUM_ENTRY(MethodOptions, Pseudo),
    ENUM_ENTRY(MethodOptions, NoInherit),
    ENUM_ENTRY(MethodOptions, NoConstruct),
    ENUM_ENTRY(MethodOptions, CompilerGenerated),
    ENUM_ENTRY(MethodOptions, Sealed),
};

static const EnumEntry<uint16_t> MemberKindNames[] = {
    ENUM_ENTRY(MethodKind, Vanilla),
    ENUM_ENTRY(MethodKind, Virtual),
    ENUM_ENTRY(MethodKind, Static),
    ENUM_ENTRY(MethodKind, Friend),
    ENUM_ENTRY(MethodKind, IntroducingVirtual),
    ENUM_ENTRY(MethodKind, PureVirtual),
    ENUM_ENTRY(MethodKind, PureIntroducingVirtual),
};

static const EnumEntry<uint8_t> PtrKindNames[] = {
    ENUM_ENTRY(PointerKind, Near16),
    ENUM_ENTRY(PointerKind, Far16),
    ENUM_ENTRY(PointerKind, Huge16),
    ENUM_ENTRY(PointerKind, BasedOnSegment),
    ENUM_ENTRY(PointerKind, BasedOnValue),
    ENUM_ENTRY(PointerKind, BasedOnSegmentValue),
    ENUM_ENTRY(PointerKind, BasedOnAddress),
    ENUM_ENTRY(PointerKind, BasedOnSegmentAddress),
    ENUM_ENTRY(PointerKind, BasedOnType),
    ENUM_ENTRY(PointerKind, BasedOnSelf),
    ENUM_ENTRY(PointerKind, Near32),
    ENUM_ENTRY(PointerKind, Far32),
    ENUM_ENTRY(PointerKind, Near64),
};

static const EnumEntry<uint8_t> PtrModeNames[] = {
    ENUM_ENTRY(PointerMode, Pointer),
    ENUM_ENTRY(PointerMode, LValueReference),
    ENUM_ENTRY(PointerMode, PointerToDataMember),
    ENUM_ENTRY(PointerMode, PointerToMemberFunction),
    ENUM_ENTRY(PointerMode, RValueReference),
};

static const EnumEntry<uint16_t> PtrMemberRepNames[] = {
    ENUM_ENTRY(PointerToMemberRepresentation, Unknown),
    ENUM_ENTRY(PointerToMemberRepresentation, SingleInheritanceData),
    ENUM_ENTRY(PointerToMemberRepresentation, MultipleInheritanceData),
    ENUM_ENTRY(PointerToMemberRepresentation, VirtualInheritanceData),
    ENUM_ENTRY(PointerToMemberRepresentation, GeneralData),
    ENUM_ENTRY(PointerToMemberRepresentation, SingleInheritanceFunction),
    ENUM_ENTRY(PointerToMemberRepresentation, MultipleInheritanceFunction),
    ENUM_ENTRY(PointerToMemberRepresentation, VirtualInheritanceFunction),
    ENUM_ENTRY(PointerToMemberRepresentation, GeneralFunction),
};

static const EnumEntry<uint16_t> TypeModifierNames[] = {
    ENUM_ENTRY(ModifierOptions, Const), ENUM_ENTRY(ModifierOptions, Volatile),
    ENUM_ENTRY(ModifierOptions, Unaligned),
};

static const EnumEntry<uint8_t> CallingConventions[] = {
    ENUM_ENTRY(CallingConvention, NearC),
    ENUM_ENTRY(CallingConvention, FarC),
    ENUM_ENTRY(CallingConvention, NearPascal),
    ENUM_ENTRY(CallingConvention, FarPascal),
    ENUM_ENTRY(CallingConvention, NearFast),
    ENUM_ENTRY(CallingConvention, FarFast),
    ENUM_ENTRY(CallingConvention, NearStdCall),
    ENUM_ENTRY(CallingConvention, FarStdCall),
    ENUM_ENTRY(CallingConvention, NearSysCall),
    ENUM_ENTRY(CallingConvention, FarSysCall),
    ENUM_ENTRY(CallingConvention, ThisCall),
    ENUM_ENTRY(CallingConvention, MipsCall),
    ENUM_ENTRY(CallingConvention, Generic),
    ENUM_ENTRY(CallingConvention, AlphaCall),
    ENUM_ENTRY(CallingConvention, PpcCall),
    ENUM_ENTRY(CallingConvention, SHCall),
    ENUM_ENTRY(CallingConvention, ArmCall),
    ENUM_ENTRY(CallingConvention, AM33Call),
    ENUM_ENTRY(CallingConvention, TriCall),
    ENUM_ENTRY(CallingConvention, SH5Call),
    ENUM_ENTRY(CallingConvention, M32RCall),
    ENUM_ENTRY(CallingConvention, ClrCall),
    ENUM_ENTRY(CallingConvention, Inline),
    ENUM_ENTRY(CallingConvention, NearVector),
};

static const EnumEntry<uint8_t> FunctionOptionEnum[] = {
    ENUM_ENTRY(FunctionOptions, CxxReturnUdt),
    ENUM_ENTRY(FunctionOptions, Constructor),
    ENUM_ENTRY(FunctionOptions, ConstructorWithVirtualBases),
};

static const EnumEntry<uint16_t> LabelTypeEnum[] = {
    ENUM_ENTRY(LabelType, Near), ENUM_ENTRY(LabelType, Far),
};

#undef ENUM_ENTRY

static StringRef getLeafTypeName(TypeLeafKind LT) {
  switch (LT) {
#define TYPE_RECORD(ename, value, name)                                        \
  case ename:                                                                  \
    return #name;
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default:
    break;
  }
  return "UnknownLeaf";
}

void TypeDumpVisitor::printTypeIndex(StringRef FieldName, TypeIndex TI) const {
  codeview::printTypeIndex(*W, FieldName, TI, TpiTypes);
}

void TypeDumpVisitor::printItemIndex(StringRef FieldName, TypeIndex TI) const {
  codeview::printTypeIndex(*W, FieldName, TI, getSourceTypes());
}

Error TypeDumpVisitor::visitTypeBegin(CVType &Record) {
  return visitTypeBegin(Record, TypeIndex::fromArrayIndex(TpiTypes.size()));
}

Error TypeDumpVisitor::visitTypeBegin(CVType &Record, TypeIndex Index) {
  W->startLine() << getLeafTypeName(Record.Type);
  W->getOStream() << " (" << HexNumber(Index.getIndex()) << ")";
  W->getOStream() << " {\n";
  W->indent();
  W->printEnum("TypeLeafKind", unsigned(Record.Type),
               makeArrayRef(LeafTypeNames));
  return Error::success();
}

Error TypeDumpVisitor::visitTypeEnd(CVType &Record) {
  if (PrintRecordBytes)
    W->printBinaryBlock("LeafData", getBytesAsCharacters(Record.content()));

  W->unindent();
  W->startLine() << "}\n";
  return Error::success();
}

Error TypeDumpVisitor::visitMemberBegin(CVMemberRecord &Record) {
  W->startLine() << getLeafTypeName(Record.Kind);
  W->getOStream() << " {\n";
  W->indent();
  W->printEnum("TypeLeafKind", unsigned(Record.Kind),
               makeArrayRef(LeafTypeNames));
  return Error::success();
}

Error TypeDumpVisitor::visitMemberEnd(CVMemberRecord &Record) {
  if (PrintRecordBytes)
    W->printBinaryBlock("LeafData", getBytesAsCharacters(Record.Data));

  W->unindent();
  W->startLine() << "}\n";
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        FieldListRecord &FieldList) {
  if (auto EC = codeview::visitMemberRecordStream(FieldList.Data, *this))
    return EC;

  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, StringIdRecord &String) {
  printItemIndex("Id", String.getId());
  W->printString("StringData", String.getString());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, ArgListRecord &Args) {
  auto Indices = Args.getIndices();
  uint32_t Size = Indices.size();
  W->printNumber("NumArgs", Size);
  ListScope Arguments(*W, "Arguments");
  for (uint32_t I = 0; I < Size; ++I) {
    printTypeIndex("ArgType", Indices[I]);
  }
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, StringListRecord &Strs) {
  auto Indices = Strs.getIndices();
  uint32_t Size = Indices.size();
  W->printNumber("NumStrings", Size);
  ListScope Arguments(*W, "Strings");
  for (uint32_t I = 0; I < Size; ++I) {
    printItemIndex("String", Indices[I]);
  }
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, ClassRecord &Class) {
  uint16_t Props = static_cast<uint16_t>(Class.getOptions());
  W->printNumber("MemberCount", Class.getMemberCount());
  W->printFlags("Properties", Props, makeArrayRef(ClassOptionNames));
  printTypeIndex("FieldList", Class.getFieldList());
  printTypeIndex("DerivedFrom", Class.getDerivationList());
  printTypeIndex("VShape", Class.getVTableShape());
  W->printNumber("SizeOf", Class.getSize());
  W->printString("Name", Class.getName());
  if (Props & uint16_t(ClassOptions::HasUniqueName))
    W->printString("LinkageName", Class.getUniqueName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, UnionRecord &Union) {
  uint16_t Props = static_cast<uint16_t>(Union.getOptions());
  W->printNumber("MemberCount", Union.getMemberCount());
  W->printFlags("Properties", Props, makeArrayRef(ClassOptionNames));
  printTypeIndex("FieldList", Union.getFieldList());
  W->printNumber("SizeOf", Union.getSize());
  W->printString("Name", Union.getName());
  if (Props & uint16_t(ClassOptions::HasUniqueName))
    W->printString("LinkageName", Union.getUniqueName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, EnumRecord &Enum) {
  uint16_t Props = static_cast<uint16_t>(Enum.getOptions());
  W->printNumber("NumEnumerators", Enum.getMemberCount());
  W->printFlags("Properties", uint16_t(Enum.getOptions()),
                makeArrayRef(ClassOptionNames));
  printTypeIndex("UnderlyingType", Enum.getUnderlyingType());
  printTypeIndex("FieldListType", Enum.getFieldList());
  W->printString("Name", Enum.getName());
  if (Props & uint16_t(ClassOptions::HasUniqueName))
    W->printString("LinkageName", Enum.getUniqueName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, ArrayRecord &AT) {
  printTypeIndex("ElementType", AT.getElementType());
  printTypeIndex("IndexType", AT.getIndexType());
  W->printNumber("SizeOf", AT.getSize());
  W->printString("Name", AT.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, VFTableRecord &VFT) {
  printTypeIndex("CompleteClass", VFT.getCompleteClass());
  printTypeIndex("OverriddenVFTable", VFT.getOverriddenVTable());
  W->printHex("VFPtrOffset", VFT.getVFPtrOffset());
  W->printString("VFTableName", VFT.getName());
  for (auto N : VFT.getMethodNames())
    W->printString("MethodName", N);
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, MemberFuncIdRecord &Id) {
  printTypeIndex("ClassType", Id.getClassType());
  printTypeIndex("FunctionType", Id.getFunctionType());
  W->printString("Name", Id.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, ProcedureRecord &Proc) {
  printTypeIndex("ReturnType", Proc.getReturnType());
  W->printEnum("CallingConvention", uint8_t(Proc.getCallConv()),
               makeArrayRef(CallingConventions));
  W->printFlags("FunctionOptions", uint8_t(Proc.getOptions()),
                makeArrayRef(FunctionOptionEnum));
  W->printNumber("NumParameters", Proc.getParameterCount());
  printTypeIndex("ArgListType", Proc.getArgumentList());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, MemberFunctionRecord &MF) {
  printTypeIndex("ReturnType", MF.getReturnType());
  printTypeIndex("ClassType", MF.getClassType());
  printTypeIndex("ThisType", MF.getThisType());
  W->printEnum("CallingConvention", uint8_t(MF.getCallConv()),
               makeArrayRef(CallingConventions));
  W->printFlags("FunctionOptions", uint8_t(MF.getOptions()),
                makeArrayRef(FunctionOptionEnum));
  W->printNumber("NumParameters", MF.getParameterCount());
  printTypeIndex("ArgListType", MF.getArgumentList());
  W->printNumber("ThisAdjustment", MF.getThisPointerAdjustment());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        MethodOverloadListRecord &MethodList) {
  for (auto &M : MethodList.getMethods()) {
    ListScope S(*W, "Method");
    printMemberAttributes(M.getAccess(), M.getMethodKind(), M.getOptions());
    printTypeIndex("Type", M.getType());
    if (M.isIntroducingVirtual())
      W->printHex("VFTableOffset", M.getVFTableOffset());
  }
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, FuncIdRecord &Func) {
  printItemIndex("ParentScope", Func.getParentScope());
  printTypeIndex("FunctionType", Func.getFunctionType());
  W->printString("Name", Func.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, TypeServer2Record &TS) {
  W->printString("Guid", formatv("{0}", TS.getGuid()).str());
  W->printNumber("Age", TS.getAge());
  W->printString("Name", TS.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, PointerRecord &Ptr) {
  printTypeIndex("PointeeType", Ptr.getReferentType());
  W->printEnum("PtrType", unsigned(Ptr.getPointerKind()),
               makeArrayRef(PtrKindNames));
  W->printEnum("PtrMode", unsigned(Ptr.getMode()), makeArrayRef(PtrModeNames));

  W->printNumber("IsFlat", Ptr.isFlat());
  W->printNumber("IsConst", Ptr.isConst());
  W->printNumber("IsVolatile", Ptr.isVolatile());
  W->printNumber("IsUnaligned", Ptr.isUnaligned());
  W->printNumber("IsRestrict", Ptr.isRestrict());
  W->printNumber("IsThisPtr&", Ptr.isLValueReferenceThisPtr());
  W->printNumber("IsThisPtr&&", Ptr.isRValueReferenceThisPtr());
  W->printNumber("SizeOf", Ptr.getSize());

  if (Ptr.isPointerToMember()) {
    const MemberPointerInfo &MI = Ptr.getMemberInfo();

    printTypeIndex("ClassType", MI.getContainingType());
    W->printEnum("Representation", uint16_t(MI.getRepresentation()),
                 makeArrayRef(PtrMemberRepNames));
  }

  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, ModifierRecord &Mod) {
  uint16_t Mods = static_cast<uint16_t>(Mod.getModifiers());
  printTypeIndex("ModifiedType", Mod.getModifiedType());
  W->printFlags("Modifiers", Mods, makeArrayRef(TypeModifierNames));

  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, BitFieldRecord &BitField) {
  printTypeIndex("Type", BitField.getType());
  W->printNumber("BitSize", BitField.getBitSize());
  W->printNumber("BitOffset", BitField.getBitOffset());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        VFTableShapeRecord &Shape) {
  W->printNumber("VFEntryCount", Shape.getEntryCount());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        UdtSourceLineRecord &Line) {
  printTypeIndex("UDT", Line.getUDT());
  printItemIndex("SourceFile", Line.getSourceFile());
  W->printNumber("LineNumber", Line.getLineNumber());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        UdtModSourceLineRecord &Line) {
  printTypeIndex("UDT", Line.getUDT());
  printItemIndex("SourceFile", Line.getSourceFile());
  W->printNumber("LineNumber", Line.getLineNumber());
  W->printNumber("Module", Line.getModule());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, BuildInfoRecord &Args) {
  W->printNumber("NumArgs", static_cast<uint32_t>(Args.getArgs().size()));

  ListScope Arguments(*W, "Arguments");
  for (auto Arg : Args.getArgs()) {
    printItemIndex("ArgType", Arg);
  }
  return Error::success();
}

void TypeDumpVisitor::printMemberAttributes(MemberAttributes Attrs) {
  return printMemberAttributes(Attrs.getAccess(), Attrs.getMethodKind(),
                               Attrs.getFlags());
}

void TypeDumpVisitor::printMemberAttributes(MemberAccess Access,
                                            MethodKind Kind,
                                            MethodOptions Options) {
  W->printEnum("AccessSpecifier", uint8_t(Access),
               makeArrayRef(MemberAccessNames));
  // Data members will be vanilla. Don't try to print a method kind for them.
  if (Kind != MethodKind::Vanilla)
    W->printEnum("MethodKind", unsigned(Kind), makeArrayRef(MemberKindNames));
  if (Options != MethodOptions::None) {
    W->printFlags("MethodOptions", unsigned(Options),
                  makeArrayRef(MethodOptionNames));
  }
}

Error TypeDumpVisitor::visitUnknownMember(CVMemberRecord &Record) {
  W->printHex("UnknownMember", unsigned(Record.Kind));
  return Error::success();
}

Error TypeDumpVisitor::visitUnknownType(CVType &Record) {
  W->printEnum("Kind", uint16_t(Record.kind()), makeArrayRef(LeafTypeNames));
  W->printNumber("Length", uint32_t(Record.content().size()));
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        NestedTypeRecord &Nested) {
  printTypeIndex("Type", Nested.getNestedType());
  W->printString("Name", Nested.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        OneMethodRecord &Method) {
  MethodKind K = Method.getMethodKind();
  printMemberAttributes(Method.getAccess(), K, Method.getOptions());
  printTypeIndex("Type", Method.getType());
  // If virtual, then read the vftable offset.
  if (Method.isIntroducingVirtual())
    W->printHex("VFTableOffset", Method.getVFTableOffset());
  W->printString("Name", Method.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        OverloadedMethodRecord &Method) {
  W->printHex("MethodCount", Method.getNumOverloads());
  printTypeIndex("MethodListIndex", Method.getMethodList());
  W->printString("Name", Method.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        DataMemberRecord &Field) {
  printMemberAttributes(Field.getAccess(), MethodKind::Vanilla,
                        MethodOptions::None);
  printTypeIndex("Type", Field.getType());
  W->printHex("FieldOffset", Field.getFieldOffset());
  W->printString("Name", Field.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        StaticDataMemberRecord &Field) {
  printMemberAttributes(Field.getAccess(), MethodKind::Vanilla,
                        MethodOptions::None);
  printTypeIndex("Type", Field.getType());
  W->printString("Name", Field.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        VFPtrRecord &VFTable) {
  printTypeIndex("Type", VFTable.getType());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        EnumeratorRecord &Enum) {
  printMemberAttributes(Enum.getAccess(), MethodKind::Vanilla,
                        MethodOptions::None);
  W->printNumber("EnumValue", Enum.getValue());
  W->printString("Name", Enum.getName());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        BaseClassRecord &Base) {
  printMemberAttributes(Base.getAccess(), MethodKind::Vanilla,
                        MethodOptions::None);
  printTypeIndex("BaseType", Base.getBaseType());
  W->printHex("BaseOffset", Base.getBaseOffset());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        VirtualBaseClassRecord &Base) {
  printMemberAttributes(Base.getAccess(), MethodKind::Vanilla,
                        MethodOptions::None);
  printTypeIndex("BaseType", Base.getBaseType());
  printTypeIndex("VBPtrType", Base.getVBPtrType());
  W->printHex("VBPtrOffset", Base.getVBPtrOffset());
  W->printHex("VBTableIndex", Base.getVTableIndex());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                        ListContinuationRecord &Cont) {
  printTypeIndex("ContinuationIndex", Cont.getContinuationIndex());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR, LabelRecord &LR) {
  W->printEnum("Mode", uint16_t(LR.Mode), makeArrayRef(LabelTypeEnum));
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        PrecompRecord &Precomp) {
  W->printHex("StartIndex", Precomp.getStartTypeIndex());
  W->printHex("Count", Precomp.getTypesCount());
  W->printHex("Signature", Precomp.getSignature());
  W->printString("PrecompFile", Precomp.getPrecompFilePath());
  return Error::success();
}

Error TypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                        EndPrecompRecord &EndPrecomp) {
  W->printHex("Signature", EndPrecomp.getSignature());
  return Error::success();
}
