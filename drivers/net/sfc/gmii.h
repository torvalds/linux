/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_GMII_H
#define EFX_GMII_H

/*
 * GMII interface
 */

#include <linux/mii.h>

/* GMII registers, excluding registers already defined as MII
 * registers in mii.h
 */
#define GMII_IER		0x12	/* Interrupt enable register */
#define GMII_ISR		0x13	/* Interrupt status register */

/* Interrupt enable register */
#define IER_ANEG_ERR		0x8000	/* Bit 15 - autonegotiation error */
#define IER_SPEED_CHG		0x4000	/* Bit 14 - speed changed */
#define IER_DUPLEX_CHG		0x2000	/* Bit 13 - duplex changed */
#define IER_PAGE_RCVD		0x1000	/* Bit 12 - page received */
#define IER_ANEG_DONE		0x0800	/* Bit 11 - autonegotiation complete */
#define IER_LINK_CHG		0x0400	/* Bit 10 - link status changed */
#define IER_SYM_ERR		0x0200	/* Bit 9 - symbol error */
#define IER_FALSE_CARRIER	0x0100	/* Bit 8 - false carrier */
#define IER_FIFO_ERR		0x0080	/* Bit 7 - FIFO over/underflow */
#define IER_MDIX_CHG		0x0040	/* Bit 6 - MDI crossover changed */
#define IER_DOWNSHIFT		0x0020	/* Bit 5 - downshift */
#define IER_ENERGY		0x0010	/* Bit 4 - energy detect */
#define IER_DTE_POWER		0x0004	/* Bit 2 - DTE power detect */
#define IER_POLARITY_CHG	0x0002	/* Bit 1 - polarity changed */
#define IER_JABBER		0x0001	/* Bit 0 - jabber */

/* Interrupt status register */
#define ISR_ANEG_ERR		0x8000	/* Bit 15 - autonegotiation error */
#define ISR_SPEED_CHG		0x4000	/* Bit 14 - speed changed */
#define ISR_DUPLEX_CHG		0x2000	/* Bit 13 - duplex changed */
#define ISR_PAGE_RCVD		0x1000	/* Bit 12 - page received */
#define ISR_ANEG_DONE		0x0800	/* Bit 11 - autonegotiation complete */
#define ISR_LINK_CHG		0x0400	/* Bit 10 - link status changed */
#define ISR_SYM_ERR		0x0200	/* Bit 9 - symbol error */
#define ISR_FALSE_CARRIER	0x0100	/* Bit 8 - false carrier */
#define ISR_FIFO_ERR		0x0080	/* Bit 7 - FIFO over/underflow */
#define ISR_MDIX_CHG		0x0040	/* Bit 6 - MDI crossover changed */
#define ISR_DOWNSHIFT		0x0020	/* Bit 5 - downshift */
#define ISR_ENERGY		0x0010	/* Bit 4 - energy detect */
#define ISR_DTE_POWER		0x0004	/* Bit 2 - DTE power detect */
#define ISR_POLARITY_CHG	0x0002	/* Bit 1 - polarity changed */
#define ISR_JABBER		0x0001	/* Bit 0 - jabber */

/* Logically extended advertisement register */
#define GM_ADVERTISE_SLCT		ADVERTISE_SLCT
#define GM_ADVERTISE_CSMA		ADVERTISE_CSMA
#define GM_ADVERTISE_10HALF		ADVERTISE_10HALF
#define GM_ADVERTISE_1000XFULL		ADVERTISE_1000XFULL
#define GM_ADVERTISE_10FULL		ADVERTISE_10FULL
#define GM_ADVERTISE_1000XHALF		ADVERTISE_1000XHALF
#define GM_ADVERTISE_100HALF		ADVERTISE_100HALF
#define GM_ADVERTISE_1000XPAUSE		ADVERTISE_1000XPAUSE
#define GM_ADVERTISE_100FULL		ADVERTISE_100FULL
#define GM_ADVERTISE_1000XPSE_ASYM	ADVERTISE_1000XPSE_ASYM
#define GM_ADVERTISE_100BASE4		ADVERTISE_100BASE4
#define GM_ADVERTISE_PAUSE_CAP		ADVERTISE_PAUSE_CAP
#define GM_ADVERTISE_PAUSE_ASYM		ADVERTISE_PAUSE_ASYM
#define GM_ADVERTISE_RESV		ADVERTISE_RESV
#define GM_ADVERTISE_RFAULT		ADVERTISE_RFAULT
#define GM_ADVERTISE_LPACK		ADVERTISE_LPACK
#define GM_ADVERTISE_NPAGE		ADVERTISE_NPAGE
#define GM_ADVERTISE_1000FULL		(ADVERTISE_1000FULL << 8)
#define GM_ADVERTISE_1000HALF		(ADVERTISE_1000HALF << 8)
#define GM_ADVERTISE_1000		(GM_ADVERTISE_1000FULL | \
					 GM_ADVERTISE_1000HALF)
#define GM_ADVERTISE_FULL		(GM_ADVERTISE_1000FULL | \
					 ADVERTISE_FULL)
#define GM_ADVERTISE_ALL		(GM_ADVERTISE_1000FULL | \
					 GM_ADVERTISE_1000HALF | \
					 ADVERTISE_ALL)

