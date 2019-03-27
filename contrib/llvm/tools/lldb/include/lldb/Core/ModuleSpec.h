//===-- ModuleSpec.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ModuleSpec_h_
#define liblldb_ModuleSpec_h_

#include "lldb/Host/FileSystem.h"
#include "lldb/Target/PathMappingList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UUID.h"

#include "llvm/Support/Chrono.h"

#include <mutex>
#include <vector>

namespace lldb_private {

class ModuleSpec {
public:
  ModuleSpec()
      : m_file(), m_platform_file(), m_symbol_file(), m_arch(), m_uuid(),
        m_object_name(), m_object_offset(0), m_object_size(0),
        m_source_mappings() {}

  ModuleSpec(const FileSpec &file_spec, const UUID &uuid = UUID())
      : m_file(file_spec), m_platform_file(), m_symbol_file(), m_arch(),
        m_uuid(uuid), m_object_name(), m_object_offset(0),
        m_object_size(FileSystem::Instance().GetByteSize(file_spec)),
        m_source_mappings() {}

  ModuleSpec(const FileSpec &file_spec, const ArchSpec &arch)
      : m_file(file_spec), m_platform_file(), m_symbol_file(), m_arch(arch),
        m_uuid(), m_object_name(), m_object_offset(0),
        m_object_size(FileSystem::Instance().GetByteSize(file_spec)),
        m_source_mappings() {}

  ModuleSpec(const ModuleSpec &rhs)
      : m_file(rhs.m_file), m_platform_file(rhs.m_platform_file),
        m_symbol_file(rhs.m_symbol_file), m_arch(rhs.m_arch),
        m_uuid(rhs.m_uuid), m_object_name(rhs.m_object_name),
        m_object_offset(rhs.m_object_offset), m_object_size(rhs.m_object_size),
        m_object_mod_time(rhs.m_object_mod_time),
        m_source_mappings(rhs.m_source_mappings) {}

  ModuleSpec &operator=(const ModuleSpec &rhs) {
    if (this != &rhs) {
      m_file = rhs.m_file;
      m_platform_file = rhs.m_platform_file;
      m_symbol_file = rhs.m_symbol_file;
      m_arch = rhs.m_arch;
      m_uuid = rhs.m_uuid;
      m_object_name = rhs.m_object_name;
      m_object_offset = rhs.m_object_offset;
      m_object_size = rhs.m_object_size;
      m_object_mod_time = rhs.m_object_mod_time;
      m_source_mappings = rhs.m_source_mappings;
    }
    return *this;
  }

  FileSpec *GetFileSpecPtr() { return (m_file ? &m_file : nullptr); }

  const FileSpec *GetFileSpecPtr() const {
    return (m_file ? &m_file : nullptr);
  }

  FileSpec &GetFileSpec() { return m_file; }

  const FileSpec &GetFileSpec() const { return m_file; }

  FileSpec *GetPlatformFileSpecPtr() {
    return (m_platform_file ? &m_platform_file : nullptr);
  }

  const FileSpec *GetPlatformFileSpecPtr() const {
    return (m_platform_file ? &m_platform_file : nullptr);
  }

  FileSpec &GetPlatformFileSpec() { return m_platform_file; }

  const FileSpec &GetPlatformFileSpec() const { return m_platform_file; }

  FileSpec *GetSymbolFileSpecPtr() {
    return (m_symbol_file ? &m_symbol_file : nullptr);
  }

  const FileSpec *GetSymbolFileSpecPtr() const {
    return (m_symbol_file ? &m_symbol_file : nullptr);
  }

  FileSpec &GetSymbolFileSpec() { return m_symbol_file; }

  const FileSpec &GetSymbolFileSpec() const { return m_symbol_file; }

  ArchSpec *GetArchitecturePtr() {
    return (m_arch.IsValid() ? &m_arch : nullptr);
  }

  const ArchSpec *GetArchitecturePtr() const {
    return (m_arch.IsValid() ? &m_arch : nullptr);
  }

  ArchSpec &GetArchitecture() { return m_arch; }

  const ArchSpec &GetArchitecture() const { return m_arch; }

