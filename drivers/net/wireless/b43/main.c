/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2009 Michael Buesch <m@bues.ch>
  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2010-2011 Rafał Miłecki <zajec5@gmail.com>

  SDIO support
  Copyright (c) 2009 Albert Herranz <albert_herranz@yahoo.es>

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
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "b43.h"
#include "main.h"
#include "debugfs.h"
#include "phy_common.h"
#include "phy_g.h"
#include "phy_n.h"
#include "dma.h"
#include "pio.h"
#include "sysfs.h"
#include "xmit.h"
#include "lo.h"
#include "pcmcia.h"
#include "sdio.h"
#include <linux/mmc/sdio_func.h>

MODULE_DESCRIPTION("Broadcom B43 wireless driver");
MODULE_AUTHOR("Martin Langer");
MODULE_AUTHOR("Stefano Brivio");
MODULE_AUTHOR("Michael Buesch");
MODULE_AUTHOR("Gábor Stefanik");
MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");

MODULE_FIRMWARE("b43/ucode11.fw");
MODULE_FIRMWARE("b43/ucode13.fw");
MODULE_FIRMWARE("b43/ucode14.fw");
MODULE_FIRMWARE("b43/ucode15.fw");
MODULE_FIRMWARE("b43/ucode16_mimo.fw");
MODULE_FIRMWARE("b43/ucode5.fw");
MODULE_FIRMWARE("b43/ucode9.fw");

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

static int modparam_hwtkip;
module_param_named(hwtkip, modparam_hwtkip, int, 0444);
MODULE_PARM_DESC(hwtkip, "Enable hardware tkip.");

static int modparam_qos = 1;
module_param_named(qos, modparam_qos, int, 0444);
MODULE_PARM_DESC(qos, "Enable QOS support (default on)");

static int modparam_btcoex = 1;
module_param_named(btcoex, modparam_btcoex, int, 0444);
MODULE_PARM_DESC(btcoex, "Enable Bluetooth coexistence (default on)");

int b43_modparam_verbose = B43_VERBOSITY_DEFAULT;
module_param_named(verbose, b43_modparam_verbose, int, 0644);
MODULE_PARM_DESC(verbose, "Log message verbosity: 0=error, 1=warn, 2=info(default), 3=debug");

static int b43_modparam_pio = 0;
module_param_named(pio, b43_modparam_pio, int, 0644);
MODULE_PARM_DESC(pio, "Use PIO accesses by default: 0=DMA, 1=PIO");

#ifdef CONFIG_B43_BCMA
static const struct bcma_device_id b43_bcma_tbl[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 0x11, BCMA_ANY_CLASS),
#ifdef CONFIG_B43_BCMA_EXTRA
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 0x17, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 0x18, BCMA_ANY_CLASS),
#endif
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 0x1D, BCMA_ANY_CLASS),
	BCMA_CORETABLE_END
};
MODULE_DEVICE_TABLE(bcma, b43_bcma_tbl);
#endif

#ifdef CONFIG_B43_SSB
static const struct ssb_device_id b43_ssb_tbl[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 5),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 6),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 7),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 9),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 10),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 11),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 12),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 13),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 15),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_80211, 16),
	SSB_DEVTABLE_END
};
MODULE_DEVICE_TABLE(ssb, b43_ssb_tbl);
#endif

/* Channel and ratetables are shared for all devices.
 * They can't be const, because ieee80211 puts some precalculated
 * data in there. This data is the same for all devices, so we don't
 * get concurrency issues */
#define RATETAB_ENT(_rateid, _flags) \
	{								\
		.bitrate	= B43_RATE_TO_BASE100KBPS(_rateid),	\
		.hw_value	= (_rateid),				\
		.flags		= (_flags),				\
	}

/*
 * NOTE: When changing this, sync with xmit.c's
 *	 b43_plcp_get_bitrate_idx_* functions!
 */
static struct ieee80211_rate __b43_ratetable[] = {
	RATETAB_ENT(B43_CCK_RATE_1MB, 0),
	RATETAB_ENT(B43_CCK_RATE_2MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(B43_CCK_RATE_5MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(B43_CCK_RATE_11MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(B43_OFDM_RATE_6MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_9MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_12MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_18MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_24MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_36MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_48MB, 0),
	RATETAB_ENT(B43_OFDM_RATE_54MB, 0),
};

#define b43_a_ratetable		(__b43_ratetable + 4)
#define b43_a_ratetable_size	8
#define b43_b_ratetable		(__b43_ratetable + 0)
#define b43_b_ratetable_size	4
#define b43_g_ratetable		(__b43_ratetable + 0)
#define b43_g_ratetable_size	12

#define CHAN4G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}
static struct ieee80211_channel b43_2ghz_chantable[] = {
	CHAN4G(1, 2412, 0),
	CHAN4G(2, 2417, 0),
	CHAN4G(3, 2422, 0),
	CHAN4G(4, 2427, 0),
	CHAN4G(5, 2432, 0),
	CHAN4G(6, 2437, 0),
	CHAN4G(7, 2442, 0),
	CHAN4G(8, 2447, 0),
	CHAN4G(9, 2452, 0),
	CHAN4G(10, 2457, 0),
	CHAN4G(11, 2462, 0),
	CHAN4G(12, 2467, 0),
	CHAN4G(13, 2472, 0),
	CHAN4G(14, 2484, 0),
};
#undef CHAN4G

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}
static struct ieee80211_channel b43_5ghz_nphy_chantable[] = {
	CHAN5G(32, 0),		CHAN5G(34, 0),
	CHAN5G(36, 0),		CHAN5G(38, 0),
	CHAN5G(40, 0),		CHAN5G(42, 0),
	CHAN5G(44, 0),		CHAN5G(46, 0),
	CHAN5G(48, 0),		CHAN5G(50, 0),
	CHAN5G(52, 0),		CHAN5G(54, 0),
	CHAN5G(56, 0),		CHAN5G(58, 0),
	CHAN5G(60, 0),		CHAN5G(62, 0),
	CHAN5G(64, 0),		CHAN5G(66, 0),
	CHAN5G(68, 0),		CHAN5G(70, 0),
	CHAN5G(72, 0),		CHAN5G(74, 0),
	CHAN5G(76, 0),		CHAN5G(78, 0),
	CHAN5G(80, 0),		CHAN5G(82, 0),
	CHAN5G(84, 0),		CHAN5G(86, 0),
	CHAN5G(88, 0),		CHAN5G(90, 0),
	CHAN5G(92, 0),		CHAN5G(94, 0),
	CHAN5G(96, 0),		CHAN5G(98, 0),
	CHAN5G(100, 0),		CHAN5G(102, 0),
	CHAN5G(104, 0),		CHAN5G(106, 0),
	CHAN5G(108, 0),		CHAN5G(110, 0),
	CHAN5G(112, 0),		CHAN5G(114, 0),
	CHAN5G(116, 0),		CHAN5G(118, 0),
	CHAN5G(120, 0),		CHAN5G(122, 0),
	CHAN5G(124, 0),		CHAN5G(126, 0),
	CHAN5G(128, 0),		CHAN5G(130, 0),
	CHAN5G(132, 0),		CHAN5G(134, 0),
	CHAN5G(136, 0),		CHAN5G(138, 0),
	CHAN5G(140, 0),		CHAN5G(142, 0),
	CHAN5G(144, 0),		CHAN5G(145, 0),
	CHAN5G(146, 0),		CHAN5G(147, 0),
	CHAN5G(148, 0),		CHAN5G(149, 0),
	CHAN5G(150, 0),		CHAN5G(151, 0),
	CHAN5G(152, 0),		CHAN5G(153, 0),
	CHAN5G(154, 0),		CHAN5G(155, 0),
	CHAN5G(156, 0),		CHAN5G(157, 0),
	CHAN5G(158, 0),		CHAN5G(159, 0),
	CHAN5G(160, 0),		CHAN5G(161, 0),
	CHAN5G(162, 0),		CHAN5G(163, 0),
	CHAN5G(164, 0),		CHAN5G(165, 0),
	CHAN5G(166, 0),		CHAN5G(168, 0),
	CHAN5G(170, 0),		CHAN5G(172, 0),
	CHAN5G(174, 0),		CHAN5G(176, 0),
	CHAN5G(178, 0),		CHAN5G(180, 0),
	CHAN5G(182, 0),		CHAN5G(184, 0),
	CHAN5G(186, 0),		CHAN5G(188, 0),
	CHAN5G(190, 0),		CHAN5G(192, 0),
	CHAN5G(194, 0),		CHAN5G(196, 0),
	CHAN5G(198, 0),		CHAN5G(200, 0),
	CHAN5G(202, 0),		CHAN5G(204, 0),
	CHAN5G(206, 0),		CHAN5G(208, 0),
	CHAN5G(210, 0),		CHAN5G(212, 0),
	CHAN5G(214, 0),		CHAN5G(216, 0),
	CHAN5G(218, 0),		CHAN5G(220, 0),
	CHAN5G(222, 0),		CHAN5G(224, 0),
	CHAN5G(226, 0),		CHAN5G(228, 0),
};

static struct ieee80211_channel b43_5ghz_aphy_chantable[] = {
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};
#undef CHAN5G

static struct ieee80211_supported_band b43_band_5GHz_nphy = {
	.band		= IEEE80211_BAND_5GHZ,
	.channels	= b43_5ghz_nphy_chantable,
	.n_channels	= ARRAY_SIZE(b43_5ghz_nphy_chantable),
	.bitrates	= b43_a_ratetable,
	.n_bitrates	= b43_a_ratetable_size,
};

static struct ieee80211_supported_band b43_band_5GHz_aphy = {
	.band		= IEEE80211_BAND_5GHZ,
	.channels	= b43_5ghz_aphy_chantable,
	.n_channels	= ARRAY_SIZE(b43_5ghz_aphy_chantable),
	.bitrates	= b43_a_ratetable,
	.n_bitrates	= b43_a_ratetable_size,
};

static struct ieee80211_supported_band b43_band_2GHz = {
	.band		= IEEE80211_BAND_2GHZ,
	.channels	= b43_2ghz_chantable,
	.n_channels	= ARRAY_SIZE(b43_2ghz_chantable),
	.bitrates	= b43_g_ratetable,
	.n_bitrates	= b43_g_ratetable_size,
};

static void b43_wireless_core_exit(struct b43_wldev *dev);
static int b43_wireless_core_init(struct b43_wldev *dev);
static struct b43_wldev * b43_wireless_core_stop(struct b43_wldev *dev);
static int b43_wireless_core_start(struct b43_wldev *dev);
static void b43_op_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *conf,
				    u32 changed);

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
	struct va_format vaf;
	va_list args;

	if (b43_modparam_verbose < B43_VERBOSITY_INFO)
		return;
	if (!b43_ratelimit(wl))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_INFO "b43-%s: %pV",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan", &vaf);

	va_end(args);
}

void b43err(struct b43_wl *wl, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (b43_modparam_verbose < B43_VERBOSITY_ERROR)
		return;
	if (!b43_ratelimit(wl))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_ERR "b43-%s ERROR: %pV",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan", &vaf);

	va_end(args);
}

void b43warn(struct b43_wl *wl, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (b43_modparam_verbose < B43_VERBOSITY_WARN)
		return;
	if (!b43_ratelimit(wl))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_WARNING "b43-%s warning: %pV",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan", &vaf);

	va_end(args);
}

void b43dbg(struct b43_wl *wl, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (b43_modparam_verbose < B43_VERBOSITY_DEBUG)
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_DEBUG "b43-%s debug: %pV",
	       (wl && wl->hw) ? wiphy_name(wl->hw->wiphy) : "wlan", &vaf);

	va_end(args);
}

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
	u32 ret;

	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			ret = b43_read16(dev, B43_MMIO_SHM_DATA_UNALIGNED);
			b43_shm_control_word(dev, routing, (offset >> 2) + 1);
			ret |= ((u32)b43_read16(dev, B43_MMIO_SHM_DATA)) << 16;

			goto out;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	ret = b43_read32(dev, B43_MMIO_SHM_DATA);
out:
	return ret;
}

u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	u16 ret;

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
	return ret;
}

void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			b43_write16(dev, B43_MMIO_SHM_DATA_UNALIGNED,
				    value & 0xFFFF);
			b43_shm_control_word(dev, routing, (offset >> 2) + 1);
			b43_write16(dev, B43_MMIO_SHM_DATA,
				    (value >> 16) & 0xFFFF);
			return;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	b43_write32(dev, B43_MMIO_SHM_DATA, value);
}

void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	if (routing == B43_SHM_SHARED) {
		B43_WARN_ON(offset & 0x0001);
		if (offset & 0x0003) {
			/* Unaligned access */
			b43_shm_control_word(dev, routing, offset >> 2);
			b43_write16(dev, B43_MMIO_SHM_DATA_UNALIGNED, value);
			return;
		}
		offset >>= 2;
	}
	b43_shm_control_word(dev, routing, offset);
	b43_write16(dev, B43_MMIO_SHM_DATA, value);
}

/* Read HostFlags */
u64 b43_hf_read(struct b43_wldev *dev)
{
	u64 ret;

	ret = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF3);
	ret <<= 16;
	ret |= b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF2);
	ret <<= 16;
	ret |= b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF1);

	return ret;
}

/* Write HostFlags */
void b43_hf_write(struct b43_wldev *dev, u64 value)
{
	u16 lo, mi, hi;

	lo = (value & 0x00000000FFFFULL);
	mi = (value & 0x0000FFFF0000ULL) >> 16;
	hi = (value & 0xFFFF00000000ULL) >> 32;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF1, lo);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF2, mi);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF3, hi);
}

/* Read the firmware capabilities bitmask (Opensource firmware only) */
static u16 b43_fwcapa_read(struct b43_wldev *dev)
{
	B43_WARN_ON(!dev->fw.opensource);
	return b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_FWCAPA);
}

void b43_tsf_read(struct b43_wldev *dev, u64 *tsf)
{
	u32 low, high;

	B43_WARN_ON(dev->dev->core_rev < 3);

	/* The hardware guarantees us an atomic read, if we
	 * read the low register first. */
	low = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_LOW);
	high = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_HIGH);

	*tsf = high;
	*tsf <<= 32;
	*tsf |= low;
}

static void b43_time_lock(struct b43_wldev *dev)
{
	b43_maskset32(dev, B43_MMIO_MACCTL, ~0, B43_MACCTL_TBTTHOLD);
	/* Commit the write */
	b43_read32(dev, B43_MMIO_MACCTL);
}

static void b43_time_unlock(struct b43_wldev *dev)
{
	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_TBTTHOLD, 0);
	/* Commit the write */
	b43_read32(dev, B43_MMIO_MACCTL);
}

static void b43_tsf_write_locked(struct b43_wldev *dev, u64 tsf)
{
	u32 low, high;

	B43_WARN_ON(dev->dev->core_rev < 3);

	low = tsf;
	high = (tsf >> 32);
	/* The hardware guarantees us an atomic write, if we
	 * write the low register first. */
	b43_write32(dev, B43_MMIO_REV3PLUS_TSF_LOW, low);
	mmiowb();
	b43_write32(dev, B43_MMIO_REV3PLUS_TSF_HIGH, high);
	mmiowb();
}

void b43_tsf_write(struct b43_wldev *dev, u64 tsf)
{
	b43_time_lock(dev);
	b43_tsf_write_locked(dev, tsf);
	b43_time_unlock(dev);
}

static
void b43_macfilter_set(struct b43_wldev *dev, u16 offset, const u8 *mac)
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
	/* This test used to exit for all but a G PHY. */
	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
		return;
	b43_write16(dev, B43_MMIO_IFSSLOT, 510 + slot_time);
	/* Shared memory location 0x0010 is the slot time and should be
	 * set to slot_time; however, this register is initially 0 and changing
	 * the value adversely affects the transmit rate for BCM4311
	 * devices. Until this behavior is unterstood, delete this step
	 *
	 * b43_shm_write16(dev, B43_SHM_SHARED, 0x0010, slot_time);
	 */
}

static void b43_short_slot_timing_enable(struct b43_wldev *dev)
{
	b43_set_slot_time(dev, 9);
}

static void b43_short_slot_timing_disable(struct b43_wldev *dev)
{
	b43_set_slot_time(dev, 20);
}

/* DummyTransmission function, as documented on
 * http://bcm-v4.sipsolutions.net/802.11/DummyTransmission
 */
void b43_dummy_transmission(struct b43_wldev *dev, bool ofdm, bool pa_on)
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

	if (ofdm) {
		max_loop = 0x1E;
		buffer[0] = 0x000201CC;
	} else {
		max_loop = 0xFA;
		buffer[0] = 0x000B846E;
	}

	for (i = 0; i < 5; i++)
		b43_ram_write(dev, i * 4, buffer[i]);

	b43_write16(dev, B43_MMIO_XMTSEL, 0x0000);

	if (dev->dev->core_rev < 11)
		b43_write16(dev, B43_MMIO_WEPCTL, 0x0000);
	else
		b43_write16(dev, B43_MMIO_WEPCTL, 0x0100);

	value = (ofdm ? 0x41 : 0x40);
	b43_write16(dev, B43_MMIO_TXE0_PHYCTL, value);
	if (phy->type == B43_PHYTYPE_N || phy->type == B43_PHYTYPE_LP ||
	    phy->type == B43_PHYTYPE_LCN)
		b43_write16(dev, B43_MMIO_TXE0_PHYCTL1, 0x1A02);

	b43_write16(dev, B43_MMIO_TXE0_WM_0, 0x0000);
	b43_write16(dev, B43_MMIO_TXE0_WM_1, 0x0000);

	b43_write16(dev, B43_MMIO_XMTTPLATETXPTR, 0x0000);
	b43_write16(dev, B43_MMIO_XMTTXCNT, 0x0014);
	b43_write16(dev, B43_MMIO_XMTSEL, 0x0826);
	b43_write16(dev, B43_MMIO_TXE0_CTL, 0x0000);

	if (!pa_on && phy->type == B43_PHYTYPE_N)
		; /*b43_nphy_pa_override(dev, false) */

	switch (phy->type) {
	case B43_PHYTYPE_N:
	case B43_PHYTYPE_LCN:
		b43_write16(dev, B43_MMIO_TXE0_AUX, 0x00D0);
		break;
	case B43_PHYTYPE_LP:
		b43_write16(dev, B43_MMIO_TXE0_AUX, 0x0050);
		break;
	default:
		b43_write16(dev, B43_MMIO_TXE0_AUX, 0x0030);
	}
	b43_read16(dev, B43_MMIO_TXE0_AUX);

	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43_radio_write16(dev, 0x0051, 0x0017);
	for (i = 0x00; i < max_loop; i++) {
		value = b43_read16(dev, B43_MMIO_TXE0_STATUS);
		if (value & 0x0080)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = b43_read16(dev, B43_MMIO_TXE0_STATUS);
		if (value & 0x0400)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x19; i++) {
		value = b43_read16(dev, B43_MMIO_IFSSTAT);
		if (!(value & 0x0100))
			break;
		udelay(10);
	}
	if (phy->radio_ver == 0x2050 && phy->radio_rev <= 0x5)
		b43_radio_write16(dev, 0x0051, 0x0037);
}

