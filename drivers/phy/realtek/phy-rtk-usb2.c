// SPDX-License-Identifier: GPL-2.0
/*
 *  phy-rtk-usb2.c RTK usb2.0 PHY driver
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/nvmem-consumer.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/usb.h>

/* GUSB2PHYACCn register */
#define PHY_NEW_REG_REQ BIT(25)
#define PHY_VSTS_BUSY   BIT(23)
#define PHY_VCTRL_SHIFT 8
#define PHY_REG_DATA_MASK 0xff

#define GET_LOW_NIBBLE(addr) ((addr) & 0x0f)
#define GET_HIGH_NIBBLE(addr) (((addr) & 0xf0) >> 4)

#define EFUS_USB_DC_CAL_RATE 2
#define EFUS_USB_DC_CAL_MAX 7

#define EFUS_USB_DC_DIS_RATE 1
#define EFUS_USB_DC_DIS_MAX 7

#define MAX_PHY_DATA_SIZE 20
#define OFFEST_PHY_READ 0x20

#define MAX_USB_PHY_NUM 4
#define MAX_USB_PHY_PAGE0_DATA_SIZE 16
#define MAX_USB_PHY_PAGE1_DATA_SIZE 16
#define MAX_USB_PHY_PAGE2_DATA_SIZE 8

#define SET_PAGE_OFFSET 0xf4
#define SET_PAGE_0 0x9b
#define SET_PAGE_1 0xbb
#define SET_PAGE_2 0xdb

#define PAGE_START 0xe0
#define PAGE0_0XE4 0xe4
#define PAGE0_0XE6 0xe6
#define PAGE0_0XE7 0xe7
#define PAGE1_0XE0 0xe0
#define PAGE1_0XE2 0xe2

#define SENSITIVITY_CTRL (BIT(4) | BIT(5) | BIT(6))
#define ENABLE_AUTO_SENSITIVITY_CALIBRATION BIT(2)
#define DEFAULT_DC_DRIVING_VALUE (0x8)
#define DEFAULT_DC_DISCONNECTION_VALUE (0x6)
#define HS_CLK_SELECT BIT(6)

struct phy_reg {
	void __iomem *reg_wrap_vstatus;
	void __iomem *reg_gusb2phyacc0;
	int vstatus_index;
};

struct phy_data {
	u8 addr;
	u8 data;
};

struct phy_cfg {
	int page0_size;
	struct phy_data page0[MAX_USB_PHY_PAGE0_DATA_SIZE];
	int page1_size;
	struct phy_data page1[MAX_USB_PHY_PAGE1_DATA_SIZE];
	int page2_size;
	struct phy_data page2[MAX_USB_PHY_PAGE2_DATA_SIZE];

	int num_phy;

	bool check_efuse;
	int check_efuse_version;
#define CHECK_EFUSE_V1 1
#define CHECK_EFUSE_V2 2
	int efuse_dc_driving_rate;
	int efuse_dc_disconnect_rate;
	int dc_driving_mask;
	int dc_disconnect_mask;
	bool usb_dc_disconnect_at_page0;
	int driving_updated_for_dev_dis;

	bool do_toggle;
	bool do_toggle_driving;
	bool use_default_parameter;
	bool is_double_sensitivity_mode;
};

struct phy_parameter {
	struct phy_reg phy_reg;

	/* Get from efuse */
	s8 efuse_usb_dc_cal;
	s8 efuse_usb_dc_dis;

	/* Get from dts */
	bool inverse_hstx_sync_clock;
	u32 driving_level;
	s32 driving_level_compensate;
	s32 disconnection_compensate;
};

struct rtk_phy {
	struct device *dev;

	struct phy_cfg *phy_cfg;
	int num_phy;
	struct phy_parameter *phy_parameter;

	struct dentry *debug_dir;
};

/* mapping 0xE0 to 0 ... 0xE7 to 7, 0xF0 to 8 ,,, 0xF7 to 15 */
static inline int page_addr_to_array_index(u8 addr)
{
	return (int)((((addr) - PAGE_START) & 0x7) +
		((((addr) - PAGE_START) & 0x10) >> 1));
}

static inline u8 array_index_to_page_addr(int index)
{
	return ((((index) + PAGE_START) & 0x7) +
		((((index) & 0x8) << 1) + PAGE_START));
}

#define PHY_IO_TIMEOUT_USEC		(50000)
#define PHY_IO_DELAY_US			(100)

static inline int utmi_wait_register(void __iomem *reg, u32 mask, u32 result)
{
	int ret;
	unsigned int val;

	ret = read_poll_timeout(readl, val, ((val & mask) == result),
				PHY_IO_DELAY_US, PHY_IO_TIMEOUT_USEC, false, reg);
	if (ret) {
		pr_err("%s can't program USB phy\n", __func__);
		return -ETIMEDOUT;
	}

	return 0;
}

static char rtk_phy_read(struct phy_reg *phy_reg, char addr)
{
	void __iomem *reg_gusb2phyacc0 = phy_reg->reg_gusb2phyacc0;
	unsigned int val;
	int ret = 0;

	addr -= OFFEST_PHY_READ;

	/* polling until VBusy == 0 */
	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return (char)ret;

	/* VCtrl = low nibble of addr, and set PHY_NEW_REG_REQ */
	val = PHY_NEW_REG_REQ | (GET_LOW_NIBBLE(addr) << PHY_VCTRL_SHIFT);
	writel(val, reg_gusb2phyacc0);
	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return (char)ret;

	/* VCtrl = high nibble of addr, and set PHY_NEW_REG_REQ */
	val = PHY_NEW_REG_REQ | (GET_HIGH_NIBBLE(addr) << PHY_VCTRL_SHIFT);
	writel(val, reg_gusb2phyacc0);
	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return (char)ret;

	val = readl(reg_gusb2phyacc0);

	return (char)(val & PHY_REG_DATA_MASK);
}

