/*
 * auth-cmd.c:  Subversion auth creds cache administration
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

/*** Includes. ***/

#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_fnmatch.h>
#include <apr_tables.h>

#include "svn_private_config.h"

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_opt.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_utf.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_auth.h"
#include "svn_sorts.h"
#include "svn_base64.h"
#include "svn_x509.h"
#include "svn_time.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_token.h"
#include "private/svn_sorts_private.h"

#include "cl.h"

/* The separator between credentials . */
#define SEP_STRING \
  "------------------------------------------------------------------------\n"

static svn_error_t *
show_cert_failures(const char *failure_string,
                   apr_pool_t *scratch_pool)
{
  unsigned int failures;

  SVN_ERR(svn_cstring_atoui(&failures, failure_string));

  if (0 == (failures & (SVN_AUTH_SSL_NOTYETVALID | SVN_AUTH_SSL_EXPIRED |
                        SVN_AUTH_SSL_CNMISMATCH | SVN_AUTH_SSL_UNKNOWNCA |
                        SVN_AUTH_SSL_OTHER)))
    return SVN_NO_ERROR;

  SVN_ERR(svn_cmdline_printf(
            scratch_pool, _("Automatic certificate validity check failed "
                            "because:\n")));

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate is not yet valid.\n")));

  if (failures & SVN_AUTH_SSL_EXPIRED)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate has expired.\n")));

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate's Common Name (hostname) "
                              "does not match the remote hostname.\n")));

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate issuer is unknown.\n")));

  if (failures & SVN_AUTH_SSL_OTHER)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  Unknown verification failure.\n")));

  return SVN_NO_ERROR;
}


/* decodes from format we store certs in for auth creds and
 * turns parsing errors into warnings if PRINT_WARNING is TRUE
 * and ignores them otherwise. returns NULL if it couldn't
 * parse a cert for any reason. */
static svn_x509_certinfo_t *
parse_certificate(const svn_string_t *ascii_cert,
                  svn_boolean_t print_warning,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_x509_certinfo_t *certinfo;
  const svn_string_t *der_cert;
  svn_error_t *err;

  /* Convert header-less PEM to DER by undoing base64 encoding. */
  der_cert = svn_base64_decode_string(ascii_cert, scratch_pool);

  err = svn_x509_parse_cert(&certinfo, der_cert->data, der_cert->len,
                            result_pool, scratch_pool);
  if (err)
    {
      /* Just display X.509 parsing errors as warnings and continue */
      if (print_warning)
        svn_handle_warning2(stderr, err, "svn: ");
      svn_error_clear(err);
      return NULL;
    }

  return certinfo;
}


struct walk_credentials_baton_t
{
  int matches;
  svn_boolean_t list;
  svn_boolean_t delete;
  svn_boolean_t show_passwords;
  apr_array_header_t *patterns;
};

static svn_boolean_t
match_pattern(const char *pattern, const char *value,
              svn_boolean_t caseblind, apr_pool_t *scratch_pool)
{
  const char *p = apr_psprintf(scratch_pool, "*%s*", pattern);
  int flags = (caseblind ? APR_FNM_CASE_BLIND : 0);

  return (apr_fnmatch(p, value, flags) == APR_SUCCESS);
}

static svn_boolean_t
match_certificate(svn_x509_certinfo_t **certinfo,
                  const char *pattern,
                  const svn_string_t *ascii_cert,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *value;
  const svn_checksum_t *checksum;
  const apr_array_header_t *hostnames;
  int i;

  *certinfo = parse_certificate(ascii_cert, FALSE, result_pool, scratch_pool);
  if (*certinfo == NULL)
    return FALSE;

  value = svn_x509_certinfo_get_subject(*certinfo, scratch_pool);
  if (match_pattern(pattern, value, FALSE, scratch_pool))
    return TRUE;

  value = svn_x509_certinfo_get_issuer(*certinfo, scratch_pool);
  if (match_pattern(pattern, value, FALSE, scratch_pool))
    return TRUE;

  checksum = svn_x509_certinfo_get_digest(*certinfo);
  value = svn_checksum_to_cstring_display(checksum, scratch_pool);
  if (match_pattern(pattern, value, TRUE, scratch_pool))
    return TRUE;

  hostnames = svn_x509_certinfo_get_hostnames(*certinfo);
  if (hostnames)
    {
      for (i = 0; i < hostnames->nelts; i++)
        {
          const char *hostname = APR_ARRAY_IDX(hostnames, i, const char *);
          if (match_pattern(pattern, hostname, TRUE, scratch_pool))
            return TRUE;
        }
    }

  return FALSE;
}


