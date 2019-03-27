/*
 * gpg_agent.c: GPG Agent provider for SVN_AUTH_CRED_*
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

/* This auth provider stores a plaintext password in memory managed by
 * a running gpg-agent. In contrast to other password store providers
 * it does not save the password to disk.
 *
 * Prompting is performed by the gpg-agent using a "pinentry" program
 * which needs to be installed separately. There are several pinentry
 * implementations with different front-ends (e.g. qt, gtk, ncurses).
 *
 * The gpg-agent will let the password time out after a while,
 * or immediately when it receives the SIGHUP signal.
 * When the password has timed out it will automatically prompt the
 * user for the password again. This is transparent to Subversion.
 *
 * SECURITY CONSIDERATIONS:
 *
 * Communication to the agent happens over a UNIX socket, which is located
 * in a directory which only the user running Subversion can access.
 * However, any program the user runs could access this socket and get
 * the Subversion password if the program knows the "cache ID" Subversion
 * uses for the password.
 * The cache ID is very easy to obtain for programs running as the same user.
 * Subversion uses the MD5 of the realmstring as cache ID, and these checksums
 * are also used as filenames within ~/.subversion/auth/svn.simple.
 * Unlike GNOME Keyring or KDE Wallet, the user is not prompted for
 * permission if another program attempts to access the password.
 *
 * Therefore, while the gpg-agent is running and has the password cached,
 * this provider is no more secure than a file storing the password in
 * plaintext.
 */


/*** Includes. ***/

#ifndef WIN32

#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_user.h>
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_checksum.h"
#include "svn_string.h"
#include "svn_hash.h"
#include "svn_user.h"
#include "svn_dirent_uri.h"

#include "auth.h"
#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#ifdef SVN_HAVE_GPG_AGENT

#define BUFFER_SIZE 1024
#define ATTEMPT_PARAMETER "svn.simple.gpg_agent.attempt"

/* Modify STR in-place such that blanks are escaped as required by the
 * gpg-agent protocol. Return a pointer to STR. */
static char *
escape_blanks(char *str)
{
  char *s = str;

  while (*s)
    {
      if (*s == ' ')
        *s = '+';
      s++;
    }

  return str;
}

#define is_hex(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'A' && (c) <= 'F'))
#define hex_to_int(c) ((c) < '9' ? (c) - '0' : (c) - 'A' + 10)
 
/* Modify STR in-place.  '%', CR and LF are always percent escaped,
   other characters may be percent escaped, always using uppercase
   hex, see https://www.gnupg.org/documentation/manuals/assuan.pdf */
static char *
unescape_assuan(char *str)
{
  char *s = str;

  while (s[0])
    {
      if (s[0] == '%' && is_hex(s[1]) && is_hex(s[2]))
        {
          char *s2 = s;
          char val = hex_to_int(s[1]) * 16 + hex_to_int(s[2]);

          s2[0] = val;
          ++s2;

          while (s2[2])
            {
              s2[0] = s2[2];
              ++s2;
            }
          s2[0] = '\0';
        }
      ++s;
    }

  return str;
}

/* Generate the string CACHE_ID_P based on the REALMSTRING allocated in
 * RESULT_POOL using SCRATCH_POOL for temporary allocations.  This is similar
 * to other password caching mechanisms. */
static svn_error_t *
get_cache_id(const char **cache_id_p, const char *realmstring,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  const char *cache_id = NULL;
  svn_checksum_t *digest = NULL;

  SVN_ERR(svn_checksum(&digest, svn_checksum_md5, realmstring,
                       strlen(realmstring), scratch_pool));
  cache_id = svn_checksum_to_cstring(digest, result_pool);
  *cache_id_p = cache_id;

  return SVN_NO_ERROR;
}

/* Attempt to read a gpg-agent response message from the socket SD into
 * buffer BUF. Buf is assumed to be N bytes large. Return TRUE if a response
 * message could be read that fits into the buffer. Else return FALSE.
 * If a message could be read it will always be NUL-terminated and the
 * trailing newline is retained. */
