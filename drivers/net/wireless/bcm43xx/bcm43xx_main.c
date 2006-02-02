/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

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
#include <net/iw_handler.h>

#include "bcm43xx.h"
#include "bcm43xx_main.h"
#include "bcm43xx_debugfs.h"
#include "bcm43xx_radio.h"
#include "bcm43xx_phy.h"
#include "bcm43xx_dma.h"
#include "bcm43xx_pio.h"
#include "bcm43xx_power.h"
#include "bcm43xx_wx.h"
#include "bcm43xx_ethtool.h"


MODULE_DESCRIPTION("Broadcom BCM43xx wireless driver");
MODULE_AUTHOR("Martin Langer");
MODULE_AUTHOR("Stefano Brivio");
MODULE_AUTHOR("Michael Buesch");
MODULE_LICENSE("GPL");

#ifdef CONFIG_BCM947XX
extern char *nvram_get(char *name);
#endif

/* Module parameters */
static int modparam_pio;
module_param_named(pio, modparam_pio, int, 0444);
MODULE_PARM_DESC(pio, "enable(1) / disable(0) PIO mode");

static int modparam_bad_frames_preempt;
module_param_named(bad_frames_preempt, modparam_bad_frames_preempt, int, 0444);
MODULE_PARM_DESC(bad_frames_preempt, "enable(1) / disable(0) Bad Frames Preemption");

static int modparam_short_retry = BCM43xx_DEFAULT_SHORT_RETRY_LIMIT;
module_param_named(short_retry, modparam_short_retry, int, 0444);
MODULE_PARM_DESC(short_retry, "Short-Retry-Limit (0 - 15)");

static int modparam_long_retry = BCM43xx_DEFAULT_LONG_RETRY_LIMIT;
module_param_named(long_retry, modparam_long_retry, int, 0444);
MODULE_PARM_DESC(long_retry, "Long-Retry-Limit (0 - 15)");

static int modparam_locale = -1;
module_param_named(locale, modparam_locale, int, 0444);
MODULE_PARM_DESC(country, "Select LocaleCode 0-11 (For travelers)");

static int modparam_noleds;
module_param_named(noleds, modparam_noleds, int, 0444);
MODULE_PARM_DESC(noleds, "Turn off all LED activity");

#ifdef CONFIG_BCM43XX_DEBUG
static char modparam_fwpostfix[64];
module_param_string(fwpostfix, modparam_fwpostfix, 64, 0444);
MODULE_PARM_DESC(fwpostfix, "Postfix for .fw files. Useful for debugging.");
#else
# define modparam_fwpostfix  ""
#endif /* CONFIG_BCM43XX_DEBUG*/


/* If you want to debug with just a single device, enable this,
 * where the string is the pci device ID (as given by the kernel's
 * pci_name function) of the device to be used.
 */
//#define DEBUG_SINGLE_DEVICE_ONLY	"0001:11:00.0"

/* If you want to enable printing of each MMIO access, enable this. */
//#define DEBUG_ENABLE_MMIO_PRINT

/* If you want to enable printing of MMIO access within
 * ucode/pcm upload, initvals write, enable this.
 */
//#define DEBUG_ENABLE_UCODE_MMIO_PRINT

/* If you want to enable printing of PCI Config Space access, enable this */
//#define DEBUG_ENABLE_PCILOG


static struct pci_device_id bcm43xx_pci_tbl[] = {

	/* Detailed list maintained at:
	 * http://openfacts.berlios.de/index-en.phtml?title=Bcm43xxDevices
	 */
	
#ifdef CONFIG_BCM947XX
	/* SB bus on BCM947xx */
	{ PCI_VENDOR_ID_BROADCOM, 0x0800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
#endif
	
	/* Broadcom 4303 802.11b */
	{ PCI_VENDOR_ID_BROADCOM, 0x4301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	
	/* Broadcom 4307 802.11b */
	{ PCI_VENDOR_ID_BROADCOM, 0x4307, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	
	/* Broadcom 4318 802.11b/g */
	{ PCI_VENDOR_ID_BROADCOM, 0x4318, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },

	/* Broadcom 4306 802.11b/g */
	{ PCI_VENDOR_ID_BROADCOM, 0x4320, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	
	/* Broadcom 4306 802.11a */
//	{ PCI_VENDOR_ID_BROADCOM, 0x4321, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },

	/* Broadcom 4309 802.11a/b/g */
	{ PCI_VENDOR_ID_BROADCOM, 0x4324, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },

	/* Broadcom 43XG 802.11b/g */
	{ PCI_VENDOR_ID_BROADCOM, 0x4325, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },

	/* required last entry */
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, bcm43xx_pci_tbl);

static void bcm43xx_ram_write(struct bcm43xx_private *bcm, u16 offset, u32 val)
{
	u32 status;

	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	if (!(status & BCM43xx_SBF_XFER_REG_BYTESWAP))
		val = swab32(val);

	bcm43xx_write32(bcm, BCM43xx_MMIO_RAM_CONTROL, offset);
	bcm43xx_write32(bcm, BCM43xx_MMIO_RAM_DATA, val);
}

static inline
void bcm43xx_shm_control_word(struct bcm43xx_private *bcm,
			      u16 routing, u16 offset)
{
	u32 control;

	/* "offset" is the WORD offset. */

	control = routing;
	control <<= 16;
	control |= offset;
	bcm43xx_write32(bcm, BCM43xx_MMIO_SHM_CONTROL, control);
}

u32 bcm43xx_shm_read32(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset)
{
	u32 ret;

	if (routing == BCM43xx_SHM_SHARED) {
		if (offset & 0x0003) {
			/* Unaligned access */
			bcm43xx_shm_control_word(bcm, routing, offset >> 2);
			ret = bcm43xx_read16(bcm, BCM43xx_MMIO_SHM_DATA_UNALIGNED);
			ret <<= 16;
			bcm43xx_shm_control_word(bcm, routing, (offset >> 2) + 1);
			ret |= bcm43xx_read16(bcm, BCM43xx_MMIO_SHM_DATA);

			return ret;
		}
		offset >>= 2;
	}
	bcm43xx_shm_control_word(bcm, routing, offset);
	ret = bcm43xx_read32(bcm, BCM43xx_MMIO_SHM_DATA);

	return ret;
}

u16 bcm43xx_shm_read16(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset)
{
	u16 ret;

	if (routing == BCM43xx_SHM_SHARED) {
		if (offset & 0x0003) {
			/* Unaligned access */
			bcm43xx_shm_control_word(bcm, routing, offset >> 2);
			ret = bcm43xx_read16(bcm, BCM43xx_MMIO_SHM_DATA_UNALIGNED);

			return ret;
		}
		offset >>= 2;
	}
	bcm43xx_shm_control_word(bcm, routing, offset);
	ret = bcm43xx_read16(bcm, BCM43xx_MMIO_SHM_DATA);

	return ret;
}

void bcm43xx_shm_write32(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u32 value)
{
	if (routing == BCM43xx_SHM_SHARED) {
		if (offset & 0x0003) {
			/* Unaligned access */
			bcm43xx_shm_control_word(bcm, routing, offset >> 2);
			bcm43xx_write16(bcm, BCM43xx_MMIO_SHM_DATA_UNALIGNED,
					(value >> 16) & 0xffff);
			bcm43xx_shm_control_word(bcm, routing, (offset >> 2) + 1);
			bcm43xx_write16(bcm, BCM43xx_MMIO_SHM_DATA,
					value & 0xffff);
			return;
		}
		offset >>= 2;
	}
	bcm43xx_shm_control_word(bcm, routing, offset);
	bcm43xx_write32(bcm, BCM43xx_MMIO_SHM_DATA, value);
}

void bcm43xx_shm_write16(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u16 value)
{
	if (routing == BCM43xx_SHM_SHARED) {
		if (offset & 0x0003) {
			/* Unaligned access */
			bcm43xx_shm_control_word(bcm, routing, offset >> 2);
			bcm43xx_write16(bcm, BCM43xx_MMIO_SHM_DATA_UNALIGNED,
					value);
			return;
		}
		offset >>= 2;
	}
	bcm43xx_shm_control_word(bcm, routing, offset);
	bcm43xx_write16(bcm, BCM43xx_MMIO_SHM_DATA, value);
}

void bcm43xx_tsf_read(struct bcm43xx_private *bcm, u64 *tsf)
{
	/* We need to be careful. As we read the TSF from multiple
	 * registers, we should take care of register overflows.
	 * In theory, the whole tsf read process should be atomic.
	 * We try to be atomic here, by restaring the read process,
	 * if any of the high registers changed (overflew).
	 */
	if (bcm->current_core->rev >= 3) {
		u32 low, high, high2;

		do {
			high = bcm43xx_read32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_HIGH);
			low = bcm43xx_read32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_LOW);
			high2 = bcm43xx_read32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_HIGH);
		} while (unlikely(high != high2));

		*tsf = high;
		*tsf <<= 32;
		*tsf |= low;
	} else {
		u64 tmp;
		u16 v0, v1, v2, v3;
		u16 test1, test2, test3;

		do {
			v3 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_3);
			v2 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_2);
			v1 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_1);
			v0 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_0);

			test3 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_3);
			test2 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_2);
			test1 = bcm43xx_read16(bcm, BCM43xx_MMIO_TSF_1);
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

void bcm43xx_tsf_write(struct bcm43xx_private *bcm, u64 tsf)
{
	u32 status;

	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	status |= BCM43xx_SBF_TIME_UPDATE;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, status);

	/* Be careful with the in-progress timer.
	 * First zero out the low register, so we have a full
	 * register-overflow duration to complete the operation.
	 */
	if (bcm->current_core->rev >= 3) {
		u32 lo = (tsf & 0x00000000FFFFFFFFULL);
		u32 hi = (tsf & 0xFFFFFFFF00000000ULL) >> 32;

		barrier();
		bcm43xx_write32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_LOW, 0);
		bcm43xx_write32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_HIGH, hi);
		bcm43xx_write32(bcm, BCM43xx_MMIO_REV3PLUS_TSF_LOW, lo);
	} else {
		u16 v0 = (tsf & 0x000000000000FFFFULL);
		u16 v1 = (tsf & 0x00000000FFFF0000ULL) >> 16;
		u16 v2 = (tsf & 0x0000FFFF00000000ULL) >> 32;
		u16 v3 = (tsf & 0xFFFF000000000000ULL) >> 48;

		barrier();
		bcm43xx_write16(bcm, BCM43xx_MMIO_TSF_0, 0);
		bcm43xx_write16(bcm, BCM43xx_MMIO_TSF_3, v3);
		bcm43xx_write16(bcm, BCM43xx_MMIO_TSF_2, v2);
		bcm43xx_write16(bcm, BCM43xx_MMIO_TSF_1, v1);
		bcm43xx_write16(bcm, BCM43xx_MMIO_TSF_0, v0);
	}

	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	status &= ~BCM43xx_SBF_TIME_UPDATE;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, status);
}

static inline
u8 bcm43xx_plcp_get_bitrate(struct bcm43xx_plcp_hdr4 *plcp,
			    const int ofdm_modulation)
{
	u8 rate;

	if (ofdm_modulation) {
		switch (plcp->raw[0] & 0xF) {
		case 0xB:
			rate = IEEE80211_OFDM_RATE_6MB;
			break;
		case 0xF:
			rate = IEEE80211_OFDM_RATE_9MB;
			break;
		case 0xA:
			rate = IEEE80211_OFDM_RATE_12MB;
			break;
		case 0xE:
			rate = IEEE80211_OFDM_RATE_18MB;
			break;
		case 0x9:
			rate = IEEE80211_OFDM_RATE_24MB;
			break;
		case 0xD:
			rate = IEEE80211_OFDM_RATE_36MB;
			break;
		case 0x8:
			rate = IEEE80211_OFDM_RATE_48MB;
			break;
		case 0xC:
			rate = IEEE80211_OFDM_RATE_54MB;
			break;
		default:
			rate = 0;
			assert(0);
		}
	} else {
		switch (plcp->raw[0]) {
		case 0x0A:
			rate = IEEE80211_CCK_RATE_1MB;
			break;
		case 0x14:
			rate = IEEE80211_CCK_RATE_2MB;
			break;
		case 0x37:
			rate = IEEE80211_CCK_RATE_5MB;
			break;
		case 0x6E:
			rate = IEEE80211_CCK_RATE_11MB;
			break;
		default:
			rate = 0;
			assert(0);
		}
	}

	return rate;
}

static inline
u8 bcm43xx_plcp_get_ratecode_cck(const u8 bitrate)
{
	switch (bitrate) {
	case IEEE80211_CCK_RATE_1MB:
		return 0x0A;
	case IEEE80211_CCK_RATE_2MB:
		return 0x14;
	case IEEE80211_CCK_RATE_5MB:
		return 0x37;
	case IEEE80211_CCK_RATE_11MB:
		return 0x6E;
	}
	assert(0);
	return 0;
}

static inline
u8 bcm43xx_plcp_get_ratecode_ofdm(const u8 bitrate)
{
	switch (bitrate) {
	case IEEE80211_OFDM_RATE_6MB:
		return 0xB;
	case IEEE80211_OFDM_RATE_9MB:
		return 0xF;
	case IEEE80211_OFDM_RATE_12MB:
		return 0xA;
	case IEEE80211_OFDM_RATE_18MB:
		return 0xE;
	case IEEE80211_OFDM_RATE_24MB:
		return 0x9;
	case IEEE80211_OFDM_RATE_36MB:
		return 0xD;
	case IEEE80211_OFDM_RATE_48MB:
		return 0x8;
	case IEEE80211_OFDM_RATE_54MB:
		return 0xC;
	}
	assert(0);
	return 0;
}

static void bcm43xx_generate_plcp_hdr(struct bcm43xx_plcp_hdr4 *plcp,
				      u16 octets, const u8 bitrate,
				      const int ofdm_modulation)
{
	__le32 *data = &(plcp->data);
	__u8 *raw = plcp->raw;

	/* Account for hardware-appended FCS. */
	octets += IEEE80211_FCS_LEN;

	if (ofdm_modulation) {
		*data = bcm43xx_plcp_get_ratecode_ofdm(bitrate);
		assert(!(octets & 0xF000));
		*data |= (octets << 5);
		*data = cpu_to_le32(*data);
	} else {
		u32 plen;

		plen = octets * 16 / bitrate;
		if ((octets * 16 % bitrate) > 0) {
			plen++;
			if ((bitrate == IEEE80211_CCK_RATE_11MB)
			    && ((octets * 8 % 11) < 4)) {
				raw[1] = 0x84;
			} else
				raw[1] = 0x04;
		} else
			raw[1] = 0x04;
		*data |= cpu_to_le32(plen << 16);
		raw[0] = bcm43xx_plcp_get_ratecode_cck(bitrate);
	}

//bcm43xx_printk_bitdump(raw, 4, 0, "PLCP");
}

void fastcall
bcm43xx_generate_txhdr(struct bcm43xx_private *bcm,
		       struct bcm43xx_txhdr *txhdr,
		       const unsigned char *fragment_data,
		       unsigned int fragment_len,
		       const int is_first_fragment,
		       const u16 cookie)
{
	const struct bcm43xx_phyinfo *phy = bcm->current_core->phy;
	const struct ieee80211_hdr_1addr *wireless_header = (const struct ieee80211_hdr_1addr *)fragment_data;
	const struct ieee80211_security *secinfo = &bcm->ieee->sec;
	u8 bitrate;
	int ofdm_modulation;
	u8 fallback_bitrate;
	int fallback_ofdm_modulation;
	u16 tmp;
	u16 encrypt_frame;

	/* Now construct the TX header. */
	memset(txhdr, 0, sizeof(*txhdr));

	//TODO: Some RTS/CTS stuff has to be done.
	//TODO: Encryption stuff.
	//TODO: others?

	bitrate = bcm->softmac->txrates.default_rate;
	ofdm_modulation = !(ieee80211_is_cck_rate(bitrate));
	fallback_bitrate = bcm->softmac->txrates.default_fallback;
	fallback_ofdm_modulation = !(ieee80211_is_cck_rate(fallback_bitrate));

	/* Set Frame Control from 80211 header. */
	txhdr->frame_control = wireless_header->frame_ctl;
	/* Copy address1 from 80211 header. */
	memcpy(txhdr->mac1, wireless_header->addr1, 6);
	/* Set the fallback duration ID. */
	//FIXME: We use the original durid for now.
	txhdr->fallback_dur_id = wireless_header->duration_id;

	/* Set the cookie (used as driver internal ID for the frame) */
	txhdr->cookie = cpu_to_le16(cookie);

	encrypt_frame = le16_to_cpup(&wireless_header->frame_ctl) & IEEE80211_FCTL_PROTECTED;
	if (encrypt_frame && !bcm->ieee->host_encrypt) {
		const struct ieee80211_hdr_3addr *hdr = (struct ieee80211_hdr_3addr *)wireless_header;
		if (fragment_len <= sizeof(struct ieee80211_hdr_3addr)+4) {
			dprintkl(KERN_ERR PFX "invalid packet with PROTECTED"
					      "flag set discarded");
			return;
		}
		memcpy(txhdr->wep_iv, hdr->payload, 4);
		/* Hardware appends ICV. */
		fragment_len += 4;
	}

	/* Generate the PLCP header and the fallback PLCP header. */
	bcm43xx_generate_plcp_hdr((struct bcm43xx_plcp_hdr4 *)(&txhdr->plcp),
				  fragment_len,
				  bitrate, ofdm_modulation);
	bcm43xx_generate_plcp_hdr(&txhdr->fallback_plcp, fragment_len,
				  fallback_bitrate, fallback_ofdm_modulation);

	/* Set the CONTROL field */
	tmp = 0;
	if (ofdm_modulation)
		tmp |= BCM43xx_TXHDRCTL_OFDM;
	if (bcm->short_preamble) //FIXME: could be the other way around, please test
		tmp |= BCM43xx_TXHDRCTL_SHORT_PREAMBLE;
	tmp |= (phy->antenna_diversity << BCM43xx_TXHDRCTL_ANTENNADIV_SHIFT)
		& BCM43xx_TXHDRCTL_ANTENNADIV_MASK;
	txhdr->control = cpu_to_le16(tmp);

	/* Set the FLAGS field */
	tmp = 0;
	if (!is_multicast_ether_addr(wireless_header->addr1) &&
	    !is_broadcast_ether_addr(wireless_header->addr1))
		tmp |= BCM43xx_TXHDRFLAG_EXPECTACK;
	if (1 /* FIXME: PS poll?? */)
		tmp |= 0x10; // FIXME: unknown meaning.
	if (fallback_ofdm_modulation)
		tmp |= BCM43xx_TXHDRFLAG_FALLBACKOFDM;
	if (is_first_fragment)
		tmp |= BCM43xx_TXHDRFLAG_FIRSTFRAGMENT;
	txhdr->flags = cpu_to_le16(tmp);

	/* Set WSEC/RATE field */
	if (encrypt_frame && !bcm->ieee->host_encrypt) {
		tmp = (bcm->key[secinfo->active_key].algorithm << BCM43xx_TXHDR_WSEC_ALGO_SHIFT)
		       & BCM43xx_TXHDR_WSEC_ALGO_MASK;
		tmp |= (secinfo->active_key << BCM43xx_TXHDR_WSEC_KEYINDEX_SHIFT)
			& BCM43xx_TXHDR_WSEC_KEYINDEX_MASK;
		txhdr->wsec_rate = cpu_to_le16(tmp);
	}

//bcm43xx_printk_bitdump((const unsigned char *)txhdr, sizeof(*txhdr), 1, "TX header");
}

