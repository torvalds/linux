#ifndef _ASM_S390_CPU_MF_H
#define _ASM_S390_CPU_MF_H

#define CPU_MF_INT_SF_MASK	0xffc00000

#define CPU_MF_INT_SF_IAE	(1 << 31)	/* invalid entry address */
#define CPU_MF_INT_SF_ISE	(1 << 30)	/* incorrect SDBT entry */
#define CPU_MF_INT_SF_PRA	(1 << 29)	/* program request alert */
#define CPU_MF_INT_SF_SACA	(1 << 23)	/* sampler auth. change alert */
#define CPU_MF_INT_SF_LSDA	(1 << 22)	/* loss of sample data alert */

#endif
