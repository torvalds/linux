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
 * This attempts to generate V1 UUIDs according to the Internet Draft
 * located at http://www.webdav.org/specs/draft-leach-uuids-guids-01.txt
 */
#include "apr.h"
#include "apr_uuid.h"
#include "apr_md5.h"
#include "apr_general.h"
#include "apr_portable.h"


#if APR_HAVE_UNISTD_H
#include <unistd.h>     /* for getpid, gethostname */
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h>     /* for rand, srand */
#endif


#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_STRINGS_H
#include <strings.h>
#endif
#if APR_HAVE_NETDB_H
#include <netdb.h>
#endif
#if APR_HAVE_SYS_TIME_H
#include <sys/time.h>   /* for gettimeofday */
#endif

#define NODE_LENGTH 6

static int uuid_state_seqnum;
static unsigned char uuid_state_node[NODE_LENGTH] = { 0 };


static void get_random_info(unsigned char node[NODE_LENGTH])
{
#if APR_HAS_RANDOM

    (void) apr_generate_random_bytes(node, NODE_LENGTH);

#else

    unsigned char seed[APR_MD5_DIGESTSIZE];
    apr_md5_ctx_t c;

    /* ### probably should revise some of this to be a bit more portable */

    /* Leach & Salz use Linux-specific struct sysinfo;
     * replace with pid/tid for portability (in the spirit of mod_unique_id) */
    struct {
	/* Add thread id here, if applicable, when we get to pthread or apr */
        pid_t pid;
#ifdef NETWARE
        apr_uint64_t t;
#else
        struct timeval t;
#endif
        char hostname[257];

    } r;

    apr_md5_init(&c);
#ifdef NETWARE
    r.pid = NXThreadGetId();
    NXGetTime(NX_SINCE_BOOT, NX_USECONDS, &(r.t));
#else
    r.pid = getpid();
    gettimeofday(&r.t, (struct timezone *)0);
#endif
    gethostname(r.hostname, 256);
    apr_md5_update(&c, (const unsigned char *)&r, sizeof(r));
    apr_md5_final(seed, &c);

    memcpy(node, seed, NODE_LENGTH);    /* use a subset of the seed bytes */
#endif
}

/* This implementation generates a random node ID instead of a
   system-dependent call to get IEEE node ID. This is also more secure:
   we aren't passing out our MAC address.
*/
static void get_pseudo_node_identifier(unsigned char *node)
{
    get_random_info(node);
    node[0] |= 0x01;                    /* this designates a random multicast node ID */
}

static void get_system_time(apr_uint64_t *uuid_time)
{
    /* ### fix this call to be more portable? */
    *uuid_time = apr_time_now();

    /* Offset between UUID formatted times and Unix formatted times.
       UUID UTC base time is October 15, 1582.
       Unix base time is January 1, 1970.      */
    *uuid_time = (*uuid_time * 10) + APR_TIME_C(0x01B21DD213814000);
}

/* true_random -- generate a crypto-quality random number. */
static int true_random(void)
{
    apr_uint64_t time_now;

#if APR_HAS_RANDOM
    unsigned char buf[2];

    if (apr_generate_random_bytes(buf, 2) == APR_SUCCESS) {
        return (buf[0] << 8) | buf[1];
    }
#endif

    /* crap. this isn't crypto quality, but it will be Good Enough */

    time_now = apr_time_now();
    srand((unsigned int)(((time_now >> 32) ^ time_now) & 0xffffffff));

    return rand() & 0x0FFFF;
}

static void init_state(void)
{
    uuid_state_seqnum = true_random();
    get_pseudo_node_identifier(uuid_state_node);
}

static void get_current_time(apr_uint64_t *timestamp)
{
    /* ### this needs to be made thread-safe! */

    apr_uint64_t time_now;
    static apr_uint64_t time_last = 0;
    static apr_uint64_t fudge = 0;

    get_system_time(&time_now);
        
    /* if clock reading changed since last UUID generated... */
    if (time_last != time_now) {
        /* The clock reading has changed since the last UUID was generated.
           Reset the fudge factor. if we are generating them too fast, then
           the fudge may need to be reset to something greater than zero. */
        if (time_last + fudge > time_now)
            fudge = time_last + fudge - time_now + 1;
        else
            fudge = 0;
        time_last = time_now;
    }
    else {
        /* We generated two really fast. Bump the fudge factor. */
        ++fudge;
    }

    *timestamp = time_now + fudge;
}

APU_DECLARE(void) apr_uuid_get(apr_uuid_t *uuid)
{
    apr_uint64_t timestamp;
    unsigned char *d = uuid->data;

#if APR_HAS_OS_UUID
    if (apr_os_uuid_get(d) == APR_SUCCESS) {
        return;
    }
#endif /* !APR_HAS_OS_UUID */

    if (!uuid_state_node[0])
        init_state();

    get_current_time(&timestamp);

    /* time_low, uint32 */
    d[3] = (unsigned char)timestamp;
    d[2] = (unsigned char)(timestamp >> 8);
    d[1] = (unsigned char)(timestamp >> 16);
    d[0] = (unsigned char)(timestamp >> 24);
    /* time_mid, uint16 */
    d[5] = (unsigned char)(timestamp >> 32);
    d[4] = (unsigned char)(timestamp >> 40);
    /* time_hi_and_version, uint16 */
    d[7] = (unsigned char)(timestamp >> 48);
    d[6] = (unsigned char)(((timestamp >> 56) & 0x0F) | 0x10);
    /* clock_seq_hi_and_reserved, uint8 */
    d[8] = (unsigned char)(((uuid_state_seqnum >> 8) & 0x3F) | 0x80);
    /* clock_seq_low, uint8 */
    d[9] = (unsigned char)uuid_state_seqnum;
    /* node, byte[6] */
    memcpy(&d[10], uuid_state_node, NODE_LENGTH);
}
