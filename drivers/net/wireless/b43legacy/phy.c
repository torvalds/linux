/*

  Broadcom B43legacy wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
		     Stefano Brivio <st3@riseup.net>
		     Michael Buesch <mbuesch@freenet.de>
		     Danny van Dyk <kugelfang@gentoo.org>
     Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2007 Larry Finger <Larry.Finger@lwfinger.net>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "b43legacy.h"
#include "phy.h"
#include "main.h"
#include "radio.h"
#include "ilt.h"


static const s8 b43legacy_tssi2dbm_b_table[] = {
	0x4D, 0x4C, 0x4B, 0x4A,
	0x4A, 0x49, 0x48, 0x47,
	0x47, 0x46, 0x45, 0x45,
	0x44, 0x43, 0x42, 0x42,
	0x41, 0x40, 0x3F, 0x3E,
	0x3D, 0x3C, 0x3B, 0x3A,
	0x39, 0x38, 0x37, 0x36,
	0x35, 0x34, 0x32, 0x31,
	0x30, 0x2F, 0x2D, 0x2C,
	0x2B, 0x29, 0x28, 0x26,
	0x25, 0x23, 0x21, 0x1F,
	0x1D, 0x1A, 0x17, 0x14,
	0x10, 0x0C, 0x06, 0x00,
	  -7,   -7,   -7,   -7,
	  -7,   -7,   -7,   -7,
	  -7,   -7,   -7,   -7,
};

static const s8 b43legacy_tssi2dbm_g_table[] = {
	 77,  77,  77,  76,
	 76,  76,  75,  75,
	 74,  74,  73,  73,
	 73,  72,  72,  71,
	 71,  70,  70,  69,
	 68,  68,  67,  67,
	 66,  65,  65,  64,
	 63,  63,  62,  61,
	 60,  59,  58,  57,
	 56,  55,  54,  53,
	 52,  50,  49,  47,
	 45,  43,  40,  37,
	 33,  28,  22,  14,
	  5,  -7, -20, -20,
	-20, -20, -20, -20,
	-20, -20, -20, -20,
};

static void b43legacy_phy_initg(struct b43legacy_wldev *dev);


static inline
void b43legacy_voluntary_preempt(void)
{
	B43legacy_BUG_ON(!(!in_atomic() && !in_irq() &&
			  !in_interrupt() && !irqs_disabled()));
#ifndef CONFIG_PREEMPT
	cond_resched();
#endif /* CONFIG_PREEMPT */
}

void b43legacy_raw_phy_lock(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	B43legacy_WARN_ON(!irqs_disabled());
	if (b43legacy_read32(dev, B43legacy_MMIO_STATUS_BITFIELD) == 0) {
		phy->locked = 0;
		return;
	}
	if (dev->dev->id.revision < 3) {
		b43legacy_mac_suspend(dev);
		spin_lock(&phy->lock);
	} else {
		if (!b43legacy_is_mode(dev->wl, IEEE80211_IF_TYPE_AP))
			b43legacy_power_saving_ctl_bits(dev, -1, 1);
	}
	phy->locked = 1;
}

void b43legacy_raw_phy_unlock(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	B43legacy_WARN_ON(!irqs_disabled());
	if (dev->dev->id.revision < 3) {
		if (phy->locked) {
			spin_unlock(&phy->lock);
			b43legacy_mac_enable(dev);
		}
	} else {
		if (!b43legacy_is_mode(dev->wl, IEEE80211_IF_TYPE_AP))
			b43legacy_power_saving_ctl_bits(dev, -1, -1);
	}
	phy->locked = 0;
}

u16 b43legacy_phy_read(struct b43legacy_wldev *dev, u16 offset)
{
	b43legacy_write16(dev, B43legacy_MMIO_PHY_CONTROL, offset);
	return b43legacy_read16(dev, B43legacy_MMIO_PHY_DATA);
}

void b43legacy_phy_write(struct b43legacy_wldev *dev, u16 offset, u16 val)
{
	b43legacy_write16(dev, B43legacy_MMIO_PHY_CONTROL, offset);
	mmiowb();
	b43legacy_write16(dev, B43legacy_MMIO_PHY_DATA, val);
}

void b43legacy_phy_calibrate(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	b43legacy_read32(dev, B43legacy_MMIO_STATUS_BITFIELD); /* Dummy read. */
	if (phy->calibrated)
		return;
	if (phy->type == B43legacy_PHYTYPE_G && phy->rev == 1) {
		b43legacy_wireless_core_reset(dev, 0);
		b43legacy_phy_initg(dev);
		b43legacy_wireless_core_reset(dev, B43legacy_TMSLOW_GMODE);
	}
	phy->calibrated = 1;
}

/* intialize B PHY power control
 * as described in http://bcm-specs.sipsolutions.net/InitPowerControl
 */
static void b43legacy_phy_init_pctl(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 saved_batt = 0;
	u16 saved_ratt = 0;
	u16 saved_txctl1 = 0;
	int must_reset_txpower = 0;

	B43legacy_BUG_ON(!(phy->type == B43legacy_PHYTYPE_B ||
			  phy->type == B43legacy_PHYTYPE_G));
	if (is_bcm_board_vendor(dev) &&
	    (dev->dev->bus->boardinfo.type == 0x0416))
		return;

	b43legacy_phy_write(dev, 0x0028, 0x8018);
	b43legacy_write16(dev, 0x03E6, b43legacy_read16(dev, 0x03E6) & 0xFFDF);

	if (phy->type == B43legacy_PHYTYPE_G) {
		if (!phy->gmode)
			return;
		b43legacy_phy_write(dev, 0x047A, 0xC111);
	}
	if (phy->savedpctlreg != 0xFFFF)
		return;
#ifdef CONFIG_B43LEGACY_DEBUG
	if (phy->manual_txpower_control)
		return;
#endif

	if (phy->type == B43legacy_PHYTYPE_B &&
	    phy->rev >= 2 &&
	    phy->radio_ver == 0x2050)
		b43legacy_radio_write16(dev, 0x0076,
					b43legacy_radio_read16(dev, 0x0076)
					| 0x0084);
	else {
		saved_batt = phy->bbatt;
		saved_ratt = phy->rfatt;
		saved_txctl1 = phy->txctl1;
		if ((phy->radio_rev >= 6) && (phy->radio_rev <= 8)
		    && /*FIXME: incomplete specs for 5 < revision < 9 */ 0)
			b43legacy_radio_set_txpower_bg(dev, 0xB, 0x1F, 0);
		else
			b43legacy_radio_set_txpower_bg(dev, 0xB, 9, 0);
		must_reset_txpower = 1;
	}
	b43legacy_dummy_transmission(dev);

	phy->savedpctlreg = b43legacy_phy_read(dev, B43legacy_PHY_G_PCTL);

	if (must_reset_txpower)
		b43legacy_radio_set_txpower_bg(dev, saved_batt, saved_ratt,
					       saved_txctl1);
	else
		b43legacy_radio_write16(dev, 0x0076, b43legacy_radio_read16(dev,
					0x0076) & 0xFF7B);
	b43legacy_radio_clear_tssi(dev);
}

static void b43legacy_phy_agcsetup(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 offset = 0x0000;

	if (phy->rev == 1)
		offset = 0x4C00;

	b43legacy_ilt_write(dev, offset, 0x00FE);
	b43legacy_ilt_write(dev, offset + 1, 0x000D);
	b43legacy_ilt_write(dev, offset + 2, 0x0013);
	b43legacy_ilt_write(dev, offset + 3, 0x0019);

	if (phy->rev == 1) {
		b43legacy_ilt_write(dev, 0x1800, 0x2710);
		b43legacy_ilt_write(dev, 0x1801, 0x9B83);
		b43legacy_ilt_write(dev, 0x1802, 0x9B83);
		b43legacy_ilt_write(dev, 0x1803, 0x0F8D);
		b43legacy_phy_write(dev, 0x0455, 0x0004);
	}

	b43legacy_phy_write(dev, 0x04A5, (b43legacy_phy_read(dev, 0x04A5)
					  & 0x00FF) | 0x5700);
	b43legacy_phy_write(dev, 0x041A, (b43legacy_phy_read(dev, 0x041A)
					  & 0xFF80) | 0x000F);
	b43legacy_phy_write(dev, 0x041A, (b43legacy_phy_read(dev, 0x041A)
					  & 0xC07F) | 0x2B80);
	b43legacy_phy_write(dev, 0x048C, (b43legacy_phy_read(dev, 0x048C)
					  & 0xF0FF) | 0x0300);

	b43legacy_radio_write16(dev, 0x007A,
				b43legacy_radio_read16(dev, 0x007A)
				| 0x0008);

	b43legacy_phy_write(dev, 0x04A0, (b43legacy_phy_read(dev, 0x04A0)
			    & 0xFFF0) | 0x0008);
	b43legacy_phy_write(dev, 0x04A1, (b43legacy_phy_read(dev, 0x04A1)
			    & 0xF0FF) | 0x0600);
	b43legacy_phy_write(dev, 0x04A2, (b43legacy_phy_read(dev, 0x04A2)
			    & 0xF0FF) | 0x0700);
	b43legacy_phy_write(dev, 0x04A0, (b43legacy_phy_read(dev, 0x04A0)
			    & 0xF0FF) | 0x0100);

	if (phy->rev == 1)
		b43legacy_phy_write(dev, 0x04A2,
				    (b43legacy_phy_read(dev, 0x04A2)
				    & 0xFFF0) | 0x0007);

	b43legacy_phy_write(dev, 0x0488, (b43legacy_phy_read(dev, 0x0488)
			    & 0xFF00) | 0x001C);
	b43legacy_phy_write(dev, 0x0488, (b43legacy_phy_read(dev, 0x0488)
			    & 0xC0FF) | 0x0200);
	b43legacy_phy_write(dev, 0x0496, (b43legacy_phy_read(dev, 0x0496)
			    & 0xFF00) | 0x001C);
	b43legacy_phy_write(dev, 0x0489, (b43legacy_phy_read(dev, 0x0489)
			    & 0xFF00) | 0x0020);
	b43legacy_phy_write(dev, 0x0489, (b43legacy_phy_read(dev, 0x0489)
			    & 0xC0FF) | 0x0200);
	b43legacy_phy_write(dev, 0x0482, (b43legacy_phy_read(dev, 0x0482)
			    & 0xFF00) | 0x002E);
	b43legacy_phy_write(dev, 0x0496, (b43legacy_phy_read(dev, 0x0496)
			    & 0x00FF) | 0x1A00);
	b43legacy_phy_write(dev, 0x0481, (b43legacy_phy_read(dev, 0x0481)
			    & 0xFF00) | 0x0028);
	b43legacy_phy_write(dev, 0x0481, (b43legacy_phy_read(dev, 0x0481)
			    & 0x00FF) | 0x2C00);

	if (phy->rev == 1) {
		b43legacy_phy_write(dev, 0x0430, 0x092B);
		b43legacy_phy_write(dev, 0x041B,
				    (b43legacy_phy_read(dev, 0x041B)
				    & 0xFFE1) | 0x0002);
	} else {
		b43legacy_phy_write(dev, 0x041B,
				    b43legacy_phy_read(dev, 0x041B) & 0xFFE1);
		b43legacy_phy_write(dev, 0x041F, 0x287A);
		b43legacy_phy_write(dev, 0x0420,
				    (b43legacy_phy_read(dev, 0x0420)
				    & 0xFFF0) | 0x0004);
	}

	if (phy->rev > 2) {
		b43legacy_phy_write(dev, 0x0422, 0x287A);
		b43legacy_phy_write(dev, 0x0420,
				    (b43legacy_phy_read(dev, 0x0420)
				    & 0x0FFF) | 0x3000);
	}

	b43legacy_phy_write(dev, 0x04A8, (b43legacy_phy_read(dev, 0x04A8)
			    & 0x8080) | 0x7874);
	b43legacy_phy_write(dev, 0x048E, 0x1C00);

	if (phy->rev == 1) {
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB)
				    & 0xF0FF) | 0x0600);
		b43legacy_phy_write(dev, 0x048B, 0x005E);
		b43legacy_phy_write(dev, 0x048C,
				    (b43legacy_phy_read(dev, 0x048C) & 0xFF00)
				    | 0x001E);
		b43legacy_phy_write(dev, 0x048D, 0x0002);
	}

	b43legacy_ilt_write(dev, offset + 0x0800, 0);
	b43legacy_ilt_write(dev, offset + 0x0801, 7);
	b43legacy_ilt_write(dev, offset + 0x0802, 16);
	b43legacy_ilt_write(dev, offset + 0x0803, 28);

	if (phy->rev >= 6) {
		b43legacy_phy_write(dev, 0x0426,
				    (b43legacy_phy_read(dev, 0x0426) & 0xFFFC));
		b43legacy_phy_write(dev, 0x0426,
				    (b43legacy_phy_read(dev, 0x0426) & 0xEFFF));
	}
}

