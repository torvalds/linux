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
 * @file svn_opt.h
 * @brief Option and argument parsing for Subversion command lines
 */

#ifndef SVN_OPT_H
#define SVN_OPT_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_getopt.h>
#include <apr_tables.h>
#include <apr_hash.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define APR_WANT_STDIO
#endif
#include <apr_want.h>   /* for FILE* */

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * All subcommand procedures in Subversion conform to this prototype.
 *
 * @a os is the apr option state after getopt processing has been run; in
 * other words, it still contains the non-option arguments following
 * the subcommand.  See @a os->argv and @a os->ind.
 *
 * @a baton is anything you need it to be.
 *
 * @a pool is used for allocating errors, and for any other allocation
 * unless the instance is explicitly documented to allocate from a
 * pool in @a baton.
 */
typedef svn_error_t *(svn_opt_subcommand_t)(
  apr_getopt_t *os, void *baton, apr_pool_t *pool);


/** The maximum number of aliases a subcommand can have. */
#define SVN_OPT_MAX_ALIASES 3

/** The maximum number of options that can be accepted by a subcommand. */
#define SVN_OPT_MAX_OPTIONS 50

/** Options that have no short option char should use an identifying
 * integer equal to or greater than this.
 */
#define SVN_OPT_FIRST_LONGOPT_ID 256


/** One element of a subcommand dispatch table.
 *
 * @since New in 1.4.
 */
typedef struct svn_opt_subcommand_desc2_t
{
  /** The full name of this command. */
  const char *name;

  /** The function this command invokes. */
  svn_opt_subcommand_t *cmd_func;

  /** A list of alias names for this command (e.g., 'up' for 'update'). */
  const char *aliases[SVN_OPT_MAX_ALIASES];

  /** A brief string describing this command, for usage messages. */
  const char *help;

  /** A list of options accepted by this command.  Each value in the
   * array is a unique enum (the 2nd field in apr_getopt_option_t)
   */
  int valid_options[SVN_OPT_MAX_OPTIONS];

  /** A list of option help descriptions, keyed by the option unique enum
   * (the 2nd field in apr_getopt_option_t), which override the generic
   * descriptions given in an apr_getopt_option_t on a per-subcommand basis.
   */
  struct { int optch; const char *desc; } desc_overrides[SVN_OPT_MAX_OPTIONS];
} svn_opt_subcommand_desc2_t;


/** One element of a subcommand dispatch table.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 *
 * Like #svn_opt_subcommand_desc2_t but lacking the @c desc_overrides
 * member.
 */
typedef struct svn_opt_subcommand_desc_t
{
  /** The full name of this command. */
  const char *name;

  /** The function this command invokes. */
  svn_opt_subcommand_t *cmd_func;

  /** A list of alias names for this command (e.g., 'up' for 'update'). */
  const char *aliases[SVN_OPT_MAX_ALIASES];

  /** A brief string describing this command, for usage messages. */
  const char *help;

  /** A list of options accepted by this command.  Each value in the
   * array is a unique enum (the 2nd field in apr_getopt_option_t)
   */
  int valid_options[SVN_OPT_MAX_OPTIONS];

} svn_opt_subcommand_desc_t;


/**
 * Return the entry in @a table whose name matches @a cmd_name, or @c NULL if
 * none.  @a cmd_name may be an alias.
 *
 * @since New in 1.4.
 */
const svn_opt_subcommand_desc2_t *
svn_opt_get_canonical_subcommand2(const svn_opt_subcommand_desc2_t *table,
                                  const char *cmd_name);


