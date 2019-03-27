//===-- SystemRuntime.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SystemRuntime_h_
#define liblldb_SystemRuntime_h_

#include <vector>

#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Target/QueueItem.h"
#include "lldb/Target/QueueList.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class SystemRuntime SystemRuntime.h "lldb/Target/SystemRuntime.h"
/// A plug-in interface definition class for system runtimes.
///
/// The system runtime plugins can collect information from the system
/// libraries during a Process' lifetime and provide information about how
/// objects/threads were originated.
///
/// For instance, a system runtime plugin use a breakpoint when threads are
/// created to record the backtrace of where that thread was created. Later,
/// when backtracing the created thread, it could extend the backtrace to show
/// where it was originally created from.
///
/// The plugin will insert its own breakpoint when Created and start
/// collecting information.  Later when it comes time to augment a Thread, it
/// can be asked to provide that information.
///
//----------------------------------------------------------------------

class SystemRuntime : public PluginInterface {
public:
  //------------------------------------------------------------------
  /// Find a system runtime plugin for a given process.
  ///
  /// Scans the installed SystemRuntime plugins and tries to find an instance
  /// that can be used to track image changes in \a process.
  ///
  /// @param[in] process
  ///     The process for which to try and locate a system runtime
  ///     plugin instance.
  //------------------------------------------------------------------
  static SystemRuntime *FindPlugin(Process *process);

  //------------------------------------------------------------------
  /// Construct with a process.
  // -----------------------------------------------------------------
  SystemRuntime(lldb_private::Process *process);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class is designed to be inherited
  /// by the plug-in instance.
  //------------------------------------------------------------------
  ~SystemRuntime() override;

  //------------------------------------------------------------------
  /// Called after attaching to a process.
  ///
  /// Allow the SystemRuntime plugin to execute some code after attaching to a
  /// process.
  //------------------------------------------------------------------
  virtual void DidAttach();

  //------------------------------------------------------------------
  /// Called after launching a process.
  ///
  /// Allow the SystemRuntime plugin to execute some code after launching a
  /// process.
  //------------------------------------------------------------------
  virtual void DidLaunch();

  //------------------------------------------------------------------
  /// Called when modules have been loaded in the process.
  ///
  /// Allow the SystemRuntime plugin to enable logging features in the system
  /// runtime libraries.
  //------------------------------------------------------------------
  virtual void ModulesDidLoad(lldb_private::ModuleList &module_list);

  //------------------------------------------------------------------
  /// Called before detaching from a process.
  ///
  /// This will give a SystemRuntime plugin a chance to free any resources in
  /// the inferior process before we detach.
  //------------------------------------------------------------------
  virtual void Detach();

  //------------------------------------------------------------------
  /// Return a list of thread origin extended backtraces that may be
  /// available.
  ///
  /// A System Runtime may be able to provide a backtrace of when this
  /// thread was originally created.  Furthermore, it may be able to provide
  /// that extended backtrace for different styles of creation. On a system
  /// with both pthreads and libdispatch, aka Grand Central Dispatch, queues,
  /// the system runtime may be able to provide the pthread creation of the
  /// thread and it may also be able to provide the backtrace of when this GCD
  /// queue work block was enqueued. The caller may request these different
  /// origins by name.
  ///
  /// The names will be provided in the order that they are most likely to be
  /// requested.  For instance, a most natural order may be to request the GCD
  /// libdispatch queue origin.  If there is none, then request the pthread
  /// origin.
  ///
  /// @return
  ///   A vector of ConstStrings with names like "pthread" or "libdispatch".
  ///   An empty vector may be returned if no thread origin extended
  ///   backtrace capabilities are available.
  //------------------------------------------------------------------
  virtual const std::vector<ConstString> &GetExtendedBacktraceTypes();

