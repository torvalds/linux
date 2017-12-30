/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/siginfo.h"
 */

#ifndef _S390_SIGINFO_H
#define _S390_SIGINFO_H

#ifdef __s390x__
#define __ARCH_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

#endif