static void b43legacy_phy_setupg(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 i;

	B43legacy_BUG_ON(phy->type != B43legacy_PHYTYPE_G);
	if (phy->rev == 1) {
		b43legacy_phy_write(dev, 0x0406, 0x4F19);
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
				    (b43legacy_phy_read(dev,
				    B43legacy_PHY_G_CRS) & 0xFC3F) | 0x0340);
		b43legacy_phy_write(dev, 0x042C, 0x005A);
		b43legacy_phy_write(dev, 0x0427, 0x001A);

		for (i = 0; i < B43legacy_ILT_FINEFREQG_SIZE; i++)
			b43legacy_ilt_write(dev, 0x5800 + i,
					    b43legacy_ilt_finefreqg[i]);
		for (i = 0; i < B43legacy_ILT_NOISEG1_SIZE; i++)
			b43legacy_ilt_write(dev, 0x1800 + i,
					    b43legacy_ilt_noiseg1[i]);
		for (i = 0; i < B43legacy_ILT_ROTOR_SIZE; i++)
			b43legacy_ilt_write32(dev, 0x2000 + i,
					      b43legacy_ilt_rotor[i]);
	} else {
		/* nrssi values are signed 6-bit values. Why 0x7654 here? */
		b43legacy_nrssi_hw_write(dev, 0xBA98, (s16)0x7654);

		if (phy->rev == 2) {
			b43legacy_phy_write(dev, 0x04C0, 0x1861);
			b43legacy_phy_write(dev, 0x04C1, 0x0271);
		} else if (phy->rev > 2) {
			b43legacy_phy_write(dev, 0x04C0, 0x0098);
			b43legacy_phy_write(dev, 0x04C1, 0x0070);
			b43legacy_phy_write(dev, 0x04C9, 0x0080);
		}
		b43legacy_phy_write(dev, 0x042B, b43legacy_phy_read(dev,
				    0x042B) | 0x800);

		for (i = 0; i < 64; i++)
			b43legacy_ilt_write(dev, 0x4000 + i, i);
		for (i = 0; i < B43legacy_ILT_NOISEG2_SIZE; i++)
			b43legacy_ilt_write(dev, 0x1800 + i,
					    b43legacy_ilt_noiseg2[i]);
	}

	if (phy->rev <= 2)
		for (i = 0; i < B43legacy_ILT_NOISESCALEG_SIZE; i++)
			b43legacy_ilt_write(dev, 0x1400 + i,
					    b43legacy_ilt_noisescaleg1[i]);
	else if ((phy->rev >= 7) && (b43legacy_phy_read(dev, 0x0449) & 0x0200))
		for (i = 0; i < B43legacy_ILT_NOISESCALEG_SIZE; i++)
			b43legacy_ilt_write(dev, 0x1400 + i,
					    b43legacy_ilt_noisescaleg3[i]);
	else
		for (i = 0; i < B43legacy_ILT_NOISESCALEG_SIZE; i++)
			b43legacy_ilt_write(dev, 0x1400 + i,
					    b43legacy_ilt_noisescaleg2[i]);

	if (phy->rev == 2)
		for (i = 0; i < B43legacy_ILT_SIGMASQR_SIZE; i++)
			b43legacy_ilt_write(dev, 0x5000 + i,
					    b43legacy_ilt_sigmasqr1[i]);
	else if ((phy->rev > 2) && (phy->rev <= 8))
		for (i = 0; i < B43legacy_ILT_SIGMASQR_SIZE; i++)
			b43legacy_ilt_write(dev, 0x5000 + i,
					    b43legacy_ilt_sigmasqr2[i]);

	if (phy->rev == 1) {
		for (i = 0; i < B43legacy_ILT_RETARD_SIZE; i++)
			b43legacy_ilt_write32(dev, 0x2400 + i,
					      b43legacy_ilt_retard[i]);
		for (i = 4; i < 20; i++)
			b43legacy_ilt_write(dev, 0x5400 + i, 0x0020);
		b43legacy_phy_agcsetup(dev);

		if (is_bcm_board_vendor(dev) &&
		    (dev->dev->bus->boardinfo.type == 0x0416) &&
		    (dev->dev->bus->boardinfo.rev == 0x0017))
			return;

		b43legacy_ilt_write(dev, 0x5001, 0x0002);
		b43legacy_ilt_write(dev, 0x5002, 0x0001);
	} else {
		for (i = 0; i <= 0x20; i++)
			b43legacy_ilt_write(dev, 0x1000 + i, 0x0820);
		b43legacy_phy_agcsetup(dev);
		b43legacy_phy_read(dev, 0x0400); /* dummy read */
		b43legacy_phy_write(dev, 0x0403, 0x1000);
		b43legacy_ilt_write(dev, 0x3C02, 0x000F);
		b43legacy_ilt_write(dev, 0x3C03, 0x0014);

		if (is_bcm_board_vendor(dev) &&
		    (dev->dev->bus->boardinfo.type == 0x0416) &&
		    (dev->dev->bus->boardinfo.rev == 0x0017))
			return;

		b43legacy_ilt_write(dev, 0x0401, 0x0002);
		b43legacy_ilt_write(dev, 0x0402, 0x0001);
	}
}

/* Initialize the APHY portion of a GPHY. */
static void b43legacy_phy_inita(struct b43legacy_wldev *dev)
{

	might_sleep();

	b43legacy_phy_setupg(dev);
	if (dev->dev->bus->sprom.r1.boardflags_lo & B43legacy_BFL_PACTRL)
		b43legacy_phy_write(dev, 0x046E, 0x03CF);
}

static void b43legacy_phy_initb2(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 offset;
	int val;

	b43legacy_write16(dev, 0x03EC, 0x3F22);
	b43legacy_phy_write(dev, 0x0020, 0x301C);
	b43legacy_phy_write(dev, 0x0026, 0x0000);
	b43legacy_phy_write(dev, 0x0030, 0x00C6);
	b43legacy_phy_write(dev, 0x0088, 0x3E00);
	val = 0x3C3D;
	for (offset = 0x0089; offset < 0x00A7; offset++) {
		b43legacy_phy_write(dev, offset, val);
		val -= 0x0202;
	}
	b43legacy_phy_write(dev, 0x03E4, 0x3000);
	b43legacy_radio_selectchannel(dev, phy->channel, 0);
	if (phy->radio_ver != 0x2050) {
		b43legacy_radio_write16(dev, 0x0075, 0x0080);
		b43legacy_radio_write16(dev, 0x0079, 0x0081);
	}
	b43legacy_radio_write16(dev, 0x0050, 0x0020);
	b43legacy_radio_write16(dev, 0x0050, 0x0023);
	if (phy->radio_ver == 0x2050) {
		b43legacy_radio_write16(dev, 0x0050, 0x0020);
		b43legacy_radio_write16(dev, 0x005A, 0x0070);
		b43legacy_radio_write16(dev, 0x005B, 0x007B);
		b43legacy_radio_write16(dev, 0x005C, 0x00B0);
		b43legacy_radio_write16(dev, 0x007A, 0x000F);
		b43legacy_phy_write(dev, 0x0038, 0x0677);
		b43legacy_radio_init2050(dev);
	}
	b43legacy_phy_write(dev, 0x0014, 0x0080);
	b43legacy_phy_write(dev, 0x0032, 0x00CA);
	b43legacy_phy_write(dev, 0x0032, 0x00CC);
	b43legacy_phy_write(dev, 0x0035, 0x07C2);
	b43legacy_phy_lo_b_measure(dev);
	b43legacy_phy_write(dev, 0x0026, 0xCC00);
	if (phy->radio_ver != 0x2050)
		b43legacy_phy_write(dev, 0x0026, 0xCE00);
	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, 0x1000);
	b43legacy_phy_write(dev, 0x002A, 0x88A3);
	if (phy->radio_ver != 0x2050)
		b43legacy_phy_write(dev, 0x002A, 0x88C2);
	b43legacy_radio_set_txpower_bg(dev, 0xFFFF, 0xFFFF, 0xFFFF);
	b43legacy_phy_init_pctl(dev);
}

static void b43legacy_phy_initb4(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 offset;
	u16 val;

	b43legacy_write16(dev, 0x03EC, 0x3F22);
	b43legacy_phy_write(dev, 0x0020, 0x301C);
	b43legacy_phy_write(dev, 0x0026, 0x0000);
	b43legacy_phy_write(dev, 0x0030, 0x00C6);
	b43legacy_phy_write(dev, 0x0088, 0x3E00);
	val = 0x3C3D;
	for (offset = 0x0089; offset < 0x00A7; offset++) {
		b43legacy_phy_write(dev, offset, val);
		val -= 0x0202;
	}
	b43legacy_phy_write(dev, 0x03E4, 0x3000);
	b43legacy_radio_selectchannel(dev, phy->channel, 0);
	if (phy->radio_ver != 0x2050) {
		b43legacy_radio_write16(dev, 0x0075, 0x0080);
		b43legacy_radio_write16(dev, 0x0079, 0x0081);
	}
	b43legacy_radio_write16(dev, 0x0050, 0x0020);
	b43legacy_radio_write16(dev, 0x0050, 0x0023);
	if (phy->radio_ver == 0x2050) {
		b43legacy_radio_write16(dev, 0x0050, 0x0020);
		b43legacy_radio_write16(dev, 0x005A, 0x0070);
		b43legacy_radio_write16(dev, 0x005B, 0x007B);
		b43legacy_radio_write16(dev, 0x005C, 0x00B0);
		b43legacy_radio_write16(dev, 0x007A, 0x000F);
		b43legacy_phy_write(dev, 0x0038, 0x0677);
		b43legacy_radio_init2050(dev);
	}
	b43legacy_phy_write(dev, 0x0014, 0x0080);
	b43legacy_phy_write(dev, 0x0032, 0x00CA);
	if (phy->radio_ver == 0x2050)
		b43legacy_phy_write(dev, 0x0032, 0x00E0);
	b43legacy_phy_write(dev, 0x0035, 0x07C2);

	b43legacy_phy_lo_b_measure(dev);

	b43legacy_phy_write(dev, 0x0026, 0xCC00);
	if (phy->radio_ver == 0x2050)
		b43legacy_phy_write(dev, 0x0026, 0xCE00);
	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, 0x1100);
	b43legacy_phy_write(dev, 0x002A, 0x88A3);
	if (phy->radio_ver == 0x2050)
		b43legacy_phy_write(dev, 0x002A, 0x88C2);
	b43legacy_radio_set_txpower_bg(dev, 0xFFFF, 0xFFFF, 0xFFFF);
	if (dev->dev->bus->sprom.r1.boardflags_lo & B43legacy_BFL_RSSI) {
		b43legacy_calc_nrssi_slope(dev);
		b43legacy_calc_nrssi_threshold(dev);
	}
	b43legacy_phy_init_pctl(dev);
}

