//===-- BreakpointResolverFileLine.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverFileLine.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// BreakpointResolverFileLine:
//----------------------------------------------------------------------
BreakpointResolverFileLine::BreakpointResolverFileLine(
    Breakpoint *bkpt, const FileSpec &file_spec, uint32_t line_no,
    uint32_t column, lldb::addr_t offset, bool check_inlines,
    bool skip_prologue, bool exact_match)
    : BreakpointResolver(bkpt, BreakpointResolver::FileLineResolver, offset),
      m_file_spec(file_spec), m_line_number(line_no), m_column(column),
      m_inlines(check_inlines), m_skip_prologue(skip_prologue),
      m_exact_match(exact_match) {}

BreakpointResolverFileLine::~BreakpointResolverFileLine() {}

BreakpointResolver *BreakpointResolverFileLine::CreateFromStructuredData(
    Breakpoint *bkpt, const StructuredData::Dictionary &options_dict,
    Status &error) {
  llvm::StringRef filename;
  uint32_t line_no;
  uint32_t column;
  bool check_inlines;
  bool skip_prologue;
  bool exact_match;
  bool success;

  lldb::addr_t offset = 0;

  success = options_dict.GetValueForKeyAsString(GetKey(OptionNames::FileName),
                                                filename);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find filename entry.");
    return nullptr;
  }

  success = options_dict.GetValueForKeyAsInteger(
      GetKey(OptionNames::LineNumber), line_no);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find line number entry.");
    return nullptr;
  }

  success =
      options_dict.GetValueForKeyAsInteger(GetKey(OptionNames::Column), column);
  if (!success) {
    // Backwards compatibility.
    column = 0;
  }

  success = options_dict.GetValueForKeyAsBoolean(GetKey(OptionNames::Inlines),
                                                 check_inlines);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find check inlines entry.");
    return nullptr;
  }

  success = options_dict.GetValueForKeyAsBoolean(
      GetKey(OptionNames::SkipPrologue), skip_prologue);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find skip prologue entry.");
    return nullptr;
  }

  success = options_dict.GetValueForKeyAsBoolean(
      GetKey(OptionNames::ExactMatch), exact_match);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find exact match entry.");
    return nullptr;
  }

  FileSpec file_spec(filename);

  return new BreakpointResolverFileLine(bkpt, file_spec, line_no, column,
                                        offset, check_inlines, skip_prologue,
                                        exact_match);
}

StructuredData::ObjectSP
BreakpointResolverFileLine::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());

  options_dict_sp->AddStringItem(GetKey(OptionNames::FileName),
                                 m_file_spec.GetPath());
  options_dict_sp->AddIntegerItem(GetKey(OptionNames::LineNumber),
                                  m_line_number);
  options_dict_sp->AddIntegerItem(GetKey(OptionNames::Column),
                                  m_column);
  options_dict_sp->AddBooleanItem(GetKey(OptionNames::Inlines), m_inlines);
  options_dict_sp->AddBooleanItem(GetKey(OptionNames::SkipPrologue),
                                  m_skip_prologue);
  options_dict_sp->AddBooleanItem(GetKey(OptionNames::ExactMatch),
                                  m_exact_match);

  return WrapOptionsDict(options_dict_sp);
}

