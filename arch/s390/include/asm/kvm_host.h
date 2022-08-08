/* SPDX-License-Identifier: GPL-2.0 */
/*
 * definition for kernel virtual machines on s390
 *
 * Copyright IBM Corp. 2008, 2018
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */


#ifndef ASM_KVM_HOST_H
#define ASM_KVM_HOST_H

#include <linux/types.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/seqlock.h>
#include <linux/module.h>
#include <asm/debug.h>
#include <asm/cpu.h>
#include <asm/fpu/api.h>
#include <asm/isc.h>
#include <asm/guarded_storage.h>

#define KVM_S390_BSCA_CPU_SLOTS 64
#define KVM_S390_ESCA_CPU_SLOTS 248
#define KVM_MAX_VCPUS 255

/*
 * These seem to be used for allocating ->chip in the routing table, which we
 * don't use. 1 is as small as we can get to reduce the needed memory. If we
 * need to look at ->chip later on, we'll need to revisit this.
 */
#define KVM_NR_IRQCHIPS 1
#define KVM_IRQCHIP_NUM_PINS 1
#define KVM_HALT_POLL_NS_DEFAULT 50000

/* s390-specific vcpu->requests bit members */
#define KVM_REQ_ENABLE_IBS	KVM_ARCH_REQ(0)
#define KVM_REQ_DISABLE_IBS	KVM_ARCH_REQ(1)
#define KVM_REQ_ICPT_OPEREXC	KVM_ARCH_REQ(2)
#define KVM_REQ_START_MIGRATION KVM_ARCH_REQ(3)
#define KVM_REQ_STOP_MIGRATION  KVM_ARCH_REQ(4)
#define KVM_REQ_VSIE_RESTART	KVM_ARCH_REQ(5)

#define SIGP_CTRL_C		0x80
#define SIGP_CTRL_SCN_MASK	0x3f

union bsca_sigp_ctrl {
	__u8 value;
	struct {
		__u8 c : 1;
		__u8 r : 1;
		__u8 scn : 6;
	};
};

union esca_sigp_ctrl {
	__u16 value;
	struct {
		__u8 c : 1;
		__u8 reserved: 7;
		__u8 scn;
	};
};

struct esca_entry {
	union esca_sigp_ctrl sigp_ctrl;
	__u16   reserved1[3];
	__u64   sda;
	__u64   reserved2[6];
};

struct bsca_entry {
	__u8	reserved0;
	union bsca_sigp_ctrl	sigp_ctrl;
	__u16	reserved[3];
	__u64	sda;
	__u64	reserved2[2];
};

union ipte_control {
	unsigned long val;
	struct {
		unsigned long k  : 1;
		unsigned long kh : 31;
		unsigned long kg : 32;
	};
};

struct bsca_block {
	union ipte_control ipte_control;
	__u64	reserved[5];
	__u64	mcn;
	__u64	reserved2;
	struct bsca_entry cpu[KVM_S390_BSCA_CPU_SLOTS];
};

struct esca_block {
	union ipte_control ipte_control;
	__u64   reserved1[7];
	__u64   mcn[4];
	__u64   reserved2[20];
	struct esca_entry cpu[KVM_S390_ESCA_CPU_SLOTS];
};

/*
 * This struct is used to store some machine check info from lowcore
 * for machine checks that happen while the guest is running.
 * This info in host's lowcore might be overwritten by a second machine
 * check from host when host is in the machine check's high-level handling.
 * The size is 24 bytes.
 */
struct mcck_volatile_info {
	__u64 mcic;
	__u64 failing_storage_address;
	__u32 ext_damage_code;
	__u32 reserved;
};

#define CR0_INITIAL_MASK (CR0_UNUSED_56 | CR0_INTERRUPT_KEY_SUBMASK | \
			  CR0_MEASUREMENT_ALERT_SUBMASK)
#define CR14_INITIAL_MASK (CR14_UNUSED_32 | CR14_UNUSED_33 | \
			   CR14_EXTERNAL_DAMAGE_SUBMASK)

#define SIDAD_SIZE_MASK		0xff
#define sida_origin(sie_block) \
	((sie_block)->sidad & PAGE_MASK)
#define sida_size(sie_block) \
	((((sie_block)->sidad & SIDAD_SIZE_MASK) + 1) * PAGE_SIZE)

#define CPUSTAT_STOPPED    0x80000000
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
#define CPUSTAT_KSS        0x00000200
#define CPUSTAT_SM         0x00000080
#define CPUSTAT_IBS        0x00000040
#define CPUSTAT_GED2       0x00000010
#define CPUSTAT_G          0x00000008
#define CPUSTAT_GED        0x00000004
#define CPUSTAT_J          0x00000002
#define CPUSTAT_P          0x00000001

