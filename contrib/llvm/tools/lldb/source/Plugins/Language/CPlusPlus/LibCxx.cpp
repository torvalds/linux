//===-- LibCxx.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "llvm/ADT/ScopeExit.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/VectorIterator.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/CPPLanguageRuntime.h"
#include "lldb/Target/ProcessStructReader.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

bool lldb_private::formatters::LibcxxOptionalSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  // An optional either contains a value or not, the member __engaged_ is
  // a bool flag, it is true if the optional has a value and false otherwise.
  ValueObjectSP engaged_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__engaged_"), true));

  if (!engaged_sp)
    return false;

  llvm::StringRef engaged_as_cstring(
      engaged_sp->GetValueAsUnsigned(0) == 1 ? "true" : "false");

  stream.Printf(" Has Value=%s ", engaged_as_cstring.data());

  return true;
}

bool lldb_private::formatters::LibcxxFunctionSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {

  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());

  if (!valobj_sp)
    return false;

  ExecutionContext exe_ctx(valobj_sp->GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();

  if (process == nullptr)
    return false;

  CPPLanguageRuntime *cpp_runtime = process->GetCPPLanguageRuntime();

  if (!cpp_runtime)
    return false;

  CPPLanguageRuntime::LibCppStdFunctionCallableInfo callable_info =
      cpp_runtime->FindLibCppStdFunctionCallableInfo(valobj_sp);

  switch (callable_info.callable_case) {
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::Invalid:
    stream.Printf(" __f_ = %" PRIu64, callable_info.member__f_pointer_value);
    return false;
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::Lambda:
    stream.Printf(
        " Lambda in File %s at Line %u",
        callable_info.callable_line_entry.file.GetFilename().GetCString(),
        callable_info.callable_line_entry.line);
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::CallableObject:
    stream.Printf(
        " Function in File %s at Line %u",
        callable_info.callable_line_entry.file.GetFilename().GetCString(),
        callable_info.callable_line_entry.line);
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::FreeOrMemberFunction:
    stream.Printf(" Function = %s ",
                  callable_info.callable_symbol.GetName().GetCString());
    break;
  }

  return true;
}

bool lldb_private::formatters::LibcxxSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;
  ValueObjectSP ptr_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__ptr_"), true));
  ValueObjectSP count_sp(valobj_sp->GetChildAtNamePath(
      {ConstString("__cntrl_"), ConstString("__shared_owners_")}));
  ValueObjectSP weakcount_sp(valobj_sp->GetChildAtNamePath(
      {ConstString("__cntrl_"), ConstString("__shared_weak_owners_")}));

  if (!ptr_sp)
    return false;

  if (ptr_sp->GetValueAsUnsigned(0) == 0) {
    stream.Printf("nullptr");
    return true;
  } else {
    bool print_pointee = false;
    Status error;
    ValueObjectSP pointee_sp = ptr_sp->Dereference(error);
    if (pointee_sp && error.Success()) {
      if (pointee_sp->DumpPrintableRepresentation(
              stream, ValueObject::eValueObjectRepresentationStyleSummary,
              lldb::eFormatInvalid,
              ValueObject::PrintableRepresentationSpecialCases::eDisable,
              false))
        print_pointee = true;
    }
    if (!print_pointee)
      stream.Printf("ptr = 0x%" PRIx64, ptr_sp->GetValueAsUnsigned(0));
  }

  if (count_sp)
    stream.Printf(" strong=%" PRIu64, 1 + count_sp->GetValueAsUnsigned(0));

  if (weakcount_sp)
    stream.Printf(" weak=%" PRIu64, 1 + weakcount_sp->GetValueAsUnsigned(0));

  return true;
}

