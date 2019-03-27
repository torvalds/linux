//===-- AddressResolverName.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AddressResolverName_h_
#define liblldb_AddressResolverName_h_

#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/lldb-defines.h"

namespace lldb_private {
class Address;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolContext;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class AddressResolverName AddressResolverName.h
/// "lldb/Core/AddressResolverName.h" This class finds addresses for a given
/// function name, either by exact match or by regular expression.
//----------------------------------------------------------------------

class AddressResolverName : public AddressResolver {
public:
  AddressResolverName(const char *func_name,
                      AddressResolver::MatchType type = Exact);

  // Creates a function breakpoint by regular expression.  Takes over control
  // of the lifespan of func_regex.
  AddressResolverName(RegularExpression &func_regex);

  AddressResolverName(const char *class_name, const char *method,
                      AddressResolver::MatchType type);

  ~AddressResolverName() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

protected:
  ConstString m_func_name;
  ConstString m_class_name; // FIXME: Not used yet.  The idea would be to stop
                            // on methods of this class.
  RegularExpression m_regex;
  AddressResolver::MatchType m_match_type;

private:
  DISALLOW_COPY_AND_ASSIGN(AddressResolverName);
};

} // namespace lldb_private

#endif // liblldb_AddressResolverName_h_
