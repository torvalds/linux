#ifndef _ASM_IA64_PTRACE_OFFSETS_H
#define _ASM_IA64_PTRACE_OFFSETS_H

/*
 * Copyright (C) 1999, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
/*
 * The "uarea" that can be accessed via PEEKUSER and POKEUSER is a
 * virtual structure that would have the following definition:
 *
 *	struct uarea {
 *		struct ia64_fpreg fph[96];		// f32-f127
 *		unsigned long nat_bits;
 *		unsigned long empty1;
 *		struct ia64_fpreg f2;			// f2-f5
 *			:
 *		struct ia64_fpreg f5;
 *		struct ia64_fpreg f10;			// f10-f31
 *			:
 *		struct ia64_fpreg f31;
 *		unsigned long r4;			// r4-r7
 *			:
 *		unsigned long r7;
 *		unsigned long b1;			// b1-b5
 *			:
 *		unsigned long b5;
 *		unsigned long ar_ec;
 *		unsigned long ar_lc;
 *		unsigned long empty2[5];
 *		unsigned long cr_ipsr;
 *		unsigned long cr_iip;
 *		unsigned long cfm;
 *		unsigned long ar_unat;
 *		unsigned long ar_pfs;
 *		unsigned long ar_rsc;
 *		unsigned long ar_rnat;
 *		unsigned long ar_bspstore;
 *		unsigned long pr;
 *		unsigned long b6;
 *		unsigned long ar_bsp;
 *		unsigned long r1;
 *		unsigned long r2;
 *		unsigned long r3;
 *		unsigned long r12;
 *		unsigned long r13;
 *		unsigned long r14;
 *		unsigned long r15;
 *		unsigned long r8;
 *		unsigned long r9;
 *		unsigned long r10;
 *		unsigned long r11;
 *		unsigned long r16;
 *			:
 *		unsigned long r31;
 *		unsigned long ar_ccv;
 *		unsigned long ar_fpsr;
 *		unsigned long b0;
 *		unsigned long b7;
 *		unsigned long f6;
 *		unsigned long f7;
 *		unsigned long f8;
 *		unsigned long f9;
 *		unsigned long ar_csd;
 *		unsigned long ar_ssd;
 *		unsigned long rsvd1[710];
 *		unsigned long dbr[8];
 *		unsigned long rsvd2[504];
 *		unsigned long ibr[8];
 *		unsigned long rsvd3[504];
 *		unsigned long pmd[4];
 *	}
 */

/* fph: */
#define PT_F32			0x0000
#define PT_F33			0x0010
#define PT_F34			0x0020
#define PT_F35			0x0030
#define PT_F36			0x0040
#define PT_F37			0x0050
#define PT_F38			0x0060
#define PT_F39			0x0070
#define PT_F40			0x0080
#define PT_F41			0x0090
#define PT_F42			0x00a0
#define PT_F43			0x00b0
#define PT_F44			0x00c0
#define PT_F45			0x00d0
#define PT_F46			0x00e0
#define PT_F47			0x00f0
#define PT_F48			0x0100
#define PT_F49			0x0110
#define PT_F50			0x0120
#define PT_F51			0x0130
#define PT_F52			0x0140
#define PT_F53			0x0150
#define PT_F54			0x0160
#define PT_F55			0x0170
#define PT_F56			0x0180
#define PT_F57			0x0190
#define PT_F58			0x01a0
#define PT_F59			0x01b0
#define PT_F60			0x01c0
#define PT_F61			0x01d0
#define PT_F62			0x01e0
#define PT_F63			0x01f0
#define PT_F64			0x0200
#define PT_F65			0x0210
#define PT_F66			0x0220
#define PT_F67			0x0230
#define PT_F68			0x0240
#define PT_F69			0x0250
#define PT_F70			0x0260
#define PT_F71			0x0270
#define PT_F72			0x0280
#define PT_F73			0x0290
#define PT_F74			0x02a0
#define PT_F75			0x02b0
#define PT_F76			0x02c0
#define PT_F77			0x02d0
#define PT_F78			0x02e0
#define PT_F79			0x02f0
#define PT_F80			0x0300
#define PT_F81			0x0310
#define PT_F82			0x0320
#define PT_F83			0x0330
#define PT_F84			0x0340
#define PT_F85			0x0350
#define PT_F86			0x0360
#define PT_F87			0x0370
#define PT_F88			0x0380
#define PT_F89			0x0390
#define PT_F90			0x03a0
#define PT_F91			0x03b0
#define PT_F92			0x03c0
#define PT_F93			0x03d0
#define PT_F94			0x03e0
#define PT_F95			0x03f0
#define PT_F96			0x0400
#define PT_F97			0x0410
#define PT_F98			0x0420
#define PT_F99			0x0430
#define PT_F100			0x0440
#define PT_F101			0x0450
#define PT_F102			0x0460
#define PT_F103			0x0470
#define PT_F104			0x0480
#define PT_F105			0x0490
#define PT_F106			0x04a0
#define PT_F107			0x04b0
#define PT_F108			0x04c0
#define PT_F109			0x04d0
#define PT_F110			0x04e0
#define PT_F111			0x04f0
#define PT_F112			0x0500
#define PT_F113			0x0510
#define PT_F114			0x0520
#define PT_F115			0x0530
#define PT_F116			0x0540
#define PT_F117			0x0550
#define PT_F118			0x0560
#define PT_F119			0x0570
#define PT_F120			0x0580
#define PT_F121			0x0590
#define PT_F122			0x05a0
#define PT_F123			0x05b0
#define PT_F124			0x05c0
#define PT_F125			0x05d0
#define PT_F126			0x05e0
#define PT_F127			0x05f0