static void key_write(struct b43_wldev *dev,
		      u8 index, u8 algorithm, const u8 *key)
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

static void keymac_write(struct b43_wldev *dev, u8 index, const u8 *addr)
{
	u32 addrtmp[2] = { 0, 0, };
	u8 pairwise_keys_start = B43_NR_GROUP_KEYS * 2;

	if (b43_new_kidx_api(dev))
		pairwise_keys_start = B43_NR_GROUP_KEYS;

	B43_WARN_ON(index < pairwise_keys_start);
	/* We have four default TX keys and possibly four default RX keys.
	 * Physical mac 0 is mapped to physical key 4 or 8, depending
	 * on the firmware version.
	 * So we must adjust the index here.
	 */
	index -= pairwise_keys_start;
	B43_WARN_ON(index >= B43_NR_PAIRWISE_KEYS);

	if (addr) {
		addrtmp[0] = addr[0];
		addrtmp[0] |= ((u32) (addr[1]) << 8);
		addrtmp[0] |= ((u32) (addr[2]) << 16);
		addrtmp[0] |= ((u32) (addr[3]) << 24);
		addrtmp[1] = addr[4];
		addrtmp[1] |= ((u32) (addr[5]) << 8);
	}

	/* Receive match transmitter address (RCMTA) mechanism */
	b43_shm_write32(dev, B43_SHM_RCMTA,
			(index * 2) + 0, addrtmp[0]);
	b43_shm_write16(dev, B43_SHM_RCMTA,
			(index * 2) + 1, addrtmp[1]);
}

/* The ucode will use phase1 key with TEK key to decrypt rx packets.
 * When a packet is received, the iv32 is checked.
 * - if it doesn't the packet is returned without modification (and software
 *   decryption can be done). That's what happen when iv16 wrap.
 * - if it does, the rc4 key is computed, and decryption is tried.
 *   Either it will success and B43_RX_MAC_DEC is returned,
 *   either it fails and B43_RX_MAC_DEC|B43_RX_MAC_DECERR is returned
 *   and the packet is not usable (it got modified by the ucode).
 * So in order to never have B43_RX_MAC_DECERR, we should provide
 * a iv32 and phase1key that match. Because we drop packets in case of
 * B43_RX_MAC_DECERR, if we have a correct iv32 but a wrong phase1key, all
 * packets will be lost without higher layer knowing (ie no resync possible
 * until next wrap).
 *
 * NOTE : this should support 50 key like RCMTA because
 * (B43_SHM_SH_KEYIDXBLOCK - B43_SHM_SH_TKIPTSCTTAK)/14 = 50
 */
static void rx_tkip_phase1_write(struct b43_wldev *dev, u8 index, u32 iv32,
		u16 *phase1key)
{
	unsigned int i;
	u32 offset;
	u8 pairwise_keys_start = B43_NR_GROUP_KEYS * 2;

	if (!modparam_hwtkip)
		return;

	if (b43_new_kidx_api(dev))
		pairwise_keys_start = B43_NR_GROUP_KEYS;

	B43_WARN_ON(index < pairwise_keys_start);
	/* We have four default TX keys and possibly four default RX keys.
	 * Physical mac 0 is mapped to physical key 4 or 8, depending
	 * on the firmware version.
	 * So we must adjust the index here.
	 */
	index -= pairwise_keys_start;
	B43_WARN_ON(index >= B43_NR_PAIRWISE_KEYS);

	if (b43_debug(dev, B43_DBG_KEYS)) {
		b43dbg(dev->wl, "rx_tkip_phase1_write : idx 0x%x, iv32 0x%x\n",
				index, iv32);
	}
	/* Write the key to the  RX tkip shared mem */
	offset = B43_SHM_SH_TKIPTSCTTAK + index * (10 + 4);
	for (i = 0; i < 10; i += 2) {
		b43_shm_write16(dev, B43_SHM_SHARED, offset + i,
				phase1key ? phase1key[i / 2] : 0);
	}
	b43_shm_write16(dev, B43_SHM_SHARED, offset + i, iv32);
	b43_shm_write16(dev, B43_SHM_SHARED, offset + i + 2, iv32 >> 16);
}

static void b43_op_update_tkip_key(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_key_conf *keyconf,
				   struct ieee80211_sta *sta,
				   u32 iv32, u16 *phase1key)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	int index = keyconf->hw_key_idx;

	if (B43_WARN_ON(!modparam_hwtkip))
		return;

	/* This is only called from the RX path through mac80211, where
	 * our mutex is already locked. */
	B43_WARN_ON(!mutex_is_locked(&wl->mutex));
	dev = wl->current_dev;
	B43_WARN_ON(!dev || b43_status(dev) < B43_STAT_INITIALIZED);

	keymac_write(dev, index, NULL);	/* First zero out mac to avoid race */

	rx_tkip_phase1_write(dev, index, iv32, phase1key);
	/* only pairwise TKIP keys are supported right now */
	if (WARN_ON(!sta))
		return;
	keymac_write(dev, index, sta->addr);
}

static void do_key_write(struct b43_wldev *dev,
			 u8 index, u8 algorithm,
			 const u8 *key, size_t key_len, const u8 *mac_addr)
{
	u8 buf[B43_SEC_KEYSIZE] = { 0, };
	u8 pairwise_keys_start = B43_NR_GROUP_KEYS * 2;

	if (b43_new_kidx_api(dev))
		pairwise_keys_start = B43_NR_GROUP_KEYS;

	B43_WARN_ON(index >= ARRAY_SIZE(dev->key));
	B43_WARN_ON(key_len > B43_SEC_KEYSIZE);

	if (index >= pairwise_keys_start)
		keymac_write(dev, index, NULL);	/* First zero out mac. */
	if (algorithm == B43_SEC_ALGO_TKIP) {
		/*
		 * We should provide an initial iv32, phase1key pair.
		 * We could start with iv32=0 and compute the corresponding
		 * phase1key, but this means calling ieee80211_get_tkip_key
		 * with a fake skb (or export other tkip function).
		 * Because we are lazy we hope iv32 won't start with
		 * 0xffffffff and let's b43_op_update_tkip_key provide a
		 * correct pair.
		 */
		rx_tkip_phase1_write(dev, index, 0xffffffff, (u16*)buf);
	} else if (index >= pairwise_keys_start) /* clear it */
		rx_tkip_phase1_write(dev, index, 0, NULL);
	if (key)
		memcpy(buf, key, key_len);
	key_write(dev, index, algorithm, buf);
	if (index >= pairwise_keys_start)
		keymac_write(dev, index, mac_addr);

	dev->key[index].algorithm = algorithm;
}

static int b43_key_write(struct b43_wldev *dev,
			 int index, u8 algorithm,
			 const u8 *key, size_t key_len,
			 const u8 *mac_addr,
			 struct ieee80211_key_conf *keyconf)
{
	int i;
	int pairwise_keys_start;

	/* For ALG_TKIP the key is encoded as a 256-bit (32 byte) data block:
	 * 	- Temporal Encryption Key (128 bits)
	 * 	- Temporal Authenticator Tx MIC Key (64 bits)
	 * 	- Temporal Authenticator Rx MIC Key (64 bits)
	 *
	 * 	Hardware only store TEK
	 */
	if (algorithm == B43_SEC_ALGO_TKIP && key_len == 32)
		key_len = 16;
	if (key_len > B43_SEC_KEYSIZE)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(dev->key); i++) {
		/* Check that we don't already have this key. */
		B43_WARN_ON(dev->key[i].keyconf == keyconf);
	}
	if (index < 0) {
		/* Pairwise key. Get an empty slot for the key. */
		if (b43_new_kidx_api(dev))
			pairwise_keys_start = B43_NR_GROUP_KEYS;
		else
			pairwise_keys_start = B43_NR_GROUP_KEYS * 2;
		for (i = pairwise_keys_start;
		     i < pairwise_keys_start + B43_NR_PAIRWISE_KEYS;
		     i++) {
			B43_WARN_ON(i >= ARRAY_SIZE(dev->key));
			if (!dev->key[i].keyconf) {
				/* found empty */
				index = i;
				break;
			}
		}
		if (index < 0) {
			b43warn(dev->wl, "Out of hardware key memory\n");
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
	if (B43_WARN_ON((index < 0) || (index >= ARRAY_SIZE(dev->key))))
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
	int i, count;

	if (b43_new_kidx_api(dev))
		count = B43_NR_GROUP_KEYS + B43_NR_PAIRWISE_KEYS;
	else
		count = B43_NR_GROUP_KEYS * 2 + B43_NR_PAIRWISE_KEYS;
	for (i = 0; i < count; i++)
		b43_key_clear(dev, i);
}

static void b43_dump_keymemory(struct b43_wldev *dev)
{
	unsigned int i, index, count, offset, pairwise_keys_start;
	u8 mac[ETH_ALEN];
	u16 algo;
	u32 rcmta0;
	u16 rcmta1;
	u64 hf;
	struct b43_key *key;

	if (!b43_debug(dev, B43_DBG_KEYS))
		return;

	hf = b43_hf_read(dev);
	b43dbg(dev->wl, "Hardware key memory dump:  USEDEFKEYS=%u\n",
	       !!(hf & B43_HF_USEDEFKEYS));
	if (b43_new_kidx_api(dev)) {
		pairwise_keys_start = B43_NR_GROUP_KEYS;
		count = B43_NR_GROUP_KEYS + B43_NR_PAIRWISE_KEYS;
	} else {
		pairwise_keys_start = B43_NR_GROUP_KEYS * 2;
		count = B43_NR_GROUP_KEYS * 2 + B43_NR_PAIRWISE_KEYS;
	}
	for (index = 0; index < count; index++) {
		key = &(dev->key[index]);
		printk(KERN_DEBUG "Key slot %02u: %s",
		       index, (key->keyconf == NULL) ? " " : "*");
		offset = dev->ktp + (index * B43_SEC_KEYSIZE);
		for (i = 0; i < B43_SEC_KEYSIZE; i += 2) {
			u16 tmp = b43_shm_read16(dev, B43_SHM_SHARED, offset + i);
			printk("%02X%02X", (tmp & 0xFF), ((tmp >> 8) & 0xFF));
		}

		algo = b43_shm_read16(dev, B43_SHM_SHARED,
				      B43_SHM_SH_KEYIDXBLOCK + (index * 2));
		printk("   Algo: %04X/%02X", algo, key->algorithm);

		if (index >= pairwise_keys_start) {
			if (key->algorithm == B43_SEC_ALGO_TKIP) {
				printk("   TKIP: ");
				offset = B43_SHM_SH_TKIPTSCTTAK + (index - 4) * (10 + 4);
				for (i = 0; i < 14; i += 2) {
					u16 tmp = b43_shm_read16(dev, B43_SHM_SHARED, offset + i);
					printk("%02X%02X", (tmp & 0xFF), ((tmp >> 8) & 0xFF));
				}
			}
			rcmta0 = b43_shm_read32(dev, B43_SHM_RCMTA,
						((index - pairwise_keys_start) * 2) + 0);
			rcmta1 = b43_shm_read16(dev, B43_SHM_RCMTA,
						((index - pairwise_keys_start) * 2) + 1);
			*((__le32 *)(&mac[0])) = cpu_to_le32(rcmta0);
			*((__le16 *)(&mac[4])) = cpu_to_le16(rcmta1);
			printk("   MAC: %pM", mac);
		} else
			printk("   DEFAULT KEY");
		printk("\n");
	}
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
		hwps = true;
	} else if (ps_flags & B43_PS_DISABLED) {
		hwps = false;
	} else {
		//TODO: If powersave is not off and FIXME is not set and we are not in adhoc
		//      and thus is not an AP and we are associated, set bit 25
	}
	if (ps_flags & B43_PS_AWAKE) {
		awake = true;
	} else if (ps_flags & B43_PS_ASLEEP) {
		awake = false;
	} else {
		//TODO: If the device is awake or this is an AP, or we are scanning, or FIXME,
		//      or we are associated, or FIXME, or the latest PS-Poll packet sent was
		//      successful, set bit26
	}

/* FIXME: For now we force awake-on and hwps-off */
	hwps = false;
	awake = true;

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
	if (awake && dev->dev->core_rev >= 5) {
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

#ifdef CONFIG_B43_BCMA
static void b43_bcma_phy_reset(struct b43_wldev *dev)
{
	u32 flags;

	/* Put PHY into reset */
	flags = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
	flags |= B43_BCMA_IOCTL_PHY_RESET;
	flags |= B43_BCMA_IOCTL_PHY_BW_20MHZ; /* Make 20 MHz def */
	bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, flags);
	udelay(2);

	/* Take PHY out of reset */
	flags = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
	flags &= ~B43_BCMA_IOCTL_PHY_RESET;
	flags |= BCMA_IOCTL_FGC;
	bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, flags);
	udelay(1);

	/* Do not force clock anymore */
	flags = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
	flags &= ~BCMA_IOCTL_FGC;
	bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, flags);
	udelay(1);
}

static void b43_bcma_wireless_core_reset(struct b43_wldev *dev, bool gmode)
{
	u32 req = B43_BCMA_CLKCTLST_80211_PLL_REQ |
		  B43_BCMA_CLKCTLST_PHY_PLL_REQ;
	u32 status = B43_BCMA_CLKCTLST_80211_PLL_ST |
		     B43_BCMA_CLKCTLST_PHY_PLL_ST;

	b43_device_enable(dev, B43_BCMA_IOCTL_PHY_CLKEN);
	bcma_core_set_clockmode(dev->dev->bdev, BCMA_CLKMODE_FAST);
	b43_bcma_phy_reset(dev);
	bcma_core_pll_ctl(dev->dev->bdev, req, status, true);
}
#endif

static void b43_ssb_wireless_core_reset(struct b43_wldev *dev, bool gmode)
{
	struct ssb_device *sdev = dev->dev->sdev;
	u32 tmslow;
	u32 flags = 0;

	if (gmode)
		flags |= B43_TMSLOW_GMODE;
	flags |= B43_TMSLOW_PHYCLKEN;
	flags |= B43_TMSLOW_PHYRESET;
	if (dev->phy.type == B43_PHYTYPE_N)
		flags |= B43_TMSLOW_PHY_BANDWIDTH_20MHZ; /* Make 20 MHz def */
	b43_device_enable(dev, flags);
	msleep(2);		/* Wait for the PLL to turn on. */

	/* Now take the PHY out of Reset again */
	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmslow |= SSB_TMSLOW_FGC;
	tmslow &= ~B43_TMSLOW_PHYRESET;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	ssb_read32(sdev, SSB_TMSLOW);	/* flush */
	msleep(1);
	tmslow &= ~SSB_TMSLOW_FGC;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);
	ssb_read32(sdev, SSB_TMSLOW);	/* flush */
	msleep(1);
}

void b43_wireless_core_reset(struct b43_wldev *dev, bool gmode)
{
	u32 macctl;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		b43_bcma_wireless_core_reset(dev, gmode);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		b43_ssb_wireless_core_reset(dev, gmode);
		break;
#endif
	}

	/* Turn Analog ON, but only if we already know the PHY-type.
	 * This protects against very early setup where we don't know the
	 * PHY-type, yet. wireless_core_reset will be called once again later,
	 * when we know the PHY-type. */
	if (dev->phy.ops)
		dev->phy.ops->switch_analog(dev, 1);

	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	macctl &= ~B43_MACCTL_GMODE;
	if (gmode)
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

	if (dev->dev->core_rev < 5)
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
}

static void b43_calculate_link_quality(struct b43_wldev *dev)
{
	/* Top half of Link Quality calculation. */

	if (dev->phy.type != B43_PHYTYPE_G)
		return;
	if (dev->noisecalc.calculation_running)
		return;
	dev->noisecalc.calculation_running = true;
	dev->noisecalc.nr_samples = 0;

	b43_generate_noise_sample(dev);
}

