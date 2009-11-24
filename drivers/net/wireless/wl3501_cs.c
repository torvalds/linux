/*
 * WL3501 Wireless LAN PCMCIA Card Driver for Linux
 * Written originally for Linux 2.0.30 by Fox Chen, mhchen@golf.ccl.itri.org.tw
 * Ported to 2.2, 2.4 & 2.5 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * Wireless extensions in 2.4 by Gustavo Niemeyer <niemeyer@conectiva.com>
 *
 * References used by Fox Chen while writing the original driver for 2.0.30:
 *
 *   1. WL24xx packet drivers (tooasm.asm)
 *   2. Access Point Firmware Interface Specification for IEEE 802.11 SUTRO
 *   3. IEEE 802.11
 *   4. Linux network driver (/usr/src/linux/drivers/net)
 *   5. ISA card driver - wl24.c
 *   6. Linux PCMCIA skeleton driver - skeleton.c
 *   7. Linux PCMCIA 3c589 network driver - 3c589_cs.c
 *
 * Tested with WL2400 firmware 1.2, Linux 2.0.30, and pcmcia-cs-2.9.12
 *   1. Performance: about 165 Kbytes/sec in TCP/IP with Ad-Hoc mode.
 *      rsh 192.168.1.3 "dd if=/dev/zero bs=1k count=1000" > /dev/null
 *      (Specification 2M bits/sec. is about 250 Kbytes/sec., but we must deduct
 *       ETHER/IP/UDP/TCP header, and acknowledgement overhead)
 *
 * Tested with Planet AP in 2.4.17, 184 Kbytes/s in UDP in Infrastructure mode,
 * 173 Kbytes/s in TCP.
 *
 * Tested with Planet AP in 2.5.73-bk, 216 Kbytes/s in Infrastructure mode
 * with a SMP machine (dual pentium 100), using pktgen, 432 pps (pkt_size = 60)
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>

#include <net/iw_handler.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include "wl3501.h"

#ifndef __i386__
#define slow_down_io()
#endif

/* For rough constant delay */
#define WL3501_NOPLOOP(n) { int x = 0; while (x++ < n) slow_down_io(); }

/*
 * All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If you do not
 * define PCMCIA_DEBUG at all, all the debug code will be left out.  If you
 * compile with PCMCIA_DEBUG=0, the debug code will be present but disabled --
 * but it can then be enabled for specific modules at load time with a
 * 'pc_debug=#' option to insmod.
 */
