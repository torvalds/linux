/*
 *
 *  Broadcom B43legacy wireless driver
 *
 *  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
 *  Copyright (c) 2005-2008 Stefano Brivio <stefano.brivio@polimi.it>
 *  Copyright (c) 2005, 2006 Michael Buesch <mb@bu3sch.de>
 *  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
 *  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 *  Copyright (c) 2007 Larry Finger <Larry.Finger@lwfinger.net>
 *
 *  Some parts of the code in this file are derived from the ipw2200
 *  driver  Copyright(c) 2003 - 2004 Intel Corporation.

 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
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
#include <net/dst.h>
#include <asm/unaligned.h>

#include "b43legacy.h"
#include "main.h"
#include "debugfs.h"
#include "phy.h"
#include "dma.h"
#include "pio.h"
#include "sysfs.h"
#include "xmit.h"
#include "radio.h"


MODULE_DESCRIPTION("Broadcom B43legacy wireless driver");
MODULE_AUTHOR("Martin Langer");
MODULE_AUTHOR("Stefano Brivio");
MODULE_AUTHOR("Michael Buesch");
MODULE_LICENSE("GPL");

MODULE_FIRMWARE(B43legacy_SUPPORTED_FIRMWARE_ID);

#if defined(CONFIG_B43LEGACY_DMA) && defined(CONFIG_B43LEGACY_PIO)
static int modparam_pio;
module_param_named(pio, modparam_pio, int, 0444);
MODULE_PARM_DESC(pio, "enable(1) / disable(0) PIO mode");
#elif defined(CONFIG_B43LEGACY_DMA)
# define modparam_pio	0
#elif defined(CONFIG_B43LEGACY_PIO)
# define modparam_pio	1
#endif

static int modparam_bad_frames_preempt;
module_param_named(bad_frames_preempt, modparam_bad_frames_preempt, int, 0444);
MODULE_PARM_DESC(bad_frames_preempt, "enable(1) / disable(0) Bad Frames"
		 " Preemption");

static char modparam_fwpostfix[16];
module_param_string(fwpostfix, modparam_fwpostfix, 16, 0444);
MODULE_PARM_DESC(fwpostfix, "Postfix for the firmware files to load.");

/* The following table supports BCM4301, BCM4303 and BCM4306/2 devices. */
static const struct ssb_device_id b43legacy_ssb_tbl[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 2),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 4),
	SSB_DEVTABLE_END
};
MODULE_DEVICE_TABLE(ssb, b43legacy_ssb_tbl);


/* Channel and ratetables are shared for all devices.
 * They can't be const, because ieee80211 puts some precalculated
 * data in there. This data is the same for all devices, so we don't
 * get concurrency issues */
#define RATETAB_ENT(_rateid, _flags) \
	{							\
		.rate	= B43legacy_RATE_TO_100KBPS(_rateid),	\
		.val	= (_rateid),				\
		.val2	= (_rateid),				\
		.flags	= (_flags),				\
	}
static struct ieee80211_rate __b43legacy_ratetable[] = {
	RATETAB_ENT(B43legacy_CCK_RATE_1MB, IEEE80211_RATE_CCK),
	RATETAB_ENT(B43legacy_CCK_RATE_2MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43legacy_CCK_RATE_5MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43legacy_CCK_RATE_11MB, IEEE80211_RATE_CCK_2),
	RATETAB_ENT(B43legacy_OFDM_RATE_6MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_9MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_12MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_18MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_24MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_36MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_48MB, IEEE80211_RATE_OFDM),
	RATETAB_ENT(B43legacy_OFDM_RATE_54MB, IEEE80211_RATE_OFDM),
};
#define b43legacy_a_ratetable		(__b43legacy_ratetable + 4)
#define b43legacy_a_ratetable_size	8
#define b43legacy_b_ratetable		(__b43legacy_ratetable + 0)
#define b43legacy_b_ratetable_size	4
#define b43legacy_g_ratetable		(__b43legacy_ratetable + 0)
#define b43legacy_g_ratetable_size	12

#define CHANTAB_ENT(_chanid, _freq) \
	{							\
		.chan	= (_chanid),				\
		.freq	= (_freq),				\
		.val	= (_chanid),				\
		.flag	= IEEE80211_CHAN_W_SCAN |		\
			  IEEE80211_CHAN_W_ACTIVE_SCAN |	\
			  IEEE80211_CHAN_W_IBSS,		\
		.power_level	= 0x0A,				\
		.antenna_max	= 0xFF,				\
	}
static struct ieee80211_channel b43legacy_bg_chantable[] = {
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
#define b43legacy_bg_chantable_size	ARRAY_SIZE(b43legacy_bg_chantable)

static void b43legacy_wireless_core_exit(struct b43legacy_wldev *dev);
static int b43legacy_wireless_core_init(struct b43legacy_wldev *dev);
static void b43legacy_wireless_core_stop(struct b43legacy_wldev *dev);
static int b43legacy_wireless_core_start(struct b43legacy_wldev *dev);


static int b43legacy_ratelimit(struct b43legacy_wl *wl)
{
	if (!wl || !wl->current_dev)
		return 1;
	if (b43legacy_status(wl->current_dev) < B43legacy_STAT_STARTED)
		return 1;
	/* We are up and running.
	 * Ratelimit the messages to avoid DoS over the net. */
	return net_ratelimit();
}

void b43legacyinfo(struct b43legacy_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43legacy_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_INFO "b43legacy-%s: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

void b43legacyerr(struct b43legacy_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43legacy_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_ERR "b43legacy-%s ERROR: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

void b43legacywarn(struct b43legacy_wl *wl, const char *fmt, ...)
{
	va_list args;

	if (!b43legacy_ratelimit(wl))
		return;
	va_start(args, fmt);
	printk(KERN_WARNING "b43legacy-%s warning: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}

#if B43legacy_DEBUG
void b43legacydbg(struct b43legacy_wl *wl, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_DEBUG "b43legacy-%s debug: ",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan");
	vprintk(fmt, args);
	va_end(args);
}
#endif /* DEBUG */

static void b43legacy_ram_write(struct b43legacy_wldev *dev, u16 offset,
				u32 val)
{
	u32 status;

	B43legacy_WARN_ON(offset % 4 != 0);

	status = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	if (status & B43legacy_MACCTL_BE)
		val = swab32(val);

	b43legacy_write32(dev, B43legacy_MMIO_RAM_CONTROL, offset);
	mmiowb();
	b43legacy_write32(dev, B43legacy_MMIO_RAM_DATA, val);
}

static inline
void b43legacy_shm_control_word(struct b43legacy_wldev *dev,
				u16 routing, u16 offset)
{
	u32 control;

	/* "offset" is the WORD offset. */

	control = routing;
	control <<= 16;
	control |= offset;
	b43legacy_write32(dev, B43legacy_MMIO_SHM_CONTROL, control);
}

u32 b43legacy_shm_read32(struct b43legacy_wldev *dev,
		       u16 routing, u16 offset)
{
	u32 ret;

	if (routing == B43legacy_SHM_SHARED) {
		B43legacy_WARN_ON((offset & 0x0001) != 0);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43legacy_shm_control_word(dev, routing, offset >> 2);
			ret = b43legacy_read16(dev,
				B43legacy_MMIO_SHM_DATA_UNALIGNED);
			ret <<= 16;
			b43legacy_shm_control_word(dev, routing,
						     (offset >> 2) + 1);
			ret |= b43legacy_read16(dev, B43legacy_MMIO_SHM_DATA);

			return ret;
		}
		offset >>= 2;
	}
	b43legacy_shm_control_word(dev, routing, offset);
	ret = b43legacy_read32(dev, B43legacy_MMIO_SHM_DATA);

	return ret;
}

u16 b43legacy_shm_read16(struct b43legacy_wldev *dev,
			   u16 routing, u16 offset)
{
	u16 ret;

	if (routing == B43legacy_SHM_SHARED) {
		B43legacy_WARN_ON((offset & 0x0001) != 0);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43legacy_shm_control_word(dev, routing, offset >> 2);
			ret = b43legacy_read16(dev,
					     B43legacy_MMIO_SHM_DATA_UNALIGNED);

			return ret;
		}
		offset >>= 2;
	}
	b43legacy_shm_control_word(dev, routing, offset);
	ret = b43legacy_read16(dev, B43legacy_MMIO_SHM_DATA);

	return ret;
}

void b43legacy_shm_write32(struct b43legacy_wldev *dev,
			   u16 routing, u16 offset,
			   u32 value)
{
	if (routing == B43legacy_SHM_SHARED) {
		B43legacy_WARN_ON((offset & 0x0001) != 0);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43legacy_shm_control_word(dev, routing, offset >> 2);
			mmiowb();
			b43legacy_write16(dev,
					  B43legacy_MMIO_SHM_DATA_UNALIGNED,
					  (value >> 16) & 0xffff);
			mmiowb();
			b43legacy_shm_control_word(dev, routing,
						   (offset >> 2) + 1);
			mmiowb();
			b43legacy_write16(dev, B43legacy_MMIO_SHM_DATA,
					  value & 0xffff);
			return;
		}
		offset >>= 2;
	}
	b43legacy_shm_control_word(dev, routing, offset);
	mmiowb();
	b43legacy_write32(dev, B43legacy_MMIO_SHM_DATA, value);
}

void b43legacy_shm_write16(struct b43legacy_wldev *dev, u16 routing, u16 offset,
			   u16 value)
{
	if (routing == B43legacy_SHM_SHARED) {
		B43legacy_WARN_ON((offset & 0x0001) != 0);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43legacy_shm_control_word(dev, routing, offset >> 2);
			mmiowb();
			b43legacy_write16(dev,
					  B43legacy_MMIO_SHM_DATA_UNALIGNED,
					  value);
			return;
		}
		offset >>= 2;
	}
	b43legacy_shm_control_word(dev, routing, offset);
	mmiowb();
	b43legacy_write16(dev, B43legacy_MMIO_SHM_DATA, value);
}

/* Read HostFlags */
u32 b43legacy_hf_read(struct b43legacy_wldev *dev)
{
	u32 ret;

	ret = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				   B43legacy_SHM_SH_HOSTFHI);
	ret <<= 16;
	ret |= b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				    B43legacy_SHM_SH_HOSTFLO);

	return ret;
}

/* Write HostFlags */
void b43legacy_hf_write(struct b43legacy_wldev *dev, u32 value)
{
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_HOSTFLO,
			      (value & 0x0000FFFF));
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_HOSTFHI,
			      ((value & 0xFFFF0000) >> 16));
}

void b43legacy_tsf_read(struct b43legacy_wldev *dev, u64 *tsf)
{
	/* We need to be careful. As we read the TSF from multiple
	 * registers, we should take care of register overflows.
	 * In theory, the whole tsf read process should be atomic.
	 * We try to be atomic here, by restaring the read process,
	 * if any of the high registers changed (overflew).
	 */
	if (dev->dev->id.revision >= 3) {
		u32 low;
		u32 high;
		u32 high2;

		do {
			high = b43legacy_read32(dev,
					B43legacy_MMIO_REV3PLUS_TSF_HIGH);
			low = b43legacy_read32(dev,
					B43legacy_MMIO_REV3PLUS_TSF_LOW);
			high2 = b43legacy_read32(dev,
					B43legacy_MMIO_REV3PLUS_TSF_HIGH);
		} while (unlikely(high != high2));

		*tsf = high;
		*tsf <<= 32;
		*tsf |= low;
	} else {
		u64 tmp;
		u16 v0;
		u16 v1;
		u16 v2;
		u16 v3;
		u16 test1;
		u16 test2;
		u16 test3;

		do {
			v3 = b43legacy_read16(dev, B43legacy_MMIO_TSF_3);
			v2 = b43legacy_read16(dev, B43legacy_MMIO_TSF_2);
			v1 = b43legacy_read16(dev, B43legacy_MMIO_TSF_1);
			v0 = b43legacy_read16(dev, B43legacy_MMIO_TSF_0);

			test3 = b43legacy_read16(dev, B43legacy_MMIO_TSF_3);
			test2 = b43legacy_read16(dev, B43legacy_MMIO_TSF_2);
			test1 = b43legacy_read16(dev, B43legacy_MMIO_TSF_1);
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

static void b43legacy_time_lock(struct b43legacy_wldev *dev)
{
	u32 status;

	status = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	status |= B43legacy_MACCTL_TBTTHOLD;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, status);
	mmiowb();
}

static void b43legacy_time_unlock(struct b43legacy_wldev *dev)
{
	u32 status;

	status = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	status &= ~B43legacy_MACCTL_TBTTHOLD;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, status);
}

static void b43legacy_tsf_write_locked(struct b43legacy_wldev *dev, u64 tsf)
{
	/* Be careful with the in-progress timer.
	 * First zero out the low register, so we have a full
	 * register-overflow duration to complete the operation.
	 */
	if (dev->dev->id.revision >= 3) {
		u32 lo = (tsf & 0x00000000FFFFFFFFULL);
		u32 hi = (tsf & 0xFFFFFFFF00000000ULL) >> 32;

		b43legacy_write32(dev, B43legacy_MMIO_REV3PLUS_TSF_LOW, 0);
		mmiowb();
		b43legacy_write32(dev, B43legacy_MMIO_REV3PLUS_TSF_HIGH,
				    hi);
		mmiowb();
		b43legacy_write32(dev, B43legacy_MMIO_REV3PLUS_TSF_LOW,
				    lo);
	} else {
		u16 v0 = (tsf & 0x000000000000FFFFULL);
		u16 v1 = (tsf & 0x00000000FFFF0000ULL) >> 16;
		u16 v2 = (tsf & 0x0000FFFF00000000ULL) >> 32;
		u16 v3 = (tsf & 0xFFFF000000000000ULL) >> 48;

		b43legacy_write16(dev, B43legacy_MMIO_TSF_0, 0);
		mmiowb();
		b43legacy_write16(dev, B43legacy_MMIO_TSF_3, v3);
		mmiowb();
		b43legacy_write16(dev, B43legacy_MMIO_TSF_2, v2);
		mmiowb();
		b43legacy_write16(dev, B43legacy_MMIO_TSF_1, v1);
		mmiowb();
		b43legacy_write16(dev, B43legacy_MMIO_TSF_0, v0);
	}
}

void b43legacy_tsf_write(struct b43legacy_wldev *dev, u64 tsf)
{
	b43legacy_time_lock(dev);
	b43legacy_tsf_write_locked(dev, tsf);
	b43legacy_time_unlock(dev);
}

static
void b43legacy_macfilter_set(struct b43legacy_wldev *dev,
			     u16 offset, const u8 *mac)
{
	static const u8 zero_addr[ETH_ALEN] = { 0 };
	u16 data;

	if (!mac)
		mac = zero_addr;

	offset |= 0x0020;
	b43legacy_write16(dev, B43legacy_MMIO_MACFILTER_CONTROL, offset);

	data = mac[0];
	data |= mac[1] << 8;
	b43legacy_write16(dev, B43legacy_MMIO_MACFILTER_DATA, data);
	data = mac[2];
	data |= mac[3] << 8;
	b43legacy_write16(dev, B43legacy_MMIO_MACFILTER_DATA, data);
	data = mac[4];
	data |= mac[5] << 8;
	b43legacy_write16(dev, B43legacy_MMIO_MACFILTER_DATA, data);
}

static void b43legacy_write_mac_bssid_templates(struct b43legacy_wldev *dev)
{
	static const u8 zero_addr[ETH_ALEN] = { 0 };
	const u8 *mac = dev->wl->mac_addr;
	const u8 *bssid = dev->wl->bssid;
	u8 mac_bssid[ETH_ALEN * 2];
	int i;
	u32 tmp;

	if (!bssid)
		bssid = zero_addr;
	if (!mac)
		mac = zero_addr;

	b43legacy_macfilter_set(dev, B43legacy_MACFILTER_BSSID, bssid);

	memcpy(mac_bssid, mac, ETH_ALEN);
	memcpy(mac_bssid + ETH_ALEN, bssid, ETH_ALEN);

	/* Write our MAC address and BSSID to template ram */
	for (i = 0; i < ARRAY_SIZE(mac_bssid); i += sizeof(u32)) {
		tmp =  (u32)(mac_bssid[i + 0]);
		tmp |= (u32)(mac_bssid[i + 1]) << 8;
		tmp |= (u32)(mac_bssid[i + 2]) << 16;
		tmp |= (u32)(mac_bssid[i + 3]) << 24;
		b43legacy_ram_write(dev, 0x20 + i, tmp);
		b43legacy_ram_write(dev, 0x78 + i, tmp);
		b43legacy_ram_write(dev, 0x478 + i, tmp);
	}
}

static void b43legacy_upload_card_macaddress(struct b43legacy_wldev *dev)
{
	b43legacy_write_mac_bssid_templates(dev);
	b43legacy_macfilter_set(dev, B43legacy_MACFILTER_SELF,
				dev->wl->mac_addr);
}

static void b43legacy_set_slot_time(struct b43legacy_wldev *dev,
				    u16 slot_time)
{
	/* slot_time is in usec. */
	if (dev->phy.type != B43legacy_PHYTYPE_G)
		return;
	b43legacy_write16(dev, 0x684, 510 + slot_time);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0010,
			      slot_time);
}