struct kvm_s390_sie_block {
	atomic_t cpuflags;		/* 0x0000 */
	__u32 : 1;			/* 0x0004 */
	__u32 prefix : 18;
	__u32 : 1;
	__u32 ibc : 12;
	__u8	reserved08[4];		/* 0x0008 */
#define PROG_IN_SIE (1<<0)
	__u32	prog0c;			/* 0x000c */
	union {
		__u8	reserved10[16];		/* 0x0010 */
		struct {
			__u64	pv_handle_cpu;
			__u64	pv_handle_config;
		};
	};
#define PROG_BLOCK_SIE	(1<<0)
#define PROG_REQUEST	(1<<1)
	atomic_t prog20;		/* 0x0020 */
	__u8	reserved24[4];		/* 0x0024 */
	__u64	cputm;			/* 0x0028 */
	__u64	ckc;			/* 0x0030 */
	__u64	epoch;			/* 0x0038 */
	__u32	svcc;			/* 0x0040 */
#define LCTL_CR0	0x8000
#define LCTL_CR6	0x0200
#define LCTL_CR9	0x0040
#define LCTL_CR10	0x0020
#define LCTL_CR11	0x0010
#define LCTL_CR14	0x0002
	__u16   lctl;			/* 0x0044 */
	__s16	icpua;			/* 0x0046 */
#define ICTL_OPEREXC	0x80000000
#define ICTL_PINT	0x20000000
#define ICTL_LPSW	0x00400000
#define ICTL_STCTL	0x00040000
#define ICTL_ISKE	0x00004000
#define ICTL_SSKE	0x00002000
#define ICTL_RRBE	0x00001000
#define ICTL_TPROT	0x00000200
	__u32	ictl;			/* 0x0048 */
#define ECA_CEI		0x80000000
#define ECA_IB		0x40000000
#define ECA_SIGPI	0x10000000
#define ECA_MVPGI	0x01000000
#define ECA_AIV		0x00200000
#define ECA_VX		0x00020000
#define ECA_PROTEXCI	0x00002000
#define ECA_APIE	0x00000008
#define ECA_SII		0x00000001
	__u32	eca;			/* 0x004c */
#define ICPT_INST	0x04
#define ICPT_PROGI	0x08
#define ICPT_INSTPROGI	0x0C
#define ICPT_EXTREQ	0x10
#define ICPT_EXTINT	0x14
#define ICPT_IOREQ	0x18
#define ICPT_WAIT	0x1c
#define ICPT_VALIDITY	0x20
#define ICPT_STOP	0x28
#define ICPT_OPEREXC	0x2C
#define ICPT_PARTEXEC	0x38
#define ICPT_IOINST	0x40
#define ICPT_KSS	0x5c
#define ICPT_MCHKREQ	0x60
#define ICPT_INT_ENABLE	0x64
#define ICPT_PV_INSTR	0x68
#define ICPT_PV_NOTIFY	0x6c
#define ICPT_PV_PREF	0x70
	__u8	icptcode;		/* 0x0050 */
	__u8	icptstatus;		/* 0x0051 */
	__u16	ihcpu;			/* 0x0052 */
	__u8	reserved54;		/* 0x0054 */
#define IICTL_CODE_NONE		 0x00
#define IICTL_CODE_MCHK		 0x01
#define IICTL_CODE_EXT		 0x02
#define IICTL_CODE_IO		 0x03
#define IICTL_CODE_RESTART	 0x04
#define IICTL_CODE_SPECIFICATION 0x10
#define IICTL_CODE_OPERAND	 0x11
	__u8	iictl;			/* 0x0055 */
	__u16	ipa;			/* 0x0056 */
	__u32	ipb;			/* 0x0058 */
	__u32	scaoh;			/* 0x005c */
#define FPF_BPBC 	0x20
	__u8	fpf;			/* 0x0060 */
#define ECB_GS		0x40
#define ECB_TE		0x10
#define ECB_SRSI	0x04
#define ECB_HOSTPROTINT	0x02
	__u8	ecb;			/* 0x0061 */
#define ECB2_CMMA	0x80
#define ECB2_IEP	0x20
#define ECB2_PFMFI	0x08
#define ECB2_ESCA	0x04
	__u8    ecb2;                   /* 0x0062 */
#define ECB3_DEA 0x08
#define ECB3_AES 0x04
#define ECB3_RI  0x01
	__u8    ecb3;			/* 0x0063 */
	__u32	scaol;			/* 0x0064 */
	__u8	sdf;			/* 0x0068 */
	__u8    epdx;			/* 0x0069 */
	__u8	cpnc;			/* 0x006a */
	__u8	reserved6b;		/* 0x006b */
	__u32	todpr;			/* 0x006c */
#define GISA_FORMAT1 0x00000001
	__u32	gd;			/* 0x0070 */
	__u8	reserved74[12];		/* 0x0074 */
	__u64	mso;			/* 0x0080 */
	__u64	msl;			/* 0x0088 */
	psw_t	gpsw;			/* 0x0090 */
	__u64	gg14;			/* 0x00a0 */
	__u64	gg15;			/* 0x00a8 */
	__u8	reservedb0[8];		/* 0x00b0 */
#define HPID_KVM	0x4
#define HPID_VSIE	0x5
	__u8	hpid;			/* 0x00b8 */
	__u8	reservedb9[7];		/* 0x00b9 */
	union {
		struct {
			__u32	eiparams;	/* 0x00c0 */
			__u16	extcpuaddr;	/* 0x00c4 */
			__u16	eic;		/* 0x00c6 */
		};
		__u64	mcic;			/* 0x00c0 */
	} __packed;
	__u32	reservedc8;		/* 0x00c8 */
	union {
		struct {
			__u16	pgmilc;		/* 0x00cc */
			__u16	iprcc;		/* 0x00ce */
		};
		__u32	edc;			/* 0x00cc */
	} __packed;
	union {
		struct {
			__u32	dxc;		/* 0x00d0 */
			__u16	mcn;		/* 0x00d4 */
			__u8	perc;		/* 0x00d6 */
			__u8	peratmid;	/* 0x00d7 */
		};
		__u64	faddr;			/* 0x00d0 */
	} __packed;
	__u64	peraddr;		/* 0x00d8 */
	__u8	eai;			/* 0x00e0 */
	__u8	peraid;			/* 0x00e1 */
	__u8	oai;			/* 0x00e2 */
	__u8	armid;			/* 0x00e3 */
	__u8	reservede4[4];		/* 0x00e4 */
	union {
		__u64	tecmc;		/* 0x00e8 */
		struct {
			__u16	subchannel_id;	/* 0x00e8 */
			__u16	subchannel_nr;	/* 0x00ea */
			__u32	io_int_parm;	/* 0x00ec */
			__u32	io_int_word;	/* 0x00f0 */
		};
	} __packed;
	__u8	reservedf4[8];		/* 0x00f4 */
#define CRYCB_FORMAT_MASK 0x00000003
#define CRYCB_FORMAT0 0x00000000
#define CRYCB_FORMAT1 0x00000001
#define CRYCB_FORMAT2 0x00000003
	__u32	crycbd;			/* 0x00fc */
	__u64	gcr[16];		/* 0x0100 */
	union {
		__u64	gbea;		/* 0x0180 */
		__u64	sidad;
	};
	__u8    reserved188[8];		/* 0x0188 */
	__u64   sdnxo;			/* 0x0190 */
	__u8    reserved198[8];		/* 0x0198 */
	__u32	fac;			/* 0x01a0 */
	__u8	reserved1a4[20];	/* 0x01a4 */
	__u64	cbrlo;			/* 0x01b8 */
	__u8	reserved1c0[8];		/* 0x01c0 */
#define ECD_HOSTREGMGMT	0x20000000
#define ECD_MEF		0x08000000
#define ECD_ETOKENF	0x02000000
#define ECD_ECC		0x00200000
	__u32	ecd;			/* 0x01c8 */
	__u8	reserved1cc[18];	/* 0x01cc */
	__u64	pp;			/* 0x01de */
	__u8	reserved1e6[2];		/* 0x01e6 */
	__u64	itdba;			/* 0x01e8 */
	__u64   riccbd;			/* 0x01f0 */
	__u64	gvrd;			/* 0x01f8 */
} __packed __aligned(512);

