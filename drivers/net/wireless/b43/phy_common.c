/*

  Broadcom B43 wireless driver
  Common PHY routines

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

#include "phy_common.h"
#include "phy_g.h"
#include "phy_a.h"
#include "nphy.h"
#include "b43.h"
#include "main.h"


int b43_phy_operations_setup(struct b43_wldev *dev)
{
	struct b43_phy *phy = &(dev->phy);
	int err;

	phy->ops = NULL;

	switch (phy->type) {
	case B43_PHYTYPE_A:
		phy->ops = &b43_phyops_a;
		break;
	case B43_PHYTYPE_G:
		phy->ops = &b43_phyops_g;
		break;
	case B43_PHYTYPE_N:
#ifdef CONFIG_B43_NPHY
		phy->ops = &b43_phyops_n;
#endif
		break;
	case B43_PHYTYPE_LP:
		/* FIXME: Not yet */
		break;
	}
	if (B43_WARN_ON(!phy->ops))
		return -ENODEV;

	err = phy->ops->allocate(dev);
	if (err)
		phy->ops = NULL;

	return err;
}

int b43_phy_init(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	const struct b43_phy_operations *ops = phy->ops;
	int err;

	phy->channel = ops->get_default_chan(dev);

	ops->software_rfkill(dev, RFKILL_STATE_UNBLOCKED);
	err = ops->init(dev);
	if (err) {
		b43err(dev->wl, "PHY init failed\n");
		goto err_block_rf;
	}
	/* Make sure to switch hardware and firmware (SHM) to
	 * the default channel. */
	err = b43_switch_channel(dev, ops->get_default_chan(dev));
	if (err) {
		b43err(dev->wl, "PHY init: Channel switch to default failed\n");
		goto err_phy_exit;
	}

	return 0;

err_phy_exit:
	if (ops->exit)
		ops->exit(dev);
err_block_rf:
	ops->software_rfkill(dev, RFKILL_STATE_SOFT_BLOCKED);

	return err;
}

void b43_phy_exit(struct b43_wldev *dev)
{
	const struct b43_phy_operations *ops = dev->phy.ops;

	ops->software_rfkill(dev, RFKILL_STATE_SOFT_BLOCKED);
	if (ops->exit)
		ops->exit(dev);
}

bool b43_has_hardware_pctl(struct b43_wldev *dev)
{
	if (!dev->phy.hardware_power_control)
		return 0;
	if (!dev->phy.ops->supports_hwpctl)
		return 0;
	return dev->phy.ops->supports_hwpctl(dev);
}

void b43_radio_lock(struct b43_wldev *dev)
{
	u32 macctl;

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	B43_WARN_ON(macctl & B43_MACCTL_RADIOLOCK);
	macctl |= B43_MACCTL_RADIOLOCK;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Commit the write and wait for the device
	 * to exit any radio register access. */
	b43_read32(dev, B43_MMIO_MACCTL);
	udelay(10);
}

void b43_radio_unlock(struct b43_wldev *dev)
{
	u32 macctl;

	/* Commit any write */
	b43_read16(dev, B43_MMIO_PHY_VER);
	/* unlock */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	B43_WARN_ON(!(macctl & B43_MACCTL_RADIOLOCK));
	macctl &= ~B43_MACCTL_RADIOLOCK;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
}

void b43_phy_lock(struct b43_wldev *dev)
{
#if B43_DEBUG
	B43_WARN_ON(dev->phy.phy_locked);
	dev->phy.phy_locked = 1;
#endif
	B43_WARN_ON(dev->dev->id.revision < 3);

	if (!b43_is_mode(dev->wl, IEEE80211_IF_TYPE_AP))
		b43_power_saving_ctl_bits(dev, B43_PS_AWAKE);
}

void b43_phy_unlock(struct b43_wldev *dev)
{
#if B43_DEBUG
	B43_WARN_ON(!dev->phy.phy_locked);
	dev->phy.phy_locked = 0;
#endif
	B43_WARN_ON(dev->dev->id.revision < 3);

	if (!b43_is_mode(dev->wl, IEEE80211_IF_TYPE_AP))
		b43_power_saving_ctl_bits(dev, 0);
}

u16 b43_radio_read(struct b43_wldev *dev, u16 reg)
{
	return dev->phy.ops->radio_read(dev, reg);
}

void b43_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	dev->phy.ops->radio_write(dev, reg, value);
}

void b43_radio_mask(struct b43_wldev *dev, u16 offset, u16 mask)
{
	b43_radio_write16(dev, offset,
			  b43_radio_read16(dev, offset) & mask);
}

void b43_radio_set(struct b43_wldev *dev, u16 offset, u16 set)
{
	b43_radio_write16(dev, offset,
			  b43_radio_read16(dev, offset) | set);
}

void b43_radio_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set)
{
	b43_radio_write16(dev, offset,
			  (b43_radio_read16(dev, offset) & mask) | set);
}

u16 b43_phy_read(struct b43_wldev *dev, u16 reg)
{
	return dev->phy.ops->phy_read(dev, reg);
}

void b43_phy_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	dev->phy.ops->phy_write(dev, reg, value);
}

void b43_phy_mask(struct b43_wldev *dev, u16 offset, u16 mask)
{
	b43_phy_write(dev, offset,
		      b43_phy_read(dev, offset) & mask);
}

void b43_phy_set(struct b43_wldev *dev, u16 offset, u16 set)
{
	b43_phy_write(dev, offset,
		      b43_phy_read(dev, offset) | set);
}

void b43_phy_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set)
{
	b43_phy_write(dev, offset,
		      (b43_phy_read(dev, offset) & mask) | set);
}

int b43_switch_channel(struct b43_wldev *dev, unsigned int new_channel)
{
	struct b43_phy *phy = &(dev->phy);
	u16 channelcookie, savedcookie;
	int err;

	if (new_channel == B43_DEFAULT_CHANNEL)
		new_channel = phy->ops->get_default_chan(dev);

	/* First we set the channel radio code to prevent the
	 * firmware from sending ghost packets.
	 */
	channelcookie = new_channel;
	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
		channelcookie |= 0x100;
	//FIXME set 40Mhz flag if required
	savedcookie = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_CHAN);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_CHAN, channelcookie);

	/* Now try to switch the PHY hardware channel. */
	err = phy->ops->switch_channel(dev, new_channel);
	if (err)
		goto err_restore_cookie;

	dev->phy.channel = new_channel;
	/* Wait for the radio to tune to the channel and stabilize. */
	msleep(8);

	return 0;

err_restore_cookie:
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_CHAN, savedcookie);

	return err;
}

void b43_software_rfkill(struct b43_wldev *dev, enum rfkill_state state)
{
	struct b43_phy *phy = &dev->phy;

	if (state == RFKILL_STATE_HARD_BLOCKED) {
		/* We cannot hardware-block the device */
		state = RFKILL_STATE_SOFT_BLOCKED;
	}

	phy->ops->software_rfkill(dev, state);
	phy->radio_on = (state == RFKILL_STATE_UNBLOCKED);
}
