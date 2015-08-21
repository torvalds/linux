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
enum xfeature_bit {
	XSTATE_BIT_FP,
	XSTATE_BIT_SSE,
	XSTATE_BIT_YMM,
	XSTATE_BIT_BNDREGS,
	XSTATE_BIT_BNDCSR,
	XSTATE_BIT_OPMASK,
	XSTATE_BIT_ZMM_Hi256,
	XSTATE_BIT_Hi16_ZMM,

	XFEATURES_NR_MAX,
};

#define XSTATE_FP		(1 << XSTATE_BIT_FP)
#define XSTATE_SSE		(1 << XSTATE_BIT_SSE)
#define XSTATE_YMM		(1 << XSTATE_BIT_YMM)
#define XSTATE_BNDREGS		(1 << XSTATE_BIT_BNDREGS)
#define XSTATE_BNDCSR		(1 << XSTATE_BIT_BNDCSR)
#define XSTATE_OPMASK		(1 << XSTATE_BIT_OPMASK)
#define XSTATE_ZMM_Hi256	(1 << XSTATE_BIT_ZMM_Hi256)
#define XSTATE_Hi16_ZMM		(1 << XSTATE_BIT_Hi16_ZMM)

#define XSTATE_FPSSE		(XSTATE_FP | XSTATE_SSE)
#define XSTATE_AVX512		(XSTATE_OPMASK | XSTATE_ZMM_Hi256 | XSTATE_Hi16_ZMM)

/*
 * There are 16x 256-bit AVX registers named YMM0-YMM15.
 * The low 128 bits are aliased to the 16 SSE registers (XMM0-XMM15)
 * and are stored in 'struct fxregs_state::xmm_space[]'.
 *
 * The high 128 bits are stored here:
 *    16x 128 bits == 256 bytes.
 */
struct ymmh_struct {
	u8				ymmh_space[256];
};

/* We don't support LWP yet: */
struct lwp_struct {
	u8				reserved[128];
};

/* Intel MPX support: */
struct bndreg {
	u64				lower_bound;
	u64				upper_bound;
} __packed;

struct bndcsr {
	u64				bndcfgu;
	u64				bndstatus;
} __packed;

struct mpx_struct {
	struct bndreg			bndreg[4];
	struct bndcsr			bndcsr;
};

struct xstate_header {
	u64				xfeatures;
	u64				xcomp_bv;
	u64				reserved[6];
} __attribute__((packed));

/* New processor state extensions should be added here: */
#define XSTATE_RESERVE			(sizeof(struct ymmh_struct) + \
					 sizeof(struct lwp_struct)  + \
					 sizeof(struct mpx_struct)  )
/*
 * This is our most modern FPU state format, as saved by the XSAVE
 * and restored by the XRSTOR instructions.
 *
 * It consists of a legacy fxregs portion, an xstate header and
 * subsequent fixed size areas as defined by the xstate header.
 * Not all CPUs support all the extensions.
 */
struct xregs_state {
	struct fxregs_state		i387;
	struct xstate_header		header;
	u8				__reserved[XSTATE_RESERVE];
} __attribute__ ((packed, aligned (64)));

/*
 * This is a union of all the possible FPU state formats
 * put together, so that we can pick the right one runtime.
 *
 * The size of the structure is determined by the largest
 * member - which is the xsave area:
 */
union fpregs_state {
	struct fregs_state		fsave;
	struct fxregs_state		fxsave;
	struct swregs_state		soft;
	struct xregs_state		xsave;
	u8 __padding[PAGE_SIZE];
};

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
	 * @fpstate_active:
	 *
	 * This flag indicates whether this context is active: if the task
	 * is not running then we can restore from this context, if the task
	 * is running then we should save into this context.
	 */
	unsigned char			fpstate_active;

	/*
	 * @fpregs_active:
	 *
	 * This flag determines whether a given context is actively
	 * loaded into the FPU's registers and that those registers
	 * represent the task's current FPU state.
	 *
	 * Note the interaction with fpstate_active:
	 *
	 *   # task does not use the FPU:
	 *   fpstate_active == 0
	 *
	 *   # task uses the FPU and regs are active:
	 *   fpstate_active == 1 && fpregs_active == 1
	 *
	 *   # the regs are inactive but still match fpstate:
	 *   fpstate_active == 1 && fpregs_active == 0 && fpregs_owner == fpu
	 *
	 * The third state is what we use for the lazy restore optimization
	 * on lazy-switching CPUs.
	 */
	unsigned char			fpregs_active;

	/*
	 * @counter:
	 *
	 * This counter contains the number of consecutive context switches
	 * during which the FPU stays used. If this is over a threshold, the
	 * lazy FPU restore logic becomes eager, to save the trap overhead.
	 * This is an unsigned char so that after 256 iterations the counter
	 * wraps and the context switch behavior turns lazy again; this is to
	 * deal with bursty apps that only use the FPU for a short time:
	 */
	unsigned char			counter;
	/*
	 * @state:
	 *
	 * In-memory copy of all FPU registers that we save/restore
	 * over context switches. If the task is using the FPU then
	 * the registers in the FPU are more recent than this state
	 * copy. If the task context-switches away then they get
	 * saved here and represent the FPU state.
	 *
	 * After context switches there may be a (short) time period
	 * during which the in-FPU hardware registers are unchanged
	 * and still perfectly match this state, if the tasks
	 * scheduled afterwards are not using the FPU.
	 *
	 * This is the 'lazy restore' window of optimization, which
	 * we track though 'fpu_fpregs_owner_ctx' and 'fpu->last_cpu'.
	 *
	 * We detect whether a subsequent task uses the FPU via setting
	 * CR0::TS to 1, which causes any FPU use to raise a #NM fault.
	 *
	 * During this window, if the task gets scheduled again, we
	 * might be able to skip having to do a restore from this
	 * memory buffer to the hardware registers - at the cost of
	 * incurring the overhead of #NM fault traps.
	 *
	 * Note that on modern CPUs that support the XSAVEOPT (or other
	 * optimized XSAVE instructions), we don't use #NM traps anymore,
	 * as the hardware can track whether FPU registers need saving
	 * or not. On such CPUs we activate the non-lazy ('eagerfpu')
	 * logic, which unconditionally saves/restores all FPU state
	 * across context switches. (if FPU state exists.)
	 */
	union fpregs_state		state;
	/*
	 * WARNING: 'state' is dynamically-sized.  Do not put
	 * anything after it here.
	 */
};

#endif /* _ASM_X86_FPU_H */