static void b43legacy_short_slot_timing_enable(struct b43legacy_wldev *dev)
{
	b43legacy_set_slot_time(dev, 9);
	dev->short_slot = 1;
}

static void b43legacy_short_slot_timing_disable(struct b43legacy_wldev *dev)
{
	b43legacy_set_slot_time(dev, 20);
	dev->short_slot = 0;
}

/* Enable a Generic IRQ. "mask" is the mask of which IRQs to enable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 b43legacy_interrupt_enable(struct b43legacy_wldev *dev,
					     u32 mask)
{
	u32 old_mask;

	old_mask = b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_MASK);
	b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_MASK, old_mask |
			  mask);

	return old_mask;
}

/* Disable a Generic IRQ. "mask" is the mask of which IRQs to disable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 b43legacy_interrupt_disable(struct b43legacy_wldev *dev,
					      u32 mask)
{
	u32 old_mask;

	old_mask = b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_MASK);
	b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_MASK, old_mask & ~mask);

	return old_mask;
}

/* Synchronize IRQ top- and bottom-half.
 * IRQs must be masked before calling this.
 * This must not be called with the irq_lock held.
 */
static void b43legacy_synchronize_irq(struct b43legacy_wldev *dev)
{
	synchronize_irq(dev->dev->irq);
	tasklet_kill(&dev->isr_tasklet);
}

/* DummyTransmission function, as documented on
 * http://bcm-specs.sipsolutions.net/DummyTransmission
 */
void b43legacy_dummy_transmission(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	unsigned int i;
	unsigned int max_loop;
	u16 value;
	u32 buffer[5] = {
		0x00000000,
		0x00D40000,
		0x00000000,
		0x01000000,
		0x00000000,
	};

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
	case B43legacy_PHYTYPE_G:
		max_loop = 0xFA;
		buffer[0] = 0x000B846E;
		break;
	default:
		B43legacy_BUG_ON(1);
		return;
	}

	for (i = 0; i < 5; i++)
		b43legacy_ram_write(dev, i * 4, buffer[i]);

	/* dummy read follows */
	b43legacy_read32(dev, B43legacy_MMIO_MACCTL);

	b43legacy_write16(dev, 0x0568, 0x0000);
	b43legacy_write16(dev, 0x07C0, 0x0000);
	b43legacy_write16(dev, 0x050C, 0x0000);
	b43legacy_write16(dev, 0x0508, 0x0000);
	b43legacy_write16(dev, 0x050A, 0x0000);
	b43legacy_write16(dev, 0x054C, 0x0000);
	b43legacy_write16(dev, 0x056A, 0x0014);
	b43legacy_write16(dev, 0x0568, 0x0826);
	b43legacy_write16(dev, 0x0500, 0x0000);
	b43legacy_write16(dev, 0x0502, 0x0030);

	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43legacy_radio_write16(dev, 0x0051, 0x0017);
	for (i = 0x00; i < max_loop; i++) {
		value = b43legacy_read16(dev, 0x050E);
		if (value & 0x0080)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = b43legacy_read16(dev, 0x050E);
		if (value & 0x0400)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = b43legacy_read16(dev, 0x0690);
		if (!(value & 0x0100))
			break;
		udelay(10);
	}
	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43legacy_radio_write16(dev, 0x0051, 0x0037);
}

/* Turn the Analog ON/OFF */
static void b43legacy_switch_analog(struct b43legacy_wldev *dev, int on)
{
	b43legacy_write16(dev, B43legacy_MMIO_PHY0, on ? 0 : 0xF4);
}

void b43legacy_wireless_core_reset(struct b43legacy_wldev *dev, u32 flags)
{
	u32 tmslow;
	u32 macctl;

	flags |= B43legacy_TMSLOW_PHYCLKEN;
	flags |= B43legacy_TMSLOW_PHYRESET;
	ssb_device_enable(dev->dev, flags);
	msleep(2); /* Wait for the PLL to turn on. */

	/* Now take the PHY out of Reset again */
	tmslow = ssb_read32(dev->dev, SSB_TMSLOW);
	tmslow |= SSB_TMSLOW_FGC;
	tmslow &= ~B43legacy_TMSLOW_PHYRESET;
	ssb_write32(dev->dev, SSB_TMSLOW, tmslow);
	ssb_read32(dev->dev, SSB_TMSLOW); /* flush */
	msleep(1);
	tmslow &= ~SSB_TMSLOW_FGC;
	ssb_write32(dev->dev, SSB_TMSLOW, tmslow);
	ssb_read32(dev->dev, SSB_TMSLOW); /* flush */
	msleep(1);

	/* Turn Analog ON */
	b43legacy_switch_analog(dev, 1);

	macctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	macctl &= ~B43legacy_MACCTL_GMODE;
	if (flags & B43legacy_TMSLOW_GMODE) {
		macctl |= B43legacy_MACCTL_GMODE;
		dev->phy.gmode = 1;
	} else
		dev->phy.gmode = 0;
	macctl |= B43legacy_MACCTL_IHR_ENABLED;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);
}

static void handle_irq_transmit_status(struct b43legacy_wldev *dev)
{
	u32 v0;
	u32 v1;
	u16 tmp;
	struct b43legacy_txstatus stat;

	while (1) {
		v0 = b43legacy_read32(dev, B43legacy_MMIO_XMITSTAT_0);
		if (!(v0 & 0x00000001))
			break;
		v1 = b43legacy_read32(dev, B43legacy_MMIO_XMITSTAT_1);

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

		b43legacy_handle_txstatus(dev, &stat);
	}
}

static void drain_txstatus_queue(struct b43legacy_wldev *dev)
{
	u32 dummy;

	if (dev->dev->id.revision < 5)
		return;
	/* Read all entries from the microcode TXstatus FIFO
	 * and throw them away.
	 */
	while (1) {
		dummy = b43legacy_read32(dev, B43legacy_MMIO_XMITSTAT_0);
		if (!(dummy & 0x00000001))
			break;
		dummy = b43legacy_read32(dev, B43legacy_MMIO_XMITSTAT_1);
	}
}

static u32 b43legacy_jssi_read(struct b43legacy_wldev *dev)
{
	u32 val = 0;

	val = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x40A);
	val <<= 16;
	val |= b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x408);

	return val;
}

static void b43legacy_jssi_write(struct b43legacy_wldev *dev, u32 jssi)
{
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x408,
			      (jssi & 0x0000FFFF));
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x40A,
			      (jssi & 0xFFFF0000) >> 16);
}

static void b43legacy_generate_noise_sample(struct b43legacy_wldev *dev)
{
	b43legacy_jssi_write(dev, 0x7F7F7F7F);
	b43legacy_write32(dev, B43legacy_MMIO_MACCMD,
			  b43legacy_read32(dev,
			  B43legacy_MMIO_MACCMD)
			  | (1 << 4));
	B43legacy_WARN_ON(dev->noisecalc.channel_at_start !=
			    dev->phy.channel);
}

static void b43legacy_calculate_link_quality(struct b43legacy_wldev *dev)
{
	/* Top half of Link Quality calculation. */

	if (dev->noisecalc.calculation_running)
		return;
	dev->noisecalc.channel_at_start = dev->phy.channel;
	dev->noisecalc.calculation_running = 1;
	dev->noisecalc.nr_samples = 0;

	b43legacy_generate_noise_sample(dev);
}

static void handle_irq_noise(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 tmp;
	u8 noise[4];
	u8 i;
	u8 j;
	s32 average;

	/* Bottom half of Link Quality calculation. */

	B43legacy_WARN_ON(!dev->noisecalc.calculation_running);
	if (dev->noisecalc.channel_at_start != phy->channel)
		goto drop_calculation;
	*((__le32 *)noise) = cpu_to_le32(b43legacy_jssi_read(dev));
	if (noise[0] == 0x7F || noise[1] == 0x7F ||
	    noise[2] == 0x7F || noise[3] == 0x7F)
		goto generate_new;

	/* Get the noise samples. */
	B43legacy_WARN_ON(dev->noisecalc.nr_samples >= 8);
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
		tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
					     0x40C);
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
	b43legacy_generate_noise_sample(dev);
}

static void handle_irq_tbtt_indication(struct b43legacy_wldev *dev)
{
	if (b43legacy_is_mode(dev->wl, IEEE80211_IF_TYPE_AP)) {
		/* TODO: PS TBTT */
	} else {
		if (1/*FIXME: the last PSpoll frame was sent successfully */)
			b43legacy_power_saving_ctl_bits(dev, -1, -1);
	}
	dev->reg124_set_0x4 = 0;
	if (b43legacy_is_mode(dev->wl, IEEE80211_IF_TYPE_IBSS))
		dev->reg124_set_0x4 = 1;
}

static void handle_irq_atim_end(struct b43legacy_wldev *dev)
{
	if (!dev->reg124_set_0x4) /*FIXME rename this variable*/
		return;
	b43legacy_write32(dev, B43legacy_MMIO_MACCMD,
			  b43legacy_read32(dev, B43legacy_MMIO_MACCMD)
			  | 0x4);
}

static void handle_irq_pmq(struct b43legacy_wldev *dev)
{
	u32 tmp;

	/* TODO: AP mode. */

	while (1) {
		tmp = b43legacy_read32(dev, B43legacy_MMIO_PS_STATUS);
		if (!(tmp & 0x00000008))
			break;
	}
	/* 16bit write is odd, but correct. */
	b43legacy_write16(dev, B43legacy_MMIO_PS_STATUS, 0x0002);
}

static void b43legacy_write_template_common(struct b43legacy_wldev *dev,
					    const u8 *data, u16 size,
					    u16 ram_offset,
					    u16 shm_size_offset, u8 rate)
{
	u32 i;
	u32 tmp;
	struct b43legacy_plcp_hdr4 plcp;

	plcp.data = 0;
	b43legacy_generate_plcp_hdr(&plcp, size + FCS_LEN, rate);
	b43legacy_ram_write(dev, ram_offset, le32_to_cpu(plcp.data));
	ram_offset += sizeof(u32);
	/* The PLCP is 6 bytes long, but we only wrote 4 bytes, yet.
	 * So leave the first two bytes of the next write blank.
	 */
	tmp = (u32)(data[0]) << 16;
	tmp |= (u32)(data[1]) << 24;
	b43legacy_ram_write(dev, ram_offset, tmp);
	ram_offset += sizeof(u32);
	for (i = 2; i < size; i += sizeof(u32)) {
		tmp = (u32)(data[i + 0]);
		if (i + 1 < size)
			tmp |= (u32)(data[i + 1]) << 8;
		if (i + 2 < size)
			tmp |= (u32)(data[i + 2]) << 16;
		if (i + 3 < size)
			tmp |= (u32)(data[i + 3]) << 24;
		b43legacy_ram_write(dev, ram_offset + i - 2, tmp);
	}
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, shm_size_offset,
			      size + sizeof(struct b43legacy_plcp_hdr6));
}

static void b43legacy_write_beacon_template(struct b43legacy_wldev *dev,
					    u16 ram_offset,
					    u16 shm_size_offset, u8 rate)
{
	int len;
	const u8 *data;

	B43legacy_WARN_ON(!dev->cached_beacon);
	len = min((size_t)dev->cached_beacon->len,
		  0x200 - sizeof(struct b43legacy_plcp_hdr6));
	data = (const u8 *)(dev->cached_beacon->data);
	b43legacy_write_template_common(dev, data,
					len, ram_offset,
					shm_size_offset, rate);
}

static void b43legacy_write_probe_resp_plcp(struct b43legacy_wldev *dev,
					    u16 shm_offset, u16 size,
					    u8 rate)
{
	struct b43legacy_plcp_hdr4 plcp;
	u32 tmp;
	__le16 dur;

	plcp.data = 0;
	b43legacy_generate_plcp_hdr(&plcp, size + FCS_LEN, rate);
	dur = ieee80211_generic_frame_duration(dev->wl->hw,
					       dev->wl->vif,
					       size,
					       B43legacy_RATE_TO_100KBPS(rate));
	/* Write PLCP in two parts and timing for packet transfer */
	tmp = le32_to_cpu(plcp.data);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, shm_offset,
			      tmp & 0xFFFF);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, shm_offset + 2,
			      tmp >> 16);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, shm_offset + 6,
			      le16_to_cpu(dur));
}

/* Instead of using custom probe response template, this function
 * just patches custom beacon template by:
 * 1) Changing packet type
 * 2) Patching duration field
 * 3) Stripping TIM
 */
static u8 *b43legacy_generate_probe_resp(struct b43legacy_wldev *dev,
					 u16 *dest_size, u8 rate)
{
	const u8 *src_data;
	u8 *dest_data;
	u16 src_size;
	u16 elem_size;
	u16 src_pos;
	u16 dest_pos;
	__le16 dur;
	struct ieee80211_hdr *hdr;

	B43legacy_WARN_ON(!dev->cached_beacon);
	src_size = dev->cached_beacon->len;
	src_data = (const u8 *)dev->cached_beacon->data;

	if (unlikely(src_size < 0x24)) {
		b43legacydbg(dev->wl, "b43legacy_generate_probe_resp: "
		       "invalid beacon\n");
		return NULL;
	}

	dest_data = kmalloc(src_size, GFP_ATOMIC);
	if (unlikely(!dest_data))
		return NULL;

	/* 0x24 is offset of first variable-len Information-Element
	 * in beacon frame.
	 */
	memcpy(dest_data, src_data, 0x24);
	src_pos = 0x24;
	dest_pos = 0x24;
	for (; src_pos < src_size - 2; src_pos += elem_size) {
		elem_size = src_data[src_pos + 1] + 2;
		if (src_data[src_pos] != 0x05) { /* TIM */
			memcpy(dest_data + dest_pos, src_data + src_pos,
			       elem_size);
			dest_pos += elem_size;
		}
	}
	*dest_size = dest_pos;
	hdr = (struct ieee80211_hdr *)dest_data;

	/* Set the frame control. */
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_RESP);
	dur = ieee80211_generic_frame_duration(dev->wl->hw,
					       dev->wl->vif,
					       *dest_size,
					       B43legacy_RATE_TO_100KBPS(rate));
	hdr->duration_id = dur;

	return dest_data;
}

