//===-- ClangASTContext.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangASTContext_h_
#define liblldb_ClangASTContext_h_

#include <stdint.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ExternalASTMerger.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"

#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-enumerations.h"

class DWARFASTParserClang;
#ifdef LLDB_ENABLE_ALL
class PDBASTParser;
#endif // LLDB_ENABLE_ALL

namespace lldb_private {

class Declaration;

class ClangASTContext : public TypeSystem {
public:
  typedef void (*CompleteTagDeclCallback)(void *baton, clang::TagDecl *);
  typedef void (*CompleteObjCInterfaceDeclCallback)(void *baton,
                                                    clang::ObjCInterfaceDecl *);

  //------------------------------------------------------------------
  // llvm casting support
  //------------------------------------------------------------------
  static bool classof(const TypeSystem *ts) {
    return ts->getKind() == TypeSystem::eKindClang;
  }

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ClangASTContext(const char *triple = nullptr);

  ~ClangASTContext() override;

  void Finalize() override;

  //------------------------------------------------------------------
  // PluginInterface functions
  //------------------------------------------------------------------
  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  static ConstString GetPluginNameStatic();

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module, Target *target);

  static void EnumerateSupportedLanguages(
      std::set<lldb::LanguageType> &languages_for_types,
      std::set<lldb::LanguageType> &languages_for_expressions);

  static void Initialize();

  static void Terminate();

  static ClangASTContext *GetASTContext(clang::ASTContext *ast_ctx);

  clang::ASTContext *getASTContext();

  void setASTContext(clang::ASTContext *ast_ctx);

  clang::Builtin::Context *getBuiltinContext();

  clang::IdentifierTable *getIdentifierTable();

  clang::LangOptions *getLanguageOptions();

  clang::SelectorTable *getSelectorTable();

  clang::FileManager *getFileManager();

  clang::SourceManager *getSourceManager();

  clang::DiagnosticsEngine *getDiagnosticsEngine();

  clang::DiagnosticConsumer *getDiagnosticConsumer();

  clang::MangleContext *getMangleContext();

  std::shared_ptr<clang::TargetOptions> &getTargetOptions();

  clang::TargetInfo *getTargetInfo();

  void Clear();

  const char *GetTargetTriple();

  void SetTargetTriple(const char *target_triple);

  void SetArchitecture(const ArchSpec &arch);

  bool HasExternalSource();

  void SetExternalSource(
      llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> &ast_source_ap);

  void RemoveExternalSource();

  bool GetCompleteDecl(clang::Decl *decl) {
    return ClangASTContext::GetCompleteDecl(getASTContext(), decl);
  }

  static void DumpDeclHiearchy(clang::Decl *decl);

  static void DumpDeclContextHiearchy(clang::DeclContext *decl_ctx);

  static bool DeclsAreEquivalent(clang::Decl *lhs_decl, clang::Decl *rhs_decl);

  static bool GetCompleteDecl(clang::ASTContext *ast, clang::Decl *decl);

  void SetMetadataAsUserID(const void *object, lldb::user_id_t user_id);

  void SetMetadata(const void *object, ClangASTMetadata &meta_data) {
    SetMetadata(getASTContext(), object, meta_data);
  }

  static void SetMetadata(clang::ASTContext *ast, const void *object,
                          ClangASTMetadata &meta_data);

  ClangASTMetadata *GetMetadata(const void *object) {
    return GetMetadata(getASTContext(), object);
  }

  static ClangASTMetadata *GetMetadata(clang::ASTContext *ast,
                                       const void *object);

  //------------------------------------------------------------------
  // Basic Types
  //------------------------------------------------------------------
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override;

  static CompilerType GetBuiltinTypeForEncodingAndBitSize(
      clang::ASTContext *ast, lldb::Encoding encoding, uint32_t bit_size);

  CompilerType GetBasicType(lldb::BasicType type);

  static CompilerType GetBasicType(clang::ASTContext *ast,
                                   lldb::BasicType type);

  static CompilerType GetBasicType(clang::ASTContext *ast,
                                   const ConstString &name);

  static lldb::BasicType GetBasicTypeEnumeration(const ConstString &name);