/**
 * Return the entry in @a table whose name matches @a cmd_name, or @c NULL if
 * none.  @a cmd_name may be an alias.
 *
 * Same as svn_opt_get_canonical_subcommand2(), but acts on
 * #svn_opt_subcommand_desc_t.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
const svn_opt_subcommand_desc_t *
svn_opt_get_canonical_subcommand(const svn_opt_subcommand_desc_t *table,
                                 const char *cmd_name);


/**
 * Return pointer to an @c apr_getopt_option_t for the option whose
 * option code is @a code, or @c NULL if no match.  @a option_table must end
 * with an element whose every field is zero.  If @a command is non-NULL,
 * then return the subcommand-specific option description instead of the
 * generic one, if a specific description is defined.
 *
 * The returned value may be statically allocated, or allocated in @a pool.
 *
 * @since New in 1.4.
 */
const apr_getopt_option_t *
svn_opt_get_option_from_code2(int code,
                              const apr_getopt_option_t *option_table,
                              const svn_opt_subcommand_desc2_t *command,
                              apr_pool_t *pool);


/**
 * Return the first entry from @a option_table whose option code is @a code,
 * or @c NULL if no match.  @a option_table must end with an element whose
 * every field is zero.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
const apr_getopt_option_t *
svn_opt_get_option_from_code(int code,
                             const apr_getopt_option_t *option_table);


/**
 * Return @c TRUE iff subcommand @a command supports option @a
 * option_code, else return @c FALSE.  If @a global_options is
 * non-NULL, it is a zero-terminated array, and all subcommands take
 * the options listed in it.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_opt_subcommand_takes_option3(const svn_opt_subcommand_desc2_t *command,
                                 int option_code,
                                 const int *global_options);

/**
 * Same as svn_opt_subcommand_takes_option3(), but with @c NULL for @a
 * global_options.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_boolean_t
svn_opt_subcommand_takes_option2(const svn_opt_subcommand_desc2_t *command,
                                 int option_code);


/**
 * Return @c TRUE iff subcommand @a command supports option @a option_code,
 * else return @c FALSE.
 *
 * Same as svn_opt_subcommand_takes_option2(), but acts on
 * #svn_opt_subcommand_desc_t.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_boolean_t
svn_opt_subcommand_takes_option(const svn_opt_subcommand_desc_t *command,
                                int option_code);


/**
 * Print a generic (not command-specific) usage message to @a stream.
 *
 * ### @todo Why is @a stream a stdio file instead of an svn stream?
 *
 * If @a header is non-NULL, print @a header followed by a newline.  Then
 * loop over @a cmd_table printing the usage for each command (getting
 * option usages from @a opt_table).  Then if @a footer is non-NULL, print
 * @a footer followed by a newline.
 *
 * Use @a pool for temporary allocation.
 *
 * @since New in 1.4.
 */
void
svn_opt_print_generic_help2(const char *header,
                            const svn_opt_subcommand_desc2_t *cmd_table,
                            const apr_getopt_option_t *opt_table,
                            const char *footer,
                            apr_pool_t *pool,
                            FILE *stream);


/**
 * Same as svn_opt_print_generic_help2(), but acts on
 * #svn_opt_subcommand_desc_t.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
void
svn_opt_print_generic_help(const char *header,
                           const svn_opt_subcommand_desc_t *cmd_table,
                           const apr_getopt_option_t *opt_table,
                           const char *footer,
                           apr_pool_t *pool,
                           FILE *stream);


/**
 * Print an option @a opt nicely into a @a string allocated in @a pool.
 * If @a doc is set, include the generic documentation string of @a opt,
 * localized to the current locale if a translation is available.
 */
void
svn_opt_format_option(const char **string,
                      const apr_getopt_option_t *opt,
                      svn_boolean_t doc,
                      apr_pool_t *pool);



/**
 * Get @a subcommand's usage from @a table, and print it to @c stdout.
 * Obtain option usage from @a options_table.  If not @c NULL, @a
 * global_options is a zero-terminated list of global options.  Use @a
 * pool for temporary allocation.  @a subcommand may be a canonical
 * command name or an alias.  ### @todo Why does this only print to
 * @c stdout, whereas svn_opt_print_generic_help() gives us a choice?
 *
 * When printing the description of an option, if the same option code
 * appears a second time in @a options_table with a different name, then
 * use that second name as an alias for the first name.  This additional
 * behaviour is new in 1.7.
 *
 * @since New in 1.5.
 */
