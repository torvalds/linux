/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IXGBE_TYPE_E610_H_
#define _IXGBE_TYPE_E610_H_

#include <linux/net/intel/libie/adminq.h>

#define BYTES_PER_DWORD	4

/* General E610 defines */
#define IXGBE_MAX_VSI			768

/* Checksum and Shadow RAM pointers */
#define IXGBE_E610_SR_NVM_CTRL_WORD		0x00
#define IXGBE_E610_SR_PBA_BLOCK_PTR		0x16
#define IXGBE_E610_SR_PBA_BLOCK_MASK		GENMASK(15, 8)
#define IXGBE_E610_SR_NVM_DEV_STARTER_VER	0x18
#define IXGBE_E610_SR_NVM_EETRACK_LO		0x2D
#define IXGBE_E610_SR_NVM_EETRACK_HI		0x2E
#define IXGBE_E610_NVM_VER_LO_MASK		GENMASK(7, 0)
#define IXGBE_E610_NVM_VER_HI_MASK		GENMASK(15, 12)
#define IXGBE_E610_SR_SW_CHECKSUM_WORD		0x3F
#define IXGBE_E610_SR_PFA_PTR			0x40
#define IXGBE_E610_SR_1ST_NVM_BANK_PTR		0x42
#define IXGBE_E610_SR_NVM_BANK_SIZE		0x43
#define IXGBE_E610_SR_1ST_OROM_BANK_PTR		0x44
#define IXGBE_E610_SR_OROM_BANK_SIZE		0x45
#define IXGBE_E610_SR_NETLIST_BANK_PTR		0x46
#define IXGBE_E610_SR_NETLIST_BANK_SIZE		0x47

/* The OROM version topology */
#define IXGBE_OROM_VER_PATCH_MASK		GENMASK_ULL(7, 0)
#define IXGBE_OROM_VER_BUILD_MASK		GENMASK_ULL(23, 8)
#define IXGBE_OROM_VER_MASK			GENMASK_ULL(31, 24)

/* CSS Header words */
#define IXGBE_NVM_CSS_HDR_LEN_L			0x02
#define IXGBE_NVM_CSS_HDR_LEN_H			0x03
#define IXGBE_NVM_CSS_SREV_L			0x14
#define IXGBE_NVM_CSS_SREV_H			0x15

#define IXGBE_HDR_LEN_ROUNDUP			32

/* Length of Authentication header section in words */
#define IXGBE_NVM_AUTH_HEADER_LEN		0x08

/* Shadow RAM related */
#define IXGBE_SR_WORDS_IN_1KB	512

/* The Netlist ID Block is located after all of the Link Topology nodes. */
#define IXGBE_NETLIST_ID_BLK_SIZE		0x30
#define IXGBE_NETLIST_ID_BLK_OFFSET(n)		IXGBE_NETLIST_LINK_TOPO_OFFSET(0x0004 + 2 * (n))

/* netlist ID block field offsets (word offsets) */
#define IXGBE_NETLIST_ID_BLK_MAJOR_VER_LOW	0x02
#define IXGBE_NETLIST_ID_BLK_MAJOR_VER_HIGH	0x03
#define IXGBE_NETLIST_ID_BLK_MINOR_VER_LOW	0x04
#define IXGBE_NETLIST_ID_BLK_MINOR_VER_HIGH	0x05
#define IXGBE_NETLIST_ID_BLK_TYPE_LOW		0x06
#define IXGBE_NETLIST_ID_BLK_TYPE_HIGH		0x07
#define IXGBE_NETLIST_ID_BLK_REV_LOW		0x08
#define IXGBE_NETLIST_ID_BLK_REV_HIGH		0x09
#define IXGBE_NETLIST_ID_BLK_SHA_HASH_WORD(n)	(0x0A + (n))
#define IXGBE_NETLIST_ID_BLK_CUST_VER		0x2F

/* The Link Topology Netlist section is stored as a series of words. It is
 * stored in the NVM as a TLV, with the first two words containing the type
 * and length.
 */
#define IXGBE_NETLIST_LINK_TOPO_MOD_ID		0x011B
#define IXGBE_NETLIST_TYPE_OFFSET		0x0000
#define IXGBE_NETLIST_LEN_OFFSET		0x0001

/* The Link Topology section follows the TLV header. When reading the netlist
 * using ixgbe_read_netlist_module, we need to account for the 2-word TLV
 * header.
 */
#define IXGBE_NETLIST_LINK_TOPO_OFFSET(n)	((n) + 2)
#define IXGBE_LINK_TOPO_MODULE_LEN	IXGBE_NETLIST_LINK_TOPO_OFFSET(0x0000)
#define IXGBE_LINK_TOPO_NODE_COUNT	IXGBE_NETLIST_LINK_TOPO_OFFSET(0x0001)
#define IXGBE_LINK_TOPO_NODE_COUNT_M		GENMASK_ULL(9, 0)

/* Firmware Status Register (GL_FWSTS) */
#define GL_FWSTS		0x00083048 /* Reset Source: POR */
#define GL_FWSTS_EP_PF0		BIT(24)
#define GL_FWSTS_EP_PF1		BIT(25)

/* Global NVM General Status Register */
#define GLNVM_GENS		0x000B6100 /* Reset Source: POR */
#define GLNVM_GENS_SR_SIZE_M	GENMASK(7, 5)

#define IXGBE_GL_MNG_FWSM		0x000B6134 /* Reset Source: POR */
#define IXGBE_GL_MNG_FWSM_RECOVERY_M	BIT(1)
#define IXGBE_GL_MNG_FWSM_ROLLBACK_M    BIT(2)

/* Flash Access Register */
#define IXGBE_GLNVM_FLA			0x000B6108 /* Reset Source: POR */
#define IXGBE_GLNVM_FLA_LOCKED_S	6
#define IXGBE_GLNVM_FLA_LOCKED_M	BIT(6)

/* Auxiliary field, mask and shift definition for Shadow RAM and NVM Flash */
#define IXGBE_SR_CTRL_WORD_1_M		GENMASK(7, 6)
#define IXGBE_SR_CTRL_WORD_VALID	BIT(0)
#define IXGBE_SR_CTRL_WORD_OROM_BANK	BIT(3)
#define IXGBE_SR_CTRL_WORD_NETLIST_BANK	BIT(4)
#define IXGBE_SR_CTRL_WORD_NVM_BANK	BIT(5)
#define IXGBE_SR_NVM_PTR_4KB_UNITS	BIT(15)

/* Admin Command Interface (ACI) registers */
#define IXGBE_PF_HIDA(_i)			(0x00085000 + ((_i) * 4))
#define IXGBE_PF_HIDA_2(_i)			(0x00085020 + ((_i) * 4))
#define IXGBE_PF_HIBA(_i)			(0x00084000 + ((_i) * 4))
#define IXGBE_PF_HICR				0x00082048

#define IXGBE_PF_HICR_EN			BIT(0)
#define IXGBE_PF_HICR_C				BIT(1)
#define IXGBE_PF_HICR_SV			BIT(2)
#define IXGBE_PF_HICR_EV			BIT(3)

#define IXGBE_FW_API_VER_MAJOR		0x01
#define IXGBE_FW_API_VER_MINOR		0x07
#define IXGBE_FW_API_VER_DIFF_ALLOWED	0x02

