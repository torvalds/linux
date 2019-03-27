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
 * See the paper "On Randomness" by Ben Laurie for an explanation of this PRNG.
 * http://www.apache-ssl.org/randomness.pdf
 * XXX: Is there a formal proof of this PRNG? Couldn't we use the more popular
 * Mersenne Twister PRNG (and BSD licensed)?
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_random.h"
#include "apr_thread_proc.h"
#include <assert.h>

#ifdef min
#undef min
#endif
#define min(a,b) ((a) < (b) ? (a) : (b))

#define APR_RANDOM_DEFAULT_POOLS 32
#define APR_RANDOM_DEFAULT_REHASH_SIZE 1024
#define APR_RANDOM_DEFAULT_RESEED_SIZE 32
#define APR_RANDOM_DEFAULT_HASH_SECRET_SIZE 32
#define APR_RANDOM_DEFAULT_G_FOR_INSECURE 32
#define APR_RANDOM_DEFAULT_G_FOR_SECURE 320

typedef struct apr_random_pool_t {
    unsigned char *pool;
    unsigned int bytes;
    unsigned int pool_size;
} apr_random_pool_t;

#define hash_init(h)            (h)->init(h)
#define hash_add(h,b,n)         (h)->add(h,b,n)
#define hash_finish(h,r)        (h)->finish(h,r)

#define hash(h,r,b,n)           hash_init(h),hash_add(h,b,n),hash_finish(h,r)

#define crypt_setkey(c,k)       (c)->set_key((c)->data,k)
#define crypt_crypt(c,out,in)   (c)->crypt((c)->date,out,in)

struct apr_random_t {
    apr_pool_t *apr_pool;
    apr_crypto_hash_t *pool_hash;
    unsigned int npools;
    apr_random_pool_t *pools;
    unsigned int next_pool;
    unsigned int generation;
    apr_size_t rehash_size;
    apr_size_t reseed_size;
    apr_crypto_hash_t *key_hash;
#define K_size(g) ((g)->key_hash->size)
    apr_crypto_hash_t *prng_hash;
#define B_size(g) ((g)->prng_hash->size)

    unsigned char *H;
    unsigned char *H_waiting;
#define H_size(g) (B_size(g)+K_size(g))
#define H_current(g) (((g)->insecure_started && !(g)->secure_started) \
                      ? (g)->H_waiting : (g)->H)

    unsigned char *randomness;
    apr_size_t random_bytes;
    unsigned int g_for_insecure;
    unsigned int g_for_secure;
    unsigned int secure_base;
    unsigned int insecure_started:1;
    unsigned int secure_started:1;

    apr_random_t *next;
};

static apr_random_t *all_random;

static apr_status_t random_cleanup(void *data)
{
    apr_random_t *remove_this = data,
                 *cur = all_random,
                 **prev_ptr = &all_random;
    while (cur) {
        if (cur == remove_this) {
            *prev_ptr = cur->next;
            break;
        }
        prev_ptr = &cur->next;
        cur = cur->next;
    }
    return APR_SUCCESS;
}


APR_DECLARE(void) apr_random_init(apr_random_t *g,apr_pool_t *p,
                                  apr_crypto_hash_t *pool_hash,
                                  apr_crypto_hash_t *key_hash,
                                  apr_crypto_hash_t *prng_hash)
{
    unsigned int n;

    g->apr_pool = p;

    g->pool_hash = pool_hash;
    g->key_hash = key_hash;
    g->prng_hash = prng_hash;

    g->npools = APR_RANDOM_DEFAULT_POOLS;
    g->pools = apr_palloc(p,g->npools*sizeof *g->pools);
    for (n = 0; n < g->npools; ++n) {
        g->pools[n].bytes = g->pools[n].pool_size = 0;
        g->pools[n].pool = NULL;
    }
    g->next_pool = 0;

    g->generation = 0;

    g->rehash_size = APR_RANDOM_DEFAULT_REHASH_SIZE;
    /* Ensure that the rehash size is twice the size of the pool hasher */
    g->rehash_size = ((g->rehash_size+2*g->pool_hash->size-1)/g->pool_hash->size
                    /2)*g->pool_hash->size*2;
    g->reseed_size = APR_RANDOM_DEFAULT_RESEED_SIZE;

    g->H = apr_pcalloc(p,H_size(g));
    g->H_waiting = apr_pcalloc(p,H_size(g));

    g->randomness = apr_palloc(p,B_size(g));
    g->random_bytes = 0;

    g->g_for_insecure = APR_RANDOM_DEFAULT_G_FOR_INSECURE;
    g->secure_base = 0;
    g->g_for_secure = APR_RANDOM_DEFAULT_G_FOR_SECURE;
    g->secure_started = g->insecure_started = 0;

    g->next = all_random;
    all_random = g;
    apr_pool_cleanup_register(p, g, random_cleanup, apr_pool_cleanup_null);
}

static void mix_pid(apr_random_t *g,unsigned char *H,pid_t pid)
{
    hash_init(g->key_hash);
    hash_add(g->key_hash,H,H_size(g));
    hash_add(g->key_hash,&pid,sizeof pid);
    hash_finish(g->key_hash,H);
}

