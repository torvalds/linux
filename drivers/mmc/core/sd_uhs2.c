// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Support for SD UHS-II cards
 */
#include <linux/err.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

#include "core.h"
#include "bus.h"
#include "sd.h"
#include "mmc_ops.h"

static const unsigned int sd_uhs2_freqs[] = { 52000000, 26000000 };

static int sd_uhs2_power_up(struct mmc_host *host)
{
	int err;

	if (host->ios.power_mode == MMC_POWER_ON)
		return 0;

	host->ios.vdd = fls(host->ocr_avail) - 1;
	host->ios.clock = host->f_init;
	host->ios.timing = MMC_TIMING_UHS2_SPEED_A;
	host->ios.power_mode = MMC_POWER_ON;

	err = host->ops->uhs2_control(host, UHS2_SET_IOS);

	return err;
}

static int sd_uhs2_power_off(struct mmc_host *host)
{
	if (host->ios.power_mode == MMC_POWER_OFF)
		return 0;

	host->ios.vdd = 0;
	host->ios.clock = 0;
	host->ios.timing = MMC_TIMING_LEGACY;
	host->ios.power_mode = MMC_POWER_OFF;

	return host->ops->uhs2_control(host, UHS2_SET_IOS);
}

/*
 * Run the phy initialization sequence, which mainly relies on the UHS-II host
 * to check that we reach the expected electrical state, between the host and
 * the card.
 */
static int sd_uhs2_phy_init(struct mmc_host *host)
{
	return 0;
}

/*
 * Do the early initialization of the card, by sending the device init broadcast
 * command and wait for the process to be completed.
 */
static int sd_uhs2_dev_init(struct mmc_host *host)
{
	return 0;
}

/*
 * Run the enumeration process by sending the enumerate command to the card.
 * Note that, we currently support only the point to point connection, which
 * means only one card can be attached per host/slot.
 */
static int sd_uhs2_enum(struct mmc_host *host, u32 *node_id)
{
	return 0;
}

/*
 * Read the UHS-II configuration registers (CFG_REG) of the card, by sending it
 * commands and by parsing the responses. Store a copy of the relevant data in
 * card->uhs2_config.
 */
static int sd_uhs2_config_read(struct mmc_host *host, struct mmc_card *card)
{
	return 0;
}

/*
 * Based on the card's and host's UHS-II capabilities, let's update the
 * configuration of the card and the host. This may also include to move to a
 * greater speed range/mode. Depending on the updated configuration, we may need
 * to do a soft reset of the card via sending it a GO_DORMANT_STATE command.
 *
 * In the final step, let's check if the card signals "config completion", which
 * indicates that the card has moved from config state into active state.
 */
static int sd_uhs2_config_write(struct mmc_host *host, struct mmc_card *card)
{
	return 0;
}

/*
 * Initialize the UHS-II card through the SD-TRAN transport layer. This enables
 * commands/requests to be backwards compatible through the legacy SD protocol.
 * UHS-II cards has a specific power limit specified for VDD1/VDD2, that should
 * be set through a legacy CMD6. Note that, the power limit that becomes set,
 * survives a soft reset through the GO_DORMANT_STATE command.
 */
static int sd_uhs2_legacy_init(struct mmc_host *host, struct mmc_card *card)
{
	return 0;
}

/*
 * Allocate the data structure for the mmc_card and run the UHS-II specific
 * initialization sequence.
 */
static int sd_uhs2_init_card(struct mmc_host *host)
{
	struct mmc_card *card;
	u32 node_id = 0;
	int err;

	err = sd_uhs2_dev_init(host);
	if (err)
		return err;

	err = sd_uhs2_enum(host, &node_id);
	if (err)
		return err;

	card = mmc_alloc_card(host, &sd_type);
	if (IS_ERR(card))
		return PTR_ERR(card);

	card->uhs2_config.node_id = node_id;
	card->type = MMC_TYPE_SD;

	err = sd_uhs2_config_read(host, card);
	if (err)
		goto err;

	err = sd_uhs2_config_write(host, card);
	if (err)
		goto err;

	host->card = card;
	return 0;

err:
	mmc_remove_card(card);
	return err;
}

static void sd_uhs2_remove(struct mmc_host *host)
{
	mmc_remove_card(host->card);
	host->card = NULL;
}

static int sd_uhs2_alive(struct mmc_host *host)
{
	return mmc_send_status(host->card, NULL);
}

static void sd_uhs2_detect(struct mmc_host *host)
{
	int err;

	mmc_get_card(host->card, NULL);
	err = _mmc_detect_card_removed(host);
	mmc_put_card(host->card, NULL);

	if (err) {
		sd_uhs2_remove(host);

		mmc_claim_host(host);
		mmc_detach_bus(host);
		sd_uhs2_power_off(host);
		mmc_release_host(host);
	}
}

static int sd_uhs2_suspend(struct mmc_host *host)
{
	return 0;
}

static int sd_uhs2_resume(struct mmc_host *host)
{
	return 0;
}

static int sd_uhs2_runtime_suspend(struct mmc_host *host)
{
	return 0;
}

static int sd_uhs2_runtime_resume(struct mmc_host *host)
{
	return 0;
}

static int sd_uhs2_shutdown(struct mmc_host *host)
{
	return 0;
}

static int sd_uhs2_hw_reset(struct mmc_host *host)
{
	return 0;
}

static const struct mmc_bus_ops sd_uhs2_ops = {
	.remove = sd_uhs2_remove,
	.alive = sd_uhs2_alive,
	.detect = sd_uhs2_detect,
	.suspend = sd_uhs2_suspend,
	.resume = sd_uhs2_resume,
	.runtime_suspend = sd_uhs2_runtime_suspend,
	.runtime_resume = sd_uhs2_runtime_resume,
	.shutdown = sd_uhs2_shutdown,
	.hw_reset = sd_uhs2_hw_reset,
};

static int sd_uhs2_attach(struct mmc_host *host)
{
	int err;

	err = sd_uhs2_power_up(host);
	if (err)
		goto err;

	err = sd_uhs2_phy_init(host);
	if (err)
		goto err;

	err = sd_uhs2_init_card(host);
	if (err)
		goto err;

	err = sd_uhs2_legacy_init(host, host->card);
	if (err)
		goto err;

	mmc_attach_bus(host, &sd_uhs2_ops);

	mmc_release_host(host);

	err = mmc_add_card(host->card);
	if (err)
		goto remove_card;

	mmc_claim_host(host);
	return 0;

remove_card:
	mmc_remove_card(host->card);
	host->card = NULL;
	mmc_claim_host(host);
	mmc_detach_bus(host);
err:
	sd_uhs2_power_off(host);
	return err;
}

int mmc_attach_sd_uhs2(struct mmc_host *host)
{
	int i, err = 0;

	if (!(host->caps2 & MMC_CAP2_SD_UHS2))
		return -EOPNOTSUPP;

	/* Turn off the legacy SD interface before trying with UHS-II. */
	mmc_power_off(host);

	/*
	 * Start UHS-II initialization at 52MHz and possibly make a retry at
	 * 26MHz according to the spec. It's required that the host driver
	 * validates ios->clock, to set a rate within the correct range.
	 */
	for (i = 0; i < ARRAY_SIZE(sd_uhs2_freqs); i++) {
		host->f_init = sd_uhs2_freqs[i];
		pr_debug("%s: %s: trying to init UHS-II card at %u Hz\n",
			 mmc_hostname(host), __func__, host->f_init);
		err = sd_uhs2_attach(host);
		if (!err)
			break;
	}

	return err;
}
