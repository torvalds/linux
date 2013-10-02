/*
** asm/bootinfo-amiga.h -- Amiga-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_AMIGA_H
#define _UAPI_ASM_M68K_BOOTINFO_AMIGA_H


    /*
     *  Amiga-specific tags
     */

#define BI_AMIGA_MODEL		0x8000	/* model (u_long) */
#define BI_AMIGA_AUTOCON	0x8001	/* AutoConfig device */
					/* (AmigaOS struct ConfigDev) */
#define BI_AMIGA_CHIP_SIZE	0x8002	/* size of Chip RAM (u_long) */
#define BI_AMIGA_VBLANK		0x8003	/* VBLANK frequency (u_char) */
#define BI_AMIGA_PSFREQ		0x8004	/* power supply frequency (u_char) */
#define BI_AMIGA_ECLOCK		0x8005	/* EClock frequency (u_long) */
#define BI_AMIGA_CHIPSET	0x8006	/* native chipset present (u_long) */
#define BI_AMIGA_SERPER		0x8007	/* serial port period (u_short) */


    /*
     *  Latest Amiga bootinfo version
     */

#define AMIGA_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_AMIGA_H */