void
svn_opt_subcommand_help3(const char *subcommand,
                         const svn_opt_subcommand_desc2_t *table,
                         const apr_getopt_option_t *options_table,
                         const int *global_options,
                         apr_pool_t *pool);

/**
 * Same as svn_opt_subcommand_help3(), but with @a global_options
 * always NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
void
svn_opt_subcommand_help2(const char *subcommand,
                         const svn_opt_subcommand_desc2_t *table,
                         const apr_getopt_option_t *options_table,
                         apr_pool_t *pool);


/**
 * Same as svn_opt_subcommand_help2(), but acts on
 * #svn_opt_subcommand_desc_t.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
void
svn_opt_subcommand_help(const char *subcommand,
                        const svn_opt_subcommand_desc_t *table,
                        const apr_getopt_option_t *options_table,
                        apr_pool_t *pool);



/* Parsing revision and date options. */

/**
 * Various ways of specifying revisions.
 *
 * @note
 * In contexts where local mods are relevant, the `working' kind
 * refers to the uncommitted "working" revision, which may be modified
 * with respect to its base revision.  In other contexts, `working'
 * should behave the same as `committed' or `current'.
 */
enum svn_opt_revision_kind {
  /** No revision information given. */
  svn_opt_revision_unspecified,

  /** revision given as number */
  svn_opt_revision_number,

  /** revision given as date */
  svn_opt_revision_date,

  /** rev of most recent change */
  svn_opt_revision_committed,

  /** (rev of most recent change) - 1 */
  svn_opt_revision_previous,

  /** .svn/entries current revision */
  svn_opt_revision_base,

  /** current, plus local mods */
  svn_opt_revision_working,

  /** repository youngest */
  svn_opt_revision_head

  /* please update svn_opt__revision_to_string() when extending this enum */
};

/**
 * A revision value, which can be specified as a number or a date.
 *
 * @note This union was formerly an anonymous inline type in
 * @c svn_opt_revision_t, and was converted to a named type just to
 * make things easier for SWIG.
 *
 * @since New in 1.3.
 */
typedef union svn_opt_revision_value_t
{
  /** The revision number */
  svn_revnum_t number;

  /** the date of the revision */
  apr_time_t date;
} svn_opt_revision_value_t;

/** A revision, specified in one of @c svn_opt_revision_kind ways. */
typedef struct svn_opt_revision_t
{
  enum svn_opt_revision_kind kind;  /**< See svn_opt_revision_kind */
  svn_opt_revision_value_t value;   /**< Extra data qualifying the @c kind */
} svn_opt_revision_t;

/** A revision range, specified in one of @c svn_opt_revision_kind ways. */
typedef struct svn_opt_revision_range_t
{
  /** The first revision in the range */
  svn_opt_revision_t start;

  /** The last revision in the range */
  svn_opt_revision_t end;
} svn_opt_revision_range_t;

/**
 * Set @a *start_revision and/or @a *end_revision according to @a arg,
 * where @a arg is "N" or "N:M", like so:
 *
 *    - If @a arg is "N", set @a *start_revision to represent N, and
 *      leave @a *end_revision untouched.
 *
 *    - If @a arg is "N:M", set @a *start_revision and @a *end_revision
 *      to represent N and M respectively.
 *
 * N and/or M may be one of the special revision descriptors
 * recognized by revision_from_word(), or a date in curly braces.
 *
 * If @a arg is invalid, return -1; else return 0.
 * It is invalid to omit a revision (as in, ":", "N:" or ":M").
 *
 * @note It is typical, though not required, for @a *start_revision and
 * @a *end_revision to be @c svn_opt_revision_unspecified kind on entry.
 *
 * Use @a pool for temporary allocations.
 */
