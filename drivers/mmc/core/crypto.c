// SPDX-License-Identifier: GPL-2.0-only
/*
 * MMC crypto engine (inline encryption) support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/blk-crypto.h>
#include <linux/mmc/host.h>

#include "core.h"
#include "crypto.h"
#include "queue.h"

void mmc_crypto_set_initial_state(struct mmc_host *host)
{
	/* Reset might clear all keys, so reprogram all the keys. */
	if (host->caps2 & MMC_CAP2_CRYPTO)
		blk_crypto_reprogram_all_keys(&host->crypto_profile);
}

void mmc_crypto_setup_queue(struct request_queue *q, struct mmc_host *host)
{
	if (host->caps2 & MMC_CAP2_CRYPTO)
		blk_crypto_register(&host->crypto_profile, q);
}
EXPORT_SYMBOL_GPL(mmc_crypto_setup_queue);

void mmc_crypto_prepare_req(struct mmc_queue_req *mqrq)
{
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct blk_crypto_keyslot *keyslot;

	if (!req->crypt_ctx)
		return;

	mrq->crypto_ctx = req->crypt_ctx;

	keyslot = req->crypt_keyslot;
	if (keyslot)
		mrq->crypto_key_slot = blk_crypto_keyslot_index(keyslot);
}
EXPORT_SYMBOL_GPL(mmc_crypto_prepare_req);
