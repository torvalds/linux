/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Machine check handler definitions
 *
 *    Copyright IBM Corp. 2000, 2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 */

#ifndef _ASM_S390_NMI_H
#define _ASM_S390_NMI_H

#include <linux/bits.h>
#include <linux/types.h>

#define MCIC_SUBCLASS_MASK	(1ULL<<63 | 1ULL<<62 | 1ULL<<61 | \
				1ULL<<59 | 1ULL<<58 | 1ULL<<56 | \
				1ULL<<55 | 1ULL<<54 | 1ULL<<53 | \
				1ULL<<52 | 1ULL<<47 | 1ULL<<46 | \
				1ULL<<45 | 1ULL<<44)
#define MCCK_CODE_SYSTEM_DAMAGE		BIT(63)
#define MCCK_CODE_EXT_DAMAGE		BIT(63 - 5)
#define MCCK_CODE_CP			BIT(63 - 9)
#define MCCK_CODE_STG_ERROR		BIT(63 - 16)
#define MCCK_CODE_STG_KEY_ERROR		BIT(63 - 18)
#define MCCK_CODE_STG_DEGRAD		BIT(63 - 19)
#define MCCK_CODE_PSW_MWP_VALID		BIT(63 - 20)
#define MCCK_CODE_PSW_IA_VALID		BIT(63 - 23)
#define MCCK_CODE_STG_FAIL_ADDR		BIT(63 - 24)
#define MCCK_CODE_CR_VALID		BIT(63 - 29)
#define MCCK_CODE_GS_VALID		BIT(63 - 36)
#define MCCK_CODE_FC_VALID		BIT(63 - 43)
#define MCCK_CODE_CPU_TIMER_VALID	BIT(63 - 46)

#ifndef __ASSEMBLY__

union mci {
	unsigned long val;
	struct {
		u64 sd :  1; /* 00 system damage */
		u64 pd :  1; /* 01 instruction-processing damage */
		u64 sr :  1; /* 02 system recovery */
		u64    :  1; /* 03 */
		u64 cd :  1; /* 04 timing-facility damage */
		u64 ed :  1; /* 05 external damage */
		u64    :  1; /* 06 */
		u64 dg :  1; /* 07 degradation */
		u64 w  :  1; /* 08 warning pending */
		u64 cp :  1; /* 09 channel-report pending */
		u64 sp :  1; /* 10 service-processor damage */
		u64 ck :  1; /* 11 channel-subsystem damage */
		u64    :  2; /* 12-13 */
		u64 b  :  1; /* 14 backed up */
		u64    :  1; /* 15 */
		u64 se :  1; /* 16 storage error uncorrected */
		u64 sc :  1; /* 17 storage error corrected */
		u64 ke :  1; /* 18 storage-key error uncorrected */
		u64 ds :  1; /* 19 storage degradation */
		u64 wp :  1; /* 20 psw mwp validity */
		u64 ms :  1; /* 21 psw mask and key validity */
		u64 pm :  1; /* 22 psw program mask and cc validity */
		u64 ia :  1; /* 23 psw instruction address validity */
		u64 fa :  1; /* 24 failing storage address validity */
		u64 vr :  1; /* 25 vector register validity */
		u64 ec :  1; /* 26 external damage code validity */
		u64 fp :  1; /* 27 floating point register validity */
		u64 gr :  1; /* 28 general register validity */
		u64 cr :  1; /* 29 control register validity */
		u64    :  1; /* 30 */
		u64 st :  1; /* 31 storage logical validity */
		u64 ie :  1; /* 32 indirect storage error */
		u64 ar :  1; /* 33 access register validity */
		u64 da :  1; /* 34 delayed access exception */
		u64    :  1; /* 35 */
		u64 gs :  1; /* 36 guarded storage registers validity */
		u64    :  5; /* 37-41 */
		u64 pr :  1; /* 42 tod programmable register validity */
		u64 fc :  1; /* 43 fp control register validity */
		u64 ap :  1; /* 44 ancillary report */
		u64    :  1; /* 45 */
		u64 ct :  1; /* 46 cpu timer validity */
		u64 cc :  1; /* 47 clock comparator validity */
		u64    : 16; /* 47-63 */
	};
};

#define MCESA_ORIGIN_MASK	(~0x3ffUL)
#define MCESA_LC_MASK		(0xfUL)
#define MCESA_MIN_SIZE		(1024)
#define MCESA_MAX_SIZE		(2048)

struct mcesa {
	u8 vector_save_area[1024];
	u8 guarded_storage_save_area[32];
};

struct pt_regs;

void nmi_alloc_mcesa_early(u64 *mcesad);
int nmi_alloc_mcesa(u64 *mcesad);
void nmi_free_mcesa(u64 *mcesad);

void s390_handle_mcck(void);
void __s390_handle_mcck(void);
int s390_do_machine_check(struct pt_regs *regs);

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_NMI_H */
