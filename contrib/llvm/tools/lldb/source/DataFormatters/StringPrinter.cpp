//===-- StringPrinter.cpp ----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/StringPrinter.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/ConvertUTF.h"

#include <ctype.h>
#include <locale>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

// we define this for all values of type but only implement it for those we
// care about that's good because we get linker errors for any unsupported type
template <lldb_private::formatters::StringPrinter::StringElementType type>
static StringPrinter::StringPrinterBufferPointer<>
GetPrintableImpl(uint8_t *buffer, uint8_t *buffer_end, uint8_t *&next);

// mimic isprint() for Unicode codepoints
static bool isprint(char32_t codepoint) {
  if (codepoint <= 0x1F || codepoint == 0x7F) // C0
  {
    return false;
  }
  if (codepoint >= 0x80 && codepoint <= 0x9F) // C1
  {
    return false;
  }
  if (codepoint == 0x2028 || codepoint == 0x2029) // line/paragraph separators
  {
    return false;
  }
  if (codepoint == 0x200E || codepoint == 0x200F ||
      (codepoint >= 0x202A &&
       codepoint <= 0x202E)) // bidirectional text control
  {
    return false;
  }
  if (codepoint >= 0xFFF9 &&
      codepoint <= 0xFFFF) // interlinears and generally specials
  {
    return false;
  }
  return true;
}

template <>
StringPrinter::StringPrinterBufferPointer<>
GetPrintableImpl<StringPrinter::StringElementType::ASCII>(uint8_t *buffer,
                                                          uint8_t *buffer_end,
                                                          uint8_t *&next) {
  StringPrinter::StringPrinterBufferPointer<> retval = {nullptr};

  switch (*buffer) {
  case 0:
    retval = {"\\0", 2};
    break;
  case '\a':
    retval = {"\\a", 2};
    break;
  case '\b':
    retval = {"\\b", 2};
    break;
  case '\f':
    retval = {"\\f", 2};
    break;
  case '\n':
    retval = {"\\n", 2};
    break;
  case '\r':
    retval = {"\\r", 2};
    break;
  case '\t':
    retval = {"\\t", 2};
    break;
  case '\v':
    retval = {"\\v", 2};
    break;
  case '\"':
    retval = {"\\\"", 2};
    break;
  case '\\':
    retval = {"\\\\", 2};
    break;
  default:
    if (isprint(*buffer))
      retval = {buffer, 1};
    else {
      uint8_t *data = new uint8_t[5];
      sprintf((char *)data, "\\x%02x", *buffer);
      retval = {data, 4, [](const uint8_t *c) { delete[] c; }};
      break;
    }
  }

  next = buffer + 1;
  return retval;
}

static char32_t ConvertUTF8ToCodePoint(unsigned char c0, unsigned char c1) {
  return (c0 - 192) * 64 + (c1 - 128);
}
static char32_t ConvertUTF8ToCodePoint(unsigned char c0, unsigned char c1,
                                       unsigned char c2) {
  return (c0 - 224) * 4096 + (c1 - 128) * 64 + (c2 - 128);
}
static char32_t ConvertUTF8ToCodePoint(unsigned char c0, unsigned char c1,
                                       unsigned char c2, unsigned char c3) {
  return (c0 - 240) * 262144 + (c2 - 128) * 4096 + (c2 - 128) * 64 + (c3 - 128);
}