int
svn_opt_parse_revision(svn_opt_revision_t *start_revision,
                       svn_opt_revision_t *end_revision,
                       const char *arg,
                       apr_pool_t *pool);

/**
 * Parse @a arg, where @a arg is "N" or "N:M", into a
 * @c svn_opt_revision_range_t and push that onto @a opt_ranges.
 *
 *    - If @a arg is "N", set the @c start field of the
 *      @c svn_opt_revision_range_t to represent N and the @c end field
 *      to @c svn_opt_revision_unspecified.
 *
 *    - If @a arg is "N:M", set the @c start field of the
 *      @c svn_opt_revision_range_t to represent N and the @c end field
 *      to represent M.
 *
 * If @a arg is invalid, return -1; else return 0.  It is invalid to omit
 * a revision (as in, ":", "N:" or ":M").
 *
 * Use @a pool to allocate @c svn_opt_revision_range_t pushed to the array.
 *
 * @since New in 1.5.
 */
int
svn_opt_parse_revision_to_range(apr_array_header_t *opt_ranges,
                                const char *arg,
                                apr_pool_t *pool);

/**
 * Resolve peg revisions and operational revisions in the following way:
 *
 *    - If @a is_url is set and @a peg_rev->kind is
 *      @c svn_opt_revision_unspecified, @a peg_rev->kind defaults to
 *      @c svn_opt_revision_head.
 *
 *    - If @a is_url is not set, and @a peg_rev->kind is
 *      @c svn_opt_revision_unspecified, @a peg_rev->kind defaults to
 *      @c svn_opt_revision_base.
 *
 *    - If @a op_rev->kind is @c svn_opt_revision_unspecified, @a op_rev
 *      defaults to @a peg_rev.
 *
 * Both @a peg_rev and @a op_rev may be modified as a result of this
 * function.  @a is_url should be set if the path the revisions refer to is
 * a url, and unset otherwise.
 *
 * If @a notice_local_mods is set, @c svn_opt_revision_working is used,
 * instead of @c svn_opt_revision_base.
 *
 * Use @a pool for allocations.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_opt_resolve_revisions(svn_opt_revision_t *peg_rev,
                          svn_opt_revision_t *op_rev,
                          svn_boolean_t is_url,
                          svn_boolean_t notice_local_mods,
                          apr_pool_t *pool);


/* Parsing arguments. */

/**
 * Pull remaining target arguments from @a os into @a *targets_p,
 * converting them to UTF-8, followed by targets from @a known_targets
 * (which might come from, for example, the "--targets" command line
 * option), which are already in UTF-8.
 *
 * On each URL target, do some IRI-to-URI encoding and some
 * auto-escaping.  On each local path, canonicalize case and path
 * separators.
 *
 * Allocate @a *targets_p and its elements in @a pool.
 *
 * If a path has the same name as a Subversion working copy
 * administrative directory, return SVN_ERR_RESERVED_FILENAME_SPECIFIED;
 * if multiple reserved paths are encountered, return a chain of
 * errors, all of which are SVN_ERR_RESERVED_FILENAME_SPECIFIED.  Do
 * not return this type of error in a chain with any other type of
 * error, and if this is the only type of error encountered, complete
 * the operation before returning the error(s).
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 * @see svn_client_args_to_target_array()
 */
SVN_DEPRECATED
svn_error_t *
svn_opt_args_to_target_array3(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              const apr_array_header_t *known_targets,
                              apr_pool_t *pool);

