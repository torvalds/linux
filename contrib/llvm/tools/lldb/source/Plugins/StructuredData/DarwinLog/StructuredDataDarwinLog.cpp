//===-- StructuredDataDarwinLog.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StructuredDataDarwinLog.h"

// C includes
#include <string.h>

// C++ includes
#include <sstream>

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanCallOnFunctionExit.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"

#define DARWIN_LOG_TYPE_VALUE "DarwinLog"

using namespace lldb;
using namespace lldb_private;

#pragma mark -
#pragma mark Anonymous Namespace

// -----------------------------------------------------------------------------
// Anonymous namespace
// -----------------------------------------------------------------------------

namespace sddarwinlog_private {
const uint64_t NANOS_PER_MICRO = 1000;
const uint64_t NANOS_PER_MILLI = NANOS_PER_MICRO * 1000;
const uint64_t NANOS_PER_SECOND = NANOS_PER_MILLI * 1000;
const uint64_t NANOS_PER_MINUTE = NANOS_PER_SECOND * 60;
const uint64_t NANOS_PER_HOUR = NANOS_PER_MINUTE * 60;

static bool DEFAULT_FILTER_FALLTHROUGH_ACCEPTS = true;

//------------------------------------------------------------------
/// Global, sticky enable switch.  If true, the user has explicitly
/// run the enable command.  When a process launches or is attached to,
/// we will enable DarwinLog if either the settings for auto-enable is
/// on, or if the user had explicitly run enable at some point prior
/// to the launch/attach.
//------------------------------------------------------------------
static bool s_is_explicitly_enabled;

class EnableOptions;
using EnableOptionsSP = std::shared_ptr<EnableOptions>;

using OptionsMap =
    std::map<DebuggerWP, EnableOptionsSP, std::owner_less<DebuggerWP>>;

static OptionsMap &GetGlobalOptionsMap() {
  static OptionsMap s_options_map;
  return s_options_map;
}

static std::mutex &GetGlobalOptionsMapLock() {
  static std::mutex s_options_map_lock;
  return s_options_map_lock;
}

EnableOptionsSP GetGlobalEnableOptions(const DebuggerSP &debugger_sp) {
  if (!debugger_sp)
    return EnableOptionsSP();

  std::lock_guard<std::mutex> locker(GetGlobalOptionsMapLock());
  OptionsMap &options_map = GetGlobalOptionsMap();
  DebuggerWP debugger_wp(debugger_sp);
  auto find_it = options_map.find(debugger_wp);
  if (find_it != options_map.end())
    return find_it->second;
  else
    return EnableOptionsSP();
}

void SetGlobalEnableOptions(const DebuggerSP &debugger_sp,
                            const EnableOptionsSP &options_sp) {
  std::lock_guard<std::mutex> locker(GetGlobalOptionsMapLock());
  OptionsMap &options_map = GetGlobalOptionsMap();
  DebuggerWP debugger_wp(debugger_sp);
  auto find_it = options_map.find(debugger_wp);
  if (find_it != options_map.end())
    find_it->second = options_sp;
  else
    options_map.insert(std::make_pair(debugger_wp, options_sp));
}

#pragma mark -
#pragma mark Settings Handling

//------------------------------------------------------------------
/// Code to handle the StructuredDataDarwinLog settings
//------------------------------------------------------------------

static constexpr PropertyDefinition g_properties[] = {
    {
        "enable-on-startup",       // name
        OptionValue::eTypeBoolean, // type
        true,                      // global
        false,                     // default uint value
        nullptr,                   // default cstring value
        {},                        // enum values
        "Enable Darwin os_log collection when debugged process is launched "
        "or attached." // description
    },
    {
        "auto-enable-options",    // name
        OptionValue::eTypeString, // type
        true,                     // global
        0,                        // default uint value
        "",                       // default cstring value
        {},                       // enum values
        "Specify the options to 'plugin structured-data darwin-log enable' "
        "that should be applied when automatically enabling logging on "
        "startup/attach." // description
    }};

enum { ePropertyEnableOnStartup = 0, ePropertyAutoEnableOptions = 1 };

class StructuredDataDarwinLogProperties : public Properties {
public:
  static ConstString &GetSettingName() {
    static ConstString g_setting_name("darwin-log");
    return g_setting_name;
  }

  StructuredDataDarwinLogProperties() : Properties() {
    m_collection_sp.reset(new OptionValueProperties(GetSettingName()));
    m_collection_sp->Initialize(g_properties);
  }

  virtual ~StructuredDataDarwinLogProperties() {}

  bool GetEnableOnStartup() const {
    const uint32_t idx = ePropertyEnableOnStartup;
    return m_collection_sp->GetPropertyAtIndexAsBoolean(
        nullptr, idx, g_properties[idx].default_uint_value != 0);
  }

  llvm::StringRef GetAutoEnableOptions() const {
    const uint32_t idx = ePropertyAutoEnableOptions;
    return m_collection_sp->GetPropertyAtIndexAsString(
        nullptr, idx, g_properties[idx].default_cstr_value);
  }

  const char *GetLoggingModuleName() const { return "libsystem_trace.dylib"; }
};

using StructuredDataDarwinLogPropertiesSP =
    std::shared_ptr<StructuredDataDarwinLogProperties>;

static const StructuredDataDarwinLogPropertiesSP &GetGlobalProperties() {
  static StructuredDataDarwinLogPropertiesSP g_settings_sp;
  if (!g_settings_sp)
    g_settings_sp.reset(new StructuredDataDarwinLogProperties());
  return g_settings_sp;
}

const char *const s_filter_attributes[] = {
    "activity",       // current activity
    "activity-chain", // entire activity chain, each level separated by ':'
    "category",       // category of the log message
    "message",        // message contents, fully expanded
    "subsystem"       // subsystem of the log message

    // Consider implementing this action as it would be cheaper to filter.
    // "message" requires always formatting the message, which is a waste of
    // cycles if it ends up being rejected. "format",      // format string
    // used to format message text
};

static const ConstString &GetDarwinLogTypeName() {
  static const ConstString s_key_name("DarwinLog");
  return s_key_name;
}

static const ConstString &GetLogEventType() {
  static const ConstString s_event_type("log");
  return s_event_type;
}

class FilterRule;
using FilterRuleSP = std::shared_ptr<FilterRule>;

class FilterRule {
public:
  virtual ~FilterRule() {}

  using OperationCreationFunc =
      std::function<FilterRuleSP(bool accept, size_t attribute_index,
                                 const std::string &op_arg, Status &error)>;

  static void RegisterOperation(const ConstString &operation,
                                const OperationCreationFunc &creation_func) {
    GetCreationFuncMap().insert(std::make_pair(operation, creation_func));
  }

  static FilterRuleSP CreateRule(bool match_accepts, size_t attribute,
                                 const ConstString &operation,
                                 const std::string &op_arg, Status &error) {
    // Find the creation func for this type of filter rule.
    auto map = GetCreationFuncMap();
    auto find_it = map.find(operation);
    if (find_it == map.end()) {
      error.SetErrorStringWithFormat("unknown filter operation \""
                                     "%s\"",
                                     operation.GetCString());
      return FilterRuleSP();
    }

    return find_it->second(match_accepts, attribute, op_arg, error);
  }

  StructuredData::ObjectSP Serialize() const {
    StructuredData::Dictionary *dict_p = new StructuredData::Dictionary();

    // Indicate whether this is an accept or reject rule.
    dict_p->AddBooleanItem("accept", m_accept);

    // Indicate which attribute of the message this filter references. This can
    // drop into the rule-specific DoSerialization if we get to the point where
    // not all FilterRule derived classes work on an attribute.  (e.g. logical
    // and/or and other compound operations).
    dict_p->AddStringItem("attribute", s_filter_attributes[m_attribute_index]);

    // Indicate the type of the rule.
    dict_p->AddStringItem("type", GetOperationType().GetCString());

    // Let the rule add its own specific details here.
    DoSerialization(*dict_p);

    return StructuredData::ObjectSP(dict_p);
  }

