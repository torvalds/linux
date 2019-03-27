//===- TypeRecord.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Endian.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace llvm {
namespace codeview {

using support::little32_t;
using support::ulittle16_t;
using support::ulittle32_t;

using CVType = CVRecord<TypeLeafKind>;
using RemappedType = RemappedRecord<TypeLeafKind>;

struct CVMemberRecord {
  TypeLeafKind Kind;
  ArrayRef<uint8_t> Data;
};
using CVTypeArray = VarStreamArray<CVType>;
using CVTypeRange = iterator_range<CVTypeArray::Iterator>;

/// Equvalent to CV_fldattr_t in cvinfo.h.
struct MemberAttributes {
  uint16_t Attrs = 0;

  enum {
    MethodKindShift = 2,
  };

  MemberAttributes() = default;

  explicit MemberAttributes(MemberAccess Access)
      : Attrs(static_cast<uint16_t>(Access)) {}

  MemberAttributes(MemberAccess Access, MethodKind Kind, MethodOptions Flags) {
    Attrs = static_cast<uint16_t>(Access);
    Attrs |= (static_cast<uint16_t>(Kind) << MethodKindShift);
    Attrs |= static_cast<uint16_t>(Flags);
  }

  /// Get the access specifier. Valid for any kind of member.
  MemberAccess getAccess() const {
    return MemberAccess(unsigned(Attrs) & unsigned(MethodOptions::AccessMask));
  }

  /// Indicates if a method is defined with friend, virtual, static, etc.
  MethodKind getMethodKind() const {
    return MethodKind(
        (unsigned(Attrs) & unsigned(MethodOptions::MethodKindMask)) >>
        MethodKindShift);
  }

  /// Get the flags that are not included in access control or method
  /// properties.
  MethodOptions getFlags() const {
    return MethodOptions(
        unsigned(Attrs) &
        ~unsigned(MethodOptions::AccessMask | MethodOptions::MethodKindMask));
  }

  /// Is this method virtual.
  bool isVirtual() const {
    auto MP = getMethodKind();
    return MP != MethodKind::Vanilla && MP != MethodKind::Friend &&
           MP != MethodKind::Static;
  }

  /// Does this member introduce a new virtual method.
  bool isIntroducedVirtual() const {
    auto MP = getMethodKind();
    return MP == MethodKind::IntroducingVirtual ||
           MP == MethodKind::PureIntroducingVirtual;
  }

  /// Is this method static.
  bool isStatic() const {
    return getMethodKind() == MethodKind::Static;
  }
};

// Does not correspond to any tag, this is the tail of an LF_POINTER record
// if it represents a member pointer.
class MemberPointerInfo {
public:
  MemberPointerInfo() = default;

  MemberPointerInfo(TypeIndex ContainingType,
                    PointerToMemberRepresentation Representation)
      : ContainingType(ContainingType), Representation(Representation) {}

  TypeIndex getContainingType() const { return ContainingType; }
  PointerToMemberRepresentation getRepresentation() const {
    return Representation;
  }

  TypeIndex ContainingType;
  PointerToMemberRepresentation Representation;
};

class TypeRecord {
protected:
  TypeRecord() = default;
  explicit TypeRecord(TypeRecordKind Kind) : Kind(Kind) {}

public:
  TypeRecordKind getKind() const { return Kind; }

  TypeRecordKind Kind;
};

// LF_MODIFIER
class ModifierRecord : public TypeRecord {
public:
  ModifierRecord() = default;
  explicit ModifierRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  ModifierRecord(TypeIndex ModifiedType, ModifierOptions Modifiers)
      : TypeRecord(TypeRecordKind::Modifier), ModifiedType(ModifiedType),
        Modifiers(Modifiers) {}

  TypeIndex getModifiedType() const { return ModifiedType; }
  ModifierOptions getModifiers() const { return Modifiers; }

  TypeIndex ModifiedType;
  ModifierOptions Modifiers;
};

// LF_PROCEDURE
class ProcedureRecord : public TypeRecord {
public:
  ProcedureRecord() = default;
  explicit ProcedureRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  ProcedureRecord(TypeIndex ReturnType, CallingConvention CallConv,
                  FunctionOptions Options, uint16_t ParameterCount,
                  TypeIndex ArgumentList)
      : TypeRecord(TypeRecordKind::Procedure), ReturnType(ReturnType),
        CallConv(CallConv), Options(Options), ParameterCount(ParameterCount),
        ArgumentList(ArgumentList) {}