static svn_boolean_t
receive_from_gpg_agent(int sd, char *buf, size_t n)
{
  int i = 0;
  size_t recvd;
  char c;

  /* Clear existing buffer content before reading response. */
  if (n > 0)
    *buf = '\0';

  /* Require the message to fit into the buffer and be terminated
   * with a newline. */
  while (i < n)
    {
      recvd = read(sd, &c, 1);
      if (recvd == -1)
        return FALSE;
      buf[i] = c;
      i++;
      if (i < n && c == '\n')
        {
          buf[i] = '\0';
          return TRUE;
        }
    }

    return FALSE;
}

/* Using socket SD, send the option OPTION with the specified VALUE
 * to the gpg agent. Store the response in BUF, assumed to be N bytes
 * in size, and evaluate the response. Return TRUE if the agent liked
 * the smell of the option, if there is such a thing, and doesn't feel
 * saturated by it. Else return FALSE.
 * Do temporary allocations in scratch_pool. */
static svn_boolean_t
send_option(int sd, char *buf, size_t n, const char *option, const char *value,
            apr_pool_t *scratch_pool)
{
  const char *request;

  request = apr_psprintf(scratch_pool, "OPTION %s=%s\n", option, value);

  if (write(sd, request, strlen(request)) == -1)
    return FALSE;

  if (!receive_from_gpg_agent(sd, buf, n))
    return FALSE;

  return (strncmp(buf, "OK", 2) == 0);
}

/* Send the BYE command and disconnect from the gpg-agent.  Doing this avoids
 * gpg-agent emitting a "Connection reset by peer" log message with some
 * versions of gpg-agent. */
static void
bye_gpg_agent(int sd)
{
  /* don't bother to check the result of the write, it either worked or it
   * didn't, but either way we're closing. */
  write(sd, "BYE\n", 4);
  close(sd);
}

/* This implements a method of finding the socket which is a mix of the
 * description from GPG 1.x's gpg-agent man page under the
 * --use-standard-socket option and the logic from GPG 2.x's socket discovery
 * code in common/homedir.c.
 *
 * The man page says the standard socket is "named 'S.gpg-agent' located
 * in the home directory."  GPG's home directory is either the directory
 * specified by $GNUPGHOME or ~/.gnupg.  GPG >= 2.1.13 will check for a
 * socket under (/var)/run/UID/gnupg before ~/.gnupg if no environment
 * variables are set.
 *
 * $GPG_AGENT_INFO takes precedence, if set, otherwise $GNUPGHOME will be
 * used.  For GPG >= 2.1.13, $GNUPGHOME will be used directly only if it
 * refers to the canonical home -- ~/.gnupg.  Otherwise, the path specified
 * by $GNUPGHOME is hashed (SHA1 + z-base-32) and the socket is expected to
 * be present under (/var)/run/UID/gnupg/d.HASH. This last mechanism is not
 * yet supported here. */
static const char *
find_gpg_agent_socket(apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  char *gpg_agent_info = NULL;
  char *gnupghome = NULL;
  const char *socket_name = NULL;

  if ((gpg_agent_info = getenv("GPG_AGENT_INFO")) != NULL)
    {
      apr_array_header_t *socket_details;

      /* For reference GPG_AGENT_INFO consists of 3 : separated fields.
       * The path to the socket, the pid of the gpg-agent process and
       * finally the version of the protocol the agent talks. */
      socket_details = svn_cstring_split(gpg_agent_info, ":", TRUE,
                                         scratch_pool);
      socket_name = APR_ARRAY_IDX(socket_details, 0, const char *);
    }
  else if ((gnupghome = getenv("GNUPGHOME")) != NULL)
    {
      const char *homedir = svn_dirent_canonicalize(gnupghome, scratch_pool);
      socket_name = svn_dirent_join(homedir, "S.gpg-agent", scratch_pool);
    }
  else
    {
      int i = 0;
      const char *maybe_socket[] = {NULL, NULL, NULL, NULL};
      const char *homedir;

#ifdef APR_HAS_USER
      apr_uid_t uid;
      apr_gid_t gid;

      if (apr_uid_current(&uid, &gid, scratch_pool) == APR_SUCCESS)
        {
          const char *uidbuf = apr_psprintf(scratch_pool, "%lu",
                                            (unsigned long)uid);
          maybe_socket[i++] = svn_dirent_join_many(scratch_pool, "/run/user",
                                                   uidbuf, "gnupg",
                                                   "S.gpg-agent",
                                                   SVN_VA_NULL);
          maybe_socket[i++] = svn_dirent_join_many(scratch_pool,
                                                   "/var/run/user",
                                                   uidbuf, "gnupg",
                                                   "S.gpg-agent",
                                                   SVN_VA_NULL);
        }
#endif

      homedir = svn_user_get_homedir(scratch_pool);
      if (homedir)
        maybe_socket[i++] = svn_dirent_join_many(scratch_pool, homedir,
                                                 ".gnupg", "S.gpg-agent",
                                                 SVN_VA_NULL);

      for (i = 0; !socket_name && maybe_socket[i]; i++)
        {
          apr_finfo_t finfo;
          svn_error_t *err = svn_io_stat(&finfo, maybe_socket[i],
                                         APR_FINFO_TYPE, scratch_pool);
          if (!err && finfo.filetype == APR_SOCK)
            socket_name = maybe_socket[i];
          svn_error_clear(err);
        }
    }

  if (socket_name)
    socket_name = apr_pstrdup(result_pool, socket_name);

  return socket_name;
}

