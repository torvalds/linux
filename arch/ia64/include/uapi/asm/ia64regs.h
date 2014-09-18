/*
 * Copyright (C) 2002,2003 Intel Corp.
 *      Jun Nakajima <jun.nakajima@intel.com>
 *      Suresh Siddha <suresh.b.siddha@intel.com>
 */

#ifndef _ASM_IA64_IA64REGS_H
#define _ASM_IA64_IA64REGS_H

/*
 * Register Names for getreg() and setreg().
 *
 * The "magic" numbers happen to match the values used by the Intel compiler's
 * getreg()/setreg() intrinsics.
 */

/* Special Registers */

#define _IA64_REG_IP		1016	/* getreg only */
#define _IA64_REG_PSR		1019
#define _IA64_REG_PSR_L		1019

/* General Integer Registers */

#define _IA64_REG_GP		1025	/* R1 */
#define _IA64_REG_R8		1032	/* R8 */
#define _IA64_REG_R9		1033	/* R9 */
#define _IA64_REG_SP		1036	/* R12 */
#define _IA64_REG_TP		1037	/* R13 */

/* Application Registers */

#define _IA64_REG_AR_KR0	3072
#define _IA64_REG_AR_KR1	3073
#define _IA64_REG_AR_KR2	3074
#define _IA64_REG_AR_KR3	3075
#define _IA64_REG_AR_KR4	3076
#define _IA64_REG_AR_KR5	3077
#define _IA64_REG_AR_KR6	3078
#define _IA64_REG_AR_KR7	3079
#define _IA64_REG_AR_RSC	3088
#define _IA64_REG_AR_BSP	3089
#define _IA64_REG_AR_BSPSTORE	3090
#define _IA64_REG_AR_RNAT	3091
#define _IA64_REG_AR_FCR	3093
#define _IA64_REG_AR_EFLAG	3096
#define _IA64_REG_AR_CSD	3097
#define _IA64_REG_AR_SSD	3098
#define _IA64_REG_AR_CFLAG	3099
#define _IA64_REG_AR_FSR	3100
#define _IA64_REG_AR_FIR	3101
#define _IA64_REG_AR_FDR	3102
#define _IA64_REG_AR_CCV	3104
#define _IA64_REG_AR_UNAT	3108
#define _IA64_REG_AR_FPSR	3112
#define _IA64_REG_AR_ITC	3116
#define _IA64_REG_AR_PFS	3136
#define _IA64_REG_AR_LC		3137
#define _IA64_REG_AR_EC		3138

/* Control Registers */

#define _IA64_REG_CR_DCR	4096
#define _IA64_REG_CR_ITM	4097
#define _IA64_REG_CR_IVA	4098
#define _IA64_REG_CR_PTA	4104
#define _IA64_REG_CR_IPSR	4112
#define _IA64_REG_CR_ISR	4113
#define _IA64_REG_CR_IIP	4115
#define _IA64_REG_CR_IFA	4116
#define _IA64_REG_CR_ITIR	4117
#define _IA64_REG_CR_IIPA	4118
#define _IA64_REG_CR_IFS	4119
#define _IA64_REG_CR_IIM	4120
#define _IA64_REG_CR_IHA	4121
#define _IA64_REG_CR_LID	4160
#define _IA64_REG_CR_IVR	4161	/* getreg only */
#define _IA64_REG_CR_TPR	4162
#define _IA64_REG_CR_EOI	4163
#define _IA64_REG_CR_IRR0	4164	/* getreg only */
#define _IA64_REG_CR_IRR1	4165	/* getreg only */
#define _IA64_REG_CR_IRR2	4166	/* getreg only */
#define _IA64_REG_CR_IRR3	4167	/* getreg only */
#define _IA64_REG_CR_ITV	4168
#define _IA64_REG_CR_PMV	4169
#define _IA64_REG_CR_CMCV	4170
#define _IA64_REG_CR_LRR0	4176
#define _IA64_REG_CR_LRR1	4177

/* Indirect Registers for getindreg() and setindreg() */

#define _IA64_REG_INDR_CPUID	9000	/* getindreg only */
#define _IA64_REG_INDR_DBR	9001
#define _IA64_REG_INDR_IBR	9002
#define _IA64_REG_INDR_PKR	9003
#define _IA64_REG_INDR_PMC	9004
#define _IA64_REG_INDR_PMD	9005
#define _IA64_REG_INDR_RR	9006

#endif /* _ASM_IA64_IA64REGS_H */
