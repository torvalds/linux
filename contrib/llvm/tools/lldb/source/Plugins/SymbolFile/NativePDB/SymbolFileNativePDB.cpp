//===-- SymbolFileNativePDB.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileNativePDB.h"

#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"

#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamBuffer.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Demangle/MicrosoftDemangle.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"

#include "DWARFLocationExpression.h"
#include "PdbAstBuilder.h"
#include "PdbSymUid.h"
#include "PdbUtil.h"
#include "UdtRecordCompleter.h"

using namespace lldb;
using namespace lldb_private;
using namespace npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

static lldb::LanguageType TranslateLanguage(PDB_Lang lang) {
  switch (lang) {
  case PDB_Lang::Cpp:
    return lldb::LanguageType::eLanguageTypeC_plus_plus;
  case PDB_Lang::C:
    return lldb::LanguageType::eLanguageTypeC;
  default:
    return lldb::LanguageType::eLanguageTypeUnknown;
  }
}

static std::unique_ptr<PDBFile> loadPDBFile(std::string PdbPath,
                                            llvm::BumpPtrAllocator &Allocator) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ErrorOrBuffer =
      llvm::MemoryBuffer::getFile(PdbPath, /*FileSize=*/-1,
                                  /*RequiresNullTerminator=*/false);
  if (!ErrorOrBuffer)
    return nullptr;
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(*ErrorOrBuffer);

  llvm::StringRef Path = Buffer->getBufferIdentifier();
  auto Stream = llvm::make_unique<llvm::MemoryBufferByteStream>(
      std::move(Buffer), llvm::support::little);

  auto File = llvm::make_unique<PDBFile>(Path, std::move(Stream), Allocator);
  if (auto EC = File->parseFileHeaders()) {
    llvm::consumeError(std::move(EC));
    return nullptr;
  }
  if (auto EC = File->parseStreamData()) {
    llvm::consumeError(std::move(EC));
    return nullptr;
  }

  return File;
}

static std::unique_ptr<PDBFile>
loadMatchingPDBFile(std::string exe_path, llvm::BumpPtrAllocator &allocator) {
  // Try to find a matching PDB for an EXE.
  using namespace llvm::object;
  auto expected_binary = createBinary(exe_path);

  // If the file isn't a PE/COFF executable, fail.
  if (!expected_binary) {
    llvm::consumeError(expected_binary.takeError());
    return nullptr;
  }
  OwningBinary<Binary> binary = std::move(*expected_binary);

  auto *obj = llvm::dyn_cast<llvm::object::COFFObjectFile>(binary.getBinary());
  if (!obj)
    return nullptr;
  const llvm::codeview::DebugInfo *pdb_info = nullptr;

  // If it doesn't have a debug directory, fail.
  llvm::StringRef pdb_file;
  auto ec = obj->getDebugPDBInfo(pdb_info, pdb_file);
  if (ec)
    return nullptr;

  // if the file doesn't exist, is not a pdb, or doesn't have a matching guid,
  // fail.
  llvm::file_magic magic;
  ec = llvm::identify_magic(pdb_file, magic);
  if (ec || magic != llvm::file_magic::pdb)
    return nullptr;
  std::unique_ptr<PDBFile> pdb = loadPDBFile(pdb_file, allocator);
  if (!pdb)
    return nullptr;

  auto expected_info = pdb->getPDBInfoStream();
  if (!expected_info) {
    llvm::consumeError(expected_info.takeError());
    return nullptr;
  }
  llvm::codeview::GUID guid;
  memcpy(&guid, pdb_info->PDB70.Signature, 16);

  if (expected_info->getGuid() != guid)
    return nullptr;
  return pdb;
}

static bool IsFunctionPrologue(const CompilandIndexItem &cci,
                               lldb::addr_t addr) {
  // FIXME: Implement this.
  return false;
}

static bool IsFunctionEpilogue(const CompilandIndexItem &cci,
                               lldb::addr_t addr) {
  // FIXME: Implement this.
  return false;
}

static llvm::StringRef GetSimpleTypeName(SimpleTypeKind kind) {
  switch (kind) {
  case SimpleTypeKind::Boolean128:
  case SimpleTypeKind::Boolean16:
  case SimpleTypeKind::Boolean32:
  case SimpleTypeKind::Boolean64:
  case SimpleTypeKind::Boolean8:
    return "bool";
  case SimpleTypeKind::Byte:
  case SimpleTypeKind::UnsignedCharacter:
    return "unsigned char";
  case SimpleTypeKind::NarrowCharacter:
    return "char";
  case SimpleTypeKind::SignedCharacter:
  case SimpleTypeKind::SByte:
    return "signed char";
  case SimpleTypeKind::Character16:
    return "char16_t";
  case SimpleTypeKind::Character32:
    return "char32_t";
  case SimpleTypeKind::Complex80:
  case SimpleTypeKind::Complex64:
  case SimpleTypeKind::Complex32:
    return "complex";
  case SimpleTypeKind::Float128:
  case SimpleTypeKind::Float80:
    return "long double";
  case SimpleTypeKind::Float64:
    return "double";
  case SimpleTypeKind::Float32:
    return "float";
  case SimpleTypeKind::Float16:
    return "single";
  case SimpleTypeKind::Int128:
    return "__int128";
  case SimpleTypeKind::Int64:
  case SimpleTypeKind::Int64Quad:
    return "int64_t";
  case SimpleTypeKind::Int32:
    return "int";
  case SimpleTypeKind::Int16:
    return "short";
  case SimpleTypeKind::UInt128:
    return "unsigned __int128";
  case SimpleTypeKind::UInt64:
  case SimpleTypeKind::UInt64Quad:
    return "uint64_t";
  case SimpleTypeKind::HResult:
    return "HRESULT";
  case SimpleTypeKind::UInt32:
    return "unsigned";
  case SimpleTypeKind::UInt16:
  case SimpleTypeKind::UInt16Short:
    return "unsigned short";
  case SimpleTypeKind::Int32Long:
    return "long";
  case SimpleTypeKind::UInt32Long:
    return "unsigned long";
  case SimpleTypeKind::Void:
    return "void";
  case SimpleTypeKind::WideCharacter:
    return "wchar_t";
  default:
    return "";
  }
}

static bool IsClassRecord(TypeLeafKind kind) {
  switch (kind) {
  case LF_STRUCTURE:
  case LF_CLASS:
  case LF_INTERFACE:
    return true;
  default:
    return false;
  }
}

void SymbolFileNativePDB::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInitialize);
}

void SymbolFileNativePDB::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

void SymbolFileNativePDB::DebuggerInitialize(Debugger &debugger) {}

ConstString SymbolFileNativePDB::GetPluginNameStatic() {
  static ConstString g_name("native-pdb");
  return g_name;
}

const char *SymbolFileNativePDB::GetPluginDescriptionStatic() {
  return "Microsoft PDB debug symbol cross-platform file reader.";
}

SymbolFile *SymbolFileNativePDB::CreateInstance(ObjectFile *obj_file) {
  return new SymbolFileNativePDB(obj_file);
}

SymbolFileNativePDB::SymbolFileNativePDB(ObjectFile *object_file)
    : SymbolFile(object_file) {}

