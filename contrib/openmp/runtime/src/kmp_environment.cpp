/*
 * kmp_environment.cpp -- Handle environment variables OS-independently.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

/* We use GetEnvironmentVariable for Windows* OS instead of getenv because the
   act of loading a DLL on Windows* OS makes any user-set environment variables
   (i.e. with putenv()) unavailable.  getenv() apparently gets a clean copy of
   the env variables as they existed at the start of the run. JH 12/23/2002

   On Windows* OS, there are two environments (at least, see below):

   1. Environment maintained by Windows* OS on IA-32 architecture. Accessible
      through GetEnvironmentVariable(), SetEnvironmentVariable(), and
      GetEnvironmentStrings().

   2. Environment maintained by C RTL. Accessible through getenv(), putenv().

   putenv() function updates both C and Windows* OS on IA-32 architecture.
   getenv() function search for variables in C RTL environment only.
   Windows* OS on IA-32 architecture functions work *only* with Windows* OS on
   IA-32 architecture.

   Windows* OS on IA-32 architecture maintained by OS, so there is always only
   one Windows* OS on IA-32 architecture per process. Changes in Windows* OS on
   IA-32 architecture are process-visible.

   C environment maintained by C RTL. Multiple copies of C RTL may be present
   in the process, and each C RTL maintains its own environment. :-(

   Thus, proper way to work with environment on Windows* OS is:

   1. Set variables with putenv() function -- both C and Windows* OS on IA-32
      architecture are being updated. Windows* OS on IA-32 architecture may be
      considered primary target, while updating C RTL environment is free bonus.

   2. Get variables with GetEnvironmentVariable() -- getenv() does not
      search Windows* OS on IA-32 architecture, and can not see variables
      set with SetEnvironmentVariable().

   2007-04-05 -- lev
*/

#include "kmp_environment.h"

#include "kmp.h" //
#include "kmp_i18n.h"
#include "kmp_os.h" // KMP_OS_*.
#include "kmp_str.h" // __kmp_str_*().

#if KMP_OS_UNIX
#include <stdlib.h> // getenv, setenv, unsetenv.
#include <string.h> // strlen, strcpy.
#if KMP_OS_DARWIN
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif
#elif KMP_OS_WINDOWS
#include <windows.h> // GetEnvironmentVariable, SetEnvironmentVariable,
// GetLastError.
#else
#error Unknown or unsupported OS.
#endif

// TODO: Eliminate direct memory allocations, use string operations instead.

static inline void *allocate(size_t size) {
  void *ptr = KMP_INTERNAL_MALLOC(size);
  if (ptr == NULL) {
    KMP_FATAL(MemoryAllocFailed);
  }
  return ptr;
} // allocate

char *__kmp_env_get(char const *name) {

  char *result = NULL;

#if KMP_OS_UNIX
  char const *value = getenv(name);
  if (value != NULL) {
    size_t len = KMP_STRLEN(value) + 1;
    result = (char *)KMP_INTERNAL_MALLOC(len);
    if (result == NULL) {
      KMP_FATAL(MemoryAllocFailed);
    }
    KMP_STRNCPY_S(result, len, value, len);
  }
#elif KMP_OS_WINDOWS
  /* We use GetEnvironmentVariable for Windows* OS instead of getenv because the
     act of loading a DLL on Windows* OS makes any user-set environment
     variables (i.e. with putenv()) unavailable. getenv() apparently gets a
     clean copy of the env variables as they existed at the start of the run.
     JH 12/23/2002 */
  DWORD rc;
  rc = GetEnvironmentVariable(name, NULL, 0);
  if (!rc) {
    DWORD error = GetLastError();
    if (error != ERROR_ENVVAR_NOT_FOUND) {
      __kmp_fatal(KMP_MSG(CantGetEnvVar, name), KMP_ERR(error), __kmp_msg_null);
    }
    // Variable is not found, it's ok, just continue.
  } else {
    DWORD len = rc;
    result = (char *)KMP_INTERNAL_MALLOC(len);
    if (result == NULL) {
      KMP_FATAL(MemoryAllocFailed);
    }
    rc = GetEnvironmentVariable(name, result, len);
    if (!rc) {
      // GetEnvironmentVariable() may return 0 if variable is empty.
      // In such a case GetLastError() returns ERROR_SUCCESS.
      DWORD error = GetLastError();
      if (error != ERROR_SUCCESS) {
        // Unexpected error. The variable should be in the environment,
        // and buffer should be large enough.
        __kmp_fatal(KMP_MSG(CantGetEnvVar, name), KMP_ERR(error),
                    __kmp_msg_null);
        KMP_INTERNAL_FREE((void *)result);
        result = NULL;
      }
    }
  }
#else
#error Unknown or unsupported OS.
#endif

  return result;

} // func __kmp_env_get

