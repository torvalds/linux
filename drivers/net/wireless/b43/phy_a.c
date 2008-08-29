/*

  Broadcom B43 wireless driver
  IEEE 802.11a PHY driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005-2007 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2008 Michael Buesch <mb@bu3sch.de>
  Copyright (c) 2005, 2006 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005, 2006 Andreas Jaggi <andreas.jaggi@waterwave.ch>

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

#include "b43.h"
#include "phy_a.h"
#include "phy_common.h"
#include "wa.h"
#include "tables.h"
#include "main.h"


/* Get the freq, as it has to be written to the device. */
static inline u16 channel2freq_a(u8 channel)
{
	B43_WARN_ON(channel > 200);

	return (5000 + 5 * channel);
}

static inline u16 freq_r3A_value(u16 frequency)
{
	u16 value;

	if (frequency < 5091)
		value = 0x0040;
	else if (frequency < 5321)
		value = 0x0000;
	else if (frequency < 5806)
		value = 0x0080;
	else
		value = 0x0040;

	return value;
}

void b43_radio_set_tx_iq(struct b43_wldev *dev)
{
	static const u8 data_high[5] = { 0x00, 0x40, 0x80, 0x90, 0xD0 };
	static const u8 data_low[5] = { 0x00, 0x01, 0x05, 0x06, 0x0A };
	u16 tmp = b43_radio_read16(dev, 0x001E);
	int i, j;

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 5; j++) {
			if (tmp == (data_high[i] << 4 | data_low[j])) {
				b43_phy_write(dev, 0x0069,
					      (i - j) << 8 | 0x00C0);
				return;
			}
		}
	}
}

static void aphy_channel_switch(struct b43_wldev *dev, unsigned int channel)
{
	u16 freq, r8, tmp;

	freq = channel2freq_a(channel);

	r8 = b43_radio_read16(dev, 0x0008);
	b43_write16(dev, 0x03F0, freq);
	b43_radio_write16(dev, 0x0008, r8);

	//TODO: write max channel TX power? to Radio 0x2D
	tmp = b43_radio_read16(dev, 0x002E);
	tmp &= 0x0080;
	//TODO: OR tmp with the Power out estimation for this channel?
	b43_radio_write16(dev, 0x002E, tmp);

	if (freq >= 4920 && freq <= 5500) {
		/*
		 * r8 = (((freq * 15 * 0xE1FC780F) >> 32) / 29) & 0x0F;
		 *    = (freq * 0.025862069
		 */
		r8 = 3 * freq / 116;	/* is equal to r8 = freq * 0.025862 */
	}
	b43_radio_write16(dev, 0x0007, (r8 << 4) | r8);
	b43_radio_write16(dev, 0x0020, (r8 << 4) | r8);
	b43_radio_write16(dev, 0x0021, (r8 << 4) | r8);
	b43_radio_write16(dev, 0x0022, (b43_radio_read16(dev, 0x0022)
					& 0x000F) | (r8 << 4));
	b43_radio_write16(dev, 0x002A, (r8 << 4));
	b43_radio_write16(dev, 0x002B, (r8 << 4));
	b43_radio_write16(dev, 0x0008, (b43_radio_read16(dev, 0x0008)
					& 0x00F0) | (r8 << 4));
	b43_radio_write16(dev, 0x0029, (b43_radio_read16(dev, 0x0029)
					& 0xFF0F) | 0x00B0);
	b43_radio_write16(dev, 0x0035, 0x00AA);
	b43_radio_write16(dev, 0x0036, 0x0085);
	b43_radio_write16(dev, 0x003A, (b43_radio_read16(dev, 0x003A)
					& 0xFF20) |
			  freq_r3A_value(freq));
	b43_radio_write16(dev, 0x003D,
			  b43_radio_read16(dev, 0x003D) & 0x00FF);
	b43_radio_write16(dev, 0x0081, (b43_radio_read16(dev, 0x0081)
					& 0xFF7F) | 0x0080);
	b43_radio_write16(dev, 0x0035,
			  b43_radio_read16(dev, 0x0035) & 0xFFEF);
	b43_radio_write16(dev, 0x0035, (b43_radio_read16(dev, 0x0035)
					& 0xFFEF) | 0x0010);
	b43_radio_set_tx_iq(dev);
	//TODO: TSSI2dbm workaround
//FIXME	b43_phy_xmitpower(dev);
}

