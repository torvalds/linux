//===-- ProcessElfCore.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include <mutex>

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Threading.h"

#include "Plugins/DynamicLoader/POSIX-DYLD/DynamicLoaderPOSIXDYLD.h"
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "ProcessElfCore.h"
#include "ThreadElfCore.h"

using namespace lldb_private;

ConstString ProcessElfCore::GetPluginNameStatic() {
  static ConstString g_name("elf-core");
  return g_name;
}

const char *ProcessElfCore::GetPluginDescriptionStatic() {
  return "ELF core dump plug-in.";
}

void ProcessElfCore::Terminate() {
  PluginManager::UnregisterPlugin(ProcessElfCore::CreateInstance);
}

lldb::ProcessSP ProcessElfCore::CreateInstance(lldb::TargetSP target_sp,
                                               lldb::ListenerSP listener_sp,
                                               const FileSpec *crash_file) {
  lldb::ProcessSP process_sp;
  if (crash_file) {
    // Read enough data for a ELF32 header or ELF64 header Note: Here we care
    // about e_type field only, so it is safe to ignore possible presence of
    // the header extension.
    const size_t header_size = sizeof(llvm::ELF::Elf64_Ehdr);

    auto data_sp = FileSystem::Instance().CreateDataBuffer(
        crash_file->GetPath(), header_size, 0);
    if (data_sp && data_sp->GetByteSize() == header_size &&
        elf::ELFHeader::MagicBytesMatch(data_sp->GetBytes())) {
      elf::ELFHeader elf_header;
      DataExtractor data(data_sp, lldb::eByteOrderLittle, 4);
      lldb::offset_t data_offset = 0;
      if (elf_header.Parse(data, &data_offset)) {
        if (elf_header.e_type == llvm::ELF::ET_CORE)
          process_sp.reset(
              new ProcessElfCore(target_sp, listener_sp, *crash_file));
      }
    }
  }
  return process_sp;
}

bool ProcessElfCore::CanDebug(lldb::TargetSP target_sp,
                              bool plugin_specified_by_name) {
  // For now we are just making sure the file exists for a given module
  if (!m_core_module_sp && FileSystem::Instance().Exists(m_core_file)) {
    ModuleSpec core_module_spec(m_core_file, target_sp->GetArchitecture());
    Status error(ModuleList::GetSharedModule(core_module_spec, m_core_module_sp,
                                             NULL, NULL, NULL));
    if (m_core_module_sp) {
      ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
      if (core_objfile && core_objfile->GetType() == ObjectFile::eTypeCoreFile)
        return true;
    }
  }
  return false;
}

//----------------------------------------------------------------------
// ProcessElfCore constructor
//----------------------------------------------------------------------
ProcessElfCore::ProcessElfCore(lldb::TargetSP target_sp,
                               lldb::ListenerSP listener_sp,
                               const FileSpec &core_file)
    : Process(target_sp, listener_sp), m_core_file(core_file) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ProcessElfCore::~ProcessElfCore() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize();
}

//----------------------------------------------------------------------
// PluginInterface
//----------------------------------------------------------------------
ConstString ProcessElfCore::GetPluginName() { return GetPluginNameStatic(); }

uint32_t ProcessElfCore::GetPluginVersion() { return 1; }

lldb::addr_t ProcessElfCore::AddAddressRangeFromLoadSegment(
    const elf::ELFProgramHeader &header) {
  const lldb::addr_t addr = header.p_vaddr;
  FileRange file_range(header.p_offset, header.p_filesz);
  VMRangeToFileOffset::Entry range_entry(addr, header.p_memsz, file_range);

  VMRangeToFileOffset::Entry *last_entry = m_core_aranges.Back();
  if (last_entry && last_entry->GetRangeEnd() == range_entry.GetRangeBase() &&
      last_entry->data.GetRangeEnd() == range_entry.data.GetRangeBase() &&
      last_entry->GetByteSize() == last_entry->data.GetByteSize()) {
    last_entry->SetRangeEnd(range_entry.GetRangeEnd());
    last_entry->data.SetRangeEnd(range_entry.data.GetRangeEnd());
  } else {
    m_core_aranges.Append(range_entry);
  }

  // Keep a separate map of permissions that that isn't coalesced so all ranges
  // are maintained.
  const uint32_t permissions =
      ((header.p_flags & llvm::ELF::PF_R) ? lldb::ePermissionsReadable : 0u) |
      ((header.p_flags & llvm::ELF::PF_W) ? lldb::ePermissionsWritable : 0u) |
      ((header.p_flags & llvm::ELF::PF_X) ? lldb::ePermissionsExecutable : 0u);

  m_core_range_infos.Append(
      VMRangeToPermissions::Entry(addr, header.p_memsz, permissions));

  return addr;
}

