/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _S390_UAPI_RUNTIME_INSTR_H
#define _S390_UAPI_RUNTIME_INSTR_H

#include <linux/types.h>

#define S390_RUNTIME_INSTR_START	0x1
#define S390_RUNTIME_INSTR_STOP		0x2

struct runtime_instr_cb {
	__u64 rca;
	__u64 roa;
	__u64 rla;

	__u32 v			: 1;
	__u32 s			: 1;
	__u32 k			: 1;
	__u32 h			: 1;
	__u32 a			: 1;
	__u32 reserved1		: 3;
	__u32 ps		: 1;
	__u32 qs		: 1;
	__u32 pc		: 1;
	__u32 qc		: 1;
	__u32 reserved2		: 1;
	__u32 g			: 1;
	__u32 u			: 1;
	__u32 l			: 1;
	__u32 key		: 4;
	__u32 reserved3		: 8;
	__u32 t			: 1;
	__u32 rgs		: 3;

	__u32 m			: 4;
	__u32 n			: 1;
	__u32 mae		: 1;
	__u32 reserved4		: 2;
	__u32 c			: 1;
	__u32 r			: 1;
	__u32 b			: 1;
	__u32 j			: 1;
	__u32 e			: 1;
	__u32 x			: 1;
	__u32 reserved5		: 2;
	__u32 bpxn		: 1;
	__u32 bpxt		: 1;
	__u32 bpti		: 1;
	__u32 bpni		: 1;
	__u32 reserved6		: 2;

	__u32 d			: 1;
	__u32 f			: 1;
	__u32 ic		: 4;
	__u32 dc		: 4;

	__u64 reserved7;
	__u64 sf;
	__u64 rsic;
	__u64 reserved8;
} __attribute__((__packed__, __aligned__(8)));

static inline void load_runtime_instr_cb(struct runtime_instr_cb *cb)
{
	asm volatile(".insn	rsy,0xeb0000000060,0,0,%0"	/* LRIC */
		: : "Q" (*cb));
}

static inline void store_runtime_instr_cb(struct runtime_instr_cb *cb)
{
	asm volatile(".insn	rsy,0xeb0000000061,0,0,%0"	/* STRIC */
		: "=Q" (*cb) : : "cc");
}

#endif /* _S390_UAPI_RUNTIME_INSTR_H */