  CompilerType GetBuiltinTypeForDWARFEncodingAndBitSize(const char *type_name,
                                                        uint32_t dw_ate,
                                                        uint32_t bit_size);

  CompilerType GetCStringType(bool is_const);

  static CompilerType GetUnknownAnyType(clang::ASTContext *ast);

  CompilerType GetUnknownAnyType() {
    return ClangASTContext::GetUnknownAnyType(getASTContext());
  }

  static clang::DeclContext *GetDeclContextForType(clang::QualType type);

  static clang::DeclContext *GetDeclContextForType(const CompilerType &type);

  uint32_t GetPointerByteSize() override;

  static clang::DeclContext *GetTranslationUnitDecl(clang::ASTContext *ast);

  clang::DeclContext *GetTranslationUnitDecl() {
    return GetTranslationUnitDecl(getASTContext());
  }

  static clang::Decl *CopyDecl(clang::ASTContext *dest_context,
                               clang::ASTContext *source_context,
                               clang::Decl *source_decl);

  static bool AreTypesSame(CompilerType type1, CompilerType type2,
                           bool ignore_qualifiers = false);

  static CompilerType GetTypeForDecl(clang::NamedDecl *decl);

  static CompilerType GetTypeForDecl(clang::TagDecl *decl);

  static CompilerType GetTypeForDecl(clang::ObjCInterfaceDecl *objc_decl);

  template <typename RecordDeclType>
  CompilerType
  GetTypeForIdentifier(const ConstString &type_name,
                       clang::DeclContext *decl_context = nullptr) {
    CompilerType compiler_type;

    if (type_name.GetLength()) {
      clang::ASTContext *ast = getASTContext();
      if (ast) {
        if (!decl_context)
          decl_context = ast->getTranslationUnitDecl();

        clang::IdentifierInfo &myIdent =
            ast->Idents.get(type_name.GetCString());
        clang::DeclarationName myName =
            ast->DeclarationNames.getIdentifier(&myIdent);

        clang::DeclContext::lookup_result result =
            decl_context->lookup(myName);

        if (!result.empty()) {
          clang::NamedDecl *named_decl = result[0];
          if (const RecordDeclType *record_decl =
                  llvm::dyn_cast<RecordDeclType>(named_decl))
            compiler_type.SetCompilerType(
                ast, clang::QualType(record_decl->getTypeForDecl(), 0));
        }
      }
    }

    return compiler_type;
  }

  CompilerType CreateStructForIdentifier(
      const ConstString &type_name,
      const std::initializer_list<std::pair<const char *, CompilerType>>
          &type_fields,
      bool packed = false);

  CompilerType GetOrCreateStructForIdentifier(
      const ConstString &type_name,
      const std::initializer_list<std::pair<const char *, CompilerType>>
          &type_fields,
      bool packed = false);

  static bool IsOperator(const char *name,
                         clang::OverloadedOperatorKind &op_kind);

  //------------------------------------------------------------------
  // Structure, Unions, Classes
  //------------------------------------------------------------------

  static clang::AccessSpecifier
  ConvertAccessTypeToAccessSpecifier(lldb::AccessType access);

  static clang::AccessSpecifier
  UnifyAccessSpecifiers(clang::AccessSpecifier lhs, clang::AccessSpecifier rhs);

  static uint32_t GetNumBaseClasses(const clang::CXXRecordDecl *cxx_record_decl,
                                    bool omit_empty_base_classes);

  CompilerType CreateRecordType(clang::DeclContext *decl_ctx,
                                lldb::AccessType access_type, const char *name,
                                int kind, lldb::LanguageType language,
                                ClangASTMetadata *metadata = nullptr);

  class TemplateParameterInfos {
  public:
    bool IsValid() const {
      if (args.empty())
        return false;
      return args.size() == names.size() &&
        ((bool)pack_name == (bool)packed_args) &&
        (!packed_args || !packed_args->packed_args);
    }

    llvm::SmallVector<const char *, 2> names;
    llvm::SmallVector<clang::TemplateArgument, 2> args;
    
    const char * pack_name = nullptr;
    std::unique_ptr<TemplateParameterInfos> packed_args;
  };

