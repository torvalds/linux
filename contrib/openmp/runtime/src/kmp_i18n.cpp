/*
 * kmp_i18n.cpp
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp_i18n.h"

#include "kmp.h"
#include "kmp_debug.h"
#include "kmp_io.h" // __kmp_printf.
#include "kmp_lock.h"
#include "kmp_os.h"

#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kmp_environment.h"
#include "kmp_i18n_default.inc"
#include "kmp_str.h"

#undef KMP_I18N_OK

#define get_section(id) ((id) >> 16)
#define get_number(id) ((id)&0xFFFF)

kmp_msg_t __kmp_msg_null = {kmp_mt_dummy, 0, NULL, 0};
static char const *no_message_available = "(No message available)";

static void __kmp_msg(kmp_msg_severity_t severity, kmp_msg_t message,
                      va_list ap);

enum kmp_i18n_cat_status {
  KMP_I18N_CLOSED, // Not yet opened or closed.
  KMP_I18N_OPENED, // Opened successfully, ready to use.
  KMP_I18N_ABSENT // Opening failed, message catalog should not be used.
}; // enum kmp_i18n_cat_status
typedef enum kmp_i18n_cat_status kmp_i18n_cat_status_t;
static volatile kmp_i18n_cat_status_t status = KMP_I18N_CLOSED;

/* Message catalog is opened at first usage, so we have to synchronize opening
   to avoid race and multiple openings.

   Closing does not require synchronization, because catalog is closed very late
   at library shutting down, when no other threads are alive.  */

static void __kmp_i18n_do_catopen();
static kmp_bootstrap_lock_t lock = KMP_BOOTSTRAP_LOCK_INITIALIZER(lock);
// `lock' variable may be placed into __kmp_i18n_catopen function because it is
// used only by that function. But we afraid a (buggy) compiler may treat it
// wrongly. So we put it outside of function just in case.

void __kmp_i18n_catopen() {
  if (status == KMP_I18N_CLOSED) {
    __kmp_acquire_bootstrap_lock(&lock);
    if (status == KMP_I18N_CLOSED) {
      __kmp_i18n_do_catopen();
    }
    __kmp_release_bootstrap_lock(&lock);
  }
} // func __kmp_i18n_catopen

/* Linux* OS and OS X* part */
#if KMP_OS_UNIX
#define KMP_I18N_OK

#include <nl_types.h>

#define KMP_I18N_NULLCAT ((nl_catd)(-1))
static nl_catd cat = KMP_I18N_NULLCAT; // !!! Shall it be volatile?
static char const *name =
    (KMP_VERSION_MAJOR == 4 ? "libguide.cat" : "libomp.cat");

/* Useful links:
http://www.opengroup.org/onlinepubs/000095399/basedefs/xbd_chap08.html#tag_08_02
http://www.opengroup.org/onlinepubs/000095399/functions/catopen.html
http://www.opengroup.org/onlinepubs/000095399/functions/setlocale.html
*/

