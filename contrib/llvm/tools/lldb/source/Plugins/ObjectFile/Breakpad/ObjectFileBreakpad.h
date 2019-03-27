//===-- ObjectFileBreakpad.h ---------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H
#define LLDB_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include "llvm/ADT/Triple.h"

namespace lldb_private {
namespace breakpad {

class ObjectFileBreakpad : public ObjectFile {
public:
  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();
  static void Terminate();

  static ConstString GetPluginNameStatic();
  static const char *GetPluginDescriptionStatic() {
    return "Breakpad object file reader.";
  }

  static ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static ObjectFile *CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                          lldb::DataBufferSP &data_sp,
                                          const lldb::ProcessSP &process_sp,
                                          lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        ModuleSpecList &specs);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  ConstString GetPluginName() override { return GetPluginNameStatic(); }

  uint32_t GetPluginVersion() override { return 1; }

  //------------------------------------------------------------------
  // ObjectFile Protocol.
  //------------------------------------------------------------------

  bool ParseHeader() override;

  lldb::ByteOrder GetByteOrder() const override {
    return m_arch.GetByteOrder();
  }

  bool IsExecutable() const override { return false; }

  uint32_t GetAddressByteSize() const override {
    return m_arch.GetAddressByteSize();
  }

  AddressClass GetAddressClass(lldb::addr_t file_addr) override {
    return AddressClass::eInvalid;
  }

  Symtab *GetSymtab() override;

  bool IsStripped() override { return false; }

  void CreateSections(SectionList &unified_section_list) override;

  void Dump(Stream *s) override {}

  ArchSpec GetArchitecture() override { return m_arch; }

  bool GetUUID(UUID *uuid) override;

  FileSpecList GetDebugSymbolFilePaths() override { return FileSpecList(); }

  uint32_t GetDependentModules(FileSpecList &files) override { return 0; }

  Type CalculateType() override { return eTypeDebugInfo; }

  Strata CalculateStrata() override { return eStrataUser; }

private:
  ArchSpec m_arch;
  UUID m_uuid;

  ObjectFileBreakpad(const lldb::ModuleSP &module_sp,
                     lldb::DataBufferSP &data_sp, lldb::offset_t data_offset,
                     const FileSpec *file, lldb::offset_t offset,
                     lldb::offset_t length, ArchSpec arch, UUID uuid);
};

} // namespace breakpad
} // namespace lldb_private
#endif // LLDB_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H
