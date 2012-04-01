/*----------------------------------------
  PERFORMANCE INSTRUMENTATION  
  Guillaume Thouvenin           08/10/98
  David S. Miller               10/06/98
  ---------------------------------------*/
#ifndef PERF_COUNTER_API
#define PERF_COUNTER_API

/* sys_perfctr() interface.  First arg is operation code
 * from enumeration below.  The meaning of further arguments
 * are determined by the operation code.
 *
 * NOTE: This system call is no longer provided, use the perf_events
 *       infrastructure.
 *
 * Pointers which are passed by the user are pointers to 64-bit
 * integers.
 *
 * Once enabled, performance counter state is retained until the
 * process either exits or performs an exec.  That is, performance
 * counters remain enabled for fork/clone children.
 */
enum perfctr_opcode {
	/* Enable UltraSparc performance counters, ARG0 is pointer
	 * to 64-bit accumulator for D0 counter in PIC, ARG1 is pointer
	 * to 64-bit accumulator for D1 counter.  ARG2 is a pointer to
	 * the initial PCR register value to use.
	 */
	PERFCTR_ON,

	/* Disable UltraSparc performance counters.  The PCR is written
	 * with zero and the user counter accumulator pointers and
	 * working PCR register value are forgotten.
	 */
	PERFCTR_OFF,

	/* Add current D0 and D1 PIC values into user pointers given
	 * in PERFCTR_ON operation.  The PIC is cleared before returning.
	 */
	PERFCTR_READ,

	/* Clear the PIC register. */
	PERFCTR_CLRPIC,

	/* Begin using a new PCR value, the pointer to which is passed
	 * in ARG0.  The PIC is also cleared after the new PCR value is
	 * written.
	 */
	PERFCTR_SETPCR,

	/* Store in pointer given in ARG0 the current PCR register value
	 * being used.
	 */
	PERFCTR_GETPCR
};

/* I don't want the kernel's namespace to be polluted with this
 * stuff when this file is included.  --DaveM
 */
#ifndef __KERNEL__

#define  PRIV 0x00000001
#define  SYS  0x00000002
#define  USR  0x00000004

/* Pic.S0 Selection Bit Field Encoding, Ultra-I/II  */
#define  CYCLE_CNT            0x00000000
#define  INSTR_CNT            0x00000010
#define  DISPATCH0_IC_MISS    0x00000020
#define  DISPATCH0_STOREBUF   0x00000030
#define  IC_REF               0x00000080
#define  DC_RD                0x00000090
#define  DC_WR                0x000000A0
#define  LOAD_USE             0x000000B0
#define  EC_REF               0x000000C0
#define  EC_WRITE_HIT_RDO     0x000000D0
#define  EC_SNOOP_INV         0x000000E0
#define  EC_RD_HIT            0x000000F0

/* Pic.S0 Selection Bit Field Encoding, Ultra-III  */
#define  US3_CYCLE_CNT	      	0x00000000
#define  US3_INSTR_CNT	      	0x00000010
#define  US3_DISPATCH0_IC_MISS	0x00000020
#define  US3_DISPATCH0_BR_TGT	0x00000030
#define  US3_DISPATCH0_2ND_BR	0x00000040
#define  US3_RSTALL_STOREQ	0x00000050
#define  US3_RSTALL_IU_USE	0x00000060
#define  US3_IC_REF		0x00000080
#define  US3_DC_RD		0x00000090
#define  US3_DC_WR		0x000000a0
#define  US3_EC_REF		0x000000c0
#define  US3_EC_WR_HIT_RTO	0x000000d0
#define  US3_EC_SNOOP_INV	0x000000e0
#define  US3_EC_RD_MISS		0x000000f0
#define  US3_PC_PORT0_RD	0x00000100
#define  US3_SI_SNOOP		0x00000110
#define  US3_SI_CIQ_FLOW	0x00000120
#define  US3_SI_OWNED		0x00000130
#define  US3_SW_COUNT_0		0x00000140
#define  US3_IU_BR_MISS_TAKEN	0x00000150
#define  US3_IU_BR_COUNT_TAKEN	0x00000160
#define  US3_DISP_RS_MISPRED	0x00000170
#define  US3_FA_PIPE_COMPL	0x00000180
#define  US3_MC_READS_0		0x00000200
#define  US3_MC_READS_1		0x00000210
#define  US3_MC_READS_2		0x00000220
#define  US3_MC_READS_3		0x00000230
#define  US3_MC_STALLS_0	0x00000240
#define  US3_MC_STALLS_2	0x00000250

