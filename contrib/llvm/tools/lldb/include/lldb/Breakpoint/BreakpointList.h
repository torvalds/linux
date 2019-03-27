//===-- BreakpointList.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointList_h_
#define liblldb_BreakpointList_h_

#include <list>
#include <mutex>

#include "lldb/Breakpoint/Breakpoint.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointList BreakpointList.h "lldb/Breakpoint/BreakpointList.h"
/// This class manages a list of breakpoints.
//----------------------------------------------------------------------

//----------------------------------------------------------------------
/// General Outline:
/// Allows adding and removing breakpoints and find by ID and index.
//----------------------------------------------------------------------

class BreakpointList {
public:
  BreakpointList(bool is_internal);

  ~BreakpointList();

  //------------------------------------------------------------------
  /// Add the breakpoint \a bp_sp to the list.
  ///
  /// @param[in] bp_sp
  ///   Shared pointer to the breakpoint that will get added to the list.
  ///
  /// @result
  ///   Returns breakpoint id.
  //------------------------------------------------------------------
  lldb::break_id_t Add(lldb::BreakpointSP &bp_sp, bool notify);

  //------------------------------------------------------------------
  /// Standard "Dump" method.  At present it does nothing.
  //------------------------------------------------------------------
  void Dump(Stream *s) const;

  //------------------------------------------------------------------
  /// Returns a shared pointer to the breakpoint with id \a breakID.  Const
  /// version.
  ///
  /// @param[in] breakID
  ///   The breakpoint ID to seek for.
  ///
  /// @result
  ///   A shared pointer to the breakpoint.  May contain a NULL pointer if the
  ///   breakpoint doesn't exist.
  //------------------------------------------------------------------
  lldb::BreakpointSP FindBreakpointByID(lldb::break_id_t breakID) const;

  //------------------------------------------------------------------
  /// Returns a shared pointer to the breakpoint with index \a i.
  ///
  /// @param[in] i
  ///   The breakpoint index to seek for.
  ///
  /// @result
  ///   A shared pointer to the breakpoint.  May contain a NULL pointer if the
  ///   breakpoint doesn't exist.
  //------------------------------------------------------------------
  lldb::BreakpointSP GetBreakpointAtIndex(size_t i) const;

  //------------------------------------------------------------------
  /// Find all the breakpoints with a given name
  ///
  /// @param[in] name
  ///   The breakpoint name for which to search.
  ///
  /// @result
  ///   \bfalse if the input name was not a legal breakpoint name.
  //------------------------------------------------------------------
  bool FindBreakpointsByName(const char *name, BreakpointList &matching_bps);

  //------------------------------------------------------------------
  /// Returns the number of elements in this breakpoint list.
  ///
  /// @result
  ///   The number of elements.
  //------------------------------------------------------------------
  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_breakpoints.size();
  }

  //------------------------------------------------------------------
  /// Removes the breakpoint given by \b breakID from this list.
  ///
  /// @param[in] breakID
  ///   The breakpoint index to remove.
  ///
  /// @result
  ///   \b true if the breakpoint \a breakID was in the list.
  //------------------------------------------------------------------
  bool Remove(lldb::break_id_t breakID, bool notify);

  //------------------------------------------------------------------
  /// Removes all invalid breakpoint locations.
  ///
  /// Removes all breakpoint locations in the list with architectures that
  /// aren't compatible with \a arch. Also remove any breakpoint locations
  /// with whose locations have address where the section has been deleted
  /// (module and object files no longer exist).
  ///
  /// This is typically used after the process calls exec, or anytime the
  /// architecture of the target changes.
  ///
  /// @param[in] arch
  ///     If valid, check the module in each breakpoint to make sure
  ///     they are compatible, otherwise, ignore architecture.
  //------------------------------------------------------------------
  void RemoveInvalidLocations(const ArchSpec &arch);

  void SetEnabledAll(bool enabled);

  void SetEnabledAllowed(bool enabled);

  //------------------------------------------------------------------
  /// Removes all the breakpoints from this list.
  //------------------------------------------------------------------
  void RemoveAll(bool notify);

  //------------------------------------------------------------------
  /// Removes all the breakpoints from this list - first checking the
  /// ePermDelete on the breakpoints.  This call should be used unless you are
  /// shutting down and need to actually clear them all.
  //------------------------------------------------------------------
  void RemoveAllowed(bool notify);

  //------------------------------------------------------------------
  /// Tell all the breakpoints to update themselves due to a change in the
  /// modules in \a module_list.  \a added says whether the module was loaded
  /// or unloaded.
  ///
  /// @param[in] module_list
  ///   The module list that has changed.
  ///
  /// @param[in] load
  ///   \b true if the modules are loaded, \b false if unloaded.
  ///
  /// @param[in] delete_locations
  ///   If \a load is \b false, then delete breakpoint locations when
  ///   when updating breakpoints.
  //------------------------------------------------------------------
  void UpdateBreakpoints(ModuleList &module_list, bool load,
                         bool delete_locations);

  void UpdateBreakpointsWhenModuleIsReplaced(lldb::ModuleSP old_module_sp,
                                             lldb::ModuleSP new_module_sp);

  void ClearAllBreakpointSites();

  //------------------------------------------------------------------
  /// Sets the passed in Locker to hold the Breakpoint List mutex.
  ///
  /// @param[in] locker
  ///   The locker object that is set.
  //------------------------------------------------------------------
  void GetListMutex(std::unique_lock<std::recursive_mutex> &lock);

protected:
  typedef std::vector<lldb::BreakpointSP> bp_collection;

  bp_collection::iterator GetBreakpointIDIterator(lldb::break_id_t breakID);

  bp_collection::const_iterator
  GetBreakpointIDConstIterator(lldb::break_id_t breakID) const;

  std::recursive_mutex &GetMutex() const { return m_mutex; }

  mutable std::recursive_mutex m_mutex;
  bp_collection m_breakpoints;
  lldb::break_id_t m_next_break_id;
  bool m_is_internal;

public:
  typedef LockingAdaptedIterable<bp_collection, lldb::BreakpointSP,
                                 list_adapter, std::recursive_mutex>
      BreakpointIterable;
  BreakpointIterable Breakpoints() {
    return BreakpointIterable(m_breakpoints, GetMutex());
  }

private:
  DISALLOW_COPY_AND_ASSIGN(BreakpointList);
};

} // namespace lldb_private

#endif // liblldb_BreakpointList_h_
