/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "internal/cryptlib.h"
#include "internal/thread_once.h"
#include <openssl/crypto.h>
#include <openssl/buffer.h>
#include "internal/bio.h"
#include <openssl/lhash.h>

#ifndef OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE
# include <execinfo.h>
#endif

/*
 * The state changes to CRYPTO_MEM_CHECK_ON | CRYPTO_MEM_CHECK_ENABLE when
 * the application asks for it (usually after library initialisation for
 * which no book-keeping is desired). State CRYPTO_MEM_CHECK_ON exists only
 * temporarily when the library thinks that certain allocations should not be
 * checked (e.g. the data structures used for memory checking).  It is not
 * suitable as an initial state: the library will unexpectedly enable memory
 * checking when it executes one of those sections that want to disable
 * checking temporarily. State CRYPTO_MEM_CHECK_ENABLE without ..._ON makes
 * no sense whatsoever.
 */
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
static int mh_mode = CRYPTO_MEM_CHECK_OFF;
#endif

#ifndef OPENSSL_NO_CRYPTO_MDEBUG
static unsigned long order = 0; /* number of memory requests */

/*-
 * For application-defined information (static C-string `info')
 * to be displayed in memory leak list.
 * Each thread has its own stack.  For applications, there is
 *   OPENSSL_mem_debug_push("...")     to push an entry,
 *   OPENSSL_mem_debug_pop()     to pop an entry,
 */
struct app_mem_info_st {
    CRYPTO_THREAD_ID threadid;
    const char *file;
    int line;
    const char *info;
    struct app_mem_info_st *next; /* tail of thread's stack */
    int references;
};

static CRYPTO_ONCE memdbg_init = CRYPTO_ONCE_STATIC_INIT;
CRYPTO_RWLOCK *memdbg_lock;
static CRYPTO_RWLOCK *long_memdbg_lock;
static CRYPTO_THREAD_LOCAL appinfokey;

/* memory-block description */
struct mem_st {
    void *addr;
    int num;
    const char *file;
    int line;
    CRYPTO_THREAD_ID threadid;
    unsigned long order;
    time_t time;
    APP_INFO *app_info;
#ifndef OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE
    void *array[30];
    size_t array_siz;
#endif
};

/*
 * hash-table of memory requests (address as * key); access requires
 * long_memdbg_lock lock
 */
static LHASH_OF(MEM) *mh = NULL;

/* num_disable > 0 iff mh_mode == CRYPTO_MEM_CHECK_ON (w/o ..._ENABLE) */
static unsigned int num_disable = 0;

/*
 * Valid iff num_disable > 0.  long_memdbg_lock is locked exactly in this
 * case (by the thread named in disabling_thread).
 */
static CRYPTO_THREAD_ID disabling_threadid;

DEFINE_RUN_ONCE_STATIC(do_memdbg_init)
{
    memdbg_lock = CRYPTO_THREAD_lock_new();
    long_memdbg_lock = CRYPTO_THREAD_lock_new();
    if (memdbg_lock == NULL || long_memdbg_lock == NULL
        || !CRYPTO_THREAD_init_local(&appinfokey, NULL)) {
        CRYPTO_THREAD_lock_free(memdbg_lock);
        memdbg_lock = NULL;
        CRYPTO_THREAD_lock_free(long_memdbg_lock);
        long_memdbg_lock = NULL;
        return 0;
    }
    return 1;
}

static void app_info_free(APP_INFO *inf)
{
    if (inf == NULL)
        return;
    if (--(inf->references) <= 0) {
        app_info_free(inf->next);
        OPENSSL_free(inf);
    }
}
#endif

