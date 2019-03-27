/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/ctype.h"
#include <limits.h>
#include "internal/cryptlib.h"
#include <openssl/lhash.h>
#include <openssl/asn1.h>
#include "internal/objects.h"
#include <openssl/bn.h>
#include "internal/asn1_int.h"
#include "obj_lcl.h"

/* obj_dat.h is generated from objects.h by obj_dat.pl */
#include "obj_dat.h"

DECLARE_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, sn);
DECLARE_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, ln);
DECLARE_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, obj);

#define ADDED_DATA      0
#define ADDED_SNAME     1
#define ADDED_LNAME     2
#define ADDED_NID       3

struct added_obj_st {
    int type;
    ASN1_OBJECT *obj;
};

static int new_nid = NUM_NID;
static LHASH_OF(ADDED_OBJ) *added = NULL;

static int sn_cmp(const ASN1_OBJECT *const *a, const unsigned int *b)
{
    return strcmp((*a)->sn, nid_objs[*b].sn);
}

IMPLEMENT_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, sn);

static int ln_cmp(const ASN1_OBJECT *const *a, const unsigned int *b)
{
    return strcmp((*a)->ln, nid_objs[*b].ln);
}

IMPLEMENT_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, ln);

static unsigned long added_obj_hash(const ADDED_OBJ *ca)
{
    const ASN1_OBJECT *a;
    int i;
    unsigned long ret = 0;
    unsigned char *p;

    a = ca->obj;
    switch (ca->type) {
    case ADDED_DATA:
        ret = a->length << 20L;
        p = (unsigned char *)a->data;
        for (i = 0; i < a->length; i++)
            ret ^= p[i] << ((i * 3) % 24);
        break;
    case ADDED_SNAME:
        ret = OPENSSL_LH_strhash(a->sn);
        break;
    case ADDED_LNAME:
        ret = OPENSSL_LH_strhash(a->ln);
        break;
    case ADDED_NID:
        ret = a->nid;
        break;
    default:
        /* abort(); */
        return 0;
    }
    ret &= 0x3fffffffL;
    ret |= ((unsigned long)ca->type) << 30L;
    return ret;
}

static int added_obj_cmp(const ADDED_OBJ *ca, const ADDED_OBJ *cb)
{
    ASN1_OBJECT *a, *b;
    int i;

    i = ca->type - cb->type;
    if (i)
        return i;
    a = ca->obj;
    b = cb->obj;
    switch (ca->type) {
    case ADDED_DATA:
        i = (a->length - b->length);
        if (i)
            return i;
        return memcmp(a->data, b->data, (size_t)a->length);
    case ADDED_SNAME:
        if (a->sn == NULL)
            return -1;
        else if (b->sn == NULL)
            return 1;
        else
            return strcmp(a->sn, b->sn);
    case ADDED_LNAME:
        if (a->ln == NULL)
            return -1;
        else if (b->ln == NULL)
            return 1;
        else
            return strcmp(a->ln, b->ln);
    case ADDED_NID:
        return a->nid - b->nid;
    default:
        /* abort(); */
        return 0;
    }
}

static int init_added(void)
{
    if (added != NULL)
        return 1;
    added = lh_ADDED_OBJ_new(added_obj_hash, added_obj_cmp);
    return added != NULL;
}

static void cleanup1_doall(ADDED_OBJ *a)
{
    a->obj->nid = 0;
    a->obj->flags |= ASN1_OBJECT_FLAG_DYNAMIC |
        ASN1_OBJECT_FLAG_DYNAMIC_STRINGS | ASN1_OBJECT_FLAG_DYNAMIC_DATA;
}

static void cleanup2_doall(ADDED_OBJ *a)
{
    a->obj->nid++;
}

static void cleanup3_doall(ADDED_OBJ *a)
{
    if (--a->obj->nid == 0)
        ASN1_OBJECT_free(a->obj);
    OPENSSL_free(a);
}

void obj_cleanup_int(void)
{
    if (added == NULL)
        return;
    lh_ADDED_OBJ_set_down_load(added, 0);
    lh_ADDED_OBJ_doall(added, cleanup1_doall); /* zero counters */
    lh_ADDED_OBJ_doall(added, cleanup2_doall); /* set counters */
    lh_ADDED_OBJ_doall(added, cleanup3_doall); /* free objects */
    lh_ADDED_OBJ_free(added);
    added = NULL;
}