  TypeIndex getReturnType() const { return ReturnType; }
  CallingConvention getCallConv() const { return CallConv; }
  FunctionOptions getOptions() const { return Options; }
  uint16_t getParameterCount() const { return ParameterCount; }
  TypeIndex getArgumentList() const { return ArgumentList; }

  TypeIndex ReturnType;
  CallingConvention CallConv;
  FunctionOptions Options;
  uint16_t ParameterCount;
  TypeIndex ArgumentList;
};

// LF_MFUNCTION
class MemberFunctionRecord : public TypeRecord {
public:
  MemberFunctionRecord() = default;
  explicit MemberFunctionRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  MemberFunctionRecord(TypeIndex ReturnType, TypeIndex ClassType,
                       TypeIndex ThisType, CallingConvention CallConv,
                       FunctionOptions Options, uint16_t ParameterCount,
                       TypeIndex ArgumentList, int32_t ThisPointerAdjustment)
      : TypeRecord(TypeRecordKind::MemberFunction), ReturnType(ReturnType),
        ClassType(ClassType), ThisType(ThisType), CallConv(CallConv),
        Options(Options), ParameterCount(ParameterCount),
        ArgumentList(ArgumentList),
        ThisPointerAdjustment(ThisPointerAdjustment) {}

  TypeIndex getReturnType() const { return ReturnType; }
  TypeIndex getClassType() const { return ClassType; }
  TypeIndex getThisType() const { return ThisType; }
  CallingConvention getCallConv() const { return CallConv; }
  FunctionOptions getOptions() const { return Options; }
  uint16_t getParameterCount() const { return ParameterCount; }
  TypeIndex getArgumentList() const { return ArgumentList; }
  int32_t getThisPointerAdjustment() const { return ThisPointerAdjustment; }

  TypeIndex ReturnType;
  TypeIndex ClassType;
  TypeIndex ThisType;
  CallingConvention CallConv;
  FunctionOptions Options;
  uint16_t ParameterCount;
  TypeIndex ArgumentList;
  int32_t ThisPointerAdjustment;
};

// LF_LABEL
class LabelRecord : public TypeRecord {
public:
  LabelRecord() = default;
  explicit LabelRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  LabelRecord(LabelType Mode) : TypeRecord(TypeRecordKind::Label), Mode(Mode) {}

  LabelType Mode;
};

// LF_MFUNC_ID
class MemberFuncIdRecord : public TypeRecord {
public:
  MemberFuncIdRecord() = default;
  explicit MemberFuncIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  MemberFuncIdRecord(TypeIndex ClassType, TypeIndex FunctionType,
                         StringRef Name)
      : TypeRecord(TypeRecordKind::MemberFuncId), ClassType(ClassType),
        FunctionType(FunctionType), Name(Name) {}

  TypeIndex getClassType() const { return ClassType; }
  TypeIndex getFunctionType() const { return FunctionType; }
  StringRef getName() const { return Name; }

  TypeIndex ClassType;
  TypeIndex FunctionType;
  StringRef Name;
};

// LF_ARGLIST
class ArgListRecord : public TypeRecord {
public:
  ArgListRecord() = default;
  explicit ArgListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  ArgListRecord(TypeRecordKind Kind, ArrayRef<TypeIndex> Indices)
      : TypeRecord(Kind), ArgIndices(Indices) {}

  ArrayRef<TypeIndex> getIndices() const { return ArgIndices; }

  std::vector<TypeIndex> ArgIndices;
};

// LF_SUBSTR_LIST
class StringListRecord : public TypeRecord {
public:
  StringListRecord() = default;
  explicit StringListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  StringListRecord(TypeRecordKind Kind, ArrayRef<TypeIndex> Indices)
      : TypeRecord(Kind), StringIndices(Indices) {}

  ArrayRef<TypeIndex> getIndices() const { return StringIndices; }

  std::vector<TypeIndex> StringIndices;
};

// LF_POINTER
class PointerRecord : public TypeRecord {
public:
  // ---------------------------XXXXX
  static const uint32_t PointerKindShift = 0;
  static const uint32_t PointerKindMask = 0x1F;

