//===-- DynamicLoaderHexagonDYLD.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/Log.h"

#include "DynamicLoaderHexagonDYLD.h"

using namespace lldb;
using namespace lldb_private;

// Aidan 21/05/2014
//
// Notes about hexagon dynamic loading:
//
//      When we connect to a target we find the dyld breakpoint address.  We put
//      a
//      breakpoint there with a callback 'RendezvousBreakpointHit()'.
//
//      It is possible to find the dyld structure address from the ELF symbol
//      table,
//      but in the case of the simulator it has not been initialized before the
//      target calls dlinit().
//
//      We can only safely parse the dyld structure after we hit the dyld
//      breakpoint
//      since at that time we know dlinit() must have been called.
//

// Find the load address of a symbol
static lldb::addr_t findSymbolAddress(Process *proc, ConstString findName) {
  assert(proc != nullptr);

  ModuleSP module = proc->GetTarget().GetExecutableModule();
  assert(module.get() != nullptr);

  ObjectFile *exe = module->GetObjectFile();
  assert(exe != nullptr);

  lldb_private::Symtab *symtab = exe->GetSymtab();
  assert(symtab != nullptr);

  for (size_t i = 0; i < symtab->GetNumSymbols(); i++) {
    const Symbol *sym = symtab->SymbolAtIndex(i);
    assert(sym != nullptr);
    const ConstString &symName = sym->GetName();

    if (ConstString::Compare(findName, symName) == 0) {
      Address addr = sym->GetAddress();
      return addr.GetLoadAddress(&proc->GetTarget());
    }
  }
  return LLDB_INVALID_ADDRESS;
}

void DynamicLoaderHexagonDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderHexagonDYLD::Terminate() {}

lldb_private::ConstString DynamicLoaderHexagonDYLD::GetPluginName() {
  return GetPluginNameStatic();
}

lldb_private::ConstString DynamicLoaderHexagonDYLD::GetPluginNameStatic() {
  static ConstString g_name("hexagon-dyld");
  return g_name;
}

const char *DynamicLoaderHexagonDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in Hexagon processes.";
}

uint32_t DynamicLoaderHexagonDYLD::GetPluginVersion() { return 1; }

DynamicLoader *DynamicLoaderHexagonDYLD::CreateInstance(Process *process,
                                                        bool force) {
  bool create = force;
  if (!create) {
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (triple_ref.getArch() == llvm::Triple::hexagon)
      create = true;
  }

  if (create)
    return new DynamicLoaderHexagonDYLD(process);
  return NULL;
}

DynamicLoaderHexagonDYLD::DynamicLoaderHexagonDYLD(Process *process)
    : DynamicLoader(process), m_rendezvous(process),
      m_load_offset(LLDB_INVALID_ADDRESS), m_entry_point(LLDB_INVALID_ADDRESS),
      m_dyld_bid(LLDB_INVALID_BREAK_ID) {}

DynamicLoaderHexagonDYLD::~DynamicLoaderHexagonDYLD() {
  if (m_dyld_bid != LLDB_INVALID_BREAK_ID) {
    m_process->GetTarget().RemoveBreakpointByID(m_dyld_bid);
    m_dyld_bid = LLDB_INVALID_BREAK_ID;
  }
}

void DynamicLoaderHexagonDYLD::DidAttach() {
  ModuleSP executable;
  addr_t load_offset;

  executable = GetTargetExecutable();

  // Find the difference between the desired load address in the elf file and
  // the real load address in memory
  load_offset = ComputeLoadOffset();

  // Check that there is a valid executable
  if (executable.get() == nullptr)
    return;

  // Disable JIT for hexagon targets because its not supported
  m_process->SetCanJIT(false);

  // Enable Interpreting of function call expressions
  m_process->SetCanInterpretFunctionCalls(true);

  // Add the current executable to the module list
  ModuleList module_list;
  module_list.Append(executable);

  // Map the loaded sections of this executable
  if (load_offset != LLDB_INVALID_ADDRESS)
    UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_offset, true);

  // AD: confirm this?
  // Load into LLDB all of the currently loaded executables in the stub
  LoadAllCurrentModules();

  // AD: confirm this?
  // Callback for the target to give it the loaded module list
  m_process->GetTarget().ModulesDidLoad(module_list);

  // Try to set a breakpoint at the rendezvous breakpoint. DidLaunch uses
  // ProbeEntry() instead.  That sets a breakpoint, at the dyld breakpoint
  // address, with a callback so that when hit, the dyld structure can be
  // parsed.
  if (!SetRendezvousBreakpoint()) {
    // fail
  }
}