static void handle_irq_noise(struct b43_wldev *dev)
{
	struct b43_phy_g *phy = dev->phy.g;
	u16 tmp;
	u8 noise[4];
	u8 i, j;
	s32 average;

	/* Bottom half of Link Quality calculation. */

	if (dev->phy.type != B43_PHYTYPE_G)
		return;

	/* Possible race condition: It might be possible that the user
	 * changed to a different channel in the meantime since we
	 * started the calculation. We ignore that fact, since it's
	 * not really that much of a problem. The background noise is
	 * an estimation only anyway. Slightly wrong results will get damped
	 * by the averaging of the 8 sample rounds. Additionally the
	 * value is shortlived. So it will be replaced by the next noise
	 * calculation round soon. */

	B43_WARN_ON(!dev->noisecalc.calculation_running);
	*((__le32 *)noise) = cpu_to_le32(b43_jssi_read(dev));
	if (noise[0] == 0x7F || noise[1] == 0x7F ||
	    noise[2] == 0x7F || noise[3] == 0x7F)
		goto generate_new;

	/* Get the noise samples. */
	B43_WARN_ON(dev->noisecalc.nr_samples >= 8);
	i = dev->noisecalc.nr_samples;
	noise[0] = clamp_val(noise[0], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[1] = clamp_val(noise[1], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[2] = clamp_val(noise[2], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
	noise[3] = clamp_val(noise[3], 0, ARRAY_SIZE(phy->nrssi_lt) - 1);
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
		dev->noisecalc.calculation_running = false;
		return;
	}
generate_new:
	b43_generate_noise_sample(dev);
}

static void handle_irq_tbtt_indication(struct b43_wldev *dev)
{
	if (b43_is_mode(dev->wl, NL80211_IFTYPE_AP)) {
		///TODO: PS TBTT
	} else {
		if (1 /*FIXME: the last PSpoll frame was sent successfully */ )
			b43_power_saving_ctl_bits(dev, 0);
	}
	if (b43_is_mode(dev->wl, NL80211_IFTYPE_ADHOC))
		dev->dfq_valid = true;
}

static void handle_irq_atim_end(struct b43_wldev *dev)
{
	if (dev->dfq_valid) {
		b43_write32(dev, B43_MMIO_MACCMD,
			    b43_read32(dev, B43_MMIO_MACCMD)
			    | B43_MACCMD_DFQ_VALID);
		dev->dfq_valid = false;
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
				      const u8 *data, u16 size,
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
		antenna_mask = dev->dev->bus_sprom->ant_available_bg;
	else
		antenna_mask = dev->dev->bus_sprom->ant_available_a;

	if (!(antenna_mask & (1 << (antenna_nr - 1)))) {
		/* This antenna is not available. Fall back to default. */
		return 0;
	}

	return antenna_nr;
}

/* Convert a b43 antenna number value to the PHY TX control value. */
static u16 b43_antenna_to_phyctl(int antenna)
{
	switch (antenna) {
	case B43_ANTENNA0:
		return B43_TXH_PHY_ANT0;
	case B43_ANTENNA1:
		return B43_TXH_PHY_ANT1;
	case B43_ANTENNA2:
		return B43_TXH_PHY_ANT2;
	case B43_ANTENNA3:
		return B43_TXH_PHY_ANT3;
	case B43_ANTENNA_AUTO0:
	case B43_ANTENNA_AUTO1:
		return B43_TXH_PHY_ANT01AUTO;
	}
	B43_WARN_ON(1);
	return 0;
}

static void b43_write_beacon_template(struct b43_wldev *dev,
				      u16 ram_offset,
				      u16 shm_size_offset)
{
	unsigned int i, len, variable_len;
	const struct ieee80211_mgmt *bcn;
	const u8 *ie;
	bool tim_found = false;
	unsigned int rate;
	u16 ctl;
	int antenna;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(dev->wl->current_beacon);

	bcn = (const struct ieee80211_mgmt *)(dev->wl->current_beacon->data);
	len = min((size_t) dev->wl->current_beacon->len,
		  0x200 - sizeof(struct b43_plcp_hdr6));
	rate = ieee80211_get_tx_rate(dev->wl->hw, info)->hw_value;

	b43_write_template_common(dev, (const u8 *)bcn,
				  len, ram_offset, shm_size_offset, rate);

	/* Write the PHY TX control parameters. */
	antenna = B43_ANTENNA_DEFAULT;
	antenna = b43_antenna_to_phyctl(antenna);
	ctl = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL);
	/* We can't send beacons with short preamble. Would get PHY errors. */
	ctl &= ~B43_TXH_PHY_SHORTPRMBL;
	ctl &= ~B43_TXH_PHY_ANT;
	ctl &= ~B43_TXH_PHY_ENC;
	ctl |= antenna;
	if (b43_is_cck_rate(rate))
		ctl |= B43_TXH_PHY_ENC_CCK;
	else
		ctl |= B43_TXH_PHY_ENC_OFDM;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL, ctl);

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
			tim_found = true;

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
		/*
		 * If ucode wants to modify TIM do it behind the beacon, this
		 * will happen, for example, when doing mesh networking.
		 */
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_TIMBPOS,
				len + sizeof(struct b43_plcp_hdr6));
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_DTIMPER, 0);
	}
	b43dbg(dev->wl, "Updated beacon template at 0x%x\n", ram_offset);
}

static void b43_upload_beacon0(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;

	if (wl->beacon0_uploaded)
		return;
	b43_write_beacon_template(dev, 0x68, 0x18);
	wl->beacon0_uploaded = true;
}

static void b43_upload_beacon1(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;

	if (wl->beacon1_uploaded)
		return;
	b43_write_beacon_template(dev, 0x468, 0x1A);
	wl->beacon1_uploaded = true;
}

static void handle_irq_beacon(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	u32 cmd, beacon0_valid, beacon1_valid;

	if (!b43_is_mode(wl, NL80211_IFTYPE_AP) &&
	    !b43_is_mode(wl, NL80211_IFTYPE_MESH_POINT) &&
	    !b43_is_mode(wl, NL80211_IFTYPE_ADHOC))
		return;

	/* This is the bottom half of the asynchronous beacon update. */

	/* Ignore interrupt in the future. */
	dev->irq_mask &= ~B43_IRQ_BEACON;

	cmd = b43_read32(dev, B43_MMIO_MACCMD);
	beacon0_valid = (cmd & B43_MACCMD_BEACON0_VALID);
	beacon1_valid = (cmd & B43_MACCMD_BEACON1_VALID);

	/* Schedule interrupt manually, if busy. */
	if (beacon0_valid && beacon1_valid) {
		b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, B43_IRQ_BEACON);
		dev->irq_mask |= B43_IRQ_BEACON;
		return;
	}

	if (unlikely(wl->beacon_templates_virgin)) {
		/* We never uploaded a beacon before.
		 * Upload both templates now, but only mark one valid. */
		wl->beacon_templates_virgin = false;
		b43_upload_beacon0(dev);
		b43_upload_beacon1(dev);
		cmd = b43_read32(dev, B43_MMIO_MACCMD);
		cmd |= B43_MACCMD_BEACON0_VALID;
		b43_write32(dev, B43_MMIO_MACCMD, cmd);
	} else {
		if (!beacon0_valid) {
			b43_upload_beacon0(dev);
			cmd = b43_read32(dev, B43_MMIO_MACCMD);
			cmd |= B43_MACCMD_BEACON0_VALID;
			b43_write32(dev, B43_MMIO_MACCMD, cmd);
		} else if (!beacon1_valid) {
			b43_upload_beacon1(dev);
			cmd = b43_read32(dev, B43_MMIO_MACCMD);
			cmd |= B43_MACCMD_BEACON1_VALID;
			b43_write32(dev, B43_MMIO_MACCMD, cmd);
		}
	}
}

static void b43_do_beacon_update_trigger_work(struct b43_wldev *dev)
{
	u32 old_irq_mask = dev->irq_mask;

	/* update beacon right away or defer to irq */
	handle_irq_beacon(dev);
	if (old_irq_mask != dev->irq_mask) {
		/* The handler updated the IRQ mask. */
		B43_WARN_ON(!dev->irq_mask);
		if (b43_read32(dev, B43_MMIO_GEN_IRQ_MASK)) {
			b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, dev->irq_mask);
		} else {
			/* Device interrupts are currently disabled. That means
			 * we just ran the hardirq handler and scheduled the
			 * IRQ thread. The thread will write the IRQ mask when
			 * it finished, so there's nothing to do here. Writing
			 * the mask _here_ would incorrectly re-enable IRQs. */
		}
	}
}

static void b43_beacon_update_trigger_work(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work, struct b43_wl,
					 beacon_update_trigger);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (likely(dev && (b43_status(dev) >= B43_STAT_INITIALIZED))) {
		if (b43_bus_host_is_sdio(dev->dev)) {
			/* wl->mutex is enough. */
			b43_do_beacon_update_trigger_work(dev);
			mmiowb();
		} else {
			spin_lock_irq(&wl->hardirq_lock);
			b43_do_beacon_update_trigger_work(dev);
			mmiowb();
			spin_unlock_irq(&wl->hardirq_lock);
		}
	}
	mutex_unlock(&wl->mutex);
}

/* Asynchronously update the packet templates in template RAM.
 * Locking: Requires wl->mutex to be locked. */
static void b43_update_templates(struct b43_wl *wl)
{
	struct sk_buff *beacon;

	/* This is the top half of the ansynchronous beacon update.
	 * The bottom half is the beacon IRQ.
	 * Beacon update must be asynchronous to avoid sending an
	 * invalid beacon. This can happen for example, if the firmware
	 * transmits a beacon while we are updating it. */

	/* We could modify the existing beacon and set the aid bit in
	 * the TIM field, but that would probably require resizing and
	 * moving of data within the beacon template.
	 * Simply request a new beacon and let mac80211 do the hard work. */
	beacon = ieee80211_beacon_get(wl->hw, wl->vif);
	if (unlikely(!beacon))
		return;

	if (wl->current_beacon)
		dev_kfree_skb_any(wl->current_beacon);
	wl->current_beacon = beacon;
	wl->beacon0_uploaded = false;
	wl->beacon1_uploaded = false;
	ieee80211_queue_work(wl->hw, &wl->beacon_update_trigger);
}

static void b43_set_beacon_int(struct b43_wldev *dev, u16 beacon_int)
{
	b43_time_lock(dev);
	if (dev->dev->core_rev >= 3) {
		b43_write32(dev, B43_MMIO_TSF_CFP_REP, (beacon_int << 16));
		b43_write32(dev, B43_MMIO_TSF_CFP_START, (beacon_int << 10));
	} else {
		b43_write16(dev, 0x606, (beacon_int >> 6));
		b43_write16(dev, 0x610, beacon_int);
	}
	b43_time_unlock(dev);
	b43dbg(dev->wl, "Set beacon interval to %u\n", beacon_int);
}

static void b43_handle_firmware_panic(struct b43_wldev *dev)
{
	u16 reason;

	/* Read the register that contains the reason code for the panic. */
	reason = b43_shm_read16(dev, B43_SHM_SCRATCH, B43_FWPANIC_REASON_REG);
	b43err(dev->wl, "Whoopsy, firmware panic! Reason: %u\n", reason);

	switch (reason) {
	default:
		b43dbg(dev->wl, "The panic reason is unknown.\n");
		/* fallthrough */
	case B43_FWPANIC_DIE:
		/* Do not restart the controller or firmware.
		 * The device is nonfunctional from now on.
		 * Restarting would result in this panic to trigger again,
		 * so we avoid that recursion. */
		break;
	case B43_FWPANIC_RESTART:
		b43_controller_restart(dev, "Microcode panic");
		break;
	}
}

static void handle_irq_ucode_debug(struct b43_wldev *dev)
{
	unsigned int i, cnt;
	u16 reason, marker_id, marker_line;
	__le16 *buf;

	/* The proprietary firmware doesn't have this IRQ. */
	if (!dev->fw.opensource)
		return;

	/* Read the register that contains the reason code for this IRQ. */
	reason = b43_shm_read16(dev, B43_SHM_SCRATCH, B43_DEBUGIRQ_REASON_REG);

	switch (reason) {
	case B43_DEBUGIRQ_PANIC:
		b43_handle_firmware_panic(dev);
		break;
	case B43_DEBUGIRQ_DUMP_SHM:
		if (!B43_DEBUG)
			break; /* Only with driver debugging enabled. */
		buf = kmalloc(4096, GFP_ATOMIC);
		if (!buf) {
			b43dbg(dev->wl, "SHM-dump: Failed to allocate memory\n");
			goto out;
		}
		for (i = 0; i < 4096; i += 2) {
			u16 tmp = b43_shm_read16(dev, B43_SHM_SHARED, i);
			buf[i / 2] = cpu_to_le16(tmp);
		}
		b43info(dev->wl, "Shared memory dump:\n");
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET,
			       16, 2, buf, 4096, 1);
		kfree(buf);
		break;
	case B43_DEBUGIRQ_DUMP_REGS:
		if (!B43_DEBUG)
			break; /* Only with driver debugging enabled. */
		b43info(dev->wl, "Microcode register dump:\n");
		for (i = 0, cnt = 0; i < 64; i++) {
			u16 tmp = b43_shm_read16(dev, B43_SHM_SCRATCH, i);
			if (cnt == 0)
				printk(KERN_INFO);
			printk("r%02u: 0x%04X  ", i, tmp);
			cnt++;
			if (cnt == 6) {
				printk("\n");
				cnt = 0;
			}
		}
		printk("\n");
		break;
	case B43_DEBUGIRQ_MARKER:
		if (!B43_DEBUG)
			break; /* Only with driver debugging enabled. */
		marker_id = b43_shm_read16(dev, B43_SHM_SCRATCH,
					   B43_MARKER_ID_REG);
		marker_line = b43_shm_read16(dev, B43_SHM_SCRATCH,
					     B43_MARKER_LINE_REG);
		b43info(dev->wl, "The firmware just executed the MARKER(%u) "
			"at line number %u\n",
			marker_id, marker_line);
		break;
	default:
		b43dbg(dev->wl, "Debug-IRQ triggered for unknown reason: %u\n",
		       reason);
	}
out:
	/* Acknowledge the debug-IRQ, so the firmware can continue. */
	b43_shm_write16(dev, B43_SHM_SCRATCH,
			B43_DEBUGIRQ_REASON_REG, B43_DEBUGIRQ_ACK);
}

static void b43_do_interrupt_thread(struct b43_wldev *dev)
{
	u32 reason;
	u32 dma_reason[ARRAY_SIZE(dev->dma_reason)];
	u32 merged_dma_reason = 0;
	int i;

	if (unlikely(b43_status(dev) != B43_STAT_STARTED))
		return;

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
			b43err(dev->wl, "This device does not support DMA "
			       "on your system. It will now be switched to PIO.\n");
			/* Fall back to PIO transfers if we get fatal DMA errors! */
			dev->use_pio = true;
			b43_controller_restart(dev, "DMA error");
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
	if (dma_reason[0] & B43_DMAIRQ_RX_DONE) {
		if (b43_using_pio_transfers(dev))
			b43_pio_rx(dev->pio.rx_queue);
		else
			b43_dma_rx(dev->dma.rx_ring);
	}
	B43_WARN_ON(dma_reason[1] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[2] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[3] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[4] & B43_DMAIRQ_RX_DONE);
	B43_WARN_ON(dma_reason[5] & B43_DMAIRQ_RX_DONE);

	if (reason & B43_IRQ_TX_OK)
		handle_irq_transmit_status(dev);

	/* Re-enable interrupts on the device by restoring the current interrupt mask. */
	b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, dev->irq_mask);

#if B43_DEBUG
	if (b43_debug(dev, B43_DBG_VERBOSESTATS)) {
		dev->irq_count++;
		for (i = 0; i < ARRAY_SIZE(dev->irq_bit_count); i++) {
			if (reason & (1 << i))
				dev->irq_bit_count[i]++;
		}
	}
#endif
}

/* Interrupt thread handler. Handles device interrupts in thread context. */
static irqreturn_t b43_interrupt_thread_handler(int irq, void *dev_id)
{
	struct b43_wldev *dev = dev_id;

	mutex_lock(&dev->wl->mutex);
	b43_do_interrupt_thread(dev);
	mmiowb();
	mutex_unlock(&dev->wl->mutex);

	return IRQ_HANDLED;
}

static irqreturn_t b43_do_interrupt(struct b43_wldev *dev)
{
	u32 reason;

	/* This code runs under wl->hardirq_lock, but _only_ on non-SDIO busses.
	 * On SDIO, this runs under wl->mutex. */

	reason = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
	if (reason == 0xffffffff)	/* shared IRQ */
		return IRQ_NONE;
	reason &= dev->irq_mask;
	if (!reason)
		return IRQ_NONE;

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
/* Unused ring
	dev->dma_reason[5] = b43_read32(dev, B43_MMIO_DMA5_REASON)
	    & 0x0000DC00;
*/

	/* ACK the interrupt. */
	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, reason);
	b43_write32(dev, B43_MMIO_DMA0_REASON, dev->dma_reason[0]);
	b43_write32(dev, B43_MMIO_DMA1_REASON, dev->dma_reason[1]);
	b43_write32(dev, B43_MMIO_DMA2_REASON, dev->dma_reason[2]);
	b43_write32(dev, B43_MMIO_DMA3_REASON, dev->dma_reason[3]);
	b43_write32(dev, B43_MMIO_DMA4_REASON, dev->dma_reason[4]);
/* Unused ring
	b43_write32(dev, B43_MMIO_DMA5_REASON, dev->dma_reason[5]);
*/

	/* Disable IRQs on the device. The IRQ thread handler will re-enable them. */
	b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, 0);
	/* Save the reason bitmasks for the IRQ thread handler. */
	dev->irq_reason = reason;

	return IRQ_WAKE_THREAD;
}

/* Interrupt handler top-half. This runs with interrupts disabled. */
static irqreturn_t b43_interrupt_handler(int irq, void *dev_id)
{
	struct b43_wldev *dev = dev_id;
	irqreturn_t ret;

	if (unlikely(b43_status(dev) < B43_STAT_STARTED))
		return IRQ_NONE;

	spin_lock(&dev->wl->hardirq_lock);
	ret = b43_do_interrupt(dev);
	mmiowb();
	spin_unlock(&dev->wl->hardirq_lock);

	return ret;
}

/* SDIO interrupt handler. This runs in process context. */
static void b43_sdio_interrupt_handler(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	irqreturn_t ret;

	mutex_lock(&wl->mutex);

	ret = b43_do_interrupt(dev);
	if (ret == IRQ_WAKE_THREAD)
		b43_do_interrupt_thread(dev);

	mutex_unlock(&wl->mutex);
}

void b43_do_release_fw(struct b43_firmware_file *fw)
{
	release_firmware(fw->data);
	fw->data = NULL;
	fw->filename = NULL;
}

static void b43_release_firmware(struct b43_wldev *dev)
{
	b43_do_release_fw(&dev->fw.ucode);
	b43_do_release_fw(&dev->fw.pcm);
	b43_do_release_fw(&dev->fw.initvals);
	b43_do_release_fw(&dev->fw.initvals_band);
}

