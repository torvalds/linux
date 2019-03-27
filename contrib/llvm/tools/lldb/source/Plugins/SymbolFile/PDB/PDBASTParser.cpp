//===-- PDBASTParser.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PDBASTParser.h"

#include "SymbolFilePDB.h"

#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/Declaration.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/TypeSystem.h"

#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"

#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm::pdb;

static int TranslateUdtKind(PDB_UdtType pdb_kind) {
  switch (pdb_kind) {
  case PDB_UdtType::Class:
    return clang::TTK_Class;
  case PDB_UdtType::Struct:
    return clang::TTK_Struct;
  case PDB_UdtType::Union:
    return clang::TTK_Union;
  case PDB_UdtType::Interface:
    return clang::TTK_Interface;
  }
  llvm_unreachable("unsuported PDB UDT type");
}

static lldb::Encoding TranslateBuiltinEncoding(PDB_BuiltinType type) {
  switch (type) {
  case PDB_BuiltinType::Float:
    return lldb::eEncodingIEEE754;
  case PDB_BuiltinType::Int:
  case PDB_BuiltinType::Long:
  case PDB_BuiltinType::Char:
    return lldb::eEncodingSint;
  case PDB_BuiltinType::Bool:
  case PDB_BuiltinType::Char16:
  case PDB_BuiltinType::Char32:
  case PDB_BuiltinType::UInt:
  case PDB_BuiltinType::ULong:
  case PDB_BuiltinType::HResult:
  case PDB_BuiltinType::WCharT:
    return lldb::eEncodingUint;
  default:
    return lldb::eEncodingInvalid;
  }
}

static lldb::Encoding TranslateEnumEncoding(PDB_VariantType type) {
  switch (type) {
  case PDB_VariantType::Int8:
  case PDB_VariantType::Int16:
  case PDB_VariantType::Int32:
  case PDB_VariantType::Int64:
    return lldb::eEncodingSint;

  case PDB_VariantType::UInt8:
  case PDB_VariantType::UInt16:
  case PDB_VariantType::UInt32:
  case PDB_VariantType::UInt64:
    return lldb::eEncodingUint;

  default:
    break;
  }

  return lldb::eEncodingSint;
}

static CompilerType
GetBuiltinTypeForPDBEncodingAndBitSize(ClangASTContext &clang_ast,
                                       const PDBSymbolTypeBuiltin &pdb_type,
                                       Encoding encoding, uint32_t width) {
  auto *ast = clang_ast.getASTContext();
  if (!ast)
    return CompilerType();

  switch (pdb_type.getBuiltinType()) {
  default:
    break;
  case PDB_BuiltinType::None:
    return CompilerType();
  case PDB_BuiltinType::Void:
    return clang_ast.GetBasicType(eBasicTypeVoid);
  case PDB_BuiltinType::Char:
    return clang_ast.GetBasicType(eBasicTypeChar);
  case PDB_BuiltinType::Bool:
    return clang_ast.GetBasicType(eBasicTypeBool);
  case PDB_BuiltinType::Long:
    if (width == ast->getTypeSize(ast->LongTy))
      return CompilerType(ast, ast->LongTy);
    if (width == ast->getTypeSize(ast->LongLongTy))
      return CompilerType(ast, ast->LongLongTy);
    break;
  case PDB_BuiltinType::ULong:
    if (width == ast->getTypeSize(ast->UnsignedLongTy))
      return CompilerType(ast, ast->UnsignedLongTy);
    if (width == ast->getTypeSize(ast->UnsignedLongLongTy))
      return CompilerType(ast, ast->UnsignedLongLongTy);
    break;
  case PDB_BuiltinType::WCharT:
    if (width == ast->getTypeSize(ast->WCharTy))
      return CompilerType(ast, ast->WCharTy);
    break;
  case PDB_BuiltinType::Char16:
    return CompilerType(ast, ast->Char16Ty);
  case PDB_BuiltinType::Char32:
    return CompilerType(ast, ast->Char32Ty);
  case PDB_BuiltinType::Float:
    // Note: types `long double` and `double` have same bit size in MSVC and
    // there is no information in the PDB to distinguish them. So when falling
    // back to default search, the compiler type of `long double` will be
    // represented by the one generated for `double`.
    break;
  }
  // If there is no match on PDB_BuiltinType, fall back to default search by
  // encoding and width only
  return clang_ast.GetBuiltinTypeForEncodingAndBitSize(encoding, width);
}

static ConstString GetPDBBuiltinTypeName(const PDBSymbolTypeBuiltin &pdb_type,
                                         CompilerType &compiler_type) {
  PDB_BuiltinType kind = pdb_type.getBuiltinType();
  switch (kind) {
  default:
    break;
  case PDB_BuiltinType::Currency:
    return ConstString("CURRENCY");
  case PDB_BuiltinType::Date:
    return ConstString("DATE");
  case PDB_BuiltinType::Variant:
    return ConstString("VARIANT");
  case PDB_BuiltinType::Complex:
    return ConstString("complex");
  case PDB_BuiltinType::Bitfield:
    return ConstString("bitfield");
  case PDB_BuiltinType::BSTR:
    return ConstString("BSTR");
  case PDB_BuiltinType::HResult:
    return ConstString("HRESULT");
  case PDB_BuiltinType::BCD:
    return ConstString("BCD");
  case PDB_BuiltinType::Char16:
    return ConstString("char16_t");
  case PDB_BuiltinType::Char32:
    return ConstString("char32_t");
  case PDB_BuiltinType::None:
    return ConstString("...");
  }
  return compiler_type.GetTypeName();
}

static bool GetDeclarationForSymbol(const PDBSymbol &symbol,
                                    Declaration &decl) {
  auto &raw_sym = symbol.getRawSymbol();
  auto first_line_up = raw_sym.getSrcLineOnTypeDefn();

  if (!first_line_up) {
    auto lines_up = symbol.getSession().findLineNumbersByAddress(
        raw_sym.getVirtualAddress(), raw_sym.getLength());
    if (!lines_up)
      return false;
    first_line_up = lines_up->getNext();
    if (!first_line_up)
      return false;
  }
  uint32_t src_file_id = first_line_up->getSourceFileId();
  auto src_file_up = symbol.getSession().getSourceFileById(src_file_id);
  if (!src_file_up)
    return false;

  FileSpec spec(src_file_up->getFileName());
  decl.SetFile(spec);
  decl.SetColumn(first_line_up->getColumnNumber());
  decl.SetLine(first_line_up->getLineNumber());
  return true;
}