static
void bcm43xx_macfilter_set(struct bcm43xx_private *bcm,
			   u16 offset,
			   const u8 *mac)
{
	u16 data;

	offset |= 0x0020;
	bcm43xx_write16(bcm, BCM43xx_MMIO_MACFILTER_CONTROL, offset);

	data = mac[0];
	data |= mac[1] << 8;
	bcm43xx_write16(bcm, BCM43xx_MMIO_MACFILTER_DATA, data);
	data = mac[2];
	data |= mac[3] << 8;
	bcm43xx_write16(bcm, BCM43xx_MMIO_MACFILTER_DATA, data);
	data = mac[4];
	data |= mac[5] << 8;
	bcm43xx_write16(bcm, BCM43xx_MMIO_MACFILTER_DATA, data);
}

static inline
void bcm43xx_macfilter_clear(struct bcm43xx_private *bcm,
			     u16 offset)
{
	const u8 zero_addr[ETH_ALEN] = { 0 };

	bcm43xx_macfilter_set(bcm, offset, zero_addr);
}

static void bcm43xx_write_mac_bssid_templates(struct bcm43xx_private *bcm)
{
	const u8 *mac = (const u8 *)(bcm->net_dev->dev_addr);
	const u8 *bssid = (const u8 *)(bcm->ieee->bssid);
	u8 mac_bssid[ETH_ALEN * 2];
	int i;

	memcpy(mac_bssid, mac, ETH_ALEN);
	memcpy(mac_bssid + ETH_ALEN, bssid, ETH_ALEN);

	/* Write our MAC address and BSSID to template ram */
	for (i = 0; i < ARRAY_SIZE(mac_bssid); i += sizeof(u32))
		bcm43xx_ram_write(bcm, 0x20 + i, *((u32 *)(mac_bssid + i)));
	for (i = 0; i < ARRAY_SIZE(mac_bssid); i += sizeof(u32))
		bcm43xx_ram_write(bcm, 0x78 + i, *((u32 *)(mac_bssid + i)));
	for (i = 0; i < ARRAY_SIZE(mac_bssid); i += sizeof(u32))
		bcm43xx_ram_write(bcm, 0x478 + i, *((u32 *)(mac_bssid + i)));
}

static inline
void bcm43xx_set_slot_time(struct bcm43xx_private *bcm, u16 slot_time)
{
	/* slot_time is in usec. */
	if (bcm->current_core->phy->type != BCM43xx_PHYTYPE_G)
		return;
	bcm43xx_write16(bcm, 0x684, 510 + slot_time);
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0010, slot_time);
}

static inline
void bcm43xx_short_slot_timing_enable(struct bcm43xx_private *bcm)
{
	bcm43xx_set_slot_time(bcm, 9);
}

static inline
void bcm43xx_short_slot_timing_disable(struct bcm43xx_private *bcm)
{
	bcm43xx_set_slot_time(bcm, 20);
}

//FIXME: rename this func?
static void bcm43xx_disassociate(struct bcm43xx_private *bcm)
{
	bcm43xx_mac_suspend(bcm);
	bcm43xx_macfilter_clear(bcm, BCM43xx_MACFILTER_ASSOC);

	bcm43xx_ram_write(bcm, 0x0026, 0x0000);
	bcm43xx_ram_write(bcm, 0x0028, 0x0000);
	bcm43xx_ram_write(bcm, 0x007E, 0x0000);
	bcm43xx_ram_write(bcm, 0x0080, 0x0000);
	bcm43xx_ram_write(bcm, 0x047E, 0x0000);
	bcm43xx_ram_write(bcm, 0x0480, 0x0000);

	if (bcm->current_core->rev < 3) {
		bcm43xx_write16(bcm, 0x0610, 0x8000);
		bcm43xx_write16(bcm, 0x060E, 0x0000);
	} else
		bcm43xx_write32(bcm, 0x0188, 0x80000000);

	bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0004, 0x000003ff);

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G &&
	    ieee80211_is_ofdm_rate(bcm->softmac->txrates.default_rate))
		bcm43xx_short_slot_timing_enable(bcm);

	bcm43xx_mac_enable(bcm);
}

//FIXME: rename this func?
static void bcm43xx_associate(struct bcm43xx_private *bcm,
			      const u8 *mac)
{
	memcpy(bcm->ieee->bssid, mac, ETH_ALEN);

	bcm43xx_mac_suspend(bcm);
	bcm43xx_macfilter_set(bcm, BCM43xx_MACFILTER_ASSOC, mac);
	bcm43xx_write_mac_bssid_templates(bcm);
	bcm43xx_mac_enable(bcm);
}

/* Enable a Generic IRQ. "mask" is the mask of which IRQs to enable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 bcm43xx_interrupt_enable(struct bcm43xx_private *bcm, u32 mask)
{
	u32 old_mask;

	old_mask = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_MASK);
	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_MASK, old_mask | mask);

	return old_mask;
}

/* Disable a Generic IRQ. "mask" is the mask of which IRQs to disable.
 * Returns the _previously_ enabled IRQ mask.
 */
static inline u32 bcm43xx_interrupt_disable(struct bcm43xx_private *bcm, u32 mask)
{
	u32 old_mask;

	old_mask = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_MASK);
	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_MASK, old_mask & ~mask);

	return old_mask;
}

/* Make sure we don't receive more data from the device. */
static int bcm43xx_disable_interrupts_sync(struct bcm43xx_private *bcm, u32 *oldstate)
{
	u32 old;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);
	if (bcm43xx_is_initializing(bcm) || bcm->shutting_down) {
		spin_unlock_irqrestore(&bcm->lock, flags);
		return -EBUSY;
	}
	old = bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);
	tasklet_disable(&bcm->isr_tasklet);
	spin_unlock_irqrestore(&bcm->lock, flags);
	if (oldstate)
		*oldstate = old;

	return 0;
}

static int bcm43xx_read_radioinfo(struct bcm43xx_private *bcm)
{
	u32 radio_id;
	u16 manufact;
	u16 version;
	u8 revision;
	s8 i;

	if (bcm->chip_id == 0x4317) {
		if (bcm->chip_rev == 0x00)
			radio_id = 0x3205017F;
		else if (bcm->chip_rev == 0x01)
			radio_id = 0x4205017F;
		else
			radio_id = 0x5205017F;
	} else {
		bcm43xx_write16(bcm, BCM43xx_MMIO_RADIO_CONTROL, BCM43xx_RADIOCTL_ID);
		radio_id = bcm43xx_read16(bcm, BCM43xx_MMIO_RADIO_DATA_HIGH);
		radio_id <<= 16;
		bcm43xx_write16(bcm, BCM43xx_MMIO_RADIO_CONTROL, BCM43xx_RADIOCTL_ID);
		radio_id |= bcm43xx_read16(bcm, BCM43xx_MMIO_RADIO_DATA_LOW);
	}

	manufact = (radio_id & 0x00000FFF);
	version = (radio_id & 0x0FFFF000) >> 12;
	revision = (radio_id & 0xF0000000) >> 28;

	dprintk(KERN_INFO PFX "Detected Radio:  ID: %x (Manuf: %x Ver: %x Rev: %x)\n",
		radio_id, manufact, version, revision);

	switch (bcm->current_core->phy->type) {
	case BCM43xx_PHYTYPE_A:
		if ((version != 0x2060) || (revision != 1) || (manufact != 0x17f))
			goto err_unsupported_radio;
		break;
	case BCM43xx_PHYTYPE_B:
		if ((version & 0xFFF0) != 0x2050)
			goto err_unsupported_radio;
		break;
	case BCM43xx_PHYTYPE_G:
		if (version != 0x2050)
			goto err_unsupported_radio;
		break;
	}

	bcm->current_core->radio->manufact = manufact;
	bcm->current_core->radio->version = version;
	bcm->current_core->radio->revision = revision;

	/* Set default attenuation values. */
	bcm->current_core->radio->txpower[0] = 2;
	bcm->current_core->radio->txpower[1] = 2;
	if (revision == 1)
		bcm->current_core->radio->txpower[2] = 3;
	else
		bcm->current_core->radio->txpower[2] = 0;

	/* Initialize the in-memory nrssi Lookup Table. */
	for (i = 0; i < 64; i++)
		bcm->current_core->radio->nrssi_lt[i] = i;

	return 0;

err_unsupported_radio:
	printk(KERN_ERR PFX "Unsupported Radio connected to the PHY!\n");
	return -ENODEV;
}

static const char * bcm43xx_locale_iso(u8 locale)
{
	/* ISO 3166-1 country codes.
	 * Note that there aren't ISO 3166-1 codes for
	 * all or locales. (Not all locales are countries)
	 */
	switch (locale) {
	case BCM43xx_LOCALE_WORLD:
	case BCM43xx_LOCALE_ALL:
		return "XX";
	case BCM43xx_LOCALE_THAILAND:
		return "TH";
	case BCM43xx_LOCALE_ISRAEL:
		return "IL";
	case BCM43xx_LOCALE_JORDAN:
		return "JO";
	case BCM43xx_LOCALE_CHINA:
		return "CN";
	case BCM43xx_LOCALE_JAPAN:
	case BCM43xx_LOCALE_JAPAN_HIGH:
		return "JP";
	case BCM43xx_LOCALE_USA_CANADA_ANZ:
	case BCM43xx_LOCALE_USA_LOW:
		return "US";
	case BCM43xx_LOCALE_EUROPE:
		return "EU";
	case BCM43xx_LOCALE_NONE:
		return "  ";
	}
	assert(0);
	return "  ";
}

static const char * bcm43xx_locale_string(u8 locale)
{
	switch (locale) {
	case BCM43xx_LOCALE_WORLD:
		return "World";
	case BCM43xx_LOCALE_THAILAND:
		return "Thailand";
	case BCM43xx_LOCALE_ISRAEL:
		return "Israel";
	case BCM43xx_LOCALE_JORDAN:
		return "Jordan";
	case BCM43xx_LOCALE_CHINA:
		return "China";
	case BCM43xx_LOCALE_JAPAN:
		return "Japan";
	case BCM43xx_LOCALE_USA_CANADA_ANZ:
		return "USA/Canada/ANZ";
	case BCM43xx_LOCALE_EUROPE:
		return "Europe";
	case BCM43xx_LOCALE_USA_LOW:
		return "USAlow";
	case BCM43xx_LOCALE_JAPAN_HIGH:
		return "JapanHigh";
	case BCM43xx_LOCALE_ALL:
		return "All";
	case BCM43xx_LOCALE_NONE:
		return "None";
	}
	assert(0);
	return "";
}

static inline u8 bcm43xx_crc8(u8 crc, u8 data)
{
	static const u8 t[] = {
		0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
		0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
		0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
		0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
		0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
		0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
		0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
		0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
		0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
		0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
		0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
		0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
		0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
		0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
		0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
		0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
		0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
		0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
		0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
		0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
		0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
		0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
		0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
		0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
		0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
		0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
		0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
		0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
		0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
		0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
		0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
		0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
	};
	return t[crc ^ data];
}

u8 bcm43xx_sprom_crc(const u16 *sprom)
{
	int word;
	u8 crc = 0xFF;

	for (word = 0; word < BCM43xx_SPROM_SIZE - 1; word++) {
		crc = bcm43xx_crc8(crc, sprom[word] & 0x00FF);
		crc = bcm43xx_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = bcm43xx_crc8(crc, sprom[BCM43xx_SPROM_VERSION] & 0x00FF);
	crc ^= 0xFF;

	return crc;
}


static int bcm43xx_read_sprom(struct bcm43xx_private *bcm)
{
	int i;
	u16 value;
	u16 *sprom;
	u8 crc, expected_crc;
#ifdef CONFIG_BCM947XX
	char *c;
#endif

	sprom = kzalloc(BCM43xx_SPROM_SIZE * sizeof(u16),
			GFP_KERNEL);
	if (!sprom) {
		printk(KERN_ERR PFX "read_sprom OOM\n");
		return -ENOMEM;
	}
#ifdef CONFIG_BCM947XX
	sprom[BCM43xx_SPROM_BOARDFLAGS2] = atoi(nvram_get("boardflags2"));
	sprom[BCM43xx_SPROM_BOARDFLAGS] = atoi(nvram_get("boardflags"));

	if ((c = nvram_get("il0macaddr")) != NULL)
		e_aton(c, (char *) &(sprom[BCM43xx_SPROM_IL0MACADDR]));

	if ((c = nvram_get("et1macaddr")) != NULL)
		e_aton(c, (char *) &(sprom[BCM43xx_SPROM_ET1MACADDR]));

	sprom[BCM43xx_SPROM_PA0B0] = atoi(nvram_get("pa0b0"));
	sprom[BCM43xx_SPROM_PA0B1] = atoi(nvram_get("pa0b1"));
	sprom[BCM43xx_SPROM_PA0B2] = atoi(nvram_get("pa0b2"));

	sprom[BCM43xx_SPROM_PA1B0] = atoi(nvram_get("pa1b0"));
	sprom[BCM43xx_SPROM_PA1B1] = atoi(nvram_get("pa1b1"));
	sprom[BCM43xx_SPROM_PA1B2] = atoi(nvram_get("pa1b2"));

	sprom[BCM43xx_SPROM_BOARDREV] = atoi(nvram_get("boardrev"));
#else
	for (i = 0; i < BCM43xx_SPROM_SIZE; i++)
		sprom[i] = bcm43xx_read16(bcm, BCM43xx_SPROM_BASE + (i * 2));

	/* CRC-8 check. */
	crc = bcm43xx_sprom_crc(sprom);
	expected_crc = (sprom[BCM43xx_SPROM_VERSION] & 0xFF00) >> 8;
	if (crc != expected_crc) {
		printk(KERN_WARNING PFX "WARNING: Invalid SPROM checksum "
					"(0x%02X, expected: 0x%02X)\n",
		       crc, expected_crc);
	}
#endif

	/* boardflags2 */
	value = sprom[BCM43xx_SPROM_BOARDFLAGS2];
	bcm->sprom.boardflags2 = value;

	/* il0macaddr */
	value = sprom[BCM43xx_SPROM_IL0MACADDR + 0];
	*(((u16 *)bcm->sprom.il0macaddr) + 0) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_IL0MACADDR + 1];
	*(((u16 *)bcm->sprom.il0macaddr) + 1) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_IL0MACADDR + 2];
	*(((u16 *)bcm->sprom.il0macaddr) + 2) = cpu_to_be16(value);

	/* et0macaddr */
	value = sprom[BCM43xx_SPROM_ET0MACADDR + 0];
	*(((u16 *)bcm->sprom.et0macaddr) + 0) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_ET0MACADDR + 1];
	*(((u16 *)bcm->sprom.et0macaddr) + 1) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_ET0MACADDR + 2];
	*(((u16 *)bcm->sprom.et0macaddr) + 2) = cpu_to_be16(value);

	/* et1macaddr */
	value = sprom[BCM43xx_SPROM_ET1MACADDR + 0];
	*(((u16 *)bcm->sprom.et1macaddr) + 0) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_ET1MACADDR + 1];
	*(((u16 *)bcm->sprom.et1macaddr) + 1) = cpu_to_be16(value);
	value = sprom[BCM43xx_SPROM_ET1MACADDR + 2];
	*(((u16 *)bcm->sprom.et1macaddr) + 2) = cpu_to_be16(value);

	/* ethernet phy settings */
	value = sprom[BCM43xx_SPROM_ETHPHY];
	bcm->sprom.et0phyaddr = (value & 0x001F);
	bcm->sprom.et1phyaddr = (value & 0x03E0) >> 5;
	bcm->sprom.et0mdcport = (value & (1 << 14)) >> 14;
	bcm->sprom.et1mdcport = (value & (1 << 15)) >> 15;

	/* boardrev, antennas, locale */
	value = sprom[BCM43xx_SPROM_BOARDREV];
	bcm->sprom.boardrev = (value & 0x00FF);
	bcm->sprom.locale = (value & 0x0F00) >> 8;
	bcm->sprom.antennas_aphy = (value & 0x3000) >> 12;
	bcm->sprom.antennas_bgphy = (value & 0xC000) >> 14;
	if (modparam_locale != -1) {
		if (modparam_locale >= 0 && modparam_locale <= 11) {
			bcm->sprom.locale = modparam_locale;
			printk(KERN_WARNING PFX "Operating with modified "
						"LocaleCode %u (%s)\n",
			       bcm->sprom.locale,
			       bcm43xx_locale_string(bcm->sprom.locale));
		} else {
			printk(KERN_WARNING PFX "Module parameter \"locale\" "
						"invalid value. (0 - 11)\n");
		}
	}

	/* pa0b* */
	value = sprom[BCM43xx_SPROM_PA0B0];
	bcm->sprom.pa0b0 = value;
	value = sprom[BCM43xx_SPROM_PA0B1];
	bcm->sprom.pa0b1 = value;
	value = sprom[BCM43xx_SPROM_PA0B2];
	bcm->sprom.pa0b2 = value;

	/* wl0gpio* */
	value = sprom[BCM43xx_SPROM_WL0GPIO0];
	if (value == 0x0000)
		value = 0xFFFF;
	bcm->sprom.wl0gpio0 = value & 0x00FF;
	bcm->sprom.wl0gpio1 = (value & 0xFF00) >> 8;
	value = sprom[BCM43xx_SPROM_WL0GPIO2];
	if (value == 0x0000)
		value = 0xFFFF;
	bcm->sprom.wl0gpio2 = value & 0x00FF;
	bcm->sprom.wl0gpio3 = (value & 0xFF00) >> 8;

	/* maxpower */
	value = sprom[BCM43xx_SPROM_MAXPWR];
	bcm->sprom.maxpower_aphy = (value & 0xFF00) >> 8;
	bcm->sprom.maxpower_bgphy = value & 0x00FF;

	/* pa1b* */
	value = sprom[BCM43xx_SPROM_PA1B0];
	bcm->sprom.pa1b0 = value;
	value = sprom[BCM43xx_SPROM_PA1B1];
	bcm->sprom.pa1b1 = value;
	value = sprom[BCM43xx_SPROM_PA1B2];
	bcm->sprom.pa1b2 = value;

	/* idle tssi target */
	value = sprom[BCM43xx_SPROM_IDL_TSSI_TGT];
	bcm->sprom.idle_tssi_tgt_aphy = value & 0x00FF;
	bcm->sprom.idle_tssi_tgt_bgphy = (value & 0xFF00) >> 8;

	/* boardflags */
	value = sprom[BCM43xx_SPROM_BOARDFLAGS];
	if (value == 0xFFFF)
		value = 0x0000;
	bcm->sprom.boardflags = value;

	/* antenna gain */
	value = sprom[BCM43xx_SPROM_ANTENNA_GAIN];
	if (value == 0x0000 || value == 0xFFFF)
		value = 0x0202;
	/* convert values to Q5.2 */
	bcm->sprom.antennagain_aphy = ((value & 0xFF00) >> 8) * 4;
	bcm->sprom.antennagain_bgphy = (value & 0x00FF) * 4;

	kfree(sprom);

	return 0;
}