SymbolFileNativePDB::~SymbolFileNativePDB() {}

uint32_t SymbolFileNativePDB::CalculateAbilities() {
  uint32_t abilities = 0;
  if (!m_obj_file)
    return 0;

  if (!m_index) {
    // Lazily load and match the PDB file, but only do this once.
    std::unique_ptr<PDBFile> file_up =
        loadMatchingPDBFile(m_obj_file->GetFileSpec().GetPath(), m_allocator);

    if (!file_up) {
      auto module_sp = m_obj_file->GetModule();
      if (!module_sp)
        return 0;
      // See if any symbol file is specified through `--symfile` option.
      FileSpec symfile = module_sp->GetSymbolFileFileSpec();
      if (!symfile)
        return 0;
      file_up = loadPDBFile(symfile.GetPath(), m_allocator);
    }

    if (!file_up)
      return 0;

    auto expected_index = PdbIndex::create(std::move(file_up));
    if (!expected_index) {
      llvm::consumeError(expected_index.takeError());
      return 0;
    }
    m_index = std::move(*expected_index);
  }
  if (!m_index)
    return 0;

  // We don't especially have to be precise here.  We only distinguish between
  // stripped and not stripped.
  abilities = kAllAbilities;

  if (m_index->dbi().isStripped())
    abilities &= ~(Blocks | LocalVariables);
  return abilities;
}

void SymbolFileNativePDB::InitializeObject() {
  m_obj_load_address = m_obj_file->GetFileOffset();
  m_index->SetLoadAddress(m_obj_load_address);
  m_index->ParseSectionContribs();

  TypeSystem *ts = m_obj_file->GetModule()->GetTypeSystemForLanguage(
      lldb::eLanguageTypeC_plus_plus);
  if (ts)
    ts->SetSymbolFile(this);

  m_ast = llvm::make_unique<PdbAstBuilder>(*m_obj_file, *m_index);
}

uint32_t SymbolFileNativePDB::GetNumCompileUnits() {
  const DbiModuleList &modules = m_index->dbi().modules();
  uint32_t count = modules.getModuleCount();
  if (count == 0)
    return count;

  // The linker can inject an additional "dummy" compilation unit into the
  // PDB. Ignore this special compile unit for our purposes, if it is there.
  // It is always the last one.
  DbiModuleDescriptor last = modules.getModuleDescriptor(count - 1);
  if (last.getModuleName() == "* Linker *")
    --count;
  return count;
}

Block &SymbolFileNativePDB::CreateBlock(PdbCompilandSymId block_id) {
  CompilandIndexItem *cii = m_index->compilands().GetCompiland(block_id.modi);
  CVSymbol sym = cii->m_debug_stream.readSymbolAtOffset(block_id.offset);

  if (sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32) {
    // This is a function.  It must be global.  Creating the Function entry for
    // it automatically creates a block for it.
    CompUnitSP comp_unit = GetOrCreateCompileUnit(*cii);
    return GetOrCreateFunction(block_id, *comp_unit)->GetBlock(false);
  }

  lldbassert(sym.kind() == S_BLOCK32);

  // This is a block.  Its parent is either a function or another block.  In
  // either case, its parent can be viewed as a block (e.g. a function contains
  // 1 big block.  So just get the parent block and add this block to it.
  BlockSym block(static_cast<SymbolRecordKind>(sym.kind()));
  cantFail(SymbolDeserializer::deserializeAs<BlockSym>(sym, block));
  lldbassert(block.Parent != 0);
  PdbCompilandSymId parent_id(block_id.modi, block.Parent);
  Block &parent_block = GetOrCreateBlock(parent_id);
  lldb::user_id_t opaque_block_uid = toOpaqueUid(block_id);
  BlockSP child_block = std::make_shared<Block>(opaque_block_uid);
  parent_block.AddChild(child_block);

  m_ast->GetOrCreateBlockDecl(block_id);

  m_blocks.insert({opaque_block_uid, child_block});
  return *child_block;
}

lldb::FunctionSP SymbolFileNativePDB::CreateFunction(PdbCompilandSymId func_id,
                                                     CompileUnit &comp_unit) {
  const CompilandIndexItem *cci =
      m_index->compilands().GetCompiland(func_id.modi);
  lldbassert(cci);
  CVSymbol sym_record = cci->m_debug_stream.readSymbolAtOffset(func_id.offset);

  lldbassert(sym_record.kind() == S_LPROC32 || sym_record.kind() == S_GPROC32);
  SegmentOffsetLength sol = GetSegmentOffsetAndLength(sym_record);

  auto file_vm_addr = m_index->MakeVirtualAddress(sol.so);
  if (file_vm_addr == LLDB_INVALID_ADDRESS || file_vm_addr == 0)
    return nullptr;

  AddressRange func_range(file_vm_addr, sol.length,
                          comp_unit.GetModule()->GetSectionList());
  if (!func_range.GetBaseAddress().IsValid())
    return nullptr;

  ProcSym proc(static_cast<SymbolRecordKind>(sym_record.kind()));
  cantFail(SymbolDeserializer::deserializeAs<ProcSym>(sym_record, proc));
  if (proc.FunctionType == TypeIndex::None())
    return nullptr;
  TypeSP func_type = GetOrCreateType(proc.FunctionType);
  if (!func_type)
    return nullptr;

  PdbTypeSymId sig_id(proc.FunctionType, false);
  Mangled mangled(proc.Name);
  FunctionSP func_sp = std::make_shared<Function>(
      &comp_unit, toOpaqueUid(func_id), toOpaqueUid(sig_id), mangled,
      func_type.get(), func_range);

  comp_unit.AddFunction(func_sp);

  m_ast->GetOrCreateFunctionDecl(func_id);

  return func_sp;
}

CompUnitSP
SymbolFileNativePDB::CreateCompileUnit(const CompilandIndexItem &cci) {
  lldb::LanguageType lang =
      cci.m_compile_opts ? TranslateLanguage(cci.m_compile_opts->getLanguage())
                         : lldb::eLanguageTypeUnknown;

  LazyBool optimized = eLazyBoolNo;
  if (cci.m_compile_opts && cci.m_compile_opts->hasOptimizations())
    optimized = eLazyBoolYes;

  llvm::SmallString<64> source_file_name =
      m_index->compilands().GetMainSourceFile(cci);
  FileSpec fs(source_file_name);

  CompUnitSP cu_sp =
      std::make_shared<CompileUnit>(m_obj_file->GetModule(), nullptr, fs,
                                    toOpaqueUid(cci.m_id), lang, optimized);

  m_obj_file->GetModule()->GetSymbolVendor()->SetCompileUnitAtIndex(
      cci.m_id.modi, cu_sp);
  return cu_sp;
}

