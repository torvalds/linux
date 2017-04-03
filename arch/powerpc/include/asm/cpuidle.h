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

/*
 * ============================ NOTE =================================
 * The older firmware populates only the RL field in the psscr_val and
 * sets the psscr_mask to 0xf. On such a firmware, the kernel sets the
 * remaining PSSCR fields to default values as follows:
 *
 * - ESL and EC bits are to 1. So wakeup from any stop state will be
 *   at vector 0x100.
 *
 * - MTL and PSLL are set to the maximum allowed value as per the ISA,
 *    i.e. 15.
 *
 * - The Transition Rate, TR is set to the Maximum value 3.
 */
#define PSSCR_HV_DEFAULT_VAL    (PSSCR_ESL | PSSCR_EC |		    \
				PSSCR_PSLL_MASK | PSSCR_TR_MASK |   \
				PSSCR_MTL_MASK)

#define PSSCR_HV_DEFAULT_MASK   (PSSCR_ESL | PSSCR_EC |		    \
				PSSCR_PSLL_MASK | PSSCR_TR_MASK |   \
				PSSCR_MTL_MASK | PSSCR_RL_MASK)
#define PSSCR_EC_SHIFT    20
#define PSSCR_ESL_SHIFT   21
#define GET_PSSCR_EC(x)   (((x) & PSSCR_EC) >> PSSCR_EC_SHIFT)
#define GET_PSSCR_ESL(x)  (((x) & PSSCR_ESL) >> PSSCR_ESL_SHIFT)
#define GET_PSSCR_RL(x)   ((x) & PSSCR_RL_MASK)

#define ERR_EC_ESL_MISMATCH		-1
#define ERR_DEEP_STATE_ESL_MISMATCH	-2

#ifndef __ASSEMBLY__
extern u32 pnv_fastsleep_workaround_at_entry[];
extern u32 pnv_fastsleep_workaround_at_exit[];

extern u64 pnv_first_deep_stop_state;

int validate_psscr_val_mask(u64 *psscr_val, u64 *psscr_mask, u32 flags);
static inline void report_invalid_psscr_val(u64 psscr_val, int err)
{
	switch (err) {
	case ERR_EC_ESL_MISMATCH:
		pr_warn("Invalid psscr 0x%016llx : ESL,EC bits unequal",
			psscr_val);
		break;
	case ERR_DEEP_STATE_ESL_MISMATCH:
		pr_warn("Invalid psscr 0x%016llx : ESL cleared for deep stop-state",
			psscr_val);
	}
}
#endif

#endif

/* Idle state entry routines */
#ifdef	CONFIG_PPC_P7_NAP
#define IDLE_STATE_ENTER_SEQ(IDLE_INST)                         \
	/* Magic NAP/SLEEP/WINKLE mode enter sequence */	\
	std	r0,0(r1);					\
	ptesync;						\
	ld	r0,0(r1);					\
236:	cmpd	cr0,r0,r0;					\
	bne	236b;						\
	IDLE_INST;						\

#define	IDLE_STATE_ENTER_SEQ_NORET(IDLE_INST)			\
	IDLE_STATE_ENTER_SEQ(IDLE_INST)                         \
	b	.
#endif /* CONFIG_PPC_P7_NAP */

#endif