  virtual void Dump(Stream &stream) const = 0;

  const ConstString &GetOperationType() const { return m_operation; }

protected:
  FilterRule(bool accept, size_t attribute_index, const ConstString &operation)
      : m_accept(accept), m_attribute_index(attribute_index),
        m_operation(operation) {}

  virtual void DoSerialization(StructuredData::Dictionary &dict) const = 0;

  bool GetMatchAccepts() const { return m_accept; }

  const char *GetFilterAttribute() const {
    return s_filter_attributes[m_attribute_index];
  }

private:
  using CreationFuncMap = std::map<ConstString, OperationCreationFunc>;

  static CreationFuncMap &GetCreationFuncMap() {
    static CreationFuncMap s_map;
    return s_map;
  }

  const bool m_accept;
  const size_t m_attribute_index;
  const ConstString m_operation;
};

using FilterRules = std::vector<FilterRuleSP>;

class RegexFilterRule : public FilterRule {
public:
  static void RegisterOperation() {
    FilterRule::RegisterOperation(StaticGetOperation(), CreateOperation);
  }

  void Dump(Stream &stream) const override {
    stream.Printf("%s %s regex %s", GetMatchAccepts() ? "accept" : "reject",
                  GetFilterAttribute(), m_regex_text.c_str());
  }

protected:
  void DoSerialization(StructuredData::Dictionary &dict) const override {
    dict.AddStringItem("regex", m_regex_text);
  }

private:
  static FilterRuleSP CreateOperation(bool accept, size_t attribute_index,
                                      const std::string &op_arg,
                                      Status &error) {
    // We treat the op_arg as a regex.  Validate it.
    if (op_arg.empty()) {
      error.SetErrorString("regex filter type requires a regex "
                           "argument");
      return FilterRuleSP();
    }

    // Instantiate the regex so we can report any errors.
    auto regex = RegularExpression(op_arg);
    if (!regex.IsValid()) {
      char error_text[256];
      error_text[0] = '\0';
      regex.GetErrorAsCString(error_text, sizeof(error_text));
      error.SetErrorString(error_text);
      return FilterRuleSP();
    }

    // We passed all our checks, this appears fine.
    error.Clear();
    return FilterRuleSP(new RegexFilterRule(accept, attribute_index, op_arg));
  }

  static const ConstString &StaticGetOperation() {
    static ConstString s_operation("regex");
    return s_operation;
  }

  RegexFilterRule(bool accept, size_t attribute_index,
                  const std::string &regex_text)
      : FilterRule(accept, attribute_index, StaticGetOperation()),
        m_regex_text(regex_text) {}

  const std::string m_regex_text;
};

class ExactMatchFilterRule : public FilterRule {
public:
  static void RegisterOperation() {
    FilterRule::RegisterOperation(StaticGetOperation(), CreateOperation);
  }

  void Dump(Stream &stream) const override {
    stream.Printf("%s %s match %s", GetMatchAccepts() ? "accept" : "reject",
                  GetFilterAttribute(), m_match_text.c_str());
  }

protected:
  void DoSerialization(StructuredData::Dictionary &dict) const override {
    dict.AddStringItem("exact_text", m_match_text);
  }

private:
  static FilterRuleSP CreateOperation(bool accept, size_t attribute_index,
                                      const std::string &op_arg,
                                      Status &error) {
    if (op_arg.empty()) {
      error.SetErrorString("exact match filter type requires an "
                           "argument containing the text that must "
                           "match the specified message attribute.");
      return FilterRuleSP();
    }

    error.Clear();
    return FilterRuleSP(
        new ExactMatchFilterRule(accept, attribute_index, op_arg));
  }

  static const ConstString &StaticGetOperation() {
    static ConstString s_operation("match");
    return s_operation;
  }

  ExactMatchFilterRule(bool accept, size_t attribute_index,
                       const std::string &match_text)
      : FilterRule(accept, attribute_index, StaticGetOperation()),
        m_match_text(match_text) {}

