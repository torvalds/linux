#ifndef _ASM_POWERPC_CPUIDLE_H
#define _ASM_POWERPC_CPUIDLE_H

#ifdef CONFIG_PPC_POWERNV
/* Used in powernv idle state management */
#define PNV_THREAD_RUNNING              0
#define PNV_THREAD_NAP                  1
#define PNV_THREAD_SLEEP                2
#define PNV_THREAD_WINKLE               3
#define PNV_CORE_IDLE_LOCK_BIT          0x100
#define PNV_CORE_IDLE_THREAD_BITS       0x0FF

#ifndef __ASSEMBLY__
extern u32 pnv_fastsleep_workaround_at_entry[];
extern u32 pnv_fastsleep_workaround_at_exit[];

extern u64 pnv_first_deep_stop_state;
#endif

#endif

/* Idle state entry routines */
#ifdef	CONFIG_PPC_P7_NAP
#define	IDLE_STATE_ENTER_SEQ(IDLE_INST)				\
	/* Magic NAP/SLEEP/WINKLE mode enter sequence */	\
	std	r0,0(r1);					\
	ptesync;						\
	ld	r0,0(r1);					\
1:	cmp	cr0,r0,r0;					\
	bne	1b;						\
	IDLE_INST;						\
	b	.
#endif /* CONFIG_PPC_P7_NAP */

#endif