/* Logically extended link partner ability register */
#define GM_LPA_SLCT			LPA_SLCT
#define GM_LPA_10HALF			LPA_10HALF
#define GM_LPA_1000XFULL		LPA_1000XFULL
#define GM_LPA_10FULL			LPA_10FULL
#define GM_LPA_1000XHALF		LPA_1000XHALF
#define GM_LPA_100HALF			LPA_100HALF
#define GM_LPA_1000XPAUSE		LPA_1000XPAUSE
#define GM_LPA_100FULL			LPA_100FULL
#define GM_LPA_1000XPAUSE_ASYM		LPA_1000XPAUSE_ASYM
#define GM_LPA_100BASE4			LPA_100BASE4
#define GM_LPA_PAUSE_CAP		LPA_PAUSE_CAP
#define GM_LPA_PAUSE_ASYM		LPA_PAUSE_ASYM
#define GM_LPA_RESV			LPA_RESV
#define GM_LPA_RFAULT			LPA_RFAULT
#define GM_LPA_LPACK			LPA_LPACK
#define GM_LPA_NPAGE			LPA_NPAGE
#define GM_LPA_1000FULL			(LPA_1000FULL << 6)
#define GM_LPA_1000HALF			(LPA_1000HALF << 6)
#define GM_LPA_10000FULL		0x00040000
#define GM_LPA_10000HALF		0x00080000
#define GM_LPA_DUPLEX			(GM_LPA_1000FULL | GM_LPA_10000FULL \
					 | LPA_DUPLEX)
#define GM_LPA_10			(LPA_10FULL | LPA_10HALF)
#define GM_LPA_100			LPA_100
#define GM_LPA_1000			(GM_LPA_1000FULL | GM_LPA_1000HALF)
#define GM_LPA_10000			(GM_LPA_10000FULL | GM_LPA_10000HALF)

/* Retrieve GMII autonegotiation advertised abilities
 *
 * The MII advertisment register (MII_ADVERTISE) is logically extended
 * to include advertisement bits ADVERTISE_1000FULL and
 * ADVERTISE_1000HALF from MII_CTRL1000.  The result can be tested
 * against the GM_ADVERTISE_xxx constants.
 */
static inline unsigned int gmii_advertised(struct mii_if_info *gmii)
{
	unsigned int advertise;
	unsigned int ctrl1000;

	advertise = gmii->mdio_read(gmii->dev, gmii->phy_id, MII_ADVERTISE);
	ctrl1000 = gmii->mdio_read(gmii->dev, gmii->phy_id, MII_CTRL1000);
	return (((ctrl1000 << 8) & GM_ADVERTISE_1000) | advertise);
}

/* Retrieve GMII autonegotiation link partner abilities
 *
 * The MII link partner ability register (MII_LPA) is logically
 * extended by adding bits LPA_1000HALF and LPA_1000FULL from
 * MII_STAT1000.  The result can be tested against the GM_LPA_xxx
 * constants.
 */
static inline unsigned int gmii_lpa(struct mii_if_info *gmii)
{
	unsigned int lpa;
	unsigned int stat1000;

	lpa = gmii->mdio_read(gmii->dev, gmii->phy_id, MII_LPA);
	stat1000 = gmii->mdio_read(gmii->dev, gmii->phy_id, MII_STAT1000);
	return (((stat1000 << 6) & GM_LPA_1000) | lpa);
}

/* Calculate GMII autonegotiated link technology
 *
 * "negotiated" should be the result of gmii_advertised() logically
 * ANDed with the result of gmii_lpa().
 *
 * "tech" will be negotiated with the unused bits masked out.  For
 * example, if both ends of the link are capable of both
 * GM_LPA_1000FULL and GM_LPA_100FULL, GM_LPA_100FULL will be masked
 * out.
 */
static inline unsigned int gmii_nway_result(unsigned int negotiated)
{
	unsigned int other_bits;

	/* Mask out the speed and duplexity bits */
	other_bits = negotiated & ~(GM_LPA_10 | GM_LPA_100 | GM_LPA_1000);

	if (negotiated & GM_LPA_1000FULL)
		return (other_bits | GM_LPA_1000FULL);
	else if (negotiated & GM_LPA_1000HALF)
		return (other_bits | GM_LPA_1000HALF);
	else
		return (other_bits | mii_nway_result(negotiated));
}

/* Calculate GMII non-autonegotiated link technology
 *
 * This provides an equivalent to gmii_nway_result for the case when
 * autonegotiation is disabled.
 */
static inline unsigned int gmii_forced_result(unsigned int bmcr)
{
	unsigned int result;
	int full_duplex;

	full_duplex = bmcr & BMCR_FULLDPLX;
	if (bmcr & BMCR_SPEED1000)
		result = full_duplex ? GM_LPA_1000FULL : GM_LPA_1000HALF;
	else if (bmcr & BMCR_SPEED100)
		result = full_duplex ? GM_LPA_100FULL : GM_LPA_100HALF;
	else
		result = full_duplex ? GM_LPA_10FULL : GM_LPA_10HALF;
	return result;
}

#endif /* EFX_GMII_H */
