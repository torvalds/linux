// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  Common PHY routines

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005-2007 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2008 Michael Buesch <m@bues.ch>
  Copyright (c) 2005, 2006 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005, 2006 Andreas Jaggi <andreas.jaggi@waterwave.ch>


*/

#include "phy_common.h"
#include "phy_g.h"
#include "phy_a.h"
#include "phy_n.h"
#include "phy_lp.h"
#include "phy_ht.h"
#include "phy_lcn.h"
#include "phy_ac.h"
#include "b43.h"
#include "main.h"


int b43_phy_allocate(struct b43_wldev *dev)
{
	struct b43_phy *phy = &(dev->phy);
	int err;

	phy->ops = NULL;

	switch (phy->type) {
	case B43_PHYTYPE_G:
#ifdef CONFIG_B43_PHY_G
		phy->ops = &b43_phyops_g;
#endif
		break;
	case B43_PHYTYPE_N:
#ifdef CONFIG_B43_PHY_N
		phy->ops = &b43_phyops_n;
#endif
		break;
	case B43_PHYTYPE_LP:
#ifdef CONFIG_B43_PHY_LP
		phy->ops = &b43_phyops_lp;
#endif
		break;
	case B43_PHYTYPE_HT:
#ifdef CONFIG_B43_PHY_HT
		phy->ops = &b43_phyops_ht;
#endif
		break;
	case B43_PHYTYPE_LCN:
#ifdef CONFIG_B43_PHY_LCN
		phy->ops = &b43_phyops_lcn;
#endif
		break;
	case B43_PHYTYPE_AC:
#ifdef CONFIG_B43_PHY_AC
		phy->ops = &b43_phyops_ac;
#endif
		break;
	}
	if (B43_WARN_ON(!phy->ops))
		return -ENODEV;

	err = phy->ops->allocate(dev);
	if (err)
		phy->ops = NULL;

	return err;
}

void b43_phy_free(struct b43_wldev *dev)
{
	dev->phy.ops->free(dev);
	dev->phy.ops = NULL;
}

int b43_phy_init(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	const struct b43_phy_operations *ops = phy->ops;
	int err;

	/* During PHY init we need to use some channel. On the first init this
	 * function is called *before* b43_op_config, so our pointer is NULL.
	 */
	if (!phy->chandef) {
		phy->chandef = &dev->wl->hw->conf.chandef;
		phy->channel = phy->chandef->chan->hw_value;
	}

	phy->ops->switch_analog(dev, true);
	b43_software_rfkill(dev, false);

	err = ops->init(dev);
	if (err) {
		b43err(dev->wl, "PHY init failed\n");
		goto err_block_rf;
	}
	phy->do_full_init = false;

	err = b43_switch_channel(dev, phy->channel);
	if (err) {
		b43err(dev->wl, "PHY init: Channel switch to default failed\n");
		goto err_phy_exit;
	}

	return 0;

err_phy_exit:
	phy->do_full_init = true;
	if (ops->exit)
		ops->exit(dev);
err_block_rf:
	b43_software_rfkill(dev, true);

	return err;
}

void b43_phy_exit(struct b43_wldev *dev)
{
	const struct b43_phy_operations *ops = dev->phy.ops;

	b43_software_rfkill(dev, true);
	dev->phy.do_full_init = true;
	if (ops->exit)
		ops->exit(dev);
}

bool b43_has_hardware_pctl(struct b43_wldev *dev)
{
	if (!dev->phy.hardware_power_control)
		return false;
	if (!dev->phy.ops->supports_hwpctl)
		return false;
	return dev->phy.ops->supports_hwpctl(dev);
}

void b43_radio_lock(struct b43_wldev *dev)
{
	u32 macctl;

#if B43_DEBUG
	B43_WARN_ON(dev->phy.radio_locked);
	dev->phy.radio_locked = true;
#endif

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl |= B43_MACCTL_RADIOLOCK;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Commit the write and wait for the firmware
	 * to finish any radio register access. */
	b43_read32(dev, B43_MMIO_MACCTL);
	udelay(10);
}

