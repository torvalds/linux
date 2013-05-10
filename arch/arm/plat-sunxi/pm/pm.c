/*
 * arch/arm/plat-sunxi/pm/pm.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * chech usb to wake up system from standby
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <linux/power/aw_pm.h>


extern char *standby_bin_start;
extern char *standby_bin_end;

static struct aw_pm_info standby_info = {
	.pmu_arg = {
		.twi_port = 0,
		.dev_addr = 10,
	},
};

static int aw_pm_valid(suspend_state_t state)
{
	return (state == PM_SUSPEND_STANDBY) || (state == PM_SUSPEND_MEM);
}

static int aw_pm_enter(suspend_state_t state)
{
	int (*standby)(struct aw_pm_info *arg) = (int (*)(struct aw_pm_info *arg))SRAM_FUNC_START;
	int ret;

	/* move standby code to sram */
	memcpy((void *)SRAM_FUNC_START, (void *)&standby_bin_start, (int)&standby_bin_end - (int)&standby_bin_start);

	/* config system wakeup evetn type */
	standby_info.standby_para.event = SUSPEND_WAKEUP_SRC_EXINT | SUSPEND_WAKEUP_SRC_ALARM |
		SUSPEND_WAKEUP_SRC_KEY | SUSPEND_WAKEUP_SRC_IR | SUSPEND_WAKEUP_SRC_USB;

	/* goto sram and run */
	ret = standby(&standby_info);
	if (!ret)
		pr_info("%s: wakeup by event:%d\n", __func__, standby_info.standby_para.event);
	else
		pr_err("%s: suspend failed with error:%d\n", __func__, ret);

	return ret;
}

static struct platform_suspend_ops aw_pm_ops = {
	.valid = aw_pm_valid,
	.enter = aw_pm_enter,
};


static int __init aw_pm_init(void)
{
	suspend_set_ops(&aw_pm_ops);

	return 0;
}


static void __exit aw_pm_exit(void)
{
	suspend_set_ops(NULL);
}

module_init(aw_pm_init);
module_exit(aw_pm_exit);

