/*
** asm/bootinfo-atari.h -- Atari-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_ATARI_H
#define _UAPI_ASM_M68K_BOOTINFO_ATARI_H


    /*
     *  Atari-specific tags
     */

#define BI_ATARI_MCH_COOKIE	0x8000	/* _MCH cookie from TOS (__be32) */
#define BI_ATARI_MCH_TYPE	0x8001	/* special machine type (__be32) */


    /*
     *  mch_cookie values (upper word of BI_ATARI_MCH_COOKIE)
     */

#define ATARI_MCH_ST		0
#define ATARI_MCH_STE		1
#define ATARI_MCH_TT		2
#define ATARI_MCH_FALCON	3


    /*
     *  Atari machine types (BI_ATARI_MCH_TYPE)
     */

#define ATARI_MACH_NORMAL	0	/* no special machine type */
#define ATARI_MACH_MEDUSA	1	/* Medusa 040 */
#define ATARI_MACH_HADES	2	/* Hades 040 or 060 */
#define ATARI_MACH_AB40		3	/* Afterburner040 on Falcon */


    /*
     *  Latest Atari bootinfo version
     */

#define ATARI_BOOTI_VERSION	MK_BI_VERSION(2, 1)


#endif /* _UAPI_ASM_M68K_BOOTINFO_ATARI_H */