#define PT_NAT_BITS		0x0600

#define PT_F2			0x0610
#define PT_F3			0x0620
#define PT_F4			0x0630
#define PT_F5			0x0640
#define PT_F10			0x0650
#define PT_F11			0x0660
#define PT_F12			0x0670
#define PT_F13			0x0680
#define PT_F14			0x0690
#define PT_F15			0x06a0
#define PT_F16			0x06b0
#define PT_F17			0x06c0
#define PT_F18			0x06d0
#define PT_F19			0x06e0
#define PT_F20			0x06f0
#define PT_F21			0x0700
#define PT_F22			0x0710
#define PT_F23			0x0720
#define PT_F24			0x0730
#define PT_F25			0x0740
#define PT_F26			0x0750
#define PT_F27			0x0760
#define PT_F28			0x0770
#define PT_F29			0x0780
#define PT_F30			0x0790
#define PT_F31			0x07a0
#define PT_R4			0x07b0
#define PT_R5			0x07b8
#define PT_R6			0x07c0
#define PT_R7			0x07c8

#define PT_B1			0x07d8
#define PT_B2			0x07e0
#define PT_B3			0x07e8
#define PT_B4			0x07f0
#define PT_B5			0x07f8

#define PT_AR_EC		0x0800
#define PT_AR_LC		0x0808

#define PT_CR_IPSR		0x0830
#define PT_CR_IIP		0x0838
#define PT_CFM			0x0840
#define PT_AR_UNAT		0x0848
#define PT_AR_PFS		0x0850
#define PT_AR_RSC		0x0858
#define PT_AR_RNAT		0x0860
#define PT_AR_BSPSTORE		0x0868
#define PT_PR			0x0870
#define PT_B6			0x0878
#define PT_AR_BSP		0x0880	/* note: this points to the *end* of the backing store! */
#define PT_R1			0x0888
#define PT_R2			0x0890
#define PT_R3			0x0898
#define PT_R12			0x08a0
#define PT_R13			0x08a8
#define PT_R14			0x08b0
#define PT_R15			0x08b8
#define PT_R8 			0x08c0
#define PT_R9			0x08c8
#define PT_R10			0x08d0
#define PT_R11			0x08d8
#define PT_R16			0x08e0
#define PT_R17			0x08e8
#define PT_R18			0x08f0
#define PT_R19			0x08f8
#define PT_R20			0x0900
#define PT_R21			0x0908
#define PT_R22			0x0910
#define PT_R23			0x0918
#define PT_R24			0x0920
#define PT_R25			0x0928
#define PT_R26			0x0930
#define PT_R27			0x0938
#define PT_R28			0x0940
#define PT_R29			0x0948
#define PT_R30			0x0950
#define PT_R31			0x0958
#define PT_AR_CCV		0x0960
#define PT_AR_FPSR		0x0968
#define PT_B0			0x0970
#define PT_B7			0x0978
#define PT_F6			0x0980
#define PT_F7			0x0990
#define PT_F8			0x09a0
#define PT_F9			0x09b0
#define PT_AR_CSD		0x09c0
#define PT_AR_SSD		0x09c8

#define PT_DBR			0x2000	/* data breakpoint registers */
#define PT_IBR			0x3000	/* instruction breakpoint registers */
#define PT_PMD			0x4000	/* performance monitoring counters */

#endif /* _ASM_IA64_PTRACE_OFFSETS_H */
