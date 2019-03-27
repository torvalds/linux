//===-- SBTarget.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTarget_h_
#define LLDB_SBTarget_h_

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBAttachInfo.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFileSpecList.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/API/SBType.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBWatchpoint.h"

namespace lldb {

class SBPlatform;

class LLDB_API SBTarget {
public:
  //------------------------------------------------------------------
  // Broadcaster bits.
  //------------------------------------------------------------------
  enum {
    eBroadcastBitBreakpointChanged = (1 << 0),
    eBroadcastBitModulesLoaded = (1 << 1),
    eBroadcastBitModulesUnloaded = (1 << 2),
    eBroadcastBitWatchpointChanged = (1 << 3),
    eBroadcastBitSymbolsLoaded = (1 << 4)
  };

  //------------------------------------------------------------------
  // Constructors
  //------------------------------------------------------------------
  SBTarget();

  SBTarget(const lldb::SBTarget &rhs);

  SBTarget(const lldb::TargetSP &target_sp);

  //------------------------------------------------------------------
  // Destructor
  //------------------------------------------------------------------
  ~SBTarget();

  const lldb::SBTarget &operator=(const lldb::SBTarget &rhs);

  bool IsValid() const;

  static bool EventIsTargetEvent(const lldb::SBEvent &event);

  static lldb::SBTarget GetTargetFromEvent(const lldb::SBEvent &event);

  static uint32_t GetNumModulesFromEvent(const lldb::SBEvent &event);

  static lldb::SBModule GetModuleAtIndexFromEvent(const uint32_t idx,
                                                  const lldb::SBEvent &event);

  static const char *GetBroadcasterClassName();

  lldb::SBProcess GetProcess();

  //------------------------------------------------------------------
  /// Sets whether we should collect statistics on lldb or not.
  ///
  /// @param[in] v
  ///     A boolean to control the collection.
  /// @return
  ///     void
  //------------------------------------------------------------------
  void SetCollectingStats(bool v);

  //------------------------------------------------------------------
  /// Returns whether statistics collection are enabled.
  ///
  /// @return
  ///     true if statistics are currently being collected, false
  ///     otherwise.
  //------------------------------------------------------------------
  bool GetCollectingStats();

  //------------------------------------------------------------------
  /// Returns a dump of the collected statistics.
  ///
  /// @return
  ///     A SBStructuredData with the statistics collected.
  //------------------------------------------------------------------
  lldb::SBStructuredData GetStatistics();

  //------------------------------------------------------------------
  /// Return the platform object associated with the target.
  ///
  /// After return, the platform object should be checked for
  /// validity.
  ///
  /// @return
  ///     A platform object.
  //------------------------------------------------------------------
  lldb::SBPlatform GetPlatform();

  //------------------------------------------------------------------
  /// Install any binaries that need to be installed.
  ///
  /// This function does nothing when debugging on the host system.
  /// When connected to remote platforms, the target's main executable
  /// and any modules that have their remote install path set will be
  /// installed on the remote platform. If the main executable doesn't
  /// have an install location set, it will be installed in the remote
  /// platform's working directory.
  ///
  /// @return
  ///     An error describing anything that went wrong during
  ///     installation.
  //------------------------------------------------------------------
  SBError Install();