void __kmp_i18n_do_catopen() {
  int english = 0;
  char *lang = __kmp_env_get("LANG");
  // TODO: What about LC_ALL or LC_MESSAGES?

  KMP_DEBUG_ASSERT(status == KMP_I18N_CLOSED);
  KMP_DEBUG_ASSERT(cat == KMP_I18N_NULLCAT);

  english = lang == NULL || // In all these cases English language is used.
            strcmp(lang, "") == 0 || strcmp(lang, " ") == 0 ||
            // Workaround for Fortran RTL bug DPD200137873 "Fortran runtime
            // resets LANG env var to space if it is not set".
            strcmp(lang, "C") == 0 || strcmp(lang, "POSIX") == 0;

  if (!english) { // English language is not yet detected, let us continue.
    // Format of LANG is: [language[_territory][.codeset][@modifier]]
    // Strip all parts except language.
    char *tail = NULL;
    __kmp_str_split(lang, '@', &lang, &tail);
    __kmp_str_split(lang, '.', &lang, &tail);
    __kmp_str_split(lang, '_', &lang, &tail);
    english = (strcmp(lang, "en") == 0);
  }

  KMP_INTERNAL_FREE(lang);

  // Do not try to open English catalog because internal messages are
  // exact copy of messages in English catalog.
  if (english) {
    status = KMP_I18N_ABSENT; // mark catalog as absent so it will not
    // be re-opened.
    return;
  }

  cat = catopen(name, 0);
  // TODO: Why do we pass 0 in flags?
  status = (cat == KMP_I18N_NULLCAT ? KMP_I18N_ABSENT : KMP_I18N_OPENED);

  if (status == KMP_I18N_ABSENT) {
    if (__kmp_generate_warnings > kmp_warnings_low) {
      // AC: only issue warning in case explicitly asked to
      int error = errno; // Save errno immediately.
      char *nlspath = __kmp_env_get("NLSPATH");
      char *lang = __kmp_env_get("LANG");

      // Infinite recursion will not occur -- status is KMP_I18N_ABSENT now, so
      // __kmp_i18n_catgets() will not try to open catalog, but will return
      // default message.
      kmp_msg_t err_code = KMP_ERR(error);
      __kmp_msg(kmp_ms_warning, KMP_MSG(CantOpenMessageCatalog, name), err_code,
                KMP_HNT(CheckEnvVar, "NLSPATH", nlspath),
                KMP_HNT(CheckEnvVar, "LANG", lang), __kmp_msg_null);
      if (__kmp_generate_warnings == kmp_warnings_off) {
        __kmp_str_free(&err_code.str);
      }

      KMP_INFORM(WillUseDefaultMessages);
      KMP_INTERNAL_FREE(nlspath);
      KMP_INTERNAL_FREE(lang);
    }
  } else { // status == KMP_I18N_OPENED
    int section = get_section(kmp_i18n_prp_Version);
    int number = get_number(kmp_i18n_prp_Version);
    char const *expected = __kmp_i18n_default_table.sect[section].str[number];
    // Expected version of the catalog.
    kmp_str_buf_t version; // Actual version of the catalog.
    __kmp_str_buf_init(&version);
    __kmp_str_buf_print(&version, "%s", catgets(cat, section, number, NULL));

    // String returned by catgets is invalid after closing catalog, so copy it.
    if (strcmp(version.str, expected) != 0) {
      __kmp_i18n_catclose(); // Close bad catalog.
      status = KMP_I18N_ABSENT; // And mark it as absent.
      if (__kmp_generate_warnings > kmp_warnings_low) {
        // AC: only issue warning in case explicitly asked to
        // And now print a warning using default messages.
        char const *name = "NLSPATH";
        char const *nlspath = __kmp_env_get(name);
        __kmp_msg(kmp_ms_warning,
                  KMP_MSG(WrongMessageCatalog, name, version.str, expected),
                  KMP_HNT(CheckEnvVar, name, nlspath), __kmp_msg_null);
        KMP_INFORM(WillUseDefaultMessages);
        KMP_INTERNAL_FREE(CCAST(char *, nlspath));
      } // __kmp_generate_warnings
    }
    __kmp_str_buf_free(&version);
  }
} // func __kmp_i18n_do_catopen

void __kmp_i18n_catclose() {
  if (status == KMP_I18N_OPENED) {
    KMP_DEBUG_ASSERT(cat != KMP_I18N_NULLCAT);
    catclose(cat);
    cat = KMP_I18N_NULLCAT;
  }
  status = KMP_I18N_CLOSED;
} // func __kmp_i18n_catclose

char const *__kmp_i18n_catgets(kmp_i18n_id_t id) {

  int section = get_section(id);
  int number = get_number(id);
  char const *message = NULL;

  if (1 <= section && section <= __kmp_i18n_default_table.size) {
    if (1 <= number && number <= __kmp_i18n_default_table.sect[section].size) {
      if (status == KMP_I18N_CLOSED) {
        __kmp_i18n_catopen();
      }
      if (status == KMP_I18N_OPENED) {
        message = catgets(cat, section, number,
                          __kmp_i18n_default_table.sect[section].str[number]);
      }
      if (message == NULL) {
        message = __kmp_i18n_default_table.sect[section].str[number];
      }
    }
  }
  if (message == NULL) {
    message = no_message_available;
  }
  return message;

} // func __kmp_i18n_catgets