void b43_radio_unlock(struct b43_wldev *dev)
{
	u32 macctl;

#if B43_DEBUG
	B43_WARN_ON(!dev->phy.radio_locked);
	dev->phy.radio_locked = false;
#endif

	/* Commit any write */
	b43_read16(dev, B43_MMIO_PHY_VER);
	/* unlock */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_RADIOLOCK;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
}

void b43_phy_lock(struct b43_wldev *dev)
{
#if B43_DEBUG
	B43_WARN_ON(dev->phy.phy_locked);
	dev->phy.phy_locked = true;
#endif
	B43_WARN_ON(dev->dev->core_rev < 3);

	if (!b43_is_mode(dev->wl, NL80211_IFTYPE_AP))
		b43_power_saving_ctl_bits(dev, B43_PS_AWAKE);
}

void b43_phy_unlock(struct b43_wldev *dev)
{
#if B43_DEBUG
	B43_WARN_ON(!dev->phy.phy_locked);
	dev->phy.phy_locked = false;
#endif
	B43_WARN_ON(dev->dev->core_rev < 3);

	if (!b43_is_mode(dev->wl, NL80211_IFTYPE_AP))
		b43_power_saving_ctl_bits(dev, 0);
}

static inline void assert_mac_suspended(struct b43_wldev *dev)
{
	if (!B43_DEBUG)
		return;
	if ((b43_status(dev) >= B43_STAT_INITIALIZED) &&
	    (dev->mac_suspended <= 0)) {
		b43dbg(dev->wl, "PHY/RADIO register access with "
		       "enabled MAC.\n");
		dump_stack();
	}
}

u16 b43_radio_read(struct b43_wldev *dev, u16 reg)
{
	assert_mac_suspended(dev);
	dev->phy.writes_counter = 0;
	return dev->phy.ops->radio_read(dev, reg);
}

void b43_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	assert_mac_suspended(dev);
	if (b43_bus_host_is_pci(dev->dev) &&
	    ++dev->phy.writes_counter > B43_MAX_WRITES_IN_ROW) {
		b43_read32(dev, B43_MMIO_MACCTL);
		dev->phy.writes_counter = 1;
	}
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

bool b43_radio_wait_value(struct b43_wldev *dev, u16 offset, u16 mask,
			  u16 value, int delay, int timeout)
{
	u16 val;
	int i;

	for (i = 0; i < timeout; i += delay) {
		val = b43_radio_read(dev, offset);
		if ((val & mask) == value)
			return true;
		udelay(delay);
	}
	return false;
}

u16 b43_phy_read(struct b43_wldev *dev, u16 reg)
{
	assert_mac_suspended(dev);
	dev->phy.writes_counter = 0;

	if (dev->phy.ops->phy_read)
		return dev->phy.ops->phy_read(dev, reg);

	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

void b43_phy_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	assert_mac_suspended(dev);
	if (b43_bus_host_is_pci(dev->dev) &&
	    ++dev->phy.writes_counter > B43_MAX_WRITES_IN_ROW) {
		b43_read16(dev, B43_MMIO_PHY_VER);
		dev->phy.writes_counter = 1;
	}

	if (dev->phy.ops->phy_write)
		return dev->phy.ops->phy_write(dev, reg, value);

	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

void b43_phy_copy(struct b43_wldev *dev, u16 destreg, u16 srcreg)
{
	b43_phy_write(dev, destreg, b43_phy_read(dev, srcreg));
}

void b43_phy_mask(struct b43_wldev *dev, u16 offset, u16 mask)
{
	if (dev->phy.ops->phy_maskset) {
		assert_mac_suspended(dev);
		dev->phy.ops->phy_maskset(dev, offset, mask, 0);
	} else {
		b43_phy_write(dev, offset,
			      b43_phy_read(dev, offset) & mask);
	}
}

void b43_phy_set(struct b43_wldev *dev, u16 offset, u16 set)
{
	if (dev->phy.ops->phy_maskset) {
		assert_mac_suspended(dev);
		dev->phy.ops->phy_maskset(dev, offset, 0xFFFF, set);
	} else {
		b43_phy_write(dev, offset,
			      b43_phy_read(dev, offset) | set);
	}
}

void b43_phy_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set)
{
	if (dev->phy.ops->phy_maskset) {
		assert_mac_suspended(dev);
		dev->phy.ops->phy_maskset(dev, offset, mask, set);
	} else {
		b43_phy_write(dev, offset,
			      (b43_phy_read(dev, offset) & mask) | set);
	}
}

