/*
 * asm-s390/kvm_host.h - definition for kernel virtual machines on s390
 *
 * Copyright IBM Corp. 2008,2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */


#ifndef ASM_KVM_HOST_H
#define ASM_KVM_HOST_H
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <asm/debug.h>
#include <asm/cpu.h>

#define KVM_MAX_VCPUS 64
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

struct sca_entry {
	atomic_t scn;
	__u32	reserved;
	__u64	sda;
	__u64	reserved2[2];
} __attribute__((packed));


struct sca_block {
	__u64	ipte_control;
	__u64	reserved[5];
	__u64	mcn;
	__u64	reserved2;
	struct sca_entry cpu[64];
} __attribute__((packed));

#define KVM_NR_PAGE_SIZES 2
#define KVM_HPAGE_GFN_SHIFT(x) (((x) - 1) * 8)
#define KVM_HPAGE_SHIFT(x) (PAGE_SHIFT + KVM_HPAGE_GFN_SHIFT(x))
#define KVM_HPAGE_SIZE(x) (1UL << KVM_HPAGE_SHIFT(x))
#define KVM_HPAGE_MASK(x)	(~(KVM_HPAGE_SIZE(x) - 1))
#define KVM_PAGES_PER_HPAGE(x)	(KVM_HPAGE_SIZE(x) / PAGE_SIZE)

#define CPUSTAT_HOST       0x80000000
#define CPUSTAT_WAIT       0x10000000
#define CPUSTAT_ECALL_PEND 0x08000000
#define CPUSTAT_STOP_INT   0x04000000
#define CPUSTAT_IO_INT     0x02000000
#define CPUSTAT_EXT_INT    0x01000000
#define CPUSTAT_RUNNING    0x00800000
#define CPUSTAT_RETAINED   0x00400000
#define CPUSTAT_TIMING_SUB 0x00020000
#define CPUSTAT_SIE_SUB    0x00010000
#define CPUSTAT_RRF        0x00008000
#define CPUSTAT_SLSV       0x00004000
#define CPUSTAT_SLSR       0x00002000
#define CPUSTAT_ZARCH      0x00000800
#define CPUSTAT_MCDS       0x00000100
#define CPUSTAT_SM         0x00000080
#define CPUSTAT_G          0x00000008
#define CPUSTAT_J          0x00000002
#define CPUSTAT_P          0x00000001

struct kvm_s390_sie_block {
	atomic_t cpuflags;		/* 0x0000 */
	__u32	prefix;			/* 0x0004 */
	__u8	reserved8[32];		/* 0x0008 */
	__u64	cputm;			/* 0x0028 */
	__u64	ckc;			/* 0x0030 */
	__u64	epoch;			/* 0x0038 */
	__u8	reserved40[4];		/* 0x0040 */
#define LCTL_CR0	0x8000
	__u16   lctl;			/* 0x0044 */
	__s16	icpua;			/* 0x0046 */
	__u32	ictl;			/* 0x0048 */
	__u32	eca;			/* 0x004c */
	__u8	icptcode;		/* 0x0050 */
	__u8	reserved51;		/* 0x0051 */
	__u16	ihcpu;			/* 0x0052 */
	__u8	reserved54[2];		/* 0x0054 */
	__u16	ipa;			/* 0x0056 */
	__u32	ipb;			/* 0x0058 */
	__u32	scaoh;			/* 0x005c */
	__u8	reserved60;		/* 0x0060 */
	__u8	ecb;			/* 0x0061 */
	__u8	reserved62[2];		/* 0x0062 */
	__u32	scaol;			/* 0x0064 */
	__u8	reserved68[4];		/* 0x0068 */
	__u32	todpr;			/* 0x006c */
	__u8	reserved70[32];		/* 0x0070 */
	psw_t	gpsw;			/* 0x0090 */
	__u64	gg14;			/* 0x00a0 */
	__u64	gg15;			/* 0x00a8 */
	__u8	reservedb0[30];		/* 0x00b0 */
	__u16   iprcc;			/* 0x00ce */
	__u8	reservedd0[48];		/* 0x00d0 */
	__u64	gcr[16];		/* 0x0100 */
	__u64	gbea;			/* 0x0180 */
	__u8	reserved188[24];	/* 0x0188 */
	__u32	fac;			/* 0x01a0 */
	__u8	reserved1a4[92];	/* 0x01a4 */
} __attribute__((packed));

