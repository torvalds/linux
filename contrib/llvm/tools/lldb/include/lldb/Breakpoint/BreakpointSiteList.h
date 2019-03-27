//===-- BreakpointSiteList.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointSiteList_h_
#define liblldb_BreakpointSiteList_h_

#include <functional>
#include <map>
#include <mutex>

#include "lldb/Breakpoint/BreakpointSite.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointSiteList BreakpointSiteList.h
/// "lldb/Breakpoint/BreakpointSiteList.h" Class that manages lists of
/// BreakpointSite shared pointers.
//----------------------------------------------------------------------
class BreakpointSiteList {
  // At present Process directly accesses the map of BreakpointSites so it can
  // do quick lookups into the map (using GetMap).
  // FIXME: Find a better interface for this.
  friend class Process;

public:
  //------------------------------------------------------------------
  /// Default constructor makes an empty list.
  //------------------------------------------------------------------
  BreakpointSiteList();

  //------------------------------------------------------------------
  /// Destructor, currently does nothing.
  //------------------------------------------------------------------
  ~BreakpointSiteList();

  //------------------------------------------------------------------
  /// Add a BreakpointSite to the list.
  ///
  /// @param[in] bp_site_sp
  ///    A shared pointer to a breakpoint site being added to the list.
  ///
  /// @return
  ///    The ID of the BreakpointSite in the list.
  //------------------------------------------------------------------
  lldb::break_id_t Add(const lldb::BreakpointSiteSP &bp_site_sp);

  //------------------------------------------------------------------
  /// Standard Dump routine, doesn't do anything at present. @param[in] s
  ///     Stream into which to dump the description.
  //------------------------------------------------------------------
  void Dump(Stream *s) const;

  //------------------------------------------------------------------
  /// Returns a shared pointer to the breakpoint site at address \a addr.
  ///
  /// @param[in] addr
  ///     The address to look for.
  ///
  /// @result
  ///     A shared pointer to the breakpoint site. May contain a NULL
  ///     pointer if no breakpoint site exists with a matching address.
  //------------------------------------------------------------------
  lldb::BreakpointSiteSP FindByAddress(lldb::addr_t addr);

  //------------------------------------------------------------------
  /// Returns a shared pointer to the breakpoint site with id \a breakID.
  ///
  /// @param[in] breakID
  ///   The breakpoint site ID to seek for.
  ///
  /// @result
  ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if
  ///   the
  ///   breakpoint doesn't exist.
  //------------------------------------------------------------------
  lldb::BreakpointSiteSP FindByID(lldb::break_id_t breakID);

  //------------------------------------------------------------------
  /// Returns a shared pointer to the breakpoint site with id \a breakID -
  /// const version.
  ///
  /// @param[in] breakID
  ///   The breakpoint site ID to seek for.
  ///
  /// @result
  ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if
  ///   the
  ///   breakpoint doesn't exist.
  //------------------------------------------------------------------
  const lldb::BreakpointSiteSP FindByID(lldb::break_id_t breakID) const;

  //------------------------------------------------------------------
  /// Returns the breakpoint site id to the breakpoint site at address \a
  /// addr.
  ///
  /// @param[in] addr
  ///   The address to match.
  ///
  /// @result
  ///   The ID of the breakpoint site, or LLDB_INVALID_BREAK_ID.
  //------------------------------------------------------------------
  lldb::break_id_t FindIDByAddress(lldb::addr_t addr);

  //------------------------------------------------------------------
  /// Returns whether the breakpoint site \a bp_site_id has \a bp_id
  //  as one of its owners.
  ///
  /// @param[in] bp_site_id
  ///   The breakpoint site id to query.
  ///
  /// @param[in] bp_id
  ///   The breakpoint id to look for in \a bp_site_id.
  ///
  /// @result
  ///   True if \a bp_site_id exists in the site list AND \a bp_id is one of the
  ///   owners of that site.
  //------------------------------------------------------------------
  bool BreakpointSiteContainsBreakpoint(lldb::break_id_t bp_site_id,
                                        lldb::break_id_t bp_id);

  void ForEach(std::function<void(BreakpointSite *)> const &callback);

  //------------------------------------------------------------------
  /// Removes the breakpoint site given by \b breakID from this list.
  ///
  /// @param[in] breakID
  ///   The breakpoint site index to remove.
  ///
  /// @result
  ///   \b true if the breakpoint site \a breakID was in the list.
  //------------------------------------------------------------------
  bool Remove(lldb::break_id_t breakID);

  //------------------------------------------------------------------
  /// Removes the breakpoint site at address \a addr from this list.
  ///
  /// @param[in] addr
  ///   The address from which to remove a breakpoint site.
  ///
  /// @result
  ///   \b true if \a addr had a breakpoint site to remove from the list.
  //------------------------------------------------------------------
  bool RemoveByAddress(lldb::addr_t addr);

  bool FindInRange(lldb::addr_t lower_bound, lldb::addr_t upper_bound,
                   BreakpointSiteList &bp_site_list) const;

  typedef void (*BreakpointSiteSPMapFunc)(lldb::BreakpointSiteSP &bp,
                                          void *baton);

  //------------------------------------------------------------------
  /// Enquires of the breakpoint site on in this list with ID \a breakID
  /// whether we should stop for the breakpoint or not.
  ///
  /// @param[in] context
  ///    This contains the information about this stop.
  ///
  /// @param[in] breakID
  ///    This break ID that we hit.
  ///
  /// @return
  ///    \b true if we should stop, \b false otherwise.
  //------------------------------------------------------------------
  bool ShouldStop(StoppointCallbackContext *context, lldb::break_id_t breakID);

  //------------------------------------------------------------------
  /// Returns the number of elements in the list.
  ///
  /// @result
  ///   The number of elements.
  //------------------------------------------------------------------
  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_bp_site_list.size();
  }

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_bp_site_list.empty();
  }

protected:
  typedef std::map<lldb::addr_t, lldb::BreakpointSiteSP> collection;

  collection::iterator GetIDIterator(lldb::break_id_t breakID);

  collection::const_iterator GetIDConstIterator(lldb::break_id_t breakID) const;

  mutable std::recursive_mutex m_mutex;
  collection m_bp_site_list; // The breakpoint site list.
};

} // namespace lldb_private

#endif // liblldb_BreakpointSiteList_h_