struct kvm_s390_itdb {
	__u8	data[256];
};

struct sie_page {
	struct kvm_s390_sie_block sie_block;
	struct mcck_volatile_info mcck_info;	/* 0x0200 */
	__u8 reserved218[360];		/* 0x0218 */
	__u64 pv_grregs[16];		/* 0x0380 */
	__u8 reserved400[512];		/* 0x0400 */
	struct kvm_s390_itdb itdb;	/* 0x0600 */
	__u8 reserved700[2304];		/* 0x0700 */
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 exit_userspace;
	u64 exit_null;
	u64 exit_external_request;
	u64 exit_io_request;
	u64 exit_external_interrupt;
	u64 exit_stop_request;
	u64 exit_validity;
	u64 exit_instruction;
	u64 exit_pei;
	u64 halt_no_poll_steal;
	u64 instruction_lctl;
	u64 instruction_lctlg;
	u64 instruction_stctl;
	u64 instruction_stctg;
	u64 exit_program_interruption;
	u64 exit_instr_and_program;
	u64 exit_operation_exception;
	u64 deliver_ckc;
	u64 deliver_cputm;
	u64 deliver_external_call;
	u64 deliver_emergency_signal;
	u64 deliver_service_signal;
	u64 deliver_virtio;
	u64 deliver_stop_signal;
	u64 deliver_prefix_signal;
	u64 deliver_restart_signal;
	u64 deliver_program;
	u64 deliver_io;
	u64 deliver_machine_check;
	u64 exit_wait_state;
	u64 inject_ckc;
	u64 inject_cputm;
	u64 inject_external_call;
	u64 inject_emergency_signal;
	u64 inject_mchk;
	u64 inject_pfault_init;
	u64 inject_program;
	u64 inject_restart;
	u64 inject_set_prefix;
	u64 inject_stop_signal;
	u64 instruction_epsw;
	u64 instruction_gs;
	u64 instruction_io_other;
	u64 instruction_lpsw;
	u64 instruction_lpswe;
	u64 instruction_pfmf;
	u64 instruction_ptff;
	u64 instruction_sck;
	u64 instruction_sckpf;
	u64 instruction_stidp;
	u64 instruction_spx;
	u64 instruction_stpx;
	u64 instruction_stap;
	u64 instruction_iske;
	u64 instruction_ri;
	u64 instruction_rrbe;
	u64 instruction_sske;
	u64 instruction_ipte_interlock;
	u64 instruction_stsi;
	u64 instruction_stfl;
	u64 instruction_tb;
	u64 instruction_tpi;
	u64 instruction_tprot;
	u64 instruction_tsch;
	u64 instruction_sie;
	u64 instruction_essa;
	u64 instruction_sthyi;
	u64 instruction_sigp_sense;
	u64 instruction_sigp_sense_running;
	u64 instruction_sigp_external_call;
	u64 instruction_sigp_emergency;
	u64 instruction_sigp_cond_emergency;
	u64 instruction_sigp_start;
	u64 instruction_sigp_stop;
	u64 instruction_sigp_stop_store_status;
	u64 instruction_sigp_store_status;
	u64 instruction_sigp_store_adtl_status;
	u64 instruction_sigp_arch;
	u64 instruction_sigp_prefix;
	u64 instruction_sigp_restart;
	u64 instruction_sigp_init_cpu_reset;
	u64 instruction_sigp_cpu_reset;
	u64 instruction_sigp_unknown;
	u64 instruction_diagnose_10;
	u64 instruction_diagnose_44;
	u64 instruction_diagnose_9c;
	u64 diag_9c_ignored;
	u64 diag_9c_forward;
	u64 instruction_diagnose_258;
	u64 instruction_diagnose_308;
	u64 instruction_diagnose_500;
	u64 instruction_diagnose_other;
	u64 pfault_sync;
};

