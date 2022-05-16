/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_H__
#define __S390_VDSO_H__

#include <vdso/datapage.h>

/* Default link addresses for the vDSOs */
#define VDSO32_LBASE	0
#define VDSO64_LBASE	0

#define VDSO_VERSION_STRING	LINUX_2.6.29

#ifndef __ASSEMBLY__

/*
 * Note about the vdso_data and vdso_per_cpu_data structures:
 *
 * NEVER USE THEM IN USERSPACE CODE DIRECTLY. The layout of the
 * structure is supposed to be known only to the function in the vdso
 * itself and may change without notice.
 */

struct vdso_per_cpu_data {
	/*
	 * Note: node_id and cpu_nr must be at adjacent memory locations.
	 * VDSO userspace must read both values with a single instruction.
	 */
	union {
		__u64 getcpu_val;
		struct {
			__u32 node_id;
			__u32 cpu_nr;
		};
	};
};

extern struct vdso_data *vdso_data;

int vdso_alloc_per_cpu(struct lowcore *lowcore);
void vdso_free_per_cpu(struct lowcore *lowcore);

#endif /* __ASSEMBLY__ */
#endif /* __S390_VDSO_H__ */