  const std::string m_match_text;
};

static void RegisterFilterOperations() {
  ExactMatchFilterRule::RegisterOperation();
  RegexFilterRule::RegisterOperation();
}

// =========================================================================
// Commands
// =========================================================================

// -------------------------------------------------------------------------
/// Provides the main on-off switch for enabling darwin logging.
///
/// It is valid to run the enable command when logging is already enabled.
/// This resets the logging with whatever settings are currently set.
// -------------------------------------------------------------------------

static constexpr OptionDefinition g_enable_option_table[] = {
    // Source stream include/exclude options (the first-level filter). This one
    // should be made as small as possible as everything that goes through here
    // must be processed by the process monitor.
    {LLDB_OPT_SET_ALL, false, "any-process", 'a', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Specifies log messages from other related processes should be "
     "included."},
    {LLDB_OPT_SET_ALL, false, "debug", 'd', OptionParser::eNoArgument, nullptr,
     {}, 0, eArgTypeNone,
     "Specifies debug-level log messages should be included.  Specifying"
     " --debug implies --info."},
    {LLDB_OPT_SET_ALL, false, "info", 'i', OptionParser::eNoArgument, nullptr,
     {}, 0, eArgTypeNone,
     "Specifies info-level log messages should be included."},
    {LLDB_OPT_SET_ALL, false, "filter", 'f', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgRawInput,
     // There doesn't appear to be a great way for me to have these multi-line,
     // formatted tables in help.  This looks mostly right but there are extra
     // linefeeds added at seemingly random spots, and indentation isn't
     // handled properly on those lines.
     "Appends a filter rule to the log message filter chain.  Multiple "
     "rules may be added by specifying this option multiple times, "
     "once per filter rule.  Filter rules are processed in the order "
     "they are specified, with the --no-match-accepts setting used "
     "for any message that doesn't match one of the rules.\n"
     "\n"
     "    Filter spec format:\n"
     "\n"
     "    --filter \"{action} {attribute} {op}\"\n"
     "\n"
     "    {action} :=\n"
     "      accept |\n"
     "      reject\n"
     "\n"
     "    {attribute} :=\n"
     "       activity       |  // message's most-derived activity\n"
     "       activity-chain |  // message's {parent}:{child} activity\n"
     "       category       |  // message's category\n"
     "       message        |  // message's expanded contents\n"
     "       subsystem      |  // message's subsystem\n"
     "\n"
     "    {op} :=\n"
     "      match {exact-match-text} |\n"
     "      regex {search-regex}\n"
     "\n"
     "The regex flavor used is the C++ std::regex ECMAScript format.  "
     "Prefer character classes like [[:digit:]] to \\d and the like, as "
     "getting the backslashes escaped through properly is error-prone."},
    {LLDB_OPT_SET_ALL, false, "live-stream", 'l',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,
     "Specify whether logging events are live-streamed or buffered.  "
     "True indicates live streaming, false indicates buffered.  The "
     "default is true (live streaming).  Live streaming will deliver "
     "log messages with less delay, but buffered capture mode has less "
     "of an observer effect."},
    {LLDB_OPT_SET_ALL, false, "no-match-accepts", 'n',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,
     "Specify whether a log message that doesn't match any filter rule "
     "is accepted or rejected, where true indicates accept.  The "
     "default is true."},
    {LLDB_OPT_SET_ALL, false, "echo-to-stderr", 'e',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,
     "Specify whether os_log()/NSLog() messages are echoed to the "
     "target program's stderr.  When DarwinLog is enabled, we shut off "
     "the mirroring of os_log()/NSLog() to the program's stderr.  "
     "Setting this flag to true will restore the stderr mirroring."
     "The default is false."},
    {LLDB_OPT_SET_ALL, false, "broadcast-events", 'b',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,
     "Specify if the plugin should broadcast events.  Broadcasting "
     "log events is a requirement for displaying the log entries in "
     "LLDB command-line.  It is also required if LLDB clients want to "
     "process log events.  The default is true."},
    // Message formatting options
    {LLDB_OPT_SET_ALL, false, "timestamp-relative", 'r',
     OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone,
     "Include timestamp in the message header when printing a log "
     "message.  The timestamp is relative to the first displayed "
     "message."},
    {LLDB_OPT_SET_ALL, false, "subsystem", 's', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Include the subsystem in the message header when displaying "
     "a log message."},
    {LLDB_OPT_SET_ALL, false, "category", 'c', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Include the category in the message header when displaying "
     "a log message."},
    {LLDB_OPT_SET_ALL, false, "activity-chain", 'C', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Include the activity parent-child chain in the message header "
     "when displaying a log message.  The activity hierarchy is "
     "displayed as {grandparent-activity}:"
     "{parent-activity}:{activity}[:...]."},
    {LLDB_OPT_SET_ALL, false, "all-fields", 'A', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Shortcut to specify that all header fields should be displayed."}};

class EnableOptions : public Options {
public:
  EnableOptions()
      : Options(), m_include_debug_level(false), m_include_info_level(false),
        m_include_any_process(false),
        m_filter_fall_through_accepts(DEFAULT_FILTER_FALLTHROUGH_ACCEPTS),
        m_echo_to_stderr(false), m_display_timestamp_relative(false),
        m_display_subsystem(false), m_display_category(false),
        m_display_activity_chain(false), m_broadcast_events(true),
        m_live_stream(true), m_filter_rules() {}

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_include_debug_level = false;
    m_include_info_level = false;
    m_include_any_process = false;
    m_filter_fall_through_accepts = DEFAULT_FILTER_FALLTHROUGH_ACCEPTS;
    m_echo_to_stderr = false;
    m_display_timestamp_relative = false;
    m_display_subsystem = false;
    m_display_category = false;
    m_display_activity_chain = false;
    m_broadcast_events = true;
    m_live_stream = true;
    m_filter_rules.clear();
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                        ExecutionContext *execution_context) override {
    Status error;

    const int short_option = m_getopt_table[option_idx].val;
    switch (short_option) {
    case 'a':
      m_include_any_process = true;
      break;

    case 'A':
      m_display_timestamp_relative = true;
      m_display_category = true;
      m_display_subsystem = true;
      m_display_activity_chain = true;
      break;

    case 'b':
      m_broadcast_events =
          OptionArgParser::ToBoolean(option_arg, true, nullptr);
      break;

    case 'c':
      m_display_category = true;
      break;

    case 'C':
      m_display_activity_chain = true;
      break;

    case 'd':
      m_include_debug_level = true;
      break;

    case 'e':
      m_echo_to_stderr = OptionArgParser::ToBoolean(option_arg, false, nullptr);
      break;

    case 'f':
      return ParseFilterRule(option_arg);

    case 'i':
      m_include_info_level = true;
      break;

    case 'l':
      m_live_stream = OptionArgParser::ToBoolean(option_arg, false, nullptr);
      break;

    case 'n':
      m_filter_fall_through_accepts =
          OptionArgParser::ToBoolean(option_arg, true, nullptr);
      break;

    case 'r':
      m_display_timestamp_relative = true;
      break;

    case 's':
      m_display_subsystem = true;
      break;

    default:
      error.SetErrorStringWithFormat("unsupported option '%c'", short_option);
    }
    return error;
  }

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::makeArrayRef(g_enable_option_table);
  }

  StructuredData::DictionarySP BuildConfigurationData(bool enabled) {
    StructuredData::DictionarySP config_sp(new StructuredData::Dictionary());

    // Set the basic enabled state.
    config_sp->AddBooleanItem("enabled", enabled);

    // If we're disabled, there's nothing more to add.
    if (!enabled)
      return config_sp;

    // Handle source stream flags.
    auto source_flags_sp =
        StructuredData::DictionarySP(new StructuredData::Dictionary());
    config_sp->AddItem("source-flags", source_flags_sp);

    source_flags_sp->AddBooleanItem("any-process", m_include_any_process);
    source_flags_sp->AddBooleanItem("debug-level", m_include_debug_level);
    // The debug-level flag, if set, implies info-level.
    source_flags_sp->AddBooleanItem("info-level", m_include_info_level ||
                                                      m_include_debug_level);
    source_flags_sp->AddBooleanItem("live-stream", m_live_stream);

    // Specify default filter rule (the fall-through)
    config_sp->AddBooleanItem("filter-fall-through-accepts",
                              m_filter_fall_through_accepts);

    // Handle filter rules
    if (!m_filter_rules.empty()) {
      auto json_filter_rules_sp =
          StructuredData::ArraySP(new StructuredData::Array);
      config_sp->AddItem("filter-rules", json_filter_rules_sp);
      for (auto &rule_sp : m_filter_rules) {
        if (!rule_sp)
          continue;
        json_filter_rules_sp->AddItem(rule_sp->Serialize());
      }
    }
    return config_sp;
  }

  bool GetIncludeDebugLevel() const { return m_include_debug_level; }

  bool GetIncludeInfoLevel() const {
    // Specifying debug level implies info level.
    return m_include_info_level || m_include_debug_level;
  }

  const FilterRules &GetFilterRules() const { return m_filter_rules; }

  bool GetFallthroughAccepts() const { return m_filter_fall_through_accepts; }

  bool GetEchoToStdErr() const { return m_echo_to_stderr; }

  bool GetDisplayTimestampRelative() const {
    return m_display_timestamp_relative;
  }

  bool GetDisplaySubsystem() const { return m_display_subsystem; }
  bool GetDisplayCategory() const { return m_display_category; }
  bool GetDisplayActivityChain() const { return m_display_activity_chain; }

  bool GetDisplayAnyHeaderFields() const {
    return m_display_timestamp_relative || m_display_activity_chain ||
           m_display_subsystem || m_display_category;
  }

  bool GetBroadcastEvents() const { return m_broadcast_events; }

private:
  Status ParseFilterRule(llvm::StringRef rule_text) {
    Status error;

    if (rule_text.empty()) {
      error.SetErrorString("invalid rule_text");
      return error;
    }

    // filter spec format:
    //
    // {action} {attribute} {op}
    //
    // {action} :=
    //   accept |
    //   reject
    //
    // {attribute} :=
    //   category       |
    //   subsystem      |
    //   activity       |
    //   activity-chain |
    //   message        |
    //   format
    //
    // {op} :=
    //   match {exact-match-text} |
    //   regex {search-regex}

    // Parse action.
    auto action_end_pos = rule_text.find(" ");
    if (action_end_pos == std::string::npos) {
      error.SetErrorStringWithFormat("could not parse filter rule "
                                     "action from \"%s\"",
                                     rule_text.str().c_str());
      return error;
    }
    auto action = rule_text.substr(0, action_end_pos);
    bool accept;
    if (action == "accept")
      accept = true;
    else if (action == "reject")
      accept = false;
    else {
      error.SetErrorString("filter action must be \"accept\" or \"deny\"");
      return error;
    }

    // parse attribute
    auto attribute_end_pos = rule_text.find(" ", action_end_pos + 1);
    if (attribute_end_pos == std::string::npos) {
      error.SetErrorStringWithFormat("could not parse filter rule "
                                     "attribute from \"%s\"",
                                     rule_text.str().c_str());
      return error;
    }
    auto attribute = rule_text.substr(action_end_pos + 1,
                                      attribute_end_pos - (action_end_pos + 1));
    auto attribute_index = MatchAttributeIndex(attribute);
    if (attribute_index < 0) {
      error.SetErrorStringWithFormat("filter rule attribute unknown: "
                                     "%s",
                                     attribute.str().c_str());
      return error;
    }

    // parse operation
    auto operation_end_pos = rule_text.find(" ", attribute_end_pos + 1);
    auto operation = rule_text.substr(
        attribute_end_pos + 1, operation_end_pos - (attribute_end_pos + 1));

    // add filter spec
    auto rule_sp =
        FilterRule::CreateRule(accept, attribute_index, ConstString(operation),
                               rule_text.substr(operation_end_pos + 1), error);

    if (rule_sp && error.Success())
      m_filter_rules.push_back(rule_sp);

    return error;
  }

  int MatchAttributeIndex(llvm::StringRef attribute_name) const {
    for (const auto &Item : llvm::enumerate(s_filter_attributes)) {
      if (attribute_name == Item.value())
        return Item.index();
    }

    // We didn't match anything.
    return -1;
  }

  bool m_include_debug_level;
  bool m_include_info_level;
  bool m_include_any_process;
  bool m_filter_fall_through_accepts;
  bool m_echo_to_stderr;
  bool m_display_timestamp_relative;
  bool m_display_subsystem;
  bool m_display_category;
  bool m_display_activity_chain;
  bool m_broadcast_events;
  bool m_live_stream;
  FilterRules m_filter_rules;
};

class EnableCommand : public CommandObjectParsed {
public:
  EnableCommand(CommandInterpreter &interpreter, bool enable, const char *name,
                const char *help, const char *syntax)
      : CommandObjectParsed(interpreter, name, help, syntax), m_enable(enable),
        m_options_sp(enable ? new EnableOptions() : nullptr) {}

protected:
  void AppendStrictSourcesWarning(CommandReturnObject &result,
                                  const char *source_name) {
    if (!source_name)
      return;

    // Check if we're *not* using strict sources.  If not, then the user is
    // going to get debug-level info anyways, probably not what they're
    // expecting. Unfortunately we can only fix this by adding an env var,
    // which would have had to have happened already.  Thus, a warning is the
    // best we can do here.
    StreamString stream;
    stream.Printf("darwin-log source settings specify to exclude "
                  "%s messages, but setting "
                  "'plugin.structured-data.darwin-log."
                  "strict-sources' is disabled.  This process will "
                  "automatically have %s messages included.  Enable"
                  " the property and relaunch the target binary to have"
                  " these messages excluded.",
                  source_name, source_name);
    result.AppendWarning(stream.GetString());
  }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    // First off, set the global sticky state of enable/disable based on this
    // command execution.
    s_is_explicitly_enabled = m_enable;