#define PGM_OPERATION			0x01
#define PGM_PRIVILEGED_OP		0x02
#define PGM_EXECUTE			0x03
#define PGM_PROTECTION			0x04
#define PGM_ADDRESSING			0x05
#define PGM_SPECIFICATION		0x06
#define PGM_DATA			0x07
#define PGM_FIXED_POINT_OVERFLOW	0x08
#define PGM_FIXED_POINT_DIVIDE		0x09
#define PGM_DECIMAL_OVERFLOW		0x0a
#define PGM_DECIMAL_DIVIDE		0x0b
#define PGM_HFP_EXPONENT_OVERFLOW	0x0c
#define PGM_HFP_EXPONENT_UNDERFLOW	0x0d
#define PGM_HFP_SIGNIFICANCE		0x0e
#define PGM_HFP_DIVIDE			0x0f
#define PGM_SEGMENT_TRANSLATION		0x10
#define PGM_PAGE_TRANSLATION		0x11
#define PGM_TRANSLATION_SPEC		0x12
#define PGM_SPECIAL_OPERATION		0x13
#define PGM_OPERAND			0x15
#define PGM_TRACE_TABEL			0x16
#define PGM_VECTOR_PROCESSING		0x1b
#define PGM_SPACE_SWITCH		0x1c
#define PGM_HFP_SQUARE_ROOT		0x1d
#define PGM_PC_TRANSLATION_SPEC		0x1f
#define PGM_AFX_TRANSLATION		0x20
#define PGM_ASX_TRANSLATION		0x21
#define PGM_LX_TRANSLATION		0x22
#define PGM_EX_TRANSLATION		0x23
#define PGM_PRIMARY_AUTHORITY		0x24
#define PGM_SECONDARY_AUTHORITY		0x25
#define PGM_LFX_TRANSLATION		0x26
#define PGM_LSX_TRANSLATION		0x27
#define PGM_ALET_SPECIFICATION		0x28
#define PGM_ALEN_TRANSLATION		0x29
#define PGM_ALE_SEQUENCE		0x2a
#define PGM_ASTE_VALIDITY		0x2b
#define PGM_ASTE_SEQUENCE		0x2c
#define PGM_EXTENDED_AUTHORITY		0x2d
#define PGM_LSTE_SEQUENCE		0x2e
#define PGM_ASTE_INSTANCE		0x2f
#define PGM_STACK_FULL			0x30
#define PGM_STACK_EMPTY			0x31
#define PGM_STACK_SPECIFICATION		0x32
#define PGM_STACK_TYPE			0x33
#define PGM_STACK_OPERATION		0x34
#define PGM_ASCE_TYPE			0x38
#define PGM_REGION_FIRST_TRANS		0x39
#define PGM_REGION_SECOND_TRANS		0x3a
#define PGM_REGION_THIRD_TRANS		0x3b
#define PGM_MONITOR			0x40
#define PGM_PER				0x80
#define PGM_CRYPTO_OPERATION		0x119

