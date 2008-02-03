/* bnx2x_hsi.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */


#define FUNC_0				0
#define FUNC_1				1
#define FUNC_MAX			2


/* This value (in milliseconds) determines the frequency of the driver
 * issuing the PULSE message code.  The firmware monitors this periodic
 * pulse to determine when to switch to an OS-absent mode. */
#define DRV_PULSE_PERIOD_MS		250

/* This value (in milliseconds) determines how long the driver should
 * wait for an acknowledgement from the firmware before timing out.  Once
 * the firmware has timed out, the driver will assume there is no firmware
 * running and there won't be any firmware-driver synchronization during a
 * driver reset. */
#define FW_ACK_TIME_OUT_MS		5000

#define FW_ACK_POLL_TIME_MS		1

#define FW_ACK_NUM_OF_POLL	(FW_ACK_TIME_OUT_MS/FW_ACK_POLL_TIME_MS)

/* LED Blink rate that will achieve ~15.9Hz */
#define LED_BLINK_RATE_VAL		480

/****************************************************************************
 * Driver <-> FW Mailbox						    *
 ****************************************************************************/
struct drv_fw_mb {
	u32 drv_mb_header;
#define DRV_MSG_CODE_MASK			0xffff0000
#define DRV_MSG_CODE_LOAD_REQ			0x10000000
#define DRV_MSG_CODE_LOAD_DONE			0x11000000
#define DRV_MSG_CODE_UNLOAD_REQ_WOL_EN		0x20000000
#define DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS 	0x20010000
#define DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP 	0x20020000
#define DRV_MSG_CODE_UNLOAD_DONE		0x21000000
#define DRV_MSG_CODE_DIAG_ENTER_REQ		0x50000000
#define DRV_MSG_CODE_DIAG_EXIT_REQ		0x60000000
#define DRV_MSG_CODE_VALIDATE_KEY		0x70000000
#define DRV_MSG_CODE_GET_CURR_KEY		0x80000000
#define DRV_MSG_CODE_GET_UPGRADE_KEY		0x81000000
#define DRV_MSG_CODE_GET_MANUF_KEY		0x82000000
#define DRV_MSG_CODE_LOAD_L2B_PRAM		0x90000000

#define DRV_MSG_SEQ_NUMBER_MASK 		0x0000ffff

	u32 drv_mb_param;

	u32 fw_mb_header;
#define FW_MSG_CODE_MASK			0xffff0000
#define FW_MSG_CODE_DRV_LOAD_COMMON		0x11000000
#define FW_MSG_CODE_DRV_LOAD_PORT		0x12000000
#define FW_MSG_CODE_DRV_LOAD_REFUSED		0x13000000
#define FW_MSG_CODE_DRV_LOAD_DONE		0x14000000
#define FW_MSG_CODE_DRV_UNLOAD_COMMON		0x21000000
#define FW_MSG_CODE_DRV_UNLOAD_PORT		0x22000000
#define FW_MSG_CODE_DRV_UNLOAD_DONE		0x23000000
#define FW_MSG_CODE_DIAG_ENTER_DONE		0x50000000
#define FW_MSG_CODE_DIAG_REFUSE 		0x51000000
#define FW_MSG_CODE_VALIDATE_KEY_SUCCESS	0x70000000
#define FW_MSG_CODE_VALIDATE_KEY_FAILURE	0x71000000
#define FW_MSG_CODE_GET_KEY_DONE		0x80000000
#define FW_MSG_CODE_NO_KEY			0x8f000000
#define FW_MSG_CODE_LIC_INFO_NOT_READY		0x8f800000
#define FW_MSG_CODE_L2B_PRAM_LOADED		0x90000000
#define FW_MSG_CODE_L2B_PRAM_T_LOAD_FAILURE	0x91000000
#define FW_MSG_CODE_L2B_PRAM_C_LOAD_FAILURE	0x92000000
#define FW_MSG_CODE_L2B_PRAM_X_LOAD_FAILURE	0x93000000
#define FW_MSG_CODE_L2B_PRAM_U_LOAD_FAILURE	0x94000000

#define FW_MSG_SEQ_NUMBER_MASK			0x0000ffff

	u32 fw_mb_param;

	u32 link_status;
	/* Driver should update this field on any link change event */

#define LINK_STATUS_LINK_FLAG_MASK		0x00000001
#define LINK_STATUS_LINK_UP			0x00000001
#define LINK_STATUS_SPEED_AND_DUPLEX_MASK	0x0000001E
#define LINK_STATUS_SPEED_AND_DUPLEX_AN_NOT_COMPLETE	(0<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10THD		(1<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10TFD		(2<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100TXHD		(3<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100T4		(4<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100TXFD		(5<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		(6<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(7<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000XFD		(7<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_2500THD		(8<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_2500TFD		(9<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_2500XFD		(9<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10GTFD		(10<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10GXFD		(10<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_12GTFD		(11<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_12GXFD		(11<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_12_5GTFD		(12<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_12_5GXFD		(12<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_13GTFD		(13<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_13GXFD		(13<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_15GTFD		(14<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_15GXFD		(14<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_16GTFD		(15<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_16GXFD		(15<<1)

#define LINK_STATUS_AUTO_NEGOTIATE_FLAG_MASK		0x00000020
#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED		0x00000020

#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE		0x00000040
#define LINK_STATUS_PARALLEL_DETECTION_FLAG_MASK	0x00000080
#define LINK_STATUS_PARALLEL_DETECTION_USED		0x00000080

#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE	0x00000200
#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE	0x00000400
#define LINK_STATUS_LINK_PARTNER_100T4_CAPABLE		0x00000800
#define LINK_STATUS_LINK_PARTNER_100TXFD_CAPABLE	0x00001000
#define LINK_STATUS_LINK_PARTNER_100TXHD_CAPABLE	0x00002000
#define LINK_STATUS_LINK_PARTNER_10TFD_CAPABLE		0x00004000
#define LINK_STATUS_LINK_PARTNER_10THD_CAPABLE		0x00008000

#define LINK_STATUS_TX_FLOW_CONTROL_FLAG_MASK		0x00010000
#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED		0x00010000

#define LINK_STATUS_RX_FLOW_CONTROL_FLAG_MASK		0x00020000
#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED		0x00020000

#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK	0x000C0000
#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE	(0<<18)
#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	(1<<18)
#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE	(2<<18)
#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE		(3<<18)

#define LINK_STATUS_SERDES_LINK 			0x00100000

#define LINK_STATUS_LINK_PARTNER_2500XFD_CAPABLE	0x00200000
#define LINK_STATUS_LINK_PARTNER_2500XHD_CAPABLE	0x00400000
#define LINK_STATUS_LINK_PARTNER_10GXFD_CAPABLE 	0x00800000
#define LINK_STATUS_LINK_PARTNER_12GXFD_CAPABLE 	0x01000000
#define LINK_STATUS_LINK_PARTNER_12_5GXFD_CAPABLE	0x02000000
#define LINK_STATUS_LINK_PARTNER_13GXFD_CAPABLE 	0x04000000
#define LINK_STATUS_LINK_PARTNER_15GXFD_CAPABLE 	0x08000000
#define LINK_STATUS_LINK_PARTNER_16GXFD_CAPABLE 	0x10000000

	u32 drv_pulse_mb;
#define DRV_PULSE_SEQ_MASK				0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK			0xffff0000
	/* The system time is in the format of
	 * (year-2001)*12*32 + month*32 + day. */
#define DRV_PULSE_ALWAYS_ALIVE				0x00008000
	/* Indicate to the firmware not to go into the
	 * OS-absent when it is not getting driver pulse.
	 * This is used for debugging as well for PXE(MBA). */

	u32 mcp_pulse_mb;
#define MCP_PULSE_SEQ_MASK				0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE				0x00008000
	/* Indicates to the driver not to assert due to lack
	 * of MCP response */
#define MCP_EVENT_MASK					0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ		0x00010000

};


/****************************************************************************
 * Shared HW configuration						    *
 ****************************************************************************/
struct shared_hw_cfg {					 /* NVRAM Offset */
	/* Up to 16 bytes of NULL-terminated string */
	u8  part_num[16];					/* 0x104 */

	u32 config;						/* 0x114 */
#define SHARED_HW_CFG_MDIO_VOLTAGE_MASK 	    0x00000001
#define SHARED_HW_CFG_MDIO_VOLTAGE_SHIFT	    0
#define SHARED_HW_CFG_MDIO_VOLTAGE_1_2V 	    0x00000000
#define SHARED_HW_CFG_MDIO_VOLTAGE_2_5V 	    0x00000001
#define SHARED_HW_CFG_MCP_RST_ON_CORE_RST_EN	    0x00000002

#define SHARED_HW_CFG_PORT_SWAP 		    0x00000004

#define SHARED_HW_CFG_BEACON_WOL_EN		    0x00000008

#define SHARED_HW_CFG_MFW_SELECT_MASK		    0x00000700
#define SHARED_HW_CFG_MFW_SELECT_SHIFT		    8
	/* Whatever MFW found in NVM
	   (if multiple found, priority order is: NC-SI, UMP, IPMI) */
#define SHARED_HW_CFG_MFW_SELECT_DEFAULT	    0x00000000
#define SHARED_HW_CFG_MFW_SELECT_NC_SI		    0x00000100
#define SHARED_HW_CFG_MFW_SELECT_UMP		    0x00000200
#define SHARED_HW_CFG_MFW_SELECT_IPMI		    0x00000300
	/* Use SPIO4 as an arbiter between: 0-NC_SI, 1-IPMI
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
#define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_IPMI   0x00000400
	/* Use SPIO4 as an arbiter between: 0-UMP, 1-IPMI
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
#define SHARED_HW_CFG_MFW_SELECT_SPIO4_UMP_IPMI     0x00000500
	/* Use SPIO4 as an arbiter between: 0-NC-SI, 1-UMP
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
#define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_UMP    0x00000600

#define SHARED_HW_CFG_LED_MODE_MASK		    0x000f0000
#define SHARED_HW_CFG_LED_MODE_SHIFT		    16
#define SHARED_HW_CFG_LED_MAC1			    0x00000000
#define SHARED_HW_CFG_LED_PHY1			    0x00010000
#define SHARED_HW_CFG_LED_PHY2			    0x00020000
#define SHARED_HW_CFG_LED_PHY3			    0x00030000
#define SHARED_HW_CFG_LED_MAC2			    0x00040000
#define SHARED_HW_CFG_LED_PHY4			    0x00050000
#define SHARED_HW_CFG_LED_PHY5			    0x00060000
#define SHARED_HW_CFG_LED_PHY6			    0x00070000
#define SHARED_HW_CFG_LED_MAC3			    0x00080000
#define SHARED_HW_CFG_LED_PHY7			    0x00090000
#define SHARED_HW_CFG_LED_PHY9			    0x000a0000
#define SHARED_HW_CFG_LED_PHY11 		    0x000b0000
#define SHARED_HW_CFG_LED_MAC4			    0x000c0000
#define SHARED_HW_CFG_LED_PHY8			    0x000d0000

#define SHARED_HW_CFG_AN_ENABLE_MASK		    0x3f000000
#define SHARED_HW_CFG_AN_ENABLE_SHIFT		    24
#define SHARED_HW_CFG_AN_ENABLE_CL37		    0x01000000
#define SHARED_HW_CFG_AN_ENABLE_CL73		    0x02000000
#define SHARED_HW_CFG_AN_ENABLE_BAM		    0x04000000
#define SHARED_HW_CFG_AN_ENABLE_PARALLEL_DETECTION  0x08000000
#define SHARED_HW_CFG_AN_EN_SGMII_FIBER_AUTO_DETECT 0x10000000
#define SHARED_HW_CFG_AN_ENABLE_REMOTE_PHY	    0x20000000

	u32 config2;						/* 0x118 */
	/* one time auto detect grace period (in sec) */
#define SHARED_HW_CFG_GRACE_PERIOD_MASK 	    0x000000ff
#define SHARED_HW_CFG_GRACE_PERIOD_SHIFT	    0

#define SHARED_HW_CFG_PCIE_GEN2_ENABLED 	    0x00000100

