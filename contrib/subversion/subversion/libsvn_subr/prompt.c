/*
 * prompt.c -- ask the user for authentication information.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_lib.h>
#include <apr_poll.h>
#include <apr_portable.h>

#include "svn_cmdline.h"
#include "svn_ctype.h"
#include "svn_string.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_path.h"

#include "private/svn_cmdline_private.h"
#include "svn_private_config.h"

#ifdef WIN32
#include <conio.h>
#elif defined(HAVE_TERMIOS_H)
#include <signal.h>
#include <termios.h>
#endif



/* Descriptor of an open terminal */
typedef struct terminal_handle_t terminal_handle_t;
struct terminal_handle_t
{
  apr_file_t *infd;              /* input file handle */
  apr_file_t *outfd;             /* output file handle */
  svn_boolean_t noecho;          /* terminal echo was turned off */
  svn_boolean_t close_handles;   /* close handles when closing the terminal */
  apr_pool_t *pool;              /* pool associated with the file handles */

#ifdef HAVE_TERMIOS_H
  svn_boolean_t restore_state;   /* terminal state was changed */
  apr_os_file_t osinfd;          /* OS-specific handle for infd */
  struct termios attr;           /* saved terminal attributes */
#endif
};

/* Initialize safe state of terminal_handle_t. */
static void
terminal_handle_init(terminal_handle_t *terminal,
                     apr_file_t *infd, apr_file_t *outfd,
                     svn_boolean_t noecho, svn_boolean_t close_handles,
                     apr_pool_t *pool)
{
  memset(terminal, 0, sizeof(*terminal));
  terminal->infd = infd;
  terminal->outfd = outfd;
  terminal->noecho = noecho;
  terminal->close_handles = close_handles;
  terminal->pool = pool;
}

/*
 * Common pool cleanup handler for terminal_handle_t. Closes TERMINAL.
 * If CLOSE_HANDLES is TRUE, close the terminal file handles.
 * If RESTORE_STATE is TRUE, restores the TERMIOS flags of the terminal.
 */
static apr_status_t
terminal_cleanup_handler(terminal_handle_t *terminal,
                         svn_boolean_t close_handles,
                         svn_boolean_t restore_state)
{
  apr_status_t status = APR_SUCCESS;

#ifdef HAVE_TERMIOS_H
  /* Restore terminal state flags. */
  if (restore_state && terminal->restore_state)
    tcsetattr(terminal->osinfd, TCSANOW, &terminal->attr);
#endif

  /* Close terminal handles. */
  if (close_handles && terminal->close_handles)
    {
      apr_file_t *const infd = terminal->infd;
      apr_file_t *const outfd = terminal->outfd;

      if (infd)
        {
          terminal->infd = NULL;
          status = apr_file_close(infd);
        }

      if (!status && outfd && outfd != infd)
        {
          terminal->outfd = NULL;
          status = apr_file_close(terminal->outfd);
        }
    }
  return status;
}

/* Normal pool cleanup for a terminal. */
static apr_status_t terminal_plain_cleanup(void *baton)
{
  return terminal_cleanup_handler(baton, FALSE, TRUE);
}

/* Child pool cleanup for a terminal -- does not restore echo state. */
static apr_status_t terminal_child_cleanup(void *baton)
{
  return terminal_cleanup_handler(baton, FALSE, FALSE);
}

/* Explicitly close the terminal, removing its cleanup handlers. */
static svn_error_t *
terminal_close(terminal_handle_t *terminal)
{
  apr_status_t status;

  /* apr_pool_cleanup_kill() removes both normal and child cleanup */
  apr_pool_cleanup_kill(terminal->pool, terminal, terminal_plain_cleanup);

  status = terminal_cleanup_handler(terminal, TRUE, TRUE);
  if (status)
    return svn_error_create(status, NULL, _("Can't close terminal"));
  return SVN_NO_ERROR;
}

/* Allocate and open *TERMINAL. If NOECHO is TRUE, try to turn off
   terminal echo.  Use POOL for all allocations.*/