#define IXGBE_ACI_DESC_SIZE		32
#define IXGBE_ACI_DESC_SIZE_IN_DWORDS	(IXGBE_ACI_DESC_SIZE / BYTES_PER_DWORD)

#define IXGBE_ACI_MAX_BUFFER_SIZE		4096    /* Size in bytes */
#define IXGBE_ACI_SEND_DELAY_TIME_MS		10
#define IXGBE_ACI_SEND_MAX_EXECUTE		3
#define IXGBE_ACI_SEND_TIMEOUT_MS		\
		(IXGBE_ACI_SEND_MAX_EXECUTE * IXGBE_ACI_SEND_DELAY_TIME_MS)
/* [ms] timeout of waiting for sync response */
#define IXGBE_ACI_SYNC_RESPONSE_TIMEOUT		100000
/* [ms] timeout of waiting for async response */
#define IXGBE_ACI_ASYNC_RESPONSE_TIMEOUT	150000
/* [ms] timeout of waiting for resource release */
#define IXGBE_ACI_RELEASE_RES_TIMEOUT		10000

/* Admin Command Interface (ACI) opcodes */
enum ixgbe_aci_opc {
	ixgbe_aci_opc_get_ver				= 0x0001,
	ixgbe_aci_opc_driver_ver			= 0x0002,
	ixgbe_aci_opc_get_exp_err			= 0x0005,

	/* resource ownership */
	ixgbe_aci_opc_req_res				= 0x0008,
	ixgbe_aci_opc_release_res			= 0x0009,

	/* device/function capabilities */
	ixgbe_aci_opc_list_func_caps			= 0x000A,
	ixgbe_aci_opc_list_dev_caps			= 0x000B,

	/* safe disable of RXEN */
	ixgbe_aci_opc_disable_rxen			= 0x000C,

	/* FW events */
	ixgbe_aci_opc_get_fw_event			= 0x0014,

	/* PHY commands */
	ixgbe_aci_opc_get_phy_caps			= 0x0600,
	ixgbe_aci_opc_set_phy_cfg			= 0x0601,
	ixgbe_aci_opc_restart_an			= 0x0605,
	ixgbe_aci_opc_get_link_status			= 0x0607,
	ixgbe_aci_opc_set_event_mask			= 0x0613,
	ixgbe_aci_opc_get_link_topo			= 0x06E0,
	ixgbe_aci_opc_get_link_topo_pin			= 0x06E1,
	ixgbe_aci_opc_read_i2c				= 0x06E2,
	ixgbe_aci_opc_write_i2c				= 0x06E3,
	ixgbe_aci_opc_read_mdio				= 0x06E4,
	ixgbe_aci_opc_write_mdio			= 0x06E5,
	ixgbe_aci_opc_set_gpio_by_func			= 0x06E6,
	ixgbe_aci_opc_get_gpio_by_func			= 0x06E7,
	ixgbe_aci_opc_set_port_id_led			= 0x06E9,
	ixgbe_aci_opc_set_gpio				= 0x06EC,
	ixgbe_aci_opc_get_gpio				= 0x06ED,
	ixgbe_aci_opc_sff_eeprom			= 0x06EE,
	ixgbe_aci_opc_prog_topo_dev_nvm			= 0x06F2,
	ixgbe_aci_opc_read_topo_dev_nvm			= 0x06F3,

	/* NVM commands */
	ixgbe_aci_opc_nvm_read				= 0x0701,
	ixgbe_aci_opc_nvm_erase				= 0x0702,
	ixgbe_aci_opc_nvm_write				= 0x0703,
	ixgbe_aci_opc_nvm_cfg_read			= 0x0704,
	ixgbe_aci_opc_nvm_cfg_write			= 0x0705,
	ixgbe_aci_opc_nvm_checksum			= 0x0706,
	ixgbe_aci_opc_nvm_write_activate		= 0x0707,
	ixgbe_aci_opc_nvm_sr_dump			= 0x0707,
	ixgbe_aci_opc_nvm_save_factory_settings		= 0x0708,
	ixgbe_aci_opc_nvm_update_empr			= 0x0709,
	ixgbe_aci_opc_nvm_pkg_data			= 0x070A,
	ixgbe_aci_opc_nvm_pass_component_tbl		= 0x070B,

	/* Alternate Structure Commands */
	ixgbe_aci_opc_write_alt_direct			= 0x0900,
	ixgbe_aci_opc_write_alt_indirect		= 0x0901,
	ixgbe_aci_opc_read_alt_direct			= 0x0902,
	ixgbe_aci_opc_read_alt_indirect			= 0x0903,
	ixgbe_aci_opc_done_alt_write			= 0x0904,
	ixgbe_aci_opc_clear_port_alt_write		= 0x0906,

	/* TCA Events */
	ixgbe_aci_opc_temp_tca_event                    = 0x0C94,

	/* debug commands */
	ixgbe_aci_opc_debug_dump_internals		= 0xFF08,

	/* SystemDiagnostic commands */
	ixgbe_aci_opc_set_health_status_config		= 0xFF20,
	ixgbe_aci_opc_get_supported_health_status_codes	= 0xFF21,
	ixgbe_aci_opc_get_health_status			= 0xFF22,
	ixgbe_aci_opc_clear_health_status		= 0xFF23,
};

#define IXGBE_DRV_VER_STR_LEN_E610	32

/* Get Expanded Error Code (0x0005, direct) */
struct ixgbe_aci_cmd_get_exp_err {
	__le32 reason;
#define IXGBE_ACI_EXPANDED_ERROR_NOT_PROVIDED	0xFFFFFFFF
	__le32 identifier;
	u8 rsvd[8];
};

/* FW update timeout definitions are in milliseconds */
#define IXGBE_NVM_TIMEOUT		180000

/* Disable RXEN (direct 0x000C) */
struct ixgbe_aci_cmd_disable_rxen {
	u8 lport_num;
	u8 reserved[15];
};