/* irq types in ascend order of priorities */
enum irq_types {
	IRQ_PEND_SET_PREFIX = 0,
	IRQ_PEND_RESTART,
	IRQ_PEND_SIGP_STOP,
	IRQ_PEND_IO_ISC_7,
	IRQ_PEND_IO_ISC_6,
	IRQ_PEND_IO_ISC_5,
	IRQ_PEND_IO_ISC_4,
	IRQ_PEND_IO_ISC_3,
	IRQ_PEND_IO_ISC_2,
	IRQ_PEND_IO_ISC_1,
	IRQ_PEND_IO_ISC_0,
	IRQ_PEND_VIRTIO,
	IRQ_PEND_PFAULT_DONE,
	IRQ_PEND_PFAULT_INIT,
	IRQ_PEND_EXT_HOST,
	IRQ_PEND_EXT_SERVICE,
	IRQ_PEND_EXT_SERVICE_EV,
	IRQ_PEND_EXT_TIMING,
	IRQ_PEND_EXT_CPU_TIMER,
	IRQ_PEND_EXT_CLOCK_COMP,
	IRQ_PEND_EXT_EXTERNAL,
	IRQ_PEND_EXT_EMERGENCY,
	IRQ_PEND_EXT_MALFUNC,
	IRQ_PEND_EXT_IRQ_KEY,
	IRQ_PEND_MCHK_REP,
	IRQ_PEND_PROG,
	IRQ_PEND_SVC,
	IRQ_PEND_MCHK_EX,
	IRQ_PEND_COUNT
};

/* We have 2M for virtio device descriptor pages. Smallest amount of
 * memory per page is 24 bytes (1 queue), so (2048*1024) / 24 = 87381
 */
#define KVM_S390_MAX_VIRTIO_IRQS 87381

/*
 * Repressible (non-floating) machine check interrupts
 * subclass bits in MCIC
 */
#define MCHK_EXTD_BIT 58
#define MCHK_DEGR_BIT 56
#define MCHK_WARN_BIT 55
#define MCHK_REP_MASK ((1UL << MCHK_DEGR_BIT) | \
		       (1UL << MCHK_EXTD_BIT) | \
		       (1UL << MCHK_WARN_BIT))

/* Exigent machine check interrupts subclass bits in MCIC */
#define MCHK_SD_BIT 63
#define MCHK_PD_BIT 62
#define MCHK_EX_MASK ((1UL << MCHK_SD_BIT) | (1UL << MCHK_PD_BIT))

#define IRQ_PEND_EXT_MASK ((1UL << IRQ_PEND_EXT_IRQ_KEY)    | \
			   (1UL << IRQ_PEND_EXT_CLOCK_COMP) | \
			   (1UL << IRQ_PEND_EXT_CPU_TIMER)  | \
			   (1UL << IRQ_PEND_EXT_MALFUNC)    | \
			   (1UL << IRQ_PEND_EXT_EMERGENCY)  | \
			   (1UL << IRQ_PEND_EXT_EXTERNAL)   | \
			   (1UL << IRQ_PEND_EXT_TIMING)     | \
			   (1UL << IRQ_PEND_EXT_HOST)       | \
			   (1UL << IRQ_PEND_EXT_SERVICE)    | \
			   (1UL << IRQ_PEND_EXT_SERVICE_EV) | \
			   (1UL << IRQ_PEND_VIRTIO)         | \
			   (1UL << IRQ_PEND_PFAULT_INIT)    | \
			   (1UL << IRQ_PEND_PFAULT_DONE))

#define IRQ_PEND_IO_MASK ((1UL << IRQ_PEND_IO_ISC_0) | \
			  (1UL << IRQ_PEND_IO_ISC_1) | \
			  (1UL << IRQ_PEND_IO_ISC_2) | \
			  (1UL << IRQ_PEND_IO_ISC_3) | \
			  (1UL << IRQ_PEND_IO_ISC_4) | \
			  (1UL << IRQ_PEND_IO_ISC_5) | \
			  (1UL << IRQ_PEND_IO_ISC_6) | \
			  (1UL << IRQ_PEND_IO_ISC_7))

#define IRQ_PEND_MCHK_MASK ((1UL << IRQ_PEND_MCHK_REP) | \
			    (1UL << IRQ_PEND_MCHK_EX))

