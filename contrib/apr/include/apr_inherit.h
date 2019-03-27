/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_INHERIT_H
#define APR_INHERIT_H

/**
 * @file apr_inherit.h 
 * @brief APR File Handle Inheritance Helpers
 * @remark This internal header includes internal declaration helpers 
 * for other headers to declare apr_foo_inherit_[un]set functions.
 */

/**
 * Prototype for type-specific declarations of apr_foo_inherit_set 
 * functions.  
 * @remark Doxygen unwraps this macro (via doxygen.conf) to provide 
 * actual help for each specific occurrence of apr_foo_inherit_set.
 * @remark the linkage is specified for APR. It would be possible to expand
 *       the macros to support other linkages.
 */
#define APR_DECLARE_INHERIT_SET(type) \
    APR_DECLARE(apr_status_t) apr_##type##_inherit_set( \
                                          apr_##type##_t *the##type)

/**
 * Prototype for type-specific declarations of apr_foo_inherit_unset 
 * functions.  
 * @remark Doxygen unwraps this macro (via doxygen.conf) to provide 
 * actual help for each specific occurrence of apr_foo_inherit_unset.
 * @remark the linkage is specified for APR. It would be possible to expand
 *       the macros to support other linkages.
 */
#define APR_DECLARE_INHERIT_UNSET(type) \
    APR_DECLARE(apr_status_t) apr_##type##_inherit_unset( \
                                          apr_##type##_t *the##type)

#endif	/* ! APR_INHERIT_H */
