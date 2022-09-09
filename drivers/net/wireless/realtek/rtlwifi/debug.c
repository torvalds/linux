// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "wifi.h"
#include "cam.h"

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_RTLWIFI_DEBUG
void _rtl_dbg_print(struct rtl_priv *rtlpriv, u64 comp, int level,
		    const char *fmt, ...)
{
	if (unlikely((comp & rtlpriv->cfg->mod_params->debug_mask) &&
		     level <= rtlpriv->cfg->mod_params->debug_level)) {
		struct va_format vaf;
		va_list args;

		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		pr_info("%pV", &vaf);

		va_end(args);
	}
}
EXPORT_SYMBOL_GPL(_rtl_dbg_print);

void _rtl_dbg_print_data(struct rtl_priv *rtlpriv, u64 comp, int level,
			 const char *titlestring,
			 const void *hexdata, int hexdatalen)
{
	if (unlikely(((comp) & rtlpriv->cfg->mod_params->debug_mask) &&
		     ((level) <= rtlpriv->cfg->mod_params->debug_level))) {
		pr_info("In process \"%s\" (pid %i): %s\n",
			current->comm, current->pid, titlestring);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE,
				     hexdata, hexdatalen);
	}
}
EXPORT_SYMBOL_GPL(_rtl_dbg_print_data);

struct rtl_debugfs_priv {
	struct rtl_priv *rtlpriv;
	int (*cb_read)(struct seq_file *m, void *v);
	ssize_t (*cb_write)(struct file *filp, const char __user *buffer,
			    size_t count, loff_t *loff);
	u32 cb_data;
};

static struct dentry *debugfs_topdir;

static int rtl_debug_get_common(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;

	return debugfs_priv->cb_read(m, v);
}

static int dl_debug_open_common(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_debug_get_common, inode->i_private);
}

