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
 * sdbm - ndbm work-alike hashed database library
 * based on Per-Aake Larson's Dynamic Hashing algorithms. BIT 18 (1978).
 * author: oz@nexus.yorku.ca
 * ex-public domain, ported to APR for Apache 2
 * core routines
 */

#include "apr.h"
#include "apr_file_io.h"
#include "apr_strings.h"
#include "apr_errno.h"
#include "apr_sdbm.h"

#include "sdbm_tune.h"
#include "sdbm_pair.h"
#include "sdbm_private.h"

#include <string.h>     /* for memset() */
#include <stdlib.h>     /* for malloc() and free() */

/*
 * forward
 */
static int getdbit (apr_sdbm_t *, long);
static apr_status_t setdbit(apr_sdbm_t *, long);
static apr_status_t getpage(apr_sdbm_t *db, long);
static apr_status_t getnext(apr_sdbm_datum_t *key, apr_sdbm_t *db);
static apr_status_t makroom(apr_sdbm_t *, long, int);

/*
 * useful macros
 */
#define bad(x)		((x).dptr == NULL || (x).dsize <= 0)
#define exhash(item)	sdbm_hash((item).dptr, (item).dsize)

#define OFF_PAG(off)	(apr_off_t) (off) * PBLKSIZ
#define OFF_DIR(off)	(apr_off_t) (off) * DBLKSIZ

static const long masks[] = {
        000000000000, 000000000001, 000000000003, 000000000007,
        000000000017, 000000000037, 000000000077, 000000000177,
        000000000377, 000000000777, 000000001777, 000000003777,
        000000007777, 000000017777, 000000037777, 000000077777,
        000000177777, 000000377777, 000000777777, 000001777777,
        000003777777, 000007777777, 000017777777, 000037777777,
        000077777777, 000177777777, 000377777777, 000777777777,
        001777777777, 003777777777, 007777777777, 017777777777
};

const apr_sdbm_datum_t sdbm_nullitem = { NULL, 0 };

static apr_status_t database_cleanup(void *data)
{
    apr_sdbm_t *db = data;

    /*
     * Can't rely on apr_sdbm_unlock, since it will merely
     * decrement the refcnt if several locks are held.
     */
    if (db->flags & (SDBM_SHARED_LOCK | SDBM_EXCLUSIVE_LOCK))
        (void) apr_file_unlock(db->dirf);
    (void) apr_file_close(db->dirf);
    (void) apr_file_close(db->pagf);
    free(db);

    return APR_SUCCESS;
}

static apr_status_t prep(apr_sdbm_t **pdb, const char *dirname, const char *pagname,
                         apr_int32_t flags, apr_fileperms_t perms, apr_pool_t *p)
{
    apr_sdbm_t *db;
    apr_status_t status;

    *pdb = NULL;

    db = malloc(sizeof(*db));
    memset(db, 0, sizeof(*db));

    db->pool = p;

    /*
     * adjust user flags so that WRONLY becomes RDWR, 
     * as required by this package. Also set our internal
     * flag for RDONLY if needed.
     */
    if (!(flags & APR_FOPEN_WRITE)) {
        db->flags |= SDBM_RDONLY;
    }

    /*
     * adjust the file open flags so that we handle locking
     * on our own (don't rely on any locking behavior within
     * an apr_file_t, in case it's ever introduced, and set
     * our own flag.
     */
    if (flags & APR_FOPEN_SHARELOCK) {
        db->flags |= SDBM_SHARED;
        flags &= ~APR_FOPEN_SHARELOCK;
    }

    flags |= APR_FOPEN_BINARY | APR_FOPEN_READ;

    /*
     * open the files in sequence, and stat the dirfile.
     * If we fail anywhere, undo everything, return NULL.
     */

    if ((status = apr_file_open(&db->dirf, dirname, flags, perms, p))
                != APR_SUCCESS)
        goto error;

    if ((status = apr_file_open(&db->pagf, pagname, flags, perms, p))
                != APR_SUCCESS)
        goto error;

    if ((status = apr_sdbm_lock(db, (db->flags & SDBM_RDONLY) 
                                        ? APR_FLOCK_SHARED
                                        : APR_FLOCK_EXCLUSIVE))
                != APR_SUCCESS)
        goto error;

    /* apr_pcalloc zeroed the buffers
     * apr_sdbm_lock stated the dirf->size and invalidated the cache
     */

    /*
     * if we are opened in SHARED mode, unlock ourself 
     */
    if (db->flags & SDBM_SHARED)
        if ((status = apr_sdbm_unlock(db)) != APR_SUCCESS)
            goto error;

    /* make sure that we close the database at some point */
    apr_pool_cleanup_register(p, db, database_cleanup, apr_pool_cleanup_null);

    /* Done! */
    *pdb = db;
    return APR_SUCCESS;

error:
    if (db->dirf && db->pagf)
        (void) apr_sdbm_unlock(db);
    if (db->dirf != NULL)
        (void) apr_file_close(db->dirf);
    if (db->pagf != NULL) {
        (void) apr_file_close(db->pagf);
    }
    free(db);
    return status;
}