static void b43legacy_write_probe_resp_template(struct b43legacy_wldev *dev,
						u16 ram_offset,
						u16 shm_size_offset, u8 rate)
{
	u8 *probe_resp_data;
	u16 size;

	B43legacy_WARN_ON(!dev->cached_beacon);
	size = dev->cached_beacon->len;
	probe_resp_data = b43legacy_generate_probe_resp(dev, &size, rate);
	if (unlikely(!probe_resp_data))
		return;

	/* Looks like PLCP headers plus packet timings are stored for
	 * all possible basic rates
	 */
	b43legacy_write_probe_resp_plcp(dev, 0x31A, size,
					B43legacy_CCK_RATE_1MB);
	b43legacy_write_probe_resp_plcp(dev, 0x32C, size,
					B43legacy_CCK_RATE_2MB);
	b43legacy_write_probe_resp_plcp(dev, 0x33E, size,
					B43legacy_CCK_RATE_5MB);
	b43legacy_write_probe_resp_plcp(dev, 0x350, size,
					B43legacy_CCK_RATE_11MB);

	size = min((size_t)size,
		   0x200 - sizeof(struct b43legacy_plcp_hdr6));
	b43legacy_write_template_common(dev, probe_resp_data,
					size, ram_offset,
					shm_size_offset, rate);
	kfree(probe_resp_data);
}

static int b43legacy_refresh_cached_beacon(struct b43legacy_wldev *dev,
					   struct sk_buff *beacon)
{
	if (dev->cached_beacon)
		kfree_skb(dev->cached_beacon);
	dev->cached_beacon = beacon;

	return 0;
}

static void b43legacy_update_templates(struct b43legacy_wldev *dev)
{
	u32 status;

	B43legacy_WARN_ON(!dev->cached_beacon);

	b43legacy_write_beacon_template(dev, 0x68, 0x18,
					B43legacy_CCK_RATE_1MB);
	b43legacy_write_beacon_template(dev, 0x468, 0x1A,
					B43legacy_CCK_RATE_1MB);
	b43legacy_write_probe_resp_template(dev, 0x268, 0x4A,
					    B43legacy_CCK_RATE_11MB);

	status = b43legacy_read32(dev, B43legacy_MMIO_MACCMD);
	status |= 0x03;
	b43legacy_write32(dev, B43legacy_MMIO_MACCMD, status);
}

static void b43legacy_refresh_templates(struct b43legacy_wldev *dev,
					struct sk_buff *beacon)
{
	int err;

	err = b43legacy_refresh_cached_beacon(dev, beacon);
	if (unlikely(err))
		return;
	b43legacy_update_templates(dev);
}

static void b43legacy_set_ssid(struct b43legacy_wldev *dev,
			       const u8 *ssid, u8 ssid_len)
{
	u32 tmp;
	u16 i;
	u16 len;

	len = min((u16)ssid_len, (u16)0x100);
	for (i = 0; i < len; i += sizeof(u32)) {
		tmp = (u32)(ssid[i + 0]);
		if (i + 1 < len)
			tmp |= (u32)(ssid[i + 1]) << 8;
		if (i + 2 < len)
			tmp |= (u32)(ssid[i + 2]) << 16;
		if (i + 3 < len)
			tmp |= (u32)(ssid[i + 3]) << 24;
		b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
				      0x380 + i, tmp);
	}
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      0x48, len);
}

static void b43legacy_set_beacon_int(struct b43legacy_wldev *dev,
				     u16 beacon_int)
{
	b43legacy_time_lock(dev);
	if (dev->dev->id.revision >= 3)
		b43legacy_write32(dev, 0x188, (beacon_int << 16));
	else {
		b43legacy_write16(dev, 0x606, (beacon_int >> 6));
		b43legacy_write16(dev, 0x610, beacon_int);
	}
	b43legacy_time_unlock(dev);
}

static void handle_irq_beacon(struct b43legacy_wldev *dev)
{
	u32 status;

	if (!b43legacy_is_mode(dev->wl, IEEE80211_IF_TYPE_AP))
		return;

	dev->irq_savedstate &= ~B43legacy_IRQ_BEACON;
	status = b43legacy_read32(dev, B43legacy_MMIO_MACCMD);

	if (!dev->cached_beacon || ((status & 0x1) && (status & 0x2))) {
		/* ACK beacon IRQ. */
		b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_REASON,
				  B43legacy_IRQ_BEACON);
		dev->irq_savedstate |= B43legacy_IRQ_BEACON;
		if (dev->cached_beacon)
			kfree_skb(dev->cached_beacon);
		dev->cached_beacon = NULL;
		return;
	}
	if (!(status & 0x1)) {
		b43legacy_write_beacon_template(dev, 0x68, 0x18,
						B43legacy_CCK_RATE_1MB);
		status |= 0x1;
		b43legacy_write32(dev, B43legacy_MMIO_MACCMD,
				  status);
	}
	if (!(status & 0x2)) {
		b43legacy_write_beacon_template(dev, 0x468, 0x1A,
						B43legacy_CCK_RATE_1MB);
		status |= 0x2;
		b43legacy_write32(dev, B43legacy_MMIO_MACCMD,
				  status);
	}
}

static void handle_irq_ucode_debug(struct b43legacy_wldev *dev)
{
}

/* Interrupt handler bottom-half */
static void b43legacy_interrupt_tasklet(struct b43legacy_wldev *dev)
{
	u32 reason;
	u32 dma_reason[ARRAY_SIZE(dev->dma_reason)];
	u32 merged_dma_reason = 0;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dev->wl->irq_lock, flags);

	B43legacy_WARN_ON(b43legacy_status(dev) <
			  B43legacy_STAT_INITIALIZED);

	reason = dev->irq_reason;
	for (i = 0; i < ARRAY_SIZE(dma_reason); i++) {
		dma_reason[i] = dev->dma_reason[i];
		merged_dma_reason |= dma_reason[i];
	}

	if (unlikely(reason & B43legacy_IRQ_MAC_TXERR))
		b43legacyerr(dev->wl, "MAC transmission error\n");

	if (unlikely(reason & B43legacy_IRQ_PHY_TXERR)) {
		b43legacyerr(dev->wl, "PHY transmission error\n");
		rmb();
		if (unlikely(atomic_dec_and_test(&dev->phy.txerr_cnt))) {
			b43legacyerr(dev->wl, "Too many PHY TX errors, "
					      "restarting the controller\n");
			b43legacy_controller_restart(dev, "PHY TX errors");
		}
	}

	if (unlikely(merged_dma_reason & (B43legacy_DMAIRQ_FATALMASK |
					  B43legacy_DMAIRQ_NONFATALMASK))) {
		if (merged_dma_reason & B43legacy_DMAIRQ_FATALMASK) {
			b43legacyerr(dev->wl, "Fatal DMA error: "
			       "0x%08X, 0x%08X, 0x%08X, "
			       "0x%08X, 0x%08X, 0x%08X\n",
			       dma_reason[0], dma_reason[1],
			       dma_reason[2], dma_reason[3],
			       dma_reason[4], dma_reason[5]);
			b43legacy_controller_restart(dev, "DMA error");
			mmiowb();
			spin_unlock_irqrestore(&dev->wl->irq_lock, flags);
			return;
		}
		if (merged_dma_reason & B43legacy_DMAIRQ_NONFATALMASK)
			b43legacyerr(dev->wl, "DMA error: "
			       "0x%08X, 0x%08X, 0x%08X, "
			       "0x%08X, 0x%08X, 0x%08X\n",
			       dma_reason[0], dma_reason[1],
			       dma_reason[2], dma_reason[3],
			       dma_reason[4], dma_reason[5]);
	}

	if (unlikely(reason & B43legacy_IRQ_UCODE_DEBUG))
		handle_irq_ucode_debug(dev);
	if (reason & B43legacy_IRQ_TBTT_INDI)
		handle_irq_tbtt_indication(dev);
	if (reason & B43legacy_IRQ_ATIM_END)
		handle_irq_atim_end(dev);
	if (reason & B43legacy_IRQ_BEACON)
		handle_irq_beacon(dev);
	if (reason & B43legacy_IRQ_PMQ)
		handle_irq_pmq(dev);
	if (reason & B43legacy_IRQ_TXFIFO_FLUSH_OK)
		;/*TODO*/
	if (reason & B43legacy_IRQ_NOISESAMPLE_OK)
		handle_irq_noise(dev);

	/* Check the DMA reason registers for received data. */
	if (dma_reason[0] & B43legacy_DMAIRQ_RX_DONE) {
		if (b43legacy_using_pio(dev))
			b43legacy_pio_rx(dev->pio.queue0);
		else
			b43legacy_dma_rx(dev->dma.rx_ring0);
	}
	B43legacy_WARN_ON(dma_reason[1] & B43legacy_DMAIRQ_RX_DONE);
	B43legacy_WARN_ON(dma_reason[2] & B43legacy_DMAIRQ_RX_DONE);
	if (dma_reason[3] & B43legacy_DMAIRQ_RX_DONE) {
		if (b43legacy_using_pio(dev))
			b43legacy_pio_rx(dev->pio.queue3);
		else
			b43legacy_dma_rx(dev->dma.rx_ring3);
	}
	B43legacy_WARN_ON(dma_reason[4] & B43legacy_DMAIRQ_RX_DONE);
	B43legacy_WARN_ON(dma_reason[5] & B43legacy_DMAIRQ_RX_DONE);

	if (reason & B43legacy_IRQ_TX_OK)
		handle_irq_transmit_status(dev);

	b43legacy_interrupt_enable(dev, dev->irq_savedstate);
	mmiowb();
	spin_unlock_irqrestore(&dev->wl->irq_lock, flags);
}

static void pio_irq_workaround(struct b43legacy_wldev *dev,
			       u16 base, int queueidx)
{
	u16 rxctl;

	rxctl = b43legacy_read16(dev, base + B43legacy_PIO_RXCTL);
	if (rxctl & B43legacy_PIO_RXCTL_DATAAVAILABLE)
		dev->dma_reason[queueidx] |= B43legacy_DMAIRQ_RX_DONE;
	else
		dev->dma_reason[queueidx] &= ~B43legacy_DMAIRQ_RX_DONE;
}

static void b43legacy_interrupt_ack(struct b43legacy_wldev *dev, u32 reason)
{
	if (b43legacy_using_pio(dev) &&
	    (dev->dev->id.revision < 3) &&
	    (!(reason & B43legacy_IRQ_PIO_WORKAROUND))) {
		/* Apply a PIO specific workaround to the dma_reasons */
		pio_irq_workaround(dev, B43legacy_MMIO_PIO1_BASE, 0);
		pio_irq_workaround(dev, B43legacy_MMIO_PIO2_BASE, 1);
		pio_irq_workaround(dev, B43legacy_MMIO_PIO3_BASE, 2);
		pio_irq_workaround(dev, B43legacy_MMIO_PIO4_BASE, 3);
	}

	b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_REASON, reason);

	b43legacy_write32(dev, B43legacy_MMIO_DMA0_REASON,
			  dev->dma_reason[0]);
	b43legacy_write32(dev, B43legacy_MMIO_DMA1_REASON,
			  dev->dma_reason[1]);
	b43legacy_write32(dev, B43legacy_MMIO_DMA2_REASON,
			  dev->dma_reason[2]);
	b43legacy_write32(dev, B43legacy_MMIO_DMA3_REASON,
			  dev->dma_reason[3]);
	b43legacy_write32(dev, B43legacy_MMIO_DMA4_REASON,
			  dev->dma_reason[4]);
	b43legacy_write32(dev, B43legacy_MMIO_DMA5_REASON,
			  dev->dma_reason[5]);
}

/* Interrupt handler top-half */
static irqreturn_t b43legacy_interrupt_handler(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	struct b43legacy_wldev *dev = dev_id;
	u32 reason;

	if (!dev)
		return IRQ_NONE;

	spin_lock(&dev->wl->irq_lock);

	if (b43legacy_status(dev) < B43legacy_STAT_STARTED)
		goto out;
	reason = b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);
	if (reason == 0xffffffff) /* shared IRQ */
		goto out;
	ret = IRQ_HANDLED;
	reason &= b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_MASK);
	if (!reason)
		goto out;

	dev->dma_reason[0] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA0_REASON)
					      & 0x0001DC00;
	dev->dma_reason[1] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA1_REASON)
					      & 0x0000DC00;
	dev->dma_reason[2] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA2_REASON)
					      & 0x0000DC00;
	dev->dma_reason[3] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA3_REASON)
					      & 0x0001DC00;
	dev->dma_reason[4] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA4_REASON)
					      & 0x0000DC00;
	dev->dma_reason[5] = b43legacy_read32(dev,
					      B43legacy_MMIO_DMA5_REASON)
					      & 0x0000DC00;

	b43legacy_interrupt_ack(dev, reason);
	/* disable all IRQs. They are enabled again in the bottom half. */
	dev->irq_savedstate = b43legacy_interrupt_disable(dev,
							  B43legacy_IRQ_ALL);
	/* save the reason code and call our bottom half. */
	dev->irq_reason = reason;
	tasklet_schedule(&dev->isr_tasklet);
out:
	mmiowb();
	spin_unlock(&dev->wl->irq_lock);

	return ret;
}

static void b43legacy_release_firmware(struct b43legacy_wldev *dev)
{
	release_firmware(dev->fw.ucode);
	dev->fw.ucode = NULL;
	release_firmware(dev->fw.pcm);
	dev->fw.pcm = NULL;
	release_firmware(dev->fw.initvals);
	dev->fw.initvals = NULL;
	release_firmware(dev->fw.initvals_band);
	dev->fw.initvals_band = NULL;
}

static void b43legacy_print_fw_helptext(struct b43legacy_wl *wl)
{
	b43legacyerr(wl, "You must go to http://linuxwireless.org/en/users/"
		     "Drivers/b43#devicefirmware "
		     "and download the correct firmware (version 3).\n");
}

static int do_request_fw(struct b43legacy_wldev *dev,
			 const char *name,
			 const struct firmware **fw)
{
	char path[sizeof(modparam_fwpostfix) + 32];
	struct b43legacy_fw_header *hdr;
	u32 size;
	int err;

	if (!name)
		return 0;

	snprintf(path, ARRAY_SIZE(path),
		 "b43legacy%s/%s.fw",
		 modparam_fwpostfix, name);
	err = request_firmware(fw, path, dev->dev->dev);
	if (err) {
		b43legacyerr(dev->wl, "Firmware file \"%s\" not found "
		       "or load failed.\n", path);
		return err;
	}
	if ((*fw)->size < sizeof(struct b43legacy_fw_header))
		goto err_format;
	hdr = (struct b43legacy_fw_header *)((*fw)->data);
	switch (hdr->type) {
	case B43legacy_FW_TYPE_UCODE:
	case B43legacy_FW_TYPE_PCM:
		size = be32_to_cpu(hdr->size);
		if (size != (*fw)->size - sizeof(struct b43legacy_fw_header))
			goto err_format;
		/* fallthrough */
	case B43legacy_FW_TYPE_IV:
		if (hdr->ver != 1)
			goto err_format;
		break;
	default:
		goto err_format;
	}

	return err;

err_format:
	b43legacyerr(dev->wl, "Firmware file \"%s\" format error.\n", path);
	return -EPROTO;
}

