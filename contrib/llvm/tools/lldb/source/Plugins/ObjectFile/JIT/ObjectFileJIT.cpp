//===-- ObjectFileJIT.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

#include "ObjectFileJIT.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/RangeMap.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/UUID.h"

#ifndef __APPLE__
#include "Utility/UuidCompatibility.h"
#endif

using namespace lldb;
using namespace lldb_private;

void ObjectFileJIT::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileJIT::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ObjectFileJIT::GetPluginNameStatic() {
  static ConstString g_name("jit");
  return g_name;
}

const char *ObjectFileJIT::GetPluginDescriptionStatic() {
  return "JIT code object file";
}

ObjectFile *ObjectFileJIT::CreateInstance(const lldb::ModuleSP &module_sp,
                                          DataBufferSP &data_sp,
                                          lldb::offset_t data_offset,
                                          const FileSpec *file,
                                          lldb::offset_t file_offset,
                                          lldb::offset_t length) {
  // JIT'ed object file is backed by the ObjectFileJITDelegate, never read from
  // a file
  return NULL;
}

ObjectFile *ObjectFileJIT::CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                                DataBufferSP &data_sp,
                                                const ProcessSP &process_sp,
                                                lldb::addr_t header_addr) {
  // JIT'ed object file is backed by the ObjectFileJITDelegate, never read from
  // memory
  return NULL;
}

size_t ObjectFileJIT::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t length, lldb_private::ModuleSpecList &specs) {
  // JIT'ed object file can't be read from a file on disk
  return 0;
}

ObjectFileJIT::ObjectFileJIT(const lldb::ModuleSP &module_sp,
                             const ObjectFileJITDelegateSP &delegate_sp)
    : ObjectFile(module_sp, NULL, 0, 0, DataBufferSP(), 0), m_delegate_wp() {
  if (delegate_sp) {
    m_delegate_wp = delegate_sp;
    m_data.SetByteOrder(delegate_sp->GetByteOrder());
    m_data.SetAddressByteSize(delegate_sp->GetAddressByteSize());
  }
}

ObjectFileJIT::~ObjectFileJIT() {}

bool ObjectFileJIT::ParseHeader() {
  // JIT code is never in a file, nor is it required to have any header
  return false;
}

ByteOrder ObjectFileJIT::GetByteOrder() const { return m_data.GetByteOrder(); }

bool ObjectFileJIT::IsExecutable() const { return false; }

uint32_t ObjectFileJIT::GetAddressByteSize() const {
  return m_data.GetAddressByteSize();
}

Symtab *ObjectFileJIT::GetSymtab() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_symtab_ap.get() == NULL) {
      m_symtab_ap.reset(new Symtab(this));
      std::lock_guard<std::recursive_mutex> symtab_guard(
          m_symtab_ap->GetMutex());
      ObjectFileJITDelegateSP delegate_sp(m_delegate_wp.lock());
      if (delegate_sp)
        delegate_sp->PopulateSymtab(this, *m_symtab_ap);
      // TODO: get symbols from delegate
      m_symtab_ap->Finalize();
    }
  }
  return m_symtab_ap.get();
}

bool ObjectFileJIT::IsStripped() {
  return false; // JIT code that is in a module is never stripped
}

void ObjectFileJIT::CreateSections(SectionList &unified_section_list) {
  if (!m_sections_ap.get()) {
    m_sections_ap.reset(new SectionList());
    ObjectFileJITDelegateSP delegate_sp(m_delegate_wp.lock());
    if (delegate_sp) {
      delegate_sp->PopulateSectionList(this, *m_sections_ap);
      unified_section_list = *m_sections_ap;
    }
  }
}

void ObjectFileJIT::Dump(Stream *s) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    s->Printf("%p: ", static_cast<void *>(this));
    s->Indent();
    s->PutCString("ObjectFileJIT");

    if (ArchSpec arch = GetArchitecture())
      *s << ", arch = " << arch.GetArchitectureName();

    s->EOL();

    SectionList *sections = GetSectionList();
    if (sections)
      sections->Dump(s, NULL, true, UINT32_MAX);

    if (m_symtab_ap.get())
      m_symtab_ap->Dump(s, NULL, eSortOrderNone);
  }
}

bool ObjectFileJIT::GetUUID(lldb_private::UUID *uuid) {
  // TODO: maybe get from delegate, not needed for first pass
  return false;
}

uint32_t ObjectFileJIT::GetDependentModules(FileSpecList &files) {
  // JIT modules don't have dependencies, but they could
  // if external functions are called and we know where they are
  files.Clear();
  return 0;
}

lldb_private::Address ObjectFileJIT::GetEntryPointAddress() {
  return Address();
}

lldb_private::Address ObjectFileJIT::GetBaseAddress() { return Address(); }

ObjectFile::Type ObjectFileJIT::CalculateType() { return eTypeJIT; }

ObjectFile::Strata ObjectFileJIT::CalculateStrata() { return eStrataJIT; }

ArchSpec ObjectFileJIT::GetArchitecture() {
  if (ObjectFileJITDelegateSP delegate_sp = m_delegate_wp.lock())
    return delegate_sp->GetArchitecture();
  return ArchSpec();
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString ObjectFileJIT::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ObjectFileJIT::GetPluginVersion() { return 1; }

bool ObjectFileJIT::SetLoadAddress(Target &target, lldb::addr_t value,
                                   bool value_is_offset) {
  size_t num_loaded_sections = 0;
  SectionList *section_list = GetSectionList();
  if (section_list) {
    const size_t num_sections = section_list->GetSize();
    // "value" is an offset to apply to each top level segment
    for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
      // Iterate through the object file sections to find all of the sections
      // that size on disk (to avoid __PAGEZERO) and load them
      SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
      if (section_sp && section_sp->GetFileSize() > 0 &&
          !section_sp->IsThreadSpecific()) {
        if (target.GetSectionLoadList().SetSectionLoadAddress(
                section_sp, section_sp->GetFileAddress() + value))
          ++num_loaded_sections;
      }
    }
  }
  return num_loaded_sections > 0;
}

size_t ObjectFileJIT::ReadSectionData(lldb_private::Section *section,
                                      lldb::offset_t section_offset, void *dst,
                                      size_t dst_len) {
  lldb::offset_t file_size = section->GetFileSize();
  if (section_offset < file_size) {
    size_t src_len = file_size - section_offset;
    if (src_len > dst_len)
      src_len = dst_len;
    const uint8_t *src =
        ((uint8_t *)(uintptr_t)section->GetFileOffset()) + section_offset;

    memcpy(dst, src, src_len);
    return src_len;
  }
  return 0;
}

size_t ObjectFileJIT::ReadSectionData(
    lldb_private::Section *section,
    lldb_private::DataExtractor &section_data) {
  if (section->GetFileSize()) {
    const void *src = (void *)(uintptr_t)section->GetFileOffset();

    DataBufferSP data_sp(
        new lldb_private::DataBufferHeap(src, section->GetFileSize()));
    if (data_sp) {
      section_data.SetData(data_sp, 0, data_sp->GetByteSize());
      section_data.SetByteOrder(GetByteOrder());
      section_data.SetAddressByteSize(GetAddressByteSize());
      return section_data.GetByteSize();
    }
  }
  section_data.Clear();
  return 0;
}
