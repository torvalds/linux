/*
 *  drivers/leds/leds-rt8547.c
 *  Driver for Richtek RT8547 LED Flash IC
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif /* #ifdef CONFIG_OF */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#endif /* #ifdef CONFIG_DEBUG_FS */

#include "rtfled.h"
#include "leds-rt8547.h"

struct rt8547_chip {
	rt_fled_info_t base;
	struct device *dev;
	struct rt8547_platform_data *pdata;
	spinlock_t io_lock;
	unsigned char suspend:1;
	int in_use_mode;
	struct platform_device rt_fled_pdev;
#ifdef CONFIG_DEBUG_FS
	struct flashlight_device *fled_dev;
	unsigned char reg_addr;
	unsigned char reg_data;
#endif				/* #ifdef CONFIG_DEBUG_FS */
};

#ifdef CONFIG_DEBUG_FS
struct rt_debug_st {
	void *info;
	int id;
};

enum {
	RT8547_DBG_REG,
	RT8547_DBG_DATA,
	RT8547_DBG_REGS,
	RT8547_DBG_FLED,
	RT8547_DBG_MAX
};

static struct dentry *debugfs_rt_dent;
static struct dentry *debugfs_file[RT8547_DBG_MAX];
static struct rt_debug_st rtdbg_data[RT8547_DBG_MAX];
#endif /* #ifdef CONFIG_DEBUG_FS */

static unsigned char rt8547_reg_initval[] = {
	0x06,			/* REG 0x01 */
	0x12,			/* REG 0x02 */
	0x02,			/* REG 0x03 */
	0x0F,			/* REG 0x04 */
};

static inline int rt8547_send_bit(struct rt8547_platform_data *pdata,
				  unsigned char bit)
{
	if (bit) {
		gpio_set_value(pdata->flset_gpio, (~(pdata->flset_active) & 0x1));
		udelay(RT8547_SHORT_DELAY);
		gpio_set_value(pdata->flset_gpio, ((pdata->flset_active) & 0x1));
		udelay(RT8547_LONG_DELAY);
	} else {
		gpio_set_value(pdata->flset_gpio, (~(pdata->flset_active) & 0x1));
		udelay(RT8547_LONG_DELAY);
		gpio_set_value(pdata->flset_gpio, ((pdata->flset_active) & 0x1));
		udelay(RT8547_SHORT_DELAY);
	}
	return 0;
}

static inline int rt8547_send_byte(struct rt8547_platform_data *pdata,
				   unsigned char byte)
{
	int i;

	/*Send order is high bit to low bit */
	for (i = 7; i >= 0; i--)
		rt8547_send_bit(pdata, byte & (0x1 << i));
	return 0;
}

static inline int rt8547_send_special_byte(struct rt8547_platform_data *pdata,
					   unsigned char byte)
{
	int i;

	/*Only send three bit for register address */
	for (i = 2; i >= 0; i--)
		rt8547_send_bit(pdata, byte & (0x1 << i));
	return 0;
}

static inline int rt8547_start_xfer(struct rt8547_platform_data *pdata)
{
	gpio_set_value(pdata->flset_gpio, ((pdata->flset_active) & 0x1));
	udelay(RT8547_START_DELAY);
	return 0;
}

static inline int rt8547_stop_xfer(struct rt8547_platform_data *pdata)
{
	/*Redundant one bit as the stop condition */
	rt8547_send_bit(pdata, 1);
	return 0;
}