static int rtk_phy_write(struct phy_reg *phy_reg, char addr, char data)
{
	unsigned int val;
	void __iomem *reg_wrap_vstatus = phy_reg->reg_wrap_vstatus;
	void __iomem *reg_gusb2phyacc0 = phy_reg->reg_gusb2phyacc0;
	int shift_bits = phy_reg->vstatus_index * 8;
	int ret = 0;

	/* write data to VStatusOut2 (data output to phy) */
	writel((u32)data << shift_bits, reg_wrap_vstatus);

	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return ret;

	/* VCtrl = low nibble of addr, set PHY_NEW_REG_REQ */
	val = PHY_NEW_REG_REQ | (GET_LOW_NIBBLE(addr) << PHY_VCTRL_SHIFT);

	writel(val, reg_gusb2phyacc0);
	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return ret;

	/* VCtrl = high nibble of addr, set PHY_NEW_REG_REQ */
	val = PHY_NEW_REG_REQ | (GET_HIGH_NIBBLE(addr) << PHY_VCTRL_SHIFT);

	writel(val, reg_gusb2phyacc0);
	ret = utmi_wait_register(reg_gusb2phyacc0, PHY_VSTS_BUSY, 0);
	if (ret)
		return ret;

	return 0;
}

static int rtk_phy_set_page(struct phy_reg *phy_reg, int page)
{
	switch (page) {
	case 0:
		return rtk_phy_write(phy_reg, SET_PAGE_OFFSET, SET_PAGE_0);
	case 1:
		return rtk_phy_write(phy_reg, SET_PAGE_OFFSET, SET_PAGE_1);
	case 2:
		return rtk_phy_write(phy_reg, SET_PAGE_OFFSET, SET_PAGE_2);
	default:
		pr_err("%s error page=%d\n", __func__, page);
	}

	return -EINVAL;
}

static u8 __updated_dc_disconnect_level_page0_0xe4(struct phy_cfg *phy_cfg,
						   struct phy_parameter *phy_parameter, u8 data)
{
	u8 ret;
	s32 val;
	s32 dc_disconnect_mask = phy_cfg->dc_disconnect_mask;
	int offset = 4;

	val = (s32)((data >> offset) & dc_disconnect_mask)
		     + phy_parameter->efuse_usb_dc_dis
		     + phy_parameter->disconnection_compensate;

	if (val > dc_disconnect_mask)
		val = dc_disconnect_mask;
	else if (val < 0)
		val = 0;

	ret = (data & (~(dc_disconnect_mask << offset))) |
		    (val & dc_disconnect_mask) << offset;

	return ret;
}

/* updated disconnect level at page0 */
static void update_dc_disconnect_level_at_page0(struct rtk_phy *rtk_phy,
						struct phy_parameter *phy_parameter, bool update)
{
	struct phy_cfg *phy_cfg;
	struct phy_reg *phy_reg;
	struct phy_data *phy_data_page;
	struct phy_data *phy_data;
	u8 addr, data;
	int offset = 4;
	s32 dc_disconnect_mask;
	int i;

	phy_cfg = rtk_phy->phy_cfg;
	phy_reg = &phy_parameter->phy_reg;

	/* Set page 0 */
	phy_data_page = phy_cfg->page0;
	rtk_phy_set_page(phy_reg, 0);

	i = page_addr_to_array_index(PAGE0_0XE4);
	phy_data = phy_data_page + i;
	if (!phy_data->addr) {
		phy_data->addr = PAGE0_0XE4;
		phy_data->data = rtk_phy_read(phy_reg, PAGE0_0XE4);
	}

	addr = phy_data->addr;
	data = phy_data->data;
	dc_disconnect_mask = phy_cfg->dc_disconnect_mask;

	if (update)
		data = __updated_dc_disconnect_level_page0_0xe4(phy_cfg, phy_parameter, data);
	else
		data = (data & ~(dc_disconnect_mask << offset)) |
			(DEFAULT_DC_DISCONNECTION_VALUE << offset);

	if (rtk_phy_write(phy_reg, addr, data))
		dev_err(rtk_phy->dev,
			"%s: Error to set page1 parameter addr=0x%x value=0x%x\n",
			__func__, addr, data);
}

static u8 __updated_dc_disconnect_level_page1_0xe2(struct phy_cfg *phy_cfg,
						   struct phy_parameter *phy_parameter, u8 data)
{
	u8 ret;
	s32 val;
	s32 dc_disconnect_mask = phy_cfg->dc_disconnect_mask;

	if (phy_cfg->check_efuse_version == CHECK_EFUSE_V1) {
		val = (s32)(data & dc_disconnect_mask)
			    + phy_parameter->efuse_usb_dc_dis
			    + phy_parameter->disconnection_compensate;
	} else { /* for CHECK_EFUSE_V2 or no efuse */
		if (phy_parameter->efuse_usb_dc_dis)
			val = (s32)(phy_parameter->efuse_usb_dc_dis +
				    phy_parameter->disconnection_compensate);
		else
			val = (s32)((data & dc_disconnect_mask) +
				    phy_parameter->disconnection_compensate);
	}

	if (val > dc_disconnect_mask)
		val = dc_disconnect_mask;
	else if (val < 0)
		val = 0;

	ret = (data & (~dc_disconnect_mask)) | (val & dc_disconnect_mask);

