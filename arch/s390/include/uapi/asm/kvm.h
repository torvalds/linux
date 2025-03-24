/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_KVM_S390_H
#define __LINUX_KVM_S390_H
/*
 * KVM s390 specific structures and definitions
 *
 * Copyright IBM Corp. 2008, 2018
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */
#include <linux/types.h>

#define __KVM_S390

struct kvm_s390_skeys {
	__u64 start_gfn;
	__u64 count;
	__u64 skeydata_addr;
	__u32 flags;
	__u32 reserved[9];
};

#define KVM_S390_CMMA_PEEK (1 << 0)

/**
 * kvm_s390_cmma_log - Used for CMMA migration.
 *
 * Used both for input and output.
 *
 * @start_gfn: Guest page number to start from.
 * @count: Size of the result buffer.
 * @flags: Control operation mode via KVM_S390_CMMA_* flags
 * @remaining: Used with KVM_S390_GET_CMMA_BITS. Indicates how many dirty
 *             pages are still remaining.
 * @mask: Used with KVM_S390_SET_CMMA_BITS. Bitmap of bits to actually set
 *        in the PGSTE.
 * @values: Pointer to the values buffer.
 *
 * Used in KVM_S390_{G,S}ET_CMMA_BITS ioctls.
 */
struct kvm_s390_cmma_log {
	__u64 start_gfn;
	__u32 count;
	__u32 flags;
	union {
		__u64 remaining;
		__u64 mask;
	};
	__u64 values;
};

#define KVM_S390_RESET_POR       1
#define KVM_S390_RESET_CLEAR     2
#define KVM_S390_RESET_SUBSYSTEM 4
#define KVM_S390_RESET_CPU_INIT  8
#define KVM_S390_RESET_IPL       16

/* for KVM_S390_MEM_OP */
struct kvm_s390_mem_op {
	/* in */
	__u64 gaddr;		/* the guest address */
	__u64 flags;		/* flags */
	__u32 size;		/* amount of bytes */
	__u32 op;		/* type of operation */
	__u64 buf;		/* buffer in userspace */
	union {
		struct {
			__u8 ar;	/* the access register number */
			__u8 key;	/* access key, ignored if flag unset */
			__u8 pad1[6];	/* ignored */
			__u64 old_addr;	/* ignored if cmpxchg flag unset */
		};
		__u32 sida_offset; /* offset into the sida */
		__u8 reserved[32]; /* ignored */
	};
};
/* types for kvm_s390_mem_op->op */
#define KVM_S390_MEMOP_LOGICAL_READ	0
#define KVM_S390_MEMOP_LOGICAL_WRITE	1
#define KVM_S390_MEMOP_SIDA_READ	2
#define KVM_S390_MEMOP_SIDA_WRITE	3
#define KVM_S390_MEMOP_ABSOLUTE_READ	4
#define KVM_S390_MEMOP_ABSOLUTE_WRITE	5
#define KVM_S390_MEMOP_ABSOLUTE_CMPXCHG	6

/* flags for kvm_s390_mem_op->flags */
#define KVM_S390_MEMOP_F_CHECK_ONLY		(1ULL << 0)
#define KVM_S390_MEMOP_F_INJECT_EXCEPTION	(1ULL << 1)
#define KVM_S390_MEMOP_F_SKEY_PROTECTION	(1ULL << 2)

/* flags specifying extension support via KVM_CAP_S390_MEM_OP_EXTENSION */
#define KVM_S390_MEMOP_EXTENSION_CAP_BASE	(1 << 0)
#define KVM_S390_MEMOP_EXTENSION_CAP_CMPXCHG	(1 << 1)

struct kvm_s390_psw {
	__u64 mask;
	__u64 addr;
};

