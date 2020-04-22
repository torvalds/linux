/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USBSS device controller driver header file
 *
 * Copyright (C) 2018-2019 Cadence.
 * Copyright (C) 2017-2018 NXP
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *         Pawel Jez <pjez@cadence.com>
 *         Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_GADGET
#define __LINUX_CDNS3_GADGET
#include <linux/usb/gadget.h>

/*
 * USBSS-DEV register interface.
 * This corresponds to the USBSS Device Controller Interface
 */

/**
 * struct cdns3_usb_regs - device controller registers.
 * @usb_conf:      Global Configuration.
 * @usb_sts:       Global Status.
 * @usb_cmd:       Global Command.
 * @usb_itpn:      ITP/SOF number.
 * @usb_lpm:       Global Command.
 * @usb_ien:       USB Interrupt Enable.
 * @usb_ists:      USB Interrupt Status.
 * @ep_sel:        Endpoint Select.
 * @ep_traddr:     Endpoint Transfer Ring Address.
 * @ep_cfg:        Endpoint Configuration.
 * @ep_cmd:        Endpoint Command.
 * @ep_sts:        Endpoint Status.
 * @ep_sts_sid:    Endpoint Status.
 * @ep_sts_en:     Endpoint Status Enable.
 * @drbl:          Doorbell.
 * @ep_ien:        EP Interrupt Enable.
 * @ep_ists:       EP Interrupt Status.
 * @usb_pwr:       Global Power Configuration.
 * @usb_conf2:     Global Configuration 2.
 * @usb_cap1:      Capability 1.
 * @usb_cap2:      Capability 2.
 * @usb_cap3:      Capability 3.
 * @usb_cap4:      Capability 4.
 * @usb_cap5:      Capability 5.
 * @usb_cap6:      Capability 6.
 * @usb_cpkt1:     Custom Packet 1.
 * @usb_cpkt2:     Custom Packet 2.
 * @usb_cpkt3:     Custom Packet 3.
 * @ep_dma_ext_addr: Upper address for DMA operations.
 * @buf_addr:      Address for On-chip Buffer operations.
 * @buf_data:      Data for On-chip Buffer operations.
 * @buf_ctrl:      On-chip Buffer Access Control.
 * @dtrans:        DMA Transfer Mode.
 * @tdl_from_trb:  Source of TD Configuration.
 * @tdl_beh:       TDL Behavior Configuration.
 * @ep_tdl:        Endpoint TDL.
 * @tdl_beh2:      TDL Behavior 2 Configuration.
 * @dma_adv_td:    DMA Advance TD Configuration.
 * @reserved1:     Reserved.
 * @cfg_regs:      Configuration.
 * @reserved2:     Reserved.
 * @dma_axi_ctrl:  AXI Control.
 * @dma_axi_id:    AXI ID register.
 * @dma_axi_cap:   AXI Capability.
 * @dma_axi_ctrl0: AXI Control 0.
 * @dma_axi_ctrl1: AXI Control 1.
 */
struct cdns3_usb_regs {
	__le32 usb_conf;
	__le32 usb_sts;
	__le32 usb_cmd;
	__le32 usb_itpn;
	__le32 usb_lpm;
	__le32 usb_ien;
	__le32 usb_ists;
	__le32 ep_sel;
	__le32 ep_traddr;
	__le32 ep_cfg;
	__le32 ep_cmd;
	__le32 ep_sts;
	__le32 ep_sts_sid;
	__le32 ep_sts_en;
	__le32 drbl;
	__le32 ep_ien;
	__le32 ep_ists;
	__le32 usb_pwr;
	__le32 usb_conf2;
	__le32 usb_cap1;
	__le32 usb_cap2;
	__le32 usb_cap3;
	__le32 usb_cap4;
	__le32 usb_cap5;
	__le32 usb_cap6;
	__le32 usb_cpkt1;
	__le32 usb_cpkt2;
	__le32 usb_cpkt3;
	__le32 ep_dma_ext_addr;
	__le32 buf_addr;
	__le32 buf_data;
	__le32 buf_ctrl;
	__le32 dtrans;
	__le32 tdl_from_trb;
	__le32 tdl_beh;
	__le32 ep_tdl;
	__le32 tdl_beh2;
	__le32 dma_adv_td;
	__le32 reserved1[26];
	__le32 cfg_reg1;
	__le32 dbg_link1;
	__le32 dbg_link2;
	__le32 cfg_regs[74];
	__le32 reserved2[51];
	__le32 dma_axi_ctrl;
	__le32 dma_axi_id;
	__le32 dma_axi_cap;
	__le32 dma_axi_ctrl0;
	__le32 dma_axi_ctrl1;
};

/* USB_CONF - bitmasks */
/* Reset USB device configuration. */
#define USB_CONF_CFGRST		BIT(0)
/* Set Configuration. */
#define USB_CONF_CFGSET		BIT(1)
/* Disconnect USB device in SuperSpeed. */
#define USB_CONF_USB3DIS	BIT(3)
/* Disconnect USB device in HS/FS */
#define USB_CONF_USB2DIS	BIT(4)
/* Little Endian access - default */
#define USB_CONF_LENDIAN	BIT(5)
/*
 * Big Endian access. Driver assume that byte order for
 * SFRs access always is as Little Endian so this bit
 * is not used.
 */
#define USB_CONF_BENDIAN	BIT(6)
/* Device software reset. */
#define USB_CONF_SWRST		BIT(7)
/* Singular DMA transfer mode. Only for VER < DEV_VER_V3*/
#define USB_CONF_DSING		BIT(8)
/* Multiple DMA transfers mode. Only for VER < DEV_VER_V3 */
#define USB_CONF_DMULT		BIT(9)
/* DMA clock turn-off enable. */
#define USB_CONF_DMAOFFEN	BIT(10)
/* DMA clock turn-off disable. */
#define USB_CONF_DMAOFFDS	BIT(11)
/* Clear Force Full Speed. */
#define USB_CONF_CFORCE_FS	BIT(12)
/* Set Force Full Speed. */
#define USB_CONF_SFORCE_FS	BIT(13)
/* Device enable. */
#define USB_CONF_DEVEN		BIT(14)
/* Device disable. */
#define USB_CONF_DEVDS		BIT(15)
/* L1 LPM state entry enable (used in HS/FS mode). */
#define USB_CONF_L1EN		BIT(16)
/* L1 LPM state entry disable (used in HS/FS mode). */
#define USB_CONF_L1DS		BIT(17)
/* USB 2.0 clock gate disable. */
#define USB_CONF_CLK2OFFEN	BIT(18)
/* USB 2.0 clock gate enable. */
#define USB_CONF_CLK2OFFDS	BIT(19)
/* L0 LPM state entry request (used in HS/FS mode). */
#define USB_CONF_LGO_L0		BIT(20)
/* USB 3.0 clock gate disable. */
#define USB_CONF_CLK3OFFEN	BIT(21)
/* USB 3.0 clock gate enable. */
#define USB_CONF_CLK3OFFDS	BIT(22)
/* Bit 23 is reserved*/
/* U1 state entry enable (used in SS mode). */
#define USB_CONF_U1EN		BIT(24)
/* U1 state entry disable (used in SS mode). */
#define USB_CONF_U1DS		BIT(25)
/* U2 state entry enable (used in SS mode). */
#define USB_CONF_U2EN		BIT(26)
/* U2 state entry disable (used in SS mode). */
#define USB_CONF_U2DS		BIT(27)
/* U0 state entry request (used in SS mode). */
#define USB_CONF_LGO_U0		BIT(28)
/* U1 state entry request (used in SS mode). */
#define USB_CONF_LGO_U1		BIT(29)
/* U2 state entry request (used in SS mode). */
#define USB_CONF_LGO_U2		BIT(30)
/* SS.Inactive state entry request (used in SS mode) */
#define USB_CONF_LGO_SSINACT	BIT(31)

/* USB_STS - bitmasks */
/*
 * Configuration status.
 * 1 - device is in the configured state.
 * 0 - device is not configured.
 */