int OBJ_new_nid(int num)
{
    int i;

    i = new_nid;
    new_nid += num;
    return i;
}

int OBJ_add_object(const ASN1_OBJECT *obj)
{
    ASN1_OBJECT *o;
    ADDED_OBJ *ao[4] = { NULL, NULL, NULL, NULL }, *aop;
    int i;

    if (added == NULL)
        if (!init_added())
            return 0;
    if ((o = OBJ_dup(obj)) == NULL)
        goto err;
    if ((ao[ADDED_NID] = OPENSSL_malloc(sizeof(*ao[0]))) == NULL)
        goto err2;
    if ((o->length != 0) && (obj->data != NULL))
        if ((ao[ADDED_DATA] = OPENSSL_malloc(sizeof(*ao[0]))) == NULL)
            goto err2;
    if (o->sn != NULL)
        if ((ao[ADDED_SNAME] = OPENSSL_malloc(sizeof(*ao[0]))) == NULL)
            goto err2;
    if (o->ln != NULL)
        if ((ao[ADDED_LNAME] = OPENSSL_malloc(sizeof(*ao[0]))) == NULL)
            goto err2;

    for (i = ADDED_DATA; i <= ADDED_NID; i++) {
        if (ao[i] != NULL) {
            ao[i]->type = i;
            ao[i]->obj = o;
            aop = lh_ADDED_OBJ_insert(added, ao[i]);
            /* memory leak, but should not normally matter */
            OPENSSL_free(aop);
        }
    }
    o->flags &=
        ~(ASN1_OBJECT_FLAG_DYNAMIC | ASN1_OBJECT_FLAG_DYNAMIC_STRINGS |
          ASN1_OBJECT_FLAG_DYNAMIC_DATA);

    return o->nid;
 err2:
    OBJerr(OBJ_F_OBJ_ADD_OBJECT, ERR_R_MALLOC_FAILURE);
 err:
    for (i = ADDED_DATA; i <= ADDED_NID; i++)
        OPENSSL_free(ao[i]);
    ASN1_OBJECT_free(o);
    return NID_undef;
}