static const struct file_operations file_ops_common = {
	.open = dl_debug_open_common,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl_debug_get_mac_page(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	u32 page = debugfs_priv->cb_data;
	int i, n;
	int max = 0xff;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

#define RTL_DEBUG_IMPL_MAC_SERIES(page, addr)			\
static struct rtl_debugfs_priv rtl_debug_priv_mac_ ##page = {	\
	.cb_read = rtl_debug_get_mac_page,			\
	.cb_data = addr,					\
}

RTL_DEBUG_IMPL_MAC_SERIES(0, 0x0000);
RTL_DEBUG_IMPL_MAC_SERIES(1, 0x0100);
RTL_DEBUG_IMPL_MAC_SERIES(2, 0x0200);
RTL_DEBUG_IMPL_MAC_SERIES(3, 0x0300);
RTL_DEBUG_IMPL_MAC_SERIES(4, 0x0400);
RTL_DEBUG_IMPL_MAC_SERIES(5, 0x0500);
RTL_DEBUG_IMPL_MAC_SERIES(6, 0x0600);
RTL_DEBUG_IMPL_MAC_SERIES(7, 0x0700);
RTL_DEBUG_IMPL_MAC_SERIES(10, 0x1000);
RTL_DEBUG_IMPL_MAC_SERIES(11, 0x1100);
RTL_DEBUG_IMPL_MAC_SERIES(12, 0x1200);
RTL_DEBUG_IMPL_MAC_SERIES(13, 0x1300);
RTL_DEBUG_IMPL_MAC_SERIES(14, 0x1400);
RTL_DEBUG_IMPL_MAC_SERIES(15, 0x1500);
RTL_DEBUG_IMPL_MAC_SERIES(16, 0x1600);
RTL_DEBUG_IMPL_MAC_SERIES(17, 0x1700);

static int rtl_debug_get_bb_page(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	struct ieee80211_hw *hw = rtlpriv->hw;
	u32 page = debugfs_priv->cb_data;
	int i, n;
	int max = 0xff;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

#define RTL_DEBUG_IMPL_BB_SERIES(page, addr)			\
static struct rtl_debugfs_priv rtl_debug_priv_bb_ ##page = {	\
	.cb_read = rtl_debug_get_bb_page,			\
	.cb_data = addr,					\
}

RTL_DEBUG_IMPL_BB_SERIES(8, 0x0800);
RTL_DEBUG_IMPL_BB_SERIES(9, 0x0900);
RTL_DEBUG_IMPL_BB_SERIES(a, 0x0a00);
RTL_DEBUG_IMPL_BB_SERIES(b, 0x0b00);
RTL_DEBUG_IMPL_BB_SERIES(c, 0x0c00);
RTL_DEBUG_IMPL_BB_SERIES(d, 0x0d00);
RTL_DEBUG_IMPL_BB_SERIES(e, 0x0e00);
RTL_DEBUG_IMPL_BB_SERIES(f, 0x0f00);
RTL_DEBUG_IMPL_BB_SERIES(18, 0x1800);
RTL_DEBUG_IMPL_BB_SERIES(19, 0x1900);
RTL_DEBUG_IMPL_BB_SERIES(1a, 0x1a00);
RTL_DEBUG_IMPL_BB_SERIES(1b, 0x1b00);
RTL_DEBUG_IMPL_BB_SERIES(1c, 0x1c00);
RTL_DEBUG_IMPL_BB_SERIES(1d, 0x1d00);
RTL_DEBUG_IMPL_BB_SERIES(1e, 0x1e00);
RTL_DEBUG_IMPL_BB_SERIES(1f, 0x1f00);

static int rtl_debug_get_reg_rf(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	struct ieee80211_hw *hw = rtlpriv->hw;
	enum radio_path rfpath = debugfs_priv->cb_data;
	int i, n;
	int max = 0x40;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		max = 0xff;

	seq_printf(m, "\nPATH(%d)", rfpath);

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n);
		for (i = 0; i < 4 && n <= max; n += 1, i++)
			seq_printf(m, "%8.8x    ",
				   rtl_get_rfreg(hw, rfpath, n, 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

#define RTL_DEBUG_IMPL_RF_SERIES(page, addr)			\
static struct rtl_debugfs_priv rtl_debug_priv_rf_ ##page = {	\
	.cb_read = rtl_debug_get_reg_rf,			\
	.cb_data = addr,					\
}

RTL_DEBUG_IMPL_RF_SERIES(a, RF90_PATH_A);
RTL_DEBUG_IMPL_RF_SERIES(b, RF90_PATH_B);

static int rtl_debug_get_cam_register(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	int start = debugfs_priv->cb_data;
	u32 target_cmd = 0;
	u32 target_val = 0;
	u8 entry_i = 0;
	u32 ulstatus;
	int i = 100, j = 0;
	int end = (start + 11 > TOTAL_CAM_ENTRY ? TOTAL_CAM_ENTRY : start + 11);

	/* This dump the current register page */
	seq_printf(m,
		   "\n#################### SECURITY CAM (%d-%d) ##################\n",
		   start, end - 1);

	for (j = start; j < end; j++) {
		seq_printf(m, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			/* polling bit, and No Write enable, and address  */
			target_cmd = entry_i + CAM_CONTENT_COUNT * j;
			target_cmd = target_cmd | BIT(31);

			/* Check polling bit is clear */
			while ((i--) >= 0) {
				ulstatus =
				    rtl_read_dword(rtlpriv,
						   rtlpriv->cfg->maps[RWCAM]);
				if (ulstatus & BIT(31))
					continue;
				else
					break;
			}

			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM],
					target_cmd);
			target_val = rtl_read_dword(rtlpriv,
						    rtlpriv->cfg->maps[RCAMO]);
			seq_printf(m, "%8.8x ", target_val);
		}
	}
	seq_puts(m, "\n");
	return 0;
}

#define RTL_DEBUG_IMPL_CAM_SERIES(page, addr)			\
static struct rtl_debugfs_priv rtl_debug_priv_cam_ ##page = {	\
	.cb_read = rtl_debug_get_cam_register,			\
	.cb_data = addr,					\
}

RTL_DEBUG_IMPL_CAM_SERIES(1, 0);
RTL_DEBUG_IMPL_CAM_SERIES(2, 11);
RTL_DEBUG_IMPL_CAM_SERIES(3, 22);

static int rtl_debug_get_btcoex(struct seq_file *m, void *v)
{
	struct rtl_debugfs_priv *debugfs_priv = m->private;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_display_bt_coex_info(rtlpriv,
								     m);

	seq_puts(m, "\n");

	return 0;
}

static struct rtl_debugfs_priv rtl_debug_priv_btcoex = {
	.cb_read = rtl_debug_get_btcoex,
	.cb_data = 0,
};

