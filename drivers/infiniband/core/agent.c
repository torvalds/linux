/*
 * Copyright (c) 2004, 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004, 2005 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: agent.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <linux/slab.h>
#include <linux/string.h>

#include "agent.h"
#include "smi.h"

#define SPFX "ib_agent: "

struct ib_agent_port_private {
	struct list_head port_list;
	struct ib_mad_agent *agent[2];
};

static DEFINE_SPINLOCK(ib_agent_port_list_lock);
static LIST_HEAD(ib_agent_port_list);

static struct ib_agent_port_private *
__ib_get_agent_port(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *entry;

	list_for_each_entry(entry, &ib_agent_port_list, port_list) {
		if (entry->agent[0]->device == device &&
		    entry->agent[0]->port_num == port_num)
			return entry;
	}
	return NULL;
}

static struct ib_agent_port_private *
ib_get_agent_port(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *entry;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	entry = __ib_get_agent_port(device, port_num);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);
	return entry;
}

int agent_send_response(struct ib_mad *mad, struct ib_grh *grh,
			struct ib_wc *wc, struct ib_device *device,
			int port_num, int qpn)
{
	struct ib_agent_port_private *port_priv;
	struct ib_mad_agent *agent;
	struct ib_mad_send_buf *send_buf;
	struct ib_ah *ah;
	int ret;

	port_priv = ib_get_agent_port(device, port_num);
	if (!port_priv) {
		printk(KERN_ERR SPFX "Unable to find port agent\n");
		return -ENODEV;
	}

	agent = port_priv->agent[qpn];
	ah = ib_create_ah_from_wc(agent->qp->pd, wc, grh, port_num);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		printk(KERN_ERR SPFX "ib_create_ah_from_wc error:%d\n", ret);
		return ret;
	}

	send_buf = ib_create_send_mad(agent, wc->src_qp, wc->pkey_index, 0,
				      IB_MGMT_MAD_HDR, IB_MGMT_MAD_DATA,
				      GFP_KERNEL);
	if (IS_ERR(send_buf)) {
		ret = PTR_ERR(send_buf);
		printk(KERN_ERR SPFX "ib_create_send_mad error:%d\n", ret);
		goto err1;
	}

	memcpy(send_buf->mad, mad, sizeof *mad);
	send_buf->ah = ah;
	if ((ret = ib_post_send_mad(send_buf, NULL))) {
		printk(KERN_ERR SPFX "ib_post_send_mad error:%d\n", ret);
		goto err2;
	}
	return 0;
err2:
	ib_free_send_mad(send_buf);
err1:
	ib_destroy_ah(ah);
	return ret;
}

static void agent_send_handler(struct ib_mad_agent *mad_agent,
			       struct ib_mad_send_wc *mad_send_wc)
{
	ib_destroy_ah(mad_send_wc->send_buf->ah);
	ib_free_send_mad(mad_send_wc->send_buf);
}

int ib_agent_port_open(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *port_priv;
	unsigned long flags;
	int ret;

	/* Create new device info */
	port_priv = kzalloc(sizeof *port_priv, GFP_KERNEL);
	if (!port_priv) {
		printk(KERN_ERR SPFX "No memory for ib_agent_port_private\n");
		ret = -ENOMEM;
		goto error1;
	}

	/* Obtain send only MAD agent for SMI QP */
	port_priv->agent[0] = ib_register_mad_agent(device, port_num,
						    IB_QPT_SMI, NULL, 0,
						    &agent_send_handler,
						    NULL, NULL);
	if (IS_ERR(port_priv->agent[0])) {
		ret = PTR_ERR(port_priv->agent[0]);
		goto error2;
	}

	/* Obtain send only MAD agent for GSI QP */
	port_priv->agent[1] = ib_register_mad_agent(device, port_num,
						    IB_QPT_GSI, NULL, 0,
						    &agent_send_handler,
						    NULL, NULL);
	if (IS_ERR(port_priv->agent[1])) {
		ret = PTR_ERR(port_priv->agent[1]);
		goto error3;
	}

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	list_add_tail(&port_priv->port_list, &ib_agent_port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	return 0;

error3:
	ib_unregister_mad_agent(port_priv->agent[0]);
error2:
	kfree(port_priv);
error1:
	return ret;
}

int ib_agent_port_close(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *port_priv;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	port_priv = __ib_get_agent_port(device, port_num);
	if (port_priv == NULL) {
		spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);
		printk(KERN_ERR SPFX "Port %d not found\n", port_num);
		return -ENODEV;
	}
	list_del(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	ib_unregister_mad_agent(port_priv->agent[1]);
	ib_unregister_mad_agent(port_priv->agent[0]);
	kfree(port_priv);
	return 0;
}