  //------------------------------------------------------------------
  /// Launch a new process.
  ///
  /// Launch a new process by spawning a new process using the
  /// target object's executable module's file as the file to launch.
  /// Arguments are given in \a argv, and the environment variables
  /// are in \a envp. Standard input and output files can be
  /// optionally re-directed to \a stdin_path, \a stdout_path, and
  /// \a stderr_path.
  ///
  /// @param[in] listener
  ///     An optional listener that will receive all process events.
  ///     If \a listener is valid then \a listener will listen to all
  ///     process events. If not valid, then this target's debugger
  ///     (SBTarget::GetDebugger()) will listen to all process events.
  ///
  /// @param[in] argv
  ///     The argument array.
  ///
  /// @param[in] envp
  ///     The environment array.
  ///
  /// @param[in] stdin_path
  ///     The path to use when re-directing the STDIN of the new
  ///     process. If all stdXX_path arguments are nullptr, a pseudo
  ///     terminal will be used.
  ///
  /// @param[in] stdout_path
  ///     The path to use when re-directing the STDOUT of the new
  ///     process. If all stdXX_path arguments are nullptr, a pseudo
  ///     terminal will be used.
  ///
  /// @param[in] stderr_path
  ///     The path to use when re-directing the STDERR of the new
  ///     process. If all stdXX_path arguments are nullptr, a pseudo
  ///     terminal will be used.
  ///
  /// @param[in] working_directory
  ///     The working directory to have the child process run in
  ///
  /// @param[in] launch_flags
  ///     Some launch options specified by logical OR'ing
  ///     lldb::LaunchFlags enumeration values together.
  ///
  /// @param[in] stop_at_entry
  ///     If false do not stop the inferior at the entry point.
  ///
  /// @param[out] error
  ///     An error object. Contains the reason if there is some failure.
  ///
  /// @return
  ///      A process object for the newly created process.
  //------------------------------------------------------------------
  lldb::SBProcess Launch(SBListener &listener, char const **argv,
                         char const **envp, const char *stdin_path,
                         const char *stdout_path, const char *stderr_path,
                         const char *working_directory,
                         uint32_t launch_flags, // See LaunchFlags
                         bool stop_at_entry, lldb::SBError &error);

  SBProcess LoadCore(const char *core_file);
  SBProcess LoadCore(const char *core_file, lldb::SBError &error);

  //------------------------------------------------------------------
  /// Launch a new process with sensible defaults.
  ///
  /// @param[in] argv
  ///     The argument array.
  ///
  /// @param[in] envp
  ///     The environment array.
  ///
  /// @param[in] working_directory
  ///     The working directory to have the child process run in
  ///
  /// Default: listener
  ///     Set to the target's debugger (SBTarget::GetDebugger())
  ///
  /// Default: launch_flags
  ///     Empty launch flags
  ///
  /// Default: stdin_path
  /// Default: stdout_path
  /// Default: stderr_path
  ///     A pseudo terminal will be used.
  ///
  /// @return
  ///      A process object for the newly created process.
  //------------------------------------------------------------------
  SBProcess LaunchSimple(const char **argv, const char **envp,
                         const char *working_directory);

  SBProcess Launch(SBLaunchInfo &launch_info, SBError &error);

  SBProcess Attach(SBAttachInfo &attach_info, SBError &error);

  //------------------------------------------------------------------
  /// Attach to process with pid.
  ///
  /// @param[in] listener
  ///     An optional listener that will receive all process events.
  ///     If \a listener is valid then \a listener will listen to all
  ///     process events. If not valid, then this target's debugger
  ///     (SBTarget::GetDebugger()) will listen to all process events.
  ///
  /// @param[in] pid
  ///     The process ID to attach to.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong if attach fails.
  ///
  /// @return
  ///      A process object for the attached process.
  //------------------------------------------------------------------
  lldb::SBProcess AttachToProcessWithID(SBListener &listener, lldb::pid_t pid,
                                        lldb::SBError &error);

  //------------------------------------------------------------------
  /// Attach to process with name.
  ///
  /// @param[in] listener
  ///     An optional listener that will receive all process events.
  ///     If \a listener is valid then \a listener will listen to all
  ///     process events. If not valid, then this target's debugger
  ///     (SBTarget::GetDebugger()) will listen to all process events.
  ///
  /// @param[in] name
  ///     Basename of process to attach to.
  ///
  /// @param[in] wait_for
  ///     If true wait for a new instance of 'name' to be launched.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong if attach fails.
  ///
  /// @return
  ///      A process object for the attached process.
  //------------------------------------------------------------------
  lldb::SBProcess AttachToProcessWithName(SBListener &listener,
                                          const char *name, bool wait_for,
                                          lldb::SBError &error);

  //------------------------------------------------------------------
  /// Connect to a remote debug server with url.
  ///
  /// @param[in] listener
  ///     An optional listener that will receive all process events.
  ///     If \a listener is valid then \a listener will listen to all
  ///     process events. If not valid, then this target's debugger
  ///     (SBTarget::GetDebugger()) will listen to all process events.
  ///
  /// @param[in] url
  ///     The url to connect to, e.g., 'connect://localhost:12345'.
  ///
  /// @param[in] plugin_name
  ///     The plugin name to be used; can be nullptr.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong if the connect fails.
  ///
  /// @return
  ///      A process object for the connected process.
  //------------------------------------------------------------------
  lldb::SBProcess ConnectRemote(SBListener &listener, const char *url,
                                const char *plugin_name, SBError &error);