APU_DECLARE(apr_status_t) apr_sdbm_open(apr_sdbm_t **db, const char *file, 
                                        apr_int32_t flags, 
                                        apr_fileperms_t perms, apr_pool_t *p)
{
    char *dirname = apr_pstrcat(p, file, APR_SDBM_DIRFEXT, NULL);
    char *pagname = apr_pstrcat(p, file, APR_SDBM_PAGFEXT, NULL);
    
    return prep(db, dirname, pagname, flags, perms, p);
}

APU_DECLARE(apr_status_t) apr_sdbm_close(apr_sdbm_t *db)
{
    return apr_pool_cleanup_run(db->pool, db, database_cleanup);
}

APU_DECLARE(apr_status_t) apr_sdbm_fetch(apr_sdbm_t *db, apr_sdbm_datum_t *val,
                                         apr_sdbm_datum_t key)
{
    apr_status_t status;
    
    if (db == NULL || bad(key))
        return APR_EINVAL;

    if ((status = apr_sdbm_lock(db, APR_FLOCK_SHARED)) != APR_SUCCESS)
        return status;

    if ((status = getpage(db, exhash(key))) == APR_SUCCESS) {
        *val = getpair(db->pagbuf, key);
        /* ### do we want a not-found result? */
    }

    (void) apr_sdbm_unlock(db);

    return status;
}

static apr_status_t write_page(apr_sdbm_t *db, const char *buf, long pagno)
{
    apr_status_t status;
    apr_off_t off = OFF_PAG(pagno);
    
    if ((status = apr_file_seek(db->pagf, APR_SET, &off)) == APR_SUCCESS)
        status = apr_file_write_full(db->pagf, buf, PBLKSIZ, NULL);

    return status;
}

APU_DECLARE(apr_status_t) apr_sdbm_delete(apr_sdbm_t *db, 
                                          const apr_sdbm_datum_t key)
{
    apr_status_t status;
    
    if (db == NULL || bad(key))
        return APR_EINVAL;
    if (apr_sdbm_rdonly(db))
        return APR_EINVAL;
    
    if ((status = apr_sdbm_lock(db, APR_FLOCK_EXCLUSIVE)) != APR_SUCCESS)
        return status;

    if ((status = getpage(db, exhash(key))) == APR_SUCCESS) {
        if (!delpair(db->pagbuf, key))
            /* ### should we define some APRUTIL codes? */
            status = APR_EGENERAL;
        else
            status = write_page(db, db->pagbuf, db->pagbno);
    }

    (void) apr_sdbm_unlock(db);

    return status;
}

APU_DECLARE(apr_status_t) apr_sdbm_store(apr_sdbm_t *db, apr_sdbm_datum_t key,
                                         apr_sdbm_datum_t val, int flags)
{
    int need;
    register long hash;
    apr_status_t status;
    
    if (db == NULL || bad(key))
        return APR_EINVAL;
    if (apr_sdbm_rdonly(db))
        return APR_EINVAL;
    need = key.dsize + val.dsize;
    /*
     * is the pair too big (or too small) for this database ??
     */
    if (need < 0 || need > PAIRMAX)
        return APR_EINVAL;

    if ((status = apr_sdbm_lock(db, APR_FLOCK_EXCLUSIVE)) != APR_SUCCESS)
        return status;

    if ((status = getpage(db, (hash = exhash(key)))) == APR_SUCCESS) {

        /*
         * if we need to replace, delete the key/data pair
         * first. If it is not there, ignore.
         */
        if (flags == APR_SDBM_REPLACE)
            (void) delpair(db->pagbuf, key);
        else if (!(flags & APR_SDBM_INSERTDUP) && duppair(db->pagbuf, key)) {
            status = APR_EEXIST;
            goto error;
        }
        /*
         * if we do not have enough room, we have to split.
         */
        if (!fitpair(db->pagbuf, need))
            if ((status = makroom(db, hash, need)) != APR_SUCCESS)
                goto error;
        /*
         * we have enough room or split is successful. insert the key,
         * and update the page file.
         */
        (void) putpair(db->pagbuf, key, val);

        status = write_page(db, db->pagbuf, db->pagbno);
    }

error:
    (void) apr_sdbm_unlock(db);    

    return status;
}

