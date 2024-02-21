/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DRIVERS_MMC_SDHCI_MSM_SCALING_H
#define _DRIVERS_MMC_SDHCI_MSM_SCALING_H

#include <linux/devfreq.h>
#include "sdhci-msm.h"

#include "../core/queue.h"
#include "../core/host.h"
#include "../core/mmc_ops.h"
#include "../core/core.h"

struct sdhci_msm_host;
enum sdhci_msm_mmc_load;

#define MMC_READ_SINGLE_BLOCK    17   /* adtc [31:0] data addr   R1  */
#define MMC_READ_MULTIPLE_BLOCK  18   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_BLOCK          24   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_MULTIPLE_BLOCK 25   /* adtc                    R1  */
#define MMC_DEVFRQ_DEFAULT_UP_THRESHOLD 35
#define MMC_DEVFRQ_DEFAULT_DOWN_THRESHOLD 5
#define MMC_DEVFRQ_DEFAULT_POLLING_MSEC 100

extern int mmc_select_bus_width(struct mmc_card *card);
extern int mmc_select_hs(struct mmc_card *card);
extern int mmc_select_hs_ddr(struct mmc_card *card);
extern int mmc_select_hs400(struct mmc_card *card);
extern int mmc_hs200_tuning(struct mmc_card *card);
extern int mmc_select_hs200(struct mmc_card *card);
extern int mmc_select_timing(struct mmc_card *card);

void sdhci_msm_scale_parse_dt(struct device *dev, struct sdhci_msm_host *msm_host);
void sdhci_msm_mmc_deferred_scaling(struct sdhci_msm_host *host);
void _sdhci_msm_mmc_cqe_clk_scaling_start_busy(struct mmc_queue *mq,
			struct sdhci_msm_host *host, bool lock_needed);
void _sdhci_msm_mmc_cqe_clk_scaling_stop_busy(struct sdhci_msm_host *host,
	bool lock_needed, int is_cqe_dcmd);
int _sdhci_msm_mmc_exit_clk_scaling(struct sdhci_msm_host *host);
int _sdhci_msm_mmc_suspend_clk_scaling(struct sdhci_msm_host *host);
int _sdhci_msm_mmc_resume_clk_scaling(struct sdhci_msm_host *host);
int _sdhci_msm_mmc_init_clk_scaling(struct sdhci_msm_host *host);
bool sdhci_msm_mmc_is_data_request(u32 opcode);
void _sdhci_msm_mmc_clk_scaling_start_busy(struct sdhci_msm_host *host, bool lock_needed);
void _sdhci_msm_mmc_clk_scaling_stop_busy(struct sdhci_msm_host *host, bool lock_needed);

struct mmc_card;
struct mmc_queue;

void sdhci_msm_update_curr_part(struct mmc_host *host, unsigned int part_type);
void sdhci_msm_dec_active_req(struct mmc_host *host);
void sdhci_msm_inc_active_req(struct mmc_host *host);
void sdhci_msm_is_dcmd(int data, int *err);
extern void sdhci_msm_mmc_cqe_clk_scaling_stop_busy(struct mmc_host *host, struct mmc_request *mrq);
extern void sdhci_msm_mmc_cqe_clk_scaling_start_busy(struct mmc_host *host,
		struct mmc_request *mrq);
void sdhci_msm_set_active_reqs(struct mmc_host *host);
void sdhci_msm_set_factors(struct mmc_host *host);
void sdhci_msm_mmc_init_setup_scaling(struct mmc_card *card, struct mmc_host *host);
void sdhci_msm_mmc_exit_clk_scaling(struct mmc_host *host);
void sdhci_msm_mmc_suspend_clk_scaling(struct mmc_host *host);
void sdhci_msm_mmc_resume_clk_scaling(struct mmc_host *host);
void sdhci_msm_mmc_init_clk_scaling(struct mmc_host *host);
void sdhci_msm_cqe_scaling_resume(struct mmc_host *host);
bool sdhci_msm_mmc_can_scale_clk(struct sdhci_msm_host *msm_host);
int sdhci_msm_mmc_clk_update_freq(struct sdhci_msm_host *host,
		unsigned long freq, enum sdhci_msm_mmc_load state);
#endif