#endif // KMP_OS_UNIX

/* Windows* OS part. */

#if KMP_OS_WINDOWS
#define KMP_I18N_OK

#include "kmp_environment.h"
#include <windows.h>

#define KMP_I18N_NULLCAT NULL
static HMODULE cat = KMP_I18N_NULLCAT; // !!! Shall it be volatile?
static char const *name =
    (KMP_VERSION_MAJOR == 4 ? "libguide40ui.dll" : "libompui.dll");

static kmp_i18n_table_t table = {0, NULL};
// Messages formatted by FormatMessage() should be freed, but catgets()
// interface assumes user will not free messages. So we cache all the retrieved
// messages in the table, which are freed at catclose().
static UINT const default_code_page = CP_OEMCP;
static UINT code_page = default_code_page;

static char const *___catgets(kmp_i18n_id_t id);
static UINT get_code_page();
static void kmp_i18n_table_free(kmp_i18n_table_t *table);

static UINT get_code_page() {

  UINT cp = default_code_page;
  char const *value = __kmp_env_get("KMP_CODEPAGE");
  if (value != NULL) {
    if (_stricmp(value, "ANSI") == 0) {
      cp = CP_ACP;
    } else if (_stricmp(value, "OEM") == 0) {
      cp = CP_OEMCP;
    } else if (_stricmp(value, "UTF-8") == 0 || _stricmp(value, "UTF8") == 0) {
      cp = CP_UTF8;
    } else if (_stricmp(value, "UTF-7") == 0 || _stricmp(value, "UTF7") == 0) {
      cp = CP_UTF7;
    } else {
      // !!! TODO: Issue a warning?
    }
  }
  KMP_INTERNAL_FREE((void *)value);
  return cp;

} // func get_code_page

static void kmp_i18n_table_free(kmp_i18n_table_t *table) {
  int s;
  int m;
  for (s = 0; s < table->size; ++s) {
    for (m = 0; m < table->sect[s].size; ++m) {
      // Free message.
      KMP_INTERNAL_FREE((void *)table->sect[s].str[m]);
      table->sect[s].str[m] = NULL;
    }
    table->sect[s].size = 0;
    // Free section itself.
    KMP_INTERNAL_FREE((void *)table->sect[s].str);
    table->sect[s].str = NULL;
  }
  table->size = 0;
  KMP_INTERNAL_FREE((void *)table->sect);
  table->sect = NULL;
} // kmp_i18n_table_free