  // ------------------------XXX-----
  static const uint32_t PointerModeShift = 5;
  static const uint32_t PointerModeMask = 0x07;

  // ----------XXX------XXXXX--------
  static const uint32_t PointerOptionMask = 0x381f00;

  // -------------XXXXXX------------
  static const uint32_t PointerSizeShift = 13;
  static const uint32_t PointerSizeMask = 0xFF;

  PointerRecord() = default;
  explicit PointerRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  PointerRecord(TypeIndex ReferentType, uint32_t Attrs)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(Attrs) {}

  PointerRecord(TypeIndex ReferentType, PointerKind PK, PointerMode PM,
                PointerOptions PO, uint8_t Size)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(calcAttrs(PK, PM, PO, Size)) {}

  PointerRecord(TypeIndex ReferentType, PointerKind PK, PointerMode PM,
                PointerOptions PO, uint8_t Size, const MemberPointerInfo &MPI)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(calcAttrs(PK, PM, PO, Size)), MemberInfo(MPI) {}

  TypeIndex getReferentType() const { return ReferentType; }

  PointerKind getPointerKind() const {
    return static_cast<PointerKind>((Attrs >> PointerKindShift) &
                                    PointerKindMask);
  }

  PointerMode getMode() const {
    return static_cast<PointerMode>((Attrs >> PointerModeShift) &
                                    PointerModeMask);
  }

  PointerOptions getOptions() const {
    return static_cast<PointerOptions>(Attrs & PointerOptionMask);
  }

  uint8_t getSize() const {
    return (Attrs >> PointerSizeShift) & PointerSizeMask;
  }

  MemberPointerInfo getMemberInfo() const { return *MemberInfo; }

  bool isPointerToMember() const {
    return getMode() == PointerMode::PointerToDataMember ||
           getMode() == PointerMode::PointerToMemberFunction;
  }

  bool isFlat() const { return !!(Attrs & uint32_t(PointerOptions::Flat32)); }
  bool isConst() const { return !!(Attrs & uint32_t(PointerOptions::Const)); }

  bool isVolatile() const {
    return !!(Attrs & uint32_t(PointerOptions::Volatile));
  }

  bool isUnaligned() const {
    return !!(Attrs & uint32_t(PointerOptions::Unaligned));
  }

  bool isRestrict() const {
    return !!(Attrs & uint32_t(PointerOptions::Restrict));
  }

  bool isLValueReferenceThisPtr() const {
    return !!(Attrs & uint32_t(PointerOptions::LValueRefThisPointer));
  }

  bool isRValueReferenceThisPtr() const {
    return !!(Attrs & uint32_t(PointerOptions::RValueRefThisPointer));
  }

  TypeIndex ReferentType;
  uint32_t Attrs;
  Optional<MemberPointerInfo> MemberInfo;

  void setAttrs(PointerKind PK, PointerMode PM, PointerOptions PO,
                uint8_t Size) {
    Attrs = calcAttrs(PK, PM, PO, Size);
  }

private:
  static uint32_t calcAttrs(PointerKind PK, PointerMode PM, PointerOptions PO,
                            uint8_t Size) {
    uint32_t A = 0;
    A |= static_cast<uint32_t>(PK);
    A |= static_cast<uint32_t>(PO);
    A |= (static_cast<uint32_t>(PM) << PointerModeShift);
    A |= (static_cast<uint32_t>(Size) << PointerSizeShift);
    return A;
  }
};

// LF_NESTTYPE
class NestedTypeRecord : public TypeRecord {
public:
  NestedTypeRecord() = default;
  explicit NestedTypeRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  NestedTypeRecord(TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::NestedType), Type(Type), Name(Name) {}

  TypeIndex getNestedType() const { return Type; }
  StringRef getName() const { return Name; }

  TypeIndex Type;
  StringRef Name;
};

// LF_FIELDLIST
class FieldListRecord : public TypeRecord {
public:
  FieldListRecord() = default;
  explicit FieldListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  explicit FieldListRecord(ArrayRef<uint8_t> Data)
      : TypeRecord(TypeRecordKind::FieldList), Data(Data) {}