static int rt8547_send_data(struct rt8547_chip *chip, unsigned char reg,
			    unsigned char data)
{
	struct rt8547_platform_data *pdata = chip->pdata;
	unsigned long flags;
	unsigned char xfer_data[3];	/*0: adddr, 1: reg, 2: reg data*/

	xfer_data[0] = RT8547_ONEWIRE_ADDR;
	xfer_data[1] = reg;
	xfer_data[2] = data;
	RT_DBG("rt8547-> 0: 0x%02x, 1: 0x%02x, 2: 0x%02x\n", xfer_data[0],
	       xfer_data[1], xfer_data[2]);
	spin_lock_irqsave(&chip->io_lock, flags);
	rt8547_start_xfer(pdata);
	rt8547_send_byte(pdata, xfer_data[0]);
	rt8547_send_special_byte(pdata, xfer_data[1]);
	rt8547_send_byte(pdata, xfer_data[2]);
	rt8547_stop_xfer(pdata);
	spin_unlock_irqrestore(&chip->io_lock, flags);
	/*write back to reg array*/
	rt8547_reg_initval[reg - 1] = data;
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int reg_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static int get_parameters(char *buf, long int *param1, int num_of_par)
{
	char *token;
	int base, cnt;

	token = strsep(&buf, " ");

	for (cnt = 0; cnt < num_of_par; cnt++) {
		if (token != NULL) {
			if ((token[1] == 'x') || (token[1] == 'X'))
				base = 16;
			else
				base = 10;

			if (kstrtoul(token, base, &param1[cnt]) != 0)
				return -EINVAL;

			token = strsep(&buf, " ");
		} else
			return -EINVAL;
	}
	return 0;
}

static ssize_t reg_debug_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	struct rt_debug_st *st = filp->private_data;
	struct rt8547_chip *di = st->info;
	char lbuf[1000];
	int i = 0, j = 0;

	lbuf[0] = '\0';
	switch (st->id) {
	case RT8547_DBG_REG:
		snprintf(lbuf, sizeof(lbuf), "0x%x\n", di->reg_addr);
		break;
	case RT8547_DBG_DATA:
		di->reg_data = rt8547_reg_initval[di->reg_addr - 1];
		snprintf(lbuf, sizeof(lbuf), "0x%x\n", di->reg_data);
		break;
	case RT8547_DBG_REGS:
		for (i = RT8547_FLED_REG0; i < RT8547_FLED_REGMAX; i++)
			j += snprintf(lbuf + j, 20, "0x%02x:%02x\n", i,
				      rt8547_reg_initval[i - 1]);
		break;
	case RT8547_DBG_FLED:
		snprintf(lbuf, sizeof(lbuf), "%d\n", di->in_use_mode);
		break;
	default:
		return -EINVAL;

	}
	return simple_read_from_buffer(ubuf, count, ppos, lbuf, strlen(lbuf));
}

