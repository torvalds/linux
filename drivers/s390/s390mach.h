/*
 *  drivers/s390/s390mach.h
 *   S/390 data definitions for machine check processing
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#ifndef __s390mach_h
#define __s390mach_h

#include <asm/types.h>

struct mci {
	__u32   sd              :  1; /* 00 system damage */
	__u32   pd              :  1; /* 01 instruction-processing damage */
	__u32   sr              :  1; /* 02 system recovery */
	__u32   to_be_defined_1 :  4; /* 03-06 */
	__u32   dg              :  1; /* 07 degradation */
	__u32   w               :  1; /* 08 warning pending */
	__u32   cp              :  1; /* 09 channel-report pending */
	__u32   to_be_defined_2 :  6; /* 10-15 */
	__u32   se              :  1; /* 16 storage error uncorrected */
	__u32   sc              :  1; /* 17 storage error corrected */
	__u32   ke              :  1; /* 18 storage-key error uncorrected */
	__u32   ds              :  1; /* 19 storage degradation */
	__u32	to_be_defined_3 :  4; /* 20-23 */
	__u32   fa              :  1; /* 24 failing storage address validity */
	__u32   to_be_defined_4 :  7; /* 25-31 */
	__u32   ie              :  1; /* 32 indirect storage error */
	__u32	to_be_defined_5 : 31; /* 33-63 */
};

/*
 * Channel Report Word
 */
struct crw {
	__u32 res1    :  1;   /* reserved zero */
	__u32 slct    :  1;   /* solicited */
	__u32 oflw    :  1;   /* overflow */
	__u32 chn     :  1;   /* chained */
	__u32 rsc     :  4;   /* reporting source code */
	__u32 anc     :  1;   /* ancillary report */
	__u32 res2    :  1;   /* reserved zero */
	__u32 erc     :  6;   /* error-recovery code */
	__u32 rsid    : 16;   /* reporting-source ID */
} __attribute__ ((packed));

#define CRW_RSC_MONITOR  0x2  /* monitoring facility */
#define CRW_RSC_SCH      0x3  /* subchannel */
#define CRW_RSC_CPATH    0x4  /* channel path */
#define CRW_RSC_CONFIG   0x9  /* configuration-alert facility */
#define CRW_RSC_CSS      0xB  /* channel subsystem */

#define CRW_ERC_EVENT    0x00 /* event information pending */
#define CRW_ERC_AVAIL    0x01 /* available */
#define CRW_ERC_INIT     0x02 /* initialized */
#define CRW_ERC_TERROR   0x03 /* temporary error */
#define CRW_ERC_IPARM    0x04 /* installed parm initialized */
#define CRW_ERC_TERM     0x05 /* terminal */
#define CRW_ERC_PERRN    0x06 /* perm. error, fac. not init */
#define CRW_ERC_PERRI    0x07 /* perm. error, facility init */
#define CRW_ERC_PMOD     0x08 /* installed parameters modified */

extern __inline__ int stcrw(struct crw *pcrw )
{
        int ccode;

        __asm__ __volatile__(
                "STCRW 0(%1)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (pcrw)
                : "cc", "1" );
        return ccode;
}

#endif /* __s390mach */