/* valid values for type in kvm_s390_interrupt */
#define KVM_S390_SIGP_STOP		0xfffe0000u
#define KVM_S390_PROGRAM_INT		0xfffe0001u
#define KVM_S390_SIGP_SET_PREFIX	0xfffe0002u
#define KVM_S390_RESTART		0xfffe0003u
#define KVM_S390_INT_PFAULT_INIT	0xfffe0004u
#define KVM_S390_INT_PFAULT_DONE	0xfffe0005u
#define KVM_S390_MCHK			0xfffe1000u
#define KVM_S390_INT_CLOCK_COMP		0xffff1004u
#define KVM_S390_INT_CPU_TIMER		0xffff1005u
#define KVM_S390_INT_VIRTIO		0xffff2603u
#define KVM_S390_INT_SERVICE		0xffff2401u
#define KVM_S390_INT_EMERGENCY		0xffff1201u
#define KVM_S390_INT_EXTERNAL_CALL	0xffff1202u
/* Anything below 0xfffe0000u is taken by INT_IO */
#define KVM_S390_INT_IO(ai,cssid,ssid,schid)   \
	(((schid)) |			       \
	 ((ssid) << 16) |		       \
	 ((cssid) << 18) |		       \
	 ((ai) << 26))
#define KVM_S390_INT_IO_MIN		0x00000000u
#define KVM_S390_INT_IO_MAX		0xfffdffffu
#define KVM_S390_INT_IO_AI_MASK		0x04000000u


struct kvm_s390_interrupt {
	__u32 type;
	__u32 parm;
	__u64 parm64;
};

struct kvm_s390_io_info {
	__u16 subchannel_id;
	__u16 subchannel_nr;
	__u32 io_int_parm;
	__u32 io_int_word;
};

struct kvm_s390_ext_info {
	__u32 ext_params;
	__u32 pad;
	__u64 ext_params2;
};

struct kvm_s390_pgm_info {
	__u64 trans_exc_code;
	__u64 mon_code;
	__u64 per_address;
	__u32 data_exc_code;
	__u16 code;
	__u16 mon_class_nr;
	__u8 per_code;
	__u8 per_atmid;
	__u8 exc_access_id;
	__u8 per_access_id;
	__u8 op_access_id;
#define KVM_S390_PGM_FLAGS_ILC_VALID	0x01
#define KVM_S390_PGM_FLAGS_ILC_0	0x02
#define KVM_S390_PGM_FLAGS_ILC_1	0x04
#define KVM_S390_PGM_FLAGS_ILC_MASK	0x06
#define KVM_S390_PGM_FLAGS_NO_REWIND	0x08
	__u8 flags;
	__u8 pad[2];
};

struct kvm_s390_prefix_info {
	__u32 address;
};

struct kvm_s390_extcall_info {
	__u16 code;
};

struct kvm_s390_emerg_info {
	__u16 code;
};

#define KVM_S390_STOP_FLAG_STORE_STATUS	0x01
struct kvm_s390_stop_info {
	__u32 flags;
};

struct kvm_s390_mchk_info {
	__u64 cr14;
	__u64 mcic;
	__u64 failing_storage_address;
	__u32 ext_damage_code;
	__u32 pad;
	__u8 fixed_logout[16];
};

struct kvm_s390_irq {
	__u64 type;
	union {
		struct kvm_s390_io_info io;
		struct kvm_s390_ext_info ext;
		struct kvm_s390_pgm_info pgm;
		struct kvm_s390_emerg_info emerg;
		struct kvm_s390_extcall_info extcall;
		struct kvm_s390_prefix_info prefix;
		struct kvm_s390_stop_info stop;
		struct kvm_s390_mchk_info mchk;
		char reserved[64];
	} u;
};

struct kvm_s390_irq_state {
	__u64 buf;
	__u32 flags;        /* will stay unused for compatibility reasons */
	__u32 len;
	__u32 reserved[4];  /* will stay unused for compatibility reasons */
};

struct kvm_s390_ucas_mapping {
	__u64 user_addr;
	__u64 vcpu_addr;
	__u64 length;
};

struct kvm_s390_pv_sec_parm {
	__u64 origin;
	__u64 length;
};

struct kvm_s390_pv_unp {
	__u64 addr;
	__u64 size;
	__u64 tweak;
};

