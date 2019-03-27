//===-- BreakpointResolver.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolver.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
// Have to include the other breakpoint resolver types here so the static
// create from StructuredData can call them.
#include "lldb/Breakpoint/BreakpointResolverAddress.h"
#include "lldb/Breakpoint/BreakpointResolverFileLine.h"
#include "lldb/Breakpoint/BreakpointResolverFileRegex.h"
#include "lldb/Breakpoint/BreakpointResolverName.h"
#include "lldb/Breakpoint/BreakpointResolverScripted.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;
using namespace lldb;

//----------------------------------------------------------------------
// BreakpointResolver:
//----------------------------------------------------------------------
const char *BreakpointResolver::g_ty_to_name[] = {"FileAndLine", "Address",
                                                  "SymbolName",  "SourceRegex",
                                                  "Exception",   "Unknown"};

const char *BreakpointResolver::g_option_names[static_cast<uint32_t>(
    BreakpointResolver::OptionNames::LastOptionName)] = {
    "AddressOffset", "Exact",     "FileName",     "Inlines",     "Language",
    "LineNumber",    "Column",    "ModuleName",   "NameMask",    "Offset",
    "PythonClass",   "Regex",     "ScriptArgs",   "SectionName", "SearchDepth",
    "SkipPrologue",  "SymbolNames"};

const char *BreakpointResolver::ResolverTyToName(enum ResolverTy type) {
  if (type > LastKnownResolverType)
    return g_ty_to_name[UnknownResolver];

  return g_ty_to_name[type];
}

BreakpointResolver::ResolverTy
BreakpointResolver::NameToResolverTy(llvm::StringRef name) {
  for (size_t i = 0; i < LastKnownResolverType; i++) {
    if (name == g_ty_to_name[i])
      return (ResolverTy)i;
  }
  return UnknownResolver;
}

BreakpointResolver::BreakpointResolver(Breakpoint *bkpt,
                                       const unsigned char resolverTy,
                                       lldb::addr_t offset)
    : m_breakpoint(bkpt), m_offset(offset), SubclassID(resolverTy) {}

BreakpointResolver::~BreakpointResolver() {}

BreakpointResolverSP BreakpointResolver::CreateFromStructuredData(
    const StructuredData::Dictionary &resolver_dict, Status &error) {
  BreakpointResolverSP result_sp;
  if (!resolver_dict.IsValid()) {
    error.SetErrorString("Can't deserialize from an invalid data object.");
    return result_sp;
  }

  llvm::StringRef subclass_name;

  bool success = resolver_dict.GetValueForKeyAsString(
      GetSerializationSubclassKey(), subclass_name);

  if (!success) {
    error.SetErrorStringWithFormat(
        "Resolver data missing subclass resolver key");
    return result_sp;
  }

  ResolverTy resolver_type = NameToResolverTy(subclass_name);
  if (resolver_type == UnknownResolver) {
    error.SetErrorStringWithFormatv("Unknown resolver type: {0}.",
                                    subclass_name);
    return result_sp;
  }

  StructuredData::Dictionary *subclass_options = nullptr;
  success = resolver_dict.GetValueForKeyAsDictionary(
      GetSerializationSubclassOptionsKey(), subclass_options);
  if (!success || !subclass_options || !subclass_options->IsValid()) {
    error.SetErrorString("Resolver data missing subclass options key.");
    return result_sp;
  }

  lldb::addr_t offset;
  success = subclass_options->GetValueForKeyAsInteger(
      GetKey(OptionNames::Offset), offset);
  if (!success) {
    error.SetErrorString("Resolver data missing offset options key.");
    return result_sp;
  }

  BreakpointResolver *resolver;

  switch (resolver_type) {
  case FileLineResolver:
    resolver = BreakpointResolverFileLine::CreateFromStructuredData(
        nullptr, *subclass_options, error);
    break;
  case AddressResolver:
    resolver = BreakpointResolverAddress::CreateFromStructuredData(
        nullptr, *subclass_options, error);
    break;
  case NameResolver:
    resolver = BreakpointResolverName::CreateFromStructuredData(
        nullptr, *subclass_options, error);
    break;
  case FileRegexResolver:
    resolver = BreakpointResolverFileRegex::CreateFromStructuredData(
        nullptr, *subclass_options, error);
    break;
  case PythonResolver:
    resolver = BreakpointResolverScripted::CreateFromStructuredData(
        nullptr, *subclass_options, error);
    break;
  case ExceptionResolver:
    error.SetErrorString("Exception resolvers are hard.");
    break;
  default:
    llvm_unreachable("Should never get an unresolvable resolver type.");
  }

  if (!error.Success()) {
    return result_sp;
  } else {
    // Add on the global offset option:
    resolver->SetOffset(offset);
    return BreakpointResolverSP(resolver);
  }
}

