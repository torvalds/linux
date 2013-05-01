/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <asm/cpu-info.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/mipsregs.h>
#include <asm/netlogic/xlr/fmn.h>
#include <asm/netlogic/xlr/xlr.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/haldefs.h>

struct xlr_board_fmn_config xlr_board_fmn_config;

static void __maybe_unused print_credit_config(struct xlr_fmn_info *fmn_info)
{
	int bkt;

	pr_info("Bucket size :\n");
	pr_info("Station\t: Size\n");
	for (bkt = 0; bkt < 16; bkt++)
		pr_info(" %d  %d  %d  %d  %d  %d  %d %d\n",
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 0],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 1],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 2],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 3],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 4],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 5],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 6],
			xlr_board_fmn_config.bucket_size[(bkt * 8) + 7]);
	pr_info("\n");

	pr_info("Credits distribution :\n");
	pr_info("Station\t: Size\n");
	for (bkt = 0; bkt < 16; bkt++)
		pr_info(" %d  %d  %d  %d  %d  %d  %d %d\n",
			fmn_info->credit_config[(bkt * 8) + 0],
			fmn_info->credit_config[(bkt * 8) + 1],
			fmn_info->credit_config[(bkt * 8) + 2],
			fmn_info->credit_config[(bkt * 8) + 3],
			fmn_info->credit_config[(bkt * 8) + 4],
			fmn_info->credit_config[(bkt * 8) + 5],
			fmn_info->credit_config[(bkt * 8) + 6],
			fmn_info->credit_config[(bkt * 8) + 7]);
	pr_info("\n");
}

static void check_credit_distribution(void)
{
	struct xlr_board_fmn_config *cfg = &xlr_board_fmn_config;
	int bkt, n, total_credits, ncores;

	ncores = hweight32(nlm_current_node()->coremask);
	for (bkt = 0; bkt < 128; bkt++) {
		total_credits = 0;
		for (n = 0; n < ncores; n++)
			total_credits += cfg->cpu[n].credit_config[bkt];
		total_credits += cfg->gmac[0].credit_config[bkt];
		total_credits += cfg->gmac[1].credit_config[bkt];
		total_credits += cfg->dma.credit_config[bkt];
		total_credits += cfg->cmp.credit_config[bkt];
		total_credits += cfg->sae.credit_config[bkt];
		total_credits += cfg->xgmac[0].credit_config[bkt];
		total_credits += cfg->xgmac[1].credit_config[bkt];
		if (total_credits > cfg->bucket_size[bkt])
			pr_err("ERROR: Bucket %d: credits (%d) > size (%d)\n",
				bkt, total_credits, cfg->bucket_size[bkt]);
	}
	pr_info("Credit distribution complete.\n");
}

/**
 * Configure bucket size and credits for a device. 'size' is the size of
 * the buckets for the device. This size is distributed among all the CPUs
 * so that all of them can send messages to the device.
 *
 * The device is also given 'cpu_credits' to send messages to the CPUs
 *
 * @dev_info: FMN information structure for each devices
 * @start_stn_id: Starting station id of dev_info
 * @end_stn_id: End station id of dev_info
 * @num_buckets: Total number of buckets for den_info
 * @cpu_credits: Allowed credits to cpu for each devices pointing by dev_info
 * @size: Size of the each buckets in the device station
 */
static void setup_fmn_cc(struct xlr_fmn_info *dev_info, int start_stn_id,
		int end_stn_id, int num_buckets, int cpu_credits, int size)
{
	int i, j, num_core, n, credits_per_cpu;
	struct xlr_fmn_info *cpu = xlr_board_fmn_config.cpu;

	num_core = hweight32(nlm_current_node()->coremask);
	dev_info->num_buckets	= num_buckets;
	dev_info->start_stn_id	= start_stn_id;
	dev_info->end_stn_id	= end_stn_id;

	n = num_core;
	if (num_core == 3)
		n = 4;

	for (i = start_stn_id; i <= end_stn_id; i++) {
		xlr_board_fmn_config.bucket_size[i] = size;

		/* Dividing device credits equally to cpus */
		credits_per_cpu = size / n;
		for (j = 0; j < num_core; j++)
			cpu[j].credit_config[i] = credits_per_cpu;

		/* credits left to distribute */
		credits_per_cpu = size - (credits_per_cpu * num_core);

		/* distribute the remaining credits (if any), among cores */
		for (j = 0; (j < num_core) && (credits_per_cpu >= 4); j++) {
			cpu[j].credit_config[i] += 4;
			credits_per_cpu -= 4;
		}
	}

	/* Distributing cpu per bucket credits to devices */
	for (i = 0; i < num_core; i++) {
		for (j = 0; j < FMN_CORE_NBUCKETS; j++)
			dev_info->credit_config[(i * 8) + j] = cpu_credits;
	}
}

