/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_S390_TPI_H
#define _ASM_S390_TPI_H

#include <linux/types.h>
#include <uapi/asm/schid.h>

#ifndef __ASSEMBLY__

/* I/O-Interruption Code as stored by TEST PENDING INTERRUPTION (TPI). */
struct tpi_info {
	struct subchannel_id schid;
	u32 intparm;
	u32 adapter_IO:1;
	u32 directed_irq:1;
	u32 isc:3;
	u32 :12;
	u32 type:3;
	u32 :12;
} __packed __aligned(4);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_TPI_H */
