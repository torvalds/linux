/*
 * kmp_str.cpp -- String manipulation routines.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp_str.h"

#include <stdarg.h> // va_*
#include <stdio.h> // vsnprintf()
#include <stdlib.h> // malloc(), realloc()

#include "kmp.h"
#include "kmp_i18n.h"

/* String buffer.

   Usage:

   // Declare buffer and initialize it.
   kmp_str_buf_t  buffer;
   __kmp_str_buf_init( & buffer );

   // Print to buffer.
   __kmp_str_buf_print(& buffer, "Error in file \"%s\" line %d\n", "foo.c", 12);
   __kmp_str_buf_print(& buffer, "    <%s>\n", line);

   // Use buffer contents. buffer.str is a pointer to data, buffer.used is a
   // number of printed characters (not including terminating zero).
   write( fd, buffer.str, buffer.used );

   // Free buffer.
   __kmp_str_buf_free( & buffer );

   // Alternatively, you can detach allocated memory from buffer:
   __kmp_str_buf_detach( & buffer );
   return buffer.str;    // That memory should be freed eventually.

   Notes:

   * Buffer users may use buffer.str and buffer.used. Users should not change
     any fields of buffer directly.
   * buffer.str is never NULL. If buffer is empty, buffer.str points to empty
     string ("").
   * For performance reasons, buffer uses stack memory (buffer.bulk) first. If
     stack memory is exhausted, buffer allocates memory on heap by malloc(), and
     reallocates it by realloc() as amount of used memory grows.
   * Buffer doubles amount of allocated memory each time it is exhausted.
*/

// TODO: __kmp_str_buf_print() can use thread local memory allocator.

#define KMP_STR_BUF_INVARIANT(b)                                               \
  {                                                                            \
    KMP_DEBUG_ASSERT((b)->str != NULL);                                        \
    KMP_DEBUG_ASSERT((b)->size >= sizeof((b)->bulk));                          \
    KMP_DEBUG_ASSERT((b)->size % sizeof((b)->bulk) == 0);                      \
    KMP_DEBUG_ASSERT((unsigned)(b)->used < (b)->size);                         \
    KMP_DEBUG_ASSERT(                                                          \
        (b)->size == sizeof((b)->bulk) ? (b)->str == &(b)->bulk[0] : 1);       \
    KMP_DEBUG_ASSERT((b)->size > sizeof((b)->bulk) ? (b)->str != &(b)->bulk[0] \
                                                   : 1);                       \
  }

void __kmp_str_buf_clear(kmp_str_buf_t *buffer) {
  KMP_STR_BUF_INVARIANT(buffer);
  if (buffer->used > 0) {
    buffer->used = 0;
    buffer->str[0] = 0;
  }
  KMP_STR_BUF_INVARIANT(buffer);
} // __kmp_str_buf_clear

void __kmp_str_buf_reserve(kmp_str_buf_t *buffer, int size) {
  KMP_STR_BUF_INVARIANT(buffer);
  KMP_DEBUG_ASSERT(size >= 0);

  if (buffer->size < (unsigned int)size) {
    // Calculate buffer size.
    do {
      buffer->size *= 2;
    } while (buffer->size < (unsigned int)size);

    // Enlarge buffer.
    if (buffer->str == &buffer->bulk[0]) {
      buffer->str = (char *)KMP_INTERNAL_MALLOC(buffer->size);
      if (buffer->str == NULL) {
        KMP_FATAL(MemoryAllocFailed);
      }
      KMP_MEMCPY_S(buffer->str, buffer->size, buffer->bulk, buffer->used + 1);
    } else {
      buffer->str = (char *)KMP_INTERNAL_REALLOC(buffer->str, buffer->size);
      if (buffer->str == NULL) {
        KMP_FATAL(MemoryAllocFailed);
      }
    }
  }

  KMP_DEBUG_ASSERT(buffer->size > 0);
  KMP_DEBUG_ASSERT(buffer->size >= (unsigned)size);
  KMP_STR_BUF_INVARIANT(buffer);
} // __kmp_str_buf_reserve