  ArrayRef<uint8_t> Data;
};

// LF_ARRAY
class ArrayRecord : public TypeRecord {
public:
  ArrayRecord() = default;
  explicit ArrayRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  ArrayRecord(TypeIndex ElementType, TypeIndex IndexType, uint64_t Size,
              StringRef Name)
      : TypeRecord(TypeRecordKind::Array), ElementType(ElementType),
        IndexType(IndexType), Size(Size), Name(Name) {}

  TypeIndex getElementType() const { return ElementType; }
  TypeIndex getIndexType() const { return IndexType; }
  uint64_t getSize() const { return Size; }
  StringRef getName() const { return Name; }

  TypeIndex ElementType;
  TypeIndex IndexType;
  uint64_t Size;
  StringRef Name;
};

class TagRecord : public TypeRecord {
protected:
  TagRecord() = default;
  explicit TagRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  TagRecord(TypeRecordKind Kind, uint16_t MemberCount, ClassOptions Options,
            TypeIndex FieldList, StringRef Name, StringRef UniqueName)
      : TypeRecord(Kind), MemberCount(MemberCount), Options(Options),
        FieldList(FieldList), Name(Name), UniqueName(UniqueName) {}

public:
  static const int HfaKindShift = 11;
  static const int HfaKindMask = 0x1800;
  static const int WinRTKindShift = 14;
  static const int WinRTKindMask = 0xC000;

  bool hasUniqueName() const {
    return (Options & ClassOptions::HasUniqueName) != ClassOptions::None;
  }

  bool isNested() const {
    return (Options & ClassOptions::Nested) != ClassOptions::None;
  }

  bool isForwardRef() const {
    return (Options & ClassOptions::ForwardReference) != ClassOptions::None;
  }

  bool containsNestedClass() const {
    return (Options & ClassOptions::ContainsNestedClass) != ClassOptions::None;
  }

  bool isScoped() const {
    return (Options & ClassOptions::Scoped) != ClassOptions::None;
  }

  uint16_t getMemberCount() const { return MemberCount; }
  ClassOptions getOptions() const { return Options; }
  TypeIndex getFieldList() const { return FieldList; }
  StringRef getName() const { return Name; }
  StringRef getUniqueName() const { return UniqueName; }

  uint16_t MemberCount;
  ClassOptions Options;
  TypeIndex FieldList;
  StringRef Name;
  StringRef UniqueName;
};

// LF_CLASS, LF_STRUCTURE, LF_INTERFACE
class ClassRecord : public TagRecord {
public:
  ClassRecord() = default;
  explicit ClassRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  ClassRecord(TypeRecordKind Kind, uint16_t MemberCount, ClassOptions Options,
              TypeIndex FieldList, TypeIndex DerivationList,
              TypeIndex VTableShape, uint64_t Size, StringRef Name,
              StringRef UniqueName)
      : TagRecord(Kind, MemberCount, Options, FieldList, Name, UniqueName),
        DerivationList(DerivationList), VTableShape(VTableShape), Size(Size) {}

  HfaKind getHfa() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & HfaKindMask) >> HfaKindShift;
    return static_cast<HfaKind>(Value);
  }

  WindowsRTClassKind getWinRTKind() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & WinRTKindMask) >> WinRTKindShift;
    return static_cast<WindowsRTClassKind>(Value);
  }

  TypeIndex getDerivationList() const { return DerivationList; }
  TypeIndex getVTableShape() const { return VTableShape; }
  uint64_t getSize() const { return Size; }

  TypeIndex DerivationList;
  TypeIndex VTableShape;
  uint64_t Size;
};

// LF_UNION
struct UnionRecord : public TagRecord {
  UnionRecord() = default;
  explicit UnionRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  UnionRecord(uint16_t MemberCount, ClassOptions Options, TypeIndex FieldList,
              uint64_t Size, StringRef Name, StringRef UniqueName)
      : TagRecord(TypeRecordKind::Union, MemberCount, Options, FieldList, Name,
                  UniqueName),
        Size(Size) {}

  HfaKind getHfa() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & HfaKindMask) >> HfaKindShift;
    return static_cast<HfaKind>(Value);
  }

  uint64_t getSize() const { return Size; }

  uint64_t Size;
};