  clang::FunctionTemplateDecl *
  CreateFunctionTemplateDecl(clang::DeclContext *decl_ctx,
                             clang::FunctionDecl *func_decl, const char *name,
                             const TemplateParameterInfos &infos);

  void CreateFunctionTemplateSpecializationInfo(
      clang::FunctionDecl *func_decl, clang::FunctionTemplateDecl *Template,
      const TemplateParameterInfos &infos);

  clang::ClassTemplateDecl *
  CreateClassTemplateDecl(clang::DeclContext *decl_ctx,
                          lldb::AccessType access_type, const char *class_name,
                          int kind, const TemplateParameterInfos &infos);

  clang::TemplateTemplateParmDecl *
  CreateTemplateTemplateParmDecl(const char *template_name);

  clang::ClassTemplateSpecializationDecl *CreateClassTemplateSpecializationDecl(
      clang::DeclContext *decl_ctx,
      clang::ClassTemplateDecl *class_template_decl, int kind,
      const TemplateParameterInfos &infos);

  CompilerType
  CreateClassTemplateSpecializationType(clang::ClassTemplateSpecializationDecl *
                                            class_template_specialization_decl);

  static clang::DeclContext *
  GetAsDeclContext(clang::CXXMethodDecl *cxx_method_decl);

  static clang::DeclContext *
  GetAsDeclContext(clang::ObjCMethodDecl *objc_method_decl);

  static bool CheckOverloadedOperatorKindParameterCount(
      bool is_method, clang::OverloadedOperatorKind op_kind,
      uint32_t num_params);

  bool FieldIsBitfield(clang::FieldDecl *field, uint32_t &bitfield_bit_size);

  static bool FieldIsBitfield(clang::ASTContext *ast, clang::FieldDecl *field,
                              uint32_t &bitfield_bit_size);

  static bool RecordHasFields(const clang::RecordDecl *record_decl);

  CompilerType CreateObjCClass(const char *name, clang::DeclContext *decl_ctx,
                               bool isForwardDecl, bool isInternal,
                               ClangASTMetadata *metadata = nullptr);

  bool SetTagTypeKind(clang::QualType type, int kind) const;

  bool SetDefaultAccessForRecordFields(clang::RecordDecl *record_decl,
                                       int default_accessibility,
                                       int *assigned_accessibilities,
                                       size_t num_assigned_accessibilities);

  // Returns a mask containing bits from the ClangASTContext::eTypeXXX
  // enumerations

  //------------------------------------------------------------------
  // Namespace Declarations
  //------------------------------------------------------------------

  clang::NamespaceDecl *
  GetUniqueNamespaceDeclaration(const char *name, clang::DeclContext *decl_ctx);

  static clang::NamespaceDecl *
  GetUniqueNamespaceDeclaration(clang::ASTContext *ast, const char *name,
                                clang::DeclContext *decl_ctx);

  //------------------------------------------------------------------
  // Function Types
  //------------------------------------------------------------------

  clang::FunctionDecl *
  CreateFunctionDeclaration(clang::DeclContext *decl_ctx, const char *name,
                            const CompilerType &function_Type, int storage,
                            bool is_inline);

  static CompilerType CreateFunctionType(clang::ASTContext *ast,
                                         const CompilerType &result_type,
                                         const CompilerType *args,
                                         unsigned num_args, bool is_variadic,
                                         unsigned type_quals,
                                         clang::CallingConv cc);

  static CompilerType CreateFunctionType(clang::ASTContext *ast,
                                         const CompilerType &result_type,
                                         const CompilerType *args,
                                         unsigned num_args, bool is_variadic,
                                         unsigned type_quals) {
    return ClangASTContext::CreateFunctionType(
        ast, result_type, args, num_args, is_variadic, type_quals, clang::CC_C);
  }

  CompilerType CreateFunctionType(const CompilerType &result_type,
                                  const CompilerType *args, unsigned num_args,
                                  bool is_variadic, unsigned type_quals) {
    return ClangASTContext::CreateFunctionType(
        getASTContext(), result_type, args, num_args, is_variadic, type_quals);
  }

  CompilerType CreateFunctionType(const CompilerType &result_type,
                                  const CompilerType *args, unsigned num_args,
                                  bool is_variadic, unsigned type_quals,
                                  clang::CallingConv cc) {
    return ClangASTContext::CreateFunctionType(getASTContext(), result_type,
                                               args, num_args, is_variadic,
                                               type_quals, cc);
  }

