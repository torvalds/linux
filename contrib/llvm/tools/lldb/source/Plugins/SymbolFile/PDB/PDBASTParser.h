//===-- PDBASTParser.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILE_PDB_PDBASTPARSER_H
#define LLDB_PLUGINS_SYMBOLFILE_PDB_PDBASTPARSER_H

#include "lldb/lldb-forward.h"

#include "lldb/Symbol/ClangASTImporter.h"

class SymbolFilePDB;

namespace clang {
class CharUnits;
class CXXRecordDecl;
class FieldDecl;
class RecordDecl;
} // namespace clang

namespace lldb_private {
class ClangASTContext;
class CompilerType;
} // namespace lldb_private

namespace llvm {
namespace pdb {
template <typename ChildType> class ConcreteSymbolEnumerator;

class PDBSymbol;
class PDBSymbolData;
class PDBSymbolFunc;
class PDBSymbolTypeBaseClass;
class PDBSymbolTypeBuiltin;
class PDBSymbolTypeUDT;
} // namespace pdb
} // namespace llvm

class PDBASTParser {
public:
  PDBASTParser(lldb_private::ClangASTContext &ast);
  ~PDBASTParser();

  lldb::TypeSP CreateLLDBTypeFromPDBType(const llvm::pdb::PDBSymbol &type);
  bool CompleteTypeFromPDB(lldb_private::CompilerType &compiler_type);

  clang::Decl *GetDeclForSymbol(const llvm::pdb::PDBSymbol &symbol);

  clang::DeclContext *
  GetDeclContextForSymbol(const llvm::pdb::PDBSymbol &symbol);
  clang::DeclContext *
  GetDeclContextContainingSymbol(const llvm::pdb::PDBSymbol &symbol);

  void ParseDeclsForDeclContext(const clang::DeclContext *decl_context);

  clang::NamespaceDecl *FindNamespaceDecl(const clang::DeclContext *parent,
                                          llvm::StringRef name);

  lldb_private::ClangASTImporter &GetClangASTImporter() {
    return m_ast_importer;
  }

private:
  typedef llvm::DenseMap<clang::CXXRecordDecl *, lldb::user_id_t>
      CXXRecordDeclToUidMap;
  typedef llvm::DenseMap<lldb::user_id_t, clang::Decl *> UidToDeclMap;
  typedef std::set<clang::NamespaceDecl *> NamespacesSet;
  typedef llvm::DenseMap<clang::DeclContext *, NamespacesSet>
      ParentToNamespacesMap;
  typedef llvm::DenseMap<clang::DeclContext *, lldb::user_id_t>
      DeclContextToUidMap;
  typedef llvm::pdb::ConcreteSymbolEnumerator<llvm::pdb::PDBSymbolData>
      PDBDataSymbolEnumerator;
  typedef llvm::pdb::ConcreteSymbolEnumerator<llvm::pdb::PDBSymbolTypeBaseClass>
      PDBBaseClassSymbolEnumerator;
  typedef llvm::pdb::ConcreteSymbolEnumerator<llvm::pdb::PDBSymbolFunc>
      PDBFuncSymbolEnumerator;

  bool AddEnumValue(lldb_private::CompilerType enum_type,
                    const llvm::pdb::PDBSymbolData &data);
  bool CompleteTypeFromUDT(lldb_private::SymbolFile &symbol_file,
                           lldb_private::CompilerType &compiler_type,
                           llvm::pdb::PDBSymbolTypeUDT &udt);
  void
  AddRecordMembers(lldb_private::SymbolFile &symbol_file,
                   lldb_private::CompilerType &record_type,
                   PDBDataSymbolEnumerator &members_enum,
                   lldb_private::ClangASTImporter::LayoutInfo &layout_info);
  void
  AddRecordBases(lldb_private::SymbolFile &symbol_file,
                 lldb_private::CompilerType &record_type, int record_kind,
                 PDBBaseClassSymbolEnumerator &bases_enum,
                 lldb_private::ClangASTImporter::LayoutInfo &layout_info) const;
  void AddRecordMethods(lldb_private::SymbolFile &symbol_file,
                        lldb_private::CompilerType &record_type,
                        PDBFuncSymbolEnumerator &methods_enum);
  clang::CXXMethodDecl *
  AddRecordMethod(lldb_private::SymbolFile &symbol_file,
                  lldb_private::CompilerType &record_type,
                  const llvm::pdb::PDBSymbolFunc &method) const;

  lldb_private::ClangASTContext &m_ast;
  lldb_private::ClangASTImporter m_ast_importer;

  CXXRecordDeclToUidMap m_forward_decl_to_uid;
  UidToDeclMap m_uid_to_decl;
  ParentToNamespacesMap m_parent_to_namespaces;
  NamespacesSet m_namespaces;
  DeclContextToUidMap m_decl_context_to_uid;
};

#endif // LLDB_PLUGINS_SYMBOLFILE_PDB_PDBASTPARSER_H
