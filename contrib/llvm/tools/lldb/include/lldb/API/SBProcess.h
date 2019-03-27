//===-- SBProcess.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBProcess_h_
#define LLDB_SBProcess_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBProcessInfo.h"
#include "lldb/API/SBQueue.h"
#include "lldb/API/SBTarget.h"
#include <stdio.h>

namespace lldb {

class SBEvent;

class LLDB_API SBProcess {
public:
  //------------------------------------------------------------------
  /// Broadcaster event bits definitions.
  //------------------------------------------------------------------
  FLAGS_ANONYMOUS_ENUM(){eBroadcastBitStateChanged = (1 << 0),
                         eBroadcastBitInterrupt = (1 << 1),
                         eBroadcastBitSTDOUT = (1 << 2),
                         eBroadcastBitSTDERR = (1 << 3),
                         eBroadcastBitProfileData = (1 << 4),
                         eBroadcastBitStructuredData = (1 << 5)};

  SBProcess();

  SBProcess(const lldb::SBProcess &rhs);

  const lldb::SBProcess &operator=(const lldb::SBProcess &rhs);

  SBProcess(const lldb::ProcessSP &process_sp);

  ~SBProcess();

  static const char *GetBroadcasterClassName();

  const char *GetPluginName();

  // DEPRECATED: use GetPluginName()
  const char *GetShortPluginName();

  void Clear();

  bool IsValid() const;

  lldb::SBTarget GetTarget() const;

  lldb::ByteOrder GetByteOrder() const;

  size_t PutSTDIN(const char *src, size_t src_len);

  size_t GetSTDOUT(char *dst, size_t dst_len) const;

  size_t GetSTDERR(char *dst, size_t dst_len) const;

  size_t GetAsyncProfileData(char *dst, size_t dst_len) const;

  void ReportEventState(const lldb::SBEvent &event, FILE *out) const;

  void AppendEventStateReport(const lldb::SBEvent &event,
                              lldb::SBCommandReturnObject &result);

  //------------------------------------------------------------------
  /// Remote connection related functions. These will fail if the
  /// process is not in eStateConnected. They are intended for use
  /// when connecting to an externally managed debugserver instance.
  //------------------------------------------------------------------
  bool RemoteAttachToProcessWithID(lldb::pid_t pid, lldb::SBError &error);

  bool RemoteLaunch(char const **argv, char const **envp,
                    const char *stdin_path, const char *stdout_path,
                    const char *stderr_path, const char *working_directory,
                    uint32_t launch_flags, bool stop_at_entry,
                    lldb::SBError &error);

  //------------------------------------------------------------------
  // Thread related functions
  //------------------------------------------------------------------
  uint32_t GetNumThreads();

  lldb::SBThread GetThreadAtIndex(size_t index);

  lldb::SBThread GetThreadByID(lldb::tid_t sb_thread_id);

  lldb::SBThread GetThreadByIndexID(uint32_t index_id);

  lldb::SBThread GetSelectedThread() const;

  //------------------------------------------------------------------
  // Function for lazily creating a thread using the current OS plug-in. This
  // function will be removed in the future when there are APIs to create
  // SBThread objects through the interface and add them to the process through
  // the SBProcess API.
  //------------------------------------------------------------------
  lldb::SBThread CreateOSPluginThread(lldb::tid_t tid, lldb::addr_t context);

  bool SetSelectedThread(const lldb::SBThread &thread);

  bool SetSelectedThreadByID(lldb::tid_t tid);

  bool SetSelectedThreadByIndexID(uint32_t index_id);

  //------------------------------------------------------------------
  // Queue related functions
  //------------------------------------------------------------------
  uint32_t GetNumQueues();

  lldb::SBQueue GetQueueAtIndex(size_t index);

  //------------------------------------------------------------------
  // Stepping related functions
  //------------------------------------------------------------------

  lldb::StateType GetState();

  int GetExitStatus();

  const char *GetExitDescription();

  //------------------------------------------------------------------
  /// Gets the process ID
  ///
  /// Returns the process identifier for the process as it is known
  /// on the system on which the process is running. For unix systems
  /// this is typically the same as if you called "getpid()" in the
  /// process.
  ///
  /// @return
  ///     Returns LLDB_INVALID_PROCESS_ID if this object does not
  ///     contain a valid process object, or if the process has not
  ///     been launched. Returns a valid process ID if the process is
  ///     valid.
  //------------------------------------------------------------------
  lldb::pid_t GetProcessID();

  //------------------------------------------------------------------
  /// Gets the unique ID associated with this process object
  ///
  /// Unique IDs start at 1 and increment up with each new process
  /// instance. Since starting a process on a system might always
  /// create a process with the same process ID, there needs to be a
  /// way to tell two process instances apart.
  ///
  /// @return
  ///     Returns a non-zero integer ID if this object contains a
  ///     valid process object, zero if this object does not contain
  ///     a valid process object.
  //------------------------------------------------------------------
  uint32_t GetUniqueID();