void b43_radio_init2060(struct b43_wldev *dev)
{
	b43_radio_write16(dev, 0x0004, 0x00C0);
	b43_radio_write16(dev, 0x0005, 0x0008);
	b43_radio_write16(dev, 0x0009, 0x0040);
	b43_radio_write16(dev, 0x0005, 0x00AA);
	b43_radio_write16(dev, 0x0032, 0x008F);
	b43_radio_write16(dev, 0x0006, 0x008F);
	b43_radio_write16(dev, 0x0034, 0x008F);
	b43_radio_write16(dev, 0x002C, 0x0007);
	b43_radio_write16(dev, 0x0082, 0x0080);
	b43_radio_write16(dev, 0x0080, 0x0000);
	b43_radio_write16(dev, 0x003F, 0x00DA);
	b43_radio_write16(dev, 0x0005, b43_radio_read16(dev, 0x0005) & ~0x0008);
	b43_radio_write16(dev, 0x0081, b43_radio_read16(dev, 0x0081) & ~0x0010);
	b43_radio_write16(dev, 0x0081, b43_radio_read16(dev, 0x0081) & ~0x0020);
	b43_radio_write16(dev, 0x0081, b43_radio_read16(dev, 0x0081) & ~0x0020);
	msleep(1);		/* delay 400usec */

	b43_radio_write16(dev, 0x0081,
			  (b43_radio_read16(dev, 0x0081) & ~0x0020) | 0x0010);
	msleep(1);		/* delay 400usec */

	b43_radio_write16(dev, 0x0005,
			  (b43_radio_read16(dev, 0x0005) & ~0x0008) | 0x0008);
	b43_radio_write16(dev, 0x0085, b43_radio_read16(dev, 0x0085) & ~0x0010);
	b43_radio_write16(dev, 0x0005, b43_radio_read16(dev, 0x0005) & ~0x0008);
	b43_radio_write16(dev, 0x0081, b43_radio_read16(dev, 0x0081) & ~0x0040);
	b43_radio_write16(dev, 0x0081,
			  (b43_radio_read16(dev, 0x0081) & ~0x0040) | 0x0040);
	b43_radio_write16(dev, 0x0005,
			  (b43_radio_read16(dev, 0x0081) & ~0x0008) | 0x0008);
	b43_phy_write(dev, 0x0063, 0xDDC6);
	b43_phy_write(dev, 0x0069, 0x07BE);
	b43_phy_write(dev, 0x006A, 0x0000);

	aphy_channel_switch(dev, dev->phy.ops->get_default_chan(dev));

	msleep(1);
}

static void b43_phy_rssiagc(struct b43_wldev *dev, u8 enable)
{
	int i;

	if (dev->phy.rev < 3) {
		if (enable)
			for (i = 0; i < B43_TAB_RSSIAGC1_SIZE; i++) {
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_LNAHPFGAIN1, i, 0xFFF8);
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_WRSSI, i, 0xFFF8);
			}
		else
			for (i = 0; i < B43_TAB_RSSIAGC1_SIZE; i++) {
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_LNAHPFGAIN1, i, b43_tab_rssiagc1[i]);
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_WRSSI, i, b43_tab_rssiagc1[i]);
			}
	} else {
		if (enable)
			for (i = 0; i < B43_TAB_RSSIAGC1_SIZE; i++)
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_WRSSI, i, 0x0820);
		else
			for (i = 0; i < B43_TAB_RSSIAGC2_SIZE; i++)
				b43_ofdmtab_write16(dev,
					B43_OFDMTAB_WRSSI, i, b43_tab_rssiagc2[i]);
	}
}

