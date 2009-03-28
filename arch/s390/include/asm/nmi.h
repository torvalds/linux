/*
 *   Machine check handler definitions
 *
 *    Copyright IBM Corp. 2000,2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#ifndef _ASM_S390_NMI_H
#define _ASM_S390_NMI_H

#include <linux/types.h>

struct mci {
	__u32 sd :  1; /* 00 system damage */
	__u32 pd :  1; /* 01 instruction-processing damage */
	__u32 sr :  1; /* 02 system recovery */
	__u32	 :  1; /* 03 */
	__u32 cd :  1; /* 04 timing-facility damage */
	__u32 ed :  1; /* 05 external damage */
	__u32	 :  1; /* 06 */
	__u32 dg :  1; /* 07 degradation */
	__u32 w  :  1; /* 08 warning pending */
	__u32 cp :  1; /* 09 channel-report pending */
	__u32 sp :  1; /* 10 service-processor damage */
	__u32 ck :  1; /* 11 channel-subsystem damage */
	__u32	 :  2; /* 12-13 */
	__u32 b  :  1; /* 14 backed up */
	__u32	 :  1; /* 15 */
	__u32 se :  1; /* 16 storage error uncorrected */
	__u32 sc :  1; /* 17 storage error corrected */
	__u32 ke :  1; /* 18 storage-key error uncorrected */
	__u32 ds :  1; /* 19 storage degradation */
	__u32 wp :  1; /* 20 psw mwp validity */
	__u32 ms :  1; /* 21 psw mask and key validity */
	__u32 pm :  1; /* 22 psw program mask and cc validity */
	__u32 ia :  1; /* 23 psw instruction address validity */
	__u32 fa :  1; /* 24 failing storage address validity */
	__u32	 :  1; /* 25 */
	__u32 ec :  1; /* 26 external damage code validity */
	__u32 fp :  1; /* 27 floating point register validity */
	__u32 gr :  1; /* 28 general register validity */
	__u32 cr :  1; /* 29 control register validity */
	__u32	 :  1; /* 30 */
	__u32 st :  1; /* 31 storage logical validity */
	__u32 ie :  1; /* 32 indirect storage error */
	__u32 ar :  1; /* 33 access register validity */
	__u32 da :  1; /* 34 delayed access exception */
	__u32	 :  7; /* 35-41 */
	__u32 pr :  1; /* 42 tod programmable register validity */
	__u32 fc :  1; /* 43 fp control register validity */
	__u32 ap :  1; /* 44 ancillary report */
	__u32	 :  1; /* 45 */
	__u32 ct :  1; /* 46 cpu timer validity */
	__u32 cc :  1; /* 47 clock comparator validity */
	__u32	 : 16; /* 47-63 */
};

struct pt_regs;

extern void s390_handle_mcck(void);
extern void s390_do_machine_check(struct pt_regs *regs);

#endif /* _ASM_S390_NMI_H */