int CRYPTO_mem_ctrl(int mode)
{
#ifdef OPENSSL_NO_CRYPTO_MDEBUG
    return mode - mode;
#else
    int ret = mh_mode;

    if (!RUN_ONCE(&memdbg_init, do_memdbg_init))
        return -1;

    CRYPTO_THREAD_write_lock(memdbg_lock);
    switch (mode) {
    default:
        break;

    case CRYPTO_MEM_CHECK_ON:
        mh_mode = CRYPTO_MEM_CHECK_ON | CRYPTO_MEM_CHECK_ENABLE;
        num_disable = 0;
        break;

    case CRYPTO_MEM_CHECK_OFF:
        mh_mode = 0;
        num_disable = 0;
        break;

    /* switch off temporarily (for library-internal use): */
    case CRYPTO_MEM_CHECK_DISABLE:
        if (mh_mode & CRYPTO_MEM_CHECK_ON) {
            CRYPTO_THREAD_ID cur = CRYPTO_THREAD_get_current_id();
            /* see if we don't have long_memdbg_lock already */
            if (!num_disable
                || !CRYPTO_THREAD_compare_id(disabling_threadid, cur)) {
                /*
                 * Long-time lock long_memdbg_lock must not be claimed
                 * while we're holding memdbg_lock, or we'll deadlock
                 * if somebody else holds long_memdbg_lock (and cannot
                 * release it because we block entry to this function). Give
                 * them a chance, first, and then claim the locks in
                 * appropriate order (long-time lock first).
                 */
                CRYPTO_THREAD_unlock(memdbg_lock);
                /*
                 * Note that after we have waited for long_memdbg_lock and
                 * memdbg_lock, we'll still be in the right "case" and
                 * "if" branch because MemCheck_start and MemCheck_stop may
                 * never be used while there are multiple OpenSSL threads.
                 */
                CRYPTO_THREAD_write_lock(long_memdbg_lock);
                CRYPTO_THREAD_write_lock(memdbg_lock);
                mh_mode &= ~CRYPTO_MEM_CHECK_ENABLE;
                disabling_threadid = cur;
            }
            num_disable++;
        }
        break;

    case CRYPTO_MEM_CHECK_ENABLE:
        if (mh_mode & CRYPTO_MEM_CHECK_ON) {
            if (num_disable) {  /* always true, or something is going wrong */
                num_disable--;
                if (num_disable == 0) {
                    mh_mode |= CRYPTO_MEM_CHECK_ENABLE;
                    CRYPTO_THREAD_unlock(long_memdbg_lock);
                }
            }
        }
        break;
    }
    CRYPTO_THREAD_unlock(memdbg_lock);
    return ret;
#endif
}

#ifndef OPENSSL_NO_CRYPTO_MDEBUG

static int mem_check_on(void)
{
    int ret = 0;
    CRYPTO_THREAD_ID cur;

    if (mh_mode & CRYPTO_MEM_CHECK_ON) {
        if (!RUN_ONCE(&memdbg_init, do_memdbg_init))
            return 0;

        cur = CRYPTO_THREAD_get_current_id();
        CRYPTO_THREAD_read_lock(memdbg_lock);

        ret = (mh_mode & CRYPTO_MEM_CHECK_ENABLE)
            || !CRYPTO_THREAD_compare_id(disabling_threadid, cur);

        CRYPTO_THREAD_unlock(memdbg_lock);
    }
    return ret;
}

static int mem_cmp(const MEM *a, const MEM *b)
{
#ifdef _WIN64
    const char *ap = (const char *)a->addr, *bp = (const char *)b->addr;
    if (ap == bp)
        return 0;
    else if (ap > bp)
        return 1;
    else
        return -1;
#else
    return (const char *)a->addr - (const char *)b->addr;
#endif
}

static unsigned long mem_hash(const MEM *a)
{
    size_t ret;

    ret = (size_t)a->addr;

    ret = ret * 17851 + (ret >> 14) * 7 + (ret >> 4) * 251;
    return ret;
}

/* returns 1 if there was an info to pop, 0 if the stack was empty. */
static int pop_info(void)
{
    APP_INFO *current = NULL;

    if (!RUN_ONCE(&memdbg_init, do_memdbg_init))
        return 0;

    current = (APP_INFO *)CRYPTO_THREAD_get_local(&appinfokey);
    if (current != NULL) {
        APP_INFO *next = current->next;

        if (next != NULL) {
            next->references++;
            CRYPTO_THREAD_set_local(&appinfokey, next);
        } else {
            CRYPTO_THREAD_set_local(&appinfokey, NULL);
        }
        if (--(current->references) <= 0) {
            current->next = NULL;
            if (next != NULL)
                next->references--;
            OPENSSL_free(current);
        }
        return 1;
    }
    return 0;
}

int CRYPTO_mem_debug_push(const char *info, const char *file, int line)
{
    APP_INFO *ami, *amim;
    int ret = 0;

    if (mem_check_on()) {
        CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);

        if (!RUN_ONCE(&memdbg_init, do_memdbg_init)
            || (ami = OPENSSL_malloc(sizeof(*ami))) == NULL)
            goto err;

        ami->threadid = CRYPTO_THREAD_get_current_id();
        ami->file = file;
        ami->line = line;
        ami->info = info;
        ami->references = 1;
        ami->next = NULL;

        amim = (APP_INFO *)CRYPTO_THREAD_get_local(&appinfokey);
        CRYPTO_THREAD_set_local(&appinfokey, ami);

        if (amim != NULL)
            ami->next = amim;
        ret = 1;
 err:
        CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
    }

    return ret;
}

int CRYPTO_mem_debug_pop(void)
{
    int ret = 0;

    if (mem_check_on()) {
        CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);
        ret = pop_info();
        CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
    }
    return ret;
}

static unsigned long break_order_num = 0;

