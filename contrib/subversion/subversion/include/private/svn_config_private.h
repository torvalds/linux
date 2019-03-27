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
 * @file svn_config_private.h
 * @brief Private config file parsing API.
 */

#ifndef SVN_CONFIG_PRIVATE_H
#define SVN_CONFIG_PRIVATE_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_io.h"
#include "svn_string.h"
#include "svn_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Description of a constructor for in-memory config file
 * representations.
 */
typedef struct svn_config__constructor_t svn_config__constructor_t;

/*
 * Constructor callback: called when the parsing of a new SECTION
 * begins. If the implementation stores the value of SECTION, it
 * must copy it into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__open_section_fn)(
    void *baton, svn_stringbuf_t *section);

/*
 * Constructor callback: called when the parsing of SECTION ends. If
 * the implementation stores the value of SECTION, it must copy it
 * into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__close_section_fn)(
    void *baton, svn_stringbuf_t *section);

/*
 * Constructor callback: called OPTION with VALUE in SECTION was
 * parsed. If the implementation stores the values of SECTION, OPTION
 * or VALUE, it must copy them into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__add_value_fn)(
    void *baton, svn_stringbuf_t *section,
    svn_stringbuf_t *option, svn_stringbuf_t *value);


/*
 * Create a new constuctor allocated from RESULT_POOL.
 * Any of the callback functions may be NULL.
 * The constructor implementation is responsible for implementing any
 * case-insensitivity, value expansion, or other features on top of
 * the basic parser.
 */
svn_config__constructor_t *
svn_config__constructor_create(
    svn_config__open_section_fn open_section_callback,
    svn_config__close_section_fn close_section_callback,
    svn_config__add_value_fn add_value_callback,
    apr_pool_t *result_pool);

/*
 * Parse the configuration from STREAM, using CONSTRUCTOR to build the
 * in-memory representation of the parsed configuration.
 * CONSTRUCTOR_BATON is passed unchanged to the constructor
 * callbacks. The parser guarantees that sections and options will be
 * passed to the callback in the same order as they're defined in
 * STREAM.
 *
 * The lifetome of section names, option names and values passed to
 * the constructor does not extend past the invocation of each
 * callback; see calback docs, above.
 *
 * The parser will use SCRATCH_POOL for its own allocations.
 */
svn_error_t *
svn_config__parse_stream(svn_stream_t *stream,
                         svn_config__constructor_t *constructor,
                         void *constructor_baton,
                         apr_pool_t *scratch_pool);

/*
 * Write the configuration CFG to STREAM, using SCRATCH_POOL for
 * temporary allocations.
 *
 * Note that option values will not be expanded and that the order
 * of sections as well as the options within them is undefined.
 */
svn_error_t *
svn_config__write(svn_stream_t *stream,
                  const svn_config_t *cfg,
                  apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CONFIG_PRIVATE_H */
