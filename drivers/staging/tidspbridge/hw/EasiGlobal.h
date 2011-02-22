/*
 * EasiGlobal.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _EASIGLOBAL_H
#define _EASIGLOBAL_H
#include <linux/types.h>

/*
 * DEFINE:        READ_ONLY, WRITE_ONLY &  READ_WRITE
 *
 * DESCRIPTION: Defines used to describe register types for EASI-checker tests.
 */

#define READ_ONLY    1
#define WRITE_ONLY   2
#define READ_WRITE   3

/*
 * MACRO:        _DEBUG_LEVEL1_EASI
 *
 * DESCRIPTION:  A MACRO which can be used to indicate that a particular beach
 *               register access function was called.
 *
 * NOTE:         We currently dont use this functionality.
 */
#define _DEBUG_LEVEL1_EASI(easi_num)     ((void)0)

#endif /* _EASIGLOBAL_H */