void __kmp_str_buf_detach(kmp_str_buf_t *buffer) {
  KMP_STR_BUF_INVARIANT(buffer);

  // If internal bulk is used, allocate memory and copy it.
  if (buffer->size <= sizeof(buffer->bulk)) {
    buffer->str = (char *)KMP_INTERNAL_MALLOC(buffer->size);
    if (buffer->str == NULL) {
      KMP_FATAL(MemoryAllocFailed);
    }
    KMP_MEMCPY_S(buffer->str, buffer->size, buffer->bulk, buffer->used + 1);
  }
} // __kmp_str_buf_detach

void __kmp_str_buf_free(kmp_str_buf_t *buffer) {
  KMP_STR_BUF_INVARIANT(buffer);
  if (buffer->size > sizeof(buffer->bulk)) {
    KMP_INTERNAL_FREE(buffer->str);
  }
  buffer->str = buffer->bulk;
  buffer->size = sizeof(buffer->bulk);
  buffer->used = 0;
  KMP_STR_BUF_INVARIANT(buffer);
} // __kmp_str_buf_free

void __kmp_str_buf_cat(kmp_str_buf_t *buffer, char const *str, int len) {
  KMP_STR_BUF_INVARIANT(buffer);
  KMP_DEBUG_ASSERT(str != NULL);
  KMP_DEBUG_ASSERT(len >= 0);
  __kmp_str_buf_reserve(buffer, buffer->used + len + 1);
  KMP_MEMCPY(buffer->str + buffer->used, str, len);
  buffer->str[buffer->used + len] = 0;
  buffer->used += len;
  KMP_STR_BUF_INVARIANT(buffer);
} // __kmp_str_buf_cat

void __kmp_str_buf_catbuf(kmp_str_buf_t *dest, const kmp_str_buf_t *src) {
  KMP_DEBUG_ASSERT(dest);
  KMP_DEBUG_ASSERT(src);
  KMP_STR_BUF_INVARIANT(dest);
  KMP_STR_BUF_INVARIANT(src);
  if (!src->str || !src->used)
    return;
  __kmp_str_buf_reserve(dest, dest->used + src->used + 1);
  KMP_MEMCPY(dest->str + dest->used, src->str, src->used);
  dest->str[dest->used + src->used] = 0;
  dest->used += src->used;
  KMP_STR_BUF_INVARIANT(dest);
} // __kmp_str_buf_catbuf

// Return the number of characters written
int __kmp_str_buf_vprint(kmp_str_buf_t *buffer, char const *format,
                         va_list args) {
  int rc;
  KMP_STR_BUF_INVARIANT(buffer);

  for (;;) {
    int const free = buffer->size - buffer->used;
    int size;

    // Try to format string.
    {
/* On Linux* OS Intel(R) 64, vsnprintf() modifies args argument, so vsnprintf()
   crashes if it is called for the second time with the same args. To prevent
   the crash, we have to pass a fresh intact copy of args to vsnprintf() on each
   iteration.

   Unfortunately, standard va_copy() macro is not available on Windows* OS.
   However, it seems vsnprintf() does not modify args argument on Windows* OS.
*/

#if !KMP_OS_WINDOWS
      va_list _args;
      va_copy(_args, args); // Make copy of args.
#define args _args // Substitute args with its copy, _args.
#endif // KMP_OS_WINDOWS
      rc = KMP_VSNPRINTF(buffer->str + buffer->used, free, format, args);
#if !KMP_OS_WINDOWS
#undef args // Remove substitution.
      va_end(_args);
#endif // KMP_OS_WINDOWS
    }

    // No errors, string has been formatted.
    if (rc >= 0 && rc < free) {
      buffer->used += rc;
      break;
    }

    // Error occurred, buffer is too small.
    if (rc >= 0) {
      // C99-conforming implementation of vsnprintf returns required buffer size
      size = buffer->used + rc + 1;
    } else {
      // Older implementations just return -1. Double buffer size.
      size = buffer->size * 2;
    }

    // Enlarge buffer.
    __kmp_str_buf_reserve(buffer, size);

    // And try again.
  }

  KMP_DEBUG_ASSERT(buffer->size > 0);
  KMP_STR_BUF_INVARIANT(buffer);
  return rc;
} // __kmp_str_buf_vprint

// Return the number of characters written
int __kmp_str_buf_print(kmp_str_buf_t *buffer, char const *format, ...) {
  int rc;
  va_list args;
  va_start(args, format);
  rc = __kmp_str_buf_vprint(buffer, format, args);
  va_end(args);
  return rc;
} // __kmp_str_buf_print

