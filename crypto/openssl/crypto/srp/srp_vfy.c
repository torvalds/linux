/*
 * Copyright 2004-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2004, EdelKey Project. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Christophe Renou and Peter Sylvester,
 * for the EdelKey project.
 */

#ifndef OPENSSL_NO_SRP
# include "internal/cryptlib.h"
# include "internal/evp_int.h"
# include <openssl/sha.h>
# include <openssl/srp.h>
# include <openssl/evp.h>
# include <openssl/buffer.h>
# include <openssl/rand.h>
# include <openssl/txt_db.h>
# include <openssl/err.h>

# define SRP_RANDOM_SALT_LEN 20
# define MAX_LEN 2500

/*
 * Note that SRP uses its own variant of base 64 encoding. A different base64
 * alphabet is used and no padding '=' characters are added. Instead we pad to
 * the front with 0 bytes and subsequently strip off leading encoded padding.
 * This variant is used for compatibility with other SRP implementations -
 * notably libsrp, but also others. It is also required for backwards
 * compatibility in order to load verifier files from other OpenSSL versions.
 */

/*
 * Convert a base64 string into raw byte array representation.
 * Returns the length of the decoded data, or -1 on error.
 */
static int t_fromb64(unsigned char *a, size_t alen, const char *src)
{
    EVP_ENCODE_CTX *ctx;
    int outl = 0, outl2 = 0;
    size_t size, padsize;
    const unsigned char *pad = (const unsigned char *)"00";

    while (*src == ' ' || *src == '\t' || *src == '\n')
        ++src;
    size = strlen(src);
    padsize = 4 - (size & 3);
    padsize &= 3;

    /* Four bytes in src become three bytes output. */
    if (size > INT_MAX || ((size + padsize) / 4) * 3 > alen)
        return -1;

    ctx = EVP_ENCODE_CTX_new();
    if (ctx == NULL)
        return -1;

    /*
     * This should never occur because 1 byte of data always requires 2 bytes of
     * encoding, i.e.
     *  0 bytes unencoded = 0 bytes encoded
     *  1 byte unencoded  = 2 bytes encoded
     *  2 bytes unencoded = 3 bytes encoded
     *  3 bytes unencoded = 4 bytes encoded
     *  4 bytes unencoded = 6 bytes encoded
     *  etc
     */
    if (padsize == 3) {
        outl = -1;
        goto err;
    }

    /* Valid padsize values are now 0, 1 or 2 */

    EVP_DecodeInit(ctx);
    evp_encode_ctx_set_flags(ctx, EVP_ENCODE_CTX_USE_SRP_ALPHABET);

    /* Add any encoded padding that is required */
    if (padsize != 0
            && EVP_DecodeUpdate(ctx, a, &outl, pad, padsize) < 0) {
        outl = -1;
        goto err;
    }
    if (EVP_DecodeUpdate(ctx, a, &outl2, (const unsigned char *)src, size) < 0) {
        outl = -1;
        goto err;
    }
    outl += outl2;
    EVP_DecodeFinal(ctx, a + outl, &outl2);
    outl += outl2;

    /* Strip off the leading padding */
    if (padsize != 0) {
        if ((int)padsize >= outl) {
            outl = -1;
            goto err;
        }

        /*
         * If we added 1 byte of padding prior to encoding then we have 2 bytes
         * of "real" data which gets spread across 4 encoded bytes like this:
         *   (6 bits pad)(2 bits pad | 4 bits data)(6 bits data)(6 bits data)
         * So 1 byte of pre-encoding padding results in 1 full byte of encoded
         * padding.
         * If we added 2 bytes of padding prior to encoding this gets encoded
         * as:
         *   (6 bits pad)(6 bits pad)(4 bits pad | 2 bits data)(6 bits data)
         * So 2 bytes of pre-encoding padding results in 2 full bytes of encoded
         * padding, i.e. we have to strip the same number of bytes of padding
         * from the encoded data as we added to the pre-encoded data.
         */
        memmove(a, a + padsize, outl - padsize);
        outl -= padsize;
    }

 err:
    EVP_ENCODE_CTX_free(ctx);

    return outl;
}