enum pv_cmd_dmp_id {
	KVM_PV_DUMP_INIT,
	KVM_PV_DUMP_CONFIG_STOR_STATE,
	KVM_PV_DUMP_COMPLETE,
	KVM_PV_DUMP_CPU,
};

struct kvm_s390_pv_dmp {
	__u64 subcmd;
	__u64 buff_addr;
	__u64 buff_len;
	__u64 gaddr;		/* For dump storage state */
	__u64 reserved[4];
};

enum pv_cmd_info_id {
	KVM_PV_INFO_VM,
	KVM_PV_INFO_DUMP,
};

struct kvm_s390_pv_info_dump {
	__u64 dump_cpu_buffer_len;
	__u64 dump_config_mem_buffer_per_1m;
	__u64 dump_config_finalize_len;
};

struct kvm_s390_pv_info_vm {
	__u64 inst_calls_list[4];
	__u64 max_cpus;
	__u64 max_guests;
	__u64 max_guest_addr;
	__u64 feature_indication;
};

struct kvm_s390_pv_info_header {
	__u32 id;
	__u32 len_max;
	__u32 len_written;
	__u32 reserved;
};

struct kvm_s390_pv_info {
	struct kvm_s390_pv_info_header header;
	union {
		struct kvm_s390_pv_info_dump dump;
		struct kvm_s390_pv_info_vm vm;
	};
};

enum pv_cmd_id {
	KVM_PV_ENABLE,
	KVM_PV_DISABLE,
	KVM_PV_SET_SEC_PARMS,
	KVM_PV_UNPACK,
	KVM_PV_VERIFY,
	KVM_PV_PREP_RESET,
	KVM_PV_UNSHARE_ALL,
	KVM_PV_INFO,
	KVM_PV_DUMP,
	KVM_PV_ASYNC_CLEANUP_PREPARE,
	KVM_PV_ASYNC_CLEANUP_PERFORM,
};

struct kvm_pv_cmd {
	__u32 cmd;	/* Command to be executed */
	__u16 rc;	/* Ultravisor return code */
	__u16 rrc;	/* Ultravisor return reason code */
	__u64 data;	/* Data or address */
	__u32 flags;    /* flags for future extensions. Must be 0 for now */
	__u32 reserved[3];
};

struct kvm_s390_zpci_op {
	/* in */
	__u32 fh;               /* target device */
	__u8  op;               /* operation to perform */
	__u8  pad[3];
	union {
		/* for KVM_S390_ZPCIOP_REG_AEN */
		struct {
			__u64 ibv;      /* Guest addr of interrupt bit vector */
			__u64 sb;       /* Guest addr of summary bit */
			__u32 flags;
			__u32 noi;      /* Number of interrupts */
			__u8 isc;       /* Guest interrupt subclass */
			__u8 sbo;       /* Offset of guest summary bit vector */
			__u16 pad;
		} reg_aen;
		__u64 reserved[8];
	} u;
};

/* types for kvm_s390_zpci_op->op */
#define KVM_S390_ZPCIOP_REG_AEN                0
#define KVM_S390_ZPCIOP_DEREG_AEN      1

/* flags for kvm_s390_zpci_op->u.reg_aen.flags */
#define KVM_S390_ZPCIOP_REGAEN_HOST    (1 << 0)

/* Device control API: s390-specific devices */
#define KVM_DEV_FLIC_GET_ALL_IRQS	1
#define KVM_DEV_FLIC_ENQUEUE		2
#define KVM_DEV_FLIC_CLEAR_IRQS		3
#define KVM_DEV_FLIC_APF_ENABLE		4
#define KVM_DEV_FLIC_APF_DISABLE_WAIT	5
#define KVM_DEV_FLIC_ADAPTER_REGISTER	6
#define KVM_DEV_FLIC_ADAPTER_MODIFY	7
#define KVM_DEV_FLIC_CLEAR_IO_IRQ	8
#define KVM_DEV_FLIC_AISM		9
#define KVM_DEV_FLIC_AIRQ_INJECT	10
#define KVM_DEV_FLIC_AISM_ALL		11
/*
 * We can have up to 4*64k pending subchannels + 8 adapter interrupts,
 * as well as up  to ASYNC_PF_PER_VCPU*KVM_MAX_VCPUS pfault done interrupts.
 * There are also sclp and machine checks. This gives us
 * sizeof(kvm_s390_irq)*(4*65536+8+64*64+1+1) = 72 * 266250 = 19170000
 * Lets round up to 8192 pages.
 */