// TODO: Find and replace all regular free() with __kmp_env_free().

void __kmp_env_free(char const **value) {

  KMP_DEBUG_ASSERT(value != NULL);
  KMP_INTERNAL_FREE(CCAST(char *, *value));
  *value = NULL;

} // func __kmp_env_free

int __kmp_env_exists(char const *name) {

#if KMP_OS_UNIX
  char const *value = getenv(name);
  return ((value == NULL) ? (0) : (1));
#elif KMP_OS_WINDOWS
  DWORD rc;
  rc = GetEnvironmentVariable(name, NULL, 0);
  if (rc == 0) {
    DWORD error = GetLastError();
    if (error != ERROR_ENVVAR_NOT_FOUND) {
      __kmp_fatal(KMP_MSG(CantGetEnvVar, name), KMP_ERR(error), __kmp_msg_null);
    }
    return 0;
  }
  return 1;
#else
#error Unknown or unsupported OS.
#endif

} // func __kmp_env_exists

void __kmp_env_set(char const *name, char const *value, int overwrite) {

#if KMP_OS_UNIX
  int rc = setenv(name, value, overwrite);
  if (rc != 0) {
    // Dead code. I tried to put too many variables into Linux* OS
    // environment on IA-32 architecture. When application consumes
    // more than ~2.5 GB of memory, entire system feels bad. Sometimes
    // application is killed (by OS?), sometimes system stops
    // responding... But this error message never appears. --ln
    __kmp_fatal(KMP_MSG(CantSetEnvVar, name), KMP_HNT(NotEnoughMemory),
                __kmp_msg_null);
  }
#elif KMP_OS_WINDOWS
  BOOL rc;
  if (!overwrite) {
    rc = GetEnvironmentVariable(name, NULL, 0);
    if (rc) {
      // Variable exists, do not overwrite.
      return;
    }
    DWORD error = GetLastError();
    if (error != ERROR_ENVVAR_NOT_FOUND) {
      __kmp_fatal(KMP_MSG(CantGetEnvVar, name), KMP_ERR(error), __kmp_msg_null);
    }
  }
  rc = SetEnvironmentVariable(name, value);
  if (!rc) {
    DWORD error = GetLastError();
    __kmp_fatal(KMP_MSG(CantSetEnvVar, name), KMP_ERR(error), __kmp_msg_null);
  }
#else
#error Unknown or unsupported OS.
#endif

} // func __kmp_env_set

void __kmp_env_unset(char const *name) {

#if KMP_OS_UNIX
  unsetenv(name);
#elif KMP_OS_WINDOWS
  BOOL rc = SetEnvironmentVariable(name, NULL);
  if (!rc) {
    DWORD error = GetLastError();
    __kmp_fatal(KMP_MSG(CantSetEnvVar, name), KMP_ERR(error), __kmp_msg_null);
  }
#else
#error Unknown or unsupported OS.
#endif

} // func __kmp_env_unset

/* Intel OpenMP RTL string representation of environment: just a string of
   characters, variables are separated with vertical bars, e. g.:

        "KMP_WARNINGS=0|KMP_AFFINITY=compact|"

    Empty variables are allowed and ignored:

        "||KMP_WARNINGS=1||"
*/

