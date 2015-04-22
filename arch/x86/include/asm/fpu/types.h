
#define	MXCSR_DEFAULT		0x1f80

struct i387_fsave_struct {
	u32			cwd;	/* FPU Control Word		*/
	u32			swd;	/* FPU Status Word		*/
	u32			twd;	/* FPU Tag Word			*/
	u32			fip;	/* FPU IP Offset		*/
	u32			fcs;	/* FPU IP Selector		*/
	u32			foo;	/* FPU Operand Pointer Offset	*/
	u32			fos;	/* FPU Operand Pointer Selector	*/

	/* 8*10 bytes for each FP-reg = 80 bytes:			*/
	u32			st_space[20];

	/* Software status information [not touched by FSAVE ]:		*/
	u32			status;
};

struct i387_fxsave_struct {
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

struct i387_soft_struct {
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

struct ymmh_struct {
	/* 16 * 16 bytes for each YMMH-reg = 256 bytes */
	u32 ymmh_space[64];
};

/* We don't support LWP yet: */
struct lwp_struct {
	u8 reserved[128];
};

struct bndreg {
	u64 lower_bound;
	u64 upper_bound;
} __packed;

struct bndcsr {
	u64 bndcfgu;
	u64 bndstatus;
} __packed;

struct xsave_hdr_struct {
	u64 xstate_bv;
	u64 xcomp_bv;
	u64 reserved[6];
} __attribute__((packed));

struct xsave_struct {
	struct i387_fxsave_struct i387;
	struct xsave_hdr_struct xsave_hdr;
	struct ymmh_struct ymmh;
	struct lwp_struct lwp;
	struct bndreg bndreg[4];
	struct bndcsr bndcsr;
	/* new processor state extensions will go here */
} __attribute__ ((packed, aligned (64)));

union thread_xstate {
	struct i387_fsave_struct	fsave;
	struct i387_fxsave_struct	fxsave;
	struct i387_soft_struct		soft;
	struct xsave_struct		xsave;
};

struct fpu {
	unsigned int last_cpu;
	unsigned int has_fpu;
	union thread_xstate *state;
	/*
	 * This counter contains the number of consecutive context switches
	 * during which the FPU stays used. If this is over a threshold, the
	 * lazy fpu saving logic becomes unlazy, to save the trap overhead.
	 * This is an unsigned char so that after 256 iterations the counter
	 * wraps and the context switch behavior turns lazy again; this is to
	 * deal with bursty apps that only use the FPU for a short time:
	 */
	unsigned char counter;
};