static ssize_t reg_debug_write(struct file *filp,
			       const char __user *ubuf, size_t cnt,
			       loff_t *ppos)
{
	struct rt_debug_st *st = filp->private_data;
	struct rt8547_chip *di = st->info;
	char lbuf[32];
	int rc;
	long int param[5];

	if (cnt > sizeof(lbuf) - 1)
		return -EINVAL;

	rc = copy_from_user(lbuf, ubuf, cnt);
	if (rc)
		return -EFAULT;

	lbuf[cnt] = '\0';

	switch (st->id) {
	case RT8547_DBG_REG:
		rc = get_parameters(lbuf, param, 1);
		if ((param[0] < RT8547_FLED_REGMAX) && (rc == 0)) {
			if ((param[0] >= RT8547_FLED_REG0
			     && param[0] <= RT8547_FLED_REG3))
				di->reg_addr = (unsigned char)param[0];
			else
				rc = -EINVAL;
		} else
			rc = -EINVAL;
		break;
	case RT8547_DBG_DATA:
		rc = get_parameters(lbuf, param, 1);
		if ((param[0] <= 0xff) && (rc == 0)) {
			rt8547_send_data(di, di->reg_addr,
					 (unsigned char)param[0]);
		} else
			rc = -EINVAL;
		break;
	case RT8547_DBG_FLED:
		if (!di->fled_dev)
			di->fled_dev = find_flashlight_by_name("rt-flash-led");
		rc = get_parameters(lbuf, param, 1);
		if ((param[0] <= FLASHLIGHT_MODE_FLASH) && (rc == 0)
		    && di->fled_dev) {
			switch (param[0]) {
			case FLASHLIGHT_MODE_TORCH:
				flashlight_set_torch_brightness(di->fled_dev,
								2);
				flashlight_set_mode(di->fled_dev,
						    FLASHLIGHT_MODE_TORCH);
				break;
			case FLASHLIGHT_MODE_FLASH:
				flashlight_set_strobe_timeout(di->fled_dev,
							      256, 256);
				flashlight_set_strobe_brightness(di->fled_dev,
								 18);
				flashlight_set_mode(di->fled_dev,
						    FLASHLIGHT_MODE_FLASH);
				flashlight_strobe(di->fled_dev);
				break;
			case FLASHLIGHT_MODE_OFF:
				flashlight_set_mode(di->fled_dev,
						    FLASHLIGHT_MODE_OFF);
				break;
			}
		} else
			rc = -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	if (rc == 0)
		rc = cnt;
	return rc;
}

static const struct file_operations reg_debug_ops = {
	.open = reg_debug_open,
	.write = reg_debug_write,
	.read = reg_debug_read
};

static void rt8547_create_debugfs(struct rt8547_chip *chip)
{
	RT_DBG("add debugfs for RT8547\n");
	debugfs_rt_dent = debugfs_create_dir("rt8547_dbg", 0);
	if (!IS_ERR(debugfs_rt_dent)) {
		rtdbg_data[0].info = chip;
		rtdbg_data[0].id = RT8547_DBG_REG;
		debugfs_file[0] = debugfs_create_file("reg",
						      S_IFREG | S_IRUGO,
						      debugfs_rt_dent,
						      (void *)&rtdbg_data[0],
						      &reg_debug_ops);

		rtdbg_data[1].info = chip;
		rtdbg_data[1].id = RT8547_DBG_DATA;
		debugfs_file[1] = debugfs_create_file("data",
						      S_IFREG | S_IRUGO,
						      debugfs_rt_dent,
						      (void *)&rtdbg_data[1],
						      &reg_debug_ops);

		rtdbg_data[2].info = chip;
		rtdbg_data[2].id = RT8547_DBG_REGS;
		debugfs_file[2] = debugfs_create_file("regs",
						      S_IFREG | S_IRUGO,
						      debugfs_rt_dent,
						      (void *)&rtdbg_data[2],
						      &reg_debug_ops);

		rtdbg_data[3].info = chip;
		rtdbg_data[3].id = RT8547_DBG_FLED;
		debugfs_file[3] = debugfs_create_file("fled",
						      S_IFREG | S_IRUGO,
						      debugfs_rt_dent,
						      (void *)&rtdbg_data[3],
						      &reg_debug_ops);
	} else {
		dev_err(chip->dev, "create debugfs failed\n");
	}
}

static void rt8547_remove_debugfs(void)
{
	if (!IS_ERR(debugfs_rt_dent))
		debugfs_remove_recursive(debugfs_rt_dent);
}
#endif /* #ifdef CONFIG_DEBUG_FS */

static inline void rt8547_fled_power_on(struct rt8547_platform_data *pdata)
{
    if (gpio_is_valid(pdata->flset_gpio))
    	gpio_set_value(pdata->flset_gpio, ((pdata->flset_active) & 0x1));
}

static inline void rt8547_fled_power_off(struct rt8547_platform_data *pdata)
{
    if (gpio_is_valid(pdata->flset_gpio))
    	gpio_set_value(pdata->flset_gpio, (~(pdata->flset_active) & 0x1));
	udelay(RT8547_STOP_DELAY);
}

static inline void rt8547_fled_ctrl_en(struct rt8547_platform_data *pdata,
				       int en)
{
    if (gpio_is_valid(pdata->ctl_gpio)){
    	if (en)
    		gpio_set_value(pdata->ctl_gpio, ((pdata->ctl_active) & 0x1));
    	else
    		gpio_set_value(pdata->ctl_gpio, (~(pdata->ctl_active) & 0x1));
	}
	RT_DBG("en %d\n", en);
}

static inline void rt8547_fled_flash_en(struct rt8547_platform_data *pdata,
					int en)
{
    if (gpio_is_valid(pdata->flen_gpio)){
    	if (en)
    		gpio_set_value(pdata->flen_gpio, ((pdata->flen_active) & 0x1));
    	else
    		gpio_set_value(pdata->flen_gpio, (~(pdata->flen_active) & 0x1));
	}
	RT_DBG("en %d\n", en);
}

static int rt8547_fled_init(struct rt_fled_info *info)
{
	RT_DBG("\n");
	return 0;
}

static int rt8547_fled_resume(struct rt_fled_info *info)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;

	RT_DBG("\n");
	fi->suspend = 0;
	return 0;
}

static int rt8547_fled_suspend(struct rt_fled_info *info, pm_message_t state)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;

	RT_DBG("\n");
	fi->suspend = 1;
	return 0;
}