static void
___kmp_env_blk_parse_string(kmp_env_blk_t *block, // M: Env block to fill.
                            char const *env // I: String to parse.
                            ) {

  char const chr_delimiter = '|';
  char const str_delimiter[] = {chr_delimiter, 0};

  char *bulk = NULL;
  kmp_env_var_t *vars = NULL;
  int count = 0; // Number of used elements in vars array.
  int delimiters = 0; // Number of delimiters in input string.

  // Copy original string, we will modify the copy.
  bulk = __kmp_str_format("%s", env);

  // Loop thru all the vars in environment block. Count delimiters (maximum
  // number of variables is number of delimiters plus one).
  {
    char const *ptr = bulk;
    for (;;) {
      ptr = strchr(ptr, chr_delimiter);
      if (ptr == NULL) {
        break;
      }
      ++delimiters;
      ptr += 1;
    }
  }

  // Allocate vars array.
  vars = (kmp_env_var_t *)allocate((delimiters + 1) * sizeof(kmp_env_var_t));

  // Loop thru all the variables.
  {
    char *var; // Pointer to variable (both name and value).
    char *name; // Pointer to name of variable.
    char *value; // Pointer to value.
    char *buf; // Buffer for __kmp_str_token() function.
    var = __kmp_str_token(bulk, str_delimiter, &buf); // Get the first var.
    while (var != NULL) {
      // Save found variable in vars array.
      __kmp_str_split(var, '=', &name, &value);
      KMP_DEBUG_ASSERT(count < delimiters + 1);
      vars[count].name = name;
      vars[count].value = value;
      ++count;
      // Get the next var.
      var = __kmp_str_token(NULL, str_delimiter, &buf);
    }
  }

  // Fill out result.
  block->bulk = bulk;
  block->vars = vars;
  block->count = count;
}

/* Windows* OS (actually, DOS) environment block is a piece of memory with
   environment variables. Each variable is terminated with zero byte, entire
   block is terminated with one extra zero byte, so we have two zero bytes at
   the end of environment block, e. g.:

        "HOME=C:\\users\\lev\x00OS=Windows_NT\x00\x00"

    It is not clear how empty environment is represented. "\x00\x00"?
*/

#if KMP_OS_WINDOWS
static void ___kmp_env_blk_parse_windows(
    kmp_env_blk_t *block, // M: Env block to fill.
    char const *env // I: Pointer to Windows* OS (DOS) environment block.
    ) {

  char *bulk = NULL;
  kmp_env_var_t *vars = NULL;
  int count = 0; // Number of used elements in vars array.
  int size = 0; // Size of bulk.

  char *name; // Pointer to name of variable.
  char *value; // Pointer to value.

  if (env != NULL) {

    // Loop thru all the vars in environment block. Count variables, find size
    // of block.
    {
      char const *var; // Pointer to beginning of var.
      int len; // Length of variable.
      count = 0;
      var =
          env; // The first variable starts and beginning of environment block.
      len = KMP_STRLEN(var);
      while (len != 0) {
        ++count;
        size = size + len + 1;
        var = var + len +
              1; // Move pointer to the beginning of the next variable.
        len = KMP_STRLEN(var);
      }
      size =
          size + 1; // Total size of env block, including terminating zero byte.
    }

    // Copy original block to bulk, we will modify bulk, not original block.
    bulk = (char *)allocate(size);
    KMP_MEMCPY_S(bulk, size, env, size);
    // Allocate vars array.
    vars = (kmp_env_var_t *)allocate(count * sizeof(kmp_env_var_t));

    // Loop thru all the vars, now in bulk.
    {
      char *var; // Pointer to beginning of var.
      int len; // Length of variable.
      count = 0;
      var = bulk;
      len = KMP_STRLEN(var);
      while (len != 0) {
        // Save variable in vars array.
        __kmp_str_split(var, '=', &name, &value);
        vars[count].name = name;
        vars[count].value = value;
        ++count;
        // Get the next var.
        var = var + len + 1;
        len = KMP_STRLEN(var);
      }
    }
  }

  // Fill out result.
  block->bulk = bulk;
  block->vars = vars;
  block->count = count;
}
#endif