lldb::TypeSP SymbolFileNativePDB::CreateModifierType(PdbTypeSymId type_id,
                                                     const ModifierRecord &mr,
                                                     CompilerType ct) {
  TpiStream &stream = m_index->tpi();

  std::string name;
  if (mr.ModifiedType.isSimple())
    name = GetSimpleTypeName(mr.ModifiedType.getSimpleKind());
  else
    name = computeTypeName(stream.typeCollection(), mr.ModifiedType);
  Declaration decl;
  lldb::TypeSP modified_type = GetOrCreateType(mr.ModifiedType);

  return std::make_shared<Type>(toOpaqueUid(type_id), this, ConstString(name),
                                modified_type->GetByteSize(), nullptr,
                                LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                                ct, Type::eResolveStateFull);
}

lldb::TypeSP
SymbolFileNativePDB::CreatePointerType(PdbTypeSymId type_id,
                                       const llvm::codeview::PointerRecord &pr,
                                       CompilerType ct) {
  TypeSP pointee = GetOrCreateType(pr.ReferentType);
  if (!pointee)
    return nullptr;

  if (pr.isPointerToMember()) {
    MemberPointerInfo mpi = pr.getMemberInfo();
    GetOrCreateType(mpi.ContainingType);
  }

  Declaration decl;
  return std::make_shared<Type>(toOpaqueUid(type_id), this, ConstString(),
                                pr.getSize(), nullptr, LLDB_INVALID_UID,
                                Type::eEncodingIsUID, decl, ct,
                                Type::eResolveStateFull);
}

lldb::TypeSP SymbolFileNativePDB::CreateSimpleType(TypeIndex ti,
                                                   CompilerType ct) {
  uint64_t uid = toOpaqueUid(PdbTypeSymId(ti, false));
  if (ti == TypeIndex::NullptrT()) {
    Declaration decl;
    return std::make_shared<Type>(
        uid, this, ConstString("std::nullptr_t"), 0, nullptr, LLDB_INVALID_UID,
        Type::eEncodingIsUID, decl, ct, Type::eResolveStateFull);
  }

  if (ti.getSimpleMode() != SimpleTypeMode::Direct) {
    TypeSP direct_sp = GetOrCreateType(ti.makeDirect());
    uint32_t pointer_size = 0;
    switch (ti.getSimpleMode()) {
    case SimpleTypeMode::FarPointer32:
    case SimpleTypeMode::NearPointer32:
      pointer_size = 4;
      break;
    case SimpleTypeMode::NearPointer64:
      pointer_size = 8;
      break;
    default:
      // 128-bit and 16-bit pointers unsupported.
      return nullptr;
    }
    Declaration decl;
    return std::make_shared<Type>(
        uid, this, ConstString(), pointer_size, nullptr, LLDB_INVALID_UID,
        Type::eEncodingIsUID, decl, ct, Type::eResolveStateFull);
  }

  if (ti.getSimpleKind() == SimpleTypeKind::NotTranslated)
    return nullptr;

  size_t size = GetTypeSizeForSimpleKind(ti.getSimpleKind());
  llvm::StringRef type_name = GetSimpleTypeName(ti.getSimpleKind());

  Declaration decl;
  return std::make_shared<Type>(uid, this, ConstString(type_name), size,
                                nullptr, LLDB_INVALID_UID, Type::eEncodingIsUID,
                                decl, ct, Type::eResolveStateFull);
}

static std::string GetUnqualifiedTypeName(const TagRecord &record) {
  if (!record.hasUniqueName()) {
    MSVCUndecoratedNameParser parser(record.Name);
    llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();

    return specs.back().GetBaseName();
  }

  llvm::ms_demangle::Demangler demangler;
  StringView sv(record.UniqueName.begin(), record.UniqueName.size());
  llvm::ms_demangle::TagTypeNode *ttn = demangler.parseTagUniqueName(sv);
  if (demangler.Error)
    return record.Name;

  llvm::ms_demangle::IdentifierNode *idn =
      ttn->QualifiedName->getUnqualifiedIdentifier();
  return idn->toString();
}

lldb::TypeSP
SymbolFileNativePDB::CreateClassStructUnion(PdbTypeSymId type_id,
                                            const TagRecord &record,
                                            size_t size, CompilerType ct) {

  std::string uname = GetUnqualifiedTypeName(record);

  // FIXME: Search IPI stream for LF_UDT_MOD_SRC_LINE.
  Declaration decl;
  return std::make_shared<Type>(toOpaqueUid(type_id), this, ConstString(uname),
                                size, nullptr, LLDB_INVALID_UID,
                                Type::eEncodingIsUID, decl, ct,
                                Type::eResolveStateForward);
}

lldb::TypeSP SymbolFileNativePDB::CreateTagType(PdbTypeSymId type_id,
                                                const ClassRecord &cr,
                                                CompilerType ct) {
  return CreateClassStructUnion(type_id, cr, cr.getSize(), ct);
}

lldb::TypeSP SymbolFileNativePDB::CreateTagType(PdbTypeSymId type_id,
                                                const UnionRecord &ur,
                                                CompilerType ct) {
  return CreateClassStructUnion(type_id, ur, ur.getSize(), ct);
}

lldb::TypeSP SymbolFileNativePDB::CreateTagType(PdbTypeSymId type_id,
                                                const EnumRecord &er,
                                                CompilerType ct) {
  std::string uname = GetUnqualifiedTypeName(er);

  Declaration decl;
  TypeSP underlying_type = GetOrCreateType(er.UnderlyingType);

  return std::make_shared<lldb_private::Type>(
      toOpaqueUid(type_id), this, ConstString(uname),
      underlying_type->GetByteSize(), nullptr, LLDB_INVALID_UID,
      lldb_private::Type::eEncodingIsUID, decl, ct,
      lldb_private::Type::eResolveStateForward);
}

TypeSP SymbolFileNativePDB::CreateArrayType(PdbTypeSymId type_id,
                                            const ArrayRecord &ar,
                                            CompilerType ct) {
  TypeSP element_type = GetOrCreateType(ar.ElementType);

  Declaration decl;
  TypeSP array_sp = std::make_shared<lldb_private::Type>(
      toOpaqueUid(type_id), this, ConstString(), ar.Size, nullptr,
      LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID, decl, ct,
      lldb_private::Type::eResolveStateFull);
  array_sp->SetEncodingType(element_type.get());
  return array_sp;
}

TypeSP SymbolFileNativePDB::CreateProcedureType(PdbTypeSymId type_id,
                                                const ProcedureRecord &pr,
                                                CompilerType ct) {
  Declaration decl;
  return std::make_shared<lldb_private::Type>(
      toOpaqueUid(type_id), this, ConstString(), 0, nullptr, LLDB_INVALID_UID,
      lldb_private::Type::eEncodingIsUID, decl, ct,
      lldb_private::Type::eResolveStateFull);
}

