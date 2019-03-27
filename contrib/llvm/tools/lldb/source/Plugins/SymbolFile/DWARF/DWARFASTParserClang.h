//===-- DWARFASTParserClang.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFASTParserClang_h_
#define SymbolFileDWARF_DWARFASTParserClang_h_

#include "clang/AST/CharUnits.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

#include "DWARFASTParser.h"
#include "DWARFDefines.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangASTImporter.h"

namespace lldb_private {
class CompileUnit;
}
class DWARFDebugInfoEntry;
class DWARFDIECollection;
class SymbolFileDWARF;

class DWARFASTParserClang : public DWARFASTParser {
public:
  DWARFASTParserClang(lldb_private::ClangASTContext &ast);

  ~DWARFASTParserClang() override;

  // DWARFASTParser interface.
  lldb::TypeSP ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                                  const DWARFDIE &die, lldb_private::Log *log,
                                  bool *type_is_new_ptr) override;

  lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const DWARFDIE &die) override;

  bool
  CompleteTypeFromDWARF(const DWARFDIE &die, lldb_private::Type *type,
                        lldb_private::CompilerType &compiler_type) override;

  lldb_private::CompilerDecl
  GetDeclForUIDFromDWARF(const DWARFDIE &die) override;

  std::vector<DWARFDIE>
  GetDIEForDeclContext(lldb_private::CompilerDeclContext decl_context) override;

  lldb_private::CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const DWARFDIE &die) override;

  lldb_private::CompilerDeclContext
  GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) override;

  lldb_private::ClangASTImporter &GetClangASTImporter();

protected:
  class DelayedAddObjCClassProperty;
  typedef std::vector<DelayedAddObjCClassProperty> DelayedPropertyList;

  clang::DeclContext *GetDeclContextForBlock(const DWARFDIE &die);

  clang::BlockDecl *ResolveBlockDIE(const DWARFDIE &die);

  clang::NamespaceDecl *ResolveNamespaceDIE(const DWARFDIE &die);

  bool ParseTemplateDIE(const DWARFDIE &die,
                        lldb_private::ClangASTContext::TemplateParameterInfos
                            &template_param_infos);
  bool ParseTemplateParameterInfos(
      const DWARFDIE &parent_die,
      lldb_private::ClangASTContext::TemplateParameterInfos
          &template_param_infos);

  bool ParseChildMembers(
      const lldb_private::SymbolContext &sc, const DWARFDIE &die,
      lldb_private::CompilerType &class_compiler_type,
      const lldb::LanguageType class_language,
      std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> &base_classes,
      std::vector<int> &member_accessibilities,
      DWARFDIECollection &member_function_dies,
      DelayedPropertyList &delayed_properties,
      lldb::AccessType &default_accessibility, bool &is_a_class,
      lldb_private::ClangASTImporter::LayoutInfo &layout_info);

  size_t
  ParseChildParameters(lldb_private::CompileUnit &comp_unit,
                       clang::DeclContext *containing_decl_ctx,
                       const DWARFDIE &parent_die, bool skip_artificial,
                       bool &is_static, bool &is_variadic,
                       bool &has_template_params,
                       std::vector<lldb_private::CompilerType> &function_args,
                       std::vector<clang::ParmVarDecl *> &function_param_decls,
                       unsigned &type_quals);

  size_t ParseChildEnumerators(const lldb_private::SymbolContext &sc,
                               lldb_private::CompilerType &compiler_type,
                               bool is_signed, uint32_t enumerator_byte_size,
                               const DWARFDIE &parent_die);

  lldb_private::Type *GetTypeForDIE(const DWARFDIE &die);

  clang::Decl *GetClangDeclForDIE(const DWARFDIE &die);

  clang::DeclContext *GetClangDeclContextForDIE(const DWARFDIE &die);

  clang::DeclContext *GetClangDeclContextContainingDIE(const DWARFDIE &die,
                                                       DWARFDIE *decl_ctx_die);

  bool CopyUniqueClassMethodTypes(const DWARFDIE &src_class_die,
                                  const DWARFDIE &dst_class_die,
                                  lldb_private::Type *class_type,
                                  DWARFDIECollection &failures);

  clang::DeclContext *GetCachedClangDeclContextForDIE(const DWARFDIE &die);

  void LinkDeclContextToDIE(clang::DeclContext *decl_ctx, const DWARFDIE &die);

  void LinkDeclToDIE(clang::Decl *decl, const DWARFDIE &die);

  lldb::TypeSP ParseTypeFromDWO(const DWARFDIE &die, lldb_private::Log *log);

  //----------------------------------------------------------------------
  // Return true if this type is a declaration to a type in an external
  // module.
  //----------------------------------------------------------------------
  lldb::ModuleSP GetModuleForType(const DWARFDIE &die);

  typedef llvm::SmallPtrSet<const DWARFDebugInfoEntry *, 4> DIEPointerSet;
  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, clang::DeclContext *>
      DIEToDeclContextMap;
  // typedef llvm::DenseMap<const clang::DeclContext *, DIEPointerSet>
  // DeclContextToDIEMap;
  typedef std::multimap<const clang::DeclContext *, const DWARFDIE>
      DeclContextToDIEMap;
  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, clang::Decl *>
      DIEToDeclMap;
  typedef llvm::DenseMap<const clang::Decl *, DIEPointerSet> DeclToDIEMap;

  lldb_private::ClangASTContext &m_ast;
  DIEToDeclMap m_die_to_decl;
  DeclToDIEMap m_decl_to_die;
  DIEToDeclContextMap m_die_to_decl_ctx;
  DeclContextToDIEMap m_decl_ctx_to_die;
  std::unique_ptr<lldb_private::ClangASTImporter> m_clang_ast_importer_ap;
};

#endif // SymbolFileDWARF_DWARFASTParserClang_h_
