/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *     Sebastian Siewior <bigeasy@linutronix.de>
 *     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ISP1760_REGS_H_
#define _ISP1760_REGS_H_

/* -----------------------------------------------------------------------------
 * Host Controller
 */

/* EHCI capability registers */
#define HC_CAPLENGTH		0x000
#define HC_LENGTH(p)		(((p) >> 00) & 0x00ff)	/* bits 7:0 */
#define HC_VERSION(p)		(((p) >> 16) & 0xffff)	/* bits 31:16 */

#define HC_HCSPARAMS		0x004
#define HCS_INDICATOR(p)	((p) & (1 << 16))	/* true: has port indicators */
#define HCS_PPC(p)		((p) & (1 << 4))	/* true: port power control */
#define HCS_N_PORTS(p)		(((p) >> 0) & 0xf)	/* bits 3:0, ports on HC */

#define HC_HCCPARAMS		0x008
#define HCC_ISOC_CACHE(p)       ((p) & (1 << 7))	/* true: can cache isoc frame */
#define HCC_ISOC_THRES(p)       (((p) >> 4) & 0x7)	/* bits 6:4, uframes cached */

/* EHCI operational registers */
#define HC_USBCMD		0x020
#define CMD_LRESET		(1 << 7)		/* partial reset (no ports, etc) */
#define CMD_RESET		(1 << 1)		/* reset HC not bus */
#define CMD_RUN			(1 << 0)		/* start/stop HC */

#define HC_USBSTS		0x024
#define STS_PCD			(1 << 2)		/* port change detect */

#define HC_FRINDEX		0x02c

#define HC_CONFIGFLAG		0x060
#define FLAG_CF			(1 << 0)		/* true: we'll support "high speed" */

#define HC_PORTSC1		0x064
#define PORT_OWNER		(1 << 13)		/* true: companion hc owns this port */
#define PORT_POWER		(1 << 12)		/* true: has power (see PPC) */
#define PORT_USB11(x)		(((x) & (3 << 10)) == (1 << 10))	/* USB 1.1 device */
#define PORT_RESET		(1 << 8)		/* reset port */
#define PORT_SUSPEND		(1 << 7)		/* suspend port */
#define PORT_RESUME		(1 << 6)		/* resume it */
#define PORT_PE			(1 << 2)		/* port enable */
#define PORT_CSC		(1 << 1)		/* connect status change */
#define PORT_CONNECT		(1 << 0)		/* device connected */
#define PORT_RWC_BITS		(PORT_CSC)

#define HC_ISO_PTD_DONEMAP_REG	0x130
#define HC_ISO_PTD_SKIPMAP_REG	0x134
#define HC_ISO_PTD_LASTPTD_REG	0x138
#define HC_INT_PTD_DONEMAP_REG	0x140
#define HC_INT_PTD_SKIPMAP_REG	0x144
#define HC_INT_PTD_LASTPTD_REG	0x148
#define HC_ATL_PTD_DONEMAP_REG	0x150
#define HC_ATL_PTD_SKIPMAP_REG	0x154
#define HC_ATL_PTD_LASTPTD_REG	0x158

/* Configuration Register */
#define HC_HW_MODE_CTRL		0x300
#define ALL_ATX_RESET		(1 << 31)
#define HW_ANA_DIGI_OC		(1 << 15)
#define HW_DEV_DMA		(1 << 11)
#define HW_COMN_IRQ		(1 << 10)
#define HW_COMN_DMA		(1 << 9)
#define HW_DATA_BUS_32BIT	(1 << 8)
#define HW_DACK_POL_HIGH	(1 << 6)
#define HW_DREQ_POL_HIGH	(1 << 5)
#define HW_INTR_HIGH_ACT	(1 << 2)
#define HW_INTR_EDGE_TRIG	(1 << 1)
#define HW_GLOBAL_INTR_EN	(1 << 0)

#define HC_CHIP_ID_REG		0x304
#define HC_SCRATCH_REG		0x308

#define HC_RESET_REG		0x30c
#define SW_RESET_RESET_HC	(1 << 1)
#define SW_RESET_RESET_ALL	(1 << 0)

#define HC_BUFFER_STATUS_REG	0x334
#define ISO_BUF_FILL		(1 << 2)
#define INT_BUF_FILL		(1 << 1)
#define ATL_BUF_FILL		(1 << 0)

#define HC_MEMORY_REG		0x33c
#define ISP_BANK(x)		((x) << 16)

#define HC_PORT1_CTRL		0x374
#define PORT1_POWER		(3 << 3)
#define PORT1_INIT1		(1 << 7)
#define PORT1_INIT2		(1 << 23)
#define HW_OTG_CTRL_SET		0x374
#define HW_OTG_CTRL_CLR		0x376
#define HW_OTG_DISABLE		(1 << 10)
#define HW_OTG_SE0_EN		(1 << 9)
#define HW_BDIS_ACON_EN		(1 << 8)
#define HW_SW_SEL_HC_DC		(1 << 7)
#define HW_VBUS_CHRG		(1 << 6)
#define HW_VBUS_DISCHRG		(1 << 5)
#define HW_VBUS_DRV		(1 << 4)
#define HW_SEL_CP_EXT		(1 << 3)
#define HW_DM_PULLDOWN		(1 << 2)
#define HW_DP_PULLDOWN		(1 << 1)
#define HW_DP_PULLUP		(1 << 0)

