// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 */

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/dev_printk.h>
#include <linux/io.h>
#include <linux/irqreturn.h>

#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_reg.h"

static void t7xx_mhccif_clear_interrupts(struct t7xx_pci_dev *t7xx_dev, u32 mask)
{
	void __iomem *mhccif_pbase = t7xx_dev->base_addr.mhccif_rc_base;

	/* Clear level 2 interrupt */
	iowrite32(mask, mhccif_pbase + REG_EP2RC_SW_INT_ACK);
	/* Ensure write is complete */
	t7xx_mhccif_read_sw_int_sts(t7xx_dev);
	/* Clear level 1 interrupt */
	t7xx_pcie_mac_clear_int_status(t7xx_dev, MHCCIF_INT);
}

static irqreturn_t t7xx_mhccif_isr_thread(int irq, void *data)
{
	struct t7xx_pci_dev *t7xx_dev = data;
	u32 int_status, val;

	val = T7XX_L1_1_BIT(1) | T7XX_L1_2_BIT(1);
	iowrite32(val, IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);

	int_status = t7xx_mhccif_read_sw_int_sts(t7xx_dev);
	if (int_status & D2H_SW_INT_MASK) {
		int ret = t7xx_pci_mhccif_isr(t7xx_dev);

		if (ret)
			dev_err(&t7xx_dev->pdev->dev, "PCI MHCCIF ISR failure: %d", ret);
	}

	t7xx_mhccif_clear_interrupts(t7xx_dev, int_status);
	t7xx_pcie_mac_set_int(t7xx_dev, MHCCIF_INT);
	return IRQ_HANDLED;
}

u32 t7xx_mhccif_read_sw_int_sts(struct t7xx_pci_dev *t7xx_dev)
{
	return ioread32(t7xx_dev->base_addr.mhccif_rc_base + REG_EP2RC_SW_INT_STS);
}

void t7xx_mhccif_mask_set(struct t7xx_pci_dev *t7xx_dev, u32 val)
{
	iowrite32(val, t7xx_dev->base_addr.mhccif_rc_base + REG_EP2RC_SW_INT_EAP_MASK_SET);
}

void t7xx_mhccif_mask_clr(struct t7xx_pci_dev *t7xx_dev, u32 val)
{
	iowrite32(val, t7xx_dev->base_addr.mhccif_rc_base + REG_EP2RC_SW_INT_EAP_MASK_CLR);
}

u32 t7xx_mhccif_mask_get(struct t7xx_pci_dev *t7xx_dev)
{
	return ioread32(t7xx_dev->base_addr.mhccif_rc_base + REG_EP2RC_SW_INT_EAP_MASK);
}

static irqreturn_t t7xx_mhccif_isr_handler(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

void t7xx_mhccif_init(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_dev->base_addr.mhccif_rc_base = t7xx_dev->base_addr.pcie_ext_reg_base +
					    MHCCIF_RC_DEV_BASE -
					    t7xx_dev->base_addr.pcie_dev_reg_trsl_addr;

	t7xx_dev->intr_handler[MHCCIF_INT] = t7xx_mhccif_isr_handler;
	t7xx_dev->intr_thread[MHCCIF_INT] = t7xx_mhccif_isr_thread;
	t7xx_dev->callback_param[MHCCIF_INT] = t7xx_dev;
}

void t7xx_mhccif_h2d_swint_trigger(struct t7xx_pci_dev *t7xx_dev, u32 channel)
{
	void __iomem *mhccif_pbase = t7xx_dev->base_addr.mhccif_rc_base;

	iowrite32(BIT(channel), mhccif_pbase + REG_RC2EP_SW_BSY);
	iowrite32(channel, mhccif_pbase + REG_RC2EP_SW_TCHNUM);
}