#define USB_STS_CFGSTS_MASK	BIT(0)
#define USB_STS_CFGSTS(p)	((p) & USB_STS_CFGSTS_MASK)
/*
 * On-chip memory overflow.
 * 0 - On-chip memory status OK.
 * 1 - On-chip memory overflow.
 */
#define USB_STS_OV_MASK		BIT(1)
#define USB_STS_OV(p)		((p) & USB_STS_OV_MASK)
/*
 * SuperSpeed connection status.
 * 0 - USB in SuperSpeed mode disconnected.
 * 1 - USB in SuperSpeed mode connected.
 */
#define USB_STS_USB3CONS_MASK	BIT(2)
#define USB_STS_USB3CONS(p)	((p) & USB_STS_USB3CONS_MASK)
/*
 * DMA transfer configuration status.
 * 0 - single request.
 * 1 - multiple TRB chain
 * Supported only for controller version <  DEV_VER_V3
 */
#define USB_STS_DTRANS_MASK	BIT(3)
#define USB_STS_DTRANS(p)	((p) & USB_STS_DTRANS_MASK)
/*
 * Device speed.
 * 0 - Undefined (value after reset).
 * 1 - Low speed
 * 2 - Full speed
 * 3 - High speed
 * 4 - Super speed
 */
#define USB_STS_USBSPEED_MASK	GENMASK(6, 4)
#define USB_STS_USBSPEED(p)	(((p) & USB_STS_USBSPEED_MASK) >> 4)
#define USB_STS_LS		(0x1 << 4)
#define USB_STS_FS		(0x2 << 4)
#define USB_STS_HS		(0x3 << 4)
#define USB_STS_SS		(0x4 << 4)
#define DEV_UNDEFSPEED(p)	(((p) & USB_STS_USBSPEED_MASK) == (0x0 << 4))
#define DEV_LOWSPEED(p)		(((p) & USB_STS_USBSPEED_MASK) == USB_STS_LS)
#define DEV_FULLSPEED(p)	(((p) & USB_STS_USBSPEED_MASK) == USB_STS_FS)
#define DEV_HIGHSPEED(p)	(((p) & USB_STS_USBSPEED_MASK) == USB_STS_HS)
#define DEV_SUPERSPEED(p)	(((p) & USB_STS_USBSPEED_MASK) == USB_STS_SS)
/*
 * Endianness for SFR access.
 * 0 - Little Endian order (default after hardware reset).
 * 1 - Big Endian order
 */
#define USB_STS_ENDIAN_MASK	BIT(7)
#define USB_STS_ENDIAN(p)	((p) & USB_STS_ENDIAN_MASK)
/*
 * HS/FS clock turn-off status.
 * 0 - hsfs clock is always on.
 * 1 - hsfs clock turn-off in L2 (HS/FS mode) is enabled
 *          (default after hardware reset).
 */
#define USB_STS_CLK2OFF_MASK	BIT(8)
#define USB_STS_CLK2OFF(p)	((p) & USB_STS_CLK2OFF_MASK)
/*
 * PCLK clock turn-off status.
 * 0 - pclk clock is always on.
 * 1 - pclk clock turn-off in U3 (SS mode) is enabled
 *          (default after hardware reset).
 */
#define USB_STS_CLK3OFF_MASK	BIT(9)
#define USB_STS_CLK3OFF(p)	((p) & USB_STS_CLK3OFF_MASK)
/*
 * Controller in reset state.
 * 0 - Internal reset is active.
 * 1 - Internal reset is not active and controller is fully operational.
 */
#define USB_STS_IN_RST_MASK	BIT(10)
#define USB_STS_IN_RST(p)	((p) & USB_STS_IN_RST_MASK)
/*
 * Status of the "TDL calculation basing on TRB" feature.
 * 0 - disabled
 * 1 - enabled
 * Supported only for DEV_VER_V2 controller version.
 */
#define USB_STS_TDL_TRB_ENABLED	BIT(11)
/*
 * Device enable Status.
 * 0 - USB device is disabled (VBUS input is disconnected from internal logic).
 * 1 - USB device is enabled (VBUS input is connected to the internal logic).
 */
#define USB_STS_DEVS_MASK	BIT(14)
#define USB_STS_DEVS(p)		((p) & USB_STS_DEVS_MASK)
/*
 * Address status.
 * 0 - USB device is default state.
 * 1 - USB device is at least in address state.
 */
#define USB_STS_ADDRESSED_MASK	BIT(15)
#define USB_STS_ADDRESSED(p)	((p) & USB_STS_ADDRESSED_MASK)
/*
 * L1 LPM state enable status (used in HS/FS mode).
 * 0 - Entering to L1 LPM state disabled.
 * 1 - Entering to L1 LPM state enabled.
 */
#define USB_STS_L1ENS_MASK	BIT(16)
#define USB_STS_L1ENS(p)	((p) & USB_STS_L1ENS_MASK)
/*
 * Internal VBUS connection status (used both in HS/FS  and SS mode).
 * 0 - internal VBUS is not detected.
 * 1 - internal VBUS is detected.
 */
#define USB_STS_VBUSS_MASK	BIT(17)
#define USB_STS_VBUSS(p)	((p) & USB_STS_VBUSS_MASK)
/*
 * HS/FS LPM  state (used in FS/HS mode).
 * 0 - L0 State
 * 1 - L1 State
 * 2 - L2 State
 * 3 - L3 State
 */
#define USB_STS_LPMST_MASK	GENMASK(19, 18)
#define DEV_L0_STATE(p)		(((p) & USB_STS_LPMST_MASK) == (0x0 << 18))
#define DEV_L1_STATE(p)		(((p) & USB_STS_LPMST_MASK) == (0x1 << 18))
#define DEV_L2_STATE(p)		(((p) & USB_STS_LPMST_MASK) == (0x2 << 18))
#define DEV_L3_STATE(p)		(((p) & USB_STS_LPMST_MASK) == (0x3 << 18))
/*
 * Disable HS status (used in FS/HS mode).
 * 0 - the disconnect bit for HS/FS mode is set .
 * 1 - the disconnect bit for HS/FS mode is not set.
 */
#define USB_STS_USB2CONS_MASK	BIT(20)
#define USB_STS_USB2CONS(p)	((p) & USB_STS_USB2CONS_MASK)
/*
 * HS/FS mode connection status (used in FS/HS mode).
 * 0 - High Speed operations in USB2.0 (FS/HS) mode not disabled.
 * 1 - High Speed operations in USB2.0 (FS/HS).
 */
#define USB_STS_DISABLE_HS_MASK	BIT(21)
#define USB_STS_DISABLE_HS(p)	((p) & USB_STS_DISABLE_HS_MASK)
/*
 * U1 state enable status (used in SS mode).
 * 0 - Entering to  U1 state disabled.
 * 1 - Entering to  U1 state enabled.
 */
#define USB_STS_U1ENS_MASK	BIT(24)
#define USB_STS_U1ENS(p)	((p) & USB_STS_U1ENS_MASK)
/*
 * U2 state enable status (used in SS mode).
 * 0 - Entering to  U2 state disabled.
 * 1 - Entering to  U2 state enabled.
 */
#define USB_STS_U2ENS_MASK	BIT(25)
#define USB_STS_U2ENS(p)	((p) & USB_STS_U2ENS_MASK)
/*
 * SuperSpeed Link LTSSM state. This field reflects USBSS-DEV current
 * SuperSpeed link state
 */
