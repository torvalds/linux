/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FPU data structures:
 */
#ifndef _ASM_X86_FPU_H
#define _ASM_X86_FPU_H

/*
 * The legacy x87 FPU state format, as saved by FSAVE and
 * restored by the FRSTOR instructions:
 */
struct fregs_state {
	u32			cwd;	/* FPU Control Word		*/
	u32			swd;	/* FPU Status Word		*/
	u32			twd;	/* FPU Tag Word			*/
	u32			fip;	/* FPU IP Offset		*/
	u32			fcs;	/* FPU IP Selector		*/
	u32			foo;	/* FPU Operand Pointer Offset	*/
	u32			fos;	/* FPU Operand Pointer Selector	*/

	/* 8*10 bytes for each FP-reg = 80 bytes:			*/
	u32			st_space[20];

	/* Software status information [not touched by FSAVE]:		*/
	u32			status;
};

/*
 * The legacy fx SSE/MMX FPU state format, as saved by FXSAVE and
 * restored by the FXRSTOR instructions. It's similar to the FSAVE
 * format, but differs in some areas, plus has extensions at
 * the end for the XMM registers.
 */
struct fxregs_state {
	u16			cwd; /* Control Word			*/
	u16			swd; /* Status Word			*/
	u16			twd; /* Tag Word			*/
	u16			fop; /* Last Instruction Opcode		*/
	union {
		struct {
			u64	rip; /* Instruction Pointer		*/
			u64	rdp; /* Data Pointer			*/
		};
		struct {
			u32	fip; /* FPU IP Offset			*/
			u32	fcs; /* FPU IP Selector			*/
			u32	foo; /* FPU Operand Offset		*/
			u32	fos; /* FPU Operand Selector		*/
		};
	};
	u32			mxcsr;		/* MXCSR Register State */
	u32			mxcsr_mask;	/* MXCSR Mask		*/

	/* 8*16 bytes for each FP-reg = 128 bytes:			*/
	u32			st_space[32];

	/* 16*16 bytes for each XMM-reg = 256 bytes:			*/
	u32			xmm_space[64];

	u32			padding[12];

	union {
		u32		padding1[12];
		u32		sw_reserved[12];
	};

} __attribute__((aligned(16)));

/* Default value for fxregs_state.mxcsr: */
#define MXCSR_DEFAULT		0x1f80

/* Copy both mxcsr & mxcsr_flags with a single u64 memcpy: */
#define MXCSR_AND_FLAGS_SIZE sizeof(u64)

/*
 * Software based FPU emulation state. This is arbitrary really,
 * it matches the x87 format to make it easier to understand:
 */
struct swregs_state {
	u32			cwd;
	u32			swd;
	u32			twd;
	u32			fip;
	u32			fcs;
	u32			foo;
	u32			fos;
	/* 8*10 bytes for each FP-reg = 80 bytes: */
	u32			st_space[20];
	u8			ftop;
	u8			changed;
	u8			lookahead;
	u8			no_update;
	u8			rm;
	u8			alimit;
	struct math_emu_info	*info;
	u32			entry_eip;
};

/*
 * List of XSAVE features Linux knows about:
 */
enum xfeature {
	XFEATURE_FP,
	XFEATURE_SSE,
	/*
	 * Values above here are "legacy states".
	 * Those below are "extended states".
	 */
	XFEATURE_YMM,
	XFEATURE_BNDREGS,
	XFEATURE_BNDCSR,
	XFEATURE_OPMASK,
	XFEATURE_ZMM_Hi256,
	XFEATURE_Hi16_ZMM,
	XFEATURE_PT_UNIMPLEMENTED_SO_FAR,
	XFEATURE_PKRU,
	XFEATURE_PASID,
	XFEATURE_RSRVD_COMP_11,
	XFEATURE_RSRVD_COMP_12,
	XFEATURE_RSRVD_COMP_13,
	XFEATURE_RSRVD_COMP_14,
	XFEATURE_LBR,

