//===-- JITLoaderList.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_JITLoaderList_h_
#define liblldb_JITLoaderList_h_

#include <mutex>
#include <vector>

#include "lldb/lldb-forward.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class JITLoaderList JITLoaderList.h "lldb/Target/JITLoaderList.h"
///
/// Class used by the Process to hold a list of its JITLoaders.
//----------------------------------------------------------------------
class JITLoaderList {
public:
  JITLoaderList();
  ~JITLoaderList();

  void Append(const lldb::JITLoaderSP &jit_loader_sp);

  void Remove(const lldb::JITLoaderSP &jit_loader_sp);

  size_t GetSize() const;

  lldb::JITLoaderSP GetLoaderAtIndex(size_t idx);

  void DidLaunch();

  void DidAttach();

  void ModulesDidLoad(ModuleList &module_list);

private:
  std::vector<lldb::JITLoaderSP> m_jit_loaders_vec;
  std::recursive_mutex m_jit_loaders_mutex;
};

} // namespace lldb_private

#endif // liblldb_JITLoaderList_h_
