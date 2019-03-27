//===-- CPlusPlusLanguage.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CPlusPlusLanguage.h"

#include <cctype>
#include <cstring>

#include <functional>
#include <memory>
#include <mutex>
#include <set>

#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/ItaniumDemangle.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/DataFormatters/CXXFunctionPointer.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/VectorType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"

#include "BlockPointer.h"
#include "CPlusPlusNameParser.h"
#include "CxxStringTypes.h"
#include "LibCxx.h"
#include "LibCxxAtomic.h"
#include "LibCxxVariant.h"
#include "LibStdcpp.h"
#include "MSVCUndecoratedNameParser.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

void CPlusPlusLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "C++ Language",
                                CreateInstance);
}

void CPlusPlusLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString CPlusPlusLanguage::GetPluginNameStatic() {
  static ConstString g_name("cplusplus");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString CPlusPlusLanguage::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t CPlusPlusLanguage::GetPluginVersion() { return 1; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

Language *CPlusPlusLanguage::CreateInstance(lldb::LanguageType language) {
  if (Language::LanguageIsCPlusPlus(language))
    return new CPlusPlusLanguage();
  return nullptr;
}

void CPlusPlusLanguage::MethodName::Clear() {
  m_full.Clear();
  m_basename = llvm::StringRef();
  m_context = llvm::StringRef();
  m_arguments = llvm::StringRef();
  m_qualifiers = llvm::StringRef();
  m_parsed = false;
  m_parse_error = false;
}

static bool ReverseFindMatchingChars(const llvm::StringRef &s,
                                     const llvm::StringRef &left_right_chars,
                                     size_t &left_pos, size_t &right_pos,
                                     size_t pos = llvm::StringRef::npos) {
  assert(left_right_chars.size() == 2);
  left_pos = llvm::StringRef::npos;
  const char left_char = left_right_chars[0];
  const char right_char = left_right_chars[1];
  pos = s.find_last_of(left_right_chars, pos);
  if (pos == llvm::StringRef::npos || s[pos] == left_char)
    return false;
  right_pos = pos;
  uint32_t depth = 1;
  while (pos > 0 && depth > 0) {
    pos = s.find_last_of(left_right_chars, pos);
    if (pos == llvm::StringRef::npos)
      return false;
    if (s[pos] == left_char) {
      if (--depth == 0) {
        left_pos = pos;
        return left_pos < right_pos;
      }
    } else if (s[pos] == right_char) {
      ++depth;
    }
  }
  return false;
}

static bool IsTrivialBasename(const llvm::StringRef &basename) {
  // Check that the basename matches with the following regular expression
  // "^~?([A-Za-z_][A-Za-z_0-9]*)$" We are using a hand written implementation
  // because it is significantly more efficient then using the general purpose
  // regular expression library.
  size_t idx = 0;
  if (basename.size() > 0 && basename[0] == '~')
    idx = 1;

  if (basename.size() <= idx)
    return false; // Empty string or "~"

  if (!std::isalpha(basename[idx]) && basename[idx] != '_')
    return false; // First charater (after removing the possible '~'') isn't in
                  // [A-Za-z_]

  // Read all characters matching [A-Za-z_0-9]
  ++idx;
  while (idx < basename.size()) {
    if (!std::isalnum(basename[idx]) && basename[idx] != '_')
      break;
    ++idx;
  }

  // We processed all characters. It is a vaild basename.
  return idx == basename.size();
}

bool CPlusPlusLanguage::MethodName::TrySimplifiedParse() {
  // This method tries to parse simple method definitions which are presumably
  // most comman in user programs. Definitions that can be parsed by this
  // function don't have return types and templates in the name.
  // A::B::C::fun(std::vector<T> &) const
  size_t arg_start, arg_end;
  llvm::StringRef full(m_full.GetCString());
  llvm::StringRef parens("()", 2);
  if (ReverseFindMatchingChars(full, parens, arg_start, arg_end)) {
    m_arguments = full.substr(arg_start, arg_end - arg_start + 1);
    if (arg_end + 1 < full.size())
      m_qualifiers = full.substr(arg_end + 1).ltrim();

    if (arg_start == 0)
      return false;
    size_t basename_end = arg_start;
    size_t context_start = 0;
    size_t context_end = full.rfind(':', basename_end);
    if (context_end == llvm::StringRef::npos)
      m_basename = full.substr(0, basename_end);
    else {
      if (context_start < context_end)
        m_context = full.substr(context_start, context_end - 1 - context_start);
      const size_t basename_begin = context_end + 1;
      m_basename = full.substr(basename_begin, basename_end - basename_begin);
    }

    if (IsTrivialBasename(m_basename)) {
      return true;
    } else {
      // The C++ basename doesn't match our regular expressions so this can't
      // be a valid C++ method, clear everything out and indicate an error
      m_context = llvm::StringRef();
      m_basename = llvm::StringRef();
      m_arguments = llvm::StringRef();
      m_qualifiers = llvm::StringRef();
      return false;
    }
  }
  return false;
}

void CPlusPlusLanguage::MethodName::Parse() {
  if (!m_parsed && m_full) {
    if (TrySimplifiedParse()) {
      m_parse_error = false;
    } else {
      CPlusPlusNameParser parser(m_full.GetStringRef());
      if (auto function = parser.ParseAsFunctionDefinition()) {
        m_basename = function.getValue().name.basename;
        m_context = function.getValue().name.context;
        m_arguments = function.getValue().arguments;
        m_qualifiers = function.getValue().qualifiers;
        m_parse_error = false;
      } else {
        m_parse_error = true;
      }
    }
    m_parsed = true;
  }
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetBasename() {
  if (!m_parsed)
    Parse();
  return m_basename;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetContext() {
  if (!m_parsed)
    Parse();
  return m_context;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetArguments() {
  if (!m_parsed)
    Parse();
  return m_arguments;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetQualifiers() {
  if (!m_parsed)
    Parse();
  return m_qualifiers;
}

std::string CPlusPlusLanguage::MethodName::GetScopeQualifiedName() {
  if (!m_parsed)
    Parse();
  if (m_context.empty())
    return m_basename;

  std::string res;
  res += m_context;
  res += "::";
  res += m_basename;
  return res;
}

bool CPlusPlusLanguage::IsCPPMangledName(const char *name) {
  // FIXME!! we should really run through all the known C++ Language plugins
  // and ask each one if this is a C++ mangled name

  if (name == nullptr)
    return false;

  // MSVC style mangling
  if (name[0] == '?')
    return true;

  return (name[0] != '\0' && name[0] == '_' && name[1] == 'Z');
}

bool CPlusPlusLanguage::ExtractContextAndIdentifier(
    const char *name, llvm::StringRef &context, llvm::StringRef &identifier) {
  if (MSVCUndecoratedNameParser::IsMSVCUndecoratedName(name))
    return MSVCUndecoratedNameParser::ExtractContextAndIdentifier(name, context,
                                                                  identifier);

  CPlusPlusNameParser parser(name);
  if (auto full_name = parser.ParseAsFullName()) {
    identifier = full_name.getValue().basename;
    context = full_name.getValue().context;
    return true;
  }
  return false;
}

namespace {
class NodeAllocator {
  llvm::BumpPtrAllocator Alloc;

public:
  void reset() { Alloc.Reset(); }

  template <typename T, typename... Args> T *makeNode(Args &&... args) {
    return new (Alloc.Allocate(sizeof(T), alignof(T)))
        T(std::forward<Args>(args)...);
  }

  void *allocateNodeArray(size_t sz) {
    return Alloc.Allocate(sizeof(llvm::itanium_demangle::Node *) * sz,
                          alignof(llvm::itanium_demangle::Node *));
  }
};

/// Given a mangled function `Mangled`, replace all the primitive function type
/// arguments of `Search` with type `Replace`.
class TypeSubstitutor
    : public llvm::itanium_demangle::AbstractManglingParser<TypeSubstitutor,
                                                            NodeAllocator> {
  /// Input character until which we have constructed the respective output
  /// already
  const char *Written;

  llvm::StringRef Search;
  llvm::StringRef Replace;
  llvm::SmallString<128> Result;

  /// Whether we have performed any substitutions.
  bool Substituted;

  void reset(llvm::StringRef Mangled, llvm::StringRef Search,
             llvm::StringRef Replace) {
    AbstractManglingParser::reset(Mangled.begin(), Mangled.end());
    Written = Mangled.begin();
    this->Search = Search;
    this->Replace = Replace;
    Result.clear();
    Substituted = false;
  }

  void appendUnchangedInput() {
    Result += llvm::StringRef(Written, First - Written);
    Written = First;
  }

public:
  TypeSubstitutor() : AbstractManglingParser(nullptr, nullptr) {}

  ConstString substitute(llvm::StringRef Mangled, llvm::StringRef From,
                         llvm::StringRef To) {
    Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

    reset(Mangled, From, To);
    if (parse() == nullptr) {
      LLDB_LOG(log, "Failed to substitute mangling in {0}", Mangled);
      return ConstString();
    }
    if (!Substituted)
      return ConstString();

    // Append any trailing unmodified input.
    appendUnchangedInput();
    LLDB_LOG(log, "Substituted mangling {0} -> {1}", Mangled, Result);
    return ConstString(Result);
  }

  llvm::itanium_demangle::Node *parseType() {
    if (llvm::StringRef(First, numLeft()).startswith(Search)) {
      // We found a match. Append unmodified input up to this point.
      appendUnchangedInput();

      // And then perform the replacement.
      Result += Replace;
      Written += Search.size();
      Substituted = true;
    }
    return AbstractManglingParser::parseType();
  }
};
}

uint32_t CPlusPlusLanguage::FindAlternateFunctionManglings(
    const ConstString mangled_name, std::set<ConstString> &alternates) {
  const auto start_size = alternates.size();
  /// Get a basic set of alternative manglings for the given symbol `name`, by
  /// making a few basic possible substitutions on basic types, storage duration
  /// and `const`ness for the given symbol. The output parameter `alternates`
  /// is filled with a best-guess, non-exhaustive set of different manglings
  /// for the given name.

  // Maybe we're looking for a const symbol but the debug info told us it was
  // non-const...
  if (!strncmp(mangled_name.GetCString(), "_ZN", 3) &&
      strncmp(mangled_name.GetCString(), "_ZNK", 4)) {
    std::string fixed_scratch("_ZNK");
    fixed_scratch.append(mangled_name.GetCString() + 3);
    alternates.insert(ConstString(fixed_scratch));
  }

  // Maybe we're looking for a static symbol but we thought it was global...
  if (!strncmp(mangled_name.GetCString(), "_Z", 2) &&
      strncmp(mangled_name.GetCString(), "_ZL", 3)) {
    std::string fixed_scratch("_ZL");
    fixed_scratch.append(mangled_name.GetCString() + 2);
    alternates.insert(ConstString(fixed_scratch));
  }

  TypeSubstitutor TS;
  // `char` is implementation defined as either `signed` or `unsigned`.  As a
  // result a char parameter has 3 possible manglings: 'c'-char, 'a'-signed
  // char, 'h'-unsigned char.  If we're looking for symbols with a signed char
  // parameter, try finding matches which have the general case 'c'.
  if (ConstString char_fixup =
          TS.substitute(mangled_name.GetStringRef(), "a", "c"))
    alternates.insert(char_fixup);

  // long long parameter mangling 'x', may actually just be a long 'l' argument
  if (ConstString long_fixup =
          TS.substitute(mangled_name.GetStringRef(), "x", "l"))
    alternates.insert(long_fixup);

  // unsigned long long parameter mangling 'y', may actually just be unsigned
  // long 'm' argument
  if (ConstString ulong_fixup =
          TS.substitute(mangled_name.GetStringRef(), "y", "m"))
    alternates.insert(ulong_fixup);

  return alternates.size() - start_size;
}

static void LoadLibCxxFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags stl_summary_flags;
  stl_summary_flags.SetCascades(true)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

#ifndef LLDB_DISABLE_PYTHON
  lldb::TypeSummaryImplSP std_string_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags,
      lldb_private::formatters::LibcxxStringSummaryProviderASCII,
      "std::string summary provider"));
  lldb::TypeSummaryImplSP std_stringu16_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags,
      lldb_private::formatters::LibcxxStringSummaryProviderUTF16,
      "std::u16string summary provider"));
  lldb::TypeSummaryImplSP std_stringu32_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags,
      lldb_private::formatters::LibcxxStringSummaryProviderUTF32,
      "std::u32string summary provider"));
  lldb::TypeSummaryImplSP std_wstring_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags, lldb_private::formatters::LibcxxWStringSummaryProvider,
      "std::wstring summary provider"));

  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__1::string"), std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__ndk1::string"), std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__1::basic_string<char, std::__1::char_traits<char>, "
                  "std::__1::allocator<char> >"),
      std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString(
          "std::__1::basic_string<char16_t, std::__1::char_traits<char16_t>, "
          "std::__1::allocator<char16_t> >"),
      std_stringu16_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString(
          "std::__1::basic_string<char32_t, std::__1::char_traits<char32_t>, "
          "std::__1::allocator<char32_t> >"),
      std_stringu32_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__ndk1::basic_string<char, "
                  "std::__ndk1::char_traits<char>, "
                  "std::__ndk1::allocator<char> >"),
      std_string_summary_sp);

  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__1::wstring"), std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__ndk1::wstring"), std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__1::basic_string<wchar_t, "
                  "std::__1::char_traits<wchar_t>, "
                  "std::__1::allocator<wchar_t> >"),
      std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__ndk1::basic_string<wchar_t, "
                  "std::__ndk1::char_traits<wchar_t>, "
                  "std::__ndk1::allocator<wchar_t> >"),
      std_wstring_summary_sp);

  SyntheticChildren::Flags stl_synth_flags;
  stl_synth_flags.SetCascades(true).SetSkipPointers(false).SetSkipReferences(
      false);
  SyntheticChildren::Flags stl_deref_flags = stl_synth_flags;
  stl_deref_flags.SetFrontEndWantsDereference();

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxBitsetSyntheticFrontEndCreator,
      "libc++ std::bitset synthetic children",
      ConstString("^std::__(ndk)?1::bitset<.+>(( )?&)?$"), stl_deref_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdVectorSyntheticFrontEndCreator,
      "libc++ std::vector synthetic children",
      ConstString("^std::__(ndk)?1::vector<.+>(( )?&)?$"), stl_deref_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdForwardListSyntheticFrontEndCreator,
      "libc++ std::forward_list synthetic children",
      ConstString("^std::__(ndk)?1::forward_list<.+>(( )?&)?$"),
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdListSyntheticFrontEndCreator,
      "libc++ std::list synthetic children",
      ConstString("^std::__(ndk)?1::list<.+>(( )?&)?$"), stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::map synthetic children",
      ConstString("^std::__(ndk)?1::map<.+> >(( )?&)?$"), stl_synth_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::set synthetic children",
      ConstString("^std::__(ndk)?1::set<.+> >(( )?&)?$"), stl_deref_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::multiset synthetic children",
      ConstString("^std::__(ndk)?1::multiset<.+> >(( )?&)?$"), stl_deref_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::multimap synthetic children",
      ConstString("^std::__(ndk)?1::multimap<.+> >(( )?&)?$"), stl_synth_flags,
      true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEndCreator,
      "libc++ std::unordered containers synthetic children",
      ConstString("^(std::__(ndk)?1::)unordered_(multi)?(map|set)<.+> >$"),
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxInitializerListSyntheticFrontEndCreator,
      "libc++ std::initializer_list synthetic children",
      ConstString("^std::initializer_list<.+>(( )?&)?$"), stl_synth_flags,
      true);
  AddCXXSynthetic(cpp_category_sp, LibcxxQueueFrontEndCreator,
                  "libc++ std::queue synthetic children",
                  ConstString("^std::__(ndk)?1::queue<.+>(( )?&)?$"),
                  stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxTupleFrontEndCreator,
                  "libc++ std::tuple synthetic children",
                  ConstString("^std::__(ndk)?1::tuple<.*>(( )?&)?$"), stl_synth_flags,
                  true);
  AddCXXSynthetic(cpp_category_sp, LibcxxOptionalFrontEndCreator,
                  "libc++ std::optional synthetic children",
                  ConstString("^std::__(ndk)?1::optional<.+>(( )?&)?$"),
                  stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxVariantFrontEndCreator,
                  "libc++ std::variant synthetic children",
                  ConstString("^std::__(ndk)?1::variant<.+>(( )?&)?$"),
                  stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxAtomicSyntheticFrontEndCreator,
      "libc++ std::atomic synthetic children",
      ConstString("^std::__(ndk)?1::atomic<.+>$"), stl_synth_flags, true);

  cpp_category_sp->GetRegexTypeSyntheticsContainer()->Add(
      RegularExpressionSP(new RegularExpression(
          llvm::StringRef("^(std::__(ndk)?1::)deque<.+>(( )?&)?$"))),
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.libcxx.stddeque_SynthProvider")));

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator,
      "shared_ptr synthetic children",
      ConstString("^(std::__(ndk)?1::)shared_ptr<.+>(( )?&)?$"),
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator,
      "weak_ptr synthetic children",
      ConstString("^(std::__(ndk)?1::)weak_ptr<.+>(( )?&)?$"), stl_synth_flags,
      true);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::LibcxxFunctionSummaryProvider,
      "libc++ std::function summary provider",
      ConstString("^std::__(ndk)?1::function<.+>$"), stl_summary_flags, true);

  stl_summary_flags.SetDontShowChildren(false);
  stl_summary_flags.SetSkipPointers(false);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::bitset summary provider",
                ConstString("^std::__(ndk)?1::bitset<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::vector summary provider",
                ConstString("^std::__(ndk)?1::vector<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::list summary provider",
                ConstString("^std::__(ndk)?1::forward_list<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::list summary provider",
                ConstString("^std::__(ndk)?1::list<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::map summary provider",
                ConstString("^std::__(ndk)?1::map<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::deque summary provider",
                ConstString("^std::__(ndk)?1::deque<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::queue summary provider",
                ConstString("^std::__(ndk)?1::queue<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::set summary provider",
                ConstString("^std::__(ndk)?1::set<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::multiset summary provider",
                ConstString("^std::__(ndk)?1::multiset<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::multimap summary provider",
                ConstString("^std::__(ndk)?1::multimap<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::LibcxxContainerSummaryProvider,
      "libc++ std::unordered containers summary provider",
      ConstString("^(std::__(ndk)?1::)unordered_(multi)?(map|set)<.+> >$"),
      stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp, LibcxxContainerSummaryProvider,
                "libc++ std::tuple summary provider",
                ConstString("^std::__(ndk)?1::tuple<.*>(( )?&)?$"), stl_summary_flags,
                true);
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::LibCxxAtomicSummaryProvider,
      "libc++ std::atomic summary provider",
      ConstString("^std::__(ndk)?1::atomic<.+>$"), stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxOptionalSummaryProvider,
                "libc++ std::optional summary provider",
                ConstString("^std::__(ndk)?1::optional<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxVariantSummaryProvider,
                "libc++ std::variant summary provider",
                ConstString("^std::__(ndk)?1::variant<.+>(( )?&)?$"),
                stl_summary_flags, true);

  stl_summary_flags.SetSkipPointers(true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxSmartPointerSummaryProvider,
                "libc++ std::shared_ptr summary provider",
                ConstString("^std::__(ndk)?1::shared_ptr<.+>(( )?&)?$"),
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxSmartPointerSummaryProvider,
                "libc++ std::weak_ptr summary provider",
                ConstString("^std::__(ndk)?1::weak_ptr<.+>(( )?&)?$"),
                stl_summary_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibCxxVectorIteratorSyntheticFrontEndCreator,
      "std::vector iterator synthetic children",
      ConstString("^std::__(ndk)?1::__wrap_iter<.+>$"), stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEndCreator,
      "std::map iterator synthetic children",
      ConstString("^std::__(ndk)?1::__map_iterator<.+>$"), stl_synth_flags,
      true);
#endif
}

static void LoadLibStdcppFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags stl_summary_flags;
  stl_summary_flags.SetCascades(true)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  lldb::TypeSummaryImplSP std_string_summary_sp(
      new StringSummaryFormat(stl_summary_flags, "${var._M_dataplus._M_p}"));

  lldb::TypeSummaryImplSP cxx11_string_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags, LibStdcppStringSummaryProvider,
      "libstdc++ c++11 std::string summary provider"));
  lldb::TypeSummaryImplSP cxx11_wstring_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags, LibStdcppWStringSummaryProvider,
      "libstdc++ c++11 std::wstring summary provider"));

  cpp_category_sp->GetTypeSummariesContainer()->Add(ConstString("std::string"),
                                                    std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<char>"), std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<char,std::char_traits<char>,std::"
                  "allocator<char> >"),
      std_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<char, std::char_traits<char>, "
                  "std::allocator<char> >"),
      std_string_summary_sp);

  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__cxx11::string"), cxx11_string_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__cxx11::basic_string<char, std::char_traits<char>, "
                  "std::allocator<char> >"),
      cxx11_string_summary_sp);

  // making sure we force-pick the summary for printing wstring (_M_p is a
  // wchar_t*)
  lldb::TypeSummaryImplSP std_wstring_summary_sp(
      new StringSummaryFormat(stl_summary_flags, "${var._M_dataplus._M_p%S}"));

  cpp_category_sp->GetTypeSummariesContainer()->Add(ConstString("std::wstring"),
                                                    std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<wchar_t>"), std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<wchar_t,std::char_traits<wchar_t>,std::"
                  "allocator<wchar_t> >"),
      std_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::basic_string<wchar_t, std::char_traits<wchar_t>, "
                  "std::allocator<wchar_t> >"),
      std_wstring_summary_sp);

  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__cxx11::wstring"), cxx11_wstring_summary_sp);
  cpp_category_sp->GetTypeSummariesContainer()->Add(
      ConstString("std::__cxx11::basic_string<wchar_t, "
                  "std::char_traits<wchar_t>, std::allocator<wchar_t> >"),
      cxx11_wstring_summary_sp);

