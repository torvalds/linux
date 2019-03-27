//===-- CxxStringTypes.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CxxStringTypes.h"

#include "llvm/Support/ConvertUTF.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Host/Time.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ProcessStructReader.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include <algorithm>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

bool lldb_private::formatters::Char16StringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  lldb::addr_t valobj_addr = GetArrayAddressOrPointerValue(valobj);
  if (valobj_addr == 0 || valobj_addr == LLDB_INVALID_ADDRESS)
    return false;

  StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
  options.SetLocation(valobj_addr);
  options.SetProcessSP(process_sp);
  options.SetStream(&stream);
  options.SetPrefixToken("u");

  if (!StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::UTF16>(options)) {
    stream.Printf("Summary Unavailable");
    return true;
  }

  return true;
}

bool lldb_private::formatters::Char32StringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  lldb::addr_t valobj_addr = GetArrayAddressOrPointerValue(valobj);
  if (valobj_addr == 0 || valobj_addr == LLDB_INVALID_ADDRESS)
    return false;

  StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
  options.SetLocation(valobj_addr);
  options.SetProcessSP(process_sp);
  options.SetStream(&stream);
  options.SetPrefixToken("U");

  if (!StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::UTF32>(options)) {
    stream.Printf("Summary Unavailable");
    return true;
  }

  return true;
}

bool lldb_private::formatters::WCharStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  lldb::addr_t valobj_addr = GetArrayAddressOrPointerValue(valobj);
  if (valobj_addr == 0 || valobj_addr == LLDB_INVALID_ADDRESS)
    return false;

  // Get a wchar_t basic type from the current type system
  CompilerType wchar_compiler_type =
      valobj.GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeWChar);

  if (!wchar_compiler_type)
    return false;

  // Safe to pass nullptr for exe_scope here.
  llvm::Optional<uint64_t> size = wchar_compiler_type.GetBitSize(nullptr);
  if (!size)
    return false;
  const uint32_t wchar_size = *size;

  StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
  options.SetLocation(valobj_addr);
  options.SetProcessSP(process_sp);
  options.SetStream(&stream);
  options.SetPrefixToken("L");

  switch (wchar_size) {
  case 8:
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::UTF8>(options);
  case 16:
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::UTF16>(options);
  case 32:
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::UTF32>(options);
  default:
    stream.Printf("size for wchar_t is not valid");
    return true;
  }
  return true;
}

bool lldb_private::formatters::Char16SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  DataExtractor data;
  Status error;
  valobj.GetData(data, error);

  if (error.Fail())
    return false;

  std::string value;
  valobj.GetValueAsCString(lldb::eFormatUnicode16, value);
  if (!value.empty())
    stream.Printf("%s ", value.c_str());

  StringPrinter::ReadBufferAndDumpToStreamOptions options(valobj);
  options.SetData(data);
  options.SetStream(&stream);
  options.SetPrefixToken("u");
  options.SetQuote('\'');
  options.SetSourceSize(1);
  options.SetBinaryZeroIsTerminator(false);

  return StringPrinter::ReadBufferAndDumpToStream<
      StringPrinter::StringElementType::UTF16>(options);
}

bool lldb_private::formatters::Char32SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  DataExtractor data;
  Status error;
  valobj.GetData(data, error);

  if (error.Fail())
    return false;

  std::string value;
  valobj.GetValueAsCString(lldb::eFormatUnicode32, value);
  if (!value.empty())
    stream.Printf("%s ", value.c_str());

  StringPrinter::ReadBufferAndDumpToStreamOptions options(valobj);
  options.SetData(data);
  options.SetStream(&stream);
  options.SetPrefixToken("U");
  options.SetQuote('\'');
  options.SetSourceSize(1);
  options.SetBinaryZeroIsTerminator(false);

  return StringPrinter::ReadBufferAndDumpToStream<
      StringPrinter::StringElementType::UTF32>(options);
}

bool lldb_private::formatters::WCharSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &) {
  DataExtractor data;
  Status error;
  valobj.GetData(data, error);

  if (error.Fail())
    return false;

  // Get a wchar_t basic type from the current type system
  CompilerType wchar_compiler_type =
      valobj.GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeWChar);

  if (!wchar_compiler_type)
    return false;

    // Safe to pass nullptr for exe_scope here.
  llvm::Optional<uint64_t> size = wchar_compiler_type.GetBitSize(nullptr);
  if (!size)
    return false;
  const uint32_t wchar_size = *size;

  StringPrinter::ReadBufferAndDumpToStreamOptions options(valobj);
  options.SetData(data);
  options.SetStream(&stream);
  options.SetPrefixToken("L");
  options.SetQuote('\'');
  options.SetSourceSize(1);
  options.SetBinaryZeroIsTerminator(false);

  switch (wchar_size) {
  case 8:
    return StringPrinter::ReadBufferAndDumpToStream<
        StringPrinter::StringElementType::UTF8>(options);
  case 16:
    return StringPrinter::ReadBufferAndDumpToStream<
        StringPrinter::StringElementType::UTF16>(options);
  case 32:
    return StringPrinter::ReadBufferAndDumpToStream<
        StringPrinter::StringElementType::UTF32>(options);
  default:
    stream.Printf("size for wchar_t is not valid");
    return true;
  }
  return true;
}
