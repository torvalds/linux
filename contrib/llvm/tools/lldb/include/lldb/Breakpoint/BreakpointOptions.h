//===-- BreakpointOptions.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointOptions_h_
#define liblldb_BreakpointOptions_h_

#include <memory>
#include <string>

#include "lldb/Utility/Baton.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointOptions BreakpointOptions.h
/// "lldb/Breakpoint/BreakpointOptions.h" Class that manages the options on a
/// breakpoint or breakpoint location.
//----------------------------------------------------------------------

class BreakpointOptions {
friend class BreakpointLocation;
friend class BreakpointName;
friend class lldb_private::BreakpointOptionGroup;
friend class Breakpoint;

public:
  enum OptionKind {
    eCallback     = 1 << 0,
    eEnabled      = 1 << 1,
    eOneShot      = 1 << 2,
    eIgnoreCount  = 1 << 3,
    eThreadSpec   = 1 << 4,
    eCondition    = 1 << 5,
    eAutoContinue = 1 << 6,
    eAllOptions   = (eCallback | eEnabled | eOneShot | eIgnoreCount | eThreadSpec
                     | eCondition | eAutoContinue)
  };
  struct CommandData {
    CommandData()
        : user_source(), script_source(),
          interpreter(lldb::eScriptLanguageNone), stop_on_error(true) {}

    CommandData(const StringList &user_source, lldb::ScriptLanguage interp)
        : user_source(user_source), script_source(), interpreter(interp),
          stop_on_error(true) {}

    ~CommandData() = default;

    static const char *GetSerializationKey() { return "BKPTCMDData"; }

    StructuredData::ObjectSP SerializeToStructuredData();

    static std::unique_ptr<CommandData>
    CreateFromStructuredData(const StructuredData::Dictionary &options_dict,
                             Status &error);

    StringList user_source;
    std::string script_source;
    enum lldb::ScriptLanguage
        interpreter; // eScriptLanguageNone means command interpreter.
    bool stop_on_error;

  private:
    enum class OptionNames : uint32_t {
      UserSource = 0,
      Interpreter,
      StopOnError,
      LastOptionName
    };

    static const char
        *g_option_names[static_cast<uint32_t>(OptionNames::LastOptionName)];

    static const char *GetKey(OptionNames enum_value) {
      return g_option_names[static_cast<uint32_t>(enum_value)];
    }
  };

  class CommandBaton : public TypedBaton<CommandData> {
  public:
    explicit CommandBaton(std::unique_ptr<CommandData> Data)
        : TypedBaton(std::move(Data)) {}

    void GetDescription(Stream *s, lldb::DescriptionLevel level) const override;
  };

  typedef std::shared_ptr<CommandBaton> CommandBatonSP;

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// This constructor allows you to specify all the breakpoint options except
  /// the callback.  That one is more complicated, and better to do by hand.
  ///
  /// @param[in] condition
  ///    The expression which if it evaluates to \b true if we are to stop
  ///
  /// @param[in] enabled
  ///    Is this breakpoint enabled.
  ///
  /// @param[in] ignore
  ///    How many breakpoint hits we should ignore before stopping.
  ///
  //------------------------------------------------------------------
  BreakpointOptions(const char *condition, bool enabled = true,
                    int32_t ignore = 0, bool one_shot = false,
                    bool auto_continue = false);

  //------------------------------------------------------------------
  /// Breakpoints make options with all flags set.  Locations and Names make
  /// options with no flags set.
  //------------------------------------------------------------------
  BreakpointOptions(bool all_flags_set);
  BreakpointOptions(const BreakpointOptions &rhs);

  virtual ~BreakpointOptions();

  static std::unique_ptr<BreakpointOptions>
  CreateFromStructuredData(Target &target,
                           const StructuredData::Dictionary &data_dict,
                           Status &error);

  virtual StructuredData::ObjectSP SerializeToStructuredData();

  static const char *GetSerializationKey() { return "BKPTOptions"; }

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------
  const BreakpointOptions &operator=(const BreakpointOptions &rhs);
  
  //------------------------------------------------------------------
  /// Copy over only the options set in the incoming BreakpointOptions.
  //------------------------------------------------------------------
  void CopyOverSetOptions(const BreakpointOptions &rhs);