#ifndef LLDB_DISABLE_PYTHON

  SyntheticChildren::Flags stl_synth_flags;
  stl_synth_flags.SetCascades(true).SetSkipPointers(false).SetSkipReferences(
      false);

  cpp_category_sp->GetRegexTypeSyntheticsContainer()->Add(
      RegularExpressionSP(
          new RegularExpression(llvm::StringRef("^std::vector<.+>(( )?&)?$"))),
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdVectorSynthProvider")));
  cpp_category_sp->GetRegexTypeSyntheticsContainer()->Add(
      RegularExpressionSP(
          new RegularExpression(llvm::StringRef("^std::map<.+> >(( )?&)?$"))),
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdMapSynthProvider")));
  cpp_category_sp->GetRegexTypeSyntheticsContainer()->Add(
      RegularExpressionSP(new RegularExpression(
          llvm::StringRef("^std::(__cxx11::)?list<.+>(( )?&)?$"))),
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdListSynthProvider")));
  stl_summary_flags.SetDontShowChildren(false);
  stl_summary_flags.SetSkipPointers(true);
  cpp_category_sp->GetRegexTypeSummariesContainer()->Add(
      RegularExpressionSP(
          new RegularExpression(llvm::StringRef("^std::vector<.+>(( )?&)?$"))),
      TypeSummaryImplSP(
          new StringSummaryFormat(stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->GetRegexTypeSummariesContainer()->Add(
      RegularExpressionSP(
          new RegularExpression(llvm::StringRef("^std::map<.+> >(( )?&)?$"))),
      TypeSummaryImplSP(
          new StringSummaryFormat(stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->GetRegexTypeSummariesContainer()->Add(
      RegularExpressionSP(new RegularExpression(
          llvm::StringRef("^std::(__cxx11::)?list<.+>(( )?&)?$"))),
      TypeSummaryImplSP(
          new StringSummaryFormat(stl_summary_flags, "size=${svar%#}")));

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppVectorIteratorSyntheticFrontEndCreator,
      "std::vector iterator synthetic children",
      ConstString("^__gnu_cxx::__normal_iterator<.+>$"), stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibstdcppMapIteratorSyntheticFrontEndCreator,
      "std::map iterator synthetic children",
      ConstString("^std::_Rb_tree_iterator<.+>$"), stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppUniquePtrSyntheticFrontEndCreator,
      "std::unique_ptr synthetic children",
      ConstString("^std::unique_ptr<.+>(( )?&)?$"), stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator,
      "std::shared_ptr synthetic children",
      ConstString("^std::shared_ptr<.+>(( )?&)?$"), stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator,
      "std::weak_ptr synthetic children",
      ConstString("^std::weak_ptr<.+>(( )?&)?$"), stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppTupleSyntheticFrontEndCreator,
      "std::tuple synthetic children", ConstString("^std::tuple<.+>(( )?&)?$"),
      stl_synth_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppUniquePointerSummaryProvider,
                "libstdc++ std::unique_ptr summary provider",
                ConstString("^std::unique_ptr<.+>(( )?&)?$"), stl_summary_flags,
                true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppSmartPointerSummaryProvider,
                "libstdc++ std::shared_ptr summary provider",
                ConstString("^std::shared_ptr<.+>(( )?&)?$"), stl_summary_flags,
                true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppSmartPointerSummaryProvider,
                "libstdc++ std::weak_ptr summary provider",
                ConstString("^std::weak_ptr<.+>(( )?&)?$"), stl_summary_flags,
                true);
#endif
}

static void LoadSystemFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags string_flags;
  string_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(false)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  TypeSummaryImpl::Flags string_array_flags;
  string_array_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

#ifndef LLDB_DISABLE_PYTHON
  // FIXME because of a bug in the FormattersContainer we need to add a summary
  // for both X* and const X* (<rdar://problem/12717717>)
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char16StringSummaryProvider,
      "char16_t * summary provider", ConstString("char16_t *"), string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16StringSummaryProvider,
                "char16_t [] summary provider",
                ConstString("char16_t \\[[0-9]+\\]"), string_array_flags, true);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char32StringSummaryProvider,
      "char32_t * summary provider", ConstString("char32_t *"), string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char32StringSummaryProvider,
                "char32_t [] summary provider",
                ConstString("char32_t \\[[0-9]+\\]"), string_array_flags, true);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::WCharStringSummaryProvider,
      "wchar_t * summary provider", ConstString("wchar_t *"), string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::WCharStringSummaryProvider,
                "wchar_t * summary provider",
                ConstString("wchar_t \\[[0-9]+\\]"), string_array_flags, true);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char16StringSummaryProvider,
      "unichar * summary provider", ConstString("unichar *"), string_flags);

  TypeSummaryImpl::Flags widechar_flags;
  widechar_flags.SetDontShowValue(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetCascades(true)
      .SetDontShowChildren(true)
      .SetHideItemNames(true)
      .SetShowMembersOneLiner(false);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char16SummaryProvider,
      "char16_t summary provider", ConstString("char16_t"), widechar_flags);
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char32SummaryProvider,
      "char32_t summary provider", ConstString("char32_t"), widechar_flags);
  AddCXXSummary(cpp_category_sp, lldb_private::formatters::WCharSummaryProvider,
                "wchar_t summary provider", ConstString("wchar_t"),
                widechar_flags);

  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::Char16SummaryProvider,
      "unichar summary provider", ConstString("unichar"), widechar_flags);
#endif
}

