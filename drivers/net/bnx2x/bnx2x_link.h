/* Copyright 2008-2010 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Written by Yaniv Rosner
 *
 */

#ifndef BNX2X_LINK_H
#define BNX2X_LINK_H



/***********************************************************/
/*                         Defines                         */
/***********************************************************/
#define DEFAULT_PHY_DEV_ADDR	3
#define E2_DEFAULT_PHY_DEV_ADDR	5



#define BNX2X_FLOW_CTRL_AUTO		PORT_FEATURE_FLOW_CONTROL_AUTO
#define BNX2X_FLOW_CTRL_TX		PORT_FEATURE_FLOW_CONTROL_TX
#define BNX2X_FLOW_CTRL_RX		PORT_FEATURE_FLOW_CONTROL_RX
#define BNX2X_FLOW_CTRL_BOTH		PORT_FEATURE_FLOW_CONTROL_BOTH
#define BNX2X_FLOW_CTRL_NONE		PORT_FEATURE_FLOW_CONTROL_NONE

#define SPEED_AUTO_NEG	    0
#define SPEED_12000		12000
#define SPEED_12500		12500
#define SPEED_13000		13000
#define SPEED_15000		15000
#define SPEED_16000		16000

#define SFP_EEPROM_VENDOR_NAME_ADDR		0x14
#define SFP_EEPROM_VENDOR_NAME_SIZE		16
#define SFP_EEPROM_VENDOR_OUI_ADDR		0x25
#define SFP_EEPROM_VENDOR_OUI_SIZE		3
#define SFP_EEPROM_PART_NO_ADDR 		0x28
#define SFP_EEPROM_PART_NO_SIZE		16
#define PWR_FLT_ERR_MSG_LEN			250

#define XGXS_EXT_PHY_TYPE(ext_phy_config) \
		((ext_phy_config) & PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK)
#define XGXS_EXT_PHY_ADDR(ext_phy_config) \
		(((ext_phy_config) & PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >> \
		 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT)
#define SERDES_EXT_PHY_TYPE(ext_phy_config) \
		((ext_phy_config) & PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK)

/* Single Media Direct board is the plain 577xx board with CX4/RJ45 jacks */
#define SINGLE_MEDIA_DIRECT(params)	(params->num_phys == 1)
/* Single Media board contains single external phy */
#define SINGLE_MEDIA(params)		(params->num_phys == 2)
/* Dual Media board contains two external phy with different media */
#define DUAL_MEDIA(params)		(params->num_phys == 3)
#define FW_PARAM_MDIO_CTRL_OFFSET 16
#define FW_PARAM_SET(phy_addr, phy_type, mdio_access) \
	(phy_addr | phy_type | mdio_access << FW_PARAM_MDIO_CTRL_OFFSET)

#define PFC_BRB_MAC_PAUSE_XOFF_THRESHOLD_PAUSEABLE		170
#define PFC_BRB_MAC_PAUSE_XOFF_THRESHOLD_NON_PAUSEABLE		0

#define PFC_BRB_MAC_PAUSE_XON_THRESHOLD_PAUSEABLE			250
#define PFC_BRB_MAC_PAUSE_XON_THRESHOLD_NON_PAUSEABLE		0

#define PFC_BRB_MAC_FULL_XOFF_THRESHOLD_PAUSEABLE			10
#define PFC_BRB_MAC_FULL_XOFF_THRESHOLD_NON_PAUSEABLE		90

#define PFC_BRB_MAC_FULL_XON_THRESHOLD_PAUSEABLE			50
#define PFC_BRB_MAC_FULL_XON_THRESHOLD_NON_PAUSEABLE		250

#define PFC_BRB_FULL_LB_XOFF_THRESHOLD				170
#define PFC_BRB_FULL_LB_XON_THRESHOLD				250

/***********************************************************/
/*                         Structs                         */
/***********************************************************/
#define INT_PHY		0
#define EXT_PHY1	1
#define EXT_PHY2	2
#define MAX_PHYS	3

/* Same configuration is shared between the XGXS and the first external phy */
#define LINK_CONFIG_SIZE (MAX_PHYS - 1)
#define LINK_CONFIG_IDX(_phy_idx) ((_phy_idx == INT_PHY) ? \
					 0 : (_phy_idx - 1))
/***********************************************************/
/*                      bnx2x_phy struct                     */
/*  Defines the required arguments and function per phy    */
/***********************************************************/
struct link_vars;
struct link_params;
struct bnx2x_phy;

typedef u8 (*config_init_t)(struct bnx2x_phy *phy, struct link_params *params,
			    struct link_vars *vars);
typedef u8 (*read_status_t)(struct bnx2x_phy *phy, struct link_params *params,
			    struct link_vars *vars);
typedef void (*link_reset_t)(struct bnx2x_phy *phy,
			     struct link_params *params);
typedef void (*config_loopback_t)(struct bnx2x_phy *phy,
				  struct link_params *params);
typedef u8 (*format_fw_ver_t)(u32 raw, u8 *str, u16 *len);
typedef void (*hw_reset_t)(struct bnx2x_phy *phy, struct link_params *params);
typedef void (*set_link_led_t)(struct bnx2x_phy *phy,
			       struct link_params *params, u8 mode);