static int b43legacy_request_firmware(struct b43legacy_wldev *dev)
{
	struct b43legacy_firmware *fw = &dev->fw;
	const u8 rev = dev->dev->id.revision;
	const char *filename;
	u32 tmshigh;
	int err;

	tmshigh = ssb_read32(dev->dev, SSB_TMSHIGH);
	if (!fw->ucode) {
		if (rev == 2)
			filename = "ucode2";
		else if (rev == 4)
			filename = "ucode4";
		else
			filename = "ucode5";
		err = do_request_fw(dev, filename, &fw->ucode);
		if (err)
			goto err_load;
	}
	if (!fw->pcm) {
		if (rev < 5)
			filename = "pcm4";
		else
			filename = "pcm5";
		err = do_request_fw(dev, filename, &fw->pcm);
		if (err)
			goto err_load;
	}
	if (!fw->initvals) {
		switch (dev->phy.type) {
		case B43legacy_PHYTYPE_B:
		case B43legacy_PHYTYPE_G:
			if ((rev >= 5) && (rev <= 10))
				filename = "b0g0initvals5";
			else if (rev == 2 || rev == 4)
				filename = "b0g0initvals2";
			else
				goto err_no_initvals;
			break;
		default:
			goto err_no_initvals;
		}
		err = do_request_fw(dev, filename, &fw->initvals);
		if (err)
			goto err_load;
	}
	if (!fw->initvals_band) {
		switch (dev->phy.type) {
		case B43legacy_PHYTYPE_B:
		case B43legacy_PHYTYPE_G:
			if ((rev >= 5) && (rev <= 10))
				filename = "b0g0bsinitvals5";
			else if (rev >= 11)
				filename = NULL;
			else if (rev == 2 || rev == 4)
				filename = NULL;
			else
				goto err_no_initvals;
			break;
		default:
			goto err_no_initvals;
		}
		err = do_request_fw(dev, filename, &fw->initvals_band);
		if (err)
			goto err_load;
	}

	return 0;

err_load:
	b43legacy_print_fw_helptext(dev->wl);
	goto error;

err_no_initvals:
	err = -ENODEV;
	b43legacyerr(dev->wl, "No Initial Values firmware file for PHY %u, "
	       "core rev %u\n", dev->phy.type, rev);
	goto error;

error:
	b43legacy_release_firmware(dev);
	return err;
}

static int b43legacy_upload_microcode(struct b43legacy_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43legacy_fw_header);
	const __be32 *data;
	unsigned int i;
	unsigned int len;
	u16 fwrev;
	u16 fwpatch;
	u16 fwdate;
	u16 fwtime;
	u32 tmp, macctl;
	int err = 0;

	/* Jump the microcode PSM to offset 0 */
	macctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	B43legacy_WARN_ON(macctl & B43legacy_MACCTL_PSM_RUN);
	macctl |= B43legacy_MACCTL_PSM_JMP0;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);
	/* Zero out all microcode PSM registers and shared memory. */
	for (i = 0; i < 64; i++)
		b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS, i, 0);
	for (i = 0; i < 4096; i += 2)
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, i, 0);

	/* Upload Microcode. */
	data = (__be32 *) (dev->fw.ucode->data + hdr_len);
	len = (dev->fw.ucode->size - hdr_len) / sizeof(__be32);
	b43legacy_shm_control_word(dev,
				   B43legacy_SHM_UCODE |
				   B43legacy_SHM_AUTOINC_W,
				   0x0000);
	for (i = 0; i < len; i++) {
		b43legacy_write32(dev, B43legacy_MMIO_SHM_DATA,
				    be32_to_cpu(data[i]));
		udelay(10);
	}

	if (dev->fw.pcm) {
		/* Upload PCM data. */
		data = (__be32 *) (dev->fw.pcm->data + hdr_len);
		len = (dev->fw.pcm->size - hdr_len) / sizeof(__be32);
		b43legacy_shm_control_word(dev, B43legacy_SHM_HW, 0x01EA);
		b43legacy_write32(dev, B43legacy_MMIO_SHM_DATA, 0x00004000);
		/* No need for autoinc bit in SHM_HW */
		b43legacy_shm_control_word(dev, B43legacy_SHM_HW, 0x01EB);
		for (i = 0; i < len; i++) {
			b43legacy_write32(dev, B43legacy_MMIO_SHM_DATA,
					  be32_to_cpu(data[i]));
			udelay(10);
		}
	}

	b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_REASON,
			  B43legacy_IRQ_ALL);

	/* Start the microcode PSM */
	macctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	macctl &= ~B43legacy_MACCTL_PSM_JMP0;
	macctl |= B43legacy_MACCTL_PSM_RUN;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);

	/* Wait for the microcode to load and respond */
	i = 0;
	while (1) {
		tmp = b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);
		if (tmp == B43legacy_IRQ_MAC_SUSPENDED)
			break;
		i++;
		if (i >= B43legacy_IRQWAIT_MAX_RETRIES) {
			b43legacyerr(dev->wl, "Microcode not responding\n");
			b43legacy_print_fw_helptext(dev->wl);
			err = -ENODEV;
			goto error;
		}
		msleep_interruptible(50);
		if (signal_pending(current)) {
			err = -EINTR;
			goto error;
		}
	}
	/* dummy read follows */
	b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);

	/* Get and check the revisions. */
	fwrev = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				     B43legacy_SHM_SH_UCODEREV);
	fwpatch = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				       B43legacy_SHM_SH_UCODEPATCH);
	fwdate = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				      B43legacy_SHM_SH_UCODEDATE);
	fwtime = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				      B43legacy_SHM_SH_UCODETIME);

	if (fwrev > 0x128) {
		b43legacyerr(dev->wl, "YOU ARE TRYING TO LOAD V4 FIRMWARE."
			     " Only firmware from binary drivers version 3.x"
			     " is supported. You must change your firmware"
			     " files.\n");
		b43legacy_print_fw_helptext(dev->wl);
		err = -EOPNOTSUPP;
		goto error;
	}
	b43legacyinfo(dev->wl, "Loading firmware version 0x%X, patch level %u "
		      "(20%.2i-%.2i-%.2i %.2i:%.2i:%.2i)\n", fwrev, fwpatch,
		      (fwdate >> 12) & 0xF, (fwdate >> 8) & 0xF, fwdate & 0xFF,
		      (fwtime >> 11) & 0x1F, (fwtime >> 5) & 0x3F,
		      fwtime & 0x1F);

	dev->fw.rev = fwrev;
	dev->fw.patch = fwpatch;

	return 0;

error:
	macctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	macctl &= ~B43legacy_MACCTL_PSM_RUN;
	macctl |= B43legacy_MACCTL_PSM_JMP0;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);

	return err;
}

static int b43legacy_write_initvals(struct b43legacy_wldev *dev,
				    const struct b43legacy_iv *ivals,
				    size_t count,
				    size_t array_size)
{
	const struct b43legacy_iv *iv;
	u16 offset;
	size_t i;
	bool bit32;

	BUILD_BUG_ON(sizeof(struct b43legacy_iv) != 6);
	iv = ivals;
	for (i = 0; i < count; i++) {
		if (array_size < sizeof(iv->offset_size))
			goto err_format;
		array_size -= sizeof(iv->offset_size);
		offset = be16_to_cpu(iv->offset_size);
		bit32 = !!(offset & B43legacy_IV_32BIT);
		offset &= B43legacy_IV_OFFSET_MASK;
		if (offset >= 0x1000)
			goto err_format;
		if (bit32) {
			u32 value;

			if (array_size < sizeof(iv->data.d32))
				goto err_format;
			array_size -= sizeof(iv->data.d32);

			value = be32_to_cpu(get_unaligned(&iv->data.d32));
			b43legacy_write32(dev, offset, value);

			iv = (const struct b43legacy_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be32));
		} else {
			u16 value;

			if (array_size < sizeof(iv->data.d16))
				goto err_format;
			array_size -= sizeof(iv->data.d16);

			value = be16_to_cpu(iv->data.d16);
			b43legacy_write16(dev, offset, value);

			iv = (const struct b43legacy_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be16));
		}
	}
	if (array_size)
		goto err_format;

	return 0;

err_format:
	b43legacyerr(dev->wl, "Initial Values Firmware file-format error.\n");
	b43legacy_print_fw_helptext(dev->wl);

	return -EPROTO;
}

static int b43legacy_upload_initvals(struct b43legacy_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43legacy_fw_header);
	const struct b43legacy_fw_header *hdr;
	struct b43legacy_firmware *fw = &dev->fw;
	const struct b43legacy_iv *ivals;
	size_t count;
	int err;

	hdr = (const struct b43legacy_fw_header *)(fw->initvals->data);
	ivals = (const struct b43legacy_iv *)(fw->initvals->data + hdr_len);
	count = be32_to_cpu(hdr->size);
	err = b43legacy_write_initvals(dev, ivals, count,
				 fw->initvals->size - hdr_len);
	if (err)
		goto out;
	if (fw->initvals_band) {
		hdr = (const struct b43legacy_fw_header *)
		      (fw->initvals_band->data);
		ivals = (const struct b43legacy_iv *)(fw->initvals_band->data
			+ hdr_len);
		count = be32_to_cpu(hdr->size);
		err = b43legacy_write_initvals(dev, ivals, count,
					 fw->initvals_band->size - hdr_len);
		if (err)
			goto out;
	}
out:

	return err;
}

/* Initialize the GPIOs
 * http://bcm-specs.sipsolutions.net/GPIO
 */
static int b43legacy_gpio_init(struct b43legacy_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct ssb_device *gpiodev, *pcidev = NULL;
	u32 mask;
	u32 set;

	b43legacy_write32(dev, B43legacy_MMIO_MACCTL,
			  b43legacy_read32(dev,
			  B43legacy_MMIO_MACCTL)
			  & 0xFFFF3FFF);

	b43legacy_write16(dev, B43legacy_MMIO_GPIO_MASK,
			  b43legacy_read16(dev,
			  B43legacy_MMIO_GPIO_MASK)
			  | 0x000F);

	mask = 0x0000001F;
	set = 0x0000000F;
	if (dev->dev->bus->chip_id == 0x4301) {
		mask |= 0x0060;
		set |= 0x0060;
	}
	if (dev->dev->bus->sprom.boardflags_lo & B43legacy_BFL_PACTRL) {
		b43legacy_write16(dev, B43legacy_MMIO_GPIO_MASK,
				  b43legacy_read16(dev,
				  B43legacy_MMIO_GPIO_MASK)
				  | 0x0200);
		mask |= 0x0200;
		set |= 0x0200;
	}
	if (dev->dev->id.revision >= 2)
		mask  |= 0x0010; /* FIXME: This is redundant. */

#ifdef CONFIG_SSB_DRIVER_PCICORE
	pcidev = bus->pcicore.dev;
#endif
	gpiodev = bus->chipco.dev ? : pcidev;
	if (!gpiodev)
		return 0;
	ssb_write32(gpiodev, B43legacy_GPIO_CONTROL,
		    (ssb_read32(gpiodev, B43legacy_GPIO_CONTROL)
		     & mask) | set);

	return 0;
}

/* Turn off all GPIO stuff. Call this on module unload, for example. */
static void b43legacy_gpio_cleanup(struct b43legacy_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct ssb_device *gpiodev, *pcidev = NULL;

#ifdef CONFIG_SSB_DRIVER_PCICORE
	pcidev = bus->pcicore.dev;
#endif
	gpiodev = bus->chipco.dev ? : pcidev;
	if (!gpiodev)
		return;
	ssb_write32(gpiodev, B43legacy_GPIO_CONTROL, 0);
}

/* http://bcm-specs.sipsolutions.net/EnableMac */
void b43legacy_mac_enable(struct b43legacy_wldev *dev)
{
	dev->mac_suspended--;
	B43legacy_WARN_ON(dev->mac_suspended < 0);
	B43legacy_WARN_ON(irqs_disabled());
	if (dev->mac_suspended == 0) {
		b43legacy_write32(dev, B43legacy_MMIO_MACCTL,
				  b43legacy_read32(dev,
				  B43legacy_MMIO_MACCTL)
				  | B43legacy_MACCTL_ENABLED);
		b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_REASON,
				  B43legacy_IRQ_MAC_SUSPENDED);
		/* the next two are dummy reads */
		b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
		b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);
		b43legacy_power_saving_ctl_bits(dev, -1, -1);

		/* Re-enable IRQs. */
		spin_lock_irq(&dev->wl->irq_lock);
		b43legacy_interrupt_enable(dev, dev->irq_savedstate);
		spin_unlock_irq(&dev->wl->irq_lock);
	}
}

/* http://bcm-specs.sipsolutions.net/SuspendMAC */
void b43legacy_mac_suspend(struct b43legacy_wldev *dev)
{
	int i;
	u32 tmp;

	might_sleep();
	B43legacy_WARN_ON(irqs_disabled());
	B43legacy_WARN_ON(dev->mac_suspended < 0);

	if (dev->mac_suspended == 0) {
		/* Mask IRQs before suspending MAC. Otherwise
		 * the MAC stays busy and won't suspend. */
		spin_lock_irq(&dev->wl->irq_lock);
		tmp = b43legacy_interrupt_disable(dev, B43legacy_IRQ_ALL);
		spin_unlock_irq(&dev->wl->irq_lock);
		b43legacy_synchronize_irq(dev);
		dev->irq_savedstate = tmp;

		b43legacy_power_saving_ctl_bits(dev, -1, 1);
		b43legacy_write32(dev, B43legacy_MMIO_MACCTL,
				  b43legacy_read32(dev,
				  B43legacy_MMIO_MACCTL)
				  & ~B43legacy_MACCTL_ENABLED);
		b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);
		for (i = 40; i; i--) {
			tmp = b43legacy_read32(dev,
					       B43legacy_MMIO_GEN_IRQ_REASON);
			if (tmp & B43legacy_IRQ_MAC_SUSPENDED)
				goto out;
			msleep(1);
		}
		b43legacyerr(dev->wl, "MAC suspend failed\n");
	}
out:
	dev->mac_suspended++;
}

static void b43legacy_adjust_opmode(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	u32 ctl;
	u16 cfp_pretbtt;

	ctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	/* Reset status to STA infrastructure mode. */
	ctl &= ~B43legacy_MACCTL_AP;
	ctl &= ~B43legacy_MACCTL_KEEP_CTL;
	ctl &= ~B43legacy_MACCTL_KEEP_BADPLCP;
	ctl &= ~B43legacy_MACCTL_KEEP_BAD;
	ctl &= ~B43legacy_MACCTL_PROMISC;
	ctl &= ~B43legacy_MACCTL_BEACPROMISC;
	ctl |= B43legacy_MACCTL_INFRA;

	if (b43legacy_is_mode(wl, IEEE80211_IF_TYPE_AP))
		ctl |= B43legacy_MACCTL_AP;
	else if (b43legacy_is_mode(wl, IEEE80211_IF_TYPE_IBSS))
		ctl &= ~B43legacy_MACCTL_INFRA;

	if (wl->filter_flags & FIF_CONTROL)
		ctl |= B43legacy_MACCTL_KEEP_CTL;
	if (wl->filter_flags & FIF_FCSFAIL)
		ctl |= B43legacy_MACCTL_KEEP_BAD;
	if (wl->filter_flags & FIF_PLCPFAIL)
		ctl |= B43legacy_MACCTL_KEEP_BADPLCP;
	if (wl->filter_flags & FIF_PROMISC_IN_BSS)
		ctl |= B43legacy_MACCTL_PROMISC;
	if (wl->filter_flags & FIF_BCN_PRBRESP_PROMISC)
		ctl |= B43legacy_MACCTL_BEACPROMISC;

	/* Workaround: On old hardware the HW-MAC-address-filter
	 * doesn't work properly, so always run promisc in filter
	 * it in software. */
	if (dev->dev->id.revision <= 4)
		ctl |= B43legacy_MACCTL_PROMISC;

	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, ctl);

	cfp_pretbtt = 2;
	if ((ctl & B43legacy_MACCTL_INFRA) &&
	    !(ctl & B43legacy_MACCTL_AP)) {
		if (dev->dev->bus->chip_id == 0x4306 &&
		    dev->dev->bus->chip_rev == 3)
			cfp_pretbtt = 100;
		else
			cfp_pretbtt = 50;
	}
	b43legacy_write16(dev, 0x612, cfp_pretbtt);
}

