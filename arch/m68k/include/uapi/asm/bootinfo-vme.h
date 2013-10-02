/*
** asm/bootinfo-vme.h -- VME-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_VME_H
#define _UAPI_ASM_M68K_BOOTINFO_VME_H


    /*
     *  VME-specific tags
     */

#define BI_VME_TYPE		0x8000	/* VME sub-architecture (u_long) */
#define BI_VME_BRDINFO		0x8001	/* VME board information (struct) */


    /*
     *  VME models (BI_VME_TYPE)
     */

#define VME_TYPE_TP34V		0x0034	/* Tadpole TP34V */
#define VME_TYPE_MVME147	0x0147	/* Motorola MVME147 */
#define VME_TYPE_MVME162	0x0162	/* Motorola MVME162 */
#define VME_TYPE_MVME166	0x0166	/* Motorola MVME166 */
#define VME_TYPE_MVME167	0x0167	/* Motorola MVME167 */
#define VME_TYPE_MVME172	0x0172	/* Motorola MVME172 */
#define VME_TYPE_MVME177	0x0177	/* Motorola MVME177 */
#define VME_TYPE_BVME4000	0x4000	/* BVM Ltd. BVME4000 */
#define VME_TYPE_BVME6000	0x6000	/* BVM Ltd. BVME6000 */

/* BI_VME_BRDINFO is a 32 byte struct as returned by the Bug code on
 * Motorola VME boards.  Contains board number, Bug version, board
 * configuration options, etc.  See include/asm/mvme16xhw.h for details.
 */


    /*
     *  Latest VME bootinfo versions
     */

#define MVME147_BOOTI_VERSION	MK_BI_VERSION(2, 0)
#define MVME16x_BOOTI_VERSION	MK_BI_VERSION(2, 0)
#define BVME6000_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_VME_H */