/* Unix environment block is a array of pointers to variables, last pointer in
   array is NULL:

        { "HOME=/home/lev", "TERM=xterm", NULL }
*/

static void
___kmp_env_blk_parse_unix(kmp_env_blk_t *block, // M: Env block to fill.
                          char **env // I: Unix environment to parse.
                          ) {

  char *bulk = NULL;
  kmp_env_var_t *vars = NULL;
  int count = 0;
  int size = 0; // Size of bulk.

  // Count number of variables and length of required bulk.
  {
    count = 0;
    size = 0;
    while (env[count] != NULL) {
      size += KMP_STRLEN(env[count]) + 1;
      ++count;
    }
  }

  // Allocate memory.
  bulk = (char *)allocate(size);
  vars = (kmp_env_var_t *)allocate(count * sizeof(kmp_env_var_t));

  // Loop thru all the vars.
  {
    char *var; // Pointer to beginning of var.
    char *name; // Pointer to name of variable.
    char *value; // Pointer to value.
    int len; // Length of variable.
    int i;
    var = bulk;
    for (i = 0; i < count; ++i) {
      // Copy variable to bulk.
      len = KMP_STRLEN(env[i]);
      KMP_MEMCPY_S(var, size, env[i], len + 1);
      // Save found variable in vars array.
      __kmp_str_split(var, '=', &name, &value);
      vars[i].name = name;
      vars[i].value = value;
      // Move pointer.
      var += len + 1;
    }
  }

  // Fill out result.
  block->bulk = bulk;
  block->vars = vars;
  block->count = count;
}

void __kmp_env_blk_init(kmp_env_blk_t *block, // M: Block to initialize.
                        char const *bulk // I: Initialization string, or NULL.
                        ) {

  if (bulk != NULL) {
    ___kmp_env_blk_parse_string(block, bulk);
  } else {
#if KMP_OS_UNIX
    ___kmp_env_blk_parse_unix(block, environ);
#elif KMP_OS_WINDOWS
    {
      char *mem = GetEnvironmentStrings();
      if (mem == NULL) {
        DWORD error = GetLastError();
        __kmp_fatal(KMP_MSG(CantGetEnvironment), KMP_ERR(error),
                    __kmp_msg_null);
      }
      ___kmp_env_blk_parse_windows(block, mem);
      FreeEnvironmentStrings(mem);
    }
#else
#error Unknown or unsupported OS.
#endif
  }

} // __kmp_env_blk_init

static int ___kmp_env_var_cmp( // Comparison function for qsort().
    kmp_env_var_t const *lhs, kmp_env_var_t const *rhs) {
  return strcmp(lhs->name, rhs->name);
}

void __kmp_env_blk_sort(
    kmp_env_blk_t *block // M: Block of environment variables to sort.
    ) {

  qsort(CCAST(kmp_env_var_t *, block->vars), block->count,
        sizeof(kmp_env_var_t),
        (int (*)(void const *, void const *)) & ___kmp_env_var_cmp);

} // __kmp_env_block_sort

void __kmp_env_blk_free(
    kmp_env_blk_t *block // M: Block of environment variables to free.
    ) {

  KMP_INTERNAL_FREE(CCAST(kmp_env_var_t *, block->vars));
  __kmp_str_free(&(block->bulk));

  block->count = 0;
  block->vars = NULL;

} // __kmp_env_blk_free

char const * // R: Value of variable or NULL if variable does not exist.
    __kmp_env_blk_var(
        kmp_env_blk_t *block, // I: Block of environment variables.
        char const *name // I: Name of variable to find.
        ) {

  int i;
  for (i = 0; i < block->count; ++i) {
    if (strcmp(block->vars[i].name, name) == 0) {
      return block->vars[i].value;
    }
  }
  return NULL;

} // __kmp_env_block_var

// end of file //
