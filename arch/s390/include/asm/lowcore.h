/*
 *  include/asm-s390/lowcore.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef _ASM_S390_LOWCORE_H
#define _ASM_S390_LOWCORE_H

#define __LC_IPL_PARMBLOCK_PTR		0x0014
#define __LC_EXT_PARAMS			0x0080
#define __LC_CPU_ADDRESS		0x0084
#define __LC_EXT_INT_CODE		0x0086

#define __LC_SVC_ILC			0x0088
#define __LC_SVC_INT_CODE		0x008a
#define __LC_PGM_ILC			0x008c
#define __LC_PGM_INT_CODE		0x008e

#define __LC_PER_ATMID			0x0096
#define __LC_PER_ADDRESS		0x0098
#define __LC_PER_ACCESS_ID		0x00a1
#define __LC_AR_MODE_ID			0x00a3

#define __LC_SUBCHANNEL_ID		0x00b8
#define __LC_SUBCHANNEL_NR		0x00ba
#define __LC_IO_INT_PARM		0x00bc
#define __LC_IO_INT_WORD		0x00c0
#define __LC_STFL_FAC_LIST		0x00c8
#define __LC_MCCK_CODE			0x00e8

#define __LC_DUMP_REIPL			0x0e00

#ifndef __s390x__
#define __LC_EXT_OLD_PSW		0x0018
#define __LC_SVC_OLD_PSW		0x0020
#define __LC_PGM_OLD_PSW		0x0028
#define __LC_MCK_OLD_PSW		0x0030
#define __LC_IO_OLD_PSW			0x0038
#define __LC_EXT_NEW_PSW		0x0058
#define __LC_SVC_NEW_PSW		0x0060
#define __LC_PGM_NEW_PSW		0x0068
#define __LC_MCK_NEW_PSW		0x0070
#define __LC_IO_NEW_PSW			0x0078
#define __LC_SAVE_AREA			0x0200
#define __LC_RETURN_PSW			0x0240
#define __LC_RETURN_MCCK_PSW		0x0248
#define __LC_SYNC_ENTER_TIMER		0x0250
#define __LC_ASYNC_ENTER_TIMER		0x0258
#define __LC_EXIT_TIMER			0x0260
#define __LC_USER_TIMER			0x0268
#define __LC_SYSTEM_TIMER		0x0270
#define __LC_STEAL_TIMER		0x0278
#define __LC_LAST_UPDATE_TIMER		0x0280
#define __LC_LAST_UPDATE_CLOCK		0x0288
#define __LC_CURRENT			0x0290
#define __LC_THREAD_INFO		0x0294
#define __LC_KERNEL_STACK		0x0298
#define __LC_ASYNC_STACK		0x029c
#define __LC_PANIC_STACK		0x02a0
#define __LC_KERNEL_ASCE		0x02a4
#define __LC_USER_ASCE			0x02a8
#define __LC_USER_EXEC_ASCE		0x02ac
#define __LC_CPUID			0x02b0
#define __LC_INT_CLOCK			0x02c8
#define __LC_MACHINE_FLAGS		0x02d8
#define __LC_FTRACE_FUNC		0x02dc
#define __LC_IRB			0x0300
#define __LC_PFAULT_INTPARM		0x0080
#define __LC_CPU_TIMER_SAVE_AREA	0x00d8
#define __LC_CLOCK_COMP_SAVE_AREA	0x00e0
#define __LC_PSW_SAVE_AREA		0x0100
#define __LC_PREFIX_SAVE_AREA		0x0108
#define __LC_AREGS_SAVE_AREA		0x0120
#define __LC_FPREGS_SAVE_AREA		0x0160
#define __LC_GPREGS_SAVE_AREA		0x0180
#define __LC_CREGS_SAVE_AREA		0x01c0
#else /* __s390x__ */
#define __LC_LAST_BREAK			0x0110
#define __LC_EXT_OLD_PSW		0x0130
#define __LC_SVC_OLD_PSW		0x0140
#define __LC_PGM_OLD_PSW		0x0150
#define __LC_MCK_OLD_PSW		0x0160
#define __LC_IO_OLD_PSW			0x0170
#define __LC_RESTART_PSW		0x01a0
#define __LC_EXT_NEW_PSW		0x01b0
#define __LC_SVC_NEW_PSW		0x01c0
#define __LC_PGM_NEW_PSW		0x01d0
#define __LC_MCK_NEW_PSW		0x01e0
#define __LC_IO_NEW_PSW			0x01f0
#define __LC_SAVE_AREA			0x0200
#define __LC_RETURN_PSW			0x0280
#define __LC_RETURN_MCCK_PSW		0x0290
#define __LC_SYNC_ENTER_TIMER		0x02a0
#define __LC_ASYNC_ENTER_TIMER		0x02a8
#define __LC_EXIT_TIMER			0x02b0
#define __LC_USER_TIMER			0x02b8
#define __LC_SYSTEM_TIMER		0x02c0
#define __LC_STEAL_TIMER		0x02c8
#define __LC_LAST_UPDATE_TIMER		0x02d0
#define __LC_LAST_UPDATE_CLOCK		0x02d8
#define __LC_CURRENT			0x02e0
#define __LC_THREAD_INFO		0x02e8
#define __LC_KERNEL_STACK		0x02f0
#define __LC_ASYNC_STACK		0x02f8
#define __LC_PANIC_STACK		0x0300
#define __LC_KERNEL_ASCE		0x0308
#define __LC_USER_ASCE			0x0310
#define __LC_USER_EXEC_ASCE		0x0318
#define __LC_CPUID			0x0320
#define __LC_INT_CLOCK			0x0340
#define __LC_VDSO_PER_CPU		0x0350
#define __LC_MACHINE_FLAGS		0x0358
#define __LC_FTRACE_FUNC		0x0360
#define __LC_IRB			0x0380
#define __LC_PASTE			0x03c0
#define __LC_PFAULT_INTPARM		0x11b8
#define __LC_FPREGS_SAVE_AREA		0x1200
#define __LC_GPREGS_SAVE_AREA		0x1280
#define __LC_PSW_SAVE_AREA		0x1300
#define __LC_PREFIX_SAVE_AREA		0x1318
#define __LC_FP_CREG_SAVE_AREA		0x131c
#define __LC_TODREG_SAVE_AREA		0x1324
#define __LC_CPU_TIMER_SAVE_AREA	0x1328
#define __LC_CLOCK_COMP_SAVE_AREA	0x1331
#define __LC_AREGS_SAVE_AREA		0x1340
#define __LC_CREGS_SAVE_AREA		0x1380
#endif /* __s390x__ */

