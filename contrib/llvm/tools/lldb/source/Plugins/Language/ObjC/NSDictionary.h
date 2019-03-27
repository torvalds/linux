//===-- NSDictionary.h ---------------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NSDictionary_h_
#define liblldb_NSDictionary_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include <map>
#include <memory>

namespace lldb_private {
namespace formatters {
template <bool name_entries>
bool NSDictionarySummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &options);

extern template bool
NSDictionarySummaryProvider<true>(ValueObject &, Stream &,
                                  const TypeSummaryOptions &);

extern template bool
NSDictionarySummaryProvider<false>(ValueObject &, Stream &,
                                   const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
NSDictionarySyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);

class NSDictionary_Additionals {
public:
  class AdditionalFormatterMatching {
  public:
    class Matcher {
    public:
      virtual ~Matcher() = default;
      virtual bool Match(ConstString class_name) = 0;

      typedef std::unique_ptr<Matcher> UP;
    };
    class Prefix : public Matcher {
    public:
      Prefix(ConstString p);
      virtual ~Prefix() = default;
      virtual bool Match(ConstString class_name) override;

    private:
      ConstString m_prefix;
    };
    class Full : public Matcher {
    public:
      Full(ConstString n);
      virtual ~Full() = default;
      virtual bool Match(ConstString class_name) override;

    private:
      ConstString m_name;
    };
    typedef Matcher::UP MatcherUP;

    MatcherUP GetFullMatch(ConstString n) { return llvm::make_unique<Full>(n); }

    MatcherUP GetPrefixMatch(ConstString p) {
      return llvm::make_unique<Prefix>(p);
    }
  };

  template <typename FormatterType>
  using AdditionalFormatter =
      std::pair<AdditionalFormatterMatching::MatcherUP, FormatterType>;

  template <typename FormatterType>
  using AdditionalFormatters = std::vector<AdditionalFormatter<FormatterType>>;

  static AdditionalFormatters<CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();

  static AdditionalFormatters<CXXSyntheticChildren::CreateFrontEndCallback> &
  GetAdditionalSynthetics();
};
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_NSDictionary_h_