#define PCMCIA_DEBUG 0
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
#define dprintk(n, format, args...) \
	{ if (pc_debug > (n)) \
		printk(KERN_INFO "%s: " format "\n", __func__ , ##args); }
#else
#define dprintk(n, format, args...)
#endif

#define wl3501_outb(a, b) { outb(a, b); slow_down_io(); }
#define wl3501_outb_p(a, b) { outb_p(a, b); slow_down_io(); }
#define wl3501_outsb(a, b, c) { outsb(a, b, c); slow_down_io(); }

#define WL3501_RELEASE_TIMEOUT (25 * HZ)
#define WL3501_MAX_ADHOC_TRIES 16

#define WL3501_RESUME	0
#define WL3501_SUSPEND	1

/*
 * The event() function is this driver's Card Services event handler.  It will
 * be called by Card Services when an appropriate card status event is
 * received. The config() and release() entry points are used to configure or
 * release a socket, in response to card insertion and ejection events.  They
 * are invoked from the wl24 event handler.
 */
static int wl3501_config(struct pcmcia_device *link);
static void wl3501_release(struct pcmcia_device *link);

/*
 * The dev_info variable is the "key" that is used to match up this
 * device driver with appropriate cards, through the card configuration
 * database.
 */
static dev_info_t wl3501_dev_info = "wl3501_cs";

static const struct {
	int reg_domain;
	int min, max, deflt;
} iw_channel_table[] = {
	{
		.reg_domain = IW_REG_DOMAIN_FCC,
		.min	    = 1,
		.max	    = 11,
		.deflt	    = 1,
	},
	{
		.reg_domain = IW_REG_DOMAIN_DOC,
		.min	    = 1,
		.max	    = 11,
		.deflt	    = 1,
	},
	{
		.reg_domain = IW_REG_DOMAIN_ETSI,
		.min	    = 1,
		.max	    = 13,
		.deflt	    = 1,
	},
	{
		.reg_domain = IW_REG_DOMAIN_SPAIN,
		.min	    = 10,
		.max	    = 11,
		.deflt	    = 10,
	},
	{
		.reg_domain = IW_REG_DOMAIN_FRANCE,
		.min	    = 10,
		.max	    = 13,
		.deflt	    = 10,
	},
	{
		.reg_domain = IW_REG_DOMAIN_MKK,
		.min	    = 14,
		.max	    = 14,
		.deflt	    = 14,
	},
	{
		.reg_domain = IW_REG_DOMAIN_MKK1,
		.min	    = 1,
		.max	    = 14,
		.deflt	    = 1,
	},
	{
		.reg_domain = IW_REG_DOMAIN_ISRAEL,
		.min	    = 3,
		.max	    = 9,
		.deflt	    = 9,
	},
};

/**
 * iw_valid_channel - validate channel in regulatory domain
 * @reg_comain - regulatory domain
 * @channel - channel to validate
 *
 * Returns 0 if invalid in the specified regulatory domain, non-zero if valid.
 */
static int iw_valid_channel(int reg_domain, int channel)
{
	int i, rc = 0;

	for (i = 0; i < ARRAY_SIZE(iw_channel_table); i++)
		if (reg_domain == iw_channel_table[i].reg_domain) {
			rc = channel >= iw_channel_table[i].min &&
			     channel <= iw_channel_table[i].max;
			break;
		}
	return rc;
}

/**
 * iw_default_channel - get default channel for a regulatory domain
 * @reg_comain - regulatory domain
 *
 * Returns the default channel for a regulatory domain
 */
static int iw_default_channel(int reg_domain)
{
	int i, rc = 1;

	for (i = 0; i < ARRAY_SIZE(iw_channel_table); i++)
		if (reg_domain == iw_channel_table[i].reg_domain) {
			rc = iw_channel_table[i].deflt;
			break;
		}
	return rc;
}

static void iw_set_mgmt_info_element(enum iw_mgmt_info_element_ids id,
				     struct iw_mgmt_info_element *el,
				     void *value, int len)
{
	el->id  = id;
	el->len = len;
	memcpy(el->data, value, len);
}

static void iw_copy_mgmt_info_element(struct iw_mgmt_info_element *to,
				      struct iw_mgmt_info_element *from)
{
	iw_set_mgmt_info_element(from->id, to, from->data, from->len);
}

static inline void wl3501_switch_page(struct wl3501_card *this, u8 page)
{
	wl3501_outb(page, this->base_addr + WL3501_NIC_BSS);
}

/*
 * Get Ethernet MAC addresss.
 *
 * WARNING: We switch to FPAGE0 and switc back again.
 *          Making sure there is no other WL function beening called by ISR.
 */
static int wl3501_get_flash_mac_addr(struct wl3501_card *this)
{
	int base_addr = this->base_addr;

	/* get MAC addr */
	wl3501_outb(WL3501_BSS_FPAGE3, base_addr + WL3501_NIC_BSS); /* BSS */
	wl3501_outb(0x00, base_addr + WL3501_NIC_LMAL);	/* LMAL */
	wl3501_outb(0x40, base_addr + WL3501_NIC_LMAH);	/* LMAH */

	/* wait for reading EEPROM */
	WL3501_NOPLOOP(100);
	this->mac_addr[0] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[1] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[2] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[3] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[4] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[5] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->reg_domain = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	wl3501_outb(WL3501_BSS_FPAGE0, base_addr + WL3501_NIC_BSS);
	wl3501_outb(0x04, base_addr + WL3501_NIC_LMAL);
	wl3501_outb(0x40, base_addr + WL3501_NIC_LMAH);
	WL3501_NOPLOOP(100);
	this->version[0] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->version[1] = inb(base_addr + WL3501_NIC_IODPA);
	/* switch to SRAM Page 0 (for safety) */
	wl3501_switch_page(this, WL3501_BSS_SPAGE0);

	/* The MAC addr should be 00:60:... */
	return this->mac_addr[0] == 0x00 && this->mac_addr[1] == 0x60;
}

/**
 * wl3501_set_to_wla - Move 'size' bytes from PC to card
 * @dest: Card addressing space
 * @src: PC addressing space
 * @size: Bytes to move
 *
 * Move 'size' bytes from PC to card. (Shouldn't be interrupted)
 */
static void wl3501_set_to_wla(struct wl3501_card *this, u16 dest, void *src,
			      int size)
{
	/* switch to SRAM Page 0 */
	wl3501_switch_page(this, (dest & 0x8000) ? WL3501_BSS_SPAGE1 :
						   WL3501_BSS_SPAGE0);
	/* set LMAL and LMAH */
	wl3501_outb(dest & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb(((dest >> 8) & 0x7f), this->base_addr + WL3501_NIC_LMAH);

	/* rep out to Port A */
	wl3501_outsb(this->base_addr + WL3501_NIC_IODPA, src, size);
}

/**
 * wl3501_get_from_wla - Move 'size' bytes from card to PC
 * @src: Card addressing space
 * @dest: PC addressing space
 * @size: Bytes to move
 *
 * Move 'size' bytes from card to PC. (Shouldn't be interrupted)
 */
static void wl3501_get_from_wla(struct wl3501_card *this, u16 src, void *dest,
				int size)
{
	/* switch to SRAM Page 0 */
	wl3501_switch_page(this, (src & 0x8000) ? WL3501_BSS_SPAGE1 :
						  WL3501_BSS_SPAGE0);
	/* set LMAL and LMAH */
	wl3501_outb(src & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb((src >> 8) & 0x7f, this->base_addr + WL3501_NIC_LMAH);

	/* rep get from Port A */
	insb(this->base_addr + WL3501_NIC_IODPA, dest, size);
}

/*
 * Get/Allocate a free Tx Data Buffer
 *
 *  *--------------*-----------------*----------------------------------*
 *  |    PLCP      |    MAC Header   |  DST  SRC         Data ...       |
 *  |  (24 bytes)  |    (30 bytes)   |  (6)  (6)  (Ethernet Row Data)   |
 *  *--------------*-----------------*----------------------------------*
 *  \               \- IEEE 802.11 -/ \-------------- len --------------/
 *   \-struct wl3501_80211_tx_hdr--/   \-------- Ethernet Frame -------/
 *
 * Return = Postion in Card
 */
static u16 wl3501_get_tx_buffer(struct wl3501_card *this, u16 len)
{
	u16 next, blk_cnt = 0, zero = 0;
	u16 full_len = sizeof(struct wl3501_80211_tx_hdr) + len;
	u16 ret = 0;

	if (full_len > this->tx_buffer_cnt * 254)
		goto out;
	ret = this->tx_buffer_head;
	while (full_len) {
		if (full_len < 254)
			full_len = 0;
		else
			full_len -= 254;
		wl3501_get_from_wla(this, this->tx_buffer_head, &next,
				    sizeof(next));
		if (!full_len)
			wl3501_set_to_wla(this, this->tx_buffer_head, &zero,
					  sizeof(zero));
		this->tx_buffer_head = next;
		blk_cnt++;
		/* if buffer is not enough */
		if (!next && full_len) {
			this->tx_buffer_head = ret;
			ret = 0;
			goto out;
		}
	}
	this->tx_buffer_cnt -= blk_cnt;
out:
	return ret;
}

/*
 * Free an allocated Tx Buffer. ptr must be correct position.
 */
static void wl3501_free_tx_buffer(struct wl3501_card *this, u16 ptr)
{
	/* check if all space is not free */
	if (!this->tx_buffer_head)
		this->tx_buffer_head = ptr;
	else
		wl3501_set_to_wla(this, this->tx_buffer_tail,
				  &ptr, sizeof(ptr));
	while (ptr) {
		u16 next;

		this->tx_buffer_cnt++;
		wl3501_get_from_wla(this, ptr, &next, sizeof(next));
		this->tx_buffer_tail = ptr;
		ptr = next;
	}
}

static int wl3501_esbq_req_test(struct wl3501_card *this)
{
	u8 tmp = 0;

	wl3501_get_from_wla(this, this->esbq_req_head + 3, &tmp, sizeof(tmp));
	return tmp & 0x80;
}

static void wl3501_esbq_req(struct wl3501_card *this, u16 *ptr)
{
	u16 tmp = 0;

	wl3501_set_to_wla(this, this->esbq_req_head, ptr, 2);
	wl3501_set_to_wla(this, this->esbq_req_head + 2, &tmp, sizeof(tmp));
	this->esbq_req_head += 4;
	if (this->esbq_req_head >= this->esbq_req_end)
		this->esbq_req_head = this->esbq_req_start;
}

static int wl3501_esbq_exec(struct wl3501_card *this, void *sig, int sig_size)
{
	int rc = -EIO;

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sig_size);
		if (ptr) {
			wl3501_set_to_wla(this, ptr, sig, sig_size);
			wl3501_esbq_req(this, &ptr);
			rc = 0;
		}
	}
	return rc;
}

static int wl3501_get_mib_value(struct wl3501_card *this, u8 index,
				void *bf, int size)
{
	struct wl3501_get_req sig = {
		.sig_id	    = WL3501_SIG_GET_REQ,
		.mib_attrib = index,
	};
	unsigned long flags;
	int rc = -EIO;

	spin_lock_irqsave(&this->lock, flags);
	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(sig));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &sig, sizeof(sig));
			wl3501_esbq_req(this, &ptr);
			this->sig_get_confirm.mib_status = 255;
			spin_unlock_irqrestore(&this->lock, flags);
			rc = wait_event_interruptible(this->wait,
				this->sig_get_confirm.mib_status != 255);
			if (!rc)
				memcpy(bf, this->sig_get_confirm.mib_value,
				       size);
			goto out;
		}
	}
	spin_unlock_irqrestore(&this->lock, flags);
out:
	return rc;
}

static int wl3501_pwr_mgmt(struct wl3501_card *this, int suspend)
{
	struct wl3501_pwr_mgmt_req sig = {
		.sig_id		= WL3501_SIG_PWR_MGMT_REQ,
		.pwr_save	= suspend,
		.wake_up	= !suspend,
		.receive_dtims	= 10,
	};
	unsigned long flags;
	int rc = -EIO;

	spin_lock_irqsave(&this->lock, flags);
	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(sig));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &sig, sizeof(sig));
			wl3501_esbq_req(this, &ptr);
			this->sig_pwr_mgmt_confirm.status = 255;
			spin_unlock_irqrestore(&this->lock, flags);
			rc = wait_event_interruptible(this->wait,
				this->sig_pwr_mgmt_confirm.status != 255);
			printk(KERN_INFO "%s: %s status=%d\n", __func__,
			       suspend ? "suspend" : "resume",
			       this->sig_pwr_mgmt_confirm.status);
			goto out;
		}
	}
	spin_unlock_irqrestore(&this->lock, flags);
out:
	return rc;
}

/**
 * wl3501_send_pkt - Send a packet.
 * @this - card
 *
 * Send a packet.
 *
 * data = Ethernet raw frame.  (e.g. data[0] - data[5] is Dest MAC Addr,
 *                                   data[6] - data[11] is Src MAC Addr)
 * Ref: IEEE 802.11
 */
