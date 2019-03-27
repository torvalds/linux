//===-- sanitizer_flags.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_flags.h"

#include "sanitizer_common.h"
#include "sanitizer_libc.h"
#include "sanitizer_list.h"
#include "sanitizer_flag_parser.h"

namespace __sanitizer {

CommonFlags common_flags_dont_use;

void CommonFlags::SetDefaults() {
#define COMMON_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "sanitizer_flags.inc"
#undef COMMON_FLAG
}

void CommonFlags::CopyFrom(const CommonFlags &other) {
  internal_memcpy(this, &other, sizeof(*this));
}

// Copy the string from "s" to "out", making the following substitutions:
// %b = binary basename
// %p = pid
void SubstituteForFlagValue(const char *s, char *out, uptr out_size) {
  char *out_end = out + out_size;
  while (*s && out < out_end - 1) {
    if (s[0] != '%') {
      *out++ = *s++;
      continue;
    }
    switch (s[1]) {
      case 'b': {
        const char *base = GetProcessName();
        CHECK(base);
        while (*base && out < out_end - 1)
          *out++ = *base++;
        s += 2; // skip "%b"
        break;
      }
      case 'p': {
        int pid = internal_getpid();
        char buf[32];
        char *buf_pos = buf + 32;
        do {
          *--buf_pos = (pid % 10) + '0';
          pid /= 10;
        } while (pid);
        while (buf_pos < buf + 32 && out < out_end - 1)
          *out++ = *buf_pos++;
        s += 2; // skip "%p"
        break;
      }
      default:
        *out++ = *s++;
        break;
    }
  }
  CHECK(out < out_end - 1);
  *out = '\0';
}

class FlagHandlerInclude : public FlagHandlerBase {
  FlagParser *parser_;
  bool ignore_missing_;

 public:
  explicit FlagHandlerInclude(FlagParser *parser, bool ignore_missing)
      : parser_(parser), ignore_missing_(ignore_missing) {}
  bool Parse(const char *value) final {
    if (internal_strchr(value, '%')) {
      char *buf = (char *)MmapOrDie(kMaxPathLength, "FlagHandlerInclude");
      SubstituteForFlagValue(value, buf, kMaxPathLength);
      bool res = parser_->ParseFile(buf, ignore_missing_);
      UnmapOrDie(buf, kMaxPathLength);
      return res;
    }
    return parser_->ParseFile(value, ignore_missing_);
  }
};

void RegisterIncludeFlags(FlagParser *parser, CommonFlags *cf) {
  FlagHandlerInclude *fh_include = new (FlagParser::Alloc) // NOLINT
      FlagHandlerInclude(parser, /*ignore_missing*/ false);
  parser->RegisterHandler("include", fh_include,
                          "read more options from the given file");
  FlagHandlerInclude *fh_include_if_exists = new (FlagParser::Alloc) // NOLINT
      FlagHandlerInclude(parser, /*ignore_missing*/ true);
  parser->RegisterHandler(
      "include_if_exists", fh_include_if_exists,
      "read more options from the given file (if it exists)");
}

void RegisterCommonFlags(FlagParser *parser, CommonFlags *cf) {
#define COMMON_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &cf->Name);
#include "sanitizer_flags.inc"
#undef COMMON_FLAG

  RegisterIncludeFlags(parser, cf);
}

void InitializeCommonFlags(CommonFlags *cf) {
  // need to record coverage to generate coverage report.
  cf->coverage |= cf->html_cov_report;
  SetVerbosity(cf->verbosity);
}

}  // namespace __sanitizer