	return ret;
}

/* updated disconnect level at page1 */
static void update_dc_disconnect_level_at_page1(struct rtk_phy *rtk_phy,
						struct phy_parameter *phy_parameter, bool update)
{
	struct phy_cfg *phy_cfg;
	struct phy_data *phy_data_page;
	struct phy_data *phy_data;
	struct phy_reg *phy_reg;
	u8 addr, data;
	s32 dc_disconnect_mask;
	int i;

	phy_cfg = rtk_phy->phy_cfg;
	phy_reg = &phy_parameter->phy_reg;

	/* Set page 1 */
	phy_data_page = phy_cfg->page1;
	rtk_phy_set_page(phy_reg, 1);

	i = page_addr_to_array_index(PAGE1_0XE2);
	phy_data = phy_data_page + i;
	if (!phy_data->addr) {
		phy_data->addr = PAGE1_0XE2;
		phy_data->data = rtk_phy_read(phy_reg, PAGE1_0XE2);
	}

	addr = phy_data->addr;
	data = phy_data->data;
	dc_disconnect_mask = phy_cfg->dc_disconnect_mask;

	if (update)
		data = __updated_dc_disconnect_level_page1_0xe2(phy_cfg, phy_parameter, data);
	else
		data = (data & ~dc_disconnect_mask) | DEFAULT_DC_DISCONNECTION_VALUE;

	if (rtk_phy_write(phy_reg, addr, data))
		dev_err(rtk_phy->dev,
			"%s: Error to set page1 parameter addr=0x%x value=0x%x\n",
			__func__, addr, data);
}

static void update_dc_disconnect_level(struct rtk_phy *rtk_phy,
				       struct phy_parameter *phy_parameter, bool update)
{
	struct phy_cfg *phy_cfg = rtk_phy->phy_cfg;

	if (phy_cfg->usb_dc_disconnect_at_page0)
		update_dc_disconnect_level_at_page0(rtk_phy, phy_parameter, update);
	else
		update_dc_disconnect_level_at_page1(rtk_phy, phy_parameter, update);
}

static u8 __update_dc_driving_page0_0xe4(struct phy_cfg *phy_cfg,
					 struct phy_parameter *phy_parameter, u8 data)
{
	s32 driving_level_compensate = phy_parameter->driving_level_compensate;
	s32 dc_driving_mask = phy_cfg->dc_driving_mask;
	s32 val;
	u8 ret;

	if (phy_cfg->check_efuse_version == CHECK_EFUSE_V1) {
		val = (s32)(data & dc_driving_mask) + driving_level_compensate
			    + phy_parameter->efuse_usb_dc_cal;
	} else { /* for CHECK_EFUSE_V2 or no efuse */
		if (phy_parameter->efuse_usb_dc_cal)
			val = (s32)((phy_parameter->efuse_usb_dc_cal & dc_driving_mask)
				    + driving_level_compensate);
		else
			val = (s32)(data & dc_driving_mask);
	}

	if (val > dc_driving_mask)
		val = dc_driving_mask;
	else if (val < 0)
		val = 0;

	ret = (data & (~dc_driving_mask)) | (val & dc_driving_mask);

	return ret;
}

static void update_dc_driving_level(struct rtk_phy *rtk_phy,
				    struct phy_parameter *phy_parameter)
{
	struct phy_cfg *phy_cfg;
	struct phy_reg *phy_reg;

	phy_reg = &phy_parameter->phy_reg;
	phy_cfg = rtk_phy->phy_cfg;
	if (!phy_cfg->page0[4].addr) {
		rtk_phy_set_page(phy_reg, 0);
		phy_cfg->page0[4].addr = PAGE0_0XE4;
		phy_cfg->page0[4].data = rtk_phy_read(phy_reg, PAGE0_0XE4);
	}

	if (phy_parameter->driving_level != DEFAULT_DC_DRIVING_VALUE) {
		u32 dc_driving_mask;
		u8 driving_level;
		u8 data;

		data = phy_cfg->page0[4].data;
		dc_driving_mask = phy_cfg->dc_driving_mask;
		driving_level = data & dc_driving_mask;

		dev_dbg(rtk_phy->dev, "%s driving_level=%d => dts driving_level=%d\n",
			__func__, driving_level, phy_parameter->driving_level);

		phy_cfg->page0[4].data = (data & (~dc_driving_mask)) |
			    (phy_parameter->driving_level & dc_driving_mask);
	}

	phy_cfg->page0[4].data = __update_dc_driving_page0_0xe4(phy_cfg,
								phy_parameter,
								phy_cfg->page0[4].data);
}

static void update_hs_clk_select(struct rtk_phy *rtk_phy,
				 struct phy_parameter *phy_parameter)
{
	struct phy_cfg *phy_cfg;
	struct phy_reg *phy_reg;

	phy_cfg = rtk_phy->phy_cfg;
	phy_reg = &phy_parameter->phy_reg;

	if (phy_parameter->inverse_hstx_sync_clock) {
		if (!phy_cfg->page0[6].addr) {
			rtk_phy_set_page(phy_reg, 0);
			phy_cfg->page0[6].addr = PAGE0_0XE6;
			phy_cfg->page0[6].data = rtk_phy_read(phy_reg, PAGE0_0XE6);
		}

		phy_cfg->page0[6].data = phy_cfg->page0[6].data | HS_CLK_SELECT;
	}
}

