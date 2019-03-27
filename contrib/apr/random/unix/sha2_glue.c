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

#include <apr.h>
#include <apr_random.h>
#include <apr_pools.h>
#include "sha2.h"

static void sha256_init(apr_crypto_hash_t *h)
{
    apr__SHA256_Init(h->data);
}

static void sha256_add(apr_crypto_hash_t *h,const void *data,
                       apr_size_t bytes)
{
    apr__SHA256_Update(h->data,data,bytes);
}

static void sha256_finish(apr_crypto_hash_t *h,unsigned char *result)
{
    apr__SHA256_Final(result,h->data);
}

APR_DECLARE(apr_crypto_hash_t *) apr_crypto_sha256_new(apr_pool_t *p)
{
    apr_crypto_hash_t *h=apr_palloc(p,sizeof *h);

    h->data=apr_palloc(p,sizeof(SHA256_CTX));
    h->init=sha256_init;
    h->add=sha256_add;
    h->finish=sha256_finish;
    h->size=256/8;

    return h;
}