  uint32_t GetAddressByteSize() const;

  lldb::SBError Destroy();

  lldb::SBError Continue();

  lldb::SBError Stop();

  lldb::SBError Kill();

  lldb::SBError Detach();

  lldb::SBError Detach(bool keep_stopped);

  lldb::SBError Signal(int signal);

  lldb::SBUnixSignals GetUnixSignals();

  void SendAsyncInterrupt();

  uint32_t GetStopID(bool include_expression_stops = false);

  //------------------------------------------------------------------
  /// Gets the stop event corresponding to stop ID.
  //
  /// Note that it wasn't fully implemented and tracks only the stop
  /// event for the last natural stop ID.
  ///
  /// @param [in] stop_id
  ///   The ID of the stop event to return.
  ///
  /// @return
  ///   The stop event corresponding to stop ID.
  //------------------------------------------------------------------
  lldb::SBEvent GetStopEventForStopID(uint32_t stop_id);

  size_t ReadMemory(addr_t addr, void *buf, size_t size, lldb::SBError &error);

  size_t WriteMemory(addr_t addr, const void *buf, size_t size,
                     lldb::SBError &error);

  size_t ReadCStringFromMemory(addr_t addr, void *buf, size_t size,
                               lldb::SBError &error);

  uint64_t ReadUnsignedFromMemory(addr_t addr, uint32_t byte_size,
                                  lldb::SBError &error);

  lldb::addr_t ReadPointerFromMemory(addr_t addr, lldb::SBError &error);

  // Events
  static lldb::StateType GetStateFromEvent(const lldb::SBEvent &event);

  static bool GetRestartedFromEvent(const lldb::SBEvent &event);

  static size_t GetNumRestartedReasonsFromEvent(const lldb::SBEvent &event);

  static const char *
  GetRestartedReasonAtIndexFromEvent(const lldb::SBEvent &event, size_t idx);

  static lldb::SBProcess GetProcessFromEvent(const lldb::SBEvent &event);

  static bool GetInterruptedFromEvent(const lldb::SBEvent &event);

  static lldb::SBStructuredData
  GetStructuredDataFromEvent(const lldb::SBEvent &event);

  static bool EventIsProcessEvent(const lldb::SBEvent &event);

  static bool EventIsStructuredDataEvent(const lldb::SBEvent &event);

  lldb::SBBroadcaster GetBroadcaster() const;

  static const char *GetBroadcasterClass();

  bool GetDescription(lldb::SBStream &description);

  //------------------------------------------------------------------
  /// Start Tracing with the given SBTraceOptions.
  ///
  /// @param[in] options
  ///     Class containing trace options like trace buffer size, meta
  ///     data buffer size, TraceType and any custom parameters
  ///     {formatted as a JSON Dictionary}. In case of errors in
  ///     formatting, an error would be reported.
  ///     It must be noted that tracing options such as buffer sizes
  ///     or other custom parameters passed maybe invalid for some
  ///     trace technologies. In such cases the trace implementations
  ///     could choose to either throw an error or could round off to
  ///     the nearest valid options to start tracing if the passed
  ///     value is not supported. To obtain the actual used trace
  ///     options please use the GetTraceConfig API. For the custom
  ///     parameters, only the parameters recognized by the target
  ///     would be used and others would be ignored.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong.
  ///
  /// @return
  ///     A SBTrace instance, which should be used
  ///     to get the trace data or other trace related operations.
  //------------------------------------------------------------------
  lldb::SBTrace StartTrace(SBTraceOptions &options, lldb::SBError &error);

  uint32_t GetNumSupportedHardwareWatchpoints(lldb::SBError &error) const;

  //------------------------------------------------------------------
  /// Load a shared library into this process.
  ///
  /// @param[in] remote_image_spec
  ///     The path for the shared library on the target what you want
  ///     to load.
  ///
  /// @param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// @return
  ///     A token that represents the shared library that can be
  ///     later used to unload the shared library. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  //------------------------------------------------------------------
  uint32_t LoadImage(lldb::SBFileSpec &remote_image_spec, lldb::SBError &error);