// Filter the symbol context list to remove contexts where the line number was
// moved into a new function. We do this conservatively, so if e.g. we cannot
// resolve the function in the context (which can happen in case of line-table-
// only debug info), we leave the context as is. The trickiest part here is
// handling inlined functions -- in this case we need to make sure we look at
// the declaration line of the inlined function, NOT the function it was
// inlined into.
void BreakpointResolverFileLine::FilterContexts(SymbolContextList &sc_list,
                                                bool is_relative) {
  if (m_exact_match)
    return; // Nothing to do. Contexts are precise.

  llvm::StringRef relative_path;
  if (is_relative)
    relative_path = m_file_spec.GetDirectory().GetStringRef();

  Log * log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS);
  for(uint32_t i = 0; i < sc_list.GetSize(); ++i) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(i, sc);
    if (is_relative) {
      // If the path was relative, make sure any matches match as long as the
      // relative parts of the path match the path from support files
      auto sc_dir = sc.line_entry.file.GetDirectory().GetStringRef();
      if (!sc_dir.endswith(relative_path)) {
        // We had a relative path specified and the relative directory doesn't
        // match so remove this one
        LLDB_LOG(log, "removing not matching relative path {0} since it "
                "doesn't end with {1}", sc_dir, relative_path);
        sc_list.RemoveContextAtIndex(i);
        --i;
        continue;
      }
    }

    if (!sc.block)
      continue;

    FileSpec file;
    uint32_t line;
    const Block *inline_block = sc.block->GetContainingInlinedBlock();
    if (inline_block) {
      const Declaration &inline_declaration = inline_block->GetInlinedFunctionInfo()->GetDeclaration();
      if (!inline_declaration.IsValid())
        continue;
      file = inline_declaration.GetFile();
      line = inline_declaration.GetLine();
    } else if (sc.function)
      sc.function->GetStartLineSourceInfo(file, line);
    else
      continue;

    if (file != sc.line_entry.file) {
      LLDB_LOG(log, "unexpected symbol context file {0}", sc.line_entry.file);
      continue;
    }

    // Compare the requested line number with the line of the function
    // declaration. In case of a function declared as:
    //
    // int
    // foo()
    // {
    //   ...
    //
    // the compiler will set the declaration line to the "foo" line, which is
    // the reason why we have -1 here. This can fail in case of two inline
    // functions defined back-to-back:
    //
    // inline int foo1() { ... }
    // inline int foo2() { ... }
    //
    // but that's the best we can do for now.
    // One complication, if the line number returned from GetStartLineSourceInfo
    // is 0, then we can't do this calculation.  That can happen if
    // GetStartLineSourceInfo gets an error, or if the first line number in
    // the function really is 0 - which happens for some languages.
    const int decl_line_is_too_late_fudge = 1;
    if (line && m_line_number < line - decl_line_is_too_late_fudge) {
      LLDB_LOG(log, "removing symbol context at {0}:{1}", file, line);
      sc_list.RemoveContextAtIndex(i);
      --i;
    }
  }
}

Searcher::CallbackReturn
BreakpointResolverFileLine::SearchCallback(SearchFilter &filter,
                                           SymbolContext &context,
                                           Address *addr, bool containing) {
  SymbolContextList sc_list;

  assert(m_breakpoint != NULL);

  // There is a tricky bit here.  You can have two compilation units that
  // #include the same file, and in one of them the function at m_line_number
  // is used (and so code and a line entry for it is generated) but in the
  // other it isn't.  If we considered the CU's independently, then in the
  // second inclusion, we'd move the breakpoint to the next function that
  // actually generated code in the header file.  That would end up being
  // confusing.  So instead, we do the CU iterations by hand here, then scan
  // through the complete list of matches, and figure out the closest line
  // number match, and only set breakpoints on that match.

  // Note also that if file_spec only had a file name and not a directory,
  // there may be many different file spec's in the resultant list.  The
  // closest line match for one will not be right for some totally different
  // file.  So we go through the match list and pull out the sets that have the
  // same file spec in their line_entry and treat each set separately.

  FileSpec search_file_spec = m_file_spec;
  const bool is_relative = m_file_spec.IsRelative();
  if (is_relative)
    search_file_spec.GetDirectory().Clear();

  const size_t num_comp_units = context.module_sp->GetNumCompileUnits();
  for (size_t i = 0; i < num_comp_units; i++) {
    CompUnitSP cu_sp(context.module_sp->GetCompileUnitAtIndex(i));
    if (cu_sp) {
      if (filter.CompUnitPasses(*cu_sp))
        cu_sp->ResolveSymbolContext(search_file_spec, m_line_number, m_inlines,
                                    m_exact_match, eSymbolContextEverything,
                                    sc_list);
    }
  }

  FilterContexts(sc_list, is_relative);

  StreamString s;
  s.Printf("for %s:%d ", m_file_spec.GetFilename().AsCString("<Unknown>"),
           m_line_number);

  SetSCMatchesByLine(filter, sc_list, m_skip_prologue, s.GetString(),
                     m_line_number, m_column);

  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth BreakpointResolverFileLine::GetDepth() {
  return lldb::eSearchDepthModule;
}

void BreakpointResolverFileLine::GetDescription(Stream *s) {
  s->Printf("file = '%s', line = %u, ", m_file_spec.GetPath().c_str(),
            m_line_number);
  if (m_column)
    s->Printf("column = %u, ", m_column);
  s->Printf("exact_match = %d", m_exact_match);
}

void BreakpointResolverFileLine::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverFileLine::CopyForBreakpoint(Breakpoint &breakpoint) {
  lldb::BreakpointResolverSP ret_sp(new BreakpointResolverFileLine(
      &breakpoint, m_file_spec, m_line_number, m_column, m_offset, m_inlines,
      m_skip_prologue, m_exact_match));

  return ret_sp;
}