static svn_error_t *
terminal_open(terminal_handle_t **terminal, svn_boolean_t noecho,
              apr_pool_t *pool)
{
  apr_status_t status;

#ifdef WIN32
  /* On Windows, we'll use the console API directly if the process has
     a console attached; otherwise we'll just use stdin and stderr. */
  const HANDLE conin = CreateFileW(L"CONIN$", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
  *terminal = apr_palloc(pool, sizeof(terminal_handle_t));
  if (conin != INVALID_HANDLE_VALUE)
    {
      /* The process has a console. */
      CloseHandle(conin);
      terminal_handle_init(*terminal, NULL, NULL, noecho, FALSE, NULL);
      return SVN_NO_ERROR;
    }
#else  /* !WIN32 */
  /* Without evidence to the contrary, we'll assume this is *nix and
     try to open /dev/tty. If that fails, we'll use stdin for input
     and stderr for prompting. */
  apr_file_t *tmpfd;
  status = apr_file_open(&tmpfd, "/dev/tty",
                         APR_FOPEN_READ | APR_FOPEN_WRITE,
                         APR_OS_DEFAULT, pool);
  *terminal = apr_palloc(pool, sizeof(terminal_handle_t));
  if (!status)
    {
      /* We have a terminal handle that we can use for input and output. */
      terminal_handle_init(*terminal, tmpfd, tmpfd, FALSE, TRUE, pool);
    }
#endif /* !WIN32 */
  else
    {
      /* There is no terminal. Sigh. */
      apr_file_t *infd;
      apr_file_t *outfd;

      status = apr_file_open_stdin(&infd, pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't open stdin"));
      status = apr_file_open_stderr(&outfd, pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't open stderr"));
      terminal_handle_init(*terminal, infd, outfd, FALSE, FALSE, pool);
    }

#ifdef HAVE_TERMIOS_H
  /* Set terminal state */
  if (0 == apr_os_file_get(&(*terminal)->osinfd, (*terminal)->infd))
    {
      if (0 == tcgetattr((*terminal)->osinfd, &(*terminal)->attr))
        {
          struct termios attr = (*terminal)->attr;
          /* Turn off signal handling and canonical input mode */
          attr.c_lflag &= ~(ISIG | ICANON);
          attr.c_cc[VMIN] = 1;          /* Read one byte at a time */
          attr.c_cc[VTIME] = 0;         /* No timeout, wait indefinitely */
          attr.c_lflag &= ~(ECHO);      /* Turn off echo */
          if (0 == tcsetattr((*terminal)->osinfd, TCSAFLUSH, &attr))
            {
              (*terminal)->noecho = noecho;
              (*terminal)->restore_state = TRUE;
            }
        }
    }
#endif /* HAVE_TERMIOS_H */

  /* Register pool cleanup to close handles and restore echo state. */
  apr_pool_cleanup_register((*terminal)->pool, *terminal,
                            terminal_plain_cleanup,
                            terminal_child_cleanup);
  return SVN_NO_ERROR;
}

/* Write a null-terminated STRING to TERMINAL.
   Use POOL for allocations related to converting STRING from UTF-8. */
static svn_error_t *
terminal_puts(const char *string, terminal_handle_t *terminal,
              apr_pool_t *pool)
{
  svn_error_t *err;
  const char *converted;

  err = svn_cmdline_cstring_from_utf8(&converted, string, pool);
  if (err)
    {
      svn_error_clear(err);
      converted = svn_cmdline_cstring_from_utf8_fuzzy(string, pool);
    }

#ifdef WIN32
  if (!terminal->outfd)
    {
      /* See terminal_open; we're using Console I/O. */
      _cputs(converted);
      return SVN_NO_ERROR;
    }
#endif

  SVN_ERR(svn_io_file_write_full(terminal->outfd, converted,
                                 strlen(converted), NULL, pool));

  return svn_error_trace(svn_io_file_flush(terminal->outfd, pool));
}

/* These codes can be returned from terminal_getc instead of a character. */
#define TERMINAL_NONE  0x80000               /* no character read, retry */
#define TERMINAL_DEL   (TERMINAL_NONE + 1)   /* the input was a deleteion */
#define TERMINAL_EOL   (TERMINAL_NONE + 2)   /* end of input/end of line */
#define TERMINAL_EOF   (TERMINAL_NONE + 3)   /* end of file during input */

/* Helper for terminal_getc: writes CH to OUTFD as a control char. */
#ifndef WIN32
static void
echo_control_char(char ch, apr_file_t *outfd)
{
  if (svn_ctype_iscntrl(ch))
    {
      const char substitute = (ch < 32? '@' + ch : '?');
      apr_file_putc('^', outfd);
      apr_file_putc(substitute, outfd);
    }
  else if (svn_ctype_isprint(ch))
    {
      /* Pass printable characters unchanged. */
      apr_file_putc(ch, outfd);
    }
  else
    {
      /* Everything else is strange. */
      apr_file_putc('^', outfd);
      apr_file_putc('!', outfd);
    }
}
#endif /* WIN32 */

/* Read one character or control code from TERMINAL, returning it in CODE.
   if CAN_ERASE and the input was a deletion, emit codes to erase the
   last character displayed on the terminal.
   Use POOL for all allocations. */
static svn_error_t *
terminal_getc(int *code, terminal_handle_t *terminal,
              svn_boolean_t can_erase, apr_pool_t *pool)
{
  const svn_boolean_t echo = !terminal->noecho;
  apr_status_t status = APR_SUCCESS;
  char ch;

#ifdef WIN32
  if (!terminal->infd)
    {
      /* See terminal_open; we're using Console I/O. */

      /*  The following was hoisted from APR's getpass for Windows. */
      int concode = _getch();
      switch (concode)
        {
        case '\r':                      /* end-of-line */
          *code = TERMINAL_EOL;
          if (echo)
            _cputs("\r\n");
          break;

        case EOF:                       /* end-of-file */
        case 26:                        /* Ctrl+Z */
          *code = TERMINAL_EOF;
          if (echo)
            _cputs((concode == EOF ? "[EOF]\r\n" : "^Z\r\n"));
          break;

        case 3:                         /* Ctrl+C, Ctrl+Break */
          /* _getch() bypasses Ctrl+C but not Ctrl+Break detection! */
          if (echo)
            _cputs("^C\r\n");
          return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);

        case 0:                         /* Function code prefix */
        case 0xE0:
          concode = (concode << 4) | _getch();
          /* Catch {DELETE}, {<--}, Num{DEL} and Num{<--} */
          if (concode == 0xE53 || concode == 0xE4B
              || concode == 0x053 || concode == 0x04B)
            {
              *code = TERMINAL_DEL;
              if (can_erase)
                _cputs("\b \b");
            }
          else
            {
              *code = TERMINAL_NONE;
              _putch('\a');
            }
          break;

        case '\b':                      /* BS */
        case 127:                       /* DEL */
          *code = TERMINAL_DEL;
          if (can_erase)
            _cputs("\b \b");
          break;

        default:
          if (!apr_iscntrl(concode))
            {
              *code = (int)(unsigned char)concode;
              _putch(echo ? concode : '*');
            }
          else
            {
              *code = TERMINAL_NONE;
              _putch('\a');
            }
        }
      return SVN_NO_ERROR;
    }
#elif defined(HAVE_TERMIOS_H)
  if (terminal->restore_state)
    {
      /* We're using a bytewise-immediate termios input */
      const struct termios *const attr = &terminal->attr;

      status = apr_file_getc(&ch, terminal->infd);
      if (status)
        return svn_error_wrap_apr(status, _("Can't read from terminal"));

      if (ch == attr->c_cc[VINTR] || ch == attr->c_cc[VQUIT])
        {
          /* Break */
          echo_control_char(ch, terminal->outfd);
          return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);
        }
      else if (ch == '\r' || ch == '\n' || ch == attr->c_cc[VEOL])
        {
          /* Newline */
          *code = TERMINAL_EOL;
          apr_file_putc('\n', terminal->outfd);
        }
      else if (ch == '\b' || ch == attr->c_cc[VERASE])
        {
          /* Delete */
          *code = TERMINAL_DEL;
          if (can_erase)
            {
              apr_file_putc('\b', terminal->outfd);
              apr_file_putc(' ', terminal->outfd);
              apr_file_putc('\b', terminal->outfd);
            }
        }
      else if (ch == attr->c_cc[VEOF])
        {
          /* End of input */
          *code = TERMINAL_EOF;
          echo_control_char(ch, terminal->outfd);
        }
      else if (ch == attr->c_cc[VSUSP])
        {
          /* Suspend */
          *code = TERMINAL_NONE;
          kill(0, SIGTSTP);
        }
      else if (!apr_iscntrl(ch))
        {
          /* Normal character */
          *code = (int)(unsigned char)ch;
          apr_file_putc((echo ? ch : '*'), terminal->outfd);
        }
      else
        {
          /* Ignored character */
          *code = TERMINAL_NONE;
          apr_file_putc('\a', terminal->outfd);
        }
      return SVN_NO_ERROR;
    }
#endif /* HAVE_TERMIOS_H */

  /* Fall back to plain stream-based I/O. */
#ifndef WIN32
  /* Wait for input on termin. This code is based on
     apr_wait_for_io_or_timeout().
     Note that this will return an EINTR on a signal. */
  {
    apr_pollfd_t pollset;
    int n;

    pollset.desc_type = APR_POLL_FILE;
    pollset.desc.f = terminal->infd;
    pollset.p = pool;
    pollset.reqevents = APR_POLLIN;

    status = apr_poll(&pollset, 1, &n, -1);

    if (n == 1 && pollset.rtnevents & APR_POLLIN)
      status = APR_SUCCESS;
  }
#endif /* !WIN32 */

  if (!status)
    status = apr_file_getc(&ch, terminal->infd);
  if (APR_STATUS_IS_EINTR(status))
    {
      *code = TERMINAL_NONE;
      return SVN_NO_ERROR;
    }
  else if (APR_STATUS_IS_EOF(status))
    {
      *code = TERMINAL_EOF;
      return SVN_NO_ERROR;
    }
  else if (status)
    return svn_error_wrap_apr(status, _("Can't read from terminal"));

  *code = (int)(unsigned char)ch;
  return SVN_NO_ERROR;
}


/* Set @a *result to the result of prompting the user with @a
 * prompt_msg.  Use @ *pb to get the cancel_func and cancel_baton.
 * Do not call the cancel_func if @a *pb is NULL.
 * Allocate @a *result in @a pool.
 *
 * If @a hide is true, then try to avoid displaying the user's input.
 */
static svn_error_t *
prompt(const char **result,
       const char *prompt_msg,
       svn_boolean_t hide,
       svn_cmdline_prompt_baton2_t *pb,
       apr_pool_t *pool)
{
  /* XXX: If this functions ever starts using members of *pb
   * which were not included in svn_cmdline_prompt_baton_t,
   * we need to update svn_cmdline_prompt_user2 and its callers. */

  svn_boolean_t saw_first_half_of_eol = FALSE;
  svn_stringbuf_t *strbuf = svn_stringbuf_create_empty(pool);
  terminal_handle_t *terminal;
  int code;
  char c;

  SVN_ERR(terminal_open(&terminal, hide, pool));
  SVN_ERR(terminal_puts(prompt_msg, terminal, pool));

  while (1)
    {
      SVN_ERR(terminal_getc(&code, terminal, (strbuf->len > 0), pool));

      /* Check for cancellation after a character has been read, some
         input processing modes may eat ^C and we'll only notice a
         cancellation signal after characters have been read --
         sometimes even after a newline. */
      if (pb)
        SVN_ERR(pb->cancel_func(pb->cancel_baton));

      switch (code)
        {
        case TERMINAL_NONE:
          /* Nothing useful happened; retry. */
          continue;

        case TERMINAL_DEL:
          /* Delete the last input character. terminal_getc takes care
             of erasing the feedback from the terminal, if applicable. */
          svn_stringbuf_chop(strbuf, 1);
          continue;

        case TERMINAL_EOL:
          /* End-of-line means end of input. Trick the EOL-detection code
             below to stop reading. */
          saw_first_half_of_eol = TRUE;
          c = APR_EOL_STR[1];   /* Could be \0 but still stops reading. */
          break;

        case TERMINAL_EOF:
          return svn_error_create(
              APR_EOF,
              terminal_close(terminal),
              _("End of file while reading from terminal"));

        default:
          /* Convert the returned code back to the character. */
          c = (char)code;
        }

      if (saw_first_half_of_eol)
        {
          if (c == APR_EOL_STR[1])
            break;
          else
            saw_first_half_of_eol = FALSE;
        }
      else if (c == APR_EOL_STR[0])
        {
          /* GCC might complain here: "warning: will never be executed"
           * That's fine. This is a compile-time check for "\r\n\0" */
          if (sizeof(APR_EOL_STR) == 3)
            {
              saw_first_half_of_eol = TRUE;
              continue;
            }
          else if (sizeof(APR_EOL_STR) == 2)
            break;
          else
            /* ### APR_EOL_STR holds more than two chars?  Who
               ever heard of such a thing? */
            SVN_ERR_MALFUNCTION();
        }

      svn_stringbuf_appendbyte(strbuf, c);
    }

  if (terminal->noecho)
    {
      /* If terminal echo was turned off, make sure future output
         to the terminal starts on a new line, as expected. */
      SVN_ERR(terminal_puts(APR_EOL_STR, terminal, pool));
    }
  SVN_ERR(terminal_close(terminal));

  return svn_cmdline_cstring_to_utf8(result, strbuf->data, pool);
}



/** Prompt functions for auth providers. **/

/* Helper function for auth provider prompters: mention the
 * authentication @a realm on stderr, in a manner appropriate for
 * preceding a prompt; or if @a realm is null, then do nothing.
 */
static svn_error_t *
maybe_print_realm(const char *realm, apr_pool_t *pool)
{
  if (realm)
    {
      terminal_handle_t *terminal;
      SVN_ERR(terminal_open(&terminal, FALSE, pool));
      SVN_ERR(terminal_puts(
                  apr_psprintf(pool,
                               _("Authentication realm: %s\n"), realm),
                  terminal, pool));
      SVN_ERR(terminal_close(terminal));
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_simple_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_simple_prompt(svn_auth_cred_simple_t **cred_p,
                               void *baton,
                               const char *realm,
                               const char *username,
                               svn_boolean_t may_save,
                               apr_pool_t *pool)
{
  svn_auth_cred_simple_t *ret = apr_pcalloc(pool, sizeof(*ret));
  const char *pass_prompt;
  svn_cmdline_prompt_baton2_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));

  if (username)
    ret->username = apr_pstrdup(pool, username);
  else
    SVN_ERR(prompt(&(ret->username), _("Username: "), FALSE, pb, pool));

  pass_prompt = apr_psprintf(pool, _("Password for '%s': "), ret->username);
  SVN_ERR(prompt(&(ret->password), pass_prompt, TRUE, pb, pool));
  ret->may_save = may_save;
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_username_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_username_prompt(svn_auth_cred_username_t **cred_p,
                                 void *baton,
                                 const char *realm,
                                 svn_boolean_t may_save,
                                 apr_pool_t *pool)
{
  svn_auth_cred_username_t *ret = apr_pcalloc(pool, sizeof(*ret));
  svn_cmdline_prompt_baton2_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));

  SVN_ERR(prompt(&(ret->username), _("Username: "), FALSE, pb, pool));
  ret->may_save = may_save;
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_server_trust_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_server_trust_prompt
  (svn_auth_cred_ssl_server_trust_t **cred_p,
   void *baton,
   const char *realm,
   apr_uint32_t failures,
   const svn_auth_ssl_server_cert_info_t *cert_info,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  const char *choice;
  svn_stringbuf_t *msg;
  svn_cmdline_prompt_baton2_t *pb = baton;
  svn_stringbuf_t *buf = svn_stringbuf_createf
    (pool, _("Error validating server certificate for '%s':\n"), realm);

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      svn_stringbuf_appendcstr
        (buf,
         _(" - The certificate is not issued by a trusted authority. Use the\n"
           "   fingerprint to validate the certificate manually!\n"));
    }

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate hostname does not match.\n"));
    }

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate is not yet valid.\n"));
    }

  if (failures & SVN_AUTH_SSL_EXPIRED)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate has expired.\n"));
    }

  if (failures & SVN_AUTH_SSL_OTHER)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate has an unknown error.\n"));
    }

  msg = svn_stringbuf_createf
    (pool,
     _("Certificate information:\n"
       " - Hostname: %s\n"
       " - Valid: from %s until %s\n"
       " - Issuer: %s\n"
       " - Fingerprint: %s\n"),
     cert_info->hostname,
     cert_info->valid_from,
     cert_info->valid_until,
     cert_info->issuer_dname,
     cert_info->fingerprint);
  svn_stringbuf_appendstr(buf, msg);

  if (may_save)
    {
      svn_stringbuf_appendcstr
        (buf, _("(R)eject, accept (t)emporarily or accept (p)ermanently? "));
    }
  else
    {
      svn_stringbuf_appendcstr(buf, _("(R)eject or accept (t)emporarily? "));
    }
  SVN_ERR(prompt(&choice, buf->data, FALSE, pb, pool));

  if (choice[0] == 't' || choice[0] == 'T')
    {
      *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
      (*cred_p)->may_save = FALSE;
      (*cred_p)->accepted_failures = failures;
    }
  else if (may_save && (choice[0] == 'p' || choice[0] == 'P'))
    {
      *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
      (*cred_p)->may_save = TRUE;
      (*cred_p)->accepted_failures = failures;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_cert_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_prompt
  (svn_auth_cred_ssl_client_cert_t **cred_p,
   void *baton,
   const char *realm,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  svn_auth_cred_ssl_client_cert_t *cred = NULL;
  const char *cert_file = NULL;
  const char *abs_cert_file = NULL;
  svn_cmdline_prompt_baton2_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));
  SVN_ERR(prompt(&cert_file, _("Client certificate filename: "),
                 FALSE, pb, pool));
  SVN_ERR(svn_dirent_get_absolute(&abs_cert_file, cert_file, pool));

  cred = apr_palloc(pool, sizeof(*cred));
  cred->cert_file = abs_cert_file;
  cred->may_save = may_save;
  *cred_p = cred;

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_cert_pw_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_pw_prompt
  (svn_auth_cred_ssl_client_cert_pw_t **cred_p,
   void *baton,
   const char *realm,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  svn_auth_cred_ssl_client_cert_pw_t *cred = NULL;
  const char *result;
  const char *text = apr_psprintf(pool, _("Passphrase for '%s': "), realm);
  svn_cmdline_prompt_baton2_t *pb = baton;

  SVN_ERR(prompt(&result, text, TRUE, pb, pool));

  cred = apr_pcalloc(pool, sizeof(*cred));
  cred->password = result;
  cred->may_save = may_save;
  *cred_p = cred;

  return SVN_NO_ERROR;
}