std::unique_ptr<Language::TypeScavenger> CPlusPlusLanguage::GetTypeScavenger() {
  class CPlusPlusTypeScavenger : public Language::ImageListTypeScavenger {
  public:
    virtual CompilerType AdjustForInclusion(CompilerType &candidate) override {
      LanguageType lang_type(candidate.GetMinimumLanguage());
      if (!Language::LanguageIsC(lang_type) &&
          !Language::LanguageIsCPlusPlus(lang_type))
        return CompilerType();
      if (candidate.IsTypedefType())
        return candidate.GetTypedefedType();
      return candidate;
    }
  };

  return std::unique_ptr<TypeScavenger>(new CPlusPlusTypeScavenger());
}

lldb::TypeCategoryImplSP CPlusPlusLanguage::GetFormatters() {
  static llvm::once_flag g_initialize;
  static TypeCategoryImplSP g_category;

  llvm::call_once(g_initialize, [this]() -> void {
    DataVisualization::Categories::GetCategory(GetPluginName(), g_category);
    if (g_category) {
      LoadLibCxxFormatters(g_category);
      LoadLibStdcppFormatters(g_category);
      LoadSystemFormatters(g_category);
    }
  });
  return g_category;
}

HardcodedFormatters::HardcodedSummaryFinder
CPlusPlusLanguage::GetHardcodedSummaries() {
  static llvm::once_flag g_initialize;
  static ConstString g_vectortypes("VectorTypes");
  static HardcodedFormatters::HardcodedSummaryFinder g_formatters;

  llvm::call_once(g_initialize, []() -> void {
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags(),
                  lldb_private::formatters::CXXFunctionPointerSummaryProvider,
                  "Function pointer summary provider"));
          if (valobj.GetCompilerType().IsFunctionPointerType()) {
            return formatter_sp;
          }
          return nullptr;
        });
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &fmt_mgr) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags()
                      .SetCascades(true)
                      .SetDontShowChildren(true)
                      .SetHideItemNames(true)
                      .SetShowMembersOneLiner(true)
                      .SetSkipPointers(true)
                      .SetSkipReferences(false),
                  lldb_private::formatters::VectorTypeSummaryProvider,
                  "vector_type pointer summary provider"));
          if (valobj.GetCompilerType().IsVectorType(nullptr, nullptr)) {
            if (fmt_mgr.GetCategory(g_vectortypes)->IsEnabled())
              return formatter_sp;
          }
          return nullptr;
        });
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &fmt_mgr) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags()
                      .SetCascades(true)
                      .SetDontShowChildren(true)
                      .SetHideItemNames(true)
                      .SetShowMembersOneLiner(true)
                      .SetSkipPointers(true)
                      .SetSkipReferences(false),
                  lldb_private::formatters::BlockPointerSummaryProvider,
                  "block pointer summary provider"));
          if (valobj.GetCompilerType().IsBlockPointerType(nullptr)) {
            return formatter_sp;
          }
          return nullptr;
        });
  });

  return g_formatters;
}

