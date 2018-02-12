/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/unistd.h"
 */

#ifndef _UAPI_ASM_S390_UNISTD_H_
#define _UAPI_ASM_S390_UNISTD_H_

#ifdef __s390x__
#include <asm/unistd_64.h>
#else
#include <asm/unistd_32.h>
#endif

#endif /* _UAPI_ASM_S390_UNISTD_H_ */
