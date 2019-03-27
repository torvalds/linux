//===- TypeIndexDiscovery.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::codeview;

static inline MethodKind getMethodKind(uint16_t Attrs) {
  Attrs &= uint16_t(MethodOptions::MethodKindMask);
  Attrs >>= 2;
  return MethodKind(Attrs);
}

static inline bool isIntroVirtual(uint16_t Attrs) {
  MethodKind MK = getMethodKind(Attrs);
  return MK == MethodKind::IntroducingVirtual ||
         MK == MethodKind::PureIntroducingVirtual;
}

static inline PointerMode getPointerMode(uint32_t Attrs) {
  return static_cast<PointerMode>((Attrs >> PointerRecord::PointerModeShift) &
                                  PointerRecord::PointerModeMask);
}

static inline bool isMemberPointer(uint32_t Attrs) {
  PointerMode Mode = getPointerMode(Attrs);
  return Mode == PointerMode::PointerToDataMember ||
         Mode == PointerMode::PointerToMemberFunction;
}

static inline uint32_t getEncodedIntegerLength(ArrayRef<uint8_t> Data) {
  uint16_t N = support::endian::read16le(Data.data());
  if (N < LF_NUMERIC)
    return 2;

  assert(N <= LF_UQUADWORD);

  constexpr uint32_t Sizes[] = {
      1,  // LF_CHAR
      2,  // LF_SHORT
      2,  // LF_USHORT
      4,  // LF_LONG
      4,  // LF_ULONG
      4,  // LF_REAL32
      8,  // LF_REAL64
      10, // LF_REAL80
      16, // LF_REAL128
      8,  // LF_QUADWORD
      8,  // LF_UQUADWORD
  };

  return 2 + Sizes[N - LF_NUMERIC];
}

static inline uint32_t getCStringLength(ArrayRef<uint8_t> Data) {
  const char *S = reinterpret_cast<const char *>(Data.data());
  return strlen(S) + 1;
}

static void handleMethodOverloadList(ArrayRef<uint8_t> Content,
                                     SmallVectorImpl<TiReference> &Refs) {
  uint32_t Offset = 0;

  while (!Content.empty()) {
    // Array of:
    //   0: Attrs
    //   2: Padding
    //   4: TypeIndex
    //   if (isIntroVirtual())
    //     8: VFTableOffset

    // At least 8 bytes are guaranteed.  4 extra bytes come iff function is an
    // intro virtual.
    uint32_t Len = 8;

    uint16_t Attrs = support::endian::read16le(Content.data());
    Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});

    if (LLVM_UNLIKELY(isIntroVirtual(Attrs)))
      Len += 4;
    Offset += Len;
    Content = Content.drop_front(Len);
  }
}

static uint32_t handleBaseClass(ArrayRef<uint8_t> Data, uint32_t Offset,
                                SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  // 8: Encoded Integer
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8 + getEncodedIntegerLength(Data.drop_front(8));
}

static uint32_t handleEnumerator(ArrayRef<uint8_t> Data, uint32_t Offset,
                                 SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: Encoded Integer
  // <next>: Name
  uint32_t Size = 4 + getEncodedIntegerLength(Data.drop_front(4));
  return Size + getCStringLength(Data.drop_front(Size));
}

static uint32_t handleDataMember(ArrayRef<uint8_t> Data, uint32_t Offset,
                                 SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  // 8: Encoded Integer
  // <next>: Name
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  uint32_t Size = 8 + getEncodedIntegerLength(Data.drop_front(8));
  return Size + getCStringLength(Data.drop_front(Size));
}

static uint32_t handleOverloadedMethod(ArrayRef<uint8_t> Data, uint32_t Offset,
                                       SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  // 8: Name
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8 + getCStringLength(Data.drop_front(8));
}

static uint32_t handleOneMethod(ArrayRef<uint8_t> Data, uint32_t Offset,
                                SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Attributes
  // 4: Type
  // if (isIntroVirtual)
  //   8: VFTableOffset
  // <next>: Name
  uint32_t Size = 8;
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});

  uint16_t Attrs = support::endian::read16le(Data.drop_front(2).data());
  if (LLVM_UNLIKELY(isIntroVirtual(Attrs)))
    Size += 4;

  return Size + getCStringLength(Data.drop_front(Size));
}

static uint32_t handleNestedType(ArrayRef<uint8_t> Data, uint32_t Offset,
                                 SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  // 8: Name
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8 + getCStringLength(Data.drop_front(8));
}