static void b43_print_fw_helptext(struct b43_wl *wl, bool error)
{
	const char text[] =
		"You must go to " \
		"http://wireless.kernel.org/en/users/Drivers/b43#devicefirmware " \
		"and download the correct firmware for this driver version. " \
		"Please carefully read all instructions on this website.\n";

	if (error)
		b43err(wl, text);
	else
		b43warn(wl, text);
}

static void b43_fw_cb(const struct firmware *firmware, void *context)
{
	struct b43_request_fw_context *ctx = context;

	ctx->blob = firmware;
	complete(&ctx->fw_load_complete);
}

int b43_do_request_fw(struct b43_request_fw_context *ctx,
		      const char *name,
		      struct b43_firmware_file *fw, bool async)
{
	struct b43_fw_header *hdr;
	u32 size;
	int err;

	if (!name) {
		/* Don't fetch anything. Free possibly cached firmware. */
		/* FIXME: We should probably keep it anyway, to save some headache
		 * on suspend/resume with multiband devices. */
		b43_do_release_fw(fw);
		return 0;
	}
	if (fw->filename) {
		if ((fw->type == ctx->req_type) &&
		    (strcmp(fw->filename, name) == 0))
			return 0; /* Already have this fw. */
		/* Free the cached firmware first. */
		/* FIXME: We should probably do this later after we successfully
		 * got the new fw. This could reduce headache with multiband devices.
		 * We could also redesign this to cache the firmware for all possible
		 * bands all the time. */
		b43_do_release_fw(fw);
	}

	switch (ctx->req_type) {
	case B43_FWTYPE_PROPRIETARY:
		snprintf(ctx->fwname, sizeof(ctx->fwname),
			 "b43%s/%s.fw",
			 modparam_fwpostfix, name);
		break;
	case B43_FWTYPE_OPENSOURCE:
		snprintf(ctx->fwname, sizeof(ctx->fwname),
			 "b43-open%s/%s.fw",
			 modparam_fwpostfix, name);
		break;
	default:
		B43_WARN_ON(1);
		return -ENOSYS;
	}
	if (async) {
		/* do this part asynchronously */
		init_completion(&ctx->fw_load_complete);
		err = request_firmware_nowait(THIS_MODULE, 1, ctx->fwname,
					      ctx->dev->dev->dev, GFP_KERNEL,
					      ctx, b43_fw_cb);
		if (err < 0) {
			pr_err("Unable to load firmware\n");
			return err;
		}
		/* stall here until fw ready */
		wait_for_completion(&ctx->fw_load_complete);
		if (ctx->blob)
			goto fw_ready;
	/* On some ARM systems, the async request will fail, but the next sync
	 * request works. For this reason, we dall through here
	 */
	}
	err = request_firmware(&ctx->blob, ctx->fwname,
			       ctx->dev->dev->dev);
	if (err == -ENOENT) {
		snprintf(ctx->errors[ctx->req_type],
			 sizeof(ctx->errors[ctx->req_type]),
			 "Firmware file \"%s\" not found\n",
			 ctx->fwname);
		return err;
	} else if (err) {
		snprintf(ctx->errors[ctx->req_type],
			 sizeof(ctx->errors[ctx->req_type]),
			 "Firmware file \"%s\" request failed (err=%d)\n",
			 ctx->fwname, err);
		return err;
	}
fw_ready:
	if (ctx->blob->size < sizeof(struct b43_fw_header))
		goto err_format;
	hdr = (struct b43_fw_header *)(ctx->blob->data);
	switch (hdr->type) {
	case B43_FW_TYPE_UCODE:
	case B43_FW_TYPE_PCM:
		size = be32_to_cpu(hdr->size);
		if (size != ctx->blob->size - sizeof(struct b43_fw_header))
			goto err_format;
		/* fallthrough */
	case B43_FW_TYPE_IV:
		if (hdr->ver != 1)
			goto err_format;
		break;
	default:
		goto err_format;
	}

	fw->data = ctx->blob;
	fw->filename = name;
	fw->type = ctx->req_type;

	return 0;

err_format:
	snprintf(ctx->errors[ctx->req_type],
		 sizeof(ctx->errors[ctx->req_type]),
		 "Firmware file \"%s\" format error.\n", ctx->fwname);
	release_firmware(ctx->blob);

	return -EPROTO;
}

static int b43_try_request_fw(struct b43_request_fw_context *ctx)
{
	struct b43_wldev *dev = ctx->dev;
	struct b43_firmware *fw = &ctx->dev->fw;
	const u8 rev = ctx->dev->dev->core_rev;
	const char *filename;
	u32 tmshigh;
	int err;

	/* Files for HT and LCN were found by trying one by one */

	/* Get microcode */
	if ((rev >= 5) && (rev <= 10)) {
		filename = "ucode5";
	} else if ((rev >= 11) && (rev <= 12)) {
		filename = "ucode11";
	} else if (rev == 13) {
		filename = "ucode13";
	} else if (rev == 14) {
		filename = "ucode14";
	} else if (rev == 15) {
		filename = "ucode15";
	} else {
		switch (dev->phy.type) {
		case B43_PHYTYPE_N:
			if (rev >= 16)
				filename = "ucode16_mimo";
			else
				goto err_no_ucode;
			break;
		case B43_PHYTYPE_HT:
			if (rev == 29)
				filename = "ucode29_mimo";
			else
				goto err_no_ucode;
			break;
		case B43_PHYTYPE_LCN:
			if (rev == 24)
				filename = "ucode24_mimo";
			else
				goto err_no_ucode;
			break;
		default:
			goto err_no_ucode;
		}
	}
	err = b43_do_request_fw(ctx, filename, &fw->ucode, true);
	if (err)
		goto err_load;

	/* Get PCM code */
	if ((rev >= 5) && (rev <= 10))
		filename = "pcm5";
	else if (rev >= 11)
		filename = NULL;
	else
		goto err_no_pcm;
	fw->pcm_request_failed = false;
	err = b43_do_request_fw(ctx, filename, &fw->pcm, false);
	if (err == -ENOENT) {
		/* We did not find a PCM file? Not fatal, but
		 * core rev <= 10 must do without hwcrypto then. */
		fw->pcm_request_failed = true;
	} else if (err)
		goto err_load;

	/* Get initvals */
	switch (dev->phy.type) {
	case B43_PHYTYPE_A:
		if ((rev >= 5) && (rev <= 10)) {
			tmshigh = ssb_read32(dev->dev->sdev, SSB_TMSHIGH);
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
			filename = "b0g0initvals13";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_N:
		if (rev >= 16)
			filename = "n0initvals16";
		else if ((rev >= 11) && (rev <= 12))
			filename = "n0initvals11";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_LP:
		if (rev == 13)
			filename = "lp0initvals13";
		else if (rev == 14)
			filename = "lp0initvals14";
		else if (rev >= 15)
			filename = "lp0initvals15";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_HT:
		if (rev == 29)
			filename = "ht0initvals29";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_LCN:
		if (rev == 24)
			filename = "lcn0initvals24";
		else
			goto err_no_initvals;
		break;
	default:
		goto err_no_initvals;
	}
	err = b43_do_request_fw(ctx, filename, &fw->initvals, false);
	if (err)
		goto err_load;

	/* Get bandswitch initvals */
	switch (dev->phy.type) {
	case B43_PHYTYPE_A:
		if ((rev >= 5) && (rev <= 10)) {
			tmshigh = ssb_read32(dev->dev->sdev, SSB_TMSHIGH);
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
		if (rev >= 16)
			filename = "n0bsinitvals16";
		else if ((rev >= 11) && (rev <= 12))
			filename = "n0bsinitvals11";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_LP:
		if (rev == 13)
			filename = "lp0bsinitvals13";
		else if (rev == 14)
			filename = "lp0bsinitvals14";
		else if (rev >= 15)
			filename = "lp0bsinitvals15";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_HT:
		if (rev == 29)
			filename = "ht0bsinitvals29";
		else
			goto err_no_initvals;
		break;
	case B43_PHYTYPE_LCN:
		if (rev == 24)
			filename = "lcn0bsinitvals24";
		else
			goto err_no_initvals;
		break;
	default:
		goto err_no_initvals;
	}
	err = b43_do_request_fw(ctx, filename, &fw->initvals_band, false);
	if (err)
		goto err_load;

	fw->opensource = (ctx->req_type == B43_FWTYPE_OPENSOURCE);

	return 0;

err_no_ucode:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (ucode) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_no_pcm:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (PCM) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_no_initvals:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (initvals) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_load:
	/* We failed to load this firmware image. The error message
	 * already is in ctx->errors. Return and let our caller decide
	 * what to do. */
	goto error;

error:
	b43_release_firmware(dev);
	return err;
}

static int b43_one_core_attach(struct b43_bus_dev *dev, struct b43_wl *wl);
static void b43_one_core_detach(struct b43_bus_dev *dev);

static void b43_request_firmware(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work,
			    struct b43_wl, firmware_load);
	struct b43_wldev *dev = wl->current_dev;
	struct b43_request_fw_context *ctx;
	unsigned int i;
	int err;
	const char *errmsg;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return;
	ctx->dev = dev;

	ctx->req_type = B43_FWTYPE_PROPRIETARY;
	err = b43_try_request_fw(ctx);
	if (!err)
		goto start_ieee80211; /* Successfully loaded it. */
	/* Was fw version known? */
	if (ctx->fatal_failure)
		goto out;

	/* proprietary fw not found, try open source */
	ctx->req_type = B43_FWTYPE_OPENSOURCE;
	err = b43_try_request_fw(ctx);
	if (!err)
		goto start_ieee80211; /* Successfully loaded it. */
	if(ctx->fatal_failure)
		goto out;

	/* Could not find a usable firmware. Print the errors. */
	for (i = 0; i < B43_NR_FWTYPES; i++) {
		errmsg = ctx->errors[i];
		if (strlen(errmsg))
			b43err(dev->wl, errmsg);
	}
	b43_print_fw_helptext(dev->wl, 1);
	goto out;

start_ieee80211:
	wl->hw->queues = B43_QOS_QUEUE_NUM;
	if (!modparam_qos || dev->fw.opensource)
		wl->hw->queues = 1;

	err = ieee80211_register_hw(wl->hw);
	if (err)
		goto err_one_core_detach;
	wl->hw_registred = true;
	b43_leds_register(wl->current_dev);
	goto out;

err_one_core_detach:
	b43_one_core_detach(dev->dev);

out:
	kfree(ctx);
}

static int b43_upload_microcode(struct b43_wldev *dev)
{
	struct wiphy *wiphy = dev->wl->hw->wiphy;
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
	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_PSM_JMP0,
		      B43_MACCTL_PSM_RUN);

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
		msleep(50);
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
	dev->fw.rev = fwrev;
	dev->fw.patch = fwpatch;
	if (dev->fw.rev >= 598)
		dev->fw.hdr_format = B43_FW_HDR_598;
	else if (dev->fw.rev >= 410)
		dev->fw.hdr_format = B43_FW_HDR_410;
	else
		dev->fw.hdr_format = B43_FW_HDR_351;
	WARN_ON(dev->fw.opensource != (fwdate == 0xFFFF));

	dev->qos_enabled = dev->wl->hw->queues > 1;
	/* Default to firmware/hardware crypto acceleration. */
	dev->hwcrypto_enabled = true;

	if (dev->fw.opensource) {
		u16 fwcapa;

		/* Patchlevel info is encoded in the "time" field. */
		dev->fw.patch = fwtime;
		b43info(dev->wl, "Loading OpenSource firmware version %u.%u\n",
			dev->fw.rev, dev->fw.patch);

		fwcapa = b43_fwcapa_read(dev);
		if (!(fwcapa & B43_FWCAPA_HWCRYPTO) || dev->fw.pcm_request_failed) {
			b43info(dev->wl, "Hardware crypto acceleration not supported by firmware\n");
			/* Disable hardware crypto and fall back to software crypto. */
			dev->hwcrypto_enabled = false;
		}
		/* adding QoS support should use an offline discovery mechanism */
		WARN(fwcapa & B43_FWCAPA_QOS, "QoS in OpenFW not supported\n");
	} else {
		b43info(dev->wl, "Loading firmware version %u.%u "
			"(20%.2i-%.2i-%.2i %.2i:%.2i:%.2i)\n",
			fwrev, fwpatch,
			(fwdate >> 12) & 0xF, (fwdate >> 8) & 0xF, fwdate & 0xFF,
			(fwtime >> 11) & 0x1F, (fwtime >> 5) & 0x3F, fwtime & 0x1F);
		if (dev->fw.pcm_request_failed) {
			b43warn(dev->wl, "No \"pcm5.fw\" firmware file found. "
				"Hardware accelerated cryptography is disabled.\n");
			b43_print_fw_helptext(dev->wl, 0);
		}
	}

	snprintf(wiphy->fw_version, sizeof(wiphy->fw_version), "%u.%u",
			dev->fw.rev, dev->fw.patch);
	wiphy->hw_version = dev->dev->core_id;

	if (dev->fw.hdr_format == B43_FW_HDR_351) {
		/* We're over the deadline, but we keep support for old fw
		 * until it turns out to be in major conflict with something new. */
		b43warn(dev->wl, "You are using an old firmware image. "
			"Support for old firmware will be removed soon "
			"(official deadline was July 2008).\n");
		b43_print_fw_helptext(dev->wl, 0);
	}

	return 0;

error:
	/* Stop the microcode PSM. */
	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_PSM_RUN,
		      B43_MACCTL_PSM_JMP0);

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

			value = get_unaligned_be32(&iv->data.d32);
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
static struct ssb_device *b43_ssb_gpio_dev(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->sdev->bus;

#ifdef CONFIG_SSB_DRIVER_PCICORE
	return (bus->chipco.dev ? bus->chipco.dev : bus->pcicore.dev);
#else
	return bus->chipco.dev;
#endif
}

static int b43_gpio_init(struct b43_wldev *dev)
{
	struct ssb_device *gpiodev;
	u32 mask, set;

	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_GPOUTSMSK, 0);
	b43_maskset16(dev, B43_MMIO_GPIO_MASK, ~0, 0xF);

	mask = 0x0000001F;
	set = 0x0000000F;
	if (dev->dev->chip_id == 0x4301) {
		mask |= 0x0060;
		set |= 0x0060;
	} else if (dev->dev->chip_id == 0x5354) {
		/* Don't allow overtaking buttons GPIOs */
		set &= 0x2; /* 0x2 is LED GPIO on BCM5354 */
	}

	if (0 /* FIXME: conditional unknown */ ) {
		b43_write16(dev, B43_MMIO_GPIO_MASK,
			    b43_read16(dev, B43_MMIO_GPIO_MASK)
			    | 0x0100);
		/* BT Coexistance Input */
		mask |= 0x0080;
		set |= 0x0080;
		/* BT Coexistance Out */
		mask |= 0x0100;
		set |= 0x0100;
	}
	if (dev->dev->bus_sprom->boardflags_lo & B43_BFL_PACTRL) {
		/* PA is controlled by gpio 9, let ucode handle it */
		b43_write16(dev, B43_MMIO_GPIO_MASK,
			    b43_read16(dev, B43_MMIO_GPIO_MASK)
			    | 0x0200);
		mask |= 0x0200;
		set |= 0x0200;
	}

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		bcma_cc_write32(&dev->dev->bdev->bus->drv_cc, BCMA_CC_GPIOCTL,
				(bcma_cc_read32(&dev->dev->bdev->bus->drv_cc,
					BCMA_CC_GPIOCTL) & ~mask) | set);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		gpiodev = b43_ssb_gpio_dev(dev);
		if (gpiodev)
			ssb_write32(gpiodev, B43_GPIO_CONTROL,
				    (ssb_read32(gpiodev, B43_GPIO_CONTROL)
				    & ~mask) | set);
		break;
#endif
	}

	return 0;
}

/* Turn off all GPIO stuff. Call this on module unload, for example. */
static void b43_gpio_cleanup(struct b43_wldev *dev)
{
	struct ssb_device *gpiodev;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		bcma_cc_write32(&dev->dev->bdev->bus->drv_cc, BCMA_CC_GPIOCTL,
				0);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		gpiodev = b43_ssb_gpio_dev(dev);
		if (gpiodev)
			ssb_write32(gpiodev, B43_GPIO_CONTROL, 0);
		break;
#endif
	}
}

/* http://bcm-specs.sipsolutions.net/EnableMac */
void b43_mac_enable(struct b43_wldev *dev)
{
	if (b43_debug(dev, B43_DBG_FIRMWARE)) {
		u16 fwstate;

		fwstate = b43_shm_read16(dev, B43_SHM_SHARED,
					 B43_SHM_SH_UCODESTAT);
		if ((fwstate != B43_SHM_SH_UCODESTAT_SUSP) &&
		    (fwstate != B43_SHM_SH_UCODESTAT_SLEEP)) {
			b43err(dev->wl, "b43_mac_enable(): The firmware "
			       "should be suspended, but current state is %u\n",
			       fwstate);
		}
	}

	dev->mac_suspended--;
	B43_WARN_ON(dev->mac_suspended < 0);
	if (dev->mac_suspended == 0) {
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0, B43_MACCTL_ENABLED);
		b43_write32(dev, B43_MMIO_GEN_IRQ_REASON,
			    B43_IRQ_MAC_SUSPENDED);
		/* Commit writes */
		b43_read32(dev, B43_MMIO_MACCTL);
		b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
		b43_power_saving_ctl_bits(dev, 0);
	}
}

/* http://bcm-specs.sipsolutions.net/SuspendMAC */
void b43_mac_suspend(struct b43_wldev *dev)
{
	int i;
	u32 tmp;

	might_sleep();
	B43_WARN_ON(dev->mac_suspended < 0);

	if (dev->mac_suspended == 0) {
		b43_power_saving_ctl_bits(dev, B43_PS_AWAKE);
		b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_ENABLED, 0);
		/* force pci to flush the write */
		b43_read32(dev, B43_MMIO_MACCTL);
		for (i = 35; i; i--) {
			tmp = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
			if (tmp & B43_IRQ_MAC_SUSPENDED)
				goto out;
			udelay(10);
		}
		/* Hm, it seems this will take some time. Use msleep(). */
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

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/MacPhyClkSet */
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on)
{
	u32 tmp;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOCTL);
		if (on)
			tmp |= B43_BCMA_IOCTL_MACPHYCLKEN;
		else
			tmp &= ~B43_BCMA_IOCTL_MACPHYCLKEN;
		bcma_awrite32(dev->dev->bdev, BCMA_IOCTL, tmp);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		if (on)
			tmp |= B43_TMSLOW_MACPHYCLKEN;
		else
			tmp &= ~B43_TMSLOW_MACPHYCLKEN;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		break;
