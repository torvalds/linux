//===-- ClangExternalASTSourceCallbacks.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ClangExternalASTSourceCallbacks.h"


// Clang headers like to use NDEBUG inside of them to enable/disable debug
// related features using "#ifndef NDEBUG" preprocessor blocks to do one thing
// or another. This is bad because it means that if clang was built in release
// mode, it assumes that you are building in release mode which is not always
// the case. You can end up with functions that are defined as empty in header
// files when NDEBUG is not defined, and this can cause link errors with the
// clang .a files that you have since you might be missing functions in the .a
// file. So we have to define NDEBUG when including clang headers to avoid any
// mismatches. This is covered by rdar://problem/8691220

#if !defined(NDEBUG) && !defined(LLVM_NDEBUG_OFF)
#define LLDB_DEFINED_NDEBUG_FOR_CLANG
#define NDEBUG
// Need to include assert.h so it is as clang would expect it to be (disabled)
#include <assert.h>
#endif

#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"

#ifdef LLDB_DEFINED_NDEBUG_FOR_CLANG
#undef NDEBUG
#undef LLDB_DEFINED_NDEBUG_FOR_CLANG
// Need to re-include assert.h so it is as _we_ would expect it to be (enabled)
#include <assert.h>
#endif

#include "lldb/Utility/Log.h"
#include "clang/AST/Decl.h"

using namespace clang;
using namespace lldb_private;

bool ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(
    const clang::DeclContext *decl_ctx,
    clang::DeclarationName clang_decl_name) {
  if (m_callback_find_by_name) {
    llvm::SmallVector<clang::NamedDecl *, 3> results;

    m_callback_find_by_name(m_callback_baton, decl_ctx, clang_decl_name,
                            &results);

    SetExternalVisibleDeclsForName(decl_ctx, clang_decl_name, results);

    return (results.size() != 0);
  }

  std::string decl_name(clang_decl_name.getAsString());
  SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
  return false;
}

void ClangExternalASTSourceCallbacks::CompleteType(TagDecl *tag_decl) {
  if (m_callback_tag_decl)
    m_callback_tag_decl(m_callback_baton, tag_decl);
}

void ClangExternalASTSourceCallbacks::CompleteType(
    ObjCInterfaceDecl *objc_decl) {
  if (m_callback_objc_decl)
    m_callback_objc_decl(m_callback_baton, objc_decl);
}

bool ClangExternalASTSourceCallbacks::layoutRecordType(
    const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &BaseOffsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &VirtualBaseOffsets) {
  if (m_callback_layout_record_type)
    return m_callback_layout_record_type(m_callback_baton, Record, Size,
                                         Alignment, FieldOffsets, BaseOffsets,
                                         VirtualBaseOffsets);

  return false;
}

void ClangExternalASTSourceCallbacks::FindExternalLexicalDecls(
    const clang::DeclContext *decl_ctx,
    llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  if (m_callback_tag_decl && decl_ctx) {
    clang::TagDecl *tag_decl = llvm::dyn_cast<clang::TagDecl>(
        const_cast<clang::DeclContext *>(decl_ctx));
    if (tag_decl)
      CompleteType(tag_decl);
  }
}
