/*
 * Copyright (c) 2010 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* XXX TBD some includes may be extraneous */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/hash.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/kref.h>
#include <asm/unaligned.h>
#include <scsi/libfc.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "tcm_fc.h"

#define TFC_SESS_DBG(lport, fmt, args...) \
	pr_debug("host%u: rport %6.6x: " fmt,	   \
		 (lport)->host->host_no,	   \
		 (lport)->port_id, ##args )

static void ft_sess_delete_all(struct ft_tport *);

/*
 * Lookup or allocate target local port.
 * Caller holds ft_lport_lock.
 */
static struct ft_tport *ft_tport_get(struct fc_lport *lport)
{
	struct ft_tpg *tpg;
	struct ft_tport *tport;
	int i;

	tport = rcu_dereference_protected(lport->prov[FC_TYPE_FCP],
					  lockdep_is_held(&ft_lport_lock));
	if (tport && tport->tpg)
		return tport;

	tpg = ft_lport_find_tpg(lport);
	if (!tpg)
		return NULL;

	if (tport) {
		tport->tpg = tpg;
		tpg->tport = tport;
		return tport;
	}

	tport = kzalloc(sizeof(*tport), GFP_KERNEL);
	if (!tport)
		return NULL;

	tport->lport = lport;
	tport->tpg = tpg;
	tpg->tport = tport;
	for (i = 0; i < FT_SESS_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&tport->hash[i]);

	rcu_assign_pointer(lport->prov[FC_TYPE_FCP], tport);
	return tport;
}

/*
 * Delete a target local port.
 * Caller holds ft_lport_lock.
 */
static void ft_tport_delete(struct ft_tport *tport)
{
	struct fc_lport *lport;
	struct ft_tpg *tpg;

	ft_sess_delete_all(tport);
	lport = tport->lport;
	lport->service_params &= ~FCP_SPPF_TARG_FCN;
	BUG_ON(tport != lport->prov[FC_TYPE_FCP]);
	RCU_INIT_POINTER(lport->prov[FC_TYPE_FCP], NULL);

	tpg = tport->tpg;
	if (tpg) {
		tpg->tport = NULL;
		tport->tpg = NULL;
	}
	kfree_rcu(tport, rcu);
}

/*
 * Add local port.
 * Called thru fc_lport_iterate().
 */
void ft_lport_add(struct fc_lport *lport, void *arg)
{
	mutex_lock(&ft_lport_lock);
	ft_tport_get(lport);
	lport->service_params |= FCP_SPPF_TARG_FCN;
	mutex_unlock(&ft_lport_lock);
}

/*
 * Delete local port.
 * Called thru fc_lport_iterate().
 */
void ft_lport_del(struct fc_lport *lport, void *arg)
{
	struct ft_tport *tport;

	mutex_lock(&ft_lport_lock);
	tport = lport->prov[FC_TYPE_FCP];
	if (tport)
		ft_tport_delete(tport);
	mutex_unlock(&ft_lport_lock);
}

/*
 * Notification of local port change from libfc.
 * Create or delete local port and associated tport.
 */
int ft_lport_notify(struct notifier_block *nb, unsigned long event, void *arg)
{
	struct fc_lport *lport = arg;

	switch (event) {
	case FC_LPORT_EV_ADD:
		ft_lport_add(lport, NULL);
		break;
	case FC_LPORT_EV_DEL:
		ft_lport_del(lport, NULL);
		break;
	}
	return NOTIFY_DONE;
}

/*
 * Hash function for FC_IDs.
 */
static u32 ft_sess_hash(u32 port_id)
{
	return hash_32(port_id, FT_SESS_HASH_BITS);
}

/*
 * Find session in local port.
 * Sessions and hash lists are RCU-protected.
 * A reference is taken which must be eventually freed.
 */
static struct ft_sess *ft_sess_get(struct fc_lport *lport, u32 port_id)
{
	struct ft_tport *tport;
	struct hlist_head *head;
	struct ft_sess *sess;
	char *reason = "no session created";

	rcu_read_lock();
	tport = rcu_dereference(lport->prov[FC_TYPE_FCP]);
	if (!tport) {
		reason = "not an FCP port";
		goto out;
	}

	head = &tport->hash[ft_sess_hash(port_id)];
	hlist_for_each_entry_rcu(sess, head, hash) {
		if (sess->port_id == port_id) {
			kref_get(&sess->kref);
			rcu_read_unlock();
			TFC_SESS_DBG(lport, "port_id %x found %p\n",
				     port_id, sess);
			return sess;
		}
	}
out:
	rcu_read_unlock();
	TFC_SESS_DBG(lport, "port_id %x not found, %s\n",
		     port_id, reason);
	return NULL;
}

static int ft_sess_alloc_cb(struct se_portal_group *se_tpg,
			    struct se_session *se_sess, void *p)
{
	struct ft_sess *sess = p;
	struct ft_tport *tport = sess->tport;
	struct hlist_head *head = &tport->hash[ft_sess_hash(sess->port_id)];

	TFC_SESS_DBG(tport->lport, "port_id %x sess %p\n", sess->port_id, sess);
	hlist_add_head_rcu(&sess->hash, head);
	tport->sess_count++;

	return 0;
}

/*
 * Allocate session and enter it in the hash for the local port.
 * Caller holds ft_lport_lock.
 */
static struct ft_sess *ft_sess_create(struct ft_tport *tport, u32 port_id,
				      struct fc_rport_priv *rdata)
{
	struct se_portal_group *se_tpg = &tport->tpg->se_tpg;
	struct ft_sess *sess;
	struct hlist_head *head;
	unsigned char initiatorname[TRANSPORT_IQN_LEN];

	ft_format_wwn(&initiatorname[0], TRANSPORT_IQN_LEN, rdata->ids.port_name);

	head = &tport->hash[ft_sess_hash(port_id)];
	hlist_for_each_entry_rcu(sess, head, hash)
		if (sess->port_id == port_id)
			return sess;

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess)
		return ERR_PTR(-ENOMEM);

	kref_init(&sess->kref); /* ref for table entry */
	sess->tport = tport;
	sess->port_id = port_id;

	sess->se_sess = target_setup_session(se_tpg, TCM_FC_DEFAULT_TAGS,
					     sizeof(struct ft_cmd),
					     TARGET_PROT_NORMAL, &initiatorname[0],
					     sess, ft_sess_alloc_cb);
	if (IS_ERR(sess->se_sess)) {
		int rc = PTR_ERR(sess->se_sess);
		kfree(sess);
		sess = ERR_PTR(rc);
	}
	return sess;
}