#ifndef __ASSEMBLY__

#include <asm/cpu.h>
#include <asm/ptrace.h>
#include <linux/types.h>

void restart_int_handler(void);
void ext_int_handler(void);
void system_call(void);
void pgm_check_handler(void);
void mcck_int_handler(void);
void io_int_handler(void);

#ifdef CONFIG_32BIT

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
}  __attribute__((packed));

#define SAVE_AREA_BASE offsetof(struct _lowcore, extended_save_area_addr)

#else /* CONFIG_32BIT */

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
}  __attribute__((packed));

#define SAVE_AREA_BASE offsetof(struct _lowcore, floating_pt_save_area)

#endif /* CONFIG_32BIT */

#ifndef __s390x__
#define LC_ORDER 0
#else
#define LC_ORDER 1
#endif

#define LC_PAGES (1UL << LC_ORDER)

struct _lowcore
{
#ifndef __s390x__
	/* 0x0000 - 0x01ff: defined by architecture */
	psw_t	restart_psw;			/* 0x0000 */
	__u32	ccw2[4];			/* 0x0008 */
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
	__u8	pad_0x00a2[0x00b8-0x00a2];	/* 0x00a2 */
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
	__u32	st_status_fixed_logout[4];	/* 0x0100 */
	__u8	pad_0x0110[0x0120-0x0110];	/* 0x0110 */

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
	__u64	exit_timer;			/* 0x0260 */
	__u64	user_timer;			/* 0x0268 */
	__u64	system_timer;			/* 0x0270 */
	__u64	steal_timer;			/* 0x0278 */
	__u64	last_update_timer;		/* 0x0280 */
	__u64	last_update_clock;		/* 0x0288 */

	/* Current process. */
	__u32	current_task;			/* 0x0290 */
	__u32	thread_info;			/* 0x0294 */
	__u32	kernel_stack;			/* 0x0298 */