  //------------------------------------------------------------------
  /// Return a Thread which shows the origin of this thread's creation.
  ///
  /// This likely returns a HistoryThread which shows how thread was
  /// originally created (e.g. "pthread" type), or how the work that is
  /// currently executing on it was originally enqueued (e.g. "libdispatch"
  /// type).
  ///
  /// There may be a chain of thread-origins; it may be informative to the end
  /// user to query the returned ThreadSP for its origins as well.
  ///
  /// @param [in] thread
  ///   The thread to examine.
  ///
  /// @param [in] type
  ///   The type of thread origin being requested.  The types supported
  ///   are returned from SystemRuntime::GetExtendedBacktraceTypes.
  ///
  /// @return
  ///   A ThreadSP which will have a StackList of frames.  This Thread will
  ///   not appear in the Process' list of current threads.  Normal thread
  ///   operations like stepping will not be available.  This is a historical
  ///   view thread and may be only useful for showing a backtrace.
  ///
  ///   An empty ThreadSP will be returned if no thread origin is available.
  //------------------------------------------------------------------
  virtual lldb::ThreadSP GetExtendedBacktraceThread(lldb::ThreadSP thread,
                                                    ConstString type);

  //------------------------------------------------------------------
  /// Get the extended backtrace thread for a QueueItem
  ///
  /// A QueueItem represents a function/block that will be executed on
  /// a libdispatch queue in the future, or it represents a function/block
  /// that is currently executing on a thread.
  ///
  /// This method will report a thread backtrace of the function that enqueued
  /// it originally, if possible.
  ///
  /// @param [in] queue_item_sp
  ///     The QueueItem that we are getting an extended backtrace for.
  ///
  /// @param [in] type
  ///     The type of extended backtrace to fetch.  The types supported
  ///     are returned from SystemRuntime::GetExtendedBacktraceTypes.
  ///
  /// @return
  ///     If an extended backtrace is available, it is returned.  Else
  ///     an empty ThreadSP is returned.
  //------------------------------------------------------------------
  virtual lldb::ThreadSP
  GetExtendedBacktraceForQueueItem(lldb::QueueItemSP queue_item_sp,
                                   ConstString type) {
    return lldb::ThreadSP();
  }

  //------------------------------------------------------------------
  /// Populate the Process' QueueList with libdispatch / GCD queues that
  /// exist.
  ///
  /// When process execution is paused, the SystemRuntime may be called to
  /// fill in the list of Queues that currently exist.
  ///
  /// @param [out] queue_list
  ///     This QueueList will be cleared, and any queues that currently exist
  ///     will be added.  An empty QueueList will be returned if no queues
  ///     exist or if this Systemruntime does not support libdispatch queues.
  //------------------------------------------------------------------
  virtual void PopulateQueueList(lldb_private::QueueList &queue_list) {}

  //------------------------------------------------------------------
  /// Get the queue name for a thread given a thread's dispatch_qaddr.
  ///
  /// On systems using libdispatch queues, a thread may be associated with a
  /// queue. There will be a call to get the thread's dispatch_qaddr.  At the
  /// dispatch_qaddr we will find the address of this thread's
  /// dispatch_queue_t structure. Given the address of the dispatch_queue_t
  /// structure for a thread, get the queue name and return it.
  ///
  /// @param [in] dispatch_qaddr
  ///     The address of the dispatch_qaddr pointer for this thread.
  ///
  /// @return
  ///     The string of this queue's name.  An empty string is returned if the
  ///     name could not be found.
  //------------------------------------------------------------------
  virtual std::string
  GetQueueNameFromThreadQAddress(lldb::addr_t dispatch_qaddr) {
    return "";
  }

  //------------------------------------------------------------------
  /// Get the QueueID for the libdispatch queue given the thread's
  /// dispatch_qaddr.
  ///
  /// On systems using libdispatch queues, a thread may be associated with a
  /// queue. There will be a call to get the thread's dispatch_qaddr.  At the
  /// dispatch_qaddr we will find the address of this thread's
  /// dispatch_queue_t structure. Given the address of the dispatch_queue_t
  /// structure for a thread, get the queue ID and return it.
  ///
  /// @param [in] dispatch_qaddr
  ///     The address of the dispatch_qaddr pointer for this thread.
  ///
  /// @return
  ///     The queue ID, or if it could not be retrieved, LLDB_INVALID_QUEUE_ID.
  //------------------------------------------------------------------
  virtual lldb::queue_id_t
  GetQueueIDFromThreadQAddress(lldb::addr_t dispatch_qaddr) {
    return LLDB_INVALID_QUEUE_ID;
  }