void __kmp_i18n_do_catopen() {

  LCID locale_id = GetThreadLocale();
  WORD lang_id = LANGIDFROMLCID(locale_id);
  WORD primary_lang_id = PRIMARYLANGID(lang_id);
  kmp_str_buf_t path;

  KMP_DEBUG_ASSERT(status == KMP_I18N_CLOSED);
  KMP_DEBUG_ASSERT(cat == KMP_I18N_NULLCAT);

  __kmp_str_buf_init(&path);

  // Do not try to open English catalog because internal messages are exact copy
  // of messages in English catalog.
  if (primary_lang_id == LANG_ENGLISH) {
    status = KMP_I18N_ABSENT; // mark catalog as absent so it will not
    // be re-opened.
    goto end;
  }

  // Construct resource DLL name.
  /* Simple LoadLibrary( name ) is not suitable due to security issue (see
     http://www.microsoft.com/technet/security/advisory/2269637.mspx). We have
     to specify full path to the message catalog.  */
  {
    // Get handle of our DLL first.
    HMODULE handle;
    BOOL brc = GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&__kmp_i18n_do_catopen), &handle);
    if (!brc) { // Error occurred.
      status = KMP_I18N_ABSENT; // mark catalog as absent so it will not be
      // re-opened.
      goto end;
      // TODO: Enable multiple messages (KMP_MSG) to be passed to __kmp_msg; and
      // print a proper warning.
    }

    // Now get path to the our DLL.
    for (;;) {
      DWORD drc = GetModuleFileName(handle, path.str, path.size);
      if (drc == 0) { // Error occurred.
        status = KMP_I18N_ABSENT;
        goto end;
      }
      if (drc < path.size) {
        path.used = drc;
        break;
      }
      __kmp_str_buf_reserve(&path, path.size * 2);
    }

    // Now construct the name of message catalog.
    kmp_str_fname fname;
    __kmp_str_fname_init(&fname, path.str);
    __kmp_str_buf_clear(&path);
    __kmp_str_buf_print(&path, "%s%lu/%s", fname.dir,
                        (unsigned long)(locale_id), name);
    __kmp_str_fname_free(&fname);
  }

  // For security reasons, use LoadLibraryEx() and load message catalog as a
  // data file.
  cat = LoadLibraryEx(path.str, NULL, LOAD_LIBRARY_AS_DATAFILE);
  status = (cat == KMP_I18N_NULLCAT ? KMP_I18N_ABSENT : KMP_I18N_OPENED);

  if (status == KMP_I18N_ABSENT) {
    if (__kmp_generate_warnings > kmp_warnings_low) {
      // AC: only issue warning in case explicitly asked to
      DWORD error = GetLastError();
      // Infinite recursion will not occur -- status is KMP_I18N_ABSENT now, so
      // __kmp_i18n_catgets() will not try to open catalog but will return
      // default message.
      /* If message catalog for another architecture found (e.g. OpenMP RTL for
         IA-32 architecture opens libompui.dll for Intel(R) 64) Windows* OS
         returns error 193 (ERROR_BAD_EXE_FORMAT). However, FormatMessage fails
         to return a message for this error, so user will see:

         OMP: Warning #2: Cannot open message catalog "1041\libompui.dll":
         OMP: System error #193: (No system error message available)
         OMP: Info #3: Default messages will be used.

         Issue hint in this case so cause of trouble is more understandable. */
      kmp_msg_t err_code = KMP_SYSERRCODE(error);
      __kmp_msg(kmp_ms_warning, KMP_MSG(CantOpenMessageCatalog, path.str),
                err_code, (error == ERROR_BAD_EXE_FORMAT
                               ? KMP_HNT(BadExeFormat, path.str, KMP_ARCH_STR)
                               : __kmp_msg_null),
                __kmp_msg_null);
      if (__kmp_generate_warnings == kmp_warnings_off) {
        __kmp_str_free(&err_code.str);
      }
      KMP_INFORM(WillUseDefaultMessages);
    }
  } else { // status == KMP_I18N_OPENED

    int section = get_section(kmp_i18n_prp_Version);
    int number = get_number(kmp_i18n_prp_Version);
    char const *expected = __kmp_i18n_default_table.sect[section].str[number];
    kmp_str_buf_t version; // Actual version of the catalog.
    __kmp_str_buf_init(&version);
    __kmp_str_buf_print(&version, "%s", ___catgets(kmp_i18n_prp_Version));
    // String returned by catgets is invalid after closing catalog, so copy it.
    if (strcmp(version.str, expected) != 0) {
      // Close bad catalog.
      __kmp_i18n_catclose();
      status = KMP_I18N_ABSENT; // And mark it as absent.
      if (__kmp_generate_warnings > kmp_warnings_low) {
        // And now print a warning using default messages.
        __kmp_msg(kmp_ms_warning,
                  KMP_MSG(WrongMessageCatalog, path.str, version.str, expected),
                  __kmp_msg_null);
        KMP_INFORM(WillUseDefaultMessages);
      } // __kmp_generate_warnings
    }
    __kmp_str_buf_free(&version);
  }
  code_page = get_code_page();