/* Get PHY capabilities (indirect 0x0600) */
struct ixgbe_aci_cmd_get_phy_caps {
	u8 lport_num;
	u8 reserved;
	__le16 param0;
	/* 18.0 - Report qualified modules */
#define IXGBE_ACI_GET_PHY_RQM		BIT(0)
	/* 18.1 - 18.3 : Report mode
	 * 000b - Report topology capabilities, without media
	 * 001b - Report topology capabilities, with media
	 * 010b - Report Active configuration
	 * 011b - Report PHY Type and FEC mode capabilities
	 * 100b - Report Default capabilities
	 */
#define IXGBE_ACI_REPORT_MODE_M			GENMASK(3, 1)
#define IXGBE_ACI_REPORT_TOPO_CAP_NO_MEDIA	0
#define IXGBE_ACI_REPORT_TOPO_CAP_MEDIA		BIT(1)
#define IXGBE_ACI_REPORT_ACTIVE_CFG		BIT(2)
#define IXGBE_ACI_REPORT_DFLT_CFG		BIT(3)
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

/* This is #define of PHY type (Extended):
 * The first set of defines is for phy_type_low.
 */
#define IXGBE_PHY_TYPE_LOW_100BASE_TX		BIT_ULL(0)
#define IXGBE_PHY_TYPE_LOW_100M_SGMII		BIT_ULL(1)
#define IXGBE_PHY_TYPE_LOW_1000BASE_T		BIT_ULL(2)
#define IXGBE_PHY_TYPE_LOW_1000BASE_SX		BIT_ULL(3)
#define IXGBE_PHY_TYPE_LOW_1000BASE_LX		BIT_ULL(4)
#define IXGBE_PHY_TYPE_LOW_1000BASE_KX		BIT_ULL(5)
#define IXGBE_PHY_TYPE_LOW_1G_SGMII		BIT_ULL(6)
#define IXGBE_PHY_TYPE_LOW_2500BASE_T		BIT_ULL(7)
#define IXGBE_PHY_TYPE_LOW_2500BASE_X		BIT_ULL(8)
#define IXGBE_PHY_TYPE_LOW_2500BASE_KX		BIT_ULL(9)
#define IXGBE_PHY_TYPE_LOW_5GBASE_T		BIT_ULL(10)
#define IXGBE_PHY_TYPE_LOW_5GBASE_KR		BIT_ULL(11)
#define IXGBE_PHY_TYPE_LOW_10GBASE_T		BIT_ULL(12)
#define IXGBE_PHY_TYPE_LOW_10G_SFI_DA		BIT_ULL(13)
#define IXGBE_PHY_TYPE_LOW_10GBASE_SR		BIT_ULL(14)
#define IXGBE_PHY_TYPE_LOW_10GBASE_LR		BIT_ULL(15)
#define IXGBE_PHY_TYPE_LOW_10GBASE_KR_CR1	BIT_ULL(16)
#define IXGBE_PHY_TYPE_LOW_10G_SFI_AOC_ACC	BIT_ULL(17)
#define IXGBE_PHY_TYPE_LOW_10G_SFI_C2C		BIT_ULL(18)
#define IXGBE_PHY_TYPE_LOW_25GBASE_T		BIT_ULL(19)
#define IXGBE_PHY_TYPE_LOW_25GBASE_CR		BIT_ULL(20)
#define IXGBE_PHY_TYPE_LOW_25GBASE_CR_S		BIT_ULL(21)
#define IXGBE_PHY_TYPE_LOW_25GBASE_CR1		BIT_ULL(22)
#define IXGBE_PHY_TYPE_LOW_25GBASE_SR		BIT_ULL(23)
#define IXGBE_PHY_TYPE_LOW_25GBASE_LR		BIT_ULL(24)
#define IXGBE_PHY_TYPE_LOW_25GBASE_KR		BIT_ULL(25)
#define IXGBE_PHY_TYPE_LOW_25GBASE_KR_S		BIT_ULL(26)
#define IXGBE_PHY_TYPE_LOW_25GBASE_KR1		BIT_ULL(27)
#define IXGBE_PHY_TYPE_LOW_25G_AUI_AOC_ACC	BIT_ULL(28)
#define IXGBE_PHY_TYPE_LOW_25G_AUI_C2C		BIT_ULL(29)
#define IXGBE_PHY_TYPE_LOW_MAX_INDEX		29
/* The second set of defines is for phy_type_high. */
#define IXGBE_PHY_TYPE_HIGH_10BASE_T		BIT_ULL(1)
#define IXGBE_PHY_TYPE_HIGH_10M_SGMII		BIT_ULL(2)
#define IXGBE_PHY_TYPE_HIGH_2500M_SGMII		BIT_ULL(56)
#define IXGBE_PHY_TYPE_HIGH_100M_USXGMII	BIT_ULL(57)
#define IXGBE_PHY_TYPE_HIGH_1G_USXGMII		BIT_ULL(58)
#define IXGBE_PHY_TYPE_HIGH_2500M_USXGMII	BIT_ULL(59)
#define IXGBE_PHY_TYPE_HIGH_5G_USXGMII		BIT_ULL(60)
#define IXGBE_PHY_TYPE_HIGH_10G_USXGMII		BIT_ULL(61)
#define IXGBE_PHY_TYPE_HIGH_MAX_INDEX		61

struct ixgbe_aci_cmd_get_phy_caps_data {
	__le64 phy_type_low; /* Use values from IXGBE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from IXGBE_PHY_TYPE_HIGH_* */
	u8 caps;
#define IXGBE_ACI_PHY_EN_TX_LINK_PAUSE			BIT(0)
#define IXGBE_ACI_PHY_EN_RX_LINK_PAUSE			BIT(1)
#define IXGBE_ACI_PHY_LOW_POWER_MODE			BIT(2)
#define IXGBE_ACI_PHY_EN_LINK				BIT(3)
#define IXGBE_ACI_PHY_AN_MODE				BIT(4)
#define IXGBE_ACI_PHY_EN_MOD_QUAL			BIT(5)
#define IXGBE_ACI_PHY_EN_LESM				BIT(6)
#define IXGBE_ACI_PHY_EN_AUTO_FEC			BIT(7)
#define IXGBE_ACI_PHY_CAPS_MASK				GENMASK(7, 0)
	u8 low_power_ctrl_an;
#define IXGBE_ACI_PHY_EN_D3COLD_LOW_POWER_AUTONEG	BIT(0)
#define IXGBE_ACI_PHY_AN_EN_CLAUSE28			BIT(1)
#define IXGBE_ACI_PHY_AN_EN_CLAUSE73			BIT(2)
#define IXGBE_ACI_PHY_AN_EN_CLAUSE37			BIT(3)
	__le16 eee_cap;
#define IXGBE_ACI_PHY_EEE_EN_100BASE_TX			BIT(0)
#define IXGBE_ACI_PHY_EEE_EN_1000BASE_T			BIT(1)
#define IXGBE_ACI_PHY_EEE_EN_10GBASE_T			BIT(2)
#define IXGBE_ACI_PHY_EEE_EN_1000BASE_KX		BIT(3)
#define IXGBE_ACI_PHY_EEE_EN_10GBASE_KR			BIT(4)
#define IXGBE_ACI_PHY_EEE_EN_25GBASE_KR			BIT(5)
#define IXGBE_ACI_PHY_EEE_EN_10BASE_T			BIT(11)
	__le16 eeer_value;
	u8 phy_id_oui[4]; /* PHY/Module ID connected on the port */
	u8 phy_fw_ver[8];
	u8 link_fec_options;
#define IXGBE_ACI_PHY_FEC_10G_KR_40G_KR4_EN		BIT(0)
#define IXGBE_ACI_PHY_FEC_10G_KR_40G_KR4_REQ		BIT(1)
#define IXGBE_ACI_PHY_FEC_25G_RS_528_REQ		BIT(2)
#define IXGBE_ACI_PHY_FEC_25G_KR_REQ			BIT(3)
#define IXGBE_ACI_PHY_FEC_25G_RS_544_REQ		BIT(4)
#define IXGBE_ACI_PHY_FEC_25G_RS_CLAUSE91_EN		BIT(6)
#define IXGBE_ACI_PHY_FEC_25G_KR_CLAUSE74_EN		BIT(7)
#define IXGBE_ACI_PHY_FEC_MASK				0xdf
	u8 module_compliance_enforcement;
#define IXGBE_ACI_MOD_ENFORCE_STRICT_MODE		BIT(0)
	u8 extended_compliance_code;
#define IXGBE_ACI_MODULE_TYPE_TOTAL_BYTE		3
	u8 module_type[IXGBE_ACI_MODULE_TYPE_TOTAL_BYTE];
#define IXGBE_ACI_MOD_TYPE_BYTE0_SFP_PLUS		0xA0
#define IXGBE_ACI_MOD_TYPE_BYTE0_QSFP_PLUS		0x80
#define IXGBE_ACI_MOD_TYPE_IDENT			1
#define IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_PASSIVE	BIT(0)
#define IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_ACTIVE	BIT(1)
#define IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_SR		BIT(4)
#define IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_LR		BIT(5)
#define IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_LRM		BIT(6)
#define IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_ER		BIT(7)
#define IXGBE_ACI_MOD_TYPE_BYTE2_SFP_PLUS		0xA0
#define IXGBE_ACI_MOD_TYPE_BYTE2_QSFP_PLUS		0x86
	u8 qualified_module_count;
	u8 rsvd2[7];	/* Bytes 47:41 reserved */
#define IXGBE_ACI_QUAL_MOD_COUNT_MAX			16
	struct {
		u8 v_oui[3];
		u8 rsvd3;
		u8 v_part[16];
		__le32 v_rev;
		__le64 rsvd4;
	} qual_modules[IXGBE_ACI_QUAL_MOD_COUNT_MAX];
};

/* Set PHY capabilities (direct 0x0601)
 * NOTE: This command must be followed by setup link and restart auto-neg
 */
struct ixgbe_aci_cmd_set_phy_cfg {
	u8 lport_num;
	u8 reserved[7];
	__le32 addr_high;
	__le32 addr_low;
};

/* Set PHY config command data structure */
struct ixgbe_aci_cmd_set_phy_cfg_data {
	__le64 phy_type_low; /* Use values from IXGBE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from IXGBE_PHY_TYPE_HIGH_* */
	u8 caps;
#define IXGBE_ACI_PHY_ENA_VALID_MASK		0xef
#define IXGBE_ACI_PHY_ENA_TX_PAUSE_ABILITY	BIT(0)
#define IXGBE_ACI_PHY_ENA_RX_PAUSE_ABILITY	BIT(1)
#define IXGBE_ACI_PHY_ENA_LOW_POWER		BIT(2)
#define IXGBE_ACI_PHY_ENA_LINK			BIT(3)
#define IXGBE_ACI_PHY_ENA_AUTO_LINK_UPDT	BIT(5)
#define IXGBE_ACI_PHY_ENA_LESM			BIT(6)
#define IXGBE_ACI_PHY_ENA_AUTO_FEC		BIT(7)
	u8 low_power_ctrl_an;
	__le16 eee_cap; /* Value from ixgbe_aci_get_phy_caps */
	__le16 eeer_value; /* Use defines from ixgbe_aci_get_phy_caps */
	u8 link_fec_opt; /* Use defines from ixgbe_aci_get_phy_caps */
	u8 module_compliance_enforcement;
};

/* Restart AN command data structure (direct 0x0605)
 * Also used for response, with only the lport_num field present.
 */
struct ixgbe_aci_cmd_restart_an {
	u8 lport_num;
	u8 reserved;
	u8 cmd_flags;
#define IXGBE_ACI_RESTART_AN_LINK_RESTART	BIT(1)
#define IXGBE_ACI_RESTART_AN_LINK_ENABLE	BIT(2)
	u8 reserved2[13];
};

/* Get link status (indirect 0x0607), also used for Link Status Event */
struct ixgbe_aci_cmd_get_link_status {
	u8 lport_num;
	u8 reserved;
	__le16 cmd_flags;
#define IXGBE_ACI_LSE_M				GENMASK(1, 0)
#define IXGBE_ACI_LSE_NOP			0x0
#define IXGBE_ACI_LSE_DIS			0x2
#define IXGBE_ACI_LSE_ENA			0x3
	/* only response uses this flag */
#define IXGBE_ACI_LSE_IS_ENABLED		0x1
	__le32 reserved2;
	__le32 addr_high;
	__le32 addr_low;
};

/* Get link status response data structure, also used for Link Status Event */
struct ixgbe_aci_cmd_get_link_status_data {
	u8 topo_media_conflict;
#define IXGBE_ACI_LINK_TOPO_CONFLICT		BIT(0)
#define IXGBE_ACI_LINK_MEDIA_CONFLICT		BIT(1)
#define IXGBE_ACI_LINK_TOPO_CORRUPT		BIT(2)
#define IXGBE_ACI_LINK_TOPO_UNREACH_PRT		BIT(4)
#define IXGBE_ACI_LINK_TOPO_UNDRUTIL_PRT	BIT(5)
#define IXGBE_ACI_LINK_TOPO_UNDRUTIL_MEDIA	BIT(6)
#define IXGBE_ACI_LINK_TOPO_UNSUPP_MEDIA	BIT(7)
	u8 link_cfg_err;
#define IXGBE_ACI_LINK_CFG_ERR				BIT(0)
#define IXGBE_ACI_LINK_CFG_COMPLETED			BIT(1)
#define IXGBE_ACI_LINK_ACT_PORT_OPT_INVAL		BIT(2)
#define IXGBE_ACI_LINK_FEAT_ID_OR_CONFIG_ID_INVAL	BIT(3)
#define IXGBE_ACI_LINK_TOPO_CRITICAL_SDP_ERR		BIT(4)
#define IXGBE_ACI_LINK_MODULE_POWER_UNSUPPORTED		BIT(5)
#define IXGBE_ACI_LINK_EXTERNAL_PHY_LOAD_FAILURE	BIT(6)
#define IXGBE_ACI_LINK_INVAL_MAX_POWER_LIMIT		BIT(7)
	u8 link_info;
#define IXGBE_ACI_LINK_UP		BIT(0)	/* Link Status */
#define IXGBE_ACI_LINK_FAULT		BIT(1)
#define IXGBE_ACI_LINK_FAULT_TX		BIT(2)
#define IXGBE_ACI_LINK_FAULT_RX		BIT(3)
#define IXGBE_ACI_LINK_FAULT_REMOTE	BIT(4)
#define IXGBE_ACI_LINK_UP_PORT		BIT(5)	/* External Port Link Status */
#define IXGBE_ACI_MEDIA_AVAILABLE	BIT(6)
#define IXGBE_ACI_SIGNAL_DETECT		BIT(7)
	u8 an_info;
#define IXGBE_ACI_AN_COMPLETED		BIT(0)
#define IXGBE_ACI_LP_AN_ABILITY		BIT(1)
#define IXGBE_ACI_PD_FAULT		BIT(2)	/* Parallel Detection Fault */
#define IXGBE_ACI_FEC_EN		BIT(3)
#define IXGBE_ACI_PHY_LOW_POWER		BIT(4)	/* Low Power State */
#define IXGBE_ACI_LINK_PAUSE_TX		BIT(5)
#define IXGBE_ACI_LINK_PAUSE_RX		BIT(6)
#define IXGBE_ACI_QUALIFIED_MODULE	BIT(7)
	u8 ext_info;
#define IXGBE_ACI_LINK_PHY_TEMP_ALARM	BIT(0)
#define IXGBE_ACI_LINK_EXCESSIVE_ERRORS	BIT(1)	/* Excessive Link Errors */
	/* Port Tx Suspended */
#define IXGBE_ACI_LINK_TX_ACTIVE	0
#define IXGBE_ACI_LINK_TX_DRAINED	1
#define IXGBE_ACI_LINK_TX_FLUSHED	3
	u8 lb_status;
#define IXGBE_ACI_LINK_LB_PHY_LCL	BIT(0)
#define IXGBE_ACI_LINK_LB_PHY_RMT	BIT(1)
#define IXGBE_ACI_LINK_LB_MAC_LCL	BIT(2)
	__le16 max_frame_size;
	u8 cfg;
#define IXGBE_ACI_LINK_25G_KR_FEC_EN		BIT(0)
#define IXGBE_ACI_LINK_25G_RS_528_FEC_EN	BIT(1)
#define IXGBE_ACI_LINK_25G_RS_544_FEC_EN	BIT(2)
#define IXGBE_ACI_FEC_MASK			GENMASK(2, 0)
	/* Pacing Config */
#define IXGBE_ACI_CFG_PACING_M		GENMASK(6, 3)
#define IXGBE_ACI_CFG_PACING_TYPE_M	BIT(7)
#define IXGBE_ACI_CFG_PACING_TYPE_AVG	0
#define IXGBE_ACI_CFG_PACING_TYPE_FIXED	IXGBE_ACI_CFG_PACING_TYPE_M
	/* External Device Power Ability */
	u8 power_desc;
#define IXGBE_ACI_PWR_CLASS_M			GENMASK(5, 0)
#define IXGBE_ACI_LINK_PWR_BASET_LOW_HIGH	0
#define IXGBE_ACI_LINK_PWR_BASET_HIGH		1
#define IXGBE_ACI_LINK_PWR_QSFP_CLASS_1		0
#define IXGBE_ACI_LINK_PWR_QSFP_CLASS_2		1
#define IXGBE_ACI_LINK_PWR_QSFP_CLASS_3		2
#define IXGBE_ACI_LINK_PWR_QSFP_CLASS_4		3
	__le16 link_speed;
#define IXGBE_ACI_LINK_SPEED_M			GENMASK(10, 0)
#define IXGBE_ACI_LINK_SPEED_10MB		BIT(0)
#define IXGBE_ACI_LINK_SPEED_100MB		BIT(1)
#define IXGBE_ACI_LINK_SPEED_1000MB		BIT(2)
#define IXGBE_ACI_LINK_SPEED_2500MB		BIT(3)
#define IXGBE_ACI_LINK_SPEED_5GB		BIT(4)
#define IXGBE_ACI_LINK_SPEED_10GB		BIT(5)
#define IXGBE_ACI_LINK_SPEED_20GB		BIT(6)
#define IXGBE_ACI_LINK_SPEED_25GB		BIT(7)
#define IXGBE_ACI_LINK_SPEED_40GB		BIT(8)
#define IXGBE_ACI_LINK_SPEED_50GB		BIT(9)
#define IXGBE_ACI_LINK_SPEED_100GB		BIT(10)
#define IXGBE_ACI_LINK_SPEED_200GB		BIT(11)
#define IXGBE_ACI_LINK_SPEED_UNKNOWN		BIT(15)
	__le16 reserved3;
	u8 ext_fec_status;
#define IXGBE_ACI_LINK_RS_272_FEC_EN	BIT(0) /* RS 272 FEC enabled */
	u8 reserved4;
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from ICE_PHY_TYPE_HIGH_* */
	/* Get link status version 2 link partner data */
	__le64 lp_phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 lp_phy_type_high; /* Use values from ICE_PHY_TYPE_HIGH_* */
	u8 lp_fec_adv;
#define IXGBE_ACI_LINK_LP_10G_KR_FEC_CAP	BIT(0)
#define IXGBE_ACI_LINK_LP_25G_KR_FEC_CAP	BIT(1)
#define IXGBE_ACI_LINK_LP_RS_528_FEC_CAP	BIT(2)
#define IXGBE_ACI_LINK_LP_50G_KR_272_FEC_CAP	BIT(3)
#define IXGBE_ACI_LINK_LP_100G_KR_272_FEC_CAP	BIT(4)
#define IXGBE_ACI_LINK_LP_200G_KR_272_FEC_CAP	BIT(5)
	u8 lp_fec_req;
#define IXGBE_ACI_LINK_LP_10G_KR_FEC_REQ	BIT(0)
#define IXGBE_ACI_LINK_LP_25G_KR_FEC_REQ	BIT(1)
#define IXGBE_ACI_LINK_LP_RS_528_FEC_REQ	BIT(2)
#define IXGBE_ACI_LINK_LP_KR_272_FEC_REQ	BIT(3)
	u8 lp_flowcontrol;
#define IXGBE_ACI_LINK_LP_PAUSE_ADV		BIT(0)
#define IXGBE_ACI_LINK_LP_ASM_DIR_ADV		BIT(1)
	u8 reserved5[5];
} __packed;

/* Set event mask command (direct 0x0613) */
struct ixgbe_aci_cmd_set_event_mask {
	u8	lport_num;
	u8	reserved[7];
	__le16	event_mask;
#define IXGBE_ACI_LINK_EVENT_UPDOWN		BIT(1)
#define IXGBE_ACI_LINK_EVENT_MEDIA_NA		BIT(2)
#define IXGBE_ACI_LINK_EVENT_LINK_FAULT		BIT(3)
#define IXGBE_ACI_LINK_EVENT_PHY_TEMP_ALARM	BIT(4)
#define IXGBE_ACI_LINK_EVENT_EXCESSIVE_ERRORS	BIT(5)
#define IXGBE_ACI_LINK_EVENT_SIGNAL_DETECT	BIT(6)
#define IXGBE_ACI_LINK_EVENT_AN_COMPLETED	BIT(7)
#define IXGBE_ACI_LINK_EVENT_MODULE_QUAL_FAIL	BIT(8)
#define IXGBE_ACI_LINK_EVENT_PORT_TX_SUSPENDED	BIT(9)
#define IXGBE_ACI_LINK_EVENT_TOPO_CONFLICT	BIT(10)
#define IXGBE_ACI_LINK_EVENT_MEDIA_CONFLICT	BIT(11)
#define IXGBE_ACI_LINK_EVENT_PHY_FW_LOAD_FAIL	BIT(12)
	u8	reserved1[6];
};

struct ixgbe_aci_cmd_link_topo_params {
	u8 lport_num;
	u8 lport_num_valid;
#define IXGBE_ACI_LINK_TOPO_PORT_NUM_VALID	BIT(0)
	u8 node_type_ctx;
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_M		GENMASK(3, 0)
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_PHY	0
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_GPIO_CTRL	1
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_MUX_CTRL	2
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_LED_CTRL	3
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_LED	4
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_THERMAL	5
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_CAGE	6
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_MEZZ	7
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_ID_EEPROM	8
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_CLK_CTRL	9
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_CLK_MUX	10
#define IXGBE_ACI_LINK_TOPO_NODE_TYPE_GPS	11
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_S		4
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_M		GENMASK(7, 4)
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_GLOBAL			0
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_BOARD			1
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_PORT			2
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_NODE			3
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_NODE_HANDLE		4
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_DIRECT_BUS_ACCESS		5
#define IXGBE_ACI_LINK_TOPO_NODE_CTX_NODE_HANDLE_BUS_ADDRESS	6
	u8 index;
};

struct ixgbe_aci_cmd_link_topo_addr {
	struct ixgbe_aci_cmd_link_topo_params topo_params;
	__le16 handle;
/* Used to decode the handle field */
#define IXGBE_ACI_LINK_TOPO_HANDLE_BRD_TYPE_M		BIT(9)
#define IXGBE_ACI_LINK_TOPO_HANDLE_BRD_TYPE_LOM		BIT(9)
#define IXGBE_ACI_LINK_TOPO_HANDLE_BRD_TYPE_MEZZ	0
};

/* Get Link Topology Handle (direct, 0x06E0) */
struct ixgbe_aci_cmd_get_link_topo {
	struct ixgbe_aci_cmd_link_topo_addr addr;
	u8 node_part_num;
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_PCA9575		0x21
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_ZL30632_80032	0x24
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_SI5384		0x25
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_C827		0x31
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_GEN_CLK_MUX	0x47
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_GEN_GPS		0x48
#define IXGBE_ACI_GET_LINK_TOPO_NODE_NR_E610_PTC	0x49
	u8 rsvd[9];
};

/* Get Link Topology Pin (direct, 0x06E1) */
struct ixgbe_aci_cmd_get_link_topo_pin {
	struct ixgbe_aci_cmd_link_topo_addr addr;
	u8 input_io_params;
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_GPIO	0
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_RESET_N	1
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_INT_N	2
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_PRESENT_N	3
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_TX_DIS	4
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_MODSEL_N	5
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_LPMODE	6
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_TX_FAULT	7
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_RX_LOSS	8
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_RS0		9
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_RS1		10
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_EEPROM_WP	11
/* 12 repeats intentionally due to two different uses depending on context */
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_LED		12
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_RED_LED	12
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_GREEN_LED	13
#define IXGBE_ACI_LINK_TOPO_IO_FUNC_BLUE_LED	14
#define IXGBE_ACI_LINK_TOPO_INPUT_IO_TYPE_GPIO	3
/* Use IXGBE_ACI_LINK_TOPO_NODE_TYPE_* for the type values */
	u8 output_io_params;
/* Use IXGBE_ACI_LINK_TOPO_NODE_TYPE_* for the type values */
	u8 output_io_flags;
#define IXGBE_ACI_LINK_TOPO_OUTPUT_POLARITY	BIT(5)
#define IXGBE_ACI_LINK_TOPO_OUTPUT_VALUE	BIT(6)
#define IXGBE_ACI_LINK_TOPO_OUTPUT_DRIVEN	BIT(7)
	u8 rsvd[7];
};

/* Set Port Identification LED (direct, 0x06E9) */
struct ixgbe_aci_cmd_set_port_id_led {
	u8 lport_num;
	u8 lport_num_valid;
	u8 ident_mode;
	u8 rsvd[13];
};

#define IXGBE_ACI_PORT_ID_PORT_NUM_VALID	BIT(0)
#define IXGBE_ACI_PORT_IDENT_LED_ORIG		0
#define IXGBE_ACI_PORT_IDENT_LED_BLINK		BIT(0)

/* Read/Write SFF EEPROM command (indirect 0x06EE) */
struct ixgbe_aci_cmd_sff_eeprom {
	u8 lport_num;
	u8 lport_num_valid;
#define IXGBE_ACI_SFF_PORT_NUM_VALID		BIT(0)
	__le16 i2c_bus_addr;
#define IXGBE_ACI_SFF_I2CBUS_7BIT_M		GENMASK(6, 0)
#define IXGBE_ACI_SFF_I2CBUS_10BIT_M		GENMASK(9, 0)
#define IXGBE_ACI_SFF_I2CBUS_TYPE_M		BIT(10)
#define IXGBE_ACI_SFF_I2CBUS_TYPE_7BIT		0
#define IXGBE_ACI_SFF_I2CBUS_TYPE_10BIT		IXGBE_ACI_SFF_I2CBUS_TYPE_M
#define IXGBE_ACI_SFF_NO_PAGE_BANK_UPDATE	0
#define IXGBE_ACI_SFF_UPDATE_PAGE		1
#define IXGBE_ACI_SFF_UPDATE_BANK		2
#define IXGBE_ACI_SFF_UPDATE_PAGE_BANK		3
#define IXGBE_ACI_SFF_IS_WRITE			BIT(15)
	__le16 i2c_offset;
	u8 module_bank;
	u8 module_page;
	__le32 addr_high;
	__le32 addr_low;
};

/* NVM Read command (indirect 0x0701)
 * NVM Erase commands (direct 0x0702)
 * NVM Write commands (indirect 0x0703)
 * NVM Write Activate commands (direct 0x0707)
 * NVM Shadow RAM Dump commands (direct 0x0707)
 */
struct ixgbe_aci_cmd_nvm {
#define IXGBE_ACI_NVM_MAX_OFFSET	0xFFFFFF
	__le16 offset_low;
	u8 offset_high; /* For Write Activate offset_high is used as flags2 */
#define IXGBE_ACI_NVM_OFFSET_HI_A_MASK  GENMASK(15, 8)
#define IXGBE_ACI_NVM_OFFSET_HI_U_MASK	GENMASK(23, 16)
	u8 cmd_flags;
#define IXGBE_ACI_NVM_LAST_CMD		BIT(0)
#define IXGBE_ACI_NVM_PCIR_REQ		BIT(0) /* Used by NVM Write reply */
#define IXGBE_ACI_NVM_PRESERVE_ALL	BIT(1)
#define IXGBE_ACI_NVM_ACTIV_SEL_NVM	BIT(3) /* Write Activate/SR Dump only */
#define IXGBE_ACI_NVM_ACTIV_SEL_OROM	BIT(4)
#define IXGBE_ACI_NVM_ACTIV_SEL_NETLIST	BIT(5)
#define IXGBE_ACI_NVM_SPECIAL_UPDATE	BIT(6)
#define IXGBE_ACI_NVM_REVERT_LAST_ACTIV	BIT(6) /* Write Activate only */
#define IXGBE_ACI_NVM_FLASH_ONLY	BIT(7)
#define IXGBE_ACI_NVM_RESET_LVL_M	GENMASK(1, 0) /* Write reply only */
#define IXGBE_ACI_NVM_POR_FLAG		0
#define IXGBE_ACI_NVM_PERST_FLAG	1
#define IXGBE_ACI_NVM_EMPR_FLAG		2
#define IXGBE_ACI_NVM_EMPR_ENA		BIT(0) /* Write Activate reply only */
#define IXGBE_ACI_NVM_NO_PRESERVATION	0x0
#define IXGBE_ACI_NVM_PRESERVE_SELECTED	0x6

