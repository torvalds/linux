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

#ifndef _AHB_H_
#define _AHB_H_

#include <linux/platform_device.h>

struct ath10k_ahb {
	struct platform_device *pdev;
	void __iomem *mem;
	unsigned long mem_len;
	void __iomem *gcc_mem;
	void __iomem *tcsr_mem;

	int irq;

	struct clk *cmd_clk;
	struct clk *ref_clk;
	struct clk *rtc_clk;

	struct reset_control *core_cold_rst;
	struct reset_control *radio_cold_rst;
	struct reset_control *radio_warm_rst;
	struct reset_control *radio_srif_rst;
	struct reset_control *cpu_init_rst;
};

#ifdef CONFIG_ATH10K_AHB

#define ATH10K_GCC_REG_BASE                  0x1800000
#define ATH10K_GCC_REG_SIZE                  0x60000

#define ATH10K_TCSR_REG_BASE                 0x1900000
#define ATH10K_TCSR_REG_SIZE                 0x80000

#define ATH10K_AHB_GCC_FEPLL_PLL_DIV         0x2f020
#define ATH10K_AHB_WIFI_SCRATCH_5_REG        0x4f014

#define ATH10K_AHB_WLAN_CORE_ID_REG          0x82030

#define ATH10K_AHB_TCSR_WIFI0_GLB_CFG        0x49000
#define ATH10K_AHB_TCSR_WIFI1_GLB_CFG        0x49004
#define TCSR_WIFIX_GLB_CFG_DISABLE_CORE_CLK  BIT(25)

#define ATH10K_AHB_TCSR_WCSS0_HALTREQ        0x52000
#define ATH10K_AHB_TCSR_WCSS1_HALTREQ        0x52010
#define ATH10K_AHB_TCSR_WCSS0_HALTACK        0x52004
#define ATH10K_AHB_TCSR_WCSS1_HALTACK        0x52014

#define ATH10K_AHB_AXI_BUS_HALT_TIMEOUT      10 /* msec */
#define AHB_AXI_BUS_HALT_REQ                 1
#define AHB_AXI_BUS_HALT_ACK                 1

#define ATH10K_AHB_CORE_CTRL_CPU_INTR_MASK   1

int ath10k_ahb_init(void);
void ath10k_ahb_exit(void);

#else /* CONFIG_ATH10K_AHB */

static inline int ath10k_ahb_init(void)
{
	return 0;
}

static inline void ath10k_ahb_exit(void)
{
}

#endif /* CONFIG_ATH10K_AHB */

#endif /* _AHB_H_ */
