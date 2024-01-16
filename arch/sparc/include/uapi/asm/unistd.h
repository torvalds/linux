/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * System calls under the Sparc.
 *
 * Don't be scared by the ugly clobbers, it is the only way I can
 * think of right now to force the arguments into fixed registers
 * before the trap into the system call with gcc 'asm' statements.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 *
 * SunOS compatibility based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */
#ifndef _UAPI_SPARC_UNISTD_H
#define _UAPI_SPARC_UNISTD_H

#ifndef __32bit_syscall_numbers__
#ifndef __arch64__
#define __32bit_syscall_numbers__
#endif
#endif

#ifdef __arch64__
#include <asm/unistd_64.h>
#else
#include <asm/unistd_32.h>
#endif

/* Bitmask values returned from kern_features system call.  */
#define KERN_FEATURE_MIXED_MODE_STACK	0x00000001

#endif /* _UAPI_SPARC_UNISTD_H */