/*
 * Convert a raw byte string into a null-terminated base64 ASCII string.
 * Returns 1 on success or 0 on error.
 */
static int t_tob64(char *dst, const unsigned char *src, int size)
{
    EVP_ENCODE_CTX *ctx = EVP_ENCODE_CTX_new();
    int outl = 0, outl2 = 0;
    unsigned char pad[2] = {0, 0};
    size_t leadz = 0;

    if (ctx == NULL)
        return 0;

    EVP_EncodeInit(ctx);
    evp_encode_ctx_set_flags(ctx, EVP_ENCODE_CTX_NO_NEWLINES
                                  | EVP_ENCODE_CTX_USE_SRP_ALPHABET);

    /*
     * We pad at the front with zero bytes until the length is a multiple of 3
     * so that EVP_EncodeUpdate/EVP_EncodeFinal does not add any of its own "="
     * padding
     */
    leadz = 3 - (size % 3);
    if (leadz != 3
            && !EVP_EncodeUpdate(ctx, (unsigned char *)dst, &outl, pad,
                                 leadz)) {
        EVP_ENCODE_CTX_free(ctx);
        return 0;
    }

    if (!EVP_EncodeUpdate(ctx, (unsigned char *)dst + outl, &outl2, src,
                          size)) {
        EVP_ENCODE_CTX_free(ctx);
        return 0;
    }
    outl += outl2;
    EVP_EncodeFinal(ctx, (unsigned char *)dst + outl, &outl2);
    outl += outl2;

    /* Strip the encoded padding at the front */
    if (leadz != 3) {
        memmove(dst, dst + leadz, outl - leadz);
        dst[outl - leadz] = '\0';
    }

    EVP_ENCODE_CTX_free(ctx);
    return 1;
}

void SRP_user_pwd_free(SRP_user_pwd *user_pwd)
{
    if (user_pwd == NULL)
        return;
    BN_free(user_pwd->s);
    BN_clear_free(user_pwd->v);
    OPENSSL_free(user_pwd->id);
    OPENSSL_free(user_pwd->info);
    OPENSSL_free(user_pwd);
}

static SRP_user_pwd *SRP_user_pwd_new(void)
{
    SRP_user_pwd *ret;

    if ((ret = OPENSSL_malloc(sizeof(*ret))) == NULL) {
        /* SRPerr(SRP_F_SRP_USER_PWD_NEW, ERR_R_MALLOC_FAILURE); */ /*ckerr_ignore*/
        return NULL;
    }
    ret->N = NULL;
    ret->g = NULL;
    ret->s = NULL;
    ret->v = NULL;
    ret->id = NULL;
    ret->info = NULL;
    return ret;
}

static void SRP_user_pwd_set_gN(SRP_user_pwd *vinfo, const BIGNUM *g,
                                const BIGNUM *N)
{
    vinfo->N = N;
    vinfo->g = g;
}

static int SRP_user_pwd_set_ids(SRP_user_pwd *vinfo, const char *id,
                                const char *info)
{
    if (id != NULL && NULL == (vinfo->id = OPENSSL_strdup(id)))
        return 0;
    return (info == NULL || NULL != (vinfo->info = OPENSSL_strdup(info)));
}

static int SRP_user_pwd_set_sv(SRP_user_pwd *vinfo, const char *s,
                               const char *v)
{
    unsigned char tmp[MAX_LEN];
    int len;

    vinfo->v = NULL;
    vinfo->s = NULL;

    len = t_fromb64(tmp, sizeof(tmp), v);
    if (len < 0)
        return 0;
    if (NULL == (vinfo->v = BN_bin2bn(tmp, len, NULL)))
        return 0;
    len = t_fromb64(tmp, sizeof(tmp), s);
    if (len < 0)
        goto err;
    vinfo->s = BN_bin2bn(tmp, len, NULL);
    if (vinfo->s == NULL)
        goto err;
    return 1;
 err:
    BN_free(vinfo->v);
    vinfo->v = NULL;
    return 0;
}