static void bcm43xx_geo_init(struct bcm43xx_private *bcm)
{
	struct ieee80211_geo geo;
	struct ieee80211_channel *chan;
	int have_a = 0, have_bg = 0;
	int i, num80211;
	u8 channel;
	struct bcm43xx_phyinfo *phy;
	const char *iso_country;

	memset(&geo, 0, sizeof(geo));
	num80211 = bcm43xx_num_80211_cores(bcm);
	for (i = 0; i < num80211; i++) {
		phy = bcm->phy + i;
		switch (phy->type) {
		case BCM43xx_PHYTYPE_B:
		case BCM43xx_PHYTYPE_G:
			have_bg = 1;
			break;
		case BCM43xx_PHYTYPE_A:
			have_a = 1;
			break;
		default:
			assert(0);
		}
	}
	iso_country = bcm43xx_locale_iso(bcm->sprom.locale);

 	if (have_a) {
		for (i = 0, channel = 0; channel < 201; channel++) {
			chan = &geo.a[i++];
			chan->freq = bcm43xx_channel_to_freq(bcm, channel);
			chan->channel = channel;
		}
		geo.a_channels = i;
	}
	if (have_bg) {
		for (i = 0, channel = 1; channel < 15; channel++) {
			chan = &geo.bg[i++];
			chan->freq = bcm43xx_channel_to_freq(bcm, channel);
			chan->channel = channel;
		}
		geo.bg_channels = i;
	}
	memcpy(geo.name, iso_country, 2);
	if (0 /*TODO: Outdoor use only */)
		geo.name[2] = 'O';
	else if (0 /*TODO: Indoor use only */)
		geo.name[2] = 'I';
	else
		geo.name[2] = ' ';
	geo.name[3] = '\0';

	ieee80211_set_geo(bcm->ieee, &geo);
}

/* DummyTransmission function, as documented on 
 * http://bcm-specs.sipsolutions.net/DummyTransmission
 */
void bcm43xx_dummy_transmission(struct bcm43xx_private *bcm)
{
	unsigned int i, max_loop;
	u16 value = 0;
	u32 buffer[5] = {
		0x00000000,
		0x0000D400,
		0x00000000,
		0x00000001,
		0x00000000,
	};

	switch (bcm->current_core->phy->type) {
	case BCM43xx_PHYTYPE_A:
		max_loop = 0x1E;
		buffer[0] = 0xCC010200;
		break;
	case BCM43xx_PHYTYPE_B:
	case BCM43xx_PHYTYPE_G:
		max_loop = 0xFA;
		buffer[0] = 0x6E840B00; 
		break;
	default:
		assert(0);
		return;
	}

	for (i = 0; i < 5; i++)
		bcm43xx_ram_write(bcm, i * 4, buffer[i]);

	bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD); /* dummy read */

	bcm43xx_write16(bcm, 0x0568, 0x0000);
	bcm43xx_write16(bcm, 0x07C0, 0x0000);
	bcm43xx_write16(bcm, 0x050C, ((bcm->current_core->phy->type == BCM43xx_PHYTYPE_A) ? 1 : 0));
	bcm43xx_write16(bcm, 0x0508, 0x0000);
	bcm43xx_write16(bcm, 0x050A, 0x0000);
	bcm43xx_write16(bcm, 0x054C, 0x0000);
	bcm43xx_write16(bcm, 0x056A, 0x0014);
	bcm43xx_write16(bcm, 0x0568, 0x0826);
	bcm43xx_write16(bcm, 0x0500, 0x0000);
	bcm43xx_write16(bcm, 0x0502, 0x0030);

	for (i = 0x00; i < max_loop; i++) {
		value = bcm43xx_read16(bcm, 0x050E);
		if ((value & 0x0080) != 0)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = bcm43xx_read16(bcm, 0x050E);
		if ((value & 0x0400) != 0)
			break;
		udelay(10);
	}
	for (i = 0x00; i < 0x0A; i++) {
		value = bcm43xx_read16(bcm, 0x0690);
		if ((value & 0x0100) == 0)
			break;
		udelay(10);
	}
}

static void key_write(struct bcm43xx_private *bcm,
		      u8 index, u8 algorithm, const u16 *key)
{
	unsigned int i, basic_wep = 0;
	u32 offset;
	u16 value;
 
	/* Write associated key information */
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x100 + (index * 2),
			    ((index << 4) | (algorithm & 0x0F)));
 
	/* The first 4 WEP keys need extra love */
	if (((algorithm == BCM43xx_SEC_ALGO_WEP) ||
	    (algorithm == BCM43xx_SEC_ALGO_WEP104)) && (index < 4))
		basic_wep = 1;
 
	/* Write key payload, 8 little endian words */
	offset = bcm->security_offset + (index * BCM43xx_SEC_KEYSIZE);
	for (i = 0; i < (BCM43xx_SEC_KEYSIZE / sizeof(u16)); i++) {
		value = cpu_to_le16(key[i]);
		bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED,
				    offset + (i * 2), value);
 
		if (!basic_wep)
			continue;
 
		bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED,
				    offset + (i * 2) + 4 * BCM43xx_SEC_KEYSIZE,
				    value);
	}
}

static void keymac_write(struct bcm43xx_private *bcm,
			 u8 index, const u32 *addr)
{
	/* for keys 0-3 there is no associated mac address */
	if (index < 4)
		return;

	index -= 4;
	if (bcm->current_core->rev >= 5) {
		bcm43xx_shm_write32(bcm,
				    BCM43xx_SHM_HWMAC,
				    index * 2,
				    cpu_to_be32(*addr));
		bcm43xx_shm_write16(bcm,
				    BCM43xx_SHM_HWMAC,
				    (index * 2) + 1,
				    cpu_to_be16(*((u16 *)(addr + 1))));
	} else {
		if (index < 8) {
			TODO(); /* Put them in the macaddress filter */
		} else {
			TODO();
			/* Put them BCM43xx_SHM_SHARED, stating index 0x0120.
			   Keep in mind to update the count of keymacs in 0x003E as well! */
		}
	}
}

static int bcm43xx_key_write(struct bcm43xx_private *bcm,
			     u8 index, u8 algorithm,
			     const u8 *_key, int key_len,
			     const u8 *mac_addr)
{
	u8 key[BCM43xx_SEC_KEYSIZE] = { 0 };

	if (index >= ARRAY_SIZE(bcm->key))
		return -EINVAL;
	if (key_len > ARRAY_SIZE(key))
		return -EINVAL;
	if (algorithm < 1 || algorithm > 5)
		return -EINVAL;

	memcpy(key, _key, key_len);
	key_write(bcm, index, algorithm, (const u16 *)key);
	keymac_write(bcm, index, (const u32 *)mac_addr);

	bcm->key[index].algorithm = algorithm;

	return 0;
}

static void bcm43xx_clear_keys(struct bcm43xx_private *bcm)
{
	static const u32 zero_mac[2] = { 0 };
	unsigned int i,j, nr_keys = 54;
	u16 offset;

	if (bcm->current_core->rev < 5)
		nr_keys = 16;
	assert(nr_keys <= ARRAY_SIZE(bcm->key));

	for (i = 0; i < nr_keys; i++) {
		bcm->key[i].enabled = 0;
		/* returns for i < 4 immediately */
		keymac_write(bcm, i, zero_mac);
		bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED,
				    0x100 + (i * 2), 0x0000);
		for (j = 0; j < 8; j++) {
			offset = bcm->security_offset + (j * 4) + (i * BCM43xx_SEC_KEYSIZE);
			bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED,
					    offset, 0x0000);
		}
	}
	dprintk(KERN_INFO PFX "Keys cleared\n");
}

/* Puts the index of the current core into user supplied core variable.
 * This function reads the value from the device.
 * Almost always you don't want to call this, but use bcm->current_core
 */
static inline
int _get_current_core(struct bcm43xx_private *bcm, int *core)
{
	int err;

	err = bcm43xx_pci_read_config32(bcm, BCM43xx_REG_ACTIVE_CORE, core);
	if (unlikely(err)) {
		dprintk(KERN_ERR PFX "BCM43xx_REG_ACTIVE_CORE read failed!\n");
		return -ENODEV;
	}
	*core = (*core - 0x18000000) / 0x1000;

	return 0;
}

/* Lowlevel core-switch function. This is only to be used in
 * bcm43xx_switch_core() and bcm43xx_probe_cores()
 */
static int _switch_core(struct bcm43xx_private *bcm, int core)
{
	int err;
	int attempts = 0;
	int current_core = -1;

	assert(core >= 0);

	err = _get_current_core(bcm, &current_core);
	if (unlikely(err))
		goto out;

	/* Write the computed value to the register. This doesn't always
	   succeed so we retry BCM43xx_SWITCH_CORE_MAX_RETRIES times */
	while (current_core != core) {
		if (unlikely(attempts++ > BCM43xx_SWITCH_CORE_MAX_RETRIES)) {
			err = -ENODEV;
			printk(KERN_ERR PFX
			       "unable to switch to core %u, retried %i times\n",
			       core, attempts);
			goto out;
		}
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_REG_ACTIVE_CORE,
						 (core * 0x1000) + 0x18000000);
		if (unlikely(err)) {
			dprintk(KERN_ERR PFX "BCM43xx_REG_ACTIVE_CORE write failed!\n");
			continue;
		}
		_get_current_core(bcm, &current_core);
#ifdef CONFIG_BCM947XX
		if (bcm->pci_dev->bus->number == 0)
			bcm->current_core_offset = 0x1000 * core;
		else
			bcm->current_core_offset = 0;
#endif
	}

	assert(err == 0);
out:
	return err;
}

int bcm43xx_switch_core(struct bcm43xx_private *bcm, struct bcm43xx_coreinfo *new_core)
{
	int err;

	if (!new_core)
		return 0;

	if (!(new_core->flags & BCM43xx_COREFLAG_AVAILABLE))
		return -ENODEV;
	if (bcm->current_core == new_core)
		return 0;
	err = _switch_core(bcm, new_core->index);
	if (!err)
		bcm->current_core = new_core;

	return err;
}

static inline int bcm43xx_core_enabled(struct bcm43xx_private *bcm)
{
	u32 value;

	value = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
	value &= BCM43xx_SBTMSTATELOW_CLOCK | BCM43xx_SBTMSTATELOW_RESET
		 | BCM43xx_SBTMSTATELOW_REJECT;

	return (value == BCM43xx_SBTMSTATELOW_CLOCK);
}

/* disable current core */
static int bcm43xx_core_disable(struct bcm43xx_private *bcm, u32 core_flags)
{
	u32 sbtmstatelow;
	u32 sbtmstatehigh;
	int i;

	/* fetch sbtmstatelow from core information registers */
	sbtmstatelow = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);

	/* core is already in reset */
	if (sbtmstatelow & BCM43xx_SBTMSTATELOW_RESET)
		goto out;

	if (sbtmstatelow & BCM43xx_SBTMSTATELOW_CLOCK) {
		sbtmstatelow = BCM43xx_SBTMSTATELOW_CLOCK |
			       BCM43xx_SBTMSTATELOW_REJECT;
		bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);

		for (i = 0; i < 1000; i++) {
			sbtmstatelow = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
			if (sbtmstatelow & BCM43xx_SBTMSTATELOW_REJECT) {
				i = -1;
				break;
			}
			udelay(10);
		}
		if (i != -1) {
			printk(KERN_ERR PFX "Error: core_disable() REJECT timeout!\n");
			return -EBUSY;
		}

		for (i = 0; i < 1000; i++) {
			sbtmstatehigh = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATEHIGH);
			if (!(sbtmstatehigh & BCM43xx_SBTMSTATEHIGH_BUSY)) {
				i = -1;
				break;
			}
			udelay(10);
		}
		if (i != -1) {
			printk(KERN_ERR PFX "Error: core_disable() BUSY timeout!\n");
			return -EBUSY;
		}

		sbtmstatelow = BCM43xx_SBTMSTATELOW_FORCE_GATE_CLOCK |
			       BCM43xx_SBTMSTATELOW_REJECT |
			       BCM43xx_SBTMSTATELOW_RESET |
			       BCM43xx_SBTMSTATELOW_CLOCK |
			       core_flags;
		bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
		udelay(10);
	}

	sbtmstatelow = BCM43xx_SBTMSTATELOW_RESET |
		       BCM43xx_SBTMSTATELOW_REJECT |
		       core_flags;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);

out:
	bcm->current_core->flags &= ~ BCM43xx_COREFLAG_ENABLED;
	return 0;
}

/* enable (reset) current core */
static int bcm43xx_core_enable(struct bcm43xx_private *bcm, u32 core_flags)
{
	u32 sbtmstatelow;
	u32 sbtmstatehigh;
	u32 sbimstate;
	int err;

	err = bcm43xx_core_disable(bcm, core_flags);
	if (err)
		goto out;

	sbtmstatelow = BCM43xx_SBTMSTATELOW_CLOCK |
		       BCM43xx_SBTMSTATELOW_RESET |
		       BCM43xx_SBTMSTATELOW_FORCE_GATE_CLOCK |
		       core_flags;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
	udelay(1);

	sbtmstatehigh = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATEHIGH);
	if (sbtmstatehigh & BCM43xx_SBTMSTATEHIGH_SERROR) {
		sbtmstatehigh = 0x00000000;
		bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATEHIGH, sbtmstatehigh);
	}

	sbimstate = bcm43xx_read32(bcm, BCM43xx_CIR_SBIMSTATE);
	if (sbimstate & (BCM43xx_SBIMSTATE_IB_ERROR | BCM43xx_SBIMSTATE_TIMEOUT)) {
		sbimstate &= ~(BCM43xx_SBIMSTATE_IB_ERROR | BCM43xx_SBIMSTATE_TIMEOUT);
		bcm43xx_write32(bcm, BCM43xx_CIR_SBIMSTATE, sbimstate);
	}

	sbtmstatelow = BCM43xx_SBTMSTATELOW_CLOCK |
		       BCM43xx_SBTMSTATELOW_FORCE_GATE_CLOCK |
		       core_flags;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
	udelay(1);

	sbtmstatelow = BCM43xx_SBTMSTATELOW_CLOCK | core_flags;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
	udelay(1);

	bcm->current_core->flags |= BCM43xx_COREFLAG_ENABLED;
	assert(err == 0);
out:
	return err;
}

/* http://bcm-specs.sipsolutions.net/80211CoreReset */
void bcm43xx_wireless_core_reset(struct bcm43xx_private *bcm, int connect_phy)
{
	u32 flags = 0x00040000;

	if ((bcm43xx_core_enabled(bcm)) && (!bcm->pio_mode)) {
//FIXME: Do we _really_ want #ifndef CONFIG_BCM947XX here?
#ifndef CONFIG_BCM947XX
		/* reset all used DMA controllers. */
		bcm43xx_dmacontroller_tx_reset(bcm, BCM43xx_MMIO_DMA1_BASE);
		bcm43xx_dmacontroller_tx_reset(bcm, BCM43xx_MMIO_DMA2_BASE);
		bcm43xx_dmacontroller_tx_reset(bcm, BCM43xx_MMIO_DMA3_BASE);
		bcm43xx_dmacontroller_tx_reset(bcm, BCM43xx_MMIO_DMA4_BASE);
		bcm43xx_dmacontroller_rx_reset(bcm, BCM43xx_MMIO_DMA1_BASE);
		if (bcm->current_core->rev < 5)
			bcm43xx_dmacontroller_rx_reset(bcm, BCM43xx_MMIO_DMA4_BASE);
#endif
	}
	if (bcm->shutting_down) {
		bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD,
		                bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD)
				& ~(BCM43xx_SBF_MAC_ENABLED | 0x00000002));
	} else {
		if (connect_phy)
			flags |= 0x20000000;
		bcm43xx_phy_connect(bcm, connect_phy);
		bcm43xx_core_enable(bcm, flags);
		bcm43xx_write16(bcm, 0x03E6, 0x0000);
		bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD,
				bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD)
				| BCM43xx_SBF_400);
	}
}

static void bcm43xx_wireless_core_disable(struct bcm43xx_private *bcm)
{
	bcm43xx_radio_turn_off(bcm);
	bcm43xx_write16(bcm, 0x03E6, 0x00F4);
	bcm43xx_core_disable(bcm, 0);
}

/* Mark the current 80211 core inactive.
 * "active_80211_core" is the other 80211 core, which is used.
 */
static int bcm43xx_wireless_core_mark_inactive(struct bcm43xx_private *bcm,
					       struct bcm43xx_coreinfo *active_80211_core)
{
	u32 sbtmstatelow;
	struct bcm43xx_coreinfo *old_core;
	int err = 0;

	bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);
	bcm43xx_radio_turn_off(bcm);
	sbtmstatelow = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
	sbtmstatelow &= ~0x200a0000;
	sbtmstatelow |= 0xa0000;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
	udelay(1);
	sbtmstatelow = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
	sbtmstatelow &= ~0xa0000;
	sbtmstatelow |= 0x80000;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
	udelay(1);

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G) {
		old_core = bcm->current_core;
		err = bcm43xx_switch_core(bcm, active_80211_core);
		if (err)
			goto out;
		sbtmstatelow = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
		sbtmstatelow &= ~0x20000000;
		sbtmstatelow |= 0x20000000;
		bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, sbtmstatelow);
		err = bcm43xx_switch_core(bcm, old_core);
	}

out:
	return err;
}

static inline void handle_irq_transmit_status(struct bcm43xx_private *bcm)
{
	u32 v0, v1;
	u16 tmp;
	struct bcm43xx_xmitstatus stat;

	assert(bcm->current_core->id == BCM43xx_COREID_80211);
	assert(bcm->current_core->rev >= 5);

	while (1) {
		v0 = bcm43xx_read32(bcm, BCM43xx_MMIO_XMITSTAT_0);
		if (!v0)
			break;
		v1 = bcm43xx_read32(bcm, BCM43xx_MMIO_XMITSTAT_1);

		stat.cookie = (v0 >> 16) & 0x0000FFFF;
		tmp = (u16)((v0 & 0xFFF0) | ((v0 & 0xF) >> 1));
		stat.flags = tmp & 0xFF;
		stat.cnt1 = (tmp & 0x0F00) >> 8;
		stat.cnt2 = (tmp & 0xF000) >> 12;
		stat.seq = (u16)(v1 & 0xFFFF);
		stat.unknown = (u16)((v1 >> 16) & 0xFF);

		bcm43xx_debugfs_log_txstat(bcm, &stat);

		if (stat.flags & BCM43xx_TXSTAT_FLAG_IGNORE)
			continue;
		if (!(stat.flags & BCM43xx_TXSTAT_FLAG_ACK)) {
			//TODO: packet was not acked (was lost)
		}
		//TODO: There are more (unknown) flags to test. see bcm43xx_main.h

		if (bcm->pio_mode)
			bcm43xx_pio_handle_xmitstatus(bcm, &stat);
		else
			bcm43xx_dma_handle_xmitstatus(bcm, &stat);
	}
}

static inline void bcm43xx_generate_noise_sample(struct bcm43xx_private *bcm)
{
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x408, 0x7F7F);
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x40A, 0x7F7F);
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD,
			bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD) | (1 << 4));
	assert(bcm->noisecalc.core_at_start == bcm->current_core);
	assert(bcm->noisecalc.channel_at_start == bcm->current_core->radio->channel);
}

static void bcm43xx_calculate_link_quality(struct bcm43xx_private *bcm)
{
	/* Top half of Link Quality calculation. */

	if (bcm->noisecalc.calculation_running)
		return;
	bcm->noisecalc.core_at_start = bcm->current_core;
	bcm->noisecalc.channel_at_start = bcm->current_core->radio->channel;
	bcm->noisecalc.calculation_running = 1;
	bcm->noisecalc.nr_samples = 0;

	bcm43xx_generate_noise_sample(bcm);
}

