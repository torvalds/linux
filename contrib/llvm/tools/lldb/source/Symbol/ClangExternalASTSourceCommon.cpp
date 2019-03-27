//===-- ClangExternalASTSourceCommon.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Utility/Stream.h"

#include <mutex>

using namespace lldb_private;

uint64_t g_TotalSizeOfMetadata = 0;

typedef llvm::DenseMap<clang::ExternalASTSource *,
                       ClangExternalASTSourceCommon *>
    ASTSourceMap;

static ASTSourceMap &GetSourceMap(std::unique_lock<std::mutex> &guard) {
  // Intentionally leaked to avoid problems with global destructors.
  static ASTSourceMap *s_source_map = new ASTSourceMap;
  static std::mutex s_mutex;
  std::unique_lock<std::mutex> locked_guard(s_mutex);
  guard.swap(locked_guard);
  return *s_source_map;
}

ClangExternalASTSourceCommon *
ClangExternalASTSourceCommon::Lookup(clang::ExternalASTSource *source) {
  std::unique_lock<std::mutex> guard;
  ASTSourceMap &source_map = GetSourceMap(guard);

  ASTSourceMap::iterator iter = source_map.find(source);

  if (iter != source_map.end()) {
    return iter->second;
  } else {
    return nullptr;
  }
}

ClangExternalASTSourceCommon::ClangExternalASTSourceCommon()
    : clang::ExternalASTSource() {
  g_TotalSizeOfMetadata += m_metadata.size();
  std::unique_lock<std::mutex> guard;
  GetSourceMap(guard)[this] = this;
}

ClangExternalASTSourceCommon::~ClangExternalASTSourceCommon() {
  std::unique_lock<std::mutex> guard;
  GetSourceMap(guard).erase(this);
  g_TotalSizeOfMetadata -= m_metadata.size();
}

ClangASTMetadata *
ClangExternalASTSourceCommon::GetMetadata(const void *object) {
  if (HasMetadata(object))
    return &m_metadata[object];
  else
    return nullptr;
}

void ClangExternalASTSourceCommon::SetMetadata(const void *object,
                                               ClangASTMetadata &metadata) {
  uint64_t orig_size = m_metadata.size();
  m_metadata[object] = metadata;
  uint64_t new_size = m_metadata.size();
  g_TotalSizeOfMetadata += (new_size - orig_size);
}

bool ClangExternalASTSourceCommon::HasMetadata(const void *object) {
  return m_metadata.find(object) != m_metadata.end();
}

void ClangASTMetadata::Dump(Stream *s) {
  lldb::user_id_t uid = GetUserID();

  if (uid != LLDB_INVALID_UID) {
    s->Printf("uid=0x%" PRIx64, uid);
  }

  uint64_t isa_ptr = GetISAPtr();
  if (isa_ptr != 0) {
    s->Printf("isa_ptr=0x%" PRIx64, isa_ptr);
  }

  const char *obj_ptr_name = GetObjectPtrName();
  if (obj_ptr_name) {
    s->Printf("obj_ptr_name=\"%s\" ", obj_ptr_name);
  }

  if (m_is_dynamic_cxx) {
    s->Printf("is_dynamic_cxx=%i ", m_is_dynamic_cxx);
  }
  s->EOL();
}
