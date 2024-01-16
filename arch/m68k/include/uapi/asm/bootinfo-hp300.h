/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
** asm/bootinfo-hp300.h -- HP9000/300-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_HP300_H
#define _UAPI_ASM_M68K_BOOTINFO_HP300_H


    /*
     *  HP9000/300-specific tags
     */

#define BI_HP300_MODEL		0x8000	/* model (__be32) */
#define BI_HP300_UART_SCODE	0x8001	/* UART select code (__be32) */
#define BI_HP300_UART_ADDR	0x8002	/* phys. addr of UART (__be32) */


    /*
     *  HP9000/300 and /400 models (BI_HP300_MODEL)
     *
     * This information was taken from NetBSD
     */

#define HP_320		0	/* 16MHz 68020+HP MMU+16K external cache */
#define HP_330		1	/* 16MHz 68020+68851 MMU */
#define HP_340		2	/* 16MHz 68030 */
#define HP_345		3	/* 50MHz 68030+32K external cache */
#define HP_350		4	/* 25MHz 68020+HP MMU+32K external cache */
#define HP_360		5	/* 25MHz 68030 */
#define HP_370		6	/* 33MHz 68030+64K external cache */
#define HP_375		7	/* 50MHz 68030+32K external cache */
#define HP_380		8	/* 25MHz 68040 */
#define HP_385		9	/* 33MHz 68040 */

#define HP_400		10	/* 50MHz 68030+32K external cache */
#define HP_425T		11	/* 25MHz 68040 - model 425t */
#define HP_425S		12	/* 25MHz 68040 - model 425s */
#define HP_425E		13	/* 25MHz 68040 - model 425e */
#define HP_433T		14	/* 33MHz 68040 - model 433t */
#define HP_433S		15	/* 33MHz 68040 - model 433s */


    /*
     *  Latest HP9000/300 bootinfo version
     */

#define HP300_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_HP300_H */