/* The function prints specified size to buffer. Size is expressed using biggest
   possible unit, for example 1024 is printed as "1k". */
void __kmp_str_buf_print_size(kmp_str_buf_t *buf, size_t size) {
  char const *names[] = {"", "k", "M", "G", "T", "P", "E", "Z", "Y"};
  int const units = sizeof(names) / sizeof(char const *);
  int u = 0;
  if (size > 0) {
    while ((size % 1024 == 0) && (u + 1 < units)) {
      size = size / 1024;
      ++u;
    }
  }

  __kmp_str_buf_print(buf, "%" KMP_SIZE_T_SPEC "%s", size, names[u]);
} // __kmp_str_buf_print_size

void __kmp_str_fname_init(kmp_str_fname_t *fname, char const *path) {
  fname->path = NULL;
  fname->dir = NULL;
  fname->base = NULL;

  if (path != NULL) {
    char *slash = NULL; // Pointer to the last character of dir.
    char *base = NULL; // Pointer to the beginning of basename.
    fname->path = __kmp_str_format("%s", path);
    // Original code used strdup() function to copy a string, but on Windows* OS
    // Intel(R) 64 it causes assertioon id debug heap, so I had to replace
    // strdup with __kmp_str_format().
    if (KMP_OS_WINDOWS) {
      __kmp_str_replace(fname->path, '\\', '/');
    }
    fname->dir = __kmp_str_format("%s", fname->path);
    slash = strrchr(fname->dir, '/');
    if (KMP_OS_WINDOWS &&
        slash == NULL) { // On Windows* OS, if slash not found,
      char first = TOLOWER(fname->dir[0]); // look for drive.
      if ('a' <= first && first <= 'z' && fname->dir[1] == ':') {
        slash = &fname->dir[1];
      }
    }
    base = (slash == NULL ? fname->dir : slash + 1);
    fname->base = __kmp_str_format("%s", base); // Copy basename
    *base = 0; // and truncate dir.
  }

} // kmp_str_fname_init

void __kmp_str_fname_free(kmp_str_fname_t *fname) {
  __kmp_str_free(&fname->path);
  __kmp_str_free(&fname->dir);
  __kmp_str_free(&fname->base);
} // kmp_str_fname_free

int __kmp_str_fname_match(kmp_str_fname_t const *fname, char const *pattern) {
  int dir_match = 1;
  int base_match = 1;

  if (pattern != NULL) {
    kmp_str_fname_t ptrn;
    __kmp_str_fname_init(&ptrn, pattern);
    dir_match = strcmp(ptrn.dir, "*/") == 0 ||
                (fname->dir != NULL && __kmp_str_eqf(fname->dir, ptrn.dir));
    base_match = strcmp(ptrn.base, "*") == 0 ||
                 (fname->base != NULL && __kmp_str_eqf(fname->base, ptrn.base));
    __kmp_str_fname_free(&ptrn);
  }

  return dir_match && base_match;
} // __kmp_str_fname_match

kmp_str_loc_t __kmp_str_loc_init(char const *psource, int init_fname) {
  kmp_str_loc_t loc;

  loc._bulk = NULL;
  loc.file = NULL;
  loc.func = NULL;
  loc.line = 0;
  loc.col = 0;

  if (psource != NULL) {
    char *str = NULL;
    char *dummy = NULL;
    char *line = NULL;
    char *col = NULL;

    // Copy psource to keep it intact.
    loc._bulk = __kmp_str_format("%s", psource);

    // Parse psource string: ";file;func;line;col;;"
    str = loc._bulk;
    __kmp_str_split(str, ';', &dummy, &str);
    __kmp_str_split(str, ';', &loc.file, &str);
    __kmp_str_split(str, ';', &loc.func, &str);
    __kmp_str_split(str, ';', &line, &str);
    __kmp_str_split(str, ';', &col, &str);

    // Convert line and col into numberic values.
    if (line != NULL) {
      loc.line = atoi(line);
      if (loc.line < 0) {
        loc.line = 0;
      }
    }
    if (col != NULL) {
      loc.col = atoi(col);
      if (loc.col < 0) {
        loc.col = 0;
      }
    }
  }

  __kmp_str_fname_init(&loc.fname, init_fname ? loc.file : NULL);

  return loc;
} // kmp_str_loc_init