static AccessType TranslateMemberAccess(PDB_MemberAccess access) {
  switch (access) {
  case PDB_MemberAccess::Private:
    return eAccessPrivate;
  case PDB_MemberAccess::Protected:
    return eAccessProtected;
  case PDB_MemberAccess::Public:
    return eAccessPublic;
  }
  return eAccessNone;
}

static AccessType GetDefaultAccessibilityForUdtKind(PDB_UdtType udt_kind) {
  switch (udt_kind) {
  case PDB_UdtType::Struct:
  case PDB_UdtType::Union:
    return eAccessPublic;
  case PDB_UdtType::Class:
  case PDB_UdtType::Interface:
    return eAccessPrivate;
  }
  llvm_unreachable("unsupported PDB UDT type");
}

static AccessType GetAccessibilityForUdt(const PDBSymbolTypeUDT &udt) {
  AccessType access = TranslateMemberAccess(udt.getAccess());
  if (access != lldb::eAccessNone || !udt.isNested())
    return access;

  auto parent = udt.getClassParent();
  if (!parent)
    return lldb::eAccessNone;

  auto parent_udt = llvm::dyn_cast<PDBSymbolTypeUDT>(parent.get());
  if (!parent_udt)
    return lldb::eAccessNone;

  return GetDefaultAccessibilityForUdtKind(parent_udt->getUdtKind());
}

static clang::MSInheritanceAttr::Spelling
GetMSInheritance(const PDBSymbolTypeUDT &udt) {
  int base_count = 0;
  bool has_virtual = false;

  auto bases_enum = udt.findAllChildren<PDBSymbolTypeBaseClass>();
  if (bases_enum) {
    while (auto base = bases_enum->getNext()) {
      base_count++;
      has_virtual |= base->isVirtualBaseClass();
    }
  }

  if (has_virtual)
    return clang::MSInheritanceAttr::Keyword_virtual_inheritance;
  if (base_count > 1)
    return clang::MSInheritanceAttr::Keyword_multiple_inheritance;
  return clang::MSInheritanceAttr::Keyword_single_inheritance;
}

static std::unique_ptr<llvm::pdb::PDBSymbol>
GetClassOrFunctionParent(const llvm::pdb::PDBSymbol &symbol) {
  const IPDBSession &session = symbol.getSession();
  const IPDBRawSymbol &raw = symbol.getRawSymbol();
  auto tag = symbol.getSymTag();

  // For items that are nested inside of a class, return the class that it is
  // nested inside of.
  // Note that only certain items can be nested inside of classes.
  switch (tag) {
  case PDB_SymType::Function:
  case PDB_SymType::Data:
  case PDB_SymType::UDT:
  case PDB_SymType::Enum:
  case PDB_SymType::FunctionSig:
  case PDB_SymType::Typedef:
  case PDB_SymType::BaseClass:
  case PDB_SymType::VTable: {
    auto class_parent_id = raw.getClassParentId();
    if (auto class_parent = session.getSymbolById(class_parent_id))
      return class_parent;
    break;
  }
  default:
    break;
  }

  // Otherwise, if it is nested inside of a function, return the function.
  // Note that only certain items can be nested inside of functions.
  switch (tag) {
  case PDB_SymType::Block:
  case PDB_SymType::Data: {
    auto lexical_parent_id = raw.getLexicalParentId();
    auto lexical_parent = session.getSymbolById(lexical_parent_id);
    if (!lexical_parent)
      return nullptr;

    auto lexical_parent_tag = lexical_parent->getSymTag();
    if (lexical_parent_tag == PDB_SymType::Function)
      return lexical_parent;
    if (lexical_parent_tag == PDB_SymType::Exe)
      return nullptr;

    return GetClassOrFunctionParent(*lexical_parent);
  }
  default:
    return nullptr;
  }
}

static clang::NamedDecl *
GetDeclFromContextByName(const clang::ASTContext &ast,
                         const clang::DeclContext &decl_context,
                         llvm::StringRef name) {
  clang::IdentifierInfo &ident = ast.Idents.get(name);
  clang::DeclarationName decl_name = ast.DeclarationNames.getIdentifier(&ident);
  clang::DeclContext::lookup_result result = decl_context.lookup(decl_name);
  if (result.empty())
    return nullptr;

  return result[0];
}

static bool IsAnonymousNamespaceName(llvm::StringRef name) {
  return name == "`anonymous namespace'" || name == "`anonymous-namespace'";
}

static clang::CallingConv TranslateCallingConvention(PDB_CallingConv pdb_cc) {
  switch (pdb_cc) {
  case llvm::codeview::CallingConvention::NearC:
    return clang::CC_C;
  case llvm::codeview::CallingConvention::NearStdCall:
    return clang::CC_X86StdCall;
  case llvm::codeview::CallingConvention::NearFast:
    return clang::CC_X86FastCall;
  case llvm::codeview::CallingConvention::ThisCall:
    return clang::CC_X86ThisCall;
  case llvm::codeview::CallingConvention::NearVector:
    return clang::CC_X86VectorCall;
  case llvm::codeview::CallingConvention::NearPascal:
    return clang::CC_X86Pascal;
  default:
    assert(false && "Unknown calling convention");
    return clang::CC_C;
  }
}

PDBASTParser::PDBASTParser(lldb_private::ClangASTContext &ast) : m_ast(ast) {}

PDBASTParser::~PDBASTParser() {}

// DebugInfoASTParser interface