template <>
StringPrinter::StringPrinterBufferPointer<>
GetPrintableImpl<StringPrinter::StringElementType::UTF8>(uint8_t *buffer,
                                                         uint8_t *buffer_end,
                                                         uint8_t *&next) {
  StringPrinter::StringPrinterBufferPointer<> retval{nullptr};

  unsigned utf8_encoded_len = llvm::getNumBytesForUTF8(*buffer);

  if (1u + std::distance(buffer, buffer_end) < utf8_encoded_len) {
    // I don't have enough bytes - print whatever I have left
    retval = {buffer, static_cast<size_t>(1 + buffer_end - buffer)};
    next = buffer_end + 1;
    return retval;
  }

  char32_t codepoint = 0;
  switch (utf8_encoded_len) {
  case 1:
    // this is just an ASCII byte - ask ASCII
    return GetPrintableImpl<StringPrinter::StringElementType::ASCII>(
        buffer, buffer_end, next);
  case 2:
    codepoint = ConvertUTF8ToCodePoint((unsigned char)*buffer,
                                       (unsigned char)*(buffer + 1));
    break;
  case 3:
    codepoint = ConvertUTF8ToCodePoint((unsigned char)*buffer,
                                       (unsigned char)*(buffer + 1),
                                       (unsigned char)*(buffer + 2));
    break;
  case 4:
    codepoint = ConvertUTF8ToCodePoint(
        (unsigned char)*buffer, (unsigned char)*(buffer + 1),
        (unsigned char)*(buffer + 2), (unsigned char)*(buffer + 3));
    break;
  default:
    // this is probably some bogus non-character thing just print it as-is and
    // hope to sync up again soon
    retval = {buffer, 1};
    next = buffer + 1;
    return retval;
  }

  if (codepoint) {
    switch (codepoint) {
    case 0:
      retval = {"\\0", 2};
      break;
    case '\a':
      retval = {"\\a", 2};
      break;
    case '\b':
      retval = {"\\b", 2};
      break;
    case '\f':
      retval = {"\\f", 2};
      break;
    case '\n':
      retval = {"\\n", 2};
      break;
    case '\r':
      retval = {"\\r", 2};
      break;
    case '\t':
      retval = {"\\t", 2};
      break;
    case '\v':
      retval = {"\\v", 2};
      break;
    case '\"':
      retval = {"\\\"", 2};
      break;
    case '\\':
      retval = {"\\\\", 2};
      break;
    default:
      if (isprint(codepoint))
        retval = {buffer, utf8_encoded_len};
      else {
        uint8_t *data = new uint8_t[11];
        sprintf((char *)data, "\\U%08x", (unsigned)codepoint);
        retval = {data, 10, [](const uint8_t *c) { delete[] c; }};
        break;
      }
    }

    next = buffer + utf8_encoded_len;
    return retval;
  }

  // this should not happen - but just in case.. try to resync at some point
  retval = {buffer, 1};
  next = buffer + 1;
  return retval;
}

// Given a sequence of bytes, this function returns: a sequence of bytes to
// actually print out + a length the following unscanned position of the buffer
// is in next
static StringPrinter::StringPrinterBufferPointer<>
GetPrintable(StringPrinter::StringElementType type, uint8_t *buffer,
             uint8_t *buffer_end, uint8_t *&next) {
  if (!buffer)
    return {nullptr};

  switch (type) {
  case StringPrinter::StringElementType::ASCII:
    return GetPrintableImpl<StringPrinter::StringElementType::ASCII>(
        buffer, buffer_end, next);
  case StringPrinter::StringElementType::UTF8:
    return GetPrintableImpl<StringPrinter::StringElementType::UTF8>(
        buffer, buffer_end, next);
  default:
    return {nullptr};
  }
}

StringPrinter::EscapingHelper
StringPrinter::GetDefaultEscapingHelper(GetPrintableElementType elem_type) {
  switch (elem_type) {
  case GetPrintableElementType::UTF8:
    return [](uint8_t *buffer, uint8_t *buffer_end,
              uint8_t *&next) -> StringPrinter::StringPrinterBufferPointer<> {
      return GetPrintable(StringPrinter::StringElementType::UTF8, buffer,
                          buffer_end, next);
    };
  case GetPrintableElementType::ASCII:
    return [](uint8_t *buffer, uint8_t *buffer_end,
              uint8_t *&next) -> StringPrinter::StringPrinterBufferPointer<> {
      return GetPrintable(StringPrinter::StringElementType::ASCII, buffer,
                          buffer_end, next);
    };
  }
  llvm_unreachable("bad element type");
}

