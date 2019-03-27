/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Header for dynamic hash table routines Author - Eric Young
 */

#ifndef HEADER_LHASH_H
# define HEADER_LHASH_H

# include <openssl/e_os2.h>
# include <openssl/bio.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct lhash_node_st OPENSSL_LH_NODE;
typedef int (*OPENSSL_LH_COMPFUNC) (const void *, const void *);
typedef unsigned long (*OPENSSL_LH_HASHFUNC) (const void *);
typedef void (*OPENSSL_LH_DOALL_FUNC) (void *);
typedef void (*OPENSSL_LH_DOALL_FUNCARG) (void *, void *);
typedef struct lhash_st OPENSSL_LHASH;

/*
 * Macros for declaring and implementing type-safe wrappers for LHASH
 * callbacks. This way, callbacks can be provided to LHASH structures without
 * function pointer casting and the macro-defined callbacks provide
 * per-variable casting before deferring to the underlying type-specific
 * callbacks. NB: It is possible to place a "static" in front of both the
 * DECLARE and IMPLEMENT macros if the functions are strictly internal.
 */

/* First: "hash" functions */
# define DECLARE_LHASH_HASH_FN(name, o_type) \
        unsigned long name##_LHASH_HASH(const void *);
# define IMPLEMENT_LHASH_HASH_FN(name, o_type) \
        unsigned long name##_LHASH_HASH(const void *arg) { \
                const o_type *a = arg; \
                return name##_hash(a); }
# define LHASH_HASH_FN(name) name##_LHASH_HASH

/* Second: "compare" functions */
# define DECLARE_LHASH_COMP_FN(name, o_type) \
        int name##_LHASH_COMP(const void *, const void *);
# define IMPLEMENT_LHASH_COMP_FN(name, o_type) \
        int name##_LHASH_COMP(const void *arg1, const void *arg2) { \
                const o_type *a = arg1;             \
                const o_type *b = arg2; \
                return name##_cmp(a,b); }
# define LHASH_COMP_FN(name) name##_LHASH_COMP

/* Fourth: "doall_arg" functions */
# define DECLARE_LHASH_DOALL_ARG_FN(name, o_type, a_type) \
        void name##_LHASH_DOALL_ARG(void *, void *);
# define IMPLEMENT_LHASH_DOALL_ARG_FN(name, o_type, a_type) \
        void name##_LHASH_DOALL_ARG(void *arg1, void *arg2) { \
                o_type *a = arg1; \
                a_type *b = arg2; \
                name##_doall_arg(a, b); }
# define LHASH_DOALL_ARG_FN(name) name##_LHASH_DOALL_ARG


# define LH_LOAD_MULT    256

int OPENSSL_LH_error(OPENSSL_LHASH *lh);
OPENSSL_LHASH *OPENSSL_LH_new(OPENSSL_LH_HASHFUNC h, OPENSSL_LH_COMPFUNC c);
void OPENSSL_LH_free(OPENSSL_LHASH *lh);
void *OPENSSL_LH_insert(OPENSSL_LHASH *lh, void *data);
void *OPENSSL_LH_delete(OPENSSL_LHASH *lh, const void *data);
void *OPENSSL_LH_retrieve(OPENSSL_LHASH *lh, const void *data);
void OPENSSL_LH_doall(OPENSSL_LHASH *lh, OPENSSL_LH_DOALL_FUNC func);
void OPENSSL_LH_doall_arg(OPENSSL_LHASH *lh, OPENSSL_LH_DOALL_FUNCARG func, void *arg);
unsigned long OPENSSL_LH_strhash(const char *c);
unsigned long OPENSSL_LH_num_items(const OPENSSL_LHASH *lh);
unsigned long OPENSSL_LH_get_down_load(const OPENSSL_LHASH *lh);
void OPENSSL_LH_set_down_load(OPENSSL_LHASH *lh, unsigned long down_load);