static void do_rtk_phy_toggle(struct rtk_phy *rtk_phy,
			      int index, bool connect)
{
	struct phy_parameter *phy_parameter;
	struct phy_cfg *phy_cfg;
	struct phy_reg *phy_reg;
	struct phy_data *phy_data_page;
	u8 addr, data;
	int i;

	phy_cfg = rtk_phy->phy_cfg;
	phy_parameter = &((struct phy_parameter *)rtk_phy->phy_parameter)[index];
	phy_reg = &phy_parameter->phy_reg;

	if (!phy_cfg->do_toggle)
		goto out;

	if (phy_cfg->is_double_sensitivity_mode)
		goto do_toggle_driving;

	/* Set page 0 */
	rtk_phy_set_page(phy_reg, 0);

	addr = PAGE0_0XE7;
	data = rtk_phy_read(phy_reg, addr);

	if (connect)
		rtk_phy_write(phy_reg, addr, data & (~SENSITIVITY_CTRL));
	else
		rtk_phy_write(phy_reg, addr, data | (SENSITIVITY_CTRL));

do_toggle_driving:

	if (!phy_cfg->do_toggle_driving)
		goto do_toggle;

	/* Page 0 addr 0xE4 driving capability */

	/* Set page 0 */
	phy_data_page = phy_cfg->page0;
	rtk_phy_set_page(phy_reg, 0);

	i = page_addr_to_array_index(PAGE0_0XE4);
	addr = phy_data_page[i].addr;
	data = phy_data_page[i].data;

	if (connect) {
		rtk_phy_write(phy_reg, addr, data);
	} else {
		u8 value;
		s32 tmp;
		s32 driving_updated =
			    phy_cfg->driving_updated_for_dev_dis;
		s32 dc_driving_mask = phy_cfg->dc_driving_mask;

		tmp = (s32)(data & dc_driving_mask) + driving_updated;

		if (tmp > dc_driving_mask)
			tmp = dc_driving_mask;
		else if (tmp < 0)
			tmp = 0;

		value = (data & (~dc_driving_mask)) | (tmp & dc_driving_mask);

		rtk_phy_write(phy_reg, addr, value);
	}

do_toggle:
	/* restore dc disconnect level before toggle */
	update_dc_disconnect_level(rtk_phy, phy_parameter, false);

	/* Set page 1 */
	rtk_phy_set_page(phy_reg, 1);

	addr = PAGE1_0XE0;
	data = rtk_phy_read(phy_reg, addr);

	rtk_phy_write(phy_reg, addr, data &
		      (~ENABLE_AUTO_SENSITIVITY_CALIBRATION));
	mdelay(1);
	rtk_phy_write(phy_reg, addr, data |
		      (ENABLE_AUTO_SENSITIVITY_CALIBRATION));

	/* update dc disconnect level after toggle */
	update_dc_disconnect_level(rtk_phy, phy_parameter, true);

out:
	return;
}

static int do_rtk_phy_init(struct rtk_phy *rtk_phy, int index)
{
	struct phy_parameter *phy_parameter;
	struct phy_cfg *phy_cfg;
	struct phy_data *phy_data_page;
	struct phy_reg *phy_reg;
	int i;

	phy_cfg = rtk_phy->phy_cfg;
	phy_parameter = &((struct phy_parameter *)rtk_phy->phy_parameter)[index];
	phy_reg = &phy_parameter->phy_reg;

	if (phy_cfg->use_default_parameter) {
		dev_dbg(rtk_phy->dev, "%s phy#%d use default parameter\n",
			__func__, index);
		goto do_toggle;
	}

	/* Set page 0 */
	phy_data_page = phy_cfg->page0;
	rtk_phy_set_page(phy_reg, 0);

	for (i = 0; i < phy_cfg->page0_size; i++) {
		struct phy_data *phy_data = phy_data_page + i;
		u8 addr = phy_data->addr;
		u8 data = phy_data->data;

		if (!addr)
			continue;

		if (rtk_phy_write(phy_reg, addr, data)) {
			dev_err(rtk_phy->dev,
				"%s: Error to set page0 parameter addr=0x%x value=0x%x\n",
				__func__, addr, data);
			return -EINVAL;
		}
	}

	/* Set page 1 */
	phy_data_page = phy_cfg->page1;
	rtk_phy_set_page(phy_reg, 1);

	for (i = 0; i < phy_cfg->page1_size; i++) {
		struct phy_data *phy_data = phy_data_page + i;
		u8 addr = phy_data->addr;
		u8 data = phy_data->data;

		if (!addr)
			continue;

		if (rtk_phy_write(phy_reg, addr, data)) {
			dev_err(rtk_phy->dev,
				"%s: Error to set page1 parameter addr=0x%x value=0x%x\n",
				__func__, addr, data);
			return -EINVAL;
		}
	}

	if (phy_cfg->page2_size == 0)
		goto do_toggle;

	/* Set page 2 */
	phy_data_page = phy_cfg->page2;
	rtk_phy_set_page(phy_reg, 2);

	for (i = 0; i < phy_cfg->page2_size; i++) {
		struct phy_data *phy_data = phy_data_page + i;
		u8 addr = phy_data->addr;
		u8 data = phy_data->data;

		if (!addr)
			continue;

		if (rtk_phy_write(phy_reg, addr, data)) {
			dev_err(rtk_phy->dev,
				"%s: Error to set page2 parameter addr=0x%x value=0x%x\n",
				__func__, addr, data);
			return -EINVAL;
		}
	}

do_toggle:
	do_rtk_phy_toggle(rtk_phy, index, false);

	return 0;
}

