/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#ifndef FWVID_H_
#define FWVID_H_

#include "firmware.h"

struct brcmf_pub;

struct brcmf_fwvid_ops {
	int (*attach)(struct brcmf_pub *drvr);
	void (*detach)(struct brcmf_pub *drvr);
};

/* exported functions */
int brcmf_fwvid_register_vendor(enum brcmf_fwvendor fwvid, struct module *mod,
				const struct brcmf_fwvid_ops *ops);
int brcmf_fwvid_unregister_vendor(enum brcmf_fwvendor fwvid, struct module *mod);

/* core driver functions */
int brcmf_fwvid_attach_ops(struct brcmf_pub *drvr);
void brcmf_fwvid_detach_ops(struct brcmf_pub *drvr);
const char *brcmf_fwvid_vendor_name(struct brcmf_pub *drvr);

static inline int brcmf_fwvid_attach(struct brcmf_pub *drvr)
{
	int ret;

	ret = brcmf_fwvid_attach_ops(drvr);
	if (ret)
		return ret;

	return drvr->vops->attach(drvr);
}

static inline void brcmf_fwvid_detach(struct brcmf_pub *drvr)
{
	if (!drvr->vops)
		return;

	drvr->vops->detach(drvr);
	brcmf_fwvid_detach_ops(drvr);
}

#endif /* FWVID_H_ */
