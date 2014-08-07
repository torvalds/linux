/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * Tmis program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "wifi.h"
#include "cam.h"

#define GET_INODE_DATA(__node)		PDE_DATA(__node)


void rtl92e_dbgp_flag_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 i;

	rtlpriv->dbg.global_debuglevel = DBG_DMESG;

	rtlpriv->dbg.global_debugcomponents =
		COMP_ERR |
		COMP_FW |
		COMP_INIT |
		COMP_RECV |
		COMP_SEND |
		COMP_MLME |
		COMP_SCAN |
		COMP_INTR |
		COMP_LED |
		COMP_SEC |
		COMP_BEACON |
		COMP_RATE |
		COMP_RXDESC |
		COMP_DIG |
		COMP_TXAGC |
		COMP_POWER |
		COMP_POWER_TRACKING |
		COMP_BB_POWERSAVING |
		COMP_SWAS |
		COMP_RF |
		COMP_TURBO |
		COMP_RATR |
		COMP_CMD |
		COMP_EASY_CONCURRENT |
		COMP_EFUSE |
		COMP_QOS | COMP_MAC80211 | COMP_REGD |
		COMP_CHAN |
		COMP_BT_COEXIST |
		COMP_IQK |
		0;

	for (i = 0; i < DBGP_TYPE_MAX; i++)
		rtlpriv->dbg.dbgp_type[i] = 0;

	/*Init Debug flag enable condition */
}

static struct proc_dir_entry *proc_topdir;