TypeSP SymbolFileNativePDB::CreateType(PdbTypeSymId type_id, CompilerType ct) {
  if (type_id.index.isSimple())
    return CreateSimpleType(type_id.index, ct);

  TpiStream &stream = type_id.is_ipi ? m_index->ipi() : m_index->tpi();
  CVType cvt = stream.getType(type_id.index);

  if (cvt.kind() == LF_MODIFIER) {
    ModifierRecord modifier;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<ModifierRecord>(cvt, modifier));
    return CreateModifierType(type_id, modifier, ct);
  }

  if (cvt.kind() == LF_POINTER) {
    PointerRecord pointer;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<PointerRecord>(cvt, pointer));
    return CreatePointerType(type_id, pointer, ct);
  }

  if (IsClassRecord(cvt.kind())) {
    ClassRecord cr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, cr));
    return CreateTagType(type_id, cr, ct);
  }

  if (cvt.kind() == LF_ENUM) {
    EnumRecord er;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, er));
    return CreateTagType(type_id, er, ct);
  }

  if (cvt.kind() == LF_UNION) {
    UnionRecord ur;
    llvm::cantFail(TypeDeserializer::deserializeAs<UnionRecord>(cvt, ur));
    return CreateTagType(type_id, ur, ct);
  }

  if (cvt.kind() == LF_ARRAY) {
    ArrayRecord ar;
    llvm::cantFail(TypeDeserializer::deserializeAs<ArrayRecord>(cvt, ar));
    return CreateArrayType(type_id, ar, ct);
  }

  if (cvt.kind() == LF_PROCEDURE) {
    ProcedureRecord pr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ProcedureRecord>(cvt, pr));
    return CreateProcedureType(type_id, pr, ct);
  }

  return nullptr;
}

TypeSP SymbolFileNativePDB::CreateAndCacheType(PdbTypeSymId type_id) {
  // If they search for a UDT which is a forward ref, try and resolve the full
  // decl and just map the forward ref uid to the full decl record.
  llvm::Optional<PdbTypeSymId> full_decl_uid;
  if (IsForwardRefUdt(type_id, m_index->tpi())) {
    auto expected_full_ti =
        m_index->tpi().findFullDeclForForwardRef(type_id.index);
    if (!expected_full_ti)
      llvm::consumeError(expected_full_ti.takeError());
    else if (*expected_full_ti != type_id.index) {
      full_decl_uid = PdbTypeSymId(*expected_full_ti, false);

      // It's possible that a lookup would occur for the full decl causing it
      // to be cached, then a second lookup would occur for the forward decl.
      // We don't want to create a second full decl, so make sure the full
      // decl hasn't already been cached.
      auto full_iter = m_types.find(toOpaqueUid(*full_decl_uid));
      if (full_iter != m_types.end()) {
        TypeSP result = full_iter->second;
        // Map the forward decl to the TypeSP for the full decl so we can take
        // the fast path next time.
        m_types[toOpaqueUid(type_id)] = result;
        return result;
      }
    }
  }

  PdbTypeSymId best_decl_id = full_decl_uid ? *full_decl_uid : type_id;

  clang::QualType qt = m_ast->GetOrCreateType(best_decl_id);

  TypeSP result = CreateType(best_decl_id, m_ast->ToCompilerType(qt));
  if (!result)
    return nullptr;

  uint64_t best_uid = toOpaqueUid(best_decl_id);
  m_types[best_uid] = result;
  // If we had both a forward decl and a full decl, make both point to the new
  // type.
  if (full_decl_uid)
    m_types[toOpaqueUid(type_id)] = result;

  return result;
}

TypeSP SymbolFileNativePDB::GetOrCreateType(PdbTypeSymId type_id) {
  // We can't use try_emplace / overwrite here because the process of creating
  // a type could create nested types, which could invalidate iterators.  So
  // we have to do a 2-phase lookup / insert.
  auto iter = m_types.find(toOpaqueUid(type_id));
  if (iter != m_types.end())
    return iter->second;

  TypeSP type = CreateAndCacheType(type_id);
  if (type)
    m_obj_file->GetModule()->GetTypeList()->Insert(type);
  return type;
}

VariableSP SymbolFileNativePDB::CreateGlobalVariable(PdbGlobalSymId var_id) {
  CVSymbol sym = m_index->symrecords().readRecord(var_id.offset);
  if (sym.kind() == S_CONSTANT)
    return CreateConstantSymbol(var_id, sym);

  lldb::ValueType scope = eValueTypeInvalid;
  TypeIndex ti;
  llvm::StringRef name;
  lldb::addr_t addr = 0;
  uint16_t section = 0;
  uint32_t offset = 0;
  bool is_external = false;
  switch (sym.kind()) {
  case S_GDATA32:
    is_external = true;
    LLVM_FALLTHROUGH;
  case S_LDATA32: {
    DataSym ds(sym.kind());
    llvm::cantFail(SymbolDeserializer::deserializeAs<DataSym>(sym, ds));
    ti = ds.Type;
    scope = (sym.kind() == S_GDATA32) ? eValueTypeVariableGlobal
                                      : eValueTypeVariableStatic;
    name = ds.Name;
    section = ds.Segment;
    offset = ds.DataOffset;
    addr = m_index->MakeVirtualAddress(ds.Segment, ds.DataOffset);
    break;
  }
  case S_GTHREAD32:
    is_external = true;
    LLVM_FALLTHROUGH;
  case S_LTHREAD32: {
    ThreadLocalDataSym tlds(sym.kind());
    llvm::cantFail(
        SymbolDeserializer::deserializeAs<ThreadLocalDataSym>(sym, tlds));
    ti = tlds.Type;
    name = tlds.Name;
    section = tlds.Segment;
    offset = tlds.DataOffset;
    addr = m_index->MakeVirtualAddress(tlds.Segment, tlds.DataOffset);
    scope = eValueTypeVariableThreadLocal;
    break;
  }
  default:
    llvm_unreachable("unreachable!");
  }

  CompUnitSP comp_unit;
  llvm::Optional<uint16_t> modi = m_index->GetModuleIndexForVa(addr);
  if (modi) {
    CompilandIndexItem &cci = m_index->compilands().GetOrCreateCompiland(*modi);
    comp_unit = GetOrCreateCompileUnit(cci);
  }

  Declaration decl;
  PdbTypeSymId tid(ti, false);
  SymbolFileTypeSP type_sp =
      std::make_shared<SymbolFileType>(*this, toOpaqueUid(tid));
  Variable::RangeList ranges;

  m_ast->GetOrCreateVariableDecl(var_id);

  DWARFExpression location = MakeGlobalLocationExpression(
      section, offset, GetObjectFile()->GetModule());

  std::string global_name("::");
  global_name += name;
  VariableSP var_sp = std::make_shared<Variable>(
      toOpaqueUid(var_id), name.str().c_str(), global_name.c_str(), type_sp,
      scope, comp_unit.get(), ranges, &decl, location, is_external, false,
      false);
  var_sp->SetLocationIsConstantValueData(false);

  return var_sp;
}

lldb::VariableSP
SymbolFileNativePDB::CreateConstantSymbol(PdbGlobalSymId var_id,
                                          const CVSymbol &cvs) {
  TpiStream &tpi = m_index->tpi();
  ConstantSym constant(cvs.kind());

  llvm::cantFail(SymbolDeserializer::deserializeAs<ConstantSym>(cvs, constant));
  std::string global_name("::");
  global_name += constant.Name;
  PdbTypeSymId tid(constant.Type, false);
  SymbolFileTypeSP type_sp =
      std::make_shared<SymbolFileType>(*this, toOpaqueUid(tid));

  Declaration decl;
  Variable::RangeList ranges;
  ModuleSP module = GetObjectFile()->GetModule();
  DWARFExpression location = MakeConstantLocationExpression(
      constant.Type, tpi, constant.Value, module);

  VariableSP var_sp = std::make_shared<Variable>(
      toOpaqueUid(var_id), constant.Name.str().c_str(), global_name.c_str(),
      type_sp, eValueTypeVariableGlobal, module.get(), ranges, &decl, location,
      false, false, false);
  var_sp->SetLocationIsConstantValueData(true);
  return var_sp;
}

