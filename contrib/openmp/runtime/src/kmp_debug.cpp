/*
 * kmp_debug.cpp -- debug utilities for the Guide library
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_debug.h" /* really necessary? */
#include "kmp_i18n.h"
#include "kmp_io.h"

#ifdef KMP_DEBUG
void __kmp_debug_printf_stdout(char const *format, ...) {
  va_list ap;
  va_start(ap, format);

  __kmp_vprintf(kmp_out, format, ap);

  va_end(ap);
}
#endif

void __kmp_debug_printf(char const *format, ...) {
  va_list ap;
  va_start(ap, format);

  __kmp_vprintf(kmp_err, format, ap);

  va_end(ap);
}

#ifdef KMP_USE_ASSERT
int __kmp_debug_assert(char const *msg, char const *file, int line) {

  if (file == NULL) {
    file = KMP_I18N_STR(UnknownFile);
  } else {
    // Remove directories from path, leave only file name. File name is enough,
    // there is no need in bothering developers and customers with full paths.
    char const *slash = strrchr(file, '/');
    if (slash != NULL) {
      file = slash + 1;
    }
  }

#ifdef KMP_DEBUG
  __kmp_acquire_bootstrap_lock(&__kmp_stdio_lock);
  __kmp_debug_printf("Assertion failure at %s(%d): %s.\n", file, line, msg);
  __kmp_release_bootstrap_lock(&__kmp_stdio_lock);
#ifdef USE_ASSERT_BREAK
#if KMP_OS_WINDOWS
  DebugBreak();
#endif
#endif // USE_ASSERT_BREAK
#ifdef USE_ASSERT_STALL
  /*    __kmp_infinite_loop(); */
  for (;;)
    ;
#endif // USE_ASSERT_STALL
#ifdef USE_ASSERT_SEG
  {
    int volatile *ZERO = (int *)0;
    ++(*ZERO);
  }
#endif // USE_ASSERT_SEG
#endif

  __kmp_fatal(KMP_MSG(AssertionFailure, file, line), KMP_HNT(SubmitBugReport),
              __kmp_msg_null);

  return 0;

} // __kmp_debug_assert

#endif // KMP_USE_ASSERT

/* Dump debugging buffer to stderr */
void __kmp_dump_debug_buffer(void) {
  if (__kmp_debug_buffer != NULL) {
    int i;
    int dc = __kmp_debug_count;
    char *db = &__kmp_debug_buffer[(dc % __kmp_debug_buf_lines) *
                                   __kmp_debug_buf_chars];
    char *db_end =
        &__kmp_debug_buffer[__kmp_debug_buf_lines * __kmp_debug_buf_chars];
    char *db2;

    __kmp_acquire_bootstrap_lock(&__kmp_stdio_lock);
    __kmp_printf_no_lock("\nStart dump of debugging buffer (entry=%d):\n",
                         dc % __kmp_debug_buf_lines);

    for (i = 0; i < __kmp_debug_buf_lines; i++) {

      if (*db != '\0') {
        /* Fix up where no carriage return before string termination char */
        for (db2 = db + 1; db2 < db + __kmp_debug_buf_chars - 1; db2++) {
          if (*db2 == '\0') {
            if (*(db2 - 1) != '\n') {
              *db2 = '\n';
              *(db2 + 1) = '\0';
            }
            break;
          }
        }
        /* Handle case at end by shortening the printed message by one char if
         * necessary */
        if (db2 == db + __kmp_debug_buf_chars - 1 && *db2 == '\0' &&
            *(db2 - 1) != '\n') {
          *(db2 - 1) = '\n';
        }

        __kmp_printf_no_lock("%4d: %.*s", i, __kmp_debug_buf_chars, db);
        *db = '\0'; /* only let it print once! */
      }

      db += __kmp_debug_buf_chars;
      if (db >= db_end)
        db = __kmp_debug_buffer;
    }

    __kmp_printf_no_lock("End dump of debugging buffer (entry=%d).\n\n",
                         (dc + i - 1) % __kmp_debug_buf_lines);
    __kmp_release_bootstrap_lock(&__kmp_stdio_lock);
  }
}
