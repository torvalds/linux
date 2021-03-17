/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_ASM_VDSO_DATA_H
#define __S390_ASM_VDSO_DATA_H

#include <linux/types.h>
#include <vdso/datapage.h>

struct arch_vdso_data {
	__u64 tod_steering_delta;
	__u64 tod_steering_end;
};

#endif /* __S390_ASM_VDSO_DATA_H */