void DynamicLoaderHexagonDYLD::DidLaunch() {}

/// Checks to see if the target module has changed, updates the target
/// accordingly and returns the target executable module.
ModuleSP DynamicLoaderHexagonDYLD::GetTargetExecutable() {
  Target &target = m_process->GetTarget();
  ModuleSP executable = target.GetExecutableModule();

  // There is no executable
  if (!executable.get())
    return executable;

  // The target executable file does not exits
  if (!FileSystem::Instance().Exists(executable->GetFileSpec()))
    return executable;

  // Prep module for loading
  ModuleSpec module_spec(executable->GetFileSpec(),
                         executable->GetArchitecture());
  ModuleSP module_sp(new Module(module_spec));

  // Check if the executable has changed and set it to the target executable if
  // they differ.
  if (module_sp.get() && module_sp->GetUUID().IsValid() &&
      executable->GetUUID().IsValid()) {
    // if the executable has changed ??
    if (module_sp->GetUUID() != executable->GetUUID())
      executable.reset();
  } else if (executable->FileHasChanged())
    executable.reset();

  if (executable.get())
    return executable;

  // TODO: What case is this code used?
  executable = target.GetSharedModule(module_spec);
  if (executable.get() != target.GetExecutableModulePointer()) {
    // Don't load dependent images since we are in dyld where we will know and
    // find out about all images that are loaded
    target.SetExecutableModule(executable, eLoadDependentsNo);
  }

  return executable;
}

// AD: Needs to be updated?
Status DynamicLoaderHexagonDYLD::CanLoadImage() { return Status(); }

void DynamicLoaderHexagonDYLD::UpdateLoadedSections(ModuleSP module,
                                                    addr_t link_map_addr,
                                                    addr_t base_addr,
                                                    bool base_addr_is_offset) {
  Target &target = m_process->GetTarget();
  const SectionList *sections = GetSectionListFromModule(module);

  assert(sections && "SectionList missing from loaded module.");

  m_loaded_modules[module] = link_map_addr;

  const size_t num_sections = sections->GetSize();

  for (unsigned i = 0; i < num_sections; ++i) {
    SectionSP section_sp(sections->GetSectionAtIndex(i));
    lldb::addr_t new_load_addr = section_sp->GetFileAddress() + base_addr;

    // AD: 02/05/14
    //   since our memory map starts from address 0, we must not ignore
    //   sections that load to address 0.  This violates the reference
    //   ELF spec, however is used for Hexagon.

    // If the file address of the section is zero then this is not an
    // allocatable/loadable section (property of ELF sh_addr).  Skip it.
    //      if (new_load_addr == base_addr)
    //          continue;

    target.SetSectionLoadAddress(section_sp, new_load_addr);
  }
}

/// Removes the loaded sections from the target in @p module.
///
/// @param module The module to traverse.
void DynamicLoaderHexagonDYLD::UnloadSections(const ModuleSP module) {
  Target &target = m_process->GetTarget();
  const SectionList *sections = GetSectionListFromModule(module);

  assert(sections && "SectionList missing from unloaded module.");

  m_loaded_modules.erase(module);

  const size_t num_sections = sections->GetSize();
  for (size_t i = 0; i < num_sections; ++i) {
    SectionSP section_sp(sections->GetSectionAtIndex(i));
    target.SetSectionUnloaded(section_sp);
  }
}

