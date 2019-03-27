//===-- DWARFASTParserClang.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "DWARFASTParserClang.h"
#include "DWARFDIE.h"
#include "DWARFDIECollection.h"
#include "DWARFDebugInfo.h"
#include "DWARFDeclContext.h"
#include "DWARFDefines.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDwo.h"
#include "SymbolFileDWARFDebugMap.h"
#include "UniqueDWARFASTType.h"

#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"

#include <map>
#include <vector>

//#define ENABLE_DEBUG_PRINTF // COMMENT OUT THIS LINE PRIOR TO CHECKIN

#ifdef ENABLE_DEBUG_PRINTF
#include <stdio.h>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;
DWARFASTParserClang::DWARFASTParserClang(ClangASTContext &ast)
    : m_ast(ast), m_die_to_decl_ctx(), m_decl_ctx_to_die() {}

DWARFASTParserClang::~DWARFASTParserClang() {}

static AccessType DW_ACCESS_to_AccessType(uint32_t dwarf_accessibility) {
  switch (dwarf_accessibility) {
  case DW_ACCESS_public:
    return eAccessPublic;
  case DW_ACCESS_private:
    return eAccessPrivate;
  case DW_ACCESS_protected:
    return eAccessProtected;
  default:
    break;
  }
  return eAccessNone;
}

static bool DeclKindIsCXXClass(clang::Decl::Kind decl_kind) {
  switch (decl_kind) {
  case clang::Decl::CXXRecord:
  case clang::Decl::ClassTemplateSpecialization:
    return true;
  default:
    break;
  }
  return false;
}

struct BitfieldInfo {
  uint64_t bit_size;
  uint64_t bit_offset;

  BitfieldInfo()
      : bit_size(LLDB_INVALID_ADDRESS), bit_offset(LLDB_INVALID_ADDRESS) {}

  void Clear() {
    bit_size = LLDB_INVALID_ADDRESS;
    bit_offset = LLDB_INVALID_ADDRESS;
  }

  bool IsValid() const {
    return (bit_size != LLDB_INVALID_ADDRESS) &&
           (bit_offset != LLDB_INVALID_ADDRESS);
  }

  bool NextBitfieldOffsetIsValid(const uint64_t next_bit_offset) const {
    if (IsValid()) {
      // This bitfield info is valid, so any subsequent bitfields must not
      // overlap and must be at a higher bit offset than any previous bitfield
      // + size.
      return (bit_size + bit_offset) <= next_bit_offset;
    } else {
      // If the this BitfieldInfo is not valid, then any offset isOK
      return true;
    }
  }
};

ClangASTImporter &DWARFASTParserClang::GetClangASTImporter() {
  if (!m_clang_ast_importer_ap) {
    m_clang_ast_importer_ap.reset(new ClangASTImporter);
  }
  return *m_clang_ast_importer_ap;
}