#endif
	}
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

	if (b43_is_mode(wl, NL80211_IFTYPE_AP) ||
	    b43_is_mode(wl, NL80211_IFTYPE_MESH_POINT))
		ctl |= B43_MACCTL_AP;
	else if (b43_is_mode(wl, NL80211_IFTYPE_ADHOC))
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
	if (dev->dev->core_rev <= 4)
		ctl |= B43_MACCTL_PROMISC;

	b43_write32(dev, B43_MMIO_MACCTL, ctl);

	cfp_pretbtt = 2;
	if ((ctl & B43_MACCTL_INFRA) && !(ctl & B43_MACCTL_AP)) {
		if (dev->dev->chip_id == 0x4306 &&
		    dev->dev->chip_rev == 3)
			cfp_pretbtt = 100;
		else
			cfp_pretbtt = 50;
	}
	b43_write16(dev, 0x612, cfp_pretbtt);

	/* FIXME: We don't currently implement the PMQ mechanism,
	 *        so always disable it. If we want to implement PMQ,
	 *        we need to enable it here (clear DISCPMQ) in AP mode.
	 */
	if (0  /* ctl & B43_MACCTL_AP */)
		b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_DISCPMQ, 0);
	else
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0, B43_MACCTL_DISCPMQ);
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
	case B43_PHYTYPE_LP:
	case B43_PHYTYPE_HT:
	case B43_PHYTYPE_LCN:
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

/* Set the default values for the PHY TX Control Words. */
static void b43_set_phytxctl_defaults(struct b43_wldev *dev)
{
	u16 ctl = 0;

	ctl |= B43_TXH_PHY_ENC_CCK;
	ctl |= B43_TXH_PHY_ANT01AUTO;
	ctl |= B43_TXH_PHY_TXPWR;

	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL, ctl);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_ACKCTSPHYCTL, ctl);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_PRPHYCTL, ctl);
}

/* Set the TX-Antenna for management frames sent by firmware. */
static void b43_mgmtframe_txantenna(struct b43_wldev *dev, int antenna)
{
	u16 ant;
	u16 tmp;

	ant = b43_antenna_to_phyctl(antenna);

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
	b43_phy_exit(dev);
	b43_gpio_cleanup(dev);
	/* firmware is released later */
}

/* Initialize the chip
 * http://bcm-specs.sipsolutions.net/ChipInit
 */
static int b43_chip_init(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	int err;
	u32 macctl;
	u16 value16;

	/* Initialize the MAC control */
	macctl = B43_MACCTL_IHR_ENABLED | B43_MACCTL_SHM_ENABLED;
	if (dev->phy.gmode)
		macctl |= B43_MACCTL_GMODE;
	macctl |= B43_MACCTL_INFRA;
	b43_write32(dev, B43_MMIO_MACCTL, macctl);

	err = b43_upload_microcode(dev);
	if (err)
		goto out;	/* firmware is released later */

	err = b43_gpio_init(dev);
	if (err)
		goto out;	/* firmware is released later */

	err = b43_upload_initvals(dev);
	if (err)
		goto err_gpio_clean;

	/* Turn the Analog on and initialize the PHY. */
	phy->ops->switch_analog(dev, 1);
	err = b43_phy_init(dev);
	if (err)
		goto err_gpio_clean;

	/* Disable Interference Mitigation. */
	if (phy->ops->interf_mitigation)
		phy->ops->interf_mitigation(dev, B43_INTERFMODE_NONE);

	/* Select the antennae */
	if (phy->ops->set_rx_antenna)
		phy->ops->set_rx_antenna(dev, B43_ANTENNA_DEFAULT);
	b43_mgmtframe_txantenna(dev, B43_ANTENNA_DEFAULT);

	if (phy->type == B43_PHYTYPE_B) {
		value16 = b43_read16(dev, 0x005E);
		value16 |= 0x0004;
		b43_write16(dev, 0x005E, value16);
	}
	b43_write32(dev, 0x0100, 0x01000000);
	if (dev->dev->core_rev < 5)
		b43_write32(dev, 0x010C, 0x01000000);

	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_INFRA, 0);
	b43_maskset32(dev, B43_MMIO_MACCTL, ~0, B43_MACCTL_INFRA);

	/* Probe Response Timeout value */
	/* FIXME: Default to 0, has to be set by ioctl probably... :-/ */
	b43_shm_write16(dev, B43_SHM_SHARED, 0x0074, 0x0000);

	/* Initially set the wireless operation mode. */
	b43_adjust_opmode(dev);

	if (dev->dev->core_rev < 3) {
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

	b43_mac_phy_clock_set(dev, true);

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		/* FIXME: 0xE74 is quite common, but should be read from CC */
		b43_write16(dev, B43_MMIO_POWERUP_DELAY, 0xE74);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		b43_write16(dev, B43_MMIO_POWERUP_DELAY,
			    dev->dev->sdev->bus->chipco.fast_pwrup_delay);
		break;
#endif
	}

	err = 0;
	b43dbg(dev->wl, "Chip initialized\n");
out:
	return err;

err_gpio_clean:
	b43_gpio_cleanup(dev);
	return err;
}

static void b43_periodic_every60sec(struct b43_wldev *dev)
{
	const struct b43_phy_operations *ops = dev->phy.ops;

	if (ops->pwork_60sec)
		ops->pwork_60sec(dev);

	/* Force check the TX power emission now. */
	b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME);
}

static void b43_periodic_every30sec(struct b43_wldev *dev)
{
	/* Update device statistics. */
	b43_calculate_link_quality(dev);
}

static void b43_periodic_every15sec(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 wdr;

	if (dev->fw.opensource) {
		/* Check if the firmware is still alive.
		 * It will reset the watchdog counter to 0 in its idle loop. */
		wdr = b43_shm_read16(dev, B43_SHM_SCRATCH, B43_WATCHDOG_REG);
		if (unlikely(wdr)) {
			b43err(dev->wl, "Firmware watchdog: The firmware died!\n");
			b43_controller_restart(dev, "Firmware watchdog");
			return;
		} else {
			b43_shm_write16(dev, B43_SHM_SCRATCH,
					B43_WATCHDOG_REG, 1);
		}
	}

	if (phy->ops->pwork_15sec)
		phy->ops->pwork_15sec(dev);

	atomic_set(&phy->txerr_cnt, B43_PHY_TX_BADNESS_LIMIT);
	wmb();

#if B43_DEBUG
	if (b43_debug(dev, B43_DBG_VERBOSESTATS)) {
		unsigned int i;

		b43dbg(dev->wl, "Stats: %7u IRQs/sec, %7u TX/sec, %7u RX/sec\n",
		       dev->irq_count / 15,
		       dev->tx_count / 15,
		       dev->rx_count / 15);
		dev->irq_count = 0;
		dev->tx_count = 0;
		dev->rx_count = 0;
		for (i = 0; i < ARRAY_SIZE(dev->irq_bit_count); i++) {
			if (dev->irq_bit_count[i]) {
				b43dbg(dev->wl, "Stats: %7u IRQ-%02u/sec (0x%08X)\n",
				       dev->irq_bit_count[i] / 15, i, (1 << i));
				dev->irq_bit_count[i] = 0;
			}
		}
	}
#endif
}

static void do_periodic_work(struct b43_wldev *dev)
{
	unsigned int state;

	state = dev->periodic_state;
	if (state % 4 == 0)
		b43_periodic_every60sec(dev);
	if (state % 2 == 0)
		b43_periodic_every30sec(dev);
	b43_periodic_every15sec(dev);
}

/* Periodic work locking policy:
 * 	The whole periodic work handler is protected by
 * 	wl->mutex. If another lock is needed somewhere in the
 * 	pwork callchain, it's acquired in-place, where it's needed.
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
	ieee80211_queue_delayed_work(wl->hw, &dev->periodic_work, delay);
out:
	mutex_unlock(&wl->mutex);
}

static void b43_periodic_tasks_setup(struct b43_wldev *dev)
{
	struct delayed_work *work = &dev->periodic_work;

	dev->periodic_state = 0;
	INIT_DELAYED_WORK(work, b43_periodic_work_handler);
	ieee80211_queue_delayed_work(dev->wl->hw, work, 0);
}

/* Check if communication with the device works correctly. */
static int b43_validate_chipaccess(struct b43_wldev *dev)
{
	u32 v, backup0, backup4;

	backup0 = b43_shm_read32(dev, B43_SHM_SHARED, 0);
	backup4 = b43_shm_read32(dev, B43_SHM_SHARED, 4);

	/* Check for read/write and endianness problems. */
	b43_shm_write32(dev, B43_SHM_SHARED, 0, 0x55AAAA55);
	if (b43_shm_read32(dev, B43_SHM_SHARED, 0) != 0x55AAAA55)
		goto error;
	b43_shm_write32(dev, B43_SHM_SHARED, 0, 0xAA5555AA);
	if (b43_shm_read32(dev, B43_SHM_SHARED, 0) != 0xAA5555AA)
		goto error;

	/* Check if unaligned 32bit SHM_SHARED access works properly.
	 * However, don't bail out on failure, because it's noncritical. */
	b43_shm_write16(dev, B43_SHM_SHARED, 0, 0x1122);
	b43_shm_write16(dev, B43_SHM_SHARED, 2, 0x3344);
	b43_shm_write16(dev, B43_SHM_SHARED, 4, 0x5566);
	b43_shm_write16(dev, B43_SHM_SHARED, 6, 0x7788);
	if (b43_shm_read32(dev, B43_SHM_SHARED, 2) != 0x55663344)
		b43warn(dev->wl, "Unaligned 32bit SHM read access is broken\n");
	b43_shm_write32(dev, B43_SHM_SHARED, 2, 0xAABBCCDD);
	if (b43_shm_read16(dev, B43_SHM_SHARED, 0) != 0x1122 ||
	    b43_shm_read16(dev, B43_SHM_SHARED, 2) != 0xCCDD ||
	    b43_shm_read16(dev, B43_SHM_SHARED, 4) != 0xAABB ||
	    b43_shm_read16(dev, B43_SHM_SHARED, 6) != 0x7788)
		b43warn(dev->wl, "Unaligned 32bit SHM write access is broken\n");

	b43_shm_write32(dev, B43_SHM_SHARED, 0, backup0);
	b43_shm_write32(dev, B43_SHM_SHARED, 4, backup4);

	if ((dev->dev->core_rev >= 3) && (dev->dev->core_rev <= 10)) {
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
	dev->ktp = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_KTP);
	/* KTP is a word address, but we address SHM bytewise.
	 * So multiply by two.
	 */
	dev->ktp *= 2;
	/* Number of RCMTA address slots */
	b43_write16(dev, B43_MMIO_RCMTA_COUNT, B43_NR_PAIRWISE_KEYS);
	/* Clear the key memory. */
	b43_clear_keys(dev);
}

#ifdef CONFIG_B43_HWRNG
static int b43_rng_read(struct hwrng *rng, u32 *data)
{
	struct b43_wl *wl = (struct b43_wl *)rng->priv;
	struct b43_wldev *dev;
	int count = -ENODEV;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (likely(dev && b43_status(dev) >= B43_STAT_INITIALIZED)) {
		*data = b43_read16(dev, B43_MMIO_RNG);
		count = sizeof(u16);
	}
	mutex_unlock(&wl->mutex);

	return count;
}
#endif /* CONFIG_B43_HWRNG */

static void b43_rng_exit(struct b43_wl *wl)
{
#ifdef CONFIG_B43_HWRNG
	if (wl->rng_initialized)
		hwrng_unregister(&wl->rng);
#endif /* CONFIG_B43_HWRNG */
}

static int b43_rng_init(struct b43_wl *wl)
{
	int err = 0;

#ifdef CONFIG_B43_HWRNG
	snprintf(wl->rng_name, ARRAY_SIZE(wl->rng_name),
		 "%s_%s", KBUILD_MODNAME, wiphy_name(wl->hw->wiphy));
	wl->rng.name = wl->rng_name;
	wl->rng.data_read = b43_rng_read;
	wl->rng.priv = (unsigned long)wl;
	wl->rng_initialized = true;
	err = hwrng_register(&wl->rng);
	if (err) {
		wl->rng_initialized = false;
		b43err(wl, "Failed to register the random "
		       "number generator (%d)\n", err);
	}
#endif /* CONFIG_B43_HWRNG */

	return err;
}

static void b43_tx_work(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work, struct b43_wl, tx_work);
	struct b43_wldev *dev;
	struct sk_buff *skb;
	int queue_num;
	int err = 0;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (unlikely(!dev || b43_status(dev) < B43_STAT_STARTED)) {
		mutex_unlock(&wl->mutex);
		return;
	}

	for (queue_num = 0; queue_num < B43_QOS_QUEUE_NUM; queue_num++) {
		while (skb_queue_len(&wl->tx_queue[queue_num])) {
			skb = skb_dequeue(&wl->tx_queue[queue_num]);
			if (b43_using_pio_transfers(dev))
				err = b43_pio_tx(dev, skb);
			else
				err = b43_dma_tx(dev, skb);
			if (err == -ENOSPC) {
				wl->tx_queue_stopped[queue_num] = 1;
				ieee80211_stop_queue(wl->hw, queue_num);
				skb_queue_head(&wl->tx_queue[queue_num], skb);
				break;
			}
			if (unlikely(err))
				ieee80211_free_txskb(wl->hw, skb);
			err = 0;
		}

		if (!err)
			wl->tx_queue_stopped[queue_num] = 0;
	}

#if B43_DEBUG
	dev->tx_count++;
#endif
	mutex_unlock(&wl->mutex);
}

static void b43_op_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);

	if (unlikely(skb->len < 2 + 2 + 6)) {
		/* Too short, this can't be a valid frame. */
		ieee80211_free_txskb(hw, skb);
		return;
	}
	B43_WARN_ON(skb_shinfo(skb)->nr_frags);

	skb_queue_tail(&wl->tx_queue[skb->queue_mapping], skb);
	if (!wl->tx_queue_stopped[skb->queue_mapping]) {
		ieee80211_queue_work(wl->hw, &wl->tx_work);
	} else {
		ieee80211_stop_queue(wl->hw, skb->queue_mapping);
	}
}

static void b43_qos_params_upload(struct b43_wldev *dev,
				  const struct ieee80211_tx_queue_params *p,
				  u16 shm_offset)
{
	u16 params[B43_NR_QOSPARAMS];
	int bslots, tmp;
	unsigned int i;

	if (!dev->qos_enabled)
		return;

	bslots = b43_read16(dev, B43_MMIO_RNG) & p->cw_min;

	memset(&params, 0, sizeof(params));

	params[B43_QOSPARAM_TXOP] = p->txop * 32;
	params[B43_QOSPARAM_CWMIN] = p->cw_min;
	params[B43_QOSPARAM_CWMAX] = p->cw_max;
	params[B43_QOSPARAM_CWCUR] = p->cw_min;
	params[B43_QOSPARAM_AIFS] = p->aifs;
	params[B43_QOSPARAM_BSLOTS] = bslots;
	params[B43_QOSPARAM_REGGAP] = bslots + p->aifs;

	for (i = 0; i < ARRAY_SIZE(params); i++) {
		if (i == B43_QOSPARAM_STATUS) {
			tmp = b43_shm_read16(dev, B43_SHM_SHARED,
					     shm_offset + (i * 2));
			/* Mark the parameters as updated. */
			tmp |= 0x100;
			b43_shm_write16(dev, B43_SHM_SHARED,
					shm_offset + (i * 2),
					tmp);
		} else {
			b43_shm_write16(dev, B43_SHM_SHARED,
					shm_offset + (i * 2),
					params[i]);
		}
	}
}

/* Mapping of mac80211 queue numbers to b43 QoS SHM offsets. */
static const u16 b43_qos_shm_offsets[] = {
	/* [mac80211-queue-nr] = SHM_OFFSET, */
	[0] = B43_QOS_VOICE,
	[1] = B43_QOS_VIDEO,
	[2] = B43_QOS_BESTEFFORT,
	[3] = B43_QOS_BACKGROUND,
};

/* Update all QOS parameters in hardware. */
static void b43_qos_upload_all(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct b43_qos_params *params;
	unsigned int i;

	if (!dev->qos_enabled)
		return;

	BUILD_BUG_ON(ARRAY_SIZE(b43_qos_shm_offsets) !=
		     ARRAY_SIZE(wl->qos_params));

	b43_mac_suspend(dev);
	for (i = 0; i < ARRAY_SIZE(wl->qos_params); i++) {
		params = &(wl->qos_params[i]);
		b43_qos_params_upload(dev, &(params->p),
				      b43_qos_shm_offsets[i]);
	}
	b43_mac_enable(dev);
}

static void b43_qos_clear(struct b43_wl *wl)
{
	struct b43_qos_params *params;
	unsigned int i;

	/* Initialize QoS parameters to sane defaults. */

	BUILD_BUG_ON(ARRAY_SIZE(b43_qos_shm_offsets) !=
		     ARRAY_SIZE(wl->qos_params));

	for (i = 0; i < ARRAY_SIZE(wl->qos_params); i++) {
		params = &(wl->qos_params[i]);

		switch (b43_qos_shm_offsets[i]) {
		case B43_QOS_VOICE:
			params->p.txop = 0;
			params->p.aifs = 2;
			params->p.cw_min = 0x0001;
			params->p.cw_max = 0x0001;
			break;
		case B43_QOS_VIDEO:
			params->p.txop = 0;
			params->p.aifs = 2;
			params->p.cw_min = 0x0001;
			params->p.cw_max = 0x0001;
			break;
		case B43_QOS_BESTEFFORT:
			params->p.txop = 0;
			params->p.aifs = 3;
			params->p.cw_min = 0x0001;
			params->p.cw_max = 0x03FF;
			break;
		case B43_QOS_BACKGROUND:
			params->p.txop = 0;
			params->p.aifs = 7;
			params->p.cw_min = 0x0001;
			params->p.cw_max = 0x03FF;
			break;
		default:
			B43_WARN_ON(1);
		}
	}
}