static int rt8547_fled_set_mode(struct rt_fled_info *info,
				flashlight_mode_t mode)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;
	int ret = 0;

	RT_DBG("mode=%d\n", mode);
	switch (mode) {
	case FLASHLIGHT_MODE_TORCH:
		if (fi->in_use_mode == FLASHLIGHT_MODE_OFF)
			rt8547_fled_power_on(fi->pdata);
		tmp = rt8547_reg_initval[RT8547_FLED_REG2 - 1];
		tmp |= RT8547_MODESEL_MASK;
		rt8547_send_data(fi, RT8547_FLED_REG2, tmp);
		rt8547_fled_ctrl_en(fi->pdata, 1);
		rt8547_fled_flash_en(fi->pdata, 1);
		fi->in_use_mode = mode;
		break;
	case FLASHLIGHT_MODE_FLASH:
		if (fi->in_use_mode == FLASHLIGHT_MODE_OFF)
			rt8547_fled_power_on(fi->pdata);
		tmp = rt8547_reg_initval[RT8547_FLED_REG2 - 1];
		tmp &= ~RT8547_MODESEL_MASK;
		rt8547_send_data(fi, RT8547_FLED_REG2, tmp);
		fi->in_use_mode = mode;
		break;
	case FLASHLIGHT_MODE_OFF:
		rt8547_fled_flash_en(fi->pdata, 0);
		rt8547_fled_ctrl_en(fi->pdata, 0);
		if (fi->in_use_mode != FLASHLIGHT_MODE_OFF)
			rt8547_fled_power_off(fi->pdata);
		fi->in_use_mode = mode;
		break;
	case FLASHLIGHT_MODE_MIXED:
	default:
		ret = -EINVAL;
	}
	return 0;
}

static int rt8547_fled_get_mode(struct rt_fled_info *info)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;

	RT_DBG("\n");
	return fi->in_use_mode;
}

static int rt8547_fled_strobe(struct rt_fled_info *info)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;

	RT_DBG("\n");
	rt8547_fled_flash_en(fi->pdata, 0);
	rt8547_fled_ctrl_en(fi->pdata, 0);
	rt8547_fled_ctrl_en(fi->pdata, 1);
	rt8547_fled_flash_en(fi->pdata, 1);
	return 0;
}

static int rt8547_fled_torch_current_list(struct rt_fled_info *info,
					  int selector)
{
	RT_DBG("selector=%d\n", selector);
	return 25000 + selector * 25000;	/* unit: uA */
}

static int rt8547_fled_strobe_current_list(struct rt_fled_info *info,
					   int selector)
{
	RT_DBG("selector=%d\n", selector);
	return 100000 + selector * 50000;	/* unit: uA */
}

static int rt8547_fled_timeout_level_list(struct rt_fled_info *info,
					  int selector)
{
	RT_DBG("selector=%d\n", selector);
	return 100000 + selector * 50000;	/* unit: uA */
}

static int rt8547_fled_lv_protection_list(struct rt_fled_info *info,
					  int selector)
{
	RT_DBG("selector=%d\n", selector);
	return 3000 + selector * 100;	/* unit: mV */
}

static int rt8547_fled_strobe_timeout_list(struct rt_fled_info *info,
					   int selector)
{
	RT_DBG("selector=%d\n", selector);
	return 64 + selector * 32;	/* unit: mS */
}

static int rt8547_fled_set_torch_current_sel(struct rt_fled_info *info,
					     int selector)
{

	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;

	RT_DBG("selector=%d\n", selector);
	tmp = rt8547_reg_initval[RT8547_FLED_REG2 - 1];
	tmp &= ~RT8547_TCLEVEL_MASK;
	tmp |= selector;
	rt8547_send_data(fi, RT8547_FLED_REG2, tmp);
	return 0;
}

static int rt8547_fled_set_strobe_current_sel(struct rt_fled_info *info,
					      int selector)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;

	RT_DBG("selector=%d\n", selector);
	tmp = rt8547_reg_initval[RT8547_FLED_REG1 - 1];
	tmp &= ~RT8547_SCLEVEL_MASK;
	tmp |= selector;
	rt8547_send_data(fi, RT8547_FLED_REG1, tmp);
	return 0;
}

static int rt8547_fled_set_timeout_level_sel(struct rt_fled_info *info,
					     int selector)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;

	RT_DBG("selector=%d\n", selector);
	if (selector > RT8547_TOL_MAX)
		return -EINVAL;
	tmp = rt8547_reg_initval[RT8547_FLED_REG1 - 1];
	tmp &= ~RT8547_TOCLEVEL_MASK;
	tmp |= (selector << RT8547_TOCLEVEL_SHFT);
	rt8547_send_data(fi, RT8547_FLED_REG1, tmp);
	return 0;
}

