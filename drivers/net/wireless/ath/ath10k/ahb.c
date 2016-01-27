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
#include <linux/clk.h>
#include <linux/reset.h>
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

static int ath10k_ahb_clock_init(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);
	struct device *dev;
	int ret;

	dev = &ar_ahb->pdev->dev;

	ar_ahb->cmd_clk = clk_get(dev, "wifi_wcss_cmd");
	if (IS_ERR_OR_NULL(ar_ahb->cmd_clk)) {
		ath10k_err(ar, "failed to get cmd clk: %ld\n",
			   PTR_ERR(ar_ahb->cmd_clk));
		ret = ar_ahb->cmd_clk ? PTR_ERR(ar_ahb->cmd_clk) : -ENODEV;
		goto out;
	}

	ar_ahb->ref_clk = clk_get(dev, "wifi_wcss_ref");
	if (IS_ERR_OR_NULL(ar_ahb->ref_clk)) {
		ath10k_err(ar, "failed to get ref clk: %ld\n",
			   PTR_ERR(ar_ahb->ref_clk));
		ret = ar_ahb->ref_clk ? PTR_ERR(ar_ahb->ref_clk) : -ENODEV;
		goto err_cmd_clk_put;
	}

	ar_ahb->rtc_clk = clk_get(dev, "wifi_wcss_rtc");
	if (IS_ERR_OR_NULL(ar_ahb->rtc_clk)) {
		ath10k_err(ar, "failed to get rtc clk: %ld\n",
			   PTR_ERR(ar_ahb->rtc_clk));
		ret = ar_ahb->rtc_clk ? PTR_ERR(ar_ahb->rtc_clk) : -ENODEV;
		goto err_ref_clk_put;
	}

	return 0;

err_ref_clk_put:
	clk_put(ar_ahb->ref_clk);

err_cmd_clk_put:
	clk_put(ar_ahb->cmd_clk);

out:
	return ret;
}

static void ath10k_ahb_clock_deinit(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	if (!IS_ERR_OR_NULL(ar_ahb->cmd_clk))
		clk_put(ar_ahb->cmd_clk);

	if (!IS_ERR_OR_NULL(ar_ahb->ref_clk))
		clk_put(ar_ahb->ref_clk);

	if (!IS_ERR_OR_NULL(ar_ahb->rtc_clk))
		clk_put(ar_ahb->rtc_clk);

	ar_ahb->cmd_clk = NULL;
	ar_ahb->ref_clk = NULL;
	ar_ahb->rtc_clk = NULL;
}

static int ath10k_ahb_clock_enable(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);
	struct device *dev;
	int ret;

	dev = &ar_ahb->pdev->dev;

	if (IS_ERR_OR_NULL(ar_ahb->cmd_clk) ||
	    IS_ERR_OR_NULL(ar_ahb->ref_clk) ||
	    IS_ERR_OR_NULL(ar_ahb->rtc_clk)) {
		ath10k_err(ar, "clock(s) is/are not initialized\n");
		ret = -EIO;
		goto out;
	}

	ret = clk_prepare_enable(ar_ahb->cmd_clk);
	if (ret) {
		ath10k_err(ar, "failed to enable cmd clk: %d\n", ret);
		goto out;
	}

	ret = clk_prepare_enable(ar_ahb->ref_clk);
	if (ret) {
		ath10k_err(ar, "failed to enable ref clk: %d\n", ret);
		goto err_cmd_clk_disable;
	}

	ret = clk_prepare_enable(ar_ahb->rtc_clk);
	if (ret) {
		ath10k_err(ar, "failed to enable rtc clk: %d\n", ret);
		goto err_ref_clk_disable;
	}

	return 0;

err_ref_clk_disable:
	clk_disable_unprepare(ar_ahb->ref_clk);

err_cmd_clk_disable:
	clk_disable_unprepare(ar_ahb->cmd_clk);

out:
	return ret;
}

static void ath10k_ahb_clock_disable(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	if (!IS_ERR_OR_NULL(ar_ahb->cmd_clk))
		clk_disable_unprepare(ar_ahb->cmd_clk);

	if (!IS_ERR_OR_NULL(ar_ahb->ref_clk))
		clk_disable_unprepare(ar_ahb->ref_clk);

	if (!IS_ERR_OR_NULL(ar_ahb->rtc_clk))
		clk_disable_unprepare(ar_ahb->rtc_clk);
}

