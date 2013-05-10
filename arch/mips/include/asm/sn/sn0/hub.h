/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 1999 Silicon Graphics, Inc.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_SN_SN0_HUB_H
#define _ASM_SN_SN0_HUB_H

/* The secret password; used to release protection */
#define HUB_PASSWORD		0x53474972756c6573ull

#define CHIPID_HUB		0
#define CHIPID_ROUTER		1

#define HUB_REV_1_0		1
#define HUB_REV_2_0		2
#define HUB_REV_2_1		3
#define HUB_REV_2_2		4
#define HUB_REV_2_3             5
#define HUB_REV_2_4             6

#define MAX_HUB_PATH		80

#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubpi.h>
#include <asm/sn/sn0/hubmd.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/sn0/hubni.h>
//#include <asm/sn/sn0/hubcore.h>

/* Translation of uncached attributes */
#define	UATTR_HSPEC	0
#define	UATTR_IO	1
#define	UATTR_MSPEC	2
#define	UATTR_UNCAC	3

#endif /* _ASM_SN_SN0_HUB_H */
