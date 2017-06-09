/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/skbuff.h>
#include <linux/slab.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfp_app.h"
#include "nfp_main.h"

static const struct nfp_app_type *apps[] = {
	&app_nic,
	&app_bpf,
};

const char *nfp_app_mip_name(struct nfp_app *app)
{
	if (!app || !app->pf->mip)
		return "";
	return nfp_mip_name(app->pf->mip);
}

struct sk_buff *nfp_app_ctrl_msg_alloc(struct nfp_app *app, unsigned int size)
{
	struct sk_buff *skb;

	if (nfp_app_ctrl_has_meta(app))
		size += 8;

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return NULL;

	if (nfp_app_ctrl_has_meta(app))
		skb_reserve(skb, 8);

	return skb;
}

struct nfp_app *nfp_app_alloc(struct nfp_pf *pf, enum nfp_app_id id)
{
	struct nfp_app *app;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(apps); i++)
		if (apps[i]->id == id)
			break;
	if (i == ARRAY_SIZE(apps)) {
		nfp_err(pf->cpp, "failed to find app with ID 0x%02hhx\n", id);
		return ERR_PTR(-EINVAL);
	}

	if (WARN_ON(!apps[i]->name || !apps[i]->vnic_init))
		return ERR_PTR(-EINVAL);

	app = kzalloc(sizeof(*app), GFP_KERNEL);
	if (!app)
		return ERR_PTR(-ENOMEM);

	app->pf = pf;
	app->cpp = pf->cpp;
	app->pdev = pf->pdev;
	app->type = apps[i];

	return app;
}

void nfp_app_free(struct nfp_app *app)
{
	kfree(app);
}