/* Initialize the core's QOS capabilities */
static void b43_qos_init(struct b43_wldev *dev)
{
	if (!dev->qos_enabled) {
		/* Disable QOS support. */
		b43_hf_write(dev, b43_hf_read(dev) & ~B43_HF_EDCF);
		b43_write16(dev, B43_MMIO_IFSCTL,
			    b43_read16(dev, B43_MMIO_IFSCTL)
			    & ~B43_MMIO_IFSCTL_USE_EDCF);
		b43dbg(dev->wl, "QoS disabled\n");
		return;
	}

	/* Upload the current QOS parameters. */
	b43_qos_upload_all(dev);

	/* Enable QOS support. */
	b43_hf_write(dev, b43_hf_read(dev) | B43_HF_EDCF);
	b43_write16(dev, B43_MMIO_IFSCTL,
		    b43_read16(dev, B43_MMIO_IFSCTL)
		    | B43_MMIO_IFSCTL_USE_EDCF);
	b43dbg(dev->wl, "QoS enabled\n");
}

static int b43_op_conf_tx(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif, u16 _queue,
			  const struct ieee80211_tx_queue_params *params)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	unsigned int queue = (unsigned int)_queue;
	int err = -ENODEV;

	if (queue >= ARRAY_SIZE(wl->qos_params)) {
		/* Queue not available or don't support setting
		 * params on this queue. Return success to not
		 * confuse mac80211. */
		return 0;
	}
	BUILD_BUG_ON(ARRAY_SIZE(b43_qos_shm_offsets) !=
		     ARRAY_SIZE(wl->qos_params));

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (unlikely(!dev || (b43_status(dev) < B43_STAT_INITIALIZED)))
		goto out_unlock;

	memcpy(&(wl->qos_params[queue].p), params, sizeof(*params));
	b43_mac_suspend(dev);
	b43_qos_params_upload(dev, &(wl->qos_params[queue].p),
			      b43_qos_shm_offsets[queue]);
	b43_mac_enable(dev);
	err = 0;

out_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

static int b43_op_get_stats(struct ieee80211_hw *hw,
			    struct ieee80211_low_level_stats *stats)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);

	mutex_lock(&wl->mutex);
	memcpy(stats, &wl->ieee_stats, sizeof(*stats));
	mutex_unlock(&wl->mutex);

	return 0;
}

static u64 b43_op_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	u64 tsf;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;

	if (dev && (b43_status(dev) >= B43_STAT_INITIALIZED))
		b43_tsf_read(dev, &tsf);
	else
		tsf = 0;

	mutex_unlock(&wl->mutex);

	return tsf;
}

static void b43_op_set_tsf(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif, u64 tsf)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;

	if (dev && (b43_status(dev) >= B43_STAT_INITIALIZED))
		b43_tsf_write(dev, tsf);

	mutex_unlock(&wl->mutex);
}

static void b43_put_phy_into_reset(struct b43_wldev *dev)
{
	u32 tmp;

	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		b43err(dev->wl,
		       "Putting PHY into reset not supported on BCMA\n");
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~B43_TMSLOW_GMODE;
		tmp |= B43_TMSLOW_PHYRESET;
		tmp |= SSB_TMSLOW_FGC;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		msleep(1);

		tmp = ssb_read32(dev->dev->sdev, SSB_TMSLOW);
		tmp &= ~SSB_TMSLOW_FGC;
		tmp |= B43_TMSLOW_PHYRESET;
		ssb_write32(dev->dev->sdev, SSB_TMSLOW, tmp);
		msleep(1);

		break;
#endif
	}
}

static const char *band_to_string(enum ieee80211_band band)
{
	switch (band) {
	case IEEE80211_BAND_5GHZ:
		return "5";
	case IEEE80211_BAND_2GHZ:
		return "2.4";
	default:
		break;
	}
	B43_WARN_ON(1);
	return "";
}

/* Expects wl->mutex locked */
static int b43_switch_band(struct b43_wl *wl, struct ieee80211_channel *chan)
{
	struct b43_wldev *up_dev = NULL;
	struct b43_wldev *down_dev;
	struct b43_wldev *d;
	int err;
	bool uninitialized_var(gmode);
	int prev_status;

	/* Find a device and PHY which supports the band. */
	list_for_each_entry(d, &wl->devlist, list) {
		switch (chan->band) {
		case IEEE80211_BAND_5GHZ:
			if (d->phy.supports_5ghz) {
				up_dev = d;
				gmode = false;
			}
			break;
		case IEEE80211_BAND_2GHZ:
			if (d->phy.supports_2ghz) {
				up_dev = d;
				gmode = true;
			}
			break;
		default:
			B43_WARN_ON(1);
			return -EINVAL;
		}
		if (up_dev)
			break;
	}
	if (!up_dev) {
		b43err(wl, "Could not find a device for %s-GHz band operation\n",
		       band_to_string(chan->band));
		return -ENODEV;
	}
	if ((up_dev == wl->current_dev) &&
	    (!!wl->current_dev->phy.gmode == !!gmode)) {
		/* This device is already running. */
		return 0;
	}
	b43dbg(wl, "Switching to %s-GHz band\n",
	       band_to_string(chan->band));
	down_dev = wl->current_dev;

	prev_status = b43_status(down_dev);
	/* Shutdown the currently running core. */
	if (prev_status >= B43_STAT_STARTED)
		down_dev = b43_wireless_core_stop(down_dev);
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
			       "selected %s-GHz band\n",
			       band_to_string(chan->band));
			goto init_failure;
		}
	}
	if (prev_status >= B43_STAT_STARTED) {
		err = b43_wireless_core_start(up_dev);
		if (err) {
			b43err(wl, "Fatal: Could not start device for "
			       "selected %s-GHz band\n",
			       band_to_string(chan->band));
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

static int b43_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	struct b43_phy *phy;
	struct ieee80211_conf *conf = &hw->conf;
	int antenna;
	int err = 0;
	bool reload_bss = false;

	mutex_lock(&wl->mutex);

	dev = wl->current_dev;

	/* Switch the band (if necessary). This might change the active core. */
	err = b43_switch_band(wl, conf->channel);
	if (err)
		goto out_unlock_mutex;

	/* Need to reload all settings if the core changed */
	if (dev != wl->current_dev) {
		dev = wl->current_dev;
		changed = ~0;
		reload_bss = true;
	}

	phy = &dev->phy;

	if (conf_is_ht(conf))
		phy->is_40mhz =
			(conf_is_ht40_minus(conf) || conf_is_ht40_plus(conf));
	else
		phy->is_40mhz = false;

	b43_mac_suspend(dev);

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		b43_set_retry_limits(dev, conf->short_frame_max_tx_count,
					  conf->long_frame_max_tx_count);
	changed &= ~IEEE80211_CONF_CHANGE_RETRY_LIMITS;
	if (!changed)
		goto out_mac_enable;

	/* Switch to the requested channel.
	 * The firmware takes care of races with the TX handler. */
	if (conf->channel->hw_value != phy->channel)
		b43_switch_channel(dev, conf->channel->hw_value);

	dev->wl->radiotap_enabled = !!(conf->flags & IEEE80211_CONF_MONITOR);

	/* Adjust the desired TX power level. */
	if (conf->power_level != 0) {
		if (conf->power_level != phy->desired_txpower) {
			phy->desired_txpower = conf->power_level;
			b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
						   B43_TXPWR_IGNORE_TSSI);
		}
	}

	/* Antennas for RX and management frame TX. */
	antenna = B43_ANTENNA_DEFAULT;
	b43_mgmtframe_txantenna(dev, antenna);
	antenna = B43_ANTENNA_DEFAULT;
	if (phy->ops->set_rx_antenna)
		phy->ops->set_rx_antenna(dev, antenna);

	if (wl->radio_enabled != phy->radio_on) {
		if (wl->radio_enabled) {
			b43_software_rfkill(dev, false);
			b43info(dev->wl, "Radio turned on by software\n");
			if (!dev->radio_hw_enable) {
				b43info(dev->wl, "The hardware RF-kill button "
					"still turns the radio physically off. "
					"Press the button to turn it on.\n");
			}
		} else {
			b43_software_rfkill(dev, true);
			b43info(dev->wl, "Radio turned off by software\n");
		}
	}

out_mac_enable:
	b43_mac_enable(dev);
out_unlock_mutex:
	mutex_unlock(&wl->mutex);

	if (wl->vif && reload_bss)
		b43_op_bss_info_changed(hw, wl->vif, &wl->vif->bss_conf, ~0);

	return err;
}

static void b43_update_basic_rates(struct b43_wldev *dev, u32 brates)
{
	struct ieee80211_supported_band *sband =
		dev->wl->hw->wiphy->bands[b43_current_band(dev->wl)];
	struct ieee80211_rate *rate;
	int i;
	u16 basic, direct, offset, basic_offset, rateptr;

	for (i = 0; i < sband->n_bitrates; i++) {
		rate = &sband->bitrates[i];

		if (b43_is_cck_rate(rate->hw_value)) {
			direct = B43_SHM_SH_CCKDIRECT;
			basic = B43_SHM_SH_CCKBASIC;
			offset = b43_plcp_get_ratecode_cck(rate->hw_value);
			offset &= 0xF;
		} else {
			direct = B43_SHM_SH_OFDMDIRECT;
			basic = B43_SHM_SH_OFDMBASIC;
			offset = b43_plcp_get_ratecode_ofdm(rate->hw_value);
			offset &= 0xF;
		}

		rate = ieee80211_get_response_rate(sband, brates, rate->bitrate);

		if (b43_is_cck_rate(rate->hw_value)) {
			basic_offset = b43_plcp_get_ratecode_cck(rate->hw_value);
			basic_offset &= 0xF;
		} else {
			basic_offset = b43_plcp_get_ratecode_ofdm(rate->hw_value);
			basic_offset &= 0xF;
		}

		/*
		 * Get the pointer that we need to point to
		 * from the direct map
		 */
		rateptr = b43_shm_read16(dev, B43_SHM_SHARED,
					 direct + 2 * basic_offset);
		/* and write it to the basic map */
		b43_shm_write16(dev, B43_SHM_SHARED, basic + 2 * offset,
				rateptr);
	}
}

static void b43_op_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *conf,
				    u32 changed)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);

	dev = wl->current_dev;
	if (!dev || b43_status(dev) < B43_STAT_STARTED)
		goto out_unlock_mutex;

	B43_WARN_ON(wl->vif != vif);

	if (changed & BSS_CHANGED_BSSID) {
		if (conf->bssid)
			memcpy(wl->bssid, conf->bssid, ETH_ALEN);
		else
			memset(wl->bssid, 0, ETH_ALEN);
	}

	if (b43_status(dev) >= B43_STAT_INITIALIZED) {
		if (changed & BSS_CHANGED_BEACON &&
		    (b43_is_mode(wl, NL80211_IFTYPE_AP) ||
		     b43_is_mode(wl, NL80211_IFTYPE_MESH_POINT) ||
		     b43_is_mode(wl, NL80211_IFTYPE_ADHOC)))
			b43_update_templates(wl);

		if (changed & BSS_CHANGED_BSSID)
			b43_write_mac_bssid_templates(dev);
	}

	b43_mac_suspend(dev);

	/* Update templates for AP/mesh mode. */
	if (changed & BSS_CHANGED_BEACON_INT &&
	    (b43_is_mode(wl, NL80211_IFTYPE_AP) ||
	     b43_is_mode(wl, NL80211_IFTYPE_MESH_POINT) ||
	     b43_is_mode(wl, NL80211_IFTYPE_ADHOC)) &&
	    conf->beacon_int)
		b43_set_beacon_int(dev, conf->beacon_int);

	if (changed & BSS_CHANGED_BASIC_RATES)
		b43_update_basic_rates(dev, conf->basic_rates);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (conf->use_short_slot)
			b43_short_slot_timing_enable(dev);
		else
			b43_short_slot_timing_disable(dev);
	}

	b43_mac_enable(dev);
out_unlock_mutex:
	mutex_unlock(&wl->mutex);
}

static int b43_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	u8 algorithm;
	u8 index;
	int err;
	static const u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (modparam_nohwcrypt)
		return -ENOSPC; /* User disabled HW-crypto */

	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		/*
		 * For now, disable hw crypto for the RSN IBSS group keys. This
		 * could be optimized in the future, but until that gets
		 * implemented, use of software crypto for group addressed
		 * frames is a acceptable to allow RSN IBSS to be used.
		 */
		return -EOPNOTSUPP;
	}

	mutex_lock(&wl->mutex);

	dev = wl->current_dev;
	err = -ENODEV;
	if (!dev || b43_status(dev) < B43_STAT_INITIALIZED)
		goto out_unlock;

	if (dev->fw.pcm_request_failed || !dev->hwcrypto_enabled) {
		/* We don't have firmware for the crypto engine.
		 * Must use software-crypto. */
		err = -EOPNOTSUPP;
		goto out_unlock;
	}

	err = -EINVAL;
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		algorithm = B43_SEC_ALGO_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		algorithm = B43_SEC_ALGO_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		algorithm = B43_SEC_ALGO_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
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
		if (algorithm == B43_SEC_ALGO_TKIP &&
		    (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ||
		    !modparam_hwtkip)) {
			/* We support only pairwise key */
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			if (WARN_ON(!sta)) {
				err = -EOPNOTSUPP;
				goto out_unlock;
			}
			/* Pairwise key with an assigned MAC address. */
			err = b43_key_write(dev, -1, algorithm,
					    key->key, key->keylen,
					    sta->addr, key);
		} else {
			/* Group key */
			err = b43_key_write(dev, index, algorithm,
					    key->key, key->keylen, NULL, key);
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
		if (algorithm == B43_SEC_ALGO_TKIP)
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
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
	if (!err) {
		b43dbg(wl, "%s hardware based encryption for keyidx: %d, "
		       "mac: %pM\n",
		       cmd == SET_KEY ? "Using" : "Disabling", key->keyidx,
		       sta ? sta->addr : bcast_addr);
		b43_dump_keymemory(dev);
	}
	mutex_unlock(&wl->mutex);

	return err;
}

static void b43_op_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed, unsigned int *fflags,
				    u64 multicast)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (!dev) {
		*fflags = 0;
		goto out_unlock;
	}

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

out_unlock:
	mutex_unlock(&wl->mutex);
}

/* Locking: wl->mutex
 * Returns the current dev. This might be different from the passed in dev,
 * because the core might be gone away while we unlocked the mutex. */
static struct b43_wldev * b43_wireless_core_stop(struct b43_wldev *dev)
{
	struct b43_wl *wl;
	struct b43_wldev *orig_dev;
	u32 mask;
	int queue_num;

	if (!dev)
		return NULL;
	wl = dev->wl;
redo:
	if (!dev || b43_status(dev) < B43_STAT_STARTED)
		return dev;

	/* Cancel work. Unlock to avoid deadlocks. */
	mutex_unlock(&wl->mutex);
	cancel_delayed_work_sync(&dev->periodic_work);
	cancel_work_sync(&wl->tx_work);
	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (!dev || b43_status(dev) < B43_STAT_STARTED) {
		/* Whoops, aliens ate up the device while we were unlocked. */
		return dev;
	}

	/* Disable interrupts on the device. */
	b43_set_status(dev, B43_STAT_INITIALIZED);
	if (b43_bus_host_is_sdio(dev->dev)) {
		/* wl->mutex is locked. That is enough. */
		b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, 0);
		b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);	/* Flush */
	} else {
		spin_lock_irq(&wl->hardirq_lock);
		b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, 0);
		b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);	/* Flush */
		spin_unlock_irq(&wl->hardirq_lock);
	}
	/* Synchronize and free the interrupt handlers. Unlock to avoid deadlocks. */
	orig_dev = dev;
	mutex_unlock(&wl->mutex);
	if (b43_bus_host_is_sdio(dev->dev)) {
		b43_sdio_free_irq(dev);
	} else {
		synchronize_irq(dev->dev->irq);
		free_irq(dev->dev->irq, dev);
	}
	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (!dev)
		return dev;
	if (dev != orig_dev) {
		if (b43_status(dev) >= B43_STAT_STARTED)
			goto redo;
		return dev;
	}
	mask = b43_read32(dev, B43_MMIO_GEN_IRQ_MASK);
	B43_WARN_ON(mask != 0xFFFFFFFF && mask);

	/* Drain all TX queues. */
	for (queue_num = 0; queue_num < B43_QOS_QUEUE_NUM; queue_num++) {
		while (skb_queue_len(&wl->tx_queue[queue_num])) {
			struct sk_buff *skb;

			skb = skb_dequeue(&wl->tx_queue[queue_num]);
			ieee80211_free_txskb(wl->hw, skb);
		}
	}

	b43_mac_suspend(dev);
	b43_leds_exit(dev);
	b43dbg(wl, "Wireless interface stopped\n");

	return dev;
}

/* Locking: wl->mutex */
static int b43_wireless_core_start(struct b43_wldev *dev)
{
	int err;

	B43_WARN_ON(b43_status(dev) != B43_STAT_INITIALIZED);

	drain_txstatus_queue(dev);
	if (b43_bus_host_is_sdio(dev->dev)) {
		err = b43_sdio_request_irq(dev, b43_sdio_interrupt_handler);
		if (err) {
			b43err(dev->wl, "Cannot request SDIO IRQ\n");
			goto out;
		}
	} else {
		err = request_threaded_irq(dev->dev->irq, b43_interrupt_handler,
					   b43_interrupt_thread_handler,
					   IRQF_SHARED, KBUILD_MODNAME, dev);
		if (err) {
			b43err(dev->wl, "Cannot request IRQ-%d\n",
			       dev->dev->irq);
			goto out;
		}
	}

	/* We are ready to run. */
	ieee80211_wake_queues(dev->wl->hw);
	b43_set_status(dev, B43_STAT_STARTED);

	/* Start data flow (TX/RX). */
	b43_mac_enable(dev);
	b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, dev->irq_mask);

	/* Start maintenance work */
	b43_periodic_tasks_setup(dev);

	b43_leds_init(dev);

	b43dbg(dev->wl, "Wireless interface started\n");