  //------------------------------------------------------------------
  /// Load a shared library into this process.
  ///
  /// @param[in] local_image_spec
  ///     The file spec that points to the shared library that you
  ///     want to load if the library is located on the host. The
  ///     library will be copied over to the location specified by
  ///     remote_image_spec or into the current working directory with
  ///     the same filename if the remote_image_spec isn't specified.
  ///
  /// @param[in] remote_image_spec
  ///     If local_image_spec is specified then the location where the
  ///     library should be copied over from the host. If
  ///     local_image_spec isn't specified, then the path for the
  ///     shared library on the target what you want to load.
  ///
  /// @param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// @return
  ///     A token that represents the shared library that can be
  ///     later used to unload the shared library. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  //------------------------------------------------------------------
  uint32_t LoadImage(const lldb::SBFileSpec &local_image_spec,
                     const lldb::SBFileSpec &remote_image_spec,
                     lldb::SBError &error);

  //------------------------------------------------------------------
  /// Load a shared library into this process, starting with a
  /// library name and a list of paths, searching along the list of
  /// paths till you find a matching library.
  ///
  /// @param[in] image_spec
  ///     The name of the shared library that you want to load.  
  ///     If image_spec is a relative path, the relative path will be
  ///     appended to the search paths.
  ///     If the image_spec is an absolute path, just the basename is used.
  ///
  /// @param[in] paths
  ///     A list of paths to search for the library whose basename is 
  ///     local_spec.
  ///
  /// @param[out] loaded_path
  ///     If the library was found along the paths, this will store the
  ///     full path to the found library.
  ///
  /// @param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to search for the shared library.
  ///
  /// @return
  ///     A token that represents the shared library that can be
  ///     later passed to UnloadImage. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  //------------------------------------------------------------------
  uint32_t LoadImageUsingPaths(const lldb::SBFileSpec &image_spec,
                               SBStringList &paths,
                               lldb::SBFileSpec &loaded_path, 
                               lldb::SBError &error);

  lldb::SBError UnloadImage(uint32_t image_token);

  lldb::SBError SendEventData(const char *data);

  //------------------------------------------------------------------
  /// Return the number of different thread-origin extended backtraces
  /// this process can support.
  ///
  /// When the process is stopped and you have an SBThread, lldb may be
  /// able to show a backtrace of when that thread was originally created,
  /// or the work item was enqueued to it (in the case of a libdispatch
  /// queue).
  ///
  /// @return
  ///   The number of thread-origin extended backtrace types that may be
  ///   available.
  //------------------------------------------------------------------
  uint32_t GetNumExtendedBacktraceTypes();

  //------------------------------------------------------------------
  /// Return the name of one of the thread-origin extended backtrace
  /// methods.
  ///
  /// @param [in] idx
  ///   The index of the name to return.  They will be returned in
  ///   the order that the user will most likely want to see them.
  ///   e.g. if the type at index 0 is not available for a thread,
  ///   see if the type at index 1 provides an extended backtrace.
  ///
  /// @return
  ///   The name at that index.
  //------------------------------------------------------------------
  const char *GetExtendedBacktraceTypeAtIndex(uint32_t idx);

  lldb::SBThreadCollection GetHistoryThreads(addr_t addr);

  bool IsInstrumentationRuntimePresent(InstrumentationRuntimeType type);

  /// Save the state of the process in a core file (or mini dump on Windows).
  lldb::SBError SaveCore(const char *file_name);

  //------------------------------------------------------------------
  /// Query the address load_addr and store the details of the memory
  /// region that contains it in the supplied SBMemoryRegionInfo object.
  /// To iterate over all memory regions use GetMemoryRegionList.
  ///
  /// @param[in] load_addr
  ///     The address to be queried.
  ///
  /// @param[out] region_info
  ///     A reference to an SBMemoryRegionInfo object that will contain
  ///     the details of the memory region containing load_addr.
  ///
  /// @return
  ///     An error object describes any errors that occurred while
  ///     querying load_addr.
  //------------------------------------------------------------------
  lldb::SBError GetMemoryRegionInfo(lldb::addr_t load_addr,
                                    lldb::SBMemoryRegionInfo &region_info);

  //------------------------------------------------------------------
  /// Return the list of memory regions within the process.
  ///
  /// @return
  ///     A list of all witin the process memory regions.
  //------------------------------------------------------------------
  lldb::SBMemoryRegionInfoList GetMemoryRegions();

  //------------------------------------------------------------------
  /// Return information about the process.
  ///
  /// Valid process info will only be returned when the process is
  /// alive, use SBProcessInfo::IsValid() to check returned info is
  /// valid.
  //------------------------------------------------------------------
  lldb::SBProcessInfo GetProcessInfo();

protected:
  friend class SBAddress;
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBCommandInterpreter;
  friend class SBDebugger;
  friend class SBExecutionContext;
  friend class SBFunction;
  friend class SBModule;
  friend class SBTarget;
  friend class SBThread;
  friend class SBValue;
  friend class lldb_private::QueueImpl;

  lldb::ProcessSP GetSP() const;

  void SetSP(const lldb::ProcessSP &process_sp);

  lldb::ProcessWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_SBProcess_h_