static void b43legacy_rate_memory_write(struct b43legacy_wldev *dev,
					u16 rate,
					int is_ofdm)
{
	u16 offset;

	if (is_ofdm) {
		offset = 0x480;
		offset += (b43legacy_plcp_get_ratecode_ofdm(rate) & 0x000F) * 2;
	} else {
		offset = 0x4C0;
		offset += (b43legacy_plcp_get_ratecode_cck(rate) & 0x000F) * 2;
	}
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, offset + 0x20,
			      b43legacy_shm_read16(dev,
			      B43legacy_SHM_SHARED, offset));
}

static void b43legacy_rate_memory_init(struct b43legacy_wldev *dev)
{
	switch (dev->phy.type) {
	case B43legacy_PHYTYPE_G:
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_6MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_12MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_18MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_24MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_36MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_48MB, 1);
		b43legacy_rate_memory_write(dev, B43legacy_OFDM_RATE_54MB, 1);
		/* fallthrough */
	case B43legacy_PHYTYPE_B:
		b43legacy_rate_memory_write(dev, B43legacy_CCK_RATE_1MB, 0);
		b43legacy_rate_memory_write(dev, B43legacy_CCK_RATE_2MB, 0);
		b43legacy_rate_memory_write(dev, B43legacy_CCK_RATE_5MB, 0);
		b43legacy_rate_memory_write(dev, B43legacy_CCK_RATE_11MB, 0);
		break;
	default:
		B43legacy_BUG_ON(1);
	}
}

/* Set the TX-Antenna for management frames sent by firmware. */
static void b43legacy_mgmtframe_txantenna(struct b43legacy_wldev *dev,
					  int antenna)
{
	u16 ant = 0;
	u16 tmp;

	switch (antenna) {
	case B43legacy_ANTENNA0:
		ant |= B43legacy_TX4_PHY_ANT0;
		break;
	case B43legacy_ANTENNA1:
		ant |= B43legacy_TX4_PHY_ANT1;
		break;
	case B43legacy_ANTENNA_AUTO:
		ant |= B43legacy_TX4_PHY_ANTLAST;
		break;
	default:
		B43legacy_BUG_ON(1);
	}

	/* FIXME We also need to set the other flags of the PHY control
	 * field somewhere. */

	/* For Beacons */
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				   B43legacy_SHM_SH_BEACPHYCTL);
	tmp = (tmp & ~B43legacy_TX4_PHY_ANT) | ant;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_BEACPHYCTL, tmp);
	/* For ACK/CTS */
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				   B43legacy_SHM_SH_ACKCTSPHYCTL);
	tmp = (tmp & ~B43legacy_TX4_PHY_ANT) | ant;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_ACKCTSPHYCTL, tmp);
	/* For Probe Resposes */
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
				   B43legacy_SHM_SH_PRPHYCTL);
	tmp = (tmp & ~B43legacy_TX4_PHY_ANT) | ant;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_PRPHYCTL, tmp);
}

/* This is the opposite of b43legacy_chip_init() */
static void b43legacy_chip_exit(struct b43legacy_wldev *dev)
{
	b43legacy_radio_turn_off(dev, 1);
	b43legacy_gpio_cleanup(dev);
	/* firmware is released later */
}

/* Initialize the chip
 * http://bcm-specs.sipsolutions.net/ChipInit
 */
static int b43legacy_chip_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	int err;
	int tmp;
	u32 value32, macctl;
	u16 value16;

	/* Initialize the MAC control */
	macctl = B43legacy_MACCTL_IHR_ENABLED | B43legacy_MACCTL_SHM_ENABLED;
	if (dev->phy.gmode)
		macctl |= B43legacy_MACCTL_GMODE;
	macctl |= B43legacy_MACCTL_INFRA;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);

	err = b43legacy_request_firmware(dev);
	if (err)
		goto out;
	err = b43legacy_upload_microcode(dev);
	if (err)
		goto out; /* firmware is released later */

	err = b43legacy_gpio_init(dev);
	if (err)
		goto out; /* firmware is released later */

	err = b43legacy_upload_initvals(dev);
	if (err)
		goto err_gpio_clean;
	b43legacy_radio_turn_on(dev);

	b43legacy_write16(dev, 0x03E6, 0x0000);
	err = b43legacy_phy_init(dev);
	if (err)
		goto err_radio_off;

	/* Select initial Interference Mitigation. */
	tmp = phy->interfmode;
	phy->interfmode = B43legacy_INTERFMODE_NONE;
	b43legacy_radio_set_interference_mitigation(dev, tmp);

	b43legacy_phy_set_antenna_diversity(dev);
	b43legacy_mgmtframe_txantenna(dev, B43legacy_ANTENNA_DEFAULT);

	if (phy->type == B43legacy_PHYTYPE_B) {
		value16 = b43legacy_read16(dev, 0x005E);
		value16 |= 0x0004;
		b43legacy_write16(dev, 0x005E, value16);
	}
	b43legacy_write32(dev, 0x0100, 0x01000000);
	if (dev->dev->id.revision < 5)
		b43legacy_write32(dev, 0x010C, 0x01000000);

	value32 = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	value32 &= ~B43legacy_MACCTL_INFRA;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, value32);
	value32 = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	value32 |= B43legacy_MACCTL_INFRA;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, value32);

	if (b43legacy_using_pio(dev)) {
		b43legacy_write32(dev, 0x0210, 0x00000100);
		b43legacy_write32(dev, 0x0230, 0x00000100);
		b43legacy_write32(dev, 0x0250, 0x00000100);
		b43legacy_write32(dev, 0x0270, 0x00000100);
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0034,
				      0x0000);
	}

	/* Probe Response Timeout value */
	/* FIXME: Default to 0, has to be set by ioctl probably... :-/ */
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0074, 0x0000);

	/* Initially set the wireless operation mode. */
	b43legacy_adjust_opmode(dev);

	if (dev->dev->id.revision < 3) {
		b43legacy_write16(dev, 0x060E, 0x0000);
		b43legacy_write16(dev, 0x0610, 0x8000);
		b43legacy_write16(dev, 0x0604, 0x0000);
		b43legacy_write16(dev, 0x0606, 0x0200);
	} else {
		b43legacy_write32(dev, 0x0188, 0x80000000);
		b43legacy_write32(dev, 0x018C, 0x02000000);
	}
	b43legacy_write32(dev, B43legacy_MMIO_GEN_IRQ_REASON, 0x00004000);
	b43legacy_write32(dev, B43legacy_MMIO_DMA0_IRQ_MASK, 0x0001DC00);
	b43legacy_write32(dev, B43legacy_MMIO_DMA1_IRQ_MASK, 0x0000DC00);
	b43legacy_write32(dev, B43legacy_MMIO_DMA2_IRQ_MASK, 0x0000DC00);
	b43legacy_write32(dev, B43legacy_MMIO_DMA3_IRQ_MASK, 0x0001DC00);
	b43legacy_write32(dev, B43legacy_MMIO_DMA4_IRQ_MASK, 0x0000DC00);
	b43legacy_write32(dev, B43legacy_MMIO_DMA5_IRQ_MASK, 0x0000DC00);

	value32 = ssb_read32(dev->dev, SSB_TMSLOW);
	value32 |= 0x00100000;
	ssb_write32(dev->dev, SSB_TMSLOW, value32);

	b43legacy_write16(dev, B43legacy_MMIO_POWERUP_DELAY,
			  dev->dev->bus->chipco.fast_pwrup_delay);

	/* PHY TX errors counter. */
	atomic_set(&phy->txerr_cnt, B43legacy_PHY_TX_BADNESS_LIMIT);

	B43legacy_WARN_ON(err != 0);
	b43legacydbg(dev->wl, "Chip initialized\n");
out:
	return err;

err_radio_off:
	b43legacy_radio_turn_off(dev, 1);
err_gpio_clean:
	b43legacy_gpio_cleanup(dev);
	goto out;
}

static void b43legacy_periodic_every120sec(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (phy->type != B43legacy_PHYTYPE_G || phy->rev < 2)
		return;

	b43legacy_mac_suspend(dev);
	b43legacy_phy_lo_g_measure(dev);
	b43legacy_mac_enable(dev);
}

static void b43legacy_periodic_every60sec(struct b43legacy_wldev *dev)
{
	b43legacy_phy_lo_mark_all_unused(dev);
	if (dev->dev->bus->sprom.boardflags_lo & B43legacy_BFL_RSSI) {
		b43legacy_mac_suspend(dev);
		b43legacy_calc_nrssi_slope(dev);
		b43legacy_mac_enable(dev);
	}
}

static void b43legacy_periodic_every30sec(struct b43legacy_wldev *dev)
{
	/* Update device statistics. */
	b43legacy_calculate_link_quality(dev);
}

static void b43legacy_periodic_every15sec(struct b43legacy_wldev *dev)
{
	b43legacy_phy_xmitpower(dev); /* FIXME: unless scanning? */

	atomic_set(&dev->phy.txerr_cnt, B43legacy_PHY_TX_BADNESS_LIMIT);
	wmb();
}

static void do_periodic_work(struct b43legacy_wldev *dev)
{
	unsigned int state;

	state = dev->periodic_state;
	if (state % 8 == 0)
		b43legacy_periodic_every120sec(dev);
	if (state % 4 == 0)
		b43legacy_periodic_every60sec(dev);
	if (state % 2 == 0)
		b43legacy_periodic_every30sec(dev);
	b43legacy_periodic_every15sec(dev);
}

/* Periodic work locking policy:
 * 	The whole periodic work handler is protected by
 * 	wl->mutex. If another lock is needed somewhere in the
 * 	pwork callchain, it's aquired in-place, where it's needed.
 */
static void b43legacy_periodic_work_handler(struct work_struct *work)
{
	struct b43legacy_wldev *dev = container_of(work, struct b43legacy_wldev,
					     periodic_work.work);
	struct b43legacy_wl *wl = dev->wl;
	unsigned long delay;

	mutex_lock(&wl->mutex);

	if (unlikely(b43legacy_status(dev) != B43legacy_STAT_STARTED))
		goto out;
	if (b43legacy_debug(dev, B43legacy_DBG_PWORK_STOP))
		goto out_requeue;

	do_periodic_work(dev);

	dev->periodic_state++;
out_requeue:
	if (b43legacy_debug(dev, B43legacy_DBG_PWORK_FAST))
		delay = msecs_to_jiffies(50);
	else
		delay = round_jiffies_relative(HZ * 15);
	queue_delayed_work(wl->hw->workqueue, &dev->periodic_work, delay);
out:
	mutex_unlock(&wl->mutex);
}

static void b43legacy_periodic_tasks_setup(struct b43legacy_wldev *dev)
{
	struct delayed_work *work = &dev->periodic_work;

	dev->periodic_state = 0;
	INIT_DELAYED_WORK(work, b43legacy_periodic_work_handler);
	queue_delayed_work(dev->wl->hw->workqueue, work, 0);
}

/* Validate access to the chip (SHM) */
static int b43legacy_validate_chipaccess(struct b43legacy_wldev *dev)
{
	u32 value;
	u32 shm_backup;

	shm_backup = b43legacy_shm_read32(dev, B43legacy_SHM_SHARED, 0);
	b43legacy_shm_write32(dev, B43legacy_SHM_SHARED, 0, 0xAA5555AA);
	if (b43legacy_shm_read32(dev, B43legacy_SHM_SHARED, 0) !=
				 0xAA5555AA)
		goto error;
	b43legacy_shm_write32(dev, B43legacy_SHM_SHARED, 0, 0x55AAAA55);
	if (b43legacy_shm_read32(dev, B43legacy_SHM_SHARED, 0) !=
				 0x55AAAA55)
		goto error;
	b43legacy_shm_write32(dev, B43legacy_SHM_SHARED, 0, shm_backup);

	value = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	if ((value | B43legacy_MACCTL_GMODE) !=
	    (B43legacy_MACCTL_GMODE | B43legacy_MACCTL_IHR_ENABLED))
		goto error;

	value = b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_REASON);
	if (value)
		goto error;

	return 0;
error:
	b43legacyerr(dev->wl, "Failed to validate the chipaccess\n");
	return -ENODEV;
}

static void b43legacy_security_init(struct b43legacy_wldev *dev)
{
	dev->max_nr_keys = (dev->dev->id.revision >= 5) ? 58 : 20;
	B43legacy_WARN_ON(dev->max_nr_keys > ARRAY_SIZE(dev->key));
	dev->ktp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED,
					0x0056);
	/* KTP is a word address, but we address SHM bytewise.
	 * So multiply by two.
	 */
	dev->ktp *= 2;
	if (dev->dev->id.revision >= 5)
		/* Number of RCMTA address slots */
		b43legacy_write16(dev, B43legacy_MMIO_RCMTA_COUNT,
				  dev->max_nr_keys - 8);
}

static int b43legacy_rng_read(struct hwrng *rng, u32 *data)
{
	struct b43legacy_wl *wl = (struct b43legacy_wl *)rng->priv;
	unsigned long flags;

	/* Don't take wl->mutex here, as it could deadlock with
	 * hwrng internal locking. It's not needed to take
	 * wl->mutex here, anyway. */

	spin_lock_irqsave(&wl->irq_lock, flags);
	*data = b43legacy_read16(wl->current_dev, B43legacy_MMIO_RNG);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return (sizeof(u16));
}

static void b43legacy_rng_exit(struct b43legacy_wl *wl)
{
	if (wl->rng_initialized)
		hwrng_unregister(&wl->rng);
}

static int b43legacy_rng_init(struct b43legacy_wl *wl)
{
	int err;

	snprintf(wl->rng_name, ARRAY_SIZE(wl->rng_name),
		 "%s_%s", KBUILD_MODNAME, wiphy_name(wl->hw->wiphy));
	wl->rng.name = wl->rng_name;
	wl->rng.data_read = b43legacy_rng_read;
	wl->rng.priv = (unsigned long)wl;
	wl->rng_initialized = 1;
	err = hwrng_register(&wl->rng);
	if (err) {
		wl->rng_initialized = 0;
		b43legacyerr(wl, "Failed to register the random "
		       "number generator (%d)\n", err);
	}

	return err;
}

