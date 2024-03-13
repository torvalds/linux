/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
** asm/bootinfo-amiga.h -- Amiga-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_AMIGA_H
#define _UAPI_ASM_M68K_BOOTINFO_AMIGA_H


    /*
     *  Amiga-specific tags
     */

#define BI_AMIGA_MODEL		0x8000	/* model (__be32) */
#define BI_AMIGA_AUTOCON	0x8001	/* AutoConfig device */
					/* (AmigaOS struct ConfigDev) */
#define BI_AMIGA_CHIP_SIZE	0x8002	/* size of Chip RAM (__be32) */
#define BI_AMIGA_VBLANK		0x8003	/* VBLANK frequency (__u8) */
#define BI_AMIGA_PSFREQ		0x8004	/* power supply frequency (__u8) */
#define BI_AMIGA_ECLOCK		0x8005	/* EClock frequency (__be32) */
#define BI_AMIGA_CHIPSET	0x8006	/* native chipset present (__be32) */
#define BI_AMIGA_SERPER		0x8007	/* serial port period (__be16) */


    /*
     *  Amiga models (BI_AMIGA_MODEL)
     */

#define AMI_UNKNOWN		0
#define AMI_500			1
#define AMI_500PLUS		2
#define AMI_600			3
#define AMI_1000		4
#define AMI_1200		5
#define AMI_2000		6
#define AMI_2500		7
#define AMI_3000		8
#define AMI_3000T		9
#define AMI_3000PLUS		10
#define AMI_4000		11
#define AMI_4000T		12
#define AMI_CDTV		13
#define AMI_CD32		14
#define AMI_DRACO		15


    /*
     *  Amiga chipsets (BI_AMIGA_CHIPSET)
     */

#define CS_STONEAGE		0
#define CS_OCS			1
#define CS_ECS			2
#define CS_AGA			3


    /*
     *  Latest Amiga bootinfo version
     */

#define AMIGA_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_AMIGA_H */