// Place a breakpoint on <_rtld_debug_state>
bool DynamicLoaderHexagonDYLD::SetRendezvousBreakpoint() {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  // This is the original code, which want to look in the rendezvous structure
  // to find the breakpoint address.  Its backwards for us, since we can easily
  // find the breakpoint address, since it is exported in our executable. We
  // however know that we cant read the Rendezvous structure until we have hit
  // the breakpoint once.
  const ConstString dyldBpName("_rtld_debug_state");
  addr_t break_addr = findSymbolAddress(m_process, dyldBpName);

  Target &target = m_process->GetTarget();

  // Do not try to set the breakpoint if we don't know where to put it
  if (break_addr == LLDB_INVALID_ADDRESS) {
    if (log)
      log->Printf("Unable to locate _rtld_debug_state breakpoint address");

    return false;
  }

  // Save the address of the rendezvous structure
  m_rendezvous.SetBreakAddress(break_addr);

  // If we haven't set the breakpoint before then set it
  if (m_dyld_bid == LLDB_INVALID_BREAK_ID) {
    Breakpoint *dyld_break =
        target.CreateBreakpoint(break_addr, true, false).get();
    dyld_break->SetCallback(RendezvousBreakpointHit, this, true);
    dyld_break->SetBreakpointKind("shared-library-event");
    m_dyld_bid = dyld_break->GetID();

    // Make sure our breakpoint is at the right address.
    assert(target.GetBreakpointByID(m_dyld_bid)
               ->FindLocationByAddress(break_addr)
               ->GetBreakpoint()
               .GetID() == m_dyld_bid);

    if (log && dyld_break == nullptr)
      log->Printf("Failed to create _rtld_debug_state breakpoint");

    // check we have successfully set bp
    return (dyld_break != nullptr);
  } else
    // rendezvous already set
    return true;
}

// We have just hit our breakpoint at <_rtld_debug_state>
bool DynamicLoaderHexagonDYLD::RendezvousBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  if (log)
    log->Printf("Rendezvous breakpoint hit!");

  DynamicLoaderHexagonDYLD *dyld_instance = nullptr;
  dyld_instance = static_cast<DynamicLoaderHexagonDYLD *>(baton);

  // if the dyld_instance is still not valid then try to locate it on the
  // symbol table
  if (!dyld_instance->m_rendezvous.IsValid()) {
    Process *proc = dyld_instance->m_process;

    const ConstString dyldStructName("_rtld_debug");
    addr_t structAddr = findSymbolAddress(proc, dyldStructName);

    if (structAddr != LLDB_INVALID_ADDRESS) {
      dyld_instance->m_rendezvous.SetRendezvousAddress(structAddr);

      if (log)
        log->Printf("Found _rtld_debug structure @ 0x%08" PRIx64, structAddr);
    } else {
      if (log)
        log->Printf("Unable to resolve the _rtld_debug structure");
    }
  }

  dyld_instance->RefreshModules();

  // Return true to stop the target, false to just let the target run.
  return dyld_instance->GetStopWhenImagesChange();
}

/// Helper method for RendezvousBreakpointHit.  Updates LLDB's current set
/// of loaded modules.
void DynamicLoaderHexagonDYLD::RefreshModules() {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  if (!m_rendezvous.Resolve())
    return;

  HexagonDYLDRendezvous::iterator I;
  HexagonDYLDRendezvous::iterator E;

  ModuleList &loaded_modules = m_process->GetTarget().GetImages();

  if (m_rendezvous.ModulesDidLoad()) {
    ModuleList new_modules;

    E = m_rendezvous.loaded_end();
    for (I = m_rendezvous.loaded_begin(); I != E; ++I) {
      FileSpec file(I->path);
      FileSystem::Instance().Resolve(file);
      ModuleSP module_sp =
          LoadModuleAtAddress(file, I->link_addr, I->base_addr, true);
      if (module_sp.get()) {
        loaded_modules.AppendIfNeeded(module_sp);
        new_modules.Append(module_sp);
      }

      if (log) {
        log->Printf("Target is loading '%s'", I->path.c_str());
        if (!module_sp.get())
          log->Printf("LLDB failed to load '%s'", I->path.c_str());
        else
          log->Printf("LLDB successfully loaded '%s'", I->path.c_str());
      }
    }
    m_process->GetTarget().ModulesDidLoad(new_modules);
  }

  if (m_rendezvous.ModulesDidUnload()) {
    ModuleList old_modules;

    E = m_rendezvous.unloaded_end();
    for (I = m_rendezvous.unloaded_begin(); I != E; ++I) {
      FileSpec file(I->path);
      FileSystem::Instance().Resolve(file);
      ModuleSpec module_spec(file);
      ModuleSP module_sp = loaded_modules.FindFirstModule(module_spec);

      if (module_sp.get()) {
        old_modules.Append(module_sp);
        UnloadSections(module_sp);
      }

      if (log)
        log->Printf("Target is unloading '%s'", I->path.c_str());
    }
    loaded_modules.Remove(old_modules);
    m_process->GetTarget().ModulesDidUnload(old_modules, false);
  }
}