#define USB_STS_LST_MASK	GENMASK(29, 26)
#define DEV_LST_U0		(((p) & USB_STS_LST_MASK) == (0x0 << 26))
#define DEV_LST_U1		(((p) & USB_STS_LST_MASK) == (0x1 << 26))
#define DEV_LST_U2		(((p) & USB_STS_LST_MASK) == (0x2 << 26))
#define DEV_LST_U3		(((p) & USB_STS_LST_MASK) == (0x3 << 26))
#define DEV_LST_DISABLED	(((p) & USB_STS_LST_MASK) == (0x4 << 26))
#define DEV_LST_RXDETECT	(((p) & USB_STS_LST_MASK) == (0x5 << 26))
#define DEV_LST_INACTIVE	(((p) & USB_STS_LST_MASK) == (0x6 << 26))
#define DEV_LST_POLLING		(((p) & USB_STS_LST_MASK) == (0x7 << 26))
#define DEV_LST_RECOVERY	(((p) & USB_STS_LST_MASK) == (0x8 << 26))
#define DEV_LST_HOT_RESET	(((p) & USB_STS_LST_MASK) == (0x9 << 26))
#define DEV_LST_COMP_MODE	(((p) & USB_STS_LST_MASK) == (0xa << 26))
#define DEV_LST_LB_STATE	(((p) & USB_STS_LST_MASK) == (0xb << 26))
/*
 * DMA clock turn-off status.
 * 0 - DMA clock is always on (default after hardware reset).
 * 1 - DMA clock turn-off in U1, U2 and U3 (SS mode) is enabled.
 */
#define USB_STS_DMAOFF_MASK	BIT(30)
#define USB_STS_DMAOFF(p)	((p) & USB_STS_DMAOFF_MASK)
/*
 * SFR Endian status.
 * 0 - Little Endian order (default after hardware reset).
 * 1 - Big Endian order.
 */
#define USB_STS_ENDIAN2_MASK	BIT(31)
#define USB_STS_ENDIAN2(p)	((p) & USB_STS_ENDIAN2_MASK)

/* USB_CMD -  bitmasks */
/* Set Function Address */
#define USB_CMD_SET_ADDR	BIT(0)
/*
 * Function Address This field is saved to the device only when the field
 * SET_ADDR is set '1 ' during write to USB_CMD register.
 * Software is responsible for entering the address of the device during
 * SET_ADDRESS request service. This field should be set immediately after
 * the SETUP packet is decoded, and prior to confirmation of the status phase
 */
#define USB_CMD_FADDR_MASK	GENMASK(7, 1)
#define USB_CMD_FADDR(p)	(((p) << 1) & USB_CMD_FADDR_MASK)
/* Send Function Wake Device Notification TP (used only in SS mode). */
#define USB_CMD_SDNFW		BIT(8)
/* Set Test Mode (used only in HS/FS mode). */
#define USB_CMD_STMODE		BIT(9)
/* Test mode selector (used only in HS/FS mode) */
#define USB_STS_TMODE_SEL_MASK	GENMASK(11, 10)
#define USB_STS_TMODE_SEL(p)	(((p) << 10) & USB_STS_TMODE_SEL_MASK)
/*
 *  Send Latency Tolerance Message Device Notification TP (used only
 *  in SS mode).
 */
#define USB_CMD_SDNLTM		BIT(12)
/* Send Custom Transaction Packet (used only in SS mode) */
#define USB_CMD_SPKT		BIT(13)
/*Device Notification 'Function Wake' - Interface value (only in SS mode. */
#define USB_CMD_DNFW_INT_MASK	GENMASK(23, 16)
#define USB_STS_DNFW_INT(p)	(((p) << 16) & USB_CMD_DNFW_INT_MASK)
/*
 * Device Notification 'Latency Tolerance Message' -373 BELT value [7:0]
 * (used only in SS mode).
 */
#define USB_CMD_DNLTM_BELT_MASK	GENMASK(27, 16)
#define USB_STS_DNLTM_BELT(p)	(((p) << 16) & USB_CMD_DNLTM_BELT_MASK)

/* USB_ITPN - bitmasks */
/*
 * ITP(SS) / SOF (HS/FS) number
 * In SS mode this field represent number of last ITP received from host.
 * In HS/FS mode this field represent number of last SOF received from host.
 */
#define USB_ITPN_MASK		GENMASK(13, 0)
#define USB_ITPN(p)		((p) & USB_ITPN_MASK)

/* USB_LPM - bitmasks */
/* Host Initiated Resume Duration. */
#define USB_LPM_HIRD_MASK	GENMASK(3, 0)
#define USB_LPM_HIRD(p)		((p) & USB_LPM_HIRD_MASK)
/* Remote Wakeup Enable (bRemoteWake). */
#define USB_LPM_BRW		BIT(4)

/* USB_IEN - bitmasks */
/* SS connection interrupt enable */
#define USB_IEN_CONIEN		BIT(0)
/* SS disconnection interrupt enable. */
#define USB_IEN_DISIEN		BIT(1)
/* USB SS warm reset interrupt enable. */
#define USB_IEN_UWRESIEN	BIT(2)
/* USB SS hot reset interrupt enable */
#define USB_IEN_UHRESIEN	BIT(3)
/* SS link U3 state enter interrupt enable (suspend).*/
#define USB_IEN_U3ENTIEN	BIT(4)
/* SS link U3 state exit interrupt enable (wakeup). */
#define USB_IEN_U3EXTIEN	BIT(5)
/* SS link U2 state enter interrupt enable.*/
#define USB_IEN_U2ENTIEN	BIT(6)
/* SS link U2 state exit interrupt enable.*/
#define USB_IEN_U2EXTIEN	BIT(7)
/* SS link U1 state enter interrupt enable.*/
#define USB_IEN_U1ENTIEN	BIT(8)
/* SS link U1 state exit interrupt enable.*/
#define USB_IEN_U1EXTIEN	BIT(9)
/* ITP/SOF packet detected interrupt enable.*/
#define USB_IEN_ITPIEN		BIT(10)
/* Wakeup interrupt enable.*/
#define USB_IEN_WAKEIEN		BIT(11)
/* Send Custom Packet interrupt enable.*/
#define USB_IEN_SPKTIEN		BIT(12)
/* HS/FS mode connection interrupt enable.*/
#define USB_IEN_CON2IEN		BIT(16)
/* HS/FS mode disconnection interrupt enable.*/
#define USB_IEN_DIS2IEN		BIT(17)
/* USB reset (HS/FS mode) interrupt enable.*/
#define USB_IEN_U2RESIEN	BIT(18)
/* LPM L2 state enter interrupt enable.*/
#define USB_IEN_L2ENTIEN	BIT(20)
/* LPM  L2 state exit interrupt enable.*/
#define USB_IEN_L2EXTIEN	BIT(21)
/* LPM L1 state enter interrupt enable.*/
#define USB_IEN_L1ENTIEN	BIT(24)
/* LPM  L1 state exit interrupt enable.*/
#define USB_IEN_L1EXTIEN	BIT(25)
/* Configuration reset interrupt enable.*/
#define USB_IEN_CFGRESIEN	BIT(26)
/* Start of the USB SS warm reset interrupt enable.*/
#define USB_IEN_UWRESSIEN	BIT(28)
/* End of the USB SS warm reset interrupt enable.*/
#define USB_IEN_UWRESEIEN	BIT(29)

#define USB_IEN_INIT  (USB_IEN_U2RESIEN | USB_ISTS_DIS2I | USB_IEN_CON2IEN \
		       | USB_IEN_UHRESIEN | USB_IEN_UWRESIEN | USB_IEN_DISIEN \
		       | USB_IEN_CONIEN | USB_IEN_U3EXTIEN | USB_IEN_L2ENTIEN \
		       | USB_IEN_L2EXTIEN | USB_IEN_L1ENTIEN | USB_IEN_U3ENTIEN)

