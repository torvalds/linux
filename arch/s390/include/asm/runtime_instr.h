#ifndef _RUNTIME_INSTR_H
#define _RUNTIME_INSTR_H

#define S390_RUNTIME_INSTR_START	0x1
#define S390_RUNTIME_INSTR_STOP		0x2

struct runtime_instr_cb {
	__u64 buf_current;
	__u64 buf_origin;
	__u64 buf_limit;

	__u32 valid		: 1;
	__u32 pstate		: 1;
	__u32 pstate_set_buf	: 1;
	__u32 home_space	: 1;
	__u32 altered		: 1;
	__u32			: 3;
	__u32 pstate_sample	: 1;
	__u32 sstate_sample	: 1;
	__u32 pstate_collect	: 1;
	__u32 sstate_collect	: 1;
	__u32			: 1;
	__u32 halted_int	: 1;
	__u32 int_requested	: 1;
	__u32 buffer_full_int	: 1;
	__u32 key		: 4;
	__u32			: 9;
	__u32 rgs		: 3;

	__u32 mode		: 4;
	__u32 next		: 1;
	__u32 mae		: 1;
	__u32			: 2;
	__u32 call_type_br	: 1;
	__u32 return_type_br	: 1;
	__u32 other_type_br	: 1;
	__u32 bc_other_type	: 1;
	__u32 emit		: 1;
	__u32 tx_abort		: 1;
	__u32			: 2;
	__u32 bp_xn		: 1;
	__u32 bp_xt		: 1;
	__u32 bp_ti		: 1;
	__u32 bp_ni		: 1;
	__u32 suppr_y		: 1;
	__u32 suppr_z		: 1;

	__u32 dc_miss_extra	: 1;
	__u32 lat_lev_ignore	: 1;
	__u32 ic_lat_lev	: 4;
	__u32 dc_lat_lev	: 4;

	__u64 reserved1;
	__u64 scaling_factor;
	__u64 rsic;
	__u64 reserved2;
} __packed __aligned(8);

extern struct runtime_instr_cb runtime_instr_empty_cb;

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

static inline void save_ri_cb(struct runtime_instr_cb *cb_prev)
{
	if (cb_prev)
		store_runtime_instr_cb(cb_prev);
}

static inline void restore_ri_cb(struct runtime_instr_cb *cb_next,
				 struct runtime_instr_cb *cb_prev)
{
	if (cb_next)
		load_runtime_instr_cb(cb_next);
	else if (cb_prev)
		load_runtime_instr_cb(&runtime_instr_empty_cb);
}

struct task_struct;

void runtime_instr_release(struct task_struct *tsk);

#endif /* _RUNTIME_INSTR_H */