lldb::TypeSP PDBASTParser::CreateLLDBTypeFromPDBType(const PDBSymbol &type) {
  Declaration decl;
  switch (type.getSymTag()) {
  case PDB_SymType::BaseClass: {
    auto symbol_file = m_ast.GetSymbolFile();
    if (!symbol_file)
      return nullptr;

    auto ty = symbol_file->ResolveTypeUID(type.getRawSymbol().getTypeId());
    return ty ? ty->shared_from_this() : nullptr;
  } break;
  case PDB_SymType::UDT: {
    auto udt = llvm::dyn_cast<PDBSymbolTypeUDT>(&type);
    assert(udt);

    // Note that, unnamed UDT being typedef-ed is generated as a UDT symbol
    // other than a Typedef symbol in PDB. For example,
    //    typedef union { short Row; short Col; } Union;
    // is generated as a named UDT in PDB:
    //    union Union { short Row; short Col; }
    // Such symbols will be handled here.

    // Some UDT with trival ctor has zero length. Just ignore.
    if (udt->getLength() == 0)
      return nullptr;

    // Ignore unnamed-tag UDTs.
    std::string name = MSVCUndecoratedNameParser::DropScope(udt->getName());
    if (name.empty())
      return nullptr;

    auto decl_context = GetDeclContextContainingSymbol(type);

    // Check if such an UDT already exists in the current context.
    // This may occur with const or volatile types. There are separate type
    // symbols in PDB for types with const or volatile modifiers, but we need
    // to create only one declaration for them all.
    Type::ResolveStateTag type_resolve_state_tag;
    CompilerType clang_type = m_ast.GetTypeForIdentifier<clang::CXXRecordDecl>(
        ConstString(name), decl_context);
    if (!clang_type.IsValid()) {
      auto access = GetAccessibilityForUdt(*udt);

      auto tag_type_kind = TranslateUdtKind(udt->getUdtKind());

      ClangASTMetadata metadata;
      metadata.SetUserID(type.getSymIndexId());
      metadata.SetIsDynamicCXXType(false);

      clang_type = m_ast.CreateRecordType(
          decl_context, access, name.c_str(), tag_type_kind,
          lldb::eLanguageTypeC_plus_plus, &metadata);
      assert(clang_type.IsValid());

      auto record_decl =
          m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
      assert(record_decl);
      m_uid_to_decl[type.getSymIndexId()] = record_decl;

      auto inheritance_attr = clang::MSInheritanceAttr::CreateImplicit(
          *m_ast.getASTContext(), GetMSInheritance(*udt));
      record_decl->addAttr(inheritance_attr);

      ClangASTContext::StartTagDeclarationDefinition(clang_type);

      auto children = udt->findAllChildren();
      if (!children || children->getChildCount() == 0) {
        // PDB does not have symbol of forwarder. We assume we get an udt w/o
        // any fields. Just complete it at this point.
        ClangASTContext::CompleteTagDeclarationDefinition(clang_type);

        ClangASTContext::SetHasExternalStorage(clang_type.GetOpaqueQualType(),
                                               false);

        type_resolve_state_tag = Type::eResolveStateFull;
      } else {
        // Add the type to the forward declarations. It will help us to avoid
        // an endless recursion in CompleteTypeFromUdt function.
        m_forward_decl_to_uid[record_decl] = type.getSymIndexId();

        ClangASTContext::SetHasExternalStorage(clang_type.GetOpaqueQualType(),
                                               true);

        type_resolve_state_tag = Type::eResolveStateForward;
      }
    } else
      type_resolve_state_tag = Type::eResolveStateForward;

    if (udt->isConstType())
      clang_type = clang_type.AddConstModifier();

    if (udt->isVolatileType())
      clang_type = clang_type.AddVolatileModifier();

    GetDeclarationForSymbol(type, decl);
    return std::make_shared<lldb_private::Type>(
        type.getSymIndexId(), m_ast.GetSymbolFile(), ConstString(name),
        udt->getLength(), nullptr, LLDB_INVALID_UID,
        lldb_private::Type::eEncodingIsUID, decl, clang_type,
        type_resolve_state_tag);
  } break;
  case PDB_SymType::Enum: {
    auto enum_type = llvm::dyn_cast<PDBSymbolTypeEnum>(&type);
    assert(enum_type);

    std::string name =
        MSVCUndecoratedNameParser::DropScope(enum_type->getName());
    auto decl_context = GetDeclContextContainingSymbol(type);
    uint64_t bytes = enum_type->getLength();

    // Check if such an enum already exists in the current context
    CompilerType ast_enum = m_ast.GetTypeForIdentifier<clang::EnumDecl>(
        ConstString(name), decl_context);
    if (!ast_enum.IsValid()) {
      auto underlying_type_up = enum_type->getUnderlyingType();
      if (!underlying_type_up)
        return nullptr;

      lldb::Encoding encoding =
          TranslateBuiltinEncoding(underlying_type_up->getBuiltinType());
      // FIXME: Type of underlying builtin is always `Int`. We correct it with
      // the very first enumerator's encoding if any.
      auto first_child = enum_type->findOneChild<PDBSymbolData>();
      if (first_child)
        encoding = TranslateEnumEncoding(first_child->getValue().Type);

      CompilerType builtin_type;
      if (bytes > 0)
        builtin_type = GetBuiltinTypeForPDBEncodingAndBitSize(
            m_ast, *underlying_type_up, encoding, bytes * 8);
      else
        builtin_type = m_ast.GetBasicType(eBasicTypeInt);

      // FIXME: PDB does not have information about scoped enumeration (Enum
      // Class). Set it false for now.
      bool isScoped = false;

      ast_enum = m_ast.CreateEnumerationType(name.c_str(), decl_context, decl,
                                             builtin_type, isScoped);

      auto enum_decl = ClangASTContext::GetAsEnumDecl(ast_enum);
      assert(enum_decl);
      m_uid_to_decl[type.getSymIndexId()] = enum_decl;

      auto enum_values = enum_type->findAllChildren<PDBSymbolData>();
      if (enum_values) {
        while (auto enum_value = enum_values->getNext()) {
          if (enum_value->getDataKind() != PDB_DataKind::Constant)
            continue;
          AddEnumValue(ast_enum, *enum_value);
        }
      }

      if (ClangASTContext::StartTagDeclarationDefinition(ast_enum))
        ClangASTContext::CompleteTagDeclarationDefinition(ast_enum);
    }

    if (enum_type->isConstType())
      ast_enum = ast_enum.AddConstModifier();

    if (enum_type->isVolatileType())
      ast_enum = ast_enum.AddVolatileModifier();

    GetDeclarationForSymbol(type, decl);
    return std::make_shared<lldb_private::Type>(
        type.getSymIndexId(), m_ast.GetSymbolFile(), ConstString(name), bytes,
        nullptr, LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID, decl,
        ast_enum, lldb_private::Type::eResolveStateFull);
  } break;
  case PDB_SymType::Typedef: {
    auto type_def = llvm::dyn_cast<PDBSymbolTypeTypedef>(&type);
    assert(type_def);

    lldb_private::Type *target_type =
        m_ast.GetSymbolFile()->ResolveTypeUID(type_def->getTypeId());
    if (!target_type)
      return nullptr;

    std::string name =
        MSVCUndecoratedNameParser::DropScope(type_def->getName());
    auto decl_ctx = GetDeclContextContainingSymbol(type);

    // Check if such a typedef already exists in the current context
    CompilerType ast_typedef =
        m_ast.GetTypeForIdentifier<clang::TypedefNameDecl>(ConstString(name),
                                                           decl_ctx);
    if (!ast_typedef.IsValid()) {
      CompilerType target_ast_type = target_type->GetFullCompilerType();

      ast_typedef = m_ast.CreateTypedefType(
          target_ast_type, name.c_str(), CompilerDeclContext(&m_ast, decl_ctx));
      if (!ast_typedef)
        return nullptr;

      auto typedef_decl = ClangASTContext::GetAsTypedefDecl(ast_typedef);
      assert(typedef_decl);
      m_uid_to_decl[type.getSymIndexId()] = typedef_decl;
    }

    if (type_def->isConstType())
      ast_typedef = ast_typedef.AddConstModifier();

    if (type_def->isVolatileType())
      ast_typedef = ast_typedef.AddVolatileModifier();

    GetDeclarationForSymbol(type, decl);
    return std::make_shared<lldb_private::Type>(
        type_def->getSymIndexId(), m_ast.GetSymbolFile(), ConstString(name),
        type_def->getLength(), nullptr, target_type->GetID(),
        lldb_private::Type::eEncodingIsTypedefUID, decl, ast_typedef,
        lldb_private::Type::eResolveStateFull);
  } break;
  case PDB_SymType::Function:
  case PDB_SymType::FunctionSig: {
    std::string name;
    PDBSymbolTypeFunctionSig *func_sig = nullptr;
    if (auto pdb_func = llvm::dyn_cast<PDBSymbolFunc>(&type)) {
      if (pdb_func->isCompilerGenerated())
        return nullptr;

      auto sig = pdb_func->getSignature();
      if (!sig)
        return nullptr;
      func_sig = sig.release();
      // Function type is named.
      name = MSVCUndecoratedNameParser::DropScope(pdb_func->getName());
    } else if (auto pdb_func_sig =
                   llvm::dyn_cast<PDBSymbolTypeFunctionSig>(&type)) {
      func_sig = const_cast<PDBSymbolTypeFunctionSig *>(pdb_func_sig);
    } else
      llvm_unreachable("Unexpected PDB symbol!");

    auto arg_enum = func_sig->getArguments();
    uint32_t num_args = arg_enum->getChildCount();
    std::vector<CompilerType> arg_list;

    bool is_variadic = func_sig->isCVarArgs();
    // Drop last variadic argument.
    if (is_variadic)
      --num_args;
    for (uint32_t arg_idx = 0; arg_idx < num_args; arg_idx++) {
      auto arg = arg_enum->getChildAtIndex(arg_idx);
      if (!arg)
        break;
      lldb_private::Type *arg_type =
          m_ast.GetSymbolFile()->ResolveTypeUID(arg->getSymIndexId());
      // If there's some error looking up one of the dependent types of this
      // function signature, bail.
      if (!arg_type)
        return nullptr;
      CompilerType arg_ast_type = arg_type->GetFullCompilerType();
      arg_list.push_back(arg_ast_type);
    }
    lldbassert(arg_list.size() <= num_args);

    auto pdb_return_type = func_sig->getReturnType();
    lldb_private::Type *return_type =
        m_ast.GetSymbolFile()->ResolveTypeUID(pdb_return_type->getSymIndexId());
    // If there's some error looking up one of the dependent types of this
    // function signature, bail.
    if (!return_type)
      return nullptr;
    CompilerType return_ast_type = return_type->GetFullCompilerType();
    uint32_t type_quals = 0;
    if (func_sig->isConstType())
      type_quals |= clang::Qualifiers::Const;
    if (func_sig->isVolatileType())
      type_quals |= clang::Qualifiers::Volatile;
    auto cc = TranslateCallingConvention(func_sig->getCallingConvention());
    CompilerType func_sig_ast_type =
        m_ast.CreateFunctionType(return_ast_type, arg_list.data(),
                                 arg_list.size(), is_variadic, type_quals, cc);

    GetDeclarationForSymbol(type, decl);
    return std::make_shared<lldb_private::Type>(
        type.getSymIndexId(), m_ast.GetSymbolFile(), ConstString(name), 0,
        nullptr, LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID, decl,
        func_sig_ast_type, lldb_private::Type::eResolveStateFull);
  } break;
  case PDB_SymType::ArrayType: {
    auto array_type = llvm::dyn_cast<PDBSymbolTypeArray>(&type);
    assert(array_type);
    uint32_t num_elements = array_type->getCount();
    uint32_t element_uid = array_type->getElementTypeId();
    uint32_t bytes = array_type->getLength();

    // If array rank > 0, PDB gives the element type at N=0. So element type
    // will parsed in the order N=0, N=1,..., N=rank sequentially.
    lldb_private::Type *element_type =
        m_ast.GetSymbolFile()->ResolveTypeUID(element_uid);
    if (!element_type)
      return nullptr;

    CompilerType element_ast_type = element_type->GetForwardCompilerType();
    // If element type is UDT, it needs to be complete.
    if (ClangASTContext::IsCXXClassType(element_ast_type) &&
        !element_ast_type.GetCompleteType()) {
      if (ClangASTContext::StartTagDeclarationDefinition(element_ast_type)) {
        ClangASTContext::CompleteTagDeclarationDefinition(element_ast_type);
      } else {
        // We are not able to start defintion.
        return nullptr;
      }
    }
    CompilerType array_ast_type = m_ast.CreateArrayType(
        element_ast_type, num_elements, /*is_gnu_vector*/ false);
    TypeSP type_sp = std::make_shared<lldb_private::Type>(
        array_type->getSymIndexId(), m_ast.GetSymbolFile(), ConstString(),
        bytes, nullptr, LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID,
        decl, array_ast_type, lldb_private::Type::eResolveStateFull);
    type_sp->SetEncodingType(element_type);
    return type_sp;
  } break;
  case PDB_SymType::BuiltinType: {
    auto *builtin_type = llvm::dyn_cast<PDBSymbolTypeBuiltin>(&type);
    assert(builtin_type);
    PDB_BuiltinType builtin_kind = builtin_type->getBuiltinType();
    if (builtin_kind == PDB_BuiltinType::None)
      return nullptr;

    uint64_t bytes = builtin_type->getLength();
    Encoding encoding = TranslateBuiltinEncoding(builtin_kind);
    CompilerType builtin_ast_type = GetBuiltinTypeForPDBEncodingAndBitSize(
        m_ast, *builtin_type, encoding, bytes * 8);

    if (builtin_type->isConstType())
      builtin_ast_type = builtin_ast_type.AddConstModifier();

    if (builtin_type->isVolatileType())
      builtin_ast_type = builtin_ast_type.AddVolatileModifier();

    auto type_name = GetPDBBuiltinTypeName(*builtin_type, builtin_ast_type);

    return std::make_shared<lldb_private::Type>(
        builtin_type->getSymIndexId(), m_ast.GetSymbolFile(), type_name, bytes,
        nullptr, LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID, decl,
        builtin_ast_type, lldb_private::Type::eResolveStateFull);
  } break;
  case PDB_SymType::PointerType: {
    auto *pointer_type = llvm::dyn_cast<PDBSymbolTypePointer>(&type);
    assert(pointer_type);
    Type *pointee_type = m_ast.GetSymbolFile()->ResolveTypeUID(
        pointer_type->getPointeeType()->getSymIndexId());
    if (!pointee_type)
      return nullptr;

    if (pointer_type->isPointerToDataMember() ||
        pointer_type->isPointerToMemberFunction()) {
      auto class_parent_uid = pointer_type->getRawSymbol().getClassParentId();
      auto class_parent_type =
          m_ast.GetSymbolFile()->ResolveTypeUID(class_parent_uid);
      assert(class_parent_type);

      CompilerType pointer_ast_type;
      pointer_ast_type = ClangASTContext::CreateMemberPointerType(
          class_parent_type->GetLayoutCompilerType(),
          pointee_type->GetForwardCompilerType());
      assert(pointer_ast_type);

      return std::make_shared<lldb_private::Type>(
          pointer_type->getSymIndexId(), m_ast.GetSymbolFile(), ConstString(),
          pointer_type->getLength(), nullptr, LLDB_INVALID_UID,
          lldb_private::Type::eEncodingIsUID, decl, pointer_ast_type,
          lldb_private::Type::eResolveStateForward);
    }

    CompilerType pointer_ast_type;
    pointer_ast_type = pointee_type->GetFullCompilerType();
    if (pointer_type->isReference())
      pointer_ast_type = pointer_ast_type.GetLValueReferenceType();
    else if (pointer_type->isRValueReference())
      pointer_ast_type = pointer_ast_type.GetRValueReferenceType();
    else
      pointer_ast_type = pointer_ast_type.GetPointerType();

    if (pointer_type->isConstType())
      pointer_ast_type = pointer_ast_type.AddConstModifier();

    if (pointer_type->isVolatileType())
      pointer_ast_type = pointer_ast_type.AddVolatileModifier();

    if (pointer_type->isRestrictedType())
      pointer_ast_type = pointer_ast_type.AddRestrictModifier();

    return std::make_shared<lldb_private::Type>(
        pointer_type->getSymIndexId(), m_ast.GetSymbolFile(), ConstString(),
        pointer_type->getLength(), nullptr, LLDB_INVALID_UID,
        lldb_private::Type::eEncodingIsUID, decl, pointer_ast_type,
        lldb_private::Type::eResolveStateFull);
  } break;
  default:
    break;
  }
  return nullptr;
}