/* USB_ISTS - bitmasks */
/* SS Connection detected. */
#define USB_ISTS_CONI		BIT(0)
/* SS Disconnection detected. */
#define USB_ISTS_DISI		BIT(1)
/* UUSB warm reset detectede. */
#define USB_ISTS_UWRESI		BIT(2)
/* USB hot reset detected. */
#define USB_ISTS_UHRESI		BIT(3)
/* U3 link state enter detected (suspend).*/
#define USB_ISTS_U3ENTI		BIT(4)
/* U3 link state exit detected (wakeup). */
#define USB_ISTS_U3EXTI		BIT(5)
/* U2 link state enter detected.*/
#define USB_ISTS_U2ENTI		BIT(6)
/* U2 link state exit detected.*/
#define USB_ISTS_U2EXTI		BIT(7)
/* U1 link state enter detected.*/
#define USB_ISTS_U1ENTI		BIT(8)
/* U1 link state exit detected.*/
#define USB_ISTS_U1EXTI		BIT(9)
/* ITP/SOF packet detected.*/
#define USB_ISTS_ITPI		BIT(10)
/* Wakeup detected.*/
#define USB_ISTS_WAKEI		BIT(11)
/* Send Custom Packet detected.*/
#define USB_ISTS_SPKTI		BIT(12)
/* HS/FS mode connection detected.*/
#define USB_ISTS_CON2I		BIT(16)
/* HS/FS mode disconnection detected.*/
#define USB_ISTS_DIS2I		BIT(17)
/* USB reset (HS/FS mode) detected.*/
#define USB_ISTS_U2RESI		BIT(18)
/* LPM L2 state enter detected.*/
#define USB_ISTS_L2ENTI		BIT(20)
/* LPM  L2 state exit detected.*/
#define USB_ISTS_L2EXTI		BIT(21)
/* LPM L1 state enter detected.*/
#define USB_ISTS_L1ENTI		BIT(24)
/* LPM L1 state exit detected.*/
#define USB_ISTS_L1EXTI		BIT(25)
/* USB configuration reset detected.*/
#define USB_ISTS_CFGRESI	BIT(26)
/* Start of the USB warm reset detected.*/
#define USB_ISTS_UWRESSI	BIT(28)
/* End of the USB warm reset detected.*/
#define USB_ISTS_UWRESEI	BIT(29)

/* USB_SEL - bitmasks */
#define EP_SEL_EPNO_MASK	GENMASK(3, 0)
/* Endpoint number. */
#define EP_SEL_EPNO(p)		((p) & EP_SEL_EPNO_MASK)
/* Endpoint direction bit - 0 - OUT, 1 - IN. */
#define EP_SEL_DIR		BIT(7)

#define select_ep_in(nr)	(EP_SEL_EPNO(p) | EP_SEL_DIR)
#define select_ep_out		(EP_SEL_EPNO(p))

/* EP_TRADDR - bitmasks */
/* Transfer Ring address. */
#define EP_TRADDR_TRADDR(p)	((p))

/* EP_CFG - bitmasks */
/* Endpoint enable */
#define EP_CFG_ENABLE		BIT(0)
/*
 *  Endpoint type.
 * 1 - isochronous
 * 2 - bulk
 * 3 - interrupt
 */
#define EP_CFG_EPTYPE_MASK	GENMASK(2, 1)
#define EP_CFG_EPTYPE(p)	(((p) << 1)  & EP_CFG_EPTYPE_MASK)
/* Stream support enable (only in SS mode). */
#define EP_CFG_STREAM_EN	BIT(3)
/* TDL check (only in SS mode for BULK EP). */
#define EP_CFG_TDL_CHK		BIT(4)
/* SID check (only in SS mode for BULK OUT EP). */
#define EP_CFG_SID_CHK		BIT(5)
/* DMA transfer endianness. */
#define EP_CFG_EPENDIAN		BIT(7)
/* Max burst size (used only in SS mode). */
#define EP_CFG_MAXBURST_MASK	GENMASK(11, 8)
#define EP_CFG_MAXBURST(p)	(((p) << 8) & EP_CFG_MAXBURST_MASK)
/* ISO max burst. */
#define EP_CFG_MULT_MASK	GENMASK(15, 14)
#define EP_CFG_MULT(p)		(((p) << 14) & EP_CFG_MULT_MASK)
/* ISO max burst. */
#define EP_CFG_MAXPKTSIZE_MASK	GENMASK(26, 16)
#define EP_CFG_MAXPKTSIZE(p)	(((p) << 16) & EP_CFG_MAXPKTSIZE_MASK)
/* Max number of buffered packets. */
#define EP_CFG_BUFFERING_MASK	GENMASK(31, 27)
#define EP_CFG_BUFFERING(p)	(((p) << 27) & EP_CFG_BUFFERING_MASK)

/* EP_CMD - bitmasks */
/* Endpoint reset. */
#define EP_CMD_EPRST		BIT(0)
/* Endpoint STALL set. */
#define EP_CMD_SSTALL		BIT(1)
/* Endpoint STALL clear. */
#define EP_CMD_CSTALL		BIT(2)
/* Send ERDY TP. */
#define EP_CMD_ERDY		BIT(3)
/* Request complete. */
#define EP_CMD_REQ_CMPL		BIT(5)
/* Transfer descriptor ready. */
#define EP_CMD_DRDY		BIT(6)
/* Data flush. */
#define EP_CMD_DFLUSH		BIT(7)
/*
 * Transfer Descriptor Length write  (used only for Bulk Stream capable
 * endpoints in SS mode).
 * Bit Removed from DEV_VER_V3 controller version.
 */
#define EP_CMD_STDL		BIT(8)
/*
 * Transfer Descriptor Length (used only in SS mode for bulk endpoints).
 * Bits Removed from DEV_VER_V3 controller version.
 */
#define EP_CMD_TDL_MASK		GENMASK(15, 9)
#define EP_CMD_TDL_SET(p)	(((p) << 9) & EP_CMD_TDL_MASK)
#define EP_CMD_TDL_GET(p)	(((p) & EP_CMD_TDL_MASK) >> 9)
#define EP_CMD_TDL_MAX		(EP_CMD_TDL_MASK >> 9)

/* ERDY Stream ID value (used in SS mode). */
#define EP_CMD_ERDY_SID_MASK	GENMASK(31, 16)
#define EP_CMD_ERDY_SID(p)	(((p) << 16) & EP_CMD_ERDY_SID_MASK)

/* EP_STS - bitmasks */
/* Setup transfer complete. */
#define EP_STS_SETUP		BIT(0)
/* Endpoint STALL status. */
#define EP_STS_STALL(p)		((p) & BIT(1))
/* Interrupt On Complete. */
#define EP_STS_IOC		BIT(2)
/* Interrupt on Short Packet. */
#define EP_STS_ISP		BIT(3)
/* Transfer descriptor missing. */
#define EP_STS_DESCMIS		BIT(4)
/* Stream Rejected (used only in SS mode) */
#define EP_STS_STREAMR		BIT(5)
/* EXIT from MOVE DATA State (used only for stream transfers in SS mode). */
#define EP_STS_MD_EXIT		BIT(6)
/* TRB error. */
#define EP_STS_TRBERR		BIT(7)
/* Not ready (used only in SS mode). */
#define EP_STS_NRDY		BIT(8)
/* DMA busy bit. */
#define EP_STS_DBUSY		BIT(9)
/* Endpoint Buffer Empty */
#define EP_STS_BUFFEMPTY(p)	((p) & BIT(10))
/* Current Cycle Status */
#define EP_STS_CCS(p)		((p) & BIT(11))
/* Prime (used only in SS mode. */
#define EP_STS_PRIME		BIT(12)
/* Stream error (used only in SS mode). */
#define EP_STS_SIDERR		BIT(13)
/* OUT size mismatch. */
#define EP_STS_OUTSMM		BIT(14)
/* ISO transmission error. */
#define EP_STS_ISOERR		BIT(15)
/* Host Packet Pending (only for SS mode). */
#define EP_STS_HOSTPP(p)	((p) & BIT(16))
/* Stream Protocol State Machine State (only for Bulk stream endpoints). */
#define EP_STS_SPSMST_MASK		GENMASK(18, 17)
#define EP_STS_SPSMST_DISABLED(p)	(((p) & EP_STS_SPSMST_MASK) >> 17)
#define EP_STS_SPSMST_IDLE(p)		(((p) & EP_STS_SPSMST_MASK) >> 17)
#define EP_STS_SPSMST_START_STREAM(p)	(((p) & EP_STS_SPSMST_MASK) >> 17)
#define EP_STS_SPSMST_MOVE_DATA(p)	(((p) & EP_STS_SPSMST_MASK) >> 17)
/* Interrupt On Transfer complete. */
#define EP_STS_IOT		BIT(19)
/* OUT queue endpoint number. */
#define EP_STS_OUTQ_NO_MASK	GENMASK(27, 24)
#define EP_STS_OUTQ_NO(p)	(((p) & EP_STS_OUTQ_NO_MASK) >> 24)
/* OUT queue valid flag. */
#define EP_STS_OUTQ_VAL_MASK	BIT(28)
#define EP_STS_OUTQ_VAL(p)	((p) & EP_STS_OUTQ_VAL_MASK)
/* SETUP WAIT. */
#define EP_STS_STPWAIT		BIT(31)