	XFEATURE_MAX,
};

#define XFEATURE_MASK_FP		(1 << XFEATURE_FP)
#define XFEATURE_MASK_SSE		(1 << XFEATURE_SSE)
#define XFEATURE_MASK_YMM		(1 << XFEATURE_YMM)
#define XFEATURE_MASK_BNDREGS		(1 << XFEATURE_BNDREGS)
#define XFEATURE_MASK_BNDCSR		(1 << XFEATURE_BNDCSR)
#define XFEATURE_MASK_OPMASK		(1 << XFEATURE_OPMASK)
#define XFEATURE_MASK_ZMM_Hi256		(1 << XFEATURE_ZMM_Hi256)
#define XFEATURE_MASK_Hi16_ZMM		(1 << XFEATURE_Hi16_ZMM)
#define XFEATURE_MASK_PT		(1 << XFEATURE_PT_UNIMPLEMENTED_SO_FAR)
#define XFEATURE_MASK_PKRU		(1 << XFEATURE_PKRU)
#define XFEATURE_MASK_PASID		(1 << XFEATURE_PASID)
#define XFEATURE_MASK_LBR		(1 << XFEATURE_LBR)

#define XFEATURE_MASK_FPSSE		(XFEATURE_MASK_FP | XFEATURE_MASK_SSE)
#define XFEATURE_MASK_AVX512		(XFEATURE_MASK_OPMASK \
					 | XFEATURE_MASK_ZMM_Hi256 \
					 | XFEATURE_MASK_Hi16_ZMM)

#define FIRST_EXTENDED_XFEATURE	XFEATURE_YMM

struct reg_128_bit {
	u8      regbytes[128/8];
};
struct reg_256_bit {
	u8	regbytes[256/8];
};
struct reg_512_bit {
	u8	regbytes[512/8];
};

/*
 * State component 2:
 *
 * There are 16x 256-bit AVX registers named YMM0-YMM15.
 * The low 128 bits are aliased to the 16 SSE registers (XMM0-XMM15)
 * and are stored in 'struct fxregs_state::xmm_space[]' in the
 * "legacy" area.
 *
 * The high 128 bits are stored here.
 */
struct ymmh_struct {
	struct reg_128_bit              hi_ymm[16];
} __packed;

/* Intel MPX support: */

struct mpx_bndreg {
	u64				lower_bound;
	u64				upper_bound;
} __packed;
/*
 * State component 3 is used for the 4 128-bit bounds registers
 */
struct mpx_bndreg_state {
	struct mpx_bndreg		bndreg[4];
} __packed;

/*
 * State component 4 is used for the 64-bit user-mode MPX
 * configuration register BNDCFGU and the 64-bit MPX status
 * register BNDSTATUS.  We call the pair "BNDCSR".
 */
struct mpx_bndcsr {
	u64				bndcfgu;
	u64				bndstatus;
} __packed;

/*
 * The BNDCSR state is padded out to be 64-bytes in size.
 */
struct mpx_bndcsr_state {
	union {
		struct mpx_bndcsr		bndcsr;
		u8				pad_to_64_bytes[64];
	};
} __packed;

/* AVX-512 Components: */

/*
 * State component 5 is used for the 8 64-bit opmask registers
 * k0-k7 (opmask state).
 */
struct avx_512_opmask_state {
	u64				opmask_reg[8];
} __packed;

/*
 * State component 6 is used for the upper 256 bits of the
 * registers ZMM0-ZMM15. These 16 256-bit values are denoted
 * ZMM0_H-ZMM15_H (ZMM_Hi256 state).
 */
struct avx_512_zmm_uppers_state {
	struct reg_256_bit		zmm_upper[16];
} __packed;

/*
 * State component 7 is used for the 16 512-bit registers
 * ZMM16-ZMM31 (Hi16_ZMM state).
 */