/* Locate a running GPG Agent, and return an open file descriptor
 * for communication with the agent in *NEW_SD. If no running agent
 * can be found, set *NEW_SD to -1. */
static svn_error_t *
find_running_gpg_agent(int *new_sd, apr_pool_t *pool)
{
  char *buffer;
  const char *socket_name = find_gpg_agent_socket(pool, pool);
  const char *request = NULL;
  const char *p = NULL;
  char *ep = NULL;
  int sd;

  *new_sd = -1;

  if (socket_name != NULL)
    {
      struct sockaddr_un addr;

      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, socket_name, sizeof(addr.sun_path) - 1);
      addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

      sd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (sd == -1)
        return SVN_NO_ERROR;

      if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
          close(sd);
          return SVN_NO_ERROR;
        }
    }
  else
    return SVN_NO_ERROR;

  /* Receive the connection status from the gpg-agent daemon. */
  buffer = apr_palloc(pool, BUFFER_SIZE);
  if (!receive_from_gpg_agent(sd, buffer, BUFFER_SIZE))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  if (strncmp(buffer, "OK", 2) != 0)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  /* The GPG-Agent documentation says:
   *  "Clients should deny to access an agent with a socket name which does
   *   not match its own configuration". */
  request = "GETINFO socket_name\n";
  if (write(sd, request, strlen(request)) == -1)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  if (!receive_from_gpg_agent(sd, buffer, BUFFER_SIZE))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  if (strncmp(buffer, "D", 1) == 0)
    p = &buffer[2];
  if (!p)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  ep = strchr(p, '\n');
  if (ep != NULL)
    *ep = '\0';
  if (strcmp(socket_name, p) != 0)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  /* The agent will terminate its response with "OK". */
  if (!receive_from_gpg_agent(sd, buffer, BUFFER_SIZE))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  if (strncmp(buffer, "OK", 2) != 0)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  *new_sd = sd;
  return SVN_NO_ERROR;
}

static svn_boolean_t
send_options(int sd, char *buf, size_t n, apr_pool_t *scratch_pool)
{
  const char *tty_name;
  const char *tty_type;
  const char *lc_ctype;
  const char *display;

  /* Send TTY_NAME to the gpg-agent daemon. */
  tty_name = getenv("GPG_TTY");
  if (tty_name != NULL)
    {
      if (!send_option(sd, buf, n, "ttyname", tty_name, scratch_pool))
        return FALSE;
    }

  /* Send TTY_TYPE to the gpg-agent daemon. */
  tty_type = getenv("TERM");
  if (tty_type != NULL)
    {
      if (!send_option(sd, buf, n, "ttytype", tty_type, scratch_pool))
        return FALSE;
    }

  /* Compute LC_CTYPE. */
  lc_ctype = getenv("LC_ALL");
  if (lc_ctype == NULL)
    lc_ctype = getenv("LC_CTYPE");
  if (lc_ctype == NULL)
    lc_ctype = getenv("LANG");

  /* Send LC_CTYPE to the gpg-agent daemon. */
  if (lc_ctype != NULL)
    {
      if (!send_option(sd, buf, n, "lc-ctype", lc_ctype, scratch_pool))
        return FALSE;
    }

  /* Send DISPLAY to the gpg-agent daemon. */
  display = getenv("DISPLAY");
  if (display != NULL)
    {
      if (!send_option(sd, buf, n, "display", display, scratch_pool))
        return FALSE;
    }

  return TRUE;
}

