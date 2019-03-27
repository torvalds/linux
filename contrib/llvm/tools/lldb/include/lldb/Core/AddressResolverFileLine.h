//===-- AddressResolverFileLine.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AddressResolverFileLine_h_
#define liblldb_AddressResolverFileLine_h_

#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-defines.h"

#include <stdint.h>

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
/// @class AddressResolverFileLine AddressResolverFileLine.h
/// "lldb/Core/AddressResolverFileLine.h" This class finds address for source
/// file and line.  Optionally, it will look for inlined instances of the file
/// and line specification.
//----------------------------------------------------------------------

class AddressResolverFileLine : public AddressResolver {
public:
  AddressResolverFileLine(const FileSpec &resolver, uint32_t line_no,
                          bool check_inlines);

  ~AddressResolverFileLine() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

protected:
  FileSpec m_file_spec;   // This is the file spec we are looking for.
  uint32_t m_line_number; // This is the line number that we are looking for.
  bool m_inlines; // This determines whether the resolver looks for inlined
                  // functions or not.

private:
  DISALLOW_COPY_AND_ASSIGN(AddressResolverFileLine);
};

} // namespace lldb_private

#endif // liblldb_AddressResolverFileLine_h_