end:
  __kmp_str_buf_free(&path);
  return;
} // func __kmp_i18n_do_catopen

void __kmp_i18n_catclose() {
  if (status == KMP_I18N_OPENED) {
    KMP_DEBUG_ASSERT(cat != KMP_I18N_NULLCAT);
    kmp_i18n_table_free(&table);
    FreeLibrary(cat);
    cat = KMP_I18N_NULLCAT;
  }
  code_page = default_code_page;
  status = KMP_I18N_CLOSED;
} // func __kmp_i18n_catclose

/* We use FormatMessage() to get strings from catalog, get system error
   messages, etc. FormatMessage() tends to return Windows* OS-style
   end-of-lines, "\r\n". When string is printed, printf() also replaces all the
   occurrences of "\n" with "\r\n" (again!), so sequences like "\r\r\r\n"
   appear in output. It is not too good.

   Additional mess comes from message catalog: Our catalog source en_US.mc file
   (generated by message-converter.pl) contains only "\n" characters, but
   en_US_msg_1033.bin file (produced by mc.exe) may contain "\r\n" or just "\n".
   This mess goes from en_US_msg_1033.bin file to message catalog,
   libompui.dll. For example, message

   Error

   (there is "\n" at the end) is compiled by mc.exe to "Error\r\n", while

   OMP: Error %1!d!: %2!s!\n

   (there is "\n" at the end as well) is compiled to "OMP: Error %1!d!:
   %2!s!\r\n\n".

   Thus, stripping all "\r" normalizes string and returns it to canonical form,
   so printf() will produce correct end-of-line sequences.

   ___strip_crs() serves for this purpose: it removes all the occurrences of
   "\r" in-place and returns new length of string.  */
static int ___strip_crs(char *str) {
  int in = 0; // Input character index.
  int out = 0; // Output character index.
  for (;;) {
    if (str[in] != '\r') {
      str[out] = str[in];
      ++out;
    }
    if (str[in] == 0) {
      break;
    }
    ++in;
  }
  return out - 1;
} // func __strip_crs

static char const *___catgets(kmp_i18n_id_t id) {

  char *result = NULL;
  PVOID addr = NULL;
  wchar_t *wmsg = NULL;
  DWORD wlen = 0;
  char *msg = NULL;
  int len = 0;
  int rc;

  KMP_DEBUG_ASSERT(cat != KMP_I18N_NULLCAT);
  wlen = // wlen does *not* include terminating null.
      FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_FROM_HMODULE |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     cat, id,
                     0, // LangId
                     (LPWSTR)&addr,
                     0, // Size in elements, not in bytes.
                     NULL);
  if (wlen <= 0) {
    goto end;
  }
  wmsg = (wchar_t *)addr; // Warning: wmsg may be not nul-terminated!

  // Calculate length of multibyte message.
  // Since wlen does not include terminating null, len does not include it also.
  len = WideCharToMultiByte(code_page,
                            0, // Flags.
                            wmsg, wlen, // Wide buffer and size.
                            NULL, 0, // Buffer and size.
                            NULL, NULL // Default char and used default char.
                            );
  if (len <= 0) {
    goto end;
  }

  // Allocate memory.
  msg = (char *)KMP_INTERNAL_MALLOC(len + 1);

  // Convert wide message to multibyte one.
  rc = WideCharToMultiByte(code_page,
                           0, // Flags.
                           wmsg, wlen, // Wide buffer and size.
                           msg, len, // Buffer and size.
                           NULL, NULL // Default char and used default char.
                           );
  if (rc <= 0 || rc > len) {
    goto end;
  }
  KMP_DEBUG_ASSERT(rc == len);
  len = rc;
  msg[len] = 0; // Put terminating null to the end.

  // Stripping all "\r" before stripping last end-of-line simplifies the task.
  len = ___strip_crs(msg);

  // Every message in catalog is terminated with "\n". Strip it.
  if (len >= 1 && msg[len - 1] == '\n') {
    --len;
    msg[len] = 0;
  }

  // Everything looks ok.
  result = msg;
  msg = NULL;

