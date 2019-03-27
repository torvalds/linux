/*
 * cmdline.c :  Helpers for command-line programs.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include <stdlib.h>             /* for atexit() */
#include <stdio.h>              /* for setvbuf() */
#include <locale.h>             /* for setlocale() */

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <crtdbg.h>
#include <io.h>
#include <conio.h>
#endif

#include <apr.h>                /* for STDIN_FILENO */
#include <apr_errno.h>          /* for apr_strerror */
#include <apr_general.h>        /* for apr_initialize/apr_terminate */
#include <apr_strings.h>        /* for apr_snprintf */
#include <apr_pools.h>
#include <apr_signal.h>

#include "svn_cmdline.h"
#include "svn_ctype.h"
#include "svn_dso.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_nls.h"
#include "svn_utf.h"
#include "svn_auth.h"
#include "svn_xml.h"
#include "svn_base64.h"
#include "svn_config.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_subst.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_utf_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"

#include "svn_private_config.h"

#include "win32_crashrpt.h"

#if defined(WIN32) && defined(_MSC_VER) && (_MSC_VER < 1400)
/* Before Visual Studio 2005, the C runtime didn't handle encodings for the
   for the stdio output handling. */
#define CMDLINE_USE_CUSTOM_ENCODING

/* The stdin encoding. If null, it's the same as the native encoding. */
static const char *input_encoding = NULL;

/* The stdout encoding. If null, it's the same as the native encoding. */
static const char *output_encoding = NULL;
#elif defined(WIN32) && defined(_MSC_VER)
/* For now limit this code to Visual C++, as the result is highly dependent
   on the CRT implementation */
#define USE_WIN32_CONSOLE_SHORTCUT

/* When TRUE, stdout/stderr is directly connected to a console */
static svn_boolean_t shortcut_stdout_to_console = FALSE;
static svn_boolean_t shortcut_stderr_to_console = FALSE;
#endif


int
svn_cmdline_init(const char *progname, FILE *error_stream)
{
  apr_status_t status;
  apr_pool_t *pool;
  svn_error_t *err;
  char prefix_buf[64];  /* 64 is probably bigger than most program names */

#ifndef WIN32
  {
    struct stat st;

    /* The following makes sure that file descriptors 0 (stdin), 1
       (stdout) and 2 (stderr) will not be "reused", because if
       e.g. file descriptor 2 would be reused when opening a file, a
       write to stderr would write to that file and most likely
       corrupt it. */
    if ((fstat(0, &st) == -1 && open("/dev/null", O_RDONLY) == -1) ||
        (fstat(1, &st) == -1 && open("/dev/null", O_WRONLY) == -1) ||
        (fstat(2, &st) == -1 && open("/dev/null", O_WRONLY) == -1))
      {
        if (error_stream)
          fprintf(error_stream, "%s: error: cannot open '/dev/null'\n",
                  progname);
        return EXIT_FAILURE;
      }
  }
#endif

  /* Ignore any errors encountered while attempting to change stream
     buffering, as the streams should retain their default buffering
     modes. */
  if (error_stream)
    setvbuf(error_stream, NULL, _IONBF, 0);
#ifndef WIN32
  setvbuf(stdout, NULL, _IOLBF, 0);
#endif

#ifdef WIN32
#ifdef CMDLINE_USE_CUSTOM_ENCODING
  /* Initialize the input and output encodings. */
  {
    static char input_encoding_buffer[16];
    static char output_encoding_buffer[16];

    apr_snprintf(input_encoding_buffer, sizeof input_encoding_buffer,
                 "CP%u", (unsigned) GetConsoleCP());
    input_encoding = input_encoding_buffer;

    apr_snprintf(output_encoding_buffer, sizeof output_encoding_buffer,
                 "CP%u", (unsigned) GetConsoleOutputCP());
    output_encoding = output_encoding_buffer;
  }
#endif /* CMDLINE_USE_CUSTOM_ENCODING */

#ifdef SVN_USE_WIN32_CRASHHANDLER
  if (!getenv("SVN_CMDLINE_DISABLE_CRASH_HANDLER"))
    {
      /* Attach (but don't load) the crash handler */
      SetUnhandledExceptionFilter(svn__unhandled_exception_filter);

#if _MSC_VER >= 1400
      /* ### This should work for VC++ 2002 (=1300) and later */
      /* Show the abort message on STDERR instead of a dialog to allow
         scripts (e.g. our testsuite) to continue after an abort without
         user intervention. Allow overriding for easier debugging. */
      if (!getenv("SVN_CMDLINE_USE_DIALOG_FOR_ABORT"))
        {
          /* In release mode: Redirect abort() errors to stderr */
          _set_error_mode(_OUT_TO_STDERR);

          /* In _DEBUG mode: Redirect all debug output (E.g. assert() to stderr.
             (Ignored in release builds) */
          _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR);
          _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR);
          _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR);
          _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
          _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
          _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        }
#endif /* _MSC_VER >= 1400 */
    }
#endif /* SVN_USE_WIN32_CRASHHANDLER */

#endif /* WIN32 */

  /* C programs default to the "C" locale. But because svn is supposed
     to be i18n-aware, it should inherit the default locale of its
     environment.  */
  if (!setlocale(LC_ALL, "")
      && !setlocale(LC_CTYPE, ""))
    {
      if (error_stream)
        {
          const char *env_vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
          const char **env_var = &env_vars[0], *env_val = NULL;
          while (*env_var)
            {
              env_val = getenv(*env_var);
              if (env_val && env_val[0])
                break;
              ++env_var;
            }

          if (!*env_var)
            {
              /* Unlikely. Can setlocale fail if no env vars are set? */
              --env_var;
              env_val = "not set";
            }

          fprintf(error_stream,
                  "%s: warning: cannot set LC_CTYPE locale\n"
                  "%s: warning: environment variable %s is %s\n"
                  "%s: warning: please check that your locale name is correct\n",
                  progname, progname, *env_var, env_val, progname);
        }
    }

  /* Initialize the APR subsystem, and register an atexit() function
     to Uninitialize that subsystem at program exit. */
  status = apr_initialize();
  if (status)
    {
      if (error_stream)
        {
          char buf[1024];
          apr_strerror(status, buf, sizeof(buf) - 1);
          fprintf(error_stream,
                  "%s: error: cannot initialize APR: %s\n",
                  progname, buf);
        }
      return EXIT_FAILURE;
    }

  strncpy(prefix_buf, progname, sizeof(prefix_buf) - 3);
  prefix_buf[sizeof(prefix_buf) - 3] = '\0';
  strcat(prefix_buf, ": ");

  /* DSO pool must be created before any other pools used by the
     application so that pool cleanup doesn't unload DSOs too
     early. See docstring of svn_dso_initialize2(). */
  if ((err = svn_dso_initialize2()))
    {
      if (error_stream)
        svn_handle_error2(err, error_stream, TRUE, prefix_buf);

      svn_error_clear(err);
      return EXIT_FAILURE;
    }

  if (0 > atexit(apr_terminate))
    {
      if (error_stream)
        fprintf(error_stream,
                "%s: error: atexit registration failed\n",
                progname);
      return EXIT_FAILURE;
    }

  /* Create a pool for use by the UTF-8 routines.  It will be cleaned
     up by APR at exit time. */
  pool = svn_pool_create(NULL);
  svn_utf_initialize2(FALSE, pool);

  if ((err = svn_nls_init()))
    {
      if (error_stream)
        svn_handle_error2(err, error_stream, TRUE, prefix_buf);

      svn_error_clear(err);
      return EXIT_FAILURE;
    }