// LF_ENUM
class EnumRecord : public TagRecord {
public:
  EnumRecord() = default;
  explicit EnumRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  EnumRecord(uint16_t MemberCount, ClassOptions Options, TypeIndex FieldList,
             StringRef Name, StringRef UniqueName, TypeIndex UnderlyingType)
      : TagRecord(TypeRecordKind::Enum, MemberCount, Options, FieldList, Name,
                  UniqueName),
        UnderlyingType(UnderlyingType) {}

  TypeIndex getUnderlyingType() const { return UnderlyingType; }

  TypeIndex UnderlyingType;
};

// LF_BITFIELD
class BitFieldRecord : public TypeRecord {
public:
  BitFieldRecord() = default;
  explicit BitFieldRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  BitFieldRecord(TypeIndex Type, uint8_t BitSize, uint8_t BitOffset)
      : TypeRecord(TypeRecordKind::BitField), Type(Type), BitSize(BitSize),
        BitOffset(BitOffset) {}

  TypeIndex getType() const { return Type; }
  uint8_t getBitOffset() const { return BitOffset; }
  uint8_t getBitSize() const { return BitSize; }

  TypeIndex Type;
  uint8_t BitSize;
  uint8_t BitOffset;
};

// LF_VTSHAPE
class VFTableShapeRecord : public TypeRecord {
public:
  VFTableShapeRecord() = default;
  explicit VFTableShapeRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  explicit VFTableShapeRecord(ArrayRef<VFTableSlotKind> Slots)
      : TypeRecord(TypeRecordKind::VFTableShape), SlotsRef(Slots) {}
  explicit VFTableShapeRecord(std::vector<VFTableSlotKind> Slots)
      : TypeRecord(TypeRecordKind::VFTableShape), Slots(std::move(Slots)) {}

  ArrayRef<VFTableSlotKind> getSlots() const {
    if (!SlotsRef.empty())
      return SlotsRef;
    return Slots;
  }

  uint32_t getEntryCount() const { return getSlots().size(); }

  ArrayRef<VFTableSlotKind> SlotsRef;
  std::vector<VFTableSlotKind> Slots;
};

// LF_TYPESERVER2
class TypeServer2Record : public TypeRecord {
public:
  TypeServer2Record() = default;
  explicit TypeServer2Record(TypeRecordKind Kind) : TypeRecord(Kind) {}
  TypeServer2Record(StringRef GuidStr, uint32_t Age, StringRef Name)
      : TypeRecord(TypeRecordKind::TypeServer2), Age(Age), Name(Name) {
    assert(GuidStr.size() == 16 && "guid isn't 16 bytes");
    ::memcpy(Guid.Guid, GuidStr.data(), 16);
  }

  const GUID &getGuid() const { return Guid; }
  uint32_t getAge() const { return Age; }
  StringRef getName() const { return Name; }

  GUID Guid;
  uint32_t Age;
  StringRef Name;
};

// LF_STRING_ID
class StringIdRecord : public TypeRecord {
public:
  StringIdRecord() = default;
  explicit StringIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  StringIdRecord(TypeIndex Id, StringRef String)
      : TypeRecord(TypeRecordKind::StringId), Id(Id), String(String) {}

  TypeIndex getId() const { return Id; }
  StringRef getString() const { return String; }

  TypeIndex Id;
  StringRef String;
};

// LF_FUNC_ID
class FuncIdRecord : public TypeRecord {
public:
  FuncIdRecord() = default;
  explicit FuncIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  FuncIdRecord(TypeIndex ParentScope, TypeIndex FunctionType, StringRef Name)
      : TypeRecord(TypeRecordKind::FuncId), ParentScope(ParentScope),
        FunctionType(FunctionType), Name(Name) {}

  TypeIndex getParentScope() const { return ParentScope; }
  TypeIndex getFunctionType() const { return FunctionType; }
  StringRef getName() const { return Name; }

  TypeIndex ParentScope;
  TypeIndex FunctionType;
  StringRef Name;
};

// LF_UDT_SRC_LINE
class UdtSourceLineRecord : public TypeRecord {
public:
  UdtSourceLineRecord() = default;
  explicit UdtSourceLineRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  UdtSourceLineRecord(TypeIndex UDT, TypeIndex SourceFile, uint32_t LineNumber)
      : TypeRecord(TypeRecordKind::UdtSourceLine), UDT(UDT),
        SourceFile(SourceFile), LineNumber(LineNumber) {}

