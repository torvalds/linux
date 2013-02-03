/******************************************************************************

  Copyright(c) 2003 - 2006 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  Intel Linux Wireless <ilw@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

  Portions of this file are based on the sample_* files provided by Wireless
  Extensions 0.26 package and copyright (c) 1997-2003 Jean Tourrilhes
  <jt@hpl.hp.com>

  Portions of this file are based on the Host AP project,
  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
    <j@w1.fi>
  Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>

  Portions of ipw2100_mod_firmware_load, ipw2100_do_mod_firmware_load, and
  ipw2100_fw_load are loosely based on drivers/sound/sound_firmware.c
  available in the 2.4.25 kernel sources, and are copyright (c) Alan Cox

******************************************************************************/
/*

 Initial driver on which this is based was developed by Janusz Gorycki,
 Maciej Urbaniak, and Maciej Sosnowski.

 Promiscuous mode support added by Jacek Wysoczynski and Maciej Urbaniak.

Theory of Operation

Tx - Commands and Data

Firmware and host share a circular queue of Transmit Buffer Descriptors (TBDs)
Each TBD contains a pointer to the physical (dma_addr_t) address of data being
sent to the firmware as well as the length of the data.

The host writes to the TBD queue at the WRITE index.  The WRITE index points
to the _next_ packet to be written and is advanced when after the TBD has been
filled.

The firmware pulls from the TBD queue at the READ index.  The READ index points
to the currently being read entry, and is advanced once the firmware is
done with a packet.

When data is sent to the firmware, the first TBD is used to indicate to the
firmware if a Command or Data is being sent.  If it is Command, all of the
command information is contained within the physical address referred to by the
TBD.  If it is Data, the first TBD indicates the type of data packet, number
of fragments, etc.  The next TBD then refers to the actual packet location.

The Tx flow cycle is as follows:

1) ipw2100_tx() is called by kernel with SKB to transmit
2) Packet is move from the tx_free_list and appended to the transmit pending
   list (tx_pend_list)
3) work is scheduled to move pending packets into the shared circular queue.
4) when placing packet in the circular queue, the incoming SKB is DMA mapped
   to a physical address.  That address is entered into a TBD.  Two TBDs are
   filled out.  The first indicating a data packet, the second referring to the
   actual payload data.
5) the packet is removed from tx_pend_list and placed on the end of the
   firmware pending list (fw_pend_list)
6) firmware is notified that the WRITE index has
7) Once the firmware has processed the TBD, INTA is triggered.
8) For each Tx interrupt received from the firmware, the READ index is checked
   to see which TBDs are done being processed.
9) For each TBD that has been processed, the ISR pulls the oldest packet
   from the fw_pend_list.
10)The packet structure contained in the fw_pend_list is then used
   to unmap the DMA address and to free the SKB originally passed to the driver
   from the kernel.
11)The packet structure is placed onto the tx_free_list

The above steps are the same for commands, only the msg_free_list/msg_pend_list
are used instead of tx_free_list/tx_pend_list

...

Critical Sections / Locking :

There are two locks utilized.  The first is the low level lock (priv->low_lock)
that protects the following:

- Access to the Tx/Rx queue lists via priv->low_lock. The lists are as follows:

  tx_free_list : Holds pre-allocated Tx buffers.
    TAIL modified in __ipw2100_tx_process()
    HEAD modified in ipw2100_tx()

  tx_pend_list : Holds used Tx buffers waiting to go into the TBD ring
    TAIL modified ipw2100_tx()
    HEAD modified by ipw2100_tx_send_data()

  msg_free_list : Holds pre-allocated Msg (Command) buffers
    TAIL modified in __ipw2100_tx_process()
    HEAD modified in ipw2100_hw_send_command()

  msg_pend_list : Holds used Msg buffers waiting to go into the TBD ring
    TAIL modified in ipw2100_hw_send_command()
    HEAD modified in ipw2100_tx_send_commands()

  The flow of data on the TX side is as follows:

  MSG_FREE_LIST + COMMAND => MSG_PEND_LIST => TBD => MSG_FREE_LIST
  TX_FREE_LIST + DATA => TX_PEND_LIST => TBD => TX_FREE_LIST

  The methods that work on the TBD ring are protected via priv->low_lock.

- The internal data state of the device itself
- Access to the firmware read/write indexes for the BD queues
  and associated logic

All external entry functions are locked with the priv->action_lock to ensure
that only one external action is invoked at a time.


*/

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/stringify.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/firmware.h>
#include <linux/acpi.h>
#include <linux/ctype.h>
#include <linux/pm_qos.h>

#include <net/lib80211.h>

#include "ipw2100.h"
#include "ipw.h"

#define IPW2100_VERSION "git-1.2.2"

#define DRV_NAME	"ipw2100"
#define DRV_VERSION	IPW2100_VERSION
#define DRV_DESCRIPTION	"Intel(R) PRO/Wireless 2100 Network Driver"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2006 Intel Corporation"

static struct pm_qos_request ipw2100_pm_qos_req;

/* Debugging stuff */
#ifdef CONFIG_IPW2100_DEBUG
#define IPW2100_RX_DEBUG	/* Reception debugging */
#endif

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

static int debug = 0;
static int network_mode = 0;
static int channel = 0;
static int associate = 0;
static int disable = 0;
#ifdef CONFIG_PM
static struct ipw2100_fw ipw2100_firmware;
#endif

#include <linux/moduleparam.h>
module_param(debug, int, 0444);
module_param_named(mode, network_mode, int, 0444);
module_param(channel, int, 0444);
module_param(associate, int, 0444);
module_param(disable, int, 0444);

MODULE_PARM_DESC(debug, "debug level");
MODULE_PARM_DESC(mode, "network mode (0=BSS,1=IBSS,2=Monitor)");
MODULE_PARM_DESC(channel, "channel");
MODULE_PARM_DESC(associate, "auto associate when scanning (default off)");
MODULE_PARM_DESC(disable, "manually disable the radio (default 0 [radio on])");

static u32 ipw2100_debug_level = IPW_DL_NONE;

#ifdef CONFIG_IPW2100_DEBUG
#define IPW_DEBUG(level, message...) \
do { \
	if (ipw2100_debug_level & (level)) { \
		printk(KERN_DEBUG "ipw2100: %c %s ", \
                       in_interrupt() ? 'I' : 'U',  __func__); \
		printk(message); \
	} \
} while (0)
#else
#define IPW_DEBUG(level, message...) do {} while (0)
#endif				/* CONFIG_IPW2100_DEBUG */

#ifdef CONFIG_IPW2100_DEBUG
static const char *command_types[] = {
	"undefined",
	"unused",		/* HOST_ATTENTION */
	"HOST_COMPLETE",
	"unused",		/* SLEEP */
	"unused",		/* HOST_POWER_DOWN */
	"unused",
	"SYSTEM_CONFIG",
	"unused",		/* SET_IMR */
	"SSID",
	"MANDATORY_BSSID",
	"AUTHENTICATION_TYPE",
	"ADAPTER_ADDRESS",
	"PORT_TYPE",
	"INTERNATIONAL_MODE",
	"CHANNEL",
	"RTS_THRESHOLD",
	"FRAG_THRESHOLD",
	"POWER_MODE",
	"TX_RATES",
	"BASIC_TX_RATES",
	"WEP_KEY_INFO",
	"unused",
	"unused",
	"unused",
	"unused",
	"WEP_KEY_INDEX",
	"WEP_FLAGS",
	"ADD_MULTICAST",
	"CLEAR_ALL_MULTICAST",
	"BEACON_INTERVAL",
	"ATIM_WINDOW",
	"CLEAR_STATISTICS",
	"undefined",
	"undefined",
	"undefined",
	"undefined",
	"TX_POWER_INDEX",
	"undefined",
	"undefined",
	"undefined",
	"undefined",
	"undefined",
	"undefined",
	"BROADCAST_SCAN",
	"CARD_DISABLE",
	"PREFERRED_BSSID",
	"SET_SCAN_OPTIONS",
	"SCAN_DWELL_TIME",
	"SWEEP_TABLE",
	"AP_OR_STATION_TABLE",
	"GROUP_ORDINALS",
	"SHORT_RETRY_LIMIT",
	"LONG_RETRY_LIMIT",
	"unused",		/* SAVE_CALIBRATION */
	"unused",		/* RESTORE_CALIBRATION */
	"undefined",
	"undefined",
	"undefined",
	"HOST_PRE_POWER_DOWN",
	"unused",		/* HOST_INTERRUPT_COALESCING */
	"undefined",
	"CARD_DISABLE_PHY_OFF",
	"MSDU_TX_RATES",
	"undefined",
	"SET_STATION_STAT_BITS",
	"CLEAR_STATIONS_STAT_BITS",
	"LEAP_ROGUE_MODE",
	"SET_SECURITY_INFORMATION",
	"DISASSOCIATION_BSSID",
	"SET_WPA_ASS_IE"
};
#endif

static const long ipw2100_frequencies[] = {
	2412, 2417, 2422, 2427,
	2432, 2437, 2442, 2447,
	2452, 2457, 2462, 2467,
	2472, 2484
};

#define FREQ_COUNT	ARRAY_SIZE(ipw2100_frequencies)

static struct ieee80211_rate ipw2100_bg_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
};

#define RATE_COUNT ARRAY_SIZE(ipw2100_bg_rates)

/* Pre-decl until we get the code solid and then we can clean it up */
static void ipw2100_tx_send_commands(struct ipw2100_priv *priv);
static void ipw2100_tx_send_data(struct ipw2100_priv *priv);
static int ipw2100_adapter_setup(struct ipw2100_priv *priv);

static void ipw2100_queues_initialize(struct ipw2100_priv *priv);
static void ipw2100_queues_free(struct ipw2100_priv *priv);
static int ipw2100_queues_allocate(struct ipw2100_priv *priv);

static int ipw2100_fw_download(struct ipw2100_priv *priv,
			       struct ipw2100_fw *fw);
static int ipw2100_get_firmware(struct ipw2100_priv *priv,
				struct ipw2100_fw *fw);
static int ipw2100_get_fwversion(struct ipw2100_priv *priv, char *buf,
				 size_t max);
static int ipw2100_get_ucodeversion(struct ipw2100_priv *priv, char *buf,
				    size_t max);
static void ipw2100_release_firmware(struct ipw2100_priv *priv,
				     struct ipw2100_fw *fw);
static int ipw2100_ucode_download(struct ipw2100_priv *priv,
				  struct ipw2100_fw *fw);
static void ipw2100_wx_event_work(struct work_struct *work);
static struct iw_statistics *ipw2100_wx_wireless_stats(struct net_device *dev);
static struct iw_handler_def ipw2100_wx_handler_def;

static inline void read_register(struct net_device *dev, u32 reg, u32 * val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	*val = ioread32(priv->ioaddr + reg);
	IPW_DEBUG_IO("r: 0x%08X => 0x%08X\n", reg, *val);
}

static inline void write_register(struct net_device *dev, u32 reg, u32 val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	iowrite32(val, priv->ioaddr + reg);
	IPW_DEBUG_IO("w: 0x%08X <= 0x%08X\n", reg, val);
}

static inline void read_register_word(struct net_device *dev, u32 reg,
				      u16 * val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	*val = ioread16(priv->ioaddr + reg);
	IPW_DEBUG_IO("r: 0x%08X => %04X\n", reg, *val);
}

static inline void read_register_byte(struct net_device *dev, u32 reg, u8 * val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	*val = ioread8(priv->ioaddr + reg);
	IPW_DEBUG_IO("r: 0x%08X => %02X\n", reg, *val);
}

static inline void write_register_word(struct net_device *dev, u32 reg, u16 val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	iowrite16(val, priv->ioaddr + reg);
	IPW_DEBUG_IO("w: 0x%08X <= %04X\n", reg, val);
}

static inline void write_register_byte(struct net_device *dev, u32 reg, u8 val)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	iowrite8(val, priv->ioaddr + reg);
	IPW_DEBUG_IO("w: 0x%08X =< %02X\n", reg, val);
}

static inline void read_nic_dword(struct net_device *dev, u32 addr, u32 * val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	read_register(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void write_nic_dword(struct net_device *dev, u32 addr, u32 val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	write_register(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void read_nic_word(struct net_device *dev, u32 addr, u16 * val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	read_register_word(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void write_nic_word(struct net_device *dev, u32 addr, u16 val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	write_register_word(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void read_nic_byte(struct net_device *dev, u32 addr, u8 * val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	read_register_byte(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void write_nic_byte(struct net_device *dev, u32 addr, u8 val)
{
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
	write_register_byte(dev, IPW_REG_INDIRECT_ACCESS_DATA, val);
}

static inline void write_nic_auto_inc_address(struct net_device *dev, u32 addr)
{
	write_register(dev, IPW_REG_AUTOINCREMENT_ADDRESS,
		       addr & IPW_REG_INDIRECT_ADDR_MASK);
}

static inline void write_nic_dword_auto_inc(struct net_device *dev, u32 val)
{
	write_register(dev, IPW_REG_AUTOINCREMENT_DATA, val);
}

static void write_nic_memory(struct net_device *dev, u32 addr, u32 len,
				    const u8 * buf)
{
	u32 aligned_addr;
	u32 aligned_len;
	u32 dif_len;
	u32 i;

	/* read first nibble byte by byte */
	aligned_addr = addr & (~0x3);
	dif_len = addr - aligned_addr;
	if (dif_len) {
		/* Start reading at aligned_addr + dif_len */
		write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
			       aligned_addr);
		for (i = dif_len; i < 4; i++, buf++)
			write_register_byte(dev,
					    IPW_REG_INDIRECT_ACCESS_DATA + i,
					    *buf);

		len -= dif_len;
		aligned_addr += 4;
	}

	/* read DWs through autoincrement registers */
	write_register(dev, IPW_REG_AUTOINCREMENT_ADDRESS, aligned_addr);
	aligned_len = len & (~0x3);
	for (i = 0; i < aligned_len; i += 4, buf += 4, aligned_addr += 4)
		write_register(dev, IPW_REG_AUTOINCREMENT_DATA, *(u32 *) buf);

	/* copy the last nibble */
	dif_len = len - aligned_len;
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS, aligned_addr);
	for (i = 0; i < dif_len; i++, buf++)
		write_register_byte(dev, IPW_REG_INDIRECT_ACCESS_DATA + i,
				    *buf);
}

static void read_nic_memory(struct net_device *dev, u32 addr, u32 len,
				   u8 * buf)
{
	u32 aligned_addr;
	u32 aligned_len;
	u32 dif_len;
	u32 i;

	/* read first nibble byte by byte */
	aligned_addr = addr & (~0x3);
	dif_len = addr - aligned_addr;
	if (dif_len) {
		/* Start reading at aligned_addr + dif_len */
		write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS,
			       aligned_addr);
		for (i = dif_len; i < 4; i++, buf++)
			read_register_byte(dev,
					   IPW_REG_INDIRECT_ACCESS_DATA + i,
					   buf);

		len -= dif_len;
		aligned_addr += 4;
	}

	/* read DWs through autoincrement registers */
	write_register(dev, IPW_REG_AUTOINCREMENT_ADDRESS, aligned_addr);
	aligned_len = len & (~0x3);
	for (i = 0; i < aligned_len; i += 4, buf += 4, aligned_addr += 4)
		read_register(dev, IPW_REG_AUTOINCREMENT_DATA, (u32 *) buf);

	/* copy the last nibble */
	dif_len = len - aligned_len;
	write_register(dev, IPW_REG_INDIRECT_ACCESS_ADDRESS, aligned_addr);
	for (i = 0; i < dif_len; i++, buf++)
		read_register_byte(dev, IPW_REG_INDIRECT_ACCESS_DATA + i, buf);
}

static bool ipw2100_hw_is_adapter_in_system(struct net_device *dev)
{
	u32 dbg;

	read_register(dev, IPW_REG_DOA_DEBUG_AREA_START, &dbg);

	return dbg == IPW_DATA_DOA_DEBUG_VALUE;
}

static int ipw2100_get_ordinal(struct ipw2100_priv *priv, u32 ord,
			       void *val, u32 * len)
{
	struct ipw2100_ordinals *ordinals = &priv->ordinals;
	u32 addr;
	u32 field_info;
	u16 field_len;
	u16 field_count;
	u32 total_length;

	if (ordinals->table1_addr == 0) {
		printk(KERN_WARNING DRV_NAME ": attempt to use fw ordinals "
		       "before they have been loaded.\n");
		return -EINVAL;
	}

	if (IS_ORDINAL_TABLE_ONE(ordinals, ord)) {
		if (*len < IPW_ORD_TAB_1_ENTRY_SIZE) {
			*len = IPW_ORD_TAB_1_ENTRY_SIZE;

			printk(KERN_WARNING DRV_NAME
			       ": ordinal buffer length too small, need %zd\n",
			       IPW_ORD_TAB_1_ENTRY_SIZE);

			return -EINVAL;
		}

		read_nic_dword(priv->net_dev,
			       ordinals->table1_addr + (ord << 2), &addr);
		read_nic_dword(priv->net_dev, addr, val);

		*len = IPW_ORD_TAB_1_ENTRY_SIZE;

		return 0;
	}

	if (IS_ORDINAL_TABLE_TWO(ordinals, ord)) {

		ord -= IPW_START_ORD_TAB_2;

		/* get the address of statistic */
		read_nic_dword(priv->net_dev,
			       ordinals->table2_addr + (ord << 3), &addr);

		/* get the second DW of statistics ;
		 * two 16-bit words - first is length, second is count */
		read_nic_dword(priv->net_dev,
			       ordinals->table2_addr + (ord << 3) + sizeof(u32),
			       &field_info);

		/* get each entry length */
		field_len = *((u16 *) & field_info);

		/* get number of entries */
		field_count = *(((u16 *) & field_info) + 1);

		/* abort if no enough memory */
		total_length = field_len * field_count;
		if (total_length > *len) {
			*len = total_length;
			return -EINVAL;
		}

		*len = total_length;
		if (!total_length)
			return 0;

		/* read the ordinal data from the SRAM */
		read_nic_memory(priv->net_dev, addr, total_length, val);

		return 0;
	}

	printk(KERN_WARNING DRV_NAME ": ordinal %d neither in table 1 nor "
	       "in table 2\n", ord);

	return -EINVAL;
}

static int ipw2100_set_ordinal(struct ipw2100_priv *priv, u32 ord, u32 * val,
			       u32 * len)
{
	struct ipw2100_ordinals *ordinals = &priv->ordinals;
	u32 addr;

	if (IS_ORDINAL_TABLE_ONE(ordinals, ord)) {
		if (*len != IPW_ORD_TAB_1_ENTRY_SIZE) {
			*len = IPW_ORD_TAB_1_ENTRY_SIZE;
			IPW_DEBUG_INFO("wrong size\n");
			return -EINVAL;
		}

		read_nic_dword(priv->net_dev,
			       ordinals->table1_addr + (ord << 2), &addr);

		write_nic_dword(priv->net_dev, addr, *val);

		*len = IPW_ORD_TAB_1_ENTRY_SIZE;

		return 0;
	}

	IPW_DEBUG_INFO("wrong table\n");
	if (IS_ORDINAL_TABLE_TWO(ordinals, ord))
		return -EINVAL;

	return -EINVAL;
}

static char *snprint_line(char *buf, size_t count,
			  const u8 * data, u32 len, u32 ofs)
{
	int out, i, j, l;
	char c;

	out = snprintf(buf, count, "%08X", ofs);

	for (l = 0, i = 0; i < 2; i++) {
		out += snprintf(buf + out, count - out, " ");
		for (j = 0; j < 8 && l < len; j++, l++)
			out += snprintf(buf + out, count - out, "%02X ",
					data[(i * 8 + j)]);
		for (; j < 8; j++)
			out += snprintf(buf + out, count - out, "   ");
	}

	out += snprintf(buf + out, count - out, " ");
	for (l = 0, i = 0; i < 2; i++) {
		out += snprintf(buf + out, count - out, " ");
		for (j = 0; j < 8 && l < len; j++, l++) {
			c = data[(i * 8 + j)];
			if (!isascii(c) || !isprint(c))
				c = '.';

			out += snprintf(buf + out, count - out, "%c", c);
		}

		for (; j < 8; j++)
			out += snprintf(buf + out, count - out, " ");
	}

	return buf;
}

static void printk_buf(int level, const u8 * data, u32 len)
{
	char line[81];
	u32 ofs = 0;
	if (!(ipw2100_debug_level & level))
		return;

	while (len) {
		printk(KERN_DEBUG "%s\n",
		       snprint_line(line, sizeof(line), &data[ofs],
				    min(len, 16U), ofs));
		ofs += 16;
		len -= min(len, 16U);
	}
}

#define MAX_RESET_BACKOFF 10

static void schedule_reset(struct ipw2100_priv *priv)
{
	unsigned long now = get_seconds();

	/* If we haven't received a reset request within the backoff period,
	 * then we can reset the backoff interval so this reset occurs
	 * immediately */
	if (priv->reset_backoff &&
	    (now - priv->last_reset > priv->reset_backoff))
		priv->reset_backoff = 0;

	priv->last_reset = get_seconds();

	if (!(priv->status & STATUS_RESET_PENDING)) {
		IPW_DEBUG_INFO("%s: Scheduling firmware restart (%ds).\n",
			       priv->net_dev->name, priv->reset_backoff);
		netif_carrier_off(priv->net_dev);
		netif_stop_queue(priv->net_dev);
		priv->status |= STATUS_RESET_PENDING;
		if (priv->reset_backoff)
			schedule_delayed_work(&priv->reset_work,
					      priv->reset_backoff * HZ);
		else
			schedule_delayed_work(&priv->reset_work, 0);

		if (priv->reset_backoff < MAX_RESET_BACKOFF)
			priv->reset_backoff++;

		wake_up_interruptible(&priv->wait_command_queue);
	} else
		IPW_DEBUG_INFO("%s: Firmware restart already in progress.\n",
			       priv->net_dev->name);

}

#define HOST_COMPLETE_TIMEOUT (2 * HZ)
static int ipw2100_hw_send_command(struct ipw2100_priv *priv,
				   struct host_command *cmd)
{
	struct list_head *element;
	struct ipw2100_tx_packet *packet;
	unsigned long flags;
	int err = 0;

	IPW_DEBUG_HC("Sending %s command (#%d), %d bytes\n",
		     command_types[cmd->host_command], cmd->host_command,
		     cmd->host_command_length);
	printk_buf(IPW_DL_HC, (u8 *) cmd->host_command_parameters,
		   cmd->host_command_length);

	spin_lock_irqsave(&priv->low_lock, flags);

	if (priv->fatal_error) {
		IPW_DEBUG_INFO
		    ("Attempt to send command while hardware in fatal error condition.\n");
		err = -EIO;
		goto fail_unlock;
	}

	if (!(priv->status & STATUS_RUNNING)) {
		IPW_DEBUG_INFO
		    ("Attempt to send command while hardware is not running.\n");
		err = -EIO;
		goto fail_unlock;
	}

	if (priv->status & STATUS_CMD_ACTIVE) {
		IPW_DEBUG_INFO
		    ("Attempt to send command while another command is pending.\n");
		err = -EBUSY;
		goto fail_unlock;
	}

	if (list_empty(&priv->msg_free_list)) {
		IPW_DEBUG_INFO("no available msg buffers\n");
		goto fail_unlock;
	}

	priv->status |= STATUS_CMD_ACTIVE;
	priv->messages_sent++;

	element = priv->msg_free_list.next;

	packet = list_entry(element, struct ipw2100_tx_packet, list);
	packet->jiffy_start = jiffies;

	/* initialize the firmware command packet */
	packet->info.c_struct.cmd->host_command_reg = cmd->host_command;
	packet->info.c_struct.cmd->host_command_reg1 = cmd->host_command1;
	packet->info.c_struct.cmd->host_command_len_reg =
	    cmd->host_command_length;
	packet->info.c_struct.cmd->sequence = cmd->host_command_sequence;

	memcpy(packet->info.c_struct.cmd->host_command_params_reg,
	       cmd->host_command_parameters,
	       sizeof(packet->info.c_struct.cmd->host_command_params_reg));

	list_del(element);
	DEC_STAT(&priv->msg_free_stat);

	list_add_tail(element, &priv->msg_pend_list);
	INC_STAT(&priv->msg_pend_stat);

	ipw2100_tx_send_commands(priv);
	ipw2100_tx_send_data(priv);

	spin_unlock_irqrestore(&priv->low_lock, flags);

	/*
	 * We must wait for this command to complete before another
	 * command can be sent...  but if we wait more than 3 seconds
	 * then there is a problem.
	 */

	err =
	    wait_event_interruptible_timeout(priv->wait_command_queue,
					     !(priv->
					       status & STATUS_CMD_ACTIVE),
					     HOST_COMPLETE_TIMEOUT);

	if (err == 0) {
		IPW_DEBUG_INFO("Command completion failed out after %dms.\n",
			       1000 * (HOST_COMPLETE_TIMEOUT / HZ));
		priv->fatal_error = IPW2100_ERR_MSG_TIMEOUT;
		priv->status &= ~STATUS_CMD_ACTIVE;
		schedule_reset(priv);
		return -EIO;
	}

	if (priv->fatal_error) {
		printk(KERN_WARNING DRV_NAME ": %s: firmware fatal error\n",
		       priv->net_dev->name);
		return -EIO;
	}

	/* !!!!! HACK TEST !!!!!
	 * When lots of debug trace statements are enabled, the driver
	 * doesn't seem to have as many firmware restart cycles...
	 *
	 * As a test, we're sticking in a 1/100s delay here */
	schedule_timeout_uninterruptible(msecs_to_jiffies(10));

	return 0;

      fail_unlock:
	spin_unlock_irqrestore(&priv->low_lock, flags);

	return err;
}

/*
 * Verify the values and data access of the hardware
 * No locks needed or used.  No functions called.
 */
static int ipw2100_verify(struct ipw2100_priv *priv)
{
	u32 data1, data2;
	u32 address;

	u32 val1 = 0x76543210;
	u32 val2 = 0xFEDCBA98;

	/* Domain 0 check - all values should be DOA_DEBUG */
	for (address = IPW_REG_DOA_DEBUG_AREA_START;
	     address < IPW_REG_DOA_DEBUG_AREA_END; address += sizeof(u32)) {
		read_register(priv->net_dev, address, &data1);
		if (data1 != IPW_DATA_DOA_DEBUG_VALUE)
			return -EIO;
	}

	/* Domain 1 check - use arbitrary read/write compare  */
	for (address = 0; address < 5; address++) {
		/* The memory area is not used now */
		write_register(priv->net_dev, IPW_REG_DOMAIN_1_OFFSET + 0x32,
			       val1);
		write_register(priv->net_dev, IPW_REG_DOMAIN_1_OFFSET + 0x36,
			       val2);
		read_register(priv->net_dev, IPW_REG_DOMAIN_1_OFFSET + 0x32,
			      &data1);
		read_register(priv->net_dev, IPW_REG_DOMAIN_1_OFFSET + 0x36,
			      &data2);
		if (val1 == data1 && val2 == data2)
			return 0;
	}

	return -EIO;
}

/*
 *
 * Loop until the CARD_DISABLED bit is the same value as the
 * supplied parameter
 *
 * TODO: See if it would be more efficient to do a wait/wake
 *       cycle and have the completion event trigger the wakeup
 *
 */
#define IPW_CARD_DISABLE_COMPLETE_WAIT		    100	// 100 milli
static int ipw2100_wait_for_card_state(struct ipw2100_priv *priv, int state)
{
	int i;
	u32 card_state;
	u32 len = sizeof(card_state);
	int err;

	for (i = 0; i <= IPW_CARD_DISABLE_COMPLETE_WAIT * 1000; i += 50) {
		err = ipw2100_get_ordinal(priv, IPW_ORD_CARD_DISABLED,
					  &card_state, &len);
		if (err) {
			IPW_DEBUG_INFO("Query of CARD_DISABLED ordinal "
				       "failed.\n");
			return 0;
		}

		/* We'll break out if either the HW state says it is
		 * in the state we want, or if HOST_COMPLETE command
		 * finishes */
		if ((card_state == state) ||
		    ((priv->status & STATUS_ENABLED) ?
		     IPW_HW_STATE_ENABLED : IPW_HW_STATE_DISABLED) == state) {
			if (state == IPW_HW_STATE_ENABLED)
				priv->status |= STATUS_ENABLED;
			else
				priv->status &= ~STATUS_ENABLED;

			return 0;
		}

		udelay(50);
	}

	IPW_DEBUG_INFO("ipw2100_wait_for_card_state to %s state timed out\n",
		       state ? "DISABLED" : "ENABLED");
	return -EIO;
}

/*********************************************************************
    Procedure   :   sw_reset_and_clock
    Purpose     :   Asserts s/w reset, asserts clock initialization
                    and waits for clock stabilization
 ********************************************************************/
static int sw_reset_and_clock(struct ipw2100_priv *priv)
{
	int i;
	u32 r;

	// assert s/w reset
	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_SW_RESET);

	// wait for clock stabilization
	for (i = 0; i < 1000; i++) {
		udelay(IPW_WAIT_RESET_ARC_COMPLETE_DELAY);

		// check clock ready bit
		read_register(priv->net_dev, IPW_REG_RESET_REG, &r);
		if (r & IPW_AUX_HOST_RESET_REG_PRINCETON_RESET)
			break;
	}

	if (i == 1000)
		return -EIO;	// TODO: better error value

	/* set "initialization complete" bit to move adapter to
	 * D0 state */
	write_register(priv->net_dev, IPW_REG_GP_CNTRL,
		       IPW_AUX_HOST_GP_CNTRL_BIT_INIT_DONE);

	/* wait for clock stabilization */
	for (i = 0; i < 10000; i++) {
		udelay(IPW_WAIT_CLOCK_STABILIZATION_DELAY * 4);

		/* check clock ready bit */
		read_register(priv->net_dev, IPW_REG_GP_CNTRL, &r);
		if (r & IPW_AUX_HOST_GP_CNTRL_BIT_CLOCK_READY)
			break;
	}

	if (i == 10000)
		return -EIO;	/* TODO: better error value */

	/* set D0 standby bit */
	read_register(priv->net_dev, IPW_REG_GP_CNTRL, &r);
	write_register(priv->net_dev, IPW_REG_GP_CNTRL,
		       r | IPW_AUX_HOST_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY);

	return 0;
}

/*********************************************************************
    Procedure   :   ipw2100_download_firmware
    Purpose     :   Initiaze adapter after power on.
                    The sequence is:
                    1. assert s/w reset first!
                    2. awake clocks & wait for clock stabilization
                    3. hold ARC (don't ask me why...)
                    4. load Dino ucode and reset/clock init again
                    5. zero-out shared mem
                    6. download f/w
 *******************************************************************/
static int ipw2100_download_firmware(struct ipw2100_priv *priv)
{
	u32 address;
	int err;

#ifndef CONFIG_PM
	/* Fetch the firmware and microcode */
	struct ipw2100_fw ipw2100_firmware;
#endif

	if (priv->fatal_error) {
		IPW_DEBUG_ERROR("%s: ipw2100_download_firmware called after "
				"fatal error %d.  Interface must be brought down.\n",
				priv->net_dev->name, priv->fatal_error);
		return -EINVAL;
	}
#ifdef CONFIG_PM
	if (!ipw2100_firmware.version) {
		err = ipw2100_get_firmware(priv, &ipw2100_firmware);
		if (err) {
			IPW_DEBUG_ERROR("%s: ipw2100_get_firmware failed: %d\n",
					priv->net_dev->name, err);
			priv->fatal_error = IPW2100_ERR_FW_LOAD;
			goto fail;
		}
	}
#else
	err = ipw2100_get_firmware(priv, &ipw2100_firmware);
	if (err) {
		IPW_DEBUG_ERROR("%s: ipw2100_get_firmware failed: %d\n",
				priv->net_dev->name, err);
		priv->fatal_error = IPW2100_ERR_FW_LOAD;
		goto fail;
	}
#endif
	priv->firmware_version = ipw2100_firmware.version;

	/* s/w reset and clock stabilization */
	err = sw_reset_and_clock(priv);
	if (err) {
		IPW_DEBUG_ERROR("%s: sw_reset_and_clock failed: %d\n",
				priv->net_dev->name, err);
		goto fail;
	}

	err = ipw2100_verify(priv);
	if (err) {
		IPW_DEBUG_ERROR("%s: ipw2100_verify failed: %d\n",
				priv->net_dev->name, err);
		goto fail;
	}

	/* Hold ARC */
	write_nic_dword(priv->net_dev,
			IPW_INTERNAL_REGISTER_HALT_AND_RESET, 0x80000000);

	/* allow ARC to run */
	write_register(priv->net_dev, IPW_REG_RESET_REG, 0);

	/* load microcode */
	err = ipw2100_ucode_download(priv, &ipw2100_firmware);
	if (err) {
		printk(KERN_ERR DRV_NAME ": %s: Error loading microcode: %d\n",
		       priv->net_dev->name, err);
		goto fail;
	}

	/* release ARC */
	write_nic_dword(priv->net_dev,
			IPW_INTERNAL_REGISTER_HALT_AND_RESET, 0x00000000);

	/* s/w reset and clock stabilization (again!!!) */
	err = sw_reset_and_clock(priv);
	if (err) {
		printk(KERN_ERR DRV_NAME
		       ": %s: sw_reset_and_clock failed: %d\n",
		       priv->net_dev->name, err);
		goto fail;
	}

	/* load f/w */
	err = ipw2100_fw_download(priv, &ipw2100_firmware);
	if (err) {
		IPW_DEBUG_ERROR("%s: Error loading firmware: %d\n",
				priv->net_dev->name, err);
		goto fail;
	}
#ifndef CONFIG_PM
	/*
	 * When the .resume method of the driver is called, the other
	 * part of the system, i.e. the ide driver could still stay in
	 * the suspend stage. This prevents us from loading the firmware
	 * from the disk.  --YZ
	 */

	/* free any storage allocated for firmware image */
	ipw2100_release_firmware(priv, &ipw2100_firmware);
#endif

	/* zero out Domain 1 area indirectly (Si requirement) */
	for (address = IPW_HOST_FW_SHARED_AREA0;
	     address < IPW_HOST_FW_SHARED_AREA0_END; address += 4)
		write_nic_dword(priv->net_dev, address, 0);
	for (address = IPW_HOST_FW_SHARED_AREA1;
	     address < IPW_HOST_FW_SHARED_AREA1_END; address += 4)
		write_nic_dword(priv->net_dev, address, 0);
	for (address = IPW_HOST_FW_SHARED_AREA2;
	     address < IPW_HOST_FW_SHARED_AREA2_END; address += 4)
		write_nic_dword(priv->net_dev, address, 0);
	for (address = IPW_HOST_FW_SHARED_AREA3;
	     address < IPW_HOST_FW_SHARED_AREA3_END; address += 4)
		write_nic_dword(priv->net_dev, address, 0);
	for (address = IPW_HOST_FW_INTERRUPT_AREA;
	     address < IPW_HOST_FW_INTERRUPT_AREA_END; address += 4)
		write_nic_dword(priv->net_dev, address, 0);

	return 0;

      fail:
	ipw2100_release_firmware(priv, &ipw2100_firmware);
	return err;
}

static inline void ipw2100_enable_interrupts(struct ipw2100_priv *priv)
{
	if (priv->status & STATUS_INT_ENABLED)
		return;
	priv->status |= STATUS_INT_ENABLED;
	write_register(priv->net_dev, IPW_REG_INTA_MASK, IPW_INTERRUPT_MASK);
}

static inline void ipw2100_disable_interrupts(struct ipw2100_priv *priv)
{
	if (!(priv->status & STATUS_INT_ENABLED))
		return;
	priv->status &= ~STATUS_INT_ENABLED;
	write_register(priv->net_dev, IPW_REG_INTA_MASK, 0x0);
}

static void ipw2100_initialize_ordinals(struct ipw2100_priv *priv)
{
	struct ipw2100_ordinals *ord = &priv->ordinals;

	IPW_DEBUG_INFO("enter\n");

	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_ORDINALS_TABLE_1,
		      &ord->table1_addr);

	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_ORDINALS_TABLE_2,
		      &ord->table2_addr);

	read_nic_dword(priv->net_dev, ord->table1_addr, &ord->table1_size);
	read_nic_dword(priv->net_dev, ord->table2_addr, &ord->table2_size);

	ord->table2_size &= 0x0000FFFF;

	IPW_DEBUG_INFO("table 1 size: %d\n", ord->table1_size);
	IPW_DEBUG_INFO("table 2 size: %d\n", ord->table2_size);
	IPW_DEBUG_INFO("exit\n");
}

static inline void ipw2100_hw_set_gpio(struct ipw2100_priv *priv)
{
	u32 reg = 0;
	/*
	 * Set GPIO 3 writable by FW; GPIO 1 writable
	 * by driver and enable clock
	 */
	reg = (IPW_BIT_GPIO_GPIO3_MASK | IPW_BIT_GPIO_GPIO1_ENABLE |
	       IPW_BIT_GPIO_LED_OFF);
	write_register(priv->net_dev, IPW_REG_GPIO, reg);
}

static int rf_kill_active(struct ipw2100_priv *priv)
{
#define MAX_RF_KILL_CHECKS 5
#define RF_KILL_CHECK_DELAY 40

	unsigned short value = 0;
	u32 reg = 0;
	int i;

	if (!(priv->hw_features & HW_FEATURE_RFKILL)) {
		wiphy_rfkill_set_hw_state(priv->ieee->wdev.wiphy, false);
		priv->status &= ~STATUS_RF_KILL_HW;
		return 0;
	}

	for (i = 0; i < MAX_RF_KILL_CHECKS; i++) {
		udelay(RF_KILL_CHECK_DELAY);
		read_register(priv->net_dev, IPW_REG_GPIO, &reg);
		value = (value << 1) | ((reg & IPW_BIT_GPIO_RF_KILL) ? 0 : 1);
	}

	if (value == 0) {
		wiphy_rfkill_set_hw_state(priv->ieee->wdev.wiphy, true);
		priv->status |= STATUS_RF_KILL_HW;
	} else {
		wiphy_rfkill_set_hw_state(priv->ieee->wdev.wiphy, false);
		priv->status &= ~STATUS_RF_KILL_HW;
	}

	return (value == 0);
}

static int ipw2100_get_hw_features(struct ipw2100_priv *priv)
{
	u32 addr, len;
	u32 val;

	/*
	 * EEPROM_SRAM_DB_START_ADDRESS using ordinal in ordinal table 1
	 */
	len = sizeof(addr);
	if (ipw2100_get_ordinal
	    (priv, IPW_ORD_EEPROM_SRAM_DB_BLOCK_START_ADDRESS, &addr, &len)) {
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);
		return -EIO;
	}

	IPW_DEBUG_INFO("EEPROM address: %08X\n", addr);

	/*
	 * EEPROM version is the byte at offset 0xfd in firmware
	 * We read 4 bytes, then shift out the byte we actually want */
	read_nic_dword(priv->net_dev, addr + 0xFC, &val);
	priv->eeprom_version = (val >> 24) & 0xFF;
	IPW_DEBUG_INFO("EEPROM version: %d\n", priv->eeprom_version);

	/*
	 *  HW RF Kill enable is bit 0 in byte at offset 0x21 in firmware
	 *
	 *  notice that the EEPROM bit is reverse polarity, i.e.
	 *     bit = 0  signifies HW RF kill switch is supported
	 *     bit = 1  signifies HW RF kill switch is NOT supported
	 */
	read_nic_dword(priv->net_dev, addr + 0x20, &val);
	if (!((val >> 24) & 0x01))
		priv->hw_features |= HW_FEATURE_RFKILL;

	IPW_DEBUG_INFO("HW RF Kill: %ssupported.\n",
		       (priv->hw_features & HW_FEATURE_RFKILL) ? "" : "not ");

	return 0;
}

/*
 * Start firmware execution after power on and intialization
 * The sequence is:
 *  1. Release ARC
 *  2. Wait for f/w initialization completes;
 */
static int ipw2100_start_adapter(struct ipw2100_priv *priv)
{
	int i;
	u32 inta, inta_mask, gpio;

	IPW_DEBUG_INFO("enter\n");

	if (priv->status & STATUS_RUNNING)
		return 0;

	/*
	 * Initialize the hw - drive adapter to DO state by setting
	 * init_done bit. Wait for clk_ready bit and Download
	 * fw & dino ucode
	 */
	if (ipw2100_download_firmware(priv)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Failed to power on the adapter.\n",
		       priv->net_dev->name);
		return -EIO;
	}

	/* Clear the Tx, Rx and Msg queues and the r/w indexes
	 * in the firmware RBD and TBD ring queue */
	ipw2100_queues_initialize(priv);

	ipw2100_hw_set_gpio(priv);

	/* TODO -- Look at disabling interrupts here to make sure none
	 * get fired during FW initialization */

	/* Release ARC - clear reset bit */
	write_register(priv->net_dev, IPW_REG_RESET_REG, 0);

	/* wait for f/w intialization complete */
	IPW_DEBUG_FW("Waiting for f/w initialization to complete...\n");
	i = 5000;
	do {
		schedule_timeout_uninterruptible(msecs_to_jiffies(40));
		/* Todo... wait for sync command ... */

		read_register(priv->net_dev, IPW_REG_INTA, &inta);

		/* check "init done" bit */
		if (inta & IPW2100_INTA_FW_INIT_DONE) {
			/* reset "init done" bit */
			write_register(priv->net_dev, IPW_REG_INTA,
				       IPW2100_INTA_FW_INIT_DONE);
			break;
		}

		/* check error conditions : we check these after the firmware
		 * check so that if there is an error, the interrupt handler
		 * will see it and the adapter will be reset */
		if (inta &
		    (IPW2100_INTA_FATAL_ERROR | IPW2100_INTA_PARITY_ERROR)) {
			/* clear error conditions */
			write_register(priv->net_dev, IPW_REG_INTA,
				       IPW2100_INTA_FATAL_ERROR |
				       IPW2100_INTA_PARITY_ERROR);
		}
	} while (--i);

	/* Clear out any pending INTAs since we aren't supposed to have
	 * interrupts enabled at this point... */
	read_register(priv->net_dev, IPW_REG_INTA, &inta);
	read_register(priv->net_dev, IPW_REG_INTA_MASK, &inta_mask);
	inta &= IPW_INTERRUPT_MASK;
	/* Clear out any pending interrupts */
	if (inta & inta_mask)
		write_register(priv->net_dev, IPW_REG_INTA, inta);

	IPW_DEBUG_FW("f/w initialization complete: %s\n",
		     i ? "SUCCESS" : "FAILED");

	if (!i) {
		printk(KERN_WARNING DRV_NAME
		       ": %s: Firmware did not initialize.\n",
		       priv->net_dev->name);
		return -EIO;
	}

	/* allow firmware to write to GPIO1 & GPIO3 */
	read_register(priv->net_dev, IPW_REG_GPIO, &gpio);

	gpio |= (IPW_BIT_GPIO_GPIO1_MASK | IPW_BIT_GPIO_GPIO3_MASK);

	write_register(priv->net_dev, IPW_REG_GPIO, gpio);

	/* Ready to receive commands */
	priv->status |= STATUS_RUNNING;

	/* The adapter has been reset; we are not associated */
	priv->status &= ~(STATUS_ASSOCIATING | STATUS_ASSOCIATED);

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

static inline void ipw2100_reset_fatalerror(struct ipw2100_priv *priv)
{
	if (!priv->fatal_error)
		return;

	priv->fatal_errors[priv->fatal_index++] = priv->fatal_error;
	priv->fatal_index %= IPW2100_ERROR_QUEUE;
	priv->fatal_error = 0;
}

/* NOTE: Our interrupt is disabled when this method is called */
static int ipw2100_power_cycle_adapter(struct ipw2100_priv *priv)
{
	u32 reg;
	int i;

	IPW_DEBUG_INFO("Power cycling the hardware.\n");

	ipw2100_hw_set_gpio(priv);

	/* Step 1. Stop Master Assert */
	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_STOP_MASTER);

	/* Step 2. Wait for stop Master Assert
	 *         (not more than 50us, otherwise ret error */
	i = 5;
	do {
		udelay(IPW_WAIT_RESET_MASTER_ASSERT_COMPLETE_DELAY);
		read_register(priv->net_dev, IPW_REG_RESET_REG, &reg);

		if (reg & IPW_AUX_HOST_RESET_REG_MASTER_DISABLED)
			break;
	} while (--i);

	priv->status &= ~STATUS_RESET_PENDING;

	if (!i) {
		IPW_DEBUG_INFO
		    ("exit - waited too long for master assert stop\n");
		return -EIO;
	}

	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_SW_RESET);

	/* Reset any fatal_error conditions */
	ipw2100_reset_fatalerror(priv);

	/* At this point, the adapter is now stopped and disabled */
	priv->status &= ~(STATUS_RUNNING | STATUS_ASSOCIATING |
			  STATUS_ASSOCIATED | STATUS_ENABLED);

	return 0;
}

/*
 * Send the CARD_DISABLE_PHY_OFF command to the card to disable it
 *
 * After disabling, if the card was associated, a STATUS_ASSN_LOST will be sent.
 *
 * STATUS_CARD_DISABLE_NOTIFICATION will be sent regardless of
 * if STATUS_ASSN_LOST is sent.
 */
static int ipw2100_hw_phy_off(struct ipw2100_priv *priv)
{

#define HW_PHY_OFF_LOOP_DELAY (HZ / 5000)

	struct host_command cmd = {
		.host_command = CARD_DISABLE_PHY_OFF,
		.host_command_sequence = 0,
		.host_command_length = 0,
	};
	int err, i;
	u32 val1, val2;

	IPW_DEBUG_HC("CARD_DISABLE_PHY_OFF\n");

	/* Turn off the radio */
	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

	for (i = 0; i < 2500; i++) {
		read_nic_dword(priv->net_dev, IPW2100_CONTROL_REG, &val1);
		read_nic_dword(priv->net_dev, IPW2100_COMMAND, &val2);

		if ((val1 & IPW2100_CONTROL_PHY_OFF) &&
		    (val2 & IPW2100_COMMAND_PHY_OFF))
			return 0;

		schedule_timeout_uninterruptible(HW_PHY_OFF_LOOP_DELAY);
	}

	return -EIO;
}

static int ipw2100_enable_adapter(struct ipw2100_priv *priv)
{
	struct host_command cmd = {
		.host_command = HOST_COMPLETE,
		.host_command_sequence = 0,
		.host_command_length = 0
	};
	int err = 0;

	IPW_DEBUG_HC("HOST_COMPLETE\n");

	if (priv->status & STATUS_ENABLED)
		return 0;

	mutex_lock(&priv->adapter_mutex);

	if (rf_kill_active(priv)) {
		IPW_DEBUG_HC("Command aborted due to RF kill active.\n");
		goto fail_up;
	}

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err) {
		IPW_DEBUG_INFO("Failed to send HOST_COMPLETE command\n");
		goto fail_up;
	}

	err = ipw2100_wait_for_card_state(priv, IPW_HW_STATE_ENABLED);
	if (err) {
		IPW_DEBUG_INFO("%s: card not responding to init command.\n",
			       priv->net_dev->name);
		goto fail_up;
	}

	if (priv->stop_hang_check) {
		priv->stop_hang_check = 0;
		schedule_delayed_work(&priv->hang_check, HZ / 2);
	}

      fail_up:
	mutex_unlock(&priv->adapter_mutex);
	return err;
}