static inline void handle_irq_noise(struct bcm43xx_private *bcm)
{
	struct bcm43xx_radioinfo *radio = bcm->current_core->radio;
	u16 tmp;
	u8 noise[4];
	u8 i, j;
	s32 average;

	/* Bottom half of Link Quality calculation. */

	assert(bcm->noisecalc.calculation_running);
	if (bcm->noisecalc.core_at_start != bcm->current_core ||
	    bcm->noisecalc.channel_at_start != radio->channel)
		goto drop_calculation;
	tmp = bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED, 0x408);
	noise[0] = (tmp & 0x00FF);
	noise[1] = (tmp & 0xFF00) >> 8;
	tmp = bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED, 0x40A);
	noise[2] = (tmp & 0x00FF);
	noise[3] = (tmp & 0xFF00) >> 8;
	if (noise[0] == 0x7F || noise[1] == 0x7F ||
	    noise[2] == 0x7F || noise[3] == 0x7F)
		goto generate_new;

	/* Get the noise samples. */
	assert(bcm->noisecalc.nr_samples <= 8);
	i = bcm->noisecalc.nr_samples;
	noise[0] = limit_value(noise[0], 0, ARRAY_SIZE(radio->nrssi_lt) - 1);
	noise[1] = limit_value(noise[1], 0, ARRAY_SIZE(radio->nrssi_lt) - 1);
	noise[2] = limit_value(noise[2], 0, ARRAY_SIZE(radio->nrssi_lt) - 1);
	noise[3] = limit_value(noise[3], 0, ARRAY_SIZE(radio->nrssi_lt) - 1);
	bcm->noisecalc.samples[i][0] = radio->nrssi_lt[noise[0]];
	bcm->noisecalc.samples[i][1] = radio->nrssi_lt[noise[1]];
	bcm->noisecalc.samples[i][2] = radio->nrssi_lt[noise[2]];
	bcm->noisecalc.samples[i][3] = radio->nrssi_lt[noise[3]];
	bcm->noisecalc.nr_samples++;
	if (bcm->noisecalc.nr_samples == 8) {
		/* Calculate the Link Quality by the noise samples. */
		average = 0;
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 4; j++)
				average += bcm->noisecalc.samples[i][j];
		}
		average /= (8 * 4);
		average *= 125;
		average += 64;
		average /= 128;
		tmp = bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED, 0x40C);
		tmp = (tmp / 128) & 0x1F;
		if (tmp >= 8)
			average += 2;
		else
			average -= 25;
		if (tmp == 8)
			average -= 72;
		else
			average -= 48;

		if (average > -65)
			bcm->stats.link_quality = 0;
		else if (average > -75)
			bcm->stats.link_quality = 1;
		else if (average > -85)
			bcm->stats.link_quality = 2;
		else
			bcm->stats.link_quality = 3;
//		dprintk(KERN_INFO PFX "Link Quality: %u (avg was %d)\n", bcm->stats.link_quality, average);
drop_calculation:
		bcm->noisecalc.calculation_running = 0;
		return;
	}
generate_new:
	bcm43xx_generate_noise_sample(bcm);
}

static inline
void handle_irq_ps(struct bcm43xx_private *bcm)
{
	if (bcm->ieee->iw_mode == IW_MODE_MASTER) {
		///TODO: PS TBTT
	} else {
		if (1/*FIXME: the last PSpoll frame was sent successfully */)
			bcm43xx_power_saving_ctl_bits(bcm, -1, -1);
	}
	if (bcm->ieee->iw_mode == IW_MODE_ADHOC)
		bcm->reg124_set_0x4 = 1;
	//FIXME else set to false?
}

static inline
void handle_irq_reg124(struct bcm43xx_private *bcm)
{
	if (!bcm->reg124_set_0x4)
		return;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD,
			bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD)
			| 0x4);
	//FIXME: reset reg124_set_0x4 to false?
}

static inline
void handle_irq_pmq(struct bcm43xx_private *bcm)
{
	u32 tmp;

	//TODO: AP mode.

	while (1) {
		tmp = bcm43xx_read32(bcm, BCM43xx_MMIO_PS_STATUS);
		if (!(tmp & 0x00000008))
			break;
	}
	/* 16bit write is odd, but correct. */
	bcm43xx_write16(bcm, BCM43xx_MMIO_PS_STATUS, 0x0002);
}

static void bcm43xx_generate_beacon_template(struct bcm43xx_private *bcm,
					     u16 ram_offset, u16 shm_size_offset)
{
	u32 value;
	u16 size = 0;

	/* Timestamp. */
	//FIXME: assumption: The chip sets the timestamp
	value = 0;
	bcm43xx_ram_write(bcm, ram_offset++, value);
	bcm43xx_ram_write(bcm, ram_offset++, value);
	size += 8;

	/* Beacon Interval / Capability Information */
	value = 0x0000;//FIXME: Which interval?
	value |= (1 << 0) << 16; /* ESS */
	value |= (1 << 2) << 16; /* CF Pollable */	//FIXME?
	value |= (1 << 3) << 16; /* CF Poll Request */	//FIXME?
	if (!bcm->ieee->open_wep)
		value |= (1 << 4) << 16; /* Privacy */
	bcm43xx_ram_write(bcm, ram_offset++, value);
	size += 4;

	/* SSID */
	//TODO

	/* FH Parameter Set */
	//TODO

	/* DS Parameter Set */
	//TODO

	/* CF Parameter Set */
	//TODO

	/* TIM */
	//TODO

	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, shm_size_offset, size);
}

static inline
void handle_irq_beacon(struct bcm43xx_private *bcm)
{
	u32 status;

	bcm->irq_savedstate &= ~BCM43xx_IRQ_BEACON;
	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD);

	if ((status & 0x1) && (status & 0x2)) {
		/* ACK beacon IRQ. */
		bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON,
				BCM43xx_IRQ_BEACON);
		bcm->irq_savedstate |= BCM43xx_IRQ_BEACON;
		return;
	}
	if (!(status & 0x1)) {
		bcm43xx_generate_beacon_template(bcm, 0x68, 0x18);
		status |= 0x1;
		bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD, status);
	}
	if (!(status & 0x2)) {
		bcm43xx_generate_beacon_template(bcm, 0x468, 0x1A);
		status |= 0x2;
		bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS2_BITFIELD, status);
	}
}

/* Debug helper for irq bottom-half to print all reason registers. */
#define bcmirq_print_reasons(description) \
	do {											\
		dprintkl(KERN_ERR PFX description "\n"						\
			 KERN_ERR PFX "  Generic Reason: 0x%08x\n"				\
			 KERN_ERR PFX "  DMA reasons:    0x%08x, 0x%08x, 0x%08x, 0x%08x\n"	\
			 KERN_ERR PFX "  DMA TX status:  0x%08x, 0x%08x, 0x%08x, 0x%08x\n",	\
			 reason,								\
			 dma_reason[0], dma_reason[1],						\
			 dma_reason[2], dma_reason[3],						\
			 bcm43xx_read32(bcm, BCM43xx_MMIO_DMA1_BASE + BCM43xx_DMA_TX_STATUS),	\
			 bcm43xx_read32(bcm, BCM43xx_MMIO_DMA2_BASE + BCM43xx_DMA_TX_STATUS),	\
			 bcm43xx_read32(bcm, BCM43xx_MMIO_DMA3_BASE + BCM43xx_DMA_TX_STATUS),	\
			 bcm43xx_read32(bcm, BCM43xx_MMIO_DMA4_BASE + BCM43xx_DMA_TX_STATUS));	\
	} while (0)

/* Interrupt handler bottom-half */
static void bcm43xx_interrupt_tasklet(struct bcm43xx_private *bcm)
{
	u32 reason;
	u32 dma_reason[4];
	int activity = 0;
	unsigned long flags;

#ifdef CONFIG_BCM43XX_DEBUG
	u32 _handled = 0x00000000;
# define bcmirq_handled(irq)	do { _handled |= (irq); } while (0)
#else
# define bcmirq_handled(irq)	do { /* nothing */ } while (0)
#endif /* CONFIG_BCM43XX_DEBUG*/

	spin_lock_irqsave(&bcm->lock, flags);
	reason = bcm->irq_reason;
	dma_reason[0] = bcm->dma_reason[0];
	dma_reason[1] = bcm->dma_reason[1];
	dma_reason[2] = bcm->dma_reason[2];
	dma_reason[3] = bcm->dma_reason[3];

	if (unlikely(reason & BCM43xx_IRQ_XMIT_ERROR)) {
		/* TX error. We get this when Template Ram is written in wrong endianess
		 * in dummy_tx(). We also get this if something is wrong with the TX header
		 * on DMA or PIO queues.
		 * Maybe we get this in other error conditions, too.
		 */
		bcmirq_print_reasons("XMIT ERROR");
		bcmirq_handled(BCM43xx_IRQ_XMIT_ERROR);
	}

	if (reason & BCM43xx_IRQ_PS) {
		handle_irq_ps(bcm);
		bcmirq_handled(BCM43xx_IRQ_PS);
	}

	if (reason & BCM43xx_IRQ_REG124) {
		handle_irq_reg124(bcm);
		bcmirq_handled(BCM43xx_IRQ_REG124);
	}

	if (reason & BCM43xx_IRQ_BEACON) {
		if (bcm->ieee->iw_mode == IW_MODE_MASTER)
			handle_irq_beacon(bcm);
		bcmirq_handled(BCM43xx_IRQ_BEACON);
	}

	if (reason & BCM43xx_IRQ_PMQ) {
		handle_irq_pmq(bcm);
		bcmirq_handled(BCM43xx_IRQ_PMQ);
	}

	if (reason & BCM43xx_IRQ_SCAN) {
		/*TODO*/
		//bcmirq_handled(BCM43xx_IRQ_SCAN);
	}

	if (reason & BCM43xx_IRQ_NOISE) {
		handle_irq_noise(bcm);
		bcmirq_handled(BCM43xx_IRQ_NOISE);
	}

	/* Check the DMA reason registers for received data. */
	assert(!(dma_reason[1] & BCM43xx_DMAIRQ_RX_DONE));
	assert(!(dma_reason[2] & BCM43xx_DMAIRQ_RX_DONE));
	if (dma_reason[0] & BCM43xx_DMAIRQ_RX_DONE) {
		if (bcm->pio_mode)
			bcm43xx_pio_rx(bcm->current_core->pio->queue0);
		else
			bcm43xx_dma_rx(bcm->current_core->dma->rx_ring0);
		activity = 1;
	}
	if (dma_reason[3] & BCM43xx_DMAIRQ_RX_DONE) {
		if (likely(bcm->current_core->rev < 5)) {
			if (bcm->pio_mode)
				bcm43xx_pio_rx(bcm->current_core->pio->queue3);
			else
				bcm43xx_dma_rx(bcm->current_core->dma->rx_ring1);
			activity = 1;
		} else
			assert(0);
	}
	bcmirq_handled(BCM43xx_IRQ_RX);

	if (reason & BCM43xx_IRQ_XMIT_STATUS) {
		if (bcm->current_core->rev >= 5) {
			handle_irq_transmit_status(bcm);
			activity = 1;
		}
		//TODO: In AP mode, this also causes sending of powersave responses.
		bcmirq_handled(BCM43xx_IRQ_XMIT_STATUS);
	}

	/* We get spurious IRQs, althought they are masked.
	 * Assume they are void and ignore them.
	 */
	bcmirq_handled(~(bcm->irq_savedstate));
	/* IRQ_PIO_WORKAROUND is handled in the top-half. */
	bcmirq_handled(BCM43xx_IRQ_PIO_WORKAROUND);
#ifdef CONFIG_BCM43XX_DEBUG
	if (unlikely(reason & ~_handled)) {
		printkl(KERN_WARNING PFX
			"Unhandled IRQ! Reason: 0x%08x,  Unhandled: 0x%08x,  "
			"DMA: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			reason, (reason & ~_handled),
			dma_reason[0], dma_reason[1],
			dma_reason[2], dma_reason[3]);
	}
#endif
#undef bcmirq_handled

	if (!modparam_noleds)
		bcm43xx_leds_update(bcm, activity);
	bcm43xx_interrupt_enable(bcm, bcm->irq_savedstate);
	spin_unlock_irqrestore(&bcm->lock, flags);
}

#undef bcmirq_print_reasons

static inline
void bcm43xx_interrupt_ack(struct bcm43xx_private *bcm,
			   u32 reason, u32 mask)
{
	bcm->dma_reason[0] = bcm43xx_read32(bcm, BCM43xx_MMIO_DMA1_REASON)
			     & 0x0001dc00;
	bcm->dma_reason[1] = bcm43xx_read32(bcm, BCM43xx_MMIO_DMA2_REASON)
			     & 0x0000dc00;
	bcm->dma_reason[2] = bcm43xx_read32(bcm, BCM43xx_MMIO_DMA3_REASON)
			     & 0x0000dc00;
	bcm->dma_reason[3] = bcm43xx_read32(bcm, BCM43xx_MMIO_DMA4_REASON)
			     & 0x0001dc00;

	if ((bcm->pio_mode) &&
	    (bcm->current_core->rev < 3) &&
	    (!(reason & BCM43xx_IRQ_PIO_WORKAROUND))) {
		/* Apply a PIO specific workaround to the dma_reasons */

#define apply_pio_workaround(BASE, QNUM) \
	do {											\
	if (bcm43xx_read16(bcm, BASE + BCM43xx_PIO_RXCTL) & BCM43xx_PIO_RXCTL_DATAAVAILABLE)	\
		bcm->dma_reason[QNUM] |= 0x00010000;						\
	else											\
		bcm->dma_reason[QNUM] &= ~0x00010000;						\
	} while (0)

		apply_pio_workaround(BCM43xx_MMIO_PIO1_BASE, 0);
		apply_pio_workaround(BCM43xx_MMIO_PIO2_BASE, 1);
		apply_pio_workaround(BCM43xx_MMIO_PIO3_BASE, 2);
		apply_pio_workaround(BCM43xx_MMIO_PIO4_BASE, 3);

#undef apply_pio_workaround
	}

	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON,
			reason & mask);

	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA1_REASON,
			bcm->dma_reason[0]);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA2_REASON,
			bcm->dma_reason[1]);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA3_REASON,
			bcm->dma_reason[2]);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA4_REASON,
			bcm->dma_reason[3]);
}

/* Interrupt handler top-half */
static irqreturn_t bcm43xx_interrupt_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct bcm43xx_private *bcm = dev_id;
	u32 reason, mask;

	if (!bcm)
		return IRQ_NONE;

	spin_lock(&bcm->lock);

	reason = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON);
	if (reason == 0xffffffff) {
		/* irq not for us (shared irq) */
		spin_unlock(&bcm->lock);
		return IRQ_NONE;
	}
	mask = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_MASK);
	if (!(reason & mask)) {
		spin_unlock(&bcm->lock);
		return IRQ_HANDLED;
	}

	bcm43xx_interrupt_ack(bcm, reason, mask);

	/* disable all IRQs. They are enabled again in the bottom half. */
	bcm->irq_savedstate = bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);

	/* save the reason code and call our bottom half. */
	bcm->irq_reason = reason;
	tasklet_schedule(&bcm->isr_tasklet);

	spin_unlock(&bcm->lock);

	return IRQ_HANDLED;
}

static void bcm43xx_release_firmware(struct bcm43xx_private *bcm, int force)
{
	if (bcm->firmware_norelease && !force)
		return; /* Suspending or controller reset. */
	release_firmware(bcm->ucode);
	bcm->ucode = NULL;
	release_firmware(bcm->pcm);
	bcm->pcm = NULL;
	release_firmware(bcm->initvals0);
	bcm->initvals0 = NULL;
	release_firmware(bcm->initvals1);
	bcm->initvals1 = NULL;
}

static int bcm43xx_request_firmware(struct bcm43xx_private *bcm)
{
	struct bcm43xx_phyinfo *phy = bcm->current_core->phy;
	u8 rev = bcm->current_core->rev;
	int err = 0;
	int nr;
	char buf[22 + sizeof(modparam_fwpostfix) - 1] = { 0 };

	if (!bcm->ucode) {
		snprintf(buf, ARRAY_SIZE(buf), "bcm43xx_microcode%d%s.fw",
			 (rev >= 5 ? 5 : rev),
			 modparam_fwpostfix);
		err = request_firmware(&bcm->ucode, buf, &bcm->pci_dev->dev);
		if (err) {
			printk(KERN_ERR PFX 
			       "Error: Microcode \"%s\" not available or load failed.\n",
			        buf);
			goto error;
		}
	}

	if (!bcm->pcm) {
		snprintf(buf, ARRAY_SIZE(buf),
			 "bcm43xx_pcm%d%s.fw",
			 (rev < 5 ? 4 : 5),
			 modparam_fwpostfix);
		err = request_firmware(&bcm->pcm, buf, &bcm->pci_dev->dev);
		if (err) {
			printk(KERN_ERR PFX
			       "Error: PCM \"%s\" not available or load failed.\n",
			       buf);
			goto error;
		}
	}

	if (!bcm->initvals0) {
		if (rev == 2 || rev == 4) {
			switch (phy->type) {
			case BCM43xx_PHYTYPE_A:
				nr = 3;
				break;
			case BCM43xx_PHYTYPE_B:
			case BCM43xx_PHYTYPE_G:
				nr = 1;
				break;
			default:
				goto err_noinitval;
			}
		
		} else if (rev >= 5) {
			switch (phy->type) {
			case BCM43xx_PHYTYPE_A:
				nr = 7;
				break;
			case BCM43xx_PHYTYPE_B:
			case BCM43xx_PHYTYPE_G:
				nr = 5;
				break;
			default:
				goto err_noinitval;
			}
		} else
			goto err_noinitval;
		snprintf(buf, ARRAY_SIZE(buf), "bcm43xx_initval%02d%s.fw",
			 nr, modparam_fwpostfix);

		err = request_firmware(&bcm->initvals0, buf, &bcm->pci_dev->dev);
		if (err) {
			printk(KERN_ERR PFX 
			       "Error: InitVals \"%s\" not available or load failed.\n",
			        buf);
			goto error;
		}
		if (bcm->initvals0->size % sizeof(struct bcm43xx_initval)) {
			printk(KERN_ERR PFX "InitVals fileformat error.\n");
			goto error;
		}
	}

	if (!bcm->initvals1) {
		if (rev >= 5) {
			u32 sbtmstatehigh;

			switch (phy->type) {
			case BCM43xx_PHYTYPE_A:
				sbtmstatehigh = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATEHIGH);
				if (sbtmstatehigh & 0x00010000)
					nr = 9;
				else
					nr = 10;
				break;
			case BCM43xx_PHYTYPE_B:
			case BCM43xx_PHYTYPE_G:
					nr = 6;
				break;
			default:
				goto err_noinitval;
			}
			snprintf(buf, ARRAY_SIZE(buf), "bcm43xx_initval%02d%s.fw",
				 nr, modparam_fwpostfix);

			err = request_firmware(&bcm->initvals1, buf, &bcm->pci_dev->dev);
			if (err) {
				printk(KERN_ERR PFX 
				       "Error: InitVals \"%s\" not available or load failed.\n",
			        	buf);
				goto error;
			}
			if (bcm->initvals1->size % sizeof(struct bcm43xx_initval)) {
				printk(KERN_ERR PFX "InitVals fileformat error.\n");
				goto error;
			}
		}
	}

out:
	return err;
error:
	bcm43xx_release_firmware(bcm, 1);
	goto out;
err_noinitval:
	printk(KERN_ERR PFX "Error: No InitVals available!\n");
	err = -ENOENT;
	goto error;
}

static void bcm43xx_upload_microcode(struct bcm43xx_private *bcm)
{
	const u32 *data;
	unsigned int i, len;

#ifdef DEBUG_ENABLE_UCODE_MMIO_PRINT
	bcm43xx_mmioprint_enable(bcm);
#else
	bcm43xx_mmioprint_disable(bcm);
#endif

	/* Upload Microcode. */
	data = (u32 *)(bcm->ucode->data);
	len = bcm->ucode->size / sizeof(u32);
	bcm43xx_shm_control_word(bcm, BCM43xx_SHM_UCODE, 0x0000);
	for (i = 0; i < len; i++) {
		bcm43xx_write32(bcm, BCM43xx_MMIO_SHM_DATA,
				be32_to_cpu(data[i]));
		udelay(10);
	}

	/* Upload PCM data. */
	data = (u32 *)(bcm->pcm->data);
	len = bcm->pcm->size / sizeof(u32);
	bcm43xx_shm_control_word(bcm, BCM43xx_SHM_PCM, 0x01ea);
	bcm43xx_write32(bcm, BCM43xx_MMIO_SHM_DATA, 0x00004000);
	bcm43xx_shm_control_word(bcm, BCM43xx_SHM_PCM, 0x01eb);
	for (i = 0; i < len; i++) {
		bcm43xx_write32(bcm, BCM43xx_MMIO_SHM_DATA,
				be32_to_cpu(data[i]));
		udelay(10);
	}

#ifdef DEBUG_ENABLE_UCODE_MMIO_PRINT
	bcm43xx_mmioprint_disable(bcm);
#else
	bcm43xx_mmioprint_enable(bcm);
#endif
}