/* This is a helper for plaintext prompt functions. */
static svn_error_t *
plaintext_prompt_helper(svn_boolean_t *may_save_plaintext,
                        const char *realmstring,
                        const char *prompt_string,
                        const char *prompt_text,
                        void *baton,
                        apr_pool_t *pool)
{
  const char *answer = NULL;
  svn_boolean_t answered = FALSE;
  svn_cmdline_prompt_baton2_t *pb = baton;
  const char *config_path = NULL;
  terminal_handle_t *terminal;

  *may_save_plaintext = FALSE; /* de facto API promise */

  if (pb)
    SVN_ERR(svn_config_get_user_config_path(&config_path, pb->config_dir,
                                            SVN_CONFIG_CATEGORY_SERVERS, pool));

  SVN_ERR(terminal_open(&terminal, FALSE, pool));
  SVN_ERR(terminal_puts(apr_psprintf(pool, prompt_text,
                                     realmstring, config_path),
                        terminal, pool));
  SVN_ERR(terminal_close(terminal));

  do
    {
      SVN_ERR(prompt(&answer, prompt_string, FALSE, pb, pool));
      if (apr_strnatcasecmp(answer, _("yes")) == 0 ||
          apr_strnatcasecmp(answer, _("y")) == 0)
        {
          *may_save_plaintext = TRUE;
          answered = TRUE;
        }
      else if (apr_strnatcasecmp(answer, _("no")) == 0 ||
               apr_strnatcasecmp(answer, _("n")) == 0)
        {
          *may_save_plaintext = FALSE;
          answered = TRUE;
        }
      else
          prompt_string = _("Please type 'yes' or 'no': ");
    }
  while (! answered);

  return SVN_NO_ERROR;
}

