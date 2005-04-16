
/*
 * ibm_emac_phy.h
 *
 *
 *      Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *      February 2003
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * This file basically duplicates sungem_phy.{c,h} with different PHYs
 * supported. I'm looking into merging that in a single mii layer more
 * flexible than mii.c 
 */

#ifndef _IBM_EMAC_PHY_H_
#define _IBM_EMAC_PHY_H_

/*
 * PHY mode settings
 * Used for multi-mode capable PHYs
 */
#define PHY_MODE_NA	0
#define PHY_MODE_MII	1
#define PHY_MODE_RMII	2
#define PHY_MODE_SMII	3
#define PHY_MODE_RGMII	4
#define PHY_MODE_TBI	5
#define PHY_MODE_GMII	6
#define PHY_MODE_RTBI	7
#define PHY_MODE_SGMII	8

/*
 * PHY specific registers/values
 */

/* CIS8201 */
#define MII_CIS8201_EPCR	0x17
#define EPCR_MODE_MASK		0x3000
#define EPCR_GMII_MODE		0x0000
#define EPCR_RGMII_MODE		0x1000
#define EPCR_TBI_MODE		0x2000
#define EPCR_RTBI_MODE		0x3000

struct mii_phy;

/* Operations supported by any kind of PHY */
struct mii_phy_ops {
	int (*init) (struct mii_phy * phy);
	int (*suspend) (struct mii_phy * phy, int wol_options);
	int (*setup_aneg) (struct mii_phy * phy, u32 advertise);
	int (*setup_forced) (struct mii_phy * phy, int speed, int fd);
	int (*poll_link) (struct mii_phy * phy);
	int (*read_link) (struct mii_phy * phy);
};

/* Structure used to statically define an mii/gii based PHY */
struct mii_phy_def {
	u32 phy_id;		/* Concatenated ID1 << 16 | ID2 */
	u32 phy_id_mask;	/* Significant bits */
	u32 features;		/* Ethtool SUPPORTED_* defines */
	int magic_aneg;		/* Autoneg does all speed test for us */
	const char *name;
	const struct mii_phy_ops *ops;
};

/* An instance of a PHY, partially borrowed from mii_if_info */
struct mii_phy {
	struct mii_phy_def *def;
	int advertising;
	int mii_id;

	/* 1: autoneg enabled, 0: disabled */
	int autoneg;

	/* forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int pause;

	/* PHY mode - if needed */
	int mode;

	/* Provided by host chip */
	struct net_device *dev;
	int (*mdio_read) (struct net_device * dev, int mii_id, int reg);
	void (*mdio_write) (struct net_device * dev, int mii_id, int reg,
			    int val);
};

/* Pass in a struct mii_phy with dev, mdio_read and mdio_write
 * filled, the remaining fields will be filled on return
 */
extern int mii_phy_probe(struct mii_phy *phy, int mii_id);

static inline int __phy_read(struct mii_phy *phy, int id, int reg)
{
	return phy->mdio_read(phy->dev, id, reg);
}

static inline void __phy_write(struct mii_phy *phy, int id, int reg, int val)
{
	phy->mdio_write(phy->dev, id, reg, val);
}

static inline int phy_read(struct mii_phy *phy, int reg)
{
	return phy->mdio_read(phy->dev, phy->mii_id, reg);
}

static inline void phy_write(struct mii_phy *phy, int reg, int val)
{
	phy->mdio_write(phy->dev, phy->mii_id, reg, val);
}

#endif				/* _IBM_EMAC_PHY_H_ */