HardcodedFormatters::HardcodedSyntheticFinder
CPlusPlusLanguage::GetHardcodedSynthetics() {
  static llvm::once_flag g_initialize;
  static ConstString g_vectortypes("VectorTypes");
  static HardcodedFormatters::HardcodedSyntheticFinder g_formatters;

  llvm::call_once(g_initialize, []() -> void {
    g_formatters.push_back([](lldb_private::ValueObject &valobj,
                              lldb::DynamicValueType,
                              FormatManager &
                                  fmt_mgr) -> SyntheticChildren::SharedPointer {
      static CXXSyntheticChildren::SharedPointer formatter_sp(
          new CXXSyntheticChildren(
              SyntheticChildren::Flags()
                  .SetCascades(true)
                  .SetSkipPointers(true)
                  .SetSkipReferences(true)
                  .SetNonCacheable(true),
              "vector_type synthetic children",
              lldb_private::formatters::VectorTypeSyntheticFrontEndCreator));
      if (valobj.GetCompilerType().IsVectorType(nullptr, nullptr)) {
        if (fmt_mgr.GetCategory(g_vectortypes)->IsEnabled())
          return formatter_sp;
      }
      return nullptr;
    });
    g_formatters.push_back([](lldb_private::ValueObject &valobj,
                              lldb::DynamicValueType,
                              FormatManager &
                                  fmt_mgr) -> SyntheticChildren::SharedPointer {
      static CXXSyntheticChildren::SharedPointer formatter_sp(
          new CXXSyntheticChildren(
              SyntheticChildren::Flags()
                  .SetCascades(true)
                  .SetSkipPointers(true)
                  .SetSkipReferences(true)
                  .SetNonCacheable(true),
              "block pointer synthetic children",
              lldb_private::formatters::BlockPointerSyntheticFrontEndCreator));
      if (valobj.GetCompilerType().IsBlockPointerType(nullptr)) {
        return formatter_sp;
      }
      return nullptr;
    });

  });

  return g_formatters;
}

bool CPlusPlusLanguage::IsSourceFile(llvm::StringRef file_path) const {
  const auto suffixes = {".cpp", ".cxx", ".c++", ".cc",  ".c",
                         ".h",   ".hh",  ".hpp", ".hxx", ".h++"};
  for (auto suffix : suffixes) {
    if (file_path.endswith_lower(suffix))
      return true;
  }

  // Check if we're in a STL path (where the files usually have no extension
  // that we could check for.
  return file_path.contains("/usr/include/c++/");
}
