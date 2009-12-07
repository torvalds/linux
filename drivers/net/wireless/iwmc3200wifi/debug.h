/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __IWM_DEBUG_H__
#define __IWM_DEBUG_H__

#define IWM_ERR(p, f, a...) dev_err(iwm_to_dev(p), f, ## a)
#define IWM_WARN(p, f, a...) dev_warn(iwm_to_dev(p), f, ## a)
#define IWM_INFO(p, f, a...) dev_info(iwm_to_dev(p), f, ## a)
#define IWM_CRIT(p, f, a...) dev_crit(iwm_to_dev(p), f, ## a)

#ifdef CONFIG_IWM_DEBUG

#define IWM_DEBUG_MODULE(i, level, module, f, a...)			     \
do {									     \
	if (unlikely(i->dbg.dbg_module[IWM_DM_##module] >= (IWM_DL_##level)))\
		dev_printk(KERN_INFO, (iwm_to_dev(i)),	             \
			   "%s " f, __func__ , ## a);			     \
} while (0)

#define IWM_HEXDUMP(i, level, module, pref, buf, len)		             \
do {									     \
	if (unlikely(i->dbg.dbg_module[IWM_DM_##module] >= (IWM_DL_##level)))\
		print_hex_dump(KERN_INFO, pref, DUMP_PREFIX_OFFSET,	     \
			       16, 1, buf, len, 1);			     \
} while (0)

#else

#define IWM_DEBUG_MODULE(i, level, module, f, a...)
#define IWM_HEXDUMP(i, level, module, pref, buf, len)

#endif /* CONFIG_IWM_DEBUG */

/* Debug modules */
enum iwm_debug_module_id {
	IWM_DM_BOOT = 0,
	IWM_DM_FW,
	IWM_DM_SDIO,
	IWM_DM_NTF,
	IWM_DM_RX,
	IWM_DM_TX,
	IWM_DM_MLME,
	IWM_DM_CMD,
	IWM_DM_WEXT,
	__IWM_DM_NR,
};
#define IWM_DM_DEFAULT 0

#define IWM_DBG_BOOT(i, l, f, a...) IWM_DEBUG_MODULE(i, l, BOOT, f, ## a)
#define IWM_DBG_FW(i, l, f, a...)   IWM_DEBUG_MODULE(i, l, FW, f, ## a)
#define IWM_DBG_SDIO(i, l, f, a...) IWM_DEBUG_MODULE(i, l, SDIO, f, ## a)
#define IWM_DBG_NTF(i, l, f, a...)  IWM_DEBUG_MODULE(i, l, NTF, f, ## a)
#define IWM_DBG_RX(i, l, f, a...)   IWM_DEBUG_MODULE(i, l, RX, f, ## a)
#define IWM_DBG_TX(i, l, f, a...)   IWM_DEBUG_MODULE(i, l, TX, f, ## a)
#define IWM_DBG_MLME(i, l, f, a...) IWM_DEBUG_MODULE(i, l, MLME, f, ## a)
#define IWM_DBG_CMD(i, l, f, a...)  IWM_DEBUG_MODULE(i, l, CMD, f, ## a)
#define IWM_DBG_WEXT(i, l, f, a...) IWM_DEBUG_MODULE(i, l, WEXT, f, ## a)

/* Debug levels */
enum iwm_debug_level {
	IWM_DL_NONE = 0,
	IWM_DL_ERR,
	IWM_DL_WARN,
	IWM_DL_INFO,
	IWM_DL_DBG,
};
#define IWM_DL_DEFAULT IWM_DL_ERR

struct iwm_debugfs {
	struct iwm_priv *iwm;
	struct dentry *rootdir;
	struct dentry *devdir;
	struct dentry *dbgdir;
	struct dentry *txdir;
	struct dentry *rxdir;
	struct dentry *busdir;

	u32 dbg_level;
	struct dentry *dbg_level_dentry;

	unsigned long dbg_modules;
	struct dentry *dbg_modules_dentry;

	u8 dbg_module[__IWM_DM_NR];
	struct dentry *dbg_module_dentries[__IWM_DM_NR];

	struct dentry *txq_dentry;
	struct dentry *tx_credit_dentry;
	struct dentry *rx_ticket_dentry;

	struct dentry *fw_err_dentry;
};

#ifdef CONFIG_IWM_DEBUG
int iwm_debugfs_init(struct iwm_priv *iwm);
void iwm_debugfs_exit(struct iwm_priv *iwm);
#else
static inline int iwm_debugfs_init(struct iwm_priv *iwm)
{
	return 0;
}
static inline void iwm_debugfs_exit(struct iwm_priv *iwm) {}
#endif

#endif
