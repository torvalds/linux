//===-- RichManglingContext.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/RichManglingContext.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"

#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// RichManglingContext
//----------------------------------------------------------------------
void RichManglingContext::ResetProvider(InfoProvider new_provider) {
  // If we want to support parsers for other languages some day, we need a
  // switch here to delete the correct parser type.
  if (m_cxx_method_parser.hasValue()) {
    assert(m_provider == PluginCxxLanguage);
    delete get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser);
    m_cxx_method_parser.reset();
  }

  assert(new_provider != None && "Only reset to a valid provider");
  m_provider = new_provider;
}

bool RichManglingContext::FromItaniumName(const ConstString &mangled) {
  bool err = m_ipd.partialDemangle(mangled.GetCString());
  if (!err) {
    ResetProvider(ItaniumPartialDemangler);
  }

  if (Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DEMANGLE)) {
    if (!err) {
      ParseFullName();
      LLDB_LOG(log, "demangled itanium: {0} -> \"{1}\"", mangled, m_ipd_buf);
    } else {
      LLDB_LOG(log, "demangled itanium: {0} -> error: failed to demangle",
               mangled);
    }
  }

  return !err; // true == success
}

bool RichManglingContext::FromCxxMethodName(const ConstString &demangled) {
  ResetProvider(PluginCxxLanguage);
  m_cxx_method_parser = new CPlusPlusLanguage::MethodName(demangled);
  return true;
}

bool RichManglingContext::IsCtorOrDtor() const {
  assert(m_provider != None && "Initialize a provider first");
  switch (m_provider) {
  case ItaniumPartialDemangler:
    return m_ipd.isCtorOrDtor();
  case PluginCxxLanguage: {
    // We can only check for destructors here.
    auto base_name =
        get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser)->GetBasename();
    return base_name.startswith("~");
  }
  case None:
    return false;
  }
  llvm_unreachable("Fully covered switch above!");
}

bool RichManglingContext::IsFunction() const {
  assert(m_provider != None && "Initialize a provider first");
  switch (m_provider) {
  case ItaniumPartialDemangler:
    return m_ipd.isFunction();
  case PluginCxxLanguage:
    return get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser)->IsValid();
  case None:
    return false;
  }
  llvm_unreachable("Fully covered switch above!");
}

void RichManglingContext::processIPDStrResult(char *ipd_res, size_t res_size) {
  // Error case: Clear the buffer.
  if (LLVM_UNLIKELY(ipd_res == nullptr)) {
    assert(res_size == m_ipd_buf_size &&
           "Failed IPD queries keep the original size in the N parameter");

    m_ipd_buf[0] = '\0';
    m_buffer = llvm::StringRef(m_ipd_buf, 0);
    return;
  }

  // IPD's res_size includes null terminator.
  assert(ipd_res[res_size - 1] == '\0' &&
         "IPD returns null-terminated strings and we rely on that");

  // Update buffer/size on realloc.
  if (LLVM_UNLIKELY(ipd_res != m_ipd_buf || res_size > m_ipd_buf_size)) {
    m_ipd_buf = ipd_res;       // std::realloc freed or reused the old buffer.
    m_ipd_buf_size = res_size; // May actually be bigger, but we can't know.

    if (Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DEMANGLE))
      LLDB_LOG(log, "ItaniumPartialDemangler Realloc: new buffer size is {0}",
               m_ipd_buf_size);
  }

  // 99% case: Just remember the string length.
  m_buffer = llvm::StringRef(m_ipd_buf, res_size - 1);
}

void RichManglingContext::ParseFunctionBaseName() {
  assert(m_provider != None && "Initialize a provider first");
  switch (m_provider) {
  case ItaniumPartialDemangler: {
    auto n = m_ipd_buf_size;
    auto buf = m_ipd.getFunctionBaseName(m_ipd_buf, &n);
    processIPDStrResult(buf, n);
    return;
  }
  case PluginCxxLanguage:
    m_buffer =
        get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser)->GetBasename();
    return;
  case None:
    return;
  }
}

void RichManglingContext::ParseFunctionDeclContextName() {
  assert(m_provider != None && "Initialize a provider first");
  switch (m_provider) {
  case ItaniumPartialDemangler: {
    auto n = m_ipd_buf_size;
    auto buf = m_ipd.getFunctionDeclContextName(m_ipd_buf, &n);
    processIPDStrResult(buf, n);
    return;
  }
  case PluginCxxLanguage:
    m_buffer =
        get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser)->GetContext();
    return;
  case None:
    return;
  }
}

void RichManglingContext::ParseFullName() {
  assert(m_provider != None && "Initialize a provider first");
  switch (m_provider) {
  case ItaniumPartialDemangler: {
    auto n = m_ipd_buf_size;
    auto buf = m_ipd.finishDemangle(m_ipd_buf, &n);
    processIPDStrResult(buf, n);
    return;
  }
  case PluginCxxLanguage:
    m_buffer = get<CPlusPlusLanguage::MethodName>(m_cxx_method_parser)
                   ->GetFullName()
                   .GetStringRef();
    return;
  case None:
    return;
  }
}
