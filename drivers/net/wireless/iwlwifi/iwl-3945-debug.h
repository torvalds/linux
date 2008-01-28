/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl3945_debug_h__
#define __iwl3945_debug_h__

#ifdef CONFIG_IWL3945_DEBUG
extern u32 iwl3945_debug_level;
#define IWL_DEBUG(level, fmt, args...) \
do { if (iwl3945_debug_level & (level)) \
  printk(KERN_ERR DRV_NAME": %c %s " fmt, \
	 in_interrupt() ? 'I' : 'U', __FUNCTION__ , ## args); } while (0)

#define IWL_DEBUG_LIMIT(level, fmt, args...) \
do { if ((iwl3945_debug_level & (level)) && net_ratelimit()) \
  printk(KERN_ERR DRV_NAME": %c %s " fmt, \
	 in_interrupt() ? 'I' : 'U', __FUNCTION__ , ## args); } while (0)
#else
static inline void IWL_DEBUG(int level, const char *fmt, ...)
{
}
static inline void IWL_DEBUG_LIMIT(int level, const char *fmt, ...)
{
}
#endif				/* CONFIG_IWL3945_DEBUG */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IWL_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IWL_xxxx_DEBUG() macro definition for your
 * classification, or use IWL_DEBUG(IWL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/iwl/debug_level
 *
 * you simply need to add your entry to the iwl3945_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/iwl then you do not have
 * CONFIG_IWL3945_DEBUG defined in your kernel configuration
 *
 */

#define IWL_DL_INFO          (1<<0)
#define IWL_DL_MAC80211      (1<<1)
#define IWL_DL_HOST_COMMAND  (1<<2)
#define IWL_DL_STATE         (1<<3)

#define IWL_DL_RADIO         (1<<7)
#define IWL_DL_POWER         (1<<8)
#define IWL_DL_TEMP          (1<<9)

#define IWL_DL_NOTIF         (1<<10)
#define IWL_DL_SCAN          (1<<11)
#define IWL_DL_ASSOC         (1<<12)
#define IWL_DL_DROP          (1<<13)

#define IWL_DL_TXPOWER       (1<<14)

#define IWL_DL_AP            (1<<15)

#define IWL_DL_FW            (1<<16)
#define IWL_DL_RF_KILL       (1<<17)
#define IWL_DL_FW_ERRORS     (1<<18)

#define IWL_DL_LED           (1<<19)

#define IWL_DL_RATE          (1<<20)

#define IWL_DL_CALIB         (1<<21)
#define IWL_DL_WEP           (1<<22)
#define IWL_DL_TX            (1<<23)
#define IWL_DL_RX            (1<<24)
#define IWL_DL_ISR           (1<<25)
#define IWL_DL_HT            (1<<26)
#define IWL_DL_IO            (1<<27)
#define IWL_DL_11H           (1<<28)

#define IWL_DL_STATS         (1<<29)
#define IWL_DL_TX_REPLY      (1<<30)
#define IWL_DL_QOS           (1<<31)

#define IWL_ERROR(f, a...) printk(KERN_ERR DRV_NAME ": " f, ## a)
#define IWL_WARNING(f, a...) printk(KERN_WARNING DRV_NAME ": " f, ## a)
#define IWL_DEBUG_INFO(f, a...)    IWL_DEBUG(IWL_DL_INFO, f, ## a)

#define IWL_DEBUG_MAC80211(f, a...)     IWL_DEBUG(IWL_DL_MAC80211, f, ## a)
#define IWL_DEBUG_TEMP(f, a...)   IWL_DEBUG(IWL_DL_TEMP, f, ## a)
#define IWL_DEBUG_SCAN(f, a...)   IWL_DEBUG(IWL_DL_SCAN, f, ## a)
#define IWL_DEBUG_RX(f, a...)     IWL_DEBUG(IWL_DL_RX, f, ## a)
#define IWL_DEBUG_TX(f, a...)     IWL_DEBUG(IWL_DL_TX, f, ## a)
#define IWL_DEBUG_ISR(f, a...)    IWL_DEBUG(IWL_DL_ISR, f, ## a)
#define IWL_DEBUG_LED(f, a...) IWL_DEBUG(IWL_DL_LED, f, ## a)
#define IWL_DEBUG_WEP(f, a...)    IWL_DEBUG(IWL_DL_WEP, f, ## a)
#define IWL_DEBUG_HC(f, a...) IWL_DEBUG(IWL_DL_HOST_COMMAND, f, ## a)
#define IWL_DEBUG_CALIB(f, a...) IWL_DEBUG(IWL_DL_CALIB, f, ## a)
#define IWL_DEBUG_FW(f, a...) IWL_DEBUG(IWL_DL_FW, f, ## a)
#define IWL_DEBUG_RF_KILL(f, a...) IWL_DEBUG(IWL_DL_RF_KILL, f, ## a)
#define IWL_DEBUG_DROP(f, a...) IWL_DEBUG(IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_DROP_LIMIT(f, a...) IWL_DEBUG_LIMIT(IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_AP(f, a...) IWL_DEBUG(IWL_DL_AP, f, ## a)
#define IWL_DEBUG_TXPOWER(f, a...) IWL_DEBUG(IWL_DL_TXPOWER, f, ## a)
#define IWL_DEBUG_IO(f, a...) IWL_DEBUG(IWL_DL_IO, f, ## a)
#define IWL_DEBUG_RATE(f, a...) IWL_DEBUG(IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_RATE_LIMIT(f, a...) IWL_DEBUG_LIMIT(IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_NOTIF(f, a...) IWL_DEBUG(IWL_DL_NOTIF, f, ## a)
#define IWL_DEBUG_ASSOC(f, a...) IWL_DEBUG(IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_ASSOC_LIMIT(f, a...) \
	IWL_DEBUG_LIMIT(IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_HT(f, a...) IWL_DEBUG(IWL_DL_HT, f, ## a)
#define IWL_DEBUG_STATS(f, a...) IWL_DEBUG(IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_TX_REPLY(f, a...) IWL_DEBUG(IWL_DL_TX_REPLY, f, ## a)
#define IWL_DEBUG_QOS(f, a...)   IWL_DEBUG(IWL_DL_QOS, f, ## a)
#define IWL_DEBUG_RADIO(f, a...)  IWL_DEBUG(IWL_DL_RADIO, f, ## a)
#define IWL_DEBUG_POWER(f, a...)  IWL_DEBUG(IWL_DL_POWER, f, ## a)
#define IWL_DEBUG_11H(f, a...)  IWL_DEBUG(IWL_DL_11H, f, ## a)

#endif