void CRYPTO_mem_debug_malloc(void *addr, size_t num, int before_p,
                             const char *file, int line)
{
    MEM *m, *mm;
    APP_INFO *amim;

    switch (before_p & 127) {
    case 0:
        break;
    case 1:
        if (addr == NULL)
            break;

        if (mem_check_on()) {
            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);

            if (!RUN_ONCE(&memdbg_init, do_memdbg_init)
                || (m = OPENSSL_malloc(sizeof(*m))) == NULL) {
                OPENSSL_free(addr);
                CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
                return;
            }
            if (mh == NULL) {
                if ((mh = lh_MEM_new(mem_hash, mem_cmp)) == NULL) {
                    OPENSSL_free(addr);
                    OPENSSL_free(m);
                    addr = NULL;
                    goto err;
                }
            }

            m->addr = addr;
            m->file = file;
            m->line = line;
            m->num = num;
            m->threadid = CRYPTO_THREAD_get_current_id();

            if (order == break_order_num) {
                /* BREAK HERE */
                m->order = order;
            }
            m->order = order++;
# ifndef OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE
            m->array_siz = backtrace(m->array, OSSL_NELEM(m->array));
# endif
            m->time = time(NULL);

            amim = (APP_INFO *)CRYPTO_THREAD_get_local(&appinfokey);
            m->app_info = amim;
            if (amim != NULL)
                amim->references++;

            if ((mm = lh_MEM_insert(mh, m)) != NULL) {
                /* Not good, but don't sweat it */
                if (mm->app_info != NULL) {
                    mm->app_info->references--;
                }
                OPENSSL_free(mm);
            }
 err:
            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
        }
        break;
    }
    return;
}

void CRYPTO_mem_debug_free(void *addr, int before_p,
        const char *file, int line)
{
    MEM m, *mp;

    switch (before_p) {
    case 0:
        if (addr == NULL)
            break;

        if (mem_check_on() && (mh != NULL)) {
            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);

            m.addr = addr;
            mp = lh_MEM_delete(mh, &m);
            if (mp != NULL) {
                app_info_free(mp->app_info);
                OPENSSL_free(mp);
            }

            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
        }
        break;
    case 1:
        break;
    }
}

void CRYPTO_mem_debug_realloc(void *addr1, void *addr2, size_t num,
                              int before_p, const char *file, int line)
{
    MEM m, *mp;

    switch (before_p) {
    case 0:
        break;
    case 1:
        if (addr2 == NULL)
            break;

        if (addr1 == NULL) {
            CRYPTO_mem_debug_malloc(addr2, num, 128 | before_p, file, line);
            break;
        }

        if (mem_check_on()) {
            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);

            m.addr = addr1;
            mp = lh_MEM_delete(mh, &m);
            if (mp != NULL) {
                mp->addr = addr2;
                mp->num = num;
#ifndef OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE
                mp->array_siz = backtrace(mp->array, OSSL_NELEM(mp->array));
#endif
                (void)lh_MEM_insert(mh, mp);
            }

            CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
        }
        break;
    }
    return;
}

typedef struct mem_leak_st {
    int (*print_cb) (const char *str, size_t len, void *u);
    void *print_cb_arg;
    int chunks;
    long bytes;
} MEM_LEAK;

