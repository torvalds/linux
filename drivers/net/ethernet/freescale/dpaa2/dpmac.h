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

/* DPMAC link configuration/state options */

#define DPMAC_LINK_OPT_AUTONEG			BIT_ULL(0)
#define DPMAC_LINK_OPT_HALF_DUPLEX		BIT_ULL(1)
#define DPMAC_LINK_OPT_PAUSE			BIT_ULL(2)
#define DPMAC_LINK_OPT_ASYM_PAUSE		BIT_ULL(3)

/* Advertised link speeds */
#define DPMAC_ADVERTISED_10BASET_FULL		BIT_ULL(0)
#define DPMAC_ADVERTISED_100BASET_FULL		BIT_ULL(1)
#define DPMAC_ADVERTISED_1000BASET_FULL		BIT_ULL(2)
#define DPMAC_ADVERTISED_10000BASET_FULL	BIT_ULL(4)
#define DPMAC_ADVERTISED_2500BASEX_FULL		BIT_ULL(5)

/* Advertise auto-negotiation enable */
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

/**
 * enum dpmac_counter_id - DPMAC counter types
 *
 * @DPMAC_CNT_ING_FRAME_64: counts 64-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_127: counts 65- to 127-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_255: counts 128- to 255-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_511: counts 256- to 511-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_1023: counts 512- to 1023-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_1518: counts 1024- to 1518-bytes frames, good or bad.
 * @DPMAC_CNT_ING_FRAME_1519_MAX: counts 1519-bytes frames and larger
 *				  (up to max frame length specified),
 *				  good or bad.
 * @DPMAC_CNT_ING_FRAG: counts frames which are shorter than 64 bytes received
 *			with a wrong CRC
 * @DPMAC_CNT_ING_JABBER: counts frames longer than the maximum frame length
 *			  specified, with a bad frame check sequence.
 * @DPMAC_CNT_ING_FRAME_DISCARD: counts dropped frames due to internal errors.
 *				 Occurs when a receive FIFO overflows.
 *				 Includes also frames truncated as a result of
 *				 the receive FIFO overflow.
 * @DPMAC_CNT_ING_ALIGN_ERR: counts frames with an alignment error
 *			     (optional used for wrong SFD).
 * @DPMAC_CNT_EGR_UNDERSIZED: counts frames transmitted that was less than 64
 *			      bytes long with a good CRC.
 * @DPMAC_CNT_ING_OVERSIZED: counts frames longer than the maximum frame length
 *			     specified, with a good frame check sequence.
 * @DPMAC_CNT_ING_VALID_PAUSE_FRAME: counts valid pause frames (regular and PFC)
 * @DPMAC_CNT_EGR_VALID_PAUSE_FRAME: counts valid pause frames transmitted
 *				     (regular and PFC).
 * @DPMAC_CNT_ING_BYTE: counts bytes received except preamble for all valid
 *			frames and valid pause frames.
 * @DPMAC_CNT_ING_MCAST_FRAME: counts received multicast frames.
 * @DPMAC_CNT_ING_BCAST_FRAME: counts received broadcast frames.
 * @DPMAC_CNT_ING_ALL_FRAME: counts each good or bad frames received.
 * @DPMAC_CNT_ING_UCAST_FRAME: counts received unicast frames.
 * @DPMAC_CNT_ING_ERR_FRAME: counts frames received with an error
 *			     (except for undersized/fragment frame).
 * @DPMAC_CNT_EGR_BYTE: counts bytes transmitted except preamble for all valid
 *			frames and valid pause frames transmitted.
 * @DPMAC_CNT_EGR_MCAST_FRAME: counts transmitted multicast frames.
 * @DPMAC_CNT_EGR_BCAST_FRAME: counts transmitted broadcast frames.
 * @DPMAC_CNT_EGR_UCAST_FRAME: counts transmitted unicast frames.
 * @DPMAC_CNT_EGR_ERR_FRAME: counts frames transmitted with an error.
 * @DPMAC_CNT_ING_GOOD_FRAME: counts frames received without error, including
 *			      pause frames.
 * @DPMAC_CNT_EGR_GOOD_FRAME: counts frames transmitted without error, including
 *			      pause frames.
 */
enum dpmac_counter_id {
	DPMAC_CNT_ING_FRAME_64,
	DPMAC_CNT_ING_FRAME_127,
	DPMAC_CNT_ING_FRAME_255,
	DPMAC_CNT_ING_FRAME_511,
	DPMAC_CNT_ING_FRAME_1023,
	DPMAC_CNT_ING_FRAME_1518,
	DPMAC_CNT_ING_FRAME_1519_MAX,
	DPMAC_CNT_ING_FRAG,
	DPMAC_CNT_ING_JABBER,
	DPMAC_CNT_ING_FRAME_DISCARD,
	DPMAC_CNT_ING_ALIGN_ERR,
	DPMAC_CNT_EGR_UNDERSIZED,
	DPMAC_CNT_ING_OVERSIZED,
	DPMAC_CNT_ING_VALID_PAUSE_FRAME,
	DPMAC_CNT_EGR_VALID_PAUSE_FRAME,
	DPMAC_CNT_ING_BYTE,
	DPMAC_CNT_ING_MCAST_FRAME,
	DPMAC_CNT_ING_BCAST_FRAME,
	DPMAC_CNT_ING_ALL_FRAME,
	DPMAC_CNT_ING_UCAST_FRAME,
	DPMAC_CNT_ING_ERR_FRAME,
	DPMAC_CNT_EGR_BYTE,
	DPMAC_CNT_EGR_MCAST_FRAME,
	DPMAC_CNT_EGR_BCAST_FRAME,
	DPMAC_CNT_EGR_UCAST_FRAME,
	DPMAC_CNT_EGR_ERR_FRAME,
	DPMAC_CNT_ING_GOOD_FRAME,
	DPMAC_CNT_EGR_GOOD_FRAME
};

int dpmac_get_counter(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
		      enum dpmac_counter_id id, u64 *value);

#endif /* __FSL_DPMAC_H */