// use this call if you already have an LLDB-side buffer for the data
template <typename SourceDataType>
static bool DumpUTFBufferToStream(
    llvm::ConversionResult (*ConvertFunction)(const SourceDataType **,
                                              const SourceDataType *,
                                              llvm::UTF8 **, llvm::UTF8 *,
                                              llvm::ConversionFlags),
    const StringPrinter::ReadBufferAndDumpToStreamOptions &dump_options) {
  Stream &stream(*dump_options.GetStream());
  if (dump_options.GetPrefixToken() != 0)
    stream.Printf("%s", dump_options.GetPrefixToken());
  if (dump_options.GetQuote() != 0)
    stream.Printf("%c", dump_options.GetQuote());
  auto data(dump_options.GetData());
  auto source_size(dump_options.GetSourceSize());
  if (data.GetByteSize() && data.GetDataStart() && data.GetDataEnd()) {
    const int bufferSPSize = data.GetByteSize();
    if (dump_options.GetSourceSize() == 0) {
      const int origin_encoding = 8 * sizeof(SourceDataType);
      source_size = bufferSPSize / (origin_encoding / 4);
    }

    const SourceDataType *data_ptr =
        (const SourceDataType *)data.GetDataStart();
    const SourceDataType *data_end_ptr = data_ptr + source_size;

    const bool zero_is_terminator = dump_options.GetBinaryZeroIsTerminator();

    if (zero_is_terminator) {
      while (data_ptr < data_end_ptr) {
        if (!*data_ptr) {
          data_end_ptr = data_ptr;
          break;
        }
        data_ptr++;
      }

      data_ptr = (const SourceDataType *)data.GetDataStart();
    }

    lldb::DataBufferSP utf8_data_buffer_sp;
    llvm::UTF8 *utf8_data_ptr = nullptr;
    llvm::UTF8 *utf8_data_end_ptr = nullptr;

    if (ConvertFunction) {
      utf8_data_buffer_sp.reset(new DataBufferHeap(4 * bufferSPSize, 0));
      utf8_data_ptr = (llvm::UTF8 *)utf8_data_buffer_sp->GetBytes();
      utf8_data_end_ptr = utf8_data_ptr + utf8_data_buffer_sp->GetByteSize();
      ConvertFunction(&data_ptr, data_end_ptr, &utf8_data_ptr,
                      utf8_data_end_ptr, llvm::lenientConversion);
      if (!zero_is_terminator)
        utf8_data_end_ptr = utf8_data_ptr;
      // needed because the ConvertFunction will change the value of the
      // data_ptr.
      utf8_data_ptr =
          (llvm::UTF8 *)utf8_data_buffer_sp->GetBytes();
    } else {
      // just copy the pointers - the cast is necessary to make the compiler
      // happy but this should only happen if we are reading UTF8 data
      utf8_data_ptr = const_cast<llvm::UTF8 *>(
          reinterpret_cast<const llvm::UTF8 *>(data_ptr));
      utf8_data_end_ptr = const_cast<llvm::UTF8 *>(
          reinterpret_cast<const llvm::UTF8 *>(data_end_ptr));
    }

    const bool escape_non_printables = dump_options.GetEscapeNonPrintables();
    lldb_private::formatters::StringPrinter::EscapingHelper escaping_callback;
    if (escape_non_printables) {
      if (Language *language = Language::FindPlugin(dump_options.GetLanguage()))
        escaping_callback = language->GetStringPrinterEscapingHelper(
            lldb_private::formatters::StringPrinter::GetPrintableElementType::
                UTF8);
      else
        escaping_callback =
            lldb_private::formatters::StringPrinter::GetDefaultEscapingHelper(
                lldb_private::formatters::StringPrinter::
                    GetPrintableElementType::UTF8);
    }

    // since we tend to accept partial data (and even partially malformed data)
    // we might end up with no NULL terminator before the end_ptr hence we need
    // to take a slower route and ensure we stay within boundaries
    for (; utf8_data_ptr < utf8_data_end_ptr;) {
      if (zero_is_terminator && !*utf8_data_ptr)
        break;

      if (escape_non_printables) {
        uint8_t *next_data = nullptr;
        auto printable =
            escaping_callback(utf8_data_ptr, utf8_data_end_ptr, next_data);
        auto printable_bytes = printable.GetBytes();
        auto printable_size = printable.GetSize();
        if (!printable_bytes || !next_data) {
          // GetPrintable() failed on us - print one byte in a desperate resync
          // attempt
          printable_bytes = utf8_data_ptr;
          printable_size = 1;
          next_data = utf8_data_ptr + 1;
        }
        for (unsigned c = 0; c < printable_size; c++)
          stream.Printf("%c", *(printable_bytes + c));
        utf8_data_ptr = (uint8_t *)next_data;
      } else {
        stream.Printf("%c", *utf8_data_ptr);
        utf8_data_ptr++;
      }
    }
  }
  if (dump_options.GetQuote() != 0)
    stream.Printf("%c", dump_options.GetQuote());
  if (dump_options.GetSuffixToken() != 0)
    stream.Printf("%s", dump_options.GetSuffixToken());
  if (dump_options.GetIsTruncated())
    stream.Printf("...");
  return true;
}