static int rt8547_fled_set_lv_protection_sel(struct rt_fled_info *info,
					     int selector)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;

	RT_DBG("selector=%d\n", selector);
	if (selector > RT8547_LVP_MAX)
		return -EINVAL;
	tmp = rt8547_reg_initval[RT8547_FLED_REG0 - 1];
	tmp &= ~RT8547_LVP_MASK;
	tmp |= selector;
	rt8547_send_data(fi, RT8547_FLED_REG0, tmp);
	return 0;
}

static int rt8547_fled_set_strobe_timeout_sel(struct rt_fled_info *info,
					      int selector)
{
	struct rt8547_chip *fi = (struct rt8547_chip *)info;
	unsigned char tmp = 0;

	RT_DBG("selector=%d\n", selector);
	if (selector > RT8547_STO_MAX)
		return -EINVAL;
	tmp = rt8547_reg_initval[RT8547_FLED_REG3 - 1];
	tmp &= ~RT8547_STO_MASK;
	tmp |= selector;
	rt8547_send_data(fi, RT8547_FLED_REG3, tmp);
	return 0;
}

static int rt8547_fled_get_torch_current_sel(struct rt_fled_info *info)
{
	int selector =
	    rt8547_reg_initval[RT8547_FLED_REG2 - 1] & RT8547_TCLEVEL_MASK;

	return selector;
}

static int rt8547_fled_get_strobe_current_sel(struct rt_fled_info *info)
{
	int selector =
	    rt8547_reg_initval[RT8547_FLED_REG1 - 1] & RT8547_SCLEVEL_MASK;

	return selector;
}

static int rt8547_fled_get_timeout_level_sel(struct rt_fled_info *info)
{
	int selector =
	    rt8547_reg_initval[RT8547_FLED_REG1 - 1] & RT8547_TOCLEVEL_MASK;

	selector >>= RT8547_TOCLEVEL_SHFT;
	return selector;
}

static int rt8547_fled_get_lv_protection_sel(struct rt_fled_info *info)
{
	int selector =
	    rt8547_reg_initval[RT8547_FLED_REG0 - 1] & RT8547_LVP_MASK;

	return selector;
}

static int rt8547_fled_get_strobe_timeout_sel(struct rt_fled_info *info)
{
	int selector =
	    rt8547_reg_initval[RT8547_FLED_REG3 - 1] & RT8547_STO_MASK;

	return selector;
}

static struct rt_fled_hal rt8547_fled_hal = {
	.fled_init = rt8547_fled_init,
	.fled_suspend = rt8547_fled_suspend,
	.fled_resume = rt8547_fled_resume,
	.fled_set_mode = rt8547_fled_set_mode,
	.fled_get_mode = rt8547_fled_get_mode,
	.fled_strobe = rt8547_fled_strobe,
	.fled_torch_current_list = rt8547_fled_torch_current_list,
	.fled_strobe_current_list = rt8547_fled_strobe_current_list,
	.fled_timeout_level_list = rt8547_fled_timeout_level_list,
	.fled_lv_protection_list = rt8547_fled_lv_protection_list,
	.fled_strobe_timeout_list = rt8547_fled_strobe_timeout_list,
	/* method to set */
	.fled_set_torch_current_sel = rt8547_fled_set_torch_current_sel,
	.fled_set_strobe_current_sel = rt8547_fled_set_strobe_current_sel,
	.fled_set_timeout_level_sel = rt8547_fled_set_timeout_level_sel,
	.fled_set_lv_protection_sel = rt8547_fled_set_lv_protection_sel,
	.fled_set_strobe_timeout_sel = rt8547_fled_set_strobe_timeout_sel,
	/* method to get */
	.fled_get_torch_current_sel = rt8547_fled_get_torch_current_sel,
	.fled_get_strobe_current_sel = rt8547_fled_get_strobe_current_sel,
	.fled_get_timeout_level_sel = rt8547_fled_get_timeout_level_sel,
	.fled_get_lv_protection_sel = rt8547_fled_get_lv_protection_sel,
	.fled_get_strobe_timeout_sel = rt8547_fled_get_strobe_timeout_sel,
};