static void print_leak(const MEM *m, MEM_LEAK *l)
{
    char buf[1024];
    char *bufp = buf;
    size_t len = sizeof(buf), ami_cnt;
    APP_INFO *amip;
    int n;
    struct tm *lcl = NULL;
    /*
     * Convert between CRYPTO_THREAD_ID (which could be anything at all) and
     * a long. This may not be meaningful depending on what CRYPTO_THREAD_ID is
     * but hopefully should give something sensible on most platforms
     */
    union {
        CRYPTO_THREAD_ID tid;
        unsigned long ltid;
    } tid;
    CRYPTO_THREAD_ID ti;

    lcl = localtime(&m->time);
    n = BIO_snprintf(bufp, len, "[%02d:%02d:%02d] ",
                     lcl->tm_hour, lcl->tm_min, lcl->tm_sec);
    if (n <= 0) {
        bufp[0] = '\0';
        return;
    }
    bufp += n;
    len -= n;

    n = BIO_snprintf(bufp, len, "%5lu file=%s, line=%d, ",
                     m->order, m->file, m->line);
    if (n <= 0)
        return;
    bufp += n;
    len -= n;

    tid.ltid = 0;
    tid.tid = m->threadid;
    n = BIO_snprintf(bufp, len, "thread=%lu, ", tid.ltid);
    if (n <= 0)
        return;
    bufp += n;
    len -= n;

    n = BIO_snprintf(bufp, len, "number=%d, address=%p\n", m->num, m->addr);
    if (n <= 0)
        return;
    bufp += n;
    len -= n;

    l->print_cb(buf, (size_t)(bufp - buf), l->print_cb_arg);

    l->chunks++;
    l->bytes += m->num;

    amip = m->app_info;
    ami_cnt = 0;

    if (amip) {
        ti = amip->threadid;

        do {
            int buf_len;
            int info_len;

            ami_cnt++;
            if (ami_cnt >= sizeof(buf) - 1)
                break;
            memset(buf, '>', ami_cnt);
            buf[ami_cnt] = '\0';
            tid.ltid = 0;
            tid.tid = amip->threadid;
            n = BIO_snprintf(buf + ami_cnt, sizeof(buf) - ami_cnt,
                             " thread=%lu, file=%s, line=%d, info=\"",
                             tid.ltid, amip->file, amip->line);
            if (n <= 0)
                break;
            buf_len = ami_cnt + n;
            info_len = strlen(amip->info);
            if (128 - buf_len - 3 < info_len) {
                memcpy(buf + buf_len, amip->info, 128 - buf_len - 3);
                buf_len = 128 - 3;
            } else {
                n = BIO_snprintf(buf + buf_len, sizeof(buf) - buf_len, "%s",
                                 amip->info);
                if (n < 0)
                    break;
                buf_len += n;
            }
            n = BIO_snprintf(buf + buf_len, sizeof(buf) - buf_len, "\"\n");
            if (n <= 0)
                break;

            l->print_cb(buf, buf_len + n, l->print_cb_arg);

            amip = amip->next;
        }
        while (amip && CRYPTO_THREAD_compare_id(amip->threadid, ti));
    }

#ifndef OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE
    {
        size_t i;
        char **strings = backtrace_symbols(m->array, m->array_siz);

        for (i = 0; i < m->array_siz; i++)
            fprintf(stderr, "##> %s\n", strings[i]);
        free(strings);
    }
#endif
}

IMPLEMENT_LHASH_DOALL_ARG_CONST(MEM, MEM_LEAK);

int CRYPTO_mem_leaks_cb(int (*cb) (const char *str, size_t len, void *u),
                        void *u)
{
    MEM_LEAK ml;

    /* Ensure all resources are released */
    OPENSSL_cleanup();

    if (!RUN_ONCE(&memdbg_init, do_memdbg_init))
        return -1;

    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);

    ml.print_cb = cb;
    ml.print_cb_arg = u;
    ml.bytes = 0;
    ml.chunks = 0;
    if (mh != NULL)
        lh_MEM_doall_MEM_LEAK(mh, print_leak, &ml);

    if (ml.chunks != 0) {
        char buf[256];

        BIO_snprintf(buf, sizeof(buf), "%ld bytes leaked in %d chunks\n",
                     ml.bytes, ml.chunks);
        cb(buf, strlen(buf), u);
    } else {
        /*
         * Make sure that, if we found no leaks, memory-leak debugging itself
         * does not introduce memory leaks (which might irritate external
         * debugging tools). (When someone enables leak checking, but does not
         * call this function, we declare it to be their fault.)
         */
        int old_mh_mode;

        CRYPTO_THREAD_write_lock(memdbg_lock);

        /*
         * avoid deadlock when lh_free() uses CRYPTO_mem_debug_free(), which uses
         * mem_check_on
         */
        old_mh_mode = mh_mode;
        mh_mode = CRYPTO_MEM_CHECK_OFF;

        lh_MEM_free(mh);
        mh = NULL;

        mh_mode = old_mh_mode;
        CRYPTO_THREAD_unlock(memdbg_lock);
    }
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_OFF);

    /* Clean up locks etc */
    CRYPTO_THREAD_cleanup_local(&appinfokey);
    CRYPTO_THREAD_lock_free(memdbg_lock);
    CRYPTO_THREAD_lock_free(long_memdbg_lock);
    memdbg_lock = NULL;
    long_memdbg_lock = NULL;

    return ml.chunks == 0 ? 1 : 0;
}

static int print_bio(const char *str, size_t len, void *b)
{
    return BIO_write((BIO *)b, str, len);
}

int CRYPTO_mem_leaks(BIO *b)
{
    /*
     * OPENSSL_cleanup() will free the ex_data locks so we can't have any
     * ex_data hanging around
     */
    bio_free_ex_data(b);

    return CRYPTO_mem_leaks_cb(print_bio, b);
}

# ifndef OPENSSL_NO_STDIO
int CRYPTO_mem_leaks_fp(FILE *fp)
{
    BIO *b;
    int ret;

    /*
     * Need to turn off memory checking when allocated BIOs ... especially as
     * we're creating them at a time when we're trying to check we've not
     * left anything un-free()'d!!
     */
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_DISABLE);
    b = BIO_new(BIO_s_file());
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
    if (b == NULL)
        return -1;
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = CRYPTO_mem_leaks_cb(print_bio, b);
    BIO_free(b);
    return ret;
}
# endif

#endif