static void b43_phy_ww(struct b43_wldev *dev)
{
	u16 b, curr_s, best_s = 0xFFFF;
	int i;

	b43_phy_write(dev, B43_PHY_CRS0,
		b43_phy_read(dev, B43_PHY_CRS0) & ~B43_PHY_CRS0_EN);
	b43_phy_write(dev, B43_PHY_OFDM(0x1B),
		b43_phy_read(dev, B43_PHY_OFDM(0x1B)) | 0x1000);
	b43_phy_write(dev, B43_PHY_OFDM(0x82),
		(b43_phy_read(dev, B43_PHY_OFDM(0x82)) & 0xF0FF) | 0x0300);
	b43_radio_write16(dev, 0x0009,
		b43_radio_read16(dev, 0x0009) | 0x0080);
	b43_radio_write16(dev, 0x0012,
		(b43_radio_read16(dev, 0x0012) & 0xFFFC) | 0x0002);
	b43_wa_initgains(dev);
	b43_phy_write(dev, B43_PHY_OFDM(0xBA), 0x3ED5);
	b = b43_phy_read(dev, B43_PHY_PWRDOWN);
	b43_phy_write(dev, B43_PHY_PWRDOWN, (b & 0xFFF8) | 0x0005);
	b43_radio_write16(dev, 0x0004,
		b43_radio_read16(dev, 0x0004) | 0x0004);
	for (i = 0x10; i <= 0x20; i++) {
		b43_radio_write16(dev, 0x0013, i);
		curr_s = b43_phy_read(dev, B43_PHY_OTABLEQ) & 0x00FF;
		if (!curr_s) {
			best_s = 0x0000;
			break;
		} else if (curr_s >= 0x0080)
			curr_s = 0x0100 - curr_s;
		if (curr_s < best_s)
			best_s = curr_s;
	}
	b43_phy_write(dev, B43_PHY_PWRDOWN, b);
	b43_radio_write16(dev, 0x0004,
		b43_radio_read16(dev, 0x0004) & 0xFFFB);
	b43_radio_write16(dev, 0x0013, best_s);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1_R1, 0, 0xFFEC);
	b43_phy_write(dev, B43_PHY_OFDM(0xB7), 0x1E80);
	b43_phy_write(dev, B43_PHY_OFDM(0xB6), 0x1C00);
	b43_phy_write(dev, B43_PHY_OFDM(0xB5), 0x0EC0);
	b43_phy_write(dev, B43_PHY_OFDM(0xB2), 0x00C0);
	b43_phy_write(dev, B43_PHY_OFDM(0xB9), 0x1FFF);
	b43_phy_write(dev, B43_PHY_OFDM(0xBB),
		(b43_phy_read(dev, B43_PHY_OFDM(0xBB)) & 0xF000) | 0x0053);
	b43_phy_write(dev, B43_PHY_OFDM61,
		(b43_phy_read(dev, B43_PHY_OFDM61) & 0xFE1F) | 0x0120);
	b43_phy_write(dev, B43_PHY_OFDM(0x13),
		(b43_phy_read(dev, B43_PHY_OFDM(0x13)) & 0x0FFF) | 0x3000);
	b43_phy_write(dev, B43_PHY_OFDM(0x14),
		(b43_phy_read(dev, B43_PHY_OFDM(0x14)) & 0x0FFF) | 0x3000);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 6, 0x0017);
	for (i = 0; i < 6; i++)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, i, 0x000F);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 0x0D, 0x000E);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 0x0E, 0x0011);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 0x0F, 0x0013);
	b43_phy_write(dev, B43_PHY_OFDM(0x33), 0x5030);
	b43_phy_write(dev, B43_PHY_CRS0,
		b43_phy_read(dev, B43_PHY_CRS0) | B43_PHY_CRS0_EN);
}

static void hardware_pctl_init_aphy(struct b43_wldev *dev)
{
	//TODO
}