lldb_private::formatters::StringPrinter::ReadStringAndDumpToStreamOptions::
    ReadStringAndDumpToStreamOptions(ValueObject &valobj)
    : ReadStringAndDumpToStreamOptions() {
  SetEscapeNonPrintables(
      valobj.GetTargetSP()->GetDebugger().GetEscapeNonPrintables());
}

lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStreamOptions::
    ReadBufferAndDumpToStreamOptions(ValueObject &valobj)
    : ReadBufferAndDumpToStreamOptions() {
  SetEscapeNonPrintables(
      valobj.GetTargetSP()->GetDebugger().GetEscapeNonPrintables());
}

lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStreamOptions::
    ReadBufferAndDumpToStreamOptions(
        const ReadStringAndDumpToStreamOptions &options)
    : ReadBufferAndDumpToStreamOptions() {
  SetStream(options.GetStream());
  SetPrefixToken(options.GetPrefixToken());
  SetSuffixToken(options.GetSuffixToken());
  SetQuote(options.GetQuote());
  SetEscapeNonPrintables(options.GetEscapeNonPrintables());
  SetBinaryZeroIsTerminator(options.GetBinaryZeroIsTerminator());
  SetLanguage(options.GetLanguage());
}

namespace lldb_private {

namespace formatters {

template <>
bool StringPrinter::ReadStringAndDumpToStream<
    StringPrinter::StringElementType::ASCII>(
    const ReadStringAndDumpToStreamOptions &options) {
  assert(options.GetStream() && "need a Stream to print the string to");
  Status my_error;

  ProcessSP process_sp(options.GetProcessSP());

  if (process_sp.get() == nullptr || options.GetLocation() == 0)
    return false;

  size_t size;
  const auto max_size = process_sp->GetTarget().GetMaximumSizeOfStringSummary();
  bool is_truncated = false;

  if (options.GetSourceSize() == 0)
    size = max_size;
  else if (!options.GetIgnoreMaxLength()) {
    size = options.GetSourceSize();
    if (size > max_size) {
      size = max_size;
      is_truncated = true;
    }
  } else
    size = options.GetSourceSize();

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(size, 0));

  process_sp->ReadCStringFromMemory(
      options.GetLocation(), (char *)buffer_sp->GetBytes(), size, my_error);

  if (my_error.Fail())
    return false;

  const char *prefix_token = options.GetPrefixToken();
  char quote = options.GetQuote();

  if (prefix_token != 0)
    options.GetStream()->Printf("%s%c", prefix_token, quote);
  else if (quote != 0)
    options.GetStream()->Printf("%c", quote);

  uint8_t *data_end = buffer_sp->GetBytes() + buffer_sp->GetByteSize();

  const bool escape_non_printables = options.GetEscapeNonPrintables();
  lldb_private::formatters::StringPrinter::EscapingHelper escaping_callback;
  if (escape_non_printables) {
    if (Language *language = Language::FindPlugin(options.GetLanguage()))
      escaping_callback = language->GetStringPrinterEscapingHelper(
          lldb_private::formatters::StringPrinter::GetPrintableElementType::
              ASCII);
    else
      escaping_callback =
          lldb_private::formatters::StringPrinter::GetDefaultEscapingHelper(
              lldb_private::formatters::StringPrinter::GetPrintableElementType::
                  ASCII);
  }

  // since we tend to accept partial data (and even partially malformed data)
  // we might end up with no NULL terminator before the end_ptr hence we need
  // to take a slower route and ensure we stay within boundaries
  for (uint8_t *data = buffer_sp->GetBytes(); *data && (data < data_end);) {
    if (escape_non_printables) {
      uint8_t *next_data = nullptr;
      auto printable = escaping_callback(data, data_end, next_data);
      auto printable_bytes = printable.GetBytes();
      auto printable_size = printable.GetSize();
      if (!printable_bytes || !next_data) {
        // GetPrintable() failed on us - print one byte in a desperate resync
        // attempt
        printable_bytes = data;
        printable_size = 1;
        next_data = data + 1;
      }
      for (unsigned c = 0; c < printable_size; c++)
        options.GetStream()->Printf("%c", *(printable_bytes + c));
      data = (uint8_t *)next_data;
    } else {
      options.GetStream()->Printf("%c", *data);
      data++;
    }
  }

  const char *suffix_token = options.GetSuffixToken();

  if (suffix_token != 0)
    options.GetStream()->Printf("%c%s", quote, suffix_token);
  else if (quote != 0)
    options.GetStream()->Printf("%c", quote);

