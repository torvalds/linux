//===-- BreakpointIDList.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointIDList_h_
#define liblldb_BreakpointIDList_h_

#include <utility>
#include <vector>


#include "lldb/lldb-enumerations.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
// class BreakpointIDList
//----------------------------------------------------------------------

class BreakpointIDList {
public:
  // TODO: Convert this class to StringRef.
  typedef std::vector<BreakpointID> BreakpointIDArray;

  BreakpointIDList();

  virtual ~BreakpointIDList();

  size_t GetSize() const;

  const BreakpointID &GetBreakpointIDAtIndex(size_t index) const;

  bool RemoveBreakpointIDAtIndex(size_t index);

  void Clear();

  bool AddBreakpointID(BreakpointID bp_id);

  bool AddBreakpointID(const char *bp_id);

  // TODO: This should take a const BreakpointID.
  bool FindBreakpointID(BreakpointID &bp_id, size_t *position) const;

  bool FindBreakpointID(const char *bp_id, size_t *position) const;

  void InsertStringArray(llvm::ArrayRef<const char *> string_array,
                         CommandReturnObject &result);

  // Returns a pair consisting of the beginning and end of a breakpoint
  // ID range expression.  If the input string is not a valid specification,
  // returns an empty pair.
  static std::pair<llvm::StringRef, llvm::StringRef>
  SplitIDRangeExpression(llvm::StringRef in_string);

  static void FindAndReplaceIDRanges(Args &old_args, Target *target,
                                     bool allow_locations,
                                     BreakpointName::Permissions
                                       ::PermissionKinds purpose,
                                     CommandReturnObject &result,
                                     Args &new_args);

private:
  BreakpointIDArray m_breakpoint_ids;
  BreakpointID m_invalid_id;

  DISALLOW_COPY_AND_ASSIGN(BreakpointIDList);
};

} // namespace lldb_private

#endif // liblldb_BreakpointIDList_h_