  lldb::SBFileSpec GetExecutable();

  // Append the path mapping (from -> to) to the target's paths mapping list.
  void AppendImageSearchPath(const char *from, const char *to,
                             lldb::SBError &error);

  bool AddModule(lldb::SBModule &module);

  lldb::SBModule AddModule(const char *path, const char *triple,
                           const char *uuid);

  lldb::SBModule AddModule(const char *path, const char *triple,
                           const char *uuid_cstr, const char *symfile);

  lldb::SBModule AddModule(const SBModuleSpec &module_spec);

  uint32_t GetNumModules() const;

  lldb::SBModule GetModuleAtIndex(uint32_t idx);

  bool RemoveModule(lldb::SBModule module);

  lldb::SBDebugger GetDebugger() const;

  lldb::SBModule FindModule(const lldb::SBFileSpec &file_spec);

  //------------------------------------------------------------------
  /// Find compile units related to *this target and passed source
  /// file.
  ///
  /// @param[in] sb_file_spec
  ///     A lldb::SBFileSpec object that contains source file
  ///     specification.
  ///
  /// @return
  ///     A lldb::SBSymbolContextList that gets filled in with all of
  ///     the symbol contexts for all the matches.
  //------------------------------------------------------------------
  lldb::SBSymbolContextList
  FindCompileUnits(const lldb::SBFileSpec &sb_file_spec);

  lldb::ByteOrder GetByteOrder();

  uint32_t GetAddressByteSize();

  const char *GetTriple();

  //------------------------------------------------------------------
  /// Architecture data byte width accessor
  ///
  /// @return
  /// The size in 8-bit (host) bytes of a minimum addressable
  /// unit from the Architecture's data bus
  //------------------------------------------------------------------
  uint32_t GetDataByteSize();

  //------------------------------------------------------------------
  /// Architecture code byte width accessor
  ///
  /// @return
  /// The size in 8-bit (host) bytes of a minimum addressable
  /// unit from the Architecture's code bus
  //------------------------------------------------------------------
  uint32_t GetCodeByteSize();

  //------------------------------------------------------------------
  /// Set the base load address for a module section.
  ///
  /// @param[in] section
  ///     The section whose base load address will be set within this
  ///     target.
  ///
  /// @param[in] section_base_addr
  ///     The base address for the section.
  ///
  /// @return
  ///      An error to indicate success, fail, and any reason for
  ///     failure.
  //------------------------------------------------------------------
  lldb::SBError SetSectionLoadAddress(lldb::SBSection section,
                                      lldb::addr_t section_base_addr);

  //------------------------------------------------------------------
  /// Clear the base load address for a module section.
  ///
  /// @param[in] section
  ///     The section whose base load address will be cleared within
  ///     this target.
  ///
  /// @return
  ///      An error to indicate success, fail, and any reason for
  ///     failure.
  //------------------------------------------------------------------
  lldb::SBError ClearSectionLoadAddress(lldb::SBSection section);

  //------------------------------------------------------------------
  /// Slide all file addresses for all module sections so that \a module
  /// appears to loaded at these slide addresses.
  ///
  /// When you need all sections within a module to be loaded at a
  /// rigid slide from the addresses found in the module object file,
  /// this function will allow you to easily and quickly slide all
  /// module sections.
  ///
  /// @param[in] module
  ///     The module to load.
  ///
  /// @param[in] sections_offset
  ///     An offset that will be applied to all section file addresses
  ///     (the virtual addresses found in the object file itself).
  ///
  /// @return
  ///     An error to indicate success, fail, and any reason for
  ///     failure.
  //------------------------------------------------------------------
  lldb::SBError SetModuleLoadAddress(lldb::SBModule module,
                                     int64_t sections_offset);

  //------------------------------------------------------------------
  /// Clear the section base load addresses for all sections in a module.
  ///
  /// @param[in] module
  ///     The module to unload.
  ///
  /// @return
  ///     An error to indicate success, fail, and any reason for
  ///     failure.
  //------------------------------------------------------------------
  lldb::SBError ClearModuleLoadAddress(lldb::SBModule module);