end:

  if (msg != NULL) {
    KMP_INTERNAL_FREE(msg);
  }
  if (wmsg != NULL) {
    LocalFree(wmsg);
  }

  return result;

} // ___catgets

char const *__kmp_i18n_catgets(kmp_i18n_id_t id) {

  int section = get_section(id);
  int number = get_number(id);
  char const *message = NULL;

  if (1 <= section && section <= __kmp_i18n_default_table.size) {
    if (1 <= number && number <= __kmp_i18n_default_table.sect[section].size) {
      if (status == KMP_I18N_CLOSED) {
        __kmp_i18n_catopen();
      }
      if (cat != KMP_I18N_NULLCAT) {
        if (table.size == 0) {
          table.sect = (kmp_i18n_section_t *)KMP_INTERNAL_CALLOC(
              (__kmp_i18n_default_table.size + 2), sizeof(kmp_i18n_section_t));
          table.size = __kmp_i18n_default_table.size;
        }
        if (table.sect[section].size == 0) {
          table.sect[section].str = (const char **)KMP_INTERNAL_CALLOC(
              __kmp_i18n_default_table.sect[section].size + 2,
              sizeof(char const *));
          table.sect[section].size =
              __kmp_i18n_default_table.sect[section].size;
        }
        if (table.sect[section].str[number] == NULL) {
          table.sect[section].str[number] = ___catgets(id);
        }
        message = table.sect[section].str[number];
      }
      if (message == NULL) {
        // Catalog is not opened or message is not found, return default
        // message.
        message = __kmp_i18n_default_table.sect[section].str[number];
      }
    }
  }
  if (message == NULL) {
    message = no_message_available;
  }
  return message;

} // func __kmp_i18n_catgets

#endif // KMP_OS_WINDOWS

// -----------------------------------------------------------------------------

#ifndef KMP_I18N_OK
#error I18n support is not implemented for this OS.
#endif // KMP_I18N_OK

// -----------------------------------------------------------------------------

void __kmp_i18n_dump_catalog(kmp_str_buf_t *buffer) {

  struct kmp_i18n_id_range_t {
    kmp_i18n_id_t first;
    kmp_i18n_id_t last;
  }; // struct kmp_i18n_id_range_t

  static struct kmp_i18n_id_range_t ranges[] = {
      {kmp_i18n_prp_first, kmp_i18n_prp_last},
      {kmp_i18n_str_first, kmp_i18n_str_last},
      {kmp_i18n_fmt_first, kmp_i18n_fmt_last},
      {kmp_i18n_msg_first, kmp_i18n_msg_last},
      {kmp_i18n_hnt_first, kmp_i18n_hnt_last}}; // ranges

  int num_of_ranges = sizeof(ranges) / sizeof(struct kmp_i18n_id_range_t);
  int range;
  kmp_i18n_id_t id;

  for (range = 0; range < num_of_ranges; ++range) {
    __kmp_str_buf_print(buffer, "*** Set #%d ***\n", range + 1);
    for (id = (kmp_i18n_id_t)(ranges[range].first + 1); id < ranges[range].last;
         id = (kmp_i18n_id_t)(id + 1)) {
      __kmp_str_buf_print(buffer, "%d: <<%s>>\n", id, __kmp_i18n_catgets(id));
    }
  }

  __kmp_printf("%s", buffer->str);

} // __kmp_i18n_dump_catalog

