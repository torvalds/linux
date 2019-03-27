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

#include "apr_version.h"
#include "apr_general.h" /* for APR_STRINGIFY */

APR_DECLARE(void) apr_version(apr_version_t *pvsn)
{
    pvsn->major = APR_MAJOR_VERSION;
    pvsn->minor = APR_MINOR_VERSION;
    pvsn->patch = APR_PATCH_VERSION;
#ifdef APR_IS_DEV_VERSION
    pvsn->is_dev = 1;
#else
    pvsn->is_dev = 0;
#endif
}

APR_DECLARE(const char *) apr_version_string(void)
{
    return APR_VERSION_STRING;
}