    // Next, if this is an enable, save off the option data. We will need it
    // later if a process hasn't been launched or attached yet.
    if (m_enable) {
      // Save off enabled configuration so we can apply these parsed options
      // the next time an attach or launch occurs.
      DebuggerSP debugger_sp =
          GetCommandInterpreter().GetDebugger().shared_from_this();
      SetGlobalEnableOptions(debugger_sp, m_options_sp);
    }

    // Now check if we have a running process.  If so, we should instruct the
    // process monitor to enable/disable DarwinLog support now.
    Target *target = GetSelectedOrDummyTarget();
    if (!target) {
      // No target, so there is nothing more to do right now.
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return true;
    }

    // Grab the active process.
    auto process_sp = target->GetProcessSP();
    if (!process_sp) {
      // No active process, so there is nothing more to do right now.
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return true;
    }

    // If the process is no longer alive, we can't do this now. We'll catch it
    // the next time the process is started up.
    if (!process_sp->IsAlive()) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return true;
    }

    // Get the plugin for the process.
    auto plugin_sp =
        process_sp->GetStructuredDataPlugin(GetDarwinLogTypeName());
    if (!plugin_sp || (plugin_sp->GetPluginName() !=
                       StructuredDataDarwinLog::GetStaticPluginName())) {
      result.AppendError("failed to get StructuredDataPlugin for "
                         "the process");
      result.SetStatus(eReturnStatusFailed);
    }
    StructuredDataDarwinLog &plugin =
        *static_cast<StructuredDataDarwinLog *>(plugin_sp.get());

    if (m_enable) {
      // Hook up the breakpoint for the process that detects when libtrace has
      // been sufficiently initialized to really start the os_log stream.  This
      // is insurance to assure us that logging is really enabled.  Requesting
      // that logging be enabled for a process before libtrace is initialized
      // results in a scenario where no errors occur, but no logging is
      // captured, either.  This step is to eliminate that possibility.
      plugin.AddInitCompletionHook(*process_sp.get());
    }

    // Send configuration to the feature by way of the process. Construct the
    // options we will use.
    auto config_sp = m_options_sp->BuildConfigurationData(m_enable);
    const Status error =
        process_sp->ConfigureStructuredData(GetDarwinLogTypeName(), config_sp);

    // Report results.
    if (!error.Success()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      // Our configuration failed, so we're definitely disabled.
      plugin.SetEnabled(false);
    } else {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      // Our configuration succeeded, so we're enabled/disabled per whichever
      // one this command is setup to do.
      plugin.SetEnabled(m_enable);
    }
    return result.Succeeded();
  }

  Options *GetOptions() override {
    // We don't have options when this represents disable.
    return m_enable ? m_options_sp.get() : nullptr;
  }

private:
  const bool m_enable;
  EnableOptionsSP m_options_sp;
};

// -------------------------------------------------------------------------
/// Provides the status command.
// -------------------------------------------------------------------------
class StatusCommand : public CommandObjectParsed {
public:
  StatusCommand(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "status",
                            "Show whether Darwin log supported is available"
                            " and enabled.",
                            "plugin structured-data darwin-log status") {}

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    auto &stream = result.GetOutputStream();

    // Figure out if we've got a process.  If so, we can tell if DarwinLog is
    // available for that process.
    Target *target = GetSelectedOrDummyTarget();
    auto process_sp = target ? target->GetProcessSP() : ProcessSP();
    if (!target || !process_sp) {
      stream.PutCString("Availability: unknown (requires process)\n");
      stream.PutCString("Enabled: not applicable "
                        "(requires process)\n");
    } else {
      auto plugin_sp =
          process_sp->GetStructuredDataPlugin(GetDarwinLogTypeName());
      stream.Printf("Availability: %s\n",
                    plugin_sp ? "available" : "unavailable");
      auto &plugin_name = StructuredDataDarwinLog::GetStaticPluginName();
      const bool enabled =
          plugin_sp ? plugin_sp->GetEnabled(plugin_name) : false;
      stream.Printf("Enabled: %s\n", enabled ? "true" : "false");
    }

    // Display filter settings.
    DebuggerSP debugger_sp =
        GetCommandInterpreter().GetDebugger().shared_from_this();
    auto options_sp = GetGlobalEnableOptions(debugger_sp);
    if (!options_sp) {
      // Nothing more to do.
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return true;
    }

    // Print filter rules
    stream.PutCString("DarwinLog filter rules:\n");

    stream.IndentMore();

    if (options_sp->GetFilterRules().empty()) {
      stream.Indent();
      stream.PutCString("none\n");
    } else {
      // Print each of the filter rules.
      int rule_number = 0;
      for (auto rule_sp : options_sp->GetFilterRules()) {
        ++rule_number;
        if (!rule_sp)
          continue;

        stream.Indent();
        stream.Printf("%02d: ", rule_number);
        rule_sp->Dump(stream);
        stream.PutChar('\n');
      }
    }
    stream.IndentLess();