/*
 (lldb) fr var ibeg --raw --ptr-depth 1
 (std::__1::__map_iterator<std::__1::__tree_iterator<std::__1::pair<int,
 std::__1::basic_string<char, std::__1::char_traits<char>,
 std::__1::allocator<char> > >, std::__1::__tree_node<std::__1::pair<int,
 std::__1::basic_string<char, std::__1::char_traits<char>,
 std::__1::allocator<char> > >, void *> *, long> >) ibeg = {
 __i_ = {
 __ptr_ = 0x0000000100103870 {
 std::__1::__tree_node_base<void *> = {
 std::__1::__tree_end_node<std::__1::__tree_node_base<void *> *> = {
 __left_ = 0x0000000000000000
 }
 __right_ = 0x0000000000000000
 __parent_ = 0x00000001001038b0
 __is_black_ = true
 }
 __value_ = {
 first = 0
 second = { std::string }
 */

lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    LibCxxMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_pair_ptr(), m_pair_sp() {
  if (valobj_sp)
    Update();
}

bool lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::Update() {
  m_pair_sp.reset();
  m_pair_ptr = nullptr;

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;

  TargetSP target_sp(valobj_sp->GetTargetSP());

  if (!target_sp)
    return false;

  if (!valobj_sp)
    return false;

  static ConstString g___i_("__i_");

  // this must be a ValueObject* because it is a child of the ValueObject we
  // are producing children for it if were a ValueObjectSP, we would end up
  // with a loop (iterator -> synthetic -> child -> parent == iterator) and
  // that would in turn leak memory by never allowing the ValueObjects to die
  // and free their memory
  m_pair_ptr = valobj_sp
                   ->GetValueForExpressionPath(
                       ".__i_.__ptr_->__value_", nullptr, nullptr,
                       ValueObject::GetValueForExpressionPathOptions()
                           .DontCheckDotVsArrowSyntax()
                           .SetSyntheticChildrenTraversal(
                               ValueObject::GetValueForExpressionPathOptions::
                                   SyntheticChildrenTraversal::None),
                       nullptr)
                   .get();

  if (!m_pair_ptr) {
    m_pair_ptr = valobj_sp
                     ->GetValueForExpressionPath(
                         ".__i_.__ptr_", nullptr, nullptr,
                         ValueObject::GetValueForExpressionPathOptions()
                             .DontCheckDotVsArrowSyntax()
                             .SetSyntheticChildrenTraversal(
                                 ValueObject::GetValueForExpressionPathOptions::
                                     SyntheticChildrenTraversal::None),
                         nullptr)
                     .get();
    if (m_pair_ptr) {
      auto __i_(valobj_sp->GetChildMemberWithName(g___i_, true));
      if (!__i_) {
        m_pair_ptr = nullptr;
        return false;
      }
      CompilerType pair_type(
          __i_->GetCompilerType().GetTypeTemplateArgument(0));
      std::string name;
      uint64_t bit_offset_ptr;
      uint32_t bitfield_bit_size_ptr;
      bool is_bitfield_ptr;
      pair_type = pair_type.GetFieldAtIndex(
          0, name, &bit_offset_ptr, &bitfield_bit_size_ptr, &is_bitfield_ptr);
      if (!pair_type) {
        m_pair_ptr = nullptr;
        return false;
      }

      auto addr(m_pair_ptr->GetValueAsUnsigned(LLDB_INVALID_ADDRESS));
      m_pair_ptr = nullptr;
      if (addr && addr != LLDB_INVALID_ADDRESS) {
        ClangASTContext *ast_ctx =
            llvm::dyn_cast_or_null<ClangASTContext>(pair_type.GetTypeSystem());
        if (!ast_ctx)
          return false;
        CompilerType tree_node_type = ast_ctx->CreateStructForIdentifier(
            ConstString(),
            {{"ptr0",
              ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
             {"ptr1",
              ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
             {"ptr2",
              ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
             {"cw", ast_ctx->GetBasicType(lldb::eBasicTypeBool)},
             {"payload", pair_type}});
        llvm::Optional<uint64_t> size = tree_node_type.GetByteSize(nullptr);
        if (!size)
          return false;
        DataBufferSP buffer_sp(new DataBufferHeap(*size, 0));
        ProcessSP process_sp(target_sp->GetProcessSP());
        Status error;
        process_sp->ReadMemory(addr, buffer_sp->GetBytes(),
                               buffer_sp->GetByteSize(), error);
        if (error.Fail())
          return false;
        DataExtractor extractor(buffer_sp, process_sp->GetByteOrder(),
                                process_sp->GetAddressByteSize());
        auto pair_sp = CreateValueObjectFromData(
            "pair", extractor, valobj_sp->GetExecutionContextRef(),
            tree_node_type);
        if (pair_sp)
          m_pair_sp = pair_sp->GetChildAtIndex(4, true);
      }
    }
  }

  return false;
}

size_t lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    CalculateNumChildren() {
  return 2;
}

lldb::ValueObjectSP
lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  if (m_pair_ptr)
    return m_pair_ptr->GetChildAtIndex(idx, true);
  if (m_pair_sp)
    return m_pair_sp->GetChildAtIndex(idx, true);
  return lldb::ValueObjectSP();
}

bool lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  if (name == ConstString("first"))
    return 0;
  if (name == ConstString("second"))
    return 1;
  return UINT32_MAX;
}

lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    ~LibCxxMapIteratorSyntheticFrontEnd() {
  // this will be deleted when its parent dies (since it's a child object)
  // delete m_pair_ptr;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibCxxMapIteratorSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

/*
 (lldb) fr var ibeg --raw --ptr-depth 1 -T
 (std::__1::__wrap_iter<int *>) ibeg = {
 (std::__1::__wrap_iter<int *>::iterator_type) __i = 0x00000001001037a0 {
 (int) *__i = 1
 }
 }
*/

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibCxxVectorIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  static ConstString g_item_name;
  if (!g_item_name)
    g_item_name.SetCString("__i");
  return (valobj_sp
              ? new VectorIteratorSyntheticFrontEnd(valobj_sp, g_item_name)
              : nullptr);
}

lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    LibcxxSharedPtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_cntrl(nullptr), m_count_sp(),
      m_weak_count_sp(), m_ptr_size(0), m_byte_order(lldb::eByteOrderInvalid) {
  if (valobj_sp)
    Update();
}

size_t lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    CalculateNumChildren() {
  return (m_cntrl ? 1 : 0);
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  if (!m_cntrl)
    return lldb::ValueObjectSP();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ValueObjectSP();

  if (idx == 0)
    return valobj_sp->GetChildMemberWithName(ConstString("__ptr_"), true);

  if (idx > 2)
    return lldb::ValueObjectSP();

  if (idx == 1) {
    if (!m_count_sp) {
      ValueObjectSP shared_owners_sp(m_cntrl->GetChildMemberWithName(
          ConstString("__shared_owners_"), true));
      if (!shared_owners_sp)
        return lldb::ValueObjectSP();
      uint64_t count = 1 + shared_owners_sp->GetValueAsUnsigned(0);
      DataExtractor data(&count, 8, m_byte_order, m_ptr_size);
      m_count_sp = CreateValueObjectFromData(
          "count", data, valobj_sp->GetExecutionContextRef(),
          shared_owners_sp->GetCompilerType());
    }
    return m_count_sp;
  } else /* if (idx == 2) */
  {
    if (!m_weak_count_sp) {
      ValueObjectSP shared_weak_owners_sp(m_cntrl->GetChildMemberWithName(
          ConstString("__shared_weak_owners_"), true));
      if (!shared_weak_owners_sp)
        return lldb::ValueObjectSP();
      uint64_t count = 1 + shared_weak_owners_sp->GetValueAsUnsigned(0);
      DataExtractor data(&count, 8, m_byte_order, m_ptr_size);
      m_weak_count_sp = CreateValueObjectFromData(
          "count", data, valobj_sp->GetExecutionContextRef(),
          shared_weak_owners_sp->GetCompilerType());
    }
    return m_weak_count_sp;
  }
}

bool lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::Update() {
  m_count_sp.reset();
  m_weak_count_sp.reset();
  m_cntrl = nullptr;

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;

  TargetSP target_sp(valobj_sp->GetTargetSP());
  if (!target_sp)
    return false;

  m_byte_order = target_sp->GetArchitecture().GetByteOrder();
  m_ptr_size = target_sp->GetArchitecture().GetAddressByteSize();

  lldb::ValueObjectSP cntrl_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__cntrl_"), true));

  m_cntrl = cntrl_sp.get(); // need to store the raw pointer to avoid a circular
                            // dependency
  return false;
}

bool lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  if (name == ConstString("__ptr_"))
    return 0;
  if (name == ConstString("count"))
    return 1;
  if (name == ConstString("weak_count"))
    return 2;
  return UINT32_MAX;
}

lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    ~LibcxxSharedPtrSyntheticFrontEnd() = default;

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxSharedPtrSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

bool lldb_private::formatters::LibcxxContainerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  if (valobj.IsPointerType()) {
    uint64_t value = valobj.GetValueAsUnsigned(0);
    if (!value)
      return false;
    stream.Printf("0x%016" PRIx64 " ", value);
  }
  return FormatEntity::FormatStringRef("size=${svar%#}", stream, nullptr,
                                       nullptr, nullptr, &valobj, false, false);
}

// the field layout in a libc++ string (cap, side, data or data, size, cap)
enum LibcxxStringLayoutMode {
  eLibcxxStringLayoutModeCSD = 0,
  eLibcxxStringLayoutModeDSC = 1,
  eLibcxxStringLayoutModeInvalid = 0xffff
};

// this function abstracts away the layout and mode details of a libc++ string
// and returns the address of the data and the size ready for callers to
// consume
static bool ExtractLibcxxStringInfo(ValueObject &valobj,
                                    ValueObjectSP &location_sp,
                                    uint64_t &size) {
  ValueObjectSP D(valobj.GetChildAtIndexPath({0, 0, 0, 0}));
  if (!D)
    return false;

  ValueObjectSP layout_decider(
    D->GetChildAtIndexPath(llvm::ArrayRef<size_t>({0, 0})));

  // this child should exist
  if (!layout_decider)
    return false;

  ConstString g_data_name("__data_");
  ConstString g_size_name("__size_");
  bool short_mode = false; // this means the string is in short-mode and the
                           // data is stored inline
  LibcxxStringLayoutMode layout = (layout_decider->GetName() == g_data_name)
                                      ? eLibcxxStringLayoutModeDSC
                                      : eLibcxxStringLayoutModeCSD;
  uint64_t size_mode_value = 0;

  if (layout == eLibcxxStringLayoutModeDSC) {
    ValueObjectSP size_mode(D->GetChildAtIndexPath({1, 1, 0}));
    if (!size_mode)
      return false;

    if (size_mode->GetName() != g_size_name) {
      // we are hitting the padding structure, move along
      size_mode = D->GetChildAtIndexPath({1, 1, 1});
      if (!size_mode)
        return false;
    }

    size_mode_value = (size_mode->GetValueAsUnsigned(0));
    short_mode = ((size_mode_value & 0x80) == 0);
  } else {
    ValueObjectSP size_mode(D->GetChildAtIndexPath({1, 0, 0}));
    if (!size_mode)
      return false;

    size_mode_value = (size_mode->GetValueAsUnsigned(0));
    short_mode = ((size_mode_value & 1) == 0);
  }

  if (short_mode) {
    ValueObjectSP s(D->GetChildAtIndex(1, true));
    if (!s)
      return false;
    location_sp = s->GetChildAtIndex(
        (layout == eLibcxxStringLayoutModeDSC) ? 0 : 1, true);
    size = (layout == eLibcxxStringLayoutModeDSC)
               ? size_mode_value
               : ((size_mode_value >> 1) % 256);
    return (location_sp.get() != nullptr);
  } else {
    ValueObjectSP l(D->GetChildAtIndex(0, true));
    if (!l)
      return false;
    // we can use the layout_decider object as the data pointer
    location_sp = (layout == eLibcxxStringLayoutModeDSC)
                      ? layout_decider
                      : l->GetChildAtIndex(2, true);
    ValueObjectSP size_vo(l->GetChildAtIndex(1, true));
    if (!size_vo || !location_sp)
      return false;
    size = size_vo->GetValueAsUnsigned(0);
    return true;
  }
}

