/*
 * Copyright (c) 2016 Mellanox Technologies Ltd.  All rights reserved.
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
 */

#include <linux/security.h>
#include <linux/completion.h>
#include <linux/list.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>
#include "core_priv.h"
#include "mad_priv.h"

static struct pkey_index_qp_list *get_pkey_idx_qp_list(struct ib_port_pkey *pp)
{
	struct pkey_index_qp_list *pkey = NULL;
	struct pkey_index_qp_list *tmp_pkey;
	struct ib_device *dev = pp->sec->dev;

	spin_lock(&dev->port_pkey_list[pp->port_num].list_lock);
	list_for_each_entry(tmp_pkey,
			    &dev->port_pkey_list[pp->port_num].pkey_list,
			    pkey_index_list) {
		if (tmp_pkey->pkey_index == pp->pkey_index) {
			pkey = tmp_pkey;
			break;
		}
	}
	spin_unlock(&dev->port_pkey_list[pp->port_num].list_lock);
	return pkey;
}

static int get_pkey_and_subnet_prefix(struct ib_port_pkey *pp,
				      u16 *pkey,
				      u64 *subnet_prefix)
{
	struct ib_device *dev = pp->sec->dev;
	int ret;

	ret = ib_get_cached_pkey(dev, pp->port_num, pp->pkey_index, pkey);
	if (ret)
		return ret;

	ret = ib_get_cached_subnet_prefix(dev, pp->port_num, subnet_prefix);

	return ret;
}

static int enforce_qp_pkey_security(u16 pkey,
				    u64 subnet_prefix,
				    struct ib_qp_security *qp_sec)
{
	struct ib_qp_security *shared_qp_sec;
	int ret;

	ret = security_ib_pkey_access(qp_sec->security, subnet_prefix, pkey);
	if (ret)
		return ret;

	list_for_each_entry(shared_qp_sec,
			    &qp_sec->shared_qp_list,
			    shared_qp_list) {
		ret = security_ib_pkey_access(shared_qp_sec->security,
					      subnet_prefix,
					      pkey);
		if (ret)
			return ret;
	}
	return 0;
}

/* The caller of this function must hold the QP security
 * mutex of the QP of the security structure in *pps.
 *
 * It takes separate ports_pkeys and security structure
 * because in some cases the pps will be for a new settings
 * or the pps will be for the real QP and security structure
 * will be for a shared QP.
 */
static int check_qp_port_pkey_settings(struct ib_ports_pkeys *pps,
				       struct ib_qp_security *sec)
{
	u64 subnet_prefix;
	u16 pkey;
	int ret = 0;

	if (!pps)
		return 0;

	if (pps->main.state != IB_PORT_PKEY_NOT_VALID) {
		ret = get_pkey_and_subnet_prefix(&pps->main,
						 &pkey,
						 &subnet_prefix);
		if (ret)
			return ret;

		ret = enforce_qp_pkey_security(pkey,
					       subnet_prefix,
					       sec);
		if (ret)
			return ret;
	}

	if (pps->alt.state != IB_PORT_PKEY_NOT_VALID) {
		ret = get_pkey_and_subnet_prefix(&pps->alt,
						 &pkey,
						 &subnet_prefix);
		if (ret)
			return ret;

		ret = enforce_qp_pkey_security(pkey,
					       subnet_prefix,
					       sec);
	}

	return ret;
}

/* The caller of this function must hold the QP security
 * mutex.
 */
static void qp_to_error(struct ib_qp_security *sec)
{
	struct ib_qp_security *shared_qp_sec;
	struct ib_qp_attr attr = {
		.qp_state = IB_QPS_ERR
	};
	struct ib_event event = {
		.event = IB_EVENT_QP_FATAL
	};

	/* If the QP is in the process of being destroyed
	 * the qp pointer in the security structure is
	 * undefined.  It cannot be modified now.
	 */
	if (sec->destroying)
		return;

	ib_modify_qp(sec->qp,
		     &attr,
		     IB_QP_STATE);

	if (sec->qp->event_handler && sec->qp->qp_context) {
		event.element.qp = sec->qp;
		sec->qp->event_handler(&event,
				       sec->qp->qp_context);
	}

	list_for_each_entry(shared_qp_sec,
			    &sec->shared_qp_list,
			    shared_qp_list) {
		struct ib_qp *qp = shared_qp_sec->qp;

		if (qp->event_handler && qp->qp_context) {
			event.element.qp = qp;
			event.device = qp->device;
			qp->event_handler(&event,
					  qp->qp_context);
		}
	}
}

