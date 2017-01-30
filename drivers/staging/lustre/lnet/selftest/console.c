/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/selftest/conctl.c
 *
 * Infrastructure of LST console
 *
 * Author: Liang Zhen <liangzhen@clusterfs.com>
 */

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/lib-lnet.h"
#include "console.h"
#include "conrpc.h"

#define LST_NODE_STATE_COUNTER(nd, p)			\
do {							\
	if ((nd)->nd_state == LST_NODE_ACTIVE)		\
		(p)->nle_nactive++;			\
	else if ((nd)->nd_state == LST_NODE_BUSY)	\
		(p)->nle_nbusy++;			\
	else if ((nd)->nd_state == LST_NODE_DOWN)	\
		(p)->nle_ndown++;			\
	else						\
		(p)->nle_nunknown++;			\
	(p)->nle_nnode++;				\
} while (0)

struct lstcon_session console_session;

static void
lstcon_node_get(struct lstcon_node *nd)
{
	LASSERT(nd->nd_ref >= 1);

	nd->nd_ref++;
}

static int
lstcon_node_find(lnet_process_id_t id, struct lstcon_node **ndpp, int create)
{
	struct lstcon_ndlink	*ndl;
	unsigned int idx = LNET_NIDADDR(id.nid) % LST_GLOBAL_HASHSIZE;

	LASSERT(id.nid != LNET_NID_ANY);

	list_for_each_entry(ndl, &console_session.ses_ndl_hash[idx],
			    ndl_hlink) {
		if (ndl->ndl_node->nd_id.nid != id.nid ||
		    ndl->ndl_node->nd_id.pid != id.pid)
			continue;

		lstcon_node_get(ndl->ndl_node);
		*ndpp = ndl->ndl_node;
		return 0;
	}

	if (!create)
		return -ENOENT;

	LIBCFS_ALLOC(*ndpp, sizeof(**ndpp) + sizeof(*ndl));
	if (!*ndpp)
		return -ENOMEM;

	ndl = (struct lstcon_ndlink *)(*ndpp + 1);

	ndl->ndl_node = *ndpp;

	ndl->ndl_node->nd_ref = 1;
	ndl->ndl_node->nd_id = id;
	ndl->ndl_node->nd_stamp = cfs_time_current();
	ndl->ndl_node->nd_state = LST_NODE_UNKNOWN;
	ndl->ndl_node->nd_timeout = 0;
	memset(&ndl->ndl_node->nd_ping, 0, sizeof(struct lstcon_rpc));

	/*
	 * queued in global hash & list, no refcount is taken by
	 * global hash & list, if caller release his refcount,
	 * node will be released
	 */
	list_add_tail(&ndl->ndl_hlink, &console_session.ses_ndl_hash[idx]);
	list_add_tail(&ndl->ndl_link, &console_session.ses_ndl_list);

	return 0;
}

static void
lstcon_node_put(struct lstcon_node *nd)
{
	struct lstcon_ndlink *ndl;

	LASSERT(nd->nd_ref > 0);

	if (--nd->nd_ref > 0)
		return;

	ndl = (struct lstcon_ndlink *)(nd + 1);

	LASSERT(!list_empty(&ndl->ndl_link));
	LASSERT(!list_empty(&ndl->ndl_hlink));

	/* remove from session */
	list_del(&ndl->ndl_link);
	list_del(&ndl->ndl_hlink);

	LIBCFS_FREE(nd, sizeof(*nd) + sizeof(*ndl));
}

static int
lstcon_ndlink_find(struct list_head *hash, lnet_process_id_t id,
		   struct lstcon_ndlink **ndlpp, int create)
{
	unsigned int idx = LNET_NIDADDR(id.nid) % LST_NODE_HASHSIZE;
	struct lstcon_ndlink *ndl;
	struct lstcon_node *nd;
	int rc;

	if (id.nid == LNET_NID_ANY)
		return -EINVAL;

	/* search in hash */
	list_for_each_entry(ndl, &hash[idx], ndl_hlink) {
		if (ndl->ndl_node->nd_id.nid != id.nid ||
		    ndl->ndl_node->nd_id.pid != id.pid)
			continue;

		*ndlpp = ndl;
		return 0;
	}

	if (!create)
		return -ENOENT;

	/* find or create in session hash */
	rc = lstcon_node_find(id, &nd, (create == 1) ? 1 : 0);
	if (rc)
		return rc;

	LIBCFS_ALLOC(ndl, sizeof(struct lstcon_ndlink));
	if (!ndl) {
		lstcon_node_put(nd);
		return -ENOMEM;
	}

	*ndlpp = ndl;

	ndl->ndl_node = nd;
	INIT_LIST_HEAD(&ndl->ndl_link);
	list_add_tail(&ndl->ndl_hlink, &hash[idx]);

	return 0;
}

static void
lstcon_ndlink_release(struct lstcon_ndlink *ndl)
{
	LASSERT(list_empty(&ndl->ndl_link));
	LASSERT(!list_empty(&ndl->ndl_hlink));

	list_del(&ndl->ndl_hlink); /* delete from hash */
	lstcon_node_put(ndl->ndl_node);

	LIBCFS_FREE(ndl, sizeof(*ndl));
}

static int
lstcon_group_alloc(char *name, struct lstcon_group **grpp)
{
	struct lstcon_group *grp;
	int i;

	LIBCFS_ALLOC(grp, offsetof(struct lstcon_group,
				   grp_ndl_hash[LST_NODE_HASHSIZE]));
	if (!grp)
		return -ENOMEM;

	grp->grp_ref = 1;
	if (name) {
		if (strlen(name) > sizeof(grp->grp_name) - 1) {
			LIBCFS_FREE(grp, offsetof(struct lstcon_group,
				    grp_ndl_hash[LST_NODE_HASHSIZE]));
			return -E2BIG;
		}
		strncpy(grp->grp_name, name, sizeof(grp->grp_name));
	}

	INIT_LIST_HEAD(&grp->grp_link);
	INIT_LIST_HEAD(&grp->grp_ndl_list);
	INIT_LIST_HEAD(&grp->grp_trans_list);

	for (i = 0; i < LST_NODE_HASHSIZE; i++)
		INIT_LIST_HEAD(&grp->grp_ndl_hash[i]);

	*grpp = grp;

	return 0;
}

static void
lstcon_group_addref(struct lstcon_group *grp)
{
	grp->grp_ref++;
}

static void lstcon_group_ndlink_release(struct lstcon_group *,
					struct lstcon_ndlink *);

static void
lstcon_group_drain(struct lstcon_group *grp, int keep)
{
	struct lstcon_ndlink *ndl;
	struct lstcon_ndlink *tmp;

	list_for_each_entry_safe(ndl, tmp, &grp->grp_ndl_list, ndl_link) {
		if (!(ndl->ndl_node->nd_state & keep))
			lstcon_group_ndlink_release(grp, ndl);
	}
}

static void
lstcon_group_decref(struct lstcon_group *grp)
{
	int i;

	if (--grp->grp_ref > 0)
		return;

	if (!list_empty(&grp->grp_link))
		list_del(&grp->grp_link);

	lstcon_group_drain(grp, 0);

	for (i = 0; i < LST_NODE_HASHSIZE; i++)
		LASSERT(list_empty(&grp->grp_ndl_hash[i]));

	LIBCFS_FREE(grp, offsetof(struct lstcon_group,
				  grp_ndl_hash[LST_NODE_HASHSIZE]));
}