static void b43legacy_phy_initb5(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 offset;
	u16 value;
	u8 old_channel;

	if (phy->analog == 1)
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x0050);
	if (!is_bcm_board_vendor(dev) &&
	    (dev->dev->bus->boardinfo.type != 0x0416)) {
		value = 0x2120;
		for (offset = 0x00A8 ; offset < 0x00C7; offset++) {
			b43legacy_phy_write(dev, offset, value);
			value += 0x0202;
		}
	}
	b43legacy_phy_write(dev, 0x0035,
			    (b43legacy_phy_read(dev, 0x0035) & 0xF0FF)
			    | 0x0700);
	if (phy->radio_ver == 0x2050)
		b43legacy_phy_write(dev, 0x0038, 0x0667);

	if (phy->gmode) {
		if (phy->radio_ver == 0x2050) {
			b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x0020);
			b43legacy_radio_write16(dev, 0x0051,
					b43legacy_radio_read16(dev, 0x0051)
					| 0x0004);
		}
		b43legacy_write16(dev, B43legacy_MMIO_PHY_RADIO, 0x0000);

		b43legacy_phy_write(dev, 0x0802, b43legacy_phy_read(dev, 0x0802)
				    | 0x0100);
		b43legacy_phy_write(dev, 0x042B, b43legacy_phy_read(dev, 0x042B)
				    | 0x2000);

		b43legacy_phy_write(dev, 0x001C, 0x186A);

		b43legacy_phy_write(dev, 0x0013, (b43legacy_phy_read(dev,
				    0x0013) & 0x00FF) | 0x1900);
		b43legacy_phy_write(dev, 0x0035, (b43legacy_phy_read(dev,
				    0x0035) & 0xFFC0) | 0x0064);
		b43legacy_phy_write(dev, 0x005D, (b43legacy_phy_read(dev,
				    0x005D) & 0xFF80) | 0x000A);
	}

	if (dev->bad_frames_preempt)
		b43legacy_phy_write(dev, B43legacy_PHY_RADIO_BITFIELD,
				    b43legacy_phy_read(dev,
				    B43legacy_PHY_RADIO_BITFIELD) | (1 << 11));

	if (phy->analog == 1) {
		b43legacy_phy_write(dev, 0x0026, 0xCE00);
		b43legacy_phy_write(dev, 0x0021, 0x3763);
		b43legacy_phy_write(dev, 0x0022, 0x1BC3);
		b43legacy_phy_write(dev, 0x0023, 0x06F9);
		b43legacy_phy_write(dev, 0x0024, 0x037E);
	} else
		b43legacy_phy_write(dev, 0x0026, 0xCC00);
	b43legacy_phy_write(dev, 0x0030, 0x00C6);
	b43legacy_write16(dev, 0x03EC, 0x3F22);

	if (phy->analog == 1)
		b43legacy_phy_write(dev, 0x0020, 0x3E1C);
	else
		b43legacy_phy_write(dev, 0x0020, 0x301C);

	if (phy->analog == 0)
		b43legacy_write16(dev, 0x03E4, 0x3000);

	old_channel = (phy->channel == 0xFF) ? 1 : phy->channel;
	/* Force to channel 7, even if not supported. */
	b43legacy_radio_selectchannel(dev, 7, 0);

	if (phy->radio_ver != 0x2050) {
		b43legacy_radio_write16(dev, 0x0075, 0x0080);
		b43legacy_radio_write16(dev, 0x0079, 0x0081);
	}

	b43legacy_radio_write16(dev, 0x0050, 0x0020);
	b43legacy_radio_write16(dev, 0x0050, 0x0023);

	if (phy->radio_ver == 0x2050) {
		b43legacy_radio_write16(dev, 0x0050, 0x0020);
		b43legacy_radio_write16(dev, 0x005A, 0x0070);
	}

	b43legacy_radio_write16(dev, 0x005B, 0x007B);
	b43legacy_radio_write16(dev, 0x005C, 0x00B0);

	b43legacy_radio_write16(dev, 0x007A, b43legacy_radio_read16(dev,
				0x007A) | 0x0007);

	b43legacy_radio_selectchannel(dev, old_channel, 0);

	b43legacy_phy_write(dev, 0x0014, 0x0080);
	b43legacy_phy_write(dev, 0x0032, 0x00CA);
	b43legacy_phy_write(dev, 0x002A, 0x88A3);

	b43legacy_radio_set_txpower_bg(dev, 0xFFFF, 0xFFFF, 0xFFFF);

	if (phy->radio_ver == 0x2050)
		b43legacy_radio_write16(dev, 0x005D, 0x000D);

	b43legacy_write16(dev, 0x03E4, (b43legacy_read16(dev, 0x03E4) &
			  0xFFC0) | 0x0004);
}

static void b43legacy_phy_initb6(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 offset;
	u16 val;
	u8 old_channel;

	b43legacy_phy_write(dev, 0x003E, 0x817A);
	b43legacy_radio_write16(dev, 0x007A,
				(b43legacy_radio_read16(dev, 0x007A) | 0x0058));
	if (phy->radio_rev == 4 ||
	     phy->radio_rev == 5) {
		b43legacy_radio_write16(dev, 0x0051, 0x0037);
		b43legacy_radio_write16(dev, 0x0052, 0x0070);
		b43legacy_radio_write16(dev, 0x0053, 0x00B3);
		b43legacy_radio_write16(dev, 0x0054, 0x009B);
		b43legacy_radio_write16(dev, 0x005A, 0x0088);
		b43legacy_radio_write16(dev, 0x005B, 0x0088);
		b43legacy_radio_write16(dev, 0x005D, 0x0088);
		b43legacy_radio_write16(dev, 0x005E, 0x0088);
		b43legacy_radio_write16(dev, 0x007D, 0x0088);
		b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
				      B43legacy_UCODEFLAGS_OFFSET,
				      (b43legacy_shm_read32(dev,
				      B43legacy_SHM_SHARED,
				      B43legacy_UCODEFLAGS_OFFSET)
				      | 0x00000200));
	}
	if (phy->radio_rev == 8) {
		b43legacy_radio_write16(dev, 0x0051, 0x0000);
		b43legacy_radio_write16(dev, 0x0052, 0x0040);
		b43legacy_radio_write16(dev, 0x0053, 0x00B7);
		b43legacy_radio_write16(dev, 0x0054, 0x0098);
		b43legacy_radio_write16(dev, 0x005A, 0x0088);
		b43legacy_radio_write16(dev, 0x005B, 0x006B);
		b43legacy_radio_write16(dev, 0x005C, 0x000F);
		if (dev->dev->bus->sprom.r1.boardflags_lo & 0x8000) {
			b43legacy_radio_write16(dev, 0x005D, 0x00FA);
			b43legacy_radio_write16(dev, 0x005E, 0x00D8);
		} else {
			b43legacy_radio_write16(dev, 0x005D, 0x00F5);
			b43legacy_radio_write16(dev, 0x005E, 0x00B8);
		}
		b43legacy_radio_write16(dev, 0x0073, 0x0003);
		b43legacy_radio_write16(dev, 0x007D, 0x00A8);
		b43legacy_radio_write16(dev, 0x007C, 0x0001);
		b43legacy_radio_write16(dev, 0x007E, 0x0008);
	}
	val = 0x1E1F;
	for (offset = 0x0088; offset < 0x0098; offset++) {
		b43legacy_phy_write(dev, offset, val);
		val -= 0x0202;
	}
	val = 0x3E3F;
	for (offset = 0x0098; offset < 0x00A8; offset++) {
		b43legacy_phy_write(dev, offset, val);
		val -= 0x0202;
	}
	val = 0x2120;
	for (offset = 0x00A8; offset < 0x00C8; offset++) {
		b43legacy_phy_write(dev, offset, (val & 0x3F3F));
		val += 0x0202;
	}
	if (phy->type == B43legacy_PHYTYPE_G) {
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A) |
					0x0020);
		b43legacy_radio_write16(dev, 0x0051,
					b43legacy_radio_read16(dev, 0x0051) |
					0x0004);
		b43legacy_phy_write(dev, 0x0802,
				    b43legacy_phy_read(dev, 0x0802) | 0x0100);
		b43legacy_phy_write(dev, 0x042B,
				    b43legacy_phy_read(dev, 0x042B) | 0x2000);
		b43legacy_phy_write(dev, 0x5B, 0x0000);
		b43legacy_phy_write(dev, 0x5C, 0x0000);
	}

	old_channel = phy->channel;
	if (old_channel >= 8)
		b43legacy_radio_selectchannel(dev, 1, 0);
	else
		b43legacy_radio_selectchannel(dev, 13, 0);

	b43legacy_radio_write16(dev, 0x0050, 0x0020);
	b43legacy_radio_write16(dev, 0x0050, 0x0023);
	udelay(40);
	if (phy->radio_rev < 6 || phy->radio_rev == 8) {
		b43legacy_radio_write16(dev, 0x007C,
					(b43legacy_radio_read16(dev, 0x007C)
					| 0x0002));
		b43legacy_radio_write16(dev, 0x0050, 0x0020);
	}
	if (phy->radio_rev <= 2) {
		b43legacy_radio_write16(dev, 0x007C, 0x0020);
		b43legacy_radio_write16(dev, 0x005A, 0x0070);
		b43legacy_radio_write16(dev, 0x005B, 0x007B);
		b43legacy_radio_write16(dev, 0x005C, 0x00B0);
	}
	b43legacy_radio_write16(dev, 0x007A,
				(b43legacy_radio_read16(dev,
				0x007A) & 0x00F8) | 0x0007);

	b43legacy_radio_selectchannel(dev, old_channel, 0);

	b43legacy_phy_write(dev, 0x0014, 0x0200);
	if (phy->radio_rev >= 6)
		b43legacy_phy_write(dev, 0x002A, 0x88C2);
	else
		b43legacy_phy_write(dev, 0x002A, 0x8AC0);
	b43legacy_phy_write(dev, 0x0038, 0x0668);
	b43legacy_radio_set_txpower_bg(dev, 0xFFFF, 0xFFFF, 0xFFFF);
	if (phy->radio_rev <= 5)
		b43legacy_phy_write(dev, 0x005D, (b43legacy_phy_read(dev,
				    0x005D) & 0xFF80) | 0x0003);
	if (phy->radio_rev <= 2)
		b43legacy_radio_write16(dev, 0x005D, 0x000D);

	if (phy->analog == 4) {
		b43legacy_write16(dev, 0x03E4, 0x0009);
		b43legacy_phy_write(dev, 0x61, b43legacy_phy_read(dev, 0x61)
				    & 0xFFF);
	} else
		b43legacy_phy_write(dev, 0x0002, (b43legacy_phy_read(dev,
				    0x0002) & 0xFFC0) | 0x0004);
	if (phy->type == B43legacy_PHYTYPE_G)
		b43legacy_write16(dev, 0x03E6, 0x0);
	if (phy->type == B43legacy_PHYTYPE_B) {
		b43legacy_write16(dev, 0x03E6, 0x8140);
		b43legacy_phy_write(dev, 0x0016, 0x0410);
		b43legacy_phy_write(dev, 0x0017, 0x0820);
		b43legacy_phy_write(dev, 0x0062, 0x0007);
		b43legacy_radio_init2050(dev);
		b43legacy_phy_lo_g_measure(dev);
		if (dev->dev->bus->sprom.r1.boardflags_lo &
		    B43legacy_BFL_RSSI) {
			b43legacy_calc_nrssi_slope(dev);
			b43legacy_calc_nrssi_threshold(dev);
		}
		b43legacy_phy_init_pctl(dev);
	}
}