	/* For Write Activate, several flags are sent as part of a separate
	 * flags2 field using a separate byte. For simplicity of the software
	 * interface, we pass the flags as a 16 bit value so these flags are
	 * all offset by 8 bits
	 */
#define IXGBE_ACI_NVM_ACTIV_REQ_EMPR	BIT(8) /* NVM Write Activate only */
	__le16 module_typeid;
	__le16 length;
#define IXGBE_ACI_NVM_ERASE_LEN	0xFFFF
	__le32 addr_high;
	__le32 addr_low;
};

/* NVM Module_Type ID, needed offset and read_len for
 * struct ixgbe_aci_cmd_nvm.
 */
#define IXGBE_ACI_NVM_START_POINT		0

/* NVM Checksum Command (direct, 0x0706) */
struct ixgbe_aci_cmd_nvm_checksum {
	u8 flags;
#define IXGBE_ACI_NVM_CHECKSUM_VERIFY	BIT(0)
#define IXGBE_ACI_NVM_CHECKSUM_RECALC	BIT(1)
	u8 rsvd;
	__le16 checksum; /* Used only by response */
#define IXGBE_ACI_NVM_CHECKSUM_CORRECT	0xBABA
	u8 rsvd2[12];
};

/* Used for NVM Set Package Data command - 0x070A */
struct ixgbe_aci_cmd_nvm_pkg_data {
	u8 reserved[3];
	u8 cmd_flags;
#define IXGBE_ACI_NVM_PKG_DELETE	BIT(0) /* used for command call */