static uint32_t handleStaticDataMember(ArrayRef<uint8_t> Data, uint32_t Offset,
                                       SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  // 8: Name
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8 + getCStringLength(Data.drop_front(8));
}

static uint32_t handleVirtualBaseClass(ArrayRef<uint8_t> Data, uint32_t Offset,
                                       bool IsIndirect,
                                       SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Attrs
  // 4: TypeIndex
  // 8: TypeIndex
  // 12: Encoded Integer
  // <next>: Encoded Integer
  uint32_t Size = 12;
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 2});
  Size += getEncodedIntegerLength(Data.drop_front(Size));
  Size += getEncodedIntegerLength(Data.drop_front(Size));
  return Size;
}

static uint32_t handleVFPtr(ArrayRef<uint8_t> Data, uint32_t Offset,
                            SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8;
}

static uint32_t handleListContinuation(ArrayRef<uint8_t> Data, uint32_t Offset,
                                       SmallVectorImpl<TiReference> &Refs) {
  // 0: Kind
  // 2: Padding
  // 4: TypeIndex
  Refs.push_back({TiRefKind::TypeRef, Offset + 4, 1});
  return 8;
}

static void handleFieldList(ArrayRef<uint8_t> Content,
                            SmallVectorImpl<TiReference> &Refs) {
  uint32_t Offset = 0;
  uint32_t ThisLen = 0;
  while (!Content.empty()) {
    TypeLeafKind Kind =
        static_cast<TypeLeafKind>(support::endian::read16le(Content.data()));
    switch (Kind) {
    case LF_BCLASS:
      ThisLen = handleBaseClass(Content, Offset, Refs);
      break;
    case LF_ENUMERATE:
      ThisLen = handleEnumerator(Content, Offset, Refs);
      break;
    case LF_MEMBER:
      ThisLen = handleDataMember(Content, Offset, Refs);
      break;
    case LF_METHOD:
      ThisLen = handleOverloadedMethod(Content, Offset, Refs);
      break;
    case LF_ONEMETHOD:
      ThisLen = handleOneMethod(Content, Offset, Refs);
      break;
    case LF_NESTTYPE:
      ThisLen = handleNestedType(Content, Offset, Refs);
      break;
    case LF_STMEMBER:
      ThisLen = handleStaticDataMember(Content, Offset, Refs);
      break;
    case LF_VBCLASS:
    case LF_IVBCLASS:
      ThisLen =
          handleVirtualBaseClass(Content, Offset, Kind == LF_VBCLASS, Refs);
      break;
    case LF_VFUNCTAB:
      ThisLen = handleVFPtr(Content, Offset, Refs);
      break;
    case LF_INDEX:
      ThisLen = handleListContinuation(Content, Offset, Refs);
      break;
    default:
      return;
    }
    Content = Content.drop_front(ThisLen);
    Offset += ThisLen;
    if (!Content.empty()) {
      uint8_t Pad = Content.front();
      if (Pad >= LF_PAD0) {
        uint32_t Skip = Pad & 0x0F;
        Content = Content.drop_front(Skip);
        Offset += Skip;
      }
    }
  }
}

static void handlePointer(ArrayRef<uint8_t> Content,
                          SmallVectorImpl<TiReference> &Refs) {
  Refs.push_back({TiRefKind::TypeRef, 0, 1});

  uint32_t Attrs = support::endian::read32le(Content.drop_front(4).data());
  if (isMemberPointer(Attrs))
    Refs.push_back({TiRefKind::TypeRef, 8, 1});
}