/* EP_STS_SID - bitmasks */
/* Stream ID (used only in SS mode). */
#define EP_STS_SID_MASK		GENMASK(15, 0)
#define EP_STS_SID(p)		((p) & EP_STS_SID_MASK)

/* EP_STS_EN - bitmasks */
/* SETUP interrupt enable. */
#define EP_STS_EN_SETUPEN	BIT(0)
/* OUT transfer missing descriptor enable. */
#define EP_STS_EN_DESCMISEN	BIT(4)
/* Stream Rejected enable. */
#define EP_STS_EN_STREAMREN	BIT(5)
/* Move Data Exit enable.*/
#define EP_STS_EN_MD_EXITEN	BIT(6)
/* TRB enable. */
#define EP_STS_EN_TRBERREN	BIT(7)
/* NRDY enable. */
#define EP_STS_EN_NRDYEN	BIT(8)
/* Prime enable. */
#define EP_STS_EN_PRIMEEEN	BIT(12)
/* Stream error enable. */
#define EP_STS_EN_SIDERREN	BIT(13)
/* OUT size mismatch enable. */
#define EP_STS_EN_OUTSMMEN	BIT(14)
/* ISO transmission error enable. */
#define EP_STS_EN_ISOERREN	BIT(15)
/* Interrupt on Transmission complete enable. */
#define EP_STS_EN_IOTEN		BIT(19)
/* Setup Wait interrupt enable. */
#define EP_STS_EN_STPWAITEN	BIT(31)

/* DRBL- bitmasks */
#define DB_VALUE_BY_INDEX(index) (1 << (index))
#define DB_VALUE_EP0_OUT	BIT(0)
#define DB_VALUE_EP0_IN		BIT(16)

/* EP_IEN - bitmasks */
#define EP_IEN(index)		(1 << (index))
#define EP_IEN_EP_OUT0		BIT(0)
#define EP_IEN_EP_IN0		BIT(16)

/* EP_ISTS - bitmasks */
#define EP_ISTS(index)		(1 << (index))
#define EP_ISTS_EP_OUT0		BIT(0)
#define EP_ISTS_EP_IN0		BIT(16)

/* USB_PWR- bitmasks */
/*Power Shut Off capability enable*/
#define PUSB_PWR_PSO_EN		BIT(0)
/*Power Shut Off capability disable*/
#define PUSB_PWR_PSO_DS		BIT(1)
/*
 * Enables turning-off Reference Clock.
 * This bit is optional and implemented only when support for OTG is
 * implemented (indicated by OTG_READY bit set to '1').
 */
#define PUSB_PWR_STB_CLK_SWITCH_EN	BIT(8)
/*
 * Status bit indicating that operation required by STB_CLK_SWITCH_EN write
 * is completed
 */
#define PUSB_PWR_STB_CLK_SWITCH_DONE	BIT(9)
/* This bit informs if Fast Registers Access is enabled. */
#define PUSB_PWR_FST_REG_ACCESS_STAT	BIT(30)
/* Fast Registers Access Enable. */
#define PUSB_PWR_FST_REG_ACCESS		BIT(31)

/* USB_CONF2- bitmasks */
/*
 * Writing 1 disables TDL calculation basing on TRB feature in controller
 * for DMULT mode.
 * Bit supported only for DEV_VER_V2 version.
 */
#define USB_CONF2_DIS_TDL_TRB		BIT(1)
/*
 * Writing 1 enables TDL calculation basing on TRB feature in controller
 * for DMULT mode.
 * Bit supported only for DEV_VER_V2 version.
 */
#define USB_CONF2_EN_TDL_TRB		BIT(2)

/* USB_CAP1- bitmasks */
/*
 * SFR Interface type
 * These field reflects type of SFR interface implemented:
 * 0x0 - OCP
 * 0x1 - AHB,
 * 0x2 - PLB
 * 0x3 - AXI
 * 0x4-0xF - reserved
 */
#define USB_CAP1_SFR_TYPE_MASK	GENMASK(3, 0)
#define DEV_SFR_TYPE_OCP(p)	(((p) & USB_CAP1_SFR_TYPE_MASK) == 0x0)
#define DEV_SFR_TYPE_AHB(p)	(((p) & USB_CAP1_SFR_TYPE_MASK) == 0x1)
#define DEV_SFR_TYPE_PLB(p)	(((p) & USB_CAP1_SFR_TYPE_MASK) == 0x2)
#define DEV_SFR_TYPE_AXI(p)	(((p) & USB_CAP1_SFR_TYPE_MASK) == 0x3)
/*
 * SFR Interface width
 * These field reflects width of SFR interface implemented:
 * 0x0 - 8 bit interface,
 * 0x1 - 16 bit interface,
 * 0x2 - 32 bit interface
 * 0x3 - 64 bit interface
 * 0x4-0xF - reserved
 */
#define USB_CAP1_SFR_WIDTH_MASK	GENMASK(7, 4)
#define DEV_SFR_WIDTH_8(p)	(((p) & USB_CAP1_SFR_WIDTH_MASK) == (0x0 << 4))
#define DEV_SFR_WIDTH_16(p)	(((p) & USB_CAP1_SFR_WIDTH_MASK) == (0x1 << 4))
#define DEV_SFR_WIDTH_32(p)	(((p) & USB_CAP1_SFR_WIDTH_MASK) == (0x2 << 4))
#define DEV_SFR_WIDTH_64(p)	(((p) & USB_CAP1_SFR_WIDTH_MASK) == (0x3 << 4))
/*
 * DMA Interface type
 * These field reflects type of DMA interface implemented:
 * 0x0 - OCP
 * 0x1 - AHB,
 * 0x2 - PLB
 * 0x3 - AXI
 * 0x4-0xF - reserved
 */
#define USB_CAP1_DMA_TYPE_MASK	GENMASK(11, 8)
#define DEV_DMA_TYPE_OCP(p)	(((p) & USB_CAP1_DMA_TYPE_MASK) == (0x0 << 8))
#define DEV_DMA_TYPE_AHB(p)	(((p) & USB_CAP1_DMA_TYPE_MASK) == (0x1 << 8))
#define DEV_DMA_TYPE_PLB(p)	(((p) & USB_CAP1_DMA_TYPE_MASK) == (0x2 << 8))
#define DEV_DMA_TYPE_AXI(p)	(((p) & USB_CAP1_DMA_TYPE_MASK) == (0x3 << 8))
/*
 * DMA Interface width
 * These field reflects width of DMA interface implemented:
 * 0x0 - reserved,
 * 0x1 - reserved,
 * 0x2 - 32 bit interface
 * 0x3 - 64 bit interface
 * 0x4-0xF - reserved
 */
#define USB_CAP1_DMA_WIDTH_MASK	GENMASK(15, 12)
#define DEV_DMA_WIDTH_32(p)	(((p) & USB_CAP1_DMA_WIDTH_MASK) == (0x2 << 12))
#define DEV_DMA_WIDTH_64(p)	(((p) & USB_CAP1_DMA_WIDTH_MASK) == (0x3 << 12))
/*
 * USB3 PHY Interface type
 * These field reflects type of USB3 PHY interface implemented:
 * 0x0 - USB PIPE,
 * 0x1 - RMMI,
 * 0x2-0xF - reserved
 */
#define USB_CAP1_U3PHY_TYPE_MASK GENMASK(19, 16)
#define DEV_U3PHY_PIPE(p) (((p) & USB_CAP1_U3PHY_TYPE_MASK) == (0x0 << 16))
#define DEV_U3PHY_RMMI(p) (((p) & USB_CAP1_U3PHY_TYPE_MASK) == (0x1 << 16))
/*
 * USB3 PHY Interface width
 * These field reflects width of USB3 PHY interface implemented:
 * 0x0 - 8 bit PIPE interface,
 * 0x1 - 16 bit PIPE interface,
 * 0x2 - 32 bit PIPE interface,
 * 0x3 - 64 bit PIPE interface
 * 0x4-0xF - reserved
 * Note: When SSIC interface is implemented this field shows the width of
 * internal PIPE interface. The RMMI interface is always 20bit wide.
 */
