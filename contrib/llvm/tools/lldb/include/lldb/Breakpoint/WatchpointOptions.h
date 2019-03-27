//===-- WatchpointOptions.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_WatchpointOptions_h_
#define liblldb_WatchpointOptions_h_

#include <memory>
#include <string>

#include "lldb/Utility/Baton.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class WatchpointOptions WatchpointOptions.h
/// "lldb/Breakpoint/WatchpointOptions.h" Class that manages the options on a
/// watchpoint.
//----------------------------------------------------------------------

class WatchpointOptions {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  //------------------------------------------------------------------
  /// Default constructor.  The watchpoint is enabled, and has no condition,
  /// callback, ignore count, etc...
  //------------------------------------------------------------------
  WatchpointOptions();
  WatchpointOptions(const WatchpointOptions &rhs);

  static WatchpointOptions *CopyOptionsNoCallback(WatchpointOptions &rhs);
  //------------------------------------------------------------------
  /// This constructor allows you to specify all the watchpoint options.
  ///
  /// @param[in] callback
  ///    This is the plugin for some code that gets run, returns \b true if we
  ///    are to stop.
  ///
  /// @param[in] baton
  ///    Client data that will get passed to the callback.
  ///
  /// @param[in] thread_id
  ///    Only stop if \a thread_id hits the watchpoint.
  //------------------------------------------------------------------
  WatchpointOptions(WatchpointHitCallback callback, void *baton,
                    lldb::tid_t thread_id = LLDB_INVALID_THREAD_ID);

  virtual ~WatchpointOptions();

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------
  const WatchpointOptions &operator=(const WatchpointOptions &rhs);

  //------------------------------------------------------------------
  // Callbacks
  //
  // Watchpoint callbacks come in two forms, synchronous and asynchronous.
  // Synchronous callbacks will get run before any of the thread plans are
  // consulted, and if they return false the target will continue "under the
  // radar" of the thread plans.  There are a couple of restrictions to
  // synchronous callbacks: 1) They should NOT resume the target themselves.
  // Just return false if you want the target to restart. 2) Watchpoints with
  // synchronous callbacks can't have conditions (or rather, they can have
  // them, but they
  //    won't do anything.  Ditto with ignore counts, etc...  You are supposed
  //    to control that all through the
  //    callback.
  // Asynchronous callbacks get run as part of the "ShouldStop" logic in the
  // thread plan.  The logic there is:
  //   a) If the watchpoint is thread specific and not for this thread, continue
  //   w/o running the callback.
  //   b) If the ignore count says we shouldn't stop, then ditto.
  //   c) If the condition says we shouldn't stop, then ditto.
  //   d) Otherwise, the callback will get run, and if it returns true we will
  //   stop, and if false we won't.
  //  The asynchronous callback can run the target itself, but at present that
  //  should be the last action the
  //  callback does.  We will relax this condition at some point, but it will
  //  take a bit of plumbing to get
  //  that to work.
  //
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Adds a callback to the watchpoint option set.
  ///
  /// @param[in] callback
  ///    The function to be called when the watchpoint gets hit.
  ///
  /// @param[in] baton_sp
  ///    A baton which will get passed back to the callback when it is invoked.
  ///
  /// @param[in] synchronous
  ///    Whether this is a synchronous or asynchronous callback.  See discussion
  ///    above.
  //------------------------------------------------------------------
  void SetCallback(WatchpointHitCallback callback,
                   const lldb::BatonSP &baton_sp, bool synchronous = false);

  //------------------------------------------------------------------
  /// Remove the callback from this option set.
  //------------------------------------------------------------------
  void ClearCallback();

  // The rest of these functions are meant to be used only within the
  // watchpoint handling mechanism.

  //------------------------------------------------------------------
  /// Use this function to invoke the callback for a specific stop.
  ///
  /// @param[in] context
  ///    The context in which the callback is to be invoked.  This includes the
  ///    stop event, the
  ///    execution context of the stop (since you might hit the same watchpoint
  ///    on multiple threads) and
  ///    whether we are currently executing synchronous or asynchronous
  ///    callbacks.
  ///
  /// @param[in] watch_id
  ///    The watchpoint ID that owns this option set.
  ///
  /// @return
  ///     The callback return value.
  //------------------------------------------------------------------
  bool InvokeCallback(StoppointCallbackContext *context,
                      lldb::user_id_t watch_id);

  //------------------------------------------------------------------
  /// Used in InvokeCallback to tell whether it is the right time to run this
  /// kind of callback.
  ///
  /// @return
  ///     The synchronicity of our callback.
  //------------------------------------------------------------------
  bool IsCallbackSynchronous() { return m_callback_is_synchronous; }

  //------------------------------------------------------------------
  /// Fetch the baton from the callback.
  ///
  /// @return
  ///     The baton.
  //------------------------------------------------------------------
  Baton *GetBaton();

  //------------------------------------------------------------------
  /// Fetch  a const version of the baton from the callback.
  ///
  /// @return
  ///     The baton.
  //------------------------------------------------------------------
  const Baton *GetBaton() const;

  //------------------------------------------------------------------
  /// Return the current thread spec for this option. This will return nullptr
  /// if the no thread specifications have been set for this Option yet.
  /// @return
  ///     The thread specification pointer for this option, or nullptr if none
  ///     has
  ///     been set yet.
  //------------------------------------------------------------------
  const ThreadSpec *GetThreadSpecNoCreate() const;

  //------------------------------------------------------------------
  /// Returns a pointer to the ThreadSpec for this option, creating it. if it
  /// hasn't been created already.   This API is used for setting the
  /// ThreadSpec items for this option.
  //------------------------------------------------------------------
  ThreadSpec *GetThreadSpec();

  void SetThreadID(lldb::tid_t thread_id);

  void GetDescription(Stream *s, lldb::DescriptionLevel level) const;

  //------------------------------------------------------------------
  /// Get description for callback only.
  //------------------------------------------------------------------
  void GetCallbackDescription(Stream *s, lldb::DescriptionLevel level) const;

  //------------------------------------------------------------------
  /// Returns true if the watchpoint option has a callback set.
  //------------------------------------------------------------------
  bool HasCallback();

  //------------------------------------------------------------------
  /// This is the default empty callback.
  /// @return
  ///     The thread id for which the watchpoint hit will stop,
  ///     LLDB_INVALID_THREAD_ID for all threads.
  //------------------------------------------------------------------
  static bool NullCallback(void *baton, StoppointCallbackContext *context,
                           lldb::user_id_t watch_id);

  struct CommandData {
    CommandData() : user_source(), script_source(), stop_on_error(true) {}

    ~CommandData() = default;

    StringList user_source;
    std::string script_source;
    bool stop_on_error;
  };

  class CommandBaton : public TypedBaton<CommandData> {
  public:
    CommandBaton(std::unique_ptr<CommandData> Data)
        : TypedBaton(std::move(Data)) {}

    void GetDescription(Stream *s, lldb::DescriptionLevel level) const override;
  };

protected:
  //------------------------------------------------------------------
  // Classes that inherit from WatchpointOptions can see and modify these
  //------------------------------------------------------------------

private:
  //------------------------------------------------------------------
  // For WatchpointOptions only
  //------------------------------------------------------------------
  WatchpointHitCallback m_callback;  // This is the callback function pointer
  lldb::BatonSP m_callback_baton_sp; // This is the client data for the callback
  bool m_callback_is_synchronous;
  std::unique_ptr<ThreadSpec>
      m_thread_spec_ap; // Thread for which this watchpoint will take
};

} // namespace lldb_private

#endif // liblldb_WatchpointOptions_h_
