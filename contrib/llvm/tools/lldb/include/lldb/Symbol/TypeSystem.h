//===-- TypeSystem.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_TypeSystem_h_
#define liblldb_TypeSystem_h_

#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

#include "lldb/Core/PluginInterface.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/lldb-private.h"

class DWARFDIE;
class DWARFASTParser;

namespace lldb_private {

//----------------------------------------------------------------------
// Interface for representing the Type Systems in different languages.
//----------------------------------------------------------------------
class TypeSystem : public PluginInterface {
public:
  //----------------------------------------------------------------------
  // Intrusive type system that allows us to use llvm casting.
  //
  // To add a new type system:
  //
  // 1 - Add a new enumeration for llvm casting below for your TypeSystem
  //     subclass, here we will use eKindFoo
  //
  // 2 - Your TypeSystem subclass will inherit from TypeSystem and needs
  //     to implement a static classof() function that returns your
  //     enumeration:
  //
  //    class Foo : public lldb_private::TypeSystem
  //    {
  //        static bool classof(const TypeSystem *ts)
  //        {
  //            return ts->getKind() == TypeSystem::eKindFoo;
  //        }
  //    };
  //
  // 3 - Contruct your TypeSystem subclass with the enumeration from below
  //
  //    Foo() :
  //        TypeSystem(TypeSystem::eKindFoo),
  //        ...
  //    {
  //    }
  //
  // Then you can use the llvm casting on any "TypeSystem *" to get an instance
  // of your subclass.
  //----------------------------------------------------------------------
  enum LLVMCastKind {
    eKindClang,
    eKindSwift,
    eKindOCaml,
    kNumKinds
  };

  //----------------------------------------------------------------------
  // Constructors and Destructors
  //----------------------------------------------------------------------
  TypeSystem(LLVMCastKind kind);

  ~TypeSystem() override;