  UUID *GetUUIDPtr() { return (m_uuid.IsValid() ? &m_uuid : nullptr); }

  const UUID *GetUUIDPtr() const {
    return (m_uuid.IsValid() ? &m_uuid : nullptr);
  }

  UUID &GetUUID() { return m_uuid; }

  const UUID &GetUUID() const { return m_uuid; }

  ConstString &GetObjectName() { return m_object_name; }

  const ConstString &GetObjectName() const { return m_object_name; }

  uint64_t GetObjectOffset() const { return m_object_offset; }

  void SetObjectOffset(uint64_t object_offset) {
    m_object_offset = object_offset;
  }

  uint64_t GetObjectSize() const { return m_object_size; }

  void SetObjectSize(uint64_t object_size) { m_object_size = object_size; }

  llvm::sys::TimePoint<> &GetObjectModificationTime() {
    return m_object_mod_time;
  }

  const llvm::sys::TimePoint<> &GetObjectModificationTime() const {
    return m_object_mod_time;
  }

  PathMappingList &GetSourceMappingList() const { return m_source_mappings; }

  void Clear() {
    m_file.Clear();
    m_platform_file.Clear();
    m_symbol_file.Clear();
    m_arch.Clear();
    m_uuid.Clear();
    m_object_name.Clear();
    m_object_offset = 0;
    m_object_size = 0;
    m_source_mappings.Clear(false);
    m_object_mod_time = llvm::sys::TimePoint<>();
  }

  explicit operator bool() const {
    if (m_file)
      return true;
    if (m_platform_file)
      return true;
    if (m_symbol_file)
      return true;
    if (m_arch.IsValid())
      return true;
    if (m_uuid.IsValid())
      return true;
    if (m_object_name)
      return true;
    if (m_object_size)
      return true;
    if (m_object_mod_time != llvm::sys::TimePoint<>())
      return true;
    return false;
  }

  void Dump(Stream &strm) const {
    bool dumped_something = false;
    if (m_file) {
      strm.PutCString("file = '");
      strm << m_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_platform_file) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("platform_file = '");
      strm << m_platform_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_symbol_file) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("symbol_file = '");
      strm << m_symbol_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_arch.IsValid()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("arch = ");
      m_arch.DumpTriple(strm);
      dumped_something = true;
    }
    if (m_uuid.IsValid()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("uuid = ");
      m_uuid.Dump(&strm);
      dumped_something = true;
    }
    if (m_object_name) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object_name = %s", m_object_name.GetCString());
      dumped_something = true;
    }
    if (m_object_offset > 0) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object_offset = %" PRIu64, m_object_offset);
      dumped_something = true;
    }
    if (m_object_size > 0) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object size = %" PRIu64, m_object_size);
      dumped_something = true;
    }
    if (m_object_mod_time != llvm::sys::TimePoint<>()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Format("object_mod_time = {0:x+}",
                  uint64_t(llvm::sys::toTimeT(m_object_mod_time)));
    }
  }

  bool Matches(const ModuleSpec &match_module_spec,
               bool exact_arch_match) const {
    if (match_module_spec.GetUUIDPtr() &&
        match_module_spec.GetUUID() != GetUUID())
      return false;
    if (match_module_spec.GetObjectName() &&
        match_module_spec.GetObjectName() != GetObjectName())
      return false;
    if (match_module_spec.GetFileSpecPtr()) {
      const FileSpec &fspec = match_module_spec.GetFileSpec();
      if (!FileSpec::Equal(fspec, GetFileSpec(),
                           !fspec.GetDirectory().IsEmpty()))
        return false;
    }
    if (GetPlatformFileSpec() && match_module_spec.GetPlatformFileSpecPtr()) {
      const FileSpec &fspec = match_module_spec.GetPlatformFileSpec();
      if (!FileSpec::Equal(fspec, GetPlatformFileSpec(),
                           !fspec.GetDirectory().IsEmpty()))
        return false;
    }
    // Only match the symbol file spec if there is one in this ModuleSpec
    if (GetSymbolFileSpec() && match_module_spec.GetSymbolFileSpecPtr()) {
      const FileSpec &fspec = match_module_spec.GetSymbolFileSpec();
      if (!FileSpec::Equal(fspec, GetSymbolFileSpec(),
                           !fspec.GetDirectory().IsEmpty()))
        return false;
    }
    if (match_module_spec.GetArchitecturePtr()) {
      if (exact_arch_match) {
        if (!GetArchitecture().IsExactMatch(
                match_module_spec.GetArchitecture()))
          return false;
      } else {
        if (!GetArchitecture().IsCompatibleMatch(
                match_module_spec.GetArchitecture()))
          return false;
      }
    }
    return true;
  }