    // Print no-match handling.
    stream.Indent();
    stream.Printf("no-match behavior: %s\n",
                  options_sp->GetFallthroughAccepts() ? "accept" : "reject");

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

// -------------------------------------------------------------------------
/// Provides the darwin-log base command
// -------------------------------------------------------------------------
class BaseCommand : public CommandObjectMultiword {
public:
  BaseCommand(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "plugin structured-data darwin-log",
                               "Commands for configuring Darwin os_log "
                               "support.",
                               "") {
    // enable
    auto enable_help = "Enable Darwin log collection, or re-enable "
                       "with modified configuration.";
    auto enable_syntax = "plugin structured-data darwin-log enable";
    auto enable_cmd_sp = CommandObjectSP(
        new EnableCommand(interpreter,
                          true, // enable
                          "enable", enable_help, enable_syntax));
    LoadSubCommand("enable", enable_cmd_sp);

    // disable
    auto disable_help = "Disable Darwin log collection.";
    auto disable_syntax = "plugin structured-data darwin-log disable";
    auto disable_cmd_sp = CommandObjectSP(
        new EnableCommand(interpreter,
                          false, // disable
                          "disable", disable_help, disable_syntax));
    LoadSubCommand("disable", disable_cmd_sp);

    // status
    auto status_cmd_sp = CommandObjectSP(new StatusCommand(interpreter));
    LoadSubCommand("status", status_cmd_sp);
  }
};

EnableOptionsSP ParseAutoEnableOptions(Status &error, Debugger &debugger) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
  // We are abusing the options data model here so that we can parse options
  // without requiring the Debugger instance.

  // We have an empty execution context at this point.  We only want to parse
  // options, and we don't need any context to do this here. In fact, we want
  // to be able to parse the enable options before having any context.
  ExecutionContext exe_ctx;

  EnableOptionsSP options_sp(new EnableOptions());
  options_sp->NotifyOptionParsingStarting(&exe_ctx);

  CommandReturnObject result;

  // Parse the arguments.
  auto options_property_sp =
      debugger.GetPropertyValue(nullptr, "plugin.structured-data.darwin-log."
                                         "auto-enable-options",
                                false, error);
  if (!error.Success())
    return EnableOptionsSP();
  if (!options_property_sp) {
    error.SetErrorString("failed to find option setting for "
                         "plugin.structured-data.darwin-log.");
    return EnableOptionsSP();
  }

  const char *enable_options =
      options_property_sp->GetAsString()->GetCurrentValue();
  Args args(enable_options);
  if (args.GetArgumentCount() > 0) {
    // Eliminate the initial '--' that would be required to set the settings
    // that themselves include '-' and/or '--'.
    const char *first_arg = args.GetArgumentAtIndex(0);
    if (first_arg && (strcmp(first_arg, "--") == 0))
      args.Shift();
  }

  bool require_validation = false;
  llvm::Expected<Args> args_or =
      options_sp->Parse(args, &exe_ctx, PlatformSP(), require_validation);
  if (!args_or) {
    LLDB_LOG_ERROR(
        log, args_or.takeError(),
        "Parsing plugin.structured-data.darwin-log.auto-enable-options value "
        "failed: {0}");
    return EnableOptionsSP();
  }

  if (!options_sp->VerifyOptions(result))
    return EnableOptionsSP();

  // We successfully parsed and validated the options.
  return options_sp;
}

bool RunEnableCommand(CommandInterpreter &interpreter) {
  StreamString command_stream;

  command_stream << "plugin structured-data darwin-log enable";
  auto enable_options = GetGlobalProperties()->GetAutoEnableOptions();
  if (!enable_options.empty()) {
    command_stream << ' ';
    command_stream << enable_options;
  }

  // Run the command.
  CommandReturnObject return_object;
  interpreter.HandleCommand(command_stream.GetData(), eLazyBoolNo,
                            return_object);
  return return_object.Succeeded();
}
}
using namespace sddarwinlog_private;

#pragma mark -
#pragma mark Public static API

// -----------------------------------------------------------------------------
// Public static API
// -----------------------------------------------------------------------------

void StructuredDataDarwinLog::Initialize() {
  RegisterFilterOperations();
  PluginManager::RegisterPlugin(
      GetStaticPluginName(), "Darwin os_log() and os_activity() support",
      &CreateInstance, &DebuggerInitialize, &FilterLaunchInfo);
}

void StructuredDataDarwinLog::Terminate() {
  PluginManager::UnregisterPlugin(&CreateInstance);
}

const ConstString &StructuredDataDarwinLog::GetStaticPluginName() {
  static ConstString s_plugin_name("darwin-log");
  return s_plugin_name;
}

#pragma mark -
#pragma mark PluginInterface API

// -----------------------------------------------------------------------------
// PluginInterface API
// -----------------------------------------------------------------------------

ConstString StructuredDataDarwinLog::GetPluginName() {
  return GetStaticPluginName();
}

uint32_t StructuredDataDarwinLog::GetPluginVersion() { return 1; }

#pragma mark -
#pragma mark StructuredDataPlugin API

// -----------------------------------------------------------------------------
// StructuredDataPlugin API
// -----------------------------------------------------------------------------

bool StructuredDataDarwinLog::SupportsStructuredDataType(
    const ConstString &type_name) {
  return type_name == GetDarwinLogTypeName();
}

void StructuredDataDarwinLog::HandleArrivalOfStructuredData(
    Process &process, const ConstString &type_name,
    const StructuredData::ObjectSP &object_sp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log) {
    StreamString json_stream;
    if (object_sp)
      object_sp->Dump(json_stream);
    else
      json_stream.PutCString("<null>");
    log->Printf("StructuredDataDarwinLog::%s() called with json: %s",
                __FUNCTION__, json_stream.GetData());
  }

  // Ignore empty structured data.
  if (!object_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() StructuredData object "
                  "is null",
                  __FUNCTION__);
    return;
  }

  // Ignore any data that isn't for us.
  if (type_name != GetDarwinLogTypeName()) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() StructuredData type "
                  "expected to be %s but was %s, ignoring",
                  __FUNCTION__, GetDarwinLogTypeName().AsCString(),
                  type_name.AsCString());
    return;
  }

  // Broadcast the structured data event if we have that enabled. This is the
  // way that the outside world (all clients) get access to this data.  This
  // plugin sets policy as to whether we do that.
  DebuggerSP debugger_sp = process.GetTarget().GetDebugger().shared_from_this();
  auto options_sp = GetGlobalEnableOptions(debugger_sp);
  if (options_sp && options_sp->GetBroadcastEvents()) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() broadcasting event",
                  __FUNCTION__);
    process.BroadcastStructuredData(object_sp, shared_from_this());
  }

  // Later, hang on to a configurable amount of these and allow commands to
  // inspect, including showing backtraces.
}

static void SetErrorWithJSON(Status &error, const char *message,
                             StructuredData::Object &object) {
  if (!message) {
    error.SetErrorString("Internal error: message not set.");
    return;
  }

  StreamString object_stream;
  object.Dump(object_stream);
  object_stream.Flush();

  error.SetErrorStringWithFormat("%s: %s", message, object_stream.GetData());
}

Status StructuredDataDarwinLog::GetDescription(
    const StructuredData::ObjectSP &object_sp, lldb_private::Stream &stream) {
  Status error;

  if (!object_sp) {
    error.SetErrorString("No structured data.");
    return error;
  }

  // Log message payload objects will be dictionaries.
  const StructuredData::Dictionary *dictionary = object_sp->GetAsDictionary();
  if (!dictionary) {
    SetErrorWithJSON(error, "Structured data should have been a dictionary "
                            "but wasn't",
                     *object_sp);
    return error;
  }

  // Validate this is really a message for our plugin.
  ConstString type_name;
  if (!dictionary->GetValueForKeyAsString("type", type_name)) {
    SetErrorWithJSON(error, "Structured data doesn't contain mandatory "
                            "type field",
                     *object_sp);
    return error;
  }

  if (type_name != GetDarwinLogTypeName()) {
    // This is okay - it simply means the data we received is not a log
    // message.  We'll just format it as is.
    object_sp->Dump(stream);
    return error;
  }

  // DarwinLog dictionaries store their data
  // in an array with key name "events".
  StructuredData::Array *events = nullptr;
  if (!dictionary->GetValueForKeyAsArray("events", events) || !events) {
    SetErrorWithJSON(error, "Log structured data is missing mandatory "
                            "'events' field, expected to be an array",
                     *object_sp);
    return error;
  }

  events->ForEach(
      [&stream, &error, &object_sp, this](StructuredData::Object *object) {
        if (!object) {
          // Invalid.  Stop iterating.
          SetErrorWithJSON(error, "Log event entry is null", *object_sp);
          return false;
        }

        auto event = object->GetAsDictionary();
        if (!event) {
          // Invalid, stop iterating.
          SetErrorWithJSON(error, "Log event is not a dictionary", *object_sp);
          return false;
        }

        // If we haven't already grabbed the first timestamp value, do that
        // now.
        if (!m_recorded_first_timestamp) {
          uint64_t timestamp = 0;
          if (event->GetValueForKeyAsInteger("timestamp", timestamp)) {
            m_first_timestamp_seen = timestamp;
            m_recorded_first_timestamp = true;
          }
        }

        HandleDisplayOfEvent(*event, stream);
        return true;
      });

  stream.Flush();
  return error;
}