static int b43legacy_op_tx(struct ieee80211_hw *hw,
			   struct sk_buff *skb,
			   struct ieee80211_tx_control *ctl)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	int err = -ENODEV;
	unsigned long flags;

	if (unlikely(!dev))
		goto out;
	if (unlikely(b43legacy_status(dev) < B43legacy_STAT_STARTED))
		goto out;
	/* DMA-TX is done without a global lock. */
	if (b43legacy_using_pio(dev)) {
		spin_lock_irqsave(&wl->irq_lock, flags);
		err = b43legacy_pio_tx(dev, skb, ctl);
		spin_unlock_irqrestore(&wl->irq_lock, flags);
	} else
		err = b43legacy_dma_tx(dev, skb, ctl);
out:
	if (unlikely(err))
		return NETDEV_TX_BUSY;
	return NETDEV_TX_OK;
}

static int b43legacy_op_conf_tx(struct ieee80211_hw *hw,
				int queue,
				const struct ieee80211_tx_queue_params *params)
{
	return 0;
}

static int b43legacy_op_get_tx_stats(struct ieee80211_hw *hw,
				     struct ieee80211_tx_queue_stats *stats)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	unsigned long flags;
	int err = -ENODEV;

	if (!dev)
		goto out;
	spin_lock_irqsave(&wl->irq_lock, flags);
	if (likely(b43legacy_status(dev) >= B43legacy_STAT_STARTED)) {
		if (b43legacy_using_pio(dev))
			b43legacy_pio_get_tx_stats(dev, stats);
		else
			b43legacy_dma_get_tx_stats(dev, stats);
		err = 0;
	}
	spin_unlock_irqrestore(&wl->irq_lock, flags);
out:
	return err;
}

static int b43legacy_op_get_stats(struct ieee80211_hw *hw,
				  struct ieee80211_low_level_stats *stats)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	unsigned long flags;

	spin_lock_irqsave(&wl->irq_lock, flags);
	memcpy(stats, &wl->ieee_stats, sizeof(*stats));
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	return 0;
}

static const char *phymode_to_string(unsigned int phymode)
{
	switch (phymode) {
	case B43legacy_PHYMODE_B:
		return "B";
	case B43legacy_PHYMODE_G:
		return "G";
	default:
		B43legacy_BUG_ON(1);
	}
	return "";
}

static int find_wldev_for_phymode(struct b43legacy_wl *wl,
				  unsigned int phymode,
				  struct b43legacy_wldev **dev,
				  bool *gmode)
{
	struct b43legacy_wldev *d;

	list_for_each_entry(d, &wl->devlist, list) {
		if (d->phy.possible_phymodes & phymode) {
			/* Ok, this device supports the PHY-mode.
			 * Set the gmode bit. */
			*gmode = 1;
			*dev = d;

			return 0;
		}
	}

	return -ESRCH;
}

static void b43legacy_put_phy_into_reset(struct b43legacy_wldev *dev)
{
	struct ssb_device *sdev = dev->dev;
	u32 tmslow;

	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmslow &= ~B43legacy_TMSLOW_GMODE;
	tmslow |= B43legacy_TMSLOW_PHYRESET;
	tmslow |= SSB_TMSLOW_FGC;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	msleep(1);

	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmslow &= ~SSB_TMSLOW_FGC;
	tmslow |= B43legacy_TMSLOW_PHYRESET;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	msleep(1);
}

/* Expects wl->mutex locked */
static int b43legacy_switch_phymode(struct b43legacy_wl *wl,
				      unsigned int new_mode)
{
	struct b43legacy_wldev *up_dev;
	struct b43legacy_wldev *down_dev;
	int err;
	bool gmode = 0;
	int prev_status;

	err = find_wldev_for_phymode(wl, new_mode, &up_dev, &gmode);
	if (err) {
		b43legacyerr(wl, "Could not find a device for %s-PHY mode\n",
		       phymode_to_string(new_mode));
		return err;
	}
	if ((up_dev == wl->current_dev) &&
	    (!!wl->current_dev->phy.gmode == !!gmode))
		/* This device is already running. */
		return 0;
	b43legacydbg(wl, "Reconfiguring PHYmode to %s-PHY\n",
	       phymode_to_string(new_mode));
	down_dev = wl->current_dev;

	prev_status = b43legacy_status(down_dev);
	/* Shutdown the currently running core. */
	if (prev_status >= B43legacy_STAT_STARTED)
		b43legacy_wireless_core_stop(down_dev);
	if (prev_status >= B43legacy_STAT_INITIALIZED)
		b43legacy_wireless_core_exit(down_dev);

	if (down_dev != up_dev)
		/* We switch to a different core, so we put PHY into
		 * RESET on the old core. */
		b43legacy_put_phy_into_reset(down_dev);

	/* Now start the new core. */
	up_dev->phy.gmode = gmode;
	if (prev_status >= B43legacy_STAT_INITIALIZED) {
		err = b43legacy_wireless_core_init(up_dev);
		if (err) {
			b43legacyerr(wl, "Fatal: Could not initialize device"
				     " for newly selected %s-PHY mode\n",
				     phymode_to_string(new_mode));
			goto init_failure;
		}
	}
	if (prev_status >= B43legacy_STAT_STARTED) {
		err = b43legacy_wireless_core_start(up_dev);
		if (err) {
			b43legacyerr(wl, "Fatal: Coult not start device for "
			       "newly selected %s-PHY mode\n",
			       phymode_to_string(new_mode));
			b43legacy_wireless_core_exit(up_dev);
			goto init_failure;
		}
	}
	B43legacy_WARN_ON(b43legacy_status(up_dev) != prev_status);

	b43legacy_shm_write32(up_dev, B43legacy_SHM_SHARED, 0x003E, 0);

	wl->current_dev = up_dev;

	return 0;
init_failure:
	/* Whoops, failed to init the new core. No core is operating now. */
	wl->current_dev = NULL;
	return err;
}

static int b43legacy_antenna_from_ieee80211(u8 antenna)
{
	switch (antenna) {
	case 0: /* default/diversity */
		return B43legacy_ANTENNA_DEFAULT;
	case 1: /* Antenna 0 */
		return B43legacy_ANTENNA0;
	case 2: /* Antenna 1 */
		return B43legacy_ANTENNA1;
	default:
		return B43legacy_ANTENNA_DEFAULT;
	}
}

static int b43legacy_op_dev_config(struct ieee80211_hw *hw,
				   struct ieee80211_conf *conf)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev;
	struct b43legacy_phy *phy;
	unsigned long flags;
	unsigned int new_phymode = 0xFFFF;
	int antenna_tx;
	int antenna_rx;
	int err = 0;
	u32 savedirqs;

	antenna_tx = b43legacy_antenna_from_ieee80211(conf->antenna_sel_tx);
	antenna_rx = b43legacy_antenna_from_ieee80211(conf->antenna_sel_rx);

	mutex_lock(&wl->mutex);

	/* Switch the PHY mode (if necessary). */
	switch (conf->phymode) {
	case MODE_IEEE80211B:
		new_phymode = B43legacy_PHYMODE_B;
		break;
	case MODE_IEEE80211G:
		new_phymode = B43legacy_PHYMODE_G;
		break;
	default:
		B43legacy_WARN_ON(1);
	}
	err = b43legacy_switch_phymode(wl, new_phymode);
	if (err)
		goto out_unlock_mutex;
	dev = wl->current_dev;
	phy = &dev->phy;

	/* Disable IRQs while reconfiguring the device.
	 * This makes it possible to drop the spinlock throughout
	 * the reconfiguration process. */
	spin_lock_irqsave(&wl->irq_lock, flags);
	if (b43legacy_status(dev) < B43legacy_STAT_STARTED) {
		spin_unlock_irqrestore(&wl->irq_lock, flags);
		goto out_unlock_mutex;
	}
	savedirqs = b43legacy_interrupt_disable(dev, B43legacy_IRQ_ALL);
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	b43legacy_synchronize_irq(dev);

	/* Switch to the requested channel.
	 * The firmware takes care of races with the TX handler. */
	if (conf->channel_val != phy->channel)
		b43legacy_radio_selectchannel(dev, conf->channel_val, 0);

	/* Enable/Disable ShortSlot timing. */
	if ((!!(conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME))
	     != dev->short_slot) {
		B43legacy_WARN_ON(phy->type != B43legacy_PHYTYPE_G);
		if (conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME)
			b43legacy_short_slot_timing_enable(dev);
		else
			b43legacy_short_slot_timing_disable(dev);
	}

	dev->wl->radiotap_enabled = !!(conf->flags & IEEE80211_CONF_RADIOTAP);

	/* Adjust the desired TX power level. */
	if (conf->power_level != 0) {
		if (conf->power_level != phy->power_level) {
			phy->power_level = conf->power_level;
			b43legacy_phy_xmitpower(dev);
		}
	}

	/* Antennas for RX and management frame TX. */
	b43legacy_mgmtframe_txantenna(dev, antenna_tx);

	/* Update templates for AP mode. */
	if (b43legacy_is_mode(wl, IEEE80211_IF_TYPE_AP))
		b43legacy_set_beacon_int(dev, conf->beacon_int);


	if (!!conf->radio_enabled != phy->radio_on) {
		if (conf->radio_enabled) {
			b43legacy_radio_turn_on(dev);
			b43legacyinfo(dev->wl, "Radio turned on by software\n");
			if (!dev->radio_hw_enable)
				b43legacyinfo(dev->wl, "The hardware RF-kill"
					      " button still turns the radio"
					      " physically off. Press the"
					      " button to turn it on.\n");
		} else {
			b43legacy_radio_turn_off(dev, 0);
			b43legacyinfo(dev->wl, "Radio turned off by"
				      " software\n");
		}
	}

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43legacy_interrupt_enable(dev, savedirqs);
	mmiowb();
	spin_unlock_irqrestore(&wl->irq_lock, flags);
out_unlock_mutex:
	mutex_unlock(&wl->mutex);

	return err;
}

static void b43legacy_op_configure_filter(struct ieee80211_hw *hw,
					  unsigned int changed,
					  unsigned int *fflags,
					  int mc_count,
					  struct dev_addr_list *mc_list)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
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

	if (changed && b43legacy_status(dev) >= B43legacy_STAT_INITIALIZED)
		b43legacy_adjust_opmode(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);
}

static int b43legacy_op_config_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_if_conf *conf)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	unsigned long flags;

	if (!dev)
		return -ENODEV;
	mutex_lock(&wl->mutex);
	spin_lock_irqsave(&wl->irq_lock, flags);
	B43legacy_WARN_ON(wl->vif != vif);
	if (conf->bssid)
		memcpy(wl->bssid, conf->bssid, ETH_ALEN);
	else
		memset(wl->bssid, 0, ETH_ALEN);
	if (b43legacy_status(dev) >= B43legacy_STAT_INITIALIZED) {
		if (b43legacy_is_mode(wl, IEEE80211_IF_TYPE_AP)) {
			B43legacy_WARN_ON(conf->type != IEEE80211_IF_TYPE_AP);
			b43legacy_set_ssid(dev, conf->ssid, conf->ssid_len);
			if (conf->beacon)
				b43legacy_refresh_templates(dev, conf->beacon);
		}
		b43legacy_write_mac_bssid_templates(dev);
	}
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	mutex_unlock(&wl->mutex);

	return 0;
}

/* Locking: wl->mutex */
static void b43legacy_wireless_core_stop(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	unsigned long flags;

	if (b43legacy_status(dev) < B43legacy_STAT_STARTED)
		return;

	/* Disable and sync interrupts. We must do this before than
	 * setting the status to INITIALIZED, as the interrupt handler
	 * won't care about IRQs then. */
	spin_lock_irqsave(&wl->irq_lock, flags);
	dev->irq_savedstate = b43legacy_interrupt_disable(dev,
							  B43legacy_IRQ_ALL);
	b43legacy_read32(dev, B43legacy_MMIO_GEN_IRQ_MASK); /* flush */
	spin_unlock_irqrestore(&wl->irq_lock, flags);
	b43legacy_synchronize_irq(dev);

	b43legacy_set_status(dev, B43legacy_STAT_INITIALIZED);

	mutex_unlock(&wl->mutex);
	/* Must unlock as it would otherwise deadlock. No races here.
	 * Cancel the possibly running self-rearming periodic work. */
	cancel_delayed_work_sync(&dev->periodic_work);
	mutex_lock(&wl->mutex);

	ieee80211_stop_queues(wl->hw); /* FIXME this could cause a deadlock */

	b43legacy_mac_suspend(dev);
	free_irq(dev->dev->irq, dev);
	b43legacydbg(wl, "Wireless interface stopped\n");
}

/* Locking: wl->mutex */
static int b43legacy_wireless_core_start(struct b43legacy_wldev *dev)
{
	int err;

	B43legacy_WARN_ON(b43legacy_status(dev) != B43legacy_STAT_INITIALIZED);

	drain_txstatus_queue(dev);
	err = request_irq(dev->dev->irq, b43legacy_interrupt_handler,
			  IRQF_SHARED, KBUILD_MODNAME, dev);
	if (err) {
		b43legacyerr(dev->wl, "Cannot request IRQ-%d\n",
		       dev->dev->irq);
		goto out;
	}
	/* We are ready to run. */
	b43legacy_set_status(dev, B43legacy_STAT_STARTED);

	/* Start data flow (TX/RX) */
	b43legacy_mac_enable(dev);
	b43legacy_interrupt_enable(dev, dev->irq_savedstate);
	ieee80211_start_queues(dev->wl->hw);

	/* Start maintenance work */
	b43legacy_periodic_tasks_setup(dev);

	b43legacydbg(dev->wl, "Wireless interface started\n");
out:
	return err;
}

/* Get PHY and RADIO versioning numbers */
static int b43legacy_phy_versioning(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u32 tmp;
	u8 analog_type;
	u8 phy_type;
	u8 phy_rev;
	u16 radio_manuf;
	u16 radio_ver;
	u16 radio_rev;
	int unsupported = 0;

	/* Get PHY versioning */
	tmp = b43legacy_read16(dev, B43legacy_MMIO_PHY_VER);
	analog_type = (tmp & B43legacy_PHYVER_ANALOG)
		      >> B43legacy_PHYVER_ANALOG_SHIFT;
	phy_type = (tmp & B43legacy_PHYVER_TYPE) >> B43legacy_PHYVER_TYPE_SHIFT;
	phy_rev = (tmp & B43legacy_PHYVER_VERSION);
	switch (phy_type) {
	case B43legacy_PHYTYPE_B:
		if (phy_rev != 2 && phy_rev != 4
		    && phy_rev != 6 && phy_rev != 7)
			unsupported = 1;
		break;
	case B43legacy_PHYTYPE_G:
		if (phy_rev > 8)
			unsupported = 1;
		break;
	default:
		unsupported = 1;
	};
	if (unsupported) {
		b43legacyerr(dev->wl, "FOUND UNSUPPORTED PHY "
		       "(Analog %u, Type %u, Revision %u)\n",
		       analog_type, phy_type, phy_rev);
		return -EOPNOTSUPP;
	}
	b43legacydbg(dev->wl, "Found PHY: Analog %u, Type %u, Revision %u\n",
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
		b43legacy_write16(dev, B43legacy_MMIO_RADIO_CONTROL,
				  B43legacy_RADIOCTL_ID);
		tmp = b43legacy_read16(dev, B43legacy_MMIO_RADIO_DATA_HIGH);
		tmp <<= 16;
		b43legacy_write16(dev, B43legacy_MMIO_RADIO_CONTROL,
				  B43legacy_RADIOCTL_ID);
		tmp |= b43legacy_read16(dev, B43legacy_MMIO_RADIO_DATA_LOW);
	}
	radio_manuf = (tmp & 0x00000FFF);
	radio_ver = (tmp & 0x0FFFF000) >> 12;
	radio_rev = (tmp & 0xF0000000) >> 28;
	switch (phy_type) {
	case B43legacy_PHYTYPE_B:
		if ((radio_ver & 0xFFF0) != 0x2050)
			unsupported = 1;
		break;
	case B43legacy_PHYTYPE_G:
		if (radio_ver != 0x2050)
			unsupported = 1;
		break;
	default:
		B43legacy_BUG_ON(1);
	}
	if (unsupported) {
		b43legacyerr(dev->wl, "FOUND UNSUPPORTED RADIO "
		       "(Manuf 0x%X, Version 0x%X, Revision %u)\n",
		       radio_manuf, radio_ver, radio_rev);
		return -EOPNOTSUPP;
	}
	b43legacydbg(dev->wl, "Found Radio: Manuf 0x%X, Version 0x%X,"
		     " Revision %u\n", radio_manuf, radio_ver, radio_rev);


	phy->radio_manuf = radio_manuf;
	phy->radio_ver = radio_ver;
	phy->radio_rev = radio_rev;

	phy->analog = analog_type;
	phy->type = phy_type;
	phy->rev = phy_rev;

	return 0;
}