#define KVM_S390_MAX_FLOAT_IRQS	266250
#define KVM_S390_FLIC_MAX_BUFFER	0x2000000

struct kvm_s390_io_adapter {
	__u32 id;
	__u8 isc;
	__u8 maskable;
	__u8 swap;
	__u8 flags;
};

#define KVM_S390_ADAPTER_SUPPRESSIBLE 0x01

struct kvm_s390_ais_req {
	__u8 isc;
	__u16 mode;
};

struct kvm_s390_ais_all {
	__u8 simm;
	__u8 nimm;
};

#define KVM_S390_IO_ADAPTER_MASK 1
#define KVM_S390_IO_ADAPTER_MAP 2
#define KVM_S390_IO_ADAPTER_UNMAP 3

struct kvm_s390_io_adapter_req {
	__u32 id;
	__u8 type;
	__u8 mask;
	__u16 pad0;
	__u64 addr;
};

/* kvm attr_group  on vm fd */
#define KVM_S390_VM_MEM_CTRL		0
#define KVM_S390_VM_TOD			1
#define KVM_S390_VM_CRYPTO		2
#define KVM_S390_VM_CPU_MODEL		3
#define KVM_S390_VM_MIGRATION		4
#define KVM_S390_VM_CPU_TOPOLOGY	5

/* kvm attributes for mem_ctrl */
#define KVM_S390_VM_MEM_ENABLE_CMMA	0
#define KVM_S390_VM_MEM_CLR_CMMA	1
#define KVM_S390_VM_MEM_LIMIT_SIZE	2

#define KVM_S390_NO_MEM_LIMIT		U64_MAX

/* kvm attributes for KVM_S390_VM_TOD */
#define KVM_S390_VM_TOD_LOW		0
#define KVM_S390_VM_TOD_HIGH		1
#define KVM_S390_VM_TOD_EXT		2

struct kvm_s390_vm_tod_clock {
	__u8  epoch_idx;
	__u64 tod;
};

/* kvm attributes for KVM_S390_VM_CPU_MODEL */
/* processor related attributes are r/w */
#define KVM_S390_VM_CPU_PROCESSOR	0
struct kvm_s390_vm_cpu_processor {
	__u64 cpuid;
	__u16 ibc;
	__u8  pad[6];
	__u64 fac_list[256];
};

/* machine related attributes are r/o */
#define KVM_S390_VM_CPU_MACHINE		1
struct kvm_s390_vm_cpu_machine {
	__u64 cpuid;
	__u32 ibc;
	__u8  pad[4];
	__u64 fac_mask[256];
	__u64 fac_list[256];
};

#define KVM_S390_VM_CPU_PROCESSOR_FEAT	2
#define KVM_S390_VM_CPU_MACHINE_FEAT	3

#define KVM_S390_VM_CPU_FEAT_NR_BITS	1024
#define KVM_S390_VM_CPU_FEAT_ESOP	0
#define KVM_S390_VM_CPU_FEAT_SIEF2	1
#define KVM_S390_VM_CPU_FEAT_64BSCAO	2
#define KVM_S390_VM_CPU_FEAT_SIIF	3
#define KVM_S390_VM_CPU_FEAT_GPERE	4
#define KVM_S390_VM_CPU_FEAT_GSLS	5
#define KVM_S390_VM_CPU_FEAT_IB		6
#define KVM_S390_VM_CPU_FEAT_CEI	7
#define KVM_S390_VM_CPU_FEAT_IBS	8
#define KVM_S390_VM_CPU_FEAT_SKEY	9
#define KVM_S390_VM_CPU_FEAT_CMMA	10
#define KVM_S390_VM_CPU_FEAT_PFMFI	11
#define KVM_S390_VM_CPU_FEAT_SIGPIF	12
#define KVM_S390_VM_CPU_FEAT_KSS	13
struct kvm_s390_vm_cpu_feat {
	__u64 feat[16];
};