# ifndef OPENSSL_NO_STDIO
void OPENSSL_LH_stats(const OPENSSL_LHASH *lh, FILE *fp);
void OPENSSL_LH_node_stats(const OPENSSL_LHASH *lh, FILE *fp);
void OPENSSL_LH_node_usage_stats(const OPENSSL_LHASH *lh, FILE *fp);
# endif
void OPENSSL_LH_stats_bio(const OPENSSL_LHASH *lh, BIO *out);
void OPENSSL_LH_node_stats_bio(const OPENSSL_LHASH *lh, BIO *out);
void OPENSSL_LH_node_usage_stats_bio(const OPENSSL_LHASH *lh, BIO *out);

# if OPENSSL_API_COMPAT < 0x10100000L
#  define _LHASH OPENSSL_LHASH
#  define LHASH_NODE OPENSSL_LH_NODE
#  define lh_error OPENSSL_LH_error
#  define lh_new OPENSSL_LH_new
#  define lh_free OPENSSL_LH_free
#  define lh_insert OPENSSL_LH_insert
#  define lh_delete OPENSSL_LH_delete
#  define lh_retrieve OPENSSL_LH_retrieve
#  define lh_doall OPENSSL_LH_doall
#  define lh_doall_arg OPENSSL_LH_doall_arg
#  define lh_strhash OPENSSL_LH_strhash
#  define lh_num_items OPENSSL_LH_num_items
#  ifndef OPENSSL_NO_STDIO
#   define lh_stats OPENSSL_LH_stats
#   define lh_node_stats OPENSSL_LH_node_stats
#   define lh_node_usage_stats OPENSSL_LH_node_usage_stats
#  endif
#  define lh_stats_bio OPENSSL_LH_stats_bio
#  define lh_node_stats_bio OPENSSL_LH_node_stats_bio
#  define lh_node_usage_stats_bio OPENSSL_LH_node_usage_stats_bio
# endif

/* Type checking... */

# define LHASH_OF(type) struct lhash_st_##type

# define DEFINE_LHASH_OF(type) \
    LHASH_OF(type) { union lh_##type##_dummy { void* d1; unsigned long d2; int d3; } dummy; }; \
    static ossl_inline LHASH_OF(type) * \
        lh_##type##_new(unsigned long (*hfn)(const type *), \
                        int (*cfn)(const type *, const type *)) \
    { \
        return (LHASH_OF(type) *) \
            OPENSSL_LH_new((OPENSSL_LH_HASHFUNC)hfn, (OPENSSL_LH_COMPFUNC)cfn); \
    } \
    static ossl_unused ossl_inline void lh_##type##_free(LHASH_OF(type) *lh) \
    { \
        OPENSSL_LH_free((OPENSSL_LHASH *)lh); \
    } \
    static ossl_unused ossl_inline type *lh_##type##_insert(LHASH_OF(type) *lh, type *d) \
    { \
        return (type *)OPENSSL_LH_insert((OPENSSL_LHASH *)lh, d); \
    } \
    static ossl_unused ossl_inline type *lh_##type##_delete(LHASH_OF(type) *lh, const type *d) \
    { \
        return (type *)OPENSSL_LH_delete((OPENSSL_LHASH *)lh, d); \
    } \
    static ossl_unused ossl_inline type *lh_##type##_retrieve(LHASH_OF(type) *lh, const type *d) \
    { \
        return (type *)OPENSSL_LH_retrieve((OPENSSL_LHASH *)lh, d); \
    } \
    static ossl_unused ossl_inline int lh_##type##_error(LHASH_OF(type) *lh) \
    { \
        return OPENSSL_LH_error((OPENSSL_LHASH *)lh); \
    } \
    static ossl_unused ossl_inline unsigned long lh_##type##_num_items(LHASH_OF(type) *lh) \
    { \
        return OPENSSL_LH_num_items((OPENSSL_LHASH *)lh); \
    } \
    static ossl_unused ossl_inline void lh_##type##_node_stats_bio(const LHASH_OF(type) *lh, BIO *out) \
    { \
        OPENSSL_LH_node_stats_bio((const OPENSSL_LHASH *)lh, out); \
    } \
    static ossl_unused ossl_inline void lh_##type##_node_usage_stats_bio(const LHASH_OF(type) *lh, BIO *out) \
    { \
        OPENSSL_LH_node_usage_stats_bio((const OPENSSL_LHASH *)lh, out); \
    } \
    static ossl_unused ossl_inline void lh_##type##_stats_bio(const LHASH_OF(type) *lh, BIO *out) \
    { \
        OPENSSL_LH_stats_bio((const OPENSSL_LHASH *)lh, out); \
    } \
    static ossl_unused ossl_inline unsigned long lh_##type##_get_down_load(LHASH_OF(type) *lh) \
    { \
        return OPENSSL_LH_get_down_load((OPENSSL_LHASH *)lh); \
    } \
    static ossl_unused ossl_inline void lh_##type##_set_down_load(LHASH_OF(type) *lh, unsigned long dl) \
    { \
        OPENSSL_LH_set_down_load((OPENSSL_LHASH *)lh, dl); \
    } \
    static ossl_unused ossl_inline void lh_##type##_doall(LHASH_OF(type) *lh, \
                                                          void (*doall)(type *)) \
    { \
        OPENSSL_LH_doall((OPENSSL_LHASH *)lh, (OPENSSL_LH_DOALL_FUNC)doall); \
    } \
    LHASH_OF(type)

