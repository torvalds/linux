/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_cmdline_private.h
 * @brief Private functions for Subversion cmdline.
 */

#ifndef SVN_CMDLINE_PRIVATE_H
#define SVN_CMDLINE_PRIVATE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Write a property as an XML element into @a *outstr.
 *
 * If @a outstr is NULL, allocate @a *outstr in @a pool; else append to
 * @a *outstr, allocating in @a outstr's pool
 *
 * @a propname is the property name. @a propval is the property value, which
 * will be encoded if it contains unsafe bytes.
 *
 * If @a inherited_prop is TRUE then @a propname is an inherited property,
 * otherwise @a propname is an explicit property.
 */
void
svn_cmdline__print_xml_prop(svn_stringbuf_t **outstr,
                            const char *propname,
                            svn_string_t *propval,
                            svn_boolean_t inherited_prop,
                            apr_pool_t *pool);


/** An implementation of @c svn_auth_gnome_keyring_unlock_prompt_func_t that
 * prompts the user for default GNOME Keyring password.
 *
 * Expects a @c svn_cmdline_prompt_baton2_t to be passed as @a baton.
 *
 * @since New in 1.6.
 * @deprecated Only used by old libgome-keyring implementation.
 */
svn_error_t *
svn_cmdline__auth_gnome_keyring_unlock_prompt(char **keyring_password,
                                              const char *keyring_name,
                                              void *baton,
                                              apr_pool_t *pool);

/** Container for config options parsed with svn_cmdline__parse_config_option
 *
 * @since New in 1.7.
 */
typedef struct svn_cmdline__config_argument_t
{
  const char *file;
  const char *section;
  const char *option;
  const char *value;
} svn_cmdline__config_argument_t;

/** Parser for 'FILE:SECTION:OPTION=[VALUE]'-style option arguments.
 *
 * Parses @a opt_arg and places its value in @a config_options, an apr array
 * containing svn_cmdline__config_argument_t* elements, allocating the option
 * data in @a pool
 *
 * [Since 1.9:] If the file, section, or option value is not recognized,
 * warn to @c stderr, using @a prefix as in svn_handle_warning2().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cmdline__parse_config_option(apr_array_header_t *config_options,
                                 const char *opt_arg,
                                 const char *prefix,
                                 apr_pool_t *pool);

/** Sets the config options in @a config_options, an apr array containing
 * @c svn_cmdline__config_argument_t* elements, to the configuration in @a cfg,
 * a hash mapping of <tt>const char *</tt> configuration file names to
 * @c svn_config_t *'s. Write warnings to stderr.
 *
 * Use @a prefix as prefix and @a argument_name in warning messages.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cmdline__apply_config_options(apr_hash_t *config,
                                  const apr_array_header_t *config_options,
                                  const char *prefix,
                                  const char *argument_name);

/* Return a string allocated in POOL that is a copy of STR but with each
 * line prefixed with INDENT. A line is all characters up to the first
 * CR-LF, LF-CR, CR or LF, or the end of STR if sooner. */
const char *
svn_cmdline__indent_string(const char *str,
                           const char *indent,
                           apr_pool_t *pool);

/* Print to stdout a hash PROP_HASH that maps property names (char *) to
   property values (svn_string_t *).  The names are assumed to be in UTF-8
   format; the values are either in UTF-8 (the special Subversion props) or
   plain binary values.

   If OUT is not NULL, then write to it rather than stdout.

   If NAMES_ONLY is true, print just names, else print names and
   values. */
svn_error_t *
svn_cmdline__print_prop_hash(svn_stream_t *out,
                             apr_hash_t *prop_hash,
                             svn_boolean_t names_only,
                             apr_pool_t *pool);

/* Similar to svn_cmdline__print_prop_hash(), only output xml to *OUTSTR.
   If INHERITED_PROPS is true, then PROP_HASH contains inherited properties,
   otherwise PROP_HASH contains explicit properties.  If *OUTSTR is NULL,
   allocate it first from POOL, otherwise append to it. */
svn_error_t *
svn_cmdline__print_xml_prop_hash(svn_stringbuf_t **outstr,
                                 apr_hash_t *prop_hash,
                                 svn_boolean_t names_only,
                                 svn_boolean_t inherited_props,
                                 apr_pool_t *pool);