static int
lstcon_group_find(const char *name, struct lstcon_group **grpp)
{
	struct lstcon_group *grp;

	list_for_each_entry(grp, &console_session.ses_grp_list, grp_link) {
		if (strncmp(grp->grp_name, name, LST_NAME_SIZE))
			continue;

		lstcon_group_addref(grp); /* +1 ref for caller */
		*grpp = grp;
		return 0;
	}

	return -ENOENT;
}

static int
lstcon_group_ndlink_find(struct lstcon_group *grp, lnet_process_id_t id,
			 struct lstcon_ndlink **ndlpp, int create)
{
	int rc;

	rc = lstcon_ndlink_find(&grp->grp_ndl_hash[0], id, ndlpp, create);
	if (rc)
		return rc;

	if (!list_empty(&(*ndlpp)->ndl_link))
		return 0;

	list_add_tail(&(*ndlpp)->ndl_link, &grp->grp_ndl_list);
	grp->grp_nnode++;

	return 0;
}

static void
lstcon_group_ndlink_release(struct lstcon_group *grp, struct lstcon_ndlink *ndl)
{
	list_del_init(&ndl->ndl_link);
	lstcon_ndlink_release(ndl);
	grp->grp_nnode--;
}

static void
lstcon_group_ndlink_move(struct lstcon_group *old,
			 struct lstcon_group *new, struct lstcon_ndlink *ndl)
{
	unsigned int idx = LNET_NIDADDR(ndl->ndl_node->nd_id.nid) %
					LST_NODE_HASHSIZE;

	list_del(&ndl->ndl_hlink);
	list_del(&ndl->ndl_link);
	old->grp_nnode--;

	list_add_tail(&ndl->ndl_hlink, &new->grp_ndl_hash[idx]);
	list_add_tail(&ndl->ndl_link, &new->grp_ndl_list);
	new->grp_nnode++;
}

static void
lstcon_group_move(struct lstcon_group *old, struct lstcon_group *new)
{
	struct lstcon_ndlink *ndl;

	while (!list_empty(&old->grp_ndl_list)) {
		ndl = list_entry(old->grp_ndl_list.next,
				 struct lstcon_ndlink, ndl_link);
		lstcon_group_ndlink_move(old, new, ndl);
	}
}

static int
lstcon_sesrpc_condition(int transop, struct lstcon_node *nd, void *arg)
{
	struct lstcon_group *grp = (struct lstcon_group *)arg;

	switch (transop) {
	case LST_TRANS_SESNEW:
		if (nd->nd_state == LST_NODE_ACTIVE)
			return 0;
		break;

	case LST_TRANS_SESEND:
		if (nd->nd_state != LST_NODE_ACTIVE)
			return 0;

		if (grp && nd->nd_ref > 1)
			return 0;
		break;

	case LST_TRANS_SESQRY:
		break;

	default:
		LBUG();
	}

	return 1;
}

static int
lstcon_sesrpc_readent(int transop, struct srpc_msg *msg,
		      lstcon_rpc_ent_t __user *ent_up)
{
	struct srpc_debug_reply *rep;

	switch (transop) {
	case LST_TRANS_SESNEW:
	case LST_TRANS_SESEND:
		return 0;

	case LST_TRANS_SESQRY:
		rep = &msg->msg_body.dbg_reply;

		if (copy_to_user(&ent_up->rpe_priv[0],
				 &rep->dbg_timeout, sizeof(int)) ||
		    copy_to_user(&ent_up->rpe_payload[0],
				 &rep->dbg_name, LST_NAME_SIZE))
			return -EFAULT;

		return 0;

	default:
		LBUG();
	}

	return 0;
}

static int
lstcon_group_nodes_add(struct lstcon_group *grp,
		       int count, lnet_process_id_t __user *ids_up,
		       unsigned int *featp,
		       struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_ndlink	*ndl;
	struct lstcon_group *tmp;
	lnet_process_id_t id;
	int i;
	int rc;

	rc = lstcon_group_alloc(NULL, &tmp);
	if (rc) {
		CERROR("Out of memory\n");
		return -ENOMEM;
	}

	for (i = 0 ; i < count; i++) {
		if (copy_from_user(&id, &ids_up[i], sizeof(id))) {
			rc = -EFAULT;
			break;
		}

		/* skip if it's in this group already */
		rc = lstcon_group_ndlink_find(grp, id, &ndl, 0);
		if (!rc)
			continue;

		/* add to tmp group */
		rc = lstcon_group_ndlink_find(tmp, id, &ndl, 1);
		if (rc) {
			CERROR("Can't create ndlink, out of memory\n");
			break;
		}
	}

	if (rc) {
		lstcon_group_decref(tmp);
		return rc;
	}

	rc = lstcon_rpc_trans_ndlist(&tmp->grp_ndl_list,
				     &tmp->grp_trans_list, LST_TRANS_SESNEW,
				     tmp, lstcon_sesrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		lstcon_group_decref(tmp);
		return rc;
	}

	/* post all RPCs */
	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	rc = lstcon_rpc_trans_interpreter(trans, result_up,
					  lstcon_sesrpc_readent);
	*featp = trans->tas_features;

	/* destroy all RPGs */
	lstcon_rpc_trans_destroy(trans);

	lstcon_group_move(tmp, grp);
	lstcon_group_decref(tmp);

	return rc;
}

static int
lstcon_group_nodes_remove(struct lstcon_group *grp,
			  int count, lnet_process_id_t __user *ids_up,
			  struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_ndlink *ndl;
	struct lstcon_group *tmp;
	lnet_process_id_t id;
	int rc;
	int i;

	/* End session and remove node from the group */

	rc = lstcon_group_alloc(NULL, &tmp);
	if (rc) {
		CERROR("Out of memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < count; i++) {
		if (copy_from_user(&id, &ids_up[i], sizeof(id))) {
			rc = -EFAULT;
			goto error;
		}

		/* move node to tmp group */
		if (!lstcon_group_ndlink_find(grp, id, &ndl, 0))
			lstcon_group_ndlink_move(grp, tmp, ndl);
	}

	rc = lstcon_rpc_trans_ndlist(&tmp->grp_ndl_list,
				     &tmp->grp_trans_list, LST_TRANS_SESEND,
				     tmp, lstcon_sesrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		goto error;
	}

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	rc = lstcon_rpc_trans_interpreter(trans, result_up, NULL);

	lstcon_rpc_trans_destroy(trans);
	/* release nodes anyway, because we can't rollback status */
	lstcon_group_decref(tmp);

	return rc;
error:
	lstcon_group_move(tmp, grp);
	lstcon_group_decref(tmp);

	return rc;
}

int
lstcon_group_add(char *name)
{
	struct lstcon_group *grp;
	int rc;

	rc = lstcon_group_find(name, &grp) ? 0 : -EEXIST;
	if (rc) {
		/* find a group with same name */
		lstcon_group_decref(grp);
		return rc;
	}

	rc = lstcon_group_alloc(name, &grp);
	if (rc) {
		CERROR("Can't allocate descriptor for group %s\n", name);
		return -ENOMEM;
	}

	list_add_tail(&grp->grp_link, &console_session.ses_grp_list);

	return rc;
}

int
lstcon_nodes_add(char *name, int count, lnet_process_id_t __user *ids_up,
		 unsigned int *featp,
		 struct list_head __user *result_up)
{
	struct lstcon_group *grp;
	int rc;

	LASSERT(count > 0);
	LASSERT(ids_up);

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group %s\n", name);
		return rc;
	}

	if (grp->grp_ref > 2) {
		/* referred by other threads or test */
		CDEBUG(D_NET, "Group %s is busy\n", name);
		lstcon_group_decref(grp);

		return -EBUSY;
	}

	rc = lstcon_group_nodes_add(grp, count, ids_up, featp, result_up);

	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_group_del(char *name)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_group *grp;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group: %s\n", name);
		return rc;
	}

	if (grp->grp_ref > 2) {
		/* referred by others threads or test */
		CDEBUG(D_NET, "Group %s is busy\n", name);
		lstcon_group_decref(grp);
		return -EBUSY;
	}

	rc = lstcon_rpc_trans_ndlist(&grp->grp_ndl_list,
				     &grp->grp_trans_list, LST_TRANS_SESEND,
				     grp, lstcon_sesrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		lstcon_group_decref(grp);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	lstcon_rpc_trans_destroy(trans);

	lstcon_group_decref(grp);
	/*
	 * -ref for session, it's destroyed,
	 * status can't be rolled back, destroy group anyway
	 */
	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_group_clean(char *name, int args)
{
	struct lstcon_group *grp = NULL;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group %s\n", name);
		return rc;
	}

	if (grp->grp_ref > 2) {
		/* referred by test */
		CDEBUG(D_NET, "Group %s is busy\n", name);
		lstcon_group_decref(grp);
		return -EBUSY;
	}

	args = (LST_NODE_ACTIVE | LST_NODE_BUSY |
		LST_NODE_DOWN | LST_NODE_UNKNOWN) & ~args;

	lstcon_group_drain(grp, args);

	lstcon_group_decref(grp);
	/* release empty group */
	if (list_empty(&grp->grp_ndl_list))
		lstcon_group_decref(grp);

	return 0;
}