	/* The default value for the core clock is 250MHz and it is
	   achieved by setting the clock change to 4 */
#define SHARED_HW_CFG_CLOCK_CHANGE_MASK 	    0x00000e00
#define SHARED_HW_CFG_CLOCK_CHANGE_SHIFT	    9

#define SHARED_HW_CFG_SMBUS_TIMING_100KHZ	    0x00000000
#define SHARED_HW_CFG_SMBUS_TIMING_400KHZ	    0x00001000

#define SHARED_HW_CFG_HIDE_FUNC1		    0x00002000

	u32 power_dissipated;					/* 0x11c */
#define SHARED_HW_CFG_POWER_DIS_CMN_MASK	    0xff000000
#define SHARED_HW_CFG_POWER_DIS_CMN_SHIFT	    24

#define SHARED_HW_CFG_POWER_MGNT_SCALE_MASK	    0x00ff0000
#define SHARED_HW_CFG_POWER_MGNT_SCALE_SHIFT	    16
#define SHARED_HW_CFG_POWER_MGNT_UNKNOWN_SCALE	    0x00000000
#define SHARED_HW_CFG_POWER_MGNT_DOT_1_WATT	    0x00010000
#define SHARED_HW_CFG_POWER_MGNT_DOT_01_WATT	    0x00020000
#define SHARED_HW_CFG_POWER_MGNT_DOT_001_WATT	    0x00030000

	u32 ump_nc_si_config;					/* 0x120 */
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MASK	    0x00000003
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_SHIFT	    0
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MAC	    0x00000000
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_PHY	    0x00000001
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MII	    0x00000000
#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_RMII	    0x00000002

#define SHARED_HW_CFG_UMP_NC_SI_NUM_DEVS_MASK	    0x00000f00
#define SHARED_HW_CFG_UMP_NC_SI_NUM_DEVS_SHIFT	    8

#define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_MASK   0x00ff0000
#define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_SHIFT  16
#define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_NONE   0x00000000
#define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_BCM5221 0x00010000

	u32 board;						/* 0x124 */
#define SHARED_HW_CFG_BOARD_TYPE_MASK		    0x0000ffff
#define SHARED_HW_CFG_BOARD_TYPE_SHIFT		    0
#define SHARED_HW_CFG_BOARD_TYPE_NONE		    0x00000000
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1000     0x00000001
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1001     0x00000002
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1002G    0x00000003
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1004G    0x00000004
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1007G    0x00000005
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1015G    0x00000006
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710A1020G    0x00000007
#define SHARED_HW_CFG_BOARD_TYPE_BCM957710T1003G    0x00000008

#define SHARED_HW_CFG_BOARD_VER_MASK		    0xffff0000
#define SHARED_HW_CFG_BOARD_VER_SHIFT		    16
#define SHARED_HW_CFG_BOARD_MAJOR_VER_MASK	    0xf0000000
#define SHARED_HW_CFG_BOARD_MAJOR_VER_SHIFT	    28
#define SHARED_HW_CFG_BOARD_MINOR_VER_MASK	    0x0f000000
#define SHARED_HW_CFG_BOARD_MINOR_VER_SHIFT	    24
#define SHARED_HW_CFG_BOARD_REV_MASK		    0x00ff0000
#define SHARED_HW_CFG_BOARD_REV_SHIFT		    16

	u32 reserved;						/* 0x128 */

};

/****************************************************************************
 * Port HW configuration						    *
 ****************************************************************************/
struct port_hw_cfg {	/* function 0: 0x12c-0x2bb, function 1: 0x2bc-0x44b */

	/* Fields below are port specific (in anticipation of dual port
	   devices */
	u32 pci_id;
#define PORT_HW_CFG_PCI_VENDOR_ID_MASK		    0xffff0000
#define PORT_HW_CFG_PCI_DEVICE_ID_MASK		    0x0000ffff

	u32 pci_sub_id;
#define PORT_HW_CFG_PCI_SUBSYS_DEVICE_ID_MASK	    0xffff0000
#define PORT_HW_CFG_PCI_SUBSYS_VENDOR_ID_MASK	    0x0000ffff

	u32 power_dissipated;
#define PORT_HW_CFG_POWER_DIS_D3_MASK		    0xff000000
#define PORT_HW_CFG_POWER_DIS_D3_SHIFT		    24
#define PORT_HW_CFG_POWER_DIS_D2_MASK		    0x00ff0000
#define PORT_HW_CFG_POWER_DIS_D2_SHIFT		    16
#define PORT_HW_CFG_POWER_DIS_D1_MASK		    0x0000ff00
#define PORT_HW_CFG_POWER_DIS_D1_SHIFT		    8
#define PORT_HW_CFG_POWER_DIS_D0_MASK		    0x000000ff
#define PORT_HW_CFG_POWER_DIS_D0_SHIFT		    0

	u32 power_consumed;
#define PORT_HW_CFG_POWER_CONS_D3_MASK		    0xff000000
#define PORT_HW_CFG_POWER_CONS_D3_SHIFT 	    24
#define PORT_HW_CFG_POWER_CONS_D2_MASK		    0x00ff0000
#define PORT_HW_CFG_POWER_CONS_D2_SHIFT 	    16
#define PORT_HW_CFG_POWER_CONS_D1_MASK		    0x0000ff00
#define PORT_HW_CFG_POWER_CONS_D1_SHIFT 	    8
#define PORT_HW_CFG_POWER_CONS_D0_MASK		    0x000000ff
#define PORT_HW_CFG_POWER_CONS_D0_SHIFT 	    0

	u32 mac_upper;
#define PORT_HW_CFG_UPPERMAC_MASK		    0x0000ffff
#define PORT_HW_CFG_UPPERMAC_SHIFT		    0
	u32 mac_lower;

	u32 iscsi_mac_upper;  /* Upper 16 bits are always zeroes */
	u32 iscsi_mac_lower;

	u32 rdma_mac_upper;   /* Upper 16 bits are always zeroes */
	u32 rdma_mac_lower;

	u32 serdes_config;
	/* for external PHY, or forced mode or during AN */
#define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_MASK 0xffff0000
#define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_SHIFT  16

#define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_MASK    0x0000ffff
#define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_SHIFT   0

	u16 serdes_tx_driver_pre_emphasis[16];
	u16 serdes_rx_driver_equalizer[16];

	u32 xgxs_config_lane0;
	u32 xgxs_config_lane1;
	u32 xgxs_config_lane2;
	u32 xgxs_config_lane3;
	/* for external PHY, or forced mode or during AN */
#define PORT_HW_CFG_XGXS_TX_DRV_PRE_EMPHASIS_MASK   0xffff0000
#define PORT_HW_CFG_XGXS_TX_DRV_PRE_EMPHASIS_SHIFT  16

#define PORT_HW_CFG_XGXS_RX_DRV_EQUALIZER_MASK	    0x0000ffff
#define PORT_HW_CFG_XGXS_RX_DRV_EQUALIZER_SHIFT     0

	u16 xgxs_tx_driver_pre_emphasis_lane0[16];
	u16 xgxs_tx_driver_pre_emphasis_lane1[16];
	u16 xgxs_tx_driver_pre_emphasis_lane2[16];
	u16 xgxs_tx_driver_pre_emphasis_lane3[16];

	u16 xgxs_rx_driver_equalizer_lane0[16];
	u16 xgxs_rx_driver_equalizer_lane1[16];
	u16 xgxs_rx_driver_equalizer_lane2[16];
	u16 xgxs_rx_driver_equalizer_lane3[16];

	u32 lane_config;
#define PORT_HW_CFG_LANE_SWAP_CFG_MASK		    0x0000ffff
#define PORT_HW_CFG_LANE_SWAP_CFG_SHIFT 	    0
#define PORT_HW_CFG_LANE_SWAP_CFG_TX_MASK	    0x000000ff
#define PORT_HW_CFG_LANE_SWAP_CFG_TX_SHIFT	    0
#define PORT_HW_CFG_LANE_SWAP_CFG_RX_MASK	    0x0000ff00
#define PORT_HW_CFG_LANE_SWAP_CFG_RX_SHIFT	    8
#define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK	    0x0000c000
#define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT	    14
	/* AN and forced */
#define PORT_HW_CFG_LANE_SWAP_CFG_01230123	    0x00001b1b
	/* forced only */
#define PORT_HW_CFG_LANE_SWAP_CFG_01233210	    0x00001be4
	/* forced only */
#define PORT_HW_CFG_LANE_SWAP_CFG_31203120	    0x0000d8d8
	/* forced only */
#define PORT_HW_CFG_LANE_SWAP_CFG_32103210	    0x0000e4e4