#define IRQ_PEND_EXT_II_MASK ((1UL << IRQ_PEND_EXT_CPU_TIMER)  | \
			      (1UL << IRQ_PEND_EXT_CLOCK_COMP) | \
			      (1UL << IRQ_PEND_EXT_EMERGENCY)  | \
			      (1UL << IRQ_PEND_EXT_EXTERNAL)   | \
			      (1UL << IRQ_PEND_EXT_SERVICE)    | \
			      (1UL << IRQ_PEND_EXT_SERVICE_EV))

struct kvm_s390_interrupt_info {
	struct list_head list;
	u64	type;
	union {
		struct kvm_s390_io_info io;
		struct kvm_s390_ext_info ext;
		struct kvm_s390_pgm_info pgm;
		struct kvm_s390_emerg_info emerg;
		struct kvm_s390_extcall_info extcall;
		struct kvm_s390_prefix_info prefix;
		struct kvm_s390_stop_info stop;
		struct kvm_s390_mchk_info mchk;
	};
};

struct kvm_s390_irq_payload {
	struct kvm_s390_io_info io;
	struct kvm_s390_ext_info ext;
	struct kvm_s390_pgm_info pgm;
	struct kvm_s390_emerg_info emerg;
	struct kvm_s390_extcall_info extcall;
	struct kvm_s390_prefix_info prefix;
	struct kvm_s390_stop_info stop;
	struct kvm_s390_mchk_info mchk;
};

struct kvm_s390_local_interrupt {
	spinlock_t lock;
	DECLARE_BITMAP(sigp_emerg_pending, KVM_MAX_VCPUS);
	struct kvm_s390_irq_payload irq;
	unsigned long pending_irqs;
};

#define FIRQ_LIST_IO_ISC_0 0
#define FIRQ_LIST_IO_ISC_1 1
#define FIRQ_LIST_IO_ISC_2 2
#define FIRQ_LIST_IO_ISC_3 3
#define FIRQ_LIST_IO_ISC_4 4
#define FIRQ_LIST_IO_ISC_5 5
#define FIRQ_LIST_IO_ISC_6 6
#define FIRQ_LIST_IO_ISC_7 7
#define FIRQ_LIST_PFAULT   8
#define FIRQ_LIST_VIRTIO   9
#define FIRQ_LIST_COUNT   10
#define FIRQ_CNTR_IO       0
#define FIRQ_CNTR_SERVICE  1
#define FIRQ_CNTR_VIRTIO   2
#define FIRQ_CNTR_PFAULT   3
#define FIRQ_MAX_COUNT     4

/* mask the AIS mode for a given ISC */
#define AIS_MODE_MASK(isc) (0x80 >> isc)

#define KVM_S390_AIS_MODE_ALL    0
#define KVM_S390_AIS_MODE_SINGLE 1

struct kvm_s390_float_interrupt {
	unsigned long pending_irqs;
	unsigned long masked_irqs;
	spinlock_t lock;
	struct list_head lists[FIRQ_LIST_COUNT];
	int counters[FIRQ_MAX_COUNT];
	struct kvm_s390_mchk_info mchk;
	struct kvm_s390_ext_info srv_signal;
	int next_rr_cpu;
	struct mutex ais_lock;
	u8 simm;
	u8 nimm;
};

struct kvm_hw_wp_info_arch {
	unsigned long addr;
	unsigned long phys_addr;
	int len;
	char *old_data;
};

struct kvm_hw_bp_info_arch {
	unsigned long addr;
	int len;
};

/*
 * Only the upper 16 bits of kvm_guest_debug->control are arch specific.
 * Further KVM_GUESTDBG flags which an be used from userspace can be found in
 * arch/s390/include/uapi/asm/kvm.h
 */
#define KVM_GUESTDBG_EXIT_PENDING 0x10000000

#define guestdbg_enabled(vcpu) \
		(vcpu->guest_debug & KVM_GUESTDBG_ENABLE)