static void discoverTypeIndices(ArrayRef<uint8_t> Content, TypeLeafKind Kind,
                                SmallVectorImpl<TiReference> &Refs) {
  uint32_t Count;
  // FIXME: In the future it would be nice if we could avoid hardcoding these
  // values.  One idea is to define some structures representing these types
  // that would allow the use of offsetof().
  switch (Kind) {
  case TypeLeafKind::LF_FUNC_ID:
    Refs.push_back({TiRefKind::IndexRef, 0, 1});
    Refs.push_back({TiRefKind::TypeRef, 4, 1});
    break;
  case TypeLeafKind::LF_MFUNC_ID:
    Refs.push_back({TiRefKind::TypeRef, 0, 2});
    break;
  case TypeLeafKind::LF_STRING_ID:
    Refs.push_back({TiRefKind::IndexRef, 0, 1});
    break;
  case TypeLeafKind::LF_SUBSTR_LIST:
    Count = support::endian::read32le(Content.data());
    if (Count > 0)
      Refs.push_back({TiRefKind::IndexRef, 4, Count});
    break;
  case TypeLeafKind::LF_BUILDINFO:
    Count = support::endian::read16le(Content.data());
    if (Count > 0)
      Refs.push_back({TiRefKind::IndexRef, 2, Count});
    break;
  case TypeLeafKind::LF_UDT_SRC_LINE:
    Refs.push_back({TiRefKind::TypeRef, 0, 1});
    Refs.push_back({TiRefKind::IndexRef, 4, 1});
    break;
  case TypeLeafKind::LF_UDT_MOD_SRC_LINE:
    Refs.push_back({TiRefKind::TypeRef, 0, 1});
    break;
  case TypeLeafKind::LF_MODIFIER:
    Refs.push_back({TiRefKind::TypeRef, 0, 1});
    break;
  case TypeLeafKind::LF_PROCEDURE:
    Refs.push_back({TiRefKind::TypeRef, 0, 1});
    Refs.push_back({TiRefKind::TypeRef, 8, 1});
    break;
  case TypeLeafKind::LF_MFUNCTION:
    Refs.push_back({TiRefKind::TypeRef, 0, 3});
    Refs.push_back({TiRefKind::TypeRef, 16, 1});
    break;
  case TypeLeafKind::LF_ARGLIST:
    Count = support::endian::read32le(Content.data());
    if (Count > 0)
      Refs.push_back({TiRefKind::TypeRef, 4, Count});
    break;
  case TypeLeafKind::LF_ARRAY:
    Refs.push_back({TiRefKind::TypeRef, 0, 2});
    break;
  case TypeLeafKind::LF_CLASS:
  case TypeLeafKind::LF_STRUCTURE:
  case TypeLeafKind::LF_INTERFACE:
    Refs.push_back({TiRefKind::TypeRef, 4, 3});
    break;
  case TypeLeafKind::LF_UNION:
    Refs.push_back({TiRefKind::TypeRef, 4, 1});
    break;
  case TypeLeafKind::LF_ENUM:
    Refs.push_back({TiRefKind::TypeRef, 4, 2});
    break;
  case TypeLeafKind::LF_BITFIELD:
    Refs.push_back({TiRefKind::TypeRef, 0, 1});
    break;
  case TypeLeafKind::LF_VFTABLE:
    Refs.push_back({TiRefKind::TypeRef, 0, 2});
    break;
  case TypeLeafKind::LF_VTSHAPE:
    break;
  case TypeLeafKind::LF_METHODLIST:
    handleMethodOverloadList(Content, Refs);
    break;
  case TypeLeafKind::LF_FIELDLIST:
    handleFieldList(Content, Refs);
    break;
  case TypeLeafKind::LF_POINTER:
    handlePointer(Content, Refs);
    break;
  default:
    break;
  }
}

static bool discoverTypeIndices(ArrayRef<uint8_t> Content, SymbolKind Kind,
                                SmallVectorImpl<TiReference> &Refs) {
  uint32_t Count;
  // FIXME: In the future it would be nice if we could avoid hardcoding these
  // values.  One idea is to define some structures representing these types
  // that would allow the use of offsetof().
  switch (Kind) {
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_LPROC32_DPC:
  case SymbolKind::S_LPROC32_DPC_ID:
    Refs.push_back({TiRefKind::IndexRef, 24, 1}); // LF_FUNC_ID
    break;
  case SymbolKind::S_UDT:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // UDT
    break;
  case SymbolKind::S_GDATA32:
  case SymbolKind::S_LDATA32:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_BUILDINFO:
    Refs.push_back({TiRefKind::IndexRef, 0, 1}); // Compile flags
    break;
  case SymbolKind::S_LTHREAD32:
  case SymbolKind::S_GTHREAD32:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_FILESTATIC:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_LOCAL:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_REGISTER:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_CONSTANT:
    Refs.push_back({TiRefKind::TypeRef, 0, 1}); // Type
    break;
  case SymbolKind::S_BPREL32:
  case SymbolKind::S_REGREL32:
    Refs.push_back({TiRefKind::TypeRef, 4, 1}); // Type
    break;
  case SymbolKind::S_CALLSITEINFO:
    Refs.push_back({TiRefKind::TypeRef, 8, 1}); // Call signature
    break;
  case SymbolKind::S_CALLERS:
  case SymbolKind::S_CALLEES:
  case SymbolKind::S_INLINEES:
    // The record is a count followed by an array of type indices.
    Count = *reinterpret_cast<const ulittle32_t *>(Content.data());
    Refs.push_back({TiRefKind::IndexRef, 4, Count}); // Callees
    break;
  case SymbolKind::S_INLINESITE:
    Refs.push_back({TiRefKind::IndexRef, 8, 1}); // ID of inlinee
    break;
  case SymbolKind::S_HEAPALLOCSITE:
    Refs.push_back({TiRefKind::TypeRef, 8, 1}); // UDT allocated
    break;

  // Defranges don't have types, just registers and code offsets.
  case SymbolKind::S_DEFRANGE_REGISTER:
  case SymbolKind::S_DEFRANGE_REGISTER_REL:
  case SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL:
  case SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
  case SymbolKind::S_DEFRANGE_SUBFIELD_REGISTER:
  case SymbolKind::S_DEFRANGE_SUBFIELD:
    break;

  // No type references.
  case SymbolKind::S_LABEL32:
  case SymbolKind::S_OBJNAME:
  case SymbolKind::S_COMPILE:
  case SymbolKind::S_COMPILE2:
  case SymbolKind::S_COMPILE3:
  case SymbolKind::S_ENVBLOCK:
  case SymbolKind::S_BLOCK32:
  case SymbolKind::S_FRAMEPROC:
  case SymbolKind::S_THUNK32:
  case SymbolKind::S_FRAMECOOKIE:
  case SymbolKind::S_UNAMESPACE:
    break;
  // Scope ending symbols.
  case SymbolKind::S_END:
  case SymbolKind::S_INLINESITE_END:
  case SymbolKind::S_PROC_ID_END:
    break;
  default:
    return false; // Unknown symbol.
  }
  return true;
}