  //------------------------------------------------------------------
  /// Find functions by name.
  ///
  /// @param[in] name
  ///     The name of the function we are looking for.
  ///
  /// @param[in] name_type_mask
  ///     A logical OR of one or more FunctionNameType enum bits that
  ///     indicate what kind of names should be used when doing the
  ///     lookup. Bits include fully qualified names, base names,
  ///     C++ methods, or ObjC selectors.
  ///     See FunctionNameType for more details.
  ///
  /// @return
  ///     A lldb::SBSymbolContextList that gets filled in with all of
  ///     the symbol contexts for all the matches.
  //------------------------------------------------------------------
  lldb::SBSymbolContextList
  FindFunctions(const char *name,
                uint32_t name_type_mask = lldb::eFunctionNameTypeAny);

  //------------------------------------------------------------------
  /// Find global and static variables by name.
  ///
  /// @param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a max_matches.
  ///
  /// @return
  ///     A list of matched variables in an SBValueList.
  //------------------------------------------------------------------
  lldb::SBValueList FindGlobalVariables(const char *name, uint32_t max_matches);

  //------------------------------------------------------------------
  /// Find the first global (or static) variable by name.
  ///
  /// @param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// @return
  ///     An SBValue that gets filled in with the found variable (if any).
  //------------------------------------------------------------------
  lldb::SBValue FindFirstGlobalVariable(const char *name);

  //------------------------------------------------------------------
  /// Find global and static variables by pattern.
  ///
  /// @param[in] name
  ///     The pattern to search for global or static variables
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a max_matches.
  ///
  /// @param[in] matchtype
  ///     The match type to use.
  ///
  /// @return
  ///     A list of matched variables in an SBValueList.
  //------------------------------------------------------------------
  lldb::SBValueList FindGlobalVariables(const char *name, uint32_t max_matches,
                                        MatchType matchtype);

  //------------------------------------------------------------------
  /// Find global functions by their name with pattern matching.
  ///
  /// @param[in] name
  ///     The pattern to search for global or static variables
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a max_matches.
  ///
  /// @param[in] matchtype
  ///     The match type to use.
  ///
  /// @return
  ///     A list of matched variables in an SBValueList.
  //------------------------------------------------------------------
  lldb::SBSymbolContextList FindGlobalFunctions(const char *name,
                                                uint32_t max_matches,
                                                MatchType matchtype);

  void Clear();

  //------------------------------------------------------------------
  /// Resolve a current file address into a section offset address.
  ///
  /// @param[in] file_addr
  ///     The file address to resolve.
  ///
  /// @return
  ///     An SBAddress which will be valid if...
  //------------------------------------------------------------------
  lldb::SBAddress ResolveFileAddress(lldb::addr_t file_addr);

  //------------------------------------------------------------------
  /// Resolve a current load address into a section offset address.
  ///
  /// @param[in] vm_addr
  ///     A virtual address from the current process state that is to
  ///     be translated into a section offset address.
  ///
  /// @return
  ///     An SBAddress which will be valid if \a vm_addr was
  ///     successfully resolved into a section offset address, or an
  ///     invalid SBAddress if \a vm_addr doesn't resolve to a section
  ///     in a module.
  //------------------------------------------------------------------
  lldb::SBAddress ResolveLoadAddress(lldb::addr_t vm_addr);

  //------------------------------------------------------------------
  /// Resolve a current load address into a section offset address
  /// using the process stop ID to identify a time in the past.
  ///
  /// @param[in] stop_id
  ///     Each time a process stops, the process stop ID integer gets
  ///     incremented. These stop IDs are used to identify past times
  ///     and can be used in history objects as a cheap way to store
  ///     the time at which the sample was taken. Specifying
  ///     UINT32_MAX will always resolve the address using the
  ///     currently loaded sections.
  ///
  /// @param[in] vm_addr
  ///     A virtual address from the current process state that is to
  ///     be translated into a section offset address.
  ///
  /// @return
  ///     An SBAddress which will be valid if \a vm_addr was
  ///     successfully resolved into a section offset address, or an
  ///     invalid SBAddress if \a vm_addr doesn't resolve to a section
  ///     in a module.
  //------------------------------------------------------------------
  lldb::SBAddress ResolvePastLoadAddress(uint32_t stop_id,
                                         lldb::addr_t vm_addr);