void b43_phy_inita(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct b43_phy *phy = &dev->phy;

	/* This lowlevel A-PHY init is also called from G-PHY init.
	 * So we must not access phy->a, if called from G-PHY code.
	 */
	B43_WARN_ON((phy->type != B43_PHYTYPE_A) &&
		    (phy->type != B43_PHYTYPE_G));

	might_sleep();

	if (phy->rev >= 6) {
		if (phy->type == B43_PHYTYPE_A)
			b43_phy_write(dev, B43_PHY_OFDM(0x1B),
				b43_phy_read(dev, B43_PHY_OFDM(0x1B)) & ~0x1000);
		if (b43_phy_read(dev, B43_PHY_ENCORE) & B43_PHY_ENCORE_EN)
			b43_phy_write(dev, B43_PHY_ENCORE,
				b43_phy_read(dev, B43_PHY_ENCORE) | 0x0010);
		else
			b43_phy_write(dev, B43_PHY_ENCORE,
				b43_phy_read(dev, B43_PHY_ENCORE) & ~0x1010);
	}

	b43_wa_all(dev);

	if (phy->type == B43_PHYTYPE_A) {
		if (phy->gmode && (phy->rev < 3))
			b43_phy_write(dev, 0x0034,
				b43_phy_read(dev, 0x0034) | 0x0001);
		b43_phy_rssiagc(dev, 0);

		b43_phy_write(dev, B43_PHY_CRS0,
			b43_phy_read(dev, B43_PHY_CRS0) | B43_PHY_CRS0_EN);

		b43_radio_init2060(dev);

		if ((bus->boardinfo.vendor == SSB_BOARDVENDOR_BCM) &&
		    ((bus->boardinfo.type == SSB_BOARD_BU4306) ||
		     (bus->boardinfo.type == SSB_BOARD_BU4309))) {
			; //TODO: A PHY LO
		}

		if (phy->rev >= 3)
			b43_phy_ww(dev);

		hardware_pctl_init_aphy(dev);

		//TODO: radar detection
	}

	if ((phy->type == B43_PHYTYPE_G) &&
	    (dev->dev->bus->sprom.boardflags_lo & B43_BFL_PACTRL)) {
		b43_phy_write(dev, B43_PHY_OFDM(0x6E),
				  (b43_phy_read(dev, B43_PHY_OFDM(0x6E))
				   & 0xE000) | 0x3CF);
	}
}

static int b43_aphy_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_a *aphy;

	aphy = kzalloc(sizeof(*aphy), GFP_KERNEL);
	if (!aphy)
		return -ENOMEM;
	dev->phy.a = aphy;

	//TODO init struct b43_phy_a

	return 0;
}

static int b43_aphy_op_init(struct b43_wldev *dev)
{
	struct b43_phy_a *aphy = dev->phy.a;

	b43_phy_inita(dev);
	aphy->initialised = 1;

	return 0;
}

static void b43_aphy_op_exit(struct b43_wldev *dev)
{
	struct b43_phy_a *aphy = dev->phy.a;

	if (aphy->initialised) {
		//TODO
		aphy->initialised = 0;
	}
	//TODO
	kfree(aphy);
	dev->phy.a = NULL;
}

static inline u16 adjust_phyreg(struct b43_wldev *dev, u16 offset)
{
	/* OFDM registers are base-registers for the A-PHY. */
	if ((offset & B43_PHYROUTE) == B43_PHYROUTE_OFDM_GPHY) {
		offset &= ~B43_PHYROUTE;
		offset |= B43_PHYROUTE_BASE;
	}

#if B43_DEBUG
	if ((offset & B43_PHYROUTE) == B43_PHYROUTE_EXT_GPHY) {
		/* Ext-G registers are only available on G-PHYs */
		b43err(dev->wl, "Invalid EXT-G PHY access at "
		       "0x%04X on A-PHY\n", offset);
		dump_stack();
	}
	if ((offset & B43_PHYROUTE) == B43_PHYROUTE_N_BMODE) {
		/* N-BMODE registers are only available on N-PHYs */
		b43err(dev->wl, "Invalid N-BMODE PHY access at "
		       "0x%04X on A-PHY\n", offset);
		dump_stack();
	}
#endif /* B43_DEBUG */

	return offset;
}

static u16 b43_aphy_op_read(struct b43_wldev *dev, u16 reg)
{
	reg = adjust_phyreg(dev, reg);
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_aphy_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	reg = adjust_phyreg(dev, reg);
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static u16 b43_aphy_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);
	/* A-PHY needs 0x40 for read access */
	reg |= 0x40;

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
}

static void b43_aphy_op_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO_DATA_LOW, value);
}

static bool b43_aphy_op_supports_hwpctl(struct b43_wldev *dev)
{
	return (dev->phy.rev >= 5);
}

static void b43_aphy_op_software_rfkill(struct b43_wldev *dev,
					enum rfkill_state state)
{//TODO
}

static int b43_aphy_op_switch_channel(struct b43_wldev *dev,
				      unsigned int new_channel)
{
	if (new_channel > 200)
		return -EINVAL;
	aphy_channel_switch(dev, new_channel);

	return 0;
}