/* Interrupt Register */
#define HC_INTERRUPT_REG	0x310

#define HC_INTERRUPT_ENABLE	0x314
#define HC_ISO_INT		(1 << 9)
#define HC_ATL_INT		(1 << 8)
#define HC_INTL_INT		(1 << 7)
#define HC_EOT_INT		(1 << 3)
#define HC_SOT_INT		(1 << 1)
#define INTERRUPT_ENABLE_MASK	(HC_INTL_INT | HC_ATL_INT)

#define HC_ISO_IRQ_MASK_OR_REG	0x318
#define HC_INT_IRQ_MASK_OR_REG	0x31c
#define HC_ATL_IRQ_MASK_OR_REG	0x320
#define HC_ISO_IRQ_MASK_AND_REG	0x324
#define HC_INT_IRQ_MASK_AND_REG	0x328
#define HC_ATL_IRQ_MASK_AND_REG	0x32c

/* -----------------------------------------------------------------------------
 * Peripheral Controller
 */

/* Initialization Registers */
#define DC_ADDRESS			0x0200
#define DC_DEVEN			(1 << 7)

#define DC_MODE				0x020c
#define DC_DMACLKON			(1 << 9)
#define DC_VBUSSTAT			(1 << 8)
#define DC_CLKAON			(1 << 7)
#define DC_SNDRSU			(1 << 6)
#define DC_GOSUSP			(1 << 5)
#define DC_SFRESET			(1 << 4)
#define DC_GLINTENA			(1 << 3)
#define DC_WKUPCS			(1 << 2)

#define DC_INTCONF			0x0210
#define DC_CDBGMOD_ACK_NAK		(0 << 6)
#define DC_CDBGMOD_ACK			(1 << 6)
#define DC_CDBGMOD_ACK_1NAK		(2 << 6)
#define DC_DDBGMODIN_ACK_NAK		(0 << 4)
#define DC_DDBGMODIN_ACK		(1 << 4)
#define DC_DDBGMODIN_ACK_1NAK		(2 << 4)
#define DC_DDBGMODOUT_ACK_NYET_NAK	(0 << 2)
#define DC_DDBGMODOUT_ACK_NYET		(1 << 2)
#define DC_DDBGMODOUT_ACK_NYET_1NAK	(2 << 2)
#define DC_INTLVL			(1 << 1)
#define DC_INTPOL			(1 << 0)

#define DC_DEBUG			0x0212
#define DC_INTENABLE			0x0214
#define DC_IEPTX(n)			(1 << (11 + 2 * (n)))
#define DC_IEPRX(n)			(1 << (10 + 2 * (n)))
#define DC_IEPRXTX(n)			(3 << (10 + 2 * (n)))
#define DC_IEP0SETUP			(1 << 8)
#define DC_IEVBUS			(1 << 7)
#define DC_IEDMA			(1 << 6)
#define DC_IEHS_STA			(1 << 5)
#define DC_IERESM			(1 << 4)
#define DC_IESUSP			(1 << 3)
#define DC_IEPSOF			(1 << 2)
#define DC_IESOF			(1 << 1)
#define DC_IEBRST			(1 << 0)

/* Data Flow Registers */
#define DC_EPINDEX			0x022c
#define DC_EP0SETUP			(1 << 5)
#define DC_ENDPIDX(n)			((n) << 1)
#define DC_EPDIR			(1 << 0)

#define DC_CTRLFUNC			0x0228
#define DC_CLBUF			(1 << 4)
#define DC_VENDP			(1 << 3)
#define DC_DSEN				(1 << 2)
#define DC_STATUS			(1 << 1)
#define DC_STALL			(1 << 0)

#define DC_DATAPORT			0x0220
#define DC_BUFLEN			0x021c
#define DC_DATACOUNT_MASK		0xffff
#define DC_BUFSTAT			0x021e
#define DC_EPMAXPKTSZ			0x0204

#define DC_EPTYPE			0x0208
#define DC_NOEMPKT			(1 << 4)
#define DC_EPENABLE			(1 << 3)
#define DC_DBLBUF			(1 << 2)
#define DC_ENDPTYP_ISOC			(1 << 0)
#define DC_ENDPTYP_BULK			(2 << 0)
#define DC_ENDPTYP_INTERRUPT		(3 << 0)

/* DMA Registers */
#define DC_DMACMD			0x0230
#define DC_DMATXCOUNT			0x0234
#define DC_DMACONF			0x0238
#define DC_DMAHW			0x023c
#define DC_DMAINTREASON			0x0250
#define DC_DMAINTEN			0x0254
#define DC_DMAEP			0x0258
#define DC_DMABURSTCOUNT		0x0264

/* General Registers */
#define DC_INTERRUPT			0x0218
#define DC_CHIPID			0x0270
#define DC_FRAMENUM			0x0274
#define DC_SCRATCH			0x0278
#define DC_UNLOCKDEV			0x027c
#define DC_INTPULSEWIDTH		0x0280
#define DC_TESTMODE			0x0284

#endif