  if (is_truncated)
    options.GetStream()->Printf("...");

  return true;
}

template <typename SourceDataType>
static bool ReadUTFBufferAndDumpToStream(
    const StringPrinter::ReadStringAndDumpToStreamOptions &options,
    llvm::ConversionResult (*ConvertFunction)(const SourceDataType **,
                                              const SourceDataType *,
                                              llvm::UTF8 **, llvm::UTF8 *,
                                              llvm::ConversionFlags)) {
  assert(options.GetStream() && "need a Stream to print the string to");

  if (options.GetLocation() == 0 ||
      options.GetLocation() == LLDB_INVALID_ADDRESS)
    return false;

  lldb::ProcessSP process_sp(options.GetProcessSP());

  if (!process_sp)
    return false;

  const int type_width = sizeof(SourceDataType);
  const int origin_encoding = 8 * type_width;
  if (origin_encoding != 8 && origin_encoding != 16 && origin_encoding != 32)
    return false;
  // if not UTF8, I need a conversion function to return proper UTF8
  if (origin_encoding != 8 && !ConvertFunction)
    return false;

  if (!options.GetStream())
    return false;

  uint32_t sourceSize = options.GetSourceSize();
  bool needs_zero_terminator = options.GetNeedsZeroTermination();

  bool is_truncated = false;
  const auto max_size = process_sp->GetTarget().GetMaximumSizeOfStringSummary();

  if (!sourceSize) {
    sourceSize = max_size;
    needs_zero_terminator = true;
  } else if (!options.GetIgnoreMaxLength()) {
    if (sourceSize > max_size) {
      sourceSize = max_size;
      is_truncated = true;
    }
  }

  const int bufferSPSize = sourceSize * type_width;

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(bufferSPSize, 0));

  if (!buffer_sp->GetBytes())
    return false;

  Status error;
  char *buffer = reinterpret_cast<char *>(buffer_sp->GetBytes());

  if (needs_zero_terminator)
    process_sp->ReadStringFromMemory(options.GetLocation(), buffer,
                                     bufferSPSize, error, type_width);
  else
    process_sp->ReadMemoryFromInferior(options.GetLocation(),
                                       (char *)buffer_sp->GetBytes(),
                                       bufferSPSize, error);

  if (error.Fail()) {
    options.GetStream()->Printf("unable to read data");
    return true;
  }

  DataExtractor data(buffer_sp, process_sp->GetByteOrder(),
                     process_sp->GetAddressByteSize());

  StringPrinter::ReadBufferAndDumpToStreamOptions dump_options(options);
  dump_options.SetData(data);
  dump_options.SetSourceSize(sourceSize);
  dump_options.SetIsTruncated(is_truncated);

  return DumpUTFBufferToStream(ConvertFunction, dump_options);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<
    StringPrinter::StringElementType::UTF8>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadUTFBufferAndDumpToStream<llvm::UTF8>(options, nullptr);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<
    StringPrinter::StringElementType::UTF16>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadUTFBufferAndDumpToStream<llvm::UTF16>(options,
                                                   llvm::ConvertUTF16toUTF8);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<
    StringPrinter::StringElementType::UTF32>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadUTFBufferAndDumpToStream<llvm::UTF32>(options,
                                                   llvm::ConvertUTF32toUTF8);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<
    StringPrinter::StringElementType::UTF8>(
    const ReadBufferAndDumpToStreamOptions &options) {
  assert(options.GetStream() && "need a Stream to print the string to");

  return DumpUTFBufferToStream<llvm::UTF8>(nullptr, options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<
    StringPrinter::StringElementType::ASCII>(
    const ReadBufferAndDumpToStreamOptions &options) {
  // treat ASCII the same as UTF8
  // FIXME: can we optimize ASCII some more?
  return ReadBufferAndDumpToStream<StringElementType::UTF8>(options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<
    StringPrinter::StringElementType::UTF16>(
    const ReadBufferAndDumpToStreamOptions &options) {
  assert(options.GetStream() && "need a Stream to print the string to");

  return DumpUTFBufferToStream(llvm::ConvertUTF16toUTF8, options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<
    StringPrinter::StringElementType::UTF32>(
    const ReadBufferAndDumpToStreamOptions &options) {
  assert(options.GetStream() && "need a Stream to print the string to");

  return DumpUTFBufferToStream(llvm::ConvertUTF32toUTF8, options);
}

} // namespace formatters

} // namespace lldb_private