/// Detect a forward declaration that is nested in a DW_TAG_module.
static bool IsClangModuleFwdDecl(const DWARFDIE &Die) {
  if (!Die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    return false;
  auto Parent = Die.GetParent();
  while (Parent.IsValid()) {
    if (Parent.Tag() == DW_TAG_module)
      return true;
    Parent = Parent.GetParent();
  }
  return false;
}

TypeSP DWARFASTParserClang::ParseTypeFromDWO(const DWARFDIE &die, Log *log) {
  ModuleSP dwo_module_sp = die.GetContainingDWOModule();
  if (!dwo_module_sp)
    return TypeSP();

  // If this type comes from a Clang module, look in the DWARF section
  // of the pcm file in the module cache. Clang generates DWO skeleton
  // units as breadcrumbs to find them.
  std::vector<CompilerContext> decl_context;
  die.GetDeclContext(decl_context);
  TypeMap dwo_types;

  if (!dwo_module_sp->GetSymbolVendor()->FindTypes(decl_context, true,
                                                   dwo_types)) {
    if (!IsClangModuleFwdDecl(die))
      return TypeSP();

    // Since this this type is defined in one of the Clang modules imported by
    // this symbol file, search all of them.
    auto *sym_file = die.GetCU()->GetSymbolFileDWARF();
    for (const auto &name_module : sym_file->getExternalTypeModules()) {
      if (!name_module.second)
        continue;
      SymbolVendor *sym_vendor = name_module.second->GetSymbolVendor();
      if (sym_vendor->FindTypes(decl_context, true, dwo_types))
        break;
    }
  }

  if (dwo_types.GetSize() != 1)
    return TypeSP();

  // We found a real definition for this type in the Clang module, so lets use
  // it and cache the fact that we found a complete type for this die.
  TypeSP dwo_type_sp = dwo_types.GetTypeAtIndex(0);
  if (!dwo_type_sp)
    return TypeSP();

  lldb_private::CompilerType dwo_type = dwo_type_sp->GetForwardCompilerType();

  lldb_private::CompilerType type =
      GetClangASTImporter().CopyType(m_ast, dwo_type);

  if (!type)
    return TypeSP();

  SymbolFileDWARF *dwarf = die.GetDWARF();
  TypeSP type_sp(new Type(
      die.GetID(), dwarf, dwo_type_sp->GetName(), dwo_type_sp->GetByteSize(),
      NULL, LLDB_INVALID_UID, Type::eEncodingInvalid,
      &dwo_type_sp->GetDeclaration(), type, Type::eResolveStateForward));

  dwarf->GetTypeList()->Insert(type_sp);
  dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
  clang::TagDecl *tag_decl = ClangASTContext::GetAsTagDecl(type);
  if (tag_decl)
    LinkDeclContextToDIE(tag_decl, die);
  else {
    clang::DeclContext *defn_decl_ctx = GetCachedClangDeclContextForDIE(die);
    if (defn_decl_ctx)
      LinkDeclContextToDIE(defn_decl_ctx, die);
  }

  return type_sp;
}

static void CompleteExternalTagDeclType(ClangASTImporter &ast_importer,
                                        clang::DeclContext *decl_ctx,
                                        DWARFDIE die,
                                        const char *type_name_cstr) {
  auto *tag_decl_ctx = clang::dyn_cast<clang::TagDecl>(decl_ctx);
  if (!tag_decl_ctx)
    return;

  // If this type was not imported from an external AST, there's nothing to do.
  CompilerType type = ClangASTContext::GetTypeForDecl(tag_decl_ctx);
  if (!type || !ast_importer.CanImport(type))
    return;

  auto qual_type = ClangUtil::GetQualType(type);
  if (!ast_importer.RequireCompleteType(qual_type)) {
    die.GetDWARF()->GetObjectFile()->GetModule()->ReportError(
        "Unable to complete the Decl context for DIE '%s' at offset "
        "0x%8.8x.\nPlease file a bug report.",
        type_name_cstr ? type_name_cstr : "", die.GetOffset());
    // We need to make the type look complete otherwise, we might crash in
    // Clang when adding children.
    if (ClangASTContext::StartTagDeclarationDefinition(type))
      ClangASTContext::CompleteTagDeclarationDefinition(type);
  }
}

TypeSP DWARFASTParserClang::ParseTypeFromDWARF(const SymbolContext &sc,
                                               const DWARFDIE &die, Log *log,
                                               bool *type_is_new_ptr) {
  TypeSP type_sp;

  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  AccessType accessibility = eAccessNone;
  if (die) {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    if (log) {
      DWARFDIE context_die;
      clang::DeclContext *context =
          GetClangDeclContextContainingDIE(die, &context_die);

      dwarf->GetObjectFile()->GetModule()->LogMessage(
          log, "SymbolFileDWARF::ParseType (die = 0x%8.8x, decl_ctx = %p (die "
               "0x%8.8x)) %s name = '%s')",
          die.GetOffset(), static_cast<void *>(context),
          context_die.GetOffset(), die.GetTagAsCString(), die.GetName());
    }
    Type *type_ptr = dwarf->GetDIEToType().lookup(die.GetDIE());
    TypeList *type_list = dwarf->GetTypeList();
    if (type_ptr == NULL) {
      if (type_is_new_ptr)
        *type_is_new_ptr = true;

      const dw_tag_t tag = die.Tag();

      bool is_forward_declaration = false;
      DWARFAttributes attributes;
      const char *type_name_cstr = NULL;
      const char *mangled_name_cstr = NULL;
      ConstString type_name_const_str;
      Type::ResolveState resolve_state = Type::eResolveStateUnresolved;
      uint64_t byte_size = 0;
      Declaration decl;

      Type::EncodingDataType encoding_data_type = Type::eEncodingIsUID;
      CompilerType clang_type;
      DWARFFormValue form_value;

      dw_attr_t attr;

      switch (tag) {
      case DW_TAG_typedef:
      case DW_TAG_base_type:
      case DW_TAG_pointer_type:
      case DW_TAG_reference_type:
      case DW_TAG_rvalue_reference_type:
      case DW_TAG_const_type:
      case DW_TAG_restrict_type:
      case DW_TAG_volatile_type:
      case DW_TAG_unspecified_type: {
        // Set a bit that lets us know that we are currently parsing this
        dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;

        const size_t num_attributes = die.GetAttributes(attributes);
        uint32_t encoding = 0;
        DWARFFormValue encoding_uid;

        if (num_attributes > 0) {
          uint32_t i;
          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_decl_file:
                decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                    form_value.Unsigned()));
                break;
              case DW_AT_decl_line:
                decl.SetLine(form_value.Unsigned());
                break;
              case DW_AT_decl_column:
                decl.SetColumn(form_value.Unsigned());
                break;
              case DW_AT_name:
                type_name_cstr = form_value.AsCString();
                if (type_name_cstr)
                  type_name_const_str.SetCString(type_name_cstr);
                break;
              case DW_AT_byte_size:
                byte_size = form_value.Unsigned();
                break;
              case DW_AT_encoding:
                encoding = form_value.Unsigned();
                break;
              case DW_AT_type:
                encoding_uid = form_value;
                break;
              default:
              case DW_AT_sibling:
                break;
              }
            }
          }
        }

        if (tag == DW_TAG_typedef && encoding_uid.IsValid()) {
          // Try to parse a typedef from the DWO file first as modules can
          // contain typedef'ed structures that have no names like:
          //
          //  typedef struct { int a; } Foo;
          //
          // In this case we will have a structure with no name and a typedef
          // named "Foo" that points to this unnamed structure. The name in the
          // typedef is the only identifier for the struct, so always try to
          // get typedefs from DWO files if possible.
          //
          // The type_sp returned will be empty if the typedef doesn't exist in
          // a DWO file, so it is cheap to call this function just to check.
          //
          // If we don't do this we end up creating a TypeSP that says this is
          // a typedef to type 0x123 (the DW_AT_type value would be 0x123 in
          // the DW_TAG_typedef), and this is the unnamed structure type. We
          // will have a hard time tracking down an unnammed structure type in
          // the module DWO file, so we make sure we don't get into this
          // situation by always resolving typedefs from the DWO file.
          const DWARFDIE encoding_die = dwarf->GetDIE(DIERef(encoding_uid));

          // First make sure that the die that this is typedef'ed to _is_ just
          // a declaration (DW_AT_declaration == 1), not a full definition
          // since template types can't be represented in modules since only
          // concrete instances of templates are ever emitted and modules won't
          // contain those
          if (encoding_die &&
              encoding_die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0) ==
                  1) {
            type_sp = ParseTypeFromDWO(die, log);
            if (type_sp)
              return type_sp;
          }
        }

        DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\") type => 0x%8.8lx\n",
                     die.GetID(), DW_TAG_value_to_name(tag), type_name_cstr,
                     encoding_uid.Reference());

        switch (tag) {
        default:
          break;

        case DW_TAG_unspecified_type:
          if (strcmp(type_name_cstr, "nullptr_t") == 0 ||
              strcmp(type_name_cstr, "decltype(nullptr)") == 0) {
            resolve_state = Type::eResolveStateFull;
            clang_type = m_ast.GetBasicType(eBasicTypeNullPtr);
            break;
          }
          // Fall through to base type below in case we can handle the type
          // there...
          LLVM_FALLTHROUGH;

        case DW_TAG_base_type:
          resolve_state = Type::eResolveStateFull;
          clang_type = m_ast.GetBuiltinTypeForDWARFEncodingAndBitSize(
              type_name_cstr, encoding, byte_size * 8);
          break;

        case DW_TAG_pointer_type:
          encoding_data_type = Type::eEncodingIsPointerUID;
          break;
        case DW_TAG_reference_type:
          encoding_data_type = Type::eEncodingIsLValueReferenceUID;
          break;
        case DW_TAG_rvalue_reference_type:
          encoding_data_type = Type::eEncodingIsRValueReferenceUID;
          break;
        case DW_TAG_typedef:
          encoding_data_type = Type::eEncodingIsTypedefUID;
          break;
        case DW_TAG_const_type:
          encoding_data_type = Type::eEncodingIsConstUID;
          break;
        case DW_TAG_restrict_type:
          encoding_data_type = Type::eEncodingIsRestrictUID;
          break;
        case DW_TAG_volatile_type:
          encoding_data_type = Type::eEncodingIsVolatileUID;
          break;
        }

        if (!clang_type &&
            (encoding_data_type == Type::eEncodingIsPointerUID ||
             encoding_data_type == Type::eEncodingIsTypedefUID)) {
          if (tag == DW_TAG_pointer_type) {
            DWARFDIE target_die = die.GetReferencedDIE(DW_AT_type);

            if (target_die.GetAttributeValueAsUnsigned(DW_AT_APPLE_block, 0)) {
              // Blocks have a __FuncPtr inside them which is a pointer to a
              // function of the proper type.

              for (DWARFDIE child_die = target_die.GetFirstChild();
                   child_die.IsValid(); child_die = child_die.GetSibling()) {
                if (!strcmp(child_die.GetAttributeValueAsString(DW_AT_name, ""),
                            "__FuncPtr")) {
                  DWARFDIE function_pointer_type =
                      child_die.GetReferencedDIE(DW_AT_type);

                  if (function_pointer_type) {
                    DWARFDIE function_type =
                        function_pointer_type.GetReferencedDIE(DW_AT_type);

                    bool function_type_is_new_pointer;
                    TypeSP lldb_function_type_sp = ParseTypeFromDWARF(
                        sc, function_type, log, &function_type_is_new_pointer);

                    if (lldb_function_type_sp) {
                      clang_type = m_ast.CreateBlockPointerType(
                          lldb_function_type_sp->GetForwardCompilerType());
                      encoding_data_type = Type::eEncodingIsUID;
                      encoding_uid.Clear();
                      resolve_state = Type::eResolveStateFull;
                    }
                  }

                  break;
                }
              }
            }
          }

          bool translation_unit_is_objc =
              (sc.comp_unit->GetLanguage() == eLanguageTypeObjC ||
               sc.comp_unit->GetLanguage() == eLanguageTypeObjC_plus_plus);

          if (translation_unit_is_objc) {
            if (type_name_cstr != NULL) {
              static ConstString g_objc_type_name_id("id");
              static ConstString g_objc_type_name_Class("Class");
              static ConstString g_objc_type_name_selector("SEL");

              if (type_name_const_str == g_objc_type_name_id) {
                if (log)
                  dwarf->GetObjectFile()->GetModule()->LogMessage(
                      log, "SymbolFileDWARF::ParseType (die = 0x%8.8x) %s '%s' "
                           "is Objective-C 'id' built-in type.",
                      die.GetOffset(), die.GetTagAsCString(), die.GetName());
                clang_type = m_ast.GetBasicType(eBasicTypeObjCID);
                encoding_data_type = Type::eEncodingIsUID;
                encoding_uid.Clear();
                resolve_state = Type::eResolveStateFull;

              } else if (type_name_const_str == g_objc_type_name_Class) {
                if (log)
                  dwarf->GetObjectFile()->GetModule()->LogMessage(
                      log, "SymbolFileDWARF::ParseType (die = 0x%8.8x) %s '%s' "
                           "is Objective-C 'Class' built-in type.",
                      die.GetOffset(), die.GetTagAsCString(), die.GetName());
                clang_type = m_ast.GetBasicType(eBasicTypeObjCClass);
                encoding_data_type = Type::eEncodingIsUID;
                encoding_uid.Clear();
                resolve_state = Type::eResolveStateFull;
              } else if (type_name_const_str == g_objc_type_name_selector) {
                if (log)
                  dwarf->GetObjectFile()->GetModule()->LogMessage(
                      log, "SymbolFileDWARF::ParseType (die = 0x%8.8x) %s '%s' "
                           "is Objective-C 'selector' built-in type.",
                      die.GetOffset(), die.GetTagAsCString(), die.GetName());
                clang_type = m_ast.GetBasicType(eBasicTypeObjCSel);
                encoding_data_type = Type::eEncodingIsUID;
                encoding_uid.Clear();
                resolve_state = Type::eResolveStateFull;
              }
            } else if (encoding_data_type == Type::eEncodingIsPointerUID &&
                       encoding_uid.IsValid()) {
              // Clang sometimes erroneously emits id as objc_object*.  In that
              // case we fix up the type to "id".

              const DWARFDIE encoding_die = dwarf->GetDIE(DIERef(encoding_uid));

              if (encoding_die && encoding_die.Tag() == DW_TAG_structure_type) {
                if (const char *struct_name = encoding_die.GetName()) {
                  if (!strcmp(struct_name, "objc_object")) {
                    if (log)
                      dwarf->GetObjectFile()->GetModule()->LogMessage(
                          log, "SymbolFileDWARF::ParseType (die = 0x%8.8x) %s "
                               "'%s' is 'objc_object*', which we overrode to "
                               "'id'.",
                          die.GetOffset(), die.GetTagAsCString(),
                          die.GetName());
                    clang_type = m_ast.GetBasicType(eBasicTypeObjCID);
                    encoding_data_type = Type::eEncodingIsUID;
                    encoding_uid.Clear();
                    resolve_state = Type::eResolveStateFull;
                  }
                }
              }
            }
          }
        }

        type_sp.reset(
            new Type(die.GetID(), dwarf, type_name_const_str, byte_size, NULL,
                     DIERef(encoding_uid).GetUID(dwarf), encoding_data_type,
                     &decl, clang_type, resolve_state));

        dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
      } break;

      case DW_TAG_structure_type:
      case DW_TAG_union_type:
      case DW_TAG_class_type: {
        // Set a bit that lets us know that we are currently parsing this
        dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;
        bool byte_size_valid = false;

        LanguageType class_language = eLanguageTypeUnknown;
        bool is_complete_objc_class = false;
        size_t calling_convention 
                = llvm::dwarf::CallingConvention::DW_CC_normal;
        
        const size_t num_attributes = die.GetAttributes(attributes);
        if (num_attributes > 0) {
          uint32_t i;
          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_decl_file:
                decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                    form_value.Unsigned()));
                break;

              case DW_AT_decl_line:
                decl.SetLine(form_value.Unsigned());
                break;

              case DW_AT_decl_column:
                decl.SetColumn(form_value.Unsigned());
                break;

              case DW_AT_name:
                type_name_cstr = form_value.AsCString();
                type_name_const_str.SetCString(type_name_cstr);
                break;

              case DW_AT_byte_size:
                byte_size = form_value.Unsigned();
                byte_size_valid = true;
                break;

              case DW_AT_accessibility:
                accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned());
                break;

              case DW_AT_declaration:
                is_forward_declaration = form_value.Boolean();
                break;

              case DW_AT_APPLE_runtime_class:
                class_language = (LanguageType)form_value.Signed();
                break;

              case DW_AT_APPLE_objc_complete_type:
                is_complete_objc_class = form_value.Signed();
                break;
              case DW_AT_calling_convention:
                calling_convention = form_value.Unsigned();
                break;
                
              case DW_AT_allocated:
              case DW_AT_associated:
              case DW_AT_data_location:
              case DW_AT_description:
              case DW_AT_start_scope:
              case DW_AT_visibility:
              default:
              case DW_AT_sibling:
                break;
              }
            }
          }
        }

        // UniqueDWARFASTType is large, so don't create a local variables on
        // the stack, put it on the heap. This function is often called
        // recursively and clang isn't good and sharing the stack space for
        // variables in different blocks.
        std::unique_ptr<UniqueDWARFASTType> unique_ast_entry_ap(
            new UniqueDWARFASTType());

        ConstString unique_typename(type_name_const_str);
        Declaration unique_decl(decl);

        if (type_name_const_str) {
          LanguageType die_language = die.GetLanguage();
          if (Language::LanguageIsCPlusPlus(die_language)) {
            // For C++, we rely solely upon the one definition rule that says
            // only one thing can exist at a given decl context. We ignore the
            // file and line that things are declared on.
            std::string qualified_name;
            if (die.GetQualifiedName(qualified_name))
              unique_typename = ConstString(qualified_name);
            unique_decl.Clear();
          }

          if (dwarf->GetUniqueDWARFASTTypeMap().Find(
                  unique_typename, die, unique_decl,
                  byte_size_valid ? byte_size : -1, *unique_ast_entry_ap)) {
            type_sp = unique_ast_entry_ap->m_type_sp;
            if (type_sp) {
              dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
              return type_sp;
            }
          }
        }

        DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
                     DW_TAG_value_to_name(tag), type_name_cstr);

        int tag_decl_kind = -1;
        AccessType default_accessibility = eAccessNone;
        if (tag == DW_TAG_structure_type) {
          tag_decl_kind = clang::TTK_Struct;
          default_accessibility = eAccessPublic;
        } else if (tag == DW_TAG_union_type) {
          tag_decl_kind = clang::TTK_Union;
          default_accessibility = eAccessPublic;
        } else if (tag == DW_TAG_class_type) {
          tag_decl_kind = clang::TTK_Class;
          default_accessibility = eAccessPrivate;
        }

        if (byte_size_valid && byte_size == 0 && type_name_cstr &&
            !die.HasChildren() &&
            sc.comp_unit->GetLanguage() == eLanguageTypeObjC) {
          // Work around an issue with clang at the moment where forward
          // declarations for objective C classes are emitted as:
          //  DW_TAG_structure_type [2]
          //  DW_AT_name( "ForwardObjcClass" )
          //  DW_AT_byte_size( 0x00 )
          //  DW_AT_decl_file( "..." )
          //  DW_AT_decl_line( 1 )
          //
          // Note that there is no DW_AT_declaration and there are no children,
          // and the byte size is zero.
          is_forward_declaration = true;
        }

        if (class_language == eLanguageTypeObjC ||
            class_language == eLanguageTypeObjC_plus_plus) {
          if (!is_complete_objc_class &&
              die.Supports_DW_AT_APPLE_objc_complete_type()) {
            // We have a valid eSymbolTypeObjCClass class symbol whose name
            // matches the current objective C class that we are trying to find
            // and this DIE isn't the complete definition (we checked
            // is_complete_objc_class above and know it is false), so the real
            // definition is in here somewhere
            type_sp = dwarf->FindCompleteObjCDefinitionTypeForDIE(
                die, type_name_const_str, true);

            if (!type_sp) {
              SymbolFileDWARFDebugMap *debug_map_symfile =
                  dwarf->GetDebugMapSymfile();
              if (debug_map_symfile) {
                // We weren't able to find a full declaration in this DWARF,
                // see if we have a declaration anywhere else...
                type_sp =
                    debug_map_symfile->FindCompleteObjCDefinitionTypeForDIE(
                        die, type_name_const_str, true);
              }
            }

            if (type_sp) {
              if (log) {
                dwarf->GetObjectFile()->GetModule()->LogMessage(
                    log, "SymbolFileDWARF(%p) - 0x%8.8x: %s type \"%s\" is an "
                         "incomplete objc type, complete type is 0x%8.8" PRIx64,
                    static_cast<void *>(this), die.GetOffset(),
                    DW_TAG_value_to_name(tag), type_name_cstr,
                    type_sp->GetID());
              }

              // We found a real definition for this type elsewhere so lets use
              // it and cache the fact that we found a complete type for this
              // die
              dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
              return type_sp;
            }
          }
        }

        if (is_forward_declaration) {
          // We have a forward declaration to a type and we need to try and
          // find a full declaration. We look in the current type index just in
          // case we have a forward declaration followed by an actual
          // declarations in the DWARF. If this fails, we need to look
          // elsewhere...
          if (log) {
            dwarf->GetObjectFile()->GetModule()->LogMessage(
                log, "SymbolFileDWARF(%p) - 0x%8.8x: %s type \"%s\" is a "
                     "forward declaration, trying to find complete type",
                static_cast<void *>(this), die.GetOffset(),
                DW_TAG_value_to_name(tag), type_name_cstr);
          }

          // See if the type comes from a DWO module and if so, track down that
          // type.
          type_sp = ParseTypeFromDWO(die, log);
          if (type_sp)
            return type_sp;

          DWARFDeclContext die_decl_ctx;
          die.GetDWARFDeclContext(die_decl_ctx);

          // type_sp = FindDefinitionTypeForDIE (dwarf_cu, die,
          // type_name_const_str);
          type_sp = dwarf->FindDefinitionTypeForDWARFDeclContext(die_decl_ctx);

          if (!type_sp) {
            SymbolFileDWARFDebugMap *debug_map_symfile =
                dwarf->GetDebugMapSymfile();
            if (debug_map_symfile) {
              // We weren't able to find a full declaration in this DWARF, see
              // if we have a declaration anywhere else...
              type_sp =
                  debug_map_symfile->FindDefinitionTypeForDWARFDeclContext(
                      die_decl_ctx);
            }
          }

          if (type_sp) {
            if (log) {
              dwarf->GetObjectFile()->GetModule()->LogMessage(
                  log, "SymbolFileDWARF(%p) - 0x%8.8x: %s type \"%s\" is a "
                       "forward declaration, complete type is 0x%8.8" PRIx64,
                  static_cast<void *>(this), die.GetOffset(),
                  DW_TAG_value_to_name(tag), type_name_cstr, type_sp->GetID());
            }

            // We found a real definition for this type elsewhere so lets use
            // it and cache the fact that we found a complete type for this die
            dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
            clang::DeclContext *defn_decl_ctx = GetCachedClangDeclContextForDIE(
                dwarf->DebugInfo()->GetDIE(DIERef(type_sp->GetID(), dwarf)));
            if (defn_decl_ctx)
              LinkDeclContextToDIE(defn_decl_ctx, die);
            return type_sp;
          }
        }
        assert(tag_decl_kind != -1);
        bool clang_type_was_created = false;
        clang_type.SetCompilerType(
            &m_ast, dwarf->GetForwardDeclDieToClangType().lookup(die.GetDIE()));
        if (!clang_type) {
          clang::DeclContext *decl_ctx =
              GetClangDeclContextContainingDIE(die, nullptr);

          // If your decl context is a record that was imported from another
          // AST context (in the gmodules case), we need to make sure the type
          // backing the Decl is complete before adding children to it. This is
          // not an issue in the non-gmodules case because the debug info will
          // always contain a full definition of parent types in that case.
          CompleteExternalTagDeclType(GetClangASTImporter(), decl_ctx, die,
                                      type_name_cstr);

          if (accessibility == eAccessNone && decl_ctx) {
            // Check the decl context that contains this class/struct/union. If
            // it is a class we must give it an accessibility.
            const clang::Decl::Kind containing_decl_kind =
                decl_ctx->getDeclKind();
            if (DeclKindIsCXXClass(containing_decl_kind))
              accessibility = default_accessibility;
          }

          ClangASTMetadata metadata;
          metadata.SetUserID(die.GetID());
          metadata.SetIsDynamicCXXType(dwarf->ClassOrStructIsVirtual(die));

          if (type_name_cstr && strchr(type_name_cstr, '<')) {
            ClangASTContext::TemplateParameterInfos template_param_infos;
            if (ParseTemplateParameterInfos(die, template_param_infos)) {
              clang::ClassTemplateDecl *class_template_decl =
                  m_ast.ParseClassTemplateDecl(decl_ctx, accessibility,
                                               type_name_cstr, tag_decl_kind,
                                               template_param_infos);
              if (!class_template_decl) {
                if (log) {
                  dwarf->GetObjectFile()->GetModule()->LogMessage(
                    log, "SymbolFileDWARF(%p) - 0x%8.8x: %s type \"%s\" "
                         "clang::ClassTemplateDecl failed to return a decl.",
                    static_cast<void *>(this), die.GetOffset(),
                    DW_TAG_value_to_name(tag), type_name_cstr);
                }
                return TypeSP();
              }
                
              clang::ClassTemplateSpecializationDecl
                  *class_specialization_decl =
                      m_ast.CreateClassTemplateSpecializationDecl(
                          decl_ctx, class_template_decl, tag_decl_kind,
                          template_param_infos);
              clang_type = m_ast.CreateClassTemplateSpecializationType(
                  class_specialization_decl);
              clang_type_was_created = true;

              m_ast.SetMetadata(class_template_decl, metadata);
              m_ast.SetMetadata(class_specialization_decl, metadata);
            }
          }

          if (!clang_type_was_created) {
            clang_type_was_created = true;
            clang_type = m_ast.CreateRecordType(decl_ctx, accessibility,
                                                type_name_cstr, tag_decl_kind,
                                                class_language, &metadata);
          }
        }
        
        // Store a forward declaration to this class type in case any
        // parameters in any class methods need it for the clang types for
        // function prototypes.
        LinkDeclContextToDIE(m_ast.GetDeclContextForType(clang_type), die);
        type_sp.reset(new Type(die.GetID(), dwarf, type_name_const_str,
                               byte_size, NULL, LLDB_INVALID_UID,
                               Type::eEncodingIsUID, &decl, clang_type,
                               Type::eResolveStateForward));

        type_sp->SetIsCompleteObjCClass(is_complete_objc_class);

        // Add our type to the unique type map so we don't end up creating many
        // copies of the same type over and over in the ASTContext for our
        // module
        unique_ast_entry_ap->m_type_sp = type_sp;
        unique_ast_entry_ap->m_die = die;
        unique_ast_entry_ap->m_declaration = unique_decl;
        unique_ast_entry_ap->m_byte_size = byte_size;
        dwarf->GetUniqueDWARFASTTypeMap().Insert(unique_typename,
                                                 *unique_ast_entry_ap);

        if (is_forward_declaration && die.HasChildren()) {
          // Check to see if the DIE actually has a definition, some version of
          // GCC will
          // emit DIEs with DW_AT_declaration set to true, but yet still have
          // subprogram, members, or inheritance, so we can't trust it
          DWARFDIE child_die = die.GetFirstChild();
          while (child_die) {
            switch (child_die.Tag()) {
            case DW_TAG_inheritance:
            case DW_TAG_subprogram:
            case DW_TAG_member:
            case DW_TAG_APPLE_property:
            case DW_TAG_class_type:
            case DW_TAG_structure_type:
            case DW_TAG_enumeration_type:
            case DW_TAG_typedef:
            case DW_TAG_union_type:
              child_die.Clear();
              is_forward_declaration = false;
              break;
            default:
              child_die = child_die.GetSibling();
              break;
            }
          }
        }

        if (!is_forward_declaration) {
          // Always start the definition for a class type so that if the class
          // has child classes or types that require the class to be created
          // for use as their decl contexts the class will be ready to accept
          // these child definitions.
          if (!die.HasChildren()) {
            // No children for this struct/union/class, lets finish it
            if (ClangASTContext::StartTagDeclarationDefinition(clang_type)) {
              ClangASTContext::CompleteTagDeclarationDefinition(clang_type);
            } else {
              dwarf->GetObjectFile()->GetModule()->ReportError(
                  "DWARF DIE at 0x%8.8x named \"%s\" was not able to start its "
                  "definition.\nPlease file a bug and attach the file at the "
                  "start of this error message",
                  die.GetOffset(), type_name_cstr);
            }

            if (tag == DW_TAG_structure_type) // this only applies in C
            {
              clang::RecordDecl *record_decl =
                  ClangASTContext::GetAsRecordDecl(clang_type);

              if (record_decl) {
                GetClangASTImporter().InsertRecordDecl(
                    record_decl, ClangASTImporter::LayoutInfo());
              }
            }
          } else if (clang_type_was_created) {
            // Start the definition if the class is not objective C since the
            // underlying decls respond to isCompleteDefinition(). Objective
            // C decls don't respond to isCompleteDefinition() so we can't
            // start the declaration definition right away. For C++
            // class/union/structs we want to start the definition in case the
            // class is needed as the declaration context for a contained class
            // or type without the need to complete that type..

            if (class_language != eLanguageTypeObjC &&
                class_language != eLanguageTypeObjC_plus_plus)
              ClangASTContext::StartTagDeclarationDefinition(clang_type);

            // Leave this as a forward declaration until we need to know the
            // details of the type. lldb_private::Type will automatically call
            // the SymbolFile virtual function
            // "SymbolFileDWARF::CompleteType(Type *)" When the definition
            // needs to be defined.
            assert(!dwarf->GetForwardDeclClangTypeToDie().count(
                       ClangUtil::RemoveFastQualifiers(clang_type)
                           .GetOpaqueQualType()) &&
                   "Type already in the forward declaration map!");
            // Can't assume m_ast.GetSymbolFile() is actually a
            // SymbolFileDWARF, it can be a SymbolFileDWARFDebugMap for Apple
            // binaries.
            dwarf->GetForwardDeclDieToClangType()[die.GetDIE()] =
                clang_type.GetOpaqueQualType();
            dwarf->GetForwardDeclClangTypeToDie()
                [ClangUtil::RemoveFastQualifiers(clang_type)
                     .GetOpaqueQualType()] = die.GetDIERef();
            m_ast.SetHasExternalStorage(clang_type.GetOpaqueQualType(), true);
          }
        }
        
        // If we made a clang type, set the trivial abi if applicable: We only
        // do this for pass by value - which implies the Trivial ABI. There
        // isn't a way to assert that something that would normally be pass by
        // value is pass by reference, so we ignore that attribute if set.
        if (calling_convention == llvm::dwarf::DW_CC_pass_by_value) {
          clang::CXXRecordDecl *record_decl =
                  m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
          if (record_decl) {
            record_decl->setHasTrivialSpecialMemberForCall();
          }
        }

      } break;

      case DW_TAG_enumeration_type: {
        // Set a bit that lets us know that we are currently parsing this
        dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;

        bool is_scoped = false;
        DWARFFormValue encoding_form;

        const size_t num_attributes = die.GetAttributes(attributes);
        if (num_attributes > 0) {
          uint32_t i;

          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_decl_file:
                decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                    form_value.Unsigned()));
                break;
              case DW_AT_decl_line:
                decl.SetLine(form_value.Unsigned());
                break;
              case DW_AT_decl_column:
                decl.SetColumn(form_value.Unsigned());
                break;
              case DW_AT_name:
                type_name_cstr = form_value.AsCString();
                type_name_const_str.SetCString(type_name_cstr);
                break;
              case DW_AT_type:
                encoding_form = form_value;
                break;
              case DW_AT_byte_size:
                byte_size = form_value.Unsigned();
                break;
              case DW_AT_accessibility:
                break; // accessibility =
                       // DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
              case DW_AT_declaration:
                is_forward_declaration = form_value.Boolean();
                break;
              case DW_AT_enum_class:
                is_scoped = form_value.Boolean();
                break;
              case DW_AT_allocated:
              case DW_AT_associated:
              case DW_AT_bit_stride:
              case DW_AT_byte_stride:
              case DW_AT_data_location:
              case DW_AT_description:
              case DW_AT_start_scope:
              case DW_AT_visibility:
              case DW_AT_specification:
              case DW_AT_abstract_origin:
              case DW_AT_sibling:
                break;
              }
            }
          }

          if (is_forward_declaration) {
            type_sp = ParseTypeFromDWO(die, log);
            if (type_sp)
              return type_sp;

            DWARFDeclContext die_decl_ctx;
            die.GetDWARFDeclContext(die_decl_ctx);

            type_sp =
                dwarf->FindDefinitionTypeForDWARFDeclContext(die_decl_ctx);

            if (!type_sp) {
              SymbolFileDWARFDebugMap *debug_map_symfile =
                  dwarf->GetDebugMapSymfile();
              if (debug_map_symfile) {
                // We weren't able to find a full declaration in this DWARF,
                // see if we have a declaration anywhere else...
                type_sp =
                    debug_map_symfile->FindDefinitionTypeForDWARFDeclContext(
                        die_decl_ctx);
              }
            }

            if (type_sp) {
              if (log) {
                dwarf->GetObjectFile()->GetModule()->LogMessage(
                    log, "SymbolFileDWARF(%p) - 0x%8.8x: %s type \"%s\" is a "
                         "forward declaration, complete type is 0x%8.8" PRIx64,
                    static_cast<void *>(this), die.GetOffset(),
                    DW_TAG_value_to_name(tag), type_name_cstr,
                    type_sp->GetID());
              }

              // We found a real definition for this type elsewhere so lets use
              // it and cache the fact that we found a complete type for this
              // die
              dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
              clang::DeclContext *defn_decl_ctx =
                  GetCachedClangDeclContextForDIE(dwarf->DebugInfo()->GetDIE(
                      DIERef(type_sp->GetID(), dwarf)));
              if (defn_decl_ctx)
                LinkDeclContextToDIE(defn_decl_ctx, die);
              return type_sp;
            }
          }
          DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
                       DW_TAG_value_to_name(tag), type_name_cstr);

          CompilerType enumerator_clang_type;
          clang_type.SetCompilerType(
              &m_ast,
              dwarf->GetForwardDeclDieToClangType().lookup(die.GetDIE()));
          if (!clang_type) {
            if (encoding_form.IsValid()) {
              Type *enumerator_type =
                  dwarf->ResolveTypeUID(DIERef(encoding_form));
              if (enumerator_type)
                enumerator_clang_type = enumerator_type->GetFullCompilerType();
            }

            if (!enumerator_clang_type) {
              if (byte_size > 0) {
                enumerator_clang_type =
                    m_ast.GetBuiltinTypeForDWARFEncodingAndBitSize(
                        NULL, DW_ATE_signed, byte_size * 8);
              } else {
                enumerator_clang_type = m_ast.GetBasicType(eBasicTypeInt);
              }
            }

            clang_type = m_ast.CreateEnumerationType(
                type_name_cstr, GetClangDeclContextContainingDIE(die, nullptr),
                decl, enumerator_clang_type, is_scoped);
          } else {
            enumerator_clang_type =
                m_ast.GetEnumerationIntegerType(clang_type.GetOpaqueQualType());
          }

          LinkDeclContextToDIE(
              ClangASTContext::GetDeclContextForType(clang_type), die);

          type_sp.reset(new Type(
              die.GetID(), dwarf, type_name_const_str, byte_size, NULL,
              DIERef(encoding_form).GetUID(dwarf), Type::eEncodingIsUID, &decl,
              clang_type, Type::eResolveStateForward));

          if (ClangASTContext::StartTagDeclarationDefinition(clang_type)) {
            if (die.HasChildren()) {
              SymbolContext cu_sc(die.GetLLDBCompileUnit());
              bool is_signed = false;
              enumerator_clang_type.IsIntegerType(is_signed);
              ParseChildEnumerators(cu_sc, clang_type, is_signed,
                                    type_sp->GetByteSize(), die);
            }
            ClangASTContext::CompleteTagDeclarationDefinition(clang_type);
          } else {
            dwarf->GetObjectFile()->GetModule()->ReportError(
                "DWARF DIE at 0x%8.8x named \"%s\" was not able to start its "
                "definition.\nPlease file a bug and attach the file at the "
                "start of this error message",
                die.GetOffset(), type_name_cstr);
          }
        }
      } break;

      case DW_TAG_inlined_subroutine:
      case DW_TAG_subprogram:
      case DW_TAG_subroutine_type: {
        // Set a bit that lets us know that we are currently parsing this
        dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;

        DWARFFormValue type_die_form;
        bool is_variadic = false;
        bool is_inline = false;
        bool is_static = false;
        bool is_virtual = false;
        bool is_explicit = false;
        bool is_artificial = false;
        bool has_template_params = false;
        DWARFFormValue specification_die_form;
        DWARFFormValue abstract_origin_die_form;
        dw_offset_t object_pointer_die_offset = DW_INVALID_OFFSET;

        unsigned type_quals = 0;
        clang::StorageClass storage =
            clang::SC_None; //, Extern, Static, PrivateExtern

        const size_t num_attributes = die.GetAttributes(attributes);
        if (num_attributes > 0) {
          uint32_t i;
          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_decl_file:
                decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                    form_value.Unsigned()));
                break;
              case DW_AT_decl_line:
                decl.SetLine(form_value.Unsigned());
                break;
              case DW_AT_decl_column:
                decl.SetColumn(form_value.Unsigned());
                break;
              case DW_AT_name:
                type_name_cstr = form_value.AsCString();
                type_name_const_str.SetCString(type_name_cstr);
                break;

              case DW_AT_linkage_name:
              case DW_AT_MIPS_linkage_name:
                mangled_name_cstr = form_value.AsCString();
                break;
              case DW_AT_type:
                type_die_form = form_value;
                break;
              case DW_AT_accessibility:
                accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned());
                break;
              case DW_AT_declaration:
                break; // is_forward_declaration = form_value.Boolean(); break;
              case DW_AT_inline:
                is_inline = form_value.Boolean();
                break;
              case DW_AT_virtuality:
                is_virtual = form_value.Boolean();
                break;
              case DW_AT_explicit:
                is_explicit = form_value.Boolean();
                break;
              case DW_AT_artificial:
                is_artificial = form_value.Boolean();
                break;

              case DW_AT_external:
                if (form_value.Unsigned()) {
                  if (storage == clang::SC_None)
                    storage = clang::SC_Extern;
                  else
                    storage = clang::SC_PrivateExtern;
                }
                break;

              case DW_AT_specification:
                specification_die_form = form_value;
                break;

              case DW_AT_abstract_origin:
                abstract_origin_die_form = form_value;
                break;

              case DW_AT_object_pointer:
                object_pointer_die_offset = form_value.Reference();
                break;

              case DW_AT_allocated:
              case DW_AT_associated:
              case DW_AT_address_class:
              case DW_AT_calling_convention:
              case DW_AT_data_location:
              case DW_AT_elemental:
              case DW_AT_entry_pc:
              case DW_AT_frame_base:
              case DW_AT_high_pc:
              case DW_AT_low_pc:
              case DW_AT_prototyped:
              case DW_AT_pure:
              case DW_AT_ranges:
              case DW_AT_recursive:
              case DW_AT_return_addr:
              case DW_AT_segment:
              case DW_AT_start_scope:
              case DW_AT_static_link:
              case DW_AT_trampoline:
              case DW_AT_visibility:
              case DW_AT_vtable_elem_location:
              case DW_AT_description:
              case DW_AT_sibling:
                break;
              }
            }
          }
        }

        std::string object_pointer_name;
        if (object_pointer_die_offset != DW_INVALID_OFFSET) {
          DWARFDIE object_pointer_die = die.GetDIE(object_pointer_die_offset);
          if (object_pointer_die) {
            const char *object_pointer_name_cstr = object_pointer_die.GetName();
            if (object_pointer_name_cstr)
              object_pointer_name = object_pointer_name_cstr;
          }
        }

        DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
                     DW_TAG_value_to_name(tag), type_name_cstr);

        CompilerType return_clang_type;
        Type *func_type = NULL;

        if (type_die_form.IsValid())
          func_type = dwarf->ResolveTypeUID(DIERef(type_die_form));

        if (func_type)
          return_clang_type = func_type->GetForwardCompilerType();
        else
          return_clang_type = m_ast.GetBasicType(eBasicTypeVoid);

        std::vector<CompilerType> function_param_types;
        std::vector<clang::ParmVarDecl *> function_param_decls;

        // Parse the function children for the parameters

        DWARFDIE decl_ctx_die;
        clang::DeclContext *containing_decl_ctx =
            GetClangDeclContextContainingDIE(die, &decl_ctx_die);
        const clang::Decl::Kind containing_decl_kind =
            containing_decl_ctx->getDeclKind();

        bool is_cxx_method = DeclKindIsCXXClass(containing_decl_kind);
        // Start off static. This will be set to false in
        // ParseChildParameters(...) if we find a "this" parameters as the
        // first parameter
        if (is_cxx_method) {
          is_static = true;
        }

        if (die.HasChildren()) {
          bool skip_artificial = true;
          ParseChildParameters(*sc.comp_unit, containing_decl_ctx, die,
                               skip_artificial, is_static, is_variadic,
                               has_template_params, function_param_types,
                               function_param_decls, type_quals);
        }

        bool ignore_containing_context = false;
        // Check for templatized class member functions. If we had any
        // DW_TAG_template_type_parameter or DW_TAG_template_value_parameter
        // the DW_TAG_subprogram DIE, then we can't let this become a method in
        // a class. Why? Because templatized functions are only emitted if one
        // of the templatized methods is used in the current compile unit and
        // we will end up with classes that may or may not include these member
        // functions and this means one class won't match another class
        // definition and it affects our ability to use a class in the clang
        // expression parser. So for the greater good, we currently must not
        // allow any template member functions in a class definition.
        if (is_cxx_method && has_template_params) {
          ignore_containing_context = true;
          is_cxx_method = false;
        }

        // clang_type will get the function prototype clang type after this
        // call
        clang_type = m_ast.CreateFunctionType(
            return_clang_type, function_param_types.data(),
            function_param_types.size(), is_variadic, type_quals);

        if (type_name_cstr) {
          bool type_handled = false;
          if (tag == DW_TAG_subprogram || tag == DW_TAG_inlined_subroutine) {
            ObjCLanguage::MethodName objc_method(type_name_cstr, true);
            if (objc_method.IsValid(true)) {
              CompilerType class_opaque_type;
              ConstString class_name(objc_method.GetClassName());
              if (class_name) {
                TypeSP complete_objc_class_type_sp(
                    dwarf->FindCompleteObjCDefinitionTypeForDIE(
                        DWARFDIE(), class_name, false));

                if (complete_objc_class_type_sp) {
                  CompilerType type_clang_forward_type =
                      complete_objc_class_type_sp->GetForwardCompilerType();
                  if (ClangASTContext::IsObjCObjectOrInterfaceType(
                          type_clang_forward_type))
                    class_opaque_type = type_clang_forward_type;
                }
              }

              if (class_opaque_type) {
                // If accessibility isn't set to anything valid, assume public
                // for now...
                if (accessibility == eAccessNone)
                  accessibility = eAccessPublic;

                clang::ObjCMethodDecl *objc_method_decl =
                    m_ast.AddMethodToObjCObjectType(
                        class_opaque_type, type_name_cstr, clang_type,
                        accessibility, is_artificial, is_variadic);
                type_handled = objc_method_decl != NULL;
                if (type_handled) {
                  LinkDeclContextToDIE(
                      ClangASTContext::GetAsDeclContext(objc_method_decl), die);
                  m_ast.SetMetadataAsUserID(objc_method_decl, die.GetID());
                } else {
                  dwarf->GetObjectFile()->GetModule()->ReportError(
                      "{0x%8.8x}: invalid Objective-C method 0x%4.4x (%s), "
                      "please file a bug and attach the file at the start of "
                      "this error message",
                      die.GetOffset(), tag, DW_TAG_value_to_name(tag));
                }
              }
            } else if (is_cxx_method) {
              // Look at the parent of this DIE and see if is is a class or
              // struct and see if this is actually a C++ method
              Type *class_type = dwarf->ResolveType(decl_ctx_die);
              if (class_type) {
                bool alternate_defn = false;
                if (class_type->GetID() != decl_ctx_die.GetID() ||
                    decl_ctx_die.GetContainingDWOModuleDIE()) {
                  alternate_defn = true;

                  // We uniqued the parent class of this function to another
                  // class so we now need to associate all dies under
                  // "decl_ctx_die" to DIEs in the DIE for "class_type"...
                  SymbolFileDWARF *class_symfile = NULL;
                  DWARFDIE class_type_die;

                  SymbolFileDWARFDebugMap *debug_map_symfile =
                      dwarf->GetDebugMapSymfile();
                  if (debug_map_symfile) {
                    class_symfile = debug_map_symfile->GetSymbolFileByOSOIndex(
                        SymbolFileDWARFDebugMap::GetOSOIndexFromUserID(
                            class_type->GetID()));
                    class_type_die = class_symfile->DebugInfo()->GetDIE(
                        DIERef(class_type->GetID(), dwarf));
                  } else {
                    class_symfile = dwarf;
                    class_type_die = dwarf->DebugInfo()->GetDIE(
                        DIERef(class_type->GetID(), dwarf));
                  }
                  if (class_type_die) {
                    DWARFDIECollection failures;

                    CopyUniqueClassMethodTypes(decl_ctx_die, class_type_die,
                                               class_type, failures);

                    // FIXME do something with these failures that's smarter
                    // than
                    // just dropping them on the ground.  Unfortunately classes
                    // don't like having stuff added to them after their
                    // definitions are complete...

                    type_ptr = dwarf->GetDIEToType()[die.GetDIE()];
                    if (type_ptr && type_ptr != DIE_IS_BEING_PARSED) {
                      type_sp = type_ptr->shared_from_this();
                      break;
                    }
                  }
                }

                if (specification_die_form.IsValid()) {
                  // We have a specification which we are going to base our
                  // function prototype off of, so we need this type to be
                  // completed so that the m_die_to_decl_ctx for the method in
                  // the specification has a valid clang decl context.
                  class_type->GetForwardCompilerType();
                  // If we have a specification, then the function type should
                  // have been made with the specification and not with this
                  // die.
                  DWARFDIE spec_die = dwarf->DebugInfo()->GetDIE(
                      DIERef(specification_die_form));
                  clang::DeclContext *spec_clang_decl_ctx =
                      GetClangDeclContextForDIE(spec_die);
                  if (spec_clang_decl_ctx) {
                    LinkDeclContextToDIE(spec_clang_decl_ctx, die);
                  } else {
                    dwarf->GetObjectFile()->GetModule()->ReportWarning(
                        "0x%8.8" PRIx64 ": DW_AT_specification(0x%8.8" PRIx64
                        ") has no decl\n",
                        die.GetID(), specification_die_form.Reference());
                  }
                  type_handled = true;
                } else if (abstract_origin_die_form.IsValid()) {
                  // We have a specification which we are going to base our
                  // function prototype off of, so we need this type to be
                  // completed so that the m_die_to_decl_ctx for the method in
                  // the abstract origin has a valid clang decl context.
                  class_type->GetForwardCompilerType();

                  DWARFDIE abs_die = dwarf->DebugInfo()->GetDIE(
                      DIERef(abstract_origin_die_form));
                  clang::DeclContext *abs_clang_decl_ctx =
                      GetClangDeclContextForDIE(abs_die);
                  if (abs_clang_decl_ctx) {
                    LinkDeclContextToDIE(abs_clang_decl_ctx, die);
                  } else {
                    dwarf->GetObjectFile()->GetModule()->ReportWarning(
                        "0x%8.8" PRIx64 ": DW_AT_abstract_origin(0x%8.8" PRIx64
                        ") has no decl\n",
                        die.GetID(), abstract_origin_die_form.Reference());
                  }
                  type_handled = true;
                } else {
                  CompilerType class_opaque_type =
                      class_type->GetForwardCompilerType();
                  if (ClangASTContext::IsCXXClassType(class_opaque_type)) {
                    if (class_opaque_type.IsBeingDefined() || alternate_defn) {
                      if (!is_static && !die.HasChildren()) {
                        // We have a C++ member function with no children (this
                        // pointer!) and clang will get mad if we try and make
                        // a function that isn't well formed in the DWARF, so
                        // we will just skip it...
                        type_handled = true;
                      } else {
                        bool add_method = true;
                        if (alternate_defn) {
                          // If an alternate definition for the class exists,
                          // then add the method only if an equivalent is not
                          // already present.
                          clang::CXXRecordDecl *record_decl =
                              m_ast.GetAsCXXRecordDecl(
                                  class_opaque_type.GetOpaqueQualType());
                          if (record_decl) {
                            for (auto method_iter = record_decl->method_begin();
                                 method_iter != record_decl->method_end();
                                 method_iter++) {
                              clang::CXXMethodDecl *method_decl = *method_iter;
                              if (method_decl->getNameInfo().getAsString() ==
                                  std::string(type_name_cstr)) {
                                if (method_decl->getType() ==
                                    ClangUtil::GetQualType(clang_type)) {
                                  add_method = false;
                                  LinkDeclContextToDIE(
                                      ClangASTContext::GetAsDeclContext(
                                          method_decl),
                                      die);
                                  type_handled = true;

                                  break;
                                }
                              }
                            }
                          }
                        }

                        if (add_method) {
                          llvm::PrettyStackTraceFormat stack_trace(
                              "SymbolFileDWARF::ParseType() is adding a method "
                              "%s to class %s in DIE 0x%8.8" PRIx64 " from %s",
                              type_name_cstr,
                              class_type->GetName().GetCString(), die.GetID(),
                              dwarf->GetObjectFile()
                                  ->GetFileSpec()
                                  .GetPath()
                                  .c_str());

                          const bool is_attr_used = false;
                          // Neither GCC 4.2 nor clang++ currently set a valid
                          // accessibility in the DWARF for C++ methods...
                          // Default to public for now...
                          if (accessibility == eAccessNone)
                            accessibility = eAccessPublic;

                          clang::CXXMethodDecl *cxx_method_decl =
                              m_ast.AddMethodToCXXRecordType(
                                  class_opaque_type.GetOpaqueQualType(),
                                  type_name_cstr, mangled_name_cstr, clang_type,
                                  accessibility, is_virtual, is_static,
                                  is_inline, is_explicit, is_attr_used,
                                  is_artificial);

                          type_handled = cxx_method_decl != NULL;

                          if (type_handled) {
                            LinkDeclContextToDIE(
                                ClangASTContext::GetAsDeclContext(
                                    cxx_method_decl),
                                die);

                            ClangASTMetadata metadata;
                            metadata.SetUserID(die.GetID());

                            if (!object_pointer_name.empty()) {
                              metadata.SetObjectPtrName(
                                  object_pointer_name.c_str());
                              if (log)
                                log->Printf(
                                    "Setting object pointer name: %s on method "
                                    "object %p.\n",
                                    object_pointer_name.c_str(),
                                    static_cast<void *>(cxx_method_decl));
                            }
                            m_ast.SetMetadata(cxx_method_decl, metadata);
                          } else {
                            ignore_containing_context = true;
                          }
                        }
                      }
                    } else {
                      // We were asked to parse the type for a method in a
                      // class, yet the class hasn't been asked to complete
                      // itself through the clang::ExternalASTSource protocol,
                      // so we need to just have the class complete itself and
                      // do things the right way, then our
                      // DIE should then have an entry in the
                      // dwarf->GetDIEToType() map. First
                      // we need to modify the dwarf->GetDIEToType() so it
                      // doesn't think we are trying to parse this DIE
                      // anymore...
                      dwarf->GetDIEToType()[die.GetDIE()] = NULL;

                      // Now we get the full type to force our class type to
                      // complete itself using the clang::ExternalASTSource
                      // protocol which will parse all base classes and all
                      // methods (including the method for this DIE).
                      class_type->GetFullCompilerType();

                      // The type for this DIE should have been filled in the
                      // function call above
                      type_ptr = dwarf->GetDIEToType()[die.GetDIE()];
                      if (type_ptr && type_ptr != DIE_IS_BEING_PARSED) {
                        type_sp = type_ptr->shared_from_this();
                        break;
                      }

                      // FIXME This is fixing some even uglier behavior but we
                      // really need to
                      // uniq the methods of each class as well as the class
                      // itself. <rdar://problem/11240464>
                      type_handled = true;
                    }
                  }
                }
              }
            }
          }

          if (!type_handled) {
            clang::FunctionDecl *function_decl = nullptr;

            if (abstract_origin_die_form.IsValid()) {
              DWARFDIE abs_die =
                  dwarf->DebugInfo()->GetDIE(DIERef(abstract_origin_die_form));

              SymbolContext sc;

              if (dwarf->ResolveType(abs_die)) {
                function_decl = llvm::dyn_cast_or_null<clang::FunctionDecl>(
                    GetCachedClangDeclContextForDIE(abs_die));

                if (function_decl) {
                  LinkDeclContextToDIE(function_decl, die);
                }
              }
            }

            if (!function_decl) {
              // We just have a function that isn't part of a class
              function_decl = m_ast.CreateFunctionDeclaration(
                  ignore_containing_context ? m_ast.GetTranslationUnitDecl()
                                            : containing_decl_ctx,
                  type_name_cstr, clang_type, storage, is_inline);

              if (has_template_params) {
                ClangASTContext::TemplateParameterInfos template_param_infos;
                ParseTemplateParameterInfos(die, template_param_infos);
                clang::FunctionTemplateDecl *func_template_decl =
                    m_ast.CreateFunctionTemplateDecl(
                        containing_decl_ctx, function_decl, type_name_cstr,
                        template_param_infos);
                m_ast.CreateFunctionTemplateSpecializationInfo(
                    function_decl, func_template_decl, template_param_infos);
              }
              
              lldbassert(function_decl);

              if (function_decl) {
                LinkDeclContextToDIE(function_decl, die);

                if (!function_param_decls.empty())
                  m_ast.SetFunctionParameters(function_decl,
                                              &function_param_decls.front(),
                                              function_param_decls.size());

                ClangASTMetadata metadata;
                metadata.SetUserID(die.GetID());

                if (!object_pointer_name.empty()) {
                  metadata.SetObjectPtrName(object_pointer_name.c_str());
                  if (log)
                    log->Printf("Setting object pointer name: %s on function "
                                "object %p.",
                                object_pointer_name.c_str(),
                                static_cast<void *>(function_decl));
                }
                m_ast.SetMetadata(function_decl, metadata);
              }
            }
          }
        }
        type_sp.reset(new Type(die.GetID(), dwarf, type_name_const_str, 0, NULL,
                               LLDB_INVALID_UID, Type::eEncodingIsUID, &decl,
                               clang_type, Type::eResolveStateFull));
        assert(type_sp.get());
      } break;

      case DW_TAG_array_type: {
        // Set a bit that lets us know that we are currently parsing this
        dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;

        DWARFFormValue type_die_form;
        int64_t first_index = 0;
        uint32_t byte_stride = 0;
        uint32_t bit_stride = 0;
        bool is_vector = false;
        const size_t num_attributes = die.GetAttributes(attributes);

        if (num_attributes > 0) {
          uint32_t i;
          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_decl_file:
                decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                    form_value.Unsigned()));
                break;
              case DW_AT_decl_line:
                decl.SetLine(form_value.Unsigned());
                break;
              case DW_AT_decl_column:
                decl.SetColumn(form_value.Unsigned());
                break;
              case DW_AT_name:
                type_name_cstr = form_value.AsCString();
                type_name_const_str.SetCString(type_name_cstr);
                break;

              case DW_AT_type:
                type_die_form = form_value;
                break;
              case DW_AT_byte_size:
                break; // byte_size = form_value.Unsigned(); break;
              case DW_AT_byte_stride:
                byte_stride = form_value.Unsigned();
                break;
              case DW_AT_bit_stride:
                bit_stride = form_value.Unsigned();
                break;
              case DW_AT_GNU_vector:
                is_vector = form_value.Boolean();
                break;
              case DW_AT_accessibility:
                break; // accessibility =
                       // DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
              case DW_AT_declaration:
                break; // is_forward_declaration = form_value.Boolean(); break;
              case DW_AT_allocated:
              case DW_AT_associated:
              case DW_AT_data_location:
              case DW_AT_description:
              case DW_AT_ordering:
              case DW_AT_start_scope:
              case DW_AT_visibility:
              case DW_AT_specification:
              case DW_AT_abstract_origin:
              case DW_AT_sibling:
                break;
              }
            }
          }

          DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
                       DW_TAG_value_to_name(tag), type_name_cstr);

          DIERef type_die_ref(type_die_form);
          Type *element_type = dwarf->ResolveTypeUID(type_die_ref);

          if (element_type) {
            auto array_info = ParseChildArrayInfo(die);
            if (array_info) {
              first_index = array_info->first_index;
              byte_stride = array_info->byte_stride;
              bit_stride = array_info->bit_stride;
            }
            if (byte_stride == 0 && bit_stride == 0)
              byte_stride = element_type->GetByteSize();
            CompilerType array_element_type =
                element_type->GetForwardCompilerType();

            if (ClangASTContext::IsCXXClassType(array_element_type) &&
                !array_element_type.GetCompleteType()) {
              ModuleSP module_sp = die.GetModule();
              if (module_sp) {
                if (die.GetCU()->GetProducer() == eProducerClang)
                  module_sp->ReportError(
                      "DWARF DW_TAG_array_type DIE at 0x%8.8x has a "
                      "class/union/struct element type DIE 0x%8.8x that is a "
                      "forward declaration, not a complete definition.\nTry "
                      "compiling the source file with -fstandalone-debug or "
                      "disable -gmodules",
                      die.GetOffset(), type_die_ref.die_offset);
                else
                  module_sp->ReportError(
                      "DWARF DW_TAG_array_type DIE at 0x%8.8x has a "
                      "class/union/struct element type DIE 0x%8.8x that is a "
                      "forward declaration, not a complete definition.\nPlease "
                      "file a bug against the compiler and include the "
                      "preprocessed output for %s",
                      die.GetOffset(), type_die_ref.die_offset,
                      die.GetLLDBCompileUnit()
                          ? die.GetLLDBCompileUnit()->GetPath().c_str()
                          : "the source file");
              }

              // We have no choice other than to pretend that the element class
              // type is complete. If we don't do this, clang will crash when
              // trying to layout the class. Since we provide layout
              // assistance, all ivars in this class and other classes will be
              // fine, this is the best we can do short of crashing.
              if (ClangASTContext::StartTagDeclarationDefinition(
                      array_element_type)) {
                ClangASTContext::CompleteTagDeclarationDefinition(
                    array_element_type);
              } else {
                module_sp->ReportError("DWARF DIE at 0x%8.8x was not able to "
                                       "start its definition.\nPlease file a "
                                       "bug and attach the file at the start "
                                       "of this error message",
                                       type_die_ref.die_offset);
              }
            }

            uint64_t array_element_bit_stride = byte_stride * 8 + bit_stride;
            if (array_info && array_info->element_orders.size() > 0) {
              uint64_t num_elements = 0;
              auto end = array_info->element_orders.rend();
              for (auto pos = array_info->element_orders.rbegin(); pos != end;
                   ++pos) {
                num_elements = *pos;
                clang_type = m_ast.CreateArrayType(array_element_type,
                                                   num_elements, is_vector);
                array_element_type = clang_type;
                array_element_bit_stride =
                    num_elements ? array_element_bit_stride * num_elements
                                 : array_element_bit_stride;
              }
            } else {
              clang_type =
                  m_ast.CreateArrayType(array_element_type, 0, is_vector);
            }
            ConstString empty_name;
            type_sp.reset(new Type(
                die.GetID(), dwarf, empty_name, array_element_bit_stride / 8,
                NULL, DIERef(type_die_form).GetUID(dwarf), Type::eEncodingIsUID,
                &decl, clang_type, Type::eResolveStateFull));
            type_sp->SetEncodingType(element_type);
            m_ast.SetMetadataAsUserID(clang_type.GetOpaqueQualType(),
                                      die.GetID());
          }
        }
      } break;

      case DW_TAG_ptr_to_member_type: {
        DWARFFormValue type_die_form;
        DWARFFormValue containing_type_die_form;

        const size_t num_attributes = die.GetAttributes(attributes);

        if (num_attributes > 0) {
          uint32_t i;
          for (i = 0; i < num_attributes; ++i) {
            attr = attributes.AttributeAtIndex(i);
            if (attributes.ExtractFormValueAtIndex(i, form_value)) {
              switch (attr) {
              case DW_AT_type:
                type_die_form = form_value;
                break;
              case DW_AT_containing_type:
                containing_type_die_form = form_value;
                break;
              }
            }
          }

          Type *pointee_type = dwarf->ResolveTypeUID(DIERef(type_die_form));
          Type *class_type =
              dwarf->ResolveTypeUID(DIERef(containing_type_die_form));

          CompilerType pointee_clang_type =
              pointee_type->GetForwardCompilerType();
          CompilerType class_clang_type = class_type->GetLayoutCompilerType();

          clang_type = ClangASTContext::CreateMemberPointerType(
              class_clang_type, pointee_clang_type);

          if (llvm::Optional<uint64_t> clang_type_size =
                  clang_type.GetByteSize(nullptr)) {
            byte_size = *clang_type_size;
            type_sp.reset(new Type(die.GetID(), dwarf, type_name_const_str,
                                   byte_size, NULL, LLDB_INVALID_UID,
                                   Type::eEncodingIsUID, NULL, clang_type,
                                   Type::eResolveStateForward));
          }
        }

        break;
      }
      default:
        dwarf->GetObjectFile()->GetModule()->ReportError(
            "{0x%8.8x}: unhandled type tag 0x%4.4x (%s), please file a bug and "
            "attach the file at the start of this error message",
            die.GetOffset(), tag, DW_TAG_value_to_name(tag));
        break;
      }

      if (type_sp.get()) {
        DWARFDIE sc_parent_die =
            SymbolFileDWARF::GetParentSymbolContextDIE(die);
        dw_tag_t sc_parent_tag = sc_parent_die.Tag();

        SymbolContextScope *symbol_context_scope = NULL;
        if (sc_parent_tag == DW_TAG_compile_unit ||
            sc_parent_tag == DW_TAG_partial_unit) {
          symbol_context_scope = sc.comp_unit;
        } else if (sc.function != NULL && sc_parent_die) {
          symbol_context_scope =
              sc.function->GetBlock(true).FindBlockByID(sc_parent_die.GetID());
          if (symbol_context_scope == NULL)
            symbol_context_scope = sc.function;
        }

        if (symbol_context_scope != NULL) {
          type_sp->SetSymbolContextScope(symbol_context_scope);
        }

        // We are ready to put this type into the uniqued list up at the module
        // level
        type_list->Insert(type_sp);

        dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
      }
    } else if (type_ptr != DIE_IS_BEING_PARSED) {
      type_sp = type_ptr->shared_from_this();
    }
  }
  return type_sp;
}