  //------------------------------------------------------------------
  // Callbacks
  //
  // Breakpoint callbacks come in two forms, synchronous and asynchronous.
  // Synchronous callbacks will get run before any of the thread plans are
  // consulted, and if they return false the target will continue "under the
  // radar" of the thread plans.  There are a couple of restrictions to
  // synchronous callbacks:
  // 1) They should NOT resume the target themselves.
  //     Just return false if you want the target to restart.
  // 2) Breakpoints with synchronous callbacks can't have conditions
  //    (or rather, they can have them, but they won't do anything.
  //    Ditto with ignore counts, etc...  You are supposed to control that all
  //    through the callback.
  // Asynchronous callbacks get run as part of the "ShouldStop" logic in the
  // thread plan.  The logic there is:
  //   a) If the breakpoint is thread specific and not for this thread, continue
  //   w/o running the callback.
  //      NB. This is actually enforced underneath the breakpoint system, the
  //      Process plugin is expected to
  //      call BreakpointSite::IsValidForThread, and set the thread's StopInfo
  //      to "no reason".  That way,
  //      thread displays won't show stops for breakpoints not for that
  //      thread...
  //   b) If the ignore count says we shouldn't stop, then ditto.
  //   c) If the condition says we shouldn't stop, then ditto.
  //   d) Otherwise, the callback will get run, and if it returns true we will
  //      stop, and if false we won't.
  //  The asynchronous callback can run the target itself, but at present that
  //  should be the last action the callback does.  We will relax this condition
  //  at some point, but it will take a bit of plumbing to get that to work.
  //
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Adds a callback to the breakpoint option set.
  ///
  /// @param[in] callback
  ///    The function to be called when the breakpoint gets hit.
  ///
  /// @param[in] baton_sp
  ///    A baton which will get passed back to the callback when it is invoked.
  ///
  /// @param[in] synchronous
  ///    Whether this is a synchronous or asynchronous callback.  See discussion
  ///    above.
  //------------------------------------------------------------------
  void SetCallback(BreakpointHitCallback callback,
                   const lldb::BatonSP &baton_sp, bool synchronous = false);

  void SetCallback(BreakpointHitCallback callback,
                   const BreakpointOptions::CommandBatonSP &command_baton_sp,
                   bool synchronous = false);

  //------------------------------------------------------------------
  /// Returns the command line commands for the callback on this breakpoint.
  ///
  /// @param[out] command_list
  ///    The commands will be appended to this list.
  ///
  /// @return
  ///    \btrue if the command callback is a command-line callback,
  ///    \bfalse otherwise.
  //------------------------------------------------------------------
  bool GetCommandLineCallbacks(StringList &command_list);

  //------------------------------------------------------------------
  /// Remove the callback from this option set.
  //------------------------------------------------------------------
  void ClearCallback();

  // The rest of these functions are meant to be used only within the
  // breakpoint handling mechanism.

