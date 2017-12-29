/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _GUARDED_STORAGE_H
#define _GUARDED_STORAGE_H

#include <linux/types.h>

struct gs_cb {
	__u64 reserved;
	__u64 gsd;
	__u64 gssm;
	__u64 gs_epl_a;
};

struct gs_epl {
	__u8 pad1;
	union {
		__u8 gs_eam;
		struct {
			__u8	: 6;
			__u8 e	: 1;
			__u8 b	: 1;
		};
	};
	union {
		__u8 gs_eci;
		struct {
			__u8 tx	: 1;
			__u8 cx	: 1;
			__u8	: 5;
			__u8 in	: 1;
		};
	};
	union {
		__u8 gs_eai;
		struct {
			__u8	: 1;
			__u8 t	: 1;
			__u8 as	: 2;
			__u8 ar	: 4;
		};
	};
	__u32 pad2;
	__u64 gs_eha;
	__u64 gs_eia;
	__u64 gs_eoa;
	__u64 gs_eir;
	__u64 gs_era;
};

#define GS_ENABLE	0
#define	GS_DISABLE	1
#define GS_SET_BC_CB	2
#define GS_CLEAR_BC_CB	3
#define GS_BROADCAST	4

static inline void load_gs_cb(struct gs_cb *gs_cb)
{
	asm volatile(".insn rxy,0xe3000000004d,0,%0" : : "Q" (*gs_cb));
}

static inline void store_gs_cb(struct gs_cb *gs_cb)
{
	asm volatile(".insn rxy,0xe30000000049,0,%0" : : "Q" (*gs_cb));
}

static inline void save_gs_cb(struct gs_cb *gs_cb)
{
	if (gs_cb)
		store_gs_cb(gs_cb);
}

static inline void restore_gs_cb(struct gs_cb *gs_cb)
{
	if (gs_cb)
		load_gs_cb(gs_cb);
}

#endif /* _GUARDED_STORAGE_H */