// DWARF parsing functions

class DWARFASTParserClang::DelayedAddObjCClassProperty {
public:
  DelayedAddObjCClassProperty(
      const CompilerType &class_opaque_type, const char *property_name,
      const CompilerType &property_opaque_type, // The property type is only
                                                // required if you don't have an
                                                // ivar decl
      clang::ObjCIvarDecl *ivar_decl, const char *property_setter_name,
      const char *property_getter_name, uint32_t property_attributes,
      const ClangASTMetadata *metadata)
      : m_class_opaque_type(class_opaque_type), m_property_name(property_name),
        m_property_opaque_type(property_opaque_type), m_ivar_decl(ivar_decl),
        m_property_setter_name(property_setter_name),
        m_property_getter_name(property_getter_name),
        m_property_attributes(property_attributes) {
    if (metadata != NULL) {
      m_metadata_ap.reset(new ClangASTMetadata());
      *m_metadata_ap = *metadata;
    }
  }

  DelayedAddObjCClassProperty(const DelayedAddObjCClassProperty &rhs) {
    *this = rhs;
  }

  DelayedAddObjCClassProperty &
  operator=(const DelayedAddObjCClassProperty &rhs) {
    m_class_opaque_type = rhs.m_class_opaque_type;
    m_property_name = rhs.m_property_name;
    m_property_opaque_type = rhs.m_property_opaque_type;
    m_ivar_decl = rhs.m_ivar_decl;
    m_property_setter_name = rhs.m_property_setter_name;
    m_property_getter_name = rhs.m_property_getter_name;
    m_property_attributes = rhs.m_property_attributes;

    if (rhs.m_metadata_ap.get()) {
      m_metadata_ap.reset(new ClangASTMetadata());
      *m_metadata_ap = *rhs.m_metadata_ap;
    }
    return *this;
  }