// -----------------------------------------------------------------------------
kmp_msg_t __kmp_msg_format(unsigned id_arg, ...) {

  kmp_msg_t msg;
  va_list args;
  kmp_str_buf_t buffer;
  __kmp_str_buf_init(&buffer);

  va_start(args, id_arg);

  // We use unsigned for the ID argument and explicitly cast it here to the
  // right enumerator because variadic functions are not compatible with
  // default promotions.
  kmp_i18n_id_t id = (kmp_i18n_id_t)id_arg;

#if KMP_OS_UNIX
  // On Linux* OS and OS X*, printf() family functions process parameter
  // numbers, for example:  "%2$s %1$s".
  __kmp_str_buf_vprint(&buffer, __kmp_i18n_catgets(id), args);
#elif KMP_OS_WINDOWS
  // On Winodws, printf() family functions does not recognize GNU style
  // parameter numbers, so we have to use FormatMessage() instead. It recognizes
  // parameter numbers, e. g.:  "%2!s! "%1!s!".
  {
    LPTSTR str = NULL;
    int len;
    FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                  __kmp_i18n_catgets(id), 0, 0, (LPTSTR)(&str), 0, &args);
    len = ___strip_crs(str);
    __kmp_str_buf_cat(&buffer, str, len);
    LocalFree(str);
  }
#else
#error
#endif
  va_end(args);
  __kmp_str_buf_detach(&buffer);

  msg.type = (kmp_msg_type_t)(id >> 16);
  msg.num = id & 0xFFFF;
  msg.str = buffer.str;
  msg.len = buffer.used;

  return msg;

} // __kmp_msg_format

// -----------------------------------------------------------------------------
static char *sys_error(int err) {

  char *message = NULL;

#if KMP_OS_WINDOWS

  LPVOID buffer = NULL;
  int len;
  DWORD rc;
  rc = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language.
      (LPTSTR)&buffer, 0, NULL);
  if (rc > 0) {
    // Message formatted. Copy it (so we can free it later with normal free().
    message = __kmp_str_format("%s", (char *)buffer);
    len = ___strip_crs(message); // Delete carriage returns if any.
    // Strip trailing newlines.
    while (len > 0 && message[len - 1] == '\n') {
      --len;
    }
    message[len] = 0;
  } else {
    // FormatMessage() failed to format system error message. GetLastError()
    // would give us error code, which we would convert to message... this it
    // dangerous recursion, which cannot clarify original error, so we will not
    // even start it.
  }
  if (buffer != NULL) {
    LocalFree(buffer);
  }

#else // Non-Windows* OS: Linux* OS or OS X*

/* There are 2 incompatible versions of strerror_r:

   char * strerror_r( int, char *, size_t );  // GNU version
   int    strerror_r( int, char *, size_t );  // XSI version
*/

#if (defined(__GLIBC__) && defined(_GNU_SOURCE)) ||                            \
    (defined(__BIONIC__) && defined(_GNU_SOURCE) &&                            \
     __ANDROID_API__ >= __ANDROID_API_M__)
  // GNU version of strerror_r.

  char buffer[2048];
  char *const err_msg = strerror_r(err, buffer, sizeof(buffer));
  // Do not eliminate this assignment to temporary variable, otherwise compiler
  // would not issue warning if strerror_r() returns `int' instead of expected
  // `char *'.
  message = __kmp_str_format("%s", err_msg);

#else // OS X*, FreeBSD* etc.
  // XSI version of strerror_r.
  int size = 2048;
  char *buffer = (char *)KMP_INTERNAL_MALLOC(size);
  int rc;
  if (buffer == NULL) {
    KMP_FATAL(MemoryAllocFailed);
  }
  rc = strerror_r(err, buffer, size);
  if (rc == -1) {
    rc = errno; // XSI version sets errno.
  }
  while (rc == ERANGE) { // ERANGE means the buffer is too small.
    KMP_INTERNAL_FREE(buffer);
    size *= 2;
    buffer = (char *)KMP_INTERNAL_MALLOC(size);
    if (buffer == NULL) {
      KMP_FATAL(MemoryAllocFailed);
    }
    rc = strerror_r(err, buffer, size);
    if (rc == -1) {
      rc = errno; // XSI version sets errno.
    }
  }
  if (rc == 0) {
    message = buffer;
  } else { // Buffer is unused. Free it.
    KMP_INTERNAL_FREE(buffer);
  }

#endif