static struct flashlight_properties rt8547_fled_props = {
	.type = FLASHLIGHT_TYPE_LED,
	.torch_brightness = 2,
	.torch_max_brightness = 15,
	.strobe_brightness = 18,
	.strobe_max_brightness = 30,
	.strobe_delay = 2,
	.strobe_timeout = 544,
	.alias_name = "rt8547-fled",
};

static void rt8547_parse_dt(struct rt8547_platform_data *pdata,
			    struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	u32 tmp;

	if (of_property_read_u32(np, "rt,def_lvp", &tmp) < 0) {
		dev_warn(dev, "use 3V as the default lvp\n");
	} else {
		if (tmp > RT8547_LVP_MAX)
			tmp = RT8547_LVP_MAX;
		rt8547_reg_initval[RT8547_FLED_REG0 - 1] &= ~RT8547_LVP_MASK;
		rt8547_reg_initval[RT8547_FLED_REG0 - 1] |= tmp;
	}

	if (of_property_read_u32(np, "rt,def_tol", &tmp) < 0) {
		dev_warn(dev, "use 100mA as the default timeout level\n");
	} else {
		if (tmp > RT8547_TOL_MAX)
			tmp = RT8547_TOL_MAX;
		tmp <<= RT8547_TOCLEVEL_SHFT;
		rt8547_reg_initval[RT8547_FLED_REG1 - 1] &=
		    ~RT8547_TOCLEVEL_MASK;
		rt8547_reg_initval[RT8547_FLED_REG1 - 1] |= tmp;
	}
	pdata->flen_gpio = of_get_named_gpio(np, "rt,flen_gpio", 0);
	pdata->ctl_gpio = of_get_named_gpio(np, "rt,ctl_gpio", 0);
	pdata->flset_gpio = of_get_named_gpio(np, "rt,flset_gpio", 0);
#endif /* #ifdef CONFIG_OF */
}

static void rt8547_parse_pdata(struct rt8547_platform_data *pdata,
			       struct device *dev)
{
	u32 tmp;

	tmp = pdata->def_lvp;
	rt8547_reg_initval[RT8547_FLED_REG0 - 1] &= ~RT8547_LVP_MASK;
	rt8547_reg_initval[RT8547_FLED_REG0 - 1] |= tmp;

	tmp = pdata->def_tol;
	tmp <<= RT8547_TOCLEVEL_SHFT;
	rt8547_reg_initval[RT8547_FLED_REG1 - 1] &= ~RT8547_TOCLEVEL_MASK;
	rt8547_reg_initval[RT8547_FLED_REG1 - 1] |= tmp;
}

static int rt8547_io_init(struct rt8547_platform_data *pdata,
			  struct device *dev)
{
	int rc = 0;

	if (gpio_is_valid(pdata->flen_gpio)) {
		rc = gpio_request_one(pdata->flen_gpio, ((~(pdata->flen_active) & 0x1) ? GPIOF_OUT_INIT_HIGH:GPIOF_OUT_INIT_LOW),
				      "rt8547_flen");
		if (rc < 0) {
			dev_err(dev, "request rt8547 flash en pin fail\n");
			goto gpio_request1;
		}

	}

	if(gpio_is_valid(pdata->ctl_gpio)){
		rc = gpio_request_one(pdata->ctl_gpio, ((~(pdata->ctl_active) & 0x1) ? GPIOF_OUT_INIT_HIGH:GPIOF_OUT_INIT_LOW),
				      "rt8547_ctl");
		if (rc < 0) {
			dev_err(dev, "request rt8547 ctl pin fail\n");
			goto gpio_request2;
		}
	}

	if(gpio_is_valid(pdata->flset_gpio)){
		rc = gpio_request_one(pdata->flset_gpio, ((~(pdata->flset_active) & 0x1) ? GPIOF_OUT_INIT_HIGH:GPIOF_OUT_INIT_LOW),
				      "rt8547_flset");
		if (rc < 0) {
			dev_err(dev, "request rt8547 flash set pin fail\n");
			/*GPIO(gpio7 GPIO_B5) is EBUSY when register after factory data reset, but after power on ,it's  normal*/
			/*goto gpio_request3;*/
		}
	}
	return 0;
/*
gpio_request3:
    if(gpio_is_valid(pdata->ctl_gpio))
    	gpio_free(pdata->ctl_gpio);
*/
gpio_request2:
    if (gpio_is_valid(pdata->flen_gpio))
    	gpio_free(pdata->flen_gpio);
gpio_request1:
	return rc;

}

