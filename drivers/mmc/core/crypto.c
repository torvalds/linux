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

	if (!req->crypt_keyslot)
		return;

	mrq->crypto_enabled = true;
	mrq->crypto_key_slot = blk_ksm_get_slot_idx(req->crypt_keyslot);

	/*
	 * For now we assume that all MMC drivers set max_dun_bytes_supported=4,
	 * which is the limit for CQHCI crypto.  So all DUNs should be 32-bit.
	 */
	WARN_ON_ONCE(req->crypt_ctx->bc_dun[0] > U32_MAX);

	mrq->data_unit_num = req->crypt_ctx->bc_dun[0];
}
EXPORT_SYMBOL_GPL(mmc_crypto_prepare_req);