  LLVMCastKind getKind() const { return m_kind; }

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module);

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Target *target);

  // Free up any resources associated with this TypeSystem.  Done before
  // removing all the TypeSystems from the TypeSystemMap.
  virtual void Finalize() {}

  virtual DWARFASTParser *GetDWARFParser() { return nullptr; }

  virtual SymbolFile *GetSymbolFile() const { return m_sym_file; }

  // Returns true if the symbol file changed during the set accessor.
  virtual void SetSymbolFile(SymbolFile *sym_file) { m_sym_file = sym_file; }

  //----------------------------------------------------------------------
  // CompilerDecl functions
  //----------------------------------------------------------------------
  virtual ConstString DeclGetName(void *opaque_decl) = 0;

  virtual ConstString DeclGetMangledName(void *opaque_decl);

  virtual CompilerDeclContext DeclGetDeclContext(void *opaque_decl);

  virtual CompilerType DeclGetFunctionReturnType(void *opaque_decl);

  virtual size_t DeclGetFunctionNumArguments(void *opaque_decl);

  virtual CompilerType DeclGetFunctionArgumentType(void *opaque_decl,
                                                   size_t arg_idx);

  //----------------------------------------------------------------------
  // CompilerDeclContext functions
  //----------------------------------------------------------------------

  virtual std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_imported_decls);

  virtual bool DeclContextIsStructUnionOrClass(void *opaque_decl_ctx) = 0;

  virtual ConstString DeclContextGetName(void *opaque_decl_ctx) = 0;

  virtual ConstString
  DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) = 0;

  virtual bool DeclContextIsClassMethod(
      void *opaque_decl_ctx, lldb::LanguageType *language_ptr,
      bool *is_instance_method_ptr, ConstString *language_object_name_ptr) = 0;

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------

  virtual bool IsArrayType(lldb::opaque_compiler_type_t type,
                           CompilerType *element_type, uint64_t *size,
                           bool *is_incomplete) = 0;

  virtual bool IsAggregateType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsAnonymousType(lldb::opaque_compiler_type_t type);

  virtual bool IsCharType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsCompleteType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsDefined(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsFloatingPointType(lldb::opaque_compiler_type_t type,
                                   uint32_t &count, bool &is_complex) = 0;

  virtual bool IsFunctionType(lldb::opaque_compiler_type_t type,
                              bool *is_variadic_ptr) = 0;

  virtual size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType
  GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                             const size_t index) = 0;

  virtual bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                                  CompilerType *function_pointer_type_ptr) = 0;

  virtual bool IsIntegerType(lldb::opaque_compiler_type_t type,
                             bool &is_signed) = 0;

  virtual bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                                 bool &is_signed) {
    is_signed = false;
    return false;
  }

  virtual bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                                     CompilerType *target_type, // Can pass NULL
                                     bool check_cplusplus, bool check_objc) = 0;

  virtual bool IsPointerType(lldb::opaque_compiler_type_t type,
                             CompilerType *pointee_type) = 0;

  virtual bool IsScalarType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsVoidType(lldb::opaque_compiler_type_t type) = 0;

  // TypeSystems can support more than one language
  virtual bool SupportsLanguage(lldb::LanguageType language) = 0;

  //----------------------------------------------------------------------
  // Type Completion
  //----------------------------------------------------------------------

  virtual bool GetCompleteType(lldb::opaque_compiler_type_t type) = 0;

  //----------------------------------------------------------------------
  // AST related queries
  //----------------------------------------------------------------------

  virtual uint32_t GetPointerByteSize() = 0;

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------

  virtual ConstString GetTypeName(lldb::opaque_compiler_type_t type) = 0;

  virtual uint32_t
  GetTypeInfo(lldb::opaque_compiler_type_t type,
              CompilerType *pointee_or_element_compiler_type) = 0;

  virtual lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) = 0;

  virtual lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) = 0;

  //----------------------------------------------------------------------
  // Creating related types
  //----------------------------------------------------------------------

  virtual CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                           uint64_t *stride) = 0;

  virtual CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                                    uint64_t size);

  virtual CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) = 0;

  // Returns -1 if this isn't a function of if the function doesn't have a
  // prototype Returns a value >= 0 if there is a prototype.
  virtual int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType
  GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                 size_t idx) = 0;

  virtual CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) = 0;

  virtual size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) = 0;

  virtual TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type, size_t idx) = 0;

  virtual CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType GetPointerType(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType
  GetLValueReferenceType(lldb::opaque_compiler_type_t type);

  virtual CompilerType
  GetRValueReferenceType(lldb::opaque_compiler_type_t type);

  virtual CompilerType AddConstModifier(lldb::opaque_compiler_type_t type);

  virtual CompilerType AddVolatileModifier(lldb::opaque_compiler_type_t type);

  virtual CompilerType AddRestrictModifier(lldb::opaque_compiler_type_t type);

  virtual CompilerType CreateTypedef(lldb::opaque_compiler_type_t type,
                                     const char *name,
                                     const CompilerDeclContext &decl_ctx);

  //----------------------------------------------------------------------
  // Exploring the type
  //----------------------------------------------------------------------

  virtual uint64_t GetBitSize(lldb::opaque_compiler_type_t type,
                              ExecutionContextScope *exe_scope) = 0;

  virtual lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                                     uint64_t &count) = 0;

  virtual lldb::Format GetFormat(lldb::opaque_compiler_type_t type) = 0;

  virtual uint32_t GetNumChildren(lldb::opaque_compiler_type_t type,
                                  bool omit_empty_base_classes,
                                  const ExecutionContext *exe_ctx) = 0;

  virtual CompilerType GetBuiltinTypeByName(const ConstString &name);

  virtual lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) = 0;

  virtual void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type,
                         const ConstString &name,
                         const llvm::APSInt &value)> const &callback) {}

  virtual uint32_t GetNumFields(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type,
                                       size_t idx, std::string &name,
                                       uint64_t *bit_offset_ptr,
                                       uint32_t *bitfield_bit_size_ptr,
                                       bool *is_bitfield_ptr) = 0;

  virtual uint32_t
  GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) = 0;

  virtual uint32_t
  GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType
  GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                            uint32_t *bit_offset_ptr) = 0;

  virtual CompilerType
  GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                             uint32_t *bit_offset_ptr) = 0;

  virtual CompilerType GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) = 0;

  // Lookup a child given a name. This function will match base class names and
  // member member names in "clang_type" only, not descendants.
  virtual uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                           const char *name,
                                           bool omit_empty_base_classes) = 0;

  // Lookup a child member given a name. This function will match member names
  // only and will descend into "clang_type" children in search for the first
  // member in this class, or any base class that matches "name".
  // TODO: Return all matches for a given name by returning a
  // vector<vector<uint32_t>>
  // so we catch all names that match a given child name, not just the first.
  virtual size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                const char *name, bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) = 0;

  virtual size_t GetNumTemplateArguments(lldb::opaque_compiler_type_t type);

  virtual lldb::TemplateArgumentKind
  GetTemplateArgumentKind(lldb::opaque_compiler_type_t type, size_t idx);
  virtual CompilerType GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                           size_t idx);
  virtual llvm::Optional<CompilerType::IntegralTemplateArgument>
  GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type, size_t idx);

  //----------------------------------------------------------------------
  // Dumping types
  //----------------------------------------------------------------------

  virtual void DumpValue(lldb::opaque_compiler_type_t type,
                         ExecutionContext *exe_ctx, Stream *s,
                         lldb::Format format, const DataExtractor &data,
                         lldb::offset_t data_offset, size_t data_byte_size,
                         uint32_t bitfield_bit_size,
                         uint32_t bitfield_bit_offset, bool show_types,
                         bool show_summary, bool verbose, uint32_t depth) = 0;

  virtual bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream *s,
                             lldb::Format format, const DataExtractor &data,
                             lldb::offset_t data_offset, size_t data_byte_size,
                             uint32_t bitfield_bit_size,
                             uint32_t bitfield_bit_offset,
                             ExecutionContextScope *exe_scope) = 0;

  virtual void
  DumpTypeDescription(lldb::opaque_compiler_type_t type) = 0; // Dump to stdout

  virtual void DumpTypeDescription(lldb::opaque_compiler_type_t type,
                                   Stream *s) = 0;

  //----------------------------------------------------------------------
  // TODO: These methods appear unused. Should they be removed?
  //----------------------------------------------------------------------

  virtual bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) = 0;

  virtual void DumpSummary(lldb::opaque_compiler_type_t type,
                           ExecutionContext *exe_ctx, Stream *s,
                           const DataExtractor &data,
                           lldb::offset_t data_offset,
                           size_t data_byte_size) = 0;

  // Converts "s" to a floating point value and place resulting floating point
  // bytes in the "dst" buffer.
  virtual size_t ConvertStringToFloatValue(lldb::opaque_compiler_type_t type,
                                           const char *s, uint8_t *dst,
                                           size_t dst_size) = 0;

  //----------------------------------------------------------------------
  // TODO: Determine if these methods should move to ClangASTContext.
  //----------------------------------------------------------------------

  virtual bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                        CompilerType *pointee_type) = 0;

  virtual unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsCStringType(lldb::opaque_compiler_type_t type,
                             uint32_t &length) = 0;

  virtual size_t GetTypeBitAlign(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) = 0;

  virtual CompilerType
  GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                      size_t bit_size) = 0;

  virtual bool IsBeingDefined(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsConst(lldb::opaque_compiler_type_t type) = 0;

  virtual uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                          CompilerType *base_type_ptr) = 0;

  virtual bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsTypedefType(lldb::opaque_compiler_type_t type) = 0;

  // If the current object represents a typedef type, get the underlying type
  virtual CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsVectorType(lldb::opaque_compiler_type_t type,
                            CompilerType *element_type, uint64_t *size) = 0;

  virtual CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) = 0;

  virtual CompilerType
  GetNonReferenceType(lldb::opaque_compiler_type_t type) = 0;

  virtual bool IsReferenceType(lldb::opaque_compiler_type_t type,
                               CompilerType *pointee_type, bool *is_rvalue) = 0;

  virtual bool
  ShouldTreatScalarValueAsAddress(lldb::opaque_compiler_type_t type) {
    return IsPointerOrReferenceType(type, nullptr);
  }

  virtual UserExpression *
  GetUserExpression(llvm::StringRef expr, llvm::StringRef prefix,
                    lldb::LanguageType language,
                    Expression::ResultType desired_type,
                    const EvaluateExpressionOptions &options) {
    return nullptr;
  }

  virtual FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                            const Address &function_address,
                                            const ValueList &arg_value_list,
                                            const char *name) {
    return nullptr;
  }

  virtual UtilityFunction *GetUtilityFunction(const char *text,
                                              const char *name) {
    return nullptr;
  }

  virtual PersistentExpressionState *GetPersistentExpressionState() {
    return nullptr;
  }

  virtual CompilerType GetTypeForFormatters(void *type);

  virtual LazyBool ShouldPrintAsOneLiner(void *type, ValueObject *valobj);

  // Type systems can have types that are placeholder types, which are meant to
  // indicate the presence of a type, but offer no actual information about
  // said types, and leave the burden of actually figuring type information out
  // to dynamic type resolution. For instance a language with a generics
  // system, can use placeholder types to indicate "type argument goes here",
  // without promising uniqueness of the placeholder, nor attaching any
  // actually idenfiable information to said placeholder. This API allows type
  // systems to tell LLDB when such a type has been encountered In response,
  // the debugger can react by not using this type as a cache entry in any
  // type-specific way For instance, LLDB will currently not cache any
  // formatters that are discovered on such a type as attributable to the
  // meaningless type itself, instead preferring to use the dynamic type
  virtual bool IsMeaninglessWithoutDynamicResolution(void *type);