int
lstcon_nodes_remove(char *name, int count, lnet_process_id_t __user *ids_up,
		    struct list_head __user *result_up)
{
	struct lstcon_group *grp = NULL;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group: %s\n", name);
		return rc;
	}

	if (grp->grp_ref > 2) {
		/* referred by test */
		CDEBUG(D_NET, "Group %s is busy\n", name);
		lstcon_group_decref(grp);
		return -EBUSY;
	}

	rc = lstcon_group_nodes_remove(grp, count, ids_up, result_up);

	lstcon_group_decref(grp);
	/* release empty group */
	if (list_empty(&grp->grp_ndl_list))
		lstcon_group_decref(grp);

	return rc;
}

int
lstcon_group_refresh(char *name, struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_group *grp;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group: %s\n", name);
		return rc;
	}

	if (grp->grp_ref > 2) {
		/* referred by test */
		CDEBUG(D_NET, "Group %s is busy\n", name);
		lstcon_group_decref(grp);
		return -EBUSY;
	}

	/* re-invite all inactive nodes int the group */
	rc = lstcon_rpc_trans_ndlist(&grp->grp_ndl_list,
				     &grp->grp_trans_list, LST_TRANS_SESNEW,
				     grp, lstcon_sesrpc_condition, &trans);
	if (rc) {
		/* local error, return */
		CDEBUG(D_NET, "Can't create transaction: %d\n", rc);
		lstcon_group_decref(grp);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	rc = lstcon_rpc_trans_interpreter(trans, result_up, NULL);

	lstcon_rpc_trans_destroy(trans);
	/* -ref for me */
	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_group_list(int index, int len, char __user *name_up)
{
	struct lstcon_group *grp;

	LASSERT(index >= 0);
	LASSERT(name_up);

	list_for_each_entry(grp, &console_session.ses_grp_list, grp_link) {
		if (!index--) {
			return copy_to_user(name_up, grp->grp_name, len) ?
					    -EFAULT : 0;
		}
	}

	return -ENOENT;
}

static int
lstcon_nodes_getent(struct list_head *head, int *index_p,
		    int *count_p, lstcon_node_ent_t __user *dents_up)
{
	struct lstcon_ndlink *ndl;
	struct lstcon_node *nd;
	int count = 0;
	int index = 0;

	LASSERT(index_p && count_p);
	LASSERT(dents_up);
	LASSERT(*index_p >= 0);
	LASSERT(*count_p > 0);

	list_for_each_entry(ndl, head, ndl_link) {
		if (index++ < *index_p)
			continue;

		if (count >= *count_p)
			break;

		nd = ndl->ndl_node;
		if (copy_to_user(&dents_up[count].nde_id,
				 &nd->nd_id, sizeof(nd->nd_id)) ||
		    copy_to_user(&dents_up[count].nde_state,
				 &nd->nd_state, sizeof(nd->nd_state)))
			return -EFAULT;

		count++;
	}

	if (index <= *index_p)
		return -ENOENT;

	*count_p = count;
	*index_p = index;

	return 0;
}

int
lstcon_group_info(char *name, lstcon_ndlist_ent_t __user *gents_p,
		  int *index_p, int *count_p,
		  lstcon_node_ent_t __user *dents_up)
{
	lstcon_ndlist_ent_t *gentp;
	struct lstcon_group *grp;
	struct lstcon_ndlink *ndl;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group %s\n", name);
		return rc;
	}

	if (dents_up) {
		/* verbose query */
		rc = lstcon_nodes_getent(&grp->grp_ndl_list,
					 index_p, count_p, dents_up);
		lstcon_group_decref(grp);

		return rc;
	}

	/* non-verbose query */
	LIBCFS_ALLOC(gentp, sizeof(lstcon_ndlist_ent_t));
	if (!gentp) {
		CERROR("Can't allocate ndlist_ent\n");
		lstcon_group_decref(grp);

		return -ENOMEM;
	}

	list_for_each_entry(ndl, &grp->grp_ndl_list, ndl_link)
		LST_NODE_STATE_COUNTER(ndl->ndl_node, gentp);

	rc = copy_to_user(gents_p, gentp,
			  sizeof(lstcon_ndlist_ent_t)) ? -EFAULT : 0;

	LIBCFS_FREE(gentp, sizeof(lstcon_ndlist_ent_t));

	lstcon_group_decref(grp);

	return rc;
}

static int
lstcon_batch_find(const char *name, struct lstcon_batch **batpp)
{
	struct lstcon_batch *bat;

	list_for_each_entry(bat, &console_session.ses_bat_list, bat_link) {
		if (!strncmp(bat->bat_name, name, LST_NAME_SIZE)) {
			*batpp = bat;
			return 0;
		}
	}

	return -ENOENT;
}