#ifdef USE_WIN32_CONSOLE_SHORTCUT
  if (_isatty(STDOUT_FILENO))
    {
      DWORD ignored;
      HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

       /* stdout is a char device handle, but is it the console? */
       if (GetConsoleMode(stdout_handle, &ignored))
        shortcut_stdout_to_console = TRUE;

       /* Don't close stdout_handle */
    }
  if (_isatty(STDERR_FILENO))
    {
      DWORD ignored;
      HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

       /* stderr is a char device handle, but is it the console? */
      if (GetConsoleMode(stderr_handle, &ignored))
          shortcut_stderr_to_console = TRUE;

      /* Don't close stderr_handle */
    }
#endif

  return EXIT_SUCCESS;
}


svn_error_t *
svn_cmdline_cstring_from_utf8(const char **dest,
                              const char *src,
                              apr_pool_t *pool)
{
#ifdef CMDLINE_USE_CUSTOM_ENCODING
  if (output_encoding != NULL)
    return svn_utf_cstring_from_utf8_ex2(dest, src, output_encoding, pool);
#endif

  return svn_utf_cstring_from_utf8(dest, src, pool);
}


const char *
svn_cmdline_cstring_from_utf8_fuzzy(const char *src,
                                    apr_pool_t *pool)
{
  return svn_utf__cstring_from_utf8_fuzzy(src, pool,
                                          svn_cmdline_cstring_from_utf8);
}


svn_error_t *
svn_cmdline_cstring_to_utf8(const char **dest,
                            const char *src,
                            apr_pool_t *pool)
{
#ifdef CMDLINE_USE_CUSTOM_ENCODING
  if (input_encoding != NULL)
    return svn_utf_cstring_to_utf8_ex2(dest, src, input_encoding, pool);
#endif

  return svn_utf_cstring_to_utf8(dest, src, pool);
}


svn_error_t *
svn_cmdline_path_local_style_from_utf8(const char **dest,
                                       const char *src,
                                       apr_pool_t *pool)
{
  return svn_cmdline_cstring_from_utf8(dest,
                                       svn_dirent_local_style(src, pool),
                                       pool);
}