static ssize_t rtl_debugfs_set_write_reg(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *loff)
{
	struct rtl_debugfs_priv *debugfs_priv = filp->private_data;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	char tmp[32 + 1];
	int tmp_len;
	u32 addr, val, len;
	int num;

	if (count < 3)
		return -EFAULT;

	tmp_len = (count > sizeof(tmp) - 1 ? sizeof(tmp) - 1 : count);

	if (!buffer || copy_from_user(tmp, buffer, tmp_len))
		return count;

	tmp[tmp_len] = '\0';

	/* write BB/MAC register */
	num = sscanf(tmp, "%x %x %x", &addr, &val, &len);

	if (num !=  3)
		return count;

	switch (len) {
	case 1:
		rtl_write_byte(rtlpriv, addr, (u8)val);
		break;
	case 2:
		rtl_write_word(rtlpriv, addr, (u16)val);
		break;
	case 4:
		rtl_write_dword(rtlpriv, addr, val);
		break;
	default:
		/*printk("error write length=%d", len);*/
		break;
	}

	return count;
}

static struct rtl_debugfs_priv rtl_debug_priv_write_reg = {
	.cb_write = rtl_debugfs_set_write_reg,
};

static ssize_t rtl_debugfs_set_write_h2c(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *loff)
{
	struct rtl_debugfs_priv *debugfs_priv = filp->private_data;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	struct ieee80211_hw *hw = rtlpriv->hw;
	char tmp[32 + 1];
	int tmp_len;
	u8 h2c_len, h2c_data_packed[8];
	int h2c_data[8];	/* idx 0: cmd */
	int i;

	if (count < 3)
		return -EFAULT;

	tmp_len = (count > sizeof(tmp) - 1 ? sizeof(tmp) - 1 : count);

	if (copy_from_user(tmp, buffer, tmp_len))
		return -EFAULT;

	tmp[tmp_len] = '\0';

	h2c_len = sscanf(tmp, "%X %X %X %X %X %X %X %X",
			 &h2c_data[0], &h2c_data[1],
			 &h2c_data[2], &h2c_data[3],
			 &h2c_data[4], &h2c_data[5],
			 &h2c_data[6], &h2c_data[7]);

	if (h2c_len == 0)
		return -EINVAL;

	for (i = 0; i < h2c_len; i++)
		h2c_data_packed[i] = (u8)h2c_data[i];

	rtlpriv->cfg->ops->fill_h2c_cmd(hw, h2c_data_packed[0],
					h2c_len - 1,
					&h2c_data_packed[1]);

	return count;
}

static struct rtl_debugfs_priv rtl_debug_priv_write_h2c = {
	.cb_write = rtl_debugfs_set_write_h2c,
};

static ssize_t rtl_debugfs_set_write_rfreg(struct file *filp,
					   const char __user *buffer,
					    size_t count, loff_t *loff)
{
	struct rtl_debugfs_priv *debugfs_priv = filp->private_data;
	struct rtl_priv *rtlpriv = debugfs_priv->rtlpriv;
	struct ieee80211_hw *hw = rtlpriv->hw;
	char tmp[32 + 1];
	int tmp_len;
	int num;
	int path;
	u32 addr, bitmask, data;

	if (count < 3)
		return -EFAULT;

	tmp_len = (count > sizeof(tmp) - 1 ? sizeof(tmp) - 1 : count);

	if (!buffer || copy_from_user(tmp, buffer, tmp_len))
		return count;

	tmp[tmp_len] = '\0';

	num = sscanf(tmp, "%X %X %X %X",
		     &path, &addr, &bitmask, &data);

	if (num != 4) {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_DMESG,
			"Format is <path> <addr> <mask> <data>\n");
		return count;
	}

	rtl_set_rfreg(hw, path, addr, bitmask, data);

	return count;
}

static struct rtl_debugfs_priv rtl_debug_priv_write_rfreg = {
	.cb_write = rtl_debugfs_set_write_rfreg,
};

static int rtl_debugfs_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t rtl_debugfs_common_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *loff)
{
	struct rtl_debugfs_priv *debugfs_priv = filp->private_data;

	return debugfs_priv->cb_write(filp, buffer, count, loff);
}