int
lstcon_batch_add(char *name)
{
	struct lstcon_batch *bat;
	int i;
	int rc;

	rc = !lstcon_batch_find(name, &bat) ? -EEXIST : 0;
	if (rc) {
		CDEBUG(D_NET, "Batch %s already exists\n", name);
		return rc;
	}

	LIBCFS_ALLOC(bat, sizeof(struct lstcon_batch));
	if (!bat) {
		CERROR("Can't allocate descriptor for batch %s\n", name);
		return -ENOMEM;
	}

	LIBCFS_ALLOC(bat->bat_cli_hash,
		     sizeof(struct list_head) * LST_NODE_HASHSIZE);
	if (!bat->bat_cli_hash) {
		CERROR("Can't allocate hash for batch %s\n", name);
		LIBCFS_FREE(bat, sizeof(struct lstcon_batch));

		return -ENOMEM;
	}

	LIBCFS_ALLOC(bat->bat_srv_hash,
		     sizeof(struct list_head) * LST_NODE_HASHSIZE);
	if (!bat->bat_srv_hash) {
		CERROR("Can't allocate hash for batch %s\n", name);
		LIBCFS_FREE(bat->bat_cli_hash, LST_NODE_HASHSIZE);
		LIBCFS_FREE(bat, sizeof(struct lstcon_batch));

		return -ENOMEM;
	}

	if (strlen(name) > sizeof(bat->bat_name) - 1) {
		LIBCFS_FREE(bat->bat_srv_hash, LST_NODE_HASHSIZE);
		LIBCFS_FREE(bat->bat_cli_hash, LST_NODE_HASHSIZE);
		LIBCFS_FREE(bat, sizeof(struct lstcon_batch));
		return -E2BIG;
	}
	strncpy(bat->bat_name, name, sizeof(bat->bat_name));
	bat->bat_hdr.tsb_index = 0;
	bat->bat_hdr.tsb_id.bat_id = ++console_session.ses_id_cookie;

	bat->bat_ntest = 0;
	bat->bat_state = LST_BATCH_IDLE;

	INIT_LIST_HEAD(&bat->bat_cli_list);
	INIT_LIST_HEAD(&bat->bat_srv_list);
	INIT_LIST_HEAD(&bat->bat_test_list);
	INIT_LIST_HEAD(&bat->bat_trans_list);

	for (i = 0; i < LST_NODE_HASHSIZE; i++) {
		INIT_LIST_HEAD(&bat->bat_cli_hash[i]);
		INIT_LIST_HEAD(&bat->bat_srv_hash[i]);
	}

	list_add_tail(&bat->bat_link, &console_session.ses_bat_list);

	return rc;
}

int
lstcon_batch_list(int index, int len, char __user *name_up)
{
	struct lstcon_batch *bat;

	LASSERT(name_up);
	LASSERT(index >= 0);

	list_for_each_entry(bat, &console_session.ses_bat_list, bat_link) {
		if (!index--) {
			return copy_to_user(name_up, bat->bat_name, len) ?
					    -EFAULT : 0;
		}
	}

	return -ENOENT;
}

int
lstcon_batch_info(char *name, lstcon_test_batch_ent_t __user *ent_up,
		  int server, int testidx, int *index_p, int *ndent_p,
		  lstcon_node_ent_t __user *dents_up)
{
	lstcon_test_batch_ent_t *entp;
	struct list_head *clilst;
	struct list_head *srvlst;
	struct lstcon_test *test = NULL;
	struct lstcon_batch *bat;
	struct lstcon_ndlink	*ndl;
	int rc;

	rc = lstcon_batch_find(name, &bat);
	if (rc) {
		CDEBUG(D_NET, "Can't find batch %s\n", name);
		return -ENOENT;
	}

	if (testidx > 0) {
		/* query test, test index start from 1 */
		list_for_each_entry(test, &bat->bat_test_list, tes_link) {
			if (testidx-- == 1)
				break;
		}

		if (testidx > 0) {
			CDEBUG(D_NET, "Can't find specified test in batch\n");
			return -ENOENT;
		}
	}

	clilst = !test ? &bat->bat_cli_list :
			 &test->tes_src_grp->grp_ndl_list;
	srvlst = !test ? &bat->bat_srv_list :
			 &test->tes_dst_grp->grp_ndl_list;

	if (dents_up) {
		rc = lstcon_nodes_getent((server ? srvlst : clilst),
					 index_p, ndent_p, dents_up);
		return rc;
	}

	/* non-verbose query */
	LIBCFS_ALLOC(entp, sizeof(lstcon_test_batch_ent_t));
	if (!entp)
		return -ENOMEM;

	if (!test) {
		entp->u.tbe_batch.bae_ntest = bat->bat_ntest;
		entp->u.tbe_batch.bae_state = bat->bat_state;
	} else {
		entp->u.tbe_test.tse_type = test->tes_type;
		entp->u.tbe_test.tse_loop = test->tes_loop;
		entp->u.tbe_test.tse_concur = test->tes_concur;
	}

	list_for_each_entry(ndl, clilst, ndl_link)
		LST_NODE_STATE_COUNTER(ndl->ndl_node, &entp->tbe_cli_nle);

	list_for_each_entry(ndl, srvlst, ndl_link)
		LST_NODE_STATE_COUNTER(ndl->ndl_node, &entp->tbe_srv_nle);

	rc = copy_to_user(ent_up, entp,
			  sizeof(lstcon_test_batch_ent_t)) ? -EFAULT : 0;

	LIBCFS_FREE(entp, sizeof(lstcon_test_batch_ent_t));

	return rc;
}

static int
lstcon_batrpc_condition(int transop, struct lstcon_node *nd, void *arg)
{
	switch (transop) {
	case LST_TRANS_TSBRUN:
		if (nd->nd_state != LST_NODE_ACTIVE)
			return -ENETDOWN;
		break;

	case LST_TRANS_TSBSTOP:
		if (nd->nd_state != LST_NODE_ACTIVE)
			return 0;
		break;

	case LST_TRANS_TSBCLIQRY:
	case LST_TRANS_TSBSRVQRY:
		break;
	}

	return 1;
}