VariableSP
SymbolFileNativePDB::GetOrCreateGlobalVariable(PdbGlobalSymId var_id) {
  auto emplace_result = m_global_vars.try_emplace(toOpaqueUid(var_id), nullptr);
  if (emplace_result.second)
    emplace_result.first->second = CreateGlobalVariable(var_id);

  return emplace_result.first->second;
}

lldb::TypeSP SymbolFileNativePDB::GetOrCreateType(TypeIndex ti) {
  return GetOrCreateType(PdbTypeSymId(ti, false));
}

FunctionSP SymbolFileNativePDB::GetOrCreateFunction(PdbCompilandSymId func_id,
                                                    CompileUnit &comp_unit) {
  auto emplace_result = m_functions.try_emplace(toOpaqueUid(func_id), nullptr);
  if (emplace_result.second)
    emplace_result.first->second = CreateFunction(func_id, comp_unit);

  return emplace_result.first->second;
}

CompUnitSP
SymbolFileNativePDB::GetOrCreateCompileUnit(const CompilandIndexItem &cci) {

  auto emplace_result =
      m_compilands.try_emplace(toOpaqueUid(cci.m_id), nullptr);
  if (emplace_result.second)
    emplace_result.first->second = CreateCompileUnit(cci);

  lldbassert(emplace_result.first->second);
  return emplace_result.first->second;
}

Block &SymbolFileNativePDB::GetOrCreateBlock(PdbCompilandSymId block_id) {
  auto iter = m_blocks.find(toOpaqueUid(block_id));
  if (iter != m_blocks.end())
    return *iter->second;

  return CreateBlock(block_id);
}

void SymbolFileNativePDB::ParseDeclsForContext(
    lldb_private::CompilerDeclContext decl_ctx) {
  clang::DeclContext *context = m_ast->FromCompilerDeclContext(decl_ctx);
  if (!context)
    return;
  m_ast->ParseDeclsForContext(*context);
}

lldb::CompUnitSP SymbolFileNativePDB::ParseCompileUnitAtIndex(uint32_t index) {
  if (index >= GetNumCompileUnits())
    return CompUnitSP();
  lldbassert(index < UINT16_MAX);
  if (index >= UINT16_MAX)
    return nullptr;

  CompilandIndexItem &item = m_index->compilands().GetOrCreateCompiland(index);

  return GetOrCreateCompileUnit(item);
}

lldb::LanguageType SymbolFileNativePDB::ParseLanguage(CompileUnit &comp_unit) {
  PdbSymUid uid(comp_unit.GetID());
  lldbassert(uid.kind() == PdbSymUidKind::Compiland);

  CompilandIndexItem *item =
      m_index->compilands().GetCompiland(uid.asCompiland().modi);
  lldbassert(item);
  if (!item->m_compile_opts)
    return lldb::eLanguageTypeUnknown;

  return TranslateLanguage(item->m_compile_opts->getLanguage());
}

void SymbolFileNativePDB::AddSymbols(Symtab &symtab) { return; }

size_t SymbolFileNativePDB::ParseFunctions(CompileUnit &comp_unit) {
  PdbSymUid uid{comp_unit.GetID()};
  lldbassert(uid.kind() == PdbSymUidKind::Compiland);
  uint16_t modi = uid.asCompiland().modi;
  CompilandIndexItem &cii = m_index->compilands().GetOrCreateCompiland(modi);

  size_t count = comp_unit.GetNumFunctions();
  const CVSymbolArray &syms = cii.m_debug_stream.getSymbolArray();
  for (auto iter = syms.begin(); iter != syms.end(); ++iter) {
    if (iter->kind() != S_LPROC32 && iter->kind() != S_GPROC32)
      continue;

    PdbCompilandSymId sym_id{modi, iter.offset()};

    FunctionSP func = GetOrCreateFunction(sym_id, comp_unit);
  }

  size_t new_count = comp_unit.GetNumFunctions();
  lldbassert(new_count >= count);
  return new_count - count;
}

static bool NeedsResolvedCompileUnit(uint32_t resolve_scope) {
  // If any of these flags are set, we need to resolve the compile unit.
  uint32_t flags = eSymbolContextCompUnit;
  flags |= eSymbolContextVariable;
  flags |= eSymbolContextFunction;
  flags |= eSymbolContextBlock;
  flags |= eSymbolContextLineEntry;
  return (resolve_scope & flags) != 0;
}

uint32_t SymbolFileNativePDB::ResolveSymbolContext(
    const Address &addr, SymbolContextItem resolve_scope, SymbolContext &sc) {
  uint32_t resolved_flags = 0;
  lldb::addr_t file_addr = addr.GetFileAddress();

  if (NeedsResolvedCompileUnit(resolve_scope)) {
    llvm::Optional<uint16_t> modi = m_index->GetModuleIndexForVa(file_addr);
    if (!modi)
      return 0;
    CompilandIndexItem *cci = m_index->compilands().GetCompiland(*modi);
    if (!cci)
      return 0;

    sc.comp_unit = GetOrCreateCompileUnit(*cci).get();
    resolved_flags |= eSymbolContextCompUnit;
  }

  if (resolve_scope & eSymbolContextFunction ||
      resolve_scope & eSymbolContextBlock) {
    lldbassert(sc.comp_unit);
    std::vector<SymbolAndUid> matches = m_index->FindSymbolsByVa(file_addr);
    // Search the matches in reverse.  This way if there are multiple matches
    // (for example we are 3 levels deep in a nested scope) it will find the
    // innermost one first.
    for (const auto &match : llvm::reverse(matches)) {
      if (match.uid.kind() != PdbSymUidKind::CompilandSym)
        continue;

      PdbCompilandSymId csid = match.uid.asCompilandSym();
      CVSymbol cvs = m_index->ReadSymbolRecord(csid);
      PDB_SymType type = CVSymToPDBSym(cvs.kind());
      if (type != PDB_SymType::Function && type != PDB_SymType::Block)
        continue;
      if (type == PDB_SymType::Function) {
        sc.function = GetOrCreateFunction(csid, *sc.comp_unit).get();
        sc.block = sc.GetFunctionBlock();
      }

      if (type == PDB_SymType::Block) {
        sc.block = &GetOrCreateBlock(csid);
        sc.function = sc.block->CalculateSymbolContextFunction();
      }
    resolved_flags |= eSymbolContextFunction;
    resolved_flags |= eSymbolContextBlock;
    break;
    }
  }

  if (resolve_scope & eSymbolContextLineEntry) {
    lldbassert(sc.comp_unit);
    if (auto *line_table = sc.comp_unit->GetLineTable()) {
      if (line_table->FindLineEntryByAddress(addr, sc.line_entry))
        resolved_flags |= eSymbolContextLineEntry;
    }
  }

  return resolved_flags;
}