#define KVM_S390_VM_CPU_PROCESSOR_SUBFUNC	4
#define KVM_S390_VM_CPU_MACHINE_SUBFUNC		5
/* for "test bit" instructions MSB 0 bit ordering, for "query" raw blocks */
struct kvm_s390_vm_cpu_subfunc {
	__u8 plo[32];		/* always */
	__u8 ptff[16];		/* with TOD-clock steering */
	__u8 kmac[16];		/* with MSA */
	__u8 kmc[16];		/* with MSA */
	__u8 km[16];		/* with MSA */
	__u8 kimd[16];		/* with MSA */
	__u8 klmd[16];		/* with MSA */
	__u8 pckmo[16];		/* with MSA3 */
	__u8 kmctr[16];		/* with MSA4 */
	__u8 kmf[16];		/* with MSA4 */
	__u8 kmo[16];		/* with MSA4 */
	__u8 pcc[16];		/* with MSA4 */
	__u8 ppno[16];		/* with MSA5 */
	__u8 kma[16];		/* with MSA8 */
	__u8 kdsa[16];		/* with MSA9 */
	__u8 sortl[32];		/* with STFLE.150 */
	__u8 dfltcc[32];	/* with STFLE.151 */
	__u8 pfcr[16];		/* with STFLE.201 */
	__u8 reserved[1712];
};

#define KVM_S390_VM_CPU_PROCESSOR_UV_FEAT_GUEST	6
#define KVM_S390_VM_CPU_MACHINE_UV_FEAT_GUEST	7

#define KVM_S390_VM_CPU_UV_FEAT_NR_BITS	64
struct kvm_s390_vm_cpu_uv_feat {
	union {
		struct {
			__u64 : 4;
			__u64 ap : 1;		/* bit 4 */
			__u64 ap_intr : 1;	/* bit 5 */
			__u64 : 58;
		};
		__u64 feat;
	};
};

/* kvm attributes for crypto */
#define KVM_S390_VM_CRYPTO_ENABLE_AES_KW	0
#define KVM_S390_VM_CRYPTO_ENABLE_DEA_KW	1
#define KVM_S390_VM_CRYPTO_DISABLE_AES_KW	2
#define KVM_S390_VM_CRYPTO_DISABLE_DEA_KW	3
#define KVM_S390_VM_CRYPTO_ENABLE_APIE		4
#define KVM_S390_VM_CRYPTO_DISABLE_APIE		5

/* kvm attributes for migration mode */
#define KVM_S390_VM_MIGRATION_STOP	0
#define KVM_S390_VM_MIGRATION_START	1
#define KVM_S390_VM_MIGRATION_STATUS	2

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
	/* general purpose regs for s390 */
	__u64 gprs[16];
};

/* for KVM_GET_SREGS and KVM_SET_SREGS */
struct kvm_sregs {
	__u32 acrs[16];
	__u64 crs[16];
};

/* for KVM_GET_FPU and KVM_SET_FPU */
struct kvm_fpu {
	__u32 fpc;
	__u64 fprs[16];
};

#define KVM_GUESTDBG_USE_HW_BP		0x00010000

#define KVM_HW_BP			1
#define KVM_HW_WP_WRITE			2
#define KVM_SINGLESTEP			4

struct kvm_debug_exit_arch {
	__u64 addr;
	__u8 type;
	__u8 pad[7]; /* Should be set to 0 */
};

struct kvm_hw_breakpoint {
	__u64 addr;
	__u64 phys_addr;
	__u64 len;
	__u8 type;
	__u8 pad[7]; /* Should be set to 0 */
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
	__u32 nr_hw_bp;
	__u32 pad; /* Should be set to 0 */
	struct kvm_hw_breakpoint __user *hw_bp;
};

