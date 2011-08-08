/*
 *    Copyright IBM Corp. 1999,2010
 *    Author(s): Hartmut Penner <hp@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Denis Joseph Barrow,
 */

#ifndef _ASM_S390_LOWCORE_H
#define _ASM_S390_LOWCORE_H

#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/cpu.h>

void restart_int_handler(void);
void ext_int_handler(void);
void system_call(void);
void pgm_check_handler(void);
void mcck_int_handler(void);
void io_int_handler(void);
void psw_restart_int_handler(void);

#ifdef CONFIG_32BIT

#define LC_ORDER 0
#define LC_PAGES 1

struct save_area {
	u32	ext_save;
	u64	timer;
	u64	clk_cmp;
	u8	pad1[24];
	u8	psw[8];
	u32	pref_reg;
	u8	pad2[20];
	u32	acc_regs[16];
	u64	fp_regs[4];
	u32	gp_regs[16];
	u32	ctrl_regs[16];
} __packed;

struct _lowcore {
	psw_t	restart_psw;			/* 0x0000 */
	psw_t	restart_old_psw;		/* 0x0008 */
	__u8	pad_0x0010[0x0014-0x0010];	/* 0x0010 */
	__u32	ipl_parmblock_ptr;		/* 0x0014 */
	psw_t	external_old_psw;		/* 0x0018 */
	psw_t	svc_old_psw;			/* 0x0020 */
	psw_t	program_old_psw;		/* 0x0028 */
	psw_t	mcck_old_psw;			/* 0x0030 */
	psw_t	io_old_psw;			/* 0x0038 */
	__u8	pad_0x0040[0x0058-0x0040];	/* 0x0040 */
	psw_t	external_new_psw;		/* 0x0058 */
	psw_t	svc_new_psw;			/* 0x0060 */
	psw_t	program_new_psw;		/* 0x0068 */
	psw_t	mcck_new_psw;			/* 0x0070 */
	psw_t	io_new_psw;			/* 0x0078 */
	__u32	ext_params;			/* 0x0080 */
	__u16	cpu_addr;			/* 0x0084 */
	__u16	ext_int_code;			/* 0x0086 */
	__u16	svc_ilc;			/* 0x0088 */
	__u16	svc_code;			/* 0x008a */
	__u16	pgm_ilc;			/* 0x008c */
	__u16	pgm_code;			/* 0x008e */
	__u32	trans_exc_code;			/* 0x0090 */
	__u16	mon_class_num;			/* 0x0094 */
	__u16	per_perc_atmid;			/* 0x0096 */
	__u32	per_address;			/* 0x0098 */
	__u32	monitor_code;			/* 0x009c */
	__u8	exc_access_id;			/* 0x00a0 */
	__u8	per_access_id;			/* 0x00a1 */
	__u8	op_access_id;			/* 0x00a2 */
	__u8	ar_access_id;			/* 0x00a3 */
	__u8	pad_0x00a4[0x00b8-0x00a4];	/* 0x00a4 */
	__u16	subchannel_id;			/* 0x00b8 */
	__u16	subchannel_nr;			/* 0x00ba */
	__u32	io_int_parm;			/* 0x00bc */
	__u32	io_int_word;			/* 0x00c0 */
	__u8	pad_0x00c4[0x00c8-0x00c4];	/* 0x00c4 */
	__u32	stfl_fac_list;			/* 0x00c8 */
	__u8	pad_0x00cc[0x00d4-0x00cc];	/* 0x00cc */
	__u32	extended_save_area_addr;	/* 0x00d4 */
	__u32	cpu_timer_save_area[2];		/* 0x00d8 */
	__u32	clock_comp_save_area[2];	/* 0x00e0 */
	__u32	mcck_interruption_code[2];	/* 0x00e8 */
	__u8	pad_0x00f0[0x00f4-0x00f0];	/* 0x00f0 */
	__u32	external_damage_code;		/* 0x00f4 */
	__u32	failing_storage_address;	/* 0x00f8 */
	__u8	pad_0x00fc[0x0100-0x00fc];	/* 0x00fc */
	psw_t	psw_save_area;			/* 0x0100 */
	__u32	prefixreg_save_area;		/* 0x0108 */
	__u8	pad_0x010c[0x0120-0x010c];	/* 0x010c */

