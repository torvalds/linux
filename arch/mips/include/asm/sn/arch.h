/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995 - 1997, 1999 Silcon Graphics, Inc.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_SN_ARCH_H
#define _ASM_SN_ARCH_H

#include <linux/types.h>
#include <asm/sn/types.h>
#ifdef CONFIG_SGI_IP27
#include <asm/sn/sn0/arch.h>
#endif

#define cputonasid(cpu)		(sn_cpu_info[(cpu)].p_nasid)
#define cputoslice(cpu)		(sn_cpu_info[(cpu)].p_slice)

#define INVALID_NASID		(nasid_t)-1
#define INVALID_PNODEID		(pnodeid_t)-1
#define INVALID_MODULE		(moduleid_t)-1
#define INVALID_PARTID		(partid_t)-1

#endif /* _ASM_SN_ARCH_H */