static int ipw2100_hw_stop_adapter(struct ipw2100_priv *priv)
{
#define HW_POWER_DOWN_DELAY (msecs_to_jiffies(100))

	struct host_command cmd = {
		.host_command = HOST_PRE_POWER_DOWN,
		.host_command_sequence = 0,
		.host_command_length = 0,
	};
	int err, i;
	u32 reg;

	if (!(priv->status & STATUS_RUNNING))
		return 0;

	priv->status |= STATUS_STOPPING;

	/* We can only shut down the card if the firmware is operational.  So,
	 * if we haven't reset since a fatal_error, then we can not send the
	 * shutdown commands. */
	if (!priv->fatal_error) {
		/* First, make sure the adapter is enabled so that the PHY_OFF
		 * command can shut it down */
		ipw2100_enable_adapter(priv);

		err = ipw2100_hw_phy_off(priv);
		if (err)
			printk(KERN_WARNING DRV_NAME
			       ": Error disabling radio %d\n", err);

		/*
		 * If in D0-standby mode going directly to D3 may cause a
		 * PCI bus violation.  Therefore we must change out of the D0
		 * state.
		 *
		 * Sending the PREPARE_FOR_POWER_DOWN will restrict the
		 * hardware from going into standby mode and will transition
		 * out of D0-standby if it is already in that state.
		 *
		 * STATUS_PREPARE_POWER_DOWN_COMPLETE will be sent by the
		 * driver upon completion.  Once received, the driver can
		 * proceed to the D3 state.
		 *
		 * Prepare for power down command to fw.  This command would
		 * take HW out of D0-standby and prepare it for D3 state.
		 *
		 * Currently FW does not support event notification for this
		 * event. Therefore, skip waiting for it.  Just wait a fixed
		 * 100ms
		 */
		IPW_DEBUG_HC("HOST_PRE_POWER_DOWN\n");

		err = ipw2100_hw_send_command(priv, &cmd);
		if (err)
			printk(KERN_WARNING DRV_NAME ": "
			       "%s: Power down command failed: Error %d\n",
			       priv->net_dev->name, err);
		else
			schedule_timeout_uninterruptible(HW_POWER_DOWN_DELAY);
	}

	priv->status &= ~STATUS_ENABLED;

	/*
	 * Set GPIO 3 writable by FW; GPIO 1 writable
	 * by driver and enable clock
	 */
	ipw2100_hw_set_gpio(priv);

	/*
	 * Power down adapter.  Sequence:
	 * 1. Stop master assert (RESET_REG[9]=1)
	 * 2. Wait for stop master (RESET_REG[8]==1)
	 * 3. S/w reset assert (RESET_REG[7] = 1)
	 */

	/* Stop master assert */
	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_STOP_MASTER);

	/* wait stop master not more than 50 usec.
	 * Otherwise return error. */
	for (i = 5; i > 0; i--) {
		udelay(10);

		/* Check master stop bit */
		read_register(priv->net_dev, IPW_REG_RESET_REG, &reg);

		if (reg & IPW_AUX_HOST_RESET_REG_MASTER_DISABLED)
			break;
	}

	if (i == 0)
		printk(KERN_WARNING DRV_NAME
		       ": %s: Could now power down adapter.\n",
		       priv->net_dev->name);

	/* assert s/w reset */
	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_SW_RESET);

	priv->status &= ~(STATUS_RUNNING | STATUS_STOPPING);

	return 0;
}

static int ipw2100_disable_adapter(struct ipw2100_priv *priv)
{
	struct host_command cmd = {
		.host_command = CARD_DISABLE,
		.host_command_sequence = 0,
		.host_command_length = 0
	};
	int err = 0;

	IPW_DEBUG_HC("CARD_DISABLE\n");

	if (!(priv->status & STATUS_ENABLED))
		return 0;

	/* Make sure we clear the associated state */
	priv->status &= ~(STATUS_ASSOCIATED | STATUS_ASSOCIATING);

	if (!priv->stop_hang_check) {
		priv->stop_hang_check = 1;
		cancel_delayed_work(&priv->hang_check);
	}

	mutex_lock(&priv->adapter_mutex);

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       ": exit - failed to send CARD_DISABLE command\n");
		goto fail_up;
	}

	err = ipw2100_wait_for_card_state(priv, IPW_HW_STATE_DISABLED);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       ": exit - card failed to change to DISABLED\n");
		goto fail_up;
	}

	IPW_DEBUG_INFO("TODO: implement scan state machine\n");

      fail_up:
	mutex_unlock(&priv->adapter_mutex);
	return err;
}

static int ipw2100_set_scan_options(struct ipw2100_priv *priv)
{
	struct host_command cmd = {
		.host_command = SET_SCAN_OPTIONS,
		.host_command_sequence = 0,
		.host_command_length = 8
	};
	int err;

	IPW_DEBUG_INFO("enter\n");

	IPW_DEBUG_SCAN("setting scan options\n");

	cmd.host_command_parameters[0] = 0;

	if (!(priv->config & CFG_ASSOCIATE))
		cmd.host_command_parameters[0] |= IPW_SCAN_NOASSOCIATE;
	if ((priv->ieee->sec.flags & SEC_ENABLED) && priv->ieee->sec.enabled)
		cmd.host_command_parameters[0] |= IPW_SCAN_MIXED_CELL;
	if (priv->config & CFG_PASSIVE_SCAN)
		cmd.host_command_parameters[0] |= IPW_SCAN_PASSIVE;

	cmd.host_command_parameters[1] = priv->channel_mask;

	err = ipw2100_hw_send_command(priv, &cmd);

	IPW_DEBUG_HC("SET_SCAN_OPTIONS 0x%04X\n",
		     cmd.host_command_parameters[0]);

	return err;
}

static int ipw2100_start_scan(struct ipw2100_priv *priv)
{
	struct host_command cmd = {
		.host_command = BROADCAST_SCAN,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	IPW_DEBUG_HC("START_SCAN\n");

	cmd.host_command_parameters[0] = 0;

	/* No scanning if in monitor mode */
	if (priv->ieee->iw_mode == IW_MODE_MONITOR)
		return 1;

	if (priv->status & STATUS_SCANNING) {
		IPW_DEBUG_SCAN("Scan requested while already in scan...\n");
		return 0;
	}

	IPW_DEBUG_INFO("enter\n");

	/* Not clearing here; doing so makes iwlist always return nothing...
	 *
	 * We should modify the table logic to use aging tables vs. clearing
	 * the table on each scan start.
	 */
	IPW_DEBUG_SCAN("starting scan\n");

	priv->status |= STATUS_SCANNING;
	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		priv->status &= ~STATUS_SCANNING;

	IPW_DEBUG_INFO("exit\n");

	return err;
}

static const struct libipw_geo ipw_geos[] = {
	{			/* Restricted */
	 "---",
	 .bg_channels = 14,
	 .bg = {{2412, 1}, {2417, 2}, {2422, 3},
		{2427, 4}, {2432, 5}, {2437, 6},
		{2442, 7}, {2447, 8}, {2452, 9},
		{2457, 10}, {2462, 11}, {2467, 12},
		{2472, 13}, {2484, 14}},
	 },
};

static int ipw2100_up(struct ipw2100_priv *priv, int deferred)
{
	unsigned long flags;
	int rc = 0;
	u32 lock;
	u32 ord_len = sizeof(lock);

	/* Age scan list entries found before suspend */
	if (priv->suspend_time) {
		libipw_networks_age(priv->ieee, priv->suspend_time);
		priv->suspend_time = 0;
	}

	/* Quiet if manually disabled. */
	if (priv->status & STATUS_RF_KILL_SW) {
		IPW_DEBUG_INFO("%s: Radio is disabled by Manual Disable "
			       "switch\n", priv->net_dev->name);
		return 0;
	}

	/* the ipw2100 hardware really doesn't want power management delays
	 * longer than 175usec
	 */
	pm_qos_update_request(&ipw2100_pm_qos_req, 175);

	/* If the interrupt is enabled, turn it off... */
	spin_lock_irqsave(&priv->low_lock, flags);
	ipw2100_disable_interrupts(priv);

	/* Reset any fatal_error conditions */
	ipw2100_reset_fatalerror(priv);
	spin_unlock_irqrestore(&priv->low_lock, flags);

	if (priv->status & STATUS_POWERED ||
	    (priv->status & STATUS_RESET_PENDING)) {
		/* Power cycle the card ... */
		if (ipw2100_power_cycle_adapter(priv)) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Could not cycle adapter.\n",
			       priv->net_dev->name);
			rc = 1;
			goto exit;
		}
	} else
		priv->status |= STATUS_POWERED;

	/* Load the firmware, start the clocks, etc. */
	if (ipw2100_start_adapter(priv)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Failed to start the firmware.\n",
		       priv->net_dev->name);
		rc = 1;
		goto exit;
	}

	ipw2100_initialize_ordinals(priv);

	/* Determine capabilities of this particular HW configuration */
	if (ipw2100_get_hw_features(priv)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Failed to determine HW features.\n",
		       priv->net_dev->name);
		rc = 1;
		goto exit;
	}

	/* Initialize the geo */
	libipw_set_geo(priv->ieee, &ipw_geos[0]);
	priv->ieee->freq_band = LIBIPW_24GHZ_BAND;

	lock = LOCK_NONE;
	if (ipw2100_set_ordinal(priv, IPW_ORD_PERS_DB_LOCK, &lock, &ord_len)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Failed to clear ordinal lock.\n",
		       priv->net_dev->name);
		rc = 1;
		goto exit;
	}

	priv->status &= ~STATUS_SCANNING;

	if (rf_kill_active(priv)) {
		printk(KERN_INFO "%s: Radio is disabled by RF switch.\n",
		       priv->net_dev->name);

		if (priv->stop_rf_kill) {
			priv->stop_rf_kill = 0;
			schedule_delayed_work(&priv->rf_kill,
					      round_jiffies_relative(HZ));
		}

		deferred = 1;
	}

	/* Turn on the interrupt so that commands can be processed */
	ipw2100_enable_interrupts(priv);

	/* Send all of the commands that must be sent prior to
	 * HOST_COMPLETE */
	if (ipw2100_adapter_setup(priv)) {
		printk(KERN_ERR DRV_NAME ": %s: Failed to start the card.\n",
		       priv->net_dev->name);
		rc = 1;
		goto exit;
	}

	if (!deferred) {
		/* Enable the adapter - sends HOST_COMPLETE */
		if (ipw2100_enable_adapter(priv)) {
			printk(KERN_ERR DRV_NAME ": "
			       "%s: failed in call to enable adapter.\n",
			       priv->net_dev->name);
			ipw2100_hw_stop_adapter(priv);
			rc = 1;
			goto exit;
		}

		/* Start a scan . . . */
		ipw2100_set_scan_options(priv);
		ipw2100_start_scan(priv);
	}

      exit:
	return rc;
}

static void ipw2100_down(struct ipw2100_priv *priv)
{
	unsigned long flags;
	union iwreq_data wrqu = {
		.ap_addr = {
			    .sa_family = ARPHRD_ETHER}
	};
	int associated = priv->status & STATUS_ASSOCIATED;

	/* Kill the RF switch timer */
	if (!priv->stop_rf_kill) {
		priv->stop_rf_kill = 1;
		cancel_delayed_work(&priv->rf_kill);
	}

	/* Kill the firmware hang check timer */
	if (!priv->stop_hang_check) {
		priv->stop_hang_check = 1;
		cancel_delayed_work(&priv->hang_check);
	}

	/* Kill any pending resets */
	if (priv->status & STATUS_RESET_PENDING)
		cancel_delayed_work(&priv->reset_work);

	/* Make sure the interrupt is on so that FW commands will be
	 * processed correctly */
	spin_lock_irqsave(&priv->low_lock, flags);
	ipw2100_enable_interrupts(priv);
	spin_unlock_irqrestore(&priv->low_lock, flags);

	if (ipw2100_hw_stop_adapter(priv))
		printk(KERN_ERR DRV_NAME ": %s: Error stopping adapter.\n",
		       priv->net_dev->name);

	/* Do not disable the interrupt until _after_ we disable
	 * the adaptor.  Otherwise the CARD_DISABLE command will never
	 * be ack'd by the firmware */
	spin_lock_irqsave(&priv->low_lock, flags);
	ipw2100_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->low_lock, flags);

	pm_qos_update_request(&ipw2100_pm_qos_req, PM_QOS_DEFAULT_VALUE);

	/* We have to signal any supplicant if we are disassociating */
	if (associated)
		wireless_send_event(priv->net_dev, SIOCGIWAP, &wrqu, NULL);

	priv->status &= ~(STATUS_ASSOCIATED | STATUS_ASSOCIATING);
	netif_carrier_off(priv->net_dev);
	netif_stop_queue(priv->net_dev);
}

static int ipw2100_wdev_init(struct net_device *dev)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	const struct libipw_geo *geo = libipw_get_geo(priv->ieee);
	struct wireless_dev *wdev = &priv->ieee->wdev;
	int i;

	memcpy(wdev->wiphy->perm_addr, priv->mac_addr, ETH_ALEN);

	/* fill-out priv->ieee->bg_band */
	if (geo->bg_channels) {
		struct ieee80211_supported_band *bg_band = &priv->ieee->bg_band;

		bg_band->band = IEEE80211_BAND_2GHZ;
		bg_band->n_channels = geo->bg_channels;
		bg_band->channels = kcalloc(geo->bg_channels,
					    sizeof(struct ieee80211_channel),
					    GFP_KERNEL);
		if (!bg_band->channels) {
			ipw2100_down(priv);
			return -ENOMEM;
		}
		/* translate geo->bg to bg_band.channels */
		for (i = 0; i < geo->bg_channels; i++) {
			bg_band->channels[i].band = IEEE80211_BAND_2GHZ;
			bg_band->channels[i].center_freq = geo->bg[i].freq;
			bg_band->channels[i].hw_value = geo->bg[i].channel;
			bg_band->channels[i].max_power = geo->bg[i].max_power;
			if (geo->bg[i].flags & LIBIPW_CH_PASSIVE_ONLY)
				bg_band->channels[i].flags |=
					IEEE80211_CHAN_PASSIVE_SCAN;
			if (geo->bg[i].flags & LIBIPW_CH_NO_IBSS)
				bg_band->channels[i].flags |=
					IEEE80211_CHAN_NO_IBSS;
			if (geo->bg[i].flags & LIBIPW_CH_RADAR_DETECT)
				bg_band->channels[i].flags |=
					IEEE80211_CHAN_RADAR;
			/* No equivalent for LIBIPW_CH_80211H_RULES,
			   LIBIPW_CH_UNIFORM_SPREADING, or
			   LIBIPW_CH_B_ONLY... */
		}
		/* point at bitrate info */
		bg_band->bitrates = ipw2100_bg_rates;
		bg_band->n_bitrates = RATE_COUNT;

		wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = bg_band;
	}

	wdev->wiphy->cipher_suites = ipw_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(ipw_cipher_suites);

	set_wiphy_dev(wdev->wiphy, &priv->pci_dev->dev);
	if (wiphy_register(wdev->wiphy))
		return -EIO;
	return 0;
}

static void ipw2100_reset_adapter(struct work_struct *work)
{
	struct ipw2100_priv *priv =
		container_of(work, struct ipw2100_priv, reset_work.work);
	unsigned long flags;
	union iwreq_data wrqu = {
		.ap_addr = {
			    .sa_family = ARPHRD_ETHER}
	};
	int associated = priv->status & STATUS_ASSOCIATED;

	spin_lock_irqsave(&priv->low_lock, flags);
	IPW_DEBUG_INFO(": %s: Restarting adapter.\n", priv->net_dev->name);
	priv->resets++;
	priv->status &= ~(STATUS_ASSOCIATED | STATUS_ASSOCIATING);
	priv->status |= STATUS_SECURITY_UPDATED;

	/* Force a power cycle even if interface hasn't been opened
	 * yet */
	cancel_delayed_work(&priv->reset_work);
	priv->status |= STATUS_RESET_PENDING;
	spin_unlock_irqrestore(&priv->low_lock, flags);

	mutex_lock(&priv->action_mutex);
	/* stop timed checks so that they don't interfere with reset */
	priv->stop_hang_check = 1;
	cancel_delayed_work(&priv->hang_check);

	/* We have to signal any supplicant if we are disassociating */
	if (associated)
		wireless_send_event(priv->net_dev, SIOCGIWAP, &wrqu, NULL);

	ipw2100_up(priv, 0);
	mutex_unlock(&priv->action_mutex);

}

static void isr_indicate_associated(struct ipw2100_priv *priv, u32 status)
{

#define MAC_ASSOCIATION_READ_DELAY (HZ)
	int ret;
	unsigned int len, essid_len;
	char essid[IW_ESSID_MAX_SIZE];
	u32 txrate;
	u32 chan;
	char *txratename;
	u8 bssid[ETH_ALEN];
	DECLARE_SSID_BUF(ssid);

	/*
	 * TBD: BSSID is usually 00:00:00:00:00:00 here and not
	 *      an actual MAC of the AP. Seems like FW sets this
	 *      address too late. Read it later and expose through
	 *      /proc or schedule a later task to query and update
	 */

	essid_len = IW_ESSID_MAX_SIZE;
	ret = ipw2100_get_ordinal(priv, IPW_ORD_STAT_ASSN_SSID,
				  essid, &essid_len);
	if (ret) {
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);
		return;
	}

	len = sizeof(u32);
	ret = ipw2100_get_ordinal(priv, IPW_ORD_CURRENT_TX_RATE, &txrate, &len);
	if (ret) {
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);
		return;
	}

	len = sizeof(u32);
	ret = ipw2100_get_ordinal(priv, IPW_ORD_OUR_FREQ, &chan, &len);
	if (ret) {
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);
		return;
	}
	len = ETH_ALEN;
	ret = ipw2100_get_ordinal(priv, IPW_ORD_STAT_ASSN_AP_BSSID, bssid,
				  &len);
	if (ret) {
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);
		return;
	}
	memcpy(priv->ieee->bssid, bssid, ETH_ALEN);

	switch (txrate) {
	case TX_RATE_1_MBIT:
		txratename = "1Mbps";
		break;
	case TX_RATE_2_MBIT:
		txratename = "2Mbsp";
		break;
	case TX_RATE_5_5_MBIT:
		txratename = "5.5Mbps";
		break;
	case TX_RATE_11_MBIT:
		txratename = "11Mbps";
		break;
	default:
		IPW_DEBUG_INFO("Unknown rate: %d\n", txrate);
		txratename = "unknown rate";
		break;
	}

	IPW_DEBUG_INFO("%s: Associated with '%s' at %s, channel %d (BSSID=%pM)\n",
		       priv->net_dev->name, print_ssid(ssid, essid, essid_len),
		       txratename, chan, bssid);

	/* now we copy read ssid into dev */
	if (!(priv->config & CFG_STATIC_ESSID)) {
		priv->essid_len = min((u8) essid_len, (u8) IW_ESSID_MAX_SIZE);
		memcpy(priv->essid, essid, priv->essid_len);
	}
	priv->channel = chan;
	memcpy(priv->bssid, bssid, ETH_ALEN);

	priv->status |= STATUS_ASSOCIATING;
	priv->connect_start = get_seconds();

	schedule_delayed_work(&priv->wx_event_work, HZ / 10);
}