#define guestdbg_sstep_enabled(vcpu) \
		(vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
#define guestdbg_hw_bp_enabled(vcpu) \
		(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
#define guestdbg_exit_pending(vcpu) (guestdbg_enabled(vcpu) && \
		(vcpu->guest_debug & KVM_GUESTDBG_EXIT_PENDING))

#define KVM_GUESTDBG_VALID_MASK \
		(KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP |\
		KVM_GUESTDBG_USE_HW_BP | KVM_GUESTDBG_EXIT_PENDING)

struct kvm_guestdbg_info_arch {
	unsigned long cr0;
	unsigned long cr9;
	unsigned long cr10;
	unsigned long cr11;
	struct kvm_hw_bp_info_arch *hw_bp_info;
	struct kvm_hw_wp_info_arch *hw_wp_info;
	int nr_hw_bp;
	int nr_hw_wp;
	unsigned long last_bp;
};

struct kvm_s390_pv_vcpu {
	u64 handle;
	unsigned long stor_base;
};

struct kvm_vcpu_arch {
	struct kvm_s390_sie_block *sie_block;
	/* if vsie is active, currently executed shadow sie control block */
	struct kvm_s390_sie_block *vsie_block;
	unsigned int      host_acrs[NUM_ACRS];
	struct gs_cb      *host_gscb;
	struct fpu	  host_fpregs;
	struct kvm_s390_local_interrupt local_int;
	struct hrtimer    ckc_timer;
	struct kvm_s390_pgm_info pgm;
	struct gmap *gmap;
	/* backup location for the currently enabled gmap when scheduled out */
	struct gmap *enabled_gmap;
	struct kvm_guestdbg_info_arch guestdbg;
	unsigned long pfault_token;
	unsigned long pfault_select;
	unsigned long pfault_compare;
	bool cputm_enabled;
	/*
	 * The seqcount protects updates to cputm_start and sie_block.cputm,
	 * this way we can have non-blocking reads with consistent values.
	 * Only the owning VCPU thread (vcpu->cpu) is allowed to change these
	 * values and to start/stop/enable/disable cpu timer accounting.
	 */
	seqcount_t cputm_seqcount;
	__u64 cputm_start;
	bool gs_enabled;
	bool skey_enabled;
	struct kvm_s390_pv_vcpu pv;
	union diag318_info diag318_info;
};

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
	u64 inject_io;
	u64 inject_float_mchk;
	u64 inject_pfault_done;
	u64 inject_service_signal;
	u64 inject_virtio;
};

struct kvm_arch_memory_slot {
};

struct s390_map_info {
	struct list_head list;
	__u64 guest_addr;
	__u64 addr;
	struct page *page;
};

struct s390_io_adapter {
	unsigned int id;
	int isc;
	bool maskable;
	bool masked;
	bool swap;
	bool suppressible;
};

#define MAX_S390_IO_ADAPTERS ((MAX_ISC + 1) * 8)
#define MAX_S390_ADAPTER_MAPS 256

/* maximum size of facilities and facility mask is 2k bytes */
#define S390_ARCH_FAC_LIST_SIZE_BYTE (1<<11)
#define S390_ARCH_FAC_LIST_SIZE_U64 \
	(S390_ARCH_FAC_LIST_SIZE_BYTE / sizeof(u64))
#define S390_ARCH_FAC_MASK_SIZE_BYTE S390_ARCH_FAC_LIST_SIZE_BYTE
#define S390_ARCH_FAC_MASK_SIZE_U64 \
	(S390_ARCH_FAC_MASK_SIZE_BYTE / sizeof(u64))

struct kvm_s390_cpu_model {
	/* facility mask supported by kvm & hosting machine */
	__u64 fac_mask[S390_ARCH_FAC_LIST_SIZE_U64];
	struct kvm_s390_vm_cpu_subfunc subfuncs;
	/* facility list requested by guest (in dma page) */
	__u64 *fac_list;
	u64 cpuid;
	unsigned short ibc;
};

struct kvm_s390_module_hook {
	int (*hook)(struct kvm_vcpu *vcpu);
	struct module *owner;
};

struct kvm_s390_crypto {
	struct kvm_s390_crypto_cb *crycb;
	struct kvm_s390_module_hook *pqap_hook;
	__u32 crycbd;
	__u8 aes_kw;
	__u8 dea_kw;
	__u8 apie;
};

#define APCB0_MASK_SIZE 1
struct kvm_s390_apcb0 {
	__u64 apm[APCB0_MASK_SIZE];		/* 0x0000 */
	__u64 aqm[APCB0_MASK_SIZE];		/* 0x0008 */
	__u64 adm[APCB0_MASK_SIZE];		/* 0x0010 */
	__u64 reserved18;			/* 0x0018 */
};

#define APCB1_MASK_SIZE 4
struct kvm_s390_apcb1 {
	__u64 apm[APCB1_MASK_SIZE];		/* 0x0000 */
	__u64 aqm[APCB1_MASK_SIZE];		/* 0x0020 */
	__u64 adm[APCB1_MASK_SIZE];		/* 0x0040 */
	__u64 reserved60[4];			/* 0x0060 */
};

struct kvm_s390_crypto_cb {
	struct kvm_s390_apcb0 apcb0;		/* 0x0000 */
	__u8   reserved20[0x0048 - 0x0020];	/* 0x0020 */
	__u8   dea_wrapping_key_mask[24];	/* 0x0048 */
	__u8   aes_wrapping_key_mask[32];	/* 0x0060 */
	struct kvm_s390_apcb1 apcb1;		/* 0x0080 */
};