static int bcm43xx_write_initvals(struct bcm43xx_private *bcm,
				  const struct bcm43xx_initval *data,
				  const unsigned int len)
{
	u16 offset, size;
	u32 value;
	unsigned int i;

	for (i = 0; i < len; i++) {
		offset = be16_to_cpu(data[i].offset);
		size = be16_to_cpu(data[i].size);
		value = be32_to_cpu(data[i].value);

		if (unlikely(offset >= 0x1000))
			goto err_format;
		if (size == 2) {
			if (unlikely(value & 0xFFFF0000))
				goto err_format;
			bcm43xx_write16(bcm, offset, (u16)value);
		} else if (size == 4) {
			bcm43xx_write32(bcm, offset, value);
		} else
			goto err_format;
	}

	return 0;

err_format:
	printk(KERN_ERR PFX "InitVals (bcm43xx_initvalXX.fw) file-format error. "
			    "Please fix your bcm43xx firmware files.\n");
	return -EPROTO;
}

static int bcm43xx_upload_initvals(struct bcm43xx_private *bcm)
{
	int err;

#ifdef DEBUG_ENABLE_UCODE_MMIO_PRINT
	bcm43xx_mmioprint_enable(bcm);
#else
	bcm43xx_mmioprint_disable(bcm);
#endif

	err = bcm43xx_write_initvals(bcm, (struct bcm43xx_initval *)bcm->initvals0->data,
				     bcm->initvals0->size / sizeof(struct bcm43xx_initval));
	if (err)
		goto out;
	if (bcm->initvals1) {
		err = bcm43xx_write_initvals(bcm, (struct bcm43xx_initval *)bcm->initvals1->data,
					     bcm->initvals1->size / sizeof(struct bcm43xx_initval));
		if (err)
			goto out;
	}

out:
#ifdef DEBUG_ENABLE_UCODE_MMIO_PRINT
	bcm43xx_mmioprint_disable(bcm);
#else
	bcm43xx_mmioprint_enable(bcm);
#endif
	return err;
}

static int bcm43xx_initialize_irq(struct bcm43xx_private *bcm)
{
	int res;
	unsigned int i;
	u32 data;

	bcm->irq = bcm->pci_dev->irq;
#ifdef CONFIG_BCM947XX
	if (bcm->pci_dev->bus->number == 0) {
		struct pci_dev *d = NULL;
		/* FIXME: we will probably need more device IDs here... */
		d = pci_find_device(PCI_VENDOR_ID_BROADCOM, 0x4324, NULL);
		if (d != NULL) {
			bcm->irq = d->irq;
		}
	}
#endif
	res = request_irq(bcm->irq, bcm43xx_interrupt_handler,
			  SA_SHIRQ, KBUILD_MODNAME, bcm);
	if (res) {
		printk(KERN_ERR PFX "Cannot register IRQ%d\n", bcm->irq);
		return -EFAULT;
	}
	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON, 0xffffffff);
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, 0x00020402);
	i = 0;
	while (1) {
		data = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON);
		if (data == BCM43xx_IRQ_READY)
			break;
		i++;
		if (i >= BCM43xx_IRQWAIT_MAX_RETRIES) {
			printk(KERN_ERR PFX "Card IRQ register not responding. "
					    "Giving up.\n");
			free_irq(bcm->irq, bcm);
			return -ENODEV;
		}
		udelay(10);
	}
	// dummy read
	bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON);

	return 0;
}

/* Switch to the core used to write the GPIO register.
 * This is either the ChipCommon, or the PCI core.
 */
static inline int switch_to_gpio_core(struct bcm43xx_private *bcm)
{
	int err;

	/* Where to find the GPIO register depends on the chipset.
	 * If it has a ChipCommon, its register at offset 0x6c is the GPIO
	 * control register. Otherwise the register at offset 0x6c in the
	 * PCI core is the GPIO control register.
	 */
	err = bcm43xx_switch_core(bcm, &bcm->core_chipcommon);
	if (err == -ENODEV) {
		err = bcm43xx_switch_core(bcm, &bcm->core_pci);
		if (err == -ENODEV) {
			printk(KERN_ERR PFX "gpio error: "
			       "Neither ChipCommon nor PCI core available!\n");
			return -ENODEV;
		} else if (err != 0)
			return -ENODEV;
	} else if (err != 0)
		return -ENODEV;

	return 0;
}

/* Initialize the GPIOs
 * http://bcm-specs.sipsolutions.net/GPIO
 */
static int bcm43xx_gpio_init(struct bcm43xx_private *bcm)
{
	struct bcm43xx_coreinfo *old_core;
	int err;
	u32 mask, value;

	value = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value &= ~0xc000;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value);

	mask = 0x0000001F;
	value = 0x0000000F;
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_CONTROL,
			bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_CONTROL) & 0xFFF0);
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_MASK,
			bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_MASK) | 0x000F);

	old_core = bcm->current_core;
	
	err = switch_to_gpio_core(bcm);
	if (err)
		return err;

	if (bcm->current_core->rev >= 2){
		mask  |= 0x10;
		value |= 0x10;
	}
	if (bcm->chip_id == 0x4301) {
		mask  |= 0x60;
		value |= 0x60;
	}
	if (bcm->sprom.boardflags & BCM43xx_BFL_PACTRL) {
		mask  |= 0x200;
		value |= 0x200;
	}

	bcm43xx_write32(bcm, BCM43xx_GPIO_CONTROL,
	                (bcm43xx_read32(bcm, BCM43xx_GPIO_CONTROL) & mask) | value);

	err = bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);

	return 0;
}

/* Turn off all GPIO stuff. Call this on module unload, for example. */
static int bcm43xx_gpio_cleanup(struct bcm43xx_private *bcm)
{
	struct bcm43xx_coreinfo *old_core;
	int err;

	old_core = bcm->current_core;
	err = switch_to_gpio_core(bcm);
	if (err)
		return err;
	bcm43xx_write32(bcm, BCM43xx_GPIO_CONTROL, 0x00000000);
	err = bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);

	return 0;
}

/* http://bcm-specs.sipsolutions.net/EnableMac */
void bcm43xx_mac_enable(struct bcm43xx_private *bcm)
{
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD,
	                bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD)
			| BCM43xx_SBF_MAC_ENABLED);
	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON, BCM43xx_IRQ_READY);
	bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD); /* dummy read */
	bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON); /* dummy read */
	bcm43xx_power_saving_ctl_bits(bcm, -1, -1);
}

/* http://bcm-specs.sipsolutions.net/SuspendMAC */
void bcm43xx_mac_suspend(struct bcm43xx_private *bcm)
{
	int i;
	u32 tmp;

	bcm43xx_power_saving_ctl_bits(bcm, -1, 1);
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD,
	                bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD)
			& ~BCM43xx_SBF_MAC_ENABLED);
	bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON); /* dummy read */
	for (i = 1000; i > 0; i--) {
		tmp = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON);
		if (tmp & BCM43xx_IRQ_READY) {
			i = -1;
			break;
		}
		udelay(10);
	}
	if (!i)
		printkl(KERN_ERR PFX "Failed to suspend mac!\n");
}

void bcm43xx_set_iwmode(struct bcm43xx_private *bcm,
			int iw_mode)
{
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&bcm->ieee->lock, flags);
	bcm->ieee->iw_mode = iw_mode;
	spin_unlock_irqrestore(&bcm->ieee->lock, flags);
	if (iw_mode == IW_MODE_MONITOR)
		bcm->net_dev->type = ARPHRD_IEEE80211;
	else
		bcm->net_dev->type = ARPHRD_ETHER;

	if (!bcm->initialized)
		return;

	bcm43xx_mac_suspend(bcm);
	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	/* Reset status to infrastructured mode */
	status &= ~(BCM43xx_SBF_MODE_AP | BCM43xx_SBF_MODE_MONITOR);
	/*FIXME: We actually set promiscuous mode as well, until we don't
	 * get the HW mac filter working */
	status |= BCM43xx_SBF_MODE_NOTADHOC | BCM43xx_SBF_MODE_PROMISC;

	switch (iw_mode) {
	case IW_MODE_MONITOR:
		status |= (BCM43xx_SBF_MODE_PROMISC |
			   BCM43xx_SBF_MODE_MONITOR);
		break;
	case IW_MODE_ADHOC:
		status &= ~BCM43xx_SBF_MODE_NOTADHOC;
		break;
	case IW_MODE_MASTER:
	case IW_MODE_SECOND:
	case IW_MODE_REPEAT:
		/* TODO: No AP/Repeater mode for now :-/ */
		TODO();
		break;
	case IW_MODE_INFRA:
		/* nothing to be done here... */
		break;
	default:
		printk(KERN_ERR PFX "Unknown iwmode %d\n", iw_mode);
	}

	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, status);
	bcm43xx_mac_enable(bcm);
}

/* This is the opposite of bcm43xx_chip_init() */
static void bcm43xx_chip_cleanup(struct bcm43xx_private *bcm)
{
	bcm43xx_radio_turn_off(bcm);
	if (!modparam_noleds)
		bcm43xx_leds_exit(bcm);
	bcm43xx_gpio_cleanup(bcm);
	free_irq(bcm->irq, bcm);
	bcm43xx_release_firmware(bcm, 0);
}

/* Initialize the chip
 * http://bcm-specs.sipsolutions.net/ChipInit
 */
static int bcm43xx_chip_init(struct bcm43xx_private *bcm)
{
	int err;
	int iw_mode = bcm->ieee->iw_mode;
	int tmp;
	u32 value32;
	u16 value16;

	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD,
			BCM43xx_SBF_CORE_READY
			| BCM43xx_SBF_400);

	err = bcm43xx_request_firmware(bcm);
	if (err)
		goto out;
	bcm43xx_upload_microcode(bcm);

	err = bcm43xx_initialize_irq(bcm);
	if (err)
		goto err_release_fw;

	err = bcm43xx_gpio_init(bcm);
	if (err)
		goto err_free_irq;

	err = bcm43xx_upload_initvals(bcm);
	if (err)
		goto err_gpio_cleanup;
	bcm43xx_radio_turn_on(bcm);

	if (modparam_noleds)
		bcm43xx_leds_turn_off(bcm);
	else
		bcm43xx_leds_update(bcm, 0);

	bcm43xx_write16(bcm, 0x03E6, 0x0000);
	err = bcm43xx_phy_init(bcm);
	if (err)
		goto err_radio_off;

	/* Select initial Interference Mitigation. */
	tmp = bcm->current_core->radio->interfmode;
	bcm->current_core->radio->interfmode = BCM43xx_RADIO_INTERFMODE_NONE;
	bcm43xx_radio_set_interference_mitigation(bcm, tmp);

	bcm43xx_phy_set_antenna_diversity(bcm);
	bcm43xx_radio_set_txantenna(bcm, BCM43xx_RADIO_TXANTENNA_DEFAULT);
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_B) {
		value16 = bcm43xx_read16(bcm, 0x005E);
		value16 |= 0x0004;
		bcm43xx_write16(bcm, 0x005E, value16);
	}
	bcm43xx_write32(bcm, 0x0100, 0x01000000);
	if (bcm->current_core->rev < 5)
		bcm43xx_write32(bcm, 0x010C, 0x01000000);

	value32 = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value32 &= ~ BCM43xx_SBF_MODE_NOTADHOC;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value32);
	value32 = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value32 |= BCM43xx_SBF_MODE_NOTADHOC;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value32);
	/*FIXME: For now, use promiscuous mode at all times; otherwise we don't
	   get broadcast or multicast packets */
	value32 = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value32 |= BCM43xx_SBF_MODE_PROMISC;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value32);

	if (iw_mode == IW_MODE_MONITOR) {
		value32 = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
		value32 |= BCM43xx_SBF_MODE_PROMISC;
		value32 |= BCM43xx_SBF_MODE_MONITOR;
		bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value32);
	}
	value32 = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value32 |= 0x100000; //FIXME: What's this? Is this correct?
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value32);

	if (bcm->pio_mode) {
		bcm43xx_write32(bcm, 0x0210, 0x00000100);
		bcm43xx_write32(bcm, 0x0230, 0x00000100);
		bcm43xx_write32(bcm, 0x0250, 0x00000100);
		bcm43xx_write32(bcm, 0x0270, 0x00000100);
		bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0034, 0x0000);
	}

	/* Probe Response Timeout value */
	/* FIXME: Default to 0, has to be set by ioctl probably... :-/ */
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0074, 0x0000);

	if (iw_mode != IW_MODE_ADHOC && iw_mode != IW_MODE_MASTER) {
		if ((bcm->chip_id == 0x4306) && (bcm->chip_rev == 3))
			bcm43xx_write16(bcm, 0x0612, 0x0064);
		else
			bcm43xx_write16(bcm, 0x0612, 0x0032);
	} else
		bcm43xx_write16(bcm, 0x0612, 0x0002);

	if (bcm->current_core->rev < 3) {
		bcm43xx_write16(bcm, 0x060E, 0x0000);
		bcm43xx_write16(bcm, 0x0610, 0x8000);
		bcm43xx_write16(bcm, 0x0604, 0x0000);
		bcm43xx_write16(bcm, 0x0606, 0x0200);
	} else {
		bcm43xx_write32(bcm, 0x0188, 0x80000000);
		bcm43xx_write32(bcm, 0x018C, 0x02000000);
	}
	bcm43xx_write32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON, 0x00004000);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA1_IRQ_MASK, 0x0001DC00);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA2_IRQ_MASK, 0x0000DC00);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA3_IRQ_MASK, 0x0000DC00);
	bcm43xx_write32(bcm, BCM43xx_MMIO_DMA4_IRQ_MASK, 0x0001DC00);

	value32 = bcm43xx_read32(bcm, BCM43xx_CIR_SBTMSTATELOW);
	value32 |= 0x00100000;
	bcm43xx_write32(bcm, BCM43xx_CIR_SBTMSTATELOW, value32);

	bcm43xx_write16(bcm, BCM43xx_MMIO_POWERUP_DELAY, bcm43xx_pctl_powerup_delay(bcm));

	assert(err == 0);
	dprintk(KERN_INFO PFX "Chip initialized\n");
out:
	return err;

err_radio_off:
	bcm43xx_radio_turn_off(bcm);
err_gpio_cleanup:
	bcm43xx_gpio_cleanup(bcm);
err_free_irq:
	free_irq(bcm->irq, bcm);
err_release_fw:
	bcm43xx_release_firmware(bcm, 1);
	goto out;
}
	
/* Validate chip access
 * http://bcm-specs.sipsolutions.net/ValidateChipAccess */
static int bcm43xx_validate_chip(struct bcm43xx_private *bcm)
{
	int err = -ENODEV;
	u32 value;
	u32 shm_backup;

	shm_backup = bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED, 0x0000);
	bcm43xx_shm_write32(bcm, BCM43xx_SHM_SHARED, 0x0000, 0xAA5555AA);
	if (bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED, 0x0000) != 0xAA5555AA) {
		printk(KERN_ERR PFX "Error: SHM mismatch (1) validating chip\n");
		goto out;
	}

	bcm43xx_shm_write32(bcm, BCM43xx_SHM_SHARED, 0x0000, 0x55AAAA55);
	if (bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED, 0x0000) != 0x55AAAA55) {
		printk(KERN_ERR PFX "Error: SHM mismatch (2) validating chip\n");
		goto out;
	}

	bcm43xx_shm_write32(bcm, BCM43xx_SHM_SHARED, 0x0000, shm_backup);

	value = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	if ((value | 0x80000000) != 0x80000400) {
		printk(KERN_ERR PFX "Error: Bad Status Bitfield while validating chip\n");
		goto out;
	}

	value = bcm43xx_read32(bcm, BCM43xx_MMIO_GEN_IRQ_REASON);
	if (value != 0x00000000) {
		printk(KERN_ERR PFX "Error: Bad interrupt reason code while validating chip\n");
		goto out;
	}

	err = 0;
out:
	return err;
}