static int ath10k_ahb_rst_ctrl_init(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);
	struct device *dev;
	int ret;

	dev = &ar_ahb->pdev->dev;

	ar_ahb->core_cold_rst = reset_control_get(dev, "wifi_core_cold");
	if (IS_ERR_OR_NULL(ar_ahb->core_cold_rst)) {
		ath10k_err(ar, "failed to get core cold rst ctrl: %ld\n",
			   PTR_ERR(ar_ahb->core_cold_rst));
		ret = ar_ahb->core_cold_rst ?
			PTR_ERR(ar_ahb->core_cold_rst) : -ENODEV;
		goto out;
	}

	ar_ahb->radio_cold_rst = reset_control_get(dev, "wifi_radio_cold");
	if (IS_ERR_OR_NULL(ar_ahb->radio_cold_rst)) {
		ath10k_err(ar, "failed to get radio cold rst ctrl: %ld\n",
			   PTR_ERR(ar_ahb->radio_cold_rst));
		ret = ar_ahb->radio_cold_rst ?
			PTR_ERR(ar_ahb->radio_cold_rst) : -ENODEV;
		goto err_core_cold_rst_put;
	}

	ar_ahb->radio_warm_rst = reset_control_get(dev, "wifi_radio_warm");
	if (IS_ERR_OR_NULL(ar_ahb->radio_warm_rst)) {
		ath10k_err(ar, "failed to get radio warm rst ctrl: %ld\n",
			   PTR_ERR(ar_ahb->radio_warm_rst));
		ret = ar_ahb->radio_warm_rst ?
			PTR_ERR(ar_ahb->radio_warm_rst) : -ENODEV;
		goto err_radio_cold_rst_put;
	}

	ar_ahb->radio_srif_rst = reset_control_get(dev, "wifi_radio_srif");
	if (IS_ERR_OR_NULL(ar_ahb->radio_srif_rst)) {
		ath10k_err(ar, "failed to get radio srif rst ctrl: %ld\n",
			   PTR_ERR(ar_ahb->radio_srif_rst));
		ret = ar_ahb->radio_srif_rst ?
			PTR_ERR(ar_ahb->radio_srif_rst) : -ENODEV;
		goto err_radio_warm_rst_put;
	}

	ar_ahb->cpu_init_rst = reset_control_get(dev, "wifi_cpu_init");
	if (IS_ERR_OR_NULL(ar_ahb->cpu_init_rst)) {
		ath10k_err(ar, "failed to get cpu init rst ctrl: %ld\n",
			   PTR_ERR(ar_ahb->cpu_init_rst));
		ret = ar_ahb->cpu_init_rst ?
			PTR_ERR(ar_ahb->cpu_init_rst) : -ENODEV;
		goto err_radio_srif_rst_put;
	}

	return 0;

err_radio_srif_rst_put:
	reset_control_put(ar_ahb->radio_srif_rst);

err_radio_warm_rst_put:
	reset_control_put(ar_ahb->radio_warm_rst);

err_radio_cold_rst_put:
	reset_control_put(ar_ahb->radio_cold_rst);

err_core_cold_rst_put:
	reset_control_put(ar_ahb->core_cold_rst);

out:
	return ret;
}

static void ath10k_ahb_rst_ctrl_deinit(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);

	if (!IS_ERR_OR_NULL(ar_ahb->core_cold_rst))
		reset_control_put(ar_ahb->core_cold_rst);

	if (!IS_ERR_OR_NULL(ar_ahb->radio_cold_rst))
		reset_control_put(ar_ahb->radio_cold_rst);

	if (!IS_ERR_OR_NULL(ar_ahb->radio_warm_rst))
		reset_control_put(ar_ahb->radio_warm_rst);

	if (!IS_ERR_OR_NULL(ar_ahb->radio_srif_rst))
		reset_control_put(ar_ahb->radio_srif_rst);

	if (!IS_ERR_OR_NULL(ar_ahb->cpu_init_rst))
		reset_control_put(ar_ahb->cpu_init_rst);

	ar_ahb->core_cold_rst = NULL;
	ar_ahb->radio_cold_rst = NULL;
	ar_ahb->radio_warm_rst = NULL;
	ar_ahb->radio_srif_rst = NULL;
	ar_ahb->cpu_init_rst = NULL;
}

static int ath10k_ahb_release_reset(struct ath10k *ar)
{
	struct ath10k_ahb *ar_ahb = ath10k_ahb_priv(ar);
	int ret;

	if (IS_ERR_OR_NULL(ar_ahb->radio_cold_rst) ||
	    IS_ERR_OR_NULL(ar_ahb->radio_warm_rst) ||
	    IS_ERR_OR_NULL(ar_ahb->radio_srif_rst) ||
	    IS_ERR_OR_NULL(ar_ahb->cpu_init_rst)) {
		ath10k_err(ar, "rst ctrl(s) is/are not initialized\n");
		return -EINVAL;
	}

	ret = reset_control_deassert(ar_ahb->radio_cold_rst);
	if (ret) {
		ath10k_err(ar, "failed to deassert radio cold rst: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(ar_ahb->radio_warm_rst);
	if (ret) {
		ath10k_err(ar, "failed to deassert radio warm rst: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(ar_ahb->radio_srif_rst);
	if (ret) {
		ath10k_err(ar, "failed to deassert radio srif rst: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(ar_ahb->cpu_init_rst);
	if (ret) {
		ath10k_err(ar, "failed to deassert cpu init rst: %d\n", ret);
		return ret;
	}

	return 0;
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
