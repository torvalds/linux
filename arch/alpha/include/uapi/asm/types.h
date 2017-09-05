#ifndef _UAPI_ALPHA_TYPES_H
#define _UAPI_ALPHA_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */

/*
 * This is here because we used to use l64 for alpha
 * and we don't want to impact user mode with our change to ll64
 * in the kernel.
 *
 * However, some user programs are fine with this.  They can
 * flag __SANE_USERSPACE_TYPES__ to get int-ll64.h here.
 */
#if !defined(__SANE_USERSPACE_TYPES__) && !defined(__KERNEL__)
#include <asm-generic/int-l64.h>
#else
#include <asm-generic/int-ll64.h>
#endif

#endif /* _UAPI_ALPHA_TYPES_H */