static int bcm43xx_probe_cores(struct bcm43xx_private *bcm)
{
	int err, i;
	int current_core;
	u32 core_vendor, core_id, core_rev;
	u32 sb_id_hi, chip_id_32 = 0;
	u16 pci_device, chip_id_16;
	u8 core_count;

	memset(&bcm->core_chipcommon, 0, sizeof(struct bcm43xx_coreinfo));
	memset(&bcm->core_pci, 0, sizeof(struct bcm43xx_coreinfo));
	memset(&bcm->core_v90, 0, sizeof(struct bcm43xx_coreinfo));
	memset(&bcm->core_pcmcia, 0, sizeof(struct bcm43xx_coreinfo));
	memset(&bcm->core_80211, 0, sizeof(struct bcm43xx_coreinfo)
				    * BCM43xx_MAX_80211_CORES);

	memset(&bcm->phy, 0, sizeof(struct bcm43xx_phyinfo)
			     * BCM43xx_MAX_80211_CORES);
	memset(&bcm->radio, 0, sizeof(struct bcm43xx_radioinfo)
			       * BCM43xx_MAX_80211_CORES);

	/* map core 0 */
	err = _switch_core(bcm, 0);
	if (err)
		goto out;

	/* fetch sb_id_hi from core information registers */
	sb_id_hi = bcm43xx_read32(bcm, BCM43xx_CIR_SB_ID_HI);

	core_id = (sb_id_hi & 0xFFF0) >> 4;
	core_rev = (sb_id_hi & 0xF);
	core_vendor = (sb_id_hi & 0xFFFF0000) >> 16;

	/* if present, chipcommon is always core 0; read the chipid from it */
	if (core_id == BCM43xx_COREID_CHIPCOMMON) {
		chip_id_32 = bcm43xx_read32(bcm, 0);
		chip_id_16 = chip_id_32 & 0xFFFF;
		bcm->core_chipcommon.flags |= BCM43xx_COREFLAG_AVAILABLE;
		bcm->core_chipcommon.id = core_id;
		bcm->core_chipcommon.rev = core_rev;
		bcm->core_chipcommon.index = 0;
		/* While we are at it, also read the capabilities. */
		bcm->chipcommon_capabilities = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_CAPABILITIES);
	} else {
		/* without a chipCommon, use a hard coded table. */
		pci_device = bcm->pci_dev->device;
		if (pci_device == 0x4301)
			chip_id_16 = 0x4301;
		else if ((pci_device >= 0x4305) && (pci_device <= 0x4307))
			chip_id_16 = 0x4307;
		else if ((pci_device >= 0x4402) && (pci_device <= 0x4403))
			chip_id_16 = 0x4402;
		else if ((pci_device >= 0x4610) && (pci_device <= 0x4615))
			chip_id_16 = 0x4610;
		else if ((pci_device >= 0x4710) && (pci_device <= 0x4715))
			chip_id_16 = 0x4710;
#ifdef CONFIG_BCM947XX
		else if ((pci_device >= 0x4320) && (pci_device <= 0x4325))
			chip_id_16 = 0x4309;
#endif
		else {
			printk(KERN_ERR PFX "Could not determine Chip ID\n");
			return -ENODEV;
		}
	}

	/* ChipCommon with Core Rev >=4 encodes number of cores,
	 * otherwise consult hardcoded table */
	if ((core_id == BCM43xx_COREID_CHIPCOMMON) && (core_rev >= 4)) {
		core_count = (chip_id_32 & 0x0F000000) >> 24;
	} else {
		switch (chip_id_16) {
			case 0x4610:
			case 0x4704:
			case 0x4710:
				core_count = 9;
				break;
			case 0x4310:
				core_count = 8;
				break;
			case 0x5365:
				core_count = 7;
				break;
			case 0x4306:
				core_count = 6;
				break;
			case 0x4301:
			case 0x4307:
				core_count = 5;
				break;
			case 0x4402:
				core_count = 3;
				break;
			default:
				/* SOL if we get here */
				assert(0);
				core_count = 1;
		}
	}

	bcm->chip_id = chip_id_16;
	bcm->chip_rev = (chip_id_32 & 0x000f0000) >> 16;

	dprintk(KERN_INFO PFX "Chip ID 0x%x, rev 0x%x\n",
		bcm->chip_id, bcm->chip_rev);
	dprintk(KERN_INFO PFX "Number of cores: %d\n", core_count);
	if (bcm->core_chipcommon.flags & BCM43xx_COREFLAG_AVAILABLE) {
		dprintk(KERN_INFO PFX "Core 0: ID 0x%x, rev 0x%x, vendor 0x%x, %s\n",
			core_id, core_rev, core_vendor,
			bcm43xx_core_enabled(bcm) ? "enabled" : "disabled");
	}

	if (bcm->core_chipcommon.flags & BCM43xx_COREFLAG_AVAILABLE)
		current_core = 1;
	else
		current_core = 0;
	for ( ; current_core < core_count; current_core++) {
		struct bcm43xx_coreinfo *core;

		err = _switch_core(bcm, current_core);
		if (err)
			goto out;
		/* Gather information */
		/* fetch sb_id_hi from core information registers */
		sb_id_hi = bcm43xx_read32(bcm, BCM43xx_CIR_SB_ID_HI);

		/* extract core_id, core_rev, core_vendor */
		core_id = (sb_id_hi & 0xFFF0) >> 4;
		core_rev = (sb_id_hi & 0xF);
		core_vendor = (sb_id_hi & 0xFFFF0000) >> 16;

		dprintk(KERN_INFO PFX "Core %d: ID 0x%x, rev 0x%x, vendor 0x%x, %s\n",
			current_core, core_id, core_rev, core_vendor,
			bcm43xx_core_enabled(bcm) ? "enabled" : "disabled" );

		core = NULL;
		switch (core_id) {
		case BCM43xx_COREID_PCI:
			core = &bcm->core_pci;
			if (core->flags & BCM43xx_COREFLAG_AVAILABLE) {
				printk(KERN_WARNING PFX "Multiple PCI cores found.\n");
				continue;
			}
			break;
		case BCM43xx_COREID_V90:
			core = &bcm->core_v90;
			if (core->flags & BCM43xx_COREFLAG_AVAILABLE) {
				printk(KERN_WARNING PFX "Multiple V90 cores found.\n");
				continue;
			}
			break;
		case BCM43xx_COREID_PCMCIA:
			core = &bcm->core_pcmcia;
			if (core->flags & BCM43xx_COREFLAG_AVAILABLE) {
				printk(KERN_WARNING PFX "Multiple PCMCIA cores found.\n");
				continue;
			}
			break;
		case BCM43xx_COREID_ETHERNET:
			core = &bcm->core_ethernet;
			if (core->flags & BCM43xx_COREFLAG_AVAILABLE) {
				printk(KERN_WARNING PFX "Multiple Ethernet cores found.\n");
				continue;
			}
			break;
		case BCM43xx_COREID_80211:
			for (i = 0; i < BCM43xx_MAX_80211_CORES; i++) {
				core = &(bcm->core_80211[i]);
				if (!(core->flags & BCM43xx_COREFLAG_AVAILABLE))
					break;
				core = NULL;
			}
			if (!core) {
				printk(KERN_WARNING PFX "More than %d cores of type 802.11 found.\n",
				       BCM43xx_MAX_80211_CORES);
				continue;
			}
			if (i != 0) {
				/* More than one 80211 core is only supported
				 * by special chips.
				 * There are chips with two 80211 cores, but with
				 * dangling pins on the second core. Be careful
				 * and ignore these cores here.
				 */
				if (bcm->pci_dev->device != 0x4324) {
					dprintk(KERN_INFO PFX "Ignoring additional 802.11 core.\n");
					continue;
				}
			}
			switch (core_rev) {
			case 2:
			case 4:
			case 5:
			case 6:
			case 7:
			case 9:
				break;
			default:
				printk(KERN_ERR PFX "Error: Unsupported 80211 core revision %u\n",
				       core_rev);
				err = -ENODEV;
				goto out;
			}
			core->phy = &bcm->phy[i];
			core->phy->antenna_diversity = 0xffff;
			core->phy->savedpctlreg = 0xFFFF;
			core->phy->minlowsig[0] = 0xFFFF;
			core->phy->minlowsig[1] = 0xFFFF;
			core->phy->minlowsigpos[0] = 0;
			core->phy->minlowsigpos[1] = 0;
			spin_lock_init(&core->phy->lock);
			core->radio = &bcm->radio[i];
			core->radio->interfmode = BCM43xx_RADIO_INTERFMODE_AUTOWLAN;
			core->radio->channel = 0xFF;
			core->radio->initial_channel = 0xFF;
			core->radio->lofcal = 0xFFFF;
			core->radio->initval = 0xFFFF;
			core->radio->nrssi[0] = -1000;
			core->radio->nrssi[1] = -1000;
			core->dma = &bcm->dma[i];
			core->pio = &bcm->pio[i];
			break;
		case BCM43xx_COREID_CHIPCOMMON:
			printk(KERN_WARNING PFX "Multiple CHIPCOMMON cores found.\n");
			break;
		default:
			printk(KERN_WARNING PFX "Unknown core found (ID 0x%x)\n", core_id);
		}
		if (core) {
			core->flags |= BCM43xx_COREFLAG_AVAILABLE;
			core->id = core_id;
			core->rev = core_rev;
			core->index = current_core;
		}
	}

	if (!(bcm->core_80211[0].flags & BCM43xx_COREFLAG_AVAILABLE)) {
		printk(KERN_ERR PFX "Error: No 80211 core found!\n");
		err = -ENODEV;
		goto out;
	}

	err = bcm43xx_switch_core(bcm, &bcm->core_80211[0]);

	assert(err == 0);
out:
	return err;
}

static void bcm43xx_gen_bssid(struct bcm43xx_private *bcm)
{
	const u8 *mac = (const u8*)(bcm->net_dev->dev_addr);
	u8 *bssid = bcm->ieee->bssid;

	switch (bcm->ieee->iw_mode) {
	case IW_MODE_ADHOC:
		random_ether_addr(bssid);
		break;
	case IW_MODE_MASTER:
	case IW_MODE_INFRA:
	case IW_MODE_REPEAT:
	case IW_MODE_SECOND:
	case IW_MODE_MONITOR:
		memcpy(bssid, mac, ETH_ALEN);
		break;
	default:
		assert(0);
	}
}

static void bcm43xx_rate_memory_write(struct bcm43xx_private *bcm,
				      u16 rate,
				      int is_ofdm)
{
	u16 offset;

	if (is_ofdm) {
		offset = 0x480;
		offset += (bcm43xx_plcp_get_ratecode_ofdm(rate) & 0x000F) * 2;
	}
	else {
		offset = 0x4C0;
		offset += (bcm43xx_plcp_get_ratecode_cck(rate) & 0x000F) * 2;
	}
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, offset + 0x20,
			    bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED, offset));
}

static void bcm43xx_rate_memory_init(struct bcm43xx_private *bcm)
{
	switch (bcm->current_core->phy->type) {
	case BCM43xx_PHYTYPE_A:
	case BCM43xx_PHYTYPE_G:
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_6MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_12MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_18MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_24MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_36MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_48MB, 1);
		bcm43xx_rate_memory_write(bcm, IEEE80211_OFDM_RATE_54MB, 1);
	case BCM43xx_PHYTYPE_B:
		bcm43xx_rate_memory_write(bcm, IEEE80211_CCK_RATE_1MB, 0);
		bcm43xx_rate_memory_write(bcm, IEEE80211_CCK_RATE_2MB, 0);
		bcm43xx_rate_memory_write(bcm, IEEE80211_CCK_RATE_5MB, 0);
		bcm43xx_rate_memory_write(bcm, IEEE80211_CCK_RATE_11MB, 0);
		break;
	default:
		assert(0);
	}
}

static void bcm43xx_wireless_core_cleanup(struct bcm43xx_private *bcm)
{
	bcm43xx_chip_cleanup(bcm);
	bcm43xx_pio_free(bcm);
	bcm43xx_dma_free(bcm);

	bcm->current_core->flags &= ~ BCM43xx_COREFLAG_INITIALIZED;
}

/* http://bcm-specs.sipsolutions.net/80211Init */
static int bcm43xx_wireless_core_init(struct bcm43xx_private *bcm)
{
	u32 ucodeflags;
	int err;
	u32 sbimconfiglow;
	u8 limit;

	if (bcm->chip_rev < 5) {
		sbimconfiglow = bcm43xx_read32(bcm, BCM43xx_CIR_SBIMCONFIGLOW);
		sbimconfiglow &= ~ BCM43xx_SBIMCONFIGLOW_REQUEST_TOUT_MASK;
		sbimconfiglow &= ~ BCM43xx_SBIMCONFIGLOW_SERVICE_TOUT_MASK;
		if (bcm->bustype == BCM43xx_BUSTYPE_PCI)
			sbimconfiglow |= 0x32;
		else if (bcm->bustype == BCM43xx_BUSTYPE_SB)
			sbimconfiglow |= 0x53;
		else
			assert(0);
		bcm43xx_write32(bcm, BCM43xx_CIR_SBIMCONFIGLOW, sbimconfiglow);
	}

	bcm43xx_phy_calibrate(bcm);
	err = bcm43xx_chip_init(bcm);
	if (err)
		goto out;

	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0016, bcm->current_core->rev);
	ucodeflags = bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED, BCM43xx_UCODEFLAGS_OFFSET);

	if (0 /*FIXME: which condition has to be used here? */)
		ucodeflags |= 0x00000010;

	/* HW decryption needs to be set now */
	ucodeflags |= 0x40000000;
	
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G) {
		ucodeflags |= BCM43xx_UCODEFLAG_UNKBGPHY;
		if (bcm->current_core->phy->rev == 1)
			ucodeflags |= BCM43xx_UCODEFLAG_UNKGPHY;
		if (bcm->sprom.boardflags & BCM43xx_BFL_PACTRL)
			ucodeflags |= BCM43xx_UCODEFLAG_UNKPACTRL;
	} else if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_B) {
		ucodeflags |= BCM43xx_UCODEFLAG_UNKBGPHY;
		if ((bcm->current_core->phy->rev >= 2) &&
		    (bcm->current_core->radio->version == 0x2050))
			ucodeflags &= ~BCM43xx_UCODEFLAG_UNKGPHY;
	}

	if (ucodeflags != bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED,
					     BCM43xx_UCODEFLAGS_OFFSET)) {
		bcm43xx_shm_write32(bcm, BCM43xx_SHM_SHARED,
				    BCM43xx_UCODEFLAGS_OFFSET, ucodeflags);
	}

	/* Short/Long Retry Limit.
	 * The retry-limit is a 4-bit counter. Enforce this to avoid overflowing
	 * the chip-internal counter.
	 */
	limit = limit_value(modparam_short_retry, 0, 0xF);
	bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0006, limit);
	limit = limit_value(modparam_long_retry, 0, 0xF);
	bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0007, limit);

	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0044, 3);
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0046, 2);

	bcm43xx_rate_memory_init(bcm);

	/* Minimum Contention Window */
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_B)
		bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0003, 0x0000001f);
	else
		bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0003, 0x0000000f);
	/* Maximum Contention Window */
	bcm43xx_shm_write32(bcm, BCM43xx_SHM_WIRELESS, 0x0004, 0x000003ff);

	bcm43xx_gen_bssid(bcm);
	bcm43xx_write_mac_bssid_templates(bcm);

	if (bcm->current_core->rev >= 5)
		bcm43xx_write16(bcm, 0x043C, 0x000C);

	if (!bcm->pio_mode) {
		err = bcm43xx_dma_init(bcm);
		if (err)
			goto err_chip_cleanup;
	} else {
		err = bcm43xx_pio_init(bcm);
		if (err)
			goto err_chip_cleanup;
	}
	bcm43xx_write16(bcm, 0x0612, 0x0050);
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0416, 0x0050);
	bcm43xx_shm_write16(bcm, BCM43xx_SHM_SHARED, 0x0414, 0x01F4);

	bcm43xx_mac_enable(bcm);
	bcm43xx_interrupt_enable(bcm, bcm->irq_savedstate);

	bcm->current_core->flags |= BCM43xx_COREFLAG_INITIALIZED;
out:
	return err;

err_chip_cleanup:
	bcm43xx_chip_cleanup(bcm);
	goto out;
}

static int bcm43xx_chipset_attach(struct bcm43xx_private *bcm)
{
	int err;
	u16 pci_status;

	err = bcm43xx_pctl_set_crystal(bcm, 1);
	if (err)
		goto out;
	bcm43xx_pci_read_config16(bcm, PCI_STATUS, &pci_status);
	bcm43xx_pci_write_config16(bcm, PCI_STATUS, pci_status & ~PCI_STATUS_SIG_TARGET_ABORT);

out:
	return err;
}

static void bcm43xx_chipset_detach(struct bcm43xx_private *bcm)
{
	bcm43xx_pctl_set_clock(bcm, BCM43xx_PCTL_CLK_SLOW);
	bcm43xx_pctl_set_crystal(bcm, 0);
}

static inline void bcm43xx_pcicore_broadcast_value(struct bcm43xx_private *bcm,
						   u32 address,
						   u32 data)
{
	bcm43xx_write32(bcm, BCM43xx_PCICORE_BCAST_ADDR, address);
	bcm43xx_write32(bcm, BCM43xx_PCICORE_BCAST_DATA, data);
}

static int bcm43xx_pcicore_commit_settings(struct bcm43xx_private *bcm)
{
	int err;
	struct bcm43xx_coreinfo *old_core;

	old_core = bcm->current_core;
	err = bcm43xx_switch_core(bcm, &bcm->core_pci);
	if (err)
		goto out;

	bcm43xx_pcicore_broadcast_value(bcm, 0xfd8, 0x00000000);

	bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);
out:
	return err;
}

/* Make an I/O Core usable. "core_mask" is the bitmask of the cores to enable.
 * To enable core 0, pass a core_mask of 1<<0
 */
static int bcm43xx_setup_backplane_pci_connection(struct bcm43xx_private *bcm,
						  u32 core_mask)
{
	u32 backplane_flag_nr;
	u32 value;
	struct bcm43xx_coreinfo *old_core;
	int err = 0;

	value = bcm43xx_read32(bcm, BCM43xx_CIR_SBTPSFLAG);
	backplane_flag_nr = value & BCM43xx_BACKPLANE_FLAG_NR_MASK;

	old_core = bcm->current_core;
	err = bcm43xx_switch_core(bcm, &bcm->core_pci);
	if (err)
		goto out;

	if (bcm->core_pci.rev < 6) {
		value = bcm43xx_read32(bcm, BCM43xx_CIR_SBINTVEC);
		value |= (1 << backplane_flag_nr);
		bcm43xx_write32(bcm, BCM43xx_CIR_SBINTVEC, value);
	} else {
		err = bcm43xx_pci_read_config32(bcm, BCM43xx_PCICFG_ICR, &value);
		if (err) {
			printk(KERN_ERR PFX "Error: ICR setup failure!\n");
			goto out_switch_back;
		}
		value |= core_mask << 8;
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCICFG_ICR, value);
		if (err) {
			printk(KERN_ERR PFX "Error: ICR setup failure!\n");
			goto out_switch_back;
		}
	}

	value = bcm43xx_read32(bcm, BCM43xx_PCICORE_SBTOPCI2);
	value |= BCM43xx_SBTOPCI2_PREFETCH | BCM43xx_SBTOPCI2_BURST;
	bcm43xx_write32(bcm, BCM43xx_PCICORE_SBTOPCI2, value);

	if (bcm->core_pci.rev < 5) {
		value = bcm43xx_read32(bcm, BCM43xx_CIR_SBIMCONFIGLOW);
		value |= (2 << BCM43xx_SBIMCONFIGLOW_SERVICE_TOUT_SHIFT)
			 & BCM43xx_SBIMCONFIGLOW_SERVICE_TOUT_MASK;
		value |= (3 << BCM43xx_SBIMCONFIGLOW_REQUEST_TOUT_SHIFT)
			 & BCM43xx_SBIMCONFIGLOW_REQUEST_TOUT_MASK;
		bcm43xx_write32(bcm, BCM43xx_CIR_SBIMCONFIGLOW, value);
		err = bcm43xx_pcicore_commit_settings(bcm);
		assert(err == 0);
	}

out_switch_back:
	err = bcm43xx_switch_core(bcm, old_core);
out:
	return err;
}

static void bcm43xx_softmac_init(struct bcm43xx_private *bcm)
{
	ieee80211softmac_start(bcm->net_dev);
}