  clang::ParmVarDecl *CreateParameterDeclaration(clang::DeclContext *decl_ctx,
                                                 const char *name,
                                                 const CompilerType &param_type,
                                                 int storage);

  void SetFunctionParameters(clang::FunctionDecl *function_decl,
                             clang::ParmVarDecl **params, unsigned num_params);

  CompilerType CreateBlockPointerType(const CompilerType &function_type);

  //------------------------------------------------------------------
  // Array Types
  //------------------------------------------------------------------

  CompilerType CreateArrayType(const CompilerType &element_type,
                               size_t element_count, bool is_vector);

  //------------------------------------------------------------------
  // Enumeration Types
  //------------------------------------------------------------------
  CompilerType CreateEnumerationType(const char *name,
                                     clang::DeclContext *decl_ctx,
                                     const Declaration &decl,
                                     const CompilerType &integer_qual_type,
                                     bool is_scoped);

  //------------------------------------------------------------------
  // Integer type functions
  //------------------------------------------------------------------

  static CompilerType GetIntTypeFromBitSize(clang::ASTContext *ast,
                                            size_t bit_size, bool is_signed);

  CompilerType GetPointerSizedIntType(bool is_signed) {
    return GetPointerSizedIntType(getASTContext(), is_signed);
  }

  static CompilerType GetPointerSizedIntType(clang::ASTContext *ast,
                                             bool is_signed);

  //------------------------------------------------------------------
  // Floating point functions
  //------------------------------------------------------------------

  static CompilerType GetFloatTypeFromBitSize(clang::ASTContext *ast,
                                              size_t bit_size);

  //------------------------------------------------------------------
  // TypeSystem methods
  //------------------------------------------------------------------
  DWARFASTParser *GetDWARFParser() override;
#ifdef LLDB_ENABLE_ALL
  PDBASTParser *GetPDBParser();
#endif // LLDB_ENABLE_ALL

  //------------------------------------------------------------------
  // ClangASTContext callbacks for external source lookups.
  //------------------------------------------------------------------
  static void CompleteTagDecl(void *baton, clang::TagDecl *);

  static void CompleteObjCInterfaceDecl(void *baton,
                                        clang::ObjCInterfaceDecl *);

  static bool LayoutRecordType(
      void *baton, const clang::RecordDecl *record_decl, uint64_t &size,
      uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  //----------------------------------------------------------------------
  // CompilerDecl override functions
  //----------------------------------------------------------------------
  ConstString DeclGetName(void *opaque_decl) override;

  ConstString DeclGetMangledName(void *opaque_decl) override;

  CompilerDeclContext DeclGetDeclContext(void *opaque_decl) override;

  CompilerType DeclGetFunctionReturnType(void *opaque_decl) override;

  size_t DeclGetFunctionNumArguments(void *opaque_decl) override;

  CompilerType DeclGetFunctionArgumentType(void *opaque_decl,
                                           size_t arg_idx) override;

  //----------------------------------------------------------------------
  // CompilerDeclContext override functions
  //----------------------------------------------------------------------

  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_using_decls) override;

  bool DeclContextIsStructUnionOrClass(void *opaque_decl_ctx) override;

  ConstString DeclContextGetName(void *opaque_decl_ctx) override;

  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;

  bool DeclContextIsClassMethod(void *opaque_decl_ctx,
                                lldb::LanguageType *language_ptr,
                                bool *is_instance_method_ptr,
                                ConstString *language_object_name_ptr) override;

  //----------------------------------------------------------------------
  // Clang specific clang::DeclContext functions
  //----------------------------------------------------------------------

  static clang::DeclContext *
  DeclContextGetAsDeclContext(const CompilerDeclContext &dc);

  static clang::ObjCMethodDecl *
  DeclContextGetAsObjCMethodDecl(const CompilerDeclContext &dc);

  static clang::CXXMethodDecl *
  DeclContextGetAsCXXMethodDecl(const CompilerDeclContext &dc);

  static clang::FunctionDecl *
  DeclContextGetAsFunctionDecl(const CompilerDeclContext &dc);