// AD:	This is very different to the Static Loader code.
//		It may be wise to look over this and its relation to stack
//		unwinding.
ThreadPlanSP
DynamicLoaderHexagonDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                       bool stop) {
  ThreadPlanSP thread_plan_sp;

  StackFrame *frame = thread.GetStackFrameAtIndex(0).get();
  const SymbolContext &context = frame->GetSymbolContext(eSymbolContextSymbol);
  Symbol *sym = context.symbol;

  if (sym == NULL || !sym->IsTrampoline())
    return thread_plan_sp;

  const ConstString sym_name = sym->GetMangled().GetName(
      lldb::eLanguageTypeUnknown, Mangled::ePreferMangled);
  if (!sym_name)
    return thread_plan_sp;

  SymbolContextList target_symbols;
  Target &target = thread.GetProcess()->GetTarget();
  const ModuleList &images = target.GetImages();

  images.FindSymbolsWithNameAndType(sym_name, eSymbolTypeCode, target_symbols);
  size_t num_targets = target_symbols.GetSize();
  if (!num_targets)
    return thread_plan_sp;

  typedef std::vector<lldb::addr_t> AddressVector;
  AddressVector addrs;
  for (size_t i = 0; i < num_targets; ++i) {
    SymbolContext context;
    AddressRange range;
    if (target_symbols.GetContextAtIndex(i, context)) {
      context.GetAddressRange(eSymbolContextEverything, 0, false, range);
      lldb::addr_t addr = range.GetBaseAddress().GetLoadAddress(&target);
      if (addr != LLDB_INVALID_ADDRESS)
        addrs.push_back(addr);
    }
  }

  if (addrs.size() > 0) {
    AddressVector::iterator start = addrs.begin();
    AddressVector::iterator end = addrs.end();

    llvm::sort(start, end);
    addrs.erase(std::unique(start, end), end);
    thread_plan_sp.reset(new ThreadPlanRunToAddress(thread, addrs, stop));
  }

  return thread_plan_sp;
}

/// Helper for the entry breakpoint callback.  Resolves the load addresses
/// of all dependent modules.
void DynamicLoaderHexagonDYLD::LoadAllCurrentModules() {
  HexagonDYLDRendezvous::iterator I;
  HexagonDYLDRendezvous::iterator E;
  ModuleList module_list;

  if (!m_rendezvous.Resolve()) {
    Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
    if (log)
      log->Printf(
          "DynamicLoaderHexagonDYLD::%s unable to resolve rendezvous address",
          __FUNCTION__);
    return;
  }

  // The rendezvous class doesn't enumerate the main module, so track that
  // ourselves here.
  ModuleSP executable = GetTargetExecutable();
  m_loaded_modules[executable] = m_rendezvous.GetLinkMapAddress();

  for (I = m_rendezvous.begin(), E = m_rendezvous.end(); I != E; ++I) {
    const char *module_path = I->path.c_str();
    FileSpec file(module_path);
    ModuleSP module_sp =
        LoadModuleAtAddress(file, I->link_addr, I->base_addr, true);
    if (module_sp.get()) {
      module_list.Append(module_sp);
    } else {
      Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
      if (log)
        log->Printf("DynamicLoaderHexagonDYLD::%s failed loading module %s at "
                    "0x%" PRIx64,
                    __FUNCTION__, module_path, I->base_addr);
    }
  }

  m_process->GetTarget().ModulesDidLoad(module_list);
}

