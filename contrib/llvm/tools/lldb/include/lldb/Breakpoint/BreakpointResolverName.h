//===-- BreakpointResolverName.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointResolverName_h_
#define liblldb_BreakpointResolverName_h_

#include <string>
#include <vector>

#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Core/Module.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointResolverName BreakpointResolverName.h
/// "lldb/Breakpoint/BreakpointResolverName.h" This class sets breakpoints on
/// a given function name, either by exact match or by regular expression.
//----------------------------------------------------------------------

class BreakpointResolverName : public BreakpointResolver {
public:
  BreakpointResolverName(Breakpoint *bkpt, const char *name,
                         lldb::FunctionNameType name_type_mask,
                         lldb::LanguageType language,
                         Breakpoint::MatchType type, lldb::addr_t offset,
                         bool skip_prologue);

  // This one takes an array of names.  It is always MatchType = Exact.
  BreakpointResolverName(Breakpoint *bkpt, const char *names[],
                         size_t num_names,
                         lldb::FunctionNameType name_type_mask,
                         lldb::LanguageType language, lldb::addr_t offset,
                         bool skip_prologue);

  // This one takes a C++ array of names.  It is always MatchType = Exact.
  BreakpointResolverName(Breakpoint *bkpt, std::vector<std::string> names,
                         lldb::FunctionNameType name_type_mask,
                         lldb::LanguageType language, lldb::addr_t offset,
                         bool skip_prologue);

  // Creates a function breakpoint by regular expression.  Takes over control
  // of the lifespan of func_regex.
  BreakpointResolverName(Breakpoint *bkpt, RegularExpression &func_regex,
                         lldb::LanguageType language, lldb::addr_t offset,
                         bool skip_prologue);

  static BreakpointResolver *
  CreateFromStructuredData(Breakpoint *bkpt,
                           const StructuredData::Dictionary &data_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

  ~BreakpointResolverName() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  void Dump(Stream *s) const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverName *) { return true; }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::NameResolver;
  }

  lldb::BreakpointResolverSP CopyForBreakpoint(Breakpoint &breakpoint) override;

protected:
  BreakpointResolverName(const BreakpointResolverName &rhs);

  std::vector<Module::LookupInfo> m_lookups;
  ConstString m_class_name;
  RegularExpression m_regex;
  Breakpoint::MatchType m_match_type;
  lldb::LanguageType m_language;
  bool m_skip_prologue;

  void AddNameLookup(const ConstString &name,
                     lldb::FunctionNameType name_type_mask);
};

} // namespace lldb_private

#endif // liblldb_BreakpointResolverName_h_