  static clang::NamespaceDecl *
  DeclContextGetAsNamespaceDecl(const CompilerDeclContext &dc);

  static ClangASTMetadata *DeclContextGetMetaData(const CompilerDeclContext &dc,
                                                  const void *object);

  static clang::ASTContext *
  DeclContextGetClangASTContext(const CompilerDeclContext &dc);

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;

  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;

  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;

  bool IsAnonymousType(lldb::opaque_compiler_type_t type) override;

  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;

  bool IsCharType(lldb::opaque_compiler_type_t type) override;

  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;

  bool IsConst(lldb::opaque_compiler_type_t type) override;

  bool IsCStringType(lldb::opaque_compiler_type_t type,
                     uint32_t &length) override;

  static bool IsCXXClassType(const CompilerType &type);

  bool IsDefined(lldb::opaque_compiler_type_t type) override;

  bool IsFloatingPointType(lldb::opaque_compiler_type_t type, uint32_t &count,
                           bool &is_complex) override;

  bool IsFunctionType(lldb::opaque_compiler_type_t type,
                      bool *is_variadic_ptr) override;

  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override;

  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override;

  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;

  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override;

  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;

  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;

  static bool IsObjCClassType(const CompilerType &type);

  static bool IsObjCClassTypeAndHasIVars(const CompilerType &type,
                                         bool check_superclass);

  static bool IsObjCObjectOrInterfaceType(const CompilerType &type);

  static bool IsObjCObjectPointerType(const CompilerType &type,
                                      CompilerType *target_type = nullptr);

  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;

  static bool IsClassType(lldb::opaque_compiler_type_t type);

  static bool IsEnumType(lldb::opaque_compiler_type_t type);

  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, // Can pass nullptr
                             bool check_cplusplus, bool check_objc) override;

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;

  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override;

  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override;

  bool IsScalarType(lldb::opaque_compiler_type_t type) override;

  bool IsTypedefType(lldb::opaque_compiler_type_t type) override;

  bool IsVoidType(lldb::opaque_compiler_type_t type) override;

  bool SupportsLanguage(lldb::LanguageType language) override;

  static bool GetCXXClassName(const CompilerType &type,
                              std::string &class_name);

  static bool GetObjCClassName(const CompilerType &type,
                               std::string &class_name);

  //----------------------------------------------------------------------
  // Type Completion
  //----------------------------------------------------------------------

  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------

  ConstString GetTypeName(lldb::opaque_compiler_type_t type) override;

  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;

  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;

  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;

  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;

  //----------------------------------------------------------------------
  // Creating related types
  //----------------------------------------------------------------------

  // Using the current type, create a new typedef to that type using
  // "typedef_name" as the name and "decl_ctx" as the decl context.
  static CompilerType
  CreateTypedefType(const CompilerType &type, const char *typedef_name,
                    const CompilerDeclContext &compiler_decl_ctx);

  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   uint64_t *stride) override;

  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override;

  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;

  // Returns -1 if this isn't a function of if the function doesn't have a
  // prototype Returns a value >= 0 if there is a prototype.
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;

  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;

  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;

  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;

  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetLValueReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetRValueReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType AddConstModifier(lldb::opaque_compiler_type_t type) override;

  CompilerType AddVolatileModifier(lldb::opaque_compiler_type_t type) override;

  CompilerType AddRestrictModifier(lldb::opaque_compiler_type_t type) override;

  CompilerType CreateTypedef(lldb::opaque_compiler_type_t type,
                             const char *name,
                             const CompilerDeclContext &decl_ctx) override;

  // If the current object represents a typedef type, get the underlying type
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;

  //----------------------------------------------------------------------
  // Create related types using the current type's AST
  //----------------------------------------------------------------------
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;

  //----------------------------------------------------------------------
  // Exploring the type
  //----------------------------------------------------------------------

  uint64_t GetByteSize(lldb::opaque_compiler_type_t type,
                       ExecutionContextScope *exe_scope) {
    return (GetBitSize(type, exe_scope) + 7) / 8;
  }

  uint64_t GetBitSize(lldb::opaque_compiler_type_t type,
                      ExecutionContextScope *exe_scope) override;

  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                             uint64_t &count) override;

  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;

  size_t GetTypeBitAlign(lldb::opaque_compiler_type_t type) override;

  uint32_t GetNumChildren(lldb::opaque_compiler_type_t type,
                          bool omit_empty_base_classes,
                          const ExecutionContext *exe_ctx) override;

  CompilerType GetBuiltinTypeByName(const ConstString &name) override;

  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;

  static lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type,
                          const ConstString &name);

  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type,
                         const ConstString &name,
                         const llvm::APSInt &value)> const &callback) override;

  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;

  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;

  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;

  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override;

  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override;

  static uint32_t GetNumPointeeChildren(clang::QualType type);

  CompilerType GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;

  // Lookup a child given a name. This function will match base class names and
  // member member names in "clang_type" only, not descendants.
  uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                   const char *name,
                                   bool omit_empty_base_classes) override;

  // Lookup a child member given a name. This function will match member names
  // only and will descend into "clang_type" children in search for the first
  // member in this class, or any base class that matches "name".
  // TODO: Return all matches for a given name by returning a
  // vector<vector<uint32_t>>
  // so we catch all names that match a given child name, not just the first.
  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                const char *name, bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;

  size_t GetNumTemplateArguments(lldb::opaque_compiler_type_t type) override;

  lldb::TemplateArgumentKind
  GetTemplateArgumentKind(lldb::opaque_compiler_type_t type,
                          size_t idx) override;
  CompilerType GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                       size_t idx) override;
  llvm::Optional<CompilerType::IntegralTemplateArgument>
  GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type,
                              size_t idx) override;

  CompilerType GetTypeForFormatters(void *type) override;