#define USB_CAP1_U3PHY_WIDTH_MASK GENMASK(23, 20)
#define DEV_U3PHY_WIDTH_8(p) \
	(((p) & USB_CAP1_U3PHY_WIDTH_MASK) == (0x0 << 20))
#define DEV_U3PHY_WIDTH_16(p) \
	(((p) & USB_CAP1_U3PHY_WIDTH_MASK) == (0x1 << 16))
#define DEV_U3PHY_WIDTH_32(p) \
	(((p) & USB_CAP1_U3PHY_WIDTH_MASK) == (0x2 << 20))
#define DEV_U3PHY_WIDTH_64(p) \
	(((p) & USB_CAP1_U3PHY_WIDTH_MASK) == (0x3 << 16))

/*
 * USB2 PHY Interface enable
 * These field informs if USB2 PHY interface is implemented:
 * 0x0 - interface NOT implemented,
 * 0x1 - interface implemented
 */
#define USB_CAP1_U2PHY_EN(p)	((p) & BIT(24))
/*
 * USB2 PHY Interface type
 * These field reflects type of USB2 PHY interface implemented:
 * 0x0 - UTMI,
 * 0x1 - ULPI
 */
#define DEV_U2PHY_ULPI(p)	((p) & BIT(25))
/*
 * USB2 PHY Interface width
 * These field reflects width of USB2 PHY interface implemented:
 * 0x0 - 8 bit interface,
 * 0x1 - 16 bit interface,
 * Note: The ULPI interface is always 8bit wide.
 */
#define DEV_U2PHY_WIDTH_16(p)	((p) & BIT(26))
/*
 * OTG Ready
 * 0x0 - pure device mode
 * 0x1 - some features and ports for CDNS USB OTG controller are implemented.
 */
#define USB_CAP1_OTG_READY(p)	((p) & BIT(27))

/*
 * When set, indicates that controller supports automatic internal TDL
 * calculation basing on the size provided in TRB (TRB[22:17]) for DMULT mode
 * Supported only for DEV_VER_V2 controller version.
 */
#define USB_CAP1_TDL_FROM_TRB(p)	((p) & BIT(28))

/* USB_CAP2- bitmasks */
/*
 * The actual size of the connected On-chip RAM memory in kB:
 * - 0 means 256 kB (max supported mem size)
 * - value other than 0 reflects the mem size in kB
 */
#define USB_CAP2_ACTUAL_MEM_SIZE(p) ((p) & GENMASK(7, 0))
/*
 * Max supported mem size
 * These field reflects width of on-chip RAM address bus width,
 * which determines max supported mem size:
 * 0x0-0x7 - reserved,
 * 0x8 - support for 4kB mem,
 * 0x9 - support for 8kB mem,
 * 0xA - support for 16kB mem,
 * 0xB - support for 32kB mem,
 * 0xC - support for 64kB mem,
 * 0xD - support for 128kB mem,
 * 0xE - support for 256kB mem,
 * 0xF - reserved
 */
#define USB_CAP2_MAX_MEM_SIZE(p) ((p) & GENMASK(11, 8))

/* USB_CAP3- bitmasks */
#define EP_IS_IMPLEMENTED(reg, index) ((reg) & (1 << (index)))

/* USB_CAP4- bitmasks */
#define EP_SUPPORT_ISO(reg, index) ((reg) & (1 << (index)))

/* USB_CAP5- bitmasks */
#define EP_SUPPORT_STREAM(reg, index) ((reg) & (1 << (index)))

/* USB_CAP6- bitmasks */
/* The USBSS-DEV Controller  Internal build number. */
#define GET_DEV_BASE_VERSION(p) ((p) & GENMASK(23, 0))
/* The USBSS-DEV Controller version number. */
#define GET_DEV_CUSTOM_VERSION(p) ((p) & GENMASK(31, 24))

#define DEV_VER_NXP_V1		0x00024502
#define DEV_VER_TI_V1		0x00024509
#define DEV_VER_V2		0x0002450C
#define DEV_VER_V3		0x0002450d

/* DBG_LINK1- bitmasks */
/*
 * LFPS_MIN_DET_U1_EXIT value This parameter configures the minimum
 * time required for decoding the received LFPS as an LFPS.U1_Exit.
 */
#define DBG_LINK1_LFPS_MIN_DET_U1_EXIT(p)	((p) & GENMASK(7, 0))
/*
 * LFPS_MIN_GEN_U1_EXIT value This parameter configures the minimum time for
 * phytxelecidle deassertion when LFPS.U1_Exit
 */
#define DBG_LINK1_LFPS_MIN_GEN_U1_EXIT_MASK	GENMASK(15, 8)
#define DBG_LINK1_LFPS_MIN_GEN_U1_EXIT(p)	(((p) << 8) & GENMASK(15, 8))
/*
 * RXDET_BREAK_DIS value This parameter configures terminating the Far-end
 * Receiver termination detection sequence:
 * 0: it is possible that USBSS_DEV will terminate Farend receiver
 *    termination detection sequence
 * 1: USBSS_DEV will not terminate Far-end receiver termination
 *    detection sequence
 */
#define DBG_LINK1_RXDET_BREAK_DIS		BIT(16)
/* LFPS_GEN_PING value This parameter configures the LFPS.Ping generation */
#define DBG_LINK1_LFPS_GEN_PING(p)		(((p) << 17) & GENMASK(21, 17))
/*
 * Set the LFPS_MIN_DET_U1_EXIT value Writing '1' to this bit writes the
 * LFPS_MIN_DET_U1_EXIT field value to the device. This bit is automatically
 * cleared. Writing '0' has no effect
 */
#define DBG_LINK1_LFPS_MIN_DET_U1_EXIT_SET	BIT(24)
/*
 * Set the LFPS_MIN_GEN_U1_EXIT value. Writing '1' to this bit writes the
 * LFPS_MIN_GEN_U1_EXIT field value to the device. This bit is automatically
 * cleared. Writing '0' has no effect
 */
#define DBG_LINK1_LFPS_MIN_GEN_U1_EXIT_SET	BIT(25)
/*
 * Set the RXDET_BREAK_DIS value Writing '1' to this bit writes
 * the RXDET_BREAK_DIS field value to the device. This bit is automatically
 * cleared. Writing '0' has no effect
 */
#define DBG_LINK1_RXDET_BREAK_DIS_SET		BIT(26)
/*
 * Set the LFPS_GEN_PING_SET value Writing '1' to this bit writes
 * the LFPS_GEN_PING field value to the device. This bit is automatically
 * cleared. Writing '0' has no effect."
 */
#define DBG_LINK1_LFPS_GEN_PING_SET		BIT(27)

/* DMA_AXI_CTRL- bitmasks */
/* The mawprot pin configuration. */
#define DMA_AXI_CTRL_MARPROT(p) ((p) & GENMASK(2, 0))
/* The marprot pin configuration. */
#define DMA_AXI_CTRL_MAWPROT(p) (((p) & GENMASK(2, 0)) << 16)
#define DMA_AXI_CTRL_NON_SECURE 0x02

#define gadget_to_cdns3_device(g) (container_of(g, struct cdns3_device, gadget))

#define ep_to_cdns3_ep(ep) (container_of(ep, struct cdns3_endpoint, endpoint))

/*-------------------------------------------------------------------------*/
/*
 * USBSS-DEV DMA interface.
 */
#define TRBS_PER_SEGMENT	40

#define ISO_MAX_INTERVAL	10

#define MAX_TRB_LENGTH          BIT(16)

#if TRBS_PER_SEGMENT < 2
#error "Incorrect TRBS_PER_SEGMENT. Minimal Transfer Ring size is 2."
#endif

#define TRBS_PER_STREAM_SEGMENT 2

#if TRBS_PER_STREAM_SEGMENT < 2
#error "Incorrect TRBS_PER_STREAMS_SEGMENT. Minimal Transfer Ring size is 2."
#endif

