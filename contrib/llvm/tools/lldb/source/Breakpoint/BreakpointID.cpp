//===-- BreakpointID.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

BreakpointID::BreakpointID(break_id_t bp_id, break_id_t loc_id)
    : m_break_id(bp_id), m_location_id(loc_id) {}

BreakpointID::~BreakpointID() = default;

static llvm::StringRef g_range_specifiers[] = {"-", "to", "To", "TO"};

// Tells whether or not STR is valid to use between two strings representing
// breakpoint IDs, to indicate a range of breakpoint IDs.  This is broken out
// into a separate function so that we can easily change or add to the format
// for specifying ID ranges at a later date.

bool BreakpointID::IsRangeIdentifier(llvm::StringRef str) {
  for (auto spec : g_range_specifiers) {
    if (spec == str)
      return true;
  }

  return false;
}

bool BreakpointID::IsValidIDExpression(llvm::StringRef str) {
  return BreakpointID::ParseCanonicalReference(str).hasValue();
}

llvm::ArrayRef<llvm::StringRef> BreakpointID::GetRangeSpecifiers() {
  return llvm::makeArrayRef(g_range_specifiers);
}

void BreakpointID::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  if (level == eDescriptionLevelVerbose)
    s->Printf("%p BreakpointID:", static_cast<void *>(this));

  if (m_break_id == LLDB_INVALID_BREAK_ID)
    s->PutCString("<invalid>");
  else if (m_location_id == LLDB_INVALID_BREAK_ID)
    s->Printf("%i", m_break_id);
  else
    s->Printf("%i.%i", m_break_id, m_location_id);
}

void BreakpointID::GetCanonicalReference(Stream *s, break_id_t bp_id,
                                         break_id_t loc_id) {
  if (bp_id == LLDB_INVALID_BREAK_ID)
    s->PutCString("<invalid>");
  else if (loc_id == LLDB_INVALID_BREAK_ID)
    s->Printf("%i", bp_id);
  else
    s->Printf("%i.%i", bp_id, loc_id);
}

llvm::Optional<BreakpointID>
BreakpointID::ParseCanonicalReference(llvm::StringRef input) {
  break_id_t bp_id;
  break_id_t loc_id = LLDB_INVALID_BREAK_ID;

  if (input.empty())
    return llvm::None;

  // If it doesn't start with an integer, it's not valid.
  if (input.consumeInteger(0, bp_id))
    return llvm::None;

  // period is optional, but if it exists, it must be followed by a number.
  if (input.consume_front(".")) {
    if (input.consumeInteger(0, loc_id))
      return llvm::None;
  }

  // And at the end, the entire string must have been consumed.
  if (!input.empty())
    return llvm::None;

  return BreakpointID(bp_id, loc_id);
}

bool BreakpointID::StringIsBreakpointName(llvm::StringRef str, Status &error) {
  error.Clear();
  if (str.empty())
  {
    error.SetErrorStringWithFormat("Empty breakpoint names are not allowed");
    return false;
  }

  // First character must be a letter or _
  if (!isalpha(str[0]) && str[0] != '_')
  {
    error.SetErrorStringWithFormat("Breakpoint names must start with a "
                                   "character or underscore: %s",
                                   str.str().c_str());
    return false;
  }

  // Cannot contain ., -, or space.
  if (str.find_first_of(".- ") != llvm::StringRef::npos) {
    error.SetErrorStringWithFormat("Breakpoint names cannot contain "
                                   "'.' or '-': \"%s\"",
                                   str.str().c_str());
    return false;
  }

  return true;
}