  bool Finalize() {
    return ClangASTContext::AddObjCClassProperty(
        m_class_opaque_type, m_property_name, m_property_opaque_type,
        m_ivar_decl, m_property_setter_name, m_property_getter_name,
        m_property_attributes, m_metadata_ap.get());
  }

private:
  CompilerType m_class_opaque_type;
  const char *m_property_name;
  CompilerType m_property_opaque_type;
  clang::ObjCIvarDecl *m_ivar_decl;
  const char *m_property_setter_name;
  const char *m_property_getter_name;
  uint32_t m_property_attributes;
  std::unique_ptr<ClangASTMetadata> m_metadata_ap;
};

bool DWARFASTParserClang::ParseTemplateDIE(
    const DWARFDIE &die,
    ClangASTContext::TemplateParameterInfos &template_param_infos) {
  const dw_tag_t tag = die.Tag();
  bool is_template_template_argument = false;

  switch (tag) {
  case DW_TAG_GNU_template_parameter_pack: {
    template_param_infos.packed_args.reset(
      new ClangASTContext::TemplateParameterInfos);
    for (DWARFDIE child_die = die.GetFirstChild(); child_die.IsValid();
         child_die = child_die.GetSibling()) {
      if (!ParseTemplateDIE(child_die, *template_param_infos.packed_args))
        return false;
    }
    if (const char *name = die.GetName()) {
      template_param_infos.pack_name = name;
    }
    return true;
  }
  case DW_TAG_GNU_template_template_param:
    is_template_template_argument = true;
    LLVM_FALLTHROUGH;
  case DW_TAG_template_type_parameter:
  case DW_TAG_template_value_parameter: {
    DWARFAttributes attributes;
    const size_t num_attributes = die.GetAttributes(attributes);
    const char *name = nullptr;
    const char *template_name = nullptr;
    CompilerType clang_type;
    uint64_t uval64 = 0;
    bool uval64_valid = false;
    if (num_attributes > 0) {
      DWARFFormValue form_value;
      for (size_t i = 0; i < num_attributes; ++i) {
        const dw_attr_t attr = attributes.AttributeAtIndex(i);

        switch (attr) {
        case DW_AT_name:
          if (attributes.ExtractFormValueAtIndex(i, form_value))
            name = form_value.AsCString();
          break;

        case DW_AT_GNU_template_name:
          if (attributes.ExtractFormValueAtIndex(i, form_value))
            template_name = form_value.AsCString();
          break;

        case DW_AT_type:
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            Type *lldb_type = die.ResolveTypeUID(DIERef(form_value));
            if (lldb_type)
              clang_type = lldb_type->GetForwardCompilerType();
          }
          break;

        case DW_AT_const_value:
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            uval64_valid = true;
            uval64 = form_value.Unsigned();
          }
          break;
        default:
          break;
        }
      }

      clang::ASTContext *ast = m_ast.getASTContext();
      if (!clang_type)
        clang_type = m_ast.GetBasicType(eBasicTypeVoid);

      if (!is_template_template_argument) {
        bool is_signed = false;
        if (name && name[0])
          template_param_infos.names.push_back(name);
        else
          template_param_infos.names.push_back(NULL);

        // Get the signed value for any integer or enumeration if available
        clang_type.IsIntegerOrEnumerationType(is_signed);

        if (tag == DW_TAG_template_value_parameter && uval64_valid) {
          llvm::Optional<uint64_t> size = clang_type.GetBitSize(nullptr);
          if (!size)
            return false;
          llvm::APInt apint(*size, uval64, is_signed);
          template_param_infos.args.push_back(
              clang::TemplateArgument(*ast, llvm::APSInt(apint, !is_signed),
                                      ClangUtil::GetQualType(clang_type)));
        } else {
          template_param_infos.args.push_back(
              clang::TemplateArgument(ClangUtil::GetQualType(clang_type)));
        }
      } else {
        auto *tplt_type = m_ast.CreateTemplateTemplateParmDecl(template_name);
        template_param_infos.names.push_back(name);
        template_param_infos.args.push_back(
            clang::TemplateArgument(clang::TemplateName(tplt_type)));
      }
    }
  }
    return true;

  default:
    break;
  }
  return false;
}

bool DWARFASTParserClang::ParseTemplateParameterInfos(
    const DWARFDIE &parent_die,
    ClangASTContext::TemplateParameterInfos &template_param_infos) {

  if (!parent_die)
    return false;

  for (DWARFDIE die = parent_die.GetFirstChild(); die.IsValid();
       die = die.GetSibling()) {
    const dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_template_type_parameter:
    case DW_TAG_template_value_parameter:
    case DW_TAG_GNU_template_parameter_pack:
    case DW_TAG_GNU_template_template_param:
      ParseTemplateDIE(die, template_param_infos);
      break;

    default:
      break;
    }
  }
  if (template_param_infos.args.empty())
    return false;
  return template_param_infos.args.size() == template_param_infos.names.size();
}

bool DWARFASTParserClang::CompleteTypeFromDWARF(const DWARFDIE &die,
                                                lldb_private::Type *type,
                                                CompilerType &clang_type) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  std::lock_guard<std::recursive_mutex> guard(
      dwarf->GetObjectFile()->GetModule()->GetMutex());

  // Disable external storage for this type so we don't get anymore
  // clang::ExternalASTSource queries for this type.
  m_ast.SetHasExternalStorage(clang_type.GetOpaqueQualType(), false);

  if (!die)
    return false;