	u32 external_phy_config;
#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK	    0xff000000
#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_SHIFT	    24
#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT	    0x00000000
#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482     0x01000000
#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_NOT_CONN    0xff000000

#define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_MASK	    0x00ff0000
#define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_SHIFT	    16

#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK	    0x0000ff00
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SHIFT	    8
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT	    0x00000000
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8071	    0x00000100
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072	    0x00000200
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073	    0x00000300
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705	    0x00000400
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706	    0x00000500
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8276	    0x00000600
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8481	    0x00000700
#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN	    0x0000ff00

#define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK	    0x000000ff
#define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT	    0

	u32 speed_capability_mask;
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_MASK	    0xffff0000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_SHIFT	    16
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL    0x00010000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF    0x00020000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF   0x00040000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL   0x00080000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_1G	    0x00100000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G	    0x00200000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10G	    0x00400000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_12G	    0x00800000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_12_5G	    0x01000000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_13G	    0x02000000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_15G	    0x04000000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_16G	    0x08000000
#define PORT_HW_CFG_SPEED_CAPABILITY_D0_RESERVED    0xf0000000

#define PORT_HW_CFG_SPEED_CAPABILITY_D3_MASK	    0x0000ffff
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_SHIFT	    0
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_FULL    0x00000001
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_HALF    0x00000002
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_HALF   0x00000004
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_FULL   0x00000008
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_1G	    0x00000010
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_2_5G	    0x00000020
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10G	    0x00000040
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_12G	    0x00000080
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_12_5G	    0x00000100
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_13G	    0x00000200
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_15G	    0x00000400
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_16G	    0x00000800
#define PORT_HW_CFG_SPEED_CAPABILITY_D3_RESERVED    0x0000f000

	u32 reserved[2];

};

/****************************************************************************
 * Shared Feature configuration 					    *
 ****************************************************************************/
struct shared_feat_cfg {				 /* NVRAM Offset */
	u32 bmc_common; 					/* 0x450 */
#define SHARED_FEATURE_BMC_ECHO_MODE_EN 	    0x00000001

};


/****************************************************************************
 * Port Feature configuration						    *
 ****************************************************************************/
struct port_feat_cfg {	/* function 0: 0x454-0x4c7, function 1: 0x4c8-0x53b */
	u32 config;
#define PORT_FEATURE_BAR1_SIZE_MASK		    0x0000000f
#define PORT_FEATURE_BAR1_SIZE_SHIFT		    0
#define PORT_FEATURE_BAR1_SIZE_DISABLED 	    0x00000000
#define PORT_FEATURE_BAR1_SIZE_64K		    0x00000001
#define PORT_FEATURE_BAR1_SIZE_128K		    0x00000002
#define PORT_FEATURE_BAR1_SIZE_256K		    0x00000003
#define PORT_FEATURE_BAR1_SIZE_512K		    0x00000004
#define PORT_FEATURE_BAR1_SIZE_1M		    0x00000005
#define PORT_FEATURE_BAR1_SIZE_2M		    0x00000006
#define PORT_FEATURE_BAR1_SIZE_4M		    0x00000007
#define PORT_FEATURE_BAR1_SIZE_8M		    0x00000008
#define PORT_FEATURE_BAR1_SIZE_16M		    0x00000009
#define PORT_FEATURE_BAR1_SIZE_32M		    0x0000000a
#define PORT_FEATURE_BAR1_SIZE_64M		    0x0000000b
#define PORT_FEATURE_BAR1_SIZE_128M		    0x0000000c
#define PORT_FEATURE_BAR1_SIZE_256M		    0x0000000d
#define PORT_FEATURE_BAR1_SIZE_512M		    0x0000000e
#define PORT_FEATURE_BAR1_SIZE_1G		    0x0000000f
#define PORT_FEATURE_BAR2_SIZE_MASK		    0x000000f0
#define PORT_FEATURE_BAR2_SIZE_SHIFT		    4
#define PORT_FEATURE_BAR2_SIZE_DISABLED 	    0x00000000
#define PORT_FEATURE_BAR2_SIZE_64K		    0x00000010
#define PORT_FEATURE_BAR2_SIZE_128K		    0x00000020
#define PORT_FEATURE_BAR2_SIZE_256K		    0x00000030
#define PORT_FEATURE_BAR2_SIZE_512K		    0x00000040
#define PORT_FEATURE_BAR2_SIZE_1M		    0x00000050
#define PORT_FEATURE_BAR2_SIZE_2M		    0x00000060
#define PORT_FEATURE_BAR2_SIZE_4M		    0x00000070
#define PORT_FEATURE_BAR2_SIZE_8M		    0x00000080
#define PORT_FEATURE_BAR2_SIZE_16M		    0x00000090
#define PORT_FEATURE_BAR2_SIZE_32M		    0x000000a0
#define PORT_FEATURE_BAR2_SIZE_64M		    0x000000b0
#define PORT_FEATURE_BAR2_SIZE_128M		    0x000000c0
#define PORT_FEATURE_BAR2_SIZE_256M		    0x000000d0
#define PORT_FEATURE_BAR2_SIZE_512M		    0x000000e0
#define PORT_FEATURE_BAR2_SIZE_1G		    0x000000f0
#define PORT_FEATURE_EN_SIZE_MASK		    0x07000000
#define PORT_FEATURE_EN_SIZE_SHIFT		    24
#define PORT_FEATURE_WOL_ENABLED		    0x01000000
#define PORT_FEATURE_MBA_ENABLED		    0x02000000
#define PORT_FEATURE_MFW_ENABLED		    0x04000000

	u32 wol_config;
	/* Default is used when driver sets to "auto" mode */
#define PORT_FEATURE_WOL_DEFAULT_MASK		    0x00000003
#define PORT_FEATURE_WOL_DEFAULT_SHIFT		    0
#define PORT_FEATURE_WOL_DEFAULT_DISABLE	    0x00000000
#define PORT_FEATURE_WOL_DEFAULT_MAGIC		    0x00000001
#define PORT_FEATURE_WOL_DEFAULT_ACPI		    0x00000002
#define PORT_FEATURE_WOL_DEFAULT_MAGIC_AND_ACPI     0x00000003
#define PORT_FEATURE_WOL_RES_PAUSE_CAP		    0x00000004
#define PORT_FEATURE_WOL_RES_ASYM_PAUSE_CAP	    0x00000008
#define PORT_FEATURE_WOL_ACPI_UPON_MGMT 	    0x00000010

	u32 mba_config;
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK	    0x00000003
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_SHIFT	    0
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_PXE	    0x00000000
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_RPL	    0x00000001
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_BOOTP	    0x00000002
#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_ISCSIB     0x00000003
#define PORT_FEATURE_MBA_RES_PAUSE_CAP		    0x00000100
#define PORT_FEATURE_MBA_RES_ASYM_PAUSE_CAP	    0x00000200
#define PORT_FEATURE_MBA_SETUP_PROMPT_ENABLE	    0x00000400
#define PORT_FEATURE_MBA_HOTKEY_CTRL_S		    0x00000000
#define PORT_FEATURE_MBA_HOTKEY_CTRL_B		    0x00000800
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_MASK	    0x000ff000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_SHIFT	    12
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_DISABLED	    0x00000000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_2K	    0x00001000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_4K	    0x00002000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_8K	    0x00003000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_16K	    0x00004000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_32K	    0x00005000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_64K	    0x00006000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_128K	    0x00007000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_256K	    0x00008000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_512K	    0x00009000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_1M	    0x0000a000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_2M	    0x0000b000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_4M	    0x0000c000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_8M	    0x0000d000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_16M	    0x0000e000
#define PORT_FEATURE_MBA_EXP_ROM_SIZE_32M	    0x0000f000
#define PORT_FEATURE_MBA_MSG_TIMEOUT_MASK	    0x00f00000
#define PORT_FEATURE_MBA_MSG_TIMEOUT_SHIFT	    20
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_MASK	    0x03000000
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_SHIFT	    24
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_AUTO	    0x00000000
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_BBS	    0x01000000
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT18H	    0x02000000
#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT19H	    0x03000000
#define PORT_FEATURE_MBA_LINK_SPEED_MASK	    0x3c000000
#define PORT_FEATURE_MBA_LINK_SPEED_SHIFT	    26
#define PORT_FEATURE_MBA_LINK_SPEED_AUTO	    0x00000000
#define PORT_FEATURE_MBA_LINK_SPEED_10HD	    0x04000000
#define PORT_FEATURE_MBA_LINK_SPEED_10FD	    0x08000000
#define PORT_FEATURE_MBA_LINK_SPEED_100HD	    0x0c000000
#define PORT_FEATURE_MBA_LINK_SPEED_100FD	    0x10000000
#define PORT_FEATURE_MBA_LINK_SPEED_1GBPS	    0x14000000
#define PORT_FEATURE_MBA_LINK_SPEED_2_5GBPS	    0x18000000
#define PORT_FEATURE_MBA_LINK_SPEED_10GBPS_CX4	    0x1c000000
#define PORT_FEATURE_MBA_LINK_SPEED_10GBPS_KX4	    0x20000000
#define PORT_FEATURE_MBA_LINK_SPEED_10GBPS_KR	    0x24000000
#define PORT_FEATURE_MBA_LINK_SPEED_12GBPS	    0x28000000
#define PORT_FEATURE_MBA_LINK_SPEED_12_5GBPS	    0x2c000000
#define PORT_FEATURE_MBA_LINK_SPEED_13GBPS	    0x30000000
#define PORT_FEATURE_MBA_LINK_SPEED_15GBPS	    0x34000000
#define PORT_FEATURE_MBA_LINK_SPEED_16GBPS	    0x38000000

	u32 bmc_config;
#define PORT_FEATURE_BMC_LINK_OVERRIDE_DEFAULT	    0x00000000
#define PORT_FEATURE_BMC_LINK_OVERRIDE_EN	    0x00000001

	u32 mba_vlan_cfg;
#define PORT_FEATURE_MBA_VLAN_TAG_MASK		    0x0000ffff
#define PORT_FEATURE_MBA_VLAN_TAG_SHIFT 	    0
#define PORT_FEATURE_MBA_VLAN_EN		    0x00010000

	u32 resource_cfg;
#define PORT_FEATURE_RESOURCE_CFG_VALID 	    0x00000001
#define PORT_FEATURE_RESOURCE_CFG_DIAG		    0x00000002
#define PORT_FEATURE_RESOURCE_CFG_L2		    0x00000004
#define PORT_FEATURE_RESOURCE_CFG_ISCSI 	    0x00000008
#define PORT_FEATURE_RESOURCE_CFG_RDMA		    0x00000010

