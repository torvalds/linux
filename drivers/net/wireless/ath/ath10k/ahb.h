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
	void __iomem *gcc_mem;
	void __iomem *tcsr_mem;

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
