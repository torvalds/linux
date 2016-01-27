/*
 * Copyright (c) 2016 Qualcomm Atheros, Inc. All rights reserved.
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/module.h>
#include "core.h"
#include "debug.h"
#include "pci.h"
#include "ahb.h"

static const struct of_device_id ath10k_ahb_of_match[] = {
	/* TODO: enable this entry once everything in place.
	 * { .compatible = "qcom,ipq4019-wifi",
	 *   .data = (void *)ATH10K_HW_QCA4019 },
	 */
	{ }
};

MODULE_DEVICE_TABLE(of, ath10k_ahb_of_match);

static inline struct ath10k_ahb *ath10k_ahb_priv(struct ath10k *ar)
{
	return &((struct ath10k_pci *)ar->drv_priv)->ahb[0];
}

static void ath10k_ahb_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	iowrite32(value, ar_ahb->mem + offset);
}

static u32 ath10k_ahb_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	return ioread32(ar_ahb->mem + offset);
}

static u32 ath10k_ahb_gcc_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	return ioread32(ar_ahb->gcc_mem + offset);
}

static void ath10k_ahb_tcsr_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	iowrite32(value, ar_ahb->tcsr_mem + offset);
}

static u32 ath10k_ahb_tcsr_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	return ioread32(ar_ahb->tcsr_mem + offset);
}

static u32 ath10k_ahb_soc_read32(struct ath10k *ar, u32 addr)
{
	return ath10k_ahb_read32(ar, RTC_SOC_BASE_ADDRESS + addr);
}

static int ath10k_ahb_get_num_banks(struct ath10k *ar)
{
	if (ar->hw_rev == ATH10K_HW_QCA4019)
		return 1;

	ath10k_warn(ar, "unknown number of banks, assuming 1\n");
	return 1;
}

static int ath10k_ahb_probe(struct platform_device *pdev)
{
	return 0;
}

static int ath10k_ahb_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ath10k_ahb_driver = {
	.driver         = {
		.name   = "ath10k_ahb",
		.of_match_table = ath10k_ahb_of_match,
	},
	.probe  = ath10k_ahb_probe,
	.remove = ath10k_ahb_remove,
};

int ath10k_ahb_init(void)
{
	int ret;

	printk(KERN_ERR "AHB support is still work in progress\n");

	ret = platform_driver_register(&ath10k_ahb_driver);
	if (ret)
		printk(KERN_ERR "failed to register ath10k ahb driver: %d\n",
		       ret);
	return ret;
}

void ath10k_ahb_exit(void)
{
	platform_driver_unregister(&ath10k_ahb_driver);
}