static int wl3501_send_pkt(struct wl3501_card *this, u8 *data, u16 len)
{
	u16 bf, sig_bf, next, tmplen, pktlen;
	struct wl3501_md_req sig = {
		.sig_id = WL3501_SIG_MD_REQ,
	};
	u8 *pdata = (char *)data;
	int rc = -EIO;

	if (wl3501_esbq_req_test(this)) {
		sig_bf = wl3501_get_tx_buffer(this, sizeof(sig));
		rc = -ENOMEM;
		if (!sig_bf)	/* No free buffer available */
			goto out;
		bf = wl3501_get_tx_buffer(this, len + 26 + 24);
		if (!bf) {
			/* No free buffer available */
			wl3501_free_tx_buffer(this, sig_bf);
			goto out;
		}
		rc = 0;
		memcpy(&sig.daddr[0], pdata, 12);
		pktlen = len - 12;
		pdata += 12;
		sig.data = bf;
		if (((*pdata) * 256 + (*(pdata + 1))) > 1500) {
			u8 addr4[ETH_ALEN] = {
				[0] = 0xAA, [1] = 0xAA, [2] = 0x03, [4] = 0x00,
			};

			wl3501_set_to_wla(this, bf + 2 +
					  offsetof(struct wl3501_tx_hdr, addr4),
					  addr4, sizeof(addr4));
			sig.size = pktlen + 24 + 4 + 6;
			if (pktlen > (254 - sizeof(struct wl3501_tx_hdr))) {
				tmplen = 254 - sizeof(struct wl3501_tx_hdr);
				pktlen -= tmplen;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this,
					  bf + 2 + sizeof(struct wl3501_tx_hdr),
					  pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		} else {
			sig.size = pktlen + 24 + 4 - 2;
			pdata += 2;
			pktlen -= 2;
			if (pktlen > (254 - sizeof(struct wl3501_tx_hdr) + 6)) {
				tmplen = 254 - sizeof(struct wl3501_tx_hdr) + 6;
				pktlen -= tmplen;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this, bf + 2 +
					  offsetof(struct wl3501_tx_hdr, addr4),
					  pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		}
		while (pktlen > 0) {
			if (pktlen > 254) {
				tmplen = 254;
				pktlen -= 254;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this, bf + 2, pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		}
		wl3501_set_to_wla(this, sig_bf, &sig, sizeof(sig));
		wl3501_esbq_req(this, &sig_bf);
	}
out:
	return rc;
}

static int wl3501_mgmt_resync(struct wl3501_card *this)
{
	struct wl3501_resync_req sig = {
		.sig_id = WL3501_SIG_RESYNC_REQ,
	};

	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static inline int wl3501_fw_bss_type(struct wl3501_card *this)
{
	return this->net_type == IW_MODE_INFRA ? WL3501_NET_TYPE_INFRA :
						 WL3501_NET_TYPE_ADHOC;
}

static inline int wl3501_fw_cap_info(struct wl3501_card *this)
{
	return this->net_type == IW_MODE_INFRA ? WL3501_MGMT_CAPABILITY_ESS :
						 WL3501_MGMT_CAPABILITY_IBSS;
}

static int wl3501_mgmt_scan(struct wl3501_card *this, u16 chan_time)
{
	struct wl3501_scan_req sig = {
		.sig_id		= WL3501_SIG_SCAN_REQ,
		.scan_type	= WL3501_SCAN_TYPE_ACTIVE,
		.probe_delay	= 0x10,
		.min_chan_time	= chan_time,
		.max_chan_time	= chan_time,
		.bss_type	= wl3501_fw_bss_type(this),
	};

	this->bss_cnt = this->join_sta_bss = 0;
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_join(struct wl3501_card *this, u16 stas)
{
	struct wl3501_join_req sig = {
		.sig_id		  = WL3501_SIG_JOIN_REQ,
		.timeout	  = 10,
		.ds_pset = {
			.el = {
				.id  = IW_MGMT_INFO_ELEMENT_DS_PARAMETER_SET,
				.len = 1,
			},
			.chan	= this->chan,
		},
	};

	memcpy(&sig.beacon_period, &this->bss_set[stas].beacon_period, 72);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_start(struct wl3501_card *this)
{
	struct wl3501_start_req sig = {
		.sig_id			= WL3501_SIG_START_REQ,
		.beacon_period		= 400,
		.dtim_period		= 1,
		.ds_pset = {
			.el = {
				.id  = IW_MGMT_INFO_ELEMENT_DS_PARAMETER_SET,
				.len = 1,
			},
			.chan	= this->chan,
		},
		.bss_basic_rset	= {
			.el = {
				.id	= IW_MGMT_INFO_ELEMENT_SUPPORTED_RATES,
				.len = 2,
			},
			.data_rate_labels = {
				[0] = IW_MGMT_RATE_LABEL_MANDATORY |
				      IW_MGMT_RATE_LABEL_1MBIT,
				[1] = IW_MGMT_RATE_LABEL_MANDATORY |
				      IW_MGMT_RATE_LABEL_2MBIT,
			},
		},
		.operational_rset	= {
			.el = {
				.id	= IW_MGMT_INFO_ELEMENT_SUPPORTED_RATES,
				.len = 2,
			},
			.data_rate_labels = {
				[0] = IW_MGMT_RATE_LABEL_MANDATORY |
				      IW_MGMT_RATE_LABEL_1MBIT,
				[1] = IW_MGMT_RATE_LABEL_MANDATORY |
				      IW_MGMT_RATE_LABEL_2MBIT,
			},
		},
		.ibss_pset		= {
			.el = {
				.id	 = IW_MGMT_INFO_ELEMENT_IBSS_PARAMETER_SET,
				.len     = 2,
			},
			.atim_window = 10,
		},
		.bss_type		= wl3501_fw_bss_type(this),
		.cap_info		= wl3501_fw_cap_info(this),
	};

	iw_copy_mgmt_info_element(&sig.ssid.el, &this->essid.el);
	iw_copy_mgmt_info_element(&this->keep_essid.el, &this->essid.el);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static void wl3501_mgmt_scan_confirm(struct wl3501_card *this, u16 addr)
{
	u16 i = 0;
	int matchflag = 0;
	struct wl3501_scan_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS) {
		dprintk(3, "success");
		if ((this->net_type == IW_MODE_INFRA &&
		     (sig.cap_info & WL3501_MGMT_CAPABILITY_ESS)) ||
		    (this->net_type == IW_MODE_ADHOC &&
		     (sig.cap_info & WL3501_MGMT_CAPABILITY_IBSS)) ||
		    this->net_type == IW_MODE_AUTO) {
			if (!this->essid.el.len)
				matchflag = 1;
			else if (this->essid.el.len == 3 &&
				 !memcmp(this->essid.essid, "ANY", 3))
				matchflag = 1;
			else if (this->essid.el.len != sig.ssid.el.len)
				matchflag = 0;
			else if (memcmp(this->essid.essid, sig.ssid.essid,
					this->essid.el.len))
				matchflag = 0;
			else
				matchflag = 1;
			if (matchflag) {
				for (i = 0; i < this->bss_cnt; i++) {
					if (!memcmp(this->bss_set[i].bssid,
						    sig.bssid, ETH_ALEN)) {
						matchflag = 0;
						break;
					}
				}
			}
			if (matchflag && (i < 20)) {
				memcpy(&this->bss_set[i].beacon_period,
				       &sig.beacon_period, 73);
				this->bss_cnt++;
				this->rssi = sig.rssi;
			}
		}
	} else if (sig.status == WL3501_STATUS_TIMEOUT) {
		dprintk(3, "timeout");
		this->join_sta_bss = 0;
		for (i = this->join_sta_bss; i < this->bss_cnt; i++)
			if (!wl3501_mgmt_join(this, i))
				break;
		this->join_sta_bss = i;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == IW_MODE_INFRA)
				wl3501_mgmt_scan(this, 100);
			else {
				this->adhoc_times++;
				if (this->adhoc_times > WL3501_MAX_ADHOC_TRIES)
					wl3501_mgmt_start(this);
				else
					wl3501_mgmt_scan(this, 100);
			}
		}
	}
}

/**
 * wl3501_block_interrupt - Mask interrupt from SUTRO
 * @this - card
 *
 * Mask interrupt from SUTRO. (i.e. SUTRO cannot interrupt the HOST)
 * Return: 1 if interrupt is originally enabled
 */
static int wl3501_block_interrupt(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = old & (~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC |
			WL3501_GCR_ENECINT));

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return old & WL3501_GCR_ENECINT;
}

/**
 * wl3501_unblock_interrupt - Enable interrupt from SUTRO
 * @this - card
 *
 * Enable interrupt from SUTRO. (i.e. SUTRO can interrupt the HOST)
 * Return: 1 if interrupt is originally enabled
 */
static int wl3501_unblock_interrupt(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = (old & ~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC)) |
		  WL3501_GCR_ENECINT;

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return old & WL3501_GCR_ENECINT;
}

/**
 * wl3501_receive - Receive data from Receive Queue.
 *
 * Receive data from Receive Queue.
 *
 * @this: card
 * @bf: address of host
 * @size: size of buffer.
 */
static u16 wl3501_receive(struct wl3501_card *this, u8 *bf, u16 size)
{
	u16 next_addr, next_addr1;
	u8 *data = bf + 12;

	size -= 12;
	wl3501_get_from_wla(this, this->start_seg + 2,
			    &next_addr, sizeof(next_addr));
	if (size > WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr)) {
		wl3501_get_from_wla(this,
				    this->start_seg +
					sizeof(struct wl3501_rx_hdr), data,
				    WL3501_BLKSZ -
					sizeof(struct wl3501_rx_hdr));
		size -= WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
		data += WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
	} else {
		wl3501_get_from_wla(this,
				    this->start_seg +
					sizeof(struct wl3501_rx_hdr),
				    data, size);
		size = 0;
	}
	while (size > 0) {
		if (size > WL3501_BLKSZ - 5) {
			wl3501_get_from_wla(this, next_addr + 5, data,
					    WL3501_BLKSZ - 5);
			size -= WL3501_BLKSZ - 5;
			data += WL3501_BLKSZ - 5;
			wl3501_get_from_wla(this, next_addr + 2, &next_addr1,
					    sizeof(next_addr1));
			next_addr = next_addr1;
		} else {
			wl3501_get_from_wla(this, next_addr + 5, data, size);
			size = 0;
		}
	}
	return 0;
}

static void wl3501_esbq_req_free(struct wl3501_card *this)
{
	u8 tmp;
	u16 addr;

	if (this->esbq_req_head == this->esbq_req_tail)
		goto out;
	wl3501_get_from_wla(this, this->esbq_req_tail + 3, &tmp, sizeof(tmp));
	if (!(tmp & 0x80))
		goto out;
	wl3501_get_from_wla(this, this->esbq_req_tail, &addr, sizeof(addr));
	wl3501_free_tx_buffer(this, addr);
	this->esbq_req_tail += 4;
	if (this->esbq_req_tail >= this->esbq_req_end)
		this->esbq_req_tail = this->esbq_req_start;
out:
	return;
}

static int wl3501_esbq_confirm(struct wl3501_card *this)
{
	u8 tmp;

	wl3501_get_from_wla(this, this->esbq_confirm + 3, &tmp, sizeof(tmp));
	return tmp & 0x80;
}

static void wl3501_online(struct net_device *dev)
{
	struct wl3501_card *this = netdev_priv(dev);

	printk(KERN_INFO "%s: Wireless LAN online. BSSID: %pM\n",
	       dev->name, this->bssid);
	netif_wake_queue(dev);
}

static void wl3501_esbq_confirm_done(struct wl3501_card *this)
{
	u8 tmp = 0;

	wl3501_set_to_wla(this, this->esbq_confirm + 3, &tmp, sizeof(tmp));
	this->esbq_confirm += 4;
	if (this->esbq_confirm >= this->esbq_confirm_end)
		this->esbq_confirm = this->esbq_confirm_start;
}

static int wl3501_mgmt_auth(struct wl3501_card *this)
{
	struct wl3501_auth_req sig = {
		.sig_id	 = WL3501_SIG_AUTH_REQ,
		.type	 = WL3501_SYS_TYPE_OPEN,
		.timeout = 1000,
	};

	dprintk(3, "entry");
	memcpy(sig.mac_addr, this->bssid, ETH_ALEN);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_association(struct wl3501_card *this)
{
	struct wl3501_assoc_req sig = {
		.sig_id		 = WL3501_SIG_ASSOC_REQ,
		.timeout	 = 1000,
		.listen_interval = 5,
		.cap_info	 = this->cap_info,
	};

	dprintk(3, "entry");
	memcpy(sig.mac_addr, this->bssid, ETH_ALEN);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static void wl3501_mgmt_join_confirm(struct net_device *dev, u16 addr)
{
	struct wl3501_card *this = netdev_priv(dev);
	struct wl3501_join_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS) {
		if (this->net_type == IW_MODE_INFRA) {
			if (this->join_sta_bss < this->bss_cnt) {
				const int i = this->join_sta_bss;
				memcpy(this->bssid,
				       this->bss_set[i].bssid, ETH_ALEN);
				this->chan = this->bss_set[i].ds_pset.chan;
				iw_copy_mgmt_info_element(&this->keep_essid.el,
						     &this->bss_set[i].ssid.el);
				wl3501_mgmt_auth(this);
			}
		} else {
			const int i = this->join_sta_bss;

			memcpy(&this->bssid, &this->bss_set[i].bssid, ETH_ALEN);
			this->chan = this->bss_set[i].ds_pset.chan;
			iw_copy_mgmt_info_element(&this->keep_essid.el,
						  &this->bss_set[i].ssid.el);
			wl3501_online(dev);
		}
	} else {
		int i;
		this->join_sta_bss++;
		for (i = this->join_sta_bss; i < this->bss_cnt; i++)
			if (!wl3501_mgmt_join(this, i))
				break;
		this->join_sta_bss = i;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == IW_MODE_INFRA)
				wl3501_mgmt_scan(this, 100);
			else {
				this->adhoc_times++;
				if (this->adhoc_times > WL3501_MAX_ADHOC_TRIES)
					wl3501_mgmt_start(this);
				else
					wl3501_mgmt_scan(this, 100);
			}
		}
	}
}

