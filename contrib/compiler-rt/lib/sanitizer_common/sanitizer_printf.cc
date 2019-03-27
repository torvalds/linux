//===-- sanitizer_printf.cc -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer.
//
// Internal printf function, used inside run-time libraries.
// We can't use libc printf because we intercept some of the functions used
// inside it.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"

#include <stdio.h>
#include <stdarg.h>

#if SANITIZER_WINDOWS && defined(_MSC_VER) && _MSC_VER < 1800 &&               \
      !defined(va_copy)
# define va_copy(dst, src) ((dst) = (src))
#endif

namespace __sanitizer {

static int AppendChar(char **buff, const char *buff_end, char c) {
  if (*buff < buff_end) {
    **buff = c;
    (*buff)++;
  }
  return 1;
}

// Appends number in a given base to buffer. If its length is less than
// |minimal_num_length|, it is padded with leading zeroes or spaces, depending
// on the value of |pad_with_zero|.
static int AppendNumber(char **buff, const char *buff_end, u64 absolute_value,
                        u8 base, u8 minimal_num_length, bool pad_with_zero,
                        bool negative, bool uppercase) {
  uptr const kMaxLen = 30;
  RAW_CHECK(base == 10 || base == 16);
  RAW_CHECK(base == 10 || !negative);
  RAW_CHECK(absolute_value || !negative);
  RAW_CHECK(minimal_num_length < kMaxLen);
  int result = 0;
  if (negative && minimal_num_length)
    --minimal_num_length;
  if (negative && pad_with_zero)
    result += AppendChar(buff, buff_end, '-');
  uptr num_buffer[kMaxLen];
  int pos = 0;
  do {
    RAW_CHECK_MSG((uptr)pos < kMaxLen, "AppendNumber buffer overflow");
    num_buffer[pos++] = absolute_value % base;
    absolute_value /= base;
  } while (absolute_value > 0);
  if (pos < minimal_num_length) {
    // Make sure compiler doesn't insert call to memset here.
    internal_memset(&num_buffer[pos], 0,
                    sizeof(num_buffer[0]) * (minimal_num_length - pos));
    pos = minimal_num_length;
  }
  RAW_CHECK(pos > 0);
  pos--;
  for (; pos >= 0 && num_buffer[pos] == 0; pos--) {
    char c = (pad_with_zero || pos == 0) ? '0' : ' ';
    result += AppendChar(buff, buff_end, c);
  }
  if (negative && !pad_with_zero) result += AppendChar(buff, buff_end, '-');
  for (; pos >= 0; pos--) {
    char digit = static_cast<char>(num_buffer[pos]);
    digit = (digit < 10) ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
    result += AppendChar(buff, buff_end, digit);
  }
  return result;
}

static int AppendUnsigned(char **buff, const char *buff_end, u64 num, u8 base,
                          u8 minimal_num_length, bool pad_with_zero,
                          bool uppercase) {
  return AppendNumber(buff, buff_end, num, base, minimal_num_length,
                      pad_with_zero, false /* negative */, uppercase);
}

static int AppendSignedDecimal(char **buff, const char *buff_end, s64 num,
                               u8 minimal_num_length, bool pad_with_zero) {
  bool negative = (num < 0);
  return AppendNumber(buff, buff_end, (u64)(negative ? -num : num), 10,
                      minimal_num_length, pad_with_zero, negative,
                      false /* uppercase */);
}


// Use the fact that explicitly requesting 0 width (%0s) results in UB and
// interpret width == 0 as "no width requested":
// width == 0 - no width requested
// width  < 0 - left-justify s within and pad it to -width chars, if necessary
// width  > 0 - right-justify s, not implemented yet
static int AppendString(char **buff, const char *buff_end, int width,
                        int max_chars, const char *s) {
  if (!s)
    s = "<null>";
  int result = 0;
  for (; *s; s++) {
    if (max_chars >= 0 && result >= max_chars)
      break;
    result += AppendChar(buff, buff_end, *s);
  }
  // Only the left justified strings are supported.
  while (width < -result)
    result += AppendChar(buff, buff_end, ' ');
  return result;
}

static int AppendPointer(char **buff, const char *buff_end, u64 ptr_value) {
  int result = 0;
  result += AppendString(buff, buff_end, 0, -1, "0x");
  result += AppendUnsigned(buff, buff_end, ptr_value, 16,
                           SANITIZER_POINTER_FORMAT_LENGTH,
                           true /* pad_with_zero */, false /* uppercase */);
  return result;
}

int VSNPrintf(char *buff, int buff_length,
              const char *format, va_list args) {
  static const char *kPrintfFormatsHelp =
      "Supported Printf formats: %([0-9]*)?(z|ll)?{d,u,x,X}; %p; "
      "%[-]([0-9]*)?(\\.\\*)?s; %c\n";
  RAW_CHECK(format);
  RAW_CHECK(buff_length > 0);
  const char *buff_end = &buff[buff_length - 1];
  const char *cur = format;
  int result = 0;
  for (; *cur; cur++) {
    if (*cur != '%') {
      result += AppendChar(&buff, buff_end, *cur);
      continue;
    }
    cur++;
    bool left_justified = *cur == '-';
    if (left_justified)
      cur++;
    bool have_width = (*cur >= '0' && *cur <= '9');
    bool pad_with_zero = (*cur == '0');
    int width = 0;
    if (have_width) {
      while (*cur >= '0' && *cur <= '9') {
        width = width * 10 + *cur++ - '0';
      }
    }
    bool have_precision = (cur[0] == '.' && cur[1] == '*');
    int precision = -1;
    if (have_precision) {
      cur += 2;
      precision = va_arg(args, int);
    }
    bool have_z = (*cur == 'z');
    cur += have_z;
    bool have_ll = !have_z && (cur[0] == 'l' && cur[1] == 'l');
    cur += have_ll * 2;
    s64 dval;
    u64 uval;
    const bool have_length = have_z || have_ll;
    const bool have_flags = have_width || have_length;
    // At the moment only %s supports precision and left-justification.
    CHECK(!((precision >= 0 || left_justified) && *cur != 's'));
    switch (*cur) {
      case 'd': {
        dval = have_ll ? va_arg(args, s64)
             : have_z ? va_arg(args, sptr)
             : va_arg(args, int);
        result += AppendSignedDecimal(&buff, buff_end, dval, width,
                                      pad_with_zero);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        uval = have_ll ? va_arg(args, u64)
             : have_z ? va_arg(args, uptr)
             : va_arg(args, unsigned);
        bool uppercase = (*cur == 'X');
        result += AppendUnsigned(&buff, buff_end, uval, (*cur == 'u') ? 10 : 16,
                                 width, pad_with_zero, uppercase);
        break;
      }
      case 'p': {
        RAW_CHECK_MSG(!have_flags, kPrintfFormatsHelp);
        result += AppendPointer(&buff, buff_end, va_arg(args, uptr));
        break;
      }
      case 's': {
        RAW_CHECK_MSG(!have_length, kPrintfFormatsHelp);
        // Only left-justified width is supported.
        CHECK(!have_width || left_justified);
        result += AppendString(&buff, buff_end, left_justified ? -width : width,
                               precision, va_arg(args, char*));
        break;
      }
      case 'c': {
        RAW_CHECK_MSG(!have_flags, kPrintfFormatsHelp);
        result += AppendChar(&buff, buff_end, va_arg(args, int));
        break;
      }
      case '%' : {
        RAW_CHECK_MSG(!have_flags, kPrintfFormatsHelp);
        result += AppendChar(&buff, buff_end, '%');
        break;
      }
      default: {
        RAW_CHECK_MSG(false, kPrintfFormatsHelp);
      }
    }
  }
  RAW_CHECK(buff <= buff_end);
  AppendChar(&buff, buff_end + 1, '\0');
  return result;
}

static void (*PrintfAndReportCallback)(const char *);
void SetPrintfAndReportCallback(void (*callback)(const char *)) {
  PrintfAndReportCallback = callback;
}

// Can be overriden in frontend.
#if SANITIZER_GO && defined(TSAN_EXTERNAL_HOOKS)
// Implementation must be defined in frontend.
extern "C" void OnPrint(const char *str);
#else
SANITIZER_INTERFACE_WEAK_DEF(void, OnPrint, const char *str) {
  (void)str;
}
#endif

static void CallPrintfAndReportCallback(const char *str) {
  OnPrint(str);
  if (PrintfAndReportCallback)
    PrintfAndReportCallback(str);
}

static void NOINLINE SharedPrintfCodeNoBuffer(bool append_pid,
                                              char *local_buffer,
                                              int buffer_size,
                                              const char *format,
                                              va_list args) {
  va_list args2;
  va_copy(args2, args);
  const int kLen = 16 * 1024;
  int needed_length;
  char *buffer = local_buffer;
  // First try to print a message using a local buffer, and then fall back to
  // mmaped buffer.
  for (int use_mmap = 0; use_mmap < 2; use_mmap++) {
    if (use_mmap) {
      va_end(args);
      va_copy(args, args2);
      buffer = (char*)MmapOrDie(kLen, "Report");
      buffer_size = kLen;
    }
    needed_length = 0;
    // Check that data fits into the current buffer.
#   define CHECK_NEEDED_LENGTH \
      if (needed_length >= buffer_size) { \
        if (!use_mmap) continue; \
        RAW_CHECK_MSG(needed_length < kLen, \
                      "Buffer in Report is too short!\n"); \
      }
    // Fuchsia's logging infrastructure always keeps track of the logging
    // process, thread, and timestamp, so never prepend such information.
    if (!SANITIZER_FUCHSIA && append_pid) {
      int pid = internal_getpid();
      const char *exe_name = GetProcessName();
      if (common_flags()->log_exe_name && exe_name) {
        needed_length += internal_snprintf(buffer, buffer_size,
                                           "==%s", exe_name);
        CHECK_NEEDED_LENGTH
      }
      needed_length += internal_snprintf(
          buffer + needed_length, buffer_size - needed_length, "==%d==", pid);
      CHECK_NEEDED_LENGTH
    }
    needed_length += VSNPrintf(buffer + needed_length,
                               buffer_size - needed_length, format, args);
    CHECK_NEEDED_LENGTH
    // If the message fit into the buffer, print it and exit.
    break;
#   undef CHECK_NEEDED_LENGTH
  }
  RawWrite(buffer);

  // Remove color sequences from the message.
  RemoveANSIEscapeSequencesFromString(buffer);
  CallPrintfAndReportCallback(buffer);
  LogMessageOnPrintf(buffer);

  // If we had mapped any memory, clean up.
  if (buffer != local_buffer)
    UnmapOrDie((void *)buffer, buffer_size);
  va_end(args2);
}

static void NOINLINE SharedPrintfCode(bool append_pid, const char *format,
                                      va_list args) {
  // |local_buffer| is small enough not to overflow the stack and/or violate
  // the stack limit enforced by TSan (-Wframe-larger-than=512). On the other
  // hand, the bigger the buffer is, the more the chance the error report will
  // fit into it.
  char local_buffer[400];
  SharedPrintfCodeNoBuffer(append_pid, local_buffer, ARRAY_SIZE(local_buffer),
                           format, args);
}

FORMAT(1, 2)
void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  SharedPrintfCode(false, format, args);
  va_end(args);
}

// Like Printf, but prints the current PID before the output string.
FORMAT(1, 2)
void Report(const char *format, ...) {
  va_list args;
  va_start(args, format);
  SharedPrintfCode(true, format, args);
  va_end(args);
}

// Writes at most "length" symbols to "buffer" (including trailing '\0').
// Returns the number of symbols that should have been written to buffer
// (not including trailing '\0'). Thus, the string is truncated
// iff return value is not less than "length".
FORMAT(3, 4)
int internal_snprintf(char *buffer, uptr length, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int needed_length = VSNPrintf(buffer, length, format, args);
  va_end(args);
  return needed_length;
}

FORMAT(2, 3)
void InternalScopedString::append(const char *format, ...) {
  CHECK_LT(length_, size());
  va_list args;
  va_start(args, format);
  VSNPrintf(data() + length_, size() - length_, format, args);
  va_end(args);
  length_ += internal_strlen(data() + length_);
  CHECK_LT(length_, size());
}

} // namespace __sanitizer