uint32_t SymbolFileNativePDB::ResolveSymbolContext(
    const FileSpec &file_spec, uint32_t line, bool check_inlines,
    lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  return 0;
}

static void AppendLineEntryToSequence(LineTable &table, LineSequence &sequence,
                                      const CompilandIndexItem &cci,
                                      lldb::addr_t base_addr,
                                      uint32_t file_number,
                                      const LineFragmentHeader &block,
                                      const LineNumberEntry &cur) {
  LineInfo cur_info(cur.Flags);

  if (cur_info.isAlwaysStepInto() || cur_info.isNeverStepInto())
    return;

  uint64_t addr = base_addr + cur.Offset;

  bool is_statement = cur_info.isStatement();
  bool is_prologue = IsFunctionPrologue(cci, addr);
  bool is_epilogue = IsFunctionEpilogue(cci, addr);

  uint32_t lno = cur_info.getStartLine();

  table.AppendLineEntryToSequence(&sequence, addr, lno, 0, file_number,
                                  is_statement, false, is_prologue, is_epilogue,
                                  false);
}

static void TerminateLineSequence(LineTable &table,
                                  const LineFragmentHeader &block,
                                  lldb::addr_t base_addr, uint32_t file_number,
                                  uint32_t last_line,
                                  std::unique_ptr<LineSequence> seq) {
  // The end is always a terminal entry, so insert it regardless.
  table.AppendLineEntryToSequence(seq.get(), base_addr + block.CodeSize,
                                  last_line, 0, file_number, false, false,
                                  false, false, true);
  table.InsertSequence(seq.release());
}

bool SymbolFileNativePDB::ParseLineTable(CompileUnit &comp_unit) {
  // Unfortunately LLDB is set up to parse the entire compile unit line table
  // all at once, even if all it really needs is line info for a specific
  // function.  In the future it would be nice if it could set the sc.m_function
  // member, and we could only get the line info for the function in question.
  PdbSymUid cu_id(comp_unit.GetID());
  lldbassert(cu_id.kind() == PdbSymUidKind::Compiland);
  CompilandIndexItem *cci =
      m_index->compilands().GetCompiland(cu_id.asCompiland().modi);
  lldbassert(cci);
  auto line_table = llvm::make_unique<LineTable>(&comp_unit);

  // This is basically a copy of the .debug$S subsections from all original COFF
  // object files merged together with address relocations applied.  We are
  // looking for all DEBUG_S_LINES subsections.
  for (const DebugSubsectionRecord &dssr :
       cci->m_debug_stream.getSubsectionsArray()) {
    if (dssr.kind() != DebugSubsectionKind::Lines)
      continue;

    DebugLinesSubsectionRef lines;
    llvm::BinaryStreamReader reader(dssr.getRecordData());
    if (auto EC = lines.initialize(reader)) {
      llvm::consumeError(std::move(EC));
      return false;
    }

    const LineFragmentHeader *lfh = lines.header();
    uint64_t virtual_addr =
        m_index->MakeVirtualAddress(lfh->RelocSegment, lfh->RelocOffset);

    const auto &checksums = cci->m_strings.checksums().getArray();
    const auto &strings = cci->m_strings.strings();
    for (const LineColumnEntry &group : lines) {
      // Indices in this structure are actually offsets of records in the
      // DEBUG_S_FILECHECKSUMS subsection.  Those entries then have an index
      // into the global PDB string table.
      auto iter = checksums.at(group.NameIndex);
      if (iter == checksums.end())
        continue;

      llvm::Expected<llvm::StringRef> efn =
          strings.getString(iter->FileNameOffset);
      if (!efn) {
        llvm::consumeError(efn.takeError());
        continue;
      }

      // LLDB wants the index of the file in the list of support files.
      auto fn_iter = llvm::find(cci->m_file_list, *efn);
      lldbassert(fn_iter != cci->m_file_list.end());
      // LLDB support file indices are 1-based.
      uint32_t file_index =
          1 + std::distance(cci->m_file_list.begin(), fn_iter);

      std::unique_ptr<LineSequence> sequence(
          line_table->CreateLineSequenceContainer());
      lldbassert(!group.LineNumbers.empty());

      for (const LineNumberEntry &entry : group.LineNumbers) {
        AppendLineEntryToSequence(*line_table, *sequence, *cci, virtual_addr,
                                  file_index, *lfh, entry);
      }
      LineInfo last_line(group.LineNumbers.back().Flags);
      TerminateLineSequence(*line_table, *lfh, virtual_addr, file_index,
                            last_line.getEndLine(), std::move(sequence));
    }
  }

  if (line_table->GetSize() == 0)
    return false;

  comp_unit.SetLineTable(line_table.release());
  return true;
}

bool SymbolFileNativePDB::ParseDebugMacros(CompileUnit &comp_unit) {
  // PDB doesn't contain information about macros
  return false;
}

bool SymbolFileNativePDB::ParseSupportFiles(CompileUnit &comp_unit,
                                            FileSpecList &support_files) {
  PdbSymUid cu_id(comp_unit.GetID());
  lldbassert(cu_id.kind() == PdbSymUidKind::Compiland);
  CompilandIndexItem *cci =
      m_index->compilands().GetCompiland(cu_id.asCompiland().modi);
  lldbassert(cci);

  for (llvm::StringRef f : cci->m_file_list) {
    FileSpec::Style style =
        f.startswith("/") ? FileSpec::Style::posix : FileSpec::Style::windows;
    FileSpec spec(f, style);
    support_files.Append(spec);
  }

  llvm::SmallString<64> main_source_file =
      m_index->compilands().GetMainSourceFile(*cci);
  FileSpec::Style style = main_source_file.startswith("/")
                              ? FileSpec::Style::posix
                              : FileSpec::Style::windows;
  FileSpec spec(main_source_file, style);
  support_files.Insert(0, spec);
  return true;
}

bool SymbolFileNativePDB::ParseImportedModules(
    const SymbolContext &sc, std::vector<ConstString> &imported_modules) {
  // PDB does not yet support module debug info
  return false;
}

size_t SymbolFileNativePDB::ParseBlocksRecursive(Function &func) {
  GetOrCreateBlock(PdbSymUid(func.GetID()).asCompilandSym());
  // FIXME: Parse child blocks
  return 1;
}

void SymbolFileNativePDB::DumpClangAST(Stream &s) { m_ast->Dump(s); }

uint32_t SymbolFileNativePDB::FindGlobalVariables(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    uint32_t max_matches, VariableList &variables) {
  using SymbolAndOffset = std::pair<uint32_t, llvm::codeview::CVSymbol>;

  std::vector<SymbolAndOffset> results = m_index->globals().findRecordsByName(
      name.GetStringRef(), m_index->symrecords());
  for (const SymbolAndOffset &result : results) {
    VariableSP var;
    switch (result.second.kind()) {
    case SymbolKind::S_GDATA32:
    case SymbolKind::S_LDATA32:
    case SymbolKind::S_GTHREAD32:
    case SymbolKind::S_LTHREAD32:
    case SymbolKind::S_CONSTANT: {
      PdbGlobalSymId global(result.first, false);
      var = GetOrCreateGlobalVariable(global);
      variables.AddVariable(var);
      break;
    }
    default:
      continue;
    }
  }
  return variables.GetSize();
}