static inline void check_pkey_qps(struct pkey_index_qp_list *pkey,
				  struct ib_device *device,
				  u8 port_num,
				  u64 subnet_prefix)
{
	struct ib_port_pkey *pp, *tmp_pp;
	bool comp;
	LIST_HEAD(to_error_list);
	u16 pkey_val;

	if (!ib_get_cached_pkey(device,
				port_num,
				pkey->pkey_index,
				&pkey_val)) {
		spin_lock(&pkey->qp_list_lock);
		list_for_each_entry(pp, &pkey->qp_list, qp_list) {
			if (atomic_read(&pp->sec->error_list_count))
				continue;

			if (enforce_qp_pkey_security(pkey_val,
						     subnet_prefix,
						     pp->sec)) {
				atomic_inc(&pp->sec->error_list_count);
				list_add(&pp->to_error_list,
					 &to_error_list);
			}
		}
		spin_unlock(&pkey->qp_list_lock);
	}

	list_for_each_entry_safe(pp,
				 tmp_pp,
				 &to_error_list,
				 to_error_list) {
		mutex_lock(&pp->sec->mutex);
		qp_to_error(pp->sec);
		list_del(&pp->to_error_list);
		atomic_dec(&pp->sec->error_list_count);
		comp = pp->sec->destroying;
		mutex_unlock(&pp->sec->mutex);

		if (comp)
			complete(&pp->sec->error_complete);
	}
}

/* The caller of this function must hold the QP security
 * mutex.
 */
static int port_pkey_list_insert(struct ib_port_pkey *pp)
{
	struct pkey_index_qp_list *tmp_pkey;
	struct pkey_index_qp_list *pkey;
	struct ib_device *dev;
	u8 port_num = pp->port_num;
	int ret = 0;

	if (pp->state != IB_PORT_PKEY_VALID)
		return 0;

	dev = pp->sec->dev;

	pkey = get_pkey_idx_qp_list(pp);

	if (!pkey) {
		bool found = false;

		pkey = kzalloc(sizeof(*pkey), GFP_KERNEL);
		if (!pkey)
			return -ENOMEM;

		spin_lock(&dev->port_pkey_list[port_num].list_lock);
		/* Check for the PKey again.  A racing process may
		 * have created it.
		 */
		list_for_each_entry(tmp_pkey,
				    &dev->port_pkey_list[port_num].pkey_list,
				    pkey_index_list) {
			if (tmp_pkey->pkey_index == pp->pkey_index) {
				kfree(pkey);
				pkey = tmp_pkey;
				found = true;
				break;
			}
		}

		if (!found) {
			pkey->pkey_index = pp->pkey_index;
			spin_lock_init(&pkey->qp_list_lock);
			INIT_LIST_HEAD(&pkey->qp_list);
			list_add(&pkey->pkey_index_list,
				 &dev->port_pkey_list[port_num].pkey_list);
		}
		spin_unlock(&dev->port_pkey_list[port_num].list_lock);
	}

	spin_lock(&pkey->qp_list_lock);
	list_add(&pp->qp_list, &pkey->qp_list);
	spin_unlock(&pkey->qp_list_lock);

	pp->state = IB_PORT_PKEY_LISTED;

	return ret;
}

/* The caller of this function must hold the QP security
 * mutex.
 */
static void port_pkey_list_remove(struct ib_port_pkey *pp)
{
	struct pkey_index_qp_list *pkey;

	if (pp->state != IB_PORT_PKEY_LISTED)
		return;

	pkey = get_pkey_idx_qp_list(pp);

	spin_lock(&pkey->qp_list_lock);
	list_del(&pp->qp_list);
	spin_unlock(&pkey->qp_list_lock);

	/* The setting may still be valid, i.e. after
	 * a destroy has failed for example.
	 */
	pp->state = IB_PORT_PKEY_VALID;
}

static void destroy_qp_security(struct ib_qp_security *sec)
{
	security_ib_free_security(sec->security);
	kfree(sec->ports_pkeys);
	kfree(sec);
}

/* The caller of this function must hold the QP security
 * mutex.
 */