	u32 smbus_config;
	/* Obsolete */
#define PORT_FEATURE_SMBUS_EN			    0x00000001
#define PORT_FEATURE_SMBUS_ADDR_MASK		    0x000000fe
#define PORT_FEATURE_SMBUS_ADDR_SHIFT		    1

	u32 iscsib_boot_cfg;
#define PORT_FEATURE_ISCSIB_SKIP_TARGET_BOOT	    0x00000001

	u32 link_config;    /* Used as HW defaults for the driver */
#define PORT_FEATURE_CONNECTED_SWITCH_MASK	    0x03000000
#define PORT_FEATURE_CONNECTED_SWITCH_SHIFT	    24
	/* (forced) low speed switch (< 10G) */
#define PORT_FEATURE_CON_SWITCH_1G_SWITCH	    0x00000000
	/* (forced) high speed switch (>= 10G) */
#define PORT_FEATURE_CON_SWITCH_10G_SWITCH	    0x01000000
#define PORT_FEATURE_CON_SWITCH_AUTO_DETECT	    0x02000000
#define PORT_FEATURE_CON_SWITCH_ONE_TIME_DETECT     0x03000000

#define PORT_FEATURE_LINK_SPEED_MASK		    0x000f0000
#define PORT_FEATURE_LINK_SPEED_SHIFT		    16
#define PORT_FEATURE_LINK_SPEED_AUTO		    0x00000000
#define PORT_FEATURE_LINK_SPEED_10M_FULL	    0x00010000
#define PORT_FEATURE_LINK_SPEED_10M_HALF	    0x00020000
#define PORT_FEATURE_LINK_SPEED_100M_HALF	    0x00030000
#define PORT_FEATURE_LINK_SPEED_100M_FULL	    0x00040000
#define PORT_FEATURE_LINK_SPEED_1G		    0x00050000
#define PORT_FEATURE_LINK_SPEED_2_5G		    0x00060000
#define PORT_FEATURE_LINK_SPEED_10G_CX4 	    0x00070000
#define PORT_FEATURE_LINK_SPEED_10G_KX4 	    0x00080000
#define PORT_FEATURE_LINK_SPEED_10G_KR		    0x00090000
#define PORT_FEATURE_LINK_SPEED_12G		    0x000a0000
#define PORT_FEATURE_LINK_SPEED_12_5G		    0x000b0000
#define PORT_FEATURE_LINK_SPEED_13G		    0x000c0000
#define PORT_FEATURE_LINK_SPEED_15G		    0x000d0000
#define PORT_FEATURE_LINK_SPEED_16G		    0x000e0000

#define PORT_FEATURE_FLOW_CONTROL_MASK		    0x00000700
#define PORT_FEATURE_FLOW_CONTROL_SHIFT 	    8
#define PORT_FEATURE_FLOW_CONTROL_AUTO		    0x00000000
#define PORT_FEATURE_FLOW_CONTROL_TX		    0x00000100
#define PORT_FEATURE_FLOW_CONTROL_RX		    0x00000200
#define PORT_FEATURE_FLOW_CONTROL_BOTH		    0x00000300
#define PORT_FEATURE_FLOW_CONTROL_NONE		    0x00000400

	/* The default for MCP link configuration,
	   uses the same defines as link_config */
	u32 mfw_wol_link_cfg;

	u32 reserved[19];

};


/****************************************************************************
 * Device Information							    *
 ****************************************************************************/
struct dev_info {						    /* size */

	u32    bc_rev; /* 8 bits each: major, minor, build */	       /* 4 */

	struct shared_hw_cfg	 shared_hw_config;		      /* 40 */

	struct port_hw_cfg	 port_hw_config[FUNC_MAX];     /* 400*2=800 */

	struct shared_feat_cfg	 shared_feature_config; 	       /* 4 */

	struct port_feat_cfg	 port_feature_config[FUNC_MAX];/* 116*2=232 */

};


/****************************************************************************
 * Management firmware state						    *
 ****************************************************************************/
/* Allocate 320 bytes for management firmware: still not known exactly
 * how much IMD needs. */
#define MGMTFW_STATE_WORD_SIZE				    80

struct mgmtfw_state {
	u32 opaque[MGMTFW_STATE_WORD_SIZE];
};


/****************************************************************************
 * Shared Memory Region 						    *
 ****************************************************************************/
struct shmem_region {			       /*   SharedMem Offset (size) */
	u32		    validity_map[FUNC_MAX];    /* 0x0 (4 * 2 = 0x8) */
#define SHR_MEM_VALIDITY_PCI_CFG		    0x00000001
#define SHR_MEM_VALIDITY_MB			    0x00000002
#define SHR_MEM_VALIDITY_DEV_INFO		    0x00000004
	/* One licensing bit should be set */
#define SHR_MEM_VALIDITY_LIC_KEY_IN_EFFECT_MASK     0x00000038
#define SHR_MEM_VALIDITY_LIC_MANUF_KEY_IN_EFFECT    0x00000008
#define SHR_MEM_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT  0x00000010
#define SHR_MEM_VALIDITY_LIC_NO_KEY_IN_EFFECT	    0x00000020

	struct drv_fw_mb    drv_fw_mb[FUNC_MAX];     /* 0x8 (28 * 2 = 0x38) */

	struct dev_info     dev_info;			    /* 0x40 (0x438) */

#ifdef _LICENSE_H
	license_key_t	    drv_lic_key[FUNC_MAX]; /* 0x478 (52 * 2 = 0x68) */
#else /* Linux! */
	u8		    reserved[52*FUNC_MAX];
#endif

	/* FW information (for internal FW use) */
	u32		    fw_info_fio_offset; 	   /* 0x4e0 (0x4)   */
	struct mgmtfw_state mgmtfw_state;		   /* 0x4e4 (0x140) */

};							   /* 0x624 */


#define BCM_5710_FW_MAJOR_VERSION			4
#define BCM_5710_FW_MINOR_VERSION			0
#define BCM_5710_FW_REVISION_VERSION			14
#define BCM_5710_FW_COMPILE_FLAGS			1


/*
 * attention bits
 */
struct atten_def_status_block {
	u32 attn_bits;
	u32 attn_bits_ack;
#if defined(__BIG_ENDIAN)
	u16 attn_bits_index;
	u8 reserved0;
	u8 status_block_id;
#elif defined(__LITTLE_ENDIAN)
	u8 status_block_id;
	u8 reserved0;
	u16 attn_bits_index;
#endif
	u32 reserved1;
};


/*
 * common data for all protocols
 */
struct doorbell_hdr {
	u8 header;
#define DOORBELL_HDR_RX (0x1<<0)
#define DOORBELL_HDR_RX_SHIFT 0
#define DOORBELL_HDR_DB_TYPE (0x1<<1)
#define DOORBELL_HDR_DB_TYPE_SHIFT 1
#define DOORBELL_HDR_DPM_SIZE (0x3<<2)
#define DOORBELL_HDR_DPM_SIZE_SHIFT 2
#define DOORBELL_HDR_CONN_TYPE (0xF<<4)
#define DOORBELL_HDR_CONN_TYPE_SHIFT 4
};

/*
 * doorbell message send to the chip
 */
struct doorbell {
#if defined(__BIG_ENDIAN)
	u16 zero_fill2;
	u8 zero_fill1;
	struct doorbell_hdr header;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr header;
	u8 zero_fill1;
	u16 zero_fill2;
#endif
};


/*
 * IGU driver acknowlegement register
 */
struct igu_ack_register {
#if defined(__BIG_ENDIAN)
	u16 sb_id_and_flags;
#define IGU_ACK_REGISTER_STATUS_BLOCK_ID (0x1F<<0)
#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT 0
#define IGU_ACK_REGISTER_STORM_ID (0x7<<5)
#define IGU_ACK_REGISTER_STORM_ID_SHIFT 5
#define IGU_ACK_REGISTER_UPDATE_INDEX (0x1<<8)
#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT 8
#define IGU_ACK_REGISTER_INTERRUPT_MODE (0x3<<9)
#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT 9
#define IGU_ACK_REGISTER_RESERVED (0x1F<<11)
#define IGU_ACK_REGISTER_RESERVED_SHIFT 11
	u16 status_block_index;
#elif defined(__LITTLE_ENDIAN)
	u16 status_block_index;
	u16 sb_id_and_flags;
#define IGU_ACK_REGISTER_STATUS_BLOCK_ID (0x1F<<0)
#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT 0
#define IGU_ACK_REGISTER_STORM_ID (0x7<<5)
#define IGU_ACK_REGISTER_STORM_ID_SHIFT 5
#define IGU_ACK_REGISTER_UPDATE_INDEX (0x1<<8)
#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT 8
#define IGU_ACK_REGISTER_INTERRUPT_MODE (0x3<<9)
#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT 9
#define IGU_ACK_REGISTER_RESERVED (0x1F<<11)
#define IGU_ACK_REGISTER_RESERVED_SHIFT 11
#endif
};


/*
 * Parser parsing flags field
 */
struct parsing_flags {
	u16 flags;
#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE (0x1<<0)
#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE_SHIFT 0
#define PARSING_FLAGS_NUMBER_OF_NESTED_VLANS (0x3<<1)
#define PARSING_FLAGS_NUMBER_OF_NESTED_VLANS_SHIFT 1
#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL (0x3<<3)
#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT 3
#define PARSING_FLAGS_IP_OPTIONS (0x1<<5)
#define PARSING_FLAGS_IP_OPTIONS_SHIFT 5
#define PARSING_FLAGS_FRAGMENTATION_STATUS (0x1<<6)
#define PARSING_FLAGS_FRAGMENTATION_STATUS_SHIFT 6
#define PARSING_FLAGS_OVER_IP_PROTOCOL (0x3<<7)
#define PARSING_FLAGS_OVER_IP_PROTOCOL_SHIFT 7
#define PARSING_FLAGS_PURE_ACK_INDICATION (0x1<<9)
#define PARSING_FLAGS_PURE_ACK_INDICATION_SHIFT 9
#define PARSING_FLAGS_TCP_OPTIONS_EXIST (0x1<<10)
#define PARSING_FLAGS_TCP_OPTIONS_EXIST_SHIFT 10
#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG (0x1<<11)
#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG_SHIFT 11
#define PARSING_FLAGS_CONNECTION_MATCH (0x1<<12)
#define PARSING_FLAGS_CONNECTION_MATCH_SHIFT 12
#define PARSING_FLAGS_LLC_SNAP (0x1<<13)
#define PARSING_FLAGS_LLC_SNAP_SHIFT 13
#define PARSING_FLAGS_RESERVED0 (0x3<<14)
#define PARSING_FLAGS_RESERVED0_SHIFT 14
};


