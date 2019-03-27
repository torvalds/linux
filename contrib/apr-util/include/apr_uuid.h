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

/**
 * @file apr_uuid.h
 * @brief APR UUID library
 */
#ifndef APR_UUID_H
#define APR_UUID_H

#include "apu.h"
#include "apr_errno.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup APR_UUID UUID Handling
 * @ingroup APR
 * @{
 */

/**
 * we represent a UUID as a block of 16 bytes.
 */

typedef struct {
    unsigned char data[16]; /**< the actual UUID */
} apr_uuid_t;

/** UUIDs are formatted as: 00112233-4455-6677-8899-AABBCCDDEEFF */
#define APR_UUID_FORMATTED_LENGTH 36


/**
 * Generate and return a (new) UUID
 * @param uuid The resulting UUID
 */ 
APU_DECLARE(void) apr_uuid_get(apr_uuid_t *uuid);

/**
 * Format a UUID into a string, following the standard format
 * @param buffer The buffer to place the formatted UUID string into. It must
 *               be at least APR_UUID_FORMATTED_LENGTH + 1 bytes long to hold
 *               the formatted UUID and a null terminator
 * @param uuid The UUID to format
 */ 
APU_DECLARE(void) apr_uuid_format(char *buffer, const apr_uuid_t *uuid);

/**
 * Parse a standard-format string into a UUID
 * @param uuid The resulting UUID
 * @param uuid_str The formatted UUID
 */ 
APU_DECLARE(apr_status_t) apr_uuid_parse(apr_uuid_t *uuid, const char *uuid_str);

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* APR_UUID_H */