static void b43legacy_calc_loopback_gain(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 backup_phy[15] = {0};
	u16 backup_radio[3];
	u16 backup_bband;
	u16 i;
	u16 loop1_cnt;
	u16 loop1_done;
	u16 loop1_omitted;
	u16 loop2_done;

	backup_phy[0] = b43legacy_phy_read(dev, 0x0429);
	backup_phy[1] = b43legacy_phy_read(dev, 0x0001);
	backup_phy[2] = b43legacy_phy_read(dev, 0x0811);
	backup_phy[3] = b43legacy_phy_read(dev, 0x0812);
	if (phy->rev != 1) {
		backup_phy[4] = b43legacy_phy_read(dev, 0x0814);
		backup_phy[5] = b43legacy_phy_read(dev, 0x0815);
	}
	backup_phy[6] = b43legacy_phy_read(dev, 0x005A);
	backup_phy[7] = b43legacy_phy_read(dev, 0x0059);
	backup_phy[8] = b43legacy_phy_read(dev, 0x0058);
	backup_phy[9] = b43legacy_phy_read(dev, 0x000A);
	backup_phy[10] = b43legacy_phy_read(dev, 0x0003);
	backup_phy[11] = b43legacy_phy_read(dev, 0x080F);
	backup_phy[12] = b43legacy_phy_read(dev, 0x0810);
	backup_phy[13] = b43legacy_phy_read(dev, 0x002B);
	backup_phy[14] = b43legacy_phy_read(dev, 0x0015);
	b43legacy_phy_read(dev, 0x002D); /* dummy read */
	backup_bband = phy->bbatt;
	backup_radio[0] = b43legacy_radio_read16(dev, 0x0052);
	backup_radio[1] = b43legacy_radio_read16(dev, 0x0043);
	backup_radio[2] = b43legacy_radio_read16(dev, 0x007A);

	b43legacy_phy_write(dev, 0x0429,
			    b43legacy_phy_read(dev, 0x0429) & 0x3FFF);
	b43legacy_phy_write(dev, 0x0001,
			    b43legacy_phy_read(dev, 0x0001) & 0x8000);
	b43legacy_phy_write(dev, 0x0811,
			    b43legacy_phy_read(dev, 0x0811) | 0x0002);
	b43legacy_phy_write(dev, 0x0812,
			    b43legacy_phy_read(dev, 0x0812) & 0xFFFD);
	b43legacy_phy_write(dev, 0x0811,
			    b43legacy_phy_read(dev, 0x0811) | 0x0001);
	b43legacy_phy_write(dev, 0x0812,
			    b43legacy_phy_read(dev, 0x0812) & 0xFFFE);
	if (phy->rev != 1) {
		b43legacy_phy_write(dev, 0x0814,
				    b43legacy_phy_read(dev, 0x0814) | 0x0001);
		b43legacy_phy_write(dev, 0x0815,
				    b43legacy_phy_read(dev, 0x0815) & 0xFFFE);
		b43legacy_phy_write(dev, 0x0814,
				    b43legacy_phy_read(dev, 0x0814) | 0x0002);
		b43legacy_phy_write(dev, 0x0815,
				    b43legacy_phy_read(dev, 0x0815) & 0xFFFD);
	}
	b43legacy_phy_write(dev, 0x0811, b43legacy_phy_read(dev, 0x0811) |
			    0x000C);
	b43legacy_phy_write(dev, 0x0812, b43legacy_phy_read(dev, 0x0812) |
			    0x000C);

	b43legacy_phy_write(dev, 0x0811, (b43legacy_phy_read(dev, 0x0811)
			    & 0xFFCF) | 0x0030);
	b43legacy_phy_write(dev, 0x0812, (b43legacy_phy_read(dev, 0x0812)
			    & 0xFFCF) | 0x0010);

	b43legacy_phy_write(dev, 0x005A, 0x0780);
	b43legacy_phy_write(dev, 0x0059, 0xC810);
	b43legacy_phy_write(dev, 0x0058, 0x000D);
	if (phy->analog == 0)
		b43legacy_phy_write(dev, 0x0003, 0x0122);
	else
		b43legacy_phy_write(dev, 0x000A,
				    b43legacy_phy_read(dev, 0x000A)
				    | 0x2000);
	if (phy->rev != 1) {
		b43legacy_phy_write(dev, 0x0814,
				    b43legacy_phy_read(dev, 0x0814) | 0x0004);
		b43legacy_phy_write(dev, 0x0815,
				    b43legacy_phy_read(dev, 0x0815) & 0xFFFB);
	}
	b43legacy_phy_write(dev, 0x0003,
			    (b43legacy_phy_read(dev, 0x0003)
			     & 0xFF9F) | 0x0040);
	if (phy->radio_ver == 0x2050 && phy->radio_rev == 2) {
		b43legacy_radio_write16(dev, 0x0052, 0x0000);
		b43legacy_radio_write16(dev, 0x0043,
					(b43legacy_radio_read16(dev, 0x0043)
					 & 0xFFF0) | 0x0009);
		loop1_cnt = 9;
	} else if (phy->radio_rev == 8) {
		b43legacy_radio_write16(dev, 0x0043, 0x000F);
		loop1_cnt = 15;
	} else
		loop1_cnt = 0;

	b43legacy_phy_set_baseband_attenuation(dev, 11);

	if (phy->rev >= 3)
		b43legacy_phy_write(dev, 0x080F, 0xC020);
	else
		b43legacy_phy_write(dev, 0x080F, 0x8020);
	b43legacy_phy_write(dev, 0x0810, 0x0000);

	b43legacy_phy_write(dev, 0x002B,
			    (b43legacy_phy_read(dev, 0x002B)
			     & 0xFFC0) | 0x0001);
	b43legacy_phy_write(dev, 0x002B,
			    (b43legacy_phy_read(dev, 0x002B)
			     & 0xC0FF) | 0x0800);
	b43legacy_phy_write(dev, 0x0811,
			    b43legacy_phy_read(dev, 0x0811) | 0x0100);
	b43legacy_phy_write(dev, 0x0812,
			    b43legacy_phy_read(dev, 0x0812) & 0xCFFF);
	if (dev->dev->bus->sprom.r1.boardflags_lo & B43legacy_BFL_EXTLNA) {
		if (phy->rev >= 7) {
			b43legacy_phy_write(dev, 0x0811,
					    b43legacy_phy_read(dev, 0x0811)
					    | 0x0800);
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_phy_read(dev, 0x0812)
					    | 0x8000);
		}
	}
	b43legacy_radio_write16(dev, 0x007A,
				b43legacy_radio_read16(dev, 0x007A)
				& 0x00F7);

	for (i = 0; i < loop1_cnt; i++) {
		b43legacy_radio_write16(dev, 0x0043, loop1_cnt);
		b43legacy_phy_write(dev, 0x0812,
				    (b43legacy_phy_read(dev, 0x0812)
				     & 0xF0FF) | (i << 8));
		b43legacy_phy_write(dev, 0x0015,
				    (b43legacy_phy_read(dev, 0x0015)
				     & 0x0FFF) | 0xA000);
		b43legacy_phy_write(dev, 0x0015,
				    (b43legacy_phy_read(dev, 0x0015)
				     & 0x0FFF) | 0xF000);
		udelay(20);
		if (b43legacy_phy_read(dev, 0x002D) >= 0x0DFC)
			break;
	}
	loop1_done = i;
	loop1_omitted = loop1_cnt - loop1_done;

	loop2_done = 0;
	if (loop1_done >= 8) {
		b43legacy_phy_write(dev, 0x0812,
				    b43legacy_phy_read(dev, 0x0812)
				    | 0x0030);
		for (i = loop1_done - 8; i < 16; i++) {
			b43legacy_phy_write(dev, 0x0812,
					    (b43legacy_phy_read(dev, 0x0812)
					     & 0xF0FF) | (i << 8));
			b43legacy_phy_write(dev, 0x0015,
					    (b43legacy_phy_read(dev, 0x0015)
					     & 0x0FFF) | 0xA000);
			b43legacy_phy_write(dev, 0x0015,
					    (b43legacy_phy_read(dev, 0x0015)
					     & 0x0FFF) | 0xF000);
			udelay(20);
			if (b43legacy_phy_read(dev, 0x002D) >= 0x0DFC)
				break;
		}
	}

	if (phy->rev != 1) {
		b43legacy_phy_write(dev, 0x0814, backup_phy[4]);
		b43legacy_phy_write(dev, 0x0815, backup_phy[5]);
	}
	b43legacy_phy_write(dev, 0x005A, backup_phy[6]);
	b43legacy_phy_write(dev, 0x0059, backup_phy[7]);
	b43legacy_phy_write(dev, 0x0058, backup_phy[8]);
	b43legacy_phy_write(dev, 0x000A, backup_phy[9]);
	b43legacy_phy_write(dev, 0x0003, backup_phy[10]);
	b43legacy_phy_write(dev, 0x080F, backup_phy[11]);
	b43legacy_phy_write(dev, 0x0810, backup_phy[12]);
	b43legacy_phy_write(dev, 0x002B, backup_phy[13]);
	b43legacy_phy_write(dev, 0x0015, backup_phy[14]);

	b43legacy_phy_set_baseband_attenuation(dev, backup_bband);

	b43legacy_radio_write16(dev, 0x0052, backup_radio[0]);
	b43legacy_radio_write16(dev, 0x0043, backup_radio[1]);
	b43legacy_radio_write16(dev, 0x007A, backup_radio[2]);

	b43legacy_phy_write(dev, 0x0811, backup_phy[2] | 0x0003);
	udelay(10);
	b43legacy_phy_write(dev, 0x0811, backup_phy[2]);
	b43legacy_phy_write(dev, 0x0812, backup_phy[3]);
	b43legacy_phy_write(dev, 0x0429, backup_phy[0]);
	b43legacy_phy_write(dev, 0x0001, backup_phy[1]);

	phy->loopback_gain[0] = ((loop1_done * 6) - (loop1_omitted * 4)) - 11;
	phy->loopback_gain[1] = (24 - (3 * loop2_done)) * 2;
}

