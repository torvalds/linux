//===-- RichManglingContext.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RichManglingContext_h_
#define liblldb_RichManglingContext_h_

#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

#include "lldb/Utility/ConstString.h"

#include "llvm/ADT/Any.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Demangle/Demangle.h"

namespace lldb_private {

/// Uniform wrapper for access to rich mangling information from different
/// providers. See Mangled::DemangleWithRichManglingInfo()
class RichManglingContext {
public:
  RichManglingContext() : m_provider(None), m_ipd_buf_size(2048) {
    m_ipd_buf = static_cast<char *>(std::malloc(m_ipd_buf_size));
    m_ipd_buf[0] = '\0';
  }

  ~RichManglingContext() { std::free(m_ipd_buf); }

  /// Use the ItaniumPartialDemangler to obtain rich mangling information from
  /// the given mangled name.
  bool FromItaniumName(const ConstString &mangled);

  /// Use the legacy language parser implementation to obtain rich mangling
  /// information from the given demangled name.
  bool FromCxxMethodName(const ConstString &demangled);

  /// If this symbol describes a constructor or destructor.
  bool IsCtorOrDtor() const;

  /// If this symbol describes a function.
  bool IsFunction() const;

  /// Get the base name of a function. This doesn't include trailing template
  /// arguments, ie "a::b<int>" gives "b". The result will overwrite the
  /// internal buffer. It can be obtained via GetBufferRef().
  void ParseFunctionBaseName();

  /// Get the context name for a function. For "a::b::c", this function returns
  /// "a::b". The result will overwrite the internal buffer. It can be obtained
  /// via GetBufferRef().
  void ParseFunctionDeclContextName();

  /// Get the entire demangled name. The result will overwrite the internal
  /// buffer. It can be obtained via GetBufferRef().
  void ParseFullName();

  /// Obtain a StringRef to the internal buffer that holds the result of the
  /// most recent ParseXy() operation. The next ParseXy() call invalidates it.
  llvm::StringRef GetBufferRef() const {
    assert(m_provider != None && "Initialize a provider first");
    return m_buffer;
  }

private:
  enum InfoProvider { None, ItaniumPartialDemangler, PluginCxxLanguage };

  /// Selects the rich mangling info provider.
  InfoProvider m_provider;

  /// Reference to the buffer used for results of ParseXy() operations.
  llvm::StringRef m_buffer;

  /// Members for ItaniumPartialDemangler
  llvm::ItaniumPartialDemangler m_ipd;
  char *m_ipd_buf;
  size_t m_ipd_buf_size;

  /// Members for PluginCxxLanguage
  /// Cannot forward declare inner class CPlusPlusLanguage::MethodName. The
  /// respective header is in Plugins and including it from here causes cyclic
  /// dependency. Instead keep a llvm::Any and cast it on-access in the cpp.
  llvm::Any m_cxx_method_parser;

  /// Clean up memory and set a new info provider for this instance.
  void ResetProvider(InfoProvider new_provider);

  /// Uniform handling of string buffers for ItaniumPartialDemangler.
  void processIPDStrResult(char *ipd_res, size_t res_len);

  /// Cast the given parser to the given type. Ideally we would have a type
  /// trait to deduce \a ParserT from a given InfoProvider, but unfortunately we
  /// can't access CPlusPlusLanguage::MethodName from within the header.
  template <class ParserT> static ParserT *get(llvm::Any parser) {
    assert(parser.hasValue());
    assert(llvm::any_isa<ParserT *>(parser));
    return llvm::any_cast<ParserT *>(parser);
  }
};

} // namespace lldb_private

#endif