static void setup_struct_phy_for_init(struct b43legacy_wldev *dev,
				      struct b43legacy_phy *phy)
{
	struct b43legacy_lopair *lo;
	int i;

	memset(phy->minlowsig, 0xFF, sizeof(phy->minlowsig));
	memset(phy->minlowsigpos, 0, sizeof(phy->minlowsigpos));

	/* Assume the radio is enabled. If it's not enabled, the state will
	 * immediately get fixed on the first periodic work run. */
	dev->radio_hw_enable = 1;

	phy->savedpctlreg = 0xFFFF;
	phy->aci_enable = 0;
	phy->aci_wlan_automatic = 0;
	phy->aci_hw_rssi = 0;

	lo = phy->_lo_pairs;
	if (lo)
		memset(lo, 0, sizeof(struct b43legacy_lopair) *
				     B43legacy_LO_COUNT);
	phy->max_lb_gain = 0;
	phy->trsw_rx_gain = 0;

	/* Set default attenuation values. */
	phy->bbatt = b43legacy_default_baseband_attenuation(dev);
	phy->rfatt = b43legacy_default_radio_attenuation(dev);
	phy->txctl1 = b43legacy_default_txctl1(dev);
	phy->txpwr_offset = 0;

	/* NRSSI */
	phy->nrssislope = 0;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi); i++)
		phy->nrssi[i] = -1000;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi_lt); i++)
		phy->nrssi_lt[i] = i;

	phy->lofcal = 0xFFFF;
	phy->initval = 0xFFFF;

	phy->interfmode = B43legacy_INTERFMODE_NONE;
	phy->channel = 0xFF;
}

static void setup_struct_wldev_for_init(struct b43legacy_wldev *dev)
{
	/* Flags */
	dev->reg124_set_0x4 = 0;

	/* Stats */
	memset(&dev->stats, 0, sizeof(dev->stats));

	setup_struct_phy_for_init(dev, &dev->phy);

	/* IRQ related flags */
	dev->irq_reason = 0;
	memset(dev->dma_reason, 0, sizeof(dev->dma_reason));
	dev->irq_savedstate = B43legacy_IRQ_MASKTEMPLATE;

	dev->mac_suspended = 1;

	/* Noise calculation context */
	memset(&dev->noisecalc, 0, sizeof(dev->noisecalc));
}

static void b43legacy_imcfglo_timeouts_workaround(struct b43legacy_wldev *dev)
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
static void b43legacy_set_retry_limits(struct b43legacy_wldev *dev,
				       unsigned int short_retry,
				       unsigned int long_retry)
{
	/* The retry limit is a 4-bit counter. Enforce this to avoid overflowing
	 * the chip-internal counter. */
	short_retry = min(short_retry, (unsigned int)0xF);
	long_retry = min(long_retry, (unsigned int)0xF);

	b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS, 0x0006, short_retry);
	b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS, 0x0007, long_retry);
}

/* Shutdown a wireless core */
/* Locking: wl->mutex */
static void b43legacy_wireless_core_exit(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	struct b43legacy_phy *phy = &dev->phy;
	u32 macctl;

	B43legacy_WARN_ON(b43legacy_status(dev) > B43legacy_STAT_INITIALIZED);
	if (b43legacy_status(dev) != B43legacy_STAT_INITIALIZED)
		return;
	b43legacy_set_status(dev, B43legacy_STAT_UNINIT);

	/* Stop the microcode PSM. */
	macctl = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	macctl &= ~B43legacy_MACCTL_PSM_RUN;
	macctl |= B43legacy_MACCTL_PSM_JMP0;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, macctl);

	mutex_unlock(&wl->mutex);
	/* Must unlock as it would otherwise deadlock. No races here.
	 * Cancel possibly pending workqueues. */
	cancel_work_sync(&dev->restart_work);
	mutex_lock(&wl->mutex);

	b43legacy_leds_exit(dev);
	b43legacy_rng_exit(dev->wl);
	b43legacy_pio_free(dev);
	b43legacy_dma_free(dev);
	b43legacy_chip_exit(dev);
	b43legacy_radio_turn_off(dev, 1);
	b43legacy_switch_analog(dev, 0);
	if (phy->dyn_tssi_tbl)
		kfree(phy->tssi2dbm);
	kfree(phy->lo_control);
	phy->lo_control = NULL;
	ssb_device_disable(dev->dev, 0);
	ssb_bus_may_powerdown(dev->dev->bus);
}

static void prepare_phy_data_for_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	int i;

	/* Set default attenuation values. */
	phy->bbatt = b43legacy_default_baseband_attenuation(dev);
	phy->rfatt = b43legacy_default_radio_attenuation(dev);
	phy->txctl1 = b43legacy_default_txctl1(dev);
	phy->txctl2 = 0xFFFF;
	phy->txpwr_offset = 0;

	/* NRSSI */
	phy->nrssislope = 0;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi); i++)
		phy->nrssi[i] = -1000;
	for (i = 0; i < ARRAY_SIZE(phy->nrssi_lt); i++)
		phy->nrssi_lt[i] = i;

	phy->lofcal = 0xFFFF;
	phy->initval = 0xFFFF;

	phy->aci_enable = 0;
	phy->aci_wlan_automatic = 0;
	phy->aci_hw_rssi = 0;

	phy->antenna_diversity = 0xFFFF;
	memset(phy->minlowsig, 0xFF, sizeof(phy->minlowsig));
	memset(phy->minlowsigpos, 0, sizeof(phy->minlowsigpos));

	/* Flags */
	phy->calibrated = 0;

	if (phy->_lo_pairs)
		memset(phy->_lo_pairs, 0,
		       sizeof(struct b43legacy_lopair) * B43legacy_LO_COUNT);
	memset(phy->loopback_gain, 0, sizeof(phy->loopback_gain));
}

/* Initialize a wireless core */
static int b43legacy_wireless_core_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	struct ssb_bus *bus = dev->dev->bus;
	struct b43legacy_phy *phy = &dev->phy;
	struct ssb_sprom *sprom = &dev->dev->bus->sprom;
	int err;
	u32 hf;
	u32 tmp;

	B43legacy_WARN_ON(b43legacy_status(dev) != B43legacy_STAT_UNINIT);

	err = ssb_bus_powerup(bus, 0);
	if (err)
		goto out;
	if (!ssb_device_is_enabled(dev->dev)) {
		tmp = phy->gmode ? B43legacy_TMSLOW_GMODE : 0;
		b43legacy_wireless_core_reset(dev, tmp);
	}

	if ((phy->type == B43legacy_PHYTYPE_B) ||
	    (phy->type == B43legacy_PHYTYPE_G)) {
		phy->_lo_pairs = kzalloc(sizeof(struct b43legacy_lopair)
					 * B43legacy_LO_COUNT,
					 GFP_KERNEL);
		if (!phy->_lo_pairs)
			return -ENOMEM;
	}
	setup_struct_wldev_for_init(dev);

	err = b43legacy_phy_init_tssi2dbm_table(dev);
	if (err)
		goto err_kfree_lo_control;

	/* Enable IRQ routing to this device. */
	ssb_pcicore_dev_irqvecs_enable(&bus->pcicore, dev->dev);

	b43legacy_imcfglo_timeouts_workaround(dev);
	prepare_phy_data_for_init(dev);
	b43legacy_phy_calibrate(dev);
	err = b43legacy_chip_init(dev);
	if (err)
		goto err_kfree_tssitbl;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_WLCOREREV,
			      dev->dev->id.revision);
	hf = b43legacy_hf_read(dev);
	if (phy->type == B43legacy_PHYTYPE_G) {
		hf |= B43legacy_HF_SYMW;
		if (phy->rev == 1)
			hf |= B43legacy_HF_GDCW;
		if (sprom->boardflags_lo & B43legacy_BFL_PACTRL)
			hf |= B43legacy_HF_OFDMPABOOST;
	} else if (phy->type == B43legacy_PHYTYPE_B) {
		hf |= B43legacy_HF_SYMW;
		if (phy->rev >= 2 && phy->radio_ver == 0x2050)
			hf &= ~B43legacy_HF_GDCW;
	}
	b43legacy_hf_write(dev, hf);

	b43legacy_set_retry_limits(dev,
				   B43legacy_DEFAULT_SHORT_RETRY_LIMIT,
				   B43legacy_DEFAULT_LONG_RETRY_LIMIT);

	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      0x0044, 3);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      0x0046, 2);

	/* Disable sending probe responses from firmware.
	 * Setting the MaxTime to one usec will always trigger
	 * a timeout, so we never send any probe resp.
	 * A timeout of zero is infinite. */
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED,
			      B43legacy_SHM_SH_PRMAXTIME, 1);

	b43legacy_rate_memory_init(dev);

	/* Minimum Contention Window */
	if (phy->type == B43legacy_PHYTYPE_B)
		b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS,
				      0x0003, 31);
	else
		b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS,
				      0x0003, 15);
	/* Maximum Contention Window */
	b43legacy_shm_write16(dev, B43legacy_SHM_WIRELESS,
			      0x0004, 1023);

	do {
		if (b43legacy_using_pio(dev))
			err = b43legacy_pio_init(dev);
		else {
			err = b43legacy_dma_init(dev);
			if (!err)
				b43legacy_qos_init(dev);
		}
	} while (err == -EAGAIN);
	if (err)
		goto err_chip_exit;

	b43legacy_write16(dev, 0x0612, 0x0050);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0416, 0x0050);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0414, 0x01F4);

	ssb_bus_powerup(bus, 1); /* Enable dynamic PCTL */
	b43legacy_upload_card_macaddress(dev);
	b43legacy_security_init(dev);
	b43legacy_rng_init(wl);

	b43legacy_set_status(dev, B43legacy_STAT_INITIALIZED);

	b43legacy_leds_init(dev);
out:
	return err;

err_chip_exit:
	b43legacy_chip_exit(dev);
err_kfree_tssitbl:
	if (phy->dyn_tssi_tbl)
		kfree(phy->tssi2dbm);
err_kfree_lo_control:
	kfree(phy->lo_control);
	phy->lo_control = NULL;
	ssb_bus_may_powerdown(bus);
	B43legacy_WARN_ON(b43legacy_status(dev) != B43legacy_STAT_UNINIT);
	return err;
}

static int b43legacy_op_add_interface(struct ieee80211_hw *hw,
				      struct ieee80211_if_init_conf *conf)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev;
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

	b43legacydbg(wl, "Adding Interface type %d\n", conf->type);

	dev = wl->current_dev;
	wl->operating = 1;
	wl->vif = conf->vif;
	wl->if_type = conf->type;
	memcpy(wl->mac_addr, conf->mac_addr, ETH_ALEN);

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43legacy_adjust_opmode(dev);
	b43legacy_upload_card_macaddress(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	err = 0;
 out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static void b43legacy_op_remove_interface(struct ieee80211_hw *hw,
					  struct ieee80211_if_init_conf *conf)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	unsigned long flags;

	b43legacydbg(wl, "Removing Interface type %d\n", conf->type);

	mutex_lock(&wl->mutex);

	B43legacy_WARN_ON(!wl->operating);
	B43legacy_WARN_ON(wl->vif != conf->vif);
	wl->vif = NULL;

	wl->operating = 0;

	spin_lock_irqsave(&wl->irq_lock, flags);
	b43legacy_adjust_opmode(dev);
	memset(wl->mac_addr, 0, ETH_ALEN);
	b43legacy_upload_card_macaddress(dev);
	spin_unlock_irqrestore(&wl->irq_lock, flags);

	mutex_unlock(&wl->mutex);
}

static int b43legacy_op_start(struct ieee80211_hw *hw)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	int did_init = 0;
	int err = 0;
	bool do_rfkill_exit = 0;

	/* First register RFkill.
	 * LEDs that are registered later depend on it. */
	b43legacy_rfkill_init(dev);

	/* Kill all old instance specific information to make sure
	 * the card won't use it in the short timeframe between start
	 * and mac80211 reconfiguring it. */
	memset(wl->bssid, 0, ETH_ALEN);
	memset(wl->mac_addr, 0, ETH_ALEN);
	wl->filter_flags = 0;

	mutex_lock(&wl->mutex);

	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED) {
		err = b43legacy_wireless_core_init(dev);
		if (err) {
			do_rfkill_exit = 1;
			goto out_mutex_unlock;
		}
		did_init = 1;
	}

	if (b43legacy_status(dev) < B43legacy_STAT_STARTED) {
		err = b43legacy_wireless_core_start(dev);
		if (err) {
			if (did_init)
				b43legacy_wireless_core_exit(dev);
			do_rfkill_exit = 1;
			goto out_mutex_unlock;
		}
	}

out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	if (do_rfkill_exit)
		b43legacy_rfkill_exit(dev);

	return err;
}

static void b43legacy_op_stop(struct ieee80211_hw *hw)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;

	b43legacy_rfkill_exit(dev);

	mutex_lock(&wl->mutex);
	if (b43legacy_status(dev) >= B43legacy_STAT_STARTED)
		b43legacy_wireless_core_stop(dev);
	b43legacy_wireless_core_exit(dev);
	mutex_unlock(&wl->mutex);
}

static int b43legacy_op_set_retry_limit(struct ieee80211_hw *hw,
					u32 short_retry_limit,
					u32 long_retry_limit)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev;
	int err = 0;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (unlikely(!dev ||
		     (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED))) {
		err = -ENODEV;
		goto out_unlock;
	}
	b43legacy_set_retry_limits(dev, short_retry_limit, long_retry_limit);
out_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static const struct ieee80211_ops b43legacy_hw_ops = {
	.tx			= b43legacy_op_tx,
	.conf_tx		= b43legacy_op_conf_tx,
	.add_interface		= b43legacy_op_add_interface,
	.remove_interface	= b43legacy_op_remove_interface,
	.config			= b43legacy_op_dev_config,
	.config_interface	= b43legacy_op_config_interface,
	.configure_filter	= b43legacy_op_configure_filter,
	.get_stats		= b43legacy_op_get_stats,
	.get_tx_stats		= b43legacy_op_get_tx_stats,
	.start			= b43legacy_op_start,
	.stop			= b43legacy_op_stop,
	.set_retry_limit	= b43legacy_op_set_retry_limit,
};

/* Hard-reset the chip. Do not call this directly.
 * Use b43legacy_controller_restart()
 */
