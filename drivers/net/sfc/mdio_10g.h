/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_MDIO_10G_H
#define EFX_MDIO_10G_H

/*
 * Definitions needed for doing 10G MDIO as specified in clause 45
 * MDIO, which do not appear in Linux yet. Also some helper functions.
 */

#include "efx.h"
#include "boards.h"

/* Numbering of the MDIO Manageable Devices (MMDs) */
/* Physical Medium Attachment/ Physical Medium Dependent sublayer */
#define MDIO_MMD_PMAPMD	(1)
/* WAN Interface Sublayer */
#define MDIO_MMD_WIS	(2)
/* Physical Coding Sublayer */
#define MDIO_MMD_PCS	(3)
/* PHY Extender Sublayer */
#define MDIO_MMD_PHYXS	(4)
/* Extender Sublayer */
#define MDIO_MMD_DTEXS	(5)
/* Transmission convergence */
#define MDIO_MMD_TC	(6)
/* Auto negotiation */
#define MDIO_MMD_AN	(7)

/* Generic register locations */
#define MDIO_MMDREG_CTRL1	(0)
#define MDIO_MMDREG_STAT1	(1)
#define MDIO_MMDREG_IDHI	(2)
#define MDIO_MMDREG_IDLOW	(3)
#define MDIO_MMDREG_SPEED	(4)
#define MDIO_MMDREG_DEVS0	(5)
#define MDIO_MMDREG_DEVS1	(6)
#define MDIO_MMDREG_CTRL2	(7)
#define MDIO_MMDREG_STAT2	(8)
#define MDIO_MMDREG_TXDIS	(9)

/* Bits in MMDREG_CTRL1 */
/* Reset */
#define MDIO_MMDREG_CTRL1_RESET_LBN	(15)
#define MDIO_MMDREG_CTRL1_RESET_WIDTH	(1)
/* Loopback */
/* Loopback bit for WIS, PCS, PHYSX and DTEXS */
#define MDIO_MMDREG_CTRL1_LBACK_LBN	(14)
#define MDIO_MMDREG_CTRL1_LBACK_WIDTH	(1)

/* Bits in MMDREG_STAT1 */
#define MDIO_MMDREG_STAT1_FAULT_LBN	(7)
#define MDIO_MMDREG_STAT1_FAULT_WIDTH	(1)
/* Link state */
#define MDIO_MMDREG_STAT1_LINK_LBN	(2)
#define MDIO_MMDREG_STAT1_LINK_WIDTH	(1)
/* Low power ability */
#define MDIO_MMDREG_STAT1_LPABLE_LBN	(1)
#define MDIO_MMDREG_STAT1_LPABLE_WIDTH	(1)

/* Bits in ID reg */
#define MDIO_ID_REV(_id32)	(_id32 & 0xf)
#define MDIO_ID_MODEL(_id32)	((_id32 >> 4) & 0x3f)
#define MDIO_ID_OUI(_id32)	(_id32 >> 10)

/* Bits in MMDREG_DEVS0. Someone thoughtfully layed things out
 * so the 'bit present' bit number of an MMD is the number of
 * that MMD */
#define DEV_PRESENT_BIT(_b) (1 << _b)

#define MDIO_MMDREG_DEVS0_PHYXS	 DEV_PRESENT_BIT(MDIO_MMD_PHYXS)
#define MDIO_MMDREG_DEVS0_PCS	 DEV_PRESENT_BIT(MDIO_MMD_PCS)
#define MDIO_MMDREG_DEVS0_PMAPMD DEV_PRESENT_BIT(MDIO_MMD_PMAPMD)

/* Bits in MMDREG_STAT2 */
#define MDIO_MMDREG_STAT2_PRESENT_VAL	(2)
#define MDIO_MMDREG_STAT2_PRESENT_LBN	(14)
#define MDIO_MMDREG_STAT2_PRESENT_WIDTH (2)

/* Bits in MMDREG_TXDIS */
#define MDIO_MMDREG_TXDIS_GLOBAL_LBN    (0)
#define MDIO_MMDREG_TXDIS_GLOBAL_WIDTH  (1)

