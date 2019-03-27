//===-- StructuredDataPlugin.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef StructuredDataPlugin_h
#define StructuredDataPlugin_h

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {

class CommandObjectMultiword;

// -----------------------------------------------------------------------------
/// Plugin that supports process-related structured data sent asynchronously
/// from the debug monitor (e.g. debugserver, lldb-server, etc.)
///
/// This plugin type is activated by a Process-derived instance when that
/// instance detects that a given structured data feature is available.
///
/// StructuredDataPlugin instances are inherently tied to a process.  The
/// main functionality they support is the ability to consume asynchronously-
/// delivered structured data from the process monitor, and do something
/// reasonable with it.  Something reasonable can include broadcasting a
/// StructuredData event, which other parts of the system can then do with
/// as they please.  An IDE could use this facility to retrieve CPU usage,
/// memory usage, and other run-time aspects of the process.  That data
/// can then be displayed meaningfully to the user through the IDE.

/// For command-line LLDB, the Debugger instance listens for the structured
/// data events raised by the plugin, and give the plugin both the output
/// and error streams such that the plugin can display something about the
/// event, at a time when the debugger ensures it is safe to write to the
/// output or error streams.
// -----------------------------------------------------------------------------

class StructuredDataPlugin
    : public PluginInterface,
      public std::enable_shared_from_this<StructuredDataPlugin> {
public:
  virtual ~StructuredDataPlugin();

  lldb::ProcessSP GetProcess() const;

  // -------------------------------------------------------------------------
  // Public instance API
  // -------------------------------------------------------------------------

  // -------------------------------------------------------------------------
  /// Return whether this plugin supports the given StructuredData feature.
  ///
  /// When Process is informed of a list of process-monitor-supported
  /// structured data features, Process will go through the list of plugins,
  /// one at a time, and have the first plugin that supports a given feature
  /// be the plugin instantiated to handle that feature.  There is a 1-1
  /// correspondence between a Process instance and a StructuredDataPlugin
  /// mapped to that process.  A plugin can support handling multiple
  /// features, and if that happens, there is a single plugin instance
  /// created covering all of the mapped features for a given process.
  ///
  /// @param[in] type_name
  ///     The name of the feature tag supported by a process.
  ///     e.g. "darwin-log".
  ///
  /// @return
  ///     true if the plugin supports the feature; otherwise, false.
  // -------------------------------------------------------------------------
  virtual bool SupportsStructuredDataType(const ConstString &type_name) = 0;

  // -------------------------------------------------------------------------
  /// Handle the arrival of asynchronous structured data from the process.
  ///
  /// When asynchronous structured data arrives from the process monitor,
  /// it is immediately delivered to the plugin mapped for that feature
  /// if one exists.  The structured data that arrives from a process
  /// monitor must be a dictionary, and it must have a string field named
  /// "type" that must contain the StructuredData feature name set as the
  /// value.  This is the manner in which the data is routed to the proper
  /// plugin instance.
  ///
  /// @param[in] process
  ///     The process instance that just received the structured data.
  ///     This will always be the same process for a given instance of
  ///     a plugin.
  ///
  /// @param[in] type_name
  ///     The name of the feature tag for the asynchronous structured data.
  ///     Note this data will also be present in the \b object_sp dictionary
  ///     under the string value with key "type".
  ///
  /// @param[in] object_sp
  ///     A shared pointer to the structured data that arrived.  This must
  ///     be a dictionary.  The only key required is the aforementioned
  ///     key named "type" that must be a string value containing the
  ///     structured data type name.
  // -------------------------------------------------------------------------
  virtual void
  HandleArrivalOfStructuredData(Process &process, const ConstString &type_name,
                                const StructuredData::ObjectSP &object_sp) = 0;

  // -------------------------------------------------------------------------
  /// Get a human-readable description of the contents of the data.
  ///
  /// In command-line LLDB, this method will be called by the Debugger
  /// instance for each structured data event generated, and the output
  /// will be printed to the LLDB console.  If nothing is added to the stream,
  /// nothing will be printed; otherwise, a newline will be added to the end
  /// when displayed.
  ///
  /// @param[in] object_sp
  ///     A shared pointer to the structured data to format.
  ///
  /// @param[in] stream
  ///     The stream where the structured data should be pretty printed.
  ///
  /// @return
  ///     The error if formatting the object contents failed; otherwise,
  ///     success.
  // -------------------------------------------------------------------------
  virtual Status GetDescription(const StructuredData::ObjectSP &object_sp,
                                lldb_private::Stream &stream) = 0;

  // -------------------------------------------------------------------------
  /// Returns whether the plugin's features are enabled.
  ///
  /// This is a convenience method for plugins that can enable or disable
  /// their functionality.  It allows retrieval of this state without
  /// requiring a cast.
  ///
  /// @param[in] type_name
  ///     The name of the feature tag for the asynchronous structured data.
  ///     This is needed for plugins that support more than one feature.
  // -------------------------------------------------------------------------
  virtual bool GetEnabled(const ConstString &type_name) const;

  // -------------------------------------------------------------------------
  /// Allow the plugin to do work related to modules that loaded in the
  /// the corresponding process.
  ///
  /// This method defaults to doing nothing.  Plugins can override it
  /// if they have any behavior they want to enable/modify based on loaded
  /// modules.
  ///
  /// @param[in] process
  ///     The process that just was notified of modules having been loaded.
  ///     This will always be the same process for a given instance of
  ///     a plugin.
  ///
  /// @param[in] module_list
  ///     The list of modules that the process registered as having just
  ///     loaded.  See \b Process::ModulesDidLoad(...).
  // -------------------------------------------------------------------------
  virtual void ModulesDidLoad(Process &process, ModuleList &module_list);

protected:
  // -------------------------------------------------------------------------
  // Derived-class API
  // -------------------------------------------------------------------------
  StructuredDataPlugin(const lldb::ProcessWP &process_wp);

  // -------------------------------------------------------------------------
  /// Derived classes must call this before attempting to hook up commands
  /// to the 'plugin structured-data' tree.
  ///
  /// This ensures the relevant command and options hook points for all
  /// StructuredDataPlugin derived classes are available for this debugger.
  /// If this has already happened, this call is a no-op.
  ///
  /// @param[in] debugger
  ///     The Debugger instance for which we're creating the required shared
  ///     components for the StructuredDataPlugin derived classes.
  // -------------------------------------------------------------------------
  static void InitializeBasePluginForDebugger(Debugger &debugger);

private:
  lldb::ProcessWP m_process_wp;

  DISALLOW_COPY_AND_ASSIGN(StructuredDataPlugin);
};
}

#endif
