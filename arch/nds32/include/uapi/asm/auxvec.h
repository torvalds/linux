/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_AUXVEC_H
#define __ASM_AUXVEC_H

/*
 * This entry gives some information about the FPU initialization
 * performed by the kernel.
 */
#define AT_FPUCW	18	/* Used FPU control word.  */


/* VDSO location */
#define AT_SYSINFO_EHDR	33

#define AT_VECTOR_SIZE_ARCH 1

#endif