/*
 * dmae command structure
 */
struct dmae_command {
	u32 opcode;
#define DMAE_COMMAND_SRC (0x1<<0)
#define DMAE_COMMAND_SRC_SHIFT 0
#define DMAE_COMMAND_DST (0x3<<1)
#define DMAE_COMMAND_DST_SHIFT 1
#define DMAE_COMMAND_C_DST (0x1<<3)
#define DMAE_COMMAND_C_DST_SHIFT 3
#define DMAE_COMMAND_C_TYPE_ENABLE (0x1<<4)
#define DMAE_COMMAND_C_TYPE_ENABLE_SHIFT 4
#define DMAE_COMMAND_C_TYPE_CRC_ENABLE (0x1<<5)
#define DMAE_COMMAND_C_TYPE_CRC_ENABLE_SHIFT 5
#define DMAE_COMMAND_C_TYPE_CRC_OFFSET (0x7<<6)
#define DMAE_COMMAND_C_TYPE_CRC_OFFSET_SHIFT 6
#define DMAE_COMMAND_ENDIANITY (0x3<<9)
#define DMAE_COMMAND_ENDIANITY_SHIFT 9
#define DMAE_COMMAND_PORT (0x1<<11)
#define DMAE_COMMAND_PORT_SHIFT 11
#define DMAE_COMMAND_CRC_RESET (0x1<<12)
#define DMAE_COMMAND_CRC_RESET_SHIFT 12
#define DMAE_COMMAND_SRC_RESET (0x1<<13)
#define DMAE_COMMAND_SRC_RESET_SHIFT 13
#define DMAE_COMMAND_DST_RESET (0x1<<14)
#define DMAE_COMMAND_DST_RESET_SHIFT 14
#define DMAE_COMMAND_RESERVED0 (0x1FFFF<<15)
#define DMAE_COMMAND_RESERVED0_SHIFT 15
	u32 src_addr_lo;
	u32 src_addr_hi;
	u32 dst_addr_lo;
	u32 dst_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 reserved1;
	u16 len;
#elif defined(__LITTLE_ENDIAN)
	u16 len;
	u16 reserved1;
#endif
	u32 comp_addr_lo;
	u32 comp_addr_hi;
	u32 comp_val;
	u32 crc32;
	u32 crc32_c;
#if defined(__BIG_ENDIAN)
	u16 crc16_c;
	u16 crc16;
#elif defined(__LITTLE_ENDIAN)
	u16 crc16;
	u16 crc16_c;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved2;
	u16 crc_t10;
#elif defined(__LITTLE_ENDIAN)
	u16 crc_t10;
	u16 reserved2;
#endif
#if defined(__BIG_ENDIAN)
	u16 xsum8;
	u16 xsum16;
#elif defined(__LITTLE_ENDIAN)
	u16 xsum16;
	u16 xsum8;
#endif
};


struct double_regpair {
	u32 regpair0_lo;
	u32 regpair0_hi;
	u32 regpair1_lo;
	u32 regpair1_hi;
};


/*
 * The eth Rx Buffer Descriptor
 */
struct eth_rx_bd {
	u32 addr_lo;
	u32 addr_hi;
};

/*
 * The eth storm context of Ustorm
 */
struct ustorm_eth_st_context {
#if defined(__BIG_ENDIAN)
	u8 sb_index_number;
	u8 status_block_id;
	u8 __local_rx_bd_cons;
	u8 __local_rx_bd_prod;
#elif defined(__LITTLE_ENDIAN)
	u8 __local_rx_bd_prod;
	u8 __local_rx_bd_cons;
	u8 status_block_id;
	u8 sb_index_number;
#endif
#if defined(__BIG_ENDIAN)
	u16 rcq_cons;
	u16 rx_bd_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 rx_bd_cons;
	u16 rcq_cons;
#endif
	u32 rx_bd_page_base_lo;
	u32 rx_bd_page_base_hi;
	u32 rcq_base_address_lo;
	u32 rcq_base_address_hi;
#if defined(__BIG_ENDIAN)
	u16 __num_of_returned_cqes;
	u8 num_rss;
	u8 flags;
#define USTORM_ETH_ST_CONTEXT_ENABLE_MC_ALIGNMENT (0x1<<0)
#define USTORM_ETH_ST_CONTEXT_ENABLE_MC_ALIGNMENT_SHIFT 0
#define USTORM_ETH_ST_CONTEXT_ENABLE_DYNAMIC_HC (0x1<<1)
#define USTORM_ETH_ST_CONTEXT_ENABLE_DYNAMIC_HC_SHIFT 1
#define USTORM_ETH_ST_CONTEXT_ENABLE_TPA (0x1<<2)
#define USTORM_ETH_ST_CONTEXT_ENABLE_TPA_SHIFT 2
#define __USTORM_ETH_ST_CONTEXT_RESERVED0 (0x1F<<3)
#define __USTORM_ETH_ST_CONTEXT_RESERVED0_SHIFT 3
#elif defined(__LITTLE_ENDIAN)
	u8 flags;
#define USTORM_ETH_ST_CONTEXT_ENABLE_MC_ALIGNMENT (0x1<<0)
#define USTORM_ETH_ST_CONTEXT_ENABLE_MC_ALIGNMENT_SHIFT 0
#define USTORM_ETH_ST_CONTEXT_ENABLE_DYNAMIC_HC (0x1<<1)
#define USTORM_ETH_ST_CONTEXT_ENABLE_DYNAMIC_HC_SHIFT 1
#define USTORM_ETH_ST_CONTEXT_ENABLE_TPA (0x1<<2)
#define USTORM_ETH_ST_CONTEXT_ENABLE_TPA_SHIFT 2
#define __USTORM_ETH_ST_CONTEXT_RESERVED0 (0x1F<<3)
#define __USTORM_ETH_ST_CONTEXT_RESERVED0_SHIFT 3
	u8 num_rss;
	u16 __num_of_returned_cqes;
#endif
#if defined(__BIG_ENDIAN)
	u16 mc_alignment_size;
	u16 agg_threshold;
#elif defined(__LITTLE_ENDIAN)
	u16 agg_threshold;
	u16 mc_alignment_size;
#endif
	struct eth_rx_bd __local_bd_ring[16];
};

/*
 * The eth storm context of Tstorm
 */
struct tstorm_eth_st_context {
	u32 __reserved0[28];
};

/*
 * The eth aggregative context section of Xstorm
 */
struct xstorm_eth_extra_ag_context_section {
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars1;
	u8 __reserved50;
	u16 __mss;
#elif defined(__LITTLE_ENDIAN)
	u16 __mss;
	u8 __reserved50;
	u8 __tcp_agg_vars1;
#endif
	u32 __snd_nxt;
	u32 __tx_wnd;
	u32 __snd_una;
	u32 __reserved53;
#if defined(__BIG_ENDIAN)
	u8 __agg_val8_th;
	u8 __agg_val8;
	u16 __tcp_agg_vars2;
#elif defined(__LITTLE_ENDIAN)
	u16 __tcp_agg_vars2;
	u8 __agg_val8;
	u8 __agg_val8_th;
#endif
	u32 __reserved58;
	u32 __reserved59;
	u32 __reserved60;
	u32 __reserved61;
#if defined(__BIG_ENDIAN)
	u16 __agg_val7_th;
	u16 __agg_val7;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val7;
	u16 __agg_val7_th;
#endif
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars5;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars3;
	u8 __reserved62;
#elif defined(__LITTLE_ENDIAN)
	u8 __reserved62;
	u8 __tcp_agg_vars3;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars5;
#endif
	u32 __tcp_agg_vars6;
#if defined(__BIG_ENDIAN)
	u16 __agg_misc6;
	u16 __tcp_agg_vars7;
#elif defined(__LITTLE_ENDIAN)
	u16 __tcp_agg_vars7;
	u16 __agg_misc6;
#endif
	u32 __agg_val10;
	u32 __agg_val10_th;
#if defined(__BIG_ENDIAN)
	u16 __reserved3;
	u8 __reserved2;
	u8 __agg_misc7;
#elif defined(__LITTLE_ENDIAN)
	u8 __agg_misc7;
	u8 __reserved2;
	u16 __reserved3;
#endif
};

/*
 * The eth aggregative context of Xstorm
 */
struct xstorm_eth_ag_context {
#if defined(__BIG_ENDIAN)
	u16 __bd_prod;
	u8 __agg_vars1;
	u8 __state;
#elif defined(__LITTLE_ENDIAN)
	u8 __state;
	u8 __agg_vars1;
	u16 __bd_prod;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_reserved;
	u8 __agg_vars4;
	u8 __agg_vars3;
	u8 __agg_vars2;
#elif defined(__LITTLE_ENDIAN)
	u8 __agg_vars2;
	u8 __agg_vars3;
	u8 __agg_vars4;
	u8 cdu_reserved;
#endif
	u32 __more_packets_to_send;
#if defined(__BIG_ENDIAN)
	u16 __agg_vars5;
	u16 __agg_val4_th;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val4_th;
	u16 __agg_vars5;
#endif
	struct xstorm_eth_extra_ag_context_section __extra_section;
#if defined(__BIG_ENDIAN)
	u16 __agg_vars7;
	u8 __agg_val3_th;
	u8 __agg_vars6;
#elif defined(__LITTLE_ENDIAN)
	u8 __agg_vars6;
	u8 __agg_val3_th;
	u16 __agg_vars7;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val11_th;
	u16 __agg_val11;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val11;
	u16 __agg_val11_th;
#endif
#if defined(__BIG_ENDIAN)
	u8 __reserved1;
	u8 __agg_val6_th;
	u16 __agg_val9;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val9;
	u8 __agg_val6_th;
	u8 __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val2_th;
	u16 __agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val2;
	u16 __agg_val2_th;
#endif
	u32 __agg_vars8;
#if defined(__BIG_ENDIAN)
	u16 __agg_misc0;
	u16 __agg_val4;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val4;
	u16 __agg_misc0;
#endif
#if defined(__BIG_ENDIAN)
	u8 __agg_val3;
	u8 __agg_val6;
	u8 __agg_val5_th;
	u8 __agg_val5;
#elif defined(__LITTLE_ENDIAN)
	u8 __agg_val5;
	u8 __agg_val5_th;
	u8 __agg_val6;
	u8 __agg_val3;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_misc1;
	u16 __bd_ind_max_val;
#elif defined(__LITTLE_ENDIAN)
	u16 __bd_ind_max_val;
	u16 __agg_misc1;
#endif
	u32 __reserved57;
	u32 __agg_misc4;
	u32 __agg_misc5;
};

