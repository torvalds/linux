#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"

#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeCompilandSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumGlobals.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeArray.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypePointer.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeTypedef.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeUDT.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeVTShape.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

// Maps codeview::SimpleTypeKind of a built-in type to the parameters necessary
// to instantiate a NativeBuiltinSymbol for that type.
static const struct BuiltinTypeEntry {
  codeview::SimpleTypeKind Kind;
  PDB_BuiltinType Type;
  uint32_t Size;
} BuiltinTypes[] = {
    {codeview::SimpleTypeKind::None, PDB_BuiltinType::None, 0},
    {codeview::SimpleTypeKind::Void, PDB_BuiltinType::Void, 0},
    {codeview::SimpleTypeKind::HResult, PDB_BuiltinType::HResult, 4},
    {codeview::SimpleTypeKind::Int16Short, PDB_BuiltinType::Int, 2},
    {codeview::SimpleTypeKind::UInt16Short, PDB_BuiltinType::UInt, 2},
    {codeview::SimpleTypeKind::Int32, PDB_BuiltinType::Int, 4},
    {codeview::SimpleTypeKind::UInt32, PDB_BuiltinType::UInt, 4},
    {codeview::SimpleTypeKind::Int32Long, PDB_BuiltinType::Int, 4},
    {codeview::SimpleTypeKind::UInt32Long, PDB_BuiltinType::UInt, 4},
    {codeview::SimpleTypeKind::Int64Quad, PDB_BuiltinType::Int, 8},
    {codeview::SimpleTypeKind::UInt64Quad, PDB_BuiltinType::UInt, 8},
    {codeview::SimpleTypeKind::NarrowCharacter, PDB_BuiltinType::Char, 1},
    {codeview::SimpleTypeKind::WideCharacter, PDB_BuiltinType::WCharT, 2},
    {codeview::SimpleTypeKind::Character16, PDB_BuiltinType::Char16, 2},
    {codeview::SimpleTypeKind::Character32, PDB_BuiltinType::Char32, 4},
    {codeview::SimpleTypeKind::SignedCharacter, PDB_BuiltinType::Char, 1},
    {codeview::SimpleTypeKind::UnsignedCharacter, PDB_BuiltinType::UInt, 1},
    {codeview::SimpleTypeKind::Float32, PDB_BuiltinType::Float, 4},
    {codeview::SimpleTypeKind::Float64, PDB_BuiltinType::Float, 8},
    {codeview::SimpleTypeKind::Float80, PDB_BuiltinType::Float, 10},
    {codeview::SimpleTypeKind::Boolean8, PDB_BuiltinType::Bool, 1},
    // This table can be grown as necessary, but these are the only types we've
    // needed so far.
};

