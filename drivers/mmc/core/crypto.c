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
		blk_ksm_reprogram_all_keys(&host->ksm);
}

void mmc_crypto_setup_queue(struct request_queue *q, struct mmc_host *host)
{
	if (host->caps2 & MMC_CAP2_CRYPTO)
		blk_ksm_register(&host->ksm, q);
}
EXPORT_SYMBOL_GPL(mmc_crypto_setup_queue);

void mmc_crypto_prepare_req(struct mmc_queue_req *mqrq)
{
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct mmc_request *mrq = &mqrq->brq.mrq;

	if (!req->crypt_ctx)
		return;

	mrq->crypto_ctx = req->crypt_ctx;
	if (req->crypt_keyslot)
		mrq->crypto_key_slot = blk_ksm_get_slot_idx(req->crypt_keyslot);
}
EXPORT_SYMBOL_GPL(mmc_crypto_prepare_req);
