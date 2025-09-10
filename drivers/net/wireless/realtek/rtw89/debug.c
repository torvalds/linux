// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include <linux/vmalloc.h>

#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "pci.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "sar.h"
#include "util.h"

#ifdef CONFIG_RTW89_DEBUGMSG
unsigned int rtw89_debug_mask;
EXPORT_SYMBOL(rtw89_debug_mask);
module_param_named(debug_mask, rtw89_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");
#endif

#ifdef CONFIG_RTW89_DEBUGFS
struct rtw89_debugfs_priv_opt {
	bool rlock:1;
	bool wlock:1;
	size_t rsize;
};

struct rtw89_debugfs_priv {
	struct rtw89_dev *rtwdev;
	ssize_t (*cb_read)(struct rtw89_dev *rtwdev,
			   struct rtw89_debugfs_priv *debugfs_priv,
			   char *buf, size_t bufsz);
	ssize_t (*cb_write)(struct rtw89_dev *rtwdev,
			    struct rtw89_debugfs_priv *debugfs_priv,
			    const char *buf, size_t count);
	struct rtw89_debugfs_priv_opt opt;
	union {
		u32 cb_data;
		struct {
			u32 addr;
			u32 len;
		} read_reg;
		struct {
			u32 addr;
			u32 mask;
			u8 path;
		} read_rf;
		struct {
			u8 ss_dbg:1;
			u8 dle_dbg:1;
			u8 dmac_dbg:1;
			u8 cmac_dbg:1;
			u8 dbg_port:1;
		} dbgpkg_en;
		struct {
			u32 start;
			u32 len;
			u8 sel;
		} mac_mem;
	};
	ssize_t rused;
	char *rbuf;
};

struct rtw89_debugfs {
	struct rtw89_debugfs_priv read_reg;
	struct rtw89_debugfs_priv write_reg;
	struct rtw89_debugfs_priv read_rf;
	struct rtw89_debugfs_priv write_rf;
	struct rtw89_debugfs_priv rf_reg_dump;
	struct rtw89_debugfs_priv txpwr_table;
	struct rtw89_debugfs_priv mac_reg_dump;
	struct rtw89_debugfs_priv mac_mem_dump;
	struct rtw89_debugfs_priv mac_dbg_port_dump;
	struct rtw89_debugfs_priv send_h2c;
	struct rtw89_debugfs_priv early_h2c;
	struct rtw89_debugfs_priv fw_crash;
	struct rtw89_debugfs_priv btc_info;
	struct rtw89_debugfs_priv btc_manual;
	struct rtw89_debugfs_priv fw_log_manual;
	struct rtw89_debugfs_priv phy_info;
	struct rtw89_debugfs_priv stations;
	struct rtw89_debugfs_priv disable_dm;
	struct rtw89_debugfs_priv mlo_mode;
};

struct rtw89_debugfs_iter_data {
	char *buf;
	size_t bufsz;
	int written_sz;
};

static void rtw89_debugfs_iter_data_setup(struct rtw89_debugfs_iter_data *iter_data,
					  char *buf, size_t bufsz)
{
	iter_data->buf = buf;
	iter_data->bufsz = bufsz;
	iter_data->written_sz = 0;
}

static void rtw89_debugfs_iter_data_next(struct rtw89_debugfs_iter_data *iter_data,
					 char *buf, size_t bufsz, int written_sz)
{
	iter_data->buf = buf;
	iter_data->bufsz = bufsz;
	iter_data->written_sz += written_sz;
}

static const u16 rtw89_rate_info_bw_to_mhz_map[] = {
	[RATE_INFO_BW_20] = 20,
	[RATE_INFO_BW_40] = 40,
	[RATE_INFO_BW_80] = 80,
	[RATE_INFO_BW_160] = 160,
	[RATE_INFO_BW_320] = 320,
};

static u16 rtw89_rate_info_bw_to_mhz(enum rate_info_bw bw)
{
	if (bw < ARRAY_SIZE(rtw89_rate_info_bw_to_mhz_map))
		return rtw89_rate_info_bw_to_mhz_map[bw];

	return 0;
}

static ssize_t rtw89_debugfs_file_read_helper(struct wiphy *wiphy, struct file *file,
					      char *buf, size_t bufsz, void *data)
{
	struct rtw89_debugfs_priv *debugfs_priv = data;
	struct rtw89_dev *rtwdev = debugfs_priv->rtwdev;
	ssize_t n;

	n = debugfs_priv->cb_read(rtwdev, debugfs_priv, buf, bufsz);
	rtw89_might_trailing_ellipsis(buf, bufsz, n);

	return n;
}

static ssize_t rtw89_debugfs_file_read(struct file *file, char __user *userbuf,
				       size_t count, loff_t *ppos)
{
	struct rtw89_debugfs_priv *debugfs_priv = file->private_data;
	struct rtw89_debugfs_priv_opt *opt = &debugfs_priv->opt;
	struct rtw89_dev *rtwdev = debugfs_priv->rtwdev;
	size_t bufsz = opt->rsize ? opt->rsize : PAGE_SIZE;
	char *buf;
	ssize_t n;

	if (!debugfs_priv->rbuf)
		debugfs_priv->rbuf = devm_kzalloc(rtwdev->dev, bufsz, GFP_KERNEL);

	buf = debugfs_priv->rbuf;
	if (!buf)
		return -ENOMEM;

	if (*ppos) {
		n = debugfs_priv->rused;
		goto out;
	}

	if (opt->rlock) {
		n = wiphy_locked_debugfs_read(rtwdev->hw->wiphy, file, buf, bufsz,
					      userbuf, count, ppos,
					      rtw89_debugfs_file_read_helper,
					      debugfs_priv);
		debugfs_priv->rused = n;

		return n;
	}

	n = rtw89_debugfs_file_read_helper(rtwdev->hw->wiphy, file, buf, bufsz,
					   debugfs_priv);
	debugfs_priv->rused = n;

out:
	return simple_read_from_buffer(userbuf, count, ppos, buf, n);
}

static ssize_t rtw89_debugfs_file_write_helper(struct wiphy *wiphy, struct file *file,
					       char *buf, size_t count, void *data)
{
	struct rtw89_debugfs_priv *debugfs_priv = data;
	struct rtw89_dev *rtwdev = debugfs_priv->rtwdev;

	return debugfs_priv->cb_write(rtwdev, debugfs_priv, buf, count);
}

static ssize_t rtw89_debugfs_file_write(struct file *file,
					const char __user *userbuf,
					size_t count, loff_t *loff)
{
	struct rtw89_debugfs_priv *debugfs_priv = file->private_data;
	struct rtw89_debugfs_priv_opt *opt = &debugfs_priv->opt;
	struct rtw89_dev *rtwdev = debugfs_priv->rtwdev;
	char *buf __free(kfree) = kmalloc(count + 1, GFP_KERNEL);
	ssize_t n;

	if (!buf)
		return -ENOMEM;

	if (opt->wlock) {
		n = wiphy_locked_debugfs_write(rtwdev->hw->wiphy,
					       file, buf, count + 1,
					       userbuf, count,
					       rtw89_debugfs_file_write_helper,
					       debugfs_priv);
		return n;
	}

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[count] = '\0';

	return debugfs_priv->cb_write(rtwdev, debugfs_priv, buf, count);
}

static const struct debugfs_short_fops file_ops_single_r = {
	.read = rtw89_debugfs_file_read,
	.llseek = generic_file_llseek,
};

static const struct debugfs_short_fops file_ops_common_rw = {
	.read = rtw89_debugfs_file_read,
	.write = rtw89_debugfs_file_write,
	.llseek = generic_file_llseek,
};

static const struct debugfs_short_fops file_ops_single_w = {
	.write = rtw89_debugfs_file_write,
	.llseek = generic_file_llseek,
};

static ssize_t
rtw89_debug_priv_read_reg_select(struct rtw89_dev *rtwdev,
				 struct rtw89_debugfs_priv *debugfs_priv,
				 const char *buf, size_t count)
{
	u32 addr, len;
	int num;

	num = sscanf(buf, "%x %x", &addr, &len);
	if (num != 2) {
		rtw89_info(rtwdev, "invalid format: <addr> <len>\n");
		return -EINVAL;
	}

	debugfs_priv->read_reg.addr = addr;
	debugfs_priv->read_reg.len = len;

	rtw89_info(rtwdev, "select read %d bytes from 0x%08x\n", len, addr);

	return count;
}

static
ssize_t rtw89_debug_priv_read_reg_get(struct rtw89_dev *rtwdev,
				      struct rtw89_debugfs_priv *debugfs_priv,
				      char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;
	u32 addr, addr_end, data, k;
	u32 len;

	len = debugfs_priv->read_reg.len;
	addr = debugfs_priv->read_reg.addr;

	if (len > 4)
		goto ndata;

	switch (len) {
	case 1:
		data = rtw89_read8(rtwdev, addr);
		break;
	case 2:
		data = rtw89_read16(rtwdev, addr);
		break;
	case 4:
		data = rtw89_read32(rtwdev, addr);
		break;
	default:
		rtw89_info(rtwdev, "invalid read reg len %d\n", len);
		return -EINVAL;
	}

	p += scnprintf(p, end - p, "get %d bytes at 0x%08x=0x%08x\n", len,
		       addr, data);

	return p - buf;

ndata:
	addr_end = addr + len;

	for (; addr < addr_end; addr += 16) {
		p += scnprintf(p, end - p, "%08xh : ", 0x18600000 + addr);
		for (k = 0; k < 16; k += 4) {
			data = rtw89_read32(rtwdev, addr + k);
			p += scnprintf(p, end - p, "%08x ", data);
		}
		p += scnprintf(p, end - p, "\n");
	}

	return p - buf;
}

static
ssize_t rtw89_debug_priv_write_reg_set(struct rtw89_dev *rtwdev,
				       struct rtw89_debugfs_priv *debugfs_priv,
				       const char *buf, size_t count)
{
	u32 addr, val, len;
	int num;

	num = sscanf(buf, "%x %x %x", &addr, &val, &len);
	if (num !=  3) {
		rtw89_info(rtwdev, "invalid format: <addr> <val> <len>\n");
		return -EINVAL;
	}

	switch (len) {
	case 1:
		rtw89_info(rtwdev, "reg write8 0x%08x: 0x%02x\n", addr, val);
		rtw89_write8(rtwdev, addr, (u8)val);
		break;
	case 2:
		rtw89_info(rtwdev, "reg write16 0x%08x: 0x%04x\n", addr, val);
		rtw89_write16(rtwdev, addr, (u16)val);
		break;
	case 4:
		rtw89_info(rtwdev, "reg write32 0x%08x: 0x%08x\n", addr, val);
		rtw89_write32(rtwdev, addr, (u32)val);
		break;
	default:
		rtw89_info(rtwdev, "invalid read write len %d\n", len);
		break;
	}

	return count;
}

static ssize_t
rtw89_debug_priv_read_rf_select(struct rtw89_dev *rtwdev,
				struct rtw89_debugfs_priv *debugfs_priv,
				const char *buf, size_t count)
{
	u32 addr, mask;
	u8 path;
	int num;

	num = sscanf(buf, "%hhd %x %x", &path, &addr, &mask);
	if (num != 3) {
		rtw89_info(rtwdev, "invalid format: <path> <addr> <mask>\n");
		return -EINVAL;
	}

	if (path >= rtwdev->chip->rf_path_num) {
		rtw89_info(rtwdev, "wrong rf path\n");
		return -EINVAL;
	}
	debugfs_priv->read_rf.addr = addr;
	debugfs_priv->read_rf.mask = mask;
	debugfs_priv->read_rf.path = path;

	rtw89_info(rtwdev, "select read rf path %d from 0x%08x\n", path, addr);

	return count;
}

static
ssize_t rtw89_debug_priv_read_rf_get(struct rtw89_dev *rtwdev,
				     struct rtw89_debugfs_priv *debugfs_priv,
				     char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;
	u32 addr, data, mask;
	u8 path;

	addr = debugfs_priv->read_rf.addr;
	mask = debugfs_priv->read_rf.mask;
	path = debugfs_priv->read_rf.path;

	data = rtw89_read_rf(rtwdev, path, addr, mask);

	p += scnprintf(p, end - p, "path %d, rf register 0x%08x=0x%08x\n",
		       path, addr, data);

	return p - buf;
}

static
ssize_t rtw89_debug_priv_write_rf_set(struct rtw89_dev *rtwdev,
				      struct rtw89_debugfs_priv *debugfs_priv,
				      const char *buf, size_t count)
{
	u32 addr, val, mask;
	u8 path;
	int num;

	num = sscanf(buf, "%hhd %x %x %x", &path, &addr, &mask, &val);
	if (num != 4) {
		rtw89_info(rtwdev, "invalid format: <path> <addr> <mask> <val>\n");
		return -EINVAL;
	}

	if (path >= rtwdev->chip->rf_path_num) {
		rtw89_info(rtwdev, "wrong rf path\n");
		return -EINVAL;
	}

	rtw89_info(rtwdev, "path %d, rf register write 0x%08x=0x%08x (mask = 0x%08x)\n",
		   path, addr, val, mask);
	rtw89_write_rf(rtwdev, path, addr, mask, val);

	return count;
}

static
ssize_t rtw89_debug_priv_rf_reg_dump_get(struct rtw89_dev *rtwdev,
					 struct rtw89_debugfs_priv *debugfs_priv,
					 char *buf, size_t bufsz)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	char *p = buf, *end = buf + bufsz;
	u32 addr, offset, data;
	u8 path;

	for (path = 0; path < chip->rf_path_num; path++) {
		p += scnprintf(p, end - p, "RF path %d:\n\n", path);
		for (addr = 0; addr < 0x100; addr += 4) {
			p += scnprintf(p, end - p, "0x%08x: ", addr);
			for (offset = 0; offset < 4; offset++) {
				data = rtw89_read_rf(rtwdev, path,
						     addr + offset, RFREG_MASK);
				p += scnprintf(p, end - p, "0x%05x  ", data);
			}
			p += scnprintf(p, end - p, "\n");
		}
		p += scnprintf(p, end - p, "\n");
	}

	return p - buf;
}

struct txpwr_ent {
	bool nested;
	union {
		const char *txt;
		const struct txpwr_ent *ptr;
	};
	u8 len;
};

struct txpwr_map {
	const struct txpwr_ent *ent;
	u8 size;
	u32 addr_from;
	u32 addr_to;
	u32 addr_to_1ss;
};