svn_error_t *
svn_cmdline__stdin_readline(const char **result,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buf = NULL;
  svn_stream_t *stdin_stream = NULL;
  svn_boolean_t oob = FALSE;

  SVN_ERR(svn_stream_for_stdin2(&stdin_stream, TRUE, scratch_pool));
  SVN_ERR(svn_stream_readline(stdin_stream, &buf, APR_EOL_STR, &oob, result_pool));

  *result = buf->data;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline_printf(apr_pool_t *pool, const char *fmt, ...)
{
  const char *message;
  va_list ap;

  /* A note about encoding issues:
   * APR uses the execution character set, but here we give it UTF-8 strings,
   * both the fmt argument and any other string arguments.  Since apr_pvsprintf
   * only cares about and produces ASCII characters, this works under the
   * assumption that all supported platforms use an execution character set
   * with ASCII as a subset.
   */

  va_start(ap, fmt);
  message = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_cmdline_fputs(message, stdout, pool);
}

svn_error_t *
svn_cmdline_fprintf(FILE *stream, apr_pool_t *pool, const char *fmt, ...)
{
  const char *message;
  va_list ap;

  /* See svn_cmdline_printf () for a note about character encoding issues. */

  va_start(ap, fmt);
  message = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_cmdline_fputs(message, stream, pool);
}

svn_error_t *
svn_cmdline_fputs(const char *string, FILE* stream, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *out;

#ifdef USE_WIN32_CONSOLE_SHORTCUT
  /* For legacy reasons the Visual C++ runtime converts output to the console
     from the native 'ansi' encoding, to unicode, then back to 'ansi' and then
     onwards to the console which is implemented as unicode.

     For operations like 'svn status -v' this may cause about 70% of the total
     processing time, with absolutely no gain.

     For this specific scenario this shortcut exists. It has the nice side
     effect of allowing full unicode output to the console.

     Note that this shortcut is not used when the output is redirected, as in
     that case the data is put on the pipe/file after the first conversion to
     ansi. In this case the most expensive conversion is already avoided.
   */
  if ((stream == stdout && shortcut_stdout_to_console)
      || (stream == stderr && shortcut_stderr_to_console))
    {
      WCHAR *result;

      if (string[0] == '\0')
        return SVN_NO_ERROR;

      SVN_ERR(svn_cmdline_fflush(stream)); /* Flush existing output */

      SVN_ERR(svn_utf__win32_utf8_to_utf16(&result, string, NULL, pool));

      if (_cputws(result))
        {
          if (apr_get_os_error())
          {
            return svn_error_wrap_apr(apr_get_os_error(), _("Write error"));
          }
        }

      return SVN_NO_ERROR;
    }
#endif

  err = svn_cmdline_cstring_from_utf8(&out, string, pool);

  if (err)
    {
      svn_error_clear(err);
      out = svn_cmdline_cstring_from_utf8_fuzzy(string, pool);
    }

  /* On POSIX systems, errno will be set on an error in fputs, but this might
     not be the case on other platforms.  We reset errno and only
     use it if it was set by the below fputs call.  Else, we just return
     a generic error. */
  errno = 0;

  if (fputs(out, stream) == EOF)
    {
      if (apr_get_os_error()) /* is errno on POSIX */
        {
          /* ### Issue #3014: Return a specific error for broken pipes,
           * ### with a single element in the error chain. */
          if (SVN__APR_STATUS_IS_EPIPE(apr_get_os_error()))
            return svn_error_create(SVN_ERR_IO_PIPE_WRITE_ERROR, NULL, NULL);
          else
            return svn_error_wrap_apr(apr_get_os_error(), _("Write error"));
        }
      else
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline_fflush(FILE *stream)
{
  /* See comment in svn_cmdline_fputs about use of errno and stdio. */
  errno = 0;
  if (fflush(stream) == EOF)
    {
      if (apr_get_os_error()) /* is errno on POSIX */
        {
          /* ### Issue #3014: Return a specific error for broken pipes,
           * ### with a single element in the error chain. */
          if (SVN__APR_STATUS_IS_EPIPE(apr_get_os_error()))
            return svn_error_create(SVN_ERR_IO_PIPE_WRITE_ERROR, NULL, NULL);
          else
            return svn_error_wrap_apr(apr_get_os_error(), _("Write error"));
        }
      else
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

const char *svn_cmdline_output_encoding(apr_pool_t *pool)
{
#ifdef CMDLINE_USE_CUSTOM_ENCODING
  if (output_encoding)
    return apr_pstrdup(pool, output_encoding);
#endif

  return SVN_APR_LOCALE_CHARSET;
}

int
svn_cmdline_handle_exit_error(svn_error_t *err,
                              apr_pool_t *pool,
                              const char *prefix)
{
  /* Issue #3014:
   * Don't print anything on broken pipes. The pipe was likely
   * closed by the process at the other end. We expect that
   * process to perform error reporting as necessary.
   *
   * ### This assumes that there is only one error in a chain for
   * ### SVN_ERR_IO_PIPE_WRITE_ERROR. See svn_cmdline_fputs(). */
  if (err->apr_err != SVN_ERR_IO_PIPE_WRITE_ERROR)
    svn_handle_error2(err, stderr, FALSE, prefix);
  svn_error_clear(err);
  if (pool)
    svn_pool_destroy(pool);
  return EXIT_FAILURE;
}

struct trust_server_cert_non_interactive_baton {
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
};

/* This implements 'svn_auth_ssl_server_trust_prompt_func_t'.

   Don't actually prompt.  Instead, set *CRED_P to valid credentials
   iff FAILURES is empty or may be accepted according to the flags
   in BATON. If there are any other failure bits, then set *CRED_P
   to null (that is, reject the cert).

   Ignore MAY_SAVE; we don't save certs we never prompted for.

   Ignore REALM and CERT_INFO,

   Ignore any further films by George Lucas. */
static svn_error_t *
trust_server_cert_non_interactive(svn_auth_cred_ssl_server_trust_t **cred_p,
                                  void *baton,
                                  const char *realm,
                                  apr_uint32_t failures,
                                  const svn_auth_ssl_server_cert_info_t
                                    *cert_info,
                                  svn_boolean_t may_save,
                                  apr_pool_t *pool)
{
  struct trust_server_cert_non_interactive_baton *b = baton;
  apr_uint32_t non_ignored_failures;
  *cred_p = NULL;

  /* Mask away bits we are instructed to ignore. */
  non_ignored_failures = failures & ~(
        (b->trust_server_cert_unknown_ca ? SVN_AUTH_SSL_UNKNOWNCA : 0)
      | (b->trust_server_cert_cn_mismatch ? SVN_AUTH_SSL_CNMISMATCH : 0)
      | (b->trust_server_cert_expired ? SVN_AUTH_SSL_EXPIRED : 0)
      | (b->trust_server_cert_not_yet_valid ? SVN_AUTH_SSL_NOTYETVALID : 0)
      | (b->trust_server_cert_other_failure ? SVN_AUTH_SSL_OTHER : 0)
  );

  /* If no failures remain, accept the certificate. */
  if (non_ignored_failures == 0)
    {
      *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
      (*cred_p)->may_save = FALSE;
      (*cred_p)->accepted_failures = failures;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline_create_auth_baton2(svn_auth_baton_t **ab,
                               svn_boolean_t non_interactive,
                               const char *auth_username,
                               const char *auth_password,
                               const char *config_dir,
                               svn_boolean_t no_auth_cache,
                               svn_boolean_t trust_server_cert_unknown_ca,
                               svn_boolean_t trust_server_cert_cn_mismatch,
                               svn_boolean_t trust_server_cert_expired,
                               svn_boolean_t trust_server_cert_not_yet_valid,
                               svn_boolean_t trust_server_cert_other_failure,
                               svn_config_t *cfg,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *pool)

{
  svn_boolean_t store_password_val = TRUE;
  svn_boolean_t store_auth_creds_val = TRUE;
  svn_auth_provider_object_t *provider;
  svn_cmdline_prompt_baton2_t *pb = NULL;

  /* The whole list of registered providers */
  apr_array_header_t *providers;

  /* Populate the registered providers with the platform-specific providers */
  SVN_ERR(svn_auth_get_platform_specific_client_providers(&providers,
                                                          cfg, pool));

  /* If we have a cancellation function, cram it and the stuff it
     needs into the prompt baton. */
  if (cancel_func)
    {
      pb = apr_palloc(pool, sizeof(*pb));
      pb->cancel_func = cancel_func;
      pb->cancel_baton = cancel_baton;
      pb->config_dir = config_dir;
    }

  if (!non_interactive)
    {
      /* This provider doesn't prompt the user in order to get creds;
         it prompts the user regarding the caching of creds. */
      svn_auth_get_simple_provider2(&provider,
                                    svn_cmdline_auth_plaintext_prompt,
                                    pb, pool);
    }
  else
    {
      svn_auth_get_simple_provider2(&provider, NULL, NULL, pool);
    }

  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
  svn_auth_get_username_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
  svn_auth_get_ssl_client_cert_file_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  if (!non_interactive)
    {
      /* This provider doesn't prompt the user in order to get creds;
         it prompts the user regarding the caching of creds. */
      svn_auth_get_ssl_client_cert_pw_file_provider2
        (&provider, svn_cmdline_auth_plaintext_passphrase_prompt,
         pb, pool);
    }
  else
    {
      svn_auth_get_ssl_client_cert_pw_file_provider2(&provider, NULL, NULL,
                                                     pool);
    }
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  if (!non_interactive)
    {
      svn_boolean_t ssl_client_cert_file_prompt;

      SVN_ERR(svn_config_get_bool(cfg, &ssl_client_cert_file_prompt,
                                  SVN_CONFIG_SECTION_AUTH,
                                  SVN_CONFIG_OPTION_SSL_CLIENT_CERT_FILE_PROMPT,
                                  FALSE));

      /* Two basic prompt providers: username/password, and just username. */
      svn_auth_get_simple_prompt_provider(&provider,
                                          svn_cmdline_auth_simple_prompt,
                                          pb,
                                          2, /* retry limit */
                                          pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      svn_auth_get_username_prompt_provider
        (&provider, svn_cmdline_auth_username_prompt, pb,
         2, /* retry limit */ pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      /* SSL prompt providers: server-certs and client-cert-passphrases.  */
      svn_auth_get_ssl_server_trust_prompt_provider
        (&provider, svn_cmdline_auth_ssl_server_trust_prompt, pb, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      svn_auth_get_ssl_client_cert_pw_prompt_provider
        (&provider, svn_cmdline_auth_ssl_client_cert_pw_prompt, pb, 2, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      /* If configuration allows, add a provider for client-cert path
         prompting, too. */
      if (ssl_client_cert_file_prompt)
        {
          svn_auth_get_ssl_client_cert_prompt_provider
            (&provider, svn_cmdline_auth_ssl_client_cert_prompt, pb, 2, pool);
          APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
        }
    }
  else if (trust_server_cert_unknown_ca || trust_server_cert_cn_mismatch ||
           trust_server_cert_expired || trust_server_cert_not_yet_valid ||
           trust_server_cert_other_failure)
    {
      struct trust_server_cert_non_interactive_baton *b;

      b = apr_palloc(pool, sizeof(*b));
      b->trust_server_cert_unknown_ca = trust_server_cert_unknown_ca;
      b->trust_server_cert_cn_mismatch = trust_server_cert_cn_mismatch;
      b->trust_server_cert_expired = trust_server_cert_expired;
      b->trust_server_cert_not_yet_valid = trust_server_cert_not_yet_valid;
      b->trust_server_cert_other_failure = trust_server_cert_other_failure;

      /* Remember, only register this provider if non_interactive. */
      svn_auth_get_ssl_server_trust_prompt_provider
        (&provider, trust_server_cert_non_interactive, b, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    }

  /* Build an authentication baton to give to libsvn_client. */
  svn_auth_open(ab, providers, pool);

  /* Place any default --username or --password credentials into the
     auth_baton's run-time parameter hash. */
  if (auth_username)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_DEFAULT_USERNAME,
                           auth_username);
  if (auth_password)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                           auth_password);

  /* Same with the --non-interactive option. */
  if (non_interactive)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_NON_INTERACTIVE, "");

  if (config_dir)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_CONFIG_DIR,
                           config_dir);

  /* Determine whether storing passwords in any form is allowed.
   * This is the deprecated location for this option, the new
   * location is SVN_CONFIG_CATEGORY_SERVERS. The RA layer may
   * override the value we set here. */
  SVN_ERR(svn_config_get_bool(cfg, &store_password_val,
                              SVN_CONFIG_SECTION_AUTH,
                              SVN_CONFIG_OPTION_STORE_PASSWORDS,
                              SVN_CONFIG_DEFAULT_OPTION_STORE_PASSWORDS));

  if (! store_password_val)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_DONT_STORE_PASSWORDS, "");

  /* Determine whether we are allowed to write to the auth/ area.
   * This is the deprecated location for this option, the new
   * location is SVN_CONFIG_CATEGORY_SERVERS. The RA layer may
   * override the value we set here. */
  SVN_ERR(svn_config_get_bool(cfg, &store_auth_creds_val,
                              SVN_CONFIG_SECTION_AUTH,
                              SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
                              SVN_CONFIG_DEFAULT_OPTION_STORE_AUTH_CREDS));

  if (no_auth_cache || ! store_auth_creds_val)
    svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_NO_AUTH_CACHE, "");

#ifdef SVN_HAVE_GNOME_KEYRING
  svn_auth_set_parameter(*ab, SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC,
                         &svn_cmdline__auth_gnome_keyring_unlock_prompt);
#endif /* SVN_HAVE_GNOME_KEYRING */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline__getopt_init(apr_getopt_t **os,
                         int argc,
                         const char *argv[],
                         apr_pool_t *pool)
{
  apr_status_t apr_err = apr_getopt_init(os, pool, argc, argv);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Error initializing command line arguments"));
  return SVN_NO_ERROR;
}


void
svn_cmdline__print_xml_prop(svn_stringbuf_t **outstr,
                            const char* propname,
                            svn_string_t *propval,
                            svn_boolean_t inherited_prop,
                            apr_pool_t *pool)
{
  const char *xml_safe;
  const char *encoding = NULL;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create_empty(pool);

  if (svn_xml_is_xml_safe(propval->data, propval->len))
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_xml_escape_cdata_string(&xml_esc, propval, pool);
      xml_safe = xml_esc->data;
    }
  else
    {
      const svn_string_t *base64ed = svn_base64_encode_string2(propval, TRUE,
                                                               pool);
      encoding = "base64";
      xml_safe = base64ed->data;
    }

  if (encoding)
    svn_xml_make_open_tag(
      outstr, pool, svn_xml_protect_pcdata,
      inherited_prop ? "inherited_property" : "property",
      "name", propname,
      "encoding", encoding, SVN_VA_NULL);
  else
    svn_xml_make_open_tag(
      outstr, pool, svn_xml_protect_pcdata,
      inherited_prop ? "inherited_property" : "property",
      "name", propname, SVN_VA_NULL);

  svn_stringbuf_appendcstr(*outstr, xml_safe);

  svn_xml_make_close_tag(
    outstr, pool,
    inherited_prop ? "inherited_property" : "property");

  return;
}

/* Return the most similar string to NEEDLE in HAYSTACK, which contains
 * HAYSTACK_LEN elements.  Return NULL if no string is sufficiently similar.
 */
/* See svn_cl__similarity_check() for a more general solution. */
static const char *
most_similar(const char *needle_cstr,
             const char **haystack,
             apr_size_t haystack_len,
             apr_pool_t *scratch_pool)
{
  const char *max_similar = NULL;
  apr_size_t max_score = 0;
  apr_size_t i;
  svn_membuf_t membuf;
  svn_string_t *needle_str = svn_string_create(needle_cstr, scratch_pool);

  svn_membuf__create(&membuf, 64, scratch_pool);

  for (i = 0; i < haystack_len; i++)
    {
      apr_size_t score;
      svn_string_t *hay = svn_string_create(haystack[i], scratch_pool);

      score = svn_string__similarity(needle_str, hay, &membuf, NULL);

      /* If you update this factor, consider updating
       * svn_cl__similarity_check(). */
      if (score >= (2 * SVN_STRING__SIM_RANGE_MAX + 1) / 3
          && score > max_score)
        {
          max_score = score;
          max_similar = haystack[i];
        }
    }

  return max_similar;
}

/* Verify that NEEDLE is in HAYSTACK, which contains HAYSTACK_LEN elements. */
static svn_error_t *
string_in_array(const char *needle,
                const char **haystack,
                apr_size_t haystack_len,
                apr_pool_t *scratch_pool)
{
  const char *next_of_kin;
  apr_size_t i;
  for (i = 0; i < haystack_len; i++)
    {
      if (!strcmp(needle, haystack[i]))
        return SVN_NO_ERROR;
    }

  /* Error. */
  next_of_kin = most_similar(needle, haystack, haystack_len, scratch_pool);
  if (next_of_kin)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Ignoring unknown value '%s'; "
                               "did you mean '%s'?"),
                             needle, next_of_kin);
  else
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Ignoring unknown value '%s'"),
                             needle);
}

#include "config_keys.inc"

/* Validate the FILE, SECTION, and OPTION components of CONFIG_OPTION are
 * known.  Return an error if not.  (An unknown value may be either a typo
 * or added in a newer minor version of Subversion.) */
static svn_error_t *
validate_config_option(svn_cmdline__config_argument_t *config_option,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t arbitrary_keys = FALSE;

  /* TODO: some day, we could also verify that OPTION is valid for SECTION;
     i.e., forbid invalid combinations such as config:auth:diff-extensions. */

#define ARRAYLEN(x) ( sizeof((x)) / sizeof((x)[0]) )

  SVN_ERR(string_in_array(config_option->file, svn__valid_config_files,
                          ARRAYLEN(svn__valid_config_files),
                          scratch_pool));
  SVN_ERR(string_in_array(config_option->section, svn__valid_config_sections,
                          ARRAYLEN(svn__valid_config_sections),
                          scratch_pool));

  /* Don't validate option names for sections such as servers[group],
   * config[tunnels], and config[auto-props] that permit arbitrary options. */
    {
      int i;

      for (i = 0; i < ARRAYLEN(svn__empty_config_sections); i++)
        {
        if (!strcmp(config_option->section, svn__empty_config_sections[i]))
          arbitrary_keys = TRUE;
        }
    }

  if (! arbitrary_keys)
    SVN_ERR(string_in_array(config_option->option, svn__valid_config_options,
                            ARRAYLEN(svn__valid_config_options),
                            scratch_pool));

#undef ARRAYLEN

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline__parse_config_option(apr_array_header_t *config_options,
                                 const char *opt_arg,
                                 const char *prefix,
                                 apr_pool_t *pool)
{
  svn_cmdline__config_argument_t *config_option;
  const char *first_colon, *second_colon, *equals_sign;
  apr_size_t len = strlen(opt_arg);
  if ((first_colon = strchr(opt_arg, ':')) && (first_colon != opt_arg))
    {
      if ((second_colon = strchr(first_colon + 1, ':')) &&
          (second_colon != first_colon + 1))
        {
          if ((equals_sign = strchr(second_colon + 1, '=')) &&
              (equals_sign != second_colon + 1))
            {
              svn_error_t *warning;

              config_option = apr_pcalloc(pool, sizeof(*config_option));
              config_option->file = apr_pstrndup(pool, opt_arg,
                                                 first_colon - opt_arg);
              config_option->section = apr_pstrndup(pool, first_colon + 1,
                                                    second_colon - first_colon - 1);
              config_option->option = apr_pstrndup(pool, second_colon + 1,
                                                   equals_sign - second_colon - 1);

              warning = validate_config_option(config_option, pool);
              if (warning)
                {
                  svn_handle_warning2(stderr, warning, prefix);
                  svn_error_clear(warning);
                }

              if (! (strchr(config_option->option, ':')))
                {
                  config_option->value = apr_pstrndup(pool, equals_sign + 1,
                                                      opt_arg + len - equals_sign - 1);
                  APR_ARRAY_PUSH(config_options, svn_cmdline__config_argument_t *)
                                       = config_option;
                  return SVN_NO_ERROR;
                }
            }
        }
    }
  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("Invalid syntax of argument of --config-option"));
}

svn_error_t *
svn_cmdline__apply_config_options(apr_hash_t *config,
                                  const apr_array_header_t *config_options,
                                  const char *prefix,
                                  const char *argument_name)
{
  int i;

  for (i = 0; i < config_options->nelts; i++)
   {
     svn_config_t *cfg;
     svn_cmdline__config_argument_t *arg =
                          APR_ARRAY_IDX(config_options, i,
                                        svn_cmdline__config_argument_t *);

     cfg = svn_hash_gets(config, arg->file);

     if (cfg)
       {
         svn_config_set(cfg, arg->section, arg->option, arg->value);
       }
     else
       {
         svn_error_t *err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
             _("Unrecognized file in argument of %s"), argument_name);

         svn_handle_warning2(stderr, err, prefix);
         svn_error_clear(err);
       }
    }

  return SVN_NO_ERROR;
}

/* Return a copy, allocated in POOL, of the next line of text from *STR
 * up to and including a CR and/or an LF. Change *STR to point to the
 * remainder of the string after the returned part. If there are no
 * characters to be returned, return NULL; never return an empty string.
 */
static const char *
next_line(const char **str, apr_pool_t *pool)
{
  const char *start = *str;
  const char *p = *str;

  /* n.b. Throughout this fn, we never read any character after a '\0'. */
  /* Skip over all non-EOL characters, if any. */
  while (*p != '\r' && *p != '\n' && *p != '\0')
    p++;
  /* Skip over \r\n or \n\r or \r or \n, if any. */
  if (*p == '\r' || *p == '\n')
    {
      char c = *p++;

      if ((c == '\r' && *p == '\n') || (c == '\n' && *p == '\r'))
        p++;
    }

  /* Now p points after at most one '\n' and/or '\r'. */
  *str = p;

  if (p == start)
    return NULL;

  return svn_string_ncreate(start, p - start, pool)->data;
}

const char *
svn_cmdline__indent_string(const char *str,
                           const char *indent,
                           apr_pool_t *pool)
{
  svn_stringbuf_t *out = svn_stringbuf_create_empty(pool);
  const char *line;

  while ((line = next_line(&str, pool)))
    {
      svn_stringbuf_appendcstr(out, indent);
      svn_stringbuf_appendcstr(out, line);
    }
  return out->data;
}

svn_error_t *
svn_cmdline__print_prop_hash(svn_stream_t *out,
                             apr_hash_t *prop_hash,
                             svn_boolean_t names_only,
                             apr_pool_t *pool)
{
  apr_array_header_t *sorted_props;
  int i;

  sorted_props = svn_sort__hash(prop_hash, svn_sort_compare_items_lexically,
                                pool);
  for (i = 0; i < sorted_props->nelts; i++)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(sorted_props, i, svn_sort__item_t);
      const char *pname = item.key;
      svn_string_t *propval = item.value;
      const char *pname_stdout;

      if (svn_prop_needs_translation(pname))
        SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                             TRUE, pool));

      SVN_ERR(svn_cmdline_cstring_from_utf8(&pname_stdout, pname, pool));

      if (out)
        {
          pname_stdout = apr_psprintf(pool, "  %s\n", pname_stdout);
          SVN_ERR(svn_subst_translate_cstring2(pname_stdout, &pname_stdout,
                                              APR_EOL_STR,  /* 'native' eol */
                                              FALSE, /* no repair */
                                              NULL,  /* no keywords */
                                              FALSE, /* no expansion */
                                              pool));

          SVN_ERR(svn_stream_puts(out, pname_stdout));
        }
      else
        {
          /* ### We leave these printfs for now, since if propval wasn't
             translated above, we don't know anything about its encoding.
             In fact, it might be binary data... */
          printf("  %s\n", pname_stdout);
        }

      if (!names_only)
        {
          /* Add an extra newline to the value before indenting, so that
           * every line of output has the indentation whether the value
           * already ended in a newline or not. */
          const char *newval = apr_psprintf(pool, "%s\n", propval->data);
          const char *indented_newval = svn_cmdline__indent_string(newval,
                                                                   "    ",
                                                                   pool);
          if (out)
            {
              SVN_ERR(svn_stream_puts(out, indented_newval));
            }
          else
            {
              printf("%s", indented_newval);
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline__print_xml_prop_hash(svn_stringbuf_t **outstr,
                                 apr_hash_t *prop_hash,
                                 svn_boolean_t names_only,
                                 svn_boolean_t inherited_props,
                                 apr_pool_t *pool)
{
  apr_array_header_t *sorted_props;
  int i;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create_empty(pool);

  sorted_props = svn_sort__hash(prop_hash, svn_sort_compare_items_lexically,
                                pool);
  for (i = 0; i < sorted_props->nelts; i++)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(sorted_props, i, svn_sort__item_t);
      const char *pname = item.key;
      svn_string_t *propval = item.value;

      if (names_only)
        {
          svn_xml_make_open_tag(
            outstr, pool, svn_xml_self_closing,
            inherited_props ? "inherited_property" : "property",
            "name", pname, SVN_VA_NULL);
        }
      else
        {
          const char *pname_out;

          if (svn_prop_needs_translation(pname))
            SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                                 TRUE, pool));

          SVN_ERR(svn_cmdline_cstring_from_utf8(&pname_out, pname, pool));

          svn_cmdline__print_xml_prop(outstr, pname_out, propval,
                                      inherited_props, pool);
        }
    }

    return SVN_NO_ERROR;
}