//----------------------------------------------------------------------
// Process Control
//----------------------------------------------------------------------
Status ProcessElfCore::DoLoadCore() {
  Status error;
  if (!m_core_module_sp) {
    error.SetErrorString("invalid core module");
    return error;
  }

  ObjectFileELF *core = (ObjectFileELF *)(m_core_module_sp->GetObjectFile());
  if (core == NULL) {
    error.SetErrorString("invalid core object file");
    return error;
  }

  llvm::ArrayRef<elf::ELFProgramHeader> segments = core->ProgramHeaders();
  if (segments.size() == 0) {
    error.SetErrorString("core file has no segments");
    return error;
  }

  SetCanJIT(false);

  m_thread_data_valid = true;

  bool ranges_are_sorted = true;
  lldb::addr_t vm_addr = 0;
  /// Walk through segments and Thread and Address Map information.
  /// PT_NOTE - Contains Thread and Register information
  /// PT_LOAD - Contains a contiguous range of Process Address Space
  for (const elf::ELFProgramHeader &H : segments) {
    DataExtractor data = core->GetSegmentData(H);

    // Parse thread contexts and auxv structure
    if (H.p_type == llvm::ELF::PT_NOTE) {
      if (llvm::Error error = ParseThreadContextsFromNoteSegment(H, data))
        return Status(std::move(error));
    }
    // PT_LOAD segments contains address map
    if (H.p_type == llvm::ELF::PT_LOAD) {
      lldb::addr_t last_addr = AddAddressRangeFromLoadSegment(H);
      if (vm_addr > last_addr)
        ranges_are_sorted = false;
      vm_addr = last_addr;
    }
  }

  if (!ranges_are_sorted) {
    m_core_aranges.Sort();
    m_core_range_infos.Sort();
  }

  // Even if the architecture is set in the target, we need to override it to
  // match the core file which is always single arch.
  ArchSpec arch(m_core_module_sp->GetArchitecture());

  ArchSpec target_arch = GetTarget().GetArchitecture();
  ArchSpec core_arch(m_core_module_sp->GetArchitecture());
  target_arch.MergeFrom(core_arch);
  GetTarget().SetArchitecture(target_arch);
 
  SetUnixSignals(UnixSignals::Create(GetArchitecture()));

  // Ensure we found at least one thread that was stopped on a signal.
  bool siginfo_signal_found = false;
  bool prstatus_signal_found = false;
  // Check we found a signal in a SIGINFO note.
  for (const auto &thread_data : m_thread_data) {
    if (thread_data.signo != 0)
      siginfo_signal_found = true;
    if (thread_data.prstatus_sig != 0)
      prstatus_signal_found = true;
  }
  if (!siginfo_signal_found) {
    // If we don't have signal from SIGINFO use the signal from each threads
    // PRSTATUS note.
    if (prstatus_signal_found) {
      for (auto &thread_data : m_thread_data)
        thread_data.signo = thread_data.prstatus_sig;
    } else if (m_thread_data.size() > 0) {
      // If all else fails force the first thread to be SIGSTOP
      m_thread_data.begin()->signo =
          GetUnixSignals()->GetSignalNumberFromName("SIGSTOP");
    }
  }

  // Core files are useless without the main executable. See if we can locate
  // the main executable using data we found in the core file notes.
  lldb::ModuleSP exe_module_sp = GetTarget().GetExecutableModule();
  if (!exe_module_sp) {
    // The first entry in the NT_FILE might be our executable
    if (!m_nt_file_entries.empty()) {
      ModuleSpec exe_module_spec;
      exe_module_spec.GetArchitecture() = arch;
      exe_module_spec.GetFileSpec().SetFile(
          m_nt_file_entries[0].path.GetCString(), FileSpec::Style::native);
      if (exe_module_spec.GetFileSpec()) {
        exe_module_sp = GetTarget().GetSharedModule(exe_module_spec);
        if (exe_module_sp)
          GetTarget().SetExecutableModule(exe_module_sp, eLoadDependentsNo);
      }
    }
  }
  return error;
}