/* Implementation of svn_auth__password_get_t that retrieves the password
   from gpg-agent */
static svn_error_t *
password_get_gpg_agent(svn_boolean_t *done,
                       const char **password,
                       apr_hash_t *creds,
                       const char *realmstring,
                       const char *username,
                       apr_hash_t *parameters,
                       svn_boolean_t non_interactive,
                       apr_pool_t *pool)
{
  int sd;
  char *p = NULL;
  char *ep = NULL;
  char *buffer;
  const char *request = NULL;
  const char *cache_id = NULL;
  char *password_prompt;
  char *realm_prompt;
  char *error_prompt;
  int *attempt;

  *done = FALSE;

  attempt = svn_hash_gets(parameters, ATTEMPT_PARAMETER);

  SVN_ERR(find_running_gpg_agent(&sd, pool));
  if (sd == -1)
    return SVN_NO_ERROR;

  buffer = apr_palloc(pool, BUFFER_SIZE);

  if (!send_options(sd, buffer, BUFFER_SIZE, pool))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_cache_id(&cache_id, realmstring, pool, pool));

  password_prompt = apr_psprintf(pool, _("Password for '%s': "), username);
  realm_prompt = apr_psprintf(pool, _("Enter your Subversion password for %s"),
                              realmstring);
  if (*attempt == 1)
    /* X means no error to the gpg-agent protocol */
    error_prompt = apr_pstrdup(pool, "X");
  else
    error_prompt = apr_pstrdup(pool, _("Authentication failed"));

  request = apr_psprintf(pool,
                         "GET_PASSPHRASE --data %s"
                         "%s %s %s %s\n",
                         non_interactive ? "--no-ask " : "",
                         cache_id,
                         escape_blanks(error_prompt),
                         escape_blanks(password_prompt),
                         escape_blanks(realm_prompt));

  if (write(sd, request, strlen(request)) == -1)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }
  if (!receive_from_gpg_agent(sd, buffer, BUFFER_SIZE))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  bye_gpg_agent(sd);

  if (strncmp(buffer, "ERR", 3) == 0)
    return SVN_NO_ERROR;

  p = NULL;
  if (strncmp(buffer, "D", 1) == 0)
    p = &buffer[2];

  if (!p)
    return SVN_NO_ERROR;

  ep = strchr(p, '\n');
  if (ep != NULL)
    *ep = '\0';

  *password = unescape_assuan(p);

  *done = TRUE;
  return SVN_NO_ERROR;
}


/* Implementation of svn_auth__password_set_t that would store the
   password in GPG Agent if that's how this particular integration
   worked.  But it isn't.  GPG Agent stores the password provided by
   the user via the pinentry program immediately upon its provision
   (and regardless of its accuracy as passwords go), so we just need
   to check if a running GPG Agent exists. */
static svn_error_t *
password_set_gpg_agent(svn_boolean_t *done,
                       apr_hash_t *creds,
                       const char *realmstring,
                       const char *username,
                       const char *password,
                       apr_hash_t *parameters,
                       svn_boolean_t non_interactive,
                       apr_pool_t *pool)
{
  int sd;

  *done = FALSE;

  SVN_ERR(find_running_gpg_agent(&sd, pool));
  if (sd == -1)
    return SVN_NO_ERROR;

  bye_gpg_agent(sd);
  *done = TRUE;

  return SVN_NO_ERROR;
}