void b43_phy_put_into_reset(struct b43_wldev *dev)
{
	u32 tmp;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		tmp &= ~B43_BCMA_IOCTL_GMODE;
		tmp |= B43_BCMA_IOCTL_PHY_RESET;
		tmp |= BCMA_IOCTL_FGC;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		udelay(1);

		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		tmp &= ~BCMA_IOCTL_FGC;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		udelay(1);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~B43_TMSLOW_GMODE;
		tmp |= B43_TMSLOW_PHYRESET;
		tmp |= SSB_TMSLOW_FGC;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		usleep_range(1000, 2000);

		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~SSB_TMSLOW_FGC;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		usleep_range(1000, 2000);

		break;
#endif
	}
}

void b43_phy_take_out_of_reset(struct b43_wldev *dev)
{
	u32 tmp;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		/* Unset reset bit (with forcing clock) */
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		tmp &= ~B43_BCMA_IOCTL_PHY_RESET;
		tmp &= ~B43_BCMA_IOCTL_PHY_CLKEN;
		tmp |= BCMA_IOCTL_FGC;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		udelay(1);

		/* Do not force clock anymore */
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		tmp &= ~BCMA_IOCTL_FGC;
		tmp |= B43_BCMA_IOCTL_PHY_CLKEN;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		udelay(1);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		/* Unset reset bit (with forcing clock) */
		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~B43_TMSLOW_PHYRESET;
		tmp &= ~B43_TMSLOW_PHYCLKEN;
		tmp |= SSB_TMSLOW_FGC;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		ssb_read32(dev->dev->sdev, SSB_TMSLOW); /* flush */
		usleep_range(1000, 2000);

		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~SSB_TMSLOW_FGC;
		tmp |= B43_TMSLOW_PHYCLKEN;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		ssb_read32(dev->dev->sdev, SSB_TMSLOW); /* flush */
		usleep_range(1000, 2000);
		break;
#endif
	}
}

int b43_switch_channel(struct b43_wldev *dev, unsigned int new_channel)
{
	struct b43_phy *phy = &(dev->phy);
	u16 channelcookie, savedcookie;
	int err;

	/* First we set the channel radio code to prevent the
	 * firmware from sending ghost packets.
	 */
	channelcookie = new_channel;
	if (b43_current_band(dev->wl) == NL80211_BAND_5GHZ)
		channelcookie |= B43_SHM_SH_CHAN_5GHZ;
	/* FIXME: set 40Mhz flag if required */
	if (0)
		channelcookie |= B43_SHM_SH_CHAN_40MHZ;
	savedcookie = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_CHAN);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_CHAN, channelcookie);

	/* Now try to switch the PHY hardware channel. */
	err = phy->ops->switch_channel(dev, new_channel);
	if (err)
		goto err_restore_cookie;

	/* Wait for the radio to tune to the channel and stabilize. */
	msleep(8);

	return 0;

err_restore_cookie:
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_CHAN, savedcookie);

	return err;
}

void b43_software_rfkill(struct b43_wldev *dev, bool blocked)
{
	struct b43_phy *phy = &dev->phy;

	b43_mac_suspend(dev);
	phy->ops->software_rfkill(dev, blocked);
	phy->radio_on = !blocked;
	b43_mac_enable(dev);
}

/**
 * b43_phy_txpower_adjust_work - TX power workqueue.
 *
 * Workqueue for updating the TX power parameters in hardware.
 */
void b43_phy_txpower_adjust_work(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work, struct b43_wl,
					 txpower_adjust_work);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;

	if (likely(dev && (b43_status(dev) >= B43_STAT_STARTED)))
		dev->phy.ops->adjust_txpower(dev);

	mutex_unlock(&wl->mutex);
}

