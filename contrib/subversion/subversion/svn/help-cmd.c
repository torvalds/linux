/*
 * help-cmd.c -- Provide help
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

#include "svn_hash.h"
#include "svn_string.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__help(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = NULL;
  svn_stringbuf_t *version_footer = NULL;
  const char *config_path;

  char help_header[] =
  N_("usage: svn <subcommand> [options] [args]\n"
     "Subversion command-line client.\n"
     "Type 'svn help <subcommand>' for help on a specific subcommand.\n"
     "Type 'svn --version' to see the program version and RA modules,\n"
     "     'svn --version --verbose' to see dependency versions as well,\n"
     "     'svn --version --quiet' to see just the version number.\n"
     "\n"
     "Most subcommands take file and/or directory arguments, recursing\n"
     "on the directories.  If no arguments are supplied to such a\n"
     "command, it recurses on the current directory (inclusive) by default.\n"
     "\n"
     "Available subcommands:\n");

  char help_footer[] =
  N_("Subversion is a tool for version control.\n"
     "For additional information, see http://subversion.apache.org/\n");

  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");

  if (baton)
    {
      svn_cl__cmd_baton_t *const cmd_baton = baton;
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
      /* Windows never actually stores plaintext passwords, it
         encrypts the contents using CryptoAPI. ...

         ... If CryptoAPI is available ... but it should be on all
         versions of Windows that are even remotely interesting two
         days before the scheduled end of the world, when this comment
         is being written. */
#  ifndef WIN32
      svn_boolean_t store_auth_creds =
        SVN_CONFIG_DEFAULT_OPTION_STORE_AUTH_CREDS;
      svn_boolean_t store_passwords =
        SVN_CONFIG_DEFAULT_OPTION_STORE_PASSWORDS;
      svn_boolean_t store_plaintext_passwords = FALSE;
      svn_config_t *cfg;

      if (cmd_baton->ctx->config)
        {
          cfg = svn_hash_gets(cmd_baton->ctx->config,
                              SVN_CONFIG_CATEGORY_CONFIG);
          if (cfg)
            {
              SVN_ERR(svn_config_get_bool(cfg, &store_auth_creds,
                                          SVN_CONFIG_SECTION_AUTH,
                                          SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
                                          store_auth_creds));
              SVN_ERR(svn_config_get_bool(cfg, &store_passwords,
                                          SVN_CONFIG_SECTION_AUTH,
                                          SVN_CONFIG_OPTION_STORE_PASSWORDS,
                                          store_passwords));
            }
          cfg = svn_hash_gets(cmd_baton->ctx->config,
                              SVN_CONFIG_CATEGORY_SERVERS);
          if (cfg)
            {
              const char *value;
              SVN_ERR(svn_config_get_yes_no_ask
                      (cfg, &value,
                       SVN_CONFIG_SECTION_GLOBAL,
                       SVN_CONFIG_OPTION_STORE_PLAINTEXT_PASSWORDS,
                       SVN_CONFIG_DEFAULT_OPTION_STORE_PLAINTEXT_PASSWORDS));
              if (0 == svn_cstring_casecmp(value, SVN_CONFIG_TRUE))
                store_plaintext_passwords = TRUE;
            }
        }

      if (store_plaintext_passwords && store_auth_creds && store_passwords)
        {
          version_footer = svn_stringbuf_create(
              _("WARNING: Plaintext password storage is enabled!\n\n"),
              pool);
          svn_stringbuf_appendcstr(version_footer, ra_desc_start);
        }
#  endif /* !WIN32 */
#endif /* !SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE */

      opt_state = cmd_baton->opt_state;
    }

  if (!version_footer)
    version_footer = svn_stringbuf_create(ra_desc_start, pool);
  SVN_ERR(svn_ra_print_modules(version_footer, pool));

  /*
   * Show auth creds storage providers.
   */
  SVN_ERR(svn_config_get_user_config_path(&config_path,
                                          opt_state ? opt_state->config_dir
                                                    : NULL,
                                          NULL,
                                          pool));
  svn_stringbuf_appendcstr(version_footer,
                           _("\nThe following authentication credential caches are available:\n\n"));

  /*### There is no API to query available providers at run time. */
  if (config_path)
    {
#if (defined(WIN32) && !defined(__MINGW32__))
      version_footer =
        svn_stringbuf_create(apr_psprintf(pool, _("%s* Wincrypt cache in %s\n"),
                                          version_footer->data,
                                          svn_dirent_local_style(config_path,
                                                                 pool)),
                             pool);
#elif !defined(SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE)
      version_footer =
        svn_stringbuf_create(apr_psprintf(pool, _("%s* Plaintext cache in %s\n"),
                                          version_footer->data,
                                          svn_dirent_local_style(config_path,
                                                                 pool)),
                             pool);
#endif
    }
#ifdef SVN_HAVE_GNOME_KEYRING
  svn_stringbuf_appendcstr(version_footer, "* Gnome Keyring\n");
#endif
#ifdef SVN_HAVE_GPG_AGENT
  svn_stringbuf_appendcstr(version_footer, "* GPG-Agent\n");
#endif
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
  svn_stringbuf_appendcstr(version_footer, "* Mac OS X Keychain\n");
#endif
#ifdef SVN_HAVE_KWALLET
  svn_stringbuf_appendcstr(version_footer, "* KWallet (KDE)\n");
#endif

  return svn_opt_print_help4(os,
                             "svn",   /* ### erm, derive somehow? */
                             opt_state ? opt_state->version : FALSE,
                             opt_state ? opt_state->quiet : FALSE,
                             opt_state ? opt_state->verbose : FALSE,
                             version_footer->data,
                             _(help_header),
                             svn_cl__cmd_table,
                             svn_cl__options,
                             svn_cl__global_options,
                             _(help_footer),
                             pool);
}
