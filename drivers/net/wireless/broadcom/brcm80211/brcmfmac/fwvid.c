// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwvid.h"

#include "wcc/vops.h"
#include "cyw/vops.h"
#include "bca/vops.h"

struct brcmf_fwvid_entry {
	const char *name;
	const struct brcmf_fwvid_ops *vops;
	struct list_head drvr_list;
#if IS_MODULE(CONFIG_BRCMFMAC)
	struct module *vmod;
	struct completion reg_done;
#endif
};

static DEFINE_MUTEX(fwvid_list_lock);

#if IS_MODULE(CONFIG_BRCMFMAC)
#define FWVID_ENTRY_INIT(_vid, _name) \
	[BRCMF_FWVENDOR_ ## _vid] = { \
		.name = #_name, \
		.reg_done = COMPLETION_INITIALIZER(fwvid_list[BRCMF_FWVENDOR_ ## _vid].reg_done), \
		.drvr_list = LIST_HEAD_INIT(fwvid_list[BRCMF_FWVENDOR_ ## _vid].drvr_list), \
	}
#else
#define FWVID_ENTRY_INIT(_vid, _name) \
	[BRCMF_FWVENDOR_ ## _vid] = { \
		.name = #_name, \
		.drvr_list = LIST_HEAD_INIT(fwvid_list[BRCMF_FWVENDOR_ ## _vid].drvr_list), \
		.vops = _vid ## _VOPS \
	}
#endif /* IS_MODULE(CONFIG_BRCMFMAC) */

static struct brcmf_fwvid_entry fwvid_list[BRCMF_FWVENDOR_NUM] = {
	FWVID_ENTRY_INIT(WCC, wcc),
	FWVID_ENTRY_INIT(CYW, cyw),
	FWVID_ENTRY_INIT(BCA, bca),
};

#if IS_MODULE(CONFIG_BRCMFMAC)
static int brcmf_fwvid_request_module(enum brcmf_fwvendor fwvid)
{
	int ret;

	if (!fwvid_list[fwvid].vmod) {
		struct completion *reg_done = &fwvid_list[fwvid].reg_done;

		mutex_unlock(&fwvid_list_lock);

		ret = request_module("brcmfmac-%s", fwvid_list[fwvid].name);
		if (ret)
			goto fail;

		ret = wait_for_completion_interruptible(reg_done);
		if (ret)
			goto fail;

		mutex_lock(&fwvid_list_lock);
	}
	return 0;

fail:
	brcmf_err("mod=%s: failed %d\n", fwvid_list[fwvid].name, ret);
	return ret;
}

int brcmf_fwvid_register_vendor(enum brcmf_fwvendor fwvid, struct module *vmod,
				const struct brcmf_fwvid_ops *vops)
{
	if (fwvid >= BRCMF_FWVENDOR_NUM)
		return -ERANGE;

	if (WARN_ON(!vmod) || WARN_ON(!vops) ||
	    WARN_ON(!vops->attach) || WARN_ON(!vops->detach))
		return -EINVAL;

	if (WARN_ON(fwvid_list[fwvid].vmod))
		return -EEXIST;

	brcmf_dbg(TRACE, "mod=%s: enter\n", fwvid_list[fwvid].name);

	mutex_lock(&fwvid_list_lock);

	fwvid_list[fwvid].vmod = vmod;
	fwvid_list[fwvid].vops = vops;

	mutex_unlock(&fwvid_list_lock);

	complete_all(&fwvid_list[fwvid].reg_done);

	return 0;
}
BRCMF_EXPORT_SYMBOL_GPL(brcmf_fwvid_register_vendor);

int brcmf_fwvid_unregister_vendor(enum brcmf_fwvendor fwvid, struct module *mod)
{
	struct brcmf_bus *bus, *tmp;

	if (fwvid >= BRCMF_FWVENDOR_NUM)
		return -ERANGE;

	if (WARN_ON(fwvid_list[fwvid].vmod != mod))
		return -ENOENT;

	mutex_lock(&fwvid_list_lock);

	list_for_each_entry_safe(bus, tmp, &fwvid_list[fwvid].drvr_list, list) {
		mutex_unlock(&fwvid_list_lock);

		brcmf_dbg(INFO, "mod=%s: removing %s\n", fwvid_list[fwvid].name,
			  dev_name(bus->dev));
		brcmf_bus_remove(bus);

		mutex_lock(&fwvid_list_lock);
	}

	fwvid_list[fwvid].vmod = NULL;
	fwvid_list[fwvid].vops = NULL;
	reinit_completion(&fwvid_list[fwvid].reg_done);

	brcmf_dbg(TRACE, "mod=%s: exit\n", fwvid_list[fwvid].name);
	mutex_unlock(&fwvid_list_lock);

	return 0;
}
BRCMF_EXPORT_SYMBOL_GPL(brcmf_fwvid_unregister_vendor);
#else
static inline int brcmf_fwvid_request_module(enum brcmf_fwvendor fwvid)
{
	return 0;
}
#endif

int brcmf_fwvid_attach_ops(struct brcmf_pub *drvr)
{
	enum brcmf_fwvendor fwvid = drvr->bus_if->fwvid;
	int ret;

	if (fwvid >= ARRAY_SIZE(fwvid_list))
		return -ERANGE;

	brcmf_dbg(TRACE, "mod=%s: enter: dev %s\n", fwvid_list[fwvid].name,
		  dev_name(drvr->bus_if->dev));

	mutex_lock(&fwvid_list_lock);

	ret = brcmf_fwvid_request_module(fwvid);
	if (ret)
		return ret;

	drvr->vops = fwvid_list[fwvid].vops;
	list_add(&drvr->bus_if->list, &fwvid_list[fwvid].drvr_list);

	mutex_unlock(&fwvid_list_lock);

	return ret;
}

void brcmf_fwvid_detach_ops(struct brcmf_pub *drvr)
{
	enum brcmf_fwvendor fwvid = drvr->bus_if->fwvid;

	if (fwvid >= ARRAY_SIZE(fwvid_list))
		return;

	brcmf_dbg(TRACE, "mod=%s: enter: dev %s\n", fwvid_list[fwvid].name,
		  dev_name(drvr->bus_if->dev));

	mutex_lock(&fwvid_list_lock);

	drvr->vops = NULL;
	list_del(&drvr->bus_if->list);

	mutex_unlock(&fwvid_list_lock);
}

const char *brcmf_fwvid_vendor_name(struct brcmf_pub *drvr)
{
	return fwvid_list[drvr->bus_if->fwvid].name;
}