static int ipw2100_set_essid(struct ipw2100_priv *priv, char *essid,
			     int length, int batch_mode)
{
	int ssid_len = min(length, IW_ESSID_MAX_SIZE);
	struct host_command cmd = {
		.host_command = SSID,
		.host_command_sequence = 0,
		.host_command_length = ssid_len
	};
	int err;
	DECLARE_SSID_BUF(ssid);

	IPW_DEBUG_HC("SSID: '%s'\n", print_ssid(ssid, essid, ssid_len));

	if (ssid_len)
		memcpy(cmd.host_command_parameters, essid, ssid_len);

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	/* Bug in FW currently doesn't honor bit 0 in SET_SCAN_OPTIONS to
	 * disable auto association -- so we cheat by setting a bogus SSID */
	if (!ssid_len && !(priv->config & CFG_ASSOCIATE)) {
		int i;
		u8 *bogus = (u8 *) cmd.host_command_parameters;
		for (i = 0; i < IW_ESSID_MAX_SIZE; i++)
			bogus[i] = 0x18 + i;
		cmd.host_command_length = IW_ESSID_MAX_SIZE;
	}

	/* NOTE:  We always send the SSID command even if the provided ESSID is
	 * the same as what we currently think is set. */

	err = ipw2100_hw_send_command(priv, &cmd);
	if (!err) {
		memset(priv->essid + ssid_len, 0, IW_ESSID_MAX_SIZE - ssid_len);
		memcpy(priv->essid, essid, ssid_len);
		priv->essid_len = ssid_len;
	}

	if (!batch_mode) {
		if (ipw2100_enable_adapter(priv))
			err = -EIO;
	}

	return err;
}

static void isr_indicate_association_lost(struct ipw2100_priv *priv, u32 status)
{
	DECLARE_SSID_BUF(ssid);

	IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE | IPW_DL_ASSOC,
		  "disassociated: '%s' %pM\n",
		  print_ssid(ssid, priv->essid, priv->essid_len),
		  priv->bssid);

	priv->status &= ~(STATUS_ASSOCIATED | STATUS_ASSOCIATING);

	if (priv->status & STATUS_STOPPING) {
		IPW_DEBUG_INFO("Card is stopping itself, discard ASSN_LOST.\n");
		return;
	}

	memset(priv->bssid, 0, ETH_ALEN);
	memset(priv->ieee->bssid, 0, ETH_ALEN);

	netif_carrier_off(priv->net_dev);
	netif_stop_queue(priv->net_dev);

	if (!(priv->status & STATUS_RUNNING))
		return;

	if (priv->status & STATUS_SECURITY_UPDATED)
		schedule_delayed_work(&priv->security_work, 0);

	schedule_delayed_work(&priv->wx_event_work, 0);
}

static void isr_indicate_rf_kill(struct ipw2100_priv *priv, u32 status)
{
	IPW_DEBUG_INFO("%s: RF Kill state changed to radio OFF.\n",
		       priv->net_dev->name);

	/* RF_KILL is now enabled (else we wouldn't be here) */
	wiphy_rfkill_set_hw_state(priv->ieee->wdev.wiphy, true);
	priv->status |= STATUS_RF_KILL_HW;

	/* Make sure the RF Kill check timer is running */
	priv->stop_rf_kill = 0;
	mod_delayed_work(system_wq, &priv->rf_kill, round_jiffies_relative(HZ));
}

static void send_scan_event(void *data)
{
	struct ipw2100_priv *priv = data;
	union iwreq_data wrqu;

	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wireless_send_event(priv->net_dev, SIOCGIWSCAN, &wrqu, NULL);
}

static void ipw2100_scan_event_later(struct work_struct *work)
{
	send_scan_event(container_of(work, struct ipw2100_priv,
					scan_event_later.work));
}

static void ipw2100_scan_event_now(struct work_struct *work)
{
	send_scan_event(container_of(work, struct ipw2100_priv,
					scan_event_now));
}

static void isr_scan_complete(struct ipw2100_priv *priv, u32 status)
{
	IPW_DEBUG_SCAN("scan complete\n");
	/* Age the scan results... */
	priv->ieee->scans++;
	priv->status &= ~STATUS_SCANNING;

	/* Only userspace-requested scan completion events go out immediately */
	if (!priv->user_requested_scan) {
		if (!delayed_work_pending(&priv->scan_event_later))
			schedule_delayed_work(&priv->scan_event_later,
					      round_jiffies_relative(msecs_to_jiffies(4000)));
	} else {
		priv->user_requested_scan = 0;
		cancel_delayed_work(&priv->scan_event_later);
		schedule_work(&priv->scan_event_now);
	}
}

#ifdef CONFIG_IPW2100_DEBUG
#define IPW2100_HANDLER(v, f) { v, f, # v }
struct ipw2100_status_indicator {
	int status;
	void (*cb) (struct ipw2100_priv * priv, u32 status);
	char *name;
};
#else
#define IPW2100_HANDLER(v, f) { v, f }
struct ipw2100_status_indicator {
	int status;
	void (*cb) (struct ipw2100_priv * priv, u32 status);
};
#endif				/* CONFIG_IPW2100_DEBUG */

static void isr_indicate_scanning(struct ipw2100_priv *priv, u32 status)
{
	IPW_DEBUG_SCAN("Scanning...\n");
	priv->status |= STATUS_SCANNING;
}

static const struct ipw2100_status_indicator status_handlers[] = {
	IPW2100_HANDLER(IPW_STATE_INITIALIZED, NULL),
	IPW2100_HANDLER(IPW_STATE_COUNTRY_FOUND, NULL),
	IPW2100_HANDLER(IPW_STATE_ASSOCIATED, isr_indicate_associated),
	IPW2100_HANDLER(IPW_STATE_ASSN_LOST, isr_indicate_association_lost),
	IPW2100_HANDLER(IPW_STATE_ASSN_CHANGED, NULL),
	IPW2100_HANDLER(IPW_STATE_SCAN_COMPLETE, isr_scan_complete),
	IPW2100_HANDLER(IPW_STATE_ENTERED_PSP, NULL),
	IPW2100_HANDLER(IPW_STATE_LEFT_PSP, NULL),
	IPW2100_HANDLER(IPW_STATE_RF_KILL, isr_indicate_rf_kill),
	IPW2100_HANDLER(IPW_STATE_DISABLED, NULL),
	IPW2100_HANDLER(IPW_STATE_POWER_DOWN, NULL),
	IPW2100_HANDLER(IPW_STATE_SCANNING, isr_indicate_scanning),
	IPW2100_HANDLER(-1, NULL)
};

static void isr_status_change(struct ipw2100_priv *priv, int status)
{
	int i;

	if (status == IPW_STATE_SCANNING &&
	    priv->status & STATUS_ASSOCIATED &&
	    !(priv->status & STATUS_SCANNING)) {
		IPW_DEBUG_INFO("Scan detected while associated, with "
			       "no scan request.  Restarting firmware.\n");

		/* Wake up any sleeping jobs */
		schedule_reset(priv);
	}

	for (i = 0; status_handlers[i].status != -1; i++) {
		if (status == status_handlers[i].status) {
			IPW_DEBUG_NOTIF("Status change: %s\n",
					status_handlers[i].name);
			if (status_handlers[i].cb)
				status_handlers[i].cb(priv, status);
			priv->wstats.status = status;
			return;
		}
	}

	IPW_DEBUG_NOTIF("unknown status received: %04x\n", status);
}

static void isr_rx_complete_command(struct ipw2100_priv *priv,
				    struct ipw2100_cmd_header *cmd)
{
#ifdef CONFIG_IPW2100_DEBUG
	if (cmd->host_command_reg < ARRAY_SIZE(command_types)) {
		IPW_DEBUG_HC("Command completed '%s (%d)'\n",
			     command_types[cmd->host_command_reg],
			     cmd->host_command_reg);
	}
#endif
	if (cmd->host_command_reg == HOST_COMPLETE)
		priv->status |= STATUS_ENABLED;

	if (cmd->host_command_reg == CARD_DISABLE)
		priv->status &= ~STATUS_ENABLED;

	priv->status &= ~STATUS_CMD_ACTIVE;

	wake_up_interruptible(&priv->wait_command_queue);
}

#ifdef CONFIG_IPW2100_DEBUG
static const char *frame_types[] = {
	"COMMAND_STATUS_VAL",
	"STATUS_CHANGE_VAL",
	"P80211_DATA_VAL",
	"P8023_DATA_VAL",
	"HOST_NOTIFICATION_VAL"
};
#endif

static int ipw2100_alloc_skb(struct ipw2100_priv *priv,
				    struct ipw2100_rx_packet *packet)
{
	packet->skb = dev_alloc_skb(sizeof(struct ipw2100_rx));
	if (!packet->skb)
		return -ENOMEM;

	packet->rxp = (struct ipw2100_rx *)packet->skb->data;
	packet->dma_addr = pci_map_single(priv->pci_dev, packet->skb->data,
					  sizeof(struct ipw2100_rx),
					  PCI_DMA_FROMDEVICE);
	/* NOTE: pci_map_single does not return an error code, and 0 is a valid
	 *       dma_addr */

	return 0;
}

#define SEARCH_ERROR   0xffffffff
#define SEARCH_FAIL    0xfffffffe
#define SEARCH_SUCCESS 0xfffffff0
#define SEARCH_DISCARD 0
#define SEARCH_SNAPSHOT 1

#define SNAPSHOT_ADDR(ofs) (priv->snapshot[((ofs) >> 12) & 0xff] + ((ofs) & 0xfff))
static void ipw2100_snapshot_free(struct ipw2100_priv *priv)
{
	int i;
	if (!priv->snapshot[0])
		return;
	for (i = 0; i < 0x30; i++)
		kfree(priv->snapshot[i]);
	priv->snapshot[0] = NULL;
}

#ifdef IPW2100_DEBUG_C3
static int ipw2100_snapshot_alloc(struct ipw2100_priv *priv)
{
	int i;
	if (priv->snapshot[0])
		return 1;
	for (i = 0; i < 0x30; i++) {
		priv->snapshot[i] = kmalloc(0x1000, GFP_ATOMIC);
		if (!priv->snapshot[i]) {
			IPW_DEBUG_INFO("%s: Error allocating snapshot "
				       "buffer %d\n", priv->net_dev->name, i);
			while (i > 0)
				kfree(priv->snapshot[--i]);
			priv->snapshot[0] = NULL;
			return 0;
		}
	}

	return 1;
}

static u32 ipw2100_match_buf(struct ipw2100_priv *priv, u8 * in_buf,
				    size_t len, int mode)
{
	u32 i, j;
	u32 tmp;
	u8 *s, *d;
	u32 ret;

	s = in_buf;
	if (mode == SEARCH_SNAPSHOT) {
		if (!ipw2100_snapshot_alloc(priv))
			mode = SEARCH_DISCARD;
	}

	for (ret = SEARCH_FAIL, i = 0; i < 0x30000; i += 4) {
		read_nic_dword(priv->net_dev, i, &tmp);
		if (mode == SEARCH_SNAPSHOT)
			*(u32 *) SNAPSHOT_ADDR(i) = tmp;
		if (ret == SEARCH_FAIL) {
			d = (u8 *) & tmp;
			for (j = 0; j < 4; j++) {
				if (*s != *d) {
					s = in_buf;
					continue;
				}

				s++;
				d++;

				if ((s - in_buf) == len)
					ret = (i + j) - len + 1;
			}
		} else if (mode == SEARCH_DISCARD)
			return ret;
	}

	return ret;
}
#endif

/*
 *
 * 0) Disconnect the SKB from the firmware (just unmap)
 * 1) Pack the ETH header into the SKB
 * 2) Pass the SKB to the network stack
 *
 * When packet is provided by the firmware, it contains the following:
 *
 * .  libipw_hdr
 * .  libipw_snap_hdr
 *
 * The size of the constructed ethernet
 *
 */
#ifdef IPW2100_RX_DEBUG
static u8 packet_data[IPW_RX_NIC_BUFFER_LENGTH];
#endif

static void ipw2100_corruption_detected(struct ipw2100_priv *priv, int i)
{
#ifdef IPW2100_DEBUG_C3
	struct ipw2100_status *status = &priv->status_queue.drv[i];
	u32 match, reg;
	int j;
#endif

	IPW_DEBUG_INFO(": PCI latency error detected at 0x%04zX.\n",
		       i * sizeof(struct ipw2100_status));

#ifdef IPW2100_DEBUG_C3
	/* Halt the firmware so we can get a good image */
	write_register(priv->net_dev, IPW_REG_RESET_REG,
		       IPW_AUX_HOST_RESET_REG_STOP_MASTER);
	j = 5;
	do {
		udelay(IPW_WAIT_RESET_MASTER_ASSERT_COMPLETE_DELAY);
		read_register(priv->net_dev, IPW_REG_RESET_REG, &reg);

		if (reg & IPW_AUX_HOST_RESET_REG_MASTER_DISABLED)
			break;
	} while (j--);

	match = ipw2100_match_buf(priv, (u8 *) status,
				  sizeof(struct ipw2100_status),
				  SEARCH_SNAPSHOT);
	if (match < SEARCH_SUCCESS)
		IPW_DEBUG_INFO("%s: DMA status match in Firmware at "
			       "offset 0x%06X, length %d:\n",
			       priv->net_dev->name, match,
			       sizeof(struct ipw2100_status));
	else
		IPW_DEBUG_INFO("%s: No DMA status match in "
			       "Firmware.\n", priv->net_dev->name);

	printk_buf((u8 *) priv->status_queue.drv,
		   sizeof(struct ipw2100_status) * RX_QUEUE_LENGTH);
#endif

	priv->fatal_error = IPW2100_ERR_C3_CORRUPTION;
	priv->net_dev->stats.rx_errors++;
	schedule_reset(priv);
}

static void isr_rx(struct ipw2100_priv *priv, int i,
			  struct libipw_rx_stats *stats)
{
	struct net_device *dev = priv->net_dev;
	struct ipw2100_status *status = &priv->status_queue.drv[i];
	struct ipw2100_rx_packet *packet = &priv->rx_buffers[i];

	IPW_DEBUG_RX("Handler...\n");

	if (unlikely(status->frame_size > skb_tailroom(packet->skb))) {
		IPW_DEBUG_INFO("%s: frame_size (%u) > skb_tailroom (%u)!"
			       "  Dropping.\n",
			       dev->name,
			       status->frame_size, skb_tailroom(packet->skb));
		dev->stats.rx_errors++;
		return;
	}

	if (unlikely(!netif_running(dev))) {
		dev->stats.rx_errors++;
		priv->wstats.discard.misc++;
		IPW_DEBUG_DROP("Dropping packet while interface is not up.\n");
		return;
	}

	if (unlikely(priv->ieee->iw_mode != IW_MODE_MONITOR &&
		     !(priv->status & STATUS_ASSOCIATED))) {
		IPW_DEBUG_DROP("Dropping packet while not associated.\n");
		priv->wstats.discard.misc++;
		return;
	}

	pci_unmap_single(priv->pci_dev,
			 packet->dma_addr,
			 sizeof(struct ipw2100_rx), PCI_DMA_FROMDEVICE);

	skb_put(packet->skb, status->frame_size);

#ifdef IPW2100_RX_DEBUG
	/* Make a copy of the frame so we can dump it to the logs if
	 * libipw_rx fails */
	skb_copy_from_linear_data(packet->skb, packet_data,
				  min_t(u32, status->frame_size,
					     IPW_RX_NIC_BUFFER_LENGTH));
#endif

	if (!libipw_rx(priv->ieee, packet->skb, stats)) {
#ifdef IPW2100_RX_DEBUG
		IPW_DEBUG_DROP("%s: Non consumed packet:\n",
			       dev->name);
		printk_buf(IPW_DL_DROP, packet_data, status->frame_size);
#endif
		dev->stats.rx_errors++;

		/* libipw_rx failed, so it didn't free the SKB */
		dev_kfree_skb_any(packet->skb);
		packet->skb = NULL;
	}

	/* We need to allocate a new SKB and attach it to the RDB. */
	if (unlikely(ipw2100_alloc_skb(priv, packet))) {
		printk(KERN_WARNING DRV_NAME ": "
		       "%s: Unable to allocate SKB onto RBD ring - disabling "
		       "adapter.\n", dev->name);
		/* TODO: schedule adapter shutdown */
		IPW_DEBUG_INFO("TODO: Shutdown adapter...\n");
	}

	/* Update the RDB entry */
	priv->rx_queue.drv[i].host_addr = packet->dma_addr;
}

#ifdef CONFIG_IPW2100_MONITOR

static void isr_rx_monitor(struct ipw2100_priv *priv, int i,
		   struct libipw_rx_stats *stats)
{
	struct net_device *dev = priv->net_dev;
	struct ipw2100_status *status = &priv->status_queue.drv[i];
	struct ipw2100_rx_packet *packet = &priv->rx_buffers[i];

	/* Magic struct that slots into the radiotap header -- no reason
	 * to build this manually element by element, we can write it much
	 * more efficiently than we can parse it. ORDER MATTERS HERE */
	struct ipw_rt_hdr {
		struct ieee80211_radiotap_header rt_hdr;
		s8 rt_dbmsignal; /* signal in dbM, kluged to signed */
	} *ipw_rt;

	IPW_DEBUG_RX("Handler...\n");

	if (unlikely(status->frame_size > skb_tailroom(packet->skb) -
				sizeof(struct ipw_rt_hdr))) {
		IPW_DEBUG_INFO("%s: frame_size (%u) > skb_tailroom (%u)!"
			       "  Dropping.\n",
			       dev->name,
			       status->frame_size,
			       skb_tailroom(packet->skb));
		dev->stats.rx_errors++;
		return;
	}

	if (unlikely(!netif_running(dev))) {
		dev->stats.rx_errors++;
		priv->wstats.discard.misc++;
		IPW_DEBUG_DROP("Dropping packet while interface is not up.\n");
		return;
	}

	if (unlikely(priv->config & CFG_CRC_CHECK &&
		     status->flags & IPW_STATUS_FLAG_CRC_ERROR)) {
		IPW_DEBUG_RX("CRC error in packet.  Dropping.\n");
		dev->stats.rx_errors++;
		return;
	}

	pci_unmap_single(priv->pci_dev, packet->dma_addr,
			 sizeof(struct ipw2100_rx), PCI_DMA_FROMDEVICE);
	memmove(packet->skb->data + sizeof(struct ipw_rt_hdr),
		packet->skb->data, status->frame_size);

	ipw_rt = (struct ipw_rt_hdr *) packet->skb->data;

	ipw_rt->rt_hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	ipw_rt->rt_hdr.it_pad = 0; /* always good to zero */
	ipw_rt->rt_hdr.it_len = cpu_to_le16(sizeof(struct ipw_rt_hdr)); /* total hdr+data */

	ipw_rt->rt_hdr.it_present = cpu_to_le32(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);

	ipw_rt->rt_dbmsignal = status->rssi + IPW2100_RSSI_TO_DBM;

	skb_put(packet->skb, status->frame_size + sizeof(struct ipw_rt_hdr));

	if (!libipw_rx(priv->ieee, packet->skb, stats)) {
		dev->stats.rx_errors++;

		/* libipw_rx failed, so it didn't free the SKB */
		dev_kfree_skb_any(packet->skb);
		packet->skb = NULL;
	}

	/* We need to allocate a new SKB and attach it to the RDB. */
	if (unlikely(ipw2100_alloc_skb(priv, packet))) {
		IPW_DEBUG_WARNING(
			"%s: Unable to allocate SKB onto RBD ring - disabling "
			"adapter.\n", dev->name);
		/* TODO: schedule adapter shutdown */
		IPW_DEBUG_INFO("TODO: Shutdown adapter...\n");
	}

	/* Update the RDB entry */
	priv->rx_queue.drv[i].host_addr = packet->dma_addr;
}

#endif

static int ipw2100_corruption_check(struct ipw2100_priv *priv, int i)
{
	struct ipw2100_status *status = &priv->status_queue.drv[i];
	struct ipw2100_rx *u = priv->rx_buffers[i].rxp;
	u16 frame_type = status->status_fields & STATUS_TYPE_MASK;

	switch (frame_type) {
	case COMMAND_STATUS_VAL:
		return (status->frame_size != sizeof(u->rx_data.command));
	case STATUS_CHANGE_VAL:
		return (status->frame_size != sizeof(u->rx_data.status));
	case HOST_NOTIFICATION_VAL:
		return (status->frame_size < sizeof(u->rx_data.notification));
	case P80211_DATA_VAL:
	case P8023_DATA_VAL:
#ifdef CONFIG_IPW2100_MONITOR
		return 0;
#else
		switch (WLAN_FC_GET_TYPE(le16_to_cpu(u->rx_data.header.frame_ctl))) {
		case IEEE80211_FTYPE_MGMT:
		case IEEE80211_FTYPE_CTL:
			return 0;
		case IEEE80211_FTYPE_DATA:
			return (status->frame_size >
				IPW_MAX_802_11_PAYLOAD_LENGTH);
		}
#endif
	}

	return 1;
}

/*
 * ipw2100 interrupts are disabled at this point, and the ISR
 * is the only code that calls this method.  So, we do not need
 * to play with any locks.
 *
 * RX Queue works as follows:
 *
 * Read index - firmware places packet in entry identified by the
 *              Read index and advances Read index.  In this manner,
 *              Read index will always point to the next packet to
 *              be filled--but not yet valid.
 *
 * Write index - driver fills this entry with an unused RBD entry.
 *               This entry has not filled by the firmware yet.
 *
 * In between the W and R indexes are the RBDs that have been received
 * but not yet processed.
 *
 * The process of handling packets will start at WRITE + 1 and advance
 * until it reaches the READ index.
 *
 * The WRITE index is cached in the variable 'priv->rx_queue.next'.
 *
 */
static void __ipw2100_rx_process(struct ipw2100_priv *priv)
{
	struct ipw2100_bd_queue *rxq = &priv->rx_queue;
	struct ipw2100_status_queue *sq = &priv->status_queue;
	struct ipw2100_rx_packet *packet;
	u16 frame_type;
	u32 r, w, i, s;
	struct ipw2100_rx *u;
	struct libipw_rx_stats stats = {
		.mac_time = jiffies,
	};

	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_RX_READ_INDEX, &r);
	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_RX_WRITE_INDEX, &w);

	if (r >= rxq->entries) {
		IPW_DEBUG_RX("exit - bad read index\n");
		return;
	}

	i = (rxq->next + 1) % rxq->entries;
	s = i;
	while (i != r) {
		/* IPW_DEBUG_RX("r = %d : w = %d : processing = %d\n",
		   r, rxq->next, i); */

		packet = &priv->rx_buffers[i];

		/* Sync the DMA for the RX buffer so CPU is sure to get
		 * the correct values */
		pci_dma_sync_single_for_cpu(priv->pci_dev, packet->dma_addr,
					    sizeof(struct ipw2100_rx),
					    PCI_DMA_FROMDEVICE);

		if (unlikely(ipw2100_corruption_check(priv, i))) {
			ipw2100_corruption_detected(priv, i);
			goto increment;
		}

		u = packet->rxp;
		frame_type = sq->drv[i].status_fields & STATUS_TYPE_MASK;
		stats.rssi = sq->drv[i].rssi + IPW2100_RSSI_TO_DBM;
		stats.len = sq->drv[i].frame_size;

		stats.mask = 0;
		if (stats.rssi != 0)
			stats.mask |= LIBIPW_STATMASK_RSSI;
		stats.freq = LIBIPW_24GHZ_BAND;

		IPW_DEBUG_RX("%s: '%s' frame type received (%d).\n",
			     priv->net_dev->name, frame_types[frame_type],
			     stats.len);

		switch (frame_type) {
		case COMMAND_STATUS_VAL:
			/* Reset Rx watchdog */
			isr_rx_complete_command(priv, &u->rx_data.command);
			break;

		case STATUS_CHANGE_VAL:
			isr_status_change(priv, u->rx_data.status);
			break;

		case P80211_DATA_VAL:
		case P8023_DATA_VAL:
#ifdef CONFIG_IPW2100_MONITOR
			if (priv->ieee->iw_mode == IW_MODE_MONITOR) {
				isr_rx_monitor(priv, i, &stats);
				break;
			}
#endif
			if (stats.len < sizeof(struct libipw_hdr_3addr))
				break;
			switch (WLAN_FC_GET_TYPE(le16_to_cpu(u->rx_data.header.frame_ctl))) {
			case IEEE80211_FTYPE_MGMT:
				libipw_rx_mgt(priv->ieee,
						 &u->rx_data.header, &stats);
				break;

			case IEEE80211_FTYPE_CTL:
				break;

			case IEEE80211_FTYPE_DATA:
				isr_rx(priv, i, &stats);
				break;

			}
			break;
		}

	      increment:
		/* clear status field associated with this RBD */
		rxq->drv[i].status.info.field = 0;

		i = (i + 1) % rxq->entries;
	}

	if (i != s) {
		/* backtrack one entry, wrapping to end if at 0 */
		rxq->next = (i ? i : rxq->entries) - 1;

		write_register(priv->net_dev,
			       IPW_MEM_HOST_SHARED_RX_WRITE_INDEX, rxq->next);
	}
}

/*
 * __ipw2100_tx_process
 *
 * This routine will determine whether the next packet on
 * the fw_pend_list has been processed by the firmware yet.
 *
 * If not, then it does nothing and returns.
 *
 * If so, then it removes the item from the fw_pend_list, frees
 * any associated storage, and places the item back on the
 * free list of its source (either msg_free_list or tx_free_list)
 *
 * TX Queue works as follows:
 *
 * Read index - points to the next TBD that the firmware will
 *              process.  The firmware will read the data, and once
 *              done processing, it will advance the Read index.
 *
 * Write index - driver fills this entry with an constructed TBD
 *               entry.  The Write index is not advanced until the
 *               packet has been configured.
 *
 * In between the W and R indexes are the TBDs that have NOT been
 * processed.  Lagging behind the R index are packets that have
 * been processed but have not been freed by the driver.
 *
 * In order to free old storage, an internal index will be maintained
 * that points to the next packet to be freed.  When all used
 * packets have been freed, the oldest index will be the same as the
 * firmware's read index.
 *
 * The OLDEST index is cached in the variable 'priv->tx_queue.oldest'
 *
 * Because the TBD structure can not contain arbitrary data, the
 * driver must keep an internal queue of cached allocations such that
 * it can put that data back into the tx_free_list and msg_free_list
 * for use by future command and data packets.
 *
 */
static int __ipw2100_tx_process(struct ipw2100_priv *priv)
{
	struct ipw2100_bd_queue *txq = &priv->tx_queue;
	struct ipw2100_bd *tbd;
	struct list_head *element;
	struct ipw2100_tx_packet *packet;
	int descriptors_used;
	int e, i;
	u32 r, w, frag_num = 0;

	if (list_empty(&priv->fw_pend_list))
		return 0;

	element = priv->fw_pend_list.next;

	packet = list_entry(element, struct ipw2100_tx_packet, list);
	tbd = &txq->drv[packet->index];

	/* Determine how many TBD entries must be finished... */
	switch (packet->type) {
	case COMMAND:
		/* COMMAND uses only one slot; don't advance */
		descriptors_used = 1;
		e = txq->oldest;
		break;

	case DATA:
		/* DATA uses two slots; advance and loop position. */
		descriptors_used = tbd->num_fragments;
		frag_num = tbd->num_fragments - 1;
		e = txq->oldest + frag_num;
		e %= txq->entries;
		break;

	default:
		printk(KERN_WARNING DRV_NAME ": %s: Bad fw_pend_list entry!\n",
		       priv->net_dev->name);
		return 0;
	}

	/* if the last TBD is not done by NIC yet, then packet is
	 * not ready to be released.
	 *
	 */
	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_TX_QUEUE_READ_INDEX,
		      &r);
	read_register(priv->net_dev, IPW_MEM_HOST_SHARED_TX_QUEUE_WRITE_INDEX,
		      &w);
	if (w != txq->next)
		printk(KERN_WARNING DRV_NAME ": %s: write index mismatch\n",
		       priv->net_dev->name);

	/*
	 * txq->next is the index of the last packet written txq->oldest is
	 * the index of the r is the index of the next packet to be read by
	 * firmware
	 */

	/*
	 * Quick graphic to help you visualize the following
	 * if / else statement
	 *
	 * ===>|                     s---->|===============
	 *                               e>|
	 * | a | b | c | d | e | f | g | h | i | j | k | l
	 *       r---->|
	 *               w
	 *
	 * w - updated by driver
	 * r - updated by firmware
	 * s - start of oldest BD entry (txq->oldest)
	 * e - end of oldest BD entry
	 *
	 */
	if (!((r <= w && (e < r || e >= w)) || (e < r && e >= w))) {
		IPW_DEBUG_TX("exit - no processed packets ready to release.\n");
		return 0;
	}

	list_del(element);
	DEC_STAT(&priv->fw_pend_stat);

#ifdef CONFIG_IPW2100_DEBUG
	{
		i = txq->oldest;
		IPW_DEBUG_TX("TX%d V=%p P=%04X T=%04X L=%d\n", i,
			     &txq->drv[i],
			     (u32) (txq->nic + i * sizeof(struct ipw2100_bd)),
			     txq->drv[i].host_addr, txq->drv[i].buf_length);

		if (packet->type == DATA) {
			i = (i + 1) % txq->entries;

			IPW_DEBUG_TX("TX%d V=%p P=%04X T=%04X L=%d\n", i,
				     &txq->drv[i],
				     (u32) (txq->nic + i *
					    sizeof(struct ipw2100_bd)),
				     (u32) txq->drv[i].host_addr,
				     txq->drv[i].buf_length);
		}
	}
#endif

	switch (packet->type) {
	case DATA:
		if (txq->drv[txq->oldest].status.info.fields.txType != 0)
			printk(KERN_WARNING DRV_NAME ": %s: Queue mismatch.  "
			       "Expecting DATA TBD but pulled "
			       "something else: ids %d=%d.\n",
			       priv->net_dev->name, txq->oldest, packet->index);

		/* DATA packet; we have to unmap and free the SKB */
		for (i = 0; i < frag_num; i++) {
			tbd = &txq->drv[(packet->index + 1 + i) % txq->entries];

			IPW_DEBUG_TX("TX%d P=%08x L=%d\n",
				     (packet->index + 1 + i) % txq->entries,
				     tbd->host_addr, tbd->buf_length);

			pci_unmap_single(priv->pci_dev,
					 tbd->host_addr,
					 tbd->buf_length, PCI_DMA_TODEVICE);
		}

		libipw_txb_free(packet->info.d_struct.txb);
		packet->info.d_struct.txb = NULL;

		list_add_tail(element, &priv->tx_free_list);
		INC_STAT(&priv->tx_free_stat);

		/* We have a free slot in the Tx queue, so wake up the
		 * transmit layer if it is stopped. */
		if (priv->status & STATUS_ASSOCIATED)
			netif_wake_queue(priv->net_dev);

		/* A packet was processed by the hardware, so update the
		 * watchdog */
		priv->net_dev->trans_start = jiffies;

		break;

	case COMMAND:
		if (txq->drv[txq->oldest].status.info.fields.txType != 1)
			printk(KERN_WARNING DRV_NAME ": %s: Queue mismatch.  "
			       "Expecting COMMAND TBD but pulled "
			       "something else: ids %d=%d.\n",
			       priv->net_dev->name, txq->oldest, packet->index);

#ifdef CONFIG_IPW2100_DEBUG
		if (packet->info.c_struct.cmd->host_command_reg <
		    ARRAY_SIZE(command_types))
			IPW_DEBUG_TX("Command '%s (%d)' processed: %d.\n",
				     command_types[packet->info.c_struct.cmd->
						   host_command_reg],
				     packet->info.c_struct.cmd->
				     host_command_reg,
				     packet->info.c_struct.cmd->cmd_status_reg);
#endif

		list_add_tail(element, &priv->msg_free_list);
		INC_STAT(&priv->msg_free_stat);
		break;
	}

	/* advance oldest used TBD pointer to start of next entry */
	txq->oldest = (e + 1) % txq->entries;
	/* increase available TBDs number */
	txq->available += descriptors_used;
	SET_STAT(&priv->txq_stat, txq->available);

	IPW_DEBUG_TX("packet latency (send to process)  %ld jiffies\n",
		     jiffies - packet->jiffy_start);

	return (!list_empty(&priv->fw_pend_list));
}

static inline void __ipw2100_tx_complete(struct ipw2100_priv *priv)
{
	int i = 0;

	while (__ipw2100_tx_process(priv) && i < 200)
		i++;

	if (i == 200) {
		printk(KERN_WARNING DRV_NAME ": "
		       "%s: Driver is running slow (%d iters).\n",
		       priv->net_dev->name, i);
	}
}

static void ipw2100_tx_send_commands(struct ipw2100_priv *priv)
{
	struct list_head *element;
	struct ipw2100_tx_packet *packet;
	struct ipw2100_bd_queue *txq = &priv->tx_queue;
	struct ipw2100_bd *tbd;
	int next = txq->next;

	while (!list_empty(&priv->msg_pend_list)) {
		/* if there isn't enough space in TBD queue, then
		 * don't stuff a new one in.
		 * NOTE: 3 are needed as a command will take one,
		 *       and there is a minimum of 2 that must be
		 *       maintained between the r and w indexes
		 */
		if (txq->available <= 3) {
			IPW_DEBUG_TX("no room in tx_queue\n");
			break;
		}

		element = priv->msg_pend_list.next;
		list_del(element);
		DEC_STAT(&priv->msg_pend_stat);

		packet = list_entry(element, struct ipw2100_tx_packet, list);

		IPW_DEBUG_TX("using TBD at virt=%p, phys=%04X\n",
			     &txq->drv[txq->next],
			     (u32) (txq->nic + txq->next *
				      sizeof(struct ipw2100_bd)));

		packet->index = txq->next;

		tbd = &txq->drv[txq->next];

		/* initialize TBD */
		tbd->host_addr = packet->info.c_struct.cmd_phys;
		tbd->buf_length = sizeof(struct ipw2100_cmd_header);
		/* not marking number of fragments causes problems
		 * with f/w debug version */
		tbd->num_fragments = 1;
		tbd->status.info.field =
		    IPW_BD_STATUS_TX_FRAME_COMMAND |
		    IPW_BD_STATUS_TX_INTERRUPT_ENABLE;

		/* update TBD queue counters */
		txq->next++;
		txq->next %= txq->entries;
		txq->available--;
		DEC_STAT(&priv->txq_stat);

		list_add_tail(element, &priv->fw_pend_list);
		INC_STAT(&priv->fw_pend_stat);
	}

	if (txq->next != next) {
		/* kick off the DMA by notifying firmware the
		 * write index has moved; make sure TBD stores are sync'd */
		wmb();
		write_register(priv->net_dev,
			       IPW_MEM_HOST_SHARED_TX_QUEUE_WRITE_INDEX,
			       txq->next);
	}
}