#if defined LLDB_CONFIGURATION_DEBUG
  //----------------------------------------------------------------------
  // For debugging purposes, the LLDB_DWARF_DONT_COMPLETE_TYPENAMES environment
  // variable can be set with one or more typenames separated by ';'
  // characters. This will cause this function to not complete any types whose
  // names match.
  //
  // Examples of setting this environment variable:
  //
  // LLDB_DWARF_DONT_COMPLETE_TYPENAMES=Foo
  // LLDB_DWARF_DONT_COMPLETE_TYPENAMES=Foo;Bar;Baz
  //----------------------------------------------------------------------
  const char *dont_complete_typenames_cstr =
      getenv("LLDB_DWARF_DONT_COMPLETE_TYPENAMES");
  if (dont_complete_typenames_cstr && dont_complete_typenames_cstr[0]) {
    const char *die_name = die.GetName();
    if (die_name && die_name[0]) {
      const char *match = strstr(dont_complete_typenames_cstr, die_name);
      if (match) {
        size_t die_name_length = strlen(die_name);
        while (match) {
          const char separator_char = ';';
          const char next_char = match[die_name_length];
          if (next_char == '\0' || next_char == separator_char) {
            if (match == dont_complete_typenames_cstr ||
                match[-1] == separator_char)
              return false;
          }
          match = strstr(match + 1, die_name);
        }
      }
    }
  }
#endif

  const dw_tag_t tag = die.Tag();

  Log *log =
      nullptr; // (LogChannelDWARF::GetLogIfAny(DWARF_LOG_DEBUG_INFO|DWARF_LOG_TYPE_COMPLETION));
  if (log)
    dwarf->GetObjectFile()->GetModule()->LogMessageVerboseBacktrace(
        log, "0x%8.8" PRIx64 ": %s '%s' resolving forward declaration...",
        die.GetID(), die.GetTagAsCString(), type->GetName().AsCString());
  assert(clang_type);
  DWARFAttributes attributes;
  switch (tag) {
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
  case DW_TAG_class_type: {
    ClangASTImporter::LayoutInfo layout_info;

    {
      if (die.HasChildren()) {
        LanguageType class_language = eLanguageTypeUnknown;
        if (ClangASTContext::IsObjCObjectOrInterfaceType(clang_type)) {
          class_language = eLanguageTypeObjC;
          // For objective C we don't start the definition when the class is
          // created.
          ClangASTContext::StartTagDeclarationDefinition(clang_type);
        }

        int tag_decl_kind = -1;
        AccessType default_accessibility = eAccessNone;
        if (tag == DW_TAG_structure_type) {
          tag_decl_kind = clang::TTK_Struct;
          default_accessibility = eAccessPublic;
        } else if (tag == DW_TAG_union_type) {
          tag_decl_kind = clang::TTK_Union;
          default_accessibility = eAccessPublic;
        } else if (tag == DW_TAG_class_type) {
          tag_decl_kind = clang::TTK_Class;
          default_accessibility = eAccessPrivate;
        }

        SymbolContext sc(die.GetLLDBCompileUnit());
        std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases;
        std::vector<int> member_accessibilities;
        bool is_a_class = false;
        // Parse members and base classes first
        DWARFDIECollection member_function_dies;

        DelayedPropertyList delayed_properties;
        ParseChildMembers(sc, die, clang_type, class_language, bases,
                          member_accessibilities, member_function_dies,
                          delayed_properties, default_accessibility, is_a_class,
                          layout_info);

        // Now parse any methods if there were any...
        size_t num_functions = member_function_dies.Size();
        if (num_functions > 0) {
          for (size_t i = 0; i < num_functions; ++i) {
            dwarf->ResolveType(member_function_dies.GetDIEAtIndex(i));
          }
        }

        if (class_language == eLanguageTypeObjC) {
          ConstString class_name(clang_type.GetTypeName());
          if (class_name) {
            DIEArray method_die_offsets;
            dwarf->GetObjCMethodDIEOffsets(class_name, method_die_offsets);

            if (!method_die_offsets.empty()) {
              DWARFDebugInfo *debug_info = dwarf->DebugInfo();

              const size_t num_matches = method_die_offsets.size();
              for (size_t i = 0; i < num_matches; ++i) {
                const DIERef &die_ref = method_die_offsets[i];
                DWARFDIE method_die = debug_info->GetDIE(die_ref);

                if (method_die)
                  method_die.ResolveType();
              }
            }

            for (DelayedPropertyList::iterator pi = delayed_properties.begin(),
                                               pe = delayed_properties.end();
                 pi != pe; ++pi)
              pi->Finalize();
          }
        }

        // If we have a DW_TAG_structure_type instead of a DW_TAG_class_type we
        // need to tell the clang type it is actually a class.
        if (class_language != eLanguageTypeObjC) {
          if (is_a_class && tag_decl_kind != clang::TTK_Class)
            m_ast.SetTagTypeKind(ClangUtil::GetQualType(clang_type),
                                 clang::TTK_Class);
        }

        // Since DW_TAG_structure_type gets used for both classes and
        // structures, we may need to set any DW_TAG_member fields to have a
        // "private" access if none was specified. When we parsed the child
        // members we tracked that actual accessibility value for each
        // DW_TAG_member in the "member_accessibilities" array. If the value
        // for the member is zero, then it was set to the
        // "default_accessibility" which for structs was "public". Below we
        // correct this by setting any fields to "private" that weren't
        // correctly set.
        if (is_a_class && !member_accessibilities.empty()) {
          // This is a class and all members that didn't have their access
          // specified are private.
          m_ast.SetDefaultAccessForRecordFields(
              m_ast.GetAsRecordDecl(clang_type), eAccessPrivate,
              &member_accessibilities.front(), member_accessibilities.size());
        }

        if (!bases.empty()) {
          // Make sure all base classes refer to complete types and not forward
          // declarations. If we don't do this, clang will crash with an
          // assertion in the call to clang_type.TransferBaseClasses()
          for (const auto &base_class : bases) {
            clang::TypeSourceInfo *type_source_info =
                base_class->getTypeSourceInfo();
            if (type_source_info) {
              CompilerType base_class_type(
                  &m_ast, type_source_info->getType().getAsOpaquePtr());
              if (!base_class_type.GetCompleteType()) {
                auto module = dwarf->GetObjectFile()->GetModule();
                module->ReportError(":: Class '%s' has a base class '%s' which "
                                    "does not have a complete definition.",
                                    die.GetName(),
                                    base_class_type.GetTypeName().GetCString());
                if (die.GetCU()->GetProducer() == eProducerClang)
                  module->ReportError(":: Try compiling the source file with "
                                      "-fstandalone-debug.");

                // We have no choice other than to pretend that the base class
                // is complete. If we don't do this, clang will crash when we
                // call setBases() inside of
                // "clang_type.TransferBaseClasses()" below. Since we
                // provide layout assistance, all ivars in this class and other
                // classes will be fine, this is the best we can do short of
                // crashing.
                if (ClangASTContext::StartTagDeclarationDefinition(
                        base_class_type)) {
                  ClangASTContext::CompleteTagDeclarationDefinition(
                      base_class_type);
                }
              }
            }
          }

          m_ast.TransferBaseClasses(clang_type.GetOpaqueQualType(),
                                    std::move(bases));
        }
      }
    }

    m_ast.AddMethodOverridesForCXXRecordType(clang_type.GetOpaqueQualType());
    ClangASTContext::BuildIndirectFields(clang_type);
    ClangASTContext::CompleteTagDeclarationDefinition(clang_type);

    if (!layout_info.field_offsets.empty() ||
        !layout_info.base_offsets.empty() ||
        !layout_info.vbase_offsets.empty()) {
      if (type)
        layout_info.bit_size = type->GetByteSize() * 8;
      if (layout_info.bit_size == 0)
        layout_info.bit_size =
            die.GetAttributeValueAsUnsigned(DW_AT_byte_size, 0) * 8;

      clang::CXXRecordDecl *record_decl =
          m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
      if (record_decl) {
        if (log) {
          ModuleSP module_sp = dwarf->GetObjectFile()->GetModule();

          if (module_sp) {
            module_sp->LogMessage(
                log,
                "ClangASTContext::CompleteTypeFromDWARF (clang_type = %p) "
                "caching layout info for record_decl = %p, bit_size = %" PRIu64
                ", alignment = %" PRIu64
                ", field_offsets[%u], base_offsets[%u], vbase_offsets[%u])",
                static_cast<void *>(clang_type.GetOpaqueQualType()),
                static_cast<void *>(record_decl), layout_info.bit_size,
                layout_info.alignment,
                static_cast<uint32_t>(layout_info.field_offsets.size()),
                static_cast<uint32_t>(layout_info.base_offsets.size()),
                static_cast<uint32_t>(layout_info.vbase_offsets.size()));

            uint32_t idx;
            {
              llvm::DenseMap<const clang::FieldDecl *, uint64_t>::const_iterator
                  pos,
                  end = layout_info.field_offsets.end();
              for (idx = 0, pos = layout_info.field_offsets.begin(); pos != end;
                   ++pos, ++idx) {
                module_sp->LogMessage(
                    log, "ClangASTContext::CompleteTypeFromDWARF (clang_type = "
                         "%p) field[%u] = { bit_offset=%u, name='%s' }",
                    static_cast<void *>(clang_type.GetOpaqueQualType()), idx,
                    static_cast<uint32_t>(pos->second),
                    pos->first->getNameAsString().c_str());
              }
            }

            {
              llvm::DenseMap<const clang::CXXRecordDecl *,
                             clang::CharUnits>::const_iterator base_pos,
                  base_end = layout_info.base_offsets.end();
              for (idx = 0, base_pos = layout_info.base_offsets.begin();
                   base_pos != base_end; ++base_pos, ++idx) {
                module_sp->LogMessage(
                    log, "ClangASTContext::CompleteTypeFromDWARF (clang_type = "
                         "%p) base[%u] = { byte_offset=%u, name='%s' }",
                    clang_type.GetOpaqueQualType(), idx,
                    (uint32_t)base_pos->second.getQuantity(),
                    base_pos->first->getNameAsString().c_str());
              }
            }
            {
              llvm::DenseMap<const clang::CXXRecordDecl *,
                             clang::CharUnits>::const_iterator vbase_pos,
                  vbase_end = layout_info.vbase_offsets.end();
              for (idx = 0, vbase_pos = layout_info.vbase_offsets.begin();
                   vbase_pos != vbase_end; ++vbase_pos, ++idx) {
                module_sp->LogMessage(
                    log, "ClangASTContext::CompleteTypeFromDWARF (clang_type = "
                         "%p) vbase[%u] = { byte_offset=%u, name='%s' }",
                    static_cast<void *>(clang_type.GetOpaqueQualType()), idx,
                    static_cast<uint32_t>(vbase_pos->second.getQuantity()),
                    vbase_pos->first->getNameAsString().c_str());
              }
            }
          }
        }
        GetClangASTImporter().InsertRecordDecl(record_decl, layout_info);
      }
    }
  }

    return (bool)clang_type;

  case DW_TAG_enumeration_type:
    if (ClangASTContext::StartTagDeclarationDefinition(clang_type)) {
      if (die.HasChildren()) {
        SymbolContext sc(die.GetLLDBCompileUnit());
        bool is_signed = false;
        clang_type.IsIntegerType(is_signed);
        ParseChildEnumerators(sc, clang_type, is_signed, type->GetByteSize(),
                              die);
      }
      ClangASTContext::CompleteTagDeclarationDefinition(clang_type);
    }
    return (bool)clang_type;

  default:
    assert(false && "not a forward clang type decl!");
    break;
  }

  return false;
}

std::vector<DWARFDIE> DWARFASTParserClang::GetDIEForDeclContext(
    lldb_private::CompilerDeclContext decl_context) {
  std::vector<DWARFDIE> result;
  for (auto it = m_decl_ctx_to_die.find(
           (clang::DeclContext *)decl_context.GetOpaqueDeclContext());
       it != m_decl_ctx_to_die.end(); it++)
    result.push_back(it->second);
  return result;
}

CompilerDecl DWARFASTParserClang::GetDeclForUIDFromDWARF(const DWARFDIE &die) {
  clang::Decl *clang_decl = GetClangDeclForDIE(die);
  if (clang_decl != nullptr)
    return CompilerDecl(&m_ast, clang_decl);
  return CompilerDecl();
}

CompilerDeclContext
DWARFASTParserClang::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  clang::DeclContext *clang_decl_ctx = GetClangDeclContextForDIE(die);
  if (clang_decl_ctx)
    return CompilerDeclContext(&m_ast, clang_decl_ctx);
  return CompilerDeclContext();
}

CompilerDeclContext
DWARFASTParserClang::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  clang::DeclContext *clang_decl_ctx =
      GetClangDeclContextContainingDIE(die, nullptr);
  if (clang_decl_ctx)
    return CompilerDeclContext(&m_ast, clang_decl_ctx);
  return CompilerDeclContext();
}

size_t DWARFASTParserClang::ParseChildEnumerators(
    const SymbolContext &sc, lldb_private::CompilerType &clang_type,
    bool is_signed, uint32_t enumerator_byte_size, const DWARFDIE &parent_die) {
  if (!parent_die)
    return 0;

  size_t enumerators_added = 0;

  for (DWARFDIE die = parent_die.GetFirstChild(); die.IsValid();
       die = die.GetSibling()) {
    const dw_tag_t tag = die.Tag();
    if (tag == DW_TAG_enumerator) {
      DWARFAttributes attributes;
      const size_t num_child_attributes = die.GetAttributes(attributes);
      if (num_child_attributes > 0) {
        const char *name = NULL;
        bool got_value = false;
        int64_t enum_value = 0;
        Declaration decl;

        uint32_t i;
        for (i = 0; i < num_child_attributes; ++i) {
          const dw_attr_t attr = attributes.AttributeAtIndex(i);
          DWARFFormValue form_value;
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            switch (attr) {
            case DW_AT_const_value:
              got_value = true;
              if (is_signed)
                enum_value = form_value.Signed();
              else
                enum_value = form_value.Unsigned();
              break;

            case DW_AT_name:
              name = form_value.AsCString();
              break;

            case DW_AT_description:
            default:
            case DW_AT_decl_file:
              decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                  form_value.Unsigned()));
              break;
            case DW_AT_decl_line:
              decl.SetLine(form_value.Unsigned());
              break;
            case DW_AT_decl_column:
              decl.SetColumn(form_value.Unsigned());
              break;
            case DW_AT_sibling:
              break;
            }
          }
        }

        if (name && name[0] && got_value) {
          m_ast.AddEnumerationValueToEnumerationType(
              clang_type, decl, name, enum_value, enumerator_byte_size * 8);
          ++enumerators_added;
        }
      }
    }
  }
  return enumerators_added;
}

#if defined(LLDB_CONFIGURATION_DEBUG) || defined(LLDB_CONFIGURATION_RELEASE)

class DIEStack {
public:
  void Push(const DWARFDIE &die) { m_dies.push_back(die); }

  void LogDIEs(Log *log) {
    StreamString log_strm;
    const size_t n = m_dies.size();
    log_strm.Printf("DIEStack[%" PRIu64 "]:\n", (uint64_t)n);
    for (size_t i = 0; i < n; i++) {
      std::string qualified_name;
      const DWARFDIE &die = m_dies[i];
      die.GetQualifiedName(qualified_name);
      log_strm.Printf("[%" PRIu64 "] 0x%8.8x: %s name='%s'\n", (uint64_t)i,
                      die.GetOffset(), die.GetTagAsCString(),
                      qualified_name.c_str());
    }
    log->PutCString(log_strm.GetData());
  }
  void Pop() { m_dies.pop_back(); }

  class ScopedPopper {
  public:
    ScopedPopper(DIEStack &die_stack)
        : m_die_stack(die_stack), m_valid(false) {}

    void Push(const DWARFDIE &die) {
      m_valid = true;
      m_die_stack.Push(die);
    }

    ~ScopedPopper() {
      if (m_valid)
        m_die_stack.Pop();
    }

  protected:
    DIEStack &m_die_stack;
    bool m_valid;
  };

protected:
  typedef std::vector<DWARFDIE> Stack;
  Stack m_dies;
};
#endif