#define __GEN_TXPWR_ENT_NESTED(_e) \
	{ .nested = true, .ptr = __txpwr_ent_##_e, \
	  .len = ARRAY_SIZE(__txpwr_ent_##_e) }

#define __GEN_TXPWR_ENT0(_t) { .len = 0, .txt = _t }

#define __GEN_TXPWR_ENT2(_t, _e0, _e1) \
	{ .len = 2, .txt = _t "\t-  " _e0 "  " _e1 }

#define __GEN_TXPWR_ENT4(_t, _e0, _e1, _e2, _e3) \
	{ .len = 4, .txt = _t "\t-  " _e0 "  " _e1 "  " _e2 "  " _e3 }

#define __GEN_TXPWR_ENT8(_t, _e0, _e1, _e2, _e3, _e4, _e5, _e6, _e7) \
	{ .len = 8, .txt = _t "\t-  " \
	  _e0 "  " _e1 "  " _e2 "  " _e3 "  " \
	  _e4 "  " _e5 "  " _e6 "  " _e7 }

static const struct txpwr_ent __txpwr_ent_byr_ax[] = {
	__GEN_TXPWR_ENT4("CCK       ", "1M   ", "2M   ", "5.5M ", "11M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M   ", "9M   ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M  ", "36M  ", "48M  ", "54M  "),
	/* 1NSS */
	__GEN_TXPWR_ENT4("MCS_1NSS  ", "MCS0 ", "MCS1 ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("MCS_1NSS  ", "MCS4 ", "MCS5 ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("MCS_1NSS  ", "MCS8 ", "MCS9 ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT4("HEDCM_1NSS", "MCS0 ", "MCS1 ", "MCS3 ", "MCS4 "),
	/* 2NSS */
	__GEN_TXPWR_ENT4("MCS_2NSS  ", "MCS0 ", "MCS1 ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("MCS_2NSS  ", "MCS4 ", "MCS5 ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("MCS_2NSS  ", "MCS8 ", "MCS9 ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT4("HEDCM_2NSS", "MCS0 ", "MCS1 ", "MCS3 ", "MCS4 "),
};

static_assert((ARRAY_SIZE(__txpwr_ent_byr_ax) * 4) ==
	(R_AX_PWR_BY_RATE_MAX - R_AX_PWR_BY_RATE + 4));

static const struct txpwr_map __txpwr_map_byr_ax = {
	.ent = __txpwr_ent_byr_ax,
	.size = ARRAY_SIZE(__txpwr_ent_byr_ax),
	.addr_from = R_AX_PWR_BY_RATE,
	.addr_to = R_AX_PWR_BY_RATE_MAX,
	.addr_to_1ss = R_AX_PWR_BY_RATE_1SS_MAX,
};

static const struct txpwr_ent __txpwr_ent_lmt_ax[] = {
	/* 1TX */
	__GEN_TXPWR_ENT2("CCK_1TX_20M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("CCK_1TX_40M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("OFDM_1TX       ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_4  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_5  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_6  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_20M_7  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_80M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_80M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_160M   ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_0p5", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_1TX_40M_2p5", "NON_BF", "BF"),
	/* 2TX */
	__GEN_TXPWR_ENT2("CCK_2TX_20M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("CCK_2TX_40M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("OFDM_2TX       ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_4  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_5  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_6  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_20M_7  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_80M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_80M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_160M   ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_0p5", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_2TX_40M_2p5", "NON_BF", "BF"),
};

static_assert((ARRAY_SIZE(__txpwr_ent_lmt_ax) * 2) ==
	(R_AX_PWR_LMT_MAX - R_AX_PWR_LMT + 4));

static const struct txpwr_map __txpwr_map_lmt_ax = {
	.ent = __txpwr_ent_lmt_ax,
	.size = ARRAY_SIZE(__txpwr_ent_lmt_ax),
	.addr_from = R_AX_PWR_LMT,
	.addr_to = R_AX_PWR_LMT_MAX,
	.addr_to_1ss = R_AX_PWR_LMT_1SS_MAX,
};

static const struct txpwr_ent __txpwr_ent_lmt_ru_ax[] = {
	/* 1TX */
	__GEN_TXPWR_ENT8("1TX", "RU26__0", "RU26__1", "RU26__2", "RU26__3",
			 "RU26__4", "RU26__5", "RU26__6", "RU26__7"),
	__GEN_TXPWR_ENT8("1TX", "RU52__0", "RU52__1", "RU52__2", "RU52__3",
			 "RU52__4", "RU52__5", "RU52__6", "RU52__7"),
	__GEN_TXPWR_ENT8("1TX", "RU106_0", "RU106_1", "RU106_2", "RU106_3",
			 "RU106_4", "RU106_5", "RU106_6", "RU106_7"),
	/* 2TX */
	__GEN_TXPWR_ENT8("2TX", "RU26__0", "RU26__1", "RU26__2", "RU26__3",
			 "RU26__4", "RU26__5", "RU26__6", "RU26__7"),
	__GEN_TXPWR_ENT8("2TX", "RU52__0", "RU52__1", "RU52__2", "RU52__3",
			 "RU52__4", "RU52__5", "RU52__6", "RU52__7"),
	__GEN_TXPWR_ENT8("2TX", "RU106_0", "RU106_1", "RU106_2", "RU106_3",
			 "RU106_4", "RU106_5", "RU106_6", "RU106_7"),
};

static_assert((ARRAY_SIZE(__txpwr_ent_lmt_ru_ax) * 8) ==
	(R_AX_PWR_RU_LMT_MAX - R_AX_PWR_RU_LMT + 4));

static const struct txpwr_map __txpwr_map_lmt_ru_ax = {
	.ent = __txpwr_ent_lmt_ru_ax,
	.size = ARRAY_SIZE(__txpwr_ent_lmt_ru_ax),
	.addr_from = R_AX_PWR_RU_LMT,
	.addr_to = R_AX_PWR_RU_LMT_MAX,
	.addr_to_1ss = R_AX_PWR_RU_LMT_1SS_MAX,
};

static const struct txpwr_ent __txpwr_ent_byr_mcs_be[] = {
	__GEN_TXPWR_ENT4("MCS_1SS       ", "MCS0  ", "MCS1  ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("MCS_1SS       ", "MCS4  ", "MCS5  ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("MCS_1SS       ", "MCS8  ", "MCS9  ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT2("MCS_1SS       ", "MCS12 ", "MCS13 \t"),
	__GEN_TXPWR_ENT4("HEDCM_1SS     ", "MCS0  ", "MCS1  ", "MCS3 ", "MCS4 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_1SS  ", "MCS0  ", "MCS1  ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_1SS  ", "MCS4  ", "MCS5  ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_1SS  ", "MCS8  ", "MCS9  ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT2("DLRU_MCS_1SS  ", "MCS12 ", "MCS13 \t"),
	__GEN_TXPWR_ENT4("DLRU_HEDCM_1SS", "MCS0  ", "MCS1  ", "MCS3 ", "MCS4 "),
	__GEN_TXPWR_ENT4("MCS_2SS       ", "MCS0  ", "MCS1  ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("MCS_2SS       ", "MCS4  ", "MCS5  ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("MCS_2SS       ", "MCS8  ", "MCS9  ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT2("MCS_2SS       ", "MCS12 ", "MCS13 \t"),
	__GEN_TXPWR_ENT4("HEDCM_2SS     ", "MCS0  ", "MCS1  ", "MCS3 ", "MCS4 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_2SS  ", "MCS0  ", "MCS1  ", "MCS2 ", "MCS3 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_2SS  ", "MCS4  ", "MCS5  ", "MCS6 ", "MCS7 "),
	__GEN_TXPWR_ENT4("DLRU_MCS_2SS  ", "MCS8  ", "MCS9  ", "MCS10", "MCS11"),
	__GEN_TXPWR_ENT2("DLRU_MCS_2SS  ", "MCS12 ", "MCS13 \t"),
	__GEN_TXPWR_ENT4("DLRU_HEDCM_2SS", "MCS0  ", "MCS1  ", "MCS3 ", "MCS4 "),
};

static const struct txpwr_ent __txpwr_ent_byr_be[] = {
	__GEN_TXPWR_ENT0("BW20"),
	__GEN_TXPWR_ENT4("CCK       ", "1M    ", "2M    ", "5.5M ", "11M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M    ", "9M    ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M   ", "36M   ", "48M  ", "54M  "),
	__GEN_TXPWR_ENT2("EHT       ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT2("DLRU_EHT  ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT_NESTED(byr_mcs_be),

	__GEN_TXPWR_ENT0("BW40"),
	__GEN_TXPWR_ENT4("CCK       ", "1M    ", "2M    ", "5.5M ", "11M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M    ", "9M    ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M   ", "36M   ", "48M  ", "54M  "),
	__GEN_TXPWR_ENT2("EHT       ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT2("DLRU_EHT  ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT_NESTED(byr_mcs_be),

	/* there is no CCK section after BW80 */
	__GEN_TXPWR_ENT0("BW80"),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M    ", "9M    ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M   ", "36M   ", "48M  ", "54M  "),
	__GEN_TXPWR_ENT2("EHT       ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT2("DLRU_EHT  ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT_NESTED(byr_mcs_be),

	__GEN_TXPWR_ENT0("BW160"),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M    ", "9M    ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M   ", "36M   ", "48M  ", "54M  "),
	__GEN_TXPWR_ENT2("EHT       ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT2("DLRU_EHT  ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT_NESTED(byr_mcs_be),

	__GEN_TXPWR_ENT0("BW320"),
	__GEN_TXPWR_ENT4("LEGACY    ", "6M    ", "9M    ", "12M  ", "18M  "),
	__GEN_TXPWR_ENT4("LEGACY    ", "24M   ", "36M   ", "48M  ", "54M  "),
	__GEN_TXPWR_ENT2("EHT       ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT2("DLRU_EHT  ", "MCS14 ", "MCS15 \t"),
	__GEN_TXPWR_ENT_NESTED(byr_mcs_be),
};

static const struct txpwr_map __txpwr_map_byr_be = {
	.ent = __txpwr_ent_byr_be,
	.size = ARRAY_SIZE(__txpwr_ent_byr_be),
	.addr_from = R_BE_PWR_BY_RATE,
	.addr_to = R_BE_PWR_BY_RATE_MAX,
	.addr_to_1ss = 0, /* not support */
};

static const struct txpwr_ent __txpwr_ent_lmt_mcs_be[] = {
	__GEN_TXPWR_ENT2("MCS_20M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_4  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_5  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_6  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_7  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_8  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_9  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_10 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_11 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_12 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_13 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_14 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_20M_15 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_4  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_5  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_6  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_7  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_80M_0  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_80M_1  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_80M_2  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_80M_3  ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_160M_0 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_160M_1 ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_320M   ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_0p5", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_2p5", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_4p5", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("MCS_40M_6p5", "NON_BF", "BF"),
};

static const struct txpwr_ent __txpwr_ent_lmt_be[] = {
	__GEN_TXPWR_ENT0("1TX"),
	__GEN_TXPWR_ENT2("CCK_20M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("CCK_40M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("OFDM       ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT_NESTED(lmt_mcs_be),

	__GEN_TXPWR_ENT0("2TX"),
	__GEN_TXPWR_ENT2("CCK_20M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("CCK_40M    ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT2("OFDM       ", "NON_BF", "BF"),
	__GEN_TXPWR_ENT_NESTED(lmt_mcs_be),
};

static const struct txpwr_map __txpwr_map_lmt_be = {
	.ent = __txpwr_ent_lmt_be,
	.size = ARRAY_SIZE(__txpwr_ent_lmt_be),
	.addr_from = R_BE_PWR_LMT,
	.addr_to = R_BE_PWR_LMT_MAX,
	.addr_to_1ss = 0, /* not support */
};

static const struct txpwr_ent __txpwr_ent_lmt_ru_indexes_be[] = {
	__GEN_TXPWR_ENT8("RU26    ", "IDX_0 ", "IDX_1 ", "IDX_2 ", "IDX_3 ",
			 "IDX_4 ", "IDX_5 ", "IDX_6 ", "IDX_7 "),
	__GEN_TXPWR_ENT8("RU26    ", "IDX_8 ", "IDX_9 ", "IDX_10", "IDX_11",
			 "IDX_12", "IDX_13", "IDX_14", "IDX_15"),
	__GEN_TXPWR_ENT8("RU52    ", "IDX_0 ", "IDX_1 ", "IDX_2 ", "IDX_3 ",
			 "IDX_4 ", "IDX_5 ", "IDX_6 ", "IDX_7 "),
	__GEN_TXPWR_ENT8("RU52    ", "IDX_8 ", "IDX_9 ", "IDX_10", "IDX_11",
			 "IDX_12", "IDX_13", "IDX_14", "IDX_15"),
	__GEN_TXPWR_ENT8("RU106   ", "IDX_0 ", "IDX_1 ", "IDX_2 ", "IDX_3 ",
			 "IDX_4 ", "IDX_5 ", "IDX_6 ", "IDX_7 "),
	__GEN_TXPWR_ENT8("RU106   ", "IDX_8 ", "IDX_9 ", "IDX_10", "IDX_11",
			 "IDX_12", "IDX_13", "IDX_14", "IDX_15"),
	__GEN_TXPWR_ENT8("RU52_26 ", "IDX_0 ", "IDX_1 ", "IDX_2 ", "IDX_3 ",
			 "IDX_4 ", "IDX_5 ", "IDX_6 ", "IDX_7 "),
	__GEN_TXPWR_ENT8("RU52_26 ", "IDX_8 ", "IDX_9 ", "IDX_10", "IDX_11",
			 "IDX_12", "IDX_13", "IDX_14", "IDX_15"),
	__GEN_TXPWR_ENT8("RU106_26", "IDX_0 ", "IDX_1 ", "IDX_2 ", "IDX_3 ",
			 "IDX_4 ", "IDX_5 ", "IDX_6 ", "IDX_7 "),
	__GEN_TXPWR_ENT8("RU106_26", "IDX_8 ", "IDX_9 ", "IDX_10", "IDX_11",
			 "IDX_12", "IDX_13", "IDX_14", "IDX_15"),
};

static const struct txpwr_ent __txpwr_ent_lmt_ru_be[] = {
	__GEN_TXPWR_ENT0("1TX"),
	__GEN_TXPWR_ENT_NESTED(lmt_ru_indexes_be),

	__GEN_TXPWR_ENT0("2TX"),
	__GEN_TXPWR_ENT_NESTED(lmt_ru_indexes_be),
};

static const struct txpwr_map __txpwr_map_lmt_ru_be = {
	.ent = __txpwr_ent_lmt_ru_be,
	.size = ARRAY_SIZE(__txpwr_ent_lmt_ru_be),
	.addr_from = R_BE_PWR_RU_LMT,
	.addr_to = R_BE_PWR_RU_LMT_MAX,
	.addr_to_1ss = 0, /* not support */
};

static unsigned int
__print_txpwr_ent(char *buf, size_t bufsz, const struct txpwr_ent *ent,
		  const s8 *bufp, const unsigned int cur, unsigned int *ate)
{
	char *p = buf, *end = buf + bufsz;
	unsigned int cnt, i;
	unsigned int eaten;
	char *fmt;

	if (ent->nested) {
		for (cnt = 0, i = 0; i < ent->len; i++, cnt += eaten)
			p += __print_txpwr_ent(p, end - p, ent->ptr + i, bufp,
					       cur + cnt, &eaten);
		*ate = cnt;
		goto out;
	}

	switch (ent->len) {
	case 0:
		p += scnprintf(p, end - p, "\t<< %s >>\n", ent->txt);
		*ate = 0;
		goto out;
	case 2:
		fmt = "%s\t| %3d, %3d,\t\tdBm\n";
		p += scnprintf(p, end - p, fmt, ent->txt, bufp[cur],
			       bufp[cur + 1]);
		*ate = 2;
		goto out;
	case 4:
		fmt = "%s\t| %3d, %3d, %3d, %3d,\tdBm\n";
		p += scnprintf(p, end - p, fmt, ent->txt, bufp[cur],
			       bufp[cur + 1],
			       bufp[cur + 2], bufp[cur + 3]);
		*ate = 4;
		goto out;
	case 8:
		fmt = "%s\t| %3d, %3d, %3d, %3d, %3d, %3d, %3d, %3d,\tdBm\n";
		p += scnprintf(p, end - p, fmt, ent->txt, bufp[cur],
			       bufp[cur + 1],
			       bufp[cur + 2], bufp[cur + 3], bufp[cur + 4],
			       bufp[cur + 5], bufp[cur + 6], bufp[cur + 7]);
		*ate = 8;
		goto out;
	default:
		return 0;
	}

out:
	return p - buf;
}

static ssize_t __print_txpwr_map(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
				 const struct txpwr_map *map)
{
	u8 fct = rtwdev->chip->txpwr_factor_mac;
	u8 path_num = rtwdev->chip->rf_path_num;
	char *p = buf, *end = buf + bufsz;
	unsigned int cur, i;
	unsigned int eaten;
	u32 max_valid_addr;
	u32 val, addr;
	s8 *bufp, tmp;
	int ret;

	bufp = vzalloc(map->addr_to - map->addr_from + 4);
	if (!bufp)
		return -ENOMEM;

	if (path_num == 1)
		max_valid_addr = map->addr_to_1ss;
	else
		max_valid_addr = map->addr_to;

	if (max_valid_addr == 0)
		return -EOPNOTSUPP;

	for (addr = map->addr_from; addr <= max_valid_addr; addr += 4) {
		ret = rtw89_mac_txpwr_read32(rtwdev, RTW89_PHY_0, addr, &val);
		if (ret)
			val = MASKDWORD;

		cur = addr - map->addr_from;
		for (i = 0; i < 4; i++, val >>= 8) {
			/* signed 7 bits, and reserved BIT(7) */
			tmp = sign_extend32(val, 6);
			bufp[cur + i] = tmp >> fct;
		}
	}

	for (cur = 0, i = 0; i < map->size; i++, cur += eaten)
		p += __print_txpwr_ent(p, end - p, &map->ent[i], bufp, cur, &eaten);

	vfree(bufp);
	return p - buf;
}

static int __print_regd(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
			const struct rtw89_chan *chan)
{
	const struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	char *p = buf, *end = buf + bufsz;
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);

	p += scnprintf(p, end - p, "%s\n", rtw89_regd_get_string(regd));
	p += scnprintf(p, end - p, "\t(txpwr UK follow ETSI: %s)\n",
		       str_yes_no(regulatory->txpwr_uk_follow_etsi));

	return p - buf;
}

struct dbgfs_txpwr_table {
	const struct txpwr_map *byr;
	const struct txpwr_map *lmt;
	const struct txpwr_map *lmt_ru;
};

static const struct dbgfs_txpwr_table dbgfs_txpwr_table_ax = {
	.byr = &__txpwr_map_byr_ax,
	.lmt = &__txpwr_map_lmt_ax,
	.lmt_ru = &__txpwr_map_lmt_ru_ax,
};

static const struct dbgfs_txpwr_table dbgfs_txpwr_table_be = {
	.byr = &__txpwr_map_byr_be,
	.lmt = &__txpwr_map_lmt_be,
	.lmt_ru = &__txpwr_map_lmt_ru_be,
};

static const struct dbgfs_txpwr_table *dbgfs_txpwr_tables[RTW89_CHIP_GEN_NUM] = {
	[RTW89_CHIP_AX] = &dbgfs_txpwr_table_ax,
	[RTW89_CHIP_BE] = &dbgfs_txpwr_table_be,
};

static
int rtw89_debug_priv_txpwr_table_get_regd(struct rtw89_dev *rtwdev,
					  char *buf, size_t bufsz,
					  const struct rtw89_chan *chan)
{
	const struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_reg_6ghz_tpe *tpe6 = &regulatory->reg_6ghz_tpe;
	char *p = buf, *end = buf + bufsz;

	p += scnprintf(p, end - p, "[Chanctx] band %u, ch %u, bw %u\n",
		       chan->band_type, chan->channel, chan->band_width);

	p += scnprintf(p, end - p, "[Regulatory] ");
	p += __print_regd(rtwdev, p, end - p, chan);

	if (chan->band_type == RTW89_BAND_6G) {
		p += scnprintf(p, end - p, "[reg6_pwr_type] %u\n",
			       regulatory->reg_6ghz_power);

		if (tpe6->valid)
			p += scnprintf(p, end - p, "[TPE] %d dBm\n",
				       tpe6->constraint);
	}

	return p - buf;
}

static
ssize_t rtw89_debug_priv_txpwr_table_get(struct rtw89_dev *rtwdev,
					 struct rtw89_debugfs_priv *debugfs_priv,
					 char *buf, size_t bufsz)
{
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;
	struct rtw89_sar_parm sar_parm = {};
	const struct dbgfs_txpwr_table *tbl;
	const struct rtw89_chan *chan;
	char *p = buf, *end = buf + bufsz;
	ssize_t n;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	rtw89_leave_ps_mode(rtwdev);
	chan = rtw89_chan_get(rtwdev, RTW89_CHANCTX_0);
	sar_parm.center_freq = chan->freq;

	p += rtw89_debug_priv_txpwr_table_get_regd(rtwdev, p, end - p, chan);

	p += scnprintf(p, end - p, "[SAR]\n");
	p += rtw89_print_sar(rtwdev, p, end - p, &sar_parm);

	p += scnprintf(p, end - p, "[TAS]\n");
	p += rtw89_print_tas(rtwdev, p, end - p);

	p += scnprintf(p, end - p, "[DAG]\n");
	p += rtw89_print_ant_gain(rtwdev, p, end - p, chan);

	tbl = dbgfs_txpwr_tables[chip_gen];
	if (!tbl)
		return -EOPNOTSUPP;

	p += scnprintf(p, end - p, "\n[TX power byrate]\n");
	n = __print_txpwr_map(rtwdev, p, end - p, tbl->byr);
	if (n < 0)
		return n;
	p += n;

	p += scnprintf(p, end - p, "\n[TX power limit]\n");
	n = __print_txpwr_map(rtwdev, p, end - p, tbl->lmt);
	if (n < 0)
		return n;
	p += n;

	p += scnprintf(p, end - p, "\n[TX power limit_ru]\n");
	n = __print_txpwr_map(rtwdev, p, end - p, tbl->lmt_ru);
	if (n < 0)
		return n;
	p += n;

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mac_reg_dump_select(struct rtw89_dev *rtwdev,
				     struct rtw89_debugfs_priv *debugfs_priv,
				     const char *buf, size_t count)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	int sel;
	int ret;

	ret = kstrtoint(buf, 0, &sel);
	if (ret)
		return ret;

	if (sel < RTW89_DBG_SEL_MAC_00 || sel > RTW89_DBG_SEL_RFC) {
		rtw89_info(rtwdev, "invalid args: %d\n", sel);
		return -EINVAL;
	}

	if (sel == RTW89_DBG_SEL_MAC_30 && chip->chip_id != RTL8852C) {
		rtw89_info(rtwdev, "sel %d is address hole on chip %d\n", sel,
			   chip->chip_id);
		return -EINVAL;
	}

	debugfs_priv->cb_data = sel;
	rtw89_info(rtwdev, "select mac page dump %d\n", debugfs_priv->cb_data);

	return count;
}

#define RTW89_MAC_PAGE_SIZE		0x100

static
ssize_t rtw89_debug_priv_mac_reg_dump_get(struct rtw89_dev *rtwdev,
					  struct rtw89_debugfs_priv *debugfs_priv,
					  char *buf, size_t bufsz)
{
	enum rtw89_debug_mac_reg_sel reg_sel = debugfs_priv->cb_data;
	char *p = buf, *end = buf + bufsz;
	u32 start, end_addr;
	u32 i, j, k, page;
	u32 val;

	switch (reg_sel) {
	case RTW89_DBG_SEL_MAC_00:
		p += scnprintf(p, end - p, "Debug selected MAC page 0x00\n");
		start = 0x000;
		end_addr = 0x014;
		break;
	case RTW89_DBG_SEL_MAC_30:
		p += scnprintf(p, end - p, "Debug selected MAC page 0x30\n");
		start = 0x030;
		end_addr = 0x033;
		break;
	case RTW89_DBG_SEL_MAC_40:
		p += scnprintf(p, end - p, "Debug selected MAC page 0x40\n");
		start = 0x040;
		end_addr = 0x07f;
		break;
	case RTW89_DBG_SEL_MAC_80:
		p += scnprintf(p, end - p, "Debug selected MAC page 0x80\n");
		start = 0x080;
		end_addr = 0x09f;
		break;
	case RTW89_DBG_SEL_MAC_C0:
		p += scnprintf(p, end - p, "Debug selected MAC page 0xc0\n");
		start = 0x0c0;
		end_addr = 0x0df;
		break;
	case RTW89_DBG_SEL_MAC_E0:
		p += scnprintf(p, end - p, "Debug selected MAC page 0xe0\n");
		start = 0x0e0;
		end_addr = 0x0ff;
		break;
	case RTW89_DBG_SEL_BB:
		p += scnprintf(p, end - p, "Debug selected BB register\n");
		start = 0x100;
		end_addr = 0x17f;
		break;
	case RTW89_DBG_SEL_IQK:
		p += scnprintf(p, end - p, "Debug selected IQK register\n");
		start = 0x180;
		end_addr = 0x1bf;
		break;
	case RTW89_DBG_SEL_RFC:
		p += scnprintf(p, end - p, "Debug selected RFC register\n");
		start = 0x1c0;
		end_addr = 0x1ff;
		break;
	default:
		p += scnprintf(p, end - p, "Selected invalid register page\n");
		return -EINVAL;
	}

	for (i = start; i <= end_addr; i++) {
		page = i << 8;
		for (j = page; j < page + RTW89_MAC_PAGE_SIZE; j += 16) {
			p += scnprintf(p, end - p, "%08xh : ", 0x18600000 + j);
			for (k = 0; k < 4; k++) {
				val = rtw89_read32(rtwdev, j + (k << 2));
				p += scnprintf(p, end - p, "%08x ", val);
			}
			p += scnprintf(p, end - p, "\n");
		}
	}

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mac_mem_dump_select(struct rtw89_dev *rtwdev,
				     struct rtw89_debugfs_priv *debugfs_priv,
				     const char *buf, size_t count)
{
	u32 sel, start_addr, len;
	int num;

	num = sscanf(buf, "%x %x %x", &sel, &start_addr, &len);
	if (num != 3) {
		rtw89_info(rtwdev, "invalid format: <sel> <start> <len>\n");
		return -EINVAL;
	}

	debugfs_priv->mac_mem.sel = sel;
	debugfs_priv->mac_mem.start = start_addr;
	debugfs_priv->mac_mem.len = len;

	rtw89_info(rtwdev, "select mem %d start %d len %d\n",
		   sel, start_addr, len);

	return count;
}

static int rtw89_debug_dump_mac_mem(struct rtw89_dev *rtwdev,
				    char *buf, size_t bufsz,
				    u8 sel, u32 start_addr, u32 len)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 filter_model_addr = mac->filter_model_addr;
	u32 indir_access_addr = mac->indir_access_addr;
	u32 mem_page_size = mac->mem_page_size;
	u32 base_addr, start_page, residue;
	char *p = buf, *end = buf + bufsz;
	u32 i, j, pp, pages;
	u32 dump_len, remain;
	u32 val;

	remain = len;
	pages = len / mem_page_size + 1;
	start_page = start_addr / mem_page_size;
	residue = start_addr % mem_page_size;
	base_addr = mac->mem_base_addrs[sel];
	base_addr += start_page * mem_page_size;

	for (pp = 0; pp < pages; pp++) {
		dump_len = min_t(u32, remain, mem_page_size);
		rtw89_write32(rtwdev, filter_model_addr, base_addr);
		for (i = indir_access_addr + residue;
		     i < indir_access_addr + dump_len;) {
			p += scnprintf(p, end - p, "%08xh:", i);
			for (j = 0;
			     j < 4 && i < indir_access_addr + dump_len;
			     j++, i += 4) {
				val = rtw89_read32(rtwdev, i);
				p += scnprintf(p, end - p, "  %08x", val);
				remain -= 4;
			}
			p += scnprintf(p, end - p, "\n");
		}
		base_addr += mem_page_size;
	}

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mac_mem_dump_get(struct rtw89_dev *rtwdev,
				  struct rtw89_debugfs_priv *debugfs_priv,
				  char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;
	bool grant_read = false;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (debugfs_priv->mac_mem.sel >= RTW89_MAC_MEM_NUM)
		return -ENOENT;

	if (rtwdev->chip->chip_id == RTL8852C) {
		switch (debugfs_priv->mac_mem.sel) {
		case RTW89_MAC_MEM_TXD_FIFO_0_V1:
		case RTW89_MAC_MEM_TXD_FIFO_1_V1:
		case RTW89_MAC_MEM_TXDATA_FIFO_0:
		case RTW89_MAC_MEM_TXDATA_FIFO_1:
			grant_read = true;
			break;
		default:
			break;
		}
	}

	rtw89_leave_ps_mode(rtwdev);
	if (grant_read)
		rtw89_write32_set(rtwdev, R_AX_TCR1, B_AX_TCR_FORCE_READ_TXDFIFO);
	p += rtw89_debug_dump_mac_mem(rtwdev, p, end - p,
				      debugfs_priv->mac_mem.sel,
				      debugfs_priv->mac_mem.start,
				      debugfs_priv->mac_mem.len);
	if (grant_read)
		rtw89_write32_clr(rtwdev, R_AX_TCR1, B_AX_TCR_FORCE_READ_TXDFIFO);

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mac_dbg_port_dump_select(struct rtw89_dev *rtwdev,
					  struct rtw89_debugfs_priv *debugfs_priv,
					  const char *buf, size_t count)
{
	int sel, set;
	int num;
	bool enable;

	num = sscanf(buf, "%d %d", &sel, &set);
	if (num != 2) {
		rtw89_info(rtwdev, "invalid format: <sel> <set>\n");
		return -EINVAL;
	}

	enable = set != 0;
	switch (sel) {
	case 0:
		debugfs_priv->dbgpkg_en.ss_dbg = enable;
		break;
	case 1:
		debugfs_priv->dbgpkg_en.dle_dbg = enable;
		break;
	case 2:
		debugfs_priv->dbgpkg_en.dmac_dbg = enable;
		break;
	case 3:
		debugfs_priv->dbgpkg_en.cmac_dbg = enable;
		break;
	case 4:
		debugfs_priv->dbgpkg_en.dbg_port = enable;
		break;
	default:
		rtw89_info(rtwdev, "invalid args: sel %d set %d\n", sel, set);
		return -EINVAL;
	}

	rtw89_info(rtwdev, "%s debug port dump %d\n",
		   enable ? "Enable" : "Disable", sel);

	return count;
}

static int rtw89_debug_mac_dump_ss_dbg(struct rtw89_dev *rtwdev,
				       char *buf, size_t bufsz)
{
	return 0;
}

static int rtw89_debug_mac_dump_dle_dbg(struct rtw89_dev *rtwdev,
				       char *buf, size_t bufsz)
{
#define DLE_DFI_DUMP(__type, __target, __sel)				\
({									\
	u32 __ctrl;							\
	u32 __reg_ctrl = R_AX_##__type##_DBG_FUN_INTF_CTL;		\
	u32 __reg_data = R_AX_##__type##_DBG_FUN_INTF_DATA;		\
	u32 __data, __val32;						\
	int __ret;							\
									\
	__ctrl = FIELD_PREP(B_AX_##__type##_DFI_TRGSEL_MASK,		\
			    DLE_DFI_TYPE_##__target) |			\
		 FIELD_PREP(B_AX_##__type##_DFI_ADDR_MASK, __sel) |	\
		 B_AX_WDE_DFI_ACTIVE;					\
	rtw89_write32(rtwdev, __reg_ctrl, __ctrl);			\
	__ret = read_poll_timeout(rtw89_read32, __val32,		\
			!(__val32 & B_AX_##__type##_DFI_ACTIVE),	\
			1000, 50000, false,				\
			rtwdev, __reg_ctrl);				\
	if (__ret) {							\
		rtw89_err(rtwdev, "failed to dump DLE %s %s %d\n",	\
			  #__type, #__target, __sel);			\
		return __ret;						\
	}								\
									\
	__data = rtw89_read32(rtwdev, __reg_data);			\
	__data;								\
})

#define DLE_DFI_FREE_PAGE_DUMP(__p, __end, __type)			\
({									\
	u32 __freepg, __pubpg;						\
	u32 __freepg_head, __freepg_tail, __pubpg_num;			\
									\
	__freepg = DLE_DFI_DUMP(__type, FREEPG, 0);			\
	__pubpg = DLE_DFI_DUMP(__type, FREEPG, 1);			\
	__freepg_head = FIELD_GET(B_AX_DLE_FREE_HEADPG, __freepg);	\
	__freepg_tail = FIELD_GET(B_AX_DLE_FREE_TAILPG, __freepg);	\
	__pubpg_num = FIELD_GET(B_AX_DLE_PUB_PGNUM, __pubpg);		\
	__p += scnprintf(__p, __end - __p, "[%s] freepg head: %d\n",	\
			 #__type, __freepg_head);			\
	__p += scnprintf(__p, __end - __p, "[%s] freepg tail: %d\n",	\
			 #__type, __freepg_tail);			\
	__p += scnprintf(__p, __end - __p, "[%s] pubpg num  : %d\n",	\
			 #__type, __pubpg_num);				\
})

#define case_QUOTA(__p, __end, __type, __id)				\
	case __type##_QTAID_##__id:					\
		val32 = DLE_DFI_DUMP(__type, QUOTA, __type##_QTAID_##__id); \
		rsv_pgnum = FIELD_GET(B_AX_DLE_RSV_PGNUM, val32);	\
		use_pgnum = FIELD_GET(B_AX_DLE_USE_PGNUM, val32);	\
		__p += scnprintf(__p, __end - __p, "[%s][%s] rsv_pgnum: %d\n", \
				 #__type, #__id, rsv_pgnum);		\
		__p += scnprintf(__p, __end - __p, "[%s][%s] use_pgnum: %d\n", \
				 #__type, #__id, use_pgnum);		\
		break
	char *p = buf, *end = buf + bufsz;
	u32 quota_id;
	u32 val32;
	u16 rsv_pgnum, use_pgnum;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL);
	if (ret) {
		p += scnprintf(p, end - p, "[DLE]  : DMAC not enabled\n");
		goto out;
	}

	DLE_DFI_FREE_PAGE_DUMP(p, end, WDE);
	DLE_DFI_FREE_PAGE_DUMP(p, end, PLE);
	for (quota_id = 0; quota_id <= WDE_QTAID_CPUIO; quota_id++) {
		switch (quota_id) {
		case_QUOTA(p, end, WDE, HOST_IF);
		case_QUOTA(p, end, WDE, WLAN_CPU);
		case_QUOTA(p, end, WDE, DATA_CPU);
		case_QUOTA(p, end, WDE, PKTIN);
		case_QUOTA(p, end, WDE, CPUIO);
		}
	}
	for (quota_id = 0; quota_id <= PLE_QTAID_CPUIO; quota_id++) {
		switch (quota_id) {
		case_QUOTA(p, end, PLE, B0_TXPL);
		case_QUOTA(p, end, PLE, B1_TXPL);
		case_QUOTA(p, end, PLE, C2H);
		case_QUOTA(p, end, PLE, H2C);
		case_QUOTA(p, end, PLE, WLAN_CPU);
		case_QUOTA(p, end, PLE, MPDU);
		case_QUOTA(p, end, PLE, CMAC0_RX);
		case_QUOTA(p, end, PLE, CMAC1_RX);
		case_QUOTA(p, end, PLE, CMAC1_BBRPT);
		case_QUOTA(p, end, PLE, WDRLS);
		case_QUOTA(p, end, PLE, CPUIO);
		}
	}

out:
	return p - buf;

#undef case_QUOTA
#undef DLE_DFI_DUMP
#undef DLE_DFI_FREE_PAGE_DUMP
}

static int rtw89_debug_mac_dump_dmac_dbg(struct rtw89_dev *rtwdev,
					 char *buf, size_t bufsz)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	char *p = buf, *end = buf + bufsz;
	u32 dmac_err;
	int i, ret;

	ret = rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL);
	if (ret) {
		p += scnprintf(p, end - p, "[DMAC] : DMAC not enabled\n");
		goto out;
	}

	dmac_err = rtw89_read32(rtwdev, R_AX_DMAC_ERR_ISR);
	p += scnprintf(p, end - p, "R_AX_DMAC_ERR_ISR=0x%08x\n", dmac_err);
	p += scnprintf(p, end - p, "R_AX_DMAC_ERR_IMR=0x%08x\n",
		       rtw89_read32(rtwdev, R_AX_DMAC_ERR_IMR));

	if (dmac_err) {
		p += scnprintf(p, end - p, "R_AX_WDE_ERR_FLAG_CFG=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDE_ERR_FLAG_CFG_NUM1));
		p += scnprintf(p, end - p, "R_AX_PLE_ERR_FLAG_CFG=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_CFG_NUM1));
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_PLE_ERRFLAG_MSG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_PLE_ERRFLAG_MSG));
			p += scnprintf(p, end - p,
				       "R_AX_WDE_ERRFLAG_MSG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_WDE_ERRFLAG_MSG));
			p += scnprintf(p, end - p,
				       "R_AX_PLE_DBGERR_LOCKEN=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_PLE_DBGERR_LOCKEN));
			p += scnprintf(p, end - p,
				       "R_AX_PLE_DBGERR_STS=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_PLE_DBGERR_STS));
		}
	}

	if (dmac_err & B_AX_WDRLS_ERR_FLAG) {
		p += scnprintf(p, end - p, "R_AX_WDRLS_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDRLS_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_WDRLS_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDRLS_ERR_ISR));
		if (chip->chip_id == RTL8852C)
			p += scnprintf(p, end - p,
				       "R_AX_RPQ_RXBD_IDX=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RPQ_RXBD_IDX_V1));
		else
			p += scnprintf(p, end - p,
				       "R_AX_RPQ_RXBD_IDX=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RPQ_RXBD_IDX));
	}

	if (dmac_err & B_AX_WSEC_ERR_FLAG) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_SEC_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_ERROR_FLAG_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_ERROR_FLAG));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_ENG_CTRL=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_ENG_CTRL));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_MPDU_PROC=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_MPDU_PROC));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_CAM_ACCESS=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_CAM_ACCESS));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_CAM_RDATA=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_CAM_RDATA));
			p += scnprintf(p, end - p, "R_AX_SEC_DEBUG1=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_DEBUG1));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_TX_DEBUG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_TX_DEBUG));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_RX_DEBUG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_RX_DEBUG));

			rtw89_write32_mask(rtwdev, R_AX_DBG_CTRL,
					   B_AX_DBG_SEL0, 0x8B);
			rtw89_write32_mask(rtwdev, R_AX_DBG_CTRL,
					   B_AX_DBG_SEL1, 0x8B);
			rtw89_write32_mask(rtwdev, R_AX_SYS_STATUS1,
					   B_AX_SEL_0XC0_MASK, 1);
			for (i = 0; i < 0x10; i++) {
				rtw89_write32_mask(rtwdev, R_AX_SEC_ENG_CTRL,
						   B_AX_SEC_DBG_PORT_FIELD_MASK, i);
				p += scnprintf(p, end - p,
					       "sel=%x,R_AX_SEC_DEBUG2=0x%08x\n",
					       i,
					       rtw89_read32(rtwdev, R_AX_SEC_DEBUG2));
			}
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_SEC_ERR_IMR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_DEBUG));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_ENG_CTRL=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_ENG_CTRL));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_MPDU_PROC=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_MPDU_PROC));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_CAM_ACCESS=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_CAM_ACCESS));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_CAM_RDATA=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_CAM_RDATA));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_CAM_WDATA=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_CAM_WDATA));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_TX_DEBUG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_TX_DEBUG));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_RX_DEBUG=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_RX_DEBUG));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_TRX_PKT_CNT=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_TRX_PKT_CNT));
			p += scnprintf(p, end - p,
				       "R_AX_SEC_TRX_BLK_CNT=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_SEC_TRX_BLK_CNT));
		}
	}

	if (dmac_err & B_AX_MPDU_ERR_FLAG) {
		p += scnprintf(p, end - p, "R_AX_MPDU_TX_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_MPDU_TX_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_MPDU_TX_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_MPDU_TX_ERR_ISR));
		p += scnprintf(p, end - p, "R_AX_MPDU_RX_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_MPDU_RX_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_MPDU_RX_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_MPDU_RX_ERR_ISR));
	}

	if (dmac_err & B_AX_STA_SCHEDULER_ERR_FLAG) {
		p += scnprintf(p, end - p,
			       "R_AX_STA_SCHEDULER_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_STA_SCHEDULER_ERR_IMR));
		p += scnprintf(p, end - p,
			       "R_AX_STA_SCHEDULER_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_STA_SCHEDULER_ERR_ISR));
	}

	if (dmac_err & B_AX_WDE_DLE_ERR_FLAG) {
		p += scnprintf(p, end - p, "R_AX_WDE_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDE_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_WDE_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDE_ERR_ISR));
		p += scnprintf(p, end - p, "R_AX_PLE_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PLE_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_PLE_ERR_FLAG_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_ISR));
	}

	if (dmac_err & B_AX_TXPKTCTRL_ERR_FLAG) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_B0_ERRFLAG_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_B0_ERRFLAG_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_B1_ERRFLAG_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_B1_ERRFLAG_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_B1_ERRFLAG_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_B1_ERRFLAG_ISR));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_ERR_IMR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_ERR_IMR_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_TXPKTCTL_ERR_IMR_ISR_B1=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_TXPKTCTL_ERR_IMR_ISR_B1));
		}
	}

	if (dmac_err & B_AX_PLE_DLE_ERR_FLAG) {
		p += scnprintf(p, end - p, "R_AX_WDE_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDE_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_WDE_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WDE_ERR_ISR));
		p += scnprintf(p, end - p, "R_AX_PLE_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PLE_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_PLE_ERR_FLAG_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_ISR));
		p += scnprintf(p, end - p, "R_AX_WD_CPUQ_OP_0=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_0));
		p += scnprintf(p, end - p, "R_AX_WD_CPUQ_OP_1=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_1));
		p += scnprintf(p, end - p, "R_AX_WD_CPUQ_OP_2=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_2));
		p += scnprintf(p, end - p, "R_AX_WD_CPUQ_OP_STATUS=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_STATUS));
		p += scnprintf(p, end - p, "R_AX_PL_CPUQ_OP_0=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_0));
		p += scnprintf(p, end - p, "R_AX_PL_CPUQ_OP_1=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_1));
		p += scnprintf(p, end - p, "R_AX_PL_CPUQ_OP_2=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_2));
		p += scnprintf(p, end - p, "R_AX_PL_CPUQ_OP_STATUS=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_STATUS));
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p, "R_AX_RX_CTRL0=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RX_CTRL0));
			p += scnprintf(p, end - p, "R_AX_RX_CTRL1=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RX_CTRL1));
			p += scnprintf(p, end - p, "R_AX_RX_CTRL2=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RX_CTRL2));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_RXDMA_PKT_INFO_0=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_0));
			p += scnprintf(p, end - p,
				       "R_AX_RXDMA_PKT_INFO_1=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_1));
			p += scnprintf(p, end - p,
				       "R_AX_RXDMA_PKT_INFO_2=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_2));
		}
	}

	if (dmac_err & B_AX_PKTIN_ERR_FLAG) {
		p += scnprintf(p, end - p, "R_AX_PKTIN_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PKTIN_ERR_IMR));
		p += scnprintf(p, end - p, "R_AX_PKTIN_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_PKTIN_ERR_ISR));
	}

	if (dmac_err & B_AX_DISPATCH_ERR_FLAG) {
		p += scnprintf(p, end - p,
			       "R_AX_HOST_DISPATCHER_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_IMR));
		p += scnprintf(p, end - p,
			       "R_AX_HOST_DISPATCHER_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_ISR));
		p += scnprintf(p, end - p,
			       "R_AX_CPU_DISPATCHER_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_IMR));
		p += scnprintf(p, end - p,
			       "R_AX_CPU_DISPATCHER_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_ISR));
		p += scnprintf(p, end - p,
			       "R_AX_OTHER_DISPATCHER_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_IMR));
		p += scnprintf(p, end - p,
			       "R_AX_OTHER_DISPATCHER_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_ISR));
	}

	if (dmac_err & B_AX_BBRPT_ERR_FLAG) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_COM_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_COM_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_CHINFO_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_CHINFO_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_DFS_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_DFS_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_ISR));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_COM_ERR_IMR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_IMR_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_CHINFO_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_ISR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_CHINFO_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_DFS_ERR_IMR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_IMR));
			p += scnprintf(p, end - p,
				       "R_AX_BBRPT_DFS_ERR_ISR=0x%08x\n",
				       rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_ISR));
		}
	}

	if (dmac_err & B_AX_HAXIDMA_ERR_FLAG && chip->chip_id == RTL8852C) {
		p += scnprintf(p, end - p, "R_AX_HAXIDMA_ERR_IMR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_HAXI_IDCT_MSK));
		p += scnprintf(p, end - p, "R_AX_HAXIDMA_ERR_ISR=0x%08x\n",
			       rtw89_read32(rtwdev, R_AX_HAXI_IDCT));
	}

out:
	return p - buf;
}

static int rtw89_debug_mac_dump_cmac_err(struct rtw89_dev *rtwdev,
					 char *buf, size_t bufsz,
					 enum rtw89_mac_idx band)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	char *p = buf, *end = buf + bufsz;
	u32 offset = 0;
	u32 cmac_err;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, band, RTW89_CMAC_SEL);
	if (ret) {
		if (band)
			p += scnprintf(p, end - p,
				       "[CMAC] : CMAC1 not enabled\n");
		else
			p += scnprintf(p, end - p,
				       "[CMAC] : CMAC0 not enabled\n");
		goto out;
	}

	if (band)
		offset = RTW89_MAC_AX_BAND_REG_OFFSET;

	cmac_err = rtw89_read32(rtwdev, R_AX_CMAC_ERR_ISR + offset);
	p += scnprintf(p, end - p, "R_AX_CMAC_ERR_ISR [%d]=0x%08x\n", band,
		       rtw89_read32(rtwdev, R_AX_CMAC_ERR_ISR + offset));
	p += scnprintf(p, end - p, "R_AX_CMAC_FUNC_EN [%d]=0x%08x\n", band,
		       rtw89_read32(rtwdev, R_AX_CMAC_FUNC_EN + offset));
	p += scnprintf(p, end - p, "R_AX_CK_EN [%d]=0x%08x\n", band,
		       rtw89_read32(rtwdev, R_AX_CK_EN + offset));

	if (cmac_err & B_AX_SCHEDULE_TOP_ERR_IND) {
		p += scnprintf(p, end - p,
			       "R_AX_SCHEDULE_ERR_IMR [%d]=0x%08x\n", band,
			       rtw89_read32(rtwdev, R_AX_SCHEDULE_ERR_IMR + offset));
		p += scnprintf(p, end - p,
			       "R_AX_SCHEDULE_ERR_ISR [%d]=0x%08x\n", band,
			       rtw89_read32(rtwdev, R_AX_SCHEDULE_ERR_ISR + offset));
	}

	if (cmac_err & B_AX_PTCL_TOP_ERR_IND) {
		p += scnprintf(p, end - p, "R_AX_PTCL_IMR0 [%d]=0x%08x\n",
			       band,
			       rtw89_read32(rtwdev, R_AX_PTCL_IMR0 + offset));
		p += scnprintf(p, end - p, "R_AX_PTCL_ISR0 [%d]=0x%08x\n",
			       band,
			       rtw89_read32(rtwdev, R_AX_PTCL_ISR0 + offset));
	}

	if (cmac_err & B_AX_DMA_TOP_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_RX_ERR_FLAG [%d]=0x%08x\n", band,
				       rtw89_read32(rtwdev, R_AX_RX_ERR_FLAG + offset));
			p += scnprintf(p, end - p,
				       "R_AX_RX_ERR_FLAG_IMR [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev, R_AX_RX_ERR_FLAG_IMR + offset));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_DLE_CTRL [%d]=0x%08x\n", band,
				       rtw89_read32(rtwdev, R_AX_DLE_CTRL + offset));
		}
	}

	if (cmac_err & B_AX_DMA_TOP_ERR_IND || cmac_err & B_AX_WMAC_RX_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_PHYINFO_ERR_ISR [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_ISR + offset));
			p += scnprintf(p, end - p,
				       "R_AX_PHYINFO_ERR_IMR [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_IMR + offset));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_PHYINFO_ERR_IMR [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_IMR + offset));
		}
	}

	if (cmac_err & B_AX_TXPWR_CTRL_ERR_IND) {
		p += scnprintf(p, end - p, "R_AX_TXPWR_IMR [%d]=0x%08x\n",
			       band,
			       rtw89_read32(rtwdev, R_AX_TXPWR_IMR + offset));
		p += scnprintf(p, end - p, "R_AX_TXPWR_ISR [%d]=0x%08x\n",
			       band,
			       rtw89_read32(rtwdev, R_AX_TXPWR_ISR + offset));
	}

	if (cmac_err & B_AX_WMAC_TX_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			p += scnprintf(p, end - p,
				       "R_AX_TRXPTCL_ERROR_INDICA [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev,
						    R_AX_TRXPTCL_ERROR_INDICA + offset));
			p += scnprintf(p, end - p,
				       "R_AX_TRXPTCL_ERROR_INDICA_MASK [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev,
						    R_AX_TRXPTCL_ERROR_INDICA_MASK + offset));
		} else {
			p += scnprintf(p, end - p,
				       "R_AX_TMAC_ERR_IMR_ISR [%d]=0x%08x\n",
				       band,
				       rtw89_read32(rtwdev,
						    R_AX_TMAC_ERR_IMR_ISR + offset));
		}
		p += scnprintf(p, end - p,
			       "R_AX_DBGSEL_TRXPTCL [%d]=0x%08x\n", band,
			       rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL + offset));
	}

	p += scnprintf(p, end - p, "R_AX_CMAC_ERR_IMR [%d]=0x%08x\n", band,
		       rtw89_read32(rtwdev, R_AX_CMAC_ERR_IMR + offset));

out:
	return p - buf;
}

static int rtw89_debug_mac_dump_cmac_dbg(struct rtw89_dev *rtwdev,
					 char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;

	p += rtw89_debug_mac_dump_cmac_err(rtwdev, p, end - p, RTW89_MAC_0);
	if (rtwdev->dbcc_en)
		p += rtw89_debug_mac_dump_cmac_err(rtwdev, p, end - p, RTW89_MAC_1);

	return p - buf;
}

static const struct rtw89_mac_dbg_port_info dbg_port_ptcl_c0 = {
	.sel_addr = R_AX_PTCL_DBG,
	.sel_byte = 1,
	.sel_msk = B_AX_PTCL_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x3F,
	.rd_addr = R_AX_PTCL_DBG_INFO,
	.rd_byte = 4,
	.rd_msk = B_AX_PTCL_DBG_INFO_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ptcl_c1 = {
	.sel_addr = R_AX_PTCL_DBG_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_PTCL_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x3F,
	.rd_addr = R_AX_PTCL_DBG_INFO_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_PTCL_DBG_INFO_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_tx0_5 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0xD,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_tx6 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x5,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_tx7 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x9,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_tx8 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x3,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_tx9_C = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x1,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_txD = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx0 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0xB,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx1 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x4,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx3 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x8,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx4 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x7,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx5_8 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x1,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_tx9 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x3,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_txA_C = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_rx0 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x8,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_rx1_2 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_rx3 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x6,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_rx4 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_hdt_rx5 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 2,
	.sel_msk = B_AX_DISPATCHER_DBG_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_rx_p0_0 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x3,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_rx_p0_1 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x6,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_rx_p0_2 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x0,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_cdt_rx_p1 = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x8,
	.end = 0xE,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_stf_ctrl = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x5,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_addr_ctrl = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x6,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_wde_intf = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0xF,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_ple_intf = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x9,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_dspt_flow_ctrl = {
	.sel_addr = R_AX_DISPATCHER_DBG_PORT,
	.sel_byte = 1,
	.sel_msk = B_AX_DISPATCHER_CH_SEL_MASK,
	.srt = 0x0,
	.end = 0x3,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_sch_c0 = {
	.sel_addr = R_AX_SCH_DBG_SEL,
	.sel_byte = 1,
	.sel_msk = B_AX_SCH_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x2F,
	.rd_addr = R_AX_SCH_DBG,
	.rd_byte = 4,
	.rd_msk = B_AX_SCHEDULER_DBG_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_sch_c1 = {
	.sel_addr = R_AX_SCH_DBG_SEL_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_SCH_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x2F,
	.rd_addr = R_AX_SCH_DBG_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_SCHEDULER_DBG_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tmac_c0 = {
	.sel_addr = R_AX_MACTX_DBG_SEL_CNT,
	.sel_byte = 1,
	.sel_msk = B_AX_DBGSEL_MACTX_MASK,
	.srt = 0x00,
	.end = 0x19,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tmac_c1 = {
	.sel_addr = R_AX_MACTX_DBG_SEL_CNT_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_DBGSEL_MACTX_MASK,
	.srt = 0x00,
	.end = 0x19,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmac_c0 = {
	.sel_addr = R_AX_RX_DEBUG_SELECT,
	.sel_byte = 1,
	.sel_msk = B_AX_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x58,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmac_c1 = {
	.sel_addr = R_AX_RX_DEBUG_SELECT_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x58,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmacst_c0 = {
	.sel_addr = R_AX_RX_STATE_MONITOR,
	.sel_byte = 1,
	.sel_msk = B_AX_STATE_SEL_MASK,
	.srt = 0x00,
	.end = 0x17,
	.rd_addr = R_AX_RX_STATE_MONITOR,
	.rd_byte = 4,
	.rd_msk = B_AX_RX_STATE_MONITOR_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmacst_c1 = {
	.sel_addr = R_AX_RX_STATE_MONITOR_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_STATE_SEL_MASK,
	.srt = 0x00,
	.end = 0x17,
	.rd_addr = R_AX_RX_STATE_MONITOR_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_RX_STATE_MONITOR_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmac_plcp_c0 = {
	.sel_addr = R_AX_RMAC_PLCP_MON,
	.sel_byte = 4,
	.sel_msk = B_AX_PCLP_MON_SEL_MASK,
	.srt = 0x0,
	.end = 0xF,
	.rd_addr = R_AX_RMAC_PLCP_MON,
	.rd_byte = 4,
	.rd_msk = B_AX_RMAC_PLCP_MON_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_rmac_plcp_c1 = {
	.sel_addr = R_AX_RMAC_PLCP_MON_C1,
	.sel_byte = 4,
	.sel_msk = B_AX_PCLP_MON_SEL_MASK,
	.srt = 0x0,
	.end = 0xF,
	.rd_addr = R_AX_RMAC_PLCP_MON_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_RMAC_PLCP_MON_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_trxptcl_c0 = {
	.sel_addr = R_AX_DBGSEL_TRXPTCL,
	.sel_byte = 1,
	.sel_msk = B_AX_DBGSEL_TRXPTCL_MASK,
	.srt = 0x08,
	.end = 0x10,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_trxptcl_c1 = {
	.sel_addr = R_AX_DBGSEL_TRXPTCL_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_DBGSEL_TRXPTCL_MASK,
	.srt = 0x08,
	.end = 0x10,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tx_infol_c0 = {
	.sel_addr = R_AX_WMAC_TX_CTRL_DEBUG,
	.sel_byte = 1,
	.sel_msk = B_AX_TX_CTRL_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x07,
	.rd_addr = R_AX_WMAC_TX_INFO0_DEBUG,
	.rd_byte = 4,
	.rd_msk = B_AX_TX_CTRL_INFO_P0_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tx_infoh_c0 = {
	.sel_addr = R_AX_WMAC_TX_CTRL_DEBUG,
	.sel_byte = 1,
	.sel_msk = B_AX_TX_CTRL_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x07,
	.rd_addr = R_AX_WMAC_TX_INFO1_DEBUG,
	.rd_byte = 4,
	.rd_msk = B_AX_TX_CTRL_INFO_P1_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tx_infol_c1 = {
	.sel_addr = R_AX_WMAC_TX_CTRL_DEBUG_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_TX_CTRL_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x07,
	.rd_addr = R_AX_WMAC_TX_INFO0_DEBUG_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_TX_CTRL_INFO_P0_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_tx_infoh_c1 = {
	.sel_addr = R_AX_WMAC_TX_CTRL_DEBUG_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_TX_CTRL_DEBUG_SEL_MASK,
	.srt = 0x00,
	.end = 0x07,
	.rd_addr = R_AX_WMAC_TX_INFO1_DEBUG_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_TX_CTRL_INFO_P1_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_txtf_infol_c0 = {
	.sel_addr = R_AX_WMAC_TX_TF_INFO_0,
	.sel_byte = 1,
	.sel_msk = B_AX_WMAC_TX_TF_INFO_SEL_MASK,
	.srt = 0x00,
	.end = 0x04,
	.rd_addr = R_AX_WMAC_TX_TF_INFO_1,
	.rd_byte = 4,
	.rd_msk = B_AX_WMAC_TX_TF_INFO_P0_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_txtf_infoh_c0 = {
	.sel_addr = R_AX_WMAC_TX_TF_INFO_0,
	.sel_byte = 1,
	.sel_msk = B_AX_WMAC_TX_TF_INFO_SEL_MASK,
	.srt = 0x00,
	.end = 0x04,
	.rd_addr = R_AX_WMAC_TX_TF_INFO_2,
	.rd_byte = 4,
	.rd_msk = B_AX_WMAC_TX_TF_INFO_P1_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_txtf_infol_c1 = {
	.sel_addr = R_AX_WMAC_TX_TF_INFO_0_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_WMAC_TX_TF_INFO_SEL_MASK,
	.srt = 0x00,
	.end = 0x04,
	.rd_addr = R_AX_WMAC_TX_TF_INFO_1_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_WMAC_TX_TF_INFO_P0_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_txtf_infoh_c1 = {
	.sel_addr = R_AX_WMAC_TX_TF_INFO_0_C1,
	.sel_byte = 1,
	.sel_msk = B_AX_WMAC_TX_TF_INFO_SEL_MASK,
	.srt = 0x00,
	.end = 0x04,
	.rd_addr = R_AX_WMAC_TX_TF_INFO_2_C1,
	.rd_byte = 4,
	.rd_msk = B_AX_WMAC_TX_TF_INFO_P1_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_bufmgn_freepg = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80000000,
	.end = 0x80000001,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_bufmgn_quota = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80010000,
	.end = 0x80010004,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_bufmgn_pagellt = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80020000,
	.end = 0x80020FFF,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_bufmgn_pktinfo = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80030000,
	.end = 0x80030FFF,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_quemgn_prepkt = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80040000,
	.end = 0x80040FFF,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_quemgn_nxtpkt = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80050000,
	.end = 0x80050FFF,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_quemgn_qlnktbl = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80060000,
	.end = 0x80060453,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_wde_quemgn_qempty = {
	.sel_addr = R_AX_WDE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_WDE_DFI_DATA_MASK,
	.srt = 0x80070000,
	.end = 0x80070011,
	.rd_addr = R_AX_WDE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_WDE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_bufmgn_freepg = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80000000,
	.end = 0x80000001,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_bufmgn_quota = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80010000,
	.end = 0x8001000A,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_bufmgn_pagellt = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80020000,
	.end = 0x80020DBF,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_bufmgn_pktinfo = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80030000,
	.end = 0x80030DBF,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_quemgn_prepkt = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80040000,
	.end = 0x80040DBF,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_quemgn_nxtpkt = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80050000,
	.end = 0x80050DBF,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_quemgn_qlnktbl = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80060000,
	.end = 0x80060041,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_ple_quemgn_qempty = {
	.sel_addr = R_AX_PLE_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_PLE_DFI_DATA_MASK,
	.srt = 0x80070000,
	.end = 0x80070001,
	.rd_addr = R_AX_PLE_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_PLE_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pktinfo = {
	.sel_addr = R_AX_DBG_FUN_INTF_CTL,
	.sel_byte = 4,
	.sel_msk = B_AX_DFI_DATA_MASK,
	.srt = 0x80000000,
	.end = 0x8000017f,
	.rd_addr = R_AX_DBG_FUN_INTF_DATA,
	.rd_byte = 4,
	.rd_msk = B_AX_DFI_DATA_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_txdma = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x03,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_rxdma = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x04,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_cvt = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x01,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_cxpl = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x05,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_io = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x05,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_misc = {
	.sel_addr = R_AX_PCIE_DBG_CTRL,
	.sel_byte = 2,
	.sel_msk = B_AX_PCIE_DBG_SEL_MASK,
	.srt = 0x00,
	.end = 0x06,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static const struct rtw89_mac_dbg_port_info dbg_port_pcie_misc2 = {
	.sel_addr = R_AX_DBG_CTRL,
	.sel_byte = 1,
	.sel_msk = B_AX_DBG_SEL0,
	.srt = 0x34,
	.end = 0x3C,
	.rd_addr = R_AX_DBG_PORT_SEL,
	.rd_byte = 4,
	.rd_msk = B_AX_DEBUG_ST_MASK
};

static int
rtw89_debug_mac_dbg_port_sel(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
			     u32 sel, const struct rtw89_mac_dbg_port_info **ppinfo)
{
	const struct rtw89_mac_dbg_port_info *info = NULL;
	char *p = buf, *end = buf + bufsz;
	u32 index;
	u32 val32;
	u16 val16;
	u8 val8;

	switch (sel) {
	case RTW89_DBG_PORT_SEL_PTCL_C0:
		info = &dbg_port_ptcl_c0;
		val16 = rtw89_read16(rtwdev, R_AX_PTCL_DBG);
		val16 |= B_AX_PTCL_DBG_EN;
		rtw89_write16(rtwdev, R_AX_PTCL_DBG, val16);
		p += scnprintf(p, end - p, "Enable PTCL C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_PTCL_C1:
		info = &dbg_port_ptcl_c1;
		val16 = rtw89_read16(rtwdev, R_AX_PTCL_DBG_C1);
		val16 |= B_AX_PTCL_DBG_EN;
		rtw89_write16(rtwdev, R_AX_PTCL_DBG_C1, val16);
		p += scnprintf(p, end - p, "Enable PTCL C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_SCH_C0:
		info = &dbg_port_sch_c0;
		val32 = rtw89_read32(rtwdev, R_AX_SCH_DBG_SEL);
		val32 |= B_AX_SCH_DBG_EN;
		rtw89_write32(rtwdev, R_AX_SCH_DBG_SEL, val32);
		p += scnprintf(p, end - p, "Enable SCH C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_SCH_C1:
		info = &dbg_port_sch_c1;
		val32 = rtw89_read32(rtwdev, R_AX_SCH_DBG_SEL_C1);
		val32 |= B_AX_SCH_DBG_EN;
		rtw89_write32(rtwdev, R_AX_SCH_DBG_SEL_C1, val32);
		p += scnprintf(p, end - p, "Enable SCH C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_TMAC_C0:
		info = &dbg_port_tmac_c0;
		val32 = rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL);
		val32 = u32_replace_bits(val32, TRXPTRL_DBG_SEL_TMAC,
					 B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write32(rtwdev, R_AX_DBGSEL_TRXPTCL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, TMAC_DBG_SEL_C0, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, TMAC_DBG_SEL_C0, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);
		p += scnprintf(p, end - p, "Enable TMAC C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_TMAC_C1:
		info = &dbg_port_tmac_c1;
		val32 = rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL_C1);
		val32 = u32_replace_bits(val32, TRXPTRL_DBG_SEL_TMAC,
					 B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write32(rtwdev, R_AX_DBGSEL_TRXPTCL_C1, val32);

		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, TMAC_DBG_SEL_C1, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, TMAC_DBG_SEL_C1, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);
		p += scnprintf(p, end - p, "Enable TMAC C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMAC_C0:
		info = &dbg_port_rmac_c0;
		val32 = rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL);
		val32 = u32_replace_bits(val32, TRXPTRL_DBG_SEL_RMAC,
					 B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write32(rtwdev, R_AX_DBGSEL_TRXPTCL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, RMAC_DBG_SEL_C0, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, RMAC_DBG_SEL_C0, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);

		val8 = rtw89_read8(rtwdev, R_AX_DBGSEL_TRXPTCL);
		val8 = u8_replace_bits(val8, RMAC_CMAC_DBG_SEL,
				       B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write8(rtwdev, R_AX_DBGSEL_TRXPTCL, val8);
		p += scnprintf(p, end - p, "Enable RMAC C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMAC_C1:
		info = &dbg_port_rmac_c1;
		val32 = rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL_C1);
		val32 = u32_replace_bits(val32, TRXPTRL_DBG_SEL_RMAC,
					 B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write32(rtwdev, R_AX_DBGSEL_TRXPTCL_C1, val32);

		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, RMAC_DBG_SEL_C1, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, RMAC_DBG_SEL_C1, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);

		val8 = rtw89_read8(rtwdev, R_AX_DBGSEL_TRXPTCL_C1);
		val8 = u8_replace_bits(val8, RMAC_CMAC_DBG_SEL,
				       B_AX_DBGSEL_TRXPTCL_MASK);
		rtw89_write8(rtwdev, R_AX_DBGSEL_TRXPTCL_C1, val8);
		p += scnprintf(p, end - p, "Enable RMAC C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMACST_C0:
		info = &dbg_port_rmacst_c0;
		p += scnprintf(p, end - p, "Enable RMAC state C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMACST_C1:
		info = &dbg_port_rmacst_c1;
		p += scnprintf(p, end - p, "Enable RMAC state C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMAC_PLCP_C0:
		info = &dbg_port_rmac_plcp_c0;
		p += scnprintf(p, end - p, "Enable RMAC PLCP C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_RMAC_PLCP_C1:
		info = &dbg_port_rmac_plcp_c1;
		p += scnprintf(p, end - p, "Enable RMAC PLCP C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_TRXPTCL_C0:
		info = &dbg_port_trxptcl_c0;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, TRXPTCL_DBG_SEL_C0, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, TRXPTCL_DBG_SEL_C0, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);
		p += scnprintf(p, end - p, "Enable TRXPTCL C0 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_TRXPTCL_C1:
		info = &dbg_port_trxptcl_c1;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, TRXPTCL_DBG_SEL_C1, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, TRXPTCL_DBG_SEL_C1, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);

		val32 = rtw89_read32(rtwdev, R_AX_SYS_STATUS1);
		val32 = u32_replace_bits(val32, MAC_DBG_SEL, B_AX_SEL_0XC0_MASK);
		rtw89_write32(rtwdev, R_AX_SYS_STATUS1, val32);
		p += scnprintf(p, end - p, "Enable TRXPTCL C1 dbgport.\n");
		break;
	case RTW89_DBG_PORT_SEL_TX_INFOL_C0:
		info = &dbg_port_tx_infol_c0;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1, val32);
		p += scnprintf(p, end - p, "Enable tx infol dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TX_INFOH_C0:
		info = &dbg_port_tx_infoh_c0;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1, val32);
		p += scnprintf(p, end - p, "Enable tx infoh dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TX_INFOL_C1:
		info = &dbg_port_tx_infol_c1;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1_C1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1_C1, val32);
		p += scnprintf(p, end - p, "Enable tx infol dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TX_INFOH_C1:
		info = &dbg_port_tx_infoh_c1;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1_C1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1_C1, val32);
		p += scnprintf(p, end - p, "Enable tx infoh dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TXTF_INFOL_C0:
		info = &dbg_port_txtf_infol_c0;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1, val32);
		p += scnprintf(p, end - p, "Enable tx tf infol dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TXTF_INFOH_C0:
		info = &dbg_port_txtf_infoh_c0;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1, val32);
		p += scnprintf(p, end - p, "Enable tx tf infoh dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TXTF_INFOL_C1:
		info = &dbg_port_txtf_infol_c1;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1_C1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1_C1, val32);
		p += scnprintf(p, end - p, "Enable tx tf infol dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_TXTF_INFOH_C1:
		info = &dbg_port_txtf_infoh_c1;
		val32 = rtw89_read32(rtwdev, R_AX_TCR1_C1);
		val32 |= B_AX_TCR_FORCE_READ_TXDFIFO;
		rtw89_write32(rtwdev, R_AX_TCR1_C1, val32);
		p += scnprintf(p, end - p, "Enable tx tf infoh dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_BUFMGN_FREEPG:
		info = &dbg_port_wde_bufmgn_freepg;
		p += scnprintf(p, end - p, "Enable wde bufmgn freepg dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_BUFMGN_QUOTA:
		info = &dbg_port_wde_bufmgn_quota;
		p += scnprintf(p, end - p, "Enable wde bufmgn quota dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_BUFMGN_PAGELLT:
		info = &dbg_port_wde_bufmgn_pagellt;
		p += scnprintf(p, end - p,
			       "Enable wde bufmgn pagellt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_BUFMGN_PKTINFO:
		info = &dbg_port_wde_bufmgn_pktinfo;
		p += scnprintf(p, end - p,
			       "Enable wde bufmgn pktinfo dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_QUEMGN_PREPKT:
		info = &dbg_port_wde_quemgn_prepkt;
		p += scnprintf(p, end - p, "Enable wde quemgn prepkt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_QUEMGN_NXTPKT:
		info = &dbg_port_wde_quemgn_nxtpkt;
		p += scnprintf(p, end - p, "Enable wde quemgn nxtpkt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_QUEMGN_QLNKTBL:
		info = &dbg_port_wde_quemgn_qlnktbl;
		p += scnprintf(p, end - p,
			       "Enable wde quemgn qlnktbl dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_WDE_QUEMGN_QEMPTY:
		info = &dbg_port_wde_quemgn_qempty;
		p += scnprintf(p, end - p, "Enable wde quemgn qempty dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_BUFMGN_FREEPG:
		info = &dbg_port_ple_bufmgn_freepg;
		p += scnprintf(p, end - p, "Enable ple bufmgn freepg dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_BUFMGN_QUOTA:
		info = &dbg_port_ple_bufmgn_quota;
		p += scnprintf(p, end - p, "Enable ple bufmgn quota dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_BUFMGN_PAGELLT:
		info = &dbg_port_ple_bufmgn_pagellt;
		p += scnprintf(p, end - p,
			       "Enable ple bufmgn pagellt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_BUFMGN_PKTINFO:
		info = &dbg_port_ple_bufmgn_pktinfo;
		p += scnprintf(p, end - p,
			       "Enable ple bufmgn pktinfo dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_QUEMGN_PREPKT:
		info = &dbg_port_ple_quemgn_prepkt;
		p += scnprintf(p, end - p, "Enable ple quemgn prepkt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_QUEMGN_NXTPKT:
		info = &dbg_port_ple_quemgn_nxtpkt;
		p += scnprintf(p, end - p, "Enable ple quemgn nxtpkt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_QUEMGN_QLNKTBL:
		info = &dbg_port_ple_quemgn_qlnktbl;
		p += scnprintf(p, end - p,
			       "Enable ple quemgn qlnktbl dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PLE_QUEMGN_QEMPTY:
		info = &dbg_port_ple_quemgn_qempty;
		p += scnprintf(p, end - p, "Enable ple quemgn qempty dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PKTINFO:
		info = &dbg_port_pktinfo;
		p += scnprintf(p, end - p, "Enable pktinfo dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX0:
		rtw89_write32_mask(rtwdev, R_AX_DBG_CTRL,
				   B_AX_DBG_SEL0, 0x80);
		rtw89_write32_mask(rtwdev, R_AX_SYS_STATUS1,
				   B_AX_SEL_0XC0_MASK, 1);
		fallthrough;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX1:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX2:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX3:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX4:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX5:
		info = &dbg_port_dspt_hdt_tx0_5;
		index = sel - RTW89_DBG_PORT_SEL_DSPT_HDT_TX0;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, index);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt tx%x dump.\n", index);
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX6:
		info = &dbg_port_dspt_hdt_tx6;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 6);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt tx6 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX7:
		info = &dbg_port_dspt_hdt_tx7;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 7);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt tx7 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX8:
		info = &dbg_port_dspt_hdt_tx8;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 8);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt tx8 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TX9:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TXA:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TXB:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TXC:
		info = &dbg_port_dspt_hdt_tx9_C;
		index = sel + 9 - RTW89_DBG_PORT_SEL_DSPT_HDT_TX9;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, index);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt tx%x dump.\n", index);
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_TXD:
		info = &dbg_port_dspt_hdt_txD;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 0);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 0xD);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt txD dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX0:
		info = &dbg_port_dspt_cdt_tx0;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 0);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx0 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX1:
		info = &dbg_port_dspt_cdt_tx1;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 1);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx1 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX3:
		info = &dbg_port_dspt_cdt_tx3;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 3);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx3 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX4:
		info = &dbg_port_dspt_cdt_tx4;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 4);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx4 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX5:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX6:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX7:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX8:
		info = &dbg_port_dspt_cdt_tx5_8;
		index = sel + 5 - RTW89_DBG_PORT_SEL_DSPT_CDT_TX5;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, index);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx%x dump.\n", index);
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TX9:
		info = &dbg_port_dspt_cdt_tx9;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 9);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx9 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TXA:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TXB:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_TXC:
		info = &dbg_port_dspt_cdt_txA_C;
		index = sel + 0xA - RTW89_DBG_PORT_SEL_DSPT_CDT_TXA;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 1);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, index);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt tx%x dump.\n", index);
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX0:
		info = &dbg_port_dspt_hdt_rx0;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 2);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 0);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt rx0 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX1:
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX2:
		info = &dbg_port_dspt_hdt_rx1_2;
		index = sel + 1 - RTW89_DBG_PORT_SEL_DSPT_HDT_RX1;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 2);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, index);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt rx%x dump.\n", index);
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX3:
		info = &dbg_port_dspt_hdt_rx3;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 2);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 3);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt rx3 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX4:
		info = &dbg_port_dspt_hdt_rx4;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 2);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 4);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt rx4 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_HDT_RX5:
		info = &dbg_port_dspt_hdt_rx5;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 2);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 5);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher hdt rx5 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_0:
		info = &dbg_port_dspt_cdt_rx_p0_0;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 3);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 0);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt rx part0 0 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0:
	case RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_1:
		info = &dbg_port_dspt_cdt_rx_p0_1;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 3);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 1);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt rx part0 1 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_2:
		info = &dbg_port_dspt_cdt_rx_p0_2;
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_INTN_SEL_MASK, 3);
		rtw89_write16_mask(rtwdev, info->sel_addr,
				   B_AX_DISPATCHER_CH_SEL_MASK, 2);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt rx part0 2 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P1:
		info = &dbg_port_dspt_cdt_rx_p1;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 3);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher cdt rx part1 dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_STF_CTRL:
		info = &dbg_port_dspt_stf_ctrl;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 4);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher stf control dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_ADDR_CTRL:
		info = &dbg_port_dspt_addr_ctrl;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 5);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher addr control dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_WDE_INTF:
		info = &dbg_port_dspt_wde_intf;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 6);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher wde interface dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_PLE_INTF:
		info = &dbg_port_dspt_ple_intf;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 7);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher ple interface dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_DSPT_FLOW_CTRL:
		info = &dbg_port_dspt_flow_ctrl;
		rtw89_write8_mask(rtwdev, info->sel_addr,
				  B_AX_DISPATCHER_INTN_SEL_MASK, 8);
		p += scnprintf(p, end - p,
			       "Enable Dispatcher flow control dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_TXDMA:
		info = &dbg_port_pcie_txdma;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_TXDMA_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_TXDMA_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie txdma dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_RXDMA:
		info = &dbg_port_pcie_rxdma;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_RXDMA_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_RXDMA_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie rxdma dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_CVT:
		info = &dbg_port_pcie_cvt;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_CVT_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_CVT_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie cvt dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_CXPL:
		info = &dbg_port_pcie_cxpl;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_CXPL_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_CXPL_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie cxpl dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_IO:
		info = &dbg_port_pcie_io;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_IO_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_IO_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie io dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_MISC:
		info = &dbg_port_pcie_misc;
		val32 = rtw89_read32(rtwdev, R_AX_DBG_CTRL);
		val32 = u32_replace_bits(val32, PCIE_MISC_DBG_SEL, B_AX_DBG_SEL0);
		val32 = u32_replace_bits(val32, PCIE_MISC_DBG_SEL, B_AX_DBG_SEL1);
		rtw89_write32(rtwdev, R_AX_DBG_CTRL, val32);
		p += scnprintf(p, end - p, "Enable pcie misc dump.\n");
		break;
	case RTW89_DBG_PORT_SEL_PCIE_MISC2:
		info = &dbg_port_pcie_misc2;
		val16 = rtw89_read16(rtwdev, R_AX_PCIE_DBG_CTRL);
		val16 = u16_replace_bits(val16, PCIE_MISC2_DBG_SEL,
					 B_AX_PCIE_DBG_SEL_MASK);
		rtw89_write16(rtwdev, R_AX_PCIE_DBG_CTRL, val16);
		p += scnprintf(p, end - p, "Enable pcie misc2 dump.\n");
		break;
	default:
		p += scnprintf(p, end - p, "Dbg port select err\n");
		break;
	}

	*ppinfo = info;

	return p - buf;
}

static bool is_dbg_port_valid(struct rtw89_dev *rtwdev, u32 sel)
{
	if (rtwdev->hci.type != RTW89_HCI_TYPE_PCIE &&
	    sel >= RTW89_DBG_PORT_SEL_PCIE_TXDMA &&
	    sel <= RTW89_DBG_PORT_SEL_PCIE_MISC2)
		return false;
	if (rtw89_is_rtl885xb(rtwdev) &&
	    sel >= RTW89_DBG_PORT_SEL_PTCL_C1 &&
	    sel <= RTW89_DBG_PORT_SEL_TXTF_INFOH_C1)
		return false;
	if (rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL) &&
	    sel >= RTW89_DBG_PORT_SEL_WDE_BUFMGN_FREEPG &&
	    sel <= RTW89_DBG_PORT_SEL_PKTINFO)
		return false;
	if (rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL) &&
	    sel >= RTW89_DBG_PORT_SEL_DSPT_HDT_TX0 &&
	    sel <= RTW89_DBG_PORT_SEL_DSPT_FLOW_CTRL)
		return false;
	if (rtw89_mac_check_mac_en(rtwdev, 0, RTW89_CMAC_SEL) &&
	    sel >= RTW89_DBG_PORT_SEL_PTCL_C0 &&
	    sel <= RTW89_DBG_PORT_SEL_TXTF_INFOH_C0)
		return false;
	if (rtw89_mac_check_mac_en(rtwdev, 1, RTW89_CMAC_SEL) &&
	    sel >= RTW89_DBG_PORT_SEL_PTCL_C1 &&
	    sel <= RTW89_DBG_PORT_SEL_TXTF_INFOH_C1)
		return false;

	return true;
}

static int rtw89_debug_mac_dbg_port_dump(struct rtw89_dev *rtwdev,
					 char *buf, size_t bufsz, u32 sel)
{
	const struct rtw89_mac_dbg_port_info *info = NULL;
	char *p = buf, *end = buf + bufsz;
	u32 val32;
	u16 val16;
	u8 val8;
	u32 i;

	p += rtw89_debug_mac_dbg_port_sel(rtwdev, p, end - p, sel, &info);

	if (!info) {
		rtw89_err(rtwdev, "failed to select debug port %d\n", sel);
		goto out;
	}

#define case_DBG_SEL(__sel) \
	case RTW89_DBG_PORT_SEL_##__sel: \
		p += scnprintf(p, end - p, "Dump debug port " #__sel ":\n"); \
		break

	switch (sel) {
	case_DBG_SEL(PTCL_C0);
	case_DBG_SEL(PTCL_C1);
	case_DBG_SEL(SCH_C0);
	case_DBG_SEL(SCH_C1);
	case_DBG_SEL(TMAC_C0);
	case_DBG_SEL(TMAC_C1);
	case_DBG_SEL(RMAC_C0);
	case_DBG_SEL(RMAC_C1);
	case_DBG_SEL(RMACST_C0);
	case_DBG_SEL(RMACST_C1);
	case_DBG_SEL(TRXPTCL_C0);
	case_DBG_SEL(TRXPTCL_C1);
	case_DBG_SEL(TX_INFOL_C0);
	case_DBG_SEL(TX_INFOH_C0);
	case_DBG_SEL(TX_INFOL_C1);
	case_DBG_SEL(TX_INFOH_C1);
	case_DBG_SEL(TXTF_INFOL_C0);
	case_DBG_SEL(TXTF_INFOH_C0);
	case_DBG_SEL(TXTF_INFOL_C1);
	case_DBG_SEL(TXTF_INFOH_C1);
	case_DBG_SEL(WDE_BUFMGN_FREEPG);
	case_DBG_SEL(WDE_BUFMGN_QUOTA);
	case_DBG_SEL(WDE_BUFMGN_PAGELLT);
	case_DBG_SEL(WDE_BUFMGN_PKTINFO);
	case_DBG_SEL(WDE_QUEMGN_PREPKT);
	case_DBG_SEL(WDE_QUEMGN_NXTPKT);
	case_DBG_SEL(WDE_QUEMGN_QLNKTBL);
	case_DBG_SEL(WDE_QUEMGN_QEMPTY);
	case_DBG_SEL(PLE_BUFMGN_FREEPG);
	case_DBG_SEL(PLE_BUFMGN_QUOTA);
	case_DBG_SEL(PLE_BUFMGN_PAGELLT);
	case_DBG_SEL(PLE_BUFMGN_PKTINFO);
	case_DBG_SEL(PLE_QUEMGN_PREPKT);
	case_DBG_SEL(PLE_QUEMGN_NXTPKT);
	case_DBG_SEL(PLE_QUEMGN_QLNKTBL);
	case_DBG_SEL(PLE_QUEMGN_QEMPTY);
	case_DBG_SEL(PKTINFO);
	case_DBG_SEL(DSPT_HDT_TX0);
	case_DBG_SEL(DSPT_HDT_TX1);
	case_DBG_SEL(DSPT_HDT_TX2);
	case_DBG_SEL(DSPT_HDT_TX3);
	case_DBG_SEL(DSPT_HDT_TX4);
	case_DBG_SEL(DSPT_HDT_TX5);
	case_DBG_SEL(DSPT_HDT_TX6);
	case_DBG_SEL(DSPT_HDT_TX7);
	case_DBG_SEL(DSPT_HDT_TX8);
	case_DBG_SEL(DSPT_HDT_TX9);
	case_DBG_SEL(DSPT_HDT_TXA);
	case_DBG_SEL(DSPT_HDT_TXB);
	case_DBG_SEL(DSPT_HDT_TXC);
	case_DBG_SEL(DSPT_HDT_TXD);
	case_DBG_SEL(DSPT_HDT_TXE);
	case_DBG_SEL(DSPT_HDT_TXF);
	case_DBG_SEL(DSPT_CDT_TX0);
	case_DBG_SEL(DSPT_CDT_TX1);
	case_DBG_SEL(DSPT_CDT_TX3);
	case_DBG_SEL(DSPT_CDT_TX4);
	case_DBG_SEL(DSPT_CDT_TX5);
	case_DBG_SEL(DSPT_CDT_TX6);
	case_DBG_SEL(DSPT_CDT_TX7);
	case_DBG_SEL(DSPT_CDT_TX8);
	case_DBG_SEL(DSPT_CDT_TX9);
	case_DBG_SEL(DSPT_CDT_TXA);
	case_DBG_SEL(DSPT_CDT_TXB);
	case_DBG_SEL(DSPT_CDT_TXC);
	case_DBG_SEL(DSPT_HDT_RX0);
	case_DBG_SEL(DSPT_HDT_RX1);
	case_DBG_SEL(DSPT_HDT_RX2);
	case_DBG_SEL(DSPT_HDT_RX3);
	case_DBG_SEL(DSPT_HDT_RX4);
	case_DBG_SEL(DSPT_HDT_RX5);
	case_DBG_SEL(DSPT_CDT_RX_P0);
	case_DBG_SEL(DSPT_CDT_RX_P0_0);
	case_DBG_SEL(DSPT_CDT_RX_P0_1);
	case_DBG_SEL(DSPT_CDT_RX_P0_2);
	case_DBG_SEL(DSPT_CDT_RX_P1);
	case_DBG_SEL(DSPT_STF_CTRL);
	case_DBG_SEL(DSPT_ADDR_CTRL);
	case_DBG_SEL(DSPT_WDE_INTF);
	case_DBG_SEL(DSPT_PLE_INTF);
	case_DBG_SEL(DSPT_FLOW_CTRL);
	case_DBG_SEL(PCIE_TXDMA);
	case_DBG_SEL(PCIE_RXDMA);
	case_DBG_SEL(PCIE_CVT);
	case_DBG_SEL(PCIE_CXPL);
	case_DBG_SEL(PCIE_IO);
	case_DBG_SEL(PCIE_MISC);
	case_DBG_SEL(PCIE_MISC2);
	}

#undef case_DBG_SEL

	p += scnprintf(p, end - p, "Sel addr = 0x%X\n", info->sel_addr);
	p += scnprintf(p, end - p, "Read addr = 0x%X\n", info->rd_addr);

	for (i = info->srt; i <= info->end; i++) {
		switch (info->sel_byte) {
		case 1:
		default:
			rtw89_write8_mask(rtwdev, info->sel_addr,
					  info->sel_msk, i);
			p += scnprintf(p, end - p, "0x%02X: ", i);
			break;
		case 2:
			rtw89_write16_mask(rtwdev, info->sel_addr,
					   info->sel_msk, i);
			p += scnprintf(p, end - p, "0x%04X: ", i);
			break;
		case 4:
			rtw89_write32_mask(rtwdev, info->sel_addr,
					   info->sel_msk, i);
			p += scnprintf(p, end - p, "0x%04X: ", i);
			break;
		}

		udelay(10);

		switch (info->rd_byte) {
		case 1:
		default:
			val8 = rtw89_read8_mask(rtwdev,
						info->rd_addr, info->rd_msk);
			p += scnprintf(p, end - p, "0x%02X\n", val8);
			break;
		case 2:
			val16 = rtw89_read16_mask(rtwdev,
						  info->rd_addr, info->rd_msk);
			p += scnprintf(p, end - p, "0x%04X\n", val16);
			break;
		case 4:
			val32 = rtw89_read32_mask(rtwdev,
						  info->rd_addr, info->rd_msk);
			p += scnprintf(p, end - p, "0x%08X\n", val32);
			break;
		}
	}

out:
	return p - buf;
}

static int rtw89_debug_mac_dump_dbg_port(struct rtw89_dev *rtwdev,
					 char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;
	ssize_t n;
	u32 sel;

	for (sel = RTW89_DBG_PORT_SEL_PTCL_C0;
	     sel < RTW89_DBG_PORT_SEL_LAST; sel++) {
		if (!is_dbg_port_valid(rtwdev, sel))
			continue;
		n = rtw89_debug_mac_dbg_port_dump(rtwdev, p, end - p, sel);
		if (n < 0) {
			rtw89_err(rtwdev,
				  "failed to dump debug port %d\n", sel);
			break;
		}
		p += n;
	}

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mac_dbg_port_dump_get(struct rtw89_dev *rtwdev,
				       struct rtw89_debugfs_priv *debugfs_priv,
				       char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;

	if (debugfs_priv->dbgpkg_en.ss_dbg)
		p += rtw89_debug_mac_dump_ss_dbg(rtwdev, p, end - p);
	if (debugfs_priv->dbgpkg_en.dle_dbg)
		p += rtw89_debug_mac_dump_dle_dbg(rtwdev, p, end - p);
	if (debugfs_priv->dbgpkg_en.dmac_dbg)
		p += rtw89_debug_mac_dump_dmac_dbg(rtwdev, p, end - p);
	if (debugfs_priv->dbgpkg_en.cmac_dbg)
		p += rtw89_debug_mac_dump_cmac_dbg(rtwdev, p, end - p);
	if (debugfs_priv->dbgpkg_en.dbg_port)
		p += rtw89_debug_mac_dump_dbg_port(rtwdev, p, end - p);

	return p - buf;
};

static u8 *rtw89_hex2bin(struct rtw89_dev *rtwdev, const char *buf, size_t count)
{
	u8 *bin;
	int num;
	int err = 0;

	num = count / 2;
	bin = kmalloc(num, GFP_KERNEL);
	if (!bin) {
		err = -EFAULT;
		goto out;
	}

	if (hex2bin(bin, buf, num)) {
		rtw89_info(rtwdev, "valid format: H1H2H3...\n");
		kfree(bin);
		err = -EINVAL;
	}

out:
	return err ? ERR_PTR(err) : bin;
}

static ssize_t rtw89_debug_priv_send_h2c_set(struct rtw89_dev *rtwdev,
					     struct rtw89_debugfs_priv *debugfs_priv,
					     const char *buf, size_t count)
{
	u8 *h2c;
	int ret;
	u16 h2c_len = count / 2;

	h2c = rtw89_hex2bin(rtwdev, buf, count);
	if (IS_ERR(h2c))
		return -EFAULT;

	ret = rtw89_fw_h2c_raw(rtwdev, h2c, h2c_len);

	kfree(h2c);

	return ret ? ret : count;
}

static ssize_t
rtw89_debug_priv_early_h2c_get(struct rtw89_dev *rtwdev,
			       struct rtw89_debugfs_priv *debugfs_priv,
			       char *buf, size_t bufsz)
{
	struct rtw89_early_h2c *early_h2c;
	char *p = buf, *end = buf + bufsz;
	int seq = 0;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	list_for_each_entry(early_h2c, &rtwdev->early_h2c_list, list)
		p += scnprintf(p, end - p, "%d: %*ph\n", ++seq,
			       early_h2c->h2c_len, early_h2c->h2c);

	return p - buf;
}

static ssize_t
rtw89_debug_priv_early_h2c_set(struct rtw89_dev *rtwdev,
			       struct rtw89_debugfs_priv *debugfs_priv,
			       const char *buf, size_t count)
{
	struct rtw89_early_h2c *early_h2c;
	u8 *h2c;
	u16 h2c_len = count / 2;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	h2c = rtw89_hex2bin(rtwdev, buf, count);
	if (IS_ERR(h2c))
		return -EFAULT;

	if (h2c_len >= 2 && h2c[0] == 0x00 && h2c[1] == 0x00) {
		kfree(h2c);
		rtw89_fw_free_all_early_h2c(rtwdev);
		goto out;
	}

	early_h2c = kmalloc(sizeof(*early_h2c), GFP_KERNEL);
	if (!early_h2c) {
		kfree(h2c);
		return -EFAULT;
	}

	early_h2c->h2c = h2c;
	early_h2c->h2c_len = h2c_len;

	list_add_tail(&early_h2c->list, &rtwdev->early_h2c_list);

out:
	return count;
}

static int rtw89_dbg_trigger_ctrl_error(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_cpuio_ctrl ctrl_para = {0};
	u16 pkt_id;
	int ret;

	rtw89_leave_ps_mode(rtwdev);

	ret = mac->dle_buf_req(rtwdev, 0x20, true, &pkt_id);
	if (ret)
		return ret;

	/* intentionally, enqueue two pkt, but has only one pkt id */
	ctrl_para.cmd_type = CPUIO_OP_CMD_ENQ_TO_HEAD;
	ctrl_para.start_pktid = pkt_id;
	ctrl_para.end_pktid = pkt_id;
	ctrl_para.pkt_num = 1; /* start from 0 */
	ctrl_para.dst_pid = WDE_DLE_PORT_ID_WDRLS;
	ctrl_para.dst_qid = WDE_DLE_QUEID_NO_REPORT;

	if (mac->set_cpuio(rtwdev, &ctrl_para, true))
		return -EFAULT;

	return 0;
}

static ssize_t
rtw89_debug_priv_fw_crash_get(struct rtw89_dev *rtwdev,
			      struct rtw89_debugfs_priv *debugfs_priv,
			      char *buf, size_t bufsz)
{
	char *p = buf, *end = buf + bufsz;

	p += scnprintf(p, end - p, "%d\n",
		       test_bit(RTW89_FLAG_CRASH_SIMULATING, rtwdev->flags));
	return p - buf;
}

enum rtw89_dbg_crash_simulation_type {
	RTW89_DBG_SIM_CPU_EXCEPTION = 1,
	RTW89_DBG_SIM_CTRL_ERROR = 2,
};

static ssize_t
rtw89_debug_priv_fw_crash_set(struct rtw89_dev *rtwdev,
			      struct rtw89_debugfs_priv *debugfs_priv,
			      const char *buf, size_t count)
{
	int (*sim)(struct rtw89_dev *rtwdev);
	u8 crash_type;
	int ret;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	ret = kstrtou8(buf, 0, &crash_type);
	if (ret)
		return -EINVAL;

	switch (crash_type) {
	case RTW89_DBG_SIM_CPU_EXCEPTION:
		if (!RTW89_CHK_FW_FEATURE_GROUP(CRASH_TRIGGER, &rtwdev->fw))
			return -EOPNOTSUPP;
		sim = rtw89_fw_h2c_trigger_cpu_exception;
		break;
	case RTW89_DBG_SIM_CTRL_ERROR:
		sim = rtw89_dbg_trigger_ctrl_error;
		break;
	default:
		return -EINVAL;
	}

	set_bit(RTW89_FLAG_CRASH_SIMULATING, rtwdev->flags);
	ret = sim(rtwdev);

	if (ret)
		return ret;

	return count;
}

static ssize_t rtw89_debug_priv_btc_info_get(struct rtw89_dev *rtwdev,
					     struct rtw89_debugfs_priv *debugfs_priv,
					     char *buf, size_t bufsz)
{
	return rtw89_btc_dump_info(rtwdev, buf, bufsz);
}

static ssize_t rtw89_debug_priv_btc_manual_set(struct rtw89_dev *rtwdev,
					       struct rtw89_debugfs_priv *debugfs_priv,
					       const char *buf, size_t count)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_btc_ver *ver = btc->ver;
	int ret;

	ret = kstrtobool(buf, &btc->manual_ctrl);
	if (ret)
		return ret;

	if (ver->fcxctrl == 7)
		btc->ctrl.ctrl_v7.manual = btc->manual_ctrl;
	else
		btc->ctrl.ctrl.manual = btc->manual_ctrl;

	return count;
}

static ssize_t rtw89_debug_priv_fw_log_manual_set(struct rtw89_dev *rtwdev,
						  struct rtw89_debugfs_priv *debugfs_priv,
						  const char *buf, size_t count)
{
	struct rtw89_fw_log *log = &rtwdev->fw.log;
	bool fw_log_manual;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (kstrtobool(buf, &fw_log_manual))
		goto out;

	log->enable = fw_log_manual;
	if (log->enable)
		rtw89_fw_log_prepare(rtwdev);
	rtw89_fw_h2c_fw_log(rtwdev, fw_log_manual);
out:
	return count;
}

static int rtw89_sta_link_info_get_iter(struct rtw89_dev *rtwdev,
					char *buf, size_t bufsz,
					struct rtw89_sta_link *rtwsta_link)
{
	static const char * const he_gi_str[] = {
		[NL80211_RATE_INFO_HE_GI_0_8] = "0.8",
		[NL80211_RATE_INFO_HE_GI_1_6] = "1.6",
		[NL80211_RATE_INFO_HE_GI_3_2] = "3.2",
	};
	static const char * const eht_gi_str[] = {
		[NL80211_RATE_INFO_EHT_GI_0_8] = "0.8",
		[NL80211_RATE_INFO_EHT_GI_1_6] = "1.6",
		[NL80211_RATE_INFO_EHT_GI_3_2] = "3.2",
	};
	struct rate_info *rate = &rtwsta_link->ra_report.txrate;
	struct ieee80211_rx_status *status = &rtwsta_link->rx_status;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 ant_num = hal->ant_diversity ? 2 : rtwdev->chip->rf_path_num;
	bool ant_asterisk = hal->tx_path_diversity || hal->ant_diversity;
	struct ieee80211_link_sta *link_sta;
	char *p = buf, *end = buf + bufsz;
	u8 evm_min, evm_max, evm_1ss;
	u16 max_rc_amsdu_len;
	u8 rssi;
	u8 snr;
	int i;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
	max_rc_amsdu_len = link_sta->agg.max_rc_amsdu_len;

	rcu_read_unlock();

	p += scnprintf(p, end - p, "TX rate [%u, %u]: ", rtwsta_link->mac_id,
		       rtwsta_link->link_id);

	if (rate->flags & RATE_INFO_FLAGS_MCS)
		p += scnprintf(p, end - p, "HT MCS-%d%s", rate->mcs,
			       rate->flags & RATE_INFO_FLAGS_SHORT_GI ? " SGI" : "");
	else if (rate->flags & RATE_INFO_FLAGS_VHT_MCS)
		p += scnprintf(p, end - p, "VHT %dSS MCS-%d%s", rate->nss,
			       rate->mcs,
			       rate->flags & RATE_INFO_FLAGS_SHORT_GI ? " SGI" : "");
	else if (rate->flags & RATE_INFO_FLAGS_HE_MCS)
		p += scnprintf(p, end - p, "HE %dSS MCS-%d GI:%s", rate->nss,
			       rate->mcs,
			       rate->he_gi <= NL80211_RATE_INFO_HE_GI_3_2 ?
			       he_gi_str[rate->he_gi] : "N/A");
	else if (rate->flags & RATE_INFO_FLAGS_EHT_MCS)
		p += scnprintf(p, end - p, "EHT %dSS MCS-%d GI:%s", rate->nss,
			       rate->mcs,
			       rate->eht_gi < ARRAY_SIZE(eht_gi_str) ?
			       eht_gi_str[rate->eht_gi] : "N/A");
	else
		p += scnprintf(p, end - p, "Legacy %d", rate->legacy);
	p += scnprintf(p, end - p, "%s",
		       rtwsta_link->ra_report.might_fallback_legacy ? " FB_G" : "");
	p += scnprintf(p, end - p, " BW:%u",
		       rtw89_rate_info_bw_to_mhz(rate->bw));
	p += scnprintf(p, end - p, " (hw_rate=0x%x)",
		       rtwsta_link->ra_report.hw_rate);
	p += scnprintf(p, end - p, " ==> agg_wait=%d (%d)\n",
		       rtwsta_link->max_agg_wait,
		       max_rc_amsdu_len);

	p += scnprintf(p, end - p, "RX rate [%u, %u]: ", rtwsta_link->mac_id,
		       rtwsta_link->link_id);

	switch (status->encoding) {
	case RX_ENC_LEGACY:
		p += scnprintf(p, end - p, "Legacy %d", status->rate_idx +
			       (status->band != NL80211_BAND_2GHZ ? 4 : 0));
		break;
	case RX_ENC_HT:
		p += scnprintf(p, end - p, "HT MCS-%d%s", status->rate_idx,
			       status->enc_flags & RX_ENC_FLAG_SHORT_GI ? " SGI" : "");
		break;
	case RX_ENC_VHT:
		p += scnprintf(p, end - p, "VHT %dSS MCS-%d%s", status->nss,
			       status->rate_idx,
			       status->enc_flags & RX_ENC_FLAG_SHORT_GI ? " SGI" : "");
		break;
	case RX_ENC_HE:
		p += scnprintf(p, end - p, "HE %dSS MCS-%d GI:%s",
			       status->nss, status->rate_idx,
			       status->he_gi <= NL80211_RATE_INFO_HE_GI_3_2 ?
			       he_gi_str[status->he_gi] : "N/A");
		break;
	case RX_ENC_EHT:
		p += scnprintf(p, end - p, "EHT %dSS MCS-%d GI:%s",
			       status->nss, status->rate_idx,
			       status->eht.gi < ARRAY_SIZE(eht_gi_str) ?
			       eht_gi_str[status->eht.gi] : "N/A");
		break;
	}
	p += scnprintf(p, end - p, " BW:%u",
		       rtw89_rate_info_bw_to_mhz(status->bw));
	p += scnprintf(p, end - p, " (hw_rate=0x%x)\n",
		       rtwsta_link->rx_hw_rate);

	rssi = ewma_rssi_read(&rtwsta_link->avg_rssi);
	p += scnprintf(p, end - p, "RSSI: %d dBm (raw=%d, prev=%d) [",
		       RTW89_RSSI_RAW_TO_DBM(rssi), rssi,
		       rtwsta_link->prev_rssi);
	for (i = 0; i < ant_num; i++) {
		rssi = ewma_rssi_read(&rtwsta_link->rssi[i]);
		p += scnprintf(p, end - p, "%d%s%s",
			       RTW89_RSSI_RAW_TO_DBM(rssi),
			       ant_asterisk && (hal->antenna_tx & BIT(i)) ? "*" : "",
			       i + 1 == ant_num ? "" : ", ");
	}
	p += scnprintf(p, end - p, "]\n");

	evm_1ss = ewma_evm_read(&rtwsta_link->evm_1ss);
	p += scnprintf(p, end - p, "EVM: [%2u.%02u, ", evm_1ss >> 2,
		       (evm_1ss & 0x3) * 25);
	for (i = 0; i < (hal->ant_diversity ? 2 : 1); i++) {
		evm_min = ewma_evm_read(&rtwsta_link->evm_min[i]);
		evm_max = ewma_evm_read(&rtwsta_link->evm_max[i]);

		p += scnprintf(p, end - p, "%s(%2u.%02u, %2u.%02u)",
			       i == 0 ? "" : " ",
			       evm_min >> 2, (evm_min & 0x3) * 25,
			       evm_max >> 2, (evm_max & 0x3) * 25);
	}
	p += scnprintf(p, end - p, "]\t");

	snr = ewma_snr_read(&rtwsta_link->avg_snr);
	p += scnprintf(p, end - p, "SNR: %u\n", snr);

	return p - buf;
}

static void rtw89_sta_info_get_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_debugfs_iter_data *iter_data =
		(struct rtw89_debugfs_iter_data *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;
	struct rtw89_sta_link *rtwsta_link;
	size_t bufsz = iter_data->bufsz;
	char *buf = iter_data->buf;
	char *p = buf, *end = buf + bufsz;
	unsigned int link_id;

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id)
		p += rtw89_sta_link_info_get_iter(rtwdev, p, end - p, rtwsta_link);

	rtw89_debugfs_iter_data_next(iter_data, p, end - p, p - buf);
}

static int
rtw89_debug_append_rx_rate(char *buf, size_t bufsz, struct rtw89_pkt_stat *pkt_stat,
			   enum rtw89_hw_rate first_rate, int len)
{
	char *p = buf, *end = buf + bufsz;
	int i;

	for (i = 0; i < len; i++)
		p += scnprintf(p, end - p, "%s%u", i == 0 ? "" : ", ",
			       pkt_stat->rx_rate_cnt[first_rate + i]);

	return p - buf;
}

#define FIRST_RATE_SAME(rate) {RTW89_HW_RATE_ ## rate, RTW89_HW_RATE_ ## rate}
#define FIRST_RATE_ENUM(rate) {RTW89_HW_RATE_ ## rate, RTW89_HW_RATE_V1_ ## rate}
#define FIRST_RATE_GEV1(rate) {RTW89_HW_RATE_INVAL, RTW89_HW_RATE_V1_ ## rate}

static const struct rtw89_rx_rate_cnt_info {
	enum rtw89_hw_rate first_rate[RTW89_CHIP_GEN_NUM];
	int len;
	int ext;
	const char *rate_mode;
} rtw89_rx_rate_cnt_infos[] = {
	{FIRST_RATE_SAME(CCK1), 4, 0, "Legacy:"},
	{FIRST_RATE_SAME(OFDM6), 8, 0, "OFDM:"},
	{FIRST_RATE_ENUM(MCS0), 8, 0, "HT 0:"},
	{FIRST_RATE_ENUM(MCS8), 8, 0, "HT 1:"},
	{FIRST_RATE_ENUM(VHT_NSS1_MCS0), 10, 2, "VHT 1SS:"},
	{FIRST_RATE_ENUM(VHT_NSS2_MCS0), 10, 2, "VHT 2SS:"},
	{FIRST_RATE_ENUM(HE_NSS1_MCS0), 12, 0, "HE 1SS:"},
	{FIRST_RATE_ENUM(HE_NSS2_MCS0), 12, 0, "HE 2SS:"},
	{FIRST_RATE_GEV1(EHT_NSS1_MCS0), 14, 2, "EHT 1SS:"},
	{FIRST_RATE_GEV1(EHT_NSS2_MCS0), 14, 0, "EHT 2SS:"},
};

static ssize_t rtw89_debug_priv_phy_info_get(struct rtw89_dev *rtwdev,
					     struct rtw89_debugfs_priv *debugfs_priv,
					     char *buf, size_t bufsz)
{
	struct rtw89_traffic_stats *stats = &rtwdev->stats;
	struct rtw89_pkt_stat *pkt_stat = &rtwdev->phystat.last_pkt_stat;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_debugfs_iter_data iter_data;
	const struct rtw89_rx_rate_cnt_info *info;
	struct rtw89_hal *hal = &rtwdev->hal;
	char *p = buf, *end = buf + bufsz;
	enum rtw89_hw_rate first_rate;
	u8 rssi;
	int i;

	rssi = ewma_rssi_read(&rtwdev->phystat.bcn_rssi);

	p += scnprintf(p, end - p, "TP TX: %u [%u] Mbps (lv: %d",
		       stats->tx_throughput, stats->tx_throughput_raw,
		       stats->tx_tfc_lv);
	if (hal->thermal_prot_lv)
		p += scnprintf(p, end - p, ", duty: %d%%",
			       100 - hal->thermal_prot_lv * RTW89_THERMAL_PROT_STEP);
	p += scnprintf(p, end - p, "), RX: %u [%u] Mbps (lv: %d)\n",
		       stats->rx_throughput, stats->rx_throughput_raw,
		       stats->rx_tfc_lv);
	p += scnprintf(p, end - p, "Beacon: %u (%d dBm), TF: %u\n",
		       pkt_stat->beacon_nr,
		       RTW89_RSSI_RAW_TO_DBM(rssi), stats->rx_tf_periodic);
	p += scnprintf(p, end - p, "Avg packet length: TX=%u, RX=%u\n",
		       stats->tx_avg_len,
		       stats->rx_avg_len);

	p += scnprintf(p, end - p, "RX count:\n");

	for (i = 0; i < ARRAY_SIZE(rtw89_rx_rate_cnt_infos); i++) {
		info = &rtw89_rx_rate_cnt_infos[i];
		first_rate = info->first_rate[chip->chip_gen];
		if (first_rate >= RTW89_HW_RATE_NR)
			continue;

		p += scnprintf(p, end - p, "%10s [", info->rate_mode);
		p += rtw89_debug_append_rx_rate(p, end - p, pkt_stat,
						first_rate, info->len);
		if (info->ext) {
			p += scnprintf(p, end - p, "][");
			p += rtw89_debug_append_rx_rate(p, end - p, pkt_stat,
							first_rate + info->len, info->ext);
		}
		p += scnprintf(p, end - p, "]\n");
	}

	rtw89_debugfs_iter_data_setup(&iter_data, p, end - p);
	ieee80211_iterate_stations_atomic(rtwdev->hw, rtw89_sta_info_get_iter, &iter_data);
	p += iter_data.written_sz;

	return p - buf;
}

static int rtw89_dump_addr_cam(struct rtw89_dev *rtwdev,
			       char *buf, size_t bufsz,
			       struct rtw89_addr_cam_entry *addr_cam)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	const struct rtw89_sec_cam_entry *sec_entry;
	char *p = buf, *end = buf + bufsz;
	u8 sec_cam_idx;
	int i;

	p += scnprintf(p, end - p, "\taddr_cam_idx=%u\n",
		       addr_cam->addr_cam_idx);
	p += scnprintf(p, end - p, "\t-> bssid_cam_idx=%u\n",
		       addr_cam->bssid_cam_idx);
	p += scnprintf(p, end - p, "\tsec_cam_bitmap=%*ph\n",
		       (int)sizeof(addr_cam->sec_cam_map),
		       addr_cam->sec_cam_map);
	for_each_set_bit(i, addr_cam->sec_cam_map, RTW89_SEC_CAM_IN_ADDR_CAM) {
		sec_cam_idx = addr_cam->sec_ent[i];
		sec_entry = cam_info->sec_entries[sec_cam_idx];
		if (!sec_entry)
			continue;
		p += scnprintf(p, end - p, "\tsec[%d]: sec_cam_idx %u", i,
			       sec_entry->sec_cam_idx);
		if (sec_entry->ext_key)
			p += scnprintf(p, end - p, ", %u",
				       sec_entry->sec_cam_idx + 1);
		p += scnprintf(p, end - p, "\n");
	}

	return p - buf;
}

__printf(4, 5)
static int rtw89_dump_pkt_offload(char *buf, size_t bufsz, struct list_head *pkt_list,
				  const char *fmt, ...)
{
	char *p = buf, *end = buf + bufsz;
	struct rtw89_pktofld_info *info;
	struct va_format vaf;
	va_list args;

	if (list_empty(pkt_list))
		return 0;

	va_start(args, fmt);
	vaf.va = &args;
	vaf.fmt = fmt;

	p += scnprintf(p, end - p, "%pV", &vaf);

	va_end(args);

	list_for_each_entry(info, pkt_list, list)
		p += scnprintf(p, end - p, "%d ", info->id);

	p += scnprintf(p, end - p, "\n");

	return p - buf;
}

static int rtw89_vif_link_ids_get(struct rtw89_dev *rtwdev,
				  char *buf, size_t bufsz, u8 *mac,
				  struct rtw89_vif_link *rtwvif_link,
				  bool designated)
{
	struct rtw89_bssid_cam_entry *bssid_cam = &rtwvif_link->bssid_cam;
	char *p = buf, *end = buf + bufsz;

	p += scnprintf(p, end - p, "    [%u] %pM\n", rtwvif_link->mac_id,
		       rtwvif_link->mac_addr);
	p += scnprintf(p, end - p, "\tlink_id=%u%s\n", rtwvif_link->link_id,
		       designated ? " (*)" : "");
	p += scnprintf(p, end - p, "\tbssid_cam_idx=%u\n",
		       bssid_cam->bssid_cam_idx);
	p += rtw89_dump_addr_cam(rtwdev, p, end - p, &rtwvif_link->addr_cam);
	p += rtw89_dump_pkt_offload(p, end - p, &rtwvif_link->general_pkt_list,
				    "\tpkt_ofld[GENERAL]: ");

	return p - buf;
}

static
void rtw89_vif_ids_get_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct rtw89_debugfs_iter_data *iter_data =
		(struct rtw89_debugfs_iter_data *)data;
	struct rtw89_vif *rtwvif = vif_to_rtwvif(vif);
	struct rtw89_dev *rtwdev = rtwvif->rtwdev;
	struct rtw89_vif_link *designated_link;
	struct rtw89_vif_link *rtwvif_link;
	size_t bufsz = iter_data->bufsz;
	char *buf = iter_data->buf;
	char *p = buf, *end = buf + bufsz;
	unsigned int link_id;

	designated_link = rtw89_get_designated_link(rtwvif);

	p += scnprintf(p, end - p, "VIF %pM\n", rtwvif->mac_addr);
	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
		p += rtw89_vif_link_ids_get(rtwdev, p, end - p, mac, rtwvif_link,
					    rtwvif_link == designated_link);

	rtw89_debugfs_iter_data_next(iter_data, p, end - p, p - buf);
}

static int rtw89_dump_ba_cam(struct rtw89_dev *rtwdev,
			     char *buf, size_t bufsz,
			     struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_ba_cam_entry *entry;
	char *p = buf, *end = buf + bufsz;
	bool first = true;

	list_for_each_entry(entry, &rtwsta_link->ba_cam_list, list) {
		if (first) {
			p += scnprintf(p, end - p, "\tba_cam ");
			first = false;
		} else {
			p += scnprintf(p, end - p, ", ");
		}
		p += scnprintf(p, end - p, "tid[%u]=%d", entry->tid,
			       (int)(entry - rtwdev->cam_info.ba_cam_entry));
	}
	p += scnprintf(p, end - p, "\n");

	return p - buf;
}

static int rtw89_sta_link_ids_get(struct rtw89_dev *rtwdev,
				  char *buf, size_t bufsz,
				  struct rtw89_sta_link *rtwsta_link,
				  bool designated)
{
	struct ieee80211_link_sta *link_sta;
	char *p = buf, *end = buf + bufsz;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);

	p += scnprintf(p, end - p, "    [%u] %pM\n", rtwsta_link->mac_id,
		       link_sta->addr);

	rcu_read_unlock();

	p += scnprintf(p, end - p, "\tlink_id=%u%s\n", rtwsta_link->link_id,
		       designated ? " (*)" : "");
	p += rtw89_dump_addr_cam(rtwdev, p, end - p, &rtwsta_link->addr_cam);
	p += rtw89_dump_ba_cam(rtwdev, p, end - p, rtwsta_link);

	return p - buf;
}

static void rtw89_sta_ids_get_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_debugfs_iter_data *iter_data =
		(struct rtw89_debugfs_iter_data *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;
	struct rtw89_sta_link *designated_link;
	struct rtw89_sta_link *rtwsta_link;
	size_t bufsz = iter_data->bufsz;
	char *buf = iter_data->buf;
	char *p = buf, *end = buf + bufsz;
	unsigned int link_id;

	designated_link = rtw89_get_designated_link(rtwsta);

	p += scnprintf(p, end - p, "STA %pM %s\n", sta->addr,
		       sta->tdls ? "(TDLS)" : "");
	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id)
		p += rtw89_sta_link_ids_get(rtwdev, p, end - p, rtwsta_link,
					    rtwsta_link == designated_link);

	rtw89_debugfs_iter_data_next(iter_data, p, end - p, p - buf);
}

static ssize_t rtw89_debug_priv_stations_get(struct rtw89_dev *rtwdev,
					     struct rtw89_debugfs_priv *debugfs_priv,
					     char *buf, size_t bufsz)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	struct rtw89_debugfs_iter_data iter_data;
	char *p = buf, *end = buf + bufsz;
	u8 idx;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	p += scnprintf(p, end - p, "map:\n");
	p += scnprintf(p, end - p, "\tmac_id:    %*ph\n",
		       (int)sizeof(rtwdev->mac_id_map),
		       rtwdev->mac_id_map);
	p += scnprintf(p, end - p, "\taddr_cam:  %*ph\n",
		       (int)sizeof(cam_info->addr_cam_map),
		       cam_info->addr_cam_map);
	p += scnprintf(p, end - p, "\tbssid_cam: %*ph\n",
		       (int)sizeof(cam_info->bssid_cam_map),
		       cam_info->bssid_cam_map);
	p += scnprintf(p, end - p, "\tsec_cam:   %*ph\n",
		       (int)sizeof(cam_info->sec_cam_map),
		       cam_info->sec_cam_map);
	p += scnprintf(p, end - p, "\tba_cam:    %*ph\n",
		       (int)sizeof(cam_info->ba_cam_map),
		       cam_info->ba_cam_map);
	p += scnprintf(p, end - p, "\tpkt_ofld:  %*ph\n",
		       (int)sizeof(rtwdev->pkt_offload),
		       rtwdev->pkt_offload);

	for (idx = NL80211_BAND_2GHZ; idx < NUM_NL80211_BANDS; idx++) {
		if (!(rtwdev->chip->support_bands & BIT(idx)))
			continue;
		p += rtw89_dump_pkt_offload(p, end - p, &rtwdev->scan_info.pkt_list[idx],
					    "\t\t[SCAN %u]: ", idx);
	}

	rtw89_debugfs_iter_data_setup(&iter_data, p, end - p);
	ieee80211_iterate_active_interfaces_atomic(rtwdev->hw,
		IEEE80211_IFACE_ITER_NORMAL, rtw89_vif_ids_get_iter, &iter_data);
	p += iter_data.written_sz;

	rtw89_debugfs_iter_data_setup(&iter_data, p, end - p);
	ieee80211_iterate_stations_atomic(rtwdev->hw, rtw89_sta_ids_get_iter, &iter_data);
	p += iter_data.written_sz;

	return p - buf;
}

static void rtw89_debug_disable_dm_cfg_bmap(struct rtw89_dev *rtwdev, u32 new)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 old = hal->disabled_dm_bitmap;

	if (new == old)
		return;

	hal->disabled_dm_bitmap = new;

	rtw89_debug(rtwdev, RTW89_DBG_STATE, "Disable DM: 0x%x -> 0x%x\n", old, new);
}

static void rtw89_debug_disable_dm_set_flag(struct rtw89_dev *rtwdev, u8 flag)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 cur = hal->disabled_dm_bitmap;

	rtw89_debug_disable_dm_cfg_bmap(rtwdev, cur | BIT(flag));
}

static void rtw89_debug_disable_dm_clr_flag(struct rtw89_dev *rtwdev, u8 flag)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 cur = hal->disabled_dm_bitmap;

	rtw89_debug_disable_dm_cfg_bmap(rtwdev, cur & ~BIT(flag));
}

#define DM_INFO(type) {RTW89_DM_ ## type, #type}

static const struct rtw89_disabled_dm_info {
	enum rtw89_dm_type type;
	const char *name;
} rtw89_disabled_dm_infos[] = {
	DM_INFO(DYNAMIC_EDCCA),
	DM_INFO(THERMAL_PROTECT),
	DM_INFO(TAS),
	DM_INFO(MLO),
};

static ssize_t
rtw89_debug_priv_disable_dm_get(struct rtw89_dev *rtwdev,
				struct rtw89_debugfs_priv *debugfs_priv,
				char *buf, size_t bufsz)
{
	const struct rtw89_disabled_dm_info *info;
	struct rtw89_hal *hal = &rtwdev->hal;
	char *p = buf, *end = buf + bufsz;
	u32 disabled;
	int i;

	p += scnprintf(p, end - p, "Disabled DM: 0x%x\n",
		       hal->disabled_dm_bitmap);

	for (i = 0; i < ARRAY_SIZE(rtw89_disabled_dm_infos); i++) {
		info = &rtw89_disabled_dm_infos[i];
		disabled = BIT(info->type) & hal->disabled_dm_bitmap;

		p += scnprintf(p, end - p, "[%d] %s: %c\n", info->type,
			       info->name,
			       disabled ? 'X' : 'O');
	}

	return p - buf;
}

static ssize_t
rtw89_debug_priv_disable_dm_set(struct rtw89_dev *rtwdev,
				struct rtw89_debugfs_priv *debugfs_priv,
				const char *buf, size_t count)
{
	u32 conf;
	int ret;

	ret = kstrtou32(buf, 0, &conf);
	if (ret)
		return -EINVAL;

	rtw89_debug_disable_dm_cfg_bmap(rtwdev, conf);

	return count;
}

static void rtw89_debug_mlo_mode_set_mlsr(struct rtw89_dev *rtwdev,
					  unsigned int link_id)
{
	struct ieee80211_vif *vif;
	struct rtw89_vif *rtwvif;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		vif = rtwvif_to_vif(rtwvif);
		if (!ieee80211_vif_is_mld(vif))
			continue;

		rtw89_core_mlsr_switch(rtwdev, rtwvif, link_id);
	}
}

static ssize_t
rtw89_debug_priv_mlo_mode_get(struct rtw89_dev *rtwdev,
			      struct rtw89_debugfs_priv *debugfs_priv,
			      char *buf, size_t bufsz)
{
	bool mlo_dm_dis = rtwdev->hal.disabled_dm_bitmap & BIT(RTW89_DM_MLO);
	char *p = buf, *end = buf + bufsz;
	struct ieee80211_vif *vif;
	struct rtw89_vif *rtwvif;
	int count = 0;

	p += scnprintf(p, end - p, "MLD(s) status: (MLO DM: %s)\n",
		       str_disable_enable(mlo_dm_dis));

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		vif = rtwvif_to_vif(rtwvif);
		if (!ieee80211_vif_is_mld(vif))
			continue;

		p += scnprintf(p, end - p,
			       "\t#%u: MLO mode %x, valid 0x%x, active 0x%x\n",
			       count++, rtwvif->mlo_mode, vif->valid_links,
			       vif->active_links);
	}

	if (count == 0)
		p += scnprintf(p, end - p, "\t(None)\n");

	return p - buf;
}

static ssize_t
rtw89_debug_priv_mlo_mode_set(struct rtw89_dev *rtwdev,
			      struct rtw89_debugfs_priv *debugfs_priv,
			      const char *buf, size_t count)
{
	u8 num, mlo_mode;
	u32 argv;

	num = sscanf(buf, "%hhx %u", &mlo_mode, &argv);
	if (num != 2)
		return -EINVAL;

	rtw89_debug_disable_dm_set_flag(rtwdev, RTW89_DM_MLO);

	rtw89_debug(rtwdev, RTW89_DBG_STATE, "Set MLO mode to %x\n", mlo_mode);

	switch (mlo_mode) {
	case RTW89_MLO_MODE_MLSR:
		rtw89_debug_mlo_mode_set_mlsr(rtwdev, argv);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_STATE, "Unsupported MLO mode\n");
		rtw89_debug_disable_dm_clr_flag(rtwdev, RTW89_DM_MLO);

		return -EOPNOTSUPP;
	}

	return count;
}

#define rtw89_debug_priv_get(name, opts...)			\
{								\
	.cb_read = rtw89_debug_priv_ ##name## _get,		\
	.opt = { opts },					\
}

#define rtw89_debug_priv_set(name, opts...)			\
{								\
	.cb_write = rtw89_debug_priv_ ##name## _set,		\
	.opt = { opts },					\
}

#define rtw89_debug_priv_select_and_get(name, opts...)		\
{								\
	.cb_write = rtw89_debug_priv_ ##name## _select,		\
	.cb_read = rtw89_debug_priv_ ##name## _get,		\
	.opt = { opts },					\
}

#define rtw89_debug_priv_set_and_get(name, opts...)		\
{								\
	.cb_write = rtw89_debug_priv_ ##name## _set,		\
	.cb_read = rtw89_debug_priv_ ##name## _get,		\
	.opt = { opts },					\
}

#define RSIZE_8K .rsize = 0x2000
#define RSIZE_12K .rsize = 0x3000
#define RSIZE_16K .rsize = 0x4000
#define RSIZE_20K .rsize = 0x5000
#define RSIZE_32K .rsize = 0x8000
#define RSIZE_64K .rsize = 0x10000
#define RSIZE_128K .rsize = 0x20000
#define RSIZE_1M .rsize = 0x100000
#define RLOCK .rlock = 1
#define WLOCK .wlock = 1
#define RWLOCK RLOCK, WLOCK

static const struct rtw89_debugfs rtw89_debugfs_templ = {
	.read_reg = rtw89_debug_priv_select_and_get(read_reg),
	.write_reg = rtw89_debug_priv_set(write_reg),
	.read_rf = rtw89_debug_priv_select_and_get(read_rf),
	.write_rf = rtw89_debug_priv_set(write_rf),
	.rf_reg_dump = rtw89_debug_priv_get(rf_reg_dump, RSIZE_8K),
	.txpwr_table = rtw89_debug_priv_get(txpwr_table, RSIZE_20K, RLOCK),
	.mac_reg_dump = rtw89_debug_priv_select_and_get(mac_reg_dump, RSIZE_128K),
	.mac_mem_dump = rtw89_debug_priv_select_and_get(mac_mem_dump, RSIZE_16K, RLOCK),
	.mac_dbg_port_dump = rtw89_debug_priv_select_and_get(mac_dbg_port_dump, RSIZE_1M),
	.send_h2c = rtw89_debug_priv_set(send_h2c),
	.early_h2c = rtw89_debug_priv_set_and_get(early_h2c, RWLOCK),
	.fw_crash = rtw89_debug_priv_set_and_get(fw_crash, WLOCK),
	.btc_info = rtw89_debug_priv_get(btc_info, RSIZE_12K),
	.btc_manual = rtw89_debug_priv_set(btc_manual),
	.fw_log_manual = rtw89_debug_priv_set(fw_log_manual, WLOCK),
	.phy_info = rtw89_debug_priv_get(phy_info),
	.stations = rtw89_debug_priv_get(stations, RLOCK),
	.disable_dm = rtw89_debug_priv_set_and_get(disable_dm, RWLOCK),
	.mlo_mode = rtw89_debug_priv_set_and_get(mlo_mode, RWLOCK),
};

#define rtw89_debugfs_add(name, mode, fopname, parent)				\
	do {									\
		struct rtw89_debugfs_priv *priv = &rtwdev->debugfs->name;	\
		priv->rtwdev = rtwdev;						\
		if (IS_ERR(debugfs_create_file(#name, mode, parent, priv,	\
					       &file_ops_ ##fopname)))		\
			pr_debug("Unable to initialize debugfs:%s\n", #name);	\
	} while (0)

#define rtw89_debugfs_add_w(name)						\
	rtw89_debugfs_add(name, S_IFREG | 0222, single_w, debugfs_topdir)
#define rtw89_debugfs_add_rw(name)						\
	rtw89_debugfs_add(name, S_IFREG | 0666, common_rw, debugfs_topdir)
#define rtw89_debugfs_add_r(name)						\
	rtw89_debugfs_add(name, S_IFREG | 0444, single_r, debugfs_topdir)

static
void rtw89_debugfs_add_sec0(struct rtw89_dev *rtwdev, struct dentry *debugfs_topdir)
{
	rtw89_debugfs_add_rw(read_reg);
	rtw89_debugfs_add_w(write_reg);
	rtw89_debugfs_add_rw(read_rf);
	rtw89_debugfs_add_w(write_rf);
	rtw89_debugfs_add_r(rf_reg_dump);
	rtw89_debugfs_add_r(txpwr_table);
	rtw89_debugfs_add_rw(mac_reg_dump);
	rtw89_debugfs_add_rw(mac_mem_dump);
	rtw89_debugfs_add_rw(mac_dbg_port_dump);
}

static
void rtw89_debugfs_add_sec1(struct rtw89_dev *rtwdev, struct dentry *debugfs_topdir)
{
	rtw89_debugfs_add_w(send_h2c);
	rtw89_debugfs_add_rw(early_h2c);
	rtw89_debugfs_add_rw(fw_crash);
	rtw89_debugfs_add_r(btc_info);
	rtw89_debugfs_add_w(btc_manual);
	rtw89_debugfs_add_w(fw_log_manual);
	rtw89_debugfs_add_r(phy_info);
	rtw89_debugfs_add_r(stations);
	rtw89_debugfs_add_rw(disable_dm);
	rtw89_debugfs_add_rw(mlo_mode);
}

void rtw89_debugfs_init(struct rtw89_dev *rtwdev)
{
	struct dentry *debugfs_topdir;

	rtwdev->debugfs = kmemdup(&rtw89_debugfs_templ,
				  sizeof(rtw89_debugfs_templ), GFP_KERNEL);
	if (!rtwdev->debugfs)
		return;

	debugfs_topdir = debugfs_create_dir("rtw89",
					    rtwdev->hw->wiphy->debugfsdir);

	rtw89_debugfs_add_sec0(rtwdev, debugfs_topdir);
	rtw89_debugfs_add_sec1(rtwdev, debugfs_topdir);
}

void rtw89_debugfs_deinit(struct rtw89_dev *rtwdev)
{
	kfree(rtwdev->debugfs);
}
#endif

#ifdef CONFIG_RTW89_DEBUGMSG
void rtw89_debug(struct rtw89_dev *rtwdev, enum rtw89_debug_mask mask,
		 const char *fmt, ...)
{
	struct va_format vaf = {
	.fmt = fmt,
	};

	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (rtw89_debug_mask & mask)
		dev_printk(KERN_DEBUG, rtwdev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(rtw89_debug);
#endif
