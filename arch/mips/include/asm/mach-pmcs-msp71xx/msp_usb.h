/******************************************************************
 * Copyright (c) 2000-2007 PMC-Sierra INC.
 *
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General
 *     Public License as published by the Free Software
 *     Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 *     02139, USA.
 *
 * PMC-SIERRA INC. DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS
 * SOFTWARE.
 */
#ifndef MSP_USB_H_
#define MSP_USB_H_

#ifdef CONFIG_MSP_HAS_DUAL_USB
#define NUM_USB_DEVS   2
#else
#define NUM_USB_DEVS   1
#endif

/* Register spaces for USB host 0 */
#define MSP_USB0_MAB_START	(MSP_USB0_BASE + 0x0)
#define MSP_USB0_MAB_END	(MSP_USB0_BASE + 0x17)
#define MSP_USB0_ID_START	(MSP_USB0_BASE + 0x40000)
#define MSP_USB0_ID_END		(MSP_USB0_BASE + 0x4008f)
#define MSP_USB0_HS_START	(MSP_USB0_BASE + 0x40100)
#define MSP_USB0_HS_END		(MSP_USB0_BASE + 0x401FF)

/* Register spaces for USB host 1 */
#define MSP_USB1_MAB_START	(MSP_USB1_BASE + 0x0)
#define MSP_USB1_MAB_END	(MSP_USB1_BASE + 0x17)
#define MSP_USB1_ID_START	(MSP_USB1_BASE + 0x40000)
#define MSP_USB1_ID_END		(MSP_USB1_BASE + 0x4008f)
#define MSP_USB1_HS_START	(MSP_USB1_BASE + 0x40100)
#define MSP_USB1_HS_END		(MSP_USB1_BASE + 0x401ff)

/* USB Identification registers */
struct msp_usbid_regs {
	u32 id;		/* 0x0: Identification register */
	u32 hwgen;	/* 0x4: General HW params */
	u32 hwhost;	/* 0x8: Host HW params */
	u32 hwdev;	/* 0xc: Device HW params */
	u32 hwtxbuf;	/* 0x10: Tx buffer HW params */
	u32 hwrxbuf;	/* 0x14: Rx buffer HW params */
	u32 reserved[26];
	u32 timer0_load; /* 0x80: General-purpose timer 0 load*/
	u32 timer0_ctrl; /* 0x84: General-purpose timer 0 control */
	u32 timer1_load; /* 0x88: General-purpose timer 1 load*/
	u32 timer1_ctrl; /* 0x8c: General-purpose timer 1 control */
};

/* MSBus to AMBA registers */
struct msp_mab_regs {
	u32 isr;	/* 0x0: Interrupt status */
	u32 imr;	/* 0x4: Interrupt mask */
	u32 thcr0;	/* 0x8: Transaction header capture 0 */
	u32 thcr1;	/* 0xc: Transaction header capture 1 */
	u32 int_stat;	/* 0x10: Interrupt status summary */
	u32 phy_cfg;	/* 0x14: USB phy config */
};

/* EHCI registers */
struct msp_usbhs_regs {
	u32 hciver;	/* 0x0: Version and offset to operational regs */
	u32 hcsparams;	/* 0x4: Host control structural parameters */
	u32 hccparams;	/* 0x8: Host control capability parameters */
	u32 reserved0[5];
	u32 dciver;	/* 0x20: Device interface version */
	u32 dccparams;	/* 0x24: Device control capability parameters */
	u32 reserved1[6];
	u32 cmd;	/* 0x40: USB command */
	u32 sts;	/* 0x44: USB status */
	u32 int_ena;	/* 0x48: USB interrupt enable */
	u32 frindex;	/* 0x4c: Frame index */
	u32 reserved3;
	union {
		struct {
			u32 flb_addr; /* 0x54: Frame list base address */
			u32 next_async_addr; /* 0x58: next asynchronous addr */
			u32 ttctrl; /* 0x5c: embedded transaction translator
							async buffer status */
			u32 burst_size; /* 0x60: Controller burst size */
			u32 tx_fifo_ctrl; /* 0x64: Tx latency FIFO tuning */
			u32 reserved0[4];
			u32 endpt_nak; /* 0x78: Endpoint NAK */
			u32 endpt_nak_ena; /* 0x7c: Endpoint NAK enable */
			u32 cfg_flag; /* 0x80: Config flag */
			u32 port_sc1; /* 0x84: Port status & control 1 */
			u32 reserved1[7];
			u32 otgsc;	/* 0xa4: OTG status & control */
			u32 mode;	/* 0xa8: USB controller mode */
		} host;

		struct {
			u32 dev_addr; /* 0x54: Device address */
			u32 endpt_list_addr; /* 0x58: Endpoint list address */
			u32 reserved0[7];
			u32 endpt_nak;	/* 0x74 */
			u32 endpt_nak_ctrl; /* 0x78 */
			u32 cfg_flag; /* 0x80 */
			u32 port_sc1; /* 0x84: Port status & control 1 */
			u32 reserved[7];
			u32 otgsc;	/* 0xa4: OTG status & control */
			u32 mode;	/* 0xa8: USB controller mode */
			u32 endpt_setup_stat; /* 0xac */
			u32 endpt_prime; /* 0xb0 */
			u32 endpt_flush; /* 0xb4 */
			u32 endpt_stat; /* 0xb8 */
			u32 endpt_complete; /* 0xbc */
			u32 endpt_ctrl0; /* 0xc0 */
			u32 endpt_ctrl1; /* 0xc4 */
			u32 endpt_ctrl2; /* 0xc8 */
			u32 endpt_ctrl3; /* 0xcc */
		} device;
	} u;
};
/*
 * Container for the more-generic platform_device.
 * This exists mainly as a way to map the non-standard register
 * spaces and make them accessible to the USB ISR.
 */
struct mspusb_device {
	struct msp_mab_regs   __iomem *mab_regs;
	struct msp_usbid_regs __iomem *usbid_regs;
	struct msp_usbhs_regs __iomem *usbhs_regs;
	struct platform_device dev;
};

#define to_mspusb_device(x) container_of((x), struct mspusb_device, dev)
#define TO_HOST_ID(x) ((x) & 0x3)
#endif /*MSP_USB_H_*/