static int SRP_user_pwd_set_sv_BN(SRP_user_pwd *vinfo, BIGNUM *s, BIGNUM *v)
{
    vinfo->v = v;
    vinfo->s = s;
    return (vinfo->s != NULL && vinfo->v != NULL);
}

static SRP_user_pwd *srp_user_pwd_dup(SRP_user_pwd *src)
{
    SRP_user_pwd *ret;

    if (src == NULL)
        return NULL;
    if ((ret = SRP_user_pwd_new()) == NULL)
        return NULL;

    SRP_user_pwd_set_gN(ret, src->g, src->N);
    if (!SRP_user_pwd_set_ids(ret, src->id, src->info)
        || !SRP_user_pwd_set_sv_BN(ret, BN_dup(src->s), BN_dup(src->v))) {
            SRP_user_pwd_free(ret);
            return NULL;
    }
    return ret;
}

SRP_VBASE *SRP_VBASE_new(char *seed_key)
{
    SRP_VBASE *vb = OPENSSL_malloc(sizeof(*vb));

    if (vb == NULL)
        return NULL;
    if ((vb->users_pwd = sk_SRP_user_pwd_new_null()) == NULL
        || (vb->gN_cache = sk_SRP_gN_cache_new_null()) == NULL) {
        OPENSSL_free(vb);
        return NULL;
    }
    vb->default_g = NULL;
    vb->default_N = NULL;
    vb->seed_key = NULL;
    if ((seed_key != NULL) && (vb->seed_key = OPENSSL_strdup(seed_key)) == NULL) {
        sk_SRP_user_pwd_free(vb->users_pwd);
        sk_SRP_gN_cache_free(vb->gN_cache);
        OPENSSL_free(vb);
        return NULL;
    }
    return vb;
}

void SRP_VBASE_free(SRP_VBASE *vb)
{
    if (!vb)
        return;
    sk_SRP_user_pwd_pop_free(vb->users_pwd, SRP_user_pwd_free);
    sk_SRP_gN_cache_free(vb->gN_cache);
    OPENSSL_free(vb->seed_key);
    OPENSSL_free(vb);
}

static SRP_gN_cache *SRP_gN_new_init(const char *ch)
{
    unsigned char tmp[MAX_LEN];
    int len;
    SRP_gN_cache *newgN = OPENSSL_malloc(sizeof(*newgN));

    if (newgN == NULL)
        return NULL;

    len = t_fromb64(tmp, sizeof(tmp), ch);
    if (len < 0)
        goto err;

    if ((newgN->b64_bn = OPENSSL_strdup(ch)) == NULL)
        goto err;

    if ((newgN->bn = BN_bin2bn(tmp, len, NULL)))
        return newgN;

    OPENSSL_free(newgN->b64_bn);
 err:
    OPENSSL_free(newgN);
    return NULL;
}

static void SRP_gN_free(SRP_gN_cache *gN_cache)
{
    if (gN_cache == NULL)
        return;
    OPENSSL_free(gN_cache->b64_bn);
    BN_free(gN_cache->bn);
    OPENSSL_free(gN_cache);
}

static SRP_gN *SRP_get_gN_by_id(const char *id, STACK_OF(SRP_gN) *gN_tab)
{
    int i;

    SRP_gN *gN;
    if (gN_tab != NULL)
        for (i = 0; i < sk_SRP_gN_num(gN_tab); i++) {
            gN = sk_SRP_gN_value(gN_tab, i);
            if (gN && (id == NULL || strcmp(gN->id, id) == 0))
                return gN;
        }

    return SRP_get_default_gN(id);
}

static BIGNUM *SRP_gN_place_bn(STACK_OF(SRP_gN_cache) *gN_cache, char *ch)
{
    int i;
    if (gN_cache == NULL)
        return NULL;

    /* search if we have already one... */
    for (i = 0; i < sk_SRP_gN_cache_num(gN_cache); i++) {
        SRP_gN_cache *cache = sk_SRP_gN_cache_value(gN_cache, i);
        if (strcmp(cache->b64_bn, ch) == 0)
            return cache->bn;
    }
    {                           /* it is the first time that we find it */
        SRP_gN_cache *newgN = SRP_gN_new_init(ch);
        if (newgN) {
            if (sk_SRP_gN_cache_insert(gN_cache, newgN, 0) > 0)
                return newgN->bn;
            SRP_gN_free(newgN);
        }
    }
    return NULL;
}