bool lldb_private::formatters::LibcxxWStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  uint64_t size = 0;
  ValueObjectSP location_sp;
  if (!ExtractLibcxxStringInfo(valobj, location_sp, size))
    return false;
  if (size == 0) {
    stream.Printf("L\"\"");
    return true;
  }
  if (!location_sp)
    return false;

  DataExtractor extractor;

  StringPrinter::ReadBufferAndDumpToStreamOptions options(valobj);

  if (summary_options.GetCapping() == TypeSummaryCapping::eTypeSummaryCapped) {
    const auto max_size = valobj.GetTargetSP()->GetMaximumSizeOfStringSummary();
    if (size > max_size) {
      size = max_size;
      options.SetIsTruncated(true);
    }
  }
  location_sp->GetPointeeData(extractor, 0, size);

  // std::wstring::size() is measured in 'characters', not bytes
  auto wchar_t_size = valobj.GetTargetSP()
                          ->GetScratchClangASTContext()
                          ->GetBasicType(lldb::eBasicTypeWChar)
                          .GetByteSize(nullptr);
  if (!wchar_t_size)
    return false;

  options.SetData(extractor);
  options.SetStream(&stream);
  options.SetPrefixToken("L");
  options.SetQuote('"');
  options.SetSourceSize(size);
  options.SetBinaryZeroIsTerminator(false);

  switch (*wchar_t_size) {
  case 1:
    StringPrinter::ReadBufferAndDumpToStream<
        lldb_private::formatters::StringPrinter::StringElementType::UTF8>(
        options);
    break;

  case 2:
    lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStream<
        lldb_private::formatters::StringPrinter::StringElementType::UTF16>(
        options);
    break;

  case 4:
    lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStream<
        lldb_private::formatters::StringPrinter::StringElementType::UTF32>(
        options);
    break;

  default:
    stream.Printf("size for wchar_t is not valid");
    return true;
  }

  return true;
}