#define LLDB_INVALID_DECL_LEVEL UINT32_MAX
  // LLDB_INVALID_DECL_LEVEL is returned by CountDeclLevels if child_decl_ctx
  // could not be found in decl_ctx.
  uint32_t CountDeclLevels(clang::DeclContext *frame_decl_ctx,
                           clang::DeclContext *child_decl_ctx,
                           ConstString *child_name = nullptr,
                           CompilerType *child_type = nullptr);

  //----------------------------------------------------------------------
  // Modifying RecordType
  //----------------------------------------------------------------------
  static clang::FieldDecl *AddFieldToRecordType(const CompilerType &type,
                                                llvm::StringRef name,
                                                const CompilerType &field_type,
                                                lldb::AccessType access,
                                                uint32_t bitfield_bit_size);

  static void BuildIndirectFields(const CompilerType &type);

  static void SetIsPacked(const CompilerType &type);

  static clang::VarDecl *AddVariableToRecordType(const CompilerType &type,
                                                 llvm::StringRef name,
                                                 const CompilerType &var_type,
                                                 lldb::AccessType access);

  clang::CXXMethodDecl *
  AddMethodToCXXRecordType(lldb::opaque_compiler_type_t type, const char *name,
                           const char *mangled_name,
                           const CompilerType &method_type,
                           lldb::AccessType access, bool is_virtual,
                           bool is_static, bool is_inline, bool is_explicit,
                           bool is_attr_used, bool is_artificial);

  void AddMethodOverridesForCXXRecordType(lldb::opaque_compiler_type_t type);

  // C++ Base Classes
  std::unique_ptr<clang::CXXBaseSpecifier>
  CreateBaseClassSpecifier(lldb::opaque_compiler_type_t type,
                           lldb::AccessType access, bool is_virtual,
                           bool base_of_class);

  bool TransferBaseClasses(
      lldb::opaque_compiler_type_t type,
      std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases);

  static bool SetObjCSuperClass(const CompilerType &type,
                                const CompilerType &superclass_compiler_type);

  static bool AddObjCClassProperty(const CompilerType &type,
                                   const char *property_name,
                                   const CompilerType &property_compiler_type,
                                   clang::ObjCIvarDecl *ivar_decl,
                                   const char *property_setter_name,
                                   const char *property_getter_name,
                                   uint32_t property_attributes,
                                   ClangASTMetadata *metadata);

  static clang::ObjCMethodDecl *AddMethodToObjCObjectType(
      const CompilerType &type,
      const char *name, // the full symbol name as seen in the symbol table
                        // (lldb::opaque_compiler_type_t type, "-[NString
                        // stringWithCString:]")
      const CompilerType &method_compiler_type, lldb::AccessType access,
      bool is_artificial, bool is_variadic);

  static bool SetHasExternalStorage(lldb::opaque_compiler_type_t type,
                                    bool has_extern);

  static bool GetHasExternalStorage(const CompilerType &type);
  //------------------------------------------------------------------
  // Tag Declarations
  //------------------------------------------------------------------
  static bool StartTagDeclarationDefinition(const CompilerType &type);

  static bool CompleteTagDeclarationDefinition(const CompilerType &type);

  //----------------------------------------------------------------------
  // Modifying Enumeration types
  //----------------------------------------------------------------------
  clang::EnumConstantDecl *AddEnumerationValueToEnumerationType(
      const CompilerType &enum_type, const Declaration &decl, const char *name,
      int64_t enum_value, uint32_t enum_value_bit_size);
  clang::EnumConstantDecl *AddEnumerationValueToEnumerationType(
      const CompilerType &enum_type, const Declaration &decl, const char *name,
      const llvm::APSInt &value);

  CompilerType GetEnumerationIntegerType(lldb::opaque_compiler_type_t type);

  //------------------------------------------------------------------
  // Pointers & References
  //------------------------------------------------------------------

  // Call this function using the class type when you want to make a member
  // pointer type to pointee_type.
  static CompilerType CreateMemberPointerType(const CompilerType &type,
                                              const CompilerType &pointee_type);

  // Converts "s" to a floating point value and place resulting floating point
  // bytes in the "dst" buffer.
  size_t ConvertStringToFloatValue(lldb::opaque_compiler_type_t type,
                                   const char *s, uint8_t *dst,
                                   size_t dst_size) override;

  //----------------------------------------------------------------------
  // Dumping types
  //----------------------------------------------------------------------
  void Dump(Stream &s);

  void DumpValue(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                 Stream *s, lldb::Format format, const DataExtractor &data,
                 lldb::offset_t data_offset, size_t data_byte_size,
                 uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                 bool show_types, bool show_summary, bool verbose,
                 uint32_t depth) override;

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream *s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override;

  void DumpSummary(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                   Stream *s, const DataExtractor &data,
                   lldb::offset_t data_offset, size_t data_byte_size) override;

  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type) override; // Dump to stdout

  void DumpTypeDescription(lldb::opaque_compiler_type_t type,
                           Stream *s) override;

  static void DumpTypeName(const CompilerType &type);

  static clang::EnumDecl *GetAsEnumDecl(const CompilerType &type);

  static clang::RecordDecl *GetAsRecordDecl(const CompilerType &type);

  static clang::TagDecl *GetAsTagDecl(const CompilerType &type);

  static clang::TypedefNameDecl *GetAsTypedefDecl(const CompilerType &type);

  clang::CXXRecordDecl *GetAsCXXRecordDecl(lldb::opaque_compiler_type_t type);

  static clang::ObjCInterfaceDecl *
  GetAsObjCInterfaceDecl(const CompilerType &type);

  clang::ClassTemplateDecl *ParseClassTemplateDecl(
      clang::DeclContext *decl_ctx, lldb::AccessType access_type,
      const char *parent_name, int tag_decl_kind,
      const ClangASTContext::TemplateParameterInfos &template_param_infos);

  clang::BlockDecl *CreateBlockDeclaration(clang::DeclContext *ctx);

  clang::UsingDirectiveDecl *
  CreateUsingDirectiveDeclaration(clang::DeclContext *decl_ctx,
                                  clang::NamespaceDecl *ns_decl);

  clang::UsingDecl *CreateUsingDeclaration(clang::DeclContext *current_decl_ctx,
                                           clang::NamedDecl *target);

  clang::VarDecl *CreateVariableDeclaration(clang::DeclContext *decl_context,
                                            const char *name,
                                            clang::QualType type);

  static lldb::opaque_compiler_type_t
  GetOpaqueCompilerType(clang::ASTContext *ast, lldb::BasicType basic_type);

  static clang::QualType GetQualType(lldb::opaque_compiler_type_t type) {
    if (type)
      return clang::QualType::getFromOpaquePtr(type);
    return clang::QualType();
  }

  static clang::QualType
  GetCanonicalQualType(lldb::opaque_compiler_type_t type) {
    if (type)
      return clang::QualType::getFromOpaquePtr(type).getCanonicalType();
    return clang::QualType();
  }

  clang::DeclarationName
  GetDeclarationName(const char *name, const CompilerType &function_clang_type);
  
  virtual const clang::ExternalASTMerger::OriginMap &GetOriginMap() {
    return m_origins;
  }