static void b43legacy_phy_initg(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 tmp;

	if (phy->rev == 1)
		b43legacy_phy_initb5(dev);
	else
		b43legacy_phy_initb6(dev);
	if (phy->rev >= 2 || phy->gmode)
		b43legacy_phy_inita(dev);

	if (phy->rev >= 2) {
		b43legacy_phy_write(dev, 0x0814, 0x0000);
		b43legacy_phy_write(dev, 0x0815, 0x0000);
	}
	if (phy->rev == 2) {
		b43legacy_phy_write(dev, 0x0811, 0x0000);
		b43legacy_phy_write(dev, 0x0015, 0x00C0);
	}
	if (phy->rev > 5) {
		b43legacy_phy_write(dev, 0x0811, 0x0400);
		b43legacy_phy_write(dev, 0x0015, 0x00C0);
	}
	if (phy->rev >= 2 || phy->gmode) {
		tmp = b43legacy_phy_read(dev, 0x0400) & 0xFF;
		if (tmp == 3 || tmp == 5) {
			b43legacy_phy_write(dev, 0x04C2, 0x1816);
			b43legacy_phy_write(dev, 0x04C3, 0x8006);
			if (tmp == 5)
				b43legacy_phy_write(dev, 0x04CC,
						    (b43legacy_phy_read(dev,
						     0x04CC) & 0x00FF) |
						     0x1F00);
		}
		b43legacy_phy_write(dev, 0x047E, 0x0078);
	}
	if (phy->radio_rev == 8) {
		b43legacy_phy_write(dev, 0x0801, b43legacy_phy_read(dev, 0x0801)
				    | 0x0080);
		b43legacy_phy_write(dev, 0x043E, b43legacy_phy_read(dev, 0x043E)
				    | 0x0004);
	}
	if (phy->rev >= 2 && phy->gmode)
		b43legacy_calc_loopback_gain(dev);
	if (phy->radio_rev != 8) {
		if (phy->initval == 0xFFFF)
			phy->initval = b43legacy_radio_init2050(dev);
		else
			b43legacy_radio_write16(dev, 0x0078, phy->initval);
	}
	if (phy->txctl2 == 0xFFFF)
		b43legacy_phy_lo_g_measure(dev);
	else {
		if (phy->radio_ver == 0x2050 && phy->radio_rev == 8)
			b43legacy_radio_write16(dev, 0x0052,
						(phy->txctl1 << 4) |
						phy->txctl2);
		else
			b43legacy_radio_write16(dev, 0x0052,
						(b43legacy_radio_read16(dev,
						 0x0052) & 0xFFF0) |
						 phy->txctl1);
		if (phy->rev >= 6)
			b43legacy_phy_write(dev, 0x0036,
					    (b43legacy_phy_read(dev, 0x0036)
					     & 0x0FFF) | (phy->txctl2 << 12));
		if (dev->dev->bus->sprom.r1.boardflags_lo &
		    B43legacy_BFL_PACTRL)
			b43legacy_phy_write(dev, 0x002E, 0x8075);
		else
			b43legacy_phy_write(dev, 0x002E, 0x807F);
		if (phy->rev < 2)
			b43legacy_phy_write(dev, 0x002F, 0x0101);
		else
			b43legacy_phy_write(dev, 0x002F, 0x0202);
	}
	if (phy->gmode || phy->rev >= 2) {
		b43legacy_phy_lo_adjust(dev, 0);
		b43legacy_phy_write(dev, 0x080F, 0x8078);
	}

	if (!(dev->dev->bus->sprom.r1.boardflags_lo & B43legacy_BFL_RSSI)) {
		/* The specs state to update the NRSSI LT with
		 * the value 0x7FFFFFFF here. I think that is some weird
		 * compiler optimization in the original driver.
		 * Essentially, what we do here is resetting all NRSSI LT
		 * entries to -32 (see the limit_value() in nrssi_hw_update())
		 */
		b43legacy_nrssi_hw_update(dev, 0xFFFF);
		b43legacy_calc_nrssi_threshold(dev);
	} else if (phy->gmode || phy->rev >= 2) {
		if (phy->nrssi[0] == -1000) {
			B43legacy_WARN_ON(phy->nrssi[1] != -1000);
			b43legacy_calc_nrssi_slope(dev);
		} else {
			B43legacy_WARN_ON(phy->nrssi[1] == -1000);
			b43legacy_calc_nrssi_threshold(dev);
		}
	}
	if (phy->radio_rev == 8)
		b43legacy_phy_write(dev, 0x0805, 0x3230);
	b43legacy_phy_init_pctl(dev);
	if (dev->dev->bus->chip_id == 0x4306
	    && dev->dev->bus->chip_package == 2) {
		b43legacy_phy_write(dev, 0x0429,
				    b43legacy_phy_read(dev, 0x0429) & 0xBFFF);
		b43legacy_phy_write(dev, 0x04C3,
				    b43legacy_phy_read(dev, 0x04C3) & 0x7FFF);
	}
}

static u16 b43legacy_phy_lo_b_r15_loop(struct b43legacy_wldev *dev)
{
	int i;
	u16 ret = 0;
	unsigned long flags;

	local_irq_save(flags);
	for (i = 0; i < 10; i++) {
		b43legacy_phy_write(dev, 0x0015, 0xAFA0);
		udelay(1);
		b43legacy_phy_write(dev, 0x0015, 0xEFA0);
		udelay(10);
		b43legacy_phy_write(dev, 0x0015, 0xFFA0);
		udelay(40);
		ret += b43legacy_phy_read(dev, 0x002C);
	}
	local_irq_restore(flags);
	b43legacy_voluntary_preempt();

	return ret;
}

void b43legacy_phy_lo_b_measure(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 regstack[12] = { 0 };
	u16 mls;
	u16 fval;
	int i;
	int j;

	regstack[0] = b43legacy_phy_read(dev, 0x0015);
	regstack[1] = b43legacy_radio_read16(dev, 0x0052) & 0xFFF0;

	if (phy->radio_ver == 0x2053) {
		regstack[2] = b43legacy_phy_read(dev, 0x000A);
		regstack[3] = b43legacy_phy_read(dev, 0x002A);
		regstack[4] = b43legacy_phy_read(dev, 0x0035);
		regstack[5] = b43legacy_phy_read(dev, 0x0003);
		regstack[6] = b43legacy_phy_read(dev, 0x0001);
		regstack[7] = b43legacy_phy_read(dev, 0x0030);

		regstack[8] = b43legacy_radio_read16(dev, 0x0043);
		regstack[9] = b43legacy_radio_read16(dev, 0x007A);
		regstack[10] = b43legacy_read16(dev, 0x03EC);
		regstack[11] = b43legacy_radio_read16(dev, 0x0052) & 0x00F0;

		b43legacy_phy_write(dev, 0x0030, 0x00FF);
		b43legacy_write16(dev, 0x03EC, 0x3F3F);
		b43legacy_phy_write(dev, 0x0035, regstack[4] & 0xFF7F);
		b43legacy_radio_write16(dev, 0x007A, regstack[9] & 0xFFF0);
	}
	b43legacy_phy_write(dev, 0x0015, 0xB000);
	b43legacy_phy_write(dev, 0x002B, 0x0004);

	if (phy->radio_ver == 0x2053) {
		b43legacy_phy_write(dev, 0x002B, 0x0203);
		b43legacy_phy_write(dev, 0x002A, 0x08A3);
	}

	phy->minlowsig[0] = 0xFFFF;

	for (i = 0; i < 4; i++) {
		b43legacy_radio_write16(dev, 0x0052, regstack[1] | i);
		b43legacy_phy_lo_b_r15_loop(dev);
	}
	for (i = 0; i < 10; i++) {
		b43legacy_radio_write16(dev, 0x0052, regstack[1] | i);
		mls = b43legacy_phy_lo_b_r15_loop(dev) / 10;
		if (mls < phy->minlowsig[0]) {
			phy->minlowsig[0] = mls;
			phy->minlowsigpos[0] = i;
		}
	}
	b43legacy_radio_write16(dev, 0x0052, regstack[1]
				| phy->minlowsigpos[0]);

	phy->minlowsig[1] = 0xFFFF;

	for (i = -4; i < 5; i += 2) {
		for (j = -4; j < 5; j += 2) {
			if (j < 0)
				fval = (0x0100 * i) + j + 0x0100;
			else
				fval = (0x0100 * i) + j;
			b43legacy_phy_write(dev, 0x002F, fval);
			mls = b43legacy_phy_lo_b_r15_loop(dev) / 10;
			if (mls < phy->minlowsig[1]) {
				phy->minlowsig[1] = mls;
				phy->minlowsigpos[1] = fval;
			}
		}
	}
	phy->minlowsigpos[1] += 0x0101;

	b43legacy_phy_write(dev, 0x002F, phy->minlowsigpos[1]);
	if (phy->radio_ver == 0x2053) {
		b43legacy_phy_write(dev, 0x000A, regstack[2]);
		b43legacy_phy_write(dev, 0x002A, regstack[3]);
		b43legacy_phy_write(dev, 0x0035, regstack[4]);
		b43legacy_phy_write(dev, 0x0003, regstack[5]);
		b43legacy_phy_write(dev, 0x0001, regstack[6]);
		b43legacy_phy_write(dev, 0x0030, regstack[7]);

		b43legacy_radio_write16(dev, 0x0043, regstack[8]);
		b43legacy_radio_write16(dev, 0x007A, regstack[9]);

		b43legacy_radio_write16(dev, 0x0052,
					(b43legacy_radio_read16(dev, 0x0052)
					& 0x000F) | regstack[11]);

		b43legacy_write16(dev, 0x03EC, regstack[10]);
	}
	b43legacy_phy_write(dev, 0x0015, regstack[0]);
}

static inline
u16 b43legacy_phy_lo_g_deviation_subval(struct b43legacy_wldev *dev,
					u16 control)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 ret;
	unsigned long flags;

	local_irq_save(flags);
	if (phy->gmode) {
		b43legacy_phy_write(dev, 0x15, 0xE300);
		control <<= 8;
		b43legacy_phy_write(dev, 0x0812, control | 0x00B0);
		udelay(5);
		b43legacy_phy_write(dev, 0x0812, control | 0x00B2);
		udelay(2);
		b43legacy_phy_write(dev, 0x0812, control | 0x00B3);
		udelay(4);
		b43legacy_phy_write(dev, 0x0015, 0xF300);
		udelay(8);
	} else {
		b43legacy_phy_write(dev, 0x0015, control | 0xEFA0);
		udelay(2);
		b43legacy_phy_write(dev, 0x0015, control | 0xEFE0);
		udelay(4);
		b43legacy_phy_write(dev, 0x0015, control | 0xFFE0);
		udelay(8);
	}
	ret = b43legacy_phy_read(dev, 0x002D);
	local_irq_restore(flags);
	b43legacy_voluntary_preempt();

	return ret;
}

static u32 b43legacy_phy_lo_g_singledeviation(struct b43legacy_wldev *dev,
					      u16 control)
{
	int i;
	u32 ret = 0;

	for (i = 0; i < 8; i++)
		ret += b43legacy_phy_lo_g_deviation_subval(dev, control);

	return ret;
}

/* Write the LocalOscillator CONTROL */
static inline
void b43legacy_lo_write(struct b43legacy_wldev *dev,
			struct b43legacy_lopair *pair)
{
	u16 value;

	value = (u8)(pair->low);
	value |= ((u8)(pair->high)) << 8;

#ifdef CONFIG_B43LEGACY_DEBUG
	/* Sanity check. */
	if (pair->low < -8 || pair->low > 8 ||
	    pair->high < -8 || pair->high > 8) {
		struct b43legacy_phy *phy = &dev->phy;
		b43legacydbg(dev->wl,
		       "WARNING: Writing invalid LOpair "
		       "(low: %d, high: %d, index: %lu)\n",
		       pair->low, pair->high,
		       (unsigned long)(pair - phy->_lo_pairs));
		dump_stack();
	}
#endif

