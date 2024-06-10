/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#ifndef FWVID_H_
#define FWVID_H_

#include "firmware.h"
#include "cfg80211.h"

struct brcmf_pub;
struct brcmf_if;

struct brcmf_fwvid_ops {
	void (*feat_attach)(struct brcmf_if *ifp);
	int (*set_sae_password)(struct brcmf_if *ifp, struct cfg80211_crypto_settings *crypto);
	int (*alloc_fweh_info)(struct brcmf_pub *drvr);
};

/* exported functions */
int brcmf_fwvid_register_vendor(enum brcmf_fwvendor fwvid, struct module *mod,
				const struct brcmf_fwvid_ops *ops);
int brcmf_fwvid_unregister_vendor(enum brcmf_fwvendor fwvid, struct module *mod);

/* core driver functions */
int brcmf_fwvid_attach(struct brcmf_pub *drvr);
void brcmf_fwvid_detach(struct brcmf_pub *drvr);
const char *brcmf_fwvid_vendor_name(struct brcmf_pub *drvr);

static inline void brcmf_fwvid_feat_attach(struct brcmf_if *ifp)
{
	const struct brcmf_fwvid_ops *vops = ifp->drvr->vops;

	if (!vops->feat_attach)
		return;

	vops->feat_attach(ifp);
}

static inline int brcmf_fwvid_set_sae_password(struct brcmf_if *ifp,
					       struct cfg80211_crypto_settings *crypto)
{
	const struct brcmf_fwvid_ops *vops = ifp->drvr->vops;

	if (!vops || !vops->set_sae_password)
		return -EOPNOTSUPP;

	return vops->set_sae_password(ifp, crypto);
}

static inline int brcmf_fwvid_alloc_fweh_info(struct brcmf_pub *drvr)
{
	if (!drvr->vops)
		return -EIO;

	return drvr->vops->alloc_fweh_info(drvr);
}

#endif /* FWVID_H_ */