bool PDBASTParser::CompleteTypeFromPDB(
    lldb_private::CompilerType &compiler_type) {
  if (GetClangASTImporter().CanImport(compiler_type))
    return GetClangASTImporter().CompleteType(compiler_type);

  // Remove the type from the forward declarations to avoid
  // an endless recursion for types like a linked list.
  clang::CXXRecordDecl *record_decl =
      m_ast.GetAsCXXRecordDecl(compiler_type.GetOpaqueQualType());
  auto uid_it = m_forward_decl_to_uid.find(record_decl);
  if (uid_it == m_forward_decl_to_uid.end())
    return true;

  auto symbol_file = static_cast<SymbolFilePDB *>(m_ast.GetSymbolFile());
  if (!symbol_file)
    return false;

  std::unique_ptr<PDBSymbol> symbol =
      symbol_file->GetPDBSession().getSymbolById(uid_it->getSecond());
  if (!symbol)
    return false;

  m_forward_decl_to_uid.erase(uid_it);

  ClangASTContext::SetHasExternalStorage(compiler_type.GetOpaqueQualType(),
                                         false);

  switch (symbol->getSymTag()) {
  case PDB_SymType::UDT: {
    auto udt = llvm::dyn_cast<PDBSymbolTypeUDT>(symbol.get());
    if (!udt)
      return false;

    return CompleteTypeFromUDT(*symbol_file, compiler_type, *udt);
  }
  default:
    llvm_unreachable("not a forward clang type decl!");
  }
}

