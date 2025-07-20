/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_STUB_DATA_H
#define __ARCH_STUB_DATA_H

#ifdef __i386__
#include <generated/asm-offsets.h>
#include <asm/ldt.h>

struct stub_data_arch {
	int sync;
	struct user_desc tls[UM_KERN_GDT_ENTRY_TLS_ENTRIES];
};
#else
#define STUB_SYNC_FS_BASE (1 << 0)
#define STUB_SYNC_GS_BASE (1 << 1)
struct stub_data_arch {
	int sync;
	unsigned long fs_base;
	unsigned long gs_base;
};
#endif

#endif /* __ARCH_STUB_DATA_H */