	u32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

/* Used for Pass Component Table command - 0x070B */
struct ixgbe_aci_cmd_nvm_pass_comp_tbl {
	u8 component_response; /* Response only */
#define IXGBE_ACI_NVM_PASS_COMP_CAN_BE_UPDATED		0x0
#define IXGBE_ACI_NVM_PASS_COMP_CAN_MAY_BE_UPDATEABLE	0x1
#define IXGBE_ACI_NVM_PASS_COMP_CAN_NOT_BE_UPDATED	0x2
#define IXGBE_ACI_NVM_PASS_COMP_PARTIAL_CHECK		0x3
	u8 component_response_code; /* Response only */
#define IXGBE_ACI_NVM_PASS_COMP_CAN_BE_UPDATED_CODE	0x0
#define IXGBE_ACI_NVM_PASS_COMP_STAMP_IDENTICAL_CODE	0x1
#define IXGBE_ACI_NVM_PASS_COMP_STAMP_LOWER		0x2
#define IXGBE_ACI_NVM_PASS_COMP_INVALID_STAMP_CODE	0x3
#define IXGBE_ACI_NVM_PASS_COMP_CONFLICT_CODE		0x4
#define IXGBE_ACI_NVM_PASS_COMP_PRE_REQ_NOT_MET_CODE	0x5
#define IXGBE_ACI_NVM_PASS_COMP_NOT_SUPPORTED_CODE	0x6
#define IXGBE_ACI_NVM_PASS_COMP_CANNOT_DOWNGRADE_CODE	0x7
#define IXGBE_ACI_NVM_PASS_COMP_INCOMPLETE_IMAGE_CODE	0x8
#define IXGBE_ACI_NVM_PASS_COMP_VER_STR_IDENTICAL_CODE	0xA
#define IXGBE_ACI_NVM_PASS_COMP_VER_STR_LOWER_CODE	0xB
	u8 reserved;
	u8 transfer_flag;
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

struct ixgbe_aci_cmd_nvm_comp_tbl {
	__le16 comp_class;
#define NVM_COMP_CLASS_ALL_FW		0x000A