	/* Interrupt and panic stack. */
	__u32	async_stack;			/* 0x029c */
	__u32	panic_stack;			/* 0x02a0 */

	/* Address space pointer. */
	__u32	kernel_asce;			/* 0x02a4 */
	__u32	user_asce;			/* 0x02a8 */
	__u32	user_exec_asce;			/* 0x02ac */

	/* SMP info area */
	struct cpuid cpu_id;			/* 0x02b0 */
	__u32	cpu_nr;				/* 0x02b8 */
	__u32	softirq_pending;		/* 0x02bc */
	__u32	percpu_offset;			/* 0x02c0 */
	__u32	ext_call_fast;			/* 0x02c4 */
	__u64	int_clock;			/* 0x02c8 */
	__u64	clock_comparator;		/* 0x02d0 */
	__u32	machine_flags;			/* 0x02d8 */
	__u32	ftrace_func;			/* 0x02dc */
	__u8	pad_0x02e0[0x0300-0x02e0];	/* 0x02e0 */

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

	/* Align to the top 1k of prefix area */
	__u8	pad_0x0e08[0x1000-0x0e08];	/* 0x0e08 */
#else /* !__s390x__ */
	/* 0x0000 - 0x01ff: defined by architecture */
	__u32	ccw1[2];			/* 0x0000 */
	__u32	ccw2[4];			/* 0x0008 */
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
	addr_t	per_address;			/* 0x0098 */
	__u8	exc_access_id;			/* 0x00a0 */
	__u8	per_access_id;			/* 0x00a1 */
	__u8	op_access_id;			/* 0x00a2 */
	__u8	ar_access_id;			/* 0x00a3 */
	__u8	pad_0x00a4[0x00a8-0x00a4];	/* 0x00a4 */
	addr_t	trans_exc_code;			/* 0x00a8 */
	addr_t	monitor_code;			/* 0x00b0 */
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
	addr_t	failing_storage_address;	/* 0x00f8 */
	__u8	pad_0x0100[0x0120-0x0100];	/* 0x0100 */
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
	__u64	exit_timer;			/* 0x02b0 */
	__u64	user_timer;			/* 0x02b8 */
	__u64	system_timer;			/* 0x02c0 */
	__u64	steal_timer;			/* 0x02c8 */
	__u64	last_update_timer;		/* 0x02d0 */
	__u64	last_update_clock;		/* 0x02d8 */

	/* Current process. */
	__u64	current_task;			/* 0x02e0 */
	__u64	thread_info;			/* 0x02e8 */
	__u64	kernel_stack;			/* 0x02f0 */

	/* Interrupt and panic stack. */
	__u64	async_stack;			/* 0x02f8 */
	__u64	panic_stack;			/* 0x0300 */

	/* Address space pointer. */
	__u64	kernel_asce;			/* 0x0308 */
	__u64	user_asce;			/* 0x0310 */
	__u64	user_exec_asce;			/* 0x0318 */

	/* SMP info area */
	struct cpuid cpu_id;			/* 0x0320 */
	__u32	cpu_nr;				/* 0x0328 */
	__u32	softirq_pending;		/* 0x032c */
	__u64	percpu_offset;			/* 0x0330 */
	__u64	ext_call_fast;			/* 0x0338 */
	__u64	int_clock;			/* 0x0340 */
	__u64	clock_comparator;		/* 0x0348 */
	__u64	vdso_per_cpu_data;		/* 0x0350 */
	__u64	machine_flags;			/* 0x0358 */
	__u64	ftrace_func;			/* 0x0360 */
	__u8	pad_0x0368[0x0380-0x0368];	/* 0x0368 */

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
	__u8	pad_0x0e0c[0x11b8-0x0e0c];	/* 0x0e0c */

	/* 64 bit extparam used for pfault/diag 250: defined by architecture */
	__u64	ext_params2;			/* 0x11B8 */
	__u8	pad_0x11c0[0x1200-0x11C0];	/* 0x11C0 */

	/* CPU register save area: defined by architecture */
	__u64	floating_pt_save_area[16];	/* 0x1200 */
	__u64	gpregs_save_area[16];		/* 0x1280 */
	__u32	st_status_fixed_logout[4];	/* 0x1300 */
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
#endif /* !__s390x__ */
} __attribute__((packed)); /* End structure*/

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

#endif

#endif