static int rtk_phy_init(struct phy *phy)
{
	struct rtk_phy *rtk_phy = phy_get_drvdata(phy);
	unsigned long phy_init_time = jiffies;
	int i, ret = 0;

	if (!rtk_phy)
		return -EINVAL;

	for (i = 0; i < rtk_phy->num_phy; i++)
		ret = do_rtk_phy_init(rtk_phy, i);

	dev_dbg(rtk_phy->dev, "Initialized RTK USB 2.0 PHY (take %dms)\n",
		jiffies_to_msecs(jiffies - phy_init_time));
	return ret;
}

static int rtk_phy_exit(struct phy *phy)
{
	return 0;
}

static void rtk_phy_toggle(struct rtk_phy *rtk_phy, bool connect, int port)
{
	int index = port;

	if (index > rtk_phy->num_phy) {
		dev_err(rtk_phy->dev, "%s: The port=%d is not in usb phy (num_phy=%d)\n",
			__func__, index, rtk_phy->num_phy);
		return;
	}

	do_rtk_phy_toggle(rtk_phy, index, connect);
}

static int rtk_phy_connect(struct phy *phy, int port)
{
	struct rtk_phy *rtk_phy = phy_get_drvdata(phy);

	dev_dbg(rtk_phy->dev, "%s port=%d\n", __func__, port);
	rtk_phy_toggle(rtk_phy, true, port);

	return 0;
}

static int rtk_phy_disconnect(struct phy *phy, int port)
{
	struct rtk_phy *rtk_phy = phy_get_drvdata(phy);

	dev_dbg(rtk_phy->dev, "%s port=%d\n", __func__, port);
	rtk_phy_toggle(rtk_phy, false, port);

	return 0;
}