/*
 * ipw2100_tx_send_data
 *
 */
static void ipw2100_tx_send_data(struct ipw2100_priv *priv)
{
	struct list_head *element;
	struct ipw2100_tx_packet *packet;
	struct ipw2100_bd_queue *txq = &priv->tx_queue;
	struct ipw2100_bd *tbd;
	int next = txq->next;
	int i = 0;
	struct ipw2100_data_header *ipw_hdr;
	struct libipw_hdr_3addr *hdr;

	while (!list_empty(&priv->tx_pend_list)) {
		/* if there isn't enough space in TBD queue, then
		 * don't stuff a new one in.
		 * NOTE: 4 are needed as a data will take two,
		 *       and there is a minimum of 2 that must be
		 *       maintained between the r and w indexes
		 */
		element = priv->tx_pend_list.next;
		packet = list_entry(element, struct ipw2100_tx_packet, list);

		if (unlikely(1 + packet->info.d_struct.txb->nr_frags >
			     IPW_MAX_BDS)) {
			/* TODO: Support merging buffers if more than
			 * IPW_MAX_BDS are used */
			IPW_DEBUG_INFO("%s: Maximum BD threshold exceeded.  "
				       "Increase fragmentation level.\n",
				       priv->net_dev->name);
		}

		if (txq->available <= 3 + packet->info.d_struct.txb->nr_frags) {
			IPW_DEBUG_TX("no room in tx_queue\n");
			break;
		}

		list_del(element);
		DEC_STAT(&priv->tx_pend_stat);

		tbd = &txq->drv[txq->next];

		packet->index = txq->next;

		ipw_hdr = packet->info.d_struct.data;
		hdr = (struct libipw_hdr_3addr *)packet->info.d_struct.txb->
		    fragments[0]->data;

		if (priv->ieee->iw_mode == IW_MODE_INFRA) {
			/* To DS: Addr1 = BSSID, Addr2 = SA,
			   Addr3 = DA */
			memcpy(ipw_hdr->src_addr, hdr->addr2, ETH_ALEN);
			memcpy(ipw_hdr->dst_addr, hdr->addr3, ETH_ALEN);
		} else if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
			/* not From/To DS: Addr1 = DA, Addr2 = SA,
			   Addr3 = BSSID */
			memcpy(ipw_hdr->src_addr, hdr->addr2, ETH_ALEN);
			memcpy(ipw_hdr->dst_addr, hdr->addr1, ETH_ALEN);
		}

		ipw_hdr->host_command_reg = SEND;
		ipw_hdr->host_command_reg1 = 0;

		/* For now we only support host based encryption */
		ipw_hdr->needs_encryption = 0;
		ipw_hdr->encrypted = packet->info.d_struct.txb->encrypted;
		if (packet->info.d_struct.txb->nr_frags > 1)
			ipw_hdr->fragment_size =
			    packet->info.d_struct.txb->frag_size -
			    LIBIPW_3ADDR_LEN;
		else
			ipw_hdr->fragment_size = 0;

		tbd->host_addr = packet->info.d_struct.data_phys;
		tbd->buf_length = sizeof(struct ipw2100_data_header);
		tbd->num_fragments = 1 + packet->info.d_struct.txb->nr_frags;
		tbd->status.info.field =
		    IPW_BD_STATUS_TX_FRAME_802_3 |
		    IPW_BD_STATUS_TX_FRAME_NOT_LAST_FRAGMENT;
		txq->next++;
		txq->next %= txq->entries;

		IPW_DEBUG_TX("data header tbd TX%d P=%08x L=%d\n",
			     packet->index, tbd->host_addr, tbd->buf_length);
#ifdef CONFIG_IPW2100_DEBUG
		if (packet->info.d_struct.txb->nr_frags > 1)
			IPW_DEBUG_FRAG("fragment Tx: %d frames\n",
				       packet->info.d_struct.txb->nr_frags);
#endif

		for (i = 0; i < packet->info.d_struct.txb->nr_frags; i++) {
			tbd = &txq->drv[txq->next];
			if (i == packet->info.d_struct.txb->nr_frags - 1)
				tbd->status.info.field =
				    IPW_BD_STATUS_TX_FRAME_802_3 |
				    IPW_BD_STATUS_TX_INTERRUPT_ENABLE;
			else
				tbd->status.info.field =
				    IPW_BD_STATUS_TX_FRAME_802_3 |
				    IPW_BD_STATUS_TX_FRAME_NOT_LAST_FRAGMENT;

			tbd->buf_length = packet->info.d_struct.txb->
			    fragments[i]->len - LIBIPW_3ADDR_LEN;

			tbd->host_addr = pci_map_single(priv->pci_dev,
							packet->info.d_struct.
							txb->fragments[i]->
							data +
							LIBIPW_3ADDR_LEN,
							tbd->buf_length,
							PCI_DMA_TODEVICE);

			IPW_DEBUG_TX("data frag tbd TX%d P=%08x L=%d\n",
				     txq->next, tbd->host_addr,
				     tbd->buf_length);

			pci_dma_sync_single_for_device(priv->pci_dev,
						       tbd->host_addr,
						       tbd->buf_length,
						       PCI_DMA_TODEVICE);

			txq->next++;
			txq->next %= txq->entries;
		}

		txq->available -= 1 + packet->info.d_struct.txb->nr_frags;
		SET_STAT(&priv->txq_stat, txq->available);

		list_add_tail(element, &priv->fw_pend_list);
		INC_STAT(&priv->fw_pend_stat);
	}

	if (txq->next != next) {
		/* kick off the DMA by notifying firmware the
		 * write index has moved; make sure TBD stores are sync'd */
		write_register(priv->net_dev,
			       IPW_MEM_HOST_SHARED_TX_QUEUE_WRITE_INDEX,
			       txq->next);
	}
}

static void ipw2100_irq_tasklet(struct ipw2100_priv *priv)
{
	struct net_device *dev = priv->net_dev;
	unsigned long flags;
	u32 inta, tmp;

	spin_lock_irqsave(&priv->low_lock, flags);
	ipw2100_disable_interrupts(priv);

	read_register(dev, IPW_REG_INTA, &inta);

	IPW_DEBUG_ISR("enter - INTA: 0x%08lX\n",
		      (unsigned long)inta & IPW_INTERRUPT_MASK);

	priv->in_isr++;
	priv->interrupts++;

	/* We do not loop and keep polling for more interrupts as this
	 * is frowned upon and doesn't play nicely with other potentially
	 * chained IRQs */
	IPW_DEBUG_ISR("INTA: 0x%08lX\n",
		      (unsigned long)inta & IPW_INTERRUPT_MASK);

	if (inta & IPW2100_INTA_FATAL_ERROR) {
		printk(KERN_WARNING DRV_NAME
		       ": Fatal interrupt. Scheduling firmware restart.\n");
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA, IPW2100_INTA_FATAL_ERROR);

		read_nic_dword(dev, IPW_NIC_FATAL_ERROR, &priv->fatal_error);
		IPW_DEBUG_INFO("%s: Fatal error value: 0x%08X\n",
			       priv->net_dev->name, priv->fatal_error);

		read_nic_dword(dev, IPW_ERROR_ADDR(priv->fatal_error), &tmp);
		IPW_DEBUG_INFO("%s: Fatal error address value: 0x%08X\n",
			       priv->net_dev->name, tmp);

		/* Wake up any sleeping jobs */
		schedule_reset(priv);
	}

	if (inta & IPW2100_INTA_PARITY_ERROR) {
		printk(KERN_ERR DRV_NAME
		       ": ***** PARITY ERROR INTERRUPT !!!!\n");
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA, IPW2100_INTA_PARITY_ERROR);
	}

	if (inta & IPW2100_INTA_RX_TRANSFER) {
		IPW_DEBUG_ISR("RX interrupt\n");

		priv->rx_interrupts++;

		write_register(dev, IPW_REG_INTA, IPW2100_INTA_RX_TRANSFER);

		__ipw2100_rx_process(priv);
		__ipw2100_tx_complete(priv);
	}

	if (inta & IPW2100_INTA_TX_TRANSFER) {
		IPW_DEBUG_ISR("TX interrupt\n");

		priv->tx_interrupts++;

		write_register(dev, IPW_REG_INTA, IPW2100_INTA_TX_TRANSFER);

		__ipw2100_tx_complete(priv);
		ipw2100_tx_send_commands(priv);
		ipw2100_tx_send_data(priv);
	}

	if (inta & IPW2100_INTA_TX_COMPLETE) {
		IPW_DEBUG_ISR("TX complete\n");
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA, IPW2100_INTA_TX_COMPLETE);

		__ipw2100_tx_complete(priv);
	}

	if (inta & IPW2100_INTA_EVENT_INTERRUPT) {
		/* ipw2100_handle_event(dev); */
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA, IPW2100_INTA_EVENT_INTERRUPT);
	}

	if (inta & IPW2100_INTA_FW_INIT_DONE) {
		IPW_DEBUG_ISR("FW init done interrupt\n");
		priv->inta_other++;

		read_register(dev, IPW_REG_INTA, &tmp);
		if (tmp & (IPW2100_INTA_FATAL_ERROR |
			   IPW2100_INTA_PARITY_ERROR)) {
			write_register(dev, IPW_REG_INTA,
				       IPW2100_INTA_FATAL_ERROR |
				       IPW2100_INTA_PARITY_ERROR);
		}

		write_register(dev, IPW_REG_INTA, IPW2100_INTA_FW_INIT_DONE);
	}

	if (inta & IPW2100_INTA_STATUS_CHANGE) {
		IPW_DEBUG_ISR("Status change interrupt\n");
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA, IPW2100_INTA_STATUS_CHANGE);
	}

	if (inta & IPW2100_INTA_SLAVE_MODE_HOST_COMMAND_DONE) {
		IPW_DEBUG_ISR("slave host mode interrupt\n");
		priv->inta_other++;
		write_register(dev, IPW_REG_INTA,
			       IPW2100_INTA_SLAVE_MODE_HOST_COMMAND_DONE);
	}

	priv->in_isr--;
	ipw2100_enable_interrupts(priv);

	spin_unlock_irqrestore(&priv->low_lock, flags);

	IPW_DEBUG_ISR("exit\n");
}

static irqreturn_t ipw2100_interrupt(int irq, void *data)
{
	struct ipw2100_priv *priv = data;
	u32 inta, inta_mask;

	if (!data)
		return IRQ_NONE;

	spin_lock(&priv->low_lock);

	/* We check to see if we should be ignoring interrupts before
	 * we touch the hardware.  During ucode load if we try and handle
	 * an interrupt we can cause keyboard problems as well as cause
	 * the ucode to fail to initialize */
	if (!(priv->status & STATUS_INT_ENABLED)) {
		/* Shared IRQ */
		goto none;
	}

	read_register(priv->net_dev, IPW_REG_INTA_MASK, &inta_mask);
	read_register(priv->net_dev, IPW_REG_INTA, &inta);

	if (inta == 0xFFFFFFFF) {
		/* Hardware disappeared */
		printk(KERN_WARNING DRV_NAME ": IRQ INTA == 0xFFFFFFFF\n");
		goto none;
	}

	inta &= IPW_INTERRUPT_MASK;

	if (!(inta & inta_mask)) {
		/* Shared interrupt */
		goto none;
	}

	/* We disable the hardware interrupt here just to prevent unneeded
	 * calls to be made.  We disable this again within the actual
	 * work tasklet, so if another part of the code re-enables the
	 * interrupt, that is fine */
	ipw2100_disable_interrupts(priv);

	tasklet_schedule(&priv->irq_tasklet);
	spin_unlock(&priv->low_lock);

	return IRQ_HANDLED;
      none:
	spin_unlock(&priv->low_lock);
	return IRQ_NONE;
}

static netdev_tx_t ipw2100_tx(struct libipw_txb *txb,
			      struct net_device *dev, int pri)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct list_head *element;
	struct ipw2100_tx_packet *packet;
	unsigned long flags;

	spin_lock_irqsave(&priv->low_lock, flags);

	if (!(priv->status & STATUS_ASSOCIATED)) {
		IPW_DEBUG_INFO("Can not transmit when not connected.\n");
		priv->net_dev->stats.tx_carrier_errors++;
		netif_stop_queue(dev);
		goto fail_unlock;
	}

	if (list_empty(&priv->tx_free_list))
		goto fail_unlock;

	element = priv->tx_free_list.next;
	packet = list_entry(element, struct ipw2100_tx_packet, list);

	packet->info.d_struct.txb = txb;

	IPW_DEBUG_TX("Sending fragment (%d bytes):\n", txb->fragments[0]->len);
	printk_buf(IPW_DL_TX, txb->fragments[0]->data, txb->fragments[0]->len);

	packet->jiffy_start = jiffies;

	list_del(element);
	DEC_STAT(&priv->tx_free_stat);

	list_add_tail(element, &priv->tx_pend_list);
	INC_STAT(&priv->tx_pend_stat);

	ipw2100_tx_send_data(priv);

	spin_unlock_irqrestore(&priv->low_lock, flags);
	return NETDEV_TX_OK;

fail_unlock:
	netif_stop_queue(dev);
	spin_unlock_irqrestore(&priv->low_lock, flags);
	return NETDEV_TX_BUSY;
}

static int ipw2100_msg_allocate(struct ipw2100_priv *priv)
{
	int i, j, err = -EINVAL;
	void *v;
	dma_addr_t p;

	priv->msg_buffers =
	    kmalloc(IPW_COMMAND_POOL_SIZE * sizeof(struct ipw2100_tx_packet),
		    GFP_KERNEL);
	if (!priv->msg_buffers)
		return -ENOMEM;

	for (i = 0; i < IPW_COMMAND_POOL_SIZE; i++) {
		v = pci_alloc_consistent(priv->pci_dev,
					 sizeof(struct ipw2100_cmd_header), &p);
		if (!v) {
			printk(KERN_ERR DRV_NAME ": "
			       "%s: PCI alloc failed for msg "
			       "buffers.\n", priv->net_dev->name);
			err = -ENOMEM;
			break;
		}

		memset(v, 0, sizeof(struct ipw2100_cmd_header));

		priv->msg_buffers[i].type = COMMAND;
		priv->msg_buffers[i].info.c_struct.cmd =
		    (struct ipw2100_cmd_header *)v;
		priv->msg_buffers[i].info.c_struct.cmd_phys = p;
	}

	if (i == IPW_COMMAND_POOL_SIZE)
		return 0;

	for (j = 0; j < i; j++) {
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct ipw2100_cmd_header),
				    priv->msg_buffers[j].info.c_struct.cmd,
				    priv->msg_buffers[j].info.c_struct.
				    cmd_phys);
	}

	kfree(priv->msg_buffers);
	priv->msg_buffers = NULL;

	return err;
}

static int ipw2100_msg_initialize(struct ipw2100_priv *priv)
{
	int i;

	INIT_LIST_HEAD(&priv->msg_free_list);
	INIT_LIST_HEAD(&priv->msg_pend_list);

	for (i = 0; i < IPW_COMMAND_POOL_SIZE; i++)
		list_add_tail(&priv->msg_buffers[i].list, &priv->msg_free_list);
	SET_STAT(&priv->msg_free_stat, i);

	return 0;
}

static void ipw2100_msg_free(struct ipw2100_priv *priv)
{
	int i;

	if (!priv->msg_buffers)
		return;

	for (i = 0; i < IPW_COMMAND_POOL_SIZE; i++) {
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct ipw2100_cmd_header),
				    priv->msg_buffers[i].info.c_struct.cmd,
				    priv->msg_buffers[i].info.c_struct.
				    cmd_phys);
	}

	kfree(priv->msg_buffers);
	priv->msg_buffers = NULL;
}

static ssize_t show_pci(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct pci_dev *pci_dev = container_of(d, struct pci_dev, dev);
	char *out = buf;
	int i, j;
	u32 val;

	for (i = 0; i < 16; i++) {
		out += sprintf(out, "[%08X] ", i * 16);
		for (j = 0; j < 16; j += 4) {
			pci_read_config_dword(pci_dev, i * 16 + j, &val);
			out += sprintf(out, "%08X ", val);
		}
		out += sprintf(out, "\n");
	}

	return out - buf;
}

static DEVICE_ATTR(pci, S_IRUGO, show_pci, NULL);

static ssize_t show_cfg(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct ipw2100_priv *p = dev_get_drvdata(d);
	return sprintf(buf, "0x%08x\n", (int)p->config);
}

static DEVICE_ATTR(cfg, S_IRUGO, show_cfg, NULL);

static ssize_t show_status(struct device *d, struct device_attribute *attr,
			   char *buf)
{
	struct ipw2100_priv *p = dev_get_drvdata(d);
	return sprintf(buf, "0x%08x\n", (int)p->status);
}

static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);

static ssize_t show_capability(struct device *d, struct device_attribute *attr,
			       char *buf)
{
	struct ipw2100_priv *p = dev_get_drvdata(d);
	return sprintf(buf, "0x%08x\n", (int)p->capability);
}

static DEVICE_ATTR(capability, S_IRUGO, show_capability, NULL);

#define IPW2100_REG(x) { IPW_ ##x, #x }
static const struct {
	u32 addr;
	const char *name;
} hw_data[] = {
IPW2100_REG(REG_GP_CNTRL),
	    IPW2100_REG(REG_GPIO),
	    IPW2100_REG(REG_INTA),
	    IPW2100_REG(REG_INTA_MASK), IPW2100_REG(REG_RESET_REG),};
#define IPW2100_NIC(x, s) { x, #x, s }
static const struct {
	u32 addr;
	const char *name;
	size_t size;
} nic_data[] = {
IPW2100_NIC(IPW2100_CONTROL_REG, 2),
	    IPW2100_NIC(0x210014, 1), IPW2100_NIC(0x210000, 1),};
#define IPW2100_ORD(x, d) { IPW_ORD_ ##x, #x, d }
static const struct {
	u8 index;
	const char *name;
	const char *desc;
} ord_data[] = {
IPW2100_ORD(STAT_TX_HOST_REQUESTS, "requested Host Tx's (MSDU)"),
	    IPW2100_ORD(STAT_TX_HOST_COMPLETE,
				"successful Host Tx's (MSDU)"),
	    IPW2100_ORD(STAT_TX_DIR_DATA,
				"successful Directed Tx's (MSDU)"),
	    IPW2100_ORD(STAT_TX_DIR_DATA1,
				"successful Directed Tx's (MSDU) @ 1MB"),
	    IPW2100_ORD(STAT_TX_DIR_DATA2,
				"successful Directed Tx's (MSDU) @ 2MB"),
	    IPW2100_ORD(STAT_TX_DIR_DATA5_5,
				"successful Directed Tx's (MSDU) @ 5_5MB"),
	    IPW2100_ORD(STAT_TX_DIR_DATA11,
				"successful Directed Tx's (MSDU) @ 11MB"),
	    IPW2100_ORD(STAT_TX_NODIR_DATA1,
				"successful Non_Directed Tx's (MSDU) @ 1MB"),
	    IPW2100_ORD(STAT_TX_NODIR_DATA2,
				"successful Non_Directed Tx's (MSDU) @ 2MB"),
	    IPW2100_ORD(STAT_TX_NODIR_DATA5_5,
				"successful Non_Directed Tx's (MSDU) @ 5.5MB"),
	    IPW2100_ORD(STAT_TX_NODIR_DATA11,
				"successful Non_Directed Tx's (MSDU) @ 11MB"),
	    IPW2100_ORD(STAT_NULL_DATA, "successful NULL data Tx's"),
	    IPW2100_ORD(STAT_TX_RTS, "successful Tx RTS"),
	    IPW2100_ORD(STAT_TX_CTS, "successful Tx CTS"),
	    IPW2100_ORD(STAT_TX_ACK, "successful Tx ACK"),
	    IPW2100_ORD(STAT_TX_ASSN, "successful Association Tx's"),
	    IPW2100_ORD(STAT_TX_ASSN_RESP,
				"successful Association response Tx's"),
	    IPW2100_ORD(STAT_TX_REASSN,
				"successful Reassociation Tx's"),
	    IPW2100_ORD(STAT_TX_REASSN_RESP,
				"successful Reassociation response Tx's"),
	    IPW2100_ORD(STAT_TX_PROBE,
				"probes successfully transmitted"),
	    IPW2100_ORD(STAT_TX_PROBE_RESP,
				"probe responses successfully transmitted"),
	    IPW2100_ORD(STAT_TX_BEACON, "tx beacon"),
	    IPW2100_ORD(STAT_TX_ATIM, "Tx ATIM"),
	    IPW2100_ORD(STAT_TX_DISASSN,
				"successful Disassociation TX"),
	    IPW2100_ORD(STAT_TX_AUTH, "successful Authentication Tx"),
	    IPW2100_ORD(STAT_TX_DEAUTH,
				"successful Deauthentication TX"),
	    IPW2100_ORD(STAT_TX_TOTAL_BYTES,
				"Total successful Tx data bytes"),
	    IPW2100_ORD(STAT_TX_RETRIES, "Tx retries"),
	    IPW2100_ORD(STAT_TX_RETRY1, "Tx retries at 1MBPS"),
	    IPW2100_ORD(STAT_TX_RETRY2, "Tx retries at 2MBPS"),
	    IPW2100_ORD(STAT_TX_RETRY5_5, "Tx retries at 5.5MBPS"),
	    IPW2100_ORD(STAT_TX_RETRY11, "Tx retries at 11MBPS"),
	    IPW2100_ORD(STAT_TX_FAILURES, "Tx Failures"),
	    IPW2100_ORD(STAT_TX_MAX_TRIES_IN_HOP,
				"times max tries in a hop failed"),
	    IPW2100_ORD(STAT_TX_DISASSN_FAIL,
				"times disassociation failed"),
	    IPW2100_ORD(STAT_TX_ERR_CTS, "missed/bad CTS frames"),
	    IPW2100_ORD(STAT_TX_ERR_ACK, "tx err due to acks"),
	    IPW2100_ORD(STAT_RX_HOST, "packets passed to host"),
	    IPW2100_ORD(STAT_RX_DIR_DATA, "directed packets"),
	    IPW2100_ORD(STAT_RX_DIR_DATA1, "directed packets at 1MB"),
	    IPW2100_ORD(STAT_RX_DIR_DATA2, "directed packets at 2MB"),
	    IPW2100_ORD(STAT_RX_DIR_DATA5_5,
				"directed packets at 5.5MB"),
	    IPW2100_ORD(STAT_RX_DIR_DATA11, "directed packets at 11MB"),
	    IPW2100_ORD(STAT_RX_NODIR_DATA, "nondirected packets"),
	    IPW2100_ORD(STAT_RX_NODIR_DATA1,
				"nondirected packets at 1MB"),
	    IPW2100_ORD(STAT_RX_NODIR_DATA2,
				"nondirected packets at 2MB"),
	    IPW2100_ORD(STAT_RX_NODIR_DATA5_5,
				"nondirected packets at 5.5MB"),
	    IPW2100_ORD(STAT_RX_NODIR_DATA11,
				"nondirected packets at 11MB"),
	    IPW2100_ORD(STAT_RX_NULL_DATA, "null data rx's"),
	    IPW2100_ORD(STAT_RX_RTS, "Rx RTS"), IPW2100_ORD(STAT_RX_CTS,
								    "Rx CTS"),
	    IPW2100_ORD(STAT_RX_ACK, "Rx ACK"),
	    IPW2100_ORD(STAT_RX_CFEND, "Rx CF End"),
	    IPW2100_ORD(STAT_RX_CFEND_ACK, "Rx CF End + CF Ack"),
	    IPW2100_ORD(STAT_RX_ASSN, "Association Rx's"),
	    IPW2100_ORD(STAT_RX_ASSN_RESP, "Association response Rx's"),
	    IPW2100_ORD(STAT_RX_REASSN, "Reassociation Rx's"),
	    IPW2100_ORD(STAT_RX_REASSN_RESP,
				"Reassociation response Rx's"),
	    IPW2100_ORD(STAT_RX_PROBE, "probe Rx's"),
	    IPW2100_ORD(STAT_RX_PROBE_RESP, "probe response Rx's"),
	    IPW2100_ORD(STAT_RX_BEACON, "Rx beacon"),
	    IPW2100_ORD(STAT_RX_ATIM, "Rx ATIM"),
	    IPW2100_ORD(STAT_RX_DISASSN, "disassociation Rx"),
	    IPW2100_ORD(STAT_RX_AUTH, "authentication Rx"),
	    IPW2100_ORD(STAT_RX_DEAUTH, "deauthentication Rx"),
	    IPW2100_ORD(STAT_RX_TOTAL_BYTES,
				"Total rx data bytes received"),
	    IPW2100_ORD(STAT_RX_ERR_CRC, "packets with Rx CRC error"),
	    IPW2100_ORD(STAT_RX_ERR_CRC1, "Rx CRC errors at 1MB"),
	    IPW2100_ORD(STAT_RX_ERR_CRC2, "Rx CRC errors at 2MB"),
	    IPW2100_ORD(STAT_RX_ERR_CRC5_5, "Rx CRC errors at 5.5MB"),
	    IPW2100_ORD(STAT_RX_ERR_CRC11, "Rx CRC errors at 11MB"),
	    IPW2100_ORD(STAT_RX_DUPLICATE1,
				"duplicate rx packets at 1MB"),
	    IPW2100_ORD(STAT_RX_DUPLICATE2,
				"duplicate rx packets at 2MB"),
	    IPW2100_ORD(STAT_RX_DUPLICATE5_5,
				"duplicate rx packets at 5.5MB"),
	    IPW2100_ORD(STAT_RX_DUPLICATE11,
				"duplicate rx packets at 11MB"),
	    IPW2100_ORD(STAT_RX_DUPLICATE, "duplicate rx packets"),
	    IPW2100_ORD(PERS_DB_LOCK, "locking fw permanent  db"),
	    IPW2100_ORD(PERS_DB_SIZE, "size of fw permanent  db"),
	    IPW2100_ORD(PERS_DB_ADDR, "address of fw permanent  db"),
	    IPW2100_ORD(STAT_RX_INVALID_PROTOCOL,
				"rx frames with invalid protocol"),
	    IPW2100_ORD(SYS_BOOT_TIME, "Boot time"),
	    IPW2100_ORD(STAT_RX_NO_BUFFER,
				"rx frames rejected due to no buffer"),
	    IPW2100_ORD(STAT_RX_MISSING_FRAG,
				"rx frames dropped due to missing fragment"),
	    IPW2100_ORD(STAT_RX_ORPHAN_FRAG,
				"rx frames dropped due to non-sequential fragment"),
	    IPW2100_ORD(STAT_RX_ORPHAN_FRAME,
				"rx frames dropped due to unmatched 1st frame"),
	    IPW2100_ORD(STAT_RX_FRAG_AGEOUT,
				"rx frames dropped due to uncompleted frame"),
	    IPW2100_ORD(STAT_RX_ICV_ERRORS,
				"ICV errors during decryption"),
	    IPW2100_ORD(STAT_PSP_SUSPENSION, "times adapter suspended"),
	    IPW2100_ORD(STAT_PSP_BCN_TIMEOUT, "beacon timeout"),
	    IPW2100_ORD(STAT_PSP_POLL_TIMEOUT,
				"poll response timeouts"),
	    IPW2100_ORD(STAT_PSP_NONDIR_TIMEOUT,
				"timeouts waiting for last {broad,multi}cast pkt"),
	    IPW2100_ORD(STAT_PSP_RX_DTIMS, "PSP DTIMs received"),
	    IPW2100_ORD(STAT_PSP_RX_TIMS, "PSP TIMs received"),
	    IPW2100_ORD(STAT_PSP_STATION_ID, "PSP Station ID"),
	    IPW2100_ORD(LAST_ASSN_TIME, "RTC time of last association"),
	    IPW2100_ORD(STAT_PERCENT_MISSED_BCNS,
				"current calculation of % missed beacons"),
	    IPW2100_ORD(STAT_PERCENT_RETRIES,
				"current calculation of % missed tx retries"),
	    IPW2100_ORD(ASSOCIATED_AP_PTR,
				"0 if not associated, else pointer to AP table entry"),
	    IPW2100_ORD(AVAILABLE_AP_CNT,
				"AP's decsribed in the AP table"),
	    IPW2100_ORD(AP_LIST_PTR, "Ptr to list of available APs"),
	    IPW2100_ORD(STAT_AP_ASSNS, "associations"),
	    IPW2100_ORD(STAT_ASSN_FAIL, "association failures"),
	    IPW2100_ORD(STAT_ASSN_RESP_FAIL,
				"failures due to response fail"),
	    IPW2100_ORD(STAT_FULL_SCANS, "full scans"),
	    IPW2100_ORD(CARD_DISABLED, "Card Disabled"),
	    IPW2100_ORD(STAT_ROAM_INHIBIT,
				"times roaming was inhibited due to activity"),
	    IPW2100_ORD(RSSI_AT_ASSN,
				"RSSI of associated AP at time of association"),
	    IPW2100_ORD(STAT_ASSN_CAUSE1,
				"reassociation: no probe response or TX on hop"),
	    IPW2100_ORD(STAT_ASSN_CAUSE2,
				"reassociation: poor tx/rx quality"),
	    IPW2100_ORD(STAT_ASSN_CAUSE3,
				"reassociation: tx/rx quality (excessive AP load"),
	    IPW2100_ORD(STAT_ASSN_CAUSE4,
				"reassociation: AP RSSI level"),
	    IPW2100_ORD(STAT_ASSN_CAUSE5,
				"reassociations due to load leveling"),
	    IPW2100_ORD(STAT_AUTH_FAIL, "times authentication failed"),
	    IPW2100_ORD(STAT_AUTH_RESP_FAIL,
				"times authentication response failed"),
	    IPW2100_ORD(STATION_TABLE_CNT,
				"entries in association table"),
	    IPW2100_ORD(RSSI_AVG_CURR, "Current avg RSSI"),
	    IPW2100_ORD(POWER_MGMT_MODE, "Power mode - 0=CAM, 1=PSP"),
	    IPW2100_ORD(COUNTRY_CODE,
				"IEEE country code as recv'd from beacon"),
	    IPW2100_ORD(COUNTRY_CHANNELS,
				"channels supported by country"),
	    IPW2100_ORD(RESET_CNT, "adapter resets (warm)"),
	    IPW2100_ORD(BEACON_INTERVAL, "Beacon interval"),
	    IPW2100_ORD(ANTENNA_DIVERSITY,
				"TRUE if antenna diversity is disabled"),
	    IPW2100_ORD(DTIM_PERIOD, "beacon intervals between DTIMs"),
	    IPW2100_ORD(OUR_FREQ,
				"current radio freq lower digits - channel ID"),
	    IPW2100_ORD(RTC_TIME, "current RTC time"),
	    IPW2100_ORD(PORT_TYPE, "operating mode"),
	    IPW2100_ORD(CURRENT_TX_RATE, "current tx rate"),
	    IPW2100_ORD(SUPPORTED_RATES, "supported tx rates"),
	    IPW2100_ORD(ATIM_WINDOW, "current ATIM Window"),
	    IPW2100_ORD(BASIC_RATES, "basic tx rates"),
	    IPW2100_ORD(NIC_HIGHEST_RATE, "NIC highest tx rate"),
	    IPW2100_ORD(AP_HIGHEST_RATE, "AP highest tx rate"),
	    IPW2100_ORD(CAPABILITIES,
				"Management frame capability field"),
	    IPW2100_ORD(AUTH_TYPE, "Type of authentication"),
	    IPW2100_ORD(RADIO_TYPE, "Adapter card platform type"),
	    IPW2100_ORD(RTS_THRESHOLD,
				"Min packet length for RTS handshaking"),
	    IPW2100_ORD(INT_MODE, "International mode"),
	    IPW2100_ORD(FRAGMENTATION_THRESHOLD,
				"protocol frag threshold"),
	    IPW2100_ORD(EEPROM_SRAM_DB_BLOCK_START_ADDRESS,
				"EEPROM offset in SRAM"),
	    IPW2100_ORD(EEPROM_SRAM_DB_BLOCK_SIZE,
				"EEPROM size in SRAM"),
	    IPW2100_ORD(EEPROM_SKU_CAPABILITY, "EEPROM SKU Capability"),
	    IPW2100_ORD(EEPROM_IBSS_11B_CHANNELS,
				"EEPROM IBSS 11b channel set"),
	    IPW2100_ORD(MAC_VERSION, "MAC Version"),
	    IPW2100_ORD(MAC_REVISION, "MAC Revision"),
	    IPW2100_ORD(RADIO_VERSION, "Radio Version"),
	    IPW2100_ORD(NIC_MANF_DATE_TIME, "MANF Date/Time STAMP"),
	    IPW2100_ORD(UCODE_VERSION, "Ucode Version"),};

static ssize_t show_registers(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	int i;
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	struct net_device *dev = priv->net_dev;
	char *out = buf;
	u32 val = 0;

	out += sprintf(out, "%30s [Address ] : Hex\n", "Register");

	for (i = 0; i < ARRAY_SIZE(hw_data); i++) {
		read_register(dev, hw_data[i].addr, &val);
		out += sprintf(out, "%30s [%08X] : %08X\n",
			       hw_data[i].name, hw_data[i].addr, val);
	}

	return out - buf;
}

static DEVICE_ATTR(registers, S_IRUGO, show_registers, NULL);

