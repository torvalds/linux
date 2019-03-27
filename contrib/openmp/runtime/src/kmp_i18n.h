/*
 * kmp_i18n.h
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_I18N_H
#define KMP_I18N_H

#include "kmp_str.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* kmp_i18n_id.inc defines kmp_i18n_id_t type. It is an enumeration with
   identifiers of all the messages in the catalog. There is one special
   identifier: kmp_i18n_null, which denotes absence of message. */
#include "kmp_i18n_id.inc" // Generated file. Do not edit it manually.

/* Low-level functions handling message catalog. __kmp_i18n_open() opens message
   catalog, __kmp_i18n_closes() it. Explicit opening is not required: if message
   catalog is not yet open, __kmp_i18n_catgets() will open it implicitly.
   However, catalog should be explicitly closed, otherwise resources (mamory,
   handles) may leak.

   __kmp_i18n_catgets() returns read-only string. It should not be freed.

   KMP_I18N_STR macro simplifies acces to strings in message catalog a bit.
   Following two lines are equivalent:

   __kmp_i18n_catgets( kmp_i18n_str_Warning )
   KMP_I18N_STR( Warning )
*/

void __kmp_i18n_catopen();
void __kmp_i18n_catclose();
char const *__kmp_i18n_catgets(kmp_i18n_id_t id);

#define KMP_I18N_STR(id) __kmp_i18n_catgets(kmp_i18n_str_##id)

/* High-level interface for printing strings targeted to the user.

   All the strings are divided into 3 types:
   * messages,
   * hints,
   * system errors.

   There are 3 kind of message severities:
   * informational messages,
   * warnings (non-fatal errors),
   * fatal errors.

   For example:
     OMP: Warning #2: Cannot open message catalog "libguide.cat":   (1)
     OMP: System error #2: No such file or directory                (2)
     OMP: Hint: Please check NLSPATH environment variable.          (3)
     OMP: Info #3: Default messages will be used.                   (4)

   where
   (1) is a message of warning severity,
   (2) is a system error caused the previous warning,
   (3) is a hint for the user how to fix the problem,
   (4) is a message of informational severity.

   Usage in complex cases (message is accompanied with hints and system errors):

   int error = errno; // We need save errno immediately, because it may
                      // be changed.
   __kmp_msg(
       kmp_ms_warning,                        // Severity
       KMP_MSG( CantOpenMessageCatalog, name ), // Primary message
       KMP_ERR( error ),                      // System error
       KMP_HNT( CheckNLSPATH ),               // Hint
       __kmp_msg_null                         // Variadic argument list finisher
   );

   Usage in simple cases (just a message, no system errors or hints):
   KMP_INFORM( WillUseDefaultMessages );
   KMP_WARNING( CantOpenMessageCatalog, name );
   KMP_FATAL( StackOverlap );
   KMP_SYSFAIL( "pthread_create", status );
   KMP_CHECK_SYSFAIL( "pthread_create", status );
   KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
*/

enum kmp_msg_type {
  kmp_mt_dummy = 0, // Special type for internal purposes.
  kmp_mt_mesg =
      4, // Primary OpenMP message, could be information, warning, or fatal.
  kmp_mt_hint = 5, // Hint to the user.
  kmp_mt_syserr = -1 // System error message.
}; // enum kmp_msg_type
typedef enum kmp_msg_type kmp_msg_type_t;

struct kmp_msg {
  kmp_msg_type_t type;
  int num;
  char *str;
  int len;
}; // struct kmp_message
typedef struct kmp_msg kmp_msg_t;

// Special message to denote the end of variadic list of arguments.
extern kmp_msg_t __kmp_msg_null;

// Helper functions. Creates messages either from message catalog or from
// system. Note: these functions allocate memory. You should pass created
// messages to __kmp_msg() function, it will print messages and destroy them.
kmp_msg_t __kmp_msg_format(unsigned id_arg, ...);
kmp_msg_t __kmp_msg_error_code(int code);
kmp_msg_t __kmp_msg_error_mesg(char const *mesg);

// Helper macros to make calls shorter.
#define KMP_MSG(...) __kmp_msg_format(kmp_i18n_msg_##__VA_ARGS__)
#define KMP_HNT(...) __kmp_msg_format(kmp_i18n_hnt_##__VA_ARGS__)
#define KMP_SYSERRCODE(code) __kmp_msg_error_code(code)
#define KMP_SYSERRMESG(mesg) __kmp_msg_error_mesg(mesg)
#define KMP_ERR KMP_SYSERRCODE

// Message severity.
enum kmp_msg_severity {
  kmp_ms_inform, // Just information for the user.
  kmp_ms_warning, // Non-fatal error, execution continues.
  kmp_ms_fatal // Fatal error, program aborts.
}; // enum kmp_msg_severity
typedef enum kmp_msg_severity kmp_msg_severity_t;

// Primary function for printing messages for the user. The first message is
// mandatory. Any number of system errors and hints may be specified. Argument
// list must be finished with __kmp_msg_null.
void __kmp_msg(kmp_msg_severity_t severity, kmp_msg_t message, ...);
KMP_NORETURN void __kmp_fatal(kmp_msg_t message, ...);

// Helper macros to make calls shorter in simple cases.
#define KMP_INFORM(...)                                                        \
  __kmp_msg(kmp_ms_inform, KMP_MSG(__VA_ARGS__), __kmp_msg_null)
#define KMP_WARNING(...)                                                       \
  __kmp_msg(kmp_ms_warning, KMP_MSG(__VA_ARGS__), __kmp_msg_null)
#define KMP_FATAL(...) __kmp_fatal(KMP_MSG(__VA_ARGS__), __kmp_msg_null)
#define KMP_SYSFAIL(func, error)                                               \
  __kmp_fatal(KMP_MSG(FunctionError, func), KMP_SYSERRCODE(error),             \
              __kmp_msg_null)

// Check error, if not zero, generate fatal error message.
#define KMP_CHECK_SYSFAIL(func, error)                                         \
  {                                                                            \
    if (error) {                                                               \
      KMP_SYSFAIL(func, error);                                                \
    }                                                                          \
  }

// Check status, if not zero, generate fatal error message using errno.
#define KMP_CHECK_SYSFAIL_ERRNO(func, status)                                  \
  {                                                                            \
    if (status != 0) {                                                         \
      int error = errno;                                                       \
      KMP_SYSFAIL(func, error);                                                \
    }                                                                          \
  }

#ifdef KMP_DEBUG
void __kmp_i18n_dump_catalog(kmp_str_buf_t *buffer);
#endif // KMP_DEBUG

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // KMP_I18N_H

// end of file //