lldb_private::DynamicLoader *ProcessElfCore::GetDynamicLoader() {
  if (m_dyld_ap.get() == NULL)
    m_dyld_ap.reset(DynamicLoader::FindPlugin(
        this, DynamicLoaderPOSIXDYLD::GetPluginNameStatic().GetCString()));
  return m_dyld_ap.get();
}

bool ProcessElfCore::UpdateThreadList(ThreadList &old_thread_list,
                                      ThreadList &new_thread_list) {
  const uint32_t num_threads = GetNumThreadContexts();
  if (!m_thread_data_valid)
    return false;

  for (lldb::tid_t tid = 0; tid < num_threads; ++tid) {
    const ThreadData &td = m_thread_data[tid];
    lldb::ThreadSP thread_sp(new ThreadElfCore(*this, td));
    new_thread_list.AddThread(thread_sp);
  }
  return new_thread_list.GetSize(false) > 0;
}

void ProcessElfCore::RefreshStateAfterStop() {}

Status ProcessElfCore::DoDestroy() { return Status(); }

//------------------------------------------------------------------
// Process Queries
//------------------------------------------------------------------

bool ProcessElfCore::IsAlive() { return true; }

//------------------------------------------------------------------
// Process Memory
//------------------------------------------------------------------
size_t ProcessElfCore::ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                  Status &error) {
  // Don't allow the caching that lldb_private::Process::ReadMemory does since
  // in core files we have it all cached our our core file anyway.
  return DoReadMemory(addr, buf, size, error);
}

Status ProcessElfCore::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                           MemoryRegionInfo &region_info) {
  region_info.Clear();
  const VMRangeToPermissions::Entry *permission_entry =
      m_core_range_infos.FindEntryThatContainsOrFollows(load_addr);
  if (permission_entry) {
    if (permission_entry->Contains(load_addr)) {
      region_info.GetRange().SetRangeBase(permission_entry->GetRangeBase());
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeEnd());
      const Flags permissions(permission_entry->data);
      region_info.SetReadable(permissions.Test(lldb::ePermissionsReadable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetWritable(permissions.Test(lldb::ePermissionsWritable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetExecutable(permissions.Test(lldb::ePermissionsExecutable)
                                    ? MemoryRegionInfo::eYes
                                    : MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eYes);
    } else if (load_addr < permission_entry->GetRangeBase()) {
      region_info.GetRange().SetRangeBase(load_addr);
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeBase());
      region_info.SetReadable(MemoryRegionInfo::eNo);
      region_info.SetWritable(MemoryRegionInfo::eNo);
      region_info.SetExecutable(MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eNo);
    }
    return Status();
  }

  region_info.GetRange().SetRangeBase(load_addr);
  region_info.GetRange().SetRangeEnd(LLDB_INVALID_ADDRESS);
  region_info.SetReadable(MemoryRegionInfo::eNo);
  region_info.SetWritable(MemoryRegionInfo::eNo);
  region_info.SetExecutable(MemoryRegionInfo::eNo);
  region_info.SetMapped(MemoryRegionInfo::eNo);
  return Status();
}

size_t ProcessElfCore::DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                    Status &error) {
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();

  if (core_objfile == NULL)
    return 0;

  // Get the address range
  const VMRangeToFileOffset::Entry *address_range =
      m_core_aranges.FindEntryThatContains(addr);
  if (address_range == NULL || address_range->GetRangeEnd() < addr) {
    error.SetErrorStringWithFormat("core file does not contain 0x%" PRIx64,
                                   addr);
    return 0;
  }

  // Convert the address into core file offset
  const lldb::addr_t offset = addr - address_range->GetRangeBase();
  const lldb::addr_t file_start = address_range->data.GetRangeBase();
  const lldb::addr_t file_end = address_range->data.GetRangeEnd();
  size_t bytes_to_read = size; // Number of bytes to read from the core file
  size_t bytes_copied = 0;   // Number of bytes actually read from the core file
  size_t zero_fill_size = 0; // Padding
  lldb::addr_t bytes_left =
      0; // Number of bytes available in the core file from the given address

  // Don't proceed if core file doesn't contain the actual data for this
  // address range.
  if (file_start == file_end)
    return 0;

  // Figure out how many on-disk bytes remain in this segment starting at the
  // given offset
  if (file_end > file_start + offset)
    bytes_left = file_end - (file_start + offset);

  // Figure out how many bytes we need to zero-fill if we are reading more
  // bytes than available in the on-disk segment
  if (bytes_to_read > bytes_left) {
    zero_fill_size = bytes_to_read - bytes_left;
    bytes_to_read = bytes_left;
  }

  // If there is data available on the core file read it
  if (bytes_to_read)
    bytes_copied =
        core_objfile->CopyData(offset + file_start, bytes_to_read, buf);

  assert(zero_fill_size <= size);
  // Pad remaining bytes
  if (zero_fill_size)
    memset(((char *)buf) + bytes_copied, 0, zero_fill_size);

  return bytes_copied + zero_fill_size;
}

void ProcessElfCore::Clear() {
  m_thread_list.Clear();

  SetUnixSignals(std::make_shared<UnixSignals>());
}

void ProcessElfCore::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance);
  });
}