void b43_phy_txpower_check(struct b43_wldev *dev, unsigned int flags)
{
	struct b43_phy *phy = &dev->phy;
	unsigned long now = jiffies;
	enum b43_txpwr_result result;

	if (!(flags & B43_TXPWR_IGNORE_TIME)) {
		/* Check if it's time for a TXpower check. */
		if (time_before(now, phy->next_txpwr_check_time))
			return; /* Not yet */
	}
	/* The next check will be needed in two seconds, or later. */
	phy->next_txpwr_check_time = round_jiffies(now + (HZ * 2));

	if ((dev->dev->board_vendor == SSB_BOARDVENDOR_BCM) &&
	    (dev->dev->board_type == SSB_BOARD_BU4306))
		return; /* No software txpower adjustment needed */

	result = phy->ops->recalc_txpower(dev, !!(flags & B43_TXPWR_IGNORE_TSSI));
	if (result == B43_TXPWR_RES_DONE)
		return; /* We are done. */
	B43_WARN_ON(result != B43_TXPWR_RES_NEED_ADJUST);
	B43_WARN_ON(phy->ops->adjust_txpower == NULL);

	/* We must adjust the transmission power in hardware.
	 * Schedule b43_phy_txpower_adjust_work(). */
	ieee80211_queue_work(dev->wl->hw, &dev->wl->txpower_adjust_work);
}

int b43_phy_shm_tssi_read(struct b43_wldev *dev, u16 shm_offset)
{
	const bool is_ofdm = (shm_offset != B43_SHM_SH_TSSI_CCK);
	unsigned int a, b, c, d;
	unsigned int average;
	u32 tmp;

	tmp = b43_shm_read32(dev, B43_SHM_SHARED, shm_offset);
	a = tmp & 0xFF;
	b = (tmp >> 8) & 0xFF;
	c = (tmp >> 16) & 0xFF;
	d = (tmp >> 24) & 0xFF;
	if (a == 0 || a == B43_TSSI_MAX ||
	    b == 0 || b == B43_TSSI_MAX ||
	    c == 0 || c == B43_TSSI_MAX ||
	    d == 0 || d == B43_TSSI_MAX)
		return -ENOENT;
	/* The values are OK. Clear them. */
	tmp = B43_TSSI_MAX | (B43_TSSI_MAX << 8) |
	      (B43_TSSI_MAX << 16) | (B43_TSSI_MAX << 24);
	b43_shm_write32(dev, B43_SHM_SHARED, shm_offset, tmp);

	if (is_ofdm) {
		a = (a + 32) & 0x3F;
		b = (b + 32) & 0x3F;
		c = (c + 32) & 0x3F;
		d = (d + 32) & 0x3F;
	}

	/* Get the average of the values with 0.5 added to each value. */
	average = (a + b + c + d + 2) / 4;
	if (is_ofdm) {
		/* Adjust for CCK-boost */
		if (b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF1)
		    & B43_HF_CCKBOOST)
			average = (average >= 13) ? (average - 13) : 0;
	}

	return average;
}

void b43_phyop_switch_analog_generic(struct b43_wldev *dev, bool on)
{
	b43_write16(dev, B43_MMIO_PHY0, on ? 0 : 0xF4);
}


bool b43_is_40mhz(struct b43_wldev *dev)
{
	return dev->phy.chandef->width == NL80211_CHAN_WIDTH_40;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/BmacPhyClkFgc */
void b43_phy_force_clock(struct b43_wldev *dev, bool force)
{
	u32 tmp;

	WARN_ON(dev->phy.type != B43_PHYTYPE_N &&
		dev->phy.type != B43_PHYTYPE_HT &&
		dev->phy.type != B43_PHYTYPE_AC);

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		if (force)
			tmp |= BCMA_IOCTL_FGC;
		else
			tmp &= ~BCMA_IOCTL_FGC;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		if (force)
			tmp |= SSB_TMSLOW_FGC;
		else
			tmp &= ~SSB_TMSLOW_FGC;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		break;
#endif
	}
}