static struct ib_ports_pkeys *get_new_pps(const struct ib_qp *qp,
					  const struct ib_qp_attr *qp_attr,
					  int qp_attr_mask)
{
	struct ib_ports_pkeys *new_pps;
	struct ib_ports_pkeys *qp_pps = qp->qp_sec->ports_pkeys;

	new_pps = kzalloc(sizeof(*new_pps), GFP_KERNEL);
	if (!new_pps)
		return NULL;

	if (qp_attr_mask & IB_QP_PORT)
		new_pps->main.port_num = qp_attr->port_num;
	else if (qp_pps)
		new_pps->main.port_num = qp_pps->main.port_num;

	if (qp_attr_mask & IB_QP_PKEY_INDEX)
		new_pps->main.pkey_index = qp_attr->pkey_index;
	else if (qp_pps)
		new_pps->main.pkey_index = qp_pps->main.pkey_index;

	if ((qp_attr_mask & IB_QP_PKEY_INDEX) && (qp_attr_mask & IB_QP_PORT))
		new_pps->main.state = IB_PORT_PKEY_VALID;

	if (!(qp_attr_mask & (IB_QP_PKEY_INDEX | IB_QP_PORT)) && qp_pps) {
		new_pps->main.port_num = qp_pps->main.port_num;
		new_pps->main.pkey_index = qp_pps->main.pkey_index;
		if (qp_pps->main.state != IB_PORT_PKEY_NOT_VALID)
			new_pps->main.state = IB_PORT_PKEY_VALID;
	}

	if (qp_attr_mask & IB_QP_ALT_PATH) {
		new_pps->alt.port_num = qp_attr->alt_port_num;
		new_pps->alt.pkey_index = qp_attr->alt_pkey_index;
		new_pps->alt.state = IB_PORT_PKEY_VALID;
	} else if (qp_pps) {
		new_pps->alt.port_num = qp_pps->alt.port_num;
		new_pps->alt.pkey_index = qp_pps->alt.pkey_index;
		if (qp_pps->alt.state != IB_PORT_PKEY_NOT_VALID)
			new_pps->alt.state = IB_PORT_PKEY_VALID;
	}

	new_pps->main.sec = qp->qp_sec;
	new_pps->alt.sec = qp->qp_sec;
	return new_pps;
}

int ib_open_shared_qp_security(struct ib_qp *qp, struct ib_device *dev)
{
	struct ib_qp *real_qp = qp->real_qp;
	int ret;

	ret = ib_create_qp_security(qp, dev);

	if (ret)
		return ret;

	if (!qp->qp_sec)
		return 0;

	mutex_lock(&real_qp->qp_sec->mutex);
	ret = check_qp_port_pkey_settings(real_qp->qp_sec->ports_pkeys,
					  qp->qp_sec);

	if (ret)
		goto ret;

	if (qp != real_qp)
		list_add(&qp->qp_sec->shared_qp_list,
			 &real_qp->qp_sec->shared_qp_list);
ret:
	mutex_unlock(&real_qp->qp_sec->mutex);
	if (ret)
		destroy_qp_security(qp->qp_sec);

	return ret;
}

void ib_close_shared_qp_security(struct ib_qp_security *sec)
{
	struct ib_qp *real_qp = sec->qp->real_qp;

	mutex_lock(&real_qp->qp_sec->mutex);
	list_del(&sec->shared_qp_list);
	mutex_unlock(&real_qp->qp_sec->mutex);

	destroy_qp_security(sec);
}

int ib_create_qp_security(struct ib_qp *qp, struct ib_device *dev)
{
	u8 i = rdma_start_port(dev);
	bool is_ib = false;
	int ret;

	while (i <= rdma_end_port(dev) && !is_ib)
		is_ib = rdma_protocol_ib(dev, i++);

	/* If this isn't an IB device don't create the security context */
	if (!is_ib)
		return 0;

	qp->qp_sec = kzalloc(sizeof(*qp->qp_sec), GFP_KERNEL);
	if (!qp->qp_sec)
		return -ENOMEM;

	qp->qp_sec->qp = qp;
	qp->qp_sec->dev = dev;
	mutex_init(&qp->qp_sec->mutex);
	INIT_LIST_HEAD(&qp->qp_sec->shared_qp_list);
	atomic_set(&qp->qp_sec->error_list_count, 0);
	init_completion(&qp->qp_sec->error_complete);
	ret = security_ib_alloc_security(&qp->qp_sec->security);
	if (ret) {
		kfree(qp->qp_sec);
		qp->qp_sec = NULL;
	}

	return ret;
}
EXPORT_SYMBOL(ib_create_qp_security);

