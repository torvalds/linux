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

static int __init nps_get_timer_clk(struct device_node *node,
			     unsigned long *timer_freq,
			     struct clk **clk)
{
	int ret;

	*clk = of_clk_get(node, 0);
	if (IS_ERR(*clk)) {
		pr_err("timer missing clk");
		return PTR_ERR(*clk);
	}

	ret = clk_prepare_enable(*clk);
	if (ret) {
		pr_err("Couldn't enable parent clk\n");
		clk_put(*clk);
		return ret;
	}

	*timer_freq = clk_get_rate(*clk);
	if (!(*timer_freq)) {
		pr_err("Couldn't get clk rate\n");
		clk_disable_unprepare(*clk);
		clk_put(*clk);
		return -EINVAL;
	}

	return 0;
}

static cycle_t nps_clksrc_read(struct clocksource *clksrc)
{
	int cluster = raw_smp_processor_id() >> NPS_CLUSTER_OFFSET;

	return (cycle_t)ioread32be(nps_msu_reg_low_addr[cluster]);
}

static int __init nps_setup_clocksource(struct device_node *node)
{
	int ret, cluster;
	struct clk *clk;
	unsigned long nps_timer1_freq;


	for (cluster = 0; cluster < NPS_CLUSTER_NUM; cluster++)
		nps_msu_reg_low_addr[cluster] =
			nps_host_reg((cluster << NPS_CLUSTER_OFFSET),
				     NPS_MSU_BLKID, NPS_MSU_TICK_LOW);

	ret = nps_get_timer_clk(node, &nps_timer1_freq, &clk);
	if (ret)
		return ret;

	ret = clocksource_mmio_init(nps_msu_reg_low_addr, "nps-tick",
				    nps_timer1_freq, 300, 32, nps_clksrc_read);
	if (ret) {
		pr_err("Couldn't register clock source.\n");
		clk_disable_unprepare(clk);
	}

	return ret;
}

CLOCKSOURCE_OF_DECLARE(ezchip_nps400_clksrc, "ezchip,nps400-timer",
		       nps_setup_clocksource);
