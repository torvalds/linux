/*
	drivers/net/tulip/media.c

	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please refer to Documentation/DocBook/tulip-user.{pdf,ps,html}
	for more information on this driver.

	Please submit bugs to http://bugzilla.kernel.org/ .
*/

#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "tulip.h"


/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() ioread32(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK		0x10000
#define MDIO_DATA_WRITE0	0x00000
#define MDIO_DATA_WRITE1	0x20000
#define MDIO_ENB		0x00000 /* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ		0x80000

static const unsigned char comet_miireg2offset[32] = {
	0xB4, 0xB8, 0xBC, 0xC0,  0xC4, 0xC8, 0xCC, 0,  0,0,0,0,  0,0,0,0,
	0,0xD0,0,0,  0,0,0,0,  0,0,0,0, 0, 0xD4, 0xD8, 0xDC, };


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.
   See IEEE 802.3-2002.pdf (Section 2, Chapter "22.2.4 Management functions")
   or DP83840A data sheet for more details.
   */

int tulip_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct tulip_private *tp = netdev_priv(dev);
	int i;
	int read_cmd = (0xf6 << 10) | ((phy_id & 0x1f) << 5) | location;
	int retval = 0;
	void __iomem *ioaddr = tp->base_addr;
	void __iomem *mdio_addr = ioaddr + CSR9;
	unsigned long flags;

	if (location & ~0x1f)
		return 0xffff;

	if (tp->chip_id == COMET  &&  phy_id == 30) {
		if (comet_miireg2offset[location])
			return ioread32(ioaddr + comet_miireg2offset[location]);
		return 0xffff;
	}

	spin_lock_irqsave(&tp->mii_lock, flags);
	if (tp->chip_id == LC82C168) {
		iowrite32(0x60020000 + (phy_id<<23) + (location<<18), ioaddr + 0xA0);
		ioread32(ioaddr + 0xA0);
		ioread32(ioaddr + 0xA0);
		for (i = 1000; i >= 0; --i) {
			barrier();
			if ( ! ((retval = ioread32(ioaddr + 0xA0)) & 0x80000000))
				break;
		}
		spin_unlock_irqrestore(&tp->mii_lock, flags);
		return retval & 0xffff;
	}

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		iowrite32(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		iowrite32(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		iowrite32(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		iowrite32(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		iowrite32(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((ioread32(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		iowrite32(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}

	spin_unlock_irqrestore(&tp->mii_lock, flags);
	return (retval>>1) & 0xffff;
}

void tulip_mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	struct tulip_private *tp = netdev_priv(dev);
	int i;
	int cmd = (0x5002 << 16) | ((phy_id & 0x1f) << 23) | (location<<18) | (val & 0xffff);
	void __iomem *ioaddr = tp->base_addr;
	void __iomem *mdio_addr = ioaddr + CSR9;
	unsigned long flags;

	if (location & ~0x1f)
		return;

	if (tp->chip_id == COMET && phy_id == 30) {
		if (comet_miireg2offset[location])
			iowrite32(val, ioaddr + comet_miireg2offset[location]);
		return;
	}

	spin_lock_irqsave(&tp->mii_lock, flags);
	if (tp->chip_id == LC82C168) {
		iowrite32(cmd, ioaddr + 0xA0);
		for (i = 1000; i >= 0; --i) {
			barrier();
			if ( ! (ioread32(ioaddr + 0xA0) & 0x80000000))
				break;
		}
		spin_unlock_irqrestore(&tp->mii_lock, flags);
		return;
	}

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		iowrite32(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		iowrite32(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		iowrite32(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		iowrite32(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		iowrite32(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		iowrite32(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}

	spin_unlock_irqrestore(&tp->mii_lock, flags);
}


/* Set up the transceiver control registers for the selected media type. */
void tulip_select_media(struct net_device *dev, int startup)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	struct mediatable *mtable = tp->mtable;
	u32 new_csr6;
	int i;

	if (mtable) {
		struct medialeaf *mleaf = &mtable->mleaf[tp->cur_index];
		unsigned char *p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0:					/* 21140 non-MII xcvr. */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: Using a 21140 non-MII transceiver"
					   " with control setting %2.2x.\n",
					   dev->name, p[1]);
			dev->if_port = p[0];
			if (startup)
				iowrite32(mtable->csr12dir | 0x100, ioaddr + CSR12);
			iowrite32(p[1], ioaddr + CSR12);
			new_csr6 = 0x02000000 | ((p[2] & 0x71) << 18);
			break;
		case 2: case 4: {
			u16 setup[5];
			u32 csr13val, csr14val, csr15dir, csr15val;
			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			dev->if_port = p[0] & MEDIA_MASK;
			if (tulip_media_cap[dev->if_port] & MediaAlwaysFD)
				tp->full_duplex = 1;

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Resetting the transceiver.\n",
						   dev->name);
				for (i = 0; i < rst[0]; i++)
					iowrite32(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: 21143 non-MII %s transceiver control "
					   "%4.4x/%4.4x.\n",
					   dev->name, medianame[dev->if_port], setup[0], setup[1]);
			if (p[0] & 0x40) {	/* SIA (CSR13-15) setup values are provided. */
				csr13val = setup[0];
				csr14val = setup[1];
				csr15dir = (setup[3]<<16) | setup[2];
				csr15val = (setup[4]<<16) | setup[2];
				iowrite32(0, ioaddr + CSR13);
				iowrite32(csr14val, ioaddr + CSR14);
				iowrite32(csr15dir, ioaddr + CSR15);	/* Direction */
				iowrite32(csr15val, ioaddr + CSR15);	/* Data */
				iowrite32(csr13val, ioaddr + CSR13);
			} else {
				csr13val = 1;
				csr14val = 0;
				csr15dir = (setup[0]<<16) | 0x0008;
				csr15val = (setup[1]<<16) | 0x0008;
				if (dev->if_port <= 4)
					csr14val = t21142_csr14[dev->if_port];
				if (startup) {
					iowrite32(0, ioaddr + CSR13);
					iowrite32(csr14val, ioaddr + CSR14);
				}
				iowrite32(csr15dir, ioaddr + CSR15);	/* Direction */
				iowrite32(csr15val, ioaddr + CSR15);	/* Data */
				if (startup) iowrite32(csr13val, ioaddr + CSR13);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s:  Setting CSR15 to %8.8x/%8.8x.\n",
					   dev->name, csr15dir, csr15val);
			if (mleaf->type == 4)
				new_csr6 = 0x82020000 | ((setup[2] & 0x71) << 18);
			else
				new_csr6 = 0x82420000;
			break;
		}
		case 1: case 3: {
			int phy_num = p[0];
			int init_length = p[1];
			u16 *misc_info, tmp_info;

			dev->if_port = 11;
			new_csr6 = 0x020E0000;
			if (mleaf->type == 3) {	/* 21142 */
				u16 *init_sequence = (u16*)(p+2);
				u16 *reset_sequence = &((u16*)(p+3))[init_length];
				int reset_length = p[2 + init_length*2];
				misc_info = reset_sequence + reset_length;
				if (startup) {
					int timeout = 10;	/* max 1 ms */
					for (i = 0; i < reset_length; i++)
						iowrite32(get_u16(&reset_sequence[i]) << 16, ioaddr + CSR15);

					/* flush posted writes */
					ioread32(ioaddr + CSR15);

					/* Sect 3.10.3 in DP83840A.pdf (p39) */
					udelay(500);

					/* Section 4.2 in DP83840A.pdf (p43) */
					/* and IEEE 802.3 "22.2.4.1.1 Reset" */
					while (timeout-- &&
						(tulip_mdio_read (dev, phy_num, MII_BMCR) & BMCR_RESET))
						udelay(100);
				}
				for (i = 0; i < init_length; i++)
					iowrite32(get_u16(&init_sequence[i]) << 16, ioaddr + CSR15);

				ioread32(ioaddr + CSR15);	/* flush posted writes */
			} else {
				u8 *init_sequence = p + 2;
				u8 *reset_sequence = p + 3 + init_length;
				int reset_length = p[2 + init_length];
				misc_info = (u16*)(reset_sequence + reset_length);
				if (startup) {
					int timeout = 10;	/* max 1 ms */
					iowrite32(mtable->csr12dir | 0x100, ioaddr + CSR12);
					for (i = 0; i < reset_length; i++)
						iowrite32(reset_sequence[i], ioaddr + CSR12);

					/* flush posted writes */
					ioread32(ioaddr + CSR12);

					/* Sect 3.10.3 in DP83840A.pdf (p39) */
					udelay(500);

					/* Section 4.2 in DP83840A.pdf (p43) */
					/* and IEEE 802.3 "22.2.4.1.1 Reset" */
					while (timeout-- &&
						(tulip_mdio_read (dev, phy_num, MII_BMCR) & BMCR_RESET))
						udelay(100);
				}
				for (i = 0; i < init_length; i++)
					iowrite32(init_sequence[i], ioaddr + CSR12);

				ioread32(ioaddr + CSR12);	/* flush posted writes */
			}

			tmp_info = get_u16(&misc_info[1]);
			if (tmp_info)
				tp->advertising[phy_num] = tmp_info | 1;
			if (tmp_info && startup < 2) {
				if (tp->mii_advertise == 0)
					tp->mii_advertise = tp->advertising[phy_num];
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s:  Advertising %4.4x on MII %d.\n",
					       dev->name, tp->mii_advertise, tp->phys[phy_num]);
				tulip_mdio_write(dev, tp->phys[phy_num], 4, tp->mii_advertise);
			}
			break;
		}
		case 5: case 6: {
			u16 setup[5];

			new_csr6 = 0; /* FIXME */

			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Resetting the transceiver.\n",
						   dev->name);
				for (i = 0; i < rst[0]; i++)
					iowrite32(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}

			break;
		}
		default:
			printk(KERN_DEBUG "%s:  Invalid media table selection %d.\n",
					   dev->name, mleaf->type);
			new_csr6 = 0x020E0000;
		}
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: Using media type %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   ioread32(ioaddr + CSR12) & 0xff);
	} else if (tp->chip_id == LC82C168) {
		if (startup && ! tp->medialock)
			dev->if_port = tp->mii_cnt ? 11 : 0;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC PHY status is %3.3x, media %s.\n",
				   dev->name, ioread32(ioaddr + 0xB8), medianame[dev->if_port]);
		if (tp->mii_cnt) {
			new_csr6 = 0x810C0000;
			iowrite32(0x0001, ioaddr + CSR15);
			iowrite32(0x0201B07A, ioaddr + 0xB8);
		} else if (startup) {
			/* Start with 10mbps to do autonegotiation. */
			iowrite32(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			iowrite32(0x0001B078, ioaddr + 0xB8);
			iowrite32(0x0201B078, ioaddr + 0xB8);
		} else if (dev->if_port == 3  ||  dev->if_port == 5) {
			iowrite32(0x33, ioaddr + CSR12);
			new_csr6 = 0x01860000;
			/* Trigger autonegotiation. */
			iowrite32(startup ? 0x0201F868 : 0x0001F868, ioaddr + 0xB8);
		} else {
			iowrite32(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			iowrite32(0x1F078, ioaddr + 0xB8);
		}
	} else {					/* Unknown chip type with no media table. */
		if (tp->default_port == 0)
			dev->if_port = tp->mii_cnt ? 11 : 3;
		if (tulip_media_cap[dev->if_port] & MediaIsMII) {
			new_csr6 = 0x020E0000;
		} else if (tulip_media_cap[dev->if_port] & MediaIsFx) {
			new_csr6 = 0x02860000;
		} else
			new_csr6 = 0x03860000;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: No media description table, assuming "
				   "%s transceiver, CSR12 %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   ioread32(ioaddr + CSR12));
	}

	tp->csr6 = new_csr6 | (tp->csr6 & 0xfdff) | (tp->full_duplex ? 0x0200 : 0);

	mdelay(1);

	return;
}

/*
  Check the MII negotiated duplex and change the CSR6 setting if
  required.
  Return 0 if everything is OK.
  Return < 0 if the transceiver is missing or has no link beat.
  */
int tulip_check_duplex(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	unsigned int bmsr, lpa, negotiated, new_csr6;

	bmsr = tulip_mdio_read(dev, tp->phys[0], MII_BMSR);
	lpa = tulip_mdio_read(dev, tp->phys[0], MII_LPA);
	if (tulip_debug > 1)
		printk(KERN_INFO "%s: MII status %4.4x, Link partner report "
			   "%4.4x.\n", dev->name, bmsr, lpa);
	if (bmsr == 0xffff)
		return -2;
	if ((bmsr & BMSR_LSTATUS) == 0) {
		int new_bmsr = tulip_mdio_read(dev, tp->phys[0], MII_BMSR);
		if ((new_bmsr & BMSR_LSTATUS) == 0) {
			if (tulip_debug  > 1)
				printk(KERN_INFO "%s: No link beat on the MII interface,"
					   " status %4.4x.\n", dev->name, new_bmsr);
			return -1;
		}
	}
	negotiated = lpa & tp->advertising[0];
	tp->full_duplex = mii_duplex(tp->full_duplex_lock, negotiated);

	new_csr6 = tp->csr6;

	if (negotiated & LPA_100) new_csr6 &= ~TxThreshold;
	else			  new_csr6 |= TxThreshold;
	if (tp->full_duplex) new_csr6 |= FullDuplex;
	else		     new_csr6 &= ~FullDuplex;

	if (new_csr6 != tp->csr6) {
		tp->csr6 = new_csr6;
		tulip_restart_rxtx(tp);

		if (tulip_debug > 0)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII"
				   "#%d link partner capability of %4.4x.\n",
				   dev->name, tp->full_duplex ? "full" : "half",
				   tp->phys[0], lpa);
		return 1;
	}

	return 0;
}

void __devinit tulip_find_mii (struct net_device *dev, int board_idx)
{
	struct tulip_private *tp = netdev_priv(dev);
	int phyn, phy_idx = 0;
	int mii_reg0;
	int mii_advert;
	unsigned int to_advert, new_bmcr, ane_switch;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later,
	   but takes much time. */
	for (phyn = 1; phyn <= 32 && phy_idx < sizeof (tp->phys); phyn++) {
		int phy = phyn & 0x1f;
		int mii_status = tulip_mdio_read (dev, phy, MII_BMSR);
		if ((mii_status & 0x8301) == 0x8001 ||
		    ((mii_status & BMSR_100BASE4) == 0
		     && (mii_status & 0x7800) != 0)) {
			/* preserve Becker logic, gain indentation level */
		} else {
			continue;
		}

		mii_reg0 = tulip_mdio_read (dev, phy, MII_BMCR);
		mii_advert = tulip_mdio_read (dev, phy, MII_ADVERTISE);
		ane_switch = 0;

		/* if not advertising at all, gen an
		 * advertising value from the capability
		 * bits in BMSR
		 */
		if ((mii_advert & ADVERTISE_ALL) == 0) {
			unsigned int tmpadv = tulip_mdio_read (dev, phy, MII_BMSR);
			mii_advert = ((tmpadv >> 6) & 0x3e0) | 1;
		}

		if (tp->mii_advertise) {
			tp->advertising[phy_idx] =
			to_advert = tp->mii_advertise;
		} else if (tp->advertising[phy_idx]) {
			to_advert = tp->advertising[phy_idx];
		} else {
			tp->advertising[phy_idx] =
			tp->mii_advertise =
			to_advert = mii_advert;
		}

		tp->phys[phy_idx++] = phy;

		printk (KERN_INFO "tulip%d:  MII transceiver #%d "
			"config %4.4x status %4.4x advertising %4.4x.\n",
			board_idx, phy, mii_reg0, mii_status, mii_advert);

		/* Fixup for DLink with miswired PHY. */
		if (mii_advert != to_advert) {
			printk (KERN_DEBUG "tulip%d:  Advertising %4.4x on PHY %d,"
				" previously advertising %4.4x.\n",
				board_idx, to_advert, phy, mii_advert);
			tulip_mdio_write (dev, phy, 4, to_advert);
		}

		/* Enable autonegotiation: some boards default to off. */
		if (tp->default_port == 0) {
			new_bmcr = mii_reg0 | BMCR_ANENABLE;
			if (new_bmcr != mii_reg0) {
				new_bmcr |= BMCR_ANRESTART;
				ane_switch = 1;
			}
		}
		/* ...or disable nway, if forcing media */
		else {
			new_bmcr = mii_reg0 & ~BMCR_ANENABLE;
			if (new_bmcr != mii_reg0)
				ane_switch = 1;
		}

		/* clear out bits we never want at this point */
		new_bmcr &= ~(BMCR_CTST | BMCR_FULLDPLX | BMCR_ISOLATE |
			      BMCR_PDOWN | BMCR_SPEED100 | BMCR_LOOPBACK |
			      BMCR_RESET);

		if (tp->full_duplex)
			new_bmcr |= BMCR_FULLDPLX;
		if (tulip_media_cap[tp->default_port] & MediaIs100)
			new_bmcr |= BMCR_SPEED100;

		if (new_bmcr != mii_reg0) {
			/* some phys need the ANE switch to
			 * happen before forced media settings
			 * will "take."  However, we write the
			 * same value twice in order not to
			 * confuse the sane phys.
			 */
			if (ane_switch) {
				tulip_mdio_write (dev, phy, MII_BMCR, new_bmcr);
				udelay (10);
			}
			tulip_mdio_write (dev, phy, MII_BMCR, new_bmcr);
		}
	}
	tp->mii_cnt = phy_idx;
	if (tp->mtable && tp->mtable->has_mii && phy_idx == 0) {
		printk (KERN_INFO "tulip%d: ***WARNING***: No MII transceiver found!\n",
			board_idx);
		tp->phys[0] = 1;
	}
}