static svn_error_t *
match_credential(svn_boolean_t *match,
                 svn_x509_certinfo_t **certinfo,
                 const char *cred_kind,
                 const char *realmstring,
                 apr_array_header_t *patterns,
                 apr_array_header_t *cred_items,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *match = FALSE;

  for (i = 0; i < patterns->nelts; i++)
    {
      const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
      int j;

      *match = match_pattern(pattern, cred_kind, FALSE, iterpool);
      if (!*match)
        *match = match_pattern(pattern, realmstring, FALSE, iterpool);
      if (!*match)
        {
          svn_pool_clear(iterpool);
          for (j = 0; j < cred_items->nelts; j++)
            {
              svn_sort__item_t item;
              const char *key;
              svn_string_t *value;

              item = APR_ARRAY_IDX(cred_items, j, svn_sort__item_t);
              key = item.key;
              value = item.value;
              if (strcmp(key, SVN_CONFIG_AUTHN_PASSWORD_KEY) == 0 ||
                  strcmp(key, SVN_CONFIG_AUTHN_PASSPHRASE_KEY) == 0)
                continue; /* don't match secrets */
              else if (strcmp(key, SVN_CONFIG_AUTHN_ASCII_CERT_KEY) == 0)
                *match = match_certificate(certinfo, pattern, value,
                                           result_pool, iterpool);
              else
                *match = match_pattern(pattern, value->data, FALSE, iterpool);

              if (*match)
                break;
            }
        }
      if (!*match)
        break;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
show_cert(svn_x509_certinfo_t *certinfo, const svn_string_t *pem_cert,
          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *hostnames;

  if (certinfo == NULL)
    certinfo = parse_certificate(pem_cert, TRUE, scratch_pool, scratch_pool);
  if (certinfo == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Subject: %s\n"),
                             svn_x509_certinfo_get_subject(certinfo, scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Valid from: %s\n"),
                             svn_time_to_human_cstring(
                                 svn_x509_certinfo_get_valid_from(certinfo),
                                 scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Valid until: %s\n"),
                             svn_time_to_human_cstring(
                                 svn_x509_certinfo_get_valid_to(certinfo),
                                 scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Issuer: %s\n"),
                             svn_x509_certinfo_get_issuer(certinfo, scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Fingerprint: %s\n"),
                             svn_checksum_to_cstring_display(
                                 svn_x509_certinfo_get_digest(certinfo),
                                 scratch_pool)));

  hostnames = svn_x509_certinfo_get_hostnames(certinfo);
  if (hostnames && !apr_is_empty_array(hostnames))
    {
      int i;
      svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);
      for (i = 0; i < hostnames->nelts; ++i)
        {
          const char *hostname = APR_ARRAY_IDX(hostnames, i, const char*);
          if (i > 0)
            svn_stringbuf_appendbytes(buf, ", ", 2);
          svn_stringbuf_appendbytes(buf, hostname, strlen(hostname));
        }
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Hostnames: %s\n"),
                                 buf->data));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
list_credential(const char *cred_kind,
                const char *realmstring,
                apr_array_header_t *cred_items,
                svn_boolean_t show_passwords,
                svn_x509_certinfo_t *certinfo,
                apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_cmdline_printf(scratch_pool, SEP_STRING));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("Credential kind: %s\n"), cred_kind));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("Authentication realm: %s\n"), realmstring));

  for (i = 0; i < cred_items->nelts; i++)
    {
      svn_sort__item_t item;
      const char *key;
      svn_string_t *value;

      svn_pool_clear(iterpool);
      item = APR_ARRAY_IDX(cred_items, i, svn_sort__item_t);
      key = item.key;
      value = item.value;
      if (strcmp(value->data, realmstring) == 0)
        continue; /* realm string was already shown above */
      else if (strcmp(key, SVN_CONFIG_AUTHN_PASSWORD_KEY) == 0)
        {
          if (show_passwords)
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Password: %s\n"), value->data));
          else
            SVN_ERR(svn_cmdline_printf(iterpool, _("Password: [not shown]\n")));
        }
      else if (strcmp(key, SVN_CONFIG_AUTHN_PASSPHRASE_KEY) == 0)
        {
          if (show_passwords)
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Passphrase: %s\n"), value->data));
          else
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Passphrase: [not shown]\n")));
        }
      else if (strcmp(key, SVN_CONFIG_AUTHN_PASSTYPE_KEY) == 0)
        SVN_ERR(svn_cmdline_printf(iterpool, _("Password cache: %s\n"),
                                   value->data));
      else if (strcmp(key, SVN_CONFIG_AUTHN_USERNAME_KEY) == 0)
        SVN_ERR(svn_cmdline_printf(iterpool, _("Username: %s\n"), value->data));
      else if (strcmp(key, SVN_CONFIG_AUTHN_ASCII_CERT_KEY) == 0)
       SVN_ERR(show_cert(certinfo, value, iterpool));
      else if (strcmp(key, SVN_CONFIG_AUTHN_FAILURES_KEY) == 0)
        SVN_ERR(show_cert_failures(value->data, iterpool));
      else
        SVN_ERR(svn_cmdline_printf(iterpool, "%s: %s\n", key, value->data));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_cmdline_printf(scratch_pool, "\n"));
  return SVN_NO_ERROR;
}