uint32_t SymbolFileNativePDB::FindFunctions(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    FunctionNameType name_type_mask, bool include_inlines, bool append,
    SymbolContextList &sc_list) {
  // For now we only support lookup by method name.
  if (!(name_type_mask & eFunctionNameTypeMethod))
    return 0;

  using SymbolAndOffset = std::pair<uint32_t, llvm::codeview::CVSymbol>;

  std::vector<SymbolAndOffset> matches = m_index->globals().findRecordsByName(
      name.GetStringRef(), m_index->symrecords());
  for (const SymbolAndOffset &match : matches) {
    if (match.second.kind() != S_PROCREF && match.second.kind() != S_LPROCREF)
      continue;
    ProcRefSym proc(match.second.kind());
    cantFail(SymbolDeserializer::deserializeAs<ProcRefSym>(match.second, proc));

    if (!IsValidRecord(proc))
      continue;

    CompilandIndexItem &cci =
        m_index->compilands().GetOrCreateCompiland(proc.modi());
    SymbolContext sc;

    sc.comp_unit = GetOrCreateCompileUnit(cci).get();
    PdbCompilandSymId func_id(proc.modi(), proc.SymOffset);
    sc.function = GetOrCreateFunction(func_id, *sc.comp_unit).get();

    sc_list.Append(sc);
  }

  return sc_list.GetSize();
}

uint32_t SymbolFileNativePDB::FindFunctions(const RegularExpression &regex,
                                            bool include_inlines, bool append,
                                            SymbolContextList &sc_list) {
  return 0;
}

uint32_t SymbolFileNativePDB::FindTypes(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    bool append, uint32_t max_matches,
    llvm::DenseSet<SymbolFile *> &searched_symbol_files, TypeMap &types) {
  if (!append)
    types.Clear();
  if (!name)
    return 0;

  searched_symbol_files.clear();
  searched_symbol_files.insert(this);

  // There is an assumption 'name' is not a regex
  size_t match_count = FindTypesByName(name.GetStringRef(), max_matches, types);

  return match_count;
}

size_t
SymbolFileNativePDB::FindTypes(const std::vector<CompilerContext> &context,
                               bool append, TypeMap &types) {
  return 0;
}

size_t SymbolFileNativePDB::FindTypesByName(llvm::StringRef name,
                                            uint32_t max_matches,
                                            TypeMap &types) {

  size_t match_count = 0;
  std::vector<TypeIndex> matches = m_index->tpi().findRecordsByName(name);
  if (max_matches > 0 && max_matches < matches.size())
    matches.resize(max_matches);

  for (TypeIndex ti : matches) {
    TypeSP type = GetOrCreateType(ti);
    if (!type)
      continue;

    types.Insert(type);
    ++match_count;
  }
  return match_count;
}

size_t SymbolFileNativePDB::ParseTypes(CompileUnit &comp_unit) {
  // Only do the full type scan the first time.
  if (m_done_full_type_scan)
    return 0;

  size_t old_count = m_obj_file->GetModule()->GetTypeList()->GetSize();
  LazyRandomTypeCollection &types = m_index->tpi().typeCollection();

  // First process the entire TPI stream.
  for (auto ti = types.getFirst(); ti; ti = types.getNext(*ti)) {
    TypeSP type = GetOrCreateType(*ti);
    if (type)
      (void)type->GetFullCompilerType();
  }

  // Next look for S_UDT records in the globals stream.
  for (const uint32_t gid : m_index->globals().getGlobalsTable()) {
    PdbGlobalSymId global{gid, false};
    CVSymbol sym = m_index->ReadSymbolRecord(global);
    if (sym.kind() != S_UDT)
      continue;

    UDTSym udt = llvm::cantFail(SymbolDeserializer::deserializeAs<UDTSym>(sym));
    bool is_typedef = true;
    if (IsTagRecord(PdbTypeSymId{udt.Type, false}, m_index->tpi())) {
      CVType cvt = m_index->tpi().getType(udt.Type);
      llvm::StringRef name = CVTagRecord::create(cvt).name();
      if (name == udt.Name)
        is_typedef = false;
    }

    if (is_typedef)
      GetOrCreateTypedef(global);
  }

  size_t new_count = m_obj_file->GetModule()->GetTypeList()->GetSize();

  m_done_full_type_scan = true;

  return new_count - old_count;
}

size_t
SymbolFileNativePDB::ParseVariablesForCompileUnit(CompileUnit &comp_unit,
                                                  VariableList &variables) {
  PdbSymUid sym_uid(comp_unit.GetID());
  lldbassert(sym_uid.kind() == PdbSymUidKind::Compiland);
  return 0;
}

VariableSP SymbolFileNativePDB::CreateLocalVariable(PdbCompilandSymId scope_id,
                                                    PdbCompilandSymId var_id,
                                                    bool is_param) {
  ModuleSP module = GetObjectFile()->GetModule();
  VariableInfo var_info = GetVariableLocationInfo(*m_index, var_id, module);
  if (!var_info.location || !var_info.ranges)
    return nullptr;

  CompilandIndexItem *cii = m_index->compilands().GetCompiland(var_id.modi);
  CompUnitSP comp_unit_sp = GetOrCreateCompileUnit(*cii);
  TypeSP type_sp = GetOrCreateType(var_info.type);
  std::string name = var_info.name.str();
  Declaration decl;
  SymbolFileTypeSP sftype =
      std::make_shared<SymbolFileType>(*this, type_sp->GetID());

  ValueType var_scope =
      is_param ? eValueTypeVariableArgument : eValueTypeVariableLocal;
  VariableSP var_sp = std::make_shared<Variable>(
      toOpaqueUid(var_id), name.c_str(), name.c_str(), sftype, var_scope,
      comp_unit_sp.get(), *var_info.ranges, &decl, *var_info.location, false,
      false, false);

  if (!is_param)
    m_ast->GetOrCreateVariableDecl(scope_id, var_id);

  m_local_variables[toOpaqueUid(var_id)] = var_sp;
  return var_sp;
}

VariableSP SymbolFileNativePDB::GetOrCreateLocalVariable(
    PdbCompilandSymId scope_id, PdbCompilandSymId var_id, bool is_param) {
  auto iter = m_local_variables.find(toOpaqueUid(var_id));
  if (iter != m_local_variables.end())
    return iter->second;

  return CreateLocalVariable(scope_id, var_id, is_param);
}

TypeSP SymbolFileNativePDB::CreateTypedef(PdbGlobalSymId id) {
  CVSymbol sym = m_index->ReadSymbolRecord(id);
  lldbassert(sym.kind() == SymbolKind::S_UDT);

  UDTSym udt = llvm::cantFail(SymbolDeserializer::deserializeAs<UDTSym>(sym));

  TypeSP target_type = GetOrCreateType(udt.Type);

  (void)m_ast->GetOrCreateTypedefDecl(id);

  Declaration decl;
  return std::make_shared<lldb_private::Type>(
      toOpaqueUid(id), this, ConstString(udt.Name), target_type->GetByteSize(),
      nullptr, target_type->GetID(), lldb_private::Type::eEncodingIsTypedefUID,
      decl, target_type->GetForwardCompilerType(),
      lldb_private::Type::eResolveStateForward);
}