protected:
  const LLVMCastKind m_kind; // Support for llvm casting
  SymbolFile *m_sym_file;
};

class TypeSystemMap {
public:
  TypeSystemMap();
  ~TypeSystemMap();

  // Clear calls Finalize on all the TypeSystems managed by this map, and then
  // empties the map.
  void Clear();

  // Iterate through all of the type systems that are created. Return true from
  // callback to keep iterating, false to stop iterating.
  void ForEach(std::function<bool(TypeSystem *)> const &callback);

  TypeSystem *GetTypeSystemForLanguage(lldb::LanguageType language,
                                       Module *module, bool can_create);

  TypeSystem *GetTypeSystemForLanguage(lldb::LanguageType language,
                                       Target *target, bool can_create);

protected:
  // This function does not take the map mutex, and should only be called from
  // functions that do take the mutex.
  void AddToMap(lldb::LanguageType language,
                lldb::TypeSystemSP const &type_system_sp);

  typedef std::map<lldb::LanguageType, lldb::TypeSystemSP> collection;
  mutable std::mutex m_mutex; ///< A mutex to keep this object happy in
                              ///multi-threaded environments.
  collection m_map;
  bool m_clear_in_progress;
};

} // namespace lldb_private

#endif // liblldb_TypeSystem_h_