out:
	return err;
}

static char *b43_phy_name(struct b43_wldev *dev, u8 phy_type)
{
	switch (phy_type) {
	case B43_PHYTYPE_A:
		return "A";
	case B43_PHYTYPE_B:
		return "B";
	case B43_PHYTYPE_G:
		return "G";
	case B43_PHYTYPE_N:
		return "N";
	case B43_PHYTYPE_LP:
		return "LP";
	case B43_PHYTYPE_SSLPN:
		return "SSLPN";
	case B43_PHYTYPE_HT:
		return "HT";
	case B43_PHYTYPE_LCN:
		return "LCN";
	case B43_PHYTYPE_LCNXN:
		return "LCNXN";
	case B43_PHYTYPE_LCN40:
		return "LCN40";
	case B43_PHYTYPE_AC:
		return "AC";
	}
	return "UNKNOWN";
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
#ifdef CONFIG_B43_PHY_N
	case B43_PHYTYPE_N:
		if (phy_rev > 9)
			unsupported = 1;
		break;
#endif
#ifdef CONFIG_B43_PHY_LP
	case B43_PHYTYPE_LP:
		if (phy_rev > 2)
			unsupported = 1;
		break;
#endif
#ifdef CONFIG_B43_PHY_HT
	case B43_PHYTYPE_HT:
		if (phy_rev > 1)
			unsupported = 1;
		break;
#endif
#ifdef CONFIG_B43_PHY_LCN
	case B43_PHYTYPE_LCN:
		if (phy_rev > 1)
			unsupported = 1;
		break;
#endif
	default:
		unsupported = 1;
	}
	if (unsupported) {
		b43err(dev->wl, "FOUND UNSUPPORTED PHY (Analog %u, Type %d (%s), Revision %u)\n",
		       analog_type, phy_type, b43_phy_name(dev, phy_type),
		       phy_rev);
		return -EOPNOTSUPP;
	}
	b43info(dev->wl, "Found PHY: Analog %u, Type %d (%s), Revision %u\n",
		analog_type, phy_type, b43_phy_name(dev, phy_type), phy_rev);

	/* Get RADIO versioning */
	if (dev->dev->core_rev >= 24) {
		u16 radio24[3];

		for (tmp = 0; tmp < 3; tmp++) {
			b43_write16(dev, B43_MMIO_RADIO24_CONTROL, tmp);
			radio24[tmp] = b43_read16(dev, B43_MMIO_RADIO24_DATA);
		}

		/* Broadcom uses "id" for our "ver" and has separated "ver" */
		/* radio_ver = (radio24[0] & 0xF0) >> 4; */

		radio_manuf = 0x17F;
		radio_ver = (radio24[2] << 8) | radio24[1];
		radio_rev = (radio24[0] & 0xF);
	} else {
		if (dev->dev->chip_id == 0x4317) {
			if (dev->dev->chip_rev == 0)
				tmp = 0x3205017F;
			else if (dev->dev->chip_rev == 1)
				tmp = 0x4205017F;
			else
				tmp = 0x5205017F;
		} else {
			b43_write16(dev, B43_MMIO_RADIO_CONTROL,
				    B43_RADIOCTL_ID);
			tmp = b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
			b43_write16(dev, B43_MMIO_RADIO_CONTROL,
				    B43_RADIOCTL_ID);
			tmp |= (u32)b43_read16(dev, B43_MMIO_RADIO_DATA_HIGH)
				<< 16;
		}
		radio_manuf = (tmp & 0x00000FFF);
		radio_ver = (tmp & 0x0FFFF000) >> 12;
		radio_rev = (tmp & 0xF0000000) >> 28;
	}

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
		if (radio_ver != 0x2055 && radio_ver != 0x2056)
			unsupported = 1;
		break;
	case B43_PHYTYPE_LP:
		if (radio_ver != 0x2062 && radio_ver != 0x2063)
			unsupported = 1;
		break;
	case B43_PHYTYPE_HT:
		if (radio_ver != 0x2059)
			unsupported = 1;
		break;
	case B43_PHYTYPE_LCN:
		if (radio_ver != 0x2064)
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
	phy->hardware_power_control = !!modparam_hwpctl;
	phy->next_txpwr_check_time = jiffies;
	/* PHY TX errors counter. */
	atomic_set(&phy->txerr_cnt, B43_PHY_TX_BADNESS_LIMIT);

#if B43_DEBUG
	phy->phy_locked = false;
	phy->radio_locked = false;
#endif
}

static void setup_struct_wldev_for_init(struct b43_wldev *dev)
{
	dev->dfq_valid = false;

	/* Assume the radio is enabled. If it's not enabled, the state will
	 * immediately get fixed on the first periodic work run. */
	dev->radio_hw_enable = true;

	/* Stats */
	memset(&dev->stats, 0, sizeof(dev->stats));

	setup_struct_phy_for_init(dev, &dev->phy);

	/* IRQ related flags */
	dev->irq_reason = 0;
	memset(dev->dma_reason, 0, sizeof(dev->dma_reason));
	dev->irq_mask = B43_IRQ_MASKTEMPLATE;
	if (b43_modparam_verbose < B43_VERBOSITY_DEBUG)
		dev->irq_mask &= ~B43_IRQ_PHY_TXERR;

	dev->mac_suspended = 1;

	/* Noise calculation context */
	memset(&dev->noisecalc, 0, sizeof(dev->noisecalc));
}

static void b43_bluetooth_coext_enable(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	u64 hf;

	if (!modparam_btcoex)
		return;
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
}

static void b43_bluetooth_coext_disable(struct b43_wldev *dev)
{
	if (!modparam_btcoex)
		return;
	//TODO
}

static void b43_imcfglo_timeouts_workaround(struct b43_wldev *dev)
{
	struct ssb_bus *bus;
	u32 tmp;

	if (dev->dev->bus_type != B43_BUS_SSB)
		return;

	bus = dev->dev->sdev->bus;

	if ((bus->chip_id == 0x4311 && bus->chip_rev == 2) ||
	    (bus->chip_id == 0x4312)) {
		tmp = ssb_read32(dev->dev->sdev, SSB_IMCFGLO);
		tmp &= ~SSB_IMCFGLO_REQTO;
		tmp &= ~SSB_IMCFGLO_SERTO;
		tmp |= 0x3;
		ssb_write32(dev->dev->sdev, SSB_IMCFGLO, tmp);
		ssb_commit_settings(bus);
	}
}

static void b43_set_synth_pu_delay(struct b43_wldev *dev, bool idle)
{
	u16 pu_delay;

	/* The time value is in microseconds. */
	if (dev->phy.type == B43_PHYTYPE_A)
		pu_delay = 3700;
	else
		pu_delay = 1050;
	if (b43_is_mode(dev->wl, NL80211_IFTYPE_ADHOC) || idle)
		pu_delay = 500;
	if ((dev->phy.radio_ver == 0x2050) && (dev->phy.radio_rev == 8))
		pu_delay = max(pu_delay, (u16)2400);

	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_SPUWKUP, pu_delay);
}

/* Set the TSF CFP pre-TargetBeaconTransmissionTime. */
static void b43_set_pretbtt(struct b43_wldev *dev)
{
	u16 pretbtt;

	/* The time value is in microseconds. */
	if (b43_is_mode(dev->wl, NL80211_IFTYPE_ADHOC)) {
		pretbtt = 2;
	} else {
		if (dev->phy.type == B43_PHYTYPE_A)
			pretbtt = 120;
		else
			pretbtt = 250;
	}
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_PRETBTT, pretbtt);
	b43_write16(dev, B43_MMIO_TSF_CFP_PRETBTT, pretbtt);
}

/* Shutdown a wireless core */
/* Locking: wl->mutex */
static void b43_wireless_core_exit(struct b43_wldev *dev)
{
	B43_WARN_ON(dev && b43_status(dev) > B43_STAT_INITIALIZED);
	if (!dev || b43_status(dev) != B43_STAT_INITIALIZED)
		return;

	/* Unregister HW RNG driver */
	b43_rng_exit(dev->wl);

	b43_set_status(dev, B43_STAT_UNINIT);

	/* Stop the microcode PSM. */
	b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_PSM_RUN,
		      B43_MACCTL_PSM_JMP0);

	b43_dma_free(dev);
	b43_pio_free(dev);
	b43_chip_exit(dev);
	dev->phy.ops->switch_analog(dev, 0);
	if (dev->wl->current_beacon) {
		dev_kfree_skb_any(dev->wl->current_beacon);
		dev->wl->current_beacon = NULL;
	}

	b43_device_disable(dev, 0);
	b43_bus_may_powerdown(dev);
}

/* Initialize a wireless core */
static int b43_wireless_core_init(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	struct b43_phy *phy = &dev->phy;
	int err;
	u64 hf;

	B43_WARN_ON(b43_status(dev) != B43_STAT_UNINIT);

	err = b43_bus_powerup(dev, 0);
	if (err)
		goto out;
	if (!b43_device_is_enabled(dev))
		b43_wireless_core_reset(dev, phy->gmode);

	/* Reset all data structures. */
	setup_struct_wldev_for_init(dev);
	phy->ops->prepare_structs(dev);

	/* Enable IRQ routing to this device. */
	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		bcma_core_pci_irq_ctl(&dev->dev->bdev->bus->drv_pci[0],
				      dev->dev->bdev, true);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		ssb_pcicore_dev_irqvecs_enable(&dev->dev->sdev->bus->pcicore,
					       dev->dev->sdev);
		break;
#endif
	}

	b43_imcfglo_timeouts_workaround(dev);
	b43_bluetooth_coext_disable(dev);
	if (phy->ops->prepare_hardware) {
		err = phy->ops->prepare_hardware(dev);
		if (err)
			goto err_busdown;
	}
	err = b43_chip_init(dev);
	if (err)
		goto err_busdown;
	b43_shm_write16(dev, B43_SHM_SHARED,
			B43_SHM_SH_WLCOREREV, dev->dev->core_rev);
	hf = b43_hf_read(dev);
	if (phy->type == B43_PHYTYPE_G) {
		hf |= B43_HF_SYMW;
		if (phy->rev == 1)
			hf |= B43_HF_GDCW;
		if (sprom->boardflags_lo & B43_BFL_PACTRL)
			hf |= B43_HF_OFDMPABOOST;
	}
	if (phy->radio_ver == 0x2050) {
		if (phy->radio_rev == 6)
			hf |= B43_HF_4318TSSI;
		if (phy->radio_rev < 6)
			hf |= B43_HF_VCORECALC;
	}
	if (sprom->boardflags_lo & B43_BFL_XTAL_NOSLOW)
		hf |= B43_HF_DSCRQ; /* Disable slowclock requests from ucode. */
#ifdef CONFIG_SSB_DRIVER_PCICORE
	if (dev->dev->bus_type == B43_BUS_SSB &&
	    dev->dev->sdev->bus->bustype == SSB_BUSTYPE_PCI &&
	    dev->dev->sdev->bus->pcicore.dev->id.revision <= 10)
		hf |= B43_HF_PCISCW; /* PCI slow clock workaround. */
#endif
	hf &= ~B43_HF_SKCFPUP;
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
	b43_set_phytxctl_defaults(dev);

	/* Minimum Contention Window */
	if (phy->type == B43_PHYTYPE_B)
		b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MINCONT, 0x1F);
	else
		b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MINCONT, 0xF);
	/* Maximum Contention Window */
	b43_shm_write16(dev, B43_SHM_SCRATCH, B43_SHM_SC_MAXCONT, 0x3FF);

	if (b43_bus_host_is_pcmcia(dev->dev) ||
	    b43_bus_host_is_sdio(dev->dev)) {
		dev->__using_pio_transfers = true;
		err = b43_pio_init(dev);
	} else if (dev->use_pio) {
		b43warn(dev->wl, "Forced PIO by use_pio module parameter. "
			"This should not be needed and will result in lower "
			"performance.\n");
		dev->__using_pio_transfers = true;
		err = b43_pio_init(dev);
	} else {
		dev->__using_pio_transfers = false;
		err = b43_dma_init(dev);
	}
	if (err)
		goto err_chip_exit;
	b43_qos_init(dev);
	b43_set_synth_pu_delay(dev, 1);
	b43_bluetooth_coext_enable(dev);

	b43_bus_powerup(dev, !(sprom->boardflags_lo & B43_BFL_XTAL_NOSLOW));
	b43_upload_card_macaddress(dev);
	b43_security_init(dev);

	ieee80211_wake_queues(dev->wl->hw);

	b43_set_status(dev, B43_STAT_INITIALIZED);

	/* Register HW RNG driver */
	b43_rng_init(dev->wl);

out:
	return err;

err_chip_exit:
	b43_chip_exit(dev);
err_busdown:
	b43_bus_may_powerdown(dev);
	B43_WARN_ON(b43_status(dev) != B43_STAT_UNINIT);
	return err;
}

static int b43_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;
	int err = -EOPNOTSUPP;

	/* TODO: allow WDS/AP devices to coexist */

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_WDS &&
	    vif->type != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	mutex_lock(&wl->mutex);
	if (wl->operating)
		goto out_mutex_unlock;

	b43dbg(wl, "Adding Interface type %d\n", vif->type);

	dev = wl->current_dev;
	wl->operating = true;
	wl->vif = vif;
	wl->if_type = vif->type;
	memcpy(wl->mac_addr, vif->addr, ETH_ALEN);

	b43_adjust_opmode(dev);
	b43_set_pretbtt(dev);
	b43_set_synth_pu_delay(dev, 0);
	b43_upload_card_macaddress(dev);

	err = 0;
 out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	if (err == 0)
		b43_op_bss_info_changed(hw, vif, &vif->bss_conf, ~0);

	return err;
}

static void b43_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;

	b43dbg(wl, "Removing Interface type %d\n", vif->type);

	mutex_lock(&wl->mutex);

	B43_WARN_ON(!wl->operating);
	B43_WARN_ON(wl->vif != vif);
	wl->vif = NULL;

	wl->operating = false;

	b43_adjust_opmode(dev);
	memset(wl->mac_addr, 0, ETH_ALEN);
	b43_upload_card_macaddress(dev);

	mutex_unlock(&wl->mutex);
}

static int b43_op_start(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	int did_init = 0;
	int err = 0;

	/* Kill all old instance specific information to make sure
	 * the card won't use it in the short timeframe between start
	 * and mac80211 reconfiguring it. */
	memset(wl->bssid, 0, ETH_ALEN);
	memset(wl->mac_addr, 0, ETH_ALEN);
	wl->filter_flags = 0;
	wl->radiotap_enabled = false;
	b43_qos_clear(wl);
	wl->beacon0_uploaded = false;
	wl->beacon1_uploaded = false;
	wl->beacon_templates_virgin = true;
	wl->radio_enabled = true;

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

	/* XXX: only do if device doesn't support rfkill irq */
	wiphy_rfkill_start_polling(hw->wiphy);

 out_mutex_unlock:
	mutex_unlock(&wl->mutex);

	/*
	 * Configuration may have been overwritten during initialization.
	 * Reload the configuration, but only if initialization was
	 * successful. Reloading the configuration after a failed init
	 * may hang the system.
	 */
	if (!err)
		b43_op_config(hw, ~0);

	return err;
}

static void b43_op_stop(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;

	cancel_work_sync(&(wl->beacon_update_trigger));

	if (!dev)
		goto out;

	mutex_lock(&wl->mutex);
	if (b43_status(dev) >= B43_STAT_STARTED) {
		dev = b43_wireless_core_stop(dev);
		if (!dev)
			goto out_unlock;
	}
	b43_wireless_core_exit(dev);
	wl->radio_enabled = false;

out_unlock:
	mutex_unlock(&wl->mutex);
out:
	cancel_work_sync(&(wl->txpower_adjust_work));
}

static int b43_op_beacon_set_tim(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta, bool set)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);

	/* FIXME: add locking */
	b43_update_templates(wl);

	return 0;
}

static void b43_op_sta_notify(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      enum sta_notify_cmd notify_cmd,
			      struct ieee80211_sta *sta)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);

	B43_WARN_ON(!vif || wl->vif != vif);
}

static void b43_op_sw_scan_start_notifier(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (dev && (b43_status(dev) >= B43_STAT_INITIALIZED)) {
		/* Disable CFP update during scan on other channels. */
		b43_hf_write(dev, b43_hf_read(dev) | B43_HF_SKCFPUP);
	}
	mutex_unlock(&wl->mutex);
}

static void b43_op_sw_scan_complete_notifier(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (dev && (b43_status(dev) >= B43_STAT_INITIALIZED)) {
		/* Re-enable CFP update. */
		b43_hf_write(dev, b43_hf_read(dev) & ~B43_HF_SKCFPUP);
	}
	mutex_unlock(&wl->mutex);
}

static int b43_op_get_survey(struct ieee80211_hw *hw, int idx,
			     struct survey_info *survey)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	struct ieee80211_conf *conf = &hw->conf;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->channel;
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = dev->stats.link_noise;

	return 0;
}