	__le16 comp_id;
#define NVM_COMP_ID_OROM		0x5
#define NVM_COMP_ID_NVM			0x6
#define NVM_COMP_ID_NETLIST		0x8

	u8 comp_class_idx;
#define FWU_COMP_CLASS_IDX_NOT_USE	0x0

	__le32 comp_cmp_stamp;
	u8 cvs_type;
#define NVM_CVS_TYPE_ASCII		0x1

	u8 cvs_len;
	u8 cvs[]; /* Component Version String */
} __packed;

/* E610-specific adapter context structures */

struct ixgbe_link_status {
	/* Refer to ixgbe_aci_phy_type for bits definition */
	u64 phy_type_low;
	u64 phy_type_high;
	u16 max_frame_size;
	u16 link_speed;
	u16 req_speeds;
	u8 topo_media_conflict;
	u8 link_cfg_err;
	u8 lse_ena;	/* Link Status Event notification */
	u8 link_info;
	u8 an_info;
	u8 ext_info;
	u8 fec_info;
	u8 pacing;
	/* Refer to #define from module_type[IXGBE_ACI_MODULE_TYPE_TOTAL_BYTE]
	 * of ixgbe_aci_get_phy_caps structure
	 */
	u8 module_type[IXGBE_ACI_MODULE_TYPE_TOTAL_BYTE];
};

/* Common HW capabilities for SW use */
struct ixgbe_hw_caps {
	/* Write CSR protection */
	u64 wr_csr_prot;
	u32 switching_mode;
	/* switching mode supported - EVB switching (including cloud) */
#define IXGBE_NVM_IMAGE_TYPE_EVB		0x0