/* for KVM_SYNC_PFAULT and KVM_REG_S390_PFTOKEN */
#define KVM_S390_PFAULT_TOKEN_INVALID	0xffffffffffffffffULL

#define KVM_SYNC_PREFIX (1UL << 0)
#define KVM_SYNC_GPRS   (1UL << 1)
#define KVM_SYNC_ACRS   (1UL << 2)
#define KVM_SYNC_CRS    (1UL << 3)
#define KVM_SYNC_ARCH0  (1UL << 4)
#define KVM_SYNC_PFAULT (1UL << 5)
#define KVM_SYNC_VRS    (1UL << 6)
#define KVM_SYNC_RICCB  (1UL << 7)
#define KVM_SYNC_FPRS   (1UL << 8)
#define KVM_SYNC_GSCB   (1UL << 9)
#define KVM_SYNC_BPBC   (1UL << 10)
#define KVM_SYNC_ETOKEN (1UL << 11)
#define KVM_SYNC_DIAG318 (1UL << 12)

#define KVM_SYNC_S390_VALID_FIELDS \
	(KVM_SYNC_PREFIX | KVM_SYNC_GPRS | KVM_SYNC_ACRS | KVM_SYNC_CRS | \
	 KVM_SYNC_ARCH0 | KVM_SYNC_PFAULT | KVM_SYNC_VRS | KVM_SYNC_RICCB | \
	 KVM_SYNC_FPRS | KVM_SYNC_GSCB | KVM_SYNC_BPBC | KVM_SYNC_ETOKEN | \
	 KVM_SYNC_DIAG318)

/* length and alignment of the sdnx as a power of two */
#define SDNXC 8
#define SDNXL (1UL << SDNXC)
/* definition of registers in kvm_run */
struct kvm_sync_regs {
	__u64 prefix;	/* prefix register */
	__u64 gprs[16];	/* general purpose registers */
	__u32 acrs[16];	/* access registers */
	__u64 crs[16];	/* control registers */
	__u64 todpr;	/* tod programmable register [ARCH0] */
	__u64 cputm;	/* cpu timer [ARCH0] */
	__u64 ckc;	/* clock comparator [ARCH0] */
	__u64 pp;	/* program parameter [ARCH0] */
	__u64 gbea;	/* guest breaking-event address [ARCH0] */
	__u64 pft;	/* pfault token [PFAULT] */
	__u64 pfs;	/* pfault select [PFAULT] */
	__u64 pfc;	/* pfault compare [PFAULT] */
	union {
		__u64 vrs[32][2];	/* vector registers (KVM_SYNC_VRS) */
		__u64 fprs[16];		/* fp registers (KVM_SYNC_FPRS) */
	};
	__u8  reserved[512];	/* for future vector expansion */
	__u32 fpc;		/* valid on KVM_SYNC_VRS or KVM_SYNC_FPRS */
	__u8 bpbc : 1;		/* bp mode */
	__u8 reserved2 : 7;
	__u8 padding1[51];	/* riccb needs to be 64byte aligned */
	__u8 riccb[64];		/* runtime instrumentation controls block */
	__u64 diag318;		/* diagnose 0x318 info */
	__u8 padding2[184];	/* sdnx needs to be 256byte aligned */
	union {
		__u8 sdnx[SDNXL];  /* state description annex */
		struct {
			__u64 reserved1[2];
			__u64 gscb[4];
			__u64 etoken;
			__u64 etoken_extension;
		};
	};
};

#define KVM_REG_S390_TODPR	(KVM_REG_S390 | KVM_REG_SIZE_U32 | 0x1)
#define KVM_REG_S390_EPOCHDIFF	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x2)
#define KVM_REG_S390_CPU_TIMER  (KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x3)
#define KVM_REG_S390_CLOCK_COMP (KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x4)
#define KVM_REG_S390_PFTOKEN	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x5)
#define KVM_REG_S390_PFCOMPARE	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x6)
#define KVM_REG_S390_PFSELECT	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x7)
#define KVM_REG_S390_PP		(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x8)
#define KVM_REG_S390_GBEA	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x9)
#endif
