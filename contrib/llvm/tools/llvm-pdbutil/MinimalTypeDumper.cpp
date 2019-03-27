//===- MinimalTypeDumper.cpp ---------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MinimalTypeDumper.h"

#include "FormatUtil.h"
#include "LinePrinter.h"

#include "llvm-pdbutil.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

static std::string formatClassOptions(uint32_t IndentLevel,
                                      ClassOptions Options, TpiStream *Stream,
                                      TypeIndex CurrentTypeIndex) {
  std::vector<std::string> Opts;

  if (Stream && Stream->supportsTypeLookup() &&
      !opts::dump::DontResolveForwardRefs &&
      ((Options & ClassOptions::ForwardReference) != ClassOptions::None)) {
    // If we're able to resolve forward references, do that.
    Expected<TypeIndex> ETI =
        Stream->findFullDeclForForwardRef(CurrentTypeIndex);
    if (!ETI) {
      consumeError(ETI.takeError());
      PUSH_FLAG(ClassOptions, ForwardReference, Options, "forward ref (??\?)");
    } else {
      const char *Direction = (*ETI == CurrentTypeIndex)
                                  ? "="
                                  : ((*ETI < CurrentTypeIndex) ? "<-" : "->");
      std::string Formatted =
          formatv("forward ref ({0} {1})", Direction, *ETI).str();
      PUSH_FLAG(ClassOptions, ForwardReference, Options, std::move(Formatted));
    }
  } else {
    PUSH_FLAG(ClassOptions, ForwardReference, Options, "forward ref");
  }

  PUSH_FLAG(ClassOptions, HasConstructorOrDestructor, Options,
            "has ctor / dtor");
  PUSH_FLAG(ClassOptions, ContainsNestedClass, Options,
            "contains nested class");
  PUSH_FLAG(ClassOptions, HasConversionOperator, Options,
            "conversion operator");
  PUSH_FLAG(ClassOptions, HasUniqueName, Options, "has unique name");
  PUSH_FLAG(ClassOptions, Intrinsic, Options, "intrin");
  PUSH_FLAG(ClassOptions, Nested, Options, "is nested");
  PUSH_FLAG(ClassOptions, HasOverloadedOperator, Options,
            "overloaded operator");
  PUSH_FLAG(ClassOptions, HasOverloadedAssignmentOperator, Options,
            "overloaded operator=");
  PUSH_FLAG(ClassOptions, Packed, Options, "packed");
  PUSH_FLAG(ClassOptions, Scoped, Options, "scoped");
  PUSH_FLAG(ClassOptions, Sealed, Options, "sealed");

  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string pointerOptions(PointerOptions Options) {
  std::vector<std::string> Opts;
  PUSH_FLAG(PointerOptions, Flat32, Options, "flat32");
  PUSH_FLAG(PointerOptions, Volatile, Options, "volatile");
  PUSH_FLAG(PointerOptions, Const, Options, "const");
  PUSH_FLAG(PointerOptions, Unaligned, Options, "unaligned");
  PUSH_FLAG(PointerOptions, Restrict, Options, "restrict");
  PUSH_FLAG(PointerOptions, WinRTSmartPointer, Options, "winrt");
  if (Opts.empty())
    return "None";
  return join(Opts, " | ");
}

static std::string modifierOptions(ModifierOptions Options) {
  std::vector<std::string> Opts;
  PUSH_FLAG(ModifierOptions, Const, Options, "const");
  PUSH_FLAG(ModifierOptions, Volatile, Options, "volatile");
  PUSH_FLAG(ModifierOptions, Unaligned, Options, "unaligned");
  if (Opts.empty())
    return "None";
  return join(Opts, " | ");
}

static std::string formatCallingConvention(CallingConvention Convention) {
  switch (Convention) {
    RETURN_CASE(CallingConvention, AlphaCall, "alphacall");
    RETURN_CASE(CallingConvention, AM33Call, "am33call");
    RETURN_CASE(CallingConvention, ArmCall, "armcall");
    RETURN_CASE(CallingConvention, ClrCall, "clrcall");
    RETURN_CASE(CallingConvention, FarC, "far cdecl");
    RETURN_CASE(CallingConvention, FarFast, "far fastcall");
    RETURN_CASE(CallingConvention, FarPascal, "far pascal");
    RETURN_CASE(CallingConvention, FarStdCall, "far stdcall");
    RETURN_CASE(CallingConvention, FarSysCall, "far syscall");
    RETURN_CASE(CallingConvention, Generic, "generic");
    RETURN_CASE(CallingConvention, Inline, "inline");
    RETURN_CASE(CallingConvention, M32RCall, "m32rcall");
    RETURN_CASE(CallingConvention, MipsCall, "mipscall");
    RETURN_CASE(CallingConvention, NearC, "cdecl");
    RETURN_CASE(CallingConvention, NearFast, "fastcall");
    RETURN_CASE(CallingConvention, NearPascal, "pascal");
    RETURN_CASE(CallingConvention, NearStdCall, "stdcall");
    RETURN_CASE(CallingConvention, NearSysCall, "near syscall");
    RETURN_CASE(CallingConvention, NearVector, "vectorcall");
    RETURN_CASE(CallingConvention, PpcCall, "ppccall");
    RETURN_CASE(CallingConvention, SHCall, "shcall");
    RETURN_CASE(CallingConvention, SH5Call, "sh5call");
    RETURN_CASE(CallingConvention, ThisCall, "thiscall");
    RETURN_CASE(CallingConvention, TriCall, "tricall");
  }
  return formatUnknownEnum(Convention);
}

static std::string formatPointerMode(PointerMode Mode) {
  switch (Mode) {
    RETURN_CASE(PointerMode, LValueReference, "ref");
    RETURN_CASE(PointerMode, Pointer, "pointer");
    RETURN_CASE(PointerMode, PointerToDataMember, "data member pointer");
    RETURN_CASE(PointerMode, PointerToMemberFunction, "member fn pointer");
    RETURN_CASE(PointerMode, RValueReference, "rvalue ref");
  }
  return formatUnknownEnum(Mode);
}

static std::string memberAccess(MemberAccess Access) {
  switch (Access) {
    RETURN_CASE(MemberAccess, None, "");
    RETURN_CASE(MemberAccess, Private, "private");
    RETURN_CASE(MemberAccess, Protected, "protected");
    RETURN_CASE(MemberAccess, Public, "public");
  }
  return formatUnknownEnum(Access);
}

static std::string methodKind(MethodKind Kind) {
  switch (Kind) {
    RETURN_CASE(MethodKind, Vanilla, "");
    RETURN_CASE(MethodKind, Virtual, "virtual");
    RETURN_CASE(MethodKind, Static, "static");
    RETURN_CASE(MethodKind, Friend, "friend");
    RETURN_CASE(MethodKind, IntroducingVirtual, "intro virtual");
    RETURN_CASE(MethodKind, PureVirtual, "pure virtual");
    RETURN_CASE(MethodKind, PureIntroducingVirtual, "pure intro virtual");
  }
  return formatUnknownEnum(Kind);
}

static std::string pointerKind(PointerKind Kind) {
  switch (Kind) {
    RETURN_CASE(PointerKind, Near16, "ptr16");
    RETURN_CASE(PointerKind, Far16, "far ptr16");
    RETURN_CASE(PointerKind, Huge16, "huge ptr16");
    RETURN_CASE(PointerKind, BasedOnSegment, "segment based");
    RETURN_CASE(PointerKind, BasedOnValue, "value based");
    RETURN_CASE(PointerKind, BasedOnSegmentValue, "segment value based");
    RETURN_CASE(PointerKind, BasedOnAddress, "address based");
    RETURN_CASE(PointerKind, BasedOnSegmentAddress, "segment address based");
    RETURN_CASE(PointerKind, BasedOnType, "type based");
    RETURN_CASE(PointerKind, BasedOnSelf, "self based");
    RETURN_CASE(PointerKind, Near32, "ptr32");
    RETURN_CASE(PointerKind, Far32, "far ptr32");
    RETURN_CASE(PointerKind, Near64, "ptr64");
  }
  return formatUnknownEnum(Kind);
}

static std::string memberAttributes(const MemberAttributes &Attrs) {
  std::vector<std::string> Opts;
  std::string Access = memberAccess(Attrs.getAccess());
  std::string Kind = methodKind(Attrs.getMethodKind());
  if (!Access.empty())
    Opts.push_back(Access);
  if (!Kind.empty())
    Opts.push_back(Kind);
  MethodOptions Flags = Attrs.getFlags();
  PUSH_FLAG(MethodOptions, Pseudo, Flags, "pseudo");
  PUSH_FLAG(MethodOptions, NoInherit, Flags, "noinherit");
  PUSH_FLAG(MethodOptions, NoConstruct, Flags, "noconstruct");
  PUSH_FLAG(MethodOptions, CompilerGenerated, Flags, "compiler-generated");
  PUSH_FLAG(MethodOptions, Sealed, Flags, "sealed");
  return join(Opts, " ");
}

static std::string formatPointerAttrs(const PointerRecord &Record) {
  PointerMode Mode = Record.getMode();
  PointerOptions Opts = Record.getOptions();
  PointerKind Kind = Record.getPointerKind();
  return formatv("mode = {0}, opts = {1}, kind = {2}", formatPointerMode(Mode),
                 pointerOptions(Opts), pointerKind(Kind));
}

static std::string formatFunctionOptions(FunctionOptions Options) {
  std::vector<std::string> Opts;

  PUSH_FLAG(FunctionOptions, CxxReturnUdt, Options, "returns cxx udt");
  PUSH_FLAG(FunctionOptions, ConstructorWithVirtualBases, Options,
            "constructor with virtual bases");
  PUSH_FLAG(FunctionOptions, Constructor, Options, "constructor");
  if (Opts.empty())
    return "None";
  return join(Opts, " | ");
}

Error MinimalTypeDumpVisitor::visitTypeBegin(CVType &Record, TypeIndex Index) {
  CurrentTypeIndex = Index;
  // formatLine puts the newline at the beginning, so we use formatLine here
  // to start a new line, and then individual visit methods use format to
  // append to the existing line.
  if (!Hashes) {
    P.formatLine("{0} | {1} [size = {2}]",
                 fmt_align(Index, AlignStyle::Right, Width),
                 formatTypeLeafKind(Record.Type), Record.length());
  } else {
    std::string H;
    if (Index.toArrayIndex() >= HashValues.size()) {
      H = "(not present)";
    } else {
      uint32_t Hash = HashValues[Index.toArrayIndex()];
      Expected<uint32_t> MaybeHash = hashTypeRecord(Record);
      if (!MaybeHash)
        return MaybeHash.takeError();
      uint32_t OurHash = *MaybeHash;
      OurHash %= NumHashBuckets;
      if (Hash == OurHash)
        H = "0x" + utohexstr(Hash);
      else
        H = "0x" + utohexstr(Hash) + ", our hash = 0x" + utohexstr(OurHash);
    }
    P.formatLine("{0} | {1} [size = {2}, hash = {3}]",
                 fmt_align(Index, AlignStyle::Right, Width),
                 formatTypeLeafKind(Record.Type), Record.length(), H);
  }
  P.Indent(Width + 3);
  return Error::success();
}
Error MinimalTypeDumpVisitor::visitTypeEnd(CVType &Record) {
  P.Unindent(Width + 3);
  if (RecordBytes) {
    AutoIndent Indent(P, 9);
    P.formatBinary("Bytes", Record.RecordData, 0);
  }
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitMemberBegin(CVMemberRecord &Record) {
  P.formatLine("- {0}", formatTypeLeafKind(Record.Kind));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitMemberEnd(CVMemberRecord &Record) {
  if (RecordBytes) {
    AutoIndent Indent(P, 2);
    P.formatBinary("Bytes", Record.Data, 0);
  }
  return Error::success();
}

StringRef MinimalTypeDumpVisitor::getTypeName(TypeIndex TI) const {
  if (TI.isNoneType())
    return "";
  return Types.getTypeName(TI);
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               FieldListRecord &FieldList) {
  if (auto EC = codeview::visitMemberRecordStream(FieldList.Data, *this))
    return EC;

  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               StringIdRecord &String) {
  P.format(" ID: {0}, String: {1}", String.getId(), String.getString());
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               ArgListRecord &Args) {
  auto Indices = Args.getIndices();
  if (Indices.empty())
    return Error::success();

  auto Max = std::max_element(Indices.begin(), Indices.end());
  uint32_t W = NumDigits(Max->getIndex()) + 2;

  for (auto I : Indices)
    P.formatLine("{0}: `{1}`", fmt_align(I, AlignStyle::Right, W),
                 getTypeName(I));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               StringListRecord &Strings) {
  auto Indices = Strings.getIndices();
  if (Indices.empty())
    return Error::success();

  auto Max = std::max_element(Indices.begin(), Indices.end());
  uint32_t W = NumDigits(Max->getIndex()) + 2;

  for (auto I : Indices)
    P.formatLine("{0}: `{1}`", fmt_align(I, AlignStyle::Right, W),
                 getTypeName(I));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               ClassRecord &Class) {
  P.format(" `{0}`", Class.Name);
  if (Class.hasUniqueName())
    P.formatLine("unique name: `{0}`", Class.UniqueName);
  P.formatLine("vtable: {0}, base list: {1}, field list: {2}",
               Class.VTableShape, Class.DerivationList, Class.FieldList);
  P.formatLine("options: {0}, sizeof {1}",
               formatClassOptions(P.getIndentLevel(), Class.Options, Stream,
                                  CurrentTypeIndex),
               Class.Size);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               UnionRecord &Union) {
  P.format(" `{0}`", Union.Name);
  if (Union.hasUniqueName())
    P.formatLine("unique name: `{0}`", Union.UniqueName);
  P.formatLine("field list: {0}", Union.FieldList);
  P.formatLine("options: {0}, sizeof {1}",
               formatClassOptions(P.getIndentLevel(), Union.Options, Stream,
                                  CurrentTypeIndex),
               Union.Size);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR, EnumRecord &Enum) {
  P.format(" `{0}`", Enum.Name);
  if (Enum.hasUniqueName())
    P.formatLine("unique name: `{0}`", Enum.UniqueName);
  P.formatLine("field list: {0}, underlying type: {1}", Enum.FieldList,
               Enum.UnderlyingType);
  P.formatLine("options: {0}",
               formatClassOptions(P.getIndentLevel(), Enum.Options, Stream,
                                  CurrentTypeIndex));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR, ArrayRecord &AT) {
  if (AT.Name.empty()) {
    P.formatLine("size: {0}, index type: {1}, element type: {2}", AT.Size,
                 AT.IndexType, AT.ElementType);
  } else {
    P.formatLine("name: {0}, size: {1}, index type: {2}, element type: {3}",
                 AT.Name, AT.Size, AT.IndexType, AT.ElementType);
  }
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               VFTableRecord &VFT) {
  P.formatLine("offset: {0}, complete class: {1}, overridden vftable: {2}",
               VFT.VFPtrOffset, VFT.CompleteClass, VFT.OverriddenVFTable);
  P.formatLine("method names: ");
  if (!VFT.MethodNames.empty()) {
    std::string Sep =
        formatv("\n{0}",
                fmt_repeat(' ', P.getIndentLevel() + strlen("method names: ")))
            .str();
    P.print(join(VFT.MethodNames, Sep));
  }
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               MemberFuncIdRecord &Id) {
  P.formatLine("name = {0}, type = {1}, class type = {2}", Id.Name,
               Id.FunctionType, Id.ClassType);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               ProcedureRecord &Proc) {
  P.formatLine("return type = {0}, # args = {1}, param list = {2}",
               Proc.ReturnType, Proc.ParameterCount, Proc.ArgumentList);
  P.formatLine("calling conv = {0}, options = {1}",
               formatCallingConvention(Proc.CallConv),
               formatFunctionOptions(Proc.Options));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               MemberFunctionRecord &MF) {
  P.formatLine("return type = {0}, # args = {1}, param list = {2}",
               MF.ReturnType, MF.ParameterCount, MF.ArgumentList);
  P.formatLine("class type = {0}, this type = {1}, this adjust = {2}",
               MF.ClassType, MF.ThisType, MF.ThisPointerAdjustment);
  P.formatLine("calling conv = {0}, options = {1}",
               formatCallingConvention(MF.CallConv),
               formatFunctionOptions(MF.Options));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               FuncIdRecord &Func) {
  P.formatLine("name = {0}, type = {1}, parent scope = {2}", Func.Name,
               Func.FunctionType, Func.ParentScope);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               TypeServer2Record &TS) {
  P.formatLine("name = {0}, age = {1}, guid = {2}", TS.Name, TS.Age, TS.Guid);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               PointerRecord &Ptr) {
  P.formatLine("referent = {0}, {1}", Ptr.ReferentType,
               formatPointerAttrs(Ptr));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               ModifierRecord &Mod) {
  P.formatLine("referent = {0}, modifiers = {1}", Mod.ModifiedType,
               modifierOptions(Mod.Modifiers));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               VFTableShapeRecord &Shape) {
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               UdtModSourceLineRecord &U) {
  P.formatLine("udt = {0}, mod = {1}, file = {2}, line = {3}", U.UDT, U.Module,
               U.SourceFile.getIndex(), U.LineNumber);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               UdtSourceLineRecord &U) {
  P.formatLine("udt = {0}, file = {1}, line = {2}", U.UDT,
               U.SourceFile.getIndex(), U.LineNumber);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               BitFieldRecord &BF) {
  P.formatLine("type = {0}, bit offset = {1}, # bits = {2}", BF.Type,
               BF.BitOffset, BF.BitSize);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(
    CVType &CVR, MethodOverloadListRecord &Overloads) {
  for (auto &M : Overloads.Methods)
    P.formatLine("- Method [type = {0}, vftable offset = {1}, attrs = {2}]",
                 M.Type, M.VFTableOffset, memberAttributes(M.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               BuildInfoRecord &BI) {
  auto Indices = BI.ArgIndices;
  if (Indices.empty())
    return Error::success();

  auto Max = std::max_element(Indices.begin(), Indices.end());
  uint32_t W = NumDigits(Max->getIndex()) + 2;

  for (auto I : Indices)
    P.formatLine("{0}: `{1}`", fmt_align(I, AlignStyle::Right, W),
                 getTypeName(I));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR, LabelRecord &R) {
  std::string Type = (R.Mode == LabelType::Far) ? "far" : "near";
  P.format(" type = {0}", Type);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               PrecompRecord &Precomp) {
  P.format(" start index = {0:X+}, types count = {1:X+}, signature = {2:X+},"
           " precomp path = {3}",
           Precomp.StartTypeIndex, Precomp.TypesCount, Precomp.Signature,
           Precomp.PrecompFilePath);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownRecord(CVType &CVR,
                                               EndPrecompRecord &EP) {
  P.format(" signature = {0:X+}", EP.Signature);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               NestedTypeRecord &Nested) {
  P.format(" [name = `{0}`, parent = {1}]", Nested.Name, Nested.Type);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               OneMethodRecord &Method) {
  P.format(" [name = `{0}`]", Method.Name);
  AutoIndent Indent(P);
  P.formatLine("type = {0}, vftable offset = {1}, attrs = {2}", Method.Type,
               Method.VFTableOffset, memberAttributes(Method.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               OverloadedMethodRecord &Method) {
  P.format(" [name = `{0}`, # overloads = {1}, overload list = {2}]",
           Method.Name, Method.NumOverloads, Method.MethodList);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               DataMemberRecord &Field) {
  P.format(" [name = `{0}`, Type = {1}, offset = {2}, attrs = {3}]", Field.Name,
           Field.Type, Field.FieldOffset, memberAttributes(Field.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               StaticDataMemberRecord &Field) {
  P.format(" [name = `{0}`, type = {1}, attrs = {2}]", Field.Name, Field.Type,
           memberAttributes(Field.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               EnumeratorRecord &Enum) {
  P.format(" [{0} = {1}]", Enum.Name,
           Enum.Value.toString(10, Enum.Value.isSigned()));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               BaseClassRecord &Base) {
  AutoIndent Indent(P);
  P.formatLine("type = {0}, offset = {1}, attrs = {2}", Base.Type, Base.Offset,
               memberAttributes(Base.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               VirtualBaseClassRecord &Base) {
  AutoIndent Indent(P);
  P.formatLine(
      "base = {0}, vbptr = {1}, vbptr offset = {2}, vtable index = {3}",
      Base.BaseType, Base.VBPtrType, Base.VBPtrOffset, Base.VTableIndex);
  P.formatLine("attrs = {0}", memberAttributes(Base.Attrs));
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               ListContinuationRecord &Cont) {
  P.format(" continuation = {0}", Cont.ContinuationIndex);
  return Error::success();
}

Error MinimalTypeDumpVisitor::visitKnownMember(CVMemberRecord &CVR,
                                               VFPtrRecord &VFP) {
  P.format(" type = {0}", VFP.Type);
  return Error::success();
}