/*
 * The eth aggregative context section of Tstorm
 */
struct tstorm_eth_extra_ag_context_section {
	u32 __agg_val1;
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars2;
	u8 __agg_val3;
	u16 __agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val2;
	u8 __agg_val3;
	u8 __tcp_agg_vars2;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val5;
	u8 __agg_val6;
	u8 __tcp_agg_vars3;
#elif defined(__LITTLE_ENDIAN)
	u8 __tcp_agg_vars3;
	u8 __agg_val6;
	u16 __agg_val5;
#endif
	u32 __reserved63;
	u32 __reserved64;
	u32 __reserved65;
	u32 __reserved66;
	u32 __reserved67;
	u32 __tcp_agg_vars1;
	u32 __reserved61;
	u32 __reserved62;
	u32 __reserved2;
};

/*
 * The eth aggregative context of Tstorm
 */
struct tstorm_eth_ag_context {
#if defined(__BIG_ENDIAN)
	u16 __reserved54;
	u8 __agg_vars1;
	u8 __state;
#elif defined(__LITTLE_ENDIAN)
	u8 __state;
	u8 __agg_vars1;
	u16 __reserved54;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val4;
	u16 __agg_vars2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_vars2;
	u16 __agg_val4;
#endif
	struct tstorm_eth_extra_ag_context_section __extra_section;
};

/*
 * The eth aggregative context of Cstorm
 */
struct cstorm_eth_ag_context {
	u32 __agg_vars1;
#if defined(__BIG_ENDIAN)
	u8 __aux1_th;
	u8 __aux1_val;
	u16 __agg_vars2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_vars2;
	u8 __aux1_val;
	u8 __aux1_th;
#endif
	u32 __num_of_treated_packet;
	u32 __last_packet_treated;
#if defined(__BIG_ENDIAN)
	u16 __reserved58;
	u16 __reserved57;
#elif defined(__LITTLE_ENDIAN)
	u16 __reserved57;
	u16 __reserved58;
#endif
#if defined(__BIG_ENDIAN)
	u8 __reserved62;
	u8 __reserved61;
	u8 __reserved60;
	u8 __reserved59;
#elif defined(__LITTLE_ENDIAN)
	u8 __reserved59;
	u8 __reserved60;
	u8 __reserved61;
	u8 __reserved62;
#endif
#if defined(__BIG_ENDIAN)
	u16 __reserved64;
	u16 __reserved63;
#elif defined(__LITTLE_ENDIAN)
	u16 __reserved63;
	u16 __reserved64;
#endif
	u32 __reserved65;
#if defined(__BIG_ENDIAN)
	u16 __agg_vars3;
	u16 __rq_inv_cnt;
#elif defined(__LITTLE_ENDIAN)
	u16 __rq_inv_cnt;
	u16 __agg_vars3;
#endif
#if defined(__BIG_ENDIAN)
	u16 __packet_index_th;
	u16 __packet_index;
#elif defined(__LITTLE_ENDIAN)
	u16 __packet_index;
	u16 __packet_index_th;
#endif
};

/*
 * The eth aggregative context of Ustorm
 */
struct ustorm_eth_ag_context {
#if defined(__BIG_ENDIAN)
	u8 __aux_counter_flags;
	u8 __agg_vars2;
	u8 __agg_vars1;
	u8 __state;
#elif defined(__LITTLE_ENDIAN)
	u8 __state;
	u8 __agg_vars1;
	u8 __agg_vars2;
	u8 __aux_counter_flags;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_usage;
	u8 __agg_misc2;
	u16 __agg_misc1;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_misc1;
	u8 __agg_misc2;
	u8 cdu_usage;
#endif
	u32 __agg_misc4;
#if defined(__BIG_ENDIAN)
	u8 __agg_val3_th;
	u8 __agg_val3;
	u16 __agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_misc3;
	u8 __agg_val3;
	u8 __agg_val3_th;
#endif
	u32 __agg_val1;
	u32 __agg_misc4_th;
#if defined(__BIG_ENDIAN)
	u16 __agg_val2_th;
	u16 __agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val2;
	u16 __agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	u16 __reserved2;
	u8 __decision_rules;
	u8 __decision_rule_enable_bits;
#elif defined(__LITTLE_ENDIAN)
	u8 __decision_rule_enable_bits;
	u8 __decision_rules;
	u16 __reserved2;
#endif
};

/*
 * Timers connection context
 */
struct timers_block_context {
	u32 __reserved_0;
	u32 __reserved_1;
	u32 __reserved_2;
	u32 __reserved_flags;
};

/*
 * structure for easy accessability to assembler
 */
struct eth_tx_bd_flags {
	u8 as_bitfield;
#define ETH_TX_BD_FLAGS_VLAN_TAG (0x1<<0)
#define ETH_TX_BD_FLAGS_VLAN_TAG_SHIFT 0
#define ETH_TX_BD_FLAGS_IP_CSUM (0x1<<1)
#define ETH_TX_BD_FLAGS_IP_CSUM_SHIFT 1
#define ETH_TX_BD_FLAGS_TCP_CSUM (0x1<<2)
#define ETH_TX_BD_FLAGS_TCP_CSUM_SHIFT 2
#define ETH_TX_BD_FLAGS_END_BD (0x1<<3)
#define ETH_TX_BD_FLAGS_END_BD_SHIFT 3
#define ETH_TX_BD_FLAGS_START_BD (0x1<<4)
#define ETH_TX_BD_FLAGS_START_BD_SHIFT 4
#define ETH_TX_BD_FLAGS_HDR_POOL (0x1<<5)
#define ETH_TX_BD_FLAGS_HDR_POOL_SHIFT 5
#define ETH_TX_BD_FLAGS_SW_LSO (0x1<<6)
#define ETH_TX_BD_FLAGS_SW_LSO_SHIFT 6
#define ETH_TX_BD_FLAGS_IPV6 (0x1<<7)
#define ETH_TX_BD_FLAGS_IPV6_SHIFT 7
};

/*
 * The eth Tx Buffer Descriptor
 */
struct eth_tx_bd {
	u32 addr_lo;
	u32 addr_hi;
	u16 nbd;
	u16 nbytes;
	u16 vlan;
	struct eth_tx_bd_flags bd_flags;
	u8 general_data;
#define ETH_TX_BD_HDR_NBDS (0x3F<<0)
#define ETH_TX_BD_HDR_NBDS_SHIFT 0
#define ETH_TX_BD_ETH_ADDR_TYPE (0x3<<6)
#define ETH_TX_BD_ETH_ADDR_TYPE_SHIFT 6
};

/*
 * Tx parsing BD structure for ETH,Relevant in START
 */
struct eth_tx_parse_bd {
	u8 global_data;
#define ETH_TX_PARSE_BD_IP_HDR_START_OFFSET (0xF<<0)
#define ETH_TX_PARSE_BD_IP_HDR_START_OFFSET_SHIFT 0
#define ETH_TX_PARSE_BD_CS_ANY_FLG (0x1<<4)
#define ETH_TX_PARSE_BD_CS_ANY_FLG_SHIFT 4
#define ETH_TX_PARSE_BD_PSEUDO_CS_WITHOUT_LEN (0x1<<5)
#define ETH_TX_PARSE_BD_PSEUDO_CS_WITHOUT_LEN_SHIFT 5
#define ETH_TX_PARSE_BD_LLC_SNAP_EN (0x1<<6)
#define ETH_TX_PARSE_BD_LLC_SNAP_EN_SHIFT 6
#define ETH_TX_PARSE_BD_NS_FLG (0x1<<7)
#define ETH_TX_PARSE_BD_NS_FLG_SHIFT 7
	u8 tcp_flags;
#define ETH_TX_PARSE_BD_FIN_FLG (0x1<<0)
#define ETH_TX_PARSE_BD_FIN_FLG_SHIFT 0
#define ETH_TX_PARSE_BD_SYN_FLG (0x1<<1)
#define ETH_TX_PARSE_BD_SYN_FLG_SHIFT 1
#define ETH_TX_PARSE_BD_RST_FLG (0x1<<2)
#define ETH_TX_PARSE_BD_RST_FLG_SHIFT 2
#define ETH_TX_PARSE_BD_PSH_FLG (0x1<<3)
#define ETH_TX_PARSE_BD_PSH_FLG_SHIFT 3
#define ETH_TX_PARSE_BD_ACK_FLG (0x1<<4)
#define ETH_TX_PARSE_BD_ACK_FLG_SHIFT 4
#define ETH_TX_PARSE_BD_URG_FLG (0x1<<5)
#define ETH_TX_PARSE_BD_URG_FLG_SHIFT 5
#define ETH_TX_PARSE_BD_ECE_FLG (0x1<<6)
#define ETH_TX_PARSE_BD_ECE_FLG_SHIFT 6
#define ETH_TX_PARSE_BD_CWR_FLG (0x1<<7)
#define ETH_TX_PARSE_BD_CWR_FLG_SHIFT 7
	u8 ip_hlen;
	s8 cs_offset;
	u16 total_hlen;
	u16 lso_mss;
	u16 tcp_pseudo_csum;
	u16 ip_id;
	u32 tcp_send_seq;
};

/*
 * The last BD in the BD memory will hold a pointer to the next BD memory
 */
struct eth_tx_next_bd {
	u32 addr_lo;
	u32 addr_hi;
	u8 reserved[8];
};

/*
 * union for 3 Bd types
 */
union eth_tx_bd_types {
	struct eth_tx_bd reg_bd;
	struct eth_tx_parse_bd parse_bd;
	struct eth_tx_next_bd next_bd;
};

/*
 * The eth storm context of Xstorm
 */
struct xstorm_eth_st_context {
	u32 tx_bd_page_base_lo;
	u32 tx_bd_page_base_hi;
#if defined(__BIG_ENDIAN)
	u16 tx_bd_cons;
	u8 __reserved0;
	u8 __local_tx_bd_prod;
#elif defined(__LITTLE_ENDIAN)
	u8 __local_tx_bd_prod;
	u8 __reserved0;
	u16 tx_bd_cons;
#endif
	u32 db_data_addr_lo;
	u32 db_data_addr_hi;
	u32 __pkt_cons;
	u32 __gso_next;
	u32 is_eth_conn_1b;
	union eth_tx_bd_types __bds[13];
};