static void bcm43xx_periodic_work0_handler(void *d)
{
	struct bcm43xx_private *bcm = d;
	unsigned long flags;
	//TODO: unsigned int aci_average;

	spin_lock_irqsave(&bcm->lock, flags);

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G) {
		//FIXME: aci_average = bcm43xx_update_aci_average(bcm);
		if (bcm->current_core->radio->aci_enable && bcm->current_core->radio->aci_wlan_automatic) {
			bcm43xx_mac_suspend(bcm);
			if (!bcm->current_core->radio->aci_enable &&
			    1 /*FIXME: We are not scanning? */) {
				/*FIXME: First add bcm43xx_update_aci_average() before
				 * uncommenting this: */
				//if (bcm43xx_radio_aci_scan)
				//	bcm43xx_radio_set_interference_mitigation(bcm,
				//	                                          BCM43xx_RADIO_INTERFMODE_MANUALWLAN);
			} else if (1/*FIXME*/) {
				//if ((aci_average > 1000) && !(bcm43xx_radio_aci_scan(bcm)))
				//	bcm43xx_radio_set_interference_mitigation(bcm,
				//	                                          BCM43xx_RADIO_INTERFMODE_MANUALWLAN);
			}
			bcm43xx_mac_enable(bcm);
		} else if  (bcm->current_core->radio->interfmode == BCM43xx_RADIO_INTERFMODE_NONWLAN) {
			if (bcm->current_core->phy->rev == 1) {
				//FIXME: implement rev1 workaround
			}
		}
	}
	bcm43xx_phy_xmitpower(bcm); //FIXME: unless scanning?
	//TODO for APHY (temperature?)

	if (likely(!bcm->shutting_down)) {
		queue_delayed_work(bcm->workqueue, &bcm->periodic_work0,
				   BCM43xx_PERIODIC_0_DELAY);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

static void bcm43xx_periodic_work1_handler(void *d)
{
	struct bcm43xx_private *bcm = d;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);

	bcm43xx_phy_lo_mark_all_unused(bcm);
	if (bcm->sprom.boardflags & BCM43xx_BFL_RSSI) {
		bcm43xx_mac_suspend(bcm);
		bcm43xx_calc_nrssi_slope(bcm);
		bcm43xx_mac_enable(bcm);
	}

	if (likely(!bcm->shutting_down)) {
		queue_delayed_work(bcm->workqueue, &bcm->periodic_work1,
				   BCM43xx_PERIODIC_1_DELAY);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

static void bcm43xx_periodic_work2_handler(void *d)
{
	struct bcm43xx_private *bcm = d;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);

	assert(bcm->current_core->phy->type == BCM43xx_PHYTYPE_G);
	assert(bcm->current_core->phy->rev >= 2);

	bcm43xx_mac_suspend(bcm);
	bcm43xx_phy_lo_g_measure(bcm);
	bcm43xx_mac_enable(bcm);

	if (likely(!bcm->shutting_down)) {
		queue_delayed_work(bcm->workqueue, &bcm->periodic_work2,
				   BCM43xx_PERIODIC_2_DELAY);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

static void bcm43xx_periodic_work3_handler(void *d)
{
	struct bcm43xx_private *bcm = d;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);

	/* Update device statistics. */
	bcm43xx_calculate_link_quality(bcm);

	if (likely(!bcm->shutting_down)) {
		queue_delayed_work(bcm->workqueue, &bcm->periodic_work3,
				   BCM43xx_PERIODIC_3_DELAY);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

/* Delete all periodic tasks and make
 * sure they are not running any longer
 */
static void bcm43xx_periodic_tasks_delete(struct bcm43xx_private *bcm)
{
	cancel_delayed_work(&bcm->periodic_work0);
	cancel_delayed_work(&bcm->periodic_work1);
	cancel_delayed_work(&bcm->periodic_work2);
	cancel_delayed_work(&bcm->periodic_work3);
	flush_workqueue(bcm->workqueue);
}

/* Setup all periodic tasks. */
static void bcm43xx_periodic_tasks_setup(struct bcm43xx_private *bcm)
{
	INIT_WORK(&bcm->periodic_work0, bcm43xx_periodic_work0_handler, bcm);
	INIT_WORK(&bcm->periodic_work1, bcm43xx_periodic_work1_handler, bcm);
	INIT_WORK(&bcm->periodic_work2, bcm43xx_periodic_work2_handler, bcm);
	INIT_WORK(&bcm->periodic_work3, bcm43xx_periodic_work3_handler, bcm);

	/* Periodic task 0: Delay ~15sec */
	queue_delayed_work(bcm->workqueue, &bcm->periodic_work0,
			   BCM43xx_PERIODIC_0_DELAY);

	/* Periodic task 1: Delay ~60sec */
	queue_delayed_work(bcm->workqueue, &bcm->periodic_work1,
			   BCM43xx_PERIODIC_1_DELAY);

	/* Periodic task 2: Delay ~120sec */
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G &&
	    bcm->current_core->phy->rev >= 2) {
		queue_delayed_work(bcm->workqueue, &bcm->periodic_work2,
				   BCM43xx_PERIODIC_2_DELAY);
	}

	/* Periodic task 3: Delay ~30sec */
	queue_delayed_work(bcm->workqueue, &bcm->periodic_work3,
			   BCM43xx_PERIODIC_3_DELAY);
}

static void bcm43xx_security_init(struct bcm43xx_private *bcm)
{
	bcm->security_offset = bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED,
						  0x0056) * 2;
	bcm43xx_clear_keys(bcm);
}

/* This is the opposite of bcm43xx_init_board() */
static void bcm43xx_free_board(struct bcm43xx_private *bcm)
{
	int i, err;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);
	bcm->initialized = 0;
	bcm->shutting_down = 1;
	spin_unlock_irqrestore(&bcm->lock, flags);

	bcm43xx_periodic_tasks_delete(bcm);

	for (i = 0; i < BCM43xx_MAX_80211_CORES; i++) {
		if (!(bcm->core_80211[i].flags & BCM43xx_COREFLAG_AVAILABLE))
			continue;
		if (!(bcm->core_80211[i].flags & BCM43xx_COREFLAG_INITIALIZED))
			continue;

		err = bcm43xx_switch_core(bcm, &bcm->core_80211[i]);
		assert(err == 0);
		bcm43xx_wireless_core_cleanup(bcm);
	}

	bcm43xx_pctl_set_crystal(bcm, 0);

	spin_lock_irqsave(&bcm->lock, flags);
	bcm->shutting_down = 0;
	spin_unlock_irqrestore(&bcm->lock, flags);
}

static int bcm43xx_init_board(struct bcm43xx_private *bcm)
{
	int i, err;
	int num_80211_cores;
	int connect_phy;
	unsigned long flags;

	might_sleep();

	spin_lock_irqsave(&bcm->lock, flags);
	bcm->initialized = 0;
	bcm->shutting_down = 0;
	spin_unlock_irqrestore(&bcm->lock, flags);

	err = bcm43xx_pctl_set_crystal(bcm, 1);
	if (err)
		goto out;
	err = bcm43xx_pctl_init(bcm);
	if (err)
		goto err_crystal_off;
	err = bcm43xx_pctl_set_clock(bcm, BCM43xx_PCTL_CLK_FAST);
	if (err)
		goto err_crystal_off;

	tasklet_enable(&bcm->isr_tasklet);
	num_80211_cores = bcm43xx_num_80211_cores(bcm);
	for (i = 0; i < num_80211_cores; i++) {
		err = bcm43xx_switch_core(bcm, &bcm->core_80211[i]);
		assert(err != -ENODEV);
		if (err)
			goto err_80211_unwind;

		/* Enable the selected wireless core.
		 * Connect PHY only on the first core.
		 */
		if (!bcm43xx_core_enabled(bcm)) {
			if (num_80211_cores == 1) {
				connect_phy = bcm->current_core->phy->connected;
			} else {
				if (i == 0)
					connect_phy = 1;
				else
					connect_phy = 0;
			}
			bcm43xx_wireless_core_reset(bcm, connect_phy);
		}

		if (i != 0)
			bcm43xx_wireless_core_mark_inactive(bcm, &bcm->core_80211[0]);

		err = bcm43xx_wireless_core_init(bcm);
		if (err)
			goto err_80211_unwind;

		if (i != 0) {
			bcm43xx_mac_suspend(bcm);
			bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);
			bcm43xx_radio_turn_off(bcm);
		}
	}
	bcm->active_80211_core = &bcm->core_80211[0];
	if (num_80211_cores >= 2) {
		bcm43xx_switch_core(bcm, &bcm->core_80211[0]);
		bcm43xx_mac_enable(bcm);
	}
	bcm43xx_macfilter_clear(bcm, BCM43xx_MACFILTER_ASSOC);
	bcm43xx_macfilter_set(bcm, BCM43xx_MACFILTER_SELF, (u8 *)(bcm->net_dev->dev_addr));
	dprintk(KERN_INFO PFX "80211 cores initialized\n");
	bcm43xx_security_init(bcm);
	bcm43xx_softmac_init(bcm);

	bcm43xx_pctl_set_clock(bcm, BCM43xx_PCTL_CLK_DYNAMIC);

	spin_lock_irqsave(&bcm->lock, flags);
	bcm->initialized = 1;
	spin_unlock_irqrestore(&bcm->lock, flags);

	if (bcm->current_core->radio->initial_channel != 0xFF) {
		bcm43xx_mac_suspend(bcm);
		bcm43xx_radio_selectchannel(bcm, bcm->current_core->radio->initial_channel, 0);
		bcm43xx_mac_enable(bcm);
	}
	bcm43xx_periodic_tasks_setup(bcm);

	assert(err == 0);
out:
	return err;

err_80211_unwind:
	tasklet_disable(&bcm->isr_tasklet);
	/* unwind all 80211 initialization */
	for (i = 0; i < num_80211_cores; i++) {
		if (!(bcm->core_80211[i].flags & BCM43xx_COREFLAG_INITIALIZED))
			continue;
		bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);
		bcm43xx_wireless_core_cleanup(bcm);
	}
err_crystal_off:
	bcm43xx_pctl_set_crystal(bcm, 0);
	goto out;
}

static void bcm43xx_detach_board(struct bcm43xx_private *bcm)
{
	struct pci_dev *pci_dev = bcm->pci_dev;
	int i;

	bcm43xx_chipset_detach(bcm);
	/* Do _not_ access the chip, after it is detached. */
	iounmap(bcm->mmio_addr);
	
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);

	/* Free allocated structures/fields */
	for (i = 0; i < BCM43xx_MAX_80211_CORES; i++) {
		kfree(bcm->phy[i]._lo_pairs);
		if (bcm->phy[i].dyn_tssi_tbl)
			kfree(bcm->phy[i].tssi2dbm);
	}
}	

static int bcm43xx_read_phyinfo(struct bcm43xx_private *bcm)
{
	u16 value;
	u8 phy_version;
	u8 phy_type;
	u8 phy_rev;
	int phy_rev_ok = 1;
	void *p;

	value = bcm43xx_read16(bcm, BCM43xx_MMIO_PHY_VER);

	phy_version = (value & 0xF000) >> 12;
	phy_type = (value & 0x0F00) >> 8;
	phy_rev = (value & 0x000F);

	dprintk(KERN_INFO PFX "Detected PHY: Version: %x, Type %x, Revision %x\n",
		phy_version, phy_type, phy_rev);

	switch (phy_type) {
	case BCM43xx_PHYTYPE_A:
		if (phy_rev >= 4)
			phy_rev_ok = 0;
		/*FIXME: We need to switch the ieee->modulation, etc.. flags,
		 *       if we switch 80211 cores after init is done.
		 *       As we do not implement on the fly switching between
		 *       wireless cores, I will leave this as a future task.
		 */
		bcm->ieee->modulation = IEEE80211_OFDM_MODULATION;
		bcm->ieee->mode = IEEE_A;
		bcm->ieee->freq_band = IEEE80211_52GHZ_BAND |
				       IEEE80211_24GHZ_BAND;
		break;
	case BCM43xx_PHYTYPE_B:
		if (phy_rev != 2 && phy_rev != 4 && phy_rev != 6 && phy_rev != 7)
			phy_rev_ok = 0;
		bcm->ieee->modulation = IEEE80211_CCK_MODULATION;
		bcm->ieee->mode = IEEE_B;
		bcm->ieee->freq_band = IEEE80211_24GHZ_BAND;
		break;
	case BCM43xx_PHYTYPE_G:
		if (phy_rev > 7)
			phy_rev_ok = 0;
		bcm->ieee->modulation = IEEE80211_OFDM_MODULATION |
					IEEE80211_CCK_MODULATION;
		bcm->ieee->mode = IEEE_G;
		bcm->ieee->freq_band = IEEE80211_24GHZ_BAND;
		break;
	default:
		printk(KERN_ERR PFX "Error: Unknown PHY Type %x\n",
		       phy_type);
		return -ENODEV;
	};
	if (!phy_rev_ok) {
		printk(KERN_WARNING PFX "Invalid PHY Revision %x\n",
		       phy_rev);
	}

	bcm->current_core->phy->version = phy_version;
	bcm->current_core->phy->type = phy_type;
	bcm->current_core->phy->rev = phy_rev;
	if ((phy_type == BCM43xx_PHYTYPE_B) || (phy_type == BCM43xx_PHYTYPE_G)) {
		p = kzalloc(sizeof(struct bcm43xx_lopair) * BCM43xx_LO_COUNT,
			    GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		bcm->current_core->phy->_lo_pairs = p;
	}

	return 0;
}

static int bcm43xx_attach_board(struct bcm43xx_private *bcm)
{
	struct pci_dev *pci_dev = bcm->pci_dev;
	struct net_device *net_dev = bcm->net_dev;
	int err;
	int i;
	void __iomem *ioaddr;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
	int num_80211_cores;
	u32 coremask;

	err = pci_enable_device(pci_dev);
	if (err) {
		printk(KERN_ERR PFX "unable to wake up pci device (%i)\n", err);
		err = -ENODEV;
		goto out;
	}

	mmio_start = pci_resource_start(pci_dev, 0);
	mmio_end = pci_resource_end(pci_dev, 0);
	mmio_flags = pci_resource_flags(pci_dev, 0);
	mmio_len = pci_resource_len(pci_dev, 0);

	/* make sure PCI base addr is MMIO */
	if (!(mmio_flags & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX
		       "%s, region #0 not an MMIO resource, aborting\n",
		       pci_name(pci_dev));
		err = -ENODEV;
		goto err_pci_disable;
	}
//FIXME: Why is this check disabled for BCM947XX? What is the IO_SIZE there?
#ifndef CONFIG_BCM947XX
	if (mmio_len != BCM43xx_IO_SIZE) {
		printk(KERN_ERR PFX
		       "%s: invalid PCI mem region size(s), aborting\n",
		       pci_name(pci_dev));
		err = -ENODEV;
		goto err_pci_disable;
	}
#endif

	err = pci_request_regions(pci_dev, KBUILD_MODNAME);
	if (err) {
		printk(KERN_ERR PFX
		       "could not access PCI resources (%i)\n", err);
		goto err_pci_disable;
	}

	/* enable PCI bus-mastering */
	pci_set_master(pci_dev);

	/* ioremap MMIO region */
	ioaddr = ioremap(mmio_start, mmio_len);
	if (!ioaddr) {
		printk(KERN_ERR PFX "%s: cannot remap MMIO, aborting\n",
		       pci_name(pci_dev));
		err = -EIO;
		goto err_pci_release;
	}

	net_dev->base_addr = (unsigned long)ioaddr;
	bcm->mmio_addr = ioaddr;
	bcm->mmio_len = mmio_len;

	bcm43xx_pci_read_config16(bcm, PCI_SUBSYSTEM_VENDOR_ID,
	                          &bcm->board_vendor);
	bcm43xx_pci_read_config16(bcm, PCI_SUBSYSTEM_ID,
	                          &bcm->board_type);
	bcm43xx_pci_read_config16(bcm, PCI_REVISION_ID,
	                          &bcm->board_revision);

	err = bcm43xx_chipset_attach(bcm);
	if (err)
		goto err_iounmap;
	err = bcm43xx_pctl_init(bcm);
	if (err)
		goto err_chipset_detach;
	err = bcm43xx_probe_cores(bcm);
	if (err)
		goto err_chipset_detach;
	
	num_80211_cores = bcm43xx_num_80211_cores(bcm);

	/* Attach all IO cores to the backplane. */
	coremask = 0;
	for (i = 0; i < num_80211_cores; i++)
		coremask |= (1 << bcm->core_80211[i].index);
	//FIXME: Also attach some non80211 cores?
	err = bcm43xx_setup_backplane_pci_connection(bcm, coremask);
	if (err) {
		printk(KERN_ERR PFX "Backplane->PCI connection failed!\n");
		goto err_chipset_detach;
	}

	err = bcm43xx_read_sprom(bcm);
	if (err)
		goto err_chipset_detach;
	err = bcm43xx_leds_init(bcm);
	if (err)
		goto err_chipset_detach;

	for (i = 0; i < num_80211_cores; i++) {
		err = bcm43xx_switch_core(bcm, &bcm->core_80211[i]);
		assert(err != -ENODEV);
		if (err)
			goto err_80211_unwind;

		/* Enable the selected wireless core.
		 * Connect PHY only on the first core.
		 */
		bcm43xx_wireless_core_reset(bcm, (i == 0));

		err = bcm43xx_read_phyinfo(bcm);
		if (err && (i == 0))
			goto err_80211_unwind;

		err = bcm43xx_read_radioinfo(bcm);
		if (err && (i == 0))
			goto err_80211_unwind;

		err = bcm43xx_validate_chip(bcm);
		if (err && (i == 0))
			goto err_80211_unwind;

		bcm43xx_radio_turn_off(bcm);
		err = bcm43xx_phy_init_tssi2dbm_table(bcm);
		if (err)
			goto err_80211_unwind;
		bcm43xx_wireless_core_disable(bcm);
	}
	bcm43xx_pctl_set_crystal(bcm, 0);

	/* Set the MAC address in the networking subsystem */
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A)
		memcpy(bcm->net_dev->dev_addr, bcm->sprom.et1macaddr, 6);
	else
		memcpy(bcm->net_dev->dev_addr, bcm->sprom.il0macaddr, 6);

	bcm43xx_geo_init(bcm);

	snprintf(bcm->nick, IW_ESSID_MAX_SIZE,
		 "Broadcom %04X", bcm->chip_id);

	assert(err == 0);
out:
	return err;

err_80211_unwind:
	for (i = 0; i < BCM43xx_MAX_80211_CORES; i++) {
		kfree(bcm->phy[i]._lo_pairs);
		if (bcm->phy[i].dyn_tssi_tbl)
			kfree(bcm->phy[i].tssi2dbm);
	}
err_chipset_detach:
	bcm43xx_chipset_detach(bcm);
err_iounmap:
	iounmap(bcm->mmio_addr);
err_pci_release:
	pci_release_regions(pci_dev);
err_pci_disable:
	pci_disable_device(pci_dev);
	goto out;
}

static inline
s8 bcm43xx_rssi_postprocess(struct bcm43xx_private *bcm, u8 in_rssi,
			    int ofdm, int adjust_2053, int adjust_2050)
{
	s32 tmp;

	switch (bcm->current_core->radio->version) {
	case 0x2050:
		if (ofdm) {
			tmp = in_rssi;
			if (tmp > 127)
				tmp -= 256;
			tmp *= 73;
			tmp /= 64;
			if (adjust_2050)
				tmp += 25;
			else
				tmp -= 3;
		} else {
			if (bcm->sprom.boardflags & BCM43xx_BFL_RSSI) {
				if (in_rssi > 63)
					in_rssi = 63;
				tmp = bcm->current_core->radio->nrssi_lt[in_rssi];
				tmp = 31 - tmp;
				tmp *= -131;
				tmp /= 128;
				tmp -= 57;
			} else {
				tmp = in_rssi;
				tmp = 31 - tmp;
				tmp *= -149;
				tmp /= 128;
				tmp -= 68;
			}
			if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_G &&
			    adjust_2050)
				tmp += 25;
		}
		break;
	case 0x2060:
		if (in_rssi > 127)
			tmp = in_rssi - 256;
		else
			tmp = in_rssi;
		break;
	default:
		tmp = in_rssi;
		tmp -= 11;
		tmp *= 103;
		tmp /= 64;
		if (adjust_2053)
			tmp -= 109;
		else
			tmp -= 83;
	}

	return (s8)tmp;
}

static inline
s8 bcm43xx_rssinoise_postprocess(struct bcm43xx_private *bcm, u8 in_rssi)
{
	s8 ret;

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A) {
		//TODO: Incomplete specs.
		ret = 0;
	} else
		ret = bcm43xx_rssi_postprocess(bcm, in_rssi, 0, 1, 1);

	return ret;
}

static inline
int bcm43xx_rx_packet(struct bcm43xx_private *bcm,
		      struct sk_buff *skb,
		      struct ieee80211_rx_stats *stats)
{
	int err;

	err = ieee80211_rx(bcm->ieee, skb, stats);
	if (unlikely(err == 0))
		return -EINVAL;
	return 0;
}

int fastcall bcm43xx_rx(struct bcm43xx_private *bcm,
			struct sk_buff *skb,
			struct bcm43xx_rxhdr *rxhdr)
{
	struct bcm43xx_plcp_hdr4 *plcp;
	struct ieee80211_rx_stats stats;
	struct ieee80211_hdr_4addr *wlhdr;
	u16 frame_ctl;
	int is_packet_for_us = 0;
	int err = -EINVAL;
	const u16 rxflags1 = le16_to_cpu(rxhdr->flags1);
	const u16 rxflags2 = le16_to_cpu(rxhdr->flags2);
	const u16 rxflags3 = le16_to_cpu(rxhdr->flags3);
	const int is_ofdm = !!(rxflags1 & BCM43xx_RXHDR_FLAGS1_OFDM);