static const struct phy_ops ops = {
	.init		= rtk_phy_init,
	.exit		= rtk_phy_exit,
	.connect	= rtk_phy_connect,
	.disconnect	= rtk_phy_disconnect,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_DEBUG_FS
static struct dentry *create_phy_debug_root(void)
{
	struct dentry *phy_debug_root;

	phy_debug_root = debugfs_lookup("phy", usb_debug_root);
	if (!phy_debug_root)
		phy_debug_root = debugfs_create_dir("phy", usb_debug_root);

	return phy_debug_root;
}

static int rtk_usb2_parameter_show(struct seq_file *s, void *unused)
{
	struct rtk_phy *rtk_phy = s->private;
	struct phy_cfg *phy_cfg;
	int i, index;

	phy_cfg = rtk_phy->phy_cfg;

	seq_puts(s, "Property:\n");
	seq_printf(s, "  check_efuse: %s\n",
		   phy_cfg->check_efuse ? "Enable" : "Disable");
	seq_printf(s, "  check_efuse_version: %d\n",
		   phy_cfg->check_efuse_version);
	seq_printf(s, "  efuse_dc_driving_rate: %d\n",
		   phy_cfg->efuse_dc_driving_rate);
	seq_printf(s, "  dc_driving_mask: 0x%x\n",
		   phy_cfg->dc_driving_mask);
	seq_printf(s, "  efuse_dc_disconnect_rate: %d\n",
		   phy_cfg->efuse_dc_disconnect_rate);
	seq_printf(s, "  dc_disconnect_mask: 0x%x\n",
		   phy_cfg->dc_disconnect_mask);
	seq_printf(s, "  usb_dc_disconnect_at_page0: %s\n",
		   phy_cfg->usb_dc_disconnect_at_page0 ? "true" : "false");
	seq_printf(s, "  do_toggle: %s\n",
		   phy_cfg->do_toggle ? "Enable" : "Disable");
	seq_printf(s, "  do_toggle_driving: %s\n",
		   phy_cfg->do_toggle_driving ? "Enable" : "Disable");
	seq_printf(s, "  driving_updated_for_dev_dis: 0x%x\n",
		   phy_cfg->driving_updated_for_dev_dis);
	seq_printf(s, "  use_default_parameter: %s\n",
		   phy_cfg->use_default_parameter ? "Enable" : "Disable");
	seq_printf(s, "  is_double_sensitivity_mode: %s\n",
		   phy_cfg->is_double_sensitivity_mode ? "Enable" : "Disable");

	for (index = 0; index < rtk_phy->num_phy; index++) {
		struct phy_parameter *phy_parameter;
		struct phy_reg *phy_reg;
		struct phy_data *phy_data_page;

		phy_parameter =  &((struct phy_parameter *)rtk_phy->phy_parameter)[index];
		phy_reg = &phy_parameter->phy_reg;

		seq_printf(s, "PHY %d:\n", index);

		seq_puts(s, "Page 0:\n");
		/* Set page 0 */
		phy_data_page = phy_cfg->page0;
		rtk_phy_set_page(phy_reg, 0);

		for (i = 0; i < phy_cfg->page0_size; i++) {
			struct phy_data *phy_data = phy_data_page + i;
			u8 addr = array_index_to_page_addr(i);
			u8 data = phy_data->data;
			u8 value = rtk_phy_read(phy_reg, addr);

			if (phy_data->addr)
				seq_printf(s, "  Page 0: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					   addr, data, value);
			else
				seq_printf(s, "  Page 0: addr=0x%x data=none ==> read value=0x%02x\n",
					   addr, value);
		}

		seq_puts(s, "Page 1:\n");
		/* Set page 1 */
		phy_data_page = phy_cfg->page1;
		rtk_phy_set_page(phy_reg, 1);

		for (i = 0; i < phy_cfg->page1_size; i++) {
			struct phy_data *phy_data = phy_data_page + i;
			u8 addr = array_index_to_page_addr(i);
			u8 data = phy_data->data;
			u8 value = rtk_phy_read(phy_reg, addr);

			if (phy_data->addr)
				seq_printf(s, "  Page 1: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					   addr, data, value);
			else
				seq_printf(s, "  Page 1: addr=0x%x data=none ==> read value=0x%02x\n",
					   addr, value);
		}

		if (phy_cfg->page2_size == 0)
			goto out;

		seq_puts(s, "Page 2:\n");
		/* Set page 2 */
		phy_data_page = phy_cfg->page2;
		rtk_phy_set_page(phy_reg, 2);

		for (i = 0; i < phy_cfg->page2_size; i++) {
			struct phy_data *phy_data = phy_data_page + i;
			u8 addr = array_index_to_page_addr(i);
			u8 data = phy_data->data;
			u8 value = rtk_phy_read(phy_reg, addr);

			if (phy_data->addr)
				seq_printf(s, "  Page 2: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					   addr, data, value);
			else
				seq_printf(s, "  Page 2: addr=0x%x data=none ==> read value=0x%02x\n",
					   addr, value);
		}

out:
		seq_puts(s, "PHY Property:\n");
		seq_printf(s, "  efuse_usb_dc_cal: %d\n",
			   (int)phy_parameter->efuse_usb_dc_cal);
		seq_printf(s, "  efuse_usb_dc_dis: %d\n",
			   (int)phy_parameter->efuse_usb_dc_dis);
		seq_printf(s, "  inverse_hstx_sync_clock: %s\n",
			   phy_parameter->inverse_hstx_sync_clock ? "Enable" : "Disable");
		seq_printf(s, "  driving_level: %d\n",
			   phy_parameter->driving_level);
		seq_printf(s, "  driving_level_compensate: %d\n",
			   phy_parameter->driving_level_compensate);
		seq_printf(s, "  disconnection_compensate: %d\n",
			   phy_parameter->disconnection_compensate);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rtk_usb2_parameter);

static inline void create_debug_files(struct rtk_phy *rtk_phy)
{
	struct dentry *phy_debug_root = NULL;

	phy_debug_root = create_phy_debug_root();
	if (!phy_debug_root)
		return;

	rtk_phy->debug_dir = debugfs_create_dir(dev_name(rtk_phy->dev),
						phy_debug_root);

	debugfs_create_file("parameter", 0444, rtk_phy->debug_dir, rtk_phy,
			    &rtk_usb2_parameter_fops);
}

static inline void remove_debug_files(struct rtk_phy *rtk_phy)
{
	debugfs_remove_recursive(rtk_phy->debug_dir);
}
#else
static inline void create_debug_files(struct rtk_phy *rtk_phy) { }
static inline void remove_debug_files(struct rtk_phy *rtk_phy) { }
#endif /* CONFIG_DEBUG_FS */

static int get_phy_data_by_efuse(struct rtk_phy *rtk_phy,
				 struct phy_parameter *phy_parameter, int index)
{
	struct phy_cfg *phy_cfg = rtk_phy->phy_cfg;
	u8 value = 0;
	struct nvmem_cell *cell;
	struct soc_device_attribute rtk_soc_groot[] = {
		    { .family = "Realtek Groot",},
		    { /* empty */ } };

	if (!phy_cfg->check_efuse)
		goto out;

	/* Read efuse for usb dc cal */
	cell = nvmem_cell_get(rtk_phy->dev, "usb-dc-cal");
	if (IS_ERR(cell)) {
		dev_dbg(rtk_phy->dev, "%s no usb-dc-cal: %ld\n",
			__func__, PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;

		buf = nvmem_cell_read(cell, &buf_size);
		if (!IS_ERR(buf)) {
			value = buf[0] & phy_cfg->dc_driving_mask;
			kfree(buf);
		}
		nvmem_cell_put(cell);
	}

	if (phy_cfg->check_efuse_version == CHECK_EFUSE_V1) {
		int rate = phy_cfg->efuse_dc_driving_rate;

		if (value <= EFUS_USB_DC_CAL_MAX)
			phy_parameter->efuse_usb_dc_cal = (int8_t)(value * rate);
		else
			phy_parameter->efuse_usb_dc_cal = -(int8_t)
				    ((EFUS_USB_DC_CAL_MAX & value) * rate);

		if (soc_device_match(rtk_soc_groot)) {
			dev_dbg(rtk_phy->dev, "For groot IC we need a workaround to adjust efuse_usb_dc_cal\n");

			/* We don't multiple dc_cal_rate=2 for positive dc cal compensate */
			if (value <= EFUS_USB_DC_CAL_MAX)
				phy_parameter->efuse_usb_dc_cal = (int8_t)(value);

			/* We set max dc cal compensate is 0x8 if otp is 0x7 */
			if (value == 0x7)
				phy_parameter->efuse_usb_dc_cal = (int8_t)(value + 1);
		}
	} else { /* for CHECK_EFUSE_V2 */
		phy_parameter->efuse_usb_dc_cal = value & phy_cfg->dc_driving_mask;
	}

	/* Read efuse for usb dc disconnect level */
	value = 0;
	cell = nvmem_cell_get(rtk_phy->dev, "usb-dc-dis");
	if (IS_ERR(cell)) {
		dev_dbg(rtk_phy->dev, "%s no usb-dc-dis: %ld\n",
			__func__, PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;

		buf = nvmem_cell_read(cell, &buf_size);
		if (!IS_ERR(buf)) {
			value = buf[0] & phy_cfg->dc_disconnect_mask;
			kfree(buf);
		}
		nvmem_cell_put(cell);
	}

	if (phy_cfg->check_efuse_version == CHECK_EFUSE_V1) {
		int rate = phy_cfg->efuse_dc_disconnect_rate;

		if (value <= EFUS_USB_DC_DIS_MAX)
			phy_parameter->efuse_usb_dc_dis = (int8_t)(value * rate);
		else
			phy_parameter->efuse_usb_dc_dis = -(int8_t)
				    ((EFUS_USB_DC_DIS_MAX & value) * rate);
	} else { /* for CHECK_EFUSE_V2 */
		phy_parameter->efuse_usb_dc_dis = value & phy_cfg->dc_disconnect_mask;
	}

out:
	return 0;
}

static int parse_phy_data(struct rtk_phy *rtk_phy)
{
	struct device *dev = rtk_phy->dev;
	struct device_node *np = dev->of_node;
	struct phy_parameter *phy_parameter;
	int ret = 0;
	int index;

	rtk_phy->phy_parameter = devm_kzalloc(dev, sizeof(struct phy_parameter) *
						rtk_phy->num_phy, GFP_KERNEL);
	if (!rtk_phy->phy_parameter)
		return -ENOMEM;

	for (index = 0; index < rtk_phy->num_phy; index++) {
		phy_parameter = &((struct phy_parameter *)rtk_phy->phy_parameter)[index];

		phy_parameter->phy_reg.reg_wrap_vstatus = of_iomap(np, 0);
		phy_parameter->phy_reg.reg_gusb2phyacc0 = of_iomap(np, 1) + index;
		phy_parameter->phy_reg.vstatus_index = index;

		if (of_property_read_bool(np, "realtek,inverse-hstx-sync-clock"))
			phy_parameter->inverse_hstx_sync_clock = true;
		else
			phy_parameter->inverse_hstx_sync_clock = false;

		if (of_property_read_u32_index(np, "realtek,driving-level",
					       index, &phy_parameter->driving_level))
			phy_parameter->driving_level = DEFAULT_DC_DRIVING_VALUE;

		if (of_property_read_u32_index(np, "realtek,driving-level-compensate",
					       index, &phy_parameter->driving_level_compensate))
			phy_parameter->driving_level_compensate = 0;

		if (of_property_read_u32_index(np, "realtek,disconnection-compensate",
					       index, &phy_parameter->disconnection_compensate))
			phy_parameter->disconnection_compensate = 0;

		get_phy_data_by_efuse(rtk_phy, phy_parameter, index);

		update_dc_driving_level(rtk_phy, phy_parameter);

		update_hs_clk_select(rtk_phy, phy_parameter);
	}

	return ret;
}

static int rtk_usb2phy_probe(struct platform_device *pdev)
{
	struct rtk_phy *rtk_phy;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	const struct phy_cfg *phy_cfg;
	int ret = 0;

	phy_cfg = of_device_get_match_data(dev);
	if (!phy_cfg) {
		dev_err(dev, "phy config are not assigned!\n");
		return -EINVAL;
	}

	rtk_phy = devm_kzalloc(dev, sizeof(*rtk_phy), GFP_KERNEL);
	if (!rtk_phy)
		return -ENOMEM;

	rtk_phy->dev			= &pdev->dev;
	rtk_phy->phy_cfg = devm_kzalloc(dev, sizeof(*phy_cfg), GFP_KERNEL);
	if (!rtk_phy->phy_cfg)
		return -ENOMEM;

	memcpy(rtk_phy->phy_cfg, phy_cfg, sizeof(*phy_cfg));

	rtk_phy->num_phy = phy_cfg->num_phy;

	ret = parse_phy_data(rtk_phy);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, rtk_phy);

	generic_phy = devm_phy_create(rtk_phy->dev, NULL, &ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, rtk_phy);

	phy_provider = devm_of_phy_provider_register(rtk_phy->dev,
						     of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	create_debug_files(rtk_phy);

err:
	return ret;
}

static void rtk_usb2phy_remove(struct platform_device *pdev)
{
	struct rtk_phy *rtk_phy = platform_get_drvdata(pdev);

	remove_debug_files(rtk_phy);
}

static const struct phy_cfg rtd1295_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0x90},
		   [3] = {0xe3, 0x3a},
		   [4] = {0xe4, 0x68},
		   [6] = {0xe6, 0x91},
		  [13] = {0xf5, 0x81},
		  [15] = {0xf7, 0x02}, },
	.page1_size = 8,
	.page1 = { /* default parameter */ },
	.page2_size = 0,
	.page2 = { /* no parameter */ },
	.num_phy = 1,
	.check_efuse = false,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = false,
};

static const struct phy_cfg rtd1395_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [4] = {0xe4, 0xac},
		  [13] = {0xf5, 0x00},
		  [15] = {0xf7, 0x02}, },
	.page1_size = 8,
	.page1 = { /* default parameter */ },
	.page2_size = 0,
	.page2 = { /* no parameter */ },
	.num_phy = 1,
	.check_efuse = false,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = false,
};

static const struct phy_cfg rtd1395_phy_cfg_2port = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [4] = {0xe4, 0xac},
		  [13] = {0xf5, 0x00},
		  [15] = {0xf7, 0x02}, },
	.page1_size = 8,
	.page1 = { /* default parameter */ },
	.page2_size = 0,
	.page2 = { /* no parameter */ },
	.num_phy = 2,
	.check_efuse = false,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = false,
};

