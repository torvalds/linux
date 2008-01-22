/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005, 2006 Michael Buesch <mb@bu3sch.de>
  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>

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
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/version.h>
#include <linux/firmware.h>
#include <linux/wireless.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <asm/unaligned.h>

#include "b43.h"
#include "main.h"
#include "debugfs.h"
#include "phy.h"
#include "dma.h"
#include "sysfs.h"
#include "xmit.h"
#include "lo.h"
#include "pcmcia.h"

MODULE_DESCRIPTION("Broadcom B43 wireless driver");
MODULE_AUTHOR("Martin Langer");
MODULE_AUTHOR("Stefano Brivio");
MODULE_AUTHOR("Michael Buesch");
MODULE_LICENSE("GPL");


static int modparam_bad_frames_preempt;
module_param_named(bad_frames_preempt, modparam_bad_frames_preempt, int, 0444);
MODULE_PARM_DESC(bad_frames_preempt,
		 "enable(1) / disable(0) Bad Frames Preemption");

static char modparam_fwpostfix[16];
module_param_string(fwpostfix, modparam_fwpostfix, 16, 0444);
MODULE_PARM_DESC(fwpostfix, "Postfix for the .fw files to load.");

static int modparam_hwpctl;
module_param_named(hwpctl, modparam_hwpctl, int, 0444);
MODULE_PARM_DESC(hwpctl, "Enable hardware-side power control (default off)");

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, int, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static const struct ssb_device_id b43_ssb_tbl[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 5),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 6),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 7),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 9),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 10),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 11),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 13),
	SSB_DEVTABLE_END
};

MODULE_DEVICE_TABLE(ssb, b43_ssb_tbl);

/* Channel and ratetables are shared for all devices.
 * They can't be const, because ieee80211 puts some precalculated
 * data in there. This data is the same for all devices, so we don't
 * get concurrency issues */
#define RATETAB_ENT(_rateid, _flags) \
	{							\
		.rate	= B43_RATE_TO_BASE100KBPS(_rateid),	\
		.val	= (_rateid),				\
		.val2	= (_rateid),				\
		.flags	= (_flags),				\
	}
static struct ieee80211_rate __b43_ratetable[] = {
	RATETAB_ENT(B43_CCK_RATE_1MB, IEEE80211_RATE_CCK),
	RATETAB_ENT(B43_CCK_RATE_2MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43_CCK_RATE_5MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43_CCK_RATE_11MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43_OFDM_RATE_6MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_9MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_12MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_18MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_24MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_36MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_48MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43_OFDM_RATE_54MB, IEEE80211_RATE_OFDM),
};

#define b43_a_ratetable		(__b43_ratetable + 4)
#define b43_a_ratetable_size	8
#define b43_b_ratetable		(__b43_ratetable + 0)
#define b43_b_ratetable_size	4
#define b43_g_ratetable		(__b43_ratetable + 0)
#define b43_g_ratetable_size	12

#define CHANTAB_ENT(_chanid, _freq) \
	{							\
		.chan	= (_chanid),				\
		.freq	= (_freq),				\
		.val	= (_chanid),				\
		.flag	= IEEE80211_CHAN_W_SCAN |		\
			  IEEE80211_CHAN_W_ACTIVE_SCAN |	\
			  IEEE80211_CHAN_W_IBSS,		\
		.power_level	= 0xFF,				\
		.antenna_max	= 0xFF,				\
	}
static struct ieee80211_channel b43_2ghz_chantable[] = {
	CHANTAB_ENT(1, 2412),
	CHANTAB_ENT(2, 2417),
	CHANTAB_ENT(3, 2422),
	CHANTAB_ENT(4, 2427),
	CHANTAB_ENT(5, 2432),
	CHANTAB_ENT(6, 2437),
	CHANTAB_ENT(7, 2442),
	CHANTAB_ENT(8, 2447),
	CHANTAB_ENT(9, 2452),
	CHANTAB_ENT(10, 2457),
	CHANTAB_ENT(11, 2462),
	CHANTAB_ENT(12, 2467),
	CHANTAB_ENT(13, 2472),
	CHANTAB_ENT(14, 2484),
};
#define b43_2ghz_chantable_size	ARRAY_SIZE(b43_2ghz_chantable)

#if 0
static struct ieee80211_channel b43_5ghz_chantable[] = {
	CHANTAB_ENT(36, 5180),
	CHANTAB_ENT(40, 5200),
	CHANTAB_ENT(44, 5220),
	CHANTAB_ENT(48, 5240),
	CHANTAB_ENT(52, 5260),
	CHANTAB_ENT(56, 5280),
	CHANTAB_ENT(60, 5300),
	CHANTAB_ENT(64, 5320),
	CHANTAB_ENT(149, 5745),
	CHANTAB_ENT(153, 5765),
	CHANTAB_ENT(157, 5785),
	CHANTAB_ENT(161, 5805),
	CHANTAB_ENT(165, 5825),
};
#define b43_5ghz_chantable_size	ARRAY_SIZE(b43_5ghz_chantable)
#endif

static void b43_wireless_core_exit(struct b43_wldev *dev);
static int b43_wireless_core_init(struct b43_wldev *dev);
static void b43_wireless_core_stop(struct b43_wldev *dev);
static int b43_wireless_core_start(struct b43_wldev *dev);

static int b43_ratelimit(struct b43_wl *wl)
{
	if (!wl || !wl->current_dev)
		return 1;
	if (b43_status(wl->current_dev) < B43_STAT_STARTED)
		return 1;
	/* We are up and running.
	 * Ratelimit the messages to avoid DoS over the net. */
	return net_ratelimit();
}

void b43info(struct b43_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_INFO "b43-%s: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

void b43err(struct b43_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_ERR "b43-%s ERROR: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

void b43warn(struct b43_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_WARNING "b43-%s warning: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

#if B43_DEBUG
void b43dbg(struct b43_wl *wl, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_DEBUG "b43-%s debug: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}
#endif /* DEBUG */

static void b43_ram_write(struct b43_wldev *dev, u16 offset, u32 val)
{
	u32 macctl;

	B43_WARN_ON(offset % 4 != 0);

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	if (macctl & B43_MACCTL_BE)
		val = swab32(val);

	b43_write32(dev, B43_MMIO_RAM_CONTROL, offset);
	mmiowb();
	b43_write32(dev, B43_MMIO_RAM_DATA, val);
}

static inline void b43_shm_control_word(struct b43_wldev *dev,
					u16 routing, u16 offset)
{
	u32 control;

	/* "offset" is the WORD offset. */
	control = routing;
	control <<= 16;
	control |= offset;
	b43_write32(dev, B43_MMIO_SHM_CONTROL, control);
}

u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	struct b43_wl *wl = dev->wl;
	unsigned long flags;
	u32 ret;

	spin_lock_irqsave(&wl->shm_lock, flags);
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			ret = b43_read16(dev, B43_MMIO_SHM_DATA_UNALIGNED);
			ret <<= 16;
			b43_shm_control_word(dev, routing, (offset >> 2) + 1);
			ret |= b43_read16(dev, B43_MMIO_SHM_DATA);

			goto out;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	ret = b43_read32(dev, B43_MMIO_SHM_DATA);
out:
	spin_unlock_irqrestore(&wl->shm_lock, flags);

	return ret;
}

u16 b43_shm_read16(struct b43_wldev * dev, u16 routing, u16 offset)
{
	struct b43_wl *wl = dev->wl;
	unsigned long flags;
	u16 ret;

	spin_lock_irqsave(&wl->shm_lock, flags);
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			ret = b43_read16(dev, B43_MMIO_SHM_DATA_UNALIGNED);

			goto out;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	ret = b43_read16(dev, B43_MMIO_SHM_DATA);
out:
	spin_unlock_irqrestore(&wl->shm_lock, flags);

	return ret;
}

void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	struct b43_wl *wl = dev->wl;
	unsigned long flags;

	spin_lock_irqsave(&wl->shm_lock, flags);
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			b43_write16(dev, B43_MMIO_SHM_DATA_UNALIGNED,
				    (value >> 16) & 0xffff);
			b43_shm_control_word(dev, routing, (offset >> 2) + 1);
			b43_write16(dev, B43_MMIO_SHM_DATA, value & 0xffff);
			goto out;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	b43_write32(dev, B43_MMIO_SHM_DATA, value);
out:
	spin_unlock_irqrestore(&wl->shm_lock, flags);
}

void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	struct b43_wl *wl = dev->wl;
	unsigned long flags;

	spin_lock_irqsave(&wl->shm_lock, flags);
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			b43_write16(dev, B43_MMIO_SHM_DATA_UNALIGNED, value);
			goto out;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	b43_write16(dev, B43_MMIO_SHM_DATA, value);
out:
	spin_unlock_irqrestore(&wl->shm_lock, flags);
}

/* Read HostFlags */
u32 b43_hf_read(struct b43_wldev * dev)
{
	u32 ret;

	ret = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTFHI);
	ret <<= 16;
	ret |= b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTFLO);

	return ret;
}

/* Write HostFlags */
void b43_hf_write(struct b43_wldev *dev, u32 value)
{
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_HOSTFLO, (value & 0x0000FFFF));
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_HOSTFHI, ((value & 0xFFFF0000) >> 16));
}

void b43_tsf_read(struct b43_wldev *dev, u64 * tsf)
{
	/* We need to be careful. As we read the TSF from multiple
	 * registers, we should take care of register overflows.
	 * In theory, the whole tsf read process should be atomic.
	 * We try to be atomic here, by restaring the read process,
	 * if any of the high registers changed (overflew).
	 */
	if (dev->dev->id.revision >= 3) {
		u32 low, high, high2;

		do {
			high = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_HIGH);
			low = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_LOW);
			high2 = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_HIGH);
		} while (unlikely(high != high2));

		*tsf = high;
		*tsf <<= 32;
		*tsf |= low;
	} else {
		u64 tmp;
		u16 v0, v1, v2, v3;
		u16 test1, test2, test3;

		do {
			v3 = b43_read16(dev, B43_MMIO_TSF_3);
			v2 = b43_read16(dev, B43_MMIO_TSF_2);
			v1 = b43_read16(dev, B43_MMIO_TSF_1);
			v0 = b43_read16(dev, B43_MMIO_TSF_0);

			test3 = b43_read16(dev, B43_MMIO_TSF_3);
			test2 = b43_read16(dev, B43_MMIO_TSF_2);
			test1 = b43_read16(dev, B43_MMIO_TSF_1);
		} while (v3 != test3 || v2 != test2 || v1 != test1);

		*tsf = v3;
		*tsf <<= 48;
		tmp = v2;
		tmp <<= 32;
		*tsf |= tmp;
		tmp = v1;
		tmp <<= 16;
		*tsf |= tmp;
		*tsf |= v0;
	}
}

static void b43_time_lock(struct b43_wldev *dev)
{
	u32 macctl;

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl |= B43_MACCTL_TBTTHOLD;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Commit the write */
	b43_read32(dev, B43_MMIO_MACCTL);
}

static void b43_time_unlock(struct b43_wldev *dev)
{
	u32 macctl;

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_TBTTHOLD;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Commit the write */
	b43_read32(dev, B43_MMIO_MACCTL);
}

static void b43_tsf_write_locked(struct b43_wldev *dev, u64 tsf)
{
	/* Be careful with the in-progress timer.
	 * First zero out the low register, so we have a full
	 * register-overflow duration to complete the operation.
	 */
	if (dev->dev->id.revision >= 3) {
		u32 lo = (tsf & 0x00000000FFFFFFFFULL);
		u32 hi = (tsf & 0xFFFFFFFF00000000ULL) >> 32;

		b43_write32(dev, B43_MMIO_REV3PLUS_TSF_LOW, 0);
		mmiowb();
		b43_write32(dev, B43_MMIO_REV3PLUS_TSF_HIGH, hi);
		mmiowb();
		b43_write32(dev, B43_MMIO_REV3PLUS_TSF_LOW, lo);
	} else {
		u16 v0 = (tsf & 0x000000000000FFFFULL);
		u16 v1 = (tsf & 0x00000000FFFF0000ULL) >> 16;
		u16 v2 = (tsf & 0x0000FFFF00000000ULL) >> 32;
		u16 v3 = (tsf & 0xFFFF000000000000ULL) >> 48;

		b43_write16(dev, B43_MMIO_TSF_0, 0);
		mmiowb();
		b43_write16(dev, B43_MMIO_TSF_3, v3);
		mmiowb();
		b43_write16(dev, B43_MMIO_TSF_2, v2);
		mmiowb();
		b43_write16(dev, B43_MMIO_TSF_1, v1);
		mmiowb();
		b43_write16(dev, B43_MMIO_TSF_0, v0);
	}
}

void b43_tsf_write(struct b43_wldev *dev, u64 tsf)
{
	b43_time_lock(dev);
	b43_tsf_write_locked(dev, tsf);
	b43_time_unlock(dev);
}

static
void b43_macfilter_set(struct b43_wldev *dev, u16 offset, const u8 * mac)
{
	static const u8 zero_addr[ETH_ALEN] = { 0 };
	u16 data;

	if (!mac)
		mac = zero_addr;

	offset |= 0x0020;
	b43_write16(dev, B43_MMIO_MACFILTER_CONTROL, offset);

	data = mac[0];
	data |= mac[1] << 8;
	b43_write16(dev, B43_MMIO_MACFILTER_DATA, data);
	data = mac[2];
	data |= mac[3] << 8;
	b43_write16(dev, B43_MMIO_MACFILTER_DATA, data);
	data = mac[4];
	data |= mac[5] << 8;
	b43_write16(dev, B43_MMIO_MACFILTER_DATA, data);
}

static void b43_write_mac_bssid_templates(struct b43_wldev *dev)
{
	const u8 *mac;
	const u8 *bssid;
	u8 mac_bssid[ETH_ALEN * 2];
	int i;
	u32 tmp;

	bssid = dev->wl->bssid;
	mac = dev->wl->mac_addr;

	b43_macfilter_set(dev, B43_MACFILTER_BSSID, bssid);

	memcpy(mac_bssid, mac, ETH_ALEN);
	memcpy(mac_bssid + ETH_ALEN, bssid, ETH_ALEN);

	/* Write our MAC address and BSSID to template ram */
	for (i = 0; i < ARRAY_SIZE(mac_bssid); i += sizeof(u32)) {
		tmp = (u32) (mac_bssid[i + 0]);
		tmp |= (u32) (mac_bssid[i + 1]) << 8;
		tmp |= (u32) (mac_bssid[i + 2]) << 16;
		tmp |= (u32) (mac_bssid[i + 3]) << 24;
		b43_ram_write(dev, 0x20 + i, tmp);
	}
}

static void b43_upload_card_macaddress(struct b43_wldev *dev)
{
	b43_write_mac_bssid_templates(dev);
	b43_macfilter_set(dev, B43_MACFILTER_SELF, dev->wl->mac_addr);
}

static void b43_set_slot_time(struct b43_wldev *dev, u16 slot_time)
{
	/* slot_time is in usec. */
	if (dev->phy.type != B43_PHYTYPE_G)
		return;
	b43_write16(dev, 0x684, 510 + slot_time);
	b43_shm_write16(dev, B43_SHM_SHARED, 0x0010, slot_time);
}

static void b43_short_slot_timing_enable(struct b43_wldev *dev)
{
	b43_set_slot_time(dev, 9);
	dev->short_slot = 1;
}

static void b43_short_slot_timing_disable(struct b43_wldev *dev)
{
	b43_set_slot_time(dev, 20);
	dev->short_slot = 0;
}

/* Enable a Generic IRQ. "mask" is the mask of which IRQs to enable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 b43_interrupt_enable(struct b43_wldev *dev, u32 mask)
{
	u32 old_mask;

	old_mask = b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);
	b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, old_mask | mask);

	return old_mask;
}

/* Disable a Generic IRQ. "mask" is the mask of which IRQs to disable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 b43_interrupt_disable(struct b43_wldev *dev, u32 mask)
{
	u32 old_mask;

	old_mask = b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);
	b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, old_mask & ~mask);

	return old_mask;
}

/* Synchronize IRQ top- and bottom-half.
 * IRQs must be masked before calling this.
 * This must not be called with the irq_lock held.
 */
static void b43_synchronize_irq(struct b43_wldev *dev)
{
	synchronize_irq(dev->dev->irq);
	tasklet_kill(&dev->isr_tasklet);
}

/* DummyTransmission function, as documented on
 * http://bcm-specs.sipsolutions.net/DummyTransmission
 */