bool StructuredDataDarwinLog::GetEnabled(const ConstString &type_name) const {
  if (type_name == GetStaticPluginName())
    return m_is_enabled;
  else
    return false;
}

void StructuredDataDarwinLog::SetEnabled(bool enabled) {
  m_is_enabled = enabled;
}

void StructuredDataDarwinLog::ModulesDidLoad(Process &process,
                                             ModuleList &module_list) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("StructuredDataDarwinLog::%s called (process uid %u)",
                __FUNCTION__, process.GetUniqueID());

  // Check if we should enable the darwin log support on startup/attach.
  if (!GetGlobalProperties()->GetEnableOnStartup() &&
      !s_is_explicitly_enabled) {
    // We're neither auto-enabled or explicitly enabled, so we shouldn't try to
    // enable here.
    if (log)
      log->Printf("StructuredDataDarwinLog::%s not applicable, we're not "
                  "enabled (process uid %u)",
                  __FUNCTION__, process.GetUniqueID());
    return;
  }

  // If we already added the breakpoint, we've got nothing left to do.
  {
    std::lock_guard<std::mutex> locker(m_added_breakpoint_mutex);
    if (m_added_breakpoint) {
      if (log)
        log->Printf("StructuredDataDarwinLog::%s process uid %u's "
                    "post-libtrace-init breakpoint is already set",
                    __FUNCTION__, process.GetUniqueID());
      return;
    }
  }

  // The logging support module name, specifies the name of the image name that
  // must be loaded into the debugged process before we can try to enable
  // logging.
  const char *logging_module_cstr =
      GetGlobalProperties()->GetLoggingModuleName();
  if (!logging_module_cstr || (logging_module_cstr[0] == 0)) {
    // We need this.  Bail.
    if (log)
      log->Printf("StructuredDataDarwinLog::%s no logging module name "
                  "specified, we don't know where to set a breakpoint "
                  "(process uid %u)",
                  __FUNCTION__, process.GetUniqueID());
    return;
  }

  const ConstString logging_module_name = ConstString(logging_module_cstr);

  // We need to see libtrace in the list of modules before we can enable
  // tracing for the target process.
  bool found_logging_support_module = false;
  for (size_t i = 0; i < module_list.GetSize(); ++i) {
    auto module_sp = module_list.GetModuleAtIndex(i);
    if (!module_sp)
      continue;

    auto &file_spec = module_sp->GetFileSpec();
    found_logging_support_module =
        (file_spec.GetLastPathComponent() == logging_module_name);
    if (found_logging_support_module)
      break;
  }

  if (!found_logging_support_module) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s logging module %s "
                  "has not yet been loaded, can't set a breakpoint "
                  "yet (process uid %u)",
                  __FUNCTION__, logging_module_name.AsCString(),
                  process.GetUniqueID());
    return;
  }

  // Time to enqueue the breakpoint so we can wait for logging support to be
  // initialized before we try to tap the libtrace stream.
  AddInitCompletionHook(process);
  if (log)
    log->Printf("StructuredDataDarwinLog::%s post-init hook breakpoint "
                "set for logging module %s (process uid %u)",
                __FUNCTION__, logging_module_name.AsCString(),
                process.GetUniqueID());

  // We need to try the enable here as well, which will succeed in the event
  // that we're attaching to (rather than launching) the process and the
  // process is already past initialization time.  In that case, the completion
  // breakpoint will never get hit and therefore won't start that way.  It
  // doesn't hurt much beyond a bit of bandwidth if we end up doing this twice.
  // It hurts much more if we don't get the logging enabled when the user
  // expects it.
  EnableNow();
}

// -----------------------------------------------------------------------------
// public destructor
// -----------------------------------------------------------------------------

StructuredDataDarwinLog::~StructuredDataDarwinLog() {
  if (m_breakpoint_id != LLDB_INVALID_BREAK_ID) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      process_sp->GetTarget().RemoveBreakpointByID(m_breakpoint_id);
      m_breakpoint_id = LLDB_INVALID_BREAK_ID;
    }
  }
}

#pragma mark -
#pragma mark Private instance methods

// -----------------------------------------------------------------------------
// Private constructors
// -----------------------------------------------------------------------------

StructuredDataDarwinLog::StructuredDataDarwinLog(const ProcessWP &process_wp)
    : StructuredDataPlugin(process_wp), m_recorded_first_timestamp(false),
      m_first_timestamp_seen(0), m_is_enabled(false),
      m_added_breakpoint_mutex(), m_added_breakpoint(),
      m_breakpoint_id(LLDB_INVALID_BREAK_ID) {}

// -----------------------------------------------------------------------------
// Private static methods
// -----------------------------------------------------------------------------

StructuredDataPluginSP
StructuredDataDarwinLog::CreateInstance(Process &process) {
  // Currently only Apple targets support the os_log/os_activity protocol.
  if (process.GetTarget().GetArchitecture().GetTriple().getVendor() ==
      llvm::Triple::VendorType::Apple) {
    auto process_wp = ProcessWP(process.shared_from_this());
    return StructuredDataPluginSP(new StructuredDataDarwinLog(process_wp));
  } else {
    return StructuredDataPluginSP();
  }
}