/**
 * This is the same as svn_opt_args_to_target_array3() except that it
 * silently ignores paths that have the same name as a working copy
 * administrative directory.
 *
 * @since New in 1.2.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_opt_args_to_target_array2(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              const apr_array_header_t *known_targets,
                              apr_pool_t *pool);


/**
 * The same as svn_opt_args_to_target_array2() except that, in
 * addition, if @a extract_revisions is set, then look for trailing
 * "@rev" syntax on the first two paths.  If the first target in @a
 * *targets_p ends in "@rev", replace it with a canonicalized version of
 * the part before "@rev" and replace @a *start_revision with the value
 * of "rev".  If the second target in @a *targets_p ends in "@rev",
 * replace it with a canonicalized version of the part before "@rev"
 * and replace @a *end_revision with the value of "rev".  Ignore
 * revision specifiers on any further paths.  "rev" can be any form of
 * single revision specifier, as accepted by svn_opt_parse_revision().
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_opt_args_to_target_array(apr_array_header_t **targets_p,
                             apr_getopt_t *os,
                             const apr_array_header_t *known_targets,
                             svn_opt_revision_t *start_revision,
                             svn_opt_revision_t *end_revision,
                             svn_boolean_t extract_revisions,
                             apr_pool_t *pool);


/**
 * Parse revprop key/value pair from @a revprop_spec (name[=value]) into
 * @a revprops, making copies of both with @a pool.  If @a revprops is
 * @c NULL, allocate a new apr_hash_t in it.  @a revprops maps
 * const char * revprop names to svn_string_t * revprop values for use
 * with svn_repos_get_commit_editor5 and other get_commit_editor APIs.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_opt_parse_revprop(apr_hash_t **revprops, const char *revprop_spec,
                      apr_pool_t *pool);


/**
 * If no targets exist in @a *targets, add `.' as the lone target.
 *
 * (Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args.)
 */
void
svn_opt_push_implicit_dot_target(apr_array_header_t *targets,
                                 apr_pool_t *pool);


/**
 * Parse @a num_args non-target arguments from the list of arguments in
 * @a os->argv, return them as <tt>const char *</tt> in @a *args_p, without
 * doing any UTF-8 conversion.  Allocate @a *args_p and its values in @a pool.
 */
svn_error_t *
svn_opt_parse_num_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       int num_args,
                       apr_pool_t *pool);


/**
 * Parse all remaining arguments from @a os->argv, return them as
 * <tt>const char *</tt> in @a *args_p, without doing any UTF-8 conversion.
 * Allocate @a *args_p and its values in @a pool.
 */
svn_error_t *
svn_opt_parse_all_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       apr_pool_t *pool);

/**
 * Parse a working-copy path or URL in @a path, extracting any trailing
 * revision specifier of the form "@rev" from the last component of
 * the path.
 *
 * Some examples would be:
 *
 *   - "foo/bar"                      -> "foo/bar",       (unspecified)
 *   - "foo/bar@13"                   -> "foo/bar",       (number, 13)
 *   - "foo/bar@HEAD"                 -> "foo/bar",       (head)
 *   - "foo/bar@{1999-12-31}"         -> "foo/bar",       (date, 1999-12-31)
 *   - "http://a/b@27"                -> "http://a/b",    (number, 27)
 *   - "http://a/b@COMMITTED"         -> "http://a/b",    (committed) [*]
 *   - "http://a/b@{1999-12-31}"      -> "http://a/b",    (date, 1999-12-31)
 *   - "http://a/b@%7B1999-12-31%7D"  -> "http://a/b",    (date, 1999-12-31)
 *   - "foo/bar@1:2"                  -> error
 *   - "foo/bar@baz"                  -> error
 *   - "foo/bar@"                     -> "foo/bar",       (unspecified)
 *   - "foo/@bar@"                    -> "foo/@bar",      (unspecified)
 *   - "foo/bar/@13"                  -> "foo/bar/",      (number, 13)
 *   - "foo/bar@@13"                  -> "foo/bar@",      (number, 13)
 *   - "foo/@bar@HEAD"                -> "foo/@bar",      (head)
 *   - "foo@/bar"                     -> "foo@/bar",      (unspecified)
 *   - "foo@HEAD/bar"                 -> "foo@HEAD/bar",  (unspecified)
 *   - "@foo/bar"                     -> "@foo/bar",      (unspecified)
 *   - "@foo/bar@"                    -> "@foo/bar",      (unspecified)
 *
 *   [*] Syntactically valid but probably not semantically useful.
 *
 * If a trailing revision specifier is found, parse it into @a *rev and
 * put the rest of the path into @a *truepath, allocating from @a pool;
 * or return an @c SVN_ERR_CL_ARG_PARSING_ERROR (with the effect on
 * @a *truepath undefined) if the revision specifier is invalid.
 * If no trailing revision specifier is found, set @a *truepath to
 * @a path and @a rev->kind to @c svn_opt_revision_unspecified.
 *
 * This function does not require that @a path be in canonical form.
 * No canonicalization is done and @a *truepath will only be in
 * canonical form if @a path is in canonical form.
 *
 * @since New in 1.1.
 * @since Since 1.6.5, this returns an error if @a path contains a peg
 * specifier with no path before it, such as "@abc".
 * @since Since 1.9.0, this no longer returns an error if @a path contains a peg
 * specifier with no path before it, such as "@abc".
 */