  TypeIndex getUDT() const { return UDT; }
  TypeIndex getSourceFile() const { return SourceFile; }
  uint32_t getLineNumber() const { return LineNumber; }

  TypeIndex UDT;
  TypeIndex SourceFile;
  uint32_t LineNumber;
};

// LF_UDT_MOD_SRC_LINE
class UdtModSourceLineRecord : public TypeRecord {
public:
  UdtModSourceLineRecord() = default;
  explicit UdtModSourceLineRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  UdtModSourceLineRecord(TypeIndex UDT, TypeIndex SourceFile,
                         uint32_t LineNumber, uint16_t Module)
      : TypeRecord(TypeRecordKind::UdtSourceLine), UDT(UDT),
        SourceFile(SourceFile), LineNumber(LineNumber), Module(Module) {}

  TypeIndex getUDT() const { return UDT; }
  TypeIndex getSourceFile() const { return SourceFile; }
  uint32_t getLineNumber() const { return LineNumber; }
  uint16_t getModule() const { return Module; }

  TypeIndex UDT;
  TypeIndex SourceFile;
  uint32_t LineNumber;
  uint16_t Module;
};

// LF_BUILDINFO
class BuildInfoRecord : public TypeRecord {
public:
  BuildInfoRecord() = default;
  explicit BuildInfoRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  BuildInfoRecord(ArrayRef<TypeIndex> ArgIndices)
      : TypeRecord(TypeRecordKind::BuildInfo),
        ArgIndices(ArgIndices.begin(), ArgIndices.end()) {}

  ArrayRef<TypeIndex> getArgs() const { return ArgIndices; }

  /// Indices of known build info arguments.
  enum BuildInfoArg {
    CurrentDirectory, ///< Absolute CWD path
    BuildTool,        ///< Absolute compiler path
    SourceFile,       ///< Path to main source file, relative or absolute
    TypeServerPDB,    ///< Absolute path of type server PDB (/Fd)
    CommandLine,      ///< Full canonical command line (maybe -cc1)
    MaxArgs
  };

  SmallVector<TypeIndex, MaxArgs> ArgIndices;
};

// LF_VFTABLE
class VFTableRecord : public TypeRecord {
public:
  VFTableRecord() = default;
  explicit VFTableRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  VFTableRecord(TypeIndex CompleteClass, TypeIndex OverriddenVFTable,
                uint32_t VFPtrOffset, StringRef Name,
                ArrayRef<StringRef> Methods)
      : TypeRecord(TypeRecordKind::VFTable), CompleteClass(CompleteClass),
        OverriddenVFTable(OverriddenVFTable), VFPtrOffset(VFPtrOffset) {
    MethodNames.push_back(Name);
    MethodNames.insert(MethodNames.end(), Methods.begin(), Methods.end());
  }

  TypeIndex getCompleteClass() const { return CompleteClass; }
  TypeIndex getOverriddenVTable() const { return OverriddenVFTable; }
  uint32_t getVFPtrOffset() const { return VFPtrOffset; }
  StringRef getName() const { return makeArrayRef(MethodNames).front(); }

  ArrayRef<StringRef> getMethodNames() const {
    return makeArrayRef(MethodNames).drop_front();
  }

  TypeIndex CompleteClass;
  TypeIndex OverriddenVFTable;
  uint32_t VFPtrOffset;
  std::vector<StringRef> MethodNames;
};

// LF_ONEMETHOD
class OneMethodRecord : public TypeRecord {
public:
  OneMethodRecord() = default;
  explicit OneMethodRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  OneMethodRecord(TypeIndex Type, MemberAttributes Attrs, int32_t VFTableOffset,
                  StringRef Name)
      : TypeRecord(TypeRecordKind::OneMethod), Type(Type), Attrs(Attrs),
        VFTableOffset(VFTableOffset), Name(Name) {}
  OneMethodRecord(TypeIndex Type, MemberAccess Access, MethodKind MK,
                  MethodOptions Options, int32_t VFTableOffset, StringRef Name)
      : TypeRecord(TypeRecordKind::OneMethod), Type(Type),
        Attrs(Access, MK, Options), VFTableOffset(VFTableOffset), Name(Name) {}

