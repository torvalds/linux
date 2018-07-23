/*
 *  linux/drivers/mmc/core/core.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_CORE_H
#define _MMC_CORE_CORE_H

#include <linux/delay.h>
#include <linux/sched.h>

struct mmc_host;
struct mmc_card;
struct mmc_request;

#define MMC_CMD_RETRIES        3

struct mmc_bus_ops {
	void (*remove)(struct mmc_host *);
	void (*detect)(struct mmc_host *);
	int (*pre_suspend)(struct mmc_host *);
	int (*suspend)(struct mmc_host *);
	int (*resume)(struct mmc_host *);
	int (*runtime_suspend)(struct mmc_host *);
	int (*runtime_resume)(struct mmc_host *);
	int (*power_save)(struct mmc_host *);
	int (*power_restore)(struct mmc_host *);
	int (*alive)(struct mmc_host *);
	int (*shutdown)(struct mmc_host *);
	int (*hw_reset)(struct mmc_host *);
	int (*sw_reset)(struct mmc_host *);
};

void mmc_attach_bus(struct mmc_host *host, const struct mmc_bus_ops *ops);
void mmc_detach_bus(struct mmc_host *host);

struct device_node *mmc_of_find_child_device(struct mmc_host *host,
		unsigned func_num);

void mmc_init_erase(struct mmc_card *card);

void mmc_set_chip_select(struct mmc_host *host, int mode);
void mmc_set_clock(struct mmc_host *host, unsigned int hz);
void mmc_set_bus_mode(struct mmc_host *host, unsigned int mode);
void mmc_set_bus_width(struct mmc_host *host, unsigned int width);
u32 mmc_select_voltage(struct mmc_host *host, u32 ocr);
int mmc_set_uhs_voltage(struct mmc_host *host, u32 ocr);
int mmc_host_set_uhs_voltage(struct mmc_host *host);
int mmc_set_signal_voltage(struct mmc_host *host, int signal_voltage);
void mmc_set_initial_signal_voltage(struct mmc_host *host);
void mmc_set_timing(struct mmc_host *host, unsigned int timing);
void mmc_set_driver_type(struct mmc_host *host, unsigned int drv_type);
int mmc_select_drive_strength(struct mmc_card *card, unsigned int max_dtr,
			      int card_drv_type, int *drv_type);
void mmc_power_up(struct mmc_host *host, u32 ocr);
void mmc_power_off(struct mmc_host *host);
void mmc_power_cycle(struct mmc_host *host, u32 ocr);
void mmc_set_initial_state(struct mmc_host *host);

static inline void mmc_delay(unsigned int ms)
{
	if (ms <= 20)
		usleep_range(ms * 1000, ms * 1250);
	else
		msleep(ms);
}

void mmc_rescan(struct work_struct *work);
void mmc_start_host(struct mmc_host *host);
void mmc_stop_host(struct mmc_host *host);

int _mmc_detect_card_removed(struct mmc_host *host);
int mmc_detect_card_removed(struct mmc_host *host);

int mmc_attach_mmc(struct mmc_host *host);
int mmc_attach_sd(struct mmc_host *host);
int mmc_attach_sdio(struct mmc_host *host);

/* Module parameters */
extern bool use_spi_crc;

/* Debugfs information for hosts and cards */
void mmc_add_host_debugfs(struct mmc_host *host);
void mmc_remove_host_debugfs(struct mmc_host *host);

void mmc_add_card_debugfs(struct mmc_card *card);
void mmc_remove_card_debugfs(struct mmc_card *card);

int mmc_execute_tuning(struct mmc_card *card);
int mmc_hs200_to_hs400(struct mmc_card *card);
int mmc_hs400_to_hs200(struct mmc_card *card);

#ifdef CONFIG_PM_SLEEP
void mmc_register_pm_notifier(struct mmc_host *host);
void mmc_unregister_pm_notifier(struct mmc_host *host);
#else
static inline void mmc_register_pm_notifier(struct mmc_host *host) { }
static inline void mmc_unregister_pm_notifier(struct mmc_host *host) { }
#endif

void mmc_wait_for_req_done(struct mmc_host *host, struct mmc_request *mrq);
bool mmc_is_req_done(struct mmc_host *host, struct mmc_request *mrq);

int mmc_start_request(struct mmc_host *host, struct mmc_request *mrq);

int mmc_erase(struct mmc_card *card, unsigned int from, unsigned int nr,
		unsigned int arg);
int mmc_can_erase(struct mmc_card *card);
int mmc_can_trim(struct mmc_card *card);
int mmc_can_discard(struct mmc_card *card);
int mmc_can_sanitize(struct mmc_card *card);
int mmc_can_secure_erase_trim(struct mmc_card *card);
int mmc_erase_group_aligned(struct mmc_card *card, unsigned int from,
			unsigned int nr);
unsigned int mmc_calc_max_discard(struct mmc_card *card);

int mmc_set_blocklen(struct mmc_card *card, unsigned int blocklen);
int mmc_set_blockcount(struct mmc_card *card, unsigned int blockcount,
			bool is_rel_write);

int __mmc_claim_host(struct mmc_host *host, struct mmc_ctx *ctx,
		     atomic_t *abort);
void mmc_release_host(struct mmc_host *host);
void mmc_get_card(struct mmc_card *card, struct mmc_ctx *ctx);
void mmc_put_card(struct mmc_card *card, struct mmc_ctx *ctx);

/**
 *	mmc_claim_host - exclusively claim a host
 *	@host: mmc host to claim
 *
 *	Claim a host for a set of operations.
 */
static inline void mmc_claim_host(struct mmc_host *host)
{
	__mmc_claim_host(host, NULL, NULL);
}

int mmc_cqe_start_req(struct mmc_host *host, struct mmc_request *mrq);
void mmc_cqe_post_req(struct mmc_host *host, struct mmc_request *mrq);
int mmc_cqe_recovery(struct mmc_host *host);

/**
 *	mmc_pre_req - Prepare for a new request
 *	@host: MMC host to prepare command
 *	@mrq: MMC request to prepare for
 *
 *	mmc_pre_req() is called in prior to mmc_start_req() to let
 *	host prepare for the new request. Preparation of a request may be
 *	performed while another request is running on the host.
 */
static inline void mmc_pre_req(struct mmc_host *host, struct mmc_request *mrq)
{
	if (host->ops->pre_req)
		host->ops->pre_req(host, mrq);
}

/**
 *	mmc_post_req - Post process a completed request
 *	@host: MMC host to post process command
 *	@mrq: MMC request to post process for
 *	@err: Error, if non zero, clean up any resources made in pre_req
 *
 *	Let the host post process a completed request. Post processing of
 *	a request may be performed while another request is running.
 */
static inline void mmc_post_req(struct mmc_host *host, struct mmc_request *mrq,
				int err)
{
	if (host->ops->post_req)
		host->ops->post_req(host, mrq, err);
}

#endif