	/* Manageability mode & supported protocols over MCTP */
	u32 mgmt_mode;
#define IXGBE_MGMT_MODE_PASS_THRU_MODE_M	GENMASK(3, 0)
#define IXGBE_MGMT_MODE_CTL_INTERFACE_M		GENMASK(7, 4)
#define IXGBE_MGMT_MODE_REDIR_SB_INTERFACE_M	GENMASK(11, 8)

	u32 mgmt_protocols_mctp;
#define IXGBE_MGMT_MODE_PROTO_RSVD	BIT(0)
#define IXGBE_MGMT_MODE_PROTO_PLDM	BIT(1)
#define IXGBE_MGMT_MODE_PROTO_OEM	BIT(2)
#define IXGBE_MGMT_MODE_PROTO_NC_SI	BIT(3)

	u32 os2bmc;
	u32 valid_functions;
	/* DCB capabilities */
	u32 active_tc_bitmap;
	u32 maxtc;

	/* RSS related capabilities */
	u32 rss_table_size;		/* 512 for PFs and 64 for VFs */
	u32 rss_table_entry_width;	/* RSS Entry width in bits */

	/* Tx/Rx queues */
	u32 num_rxq;			/* Number/Total Rx queues */
	u32 rxq_first_id;		/* First queue ID for Rx queues */
	u32 num_txq;			/* Number/Total Tx queues */
	u32 txq_first_id;		/* First queue ID for Tx queues */

	/* MSI-X vectors */
	u32 num_msix_vectors;
	u32 msix_vector_first_id;

	/* Max MTU for function or device */
	u32 max_mtu;

	/* WOL related */
	u32 num_wol_proxy_fltr;
	u32 wol_proxy_vsi_seid;

	/* LED/SDP pin count */
	u32 led_pin_num;
	u32 sdp_pin_num;

	/* LED/SDP - Supports up to 12 LED pins and 8 SDP signals */
#define IXGBE_MAX_SUPPORTED_GPIO_LED	12
#define IXGBE_MAX_SUPPORTED_GPIO_SDP	8
	u8 led[IXGBE_MAX_SUPPORTED_GPIO_LED];
	u8 sdp[IXGBE_MAX_SUPPORTED_GPIO_SDP];
	/* SR-IOV virtualization */
	u8 sr_iov_1_1;			/* SR-IOV enabled */
	/* VMDQ */
	u8 vmdq;			/* VMDQ supported */

