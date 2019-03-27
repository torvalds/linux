//===-- AddressResolverFileLine.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressResolverFileLine.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

#include <inttypes.h>
#include <vector>

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// AddressResolverFileLine:
//----------------------------------------------------------------------
AddressResolverFileLine::AddressResolverFileLine(const FileSpec &file_spec,
                                                 uint32_t line_no,
                                                 bool check_inlines)
    : AddressResolver(), m_file_spec(file_spec), m_line_number(line_no),
      m_inlines(check_inlines) {}

AddressResolverFileLine::~AddressResolverFileLine() {}

Searcher::CallbackReturn
AddressResolverFileLine::SearchCallback(SearchFilter &filter,
                                        SymbolContext &context, Address *addr,
                                        bool containing) {
  SymbolContextList sc_list;
  uint32_t sc_list_size;
  CompileUnit *cu = context.comp_unit;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

  sc_list_size =
      cu->ResolveSymbolContext(m_file_spec, m_line_number, m_inlines, false,
                               eSymbolContextEverything, sc_list);
  for (uint32_t i = 0; i < sc_list_size; i++) {
    SymbolContext sc;
    if (sc_list.GetContextAtIndex(i, sc)) {
      Address line_start = sc.line_entry.range.GetBaseAddress();
      addr_t byte_size = sc.line_entry.range.GetByteSize();
      if (line_start.IsValid()) {
        AddressRange new_range(line_start, byte_size);
        m_address_ranges.push_back(new_range);
        if (log) {
          StreamString s;
          // new_bp_loc->GetDescription (&s, lldb::eDescriptionLevelVerbose);
          // log->Printf ("Added address: %s\n", s.GetData());
        }
      } else {
        if (log)
          log->Printf(
              "error: Unable to resolve address at file address 0x%" PRIx64
              " for %s:%d\n",
              line_start.GetFileAddress(),
              m_file_spec.GetFilename().AsCString("<Unknown>"), m_line_number);
      }
    }
  }
  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth AddressResolverFileLine::GetDepth() {
  return lldb::eSearchDepthCompUnit;
}

void AddressResolverFileLine::GetDescription(Stream *s) {
  s->Printf("File and line address - file: \"%s\" line: %u",
            m_file_spec.GetFilename().AsCString("<Unknown>"), m_line_number);
}
