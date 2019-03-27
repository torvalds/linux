#include "UdtRecordCompleter.h"

#include "PdbAstBuilder.h"
#include "PdbIndex.h"
#include "PdbSymUid.h"
#include "PdbUtil.h"

#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

using namespace llvm::codeview;
using namespace llvm::pdb;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::npdb;

using Error = llvm::Error;

UdtRecordCompleter::UdtRecordCompleter(PdbTypeSymId id,
                                       CompilerType &derived_ct,
                                       clang::TagDecl &tag_decl,
                                       PdbAstBuilder &ast_builder,
                                       TpiStream &tpi)
    : m_id(id), m_derived_ct(derived_ct), m_tag_decl(tag_decl),
      m_ast_builder(ast_builder), m_tpi(tpi) {
  CVType cvt = m_tpi.getType(m_id.index);
  switch (cvt.kind()) {
  case LF_ENUM:
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, m_cvr.er));
    break;
  case LF_UNION:
    llvm::cantFail(TypeDeserializer::deserializeAs<UnionRecord>(cvt, m_cvr.ur));
    break;
  case LF_CLASS:
  case LF_STRUCTURE:
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, m_cvr.cr));
    break;
  default:
    llvm_unreachable("unreachable!");
  }
}

clang::QualType UdtRecordCompleter::AddBaseClassForTypeIndex(
    llvm::codeview::TypeIndex ti, llvm::codeview::MemberAccess access) {
  PdbTypeSymId type_id(ti);
  clang::QualType qt = m_ast_builder.GetOrCreateType(type_id);

  CVType udt_cvt = m_tpi.getType(ti);

  std::unique_ptr<clang::CXXBaseSpecifier> base_spec =
      m_ast_builder.clang().CreateBaseClassSpecifier(
          qt.getAsOpaquePtr(), TranslateMemberAccess(access), false,
          udt_cvt.kind() == LF_CLASS);
  lldbassert(base_spec);
  m_bases.push_back(std::move(base_spec));
  return qt;
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           BaseClassRecord &base) {
  clang::QualType base_qt =
      AddBaseClassForTypeIndex(base.Type, base.getAccess());

  auto decl =
      m_ast_builder.clang().GetAsCXXRecordDecl(base_qt.getAsOpaquePtr());
  lldbassert(decl);

  auto offset = clang::CharUnits::fromQuantity(base.getBaseOffset());
  m_layout.base_offsets.insert(std::make_pair(decl, offset));

  return llvm::Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           VirtualBaseClassRecord &base) {
  AddBaseClassForTypeIndex(base.BaseType, base.getAccess());

  // FIXME: Handle virtual base offsets.
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           ListContinuationRecord &cont) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           VFPtrRecord &vfptr) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(
    CVMemberRecord &cvr, StaticDataMemberRecord &static_data_member) {
  clang::QualType member_type =
      m_ast_builder.GetOrCreateType(PdbTypeSymId(static_data_member.Type));

  m_ast_builder.CompleteType(member_type);

  CompilerType member_ct = m_ast_builder.ToCompilerType(member_type);

  lldb::AccessType access =
      TranslateMemberAccess(static_data_member.getAccess());
  ClangASTContext::AddVariableToRecordType(
      m_derived_ct, static_data_member.Name, member_ct, access);

  // FIXME: Add a PdbSymUid namespace for field list members and update
  // the m_uid_to_decl map with this decl.
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           NestedTypeRecord &nested) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           DataMemberRecord &data_member) {

  uint64_t offset = data_member.FieldOffset * 8;
  uint32_t bitfield_width = 0;

  TypeIndex ti(data_member.Type);
  if (!ti.isSimple()) {
    CVType cvt = m_tpi.getType(ti);
    if (cvt.kind() == LF_BITFIELD) {
      BitFieldRecord bfr;
      llvm::cantFail(TypeDeserializer::deserializeAs<BitFieldRecord>(cvt, bfr));
      offset += bfr.BitOffset;
      bitfield_width = bfr.BitSize;
      ti = bfr.Type;
    }
  }

  clang::QualType member_qt = m_ast_builder.GetOrCreateType(PdbTypeSymId(ti));
  m_ast_builder.CompleteType(member_qt);

  lldb::AccessType access = TranslateMemberAccess(data_member.getAccess());

  clang::FieldDecl *decl = ClangASTContext::AddFieldToRecordType(
      m_derived_ct, data_member.Name, m_ast_builder.ToCompilerType(member_qt),
      access, bitfield_width);
  // FIXME: Add a PdbSymUid namespace for field list members and update
  // the m_uid_to_decl map with this decl.

  m_layout.field_offsets.insert(std::make_pair(decl, offset));

  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           OneMethodRecord &one_method) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           OverloadedMethodRecord &overloaded) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           EnumeratorRecord &enumerator) {
  Declaration decl;
  llvm::StringRef name = DropNameScope(enumerator.getName());

  m_ast_builder.clang().AddEnumerationValueToEnumerationType(
      m_derived_ct, decl, name.str().c_str(), enumerator.Value);
  return Error::success();
}

void UdtRecordCompleter::complete() {
  ClangASTContext &clang = m_ast_builder.clang();
  clang.TransferBaseClasses(m_derived_ct.GetOpaqueQualType(),
                            std::move(m_bases));

  clang.AddMethodOverridesForCXXRecordType(m_derived_ct.GetOpaqueQualType());
  ClangASTContext::BuildIndirectFields(m_derived_ct);
  ClangASTContext::CompleteTagDeclarationDefinition(m_derived_ct);

  if (auto *record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(&m_tag_decl)) {
    m_ast_builder.importer().InsertRecordDecl(record_decl, m_layout);
  }
}