static inline void wl3501_alarm_interrupt(struct net_device *dev,
					  struct wl3501_card *this)
{
	if (this->net_type == IW_MODE_INFRA) {
		printk(KERN_INFO "Wireless LAN offline\n");
		netif_stop_queue(dev);
		wl3501_mgmt_resync(this);
	}
}

static inline void wl3501_md_confirm_interrupt(struct net_device *dev,
					       struct wl3501_card *this,
					       u16 addr)
{
	struct wl3501_md_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	wl3501_free_tx_buffer(this, sig.data);
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
}

static inline void wl3501_md_ind_interrupt(struct net_device *dev,
					   struct wl3501_card *this, u16 addr)
{
	struct wl3501_md_ind sig;
	struct sk_buff *skb;
	u8 rssi, addr4[ETH_ALEN];
	u16 pkt_len;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	this->start_seg = sig.data;
	wl3501_get_from_wla(this,
			    sig.data + offsetof(struct wl3501_rx_hdr, rssi),
			    &rssi, sizeof(rssi));
	this->rssi = rssi <= 63 ? (rssi * 100) / 64 : 255;

	wl3501_get_from_wla(this,
			    sig.data +
				offsetof(struct wl3501_rx_hdr, addr4),
			    &addr4, sizeof(addr4));
	if (!(addr4[0] == 0xAA && addr4[1] == 0xAA &&
	      addr4[2] == 0x03 && addr4[4] == 0x00)) {
		printk(KERN_INFO "Insupported packet type!\n");
		return;
	}
	pkt_len = sig.size + 12 - 24 - 4 - 6;

	skb = dev_alloc_skb(pkt_len + 5);

	if (!skb) {
		printk(KERN_WARNING "%s: Can't alloc a sk_buff of size %d.\n",
		       dev->name, pkt_len);
		dev->stats.rx_dropped++;
	} else {
		skb->dev = dev;
		skb_reserve(skb, 2); /* IP headers on 16 bytes boundaries */
		skb_copy_to_linear_data(skb, (unsigned char *)&sig.daddr, 12);
		wl3501_receive(this, skb->data, pkt_len);
		skb_put(skb, pkt_len);
		skb->protocol	= eth_type_trans(skb, dev);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		netif_rx(skb);
	}
}

static inline void wl3501_get_confirm_interrupt(struct wl3501_card *this,
						u16 addr, void *sig, int size)
{
	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &this->sig_get_confirm,
			    sizeof(this->sig_get_confirm));
	wake_up(&this->wait);
}

static inline void wl3501_start_confirm_interrupt(struct net_device *dev,
						  struct wl3501_card *this,
						  u16 addr)
{
	struct wl3501_start_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS)
		netif_wake_queue(dev);
}