static int
lstcon_batch_op(struct lstcon_batch *bat, int transop,
		struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	int rc;

	rc = lstcon_rpc_trans_ndlist(&bat->bat_cli_list,
				     &bat->bat_trans_list, transop,
				     bat, lstcon_batrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	rc = lstcon_rpc_trans_interpreter(trans, result_up, NULL);

	lstcon_rpc_trans_destroy(trans);

	return rc;
}

int
lstcon_batch_run(char *name, int timeout, struct list_head __user *result_up)
{
	struct lstcon_batch *bat;
	int rc;

	if (lstcon_batch_find(name, &bat)) {
		CDEBUG(D_NET, "Can't find batch %s\n", name);
		return -ENOENT;
	}

	bat->bat_arg = timeout;

	rc = lstcon_batch_op(bat, LST_TRANS_TSBRUN, result_up);

	/* mark batch as running if it's started in any node */
	if (lstcon_tsbop_stat_success(lstcon_trans_stat(), 0))
		bat->bat_state = LST_BATCH_RUNNING;

	return rc;
}

int
lstcon_batch_stop(char *name, int force, struct list_head __user *result_up)
{
	struct lstcon_batch *bat;
	int rc;

	if (lstcon_batch_find(name, &bat)) {
		CDEBUG(D_NET, "Can't find batch %s\n", name);
		return -ENOENT;
	}

	bat->bat_arg = force;

	rc = lstcon_batch_op(bat, LST_TRANS_TSBSTOP, result_up);

	/* mark batch as stopped if all RPCs finished */
	if (!lstcon_tsbop_stat_failure(lstcon_trans_stat(), 0))
		bat->bat_state = LST_BATCH_IDLE;

	return rc;
}

static void
lstcon_batch_destroy(struct lstcon_batch *bat)
{
	struct lstcon_ndlink *ndl;
	struct lstcon_test *test;
	int i;

	list_del(&bat->bat_link);

	while (!list_empty(&bat->bat_test_list)) {
		test = list_entry(bat->bat_test_list.next,
				  struct lstcon_test, tes_link);
		LASSERT(list_empty(&test->tes_trans_list));

		list_del(&test->tes_link);

		lstcon_group_decref(test->tes_src_grp);
		lstcon_group_decref(test->tes_dst_grp);

		LIBCFS_FREE(test, offsetof(struct lstcon_test,
					   tes_param[test->tes_paramlen]));
	}

	LASSERT(list_empty(&bat->bat_trans_list));

	while (!list_empty(&bat->bat_cli_list)) {
		ndl = list_entry(bat->bat_cli_list.next,
				 struct lstcon_ndlink, ndl_link);
		list_del_init(&ndl->ndl_link);

		lstcon_ndlink_release(ndl);
	}

	while (!list_empty(&bat->bat_srv_list)) {
		ndl = list_entry(bat->bat_srv_list.next,
				 struct lstcon_ndlink, ndl_link);
		list_del_init(&ndl->ndl_link);

		lstcon_ndlink_release(ndl);
	}

	for (i = 0; i < LST_NODE_HASHSIZE; i++) {
		LASSERT(list_empty(&bat->bat_cli_hash[i]));
		LASSERT(list_empty(&bat->bat_srv_hash[i]));
	}

	LIBCFS_FREE(bat->bat_cli_hash,
		    sizeof(struct list_head) * LST_NODE_HASHSIZE);
	LIBCFS_FREE(bat->bat_srv_hash,
		    sizeof(struct list_head) * LST_NODE_HASHSIZE);
	LIBCFS_FREE(bat, sizeof(struct lstcon_batch));
}

static int
lstcon_testrpc_condition(int transop, struct lstcon_node *nd, void *arg)
{
	struct lstcon_test *test;
	struct lstcon_batch *batch;
	struct lstcon_ndlink *ndl;
	struct list_head *hash;
	struct list_head *head;

	test = (struct lstcon_test *)arg;
	LASSERT(test);

	batch = test->tes_batch;
	LASSERT(batch);

	if (test->tes_oneside &&
	    transop == LST_TRANS_TSBSRVADD)
		return 0;

	if (nd->nd_state != LST_NODE_ACTIVE)
		return -ENETDOWN;

	if (transop == LST_TRANS_TSBCLIADD) {
		hash = batch->bat_cli_hash;
		head = &batch->bat_cli_list;

	} else {
		LASSERT(transop == LST_TRANS_TSBSRVADD);

		hash = batch->bat_srv_hash;
		head = &batch->bat_srv_list;
	}

	LASSERT(nd->nd_id.nid != LNET_NID_ANY);

	if (lstcon_ndlink_find(hash, nd->nd_id, &ndl, 1))
		return -ENOMEM;

	if (list_empty(&ndl->ndl_link))
		list_add_tail(&ndl->ndl_link, head);

	return 1;
}

static int
lstcon_test_nodes_add(struct lstcon_test *test,
		      struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_group *grp;
	int transop;
	int rc;

	LASSERT(test->tes_src_grp);
	LASSERT(test->tes_dst_grp);

	transop = LST_TRANS_TSBSRVADD;
	grp = test->tes_dst_grp;
again:
	rc = lstcon_rpc_trans_ndlist(&grp->grp_ndl_list,
				     &test->tes_trans_list, transop,
				     test, lstcon_testrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	if (lstcon_trans_stat()->trs_rpc_errno ||
	    lstcon_trans_stat()->trs_fwk_errno) {
		lstcon_rpc_trans_interpreter(trans, result_up, NULL);

		lstcon_rpc_trans_destroy(trans);
		/* return if any error */
		CDEBUG(D_NET, "Failed to add test %s, RPC error %d, framework error %d\n",
		       transop == LST_TRANS_TSBCLIADD ? "client" : "server",
		       lstcon_trans_stat()->trs_rpc_errno,
		       lstcon_trans_stat()->trs_fwk_errno);

		return rc;
	}

	lstcon_rpc_trans_destroy(trans);

	if (transop == LST_TRANS_TSBCLIADD)
		return rc;

	transop = LST_TRANS_TSBCLIADD;
	grp = test->tes_src_grp;
	test->tes_cliidx = 0;

	/* requests to test clients */
	goto again;
}

static int
lstcon_verify_batch(const char *name, struct lstcon_batch **batch)
{
	int rc;

	rc = lstcon_batch_find(name, batch);
	if (rc) {
		CDEBUG(D_NET, "Can't find batch %s\n", name);
		return rc;
	}

	if ((*batch)->bat_state != LST_BATCH_IDLE) {
		CDEBUG(D_NET, "Can't change running batch %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static int
lstcon_verify_group(const char *name, struct lstcon_group **grp)
{
	int rc;
	struct lstcon_ndlink	*ndl;

	rc = lstcon_group_find(name, grp);
	if (rc) {
		CDEBUG(D_NET, "can't find group %s\n", name);
		return rc;
	}

	list_for_each_entry(ndl, &(*grp)->grp_ndl_list, ndl_link) {
		if (ndl->ndl_node->nd_state == LST_NODE_ACTIVE)
			return 0;
	}

	CDEBUG(D_NET, "Group %s has no ACTIVE nodes\n", name);

	return -EINVAL;
}

int
lstcon_test_add(char *batch_name, int type, int loop,
		int concur, int dist, int span,
		char *src_name, char *dst_name,
		void *param, int paramlen, int *retp,
		struct list_head __user *result_up)
{
	struct lstcon_test *test = NULL;
	int rc;
	struct lstcon_group *src_grp = NULL;
	struct lstcon_group *dst_grp = NULL;
	struct lstcon_batch *batch = NULL;

	/*
	 * verify that a batch of the given name exists, and the groups
	 * that will be part of the batch exist and have at least one
	 * active node
	 */
	rc = lstcon_verify_batch(batch_name, &batch);
	if (rc)
		goto out;

	rc = lstcon_verify_group(src_name, &src_grp);
	if (rc)
		goto out;

	rc = lstcon_verify_group(dst_name, &dst_grp);
	if (rc)
		goto out;

	if (dst_grp->grp_userland)
		*retp = 1;

	LIBCFS_ALLOC(test, offsetof(struct lstcon_test, tes_param[paramlen]));
	if (!test) {
		CERROR("Can't allocate test descriptor\n");
		rc = -ENOMEM;

		goto out;
	}

	test->tes_hdr.tsb_id = batch->bat_hdr.tsb_id;
	test->tes_batch	= batch;
	test->tes_type = type;
	test->tes_oneside = 0; /* TODO */
	test->tes_loop = loop;
	test->tes_concur = concur;
	test->tes_stop_onerr = 1; /* TODO */
	test->tes_span = span;
	test->tes_dist = dist;
	test->tes_cliidx = 0; /* just used for creating RPC */
	test->tes_src_grp = src_grp;
	test->tes_dst_grp = dst_grp;
	INIT_LIST_HEAD(&test->tes_trans_list);

	if (param) {
		test->tes_paramlen = paramlen;
		memcpy(&test->tes_param[0], param, paramlen);
	}

	rc = lstcon_test_nodes_add(test, result_up);

	if (rc)
		goto out;

	if (lstcon_trans_stat()->trs_rpc_errno ||
	    lstcon_trans_stat()->trs_fwk_errno)
		CDEBUG(D_NET, "Failed to add test %d to batch %s\n", type,
		       batch_name);

	/* add to test list anyway, so user can check what's going on */
	list_add_tail(&test->tes_link, &batch->bat_test_list);

	batch->bat_ntest++;
	test->tes_hdr.tsb_index = batch->bat_ntest;

	/*  hold groups so nobody can change them */
	return rc;
out:
	if (test)
		LIBCFS_FREE(test, offsetof(struct lstcon_test, tes_param[paramlen]));

	if (dst_grp)
		lstcon_group_decref(dst_grp);

	if (src_grp)
		lstcon_group_decref(src_grp);

	return rc;
}

static int
lstcon_test_find(struct lstcon_batch *batch, int idx,
		 struct lstcon_test **testpp)
{
	struct lstcon_test *test;

	list_for_each_entry(test, &batch->bat_test_list, tes_link) {
		if (idx == test->tes_hdr.tsb_index) {
			*testpp = test;
			return 0;
		}
	}

	return -ENOENT;
}

static int
lstcon_tsbrpc_readent(int transop, struct srpc_msg *msg,
		      lstcon_rpc_ent_t __user *ent_up)
{
	struct srpc_batch_reply *rep = &msg->msg_body.bat_reply;

	LASSERT(transop == LST_TRANS_TSBCLIQRY ||
		transop == LST_TRANS_TSBSRVQRY);

	/* positive errno, framework error code */
	if (copy_to_user(&ent_up->rpe_priv[0], &rep->bar_active,
			 sizeof(rep->bar_active)))
		return -EFAULT;

	return 0;
}

int
lstcon_test_batch_query(char *name, int testidx, int client,
			int timeout, struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	struct list_head *translist;
	struct list_head *ndlist;
	struct lstcon_tsb_hdr *hdr;
	struct lstcon_batch *batch;
	struct lstcon_test *test = NULL;
	int transop;
	int rc;

	rc = lstcon_batch_find(name, &batch);
	if (rc) {
		CDEBUG(D_NET, "Can't find batch: %s\n", name);
		return rc;
	}

	if (!testidx) {
		translist = &batch->bat_trans_list;
		ndlist = &batch->bat_cli_list;
		hdr = &batch->bat_hdr;
	} else {
		/* query specified test only */
		rc = lstcon_test_find(batch, testidx, &test);
		if (rc) {
			CDEBUG(D_NET, "Can't find test: %d\n", testidx);
			return rc;
		}

		translist = &test->tes_trans_list;
		ndlist = &test->tes_src_grp->grp_ndl_list;
		hdr = &test->tes_hdr;
	}

	transop = client ? LST_TRANS_TSBCLIQRY : LST_TRANS_TSBSRVQRY;

	rc = lstcon_rpc_trans_ndlist(ndlist, translist, transop, hdr,
				     lstcon_batrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, timeout);

	/* query a batch, not a test */
	if (!testidx &&
	    !lstcon_rpc_stat_failure(lstcon_trans_stat(), 0) &&
	    !lstcon_tsbqry_stat_run(lstcon_trans_stat(), 0)) {
		/* all RPCs finished, and no active test */
		batch->bat_state = LST_BATCH_IDLE;
	}

	rc = lstcon_rpc_trans_interpreter(trans, result_up,
					  lstcon_tsbrpc_readent);
	lstcon_rpc_trans_destroy(trans);

	return rc;
}

static int
lstcon_statrpc_readent(int transop, struct srpc_msg *msg,
		       lstcon_rpc_ent_t __user *ent_up)
{
	struct srpc_stat_reply *rep = &msg->msg_body.stat_reply;
	sfw_counters_t __user *sfwk_stat;
	srpc_counters_t __user *srpc_stat;
	lnet_counters_t __user *lnet_stat;

	if (rep->str_status)
		return 0;

	sfwk_stat = (sfw_counters_t __user *)&ent_up->rpe_payload[0];
	srpc_stat = (srpc_counters_t __user *)(sfwk_stat + 1);
	lnet_stat = (lnet_counters_t __user *)(srpc_stat + 1);

	if (copy_to_user(sfwk_stat, &rep->str_fw, sizeof(*sfwk_stat)) ||
	    copy_to_user(srpc_stat, &rep->str_rpc, sizeof(*srpc_stat)) ||
	    copy_to_user(lnet_stat, &rep->str_lnet, sizeof(*lnet_stat)))
		return -EFAULT;

	return 0;
}

static int
lstcon_ndlist_stat(struct list_head *ndlist,
		   int timeout, struct list_head __user *result_up)
{
	struct list_head head;
	struct lstcon_rpc_trans *trans;
	int rc;

	INIT_LIST_HEAD(&head);

	rc = lstcon_rpc_trans_ndlist(ndlist, &head,
				     LST_TRANS_STATQRY, NULL, NULL, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_VALIDATE_TIMEOUT(timeout));

	rc = lstcon_rpc_trans_interpreter(trans, result_up,
					  lstcon_statrpc_readent);
	lstcon_rpc_trans_destroy(trans);

	return rc;
}

int
lstcon_group_stat(char *grp_name, int timeout,
		  struct list_head __user *result_up)
{
	struct lstcon_group *grp;
	int rc;

	rc = lstcon_group_find(grp_name, &grp);
	if (rc) {
		CDEBUG(D_NET, "Can't find group %s\n", grp_name);
		return rc;
	}

	rc = lstcon_ndlist_stat(&grp->grp_ndl_list, timeout, result_up);

	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_nodes_stat(int count, lnet_process_id_t __user *ids_up,
		  int timeout, struct list_head __user *result_up)
{
	struct lstcon_ndlink	*ndl;
	struct lstcon_group *tmp;
	lnet_process_id_t id;
	int i;
	int rc;

	rc = lstcon_group_alloc(NULL, &tmp);
	if (rc) {
		CERROR("Out of memory\n");
		return -ENOMEM;
	}

	for (i = 0 ; i < count; i++) {
		if (copy_from_user(&id, &ids_up[i], sizeof(id))) {
			rc = -EFAULT;
			break;
		}

		/* add to tmp group */
		rc = lstcon_group_ndlink_find(tmp, id, &ndl, 2);
		if (rc) {
			CDEBUG((rc == -ENOMEM) ? D_ERROR : D_NET,
			       "Failed to find or create %s: %d\n",
			       libcfs_id2str(id), rc);
			break;
		}
	}

	if (rc) {
		lstcon_group_decref(tmp);
		return rc;
	}

	rc = lstcon_ndlist_stat(&tmp->grp_ndl_list, timeout, result_up);

	lstcon_group_decref(tmp);

	return rc;
}

static int
lstcon_debug_ndlist(struct list_head *ndlist,
		    struct list_head *translist,
		    int timeout, struct list_head __user *result_up)
{
	struct lstcon_rpc_trans *trans;
	int rc;

	rc = lstcon_rpc_trans_ndlist(ndlist, translist, LST_TRANS_SESQRY,
				     NULL, lstcon_sesrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	lstcon_rpc_trans_postwait(trans, LST_VALIDATE_TIMEOUT(timeout));

	rc = lstcon_rpc_trans_interpreter(trans, result_up,
					  lstcon_sesrpc_readent);
	lstcon_rpc_trans_destroy(trans);

	return rc;
}

int
lstcon_session_debug(int timeout, struct list_head __user *result_up)
{
	return lstcon_debug_ndlist(&console_session.ses_ndl_list,
				   NULL, timeout, result_up);
}

int
lstcon_batch_debug(int timeout, char *name,
		   int client, struct list_head __user *result_up)
{
	struct lstcon_batch *bat;
	int rc;

	rc = lstcon_batch_find(name, &bat);
	if (rc)
		return -ENOENT;

	rc = lstcon_debug_ndlist(client ? &bat->bat_cli_list :
					  &bat->bat_srv_list,
				 NULL, timeout, result_up);

	return rc;
}

int
lstcon_group_debug(int timeout, char *name,
		   struct list_head __user *result_up)
{
	struct lstcon_group *grp;
	int rc;

	rc = lstcon_group_find(name, &grp);
	if (rc)
		return -ENOENT;

	rc = lstcon_debug_ndlist(&grp->grp_ndl_list, NULL,
				 timeout, result_up);
	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_nodes_debug(int timeout,
		   int count, lnet_process_id_t __user *ids_up,
		   struct list_head __user *result_up)
{
	lnet_process_id_t id;
	struct lstcon_ndlink *ndl;
	struct lstcon_group *grp;
	int i;
	int rc;

	rc = lstcon_group_alloc(NULL, &grp);
	if (rc) {
		CDEBUG(D_NET, "Out of memory\n");
		return rc;
	}

	for (i = 0; i < count; i++) {
		if (copy_from_user(&id, &ids_up[i], sizeof(id))) {
			rc = -EFAULT;
			break;
		}

		/* node is added to tmp group */
		rc = lstcon_group_ndlink_find(grp, id, &ndl, 1);
		if (rc) {
			CERROR("Can't create node link\n");
			break;
		}
	}

	if (rc) {
		lstcon_group_decref(grp);
		return rc;
	}

	rc = lstcon_debug_ndlist(&grp->grp_ndl_list, NULL,
				 timeout, result_up);

	lstcon_group_decref(grp);

	return rc;
}

int
lstcon_session_match(lst_sid_t sid)
{
	return (console_session.ses_id.ses_nid == sid.ses_nid &&
		console_session.ses_id.ses_stamp == sid.ses_stamp) ? 1 : 0;
}

static void
lstcon_new_session_id(lst_sid_t *sid)
{
	lnet_process_id_t id;

	LASSERT(console_session.ses_state == LST_SESSION_NONE);

	LNetGetId(1, &id);
	sid->ses_nid = id.nid;
	sid->ses_stamp = cfs_time_current();
}

int
lstcon_session_new(char *name, int key, unsigned int feats,
		   int timeout, int force, lst_sid_t __user *sid_up)
{
	int rc = 0;
	int i;

	if (console_session.ses_state != LST_SESSION_NONE) {
		/* session exists */
		if (!force) {
			CNETERR("Session %s already exists\n",
				console_session.ses_name);
			return -EEXIST;
		}

		rc = lstcon_session_end();

		/* lstcon_session_end() only return local error */
		if (rc)
			return rc;
	}

	if (feats & ~LST_FEATS_MASK) {
		CNETERR("Unknown session features %x\n",
			(feats & ~LST_FEATS_MASK));
		return -EINVAL;
	}

	for (i = 0; i < LST_GLOBAL_HASHSIZE; i++)
		LASSERT(list_empty(&console_session.ses_ndl_hash[i]));

	lstcon_new_session_id(&console_session.ses_id);

	console_session.ses_key = key;
	console_session.ses_state = LST_SESSION_ACTIVE;
	console_session.ses_force = !!force;
	console_session.ses_features = feats;
	console_session.ses_feats_updated = 0;
	console_session.ses_timeout = (timeout <= 0) ?
				      LST_CONSOLE_TIMEOUT : timeout;

	if (strlen(name) > sizeof(console_session.ses_name) - 1)
		return -E2BIG;
	strlcpy(console_session.ses_name, name,
		sizeof(console_session.ses_name));

	rc = lstcon_batch_add(LST_DEFAULT_BATCH);
	if (rc)
		return rc;

	rc = lstcon_rpc_pinger_start();
	if (rc) {
		struct lstcon_batch *bat = NULL;

		lstcon_batch_find(LST_DEFAULT_BATCH, &bat);
		lstcon_batch_destroy(bat);

		return rc;
	}

	if (!copy_to_user(sid_up, &console_session.ses_id,
			  sizeof(lst_sid_t)))
		return rc;

	lstcon_session_end();

	return -EFAULT;
}

int
lstcon_session_info(lst_sid_t __user *sid_up, int __user *key_up,
		    unsigned __user *featp,
		    lstcon_ndlist_ent_t __user *ndinfo_up,
		    char __user *name_up, int len)
{
	lstcon_ndlist_ent_t *entp;
	struct lstcon_ndlink *ndl;
	int rc = 0;

	if (console_session.ses_state != LST_SESSION_ACTIVE)
		return -ESRCH;

	LIBCFS_ALLOC(entp, sizeof(*entp));
	if (!entp)
		return -ENOMEM;

	list_for_each_entry(ndl, &console_session.ses_ndl_list, ndl_link)
		LST_NODE_STATE_COUNTER(ndl->ndl_node, entp);

	if (copy_to_user(sid_up, &console_session.ses_id,
			 sizeof(lst_sid_t)) ||
	    copy_to_user(key_up, &console_session.ses_key,
			 sizeof(*key_up)) ||
	    copy_to_user(featp, &console_session.ses_features,
			 sizeof(*featp)) ||
	    copy_to_user(ndinfo_up, entp, sizeof(*entp)) ||
	    copy_to_user(name_up, console_session.ses_name, len))
		rc = -EFAULT;

	LIBCFS_FREE(entp, sizeof(*entp));

	return rc;
}

int
lstcon_session_end(void)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_group *grp;
	struct lstcon_batch *bat;
	int rc = 0;

	LASSERT(console_session.ses_state == LST_SESSION_ACTIVE);

	rc = lstcon_rpc_trans_ndlist(&console_session.ses_ndl_list,
				     NULL, LST_TRANS_SESEND, NULL,
				     lstcon_sesrpc_condition, &trans);
	if (rc) {
		CERROR("Can't create transaction: %d\n", rc);
		return rc;
	}

	console_session.ses_shutdown = 1;

	lstcon_rpc_pinger_stop();

	lstcon_rpc_trans_postwait(trans, LST_TRANS_TIMEOUT);

	lstcon_rpc_trans_destroy(trans);
	/* User can do nothing even rpc failed, so go on */

	/* waiting for orphan rpcs to die */
	lstcon_rpc_cleanup_wait();

	console_session.ses_id = LST_INVALID_SID;
	console_session.ses_state = LST_SESSION_NONE;
	console_session.ses_key = 0;
	console_session.ses_force = 0;
	console_session.ses_feats_updated = 0;

	/* destroy all batches */
	while (!list_empty(&console_session.ses_bat_list)) {
		bat = list_entry(console_session.ses_bat_list.next,
				 struct lstcon_batch, bat_link);

		lstcon_batch_destroy(bat);
	}

	/* destroy all groups */
	while (!list_empty(&console_session.ses_grp_list)) {
		grp = list_entry(console_session.ses_grp_list.next,
				 struct lstcon_group, grp_link);
		LASSERT(grp->grp_ref == 1);

		lstcon_group_decref(grp);
	}

	/* all nodes should be released */
	LASSERT(list_empty(&console_session.ses_ndl_list));

	console_session.ses_shutdown = 0;
	console_session.ses_expired = 0;

	return rc;
}

int
lstcon_session_feats_check(unsigned int feats)
{
	int rc = 0;

	if (feats & ~LST_FEATS_MASK) {
		CERROR("Can't support these features: %x\n",
		       (feats & ~LST_FEATS_MASK));
		return -EPROTO;
	}

	spin_lock(&console_session.ses_rpc_lock);

	if (!console_session.ses_feats_updated) {
		console_session.ses_feats_updated = 1;
		console_session.ses_features = feats;
	}

	if (console_session.ses_features != feats)
		rc = -EPROTO;

	spin_unlock(&console_session.ses_rpc_lock);

	if (rc) {
		CERROR("remote features %x do not match with session features %x of console\n",
		       feats, console_session.ses_features);
	}

	return rc;
}

static int
lstcon_acceptor_handle(struct srpc_server_rpc *rpc)
{
	struct srpc_msg *rep	= &rpc->srpc_replymsg;
	struct srpc_msg *req	= &rpc->srpc_reqstbuf->buf_msg;
	struct srpc_join_reqst *jreq = &req->msg_body.join_reqst;
	struct srpc_join_reply *jrep = &rep->msg_body.join_reply;
	struct lstcon_group *grp = NULL;
	struct lstcon_ndlink *ndl;
	int rc = 0;

	sfw_unpack_message(req);

	mutex_lock(&console_session.ses_mutex);

	jrep->join_sid = console_session.ses_id;

	if (console_session.ses_id.ses_nid == LNET_NID_ANY) {
		jrep->join_status = ESRCH;
		goto out;
	}

	if (lstcon_session_feats_check(req->msg_ses_feats)) {
		jrep->join_status = EPROTO;
		goto out;
	}

	if (jreq->join_sid.ses_nid != LNET_NID_ANY &&
	    !lstcon_session_match(jreq->join_sid)) {
		jrep->join_status = EBUSY;
		goto out;
	}

	if (lstcon_group_find(jreq->join_group, &grp)) {
		rc = lstcon_group_alloc(jreq->join_group, &grp);
		if (rc) {
			CERROR("Out of memory\n");
			goto out;
		}

		list_add_tail(&grp->grp_link,
			      &console_session.ses_grp_list);
		lstcon_group_addref(grp);
	}

	if (grp->grp_ref > 2) {
		/* Group in using */
		jrep->join_status = EBUSY;
		goto out;
	}

	rc = lstcon_group_ndlink_find(grp, rpc->srpc_peer, &ndl, 0);
	if (!rc) {
		jrep->join_status = EEXIST;
		goto out;
	}

	rc = lstcon_group_ndlink_find(grp, rpc->srpc_peer, &ndl, 1);
	if (rc) {
		CERROR("Out of memory\n");
		goto out;
	}

	ndl->ndl_node->nd_state = LST_NODE_ACTIVE;
	ndl->ndl_node->nd_timeout = console_session.ses_timeout;

	if (!grp->grp_userland)
		grp->grp_userland = 1;

	strlcpy(jrep->join_session, console_session.ses_name,
		sizeof(jrep->join_session));
	jrep->join_timeout = console_session.ses_timeout;
	jrep->join_status = 0;

out:
	rep->msg_ses_feats = console_session.ses_features;
	if (grp)
		lstcon_group_decref(grp);

	mutex_unlock(&console_session.ses_mutex);

	return rc;
}

static struct srpc_service lstcon_acceptor_service;

static void lstcon_init_acceptor_service(void)
{
	/* initialize selftest console acceptor service table */
	lstcon_acceptor_service.sv_name = "join session";
	lstcon_acceptor_service.sv_handler = lstcon_acceptor_handle;
	lstcon_acceptor_service.sv_id = SRPC_SERVICE_JOIN;
	lstcon_acceptor_service.sv_wi_total = SFW_FRWK_WI_MAX;
}

static DECLARE_IOCTL_HANDLER(lstcon_ioctl_handler, lstcon_ioctl_entry);

/* initialize console */
int
lstcon_console_init(void)
{
	int i;
	int rc;

	memset(&console_session, 0, sizeof(struct lstcon_session));

	console_session.ses_id = LST_INVALID_SID;
	console_session.ses_state = LST_SESSION_NONE;
	console_session.ses_timeout = 0;
	console_session.ses_force = 0;
	console_session.ses_expired = 0;
	console_session.ses_feats_updated = 0;
	console_session.ses_features = LST_FEATS_MASK;
	console_session.ses_laststamp = ktime_get_real_seconds();

	mutex_init(&console_session.ses_mutex);

	INIT_LIST_HEAD(&console_session.ses_ndl_list);
	INIT_LIST_HEAD(&console_session.ses_grp_list);
	INIT_LIST_HEAD(&console_session.ses_bat_list);
	INIT_LIST_HEAD(&console_session.ses_trans_list);

	LIBCFS_ALLOC(console_session.ses_ndl_hash,
		     sizeof(struct list_head) * LST_GLOBAL_HASHSIZE);
	if (!console_session.ses_ndl_hash)
		return -ENOMEM;

	for (i = 0; i < LST_GLOBAL_HASHSIZE; i++)
		INIT_LIST_HEAD(&console_session.ses_ndl_hash[i]);

	/* initialize acceptor service table */
	lstcon_init_acceptor_service();

	rc = srpc_add_service(&lstcon_acceptor_service);
	LASSERT(rc != -EBUSY);
	if (rc) {
		LIBCFS_FREE(console_session.ses_ndl_hash,
			    sizeof(struct list_head) * LST_GLOBAL_HASHSIZE);
		return rc;
	}

	rc = srpc_service_add_buffers(&lstcon_acceptor_service,
				      lstcon_acceptor_service.sv_wi_total);
	if (rc) {
		rc = -ENOMEM;
		goto out;
	}

	rc = libcfs_register_ioctl(&lstcon_ioctl_handler);

	if (!rc) {
		lstcon_rpc_module_init();
		return 0;
	}

out:
	srpc_shutdown_service(&lstcon_acceptor_service);
	srpc_remove_service(&lstcon_acceptor_service);

	LIBCFS_FREE(console_session.ses_ndl_hash,
		    sizeof(struct list_head) * LST_GLOBAL_HASHSIZE);

	srpc_wait_service_shutdown(&lstcon_acceptor_service);

	return rc;
}

int
lstcon_console_fini(void)
{
	int i;

	libcfs_deregister_ioctl(&lstcon_ioctl_handler);

	mutex_lock(&console_session.ses_mutex);

	srpc_shutdown_service(&lstcon_acceptor_service);
	srpc_remove_service(&lstcon_acceptor_service);

	if (console_session.ses_state != LST_SESSION_NONE)
		lstcon_session_end();

	lstcon_rpc_module_fini();

	mutex_unlock(&console_session.ses_mutex);

	LASSERT(list_empty(&console_session.ses_ndl_list));
	LASSERT(list_empty(&console_session.ses_grp_list));
	LASSERT(list_empty(&console_session.ses_bat_list));
	LASSERT(list_empty(&console_session.ses_trans_list));

	for (i = 0; i < LST_NODE_HASHSIZE; i++)
		LASSERT(list_empty(&console_session.ses_ndl_hash[i]));

	LIBCFS_FREE(console_session.ses_ndl_hash,
		    sizeof(struct list_head) * LST_GLOBAL_HASHSIZE);

	srpc_wait_service_shutdown(&lstcon_acceptor_service);

	return 0;
}