Function *DWARFASTParserClang::ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                                      const DWARFDIE &die) {
  DWARFRangeList func_ranges;
  const char *name = NULL;
  const char *mangled = NULL;
  int decl_file = 0;
  int decl_line = 0;
  int decl_column = 0;
  int call_file = 0;
  int call_line = 0;
  int call_column = 0;
  DWARFExpression frame_base(die.GetCU());

  const dw_tag_t tag = die.Tag();

  if (tag != DW_TAG_subprogram)
    return NULL;

  if (die.GetDIENamesAndRanges(name, mangled, func_ranges, decl_file, decl_line,
                               decl_column, call_file, call_line, call_column,
                               &frame_base)) {

    // Union of all ranges in the function DIE (if the function is
    // discontiguous)
    AddressRange func_range;
    lldb::addr_t lowest_func_addr = func_ranges.GetMinRangeBase(0);
    lldb::addr_t highest_func_addr = func_ranges.GetMaxRangeEnd(0);
    if (lowest_func_addr != LLDB_INVALID_ADDRESS &&
        lowest_func_addr <= highest_func_addr) {
      ModuleSP module_sp(die.GetModule());
      func_range.GetBaseAddress().ResolveAddressUsingFileSections(
          lowest_func_addr, module_sp->GetSectionList());
      if (func_range.GetBaseAddress().IsValid())
        func_range.SetByteSize(highest_func_addr - lowest_func_addr);
    }

    if (func_range.GetBaseAddress().IsValid()) {
      Mangled func_name;
      if (mangled)
        func_name.SetValue(ConstString(mangled), true);
      else if ((die.GetParent().Tag() == DW_TAG_compile_unit ||
                die.GetParent().Tag() == DW_TAG_partial_unit) &&
               Language::LanguageIsCPlusPlus(die.GetLanguage()) && name &&
               strcmp(name, "main") != 0) {
        // If the mangled name is not present in the DWARF, generate the
        // demangled name using the decl context. We skip if the function is
        // "main" as its name is never mangled.
        bool is_static = false;
        bool is_variadic = false;
        bool has_template_params = false;
        unsigned type_quals = 0;
        std::vector<CompilerType> param_types;
        std::vector<clang::ParmVarDecl *> param_decls;
        DWARFDeclContext decl_ctx;
        StreamString sstr;

        die.GetDWARFDeclContext(decl_ctx);
        sstr << decl_ctx.GetQualifiedName();

        clang::DeclContext *containing_decl_ctx =
            GetClangDeclContextContainingDIE(die, nullptr);
        ParseChildParameters(comp_unit, containing_decl_ctx, die, true,
                             is_static, is_variadic, has_template_params,
                             param_types, param_decls, type_quals);
        sstr << "(";
        for (size_t i = 0; i < param_types.size(); i++) {
          if (i > 0)
            sstr << ", ";
          sstr << param_types[i].GetTypeName();
        }
        if (is_variadic)
          sstr << ", ...";
        sstr << ")";
        if (type_quals & clang::Qualifiers::Const)
          sstr << " const";

        func_name.SetValue(ConstString(sstr.GetString()), false);
      } else
        func_name.SetValue(ConstString(name), false);

      FunctionSP func_sp;
      std::unique_ptr<Declaration> decl_ap;
      if (decl_file != 0 || decl_line != 0 || decl_column != 0)
        decl_ap.reset(new Declaration(
            comp_unit.GetSupportFiles().GetFileSpecAtIndex(decl_file),
            decl_line, decl_column));

      SymbolFileDWARF *dwarf = die.GetDWARF();
      // Supply the type _only_ if it has already been parsed
      Type *func_type = dwarf->GetDIEToType().lookup(die.GetDIE());

      assert(func_type == NULL || func_type != DIE_IS_BEING_PARSED);

      if (dwarf->FixupAddress(func_range.GetBaseAddress())) {
        const user_id_t func_user_id = die.GetID();
        func_sp.reset(new Function(&comp_unit,
                                   func_user_id, // UserID is the DIE offset
                                   func_user_id, func_name, func_type,
                                   func_range)); // first address range

        if (func_sp.get() != NULL) {
          if (frame_base.IsValid())
            func_sp->GetFrameBaseExpression() = frame_base;
          comp_unit.AddFunction(func_sp);
          return func_sp.get();
        }
      }
    }
  }
  return NULL;
}

bool DWARFASTParserClang::ParseChildMembers(
    const SymbolContext &sc, const DWARFDIE &parent_die,
    CompilerType &class_clang_type, const LanguageType class_language,
    std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> &base_classes,
    std::vector<int> &member_accessibilities,
    DWARFDIECollection &member_function_dies,
    DelayedPropertyList &delayed_properties, AccessType &default_accessibility,
    bool &is_a_class, ClangASTImporter::LayoutInfo &layout_info) {
  if (!parent_die)
    return 0;

  // Get the parent byte size so we can verify any members will fit
  const uint64_t parent_byte_size =
      parent_die.GetAttributeValueAsUnsigned(DW_AT_byte_size, UINT64_MAX);
  const uint64_t parent_bit_size =
      parent_byte_size == UINT64_MAX ? UINT64_MAX : parent_byte_size * 8;

  uint32_t member_idx = 0;
  BitfieldInfo last_field_info;

  ModuleSP module_sp = parent_die.GetDWARF()->GetObjectFile()->GetModule();
  ClangASTContext *ast =
      llvm::dyn_cast_or_null<ClangASTContext>(class_clang_type.GetTypeSystem());
  if (ast == nullptr)
    return 0;

  for (DWARFDIE die = parent_die.GetFirstChild(); die.IsValid();
       die = die.GetSibling()) {
    dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_member:
    case DW_TAG_APPLE_property: {
      DWARFAttributes attributes;
      const size_t num_attributes = die.GetAttributes(attributes);
      if (num_attributes > 0) {
        Declaration decl;
        // DWARFExpression location;
        const char *name = NULL;
        const char *prop_name = NULL;
        const char *prop_getter_name = NULL;
        const char *prop_setter_name = NULL;
        uint32_t prop_attributes = 0;

        bool is_artificial = false;
        DWARFFormValue encoding_form;
        AccessType accessibility = eAccessNone;
        uint32_t member_byte_offset =
            (parent_die.Tag() == DW_TAG_union_type) ? 0 : UINT32_MAX;
        size_t byte_size = 0;
        int64_t bit_offset = 0;
        uint64_t data_bit_offset = UINT64_MAX;
        size_t bit_size = 0;
        bool is_external =
            false; // On DW_TAG_members, this means the member is static
        uint32_t i;
        for (i = 0; i < num_attributes && !is_artificial; ++i) {
          const dw_attr_t attr = attributes.AttributeAtIndex(i);
          DWARFFormValue form_value;
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            switch (attr) {
            case DW_AT_decl_file:
              decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                  form_value.Unsigned()));
              break;
            case DW_AT_decl_line:
              decl.SetLine(form_value.Unsigned());
              break;
            case DW_AT_decl_column:
              decl.SetColumn(form_value.Unsigned());
              break;
            case DW_AT_name:
              name = form_value.AsCString();
              break;
            case DW_AT_type:
              encoding_form = form_value;
              break;
            case DW_AT_bit_offset:
              bit_offset = form_value.Signed();
              break;
            case DW_AT_bit_size:
              bit_size = form_value.Unsigned();
              break;
            case DW_AT_byte_size:
              byte_size = form_value.Unsigned();
              break;
            case DW_AT_data_bit_offset:
              data_bit_offset = form_value.Unsigned();
              break;
            case DW_AT_data_member_location:
              if (form_value.BlockData()) {
                Value initialValue(0);
                Value memberOffset(0);
                const DWARFDataExtractor &debug_info_data = die.GetData();
                uint32_t block_length = form_value.Unsigned();
                uint32_t block_offset =
                    form_value.BlockData() - debug_info_data.GetDataStart();
                if (DWARFExpression::Evaluate(
                        nullptr, // ExecutionContext *
                        nullptr, // RegisterContext *
                        module_sp, debug_info_data, die.GetCU(), block_offset,
                        block_length, eRegisterKindDWARF, &initialValue,
                        nullptr, memberOffset, nullptr)) {
                  member_byte_offset = memberOffset.ResolveValue(NULL).UInt();
                }
              } else {
                // With DWARF 3 and later, if the value is an integer constant,
                // this form value is the offset in bytes from the beginning of
                // the containing entity.
                member_byte_offset = form_value.Unsigned();
              }
              break;

            case DW_AT_accessibility:
              accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned());
              break;
            case DW_AT_artificial:
              is_artificial = form_value.Boolean();
              break;
            case DW_AT_APPLE_property_name:
              prop_name = form_value.AsCString();
              break;
            case DW_AT_APPLE_property_getter:
              prop_getter_name = form_value.AsCString();
              break;
            case DW_AT_APPLE_property_setter:
              prop_setter_name = form_value.AsCString();
              break;
            case DW_AT_APPLE_property_attribute:
              prop_attributes = form_value.Unsigned();
              break;
            case DW_AT_external:
              is_external = form_value.Boolean();
              break;

            default:
            case DW_AT_declaration:
            case DW_AT_description:
            case DW_AT_mutable:
            case DW_AT_visibility:
            case DW_AT_sibling:
              break;
            }
          }
        }

        if (prop_name) {
          ConstString fixed_getter;
          ConstString fixed_setter;

          // Check if the property getter/setter were provided as full names.
          // We want basenames, so we extract them.

          if (prop_getter_name && prop_getter_name[0] == '-') {
            ObjCLanguage::MethodName prop_getter_method(prop_getter_name, true);
            prop_getter_name = prop_getter_method.GetSelector().GetCString();
          }

          if (prop_setter_name && prop_setter_name[0] == '-') {
            ObjCLanguage::MethodName prop_setter_method(prop_setter_name, true);
            prop_setter_name = prop_setter_method.GetSelector().GetCString();
          }

          // If the names haven't been provided, they need to be filled in.

          if (!prop_getter_name) {
            prop_getter_name = prop_name;
          }
          if (!prop_setter_name && prop_name[0] &&
              !(prop_attributes & DW_APPLE_PROPERTY_readonly)) {
            StreamString ss;

            ss.Printf("set%c%s:", toupper(prop_name[0]), &prop_name[1]);

            fixed_setter.SetString(ss.GetString());
            prop_setter_name = fixed_setter.GetCString();
          }
        }

        // Clang has a DWARF generation bug where sometimes it represents
        // fields that are references with bad byte size and bit size/offset
        // information such as:
        //
        //  DW_AT_byte_size( 0x00 )
        //  DW_AT_bit_size( 0x40 )
        //  DW_AT_bit_offset( 0xffffffffffffffc0 )
        //
        // So check the bit offset to make sure it is sane, and if the values
        // are not sane, remove them. If we don't do this then we will end up
        // with a crash if we try to use this type in an expression when clang
        // becomes unhappy with its recycled debug info.

        if (byte_size == 0 && bit_offset < 0) {
          bit_size = 0;
          bit_offset = 0;
        }

        // FIXME: Make Clang ignore Objective-C accessibility for expressions
        if (class_language == eLanguageTypeObjC ||
            class_language == eLanguageTypeObjC_plus_plus)
          accessibility = eAccessNone;

        // Handle static members
        if (is_external && member_byte_offset == UINT32_MAX) {
          Type *var_type = die.ResolveTypeUID(DIERef(encoding_form));

          if (var_type) {
            if (accessibility == eAccessNone)
              accessibility = eAccessPublic;
            ClangASTContext::AddVariableToRecordType(
                class_clang_type, name, var_type->GetLayoutCompilerType(),
                accessibility);
          }
          break;
        }

        if (!is_artificial) {
          Type *member_type = die.ResolveTypeUID(DIERef(encoding_form));

          clang::FieldDecl *field_decl = NULL;
          if (tag == DW_TAG_member) {
            if (member_type) {
              if (accessibility == eAccessNone)
                accessibility = default_accessibility;
              member_accessibilities.push_back(accessibility);

              uint64_t field_bit_offset =
                  (member_byte_offset == UINT32_MAX ? 0
                                                    : (member_byte_offset * 8));
              if (bit_size > 0) {

                BitfieldInfo this_field_info;
                this_field_info.bit_offset = field_bit_offset;
                this_field_info.bit_size = bit_size;

                /////////////////////////////////////////////////////////////
                // How to locate a field given the DWARF debug information
                //
                // AT_byte_size indicates the size of the word in which the bit
                // offset must be interpreted.
                //
                // AT_data_member_location indicates the byte offset of the
                // word from the base address of the structure.
                //
                // AT_bit_offset indicates how many bits into the word
                // (according to the host endianness) the low-order bit of the
                // field starts.  AT_bit_offset can be negative.
                //
                // AT_bit_size indicates the size of the field in bits.
                /////////////////////////////////////////////////////////////

                if (data_bit_offset != UINT64_MAX) {
                  this_field_info.bit_offset = data_bit_offset;
                } else {
                  if (byte_size == 0)
                    byte_size = member_type->GetByteSize();

                  ObjectFile *objfile = die.GetDWARF()->GetObjectFile();
                  if (objfile->GetByteOrder() == eByteOrderLittle) {
                    this_field_info.bit_offset += byte_size * 8;
                    this_field_info.bit_offset -= (bit_offset + bit_size);
                  } else {
                    this_field_info.bit_offset += bit_offset;
                  }
                }

                if ((this_field_info.bit_offset >= parent_bit_size) ||
                    !last_field_info.NextBitfieldOffsetIsValid(
                        this_field_info.bit_offset)) {
                  ObjectFile *objfile = die.GetDWARF()->GetObjectFile();
                  objfile->GetModule()->ReportWarning(
                      "0x%8.8" PRIx64 ": %s bitfield named \"%s\" has invalid "
                                      "bit offset (0x%8.8" PRIx64
                      ") member will be ignored. Please file a bug against the "
                      "compiler and include the preprocessed output for %s\n",
                      die.GetID(), DW_TAG_value_to_name(tag), name,
                      this_field_info.bit_offset,
                      sc.comp_unit ? sc.comp_unit->GetPath().c_str()
                                   : "the source file");
                  this_field_info.Clear();
                  continue;
                }

                // Update the field bit offset we will report for layout
                field_bit_offset = this_field_info.bit_offset;

                // If the member to be emitted did not start on a character
                // boundary and there is empty space between the last field and
                // this one, then we need to emit an anonymous member filling
                // up the space up to its start.  There are three cases here:
                //
                // 1 If the previous member ended on a character boundary, then
                // we can emit an
                //   anonymous member starting at the most recent character
                //   boundary.
                //
                // 2 If the previous member did not end on a character boundary
                // and the distance
                //   from the end of the previous member to the current member
                //   is less than a
                //   word width, then we can emit an anonymous member starting
                //   right after the
                //   previous member and right before this member.
                //
                // 3 If the previous member did not end on a character boundary
                // and the distance
                //   from the end of the previous member to the current member
                //   is greater than
                //   or equal a word width, then we act as in Case 1.

                const uint64_t character_width = 8;
                const uint64_t word_width = 32;

                // Objective-C has invalid DW_AT_bit_offset values in older
                // versions of clang, so we have to be careful and only insert
                // unnamed bitfields if we have a new enough clang.
                bool detect_unnamed_bitfields = true;

                if (class_language == eLanguageTypeObjC ||
                    class_language == eLanguageTypeObjC_plus_plus)
                  detect_unnamed_bitfields =
                      die.GetCU()->Supports_unnamed_objc_bitfields();

                if (detect_unnamed_bitfields) {
                  BitfieldInfo anon_field_info;

                  if ((this_field_info.bit_offset % character_width) !=
                      0) // not char aligned
                  {
                    uint64_t last_field_end = 0;

                    if (last_field_info.IsValid())
                      last_field_end =
                          last_field_info.bit_offset + last_field_info.bit_size;

                    if (this_field_info.bit_offset != last_field_end) {
                      if (((last_field_end % character_width) == 0) || // case 1
                          (this_field_info.bit_offset - last_field_end >=
                           word_width)) // case 3
                      {
                        anon_field_info.bit_size =
                            this_field_info.bit_offset % character_width;
                        anon_field_info.bit_offset =
                            this_field_info.bit_offset -
                            anon_field_info.bit_size;
                      } else // case 2
                      {
                        anon_field_info.bit_size =
                            this_field_info.bit_offset - last_field_end;
                        anon_field_info.bit_offset = last_field_end;
                      }
                    }
                  }

                  if (anon_field_info.IsValid()) {
                    clang::FieldDecl *unnamed_bitfield_decl =
                        ClangASTContext::AddFieldToRecordType(
                            class_clang_type, llvm::StringRef(),
                            m_ast.GetBuiltinTypeForEncodingAndBitSize(
                                eEncodingSint, word_width),
                            accessibility, anon_field_info.bit_size);

                    layout_info.field_offsets.insert(std::make_pair(
                        unnamed_bitfield_decl, anon_field_info.bit_offset));
                  }
                }
                last_field_info = this_field_info;
              } else {
                last_field_info.Clear();
              }

              CompilerType member_clang_type =
                  member_type->GetLayoutCompilerType();
              if (!member_clang_type.IsCompleteType())
                member_clang_type.GetCompleteType();

              {
                // Older versions of clang emit array[0] and array[1] in the
                // same way (<rdar://problem/12566646>). If the current field
                // is at the end of the structure, then there is definitely no
                // room for extra elements and we override the type to
                // array[0].

                CompilerType member_array_element_type;
                uint64_t member_array_size;
                bool member_array_is_incomplete;

                if (member_clang_type.IsArrayType(
                        &member_array_element_type, &member_array_size,
                        &member_array_is_incomplete) &&
                    !member_array_is_incomplete) {
                  uint64_t parent_byte_size =
                      parent_die.GetAttributeValueAsUnsigned(DW_AT_byte_size,
                                                             UINT64_MAX);

                  if (member_byte_offset >= parent_byte_size) {
                    if (member_array_size != 1 &&
                        (member_array_size != 0 ||
                         member_byte_offset > parent_byte_size)) {
                      module_sp->ReportError(
                          "0x%8.8" PRIx64
                          ": DW_TAG_member '%s' refers to type 0x%8.8" PRIx64
                          " which extends beyond the bounds of 0x%8.8" PRIx64,
                          die.GetID(), name, encoding_form.Reference(),
                          parent_die.GetID());
                    }

                    member_clang_type = m_ast.CreateArrayType(
                        member_array_element_type, 0, false);
                  }
                }
              }

              if (ClangASTContext::IsCXXClassType(member_clang_type) &&
                  !member_clang_type.GetCompleteType()) {
                if (die.GetCU()->GetProducer() == eProducerClang)
                  module_sp->ReportError(
                      "DWARF DIE at 0x%8.8x (class %s) has a member variable "
                      "0x%8.8x (%s) whose type is a forward declaration, not a "
                      "complete definition.\nTry compiling the source file "
                      "with -fstandalone-debug",
                      parent_die.GetOffset(), parent_die.GetName(),
                      die.GetOffset(), name);
                else
                  module_sp->ReportError(
                      "DWARF DIE at 0x%8.8x (class %s) has a member variable "
                      "0x%8.8x (%s) whose type is a forward declaration, not a "
                      "complete definition.\nPlease file a bug against the "
                      "compiler and include the preprocessed output for %s",
                      parent_die.GetOffset(), parent_die.GetName(),
                      die.GetOffset(), name,
                      sc.comp_unit ? sc.comp_unit->GetPath().c_str()
                                   : "the source file");
                // We have no choice other than to pretend that the member
                // class is complete. If we don't do this, clang will crash
                // when trying to layout the class. Since we provide layout
                // assistance, all ivars in this class and other classes will
                // be fine, this is the best we can do short of crashing.
                if (ClangASTContext::StartTagDeclarationDefinition(
                        member_clang_type)) {
                  ClangASTContext::CompleteTagDeclarationDefinition(
                      member_clang_type);
                } else {
                  module_sp->ReportError(
                      "DWARF DIE at 0x%8.8x (class %s) has a member variable "
                      "0x%8.8x (%s) whose type claims to be a C++ class but we "
                      "were not able to start its definition.\nPlease file a "
                      "bug and attach the file at the start of this error "
                      "message",
                      parent_die.GetOffset(), parent_die.GetName(),
                      die.GetOffset(), name);
                }
              }

              field_decl = ClangASTContext::AddFieldToRecordType(
                  class_clang_type, name, member_clang_type, accessibility,
                  bit_size);

              m_ast.SetMetadataAsUserID(field_decl, die.GetID());

              layout_info.field_offsets.insert(
                  std::make_pair(field_decl, field_bit_offset));
            } else {
              if (name)
                module_sp->ReportError(
                    "0x%8.8" PRIx64
                    ": DW_TAG_member '%s' refers to type 0x%8.8" PRIx64
                    " which was unable to be parsed",
                    die.GetID(), name, encoding_form.Reference());
              else
                module_sp->ReportError(
                    "0x%8.8" PRIx64
                    ": DW_TAG_member refers to type 0x%8.8" PRIx64
                    " which was unable to be parsed",
                    die.GetID(), encoding_form.Reference());
            }
          }

          if (prop_name != NULL && member_type) {
            clang::ObjCIvarDecl *ivar_decl = NULL;

            if (field_decl) {
              ivar_decl = clang::dyn_cast<clang::ObjCIvarDecl>(field_decl);
              assert(ivar_decl != NULL);
            }

            ClangASTMetadata metadata;
            metadata.SetUserID(die.GetID());
            delayed_properties.push_back(DelayedAddObjCClassProperty(
                class_clang_type, prop_name,
                member_type->GetLayoutCompilerType(), ivar_decl,
                prop_setter_name, prop_getter_name, prop_attributes,
                &metadata));

            if (ivar_decl)
              m_ast.SetMetadataAsUserID(ivar_decl, die.GetID());
          }
        }
      }
      ++member_idx;
    } break;

    case DW_TAG_subprogram:
      // Let the type parsing code handle this one for us.
      member_function_dies.Append(die);
      break;

    case DW_TAG_inheritance: {
      is_a_class = true;
      if (default_accessibility == eAccessNone)
        default_accessibility = eAccessPrivate;
      // TODO: implement DW_TAG_inheritance type parsing
      DWARFAttributes attributes;
      const size_t num_attributes = die.GetAttributes(attributes);
      if (num_attributes > 0) {
        Declaration decl;
        DWARFExpression location(die.GetCU());
        DWARFFormValue encoding_form;
        AccessType accessibility = default_accessibility;
        bool is_virtual = false;
        bool is_base_of_class = true;
        off_t member_byte_offset = 0;
        uint32_t i;
        for (i = 0; i < num_attributes; ++i) {
          const dw_attr_t attr = attributes.AttributeAtIndex(i);
          DWARFFormValue form_value;
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            switch (attr) {
            case DW_AT_decl_file:
              decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                  form_value.Unsigned()));
              break;
            case DW_AT_decl_line:
              decl.SetLine(form_value.Unsigned());
              break;
            case DW_AT_decl_column:
              decl.SetColumn(form_value.Unsigned());
              break;
            case DW_AT_type:
              encoding_form = form_value;
              break;
            case DW_AT_data_member_location:
              if (form_value.BlockData()) {
                Value initialValue(0);
                Value memberOffset(0);
                const DWARFDataExtractor &debug_info_data = die.GetData();
                uint32_t block_length = form_value.Unsigned();
                uint32_t block_offset =
                    form_value.BlockData() - debug_info_data.GetDataStart();
                if (DWARFExpression::Evaluate(nullptr, nullptr, module_sp,
                                              debug_info_data, die.GetCU(),
                                              block_offset, block_length,
                                              eRegisterKindDWARF, &initialValue,
                                              nullptr, memberOffset, nullptr)) {
                  member_byte_offset = memberOffset.ResolveValue(NULL).UInt();
                }
              } else {
                // With DWARF 3 and later, if the value is an integer constant,
                // this form value is the offset in bytes from the beginning of
                // the containing entity.
                member_byte_offset = form_value.Unsigned();
              }
              break;

            case DW_AT_accessibility:
              accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned());
              break;

            case DW_AT_virtuality:
              is_virtual = form_value.Boolean();
              break;

            case DW_AT_sibling:
              break;

            default:
              break;
            }
          }
        }

        Type *base_class_type = die.ResolveTypeUID(DIERef(encoding_form));
        if (base_class_type == NULL) {
          module_sp->ReportError("0x%8.8x: DW_TAG_inheritance failed to "
                                 "resolve the base class at 0x%8.8" PRIx64
                                 " from enclosing type 0x%8.8x. \nPlease file "
                                 "a bug and attach the file at the start of "
                                 "this error message",
                                 die.GetOffset(), encoding_form.Reference(),
                                 parent_die.GetOffset());
          break;
        }

        CompilerType base_class_clang_type =
            base_class_type->GetFullCompilerType();
        assert(base_class_clang_type);
        if (class_language == eLanguageTypeObjC) {
          ast->SetObjCSuperClass(class_clang_type, base_class_clang_type);
        } else {
          std::unique_ptr<clang::CXXBaseSpecifier> result =
              ast->CreateBaseClassSpecifier(
                  base_class_clang_type.GetOpaqueQualType(), accessibility,
                  is_virtual, is_base_of_class);
          if (!result)
            break;

          base_classes.push_back(std::move(result));

          if (is_virtual) {
            // Do not specify any offset for virtual inheritance. The DWARF
            // produced by clang doesn't give us a constant offset, but gives
            // us a DWARF expressions that requires an actual object in memory.
            // the DW_AT_data_member_location for a virtual base class looks
            // like:
            //      DW_AT_data_member_location( DW_OP_dup, DW_OP_deref,
            //      DW_OP_constu(0x00000018), DW_OP_minus, DW_OP_deref,
            //      DW_OP_plus )
            // Given this, there is really no valid response we can give to
            // clang for virtual base class offsets, and this should eventually
            // be removed from LayoutRecordType() in the external
            // AST source in clang.
          } else {
            layout_info.base_offsets.insert(std::make_pair(
                ast->GetAsCXXRecordDecl(
                    base_class_clang_type.GetOpaqueQualType()),
                clang::CharUnits::fromQuantity(member_byte_offset)));
          }
        }
      }
    } break;

    default:
      break;
    }
  }

  return true;
}