StructuredData::DictionarySP BreakpointResolver::WrapOptionsDict(
    StructuredData::DictionarySP options_dict_sp) {
  if (!options_dict_sp || !options_dict_sp->IsValid())
    return StructuredData::DictionarySP();

  StructuredData::DictionarySP type_dict_sp(new StructuredData::Dictionary());
  type_dict_sp->AddStringItem(GetSerializationSubclassKey(), GetResolverName());
  type_dict_sp->AddItem(GetSerializationSubclassOptionsKey(), options_dict_sp);

  // Add the m_offset to the dictionary:
  options_dict_sp->AddIntegerItem(GetKey(OptionNames::Offset), m_offset);

  return type_dict_sp;
}

void BreakpointResolver::SetBreakpoint(Breakpoint *bkpt) {
  m_breakpoint = bkpt;
  NotifyBreakpointSet();
}

void BreakpointResolver::ResolveBreakpointInModules(SearchFilter &filter,
                                                    ModuleList &modules) {
  filter.SearchInModuleList(*this, modules);
}

void BreakpointResolver::ResolveBreakpoint(SearchFilter &filter) {
  filter.Search(*this);
}

namespace {
struct SourceLoc {
  uint32_t line = UINT32_MAX;
  uint32_t column;
  SourceLoc(uint32_t l, uint32_t c) : line(l), column(c ? c : UINT32_MAX) {}
  SourceLoc(const SymbolContext &sc)
      : line(sc.line_entry.line),
        column(sc.line_entry.column ? sc.line_entry.column : UINT32_MAX) {}
};

bool operator<(const SourceLoc a, const SourceLoc b) {
  if (a.line < b.line)
    return true;
  if (a.line > b.line)
    return false;
  uint32_t a_col = a.column ? a.column : UINT32_MAX;
  uint32_t b_col = b.column ? b.column : UINT32_MAX;
  return a_col < b_col;
}
} // namespace

