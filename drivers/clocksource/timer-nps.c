/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/cpu.h>
#include <soc/nps/common.h>

#define NPS_MSU_TICK_LOW	0xC8
#define NPS_CLUSTER_OFFSET	8
#define NPS_CLUSTER_NUM		16

/* This array is per cluster of CPUs (Each NPS400 cluster got 256 CPUs) */
static void *nps_msu_reg_low_addr[NPS_CLUSTER_NUM] __read_mostly;

static unsigned long nps_timer_rate;

static cycle_t nps_clksrc_read(struct clocksource *clksrc)
{
	int cluster = raw_smp_processor_id() >> NPS_CLUSTER_OFFSET;

	return (cycle_t)ioread32be(nps_msu_reg_low_addr[cluster]);
}

static void __init nps_setup_clocksource(struct device_node *node,
					 struct clk *clk)
{
	int ret, cluster;

	for (cluster = 0; cluster < NPS_CLUSTER_NUM; cluster++)
		nps_msu_reg_low_addr[cluster] =
			nps_host_reg((cluster << NPS_CLUSTER_OFFSET),
				 NPS_MSU_BLKID, NPS_MSU_TICK_LOW);

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Couldn't enable parent clock\n");
		return;
	}

	nps_timer_rate = clk_get_rate(clk);

	ret = clocksource_mmio_init(nps_msu_reg_low_addr, "EZnps-tick",
				    nps_timer_rate, 301, 32, nps_clksrc_read);
	if (ret) {
		pr_err("Couldn't register clock source.\n");
		clk_disable_unprepare(clk);
	}
}

static void __init nps_timer_init(struct device_node *node)
{
	struct clk *clk;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("Can't get timer clock.\n");
		return;
	}

	nps_setup_clocksource(node, clk);
}

CLOCKSOURCE_OF_DECLARE(ezchip_nps400_clksrc, "ezchip,nps400-timer",
		       nps_timer_init);