static int rt8547_io_deinit(struct rt8547_platform_data *pdata)
{
    if (gpio_is_valid(pdata->flen_gpio)){
    	gpio_direction_input(pdata->flen_gpio);
    	gpio_free(pdata->flen_gpio);
	}
    if(gpio_is_valid(pdata->ctl_gpio)){
    	gpio_direction_input(pdata->ctl_gpio);
    	gpio_free(pdata->ctl_gpio);
	}
	if(gpio_is_valid(pdata->flset_gpio)){
    	gpio_direction_input(pdata->flset_gpio);
    	gpio_free(pdata->flset_gpio);
	}
	return 0;
}

static void rt8547_reg_init(struct rt8547_chip *chip)
{
	RT_DBG("\n");
	rt8547_send_data(chip, RT8547_FLED_REG0,
			 rt8547_reg_initval[RT8547_FLED_REG0 - 1]);
	rt8547_send_data(chip, RT8547_FLED_REG1,
			 rt8547_reg_initval[RT8547_FLED_REG1 - 1]);
	rt8547_send_data(chip, RT8547_FLED_REG2,
			 rt8547_reg_initval[RT8547_FLED_REG2 - 1]);
	rt8547_send_data(chip, RT8547_FLED_REG3,
			 rt8547_reg_initval[RT8547_FLED_REG3 - 1]);
}

static void rt8547_release(struct device *dev)
{
}

static int rt8547_led_probe(struct platform_device *pdev)
{
	struct rt8547_platform_data *pdata = pdev->dev.platform_data;
	struct rt8547_chip *chip;
	bool use_dt = pdev->dev.of_node;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	if (use_dt) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			goto err_probe;
		rt8547_parse_dt(pdata, &pdev->dev);
	} else {
		if (!pdata)
			goto err_probe;
		rt8547_parse_pdata(pdata, &pdev->dev);
	}

	ret = rt8547_io_init(pdata, &pdev->dev);
	if (ret < 0)
		goto err_io;

	chip->dev = &pdev->dev;
	chip->pdata = pdata;
	spin_lock_init(&chip->io_lock);
	chip->in_use_mode = FLASHLIGHT_MODE_OFF;
	platform_set_drvdata(pdev, chip);

	rt8547_fled_power_on(pdata);
	rt8547_reg_init(chip);
	rt8547_fled_power_off(pdata);

	chip->base.hal = &rt8547_fled_hal;
	chip->base.init_props = &rt8547_fled_props;
	chip->rt_fled_pdev.dev.parent = &pdev->dev;
	chip->rt_fled_pdev.dev.release = rt8547_release;
	chip->rt_fled_pdev.name = "rt-flash-led";
	chip->rt_fled_pdev.id = -1;
	ret = platform_device_register(&chip->rt_fled_pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "register rtfled fail\n");
		goto err_io;
	}
#ifdef CONFIG_DEBUG_FS
	rt8547_create_debugfs(chip);
#endif /* #ifdef CONFIG_DEBUG_FS */
	dev_info(&pdev->dev, "driver successfully registered\n");
	return 0;
err_io:
	if (use_dt)
		devm_kfree(&pdev->dev, pdata);
err_probe:
	devm_kfree(&pdev->dev, chip);
	return ret;
}

static int rt8547_led_remove(struct platform_device *pdev)
{
	struct rt8547_chip *chip = platform_get_drvdata(pdev);

#ifdef CONFIG_DEBUG_FS
	rt8547_remove_debugfs();
#endif /* #ifdef CONFIG_DEBUG_FS */
	platform_device_unregister(&chip->rt_fled_pdev);
	rt8547_io_deinit(chip->pdata);
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt8547",},
	{},
};

static struct platform_driver rt8547_led_driver = {
	.driver = {
		   .name = "rt8547",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt8547_led_probe,
	.remove = rt8547_led_remove,
};

static int rt8547_led_init(void)
{
	return platform_driver_register(&rt8547_led_driver);
}

module_init(rt8547_led_init);

static void rt8547_led_exit(void)
{
	platform_driver_unregister(&rt8547_led_driver);
}

module_exit(rt8547_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("LED Flash Driver for RT8547");
MODULE_VERSION(RT8547_DRV_VER);