SymbolCache::SymbolCache(NativeSession &Session, DbiStream *Dbi)
    : Session(Session), Dbi(Dbi) {
  // Id 0 is reserved for the invalid symbol.
  Cache.push_back(nullptr);

  if (Dbi)
    Compilands.resize(Dbi->modules().getModuleCount());
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createTypeEnumerator(TypeLeafKind Kind) {
  return createTypeEnumerator(std::vector<TypeLeafKind>{Kind});
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createTypeEnumerator(std::vector<TypeLeafKind> Kinds) {
  auto Tpi = Session.getPDBFile().getPDBTpiStream();
  if (!Tpi) {
    consumeError(Tpi.takeError());
    return nullptr;
  }
  auto &Types = Tpi->typeCollection();
  return std::unique_ptr<IPDBEnumSymbols>(
      new NativeEnumTypes(Session, Types, std::move(Kinds)));
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createGlobalsEnumerator(codeview::SymbolKind Kind) {
  return std::unique_ptr<IPDBEnumSymbols>(
      new NativeEnumGlobals(Session, {Kind}));
}

SymIndexId SymbolCache::createSimpleType(TypeIndex Index,
                                         ModifierOptions Mods) {
  if (Index.getSimpleMode() != codeview::SimpleTypeMode::Direct)
    return createSymbol<NativeTypePointer>(Index);

  const auto Kind = Index.getSimpleKind();
  const auto It = std::find_if(
      std::begin(BuiltinTypes), std::end(BuiltinTypes),
      [Kind](const BuiltinTypeEntry &Builtin) { return Builtin.Kind == Kind; });
  if (It == std::end(BuiltinTypes))
    return 0;
  return createSymbol<NativeTypeBuiltin>(Mods, It->Type, It->Size);
}

SymIndexId
SymbolCache::createSymbolForModifiedType(codeview::TypeIndex ModifierTI,
                                         codeview::CVType CVT) {
  ModifierRecord Record;
  if (auto EC = TypeDeserializer::deserializeAs<ModifierRecord>(CVT, Record)) {
    consumeError(std::move(EC));
    return 0;
  }

  if (Record.ModifiedType.isSimple())
    return createSimpleType(Record.ModifiedType, Record.Modifiers);

  // Make sure we create and cache a record for the unmodified type.
  SymIndexId UnmodifiedId = findSymbolByTypeIndex(Record.ModifiedType);
  NativeRawSymbol &UnmodifiedNRS = *Cache[UnmodifiedId];

  switch (UnmodifiedNRS.getSymTag()) {
  case PDB_SymType::Enum:
    return createSymbol<NativeTypeEnum>(
        static_cast<NativeTypeEnum &>(UnmodifiedNRS), std::move(Record));
  case PDB_SymType::UDT:
    return createSymbol<NativeTypeUDT>(
        static_cast<NativeTypeUDT &>(UnmodifiedNRS), std::move(Record));
  default:
    // No other types can be modified.  (LF_POINTER, for example, records
    // its modifiers a different way.
    assert(false && "Invalid LF_MODIFIER record");
    break;
  }
  return 0;
}

SymIndexId SymbolCache::findSymbolByTypeIndex(codeview::TypeIndex Index) {
  // First see if it's already in our cache.
  const auto Entry = TypeIndexToSymbolId.find(Index);
  if (Entry != TypeIndexToSymbolId.end())
    return Entry->second;

  // Symbols for built-in types are created on the fly.
  if (Index.isSimple()) {
    SymIndexId Result = createSimpleType(Index, ModifierOptions::None);
    assert(TypeIndexToSymbolId.count(Index) == 0);
    TypeIndexToSymbolId[Index] = Result;
    return Result;
  }

  // We need to instantiate and cache the desired type symbol.
  auto Tpi = Session.getPDBFile().getPDBTpiStream();
  if (!Tpi) {
    consumeError(Tpi.takeError());
    return 0;
  }
  codeview::LazyRandomTypeCollection &Types = Tpi->typeCollection();
  codeview::CVType CVT = Types.getType(Index);

  if (isUdtForwardRef(CVT)) {
    Expected<TypeIndex> EFD = Tpi->findFullDeclForForwardRef(Index);

    if (!EFD)
      consumeError(EFD.takeError());
    else if (*EFD != Index) {
      assert(!isUdtForwardRef(Types.getType(*EFD)));
      SymIndexId Result = findSymbolByTypeIndex(*EFD);
      // Record a mapping from ForwardRef -> SymIndex of complete type so that
      // we'll take the fast path next time.
      assert(TypeIndexToSymbolId.count(Index) == 0);
      TypeIndexToSymbolId[Index] = Result;
      return Result;
    }
  }

  // At this point if we still have a forward ref udt it means the full decl was
  // not in the PDB.  We just have to deal with it and use the forward ref.
  SymIndexId Id = 0;
  switch (CVT.kind()) {
  case codeview::LF_ENUM:
    Id = createSymbolForType<NativeTypeEnum, EnumRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_ARRAY:
    Id = createSymbolForType<NativeTypeArray, ArrayRecord>(Index,
                                                           std::move(CVT));
    break;
  case codeview::LF_CLASS:
  case codeview::LF_STRUCTURE:
  case codeview::LF_INTERFACE:
    Id = createSymbolForType<NativeTypeUDT, ClassRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_UNION:
    Id = createSymbolForType<NativeTypeUDT, UnionRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_POINTER:
    Id = createSymbolForType<NativeTypePointer, PointerRecord>(Index,
                                                               std::move(CVT));
    break;
  case codeview::LF_MODIFIER:
    Id = createSymbolForModifiedType(Index, std::move(CVT));
    break;
  case codeview::LF_PROCEDURE:
    Id = createSymbolForType<NativeTypeFunctionSig, ProcedureRecord>(
        Index, std::move(CVT));
    break;
  case codeview::LF_MFUNCTION:
    Id = createSymbolForType<NativeTypeFunctionSig, MemberFunctionRecord>(
        Index, std::move(CVT));
    break;
  case codeview::LF_VTSHAPE:
    Id = createSymbolForType<NativeTypeVTShape, VFTableShapeRecord>(
        Index, std::move(CVT));
    break;
  default:
    Id = createSymbolPlaceholder();
    break;
  }
  if (Id != 0) {
    assert(TypeIndexToSymbolId.count(Index) == 0);
    TypeIndexToSymbolId[Index] = Id;
  }
  return Id;
}

std::unique_ptr<PDBSymbol>
SymbolCache::getSymbolById(SymIndexId SymbolId) const {
  assert(SymbolId < Cache.size());

  // Id 0 is reserved.
  if (SymbolId == 0 || SymbolId >= Cache.size())
    return nullptr;

  // Make sure to handle the case where we've inserted a placeholder symbol
  // for types we don't yet suppport.
  NativeRawSymbol *NRS = Cache[SymbolId].get();
  if (!NRS)
    return nullptr;

  return PDBSymbol::create(Session, *NRS);
}

NativeRawSymbol &SymbolCache::getNativeSymbolById(SymIndexId SymbolId) const {
  return *Cache[SymbolId];
}

uint32_t SymbolCache::getNumCompilands() const {
  if (!Dbi)
    return 0;

  return Dbi->modules().getModuleCount();
}

SymIndexId SymbolCache::getOrCreateGlobalSymbolByOffset(uint32_t Offset) {
  auto Iter = GlobalOffsetToSymbolId.find(Offset);
  if (Iter != GlobalOffsetToSymbolId.end())
    return Iter->second;

  SymbolStream &SS = cantFail(Session.getPDBFile().getPDBSymbolStream());
  CVSymbol CVS = SS.readRecord(Offset);
  SymIndexId Id = 0;
  switch (CVS.kind()) {
  case SymbolKind::S_UDT: {
    UDTSym US = cantFail(SymbolDeserializer::deserializeAs<UDTSym>(CVS));
    Id = createSymbol<NativeTypeTypedef>(std::move(US));
    break;
  }
  default:
    Id = createSymbolPlaceholder();
    break;
  }
  if (Id != 0) {
    assert(GlobalOffsetToSymbolId.count(Offset) == 0);
    GlobalOffsetToSymbolId[Offset] = Id;
  }

  return Id;
}

std::unique_ptr<PDBSymbolCompiland>
SymbolCache::getOrCreateCompiland(uint32_t Index) {
  if (!Dbi)
    return nullptr;

  if (Index >= Compilands.size())
    return nullptr;

  if (Compilands[Index] == 0) {
    const DbiModuleList &Modules = Dbi->modules();
    Compilands[Index] =
        createSymbol<NativeCompilandSymbol>(Modules.getModuleDescriptor(Index));
  }

  return Session.getConcreteSymbolById<PDBSymbolCompiland>(Compilands[Index]);
}