void b43_dummy_transmission(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	unsigned int i, max_loop;
	u16 value;
	u32 buffer[5] = {
		0x00000000,
		0x00D40000,
		0x00000000,
		0x01000000,
		0x00000000,
	};

	switch (phy->type) {
	case B43_PHYTYPE_A:
		max_loop = 0x1E;
		buffer[0] = 0x000201CC;
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
		max_loop = 0xFA;
		buffer[0] = 0x000B846E;
		break;
	default:
		B43_WARN_ON(1);
		return;
	}

	for (i = 0; i < 5; i++)
		b43_ram_write(dev, i * 4, buffer[i]);

	/* Commit writes */
	b43_read32(dev, B43_MMIO_MACCTL);

	b43_write16(dev, 0x0568, 0x0000);
	b43_write16(dev, 0x07C0, 0x0000);
	value = ((phy->type == B43_PHYTYPE_A) ? 1 : 0);
	b43_write16(dev, 0x050C, value);
	b43_write16(dev, 0x0508, 0x0000);
	b43_write16(dev, 0x050A, 0x0000);
	b43_write16(dev, 0x054C, 0x0000);
	b43_write16(dev, 0x056A, 0x0014);
	b43_write16(dev, 0x0568, 0x0826);
	b43_write16(dev, 0x0500, 0x0000);
	b43_write16(dev, 0x0502, 0x0030);

	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43_radio_write16(dev, 0x0051, 0x0017);
	for (i = 0x00; i < max_loop; i++) {
		value = b43_read16(dev, 0x050E);
		if (value & 0x0080)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = b43_read16(dev, 0x050E);
		if (value & 0x0400)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = b43_read16(dev, 0x0690);
		if (!(value & 0x0100))
			break;
		udelay(10);
	}
	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43_radio_write16(dev, 0x0051, 0x0037);
}

static void key_write(struct b43_wldev *dev,
		      u8 index, u8 algorithm, const u8 * key)
{
	unsigned int i;
	u32 offset;
	u16 value;
	u16 kidx;

	/* Key index/algo block */
	kidx = b43_kidx_to_fw(dev, index);
	value = ((kidx << 4) | algorithm);
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_KEYIDXBLOCK + (kidx * 2), value);

	/* Write the key to the Key Table Pointer offset */
	offset = dev->ktp + (index * B43_SEC_KEYSIZE);
	for (i = 0; i < B43_SEC_KEYSIZE; i += 2) {
		value = key[i];
		value |= (u16) (key[i + 1]) << 8;
		b43_shm_write16(dev, B43_SHM_SHARED, offset + i, value);
	}
}

static void keymac_write(struct b43_wldev *dev, u8 index, const u8 * addr)
{
	u32 addrtmp[2] = { 0, 0, };
	u8 per_sta_keys_start = 8;

	if (b43_new_kidx_api(dev))
		per_sta_keys_start = 4;

	B43_WARN_ON(index < per_sta_keys_start);
	/* We have two default TX keys and possibly two default RX keys.
	 * Physical mac 0 is mapped to physical key 4 or 8, depending
	 * on the firmware version.
	 * So we must adjust the index here.
	 */
	index -= per_sta_keys_start;

	if (addr) {
		addrtmp[0] = addr[0];
		addrtmp[0] |= ((u32) (addr[1]) << 8);
		addrtmp[0] |= ((u32) (addr[2]) << 16);
		addrtmp[0] |= ((u32) (addr[3]) << 24);
		addrtmp[1] = addr[4];
		addrtmp[1] |= ((u32) (addr[5]) << 8);
	}

	if (dev->dev->id.revision >= 5) {
		/* Receive match transmitter address mechanism */
		b43_shm_write32(dev, B43_SHM_RCMTA,
				(index * 2) + 0, addrtmp[0]);
		b43_shm_write16(dev, B43_SHM_RCMTA,
				(index * 2) + 1, addrtmp[1]);
	} else {
		/* RXE (Receive Engine) and
		 * PSM (Programmable State Machine) mechanism
		 */
		if (index < 8) {
			/* TODO write to RCM 16, 19, 22 and 25 */
		} else {
			b43_shm_write32(dev, B43_SHM_SHARED,
					B43_SHM_SH_PSM + (index * 6) + 0,
					addrtmp[0]);
			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_SHM_SH_PSM + (index * 6) + 4,
					addrtmp[1]);
		}
	}
}

static void do_key_write(struct b43_wldev *dev,
			 u8 index, u8 algorithm,
			 const u8 * key, size_t key_len, const u8 * mac_addr)
{
	u8 buf[B43_SEC_KEYSIZE] = { 0, };
	u8 per_sta_keys_start = 8;

	if (b43_new_kidx_api(dev))
		per_sta_keys_start = 4;

	B43_WARN_ON(index >= dev->max_nr_keys);
	B43_WARN_ON(key_len > B43_SEC_KEYSIZE);

	if (index >= per_sta_keys_start)
		keymac_write(dev, index, NULL);	/* First zero out mac. */
	if (key)
		memcpy(buf, key, key_len);
	key_write(dev, index, algorithm, buf);
	if (index >= per_sta_keys_start)
		keymac_write(dev, index, mac_addr);

	dev->key[index].algorithm = algorithm;
}

static int b43_key_write(struct b43_wldev *dev,
			 int index, u8 algorithm,
			 const u8 * key, size_t key_len,
			 const u8 * mac_addr,
			 struct ieee80211_key_conf *keyconf)
{
	int i;
	int sta_keys_start;

	if (key_len > B43_SEC_KEYSIZE)
		return -EINVAL;
	for (i = 0; i < dev->max_nr_keys; i++) {
		/* Check that we don't already have this key. */
		B43_WARN_ON(dev->key[i].keyconf == keyconf);
	}
	if (index < 0) {
		/* Either pairwise key or address is 00:00:00:00:00:00
		 * for transmit-only keys. Search the index. */
		if (b43_new_kidx_api(dev))
			sta_keys_start = 4;
		else
			sta_keys_start = 8;
		for (i = sta_keys_start; i < dev->max_nr_keys; i++) {
			if (!dev->key[i].keyconf) {
				/* found empty */
				index = i;
				break;
			}
		}
		if (index < 0) {
			b43err(dev->wl, "Out of hardware key memory\n");
			return -ENOSPC;
		}
	} else
		B43_WARN_ON(index > 3);

	do_key_write(dev, index, algorithm, key, key_len, mac_addr);
	if ((index <= 3) && !b43_new_kidx_api(dev)) {
		/* Default RX key */
		B43_WARN_ON(mac_addr);
		do_key_write(dev, index + 4, algorithm, key, key_len, NULL);
	}
	keyconf->hw_key_idx = index;
	dev->key[index].keyconf = keyconf;

	return 0;
}

static int b43_key_clear(struct b43_wldev *dev, int index)
{
	if (B43_WARN_ON((index < 0) || (index >= dev->max_nr_keys)))
		return -EINVAL;
	do_key_write(dev, index, B43_SEC_ALGO_NONE,
		     NULL, B43_SEC_KEYSIZE, NULL);
	if ((index <= 3) && !b43_new_kidx_api(dev)) {
		do_key_write(dev, index + 4, B43_SEC_ALGO_NONE,
			     NULL, B43_SEC_KEYSIZE, NULL);
	}
	dev->key[index].keyconf = NULL;

	return 0;
}

static void b43_clear_keys(struct b43_wldev *dev)
{
	int i;

	for (i = 0; i < dev->max_nr_keys; i++)
		b43_key_clear(dev, i);
}

void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	u32 macctl;
	u16 ucstat;
	bool hwps;
	bool awake;
	int i;

	B43_WARN_ON((ps_flags & B43_PS_ENABLED) &&
		    (ps_flags & B43_PS_DISABLED));
	B43_WARN_ON((ps_flags & B43_PS_AWAKE) && (ps_flags & B43_PS_ASLEEP));

	if (ps_flags & B43_PS_ENABLED) {
		hwps = 1;
	} else if (ps_flags & B43_PS_DISABLED) {
		hwps = 0;
	} else {
		//TODO: If powersave is not off and FIXME is not set and we are not in adhoc
		//      and thus is not an AP and we are associated, set bit 25
	}
	if (ps_flags & B43_PS_AWAKE) {
		awake = 1;
	} else if (ps_flags & B43_PS_ASLEEP) {
		awake = 0;
	} else {
		//TODO: If the device is awake or this is an AP, or we are scanning, or FIXME,
		//      or we are associated, or FIXME, or the latest PS-Poll packet sent was
		//      successful, set bit26
	}

/* FIXME: For now we force awake-on and hwps-off */
	hwps = 0;
	awake = 1;

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	if (hwps)
		macctl |= B43_MACCTL_HWPS;
	else
		macctl &= ~B43_MACCTL_HWPS;
	if (awake)
		macctl |= B43_MACCTL_AWAKE;
	else
		macctl &= ~B43_MACCTL_AWAKE;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Commit write */
	b43_read32(dev, B43_MMIO_MACCTL);
	if (awake && dev->dev->id.revision >= 5) {
		/* Wait for the microcode to wake up. */
		for (i = 0; i < 100; i++) {
			ucstat = b43_shm_read16(dev, B43_SHM_SHARED,
						B43_SHM_SH_UCODESTAT);
			if (ucstat != B43_SHM_SH_UCODESTAT_SLEEP)
				break;
			udelay(10);
		}
	}
}

/* Turn the Analog ON/OFF */
static void b43_switch_analog(struct b43_wldev *dev, int on)
{
	b43_write16(dev, B43_MMIO_PHY0, on ? 0 : 0xF4);
}

void b43_wireless_core_reset(struct b43_wldev *dev, u32 flags)
{
	u32 tmslow;
	u32 macctl;

	flags |= B43_TMSLOW_PHYCLKEN;
	flags |= B43_TMSLOW_PHYRESET;
	ssb_device_enable(dev->dev, flags);
	msleep(2);		/* Wait for the PLL to turn on. */

	/* Now take the PHY out of Reset again */
	tmslow = ssb_read32(dev->dev, SSB_TMSLOW);
	tmslow |= SSB_TMSLOW_FGC;
	tmslow &= ~B43_TMSLOW_PHYRESET;
	ssb_write32(dev->dev, SSB_TMSLOW, tmslow);
	ssb_read32(dev->dev, SSB_TMSLOW);	/* flush */
	msleep(1);
	tmslow &= ~SSB_TMSLOW_FGC;
	ssb_write32(dev->dev, SSB_TMSLOW, tmslow);
	ssb_read32(dev->dev, SSB_TMSLOW);	/* flush */
	msleep(1);

	/* Turn Analog ON */
	b43_switch_analog(dev, 1);

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_GMODE;
	if (flags & B43_TMSLOW_GMODE)
		macctl |= B43_MACCTL_GMODE;
	macctl |= B43_MACCTL_IHR_ENABLED;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
}

static void handle_irq_transmit_status(struct b43_wldev *dev)
{
	u32 v0, v1;
	u16 tmp;
	struct b43_txstatus stat;

	while (1) {
		v0 = b43_read32(dev, B43_MMIO_XMITSTAT_0);
		if (!(v0 & 0x00000001))
			break;
		v1 = b43_read32(dev, B43_MMIO_XMITSTAT_1);

		stat.cookie = (v0 >> 16);
		stat.seq = (v1 & 0x0000FFFF);
		stat.phy_stat = ((v1 & 0x00FF0000) >> 16);
		tmp = (v0 & 0x0000FFFF);
		stat.frame_count = ((tmp & 0xF000) >> 12);
		stat.rts_count = ((tmp & 0x0F00) >> 8);
		stat.supp_reason = ((tmp & 0x001C) >> 2);
		stat.pm_indicated = !!(tmp & 0x0080);
		stat.intermediate = !!(tmp & 0x0040);
		stat.for_ampdu = !!(tmp & 0x0020);
		stat.acked = !!(tmp & 0x0002);

		b43_handle_txstatus(dev, &stat);
	}
}

static void drain_txstatus_queue(struct b43_wldev *dev)
{
	u32 dummy;

	if (dev->dev->id.revision < 5)
		return;
	/* Read all entries from the microcode TXstatus FIFO
	 * and throw them away.
	 */
	while (1) {
		dummy = b43_read32(dev, B43_MMIO_XMITSTAT_0);
		if (!(dummy & 0x00000001))
			break;
		dummy = b43_read32(dev, B43_MMIO_XMITSTAT_1);
	}
}

static u32 b43_jssi_read(struct b43_wldev *dev)
{
	u32 val = 0;

	val = b43_shm_read16(dev, B43_SHM_SHARED, 0x08A);
	val <<= 16;
	val |= b43_shm_read16(dev, B43_SHM_SHARED, 0x088);

	return val;
}

static void b43_jssi_write(struct b43_wldev *dev, u32 jssi)
{
	b43_shm_write16(dev, B43_SHM_SHARED, 0x088, (jssi & 0x0000FFFF));
	b43_shm_write16(dev, B43_SHM_SHARED, 0x08A, (jssi & 0xFFFF0000) >> 16);
}

static void b43_generate_noise_sample(struct b43_wldev *dev)
{
	b43_jssi_write(dev, 0x7F7F7F7F);
	b43_write32(dev, B43_MMIO_MACCMD,
		    b43_read32(dev, B43_MMIO_MACCMD) | B43_MACCMD_BGNOISE);
	B43_WARN_ON(dev->noisecalc.channel_at_start != dev->phy.channel);
}

static void b43_calculate_link_quality(struct b43_wldev *dev)
{
	/* Top half of Link Quality calculation. */

	if (dev->noisecalc.calculation_running)
		return;
	dev->noisecalc.channel_at_start = dev->phy.channel;
	dev->noisecalc.calculation_running = 1;
	dev->noisecalc.nr_samples = 0;

	b43_generate_noise_sample(dev);
}

static void handle_irq_noise(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 tmp;
	u8 noise[4];
	u8 i, j;
	s32 average;

	/* Bottom half of Link Quality calculation. */

	B43_WARN_ON(!dev->noisecalc.calculation_running);
	if (dev->noisecalc.channel_at_start != phy->channel)
		goto drop_calculation;
	*((__le32 *)noise) = cpu_to_le32(b43_jssi_read(dev));
	if (noise[0] == 0x7F || noise[1] == 0x7F ||
	    noise[2] == 0x7F || noise[3] == 0x7F)
		goto generate_new;

	/* Get the noise samples. */
	B43_WARN_ON(dev->noisecalc.nr_samples >= 8);
	i = dev->noisecalc.nr_samples;
	noise[0] = limit_value(noise[0], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[1] = limit_value(noise[1], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[2] = limit_value(noise[2], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[3] = limit_value(noise[3], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	dev->noisecalc.samples[i][0] = phy->nrssi_lt[noise[0]];
	dev->noisecalc.samples[i][1] = phy->nrssi_lt[noise[1]];
	dev->noisecalc.samples[i][2] = phy->nrssi_lt[noise[2]];
	dev->noisecalc.samples[i][3] = phy->nrssi_lt[noise[3]];
	dev->noisecalc.nr_samples++;
	if (dev->noisecalc.nr_samples == 8) {
		/* Calculate the Link Quality by the noise samples. */
		average = 0;
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 4; j++)
				average += dev->noisecalc.samples[i][j];
		}
		average /= (8 * 4);
		average *= 125;
		average += 64;
		average /= 128;
		tmp = b43_shm_read16(dev, B43_SHM_SHARED, 0x40C);
		tmp = (tmp / 128) & 0x1F;
		if (tmp >= 8)
			average += 2;
		else
			average -= 25;
		if (tmp == 8)
			average -= 72;
		else
			average -= 48;

		dev->stats.link_noise = average;
	      drop_calculation:
		dev->noisecalc.calculation_running = 0;
		return;
	}
      generate_new:
	b43_generate_noise_sample(dev);
}

static void handle_irq_tbtt_indication(struct b43_wldev *dev)
{
	if (b43_is_mode(dev->wl, IEEE80211_IF_TYPE_AP)) {
		///TODO: PS TBTT
	} else {
		if (1 /*FIXME: the last PSpoll frame was sent successfully */ )
			b43_power_saving_ctl_bits(dev, 0);
	}
	if (b43_is_mode(dev->wl, IEEE80211_IF_TYPE_IBSS))
		dev->dfq_valid = 1;
}

static void handle_irq_atim_end(struct b43_wldev *dev)
{
	if (dev->dfq_valid) {
		b43_write32(dev, B43_MMIO_MACCMD,
			    b43_read32(dev, B43_MMIO_MACCMD)
			    | B43_MACCMD_DFQ_VALID);
		dev->dfq_valid = 0;
	}
}

static void handle_irq_pmq(struct b43_wldev *dev)
{
	u32 tmp;

	//TODO: AP mode.

	while (1) {
		tmp = b43_read32(dev, B43_MMIO_PS_STATUS);
		if (!(tmp & 0x00000008))
			break;
	}
	/* 16bit write is odd, but correct. */
	b43_write16(dev, B43_MMIO_PS_STATUS, 0x0002);
}

static void b43_write_template_common(struct b43_wldev *dev,
				      const u8 * data, u16 size,
				      u16 ram_offset,
				      u16 shm_size_offset, u8 rate)
{
	u32 i, tmp;
	struct b43_plcp_hdr4 plcp;

	plcp.data = 0;
	b43_generate_plcp_hdr(&plcp, size + FCS_LEN, rate);
	b43_ram_write(dev, ram_offset, le32_to_cpu(plcp.data));
	ram_offset += sizeof(u32);
	/* The PLCP is 6 bytes long, but we only wrote 4 bytes, yet.
	 * So leave the first two bytes of the next write blank.
	 */
	tmp = (u32) (data[0]) << 16;
	tmp |= (u32) (data[1]) << 24;
	b43_ram_write(dev, ram_offset, tmp);
	ram_offset += sizeof(u32);
	for (i = 2; i < size; i += sizeof(u32)) {
		tmp = (u32) (data[i + 0]);
		if (i + 1 < size)
			tmp |= (u32) (data[i + 1]) << 8;
		if (i + 2 < size)
			tmp |= (u32) (data[i + 2]) << 16;
		if (i + 3 < size)
			tmp |= (u32) (data[i + 3]) << 24;
		b43_ram_write(dev, ram_offset + i - 2, tmp);
	}
	b43_shm_write16(dev, B43_SHM_SHARED, shm_size_offset,
			size + sizeof(struct b43_plcp_hdr6));
}

static void b43_write_beacon_template(struct b43_wldev *dev,
				      u16 ram_offset,
				      u16 shm_size_offset, u8 rate)
{
	unsigned int i, len, variable_len;
	const struct ieee80211_mgmt *bcn;
	const u8 *ie;
	bool tim_found = 0;

	bcn = (const struct ieee80211_mgmt *)(dev->wl->current_beacon->data);
	len = min((size_t) dev->wl->current_beacon->len,
		  0x200 - sizeof(struct b43_plcp_hdr6));

	b43_write_template_common(dev, (const u8 *)bcn,
				  len, ram_offset, shm_size_offset, rate);

	/* Find the position of the TIM and the DTIM_period value
	 * and write them to SHM. */
	ie = bcn->u.beacon.variable;
	variable_len = len - offsetof(struct ieee80211_mgmt, u.beacon.variable);
	for (i = 0; i < variable_len - 2; ) {
		uint8_t ie_id, ie_len;

		ie_id = ie[i];
		ie_len = ie[i + 1];
		if (ie_id == 5) {
			u16 tim_position;
			u16 dtim_period;
			/* This is the TIM Information Element */

			/* Check whether the ie_len is in the beacon data range. */
			if (variable_len < ie_len + 2 + i)
				break;
			/* A valid TIM is at least 4 bytes long. */
			if (ie_len < 4)
				break;
			tim_found = 1;

			tim_position = sizeof(struct b43_plcp_hdr6);
			tim_position += offsetof(struct ieee80211_mgmt, u.beacon.variable);
			tim_position += i;

			dtim_period = ie[i + 3];

			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_SHM_SH_TIMBPOS, tim_position);
			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_SHM_SH_DTIMPER, dtim_period);
			break;
		}
		i += ie_len + 2;
	}
	if (!tim_found) {
		b43warn(dev->wl, "Did not find a valid TIM IE in "
			"the beacon template packet. AP or IBSS operation "
			"may be broken.\n");
	}
}