  TypeIndex getType() const { return Type; }
  MethodKind getMethodKind() const { return Attrs.getMethodKind(); }
  MethodOptions getOptions() const { return Attrs.getFlags(); }
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  int32_t getVFTableOffset() const { return VFTableOffset; }
  StringRef getName() const { return Name; }

  bool isIntroducingVirtual() const {
    return getMethodKind() == MethodKind::IntroducingVirtual ||
           getMethodKind() == MethodKind::PureIntroducingVirtual;
  }

  TypeIndex Type;
  MemberAttributes Attrs;
  int32_t VFTableOffset;
  StringRef Name;
};

// LF_METHODLIST
class MethodOverloadListRecord : public TypeRecord {
public:
  MethodOverloadListRecord() = default;
  explicit MethodOverloadListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  MethodOverloadListRecord(ArrayRef<OneMethodRecord> Methods)
      : TypeRecord(TypeRecordKind::MethodOverloadList), Methods(Methods) {}

  ArrayRef<OneMethodRecord> getMethods() const { return Methods; }

  std::vector<OneMethodRecord> Methods;
};

/// For method overload sets.  LF_METHOD
class OverloadedMethodRecord : public TypeRecord {
public:
  OverloadedMethodRecord() = default;
  explicit OverloadedMethodRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  OverloadedMethodRecord(uint16_t NumOverloads, TypeIndex MethodList,
                         StringRef Name)
      : TypeRecord(TypeRecordKind::OverloadedMethod),
        NumOverloads(NumOverloads), MethodList(MethodList), Name(Name) {}

  uint16_t getNumOverloads() const { return NumOverloads; }
  TypeIndex getMethodList() const { return MethodList; }
  StringRef getName() const { return Name; }

  uint16_t NumOverloads;
  TypeIndex MethodList;
  StringRef Name;
};

// LF_MEMBER
class DataMemberRecord : public TypeRecord {
public:
  DataMemberRecord() = default;
  explicit DataMemberRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  DataMemberRecord(MemberAttributes Attrs, TypeIndex Type, uint64_t Offset,
                   StringRef Name)
      : TypeRecord(TypeRecordKind::DataMember), Attrs(Attrs), Type(Type),
        FieldOffset(Offset), Name(Name) {}
  DataMemberRecord(MemberAccess Access, TypeIndex Type, uint64_t Offset,
                   StringRef Name)
      : TypeRecord(TypeRecordKind::DataMember), Attrs(Access), Type(Type),
        FieldOffset(Offset), Name(Name) {}

  MemberAccess getAccess() const { return Attrs.getAccess(); }
  TypeIndex getType() const { return Type; }
  uint64_t getFieldOffset() const { return FieldOffset; }
  StringRef getName() const { return Name; }

  MemberAttributes Attrs;
  TypeIndex Type;
  uint64_t FieldOffset;
  StringRef Name;
};

// LF_STMEMBER
class StaticDataMemberRecord : public TypeRecord {
public:
  StaticDataMemberRecord() = default;
  explicit StaticDataMemberRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  StaticDataMemberRecord(MemberAttributes Attrs, TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::StaticDataMember), Attrs(Attrs), Type(Type),
        Name(Name) {}
  StaticDataMemberRecord(MemberAccess Access, TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::StaticDataMember), Attrs(Access), Type(Type),
        Name(Name) {}

  MemberAccess getAccess() const { return Attrs.getAccess(); }
  TypeIndex getType() const { return Type; }
  StringRef getName() const { return Name; }

  MemberAttributes Attrs;
  TypeIndex Type;
  StringRef Name;
};

// LF_ENUMERATE
class EnumeratorRecord : public TypeRecord {
public:
  EnumeratorRecord() = default;
  explicit EnumeratorRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  EnumeratorRecord(MemberAttributes Attrs, APSInt Value, StringRef Name)
      : TypeRecord(TypeRecordKind::Enumerator), Attrs(Attrs),
        Value(std::move(Value)), Name(Name) {}
  EnumeratorRecord(MemberAccess Access, APSInt Value, StringRef Name)
      : TypeRecord(TypeRecordKind::Enumerator), Attrs(Access),
        Value(std::move(Value)), Name(Name) {}

  MemberAccess getAccess() const { return Attrs.getAccess(); }
  APSInt getValue() const { return Value; }
  StringRef getName() const { return Name; }