size_t DWARFASTParserClang::ParseChildParameters(
    CompileUnit &comp_unit, clang::DeclContext *containing_decl_ctx,
    const DWARFDIE &parent_die, bool skip_artificial, bool &is_static,
    bool &is_variadic, bool &has_template_params,
    std::vector<CompilerType> &function_param_types,
    std::vector<clang::ParmVarDecl *> &function_param_decls,
    unsigned &type_quals) {
  if (!parent_die)
    return 0;

  size_t arg_idx = 0;
  for (DWARFDIE die = parent_die.GetFirstChild(); die.IsValid();
       die = die.GetSibling()) {
    const dw_tag_t tag = die.Tag();
    switch (tag) {
    case DW_TAG_formal_parameter: {
      DWARFAttributes attributes;
      const size_t num_attributes = die.GetAttributes(attributes);
      if (num_attributes > 0) {
        const char *name = NULL;
        Declaration decl;
        DWARFFormValue param_type_die_form;
        bool is_artificial = false;
        // one of None, Auto, Register, Extern, Static, PrivateExtern

        clang::StorageClass storage = clang::SC_None;
        uint32_t i;
        for (i = 0; i < num_attributes; ++i) {
          const dw_attr_t attr = attributes.AttributeAtIndex(i);
          DWARFFormValue form_value;
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            switch (attr) {
            case DW_AT_decl_file:
              decl.SetFile(comp_unit.GetSupportFiles().GetFileSpecAtIndex(
                  form_value.Unsigned()));
              break;
            case DW_AT_decl_line:
              decl.SetLine(form_value.Unsigned());
              break;
            case DW_AT_decl_column:
              decl.SetColumn(form_value.Unsigned());
              break;
            case DW_AT_name:
              name = form_value.AsCString();
              break;
            case DW_AT_type:
              param_type_die_form = form_value;
              break;
            case DW_AT_artificial:
              is_artificial = form_value.Boolean();
              break;
            case DW_AT_location:
            case DW_AT_const_value:
            case DW_AT_default_value:
            case DW_AT_description:
            case DW_AT_endianity:
            case DW_AT_is_optional:
            case DW_AT_segment:
            case DW_AT_variable_parameter:
            default:
            case DW_AT_abstract_origin:
            case DW_AT_sibling:
              break;
            }
          }
        }

        bool skip = false;
        if (skip_artificial && is_artificial) {
          // In order to determine if a C++ member function is "const" we
          // have to look at the const-ness of "this"...
          if (arg_idx == 0 &&
              DeclKindIsCXXClass(containing_decl_ctx->getDeclKind()) &&
              // Often times compilers omit the "this" name for the
              // specification DIEs, so we can't rely upon the name being in
              // the formal parameter DIE...
              (name == NULL || ::strcmp(name, "this") == 0)) {
            Type *this_type = die.ResolveTypeUID(DIERef(param_type_die_form));
            if (this_type) {
              uint32_t encoding_mask = this_type->GetEncodingMask();
              if (encoding_mask & Type::eEncodingIsPointerUID) {
                is_static = false;

                if (encoding_mask & (1u << Type::eEncodingIsConstUID))
                  type_quals |= clang::Qualifiers::Const;
                if (encoding_mask & (1u << Type::eEncodingIsVolatileUID))
                  type_quals |= clang::Qualifiers::Volatile;
              }
            }
          }
          skip = true;
        }

        if (!skip) {
          Type *type = die.ResolveTypeUID(DIERef(param_type_die_form));
          if (type) {
            function_param_types.push_back(type->GetForwardCompilerType());

            clang::ParmVarDecl *param_var_decl =
                m_ast.CreateParameterDeclaration(containing_decl_ctx, name,
                                                 type->GetForwardCompilerType(),
                                                 storage);
            assert(param_var_decl);
            function_param_decls.push_back(param_var_decl);

            m_ast.SetMetadataAsUserID(param_var_decl, die.GetID());
          }
        }
      }
      arg_idx++;
    } break;

    case DW_TAG_unspecified_parameters:
      is_variadic = true;
      break;

    case DW_TAG_template_type_parameter:
    case DW_TAG_template_value_parameter:
    case DW_TAG_GNU_template_parameter_pack:
      // The one caller of this was never using the template_param_infos, and
      // the local variable was taking up a large amount of stack space in
      // SymbolFileDWARF::ParseType() so this was removed. If we ever need the
      // template params back, we can add them back.
      // ParseTemplateDIE (dwarf_cu, die, template_param_infos);
      has_template_params = true;
      break;

    default:
      break;
    }
  }
  return arg_idx;
}

llvm::Optional<SymbolFile::ArrayInfo>
DWARFASTParser::ParseChildArrayInfo(const DWARFDIE &parent_die,
                                    const ExecutionContext *exe_ctx) {
  SymbolFile::ArrayInfo array_info;
  if (!parent_die)
    return llvm::None;

  for (DWARFDIE die = parent_die.GetFirstChild(); die.IsValid();
       die = die.GetSibling()) {
    const dw_tag_t tag = die.Tag();
    switch (tag) {
    case DW_TAG_subrange_type: {
      DWARFAttributes attributes;
      const size_t num_child_attributes = die.GetAttributes(attributes);
      if (num_child_attributes > 0) {
        uint64_t num_elements = 0;
        uint64_t lower_bound = 0;
        uint64_t upper_bound = 0;
        bool upper_bound_valid = false;
        uint32_t i;
        for (i = 0; i < num_child_attributes; ++i) {
          const dw_attr_t attr = attributes.AttributeAtIndex(i);
          DWARFFormValue form_value;
          if (attributes.ExtractFormValueAtIndex(i, form_value)) {
            switch (attr) {
            case DW_AT_name:
              break;

            case DW_AT_count:
              if (DWARFDIE var_die = die.GetReferencedDIE(DW_AT_count)) {
                if (var_die.Tag() == DW_TAG_variable)
                  if (exe_ctx) {
                    if (auto frame = exe_ctx->GetFrameSP()) {
                      Status error;
                      lldb::VariableSP var_sp;
                      auto valobj_sp = frame->GetValueForVariableExpressionPath(
                          var_die.GetName(), eNoDynamicValues, 0, var_sp,
                          error);
                      if (valobj_sp) {
                        num_elements = valobj_sp->GetValueAsUnsigned(0);
                        break;
                      }
                    }
                  }
              } else
                num_elements = form_value.Unsigned();
              break;

            case DW_AT_bit_stride:
              array_info.bit_stride = form_value.Unsigned();
              break;

            case DW_AT_byte_stride:
              array_info.byte_stride = form_value.Unsigned();
              break;

            case DW_AT_lower_bound:
              lower_bound = form_value.Unsigned();
              break;

            case DW_AT_upper_bound:
              upper_bound_valid = true;
              upper_bound = form_value.Unsigned();
              break;

            default:
            case DW_AT_abstract_origin:
            case DW_AT_accessibility:
            case DW_AT_allocated:
            case DW_AT_associated:
            case DW_AT_data_location:
            case DW_AT_declaration:
            case DW_AT_description:
            case DW_AT_sibling:
            case DW_AT_threads_scaled:
            case DW_AT_type:
            case DW_AT_visibility:
              break;
            }
          }
        }

        if (num_elements == 0) {
          if (upper_bound_valid && upper_bound >= lower_bound)
            num_elements = upper_bound - lower_bound + 1;
        }

        array_info.element_orders.push_back(num_elements);
      }
    } break;
    }
  }
  return array_info;
}

Type *DWARFASTParserClang::GetTypeForDIE(const DWARFDIE &die) {
  if (die) {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    DWARFAttributes attributes;
    const size_t num_attributes = die.GetAttributes(attributes);
    if (num_attributes > 0) {
      DWARFFormValue type_die_form;
      for (size_t i = 0; i < num_attributes; ++i) {
        dw_attr_t attr = attributes.AttributeAtIndex(i);
        DWARFFormValue form_value;

        if (attr == DW_AT_type &&
            attributes.ExtractFormValueAtIndex(i, form_value))
          return dwarf->ResolveTypeUID(dwarf->GetDIE(DIERef(form_value)), true);
      }
    }
  }

  return nullptr;
}

clang::Decl *DWARFASTParserClang::GetClangDeclForDIE(const DWARFDIE &die) {
  if (!die)
    return nullptr;

  switch (die.Tag()) {
  case DW_TAG_variable:
  case DW_TAG_constant:
  case DW_TAG_formal_parameter:
  case DW_TAG_imported_declaration:
  case DW_TAG_imported_module:
    break;
  default:
    return nullptr;
  }

  DIEToDeclMap::iterator cache_pos = m_die_to_decl.find(die.GetDIE());
  if (cache_pos != m_die_to_decl.end())
    return cache_pos->second;

  if (DWARFDIE spec_die = die.GetReferencedDIE(DW_AT_specification)) {
    clang::Decl *decl = GetClangDeclForDIE(spec_die);
    m_die_to_decl[die.GetDIE()] = decl;
    m_decl_to_die[decl].insert(die.GetDIE());
    return decl;
  }

  if (DWARFDIE abstract_origin_die =
          die.GetReferencedDIE(DW_AT_abstract_origin)) {
    clang::Decl *decl = GetClangDeclForDIE(abstract_origin_die);
    m_die_to_decl[die.GetDIE()] = decl;
    m_decl_to_die[decl].insert(die.GetDIE());
    return decl;
  }

  clang::Decl *decl = nullptr;
  switch (die.Tag()) {
  case DW_TAG_variable:
  case DW_TAG_constant:
  case DW_TAG_formal_parameter: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    Type *type = GetTypeForDIE(die);
    if (dwarf && type) {
      const char *name = die.GetName();
      clang::DeclContext *decl_context =
          ClangASTContext::DeclContextGetAsDeclContext(
              dwarf->GetDeclContextContainingUID(die.GetID()));
      decl = m_ast.CreateVariableDeclaration(
          decl_context, name,
          ClangUtil::GetQualType(type->GetForwardCompilerType()));
    }
    break;
  }
  case DW_TAG_imported_declaration: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    DWARFDIE imported_uid = die.GetAttributeValueAsReferenceDIE(DW_AT_import);
    if (imported_uid) {
      CompilerDecl imported_decl = imported_uid.GetDecl();
      if (imported_decl) {
        clang::DeclContext *decl_context =
            ClangASTContext::DeclContextGetAsDeclContext(
                dwarf->GetDeclContextContainingUID(die.GetID()));
        if (clang::NamedDecl *clang_imported_decl =
                llvm::dyn_cast<clang::NamedDecl>(
                    (clang::Decl *)imported_decl.GetOpaqueDecl()))
          decl =
              m_ast.CreateUsingDeclaration(decl_context, clang_imported_decl);
      }
    }
    break;
  }
  case DW_TAG_imported_module: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    DWARFDIE imported_uid = die.GetAttributeValueAsReferenceDIE(DW_AT_import);

    if (imported_uid) {
      CompilerDeclContext imported_decl_ctx = imported_uid.GetDeclContext();
      if (imported_decl_ctx) {
        clang::DeclContext *decl_context =
            ClangASTContext::DeclContextGetAsDeclContext(
                dwarf->GetDeclContextContainingUID(die.GetID()));
        if (clang::NamespaceDecl *ns_decl =
                ClangASTContext::DeclContextGetAsNamespaceDecl(
                    imported_decl_ctx))
          decl = m_ast.CreateUsingDirectiveDeclaration(decl_context, ns_decl);
      }
    }
    break;
  }
  default:
    break;
  }

  m_die_to_decl[die.GetDIE()] = decl;
  m_decl_to_die[decl].insert(die.GetDIE());

  return decl;
}

clang::DeclContext *
DWARFASTParserClang::GetClangDeclContextForDIE(const DWARFDIE &die) {
  if (die) {
    clang::DeclContext *decl_ctx = GetCachedClangDeclContextForDIE(die);
    if (decl_ctx)
      return decl_ctx;

    bool try_parsing_type = true;
    switch (die.Tag()) {
    case DW_TAG_compile_unit:
    case DW_TAG_partial_unit:
      decl_ctx = m_ast.GetTranslationUnitDecl();
      try_parsing_type = false;
      break;

    case DW_TAG_namespace:
      decl_ctx = ResolveNamespaceDIE(die);
      try_parsing_type = false;
      break;

    case DW_TAG_lexical_block:
      decl_ctx = GetDeclContextForBlock(die);
      try_parsing_type = false;
      break;

    default:
      break;
    }

    if (decl_ctx == nullptr && try_parsing_type) {
      Type *type = die.GetDWARF()->ResolveType(die);
      if (type)
        decl_ctx = GetCachedClangDeclContextForDIE(die);
    }

    if (decl_ctx) {
      LinkDeclContextToDIE(decl_ctx, die);
      return decl_ctx;
    }
  }
  return nullptr;
}