static void b43_write_probe_resp_plcp(struct b43_wldev *dev,
				      u16 shm_offset, u16 size, u8 rate)
{
	struct b43_plcp_hdr4 plcp;
	u32 tmp;
	__le16 dur;

	plcp.data = 0;
	b43_generate_plcp_hdr(&plcp, size + FCS_LEN, rate);
	dur = ieee80211_generic_frame_duration(dev->wl->hw,
					       dev->wl->vif, size,
					       B43_RATE_TO_BASE100KBPS(rate));
	/* Write PLCP in two parts and timing for packet transfer */
	tmp = le32_to_cpu(plcp.data);
	b43_shm_write16(dev, B43_SHM_SHARED, shm_offset, tmp & 0xFFFF);
	b43_shm_write16(dev, B43_SHM_SHARED, shm_offset + 2, tmp >> 16);
	b43_shm_write16(dev, B43_SHM_SHARED, shm_offset + 6, le16_to_cpu(dur));
}

/* Instead of using custom probe response template, this function
 * just patches custom beacon template by:
 * 1) Changing packet type
 * 2) Patching duration field
 * 3) Stripping TIM
 */
static const u8 * b43_generate_probe_resp(struct b43_wldev *dev,
					  u16 *dest_size, u8 rate)
{
	const u8 *src_data;
	u8 *dest_data;
	u16 src_size, elem_size, src_pos, dest_pos;
	__le16 dur;
	struct ieee80211_hdr *hdr;
	size_t ie_start;

	src_size = dev->wl->current_beacon->len;
	src_data = (const u8 *)dev->wl->current_beacon->data;

	/* Get the start offset of the variable IEs in the packet. */
	ie_start = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	B43_WARN_ON(ie_start != offsetof(struct ieee80211_mgmt, u.beacon.variable));

	if (B43_WARN_ON(src_size < ie_start))
		return NULL;

	dest_data = kmalloc(src_size, GFP_ATOMIC);
	if (unlikely(!dest_data))
		return NULL;

	/* Copy the static data and all Information Elements, except the TIM. */
	memcpy(dest_data, src_data, ie_start);
	src_pos = ie_start;
	dest_pos = ie_start;
	for ( ; src_pos < src_size - 2; src_pos += elem_size) {
		elem_size = src_data[src_pos + 1] + 2;
		if (src_data[src_pos] == 5) {
			/* This is the TIM. */
			continue;
		}
		memcpy(dest_data + dest_pos, src_data + src_pos,
		       elem_size);
		dest_pos += elem_size;
	}
	*dest_size = dest_pos;
	hdr = (struct ieee80211_hdr *)dest_data;

	/* Set the frame control. */
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_RESP);
	dur = ieee80211_generic_frame_duration(dev->wl->hw,
					       dev->wl->vif, *dest_size,
					       B43_RATE_TO_BASE100KBPS(rate));
	hdr->duration_id = dur;

	return dest_data;
}

static void b43_write_probe_resp_template(struct b43_wldev *dev,
					  u16 ram_offset,
					  u16 shm_size_offset, u8 rate)
{
	const u8 *probe_resp_data;
	u16 size;

	size = dev->wl->current_beacon->len;
	probe_resp_data = b43_generate_probe_resp(dev, &size, rate);
	if (unlikely(!probe_resp_data))
		return;

	/* Looks like PLCP headers plus packet timings are stored for
	 * all possible basic rates
	 */
	b43_write_probe_resp_plcp(dev, 0x31A, size, B43_CCK_RATE_1MB);
	b43_write_probe_resp_plcp(dev, 0x32C, size, B43_CCK_RATE_2MB);
	b43_write_probe_resp_plcp(dev, 0x33E, size, B43_CCK_RATE_5MB);
	b43_write_probe_resp_plcp(dev, 0x350, size, B43_CCK_RATE_11MB);

	size = min((size_t) size, 0x200 - sizeof(struct b43_plcp_hdr6));
	b43_write_template_common(dev, probe_resp_data,
				  size, ram_offset, shm_size_offset, rate);
	kfree(probe_resp_data);
}

/* Asynchronously update the packet templates in template RAM.
 * Locking: Requires wl->irq_lock to be locked. */
static void b43_update_templates(struct b43_wl *wl, struct sk_buff *beacon)
{
	/* This is the top half of the ansynchronous beacon update.
	 * The bottom half is the beacon IRQ.
	 * Beacon update must be asynchronous to avoid sending an
	 * invalid beacon. This can happen for example, if the firmware
	 * transmits a beacon while we are updating it. */

	if (wl->current_beacon)
		dev_kfree_skb_any(wl->current_beacon);
	wl->current_beacon = beacon;
	wl->beacon0_uploaded = 0;
	wl->beacon1_uploaded = 0;
}

static void b43_set_ssid(struct b43_wldev *dev, const u8 * ssid, u8 ssid_len)
{
	u32 tmp;
	u16 i, len;

	len = min((u16) ssid_len, (u16) 0x100);
	for (i = 0; i < len; i += sizeof(u32)) {
		tmp = (u32) (ssid[i + 0]);
		if (i + 1 < len)
			tmp |= (u32) (ssid[i + 1]) << 8;
		if (i + 2 < len)
			tmp |= (u32) (ssid[i + 2]) << 16;
		if (i + 3 < len)
			tmp |= (u32) (ssid[i + 3]) << 24;
		b43_shm_write32(dev, B43_SHM_SHARED, 0x380 + i, tmp);
	}
	b43_shm_write16(dev, B43_SHM_SHARED, 0x48, len);
}

static void b43_set_beacon_int(struct b43_wldev *dev, u16 beacon_int)
{
	b43_time_lock(dev);
	if (dev->dev->id.revision >= 3) {
		b43_write32(dev, 0x188, (beacon_int << 16));
	} else {
		b43_write16(dev, 0x606, (beacon_int >> 6));
		b43_write16(dev, 0x610, beacon_int);
	}
	b43_time_unlock(dev);
}

static void handle_irq_beacon(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	u32 cmd;

	if (!b43_is_mode(wl, IEEE80211_IF_TYPE_AP))
		return;

	/* This is the bottom half of the asynchronous beacon update. */

	cmd = b43_read32(dev, B43_MMIO_MACCMD);
	if (!(cmd & B43_MACCMD_BEACON0_VALID)) {
		if (!wl->beacon0_uploaded) {
			b43_write_beacon_template(dev, 0x68, 0x18,
						  B43_CCK_RATE_1MB);
			b43_write_probe_resp_template(dev, 0x268, 0x4A,
						      B43_CCK_RATE_11MB);
			wl->beacon0_uploaded = 1;
		}
		cmd |= B43_MACCMD_BEACON0_VALID;
	}
	if (!(cmd & B43_MACCMD_BEACON1_VALID)) {
		if (!wl->beacon1_uploaded) {
			b43_write_beacon_template(dev, 0x468, 0x1A,
						  B43_CCK_RATE_1MB);
			wl->beacon1_uploaded = 1;
		}
		cmd |= B43_MACCMD_BEACON1_VALID;
	}
	b43_write32(dev, B43_MMIO_MACCMD, cmd);
}

static void handle_irq_ucode_debug(struct b43_wldev *dev)
{
	//TODO
}

/* Interrupt handler bottom-half */
static void b43_interrupt_tasklet(struct b43_wldev *dev)
{
	u32 reason;
	u32 dma_reason[ARRAY_SIZE(dev->dma_reason)];
	u32 merged_dma_reason = 0;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dev->wl->irq_lock, flags);

	B43_WARN_ON(b43_status(dev) != B43_STAT_STARTED);

	reason = dev->irq_reason;
	for (i = 0; i < ARRAY_SIZE(dma_reason); i++) {
		dma_reason[i] = dev->dma_reason[i];
		merged_dma_reason |= dma_reason[i];
	}

	if (unlikely(reason & B43_IRQ_MAC_TXERR))
		b43err(dev->wl, "MAC transmission error\n");

	if (unlikely(reason & B43_IRQ_PHY_TXERR)) {
		b43err(dev->wl, "PHY transmission error\n");
		rmb();
		if (unlikely(atomic_dec_and_test(&dev->phy.txerr_cnt))) {
			atomic_set(&dev->phy.txerr_cnt,
				   B43_PHY_TX_BADNESS_LIMIT);
			b43err(dev->wl, "Too many PHY TX errors, "
					"restarting the controller\n");
			b43_controller_restart(dev, "PHY TX errors");
		}
	}

	if (unlikely(merged_dma_reason & (B43_DMAIRQ_FATALMASK |
					  B43_DMAIRQ_NONFATALMASK))) {
		if (merged_dma_reason & B43_DMAIRQ_FATALMASK) {
			b43err(dev->wl, "Fatal DMA error: "
			       "0x%08X, 0x%08X, 0x%08X, "
			       "0x%08X, 0x%08X, 0x%08X\n",
			       dma_reason[0], dma_reason[1],
			       dma_reason[2], dma_reason[3],
			       dma_reason[4], dma_reason[5]);
			b43_controller_restart(dev, "DMA error");
			mmiowb();
			spin_unlock_irqrestore(&dev->wl->irq_lock, flags);
			return;
		}
		if (merged_dma_reason & B43_DMAIRQ_NONFATALMASK) {
			b43err(dev->wl, "DMA error: "
			       "0x%08X, 0x%08X, 0x%08X, "
			       "0x%08X, 0x%08X, 0x%08X\n",
			       dma_reason[0], dma_reason[1],
			       dma_reason[2], dma_reason[3],
			       dma_reason[4], dma_reason[5]);
		}
	}

	if (unlikely(reason & B43_IRQ_UCODE_DEBUG))
		handle_irq_ucode_debug(dev);
	if (reason & B43_IRQ_TBTT_INDI)
		handle_irq_tbtt_indication(dev);
	if (reason & B43_IRQ_ATIM_END)
		handle_irq_atim_end(dev);
	if (reason & B43_IRQ_BEACON)
		handle_irq_beacon(dev);
	if (reason & B43_IRQ_PMQ)
		handle_irq_pmq(dev);
	if (reason & B43_IRQ_TXFIFO_FLUSH_OK)
		;/* TODO */
	if (reason & B43_IRQ_NOISESAMPLE_OK)
		handle_irq_noise(dev);

	/* Check the DMA reason registers for received data. */
	if (dma_reason[0] & B43_DMAIRQ_RX_DONE)
		b43_dma_rx(dev->dma.rx_ring0);
	if (dma_reason[3] & B43_DMAIRQ_RX_DONE)
		b43_dma_rx(dev->dma.rx_ring3);
	B43_WARN_ON(dma_reason[1] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[2] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[4] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[5] & B43_DMAIRQ_RX_DONE);

	if (reason & B43_IRQ_TX_OK)
		handle_irq_transmit_status(dev);

	b43_interrupt_enable(dev, dev->irq_savedstate);
	mmiowb();
	spin_unlock_irqrestore(&dev->wl->irq_lock, flags);
}

static void b43_interrupt_ack(struct b43_wldev *dev, u32 reason)
{
	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, reason);

	b43_write32(dev, B43_MMIO_DMA0_REASON, dev->dma_reason[0]);
	b43_write32(dev, B43_MMIO_DMA1_REASON, dev->dma_reason[1]);
	b43_write32(dev, B43_MMIO_DMA2_REASON, dev->dma_reason[2]);
	b43_write32(dev, B43_MMIO_DMA3_REASON, dev->dma_reason[3]);
	b43_write32(dev, B43_MMIO_DMA4_REASON, dev->dma_reason[4]);
	b43_write32(dev, B43_MMIO_DMA5_REASON, dev->dma_reason[5]);
}

/* Interrupt handler top-half */
static irqreturn_t b43_interrupt_handler(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	struct b43_wldev *dev = dev_id;
	u32 reason;

	if (!dev)
		return IRQ_NONE;

	spin_lock(&dev->wl->irq_lock);

	if (b43_status(dev) < B43_STAT_STARTED)
		goto out;
	reason = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
	if (reason == 0xffffffff)	/* shared IRQ */
		goto out;
	ret = IRQ_HANDLED;
	reason &= b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);
	if (!reason)
		goto out;

	dev->dma_reason[0] = b43_read32(dev, B43_MMIO_DMA0_REASON)
	    & 0x0001DC00;
	dev->dma_reason[1] = b43_read32(dev, B43_MMIO_DMA1_REASON)
	    & 0x0000DC00;
	dev->dma_reason[2] = b43_read32(dev, B43_MMIO_DMA2_REASON)
	    & 0x0000DC00;
	dev->dma_reason[3] = b43_read32(dev, B43_MMIO_DMA3_REASON)
	    & 0x0001DC00;
	dev->dma_reason[4] = b43_read32(dev, B43_MMIO_DMA4_REASON)
	    & 0x0000DC00;
	dev->dma_reason[5] = b43_read32(dev, B43_MMIO_DMA5_REASON)
	    & 0x0000DC00;

	b43_interrupt_ack(dev, reason);
	/* disable all IRQs. They are enabled again in the bottom half. */
	dev->irq_savedstate = b43_interrupt_disable(dev, B43_IRQ_ALL);
	/* save the reason code and call our bottom half. */
	dev->irq_reason = reason;
	tasklet_schedule(&dev->isr_tasklet);
      out:
	mmiowb();
	spin_unlock(&dev->wl->irq_lock);

	return ret;
}

static void do_release_fw(struct b43_firmware_file *fw)
{
	release_firmware(fw->data);
	fw->data = NULL;
	fw->filename = NULL;
}