	/* CPU register save area: defined by architecture */
	__u32	access_regs_save_area[16];	/* 0x0120 */
	__u32	floating_pt_save_area[8];	/* 0x0160 */
	__u32	gpregs_save_area[16];		/* 0x0180 */
	__u32	cregs_save_area[16];		/* 0x01c0 */

	/* Return psws. */
	__u32	save_area[16];			/* 0x0200 */
	psw_t	return_psw;			/* 0x0240 */
	psw_t	return_mcck_psw;		/* 0x0248 */

	/* CPU time accounting values */
	__u64	sync_enter_timer;		/* 0x0250 */
	__u64	async_enter_timer;		/* 0x0258 */
	__u64	mcck_enter_timer;		/* 0x0260 */
	__u64	exit_timer;			/* 0x0268 */
	__u64	user_timer;			/* 0x0270 */
	__u64	system_timer;			/* 0x0278 */
	__u64	steal_timer;			/* 0x0280 */
	__u64	last_update_timer;		/* 0x0288 */
	__u64	last_update_clock;		/* 0x0290 */

	/* Current process. */
	__u32	current_task;			/* 0x0298 */
	__u32	thread_info;			/* 0x029c */
	__u32	kernel_stack;			/* 0x02a0 */

	/* Interrupt and panic stack. */
	__u32	async_stack;			/* 0x02a4 */
	__u32	panic_stack;			/* 0x02a8 */

	/* Address space pointer. */
	__u32	kernel_asce;			/* 0x02ac */
	__u32	user_asce;			/* 0x02b0 */
	__u32	current_pid;			/* 0x02b4 */

	/* SMP info area */
	__u32	cpu_nr;				/* 0x02b8 */
	__u32	softirq_pending;		/* 0x02bc */
	__u32	percpu_offset;			/* 0x02c0 */
	__u32	ext_call_fast;			/* 0x02c4 */
	__u64	int_clock;			/* 0x02c8 */
	__u64	mcck_clock;			/* 0x02d0 */
	__u64	clock_comparator;		/* 0x02d8 */
	__u32	machine_flags;			/* 0x02e0 */
	__u32	ftrace_func;			/* 0x02e4 */
	__u8	pad_0x02e8[0x0300-0x02e8];	/* 0x02e8 */

	/* Interrupt response block */
	__u8	irb[64];			/* 0x0300 */

	__u8	pad_0x0340[0x0e00-0x0340];	/* 0x0340 */

	/*
	 * 0xe00 contains the address of the IPL Parameter Information
	 * block. Dump tools need IPIB for IPL after dump.
	 * Note: do not change the position of any fields in 0x0e00-0x0f00
	 */
	__u32	ipib;				/* 0x0e00 */
	__u32	ipib_checksum;			/* 0x0e04 */

	/* 64 bit save area */
	__u64	save_area_64;			/* 0x0e08 */
	__u8	pad_0x0e10[0x0f00-0x0e10];	/* 0x0e10 */

	/* Extended facility list */
	__u64	stfle_fac_list[32];		/* 0x0f00 */
} __packed;

#else /* CONFIG_32BIT */

#define LC_ORDER 1
#define LC_PAGES 2

struct save_area {
	u64	fp_regs[16];
	u64	gp_regs[16];
	u8	psw[16];
	u8	pad1[8];
	u32	pref_reg;
	u32	fp_ctrl_reg;
	u8	pad2[4];
	u32	tod_reg;
	u64	timer;
	u64	clk_cmp;
	u8	pad3[8];
	u32	acc_regs[16];
	u64	ctrl_regs[16];
} __packed;