static void b43legacy_chip_reset(struct work_struct *work)
{
	struct b43legacy_wldev *dev =
		container_of(work, struct b43legacy_wldev, restart_work);
	struct b43legacy_wl *wl = dev->wl;
	int err = 0;
	int prev_status;

	mutex_lock(&wl->mutex);

	prev_status = b43legacy_status(dev);
	/* Bring the device down... */
	if (prev_status >= B43legacy_STAT_STARTED)
		b43legacy_wireless_core_stop(dev);
	if (prev_status >= B43legacy_STAT_INITIALIZED)
		b43legacy_wireless_core_exit(dev);

	/* ...and up again. */
	if (prev_status >= B43legacy_STAT_INITIALIZED) {
		err = b43legacy_wireless_core_init(dev);
		if (err)
			goto out;
	}
	if (prev_status >= B43legacy_STAT_STARTED) {
		err = b43legacy_wireless_core_start(dev);
		if (err) {
			b43legacy_wireless_core_exit(dev);
			goto out;
		}
	}
out:
	mutex_unlock(&wl->mutex);
	if (err)
		b43legacyerr(wl, "Controller restart FAILED\n");
	else
		b43legacyinfo(wl, "Controller restarted\n");
}

static int b43legacy_setup_modes(struct b43legacy_wldev *dev,
				 int have_bphy,
				 int have_gphy)
{
	struct ieee80211_hw *hw = dev->wl->hw;
	struct ieee80211_hw_mode *mode;
	struct b43legacy_phy *phy = &dev->phy;
	int cnt = 0;
	int err;

	phy->possible_phymodes = 0;
	for (; 1; cnt++) {
		if (have_bphy) {
			B43legacy_WARN_ON(cnt >= B43legacy_MAX_PHYHWMODES);
			mode = &phy->hwmodes[cnt];

			mode->mode = MODE_IEEE80211B;
			mode->num_channels = b43legacy_bg_chantable_size;
			mode->channels = b43legacy_bg_chantable;
			mode->num_rates = b43legacy_b_ratetable_size;
			mode->rates = b43legacy_b_ratetable;
			err = ieee80211_register_hwmode(hw, mode);
			if (err)
				return err;

			phy->possible_phymodes |= B43legacy_PHYMODE_B;
			have_bphy = 0;
			continue;
		}
		if (have_gphy) {
			B43legacy_WARN_ON(cnt >= B43legacy_MAX_PHYHWMODES);
			mode = &phy->hwmodes[cnt];

			mode->mode = MODE_IEEE80211G;
			mode->num_channels = b43legacy_bg_chantable_size;
			mode->channels = b43legacy_bg_chantable;
			mode->num_rates = b43legacy_g_ratetable_size;
			mode->rates = b43legacy_g_ratetable;
			err = ieee80211_register_hwmode(hw, mode);
			if (err)
				return err;

			phy->possible_phymodes |= B43legacy_PHYMODE_G;
			have_gphy = 0;
			continue;
		}
		break;
	}

	return 0;
}

static void b43legacy_wireless_core_detach(struct b43legacy_wldev *dev)
{
	/* We release firmware that late to not be required to re-request
	 * is all the time when we reinit the core. */
	b43legacy_release_firmware(dev);
}

static int b43legacy_wireless_core_attach(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	struct ssb_bus *bus = dev->dev->bus;
	struct pci_dev *pdev = bus->host_pci;
	int err;
	int have_bphy = 0;
	int have_gphy = 0;
	u32 tmp;

	/* Do NOT do any device initialization here.
	 * Do it in wireless_core_init() instead.
	 * This function is for gathering basic information about the HW, only.
	 * Also some structs may be set up here. But most likely you want to
	 * have that in core_init(), too.
	 */

	err = ssb_bus_powerup(bus, 0);
	if (err) {
		b43legacyerr(wl, "Bus powerup failed\n");
		goto out;
	}
	/* Get the PHY type. */
	if (dev->dev->id.revision >= 5) {
		u32 tmshigh;

		tmshigh = ssb_read32(dev->dev, SSB_TMSHIGH);
		have_gphy = !!(tmshigh & B43legacy_TMSHIGH_GPHY);
		if (!have_gphy)
			have_bphy = 1;
	} else if (dev->dev->id.revision == 4)
		have_gphy = 1;
	else
		have_bphy = 1;

	dev->phy.gmode = (have_gphy || have_bphy);
	tmp = dev->phy.gmode ? B43legacy_TMSLOW_GMODE : 0;
	b43legacy_wireless_core_reset(dev, tmp);

	err = b43legacy_phy_versioning(dev);
	if (err)
		goto err_powerdown;
	/* Check if this device supports multiband. */
	if (!pdev ||
	    (pdev->device != 0x4312 &&
	     pdev->device != 0x4319 &&
	     pdev->device != 0x4324)) {
		/* No multiband support. */
		have_bphy = 0;
		have_gphy = 0;
		switch (dev->phy.type) {
		case B43legacy_PHYTYPE_B:
			have_bphy = 1;
			break;
		case B43legacy_PHYTYPE_G:
			have_gphy = 1;
			break;
		default:
			B43legacy_BUG_ON(1);
		}
	}
	dev->phy.gmode = (have_gphy || have_bphy);
	tmp = dev->phy.gmode ? B43legacy_TMSLOW_GMODE : 0;
	b43legacy_wireless_core_reset(dev, tmp);

	err = b43legacy_validate_chipaccess(dev);
	if (err)
		goto err_powerdown;
	err = b43legacy_setup_modes(dev, have_bphy, have_gphy);
	if (err)
		goto err_powerdown;

	/* Now set some default "current_dev" */
	if (!wl->current_dev)
		wl->current_dev = dev;
	INIT_WORK(&dev->restart_work, b43legacy_chip_reset);

	b43legacy_radio_turn_off(dev, 1);
	b43legacy_switch_analog(dev, 0);
	ssb_device_disable(dev->dev, 0);
	ssb_bus_may_powerdown(bus);

out:
	return err;

err_powerdown:
	ssb_bus_may_powerdown(bus);
	return err;
}

static void b43legacy_one_core_detach(struct ssb_device *dev)
{
	struct b43legacy_wldev *wldev;
	struct b43legacy_wl *wl;

	wldev = ssb_get_drvdata(dev);
	wl = wldev->wl;
	cancel_work_sync(&wldev->restart_work);
	b43legacy_debugfs_remove_device(wldev);
	b43legacy_wireless_core_detach(wldev);
	list_del(&wldev->list);
	wl->nr_devs--;
	ssb_set_drvdata(dev, NULL);
	kfree(wldev);
}

static int b43legacy_one_core_attach(struct ssb_device *dev,
				     struct b43legacy_wl *wl)
{
	struct b43legacy_wldev *wldev;
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
		     (pdev->device != 0x4313) &&
		     (pdev->device != 0x431A))) {
			b43legacydbg(wl, "Ignoring unconnected 802.11 core\n");
			return -ENODEV;
		}
	}

	wldev = kzalloc(sizeof(*wldev), GFP_KERNEL);
	if (!wldev)
		goto out;

	wldev->dev = dev;
	wldev->wl = wl;
	b43legacy_set_status(wldev, B43legacy_STAT_UNINIT);
	wldev->bad_frames_preempt = modparam_bad_frames_preempt;
	tasklet_init(&wldev->isr_tasklet,
		     (void (*)(unsigned long))b43legacy_interrupt_tasklet,
		     (unsigned long)wldev);
	if (modparam_pio)
		wldev->__using_pio = 1;
	INIT_LIST_HEAD(&wldev->list);

	err = b43legacy_wireless_core_attach(wldev);
	if (err)
		goto err_kfree_wldev;

	list_add(&wldev->list, &wl->devlist);
	wl->nr_devs++;
	ssb_set_drvdata(dev, wldev);
	b43legacy_debugfs_add_device(wldev);
out:
	return err;

err_kfree_wldev:
	kfree(wldev);
	return err;
}

static void b43legacy_sprom_fixup(struct ssb_bus *bus)
{
	/* boardflags workarounds */
	if (bus->boardinfo.vendor == PCI_VENDOR_ID_APPLE &&
	    bus->boardinfo.type == 0x4E &&
	    bus->boardinfo.rev > 0x40)
		bus->sprom.boardflags_lo |= B43legacy_BFL_PACTRL;
}

static void b43legacy_wireless_exit(struct ssb_device *dev,
				  struct b43legacy_wl *wl)
{
	struct ieee80211_hw *hw = wl->hw;

	ssb_set_devtypedata(dev, NULL);
	ieee80211_free_hw(hw);
}

static int b43legacy_wireless_init(struct ssb_device *dev)
{
	struct ssb_sprom *sprom = &dev->bus->sprom;
	struct ieee80211_hw *hw;
	struct b43legacy_wl *wl;
	int err = -ENOMEM;

	b43legacy_sprom_fixup(dev->bus);

	hw = ieee80211_alloc_hw(sizeof(*wl), &b43legacy_hw_ops);
	if (!hw) {
		b43legacyerr(NULL, "Could not allocate ieee80211 device\n");
		goto out;
	}

	/* fill hw info */
	hw->flags = IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE |
		    IEEE80211_HW_RX_INCLUDES_FCS;
	hw->max_signal = 100;
	hw->max_rssi = -110;
	hw->max_noise = -110;
	hw->queues = 1; /* FIXME: hardware has more queues */
	SET_IEEE80211_DEV(hw, dev->dev);
	if (is_valid_ether_addr(sprom->et1mac))
		SET_IEEE80211_PERM_ADDR(hw, sprom->et1mac);
	else
		SET_IEEE80211_PERM_ADDR(hw, sprom->il0mac);

	/* Get and initialize struct b43legacy_wl */
	wl = hw_to_b43legacy_wl(hw);
	memset(wl, 0, sizeof(*wl));
	wl->hw = hw;
	spin_lock_init(&wl->irq_lock);
	spin_lock_init(&wl->leds_lock);
	mutex_init(&wl->mutex);
	INIT_LIST_HEAD(&wl->devlist);

	ssb_set_devtypedata(dev, wl);
	b43legacyinfo(wl, "Broadcom %04X WLAN found\n", dev->bus->chip_id);
	err = 0;
out:
	return err;
}

static int b43legacy_probe(struct ssb_device *dev,
			 const struct ssb_device_id *id)
{
	struct b43legacy_wl *wl;
	int err;
	int first = 0;

	wl = ssb_get_devtypedata(dev);
	if (!wl) {
		/* Probing the first core - setup common struct b43legacy_wl */
		first = 1;
		err = b43legacy_wireless_init(dev);
		if (err)
			goto out;
		wl = ssb_get_devtypedata(dev);
		B43legacy_WARN_ON(!wl);
	}
	err = b43legacy_one_core_attach(dev, wl);
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
	b43legacy_one_core_detach(dev);
err_wireless_exit:
	if (first)
		b43legacy_wireless_exit(dev, wl);
	return err;
}

static void b43legacy_remove(struct ssb_device *dev)
{
	struct b43legacy_wl *wl = ssb_get_devtypedata(dev);
	struct b43legacy_wldev *wldev = ssb_get_drvdata(dev);

	B43legacy_WARN_ON(!wl);
	if (wl->current_dev == wldev)
		ieee80211_unregister_hw(wl->hw);

	b43legacy_one_core_detach(dev);

	if (list_empty(&wl->devlist))
		/* Last core on the chip unregistered.
		 * We can destroy common struct b43legacy_wl.
		 */
		b43legacy_wireless_exit(dev, wl);
}

/* Perform a hardware reset. This can be called from any context. */
void b43legacy_controller_restart(struct b43legacy_wldev *dev,
				  const char *reason)
{
	/* Must avoid requeueing, if we are in shutdown. */
	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED)
		return;
	b43legacyinfo(dev->wl, "Controller RESET (%s) ...\n", reason);
	queue_work(dev->wl->hw->workqueue, &dev->restart_work);
}

#ifdef CONFIG_PM

static int b43legacy_suspend(struct ssb_device *dev, pm_message_t state)
{
	struct b43legacy_wldev *wldev = ssb_get_drvdata(dev);
	struct b43legacy_wl *wl = wldev->wl;

	b43legacydbg(wl, "Suspending...\n");

	mutex_lock(&wl->mutex);
	wldev->suspend_init_status = b43legacy_status(wldev);
	if (wldev->suspend_init_status >= B43legacy_STAT_STARTED)
		b43legacy_wireless_core_stop(wldev);
	if (wldev->suspend_init_status >= B43legacy_STAT_INITIALIZED)
		b43legacy_wireless_core_exit(wldev);
	mutex_unlock(&wl->mutex);

	b43legacydbg(wl, "Device suspended.\n");

	return 0;
}

static int b43legacy_resume(struct ssb_device *dev)
{
	struct b43legacy_wldev *wldev = ssb_get_drvdata(dev);
	struct b43legacy_wl *wl = wldev->wl;
	int err = 0;

	b43legacydbg(wl, "Resuming...\n");

	mutex_lock(&wl->mutex);
	if (wldev->suspend_init_status >= B43legacy_STAT_INITIALIZED) {
		err = b43legacy_wireless_core_init(wldev);
		if (err) {
			b43legacyerr(wl, "Resume failed at core init\n");
			goto out;
		}
	}
	if (wldev->suspend_init_status >= B43legacy_STAT_STARTED) {
		err = b43legacy_wireless_core_start(wldev);
		if (err) {
			b43legacy_wireless_core_exit(wldev);
			b43legacyerr(wl, "Resume failed at core start\n");
			goto out;
		}
	}
	mutex_unlock(&wl->mutex);

	b43legacydbg(wl, "Device resumed.\n");
out:
	return err;
}

#else	/* CONFIG_PM */
# define b43legacy_suspend	NULL
# define b43legacy_resume		NULL
#endif	/* CONFIG_PM */

static struct ssb_driver b43legacy_ssb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= b43legacy_ssb_tbl,
	.probe		= b43legacy_probe,
	.remove		= b43legacy_remove,
	.suspend	= b43legacy_suspend,
	.resume		= b43legacy_resume,
};

static void b43legacy_print_driverinfo(void)
{
	const char *feat_pci = "", *feat_leds = "", *feat_rfkill = "",
		   *feat_pio = "", *feat_dma = "";

#ifdef CONFIG_B43LEGACY_PCI_AUTOSELECT
	feat_pci = "P";
#endif
#ifdef CONFIG_B43LEGACY_LEDS
	feat_leds = "L";
#endif
#ifdef CONFIG_B43LEGACY_RFKILL
	feat_rfkill = "R";
#endif
#ifdef CONFIG_B43LEGACY_PIO
	feat_pio = "I";
#endif
#ifdef CONFIG_B43LEGACY_DMA
	feat_dma = "D";
#endif
	printk(KERN_INFO "Broadcom 43xx-legacy driver loaded "
	       "[ Features: %s%s%s%s%s, Firmware-ID: "
	       B43legacy_SUPPORTED_FIRMWARE_ID " ]\n",
	       feat_pci, feat_leds, feat_rfkill, feat_pio, feat_dma);
}

static int __init b43legacy_init(void)
{
	int err;

	b43legacy_debugfs_init();

	err = ssb_driver_register(&b43legacy_ssb_driver);
	if (err)
		goto err_dfs_exit;

	b43legacy_print_driverinfo();

	return err;

err_dfs_exit:
	b43legacy_debugfs_exit();
	return err;
}

static void __exit b43legacy_exit(void)
{
	ssb_driver_unregister(&b43legacy_ssb_driver);
	b43legacy_debugfs_exit();
}

module_init(b43legacy_init)
module_exit(b43legacy_exit)