static inline void wl3501_assoc_confirm_interrupt(struct net_device *dev,
						  u16 addr)
{
	struct wl3501_card *this = netdev_priv(dev);
	struct wl3501_assoc_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));

	if (sig.status == WL3501_STATUS_SUCCESS)
		wl3501_online(dev);
}

static inline void wl3501_auth_confirm_interrupt(struct wl3501_card *this,
						 u16 addr)
{
	struct wl3501_auth_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));

	if (sig.status == WL3501_STATUS_SUCCESS)
		wl3501_mgmt_association(this);
	else
		wl3501_mgmt_resync(this);
}

static inline void wl3501_rx_interrupt(struct net_device *dev)
{
	int morepkts;
	u16 addr;
	u8 sig_id;
	struct wl3501_card *this = netdev_priv(dev);

	dprintk(3, "entry");
loop:
	morepkts = 0;
	if (!wl3501_esbq_confirm(this))
		goto free;
	wl3501_get_from_wla(this, this->esbq_confirm, &addr, sizeof(addr));
	wl3501_get_from_wla(this, addr + 2, &sig_id, sizeof(sig_id));

	switch (sig_id) {
	case WL3501_SIG_DEAUTH_IND:
	case WL3501_SIG_DISASSOC_IND:
	case WL3501_SIG_ALARM:
		wl3501_alarm_interrupt(dev, this);
		break;
	case WL3501_SIG_MD_CONFIRM:
		wl3501_md_confirm_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_MD_IND:
		wl3501_md_ind_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_GET_CONFIRM:
		wl3501_get_confirm_interrupt(this, addr,
					     &this->sig_get_confirm,
					     sizeof(this->sig_get_confirm));
		break;
	case WL3501_SIG_PWR_MGMT_CONFIRM:
		wl3501_get_confirm_interrupt(this, addr,
					     &this->sig_pwr_mgmt_confirm,
					    sizeof(this->sig_pwr_mgmt_confirm));
		break;
	case WL3501_SIG_START_CONFIRM:
		wl3501_start_confirm_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_SCAN_CONFIRM:
		wl3501_mgmt_scan_confirm(this, addr);
		break;
	case WL3501_SIG_JOIN_CONFIRM:
		wl3501_mgmt_join_confirm(dev, addr);
		break;
	case WL3501_SIG_ASSOC_CONFIRM:
		wl3501_assoc_confirm_interrupt(dev, addr);
		break;
	case WL3501_SIG_AUTH_CONFIRM:
		wl3501_auth_confirm_interrupt(this, addr);
		break;
	case WL3501_SIG_RESYNC_CONFIRM:
		wl3501_mgmt_resync(this); /* FIXME: should be resync_confirm */
		break;
	}
	wl3501_esbq_confirm_done(this);
	morepkts = 1;
	/* free request if necessary */
free:
	wl3501_esbq_req_free(this);
	if (morepkts)
		goto loop;
}

static inline void wl3501_ack_interrupt(struct wl3501_card *this)
{
	wl3501_outb(WL3501_GCR_ECINT, this->base_addr + WL3501_NIC_GCR);
}

/**
 * wl3501_interrupt - Hardware interrupt from card.
 * @irq - Interrupt number
 * @dev_id - net_device
 *
 * We must acknowledge the interrupt as soon as possible, and block the
 * interrupt from the same card immediately to prevent re-entry.
 *
 * Before accessing the Control_Status_Block, we must lock SUTRO first.
 * On the other hand, to prevent SUTRO from malfunctioning, we must
 * unlock the SUTRO as soon as possible.
 */
static irqreturn_t wl3501_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct wl3501_card *this;

	this = netdev_priv(dev);
	spin_lock(&this->lock);
	wl3501_ack_interrupt(this);
	wl3501_block_interrupt(this);
	wl3501_rx_interrupt(dev);
	wl3501_unblock_interrupt(this);
	spin_unlock(&this->lock);

	return IRQ_HANDLED;
}

static int wl3501_reset_board(struct wl3501_card *this)
{
	u8 tmp = 0;
	int i, rc = 0;

	/* Coreset */
	wl3501_outb_p(WL3501_GCR_CORESET, this->base_addr + WL3501_NIC_GCR);
	wl3501_outb_p(0, this->base_addr + WL3501_NIC_GCR);
	wl3501_outb_p(WL3501_GCR_CORESET, this->base_addr + WL3501_NIC_GCR);

	/* Reset SRAM 0x480 to zero */
	wl3501_set_to_wla(this, 0x480, &tmp, sizeof(tmp));

	/* Start up */
	wl3501_outb_p(0, this->base_addr + WL3501_NIC_GCR);

	WL3501_NOPLOOP(1024 * 50);

	wl3501_unblock_interrupt(this);	/* acme: was commented */

	/* Polling Self_Test_Status */
	for (i = 0; i < 10000; i++) {
		wl3501_get_from_wla(this, 0x480, &tmp, sizeof(tmp));

		if (tmp == 'W') {
			/* firmware complete all test successfully */
			tmp = 'A';
			wl3501_set_to_wla(this, 0x480, &tmp, sizeof(tmp));
			goto out;
		}
		WL3501_NOPLOOP(10);
	}
	printk(KERN_WARNING "%s: failed to reset the board!\n", __func__);
	rc = -ENODEV;
out:
	return rc;
}

static int wl3501_init_firmware(struct wl3501_card *this)
{
	u16 ptr, next;
	int rc = wl3501_reset_board(this);

	if (rc)
		goto fail;
	this->card_name[0] = '\0';
	wl3501_get_from_wla(this, 0x1a00,
			    this->card_name, sizeof(this->card_name));
	this->card_name[sizeof(this->card_name) - 1] = '\0';
	this->firmware_date[0] = '\0';
	wl3501_get_from_wla(this, 0x1a40,
			    this->firmware_date, sizeof(this->firmware_date));
	this->firmware_date[sizeof(this->firmware_date) - 1] = '\0';
	/* Switch to SRAM Page 0 */
	wl3501_switch_page(this, WL3501_BSS_SPAGE0);
	/* Read parameter from card */
	wl3501_get_from_wla(this, 0x482, &this->esbq_req_start, 2);
	wl3501_get_from_wla(this, 0x486, &this->esbq_req_end, 2);
	wl3501_get_from_wla(this, 0x488, &this->esbq_confirm_start, 2);
	wl3501_get_from_wla(this, 0x48c, &this->esbq_confirm_end, 2);
	wl3501_get_from_wla(this, 0x48e, &this->tx_buffer_head, 2);
	wl3501_get_from_wla(this, 0x492, &this->tx_buffer_size, 2);
	this->esbq_req_tail	= this->esbq_req_head = this->esbq_req_start;
	this->esbq_req_end     += this->esbq_req_start;
	this->esbq_confirm	= this->esbq_confirm_start;
	this->esbq_confirm_end += this->esbq_confirm_start;
	/* Initial Tx Buffer */
	this->tx_buffer_cnt = 1;
	ptr = this->tx_buffer_head;
	next = ptr + WL3501_BLKSZ;
	while ((next - this->tx_buffer_head) < this->tx_buffer_size) {
		this->tx_buffer_cnt++;
		wl3501_set_to_wla(this, ptr, &next, sizeof(next));
		ptr = next;
		next = ptr + WL3501_BLKSZ;
	}
	rc = 0;
	next = 0;
	wl3501_set_to_wla(this, ptr, &next, sizeof(next));
	this->tx_buffer_tail = ptr;
out:
	return rc;
fail:
	printk(KERN_WARNING "%s: failed!\n", __func__);
	goto out;
}

static int wl3501_close(struct net_device *dev)
{
	struct wl3501_card *this = netdev_priv(dev);
	int rc = -ENODEV;
	unsigned long flags;
	struct pcmcia_device *link;
	link = this->p_dev;

	spin_lock_irqsave(&this->lock, flags);
	link->open--;

	/* Stop wl3501_hard_start_xmit() from now on */
	netif_stop_queue(dev);
	wl3501_ack_interrupt(this);

	/* Mask interrupts from the SUTRO */
	wl3501_block_interrupt(this);

	rc = 0;
	printk(KERN_INFO "%s: WL3501 closed\n", dev->name);
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
}