svn_error_t *
svn_opt_parse_path(svn_opt_revision_t *rev,
                   const char **truepath,
                   const char *path,
                   apr_pool_t *pool);

/**
 * Central dispatcher function for various kinds of help message.
 * Prints one of:
 *   * subcommand-specific help (svn_opt_subcommand_help)
 *   * generic help (svn_opt_print_generic_help)
 *   * version info
 *   * simple usage complaint: "Type '@a pgm_name help' for usage."
 *
 * If @a os is not @c NULL and it contains arguments, then try
 * printing help for them as though they are subcommands, using @a
 * cmd_table and @a option_table for option information.  If not @c
 * NULL, @a global_options is a zero-terminated array of options taken
 * by all subcommands.
 *
 * Else, if @a print_version is TRUE, then print version info, in
 * brief form if @a quiet is also TRUE; if @a quiet is FALSE, then if
 * @a version_footer is non-NULL, print it following the version
 * information. If @a verbose is TRUE, also print information about
 * the running system and loaded shared libraries, where available.
 *
 * Else, if @a os is not @c NULL and does not contain arguments, print
 * generic help, via svn_opt_print_generic_help2() with the @a header,
 * @a cmd_table, @a option_table, and @a footer arguments.
 *
 * Else, when @a os is @c NULL, print the simple usage complaint.
 *
 * Use @a pool for temporary allocations.
 *
 * Notes: The reason this function handles both version printing and
 * general usage help is that a confused user might put both the
 * --version flag *and* subcommand arguments on a help command line.
 * The logic for handling such a situation should be in one place.
 *
 * @since New in 1.8.
 */

svn_error_t *
svn_opt_print_help4(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    svn_boolean_t verbose,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc2_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const int *global_options,
                    const char *footer,
                    apr_pool_t *pool);

/**
 * Same as svn_opt_print_help4(), but with @a verbose always @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */

SVN_DEPRECATED
svn_error_t *
svn_opt_print_help3(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc2_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const int *global_options,
                    const char *footer,
                    apr_pool_t *pool);

/**
 * Same as svn_opt_print_help3(), but with @a global_options always @c
 * NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */

SVN_DEPRECATED
svn_error_t *
svn_opt_print_help2(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc2_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const char *footer,
                    apr_pool_t *pool);


/**
 * Same as svn_opt_print_help2(), but acts on #svn_opt_subcommand_desc_t.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_opt_print_help(apr_getopt_t *os,
                   const char *pgm_name,
                   svn_boolean_t print_version,
                   svn_boolean_t quiet,
                   const char *version_footer,
                   const char *header,
                   const svn_opt_subcommand_desc_t *cmd_table,
                   const apr_getopt_option_t *option_table,
                   const char *footer,
                   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_OPT_H */