clang::Decl *
PDBASTParser::GetDeclForSymbol(const llvm::pdb::PDBSymbol &symbol) {
  uint32_t sym_id = symbol.getSymIndexId();
  auto it = m_uid_to_decl.find(sym_id);
  if (it != m_uid_to_decl.end())
    return it->second;

  auto symbol_file = static_cast<SymbolFilePDB *>(m_ast.GetSymbolFile());
  if (!symbol_file)
    return nullptr;

  // First of all, check if the symbol is a member of a class. Resolve the full
  // class type and return the declaration from the cache if so.
  auto tag = symbol.getSymTag();
  if (tag == PDB_SymType::Data || tag == PDB_SymType::Function) {
    const IPDBSession &session = symbol.getSession();
    const IPDBRawSymbol &raw = symbol.getRawSymbol();

    auto class_parent_id = raw.getClassParentId();
    if (std::unique_ptr<PDBSymbol> class_parent =
            session.getSymbolById(class_parent_id)) {
      auto class_parent_type = symbol_file->ResolveTypeUID(class_parent_id);
      if (!class_parent_type)
        return nullptr;

      CompilerType class_parent_ct = class_parent_type->GetFullCompilerType();

      // Look a declaration up in the cache after completing the class
      clang::Decl *decl = m_uid_to_decl.lookup(sym_id);
      if (decl)
        return decl;

      // A declaration was not found in the cache. It means that the symbol
      // has the class parent, but the class doesn't have the symbol in its
      // children list.
      if (auto func = llvm::dyn_cast_or_null<PDBSymbolFunc>(&symbol)) {
        // Try to find a class child method with the same RVA and use its
        // declaration if found.
        if (uint32_t rva = func->getRelativeVirtualAddress()) {
          if (std::unique_ptr<ConcreteSymbolEnumerator<PDBSymbolFunc>>
                  methods_enum =
                      class_parent->findAllChildren<PDBSymbolFunc>()) {
            while (std::unique_ptr<PDBSymbolFunc> method =
                       methods_enum->getNext()) {
              if (method->getRelativeVirtualAddress() == rva) {
                decl = m_uid_to_decl.lookup(method->getSymIndexId());
                if (decl)
                  break;
              }
            }
          }
        }

        // If no class methods with the same RVA were found, then create a new
        // method. It is possible for template methods.
        if (!decl)
          decl = AddRecordMethod(*symbol_file, class_parent_ct, *func);
      }

      if (decl)
        m_uid_to_decl[sym_id] = decl;

      return decl;
    }
  }

  // If we are here, then the symbol is not belonging to a class and is not
  // contained in the cache. So create a declaration for it.
  switch (symbol.getSymTag()) {
  case PDB_SymType::Data: {
    auto data = llvm::dyn_cast<PDBSymbolData>(&symbol);
    assert(data);

    auto decl_context = GetDeclContextContainingSymbol(symbol);
    assert(decl_context);

    // May be the current context is a class really, but we haven't found
    // any class parent. This happens e.g. in the case of class static
    // variables - they has two symbols, one is a child of the class when
    // another is a child of the exe. So always complete the parent and use
    // an existing declaration if possible.
    if (auto parent_decl = llvm::dyn_cast_or_null<clang::TagDecl>(decl_context))
      m_ast.GetCompleteDecl(parent_decl);

    std::string name = MSVCUndecoratedNameParser::DropScope(data->getName());

    // Check if the current context already contains the symbol with the name.
    clang::Decl *decl =
        GetDeclFromContextByName(*m_ast.getASTContext(), *decl_context, name);
    if (!decl) {
      auto type = symbol_file->ResolveTypeUID(data->getTypeId());
      if (!type)
        return nullptr;

      decl = m_ast.CreateVariableDeclaration(
          decl_context, name.c_str(),
          ClangUtil::GetQualType(type->GetLayoutCompilerType()));
    }

    m_uid_to_decl[sym_id] = decl;

    return decl;
  }
  case PDB_SymType::Function: {
    auto func = llvm::dyn_cast<PDBSymbolFunc>(&symbol);
    assert(func);

    auto decl_context = GetDeclContextContainingSymbol(symbol);
    assert(decl_context);

    std::string name = MSVCUndecoratedNameParser::DropScope(func->getName());

    Type *type = symbol_file->ResolveTypeUID(sym_id);
    if (!type)
      return nullptr;

    auto storage = func->isStatic() ? clang::StorageClass::SC_Static
                                    : clang::StorageClass::SC_None;

    auto decl = m_ast.CreateFunctionDeclaration(
        decl_context, name.c_str(), type->GetForwardCompilerType(), storage,
        func->hasInlineAttribute());

    std::vector<clang::ParmVarDecl *> params;
    if (std::unique_ptr<PDBSymbolTypeFunctionSig> sig = func->getSignature()) {
      if (std::unique_ptr<ConcreteSymbolEnumerator<PDBSymbolTypeFunctionArg>>
              arg_enum = sig->findAllChildren<PDBSymbolTypeFunctionArg>()) {
        while (std::unique_ptr<PDBSymbolTypeFunctionArg> arg =
                   arg_enum->getNext()) {
          Type *arg_type = symbol_file->ResolveTypeUID(arg->getTypeId());
          if (!arg_type)
            continue;

          clang::ParmVarDecl *param = m_ast.CreateParameterDeclaration(
              decl, nullptr, arg_type->GetForwardCompilerType(),
              clang::SC_None);
          if (param)
            params.push_back(param);
        }
      }
    }
    if (params.size())
      m_ast.SetFunctionParameters(decl, params.data(), params.size());

    m_uid_to_decl[sym_id] = decl;

    return decl;
  }
  default: {
    // It's not a variable and not a function, check if it's a type
    Type *type = symbol_file->ResolveTypeUID(sym_id);
    if (!type)
      return nullptr;

    return m_uid_to_decl.lookup(sym_id);
  }
  }
}