	b43legacy_phy_write(dev, B43legacy_PHY_G_LO_CONTROL, value);
}

static inline
struct b43legacy_lopair *b43legacy_find_lopair(struct b43legacy_wldev *dev,
					       u16 bbatt,
					       u16 rfatt,
					       u16 tx)
{
	static const u8 dict[10] = { 11, 10, 11, 12, 13, 12, 13, 12, 13, 12 };
	struct b43legacy_phy *phy = &dev->phy;

	if (bbatt > 6)
		bbatt = 6;
	B43legacy_WARN_ON(rfatt >= 10);

	if (tx == 3)
		return b43legacy_get_lopair(phy, rfatt, bbatt);
	return b43legacy_get_lopair(phy, dict[rfatt], bbatt);
}

static inline
struct b43legacy_lopair *b43legacy_current_lopair(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	return b43legacy_find_lopair(dev, phy->bbatt,
				     phy->rfatt, phy->txctl1);
}

/* Adjust B/G LO */
void b43legacy_phy_lo_adjust(struct b43legacy_wldev *dev, int fixed)
{
	struct b43legacy_lopair *pair;

	if (fixed) {
		/* Use fixed values. Only for initialization. */
		pair = b43legacy_find_lopair(dev, 2, 3, 0);
	} else
		pair = b43legacy_current_lopair(dev);
	b43legacy_lo_write(dev, pair);
}

static void b43legacy_phy_lo_g_measure_txctl2(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 txctl2 = 0;
	u16 i;
	u32 smallest;
	u32 tmp;

	b43legacy_radio_write16(dev, 0x0052, 0x0000);
	udelay(10);
	smallest = b43legacy_phy_lo_g_singledeviation(dev, 0);
	for (i = 0; i < 16; i++) {
		b43legacy_radio_write16(dev, 0x0052, i);
		udelay(10);
		tmp = b43legacy_phy_lo_g_singledeviation(dev, 0);
		if (tmp < smallest) {
			smallest = tmp;
			txctl2 = i;
		}
	}
	phy->txctl2 = txctl2;
}

static
void b43legacy_phy_lo_g_state(struct b43legacy_wldev *dev,
			      const struct b43legacy_lopair *in_pair,
			      struct b43legacy_lopair *out_pair,
			      u16 r27)
{
	static const struct b43legacy_lopair transitions[8] = {
		{ .high =  1,  .low =  1, },
		{ .high =  1,  .low =  0, },
		{ .high =  1,  .low = -1, },
		{ .high =  0,  .low = -1, },
		{ .high = -1,  .low = -1, },
		{ .high = -1,  .low =  0, },
		{ .high = -1,  .low =  1, },
		{ .high =  0,  .low =  1, },
	};
	struct b43legacy_lopair lowest_transition = {
		.high = in_pair->high,
		.low = in_pair->low,
	};
	struct b43legacy_lopair tmp_pair;
	struct b43legacy_lopair transition;
	int i = 12;
	int state = 0;
	int found_lower;
	int j;
	int begin;
	int end;
	u32 lowest_deviation;
	u32 tmp;

	/* Note that in_pair and out_pair can point to the same pair.
	 * Be careful. */

	b43legacy_lo_write(dev, &lowest_transition);
	lowest_deviation = b43legacy_phy_lo_g_singledeviation(dev, r27);
	do {
		found_lower = 0;
		B43legacy_WARN_ON(!(state >= 0 && state <= 8));
		if (state == 0) {
			begin = 1;
			end = 8;
		} else if (state % 2 == 0) {
			begin = state - 1;
			end = state + 1;
		} else {
			begin = state - 2;
			end = state + 2;
		}
		if (begin < 1)
			begin += 8;
		if (end > 8)
			end -= 8;

		j = begin;
		tmp_pair.high = lowest_transition.high;
		tmp_pair.low = lowest_transition.low;
		while (1) {
			B43legacy_WARN_ON(!(j >= 1 && j <= 8));
			transition.high = tmp_pair.high +
					  transitions[j - 1].high;
			transition.low = tmp_pair.low + transitions[j - 1].low;
			if ((abs(transition.low) < 9)
			     && (abs(transition.high) < 9)) {
				b43legacy_lo_write(dev, &transition);
				tmp = b43legacy_phy_lo_g_singledeviation(dev,
								       r27);
				if (tmp < lowest_deviation) {
					lowest_deviation = tmp;
					state = j;
					found_lower = 1;

					lowest_transition.high =
								transition.high;
					lowest_transition.low = transition.low;
				}
			}
			if (j == end)
				break;
			if (j == 8)
				j = 1;
			else
				j++;
		}
	} while (i-- && found_lower);

	out_pair->high = lowest_transition.high;
	out_pair->low = lowest_transition.low;
}

/* Set the baseband attenuation value on chip. */
void b43legacy_phy_set_baseband_attenuation(struct b43legacy_wldev *dev,
					    u16 bbatt)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 value;

	if (phy->analog == 0) {
		value = (b43legacy_read16(dev, 0x03E6) & 0xFFF0);
		value |= (bbatt & 0x000F);
		b43legacy_write16(dev, 0x03E6, value);
		return;
	}

	if (phy->analog > 1) {
		value = b43legacy_phy_read(dev, 0x0060) & 0xFFC3;
		value |= (bbatt << 2) & 0x003C;
	} else {
		value = b43legacy_phy_read(dev, 0x0060) & 0xFF87;
		value |= (bbatt << 3) & 0x0078;
	}
	b43legacy_phy_write(dev, 0x0060, value);
}

/* http://bcm-specs.sipsolutions.net/LocalOscillator/Measure */
void b43legacy_phy_lo_g_measure(struct b43legacy_wldev *dev)
{
	static const u8 pairorder[10] = { 3, 1, 5, 7, 9, 2, 0, 4, 6, 8 };
	const int is_initializing = (b43legacy_status(dev)
				     < B43legacy_STAT_STARTED);
	struct b43legacy_phy *phy = &dev->phy;
	u16 h;
	u16 i;
	u16 oldi = 0;
	u16 j;
	struct b43legacy_lopair control;
	struct b43legacy_lopair *tmp_control;
	u16 tmp;
	u16 regstack[16] = { 0 };
	u8 oldchannel;

	/* XXX: What are these? */
	u8 r27 = 0;
	u16 r31;

	oldchannel = phy->channel;
	/* Setup */
	if (phy->gmode) {
		regstack[0] = b43legacy_phy_read(dev, B43legacy_PHY_G_CRS);
		regstack[1] = b43legacy_phy_read(dev, 0x0802);
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS, regstack[0]
				    & 0x7FFF);
		b43legacy_phy_write(dev, 0x0802, regstack[1] & 0xFFFC);
	}
	regstack[3] = b43legacy_read16(dev, 0x03E2);
	b43legacy_write16(dev, 0x03E2, regstack[3] | 0x8000);
	regstack[4] = b43legacy_read16(dev, B43legacy_MMIO_CHANNEL_EXT);
	regstack[5] = b43legacy_phy_read(dev, 0x15);
	regstack[6] = b43legacy_phy_read(dev, 0x2A);
	regstack[7] = b43legacy_phy_read(dev, 0x35);
	regstack[8] = b43legacy_phy_read(dev, 0x60);
	regstack[9] = b43legacy_radio_read16(dev, 0x43);
	regstack[10] = b43legacy_radio_read16(dev, 0x7A);
	regstack[11] = b43legacy_radio_read16(dev, 0x52);
	if (phy->gmode) {
		regstack[12] = b43legacy_phy_read(dev, 0x0811);
		regstack[13] = b43legacy_phy_read(dev, 0x0812);
		regstack[14] = b43legacy_phy_read(dev, 0x0814);
		regstack[15] = b43legacy_phy_read(dev, 0x0815);
	}
	b43legacy_radio_selectchannel(dev, 6, 0);
	if (phy->gmode) {
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS, regstack[0]
				    & 0x7FFF);
		b43legacy_phy_write(dev, 0x0802, regstack[1] & 0xFFFC);
		b43legacy_dummy_transmission(dev);
	}
	b43legacy_radio_write16(dev, 0x0043, 0x0006);

	b43legacy_phy_set_baseband_attenuation(dev, 2);

	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, 0x0000);
	b43legacy_phy_write(dev, 0x002E, 0x007F);
	b43legacy_phy_write(dev, 0x080F, 0x0078);
	b43legacy_phy_write(dev, 0x0035, regstack[7] & ~(1 << 7));
	b43legacy_radio_write16(dev, 0x007A, regstack[10] & 0xFFF0);
	b43legacy_phy_write(dev, 0x002B, 0x0203);
	b43legacy_phy_write(dev, 0x002A, 0x08A3);
	if (phy->gmode) {
		b43legacy_phy_write(dev, 0x0814, regstack[14] | 0x0003);
		b43legacy_phy_write(dev, 0x0815, regstack[15] & 0xFFFC);
		b43legacy_phy_write(dev, 0x0811, 0x01B3);
		b43legacy_phy_write(dev, 0x0812, 0x00B2);
	}
	if (is_initializing)
		b43legacy_phy_lo_g_measure_txctl2(dev);
	b43legacy_phy_write(dev, 0x080F, 0x8078);

	/* Measure */
	control.low = 0;
	control.high = 0;
	for (h = 0; h < 10; h++) {
		/* Loop over each possible RadioAttenuation (0-9) */
		i = pairorder[h];
		if (is_initializing) {
			if (i == 3) {
				control.low = 0;
				control.high = 0;
			} else if (((i % 2 == 1) && (oldi % 2 == 1)) ||
				  ((i % 2 == 0) && (oldi % 2 == 0))) {
				tmp_control = b43legacy_get_lopair(phy, oldi,
								   0);
				memcpy(&control, tmp_control, sizeof(control));
			} else {
				tmp_control = b43legacy_get_lopair(phy, 3, 0);
				memcpy(&control, tmp_control, sizeof(control));
			}
		}
		/* Loop over each possible BasebandAttenuation/2 */
		for (j = 0; j < 4; j++) {
			if (is_initializing) {
				tmp = i * 2 + j;
				r27 = 0;
				r31 = 0;
				if (tmp > 14) {
					r31 = 1;
					if (tmp > 17)
						r27 = 1;
					if (tmp > 19)
						r27 = 2;
				}
			} else {
				tmp_control = b43legacy_get_lopair(phy, i,
								   j * 2);
				if (!tmp_control->used)
					continue;
				memcpy(&control, tmp_control, sizeof(control));
				r27 = 3;
				r31 = 0;
			}
			b43legacy_radio_write16(dev, 0x43, i);
			b43legacy_radio_write16(dev, 0x52, phy->txctl2);
			udelay(10);
			b43legacy_voluntary_preempt();

			b43legacy_phy_set_baseband_attenuation(dev, j * 2);

			tmp = (regstack[10] & 0xFFF0);
			if (r31)
				tmp |= 0x0008;
			b43legacy_radio_write16(dev, 0x007A, tmp);

			tmp_control = b43legacy_get_lopair(phy, i, j * 2);
			b43legacy_phy_lo_g_state(dev, &control, tmp_control,
						 r27);
		}
		oldi = i;
	}
	/* Loop over each possible RadioAttenuation (10-13) */
	for (i = 10; i < 14; i++) {
		/* Loop over each possible BasebandAttenuation/2 */
		for (j = 0; j < 4; j++) {
			if (is_initializing) {
				tmp_control = b43legacy_get_lopair(phy, i - 9,
								 j * 2);
				memcpy(&control, tmp_control, sizeof(control));
				/* FIXME: The next line is wrong, as the
				 * following if statement can never trigger. */
				tmp = (i - 9) * 2 + j - 5;
				r27 = 0;
				r31 = 0;
				if (tmp > 14) {
					r31 = 1;
					if (tmp > 17)
						r27 = 1;
					if (tmp > 19)
						r27 = 2;
				}
			} else {
				tmp_control = b43legacy_get_lopair(phy, i - 9,
								   j * 2);
				if (!tmp_control->used)
					continue;
				memcpy(&control, tmp_control, sizeof(control));
				r27 = 3;
				r31 = 0;
			}
			b43legacy_radio_write16(dev, 0x43, i - 9);
			/* FIXME: shouldn't txctl1 be zero in the next line
			 * and 3 in the loop above? */
			b43legacy_radio_write16(dev, 0x52,
					      phy->txctl2
					      | (3/*txctl1*/ << 4));
			udelay(10);
			b43legacy_voluntary_preempt();

			b43legacy_phy_set_baseband_attenuation(dev, j * 2);

			tmp = (regstack[10] & 0xFFF0);
			if (r31)
				tmp |= 0x0008;
			b43legacy_radio_write16(dev, 0x7A, tmp);

			tmp_control = b43legacy_get_lopair(phy, i, j * 2);
			b43legacy_phy_lo_g_state(dev, &control, tmp_control,
						 r27);
		}
	}

	/* Restoration */
	if (phy->gmode) {
		b43legacy_phy_write(dev, 0x0015, 0xE300);
		b43legacy_phy_write(dev, 0x0812, (r27 << 8) | 0xA0);
		udelay(5);
		b43legacy_phy_write(dev, 0x0812, (r27 << 8) | 0xA2);
		udelay(2);
		b43legacy_phy_write(dev, 0x0812, (r27 << 8) | 0xA3);
		b43legacy_voluntary_preempt();
	} else
		b43legacy_phy_write(dev, 0x0015, r27 | 0xEFA0);
	b43legacy_phy_lo_adjust(dev, is_initializing);
	b43legacy_phy_write(dev, 0x002E, 0x807F);
	if (phy->gmode)
		b43legacy_phy_write(dev, 0x002F, 0x0202);
	else
		b43legacy_phy_write(dev, 0x002F, 0x0101);
	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, regstack[4]);
	b43legacy_phy_write(dev, 0x0015, regstack[5]);
	b43legacy_phy_write(dev, 0x002A, regstack[6]);
	b43legacy_phy_write(dev, 0x0035, regstack[7]);
	b43legacy_phy_write(dev, 0x0060, regstack[8]);
	b43legacy_radio_write16(dev, 0x0043, regstack[9]);
	b43legacy_radio_write16(dev, 0x007A, regstack[10]);
	regstack[11] &= 0x00F0;
	regstack[11] |= (b43legacy_radio_read16(dev, 0x52) & 0x000F);
	b43legacy_radio_write16(dev, 0x52, regstack[11]);
	b43legacy_write16(dev, 0x03E2, regstack[3]);
	if (phy->gmode) {
		b43legacy_phy_write(dev, 0x0811, regstack[12]);
		b43legacy_phy_write(dev, 0x0812, regstack[13]);
		b43legacy_phy_write(dev, 0x0814, regstack[14]);
		b43legacy_phy_write(dev, 0x0815, regstack[15]);
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS, regstack[0]);
		b43legacy_phy_write(dev, 0x0802, regstack[1]);
	}
	b43legacy_radio_selectchannel(dev, oldchannel, 1);