struct kvm_vcpu_stat {
	u32 exit_userspace;
	u32 exit_null;
	u32 exit_external_request;
	u32 exit_external_interrupt;
	u32 exit_stop_request;
	u32 exit_validity;
	u32 exit_instruction;
	u32 instruction_lctl;
	u32 instruction_lctlg;
	u32 exit_program_interruption;
	u32 exit_instr_and_program;
	u32 deliver_emergency_signal;
	u32 deliver_service_signal;
	u32 deliver_virtio_interrupt;
	u32 deliver_stop_signal;
	u32 deliver_prefix_signal;
	u32 deliver_restart_signal;
	u32 deliver_program_int;
	u32 exit_wait_state;
	u32 instruction_stidp;
	u32 instruction_spx;
	u32 instruction_stpx;
	u32 instruction_stap;
	u32 instruction_storage_key;
	u32 instruction_stsch;
	u32 instruction_chsc;
	u32 instruction_stsi;
	u32 instruction_stfl;
	u32 instruction_tprot;
	u32 instruction_sigp_sense;
	u32 instruction_sigp_emergency;
	u32 instruction_sigp_stop;
	u32 instruction_sigp_arch;
	u32 instruction_sigp_prefix;
	u32 instruction_sigp_restart;
	u32 diagnose_44;
};

struct kvm_s390_io_info {
	__u16        subchannel_id;            /* 0x0b8 */
	__u16        subchannel_nr;            /* 0x0ba */
	__u32        io_int_parm;              /* 0x0bc */
	__u32        io_int_word;              /* 0x0c0 */
};

struct kvm_s390_ext_info {
	__u32 ext_params;
	__u64 ext_params2;
};

#define PGM_OPERATION            0x01
#define PGM_PRIVILEGED_OPERATION 0x02
#define PGM_EXECUTE              0x03
#define PGM_PROTECTION           0x04
#define PGM_ADDRESSING           0x05
#define PGM_SPECIFICATION        0x06
#define PGM_DATA                 0x07

struct kvm_s390_pgm_info {
	__u16 code;
};

struct kvm_s390_prefix_info {
	__u32 address;
};

struct kvm_s390_emerg_info {
	__u16 code;
};

struct kvm_s390_interrupt_info {
	struct list_head list;
	u64	type;
	union {
		struct kvm_s390_io_info io;
		struct kvm_s390_ext_info ext;
		struct kvm_s390_pgm_info pgm;
		struct kvm_s390_emerg_info emerg;
		struct kvm_s390_prefix_info prefix;
	};
};

/* for local_interrupt.action_flags */
#define ACTION_STORE_ON_STOP		(1<<0)
#define ACTION_STOP_ON_STOP		(1<<1)
#define ACTION_RELOADVCPU_ON_STOP	(1<<2)

struct kvm_s390_local_interrupt {
	spinlock_t lock;
	struct list_head list;
	atomic_t active;
	struct kvm_s390_float_interrupt *float_int;
	int timer_due; /* event indicator for waitqueue below */
	wait_queue_head_t wq;
	atomic_t *cpuflags;
	unsigned int action_bits;
};

struct kvm_s390_float_interrupt {
	spinlock_t lock;
	struct list_head list;
	atomic_t active;
	int next_rr_cpu;
	unsigned long idle_mask [(64 + sizeof(long) - 1) / sizeof(long)];
	struct kvm_s390_local_interrupt *local_int[64];
};


struct kvm_vcpu_arch {
	struct kvm_s390_sie_block *sie_block;
	unsigned long	  guest_gprs[16];
	s390_fp_regs      host_fpregs;
	unsigned int      host_acrs[NUM_ACRS];
	s390_fp_regs      guest_fpregs;
	unsigned int      guest_acrs[NUM_ACRS];
	struct kvm_s390_local_interrupt local_int;
	struct hrtimer    ckc_timer;
	struct tasklet_struct tasklet;
	union  {
		struct cpuid	cpu_id;
		u64		stidp_data;
	};
	struct gmap *gmap;
};

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_arch{
	struct sca_block *sca;
	debug_info_t *dbf;
	struct kvm_s390_float_interrupt float_int;
	struct gmap *gmap;
};

extern int sie64a(struct kvm_s390_sie_block *, unsigned long *);
#endif