static void b43_release_firmware(struct b43_wldev *dev)
{
	do_release_fw(&dev->fw.ucode);
	do_release_fw(&dev->fw.pcm);
	do_release_fw(&dev->fw.initvals);
	do_release_fw(&dev->fw.initvals_band);
}

static void b43_print_fw_helptext(struct b43_wl *wl, bool error)
{
	const char *text;

	text = "You must go to "
	       "http://linuxwireless.org/en/users/Drivers/b43#devicefirmware "
	       "and download the latest firmware (version 4).\n";
	if (error)
		b43err(wl, text);
	else
		b43warn(wl, text);
}

static int do_request_fw(struct b43_wldev *dev,
			 const char *name,
			 struct b43_firmware_file *fw)
{
	char path[sizeof(modparam_fwpostfix) + 32];
	const struct firmware *blob;
	struct b43_fw_header *hdr;
	u32 size;
	int err;

	if (!name) {
		/* Don't fetch anything. Free possibly cached firmware. */
		do_release_fw(fw);
		return 0;
	}
	if (fw->filename) {
		if (strcmp(fw->filename, name) == 0)
			return 0; /* Already have this fw. */
		/* Free the cached firmware first. */
		do_release_fw(fw);
	}

	snprintf(path, ARRAY_SIZE(path),
		 "b43%s/%s.fw",
		 modparam_fwpostfix, name);
	err = request_firmware(&blob, path, dev->dev->dev);
	if (err) {
		b43err(dev->wl, "Firmware file \"%s\" not found "
		       "or load failed.\n", path);
		return err;
	}
	if (blob->size < sizeof(struct b43_fw_header))
		goto err_format;
	hdr = (struct b43_fw_header *)(blob->data);
	switch (hdr->type) {
	case B43_FW_TYPE_UCODE:
	case B43_FW_TYPE_PCM:
		size = be32_to_cpu(hdr->size);
		if (size != blob->size - sizeof(struct b43_fw_header))
			goto err_format;
		/* fallthrough */
	case B43_FW_TYPE_IV:
		if (hdr->ver != 1)
			goto err_format;
		break;
	default:
		goto err_format;
	}

	fw->data = blob;
	fw->filename = name;

	return 0;

err_format:
	b43err(dev->wl, "Firmware file \"%s\" format error.\n", path);
	release_firmware(blob);

	return -EPROTO;
}

static int b43_request_firmware(struct b43_wldev *dev)
{
	struct b43_firmware *fw = &dev->fw;
	const u8 rev = dev->dev->id.revision;
	const char *filename;
	u32 tmshigh;
	int err;

	/* Get microcode */
	tmshigh = ssb_read32(dev->dev, SSB_TMSHIGH);
	if ((rev >= 5) && (rev <= 10))
		filename = "ucode5";
	else if ((rev >= 11) && (rev <= 12))
		filename = "ucode11";
	else if (rev >= 13)
		filename = "ucode13";
	else
		goto err_no_ucode;
	err = do_request_fw(dev, filename, &fw->ucode);
	if (err)
		goto err_load;

	/* Get PCM code */
	if ((rev >= 5) && (rev <= 10))
		filename = "pcm5";
	else if (rev >= 11)
		filename = NULL;
	else
		goto err_no_pcm;
	err = do_request_fw(dev, filename, &fw->pcm);
	if (err)
		goto err_load;

	/* Get initvals */
	switch (dev->phy.type) {
	case B43_PHYTYPE_A:
		if ((rev >= 5) && (rev <= 10)) {
			if (tmshigh & B43_TMSHIGH_HAVE_2GHZ_PHY)
				filename = "a0g1initvals5";
			else
				filename = "a0g0initvals5";
		} else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_G:
		if ((rev >= 5) && (rev <= 10))
			filename = "b0g0initvals5";
		else if (rev >= 13)
			filename = "lp0initvals13";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_N:
		if ((rev >= 11) && (rev <= 12))
			filename = "n0initvals11";
		else
			goto err_no_initvals;
		break;
	default:
		goto err_no_initvals;
	}
	err = do_request_fw(dev, filename, &fw->initvals);
	if (err)
		goto err_load;

	/* Get bandswitch initvals */
	switch (dev->phy.type) {
	case B43_PHYTYPE_A:
		if ((rev >= 5) && (rev <= 10)) {
			if (tmshigh & B43_TMSHIGH_HAVE_2GHZ_PHY)
				filename = "a0g1bsinitvals5";
			else
				filename = "a0g0bsinitvals5";
		} else if (rev >= 11)
			filename = NULL;
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_G:
		if ((rev >= 5) && (rev <= 10))
			filename = "b0g0bsinitvals5";
		else if (rev >= 11)
			filename = NULL;
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_N:
		if ((rev >= 11) && (rev <= 12))
			filename = "n0bsinitvals11";
		else
			goto err_no_initvals;
		break;
	default:
		goto err_no_initvals;
	}
	err = do_request_fw(dev, filename, &fw->initvals_band);
	if (err)
		goto err_load;

	return 0;

err_load:
	b43_print_fw_helptext(dev->wl, 1);
	goto error;

err_no_ucode:
	err = -ENODEV;
	b43err(dev->wl, "No microcode available for core rev %u\n", rev);
	goto error;

err_no_pcm:
	err = -ENODEV;
	b43err(dev->wl, "No PCM available for core rev %u\n", rev);
	goto error;

err_no_initvals:
	err = -ENODEV;
	b43err(dev->wl, "No Initial Values firmware file for PHY %u, "
	       "core rev %u\n", dev->phy.type, rev);
	goto error;

error:
	b43_release_firmware(dev);
	return err;
}

static int b43_upload_microcode(struct b43_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43_fw_header);
	const __be32 *data;
	unsigned int i, len;
	u16 fwrev, fwpatch, fwdate, fwtime;
	u32 tmp, macctl;
	int err = 0;

	/* Jump the microcode PSM to offset 0 */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	B43_WARN_ON(macctl & B43_MACCTL_PSM_RUN);
	macctl |= B43_MACCTL_PSM_JMP0;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);
	/* Zero out all microcode PSM registers and shared memory. */
	for (i = 0; i < 64; i++)
		b43_shm_write16(dev, B43_SHM_SCRATCH, i, 0);
	for (i = 0; i < 4096; i += 2)
		b43_shm_write16(dev, B43_SHM_SHARED, i, 0);

	/* Upload Microcode. */
	data = (__be32 *) (dev->fw.ucode.data->data + hdr_len);
	len = (dev->fw.ucode.data->size - hdr_len) / sizeof(__be32);
	b43_shm_control_word(dev, B43_SHM_UCODE | B43_SHM_AUTOINC_W, 0x0000);
	for (i = 0; i < len; i++) {
		b43_write32(dev, B43_MMIO_SHM_DATA, be32_to_cpu(data[i]));
		udelay(10);
	}

	if (dev->fw.pcm.data) {
		/* Upload PCM data. */
		data = (__be32 *) (dev->fw.pcm.data->data + hdr_len);
		len = (dev->fw.pcm.data->size - hdr_len) / sizeof(__be32);
		b43_shm_control_word(dev, B43_SHM_HW, 0x01EA);
		b43_write32(dev, B43_MMIO_SHM_DATA, 0x00004000);
		/* No need for autoinc bit in SHM_HW */
		b43_shm_control_word(dev, B43_SHM_HW, 0x01EB);
		for (i = 0; i < len; i++) {
			b43_write32(dev, B43_MMIO_SHM_DATA, be32_to_cpu(data[i]));
			udelay(10);
		}
	}

	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, B43_IRQ_ALL);

	/* Start the microcode PSM */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_PSM_JMP0;
	macctl |= B43_MACCTL_PSM_RUN;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);

	/* Wait for the microcode to load and respond */
	i = 0;
	while (1) {
		tmp = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
		if (tmp == B43_IRQ_MAC_SUSPENDED)
			break;
		i++;
		if (i >= 20) {
			b43err(dev->wl, "Microcode not responding\n");
			b43_print_fw_helptext(dev->wl, 1);
			err = -ENODEV;
			goto error;
		}
		msleep_interruptible(50);
		if (signal_pending(current)) {
			err = -EINTR;
			goto error;
		}
	}
	b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);	/* dummy read */

	/* Get and check the revisions. */
	fwrev = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEREV);
	fwpatch = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEPATCH);
	fwdate = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEDATE);
	fwtime = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODETIME);

	if (fwrev <= 0x128) {
		b43err(dev->wl, "YOUR FIRMWARE IS TOO OLD. Firmware from "
		       "binary drivers older than version 4.x is unsupported. "
		       "You must upgrade your firmware files.\n");
		b43_print_fw_helptext(dev->wl, 1);
		err = -EOPNOTSUPP;
		goto error;
	}
	b43dbg(dev->wl, "Loading firmware version %u.%u "
	       "(20%.2i-%.2i-%.2i %.2i:%.2i:%.2i)\n",
	       fwrev, fwpatch,
	       (fwdate >> 12) & 0xF, (fwdate >> 8) & 0xF, fwdate & 0xFF,
	       (fwtime >> 11) & 0x1F, (fwtime >> 5) & 0x3F, fwtime & 0x1F);

	dev->fw.rev = fwrev;
	dev->fw.patch = fwpatch;

	if (b43_is_old_txhdr_format(dev)) {
		b43warn(dev->wl, "You are using an old firmware image. "
			"Support for old firmware will be removed in July 2008.\n");
		b43_print_fw_helptext(dev->wl, 0);
	}

	return 0;

error:
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_PSM_RUN;
	macctl |= B43_MACCTL_PSM_JMP0;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);

	return err;
}

static int b43_write_initvals(struct b43_wldev *dev,
			      const struct b43_iv *ivals,
			      size_t count,
			      size_t array_size)
{
	const struct b43_iv *iv;
	u16 offset;
	size_t i;
	bool bit32;

	BUILD_BUG_ON(sizeof(struct b43_iv) != 6);
	iv = ivals;
	for (i = 0; i < count; i++) {
		if (array_size < sizeof(iv->offset_size))
			goto err_format;
		array_size -= sizeof(iv->offset_size);
		offset = be16_to_cpu(iv->offset_size);
		bit32 = !!(offset & B43_IV_32BIT);
		offset &= B43_IV_OFFSET_MASK;
		if (offset >= 0x1000)
			goto err_format;
		if (bit32) {
			u32 value;

			if (array_size < sizeof(iv->data.d32))
				goto err_format;
			array_size -= sizeof(iv->data.d32);

			value = be32_to_cpu(get_unaligned(&iv->data.d32));
			b43_write32(dev, offset, value);

			iv = (const struct b43_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be32));
		} else {
			u16 value;

			if (array_size < sizeof(iv->data.d16))
				goto err_format;
			array_size -= sizeof(iv->data.d16);

			value = be16_to_cpu(iv->data.d16);
			b43_write16(dev, offset, value);

			iv = (const struct b43_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be16));
		}
	}
	if (array_size)
		goto err_format;

	return 0;

err_format:
	b43err(dev->wl, "Initial Values Firmware file-format error.\n");
	b43_print_fw_helptext(dev->wl, 1);

	return -EPROTO;
}

static int b43_upload_initvals(struct b43_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43_fw_header);
	const struct b43_fw_header *hdr;
	struct b43_firmware *fw = &dev->fw;
	const struct b43_iv *ivals;
	size_t count;
	int err;

	hdr = (const struct b43_fw_header *)(fw->initvals.data->data);
	ivals = (const struct b43_iv *)(fw->initvals.data->data + hdr_len);
	count = be32_to_cpu(hdr->size);
	err = b43_write_initvals(dev, ivals, count,
				 fw->initvals.data->size - hdr_len);
	if (err)
		goto out;
	if (fw->initvals_band.data) {
		hdr = (const struct b43_fw_header *)(fw->initvals_band.data->data);
		ivals = (const struct b43_iv *)(fw->initvals_band.data->data + hdr_len);
		count = be32_to_cpu(hdr->size);
		err = b43_write_initvals(dev, ivals, count,
					 fw->initvals_band.data->size - hdr_len);
		if (err)
			goto out;
	}
out:

	return err;
}

/* Initialize the GPIOs
 * http://bcm-specs.sipsolutions.net/GPIO
 */
static int b43_gpio_init(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct ssb_device *gpiodev, *pcidev = NULL;
	u32 mask, set;

	b43_write32(dev, B43_MMIO_MACCTL, b43_read32(dev, B43_MMIO_MACCTL)
		    & ~B43_MACCTL_GPOUTSMSK);

	b43_write16(dev, B43_MMIO_GPIO_MASK, b43_read16(dev, B43_MMIO_GPIO_MASK)
		    | 0x000F);

	mask = 0x0000001F;
	set = 0x0000000F;
	if (dev->dev->bus->chip_id == 0x4301) {
		mask |= 0x0060;
		set |= 0x0060;
	}
	if (0 /* FIXME: conditional unknown */ ) {
		b43_write16(dev, B43_MMIO_GPIO_MASK,
			    b43_read16(dev, B43_MMIO_GPIO_MASK)
			    | 0x0100);
		mask |= 0x0180;
		set |= 0x0180;
	}
	if (dev->dev->bus->sprom.boardflags_lo & B43_BFL_PACTRL) {
		b43_write16(dev, B43_MMIO_GPIO_MASK,
			    b43_read16(dev, B43_MMIO_GPIO_MASK)
			    | 0x0200);
		mask |= 0x0200;
		set |= 0x0200;
	}
	if (dev->dev->id.revision >= 2)
		mask |= 0x0010;	/* FIXME: This is redundant. */

#ifdef CONFIG_SSB_DRIVER_PCICORE
	pcidev = bus->pcicore.dev;
#endif
	gpiodev = bus->chipco.dev ? : pcidev;
	if (!gpiodev)
		return 0;
	ssb_write32(gpiodev, B43_GPIO_CONTROL,
		    (ssb_read32(gpiodev, B43_GPIO_CONTROL)
		     & mask) | set);

	return 0;
}

/* Turn off all GPIO stuff. Call this on module unload, for example. */
static void b43_gpio_cleanup(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct ssb_device *gpiodev, *pcidev = NULL;

#ifdef CONFIG_SSB_DRIVER_PCICORE
	pcidev = bus->pcicore.dev;
#endif
	gpiodev = bus->chipco.dev ? : pcidev;
	if (!gpiodev)
		return;
	ssb_write32(gpiodev, B43_GPIO_CONTROL, 0);
}

/* http://bcm-specs.sipsolutions.net/EnableMac */
void b43_mac_enable(struct b43_wldev *dev)
{
	dev->mac_suspended--;
	B43_WARN_ON(dev->mac_suspended < 0);
	B43_WARN_ON(irqs_disabled());
	if (dev->mac_suspended == 0) {
		b43_write32(dev, B43_MMIO_MACCTL,
			    b43_read32(dev, B43_MMIO_MACCTL)
			    | B43_MACCTL_ENABLED);
		b43_write32(dev, B43_MMIO_GEN_IRQ_REASON,
			    B43_IRQ_MAC_SUSPENDED);
		/* Commit writes */
		b43_read32(dev, B43_MMIO_MACCTL);
		b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
		b43_power_saving_ctl_bits(dev, 0);

		/* Re-enable IRQs. */
		spin_lock_irq(&dev->wl->irq_lock);
		b43_interrupt_enable(dev, dev->irq_savedstate);
		spin_unlock_irq(&dev->wl->irq_lock);
	}
}

/* http://bcm-specs.sipsolutions.net/SuspendMAC */
void b43_mac_suspend(struct b43_wldev *dev)
{
	int i;
	u32 tmp;

	might_sleep();
	B43_WARN_ON(irqs_disabled());
	B43_WARN_ON(dev->mac_suspended < 0);

	if (dev->mac_suspended == 0) {
		/* Mask IRQs before suspending MAC. Otherwise
		 * the MAC stays busy and won't suspend. */
		spin_lock_irq(&dev->wl->irq_lock);
		tmp = b43_interrupt_disable(dev, B43_IRQ_ALL);
		spin_unlock_irq(&dev->wl->irq_lock);
		b43_synchronize_irq(dev);
		dev->irq_savedstate = tmp;

		b43_power_saving_ctl_bits(dev, B43_PS_AWAKE);
		b43_write32(dev, B43_MMIO_MACCTL,
			    b43_read32(dev, B43_MMIO_MACCTL)
			    & ~B43_MACCTL_ENABLED);
		/* force pci to flush the write */
		b43_read32(dev, B43_MMIO_MACCTL);
		for (i = 40; i; i--) {
			tmp = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
			if (tmp & B43_IRQ_MAC_SUSPENDED)
				goto out;
			msleep(1);
		}
		b43err(dev->wl, "MAC suspend failed\n");
	}
out:
	dev->mac_suspended++;
}

