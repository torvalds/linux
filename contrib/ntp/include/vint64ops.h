/*
 * vint64ops.h - operations on 'vint64' values
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 * ----------------------------------------------------------------------
 * This is an attempt to get the vint64 calculations stuff centralised.
 */
#ifndef VINT64OPS_H
#define VINT64OPS_H

/* signed/unsigned compare. returns 1/0/-1 if lhs >/=/< rhs */
extern int icmpv64(const vint64 * lhs,	const vint64 * rhs);
extern int ucmpv64(const vint64 * lhs,	const vint64 * rhs);

/* add / subtract */
extern vint64 addv64(const vint64 *lhs, const vint64 *rhs);
extern vint64 addv64i32(const vint64 * lhs, int32_t rhs);
extern vint64 addv64u32(const vint64 * lhs, uint32_t rhs);

extern vint64 subv64(const vint64 *lhs, const vint64 *rhs);
extern vint64 subv64i32(const vint64 * lhs, int32_t rhs);
extern vint64 subv64u32(const vint64 * lhs, uint32_t rhs);

/* parsing. works like strtoul() or strtoull() */
extern vint64 strtouv64(const char * begp, char ** endp, int base);

#endif /*!defined(VINT64OPS_H)*/