/*
 *Only for ISOC endpoints - maximum number of TRBs is calculated as
 * pow(2, bInterval-1) * number of usb requests. It is limitation made by
 * driver to save memory. Controller must prepare TRB for each ITP even
 * if bInterval > 1. It's the reason why driver needs so many TRBs for
 * isochronous endpoints.
 */
#define TRBS_PER_ISOC_SEGMENT	(ISO_MAX_INTERVAL * 8)

#define GET_TRBS_PER_SEGMENT(ep_type) ((ep_type) == USB_ENDPOINT_XFER_ISOC ? \
				      TRBS_PER_ISOC_SEGMENT : TRBS_PER_SEGMENT)
/**
 * struct cdns3_trb - represent Transfer Descriptor block.
 * @buffer:	pointer to buffer data
 * @length:	length of data
 * @control:	control flags.
 *
 * This structure describes transfer block serviced by DMA module.
 */
struct cdns3_trb {
	__le32 buffer;
	__le32 length;
	__le32 control;
};

#define TRB_SIZE		(sizeof(struct cdns3_trb))
#define TRB_RING_SIZE		(TRB_SIZE * TRBS_PER_SEGMENT)
#define TRB_STREAM_RING_SIZE	(TRB_SIZE * TRBS_PER_STREAM_SEGMENT)
#define TRB_ISO_RING_SIZE	(TRB_SIZE * TRBS_PER_ISOC_SEGMENT)
#define TRB_CTRL_RING_SIZE	(TRB_SIZE * 2)

/* TRB bit mask */
#define TRB_TYPE_BITMASK	GENMASK(15, 10)
#define TRB_TYPE(p)		((p) << 10)
#define TRB_FIELD_TO_TYPE(p)	(((p) & TRB_TYPE_BITMASK) >> 10)

/* TRB type IDs */
/* bulk, interrupt, isoc , and control data stage */
#define TRB_NORMAL		1
/* TRB for linking ring segments */
#define TRB_LINK		6

/* Cycle bit - indicates TRB ownership by driver or hw*/
#define TRB_CYCLE		BIT(0)
/*
 * When set to '1', the device will toggle its interpretation of the Cycle bit
 */
#define TRB_TOGGLE		BIT(1)

/*
 * Short Packet (SP). OUT EPs at DMULT=1 only. Indicates if the TRB was
 * processed while USB short packet was received. No more buffers defined by
 * the TD will be used. DMA will automatically advance to next TD.
 * - Shall be set to 0 by Software when putting TRB on the Transfer Ring
 * - Shall be set to 1 by Controller when Short Packet condition for this TRB
 *   is detected independent if ISP is set or not.
 */
#define TRB_SP			BIT(1)

/* Interrupt on short packet*/
#define TRB_ISP			BIT(2)
/*Setting this bit enables FIFO DMA operation mode*/
#define TRB_FIFO_MODE		BIT(3)
/* Set PCIe no snoop attribute */
#define TRB_CHAIN		BIT(4)
/* Interrupt on completion */
#define TRB_IOC			BIT(5)

/* stream ID bitmasks. */
#define TRB_STREAM_ID_BITMASK		GENMASK(31, 16)
#define TRB_STREAM_ID(p)		((p) << 16)
#define TRB_FIELD_TO_STREAMID(p)	(((p) & TRB_STREAM_ID_BITMASK) >> 16)

/* Size of TD expressed in USB packets for HS/FS mode. */
#define TRB_TDL_HS_SIZE(p)	(((p) << 16) & GENMASK(31, 16))
#define TRB_TDL_HS_SIZE_GET(p)	(((p) & GENMASK(31, 16)) >> 16)

/* transfer_len bitmasks. */
#define TRB_LEN(p)		((p) & GENMASK(16, 0))

/* Size of TD expressed in USB packets for SS mode. */
#define TRB_TDL_SS_SIZE(p)	(((p) << 17) & GENMASK(23, 17))
#define TRB_TDL_SS_SIZE_GET(p)	(((p) & GENMASK(23, 17)) >> 17)

/* transfer_len bitmasks - bits 31:24 */
#define TRB_BURST_LEN(p)	(((p) << 24) & GENMASK(31, 24))
#define TRB_BURST_LEN_GET(p)	(((p) & GENMASK(31, 24)) >> 24)

/* Data buffer pointer bitmasks*/
#define TRB_BUFFER(p)		((p) & GENMASK(31, 0))

/*-------------------------------------------------------------------------*/
/* Driver numeric constants */

/* Such declaration should be added to ch9.h */
#define USB_DEVICE_MAX_ADDRESS		127

/* Endpoint init values */
#define CDNS3_EP_MAX_PACKET_LIMIT	1024
#define CDNS3_EP_MAX_STREAMS		15
#define CDNS3_EP0_MAX_PACKET_LIMIT	512

/* All endpoints including EP0 */
#define CDNS3_ENDPOINTS_MAX_COUNT	32
#define CDNS3_EP_ZLP_BUF_SIZE		1024

#define CDNS3_EP_BUF_SIZE		4	/* KB */
#define CDNS3_EP_ISO_HS_MULT		3
#define CDNS3_EP_ISO_SS_BURST		3
#define CDNS3_MAX_NUM_DESCMISS_BUF	32
#define CDNS3_DESCMIS_BUF_SIZE		2048	/* Bytes */
#define CDNS3_WA2_NUM_BUFFERS		128
/*-------------------------------------------------------------------------*/
/* Used structs */

struct cdns3_device;

/**
 * struct cdns3_endpoint - extended device side representation of USB endpoint.
 * @endpoint: usb endpoint
 * @pending_req_list: list of requests queuing on transfer ring.
 * @deferred_req_list: list of requests waiting for queuing on transfer ring.
 * @wa2_descmiss_req_list: list of requests internally allocated by driver.
 * @trb_pool: transfer ring - array of transaction buffers
 * @trb_pool_dma: dma address of transfer ring
 * @cdns3_dev: device associated with this endpoint
 * @name: a human readable name e.g. ep1out
 * @flags: specify the current state of endpoint
 * @descmis_req: internal transfer object used for getting data from on-chip
 *     buffer. It can happen only if function driver doesn't send usb_request
 *     object on time.
 * @dir: endpoint direction
 * @num: endpoint number (1 - 15)
 * @type: set to bmAttributes & USB_ENDPOINT_XFERTYPE_MASK
 * @interval: interval between packets used for ISOC endpoint.
 * @free_trbs: number of free TRBs in transfer ring
 * @num_trbs: number of all TRBs in transfer ring
 * @alloc_ring_size: size of the allocated TRB ring
 * @pcs: producer cycle state
 * @ccs: consumer cycle state
 * @enqueue: enqueue index in transfer ring
 * @dequeue: dequeue index in transfer ring
 * @trb_burst_size: number of burst used in trb.
 */
struct cdns3_endpoint {
	struct usb_ep		endpoint;
	struct list_head	pending_req_list;
	struct list_head	deferred_req_list;
	struct list_head	wa2_descmiss_req_list;
	int			wa2_counter;

	struct cdns3_trb	*trb_pool;
	dma_addr_t		trb_pool_dma;

	struct cdns3_device	*cdns3_dev;
	char			name[20];

#define EP_ENABLED		BIT(0)
#define EP_STALLED		BIT(1)
#define EP_STALL_PENDING	BIT(2)
#define EP_WEDGE		BIT(3)
#define EP_TRANSFER_STARTED	BIT(4)
#define EP_UPDATE_EP_TRBADDR	BIT(5)
#define EP_PENDING_REQUEST	BIT(6)
#define EP_RING_FULL		BIT(7)
#define EP_CLAIMED		BIT(8)
#define EP_DEFERRED_DRDY	BIT(9)
#define EP_QUIRK_ISO_OUT_EN	BIT(10)
#define EP_QUIRK_END_TRANSFER	BIT(11)
#define EP_QUIRK_EXTRA_BUF_DET	BIT(12)
#define EP_QUIRK_EXTRA_BUF_EN	BIT(13)
#define EP_TDLCHK_EN		BIT(15)
	u32			flags;

