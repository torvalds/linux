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
 * FILE:        sha2.h
 * AUTHOR:      Aaron D. Gifford <me@aarongifford.com>
 * 
 * A licence was granted to the ASF by Aaron on 4 November 2003.
 */

#ifndef __SHA2_H__
#define __SHA2_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "apr.h"

/*** SHA-256 Various Length Definitions ***********************/
#define SHA256_BLOCK_LENGTH             64
#define SHA256_DIGEST_LENGTH            32
#define SHA256_DIGEST_STRING_LENGTH     (SHA256_DIGEST_LENGTH * 2 + 1)


/*** SHA-256/384/512 Context Structures *******************************/
typedef struct _SHA256_CTX {
        apr_uint32_t    state[8];
        apr_uint64_t    bitcount;
        apr_byte_t      buffer[SHA256_BLOCK_LENGTH];
} SHA256_CTX;


/*** SHA-256/384/512 Function Prototypes ******************************/
void apr__SHA256_Init(SHA256_CTX *);
void apr__SHA256_Update(SHA256_CTX *, const apr_byte_t *, size_t);
void apr__SHA256_Final(apr_byte_t [SHA256_DIGEST_LENGTH], SHA256_CTX *);
char* apr__SHA256_End(SHA256_CTX *, char [SHA256_DIGEST_STRING_LENGTH]);
char* apr__SHA256_Data(const apr_byte_t *, size_t,
                  char [SHA256_DIGEST_STRING_LENGTH]);
                  
#ifdef  __cplusplus
}
#endif /* __cplusplus */

#endif /* __SHA2_H__ */