/* This implements `svn_config_auth_walk_func_t` */
static svn_error_t *
walk_credentials(svn_boolean_t *delete_cred,
                 void *baton,
                 const char *cred_kind,
                 const char *realmstring,
                 apr_hash_t *cred_hash,
                 apr_pool_t *scratch_pool)
{
  struct walk_credentials_baton_t *b = baton;
  apr_array_header_t *sorted_cred_items;
  svn_x509_certinfo_t *certinfo = NULL;

  *delete_cred = FALSE;

  sorted_cred_items = svn_sort__hash(cred_hash,
                                     svn_sort_compare_items_lexically,
                                     scratch_pool);
  if (b->patterns->nelts > 0)
    {
      svn_boolean_t match;

      SVN_ERR(match_credential(&match, &certinfo, cred_kind, realmstring,
                               b->patterns, sorted_cred_items,
                               scratch_pool, scratch_pool));
      if (!match)
        return SVN_NO_ERROR;
    }

  b->matches++;

  if (b->list)
    SVN_ERR(list_credential(cred_kind, realmstring, sorted_cred_items,
                            b->show_passwords, certinfo, scratch_pool));
  if (b->delete)
    {
      *delete_cred = TRUE;
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Deleting %s credential for realm '%s'\n"),
                                 cred_kind, realmstring));
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
svn_cl__auth(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  const char *config_path;
  struct walk_credentials_baton_t b;

  b.matches = 0;
  b.show_passwords = opt_state->show_passwords;
  b.list = !opt_state->remove;
  b.delete = opt_state->remove;
  b.patterns = apr_array_make(pool, 1, sizeof(const char *));
  for (; os->ind < os->argc; os->ind++)
    {
      /* The apr_getopt targets are still in native encoding. */
      const char *raw_target = os->argv[os->ind];
      const char *utf8_target;

      SVN_ERR(svn_utf_cstring_to_utf8(&utf8_target,
                                      raw_target, pool));
      APR_ARRAY_PUSH(b.patterns, const char *) = utf8_target;
    }

  SVN_ERR(svn_config_get_user_config_path(&config_path,
                                          opt_state->config_dir, NULL,
                                          pool));

  if (b.delete && b.patterns->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_config_walk_auth_data(config_path, walk_credentials, &b, pool));

  if (b.list)
    {
      if (b.matches == 0)
        {
          if (b.patterns->nelts == 0)
            SVN_ERR(svn_cmdline_printf(pool,
                      _("Credentials cache in '%s' is empty\n"),
                      svn_dirent_local_style(config_path, pool)));
          else
            return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, 0,
                                     _("Credentials cache in '%s' contains "
                                       "no matching credentials"),
                                     svn_dirent_local_style(config_path, pool));
        }
      else
        {
          if (b.patterns->nelts == 0)
            SVN_ERR(svn_cmdline_printf(pool,
                      _("Credentials cache in '%s' contains %d credentials\n"),
                      svn_dirent_local_style(config_path, pool), b.matches));
          else
            SVN_ERR(svn_cmdline_printf(pool,
                      _("Credentials cache in '%s' contains %d matching "
                        "credentials\n"),
                      svn_dirent_local_style(config_path, pool), b.matches));
        }

    }

  if (b.delete)
    {
      if (b.matches == 0)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, 0,
                                 _("Credentials cache in '%s' contains "
                                   "no matching credentials"),
                                 svn_dirent_local_style(config_path, pool));
      else
        SVN_ERR(svn_cmdline_printf(pool, _("Deleted %d matching credentials "
                                   "from '%s'\n"), b.matches,
                                   svn_dirent_local_style(config_path, pool)));
    }

  return SVN_NO_ERROR;
}