  MemberAttributes Attrs;
  APSInt Value;
  StringRef Name;
};

// LF_VFUNCTAB
class VFPtrRecord : public TypeRecord {
public:
  VFPtrRecord() = default;
  explicit VFPtrRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  VFPtrRecord(TypeIndex Type)
      : TypeRecord(TypeRecordKind::VFPtr), Type(Type) {}

  TypeIndex getType() const { return Type; }

  TypeIndex Type;
};

// LF_BCLASS, LF_BINTERFACE
class BaseClassRecord : public TypeRecord {
public:
  BaseClassRecord() = default;
  explicit BaseClassRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  BaseClassRecord(MemberAttributes Attrs, TypeIndex Type, uint64_t Offset)
      : TypeRecord(TypeRecordKind::BaseClass), Attrs(Attrs), Type(Type),
        Offset(Offset) {}
  BaseClassRecord(MemberAccess Access, TypeIndex Type, uint64_t Offset)
      : TypeRecord(TypeRecordKind::BaseClass), Attrs(Access), Type(Type),
        Offset(Offset) {}

  MemberAccess getAccess() const { return Attrs.getAccess(); }
  TypeIndex getBaseType() const { return Type; }
  uint64_t getBaseOffset() const { return Offset; }

  MemberAttributes Attrs;
  TypeIndex Type;
  uint64_t Offset;
};

// LF_VBCLASS, LF_IVBCLASS
class VirtualBaseClassRecord : public TypeRecord {
public:
  VirtualBaseClassRecord() = default;
  explicit VirtualBaseClassRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  VirtualBaseClassRecord(TypeRecordKind Kind, MemberAttributes Attrs,
                         TypeIndex BaseType, TypeIndex VBPtrType,
                         uint64_t Offset, uint64_t Index)
      : TypeRecord(Kind), Attrs(Attrs), BaseType(BaseType),
        VBPtrType(VBPtrType), VBPtrOffset(Offset), VTableIndex(Index) {}
  VirtualBaseClassRecord(TypeRecordKind Kind, MemberAccess Access,
                         TypeIndex BaseType, TypeIndex VBPtrType,
                         uint64_t Offset, uint64_t Index)
      : TypeRecord(Kind), Attrs(Access), BaseType(BaseType),
        VBPtrType(VBPtrType), VBPtrOffset(Offset), VTableIndex(Index) {}

  MemberAccess getAccess() const { return Attrs.getAccess(); }
  TypeIndex getBaseType() const { return BaseType; }
  TypeIndex getVBPtrType() const { return VBPtrType; }
  uint64_t getVBPtrOffset() const { return VBPtrOffset; }
  uint64_t getVTableIndex() const { return VTableIndex; }

  MemberAttributes Attrs;
  TypeIndex BaseType;
  TypeIndex VBPtrType;
  uint64_t VBPtrOffset;
  uint64_t VTableIndex;
};

/// LF_INDEX - Used to chain two large LF_FIELDLIST or LF_METHODLIST records
/// together. The first will end in an LF_INDEX record that points to the next.
class ListContinuationRecord : public TypeRecord {
public:
  ListContinuationRecord() = default;
  explicit ListContinuationRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  ListContinuationRecord(TypeIndex ContinuationIndex)
      : TypeRecord(TypeRecordKind::ListContinuation),
        ContinuationIndex(ContinuationIndex) {}

  TypeIndex getContinuationIndex() const { return ContinuationIndex; }

  TypeIndex ContinuationIndex;
};

// LF_PRECOMP
class PrecompRecord : public TypeRecord {
public:
  PrecompRecord() = default;
  explicit PrecompRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  uint32_t getStartTypeIndex() const { return StartTypeIndex; }
  uint32_t getTypesCount() const { return TypesCount; }
  uint32_t getSignature() const { return Signature; }
  StringRef getPrecompFilePath() const { return PrecompFilePath; }

  uint32_t StartTypeIndex;
  uint32_t TypesCount;
  uint32_t Signature;
  StringRef PrecompFilePath;
};

// LF_ENDPRECOMP
class EndPrecompRecord : public TypeRecord {
public:
  EndPrecompRecord() = default;
  explicit EndPrecompRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  uint32_t getSignature() const { return Signature; }

  uint32_t Signature;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H