clang::DeclContext *
PDBASTParser::GetDeclContextForSymbol(const llvm::pdb::PDBSymbol &symbol) {
  if (symbol.getSymTag() == PDB_SymType::Function) {
    clang::DeclContext *result =
        llvm::dyn_cast_or_null<clang::FunctionDecl>(GetDeclForSymbol(symbol));

    if (result)
      m_decl_context_to_uid[result] = symbol.getSymIndexId();

    return result;
  }

  auto symbol_file = static_cast<SymbolFilePDB *>(m_ast.GetSymbolFile());
  if (!symbol_file)
    return nullptr;

  auto type = symbol_file->ResolveTypeUID(symbol.getSymIndexId());
  if (!type)
    return nullptr;

  clang::DeclContext *result =
      m_ast.GetDeclContextForType(type->GetForwardCompilerType());

  if (result)
    m_decl_context_to_uid[result] = symbol.getSymIndexId();

  return result;
}

clang::DeclContext *PDBASTParser::GetDeclContextContainingSymbol(
    const llvm::pdb::PDBSymbol &symbol) {
  auto parent = GetClassOrFunctionParent(symbol);
  while (parent) {
    if (auto parent_context = GetDeclContextForSymbol(*parent))
      return parent_context;

    parent = GetClassOrFunctionParent(*parent);
  }

  // We can't find any class or function parent of the symbol. So analyze
  // the full symbol name. The symbol may be belonging to a namespace
  // or function (or even to a class if it's e.g. a static variable symbol).

  // TODO: Make clang to emit full names for variables in namespaces
  // (as MSVC does)

  std::string name(symbol.getRawSymbol().getName());
  MSVCUndecoratedNameParser parser(name);
  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();
  if (specs.empty())
    return m_ast.GetTranslationUnitDecl();

  auto symbol_file = static_cast<SymbolFilePDB *>(m_ast.GetSymbolFile());
  if (!symbol_file)
    return m_ast.GetTranslationUnitDecl();

  auto global = symbol_file->GetPDBSession().getGlobalScope();
  if (!global)
    return m_ast.GetTranslationUnitDecl();

  bool has_type_or_function_parent = false;
  clang::DeclContext *curr_context = m_ast.GetTranslationUnitDecl();
  for (std::size_t i = 0; i < specs.size() - 1; i++) {
    // Check if there is a function or a type with the current context's name.
    if (std::unique_ptr<IPDBEnumSymbols> children_enum = global->findChildren(
            PDB_SymType::None, specs[i].GetFullName(), NS_CaseSensitive)) {
      while (IPDBEnumChildren<PDBSymbol>::ChildTypePtr child =
                 children_enum->getNext()) {
        if (clang::DeclContext *child_context =
                GetDeclContextForSymbol(*child)) {
          // Note that `GetDeclContextForSymbol' retrieves
          // a declaration context for functions and types only,
          // so if we are here then `child_context' is guaranteed
          // a function or a type declaration context.
          has_type_or_function_parent = true;
          curr_context = child_context;
        }
      }
    }

    // If there were no functions or types above then retrieve a namespace with
    // the current context's name. There can be no namespaces inside a function
    // or a type. We check it to avoid fake namespaces such as `__l2':
    // `N0::N1::CClass::PrivateFunc::__l2::InnerFuncStruct'
    if (!has_type_or_function_parent) {
      std::string namespace_name = specs[i].GetBaseName();
      const char *namespace_name_c_str =
          IsAnonymousNamespaceName(namespace_name) ? nullptr
                                                   : namespace_name.data();
      clang::NamespaceDecl *namespace_decl =
          m_ast.GetUniqueNamespaceDeclaration(namespace_name_c_str,
                                              curr_context);

      m_parent_to_namespaces[curr_context].insert(namespace_decl);
      m_namespaces.insert(namespace_decl);

      curr_context = namespace_decl;
    }
  }

  return curr_context;
}