/*
 * this function parses verifier file. Format is:
 * string(index):base64(N):base64(g):0
 * string(username):base64(v):base64(salt):int(index)
 */

int SRP_VBASE_init(SRP_VBASE *vb, char *verifier_file)
{
    int error_code;
    STACK_OF(SRP_gN) *SRP_gN_tab = sk_SRP_gN_new_null();
    char *last_index = NULL;
    int i;
    char **pp;

    SRP_gN *gN = NULL;
    SRP_user_pwd *user_pwd = NULL;

    TXT_DB *tmpdb = NULL;
    BIO *in = BIO_new(BIO_s_file());

    error_code = SRP_ERR_OPEN_FILE;

    if (in == NULL || BIO_read_filename(in, verifier_file) <= 0)
        goto err;

    error_code = SRP_ERR_VBASE_INCOMPLETE_FILE;

    if ((tmpdb = TXT_DB_read(in, DB_NUMBER)) == NULL)
        goto err;

    error_code = SRP_ERR_MEMORY;

    if (vb->seed_key) {
        last_index = SRP_get_default_gN(NULL)->id;
    }
    for (i = 0; i < sk_OPENSSL_PSTRING_num(tmpdb->data); i++) {
        pp = sk_OPENSSL_PSTRING_value(tmpdb->data, i);
        if (pp[DB_srptype][0] == DB_SRP_INDEX) {
            /*
             * we add this couple in the internal Stack
             */

            if ((gN = OPENSSL_malloc(sizeof(*gN))) == NULL)
                goto err;

            if ((gN->id = OPENSSL_strdup(pp[DB_srpid])) == NULL
                || (gN->N = SRP_gN_place_bn(vb->gN_cache, pp[DB_srpverifier]))
                        == NULL
                || (gN->g = SRP_gN_place_bn(vb->gN_cache, pp[DB_srpsalt]))
                        == NULL
                || sk_SRP_gN_insert(SRP_gN_tab, gN, 0) == 0)
                goto err;

            gN = NULL;

            if (vb->seed_key != NULL) {
                last_index = pp[DB_srpid];
            }
        } else if (pp[DB_srptype][0] == DB_SRP_VALID) {
            /* it is a user .... */
            const SRP_gN *lgN;

            if ((lgN = SRP_get_gN_by_id(pp[DB_srpgN], SRP_gN_tab)) != NULL) {
                error_code = SRP_ERR_MEMORY;
                if ((user_pwd = SRP_user_pwd_new()) == NULL)
                    goto err;

                SRP_user_pwd_set_gN(user_pwd, lgN->g, lgN->N);
                if (!SRP_user_pwd_set_ids
                    (user_pwd, pp[DB_srpid], pp[DB_srpinfo]))
                    goto err;

                error_code = SRP_ERR_VBASE_BN_LIB;
                if (!SRP_user_pwd_set_sv
                    (user_pwd, pp[DB_srpsalt], pp[DB_srpverifier]))
                    goto err;

                if (sk_SRP_user_pwd_insert(vb->users_pwd, user_pwd, 0) == 0)
                    goto err;
                user_pwd = NULL; /* abandon responsibility */
            }
        }
    }

    if (last_index != NULL) {
        /* this means that we want to simulate a default user */

        if (((gN = SRP_get_gN_by_id(last_index, SRP_gN_tab)) == NULL)) {
            error_code = SRP_ERR_VBASE_BN_LIB;
            goto err;
        }
        vb->default_g = gN->g;
        vb->default_N = gN->N;
        gN = NULL;
    }
    error_code = SRP_NO_ERROR;

 err:
    /*
     * there may be still some leaks to fix, if this fails, the application
     * terminates most likely
     */

    if (gN != NULL) {
        OPENSSL_free(gN->id);
        OPENSSL_free(gN);
    }

    SRP_user_pwd_free(user_pwd);

    TXT_DB_free(tmpdb);
    BIO_free_all(in);

    sk_SRP_gN_free(SRP_gN_tab);

    return error_code;

}