svn_boolean_t
svn_cmdline__stdin_is_a_terminal(void)
{
#ifdef WIN32
  return (_isatty(STDIN_FILENO) != 0);
#else
  return (isatty(STDIN_FILENO) != 0);
#endif
}

svn_boolean_t
svn_cmdline__stdout_is_a_terminal(void)
{
#ifdef WIN32
  return (_isatty(STDOUT_FILENO) != 0);
#else
  return (isatty(STDOUT_FILENO) != 0);
#endif
}

svn_boolean_t
svn_cmdline__stderr_is_a_terminal(void)
{
#ifdef WIN32
  return (_isatty(STDERR_FILENO) != 0);
#else
  return (isatty(STDERR_FILENO) != 0);
#endif
}

svn_boolean_t
svn_cmdline__be_interactive(svn_boolean_t non_interactive,
                            svn_boolean_t force_interactive)
{
  /* If neither --non-interactive nor --force-interactive was passed,
   * be interactive if stdin is a terminal.
   * If --force-interactive was passed, always be interactive. */
  if (!force_interactive && !non_interactive)
    {
      return svn_cmdline__stdin_is_a_terminal();
    }
  else if (force_interactive)
    return TRUE;

  return !non_interactive;
}


/* Helper for the next two functions.  Set *EDITOR to some path to an
   editor binary.  Sources to search include: the EDITOR_CMD argument
   (if not NULL), $SVN_EDITOR, the runtime CONFIG variable (if CONFIG
   is not NULL), $VISUAL, $EDITOR.  Return
   SVN_ERR_CL_NO_EXTERNAL_EDITOR if no binary can be found. */