struct avx_512_hi16_state {
	struct reg_512_bit		hi16_zmm[16];
} __packed;

/*
 * State component 9: 32-bit PKRU register.  The state is
 * 8 bytes long but only 4 bytes is used currently.
 */
struct pkru_state {
	u32				pkru;
	u32				pad;
} __packed;

/*
 * State component 15: Architectural LBR configuration state.
 * The size of Arch LBR state depends on the number of LBRs (lbr_depth).
 */

struct lbr_entry {
	u64 from;
	u64 to;
	u64 info;
};

struct arch_lbr_state {
	u64 lbr_ctl;
	u64 lbr_depth;
	u64 ler_from;
	u64 ler_to;
	u64 ler_info;
	struct lbr_entry		entries[];
} __packed;

/*
 * State component 10 is supervisor state used for context-switching the
 * PASID state.
 */
struct ia32_pasid_state {
	u64 pasid;
} __packed;

struct xstate_header {
	u64				xfeatures;
	u64				xcomp_bv;
	u64				reserved[6];
} __attribute__((packed));

/*
 * xstate_header.xcomp_bv[63] indicates that the extended_state_area
 * is in compacted format.
 */
#define XCOMP_BV_COMPACTED_FORMAT ((u64)1 << 63)

/*
 * This is our most modern FPU state format, as saved by the XSAVE
 * and restored by the XRSTOR instructions.
 *
 * It consists of a legacy fxregs portion, an xstate header and
 * subsequent areas as defined by the xstate header.  Not all CPUs
 * support all the extensions, so the size of the extended area
 * can vary quite a bit between CPUs.
 */
struct xregs_state {
	struct fxregs_state		i387;
	struct xstate_header		header;
	u8				extended_state_area[0];
} __attribute__ ((packed, aligned (64)));

/*
 * This is a union of all the possible FPU state formats
 * put together, so that we can pick the right one runtime.
 *
 * The size of the structure is determined by the largest
 * member - which is the xsave area.  The padding is there
 * to ensure that statically-allocated task_structs (just
 * the init_task today) have enough space.
 */
union fpregs_state {
	struct fregs_state		fsave;
	struct fxregs_state		fxsave;
	struct swregs_state		soft;
	struct xregs_state		xsave;
	u8 __padding[PAGE_SIZE];
};

struct fpstate {
	/* @regs: The register state union for all supported formats */
	union fpregs_state		regs;

	/* @regs is dynamically sized! Don't add anything after @regs! */
} __aligned(64);

/*
 * Highest level per task FPU state data structure that
 * contains the FPU register state plus various FPU
 * state fields:
 */
struct fpu {
	/*
	 * @last_cpu:
	 *
	 * Records the last CPU on which this context was loaded into
	 * FPU registers. (In the lazy-restore case we might be
	 * able to reuse FPU registers across multiple context switches
	 * this way, if no intermediate task used the FPU.)
	 *
	 * A value of -1 is used to indicate that the FPU state in context
	 * memory is newer than the FPU state in registers, and that the
	 * FPU state should be reloaded next time the task is run.
	 */
	unsigned int			last_cpu;

	/*
	 * @avx512_timestamp:
	 *
	 * Records the timestamp of AVX512 use during last context switch.
	 */
	unsigned long			avx512_timestamp;

	/*
	 * @fpstate:
	 *
	 * Pointer to the active struct fpstate. Initialized to
	 * point at @__fpstate below.
	 */
	struct fpstate			*fpstate;

	/*
	 * @__fpstate:
	 *
	 * Initial in-memory storage for FPU registers which are saved in
	 * context switch and when the kernel uses the FPU. The registers
	 * are restored from this storage on return to user space if they
	 * are not longer containing the tasks FPU register state.
	 */
	struct fpstate			__fpstate;
	/*
	 * WARNING: '__fpstate' is dynamically-sized.  Do not put
	 * anything after it here.
	 */
};

#endif /* _ASM_X86_FPU_H */