#ifdef CONFIG_B43LEGACY_DEBUG
	{
		/* Sanity check for all lopairs. */
		for (i = 0; i < B43legacy_LO_COUNT; i++) {
			tmp_control = phy->_lo_pairs + i;
			if (tmp_control->low < -8 || tmp_control->low > 8 ||
			    tmp_control->high < -8 || tmp_control->high > 8)
				b43legacywarn(dev->wl,
				       "WARNING: Invalid LOpair (low: %d, high:"
				       " %d, index: %d)\n",
				       tmp_control->low, tmp_control->high, i);
		}
	}
#endif /* CONFIG_B43LEGACY_DEBUG */
}

static
void b43legacy_phy_lo_mark_current_used(struct b43legacy_wldev *dev)
{
	struct b43legacy_lopair *pair;

	pair = b43legacy_current_lopair(dev);
	pair->used = 1;
}

void b43legacy_phy_lo_mark_all_unused(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	struct b43legacy_lopair *pair;
	int i;

	for (i = 0; i < B43legacy_LO_COUNT; i++) {
		pair = phy->_lo_pairs + i;
		pair->used = 0;
	}
}

/* http://bcm-specs.sipsolutions.net/EstimatePowerOut
 * This function converts a TSSI value to dBm in Q5.2
 */
static s8 b43legacy_phy_estimate_power_out(struct b43legacy_wldev *dev, s8 tssi)
{
	struct b43legacy_phy *phy = &dev->phy;
	s8 dbm = 0;
	s32 tmp;

	tmp = phy->idle_tssi;
	tmp += tssi;
	tmp -= phy->savedpctlreg;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
	case B43legacy_PHYTYPE_G:
		tmp = limit_value(tmp, 0x00, 0x3F);
		dbm = phy->tssi2dbm[tmp];
		break;
	default:
		B43legacy_BUG_ON(1);
	}

	return dbm;
}

/* http://bcm-specs.sipsolutions.net/RecalculateTransmissionPower */
void b43legacy_phy_xmitpower(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 tmp;
	u16 txpower;
	s8 v0;
	s8 v1;
	s8 v2;
	s8 v3;
	s8 average;
	int max_pwr;
	s16 desired_pwr;
	s16 estimated_pwr;
	s16 pwr_adjust;
	s16 radio_att_delta;
	s16 baseband_att_delta;
	s16 radio_attenuation;
	s16 baseband_attenuation;
	unsigned long phylock_flags;

	if (phy->savedpctlreg == 0xFFFF)
		return;
	if ((dev->dev->bus->boardinfo.type == 0x0416) &&
	    is_bcm_board_vendor(dev))
		return;
#ifdef CONFIG_B43LEGACY_DEBUG
	if (phy->manual_txpower_control)
		return;
#endif

	B43legacy_BUG_ON(!(phy->type == B43legacy_PHYTYPE_B ||
			 phy->type == B43legacy_PHYTYPE_G));
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x0058);
	v0 = (s8)(tmp & 0x00FF);
	v1 = (s8)((tmp & 0xFF00) >> 8);
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x005A);
	v2 = (s8)(tmp & 0x00FF);
	v3 = (s8)((tmp & 0xFF00) >> 8);
	tmp = 0;

	if (v0 == 0x7F || v1 == 0x7F || v2 == 0x7F || v3 == 0x7F) {
		tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
					 0x0070);
		v0 = (s8)(tmp & 0x00FF);
		v1 = (s8)((tmp & 0xFF00) >> 8);
		tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
					 0x0072);
		v2 = (s8)(tmp & 0x00FF);
		v3 = (s8)((tmp & 0xFF00) >> 8);
		if (v0 == 0x7F || v1 == 0x7F || v2 == 0x7F || v3 == 0x7F)
			return;
		v0 = (v0 + 0x20) & 0x3F;
		v1 = (v1 + 0x20) & 0x3F;
		v2 = (v2 + 0x20) & 0x3F;
		v3 = (v3 + 0x20) & 0x3F;
		tmp = 1;
	}
	b43legacy_radio_clear_tssi(dev);

	average = (v0 + v1 + v2 + v3 + 2) / 4;

	if (tmp && (b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x005E)
	    & 0x8))
		average -= 13;

	estimated_pwr = b43legacy_phy_estimate_power_out(dev, average);

	max_pwr = dev->dev->bus->sprom.r1.maxpwr_bg;

	if ((dev->dev->bus->sprom.r1.boardflags_lo
	     & B43legacy_BFL_PACTRL) &&
	    (phy->type == B43legacy_PHYTYPE_G))
		max_pwr -= 0x3;
	if (unlikely(max_pwr <= 0)) {
		b43legacywarn(dev->wl, "Invalid max-TX-power value in SPROM."
			"\n");
		max_pwr = 74; /* fake it */
		dev->dev->bus->sprom.r1.maxpwr_bg = max_pwr;
	}

	/* Use regulatory information to get the maximum power.
	 * In the absence of such data from mac80211, we will use 20 dBm, which
	 * is the value for the EU, US, Canada, and most of the world.
	 * The regulatory maximum is reduced by the antenna gain (from sprom)
	 * and 1.5 dBm (a safety factor??). The result is in Q5.2 format
	 * which accounts for the factor of 4 */
#define REG_MAX_PWR 20
	max_pwr = min(REG_MAX_PWR * 4 - dev->dev->bus->sprom.r1.antenna_gain_bg
		      - 0x6, max_pwr);

	/* find the desired power in Q5.2 - power_level is in dBm
	 * and limit it - max_pwr is already in Q5.2 */
	desired_pwr = limit_value(phy->power_level << 2, 0, max_pwr);
	if (b43legacy_debug(dev, B43legacy_DBG_XMITPOWER))
		b43legacydbg(dev->wl, "Current TX power output: " Q52_FMT
		       " dBm, Desired TX power output: " Q52_FMT
		       " dBm\n", Q52_ARG(estimated_pwr),
		       Q52_ARG(desired_pwr));
	/* Check if we need to adjust the current power. The factor of 2 is
	 * for damping */
	pwr_adjust = (desired_pwr - estimated_pwr) / 2;
	/* RF attenuation delta
	 * The minus sign is because lower attenuation => more power */
	radio_att_delta = -(pwr_adjust + 7) >> 3;
	/* Baseband attenuation delta */
	baseband_att_delta = -(pwr_adjust >> 1) - (4 * radio_att_delta);
	/* Do we need to adjust anything? */
	if ((radio_att_delta == 0) && (baseband_att_delta == 0)) {
		b43legacy_phy_lo_mark_current_used(dev);
		return;
	}

	/* Calculate the new attenuation values. */
	baseband_attenuation = phy->bbatt;
	baseband_attenuation += baseband_att_delta;
	radio_attenuation = phy->rfatt;
	radio_attenuation += radio_att_delta;

	/* Get baseband and radio attenuation values into permitted ranges.
	 * baseband 0-11, radio 0-9.
	 * Radio attenuation affects power level 4 times as much as baseband.
	 */
	if (radio_attenuation < 0) {
		baseband_attenuation -= (4 * -radio_attenuation);
		radio_attenuation = 0;
	} else if (radio_attenuation > 9) {
		baseband_attenuation += (4 * (radio_attenuation - 9));
		radio_attenuation = 9;
	} else {
		while (baseband_attenuation < 0 && radio_attenuation > 0) {
			baseband_attenuation += 4;
			radio_attenuation--;
		}
		while (baseband_attenuation > 11 && radio_attenuation < 9) {
			baseband_attenuation -= 4;
			radio_attenuation++;
		}
	}
	baseband_attenuation = limit_value(baseband_attenuation, 0, 11);

	txpower = phy->txctl1;
	if ((phy->radio_ver == 0x2050) && (phy->radio_rev == 2)) {
		if (radio_attenuation <= 1) {
			if (txpower == 0) {
				txpower = 3;
				radio_attenuation += 2;
				baseband_attenuation += 2;
			} else if (dev->dev->bus->sprom.r1.boardflags_lo
				   & B43legacy_BFL_PACTRL) {
				baseband_attenuation += 4 *
						     (radio_attenuation - 2);
				radio_attenuation = 2;
			}
		} else if (radio_attenuation > 4 && txpower != 0) {
			txpower = 0;
			if (baseband_attenuation < 3) {
				radio_attenuation -= 3;
				baseband_attenuation += 2;
			} else {
				radio_attenuation -= 2;
				baseband_attenuation -= 2;
			}
		}
	}
	/* Save the control values */
	phy->txctl1 = txpower;
	baseband_attenuation = limit_value(baseband_attenuation, 0, 11);
	radio_attenuation = limit_value(radio_attenuation, 0, 9);
	phy->rfatt = radio_attenuation;
	phy->bbatt = baseband_attenuation;

	/* Adjust the hardware */
	b43legacy_phy_lock(dev, phylock_flags);
	b43legacy_radio_lock(dev);
	b43legacy_radio_set_txpower_bg(dev, baseband_attenuation,
				       radio_attenuation, txpower);
	b43legacy_phy_lo_mark_current_used(dev);
	b43legacy_radio_unlock(dev);
	b43legacy_phy_unlock(dev, phylock_flags);
}