/* Search for a text editor command in standard environment variables,
   and invoke it to edit PATH.  Use POOL for all allocations.

   If EDITOR_CMD is not NULL, it is the name of the external editor
   command to use, overriding anything else that might determine the
   editor.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.  */
svn_error_t *
svn_cmdline__edit_file_externally(const char *path,
                                  const char *editor_cmd,
                                  apr_hash_t *config,
                                  apr_pool_t *pool);

/* Search for a text editor command in standard environment variables,
   and invoke it to edit CONTENTS (using a temporary file created in
   directory BASE_DIR).  Return the new contents in *EDITED_CONTENTS,
   or set *EDITED_CONTENTS to NULL if no edit was performed.

   If EDITOR_CMD is not NULL, it is the name of the external editor
   command to use, overriding anything else that might determine the
   editor.

   If TMPFILE_LEFT is NULL, the temporary file will be destroyed.
   Else, the file will be left on disk, and its path returned in
   *TMPFILE_LEFT.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.

   If AS_TEXT is TRUE, recode CONTENTS and convert to native eol-style before
   editing and back again afterwards.  In this case, ENCODING determines the
   encoding used during editing.  If non-NULL, use the named encoding, else
   use the system encoding.  If AS_TEXT is FALSE, don't do any translation.
   In that case, ENCODING is ignored.

   Use POOL for all allocations.  Use PREFIX as the prefix for the
   temporary file used by the editor.

   If return error, *EDITED_CONTENTS is not touched. */
svn_error_t *
svn_cmdline__edit_string_externally(svn_string_t **edited_contents,
                                    const char **tmpfile_left,
                                    const char *editor_cmd,
                                    const char *base_dir,
                                    const svn_string_t *contents,
                                    const char *prefix,
                                    apr_hash_t *config,
                                    svn_boolean_t as_text,
                                    const char *encoding,
                                    apr_pool_t *pool);


/** Wrapper for apr_getopt_init(), which see.
 *
 * @since New in 1.4.
 */
svn_error_t *
svn_cmdline__getopt_init(apr_getopt_t **os,
                         int argc,
                         const char *argv[],
                         apr_pool_t *pool);

/*  */
svn_boolean_t
svn_cmdline__stdin_is_a_terminal(void);

/*  */
svn_boolean_t
svn_cmdline__stdout_is_a_terminal(void);

/*  */
svn_boolean_t
svn_cmdline__stderr_is_a_terminal(void);

/* Determine whether interactive mode should be enabled, based on whether
 * the user passed the --non-interactive or --force-interactive options.
 * If neither option was passed, interactivity is enabled if standard
 * input is connected to a terminal device.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_cmdline__be_interactive(svn_boolean_t non_interactive,
                            svn_boolean_t force_interactive);

/* Parses the argument value of '--trust-server-cert-failures' OPT_ARG into
 * the expected booleans for passing to svn_cmdline_create_auth_baton2()
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_cmdline__parse_trust_options(
                        svn_boolean_t *trust_server_cert_unknown_ca,
                        svn_boolean_t *trust_server_cert_cn_mismatch,
                        svn_boolean_t *trust_server_cert_expired,
                        svn_boolean_t *trust_server_cert_not_yet_valid,
                        svn_boolean_t *trust_server_cert_other_failure,
                        const char *opt_arg,
                        apr_pool_t *scratch_pool);

/* Setup signal handlers for signals such as SIGINT and return a
   cancellation handler function.  This also sets some other signals
   to be ignored. */
svn_cancel_func_t
svn_cmdline__setup_cancellation_handler(void);

/* Set the handlers for signals such as SIGINT back to default. */
void
svn_cmdline__disable_cancellation_handler(void);

/* Exit this process with a status that indicates the cancellation
   signal, or return without exiting if there is no signal.  This
   allows the shell to use WIFSIGNALED and WTERMSIG to detect the
   signal.  See http://www.cons.org/cracauer/sigint.html */
void
svn_cmdline__cancellation_exit(void);

/** Reads a string from stdin until a newline or EOF is found
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_cmdline__stdin_readline(const char **result,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CMDLINE_PRIVATE_H */
