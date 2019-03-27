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

/* common .c
 * This file has any function that is truly common and platform
 * neutral.  Or at least that's the theory.
 * 
 * The header files are a problem so there are a few #ifdef's to take
 * care of those.
 *
 */

#include "apr.h"
#include "apr_private.h"
#include "apr_mmap.h"
#include "apr_errno.h"

#if APR_HAS_MMAP || defined(BEOS)

APR_DECLARE(apr_status_t) apr_mmap_offset(void **addr, apr_mmap_t *mmap,
                                          apr_off_t offset)
{
    if (offset < 0 || (apr_size_t)offset > mmap->size)
        return APR_EINVAL;
    
    (*addr) = (char *) mmap->mm + offset;
    return APR_SUCCESS;
}

#endif