static inline
s32 b43legacy_tssi2dbm_ad(s32 num, s32 den)
{
	if (num < 0)
		return num/den;
	else
		return (num+den/2)/den;
}

static inline
s8 b43legacy_tssi2dbm_entry(s8 entry [], u8 index, s16 pab0, s16 pab1, s16 pab2)
{
	s32 m1;
	s32 m2;
	s32 f = 256;
	s32 q;
	s32 delta;
	s8 i = 0;

	m1 = b43legacy_tssi2dbm_ad(16 * pab0 + index * pab1, 32);
	m2 = max(b43legacy_tssi2dbm_ad(32768 + index * pab2, 256), 1);
	do {
		if (i > 15)
			return -EINVAL;
		q = b43legacy_tssi2dbm_ad(f * 4096 -
					  b43legacy_tssi2dbm_ad(m2 * f, 16) *
					  f, 2048);
		delta = abs(q - f);
		f = q;
		i++;
	} while (delta >= 2);
	entry[index] = limit_value(b43legacy_tssi2dbm_ad(m1 * f, 8192),
				   -127, 128);
	return 0;
}

/* http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table */
int b43legacy_phy_init_tssi2dbm_table(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	s16 pab0;
	s16 pab1;
	s16 pab2;
	u8 idx;
	s8 *dyn_tssi2dbm;

	B43legacy_WARN_ON(!(phy->type == B43legacy_PHYTYPE_B ||
			  phy->type == B43legacy_PHYTYPE_G));
	pab0 = (s16)(dev->dev->bus->sprom.r1.pa0b0);
	pab1 = (s16)(dev->dev->bus->sprom.r1.pa0b1);
	pab2 = (s16)(dev->dev->bus->sprom.r1.pa0b2);

	if ((dev->dev->bus->chip_id == 0x4301) && (phy->radio_ver != 0x2050)) {
		phy->idle_tssi = 0x34;
		phy->tssi2dbm = b43legacy_tssi2dbm_b_table;
		return 0;
	}

	if (pab0 != 0 && pab1 != 0 && pab2 != 0 &&
	    pab0 != -1 && pab1 != -1 && pab2 != -1) {
		/* The pabX values are set in SPROM. Use them. */
		if ((s8)dev->dev->bus->sprom.r1.itssi_bg != 0 &&
		    (s8)dev->dev->bus->sprom.r1.itssi_bg != -1)
			phy->idle_tssi = (s8)(dev->dev->bus->sprom.r1.itssi_bg);
		else
			phy->idle_tssi = 62;
		dyn_tssi2dbm = kmalloc(64, GFP_KERNEL);
		if (dyn_tssi2dbm == NULL) {
			b43legacyerr(dev->wl, "Could not allocate memory "
			       "for tssi2dbm table\n");
			return -ENOMEM;
		}
		for (idx = 0; idx < 64; idx++)
			if (b43legacy_tssi2dbm_entry(dyn_tssi2dbm, idx, pab0,
						     pab1, pab2)) {
				phy->tssi2dbm = NULL;
				b43legacyerr(dev->wl, "Could not generate "
				       "tssi2dBm table\n");
				kfree(dyn_tssi2dbm);
				return -ENODEV;
			}
		phy->tssi2dbm = dyn_tssi2dbm;
		phy->dyn_tssi_tbl = 1;
	} else {
		/* pabX values not set in SPROM. */
		switch (phy->type) {
		case B43legacy_PHYTYPE_B:
			phy->idle_tssi = 0x34;
			phy->tssi2dbm = b43legacy_tssi2dbm_b_table;
			break;
		case B43legacy_PHYTYPE_G:
			phy->idle_tssi = 0x34;
			phy->tssi2dbm = b43legacy_tssi2dbm_g_table;
			break;
		}
	}

	return 0;
}

int b43legacy_phy_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	int err = -ENODEV;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
		switch (phy->rev) {
		case 2:
			b43legacy_phy_initb2(dev);
			err = 0;
			break;
		case 4:
			b43legacy_phy_initb4(dev);
			err = 0;
			break;
		case 5:
			b43legacy_phy_initb5(dev);
			err = 0;
			break;
		case 6:
			b43legacy_phy_initb6(dev);
			err = 0;
			break;
		}
		break;
	case B43legacy_PHYTYPE_G:
		b43legacy_phy_initg(dev);
		err = 0;
		break;
	}
	if (err)
		b43legacyerr(dev->wl, "Unknown PHYTYPE found\n");

	return err;
}

void b43legacy_phy_set_antenna_diversity(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 antennadiv;
	u16 offset;
	u16 value;
	u32 ucodeflags;

	antennadiv = phy->antenna_diversity;

	if (antennadiv == 0xFFFF)
		antennadiv = 3;
	B43legacy_WARN_ON(antennadiv > 3);

	ucodeflags = b43legacy_shm_read32(dev, B43legacy_SHM_SHARED,
					  B43legacy_UCODEFLAGS_OFFSET);
	b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
			      B43legacy_UCODEFLAGS_OFFSET,
			      ucodeflags & ~B43legacy_UCODEFLAG_AUTODIV);

	switch (phy->type) {
	case B43legacy_PHYTYPE_G:
		offset = 0x0400;

		if (antennadiv == 2)
			value = (3/*automatic*/ << 7);
		else
			value = (antennadiv << 7);
		b43legacy_phy_write(dev, offset + 1,
				    (b43legacy_phy_read(dev, offset + 1)
				    & 0x7E7F) | value);

		if (antennadiv >= 2) {
			if (antennadiv == 2)
				value = (antennadiv << 7);
			else
				value = (0/*force0*/ << 7);
			b43legacy_phy_write(dev, offset + 0x2B,
					    (b43legacy_phy_read(dev,
					    offset + 0x2B)
					    & 0xFEFF) | value);
		}

		if (phy->type == B43legacy_PHYTYPE_G) {
			if (antennadiv >= 2)
				b43legacy_phy_write(dev, 0x048C,
						    b43legacy_phy_read(dev,
						    0x048C) | 0x2000);
			else
				b43legacy_phy_write(dev, 0x048C,
						    b43legacy_phy_read(dev,
						    0x048C) & ~0x2000);
			if (phy->rev >= 2) {
				b43legacy_phy_write(dev, 0x0461,
						    b43legacy_phy_read(dev,
						    0x0461) | 0x0010);
				b43legacy_phy_write(dev, 0x04AD,
						    (b43legacy_phy_read(dev,
						    0x04AD)
						    & 0x00FF) | 0x0015);
				if (phy->rev == 2)
					b43legacy_phy_write(dev, 0x0427,
							    0x0008);
				else
					b43legacy_phy_write(dev, 0x0427,
						(b43legacy_phy_read(dev, 0x0427)
						 & 0x00FF) | 0x0008);
			} else if (phy->rev >= 6)
				b43legacy_phy_write(dev, 0x049B, 0x00DC);
		} else {
			if (phy->rev < 3)
				b43legacy_phy_write(dev, 0x002B,
						    (b43legacy_phy_read(dev,
						    0x002B) & 0x00FF)
						    | 0x0024);
			else {
				b43legacy_phy_write(dev, 0x0061,
						    b43legacy_phy_read(dev,
						    0x0061) | 0x0010);
				if (phy->rev == 3) {
					b43legacy_phy_write(dev, 0x0093,
							    0x001D);
					b43legacy_phy_write(dev, 0x0027,
							    0x0008);
				} else {
					b43legacy_phy_write(dev, 0x0093,
							    0x003A);
					b43legacy_phy_write(dev, 0x0027,
						(b43legacy_phy_read(dev, 0x0027)
						 & 0x00FF) | 0x0008);
				}
			}
		}
		break;
	case B43legacy_PHYTYPE_B:
		if (dev->dev->id.revision == 2)
			value = (3/*automatic*/ << 7);
		else
			value = (antennadiv << 7);
		b43legacy_phy_write(dev, 0x03E2,
				    (b43legacy_phy_read(dev, 0x03E2)
				    & 0xFE7F) | value);
		break;
	default:
		B43legacy_WARN_ON(1);
	}

	if (antennadiv >= 2) {
		ucodeflags = b43legacy_shm_read32(dev, B43legacy_SHM_SHARED,
						  B43legacy_UCODEFLAGS_OFFSET);
		b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
				      B43legacy_UCODEFLAGS_OFFSET,
				      ucodeflags | B43legacy_UCODEFLAG_AUTODIV);
	}

	phy->antenna_diversity = antennadiv;
}

/* Set the PowerSavingControlBits.
 * Bitvalues:
 *   0  => unset the bit
 *   1  => set the bit
 *   -1 => calculate the bit
 */
void b43legacy_power_saving_ctl_bits(struct b43legacy_wldev *dev,
				     int bit25, int bit26)
{
	int i;
	u32 status;

/* FIXME: Force 25 to off and 26 to on for now: */
bit25 = 0;
bit26 = 1;

	if (bit25 == -1) {
		/* TODO: If powersave is not off and FIXME is not set and we
		 *	are not in adhoc and thus is not an AP and we arei
		 *	associated, set bit 25 */
	}
	if (bit26 == -1) {
		/* TODO: If the device is awake or this is an AP, or we are
		 *	scanning, or FIXME, or we are associated, or FIXME,
		 *	or the latest PS-Poll packet sent was successful,
		 *	set bit26  */
	}
	status = b43legacy_read32(dev, B43legacy_MMIO_STATUS_BITFIELD);
	if (bit25)
		status |= B43legacy_SBF_PS1;
	else
		status &= ~B43legacy_SBF_PS1;
	if (bit26)
		status |= B43legacy_SBF_PS2;
	else
		status &= ~B43legacy_SBF_PS2;
	b43legacy_write32(dev, B43legacy_MMIO_STATUS_BITFIELD, status);
	if (bit26 && dev->dev->id.revision >= 5) {
		for (i = 0; i < 100; i++) {
			if (b43legacy_shm_read32(dev, B43legacy_SHM_SHARED,
						 0x0040) != 4)
				break;
			udelay(10);
		}
	}
}