/* Pic.S1 Selection Bit Field Encoding, Ultra-I/II  */
#define  CYCLE_CNT_D1         0x00000000
#define  INSTR_CNT_D1         0x00000800
#define  DISPATCH0_IC_MISPRED 0x00001000
#define  DISPATCH0_FP_USE     0x00001800
#define  IC_HIT               0x00004000
#define  DC_RD_HIT            0x00004800
#define  DC_WR_HIT            0x00005000
#define  LOAD_USE_RAW         0x00005800
#define  EC_HIT               0x00006000
#define  EC_WB                0x00006800
#define  EC_SNOOP_CB          0x00007000
#define  EC_IT_HIT            0x00007800

/* Pic.S1 Selection Bit Field Encoding, Ultra-III  */
#define  US3_CYCLE_CNT_D1	0x00000000
#define  US3_INSTR_CNT_D1	0x00000800
#define  US3_DISPATCH0_MISPRED	0x00001000
#define  US3_IC_MISS_CANCELLED	0x00001800
#define  US3_RE_ENDIAN_MISS	0x00002000
#define  US3_RE_FPU_BYPASS	0x00002800
#define  US3_RE_DC_MISS		0x00003000
#define  US3_RE_EC_MISS		0x00003800
#define  US3_IC_MISS		0x00004000
#define  US3_DC_RD_MISS		0x00004800
#define  US3_DC_WR_MISS		0x00005000
#define  US3_RSTALL_FP_USE	0x00005800
#define  US3_EC_MISSES		0x00006000
#define  US3_EC_WB		0x00006800
#define  US3_EC_SNOOP_CB	0x00007000
#define  US3_EC_IC_MISS		0x00007800
#define  US3_RE_PC_MISS		0x00008000
#define  US3_ITLB_MISS		0x00008800
#define  US3_DTLB_MISS		0x00009000
#define  US3_WC_MISS		0x00009800
#define  US3_WC_SNOOP_CB	0x0000a000
#define  US3_WC_SCRUBBED	0x0000a800
#define  US3_WC_WB_WO_READ	0x0000b000
#define  US3_PC_SOFT_HIT	0x0000c000
#define  US3_PC_SNOOP_INV	0x0000c800
#define  US3_PC_HARD_HIT	0x0000d000
#define  US3_PC_PORT1_RD	0x0000d800
#define  US3_SW_COUNT_1		0x0000e000
#define  US3_IU_STAT_BR_MIS_UNTAKEN	0x0000e800
#define  US3_IU_STAT_BR_COUNT_UNTAKEN	0x0000f000
#define  US3_PC_MS_MISSES	0x0000f800
#define  US3_MC_WRITES_0	0x00010800
#define  US3_MC_WRITES_1	0x00011000
#define  US3_MC_WRITES_2	0x00011800
#define  US3_MC_WRITES_3	0x00012000
#define  US3_MC_STALLS_1	0x00012800
#define  US3_MC_STALLS_3	0x00013000
#define  US3_RE_RAW_MISS	0x00013800
#define  US3_FM_PIPE_COMPLETION	0x00014000

struct vcounter_struct {
  unsigned long long vcnt0;
  unsigned long long vcnt1;
};

#else /* !(__KERNEL__) */

#ifndef CONFIG_SPARC32

/* Performance counter register access. */
#define read_pcr(__p)  __asm__ __volatile__("rd	%%pcr, %0" : "=r" (__p))
#define write_pcr(__p) __asm__ __volatile__("wr	%0, 0x0, %%pcr" : : "r" (__p))
#define read_pic(__p)  __asm__ __volatile__("rd %%pic, %0" : "=r" (__p))

/* Blackbird errata workaround.  See commentary in
 * arch/sparc64/kernel/smp.c:smp_percpu_timer_interrupt()
 * for more information.
 */
#define write_pic(__p)  					\
	__asm__ __volatile__("ba,pt	%%xcc, 99f\n\t"		\
			     " nop\n\t"				\
			     ".align	64\n"			\
			  "99:wr	%0, 0x0, %%pic\n\t"	\
			     "rd	%%pic, %%g0" : : "r" (__p))
#define reset_pic()	write_pic(0)

#endif /* !CONFIG_SPARC32 */

#endif /* !(__KERNEL__) */

#endif /* !(PERF_COUNTER_API) */