/**
 * wl3501_reset - Reset the SUTRO.
 * @dev - network device
 *
 * It is almost the same as wl3501_open(). In fact, we may just wl3501_close()
 * and wl3501_open() again, but I wouldn't like to free_irq() when the driver
 * is running. It seems to be dangerous.
 */
static int wl3501_reset(struct net_device *dev)
{
	struct wl3501_card *this = netdev_priv(dev);
	int rc = -ENODEV;

	wl3501_block_interrupt(this);

	if (wl3501_init_firmware(this)) {
		printk(KERN_WARNING "%s: Can't initialize Firmware!\n",
		       dev->name);
		/* Free IRQ, and mark IRQ as unused */
		free_irq(dev->irq, dev);
		goto out;
	}

	/*
	 * Queue has to be started only when the Card is Started
	 */
	netif_stop_queue(dev);
	this->adhoc_times = 0;
	wl3501_ack_interrupt(this);
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	dprintk(1, "%s: device reset", dev->name);
	rc = 0;
out:
	return rc;
}

static void wl3501_tx_timeout(struct net_device *dev)
{
	struct wl3501_card *this = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned long flags;
	int rc;

	stats->tx_errors++;
	spin_lock_irqsave(&this->lock, flags);
	rc = wl3501_reset(dev);
	spin_unlock_irqrestore(&this->lock, flags);
	if (rc)
		printk(KERN_ERR "%s: Error %d resetting card on Tx timeout!\n",
		       dev->name, rc);
	else {
		dev->trans_start = jiffies;
		netif_wake_queue(dev);
	}
}

/*
 * Return : 0 - OK
 *	    1 - Could not transmit (dev_queue_xmit will queue it)
 *		and try to sent it later
 */
static netdev_tx_t wl3501_hard_start_xmit(struct sk_buff *skb,
						struct net_device *dev)
{
	int enabled, rc;
	struct wl3501_card *this = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&this->lock, flags);
	enabled = wl3501_block_interrupt(this);
	dev->trans_start = jiffies;
	rc = wl3501_send_pkt(this, skb->data, skb->len);
	if (enabled)
		wl3501_unblock_interrupt(this);
	if (rc) {
		++dev->stats.tx_dropped;
		netif_stop_queue(dev);
	} else {
		++dev->stats.tx_packets;
		dev->stats.tx_bytes += skb->len;
		kfree_skb(skb);

		if (this->tx_buffer_cnt < 2)
			netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&this->lock, flags);
	return NETDEV_TX_OK;
}

static int wl3501_open(struct net_device *dev)
{
	int rc = -ENODEV;
	struct wl3501_card *this = netdev_priv(dev);
	unsigned long flags;
	struct pcmcia_device *link;
	link = this->p_dev;

	spin_lock_irqsave(&this->lock, flags);
	if (!pcmcia_dev_present(link))
		goto out;
	netif_device_attach(dev);
	link->open++;

	/* Initial WL3501 firmware */
	dprintk(1, "%s: Initialize WL3501 firmware...", dev->name);
	if (wl3501_init_firmware(this))
		goto fail;
	/* Initial device variables */
	this->adhoc_times = 0;
	/* Acknowledge Interrupt, for cleaning last state */
	wl3501_ack_interrupt(this);

	/* Enable interrupt from card after all */
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	rc = 0;
	dprintk(1, "%s: WL3501 opened", dev->name);
	printk(KERN_INFO "%s: Card Name: %s\n"
			 "%s: Firmware Date: %s\n",
			 dev->name, this->card_name,
			 dev->name, this->firmware_date);
out:
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
fail:
	printk(KERN_WARNING "%s: Can't initialize firmware!\n", dev->name);
	goto out;
}

static struct iw_statistics *wl3501_get_wireless_stats(struct net_device *dev)
{
	struct wl3501_card *this = netdev_priv(dev);
	struct iw_statistics *wstats = &this->wstats;
	u32 value; /* size checked: it is u32 */

	memset(wstats, 0, sizeof(*wstats));
	wstats->status = netif_running(dev);
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_ICV_ERROR_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_UNDECRYPTABLE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_EXCLUDED_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_RETRY_COUNT,
				  &value, sizeof(value)))
		wstats->discard.retries	= value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_FAILED_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_RTS_FAILURE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_ACK_FAILURE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_FRAME_DUPLICATE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	return wstats;
}

static void wl3501_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, wl3501_dev_info, sizeof(info->driver));
}

static const struct ethtool_ops ops = {
	.get_drvinfo = wl3501_get_drvinfo
};

/**
 * wl3501_detach - deletes a driver "instance"
 * @link - FILL_IN
 *
 * This deletes a driver "instance". The device is de-registered with Card
 * Services. If it has been released, all local data structures are freed.
 * Otherwise, the structures will be freed when the device is released.
 */
static void wl3501_detach(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	/* If the device is currently configured and active, we won't actually
	 * delete it yet.  Instead, it is marked so that when the release()
	 * function is called, that will trigger a proper detach(). */

	while (link->open > 0)
		wl3501_close(dev);

	netif_device_detach(dev);
	wl3501_release(link);

	if (link->priv)
		free_netdev(link->priv);

	return;
}

static int wl3501_get_name(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	strlcpy(wrqu->name, "IEEE 802.11-DS", sizeof(wrqu->name));
	return 0;
}

static int wl3501_set_freq(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);
	int channel = wrqu->freq.m;
	int rc = -EINVAL;

	if (iw_valid_channel(this->reg_domain, channel)) {
		this->chan = channel;
		rc = wl3501_reset(dev);
	}
	return rc;
}

static int wl3501_get_freq(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	wrqu->freq.m = ieee80211_dsss_chan_to_freq(this->chan) * 100000;
	wrqu->freq.e = 1;
	return 0;
}

static int wl3501_set_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	int rc = -EINVAL;

	if (wrqu->mode == IW_MODE_INFRA ||
	    wrqu->mode == IW_MODE_ADHOC ||
	    wrqu->mode == IW_MODE_AUTO) {
		struct wl3501_card *this = netdev_priv(dev);

		this->net_type = wrqu->mode;
		rc = wl3501_reset(dev);
	}
	return rc;
}

static int wl3501_get_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	wrqu->mode = this->net_type;
	return 0;
}

static int wl3501_get_sens(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	wrqu->sens.value = this->rssi;
	wrqu->sens.disabled = !wrqu->sens.value;
	wrqu->sens.fixed = 1;
	return 0;
}

static int wl3501_get_range(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;

	/* Set the length (very important for backward compatibility) */
	wrqu->data.length = sizeof(*range);

	/* Set all the info we don't care or don't know about to zero */
	memset(range, 0, sizeof(*range));

	/* Set the Wireless Extension versions */
	range->we_version_compiled	= WIRELESS_EXT;
	range->we_version_source	= 1;
	range->throughput		= 2 * 1000 * 1000;     /* ~2 Mb/s */
	/* FIXME: study the code to fill in more fields... */
	return 0;
}

static int wl3501_set_wap(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);
	static const u8 bcast[ETH_ALEN] = { 255, 255, 255, 255, 255, 255 };
	int rc = -EINVAL;

	/* FIXME: we support other ARPHRDs...*/
	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		goto out;
	if (!memcmp(bcast, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		/* FIXME: rescan? */
	} else
		memcpy(this->bssid, wrqu->ap_addr.sa_data, ETH_ALEN);
		/* FIXME: rescan? deassoc & scan? */
	rc = 0;
out:
	return rc;
}

static int wl3501_get_wap(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu->ap_addr.sa_data, this->bssid, ETH_ALEN);
	return 0;
}

static int wl3501_set_scan(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	/*
	 * FIXME: trigger scanning with a reset, yes, I'm lazy
	 */
	return wl3501_reset(dev);
}