static const struct file_operations file_ops_common_write = {
	.owner = THIS_MODULE,
	.write = rtl_debugfs_common_write,
	.open = simple_open,
	.release = rtl_debugfs_close,
};

#define RTL_DEBUGFS_ADD_CORE(name, mode, fopname)			   \
	do {								   \
		rtl_debug_priv_ ##name.rtlpriv = rtlpriv;		   \
		debugfs_create_file(#name, mode, parent,		   \
				    &rtl_debug_priv_ ##name,		   \
				    &file_ops_ ##fopname);		   \
	} while (0)

#define RTL_DEBUGFS_ADD(name)						   \
		RTL_DEBUGFS_ADD_CORE(name, S_IFREG | 0444, common)
#define RTL_DEBUGFS_ADD_W(name)						   \
		RTL_DEBUGFS_ADD_CORE(name, S_IFREG | 0222, common_write)

void rtl_debug_add_one(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct dentry *parent;

	snprintf(rtlpriv->dbg.debugfs_name, 18, "%pMF", rtlefuse->dev_addr);

	rtlpriv->dbg.debugfs_dir =
		debugfs_create_dir(rtlpriv->dbg.debugfs_name, debugfs_topdir);

	parent = rtlpriv->dbg.debugfs_dir;

	RTL_DEBUGFS_ADD(mac_0);
	RTL_DEBUGFS_ADD(mac_1);
	RTL_DEBUGFS_ADD(mac_2);
	RTL_DEBUGFS_ADD(mac_3);
	RTL_DEBUGFS_ADD(mac_4);
	RTL_DEBUGFS_ADD(mac_5);
	RTL_DEBUGFS_ADD(mac_6);
	RTL_DEBUGFS_ADD(mac_7);
	RTL_DEBUGFS_ADD(bb_8);
	RTL_DEBUGFS_ADD(bb_9);
	RTL_DEBUGFS_ADD(bb_a);
	RTL_DEBUGFS_ADD(bb_b);
	RTL_DEBUGFS_ADD(bb_c);
	RTL_DEBUGFS_ADD(bb_d);
	RTL_DEBUGFS_ADD(bb_e);
	RTL_DEBUGFS_ADD(bb_f);
	RTL_DEBUGFS_ADD(mac_10);
	RTL_DEBUGFS_ADD(mac_11);
	RTL_DEBUGFS_ADD(mac_12);
	RTL_DEBUGFS_ADD(mac_13);
	RTL_DEBUGFS_ADD(mac_14);
	RTL_DEBUGFS_ADD(mac_15);
	RTL_DEBUGFS_ADD(mac_16);
	RTL_DEBUGFS_ADD(mac_17);
	RTL_DEBUGFS_ADD(bb_18);
	RTL_DEBUGFS_ADD(bb_19);
	RTL_DEBUGFS_ADD(bb_1a);
	RTL_DEBUGFS_ADD(bb_1b);
	RTL_DEBUGFS_ADD(bb_1c);
	RTL_DEBUGFS_ADD(bb_1d);
	RTL_DEBUGFS_ADD(bb_1e);
	RTL_DEBUGFS_ADD(bb_1f);
	RTL_DEBUGFS_ADD(rf_a);
	RTL_DEBUGFS_ADD(rf_b);

	RTL_DEBUGFS_ADD(cam_1);
	RTL_DEBUGFS_ADD(cam_2);
	RTL_DEBUGFS_ADD(cam_3);

	RTL_DEBUGFS_ADD(btcoex);

	RTL_DEBUGFS_ADD_W(write_reg);
	RTL_DEBUGFS_ADD_W(write_h2c);
	RTL_DEBUGFS_ADD_W(write_rfreg);
}
EXPORT_SYMBOL_GPL(rtl_debug_add_one);

void rtl_debug_remove_one(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	debugfs_remove_recursive(rtlpriv->dbg.debugfs_dir);
	rtlpriv->dbg.debugfs_dir = NULL;
}
EXPORT_SYMBOL_GPL(rtl_debug_remove_one);

void rtl_debugfs_add_topdir(void)
{
	debugfs_topdir = debugfs_create_dir("rtlwifi", NULL);
}

void rtl_debugfs_remove_topdir(void)
{
	debugfs_remove_recursive(debugfs_topdir);
}

#endif