void ib_destroy_qp_security_begin(struct ib_qp_security *sec)
{
	/* Return if not IB */
	if (!sec)
		return;

	mutex_lock(&sec->mutex);

	/* Remove the QP from the lists so it won't get added to
	 * a to_error_list during the destroy process.
	 */
	if (sec->ports_pkeys) {
		port_pkey_list_remove(&sec->ports_pkeys->main);
		port_pkey_list_remove(&sec->ports_pkeys->alt);
	}

	/* If the QP is already in one or more of those lists
	 * the destroying flag will ensure the to error flow
	 * doesn't operate on an undefined QP.
	 */
	sec->destroying = true;

	/* Record the error list count to know how many completions
	 * to wait for.
	 */
	sec->error_comps_pending = atomic_read(&sec->error_list_count);

	mutex_unlock(&sec->mutex);
}

void ib_destroy_qp_security_abort(struct ib_qp_security *sec)
{
	int ret;
	int i;

	/* Return if not IB */
	if (!sec)
		return;

	/* If a concurrent cache update is in progress this
	 * QP security could be marked for an error state
	 * transition.  Wait for this to complete.
	 */
	for (i = 0; i < sec->error_comps_pending; i++)
		wait_for_completion(&sec->error_complete);

	mutex_lock(&sec->mutex);
	sec->destroying = false;

	/* Restore the position in the lists and verify
	 * access is still allowed in case a cache update
	 * occurred while attempting to destroy.
	 *
	 * Because these setting were listed already
	 * and removed during ib_destroy_qp_security_begin
	 * we know the pkey_index_qp_list for the PKey
	 * already exists so port_pkey_list_insert won't fail.
	 */
	if (sec->ports_pkeys) {
		port_pkey_list_insert(&sec->ports_pkeys->main);
		port_pkey_list_insert(&sec->ports_pkeys->alt);
	}

	ret = check_qp_port_pkey_settings(sec->ports_pkeys, sec);
	if (ret)
		qp_to_error(sec);

	mutex_unlock(&sec->mutex);
}

void ib_destroy_qp_security_end(struct ib_qp_security *sec)
{
	int i;

	/* Return if not IB */
	if (!sec)
		return;

	/* If a concurrent cache update is occurring we must
	 * wait until this QP security structure is processed
	 * in the QP to error flow before destroying it because
	 * the to_error_list is in use.
	 */
	for (i = 0; i < sec->error_comps_pending; i++)
		wait_for_completion(&sec->error_complete);

	destroy_qp_security(sec);
}

void ib_security_cache_change(struct ib_device *device,
			      u8 port_num,
			      u64 subnet_prefix)
{
	struct pkey_index_qp_list *pkey;

	list_for_each_entry(pkey,
			    &device->port_pkey_list[port_num].pkey_list,
			    pkey_index_list) {
		check_pkey_qps(pkey,
			       device,
			       port_num,
			       subnet_prefix);
	}
}

void ib_security_destroy_port_pkey_list(struct ib_device *device)
{
	struct pkey_index_qp_list *pkey, *tmp_pkey;
	int i;

	for (i = rdma_start_port(device); i <= rdma_end_port(device); i++) {
		spin_lock(&device->port_pkey_list[i].list_lock);
		list_for_each_entry_safe(pkey,
					 tmp_pkey,
					 &device->port_pkey_list[i].pkey_list,
					 pkey_index_list) {
			list_del(&pkey->pkey_index_list);
			kfree(pkey);
		}
		spin_unlock(&device->port_pkey_list[i].list_lock);
	}
}

int ib_security_modify_qp(struct ib_qp *qp,
			  struct ib_qp_attr *qp_attr,
			  int qp_attr_mask,
			  struct ib_udata *udata)
{
	int ret = 0;
	struct ib_ports_pkeys *tmp_pps;
	struct ib_ports_pkeys *new_pps = NULL;
	struct ib_qp *real_qp = qp->real_qp;
	bool special_qp = (real_qp->qp_type == IB_QPT_SMI ||
			   real_qp->qp_type == IB_QPT_GSI ||
			   real_qp->qp_type >= IB_QPT_RESERVED1);
	bool pps_change = ((qp_attr_mask & (IB_QP_PKEY_INDEX | IB_QP_PORT)) ||
			   (qp_attr_mask & IB_QP_ALT_PATH));

	WARN_ONCE((qp_attr_mask & IB_QP_PORT &&
		   rdma_protocol_ib(real_qp->device, qp_attr->port_num) &&
		   !real_qp->qp_sec),
		   "%s: QP security is not initialized for IB QP: %d\n",
		   __func__, real_qp->qp_num);

	/* The port/pkey settings are maintained only for the real QP. Open
	 * handles on the real QP will be in the shared_qp_list. When
	 * enforcing security on the real QP all the shared QPs will be
	 * checked as well.
	 */