#endif /* KMP_OS_WINDOWS */

  if (message == NULL) {
    // TODO: I18n this message.
    message = __kmp_str_format("%s", "(No system error message available)");
  }
  return message;
} // sys_error

// -----------------------------------------------------------------------------
kmp_msg_t __kmp_msg_error_code(int code) {

  kmp_msg_t msg;
  msg.type = kmp_mt_syserr;
  msg.num = code;
  msg.str = sys_error(code);
  msg.len = KMP_STRLEN(msg.str);
  return msg;

} // __kmp_msg_error_code

// -----------------------------------------------------------------------------
kmp_msg_t __kmp_msg_error_mesg(char const *mesg) {

  kmp_msg_t msg;
  msg.type = kmp_mt_syserr;
  msg.num = 0;
  msg.str = __kmp_str_format("%s", mesg);
  msg.len = KMP_STRLEN(msg.str);
  return msg;

} // __kmp_msg_error_mesg

// -----------------------------------------------------------------------------
void __kmp_msg(kmp_msg_severity_t severity, kmp_msg_t message, va_list args) {
  kmp_i18n_id_t format; // format identifier
  kmp_msg_t fmsg; // formatted message
  kmp_str_buf_t buffer;

  if (severity != kmp_ms_fatal && __kmp_generate_warnings == kmp_warnings_off)
    return; // no reason to form a string in order to not print it

  __kmp_str_buf_init(&buffer);

  // Format the primary message.
  switch (severity) {
  case kmp_ms_inform: {
    format = kmp_i18n_fmt_Info;
  } break;
  case kmp_ms_warning: {
    format = kmp_i18n_fmt_Warning;
  } break;
  case kmp_ms_fatal: {
    format = kmp_i18n_fmt_Fatal;
  } break;
  default: { KMP_DEBUG_ASSERT(0); }
  }
  fmsg = __kmp_msg_format(format, message.num, message.str);
  __kmp_str_free(&message.str);
  __kmp_str_buf_cat(&buffer, fmsg.str, fmsg.len);
  __kmp_str_free(&fmsg.str);

  // Format other messages.
  for (;;) {
    message = va_arg(args, kmp_msg_t);
    if (message.type == kmp_mt_dummy && message.str == NULL) {
      break;
    }
    switch (message.type) {
    case kmp_mt_hint: {
      format = kmp_i18n_fmt_Hint;
      // we cannot skip %1$ and only use %2$ to print the message without the
      // number
      fmsg = __kmp_msg_format(format, message.str);
    } break;
    case kmp_mt_syserr: {
      format = kmp_i18n_fmt_SysErr;
      fmsg = __kmp_msg_format(format, message.num, message.str);
    } break;
    default: { KMP_DEBUG_ASSERT(0); }
    }
    __kmp_str_free(&message.str);
    __kmp_str_buf_cat(&buffer, fmsg.str, fmsg.len);
    __kmp_str_free(&fmsg.str);
  }

  // Print formatted messages.
  // This lock prevents multiple fatal errors on the same problem.
  // __kmp_acquire_bootstrap_lock( & lock );    // GEH - This lock causing tests
  // to hang on OS X*.
  __kmp_printf("%s", buffer.str);
  __kmp_str_buf_free(&buffer);

  // __kmp_release_bootstrap_lock( & lock );  // GEH - this lock causing tests
  // to hang on OS X*.

} // __kmp_msg

void __kmp_msg(kmp_msg_severity_t severity, kmp_msg_t message, ...) {
  va_list args;
  va_start(args, message);
  __kmp_msg(severity, message, args);
  va_end(args);
}

void __kmp_fatal(kmp_msg_t message, ...) {
  va_list args;
  va_start(args, message);
  __kmp_msg(kmp_ms_fatal, message, args);
  va_end(args);
#if KMP_OS_WINDOWS
  // Delay to give message a chance to appear before reaping
  __kmp_thread_sleep(500);
#endif
  __kmp_abort_process();
} // __kmp_fatal

// end of file //