static void mixer(apr_random_t *g,pid_t pid)
{
    unsigned char *H = H_current(g);

    /* mix the PID into the current H */
    mix_pid(g,H,pid);
    /* if we are in waiting, then also mix into main H */
    if (H != g->H)
        mix_pid(g,g->H,pid);
    /* change order of pool mixing for good measure - note that going
       backwards is much better than going forwards */
    --g->generation;
    /* blow away any lingering randomness */
    g->random_bytes = 0;
}

APR_DECLARE(void) apr_random_after_fork(apr_proc_t *proc)
{
    apr_random_t *r;

    for (r = all_random; r; r = r->next)
        /* 
         * XXX Note: the pid does not provide sufficient entropy to 
         * actually call this secure.  See Ben's paper referenced at 
         * the top of this file. 
         */
        mixer(r,proc->pid);
}

APR_DECLARE(apr_random_t *) apr_random_standard_new(apr_pool_t *p)
{
    apr_random_t *r = apr_palloc(p,sizeof *r);
    
    apr_random_init(r,p,apr_crypto_sha256_new(p),apr_crypto_sha256_new(p),
                    apr_crypto_sha256_new(p));
    return r;
}

static void rekey(apr_random_t *g)
{
    unsigned int n;
    unsigned char *H = H_current(g);

    hash_init(g->key_hash);
    hash_add(g->key_hash,H,H_size(g));
    for (n = 0 ; n < g->npools && (n == 0 || g->generation&(1 << (n-1)))
            ; ++n) {
        hash_add(g->key_hash,g->pools[n].pool,g->pools[n].bytes);
        g->pools[n].bytes = 0;
    }
    hash_finish(g->key_hash,H+B_size(g));

    ++g->generation;
    if (!g->insecure_started && g->generation > g->g_for_insecure) {
        g->insecure_started = 1;
        if (!g->secure_started) {
            memcpy(g->H_waiting,g->H,H_size(g));
            g->secure_base = g->generation;
        }
    }

    if (!g->secure_started && g->generation > g->secure_base+g->g_for_secure) {
        g->secure_started = 1;
        memcpy(g->H,g->H_waiting,H_size(g));
    }
}

APR_DECLARE(void) apr_random_add_entropy(apr_random_t *g,const void *entropy_,
                                         apr_size_t bytes)
{
    unsigned int n;
    const unsigned char *entropy = entropy_;

    for (n = 0; n < bytes; ++n) {
        apr_random_pool_t *p = &g->pools[g->next_pool];

        if (++g->next_pool == g->npools)
            g->next_pool = 0;

        if (p->pool_size < p->bytes+1) {
            unsigned char *np = apr_palloc(g->apr_pool,(p->bytes+1)*2);

            memcpy(np,p->pool,p->bytes);
            p->pool = np;
            p->pool_size = (p->bytes+1)*2;
        }
        p->pool[p->bytes++] = entropy[n];

        if (p->bytes == g->rehash_size) {
            apr_size_t r;

            for (r = 0; r < p->bytes/2; r+=g->pool_hash->size)
                hash(g->pool_hash,p->pool+r,p->pool+r*2,g->pool_hash->size*2);
            p->bytes/=2;
        }
        assert(p->bytes < g->rehash_size);
    }

    if (g->pools[0].bytes >= g->reseed_size)
        rekey(g);
}

/* This will give g->B_size bytes of randomness */
static void apr_random_block(apr_random_t *g,unsigned char *random)
{
    /* FIXME: in principle, these are different hashes */
    hash(g->prng_hash,g->H,g->H,H_size(g));
    hash(g->prng_hash,random,g->H,B_size(g));
}

static void apr_random_bytes(apr_random_t *g,unsigned char *random,
                             apr_size_t bytes)
{
    apr_size_t n;

    for (n = 0; n < bytes; ) {
        apr_size_t l;

        if (g->random_bytes == 0) {
            apr_random_block(g,g->randomness);
            g->random_bytes = B_size(g);
        }
        l = min(bytes-n,g->random_bytes);
        memcpy(&random[n],g->randomness+B_size(g)-g->random_bytes,l);
        g->random_bytes-=l;
        n+=l;
    }
}

APR_DECLARE(apr_status_t) apr_random_secure_bytes(apr_random_t *g,
                                                  void *random,
                                                  apr_size_t bytes)
{
    if (!g->secure_started)
        return APR_ENOTENOUGHENTROPY;
    apr_random_bytes(g,random,bytes);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_random_insecure_bytes(apr_random_t *g,
                                                    void *random,
                                                    apr_size_t bytes)
{
    if (!g->insecure_started)
        return APR_ENOTENOUGHENTROPY;
    apr_random_bytes(g,random,bytes);
    return APR_SUCCESS;
}

APR_DECLARE(void) apr_random_barrier(apr_random_t *g)
{
    g->secure_started = 0;
    g->secure_base = g->generation;
}

APR_DECLARE(apr_status_t) apr_random_secure_ready(apr_random_t *r)
{
    if (!r->secure_started)
        return APR_ENOTENOUGHENTROPY;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_random_insecure_ready(apr_random_t *r)
{
    if (!r->insecure_started)
        return APR_ENOTENOUGHENTROPY;
    return APR_SUCCESS;
}