struct _lowcore {
	__u8	pad_0x0000[0x0014-0x0000];	/* 0x0000 */
	__u32	ipl_parmblock_ptr;		/* 0x0014 */
	__u8	pad_0x0018[0x0080-0x0018];	/* 0x0018 */
	__u32	ext_params;			/* 0x0080 */
	__u16	cpu_addr;			/* 0x0084 */
	__u16	ext_int_code;			/* 0x0086 */
	__u16	svc_ilc;			/* 0x0088 */
	__u16	svc_code;			/* 0x008a */
	__u16	pgm_ilc;			/* 0x008c */
	__u16	pgm_code;			/* 0x008e */
	__u32	data_exc_code;			/* 0x0090 */
	__u16	mon_class_num;			/* 0x0094 */
	__u16	per_perc_atmid;			/* 0x0096 */
	__u64	per_address;			/* 0x0098 */
	__u8	exc_access_id;			/* 0x00a0 */
	__u8	per_access_id;			/* 0x00a1 */
	__u8	op_access_id;			/* 0x00a2 */
	__u8	ar_access_id;			/* 0x00a3 */
	__u8	pad_0x00a4[0x00a8-0x00a4];	/* 0x00a4 */
	__u64	trans_exc_code;			/* 0x00a8 */
	__u64	monitor_code;			/* 0x00b0 */
	__u16	subchannel_id;			/* 0x00b8 */
	__u16	subchannel_nr;			/* 0x00ba */
	__u32	io_int_parm;			/* 0x00bc */
	__u32	io_int_word;			/* 0x00c0 */
	__u8	pad_0x00c4[0x00c8-0x00c4];	/* 0x00c4 */
	__u32	stfl_fac_list;			/* 0x00c8 */
	__u8	pad_0x00cc[0x00e8-0x00cc];	/* 0x00cc */
	__u32	mcck_interruption_code[2];	/* 0x00e8 */
	__u8	pad_0x00f0[0x00f4-0x00f0];	/* 0x00f0 */
	__u32	external_damage_code;		/* 0x00f4 */
	__u64	failing_storage_address;	/* 0x00f8 */
	__u8	pad_0x0100[0x0110-0x0100];	/* 0x0100 */
	__u64	breaking_event_addr;		/* 0x0110 */
	__u8	pad_0x0118[0x0120-0x0118];	/* 0x0118 */
	psw_t	restart_old_psw;		/* 0x0120 */
	psw_t	external_old_psw;		/* 0x0130 */
	psw_t	svc_old_psw;			/* 0x0140 */
	psw_t	program_old_psw;		/* 0x0150 */
	psw_t	mcck_old_psw;			/* 0x0160 */
	psw_t	io_old_psw;			/* 0x0170 */
	__u8	pad_0x0180[0x01a0-0x0180];	/* 0x0180 */
	psw_t	restart_psw;			/* 0x01a0 */
	psw_t	external_new_psw;		/* 0x01b0 */
	psw_t	svc_new_psw;			/* 0x01c0 */
	psw_t	program_new_psw;		/* 0x01d0 */
	psw_t	mcck_new_psw;			/* 0x01e0 */
	psw_t	io_new_psw;			/* 0x01f0 */

	/* Entry/exit save area & return psws. */
	__u64	save_area[16];			/* 0x0200 */
	psw_t	return_psw;			/* 0x0280 */
	psw_t	return_mcck_psw;		/* 0x0290 */

	/* CPU accounting and timing values. */
	__u64	sync_enter_timer;		/* 0x02a0 */
	__u64	async_enter_timer;		/* 0x02a8 */
	__u64	mcck_enter_timer;		/* 0x02b0 */
	__u64	exit_timer;			/* 0x02b8 */
	__u64	user_timer;			/* 0x02c0 */
	__u64	system_timer;			/* 0x02c8 */
	__u64	steal_timer;			/* 0x02d0 */
	__u64	last_update_timer;		/* 0x02d8 */
	__u64	last_update_clock;		/* 0x02e0 */