/*
 * The eth storm context of Cstorm
 */
struct cstorm_eth_st_context {
#if defined(__BIG_ENDIAN)
	u16 __reserved0;
	u8 sb_index_number;
	u8 status_block_id;
#elif defined(__LITTLE_ENDIAN)
	u8 status_block_id;
	u8 sb_index_number;
	u16 __reserved0;
#endif
	u32 __reserved1[3];
};

/*
 * Ethernet connection context
 */
struct eth_context {
	struct ustorm_eth_st_context ustorm_st_context;
	struct tstorm_eth_st_context tstorm_st_context;
	struct xstorm_eth_ag_context xstorm_ag_context;
	struct tstorm_eth_ag_context tstorm_ag_context;
	struct cstorm_eth_ag_context cstorm_ag_context;
	struct ustorm_eth_ag_context ustorm_ag_context;
	struct timers_block_context timers_context;
	struct xstorm_eth_st_context xstorm_st_context;
	struct cstorm_eth_st_context cstorm_st_context;
};


/*
 * ethernet doorbell
 */
struct eth_tx_doorbell {
#if defined(__BIG_ENDIAN)
	u16 npackets;
	u8 params;
#define ETH_TX_DOORBELL_NUM_BDS (0x3F<<0)
#define ETH_TX_DOORBELL_NUM_BDS_SHIFT 0
#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG (0x1<<6)
#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT 6
#define ETH_TX_DOORBELL_SPARE (0x1<<7)
#define ETH_TX_DOORBELL_SPARE_SHIFT 7
	struct doorbell_hdr hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr hdr;
	u8 params;
#define ETH_TX_DOORBELL_NUM_BDS (0x3F<<0)
#define ETH_TX_DOORBELL_NUM_BDS_SHIFT 0
#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG (0x1<<6)
#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT 6
#define ETH_TX_DOORBELL_SPARE (0x1<<7)
#define ETH_TX_DOORBELL_SPARE_SHIFT 7
	u16 npackets;
#endif
};


/*
 * ustorm status block
 */