ASN1_OBJECT *OBJ_nid2obj(int n)
{
    ADDED_OBJ ad, *adp;
    ASN1_OBJECT ob;

    if ((n >= 0) && (n < NUM_NID)) {
        if ((n != NID_undef) && (nid_objs[n].nid == NID_undef)) {
            OBJerr(OBJ_F_OBJ_NID2OBJ, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
        return (ASN1_OBJECT *)&(nid_objs[n]);
    } else if (added == NULL)
        return NULL;
    else {
        ad.type = ADDED_NID;
        ad.obj = &ob;
        ob.nid = n;
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj;
        else {
            OBJerr(OBJ_F_OBJ_NID2OBJ, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
    }
}

const char *OBJ_nid2sn(int n)
{
    ADDED_OBJ ad, *adp;
    ASN1_OBJECT ob;

    if ((n >= 0) && (n < NUM_NID)) {
        if ((n != NID_undef) && (nid_objs[n].nid == NID_undef)) {
            OBJerr(OBJ_F_OBJ_NID2SN, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
        return nid_objs[n].sn;
    } else if (added == NULL)
        return NULL;
    else {
        ad.type = ADDED_NID;
        ad.obj = &ob;
        ob.nid = n;
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj->sn;
        else {
            OBJerr(OBJ_F_OBJ_NID2SN, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
    }
}

const char *OBJ_nid2ln(int n)
{
    ADDED_OBJ ad, *adp;
    ASN1_OBJECT ob;

    if ((n >= 0) && (n < NUM_NID)) {
        if ((n != NID_undef) && (nid_objs[n].nid == NID_undef)) {
            OBJerr(OBJ_F_OBJ_NID2LN, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
        return nid_objs[n].ln;
    } else if (added == NULL)
        return NULL;
    else {
        ad.type = ADDED_NID;
        ad.obj = &ob;
        ob.nid = n;
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj->ln;
        else {
            OBJerr(OBJ_F_OBJ_NID2LN, OBJ_R_UNKNOWN_NID);
            return NULL;
        }
    }
}

static int obj_cmp(const ASN1_OBJECT *const *ap, const unsigned int *bp)
{
    int j;
    const ASN1_OBJECT *a = *ap;
    const ASN1_OBJECT *b = &nid_objs[*bp];

    j = (a->length - b->length);
    if (j)
        return j;
    if (a->length == 0)
        return 0;
    return memcmp(a->data, b->data, a->length);
}

IMPLEMENT_OBJ_BSEARCH_CMP_FN(const ASN1_OBJECT *, unsigned int, obj);

int OBJ_obj2nid(const ASN1_OBJECT *a)
{
    const unsigned int *op;
    ADDED_OBJ ad, *adp;

    if (a == NULL)
        return NID_undef;
    if (a->nid != 0)
        return a->nid;

    if (a->length == 0)
        return NID_undef;

    if (added != NULL) {
        ad.type = ADDED_DATA;
        ad.obj = (ASN1_OBJECT *)a; /* XXX: ugly but harmless */
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj->nid;
    }
    op = OBJ_bsearch_obj(&a, obj_objs, NUM_OBJ);
    if (op == NULL)
        return NID_undef;
    return nid_objs[*op].nid;
}

/*
 * Convert an object name into an ASN1_OBJECT if "noname" is not set then
 * search for short and long names first. This will convert the "dotted" form
 * into an object: unlike OBJ_txt2nid it can be used with any objects, not
 * just registered ones.
 */

ASN1_OBJECT *OBJ_txt2obj(const char *s, int no_name)
{
    int nid = NID_undef;
    ASN1_OBJECT *op;
    unsigned char *buf;
    unsigned char *p;
    const unsigned char *cp;
    int i, j;

    if (!no_name) {
        if (((nid = OBJ_sn2nid(s)) != NID_undef) ||
            ((nid = OBJ_ln2nid(s)) != NID_undef))
            return OBJ_nid2obj(nid);
    }

    /* Work out size of content octets */
    i = a2d_ASN1_OBJECT(NULL, 0, s, -1);
    if (i <= 0) {
        /* Don't clear the error */
        /*
         * ERR_clear_error();
         */
        return NULL;
    }
    /* Work out total size */
    j = ASN1_object_size(0, i, V_ASN1_OBJECT);
    if (j < 0)
        return NULL;

    if ((buf = OPENSSL_malloc(j)) == NULL) {
        OBJerr(OBJ_F_OBJ_TXT2OBJ, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    p = buf;
    /* Write out tag+length */
    ASN1_put_object(&p, 0, i, V_ASN1_OBJECT, V_ASN1_UNIVERSAL);
    /* Write out contents */
    a2d_ASN1_OBJECT(p, i, s, -1);

    cp = buf;
    op = d2i_ASN1_OBJECT(NULL, &cp, j);
    OPENSSL_free(buf);
    return op;
}

int OBJ_obj2txt(char *buf, int buf_len, const ASN1_OBJECT *a, int no_name)
{
    int i, n = 0, len, nid, first, use_bn;
    BIGNUM *bl;
    unsigned long l;
    const unsigned char *p;
    char tbuf[DECIMAL_SIZE(i) + DECIMAL_SIZE(l) + 2];

    /* Ensure that, at every state, |buf| is NUL-terminated. */
    if (buf && buf_len > 0)
        buf[0] = '\0';

    if ((a == NULL) || (a->data == NULL))
        return 0;

    if (!no_name && (nid = OBJ_obj2nid(a)) != NID_undef) {
        const char *s;
        s = OBJ_nid2ln(nid);
        if (s == NULL)
            s = OBJ_nid2sn(nid);
        if (s) {
            if (buf)
                OPENSSL_strlcpy(buf, s, buf_len);
            n = strlen(s);
            return n;
        }
    }

    len = a->length;
    p = a->data;

    first = 1;
    bl = NULL;

    while (len > 0) {
        l = 0;
        use_bn = 0;
        for (;;) {
            unsigned char c = *p++;
            len--;
            if ((len == 0) && (c & 0x80))
                goto err;
            if (use_bn) {
                if (!BN_add_word(bl, c & 0x7f))
                    goto err;
            } else
                l |= c & 0x7f;
            if (!(c & 0x80))
                break;
            if (!use_bn && (l > (ULONG_MAX >> 7L))) {
                if (bl == NULL && (bl = BN_new()) == NULL)
                    goto err;
                if (!BN_set_word(bl, l))
                    goto err;
                use_bn = 1;
            }
            if (use_bn) {
                if (!BN_lshift(bl, bl, 7))
                    goto err;
            } else
                l <<= 7L;
        }

        if (first) {
            first = 0;
            if (l >= 80) {
                i = 2;
                if (use_bn) {
                    if (!BN_sub_word(bl, 80))
                        goto err;
                } else
                    l -= 80;
            } else {
                i = (int)(l / 40);
                l -= (long)(i * 40);
            }
            if (buf && (buf_len > 1)) {
                *buf++ = i + '0';
                *buf = '\0';
                buf_len--;
            }
            n++;
        }

        if (use_bn) {
            char *bndec;
            bndec = BN_bn2dec(bl);
            if (!bndec)
                goto err;
            i = strlen(bndec);
            if (buf) {
                if (buf_len > 1) {
                    *buf++ = '.';
                    *buf = '\0';
                    buf_len--;
                }
                OPENSSL_strlcpy(buf, bndec, buf_len);
                if (i > buf_len) {
                    buf += buf_len;
                    buf_len = 0;
                } else {
                    buf += i;
                    buf_len -= i;
                }
            }
            n++;
            n += i;
            OPENSSL_free(bndec);
        } else {
            BIO_snprintf(tbuf, sizeof(tbuf), ".%lu", l);
            i = strlen(tbuf);
            if (buf && (buf_len > 0)) {
                OPENSSL_strlcpy(buf, tbuf, buf_len);
                if (i > buf_len) {
                    buf += buf_len;
                    buf_len = 0;
                } else {
                    buf += i;
                    buf_len -= i;
                }
            }
            n += i;
            l = 0;
        }
    }

    BN_free(bl);
    return n;

 err:
    BN_free(bl);
    return -1;
}

int OBJ_txt2nid(const char *s)
{
    ASN1_OBJECT *obj;
    int nid;
    obj = OBJ_txt2obj(s, 0);
    nid = OBJ_obj2nid(obj);
    ASN1_OBJECT_free(obj);
    return nid;
}

int OBJ_ln2nid(const char *s)
{
    ASN1_OBJECT o;
    const ASN1_OBJECT *oo = &o;
    ADDED_OBJ ad, *adp;
    const unsigned int *op;

    o.ln = s;
    if (added != NULL) {
        ad.type = ADDED_LNAME;
        ad.obj = &o;
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj->nid;
    }
    op = OBJ_bsearch_ln(&oo, ln_objs, NUM_LN);
    if (op == NULL)
        return NID_undef;
    return nid_objs[*op].nid;
}

int OBJ_sn2nid(const char *s)
{
    ASN1_OBJECT o;
    const ASN1_OBJECT *oo = &o;
    ADDED_OBJ ad, *adp;
    const unsigned int *op;

    o.sn = s;
    if (added != NULL) {
        ad.type = ADDED_SNAME;
        ad.obj = &o;
        adp = lh_ADDED_OBJ_retrieve(added, &ad);
        if (adp != NULL)
            return adp->obj->nid;
    }
    op = OBJ_bsearch_sn(&oo, sn_objs, NUM_SN);
    if (op == NULL)
        return NID_undef;
    return nid_objs[*op].nid;
}

const void *OBJ_bsearch_(const void *key, const void *base, int num, int size,
                         int (*cmp) (const void *, const void *))
{
    return OBJ_bsearch_ex_(key, base, num, size, cmp, 0);
}

const void *OBJ_bsearch_ex_(const void *key, const void *base_, int num,
                            int size,
                            int (*cmp) (const void *, const void *),
                            int flags)
{
    const char *base = base_;
    int l, h, i = 0, c = 0;
    const char *p = NULL;

    if (num == 0)
        return NULL;
    l = 0;
    h = num;
    while (l < h) {
        i = (l + h) / 2;
        p = &(base[i * size]);
        c = (*cmp) (key, p);
        if (c < 0)
            h = i;
        else if (c > 0)
            l = i + 1;
        else
            break;
    }
#ifdef CHARSET_EBCDIC
    /*
     * THIS IS A KLUDGE - Because the *_obj is sorted in ASCII order, and I
     * don't have perl (yet), we revert to a *LINEAR* search when the object
     * wasn't found in the binary search.
     */
    if (c != 0) {
        for (i = 0; i < num; ++i) {
            p = &(base[i * size]);
            c = (*cmp) (key, p);
            if (c == 0 || (c < 0 && (flags & OBJ_BSEARCH_VALUE_ON_NOMATCH)))
                return p;
        }
    }
#endif
    if (c != 0 && !(flags & OBJ_BSEARCH_VALUE_ON_NOMATCH))
        p = NULL;
    else if (c == 0 && (flags & OBJ_BSEARCH_FIRST_VALUE_ON_MATCH)) {
        while (i > 0 && (*cmp) (key, &(base[(i - 1) * size])) == 0)
            i--;
        p = &(base[i * size]);
    }
    return p;
}

/*
 * Parse a BIO sink to create some extra oid's objects.
 * Line format:<OID:isdigit or '.']><isspace><SN><isspace><LN>
 */
int OBJ_create_objects(BIO *in)
{
    char buf[512];
    int i, num = 0;
    char *o, *s, *l = NULL;

    for (;;) {
        s = o = NULL;
        i = BIO_gets(in, buf, 512);
        if (i <= 0)
            return num;
        buf[i - 1] = '\0';
        if (!ossl_isalnum(buf[0]))
            return num;
        o = s = buf;
        while (ossl_isdigit(*s) || *s == '.')
            s++;
        if (*s != '\0') {
            *(s++) = '\0';
            while (ossl_isspace(*s))
                s++;
            if (*s == '\0') {
                s = NULL;
            } else {
                l = s;
                while (*l != '\0' && !ossl_isspace(*l))
                    l++;
                if (*l != '\0') {
                    *(l++) = '\0';
                    while (ossl_isspace(*l))
                        l++;
                    if (*l == '\0') {
                        l = NULL;
                    }
                } else {
                    l = NULL;
                }
            }
        } else {
            s = NULL;
        }
        if (*o == '\0')
            return num;
        if (!OBJ_create(o, s, l))
            return num;
        num++;
    }
}

int OBJ_create(const char *oid, const char *sn, const char *ln)
{
    ASN1_OBJECT *tmpoid = NULL;
    int ok = 0;

    /* Check to see if short or long name already present */
    if ((sn != NULL && OBJ_sn2nid(sn) != NID_undef)
            || (ln != NULL && OBJ_ln2nid(ln) != NID_undef)) {
        OBJerr(OBJ_F_OBJ_CREATE, OBJ_R_OID_EXISTS);
        return 0;
    }

    /* Convert numerical OID string to an ASN1_OBJECT structure */
    tmpoid = OBJ_txt2obj(oid, 1);
    if (tmpoid == NULL)
        return 0;

    /* If NID is not NID_undef then object already exists */
    if (OBJ_obj2nid(tmpoid) != NID_undef) {
        OBJerr(OBJ_F_OBJ_CREATE, OBJ_R_OID_EXISTS);
        goto err;
    }

    tmpoid->nid = OBJ_new_nid(1);
    tmpoid->sn = (char *)sn;
    tmpoid->ln = (char *)ln;

    ok = OBJ_add_object(tmpoid);

    tmpoid->sn = NULL;
    tmpoid->ln = NULL;

 err:
    ASN1_OBJECT_free(tmpoid);
    return ok;
}

size_t OBJ_length(const ASN1_OBJECT *obj)
{
    if (obj == NULL)
        return 0;
    return obj->length;
}

const unsigned char *OBJ_get0_data(const ASN1_OBJECT *obj)
{
    if (obj == NULL)
        return NULL;
    return obj->data;
}