	struct cdns3_request	*descmis_req;

	u8			dir;
	u8			num;
	u8			type;
	int			interval;

	int			free_trbs;
	int			num_trbs;
	int			alloc_ring_size;
	u8			pcs;
	u8			ccs;
	int			enqueue;
	int			dequeue;
	u8			trb_burst_size;

	unsigned int		wa1_set:1;
	struct cdns3_trb	*wa1_trb;
	unsigned int		wa1_trb_index;
	unsigned int		wa1_cycle_bit:1;

	/* Stream related */
	unsigned int		use_streams:1;
	unsigned int		prime_flag:1;
	u32			ep_sts_pending;
	u16			last_stream_id;
	u16			pending_tdl;
	unsigned int		stream_sg_idx;
};

/**
 * struct cdns3_aligned_buf - represent aligned buffer used for DMA transfer
 * @buf: aligned to 8 bytes data buffer. Buffer address used in
 *       TRB shall be aligned to 8.
 * @dma: dma address
 * @size: size of buffer
 * @in_use: inform if this buffer is associated with usb_request
 * @list: used to adding instance of this object to list
 */
struct cdns3_aligned_buf {
	void			*buf;
	dma_addr_t		dma;
	u32			size;
	unsigned		in_use:1;
	struct list_head	list;
};

/**
 * struct cdns3_request - extended device side representation of usb_request
 *                        object .
 * @request: generic usb_request object describing single I/O request.
 * @priv_ep: extended representation of usb_ep object
 * @trb: the first TRB association with this request
 * @start_trb: number of the first TRB in transfer ring
 * @end_trb: number of the last TRB in transfer ring
 * @aligned_buf: object holds information about aligned buffer associated whit
 *               this endpoint
 * @flags: flag specifying special usage of request
 * @list: used by internally allocated request to add to wa2_descmiss_req_list.
 */
struct cdns3_request {
	struct usb_request		request;
	struct cdns3_endpoint		*priv_ep;
	struct cdns3_trb		*trb;
	int				start_trb;
	int				end_trb;
	struct cdns3_aligned_buf	*aligned_buf;
#define REQUEST_PENDING			BIT(0)
#define REQUEST_INTERNAL		BIT(1)
#define REQUEST_INTERNAL_CH		BIT(2)
#define REQUEST_ZLP			BIT(3)
#define REQUEST_UNALIGNED		BIT(4)
	u32				flags;
	struct list_head		list;
};

#define to_cdns3_request(r) (container_of(r, struct cdns3_request, request))

/*Stages used during enumeration process.*/
#define CDNS3_SETUP_STAGE		0x0
#define CDNS3_DATA_STAGE		0x1
#define CDNS3_STATUS_STAGE		0x2

/**
 * struct cdns3_device - represent USB device.
 * @dev: pointer to device structure associated whit this controller
 * @sysdev: pointer to the DMA capable device
 * @gadget: device side representation of the peripheral controller
 * @gadget_driver: pointer to the gadget driver
 * @dev_ver: device controller version.
 * @lock: for synchronizing
 * @regs: base address for device side registers
 * @setup_buf: used while processing usb control requests
 * @setup_dma: dma address for setup_buf
 * @zlp_buf - zlp buffer
 * @ep0_stage: ep0 stage during enumeration process.
 * @ep0_data_dir: direction for control transfer
 * @eps: array of pointers to all endpoints with exclusion ep0
 * @aligned_buf_list: list of aligned buffers internally allocated by driver
 * @aligned_buf_wq: workqueue freeing  no longer used aligned buf.
 * @selected_ep: actually selected endpoint. It's used only to improve
 *               performance.
 * @isoch_delay: value from Set Isoch Delay request. Only valid on SS/SSP.
 * @u1_allowed: allow device transition to u1 state
 * @u2_allowed: allow device transition to u2 state
 * @is_selfpowered: device is self powered
 * @setup_pending: setup packet is processing by gadget driver
 * @hw_configured_flag: hardware endpoint configuration was set.
 * @wake_up_flag: allow device to remote up the host
 * @status_completion_no_call: indicate that driver is waiting for status s
 *     stage completion. It's used in deferred SET_CONFIGURATION request.
 * @onchip_buffers: number of available on-chip buffers.
 * @onchip_used_size: actual size of on-chip memory assigned to endpoints.
 * @pending_status_wq: workqueue handling status stage for deferred requests.
 * @pending_status_request: request for which status stage was deferred
 */
struct cdns3_device {
	struct device			*dev;
	struct device			*sysdev;

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*gadget_driver;

#define CDNS_REVISION_V0		0x00024501
#define CDNS_REVISION_V1		0x00024509
	u32				dev_ver;

	/* generic spin-lock for drivers */
	spinlock_t			lock;

	struct cdns3_usb_regs		__iomem *regs;

	struct usb_ctrlrequest		*setup_buf;
	dma_addr_t			setup_dma;
	void				*zlp_buf;

	u8				ep0_stage;
	int				ep0_data_dir;

	struct cdns3_endpoint		*eps[CDNS3_ENDPOINTS_MAX_COUNT];

	struct list_head		aligned_buf_list;
	struct work_struct		aligned_buf_wq;

	u32				selected_ep;
	u16				isoch_delay;

	unsigned			wait_for_setup:1;
	unsigned			u1_allowed:1;
	unsigned			u2_allowed:1;
	unsigned			is_selfpowered:1;
	unsigned			setup_pending:1;
	unsigned			hw_configured_flag:1;
	unsigned			wake_up_flag:1;
	unsigned			status_completion_no_call:1;
	unsigned			using_streams:1;
	int				out_mem_is_allocated;

	struct work_struct		pending_status_wq;
	struct usb_request		*pending_status_request;

	/*in KB */
	u16				onchip_buffers;
	u16				onchip_used_size;
};

void cdns3_set_register_bit(void __iomem *ptr, u32 mask);
dma_addr_t cdns3_trb_virt_to_dma(struct cdns3_endpoint *priv_ep,
				 struct cdns3_trb *trb);
enum usb_device_speed cdns3_get_speed(struct cdns3_device *priv_dev);
void cdns3_pending_setup_status_handler(struct work_struct *work);
void cdns3_hw_reset_eps_config(struct cdns3_device *priv_dev);
void cdns3_set_hw_configuration(struct cdns3_device *priv_dev);
void cdns3_select_ep(struct cdns3_device *priv_dev, u32 ep);
void cdns3_allow_enable_l1(struct cdns3_device *priv_dev, int enable);
struct usb_request *cdns3_next_request(struct list_head *list);
void cdns3_rearm_transfer(struct cdns3_endpoint *priv_ep, u8 rearm);
int cdns3_allocate_trb_pool(struct cdns3_endpoint *priv_ep);
u8 cdns3_ep_addr_to_index(u8 ep_addr);
int cdns3_gadget_ep_set_wedge(struct usb_ep *ep);
int cdns3_gadget_ep_set_halt(struct usb_ep *ep, int value);
void __cdns3_gadget_ep_set_halt(struct cdns3_endpoint *priv_ep);
int __cdns3_gadget_ep_clear_halt(struct cdns3_endpoint *priv_ep);
struct usb_request *cdns3_gadget_ep_alloc_request(struct usb_ep *ep,
						  gfp_t gfp_flags);
void cdns3_gadget_ep_free_request(struct usb_ep *ep,
				  struct usb_request *request);
int cdns3_gadget_ep_dequeue(struct usb_ep *ep, struct usb_request *request);
void cdns3_gadget_giveback(struct cdns3_endpoint *priv_ep,
			   struct cdns3_request *priv_req,
			   int status);

int cdns3_init_ep0(struct cdns3_device *priv_dev,
		   struct cdns3_endpoint *priv_ep);
void cdns3_ep0_config(struct cdns3_device *priv_dev);
void cdns3_ep_config(struct cdns3_endpoint *priv_ep);
void cdns3_check_ep0_interrupt_proceed(struct cdns3_device *priv_dev, int dir);
int __cdns3_gadget_wakeup(struct cdns3_device *priv_dev);

#endif /* __LINUX_CDNS3_GADGET */
