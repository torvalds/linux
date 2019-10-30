/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 */
#ifndef __FSL_DPMAC_H
#define __FSL_DPMAC_H

/* Data Path MAC API
 * Contains initialization APIs and runtime control APIs for DPMAC
 */

struct fsl_mc_io;

int dpmac_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpmac_id,
	       u16 *token);

int dpmac_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

/**
 * enum dpmac_link_type -  DPMAC link type
 * @DPMAC_LINK_TYPE_NONE: No link
 * @DPMAC_LINK_TYPE_FIXED: Link is fixed type
 * @DPMAC_LINK_TYPE_PHY: Link by PHY ID
 * @DPMAC_LINK_TYPE_BACKPLANE: Backplane link type
 */
enum dpmac_link_type {
	DPMAC_LINK_TYPE_NONE,
	DPMAC_LINK_TYPE_FIXED,
	DPMAC_LINK_TYPE_PHY,
	DPMAC_LINK_TYPE_BACKPLANE
};

/**
 * enum dpmac_eth_if - DPMAC Ethrnet interface
 * @DPMAC_ETH_IF_MII: MII interface
 * @DPMAC_ETH_IF_RMII: RMII interface
 * @DPMAC_ETH_IF_SMII: SMII interface
 * @DPMAC_ETH_IF_GMII: GMII interface
 * @DPMAC_ETH_IF_RGMII: RGMII interface
 * @DPMAC_ETH_IF_SGMII: SGMII interface
 * @DPMAC_ETH_IF_QSGMII: QSGMII interface
 * @DPMAC_ETH_IF_XAUI: XAUI interface
 * @DPMAC_ETH_IF_XFI: XFI interface
 * @DPMAC_ETH_IF_CAUI: CAUI interface
 * @DPMAC_ETH_IF_1000BASEX: 1000BASEX interface
 * @DPMAC_ETH_IF_USXGMII: USXGMII interface
 */
enum dpmac_eth_if {
	DPMAC_ETH_IF_MII,
	DPMAC_ETH_IF_RMII,
	DPMAC_ETH_IF_SMII,
	DPMAC_ETH_IF_GMII,
	DPMAC_ETH_IF_RGMII,
	DPMAC_ETH_IF_SGMII,
	DPMAC_ETH_IF_QSGMII,
	DPMAC_ETH_IF_XAUI,
	DPMAC_ETH_IF_XFI,
	DPMAC_ETH_IF_CAUI,
	DPMAC_ETH_IF_1000BASEX,
	DPMAC_ETH_IF_USXGMII,
};

/**
 * struct dpmac_attr - Structure representing DPMAC attributes
 * @id:		DPMAC object ID
 * @max_rate:	Maximum supported rate - in Mbps
 * @eth_if:	Ethernet interface
 * @link_type:	link type
 */
struct dpmac_attr {
	u16 id;
	u32 max_rate;
	enum dpmac_eth_if eth_if;
	enum dpmac_link_type link_type;
};

int dpmac_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpmac_attr *attr);

/**
 * DPMAC link configuration/state options
 */

/**
 * Enable auto-negotiation
 */
#define DPMAC_LINK_OPT_AUTONEG			BIT_ULL(0)
/**
 * Enable half-duplex mode
 */
#define DPMAC_LINK_OPT_HALF_DUPLEX		BIT_ULL(1)
/**
 * Enable pause frames
 */
#define DPMAC_LINK_OPT_PAUSE			BIT_ULL(2)
/**
 * Enable a-symmetric pause frames
 */
#define DPMAC_LINK_OPT_ASYM_PAUSE		BIT_ULL(3)

/**
 * Advertised link speeds
 */
#define DPMAC_ADVERTISED_10BASET_FULL		BIT_ULL(0)
#define DPMAC_ADVERTISED_100BASET_FULL		BIT_ULL(1)
#define DPMAC_ADVERTISED_1000BASET_FULL		BIT_ULL(2)
#define DPMAC_ADVERTISED_10000BASET_FULL	BIT_ULL(4)
#define DPMAC_ADVERTISED_2500BASEX_FULL		BIT_ULL(5)

/**
 * Advertise auto-negotiation enable
 */
#define DPMAC_ADVERTISED_AUTONEG		BIT_ULL(3)

/**
 * struct dpmac_link_state - DPMAC link configuration request
 * @rate: Rate in Mbps
 * @options: Enable/Disable DPMAC link cfg features (bitmap)
 * @up: Link state
 * @state_valid: Ignore/Update the state of the link
 * @supported: Speeds capability of the phy (bitmap)
 * @advertising: Speeds that are advertised for autoneg (bitmap)
 */
struct dpmac_link_state {
	u32 rate;
	u64 options;
	int up;
	int state_valid;
	u64 supported;
	u64 advertising;
};

int dpmac_set_link_state(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpmac_link_state *link_state);

#endif /* __FSL_DPMAC_H */