typedef void (*phy_specific_func_t)(struct bnx2x_phy *phy,
				    struct link_params *params, u32 action);

struct bnx2x_phy {
	u32 type;

	/* Loaded during init */
	u8 addr;

	u8 flags;
	/* Require HW lock */
#define FLAGS_HW_LOCK_REQUIRED		(1<<0)
	/* No Over-Current detection */
#define FLAGS_NOC			(1<<1)
	/* Fan failure detection required */
#define FLAGS_FAN_FAILURE_DET_REQ	(1<<2)
	/* Initialize first the XGXS and only then the phy itself */
#define FLAGS_INIT_XGXS_FIRST		(1<<3)
#define FLAGS_REARM_LATCH_SIGNAL	(1<<6)
#define FLAGS_SFP_NOT_APPROVED		(1<<7)

	u8 def_md_devad;
	u8 reserved;
	/* preemphasis values for the rx side */
	u16 rx_preemphasis[4];

	/* preemphasis values for the tx side */
	u16 tx_preemphasis[4];

	/* EMAC address for access MDIO */
	u32 mdio_ctrl;

	u32 supported;

	u32 media_type;
#define	ETH_PHY_UNSPECIFIED 0x0
#define	ETH_PHY_SFP_FIBER   0x1
#define	ETH_PHY_XFP_FIBER   0x2
#define	ETH_PHY_DA_TWINAX   0x3
#define	ETH_PHY_BASE_T      0x4
#define	ETH_PHY_NOT_PRESENT 0xff

	/* The address in which version is located*/
	u32 ver_addr;

	u16 req_flow_ctrl;

	u16 req_line_speed;

	u32 speed_cap_mask;

	u16 req_duplex;
	u16 rsrv;
	/* Called per phy/port init, and it configures LASI, speed, autoneg,
	 duplex, flow control negotiation, etc. */
	config_init_t config_init;

	/* Called due to interrupt. It determines the link, speed */
	read_status_t read_status;

	/* Called when driver is unloading. Should reset the phy */
	link_reset_t link_reset;

	/* Set the loopback configuration for the phy */
	config_loopback_t config_loopback;

	/* Format the given raw number into str up to len */
	format_fw_ver_t format_fw_ver;

	/* Reset the phy (both ports) */
	hw_reset_t hw_reset;

	/* Set link led mode (on/off/oper)*/
	set_link_led_t set_link_led;

	/* PHY Specific tasks */
	phy_specific_func_t phy_specific_func;
#define DISABLE_TX	1
#define ENABLE_TX	2
};

/* Inputs parameters to the CLC */
struct link_params {

	u8 port;

	/* Default / User Configuration */
	u8 loopback_mode;
#define LOOPBACK_NONE	0
#define LOOPBACK_EMAC	1
#define LOOPBACK_BMAC	2
#define LOOPBACK_XGXS		3
#define LOOPBACK_EXT_PHY	4
#define LOOPBACK_EXT 	5

	/* Device parameters */
	u8 mac_addr[6];

	u16 req_duplex[LINK_CONFIG_SIZE];
	u16 req_flow_ctrl[LINK_CONFIG_SIZE];

	u16 req_line_speed[LINK_CONFIG_SIZE]; /* Also determine AutoNeg */

	/* shmem parameters */
	u32 shmem_base;
	u32 shmem2_base;
	u32 speed_cap_mask[LINK_CONFIG_SIZE];
	u32 switch_cfg;
#define SWITCH_CFG_1G		PORT_FEATURE_CON_SWITCH_1G_SWITCH
#define SWITCH_CFG_10G		PORT_FEATURE_CON_SWITCH_10G_SWITCH
#define SWITCH_CFG_AUTO_DETECT	PORT_FEATURE_CON_SWITCH_AUTO_DETECT

	u32 lane_config;

	/* Phy register parameter */
	u32 chip_id;

	u32 feature_config_flags;
#define FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED (1<<0)
#define FEATURE_CONFIG_PFC_ENABLED		(1<<1)
#define FEATURE_CONFIG_BC_SUPPORTS_OPT_MDL_VRFY	(1<<2)
#define FEATURE_CONFIG_BC_SUPPORTS_DUAL_PHY_OPT_MDL_VRFY	(1<<3)
	/* Will be populated during common init */
	struct bnx2x_phy phy[MAX_PHYS];

	/* Will be populated during common init */
	u8 num_phys;

	u8 rsrv;
	u16 hw_led_mode; /* part of the hw_config read from the shmem */
	u32 multi_phy_config;

	/* Device pointer passed to all callback functions */
	struct bnx2x *bp;
	u16 req_fc_auto_adv; /* Should be set to TX / BOTH when
				req_flow_ctrl is set to AUTO */
};

/* Output parameters */
struct link_vars {
	u8 phy_flags;

	u8 mac_type;
#define MAC_TYPE_NONE		0
#define MAC_TYPE_EMAC		1
#define MAC_TYPE_BMAC		2