template <StringPrinter::StringElementType element_type>
bool LibcxxStringSummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &summary_options,
                                 std::string prefix_token = "") {
  uint64_t size = 0;
  ValueObjectSP location_sp;

  if (!ExtractLibcxxStringInfo(valobj, location_sp, size))
    return false;

  if (size == 0) {
    stream.Printf("\"\"");
    return true;
  }

  if (!location_sp)
    return false;

  StringPrinter::ReadBufferAndDumpToStreamOptions options(valobj);

  DataExtractor extractor;
  if (summary_options.GetCapping() == TypeSummaryCapping::eTypeSummaryCapped) {
    const auto max_size = valobj.GetTargetSP()->GetMaximumSizeOfStringSummary();
    if (size > max_size) {
      size = max_size;
      options.SetIsTruncated(true);
    }
  }
  location_sp->GetPointeeData(extractor, 0, size);

  options.SetData(extractor);
  options.SetStream(&stream);

  if (prefix_token.empty())
    options.SetPrefixToken(nullptr);
  else
    options.SetPrefixToken(prefix_token);

  options.SetQuote('"');
  options.SetSourceSize(size);
  options.SetBinaryZeroIsTerminator(false);
  StringPrinter::ReadBufferAndDumpToStream<element_type>(options);

  return true;
}

bool lldb_private::formatters::LibcxxStringSummaryProviderASCII(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return LibcxxStringSummaryProvider<StringPrinter::StringElementType::ASCII>(
      valobj, stream, summary_options);
}

bool lldb_private::formatters::LibcxxStringSummaryProviderUTF16(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return LibcxxStringSummaryProvider<StringPrinter::StringElementType::UTF16>(
      valobj, stream, summary_options, "u");
}

bool lldb_private::formatters::LibcxxStringSummaryProviderUTF32(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return LibcxxStringSummaryProvider<StringPrinter::StringElementType::UTF32>(
      valobj, stream, summary_options, "U");
}