	if (rxflags2 & BCM43xx_RXHDR_FLAGS2_TYPE2FRAME) {
		plcp = (struct bcm43xx_plcp_hdr4 *)(skb->data + 2);
		/* Skip two unknown bytes and the PLCP header. */
		skb_pull(skb, 2 + sizeof(struct bcm43xx_plcp_hdr6));
	} else {
		plcp = (struct bcm43xx_plcp_hdr4 *)(skb->data);
		/* Skip the PLCP header. */
		skb_pull(skb, sizeof(struct bcm43xx_plcp_hdr6));
	}
	/* The SKB contains the PAYLOAD (wireless header + data)
	 * at this point. The FCS at the end is stripped.
	 */

	memset(&stats, 0, sizeof(stats));
	stats.mac_time = le16_to_cpu(rxhdr->mactime);
	stats.rssi = bcm43xx_rssi_postprocess(bcm, rxhdr->rssi, is_ofdm,
					      !!(rxflags1 & BCM43xx_RXHDR_FLAGS1_2053RSSIADJ),
					      !!(rxflags3 & BCM43xx_RXHDR_FLAGS3_2050RSSIADJ));
	stats.signal = rxhdr->signal_quality;	//FIXME
//TODO	stats.noise = 
	stats.rate = bcm43xx_plcp_get_bitrate(plcp, is_ofdm);
//printk("RX ofdm %d, rate == %u\n", is_ofdm, stats.rate);
	stats.received_channel = bcm->current_core->radio->channel;
//TODO	stats.control = 
	stats.mask = IEEE80211_STATMASK_SIGNAL |
//TODO		     IEEE80211_STATMASK_NOISE |
		     IEEE80211_STATMASK_RATE |
		     IEEE80211_STATMASK_RSSI;
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A)
		stats.freq = IEEE80211_52GHZ_BAND;
	else
		stats.freq = IEEE80211_24GHZ_BAND;
	stats.len = skb->len;

	bcm->stats.last_rx = jiffies;
	if (bcm->ieee->iw_mode == IW_MODE_MONITOR)
		return bcm43xx_rx_packet(bcm, skb, &stats);

	wlhdr = (struct ieee80211_hdr_4addr *)(skb->data);

	switch (bcm->ieee->iw_mode) {
	case IW_MODE_ADHOC:
		if (memcmp(wlhdr->addr1, bcm->net_dev->dev_addr, ETH_ALEN) == 0 ||
		    memcmp(wlhdr->addr3, bcm->ieee->bssid, ETH_ALEN) == 0 ||
		    is_broadcast_ether_addr(wlhdr->addr1) ||
		    is_multicast_ether_addr(wlhdr->addr1) ||
		    bcm->net_dev->flags & IFF_PROMISC)
			is_packet_for_us = 1;
		break;
	case IW_MODE_INFRA:
	default:
		/* When receiving multicast or broadcast packets, filter out
		   the packets we send ourself; we shouldn't see those */
		if (memcmp(wlhdr->addr3, bcm->ieee->bssid, ETH_ALEN) == 0 ||
		    memcmp(wlhdr->addr1, bcm->net_dev->dev_addr, ETH_ALEN) == 0 ||
		    (memcmp(wlhdr->addr3, bcm->net_dev->dev_addr, ETH_ALEN) &&
		     (is_broadcast_ether_addr(wlhdr->addr1) ||
		      is_multicast_ether_addr(wlhdr->addr1) ||
		      bcm->net_dev->flags & IFF_PROMISC)))
			is_packet_for_us = 1;
		break;
	}

	frame_ctl = le16_to_cpu(wlhdr->frame_ctl);
	if ((frame_ctl & IEEE80211_FCTL_PROTECTED) && !bcm->ieee->host_decrypt) {
		frame_ctl &= ~IEEE80211_FCTL_PROTECTED;
		wlhdr->frame_ctl = cpu_to_le16(frame_ctl);		
		/* trim IV and ICV */
		/* FIXME: this must be done only for WEP encrypted packets */
		if (skb->len < 32) {
			dprintkl(KERN_ERR PFX "RX packet dropped (PROTECTED flag "
					      "set and length < 32)\n");
			return -EINVAL;
		} else {		
			memmove(skb->data + 4, skb->data, 24);
			skb_pull(skb, 4);
			skb_trim(skb, skb->len - 4);
			stats.len -= 8;
		}
		wlhdr = (struct ieee80211_hdr_4addr *)(skb->data);
	}
	
	switch (WLAN_FC_GET_TYPE(frame_ctl)) {
	case IEEE80211_FTYPE_MGMT:
		ieee80211_rx_mgt(bcm->ieee, wlhdr, &stats);
		break;
	case IEEE80211_FTYPE_DATA:
		if (is_packet_for_us)
			err = bcm43xx_rx_packet(bcm, skb, &stats);
		break;
	case IEEE80211_FTYPE_CTL:
		break;
	default:
		assert(0);
		return -EINVAL;
	}

	return err;
}

/* Do the Hardware IO operations to send the txb */
static inline int bcm43xx_tx(struct bcm43xx_private *bcm,
			     struct ieee80211_txb *txb)
{
	int err = -ENODEV;

	if (bcm->pio_mode)
		err = bcm43xx_pio_transfer_txb(bcm, txb);
	else
		err = bcm43xx_dma_tx(bcm, txb);

	return err;
}

static void bcm43xx_ieee80211_set_chan(struct net_device *net_dev,
				       u8 channel)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);
	bcm43xx_mac_suspend(bcm);
	bcm43xx_radio_selectchannel(bcm, channel, 0);
	bcm43xx_mac_enable(bcm);
	spin_unlock_irqrestore(&bcm->lock, flags);
}

/* set_security() callback in struct ieee80211_device */
static void bcm43xx_ieee80211_set_security(struct net_device *net_dev,
					   struct ieee80211_security *sec)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct ieee80211_security *secinfo = &bcm->ieee->sec;
	unsigned long flags;
	int keyidx;
	
	dprintk(KERN_INFO PFX "set security called\n");
	
	spin_lock_irqsave(&bcm->lock, flags);
	
	for (keyidx = 0; keyidx<WEP_KEYS; keyidx++)
		if (sec->flags & (1<<keyidx)) {
			secinfo->encode_alg[keyidx] = sec->encode_alg[keyidx];
			secinfo->key_sizes[keyidx] = sec->key_sizes[keyidx];
			memcpy(secinfo->keys[keyidx], sec->keys[keyidx], SCM_KEY_LEN);
		}
	
	if (sec->flags & SEC_ACTIVE_KEY) {
		secinfo->active_key = sec->active_key;
		dprintk(KERN_INFO PFX "   .active_key = %d\n", sec->active_key);
	}
	if (sec->flags & SEC_UNICAST_GROUP) {
		secinfo->unicast_uses_group = sec->unicast_uses_group;
		dprintk(KERN_INFO PFX "   .unicast_uses_group = %d\n", sec->unicast_uses_group);
	}
	if (sec->flags & SEC_LEVEL) {
		secinfo->level = sec->level;
		dprintk(KERN_INFO PFX "   .level = %d\n", sec->level);
	}
	if (sec->flags & SEC_ENABLED) {
		secinfo->enabled = sec->enabled;
		dprintk(KERN_INFO PFX "   .enabled = %d\n", sec->enabled);
	}
	if (sec->flags & SEC_ENCRYPT) {
		secinfo->encrypt = sec->encrypt;
		dprintk(KERN_INFO PFX "   .encrypt = %d\n", sec->encrypt);
	}
	if (bcm->initialized && !bcm->ieee->host_encrypt) {
		if (secinfo->enabled) {
			/* upload WEP keys to hardware */
			char null_address[6] = { 0 };
			u8 algorithm = 0;
			for (keyidx = 0; keyidx<WEP_KEYS; keyidx++) {
				if (!(sec->flags & (1<<keyidx)))
					continue;
				switch (sec->encode_alg[keyidx]) {
					case SEC_ALG_NONE: algorithm = BCM43xx_SEC_ALGO_NONE; break;
					case SEC_ALG_WEP:
						algorithm = BCM43xx_SEC_ALGO_WEP;
						if (secinfo->key_sizes[keyidx] == 13)
							algorithm = BCM43xx_SEC_ALGO_WEP104;
						break;
					case SEC_ALG_TKIP:
						FIXME();
						algorithm = BCM43xx_SEC_ALGO_TKIP;
						break;
					case SEC_ALG_CCMP:
						FIXME();
						algorithm = BCM43xx_SEC_ALGO_AES;
						break;
					default:
						assert(0);
						break;
				}
				bcm43xx_key_write(bcm, keyidx, algorithm, sec->keys[keyidx], secinfo->key_sizes[keyidx], &null_address[0]);
				bcm->key[keyidx].enabled = 1;
				bcm->key[keyidx].algorithm = algorithm;
			}
		} else
				bcm43xx_clear_keys(bcm);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

/* hard_start_xmit() callback in struct ieee80211_device */
static int bcm43xx_ieee80211_hard_start_xmit(struct ieee80211_txb *txb,
					     struct net_device *net_dev,
					     int pri)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err = -ENODEV;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);
	if (likely(bcm->initialized))
		err = bcm43xx_tx(bcm, txb);
	spin_unlock_irqrestore(&bcm->lock, flags);

	return err;
}

static struct net_device_stats * bcm43xx_net_get_stats(struct net_device *net_dev)
{
	return &(bcm43xx_priv(net_dev)->ieee->stats);
}

static void bcm43xx_net_tx_timeout(struct net_device *net_dev)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	bcm43xx_controller_restart(bcm, "TX timeout");
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bcm43xx_net_poll_controller(struct net_device *net_dev)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;

	local_irq_save(flags);
	bcm43xx_interrupt_handler(bcm->irq, bcm, NULL);
	local_irq_restore(flags);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

static int bcm43xx_net_open(struct net_device *net_dev)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	return bcm43xx_init_board(bcm);
}

static int bcm43xx_net_stop(struct net_device *net_dev)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	ieee80211softmac_stop(net_dev);
	bcm43xx_disable_interrupts_sync(bcm, NULL);
	bcm43xx_free_board(bcm);

	return 0;
}

static void bcm43xx_init_private(struct bcm43xx_private *bcm,
				 struct net_device *net_dev,
				 struct pci_dev *pci_dev,
				 struct workqueue_struct *wq)
{
	bcm->ieee = netdev_priv(net_dev);
	bcm->softmac = ieee80211_priv(net_dev);
	bcm->softmac->set_channel = bcm43xx_ieee80211_set_chan;
	bcm->workqueue = wq;

#ifdef DEBUG_ENABLE_MMIO_PRINT
	bcm43xx_mmioprint_initial(bcm, 1);
#else
	bcm43xx_mmioprint_initial(bcm, 0);
#endif
#ifdef DEBUG_ENABLE_PCILOG
	bcm43xx_pciprint_initial(bcm, 1);
#else
	bcm43xx_pciprint_initial(bcm, 0);
#endif

	bcm->irq_savedstate = BCM43xx_IRQ_INITIAL;
	bcm->pci_dev = pci_dev;
	bcm->net_dev = net_dev;
	if (modparam_bad_frames_preempt)
		bcm->bad_frames_preempt = 1;
	spin_lock_init(&bcm->lock);
	tasklet_init(&bcm->isr_tasklet,
		     (void (*)(unsigned long))bcm43xx_interrupt_tasklet,
		     (unsigned long)bcm);
	tasklet_disable_nosync(&bcm->isr_tasklet);
	if (modparam_pio) {
		bcm->pio_mode = 1;
	} else {
		if (pci_set_dma_mask(pci_dev, DMA_30BIT_MASK) == 0) {
			bcm->pio_mode = 0;
		} else {
			printk(KERN_WARNING PFX "DMA not supported. Falling back to PIO.\n");
			bcm->pio_mode = 1;
		}
	}
	bcm->rts_threshold = BCM43xx_DEFAULT_RTS_THRESHOLD;

	/* default to sw encryption for now */
	bcm->ieee->host_build_iv = 0;
	bcm->ieee->host_encrypt = 1;
	bcm->ieee->host_decrypt = 1;
	
	bcm->ieee->iw_mode = BCM43xx_INITIAL_IWMODE;
	bcm->ieee->tx_headroom = sizeof(struct bcm43xx_txhdr);
	bcm->ieee->set_security = bcm43xx_ieee80211_set_security;
	bcm->ieee->hard_start_xmit = bcm43xx_ieee80211_hard_start_xmit;
}

static int __devinit bcm43xx_init_one(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	struct net_device *net_dev;
	struct bcm43xx_private *bcm;
	struct workqueue_struct *wq;
	int err;

#ifdef CONFIG_BCM947XX
	if ((pdev->bus->number == 0) && (pdev->device != 0x0800))
		return -ENODEV;
#endif

#ifdef DEBUG_SINGLE_DEVICE_ONLY
	if (strcmp(pci_name(pdev), DEBUG_SINGLE_DEVICE_ONLY))
		return -ENODEV;
#endif

	net_dev = alloc_ieee80211softmac(sizeof(*bcm));
	if (!net_dev) {
		printk(KERN_ERR PFX
		       "could not allocate ieee80211 device %s\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto out;
	}
	/* initialize the net_device struct */
	SET_MODULE_OWNER(net_dev);
	SET_NETDEV_DEV(net_dev, &pdev->dev);

	net_dev->open = bcm43xx_net_open;
	net_dev->stop = bcm43xx_net_stop;
	net_dev->get_stats = bcm43xx_net_get_stats;
	net_dev->tx_timeout = bcm43xx_net_tx_timeout;
#ifdef CONFIG_NET_POLL_CONTROLLER
	net_dev->poll_controller = bcm43xx_net_poll_controller;
#endif
	net_dev->wireless_handlers = &bcm43xx_wx_handlers_def;
	net_dev->irq = pdev->irq;
	SET_ETHTOOL_OPS(net_dev, &bcm43xx_ethtool_ops);

	/* initialize the bcm43xx_private struct */
	bcm = bcm43xx_priv(net_dev);
	memset(bcm, 0, sizeof(*bcm));
	wq = create_workqueue(KBUILD_MODNAME "_wq");
	if (!wq) {
		err = -ENOMEM;
		goto err_free_netdev;
	}
	bcm43xx_init_private(bcm, net_dev, pdev, wq);

	pci_set_drvdata(pdev, net_dev);

	err = bcm43xx_attach_board(bcm);
	if (err)
		goto err_destroy_wq;

	err = register_netdev(net_dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot register net device, "
		       "aborting.\n");
		err = -ENOMEM;
		goto err_detach_board;
	}

	bcm43xx_debugfs_add_device(bcm);

	assert(err == 0);
out:
	return err;

err_detach_board:
	bcm43xx_detach_board(bcm);
err_destroy_wq:
	destroy_workqueue(wq);
err_free_netdev:
	free_ieee80211softmac(net_dev);
	goto out;
}

static void __devexit bcm43xx_remove_one(struct pci_dev *pdev)
{
	struct net_device *net_dev = pci_get_drvdata(pdev);
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	bcm43xx_debugfs_remove_device(bcm);
	unregister_netdev(net_dev);
	bcm43xx_detach_board(bcm);
	assert(bcm->ucode == NULL);
	destroy_workqueue(bcm->workqueue);
	free_ieee80211softmac(net_dev);
}

/* Hard-reset the chip. Do not call this directly.
 * Use bcm43xx_controller_restart()
 */
static void bcm43xx_chip_reset(void *_bcm)
{
	struct bcm43xx_private *bcm = _bcm;
	struct net_device *net_dev = bcm->net_dev;
	struct pci_dev *pci_dev = bcm->pci_dev;
	struct workqueue_struct *wq = bcm->workqueue;
	int err;
	int was_initialized = bcm->initialized;

	netif_stop_queue(bcm->net_dev);
	tasklet_disable(&bcm->isr_tasklet);

	bcm->firmware_norelease = 1;
	if (was_initialized)
		bcm43xx_free_board(bcm);
	bcm->firmware_norelease = 0;
	bcm43xx_detach_board(bcm);
	bcm43xx_init_private(bcm, net_dev, pci_dev, wq);
	err = bcm43xx_attach_board(bcm);
	if (err)
		goto failure;
	if (was_initialized) {
		err = bcm43xx_init_board(bcm);
		if (err)
			goto failure;
	}
	netif_wake_queue(bcm->net_dev);
	printk(KERN_INFO PFX "Controller restarted\n");

	return;
failure:
	printk(KERN_ERR PFX "Controller restart failed\n");
}

/* Hard-reset the chip.
 * This can be called from interrupt or process context.
 * Make sure to _not_ re-enable device interrupts after this has been called.
*/
void bcm43xx_controller_restart(struct bcm43xx_private *bcm, const char *reason)
{
	bcm43xx_interrupt_disable(bcm, BCM43xx_IRQ_ALL);
	printk(KERN_ERR PFX "Controller RESET (%s) ...\n", reason);
	INIT_WORK(&bcm->restart_work, bcm43xx_chip_reset, bcm);
	queue_work(bcm->workqueue, &bcm->restart_work);
}

#ifdef CONFIG_PM

static int bcm43xx_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *net_dev = pci_get_drvdata(pdev);
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int try_to_shutdown = 0, err;

	dprintk(KERN_INFO PFX "Suspending...\n");

	spin_lock_irqsave(&bcm->lock, flags);
	bcm->was_initialized = bcm->initialized;
	if (bcm->initialized)
		try_to_shutdown = 1;
	spin_unlock_irqrestore(&bcm->lock, flags);

	netif_device_detach(net_dev);
	if (try_to_shutdown) {
		ieee80211softmac_stop(net_dev);
		err = bcm43xx_disable_interrupts_sync(bcm, &bcm->irq_savedstate);
		if (unlikely(err)) {
			dprintk(KERN_ERR PFX "Suspend failed.\n");
			return -EAGAIN;
		}
		bcm->firmware_norelease = 1;
		bcm43xx_free_board(bcm);
		bcm->firmware_norelease = 0;
	}
	bcm43xx_chipset_detach(bcm);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	dprintk(KERN_INFO PFX "Device suspended.\n");

	return 0;
}

static int bcm43xx_resume(struct pci_dev *pdev)
{
	struct net_device *net_dev = pci_get_drvdata(pdev);
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err = 0;

	dprintk(KERN_INFO PFX "Resuming...\n");

	pci_set_power_state(pdev, 0);
	pci_enable_device(pdev);
	pci_restore_state(pdev);

	bcm43xx_chipset_attach(bcm);
	if (bcm->was_initialized) {
		bcm->irq_savedstate = BCM43xx_IRQ_INITIAL;
		err = bcm43xx_init_board(bcm);
	}
	if (err) {
		printk(KERN_ERR PFX "Resume failed!\n");
		return err;
	}

	netif_device_attach(net_dev);
	
	/*FIXME: This should be handled by softmac instead. */
	schedule_work(&bcm->softmac->associnfo.work);

	dprintk(KERN_INFO PFX "Device resumed.\n");

	return 0;
}

#endif				/* CONFIG_PM */

static struct pci_driver bcm43xx_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = bcm43xx_pci_tbl,
	.probe = bcm43xx_init_one,
	.remove = __devexit_p(bcm43xx_remove_one),
#ifdef CONFIG_PM
	.suspend = bcm43xx_suspend,
	.resume = bcm43xx_resume,
#endif				/* CONFIG_PM */
};

static int __init bcm43xx_init(void)
{
	printk(KERN_INFO KBUILD_MODNAME " driver\n");
	bcm43xx_debugfs_init();
	return pci_register_driver(&bcm43xx_pci_driver);
}

static void __exit bcm43xx_exit(void)
{
	pci_unregister_driver(&bcm43xx_pci_driver);
	bcm43xx_debugfs_exit();
}

module_init(bcm43xx_init)
module_exit(bcm43xx_exit)

/* vim: set ts=8 sw=8 sts=8: */