void StructuredDataDarwinLog::DebuggerInitialize(Debugger &debugger) {
  // Setup parent class first.
  StructuredDataPlugin::InitializeBasePluginForDebugger(debugger);

  // Get parent command.
  auto &interpreter = debugger.GetCommandInterpreter();
  llvm::StringRef parent_command_text = "plugin structured-data";
  auto parent_command =
      interpreter.GetCommandObjectForCommand(parent_command_text);
  if (!parent_command) {
    // Ut oh, parent failed to create parent command.
    // TODO log
    return;
  }

  auto command_name = "darwin-log";
  auto command_sp = CommandObjectSP(new BaseCommand(interpreter));
  bool result = parent_command->LoadSubCommand(command_name, command_sp);
  if (!result) {
    // TODO log it once we setup structured data logging
  }

  if (!PluginManager::GetSettingForPlatformPlugin(
          debugger, StructuredDataDarwinLogProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForStructuredDataPlugin(
        debugger, GetGlobalProperties()->GetValueProperties(),
        ConstString("Properties for the darwin-log"
                    " plug-in."),
        is_global_setting);
  }
}

Status StructuredDataDarwinLog::FilterLaunchInfo(ProcessLaunchInfo &launch_info,
                                                 Target *target) {
  Status error;

  // If we're not debugging this launched process, there's nothing for us to do
  // here.
  if (!launch_info.GetFlags().AnySet(eLaunchFlagDebug))
    return error;

  // Darwin os_log() support automatically adds debug-level and info-level
  // messages when a debugger is attached to a process.  However, with
  // integrated support for debugging built into the command-line LLDB, the
  // user may specifically set to *not* include debug-level and info-level
  // content.  When the user is using the integrated log support, we want to
  // put the kabosh on that automatic adding of info and debug level. This is
  // done by adding an environment variable to the process on launch. (This
  // also means it is not possible to suppress this behavior if attaching to an
  // already-running app).
  // Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));

  // If the target architecture is not one that supports DarwinLog, we have
  // nothing to do here.
  auto &triple = target ? target->GetArchitecture().GetTriple()
                        : launch_info.GetArchitecture().GetTriple();
  if (triple.getVendor() != llvm::Triple::Apple) {
    return error;
  }

  // If DarwinLog is not enabled (either by explicit user command or via the
  // auto-enable option), then we have nothing to do.
  if (!GetGlobalProperties()->GetEnableOnStartup() &&
      !s_is_explicitly_enabled) {
    // Nothing to do, DarwinLog is not enabled.
    return error;
  }

  // If we don't have parsed configuration info, that implies we have enable-
  // on-startup set up, but we haven't yet attempted to run the enable command.
  if (!target) {
    // We really can't do this without a target.  We need to be able to get to
    // the debugger to get the proper options to do this right.
    // TODO log.
    error.SetErrorString("requires a target to auto-enable DarwinLog.");
    return error;
  }

  DebuggerSP debugger_sp = target->GetDebugger().shared_from_this();
  auto options_sp = GetGlobalEnableOptions(debugger_sp);
  if (!options_sp && debugger_sp) {
    options_sp = ParseAutoEnableOptions(error, *debugger_sp.get());
    if (!options_sp || !error.Success())
      return error;

    // We already parsed the options, save them now so we don't generate them
    // again until the user runs the command manually.
    SetGlobalEnableOptions(debugger_sp, options_sp);
  }

  if (!options_sp->GetEchoToStdErr()) {
    // The user doesn't want to see os_log/NSLog messages echo to stderr. That
    // mechanism is entirely separate from the DarwinLog support. By default we
    // don't want to get it via stderr, because that would be in duplicate of
    // the explicit log support here.

    // Here we need to strip out any OS_ACTIVITY_DT_MODE setting to prevent
    // echoing of os_log()/NSLog() to stderr in the target program.
    launch_info.GetEnvironment().erase("OS_ACTIVITY_DT_MODE");

    // We will also set the env var that tells any downstream launcher from
    // adding OS_ACTIVITY_DT_MODE.
    launch_info.GetEnvironment()["IDE_DISABLED_OS_ACTIVITY_DT_MODE"] = "1";
  }

  // Set the OS_ACTIVITY_MODE env var appropriately to enable/disable debug and
  // info level messages.
  const char *env_var_value;
  if (options_sp->GetIncludeDebugLevel())
    env_var_value = "debug";
  else if (options_sp->GetIncludeInfoLevel())
    env_var_value = "info";
  else
    env_var_value = "default";

  launch_info.GetEnvironment()["OS_ACTIVITY_MODE"] = env_var_value;

  return error;
}

bool StructuredDataDarwinLog::InitCompletionHookCallback(
    void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
    lldb::user_id_t break_loc_id) {
  // We hit the init function.  We now want to enqueue our new thread plan,
  // which will in turn enqueue a StepOut thread plan. When the StepOut
  // finishes and control returns to our new thread plan, that is the time when
  // we can execute our logic to enable the logging support.

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() called", __FUNCTION__);

  // Get the current thread.
  if (!context) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: no context, "
                  "ignoring",
                  __FUNCTION__);
    return false;
  }

  // Get the plugin from the process.
  auto process_sp = context->exe_ctx_ref.GetProcessSP();
  if (!process_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: invalid "
                  "process in context, ignoring",
                  __FUNCTION__);
    return false;
  }
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() call is for process uid %d",
                __FUNCTION__, process_sp->GetUniqueID());

  auto plugin_sp = process_sp->GetStructuredDataPlugin(GetDarwinLogTypeName());
  if (!plugin_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: no plugin for "
                  "feature %s in process uid %u",
                  __FUNCTION__, GetDarwinLogTypeName().AsCString(),
                  process_sp->GetUniqueID());
    return false;
  }

  // Create the callback for when the thread plan completes.
  bool called_enable_method = false;
  const auto process_uid = process_sp->GetUniqueID();

  std::weak_ptr<StructuredDataPlugin> plugin_wp(plugin_sp);
  ThreadPlanCallOnFunctionExit::Callback callback =
      [plugin_wp, &called_enable_method, log, process_uid]() {
        if (log)
          log->Printf("StructuredDataDarwinLog::post-init callback: "
                      "called (process uid %u)",
                      process_uid);

        auto strong_plugin_sp = plugin_wp.lock();
        if (!strong_plugin_sp) {
          if (log)
            log->Printf("StructuredDataDarwinLog::post-init callback: "
                        "plugin no longer exists, ignoring (process "
                        "uid %u)",
                        process_uid);
          return;
        }
        // Make sure we only call it once, just in case the thread plan hits
        // the breakpoint twice.
        if (!called_enable_method) {
          if (log)
            log->Printf("StructuredDataDarwinLog::post-init callback: "
                        "calling EnableNow() (process uid %u)",
                        process_uid);
          static_cast<StructuredDataDarwinLog *>(strong_plugin_sp.get())
              ->EnableNow();
          called_enable_method = true;
        } else {
          // Our breakpoint was hit more than once.  Unexpected but no harm
          // done.  Log it.
          if (log)
            log->Printf("StructuredDataDarwinLog::post-init callback: "
                        "skipping EnableNow(), already called by "
                        "callback [we hit this more than once] "
                        "(process uid %u)",
                        process_uid);
        }
      };

  // Grab the current thread.
  auto thread_sp = context->exe_ctx_ref.GetThreadSP();
  if (!thread_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: failed to "
                  "retrieve the current thread from the execution "
                  "context, nowhere to run the thread plan (process uid "
                  "%u)",
                  __FUNCTION__, process_sp->GetUniqueID());
    return false;
  }

  // Queue the thread plan.
  auto thread_plan_sp = ThreadPlanSP(
      new ThreadPlanCallOnFunctionExit(*thread_sp.get(), callback));
  const bool abort_other_plans = false;
  thread_sp->QueueThreadPlan(thread_plan_sp, abort_other_plans);
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() queuing thread plan on "
                "trace library init method entry (process uid %u)",
                __FUNCTION__, process_sp->GetUniqueID());

  // We return false here to indicate that it isn't a public stop.
  return false;
}

void StructuredDataDarwinLog::AddInitCompletionHook(Process &process) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() called (process uid %u)",
                __FUNCTION__, process.GetUniqueID());

  // Make sure we haven't already done this.
  {
    std::lock_guard<std::mutex> locker(m_added_breakpoint_mutex);
    if (m_added_breakpoint) {
      if (log)
        log->Printf("StructuredDataDarwinLog::%s() ignoring request, "
                    "breakpoint already set (process uid %u)",
                    __FUNCTION__, process.GetUniqueID());
      return;
    }

    // We're about to do this, don't let anybody else try to do it.
    m_added_breakpoint = true;
  }

  // Set a breakpoint for the process that will kick in when libtrace has
  // finished its initialization.
  Target &target = process.GetTarget();

  // Build up the module list.
  FileSpecList module_spec_list;
  auto module_file_spec =
      FileSpec(GetGlobalProperties()->GetLoggingModuleName());
  module_spec_list.Append(module_file_spec);

  // We aren't specifying a source file set.
  FileSpecList *source_spec_list = nullptr;

  const char *func_name = "_libtrace_init";
  const lldb::addr_t offset = 0;
  const LazyBool skip_prologue = eLazyBoolCalculate;
  // This is an internal breakpoint - the user shouldn't see it.
  const bool internal = true;
  const bool hardware = false;

  auto breakpoint_sp = target.CreateBreakpoint(
      &module_spec_list, source_spec_list, func_name, eFunctionNameTypeFull,
      eLanguageTypeC, offset, skip_prologue, internal, hardware);
  if (!breakpoint_sp) {
    // Huh?  Bail here.
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() failed to set "
                  "breakpoint in module %s, function %s (process uid %u)",
                  __FUNCTION__, GetGlobalProperties()->GetLoggingModuleName(),
                  func_name, process.GetUniqueID());
    return;
  }

  // Set our callback.
  breakpoint_sp->SetCallback(InitCompletionHookCallback, nullptr);
  m_breakpoint_id = breakpoint_sp->GetID();
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() breakpoint set in module %s,"
                "function %s (process uid %u)",
                __FUNCTION__, GetGlobalProperties()->GetLoggingModuleName(),
                func_name, process.GetUniqueID());
}

