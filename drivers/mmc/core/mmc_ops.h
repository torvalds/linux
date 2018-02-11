/*
 *  linux/drivers/mmc/core/mmc_ops.h
 *
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _MMC_MMC_OPS_H
#define _MMC_MMC_OPS_H

#include <linux/types.h>

struct mmc_host;
struct mmc_card;

int mmc_select_card(struct mmc_card *card);
int mmc_deselect_cards(struct mmc_host *host);
int mmc_set_dsr(struct mmc_host *host);
int mmc_go_idle(struct mmc_host *host);
int mmc_send_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr);
int mmc_set_relative_addr(struct mmc_card *card);
int mmc_send_csd(struct mmc_card *card, u32 *csd);
int __mmc_send_status(struct mmc_card *card, u32 *status, unsigned int retries);
int mmc_send_status(struct mmc_card *card, u32 *status);
int mmc_send_cid(struct mmc_host *host, u32 *cid);
int mmc_spi_read_ocr(struct mmc_host *host, int highcap, u32 *ocrp);
int mmc_spi_set_crc(struct mmc_host *host, int use_crc);
int mmc_bus_test(struct mmc_card *card, u8 bus_width);
int mmc_interrupt_hpi(struct mmc_card *card);
int mmc_can_ext_csd(struct mmc_card *card);
int mmc_get_ext_csd(struct mmc_card *card, u8 **new_ext_csd);
int mmc_switch_status(struct mmc_card *card);
int __mmc_switch_status(struct mmc_card *card, bool crc_err_fatal);
int __mmc_switch(struct mmc_card *card, u8 set, u8 index, u8 value,
		unsigned int timeout_ms, unsigned char timing,
		bool use_busy_signal, bool send_status,	bool retry_crc_err);
int mmc_switch(struct mmc_card *card, u8 set, u8 index, u8 value,
		unsigned int timeout_ms);
int mmc_stop_bkops(struct mmc_card *card);
void mmc_start_bkops(struct mmc_card *card, bool from_exception);
int mmc_flush_cache(struct mmc_card *card);
int mmc_cmdq_enable(struct mmc_card *card);
int mmc_cmdq_disable(struct mmc_card *card);

#endif