static int wl3501_get_scan(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);
	int i;
	char *current_ev = extra;
	struct iw_event iwe;

	for (i = 0; i < this->bss_cnt; ++i) {
		iwe.cmd			= SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, this->bss_set[i].bssid, ETH_ALEN);
		current_ev = iwe_stream_add_event(info, current_ev,
						  extra + IW_SCAN_MAX_DATA,
						  &iwe, IW_EV_ADDR_LEN);
		iwe.cmd		  = SIOCGIWESSID;
		iwe.u.data.flags  = 1;
		iwe.u.data.length = this->bss_set[i].ssid.el.len;
		current_ev = iwe_stream_add_point(info, current_ev,
						  extra + IW_SCAN_MAX_DATA,
						  &iwe,
						  this->bss_set[i].ssid.essid);
		iwe.cmd	   = SIOCGIWMODE;
		iwe.u.mode = this->bss_set[i].bss_type;
		current_ev = iwe_stream_add_event(info, current_ev,
						  extra + IW_SCAN_MAX_DATA,
						  &iwe, IW_EV_UINT_LEN);
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = this->bss_set[i].ds_pset.chan;
		iwe.u.freq.e = 0;
		current_ev = iwe_stream_add_event(info, current_ev,
						  extra + IW_SCAN_MAX_DATA,
						  &iwe, IW_EV_FREQ_LEN);
		iwe.cmd = SIOCGIWENCODE;
		if (this->bss_set[i].cap_info & WL3501_MGMT_CAPABILITY_PRIVACY)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		current_ev = iwe_stream_add_point(info, current_ev,
						  extra + IW_SCAN_MAX_DATA,
						  &iwe, NULL);
	}
	/* Length of data */
	wrqu->data.length = (current_ev - extra);
	wrqu->data.flags = 0; /* FIXME: set properly these flags */
	return 0;
}

static int wl3501_set_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	if (wrqu->data.flags) {
		iw_set_mgmt_info_element(IW_MGMT_INFO_ELEMENT_SSID,
					 &this->essid.el,
					 extra, wrqu->data.length);
	} else { /* We accept any ESSID */
		iw_set_mgmt_info_element(IW_MGMT_INFO_ELEMENT_SSID,
					 &this->essid.el, "ANY", 3);
	}
	return wl3501_reset(dev);
}

static int wl3501_get_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&this->lock, flags);
	wrqu->essid.flags  = 1;
	wrqu->essid.length = this->essid.el.len;
	memcpy(extra, this->essid.essid, this->essid.el.len);
	spin_unlock_irqrestore(&this->lock, flags);
	return 0;
}

static int wl3501_set_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	if (wrqu->data.length > sizeof(this->nick))
		return -E2BIG;
	strlcpy(this->nick, extra, wrqu->data.length);
	return 0;
}

static int wl3501_get_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = netdev_priv(dev);

	strlcpy(extra, this->nick, 32);
	wrqu->data.length = strlen(extra);
	return 0;
}

static int wl3501_get_rate(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	/*
	 * FIXME: have to see from where to get this info, perhaps this card
	 * works at 1 Mbit/s too... for now leave at 2 Mbit/s that is the most
	 * common with the Planet Access Points. -acme
	 */
	wrqu->bitrate.value = 2000000;
	wrqu->bitrate.fixed = 1;
	return 0;
}

static int wl3501_get_rts_threshold(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	u16 threshold; /* size checked: it is u16 */
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_RTS_THRESHOLD,
				      &threshold, sizeof(threshold));
	if (!rc) {
		wrqu->rts.value = threshold;
		wrqu->rts.disabled = threshold >= 2347;
		wrqu->rts.fixed = 1;
	}
	return rc;
}

static int wl3501_get_frag_threshold(struct net_device *dev,
				     struct iw_request_info *info,
				     union iwreq_data *wrqu, char *extra)
{
	u16 threshold; /* size checked: it is u16 */
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_FRAG_THRESHOLD,
				      &threshold, sizeof(threshold));
	if (!rc) {
		wrqu->frag.value = threshold;
		wrqu->frag.disabled = threshold >= 2346;
		wrqu->frag.fixed = 1;
	}
	return rc;
}

static int wl3501_get_txpow(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u16 txpow;
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_CURRENT_TX_PWR_LEVEL,
				      &txpow, sizeof(txpow));
	if (!rc) {
		wrqu->txpower.value = txpow;
		wrqu->txpower.disabled = 0;
		/*
		 * From the MIB values I think this can be configurable,
		 * as it lists several tx power levels -acme
		 */
		wrqu->txpower.fixed = 0;
		wrqu->txpower.flags = IW_TXPOW_MWATT;
	}
	return rc;
}

static int wl3501_get_retry(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u8 retry; /* size checked: it is u8 */
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_LONG_RETRY_LIMIT,
				      &retry, sizeof(retry));
	if (rc)
		goto out;
	if (wrqu->retry.flags & IW_RETRY_LONG) {
		wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
		goto set_value;
	}
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_SHORT_RETRY_LIMIT,
				  &retry, sizeof(retry));
	if (rc)
		goto out;
	wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_SHORT;
set_value:
	wrqu->retry.value = retry;
	wrqu->retry.disabled = 0;
out:
	return rc;
}

static int wl3501_get_encode(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u8 implemented, restricted, keys[100], len_keys, tocopy;
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_PRIV_OPT_IMPLEMENTED,
				      &implemented, sizeof(implemented));
	if (rc)
		goto out;
	if (!implemented) {
		wrqu->encoding.flags = IW_ENCODE_DISABLED;
		goto out;
	}
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_EXCLUDE_UNENCRYPTED,
				  &restricted, sizeof(restricted));
	if (rc)
		goto out;
	wrqu->encoding.flags = restricted ? IW_ENCODE_RESTRICTED :
					    IW_ENCODE_OPEN;
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_KEY_MAPPINGS_LEN,
				  &len_keys, sizeof(len_keys));
	if (rc)
		goto out;
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_KEY_MAPPINGS,
				  keys, len_keys);
	if (rc)
		goto out;
	tocopy = min_t(u8, len_keys, wrqu->encoding.length);
	tocopy = min_t(u8, tocopy, 100);
	wrqu->encoding.length = tocopy;
	memcpy(extra, keys, tocopy);
out:
	return rc;
}

static int wl3501_get_power(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u8 pwr_state;
	struct wl3501_card *this = netdev_priv(dev);
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_CURRENT_PWR_STATE,
				      &pwr_state, sizeof(pwr_state));
	if (rc)
		goto out;
	wrqu->power.disabled = !pwr_state;
	wrqu->power.flags = IW_POWER_ON;
out:
	return rc;
}

static const iw_handler	wl3501_handler[] = {
	[SIOCGIWNAME	- SIOCIWFIRST] = wl3501_get_name,
	[SIOCSIWFREQ	- SIOCIWFIRST] = wl3501_set_freq,
	[SIOCGIWFREQ	- SIOCIWFIRST] = wl3501_get_freq,
	[SIOCSIWMODE	- SIOCIWFIRST] = wl3501_set_mode,
	[SIOCGIWMODE	- SIOCIWFIRST] = wl3501_get_mode,
	[SIOCGIWSENS	- SIOCIWFIRST] = wl3501_get_sens,
	[SIOCGIWRANGE	- SIOCIWFIRST] = wl3501_get_range,
	[SIOCSIWSPY	- SIOCIWFIRST] = iw_handler_set_spy,
	[SIOCGIWSPY	- SIOCIWFIRST] = iw_handler_get_spy,
	[SIOCSIWTHRSPY	- SIOCIWFIRST] = iw_handler_set_thrspy,
	[SIOCGIWTHRSPY	- SIOCIWFIRST] = iw_handler_get_thrspy,
	[SIOCSIWAP	- SIOCIWFIRST] = wl3501_set_wap,
	[SIOCGIWAP	- SIOCIWFIRST] = wl3501_get_wap,
	[SIOCSIWSCAN	- SIOCIWFIRST] = wl3501_set_scan,
	[SIOCGIWSCAN	- SIOCIWFIRST] = wl3501_get_scan,
	[SIOCSIWESSID	- SIOCIWFIRST] = wl3501_set_essid,
	[SIOCGIWESSID	- SIOCIWFIRST] = wl3501_get_essid,
	[SIOCSIWNICKN	- SIOCIWFIRST] = wl3501_set_nick,
	[SIOCGIWNICKN	- SIOCIWFIRST] = wl3501_get_nick,
	[SIOCGIWRATE	- SIOCIWFIRST] = wl3501_get_rate,
	[SIOCGIWRTS	- SIOCIWFIRST] = wl3501_get_rts_threshold,
	[SIOCGIWFRAG	- SIOCIWFIRST] = wl3501_get_frag_threshold,
	[SIOCGIWTXPOW	- SIOCIWFIRST] = wl3501_get_txpow,
	[SIOCGIWRETRY	- SIOCIWFIRST] = wl3501_get_retry,
	[SIOCGIWENCODE	- SIOCIWFIRST] = wl3501_get_encode,
	[SIOCGIWPOWER	- SIOCIWFIRST] = wl3501_get_power,
};