  SBSymbolContext ResolveSymbolContextForAddress(const SBAddress &addr,
                                                 uint32_t resolve_scope);

  //------------------------------------------------------------------
  /// Read target memory. If a target process is running then memory
  /// is read from here. Otherwise the memory is read from the object
  /// files. For a target whose bytes are sized as a multiple of host
  /// bytes, the data read back will preserve the target's byte order.
  ///
  /// @param[in] addr
  ///     A target address to read from.
  ///
  /// @param[out] buf
  ///     The buffer to read memory into.
  ///
  /// @param[in] size
  ///     The maximum number of host bytes to read in the buffer passed
  ///     into this call
  ///
  /// @param[out] error
  ///     Status information is written here if the memory read fails.
  ///
  /// @return
  ///     The amount of data read in host bytes.
  //------------------------------------------------------------------
  size_t ReadMemory(const SBAddress addr, void *buf, size_t size,
                    lldb::SBError &error);

  lldb::SBBreakpoint BreakpointCreateByLocation(const char *file,
                                                uint32_t line);

  lldb::SBBreakpoint
  BreakpointCreateByLocation(const lldb::SBFileSpec &file_spec, uint32_t line);

  lldb::SBBreakpoint
  BreakpointCreateByLocation(const lldb::SBFileSpec &file_spec, uint32_t line,
                             lldb::addr_t offset);

  lldb::SBBreakpoint
  BreakpointCreateByLocation(const lldb::SBFileSpec &file_spec, uint32_t line,
                             lldb::addr_t offset, SBFileSpecList &module_list);

  lldb::SBBreakpoint
  BreakpointCreateByLocation(const lldb::SBFileSpec &file_spec, uint32_t line,
                             uint32_t column, lldb::addr_t offset,
                             SBFileSpecList &module_list);

  lldb::SBBreakpoint BreakpointCreateByName(const char *symbol_name,
                                            const char *module_name = nullptr);

