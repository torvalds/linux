/*
 * kmp_error.h -- PTS functions for error checking at runtime.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_ERROR_H
#define KMP_ERROR_H

#include "kmp_i18n.h"

/* ------------------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif

void __kmp_error_construct(kmp_i18n_id_t id, enum cons_type ct,
                           ident_t const *ident);
void __kmp_error_construct2(kmp_i18n_id_t id, enum cons_type ct,
                            ident_t const *ident, struct cons_data const *cons);

struct cons_header *__kmp_allocate_cons_stack(int gtid);
void __kmp_free_cons_stack(void *ptr);

void __kmp_push_parallel(int gtid, ident_t const *ident);
void __kmp_push_workshare(int gtid, enum cons_type ct, ident_t const *ident);
#if KMP_USE_DYNAMIC_LOCK
void __kmp_push_sync(int gtid, enum cons_type ct, ident_t const *ident,
                     kmp_user_lock_p name, kmp_uint32);
#else
void __kmp_push_sync(int gtid, enum cons_type ct, ident_t const *ident,
                     kmp_user_lock_p name);
#endif

void __kmp_check_workshare(int gtid, enum cons_type ct, ident_t const *ident);
#if KMP_USE_DYNAMIC_LOCK
void __kmp_check_sync(int gtid, enum cons_type ct, ident_t const *ident,
                      kmp_user_lock_p name, kmp_uint32);
#else
void __kmp_check_sync(int gtid, enum cons_type ct, ident_t const *ident,
                      kmp_user_lock_p name);
#endif

void __kmp_pop_parallel(int gtid, ident_t const *ident);
enum cons_type __kmp_pop_workshare(int gtid, enum cons_type ct,
                                   ident_t const *ident);
void __kmp_pop_sync(int gtid, enum cons_type ct, ident_t const *ident);
void __kmp_check_barrier(int gtid, enum cons_type ct, ident_t const *ident);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KMP_ERROR_H