static const struct iw_handler_def wl3501_handler_def = {
	.num_standard	= ARRAY_SIZE(wl3501_handler),
	.standard	= (iw_handler *)wl3501_handler,
	.get_wireless_stats = wl3501_get_wireless_stats,
};

static const struct net_device_ops wl3501_netdev_ops = {
	.ndo_open		= wl3501_open,
	.ndo_stop		= wl3501_close,
	.ndo_start_xmit		= wl3501_hard_start_xmit,
	.ndo_tx_timeout		= wl3501_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/**
 * wl3501_attach - creates an "instance" of the driver
 *
 * Creates an "instance" of the driver, allocating local data structures for
 * one device.  The device is registered with Card Services.
 *
 * The dev_link structure is initialized, but we don't actually configure the
 * card at this point -- we wait until we receive a card insertion event.
 */
static int wl3501_probe(struct pcmcia_device *p_dev)
{
	struct net_device *dev;
	struct wl3501_card *this;

	/* The io structure describes IO port mapping */
	p_dev->io.NumPorts1	= 16;
	p_dev->io.Attributes1	= IO_DATA_PATH_WIDTH_8;
	p_dev->io.IOAddrLines	= 5;

	/* Interrupt setup */
	p_dev->irq.Attributes	= IRQ_TYPE_DYNAMIC_SHARING | IRQ_HANDLE_PRESENT;
	p_dev->irq.IRQInfo1	= IRQ_LEVEL_ID;
	p_dev->irq.Handler = wl3501_interrupt;

	/* General socket configuration */
	p_dev->conf.Attributes	= CONF_ENABLE_IRQ;
	p_dev->conf.IntType	= INT_MEMORY_AND_IO;
	p_dev->conf.ConfigIndex	= 1;

	dev = alloc_etherdev(sizeof(struct wl3501_card));
	if (!dev)
		goto out_link;


	dev->netdev_ops		= &wl3501_netdev_ops;
	dev->watchdog_timeo	= 5 * HZ;

	this = netdev_priv(dev);
	this->wireless_data.spy_data = &this->spy_data;
	this->p_dev = p_dev;
	dev->wireless_data	= &this->wireless_data;
	dev->wireless_handlers	= &wl3501_handler_def;
	SET_ETHTOOL_OPS(dev, &ops);
	netif_stop_queue(dev);
	p_dev->priv = p_dev->irq.Instance = dev;

	return wl3501_config(p_dev);
out_link:
	return -ENOMEM;
}

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

/**
 * wl3501_config - configure the PCMCIA socket and make eth device available
 * @link - FILL_IN
 *
 * wl3501_config() is scheduled to run after a CARD_INSERTION event is
 * received, to configure the PCMCIA socket, and to make the ethernet device
 * available to the system.
 */
static int wl3501_config(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	int i = 0, j, last_fn, last_ret;
	struct wl3501_card *this;

	/* Try allocating IO ports.  This tries a few fixed addresses.  If you
	 * want, you can also read the card's config table to pick addresses --
	 * see the serial driver for an example. */

	for (j = 0x280; j < 0x400; j += 0x20) {
		/* The '^0x300' is so that we probe 0x300-0x3ff first, then
		 * 0x200-0x2ff, and so on, because this seems safer */
		link->io.BasePort1 = j;
		link->io.BasePort2 = link->io.BasePort1 + 0x10;
		i = pcmcia_request_io(link, &link->io);
		if (i == 0)
			break;
	}
	if (i != 0) {
		cs_error(link, RequestIO, i);
		goto failed;
	}

	/* Now allocate an interrupt line. Note that this does not actually
	 * assign a handler to the interrupt. */

	CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));

	/* This actually configures the PCMCIA socket -- setting up the I/O
	 * windows and the interrupt mapping.  */

	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));

	dev->irq = link->irq.AssignedIRQ;
	dev->base_addr = link->io.BasePort1;
	SET_NETDEV_DEV(dev, &handle_to_dev(link));
	if (register_netdev(dev)) {
		printk(KERN_NOTICE "wl3501_cs: register_netdev() failed\n");
		goto failed;
	}

	this = netdev_priv(dev);
	/*
	 * At this point, the dev_node_t structure(s) should be initialized and
	 * arranged in a linked list at link->dev_node.
	 */
	link->dev_node = &this->node;

	this->base_addr = dev->base_addr;

	if (!wl3501_get_flash_mac_addr(this)) {
		printk(KERN_WARNING "%s: Cant read MAC addr in flash ROM?\n",
		       dev->name);
		goto failed;
	}
	strcpy(this->node.dev_name, dev->name);

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = ((char *)&this->mac_addr)[i];

	/* print probe information */
	printk(KERN_INFO "%s: wl3501 @ 0x%3.3x, IRQ %d, "
	       "MAC addr in flash ROM:%pM\n",
	       dev->name, this->base_addr, (int)dev->irq,
	       dev->dev_addr);
	/*
	 * Initialize card parameters - added by jss
	 */
	this->net_type		= IW_MODE_INFRA;
	this->bss_cnt		= 0;
	this->join_sta_bss	= 0;
	this->adhoc_times	= 0;
	iw_set_mgmt_info_element(IW_MGMT_INFO_ELEMENT_SSID, &this->essid.el,
				 "ANY", 3);
	this->card_name[0]	= '\0';
	this->firmware_date[0]	= '\0';
	this->rssi		= 255;
	this->chan		= iw_default_channel(this->reg_domain);
	strlcpy(this->nick, "Planet WL3501", sizeof(this->nick));
	spin_lock_init(&this->lock);
	init_waitqueue_head(&this->wait);
	netif_start_queue(dev);
	return 0;

cs_failed:
	cs_error(link, last_fn, last_ret);
failed:
	wl3501_release(link);
	return -ENODEV;
}

/**
 * wl3501_release - unregister the net, release PCMCIA configuration
 * @arg - link
 *
 * After a card is removed, wl3501_release() will unregister the net device,
 * and release the PCMCIA configuration.  If the device is still open, this
 * will be postponed until it is closed.
 */
static void wl3501_release(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	/* Unlink the device chain */
	if (link->dev_node)
		unregister_netdev(dev);

	pcmcia_disable_device(link);
}

static int wl3501_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	wl3501_pwr_mgmt(netdev_priv(dev), WL3501_SUSPEND);
	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int wl3501_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	wl3501_pwr_mgmt(netdev_priv(dev), WL3501_RESUME);
	if (link->open) {
		wl3501_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}


static struct pcmcia_device_id wl3501_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0001),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, wl3501_ids);

static struct pcmcia_driver wl3501_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "wl3501_cs",
	},
	.probe		= wl3501_probe,
	.remove		= wl3501_detach,
	.id_table	= wl3501_ids,
	.suspend	= wl3501_suspend,
	.resume		= wl3501_resume,
};

static int __init wl3501_init_module(void)
{
	return pcmcia_register_driver(&wl3501_driver);
}

static void __exit wl3501_exit_module(void)
{
	pcmcia_unregister_driver(&wl3501_driver);
}

module_init(wl3501_init_module);
module_exit(wl3501_exit_module);

MODULE_AUTHOR("Fox Chen <mhchen@golf.ccl.itri.org.tw>, "
	      "Arnaldo Carvalho de Melo <acme@conectiva.com.br>,"
	      "Gustavo Niemeyer <niemeyer@conectiva.com>");
MODULE_DESCRIPTION("Planet wl3501 wireless driver");
MODULE_LICENSE("GPL");