static bool IsSubroutine(const DWARFDIE &die) {
  switch (die.Tag()) {
  case DW_TAG_subprogram:
  case DW_TAG_inlined_subroutine:
    return true;
  default:
    return false;
  }
}

static DWARFDIE GetContainingFunctionWithAbstractOrigin(const DWARFDIE &die) {
  for (DWARFDIE candidate = die; candidate; candidate = candidate.GetParent()) {
    if (IsSubroutine(candidate)) {
      if (candidate.GetReferencedDIE(DW_AT_abstract_origin)) {
        return candidate;
      } else {
        return DWARFDIE();
      }
    }
  }
  assert(0 && "Shouldn't call GetContainingFunctionWithAbstractOrigin on "
              "something not in a function");
  return DWARFDIE();
}

static DWARFDIE FindAnyChildWithAbstractOrigin(const DWARFDIE &context) {
  for (DWARFDIE candidate = context.GetFirstChild(); candidate.IsValid();
       candidate = candidate.GetSibling()) {
    if (candidate.GetReferencedDIE(DW_AT_abstract_origin)) {
      return candidate;
    }
  }
  return DWARFDIE();
}

static DWARFDIE FindFirstChildWithAbstractOrigin(const DWARFDIE &block,
                                                 const DWARFDIE &function) {
  assert(IsSubroutine(function));
  for (DWARFDIE context = block; context != function.GetParent();
       context = context.GetParent()) {
    assert(!IsSubroutine(context) || context == function);
    if (DWARFDIE child = FindAnyChildWithAbstractOrigin(context)) {
      return child;
    }
  }
  return DWARFDIE();
}

clang::DeclContext *
DWARFASTParserClang::GetDeclContextForBlock(const DWARFDIE &die) {
  assert(die.Tag() == DW_TAG_lexical_block);
  DWARFDIE containing_function_with_abstract_origin =
      GetContainingFunctionWithAbstractOrigin(die);
  if (!containing_function_with_abstract_origin) {
    return (clang::DeclContext *)ResolveBlockDIE(die);
  }
  DWARFDIE child = FindFirstChildWithAbstractOrigin(
      die, containing_function_with_abstract_origin);
  CompilerDeclContext decl_context =
      GetDeclContextContainingUIDFromDWARF(child);
  return (clang::DeclContext *)decl_context.GetOpaqueDeclContext();
}

clang::BlockDecl *DWARFASTParserClang::ResolveBlockDIE(const DWARFDIE &die) {
  if (die && die.Tag() == DW_TAG_lexical_block) {
    clang::BlockDecl *decl =
        llvm::cast_or_null<clang::BlockDecl>(m_die_to_decl_ctx[die.GetDIE()]);

    if (!decl) {
      DWARFDIE decl_context_die;
      clang::DeclContext *decl_context =
          GetClangDeclContextContainingDIE(die, &decl_context_die);
      decl = m_ast.CreateBlockDeclaration(decl_context);

      if (decl)
        LinkDeclContextToDIE((clang::DeclContext *)decl, die);
    }

    return decl;
  }
  return nullptr;
}

clang::NamespaceDecl *
DWARFASTParserClang::ResolveNamespaceDIE(const DWARFDIE &die) {
  if (die && die.Tag() == DW_TAG_namespace) {
    // See if we already parsed this namespace DIE and associated it with a
    // uniqued namespace declaration
    clang::NamespaceDecl *namespace_decl =
        static_cast<clang::NamespaceDecl *>(m_die_to_decl_ctx[die.GetDIE()]);
    if (namespace_decl)
      return namespace_decl;
    else {
      const char *namespace_name = die.GetName();
      clang::DeclContext *containing_decl_ctx =
          GetClangDeclContextContainingDIE(die, nullptr);
      namespace_decl = m_ast.GetUniqueNamespaceDeclaration(namespace_name,
                                                           containing_decl_ctx);
      Log *log =
          nullptr; // (LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_INFO));
      if (log) {
        SymbolFileDWARF *dwarf = die.GetDWARF();
        if (namespace_name) {
          dwarf->GetObjectFile()->GetModule()->LogMessage(
              log, "ASTContext => %p: 0x%8.8" PRIx64
                   ": DW_TAG_namespace with DW_AT_name(\"%s\") => "
                   "clang::NamespaceDecl *%p (original = %p)",
              static_cast<void *>(m_ast.getASTContext()), die.GetID(),
              namespace_name, static_cast<void *>(namespace_decl),
              static_cast<void *>(namespace_decl->getOriginalNamespace()));
        } else {
          dwarf->GetObjectFile()->GetModule()->LogMessage(
              log, "ASTContext => %p: 0x%8.8" PRIx64
                   ": DW_TAG_namespace (anonymous) => clang::NamespaceDecl *%p "
                   "(original = %p)",
              static_cast<void *>(m_ast.getASTContext()), die.GetID(),
              static_cast<void *>(namespace_decl),
              static_cast<void *>(namespace_decl->getOriginalNamespace()));
        }
      }

      if (namespace_decl)
        LinkDeclContextToDIE((clang::DeclContext *)namespace_decl, die);
      return namespace_decl;
    }
  }
  return nullptr;
}

clang::DeclContext *DWARFASTParserClang::GetClangDeclContextContainingDIE(
    const DWARFDIE &die, DWARFDIE *decl_ctx_die_copy) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  DWARFDIE decl_ctx_die = dwarf->GetDeclContextDIEContainingDIE(die);

  if (decl_ctx_die_copy)
    *decl_ctx_die_copy = decl_ctx_die;

  if (decl_ctx_die) {
    clang::DeclContext *clang_decl_ctx =
        GetClangDeclContextForDIE(decl_ctx_die);
    if (clang_decl_ctx)
      return clang_decl_ctx;
  }
  return m_ast.GetTranslationUnitDecl();
}

clang::DeclContext *
DWARFASTParserClang::GetCachedClangDeclContextForDIE(const DWARFDIE &die) {
  if (die) {
    DIEToDeclContextMap::iterator pos = m_die_to_decl_ctx.find(die.GetDIE());
    if (pos != m_die_to_decl_ctx.end())
      return pos->second;
  }
  return nullptr;
}

void DWARFASTParserClang::LinkDeclContextToDIE(clang::DeclContext *decl_ctx,
                                               const DWARFDIE &die) {
  m_die_to_decl_ctx[die.GetDIE()] = decl_ctx;
  // There can be many DIEs for a single decl context
  // m_decl_ctx_to_die[decl_ctx].insert(die.GetDIE());
  m_decl_ctx_to_die.insert(std::make_pair(decl_ctx, die));
}

bool DWARFASTParserClang::CopyUniqueClassMethodTypes(
    const DWARFDIE &src_class_die, const DWARFDIE &dst_class_die,
    lldb_private::Type *class_type, DWARFDIECollection &failures) {
  if (!class_type || !src_class_die || !dst_class_die)
    return false;
  if (src_class_die.Tag() != dst_class_die.Tag())
    return false;

  // We need to complete the class type so we can get all of the method types
  // parsed so we can then unique those types to their equivalent counterparts
  // in "dst_cu" and "dst_class_die"
  class_type->GetFullCompilerType();

  DWARFDIE src_die;
  DWARFDIE dst_die;
  UniqueCStringMap<DWARFDIE> src_name_to_die;
  UniqueCStringMap<DWARFDIE> dst_name_to_die;
  UniqueCStringMap<DWARFDIE> src_name_to_die_artificial;
  UniqueCStringMap<DWARFDIE> dst_name_to_die_artificial;
  for (src_die = src_class_die.GetFirstChild(); src_die.IsValid();
       src_die = src_die.GetSibling()) {
    if (src_die.Tag() == DW_TAG_subprogram) {
      // Make sure this is a declaration and not a concrete instance by looking
      // for DW_AT_declaration set to 1. Sometimes concrete function instances
      // are placed inside the class definitions and shouldn't be included in
      // the list of things are are tracking here.
      if (src_die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0) == 1) {
        const char *src_name = src_die.GetMangledName();
        if (src_name) {
          ConstString src_const_name(src_name);
          if (src_die.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
            src_name_to_die_artificial.Append(src_const_name, src_die);
          else
            src_name_to_die.Append(src_const_name, src_die);
        }
      }
    }
  }
  for (dst_die = dst_class_die.GetFirstChild(); dst_die.IsValid();
       dst_die = dst_die.GetSibling()) {
    if (dst_die.Tag() == DW_TAG_subprogram) {
      // Make sure this is a declaration and not a concrete instance by looking
      // for DW_AT_declaration set to 1. Sometimes concrete function instances
      // are placed inside the class definitions and shouldn't be included in
      // the list of things are are tracking here.
      if (dst_die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0) == 1) {
        const char *dst_name = dst_die.GetMangledName();
        if (dst_name) {
          ConstString dst_const_name(dst_name);
          if (dst_die.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
            dst_name_to_die_artificial.Append(dst_const_name, dst_die);
          else
            dst_name_to_die.Append(dst_const_name, dst_die);
        }
      }
    }
  }
  const uint32_t src_size = src_name_to_die.GetSize();
  const uint32_t dst_size = dst_name_to_die.GetSize();
  Log *log = nullptr; // (LogChannelDWARF::GetLogIfAny(DWARF_LOG_DEBUG_INFO |
                      // DWARF_LOG_TYPE_COMPLETION));

  // Is everything kosher so we can go through the members at top speed?
  bool fast_path = true;

  if (src_size != dst_size) {
    if (src_size != 0 && dst_size != 0) {
      if (log)
        log->Printf("warning: trying to unique class DIE 0x%8.8x to 0x%8.8x, "
                    "but they didn't have the same size (src=%d, dst=%d)",
                    src_class_die.GetOffset(), dst_class_die.GetOffset(),
                    src_size, dst_size);
    }

    fast_path = false;
  }

  uint32_t idx;

  if (fast_path) {
    for (idx = 0; idx < src_size; ++idx) {
      src_die = src_name_to_die.GetValueAtIndexUnchecked(idx);
      dst_die = dst_name_to_die.GetValueAtIndexUnchecked(idx);

      if (src_die.Tag() != dst_die.Tag()) {
        if (log)
          log->Printf("warning: tried to unique class DIE 0x%8.8x to 0x%8.8x, "
                      "but 0x%8.8x (%s) tags didn't match 0x%8.8x (%s)",
                      src_class_die.GetOffset(), dst_class_die.GetOffset(),
                      src_die.GetOffset(), src_die.GetTagAsCString(),
                      dst_die.GetOffset(), dst_die.GetTagAsCString());
        fast_path = false;
      }

      const char *src_name = src_die.GetMangledName();
      const char *dst_name = dst_die.GetMangledName();

      // Make sure the names match
      if (src_name == dst_name || (strcmp(src_name, dst_name) == 0))
        continue;

      if (log)
        log->Printf("warning: tried to unique class DIE 0x%8.8x to 0x%8.8x, "
                    "but 0x%8.8x (%s) names didn't match 0x%8.8x (%s)",
                    src_class_die.GetOffset(), dst_class_die.GetOffset(),
                    src_die.GetOffset(), src_name, dst_die.GetOffset(),
                    dst_name);

      fast_path = false;
    }
  }

  DWARFASTParserClang *src_dwarf_ast_parser =
      (DWARFASTParserClang *)src_die.GetDWARFParser();
  DWARFASTParserClang *dst_dwarf_ast_parser =
      (DWARFASTParserClang *)dst_die.GetDWARFParser();

  // Now do the work of linking the DeclContexts and Types.
  if (fast_path) {
    // We can do this quickly.  Just run across the tables index-for-index
    // since we know each node has matching names and tags.
    for (idx = 0; idx < src_size; ++idx) {
      src_die = src_name_to_die.GetValueAtIndexUnchecked(idx);
      dst_die = dst_name_to_die.GetValueAtIndexUnchecked(idx);

      clang::DeclContext *src_decl_ctx =
          src_dwarf_ast_parser->m_die_to_decl_ctx[src_die.GetDIE()];
      if (src_decl_ctx) {
        if (log)
          log->Printf("uniquing decl context %p from 0x%8.8x for 0x%8.8x",
                      static_cast<void *>(src_decl_ctx), src_die.GetOffset(),
                      dst_die.GetOffset());
        dst_dwarf_ast_parser->LinkDeclContextToDIE(src_decl_ctx, dst_die);
      } else {
        if (log)
          log->Printf("warning: tried to unique decl context from 0x%8.8x for "
                      "0x%8.8x, but none was found",
                      src_die.GetOffset(), dst_die.GetOffset());
      }

      Type *src_child_type =
          dst_die.GetDWARF()->GetDIEToType()[src_die.GetDIE()];
      if (src_child_type) {
        if (log)
          log->Printf(
              "uniquing type %p (uid=0x%" PRIx64 ") from 0x%8.8x for 0x%8.8x",
              static_cast<void *>(src_child_type), src_child_type->GetID(),
              src_die.GetOffset(), dst_die.GetOffset());
        dst_die.GetDWARF()->GetDIEToType()[dst_die.GetDIE()] = src_child_type;
      } else {
        if (log)
          log->Printf("warning: tried to unique lldb_private::Type from "
                      "0x%8.8x for 0x%8.8x, but none was found",
                      src_die.GetOffset(), dst_die.GetOffset());
      }
    }
  } else {
    // We must do this slowly.  For each member of the destination, look up a
    // member in the source with the same name, check its tag, and unique them
    // if everything matches up.  Report failures.

    if (!src_name_to_die.IsEmpty() && !dst_name_to_die.IsEmpty()) {
      src_name_to_die.Sort();

      for (idx = 0; idx < dst_size; ++idx) {
        ConstString dst_name = dst_name_to_die.GetCStringAtIndex(idx);
        dst_die = dst_name_to_die.GetValueAtIndexUnchecked(idx);
        src_die = src_name_to_die.Find(dst_name, DWARFDIE());

        if (src_die && (src_die.Tag() == dst_die.Tag())) {
          clang::DeclContext *src_decl_ctx =
              src_dwarf_ast_parser->m_die_to_decl_ctx[src_die.GetDIE()];
          if (src_decl_ctx) {
            if (log)
              log->Printf("uniquing decl context %p from 0x%8.8x for 0x%8.8x",
                          static_cast<void *>(src_decl_ctx),
                          src_die.GetOffset(), dst_die.GetOffset());
            dst_dwarf_ast_parser->LinkDeclContextToDIE(src_decl_ctx, dst_die);
          } else {
            if (log)
              log->Printf("warning: tried to unique decl context from 0x%8.8x "
                          "for 0x%8.8x, but none was found",
                          src_die.GetOffset(), dst_die.GetOffset());
          }

          Type *src_child_type =
              dst_die.GetDWARF()->GetDIEToType()[src_die.GetDIE()];
          if (src_child_type) {
            if (log)
              log->Printf("uniquing type %p (uid=0x%" PRIx64
                          ") from 0x%8.8x for 0x%8.8x",
                          static_cast<void *>(src_child_type),
                          src_child_type->GetID(), src_die.GetOffset(),
                          dst_die.GetOffset());
            dst_die.GetDWARF()->GetDIEToType()[dst_die.GetDIE()] =
                src_child_type;
          } else {
            if (log)
              log->Printf("warning: tried to unique lldb_private::Type from "
                          "0x%8.8x for 0x%8.8x, but none was found",
                          src_die.GetOffset(), dst_die.GetOffset());
          }
        } else {
          if (log)
            log->Printf("warning: couldn't find a match for 0x%8.8x",
                        dst_die.GetOffset());

          failures.Append(dst_die);
        }
      }
    }
  }

  const uint32_t src_size_artificial = src_name_to_die_artificial.GetSize();
  const uint32_t dst_size_artificial = dst_name_to_die_artificial.GetSize();

  if (src_size_artificial && dst_size_artificial) {
    dst_name_to_die_artificial.Sort();

    for (idx = 0; idx < src_size_artificial; ++idx) {
      ConstString src_name_artificial =
          src_name_to_die_artificial.GetCStringAtIndex(idx);
      src_die = src_name_to_die_artificial.GetValueAtIndexUnchecked(idx);
      dst_die =
          dst_name_to_die_artificial.Find(src_name_artificial, DWARFDIE());

      if (dst_die) {
        // Both classes have the artificial types, link them
        clang::DeclContext *src_decl_ctx =
            src_dwarf_ast_parser->m_die_to_decl_ctx[src_die.GetDIE()];
        if (src_decl_ctx) {
          if (log)
            log->Printf("uniquing decl context %p from 0x%8.8x for 0x%8.8x",
                        static_cast<void *>(src_decl_ctx), src_die.GetOffset(),
                        dst_die.GetOffset());
          dst_dwarf_ast_parser->LinkDeclContextToDIE(src_decl_ctx, dst_die);
        } else {
          if (log)
            log->Printf("warning: tried to unique decl context from 0x%8.8x "
                        "for 0x%8.8x, but none was found",
                        src_die.GetOffset(), dst_die.GetOffset());
        }

        Type *src_child_type =
            dst_die.GetDWARF()->GetDIEToType()[src_die.GetDIE()];
        if (src_child_type) {
          if (log)
            log->Printf(
                "uniquing type %p (uid=0x%" PRIx64 ") from 0x%8.8x for 0x%8.8x",
                static_cast<void *>(src_child_type), src_child_type->GetID(),
                src_die.GetOffset(), dst_die.GetOffset());
          dst_die.GetDWARF()->GetDIEToType()[dst_die.GetDIE()] = src_child_type;
        } else {
          if (log)
            log->Printf("warning: tried to unique lldb_private::Type from "
                        "0x%8.8x for 0x%8.8x, but none was found",
                        src_die.GetOffset(), dst_die.GetOffset());
        }
      }
    }
  }

  if (dst_size_artificial) {
    for (idx = 0; idx < dst_size_artificial; ++idx) {
      ConstString dst_name_artificial =
          dst_name_to_die_artificial.GetCStringAtIndex(idx);
      dst_die = dst_name_to_die_artificial.GetValueAtIndexUnchecked(idx);
      if (log)
        log->Printf("warning: need to create artificial method for 0x%8.8x for "
                    "method '%s'",
                    dst_die.GetOffset(), dst_name_artificial.GetCString());

      failures.Append(dst_die);
    }
  }

  return (failures.Size() != 0);
}