	u8 phy_link_up; /* internal phy link indication */
	u8 link_up;

	u16 line_speed;
	u16 duplex;

	u16 flow_ctrl;
	u16 ieee_fc;

	/* The same definitions as the shmem parameter */
	u32 link_status;
};

/***********************************************************/
/*                         Functions                       */
/***********************************************************/
u8 bnx2x_phy_init(struct link_params *input, struct link_vars *output);

/* Reset the link. Should be called when driver or interface goes down
   Before calling phy firmware upgrade, the reset_ext_phy should be set
   to 0 */
u8 bnx2x_link_reset(struct link_params *params, struct link_vars *vars,
		  u8 reset_ext_phy);

/* bnx2x_link_update should be called upon link interrupt */
u8 bnx2x_link_update(struct link_params *input, struct link_vars *output);

/* use the following phy functions to read/write from external_phy
  In order to use it to read/write internal phy registers, use
  DEFAULT_PHY_DEV_ADDR as devad, and (_bank + (_addr & 0xf)) as
  the register */
u8 bnx2x_phy_read(struct link_params *params, u8 phy_addr,
		  u8 devad, u16 reg, u16 *ret_val);

u8 bnx2x_phy_write(struct link_params *params, u8 phy_addr,
		   u8 devad, u16 reg, u16 val);
/* Reads the link_status from the shmem,
   and update the link vars accordingly */
void bnx2x_link_status_update(struct link_params *input,
			    struct link_vars *output);
/* returns string representing the fw_version of the external phy */
u8 bnx2x_get_ext_phy_fw_version(struct link_params *params, u8 driver_loaded,
			      u8 *version, u16 len);

/* Set/Unset the led
   Basically, the CLC takes care of the led for the link, but in case one needs
   to set/unset the led unnaturally, set the "mode" to LED_MODE_OPER to
   blink the led, and LED_MODE_OFF to set the led off.*/
u8 bnx2x_set_led(struct link_params *params, struct link_vars *vars,
		 u8 mode, u32 speed);
#define LED_MODE_OFF			0
#define LED_MODE_ON			1
#define LED_MODE_OPER			2
#define LED_MODE_FRONT_PANEL_OFF	3

/* bnx2x_handle_module_detect_int should be called upon module detection
   interrupt */
void bnx2x_handle_module_detect_int(struct link_params *params);

/* Get the actual link status. In case it returns 0, link is up,
	otherwise link is down*/
u8 bnx2x_test_link(struct link_params *input, struct link_vars *vars,
		   u8 is_serdes);

/* One-time initialization for external phy after power up */
u8 bnx2x_common_init_phy(struct bnx2x *bp, u32 shmem_base_path[],
			 u32 shmem2_base_path[], u32 chip_id);

/* Reset the external PHY using GPIO */
void bnx2x_ext_phy_hw_reset(struct bnx2x *bp, u8 port);

/* Reset the external of SFX7101 */
void bnx2x_sfx7101_sp_sw_reset(struct bnx2x *bp, struct bnx2x_phy *phy);

void bnx2x_hw_reset_phy(struct link_params *params);

/* Checks if HW lock is required for this phy/board type */
u8 bnx2x_hw_lock_required(struct bnx2x *bp, u32 shmem_base,
			  u32 shmem2_base);

/* Check swap bit and adjust PHY order */
u32 bnx2x_phy_selection(struct link_params *params);

/* Probe the phys on board, and populate them in "params" */
u8 bnx2x_phy_probe(struct link_params *params);
/* Checks if fan failure detection is required on one of the phys on board */
u8 bnx2x_fan_failure_det_req(struct bnx2x *bp, u32 shmem_base,
			     u32 shmem2_base, u8 port);

/* PFC port configuration params */
struct bnx2x_nig_brb_pfc_port_params {
	/* NIG */
	u32 pause_enable;
	u32 llfc_out_en;
	u32 llfc_enable;
	u32 pkt_priority_to_cos;
	u32 rx_cos0_priority_mask;
	u32 rx_cos1_priority_mask;
	u32 llfc_high_priority_classes;
	u32 llfc_low_priority_classes;
	/* BRB */
	u32 cos0_pauseable;
	u32 cos1_pauseable;
};

/**
 * Used to update the PFC attributes in EMAC, BMAC, NIG and BRB
 * when link is already up
 */
void bnx2x_update_pfc(struct link_params *params,
		      struct link_vars *vars,
		      struct bnx2x_nig_brb_pfc_port_params *pfc_params);


/* Used to configure the ETS to disable */
void bnx2x_ets_disabled(struct link_params *params);

/* Used to configure the ETS to BW limited */
void bnx2x_ets_bw_limit(const struct link_params *params, const u32 cos0_bw,
						const u32 cos1_bw);

/* Used to configure the ETS to strict */
u8 bnx2x_ets_strict(const struct link_params *params, const u8 strict_cos);

/* Read pfc statistic*/
void bnx2x_pfc_statistic(struct link_params *params, struct link_vars *vars,
						 u32 pfc_frames_sent[2],
						 u32 pfc_frames_received[2]);
#endif /* BNX2X_LINK_H */
