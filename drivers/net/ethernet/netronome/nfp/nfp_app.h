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

#ifndef _NFP_APP_H
#define _NFP_APP_H 1

struct bpf_prog;
struct net_device;
struct pci_dev;
struct tc_to_netdev;
struct nfp_app;
struct nfp_cpp;
struct nfp_pf;
struct nfp_net;

enum nfp_app_id {
	NFP_APP_CORE_NIC	= 0x1,
	NFP_APP_BPF_NIC		= 0x2,
};

extern const struct nfp_app_type app_nic;
extern const struct nfp_app_type app_bpf;

/**
 * struct nfp_app_type - application definition
 * @id:		application ID
 * @name:	application name
 *
 * Callbacks
 * @init:	perform basic app checks
 * @extra_cap:	extra capabilities string
 * @vnic_init:	init vNICs (assign port types, etc.)
 * @vnic_clean:	clean up app's vNIC state
 * @setup_tc:	setup TC ndo
 * @tc_busy:	TC HW offload busy (rules loaded)
 * @xdp_offload:    offload an XDP program
 */
struct nfp_app_type {
	enum nfp_app_id id;
	const char *name;

	int (*init)(struct nfp_app *app);

	const char *(*extra_cap)(struct nfp_app *app, struct nfp_net *nn);

	int (*vnic_init)(struct nfp_app *app, struct nfp_net *nn,
			 unsigned int id);
	void (*vnic_clean)(struct nfp_app *app, struct nfp_net *nn);

	int (*setup_tc)(struct nfp_app *app, struct net_device *netdev,
			u32 handle, __be16 proto, struct tc_to_netdev *tc);
	bool (*tc_busy)(struct nfp_app *app, struct nfp_net *nn);
	int (*xdp_offload)(struct nfp_app *app, struct nfp_net *nn,
			   struct bpf_prog *prog);
};

/**
 * struct nfp_app - NFP application container
 * @pdev:	backpointer to PCI device
 * @pf:		backpointer to NFP PF structure
 * @cpp:	pointer to the CPP handle
 * @type:	pointer to const application ops and info
 */
struct nfp_app {
	struct pci_dev *pdev;
	struct nfp_pf *pf;
	struct nfp_cpp *cpp;

	const struct nfp_app_type *type;
};

static inline int nfp_app_init(struct nfp_app *app)
{
	if (!app->type->init)
		return 0;
	return app->type->init(app);
}

static inline int nfp_app_vnic_init(struct nfp_app *app, struct nfp_net *nn,
				    unsigned int id)
{
	return app->type->vnic_init(app, nn, id);
}

static inline void nfp_app_vnic_clean(struct nfp_app *app, struct nfp_net *nn)
{
	if (app->type->vnic_clean)
		app->type->vnic_clean(app, nn);
}

static inline const char *nfp_app_name(struct nfp_app *app)
{
	if (!app)
		return "";
	return app->type->name;
}

static inline const char *nfp_app_extra_cap(struct nfp_app *app,
					    struct nfp_net *nn)
{
	if (!app || !app->type->extra_cap)
		return "";
	return app->type->extra_cap(app, nn);
}

static inline bool nfp_app_has_tc(struct nfp_app *app)
{
	return app && app->type->setup_tc;
}

static inline bool nfp_app_tc_busy(struct nfp_app *app, struct nfp_net *nn)
{
	if (!app || !app->type->tc_busy)
		return false;
	return app->type->tc_busy(app, nn);
}

static inline int nfp_app_setup_tc(struct nfp_app *app,
				   struct net_device *netdev,
				   u32 handle, __be16 proto,
				   struct tc_to_netdev *tc)
{
	if (!app || !app->type->setup_tc)
		return -EOPNOTSUPP;
	return app->type->setup_tc(app, netdev, handle, proto, tc);
}

static inline int nfp_app_xdp_offload(struct nfp_app *app, struct nfp_net *nn,
				      struct bpf_prog *prog)
{
	if (!app || !app->type->xdp_offload)
		return -EOPNOTSUPP;
	return app->type->xdp_offload(app, nn, prog);
}

struct nfp_app *nfp_app_alloc(struct nfp_pf *pf, enum nfp_app_id id);
void nfp_app_free(struct nfp_app *app);

/* Callbacks shared between apps */

int nfp_app_nic_vnic_init(struct nfp_app *app, struct nfp_net *nn,
			  unsigned int id);

#endif
