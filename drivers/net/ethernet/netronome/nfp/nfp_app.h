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

#include <net/devlink.h>

#include "nfp_net_repr.h"

struct bpf_prog;
struct net_device;
struct pci_dev;
struct sk_buff;
struct sk_buff;
struct nfp_app;
struct nfp_cpp;
struct nfp_pf;
struct nfp_repr;
struct nfp_net;

enum nfp_app_id {
	NFP_APP_CORE_NIC	= 0x1,
	NFP_APP_BPF_NIC		= 0x2,
	NFP_APP_FLOWER_NIC	= 0x3,
};

extern const struct nfp_app_type app_nic;
extern const struct nfp_app_type app_bpf;
extern const struct nfp_app_type app_flower;

/**
 * struct nfp_app_type - application definition
 * @id:		application ID
 * @name:	application name
 * @ctrl_has_meta:  control messages have prepend of type:5/port:CTRL
 *
 * Callbacks
 * @init:	perform basic app checks and init
 * @clean:	clean app state
 * @extra_cap:	extra capabilities string
 * @vnic_alloc:	allocate vNICs (assign port types, etc.)
 * @vnic_free:	free up app's vNIC state
 * @vnic_init:	vNIC netdev was registered
 * @vnic_clean:	vNIC netdev about to be unregistered
 * @repr_open:	representor netdev open callback
 * @repr_stop:	representor netdev stop callback
 * @start:	start application logic
 * @stop:	stop application logic
 * @ctrl_msg_rx:    control message handler
 * @setup_tc:	setup TC ndo
 * @tc_busy:	TC HW offload busy (rules loaded)
 * @xdp_offload:    offload an XDP program
 * @eswitch_mode_get:    get SR-IOV eswitch mode
 * @sriov_enable: app-specific sriov initialisation
 * @sriov_disable: app-specific sriov clean-up
 * @repr_get:	get representor netdev
 */
struct nfp_app_type {
	enum nfp_app_id id;
	const char *name;

	bool ctrl_has_meta;

	int (*init)(struct nfp_app *app);
	void (*clean)(struct nfp_app *app);

	const char *(*extra_cap)(struct nfp_app *app, struct nfp_net *nn);

	int (*vnic_alloc)(struct nfp_app *app, struct nfp_net *nn,
			  unsigned int id);
	void (*vnic_free)(struct nfp_app *app, struct nfp_net *nn);
	int (*vnic_init)(struct nfp_app *app, struct nfp_net *nn);
	void (*vnic_clean)(struct nfp_app *app, struct nfp_net *nn);

	int (*repr_open)(struct nfp_app *app, struct nfp_repr *repr);
	int (*repr_stop)(struct nfp_app *app, struct nfp_repr *repr);

	int (*start)(struct nfp_app *app);
	void (*stop)(struct nfp_app *app);

	void (*ctrl_msg_rx)(struct nfp_app *app, struct sk_buff *skb);

	int (*setup_tc)(struct nfp_app *app, struct net_device *netdev,
			enum tc_setup_type type, void *type_data);
	bool (*tc_busy)(struct nfp_app *app, struct nfp_net *nn);
	int (*xdp_offload)(struct nfp_app *app, struct nfp_net *nn,
			   struct bpf_prog *prog);

	int (*sriov_enable)(struct nfp_app *app, int num_vfs);
	void (*sriov_disable)(struct nfp_app *app);

	enum devlink_eswitch_mode (*eswitch_mode_get)(struct nfp_app *app);
	struct net_device *(*repr_get)(struct nfp_app *app, u32 id);
};

/**
 * struct nfp_app - NFP application container
 * @pdev:	backpointer to PCI device
 * @pf:		backpointer to NFP PF structure
 * @cpp:	pointer to the CPP handle
 * @ctrl:	pointer to ctrl vNIC struct
 * @reprs:	array of pointers to representors
 * @type:	pointer to const application ops and info
 * @priv:	app-specific priv data
 */
struct nfp_app {
	struct pci_dev *pdev;
	struct nfp_pf *pf;
	struct nfp_cpp *cpp;

	struct nfp_net *ctrl;
	struct nfp_reprs __rcu *reprs[NFP_REPR_TYPE_MAX + 1];

	const struct nfp_app_type *type;
	void *priv;
};

bool nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb);

static inline int nfp_app_init(struct nfp_app *app)
{
	if (!app->type->init)
		return 0;
	return app->type->init(app);
}