protected:
  FileSpec m_file;
  FileSpec m_platform_file;
  FileSpec m_symbol_file;
  ArchSpec m_arch;
  UUID m_uuid;
  ConstString m_object_name;
  uint64_t m_object_offset;
  uint64_t m_object_size;
  llvm::sys::TimePoint<> m_object_mod_time;
  mutable PathMappingList m_source_mappings;
};

class ModuleSpecList {
public:
  ModuleSpecList() : m_specs(), m_mutex() {}

  ModuleSpecList(const ModuleSpecList &rhs) : m_specs(), m_mutex() {
    std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex);
    std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex);
    m_specs = rhs.m_specs;
  }

  ~ModuleSpecList() = default;

  ModuleSpecList &operator=(const ModuleSpecList &rhs) {
    if (this != &rhs) {
      std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex);
      std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex);
      m_specs = rhs.m_specs;
    }
    return *this;
  }

  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_specs.size();
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_specs.clear();
  }

  void Append(const ModuleSpec &spec) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_specs.push_back(spec);
  }

  void Append(const ModuleSpecList &rhs) {
    std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex);
    std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex);
    m_specs.insert(m_specs.end(), rhs.m_specs.begin(), rhs.m_specs.end());
  }

  // The index "i" must be valid and this can't be used in multi-threaded code
  // as no mutex lock is taken.
  ModuleSpec &GetModuleSpecRefAtIndex(size_t i) { return m_specs[i]; }

  bool GetModuleSpecAtIndex(size_t i, ModuleSpec &module_spec) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (i < m_specs.size()) {
      module_spec = m_specs[i];
      return true;
    }
    module_spec.Clear();
    return false;
  }

  bool FindMatchingModuleSpec(const ModuleSpec &module_spec,
                              ModuleSpec &match_module_spec) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    bool exact_arch_match = true;
    for (auto spec : m_specs) {
      if (spec.Matches(module_spec, exact_arch_match)) {
        match_module_spec = spec;
        return true;
      }
    }

    // If there was an architecture, retry with a compatible arch
    if (module_spec.GetArchitecturePtr()) {
      exact_arch_match = false;
      for (auto spec : m_specs) {
        if (spec.Matches(module_spec, exact_arch_match)) {
          match_module_spec = spec;
          return true;
        }
      }
    }
    match_module_spec.Clear();
    return false;
  }

  size_t FindMatchingModuleSpecs(const ModuleSpec &module_spec,
                                 ModuleSpecList &matching_list) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    bool exact_arch_match = true;
    const size_t initial_match_count = matching_list.GetSize();
    for (auto spec : m_specs) {
      if (spec.Matches(module_spec, exact_arch_match))
        matching_list.Append(spec);
    }

    // If there was an architecture, retry with a compatible arch if no matches
    // were found
    if (module_spec.GetArchitecturePtr() &&
        (initial_match_count == matching_list.GetSize())) {
      exact_arch_match = false;
      for (auto spec : m_specs) {
        if (spec.Matches(module_spec, exact_arch_match))
          matching_list.Append(spec);
      }
    }
    return matching_list.GetSize() - initial_match_count;
  }

  void Dump(Stream &strm) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    uint32_t idx = 0;
    for (auto spec : m_specs) {
      strm.Printf("[%u] ", idx);
      spec.Dump(strm);
      strm.EOL();
      ++idx;
    }
  }

protected:
  typedef std::vector<ModuleSpec> collection; ///< The module collection type.
  collection m_specs;                         ///< The collection of modules.
  mutable std::recursive_mutex m_mutex;
};

} // namespace lldb_private

#endif // liblldb_ModuleSpec_h_