/* An implementation of svn_auth_provider_t::first_credentials() */
static svn_error_t *
simple_gpg_agent_first_creds(void **credentials,
                             void **iter_baton,
                             void *provider_baton,
                             apr_hash_t *parameters,
                             const char *realmstring,
                             apr_pool_t *pool)
{
  svn_error_t *err;
  int *attempt = apr_palloc(pool, sizeof(*attempt));

  *attempt = 1;
  svn_hash_sets(parameters, ATTEMPT_PARAMETER, attempt);
  err = svn_auth__simple_creds_cache_get(credentials, iter_baton,
                                         provider_baton, parameters,
                                         realmstring, password_get_gpg_agent,
                                         SVN_AUTH__GPG_AGENT_PASSWORD_TYPE,
                                         pool);
  *iter_baton = attempt;

  return err;
}

/* An implementation of svn_auth_provider_t::next_credentials() */
static svn_error_t *
simple_gpg_agent_next_creds(void **credentials,
                            void *iter_baton,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  int *attempt = (int *)iter_baton;
  int sd;
  char *buffer;
  const char *cache_id = NULL;
  const char *request = NULL;

  *credentials = NULL;

  /* The users previous credentials failed so first remove the cached entry,
   * before trying to retrieve them again.  Because gpg-agent stores cached
   * credentials immediately upon retrieving them, this gives us the
   * opportunity to remove the invalid credentials and prompt the
   * user again.  While it's possible that server side issues could trigger
   * this, this cache is ephemeral so at worst we're just speeding up
   * when the user would need to re-enter their password. */

  if (svn_hash_gets(parameters, SVN_AUTH_PARAM_NON_INTERACTIVE))
    {
      /* In this case since we're running non-interactively we do not
       * want to clear the cache since the user was never prompted by
       * gpg-agent to set a password. */
      return SVN_NO_ERROR;
    }

  *attempt = *attempt + 1;

  SVN_ERR(find_running_gpg_agent(&sd, pool));
  if (sd == -1)
    return SVN_NO_ERROR;

  buffer = apr_palloc(pool, BUFFER_SIZE);

  if (!send_options(sd, buffer, BUFFER_SIZE, pool))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_cache_id(&cache_id, realmstring, pool, pool));

  request = apr_psprintf(pool, "CLEAR_PASSPHRASE %s\n", cache_id);

  if (write(sd, request, strlen(request)) == -1)
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  if (!receive_from_gpg_agent(sd, buffer, BUFFER_SIZE))
    {
      bye_gpg_agent(sd);
      return SVN_NO_ERROR;
    }

  bye_gpg_agent(sd);

  if (strncmp(buffer, "OK\n", 3) != 0)
    return SVN_NO_ERROR;

  /* TODO: This attempt limit hard codes it at 3 attempts (or 2 retries)
   * which matches svn command line client's retry_limit as set in
   * svn_cmdline_create_auth_baton().  It would be nice to have that
   * limit reflected here but that violates the boundry between the
   * prompt provider and the cache provider.  gpg-agent is acting as
   * both here due to the peculiarties of their design so we'll have to
   * live with this for now.  Note that when these failures get exceeded
   * it'll eventually fall back on the retry limits of whatever prompt
   * provider is in effect, so this effectively doubles the limit. */
  if (*attempt < 4)
    return svn_auth__simple_creds_cache_get(credentials, &iter_baton,
                                            provider_baton, parameters,
                                            realmstring,
                                            password_get_gpg_agent,
                                            SVN_AUTH__GPG_AGENT_PASSWORD_TYPE,
                                            pool);

  return SVN_NO_ERROR;
}


/* An implementation of svn_auth_provider_t::save_credentials() */
static svn_error_t *
simple_gpg_agent_save_creds(svn_boolean_t *saved,
                            void *credentials,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_set(saved, credentials,
                                          provider_baton, parameters,
                                          realmstring, password_set_gpg_agent,
                                          SVN_AUTH__GPG_AGENT_PASSWORD_TYPE,
                                          pool);
}


static const svn_auth_provider_t gpg_agent_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_gpg_agent_first_creds,
  simple_gpg_agent_next_creds,
  simple_gpg_agent_save_creds
};


/* Public API */
void
svn_auth__get_gpg_agent_simple_provider(svn_auth_provider_object_t **provider,
                                       apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gpg_agent_simple_provider;
  *provider = po;
}

#endif /* SVN_HAVE_GPG_AGENT */
#endif /* !WIN32 */
