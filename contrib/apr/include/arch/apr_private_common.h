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

/*
 * This file contains private declarations common to all architectures.
 */

#ifndef APR_PRIVATE_COMMON_H
#define APR_PRIVATE_COMMON_H

#include "apr_pools.h"
#include "apr_tables.h"

apr_status_t apr_filepath_list_split_impl(apr_array_header_t **pathelts,
                                          const char *liststr,
                                          char separator,
                                          apr_pool_t *p);

apr_status_t apr_filepath_list_merge_impl(char **liststr,
                                          apr_array_header_t *pathelts,
                                          char separator,
                                          apr_pool_t *p);

/* temporary defines to handle 64bit compile mismatches */
#define APR_INT_TRUNC_CAST    int
#define APR_UINT32_TRUNC_CAST apr_uint32_t

#endif  /*APR_PRIVATE_COMMON_H*/