/// Computes a value for m_load_offset returning the computed address on
/// success and LLDB_INVALID_ADDRESS on failure.
addr_t DynamicLoaderHexagonDYLD::ComputeLoadOffset() {
  // Here we could send a GDB packet to know the load offset
  //
  // send:    $qOffsets#4b
  // get:     Text=0;Data=0;Bss=0
  //
  // Currently qOffsets is not supported by pluginProcessGDBRemote
  //
  return 0;
}

// Here we must try to read the entry point directly from the elf header.  This
// is possible if the process is not relocatable or dynamically linked.
//
// an alternative is to look at the PC if we can be sure that we have connected
// when the process is at the entry point.
// I dont think that is reliable for us.
addr_t DynamicLoaderHexagonDYLD::GetEntryPoint() {
  if (m_entry_point != LLDB_INVALID_ADDRESS)
    return m_entry_point;
  // check we have a valid process
  if (m_process == nullptr)
    return LLDB_INVALID_ADDRESS;
  // Get the current executable module
  Module &module = *(m_process->GetTarget().GetExecutableModule().get());
  // Get the object file (elf file) for this module
  lldb_private::ObjectFile &object = *(module.GetObjectFile());
  // Check if the file is executable (ie, not shared object or relocatable)
  if (object.IsExecutable()) {
    // Get the entry point address for this object
    lldb_private::Address entry = object.GetEntryPointAddress();
    // Return the entry point address
    return entry.GetFileAddress();
  }
  // No idea so back out
  return LLDB_INVALID_ADDRESS;
}

const SectionList *DynamicLoaderHexagonDYLD::GetSectionListFromModule(
    const ModuleSP module) const {
  SectionList *sections = nullptr;
  if (module.get()) {
    ObjectFile *obj_file = module->GetObjectFile();
    if (obj_file) {
      sections = obj_file->GetSectionList();
    }
  }
  return sections;
}

static int ReadInt(Process *process, addr_t addr) {
  Status error;
  int value = (int)process->ReadUnsignedIntegerFromMemory(
      addr, sizeof(uint32_t), 0, error);
  if (error.Fail())
    return -1;
  else
    return value;
}

lldb::addr_t
DynamicLoaderHexagonDYLD::GetThreadLocalData(const lldb::ModuleSP module,
                                             const lldb::ThreadSP thread,
                                             lldb::addr_t tls_file_addr) {
  auto it = m_loaded_modules.find(module);
  if (it == m_loaded_modules.end())
    return LLDB_INVALID_ADDRESS;

  addr_t link_map = it->second;
  if (link_map == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  const HexagonDYLDRendezvous::ThreadInfo &metadata =
      m_rendezvous.GetThreadInfo();
  if (!metadata.valid)
    return LLDB_INVALID_ADDRESS;

  // Get the thread pointer.
  addr_t tp = thread->GetThreadPointer();
  if (tp == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  // Find the module's modid.
  int modid = ReadInt(m_process, link_map + metadata.modid_offset);
  if (modid == -1)
    return LLDB_INVALID_ADDRESS;

  // Lookup the DTV structure for this thread.
  addr_t dtv_ptr = tp + metadata.dtv_offset;
  addr_t dtv = ReadPointer(dtv_ptr);
  if (dtv == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  // Find the TLS block for this module.
  addr_t dtv_slot = dtv + metadata.dtv_slot_size * modid;
  addr_t tls_block = ReadPointer(dtv_slot + metadata.tls_offset);

  Module *mod = module.get();
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
  if (log)
    log->Printf("DynamicLoaderHexagonDYLD::Performed TLS lookup: "
                "module=%s, link_map=0x%" PRIx64 ", tp=0x%" PRIx64
                ", modid=%i, tls_block=0x%" PRIx64,
                mod->GetObjectName().AsCString(""), link_map, tp, modid,
                tls_block);

  if (tls_block == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;
  else
    return tls_block + tls_file_addr;
}
