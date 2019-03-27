//===-- sanitizer_flag_parser.cc ------------------------------------------===//
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

#include "sanitizer_flag_parser.h"

#include "sanitizer_common.h"
#include "sanitizer_libc.h"
#include "sanitizer_flags.h"
#include "sanitizer_flag_parser.h"

namespace __sanitizer {

LowLevelAllocator FlagParser::Alloc;

class UnknownFlags {
  static const int kMaxUnknownFlags = 20;
  const char *unknown_flags_[kMaxUnknownFlags];
  int n_unknown_flags_;

 public:
  void Add(const char *name) {
    CHECK_LT(n_unknown_flags_, kMaxUnknownFlags);
    unknown_flags_[n_unknown_flags_++] = name;
  }

  void Report() {
    if (!n_unknown_flags_) return;
    Printf("WARNING: found %d unrecognized flag(s):\n", n_unknown_flags_);
    for (int i = 0; i < n_unknown_flags_; ++i)
      Printf("    %s\n", unknown_flags_[i]);
    n_unknown_flags_ = 0;
  }
};

UnknownFlags unknown_flags;

void ReportUnrecognizedFlags() {
  unknown_flags.Report();
}

char *FlagParser::ll_strndup(const char *s, uptr n) {
  uptr len = internal_strnlen(s, n);
  char *s2 = (char*)Alloc.Allocate(len + 1);
  internal_memcpy(s2, s, len);
  s2[len] = 0;
  return s2;
}

void FlagParser::PrintFlagDescriptions() {
  Printf("Available flags for %s:\n", SanitizerToolName);
  for (int i = 0; i < n_flags_; ++i)
    Printf("\t%s\n\t\t- %s\n", flags_[i].name, flags_[i].desc);
}

void FlagParser::fatal_error(const char *err) {
  Printf("ERROR: %s\n", err);
  Die();
}

bool FlagParser::is_space(char c) {
  return c == ' ' || c == ',' || c == ':' || c == '\n' || c == '\t' ||
         c == '\r';
}

void FlagParser::skip_whitespace() {
  while (is_space(buf_[pos_])) ++pos_;
}

void FlagParser::parse_flag() {
  uptr name_start = pos_;
  while (buf_[pos_] != 0 && buf_[pos_] != '=' && !is_space(buf_[pos_])) ++pos_;
  if (buf_[pos_] != '=') fatal_error("expected '='");
  char *name = ll_strndup(buf_ + name_start, pos_ - name_start);

  uptr value_start = ++pos_;
  char *value;
  if (buf_[pos_] == '\'' || buf_[pos_] == '"') {
    char quote = buf_[pos_++];
    while (buf_[pos_] != 0 && buf_[pos_] != quote) ++pos_;
    if (buf_[pos_] == 0) fatal_error("unterminated string");
    value = ll_strndup(buf_ + value_start + 1, pos_ - value_start - 1);
    ++pos_; // consume the closing quote
  } else {
    while (buf_[pos_] != 0 && !is_space(buf_[pos_])) ++pos_;
    if (buf_[pos_] != 0 && !is_space(buf_[pos_]))
      fatal_error("expected separator or eol");
    value = ll_strndup(buf_ + value_start, pos_ - value_start);
  }

  bool res = run_handler(name, value);
  if (!res) fatal_error("Flag parsing failed.");
}

void FlagParser::parse_flags() {
  while (true) {
    skip_whitespace();
    if (buf_[pos_] == 0) break;
    parse_flag();
  }

  // Do a sanity check for certain flags.
  if (common_flags_dont_use.malloc_context_size < 1)
    common_flags_dont_use.malloc_context_size = 1;
}

void FlagParser::ParseString(const char *s) {
  if (!s) return;
  // Backup current parser state to allow nested ParseString() calls.
  const char *old_buf_ = buf_;
  uptr old_pos_ = pos_;
  buf_ = s;
  pos_ = 0;

  parse_flags();

  buf_ = old_buf_;
  pos_ = old_pos_;
}

bool FlagParser::ParseFile(const char *path, bool ignore_missing) {
  static const uptr kMaxIncludeSize = 1 << 15;
  char *data;
  uptr data_mapped_size;
  error_t err;
  uptr len;
  if (!ReadFileToBuffer(path, &data, &data_mapped_size, &len,
                        Max(kMaxIncludeSize, GetPageSizeCached()), &err)) {
    if (ignore_missing)
      return true;
    Printf("Failed to read options from '%s': error %d\n", path, err);
    return false;
  }
  ParseString(data);
  UnmapOrDie(data, data_mapped_size);
  return true;
}

bool FlagParser::run_handler(const char *name, const char *value) {
  for (int i = 0; i < n_flags_; ++i) {
    if (internal_strcmp(name, flags_[i].name) == 0)
      return flags_[i].handler->Parse(value);
  }
  // Unrecognized flag. This is not a fatal error, we may print a warning later.
  unknown_flags.Add(name);
  return true;
}

void FlagParser::RegisterHandler(const char *name, FlagHandlerBase *handler,
                                 const char *desc) {
  CHECK_LT(n_flags_, kMaxFlags);
  flags_[n_flags_].name = name;
  flags_[n_flags_].desc = desc;
  flags_[n_flags_].handler = handler;
  ++n_flags_;
}

FlagParser::FlagParser() : n_flags_(0), buf_(nullptr), pos_(0) {
  flags_ = (Flag *)Alloc.Allocate(sizeof(Flag) * kMaxFlags);
}

}  // namespace __sanitizer