struct kvm_s390_gisa {
	union {
		struct { /* common to all formats */
			u32 next_alert;
			u8  ipm;
			u8  reserved01[2];
			u8  iam;
		};
		struct { /* format 0 */
			u32 next_alert;
			u8  ipm;
			u8  reserved01;
			u8  : 6;
			u8  g : 1;
			u8  c : 1;
			u8  iam;
			u8  reserved02[4];
			u32 airq_count;
		} g0;
		struct { /* format 1 */
			u32 next_alert;
			u8  ipm;
			u8  simm;
			u8  nimm;
			u8  iam;
			u8  aism[8];
			u8  : 6;
			u8  g : 1;
			u8  c : 1;
			u8  reserved03[11];
			u32 airq_count;
		} g1;
		struct {
			u64 word[4];
		} u64;
	};
};

struct kvm_s390_gib {
	u32 alert_list_origin;
	u32 reserved01;
	u8:5;
	u8  nisc:3;
	u8  reserved03[3];
	u32 reserved04[5];
};

/*
 * sie_page2 has to be allocated as DMA because fac_list, crycb and
 * gisa need 31bit addresses in the sie control block.
 */
struct sie_page2 {
	__u64 fac_list[S390_ARCH_FAC_LIST_SIZE_U64];	/* 0x0000 */
	struct kvm_s390_crypto_cb crycb;		/* 0x0800 */
	struct kvm_s390_gisa gisa;			/* 0x0900 */
	struct kvm *kvm;				/* 0x0920 */
	u8 reserved928[0x1000 - 0x928];			/* 0x0928 */
};

struct kvm_s390_vsie {
	struct mutex mutex;
	struct radix_tree_root addr_to_page;
	int page_count;
	int next;
	struct page *pages[KVM_MAX_VCPUS];
};

struct kvm_s390_gisa_iam {
	u8 mask;
	spinlock_t ref_lock;
	u32 ref_count[MAX_ISC + 1];
};

struct kvm_s390_gisa_interrupt {
	struct kvm_s390_gisa *origin;
	struct kvm_s390_gisa_iam alert;
	struct hrtimer timer;
	u64 expires;
	DECLARE_BITMAP(kicked_mask, KVM_MAX_VCPUS);
};

struct kvm_s390_pv {
	u64 handle;
	u64 guest_len;
	unsigned long stor_base;
	void *stor_var;
};

struct kvm_arch{
	void *sca;
	int use_esca;
	rwlock_t sca_lock;
	debug_info_t *dbf;
	struct kvm_s390_float_interrupt float_int;
	struct kvm_device *flic;
	struct gmap *gmap;
	unsigned long mem_limit;
	int css_support;
	int use_irqchip;
	int use_cmma;
	int use_pfmfi;
	int use_skf;
	int user_cpu_state_ctrl;
	int user_sigp;
	int user_stsi;
	int user_instr0;
	struct s390_io_adapter *adapters[MAX_S390_IO_ADAPTERS];
	wait_queue_head_t ipte_wq;
	int ipte_lock_count;
	struct mutex ipte_mutex;
	spinlock_t start_stop_lock;
	struct sie_page2 *sie_page2;
	struct kvm_s390_cpu_model model;
	struct kvm_s390_crypto crypto;
	struct kvm_s390_vsie vsie;
	u8 epdx;
	u64 epoch;
	int migration_mode;
	atomic64_t cmma_dirty_pages;
	/* subset of available cpu features enabled by user space */
	DECLARE_BITMAP(cpu_feat, KVM_S390_VM_CPU_FEAT_NR_BITS);
	DECLARE_BITMAP(idle_mask, KVM_MAX_VCPUS);
	struct kvm_s390_gisa_interrupt gisa_int;
	struct kvm_s390_pv pv;
};

#define KVM_HVA_ERR_BAD		(-1UL)
#define KVM_HVA_ERR_RO_BAD	(-2UL)

static inline bool kvm_is_error_hva(unsigned long addr)
{
	return IS_ERR_VALUE(addr);
}

#define ASYNC_PF_PER_VCPU	64
struct kvm_arch_async_pf {
	unsigned long pfault_token;
};

bool kvm_arch_can_dequeue_async_page_present(struct kvm_vcpu *vcpu);

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
			       struct kvm_async_pf *work);

bool kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work);

void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work);

static inline void kvm_arch_async_page_present_queued(struct kvm_vcpu *vcpu) {}

void kvm_arch_crypto_clear_masks(struct kvm *kvm);
void kvm_arch_crypto_set_masks(struct kvm *kvm, unsigned long *apm,
			       unsigned long *aqm, unsigned long *adm);

extern int sie64a(struct kvm_s390_sie_block *, u64 *);
extern char sie_exit;

extern int kvm_s390_gisc_register(struct kvm *kvm, u32 gisc);
extern int kvm_s390_gisc_unregister(struct kvm *kvm, u32 gisc);

static inline void kvm_arch_hardware_disable(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_free_memslot(struct kvm *kvm,
					 struct kvm_memory_slot *slot) {}
static inline void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen) {}
static inline void kvm_arch_flush_shadow_all(struct kvm *kvm) {}
static inline void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
		struct kvm_memory_slot *slot) {}
static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu) {}

void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu);

#endif