static inline void nfp_app_clean(struct nfp_app *app)
{
	if (app->type->clean)
		app->type->clean(app);
}

static inline int nfp_app_vnic_alloc(struct nfp_app *app, struct nfp_net *nn,
				     unsigned int id)
{
	return app->type->vnic_alloc(app, nn, id);
}

static inline void nfp_app_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	if (app->type->vnic_free)
		app->type->vnic_free(app, nn);
}

static inline int nfp_app_vnic_init(struct nfp_app *app, struct nfp_net *nn)
{
	if (!app->type->vnic_init)
		return 0;
	return app->type->vnic_init(app, nn);
}

static inline void nfp_app_vnic_clean(struct nfp_app *app, struct nfp_net *nn)
{
	if (app->type->vnic_clean)
		app->type->vnic_clean(app, nn);
}

static inline int nfp_app_repr_open(struct nfp_app *app, struct nfp_repr *repr)
{
	if (!app->type->repr_open)
		return -EINVAL;
	return app->type->repr_open(app, repr);
}

static inline int nfp_app_repr_stop(struct nfp_app *app, struct nfp_repr *repr)
{
	if (!app->type->repr_stop)
		return -EINVAL;
	return app->type->repr_stop(app, repr);
}

static inline int nfp_app_start(struct nfp_app *app, struct nfp_net *ctrl)
{
	app->ctrl = ctrl;
	if (!app->type->start)
		return 0;
	return app->type->start(app);
}

static inline void nfp_app_stop(struct nfp_app *app)
{
	if (!app->type->stop)
		return;
	app->type->stop(app);
}

static inline const char *nfp_app_name(struct nfp_app *app)
{
	if (!app)
		return "";
	return app->type->name;
}

static inline bool nfp_app_needs_ctrl_vnic(struct nfp_app *app)
{
	return app && app->type->ctrl_msg_rx;
}

static inline bool nfp_app_ctrl_has_meta(struct nfp_app *app)
{
	return app->type->ctrl_has_meta;
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
				   enum tc_setup_type type, void *type_data)
{
	if (!app || !app->type->setup_tc)
		return -EOPNOTSUPP;
	return app->type->setup_tc(app, netdev, type, type_data);
}

static inline int nfp_app_xdp_offload(struct nfp_app *app, struct nfp_net *nn,
				      struct bpf_prog *prog)
{
	if (!app || !app->type->xdp_offload)
		return -EOPNOTSUPP;
	return app->type->xdp_offload(app, nn, prog);
}

static inline bool nfp_app_ctrl_tx(struct nfp_app *app, struct sk_buff *skb)
{
	return nfp_ctrl_tx(app->ctrl, skb);
}

static inline void nfp_app_ctrl_rx(struct nfp_app *app, struct sk_buff *skb)
{
	app->type->ctrl_msg_rx(app, skb);
}

static inline int nfp_app_eswitch_mode_get(struct nfp_app *app, u16 *mode)
{
	if (!app->type->eswitch_mode_get)
		return -EOPNOTSUPP;

	*mode = app->type->eswitch_mode_get(app);

	return 0;
}

static inline int nfp_app_sriov_enable(struct nfp_app *app, int num_vfs)
{
	if (!app || !app->type->sriov_enable)
		return -EOPNOTSUPP;
	return app->type->sriov_enable(app, num_vfs);
}

static inline void nfp_app_sriov_disable(struct nfp_app *app)
{
	if (app && app->type->sriov_disable)
		app->type->sriov_disable(app);
}

static inline struct net_device *nfp_app_repr_get(struct nfp_app *app, u32 id)
{
	if (unlikely(!app || !app->type->repr_get))
		return NULL;

	return app->type->repr_get(app, id);
}

struct nfp_app *nfp_app_from_netdev(struct net_device *netdev);

struct nfp_reprs *
nfp_app_reprs_set(struct nfp_app *app, enum nfp_repr_type type,
		  struct nfp_reprs *reprs);

const char *nfp_app_mip_name(struct nfp_app *app);
struct sk_buff *
nfp_app_ctrl_msg_alloc(struct nfp_app *app, unsigned int size, gfp_t priority);

struct nfp_app *nfp_app_alloc(struct nfp_pf *pf, enum nfp_app_id id);
void nfp_app_free(struct nfp_app *app);

/* Callbacks shared between apps */

int nfp_app_nic_vnic_alloc(struct nfp_app *app, struct nfp_net *nn,
			   unsigned int id);

#endif