	/* EVB capabilities */
	u8 evb_802_1_qbg;		/* Edge Virtual Bridging */
	u8 evb_802_1_qbh;		/* Bridge Port Extension */

	u8 dcb;
	u8 iscsi;
	u8 ieee_1588;
	u8 mgmt_cem;

	/* WoL and APM support */
#define IXGBE_WOL_SUPPORT_M		BIT(0)
#define IXGBE_ACPI_PROG_MTHD_M		BIT(1)
#define IXGBE_PROXY_SUPPORT_M		BIT(2)
	u8 apm_wol_support;
	u8 acpi_prog_mthd;
	u8 proxy_support;
	bool nvm_update_pending_nvm;
	bool nvm_update_pending_orom;
	bool nvm_update_pending_netlist;
#define IXGBE_NVM_PENDING_NVM_IMAGE		BIT(0)
#define IXGBE_NVM_PENDING_OROM			BIT(1)
#define IXGBE_NVM_PENDING_NETLIST		BIT(2)
	bool sec_rev_disabled;
	bool update_disabled;
	bool nvm_unified_update;
	bool netlist_auth;
#define IXGBE_NVM_MGMT_SEC_REV_DISABLED		BIT(0)
#define IXGBE_NVM_MGMT_UPDATE_DISABLED		BIT(1)
#define IXGBE_NVM_MGMT_UNIFIED_UPD_SUPPORT	BIT(3)
#define IXGBE_NVM_MGMT_NETLIST_AUTH_SUPPORT	BIT(5)
	bool no_drop_policy_support;
	/* PCIe reset avoidance */
	bool pcie_reset_avoidance; /* false: not supported, true: supported */
	/* Post update reset restriction */
	bool reset_restrict_support; /* false: not supported, true: supported */

	/* External topology device images within the NVM */
#define IXGBE_EXT_TOPO_DEV_IMG_COUNT	4
	u32 ext_topo_dev_img_ver_high[IXGBE_EXT_TOPO_DEV_IMG_COUNT];
	u32 ext_topo_dev_img_ver_low[IXGBE_EXT_TOPO_DEV_IMG_COUNT];
	u8 ext_topo_dev_img_part_num[IXGBE_EXT_TOPO_DEV_IMG_COUNT];
#define IXGBE_EXT_TOPO_DEV_IMG_PART_NUM_S	8
#define IXGBE_EXT_TOPO_DEV_IMG_PART_NUM_M	GENMASK(15, 8)
	bool ext_topo_dev_img_load_en[IXGBE_EXT_TOPO_DEV_IMG_COUNT];
#define IXGBE_EXT_TOPO_DEV_IMG_LOAD_EN	BIT(0)
	bool ext_topo_dev_img_prog_en[IXGBE_EXT_TOPO_DEV_IMG_COUNT];
#define IXGBE_EXT_TOPO_DEV_IMG_PROG_EN	BIT(1)
} __packed;

#define IXGBE_OROM_CIV_SIGNATURE	"$CIV"

struct ixgbe_orom_civd_info {
	u8 signature[4];	/* Must match ASCII '$CIV' characters */
	u8 checksum;		/* Simple modulo 256 sum of all structure bytes must equal 0 */
	__le32 combo_ver;	/* Combo Image Version number */
	u8 combo_name_len;	/* Length of the unicode combo image version string, max of 32 */
	__le16 combo_name[32];	/* Unicode string representing the Combo Image version */
} __packed;

/* Function specific capabilities */
struct ixgbe_hw_func_caps {
	u32 num_allocd_vfs;		/* Number of allocated VFs */
	u32 vf_base_id;			/* Logical ID of the first VF */
	u32 guar_num_vsi;
	struct ixgbe_hw_caps common_cap;
	bool no_drop_policy_ena;
};

/* Device wide capabilities */
struct ixgbe_hw_dev_caps {
	struct ixgbe_hw_caps common_cap;
	u32 num_vfs_exposed;		/* Total number of VFs exposed */
	u32 num_vsi_allocd_to_host;	/* Excluding EMP VSI */
	u32 num_flow_director_fltr;	/* Number of FD filters available */
	u32 num_funcs;
};

/* ACI event information */
struct ixgbe_aci_event {
	struct libie_aq_desc desc;
	u8 *msg_buf;
	u16 msg_len;
	u16 buf_len;
};

struct ixgbe_aci_info {
	struct mutex lock;		/* admin command interface lock */
	enum libie_aq_err last_status;	/* last status of sent admin command */
};

enum ixgbe_bank_select {
	IXGBE_ACTIVE_FLASH_BANK,
	IXGBE_INACTIVE_FLASH_BANK,
};

/* Option ROM version information */
struct ixgbe_orom_info {
	u8 major;			/* Major version of OROM */
	u8 patch;			/* Patch version of OROM */
	u16 build;			/* Build version of OROM */
	u32 srev;			/* Security revision */
};

/* NVM version information */
struct ixgbe_nvm_info {
	u32 eetrack;
	u32 srev;
	u8 major;
	u8 minor;
} __packed;

/* netlist version information */
struct ixgbe_netlist_info {
	u32 major;			/* major high/low */
	u32 minor;			/* minor high/low */
	u32 type;			/* type high/low */
	u32 rev;			/* revision high/low */
	u32 hash;			/* SHA-1 hash word */
	u16 cust_ver;			/* customer version */
} __packed;

/* Enumeration of possible flash banks for the NVM, OROM, and Netlist modules
 * of the flash image.
 */
enum ixgbe_flash_bank {
	IXGBE_INVALID_FLASH_BANK,
	IXGBE_1ST_FLASH_BANK,
	IXGBE_2ND_FLASH_BANK,
};

/* information for accessing NVM, OROM, and Netlist flash banks */
struct ixgbe_bank_info {
	u32 nvm_ptr;				/* Pointer to 1st NVM bank */
	u32 nvm_size;				/* Size of NVM bank */
	u32 orom_ptr;				/* Pointer to 1st OROM bank */
	u32 orom_size;				/* Size of OROM bank */
	u32 netlist_ptr;			/* Ptr to 1st Netlist bank */
	u32 netlist_size;			/* Size of Netlist bank */
	enum ixgbe_flash_bank nvm_bank;		/* Active NVM bank */
	enum ixgbe_flash_bank orom_bank;	/* Active OROM bank */
	enum ixgbe_flash_bank netlist_bank;	/* Active Netlist bank */
};

/* Flash Chip Information */
struct ixgbe_flash_info {
	struct ixgbe_orom_info orom;	/* Option ROM version info */
	u32 flash_size;			/* Available flash size in bytes */
	struct ixgbe_nvm_info nvm;	/* NVM version information */
	struct ixgbe_netlist_info netlist;	/* Netlist version info */
	struct ixgbe_bank_info banks;	/* Flash Bank information */
	u16 sr_words;			/* Shadow RAM size in words */
	u8 blank_nvm_mode;		/* is NVM empty (no FW present) */
};

#endif /* _IXGBE_TYPE_E610_H_ */