static ssize_t show_hardware(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	struct net_device *dev = priv->net_dev;
	char *out = buf;
	int i;

	out += sprintf(out, "%30s [Address ] : Hex\n", "NIC entry");

	for (i = 0; i < ARRAY_SIZE(nic_data); i++) {
		u8 tmp8;
		u16 tmp16;
		u32 tmp32;

		switch (nic_data[i].size) {
		case 1:
			read_nic_byte(dev, nic_data[i].addr, &tmp8);
			out += sprintf(out, "%30s [%08X] : %02X\n",
				       nic_data[i].name, nic_data[i].addr,
				       tmp8);
			break;
		case 2:
			read_nic_word(dev, nic_data[i].addr, &tmp16);
			out += sprintf(out, "%30s [%08X] : %04X\n",
				       nic_data[i].name, nic_data[i].addr,
				       tmp16);
			break;
		case 4:
			read_nic_dword(dev, nic_data[i].addr, &tmp32);
			out += sprintf(out, "%30s [%08X] : %08X\n",
				       nic_data[i].name, nic_data[i].addr,
				       tmp32);
			break;
		}
	}
	return out - buf;
}

static DEVICE_ATTR(hardware, S_IRUGO, show_hardware, NULL);

static ssize_t show_memory(struct device *d, struct device_attribute *attr,
			   char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	struct net_device *dev = priv->net_dev;
	static unsigned long loop = 0;
	int len = 0;
	u32 buffer[4];
	int i;
	char line[81];

	if (loop >= 0x30000)
		loop = 0;

	/* sysfs provides us PAGE_SIZE buffer */
	while (len < PAGE_SIZE - 128 && loop < 0x30000) {

		if (priv->snapshot[0])
			for (i = 0; i < 4; i++)
				buffer[i] =
				    *(u32 *) SNAPSHOT_ADDR(loop + i * 4);
		else
			for (i = 0; i < 4; i++)
				read_nic_dword(dev, loop + i * 4, &buffer[i]);

		if (priv->dump_raw)
			len += sprintf(buf + len,
				       "%c%c%c%c"
				       "%c%c%c%c"
				       "%c%c%c%c"
				       "%c%c%c%c",
				       ((u8 *) buffer)[0x0],
				       ((u8 *) buffer)[0x1],
				       ((u8 *) buffer)[0x2],
				       ((u8 *) buffer)[0x3],
				       ((u8 *) buffer)[0x4],
				       ((u8 *) buffer)[0x5],
				       ((u8 *) buffer)[0x6],
				       ((u8 *) buffer)[0x7],
				       ((u8 *) buffer)[0x8],
				       ((u8 *) buffer)[0x9],
				       ((u8 *) buffer)[0xa],
				       ((u8 *) buffer)[0xb],
				       ((u8 *) buffer)[0xc],
				       ((u8 *) buffer)[0xd],
				       ((u8 *) buffer)[0xe],
				       ((u8 *) buffer)[0xf]);
		else
			len += sprintf(buf + len, "%s\n",
				       snprint_line(line, sizeof(line),
						    (u8 *) buffer, 16, loop));
		loop += 16;
	}

	return len;
}

static ssize_t store_memory(struct device *d, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	struct net_device *dev = priv->net_dev;
	const char *p = buf;

	(void)dev;		/* kill unused-var warning for debug-only code */

	if (count < 1)
		return count;

	if (p[0] == '1' ||
	    (count >= 2 && tolower(p[0]) == 'o' && tolower(p[1]) == 'n')) {
		IPW_DEBUG_INFO("%s: Setting memory dump to RAW mode.\n",
			       dev->name);
		priv->dump_raw = 1;

	} else if (p[0] == '0' || (count >= 2 && tolower(p[0]) == 'o' &&
				   tolower(p[1]) == 'f')) {
		IPW_DEBUG_INFO("%s: Setting memory dump to HEX mode.\n",
			       dev->name);
		priv->dump_raw = 0;

	} else if (tolower(p[0]) == 'r') {
		IPW_DEBUG_INFO("%s: Resetting firmware snapshot.\n", dev->name);
		ipw2100_snapshot_free(priv);

	} else
		IPW_DEBUG_INFO("%s: Usage: 0|on = HEX, 1|off = RAW, "
			       "reset = clear memory snapshot\n", dev->name);

	return count;
}

static DEVICE_ATTR(memory, S_IWUSR | S_IRUGO, show_memory, store_memory);

static ssize_t show_ordinals(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	u32 val = 0;
	int len = 0;
	u32 val_len;
	static int loop = 0;

	if (priv->status & STATUS_RF_KILL_MASK)
		return 0;

	if (loop >= ARRAY_SIZE(ord_data))
		loop = 0;

	/* sysfs provides us PAGE_SIZE buffer */
	while (len < PAGE_SIZE - 128 && loop < ARRAY_SIZE(ord_data)) {
		val_len = sizeof(u32);

		if (ipw2100_get_ordinal(priv, ord_data[loop].index, &val,
					&val_len))
			len += sprintf(buf + len, "[0x%02X] = ERROR    %s\n",
				       ord_data[loop].index,
				       ord_data[loop].desc);
		else
			len += sprintf(buf + len, "[0x%02X] = 0x%08X %s\n",
				       ord_data[loop].index, val,
				       ord_data[loop].desc);
		loop++;
	}

	return len;
}

static DEVICE_ATTR(ordinals, S_IRUGO, show_ordinals, NULL);

static ssize_t show_stats(struct device *d, struct device_attribute *attr,
			  char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	char *out = buf;

	out += sprintf(out, "interrupts: %d {tx: %d, rx: %d, other: %d}\n",
		       priv->interrupts, priv->tx_interrupts,
		       priv->rx_interrupts, priv->inta_other);
	out += sprintf(out, "firmware resets: %d\n", priv->resets);
	out += sprintf(out, "firmware hangs: %d\n", priv->hangs);
#ifdef CONFIG_IPW2100_DEBUG
	out += sprintf(out, "packet mismatch image: %s\n",
		       priv->snapshot[0] ? "YES" : "NO");
#endif

	return out - buf;
}

static DEVICE_ATTR(stats, S_IRUGO, show_stats, NULL);

static int ipw2100_switch_mode(struct ipw2100_priv *priv, u32 mode)
{
	int err;

	if (mode == priv->ieee->iw_mode)
		return 0;

	err = ipw2100_disable_adapter(priv);
	if (err) {
		printk(KERN_ERR DRV_NAME ": %s: Could not disable adapter %d\n",
		       priv->net_dev->name, err);
		return err;
	}

	switch (mode) {
	case IW_MODE_INFRA:
		priv->net_dev->type = ARPHRD_ETHER;
		break;
	case IW_MODE_ADHOC:
		priv->net_dev->type = ARPHRD_ETHER;
		break;
#ifdef CONFIG_IPW2100_MONITOR
	case IW_MODE_MONITOR:
		priv->last_mode = priv->ieee->iw_mode;
		priv->net_dev->type = ARPHRD_IEEE80211_RADIOTAP;
		break;
#endif				/* CONFIG_IPW2100_MONITOR */
	}

	priv->ieee->iw_mode = mode;

#ifdef CONFIG_PM
	/* Indicate ipw2100_download_firmware download firmware
	 * from disk instead of memory. */
	ipw2100_firmware.version = 0;
#endif

	printk(KERN_INFO "%s: Resetting on mode change.\n", priv->net_dev->name);
	priv->reset_backoff = 0;
	schedule_reset(priv);

	return 0;
}

static ssize_t show_internals(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	int len = 0;

#define DUMP_VAR(x,y) len += sprintf(buf + len, # x ": %" y "\n", priv-> x)

	if (priv->status & STATUS_ASSOCIATED)
		len += sprintf(buf + len, "connected: %lu\n",
			       get_seconds() - priv->connect_start);
	else
		len += sprintf(buf + len, "not connected\n");

	DUMP_VAR(ieee->crypt_info.crypt[priv->ieee->crypt_info.tx_keyidx], "p");
	DUMP_VAR(status, "08lx");
	DUMP_VAR(config, "08lx");
	DUMP_VAR(capability, "08lx");

	len +=
	    sprintf(buf + len, "last_rtc: %lu\n",
		    (unsigned long)priv->last_rtc);

	DUMP_VAR(fatal_error, "d");
	DUMP_VAR(stop_hang_check, "d");
	DUMP_VAR(stop_rf_kill, "d");
	DUMP_VAR(messages_sent, "d");

	DUMP_VAR(tx_pend_stat.value, "d");
	DUMP_VAR(tx_pend_stat.hi, "d");

	DUMP_VAR(tx_free_stat.value, "d");
	DUMP_VAR(tx_free_stat.lo, "d");

	DUMP_VAR(msg_free_stat.value, "d");
	DUMP_VAR(msg_free_stat.lo, "d");

	DUMP_VAR(msg_pend_stat.value, "d");
	DUMP_VAR(msg_pend_stat.hi, "d");

	DUMP_VAR(fw_pend_stat.value, "d");
	DUMP_VAR(fw_pend_stat.hi, "d");

	DUMP_VAR(txq_stat.value, "d");
	DUMP_VAR(txq_stat.lo, "d");

	DUMP_VAR(ieee->scans, "d");
	DUMP_VAR(reset_backoff, "d");

	return len;
}

static DEVICE_ATTR(internals, S_IRUGO, show_internals, NULL);

static ssize_t show_bssinfo(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	char essid[IW_ESSID_MAX_SIZE + 1];
	u8 bssid[ETH_ALEN];
	u32 chan = 0;
	char *out = buf;
	unsigned int length;
	int ret;

	if (priv->status & STATUS_RF_KILL_MASK)
		return 0;

	memset(essid, 0, sizeof(essid));
	memset(bssid, 0, sizeof(bssid));

	length = IW_ESSID_MAX_SIZE;
	ret = ipw2100_get_ordinal(priv, IPW_ORD_STAT_ASSN_SSID, essid, &length);
	if (ret)
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);

	length = sizeof(bssid);
	ret = ipw2100_get_ordinal(priv, IPW_ORD_STAT_ASSN_AP_BSSID,
				  bssid, &length);
	if (ret)
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);

	length = sizeof(u32);
	ret = ipw2100_get_ordinal(priv, IPW_ORD_OUR_FREQ, &chan, &length);
	if (ret)
		IPW_DEBUG_INFO("failed querying ordinals at line %d\n",
			       __LINE__);

	out += sprintf(out, "ESSID: %s\n", essid);
	out += sprintf(out, "BSSID:   %pM\n", bssid);
	out += sprintf(out, "Channel: %d\n", chan);

	return out - buf;
}

static DEVICE_ATTR(bssinfo, S_IRUGO, show_bssinfo, NULL);

#ifdef CONFIG_IPW2100_DEBUG
static ssize_t show_debug_level(struct device_driver *d, char *buf)
{
	return sprintf(buf, "0x%08X\n", ipw2100_debug_level);
}

static ssize_t store_debug_level(struct device_driver *d,
				 const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		p++;
		if (p[0] == 'x' || p[0] == 'X')
			p++;
		val = simple_strtoul(p, &p, 16);
	} else
		val = simple_strtoul(p, &p, 10);
	if (p == buf)
		IPW_DEBUG_INFO(": %s is not in hex or decimal form.\n", buf);
	else
		ipw2100_debug_level = val;

	return strnlen(buf, count);
}

static DRIVER_ATTR(debug_level, S_IWUSR | S_IRUGO, show_debug_level,
		   store_debug_level);
#endif				/* CONFIG_IPW2100_DEBUG */

static ssize_t show_fatal_error(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	char *out = buf;
	int i;

	if (priv->fatal_error)
		out += sprintf(out, "0x%08X\n", priv->fatal_error);
	else
		out += sprintf(out, "0\n");

	for (i = 1; i <= IPW2100_ERROR_QUEUE; i++) {
		if (!priv->fatal_errors[(priv->fatal_index - i) %
					IPW2100_ERROR_QUEUE])
			continue;

		out += sprintf(out, "%d. 0x%08X\n", i,
			       priv->fatal_errors[(priv->fatal_index - i) %
						  IPW2100_ERROR_QUEUE]);
	}

	return out - buf;
}

static ssize_t store_fatal_error(struct device *d,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	schedule_reset(priv);
	return count;
}

static DEVICE_ATTR(fatal_error, S_IWUSR | S_IRUGO, show_fatal_error,
		   store_fatal_error);

static ssize_t show_scan_age(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%d\n", priv->ieee->scan_age);
}

static ssize_t store_scan_age(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	struct net_device *dev = priv->net_dev;
	char buffer[] = "00000000";
	unsigned long len =
	    (sizeof(buffer) - 1) > count ? count : sizeof(buffer) - 1;
	unsigned long val;
	char *p = buffer;

	(void)dev;		/* kill unused-var warning for debug-only code */

	IPW_DEBUG_INFO("enter\n");

	strncpy(buffer, buf, len);
	buffer[len] = 0;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		p++;
		if (p[0] == 'x' || p[0] == 'X')
			p++;
		val = simple_strtoul(p, &p, 16);
	} else
		val = simple_strtoul(p, &p, 10);
	if (p == buffer) {
		IPW_DEBUG_INFO("%s: user supplied invalid value.\n", dev->name);
	} else {
		priv->ieee->scan_age = val;
		IPW_DEBUG_INFO("set scan_age = %u\n", priv->ieee->scan_age);
	}

	IPW_DEBUG_INFO("exit\n");
	return len;
}

static DEVICE_ATTR(scan_age, S_IWUSR | S_IRUGO, show_scan_age, store_scan_age);

static ssize_t show_rf_kill(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	/* 0 - RF kill not enabled
	   1 - SW based RF kill active (sysfs)
	   2 - HW based RF kill active
	   3 - Both HW and SW baed RF kill active */
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	int val = ((priv->status & STATUS_RF_KILL_SW) ? 0x1 : 0x0) |
	    (rf_kill_active(priv) ? 0x2 : 0x0);
	return sprintf(buf, "%i\n", val);
}

static int ipw_radio_kill_sw(struct ipw2100_priv *priv, int disable_radio)
{
	if ((disable_radio ? 1 : 0) ==
	    (priv->status & STATUS_RF_KILL_SW ? 1 : 0))
		return 0;

	IPW_DEBUG_RF_KILL("Manual SW RF Kill set to: RADIO  %s\n",
			  disable_radio ? "OFF" : "ON");

	mutex_lock(&priv->action_mutex);

	if (disable_radio) {
		priv->status |= STATUS_RF_KILL_SW;
		ipw2100_down(priv);
	} else {
		priv->status &= ~STATUS_RF_KILL_SW;
		if (rf_kill_active(priv)) {
			IPW_DEBUG_RF_KILL("Can not turn radio back on - "
					  "disabled by HW switch\n");
			/* Make sure the RF_KILL check timer is running */
			priv->stop_rf_kill = 0;
			mod_delayed_work(system_wq, &priv->rf_kill,
					 round_jiffies_relative(HZ));
		} else
			schedule_reset(priv);
	}

	mutex_unlock(&priv->action_mutex);
	return 1;
}

static ssize_t store_rf_kill(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ipw2100_priv *priv = dev_get_drvdata(d);
	ipw_radio_kill_sw(priv, buf[0] == '1');
	return count;
}

static DEVICE_ATTR(rf_kill, S_IWUSR | S_IRUGO, show_rf_kill, store_rf_kill);

static struct attribute *ipw2100_sysfs_entries[] = {
	&dev_attr_hardware.attr,
	&dev_attr_registers.attr,
	&dev_attr_ordinals.attr,
	&dev_attr_pci.attr,
	&dev_attr_stats.attr,
	&dev_attr_internals.attr,
	&dev_attr_bssinfo.attr,
	&dev_attr_memory.attr,
	&dev_attr_scan_age.attr,
	&dev_attr_fatal_error.attr,
	&dev_attr_rf_kill.attr,
	&dev_attr_cfg.attr,
	&dev_attr_status.attr,
	&dev_attr_capability.attr,
	NULL,
};

static struct attribute_group ipw2100_attribute_group = {
	.attrs = ipw2100_sysfs_entries,
};

static int status_queue_allocate(struct ipw2100_priv *priv, int entries)
{
	struct ipw2100_status_queue *q = &priv->status_queue;

	IPW_DEBUG_INFO("enter\n");

	q->size = entries * sizeof(struct ipw2100_status);
	q->drv =
	    (struct ipw2100_status *)pci_alloc_consistent(priv->pci_dev,
							  q->size, &q->nic);
	if (!q->drv) {
		IPW_DEBUG_WARNING("Can not allocate status queue.\n");
		return -ENOMEM;
	}

	memset(q->drv, 0, q->size);

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

static void status_queue_free(struct ipw2100_priv *priv)
{
	IPW_DEBUG_INFO("enter\n");

	if (priv->status_queue.drv) {
		pci_free_consistent(priv->pci_dev, priv->status_queue.size,
				    priv->status_queue.drv,
				    priv->status_queue.nic);
		priv->status_queue.drv = NULL;
	}

	IPW_DEBUG_INFO("exit\n");
}

static int bd_queue_allocate(struct ipw2100_priv *priv,
			     struct ipw2100_bd_queue *q, int entries)
{
	IPW_DEBUG_INFO("enter\n");

	memset(q, 0, sizeof(struct ipw2100_bd_queue));

	q->entries = entries;
	q->size = entries * sizeof(struct ipw2100_bd);
	q->drv = pci_alloc_consistent(priv->pci_dev, q->size, &q->nic);
	if (!q->drv) {
		IPW_DEBUG_INFO
		    ("can't allocate shared memory for buffer descriptors\n");
		return -ENOMEM;
	}
	memset(q->drv, 0, q->size);

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

static void bd_queue_free(struct ipw2100_priv *priv, struct ipw2100_bd_queue *q)
{
	IPW_DEBUG_INFO("enter\n");

	if (!q)
		return;

	if (q->drv) {
		pci_free_consistent(priv->pci_dev, q->size, q->drv, q->nic);
		q->drv = NULL;
	}

	IPW_DEBUG_INFO("exit\n");
}

static void bd_queue_initialize(struct ipw2100_priv *priv,
				struct ipw2100_bd_queue *q, u32 base, u32 size,
				u32 r, u32 w)
{
	IPW_DEBUG_INFO("enter\n");

	IPW_DEBUG_INFO("initializing bd queue at virt=%p, phys=%08x\n", q->drv,
		       (u32) q->nic);

	write_register(priv->net_dev, base, q->nic);
	write_register(priv->net_dev, size, q->entries);
	write_register(priv->net_dev, r, q->oldest);
	write_register(priv->net_dev, w, q->next);

	IPW_DEBUG_INFO("exit\n");
}

static void ipw2100_kill_works(struct ipw2100_priv *priv)
{
	priv->stop_rf_kill = 1;
	priv->stop_hang_check = 1;
	cancel_delayed_work_sync(&priv->reset_work);
	cancel_delayed_work_sync(&priv->security_work);
	cancel_delayed_work_sync(&priv->wx_event_work);
	cancel_delayed_work_sync(&priv->hang_check);
	cancel_delayed_work_sync(&priv->rf_kill);
	cancel_work_sync(&priv->scan_event_now);
	cancel_delayed_work_sync(&priv->scan_event_later);
}

static int ipw2100_tx_allocate(struct ipw2100_priv *priv)
{
	int i, j, err = -EINVAL;
	void *v;
	dma_addr_t p;

	IPW_DEBUG_INFO("enter\n");

	err = bd_queue_allocate(priv, &priv->tx_queue, TX_QUEUE_LENGTH);
	if (err) {
		IPW_DEBUG_ERROR("%s: failed bd_queue_allocate\n",
				priv->net_dev->name);
		return err;
	}

	priv->tx_buffers = kmalloc_array(TX_PENDED_QUEUE_LENGTH,
					 sizeof(struct ipw2100_tx_packet),
					 GFP_ATOMIC);
	if (!priv->tx_buffers) {
		bd_queue_free(priv, &priv->tx_queue);
		return -ENOMEM;
	}

	for (i = 0; i < TX_PENDED_QUEUE_LENGTH; i++) {
		v = pci_alloc_consistent(priv->pci_dev,
					 sizeof(struct ipw2100_data_header),
					 &p);
		if (!v) {
			printk(KERN_ERR DRV_NAME
			       ": %s: PCI alloc failed for tx " "buffers.\n",
			       priv->net_dev->name);
			err = -ENOMEM;
			break;
		}

		priv->tx_buffers[i].type = DATA;
		priv->tx_buffers[i].info.d_struct.data =
		    (struct ipw2100_data_header *)v;
		priv->tx_buffers[i].info.d_struct.data_phys = p;
		priv->tx_buffers[i].info.d_struct.txb = NULL;
	}

	if (i == TX_PENDED_QUEUE_LENGTH)
		return 0;

	for (j = 0; j < i; j++) {
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct ipw2100_data_header),
				    priv->tx_buffers[j].info.d_struct.data,
				    priv->tx_buffers[j].info.d_struct.
				    data_phys);
	}

	kfree(priv->tx_buffers);
	priv->tx_buffers = NULL;

	return err;
}

static void ipw2100_tx_initialize(struct ipw2100_priv *priv)
{
	int i;

	IPW_DEBUG_INFO("enter\n");

	/*
	 * reinitialize packet info lists
	 */
	INIT_LIST_HEAD(&priv->fw_pend_list);
	INIT_STAT(&priv->fw_pend_stat);

	/*
	 * reinitialize lists
	 */
	INIT_LIST_HEAD(&priv->tx_pend_list);
	INIT_LIST_HEAD(&priv->tx_free_list);
	INIT_STAT(&priv->tx_pend_stat);
	INIT_STAT(&priv->tx_free_stat);

	for (i = 0; i < TX_PENDED_QUEUE_LENGTH; i++) {
		/* We simply drop any SKBs that have been queued for
		 * transmit */
		if (priv->tx_buffers[i].info.d_struct.txb) {
			libipw_txb_free(priv->tx_buffers[i].info.d_struct.
					   txb);
			priv->tx_buffers[i].info.d_struct.txb = NULL;
		}

		list_add_tail(&priv->tx_buffers[i].list, &priv->tx_free_list);
	}

	SET_STAT(&priv->tx_free_stat, i);

	priv->tx_queue.oldest = 0;
	priv->tx_queue.available = priv->tx_queue.entries;
	priv->tx_queue.next = 0;
	INIT_STAT(&priv->txq_stat);
	SET_STAT(&priv->txq_stat, priv->tx_queue.available);

	bd_queue_initialize(priv, &priv->tx_queue,
			    IPW_MEM_HOST_SHARED_TX_QUEUE_BD_BASE,
			    IPW_MEM_HOST_SHARED_TX_QUEUE_BD_SIZE,
			    IPW_MEM_HOST_SHARED_TX_QUEUE_READ_INDEX,
			    IPW_MEM_HOST_SHARED_TX_QUEUE_WRITE_INDEX);

	IPW_DEBUG_INFO("exit\n");

}

static void ipw2100_tx_free(struct ipw2100_priv *priv)
{
	int i;

	IPW_DEBUG_INFO("enter\n");

	bd_queue_free(priv, &priv->tx_queue);

	if (!priv->tx_buffers)
		return;

	for (i = 0; i < TX_PENDED_QUEUE_LENGTH; i++) {
		if (priv->tx_buffers[i].info.d_struct.txb) {
			libipw_txb_free(priv->tx_buffers[i].info.d_struct.
					   txb);
			priv->tx_buffers[i].info.d_struct.txb = NULL;
		}
		if (priv->tx_buffers[i].info.d_struct.data)
			pci_free_consistent(priv->pci_dev,
					    sizeof(struct ipw2100_data_header),
					    priv->tx_buffers[i].info.d_struct.
					    data,
					    priv->tx_buffers[i].info.d_struct.
					    data_phys);
	}

	kfree(priv->tx_buffers);
	priv->tx_buffers = NULL;

	IPW_DEBUG_INFO("exit\n");
}

static int ipw2100_rx_allocate(struct ipw2100_priv *priv)
{
	int i, j, err = -EINVAL;

	IPW_DEBUG_INFO("enter\n");

	err = bd_queue_allocate(priv, &priv->rx_queue, RX_QUEUE_LENGTH);
	if (err) {
		IPW_DEBUG_INFO("failed bd_queue_allocate\n");
		return err;
	}

	err = status_queue_allocate(priv, RX_QUEUE_LENGTH);
	if (err) {
		IPW_DEBUG_INFO("failed status_queue_allocate\n");
		bd_queue_free(priv, &priv->rx_queue);
		return err;
	}

	/*
	 * allocate packets
	 */
	priv->rx_buffers = kmalloc(RX_QUEUE_LENGTH *
				   sizeof(struct ipw2100_rx_packet),
				   GFP_KERNEL);
	if (!priv->rx_buffers) {
		IPW_DEBUG_INFO("can't allocate rx packet buffer table\n");

		bd_queue_free(priv, &priv->rx_queue);

		status_queue_free(priv);

		return -ENOMEM;
	}

	for (i = 0; i < RX_QUEUE_LENGTH; i++) {
		struct ipw2100_rx_packet *packet = &priv->rx_buffers[i];

		err = ipw2100_alloc_skb(priv, packet);
		if (unlikely(err)) {
			err = -ENOMEM;
			break;
		}

		/* The BD holds the cache aligned address */
		priv->rx_queue.drv[i].host_addr = packet->dma_addr;
		priv->rx_queue.drv[i].buf_length = IPW_RX_NIC_BUFFER_LENGTH;
		priv->status_queue.drv[i].status_fields = 0;
	}

	if (i == RX_QUEUE_LENGTH)
		return 0;

	for (j = 0; j < i; j++) {
		pci_unmap_single(priv->pci_dev, priv->rx_buffers[j].dma_addr,
				 sizeof(struct ipw2100_rx_packet),
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb(priv->rx_buffers[j].skb);
	}

	kfree(priv->rx_buffers);
	priv->rx_buffers = NULL;

	bd_queue_free(priv, &priv->rx_queue);

	status_queue_free(priv);

	return err;
}

static void ipw2100_rx_initialize(struct ipw2100_priv *priv)
{
	IPW_DEBUG_INFO("enter\n");

	priv->rx_queue.oldest = 0;
	priv->rx_queue.available = priv->rx_queue.entries - 1;
	priv->rx_queue.next = priv->rx_queue.entries - 1;

	INIT_STAT(&priv->rxq_stat);
	SET_STAT(&priv->rxq_stat, priv->rx_queue.available);

	bd_queue_initialize(priv, &priv->rx_queue,
			    IPW_MEM_HOST_SHARED_RX_BD_BASE,
			    IPW_MEM_HOST_SHARED_RX_BD_SIZE,
			    IPW_MEM_HOST_SHARED_RX_READ_INDEX,
			    IPW_MEM_HOST_SHARED_RX_WRITE_INDEX);

	/* set up the status queue */
	write_register(priv->net_dev, IPW_MEM_HOST_SHARED_RX_STATUS_BASE,
		       priv->status_queue.nic);

	IPW_DEBUG_INFO("exit\n");
}

static void ipw2100_rx_free(struct ipw2100_priv *priv)
{
	int i;

	IPW_DEBUG_INFO("enter\n");

	bd_queue_free(priv, &priv->rx_queue);
	status_queue_free(priv);

	if (!priv->rx_buffers)
		return;

	for (i = 0; i < RX_QUEUE_LENGTH; i++) {
		if (priv->rx_buffers[i].rxp) {
			pci_unmap_single(priv->pci_dev,
					 priv->rx_buffers[i].dma_addr,
					 sizeof(struct ipw2100_rx),
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb(priv->rx_buffers[i].skb);
		}
	}

	kfree(priv->rx_buffers);
	priv->rx_buffers = NULL;

	IPW_DEBUG_INFO("exit\n");
}

static int ipw2100_read_mac_address(struct ipw2100_priv *priv)
{
	u32 length = ETH_ALEN;
	u8 addr[ETH_ALEN];

	int err;

	err = ipw2100_get_ordinal(priv, IPW_ORD_STAT_ADAPTER_MAC, addr, &length);
	if (err) {
		IPW_DEBUG_INFO("MAC address read failed\n");
		return -EIO;
	}

	memcpy(priv->net_dev->dev_addr, addr, ETH_ALEN);
	IPW_DEBUG_INFO("card MAC is %pM\n", priv->net_dev->dev_addr);

	return 0;
}

/********************************************************************
 *
 * Firmware Commands
 *
 ********************************************************************/

static int ipw2100_set_mac_address(struct ipw2100_priv *priv, int batch_mode)
{
	struct host_command cmd = {
		.host_command = ADAPTER_ADDRESS,
		.host_command_sequence = 0,
		.host_command_length = ETH_ALEN
	};
	int err;

	IPW_DEBUG_HC("SET_MAC_ADDRESS\n");

	IPW_DEBUG_INFO("enter\n");

	if (priv->config & CFG_CUSTOM_MAC) {
		memcpy(cmd.host_command_parameters, priv->mac_addr, ETH_ALEN);
		memcpy(priv->net_dev->dev_addr, priv->mac_addr, ETH_ALEN);
	} else
		memcpy(cmd.host_command_parameters, priv->net_dev->dev_addr,
		       ETH_ALEN);

	err = ipw2100_hw_send_command(priv, &cmd);

	IPW_DEBUG_INFO("exit\n");
	return err;
}

static int ipw2100_set_port_type(struct ipw2100_priv *priv, u32 port_type,
				 int batch_mode)
{
	struct host_command cmd = {
		.host_command = PORT_TYPE,
		.host_command_sequence = 0,
		.host_command_length = sizeof(u32)
	};
	int err;

	switch (port_type) {
	case IW_MODE_INFRA:
		cmd.host_command_parameters[0] = IPW_BSS;
		break;
	case IW_MODE_ADHOC:
		cmd.host_command_parameters[0] = IPW_IBSS;
		break;
	}

	IPW_DEBUG_HC("PORT_TYPE: %s\n",
		     port_type == IPW_IBSS ? "Ad-Hoc" : "Managed");

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Could not disable adapter %d\n",
			       priv->net_dev->name, err);
			return err;
		}
	}

	/* send cmd to firmware */
	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

static int ipw2100_set_channel(struct ipw2100_priv *priv, u32 channel,
			       int batch_mode)
{
	struct host_command cmd = {
		.host_command = CHANNEL,
		.host_command_sequence = 0,
		.host_command_length = sizeof(u32)
	};
	int err;

	cmd.host_command_parameters[0] = channel;

	IPW_DEBUG_HC("CHANNEL: %d\n", channel);

	/* If BSS then we don't support channel selection */
	if (priv->ieee->iw_mode == IW_MODE_INFRA)
		return 0;

	if ((channel != 0) &&
	    ((channel < REG_MIN_CHANNEL) || (channel > REG_MAX_CHANNEL)))
		return -EINVAL;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err) {
		IPW_DEBUG_INFO("Failed to set channel to %d", channel);
		return err;
	}

	if (channel)
		priv->config |= CFG_STATIC_CHANNEL;
	else
		priv->config &= ~CFG_STATIC_CHANNEL;

	priv->channel = channel;

	if (!batch_mode) {
		err = ipw2100_enable_adapter(priv);
		if (err)
			return err;
	}

	return 0;
}

static int ipw2100_system_config(struct ipw2100_priv *priv, int batch_mode)
{
	struct host_command cmd = {
		.host_command = SYSTEM_CONFIG,
		.host_command_sequence = 0,
		.host_command_length = 12,
	};
	u32 ibss_mask, len = sizeof(u32);
	int err;

	/* Set system configuration */

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	if (priv->ieee->iw_mode == IW_MODE_ADHOC)
		cmd.host_command_parameters[0] |= IPW_CFG_IBSS_AUTO_START;

	cmd.host_command_parameters[0] |= IPW_CFG_IBSS_MASK |
	    IPW_CFG_BSS_MASK | IPW_CFG_802_1x_ENABLE;

	if (!(priv->config & CFG_LONG_PREAMBLE))
		cmd.host_command_parameters[0] |= IPW_CFG_PREAMBLE_AUTO;

	err = ipw2100_get_ordinal(priv,
				  IPW_ORD_EEPROM_IBSS_11B_CHANNELS,
				  &ibss_mask, &len);
	if (err)
		ibss_mask = IPW_IBSS_11B_DEFAULT_MASK;

	cmd.host_command_parameters[1] = REG_CHANNEL_MASK;
	cmd.host_command_parameters[2] = REG_CHANNEL_MASK & ibss_mask;

	/* 11b only */
	/*cmd.host_command_parameters[0] |= DIVERSITY_ANTENNA_A; */

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

/* If IPv6 is configured in the kernel then we don't want to filter out all
 * of the multicast packets as IPv6 needs some. */
#if !defined(CONFIG_IPV6) && !defined(CONFIG_IPV6_MODULE)
	cmd.host_command = ADD_MULTICAST;
	cmd.host_command_sequence = 0;
	cmd.host_command_length = 0;

	ipw2100_hw_send_command(priv, &cmd);
#endif
	if (!batch_mode) {
		err = ipw2100_enable_adapter(priv);
		if (err)
			return err;
	}

	return 0;
}

static int ipw2100_set_tx_rates(struct ipw2100_priv *priv, u32 rate,
				int batch_mode)
{
	struct host_command cmd = {
		.host_command = BASIC_TX_RATES,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = rate & TX_RATE_MASK;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	/* Set BASIC TX Rate first */
	ipw2100_hw_send_command(priv, &cmd);

	/* Set TX Rate */
	cmd.host_command = TX_RATES;
	ipw2100_hw_send_command(priv, &cmd);

	/* Set MSDU TX Rate */
	cmd.host_command = MSDU_TX_RATES;
	ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode) {
		err = ipw2100_enable_adapter(priv);
		if (err)
			return err;
	}

	priv->tx_rates = rate;

	return 0;
}

