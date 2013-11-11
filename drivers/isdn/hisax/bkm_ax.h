/* $Id: bkm_ax.h,v 1.5.6.3 2001/09/23 22:24:46 kai Exp $
 *
 * low level decls for T-Berkom cards A4T and Scitel Quadro (4*S0, passive)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef	__BKM_AX_H__
#define	__BKM_AX_H__

/* Supported boards	(subtypes) */
#define SCT_1		1
#define	SCT_2		2
#define	SCT_3		3
#define	SCT_4		4
#define BKM_A4T		5

#define	PLX_ADDR_PLX		0x14	/* Addr PLX configuration */
#define	PLX_ADDR_ISAC		0x18	/* Addr ISAC */
#define	PLX_ADDR_HSCX		0x1C	/* Addr HSCX */
#define	PLX_ADDR_ALE		0x20	/* Addr ALE */
#define	PLX_ADDR_ALEPLUS	0x24	/* Next Addr behind ALE */

#define	PLX_SUBVEN		0x2C	/* Offset SubVendor */
#define	PLX_SUBSYS		0x2E	/* Offset SubSystem */


/* Application specific registers I20 (Siemens SZB6120H) */
typedef	struct {
	/* Video front end horizontal configuration register */
	volatile u_int	i20VFEHorzCfg;	/* Offset 00 */
	/* Video front end vertical configuration register */
	volatile u_int	i20VFEVertCfg;	/* Offset 04 */
	/* Video front end scaler and pixel format register */
	volatile u_int	i20VFEScaler;	/* Offset 08 */
	/* Video display top register */
	volatile u_int	i20VDispTop;	/* Offset 0C */
	/* Video display bottom register */
	volatile u_int	i20VDispBottom;	/* Offset 10 */
	/* Video stride, status and frame grab register */
	volatile u_int	i20VidFrameGrab;/* Offset 14 */
	/* Video display configuration register */
	volatile u_int	i20VDispCfg;	/* Offset 18 */
	/* Video masking map top */
	volatile u_int	i20VMaskTop;	/* Offset 1C */
	/* Video masking map bottom */
	volatile u_int	i20VMaskBottom;	/* Offset 20 */
	/* Overlay control register */
	volatile u_int	i20OvlyControl;	/* Offset 24 */
	/* System, PCI and general purpose pins control register */
	volatile u_int	i20SysControl;	/* Offset 28 */
#define	sysRESET		0x01000000	/* bit 24:Softreset (Low)		*/
	/* GPIO 4...0: Output fixed for our cfg! */
#define	sysCFG			0x000000E0	/* GPIO 7,6,5: Input */
	/* General purpose pins and guest bus control register */
	volatile u_int	i20GuestControl;/* Offset 2C */
#define	guestWAIT_CFG	0x00005555	/* 4 PCI waits for all */
#define	guestISDN_INT_E	0x01000000	/* ISDN Int en (low) */
#define	guestVID_INT_E	0x02000000	/* Video interrupt en (low) */
#define	guestADI1_INT_R	0x04000000	/* ADI #1 int req (low) */
#define	guestADI2_INT_R	0x08000000	/* ADI #2 int req (low) */
#define	guestISDN_RES	0x10000000	/* ISDN reset bit (high) */
#define	guestADI1_INT_S	0x20000000	/* ADI #1 int pending (low) */
#define	guestADI2_INT_S	0x40000000	/* ADI #2 int pending (low) */
#define	guestISDN_INT_S	0x80000000	/* ISAC int pending (low) */

#define	g_A4T_JADE_RES	0x01000000	/* JADE Reset (High) */
#define	g_A4T_ISAR_RES	0x02000000	/* ISAR Reset (High) */
#define	g_A4T_ISAC_RES	0x04000000	/* ISAC Reset (High) */
#define	g_A4T_JADE_BOOTR 0x08000000	/* JADE enable boot SRAM (Low) NOT USED */
#define	g_A4T_ISAR_BOOTR 0x10000000	/* ISAR enable boot SRAM (Low) NOT USED */
#define	g_A4T_JADE_INT_S 0x20000000	/* JADE interrupt pnd (Low) */
#define	g_A4T_ISAR_INT_S 0x40000000	/* ISAR interrupt pnd (Low) */
#define	g_A4T_ISAC_INT_S 0x80000000	/* ISAC interrupt pnd (Low) */

	volatile u_int	i20CodeSource;	/* Offset 30 */
	volatile u_int	i20CodeXferCtrl;/* Offset 34 */
	volatile u_int	i20CodeMemPtr;	/* Offset 38 */

	volatile u_int	i20IntStatus;	/* Offset 3C */
	volatile u_int	i20IntCtrl;	/* Offset 40 */
#define	intISDN		0x40000000	/* GIRQ1En (ISAC/ADI) (High) */
#define	intVID		0x20000000	/* GIRQ0En (VSYNC)    (High) */
#define	intCOD		0x10000000	/* CodRepIrqEn        (High) */
#define	intPCI		0x01000000	/* PCI IntA enable    (High) */

	volatile u_int	i20I2CCtrl;	/* Offset 44					*/
} I20_REGISTER_FILE, *PI20_REGISTER_FILE;

/*
 * Postoffice structure for A4T
 *
 */
#define	PO_OFFSET	0x00000200	/* Postoffice offset from base */

#define	GCS_0		0x00000000	/* Guest bus chip selects */
#define	GCS_1		0x00100000
#define	GCS_2		0x00200000
#define	GCS_3		0x00300000

#define	PO_READ		0x00000000	/* R/W from/to guest bus */
#define	PO_WRITE	0x00800000

#define	PO_PEND		0x02000000

#define POSTOFFICE(postoffice) *(volatile unsigned int *)(postoffice)

/* Wait unlimited (don't worry)										*/
#define	__WAITI20__(postoffice)					\
	do {							\
		while ((POSTOFFICE(postoffice) & PO_PEND)) ;	\
	} while (0)

#endif	/* __BKM_AX_H__ */
