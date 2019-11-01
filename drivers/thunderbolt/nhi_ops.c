// SPDX-License-Identifier: GPL-2.0
/*
 * NHI specific operations
 *
 * Copyright (C) 2019, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/delay.h>
#include <linux/suspend.h>

#include "nhi.h"
#include "nhi_regs.h"
#include "tb.h"

/* Ice Lake specific NHI operations */

#define ICL_LC_MAILBOX_TIMEOUT	500 /* ms */

static int check_for_device(struct device *dev, void *data)
{
	return tb_is_switch(dev);
}

static bool icl_nhi_is_device_connected(struct tb_nhi *nhi)
{
	struct tb *tb = pci_get_drvdata(nhi->pdev);
	int ret;

	ret = device_for_each_child(&tb->root_switch->dev, NULL,
				    check_for_device);
	return ret > 0;
}

static int icl_nhi_force_power(struct tb_nhi *nhi, bool power)
{
	u32 vs_cap;

	/*
	 * The Thunderbolt host controller is present always in Ice Lake
	 * but the firmware may not be loaded and running (depending
	 * whether there is device connected and so on). Each time the
	 * controller is used we need to "Force Power" it first and wait
	 * for the firmware to indicate it is up and running. This "Force
	 * Power" is really not about actually powering on/off the
	 * controller so it is accessible even if "Force Power" is off.
	 *
	 * The actual power management happens inside shared ACPI power
	 * resources using standard ACPI methods.
	 */
	pci_read_config_dword(nhi->pdev, VS_CAP_22, &vs_cap);
	if (power) {
		vs_cap &= ~VS_CAP_22_DMA_DELAY_MASK;
		vs_cap |= 0x22 << VS_CAP_22_DMA_DELAY_SHIFT;
		vs_cap |= VS_CAP_22_FORCE_POWER;
	} else {
		vs_cap &= ~VS_CAP_22_FORCE_POWER;
	}
	pci_write_config_dword(nhi->pdev, VS_CAP_22, vs_cap);

	if (power) {
		unsigned int retries = 10;
		u32 val;

		/* Wait until the firmware tells it is up and running */
		do {
			pci_read_config_dword(nhi->pdev, VS_CAP_9, &val);
			if (val & VS_CAP_9_FW_READY)
				return 0;
			msleep(250);
		} while (--retries);

		return -ETIMEDOUT;
	}

	return 0;
}

static void icl_nhi_lc_mailbox_cmd(struct tb_nhi *nhi, enum icl_lc_mailbox_cmd cmd)
{
	u32 data;

	data = (cmd << VS_CAP_19_CMD_SHIFT) & VS_CAP_19_CMD_MASK;
	pci_write_config_dword(nhi->pdev, VS_CAP_19, data | VS_CAP_19_VALID);
}

static int icl_nhi_lc_mailbox_cmd_complete(struct tb_nhi *nhi, int timeout)
{
	unsigned long end;
	u32 data;

	if (!timeout)
		goto clear;

	end = jiffies + msecs_to_jiffies(timeout);
	do {
		pci_read_config_dword(nhi->pdev, VS_CAP_18, &data);
		if (data & VS_CAP_18_DONE)
			goto clear;
		msleep(100);
	} while (time_before(jiffies, end));

	return -ETIMEDOUT;

clear:
	/* Clear the valid bit */
	pci_write_config_dword(nhi->pdev, VS_CAP_19, 0);
	return 0;
}

static void icl_nhi_set_ltr(struct tb_nhi *nhi)
{
	u32 max_ltr, ltr;

	pci_read_config_dword(nhi->pdev, VS_CAP_16, &max_ltr);
	max_ltr &= 0xffff;
	/* Program the same value for both snoop and no-snoop */
	ltr = max_ltr << 16 | max_ltr;
	pci_write_config_dword(nhi->pdev, VS_CAP_15, ltr);
}

static int icl_nhi_suspend(struct tb_nhi *nhi)
{
	int ret;

	if (icl_nhi_is_device_connected(nhi))
		return 0;

	/*
	 * If there is no device connected we need to perform both: a
	 * handshake through LC mailbox and force power down before
	 * entering D3.
	 */
	icl_nhi_lc_mailbox_cmd(nhi, ICL_LC_PREPARE_FOR_RESET);
	ret = icl_nhi_lc_mailbox_cmd_complete(nhi, ICL_LC_MAILBOX_TIMEOUT);
	if (ret)
		return ret;

	return icl_nhi_force_power(nhi, false);
}

static int icl_nhi_suspend_noirq(struct tb_nhi *nhi, bool wakeup)
{
	enum icl_lc_mailbox_cmd cmd;

	if (!pm_suspend_via_firmware())
		return icl_nhi_suspend(nhi);

	cmd = wakeup ? ICL_LC_GO2SX : ICL_LC_GO2SX_NO_WAKE;
	icl_nhi_lc_mailbox_cmd(nhi, cmd);
	return icl_nhi_lc_mailbox_cmd_complete(nhi, ICL_LC_MAILBOX_TIMEOUT);
}

static int icl_nhi_resume(struct tb_nhi *nhi)
{
	int ret;

	ret = icl_nhi_force_power(nhi, true);
	if (ret)
		return ret;

	icl_nhi_set_ltr(nhi);
	return 0;
}

static void icl_nhi_shutdown(struct tb_nhi *nhi)
{
	icl_nhi_force_power(nhi, false);
}

const struct tb_nhi_ops icl_nhi_ops = {
	.init = icl_nhi_resume,
	.suspend_noirq = icl_nhi_suspend_noirq,
	.resume_noirq = icl_nhi_resume,
	.runtime_suspend = icl_nhi_suspend,
	.runtime_resume = icl_nhi_resume,
	.shutdown = icl_nhi_shutdown,
};
