//===-- StringPrinter.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StringPrinter_h_
#define liblldb_StringPrinter_h_

#include <functional>
#include <string>

#include "lldb/lldb-forward.h"

#include "lldb/Utility/DataExtractor.h"

namespace lldb_private {
namespace formatters {
class StringPrinter {
public:
  enum class StringElementType { ASCII, UTF8, UTF16, UTF32 };

  enum class GetPrintableElementType { ASCII, UTF8 };

  class ReadStringAndDumpToStreamOptions {
  public:
    ReadStringAndDumpToStreamOptions()
        : m_location(0), m_process_sp(), m_stream(nullptr), m_prefix_token(),
          m_suffix_token(), m_quote('"'), m_source_size(0),
          m_needs_zero_termination(true), m_escape_non_printables(true),
          m_ignore_max_length(false), m_zero_is_terminator(true),
          m_language_type(lldb::eLanguageTypeUnknown) {}

    ReadStringAndDumpToStreamOptions(ValueObject &valobj);

    ReadStringAndDumpToStreamOptions &SetLocation(uint64_t l) {
      m_location = l;
      return *this;
    }

    uint64_t GetLocation() const { return m_location; }

    ReadStringAndDumpToStreamOptions &SetProcessSP(lldb::ProcessSP p) {
      m_process_sp = p;
      return *this;
    }

    lldb::ProcessSP GetProcessSP() const { return m_process_sp; }

    ReadStringAndDumpToStreamOptions &SetStream(Stream *s) {
      m_stream = s;
      return *this;
    }

    Stream *GetStream() const { return m_stream; }

    ReadStringAndDumpToStreamOptions &SetPrefixToken(const std::string &p) {
      m_prefix_token = p;
      return *this;
    }

    ReadStringAndDumpToStreamOptions &SetPrefixToken(std::nullptr_t) {
      m_prefix_token.clear();
      return *this;
    }

    const char *GetPrefixToken() const { return m_prefix_token.c_str(); }

    ReadStringAndDumpToStreamOptions &SetSuffixToken(const std::string &p) {
      m_suffix_token = p;
      return *this;
    }

    ReadStringAndDumpToStreamOptions &SetSuffixToken(std::nullptr_t) {
      m_suffix_token.clear();
      return *this;
    }

    const char *GetSuffixToken() const { return m_suffix_token.c_str(); }

    ReadStringAndDumpToStreamOptions &SetQuote(char q) {
      m_quote = q;
      return *this;
    }

    char GetQuote() const { return m_quote; }

    ReadStringAndDumpToStreamOptions &SetSourceSize(uint32_t s) {
      m_source_size = s;
      return *this;
    }

    uint32_t GetSourceSize() const { return m_source_size; }

    ReadStringAndDumpToStreamOptions &SetNeedsZeroTermination(bool z) {
      m_needs_zero_termination = z;
      return *this;
    }

    bool GetNeedsZeroTermination() const { return m_needs_zero_termination; }

    ReadStringAndDumpToStreamOptions &SetBinaryZeroIsTerminator(bool e) {
      m_zero_is_terminator = e;
      return *this;
    }

    bool GetBinaryZeroIsTerminator() const { return m_zero_is_terminator; }

    ReadStringAndDumpToStreamOptions &SetEscapeNonPrintables(bool e) {
      m_escape_non_printables = e;
      return *this;
    }

    bool GetEscapeNonPrintables() const { return m_escape_non_printables; }

    ReadStringAndDumpToStreamOptions &SetIgnoreMaxLength(bool e) {
      m_ignore_max_length = e;
      return *this;
    }

    bool GetIgnoreMaxLength() const { return m_ignore_max_length; }

    ReadStringAndDumpToStreamOptions &SetLanguage(lldb::LanguageType l) {
      m_language_type = l;
      return *this;
    }

    lldb::LanguageType GetLanguage() const

    {
      return m_language_type;
    }

  private:
    uint64_t m_location;
    lldb::ProcessSP m_process_sp;
    Stream *m_stream;
    std::string m_prefix_token;
    std::string m_suffix_token;
    char m_quote;
    uint32_t m_source_size;
    bool m_needs_zero_termination;
    bool m_escape_non_printables;
    bool m_ignore_max_length;
    bool m_zero_is_terminator;
    lldb::LanguageType m_language_type;
  };

  class ReadBufferAndDumpToStreamOptions {
  public:
    ReadBufferAndDumpToStreamOptions()
        : m_data(), m_stream(nullptr), m_prefix_token(), m_suffix_token(),
          m_quote('"'), m_source_size(0), m_escape_non_printables(true),
          m_zero_is_terminator(true), m_is_truncated(false),
          m_language_type(lldb::eLanguageTypeUnknown) {}

    ReadBufferAndDumpToStreamOptions(ValueObject &valobj);

    ReadBufferAndDumpToStreamOptions(
        const ReadStringAndDumpToStreamOptions &options);

    ReadBufferAndDumpToStreamOptions &SetData(DataExtractor d) {
      m_data = d;
      return *this;
    }

    lldb_private::DataExtractor GetData() const { return m_data; }

    ReadBufferAndDumpToStreamOptions &SetStream(Stream *s) {
      m_stream = s;
      return *this;
    }

    Stream *GetStream() const { return m_stream; }