void PDBASTParser::ParseDeclsForDeclContext(
    const clang::DeclContext *decl_context) {
  auto symbol_file = static_cast<SymbolFilePDB *>(m_ast.GetSymbolFile());
  if (!symbol_file)
    return;

  IPDBSession &session = symbol_file->GetPDBSession();
  auto symbol_up =
      session.getSymbolById(m_decl_context_to_uid.lookup(decl_context));
  auto global_up = session.getGlobalScope();

  PDBSymbol *symbol;
  if (symbol_up)
    symbol = symbol_up.get();
  else if (global_up)
    symbol = global_up.get();
  else
    return;

  if (auto children = symbol->findAllChildren())
    while (auto child = children->getNext())
      GetDeclForSymbol(*child);
}

clang::NamespaceDecl *
PDBASTParser::FindNamespaceDecl(const clang::DeclContext *parent,
                                llvm::StringRef name) {
  NamespacesSet *set;
  if (parent) {
    auto pit = m_parent_to_namespaces.find(parent);
    if (pit == m_parent_to_namespaces.end())
      return nullptr;

    set = &pit->second;
  } else {
    set = &m_namespaces;
  }
  assert(set);

  for (clang::NamespaceDecl *namespace_decl : *set)
    if (namespace_decl->getName().equals(name))
      return namespace_decl;

  for (clang::NamespaceDecl *namespace_decl : *set)
    if (namespace_decl->isAnonymousNamespace())
      return FindNamespaceDecl(namespace_decl, name);

  return nullptr;
}

bool PDBASTParser::AddEnumValue(CompilerType enum_type,
                                const PDBSymbolData &enum_value) {
  Declaration decl;
  Variant v = enum_value.getValue();
  std::string name = MSVCUndecoratedNameParser::DropScope(enum_value.getName());
  int64_t raw_value;
  switch (v.Type) {
  case PDB_VariantType::Int8:
    raw_value = v.Value.Int8;
    break;
  case PDB_VariantType::Int16:
    raw_value = v.Value.Int16;
    break;
  case PDB_VariantType::Int32:
    raw_value = v.Value.Int32;
    break;
  case PDB_VariantType::Int64:
    raw_value = v.Value.Int64;
    break;
  case PDB_VariantType::UInt8:
    raw_value = v.Value.UInt8;
    break;
  case PDB_VariantType::UInt16:
    raw_value = v.Value.UInt16;
    break;
  case PDB_VariantType::UInt32:
    raw_value = v.Value.UInt32;
    break;
  case PDB_VariantType::UInt64:
    raw_value = v.Value.UInt64;
    break;
  default:
    return false;
  }
  CompilerType underlying_type =
      m_ast.GetEnumerationIntegerType(enum_type.GetOpaqueQualType());
  uint32_t byte_size = m_ast.getASTContext()->getTypeSize(
      ClangUtil::GetQualType(underlying_type));
  auto enum_constant_decl = m_ast.AddEnumerationValueToEnumerationType(
      enum_type, decl, name.c_str(), raw_value, byte_size * 8);
  if (!enum_constant_decl)
    return false;

  m_uid_to_decl[enum_value.getSymIndexId()] = enum_constant_decl;

  return true;
}

bool PDBASTParser::CompleteTypeFromUDT(
    lldb_private::SymbolFile &symbol_file,
    lldb_private::CompilerType &compiler_type,
    llvm::pdb::PDBSymbolTypeUDT &udt) {
  ClangASTImporter::LayoutInfo layout_info;
  layout_info.bit_size = udt.getLength() * 8;

  auto nested_enums = udt.findAllChildren<PDBSymbolTypeUDT>();
  if (nested_enums)
    while (auto nested = nested_enums->getNext())
      symbol_file.ResolveTypeUID(nested->getSymIndexId());

  auto bases_enum = udt.findAllChildren<PDBSymbolTypeBaseClass>();
  if (bases_enum)
    AddRecordBases(symbol_file, compiler_type,
                   TranslateUdtKind(udt.getUdtKind()), *bases_enum,
                   layout_info);

  auto members_enum = udt.findAllChildren<PDBSymbolData>();
  if (members_enum)
    AddRecordMembers(symbol_file, compiler_type, *members_enum, layout_info);

  auto methods_enum = udt.findAllChildren<PDBSymbolFunc>();
  if (methods_enum)
    AddRecordMethods(symbol_file, compiler_type, *methods_enum);

  m_ast.AddMethodOverridesForCXXRecordType(compiler_type.GetOpaqueQualType());
  ClangASTContext::BuildIndirectFields(compiler_type);
  ClangASTContext::CompleteTagDeclarationDefinition(compiler_type);

  clang::CXXRecordDecl *record_decl =
      m_ast.GetAsCXXRecordDecl(compiler_type.GetOpaqueQualType());
  if (!record_decl)
    return static_cast<bool>(compiler_type);

  GetClangASTImporter().InsertRecordDecl(record_decl, layout_info);

  return static_cast<bool>(compiler_type);
}