	if (pps_change && !special_qp && real_qp->qp_sec) {
		mutex_lock(&real_qp->qp_sec->mutex);
		new_pps = get_new_pps(real_qp,
				      qp_attr,
				      qp_attr_mask);
		if (!new_pps) {
			mutex_unlock(&real_qp->qp_sec->mutex);
			return -ENOMEM;
		}
		/* Add this QP to the lists for the new port
		 * and pkey settings before checking for permission
		 * in case there is a concurrent cache update
		 * occurring.  Walking the list for a cache change
		 * doesn't acquire the security mutex unless it's
		 * sending the QP to error.
		 */
		ret = port_pkey_list_insert(&new_pps->main);

		if (!ret)
			ret = port_pkey_list_insert(&new_pps->alt);

		if (!ret)
			ret = check_qp_port_pkey_settings(new_pps,
							  real_qp->qp_sec);
	}

	if (!ret)
		ret = real_qp->device->modify_qp(real_qp,
						 qp_attr,
						 qp_attr_mask,
						 udata);

	if (new_pps) {
		/* Clean up the lists and free the appropriate
		 * ports_pkeys structure.
		 */
		if (ret) {
			tmp_pps = new_pps;
		} else {
			tmp_pps = real_qp->qp_sec->ports_pkeys;
			real_qp->qp_sec->ports_pkeys = new_pps;
		}

		if (tmp_pps) {
			port_pkey_list_remove(&tmp_pps->main);
			port_pkey_list_remove(&tmp_pps->alt);
		}
		kfree(tmp_pps);
		mutex_unlock(&real_qp->qp_sec->mutex);
	}
	return ret;
}

static int ib_security_pkey_access(struct ib_device *dev,
				   u8 port_num,
				   u16 pkey_index,
				   void *sec)
{
	u64 subnet_prefix;
	u16 pkey;
	int ret;

	if (!rdma_protocol_ib(dev, port_num))
		return 0;

	ret = ib_get_cached_pkey(dev, port_num, pkey_index, &pkey);
	if (ret)
		return ret;

	ret = ib_get_cached_subnet_prefix(dev, port_num, &subnet_prefix);

	if (ret)
		return ret;

	return security_ib_pkey_access(sec, subnet_prefix, pkey);
}

static int ib_mad_agent_security_change(struct notifier_block *nb,
					unsigned long event,
					void *data)
{
	struct ib_mad_agent *ag = container_of(nb, struct ib_mad_agent, lsm_nb);

	if (event != LSM_POLICY_CHANGE)
		return NOTIFY_DONE;

	ag->smp_allowed = !security_ib_endport_manage_subnet(ag->security,
							     ag->device->name,
							     ag->port_num);

	return NOTIFY_OK;
}

int ib_mad_agent_security_setup(struct ib_mad_agent *agent,
				enum ib_qp_type qp_type)
{
	int ret;

	if (!rdma_protocol_ib(agent->device, agent->port_num))
		return 0;

	ret = security_ib_alloc_security(&agent->security);
	if (ret)
		return ret;

	if (qp_type != IB_QPT_SMI)
		return 0;

	ret = security_ib_endport_manage_subnet(agent->security,
						agent->device->name,
						agent->port_num);
	if (ret)
		goto free_security;

	agent->lsm_nb.notifier_call = ib_mad_agent_security_change;
	ret = register_lsm_notifier(&agent->lsm_nb);
	if (ret)
		goto free_security;

	agent->smp_allowed = true;
	agent->lsm_nb_reg = true;
	return 0;

free_security:
	security_ib_free_security(agent->security);
	return ret;
}

void ib_mad_agent_security_cleanup(struct ib_mad_agent *agent)
{
	if (!rdma_protocol_ib(agent->device, agent->port_num))
		return;

	if (agent->lsm_nb_reg)
		unregister_lsm_notifier(&agent->lsm_nb);

	security_ib_free_security(agent->security);
}

int ib_mad_enforce_security(struct ib_mad_agent_private *map, u16 pkey_index)
{
	if (!rdma_protocol_ib(map->agent.device, map->agent.port_num))
		return 0;

	if (map->agent.qp->qp_type == IB_QPT_SMI) {
		if (!map->agent.smp_allowed)
			return -EACCES;
		return 0;
	}

	return ib_security_pkey_access(map->agent.device,
				       map->agent.port_num,
				       pkey_index,
				       map->agent.security);
}
