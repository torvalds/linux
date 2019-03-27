//===-- sanitizer_suppressions.cc -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Suppression parsing/matching code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_suppressions.h"

#include "sanitizer_allocator_internal.h"
#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_file.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"

namespace __sanitizer {

SuppressionContext::SuppressionContext(const char *suppression_types[],
                                       int suppression_types_num)
    : suppression_types_(suppression_types),
      suppression_types_num_(suppression_types_num),
      can_parse_(true) {
  CHECK_LE(suppression_types_num_, kMaxSuppressionTypes);
  internal_memset(has_suppression_type_, 0, suppression_types_num_);
}

static bool GetPathAssumingFileIsRelativeToExec(const char *file_path,
                                                /*out*/char *new_file_path,
                                                uptr new_file_path_size) {
  InternalScopedString exec(kMaxPathLength);
  if (ReadBinaryNameCached(exec.data(), exec.size())) {
    const char *file_name_pos = StripModuleName(exec.data());
    uptr path_to_exec_len = file_name_pos - exec.data();
    internal_strncat(new_file_path, exec.data(),
                     Min(path_to_exec_len, new_file_path_size - 1));
    internal_strncat(new_file_path, file_path,
                     new_file_path_size - internal_strlen(new_file_path) - 1);
    return true;
  }
  return false;
}

void SuppressionContext::ParseFromFile(const char *filename) {
  if (filename[0] == '\0')
    return;

#if !SANITIZER_FUCHSIA
  // If we cannot find the file, check if its location is relative to
  // the location of the executable.
  InternalScopedString new_file_path(kMaxPathLength);
  if (!FileExists(filename) && !IsAbsolutePath(filename) &&
      GetPathAssumingFileIsRelativeToExec(filename, new_file_path.data(),
                                          new_file_path.size())) {
    filename = new_file_path.data();
  }
#endif  // !SANITIZER_FUCHSIA

  // Read the file.
  VPrintf(1, "%s: reading suppressions file at %s\n",
          SanitizerToolName, filename);
  char *file_contents;
  uptr buffer_size;
  uptr contents_size;
  if (!ReadFileToBuffer(filename, &file_contents, &buffer_size,
                        &contents_size)) {
    Printf("%s: failed to read suppressions file '%s'\n", SanitizerToolName,
           filename);
    Die();
  }

  Parse(file_contents);
}

bool SuppressionContext::Match(const char *str, const char *type,
                               Suppression **s) {
  can_parse_ = false;
  if (!HasSuppressionType(type))
    return false;
  for (uptr i = 0; i < suppressions_.size(); i++) {
    Suppression &cur = suppressions_[i];
    if (0 == internal_strcmp(cur.type, type) && TemplateMatch(cur.templ, str)) {
      *s = &cur;
      return true;
    }
  }
  return false;
}

static const char *StripPrefix(const char *str, const char *prefix) {
  while (str && *str == *prefix) {
    str++;
    prefix++;
  }
  if (!*prefix)
    return str;
  return 0;
}

void SuppressionContext::Parse(const char *str) {
  // Context must not mutate once Match has been called.
  CHECK(can_parse_);
  const char *line = str;
  while (line) {
    while (line[0] == ' ' || line[0] == '\t')
      line++;
    const char *end = internal_strchr(line, '\n');
    if (end == 0)
      end = line + internal_strlen(line);
    if (line != end && line[0] != '#') {
      const char *end2 = end;
      while (line != end2 &&
             (end2[-1] == ' ' || end2[-1] == '\t' || end2[-1] == '\r'))
        end2--;
      int type;
      for (type = 0; type < suppression_types_num_; type++) {
        const char *next_char = StripPrefix(line, suppression_types_[type]);
        if (next_char && *next_char == ':') {
          line = ++next_char;
          break;
        }
      }
      if (type == suppression_types_num_) {
        Printf("%s: failed to parse suppressions\n", SanitizerToolName);
        Die();
      }
      Suppression s;
      s.type = suppression_types_[type];
      s.templ = (char*)InternalAlloc(end2 - line + 1);
      internal_memcpy(s.templ, line, end2 - line);
      s.templ[end2 - line] = 0;
      suppressions_.push_back(s);
      has_suppression_type_[type] = true;
    }
    if (end[0] == 0)
      break;
    line = end + 1;
  }
}

uptr SuppressionContext::SuppressionCount() const {
  return suppressions_.size();
}

bool SuppressionContext::HasSuppressionType(const char *type) const {
  for (int i = 0; i < suppression_types_num_; i++) {
    if (0 == internal_strcmp(type, suppression_types_[i]))
      return has_suppression_type_[i];
  }
  return false;
}

const Suppression *SuppressionContext::SuppressionAt(uptr i) const {
  CHECK_LT(i, suppressions_.size());
  return &suppressions_[i];
}

void SuppressionContext::GetMatched(
    InternalMmapVector<Suppression *> *matched) {
  for (uptr i = 0; i < suppressions_.size(); i++)
    if (atomic_load_relaxed(&suppressions_[i].hit_count))
      matched->push_back(&suppressions_[i]);
}

}  // namespace __sanitizer