  //------------------------------------------------------------------
  /// Get the libdispatch_queue_t address for the queue given the thread's
  /// dispatch_qaddr.
  ///
  /// On systems using libdispatch queues, a thread may be associated with a
  /// queue. There will be a call to get the thread's dispatch_qaddr. Given
  /// the thread's dispatch_qaddr, find the libdispatch_queue_t address and
  /// return it.
  ///
  /// @param [in] dispatch_qaddr
  ///     The address of the dispatch_qaddr pointer for this thread.
  ///
  /// @return
  ///     The libdispatch_queue_t address, or LLDB_INVALID_ADDRESS if
  ///     unavailable/not found.
  //------------------------------------------------------------------
  virtual lldb::addr_t
  GetLibdispatchQueueAddressFromThreadQAddress(lldb::addr_t dispatch_qaddr) {
    return LLDB_INVALID_ADDRESS;
  }

  //------------------------------------------------------------------
  /// Retrieve the Queue kind for the queue at a thread's dispatch_qaddr.
  ///
  /// Retrieve the Queue kind - either eQueueKindSerial or
  /// eQueueKindConcurrent, indicating that this queue processes work items
  /// serially or concurrently.
  ///
  /// @return
  ///     The Queue kind, if it could be read, else eQueueKindUnknown.
  //------------------------------------------------------------------
  virtual lldb::QueueKind GetQueueKind(lldb::addr_t dispatch_qaddr) {
    return lldb::eQueueKindUnknown;
  }

  //------------------------------------------------------------------
  /// Get the pending work items for a libdispatch Queue
  ///
  /// If this system/process is using libdispatch and the runtime can do so,
  /// retrieve the list of pending work items for the specified Queue and add
  /// it to the Queue.
  ///
  /// @param [in] queue
  ///     The queue of interest.
  //------------------------------------------------------------------
  virtual void PopulatePendingItemsForQueue(lldb_private::Queue *queue) {}

  //------------------------------------------------------------------
  /// Complete the fields in a QueueItem
  ///
  /// PopulatePendingItemsForQueue() may not fill in all of the QueueItem
  /// details; when the remaining fields are needed, they will be fetched by
  /// call this method.
  ///
  /// @param [in] queue_item
  ///   The QueueItem that we will be completing.
  ///
  /// @param [in] item_ref
  ///     The item_ref token that is needed to retrieve the rest of the
  ///     information about the QueueItem.
  //------------------------------------------------------------------
  virtual void CompleteQueueItem(lldb_private::QueueItem *queue_item,
                                 lldb::addr_t item_ref) {}

  //------------------------------------------------------------------
  /// Add key-value pairs to the StructuredData dictionary object with
  /// information debugserver  may need when constructing the
  /// jThreadExtendedInfo packet.
  ///
  /// @param [out] dict
  ///     Dictionary to which key-value pairs should be added; they will
  ///     be sent to the remote gdb server stub as arguments in the
  ///     jThreadExtendedInfo request.
  //------------------------------------------------------------------
  virtual void AddThreadExtendedInfoPacketHints(
      lldb_private::StructuredData::ObjectSP dict) {}

  /// Determine whether it is safe to run an expression on a given thread
  ///
  /// If a system must not run functions on a thread in some particular state,
  /// this method gives a way for it to flag that the expression should not be
  /// run.
  ///
  /// @param [in] thread_sp
  ///     The thread we want to run the expression on.
  ///
  /// @return
  ///     True will be returned if there are no known problems with running an
  ///     expression on this thread.  False means that the inferior function
  ///     call should not be made on this thread.
  //------------------------------------------------------------------
  virtual bool SafeToCallFunctionsOnThisThread(lldb::ThreadSP thread_sp) {
    return true;
  }

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  Process *m_process;

  std::vector<ConstString> m_types;

private:
  DISALLOW_COPY_AND_ASSIGN(SystemRuntime);
};

} // namespace lldb_private

#endif // liblldb_SystemRuntime_h_