protected:
  const clang::ClassTemplateSpecializationDecl *
  GetAsTemplateSpecialization(lldb::opaque_compiler_type_t type);

  //------------------------------------------------------------------
  // Classes that inherit from ClangASTContext can see and modify these
  //------------------------------------------------------------------
  // clang-format off
    std::string                                     m_target_triple;
    std::unique_ptr<clang::ASTContext>              m_ast_ap;
    std::unique_ptr<clang::LangOptions>             m_language_options_ap;
    std::unique_ptr<clang::FileManager>             m_file_manager_ap;
    std::unique_ptr<clang::FileSystemOptions>       m_file_system_options_ap;
    std::unique_ptr<clang::SourceManager>           m_source_manager_ap;
    std::unique_ptr<clang::DiagnosticsEngine>       m_diagnostics_engine_ap;
    std::unique_ptr<clang::DiagnosticConsumer>      m_diagnostic_consumer_ap;
    std::shared_ptr<clang::TargetOptions>           m_target_options_rp;
    std::unique_ptr<clang::TargetInfo>              m_target_info_ap;
    std::unique_ptr<clang::IdentifierTable>         m_identifier_table_ap;
    std::unique_ptr<clang::SelectorTable>           m_selector_table_ap;
    std::unique_ptr<clang::Builtin::Context>        m_builtins_ap;
    std::unique_ptr<DWARFASTParserClang>            m_dwarf_ast_parser_ap;
