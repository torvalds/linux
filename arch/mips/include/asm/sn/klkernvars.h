/*
 * File ported from IRIX to Linux by Kanoj Sarcar, 06/08/00.
 * Copyright 2000 Silicon Graphics, Inc.
 */
#ifndef __ASM_SN_KLKERNVARS_H
#define __ASM_SN_KLKERNVARS_H

#define KV_MAGIC_OFFSET		0x0
#define KV_RO_NASID_OFFSET	0x4
#define KV_RW_NASID_OFFSET	0x6

#define KV_MAGIC		0x5f4b565f

#ifndef __ASSEMBLY__

#include <asm/sn/types.h>

typedef struct kern_vars_s {
	int		kv_magic;
	nasid_t		kv_ro_nasid;
	nasid_t		kv_rw_nasid;
	unsigned long	kv_ro_baseaddr;
	unsigned long	kv_rw_baseaddr;
} kern_vars_t;

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_SN_KLKERNVARS_H */