static void b43_adjust_opmode(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	u32 ctl;
	u16 cfp_pretbtt;

	ctl = b43_read32(dev, B43_MMIO_MACCTL);
	/* Reset status to STA infrastructure mode. */
	ctl &= ~B43_MACCTL_AP;
	ctl &= ~B43_MACCTL_KEEP_CTL;
	ctl &= ~B43_MACCTL_KEEP_BADPLCP;
	ctl &= ~B43_MACCTL_KEEP_BAD;
	ctl &= ~B43_MACCTL_PROMISC;
	ctl &= ~B43_MACCTL_BEACPROMISC;
	ctl |= B43_MACCTL_INFRA;

	if (b43_is_mode(wl, IEEE80211_IF_TYPE_AP))
		ctl |= B43_MACCTL_AP;
	else if (b43_is_mode(wl, IEEE80211_IF_TYPE_IBSS))
		ctl &= ~B43_MACCTL_INFRA;

	if (wl->filter_flags & FIF_CONTROL)
		ctl |= B43_MACCTL_KEEP_CTL;
	if (wl->filter_flags & FIF_FCSFAIL)
		ctl |= B43_MACCTL_KEEP_BAD;
	if (wl->filter_flags & FIF_PLCPFAIL)
		ctl |= B43_MACCTL_KEEP_BADPLCP;
	if (wl->filter_flags & FIF_PROMISC_IN_BSS)
		ctl |= B43_MACCTL_PROMISC;
	if (wl->filter_flags & FIF_BCN_PRBRESP_PROMISC)
		ctl |= B43_MACCTL_BEACPROMISC;

	/* Workaround: On old hardware the HW-MAC-address-filter
	 * doesn't work properly, so always run promisc in filter
	 * it in software. */
	if (dev->dev->id.revision <= 4)
		ctl |= B43_MACCTL_PROMISC;

	b43_write32(dev, B43_MMIO_MACCTL, ctl);

	cfp_pretbtt = 2;
	if ((ctl & B43_MACCTL_INFRA) && !(ctl & B43_MACCTL_AP)) {
		if (dev->dev->bus->chip_id == 0x4306 &&
		    dev->dev->bus->chip_rev == 3)
			cfp_pretbtt = 100;
		else
			cfp_pretbtt = 50;
	}
	b43_write16(dev, 0x612, cfp_pretbtt);
}

static void b43_rate_memory_write(struct b43_wldev *dev, u16 rate, int is_ofdm)
{
	u16 offset;

	if (is_ofdm) {
		offset = 0x480;
		offset += (b43_plcp_get_ratecode_ofdm(rate) & 0x000F) * 2;
	} else {
		offset = 0x4C0;
		offset += (b43_plcp_get_ratecode_cck(rate) & 0x000F) * 2;
	}
	b43_shm_write16(dev, B43_SHM_SHARED, offset + 0x20,
			b43_shm_read16(dev, B43_SHM_SHARED, offset));
}

static void b43_rate_memory_init(struct b43_wldev *dev)
{
	switch (dev->phy.type) {
	case B43_PHYTYPE_A:
	case B43_PHYTYPE_G:
	case B43_PHYTYPE_N:
		b43_rate_memory_write(dev, B43_OFDM_RATE_6MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_12MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_18MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_24MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_36MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_48MB, 1);
		b43_rate_memory_write(dev, B43_OFDM_RATE_54MB, 1);
		if (dev->phy.type == B43_PHYTYPE_A)
			break;
		/* fallthrough */
	case B43_PHYTYPE_B:
		b43_rate_memory_write(dev, B43_CCK_RATE_1MB, 0);
		b43_rate_memory_write(dev, B43_CCK_RATE_2MB, 0);
		b43_rate_memory_write(dev, B43_CCK_RATE_5MB, 0);
		b43_rate_memory_write(dev, B43_CCK_RATE_11MB, 0);
		break;
	default:
		B43_WARN_ON(1);
	}
}

/* Set the TX-Antenna for management frames sent by firmware. */
static void b43_mgmtframe_txantenna(struct b43_wldev *dev, int antenna)
{
	u16 ant = 0;
	u16 tmp;

	switch (antenna) {
	case B43_ANTENNA0:
		ant |= B43_TXH_PHY_ANT0;
		break;
	case B43_ANTENNA1:
		ant |= B43_TXH_PHY_ANT1;
		break;
	case B43_ANTENNA2:
		ant |= B43_TXH_PHY_ANT2;
		break;
	case B43_ANTENNA3:
		ant |= B43_TXH_PHY_ANT3;
		break;
	case B43_ANTENNA_AUTO:
		ant |= B43_TXH_PHY_ANT01AUTO;
		break;
	default:
		B43_WARN_ON(1);
	}

	/* FIXME We also need to set the other flags of the PHY control field somewhere. */

	/* For Beacons */
	tmp = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL);
	tmp = (tmp & ~B43_TXH_PHY_ANT) | ant;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL, tmp);
	/* For ACK/CTS */
	tmp = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_ACKCTSPHYCTL);
	tmp = (tmp & ~B43_TXH_PHY_ANT) | ant;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_ACKCTSPHYCTL, tmp);
	/* For Probe Resposes */
	tmp = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_PRPHYCTL);
	tmp = (tmp & ~B43_TXH_PHY_ANT) | ant;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_PRPHYCTL, tmp);
}

/* This is the opposite of b43_chip_init() */
static void b43_chip_exit(struct b43_wldev *dev)
{
	b43_radio_turn_off(dev, 1);
	b43_gpio_cleanup(dev);
	/* firmware is released later */
}

/* Initialize the chip
 * http://bcm-specs.sipsolutions.net/ChipInit
 */
static int b43_chip_init(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	int err, tmp;
	u32 value32, macctl;
	u16 value16;

	/* Initialize the MAC control */
	macctl = B43_MACCTL_IHR_ENABLED | B43_MACCTL_SHM_ENABLED;
	if (dev->phy.gmode)
		macctl |= B43_MACCTL_GMODE;
	macctl |= B43_MACCTL_INFRA;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);

	err = b43_request_firmware(dev);
	if (err)
		goto out;
	err = b43_upload_microcode(dev);
	if (err)
		goto out;	/* firmware is released later */

	err = b43_gpio_init(dev);
	if (err)
		goto out;	/* firmware is released later */

	err = b43_upload_initvals(dev);
	if (err)
		goto err_gpio_clean;
	b43_radio_turn_on(dev);

	b43_write16(dev, 0x03E6, 0x0000);
	err = b43_phy_init(dev);
	if (err)
		goto err_radio_off;

	/* Select initial Interference Mitigation. */
	tmp = phy->interfmode;
	phy->interfmode = B43_INTERFMODE_NONE;
	b43_radio_set_interference_mitigation(dev, tmp);

	b43_set_rx_antenna(dev, B43_ANTENNA_DEFAULT);
	b43_mgmtframe_txantenna(dev, B43_ANTENNA_DEFAULT);

	if (phy->type == B43_PHYTYPE_B) {
		value16 = b43_read16(dev, 0x005E);
		value16 |= 0x0004;
		b43_write16(dev, 0x005E, value16);
	}
	b43_write32(dev, 0x0100, 0x01000000);
	if (dev->dev->id.revision < 5)
		b43_write32(dev, 0x010C, 0x01000000);

	b43_write32(dev, B43_MMIO_MACCTL, b43_read32(dev, B43_MMIO_MACCTL)
		    & ~B43_MACCTL_INFRA);
	b43_write32(dev, B43_MMIO_MACCTL, b43_read32(dev, B43_MMIO_MACCTL)
		    | B43_MACCTL_INFRA);

	/* Probe Response Timeout value */
	/* FIXME: Default to 0, has to be set by ioctl probably... :-/ */
	b43_shm_write16(dev, B43_SHM_SHARED, 0x0074, 0x0000);

	/* Initially set the wireless operation mode. */
	b43_adjust_opmode(dev);

	if (dev->dev->id.revision < 3) {
		b43_write16(dev, 0x060E, 0x0000);
		b43_write16(dev, 0x0610, 0x8000);
		b43_write16(dev, 0x0604, 0x0000);
		b43_write16(dev, 0x0606, 0x0200);
	} else {
		b43_write32(dev, 0x0188, 0x80000000);
		b43_write32(dev, 0x018C, 0x02000000);
	}
	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, 0x00004000);
	b43_write32(dev, B43_MMIO_DMA0_IRQ_MASK, 0x0001DC00);
	b43_write32(dev, B43_MMIO_DMA1_IRQ_MASK, 0x0000DC00);
	b43_write32(dev, B43_MMIO_DMA2_IRQ_MASK, 0x0000DC00);
	b43_write32(dev, B43_MMIO_DMA3_IRQ_MASK, 0x0001DC00);
	b43_write32(dev, B43_MMIO_DMA4_IRQ_MASK, 0x0000DC00);
	b43_write32(dev, B43_MMIO_DMA5_IRQ_MASK, 0x0000DC00);

	value32 = ssb_read32(dev->dev, SSB_TMSLOW);
	value32 |= 0x00100000;
	ssb_write32(dev->dev, SSB_TMSLOW, value32);

	b43_write16(dev, B43_MMIO_POWERUP_DELAY,
		    dev->dev->bus->chipco.fast_pwrup_delay);

	err = 0;
	b43dbg(dev->wl, "Chip initialized\n");
out:
	return err;

err_radio_off:
	b43_radio_turn_off(dev, 1);
err_gpio_clean:
	b43_gpio_cleanup(dev);
	return err;
}

static void b43_periodic_every120sec(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->type != B43_PHYTYPE_G || phy->rev < 2)
		return;

	b43_mac_suspend(dev);
	b43_lo_g_measure(dev);
	b43_mac_enable(dev);
	if (b43_has_hardware_pctl(phy))
		b43_lo_g_ctl_mark_all_unused(dev);
}

static void b43_periodic_every60sec(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->type != B43_PHYTYPE_G)
		return;
	if (!b43_has_hardware_pctl(phy))
		b43_lo_g_ctl_mark_all_unused(dev);
	if (dev->dev->bus->sprom.boardflags_lo & B43_BFL_RSSI) {
		b43_mac_suspend(dev);
		b43_calc_nrssi_slope(dev);
		if ((phy->radio_ver == 0x2050) && (phy->radio_rev == 8)) {
			u8 old_chan = phy->channel;

			/* VCO Calibration */
			if (old_chan >= 8)
				b43_radio_selectchannel(dev, 1, 0);
			else
				b43_radio_selectchannel(dev, 13, 0);
			b43_radio_selectchannel(dev, old_chan, 0);
		}
		b43_mac_enable(dev);
	}
}

static void b43_periodic_every30sec(struct b43_wldev *dev)
{
	/* Update device statistics. */
	b43_calculate_link_quality(dev);
}

static void b43_periodic_every15sec(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->type == B43_PHYTYPE_G) {
		//TODO: update_aci_moving_average
		if (phy->aci_enable && phy->aci_wlan_automatic) {
			b43_mac_suspend(dev);
			if (!phy->aci_enable && 1 /*TODO: not scanning? */ ) {
				if (0 /*TODO: bunch of conditions */ ) {
					b43_radio_set_interference_mitigation
					    (dev, B43_INTERFMODE_MANUALWLAN);
				}
			} else if (1 /*TODO*/) {
				/*
				   if ((aci_average > 1000) && !(b43_radio_aci_scan(dev))) {
				   b43_radio_set_interference_mitigation(dev,
				   B43_INTERFMODE_NONE);
				   }
				 */
			}
			b43_mac_enable(dev);
		} else if (phy->interfmode == B43_INTERFMODE_NONWLAN &&
			   phy->rev == 1) {
			//TODO: implement rev1 workaround
		}
	}
	b43_phy_xmitpower(dev);	//FIXME: unless scanning?
	//TODO for APHY (temperature?)

	atomic_set(&phy->txerr_cnt, B43_PHY_TX_BADNESS_LIMIT);
	wmb();
}

static void do_periodic_work(struct b43_wldev *dev)
{
	unsigned int state;

	state = dev->periodic_state;
	if (state % 8 == 0)
		b43_periodic_every120sec(dev);
	if (state % 4 == 0)
		b43_periodic_every60sec(dev);
	if (state % 2 == 0)
		b43_periodic_every30sec(dev);
	b43_periodic_every15sec(dev);
}

/* Periodic work locking policy:
 * 	The whole periodic work handler is protected by
 * 	wl->mutex. If another lock is needed somewhere in the
 * 	pwork callchain, it's aquired in-place, where it's needed.
 */
static void b43_periodic_work_handler(struct work_struct *work)
{
	struct b43_wldev *dev = container_of(work, struct b43_wldev,
					     periodic_work.work);
	struct b43_wl *wl = dev->wl;
	unsigned long delay;

	mutex_lock(&wl->mutex);

	if (unlikely(b43_status(dev) != B43_STAT_STARTED))
		goto out;
	if (b43_debug(dev, B43_DBG_PWORK_STOP))
		goto out_requeue;

	do_periodic_work(dev);

	dev->periodic_state++;
out_requeue:
	if (b43_debug(dev, B43_DBG_PWORK_FAST))
		delay = msecs_to_jiffies(50);
	else
		delay = round_jiffies_relative(HZ * 15);
	queue_delayed_work(wl->hw->workqueue, &dev->periodic_work, delay);
out:
	mutex_unlock(&wl->mutex);
}

static void b43_periodic_tasks_setup(struct b43_wldev *dev)
{
	struct delayed_work *work = &dev->periodic_work;

	dev->periodic_state = 0;
	INIT_DELAYED_WORK(work, b43_periodic_work_handler);
	queue_delayed_work(dev->wl->hw->workqueue, work, 0);
}

/* Check if communication with the device works correctly. */
static int b43_validate_chipaccess(struct b43_wldev *dev)
{
	u32 v, backup;

	backup = b43_shm_read32(dev, B43_SHM_SHARED, 0);

	/* Check for read/write and endianness problems. */
	b43_shm_write32(dev, B43_SHM_SHARED, 0, 0x55AAAA55);
	if (b43_shm_read32(dev, B43_SHM_SHARED, 0) != 0x55AAAA55)
		goto error;
	b43_shm_write32(dev, B43_SHM_SHARED, 0, 0xAA5555AA);
	if (b43_shm_read32(dev, B43_SHM_SHARED, 0) != 0xAA5555AA)
		goto error;

	b43_shm_write32(dev, B43_SHM_SHARED, 0, backup);

	if ((dev->dev->id.revision >= 3) && (dev->dev->id.revision <= 10)) {
		/* The 32bit register shadows the two 16bit registers
		 * with update sideeffects. Validate this. */
		b43_write16(dev, B43_MMIO_TSF_CFP_START, 0xAAAA);
		b43_write32(dev, B43_MMIO_TSF_CFP_START, 0xCCCCBBBB);
		if (b43_read16(dev, B43_MMIO_TSF_CFP_START_LOW) != 0xBBBB)
			goto error;
		if (b43_read16(dev, B43_MMIO_TSF_CFP_START_HIGH) != 0xCCCC)
			goto error;
	}
	b43_write32(dev, B43_MMIO_TSF_CFP_START, 0);

	v = b43_read32(dev, B43_MMIO_MACCTL);
	v |= B43_MACCTL_GMODE;
	if (v != (B43_MACCTL_GMODE | B43_MACCTL_IHR_ENABLED))
		goto error;

	return 0;
error:
	b43err(dev->wl, "Failed to validate the chipaccess\n");
	return -ENODEV;
}

static void b43_security_init(struct b43_wldev *dev)
{
	dev->max_nr_keys = (dev->dev->id.revision >= 5) ? 58 : 20;
	B43_WARN_ON(dev->max_nr_keys > ARRAY_SIZE(dev->key));
	dev->ktp = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_KTP);
	/* KTP is a word address, but we address SHM bytewise.
	 * So multiply by two.
	 */
	dev->ktp *= 2;
	if (dev->dev->id.revision >= 5) {
		/* Number of RCMTA address slots */
		b43_write16(dev, B43_MMIO_RCMTA_COUNT, dev->max_nr_keys - 8);
	}
	b43_clear_keys(dev);
}

static int b43_rng_read(struct hwrng *rng, u32 * data)
{
	struct b43_wl *wl = (struct b43_wl *)rng->priv;
	unsigned long flags;

	/* Don't take wl->mutex here, as it could deadlock with
	 * hwrng internal locking. It's not needed to take
	 * wl->mutex here, anyway. */

	spin_lock_irqsave(&wl->irq_lock, flags);
	*data = b43_read16(wl->current_dev, B43_MMIO_RNG);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return (sizeof(u16));
}

static void b43_rng_exit(struct b43_wl *wl)
{
	if (wl->rng_initialized)
		hwrng_unregister(&wl->rng);
}