/*
 * Unhash the session.
 * Caller holds ft_lport_lock.
 */
static void ft_sess_unhash(struct ft_sess *sess)
{
	struct ft_tport *tport = sess->tport;

	hlist_del_rcu(&sess->hash);
	BUG_ON(!tport->sess_count);
	tport->sess_count--;
	sess->port_id = -1;
	sess->params = 0;
}

/*
 * Delete session from hash.
 * Caller holds ft_lport_lock.
 */
static struct ft_sess *ft_sess_delete(struct ft_tport *tport, u32 port_id)
{
	struct hlist_head *head;
	struct ft_sess *sess;

	head = &tport->hash[ft_sess_hash(port_id)];
	hlist_for_each_entry_rcu(sess, head, hash) {
		if (sess->port_id == port_id) {
			ft_sess_unhash(sess);
			return sess;
		}
	}
	return NULL;
}

static void ft_close_sess(struct ft_sess *sess)
{
	target_sess_cmd_list_set_waiting(sess->se_sess);
	target_wait_for_sess_cmds(sess->se_sess);
	ft_sess_put(sess);
}

/*
 * Delete all sessions from tport.
 * Caller holds ft_lport_lock.
 */
static void ft_sess_delete_all(struct ft_tport *tport)
{
	struct hlist_head *head;
	struct ft_sess *sess;

	for (head = tport->hash;
	     head < &tport->hash[FT_SESS_HASH_SIZE]; head++) {
		hlist_for_each_entry_rcu(sess, head, hash) {
			ft_sess_unhash(sess);
			ft_close_sess(sess);	/* release from table */
		}
	}
}

/*
 * TCM ops for sessions.
 */

/*
 * Remove session and send PRLO.
 * This is called when the ACL is being deleted or queue depth is changing.
 */
void ft_sess_close(struct se_session *se_sess)
{
	struct ft_sess *sess = se_sess->fabric_sess_ptr;
	u32 port_id;

	mutex_lock(&ft_lport_lock);
	port_id = sess->port_id;
	if (port_id == -1) {
		mutex_unlock(&ft_lport_lock);
		return;
	}
	TFC_SESS_DBG(sess->tport->lport, "port_id %x close session\n", port_id);
	ft_sess_unhash(sess);
	mutex_unlock(&ft_lport_lock);
	ft_close_sess(sess);
	/* XXX Send LOGO or PRLO */
	synchronize_rcu();		/* let transport deregister happen */
}

u32 ft_sess_get_index(struct se_session *se_sess)
{
	struct ft_sess *sess = se_sess->fabric_sess_ptr;

	return sess->port_id;	/* XXX TBD probably not what is needed */
}

u32 ft_sess_get_port_name(struct se_session *se_sess,
			  unsigned char *buf, u32 len)
{
	struct ft_sess *sess = se_sess->fabric_sess_ptr;

	return ft_format_wwn(buf, len, sess->port_name);
}

/*
 * libfc ops involving sessions.
 */