void __kmp_str_loc_free(kmp_str_loc_t *loc) {
  __kmp_str_fname_free(&loc->fname);
  __kmp_str_free(&(loc->_bulk));
  loc->file = NULL;
  loc->func = NULL;
} // kmp_str_loc_free

/* This function is intended to compare file names. On Windows* OS file names
   are case-insensitive, so functions performs case-insensitive comparison. On
   Linux* OS it performs case-sensitive comparison. Note: The function returns
   *true* if strings are *equal*. */
int __kmp_str_eqf( // True, if strings are equal, false otherwise.
    char const *lhs, // First string.
    char const *rhs // Second string.
    ) {
  int result;
#if KMP_OS_WINDOWS
  result = (_stricmp(lhs, rhs) == 0);
#else
  result = (strcmp(lhs, rhs) == 0);
#endif
  return result;
} // __kmp_str_eqf

/* This function is like sprintf, but it *allocates* new buffer, which must be
   freed eventually by __kmp_str_free(). The function is very convenient for
   constructing strings, it successfully replaces strdup(), strcat(), it frees
   programmer from buffer allocations and helps to avoid buffer overflows.
   Examples:

   str = __kmp_str_format("%s", orig); //strdup() doesn't care about buffer size
   __kmp_str_free( & str );
   str = __kmp_str_format( "%s%s", orig1, orig2 ); // strcat(), doesn't care
                                                   // about buffer size.
   __kmp_str_free( & str );
   str = __kmp_str_format( "%s/%s.txt", path, file ); // constructing string.
   __kmp_str_free( & str );

   Performance note:
   This function allocates memory with malloc() calls, so do not call it from
   performance-critical code. In performance-critical code consider using
   kmp_str_buf_t instead, since it uses stack-allocated buffer for short
   strings.

   Why does this function use malloc()?
   1. __kmp_allocate() returns cache-aligned memory allocated with malloc().
      There are no reasons in using __kmp_allocate() for strings due to extra
      overhead while cache-aligned memory is not necessary.
   2. __kmp_thread_malloc() cannot be used because it requires pointer to thread
      structure. We need to perform string operations during library startup
      (for example, in __kmp_register_library_startup()) when no thread
      structures are allocated yet.
   So standard malloc() is the only available option.
*/

char *__kmp_str_format( // Allocated string.
    char const *format, // Format string.
    ... // Other parameters.
    ) {
  va_list args;
  int size = 512;
  char *buffer = NULL;
  int rc;

  // Allocate buffer.
  buffer = (char *)KMP_INTERNAL_MALLOC(size);
  if (buffer == NULL) {
    KMP_FATAL(MemoryAllocFailed);
  }

  for (;;) {
    // Try to format string.
    va_start(args, format);
    rc = KMP_VSNPRINTF(buffer, size, format, args);
    va_end(args);

    // No errors, string has been formatted.
    if (rc >= 0 && rc < size) {
      break;
    }

    // Error occurred, buffer is too small.
    if (rc >= 0) {
      // C99-conforming implementation of vsnprintf returns required buffer
      // size.
      size = rc + 1;
    } else {
      // Older implementations just return -1.
      size = size * 2;
    }

    // Enlarge buffer and try again.
    buffer = (char *)KMP_INTERNAL_REALLOC(buffer, size);
    if (buffer == NULL) {
      KMP_FATAL(MemoryAllocFailed);
    }
  }

  return buffer;
} // func __kmp_str_format

void __kmp_str_free(char **str) {
  KMP_DEBUG_ASSERT(str != NULL);
  KMP_INTERNAL_FREE(*str);
  *str = NULL;
} // func __kmp_str_free

/* If len is zero, returns true iff target and data have exact case-insensitive
   match. If len is negative, returns true iff target is a case-insensitive
   substring of data. If len is positive, returns true iff target is a
   case-insensitive substring of data or vice versa, and neither is shorter than
   len. */
int __kmp_str_match(char const *target, int len, char const *data) {
  int i;
  if (target == NULL || data == NULL) {
    return FALSE;
  }
  for (i = 0; target[i] && data[i]; ++i) {
    if (TOLOWER(target[i]) != TOLOWER(data[i])) {
      return FALSE;
    }
  }
  return ((len > 0) ? i >= len : (!target[i] && (len || !data[i])));
} // __kmp_str_match