static int b43_rng_init(struct b43_wl *wl)
{
	int err;

	snprintf(wl->rng_name, ARRAY_SIZE(wl->rng_name),
		 "%s_%s", KBUILD_MODNAME, wiphy_name(wl->hw->wiphy));
	wl->rng.name = wl->rng_name;
	wl->rng.data_read = b43_rng_read;
	wl->rng.priv = (unsigned long)wl;
	wl->rng_initialized = 1;
	err = hwrng_register(&wl->rng);
	if (err) {
		wl->rng_initialized = 0;
		b43err(wl, "Failed to register the random "
		       "number generator (%d)\n", err);
	}

	return err;
}

static int b43_op_tx(struct ieee80211_hw *hw,
		     struct sk_buff *skb,
		     struct ieee80211_tx_control *ctl)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	int err = -ENODEV;

	if (unlikely(!dev))
		goto out;
	if (unlikely(b43_status(dev) < B43_STAT_STARTED))
		goto out;
	/* DMA-TX is done without a global lock. */
	err = b43_dma_tx(dev, skb, ctl);
out:
	if (unlikely(err))
		return NETDEV_TX_BUSY;
	return NETDEV_TX_OK;
}

static int b43_op_conf_tx(struct ieee80211_hw *hw,
			  int queue,
			  const struct ieee80211_tx_queue_params *params)
{
	return 0;
}

static int b43_op_get_tx_stats(struct ieee80211_hw *hw,
			       struct ieee80211_tx_queue_stats *stats)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	unsigned long flags;
	int err = -ENODEV;

	if (!dev)
		goto out;
	spin_lock_irqsave(&wl->irq_lock, flags);
	if (likely(b43_status(dev) >= B43_STAT_STARTED)) {
		b43_dma_get_tx_stats(dev, stats);
		err = 0;
	}
	spin_unlock_irqrestore(&wl->irq_lock, flags);
out:
	return err;
}

static int b43_op_get_stats(struct ieee80211_hw *hw,
			    struct ieee80211_low_level_stats *stats)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	unsigned long flags;

	spin_lock_irqsave(&wl->irq_lock, flags);
	memcpy(stats, &wl->ieee_stats, sizeof(*stats));
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return 0;
}

static const char *phymode_to_string(unsigned int phymode)
{
	switch (phymode) {
	case B43_PHYMODE_A:
		return "A";
	case B43_PHYMODE_B:
		return "B";
	case B43_PHYMODE_G:
		return "G";
	default:
		B43_WARN_ON(1);
	}
	return "";
}

static int find_wldev_for_phymode(struct b43_wl *wl,
				  unsigned int phymode,
				  struct b43_wldev **dev, bool * gmode)
{
	struct b43_wldev *d;

	list_for_each_entry(d, &wl->devlist, list) {
		if (d->phy.possible_phymodes & phymode) {
			/* Ok, this device supports the PHY-mode.
			 * Now figure out how the gmode bit has to be
			 * set to support it. */
			if (phymode == B43_PHYMODE_A)
				*gmode = 0;
			else
				*gmode = 1;
			*dev = d;

			return 0;
		}
	}

	return -ESRCH;
}

static void b43_put_phy_into_reset(struct b43_wldev *dev)
{
	struct ssb_device *sdev = dev->dev;
	u32 tmslow;

	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmslow &= ~B43_TMSLOW_GMODE;
	tmslow |= B43_TMSLOW_PHYRESET;
	tmslow |= SSB_TMSLOW_FGC;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	msleep(1);

	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmslow &= ~SSB_TMSLOW_FGC;
	tmslow |= B43_TMSLOW_PHYRESET;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	msleep(1);
}

/* Expects wl->mutex locked */
static int b43_switch_phymode(struct b43_wl *wl, unsigned int new_mode)
{
	struct b43_wldev *up_dev;
	struct b43_wldev *down_dev;
	int err;
	bool gmode = 0;
	int prev_status;

	err = find_wldev_for_phymode(wl, new_mode, &up_dev, &gmode);
	if (err) {
		b43err(wl, "Could not find a device for %s-PHY mode\n",
		       phymode_to_string(new_mode));
		return err;
	}
	if ((up_dev == wl->current_dev) &&
	    (!!wl->current_dev->phy.gmode == !!gmode)) {
		/* This device is already running. */
		return 0;
	}
	b43dbg(wl, "Reconfiguring PHYmode to %s-PHY\n",
	       phymode_to_string(new_mode));
	down_dev = wl->current_dev;

	prev_status = b43_status(down_dev);
	/* Shutdown the currently running core. */
	if (prev_status >= B43_STAT_STARTED)
		b43_wireless_core_stop(down_dev);
	if (prev_status >= B43_STAT_INITIALIZED)
		b43_wireless_core_exit(down_dev);

	if (down_dev != up_dev) {
		/* We switch to a different core, so we put PHY into
		 * RESET on the old core. */
		b43_put_phy_into_reset(down_dev);
	}

	/* Now start the new core. */
	up_dev->phy.gmode = gmode;
	if (prev_status >= B43_STAT_INITIALIZED) {
		err = b43_wireless_core_init(up_dev);
		if (err) {
			b43err(wl, "Fatal: Could not initialize device for "
			       "newly selected %s-PHY mode\n",
			       phymode_to_string(new_mode));
			goto init_failure;
		}
	}
	if (prev_status >= B43_STAT_STARTED) {
		err = b43_wireless_core_start(up_dev);
		if (err) {
			b43err(wl, "Fatal: Coult not start device for "
			       "newly selected %s-PHY mode\n",
			       phymode_to_string(new_mode));
			b43_wireless_core_exit(up_dev);
			goto init_failure;
		}
	}
	B43_WARN_ON(b43_status(up_dev) != prev_status);

	wl->current_dev = up_dev;

	return 0;
      init_failure:
	/* Whoops, failed to init the new core. No core is operating now. */
	wl->current_dev = NULL;
	return err;
}

/* Check if the use of the antenna that ieee80211 told us to
 * use is possible. This will fall back to DEFAULT.
 * "antenna_nr" is the antenna identifier we got from ieee80211. */
u8 b43_ieee80211_antenna_sanitize(struct b43_wldev *dev,
				  u8 antenna_nr)
{
	u8 antenna_mask;

	if (antenna_nr == 0) {
		/* Zero means "use default antenna". That's always OK. */
		return 0;
	}

	/* Get the mask of available antennas. */
	if (dev->phy.gmode)
		antenna_mask = dev->dev->bus->sprom.ant_available_bg;
	else
		antenna_mask = dev->dev->bus->sprom.ant_available_a;

	if (!(antenna_mask & (1 << (antenna_nr - 1)))) {
		/* This antenna is not available. Fall back to default. */
		return 0;
	}

	return antenna_nr;
}

static int b43_antenna_from_ieee80211(struct b43_wldev *dev, u8 antenna)
{
	antenna = b43_ieee80211_antenna_sanitize(dev, antenna);
	switch (antenna) {
	case 0:		/* default/diversity */
		return B43_ANTENNA_DEFAULT;
	case 1:		/* Antenna 0 */
		return B43_ANTENNA0;
	case 2:		/* Antenna 1 */
		return B43_ANTENNA1;
	case 3:		/* Antenna 2 */
		return B43_ANTENNA2;
	case 4:		/* Antenna 3 */
		return B43_ANTENNA3;
	default:
		return B43_ANTENNA_DEFAULT;
	}
}

static int b43_op_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	struct b43_phy *phy;
	unsigned long flags;
	unsigned int new_phymode = 0xFFFF;
	int antenna;
	int err = 0;
	u32 savedirqs;

	mutex_lock(&wl->mutex);

	/* Switch the PHY mode (if necessary). */
	switch (conf->phymode) {
	case MODE_IEEE80211A:
		new_phymode = B43_PHYMODE_A;
		break;
	case MODE_IEEE80211B:
		new_phymode = B43_PHYMODE_B;
		break;
	case MODE_IEEE80211G:
		new_phymode = B43_PHYMODE_G;
		break;
	default:
		B43_WARN_ON(1);
	}
	err = b43_switch_phymode(wl, new_phymode);
	if (err)
		goto out_unlock_mutex;
	dev = wl->current_dev;
	phy = &dev->phy;

	/* Disable IRQs while reconfiguring the device.
	 * This makes it possible to drop the spinlock throughout
	 * the reconfiguration process. */
	spin_lock_irqsave(&wl->irq_lock, flags);
	if (b43_status(dev) < B43_STAT_STARTED) {
		spin_unlock_irqrestore(&wl->irq_lock, flags);
		goto out_unlock_mutex;
	}
	savedirqs = b43_interrupt_disable(dev, B43_IRQ_ALL);
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	b43_synchronize_irq(dev);

	/* Switch to the requested channel.
	 * The firmware takes care of races with the TX handler. */
	if (conf->channel_val != phy->channel)
		b43_radio_selectchannel(dev, conf->channel_val, 0);

	/* Enable/Disable ShortSlot timing. */
	if ((!!(conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME)) !=
	    dev->short_slot) {
		B43_WARN_ON(phy->type != B43_PHYTYPE_G);
		if (conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME)
			b43_short_slot_timing_enable(dev);
		else
			b43_short_slot_timing_disable(dev);
	}

	dev->wl->radiotap_enabled = !!(conf->flags & IEEE80211_CONF_RADIOTAP);

	/* Adjust the desired TX power level. */
	if (conf->power_level != 0) {
		if (conf->power_level != phy->power_level) {
			phy->power_level = conf->power_level;
			b43_phy_xmitpower(dev);
		}
	}

	/* Antennas for RX and management frame TX. */
	antenna = b43_antenna_from_ieee80211(dev, conf->antenna_sel_tx);
	b43_mgmtframe_txantenna(dev, antenna);
	antenna = b43_antenna_from_ieee80211(dev, conf->antenna_sel_rx);
	b43_set_rx_antenna(dev, antenna);

	/* Update templates for AP mode. */
	if (b43_is_mode(wl, IEEE80211_IF_TYPE_AP))
		b43_set_beacon_int(dev, conf->beacon_int);

	if (!!conf->radio_enabled != phy->radio_on) {
		if (conf->radio_enabled) {
			b43_radio_turn_on(dev);
			b43info(dev->wl, "Radio turned on by software\n");
			if (!dev->radio_hw_enable) {
				b43info(dev->wl, "The hardware RF-kill button "
					"still turns the radio physically off. "
					"Press the button to turn it on.\n");
			}
		} else {
			b43_radio_turn_off(dev, 0);
			b43info(dev->wl, "Radio turned off by software\n");
		}
	}

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43_interrupt_enable(dev, savedirqs);
	mmiowb();
	spin_unlock_irqrestore(&wl->irq_lock, flags);
      out_unlock_mutex:
	mutex_unlock(&wl->mutex);

	return err;
}

static int b43_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   const u8 *local_addr, const u8 *addr,
			   struct ieee80211_key_conf *key)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	unsigned long flags;
	u8 algorithm;
	u8 index;
	int err;
	DECLARE_MAC_BUF(mac);

	if (modparam_nohwcrypt)
		return -ENOSPC; /* User disabled HW-crypto */

	mutex_lock(&wl->mutex);
	spin_lock_irqsave(&wl->irq_lock, flags);

	dev = wl->current_dev;
	err = -ENODEV;
	if (!dev || b43_status(dev) < B43_STAT_INITIALIZED)
		goto out_unlock;

	err = -EINVAL;
	switch (key->alg) {
	case ALG_WEP:
		if (key->keylen == 5)
			algorithm = B43_SEC_ALGO_WEP40;
		else
			algorithm = B43_SEC_ALGO_WEP104;
		break;
	case ALG_TKIP:
		algorithm = B43_SEC_ALGO_TKIP;
		break;
	case ALG_CCMP:
		algorithm = B43_SEC_ALGO_AES;
		break;
	default:
		B43_WARN_ON(1);
		goto out_unlock;
	}
	index = (u8) (key->keyidx);
	if (index > 3)
		goto out_unlock;

	switch (cmd) {
	case SET_KEY:
		if (algorithm == B43_SEC_ALGO_TKIP) {
			/* FIXME: No TKIP hardware encryption for now. */
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		if (is_broadcast_ether_addr(addr)) {
			/* addr is FF:FF:FF:FF:FF:FF for default keys */
			err = b43_key_write(dev, index, algorithm,
					    key->key, key->keylen, NULL, key);
		} else {
			/*
			 * either pairwise key or address is 00:00:00:00:00:00
			 * for transmit-only keys
			 */
			err = b43_key_write(dev, -1, algorithm,
					    key->key, key->keylen, addr, key);
		}
		if (err)
			goto out_unlock;

		if (algorithm == B43_SEC_ALGO_WEP40 ||
		    algorithm == B43_SEC_ALGO_WEP104) {
			b43_hf_write(dev, b43_hf_read(dev) | B43_HF_USEDEFKEYS);
		} else {
			b43_hf_write(dev,
				     b43_hf_read(dev) & ~B43_HF_USEDEFKEYS);
		}
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		break;
	case DISABLE_KEY: {
		err = b43_key_clear(dev, key->hw_key_idx);
		if (err)
			goto out_unlock;
		break;
	}
	default:
		B43_WARN_ON(1);
	}
out_unlock:
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	mutex_unlock(&wl->mutex);
	if (!err) {
		b43dbg(wl, "%s hardware based encryption for keyidx: %d, "
		       "mac: %s\n",
		       cmd == SET_KEY ? "Using" : "Disabling", key->keyidx,
		       print_mac(mac, addr));
	}
	return err;
}

static void b43_op_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed, unsigned int *fflags,
				    int mc_count, struct dev_addr_list *mc_list)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	unsigned long flags;

	if (!dev) {
		*fflags = 0;
		return;
	}

	spin_lock_irqsave(&wl->irq_lock, flags);
	*fflags &= FIF_PROMISC_IN_BSS |
		  FIF_ALLMULTI |
		  FIF_FCSFAIL |
		  FIF_PLCPFAIL |
		  FIF_CONTROL |
		  FIF_OTHER_BSS |
		  FIF_BCN_PRBRESP_PROMISC;

	changed &= FIF_PROMISC_IN_BSS |
		   FIF_ALLMULTI |
		   FIF_FCSFAIL |
		   FIF_PLCPFAIL |
		   FIF_CONTROL |
		   FIF_OTHER_BSS |
		   FIF_BCN_PRBRESP_PROMISC;

	wl->filter_flags = *fflags;

	if (changed && b43_status(dev) >= B43_STAT_INITIALIZED)
		b43_adjust_opmode(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);
}

static int b43_op_config_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_if_conf *conf)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	unsigned long flags;

	if (!dev)
		return -ENODEV;
	mutex_lock(&wl->mutex);
	spin_lock_irqsave(&wl->irq_lock, flags);
	B43_WARN_ON(wl->vif != vif);
	if (conf->bssid)
		memcpy(wl->bssid, conf->bssid, ETH_ALEN);
	else
		memset(wl->bssid, 0, ETH_ALEN);
	if (b43_status(dev) >= B43_STAT_INITIALIZED) {
		if (b43_is_mode(wl, IEEE80211_IF_TYPE_AP)) {
			B43_WARN_ON(conf->type != IEEE80211_IF_TYPE_AP);
			b43_set_ssid(dev, conf->ssid, conf->ssid_len);
			if (conf->beacon)
				b43_update_templates(wl, conf->beacon);
		}
		b43_write_mac_bssid_templates(dev);
	}
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	mutex_unlock(&wl->mutex);

	return 0;
}

/* Locking: wl->mutex */
static void b43_wireless_core_stop(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	unsigned long flags;

	if (b43_status(dev) < B43_STAT_STARTED)
		return;

	/* Disable and sync interrupts. We must do this before than
	 * setting the status to INITIALIZED, as the interrupt handler
	 * won't care about IRQs then. */
	spin_lock_irqsave(&wl->irq_lock, flags);
	dev->irq_savedstate = b43_interrupt_disable(dev, B43_IRQ_ALL);
	b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);	/* flush */
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	b43_synchronize_irq(dev);

	b43_set_status(dev, B43_STAT_INITIALIZED);

	mutex_unlock(&wl->mutex);
	/* Must unlock as it would otherwise deadlock. No races here.
	 * Cancel the possibly running self-rearming periodic work. */
	cancel_delayed_work_sync(&dev->periodic_work);
	mutex_lock(&wl->mutex);

	ieee80211_stop_queues(wl->hw);	//FIXME this could cause a deadlock, as mac80211 seems buggy.

	b43_mac_suspend(dev);
	free_irq(dev->dev->irq, dev);
	b43dbg(wl, "Wireless interface stopped\n");
}