struct ustorm_def_status_block {
	u16 index_values[HC_USTORM_DEF_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * cstorm status block
 */
struct cstorm_def_status_block {
	u16 index_values[HC_CSTORM_DEF_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * xstorm status block
 */
struct xstorm_def_status_block {
	u16 index_values[HC_XSTORM_DEF_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * tstorm status block
 */
struct tstorm_def_status_block {
	u16 index_values[HC_TSTORM_DEF_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * host status block
 */
struct host_def_status_block {
	struct atten_def_status_block atten_status_block;
	struct ustorm_def_status_block u_def_status_block;
	struct cstorm_def_status_block c_def_status_block;
	struct xstorm_def_status_block x_def_status_block;
	struct tstorm_def_status_block t_def_status_block;
};


/*
 * ustorm status block
 */
struct ustorm_status_block {
	u16 index_values[HC_USTORM_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * cstorm status block
 */
struct cstorm_status_block {
	u16 index_values[HC_CSTORM_SB_NUM_INDICES];
	u16 status_block_index;
	u8 reserved0;
	u8 status_block_id;
	u32 __flags;
};

/*
 * host status block
 */
struct host_status_block {
	struct ustorm_status_block u_status_block;
	struct cstorm_status_block c_status_block;
};


/*
 * The data for RSS setup ramrod
 */
struct eth_client_setup_ramrod_data {
	u32 client_id_5b;
	u8 is_rdma_1b;
	u8 reserved0;
	u16 reserved1;
};


/*
 * L2 dynamic host coalescing init parameters
 */
struct eth_dynamic_hc_config {
	u32 threshold[3];
	u8 hc_timeout[4];
};


/*
 * regular eth FP CQE parameters struct
 */
struct eth_fast_path_rx_cqe {
	u8 type;
	u8 error_type_flags;
#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG (0x1<<0)
#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG_SHIFT 0
#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG (0x1<<1)
#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG_SHIFT 1
#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG (0x1<<2)
#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG_SHIFT 2
#define ETH_FAST_PATH_RX_CQE_START_FLG (0x1<<3)
#define ETH_FAST_PATH_RX_CQE_START_FLG_SHIFT 3
#define ETH_FAST_PATH_RX_CQE_END_FLG (0x1<<4)
#define ETH_FAST_PATH_RX_CQE_END_FLG_SHIFT 4
#define ETH_FAST_PATH_RX_CQE_RESERVED0 (0x7<<5)
#define ETH_FAST_PATH_RX_CQE_RESERVED0_SHIFT 5
	u8 status_flags;
#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE (0x7<<0)
#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE_SHIFT 0
#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG (0x1<<3)
#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG_SHIFT 3
#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG (0x1<<4)
#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG_SHIFT 4
#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG (0x1<<5)
#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG_SHIFT 5
#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG (0x1<<6)
#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG_SHIFT 6
#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG (0x1<<7)
#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG_SHIFT 7
	u8 placement_offset;
	u32 rss_hash_result;
	u16 vlan_tag;
	u16 pkt_len;
	u16 queue_index;
	struct parsing_flags pars_flags;
};


/*
 * The data for RSS setup ramrod
 */
struct eth_halt_ramrod_data {
	u32 client_id_5b;
	u32 reserved0;
};


/*
 * Place holder for ramrods protocol specific data
 */
struct ramrod_data {
	u32 data_lo;
	u32 data_hi;
};

/*
 * union for ramrod data for ethernet protocol (CQE) (force size of 16 bits)
 */
union eth_ramrod_data {
	struct ramrod_data general;
};


/*
 * Rx Last BD in page (in ETH)
 */
struct eth_rx_bd_next_page {
	u32 addr_lo;
	u32 addr_hi;
	u8 reserved[8];
};


/*
 * Eth Rx Cqe structure- general structure for ramrods
 */
struct common_ramrod_eth_rx_cqe {
	u8 type;
	u8 conn_type_3b;
	u16 reserved;
	u32 conn_and_cmd_data;
#define COMMON_RAMROD_ETH_RX_CQE_CID (0xFFFFFF<<0)
#define COMMON_RAMROD_ETH_RX_CQE_CID_SHIFT 0
#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID (0xFF<<24)
#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT 24
	struct ramrod_data protocol_data;
};

/*
 * Rx Last CQE in page (in ETH)
 */
struct eth_rx_cqe_next_page {
	u32 addr_lo;
	u32 addr_hi;
	u32 reserved0;
	u32 reserved1;
};

/*
 * union for all eth rx cqe types (fix their sizes)
 */
union eth_rx_cqe {
	struct eth_fast_path_rx_cqe fast_path_cqe;
	struct common_ramrod_eth_rx_cqe ramrod_cqe;
	struct eth_rx_cqe_next_page next_page_cqe;
};


/*
 * common data for all protocols
 */
struct spe_hdr {
	u32 conn_and_cmd_data;
#define SPE_HDR_CID (0xFFFFFF<<0)
#define SPE_HDR_CID_SHIFT 0
#define SPE_HDR_CMD_ID (0xFF<<24)
#define SPE_HDR_CMD_ID_SHIFT 24
	u16 type;
#define SPE_HDR_CONN_TYPE (0xFF<<0)
#define SPE_HDR_CONN_TYPE_SHIFT 0
#define SPE_HDR_COMMON_RAMROD (0xFF<<8)
#define SPE_HDR_COMMON_RAMROD_SHIFT 8
	u16 reserved;
};

struct regpair {
	u32 lo;
	u32 hi;
};

/*
 * ethernet slow path element
 */
union eth_specific_data {
	u8 protocol_data[8];
	struct regpair mac_config_addr;
	struct eth_client_setup_ramrod_data client_setup_ramrod_data;
	struct eth_halt_ramrod_data halt_ramrod_data;
	struct regpair leading_cqe_addr;
	struct regpair update_data_addr;
};

/*
 * ethernet slow path element
 */
struct eth_spe {
	struct spe_hdr hdr;
	union eth_specific_data data;
};


/*
 * doorbell data in host memory
 */
struct eth_tx_db_data {
	u32 packets_prod;
	u16 bds_prod;
	u16 reserved;
};


/*
 * Common configuration parameters per port in Tstorm
 */
struct tstorm_eth_function_common_config {
	u32 config_flags;
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY (0x1<<0)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY_SHIFT 0
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY (0x1<<1)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY_SHIFT 1
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY (0x1<<2)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY_SHIFT 2
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY (0x1<<3)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY_SHIFT 3
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_ENABLE (0x1<<4)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_ENABLE_SHIFT 4
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_DEFAULT_ENABLE (0x1<<5)
#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_DEFAULT_ENABLE_SHIFT 5
#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0 (0x3FFFFFF<<6)
#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0_SHIFT 6
#if defined(__BIG_ENDIAN)
	u16 __secondary_vlan_id;
	u8 leading_client_id;
	u8 rss_result_mask;
#elif defined(__LITTLE_ENDIAN)
	u8 rss_result_mask;
	u8 leading_client_id;
	u16 __secondary_vlan_id;
#endif
};

/*
 * parameters for eth update ramrod
 */
struct eth_update_ramrod_data {
	struct tstorm_eth_function_common_config func_config;
	u8 indirectionTable[128];
};


/*
 * MAC filtering configuration command header
 */
struct mac_configuration_hdr {
	u8 length_6b;
	u8 offset;
	u16 reserved0;
	u32 reserved1;
};

/*
 * MAC address in list for ramrod
 */
struct tstorm_cam_entry {
	u16 lsb_mac_addr;
	u16 middle_mac_addr;
	u16 msb_mac_addr;
	u16 flags;
#define TSTORM_CAM_ENTRY_PORT_ID (0x1<<0)
#define TSTORM_CAM_ENTRY_PORT_ID_SHIFT 0
#define TSTORM_CAM_ENTRY_RSRVVAL0 (0x7<<1)
#define TSTORM_CAM_ENTRY_RSRVVAL0_SHIFT 1
#define TSTORM_CAM_ENTRY_RESERVED0 (0xFFF<<4)
#define TSTORM_CAM_ENTRY_RESERVED0_SHIFT 4
};

/*
 * MAC filtering: CAM target table entry
 */
struct tstorm_cam_target_table_entry {
	u8 flags;
#define TSTORM_CAM_TARGET_TABLE_ENTRY_BROADCAST (0x1<<0)
#define TSTORM_CAM_TARGET_TABLE_ENTRY_BROADCAST_SHIFT 0
#define TSTORM_CAM_TARGET_TABLE_ENTRY_OVERRIDE_VLAN_REMOVAL (0x1<<1)
#define TSTORM_CAM_TARGET_TABLE_ENTRY_OVERRIDE_VLAN_REMOVAL_SHIFT 1
#define TSTORM_CAM_TARGET_TABLE_ENTRY_ACTION_TYPE (0x1<<2)
#define TSTORM_CAM_TARGET_TABLE_ENTRY_ACTION_TYPE_SHIFT 2
#define TSTORM_CAM_TARGET_TABLE_ENTRY_RDMA_MAC (0x1<<3)
#define TSTORM_CAM_TARGET_TABLE_ENTRY_RDMA_MAC_SHIFT 3
#define TSTORM_CAM_TARGET_TABLE_ENTRY_RESERVED0 (0xF<<4)
#define TSTORM_CAM_TARGET_TABLE_ENTRY_RESERVED0_SHIFT 4
	u8 client_id;
	u16 vlan_id;
};

/*
 * MAC address in list for ramrod
 */
struct mac_configuration_entry {
	struct tstorm_cam_entry cam_entry;
	struct tstorm_cam_target_table_entry target_table_entry;
};

/*
 * MAC filtering configuration command
 */
struct mac_configuration_cmd {
	struct mac_configuration_hdr hdr;
	struct mac_configuration_entry config_table[64];
};


/*
 * Configuration parameters per client in Tstorm
 */
struct tstorm_eth_client_config {
#if defined(__BIG_ENDIAN)
	u16 statistics_counter_id;
	u16 mtu;
#elif defined(__LITTLE_ENDIAN)
	u16 mtu;
	u16 statistics_counter_id;
#endif
#if defined(__BIG_ENDIAN)
	u16 drop_flags;
#define TSTORM_ETH_CLIENT_CONFIG_DROP_IP_CS_ERR (0x1<<0)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_IP_CS_ERR_SHIFT 0
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TCP_CS_ERR (0x1<<1)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TCP_CS_ERR_SHIFT 1
#define TSTORM_ETH_CLIENT_CONFIG_DROP_MAC_ERR (0x1<<2)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_MAC_ERR_SHIFT 2
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TTL0 (0x1<<3)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TTL0_SHIFT 3
#define TSTORM_ETH_CLIENT_CONFIG_DROP_UDP_CS_ERR (0x1<<4)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_UDP_CS_ERR_SHIFT 4
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED1 (0x7FF<<5)
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED1_SHIFT 5
	u16 config_flags;
#define TSTORM_ETH_CLIENT_CONFIG_VLAN_REMOVAL_ENABLE (0x1<<0)
#define TSTORM_ETH_CLIENT_CONFIG_VLAN_REMOVAL_ENABLE_SHIFT 0
#define TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE (0x1<<1)
#define TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE_SHIFT 1
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED0 (0x3FFF<<2)
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED0_SHIFT 2
#elif defined(__LITTLE_ENDIAN)
	u16 config_flags;
#define TSTORM_ETH_CLIENT_CONFIG_VLAN_REMOVAL_ENABLE (0x1<<0)
#define TSTORM_ETH_CLIENT_CONFIG_VLAN_REMOVAL_ENABLE_SHIFT 0
#define TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE (0x1<<1)
#define TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE_SHIFT 1
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED0 (0x3FFF<<2)
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED0_SHIFT 2
	u16 drop_flags;
#define TSTORM_ETH_CLIENT_CONFIG_DROP_IP_CS_ERR (0x1<<0)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_IP_CS_ERR_SHIFT 0
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TCP_CS_ERR (0x1<<1)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TCP_CS_ERR_SHIFT 1
#define TSTORM_ETH_CLIENT_CONFIG_DROP_MAC_ERR (0x1<<2)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_MAC_ERR_SHIFT 2
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TTL0 (0x1<<3)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_TTL0_SHIFT 3
#define TSTORM_ETH_CLIENT_CONFIG_DROP_UDP_CS_ERR (0x1<<4)
#define TSTORM_ETH_CLIENT_CONFIG_DROP_UDP_CS_ERR_SHIFT 4
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED1 (0x7FF<<5)
#define __TSTORM_ETH_CLIENT_CONFIG_RESERVED1_SHIFT 5
#endif
};


/*
 * MAC filtering configuration parameters per port in Tstorm
 */
struct tstorm_eth_mac_filter_config {
	u32 ucast_drop_all;
	u32 ucast_accept_all;
	u32 mcast_drop_all;
	u32 mcast_accept_all;
	u32 bcast_drop_all;
	u32 bcast_accept_all;
	u32 strict_vlan;
	u32 __secondary_vlan_clients;
};


struct rate_shaping_per_protocol {
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u16 protocol_rate;
#elif defined(__LITTLE_ENDIAN)
	u16 protocol_rate;
	u16 reserved0;
#endif
	u32 protocol_quota;
	s32 current_credit;
	u32 reserved;
};

struct rate_shaping_vars {
	struct rate_shaping_per_protocol protocol_vars[NUM_OF_PROTOCOLS];
	u32 pause_mask;
	u32 periodic_stop;
	u32 rs_periodic_timeout;
	u32 rs_threshold;
	u32 last_periodic_time;
	u32 reserved;
};

struct fairness_per_protocol {
	u32 credit_delta;
	s32 fair_credit;
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u8 state;
	u8 weight;
#elif defined(__LITTLE_ENDIAN)
	u8 weight;
	u8 state;
	u16 reserved0;
#endif
	u32 reserved1;
};

struct fairness_vars {
	struct fairness_per_protocol protocol_vars[NUM_OF_PROTOCOLS];
	u32 upper_bound;
	u32 port_rate;
	u32 pause_mask;
	u32 fair_threshold;
};

struct safc_struct {
	u32 cur_pause_mask;
	u32 expire_time;
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u8 cur_cos_types;
	u8 safc_timeout_usec;
#elif defined(__LITTLE_ENDIAN)
	u8 safc_timeout_usec;
	u8 cur_cos_types;
	u16 reserved0;
#endif
	u32 reserved1;
};

struct demo_struct {
	u8 con_number[NUM_OF_PROTOCOLS];
#if defined(__BIG_ENDIAN)
	u8 reserved1;
	u8 fairness_enable;
	u8 rate_shaping_enable;
	u8 cmng_enable;
#elif defined(__LITTLE_ENDIAN)
	u8 cmng_enable;
	u8 rate_shaping_enable;
	u8 fairness_enable;
	u8 reserved1;
#endif
};

struct cmng_struct {
	struct rate_shaping_vars rs_vars;
	struct fairness_vars fair_vars;
	struct safc_struct safc_vars;
	struct demo_struct demo_vars;
};


struct cos_to_protocol {
	u8 mask[MAX_COS_NUMBER];
};


/*
 * Common statistics collected by the Xstorm (per port)
 */
struct xstorm_common_stats {
	struct regpair total_sent_bytes;
	u32 total_sent_pkts;
	u32 unicast_pkts_sent;
	struct regpair unicast_bytes_sent;
	struct regpair multicast_bytes_sent;
	u32 multicast_pkts_sent;
	u32 broadcast_pkts_sent;
	struct regpair broadcast_bytes_sent;
	struct regpair done;
};

/*
 * Protocol-common statistics collected by the Tstorm (per client)
 */
struct tstorm_per_client_stats {
	struct regpair total_rcv_bytes;
	struct regpair rcv_unicast_bytes;
	struct regpair rcv_broadcast_bytes;
	struct regpair rcv_multicast_bytes;
	struct regpair rcv_error_bytes;
	u32 checksum_discard;
	u32 packets_too_big_discard;
	u32 total_rcv_pkts;
	u32 rcv_unicast_pkts;
	u32 rcv_broadcast_pkts;
	u32 rcv_multicast_pkts;
	u32 no_buff_discard;
	u32 ttl0_discard;
	u32 mac_discard;
	u32 reserved;
};

/*
 * Protocol-common statistics collected by the Tstorm (per port)
 */
struct tstorm_common_stats {
	struct tstorm_per_client_stats client_statistics[MAX_T_STAT_COUNTER_ID];
	u32 mac_filter_discard;
	u32 xxoverflow_discard;
	u32 brb_truncate_discard;
	u32 reserved;
	struct regpair done;
};

/*
 * Eth statistics query sturcture for the eth_stats_quesry ramrod
 */
struct eth_stats_query {
	struct xstorm_common_stats xstorm_common;
	struct tstorm_common_stats tstorm_common;
};


/*
 * FW version stored in the Xstorm RAM
 */
struct fw_version {
#if defined(__BIG_ENDIAN)
	u16 patch;
	u8 primary;
	u8 client;
#elif defined(__LITTLE_ENDIAN)
	u8 client;
	u8 primary;
	u16 patch;
#endif
	u32 flags;
#define FW_VERSION_OPTIMIZED (0x1<<0)
#define FW_VERSION_OPTIMIZED_SHIFT 0
#define FW_VERSION_BIG_ENDIEN (0x1<<1)
#define FW_VERSION_BIG_ENDIEN_SHIFT 1
#define __FW_VERSION_RESERVED (0x3FFFFFFF<<2)
#define __FW_VERSION_RESERVED_SHIFT 2
};


/*
 * FW version stored in first line of pram
 */
struct pram_fw_version {
#if defined(__BIG_ENDIAN)
	u16 patch;
	u8 primary;
	u8 client;
#elif defined(__LITTLE_ENDIAN)
	u8 client;
	u8 primary;
	u16 patch;
#endif
	u8 flags;
#define PRAM_FW_VERSION_OPTIMIZED (0x1<<0)
#define PRAM_FW_VERSION_OPTIMIZED_SHIFT 0
#define PRAM_FW_VERSION_STORM_ID (0x3<<1)
#define PRAM_FW_VERSION_STORM_ID_SHIFT 1
#define PRAM_FW_VERSION_BIG_ENDIEN (0x1<<3)
#define PRAM_FW_VERSION_BIG_ENDIEN_SHIFT 3
#define __PRAM_FW_VERSION_RESERVED0 (0xF<<4)
#define __PRAM_FW_VERSION_RESERVED0_SHIFT 4
};


/*
 * The send queue element
 */
struct slow_path_element {
	struct spe_hdr hdr;
	u8 protocol_data[8];
};


/*
 * eth/toe flags that indicate if to query
 */
struct stats_indication_flags {
	u32 collect_eth;
	u32 collect_toe;
};