void llvm::codeview::discoverTypeIndices(const CVType &Type,
                                         SmallVectorImpl<TiReference> &Refs) {
  ::discoverTypeIndices(Type.content(), Type.kind(), Refs);
}

static void resolveTypeIndexReferences(ArrayRef<uint8_t> RecordData,
                                       ArrayRef<TiReference> Refs,
                                       SmallVectorImpl<TypeIndex> &Indices) {
  Indices.clear();

  if (Refs.empty())
    return;

  RecordData = RecordData.drop_front(sizeof(RecordPrefix));

  BinaryStreamReader Reader(RecordData, support::little);
  for (const auto &Ref : Refs) {
    Reader.setOffset(Ref.Offset);
    FixedStreamArray<TypeIndex> Run;
    cantFail(Reader.readArray(Run, Ref.Count));
    Indices.append(Run.begin(), Run.end());
  }
}

void llvm::codeview::discoverTypeIndices(const CVType &Type,
                                         SmallVectorImpl<TypeIndex> &Indices) {
  return discoverTypeIndices(Type.RecordData, Indices);
}

void llvm::codeview::discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                                         SmallVectorImpl<TypeIndex> &Indices) {
  SmallVector<TiReference, 4> Refs;
  discoverTypeIndices(RecordData, Refs);
  resolveTypeIndexReferences(RecordData, Refs, Indices);
}

void llvm::codeview::discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                                         SmallVectorImpl<TiReference> &Refs) {
  const RecordPrefix *P =
      reinterpret_cast<const RecordPrefix *>(RecordData.data());
  TypeLeafKind K = static_cast<TypeLeafKind>(uint16_t(P->RecordKind));
  ::discoverTypeIndices(RecordData.drop_front(sizeof(RecordPrefix)), K, Refs);
}

bool llvm::codeview::discoverTypeIndicesInSymbol(
    const CVSymbol &Sym, SmallVectorImpl<TiReference> &Refs) {
  SymbolKind K = Sym.kind();
  return ::discoverTypeIndices(Sym.content(), K, Refs);
}

bool llvm::codeview::discoverTypeIndicesInSymbol(
    ArrayRef<uint8_t> RecordData, SmallVectorImpl<TiReference> &Refs) {
  const RecordPrefix *P =
      reinterpret_cast<const RecordPrefix *>(RecordData.data());
  SymbolKind K = static_cast<SymbolKind>(uint16_t(P->RecordKind));
  return ::discoverTypeIndices(RecordData.drop_front(sizeof(RecordPrefix)), K,
                               Refs);
}

bool llvm::codeview::discoverTypeIndicesInSymbol(
    ArrayRef<uint8_t> RecordData, SmallVectorImpl<TypeIndex> &Indices) {
  SmallVector<TiReference, 2> Refs;
  if (!discoverTypeIndicesInSymbol(RecordData, Refs))
    return false;
  resolveTypeIndexReferences(RecordData, Refs, Indices);
  return true;
}