void StructuredDataDarwinLog::DumpTimestamp(Stream &stream,
                                            uint64_t timestamp) {
  const uint64_t delta_nanos = timestamp - m_first_timestamp_seen;

  const uint64_t hours = delta_nanos / NANOS_PER_HOUR;
  uint64_t nanos_remaining = delta_nanos % NANOS_PER_HOUR;

  const uint64_t minutes = nanos_remaining / NANOS_PER_MINUTE;
  nanos_remaining = nanos_remaining % NANOS_PER_MINUTE;

  const uint64_t seconds = nanos_remaining / NANOS_PER_SECOND;
  nanos_remaining = nanos_remaining % NANOS_PER_SECOND;

  stream.Printf("%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%09" PRIu64, hours,
                minutes, seconds, nanos_remaining);
}

size_t
StructuredDataDarwinLog::DumpHeader(Stream &output_stream,
                                    const StructuredData::Dictionary &event) {
  StreamString stream;

  ProcessSP process_sp = GetProcess();
  if (!process_sp) {
    // TODO log
    return 0;
  }

  DebuggerSP debugger_sp =
      process_sp->GetTarget().GetDebugger().shared_from_this();
  if (!debugger_sp) {
    // TODO log
    return 0;
  }

  auto options_sp = GetGlobalEnableOptions(debugger_sp);
  if (!options_sp) {
    // TODO log
    return 0;
  }

  // Check if we should even display a header.
  if (!options_sp->GetDisplayAnyHeaderFields())
    return 0;

  stream.PutChar('[');

  int header_count = 0;
  if (options_sp->GetDisplayTimestampRelative()) {
    uint64_t timestamp = 0;
    if (event.GetValueForKeyAsInteger("timestamp", timestamp)) {
      DumpTimestamp(stream, timestamp);
      ++header_count;
    }
  }

  if (options_sp->GetDisplayActivityChain()) {
    llvm::StringRef activity_chain;
    if (event.GetValueForKeyAsString("activity-chain", activity_chain) &&
        !activity_chain.empty()) {
      if (header_count > 0)
        stream.PutChar(',');

      // Display the activity chain, from parent-most to child-most activity,
      // separated by a colon (:).
      stream.PutCString("activity-chain=");
      stream.PutCString(activity_chain);
      ++header_count;
    }
  }

  if (options_sp->GetDisplaySubsystem()) {
    llvm::StringRef subsystem;
    if (event.GetValueForKeyAsString("subsystem", subsystem) &&
        !subsystem.empty()) {
      if (header_count > 0)
        stream.PutChar(',');
      stream.PutCString("subsystem=");
      stream.PutCString(subsystem);
      ++header_count;
    }
  }

  if (options_sp->GetDisplayCategory()) {
    llvm::StringRef category;
    if (event.GetValueForKeyAsString("category", category) &&
        !category.empty()) {
      if (header_count > 0)
        stream.PutChar(',');
      stream.PutCString("category=");
      stream.PutCString(category);
      ++header_count;
    }
  }
  stream.PutCString("] ");

  output_stream.PutCString(stream.GetString());

  return stream.GetSize();
}

size_t StructuredDataDarwinLog::HandleDisplayOfEvent(
    const StructuredData::Dictionary &event, Stream &stream) {
  // Check the type of the event.
  ConstString event_type;
  if (!event.GetValueForKeyAsString("type", event_type)) {
    // Hmm, we expected to get events that describe what they are.  Continue
    // anyway.
    return 0;
  }

  if (event_type != GetLogEventType())
    return 0;

  size_t total_bytes = 0;

  // Grab the message content.
  llvm::StringRef message;
  if (!event.GetValueForKeyAsString("message", message))
    return true;

  // Display the log entry.
  const auto len = message.size();

  total_bytes += DumpHeader(stream, event);

  stream.Write(message.data(), len);
  total_bytes += len;

  // Add an end of line.
  stream.PutChar('\n');
  total_bytes += sizeof(char);

  return total_bytes;
}

void StructuredDataDarwinLog::EnableNow() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() called", __FUNCTION__);

  // Run the enable command.
  auto process_sp = GetProcess();
  if (!process_sp) {
    // Nothing to do.
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: failed to get "
                  "valid process, skipping",
                  __FUNCTION__);
    return;
  }
  if (log)
    log->Printf("StructuredDataDarwinLog::%s() call is for process uid %u",
                __FUNCTION__, process_sp->GetUniqueID());

  // If we have configuration data, we can directly enable it now. Otherwise,
  // we need to run through the command interpreter to parse the auto-run
  // options (which is the only way we get here without having already-parsed
  // configuration data).
  DebuggerSP debugger_sp =
      process_sp->GetTarget().GetDebugger().shared_from_this();
  if (!debugger_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: failed to get "
                  "debugger shared pointer, skipping (process uid %u)",
                  __FUNCTION__, process_sp->GetUniqueID());
    return;
  }

  auto options_sp = GetGlobalEnableOptions(debugger_sp);
  if (!options_sp) {
    // We haven't run the enable command yet.  Just do that now, it'll take
    // care of the rest.
    auto &interpreter = debugger_sp->GetCommandInterpreter();
    const bool success = RunEnableCommand(interpreter);
    if (log) {
      if (success)
        log->Printf("StructuredDataDarwinLog::%s() ran enable command "
                    "successfully for (process uid %u)",
                    __FUNCTION__, process_sp->GetUniqueID());
      else
        log->Printf("StructuredDataDarwinLog::%s() error: running "
                    "enable command failed (process uid %u)",
                    __FUNCTION__, process_sp->GetUniqueID());
    }
    // Report failures to the debugger error stream.
    auto error_stream_sp = debugger_sp->GetAsyncErrorStream();
    if (error_stream_sp) {
      error_stream_sp->Printf("failed to configure DarwinLog "
                              "support\n");
      error_stream_sp->Flush();
    }
    return;
  }

  // We've previously been enabled. We will re-enable now with the previously
  // specified options.
  auto config_sp = options_sp->BuildConfigurationData(true);
  if (!config_sp) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() warning: failed to "
                  "build configuration data for enable options, skipping "
                  "(process uid %u)",
                  __FUNCTION__, process_sp->GetUniqueID());
    return;
  }

  // We can run it directly.
  // Send configuration to the feature by way of the process.
  const Status error =
      process_sp->ConfigureStructuredData(GetDarwinLogTypeName(), config_sp);

  // Report results.
  if (!error.Success()) {
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() "
                  "ConfigureStructuredData() call failed "
                  "(process uid %u): %s",
                  __FUNCTION__, process_sp->GetUniqueID(), error.AsCString());
    auto error_stream_sp = debugger_sp->GetAsyncErrorStream();
    if (error_stream_sp) {
      error_stream_sp->Printf("failed to configure DarwinLog "
                              "support: %s\n",
                              error.AsCString());
      error_stream_sp->Flush();
    }
    m_is_enabled = false;
  } else {
    m_is_enabled = true;
    if (log)
      log->Printf("StructuredDataDarwinLog::%s() success via direct "
                  "configuration (process uid %u)",
                  __FUNCTION__, process_sp->GetUniqueID());
  }
}