/*
 * Each core has 256 slots and 8 buckets,
 * Configure the 8 buckets each with 32 slots
 */
static void setup_cpu_fmninfo(struct xlr_fmn_info *cpu, int num_core)
{
	int i, j;

	for (i = 0; i < num_core; i++) {
		cpu[i].start_stn_id	= (8 * i);
		cpu[i].end_stn_id	= (8 * i + 8);

		for (j = cpu[i].start_stn_id; j < cpu[i].end_stn_id; j++)
			xlr_board_fmn_config.bucket_size[j] = 32;
	}
}

/**
 * Setup the FMN details for each devices according to the device available
 * in each variant of XLR/XLS processor
 */
void xlr_board_info_setup(void)
{
	struct xlr_fmn_info *cpu = xlr_board_fmn_config.cpu;
	struct xlr_fmn_info *gmac = xlr_board_fmn_config.gmac;
	struct xlr_fmn_info *xgmac = xlr_board_fmn_config.xgmac;
	struct xlr_fmn_info *dma = &xlr_board_fmn_config.dma;
	struct xlr_fmn_info *cmp = &xlr_board_fmn_config.cmp;
	struct xlr_fmn_info *sae = &xlr_board_fmn_config.sae;
	int processor_id, num_core;

	num_core = hweight32(nlm_current_node()->coremask);
	processor_id = read_c0_prid() & 0xff00;

	setup_cpu_fmninfo(cpu, num_core);
	switch (processor_id) {
	case PRID_IMP_NETLOGIC_XLS104:
	case PRID_IMP_NETLOGIC_XLS108:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 16, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 8, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 8, 128);
		break;

	case PRID_IMP_NETLOGIC_XLS204:
	case PRID_IMP_NETLOGIC_XLS208:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 16, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 8, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 8, 128);
		break;

	case PRID_IMP_NETLOGIC_XLS404:
	case PRID_IMP_NETLOGIC_XLS408:
	case PRID_IMP_NETLOGIC_XLS404B:
	case PRID_IMP_NETLOGIC_XLS408B:
	case PRID_IMP_NETLOGIC_XLS416B:
	case PRID_IMP_NETLOGIC_XLS608B:
	case PRID_IMP_NETLOGIC_XLS616B:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 8, 32);
		setup_fmn_cc(&gmac[1], FMN_STNID_GMAC1_FR_0,
					FMN_STNID_GMAC1_TX3, 8, 8, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 4, 64);
		setup_fmn_cc(cmp, FMN_STNID_CMP_0,
					FMN_STNID_CMP_3, 4, 4, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 8, 128);
		break;

	case PRID_IMP_NETLOGIC_XLS412B:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 8, 32);
		setup_fmn_cc(&gmac[1], FMN_STNID_GMAC1_FR_0,
					FMN_STNID_GMAC1_TX3, 8, 8, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 4, 64);
		setup_fmn_cc(cmp, FMN_STNID_CMP_0,
					FMN_STNID_CMP_3, 4, 4, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 8, 128);
		break;

	case PRID_IMP_NETLOGIC_XLR308:
	case PRID_IMP_NETLOGIC_XLR308C:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 16, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 8, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 4, 128);
		break;

	case PRID_IMP_NETLOGIC_XLR532:
	case PRID_IMP_NETLOGIC_XLR532C:
	case PRID_IMP_NETLOGIC_XLR516C:
	case PRID_IMP_NETLOGIC_XLR508C:
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 16, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 8, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 4, 128);
		break;

	case PRID_IMP_NETLOGIC_XLR732:
	case PRID_IMP_NETLOGIC_XLR716:
		setup_fmn_cc(&xgmac[0], FMN_STNID_XMAC0_00_TX,
					FMN_STNID_XMAC0_15_TX, 8, 0, 32);
		setup_fmn_cc(&xgmac[1], FMN_STNID_XMAC1_00_TX,
					FMN_STNID_XMAC1_15_TX, 8, 0, 32);
		setup_fmn_cc(&gmac[0], FMN_STNID_GMAC0,
					FMN_STNID_GMAC0_TX3, 8, 24, 32);
		setup_fmn_cc(dma, FMN_STNID_DMA_0,
					FMN_STNID_DMA_3, 4, 4, 64);
		setup_fmn_cc(sae, FMN_STNID_SEC0,
					FMN_STNID_SEC1, 2, 4, 128);
		break;
	default:
		pr_err("Unknown CPU with processor ID [%d]\n", processor_id);
		pr_err("Error: Cannot initialize FMN credits.\n");
	}

	check_credit_distribution();

#if 0 /* debug */
	print_credit_config(&cpu[0]);
	print_credit_config(&gmac[0]);
#endif
}