/* Locking: wl->mutex */
static int b43_wireless_core_start(struct b43_wldev *dev)
{
	int err;

	B43_WARN_ON(b43_status(dev) != B43_STAT_INITIALIZED);

	drain_txstatus_queue(dev);
	err = request_irq(dev->dev->irq, b43_interrupt_handler,
			  IRQF_SHARED, KBUILD_MODNAME, dev);
	if (err) {
		b43err(dev->wl, "Cannot request IRQ-%d\n", dev->dev->irq);
		goto out;
	}

	/* We are ready to run. */
	b43_set_status(dev, B43_STAT_STARTED);

	/* Start data flow (TX/RX). */
	b43_mac_enable(dev);
	b43_interrupt_enable(dev, dev->irq_savedstate);
	ieee80211_start_queues(dev->wl->hw);

	/* Start maintainance work */
	b43_periodic_tasks_setup(dev);

	b43dbg(dev->wl, "Wireless interface started\n");
      out:
	return err;
}

/* Get PHY and RADIO versioning numbers */
static int b43_phy_versioning(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u32 tmp;
	u8 analog_type;
	u8 phy_type;
	u8 phy_rev;
	u16 radio_manuf;
	u16 radio_ver;
	u16 radio_rev;
	int unsupported = 0;

	/* Get PHY versioning */
	tmp = b43_read16(dev, B43_MMIO_PHY_VER);
	analog_type = (tmp & B43_PHYVER_ANALOG) >> B43_PHYVER_ANALOG_SHIFT;
	phy_type = (tmp & B43_PHYVER_TYPE) >> B43_PHYVER_TYPE_SHIFT;
	phy_rev = (tmp & B43_PHYVER_VERSION);
	switch (phy_type) {
	case B43_PHYTYPE_A:
		if (phy_rev >= 4)
			unsupported = 1;
		break;
	case B43_PHYTYPE_B:
		if (phy_rev != 2 && phy_rev != 4 && phy_rev != 6
		    && phy_rev != 7)
			unsupported = 1;
		break;
	case B43_PHYTYPE_G:
		if (phy_rev > 9)
			unsupported = 1;
		break;
#ifdef CONFIG_B43_NPHY
	case B43_PHYTYPE_N:
		if (phy_rev > 1)
			unsupported = 1;
		break;
#endif
	default:
		unsupported = 1;
	};
	if (unsupported) {
		b43err(dev->wl, "FOUND UNSUPPORTED PHY "
		       "(Analog %u, Type %u, Revision %u)\n",
		       analog_type, phy_type, phy_rev);
		return -EOPNOTSUPP;
	}
	b43dbg(dev->wl, "Found PHY: Analog %u, Type %u, Revision %u\n",
	       analog_type, phy_type, phy_rev);

	/* Get RADIO versioning */
	if (dev->dev->bus->chip_id == 0x4317) {
		if (dev->dev->bus->chip_rev == 0)
			tmp = 0x3205017F;
		else if (dev->dev->bus->chip_rev == 1)
			tmp = 0x4205017F;
		else
			tmp = 0x5205017F;
	} else {
		b43_write16(dev, B43_MMIO_RADIO_CONTROL, B43_RADIOCTL_ID);
		tmp = b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
		b43_write16(dev, B43_MMIO_RADIO_CONTROL, B43_RADIOCTL_ID);
		tmp |= (u32)b43_read16(dev, B43_MMIO_RADIO_DATA_HIGH) << 16;
	}
	radio_manuf = (tmp & 0x00000FFF);
	radio_ver = (tmp & 0x0FFFF000) >> 12;
	radio_rev = (tmp & 0xF0000000) >> 28;
	if (radio_manuf != 0x17F /* Broadcom */)
		unsupported = 1;
	switch (phy_type) {
	case B43_PHYTYPE_A:
		if (radio_ver != 0x2060)
			unsupported = 1;
		if (radio_rev != 1)
			unsupported = 1;
		if (radio_manuf != 0x17F)
			unsupported = 1;
		break;
	case B43_PHYTYPE_B:
		if ((radio_ver & 0xFFF0) != 0x2050)
			unsupported = 1;
		break;
	case B43_PHYTYPE_G:
		if (radio_ver != 0x2050)
			unsupported = 1;
		break;
	case B43_PHYTYPE_N:
		if (radio_ver != 0x2055)
			unsupported = 1;
		break;
	default:
		B43_WARN_ON(1);
	}
	if (unsupported) {
		b43err(dev->wl, "FOUND UNSUPPORTED RADIO "
		       "(Manuf 0x%X, Version 0x%X, Revision %u)\n",
		       radio_manuf, radio_ver, radio_rev);
		return -EOPNOTSUPP;
	}
	b43dbg(dev->wl, "Found Radio: Manuf 0x%X, Version 0x%X, Revision %u\n",
	       radio_manuf, radio_ver, radio_rev);

	phy->radio_manuf = radio_manuf;
	phy->radio_ver = radio_ver;
	phy->radio_rev = radio_rev;

	phy->analog = analog_type;
	phy->type = phy_type;
	phy->rev = phy_rev;

	return 0;
}

static void setup_struct_phy_for_init(struct b43_wldev *dev,
				      struct b43_phy *phy)
{
	struct b43_txpower_lo_control *lo;
	int i;

	memset(phy->minlowsig, 0xFF, sizeof(phy->minlowsig));
	memset(phy->minlowsigpos, 0, sizeof(phy->minlowsigpos));

	phy->aci_enable = 0;
	phy->aci_wlan_automatic = 0;
	phy->aci_hw_rssi = 0;

	phy->radio_off_context.valid = 0;

	lo = phy->lo_control;
	if (lo) {
		memset(lo, 0, sizeof(*(phy->lo_control)));
		lo->rebuild = 1;
		lo->tx_bias = 0xFF;
	}
	phy->max_lb_gain = 0;
	phy->trsw_rx_gain = 0;
	phy->txpwr_offset = 0;

	/* NRSSI */
	phy->nrssislope = 0;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi); i++)
		phy->nrssi[i] = -1000;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi_lt); i++)
		phy->nrssi_lt[i] = i;

	phy->lofcal = 0xFFFF;
	phy->initval = 0xFFFF;

	phy->interfmode = B43_INTERFMODE_NONE;
	phy->channel = 0xFF;

	phy->hardware_power_control = !!modparam_hwpctl;

	/* PHY TX errors counter. */
	atomic_set(&phy->txerr_cnt, B43_PHY_TX_BADNESS_LIMIT);

	/* OFDM-table address caching. */
	phy->ofdmtab_addr_direction = B43_OFDMTAB_DIRECTION_UNKNOWN;
}

static void setup_struct_wldev_for_init(struct b43_wldev *dev)
{
	dev->dfq_valid = 0;

	/* Assume the radio is enabled. If it's not enabled, the state will
	 * immediately get fixed on the first periodic work run. */
	dev->radio_hw_enable = 1;

	/* Stats */
	memset(&dev->stats, 0, sizeof(dev->stats));

	setup_struct_phy_for_init(dev, &dev->phy);

	/* IRQ related flags */
	dev->irq_reason = 0;
	memset(dev->dma_reason, 0, sizeof(dev->dma_reason));
	dev->irq_savedstate = B43_IRQ_MASKTEMPLATE;

	dev->mac_suspended = 1;

	/* Noise calculation context */
	memset(&dev->noisecalc, 0, sizeof(dev->noisecalc));
}

static void b43_bluetooth_coext_enable(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = &dev->dev->bus->sprom;
	u32 hf;

	if (!(sprom->boardflags_lo & B43_BFL_BTCOEXIST))
		return;
	if (dev->phy.type != B43_PHYTYPE_B && !dev->phy.gmode)
		return;

	hf = b43_hf_read(dev);
	if (sprom->boardflags_lo & B43_BFL_BTCMOD)
		hf |= B43_HF_BTCOEXALT;
	else
		hf |= B43_HF_BTCOEX;
	b43_hf_write(dev, hf);
	//TODO
}

static void b43_bluetooth_coext_disable(struct b43_wldev *dev)
{				//TODO
}

static void b43_imcfglo_timeouts_workaround(struct b43_wldev *dev)
{
#ifdef CONFIG_SSB_DRIVER_PCICORE
	struct ssb_bus *bus = dev->dev->bus;
	u32 tmp;

	if (bus->pcicore.dev &&
	    bus->pcicore.dev->id.coreid == SSB_DEV_PCI &&
	    bus->pcicore.dev->id.revision <= 5) {
		/* IMCFGLO timeouts workaround. */
		tmp = ssb_read32(dev->dev, SSB_IMCFGLO);
		tmp &= ~SSB_IMCFGLO_REQTO;
		tmp &= ~SSB_IMCFGLO_SERTO;
		switch (bus->bustype) {
		case SSB_BUSTYPE_PCI:
		case SSB_BUSTYPE_PCMCIA:
			tmp |= 0x32;
			break;
		case SSB_BUSTYPE_SSB:
			tmp |= 0x53;
			break;
		}
		ssb_write32(dev->dev, SSB_IMCFGLO, tmp);
	}
#endif /* CONFIG_SSB_DRIVER_PCICORE */
}

/* Write the short and long frame retry limit values. */
static void b43_set_retry_limits(struct b43_wldev *dev,
				 unsigned int short_retry,
				 unsigned int long_retry)
{
	/* The retry limit is a 4-bit counter. Enforce this to avoid overflowing
	 * the chip-internal counter. */
	short_retry = min(short_retry, (unsigned int)0xF);
	long_retry = min(long_retry, (unsigned int)0xF);

	b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_SRLIMIT,
			short_retry);
	b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_LRLIMIT,
			long_retry);
}

/* Shutdown a wireless core */
/* Locking: wl->mutex */
static void b43_wireless_core_exit(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u32 macctl;

	B43_WARN_ON(b43_status(dev) > B43_STAT_INITIALIZED);
	if (b43_status(dev) != B43_STAT_INITIALIZED)
		return;
	b43_set_status(dev, B43_STAT_UNINIT);

	/* Stop the microcode PSM. */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_PSM_RUN;
	macctl |= B43_MACCTL_PSM_JMP0;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);

	b43_leds_exit(dev);
	b43_rng_exit(dev->wl);
	b43_dma_free(dev);
	b43_chip_exit(dev);
	b43_radio_turn_off(dev, 1);
	b43_switch_analog(dev, 0);
	if (phy->dyn_tssi_tbl)
		kfree(phy->tssi2dbm);
	kfree(phy->lo_control);
	phy->lo_control = NULL;
	if (dev->wl->current_beacon) {
		dev_kfree_skb_any(dev->wl->current_beacon);
		dev->wl->current_beacon = NULL;
	}

	ssb_device_disable(dev->dev, 0);
	ssb_bus_may_powerdown(dev->dev->bus);
}

/* Initialize a wireless core */
static int b43_wireless_core_init(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct ssb_bus *bus = dev->dev->bus;
	struct ssb_sprom *sprom = &bus->sprom;
	struct b43_phy *phy = &dev->phy;
	int err;
	u32 hf, tmp;

	B43_WARN_ON(b43_status(dev) != B43_STAT_UNINIT);

	err = ssb_bus_powerup(bus, 0);
	if (err)
		goto out;
	if (!ssb_device_is_enabled(dev->dev)) {
		tmp = phy->gmode ? B43_TMSLOW_GMODE : 0;
		b43_wireless_core_reset(dev, tmp);
	}

	if ((phy->type == B43_PHYTYPE_B) || (phy->type == B43_PHYTYPE_G)) {
		phy->lo_control =
		    kzalloc(sizeof(*(phy->lo_control)), GFP_KERNEL);
		if (!phy->lo_control) {
			err = -ENOMEM;
			goto err_busdown;
		}
	}
	setup_struct_wldev_for_init(dev);

	err = b43_phy_init_tssi2dbm_table(dev);
	if (err)
		goto err_kfree_lo_control;

	/* Enable IRQ routing to this device. */
	ssb_pcicore_dev_irqvecs_enable(&bus->pcicore, dev->dev);

	b43_imcfglo_timeouts_workaround(dev);
	b43_bluetooth_coext_disable(dev);
	b43_phy_early_init(dev);
	err = b43_chip_init(dev);
	if (err)
		goto err_kfree_tssitbl;
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_WLCOREREV, dev->dev->id.revision);
	hf = b43_hf_read(dev);
	if (phy->type == B43_PHYTYPE_G) {
		hf |= B43_HF_SYMW;
		if (phy->rev == 1)
			hf |= B43_HF_GDCW;
		if (sprom->boardflags_lo & B43_BFL_PACTRL)
			hf |= B43_HF_OFDMPABOOST;
	} else if (phy->type == B43_PHYTYPE_B) {
		hf |= B43_HF_SYMW;
		if (phy->rev >= 2 && phy->radio_ver == 0x2050)
			hf &= ~B43_HF_GDCW;
	}
	b43_hf_write(dev, hf);

	b43_set_retry_limits(dev, B43_DEFAULT_SHORT_RETRY_LIMIT,
			     B43_DEFAULT_LONG_RETRY_LIMIT);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_SFFBLIM, 3);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_LFFBLIM, 2);

	/* Disable sending probe responses from firmware.
	 * Setting the MaxTime to one usec will always trigger
	 * a timeout, so we never send any probe resp.
	 * A timeout of zero is infinite. */
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_PRMAXTIME, 1);

	b43_rate_memory_init(dev);

	/* Minimum Contention Window */
	if (phy->type == B43_PHYTYPE_B) {
		b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MINCONT, 0x1F);
	} else {
		b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MINCONT, 0xF);
	}
	/* Maximum Contention Window */
	b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MAXCONT, 0x3FF);

	err = b43_dma_init(dev);
	if (err)
		goto err_chip_exit;
	b43_qos_init(dev);

//FIXME
#if 1
	b43_write16(dev, 0x0612, 0x0050);
	b43_shm_write16(dev, B43_SHM_SHARED, 0x0416, 0x0050);
	b43_shm_write16(dev, B43_SHM_SHARED, 0x0414, 0x01F4);
#endif

	b43_bluetooth_coext_enable(dev);

	ssb_bus_powerup(bus, 1);	/* Enable dynamic PCTL */
	memset(wl->bssid, 0, ETH_ALEN);
	memset(wl->mac_addr, 0, ETH_ALEN);
	b43_upload_card_macaddress(dev);
	b43_security_init(dev);
	b43_rng_init(wl);

	b43_set_status(dev, B43_STAT_INITIALIZED);

	b43_leds_init(dev);
out:
	return err;

      err_chip_exit:
	b43_chip_exit(dev);
      err_kfree_tssitbl:
	if (phy->dyn_tssi_tbl)
		kfree(phy->tssi2dbm);
      err_kfree_lo_control:
	kfree(phy->lo_control);
	phy->lo_control = NULL;
      err_busdown:
	ssb_bus_may_powerdown(bus);
	B43_WARN_ON(b43_status(dev) != B43_STAT_UNINIT);
	return err;
}

static int b43_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	unsigned long flags;
	int err = -EOPNOTSUPP;

	/* TODO: allow WDS/AP devices to coexist */

	if (conf->type != IEEE80211_IF_TYPE_AP &&
	    conf->type != IEEE80211_IF_TYPE_STA &&
	    conf->type != IEEE80211_IF_TYPE_WDS &&
	    conf->type != IEEE80211_IF_TYPE_IBSS)
		return -EOPNOTSUPP;

	mutex_lock(&wl->mutex);
	if (wl->operating)
		goto out_mutex_unlock;

	b43dbg(wl, "Adding Interface type %d\n", conf->type);

	dev = wl->current_dev;
	wl->operating = 1;
	wl->vif = conf->vif;
	wl->if_type = conf->type;
	memcpy(wl->mac_addr, conf->mac_addr, ETH_ALEN);

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43_adjust_opmode(dev);
	b43_upload_card_macaddress(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	err = 0;
 out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static void b43_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_if_init_conf *conf)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	unsigned long flags;

	b43dbg(wl, "Removing Interface type %d\n", conf->type);

	mutex_lock(&wl->mutex);

	B43_WARN_ON(!wl->operating);
	B43_WARN_ON(wl->vif != conf->vif);
	wl->vif = NULL;

	wl->operating = 0;

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43_adjust_opmode(dev);
	memset(wl->mac_addr, 0, ETH_ALEN);
	b43_upload_card_macaddress(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	mutex_unlock(&wl->mutex);
}

static int b43_op_start(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	int did_init = 0;
	int err = 0;

	/* First register RFkill.
	 * LEDs that are registered later depend on it. */
	b43_rfkill_init(dev);

	mutex_lock(&wl->mutex);

	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		err = b43_wireless_core_init(dev);
		if (err)
			goto out_mutex_unlock;
		did_init = 1;
	}

	if (b43_status(dev) < B43_STAT_STARTED) {
		err = b43_wireless_core_start(dev);
		if (err) {
			if (did_init)
				b43_wireless_core_exit(dev);
			goto out_mutex_unlock;
		}
	}

 out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static void b43_op_stop(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;

	b43_rfkill_exit(dev);

	mutex_lock(&wl->mutex);
	if (b43_status(dev) >= B43_STAT_STARTED)
		b43_wireless_core_stop(dev);
	b43_wireless_core_exit(dev);
	mutex_unlock(&wl->mutex);
}