#ifdef LLDB_ENABLE_ALL
    std::unique_ptr<PDBASTParser>                   m_pdb_ast_parser_ap;
#endif // LLDB_ENABLE_ALL
    std::unique_ptr<ClangASTSource>                 m_scratch_ast_source_ap;
    std::unique_ptr<clang::MangleContext>           m_mangle_ctx_ap;
    CompleteTagDeclCallback                         m_callback_tag_decl;
    CompleteObjCInterfaceDeclCallback               m_callback_objc_decl;
    void *                                          m_callback_baton;
    clang::ExternalASTMerger::OriginMap             m_origins;
    uint32_t                                        m_pointer_byte_size;
    bool                                            m_ast_owned;
    bool                                            m_can_evaluate_expressions;
  // clang-format on
private:
  //------------------------------------------------------------------
  // For ClangASTContext only
  //------------------------------------------------------------------
  ClangASTContext(const ClangASTContext &);
  const ClangASTContext &operator=(const ClangASTContext &);
};

class ClangASTContextForExpressions : public ClangASTContext {
public:
  ClangASTContextForExpressions(Target &target);

  ~ClangASTContextForExpressions() override = default;

  UserExpression *
  GetUserExpression(llvm::StringRef expr, llvm::StringRef prefix,
                    lldb::LanguageType language,
                    Expression::ResultType desired_type,
                    const EvaluateExpressionOptions &options) override;

  FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                    const Address &function_address,
                                    const ValueList &arg_value_list,
                                    const char *name) override;

  UtilityFunction *GetUtilityFunction(const char *text,
                                      const char *name) override;

  PersistentExpressionState *GetPersistentExpressionState() override;
  
  clang::ExternalASTMerger &GetMergerUnchecked();
  
  const clang::ExternalASTMerger::OriginMap &GetOriginMap() override {
    return GetMergerUnchecked().GetOrigins();
  }
private:
  lldb::TargetWP m_target_wp;
  lldb::ClangPersistentVariablesUP m_persistent_variables; ///< These are the
                                                           ///persistent
                                                           ///variables
                                                           ///associated with
                                                           ///this process for
                                                           ///the expression
                                                           ///parser.
};

} // namespace lldb_private

#endif // liblldb_ClangASTContext_h_