static int ipw2100_set_power_mode(struct ipw2100_priv *priv, int power_level)
{
	struct host_command cmd = {
		.host_command = POWER_MODE,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = power_level;

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

	if (power_level == IPW_POWER_MODE_CAM)
		priv->power_mode = IPW_POWER_LEVEL(priv->power_mode);
	else
		priv->power_mode = IPW_POWER_ENABLED | power_level;

#ifdef IPW2100_TX_POWER
	if (priv->port_type == IBSS && priv->adhoc_power != DFTL_IBSS_TX_POWER) {
		/* Set beacon interval */
		cmd.host_command = TX_POWER_INDEX;
		cmd.host_command_parameters[0] = (u32) priv->adhoc_power;

		err = ipw2100_hw_send_command(priv, &cmd);
		if (err)
			return err;
	}
#endif

	return 0;
}

static int ipw2100_set_rts_threshold(struct ipw2100_priv *priv, u32 threshold)
{
	struct host_command cmd = {
		.host_command = RTS_THRESHOLD,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	if (threshold & RTS_DISABLED)
		cmd.host_command_parameters[0] = MAX_RTS_THRESHOLD;
	else
		cmd.host_command_parameters[0] = threshold & ~RTS_DISABLED;

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

	priv->rts_threshold = threshold;

	return 0;
}

#if 0
int ipw2100_set_fragmentation_threshold(struct ipw2100_priv *priv,
					u32 threshold, int batch_mode)
{
	struct host_command cmd = {
		.host_command = FRAG_THRESHOLD,
		.host_command_sequence = 0,
		.host_command_length = 4,
		.host_command_parameters[0] = 0,
	};
	int err;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	if (threshold == 0)
		threshold = DEFAULT_FRAG_THRESHOLD;
	else {
		threshold = max(threshold, MIN_FRAG_THRESHOLD);
		threshold = min(threshold, MAX_FRAG_THRESHOLD);
	}

	cmd.host_command_parameters[0] = threshold;

	IPW_DEBUG_HC("FRAG_THRESHOLD: %u\n", threshold);

	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	if (!err)
		priv->frag_threshold = threshold;

	return err;
}
#endif

static int ipw2100_set_short_retry(struct ipw2100_priv *priv, u32 retry)
{
	struct host_command cmd = {
		.host_command = SHORT_RETRY_LIMIT,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = retry;

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

	priv->short_retry_limit = retry;

	return 0;
}

static int ipw2100_set_long_retry(struct ipw2100_priv *priv, u32 retry)
{
	struct host_command cmd = {
		.host_command = LONG_RETRY_LIMIT,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = retry;

	err = ipw2100_hw_send_command(priv, &cmd);
	if (err)
		return err;

	priv->long_retry_limit = retry;

	return 0;
}

static int ipw2100_set_mandatory_bssid(struct ipw2100_priv *priv, u8 * bssid,
				       int batch_mode)
{
	struct host_command cmd = {
		.host_command = MANDATORY_BSSID,
		.host_command_sequence = 0,
		.host_command_length = (bssid == NULL) ? 0 : ETH_ALEN
	};
	int err;

#ifdef CONFIG_IPW2100_DEBUG
	if (bssid != NULL)
		IPW_DEBUG_HC("MANDATORY_BSSID: %pM\n", bssid);
	else
		IPW_DEBUG_HC("MANDATORY_BSSID: <clear>\n");
#endif
	/* if BSSID is empty then we disable mandatory bssid mode */
	if (bssid != NULL)
		memcpy(cmd.host_command_parameters, bssid, ETH_ALEN);

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

static int ipw2100_disassociate_bssid(struct ipw2100_priv *priv)
{
	struct host_command cmd = {
		.host_command = DISASSOCIATION_BSSID,
		.host_command_sequence = 0,
		.host_command_length = ETH_ALEN
	};
	int err;
	int len;

	IPW_DEBUG_HC("DISASSOCIATION_BSSID\n");

	len = ETH_ALEN;
	/* The Firmware currently ignores the BSSID and just disassociates from
	 * the currently associated AP -- but in the off chance that a future
	 * firmware does use the BSSID provided here, we go ahead and try and
	 * set it to the currently associated AP's BSSID */
	memcpy(cmd.host_command_parameters, priv->bssid, ETH_ALEN);

	err = ipw2100_hw_send_command(priv, &cmd);

	return err;
}

static int ipw2100_set_wpa_ie(struct ipw2100_priv *,
			      struct ipw2100_wpa_assoc_frame *, int)
    __attribute__ ((unused));

static int ipw2100_set_wpa_ie(struct ipw2100_priv *priv,
			      struct ipw2100_wpa_assoc_frame *wpa_frame,
			      int batch_mode)
{
	struct host_command cmd = {
		.host_command = SET_WPA_IE,
		.host_command_sequence = 0,
		.host_command_length = sizeof(struct ipw2100_wpa_assoc_frame),
	};
	int err;

	IPW_DEBUG_HC("SET_WPA_IE\n");

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	memcpy(cmd.host_command_parameters, wpa_frame,
	       sizeof(struct ipw2100_wpa_assoc_frame));

	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode) {
		if (ipw2100_enable_adapter(priv))
			err = -EIO;
	}

	return err;
}

struct security_info_params {
	u32 allowed_ciphers;
	u16 version;
	u8 auth_mode;
	u8 replay_counters_number;
	u8 unicast_using_group;
} __packed;

static int ipw2100_set_security_information(struct ipw2100_priv *priv,
					    int auth_mode,
					    int security_level,
					    int unicast_using_group,
					    int batch_mode)
{
	struct host_command cmd = {
		.host_command = SET_SECURITY_INFORMATION,
		.host_command_sequence = 0,
		.host_command_length = sizeof(struct security_info_params)
	};
	struct security_info_params *security =
	    (struct security_info_params *)&cmd.host_command_parameters;
	int err;
	memset(security, 0, sizeof(*security));

	/* If shared key AP authentication is turned on, then we need to
	 * configure the firmware to try and use it.
	 *
	 * Actual data encryption/decryption is handled by the host. */
	security->auth_mode = auth_mode;
	security->unicast_using_group = unicast_using_group;

	switch (security_level) {
	default:
	case SEC_LEVEL_0:
		security->allowed_ciphers = IPW_NONE_CIPHER;
		break;
	case SEC_LEVEL_1:
		security->allowed_ciphers = IPW_WEP40_CIPHER |
		    IPW_WEP104_CIPHER;
		break;
	case SEC_LEVEL_2:
		security->allowed_ciphers = IPW_WEP40_CIPHER |
		    IPW_WEP104_CIPHER | IPW_TKIP_CIPHER;
		break;
	case SEC_LEVEL_2_CKIP:
		security->allowed_ciphers = IPW_WEP40_CIPHER |
		    IPW_WEP104_CIPHER | IPW_CKIP_CIPHER;
		break;
	case SEC_LEVEL_3:
		security->allowed_ciphers = IPW_WEP40_CIPHER |
		    IPW_WEP104_CIPHER | IPW_TKIP_CIPHER | IPW_CCMP_CIPHER;
		break;
	}

	IPW_DEBUG_HC
	    ("SET_SECURITY_INFORMATION: auth:%d cipher:0x%02X (level %d)\n",
	     security->auth_mode, security->allowed_ciphers, security_level);

	security->replay_counters_number = 0;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

static int ipw2100_set_tx_power(struct ipw2100_priv *priv, u32 tx_power)
{
	struct host_command cmd = {
		.host_command = TX_POWER_INDEX,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err = 0;
	u32 tmp = tx_power;

	if (tx_power != IPW_TX_POWER_DEFAULT)
		tmp = (tx_power - IPW_TX_POWER_MIN_DBM) * 16 /
		      (IPW_TX_POWER_MAX_DBM - IPW_TX_POWER_MIN_DBM);

	cmd.host_command_parameters[0] = tmp;

	if (priv->ieee->iw_mode == IW_MODE_ADHOC)
		err = ipw2100_hw_send_command(priv, &cmd);
	if (!err)
		priv->tx_power = tx_power;

	return 0;
}

static int ipw2100_set_ibss_beacon_interval(struct ipw2100_priv *priv,
					    u32 interval, int batch_mode)
{
	struct host_command cmd = {
		.host_command = BEACON_INTERVAL,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = interval;

	IPW_DEBUG_INFO("enter\n");

	if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
		if (!batch_mode) {
			err = ipw2100_disable_adapter(priv);
			if (err)
				return err;
		}

		ipw2100_hw_send_command(priv, &cmd);

		if (!batch_mode) {
			err = ipw2100_enable_adapter(priv);
			if (err)
				return err;
		}
	}

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

static void ipw2100_queues_initialize(struct ipw2100_priv *priv)
{
	ipw2100_tx_initialize(priv);
	ipw2100_rx_initialize(priv);
	ipw2100_msg_initialize(priv);
}

static void ipw2100_queues_free(struct ipw2100_priv *priv)
{
	ipw2100_tx_free(priv);
	ipw2100_rx_free(priv);
	ipw2100_msg_free(priv);
}

static int ipw2100_queues_allocate(struct ipw2100_priv *priv)
{
	if (ipw2100_tx_allocate(priv) ||
	    ipw2100_rx_allocate(priv) || ipw2100_msg_allocate(priv))
		goto fail;

	return 0;

      fail:
	ipw2100_tx_free(priv);
	ipw2100_rx_free(priv);
	ipw2100_msg_free(priv);
	return -ENOMEM;
}

#define IPW_PRIVACY_CAPABLE 0x0008

static int ipw2100_set_wep_flags(struct ipw2100_priv *priv, u32 flags,
				 int batch_mode)
{
	struct host_command cmd = {
		.host_command = WEP_FLAGS,
		.host_command_sequence = 0,
		.host_command_length = 4
	};
	int err;

	cmd.host_command_parameters[0] = flags;

	IPW_DEBUG_HC("WEP_FLAGS: flags = 0x%08X\n", flags);

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Could not disable adapter %d\n",
			       priv->net_dev->name, err);
			return err;
		}
	}

	/* send cmd to firmware */
	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

struct ipw2100_wep_key {
	u8 idx;
	u8 len;
	u8 key[13];
};

/* Macros to ease up priting WEP keys */
#define WEP_FMT_64  "%02X%02X%02X%02X-%02X"
#define WEP_FMT_128 "%02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X"
#define WEP_STR_64(x) x[0],x[1],x[2],x[3],x[4]
#define WEP_STR_128(x) x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7],x[8],x[9],x[10]

/**
 * Set a the wep key
 *
 * @priv: struct to work on
 * @idx: index of the key we want to set
 * @key: ptr to the key data to set
 * @len: length of the buffer at @key
 * @batch_mode: FIXME perform the operation in batch mode, not
 *              disabling the device.
 *
 * @returns 0 if OK, < 0 errno code on error.
 *
 * Fill out a command structure with the new wep key, length an
 * index and send it down the wire.
 */
static int ipw2100_set_key(struct ipw2100_priv *priv,
			   int idx, char *key, int len, int batch_mode)
{
	int keylen = len ? (len <= 5 ? 5 : 13) : 0;
	struct host_command cmd = {
		.host_command = WEP_KEY_INFO,
		.host_command_sequence = 0,
		.host_command_length = sizeof(struct ipw2100_wep_key),
	};
	struct ipw2100_wep_key *wep_key = (void *)cmd.host_command_parameters;
	int err;

	IPW_DEBUG_HC("WEP_KEY_INFO: index = %d, len = %d/%d\n",
		     idx, keylen, len);

	/* NOTE: We don't check cached values in case the firmware was reset
	 * or some other problem is occurring.  If the user is setting the key,
	 * then we push the change */

	wep_key->idx = idx;
	wep_key->len = keylen;

	if (keylen) {
		memcpy(wep_key->key, key, len);
		memset(wep_key->key + len, 0, keylen - len);
	}

	/* Will be optimized out on debug not being configured in */
	if (keylen == 0)
		IPW_DEBUG_WEP("%s: Clearing key %d\n",
			      priv->net_dev->name, wep_key->idx);
	else if (keylen == 5)
		IPW_DEBUG_WEP("%s: idx: %d, len: %d key: " WEP_FMT_64 "\n",
			      priv->net_dev->name, wep_key->idx, wep_key->len,
			      WEP_STR_64(wep_key->key));
	else
		IPW_DEBUG_WEP("%s: idx: %d, len: %d key: " WEP_FMT_128
			      "\n",
			      priv->net_dev->name, wep_key->idx, wep_key->len,
			      WEP_STR_128(wep_key->key));

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		/* FIXME: IPG: shouldn't this prink be in _disable_adapter()? */
		if (err) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Could not disable adapter %d\n",
			       priv->net_dev->name, err);
			return err;
		}
	}

	/* send cmd to firmware */
	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode) {
		int err2 = ipw2100_enable_adapter(priv);
		if (err == 0)
			err = err2;
	}
	return err;
}

static int ipw2100_set_key_index(struct ipw2100_priv *priv,
				 int idx, int batch_mode)
{
	struct host_command cmd = {
		.host_command = WEP_KEY_INDEX,
		.host_command_sequence = 0,
		.host_command_length = 4,
		.host_command_parameters = {idx},
	};
	int err;

	IPW_DEBUG_HC("WEP_KEY_INDEX: index = %d\n", idx);

	if (idx < 0 || idx > 3)
		return -EINVAL;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Could not disable adapter %d\n",
			       priv->net_dev->name, err);
			return err;
		}
	}

	/* send cmd to firmware */
	err = ipw2100_hw_send_command(priv, &cmd);

	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

static int ipw2100_configure_security(struct ipw2100_priv *priv, int batch_mode)
{
	int i, err, auth_mode, sec_level, use_group;

	if (!(priv->status & STATUS_RUNNING))
		return 0;

	if (!batch_mode) {
		err = ipw2100_disable_adapter(priv);
		if (err)
			return err;
	}

	if (!priv->ieee->sec.enabled) {
		err =
		    ipw2100_set_security_information(priv, IPW_AUTH_OPEN,
						     SEC_LEVEL_0, 0, 1);
	} else {
		auth_mode = IPW_AUTH_OPEN;
		if (priv->ieee->sec.flags & SEC_AUTH_MODE) {
			if (priv->ieee->sec.auth_mode == WLAN_AUTH_SHARED_KEY)
				auth_mode = IPW_AUTH_SHARED;
			else if (priv->ieee->sec.auth_mode == WLAN_AUTH_LEAP)
				auth_mode = IPW_AUTH_LEAP_CISCO_ID;
		}

		sec_level = SEC_LEVEL_0;
		if (priv->ieee->sec.flags & SEC_LEVEL)
			sec_level = priv->ieee->sec.level;

		use_group = 0;
		if (priv->ieee->sec.flags & SEC_UNICAST_GROUP)
			use_group = priv->ieee->sec.unicast_uses_group;

		err =
		    ipw2100_set_security_information(priv, auth_mode, sec_level,
						     use_group, 1);
	}

	if (err)
		goto exit;

	if (priv->ieee->sec.enabled) {
		for (i = 0; i < 4; i++) {
			if (!(priv->ieee->sec.flags & (1 << i))) {
				memset(priv->ieee->sec.keys[i], 0, WEP_KEY_LEN);
				priv->ieee->sec.key_sizes[i] = 0;
			} else {
				err = ipw2100_set_key(priv, i,
						      priv->ieee->sec.keys[i],
						      priv->ieee->sec.
						      key_sizes[i], 1);
				if (err)
					goto exit;
			}
		}

		ipw2100_set_key_index(priv, priv->ieee->crypt_info.tx_keyidx, 1);
	}

	/* Always enable privacy so the Host can filter WEP packets if
	 * encrypted data is sent up */
	err =
	    ipw2100_set_wep_flags(priv,
				  priv->ieee->sec.
				  enabled ? IPW_PRIVACY_CAPABLE : 0, 1);
	if (err)
		goto exit;

	priv->status &= ~STATUS_SECURITY_UPDATED;

      exit:
	if (!batch_mode)
		ipw2100_enable_adapter(priv);

	return err;
}

static void ipw2100_security_work(struct work_struct *work)
{
	struct ipw2100_priv *priv =
		container_of(work, struct ipw2100_priv, security_work.work);

	/* If we happen to have reconnected before we get a chance to
	 * process this, then update the security settings--which causes
	 * a disassociation to occur */
	if (!(priv->status & STATUS_ASSOCIATED) &&
	    priv->status & STATUS_SECURITY_UPDATED)
		ipw2100_configure_security(priv, 0);
}

static void shim__set_security(struct net_device *dev,
			       struct libipw_security *sec)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int i, force_update = 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED))
		goto done;

	for (i = 0; i < 4; i++) {
		if (sec->flags & (1 << i)) {
			priv->ieee->sec.key_sizes[i] = sec->key_sizes[i];
			if (sec->key_sizes[i] == 0)
				priv->ieee->sec.flags &= ~(1 << i);
			else
				memcpy(priv->ieee->sec.keys[i], sec->keys[i],
				       sec->key_sizes[i]);
			if (sec->level == SEC_LEVEL_1) {
				priv->ieee->sec.flags |= (1 << i);
				priv->status |= STATUS_SECURITY_UPDATED;
			} else
				priv->ieee->sec.flags &= ~(1 << i);
		}
	}

	if ((sec->flags & SEC_ACTIVE_KEY) &&
	    priv->ieee->sec.active_key != sec->active_key) {
		if (sec->active_key <= 3) {
			priv->ieee->sec.active_key = sec->active_key;
			priv->ieee->sec.flags |= SEC_ACTIVE_KEY;
		} else
			priv->ieee->sec.flags &= ~SEC_ACTIVE_KEY;

		priv->status |= STATUS_SECURITY_UPDATED;
	}

	if ((sec->flags & SEC_AUTH_MODE) &&
	    (priv->ieee->sec.auth_mode != sec->auth_mode)) {
		priv->ieee->sec.auth_mode = sec->auth_mode;
		priv->ieee->sec.flags |= SEC_AUTH_MODE;
		priv->status |= STATUS_SECURITY_UPDATED;
	}

	if (sec->flags & SEC_ENABLED && priv->ieee->sec.enabled != sec->enabled) {
		priv->ieee->sec.flags |= SEC_ENABLED;
		priv->ieee->sec.enabled = sec->enabled;
		priv->status |= STATUS_SECURITY_UPDATED;
		force_update = 1;
	}

	if (sec->flags & SEC_ENCRYPT)
		priv->ieee->sec.encrypt = sec->encrypt;

	if (sec->flags & SEC_LEVEL && priv->ieee->sec.level != sec->level) {
		priv->ieee->sec.level = sec->level;
		priv->ieee->sec.flags |= SEC_LEVEL;
		priv->status |= STATUS_SECURITY_UPDATED;
	}

	IPW_DEBUG_WEP("Security flags: %c %c%c%c%c %c%c%c%c\n",
		      priv->ieee->sec.flags & (1 << 8) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 7) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 6) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 5) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 4) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 3) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 2) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 1) ? '1' : '0',
		      priv->ieee->sec.flags & (1 << 0) ? '1' : '0');

/* As a temporary work around to enable WPA until we figure out why
 * wpa_supplicant toggles the security capability of the driver, which
 * forces a disassocation with force_update...
 *
 *	if (force_update || !(priv->status & STATUS_ASSOCIATED))*/
	if (!(priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)))
		ipw2100_configure_security(priv, 0);
      done:
	mutex_unlock(&priv->action_mutex);
}

static int ipw2100_adapter_setup(struct ipw2100_priv *priv)
{
	int err;
	int batch_mode = 1;
	u8 *bssid;

	IPW_DEBUG_INFO("enter\n");

	err = ipw2100_disable_adapter(priv);
	if (err)
		return err;
#ifdef CONFIG_IPW2100_MONITOR
	if (priv->ieee->iw_mode == IW_MODE_MONITOR) {
		err = ipw2100_set_channel(priv, priv->channel, batch_mode);
		if (err)
			return err;

		IPW_DEBUG_INFO("exit\n");

		return 0;
	}
#endif				/* CONFIG_IPW2100_MONITOR */

	err = ipw2100_read_mac_address(priv);
	if (err)
		return -EIO;

	err = ipw2100_set_mac_address(priv, batch_mode);
	if (err)
		return err;

	err = ipw2100_set_port_type(priv, priv->ieee->iw_mode, batch_mode);
	if (err)
		return err;

	if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
		err = ipw2100_set_channel(priv, priv->channel, batch_mode);
		if (err)
			return err;
	}

	err = ipw2100_system_config(priv, batch_mode);
	if (err)
		return err;

	err = ipw2100_set_tx_rates(priv, priv->tx_rates, batch_mode);
	if (err)
		return err;

	/* Default to power mode OFF */
	err = ipw2100_set_power_mode(priv, IPW_POWER_MODE_CAM);
	if (err)
		return err;

	err = ipw2100_set_rts_threshold(priv, priv->rts_threshold);
	if (err)
		return err;

	if (priv->config & CFG_STATIC_BSSID)
		bssid = priv->bssid;
	else
		bssid = NULL;
	err = ipw2100_set_mandatory_bssid(priv, bssid, batch_mode);
	if (err)
		return err;

	if (priv->config & CFG_STATIC_ESSID)
		err = ipw2100_set_essid(priv, priv->essid, priv->essid_len,
					batch_mode);
	else
		err = ipw2100_set_essid(priv, NULL, 0, batch_mode);
	if (err)
		return err;

	err = ipw2100_configure_security(priv, batch_mode);
	if (err)
		return err;

	if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
		err =
		    ipw2100_set_ibss_beacon_interval(priv,
						     priv->beacon_interval,
						     batch_mode);
		if (err)
			return err;

		err = ipw2100_set_tx_power(priv, priv->tx_power);
		if (err)
			return err;
	}

	/*
	   err = ipw2100_set_fragmentation_threshold(
	   priv, priv->frag_threshold, batch_mode);
	   if (err)
	   return err;
	 */

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

/*************************************************************************
 *
 * EXTERNALLY CALLED METHODS
 *
 *************************************************************************/

/* This method is called by the network layer -- not to be confused with
 * ipw2100_set_mac_address() declared above called by this driver (and this
 * method as well) to talk to the firmware */
static int ipw2100_set_address(struct net_device *dev, void *p)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct sockaddr *addr = p;
	int err = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	mutex_lock(&priv->action_mutex);

	priv->config |= CFG_CUSTOM_MAC;
	memcpy(priv->mac_addr, addr->sa_data, ETH_ALEN);

	err = ipw2100_set_mac_address(priv, 0);
	if (err)
		goto done;

	priv->reset_backoff = 0;
	mutex_unlock(&priv->action_mutex);
	ipw2100_reset_adapter(&priv->reset_work.work);
	return 0;

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_open(struct net_device *dev)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	unsigned long flags;
	IPW_DEBUG_INFO("dev->open\n");

	spin_lock_irqsave(&priv->low_lock, flags);
	if (priv->status & STATUS_ASSOCIATED) {
		netif_carrier_on(dev);
		netif_start_queue(dev);
	}
	spin_unlock_irqrestore(&priv->low_lock, flags);

	return 0;
}

static int ipw2100_close(struct net_device *dev)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	unsigned long flags;
	struct list_head *element;
	struct ipw2100_tx_packet *packet;

	IPW_DEBUG_INFO("enter\n");

	spin_lock_irqsave(&priv->low_lock, flags);

	if (priv->status & STATUS_ASSOCIATED)
		netif_carrier_off(dev);
	netif_stop_queue(dev);

	/* Flush the TX queue ... */
	while (!list_empty(&priv->tx_pend_list)) {
		element = priv->tx_pend_list.next;
		packet = list_entry(element, struct ipw2100_tx_packet, list);

		list_del(element);
		DEC_STAT(&priv->tx_pend_stat);

		libipw_txb_free(packet->info.d_struct.txb);
		packet->info.d_struct.txb = NULL;

		list_add_tail(element, &priv->tx_free_list);
		INC_STAT(&priv->tx_free_stat);
	}
	spin_unlock_irqrestore(&priv->low_lock, flags);

	IPW_DEBUG_INFO("exit\n");

	return 0;
}

/*
 * TODO:  Fix this function... its just wrong
 */
static void ipw2100_tx_timeout(struct net_device *dev)
{
	struct ipw2100_priv *priv = libipw_priv(dev);

	dev->stats.tx_errors++;

#ifdef CONFIG_IPW2100_MONITOR
	if (priv->ieee->iw_mode == IW_MODE_MONITOR)
		return;
#endif

	IPW_DEBUG_INFO("%s: TX timed out.  Scheduling firmware restart.\n",
		       dev->name);
	schedule_reset(priv);
}

static int ipw2100_wpa_enable(struct ipw2100_priv *priv, int value)
{
	/* This is called when wpa_supplicant loads and closes the driver
	 * interface. */
	priv->ieee->wpa_enabled = value;
	return 0;
}

static int ipw2100_wpa_set_auth_algs(struct ipw2100_priv *priv, int value)
{

	struct libipw_device *ieee = priv->ieee;
	struct libipw_security sec = {
		.flags = SEC_AUTH_MODE,
	};
	int ret = 0;

	if (value & IW_AUTH_ALG_SHARED_KEY) {
		sec.auth_mode = WLAN_AUTH_SHARED_KEY;
		ieee->open_wep = 0;
	} else if (value & IW_AUTH_ALG_OPEN_SYSTEM) {
		sec.auth_mode = WLAN_AUTH_OPEN;
		ieee->open_wep = 1;
	} else if (value & IW_AUTH_ALG_LEAP) {
		sec.auth_mode = WLAN_AUTH_LEAP;
		ieee->open_wep = 1;
	} else
		return -EINVAL;

	if (ieee->set_security)
		ieee->set_security(ieee->dev, &sec);
	else
		ret = -EOPNOTSUPP;

	return ret;
}

static void ipw2100_wpa_assoc_frame(struct ipw2100_priv *priv,
				    char *wpa_ie, int wpa_ie_len)
{

	struct ipw2100_wpa_assoc_frame frame;

	frame.fixed_ie_mask = 0;

	/* copy WPA IE */
	memcpy(frame.var_ie, wpa_ie, wpa_ie_len);
	frame.var_ie_len = wpa_ie_len;

	/* make sure WPA is enabled */
	ipw2100_wpa_enable(priv, 1);
	ipw2100_set_wpa_ie(priv, &frame, 0);
}

static void ipw_ethtool_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *info)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	char fw_ver[64], ucode_ver[64];

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));

	ipw2100_get_fwversion(priv, fw_ver, sizeof(fw_ver));
	ipw2100_get_ucodeversion(priv, ucode_ver, sizeof(ucode_ver));

	snprintf(info->fw_version, sizeof(info->fw_version), "%s:%d:%s",
		 fw_ver, priv->eeprom_version, ucode_ver);

	strlcpy(info->bus_info, pci_name(priv->pci_dev),
		sizeof(info->bus_info));
}

static u32 ipw2100_ethtool_get_link(struct net_device *dev)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	return (priv->status & STATUS_ASSOCIATED) ? 1 : 0;
}

static const struct ethtool_ops ipw2100_ethtool_ops = {
	.get_link = ipw2100_ethtool_get_link,
	.get_drvinfo = ipw_ethtool_get_drvinfo,
};

static void ipw2100_hang_check(struct work_struct *work)
{
	struct ipw2100_priv *priv =
		container_of(work, struct ipw2100_priv, hang_check.work);
	unsigned long flags;
	u32 rtc = 0xa5a5a5a5;
	u32 len = sizeof(rtc);
	int restart = 0;

	spin_lock_irqsave(&priv->low_lock, flags);

	if (priv->fatal_error != 0) {
		/* If fatal_error is set then we need to restart */
		IPW_DEBUG_INFO("%s: Hardware fatal error detected.\n",
			       priv->net_dev->name);

		restart = 1;
	} else if (ipw2100_get_ordinal(priv, IPW_ORD_RTC_TIME, &rtc, &len) ||
		   (rtc == priv->last_rtc)) {
		/* Check if firmware is hung */
		IPW_DEBUG_INFO("%s: Firmware RTC stalled.\n",
			       priv->net_dev->name);

		restart = 1;
	}

	if (restart) {
		/* Kill timer */
		priv->stop_hang_check = 1;
		priv->hangs++;

		/* Restart the NIC */
		schedule_reset(priv);
	}

	priv->last_rtc = rtc;

	if (!priv->stop_hang_check)
		schedule_delayed_work(&priv->hang_check, HZ / 2);

	spin_unlock_irqrestore(&priv->low_lock, flags);
}

static void ipw2100_rf_kill(struct work_struct *work)
{
	struct ipw2100_priv *priv =
		container_of(work, struct ipw2100_priv, rf_kill.work);
	unsigned long flags;

	spin_lock_irqsave(&priv->low_lock, flags);

	if (rf_kill_active(priv)) {
		IPW_DEBUG_RF_KILL("RF Kill active, rescheduling GPIO check\n");
		if (!priv->stop_rf_kill)
			schedule_delayed_work(&priv->rf_kill,
					      round_jiffies_relative(HZ));
		goto exit_unlock;
	}

	/* RF Kill is now disabled, so bring the device back up */

	if (!(priv->status & STATUS_RF_KILL_MASK)) {
		IPW_DEBUG_RF_KILL("HW RF Kill no longer active, restarting "
				  "device\n");
		schedule_reset(priv);
	} else
		IPW_DEBUG_RF_KILL("HW RF Kill deactivated.  SW RF Kill still "
				  "enabled\n");

      exit_unlock:
	spin_unlock_irqrestore(&priv->low_lock, flags);
}

static void ipw2100_irq_tasklet(struct ipw2100_priv *priv);

static const struct net_device_ops ipw2100_netdev_ops = {
	.ndo_open		= ipw2100_open,
	.ndo_stop		= ipw2100_close,
	.ndo_start_xmit		= libipw_xmit,
	.ndo_change_mtu		= libipw_change_mtu,
	.ndo_tx_timeout		= ipw2100_tx_timeout,
	.ndo_set_mac_address	= ipw2100_set_address,
	.ndo_validate_addr	= eth_validate_addr,
};

/* Look into using netdev destructor to shutdown libipw? */

static struct net_device *ipw2100_alloc_device(struct pci_dev *pci_dev,
					       void __iomem * ioaddr)
{
	struct ipw2100_priv *priv;
	struct net_device *dev;

	dev = alloc_libipw(sizeof(struct ipw2100_priv), 0);
	if (!dev)
		return NULL;
	priv = libipw_priv(dev);
	priv->ieee = netdev_priv(dev);
	priv->pci_dev = pci_dev;
	priv->net_dev = dev;
	priv->ioaddr = ioaddr;

	priv->ieee->hard_start_xmit = ipw2100_tx;
	priv->ieee->set_security = shim__set_security;

	priv->ieee->perfect_rssi = -20;
	priv->ieee->worst_rssi = -85;

	dev->netdev_ops = &ipw2100_netdev_ops;
	dev->ethtool_ops = &ipw2100_ethtool_ops;
	dev->wireless_handlers = &ipw2100_wx_handler_def;
	priv->wireless_data.libipw = priv->ieee;
	dev->wireless_data = &priv->wireless_data;
	dev->watchdog_timeo = 3 * HZ;
	dev->irq = 0;

	/* NOTE: We don't use the wireless_handlers hook
	 * in dev as the system will start throwing WX requests
	 * to us before we're actually initialized and it just
	 * ends up causing problems.  So, we just handle
	 * the WX extensions through the ipw2100_ioctl interface */

	/* memset() puts everything to 0, so we only have explicitly set
	 * those values that need to be something else */

	/* If power management is turned on, default to AUTO mode */
	priv->power_mode = IPW_POWER_AUTO;

#ifdef CONFIG_IPW2100_MONITOR
	priv->config |= CFG_CRC_CHECK;
#endif
	priv->ieee->wpa_enabled = 0;
	priv->ieee->drop_unencrypted = 0;
	priv->ieee->privacy_invoked = 0;
	priv->ieee->ieee802_1x = 1;

	/* Set module parameters */
	switch (network_mode) {
	case 1:
		priv->ieee->iw_mode = IW_MODE_ADHOC;
		break;
#ifdef CONFIG_IPW2100_MONITOR
	case 2:
		priv->ieee->iw_mode = IW_MODE_MONITOR;
		break;
#endif
	default:
	case 0:
		priv->ieee->iw_mode = IW_MODE_INFRA;
		break;
	}

	if (disable == 1)
		priv->status |= STATUS_RF_KILL_SW;

	if (channel != 0 &&
	    ((channel >= REG_MIN_CHANNEL) && (channel <= REG_MAX_CHANNEL))) {
		priv->config |= CFG_STATIC_CHANNEL;
		priv->channel = channel;
	}

	if (associate)
		priv->config |= CFG_ASSOCIATE;

	priv->beacon_interval = DEFAULT_BEACON_INTERVAL;
	priv->short_retry_limit = DEFAULT_SHORT_RETRY_LIMIT;
	priv->long_retry_limit = DEFAULT_LONG_RETRY_LIMIT;
	priv->rts_threshold = DEFAULT_RTS_THRESHOLD | RTS_DISABLED;
	priv->frag_threshold = DEFAULT_FTS | FRAG_DISABLED;
	priv->tx_power = IPW_TX_POWER_DEFAULT;
	priv->tx_rates = DEFAULT_TX_RATES;

	strcpy(priv->nick, "ipw2100");

	spin_lock_init(&priv->low_lock);
	mutex_init(&priv->action_mutex);
	mutex_init(&priv->adapter_mutex);

	init_waitqueue_head(&priv->wait_command_queue);

	netif_carrier_off(dev);

	INIT_LIST_HEAD(&priv->msg_free_list);
	INIT_LIST_HEAD(&priv->msg_pend_list);
	INIT_STAT(&priv->msg_free_stat);
	INIT_STAT(&priv->msg_pend_stat);

	INIT_LIST_HEAD(&priv->tx_free_list);
	INIT_LIST_HEAD(&priv->tx_pend_list);
	INIT_STAT(&priv->tx_free_stat);
	INIT_STAT(&priv->tx_pend_stat);

	INIT_LIST_HEAD(&priv->fw_pend_list);
	INIT_STAT(&priv->fw_pend_stat);

	INIT_DELAYED_WORK(&priv->reset_work, ipw2100_reset_adapter);
	INIT_DELAYED_WORK(&priv->security_work, ipw2100_security_work);
	INIT_DELAYED_WORK(&priv->wx_event_work, ipw2100_wx_event_work);
	INIT_DELAYED_WORK(&priv->hang_check, ipw2100_hang_check);
	INIT_DELAYED_WORK(&priv->rf_kill, ipw2100_rf_kill);
	INIT_WORK(&priv->scan_event_now, ipw2100_scan_event_now);
	INIT_DELAYED_WORK(&priv->scan_event_later, ipw2100_scan_event_later);

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     ipw2100_irq_tasklet, (unsigned long)priv);

	/* NOTE:  We do not start the deferred work for status checks yet */
	priv->stop_rf_kill = 1;
	priv->stop_hang_check = 1;

	return dev;
}