/* MMD-specific bits, ordered by MMD, then register */
#define MDIO_PMAPMD_CTRL1_LBACK_LBN	(0)
#define MDIO_PMAPMD_CTRL1_LBACK_WIDTH	(1)

/* PMA type (4 bits) */
#define MDIO_PMAPMD_CTRL2_10G_CX4	(0x0)
#define MDIO_PMAPMD_CTRL2_10G_EW	(0x1)
#define MDIO_PMAPMD_CTRL2_10G_LW	(0x2)
#define MDIO_PMAPMD_CTRL2_10G_SW	(0x3)
#define MDIO_PMAPMD_CTRL2_10G_LX4	(0x4)
#define MDIO_PMAPMD_CTRL2_10G_ER	(0x5)
#define MDIO_PMAPMD_CTRL2_10G_LR	(0x6)
#define MDIO_PMAPMD_CTRL2_10G_SR	(0x7)
/* Reserved */
#define MDIO_PMAPMD_CTRL2_10G_BT	(0x9)
/* Reserved */
/* Reserved */
#define MDIO_PMAPMD_CTRL2_1G_BT		(0xc)
/* Reserved */
#define MDIO_PMAPMD_CTRL2_100_BT	(0xe)
#define MDIO_PMAPMD_CTRL2_10_BT		(0xf)
#define MDIO_PMAPMD_CTRL2_TYPE_MASK	(0xf)

/* PHY XGXS lane state */
#define MDIO_PHYXS_LANE_STATE		(0x18)
#define MDIO_PHYXS_LANE_ALIGNED_LBN	(12)

/* AN registers */
#define MDIO_AN_STATUS			(1)
#define MDIO_AN_STATUS_XNP_LBN		(7)
#define MDIO_AN_STATUS_PAGE_LBN		(6)
#define MDIO_AN_STATUS_AN_DONE_LBN	(5)
#define MDIO_AN_STATUS_LP_AN_CAP_LBN	(0)

#define MDIO_AN_10GBT_STATUS		(33)
#define MDIO_AN_10GBT_STATUS_MS_FLT_LBN (15) /* MASTER/SLAVE config fault */
#define MDIO_AN_10GBT_STATUS_MS_LBN     (14) /* MASTER/SLAVE config */
#define MDIO_AN_10GBT_STATUS_LOC_OK_LBN (13) /* Local OK */
#define MDIO_AN_10GBT_STATUS_REM_OK_LBN (12) /* Remote OK */
#define MDIO_AN_10GBT_STATUS_LP_10G_LBN (11) /* Link partner is 10GBT capable */
#define MDIO_AN_10GBT_STATUS_LP_LTA_LBN (10) /* LP loop timing ability */
#define MDIO_AN_10GBT_STATUS_LP_TRR_LBN (9)  /* LP Training Reset Request */


/* Packing of the prt and dev arguments of clause 45 style MDIO into a
 * single int so they can be passed into the mdio_read/write functions
 * that currently exist. Note that as Falcon is the only current user,
 * the packed form is chosen to match what Falcon needs to write into
 * a register. This is checked at compile-time so do not change it. If
 * your target chip needs things layed out differently you will need
 * to unpack the arguments in your chip-specific mdio functions.
 */
 /* These are defined by the standard. */
#define MDIO45_PRT_ID_WIDTH  (5)
#define MDIO45_DEV_ID_WIDTH  (5)

/* The prt ID is just packed in immediately to the left of the dev ID */
#define MDIO45_PRT_DEV_WIDTH (MDIO45_PRT_ID_WIDTH + MDIO45_DEV_ID_WIDTH)

#define MDIO45_PRT_ID_MASK   ((1 << MDIO45_PRT_DEV_WIDTH) - 1)
/* This is the prt + dev extended by 1 bit to hold the 'is clause 45' flag. */
#define MDIO45_XPRT_ID_WIDTH   (MDIO45_PRT_DEV_WIDTH + 1)
#define MDIO45_XPRT_ID_MASK   ((1 << MDIO45_XPRT_ID_WIDTH) - 1)
#define MDIO45_XPRT_ID_IS10G   (1 << (MDIO45_XPRT_ID_WIDTH - 1))