static svn_error_t *
find_editor_binary(const char **editor,
                   const char *editor_cmd,
                   apr_hash_t *config)
{
  const char *e;
  struct svn_config_t *cfg;

  /* Use the editor specified on the command line via --editor-cmd, if any. */
  e = editor_cmd;

  /* Otherwise look for the Subversion-specific environment variable. */
  if (! e)
    e = getenv("SVN_EDITOR");

  /* If not found then fall back on the config file. */
  if (! e)
    {
      cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
      svn_config_get(cfg, &e, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_EDITOR_CMD, NULL);
    }

  /* If not found yet then try general purpose environment variables. */
  if (! e)
    e = getenv("VISUAL");
  if (! e)
    e = getenv("EDITOR");

#ifdef SVN_CLIENT_EDITOR
  /* If still not found then fall back on the hard-coded default. */
  if (! e)
    e = SVN_CLIENT_EDITOR;
#endif

  /* Error if there is no editor specified */
  if (e)
    {
      const char *c;

      for (c = e; *c; c++)
        if (!svn_ctype_isspace(*c))
          break;

      if (! *c)
        return svn_error_create
          (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
           _("The EDITOR, SVN_EDITOR or VISUAL environment variable or "
             "'editor-cmd' run-time configuration option is empty or "
             "consists solely of whitespace. Expected a shell command."));
    }
  else
    return svn_error_create
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
       _("None of the environment variables SVN_EDITOR, VISUAL or EDITOR are "
         "set, and no 'editor-cmd' run-time configuration option was found"));

  *editor = e;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cmdline__edit_file_externally(const char *path,
                                  const char *editor_cmd,
                                  apr_hash_t *config,
                                  apr_pool_t *pool)
{
  const char *editor, *cmd, *base_dir, *file_name, *base_dir_apr;
  char *old_cwd;
  int sys_err;
  apr_status_t apr_err;

  svn_dirent_split(&base_dir, &file_name, path, pool);

  SVN_ERR(find_editor_binary(&editor, editor_cmd, config));

  apr_err = apr_filepath_get(&old_cwd, APR_FILEPATH_NATIVE, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get working directory"));

  /* APR doesn't like "" directories */
  if (base_dir[0] == '\0')
    base_dir_apr = ".";
  else
    SVN_ERR(svn_path_cstring_from_utf8(&base_dir_apr, base_dir, pool));

  apr_err = apr_filepath_set(base_dir_apr, pool);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't change working directory to '%s'"), base_dir);

  cmd = apr_psprintf(pool, "%s %s", editor, file_name);
  sys_err = system(cmd);

  apr_err = apr_filepath_set(old_cwd, pool);
  if (apr_err)
    svn_handle_error2(svn_error_wrap_apr
                      (apr_err, _("Can't restore working directory")),
                      stderr, TRUE /* fatal */, "svn: ");

  if (sys_err)
    /* Extracting any meaning from sys_err is platform specific, so just
       use the raw value. */
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             _("system('%s') returned %d"), cmd, sys_err);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cmdline__edit_string_externally(svn_string_t **edited_contents /* UTF-8! */,
                                    const char **tmpfile_left /* UTF-8! */,
                                    const char *editor_cmd,
                                    const char *base_dir /* UTF-8! */,
                                    const svn_string_t *contents /* UTF-8! */,
                                    const char *filename,
                                    apr_hash_t *config,
                                    svn_boolean_t as_text,
                                    const char *encoding,
                                    apr_pool_t *pool)
{
  const char *editor;
  const char *cmd;
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  const char *tmpfile_native;
  const char *base_dir_apr;
  svn_string_t *translated_contents;
  apr_status_t apr_err;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR;
  char *old_cwd;
  int sys_err;
  svn_boolean_t remove_file = TRUE;

  SVN_ERR(find_editor_binary(&editor, editor_cmd, config));

  /* Convert file contents from UTF-8/LF if desired. */
  if (as_text)
    {
      const char *translated;
      SVN_ERR(svn_subst_translate_cstring2(contents->data, &translated,
                                           APR_EOL_STR, FALSE,
                                           NULL, FALSE, pool));
      translated_contents = svn_string_create_empty(pool);
      if (encoding)
        SVN_ERR(svn_utf_cstring_from_utf8_ex2(&translated_contents->data,
                                              translated, encoding, pool));
      else
        SVN_ERR(svn_utf_cstring_from_utf8(&translated_contents->data,
                                          translated, pool));
      translated_contents->len = strlen(translated_contents->data);
    }
  else
    translated_contents = svn_string_dup(contents, pool);

  /* Move to BASE_DIR to avoid getting characters that need quoting
     into tmpfile_name */
  apr_err = apr_filepath_get(&old_cwd, APR_FILEPATH_NATIVE, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get working directory"));

  /* APR doesn't like "" directories */
  if (base_dir[0] == '\0')
    base_dir_apr = ".";
  else
    SVN_ERR(svn_path_cstring_from_utf8(&base_dir_apr, base_dir, pool));
  apr_err = apr_filepath_set(base_dir_apr, pool);
  if (apr_err)
    {
      return svn_error_wrap_apr
        (apr_err, _("Can't change working directory to '%s'"), base_dir);
    }

  /*** From here on, any problems that occur require us to cd back!! ***/

  /* Ask the working copy for a temporary file named FILENAME-something. */
  err = svn_io_open_uniquely_named(&tmp_file, &tmpfile_name,
                                   "" /* dirpath */,
                                   filename,
                                   ".tmp",
                                   svn_io_file_del_none, pool, pool);

  if (err && (APR_STATUS_IS_EACCES(err->apr_err) || err->apr_err == EROFS))
    {
      const char *temp_dir_apr;

      svn_error_clear(err);

      SVN_ERR(svn_io_temp_dir(&base_dir, pool));

      SVN_ERR(svn_path_cstring_from_utf8(&temp_dir_apr, base_dir, pool));
      apr_err = apr_filepath_set(temp_dir_apr, pool);
      if (apr_err)
        {
          return svn_error_wrap_apr
            (apr_err, _("Can't change working directory to '%s'"), base_dir);
        }

      err = svn_io_open_uniquely_named(&tmp_file, &tmpfile_name,
                                       "" /* dirpath */,
                                       filename,
                                       ".tmp",
                                       svn_io_file_del_none, pool, pool);
    }

  if (err)
    goto cleanup2;

  /*** From here on, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  err = svn_io_file_write_full(tmp_file, translated_contents->data,
                               translated_contents->len, &written,
                               pool);

  err = svn_error_compose_create(err, svn_io_file_close(tmp_file, pool));

  /* Make sure the whole CONTENTS were written, else return an error. */
  if (err)
    goto cleanup;

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  err = svn_io_stat(&finfo_before, tmpfile_name, APR_FINFO_MTIME, pool);
  if (err)
    goto cleanup;

  /* Backdate the file a little bit in case the editor is very fast
     and doesn't change the size.  (Use two seconds, since some
     filesystems have coarse granularity.)  It's OK if this call
     fails, so we don't check its return value.*/
  err = svn_io_set_file_affected_time(finfo_before.mtime
                                              - apr_time_from_sec(2),
                                      tmpfile_name, pool);
  svn_error_clear(err);

  /* Stat it again to get the mtime we actually set. */
  err = svn_io_stat(&finfo_before, tmpfile_name,
                    APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (err)
    goto cleanup;

  /* Prepare the editor command line.  */
  err = svn_utf_cstring_from_utf8(&tmpfile_native, tmpfile_name, pool);
  if (err)
    goto cleanup;
  cmd = apr_psprintf(pool, "%s %s", editor, tmpfile_native);

  /* If the caller wants us to leave the file around, return the path
     of the file we'll use, and make a note not to destroy it.  */
  if (tmpfile_left)
    {
      *tmpfile_left = svn_dirent_join(base_dir, tmpfile_name, pool);
      remove_file = FALSE;
    }

  /* Now, run the editor command line.  */
  sys_err = system(cmd);
  if (sys_err != 0)
    {
      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      err =  svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               _("system('%s') returned %d"), cmd, sys_err);
      goto cleanup;
    }

  /* Get information about the temporary file after the assumed editing. */
  err = svn_io_stat(&finfo_after, tmpfile_name,
                    APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (err)
    goto cleanup;

  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *edited_contents_s;
      err = svn_stringbuf_from_file2(&edited_contents_s, tmpfile_name, pool);
      if (err)
        goto cleanup;

      *edited_contents = svn_stringbuf__morph_into_string(edited_contents_s);

      /* Translate back to UTF8/LF if desired. */
      if (as_text)
        {
          err = svn_subst_translate_string2(edited_contents, NULL, NULL,
                                            *edited_contents, encoding, FALSE,
                                            pool, pool);
          if (err)
            {
              err = svn_error_quick_wrap
                (err,
                 _("Error normalizing edited contents to internal format"));
              goto cleanup;
            }
        }
    }
  else
    {
      /* No edits seem to have been made */
      *edited_contents = NULL;
    }

 cleanup:
  if (remove_file)
    {
      /* Remove the file from disk.  */
      err = svn_error_compose_create(
              err,
              svn_io_remove_file2(tmpfile_name, FALSE, pool));
    }

 cleanup2:
  /* If we against all probability can't cd back, all further relative
     file references would be screwed up, so we have to abort. */
  apr_err = apr_filepath_set(old_cwd, pool);
  if (apr_err)
    {
      svn_handle_error2(svn_error_wrap_apr
                        (apr_err, _("Can't restore working directory")),
                        stderr, TRUE /* fatal */, "svn: ");
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_cmdline__parse_trust_options(
                        svn_boolean_t *trust_server_cert_unknown_ca,
                        svn_boolean_t *trust_server_cert_cn_mismatch,
                        svn_boolean_t *trust_server_cert_expired,
                        svn_boolean_t *trust_server_cert_not_yet_valid,
                        svn_boolean_t *trust_server_cert_other_failure,
                        const char *opt_arg,
                        apr_pool_t *scratch_pool)
{
  apr_array_header_t *failures;
  int i;

  *trust_server_cert_unknown_ca = FALSE;
  *trust_server_cert_cn_mismatch = FALSE;
  *trust_server_cert_expired = FALSE;
  *trust_server_cert_not_yet_valid = FALSE;
  *trust_server_cert_other_failure = FALSE;

  failures = svn_cstring_split(opt_arg, ", \n\r\t\v", TRUE, scratch_pool);

  for (i = 0; i < failures->nelts; i++)
    {
      const char *value = APR_ARRAY_IDX(failures, i, const char *);
      if (!strcmp(value, "unknown-ca"))
        *trust_server_cert_unknown_ca = TRUE;
      else if (!strcmp(value, "cn-mismatch"))
        *trust_server_cert_cn_mismatch = TRUE;
      else if (!strcmp(value, "expired"))
        *trust_server_cert_expired = TRUE;
      else if (!strcmp(value, "not-yet-valid"))
        *trust_server_cert_not_yet_valid = TRUE;
      else if (!strcmp(value, "other"))
        *trust_server_cert_other_failure = TRUE;
      else
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("Unknown value '%s' for %s.\n"
                                    "Supported values: %s"),
                                  value,
                                  "--trust-server-cert-failures",
                                  "unknown-ca, cn-mismatch, expired, "
                                  "not-yet-valid, other");
    }

  return SVN_NO_ERROR;
}

/* Flags to see if we've been cancelled by the client or not. */
static volatile sig_atomic_t cancelled = FALSE;
static volatile sig_atomic_t signum_cancelled = 0;

/* The signals we handle. */
static int signal_map [] = {
  SIGINT
#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  , SIGBREAK
#endif
#ifdef SIGHUP
  , SIGHUP
#endif
#ifdef SIGTERM
  , SIGTERM
#endif
};

/* A signal handler to support cancellation. */
static void
signal_handler(int signum)
{
  int i;

  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
  for (i = 0; i < sizeof(signal_map)/sizeof(signal_map[0]); ++i)
    if (signal_map[i] == signum)
      {
        signum_cancelled = i + 1;
        break;
      }
}

/* An svn_cancel_func_t callback. */
static svn_error_t *
check_cancel(void *baton)
{
  /* Cancel baton should be always NULL in command line client. */
  SVN_ERR_ASSERT(baton == NULL);
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}

svn_cancel_func_t
svn_cmdline__setup_cancellation_handler(void)
{
  int i;

  for (i = 0; i < sizeof(signal_map)/sizeof(signal_map[0]); ++i)
    apr_signal(signal_map[i], signal_handler);

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it, otherwise
   * working with large files when compiled against an APR that doesn't have
   * large file support will crash the program, which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

  return check_cancel;
}

void
svn_cmdline__disable_cancellation_handler(void)
{
  int i;

  for (i = 0; i < sizeof(signal_map)/sizeof(signal_map[0]); ++i)
    apr_signal(signal_map[i], SIG_DFL);
}

void
svn_cmdline__cancellation_exit(void)
{
  int signum = 0;

  if (cancelled && signum_cancelled)
    signum = signal_map[signum_cancelled - 1];
  if (signum)
    {
#ifndef WIN32
      apr_signal(signum, SIG_DFL);
      /* No APR support for getpid() so cannot use apr_proc_kill(). */
      kill(getpid(), signum);
#endif
    }
}
