/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/mmc/core/core.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
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
	int (*alive)(struct mmc_host *);
	int (*shutdown)(struct mmc_host *);
	int (*hw_reset)(struct mmc_host *);
	int (*sw_reset)(struct mmc_host *);
	bool (*cache_enabled)(struct mmc_host *);
	int (*flush_cache)(struct mmc_host *);
	int (*handle_undervoltage)(struct mmc_host *host);
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
u32 mmc_vddrange_to_ocrmask(int vdd_min, int vdd_max);
int mmc_handle_undervoltage(struct mmc_host *host);
void mmc_regulator_register_undervoltage_notifier(struct mmc_host *host);
void mmc_regulator_unregister_undervoltage_notifier(struct mmc_host *host);
void mmc_undervoltage_workfn(struct work_struct *work);

static inline void mmc_delay(unsigned int ms)
{
	if (ms <= 20)
		usleep_range(ms * 1000, ms * 1250);
	else
		msleep(ms);
}

void mmc_rescan(struct work_struct *work);
void mmc_start_host(struct mmc_host *host);
void __mmc_stop_host(struct mmc_host *host);
void mmc_stop_host(struct mmc_host *host);

void _mmc_detect_change(struct mmc_host *host, unsigned long delay,
			bool cd_irq);
int _mmc_detect_card_removed(struct mmc_host *host);
int mmc_detect_card_removed(struct mmc_host *host);

int mmc_attach_mmc(struct mmc_host *host);
int mmc_attach_sd(struct mmc_host *host);
int mmc_attach_sdio(struct mmc_host *host);
int mmc_attach_sd_uhs2(struct mmc_host *host);

/* Module parameters */
extern bool use_spi_crc;

/* Debugfs information for hosts and cards */
#ifdef CONFIG_DEBUG_FS
void mmc_add_host_debugfs(struct mmc_host *host);
void mmc_remove_host_debugfs(struct mmc_host *host);

void mmc_add_card_debugfs(struct mmc_card *card);
void mmc_remove_card_debugfs(struct mmc_card *card);
#else
static inline void mmc_add_host_debugfs(struct mmc_host *host)
{
}
static inline void mmc_remove_host_debugfs(struct mmc_host *host)
{
}
static inline void mmc_add_card_debugfs(struct mmc_card *card)
{
}
static inline void mmc_remove_card_debugfs(struct mmc_card *card)
{
}
#endif

int mmc_execute_tuning(struct mmc_card *card);
int mmc_hs200_to_hs400(struct mmc_card *card);
int mmc_hs400_to_hs200(struct mmc_card *card);

void mmc_wait_for_req_done(struct mmc_host *host, struct mmc_request *mrq);
bool mmc_is_req_done(struct mmc_host *host, struct mmc_request *mrq);

int mmc_start_request(struct mmc_host *host, struct mmc_request *mrq);

int mmc_erase(struct mmc_card *card, sector_t from, unsigned int nr, unsigned int arg);
bool mmc_card_can_erase(struct mmc_card *card);
bool mmc_card_can_trim(struct mmc_card *card);
bool mmc_card_can_discard(struct mmc_card *card);
bool mmc_card_can_sanitize(struct mmc_card *card);
bool mmc_card_can_secure_erase_trim(struct mmc_card *card);
bool mmc_card_can_cmd23(struct mmc_card *card);
int mmc_erase_group_aligned(struct mmc_card *card, sector_t from, unsigned int nr);
unsigned int mmc_calc_max_discard(struct mmc_card *card);

int mmc_set_blocklen(struct mmc_card *card, unsigned int blocklen);

int __mmc_claim_host(struct mmc_host *host, struct mmc_ctx *ctx,
		     atomic_t *abort);
void mmc_release_host(struct mmc_host *host);
void mmc_get_card(struct mmc_card *card, struct mmc_ctx *ctx);
void mmc_put_card(struct mmc_card *card, struct mmc_ctx *ctx);

int mmc_card_alternative_gpt_sector(struct mmc_card *card, sector_t *sector);

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

static inline bool mmc_cache_enabled(struct mmc_host *host)
{
	if (host->bus_ops->cache_enabled)
		return host->bus_ops->cache_enabled(host);

	return false;
}

static inline int mmc_flush_cache(struct mmc_host *host)
{
	if (host->bus_ops->flush_cache)
		return host->bus_ops->flush_cache(host);

	return 0;
}

static inline unsigned int mmc_sector_div(sector_t dividend, u32 divisor)
{
	return div_u64(dividend, divisor);
}

static inline unsigned int mmc_sector_mod(sector_t dividend, u32 divisor)
{
	return sector_div(dividend, divisor);
}

#endif