#define MDIO45_PRT_ID_COMP_LBN   MDIO45_DEV_ID_WIDTH
#define MDIO45_PRT_ID_COMP_WIDTH  MDIO45_PRT_ID_WIDTH
#define MDIO45_DEV_ID_COMP_LBN    0
#define MDIO45_DEV_ID_COMP_WIDTH  MDIO45_DEV_ID_WIDTH

/* Compose port and device into a phy_id */
static inline int mdio_clause45_pack(u8 prt, u8 dev)
{
	efx_dword_t phy_id;
	EFX_POPULATE_DWORD_2(phy_id, MDIO45_PRT_ID_COMP, prt,
			     MDIO45_DEV_ID_COMP, dev);
	return MDIO45_XPRT_ID_IS10G | EFX_DWORD_VAL(phy_id);
}

static inline void mdio_clause45_unpack(u32 val, u8 *prt, u8 *dev)
{
	efx_dword_t phy_id;
	EFX_POPULATE_DWORD_1(phy_id, EFX_DWORD_0, val);
	*prt = EFX_DWORD_FIELD(phy_id, MDIO45_PRT_ID_COMP);
	*dev = EFX_DWORD_FIELD(phy_id, MDIO45_DEV_ID_COMP);
}

static inline int mdio_clause45_read(struct efx_nic *efx,
				     u8 prt, u8 dev, u16 addr)
{
	return efx->mii.mdio_read(efx->net_dev,
				  mdio_clause45_pack(prt, dev), addr);
}

static inline void mdio_clause45_write(struct efx_nic *efx,
				       u8 prt, u8 dev, u16 addr, int value)
{
	efx->mii.mdio_write(efx->net_dev,
			    mdio_clause45_pack(prt, dev), addr, value);
}


static inline u32 mdio_clause45_read_id(struct efx_nic *efx, int mmd)
{
	int phy_id = efx->mii.phy_id;
	u16 id_low = mdio_clause45_read(efx, phy_id, mmd, MDIO_MMDREG_IDLOW);
	u16 id_hi = mdio_clause45_read(efx, phy_id, mmd, MDIO_MMDREG_IDHI);
	return (id_hi << 16) | (id_low);
}

static inline bool mdio_clause45_phyxgxs_lane_sync(struct efx_nic *efx)
{
	int i, lane_status;
	bool sync;

	for (i = 0; i < 2; ++i)
		lane_status = mdio_clause45_read(efx, efx->mii.phy_id,
						 MDIO_MMD_PHYXS,
						 MDIO_PHYXS_LANE_STATE);

	sync = !!(lane_status & (1 << MDIO_PHYXS_LANE_ALIGNED_LBN));
	if (!sync)
		EFX_LOG(efx, "XGXS lane status: %x\n", lane_status);
	return sync;
}

extern const char *mdio_clause45_mmd_name(int mmd);

/*
 * Reset a specific MMD and wait for reset to clear.
 * Return number of spins left (>0) on success, -%ETIMEDOUT on failure.
 *
 * This function will sleep
 */
extern int mdio_clause45_reset_mmd(struct efx_nic *efx, int mmd,
				   int spins, int spintime);

/* As mdio_clause45_check_mmd but for multiple MMDs */
int mdio_clause45_check_mmds(struct efx_nic *efx,
			     unsigned int mmd_mask, unsigned int fatal_mask);

/* Check the link status of specified mmds in bit mask */
extern bool mdio_clause45_links_ok(struct efx_nic *efx,
				   unsigned int mmd_mask);

/* Generic transmit disable support though PMAPMD */
extern void mdio_clause45_transmit_disable(struct efx_nic *efx);

/* Generic part of reconfigure: set/clear loopback bits */
extern void mdio_clause45_phy_reconfigure(struct efx_nic *efx);

/* Read (some of) the PHY settings over MDIO */
extern void mdio_clause45_get_settings(struct efx_nic *efx,
				       struct ethtool_cmd *ecmd);

/* Set (some of) the PHY settings over MDIO */
extern int mdio_clause45_set_settings(struct efx_nic *efx,
				      struct ethtool_cmd *ecmd);

/* Wait for specified MMDs to exit reset within a timeout */
extern int mdio_clause45_wait_reset_mmds(struct efx_nic *efx,
					 unsigned int mmd_mask);

#endif /* EFX_MDIO_10G_H */