/*
 * makroom - make room by splitting the overfull page
 * this routine will attempt to make room for SPLTMAX times before
 * giving up.
 */
static apr_status_t makroom(apr_sdbm_t *db, long hash, int need)
{
    long newp;
    char twin[PBLKSIZ];
    char *pag = db->pagbuf;
    char *new = twin;
    register int smax = SPLTMAX;
    apr_status_t status;

    do {
        /*
         * split the current page
         */
        (void) splpage(pag, new, db->hmask + 1);
        /*
         * address of the new page
         */
        newp = (hash & db->hmask) | (db->hmask + 1);

        /*
         * write delay, read avoidence/cache shuffle:
         * select the page for incoming pair: if key is to go to the new page,
         * write out the previous one, and copy the new one over, thus making
         * it the current page. If not, simply write the new page, and we are
         * still looking at the page of interest. current page is not updated
         * here, as sdbm_store will do so, after it inserts the incoming pair.
         */
        if (hash & (db->hmask + 1)) {
            if ((status = write_page(db, db->pagbuf, db->pagbno)) 
                        != APR_SUCCESS)
                return status;
                    
            db->pagbno = newp;
            (void) memcpy(pag, new, PBLKSIZ);
        }
        else {
            if ((status = write_page(db, new, newp)) != APR_SUCCESS)
                return status;
        }

        if ((status = setdbit(db, db->curbit)) != APR_SUCCESS)
            return status;
        /*
         * see if we have enough room now
         */
        if (fitpair(pag, need))
            return APR_SUCCESS;
        /*
         * try again... update curbit and hmask as getpage would have
         * done. because of our update of the current page, we do not
         * need to read in anything. BUT we have to write the current
         * [deferred] page out, as the window of failure is too great.
         */
        db->curbit = 2 * db->curbit
                   + ((hash & (db->hmask + 1)) ? 2 : 1);
        db->hmask |= db->hmask + 1;
            
        if ((status = write_page(db, db->pagbuf, db->pagbno))
                    != APR_SUCCESS)
            return status;
            
    } while (--smax);

    /*
     * if we are here, this is real bad news. After SPLTMAX splits,
     * we still cannot fit the key. say goodnight.
     */
#if 0
    (void) write(2, "sdbm: cannot insert after SPLTMAX attempts.\n", 44);
#endif
    /* ### ENOSPC not really appropriate but better than nothing */
    return APR_ENOSPC;

}

/* Reads 'len' bytes from file 'f' at offset 'off' into buf.
 * 'off' is given relative to the start of the file.
 * If EOF is returned while reading, this is taken as success.
 */
static apr_status_t read_from(apr_file_t *f, void *buf, 
             apr_off_t off, apr_size_t len)
{
    apr_status_t status;

    if ((status = apr_file_seek(f, APR_SET, &off)) != APR_SUCCESS ||
        ((status = apr_file_read_full(f, buf, len, NULL)) != APR_SUCCESS)) {
        /* if EOF is reached, pretend we read all zero's */
        if (status == APR_EOF) {
            memset(buf, 0, len);
            status = APR_SUCCESS;
        }
    }

    return status;
}

/*
 * the following two routines will break if
 * deletions aren't taken into account. (ndbm bug)
 */
APU_DECLARE(apr_status_t) apr_sdbm_firstkey(apr_sdbm_t *db, 
                                            apr_sdbm_datum_t *key)
{
    apr_status_t status;
    
    if ((status = apr_sdbm_lock(db, APR_FLOCK_SHARED)) != APR_SUCCESS)
        return status;

    /*
     * start at page 0
     */
    if ((status = read_from(db->pagf, db->pagbuf, OFF_PAG(0), PBLKSIZ))
                == APR_SUCCESS) {
        db->pagbno = 0;
        db->blkptr = 0;
        db->keyptr = 0;
        status = getnext(key, db);
    }

    (void) apr_sdbm_unlock(db);

    return status;
}

APU_DECLARE(apr_status_t) apr_sdbm_nextkey(apr_sdbm_t *db, 
                                           apr_sdbm_datum_t *key)
{
    apr_status_t status;
    
    if ((status = apr_sdbm_lock(db, APR_FLOCK_SHARED)) != APR_SUCCESS)
        return status;

    status = getnext(key, db);

    (void) apr_sdbm_unlock(db);

    return status;
}