static int ft_prli_locked(struct fc_rport_priv *rdata, u32 spp_len,
			  const struct fc_els_spp *rspp, struct fc_els_spp *spp)
{
	struct ft_tport *tport;
	struct ft_sess *sess;
	u32 fcp_parm;

	tport = ft_tport_get(rdata->local_port);
	if (!tport)
		goto not_target;	/* not a target for this local port */

	if (!rspp)
		goto fill;

	if (rspp->spp_flags & (FC_SPP_OPA_VAL | FC_SPP_RPA_VAL))
		return FC_SPP_RESP_NO_PA;

	/*
	 * If both target and initiator bits are off, the SPP is invalid.
	 */
	fcp_parm = ntohl(rspp->spp_params);
	if (!(fcp_parm & (FCP_SPPF_INIT_FCN | FCP_SPPF_TARG_FCN)))
		return FC_SPP_RESP_INVL;

	/*
	 * Create session (image pair) only if requested by
	 * EST_IMG_PAIR flag and if the requestor is an initiator.
	 */
	if (rspp->spp_flags & FC_SPP_EST_IMG_PAIR) {
		spp->spp_flags |= FC_SPP_EST_IMG_PAIR;
		if (!(fcp_parm & FCP_SPPF_INIT_FCN))
			return FC_SPP_RESP_CONF;
		sess = ft_sess_create(tport, rdata->ids.port_id, rdata);
		if (IS_ERR(sess)) {
			if (PTR_ERR(sess) == -EACCES) {
				spp->spp_flags &= ~FC_SPP_EST_IMG_PAIR;
				return FC_SPP_RESP_CONF;
			} else
				return FC_SPP_RESP_RES;
		}
		if (!sess->params)
			rdata->prli_count++;
		sess->params = fcp_parm;
		sess->port_name = rdata->ids.port_name;
		sess->max_frame = rdata->maxframe_size;

		/* XXX TBD - clearing actions.  unit attn, see 4.10 */
	}

	/*
	 * OR in our service parameters with other provider (initiator), if any.
	 */
fill:
	fcp_parm = ntohl(spp->spp_params);
	fcp_parm &= ~FCP_SPPF_RETRY;
	spp->spp_params = htonl(fcp_parm | FCP_SPPF_TARG_FCN);
	return FC_SPP_RESP_ACK;

not_target:
	fcp_parm = ntohl(spp->spp_params);
	fcp_parm &= ~FCP_SPPF_TARG_FCN;
	spp->spp_params = htonl(fcp_parm);
	return 0;
}

/**
 * tcm_fcp_prli() - Handle incoming or outgoing PRLI for the FCP target
 * @rdata: remote port private
 * @spp_len: service parameter page length
 * @rspp: received service parameter page (NULL for outgoing PRLI)
 * @spp: response service parameter page
 *
 * Returns spp response code.
 */
static int ft_prli(struct fc_rport_priv *rdata, u32 spp_len,
		   const struct fc_els_spp *rspp, struct fc_els_spp *spp)
{
	int ret;

	mutex_lock(&ft_lport_lock);
	ret = ft_prli_locked(rdata, spp_len, rspp, spp);
	mutex_unlock(&ft_lport_lock);
	TFC_SESS_DBG(rdata->local_port, "port_id %x flags %x ret %x\n",
		     rdata->ids.port_id, rspp ? rspp->spp_flags : 0, ret);
	return ret;
}

static void ft_sess_free(struct kref *kref)
{
	struct ft_sess *sess = container_of(kref, struct ft_sess, kref);

	target_remove_session(sess->se_sess);
	kfree_rcu(sess, rcu);
}

void ft_sess_put(struct ft_sess *sess)
{
	int sess_held = kref_read(&sess->kref);

	BUG_ON(!sess_held);
	kref_put(&sess->kref, ft_sess_free);
}

static void ft_prlo(struct fc_rport_priv *rdata)
{
	struct ft_sess *sess;
	struct ft_tport *tport;

	mutex_lock(&ft_lport_lock);
	tport = rcu_dereference_protected(rdata->local_port->prov[FC_TYPE_FCP],
					  lockdep_is_held(&ft_lport_lock));

	if (!tport) {
		mutex_unlock(&ft_lport_lock);
		return;
	}
	sess = ft_sess_delete(tport, rdata->ids.port_id);
	if (!sess) {
		mutex_unlock(&ft_lport_lock);
		return;
	}
	mutex_unlock(&ft_lport_lock);
	ft_close_sess(sess);		/* release from table */
	rdata->prli_count--;
	/* XXX TBD - clearing actions.  unit attn, see 4.10 */
}

/*
 * Handle incoming FCP request.
 * Caller has verified that the frame is type FCP.
 */
static void ft_recv(struct fc_lport *lport, struct fc_frame *fp)
{
	struct ft_sess *sess;
	u32 sid = fc_frame_sid(fp);

	TFC_SESS_DBG(lport, "recv sid %x\n", sid);

	sess = ft_sess_get(lport, sid);
	if (!sess) {
		TFC_SESS_DBG(lport, "sid %x sess lookup failed\n", sid);
		/* TBD XXX - if FCP_CMND, send PRLO */
		fc_frame_free(fp);
		return;
	}
	ft_recv_req(sess, fp);	/* must do ft_sess_put() */
}

/*
 * Provider ops for libfc.
 */
struct fc4_prov ft_prov = {
	.prli = ft_prli,
	.prlo = ft_prlo,
	.recv = ft_recv,
	.module = THIS_MODULE,
};