static const struct ieee80211_ops b43_hw_ops = {
	.tx			= b43_op_tx,
	.conf_tx		= b43_op_conf_tx,
	.add_interface		= b43_op_add_interface,
	.remove_interface	= b43_op_remove_interface,
	.config			= b43_op_config,
	.bss_info_changed	= b43_op_bss_info_changed,
	.configure_filter	= b43_op_configure_filter,
	.set_key		= b43_op_set_key,
	.update_tkip_key	= b43_op_update_tkip_key,
	.get_stats		= b43_op_get_stats,
	.get_tsf		= b43_op_get_tsf,
	.set_tsf		= b43_op_set_tsf,
	.start			= b43_op_start,
	.stop			= b43_op_stop,
	.set_tim		= b43_op_beacon_set_tim,
	.sta_notify		= b43_op_sta_notify,
	.sw_scan_start		= b43_op_sw_scan_start_notifier,
	.sw_scan_complete	= b43_op_sw_scan_complete_notifier,
	.get_survey		= b43_op_get_survey,
	.rfkill_poll		= b43_rfkill_poll,
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
	if (prev_status >= B43_STAT_STARTED) {
		dev = b43_wireless_core_stop(dev);
		if (!dev) {
			err = -ENODEV;
			goto out;
		}
	}
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
	if (err)
		wl->current_dev = NULL; /* Failed to init the dev. */
	mutex_unlock(&wl->mutex);

	if (err) {
		b43err(wl, "Controller restart FAILED\n");
		return;
	}

	/* reload configuration */
	b43_op_config(wl->hw, ~0);
	if (wl->vif)
		b43_op_bss_info_changed(wl->hw, wl->vif, &wl->vif->bss_conf, ~0);

	b43info(wl, "Controller restarted\n");
}

static int b43_setup_bands(struct b43_wldev *dev,
			   bool have_2ghz_phy, bool have_5ghz_phy)
{
	struct ieee80211_hw *hw = dev->wl->hw;

	if (have_2ghz_phy)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &b43_band_2GHz;
	if (dev->phy.type == B43_PHYTYPE_N) {
		if (have_5ghz_phy)
			hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &b43_band_5GHz_nphy;
	} else {
		if (have_5ghz_phy)
			hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &b43_band_5GHz_aphy;
	}

	dev->phy.supports_2ghz = have_2ghz_phy;
	dev->phy.supports_5ghz = have_5ghz_phy;

	return 0;
}

static void b43_wireless_core_detach(struct b43_wldev *dev)
{
	/* We release firmware that late to not be required to re-request
	 * is all the time when we reinit the core. */
	b43_release_firmware(dev);
	b43_phy_free(dev);
}

static int b43_wireless_core_attach(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct pci_dev *pdev = NULL;
	int err;
	u32 tmp;
	bool have_2ghz_phy = false, have_5ghz_phy = false;

	/* Do NOT do any device initialization here.
	 * Do it in wireless_core_init() instead.
	 * This function is for gathering basic information about the HW, only.
	 * Also some structs may be set up here. But most likely you want to have
	 * that in core_init(), too.
	 */

#ifdef CONFIG_B43_SSB
	if (dev->dev->bus_type == B43_BUS_SSB &&
	    dev->dev->sdev->bus->bustype == SSB_BUSTYPE_PCI)
		pdev = dev->dev->sdev->bus->host_pci;
#endif

	err = b43_bus_powerup(dev, 0);
	if (err) {
		b43err(wl, "Bus powerup failed\n");
		goto out;
	}

	/* Get the PHY type. */
	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		tmp = bcma_aread32(dev->dev->bdev, BCMA_IOST);
		have_2ghz_phy = !!(tmp & B43_BCMA_IOST_2G_PHY);
		have_5ghz_phy = !!(tmp & B43_BCMA_IOST_5G_PHY);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		if (dev->dev->core_rev >= 5) {
			tmp = ssb_read32(dev->dev->sdev, SSB_TMSHIGH);
			have_2ghz_phy = !!(tmp & B43_TMSHIGH_HAVE_2GHZ_PHY);
			have_5ghz_phy = !!(tmp & B43_TMSHIGH_HAVE_5GHZ_PHY);
		} else
			B43_WARN_ON(1);
		break;
#endif
	}

	dev->phy.gmode = have_2ghz_phy;
	dev->phy.radio_on = true;
	b43_wireless_core_reset(dev, dev->phy.gmode);

	err = b43_phy_versioning(dev);
	if (err)
		goto err_powerdown;
	/* Check if this device supports multiband. */
	if (!pdev ||
	    (pdev->device != 0x4312 &&
	     pdev->device != 0x4319 && pdev->device != 0x4324)) {
		/* No multiband support. */
		have_2ghz_phy = false;
		have_5ghz_phy = false;
		switch (dev->phy.type) {
		case B43_PHYTYPE_A:
			have_5ghz_phy = true;
			break;
		case B43_PHYTYPE_LP: //FIXME not always!
#if 0 //FIXME enabling 5GHz causes a NULL pointer dereference
			have_5ghz_phy = 1;
#endif
		case B43_PHYTYPE_G:
		case B43_PHYTYPE_N:
		case B43_PHYTYPE_HT:
		case B43_PHYTYPE_LCN:
			have_2ghz_phy = true;
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
	if (1 /* disable A-PHY */) {
		/* FIXME: For now we disable the A-PHY on multi-PHY devices. */
		if (dev->phy.type != B43_PHYTYPE_N &&
		    dev->phy.type != B43_PHYTYPE_LP) {
			have_2ghz_phy = true;
			have_5ghz_phy = false;
		}
	}

	err = b43_phy_allocate(dev);
	if (err)
		goto err_powerdown;

	dev->phy.gmode = have_2ghz_phy;
	b43_wireless_core_reset(dev, dev->phy.gmode);

	err = b43_validate_chipaccess(dev);
	if (err)
		goto err_phy_free;
	err = b43_setup_bands(dev, have_2ghz_phy, have_5ghz_phy);
	if (err)
		goto err_phy_free;

	/* Now set some default "current_dev" */
	if (!wl->current_dev)
		wl->current_dev = dev;
	INIT_WORK(&dev->restart_work, b43_chip_reset);

	dev->phy.ops->switch_analog(dev, 0);
	b43_device_disable(dev, 0);
	b43_bus_may_powerdown(dev);

out:
	return err;

err_phy_free:
	b43_phy_free(dev);
err_powerdown:
	b43_bus_may_powerdown(dev);
	return err;
}

static void b43_one_core_detach(struct b43_bus_dev *dev)
{
	struct b43_wldev *wldev;
	struct b43_wl *wl;

	/* Do not cancel ieee80211-workqueue based work here.
	 * See comment in b43_remove(). */

	wldev = b43_bus_get_wldev(dev);
	wl = wldev->wl;
	b43_debugfs_remove_device(wldev);
	b43_wireless_core_detach(wldev);
	list_del(&wldev->list);
	wl->nr_devs--;
	b43_bus_set_wldev(dev, NULL);
	kfree(wldev);
}

static int b43_one_core_attach(struct b43_bus_dev *dev, struct b43_wl *wl)
{
	struct b43_wldev *wldev;
	int err = -ENOMEM;

	wldev = kzalloc(sizeof(*wldev), GFP_KERNEL);
	if (!wldev)
		goto out;

	wldev->use_pio = b43_modparam_pio;
	wldev->dev = dev;
	wldev->wl = wl;
	b43_set_status(wldev, B43_STAT_UNINIT);
	wldev->bad_frames_preempt = modparam_bad_frames_preempt;
	INIT_LIST_HEAD(&wldev->list);

	err = b43_wireless_core_attach(wldev);
	if (err)
		goto err_kfree_wldev;

	list_add(&wldev->list, &wl->devlist);
	wl->nr_devs++;
	b43_bus_set_wldev(dev, wldev);
	b43_debugfs_add_device(wldev);

      out:
	return err;

      err_kfree_wldev:
	kfree(wldev);
	return err;
}

#define IS_PDEV(pdev, _vendor, _device, _subvendor, _subdevice)		( \
	(pdev->vendor == PCI_VENDOR_ID_##_vendor) &&			\
	(pdev->device == _device) &&					\
	(pdev->subsystem_vendor == PCI_VENDOR_ID_##_subvendor) &&	\
	(pdev->subsystem_device == _subdevice)				)

static void b43_sprom_fixup(struct ssb_bus *bus)
{
	struct pci_dev *pdev;

	/* boardflags workarounds */
	if (bus->boardinfo.vendor == SSB_BOARDVENDOR_DELL &&
	    bus->chip_id == 0x4301 && bus->sprom.board_rev == 0x74)
		bus->sprom.boardflags_lo |= B43_BFL_BTCOEXIST;
	if (bus->boardinfo.vendor == PCI_VENDOR_ID_APPLE &&
	    bus->boardinfo.type == 0x4E && bus->sprom.board_rev > 0x40)
		bus->sprom.boardflags_lo |= B43_BFL_PACTRL;
	if (bus->bustype == SSB_BUSTYPE_PCI) {
		pdev = bus->host_pci;
		if (IS_PDEV(pdev, BROADCOM, 0x4318, ASUSTEK, 0x100F) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320,    DELL, 0x0003) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320,      HP, 0x12f8) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320, LINKSYS, 0x0015) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320, LINKSYS, 0x0014) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320, LINKSYS, 0x0013) ||
		    IS_PDEV(pdev, BROADCOM, 0x4320, MOTOROLA, 0x7010))
			bus->sprom.boardflags_lo &= ~B43_BFL_BTCOEXIST;
	}
}

static void b43_wireless_exit(struct b43_bus_dev *dev, struct b43_wl *wl)
{
	struct ieee80211_hw *hw = wl->hw;

	ssb_set_devtypedata(dev->sdev, NULL);
	ieee80211_free_hw(hw);
}

static struct b43_wl *b43_wireless_init(struct b43_bus_dev *dev)
{
	struct ssb_sprom *sprom = dev->bus_sprom;
	struct ieee80211_hw *hw;
	struct b43_wl *wl;
	char chip_name[6];
	int queue_num;

	hw = ieee80211_alloc_hw(sizeof(*wl), &b43_hw_ops);
	if (!hw) {
		b43err(NULL, "Could not allocate ieee80211 device\n");
		return ERR_PTR(-ENOMEM);
	}
	wl = hw_to_b43_wl(hw);

	/* fill hw info */
	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		    IEEE80211_HW_SIGNAL_DBM;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_MESH_POINT) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_WDS) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	wl->hw_registred = false;
	hw->max_rates = 2;
	SET_IEEE80211_DEV(hw, dev->dev);
	if (is_valid_ether_addr(sprom->et1mac))
		SET_IEEE80211_PERM_ADDR(hw, sprom->et1mac);
	else
		SET_IEEE80211_PERM_ADDR(hw, sprom->il0mac);

	/* Initialize struct b43_wl */
	wl->hw = hw;
	mutex_init(&wl->mutex);
	spin_lock_init(&wl->hardirq_lock);
	INIT_LIST_HEAD(&wl->devlist);
	INIT_WORK(&wl->beacon_update_trigger, b43_beacon_update_trigger_work);
	INIT_WORK(&wl->txpower_adjust_work, b43_phy_txpower_adjust_work);
	INIT_WORK(&wl->tx_work, b43_tx_work);

	/* Initialize queues and flags. */
	for (queue_num = 0; queue_num < B43_QOS_QUEUE_NUM; queue_num++) {
		skb_queue_head_init(&wl->tx_queue[queue_num]);
		wl->tx_queue_stopped[queue_num] = 0;
	}

	snprintf(chip_name, ARRAY_SIZE(chip_name),
		 (dev->chip_id > 0x9999) ? "%d" : "%04X", dev->chip_id);
	b43info(wl, "Broadcom %s WLAN found (core revision %u)\n", chip_name,
		dev->core_rev);
	return wl;
}

#ifdef CONFIG_B43_BCMA
static int b43_bcma_probe(struct bcma_device *core)
{
	struct b43_bus_dev *dev;
	struct b43_wl *wl;
	int err;

	dev = b43_bus_dev_bcma_init(core);
	if (!dev)
		return -ENODEV;

	wl = b43_wireless_init(dev);
	if (IS_ERR(wl)) {
		err = PTR_ERR(wl);
		goto bcma_out;
	}

	err = b43_one_core_attach(dev, wl);
	if (err)
		goto bcma_err_wireless_exit;

	/* setup and start work to load firmware */
	INIT_WORK(&wl->firmware_load, b43_request_firmware);
	schedule_work(&wl->firmware_load);

bcma_out:
	return err;

bcma_err_wireless_exit:
	ieee80211_free_hw(wl->hw);
	return err;
}

static void b43_bcma_remove(struct bcma_device *core)
{
	struct b43_wldev *wldev = bcma_get_drvdata(core);
	struct b43_wl *wl = wldev->wl;

	/* We must cancel any work here before unregistering from ieee80211,
	 * as the ieee80211 unreg will destroy the workqueue. */
	cancel_work_sync(&wldev->restart_work);
	cancel_work_sync(&wl->firmware_load);

	B43_WARN_ON(!wl);
	if (!wldev->fw.ucode.data)
		return;			/* NULL if firmware never loaded */
	if (wl->current_dev == wldev && wl->hw_registred) {
		b43_leds_stop(wldev);
		ieee80211_unregister_hw(wl->hw);
	}

	b43_one_core_detach(wldev->dev);

	b43_leds_unregister(wl);

	ieee80211_free_hw(wl->hw);
}

static struct bcma_driver b43_bcma_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= b43_bcma_tbl,
	.probe		= b43_bcma_probe,
	.remove		= b43_bcma_remove,
};
#endif

#ifdef CONFIG_B43_SSB
static
int b43_ssb_probe(struct ssb_device *sdev, const struct ssb_device_id *id)
{
	struct b43_bus_dev *dev;
	struct b43_wl *wl;
	int err;
	int first = 0;

	dev = b43_bus_dev_ssb_init(sdev);
	if (!dev)
		return -ENOMEM;

	wl = ssb_get_devtypedata(sdev);
	if (!wl) {
		/* Probing the first core. Must setup common struct b43_wl */
		first = 1;
		b43_sprom_fixup(sdev->bus);
		wl = b43_wireless_init(dev);
		if (IS_ERR(wl)) {
			err = PTR_ERR(wl);
			goto out;
		}
		ssb_set_devtypedata(sdev, wl);
		B43_WARN_ON(ssb_get_devtypedata(sdev) != wl);
	}
	err = b43_one_core_attach(dev, wl);
	if (err)
		goto err_wireless_exit;

	/* setup and start work to load firmware */
	INIT_WORK(&wl->firmware_load, b43_request_firmware);
	schedule_work(&wl->firmware_load);

      out:
	return err;

      err_wireless_exit:
	if (first)
		b43_wireless_exit(dev, wl);
	return err;
}

static void b43_ssb_remove(struct ssb_device *sdev)
{
	struct b43_wl *wl = ssb_get_devtypedata(sdev);
	struct b43_wldev *wldev = ssb_get_drvdata(sdev);
	struct b43_bus_dev *dev = wldev->dev;

	/* We must cancel any work here before unregistering from ieee80211,
	 * as the ieee80211 unreg will destroy the workqueue. */
	cancel_work_sync(&wldev->restart_work);
	cancel_work_sync(&wl->firmware_load);

	B43_WARN_ON(!wl);
	if (!wldev->fw.ucode.data)
		return;			/* NULL if firmware never loaded */
	if (wl->current_dev == wldev && wl->hw_registred) {
		b43_leds_stop(wldev);
		ieee80211_unregister_hw(wl->hw);
	}

	b43_one_core_detach(dev);

	if (list_empty(&wl->devlist)) {
		b43_leds_unregister(wl);
		/* Last core on the chip unregistered.
		 * We can destroy common struct b43_wl.
		 */
		b43_wireless_exit(dev, wl);
	}
}

static struct ssb_driver b43_ssb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= b43_ssb_tbl,
	.probe		= b43_ssb_probe,
	.remove		= b43_ssb_remove,
};
#endif /* CONFIG_B43_SSB */

/* Perform a hardware reset. This can be called from any context. */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	/* Must avoid requeueing, if we are in shutdown. */
	if (b43_status(dev) < B43_STAT_INITIALIZED)
		return;
	b43info(dev->wl, "Controller RESET (%s) ...\n", reason);
	ieee80211_queue_work(dev->wl->hw, &dev->restart_work);
}

static void b43_print_driverinfo(void)
{
	const char *feat_pci = "", *feat_pcmcia = "", *feat_nphy = "",
		   *feat_leds = "", *feat_sdio = "";

#ifdef CONFIG_B43_PCI_AUTOSELECT
	feat_pci = "P";
#endif
#ifdef CONFIG_B43_PCMCIA
	feat_pcmcia = "M";
#endif
#ifdef CONFIG_B43_PHY_N
	feat_nphy = "N";
#endif
#ifdef CONFIG_B43_LEDS
	feat_leds = "L";
#endif
#ifdef CONFIG_B43_SDIO
	feat_sdio = "S";
#endif
	printk(KERN_INFO "Broadcom 43xx driver loaded "
	       "[ Features: %s%s%s%s%s ]\n",
	       feat_pci, feat_pcmcia, feat_nphy,
	       feat_leds, feat_sdio);
}

static int __init b43_init(void)
{
	int err;

	b43_debugfs_init();
	err = b43_pcmcia_init();
	if (err)
		goto err_dfs_exit;
	err = b43_sdio_init();
	if (err)
		goto err_pcmcia_exit;
#ifdef CONFIG_B43_BCMA
	err = bcma_driver_register(&b43_bcma_driver);
	if (err)
		goto err_sdio_exit;
#endif
#ifdef CONFIG_B43_SSB
	err = ssb_driver_register(&b43_ssb_driver);
	if (err)
		goto err_bcma_driver_exit;
#endif
	b43_print_driverinfo();

	return err;

#ifdef CONFIG_B43_SSB
err_bcma_driver_exit:
#endif
#ifdef CONFIG_B43_BCMA
	bcma_driver_unregister(&b43_bcma_driver);
err_sdio_exit:
#endif
	b43_sdio_exit();
err_pcmcia_exit:
	b43_pcmcia_exit();
err_dfs_exit:
	b43_debugfs_exit();
	return err;
}

static void __exit b43_exit(void)
{
#ifdef CONFIG_B43_SSB
	ssb_driver_unregister(&b43_ssb_driver);
#endif
#ifdef CONFIG_B43_BCMA
	bcma_driver_unregister(&b43_bcma_driver);
#endif
	b43_sdio_exit();
	b43_pcmcia_exit();
	b43_debugfs_exit();
}

module_init(b43_init)
module_exit(b43_exit)