int __kmp_str_match_false(char const *data) {
  int result =
      __kmp_str_match("false", 1, data) || __kmp_str_match("off", 2, data) ||
      __kmp_str_match("0", 1, data) || __kmp_str_match(".false.", 2, data) ||
      __kmp_str_match(".f.", 2, data) || __kmp_str_match("no", 1, data) ||
      __kmp_str_match("disabled", 0, data);
  return result;
} // __kmp_str_match_false

int __kmp_str_match_true(char const *data) {
  int result =
      __kmp_str_match("true", 1, data) || __kmp_str_match("on", 2, data) ||
      __kmp_str_match("1", 1, data) || __kmp_str_match(".true.", 2, data) ||
      __kmp_str_match(".t.", 2, data) || __kmp_str_match("yes", 1, data) ||
      __kmp_str_match("enabled", 0, data);
  return result;
} // __kmp_str_match_true

void __kmp_str_replace(char *str, char search_for, char replace_with) {
  char *found = NULL;

  found = strchr(str, search_for);
  while (found) {
    *found = replace_with;
    found = strchr(found + 1, search_for);
  }
} // __kmp_str_replace

void __kmp_str_split(char *str, // I: String to split.
                     char delim, // I: Character to split on.
                     char **head, // O: Pointer to head (may be NULL).
                     char **tail // O: Pointer to tail (may be NULL).
                     ) {
  char *h = str;
  char *t = NULL;
  if (str != NULL) {
    char *ptr = strchr(str, delim);
    if (ptr != NULL) {
      *ptr = 0;
      t = ptr + 1;
    }
  }
  if (head != NULL) {
    *head = h;
  }
  if (tail != NULL) {
    *tail = t;
  }
} // __kmp_str_split

/* strtok_r() is not available on Windows* OS. This function reimplements
   strtok_r(). */
char *__kmp_str_token(
    char *str, // String to split into tokens. Note: String *is* modified!
    char const *delim, // Delimiters.
    char **buf // Internal buffer.
    ) {
  char *token = NULL;
#if KMP_OS_WINDOWS
  // On Windows* OS there is no strtok_r() function. Let us implement it.
  if (str != NULL) {
    *buf = str; // First call, initialize buf.
  }
  *buf += strspn(*buf, delim); // Skip leading delimiters.
  if (**buf != 0) { // Rest of the string is not yet empty.
    token = *buf; // Use it as result.
    *buf += strcspn(*buf, delim); // Skip non-delimiters.
    if (**buf != 0) { // Rest of the string is not yet empty.
      **buf = 0; // Terminate token here.
      *buf += 1; // Advance buf to start with the next token next time.
    }
  }
#else
  // On Linux* OS and OS X*, strtok_r() is available. Let us use it.
  token = strtok_r(str, delim, buf);
#endif
  return token;
} // __kmp_str_token

int __kmp_str_to_int(char const *str, char sentinel) {
  int result, factor;
  char const *t;

  result = 0;

  for (t = str; *t != '\0'; ++t) {
    if (*t < '0' || *t > '9')
      break;
    result = (result * 10) + (*t - '0');
  }

  switch (*t) {
  case '\0': /* the current default for no suffix is bytes */
    factor = 1;
    break;
  case 'b':
  case 'B': /* bytes */
    ++t;
    factor = 1;
    break;
  case 'k':
  case 'K': /* kilo-bytes */
    ++t;
    factor = 1024;
    break;
  case 'm':
  case 'M': /* mega-bytes */
    ++t;
    factor = (1024 * 1024);
    break;
  default:
    if (*t != sentinel)
      return (-1);
    t = "";
    factor = 1;
  }

  if (result > (INT_MAX / factor))
    result = INT_MAX;
  else
    result *= factor;

  return (*t != 0 ? 0 : result);
} // __kmp_str_to_int

/* The routine parses input string. It is expected it is a unsigned integer with
   optional unit. Units are: "b" for bytes, "kb" or just "k" for kilobytes, "mb"
   or "m" for megabytes, ..., "yb" or "y" for yottabytes. :-) Unit name is
   case-insensitive. The routine returns 0 if everything is ok, or error code:
   -1 in case of overflow, -2 in case of unknown unit. *size is set to parsed
   value. In case of overflow *size is set to KMP_SIZE_T_MAX, in case of unknown
   unit *size is set to zero. */
