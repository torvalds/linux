/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Data definitions for channel report processing
 *    Copyright IBM Corp. 2000, 2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#ifndef _ASM_S390_CRW_H
#define _ASM_S390_CRW_H

#include <linux/types.h>

/*
 * Channel Report Word
 */
struct crw {
	__u32 res1 :  1;   /* reserved zero */
	__u32 slct :  1;   /* solicited */
	__u32 oflw :  1;   /* overflow */
	__u32 chn  :  1;   /* chained */
	__u32 rsc  :  4;   /* reporting source code */
	__u32 anc  :  1;   /* ancillary report */
	__u32 res2 :  1;   /* reserved zero */
	__u32 erc  :  6;   /* error-recovery code */
	__u32 rsid : 16;   /* reporting-source ID */
} __attribute__ ((packed));

typedef void (*crw_handler_t)(struct crw *, struct crw *, int);

extern int crw_register_handler(int rsc, crw_handler_t handler);
extern void crw_unregister_handler(int rsc);
extern void crw_handle_channel_report(void);
void crw_wait_for_channel_report(void);

#define NR_RSCS 16

#define CRW_RSC_MONITOR  0x2  /* monitoring facility */
#define CRW_RSC_SCH	 0x3  /* subchannel */
#define CRW_RSC_CPATH	 0x4  /* channel path */
#define CRW_RSC_CONFIG	 0x9  /* configuration-alert facility */
#define CRW_RSC_CSS	 0xB  /* channel subsystem */

#define CRW_ERC_EVENT	 0x00 /* event information pending */
#define CRW_ERC_AVAIL	 0x01 /* available */
#define CRW_ERC_INIT	 0x02 /* initialized */
#define CRW_ERC_TERROR	 0x03 /* temporary error */
#define CRW_ERC_IPARM	 0x04 /* installed parm initialized */
#define CRW_ERC_TERM	 0x05 /* terminal */
#define CRW_ERC_PERRN	 0x06 /* perm. error, fac. not init */
#define CRW_ERC_PERRI	 0x07 /* perm. error, facility init */
#define CRW_ERC_PMOD	 0x08 /* installed parameters modified */

#endif /* _ASM_S390_CRW_H */