	/* Current process. */
	__u64	current_task;			/* 0x02e8 */
	__u64	thread_info;			/* 0x02f0 */
	__u64	kernel_stack;			/* 0x02f8 */

	/* Interrupt and panic stack. */
	__u64	async_stack;			/* 0x0300 */
	__u64	panic_stack;			/* 0x0308 */

	/* Address space pointer. */
	__u64	kernel_asce;			/* 0x0310 */
	__u64	user_asce;			/* 0x0318 */
	__u64	current_pid;			/* 0x0320 */

	/* SMP info area */
	__u32	cpu_nr;				/* 0x0328 */
	__u32	softirq_pending;		/* 0x032c */
	__u64	percpu_offset;			/* 0x0330 */
	__u64	ext_call_fast;			/* 0x0338 */
	__u64	int_clock;			/* 0x0340 */
	__u64	mcck_clock;			/* 0x0348 */
	__u64	clock_comparator;		/* 0x0350 */
	__u64	vdso_per_cpu_data;		/* 0x0358 */
	__u64	machine_flags;			/* 0x0360 */
	__u64	ftrace_func;			/* 0x0368 */
	__u64	gmap;				/* 0x0370 */
	__u64	cmf_hpp;			/* 0x0378 */

	/* Interrupt response block. */
	__u8	irb[64];			/* 0x0380 */

	/* Per cpu primary space access list */
	__u32	paste[16];			/* 0x03c0 */

	__u8	pad_0x0400[0x0e00-0x0400];	/* 0x0400 */

	/*
	 * 0xe00 contains the address of the IPL Parameter Information
	 * block. Dump tools need IPIB for IPL after dump.
	 * Note: do not change the position of any fields in 0x0e00-0x0f00
	 */
	__u64	ipib;				/* 0x0e00 */
	__u32	ipib_checksum;			/* 0x0e08 */

	/* 64 bit save area */
	__u64	save_area_64;			/* 0x0e0c */
	__u8	pad_0x0e14[0x0f00-0x0e14];	/* 0x0e14 */

	/* Extended facility list */
	__u64	stfle_fac_list[32];		/* 0x0f00 */
	__u8	pad_0x1000[0x11b8-0x1000];	/* 0x1000 */

	/* 64 bit extparam used for pfault/diag 250: defined by architecture */
	__u64	ext_params2;			/* 0x11B8 */
	__u8	pad_0x11c0[0x1200-0x11C0];	/* 0x11C0 */

	/* CPU register save area: defined by architecture */
	__u64	floating_pt_save_area[16];	/* 0x1200 */
	__u64	gpregs_save_area[16];		/* 0x1280 */
	psw_t	psw_save_area;			/* 0x1300 */
	__u8	pad_0x1310[0x1318-0x1310];	/* 0x1310 */
	__u32	prefixreg_save_area;		/* 0x1318 */
	__u32	fpt_creg_save_area;		/* 0x131c */
	__u8	pad_0x1320[0x1324-0x1320];	/* 0x1320 */
	__u32	tod_progreg_save_area;		/* 0x1324 */
	__u32	cpu_timer_save_area[2];		/* 0x1328 */
	__u32	clock_comp_save_area[2];	/* 0x1330 */
	__u8	pad_0x1338[0x1340-0x1338];	/* 0x1338 */
	__u32	access_regs_save_area[16];	/* 0x1340 */
	__u64	cregs_save_area[16];		/* 0x1380 */

	/* align to the top of the prefix area */
	__u8	pad_0x1400[0x2000-0x1400];	/* 0x1400 */
} __packed;

#endif /* CONFIG_32BIT */

#define S390_lowcore (*((struct _lowcore *) 0))

extern struct _lowcore *lowcore_ptr[];

static inline void set_prefix(__u32 address)
{
	asm volatile("spx %0" : : "m" (address) : "memory");
}

static inline __u32 store_prefix(void)
{
	__u32 address;

	asm volatile("stpx %0" : "=m" (address));
	return address;
}

#endif /* _ASM_S390_LOWCORE_H */