#define IMPLEMENT_LHASH_DOALL_ARG_CONST(type, argtype) \
    int_implement_lhash_doall(type, argtype, const type)

#define IMPLEMENT_LHASH_DOALL_ARG(type, argtype) \
    int_implement_lhash_doall(type, argtype, type)

#define int_implement_lhash_doall(type, argtype, cbargtype) \
    static ossl_unused ossl_inline void \
        lh_##type##_doall_##argtype(LHASH_OF(type) *lh, \
                                   void (*fn)(cbargtype *, argtype *), \
                                   argtype *arg) \
    { \
        OPENSSL_LH_doall_arg((OPENSSL_LHASH *)lh, (OPENSSL_LH_DOALL_FUNCARG)fn, (void *)arg); \
    } \
    LHASH_OF(type)

DEFINE_LHASH_OF(OPENSSL_STRING);
# ifdef _MSC_VER
/*
 * push and pop this warning:
 *   warning C4090: 'function': different 'const' qualifiers
 */
#  pragma warning (push)
#  pragma warning (disable: 4090)
# endif

DEFINE_LHASH_OF(OPENSSL_CSTRING);

# ifdef _MSC_VER
#  pragma warning (pop)
# endif

/*
 * If called without higher optimization (min. -xO3) the Oracle Developer
 * Studio compiler generates code for the defined (static inline) functions
 * above.
 * This would later lead to the linker complaining about missing symbols when
 * this header file is included but the resulting object is not linked against
 * the Crypto library (openssl#6912).
 */
# ifdef __SUNPRO_C
#  pragma weak OPENSSL_LH_new
#  pragma weak OPENSSL_LH_free
#  pragma weak OPENSSL_LH_insert
#  pragma weak OPENSSL_LH_delete
#  pragma weak OPENSSL_LH_retrieve
#  pragma weak OPENSSL_LH_error
#  pragma weak OPENSSL_LH_num_items
#  pragma weak OPENSSL_LH_node_stats_bio
#  pragma weak OPENSSL_LH_node_usage_stats_bio
#  pragma weak OPENSSL_LH_stats_bio
#  pragma weak OPENSSL_LH_get_down_load
#  pragma weak OPENSSL_LH_set_down_load
#  pragma weak OPENSSL_LH_doall
#  pragma weak OPENSSL_LH_doall_arg
# endif /* __SUNPRO_C */

#ifdef  __cplusplus
}
#endif

#endif