  //------------------------------------------------------------------
  /// Use this function to invoke the callback for a specific stop.
  ///
  /// @param[in] context
  ///    The context in which the callback is to be invoked.  This includes the
  ///    stop event, the
  ///    execution context of the stop (since you might hit the same breakpoint
  ///    on multiple threads) and
  ///    whether we are currently executing synchronous or asynchronous
  ///    callbacks.
  ///
  /// @param[in] break_id
  ///    The breakpoint ID that owns this option set.
  ///
  /// @param[in] break_loc_id
  ///    The breakpoint location ID that owns this option set.
  ///
  /// @return
  ///     The callback return value.
  //------------------------------------------------------------------
  bool InvokeCallback(StoppointCallbackContext *context,
                      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  //------------------------------------------------------------------
  /// Used in InvokeCallback to tell whether it is the right time to run this
  /// kind of callback.
  ///
  /// @return
  ///     The synchronicity of our callback.
  //------------------------------------------------------------------
  bool IsCallbackSynchronous() const { return m_callback_is_synchronous; }

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
  // Condition
  //------------------------------------------------------------------
  //------------------------------------------------------------------
  /// Set the breakpoint option's condition.
  ///
  /// @param[in] condition
  ///    The condition expression to evaluate when the breakpoint is hit.
  //------------------------------------------------------------------
  void SetCondition(const char *condition);

  //------------------------------------------------------------------
  /// Return a pointer to the text of the condition expression.
  ///
  /// @return
  ///    A pointer to the condition expression text, or nullptr if no
  //     condition has been set.
  //------------------------------------------------------------------
  const char *GetConditionText(size_t *hash = nullptr) const;

  //------------------------------------------------------------------
  // Enabled/Ignore Count
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Check the Enable/Disable state.
  /// @return
  ///     \b true if the breakpoint is enabled, \b false if disabled.
  //------------------------------------------------------------------
  bool IsEnabled() const { return m_enabled; }

  //------------------------------------------------------------------
  /// If \a enable is \b true, enable the breakpoint, if \b false disable it.
  //------------------------------------------------------------------
  void SetEnabled(bool enabled) { 
    m_enabled = enabled;
    m_set_flags.Set(eEnabled);
  }

  //------------------------------------------------------------------
  /// Check the auto-continue state.
  /// @return
  ///     \b true if the breakpoint is set to auto-continue, \b false otherwise.
  //------------------------------------------------------------------
  bool IsAutoContinue() const { return m_auto_continue; }

  //------------------------------------------------------------------
  /// Set the auto-continue state.
  //------------------------------------------------------------------
  void SetAutoContinue(bool auto_continue) { 
    m_auto_continue = auto_continue;
    m_set_flags.Set(eAutoContinue);
  }

  //------------------------------------------------------------------
  /// Check the One-shot state.
  /// @return
  ///     \b true if the breakpoint is one-shot, \b false otherwise.
  //------------------------------------------------------------------
  bool IsOneShot() const { return m_one_shot; }

  //------------------------------------------------------------------
  /// If \a enable is \b true, enable the breakpoint, if \b false disable it.
  //------------------------------------------------------------------
  void SetOneShot(bool one_shot) { 
    m_one_shot = one_shot; 
    m_set_flags.Set(eOneShot); 
  }

  //------------------------------------------------------------------
  /// Set the breakpoint to ignore the next \a count breakpoint hits.
  /// @param[in] count
  ///    The number of breakpoint hits to ignore.
  //------------------------------------------------------------------

  void SetIgnoreCount(uint32_t n) { 
    m_ignore_count = n; 
    m_set_flags.Set(eIgnoreCount);
  }

  //------------------------------------------------------------------
  /// Return the current Ignore Count.
  /// @return
  ///     The number of breakpoint hits to be ignored.
  //------------------------------------------------------------------
  uint32_t GetIgnoreCount() const { return m_ignore_count; }

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
  /// Returns true if the breakpoint option has a callback set.
  //------------------------------------------------------------------
  bool HasCallback() const;

  //------------------------------------------------------------------
  /// This is the default empty callback.
  //------------------------------------------------------------------
  static bool NullCallback(void *baton, StoppointCallbackContext *context,
                           lldb::user_id_t break_id,
                           lldb::user_id_t break_loc_id);

  //------------------------------------------------------------------
  /// Set a callback based on BreakpointOptions::CommandData. @param[in]
  /// cmd_data
  ///     A UP holding the new'ed CommandData object.
  ///     The breakpoint will take ownership of pointer held by this object.
  //------------------------------------------------------------------
  void SetCommandDataCallback(std::unique_ptr<CommandData> &cmd_data);
  
  void Clear();
  
  bool AnySet() const {
    return m_set_flags.AnySet(eAllOptions);
  }
  
protected:
//------------------------------------------------------------------
  // Classes that inherit from BreakpointOptions can see and modify these
  //------------------------------------------------------------------
  bool IsOptionSet(OptionKind kind)
  {
    return m_set_flags.Test(kind);
  }

  enum class OptionNames {
    ConditionText = 0,
    IgnoreCount,
    EnabledState,
    OneShotState,
    AutoContinue,
    LastOptionName
  };
  static const char *g_option_names[(size_t)OptionNames::LastOptionName];

  static const char *GetKey(OptionNames enum_value) {
    return g_option_names[(size_t)enum_value];
  }

  static bool BreakpointOptionsCallbackFunction(
      void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
      lldb::user_id_t break_loc_id);

  void SetThreadSpec(std::unique_ptr<ThreadSpec> &thread_spec_up);

private:
  //------------------------------------------------------------------
  // For BreakpointOptions only
  //------------------------------------------------------------------
  BreakpointHitCallback m_callback;  // This is the callback function pointer
  lldb::BatonSP m_callback_baton_sp; // This is the client data for the callback
  bool m_baton_is_command_baton;
  bool m_callback_is_synchronous;
  bool m_enabled;
  bool m_one_shot;
  uint32_t m_ignore_count; // Number of times to ignore this breakpoint
  std::unique_ptr<ThreadSpec>
      m_thread_spec_ap;         // Thread for which this breakpoint will take
  std::string m_condition_text; // The condition to test.
  size_t m_condition_text_hash; // Its hash, so that locations know when the
                                // condition is updated.
  bool m_auto_continue;         // If set, auto-continue from breakpoint.
  Flags m_set_flags;            // Which options are set at this level.  Drawn
                                // from BreakpointOptions::SetOptionsFlags.
};

} // namespace lldb_private

#endif // liblldb_BreakpointOptions_h_
