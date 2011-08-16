/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __il_debug_h__
#define __il_debug_h__

struct il_priv;
extern u32 il_debug_level;

#define IL_ERR(p, f, a...) dev_err(&((p)->pci_dev->dev), f, ## a)
#define IL_WARN(p, f, a...) dev_warn(&((p)->pci_dev->dev), f, ## a)
#define IL_INFO(p, f, a...) dev_info(&((p)->pci_dev->dev), f, ## a)
#define IL_CRIT(p, f, a...) dev_crit(&((p)->pci_dev->dev), f, ## a)

#define il_print_hex_error(il, p, len)				 \
do {									\
	print_hex_dump(KERN_ERR, "iwl data: ",				\
		       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);		\
} while (0)

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
#define IL_DEBUG(__priv, level, fmt, args...)				\
do {									\
	if (il_get_debug_level(__priv) & (level))			\
		dev_printk(KERN_ERR, &(__priv->hw->wiphy->dev),		\
			 "%c %s " fmt, in_interrupt() ? 'I' : 'U',	\
			__func__ , ## args);				\
} while (0)

#define il_print_hex_dump(il, level, p, len) 			\
do {									\
	if (il_get_debug_level(il) & level)				\
		print_hex_dump(KERN_DEBUG, "iwl data: ",		\
			       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);	\
} while (0)

#else
#define IL_DEBUG(__priv, level, fmt, args...)
static inline void il_print_hex_dump(struct il_priv *il, int level,
				      const void *p, u32 len)
{}
#endif				/* CONFIG_IWLWIFI_LEGACY_DEBUG */

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
int il_dbgfs_register(struct il_priv *il, const char *name);
void il_dbgfs_unregister(struct il_priv *il);
#else
static inline int
il_dbgfs_register(struct il_priv *il, const char *name)
{
	return 0;
}
static inline void il_dbgfs_unregister(struct il_priv *il)
{
}
#endif				/* CONFIG_IWLWIFI_LEGACY_DEBUGFS */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of
 *
 * #define IL_DL_xxxx VALUE
 *
 * where xxxx should be the name of the classification (for example, WEP).
 *
 * You then need to either add a IL_xxxx_DEBUG() macro definition for your
 * classification, or use IL_DEBUG(IL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * The active debug levels can be accessed via files
 *
 * 	/sys/module/iwl4965/parameters/debug{50}
 *	/sys/module/iwl3945/parameters/debug
 * 	/sys/class/net/wlan0/device/debug_level
 *
 * when CONFIG_IWLWIFI_LEGACY_DEBUG=y.
 */

/* 0x0000000F - 0x00000001 */
#define IL_DL_INFO		(1 << 0)
#define IL_DL_MAC80211		(1 << 1)
#define IL_DL_HCMD		(1 << 2)
#define IL_DL_STATE		(1 << 3)
/* 0x000000F0 - 0x00000010 */
#define IL_DL_MACDUMP		(1 << 4)
#define IL_DL_HCMD_DUMP	(1 << 5)
#define IL_DL_EEPROM		(1 << 6)
#define IL_DL_RADIO		(1 << 7)
/* 0x00000F00 - 0x00000100 */
#define IL_DL_POWER		(1 << 8)
#define IL_DL_TEMP		(1 << 9)
#define IL_DL_NOTIF		(1 << 10)
#define IL_DL_SCAN		(1 << 11)
/* 0x0000F000 - 0x00001000 */
#define IL_DL_ASSOC		(1 << 12)
#define IL_DL_DROP		(1 << 13)
#define IL_DL_TXPOWER		(1 << 14)
#define IL_DL_AP		(1 << 15)
/* 0x000F0000 - 0x00010000 */
#define IL_DL_FW		(1 << 16)
#define IL_DL_RF_KILL		(1 << 17)
#define IL_DL_FW_ERRORS	(1 << 18)
#define IL_DL_LED		(1 << 19)
/* 0x00F00000 - 0x00100000 */
#define IL_DL_RATE		(1 << 20)
#define IL_DL_CALIB		(1 << 21)
#define IL_DL_WEP		(1 << 22)
#define IL_DL_TX		(1 << 23)
/* 0x0F000000 - 0x01000000 */
#define IL_DL_RX		(1 << 24)
#define IL_DL_ISR		(1 << 25)
#define IL_DL_HT		(1 << 26)
/* 0xF0000000 - 0x10000000 */
#define IL_DL_11H		(1 << 28)
#define IL_DL_STATS		(1 << 29)
#define IL_DL_TX_REPLY		(1 << 30)
#define IL_DL_QOS		(1 << 31)

#define IL_DEBUG_INFO(p, f, a...)	IL_DEBUG(p, IL_DL_INFO, f, ## a)
#define IL_DEBUG_MAC80211(p, f, a...)	IL_DEBUG(p, IL_DL_MAC80211, f, ## a)
#define IL_DEBUG_MACDUMP(p, f, a...)	IL_DEBUG(p, IL_DL_MACDUMP, f, ## a)
#define IL_DEBUG_TEMP(p, f, a...)	IL_DEBUG(p, IL_DL_TEMP, f, ## a)
#define IL_DEBUG_SCAN(p, f, a...)	IL_DEBUG(p, IL_DL_SCAN, f, ## a)
#define IL_DEBUG_RX(p, f, a...)	IL_DEBUG(p, IL_DL_RX, f, ## a)
#define IL_DEBUG_TX(p, f, a...)	IL_DEBUG(p, IL_DL_TX, f, ## a)
#define IL_DEBUG_ISR(p, f, a...)	IL_DEBUG(p, IL_DL_ISR, f, ## a)
#define IL_DEBUG_LED(p, f, a...)	IL_DEBUG(p, IL_DL_LED, f, ## a)
#define IL_DEBUG_WEP(p, f, a...)	IL_DEBUG(p, IL_DL_WEP, f, ## a)
#define IL_DEBUG_HC(p, f, a...)	IL_DEBUG(p, IL_DL_HCMD, f, ## a)
#define IL_DEBUG_HC_DUMP(p, f, a...)	IL_DEBUG(p, IL_DL_HCMD_DUMP, f, ## a)
#define IL_DEBUG_EEPROM(p, f, a...)	IL_DEBUG(p, IL_DL_EEPROM, f, ## a)
#define IL_DEBUG_CALIB(p, f, a...)	IL_DEBUG(p, IL_DL_CALIB, f, ## a)
#define IL_DEBUG_FW(p, f, a...)	IL_DEBUG(p, IL_DL_FW, f, ## a)
#define IL_DEBUG_RF_KILL(p, f, a...)	IL_DEBUG(p, IL_DL_RF_KILL, f, ## a)
#define IL_DEBUG_DROP(p, f, a...)	IL_DEBUG(p, IL_DL_DROP, f, ## a)
#define IL_DEBUG_AP(p, f, a...)	IL_DEBUG(p, IL_DL_AP, f, ## a)
#define IL_DEBUG_TXPOWER(p, f, a...)	IL_DEBUG(p, IL_DL_TXPOWER, f, ## a)
#define IL_DEBUG_RATE(p, f, a...)	IL_DEBUG(p, IL_DL_RATE, f, ## a)
#define IL_DEBUG_NOTIF(p, f, a...)	IL_DEBUG(p, IL_DL_NOTIF, f, ## a)
#define IL_DEBUG_ASSOC(p, f, a...)	IL_DEBUG(p, IL_DL_ASSOC, f, ## a)
#define IL_DEBUG_HT(p, f, a...)	IL_DEBUG(p, IL_DL_HT, f, ## a)
#define IL_DEBUG_STATS(p, f, a...)	IL_DEBUG(p, IL_DL_STATS, f, ## a)
#define IL_DEBUG_TX_REPLY(p, f, a...)	IL_DEBUG(p, IL_DL_TX_REPLY, f, ## a)
#define IL_DEBUG_QOS(p, f, a...)	IL_DEBUG(p, IL_DL_QOS, f, ## a)
#define IL_DEBUG_RADIO(p, f, a...)	IL_DEBUG(p, IL_DL_RADIO, f, ## a)
#define IL_DEBUG_POWER(p, f, a...)	IL_DEBUG(p, IL_DL_POWER, f, ## a)
#define IL_DEBUG_11H(p, f, a...)	IL_DEBUG(p, IL_DL_11H, f, ## a)

#endif