/* This implements 'svn_auth_plaintext_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_plaintext_prompt(svn_boolean_t *may_save_plaintext,
                                  const char *realmstring,
                                  void *baton,
                                  apr_pool_t *pool)
{
  const char *prompt_string = _("Store password unencrypted (yes/no)? ");
  const char *prompt_text =
  _("\n-----------------------------------------------------------------------"
    "\nATTENTION!  Your password for authentication realm:\n"
    "\n"
    "   %s\n"
    "\n"
    "can only be stored to disk unencrypted!  You are advised to configure\n"
    "your system so that Subversion can store passwords encrypted, if\n"
    "possible.  See the documentation for details.\n"
    "\n"
    "You can avoid future appearances of this warning by setting the value\n"
    "of the 'store-plaintext-passwords' option to either 'yes' or 'no' in\n"
    "'%s'.\n"
    "-----------------------------------------------------------------------\n"
    );

  return plaintext_prompt_helper(may_save_plaintext, realmstring,
                                 prompt_string, prompt_text, baton,
                                 pool);
}

/* This implements 'svn_auth_plaintext_passphrase_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_plaintext_passphrase_prompt(svn_boolean_t *may_save_plaintext,
                                             const char *realmstring,
                                             void *baton,
                                             apr_pool_t *pool)
{
  const char *prompt_string = _("Store passphrase unencrypted (yes/no)? ");
  const char *prompt_text =
  _("\n-----------------------------------------------------------------------\n"
    "ATTENTION!  Your passphrase for client certificate:\n"
    "\n"
    "   %s\n"
    "\n"
    "can only be stored to disk unencrypted!  You are advised to configure\n"
    "your system so that Subversion can store passphrase encrypted, if\n"
    "possible.  See the documentation for details.\n"
    "\n"
    "You can avoid future appearances of this warning by setting the value\n"
    "of the 'store-ssl-client-cert-pp-plaintext' option to either 'yes' or\n"
    "'no' in '%s'.\n"
    "-----------------------------------------------------------------------\n"
    );

  return plaintext_prompt_helper(may_save_plaintext, realmstring,
                                 prompt_string, prompt_text, baton,
                                 pool);
}


/** Generic prompting. **/

svn_error_t *
svn_cmdline_prompt_user2(const char **result,
                         const char *prompt_str,
                         svn_cmdline_prompt_baton_t *baton,
                         apr_pool_t *pool)
{
  /* XXX: We know prompt doesn't use the new members
   * of svn_cmdline_prompt_baton2_t. */
  return prompt(result, prompt_str, FALSE /* don't hide input */,
                (svn_cmdline_prompt_baton2_t *)baton, pool);
}

/* This implements 'svn_auth_gnome_keyring_unlock_prompt_func_t'. */
svn_error_t *
svn_cmdline__auth_gnome_keyring_unlock_prompt(char **keyring_password,
                                              const char *keyring_name,
                                              void *baton,
                                              apr_pool_t *pool)
{
  const char *password;
  const char *pass_prompt;
  svn_cmdline_prompt_baton2_t *pb = baton;

  pass_prompt = apr_psprintf(pool, _("Password for '%s' GNOME keyring: "),
                             keyring_name);
  SVN_ERR(prompt(&password, pass_prompt, TRUE, pb, pool));
  *keyring_password = apr_pstrdup(pool, password);
  return SVN_NO_ERROR;
}