static int ipw2100_pci_init_one(struct pci_dev *pci_dev,
				const struct pci_device_id *ent)
{
	void __iomem *ioaddr;
	struct net_device *dev = NULL;
	struct ipw2100_priv *priv = NULL;
	int err = 0;
	int registered = 0;
	u32 val;

	IPW_DEBUG_INFO("enter\n");

	if (!(pci_resource_flags(pci_dev, 0) & IORESOURCE_MEM)) {
		IPW_DEBUG_INFO("weird - resource type is not memory\n");
		err = -ENODEV;
		goto out;
	}

	ioaddr = pci_iomap(pci_dev, 0, 0);
	if (!ioaddr) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling ioremap_nocache.\n");
		err = -EIO;
		goto fail;
	}

	/* allocate and initialize our net_device */
	dev = ipw2100_alloc_device(pci_dev, ioaddr);
	if (!dev) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling ipw2100_alloc_device.\n");
		err = -ENOMEM;
		goto fail;
	}

	/* set up PCI mappings for device */
	err = pci_enable_device(pci_dev);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling pci_enable_device.\n");
		return err;
	}

	priv = libipw_priv(dev);

	pci_set_master(pci_dev);
	pci_set_drvdata(pci_dev, priv);

	err = pci_set_dma_mask(pci_dev, DMA_BIT_MASK(32));
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling pci_set_dma_mask.\n");
		pci_disable_device(pci_dev);
		return err;
	}

	err = pci_request_regions(pci_dev, DRV_NAME);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling pci_request_regions.\n");
		pci_disable_device(pci_dev);
		return err;
	}

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_read_config_dword(pci_dev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pci_dev, 0x40, val & 0xffff00ff);

	pci_set_power_state(pci_dev, PCI_D0);

	if (!ipw2100_hw_is_adapter_in_system(dev)) {
		printk(KERN_WARNING DRV_NAME
		       "Device not found via register read.\n");
		err = -ENODEV;
		goto fail;
	}

	SET_NETDEV_DEV(dev, &pci_dev->dev);

	/* Force interrupts to be shut off on the device */
	priv->status |= STATUS_INT_ENABLED;
	ipw2100_disable_interrupts(priv);

	/* Allocate and initialize the Tx/Rx queues and lists */
	if (ipw2100_queues_allocate(priv)) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling ipw2100_queues_allocate.\n");
		err = -ENOMEM;
		goto fail;
	}
	ipw2100_queues_initialize(priv);

	err = request_irq(pci_dev->irq,
			  ipw2100_interrupt, IRQF_SHARED, dev->name, priv);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling request_irq: %d.\n", pci_dev->irq);
		goto fail;
	}
	dev->irq = pci_dev->irq;

	IPW_DEBUG_INFO("Attempting to register device...\n");

	printk(KERN_INFO DRV_NAME
	       ": Detected Intel PRO/Wireless 2100 Network Connection\n");

	err = ipw2100_up(priv, 1);
	if (err)
		goto fail;

	err = ipw2100_wdev_init(dev);
	if (err)
		goto fail;
	registered = 1;

	/* Bring up the interface.  Pre 0.46, after we registered the
	 * network device we would call ipw2100_up.  This introduced a race
	 * condition with newer hotplug configurations (network was coming
	 * up and making calls before the device was initialized).
	 */
	err = register_netdev(dev);
	if (err) {
		printk(KERN_WARNING DRV_NAME
		       "Error calling register_netdev.\n");
		goto fail;
	}
	registered = 2;

	mutex_lock(&priv->action_mutex);

	IPW_DEBUG_INFO("%s: Bound to %s\n", dev->name, pci_name(pci_dev));

	/* perform this after register_netdev so that dev->name is set */
	err = sysfs_create_group(&pci_dev->dev.kobj, &ipw2100_attribute_group);
	if (err)
		goto fail_unlock;

	/* If the RF Kill switch is disabled, go ahead and complete the
	 * startup sequence */
	if (!(priv->status & STATUS_RF_KILL_MASK)) {
		/* Enable the adapter - sends HOST_COMPLETE */
		if (ipw2100_enable_adapter(priv)) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: failed in call to enable adapter.\n",
			       priv->net_dev->name);
			ipw2100_hw_stop_adapter(priv);
			err = -EIO;
			goto fail_unlock;
		}

		/* Start a scan . . . */
		ipw2100_set_scan_options(priv);
		ipw2100_start_scan(priv);
	}

	IPW_DEBUG_INFO("exit\n");

	priv->status |= STATUS_INITIALIZED;

	mutex_unlock(&priv->action_mutex);
out:
	return err;

      fail_unlock:
	mutex_unlock(&priv->action_mutex);
      fail:
	if (dev) {
		if (registered >= 2)
			unregister_netdev(dev);

		if (registered) {
			wiphy_unregister(priv->ieee->wdev.wiphy);
			kfree(priv->ieee->bg_band.channels);
		}

		ipw2100_hw_stop_adapter(priv);

		ipw2100_disable_interrupts(priv);

		if (dev->irq)
			free_irq(dev->irq, priv);

		ipw2100_kill_works(priv);

		/* These are safe to call even if they weren't allocated */
		ipw2100_queues_free(priv);
		sysfs_remove_group(&pci_dev->dev.kobj,
				   &ipw2100_attribute_group);

		free_libipw(dev, 0);
		pci_set_drvdata(pci_dev, NULL);
	}

	pci_iounmap(pci_dev, ioaddr);

	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
	goto out;
}

static void ipw2100_pci_remove_one(struct pci_dev *pci_dev)
{
	struct ipw2100_priv *priv = pci_get_drvdata(pci_dev);
	struct net_device *dev = priv->net_dev;

	mutex_lock(&priv->action_mutex);

	priv->status &= ~STATUS_INITIALIZED;

	sysfs_remove_group(&pci_dev->dev.kobj, &ipw2100_attribute_group);

#ifdef CONFIG_PM
	if (ipw2100_firmware.version)
		ipw2100_release_firmware(priv, &ipw2100_firmware);
#endif
	/* Take down the hardware */
	ipw2100_down(priv);

	/* Release the mutex so that the network subsystem can
	 * complete any needed calls into the driver... */
	mutex_unlock(&priv->action_mutex);

	/* Unregister the device first - this results in close()
	 * being called if the device is open.  If we free storage
	 * first, then close() will crash.
	 * FIXME: remove the comment above. */
	unregister_netdev(dev);

	ipw2100_kill_works(priv);

	ipw2100_queues_free(priv);

	/* Free potential debugging firmware snapshot */
	ipw2100_snapshot_free(priv);

	free_irq(dev->irq, priv);

	pci_iounmap(pci_dev, priv->ioaddr);

	/* wiphy_unregister needs to be here, before free_libipw */
	wiphy_unregister(priv->ieee->wdev.wiphy);
	kfree(priv->ieee->bg_band.channels);
	free_libipw(dev, 0);

	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);

	IPW_DEBUG_INFO("exit\n");
}

#ifdef CONFIG_PM
static int ipw2100_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct ipw2100_priv *priv = pci_get_drvdata(pci_dev);
	struct net_device *dev = priv->net_dev;

	IPW_DEBUG_INFO("%s: Going into suspend...\n", dev->name);

	mutex_lock(&priv->action_mutex);
	if (priv->status & STATUS_INITIALIZED) {
		/* Take down the device; powers it off, etc. */
		ipw2100_down(priv);
	}

	/* Remove the PRESENT state of the device */
	netif_device_detach(dev);

	pci_save_state(pci_dev);
	pci_disable_device(pci_dev);
	pci_set_power_state(pci_dev, PCI_D3hot);

	priv->suspend_at = get_seconds();

	mutex_unlock(&priv->action_mutex);

	return 0;
}

static int ipw2100_resume(struct pci_dev *pci_dev)
{
	struct ipw2100_priv *priv = pci_get_drvdata(pci_dev);
	struct net_device *dev = priv->net_dev;
	int err;
	u32 val;

	if (IPW2100_PM_DISABLED)
		return 0;

	mutex_lock(&priv->action_mutex);

	IPW_DEBUG_INFO("%s: Coming out of suspend...\n", dev->name);

	pci_set_power_state(pci_dev, PCI_D0);
	err = pci_enable_device(pci_dev);
	if (err) {
		printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
		       dev->name);
		mutex_unlock(&priv->action_mutex);
		return err;
	}
	pci_restore_state(pci_dev);

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep PCI Tx retries
	 * from interfering with C3 CPU state. pci_restore_state won't help
	 * here since it only restores the first 64 bytes pci config header.
	 */
	pci_read_config_dword(pci_dev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pci_dev, 0x40, val & 0xffff00ff);

	/* Set the device back into the PRESENT state; this will also wake
	 * the queue of needed */
	netif_device_attach(dev);

	priv->suspend_time = get_seconds() - priv->suspend_at;

	/* Bring the device back up */
	if (!(priv->status & STATUS_RF_KILL_SW))
		ipw2100_up(priv, 0);

	mutex_unlock(&priv->action_mutex);

	return 0;
}
#endif

static void ipw2100_shutdown(struct pci_dev *pci_dev)
{
	struct ipw2100_priv *priv = pci_get_drvdata(pci_dev);

	/* Take down the device; powers it off, etc. */
	ipw2100_down(priv);

	pci_disable_device(pci_dev);
}

#define IPW2100_DEV_ID(x) { PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, x }

static DEFINE_PCI_DEVICE_TABLE(ipw2100_pci_id_table) = {
	IPW2100_DEV_ID(0x2520),	/* IN 2100A mPCI 3A */
	IPW2100_DEV_ID(0x2521),	/* IN 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2524),	/* IN 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2525),	/* IN 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2526),	/* IN 2100A mPCI Gen A3 */
	IPW2100_DEV_ID(0x2522),	/* IN 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2523),	/* IN 2100 mPCI 3A */
	IPW2100_DEV_ID(0x2527),	/* IN 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2528),	/* IN 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2529),	/* IN 2100 mPCI 3B */
	IPW2100_DEV_ID(0x252B),	/* IN 2100 mPCI 3A */
	IPW2100_DEV_ID(0x252C),	/* IN 2100 mPCI 3A */
	IPW2100_DEV_ID(0x252D),	/* IN 2100 mPCI 3A */

	IPW2100_DEV_ID(0x2550),	/* IB 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2551),	/* IB 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2553),	/* IB 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2554),	/* IB 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2555),	/* IB 2100 mPCI 3B */

	IPW2100_DEV_ID(0x2560),	/* DE 2100A mPCI 3A */
	IPW2100_DEV_ID(0x2562),	/* DE 2100A mPCI 3A */
	IPW2100_DEV_ID(0x2563),	/* DE 2100A mPCI 3A */
	IPW2100_DEV_ID(0x2561),	/* DE 2100 mPCI 3A */
	IPW2100_DEV_ID(0x2565),	/* DE 2100 mPCI 3A */
	IPW2100_DEV_ID(0x2566),	/* DE 2100 mPCI 3A */
	IPW2100_DEV_ID(0x2567),	/* DE 2100 mPCI 3A */

	IPW2100_DEV_ID(0x2570),	/* GA 2100 mPCI 3B */

	IPW2100_DEV_ID(0x2580),	/* TO 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2582),	/* TO 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2583),	/* TO 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2581),	/* TO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2585),	/* TO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2586),	/* TO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2587),	/* TO 2100 mPCI 3B */

	IPW2100_DEV_ID(0x2590),	/* SO 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2592),	/* SO 2100A mPCI 3B */
	IPW2100_DEV_ID(0x2591),	/* SO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2593),	/* SO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2596),	/* SO 2100 mPCI 3B */
	IPW2100_DEV_ID(0x2598),	/* SO 2100 mPCI 3B */

	IPW2100_DEV_ID(0x25A0),	/* HP 2100 mPCI 3B */
	{0,},
};

MODULE_DEVICE_TABLE(pci, ipw2100_pci_id_table);

static struct pci_driver ipw2100_pci_driver = {
	.name = DRV_NAME,
	.id_table = ipw2100_pci_id_table,
	.probe = ipw2100_pci_init_one,
	.remove = ipw2100_pci_remove_one,
#ifdef CONFIG_PM
	.suspend = ipw2100_suspend,
	.resume = ipw2100_resume,
#endif
	.shutdown = ipw2100_shutdown,
};

/**
 * Initialize the ipw2100 driver/module
 *
 * @returns 0 if ok, < 0 errno node con error.
 *
 * Note: we cannot init the /proc stuff until the PCI driver is there,
 * or we risk an unlikely race condition on someone accessing
 * uninitialized data in the PCI dev struct through /proc.
 */