static SRP_user_pwd *find_user(SRP_VBASE *vb, char *username)
{
    int i;
    SRP_user_pwd *user;

    if (vb == NULL)
        return NULL;

    for (i = 0; i < sk_SRP_user_pwd_num(vb->users_pwd); i++) {
        user = sk_SRP_user_pwd_value(vb->users_pwd, i);
        if (strcmp(user->id, username) == 0)
            return user;
    }

    return NULL;
}

# if OPENSSL_API_COMPAT < 0x10100000L
/*
 * DEPRECATED: use SRP_VBASE_get1_by_user instead.
 * This method ignores the configured seed and fails for an unknown user.
 * Ownership of the returned pointer is not released to the caller.
 * In other words, caller must not free the result.
 */
SRP_user_pwd *SRP_VBASE_get_by_user(SRP_VBASE *vb, char *username)
{
    return find_user(vb, username);
}
# endif

/*
 * Ownership of the returned pointer is released to the caller.
 * In other words, caller must free the result once done.
 */
SRP_user_pwd *SRP_VBASE_get1_by_user(SRP_VBASE *vb, char *username)
{
    SRP_user_pwd *user;
    unsigned char digv[SHA_DIGEST_LENGTH];
    unsigned char digs[SHA_DIGEST_LENGTH];
    EVP_MD_CTX *ctxt = NULL;

    if (vb == NULL)
        return NULL;

    if ((user = find_user(vb, username)) != NULL)
        return srp_user_pwd_dup(user);

    if ((vb->seed_key == NULL) ||
        (vb->default_g == NULL) || (vb->default_N == NULL))
        return NULL;

/* if the user is unknown we set parameters as well if we have a seed_key */

    if ((user = SRP_user_pwd_new()) == NULL)
        return NULL;

    SRP_user_pwd_set_gN(user, vb->default_g, vb->default_N);

    if (!SRP_user_pwd_set_ids(user, username, NULL))
        goto err;

    if (RAND_priv_bytes(digv, SHA_DIGEST_LENGTH) <= 0)
        goto err;
    ctxt = EVP_MD_CTX_new();
    if (ctxt == NULL
        || !EVP_DigestInit_ex(ctxt, EVP_sha1(), NULL)
        || !EVP_DigestUpdate(ctxt, vb->seed_key, strlen(vb->seed_key))
        || !EVP_DigestUpdate(ctxt, username, strlen(username))
        || !EVP_DigestFinal_ex(ctxt, digs, NULL))
        goto err;
    EVP_MD_CTX_free(ctxt);
    ctxt = NULL;
    if (SRP_user_pwd_set_sv_BN(user,
                               BN_bin2bn(digs, SHA_DIGEST_LENGTH, NULL),
                               BN_bin2bn(digv, SHA_DIGEST_LENGTH, NULL)))
        return user;

 err:
    EVP_MD_CTX_free(ctxt);
    SRP_user_pwd_free(user);
    return NULL;
}

/*
 * create a verifier (*salt,*verifier,g and N are in base64)
 */