/*
 * all important binary tree traversal
 */
static apr_status_t getpage(apr_sdbm_t *db, long hash)
{
    register int hbit;
    register long dbit;
    register long pagb;
    apr_status_t status;

    dbit = 0;
    hbit = 0;
    while (dbit < db->maxbno && getdbit(db, dbit))
    dbit = 2 * dbit + ((hash & (1 << hbit++)) ? 2 : 1);

    debug(("dbit: %d...", dbit));

    db->curbit = dbit;
    db->hmask = masks[hbit];

    pagb = hash & db->hmask;
    /*
     * see if the block we need is already in memory.
     * note: this lookaside cache has about 10% hit rate.
     */
    if (pagb != db->pagbno) { 
        /*
         * note: here, we assume a "hole" is read as 0s.
         * if not, must zero pagbuf first.
         * ### joe: this assumption was surely never correct? but
         * ### we make it so in read_from anyway.
         */
        if ((status = read_from(db->pagf, db->pagbuf, OFF_PAG(pagb), PBLKSIZ)) 
                    != APR_SUCCESS)
            return status;

        if (!chkpage(db->pagbuf))
            return APR_ENOSPC; /* ### better error? */
        db->pagbno = pagb;

        debug(("pag read: %d\n", pagb));
    }
    return APR_SUCCESS;
}

static int getdbit(apr_sdbm_t *db, long dbit)
{
    register long c;
    register long dirb;

    c = dbit / BYTESIZ;
    dirb = c / DBLKSIZ;

    if (dirb != db->dirbno) {
        if (read_from(db->dirf, db->dirbuf, OFF_DIR(dirb), DBLKSIZ)
                    != APR_SUCCESS)
            return 0;

        db->dirbno = dirb;

        debug(("dir read: %d\n", dirb));
    }

    return db->dirbuf[c % DBLKSIZ] & (1 << dbit % BYTESIZ);
}

static apr_status_t setdbit(apr_sdbm_t *db, long dbit)
{
    register long c;
    register long dirb;
    apr_status_t status;
    apr_off_t off;

    c = dbit / BYTESIZ;
    dirb = c / DBLKSIZ;

    if (dirb != db->dirbno) {
        if ((status = read_from(db->dirf, db->dirbuf, OFF_DIR(dirb), DBLKSIZ))
                    != APR_SUCCESS)
            return status;

        db->dirbno = dirb;
        
        debug(("dir read: %d\n", dirb));
    }

    db->dirbuf[c % DBLKSIZ] |= (1 << dbit % BYTESIZ);

    if (dbit >= db->maxbno)
        db->maxbno += DBLKSIZ * BYTESIZ;

    off = OFF_DIR(dirb);
    if ((status = apr_file_seek(db->dirf, APR_SET, &off)) == APR_SUCCESS)
        status = apr_file_write_full(db->dirf, db->dirbuf, DBLKSIZ, NULL);

    return status;
}

/*
* getnext - get the next key in the page, and if done with
* the page, try the next page in sequence
*/
static apr_status_t getnext(apr_sdbm_datum_t *key, apr_sdbm_t *db)
{
    apr_status_t status;
    for (;;) {
        db->keyptr++;
        *key = getnkey(db->pagbuf, db->keyptr);
        if (key->dptr != NULL)
            return APR_SUCCESS;
        /*
         * we either run out, or there is nothing on this page..
         * try the next one... If we lost our position on the
         * file, we will have to seek.
         */
        db->keyptr = 0;
        if (db->pagbno != db->blkptr++) {
            apr_off_t off = OFF_PAG(db->blkptr);
            if ((status = apr_file_seek(db->pagf, APR_SET, &off))
                        != APR_SUCCESS)
                return status;
        }

        db->pagbno = db->blkptr;
        /* ### EOF acceptable here too? */
        if ((status = apr_file_read_full(db->pagf, db->pagbuf, PBLKSIZ, NULL))
                    != APR_SUCCESS)
            return status;
        if (!chkpage(db->pagbuf))
            return APR_EGENERAL;     /* ### need better error */
    }

    /* NOTREACHED */
}


APU_DECLARE(int) apr_sdbm_rdonly(apr_sdbm_t *db)
{
    /* ### Should we return true if the first lock is a share lock,
     *     to reflect that apr_sdbm_store and apr_sdbm_delete will fail?
     */
    return (db->flags & SDBM_RDONLY) != 0;
}