  // This version uses name_type_mask = eFunctionNameTypeAuto
  lldb::SBBreakpoint
  BreakpointCreateByName(const char *symbol_name,
                         const SBFileSpecList &module_list,
                         const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByName(
      const char *symbol_name,
      uint32_t
          name_type_mask, // Logical OR one or more FunctionNameType enum bits
      const SBFileSpecList &module_list,
      const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByName(
      const char *symbol_name,
      uint32_t
          name_type_mask, // Logical OR one or more FunctionNameType enum bits
      lldb::LanguageType symbol_language,
      const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByNames(
      const char *symbol_name[], uint32_t num_names,
      uint32_t
          name_type_mask, // Logical OR one or more FunctionNameType enum bits
      const SBFileSpecList &module_list,
      const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByNames(
      const char *symbol_name[], uint32_t num_names,
      uint32_t
          name_type_mask, // Logical OR one or more FunctionNameType enum bits
      lldb::LanguageType symbol_language,
      const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByNames(
      const char *symbol_name[], uint32_t num_names,
      uint32_t
          name_type_mask, // Logical OR one or more FunctionNameType enum bits
      lldb::LanguageType symbol_language,
      lldb::addr_t offset, const SBFileSpecList &module_list,
      const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByRegex(const char *symbol_name_regex,
                                             const char *module_name = nullptr);

  lldb::SBBreakpoint
  BreakpointCreateByRegex(const char *symbol_name_regex,
                          const SBFileSpecList &module_list,
                          const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint BreakpointCreateByRegex(
      const char *symbol_name_regex, lldb::LanguageType symbol_language,
      const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list);

  lldb::SBBreakpoint
  BreakpointCreateBySourceRegex(const char *source_regex,
                                const SBFileSpec &source_file,
                                const char *module_name = nullptr);

  lldb::SBBreakpoint
  BreakpointCreateBySourceRegex(const char *source_regex,
                                const SBFileSpecList &module_list,
                                const SBFileSpecList &source_file);

  lldb::SBBreakpoint BreakpointCreateBySourceRegex(
      const char *source_regex, const SBFileSpecList &module_list,
      const SBFileSpecList &source_file, const SBStringList &func_names);

  lldb::SBBreakpoint BreakpointCreateForException(lldb::LanguageType language,
                                                  bool catch_bp, bool throw_bp);

  lldb::SBBreakpoint BreakpointCreateByAddress(addr_t address);

  lldb::SBBreakpoint BreakpointCreateBySBAddress(SBAddress &address);
  
  //------------------------------------------------------------------
  /// Create a breakpoint using a scripted resolver.
  ///
  /// @param[in] class_name
  ///    This is the name of the class that implements a scripted resolver.
  ///
  /// @param[in] extra_args
  ///    This is an SBStructuredData object that will get passed to the
  ///    constructor of the class in class_name.  You can use this to 
  ///    reuse the same class, parametrizing with entries from this 
  ///    dictionary.
  ///
  /// @param module_list
  ///    If this is non-empty, this will be used as the module filter in the 
  ///    SearchFilter created for this breakpoint.
  ///
  /// @param file_list
  ///    If this is non-empty, this will be used as the comp unit filter in the 
  ///    SearchFilter created for this breakpoint.
  ///
  /// @return
  ///     An SBBreakpoint that will set locations based on the logic in the
  ///     resolver's search callback.
  //------------------------------------------------------------------
  lldb::SBBreakpoint BreakpointCreateFromScript(
      const char *class_name,
      SBStructuredData &extra_args,
      const SBFileSpecList &module_list,
      const SBFileSpecList &file_list,
      bool request_hardware = false);

  //------------------------------------------------------------------
  /// Read breakpoints from source_file and return the newly created
  /// breakpoints in bkpt_list.
  ///
  /// @param[in] source_file
  ///    The file from which to read the breakpoints.
  ///
  /// @param[out] new_bps
  ///    A list of the newly created breakpoints.
  ///
  /// @return
  ///     An SBError detailing any errors in reading in the breakpoints.
  //------------------------------------------------------------------
  lldb::SBError BreakpointsCreateFromFile(SBFileSpec &source_file,
                                          SBBreakpointList &new_bps);

  //------------------------------------------------------------------
  /// Read breakpoints from source_file and return the newly created
  /// breakpoints in bkpt_list.
  ///
  /// @param[in] source_file
  ///    The file from which to read the breakpoints.
  ///
  /// @param[in] matching_names
  ///    Only read in breakpoints whose names match one of the names in this
  ///    list.
  ///
  /// @param[out] new_bps
  ///    A list of the newly created breakpoints.
  ///
  /// @return
  ///     An SBError detailing any errors in reading in the breakpoints.
  //------------------------------------------------------------------
  lldb::SBError BreakpointsCreateFromFile(SBFileSpec &source_file,
                                          SBStringList &matching_names,
                                          SBBreakpointList &new_bps);

  //------------------------------------------------------------------
  /// Write breakpoints to dest_file.
  ///
  /// @param[in] dest_file
  ///    The file to which to write the breakpoints.
  ///
  /// @return
  ///     An SBError detailing any errors in writing in the breakpoints.
  //------------------------------------------------------------------
  lldb::SBError BreakpointsWriteToFile(SBFileSpec &dest_file);

  //------------------------------------------------------------------
  /// Write breakpoints listed in bkpt_list to dest_file.
  ///
  /// @param[in] dest_file
  ///    The file to which to write the breakpoints.
  ///
  /// @param[in] bkpt_list
  ///    Only write breakpoints from this list.
  ///
  /// @param[in] append
  ///    If \btrue, append the breakpoints in bkpt_list to the others
  ///    serialized in dest_file.  If dest_file doesn't exist, then a new
  ///    file will be created and the breakpoints in bkpt_list written to it.
  ///
  /// @return
  ///     An SBError detailing any errors in writing in the breakpoints.
  //------------------------------------------------------------------
  lldb::SBError BreakpointsWriteToFile(SBFileSpec &dest_file,
                                       SBBreakpointList &bkpt_list,
                                       bool append = false);

  uint32_t GetNumBreakpoints() const;

  lldb::SBBreakpoint GetBreakpointAtIndex(uint32_t idx) const;

  bool BreakpointDelete(break_id_t break_id);

  lldb::SBBreakpoint FindBreakpointByID(break_id_t break_id);

  // Finds all breakpoints by name, returning the list in bkpt_list.  Returns
  // false if the name is not a valid breakpoint name, true otherwise.
  bool FindBreakpointsByName(const char *name, SBBreakpointList &bkpt_list);

  void GetBreakpointNames(SBStringList &names);

  void DeleteBreakpointName(const char *name);

  bool EnableAllBreakpoints();

  bool DisableAllBreakpoints();

  bool DeleteAllBreakpoints();

  uint32_t GetNumWatchpoints() const;

  lldb::SBWatchpoint GetWatchpointAtIndex(uint32_t idx) const;

  bool DeleteWatchpoint(lldb::watch_id_t watch_id);

  lldb::SBWatchpoint FindWatchpointByID(lldb::watch_id_t watch_id);

  lldb::SBWatchpoint WatchAddress(lldb::addr_t addr, size_t size, bool read,
                                  bool write, SBError &error);

  bool EnableAllWatchpoints();

  bool DisableAllWatchpoints();

  bool DeleteAllWatchpoints();

  lldb::SBBroadcaster GetBroadcaster() const;

  lldb::SBType FindFirstType(const char *type);

  lldb::SBTypeList FindTypes(const char *type);

  lldb::SBType GetBasicType(lldb::BasicType type);

  lldb::SBValue CreateValueFromAddress(const char *name, lldb::SBAddress addr,
                                       lldb::SBType type);

  lldb::SBValue CreateValueFromData(const char *name, lldb::SBData data,
                                    lldb::SBType type);

  lldb::SBValue CreateValueFromExpression(const char *name, const char *expr);

  SBSourceManager GetSourceManager();

  lldb::SBInstructionList ReadInstructions(lldb::SBAddress base_addr,
                                           uint32_t count);

  lldb::SBInstructionList ReadInstructions(lldb::SBAddress base_addr,
                                           uint32_t count,
                                           const char *flavor_string);

  lldb::SBInstructionList GetInstructions(lldb::SBAddress base_addr,
                                          const void *buf, size_t size);

  // The "WithFlavor" is necessary to keep SWIG from getting confused about
  // overloaded arguments when using the buf + size -> Python Object magic.

  lldb::SBInstructionList GetInstructionsWithFlavor(lldb::SBAddress base_addr,
                                                    const char *flavor_string,
                                                    const void *buf,
                                                    size_t size);

  lldb::SBInstructionList GetInstructions(lldb::addr_t base_addr,
                                          const void *buf, size_t size);

  lldb::SBInstructionList GetInstructionsWithFlavor(lldb::addr_t base_addr,
                                                    const char *flavor_string,
                                                    const void *buf,
                                                    size_t size);

  lldb::SBSymbolContextList FindSymbols(const char *name,
                                        lldb::SymbolType type = eSymbolTypeAny);

  bool operator==(const lldb::SBTarget &rhs) const;

  bool operator!=(const lldb::SBTarget &rhs) const;

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBValue EvaluateExpression(const char *expr);

  lldb::SBValue EvaluateExpression(const char *expr,
                                   const SBExpressionOptions &options);

  lldb::addr_t GetStackRedZoneSize();

  lldb::SBLaunchInfo GetLaunchInfo() const;

  void SetLaunchInfo(const lldb::SBLaunchInfo &launch_info);

protected:
  friend class SBAddress;
  friend class SBBlock;
  friend class SBBreakpointList;
  friend class SBBreakpointNameImpl;
  friend class SBDebugger;
  friend class SBExecutionContext;
  friend class SBFunction;
  friend class SBInstruction;
  friend class SBModule;
  friend class SBProcess;
  friend class SBSection;
  friend class SBSourceManager;
  friend class SBSymbol;
  friend class SBValue;
  friend class SBVariablesOptions;

  //------------------------------------------------------------------
  // Constructors are private, use static Target::Create function to create an
  // instance of this class.
  //------------------------------------------------------------------

  lldb::TargetSP GetSP() const;

  void SetSP(const lldb::TargetSP &target_sp);

private:
  lldb::TargetSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBTarget_h_
