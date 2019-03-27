//===-- AddressResolverName.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressResolverName.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>
#include <vector>

#include <stdint.h>

using namespace lldb;
using namespace lldb_private;

AddressResolverName::AddressResolverName(const char *func_name,
                                         AddressResolver::MatchType type)
    : AddressResolver(), m_func_name(func_name), m_class_name(nullptr),
      m_regex(), m_match_type(type) {
  if (m_match_type == AddressResolver::Regexp) {
    if (!m_regex.Compile(m_func_name.GetStringRef())) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

      if (log)
        log->Warning("function name regexp: \"%s\" did not compile.",
                     m_func_name.AsCString());
    }
  }
}

AddressResolverName::AddressResolverName(RegularExpression &func_regex)
    : AddressResolver(), m_func_name(nullptr), m_class_name(nullptr),
      m_regex(func_regex), m_match_type(AddressResolver::Regexp) {}

AddressResolverName::AddressResolverName(const char *class_name,
                                         const char *method,
                                         AddressResolver::MatchType type)
    : AddressResolver(), m_func_name(method), m_class_name(class_name),
      m_regex(), m_match_type(type) {}

AddressResolverName::~AddressResolverName() = default;

// FIXME: Right now we look at the module level, and call the module's
// "FindFunctions".
// Greg says he will add function tables, maybe at the CompileUnit level to
// accelerate function lookup.  At that point, we should switch the depth to
// CompileUnit, and look in these tables.

Searcher::CallbackReturn
AddressResolverName::SearchCallback(SearchFilter &filter,
                                    SymbolContext &context, Address *addr,
                                    bool containing) {
  SymbolContextList func_list;
  SymbolContextList sym_list;

  bool skip_prologue = true;
  uint32_t i;
  SymbolContext sc;
  Address func_addr;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

  if (m_class_name) {
    if (log)
      log->Warning("Class/method function specification not supported yet.\n");
    return Searcher::eCallbackReturnStop;
  }

  const bool include_symbols = false;
  const bool include_inlines = true;
  const bool append = false;
  switch (m_match_type) {
  case AddressResolver::Exact:
    if (context.module_sp) {
      context.module_sp->FindSymbolsWithNameAndType(m_func_name,
                                                    eSymbolTypeCode, sym_list);
      context.module_sp->FindFunctions(m_func_name, nullptr,
                                       eFunctionNameTypeAuto, include_symbols,
                                       include_inlines, append, func_list);
    }
    break;

  case AddressResolver::Regexp:
    if (context.module_sp) {
      context.module_sp->FindSymbolsMatchingRegExAndType(
          m_regex, eSymbolTypeCode, sym_list);
      context.module_sp->FindFunctions(m_regex, include_symbols,
                                       include_inlines, append, func_list);
    }
    break;

  case AddressResolver::Glob:
    if (log)
      log->Warning("glob is not supported yet.");
    break;
  }

  // Remove any duplicates between the function list and the symbol list
  if (func_list.GetSize()) {
    for (i = 0; i < func_list.GetSize(); i++) {
      if (!func_list.GetContextAtIndex(i, sc))
        continue;

      if (sc.function == nullptr)
        continue;
      uint32_t j = 0;
      while (j < sym_list.GetSize()) {
        SymbolContext symbol_sc;
        if (sym_list.GetContextAtIndex(j, symbol_sc)) {
          if (symbol_sc.symbol && symbol_sc.symbol->ValueIsAddress()) {
            if (sc.function->GetAddressRange().GetBaseAddress() ==
                symbol_sc.symbol->GetAddressRef()) {
              sym_list.RemoveContextAtIndex(j);
              continue; // Don't increment j
            }
          }
        }

        j++;
      }
    }

    for (i = 0; i < func_list.GetSize(); i++) {
      if (func_list.GetContextAtIndex(i, sc)) {
        if (sc.function) {
          func_addr = sc.function->GetAddressRange().GetBaseAddress();
          addr_t byte_size = sc.function->GetAddressRange().GetByteSize();
          if (skip_prologue) {
            const uint32_t prologue_byte_size =
                sc.function->GetPrologueByteSize();
            if (prologue_byte_size) {
              func_addr.SetOffset(func_addr.GetOffset() + prologue_byte_size);
              byte_size -= prologue_byte_size;
            }
          }

          if (filter.AddressPasses(func_addr)) {
            AddressRange new_range(func_addr, byte_size);
            m_address_ranges.push_back(new_range);
          }
        }
      }
    }
  }

  for (i = 0; i < sym_list.GetSize(); i++) {
    if (sym_list.GetContextAtIndex(i, sc)) {
      if (sc.symbol && sc.symbol->ValueIsAddress()) {
        func_addr = sc.symbol->GetAddressRef();
        addr_t byte_size = sc.symbol->GetByteSize();

        if (skip_prologue) {
          const uint32_t prologue_byte_size = sc.symbol->GetPrologueByteSize();
          if (prologue_byte_size) {
            func_addr.SetOffset(func_addr.GetOffset() + prologue_byte_size);
            byte_size -= prologue_byte_size;
          }
        }

        if (filter.AddressPasses(func_addr)) {
          AddressRange new_range(func_addr, byte_size);
          m_address_ranges.push_back(new_range);
        }
      }
    }
  }
  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth AddressResolverName::GetDepth() {
  return lldb::eSearchDepthModule;
}

void AddressResolverName::GetDescription(Stream *s) {
  s->PutCString("Address by function name: ");

  if (m_match_type == AddressResolver::Regexp)
    s->Printf("'%s' (regular expression)", m_regex.GetText().str().c_str());
  else
    s->Printf("'%s'", m_func_name.AsCString());
}