static const struct phy_cfg rtd1619_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [4] = {0xe4, 0x68}, },
	.page1_size = 8,
	.page1 = { /* default parameter */ },
	.page2_size = 0,
	.page2 = { /* no parameter */ },
	.num_phy = 1,
	.check_efuse = true,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = false,
};

static const struct phy_cfg rtd1319_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0x18},
		   [4] = {0xe4, 0x6a},
		   [7] = {0xe7, 0x71},
		  [13] = {0xf5, 0x15},
		  [15] = {0xf7, 0x32}, },
	.page1_size = 8,
	.page1 = { [3] = {0xe3, 0x44}, },
	.page2_size = MAX_USB_PHY_PAGE2_DATA_SIZE,
	.page2 = { [0] = {0xe0, 0x01}, },
	.num_phy = 1,
	.check_efuse = true,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = true,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = true,
};

static const struct phy_cfg rtd1312c_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0x14},
		   [4] = {0xe4, 0x67},
		   [5] = {0xe5, 0x55}, },
	.page1_size = 8,
	.page1 = { [3] = {0xe3, 0x23},
		   [6] = {0xe6, 0x58}, },
	.page2_size = MAX_USB_PHY_PAGE2_DATA_SIZE,
	.page2 = { /* default parameter */ },
	.num_phy = 1,
	.check_efuse = true,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = 1,
	.dc_driving_mask = 0xf,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = true,
	.do_toggle = true,
	.do_toggle_driving = true,
	.driving_updated_for_dev_dis = 0xf,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = true,
};