    ReadBufferAndDumpToStreamOptions &SetPrefixToken(const std::string &p) {
      m_prefix_token = p;
      return *this;
    }

    ReadBufferAndDumpToStreamOptions &SetPrefixToken(std::nullptr_t) {
      m_prefix_token.clear();
      return *this;
    }

    const char *GetPrefixToken() const { return m_prefix_token.c_str(); }

    ReadBufferAndDumpToStreamOptions &SetSuffixToken(const std::string &p) {
      m_suffix_token = p;
      return *this;
    }

    ReadBufferAndDumpToStreamOptions &SetSuffixToken(std::nullptr_t) {
      m_suffix_token.clear();
      return *this;
    }

    const char *GetSuffixToken() const { return m_suffix_token.c_str(); }

    ReadBufferAndDumpToStreamOptions &SetQuote(char q) {
      m_quote = q;
      return *this;
    }

    char GetQuote() const { return m_quote; }

    ReadBufferAndDumpToStreamOptions &SetSourceSize(uint32_t s) {
      m_source_size = s;
      return *this;
    }

    uint32_t GetSourceSize() const { return m_source_size; }

    ReadBufferAndDumpToStreamOptions &SetEscapeNonPrintables(bool e) {
      m_escape_non_printables = e;
      return *this;
    }

    bool GetEscapeNonPrintables() const { return m_escape_non_printables; }

    ReadBufferAndDumpToStreamOptions &SetBinaryZeroIsTerminator(bool e) {
      m_zero_is_terminator = e;
      return *this;
    }

    bool GetBinaryZeroIsTerminator() const { return m_zero_is_terminator; }

    ReadBufferAndDumpToStreamOptions &SetIsTruncated(bool t) {
      m_is_truncated = t;
      return *this;
    }

    bool GetIsTruncated() const { return m_is_truncated; }

    ReadBufferAndDumpToStreamOptions &SetLanguage(lldb::LanguageType l) {
      m_language_type = l;
      return *this;
    }

    lldb::LanguageType GetLanguage() const

    {
      return m_language_type;
    }

  private:
    DataExtractor m_data;
    Stream *m_stream;
    std::string m_prefix_token;
    std::string m_suffix_token;
    char m_quote;
    uint32_t m_source_size;
    bool m_escape_non_printables;
    bool m_zero_is_terminator;
    bool m_is_truncated;
    lldb::LanguageType m_language_type;
  };

  // I can't use a std::unique_ptr for this because the Deleter is a template
  // argument there
  // and I want the same type to represent both pointers I want to free and
  // pointers I don't need to free - which is what this class essentially is
  // It's very specialized to the needs of this file, and not suggested for
  // general use
  template <typename T = uint8_t, typename U = char, typename S = size_t>
  struct StringPrinterBufferPointer {
  public:
    typedef std::function<void(const T *)> Deleter;

    StringPrinterBufferPointer(std::nullptr_t ptr)
        : m_data(nullptr), m_size(0), m_deleter() {}

    StringPrinterBufferPointer(const T *bytes, S size,
                               Deleter deleter = nullptr)
        : m_data(bytes), m_size(size), m_deleter(deleter) {}

    StringPrinterBufferPointer(const U *bytes, S size,
                               Deleter deleter = nullptr)
        : m_data(reinterpret_cast<const T *>(bytes)), m_size(size),
          m_deleter(deleter) {}

    StringPrinterBufferPointer(StringPrinterBufferPointer &&rhs)
        : m_data(rhs.m_data), m_size(rhs.m_size), m_deleter(rhs.m_deleter) {
      rhs.m_data = nullptr;
    }

    StringPrinterBufferPointer(const StringPrinterBufferPointer &rhs)
        : m_data(rhs.m_data), m_size(rhs.m_size), m_deleter(rhs.m_deleter) {
      rhs.m_data = nullptr; // this is why m_data has to be mutable
    }

    ~StringPrinterBufferPointer() {
      if (m_data && m_deleter)
        m_deleter(m_data);
      m_data = nullptr;
    }

    const T *GetBytes() const { return m_data; }

    const S GetSize() const { return m_size; }

    StringPrinterBufferPointer &
    operator=(const StringPrinterBufferPointer &rhs) {
      if (m_data && m_deleter)
        m_deleter(m_data);
      m_data = rhs.m_data;
      m_size = rhs.m_size;
      m_deleter = rhs.m_deleter;
      rhs.m_data = nullptr;
      return *this;
    }

  private:
    mutable const T *m_data;
    size_t m_size;
    Deleter m_deleter;
  };

  typedef std::function<StringPrinter::StringPrinterBufferPointer<
      uint8_t, char, size_t>(uint8_t *, uint8_t *, uint8_t *&)>
      EscapingHelper;
  typedef std::function<EscapingHelper(GetPrintableElementType)>
      EscapingHelperGenerator;

  static EscapingHelper
  GetDefaultEscapingHelper(GetPrintableElementType elem_type);

  template <StringElementType element_type>
  static bool
  ReadStringAndDumpToStream(const ReadStringAndDumpToStreamOptions &options);

  template <StringElementType element_type>
  static bool
  ReadBufferAndDumpToStream(const ReadBufferAndDumpToStreamOptions &options);
};

} // namespace formatters
} // namespace lldb_private

#endif // liblldb_StringPrinter_h_