void BreakpointResolver::SetSCMatchesByLine(SearchFilter &filter,
                                            SymbolContextList &sc_list,
                                            bool skip_prologue,
                                            llvm::StringRef log_ident,
                                            uint32_t line, uint32_t column) {
  llvm::SmallVector<SymbolContext, 16> all_scs;
  for (uint32_t i = 0; i < sc_list.GetSize(); ++i)
    all_scs.push_back(sc_list[i]);

  while (all_scs.size()) {
    uint32_t closest_line = UINT32_MAX;

    // Move all the elements with a matching file spec to the end.
    auto &match = all_scs[0];
    auto worklist_begin = std::partition(
        all_scs.begin(), all_scs.end(), [&](const SymbolContext &sc) {
          if (sc.line_entry.file == match.line_entry.file ||
              sc.line_entry.original_file == match.line_entry.original_file) {
            // When a match is found, keep track of the smallest line number.
            closest_line = std::min(closest_line, sc.line_entry.line);
            return false;
          }
          return true;
        });

    // (worklist_begin, worklist_end) now contains all entries for one filespec.
    auto worklist_end = all_scs.end();

    if (column) {
      // If a column was requested, do a more precise match and only
      // return the first location that comes after or at the
      // requested location.
      SourceLoc requested(line, column);
      // First, filter out all entries left of the requested column.
      worklist_end = std::remove_if(
          worklist_begin, worklist_end,
          [&](const SymbolContext &sc) { return SourceLoc(sc) < requested; });
      // Sort the remaining entries by (line, column).
      llvm::sort(worklist_begin, worklist_end,
                 [](const SymbolContext &a, const SymbolContext &b) {
                   return SourceLoc(a) < SourceLoc(b);
                 });

      // Filter out all locations with a source location after the closest match.
      if (worklist_begin != worklist_end)
        worklist_end = std::remove_if(
            worklist_begin, worklist_end, [&](const SymbolContext &sc) {
              return SourceLoc(*worklist_begin) < SourceLoc(sc);
            });
    } else {
      // Remove all entries with a larger line number.
      // ResolveSymbolContext will always return a number that is >=
      // the line number you pass in. So the smaller line number is
      // always better.
      worklist_end = std::remove_if(worklist_begin, worklist_end,
                                    [&](const SymbolContext &sc) {
                                      return closest_line != sc.line_entry.line;
                                    });
    }

    // Sort by file address.
    llvm::sort(worklist_begin, worklist_end,
               [](const SymbolContext &a, const SymbolContext &b) {
                 return a.line_entry.range.GetBaseAddress().GetFileAddress() <
                        b.line_entry.range.GetBaseAddress().GetFileAddress();
               });

    // Go through and see if there are line table entries that are
    // contiguous, and if so keep only the first of the contiguous range.
    // We do this by picking the first location in each lexical block.
    llvm::SmallDenseSet<Block *, 8> blocks_with_breakpoints;
    for (auto first = worklist_begin; first != worklist_end; ++first) {
      assert(!blocks_with_breakpoints.count(first->block));
      blocks_with_breakpoints.insert(first->block);
      worklist_end =
          std::remove_if(std::next(first), worklist_end,
                         [&](const SymbolContext &sc) {
                           return blocks_with_breakpoints.count(sc.block);
                         });
    }

    // Make breakpoints out of the closest line number match.
    for (auto &sc : llvm::make_range(worklist_begin, worklist_end))
      AddLocation(filter, sc, skip_prologue, log_ident);

    // Remove all contexts processed by this iteration.
    all_scs.erase(worklist_begin, all_scs.end());
  }
}

void BreakpointResolver::AddLocation(SearchFilter &filter,
                                     const SymbolContext &sc,
                                     bool skip_prologue,
                                     llvm::StringRef log_ident) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  Address line_start = sc.line_entry.range.GetBaseAddress();
  if (!line_start.IsValid()) {
    if (log)
      log->Printf("error: Unable to set breakpoint %s at file address "
                  "0x%" PRIx64 "\n",
                  log_ident.str().c_str(), line_start.GetFileAddress());
    return;
  }

  if (!filter.AddressPasses(line_start)) {
    if (log)
      log->Printf("Breakpoint %s at file address 0x%" PRIx64
                  " didn't pass the filter.\n",
                  log_ident.str().c_str(), line_start.GetFileAddress());
  }

  // If the line number is before the prologue end, move it there...
  bool skipped_prologue = false;
  if (skip_prologue && sc.function) {
    Address prologue_addr(sc.function->GetAddressRange().GetBaseAddress());
    if (prologue_addr.IsValid() && (line_start == prologue_addr)) {
      const uint32_t prologue_byte_size = sc.function->GetPrologueByteSize();
      if (prologue_byte_size) {
        prologue_addr.Slide(prologue_byte_size);

        if (filter.AddressPasses(prologue_addr)) {
          skipped_prologue = true;
          line_start = prologue_addr;
        }
      }
    }
  }

  BreakpointLocationSP bp_loc_sp(AddLocation(line_start));
  if (log && bp_loc_sp && !m_breakpoint->IsInternal()) {
    StreamString s;
    bp_loc_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
    log->Printf("Added location (skipped prologue: %s): %s \n",
                skipped_prologue ? "yes" : "no", s.GetData());
  }
}

BreakpointLocationSP BreakpointResolver::AddLocation(Address loc_addr,
                                                     bool *new_location) {
  loc_addr.Slide(m_offset);
  return m_breakpoint->AddLocation(loc_addr, new_location);
}

void BreakpointResolver::SetOffset(lldb::addr_t offset) {
  // There may already be an offset, so we are actually adjusting location
  // addresses by the difference.
  // lldb::addr_t slide = offset - m_offset;
  // FIXME: We should go fix up all the already set locations for the new
  // slide.

  m_offset = offset;
}