TypeSP SymbolFileNativePDB::GetOrCreateTypedef(PdbGlobalSymId id) {
  auto iter = m_types.find(toOpaqueUid(id));
  if (iter != m_types.end())
    return iter->second;

  return CreateTypedef(id);
}

size_t SymbolFileNativePDB::ParseVariablesForBlock(PdbCompilandSymId block_id) {
  Block &block = GetOrCreateBlock(block_id);

  size_t count = 0;

  CompilandIndexItem *cii = m_index->compilands().GetCompiland(block_id.modi);
  CVSymbol sym = cii->m_debug_stream.readSymbolAtOffset(block_id.offset);
  uint32_t params_remaining = 0;
  switch (sym.kind()) {
  case S_GPROC32:
  case S_LPROC32: {
    ProcSym proc(static_cast<SymbolRecordKind>(sym.kind()));
    cantFail(SymbolDeserializer::deserializeAs<ProcSym>(sym, proc));
    CVType signature = m_index->tpi().getType(proc.FunctionType);
    ProcedureRecord sig;
    cantFail(TypeDeserializer::deserializeAs<ProcedureRecord>(signature, sig));
    params_remaining = sig.getParameterCount();
    break;
  }
  case S_BLOCK32:
    break;
  default:
    lldbassert(false && "Symbol is not a block!");
    return 0;
  }

  VariableListSP variables = block.GetBlockVariableList(false);
  if (!variables) {
    variables = std::make_shared<VariableList>();
    block.SetVariableList(variables);
  }

  CVSymbolArray syms = limitSymbolArrayToScope(
      cii->m_debug_stream.getSymbolArray(), block_id.offset);

  // Skip the first record since it's a PROC32 or BLOCK32, and there's
  // no point examining it since we know it's not a local variable.
  syms.drop_front();
  auto iter = syms.begin();
  auto end = syms.end();

  while (iter != end) {
    uint32_t record_offset = iter.offset();
    CVSymbol variable_cvs = *iter;
    PdbCompilandSymId child_sym_id(block_id.modi, record_offset);
    ++iter;

    // If this is a block, recurse into its children and then skip it.
    if (variable_cvs.kind() == S_BLOCK32) {
      uint32_t block_end = getScopeEndOffset(variable_cvs);
      count += ParseVariablesForBlock(child_sym_id);
      iter = syms.at(block_end);
      continue;
    }

    bool is_param = params_remaining > 0;
    VariableSP variable;
    switch (variable_cvs.kind()) {
    case S_REGREL32:
    case S_REGISTER:
    case S_LOCAL:
      variable = GetOrCreateLocalVariable(block_id, child_sym_id, is_param);
      if (is_param)
        --params_remaining;
      if (variable)
        variables->AddVariableIfUnique(variable);
      break;
    default:
      break;
    }
  }

  // Pass false for set_children, since we call this recursively so that the
  // children will call this for themselves.
  block.SetDidParseVariables(true, false);

  return count;
}

size_t SymbolFileNativePDB::ParseVariablesForContext(const SymbolContext &sc) {
  lldbassert(sc.function || sc.comp_unit);

  VariableListSP variables;
  if (sc.block) {
    PdbSymUid block_id(sc.block->GetID());

    size_t count = ParseVariablesForBlock(block_id.asCompilandSym());
    return count;
  }

  if (sc.function) {
    PdbSymUid block_id(sc.function->GetID());

    size_t count = ParseVariablesForBlock(block_id.asCompilandSym());
    return count;
  }

  if (sc.comp_unit) {
    variables = sc.comp_unit->GetVariableList(false);
    if (!variables) {
      variables = std::make_shared<VariableList>();
      sc.comp_unit->SetVariableList(variables);
    }
    return ParseVariablesForCompileUnit(*sc.comp_unit, *variables);
  }

  llvm_unreachable("Unreachable!");
}

CompilerDecl SymbolFileNativePDB::GetDeclForUID(lldb::user_id_t uid) {
  clang::Decl *decl = m_ast->GetOrCreateDeclForUid(PdbSymUid(uid));

  return m_ast->ToCompilerDecl(*decl);
}

CompilerDeclContext
SymbolFileNativePDB::GetDeclContextForUID(lldb::user_id_t uid) {
  clang::DeclContext *context =
      m_ast->GetOrCreateDeclContextForUid(PdbSymUid(uid));
  if (!context)
    return {};

  return m_ast->ToCompilerDeclContext(*context);
}

CompilerDeclContext
SymbolFileNativePDB::GetDeclContextContainingUID(lldb::user_id_t uid) {
  clang::DeclContext *context = m_ast->GetParentDeclContext(PdbSymUid(uid));
  return m_ast->ToCompilerDeclContext(*context);
}

Type *SymbolFileNativePDB::ResolveTypeUID(lldb::user_id_t type_uid) {
  auto iter = m_types.find(type_uid);
  // lldb should not be passing us non-sensical type uids.  the only way it
  // could have a type uid in the first place is if we handed it out, in which
  // case we should know about the type.  However, that doesn't mean we've
  // instantiated it yet.  We can vend out a UID for a future type.  So if the
  // type doesn't exist, let's instantiate it now.
  if (iter != m_types.end())
    return &*iter->second;

  PdbSymUid uid(type_uid);
  lldbassert(uid.kind() == PdbSymUidKind::Type);
  PdbTypeSymId type_id = uid.asTypeSym();
  if (type_id.index.isNoneType())
    return nullptr;

  TypeSP type_sp = CreateAndCacheType(type_id);
  return &*type_sp;
}

llvm::Optional<SymbolFile::ArrayInfo>
SymbolFileNativePDB::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  return llvm::None;
}


bool SymbolFileNativePDB::CompleteType(CompilerType &compiler_type) {
  clang::QualType qt =
      clang::QualType::getFromOpaquePtr(compiler_type.GetOpaqueQualType());

  return m_ast->CompleteType(qt);
}

size_t SymbolFileNativePDB::GetTypes(lldb_private::SymbolContextScope *sc_scope,
                                     TypeClass type_mask,
                                     lldb_private::TypeList &type_list) {
  return 0;
}

CompilerDeclContext
SymbolFileNativePDB::FindNamespace(const ConstString &name,
                                   const CompilerDeclContext *parent_decl_ctx) {
  return {};
}

TypeSystem *
SymbolFileNativePDB::GetTypeSystemForLanguage(lldb::LanguageType language) {
  auto type_system =
      m_obj_file->GetModule()->GetTypeSystemForLanguage(language);
  if (type_system)
    type_system->SetSymbolFile(this);
  return type_system;
}

ConstString SymbolFileNativePDB::GetPluginName() {
  static ConstString g_name("pdb");
  return g_name;
}

uint32_t SymbolFileNativePDB::GetPluginVersion() { return 1; }