static int b43_op_set_retry_limit(struct ieee80211_hw *hw,
				  u32 short_retry_limit, u32 long_retry_limit)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	int err = 0;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (unlikely(!dev || (b43_status(dev) < B43_STAT_INITIALIZED))) {
		err = -ENODEV;
		goto out_unlock;
	}
	b43_set_retry_limits(dev, short_retry_limit, long_retry_limit);
out_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static int b43_op_beacon_set_tim(struct ieee80211_hw *hw, int aid, int set)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct sk_buff *beacon;
	unsigned long flags;

	/* We could modify the existing beacon and set the aid bit in
	 * the TIM field, but that would probably require resizing and
	 * moving of data within the beacon template.
	 * Simply request a new beacon and let mac80211 do the hard work. */
	beacon = ieee80211_beacon_get(hw, wl->vif, NULL);
	if (unlikely(!beacon))
		return -ENOMEM;
	spin_lock_irqsave(&wl->irq_lock, flags);
	b43_update_templates(wl, beacon);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return 0;
}

static int b43_op_ibss_beacon_update(struct ieee80211_hw *hw,
				     struct sk_buff *beacon,
				     struct ieee80211_tx_control *ctl)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	unsigned long flags;

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43_update_templates(wl, beacon);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return 0;
}

static const struct ieee80211_ops b43_hw_ops = {
	.tx			= b43_op_tx,
	.conf_tx		= b43_op_conf_tx,
	.add_interface		= b43_op_add_interface,
	.remove_interface	= b43_op_remove_interface,
	.config			= b43_op_config,
	.config_interface	= b43_op_config_interface,
	.configure_filter	= b43_op_configure_filter,
	.set_key		= b43_op_set_key,
	.get_stats		= b43_op_get_stats,
	.get_tx_stats		= b43_op_get_tx_stats,
	.start			= b43_op_start,
	.stop			= b43_op_stop,
	.set_retry_limit	= b43_op_set_retry_limit,
	.set_tim		= b43_op_beacon_set_tim,
	.beacon_update		= b43_op_ibss_beacon_update,
};

/* Hard-reset the chip. Do not call this directly.
 * Use b43_controller_restart()
 */
static void b43_chip_reset(struct work_struct *work)
{
	struct b43_wldev *dev =
	    container_of(work, struct b43_wldev, restart_work);
	struct b43_wl *wl = dev->wl;
	int err = 0;
	int prev_status;

	mutex_lock(&wl->mutex);

	prev_status = b43_status(dev);
	/* Bring the device down... */
	if (prev_status >= B43_STAT_STARTED)
		b43_wireless_core_stop(dev);
	if (prev_status >= B43_STAT_INITIALIZED)
		b43_wireless_core_exit(dev);

	/* ...and up again. */
	if (prev_status >= B43_STAT_INITIALIZED) {
		err = b43_wireless_core_init(dev);
		if (err)
			goto out;
	}
	if (prev_status >= B43_STAT_STARTED) {
		err = b43_wireless_core_start(dev);
		if (err) {
			b43_wireless_core_exit(dev);
			goto out;
		}
	}
      out:
	mutex_unlock(&wl->mutex);
	if (err)
		b43err(wl, "Controller restart FAILED\n");
	else
		b43info(wl, "Controller restarted\n");
}

static int b43_setup_modes(struct b43_wldev *dev,
			   bool have_2ghz_phy, bool have_5ghz_phy)
{
	struct ieee80211_hw *hw = dev->wl->hw;
	struct ieee80211_hw_mode *mode;
	struct b43_phy *phy = &dev->phy;
	int err;

	/* XXX: This function will go away soon, when mac80211
	 *      band stuff is rewritten. So this is just a hack.
	 *      For now we always claim GPHY mode, as there is no
	 *      support for NPHY and APHY in the device, yet.
	 *      This assumption is OK, as any B, N or A PHY will already
	 *      have died a horrible sanity check death earlier. */

	mode = &phy->hwmodes[0];
	mode->mode = MODE_IEEE80211G;
	mode->num_channels = b43_2ghz_chantable_size;
	mode->channels = b43_2ghz_chantable;
	mode->num_rates = b43_g_ratetable_size;
	mode->rates = b43_g_ratetable;
	err = ieee80211_register_hwmode(hw, mode);
	if (err)
		return err;
	phy->possible_phymodes |= B43_PHYMODE_G;

	return 0;
}

static void b43_wireless_core_detach(struct b43_wldev *dev)
{
	/* We release firmware that late to not be required to re-request
	 * is all the time when we reinit the core. */
	b43_release_firmware(dev);
}

static int b43_wireless_core_attach(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct ssb_bus *bus = dev->dev->bus;
	struct pci_dev *pdev = bus->host_pci;
	int err;
	bool have_2ghz_phy = 0, have_5ghz_phy = 0;
	u32 tmp;

	/* Do NOT do any device initialization here.
	 * Do it in wireless_core_init() instead.
	 * This function is for gathering basic information about the HW, only.
	 * Also some structs may be set up here. But most likely you want to have
	 * that in core_init(), too.
	 */

	err = ssb_bus_powerup(bus, 0);
	if (err) {
		b43err(wl, "Bus powerup failed\n");
		goto out;
	}
	/* Get the PHY type. */
	if (dev->dev->id.revision >= 5) {
		u32 tmshigh;

		tmshigh = ssb_read32(dev->dev, SSB_TMSHIGH);
		have_2ghz_phy = !!(tmshigh & B43_TMSHIGH_HAVE_2GHZ_PHY);
		have_5ghz_phy = !!(tmshigh & B43_TMSHIGH_HAVE_5GHZ_PHY);
	} else
		B43_WARN_ON(1);

	dev->phy.gmode = have_2ghz_phy;
	tmp = dev->phy.gmode ? B43_TMSLOW_GMODE : 0;
	b43_wireless_core_reset(dev, tmp);

	err = b43_phy_versioning(dev);
	if (err)
		goto err_powerdown;
	/* Check if this device supports multiband. */
	if (!pdev ||
	    (pdev->device != 0x4312 &&
	     pdev->device != 0x4319 && pdev->device != 0x4324)) {
		/* No multiband support. */
		have_2ghz_phy = 0;
		have_5ghz_phy = 0;
		switch (dev->phy.type) {
		case B43_PHYTYPE_A:
			have_5ghz_phy = 1;
			break;
		case B43_PHYTYPE_G:
		case B43_PHYTYPE_N:
			have_2ghz_phy = 1;
			break;
		default:
			B43_WARN_ON(1);
		}
	}
	if (dev->phy.type == B43_PHYTYPE_A) {
		/* FIXME */
		b43err(wl, "IEEE 802.11a devices are unsupported\n");
		err = -EOPNOTSUPP;
		goto err_powerdown;
	}
	dev->phy.gmode = have_2ghz_phy;
	tmp = dev->phy.gmode ? B43_TMSLOW_GMODE : 0;
	b43_wireless_core_reset(dev, tmp);

	err = b43_validate_chipaccess(dev);
	if (err)
		goto err_powerdown;
	err = b43_setup_modes(dev, have_2ghz_phy, have_5ghz_phy);
	if (err)
		goto err_powerdown;

	/* Now set some default "current_dev" */
	if (!wl->current_dev)
		wl->current_dev = dev;
	INIT_WORK(&dev->restart_work, b43_chip_reset);

	b43_radio_turn_off(dev, 1);
	b43_switch_analog(dev, 0);
	ssb_device_disable(dev->dev, 0);
	ssb_bus_may_powerdown(bus);

out:
	return err;

err_powerdown:
	ssb_bus_may_powerdown(bus);
	return err;
}

static void b43_one_core_detach(struct ssb_device *dev)
{
	struct b43_wldev *wldev;
	struct b43_wl *wl;

	wldev = ssb_get_drvdata(dev);
	wl = wldev->wl;
	cancel_work_sync(&wldev->restart_work);
	b43_debugfs_remove_device(wldev);
	b43_wireless_core_detach(wldev);
	list_del(&wldev->list);
	wl->nr_devs--;
	ssb_set_drvdata(dev, NULL);
	kfree(wldev);
}

static int b43_one_core_attach(struct ssb_device *dev, struct b43_wl *wl)
{
	struct b43_wldev *wldev;
	struct pci_dev *pdev;
	int err = -ENOMEM;

	if (!list_empty(&wl->devlist)) {
		/* We are not the first core on this chip. */
		pdev = dev->bus->host_pci;
		/* Only special chips support more than one wireless
		 * core, although some of the other chips have more than
		 * one wireless core as well. Check for this and
		 * bail out early.
		 */
		if (!pdev ||
		    ((pdev->device != 0x4321) &&
		     (pdev->device != 0x4313) && (pdev->device != 0x431A))) {
			b43dbg(wl, "Ignoring unconnected 802.11 core\n");
			return -ENODEV;
		}
	}

	wldev = kzalloc(sizeof(*wldev), GFP_KERNEL);
	if (!wldev)
		goto out;

	wldev->dev = dev;
	wldev->wl = wl;
	b43_set_status(wldev, B43_STAT_UNINIT);
	wldev->bad_frames_preempt = modparam_bad_frames_preempt;
	tasklet_init(&wldev->isr_tasklet,
		     (void (*)(unsigned long))b43_interrupt_tasklet,
		     (unsigned long)wldev);
	INIT_LIST_HEAD(&wldev->list);

	err = b43_wireless_core_attach(wldev);
	if (err)
		goto err_kfree_wldev;

	list_add(&wldev->list, &wl->devlist);
	wl->nr_devs++;
	ssb_set_drvdata(dev, wldev);
	b43_debugfs_add_device(wldev);

      out:
	return err;

      err_kfree_wldev:
	kfree(wldev);
	return err;
}

static void b43_sprom_fixup(struct ssb_bus *bus)
{
	/* boardflags workarounds */
	if (bus->boardinfo.vendor == SSB_BOARDVENDOR_DELL &&
	    bus->chip_id == 0x4301 && bus->boardinfo.rev == 0x74)
		bus->sprom.boardflags_lo |= B43_BFL_BTCOEXIST;
	if (bus->boardinfo.vendor == PCI_VENDOR_ID_APPLE &&
	    bus->boardinfo.type == 0x4E && bus->boardinfo.rev > 0x40)
		bus->sprom.boardflags_lo |= B43_BFL_PACTRL;
}

static void b43_wireless_exit(struct ssb_device *dev, struct b43_wl *wl)
{
	struct ieee80211_hw *hw = wl->hw;

	ssb_set_devtypedata(dev, NULL);
	ieee80211_free_hw(hw);
}

static int b43_wireless_init(struct ssb_device *dev)
{
	struct ssb_sprom *sprom = &dev->bus->sprom;
	struct ieee80211_hw *hw;
	struct b43_wl *wl;
	int err = -ENOMEM;

	b43_sprom_fixup(dev->bus);

	hw = ieee80211_alloc_hw(sizeof(*wl), &b43_hw_ops);
	if (!hw) {
		b43err(NULL, "Could not allocate ieee80211 device\n");
		goto out;
	}

	/* fill hw info */
	hw->flags = IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE |
		    IEEE80211_HW_RX_INCLUDES_FCS;
	hw->max_signal = 100;
	hw->max_rssi = -110;
	hw->max_noise = -110;
	hw->queues = 1;		/* FIXME: hardware has more queues */
	SET_IEEE80211_DEV(hw, dev->dev);
	if (is_valid_ether_addr(sprom->et1mac))
		SET_IEEE80211_PERM_ADDR(hw, sprom->et1mac);
	else
		SET_IEEE80211_PERM_ADDR(hw, sprom->il0mac);

	/* Get and initialize struct b43_wl */
	wl = hw_to_b43_wl(hw);
	memset(wl, 0, sizeof(*wl));
	wl->hw = hw;
	spin_lock_init(&wl->irq_lock);
	spin_lock_init(&wl->leds_lock);
	spin_lock_init(&wl->shm_lock);
	mutex_init(&wl->mutex);
	INIT_LIST_HEAD(&wl->devlist);

	ssb_set_devtypedata(dev, wl);
	b43info(wl, "Broadcom %04X WLAN found\n", dev->bus->chip_id);
	err = 0;
      out:
	return err;
}

static int b43_probe(struct ssb_device *dev, const struct ssb_device_id *id)
{
	struct b43_wl *wl;
	int err;
	int first = 0;

	wl = ssb_get_devtypedata(dev);
	if (!wl) {
		/* Probing the first core. Must setup common struct b43_wl */
		first = 1;
		err = b43_wireless_init(dev);
		if (err)
			goto out;
		wl = ssb_get_devtypedata(dev);
		B43_WARN_ON(!wl);
	}
	err = b43_one_core_attach(dev, wl);
	if (err)
		goto err_wireless_exit;

	if (first) {
		err = ieee80211_register_hw(wl->hw);
		if (err)
			goto err_one_core_detach;
	}

      out:
	return err;

      err_one_core_detach:
	b43_one_core_detach(dev);
      err_wireless_exit:
	if (first)
		b43_wireless_exit(dev, wl);
	return err;
}

static void b43_remove(struct ssb_device *dev)
{
	struct b43_wl *wl = ssb_get_devtypedata(dev);
	struct b43_wldev *wldev = ssb_get_drvdata(dev);

	B43_WARN_ON(!wl);
	if (wl->current_dev == wldev)
		ieee80211_unregister_hw(wl->hw);

	b43_one_core_detach(dev);

	if (list_empty(&wl->devlist)) {
		/* Last core on the chip unregistered.
		 * We can destroy common struct b43_wl.
		 */
		b43_wireless_exit(dev, wl);
	}
}

/* Perform a hardware reset. This can be called from any context. */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	/* Must avoid requeueing, if we are in shutdown. */
	if (b43_status(dev) < B43_STAT_INITIALIZED)
		return;
	b43info(dev->wl, "Controller RESET (%s) ...\n", reason);
	queue_work(dev->wl->hw->workqueue, &dev->restart_work);
}

#ifdef CONFIG_PM

static int b43_suspend(struct ssb_device *dev, pm_message_t state)
{
	struct b43_wldev *wldev = ssb_get_drvdata(dev);
	struct b43_wl *wl = wldev->wl;

	b43dbg(wl, "Suspending...\n");

	mutex_lock(&wl->mutex);
	wldev->suspend_init_status = b43_status(wldev);
	if (wldev->suspend_init_status >= B43_STAT_STARTED)
		b43_wireless_core_stop(wldev);
	if (wldev->suspend_init_status >= B43_STAT_INITIALIZED)
		b43_wireless_core_exit(wldev);
	mutex_unlock(&wl->mutex);

	b43dbg(wl, "Device suspended.\n");

	return 0;
}

static int b43_resume(struct ssb_device *dev)
{
	struct b43_wldev *wldev = ssb_get_drvdata(dev);
	struct b43_wl *wl = wldev->wl;
	int err = 0;

	b43dbg(wl, "Resuming...\n");

	mutex_lock(&wl->mutex);
	if (wldev->suspend_init_status >= B43_STAT_INITIALIZED) {
		err = b43_wireless_core_init(wldev);
		if (err) {
			b43err(wl, "Resume failed at core init\n");
			goto out;
		}
	}
	if (wldev->suspend_init_status >= B43_STAT_STARTED) {
		err = b43_wireless_core_start(wldev);
		if (err) {
			b43_wireless_core_exit(wldev);
			b43err(wl, "Resume failed at core start\n");
			goto out;
		}
	}
	mutex_unlock(&wl->mutex);

	b43dbg(wl, "Device resumed.\n");
      out:
	return err;
}

#else /* CONFIG_PM */
# define b43_suspend	NULL
# define b43_resume	NULL
#endif /* CONFIG_PM */

static struct ssb_driver b43_ssb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= b43_ssb_tbl,
	.probe		= b43_probe,
	.remove		= b43_remove,
	.suspend	= b43_suspend,
	.resume		= b43_resume,
};

static int __init b43_init(void)
{
	int err;

	b43_debugfs_init();
	err = b43_pcmcia_init();
	if (err)
		goto err_dfs_exit;
	err = ssb_driver_register(&b43_ssb_driver);
	if (err)
		goto err_pcmcia_exit;

	return err;

err_pcmcia_exit:
	b43_pcmcia_exit();
err_dfs_exit:
	b43_debugfs_exit();
	return err;
}

static void __exit b43_exit(void)
{
	ssb_driver_unregister(&b43_ssb_driver);
	b43_pcmcia_exit();
	b43_debugfs_exit();
}

module_init(b43_init)
module_exit(b43_exit)