lldb::addr_t ProcessElfCore::GetImageInfoAddress() {
  ObjectFile *obj_file = GetTarget().GetExecutableModule()->GetObjectFile();
  Address addr = obj_file->GetImageInfoAddress(&GetTarget());

  if (addr.IsValid())
    return addr.GetLoadAddress(&GetTarget());
  return LLDB_INVALID_ADDRESS;
}

// Parse a FreeBSD NT_PRSTATUS note - see FreeBSD sys/procfs.h for details.
static void ParseFreeBSDPrStatus(ThreadData &thread_data,
                                 const DataExtractor &data,
                                 const ArchSpec &arch) {
  lldb::offset_t offset = 0;
  bool lp64 = (arch.GetMachine() == llvm::Triple::aarch64 ||
               arch.GetMachine() == llvm::Triple::mips64 ||
               arch.GetMachine() == llvm::Triple::ppc64 ||
               arch.GetMachine() == llvm::Triple::x86_64);
  int pr_version = data.GetU32(&offset);

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log) {
    if (pr_version > 1)
      log->Printf("FreeBSD PRSTATUS unexpected version %d", pr_version);
  }

  // Skip padding, pr_statussz, pr_gregsetsz, pr_fpregsetsz, pr_osreldate
  if (lp64)
    offset += 32;
  else
    offset += 16;

  thread_data.signo = data.GetU32(&offset); // pr_cursig
  thread_data.tid = data.GetU32(&offset);   // pr_pid
  if (lp64)
    offset += 4;

  size_t len = data.GetByteSize() - offset;
  thread_data.gpregset = DataExtractor(data, offset, len);
}

static void ParseNetBSDProcInfo(ThreadData &thread_data,
                                const DataExtractor &data) {
  lldb::offset_t offset = 0;

  int version = data.GetU32(&offset);
  if (version != 1)
    return;

  offset += 4;
  thread_data.signo = data.GetU32(&offset);
}

static void ParseOpenBSDProcInfo(ThreadData &thread_data,
                                 const DataExtractor &data) {
  lldb::offset_t offset = 0;

  int version = data.GetU32(&offset);
  if (version != 1)
    return;

  offset += 4;
  thread_data.signo = data.GetU32(&offset);
}

llvm::Expected<std::vector<CoreNote>>
ProcessElfCore::parseSegment(const DataExtractor &segment) {
  lldb::offset_t offset = 0;
  std::vector<CoreNote> result;

  while (offset < segment.GetByteSize()) {
    ELFNote note = ELFNote();
    if (!note.Parse(segment, &offset))
      return llvm::make_error<llvm::StringError>(
          "Unable to parse note segment", llvm::inconvertibleErrorCode());

    size_t note_start = offset;
    size_t note_size = llvm::alignTo(note.n_descsz, 4);
    DataExtractor note_data(segment, note_start, note_size);

    result.push_back({note, note_data});
    offset += note_size;
  }

  return std::move(result);
}

