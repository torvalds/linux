/*
** asm/bootinfo-hp300.h -- HP9000/300-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_HP300_H
#define _UAPI_ASM_M68K_BOOTINFO_HP300_H


    /*
     *  HP9000/300-specific tags
     */

#define BI_HP300_MODEL		0x8000	/* model (u_long) */
#define BI_HP300_UART_SCODE	0x8001	/* UART select code (u_long) */
#define BI_HP300_UART_ADDR	0x8002	/* phys. addr of UART (u_long) */


    /*
     *  Latest HP9000/300 bootinfo version
     */

#define HP300_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_HP300_H */