static const struct phy_cfg rtd1619b_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0xa3},
		   [4] = {0xe4, 0xa8},
		   [5] = {0xe5, 0x4f},
		   [6] = {0xe6, 0x02}, },
	.page1_size = 8,
	.page1 = { [3] = {0xe3, 0x64}, },
	.page2_size = MAX_USB_PHY_PAGE2_DATA_SIZE,
	.page2 = { [7] = {0xe7, 0x45}, },
	.num_phy = 1,
	.check_efuse = true,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = EFUS_USB_DC_CAL_RATE,
	.dc_driving_mask = 0x1f,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = false,
	.do_toggle = true,
	.do_toggle_driving = true,
	.driving_updated_for_dev_dis = 0x8,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = true,
};

static const struct phy_cfg rtd1319d_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0xa3},
		   [4] = {0xe4, 0x8e},
		   [5] = {0xe5, 0x4f},
		   [6] = {0xe6, 0x02}, },
	.page1_size = MAX_USB_PHY_PAGE1_DATA_SIZE,
	.page1 = { [14] = {0xf5, 0x1}, },
	.page2_size = MAX_USB_PHY_PAGE2_DATA_SIZE,
	.page2 = { [7] = {0xe7, 0x44}, },
	.check_efuse = true,
	.num_phy = 1,
	.check_efuse_version = CHECK_EFUSE_V1,
	.efuse_dc_driving_rate = EFUS_USB_DC_CAL_RATE,
	.dc_driving_mask = 0x1f,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = false,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0x8,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = true,
};

static const struct phy_cfg rtd1315e_phy_cfg = {
	.page0_size = MAX_USB_PHY_PAGE0_DATA_SIZE,
	.page0 = { [0] = {0xe0, 0xa3},
		   [4] = {0xe4, 0x8c},
		   [5] = {0xe5, 0x4f},
		   [6] = {0xe6, 0x02}, },
	.page1_size = MAX_USB_PHY_PAGE1_DATA_SIZE,
	.page1 = { [3] = {0xe3, 0x7f},
		  [14] = {0xf5, 0x01}, },
	.page2_size = MAX_USB_PHY_PAGE2_DATA_SIZE,
	.page2 = { [7] = {0xe7, 0x44}, },
	.num_phy = 1,
	.check_efuse = true,
	.check_efuse_version = CHECK_EFUSE_V2,
	.efuse_dc_driving_rate = EFUS_USB_DC_CAL_RATE,
	.dc_driving_mask = 0x1f,
	.efuse_dc_disconnect_rate = EFUS_USB_DC_DIS_RATE,
	.dc_disconnect_mask = 0xf,
	.usb_dc_disconnect_at_page0 = false,
	.do_toggle = true,
	.do_toggle_driving = false,
	.driving_updated_for_dev_dis = 0x8,
	.use_default_parameter = false,
	.is_double_sensitivity_mode = true,
};

static const struct of_device_id usbphy_rtk_dt_match[] = {
	{ .compatible = "realtek,rtd1295-usb2phy", .data = &rtd1295_phy_cfg },
	{ .compatible = "realtek,rtd1312c-usb2phy", .data = &rtd1312c_phy_cfg },
	{ .compatible = "realtek,rtd1315e-usb2phy", .data = &rtd1315e_phy_cfg },
	{ .compatible = "realtek,rtd1319-usb2phy", .data = &rtd1319_phy_cfg },
	{ .compatible = "realtek,rtd1319d-usb2phy", .data = &rtd1319d_phy_cfg },
	{ .compatible = "realtek,rtd1395-usb2phy", .data = &rtd1395_phy_cfg },
	{ .compatible = "realtek,rtd1395-usb2phy-2port", .data = &rtd1395_phy_cfg_2port },
	{ .compatible = "realtek,rtd1619-usb2phy", .data = &rtd1619_phy_cfg },
	{ .compatible = "realtek,rtd1619b-usb2phy", .data = &rtd1619b_phy_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, usbphy_rtk_dt_match);

static struct platform_driver rtk_usb2phy_driver = {
	.probe		= rtk_usb2phy_probe,
	.remove_new	= rtk_usb2phy_remove,
	.driver		= {
		.name	= "rtk-usb2phy",
		.of_match_table = usbphy_rtk_dt_match,
	},
};

module_platform_driver(rtk_usb2phy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_DESCRIPTION("Realtek usb 2.0 phy driver");