void __kmp_str_to_size( // R: Error code.
    char const *str, // I: String of characters, unsigned number and unit ("b",
    // "kb", etc).
    size_t *out, // O: Parsed number.
    size_t dfactor, // I: The factor if none of the letters specified.
    char const **error // O: Null if everything is ok, error message otherwise.
    ) {

  size_t value = 0;
  size_t factor = 0;
  int overflow = 0;
  int i = 0;
  int digit;

  KMP_DEBUG_ASSERT(str != NULL);

  // Skip spaces.
  while (str[i] == ' ' || str[i] == '\t') {
    ++i;
  }

  // Parse number.
  if (str[i] < '0' || str[i] > '9') {
    *error = KMP_I18N_STR(NotANumber);
    return;
  }
  do {
    digit = str[i] - '0';
    overflow = overflow || (value > (KMP_SIZE_T_MAX - digit) / 10);
    value = (value * 10) + digit;
    ++i;
  } while (str[i] >= '0' && str[i] <= '9');

  // Skip spaces.
  while (str[i] == ' ' || str[i] == '\t') {
    ++i;
  }

// Parse unit.
#define _case(ch, exp)                                                         \
  case ch:                                                                     \
  case ch - ('a' - 'A'): {                                                     \
    size_t shift = (exp)*10;                                                   \
    ++i;                                                                       \
    if (shift < sizeof(size_t) * 8) {                                          \
      factor = (size_t)(1) << shift;                                           \
    } else {                                                                   \
      overflow = 1;                                                            \
    }                                                                          \
  } break;
  switch (str[i]) {
    _case('k', 1); // Kilo
    _case('m', 2); // Mega
    _case('g', 3); // Giga
    _case('t', 4); // Tera
    _case('p', 5); // Peta
    _case('e', 6); // Exa
    _case('z', 7); // Zetta
    _case('y', 8); // Yotta
    // Oops. No more units...
  }
#undef _case
  if (str[i] == 'b' || str[i] == 'B') { // Skip optional "b".
    if (factor == 0) {
      factor = 1;
    }
    ++i;
  }
  if (!(str[i] == ' ' || str[i] == '\t' || str[i] == 0)) { // Bad unit
    *error = KMP_I18N_STR(BadUnit);
    return;
  }

  if (factor == 0) {
    factor = dfactor;
  }

  // Apply factor.
  overflow = overflow || (value > (KMP_SIZE_T_MAX / factor));
  value *= factor;

  // Skip spaces.
  while (str[i] == ' ' || str[i] == '\t') {
    ++i;
  }

  if (str[i] != 0) {
    *error = KMP_I18N_STR(IllegalCharacters);
    return;
  }

  if (overflow) {
    *error = KMP_I18N_STR(ValueTooLarge);
    *out = KMP_SIZE_T_MAX;
    return;
  }

  *error = NULL;
  *out = value;
} // __kmp_str_to_size

void __kmp_str_to_uint( // R: Error code.
    char const *str, // I: String of characters, unsigned number.
    kmp_uint64 *out, // O: Parsed number.
    char const **error // O: Null if everything is ok, error message otherwise.
    ) {
  size_t value = 0;
  int overflow = 0;
  int i = 0;
  int digit;

  KMP_DEBUG_ASSERT(str != NULL);

  // Skip spaces.
  while (str[i] == ' ' || str[i] == '\t') {
    ++i;
  }

  // Parse number.
  if (str[i] < '0' || str[i] > '9') {
    *error = KMP_I18N_STR(NotANumber);
    return;
  }
  do {
    digit = str[i] - '0';
    overflow = overflow || (value > (KMP_SIZE_T_MAX - digit) / 10);
    value = (value * 10) + digit;
    ++i;
  } while (str[i] >= '0' && str[i] <= '9');

  // Skip spaces.
  while (str[i] == ' ' || str[i] == '\t') {
    ++i;
  }

  if (str[i] != 0) {
    *error = KMP_I18N_STR(IllegalCharacters);
    return;
  }

  if (overflow) {
    *error = KMP_I18N_STR(ValueTooLarge);
    *out = (kmp_uint64)-1;
    return;
  }

  *error = NULL;
  *out = value;
} // __kmp_str_to_unit

// end of file //
