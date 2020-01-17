// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Move OProfile dependencies from spufs module to the kernel so it
 * can run on yesn-cell PPC.
 *
 * Copyright (C) IBM 2005
 */

#undef DEBUG

#include <linux/export.h>
#include <linux/yestifier.h>
#include <asm/spu.h>
#include "spufs/spufs.h"

static BLOCKING_NOTIFIER_HEAD(spu_switch_yestifier);

void spu_switch_yestify(struct spu *spu, struct spu_context *ctx)
{
	blocking_yestifier_call_chain(&spu_switch_yestifier,
				     ctx ? ctx->object_id : 0, spu);
}
EXPORT_SYMBOL_GPL(spu_switch_yestify);

int spu_switch_event_register(struct yestifier_block *n)
{
	int ret;
	ret = blocking_yestifier_chain_register(&spu_switch_yestifier, n);
	if (!ret)
		yestify_spus_active();
	return ret;
}
EXPORT_SYMBOL_GPL(spu_switch_event_register);

int spu_switch_event_unregister(struct yestifier_block *n)
{
	return blocking_yestifier_chain_unregister(&spu_switch_yestifier, n);
}
EXPORT_SYMBOL_GPL(spu_switch_event_unregister);

void spu_set_profile_private_kref(struct spu_context *ctx,
				  struct kref *prof_info_kref,
				  void (* prof_info_release) (struct kref *kref))
{
	ctx->prof_priv_kref = prof_info_kref;
	ctx->prof_priv_release = prof_info_release;
}
EXPORT_SYMBOL_GPL(spu_set_profile_private_kref);

void *spu_get_profile_private_kref(struct spu_context *ctx)
{
	return ctx->prof_priv_kref;
}
EXPORT_SYMBOL_GPL(spu_get_profile_private_kref);