static int rtl_proc_get_mac_0(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x000;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_0(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_0, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_0 = {
	.open = dl_proc_open_mac_0,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_1(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x100;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_1(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_1, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_1 = {
	.open = dl_proc_open_mac_1,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_2(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x200;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_2(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_2, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_2 = {
	.open = dl_proc_open_mac_2,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_3(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x300;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_3(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_3, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_3 = {
	.open = dl_proc_open_mac_3,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_4(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x400;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_4(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_4, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_4 = {
	.open = dl_proc_open_mac_4,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_5(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x500;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_5(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_5, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_5 = {
	.open = dl_proc_open_mac_5,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_6(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x600;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_6(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_6, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_6 = {
	.open = dl_proc_open_mac_6,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_mac_7(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i, n, page;
	int max = 0xff;
	page = 0x700;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_read_dword(rtlpriv, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_mac_7(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_mac_7, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_mac_7 = {
	.open = dl_proc_open_mac_7,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_8(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0x800;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_8(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_8, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_8 = {
	.open = dl_proc_open_bb_8,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_9(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0x900;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_9(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_9, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_9 = {
	.open = dl_proc_open_bb_9,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_a(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xa00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_a(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_a, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_a = {
	.open = dl_proc_open_bb_a,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_b(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xb00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_b(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_b, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_b = {
	.open = dl_proc_open_bb_b,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_c(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xc00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_c(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_c, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_c = {
	.open = dl_proc_open_bb_c,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_d(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xd00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_d(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_d, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_d = {
	.open = dl_proc_open_bb_d,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_e(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xe00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_e(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_e, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_e = {
	.open = dl_proc_open_bb_e,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_bb_f(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n, page;
	int max = 0xff;
	page = 0xf00;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtl_get_bbreg(hw, (page | n), 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_bb_f(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_bb_f, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_bb_f = {
	.open = dl_proc_open_bb_f,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_reg_rf_a(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n;
	int max = 0x40;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n);
		for (i = 0; i < 4 && n <= max; n += 1, i++)
			seq_printf(m, "%8.8x    ",
				   rtl_get_rfreg(hw, RF90_PATH_A, n, 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_rf_a(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_reg_rf_a, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_rf_a = {
	.open = dl_proc_open_rf_a,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_reg_rf_b(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	int i, n;
	int max = 0x40;

	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n);
		for (i = 0; i < 4 && n <= max; n += 1, i++)
			seq_printf(m, "%8.8x    ",
				   rtl_get_rfreg(hw, RF90_PATH_B, n,
						 0xffffffff));
	}
	seq_puts(m, "\n");
	return 0;
}

static int dl_proc_open_rf_b(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_reg_rf_b, GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_rf_b = {
	.open = dl_proc_open_rf_b,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_cam_register_1(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 target_cmd = 0;
	u32 target_val = 0;
	u8 entry_i = 0;
	u32 ulstatus;
	int i = 100, j = 0;

	/* This dump the current register page */
	seq_puts(m,
	    "\n#################### SECURITY CAM (0-10) ##################\n ");

	for (j = 0; j < 11; j++) {
		seq_printf(m, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			/* polling bit, and No Write enable, and address  */
			target_cmd = entry_i + CAM_CONTENT_COUNT * j;
			target_cmd = target_cmd | BIT(31);

			/* Check polling bit is clear */
			while ((i--) >= 0) {
				ulstatus = rtl_read_dword(rtlpriv,
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

static int dl_proc_open_cam_1(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_cam_register_1,
			   GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_cam_1 = {
	.open = dl_proc_open_cam_1,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_cam_register_2(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 target_cmd = 0;
	u32 target_val = 0;
	u8 entry_i = 0;
	u32 ulstatus;
	int i = 100, j = 0;

	/* This dump the current register page */
	seq_puts(m,
	    "\n################### SECURITY CAM (11-21) ##################\n ");

	for (j = 11; j < 22; j++) {
		seq_printf(m, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			target_cmd = entry_i + CAM_CONTENT_COUNT * j;
			target_cmd = target_cmd | BIT(31);

			while ((i--) >= 0) {
				ulstatus = rtl_read_dword(rtlpriv,
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

static int dl_proc_open_cam_2(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_cam_register_2,
			   GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_cam_2 = {
	.open = dl_proc_open_cam_2,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int rtl_proc_get_cam_register_3(struct seq_file *m, void *v)
{
	struct ieee80211_hw *hw = m->private;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 target_cmd = 0;
	u32 target_val = 0;
	u8 entry_i = 0;
	u32 ulstatus;
	int i = 100, j = 0;

	/* This dump the current register page */
	seq_puts(m,
	    "\n################### SECURITY CAM (22-31) ##################\n ");

	for (j = 22; j < TOTAL_CAM_ENTRY; j++) {
		seq_printf(m, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			target_cmd = entry_i+CAM_CONTENT_COUNT*j;
			target_cmd = target_cmd | BIT(31);

			while ((i--) >= 0) {
				ulstatus = rtl_read_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM]);
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

static int dl_proc_open_cam_3(struct inode *inode, struct file *file)
{
	return single_open(file, rtl_proc_get_cam_register_3,
			   GET_INODE_DATA(inode));
}

static const struct file_operations file_ops_cam_3 = {
	.open = dl_proc_open_cam_3,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

void rtl_proc_add_one(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct proc_dir_entry *entry;

	snprintf(rtlpriv->dbg.proc_name, 18, "%x-%x-%x-%x-%x-%x",
		 rtlefuse->dev_addr[0], rtlefuse->dev_addr[1],
		 rtlefuse->dev_addr[2], rtlefuse->dev_addr[3],
		 rtlefuse->dev_addr[4], rtlefuse->dev_addr[5]);

	rtlpriv->dbg.proc_dir = proc_mkdir(rtlpriv->dbg.proc_name, proc_topdir);
	if (!rtlpriv->dbg.proc_dir) {
		RT_TRACE(COMP_INIT, DBG_EMERG,
			 ("Unable to init /proc/net/%s/%s\n",
			  rtlpriv->cfg->name,
			  rtlpriv->dbg.proc_name));
		return;
	}

	entry = proc_create_data("mac-0", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_0, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, DBG_EMERG,
			 ("Unable to initialize /proc/net/%s/%s/mac-0\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-1", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_1, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-1\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-2", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_2, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-2\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-3", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_3, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-3\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-4", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_4, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-4\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-5", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_5, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-5\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-6", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_6, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-6\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("mac-7", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_mac_7, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/mac-7\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-8", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_8, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-8\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-9", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_9, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-9\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-a", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_a, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-a\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-b", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_b, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-b\n",
		      rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-c", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_c, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-c\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-d", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_d, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-d\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-e", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_e, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-e\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("bb-f", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_bb_f, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/bb-f\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("rf-a", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_rf_a, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/rf-a\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("rf-b", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_rf_b, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/rf-b\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("cam-1", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_cam_1, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/cam-1\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("cam-2", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_cam_2, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/cam-2\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));

	entry = proc_create_data("cam-3", S_IFREG | S_IRUGO,
				 rtlpriv->dbg.proc_dir, &file_ops_cam_3, hw);
	if (!entry)
		RT_TRACE(COMP_INIT, COMP_ERR,
			 ("Unable to initialize /proc/net/%s/%s/cam-3\n",
			  rtlpriv->cfg->name, rtlpriv->dbg.proc_name));
}

void rtl_proc_remove_one(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dbg.proc_dir) {
		remove_proc_entry("mac-0", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-1", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-2", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-3", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-4", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-5", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-6", rtlpriv->dbg.proc_dir);
		remove_proc_entry("mac-7", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-8", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-9", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-a", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-b", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-c", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-d", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-e", rtlpriv->dbg.proc_dir);
		remove_proc_entry("bb-f", rtlpriv->dbg.proc_dir);
		remove_proc_entry("rf-a", rtlpriv->dbg.proc_dir);
		remove_proc_entry("rf-b", rtlpriv->dbg.proc_dir);
		remove_proc_entry("cam-1", rtlpriv->dbg.proc_dir);
		remove_proc_entry("cam-2", rtlpriv->dbg.proc_dir);
		remove_proc_entry("cam-3", rtlpriv->dbg.proc_dir);

		remove_proc_entry(rtlpriv->dbg.proc_name, proc_topdir);

		rtlpriv->dbg.proc_dir = NULL;
	}
}

void rtl_proc_add_topdir(void)
{
	proc_topdir = proc_mkdir("rtlwifi", init_net.proc_net);
}

void rtl_proc_remove_topdir(void)
{
	if (proc_topdir)
		remove_proc_entry("rtlwifi", init_net.proc_net);
}