static unsigned int b43_aphy_op_get_default_chan(struct b43_wldev *dev)
{
	return 36; /* Default to channel 36 */
}

static void b43_aphy_op_set_rx_antenna(struct b43_wldev *dev, int antenna)
{//TODO
	struct b43_phy *phy = &dev->phy;
	u64 hf;
	u16 tmp;
	int autodiv = 0;

	if (antenna == B43_ANTENNA_AUTO0 || antenna == B43_ANTENNA_AUTO1)
		autodiv = 1;

	hf = b43_hf_read(dev);
	hf &= ~B43_HF_ANTDIVHELP;
	b43_hf_write(dev, hf);

	tmp = b43_phy_read(dev, B43_PHY_BBANDCFG);
	tmp &= ~B43_PHY_BBANDCFG_RXANT;
	tmp |= (autodiv ? B43_ANTENNA_AUTO0 : antenna)
	    << B43_PHY_BBANDCFG_RXANT_SHIFT;
	b43_phy_write(dev, B43_PHY_BBANDCFG, tmp);

	if (autodiv) {
		tmp = b43_phy_read(dev, B43_PHY_ANTDWELL);
		if (antenna == B43_ANTENNA_AUTO0)
			tmp &= ~B43_PHY_ANTDWELL_AUTODIV1;
		else
			tmp |= B43_PHY_ANTDWELL_AUTODIV1;
		b43_phy_write(dev, B43_PHY_ANTDWELL, tmp);
	}
	if (phy->rev < 3) {
		tmp = b43_phy_read(dev, B43_PHY_ANTDWELL);
		tmp = (tmp & 0xFF00) | 0x24;
		b43_phy_write(dev, B43_PHY_ANTDWELL, tmp);
	} else {
		tmp = b43_phy_read(dev, B43_PHY_OFDM61);
		tmp |= 0x10;
		b43_phy_write(dev, B43_PHY_OFDM61, tmp);
		if (phy->analog == 3) {
			b43_phy_write(dev, B43_PHY_CLIPPWRDOWNT,
				      0x1D);
			b43_phy_write(dev, B43_PHY_ADIVRELATED,
				      8);
		} else {
			b43_phy_write(dev, B43_PHY_CLIPPWRDOWNT,
				      0x3A);
			tmp =
			    b43_phy_read(dev,
					 B43_PHY_ADIVRELATED);
			tmp = (tmp & 0xFF00) | 8;
			b43_phy_write(dev, B43_PHY_ADIVRELATED,
				      tmp);
		}
	}

	hf |= B43_HF_ANTDIVHELP;
	b43_hf_write(dev, hf);
}

static void b43_aphy_op_adjust_txpower(struct b43_wldev *dev)
{//TODO
}

static enum b43_txpwr_result b43_aphy_op_recalc_txpower(struct b43_wldev *dev,
							bool ignore_tssi)
{//TODO
	return B43_TXPWR_RES_DONE;
}

static void b43_aphy_op_pwork_15sec(struct b43_wldev *dev)
{//TODO
}

static void b43_aphy_op_pwork_60sec(struct b43_wldev *dev)
{//TODO
}

const struct b43_phy_operations b43_phyops_a = {
	.allocate		= b43_aphy_op_allocate,
	.init			= b43_aphy_op_init,
	.exit			= b43_aphy_op_exit,
	.phy_read		= b43_aphy_op_read,
	.phy_write		= b43_aphy_op_write,
	.radio_read		= b43_aphy_op_radio_read,
	.radio_write		= b43_aphy_op_radio_write,
	.supports_hwpctl	= b43_aphy_op_supports_hwpctl,
	.software_rfkill	= b43_aphy_op_software_rfkill,
	.switch_channel		= b43_aphy_op_switch_channel,
	.get_default_chan	= b43_aphy_op_get_default_chan,
	.set_rx_antenna		= b43_aphy_op_set_rx_antenna,
	.recalc_txpower		= b43_aphy_op_recalc_txpower,
	.adjust_txpower		= b43_aphy_op_adjust_txpower,
	.pwork_15sec		= b43_aphy_op_pwork_15sec,
	.pwork_60sec		= b43_aphy_op_pwork_60sec,
};