llvm::Error ProcessElfCore::parseFreeBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  bool have_prstatus = false;
  bool have_prpsinfo = false;
  ThreadData thread_data;
  for (const auto &note : notes) {
    if (note.info.n_name != "FreeBSD")
      continue;

    if ((note.info.n_type == FREEBSD::NT_PRSTATUS && have_prstatus) ||
        (note.info.n_type == FREEBSD::NT_PRPSINFO && have_prpsinfo)) {
      assert(thread_data.gpregset.GetByteSize() > 0);
      // Add the new thread to thread list
      m_thread_data.push_back(thread_data);
      thread_data = ThreadData();
      have_prstatus = false;
      have_prpsinfo = false;
    }

    switch (note.info.n_type) {
    case FREEBSD::NT_PRSTATUS:
      have_prstatus = true;
      ParseFreeBSDPrStatus(thread_data, note.data, GetArchitecture());
      break;
    case FREEBSD::NT_PRPSINFO:
      have_prpsinfo = true;
      break;
    case FREEBSD::NT_THRMISC: {
      lldb::offset_t offset = 0;
      thread_data.name = note.data.GetCStr(&offset, 20);
      break;
    }
    case FREEBSD::NT_PROCSTAT_AUXV:
      // FIXME: FreeBSD sticks an int at the beginning of the note
      m_auxv = DataExtractor(note.data, 4, note.data.GetByteSize() - 4);
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  if (!have_prstatus) {
    return llvm::make_error<llvm::StringError>(
        "Could not find NT_PRSTATUS note in core file.",
        llvm::inconvertibleErrorCode());
  }
  m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

llvm::Error ProcessElfCore::parseNetBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  ThreadData thread_data;
  for (const auto &note : notes) {
    // NetBSD per-thread information is stored in notes named "NetBSD-CORE@nnn"
    // so match on the initial part of the string.
    if (!llvm::StringRef(note.info.n_name).startswith("NetBSD-CORE"))
      continue;

    switch (note.info.n_type) {
    case NETBSD::NT_PROCINFO:
      ParseNetBSDProcInfo(thread_data, note.data);
      break;
    case NETBSD::NT_AUXV:
      m_auxv = note.data;
      break;

    case NETBSD::NT_AMD64_REGS:
      if (GetArchitecture().GetMachine() == llvm::Triple::x86_64)
        thread_data.gpregset = note.data;
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  if (thread_data.gpregset.GetByteSize() == 0) {
    return llvm::make_error<llvm::StringError>(
        "Could not find general purpose registers note in core file.",
        llvm::inconvertibleErrorCode());
  }
  m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

llvm::Error ProcessElfCore::parseOpenBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  ThreadData thread_data;
  for (const auto &note : notes) {
    // OpenBSD per-thread information is stored in notes named "OpenBSD@nnn" so
    // match on the initial part of the string.
    if (!llvm::StringRef(note.info.n_name).startswith("OpenBSD"))
      continue;

    switch (note.info.n_type) {
    case OPENBSD::NT_PROCINFO:
      ParseOpenBSDProcInfo(thread_data, note.data);
      break;
    case OPENBSD::NT_AUXV:
      m_auxv = note.data;
      break;
    case OPENBSD::NT_REGS:
      thread_data.gpregset = note.data;
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  if (thread_data.gpregset.GetByteSize() == 0) {
    return llvm::make_error<llvm::StringError>(
        "Could not find general purpose registers note in core file.",
        llvm::inconvertibleErrorCode());
  }
  m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

/// A description of a linux process usually contains the following NOTE
/// entries:
/// - NT_PRPSINFO - General process information like pid, uid, name, ...
/// - NT_SIGINFO - Information about the signal that terminated the process
/// - NT_AUXV - Process auxiliary vector
/// - NT_FILE - Files mapped into memory
/// 
/// Additionally, for each thread in the process the core file will contain at
/// least the NT_PRSTATUS note, containing the thread id and general purpose
/// registers. It may include additional notes for other register sets (floating
/// point and vector registers, ...). The tricky part here is that some of these
/// notes have "CORE" in their owner fields, while other set it to "LINUX".
llvm::Error ProcessElfCore::parseLinuxNotes(llvm::ArrayRef<CoreNote> notes) {
  const ArchSpec &arch = GetArchitecture();
  bool have_prstatus = false;
  bool have_prpsinfo = false;
  ThreadData thread_data;
  for (const auto &note : notes) {
    if (note.info.n_name != "CORE" && note.info.n_name != "LINUX")
      continue;

    if ((note.info.n_type == LINUX::NT_PRSTATUS && have_prstatus) ||
        (note.info.n_type == LINUX::NT_PRPSINFO && have_prpsinfo)) {
      assert(thread_data.gpregset.GetByteSize() > 0);
      // Add the new thread to thread list
      m_thread_data.push_back(thread_data);
      thread_data = ThreadData();
      have_prstatus = false;
      have_prpsinfo = false;
    }

    switch (note.info.n_type) {
    case LINUX::NT_PRSTATUS: {
      have_prstatus = true;
      ELFLinuxPrStatus prstatus;
      Status status = prstatus.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.prstatus_sig = prstatus.pr_cursig;
      thread_data.tid = prstatus.pr_pid;
      uint32_t header_size = ELFLinuxPrStatus::GetSize(arch);
      size_t len = note.data.GetByteSize() - header_size;
      thread_data.gpregset = DataExtractor(note.data, header_size, len);
      break;
    }
    case LINUX::NT_PRPSINFO: {
      have_prpsinfo = true;
      ELFLinuxPrPsInfo prpsinfo;
      Status status = prpsinfo.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.name.assign (prpsinfo.pr_fname, strnlen (prpsinfo.pr_fname, sizeof (prpsinfo.pr_fname)));
      SetID(prpsinfo.pr_pid);
      break;
    }
    case LINUX::NT_SIGINFO: {
      ELFLinuxSigInfo siginfo;
      Status status = siginfo.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.signo = siginfo.si_signo;
      break;
    }
    case LINUX::NT_FILE: {
      m_nt_file_entries.clear();
      lldb::offset_t offset = 0;
      const uint64_t count = note.data.GetAddress(&offset);
      note.data.GetAddress(&offset); // Skip page size
      for (uint64_t i = 0; i < count; ++i) {
        NT_FILE_Entry entry;
        entry.start = note.data.GetAddress(&offset);
        entry.end = note.data.GetAddress(&offset);
        entry.file_ofs = note.data.GetAddress(&offset);
        m_nt_file_entries.push_back(entry);
      }
      for (uint64_t i = 0; i < count; ++i) {
        const char *path = note.data.GetCStr(&offset);
        if (path && path[0])
          m_nt_file_entries[i].path.SetCString(path);
      }
      break;
    }
    case LINUX::NT_AUXV:
      m_auxv = note.data;
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  // Add last entry in the note section
  if (have_prstatus)
    m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

/// Parse Thread context from PT_NOTE segment and store it in the thread list
/// A note segment consists of one or more NOTE entries, but their types and
/// meaning differ depending on the OS.
llvm::Error ProcessElfCore::ParseThreadContextsFromNoteSegment(
    const elf::ELFProgramHeader &segment_header, DataExtractor segment_data) {
  assert(segment_header.p_type == llvm::ELF::PT_NOTE);

  auto notes_or_error = parseSegment(segment_data);
  if(!notes_or_error)
    return notes_or_error.takeError();
  switch (GetArchitecture().GetTriple().getOS()) {
  case llvm::Triple::FreeBSD:
    return parseFreeBSDNotes(*notes_or_error);
  case llvm::Triple::Linux:
    return parseLinuxNotes(*notes_or_error);
  case llvm::Triple::NetBSD:
    return parseNetBSDNotes(*notes_or_error);
  case llvm::Triple::OpenBSD:
    return parseOpenBSDNotes(*notes_or_error);
  default:
    return llvm::make_error<llvm::StringError>(
        "Don't know how to parse core file. Unsupported OS.",
        llvm::inconvertibleErrorCode());
  }
}

uint32_t ProcessElfCore::GetNumThreadContexts() {
  if (!m_thread_data_valid)
    DoLoadCore();
  return m_thread_data.size();
}

ArchSpec ProcessElfCore::GetArchitecture() {
  ArchSpec arch = m_core_module_sp->GetObjectFile()->GetArchitecture();

  ArchSpec target_arch = GetTarget().GetArchitecture();
  arch.MergeFrom(target_arch);

  // On MIPS there is no way to differentiate betwenn 32bit and 64bit core
  // files and this information can't be merged in from the target arch so we
  // fail back to unconditionally returning the target arch in this config.
  if (target_arch.IsMIPS()) {
    return target_arch;
  }

  return arch;
}

const lldb::DataBufferSP ProcessElfCore::GetAuxvData() {
  const uint8_t *start = m_auxv.GetDataStart();
  size_t len = m_auxv.GetByteSize();
  lldb::DataBufferSP buffer(new lldb_private::DataBufferHeap(start, len));
  return buffer;
}

bool ProcessElfCore::GetProcessInfo(ProcessInstanceInfo &info) {
  info.Clear();
  info.SetProcessID(GetID());
  info.SetArchitecture(GetArchitecture());
  lldb::ModuleSP module_sp = GetTarget().GetExecutableModule();
  if (module_sp) {
    const bool add_exe_file_as_first_arg = false;
    info.SetExecutableFile(GetTarget().GetExecutableModule()->GetFileSpec(),
                           add_exe_file_as_first_arg);
  }
  return true;
}