static int __init ipw2100_init(void)
{
	int ret;

	printk(KERN_INFO DRV_NAME ": %s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	printk(KERN_INFO DRV_NAME ": %s\n", DRV_COPYRIGHT);

	pm_qos_add_request(&ipw2100_pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	ret = pci_register_driver(&ipw2100_pci_driver);
	if (ret)
		goto out;

#ifdef CONFIG_IPW2100_DEBUG
	ipw2100_debug_level = debug;
	ret = driver_create_file(&ipw2100_pci_driver.driver,
				 &driver_attr_debug_level);
#endif

out:
	return ret;
}

/**
 * Cleanup ipw2100 driver registration
 */
static void __exit ipw2100_exit(void)
{
	/* FIXME: IPG: check that we have no instances of the devices open */
#ifdef CONFIG_IPW2100_DEBUG
	driver_remove_file(&ipw2100_pci_driver.driver,
			   &driver_attr_debug_level);
#endif
	pci_unregister_driver(&ipw2100_pci_driver);
	pm_qos_remove_request(&ipw2100_pm_qos_req);
}

module_init(ipw2100_init);
module_exit(ipw2100_exit);

static int ipw2100_wx_get_name(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	if (!(priv->status & STATUS_ASSOCIATED))
		strcpy(wrqu->name, "unassociated");
	else
		snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");

	IPW_DEBUG_WX("Name: %s\n", wrqu->name);
	return 0;
}

static int ipw2100_wx_set_freq(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct iw_freq *fwrq = &wrqu->freq;
	int err = 0;

	if (priv->ieee->iw_mode == IW_MODE_INFRA)
		return -EOPNOTSUPP;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	/* if setting by freq convert to channel */
	if (fwrq->e == 1) {
		if ((fwrq->m >= (int)2.412e8 && fwrq->m <= (int)2.487e8)) {
			int f = fwrq->m / 100000;
			int c = 0;

			while ((c < REG_MAX_CHANNEL) &&
			       (f != ipw2100_frequencies[c]))
				c++;

			/* hack to fall through */
			fwrq->e = 0;
			fwrq->m = c + 1;
		}
	}

	if (fwrq->e > 0 || fwrq->m > 1000) {
		err = -EOPNOTSUPP;
		goto done;
	} else {		/* Set the channel */
		IPW_DEBUG_WX("SET Freq/Channel -> %d\n", fwrq->m);
		err = ipw2100_set_channel(priv, fwrq->m, 0);
	}

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_freq(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->freq.e = 0;

	/* If we are associated, trying to associate, or have a statically
	 * configured CHANNEL then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_CHANNEL ||
	    priv->status & STATUS_ASSOCIATED)
		wrqu->freq.m = priv->channel;
	else
		wrqu->freq.m = 0;

	IPW_DEBUG_WX("GET Freq/Channel -> %d\n", priv->channel);
	return 0;

}

static int ipw2100_wx_set_mode(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0;

	IPW_DEBUG_WX("SET Mode -> %d\n", wrqu->mode);

	if (wrqu->mode == priv->ieee->iw_mode)
		return 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	switch (wrqu->mode) {
#ifdef CONFIG_IPW2100_MONITOR
	case IW_MODE_MONITOR:
		err = ipw2100_switch_mode(priv, IW_MODE_MONITOR);
		break;
#endif				/* CONFIG_IPW2100_MONITOR */
	case IW_MODE_ADHOC:
		err = ipw2100_switch_mode(priv, IW_MODE_ADHOC);
		break;
	case IW_MODE_INFRA:
	case IW_MODE_AUTO:
	default:
		err = ipw2100_switch_mode(priv, IW_MODE_INFRA);
		break;
	}

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_mode(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->mode = priv->ieee->iw_mode;
	IPW_DEBUG_WX("GET Mode -> %d\n", wrqu->mode);

	return 0;
}

#define POWER_MODES 5

/* Values are in microsecond */
static const s32 timeout_duration[POWER_MODES] = {
	350000,
	250000,
	75000,
	37000,
	25000,
};

static const s32 period_duration[POWER_MODES] = {
	400000,
	700000,
	1000000,
	1000000,
	1000000
};

static int ipw2100_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	struct iw_range *range = (struct iw_range *)extra;
	u16 val;
	int i, level;

	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* Let's try to keep this struct in the same order as in
	 * linux/include/wireless.h
	 */

	/* TODO: See what values we can set, and remove the ones we can't
	 * set, or fill them with some default data.
	 */

	/* ~5 Mb/s real (802.11b) */
	range->throughput = 5 * 1000 * 1000;

//      range->sensitivity;     /* signal level threshold range */

	range->max_qual.qual = 100;
	/* TODO: Find real max RSSI and stick here */
	range->max_qual.level = 0;
	range->max_qual.noise = 0;
	range->max_qual.updated = 7;	/* Updated all three */

	range->avg_qual.qual = 70;	/* > 8% missed beacons is 'bad' */
	/* TODO: Find real 'good' to 'bad' threshold value for RSSI */
	range->avg_qual.level = 20 + IPW2100_RSSI_TO_DBM;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7;	/* Updated all three */

	range->num_bitrates = RATE_COUNT;

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++) {
		range->bitrate[i] = ipw2100_bg_rates[i].bitrate * 100 * 1000;
	}

	range->min_rts = MIN_RTS_THRESHOLD;
	range->max_rts = MAX_RTS_THRESHOLD;
	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->min_pmp = period_duration[0];	/* Minimal PM period */
	range->max_pmp = period_duration[POWER_MODES - 1];	/* Maximal PM period */
	range->min_pmt = timeout_duration[POWER_MODES - 1];	/* Minimal PM timeout */
	range->max_pmt = timeout_duration[0];	/* Maximal PM timeout */

	/* How to decode max/min PM period */
	range->pmp_flags = IW_POWER_PERIOD;
	/* How to decode max/min PM period */
	range->pmt_flags = IW_POWER_TIMEOUT;
	/* What PM options are supported */
	range->pm_capa = IW_POWER_TIMEOUT | IW_POWER_PERIOD;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;	/* Different token sizes */
	range->num_encoding_sizes = 2;	/* Number of entry in the list */
	range->max_encoding_tokens = WEP_KEYS;	/* Max number of tokens */
//      range->encoding_login_index;            /* token index for login token */

	if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
		range->txpower_capa = IW_TXPOW_DBM;
		range->num_txpower = IW_MAX_TXPOWER;
		for (i = 0, level = (IPW_TX_POWER_MAX_DBM * 16);
		     i < IW_MAX_TXPOWER;
		     i++, level -=
		     ((IPW_TX_POWER_MAX_DBM -
		       IPW_TX_POWER_MIN_DBM) * 16) / (IW_MAX_TXPOWER - 1))
			range->txpower[i] = level / 16;
	} else {
		range->txpower_capa = 0;
		range->num_txpower = 0;
	}

	/* Set the Wireless Extension versions */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 18;

//      range->retry_capa;      /* What retry options are supported */
//      range->retry_flags;     /* How to decode max/min retry limit */
//      range->r_time_flags;    /* How to decode max/min retry life */
//      range->min_retry;       /* Minimal number of retries */
//      range->max_retry;       /* Maximal number of retries */
//      range->min_r_time;      /* Minimal retry lifetime */
//      range->max_r_time;      /* Maximal retry lifetime */

	range->num_channels = FREQ_COUNT;

	val = 0;
	for (i = 0; i < FREQ_COUNT; i++) {
		// TODO: Include only legal frequencies for some countries
//              if (local->channel_mask & (1 << i)) {
		range->freq[val].i = i + 1;
		range->freq[val].m = ipw2100_frequencies[i] * 100000;
		range->freq[val].e = 1;
		val++;
//              }
		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;

	/* Event capability (kernel + driver) */
	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
		IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	IPW_DEBUG_WX("GET Range\n");

	return 0;
}

static int ipw2100_wx_set_wap(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0;

	// sanity checks
	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		return -EINVAL;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (is_broadcast_ether_addr(wrqu->ap_addr.sa_data) ||
	    is_zero_ether_addr(wrqu->ap_addr.sa_data)) {
		/* we disable mandatory BSSID association */
		IPW_DEBUG_WX("exit - disable mandatory BSSID\n");
		priv->config &= ~CFG_STATIC_BSSID;
		err = ipw2100_set_mandatory_bssid(priv, NULL, 0);
		goto done;
	}

	priv->config |= CFG_STATIC_BSSID;
	memcpy(priv->mandatory_bssid_mac, wrqu->ap_addr.sa_data, ETH_ALEN);

	err = ipw2100_set_mandatory_bssid(priv, wrqu->ap_addr.sa_data, 0);

	IPW_DEBUG_WX("SET BSSID -> %pM\n", wrqu->ap_addr.sa_data);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_wap(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	/* If we are associated, trying to associate, or have a statically
	 * configured BSSID then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_BSSID || priv->status & STATUS_ASSOCIATED) {
		wrqu->ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(wrqu->ap_addr.sa_data, priv->bssid, ETH_ALEN);
	} else
		memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

	IPW_DEBUG_WX("Getting WAP BSSID: %pM\n", wrqu->ap_addr.sa_data);
	return 0;
}

static int ipw2100_wx_set_essid(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	char *essid = "";	/* ANY */
	int length = 0;
	int err = 0;
	DECLARE_SSID_BUF(ssid);

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (wrqu->essid.flags && wrqu->essid.length) {
		length = wrqu->essid.length;
		essid = extra;
	}

	if (length == 0) {
		IPW_DEBUG_WX("Setting ESSID to ANY\n");
		priv->config &= ~CFG_STATIC_ESSID;
		err = ipw2100_set_essid(priv, NULL, 0, 0);
		goto done;
	}

	length = min(length, IW_ESSID_MAX_SIZE);

	priv->config |= CFG_STATIC_ESSID;

	if (priv->essid_len == length && !memcmp(priv->essid, extra, length)) {
		IPW_DEBUG_WX("ESSID set to current ESSID.\n");
		err = 0;
		goto done;
	}

	IPW_DEBUG_WX("Setting ESSID: '%s' (%d)\n",
		     print_ssid(ssid, essid, length), length);

	priv->essid_len = length;
	memcpy(priv->essid, essid, priv->essid_len);

	err = ipw2100_set_essid(priv, essid, length, 0);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_essid(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	DECLARE_SSID_BUF(ssid);

	/* If we are associated, trying to associate, or have a statically
	 * configured ESSID then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_ESSID || priv->status & STATUS_ASSOCIATED) {
		IPW_DEBUG_WX("Getting essid: '%s'\n",
			     print_ssid(ssid, priv->essid, priv->essid_len));
		memcpy(extra, priv->essid, priv->essid_len);
		wrqu->essid.length = priv->essid_len;
		wrqu->essid.flags = 1;	/* active */
	} else {
		IPW_DEBUG_WX("Getting essid: ANY\n");
		wrqu->essid.length = 0;
		wrqu->essid.flags = 0;	/* active */
	}

	return 0;
}

static int ipw2100_wx_set_nick(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	if (wrqu->data.length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	wrqu->data.length = min((size_t) wrqu->data.length, sizeof(priv->nick));
	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, extra, wrqu->data.length);

	IPW_DEBUG_WX("SET Nickname -> %s\n", priv->nick);

	return 0;
}

static int ipw2100_wx_get_nick(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->data.length = strlen(priv->nick);
	memcpy(extra, priv->nick, wrqu->data.length);
	wrqu->data.flags = 1;	/* active */

	IPW_DEBUG_WX("GET Nickname -> %s\n", extra);

	return 0;
}

static int ipw2100_wx_set_rate(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	u32 target_rate = wrqu->bitrate.value;
	u32 rate;
	int err = 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	rate = 0;

	if (target_rate == 1000000 ||
	    (!wrqu->bitrate.fixed && target_rate > 1000000))
		rate |= TX_RATE_1_MBIT;
	if (target_rate == 2000000 ||
	    (!wrqu->bitrate.fixed && target_rate > 2000000))
		rate |= TX_RATE_2_MBIT;
	if (target_rate == 5500000 ||
	    (!wrqu->bitrate.fixed && target_rate > 5500000))
		rate |= TX_RATE_5_5_MBIT;
	if (target_rate == 11000000 ||
	    (!wrqu->bitrate.fixed && target_rate > 11000000))
		rate |= TX_RATE_11_MBIT;
	if (rate == 0)
		rate = DEFAULT_TX_RATES;

	err = ipw2100_set_tx_rates(priv, rate, 0);

	IPW_DEBUG_WX("SET Rate -> %04X\n", rate);
      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_rate(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int val;
	unsigned int len = sizeof(val);
	int err = 0;

	if (!(priv->status & STATUS_ENABLED) ||
	    priv->status & STATUS_RF_KILL_MASK ||
	    !(priv->status & STATUS_ASSOCIATED)) {
		wrqu->bitrate.value = 0;
		return 0;
	}

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	err = ipw2100_get_ordinal(priv, IPW_ORD_CURRENT_TX_RATE, &val, &len);
	if (err) {
		IPW_DEBUG_WX("failed querying ordinals.\n");
		goto done;
	}

	switch (val & TX_RATE_MASK) {
	case TX_RATE_1_MBIT:
		wrqu->bitrate.value = 1000000;
		break;
	case TX_RATE_2_MBIT:
		wrqu->bitrate.value = 2000000;
		break;
	case TX_RATE_5_5_MBIT:
		wrqu->bitrate.value = 5500000;
		break;
	case TX_RATE_11_MBIT:
		wrqu->bitrate.value = 11000000;
		break;
	default:
		wrqu->bitrate.value = 0;
	}

	IPW_DEBUG_WX("GET Rate -> %d\n", wrqu->bitrate.value);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_set_rts(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int value, err;

	/* Auto RTS not yet supported */
	if (wrqu->rts.fixed == 0)
		return -EINVAL;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (wrqu->rts.disabled)
		value = priv->rts_threshold | RTS_DISABLED;
	else {
		if (wrqu->rts.value < 1 || wrqu->rts.value > 2304) {
			err = -EINVAL;
			goto done;
		}
		value = wrqu->rts.value;
	}

	err = ipw2100_set_rts_threshold(priv, value);

	IPW_DEBUG_WX("SET RTS Threshold -> 0x%08X\n", value);
      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_rts(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->rts.value = priv->rts_threshold & ~RTS_DISABLED;
	wrqu->rts.fixed = 1;	/* no auto select */

	/* If RTS is set to the default value, then it is disabled */
	wrqu->rts.disabled = (priv->rts_threshold & RTS_DISABLED) ? 1 : 0;

	IPW_DEBUG_WX("GET RTS Threshold -> 0x%08X\n", wrqu->rts.value);

	return 0;
}

static int ipw2100_wx_set_txpow(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0, value;
	
	if (ipw_radio_kill_sw(priv, wrqu->txpower.disabled))
		return -EINPROGRESS;

	if (priv->ieee->iw_mode != IW_MODE_ADHOC)
		return 0;

	if ((wrqu->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM)
		return -EINVAL;

	if (wrqu->txpower.fixed == 0)
		value = IPW_TX_POWER_DEFAULT;
	else {
		if (wrqu->txpower.value < IPW_TX_POWER_MIN_DBM ||
		    wrqu->txpower.value > IPW_TX_POWER_MAX_DBM)
			return -EINVAL;

		value = wrqu->txpower.value;
	}

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	err = ipw2100_set_tx_power(priv, value);

	IPW_DEBUG_WX("SET TX Power -> %d\n", value);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_txpow(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->txpower.disabled = (priv->status & STATUS_RF_KILL_MASK) ? 1 : 0;

	if (priv->tx_power == IPW_TX_POWER_DEFAULT) {
		wrqu->txpower.fixed = 0;
		wrqu->txpower.value = IPW_TX_POWER_MAX_DBM;
	} else {
		wrqu->txpower.fixed = 1;
		wrqu->txpower.value = priv->tx_power;
	}

	wrqu->txpower.flags = IW_TXPOW_DBM;

	IPW_DEBUG_WX("GET TX Power -> %d\n", wrqu->txpower.value);

	return 0;
}

static int ipw2100_wx_set_frag(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	if (!wrqu->frag.fixed)
		return -EINVAL;

	if (wrqu->frag.disabled) {
		priv->frag_threshold |= FRAG_DISABLED;
		priv->ieee->fts = DEFAULT_FTS;
	} else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		priv->ieee->fts = wrqu->frag.value & ~0x1;
		priv->frag_threshold = priv->ieee->fts;
	}

	IPW_DEBUG_WX("SET Frag Threshold -> %d\n", priv->ieee->fts);

	return 0;
}

static int ipw2100_wx_get_frag(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	wrqu->frag.value = priv->frag_threshold & ~FRAG_DISABLED;
	wrqu->frag.fixed = 0;	/* no auto select */
	wrqu->frag.disabled = (priv->frag_threshold & FRAG_DISABLED) ? 1 : 0;

	IPW_DEBUG_WX("GET Frag Threshold -> %d\n", wrqu->frag.value);

	return 0;
}

static int ipw2100_wx_set_retry(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0;

	if (wrqu->retry.flags & IW_RETRY_LIFETIME || wrqu->retry.disabled)
		return -EINVAL;

	if (!(wrqu->retry.flags & IW_RETRY_LIMIT))
		return 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (wrqu->retry.flags & IW_RETRY_SHORT) {
		err = ipw2100_set_short_retry(priv, wrqu->retry.value);
		IPW_DEBUG_WX("SET Short Retry Limit -> %d\n",
			     wrqu->retry.value);
		goto done;
	}

	if (wrqu->retry.flags & IW_RETRY_LONG) {
		err = ipw2100_set_long_retry(priv, wrqu->retry.value);
		IPW_DEBUG_WX("SET Long Retry Limit -> %d\n",
			     wrqu->retry.value);
		goto done;
	}

	err = ipw2100_set_short_retry(priv, wrqu->retry.value);
	if (!err)
		err = ipw2100_set_long_retry(priv, wrqu->retry.value);

	IPW_DEBUG_WX("SET Both Retry Limits -> %d\n", wrqu->retry.value);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_retry(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	wrqu->retry.disabled = 0;	/* can't be disabled */

	if ((wrqu->retry.flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME)
		return -EINVAL;

	if (wrqu->retry.flags & IW_RETRY_LONG) {
		wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
		wrqu->retry.value = priv->long_retry_limit;
	} else {
		wrqu->retry.flags =
		    (priv->short_retry_limit !=
		     priv->long_retry_limit) ?
		    IW_RETRY_LIMIT | IW_RETRY_SHORT : IW_RETRY_LIMIT;

		wrqu->retry.value = priv->short_retry_limit;
	}

	IPW_DEBUG_WX("GET Retry -> %d\n", wrqu->retry.value);

	return 0;
}

static int ipw2100_wx_set_scan(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	IPW_DEBUG_WX("Initiating scan...\n");

	priv->user_requested_scan = 1;
	if (ipw2100_set_scan_options(priv) || ipw2100_start_scan(priv)) {
		IPW_DEBUG_WX("Start scan failed.\n");

		/* TODO: Mark a scan as pending so when hardware initialized
		 *       a scan starts */
	}

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_scan(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	return libipw_wx_get_scan(priv->ieee, info, wrqu, extra);
}

/*
 * Implementation based on code in hostap-driver v0.1.3 hostap_ioctl.c
 */
static int ipw2100_wx_set_encode(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key)
{
	/*
	 * No check of STATUS_INITIALIZED required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	return libipw_wx_set_encode(priv->ieee, info, wrqu, key);
}

static int ipw2100_wx_get_encode(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	return libipw_wx_get_encode(priv->ieee, info, wrqu, key);
}

static int ipw2100_wx_set_power(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (wrqu->power.disabled) {
		priv->power_mode = IPW_POWER_LEVEL(priv->power_mode);
		err = ipw2100_set_power_mode(priv, IPW_POWER_MODE_CAM);
		IPW_DEBUG_WX("SET Power Management Mode -> off\n");
		goto done;
	}

	switch (wrqu->power.flags & IW_POWER_MODE) {
	case IW_POWER_ON:	/* If not specified */
	case IW_POWER_MODE:	/* If set all mask */
	case IW_POWER_ALL_R:	/* If explicitly state all */
		break;
	default:		/* Otherwise we don't support it */
		IPW_DEBUG_WX("SET PM Mode: %X not supported.\n",
			     wrqu->power.flags);
		err = -EOPNOTSUPP;
		goto done;
	}

	/* If the user hasn't specified a power management mode yet, default
	 * to BATTERY */
	priv->power_mode = IPW_POWER_ENABLED | priv->power_mode;
	err = ipw2100_set_power_mode(priv, IPW_POWER_LEVEL(priv->power_mode));

	IPW_DEBUG_WX("SET Power Management Mode -> 0x%02X\n", priv->power_mode);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;

}

static int ipw2100_wx_get_power(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	if (!(priv->power_mode & IPW_POWER_ENABLED))
		wrqu->power.disabled = 1;
	else {
		wrqu->power.disabled = 0;
		wrqu->power.flags = 0;
	}

	IPW_DEBUG_WX("GET Power Management Mode -> %02X\n", priv->power_mode);

	return 0;
}

/*
 * WE-18 WPA support
 */

/* SIOCSIWGENIE */
static int ipw2100_wx_set_genie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{

	struct ipw2100_priv *priv = libipw_priv(dev);
	struct libipw_device *ieee = priv->ieee;
	u8 *buf;

	if (!ieee->wpa_enabled)
		return -EOPNOTSUPP;

	if (wrqu->data.length > MAX_WPA_IE_LEN ||
	    (wrqu->data.length && extra == NULL))
		return -EINVAL;

	if (wrqu->data.length) {
		buf = kmemdup(extra, wrqu->data.length, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		kfree(ieee->wpa_ie);
		ieee->wpa_ie = buf;
		ieee->wpa_ie_len = wrqu->data.length;
	} else {
		kfree(ieee->wpa_ie);
		ieee->wpa_ie = NULL;
		ieee->wpa_ie_len = 0;
	}

	ipw2100_wpa_assoc_frame(priv, ieee->wpa_ie, ieee->wpa_ie_len);

	return 0;
}

/* SIOCGIWGENIE */
static int ipw2100_wx_get_genie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct libipw_device *ieee = priv->ieee;

	if (ieee->wpa_ie_len == 0 || ieee->wpa_ie == NULL) {
		wrqu->data.length = 0;
		return 0;
	}

	if (wrqu->data.length < ieee->wpa_ie_len)
		return -E2BIG;

	wrqu->data.length = ieee->wpa_ie_len;
	memcpy(extra, ieee->wpa_ie, ieee->wpa_ie_len);

	return 0;
}

/* SIOCSIWAUTH */
static int ipw2100_wx_set_auth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct libipw_device *ieee = priv->ieee;
	struct iw_param *param = &wrqu->param;
	struct lib80211_crypt_data *crypt;
	unsigned long flags;
	int ret = 0;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
		 * ipw2200 does not use these parameters
		 */
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		crypt = priv->ieee->crypt_info.crypt[priv->ieee->crypt_info.tx_keyidx];
		if (!crypt || !crypt->ops->set_flags || !crypt->ops->get_flags)
			break;

		flags = crypt->ops->get_flags(crypt->priv);

		if (param->value)
			flags |= IEEE80211_CRYPTO_TKIP_COUNTERMEASURES;
		else
			flags &= ~IEEE80211_CRYPTO_TKIP_COUNTERMEASURES;

		crypt->ops->set_flags(flags, crypt->priv);

		break;

	case IW_AUTH_DROP_UNENCRYPTED:{
			/* HACK:
			 *
			 * wpa_supplicant calls set_wpa_enabled when the driver
			 * is loaded and unloaded, regardless of if WPA is being
			 * used.  No other calls are made which can be used to
			 * determine if encryption will be used or not prior to
			 * association being expected.  If encryption is not being
			 * used, drop_unencrypted is set to false, else true -- we
			 * can use this to determine if the CAP_PRIVACY_ON bit should
			 * be set.
			 */
			struct libipw_security sec = {
				.flags = SEC_ENABLED,
				.enabled = param->value,
			};
			priv->ieee->drop_unencrypted = param->value;
			/* We only change SEC_LEVEL for open mode. Others
			 * are set by ipw_wpa_set_encryption.
			 */
			if (!param->value) {
				sec.flags |= SEC_LEVEL;
				sec.level = SEC_LEVEL_0;
			} else {
				sec.flags |= SEC_LEVEL;
				sec.level = SEC_LEVEL_1;
			}
			if (priv->ieee->set_security)
				priv->ieee->set_security(priv->ieee->dev, &sec);
			break;
		}

	case IW_AUTH_80211_AUTH_ALG:
		ret = ipw2100_wpa_set_auth_algs(priv, param->value);
		break;

	case IW_AUTH_WPA_ENABLED:
		ret = ipw2100_wpa_enable(priv, param->value);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		ieee->ieee802_1x = param->value;
		break;

		//case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
		ieee->privacy_invoked = param->value;
		break;

	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

/* SIOCGIWAUTH */
static int ipw2100_wx_get_auth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct libipw_device *ieee = priv->ieee;
	struct lib80211_crypt_data *crypt;
	struct iw_param *param = &wrqu->param;
	int ret = 0;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
		 * wpa_supplicant will control these internally
		 */
		ret = -EOPNOTSUPP;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		crypt = priv->ieee->crypt_info.crypt[priv->ieee->crypt_info.tx_keyidx];
		if (!crypt || !crypt->ops->get_flags) {
			IPW_DEBUG_WARNING("Can't get TKIP countermeasures: "
					  "crypt not set!\n");
			break;
		}

		param->value = (crypt->ops->get_flags(crypt->priv) &
				IEEE80211_CRYPTO_TKIP_COUNTERMEASURES) ? 1 : 0;

		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		param->value = ieee->drop_unencrypted;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		param->value = priv->ieee->sec.auth_mode;
		break;

	case IW_AUTH_WPA_ENABLED:
		param->value = ieee->wpa_enabled;
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		param->value = ieee->ieee802_1x;
		break;

	case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
		param->value = ieee->privacy_invoked;
		break;

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/* SIOCSIWENCODEEXT */
static int ipw2100_wx_set_encodeext(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	return libipw_wx_set_encodeext(priv->ieee, info, wrqu, extra);
}

/* SIOCGIWENCODEEXT */
static int ipw2100_wx_get_encodeext(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	return libipw_wx_get_encodeext(priv->ieee, info, wrqu, extra);
}

/* SIOCSIWMLME */
static int ipw2100_wx_set_mlme(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	__le16 reason;

	reason = cpu_to_le16(mlme->reason_code);

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		// silently ignore
		break;

	case IW_MLME_DISASSOC:
		ipw2100_disassociate_bssid(priv);
		break;

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/*
 *
 * IWPRIV handlers
 *
 */
#ifdef CONFIG_IPW2100_MONITOR
static int ipw2100_wx_set_promisc(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int *parms = (int *)extra;
	int enable = (parms[0] > 0);
	int err = 0;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (enable) {
		if (priv->ieee->iw_mode == IW_MODE_MONITOR) {
			err = ipw2100_set_channel(priv, parms[1], 0);
			goto done;
		}
		priv->channel = parms[1];
		err = ipw2100_switch_mode(priv, IW_MODE_MONITOR);
	} else {
		if (priv->ieee->iw_mode == IW_MODE_MONITOR)
			err = ipw2100_switch_mode(priv, priv->last_mode);
	}
      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_reset(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	if (priv->status & STATUS_INITIALIZED)
		schedule_reset(priv);
	return 0;
}

#endif

static int ipw2100_wx_set_powermode(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err = 0, mode = *(int *)extra;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if ((mode < 0) || (mode > POWER_MODES))
		mode = IPW_POWER_AUTO;

	if (IPW_POWER_LEVEL(priv->power_mode) != mode)
		err = ipw2100_set_power_mode(priv, mode);
      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

#define MAX_POWER_STRING 80
static int ipw2100_wx_get_powermode(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);
	int level = IPW_POWER_LEVEL(priv->power_mode);
	s32 timeout, period;

	if (!(priv->power_mode & IPW_POWER_ENABLED)) {
		snprintf(extra, MAX_POWER_STRING,
			 "Power save level: %d (Off)", level);
	} else {
		switch (level) {
		case IPW_POWER_MODE_CAM:
			snprintf(extra, MAX_POWER_STRING,
				 "Power save level: %d (None)", level);
			break;
		case IPW_POWER_AUTO:
			snprintf(extra, MAX_POWER_STRING,
				 "Power save level: %d (Auto)", level);
			break;
		default:
			timeout = timeout_duration[level - 1] / 1000;
			period = period_duration[level - 1] / 1000;
			snprintf(extra, MAX_POWER_STRING,
				 "Power save level: %d "
				 "(Timeout %dms, Period %dms)",
				 level, timeout, period);
		}
	}

	wrqu->data.length = strlen(extra) + 1;

	return 0;
}

static int ipw2100_wx_set_preamble(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err, mode = *(int *)extra;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (mode == 1)
		priv->config |= CFG_LONG_PREAMBLE;
	else if (mode == 0)
		priv->config &= ~CFG_LONG_PREAMBLE;
	else {
		err = -EINVAL;
		goto done;
	}

	err = ipw2100_system_config(priv, 0);

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_preamble(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	if (priv->config & CFG_LONG_PREAMBLE)
		snprintf(wrqu->name, IFNAMSIZ, "long (1)");
	else
		snprintf(wrqu->name, IFNAMSIZ, "auto (0)");

	return 0;
}

#ifdef CONFIG_IPW2100_MONITOR
static int ipw2100_wx_set_crc_check(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw2100_priv *priv = libipw_priv(dev);
	int err, mode = *(int *)extra;

	mutex_lock(&priv->action_mutex);
	if (!(priv->status & STATUS_INITIALIZED)) {
		err = -EIO;
		goto done;
	}

	if (mode == 1)
		priv->config |= CFG_CRC_CHECK;
	else if (mode == 0)
		priv->config &= ~CFG_CRC_CHECK;
	else {
		err = -EINVAL;
		goto done;
	}
	err = 0;

      done:
	mutex_unlock(&priv->action_mutex);
	return err;
}

static int ipw2100_wx_get_crc_check(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	/*
	 * This can be called at any time.  No action lock required
	 */

	struct ipw2100_priv *priv = libipw_priv(dev);

	if (priv->config & CFG_CRC_CHECK)
		snprintf(wrqu->name, IFNAMSIZ, "CRC checked (1)");
	else
		snprintf(wrqu->name, IFNAMSIZ, "CRC ignored (0)");

	return 0;
}
#endif				/* CONFIG_IPW2100_MONITOR */

static iw_handler ipw2100_wx_handlers[] = {
	IW_HANDLER(SIOCGIWNAME, ipw2100_wx_get_name),
	IW_HANDLER(SIOCSIWFREQ, ipw2100_wx_set_freq),
	IW_HANDLER(SIOCGIWFREQ, ipw2100_wx_get_freq),
	IW_HANDLER(SIOCSIWMODE, ipw2100_wx_set_mode),
	IW_HANDLER(SIOCGIWMODE, ipw2100_wx_get_mode),
	IW_HANDLER(SIOCGIWRANGE, ipw2100_wx_get_range),
	IW_HANDLER(SIOCSIWAP, ipw2100_wx_set_wap),
	IW_HANDLER(SIOCGIWAP, ipw2100_wx_get_wap),
	IW_HANDLER(SIOCSIWMLME, ipw2100_wx_set_mlme),
	IW_HANDLER(SIOCSIWSCAN, ipw2100_wx_set_scan),
	IW_HANDLER(SIOCGIWSCAN, ipw2100_wx_get_scan),
	IW_HANDLER(SIOCSIWESSID, ipw2100_wx_set_essid),
	IW_HANDLER(SIOCGIWESSID, ipw2100_wx_get_essid),
	IW_HANDLER(SIOCSIWNICKN, ipw2100_wx_set_nick),
	IW_HANDLER(SIOCGIWNICKN, ipw2100_wx_get_nick),
	IW_HANDLER(SIOCSIWRATE, ipw2100_wx_set_rate),
	IW_HANDLER(SIOCGIWRATE, ipw2100_wx_get_rate),
	IW_HANDLER(SIOCSIWRTS, ipw2100_wx_set_rts),
	IW_HANDLER(SIOCGIWRTS, ipw2100_wx_get_rts),
	IW_HANDLER(SIOCSIWFRAG, ipw2100_wx_set_frag),
	IW_HANDLER(SIOCGIWFRAG, ipw2100_wx_get_frag),
	IW_HANDLER(SIOCSIWTXPOW, ipw2100_wx_set_txpow),
	IW_HANDLER(SIOCGIWTXPOW, ipw2100_wx_get_txpow),
	IW_HANDLER(SIOCSIWRETRY, ipw2100_wx_set_retry),
	IW_HANDLER(SIOCGIWRETRY, ipw2100_wx_get_retry),
	IW_HANDLER(SIOCSIWENCODE, ipw2100_wx_set_encode),
	IW_HANDLER(SIOCGIWENCODE, ipw2100_wx_get_encode),
	IW_HANDLER(SIOCSIWPOWER, ipw2100_wx_set_power),
	IW_HANDLER(SIOCGIWPOWER, ipw2100_wx_get_power),
	IW_HANDLER(SIOCSIWGENIE, ipw2100_wx_set_genie),
	IW_HANDLER(SIOCGIWGENIE, ipw2100_wx_get_genie),
	IW_HANDLER(SIOCSIWAUTH, ipw2100_wx_set_auth),
	IW_HANDLER(SIOCGIWAUTH, ipw2100_wx_get_auth),
	IW_HANDLER(SIOCSIWENCODEEXT, ipw2100_wx_set_encodeext),
	IW_HANDLER(SIOCGIWENCODEEXT, ipw2100_wx_get_encodeext),
};

#define IPW2100_PRIV_SET_MONITOR	SIOCIWFIRSTPRIV
#define IPW2100_PRIV_RESET		SIOCIWFIRSTPRIV+1
#define IPW2100_PRIV_SET_POWER		SIOCIWFIRSTPRIV+2
#define IPW2100_PRIV_GET_POWER		SIOCIWFIRSTPRIV+3
#define IPW2100_PRIV_SET_LONGPREAMBLE	SIOCIWFIRSTPRIV+4
#define IPW2100_PRIV_GET_LONGPREAMBLE	SIOCIWFIRSTPRIV+5
#define IPW2100_PRIV_SET_CRC_CHECK	SIOCIWFIRSTPRIV+6
#define IPW2100_PRIV_GET_CRC_CHECK	SIOCIWFIRSTPRIV+7

static const struct iw_priv_args ipw2100_private_args[] = {

#ifdef CONFIG_IPW2100_MONITOR
	{
	 IPW2100_PRIV_SET_MONITOR,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "monitor"},
	{
	 IPW2100_PRIV_RESET,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 0, 0, "reset"},
#endif				/* CONFIG_IPW2100_MONITOR */

	{
	 IPW2100_PRIV_SET_POWER,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_power"},
	{
	 IPW2100_PRIV_GET_POWER,
	 0, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_POWER_STRING,
	 "get_power"},
	{
	 IPW2100_PRIV_SET_LONGPREAMBLE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_preamble"},
	{
	 IPW2100_PRIV_GET_LONGPREAMBLE,
	 0, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "get_preamble"},
#ifdef CONFIG_IPW2100_MONITOR
	{
	 IPW2100_PRIV_SET_CRC_CHECK,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_crc_check"},
	{
	 IPW2100_PRIV_GET_CRC_CHECK,
	 0, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "get_crc_check"},
#endif				/* CONFIG_IPW2100_MONITOR */
};

static iw_handler ipw2100_private_handler[] = {
#ifdef CONFIG_IPW2100_MONITOR
	ipw2100_wx_set_promisc,
	ipw2100_wx_reset,
#else				/* CONFIG_IPW2100_MONITOR */
	NULL,
	NULL,
#endif				/* CONFIG_IPW2100_MONITOR */
	ipw2100_wx_set_powermode,
	ipw2100_wx_get_powermode,
	ipw2100_wx_set_preamble,
	ipw2100_wx_get_preamble,
#ifdef CONFIG_IPW2100_MONITOR
	ipw2100_wx_set_crc_check,
	ipw2100_wx_get_crc_check,
#else				/* CONFIG_IPW2100_MONITOR */
	NULL,
	NULL,
#endif				/* CONFIG_IPW2100_MONITOR */
};

/*
 * Get wireless statistics.
 * Called by /proc/net/wireless
 * Also called by SIOCGIWSTATS
 */
static struct iw_statistics *ipw2100_wx_wireless_stats(struct net_device *dev)
{
	enum {
		POOR = 30,
		FAIR = 60,
		GOOD = 80,
		VERY_GOOD = 90,
		EXCELLENT = 95,
		PERFECT = 100
	};
	int rssi_qual;
	int tx_qual;
	int beacon_qual;
	int quality;

	struct ipw2100_priv *priv = libipw_priv(dev);
	struct iw_statistics *wstats;
	u32 rssi, tx_retries, missed_beacons, tx_failures;
	u32 ord_len = sizeof(u32);

	if (!priv)
		return (struct iw_statistics *)NULL;

	wstats = &priv->wstats;

	/* if hw is disabled, then ipw2100_get_ordinal() can't be called.
	 * ipw2100_wx_wireless_stats seems to be called before fw is
	 * initialized.  STATUS_ASSOCIATED will only be set if the hw is up
	 * and associated; if not associcated, the values are all meaningless
	 * anyway, so set them all to NULL and INVALID */
	if (!(priv->status & STATUS_ASSOCIATED)) {
		wstats->miss.beacon = 0;
		wstats->discard.retries = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = 7;
		wstats->qual.updated |= IW_QUAL_NOISE_INVALID |
		    IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_INVALID;
		return wstats;
	}

	if (ipw2100_get_ordinal(priv, IPW_ORD_STAT_PERCENT_MISSED_BCNS,
				&missed_beacons, &ord_len))
		goto fail_get_ordinal;

	/* If we don't have a connection the quality and level is 0 */
	if (!(priv->status & STATUS_ASSOCIATED)) {
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
	} else {
		if (ipw2100_get_ordinal(priv, IPW_ORD_RSSI_AVG_CURR,
					&rssi, &ord_len))
			goto fail_get_ordinal;
		wstats->qual.level = rssi + IPW2100_RSSI_TO_DBM;
		if (rssi < 10)
			rssi_qual = rssi * POOR / 10;
		else if (rssi < 15)
			rssi_qual = (rssi - 10) * (FAIR - POOR) / 5 + POOR;
		else if (rssi < 20)
			rssi_qual = (rssi - 15) * (GOOD - FAIR) / 5 + FAIR;
		else if (rssi < 30)
			rssi_qual = (rssi - 20) * (VERY_GOOD - GOOD) /
			    10 + GOOD;
		else
			rssi_qual = (rssi - 30) * (PERFECT - VERY_GOOD) /
			    10 + VERY_GOOD;

		if (ipw2100_get_ordinal(priv, IPW_ORD_STAT_PERCENT_RETRIES,
					&tx_retries, &ord_len))
			goto fail_get_ordinal;

		if (tx_retries > 75)
			tx_qual = (90 - tx_retries) * POOR / 15;
		else if (tx_retries > 70)
			tx_qual = (75 - tx_retries) * (FAIR - POOR) / 5 + POOR;
		else if (tx_retries > 65)
			tx_qual = (70 - tx_retries) * (GOOD - FAIR) / 5 + FAIR;
		else if (tx_retries > 50)
			tx_qual = (65 - tx_retries) * (VERY_GOOD - GOOD) /
			    15 + GOOD;
		else
			tx_qual = (50 - tx_retries) *
			    (PERFECT - VERY_GOOD) / 50 + VERY_GOOD;

		if (missed_beacons > 50)
			beacon_qual = (60 - missed_beacons) * POOR / 10;
		else if (missed_beacons > 40)
			beacon_qual = (50 - missed_beacons) * (FAIR - POOR) /
			    10 + POOR;
		else if (missed_beacons > 32)
			beacon_qual = (40 - missed_beacons) * (GOOD - FAIR) /
			    18 + FAIR;
		else if (missed_beacons > 20)
			beacon_qual = (32 - missed_beacons) *
			    (VERY_GOOD - GOOD) / 20 + GOOD;
		else
			beacon_qual = (20 - missed_beacons) *
			    (PERFECT - VERY_GOOD) / 20 + VERY_GOOD;

		quality = min(tx_qual, rssi_qual);
		quality = min(beacon_qual, quality);

#ifdef CONFIG_IPW2100_DEBUG
		if (beacon_qual == quality)
			IPW_DEBUG_WX("Quality clamped by Missed Beacons\n");
		else if (tx_qual == quality)
			IPW_DEBUG_WX("Quality clamped by Tx Retries\n");
		else if (quality != 100)
			IPW_DEBUG_WX("Quality clamped by Signal Strength\n");
		else
			IPW_DEBUG_WX("Quality not clamped.\n");
#endif

		wstats->qual.qual = quality;
		wstats->qual.level = rssi + IPW2100_RSSI_TO_DBM;
	}

	wstats->qual.noise = 0;
	wstats->qual.updated = 7;
	wstats->qual.updated |= IW_QUAL_NOISE_INVALID;

	/* FIXME: this is percent and not a # */
	wstats->miss.beacon = missed_beacons;

	if (ipw2100_get_ordinal(priv, IPW_ORD_STAT_TX_FAILURES,
				&tx_failures, &ord_len))
		goto fail_get_ordinal;
	wstats->discard.retries = tx_failures;

	return wstats;

      fail_get_ordinal:
	IPW_DEBUG_WX("failed querying ordinals.\n");

	return (struct iw_statistics *)NULL;
}

static struct iw_handler_def ipw2100_wx_handler_def = {
	.standard = ipw2100_wx_handlers,
	.num_standard = ARRAY_SIZE(ipw2100_wx_handlers),
	.num_private = ARRAY_SIZE(ipw2100_private_handler),
	.num_private_args = ARRAY_SIZE(ipw2100_private_args),
	.private = (iw_handler *) ipw2100_private_handler,
	.private_args = (struct iw_priv_args *)ipw2100_private_args,
	.get_wireless_stats = ipw2100_wx_wireless_stats,
};

static void ipw2100_wx_event_work(struct work_struct *work)
{
	struct ipw2100_priv *priv =
		container_of(work, struct ipw2100_priv, wx_event_work.work);
	union iwreq_data wrqu;
	unsigned int len = ETH_ALEN;

	if (priv->status & STATUS_STOPPING)
		return;

	mutex_lock(&priv->action_mutex);

	IPW_DEBUG_WX("enter\n");

	mutex_unlock(&priv->action_mutex);

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	/* Fetch BSSID from the hardware */
	if (!(priv->status & (STATUS_ASSOCIATING | STATUS_ASSOCIATED)) ||
	    priv->status & STATUS_RF_KILL_MASK ||
	    ipw2100_get_ordinal(priv, IPW_ORD_STAT_ASSN_AP_BSSID,
				&priv->bssid, &len)) {
		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	} else {
		/* We now have the BSSID, so can finish setting to the full
		 * associated state */
		memcpy(wrqu.ap_addr.sa_data, priv->bssid, ETH_ALEN);
		memcpy(priv->ieee->bssid, priv->bssid, ETH_ALEN);
		priv->status &= ~STATUS_ASSOCIATING;
		priv->status |= STATUS_ASSOCIATED;
		netif_carrier_on(priv->net_dev);
		netif_wake_queue(priv->net_dev);
	}

	if (!(priv->status & STATUS_ASSOCIATED)) {
		IPW_DEBUG_WX("Configuring ESSID\n");
		mutex_lock(&priv->action_mutex);
		/* This is a disassociation event, so kick the firmware to
		 * look for another AP */
		if (priv->config & CFG_STATIC_ESSID)
			ipw2100_set_essid(priv, priv->essid, priv->essid_len,
					  0);
		else
			ipw2100_set_essid(priv, NULL, 0, 0);
		mutex_unlock(&priv->action_mutex);
	}

	wireless_send_event(priv->net_dev, SIOCGIWAP, &wrqu, NULL);
}

#define IPW2100_FW_MAJOR_VERSION 1
#define IPW2100_FW_MINOR_VERSION 3

#define IPW2100_FW_MINOR(x) ((x & 0xff) >> 8)
#define IPW2100_FW_MAJOR(x) (x & 0xff)

#define IPW2100_FW_VERSION ((IPW2100_FW_MINOR_VERSION << 8) | \
                             IPW2100_FW_MAJOR_VERSION)

#define IPW2100_FW_PREFIX "ipw2100-" __stringify(IPW2100_FW_MAJOR_VERSION) \
"." __stringify(IPW2100_FW_MINOR_VERSION)

#define IPW2100_FW_NAME(x) IPW2100_FW_PREFIX "" x ".fw"

/*

BINARY FIRMWARE HEADER FORMAT

offset      length   desc
0           2        version
2           2        mode == 0:BSS,1:IBSS,2:MONITOR
4           4        fw_len
8           4        uc_len
C           fw_len   firmware data
12 + fw_len uc_len   microcode data

*/

struct ipw2100_fw_header {
	short version;
	short mode;
	unsigned int fw_size;
	unsigned int uc_size;
} __packed;

static int ipw2100_mod_firmware_load(struct ipw2100_fw *fw)
{
	struct ipw2100_fw_header *h =
	    (struct ipw2100_fw_header *)fw->fw_entry->data;

	if (IPW2100_FW_MAJOR(h->version) != IPW2100_FW_MAJOR_VERSION) {
		printk(KERN_WARNING DRV_NAME ": Firmware image not compatible "
		       "(detected version id of %u). "
		       "See Documentation/networking/README.ipw2100\n",
		       h->version);
		return 1;
	}

	fw->version = h->version;
	fw->fw.data = fw->fw_entry->data + sizeof(struct ipw2100_fw_header);
	fw->fw.size = h->fw_size;
	fw->uc.data = fw->fw.data + h->fw_size;
	fw->uc.size = h->uc_size;

	return 0;
}

static int ipw2100_get_firmware(struct ipw2100_priv *priv,
				struct ipw2100_fw *fw)
{
	char *fw_name;
	int rc;

	IPW_DEBUG_INFO("%s: Using hotplug firmware load.\n",
		       priv->net_dev->name);

	switch (priv->ieee->iw_mode) {
	case IW_MODE_ADHOC:
		fw_name = IPW2100_FW_NAME("-i");
		break;
#ifdef CONFIG_IPW2100_MONITOR
	case IW_MODE_MONITOR:
		fw_name = IPW2100_FW_NAME("-p");
		break;
#endif
	case IW_MODE_INFRA:
	default:
		fw_name = IPW2100_FW_NAME("");
		break;
	}

	rc = request_firmware(&fw->fw_entry, fw_name, &priv->pci_dev->dev);

	if (rc < 0) {
		printk(KERN_ERR DRV_NAME ": "
		       "%s: Firmware '%s' not available or load failed.\n",
		       priv->net_dev->name, fw_name);
		return rc;
	}
	IPW_DEBUG_INFO("firmware data %p size %zd\n", fw->fw_entry->data,
		       fw->fw_entry->size);

	ipw2100_mod_firmware_load(fw);

	return 0;
}

MODULE_FIRMWARE(IPW2100_FW_NAME("-i"));
#ifdef CONFIG_IPW2100_MONITOR
MODULE_FIRMWARE(IPW2100_FW_NAME("-p"));
#endif
MODULE_FIRMWARE(IPW2100_FW_NAME(""));

static void ipw2100_release_firmware(struct ipw2100_priv *priv,
				     struct ipw2100_fw *fw)
{
	fw->version = 0;
	release_firmware(fw->fw_entry);
	fw->fw_entry = NULL;
}

static int ipw2100_get_fwversion(struct ipw2100_priv *priv, char *buf,
				 size_t max)
{
	char ver[MAX_FW_VERSION_LEN];
	u32 len = MAX_FW_VERSION_LEN;
	u32 tmp;
	int i;
	/* firmware version is an ascii string (max len of 14) */
	if (ipw2100_get_ordinal(priv, IPW_ORD_STAT_FW_VER_NUM, ver, &len))
		return -EIO;
	tmp = max;
	if (len >= max)
		len = max - 1;
	for (i = 0; i < len; i++)
		buf[i] = ver[i];
	buf[i] = '\0';
	return tmp;
}

static int ipw2100_get_ucodeversion(struct ipw2100_priv *priv, char *buf,
				    size_t max)
{
	u32 ver;
	u32 len = sizeof(ver);
	/* microcode version is a 32 bit integer */
	if (ipw2100_get_ordinal(priv, IPW_ORD_UCODE_VERSION, &ver, &len))
		return -EIO;
	return snprintf(buf, max, "%08X", ver);
}

/*
 * On exit, the firmware will have been freed from the fw list
 */
static int ipw2100_fw_download(struct ipw2100_priv *priv, struct ipw2100_fw *fw)
{
	/* firmware is constructed of N contiguous entries, each entry is
	 * structured as:
	 *
	 * offset    sie         desc
	 * 0         4           address to write to
	 * 4         2           length of data run
	 * 6         length      data
	 */
	unsigned int addr;
	unsigned short len;

	const unsigned char *firmware_data = fw->fw.data;
	unsigned int firmware_data_left = fw->fw.size;

	while (firmware_data_left > 0) {
		addr = *(u32 *) (firmware_data);
		firmware_data += 4;
		firmware_data_left -= 4;

		len = *(u16 *) (firmware_data);
		firmware_data += 2;
		firmware_data_left -= 2;

		if (len > 32) {
			printk(KERN_ERR DRV_NAME ": "
			       "Invalid firmware run-length of %d bytes\n",
			       len);
			return -EINVAL;
		}

		write_nic_memory(priv->net_dev, addr, len, firmware_data);
		firmware_data += len;
		firmware_data_left -= len;
	}

	return 0;
}

struct symbol_alive_response {
	u8 cmd_id;
	u8 seq_num;
	u8 ucode_rev;
	u8 eeprom_valid;
	u16 valid_flags;
	u8 IEEE_addr[6];
	u16 flags;
	u16 pcb_rev;
	u16 clock_settle_time;	// 1us LSB
	u16 powerup_settle_time;	// 1us LSB
	u16 hop_settle_time;	// 1us LSB
	u8 date[3];		// month, day, year
	u8 time[2];		// hours, minutes
	u8 ucode_valid;
};

static int ipw2100_ucode_download(struct ipw2100_priv *priv,
				  struct ipw2100_fw *fw)
{
	struct net_device *dev = priv->net_dev;
	const unsigned char *microcode_data = fw->uc.data;
	unsigned int microcode_data_left = fw->uc.size;
	void __iomem *reg = priv->ioaddr;

	struct symbol_alive_response response;
	int i, j;
	u8 data;

	/* Symbol control */
	write_nic_word(dev, IPW2100_CONTROL_REG, 0x703);
	readl(reg);
	write_nic_word(dev, IPW2100_CONTROL_REG, 0x707);
	readl(reg);

	/* HW config */
	write_nic_byte(dev, 0x210014, 0x72);	/* fifo width =16 */
	readl(reg);
	write_nic_byte(dev, 0x210014, 0x72);	/* fifo width =16 */
	readl(reg);

	/* EN_CS_ACCESS bit to reset control store pointer */
	write_nic_byte(dev, 0x210000, 0x40);
	readl(reg);
	write_nic_byte(dev, 0x210000, 0x0);
	readl(reg);
	write_nic_byte(dev, 0x210000, 0x40);
	readl(reg);

	/* copy microcode from buffer into Symbol */

	while (microcode_data_left > 0) {
		write_nic_byte(dev, 0x210010, *microcode_data++);
		write_nic_byte(dev, 0x210010, *microcode_data++);
		microcode_data_left -= 2;
	}

	/* EN_CS_ACCESS bit to reset the control store pointer */
	write_nic_byte(dev, 0x210000, 0x0);
	readl(reg);

	/* Enable System (Reg 0)
	 * first enable causes garbage in RX FIFO */
	write_nic_byte(dev, 0x210000, 0x0);
	readl(reg);
	write_nic_byte(dev, 0x210000, 0x80);
	readl(reg);

	/* Reset External Baseband Reg */
	write_nic_word(dev, IPW2100_CONTROL_REG, 0x703);
	readl(reg);
	write_nic_word(dev, IPW2100_CONTROL_REG, 0x707);
	readl(reg);

	/* HW Config (Reg 5) */
	write_nic_byte(dev, 0x210014, 0x72);	// fifo width =16
	readl(reg);
	write_nic_byte(dev, 0x210014, 0x72);	// fifo width =16
	readl(reg);

	/* Enable System (Reg 0)
	 * second enable should be OK */
	write_nic_byte(dev, 0x210000, 0x00);	// clear enable system
	readl(reg);
	write_nic_byte(dev, 0x210000, 0x80);	// set enable system

	/* check Symbol is enabled - upped this from 5 as it wasn't always
	 * catching the update */
	for (i = 0; i < 10; i++) {
		udelay(10);

		/* check Dino is enabled bit */
		read_nic_byte(dev, 0x210000, &data);
		if (data & 0x1)
			break;
	}

	if (i == 10) {
		printk(KERN_ERR DRV_NAME ": %s: Error initializing Symbol\n",
		       dev->name);
		return -EIO;
	}

	/* Get Symbol alive response */
	for (i = 0; i < 30; i++) {
		/* Read alive response structure */
		for (j = 0;
		     j < (sizeof(struct symbol_alive_response) >> 1); j++)
			read_nic_word(dev, 0x210004, ((u16 *) & response) + j);

		if ((response.cmd_id == 1) && (response.ucode_valid == 0x1))
			break;
		udelay(10);
	}

	if (i == 30) {
		printk(KERN_ERR DRV_NAME
		       ": %s: No response from Symbol - hw not alive\n",
		       dev->name);
		printk_buf(IPW_DL_ERROR, (u8 *) & response, sizeof(response));
		return -EIO;
	}

	return 0;
}