void PDBASTParser::AddRecordMembers(
    lldb_private::SymbolFile &symbol_file,
    lldb_private::CompilerType &record_type,
    PDBDataSymbolEnumerator &members_enum,
    lldb_private::ClangASTImporter::LayoutInfo &layout_info) {
  while (auto member = members_enum.getNext()) {
    if (member->isCompilerGenerated())
      continue;

    auto member_name = member->getName();

    auto member_type = symbol_file.ResolveTypeUID(member->getTypeId());
    if (!member_type)
      continue;

    auto member_comp_type = member_type->GetLayoutCompilerType();
    if (!member_comp_type.GetCompleteType()) {
      symbol_file.GetObjectFile()->GetModule()->ReportError(
          ":: Class '%s' has a member '%s' of type '%s' "
          "which does not have a complete definition.",
          record_type.GetTypeName().GetCString(), member_name.c_str(),
          member_comp_type.GetTypeName().GetCString());
      if (ClangASTContext::StartTagDeclarationDefinition(member_comp_type))
        ClangASTContext::CompleteTagDeclarationDefinition(member_comp_type);
    }

    auto access = TranslateMemberAccess(member->getAccess());

    switch (member->getDataKind()) {
    case PDB_DataKind::Member: {
      auto location_type = member->getLocationType();

      auto bit_size = member->getLength();
      if (location_type == PDB_LocType::ThisRel)
        bit_size *= 8;

      auto decl = ClangASTContext::AddFieldToRecordType(
          record_type, member_name.c_str(), member_comp_type, access, bit_size);
      if (!decl)
        continue;

      m_uid_to_decl[member->getSymIndexId()] = decl;

      auto offset = member->getOffset() * 8;
      if (location_type == PDB_LocType::BitField)
        offset += member->getBitPosition();

      layout_info.field_offsets.insert(std::make_pair(decl, offset));

      break;
    }
    case PDB_DataKind::StaticMember: {
      auto decl = ClangASTContext::AddVariableToRecordType(
          record_type, member_name.c_str(), member_comp_type, access);
      if (!decl)
        continue;

      m_uid_to_decl[member->getSymIndexId()] = decl;

      break;
    }
    default:
      llvm_unreachable("unsupported PDB data kind");
    }
  }
}

void PDBASTParser::AddRecordBases(
    lldb_private::SymbolFile &symbol_file,
    lldb_private::CompilerType &record_type, int record_kind,
    PDBBaseClassSymbolEnumerator &bases_enum,
    lldb_private::ClangASTImporter::LayoutInfo &layout_info) const {
  std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> base_classes;

  while (auto base = bases_enum.getNext()) {
    auto base_type = symbol_file.ResolveTypeUID(base->getTypeId());
    if (!base_type)
      continue;

    auto base_comp_type = base_type->GetFullCompilerType();
    if (!base_comp_type.GetCompleteType()) {
      symbol_file.GetObjectFile()->GetModule()->ReportError(
          ":: Class '%s' has a base class '%s' "
          "which does not have a complete definition.",
          record_type.GetTypeName().GetCString(),
          base_comp_type.GetTypeName().GetCString());
      if (ClangASTContext::StartTagDeclarationDefinition(base_comp_type))
        ClangASTContext::CompleteTagDeclarationDefinition(base_comp_type);
    }

    auto access = TranslateMemberAccess(base->getAccess());

    auto is_virtual = base->isVirtualBaseClass();

    std::unique_ptr<clang::CXXBaseSpecifier> base_spec =
        m_ast.CreateBaseClassSpecifier(base_comp_type.GetOpaqueQualType(),
                                       access, is_virtual,
                                       record_kind == clang::TTK_Class);
    lldbassert(base_spec);

    base_classes.push_back(std::move(base_spec));

    if (is_virtual)
      continue;

    auto decl = m_ast.GetAsCXXRecordDecl(base_comp_type.GetOpaqueQualType());
    if (!decl)
      continue;

    auto offset = clang::CharUnits::fromQuantity(base->getOffset());
    layout_info.base_offsets.insert(std::make_pair(decl, offset));
  }

  m_ast.TransferBaseClasses(record_type.GetOpaqueQualType(),
                            std::move(base_classes));
}

void PDBASTParser::AddRecordMethods(lldb_private::SymbolFile &symbol_file,
                                    lldb_private::CompilerType &record_type,
                                    PDBFuncSymbolEnumerator &methods_enum) {
  while (std::unique_ptr<PDBSymbolFunc> method = methods_enum.getNext())
    if (clang::CXXMethodDecl *decl =
            AddRecordMethod(symbol_file, record_type, *method))
      m_uid_to_decl[method->getSymIndexId()] = decl;
}

clang::CXXMethodDecl *
PDBASTParser::AddRecordMethod(lldb_private::SymbolFile &symbol_file,
                              lldb_private::CompilerType &record_type,
                              const llvm::pdb::PDBSymbolFunc &method) const {
  std::string name = MSVCUndecoratedNameParser::DropScope(method.getName());

  Type *method_type = symbol_file.ResolveTypeUID(method.getSymIndexId());
  // MSVC specific __vecDelDtor.
  if (!method_type)
    return nullptr;

  CompilerType method_comp_type = method_type->GetFullCompilerType();
  if (!method_comp_type.GetCompleteType()) {
    symbol_file.GetObjectFile()->GetModule()->ReportError(
        ":: Class '%s' has a method '%s' whose type cannot be completed.",
        record_type.GetTypeName().GetCString(),
        method_comp_type.GetTypeName().GetCString());
    if (ClangASTContext::StartTagDeclarationDefinition(method_comp_type))
      ClangASTContext::CompleteTagDeclarationDefinition(method_comp_type);
  }

  AccessType access = TranslateMemberAccess(method.getAccess());
  if (access == eAccessNone)
    access = eAccessPublic;

  // TODO: get mangled name for the method.
  return m_ast.AddMethodToCXXRecordType(
      record_type.GetOpaqueQualType(), name.c_str(),
      /*mangled_name*/ nullptr, method_comp_type, access, method.isVirtual(),
      method.isStatic(), method.hasInlineAttribute(),
      /*is_explicit*/ false, // FIXME: Need this field in CodeView.
      /*is_attr_used*/ false,
      /*is_artificial*/ method.isCompilerGenerated());
}
