//===-- Breakpoint.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Breakpoint_h_
#define liblldb_Breakpoint_h_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointLocationCollection.h"
#include "lldb/Breakpoint/BreakpointLocationList.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Breakpoint/Stoppoint.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Breakpoint Breakpoint.h "lldb/Breakpoint/Breakpoint.h" Class that
/// manages logical breakpoint setting.
//----------------------------------------------------------------------

//----------------------------------------------------------------------
/// General Outline:
/// A breakpoint has four main parts, a filter, a resolver, the list of
/// breakpoint
/// locations that have been determined for the filter/resolver pair, and
/// finally a set of options for the breakpoint.
///
/// \b Filter:
/// This is an object derived from SearchFilter.  It manages the search for
/// breakpoint location matches through the symbols in the module list of the
/// target that owns it.  It also filters out locations based on whatever
/// logic it wants.
///
/// \b Resolver:
/// This is an object derived from BreakpointResolver.  It provides a callback
/// to the filter that will find breakpoint locations.  How it does this is
/// determined by what kind of resolver it is.
///
/// The Breakpoint class also provides constructors for the common breakpoint
/// cases which make the appropriate filter and resolver for you.
///
/// \b Location List:
/// This stores the breakpoint locations that have been determined to date.
/// For a given breakpoint, there will be only one location with a given
/// address.  Adding a location at an already taken address will just return
/// the location already at that address.  Locations can be looked up by ID,
/// or by address.
///
/// \b Options:
/// This includes:
///    \b Enabled/Disabled
///    \b Ignore Count
///    \b Callback
///    \b Condition
/// Note, these options can be set on the breakpoint, and they can also be set
/// on the individual locations.  The options set on the breakpoint take
/// precedence over the options set on the individual location. So for
/// instance disabling the breakpoint will cause NONE of the locations to get
/// hit. But if the breakpoint is enabled, then the location's enabled state
/// will be checked to determine whether to insert that breakpoint location.
/// Similarly, if the breakpoint condition says "stop", we won't check the
/// location's condition. But if the breakpoint condition says "continue",
/// then we will check the location for whether to actually stop or not. One
/// subtle point worth observing here is that you don't actually stop at a
/// Breakpoint, you always stop at one of its locations.  So the "should stop"
/// tests are done by the location, not by the breakpoint.
//----------------------------------------------------------------------
class Breakpoint : public std::enable_shared_from_this<Breakpoint>,
                   public Stoppoint {
public:
  static const ConstString &GetEventIdentifier();

  //------------------------------------------------------------------
  /// An enum specifying the match style for breakpoint settings.  At present
  /// only used for function name style breakpoints.
  //------------------------------------------------------------------
  typedef enum { Exact, Regexp, Glob } MatchType;

private:
  enum class OptionNames : uint32_t { Names = 0, Hardware, LastOptionName };

  static const char
      *g_option_names[static_cast<uint32_t>(OptionNames::LastOptionName)];

  static const char *GetKey(OptionNames enum_value) {
    return g_option_names[static_cast<uint32_t>(enum_value)];
  }

public:
  class BreakpointEventData : public EventData {
  public:
    BreakpointEventData(lldb::BreakpointEventType sub_type,
                        const lldb::BreakpointSP &new_breakpoint_sp);

    ~BreakpointEventData() override;

    static const ConstString &GetFlavorString();

    const ConstString &GetFlavor() const override;

    lldb::BreakpointEventType GetBreakpointEventType() const;

    lldb::BreakpointSP &GetBreakpoint();

    BreakpointLocationCollection &GetBreakpointLocationCollection() {
      return m_locations;
    }

    void Dump(Stream *s) const override;

    static lldb::BreakpointEventType
    GetBreakpointEventTypeFromEvent(const lldb::EventSP &event_sp);

    static lldb::BreakpointSP
    GetBreakpointFromEvent(const lldb::EventSP &event_sp);

    static lldb::BreakpointLocationSP
    GetBreakpointLocationAtIndexFromEvent(const lldb::EventSP &event_sp,
                                          uint32_t loc_idx);

    static size_t
    GetNumBreakpointLocationsFromEvent(const lldb::EventSP &event_sp);

    static const BreakpointEventData *
    GetEventDataFromEvent(const Event *event_sp);

  private:
    lldb::BreakpointEventType m_breakpoint_event;
    lldb::BreakpointSP m_new_breakpoint_sp;
    BreakpointLocationCollection m_locations;

    DISALLOW_COPY_AND_ASSIGN(BreakpointEventData);
  };

  class BreakpointPrecondition {
  public:
    virtual ~BreakpointPrecondition() = default;

    virtual bool EvaluatePrecondition(StoppointCallbackContext &context);

    virtual Status ConfigurePrecondition(Args &options);

    virtual void GetDescription(Stream &stream, lldb::DescriptionLevel level);
  };

  typedef std::shared_ptr<BreakpointPrecondition> BreakpointPreconditionSP;

  // Saving & restoring breakpoints:
  static lldb::BreakpointSP CreateFromStructuredData(
      Target &target, StructuredData::ObjectSP &data_object_sp, Status &error);

  static bool
  SerializedBreakpointMatchesNames(StructuredData::ObjectSP &bkpt_object_sp,
                                   std::vector<std::string> &names);

  virtual StructuredData::ObjectSP SerializeToStructuredData();

  static const char *GetSerializationKey() { return "Breakpoint"; }
  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is not virtual since there should be no reason to
  /// subclass breakpoints.  The varieties of breakpoints are specified
  /// instead by providing different resolvers & filters.
  //------------------------------------------------------------------
  ~Breakpoint() override;

  //------------------------------------------------------------------
  // Methods
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Tell whether this breakpoint is an "internal" breakpoint. @return
  ///     Returns \b true if this is an internal breakpoint, \b false otherwise.
  //------------------------------------------------------------------
  bool IsInternal() const;

  //------------------------------------------------------------------
  /// Standard "Dump" method.  At present it does nothing.
  //------------------------------------------------------------------
  void Dump(Stream *s) override;

  //------------------------------------------------------------------
  // The next set of methods provide ways to tell the breakpoint to update it's
  // location list - usually done when modules appear or disappear.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Tell this breakpoint to clear all its breakpoint sites.  Done when the
  /// process holding the breakpoint sites is destroyed.
  //------------------------------------------------------------------
  void ClearAllBreakpointSites();

  //------------------------------------------------------------------
  /// Tell this breakpoint to scan it's target's module list and resolve any
  /// new locations that match the breakpoint's specifications.
  //------------------------------------------------------------------
  void ResolveBreakpoint();

  //------------------------------------------------------------------
  /// Tell this breakpoint to scan a given module list and resolve any new
  /// locations that match the breakpoint's specifications.
  ///
  /// @param[in] module_list
  ///    The list of modules to look in for new locations.
  ///
  /// @param[in]  send_event
  ///     If \b true, send a breakpoint location added event for non-internal
  ///     breakpoints.
  //------------------------------------------------------------------
  void ResolveBreakpointInModules(ModuleList &module_list,
                                  bool send_event = true);

  //------------------------------------------------------------------
  /// Tell this breakpoint to scan a given module list and resolve any new
  /// locations that match the breakpoint's specifications.
  ///
  /// @param[in] changed_modules
  ///    The list of modules to look in for new locations.
  ///
  /// @param[in]  new_locations
  ///     Fills new_locations with the new locations that were made.
  //------------------------------------------------------------------
  void ResolveBreakpointInModules(ModuleList &module_list,
                                  BreakpointLocationCollection &new_locations);

  //------------------------------------------------------------------
  /// Like ResolveBreakpointInModules, but allows for "unload" events, in
  /// which case we will remove any locations that are in modules that got
  /// unloaded.
  ///
  /// @param[in] changedModules
  ///    The list of modules to look in for new locations.
  /// @param[in] load_event
  ///    If \b true then the modules were loaded, if \b false, unloaded.
  /// @param[in] delete_locations
  ///    If \b true then the modules were unloaded delete any locations in the
  ///    changed modules.
  //------------------------------------------------------------------
  void ModulesChanged(ModuleList &changed_modules, bool load_event,
                      bool delete_locations = false);

  //------------------------------------------------------------------
  /// Tells the breakpoint the old module \a old_module_sp has been replaced
  /// by new_module_sp (usually because the underlying file has been rebuilt,
  /// and the old version is gone.)
  ///
  /// @param[in] old_module_sp
  ///    The old module that is going away.
  /// @param[in] new_module_sp
  ///    The new module that is replacing it.
  //------------------------------------------------------------------
  void ModuleReplaced(lldb::ModuleSP old_module_sp,
                      lldb::ModuleSP new_module_sp);

  //------------------------------------------------------------------
  // The next set of methods provide access to the breakpoint locations for
  // this breakpoint.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Add a location to the breakpoint's location list.  This is only meant to
  /// be called by the breakpoint's resolver.  FIXME: how do I ensure that?
  ///
  /// @param[in] addr
  ///    The Address specifying the new location.
  /// @param[out] new_location
  ///    Set to \b true if a new location was created, to \b false if there
  ///    already was a location at this Address.
  /// @return
  ///    Returns a pointer to the new location.
  //------------------------------------------------------------------
  lldb::BreakpointLocationSP AddLocation(const Address &addr,
                                         bool *new_location = nullptr);

  //------------------------------------------------------------------
  /// Find a breakpoint location by Address.
  ///
  /// @param[in] addr
  ///    The Address specifying the location.
  /// @return
  ///    Returns a shared pointer to the location at \a addr.  The pointer
  ///    in the shared pointer will be nullptr if there is no location at that
  ///    address.
  //------------------------------------------------------------------
  lldb::BreakpointLocationSP FindLocationByAddress(const Address &addr);

  //------------------------------------------------------------------
  /// Find a breakpoint location ID by Address.
  ///
  /// @param[in] addr
  ///    The Address specifying the location.
  /// @return
  ///    Returns the UID of the location at \a addr, or \b LLDB_INVALID_ID if
  ///    there is no breakpoint location at that address.
  //------------------------------------------------------------------
  lldb::break_id_t FindLocationIDByAddress(const Address &addr);

  //------------------------------------------------------------------
  /// Find a breakpoint location for a given breakpoint location ID.
  ///
  /// @param[in] bp_loc_id
  ///    The ID specifying the location.
  /// @return
  ///    Returns a shared pointer to the location with ID \a bp_loc_id.  The
  ///    pointer
  ///    in the shared pointer will be nullptr if there is no location with that
  ///    ID.
  //------------------------------------------------------------------
  lldb::BreakpointLocationSP FindLocationByID(lldb::break_id_t bp_loc_id);

  //------------------------------------------------------------------
  /// Get breakpoint locations by index.
  ///
  /// @param[in] index
  ///    The location index.
  ///
  /// @return
  ///     Returns a shared pointer to the location with index \a
  ///     index. The shared pointer might contain nullptr if \a index is
  ///     greater than then number of actual locations.
  //------------------------------------------------------------------
  lldb::BreakpointLocationSP GetLocationAtIndex(size_t index);

  //------------------------------------------------------------------
  /// Removes all invalid breakpoint locations.
  ///
  /// Removes all breakpoint locations with architectures that aren't
  /// compatible with \a arch. Also remove any breakpoint locations with whose
  /// locations have address where the section has been deleted (module and
  /// object files no longer exist).
  ///
  /// This is typically used after the process calls exec, or anytime the
  /// architecture of the target changes.
  ///
  /// @param[in] arch
  ///     If valid, check the module in each breakpoint to make sure
  ///     they are compatible, otherwise, ignore architecture.
  //------------------------------------------------------------------
  void RemoveInvalidLocations(const ArchSpec &arch);

  //------------------------------------------------------------------
  // The next section deals with various breakpoint options.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// If \a enable is \b true, enable the breakpoint, if \b false disable it.
  //------------------------------------------------------------------
  void SetEnabled(bool enable) override;

  //------------------------------------------------------------------
  /// Check the Enable/Disable state.
  /// @return
  ///     \b true if the breakpoint is enabled, \b false if disabled.
  //------------------------------------------------------------------
  bool IsEnabled() override;

  //------------------------------------------------------------------
  /// Set the breakpoint to ignore the next \a count breakpoint hits.
  /// @param[in] count
  ///    The number of breakpoint hits to ignore.
  //------------------------------------------------------------------
  void SetIgnoreCount(uint32_t count);

  //------------------------------------------------------------------
  /// Return the current ignore count/
  /// @return
  ///     The number of breakpoint hits to be ignored.
  //------------------------------------------------------------------
  uint32_t GetIgnoreCount() const;

  //------------------------------------------------------------------
  /// Return the current hit count for all locations. @return
  ///     The current hit count for all locations.
  //------------------------------------------------------------------
  uint32_t GetHitCount() const;

  //------------------------------------------------------------------
  /// If \a one_shot is \b true, breakpoint will be deleted on first hit.
  //------------------------------------------------------------------
  void SetOneShot(bool one_shot);

  //------------------------------------------------------------------
  /// Check the OneShot state.
  /// @return
  ///     \b true if the breakpoint is one shot, \b false otherwise.
  //------------------------------------------------------------------
  bool IsOneShot() const;

  //------------------------------------------------------------------
  /// If \a auto_continue is \b true, breakpoint will auto-continue when on
  /// hit.
  //------------------------------------------------------------------
  void SetAutoContinue(bool auto_continue);

  //------------------------------------------------------------------
  /// Check the AutoContinue state.
  /// @return
  ///     \b true if the breakpoint is set to auto-continue, \b false otherwise.
  //------------------------------------------------------------------
  bool IsAutoContinue() const;

  //------------------------------------------------------------------
  /// Set the valid thread to be checked when the breakpoint is hit.
  /// @param[in] thread_id
  ///    If this thread hits the breakpoint, we stop, otherwise not.
  //------------------------------------------------------------------
  void SetThreadID(lldb::tid_t thread_id);

  //------------------------------------------------------------------
  /// Return the current stop thread value.
  /// @return
  ///     The thread id for which the breakpoint hit will stop,
  ///     LLDB_INVALID_THREAD_ID for all threads.
  //------------------------------------------------------------------
  lldb::tid_t GetThreadID() const;

  void SetThreadIndex(uint32_t index);

  uint32_t GetThreadIndex() const;

  void SetThreadName(const char *thread_name);

  const char *GetThreadName() const;

  void SetQueueName(const char *queue_name);

  const char *GetQueueName() const;

  //------------------------------------------------------------------
  /// Set the callback action invoked when the breakpoint is hit.
  ///
  /// @param[in] callback
  ///    The method that will get called when the breakpoint is hit.
  /// @param[in] baton
  ///    A void * pointer that will get passed back to the callback function.
  /// @param[in] is_synchronous
  ///    If \b true the callback will be run on the private event thread
  ///    before the stop event gets reported.  If false, the callback will get
  ///    handled on the public event thread after the stop has been posted.
  ///
  /// @return
  ///    \b true if the process should stop when you hit the breakpoint.
  ///    \b false if it should continue.
  //------------------------------------------------------------------
  void SetCallback(BreakpointHitCallback callback, void *baton,
                   bool is_synchronous = false);

  void SetCallback(BreakpointHitCallback callback,
                   const lldb::BatonSP &callback_baton_sp,
                   bool is_synchronous = false);

  void ClearCallback();

  //------------------------------------------------------------------
  /// Set the breakpoint's condition.
  ///
  /// @param[in] condition
  ///    The condition expression to evaluate when the breakpoint is hit.
  ///    Pass in nullptr to clear the condition.
  //------------------------------------------------------------------
  void SetCondition(const char *condition);

  //------------------------------------------------------------------
  /// Return a pointer to the text of the condition expression.
  ///
  /// @return
  ///    A pointer to the condition expression text, or nullptr if no
  //     condition has been set.
  //------------------------------------------------------------------
  const char *GetConditionText() const;

  //------------------------------------------------------------------
  // The next section are various utility functions.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Return the number of breakpoint locations that have resolved to actual
  /// breakpoint sites.
  ///
  /// @return
  ///     The number locations resolved breakpoint sites.
  //------------------------------------------------------------------
  size_t GetNumResolvedLocations() const;

  //------------------------------------------------------------------
  /// Return whether this breakpoint has any resolved locations.
  ///
  /// @return
  ///     True if GetNumResolvedLocations > 0
  //------------------------------------------------------------------
  bool HasResolvedLocations() const;

  //------------------------------------------------------------------
  /// Return the number of breakpoint locations.
  ///
  /// @return
  ///     The number breakpoint locations.
  //------------------------------------------------------------------
  size_t GetNumLocations() const;

  //------------------------------------------------------------------
  /// Put a description of this breakpoint into the stream \a s.
  ///
  /// @param[in] s
  ///     Stream into which to dump the description.
  ///
  /// @param[in] level
  ///     The description level that indicates the detail level to
  ///     provide.
  ///
  /// @see lldb::DescriptionLevel
  //------------------------------------------------------------------
  void GetDescription(Stream *s, lldb::DescriptionLevel level,
                      bool show_locations = false);

  //------------------------------------------------------------------
  /// Set the "kind" description for a breakpoint.  If the breakpoint is hit
  /// the stop info will show this "kind" description instead of the
  /// breakpoint number.  Mostly useful for internal breakpoints, where the
  /// breakpoint number doesn't have meaning to the user.
  ///
  /// @param[in] kind
  ///     New "kind" description.
  //------------------------------------------------------------------
  void SetBreakpointKind(const char *kind) { m_kind_description.assign(kind); }

  //------------------------------------------------------------------
  /// Return the "kind" description for a breakpoint.
  ///
  /// @return
  ///     The breakpoint kind, or nullptr if none is set.
  //------------------------------------------------------------------
  const char *GetBreakpointKind() const { return m_kind_description.c_str(); }

  //------------------------------------------------------------------
  /// Accessor for the breakpoint Target.
  /// @return
  ///     This breakpoint's Target.
  //------------------------------------------------------------------
  Target &GetTarget() { return m_target; }

  const Target &GetTarget() const { return m_target; }

  const lldb::TargetSP GetTargetSP();

  void GetResolverDescription(Stream *s);

  //------------------------------------------------------------------
  /// Find breakpoint locations which match the (filename, line_number)
  /// description. The breakpoint location collection is to be filled with the
  /// matching locations. It should be initialized with 0 size by the API
  /// client.
  ///
  /// @return
  ///     True if there is a match
  ///
  ///     The locations which match the filename and line_number in loc_coll.
  ///     If its
  ///     size is 0 and true is returned, it means the breakpoint fully matches
  ///     the
  ///     description.
  //------------------------------------------------------------------
  bool GetMatchingFileLine(const ConstString &filename, uint32_t line_number,
                           BreakpointLocationCollection &loc_coll);

  void GetFilterDescription(Stream *s);

  //------------------------------------------------------------------
  /// Returns the BreakpointOptions structure set at the breakpoint level.
  ///
  /// Meant to be used by the BreakpointLocation class.
  ///
  /// @return
  ///     A pointer to this breakpoint's BreakpointOptions.
  //------------------------------------------------------------------
  BreakpointOptions *GetOptions();

  //------------------------------------------------------------------
  /// Returns the BreakpointOptions structure set at the breakpoint level.
  ///
  /// Meant to be used by the BreakpointLocation class.
  ///
  /// @return
  ///     A pointer to this breakpoint's BreakpointOptions.
  //------------------------------------------------------------------
  const BreakpointOptions *GetOptions() const;

  //------------------------------------------------------------------
  /// Invoke the callback action when the breakpoint is hit.
  ///
  /// Meant to be used by the BreakpointLocation class.
  ///
  /// @param[in] context
  ///     Described the breakpoint event.
  ///
  /// @param[in] bp_loc_id
  ///     Which breakpoint location hit this breakpoint.
  ///
  /// @return
  ///     \b true if the target should stop at this breakpoint and \b false not.
  //------------------------------------------------------------------
  bool InvokeCallback(StoppointCallbackContext *context,
                      lldb::break_id_t bp_loc_id);

  bool IsHardware() const { return m_hardware; }

  lldb::BreakpointResolverSP GetResolver() { return m_resolver_sp; }

  lldb::SearchFilterSP GetSearchFilter() { return m_filter_sp; }

private: // The target needs to manage adding & removing names.  It will do the
         // checking for name validity as well.
  bool AddName(llvm::StringRef new_name);

  void RemoveName(const char *name_to_remove) {
    if (name_to_remove)
      m_name_list.erase(name_to_remove);
  }
  
public:
  bool MatchesName(const char *name) {
    return m_name_list.find(name) != m_name_list.end();
  }

  void GetNames(std::vector<std::string> &names) {
    names.clear();
    for (auto name : m_name_list) {
      names.push_back(name);
    }
  }

  //------------------------------------------------------------------
  /// Set a pre-condition filter that overrides all user provided
  /// filters/callbacks etc.
  ///
  /// Used to define fancy breakpoints that can do dynamic hit detection
  /// without taking up the condition slot - which really belongs to the user
  /// anyway...
  ///
  /// The Precondition should not continue the target, it should return true
  /// if the condition says to stop and false otherwise.
  ///
  //------------------------------------------------------------------
  void SetPrecondition(BreakpointPreconditionSP precondition_sp) {
    m_precondition_sp = precondition_sp;
  }

  bool EvaluatePrecondition(StoppointCallbackContext &context);

  BreakpointPreconditionSP GetPrecondition() { return m_precondition_sp; }
  
  // Produces the OR'ed values for all the names assigned to this breakpoint.
  const BreakpointName::Permissions &GetPermissions() const { 
      return m_permissions; 
  }

  BreakpointName::Permissions &GetPermissions() { 
      return m_permissions; 
  }
  
  bool AllowList() const {
    return GetPermissions().GetAllowList();
  }
  bool AllowDisable() const {
    return GetPermissions().GetAllowDisable();
  }
  bool AllowDelete() const {
    return GetPermissions().GetAllowDelete();
  }

protected:
  friend class Target;
  //------------------------------------------------------------------
  // Protected Methods
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Constructors and Destructors
  /// Only the Target can make a breakpoint, and it owns the breakpoint
  /// lifespans. The constructor takes a filter and a resolver.  Up in Target
  /// there are convenience variants that make breakpoints for some common
  /// cases.
  ///
  /// @param[in] target
  ///    The target in which the breakpoint will be set.
  ///
  /// @param[in] filter_sp
  ///    Shared pointer to the search filter that restricts the search domain of
  ///    the breakpoint.
  ///
  /// @param[in] resolver_sp
  ///    Shared pointer to the resolver object that will determine breakpoint
  ///    matches.
  ///
  /// @param hardware
  ///    If true, request a hardware breakpoint to be used to implement the
  ///    breakpoint locations.
  ///
  /// @param resolve_indirect_symbols
  ///    If true, and the address of a given breakpoint location in this
  ///    breakpoint is set on an
  ///    indirect symbol (i.e. Symbol::IsIndirect returns true) then the actual
  ///    breakpoint site will
  ///    be set on the target of the indirect symbol.
  //------------------------------------------------------------------
  // This is the generic constructor
  Breakpoint(Target &target, lldb::SearchFilterSP &filter_sp,
             lldb::BreakpointResolverSP &resolver_sp, bool hardware,
             bool resolve_indirect_symbols = true);

  friend class BreakpointLocation; // To call the following two when determining
                                   // whether to stop.

  void DecrementIgnoreCount();

  // BreakpointLocation::IgnoreCountShouldStop &
  // Breakpoint::IgnoreCountShouldStop can only be called once per stop, and
  // BreakpointLocation::IgnoreCountShouldStop should be tested first, and if
  // it returns false we should continue, otherwise we should test
  // Breakpoint::IgnoreCountShouldStop.

  bool IgnoreCountShouldStop();

  void IncrementHitCount() { m_hit_count++; }

  void DecrementHitCount() {
    assert(m_hit_count > 0);
    m_hit_count--;
  }

private:
  // This one should only be used by Target to copy breakpoints from target to
  // target - primarily from the dummy target to prime new targets.
  Breakpoint(Target &new_target, Breakpoint &bp_to_copy_from);

  //------------------------------------------------------------------
  // For Breakpoint only
  //------------------------------------------------------------------
  bool m_being_created;
  bool
      m_hardware; // If this breakpoint is required to use a hardware breakpoint
  Target &m_target; // The target that holds this breakpoint.
  std::unordered_set<std::string> m_name_list; // If not empty, this is the name
                                               // of this breakpoint (many
                                               // breakpoints can share the same
                                               // name.)
  lldb::SearchFilterSP
      m_filter_sp; // The filter that constrains the breakpoint's domain.
  lldb::BreakpointResolverSP
      m_resolver_sp; // The resolver that defines this breakpoint.
  BreakpointPreconditionSP m_precondition_sp; // The precondition is a
                                              // breakpoint-level hit filter
                                              // that can be used
  // to skip certain breakpoint hits.  For instance, exception breakpoints use
  // this to limit the stop to certain exception classes, while leaving the
  // condition & callback free for user specification.
  std::unique_ptr<BreakpointOptions>
      m_options_up; // Settable breakpoint options
  BreakpointLocationList
      m_locations; // The list of locations currently found for this breakpoint.
  std::string m_kind_description;
  bool m_resolve_indirect_symbols;
  uint32_t m_hit_count; // Number of times this breakpoint/watchpoint has been
                        // hit.  This is kept
  // separately from the locations hit counts, since locations can go away when
  // their backing library gets unloaded, and we would lose hit counts.
  BreakpointName::Permissions m_permissions;

  void SendBreakpointChangedEvent(lldb::BreakpointEventType eventKind);

  void SendBreakpointChangedEvent(BreakpointEventData *data);

  DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

} // namespace lldb_private

#endif // liblldb_Breakpoint_h_