char *SRP_create_verifier(const char *user, const char *pass, char **salt,
                          char **verifier, const char *N, const char *g)
{
    int len;
    char *result = NULL, *vf = NULL;
    const BIGNUM *N_bn = NULL, *g_bn = NULL;
    BIGNUM *N_bn_alloc = NULL, *g_bn_alloc = NULL, *s = NULL, *v = NULL;
    unsigned char tmp[MAX_LEN];
    unsigned char tmp2[MAX_LEN];
    char *defgNid = NULL;
    int vfsize = 0;

    if ((user == NULL) ||
        (pass == NULL) || (salt == NULL) || (verifier == NULL))
        goto err;

    if (N) {
        if ((len = t_fromb64(tmp, sizeof(tmp), N)) <= 0)
            goto err;
        N_bn_alloc = BN_bin2bn(tmp, len, NULL);
        if (N_bn_alloc == NULL)
            goto err;
        N_bn = N_bn_alloc;
        if ((len = t_fromb64(tmp, sizeof(tmp) ,g)) <= 0)
            goto err;
        g_bn_alloc = BN_bin2bn(tmp, len, NULL);
        if (g_bn_alloc == NULL)
            goto err;
        g_bn = g_bn_alloc;
        defgNid = "*";
    } else {
        SRP_gN *gN = SRP_get_gN_by_id(g, NULL);
        if (gN == NULL)
            goto err;
        N_bn = gN->N;
        g_bn = gN->g;
        defgNid = gN->id;
    }

    if (*salt == NULL) {
        if (RAND_bytes(tmp2, SRP_RANDOM_SALT_LEN) <= 0)
            goto err;

        s = BN_bin2bn(tmp2, SRP_RANDOM_SALT_LEN, NULL);
    } else {
        if ((len = t_fromb64(tmp2, sizeof(tmp2), *salt)) <= 0)
            goto err;
        s = BN_bin2bn(tmp2, len, NULL);
    }
    if (s == NULL)
        goto err;

    if (!SRP_create_verifier_BN(user, pass, &s, &v, N_bn, g_bn))
        goto err;

    if (BN_bn2bin(v, tmp) < 0)
        goto err;
    vfsize = BN_num_bytes(v) * 2;
    if (((vf = OPENSSL_malloc(vfsize)) == NULL))
        goto err;
    if (!t_tob64(vf, tmp, BN_num_bytes(v)))
        goto err;

    if (*salt == NULL) {
        char *tmp_salt;

        if ((tmp_salt = OPENSSL_malloc(SRP_RANDOM_SALT_LEN * 2)) == NULL) {
            goto err;
        }
        if (!t_tob64(tmp_salt, tmp2, SRP_RANDOM_SALT_LEN)) {
            OPENSSL_free(tmp_salt);
            goto err;
        }
        *salt = tmp_salt;
    }

    *verifier = vf;
    vf = NULL;
    result = defgNid;

 err:
    BN_free(N_bn_alloc);
    BN_free(g_bn_alloc);
    OPENSSL_clear_free(vf, vfsize);
    BN_clear_free(s);
    BN_clear_free(v);
    return result;
}

/*
 * create a verifier (*salt,*verifier,g and N are BIGNUMs). If *salt != NULL
 * then the provided salt will be used. On successful exit *verifier will point
 * to a newly allocated BIGNUM containing the verifier and (if a salt was not
 * provided) *salt will be populated with a newly allocated BIGNUM containing a
 * random salt.
 * The caller is responsible for freeing the allocated *salt and *verifier
 * BIGNUMS.
 */
int SRP_create_verifier_BN(const char *user, const char *pass, BIGNUM **salt,
                           BIGNUM **verifier, const BIGNUM *N,
                           const BIGNUM *g)
{
    int result = 0;
    BIGNUM *x = NULL;
    BN_CTX *bn_ctx = BN_CTX_new();
    unsigned char tmp2[MAX_LEN];
    BIGNUM *salttmp = NULL;

    if ((user == NULL) ||
        (pass == NULL) ||
        (salt == NULL) ||
        (verifier == NULL) || (N == NULL) || (g == NULL) || (bn_ctx == NULL))
        goto err;

    if (*salt == NULL) {
        if (RAND_bytes(tmp2, SRP_RANDOM_SALT_LEN) <= 0)
            goto err;

        salttmp = BN_bin2bn(tmp2, SRP_RANDOM_SALT_LEN, NULL);
        if (salttmp == NULL)
            goto err;
    } else {
        salttmp = *salt;
    }

    x = SRP_Calc_x(salttmp, user, pass);
    if (x == NULL)
        goto err;

    *verifier = BN_new();
    if (*verifier == NULL)
        goto err;

    if (!BN_mod_exp(*verifier, g, x, N, bn_ctx)) {
        BN_clear_free(*verifier);
        goto err;
    }

    result = 1;
    *salt = salttmp;

 err:
    if (salt != NULL && *salt != salttmp)
        BN_clear_free(salttmp);
    BN_clear_free(x);
    BN_CTX_free(bn_ctx);
    return result;
}

#endif
