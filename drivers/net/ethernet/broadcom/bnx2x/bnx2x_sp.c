/* bnx2x_sp.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2011-2012 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Vladislav Zolotarov
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/crc32c.h>
#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include "bnx2x_sp.h"

#define BNX2X_MAX_EMUL_MULTI		16

#define MAC_LEADING_ZERO_CNT (ALIGN(ETH_ALEN, sizeof(u32)) - ETH_ALEN)

/**** Exe Queue interfaces ****/

/**
 * bnx2x_exe_queue_init - init the Exe Queue object
 *
 * @o:		poiter to the object
 * @exe_len:	length
 * @owner:	poiter to the owner
 * @validate:	validate function pointer
 * @optimize:	optimize function pointer
 * @exec:	execute function pointer
 * @get:	get function pointer
 */
static inline void bnx2x_exe_queue_init(struct bnx2x *bp,
					struct bnx2x_exe_queue_obj *o,
					int exe_len,
					union bnx2x_qable_obj *owner,
					exe_q_validate validate,
					exe_q_remove remove,
					exe_q_optimize optimize,
					exe_q_execute exec,
					exe_q_get get)
{
	memset(o, 0, sizeof(*o));

	INIT_LIST_HEAD(&o->exe_queue);
	INIT_LIST_HEAD(&o->pending_comp);

	spin_lock_init(&o->lock);

	o->exe_chunk_len = exe_len;
	o->owner         = owner;

	/* Owner specific callbacks */
	o->validate      = validate;
	o->remove        = remove;
	o->optimize      = optimize;
	o->execute       = exec;
	o->get           = get;

	DP(BNX2X_MSG_SP, "Setup the execution queue with the chunk length of %d\n",
	   exe_len);
}

static inline void bnx2x_exe_queue_free_elem(struct bnx2x *bp,
					     struct bnx2x_exeq_elem *elem)
{
	DP(BNX2X_MSG_SP, "Deleting an exe_queue element\n");
	kfree(elem);
}

static inline int bnx2x_exe_queue_length(struct bnx2x_exe_queue_obj *o)
{
	struct bnx2x_exeq_elem *elem;
	int cnt = 0;

	spin_lock_bh(&o->lock);

	list_for_each_entry(elem, &o->exe_queue, link)
		cnt++;

	spin_unlock_bh(&o->lock);

	return cnt;
}

/**
 * bnx2x_exe_queue_add - add a new element to the execution queue
 *
 * @bp:		driver handle
 * @o:		queue
 * @cmd:	new command to add
 * @restore:	true - do not optimize the command
 *
 * If the element is optimized or is illegal, frees it.
 */
static inline int bnx2x_exe_queue_add(struct bnx2x *bp,
				      struct bnx2x_exe_queue_obj *o,
				      struct bnx2x_exeq_elem *elem,
				      bool restore)
{
	int rc;

	spin_lock_bh(&o->lock);

	if (!restore) {
		/* Try to cancel this element queue */
		rc = o->optimize(bp, o->owner, elem);
		if (rc)
			goto free_and_exit;

		/* Check if this request is ok */
		rc = o->validate(bp, o->owner, elem);
		if (rc) {
			BNX2X_ERR("Preamble failed: %d\n", rc);
			goto free_and_exit;
		}
	}

	/* If so, add it to the execution queue */
	list_add_tail(&elem->link, &o->exe_queue);

	spin_unlock_bh(&o->lock);

	return 0;

free_and_exit:
	bnx2x_exe_queue_free_elem(bp, elem);

	spin_unlock_bh(&o->lock);

	return rc;

}

static inline void __bnx2x_exe_queue_reset_pending(
	struct bnx2x *bp,
	struct bnx2x_exe_queue_obj *o)
{
	struct bnx2x_exeq_elem *elem;

	while (!list_empty(&o->pending_comp)) {
		elem = list_first_entry(&o->pending_comp,
					struct bnx2x_exeq_elem, link);

		list_del(&elem->link);
		bnx2x_exe_queue_free_elem(bp, elem);
	}
}

static inline void bnx2x_exe_queue_reset_pending(struct bnx2x *bp,
						 struct bnx2x_exe_queue_obj *o)
{

	spin_lock_bh(&o->lock);

	__bnx2x_exe_queue_reset_pending(bp, o);

	spin_unlock_bh(&o->lock);

}

/**
 * bnx2x_exe_queue_step - execute one execution chunk atomically
 *
 * @bp:			driver handle
 * @o:			queue
 * @ramrod_flags:	flags
 *
 * (Atomicy is ensured using the exe_queue->lock).
 */
static inline int bnx2x_exe_queue_step(struct bnx2x *bp,
				       struct bnx2x_exe_queue_obj *o,
				       unsigned long *ramrod_flags)
{
	struct bnx2x_exeq_elem *elem, spacer;
	int cur_len = 0, rc;

	memset(&spacer, 0, sizeof(spacer));

	spin_lock_bh(&o->lock);

	/*
	 * Next step should not be performed until the current is finished,
	 * unless a DRV_CLEAR_ONLY bit is set. In this case we just want to
	 * properly clear object internals without sending any command to the FW
	 * which also implies there won't be any completion to clear the
	 * 'pending' list.
	 */
	if (!list_empty(&o->pending_comp)) {
		if (test_bit(RAMROD_DRV_CLR_ONLY, ramrod_flags)) {
			DP(BNX2X_MSG_SP, "RAMROD_DRV_CLR_ONLY requested: resetting a pending_comp list\n");
			__bnx2x_exe_queue_reset_pending(bp, o);
		} else {
			spin_unlock_bh(&o->lock);
			return 1;
		}
	}

	/*
	 * Run through the pending commands list and create a next
	 * execution chunk.
	 */
	while (!list_empty(&o->exe_queue)) {
		elem = list_first_entry(&o->exe_queue, struct bnx2x_exeq_elem,
					link);
		WARN_ON(!elem->cmd_len);

		if (cur_len + elem->cmd_len <= o->exe_chunk_len) {
			cur_len += elem->cmd_len;
			/*
			 * Prevent from both lists being empty when moving an
			 * element. This will allow the call of
			 * bnx2x_exe_queue_empty() without locking.
			 */
			list_add_tail(&spacer.link, &o->pending_comp);
			mb();
			list_move_tail(&elem->link, &o->pending_comp);
			list_del(&spacer.link);
		} else
			break;
	}

	/* Sanity check */
	if (!cur_len) {
		spin_unlock_bh(&o->lock);
		return 0;
	}

	rc = o->execute(bp, o->owner, &o->pending_comp, ramrod_flags);
	if (rc < 0)
		/*
		 *  In case of an error return the commands back to the queue
		 *  and reset the pending_comp.
		 */
		list_splice_init(&o->pending_comp, &o->exe_queue);
	else if (!rc)
		/*
		 * If zero is returned, means there are no outstanding pending
		 * completions and we may dismiss the pending list.
		 */
		__bnx2x_exe_queue_reset_pending(bp, o);

	spin_unlock_bh(&o->lock);
	return rc;
}

static inline bool bnx2x_exe_queue_empty(struct bnx2x_exe_queue_obj *o)
{
	bool empty = list_empty(&o->exe_queue);

	/* Don't reorder!!! */
	mb();

	return empty && list_empty(&o->pending_comp);
}

static inline struct bnx2x_exeq_elem *bnx2x_exe_queue_alloc_elem(
	struct bnx2x *bp)
{
	DP(BNX2X_MSG_SP, "Allocating a new exe_queue element\n");
	return kzalloc(sizeof(struct bnx2x_exeq_elem), GFP_ATOMIC);
}

/************************ raw_obj functions ***********************************/
static bool bnx2x_raw_check_pending(struct bnx2x_raw_obj *o)
{
	return !!test_bit(o->state, o->pstate);
}

static void bnx2x_raw_clear_pending(struct bnx2x_raw_obj *o)
{
	smp_mb__before_clear_bit();
	clear_bit(o->state, o->pstate);
	smp_mb__after_clear_bit();
}

static void bnx2x_raw_set_pending(struct bnx2x_raw_obj *o)
{
	smp_mb__before_clear_bit();
	set_bit(o->state, o->pstate);
	smp_mb__after_clear_bit();
}

/**
 * bnx2x_state_wait - wait until the given bit(state) is cleared
 *
 * @bp:		device handle
 * @state:	state which is to be cleared
 * @state_p:	state buffer
 *
 */
static inline int bnx2x_state_wait(struct bnx2x *bp, int state,
				   unsigned long *pstate)
{
	/* can take a while if any port is running */
	int cnt = 5000;


	if (CHIP_REV_IS_EMUL(bp))
		cnt *= 20;

	DP(BNX2X_MSG_SP, "waiting for state to become %d\n", state);

	might_sleep();
	while (cnt--) {
		if (!test_bit(state, pstate)) {
#ifdef BNX2X_STOP_ON_ERROR
			DP(BNX2X_MSG_SP, "exit  (cnt %d)\n", 5000 - cnt);
#endif
			return 0;
		}

		usleep_range(1000, 1000);

		if (bp->panic)
			return -EIO;
	}

	/* timeout! */
	BNX2X_ERR("timeout waiting for state %d\n", state);
#ifdef BNX2X_STOP_ON_ERROR
	bnx2x_panic();
#endif

	return -EBUSY;
}

static int bnx2x_raw_wait(struct bnx2x *bp, struct bnx2x_raw_obj *raw)
{
	return bnx2x_state_wait(bp, raw->state, raw->pstate);
}

/***************** Classification verbs: Set/Del MAC/VLAN/VLAN-MAC ************/
/* credit handling callbacks */
static bool bnx2x_get_cam_offset_mac(struct bnx2x_vlan_mac_obj *o, int *offset)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;

	WARN_ON(!mp);

	return mp->get_entry(mp, offset);
}

static bool bnx2x_get_credit_mac(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;

	WARN_ON(!mp);

	return mp->get(mp, 1);
}

static bool bnx2x_get_cam_offset_vlan(struct bnx2x_vlan_mac_obj *o, int *offset)
{
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	WARN_ON(!vp);

	return vp->get_entry(vp, offset);
}

static bool bnx2x_get_credit_vlan(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	WARN_ON(!vp);

	return vp->get(vp, 1);
}

static bool bnx2x_get_credit_vlan_mac(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	if (!mp->get(mp, 1))
		return false;

	if (!vp->get(vp, 1)) {
		mp->put(mp, 1);
		return false;
	}

	return true;
}

static bool bnx2x_put_cam_offset_mac(struct bnx2x_vlan_mac_obj *o, int offset)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;

	return mp->put_entry(mp, offset);
}

static bool bnx2x_put_credit_mac(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;

	return mp->put(mp, 1);
}

static bool bnx2x_put_cam_offset_vlan(struct bnx2x_vlan_mac_obj *o, int offset)
{
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	return vp->put_entry(vp, offset);
}

static bool bnx2x_put_credit_vlan(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	return vp->put(vp, 1);
}

static bool bnx2x_put_credit_vlan_mac(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_credit_pool_obj *mp = o->macs_pool;
	struct bnx2x_credit_pool_obj *vp = o->vlans_pool;

	if (!mp->put(mp, 1))
		return false;

	if (!vp->put(vp, 1)) {
		mp->get(mp, 1);
		return false;
	}

	return true;
}

static int bnx2x_get_n_elements(struct bnx2x *bp, struct bnx2x_vlan_mac_obj *o,
				int n, u8 *buf)
{
	struct bnx2x_vlan_mac_registry_elem *pos;
	u8 *next = buf;
	int counter = 0;

	/* traverse list */
	list_for_each_entry(pos, &o->head, link) {
		if (counter < n) {
			/* place leading zeroes in buffer */
			memset(next, 0, MAC_LEADING_ZERO_CNT);

			/* place mac after leading zeroes*/
			memcpy(next + MAC_LEADING_ZERO_CNT, pos->u.mac.mac,
			       ETH_ALEN);

			/* calculate address of next element and
			 * advance counter
			 */
			counter++;
			next = buf + counter * ALIGN(ETH_ALEN, sizeof(u32));

			DP(BNX2X_MSG_SP, "copied element number %d to address %p element was %pM\n",
			   counter, next, pos->u.mac.mac);
		}
	}
	return counter * ETH_ALEN;
}

/* check_add() callbacks */
static int bnx2x_check_mac_add(struct bnx2x *bp,
			       struct bnx2x_vlan_mac_obj *o,
			       union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking MAC %pM for ADD command\n", data->mac.mac);

	if (!is_valid_ether_addr(data->mac.mac))
		return -EINVAL;

	/* Check if a requested MAC already exists */
	list_for_each_entry(pos, &o->head, link)
		if (!memcmp(data->mac.mac, pos->u.mac.mac, ETH_ALEN))
			return -EEXIST;

	return 0;
}

static int bnx2x_check_vlan_add(struct bnx2x *bp,
				struct bnx2x_vlan_mac_obj *o,
				union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking VLAN %d for ADD command\n", data->vlan.vlan);

	list_for_each_entry(pos, &o->head, link)
		if (data->vlan.vlan == pos->u.vlan.vlan)
			return -EEXIST;

	return 0;
}

static int bnx2x_check_vlan_mac_add(struct bnx2x *bp,
				    struct bnx2x_vlan_mac_obj *o,
				   union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking VLAN_MAC (%pM, %d) for ADD command\n",
	   data->vlan_mac.mac, data->vlan_mac.vlan);

	list_for_each_entry(pos, &o->head, link)
		if ((data->vlan_mac.vlan == pos->u.vlan_mac.vlan) &&
		    (!memcmp(data->vlan_mac.mac, pos->u.vlan_mac.mac,
			     ETH_ALEN)))
			return -EEXIST;

	return 0;
}


/* check_del() callbacks */
static struct bnx2x_vlan_mac_registry_elem *
	bnx2x_check_mac_del(struct bnx2x *bp,
			    struct bnx2x_vlan_mac_obj *o,
			    union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking MAC %pM for DEL command\n", data->mac.mac);

	list_for_each_entry(pos, &o->head, link)
		if (!memcmp(data->mac.mac, pos->u.mac.mac, ETH_ALEN))
			return pos;

	return NULL;
}

static struct bnx2x_vlan_mac_registry_elem *
	bnx2x_check_vlan_del(struct bnx2x *bp,
			     struct bnx2x_vlan_mac_obj *o,
			     union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking VLAN %d for DEL command\n", data->vlan.vlan);

	list_for_each_entry(pos, &o->head, link)
		if (data->vlan.vlan == pos->u.vlan.vlan)
			return pos;

	return NULL;
}

static struct bnx2x_vlan_mac_registry_elem *
	bnx2x_check_vlan_mac_del(struct bnx2x *bp,
				 struct bnx2x_vlan_mac_obj *o,
				 union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;

	DP(BNX2X_MSG_SP, "Checking VLAN_MAC (%pM, %d) for DEL command\n",
	   data->vlan_mac.mac, data->vlan_mac.vlan);

	list_for_each_entry(pos, &o->head, link)
		if ((data->vlan_mac.vlan == pos->u.vlan_mac.vlan) &&
		    (!memcmp(data->vlan_mac.mac, pos->u.vlan_mac.mac,
			     ETH_ALEN)))
			return pos;

	return NULL;
}

/* check_move() callback */
static bool bnx2x_check_move(struct bnx2x *bp,
			     struct bnx2x_vlan_mac_obj *src_o,
			     struct bnx2x_vlan_mac_obj *dst_o,
			     union bnx2x_classification_ramrod_data *data)
{
	struct bnx2x_vlan_mac_registry_elem *pos;
	int rc;

	/* Check if we can delete the requested configuration from the first
	 * object.
	 */
	pos = src_o->check_del(bp, src_o, data);

	/*  check if configuration can be added */
	rc = dst_o->check_add(bp, dst_o, data);

	/* If this classification can not be added (is already set)
	 * or can't be deleted - return an error.
	 */
	if (rc || !pos)
		return false;

	return true;
}

static bool bnx2x_check_move_always_err(
	struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *src_o,
	struct bnx2x_vlan_mac_obj *dst_o,
	union bnx2x_classification_ramrod_data *data)
{
	return false;
}


static inline u8 bnx2x_vlan_mac_get_rx_tx_flag(struct bnx2x_vlan_mac_obj *o)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	u8 rx_tx_flag = 0;

	if ((raw->obj_type == BNX2X_OBJ_TYPE_TX) ||
	    (raw->obj_type == BNX2X_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_CLASSIFY_CMD_HEADER_TX_CMD;

	if ((raw->obj_type == BNX2X_OBJ_TYPE_RX) ||
	    (raw->obj_type == BNX2X_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_CLASSIFY_CMD_HEADER_RX_CMD;

	return rx_tx_flag;
}


void bnx2x_set_mac_in_nig(struct bnx2x *bp,
			  bool add, unsigned char *dev_addr, int index)
{
	u32 wb_data[2];
	u32 reg_offset = BP_PORT(bp) ? NIG_REG_LLH1_FUNC_MEM :
			 NIG_REG_LLH0_FUNC_MEM;

	if (!IS_MF_SI(bp) && !IS_MF_AFEX(bp))
		return;

	if (index > BNX2X_LLH_CAM_MAX_PF_LINE)
		return;

	DP(BNX2X_MSG_SP, "Going to %s LLH configuration at entry %d\n",
			 (add ? "ADD" : "DELETE"), index);

	if (add) {
		/* LLH_FUNC_MEM is a u64 WB register */
		reg_offset += 8*index;

		wb_data[0] = ((dev_addr[2] << 24) | (dev_addr[3] << 16) |
			      (dev_addr[4] <<  8) |  dev_addr[5]);
		wb_data[1] = ((dev_addr[0] <<  8) |  dev_addr[1]);

		REG_WR_DMAE(bp, reg_offset, wb_data, 2);
	}

	REG_WR(bp, (BP_PORT(bp) ? NIG_REG_LLH1_FUNC_MEM_ENABLE :
				  NIG_REG_LLH0_FUNC_MEM_ENABLE) + 4*index, add);
}

/**
 * bnx2x_vlan_mac_set_cmd_hdr_e2 - set a header in a single classify ramrod
 *
 * @bp:		device handle
 * @o:		queue for which we want to configure this rule
 * @add:	if true the command is an ADD command, DEL otherwise
 * @opcode:	CLASSIFY_RULE_OPCODE_XXX
 * @hdr:	pointer to a header to setup
 *
 */
static inline void bnx2x_vlan_mac_set_cmd_hdr_e2(struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *o, bool add, int opcode,
	struct eth_classify_cmd_header *hdr)
{
	struct bnx2x_raw_obj *raw = &o->raw;

	hdr->client_id = raw->cl_id;
	hdr->func_id = raw->func_id;

	/* Rx or/and Tx (internal switching) configuration ? */
	hdr->cmd_general_data |=
		bnx2x_vlan_mac_get_rx_tx_flag(o);

	if (add)
		hdr->cmd_general_data |= ETH_CLASSIFY_CMD_HEADER_IS_ADD;

	hdr->cmd_general_data |=
		(opcode << ETH_CLASSIFY_CMD_HEADER_OPCODE_SHIFT);
}

/**
 * bnx2x_vlan_mac_set_rdata_hdr_e2 - set the classify ramrod data header
 *
 * @cid:	connection id
 * @type:	BNX2X_FILTER_XXX_PENDING
 * @hdr:	poiter to header to setup
 * @rule_cnt:
 *
 * currently we always configure one rule and echo field to contain a CID and an
 * opcode type.
 */
static inline void bnx2x_vlan_mac_set_rdata_hdr_e2(u32 cid, int type,
				struct eth_classify_header *hdr, int rule_cnt)
{
	hdr->echo = (cid & BNX2X_SWCID_MASK) | (type << BNX2X_SWCID_SHIFT);
	hdr->rule_cnt = (u8)rule_cnt;
}


/* hw_config() callbacks */
static void bnx2x_set_one_mac_e2(struct bnx2x *bp,
				 struct bnx2x_vlan_mac_obj *o,
				 struct bnx2x_exeq_elem *elem, int rule_idx,
				 int cam_offset)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
		(struct eth_classify_rules_ramrod_data *)(raw->rdata);
	int rule_cnt = rule_idx + 1, cmd = elem->cmd_data.vlan_mac.cmd;
	union eth_classify_rule_cmd *rule_entry = &data->rules[rule_idx];
	bool add = (cmd == BNX2X_VLAN_MAC_ADD) ? true : false;
	unsigned long *vlan_mac_flags = &elem->cmd_data.vlan_mac.vlan_mac_flags;
	u8 *mac = elem->cmd_data.vlan_mac.u.mac.mac;

	/*
	 * Set LLH CAM entry: currently only iSCSI and ETH macs are
	 * relevant. In addition, current implementation is tuned for a
	 * single ETH MAC.
	 *
	 * When multiple unicast ETH MACs PF configuration in switch
	 * independent mode is required (NetQ, multiple netdev MACs,
	 * etc.), consider better utilisation of 8 per function MAC
	 * entries in the LLH register. There is also
	 * NIG_REG_P[01]_LLH_FUNC_MEM2 registers that complete the
	 * total number of CAM entries to 16.
	 *
	 * Currently we won't configure NIG for MACs other than a primary ETH
	 * MAC and iSCSI L2 MAC.
	 *
	 * If this MAC is moving from one Queue to another, no need to change
	 * NIG configuration.
	 */
	if (cmd != BNX2X_VLAN_MAC_MOVE) {
		if (test_bit(BNX2X_ISCSI_ETH_MAC, vlan_mac_flags))
			bnx2x_set_mac_in_nig(bp, add, mac,
					     BNX2X_LLH_CAM_ISCSI_ETH_LINE);
		else if (test_bit(BNX2X_ETH_MAC, vlan_mac_flags))
			bnx2x_set_mac_in_nig(bp, add, mac,
					     BNX2X_LLH_CAM_ETH_LINE);
	}

	/* Reset the ramrod data buffer for the first rule */
	if (rule_idx == 0)
		memset(data, 0, sizeof(*data));

	/* Setup a command header */
	bnx2x_vlan_mac_set_cmd_hdr_e2(bp, o, add, CLASSIFY_RULE_OPCODE_MAC,
				      &rule_entry->mac.header);

	DP(BNX2X_MSG_SP, "About to %s MAC %pM for Queue %d\n",
	   (add ? "add" : "delete"), mac, raw->cl_id);

	/* Set a MAC itself */
	bnx2x_set_fw_mac_addr(&rule_entry->mac.mac_msb,
			      &rule_entry->mac.mac_mid,
			      &rule_entry->mac.mac_lsb, mac);

	/* MOVE: Add a rule that will add this MAC to the target Queue */
	if (cmd == BNX2X_VLAN_MAC_MOVE) {
		rule_entry++;
		rule_cnt++;

		/* Setup ramrod data */
		bnx2x_vlan_mac_set_cmd_hdr_e2(bp,
					elem->cmd_data.vlan_mac.target_obj,
					      true, CLASSIFY_RULE_OPCODE_MAC,
					      &rule_entry->mac.header);

		/* Set a MAC itself */
		bnx2x_set_fw_mac_addr(&rule_entry->mac.mac_msb,
				      &rule_entry->mac.mac_mid,
				      &rule_entry->mac.mac_lsb, mac);
	}

	/* Set the ramrod data header */
	/* TODO: take this to the higher level in order to prevent multiple
		 writing */
	bnx2x_vlan_mac_set_rdata_hdr_e2(raw->cid, raw->state, &data->header,
					rule_cnt);
}

/**
 * bnx2x_vlan_mac_set_rdata_hdr_e1x - set a header in a single classify ramrod
 *
 * @bp:		device handle
 * @o:		queue
 * @type:
 * @cam_offset:	offset in cam memory
 * @hdr:	pointer to a header to setup
 *
 * E1/E1H
 */
static inline void bnx2x_vlan_mac_set_rdata_hdr_e1x(struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *o, int type, int cam_offset,
	struct mac_configuration_hdr *hdr)
{
	struct bnx2x_raw_obj *r = &o->raw;

	hdr->length = 1;
	hdr->offset = (u8)cam_offset;
	hdr->client_id = 0xff;
	hdr->echo = ((r->cid & BNX2X_SWCID_MASK) | (type << BNX2X_SWCID_SHIFT));
}

static inline void bnx2x_vlan_mac_set_cfg_entry_e1x(struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *o, bool add, int opcode, u8 *mac,
	u16 vlan_id, struct mac_configuration_entry *cfg_entry)
{
	struct bnx2x_raw_obj *r = &o->raw;
	u32 cl_bit_vec = (1 << r->cl_id);

	cfg_entry->clients_bit_vector = cpu_to_le32(cl_bit_vec);
	cfg_entry->pf_id = r->func_id;
	cfg_entry->vlan_id = cpu_to_le16(vlan_id);

	if (add) {
		SET_FLAG(cfg_entry->flags, MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_SET);
		SET_FLAG(cfg_entry->flags,
			 MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE, opcode);

		/* Set a MAC in a ramrod data */
		bnx2x_set_fw_mac_addr(&cfg_entry->msb_mac_addr,
				      &cfg_entry->middle_mac_addr,
				      &cfg_entry->lsb_mac_addr, mac);
	} else
		SET_FLAG(cfg_entry->flags, MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_INVALIDATE);
}

static inline void bnx2x_vlan_mac_set_rdata_e1x(struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *o, int type, int cam_offset, bool add,
	u8 *mac, u16 vlan_id, int opcode, struct mac_configuration_cmd *config)
{
	struct mac_configuration_entry *cfg_entry = &config->config_table[0];
	struct bnx2x_raw_obj *raw = &o->raw;

	bnx2x_vlan_mac_set_rdata_hdr_e1x(bp, o, type, cam_offset,
					 &config->hdr);
	bnx2x_vlan_mac_set_cfg_entry_e1x(bp, o, add, opcode, mac, vlan_id,
					 cfg_entry);

	DP(BNX2X_MSG_SP, "%s MAC %pM CLID %d CAM offset %d\n",
			 (add ? "setting" : "clearing"),
			 mac, raw->cl_id, cam_offset);
}

/**
 * bnx2x_set_one_mac_e1x - fill a single MAC rule ramrod data
 *
 * @bp:		device handle
 * @o:		bnx2x_vlan_mac_obj
 * @elem:	bnx2x_exeq_elem
 * @rule_idx:	rule_idx
 * @cam_offset: cam_offset
 */
static void bnx2x_set_one_mac_e1x(struct bnx2x *bp,
				  struct bnx2x_vlan_mac_obj *o,
				  struct bnx2x_exeq_elem *elem, int rule_idx,
				  int cam_offset)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)(raw->rdata);
	/*
	 * 57710 and 57711 do not support MOVE command,
	 * so it's either ADD or DEL
	 */
	bool add = (elem->cmd_data.vlan_mac.cmd == BNX2X_VLAN_MAC_ADD) ?
		true : false;

	/* Reset the ramrod data buffer */
	memset(config, 0, sizeof(*config));

	bnx2x_vlan_mac_set_rdata_e1x(bp, o, raw->state,
				     cam_offset, add,
				     elem->cmd_data.vlan_mac.u.mac.mac, 0,
				     ETH_VLAN_FILTER_ANY_VLAN, config);
}

static void bnx2x_set_one_vlan_e2(struct bnx2x *bp,
				  struct bnx2x_vlan_mac_obj *o,
				  struct bnx2x_exeq_elem *elem, int rule_idx,
				  int cam_offset)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
		(struct eth_classify_rules_ramrod_data *)(raw->rdata);
	int rule_cnt = rule_idx + 1;
	union eth_classify_rule_cmd *rule_entry = &data->rules[rule_idx];
	int cmd = elem->cmd_data.vlan_mac.cmd;
	bool add = (cmd == BNX2X_VLAN_MAC_ADD) ? true : false;
	u16 vlan = elem->cmd_data.vlan_mac.u.vlan.vlan;

	/* Reset the ramrod data buffer for the first rule */
	if (rule_idx == 0)
		memset(data, 0, sizeof(*data));

	/* Set a rule header */
	bnx2x_vlan_mac_set_cmd_hdr_e2(bp, o, add, CLASSIFY_RULE_OPCODE_VLAN,
				      &rule_entry->vlan.header);

	DP(BNX2X_MSG_SP, "About to %s VLAN %d\n", (add ? "add" : "delete"),
			 vlan);

	/* Set a VLAN itself */
	rule_entry->vlan.vlan = cpu_to_le16(vlan);

	/* MOVE: Add a rule that will add this MAC to the target Queue */
	if (cmd == BNX2X_VLAN_MAC_MOVE) {
		rule_entry++;
		rule_cnt++;

		/* Setup ramrod data */
		bnx2x_vlan_mac_set_cmd_hdr_e2(bp,
					elem->cmd_data.vlan_mac.target_obj,
					      true, CLASSIFY_RULE_OPCODE_VLAN,
					      &rule_entry->vlan.header);

		/* Set a VLAN itself */
		rule_entry->vlan.vlan = cpu_to_le16(vlan);
	}

	/* Set the ramrod data header */
	/* TODO: take this to the higher level in order to prevent multiple
		 writing */
	bnx2x_vlan_mac_set_rdata_hdr_e2(raw->cid, raw->state, &data->header,
					rule_cnt);
}

static void bnx2x_set_one_vlan_mac_e2(struct bnx2x *bp,
				      struct bnx2x_vlan_mac_obj *o,
				      struct bnx2x_exeq_elem *elem,
				      int rule_idx, int cam_offset)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
		(struct eth_classify_rules_ramrod_data *)(raw->rdata);
	int rule_cnt = rule_idx + 1;
	union eth_classify_rule_cmd *rule_entry = &data->rules[rule_idx];
	int cmd = elem->cmd_data.vlan_mac.cmd;
	bool add = (cmd == BNX2X_VLAN_MAC_ADD) ? true : false;
	u16 vlan = elem->cmd_data.vlan_mac.u.vlan_mac.vlan;
	u8 *mac = elem->cmd_data.vlan_mac.u.vlan_mac.mac;


	/* Reset the ramrod data buffer for the first rule */
	if (rule_idx == 0)
		memset(data, 0, sizeof(*data));

	/* Set a rule header */
	bnx2x_vlan_mac_set_cmd_hdr_e2(bp, o, add, CLASSIFY_RULE_OPCODE_PAIR,
				      &rule_entry->pair.header);

	/* Set VLAN and MAC themselvs */
	rule_entry->pair.vlan = cpu_to_le16(vlan);
	bnx2x_set_fw_mac_addr(&rule_entry->pair.mac_msb,
			      &rule_entry->pair.mac_mid,
			      &rule_entry->pair.mac_lsb, mac);

	/* MOVE: Add a rule that will add this MAC to the target Queue */
	if (cmd == BNX2X_VLAN_MAC_MOVE) {
		rule_entry++;
		rule_cnt++;

		/* Setup ramrod data */
		bnx2x_vlan_mac_set_cmd_hdr_e2(bp,
					elem->cmd_data.vlan_mac.target_obj,
					      true, CLASSIFY_RULE_OPCODE_PAIR,
					      &rule_entry->pair.header);

		/* Set a VLAN itself */
		rule_entry->pair.vlan = cpu_to_le16(vlan);
		bnx2x_set_fw_mac_addr(&rule_entry->pair.mac_msb,
				      &rule_entry->pair.mac_mid,
				      &rule_entry->pair.mac_lsb, mac);
	}

	/* Set the ramrod data header */
	/* TODO: take this to the higher level in order to prevent multiple
		 writing */
	bnx2x_vlan_mac_set_rdata_hdr_e2(raw->cid, raw->state, &data->header,
					rule_cnt);
}

/**
 * bnx2x_set_one_vlan_mac_e1h -
 *
 * @bp:		device handle
 * @o:		bnx2x_vlan_mac_obj
 * @elem:	bnx2x_exeq_elem
 * @rule_idx:	rule_idx
 * @cam_offset:	cam_offset
 */
static void bnx2x_set_one_vlan_mac_e1h(struct bnx2x *bp,
				       struct bnx2x_vlan_mac_obj *o,
				       struct bnx2x_exeq_elem *elem,
				       int rule_idx, int cam_offset)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)(raw->rdata);
	/*
	 * 57710 and 57711 do not support MOVE command,
	 * so it's either ADD or DEL
	 */
	bool add = (elem->cmd_data.vlan_mac.cmd == BNX2X_VLAN_MAC_ADD) ?
		true : false;

	/* Reset the ramrod data buffer */
	memset(config, 0, sizeof(*config));

	bnx2x_vlan_mac_set_rdata_e1x(bp, o, BNX2X_FILTER_VLAN_MAC_PENDING,
				     cam_offset, add,
				     elem->cmd_data.vlan_mac.u.vlan_mac.mac,
				     elem->cmd_data.vlan_mac.u.vlan_mac.vlan,
				     ETH_VLAN_FILTER_CLASSIFY, config);
}

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

/**
 * bnx2x_vlan_mac_restore - reconfigure next MAC/VLAN/VLAN-MAC element
 *
 * @bp:		device handle
 * @p:		command parameters
 * @ppos:	pointer to the cooky
 *
 * reconfigure next MAC/VLAN/VLAN-MAC element from the
 * previously configured elements list.
 *
 * from command parameters only RAMROD_COMP_WAIT bit in ramrod_flags is	taken
 * into an account
 *
 * pointer to the cooky  - that should be given back in the next call to make
 * function handle the next element. If *ppos is set to NULL it will restart the
 * iterator. If returned *ppos == NULL this means that the last element has been
 * handled.
 *
 */
static int bnx2x_vlan_mac_restore(struct bnx2x *bp,
			   struct bnx2x_vlan_mac_ramrod_params *p,
			   struct bnx2x_vlan_mac_registry_elem **ppos)
{
	struct bnx2x_vlan_mac_registry_elem *pos;
	struct bnx2x_vlan_mac_obj *o = p->vlan_mac_obj;

	/* If list is empty - there is nothing to do here */
	if (list_empty(&o->head)) {
		*ppos = NULL;
		return 0;
	}

	/* make a step... */
	if (*ppos == NULL)
		*ppos = list_first_entry(&o->head,
					 struct bnx2x_vlan_mac_registry_elem,
					 link);
	else
		*ppos = list_next_entry(*ppos, link);

	pos = *ppos;

	/* If it's the last step - return NULL */
	if (list_is_last(&pos->link, &o->head))
		*ppos = NULL;

	/* Prepare a 'user_req' */
	memcpy(&p->user_req.u, &pos->u, sizeof(pos->u));

	/* Set the command */
	p->user_req.cmd = BNX2X_VLAN_MAC_ADD;

	/* Set vlan_mac_flags */
	p->user_req.vlan_mac_flags = pos->vlan_mac_flags;

	/* Set a restore bit */
	__set_bit(RAMROD_RESTORE, &p->ramrod_flags);

	return bnx2x_config_vlan_mac(bp, p);
}

/*
 * bnx2x_exeq_get_mac/bnx2x_exeq_get_vlan/bnx2x_exeq_get_vlan_mac return a
 * pointer to an element with a specific criteria and NULL if such an element
 * hasn't been found.
 */
static struct bnx2x_exeq_elem *bnx2x_exeq_get_mac(
	struct bnx2x_exe_queue_obj *o,
	struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_exeq_elem *pos;
	struct bnx2x_mac_ramrod_data *data = &elem->cmd_data.vlan_mac.u.mac;

	/* Check pending for execution commands */
	list_for_each_entry(pos, &o->exe_queue, link)
		if (!memcmp(&pos->cmd_data.vlan_mac.u.mac, data,
			      sizeof(*data)) &&
		    (pos->cmd_data.vlan_mac.cmd == elem->cmd_data.vlan_mac.cmd))
			return pos;

	return NULL;
}

static struct bnx2x_exeq_elem *bnx2x_exeq_get_vlan(
	struct bnx2x_exe_queue_obj *o,
	struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_exeq_elem *pos;
	struct bnx2x_vlan_ramrod_data *data = &elem->cmd_data.vlan_mac.u.vlan;

	/* Check pending for execution commands */
	list_for_each_entry(pos, &o->exe_queue, link)
		if (!memcmp(&pos->cmd_data.vlan_mac.u.vlan, data,
			      sizeof(*data)) &&
		    (pos->cmd_data.vlan_mac.cmd == elem->cmd_data.vlan_mac.cmd))
			return pos;

	return NULL;
}

static struct bnx2x_exeq_elem *bnx2x_exeq_get_vlan_mac(
	struct bnx2x_exe_queue_obj *o,
	struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_exeq_elem *pos;
	struct bnx2x_vlan_mac_ramrod_data *data =
		&elem->cmd_data.vlan_mac.u.vlan_mac;

	/* Check pending for execution commands */
	list_for_each_entry(pos, &o->exe_queue, link)
		if (!memcmp(&pos->cmd_data.vlan_mac.u.vlan_mac, data,
			      sizeof(*data)) &&
		    (pos->cmd_data.vlan_mac.cmd == elem->cmd_data.vlan_mac.cmd))
			return pos;

	return NULL;
}

/**
 * bnx2x_validate_vlan_mac_add - check if an ADD command can be executed
 *
 * @bp:		device handle
 * @qo:		bnx2x_qable_obj
 * @elem:	bnx2x_exeq_elem
 *
 * Checks that the requested configuration can be added. If yes and if
 * requested, consume CAM credit.
 *
 * The 'validate' is run after the 'optimize'.
 *
 */
static inline int bnx2x_validate_vlan_mac_add(struct bnx2x *bp,
					      union bnx2x_qable_obj *qo,
					      struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_vlan_mac_obj *o = &qo->vlan_mac;
	struct bnx2x_exe_queue_obj *exeq = &o->exe_queue;
	int rc;

	/* Check the registry */
	rc = o->check_add(bp, o, &elem->cmd_data.vlan_mac.u);
	if (rc) {
		DP(BNX2X_MSG_SP, "ADD command is not allowed considering current registry state.\n");
		return rc;
	}

	/*
	 * Check if there is a pending ADD command for this
	 * MAC/VLAN/VLAN-MAC. Return an error if there is.
	 */
	if (exeq->get(exeq, elem)) {
		DP(BNX2X_MSG_SP, "There is a pending ADD command already\n");
		return -EEXIST;
	}

	/*
	 * TODO: Check the pending MOVE from other objects where this
	 * object is a destination object.
	 */

	/* Consume the credit if not requested not to */
	if (!(test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT,
		       &elem->cmd_data.vlan_mac.vlan_mac_flags) ||
	    o->get_credit(o)))
		return -EINVAL;

	return 0;
}

/**
 * bnx2x_validate_vlan_mac_del - check if the DEL command can be executed
 *
 * @bp:		device handle
 * @qo:		quable object to check
 * @elem:	element that needs to be deleted
 *
 * Checks that the requested configuration can be deleted. If yes and if
 * requested, returns a CAM credit.
 *
 * The 'validate' is run after the 'optimize'.
 */
static inline int bnx2x_validate_vlan_mac_del(struct bnx2x *bp,
					      union bnx2x_qable_obj *qo,
					      struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_vlan_mac_obj *o = &qo->vlan_mac;
	struct bnx2x_vlan_mac_registry_elem *pos;
	struct bnx2x_exe_queue_obj *exeq = &o->exe_queue;
	struct bnx2x_exeq_elem query_elem;

	/* If this classification can not be deleted (doesn't exist)
	 * - return a BNX2X_EXIST.
	 */
	pos = o->check_del(bp, o, &elem->cmd_data.vlan_mac.u);
	if (!pos) {
		DP(BNX2X_MSG_SP, "DEL command is not allowed considering current registry state\n");
		return -EEXIST;
	}

	/*
	 * Check if there are pending DEL or MOVE commands for this
	 * MAC/VLAN/VLAN-MAC. Return an error if so.
	 */
	memcpy(&query_elem, elem, sizeof(query_elem));

	/* Check for MOVE commands */
	query_elem.cmd_data.vlan_mac.cmd = BNX2X_VLAN_MAC_MOVE;
	if (exeq->get(exeq, &query_elem)) {
		BNX2X_ERR("There is a pending MOVE command already\n");
		return -EINVAL;
	}

	/* Check for DEL commands */
	if (exeq->get(exeq, elem)) {
		DP(BNX2X_MSG_SP, "There is a pending DEL command already\n");
		return -EEXIST;
	}

	/* Return the credit to the credit pool if not requested not to */
	if (!(test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT,
		       &elem->cmd_data.vlan_mac.vlan_mac_flags) ||
	    o->put_credit(o))) {
		BNX2X_ERR("Failed to return a credit\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * bnx2x_validate_vlan_mac_move - check if the MOVE command can be executed
 *
 * @bp:		device handle
 * @qo:		quable object to check (source)
 * @elem:	element that needs to be moved
 *
 * Checks that the requested configuration can be moved. If yes and if
 * requested, returns a CAM credit.
 *
 * The 'validate' is run after the 'optimize'.
 */
static inline int bnx2x_validate_vlan_mac_move(struct bnx2x *bp,
					       union bnx2x_qable_obj *qo,
					       struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_vlan_mac_obj *src_o = &qo->vlan_mac;
	struct bnx2x_vlan_mac_obj *dest_o = elem->cmd_data.vlan_mac.target_obj;
	struct bnx2x_exeq_elem query_elem;
	struct bnx2x_exe_queue_obj *src_exeq = &src_o->exe_queue;
	struct bnx2x_exe_queue_obj *dest_exeq = &dest_o->exe_queue;

	/*
	 * Check if we can perform this operation based on the current registry
	 * state.
	 */
	if (!src_o->check_move(bp, src_o, dest_o,
			       &elem->cmd_data.vlan_mac.u)) {
		DP(BNX2X_MSG_SP, "MOVE command is not allowed considering current registry state\n");
		return -EINVAL;
	}

	/*
	 * Check if there is an already pending DEL or MOVE command for the
	 * source object or ADD command for a destination object. Return an
	 * error if so.
	 */
	memcpy(&query_elem, elem, sizeof(query_elem));

	/* Check DEL on source */
	query_elem.cmd_data.vlan_mac.cmd = BNX2X_VLAN_MAC_DEL;
	if (src_exeq->get(src_exeq, &query_elem)) {
		BNX2X_ERR("There is a pending DEL command on the source queue already\n");
		return -EINVAL;
	}

	/* Check MOVE on source */
	if (src_exeq->get(src_exeq, elem)) {
		DP(BNX2X_MSG_SP, "There is a pending MOVE command already\n");
		return -EEXIST;
	}

	/* Check ADD on destination */
	query_elem.cmd_data.vlan_mac.cmd = BNX2X_VLAN_MAC_ADD;
	if (dest_exeq->get(dest_exeq, &query_elem)) {
		BNX2X_ERR("There is a pending ADD command on the destination queue already\n");
		return -EINVAL;
	}

	/* Consume the credit if not requested not to */
	if (!(test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT_DEST,
		       &elem->cmd_data.vlan_mac.vlan_mac_flags) ||
	    dest_o->get_credit(dest_o)))
		return -EINVAL;

	if (!(test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT,
		       &elem->cmd_data.vlan_mac.vlan_mac_flags) ||
	    src_o->put_credit(src_o))) {
		/* return the credit taken from dest... */
		dest_o->put_credit(dest_o);
		return -EINVAL;
	}

	return 0;
}

static int bnx2x_validate_vlan_mac(struct bnx2x *bp,
				   union bnx2x_qable_obj *qo,
				   struct bnx2x_exeq_elem *elem)
{
	switch (elem->cmd_data.vlan_mac.cmd) {
	case BNX2X_VLAN_MAC_ADD:
		return bnx2x_validate_vlan_mac_add(bp, qo, elem);
	case BNX2X_VLAN_MAC_DEL:
		return bnx2x_validate_vlan_mac_del(bp, qo, elem);
	case BNX2X_VLAN_MAC_MOVE:
		return bnx2x_validate_vlan_mac_move(bp, qo, elem);
	default:
		return -EINVAL;
	}
}

static int bnx2x_remove_vlan_mac(struct bnx2x *bp,
				  union bnx2x_qable_obj *qo,
				  struct bnx2x_exeq_elem *elem)
{
	int rc = 0;

	/* If consumption wasn't required, nothing to do */
	if (test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT,
		     &elem->cmd_data.vlan_mac.vlan_mac_flags))
		return 0;

	switch (elem->cmd_data.vlan_mac.cmd) {
	case BNX2X_VLAN_MAC_ADD:
	case BNX2X_VLAN_MAC_MOVE:
		rc = qo->vlan_mac.put_credit(&qo->vlan_mac);
		break;
	case BNX2X_VLAN_MAC_DEL:
		rc = qo->vlan_mac.get_credit(&qo->vlan_mac);
		break;
	default:
		return -EINVAL;
	}

	if (rc != true)
		return -EINVAL;

	return 0;
}

/**
 * bnx2x_wait_vlan_mac - passivly wait for 5 seconds until all work completes.
 *
 * @bp:		device handle
 * @o:		bnx2x_vlan_mac_obj
 *
 */
static int bnx2x_wait_vlan_mac(struct bnx2x *bp,
			       struct bnx2x_vlan_mac_obj *o)
{
	int cnt = 5000, rc;
	struct bnx2x_exe_queue_obj *exeq = &o->exe_queue;
	struct bnx2x_raw_obj *raw = &o->raw;

	while (cnt--) {
		/* Wait for the current command to complete */
		rc = raw->wait_comp(bp, raw);
		if (rc)
			return rc;

		/* Wait until there are no pending commands */
		if (!bnx2x_exe_queue_empty(exeq))
			usleep_range(1000, 1000);
		else
			return 0;
	}

	return -EBUSY;
}

/**
 * bnx2x_complete_vlan_mac - complete one VLAN-MAC ramrod
 *
 * @bp:		device handle
 * @o:		bnx2x_vlan_mac_obj
 * @cqe:
 * @cont:	if true schedule next execution chunk
 *
 */
static int bnx2x_complete_vlan_mac(struct bnx2x *bp,
				   struct bnx2x_vlan_mac_obj *o,
				   union event_ring_elem *cqe,
				   unsigned long *ramrod_flags)
{
	struct bnx2x_raw_obj *r = &o->raw;
	int rc;

	/* Reset pending list */
	bnx2x_exe_queue_reset_pending(bp, &o->exe_queue);

	/* Clear pending */
	r->clear_pending(r);

	/* If ramrod failed this is most likely a SW bug */
	if (cqe->message.error)
		return -EINVAL;

	/* Run the next bulk of pending commands if requeted */
	if (test_bit(RAMROD_CONT, ramrod_flags)) {
		rc = bnx2x_exe_queue_step(bp, &o->exe_queue, ramrod_flags);
		if (rc < 0)
			return rc;
	}

	/* If there is more work to do return PENDING */
	if (!bnx2x_exe_queue_empty(&o->exe_queue))
		return 1;

	return 0;
}

/**
 * bnx2x_optimize_vlan_mac - optimize ADD and DEL commands.
 *
 * @bp:		device handle
 * @o:		bnx2x_qable_obj
 * @elem:	bnx2x_exeq_elem
 */
static int bnx2x_optimize_vlan_mac(struct bnx2x *bp,
				   union bnx2x_qable_obj *qo,
				   struct bnx2x_exeq_elem *elem)
{
	struct bnx2x_exeq_elem query, *pos;
	struct bnx2x_vlan_mac_obj *o = &qo->vlan_mac;
	struct bnx2x_exe_queue_obj *exeq = &o->exe_queue;

	memcpy(&query, elem, sizeof(query));

	switch (elem->cmd_data.vlan_mac.cmd) {
	case BNX2X_VLAN_MAC_ADD:
		query.cmd_data.vlan_mac.cmd = BNX2X_VLAN_MAC_DEL;
		break;
	case BNX2X_VLAN_MAC_DEL:
		query.cmd_data.vlan_mac.cmd = BNX2X_VLAN_MAC_ADD;
		break;
	default:
		/* Don't handle anything other than ADD or DEL */
		return 0;
	}

	/* If we found the appropriate element - delete it */
	pos = exeq->get(exeq, &query);
	if (pos) {

		/* Return the credit of the optimized command */
		if (!test_bit(BNX2X_DONT_CONSUME_CAM_CREDIT,
			      &pos->cmd_data.vlan_mac.vlan_mac_flags)) {
			if ((query.cmd_data.vlan_mac.cmd ==
			     BNX2X_VLAN_MAC_ADD) && !o->put_credit(o)) {
				BNX2X_ERR("Failed to return the credit for the optimized ADD command\n");
				return -EINVAL;
			} else if (!o->get_credit(o)) { /* VLAN_MAC_DEL */
				BNX2X_ERR("Failed to recover the credit from the optimized DEL command\n");
				return -EINVAL;
			}
		}

		DP(BNX2X_MSG_SP, "Optimizing %s command\n",
			   (elem->cmd_data.vlan_mac.cmd == BNX2X_VLAN_MAC_ADD) ?
			   "ADD" : "DEL");

		list_del(&pos->link);
		bnx2x_exe_queue_free_elem(bp, pos);
		return 1;
	}

	return 0;
}

/**
 * bnx2x_vlan_mac_get_registry_elem - prepare a registry element
 *
 * @bp:	  device handle
 * @o:
 * @elem:
 * @restore:
 * @re:
 *
 * prepare a registry element according to the current command request.
 */
static inline int bnx2x_vlan_mac_get_registry_elem(
	struct bnx2x *bp,
	struct bnx2x_vlan_mac_obj *o,
	struct bnx2x_exeq_elem *elem,
	bool restore,
	struct bnx2x_vlan_mac_registry_elem **re)
{
	int cmd = elem->cmd_data.vlan_mac.cmd;
	struct bnx2x_vlan_mac_registry_elem *reg_elem;

	/* Allocate a new registry element if needed. */
	if (!restore &&
	    ((cmd == BNX2X_VLAN_MAC_ADD) || (cmd == BNX2X_VLAN_MAC_MOVE))) {
		reg_elem = kzalloc(sizeof(*reg_elem), GFP_ATOMIC);
		if (!reg_elem)
			return -ENOMEM;

		/* Get a new CAM offset */
		if (!o->get_cam_offset(o, &reg_elem->cam_offset)) {
			/*
			 * This shell never happen, because we have checked the
			 * CAM availiability in the 'validate'.
			 */
			WARN_ON(1);
			kfree(reg_elem);
			return -EINVAL;
		}

		DP(BNX2X_MSG_SP, "Got cam offset %d\n", reg_elem->cam_offset);

		/* Set a VLAN-MAC data */
		memcpy(&reg_elem->u, &elem->cmd_data.vlan_mac.u,
			  sizeof(reg_elem->u));

		/* Copy the flags (needed for DEL and RESTORE flows) */
		reg_elem->vlan_mac_flags =
			elem->cmd_data.vlan_mac.vlan_mac_flags;
	} else /* DEL, RESTORE */
		reg_elem = o->check_del(bp, o, &elem->cmd_data.vlan_mac.u);

	*re = reg_elem;
	return 0;
}

/**
 * bnx2x_execute_vlan_mac - execute vlan mac command
 *
 * @bp:			device handle
 * @qo:
 * @exe_chunk:
 * @ramrod_flags:
 *
 * go and send a ramrod!
 */
static int bnx2x_execute_vlan_mac(struct bnx2x *bp,
				  union bnx2x_qable_obj *qo,
				  struct list_head *exe_chunk,
				  unsigned long *ramrod_flags)
{
	struct bnx2x_exeq_elem *elem;
	struct bnx2x_vlan_mac_obj *o = &qo->vlan_mac, *cam_obj;
	struct bnx2x_raw_obj *r = &o->raw;
	int rc, idx = 0;
	bool restore = test_bit(RAMROD_RESTORE, ramrod_flags);
	bool drv_only = test_bit(RAMROD_DRV_CLR_ONLY, ramrod_flags);
	struct bnx2x_vlan_mac_registry_elem *reg_elem;
	int cmd;

	/*
	 * If DRIVER_ONLY execution is requested, cleanup a registry
	 * and exit. Otherwise send a ramrod to FW.
	 */
	if (!drv_only) {
		WARN_ON(r->check_pending(r));

		/* Set pending */
		r->set_pending(r);

		/* Fill tha ramrod data */
		list_for_each_entry(elem, exe_chunk, link) {
			cmd = elem->cmd_data.vlan_mac.cmd;
			/*
			 * We will add to the target object in MOVE command, so
			 * change the object for a CAM search.
			 */
			if (cmd == BNX2X_VLAN_MAC_MOVE)
				cam_obj = elem->cmd_data.vlan_mac.target_obj;
			else
				cam_obj = o;

			rc = bnx2x_vlan_mac_get_registry_elem(bp, cam_obj,
							      elem, restore,
							      &reg_elem);
			if (rc)
				goto error_exit;

			WARN_ON(!reg_elem);

			/* Push a new entry into the registry */
			if (!restore &&
			    ((cmd == BNX2X_VLAN_MAC_ADD) ||
			    (cmd == BNX2X_VLAN_MAC_MOVE)))
				list_add(&reg_elem->link, &cam_obj->head);

			/* Configure a single command in a ramrod data buffer */
			o->set_one_rule(bp, o, elem, idx,
					reg_elem->cam_offset);

			/* MOVE command consumes 2 entries in the ramrod data */
			if (cmd == BNX2X_VLAN_MAC_MOVE)
				idx += 2;
			else
				idx++;
		}

		/*
		 *  No need for an explicit memory barrier here as long we would
		 *  need to ensure the ordering of writing to the SPQ element
		 *  and updating of the SPQ producer which involves a memory
		 *  read and we will have to put a full memory barrier there
		 *  (inside bnx2x_sp_post()).
		 */

		rc = bnx2x_sp_post(bp, o->ramrod_cmd, r->cid,
				   U64_HI(r->rdata_mapping),
				   U64_LO(r->rdata_mapping),
				   ETH_CONNECTION_TYPE);
		if (rc)
			goto error_exit;
	}

	/* Now, when we are done with the ramrod - clean up the registry */
	list_for_each_entry(elem, exe_chunk, link) {
		cmd = elem->cmd_data.vlan_mac.cmd;
		if ((cmd == BNX2X_VLAN_MAC_DEL) ||
		    (cmd == BNX2X_VLAN_MAC_MOVE)) {
			reg_elem = o->check_del(bp, o,
						&elem->cmd_data.vlan_mac.u);

			WARN_ON(!reg_elem);

			o->put_cam_offset(o, reg_elem->cam_offset);
			list_del(&reg_elem->link);
			kfree(reg_elem);
		}
	}

	if (!drv_only)
		return 1;
	else
		return 0;

error_exit:
	r->clear_pending(r);

	/* Cleanup a registry in case of a failure */
	list_for_each_entry(elem, exe_chunk, link) {
		cmd = elem->cmd_data.vlan_mac.cmd;

		if (cmd == BNX2X_VLAN_MAC_MOVE)
			cam_obj = elem->cmd_data.vlan_mac.target_obj;
		else
			cam_obj = o;

		/* Delete all newly added above entries */
		if (!restore &&
		    ((cmd == BNX2X_VLAN_MAC_ADD) ||
		    (cmd == BNX2X_VLAN_MAC_MOVE))) {
			reg_elem = o->check_del(bp, cam_obj,
						&elem->cmd_data.vlan_mac.u);
			if (reg_elem) {
				list_del(&reg_elem->link);
				kfree(reg_elem);
			}
		}
	}

	return rc;
}

static inline int bnx2x_vlan_mac_push_new_cmd(
	struct bnx2x *bp,
	struct bnx2x_vlan_mac_ramrod_params *p)
{
	struct bnx2x_exeq_elem *elem;
	struct bnx2x_vlan_mac_obj *o = p->vlan_mac_obj;
	bool restore = test_bit(RAMROD_RESTORE, &p->ramrod_flags);

	/* Allocate the execution queue element */
	elem = bnx2x_exe_queue_alloc_elem(bp);
	if (!elem)
		return -ENOMEM;

	/* Set the command 'length' */
	switch (p->user_req.cmd) {
	case BNX2X_VLAN_MAC_MOVE:
		elem->cmd_len = 2;
		break;
	default:
		elem->cmd_len = 1;
	}

	/* Fill the object specific info */
	memcpy(&elem->cmd_data.vlan_mac, &p->user_req, sizeof(p->user_req));

	/* Try to add a new command to the pending list */
	return bnx2x_exe_queue_add(bp, &o->exe_queue, elem, restore);
}

/**
 * bnx2x_config_vlan_mac - configure VLAN/MAC/VLAN_MAC filtering rules.
 *
 * @bp:	  device handle
 * @p:
 *
 */
int bnx2x_config_vlan_mac(
	struct bnx2x *bp,
	struct bnx2x_vlan_mac_ramrod_params *p)
{
	int rc = 0;
	struct bnx2x_vlan_mac_obj *o = p->vlan_mac_obj;
	unsigned long *ramrod_flags = &p->ramrod_flags;
	bool cont = test_bit(RAMROD_CONT, ramrod_flags);
	struct bnx2x_raw_obj *raw = &o->raw;

	/*
	 * Add new elements to the execution list for commands that require it.
	 */
	if (!cont) {
		rc = bnx2x_vlan_mac_push_new_cmd(bp, p);
		if (rc)
			return rc;
	}

	/*
	 * If nothing will be executed further in this iteration we want to
	 * return PENDING if there are pending commands
	 */
	if (!bnx2x_exe_queue_empty(&o->exe_queue))
		rc = 1;

	if (test_bit(RAMROD_DRV_CLR_ONLY, ramrod_flags))  {
		DP(BNX2X_MSG_SP, "RAMROD_DRV_CLR_ONLY requested: clearing a pending bit.\n");
		raw->clear_pending(raw);
	}

	/* Execute commands if required */
	if (cont || test_bit(RAMROD_EXEC, ramrod_flags) ||
	    test_bit(RAMROD_COMP_WAIT, ramrod_flags)) {
		rc = bnx2x_exe_queue_step(bp, &o->exe_queue, ramrod_flags);
		if (rc < 0)
			return rc;
	}

	/*
	 * RAMROD_COMP_WAIT is a superset of RAMROD_EXEC. If it was set
	 * then user want to wait until the last command is done.
	 */
	if (test_bit(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		/*
		 * Wait maximum for the current exe_queue length iterations plus
		 * one (for the current pending command).
		 */
		int max_iterations = bnx2x_exe_queue_length(&o->exe_queue) + 1;

		while (!bnx2x_exe_queue_empty(&o->exe_queue) &&
		       max_iterations--) {

			/* Wait for the current command to complete */
			rc = raw->wait_comp(bp, raw);
			if (rc)
				return rc;

			/* Make a next step */
			rc = bnx2x_exe_queue_step(bp, &o->exe_queue,
						  ramrod_flags);
			if (rc < 0)
				return rc;
		}

		return 0;
	}

	return rc;
}



/**
 * bnx2x_vlan_mac_del_all - delete elements with given vlan_mac_flags spec
 *
 * @bp:			device handle
 * @o:
 * @vlan_mac_flags:
 * @ramrod_flags:	execution flags to be used for this deletion
 *
 * if the last operation has completed successfully and there are no
 * moreelements left, positive value if the last operation has completed
 * successfully and there are more previously configured elements, negative
 * value is current operation has failed.
 */
static int bnx2x_vlan_mac_del_all(struct bnx2x *bp,
				  struct bnx2x_vlan_mac_obj *o,
				  unsigned long *vlan_mac_flags,
				  unsigned long *ramrod_flags)
{
	struct bnx2x_vlan_mac_registry_elem *pos = NULL;
	int rc = 0;
	struct bnx2x_vlan_mac_ramrod_params p;
	struct bnx2x_exe_queue_obj *exeq = &o->exe_queue;
	struct bnx2x_exeq_elem *exeq_pos, *exeq_pos_n;

	/* Clear pending commands first */

	spin_lock_bh(&exeq->lock);

	list_for_each_entry_safe(exeq_pos, exeq_pos_n, &exeq->exe_queue, link) {
		if (exeq_pos->cmd_data.vlan_mac.vlan_mac_flags ==
		    *vlan_mac_flags) {
			rc = exeq->remove(bp, exeq->owner, exeq_pos);
			if (rc) {
				BNX2X_ERR("Failed to remove command\n");
				spin_unlock_bh(&exeq->lock);
				return rc;
			}
			list_del(&exeq_pos->link);
		}
	}

	spin_unlock_bh(&exeq->lock);

	/* Prepare a command request */
	memset(&p, 0, sizeof(p));
	p.vlan_mac_obj = o;
	p.ramrod_flags = *ramrod_flags;
	p.user_req.cmd = BNX2X_VLAN_MAC_DEL;

	/*
	 * Add all but the last VLAN-MAC to the execution queue without actually
	 * execution anything.
	 */
	__clear_bit(RAMROD_COMP_WAIT, &p.ramrod_flags);
	__clear_bit(RAMROD_EXEC, &p.ramrod_flags);
	__clear_bit(RAMROD_CONT, &p.ramrod_flags);

	list_for_each_entry(pos, &o->head, link) {
		if (pos->vlan_mac_flags == *vlan_mac_flags) {
			p.user_req.vlan_mac_flags = pos->vlan_mac_flags;
			memcpy(&p.user_req.u, &pos->u, sizeof(pos->u));
			rc = bnx2x_config_vlan_mac(bp, &p);
			if (rc < 0) {
				BNX2X_ERR("Failed to add a new DEL command\n");
				return rc;
			}
		}
	}

	p.ramrod_flags = *ramrod_flags;
	__set_bit(RAMROD_CONT, &p.ramrod_flags);

	return bnx2x_config_vlan_mac(bp, &p);
}

static inline void bnx2x_init_raw_obj(struct bnx2x_raw_obj *raw, u8 cl_id,
	u32 cid, u8 func_id, void *rdata, dma_addr_t rdata_mapping, int state,
	unsigned long *pstate, bnx2x_obj_type type)
{
	raw->func_id = func_id;
	raw->cid = cid;
	raw->cl_id = cl_id;
	raw->rdata = rdata;
	raw->rdata_mapping = rdata_mapping;
	raw->state = state;
	raw->pstate = pstate;
	raw->obj_type = type;
	raw->check_pending = bnx2x_raw_check_pending;
	raw->clear_pending = bnx2x_raw_clear_pending;
	raw->set_pending = bnx2x_raw_set_pending;
	raw->wait_comp = bnx2x_raw_wait;
}

static inline void bnx2x_init_vlan_mac_common(struct bnx2x_vlan_mac_obj *o,
	u8 cl_id, u32 cid, u8 func_id, void *rdata, dma_addr_t rdata_mapping,
	int state, unsigned long *pstate, bnx2x_obj_type type,
	struct bnx2x_credit_pool_obj *macs_pool,
	struct bnx2x_credit_pool_obj *vlans_pool)
{
	INIT_LIST_HEAD(&o->head);

	o->macs_pool = macs_pool;
	o->vlans_pool = vlans_pool;

	o->delete_all = bnx2x_vlan_mac_del_all;
	o->restore = bnx2x_vlan_mac_restore;
	o->complete = bnx2x_complete_vlan_mac;
	o->wait = bnx2x_wait_vlan_mac;

	bnx2x_init_raw_obj(&o->raw, cl_id, cid, func_id, rdata, rdata_mapping,
			   state, pstate, type);
}


void bnx2x_init_mac_obj(struct bnx2x *bp,
			struct bnx2x_vlan_mac_obj *mac_obj,
			u8 cl_id, u32 cid, u8 func_id, void *rdata,
			dma_addr_t rdata_mapping, int state,
			unsigned long *pstate, bnx2x_obj_type type,
			struct bnx2x_credit_pool_obj *macs_pool)
{
	union bnx2x_qable_obj *qable_obj = (union bnx2x_qable_obj *)mac_obj;

	bnx2x_init_vlan_mac_common(mac_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type,
				   macs_pool, NULL);

	/* CAM credit pool handling */
	mac_obj->get_credit = bnx2x_get_credit_mac;
	mac_obj->put_credit = bnx2x_put_credit_mac;
	mac_obj->get_cam_offset = bnx2x_get_cam_offset_mac;
	mac_obj->put_cam_offset = bnx2x_put_cam_offset_mac;

	if (CHIP_IS_E1x(bp)) {
		mac_obj->set_one_rule      = bnx2x_set_one_mac_e1x;
		mac_obj->check_del         = bnx2x_check_mac_del;
		mac_obj->check_add         = bnx2x_check_mac_add;
		mac_obj->check_move        = bnx2x_check_move_always_err;
		mac_obj->ramrod_cmd        = RAMROD_CMD_ID_ETH_SET_MAC;

		/* Exe Queue */
		bnx2x_exe_queue_init(bp,
				     &mac_obj->exe_queue, 1, qable_obj,
				     bnx2x_validate_vlan_mac,
				     bnx2x_remove_vlan_mac,
				     bnx2x_optimize_vlan_mac,
				     bnx2x_execute_vlan_mac,
				     bnx2x_exeq_get_mac);
	} else {
		mac_obj->set_one_rule      = bnx2x_set_one_mac_e2;
		mac_obj->check_del         = bnx2x_check_mac_del;
		mac_obj->check_add         = bnx2x_check_mac_add;
		mac_obj->check_move        = bnx2x_check_move;
		mac_obj->ramrod_cmd        =
			RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES;
		mac_obj->get_n_elements    = bnx2x_get_n_elements;

		/* Exe Queue */
		bnx2x_exe_queue_init(bp,
				     &mac_obj->exe_queue, CLASSIFY_RULES_COUNT,
				     qable_obj, bnx2x_validate_vlan_mac,
				     bnx2x_remove_vlan_mac,
				     bnx2x_optimize_vlan_mac,
				     bnx2x_execute_vlan_mac,
				     bnx2x_exeq_get_mac);
	}
}

void bnx2x_init_vlan_obj(struct bnx2x *bp,
			 struct bnx2x_vlan_mac_obj *vlan_obj,
			 u8 cl_id, u32 cid, u8 func_id, void *rdata,
			 dma_addr_t rdata_mapping, int state,
			 unsigned long *pstate, bnx2x_obj_type type,
			 struct bnx2x_credit_pool_obj *vlans_pool)
{
	union bnx2x_qable_obj *qable_obj = (union bnx2x_qable_obj *)vlan_obj;

	bnx2x_init_vlan_mac_common(vlan_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type, NULL,
				   vlans_pool);

	vlan_obj->get_credit = bnx2x_get_credit_vlan;
	vlan_obj->put_credit = bnx2x_put_credit_vlan;
	vlan_obj->get_cam_offset = bnx2x_get_cam_offset_vlan;
	vlan_obj->put_cam_offset = bnx2x_put_cam_offset_vlan;

	if (CHIP_IS_E1x(bp)) {
		BNX2X_ERR("Do not support chips others than E2 and newer\n");
		BUG();
	} else {
		vlan_obj->set_one_rule      = bnx2x_set_one_vlan_e2;
		vlan_obj->check_del         = bnx2x_check_vlan_del;
		vlan_obj->check_add         = bnx2x_check_vlan_add;
		vlan_obj->check_move        = bnx2x_check_move;
		vlan_obj->ramrod_cmd        =
			RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES;

		/* Exe Queue */
		bnx2x_exe_queue_init(bp,
				     &vlan_obj->exe_queue, CLASSIFY_RULES_COUNT,
				     qable_obj, bnx2x_validate_vlan_mac,
				     bnx2x_remove_vlan_mac,
				     bnx2x_optimize_vlan_mac,
				     bnx2x_execute_vlan_mac,
				     bnx2x_exeq_get_vlan);
	}
}

void bnx2x_init_vlan_mac_obj(struct bnx2x *bp,
			     struct bnx2x_vlan_mac_obj *vlan_mac_obj,
			     u8 cl_id, u32 cid, u8 func_id, void *rdata,
			     dma_addr_t rdata_mapping, int state,
			     unsigned long *pstate, bnx2x_obj_type type,
			     struct bnx2x_credit_pool_obj *macs_pool,
			     struct bnx2x_credit_pool_obj *vlans_pool)
{
	union bnx2x_qable_obj *qable_obj =
		(union bnx2x_qable_obj *)vlan_mac_obj;

	bnx2x_init_vlan_mac_common(vlan_mac_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type,
				   macs_pool, vlans_pool);

	/* CAM pool handling */
	vlan_mac_obj->get_credit = bnx2x_get_credit_vlan_mac;
	vlan_mac_obj->put_credit = bnx2x_put_credit_vlan_mac;
	/*
	 * CAM offset is relevant for 57710 and 57711 chips only which have a
	 * single CAM for both MACs and VLAN-MAC pairs. So the offset
	 * will be taken from MACs' pool object only.
	 */
	vlan_mac_obj->get_cam_offset = bnx2x_get_cam_offset_mac;
	vlan_mac_obj->put_cam_offset = bnx2x_put_cam_offset_mac;

	if (CHIP_IS_E1(bp)) {
		BNX2X_ERR("Do not support chips others than E2\n");
		BUG();
	} else if (CHIP_IS_E1H(bp)) {
		vlan_mac_obj->set_one_rule      = bnx2x_set_one_vlan_mac_e1h;
		vlan_mac_obj->check_del         = bnx2x_check_vlan_mac_del;
		vlan_mac_obj->check_add         = bnx2x_check_vlan_mac_add;
		vlan_mac_obj->check_move        = bnx2x_check_move_always_err;
		vlan_mac_obj->ramrod_cmd        = RAMROD_CMD_ID_ETH_SET_MAC;

		/* Exe Queue */
		bnx2x_exe_queue_init(bp,
				     &vlan_mac_obj->exe_queue, 1, qable_obj,
				     bnx2x_validate_vlan_mac,
				     bnx2x_remove_vlan_mac,
				     bnx2x_optimize_vlan_mac,
				     bnx2x_execute_vlan_mac,
				     bnx2x_exeq_get_vlan_mac);
	} else {
		vlan_mac_obj->set_one_rule      = bnx2x_set_one_vlan_mac_e2;
		vlan_mac_obj->check_del         = bnx2x_check_vlan_mac_del;
		vlan_mac_obj->check_add         = bnx2x_check_vlan_mac_add;
		vlan_mac_obj->check_move        = bnx2x_check_move;
		vlan_mac_obj->ramrod_cmd        =
			RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES;

		/* Exe Queue */
		bnx2x_exe_queue_init(bp,
				     &vlan_mac_obj->exe_queue,
				     CLASSIFY_RULES_COUNT,
				     qable_obj, bnx2x_validate_vlan_mac,
				     bnx2x_remove_vlan_mac,
				     bnx2x_optimize_vlan_mac,
				     bnx2x_execute_vlan_mac,
				     bnx2x_exeq_get_vlan_mac);
	}

}

/* RX_MODE verbs: DROP_ALL/ACCEPT_ALL/ACCEPT_ALL_MULTI/ACCEPT_ALL_VLAN/NORMAL */
static inline void __storm_memset_mac_filters(struct bnx2x *bp,
			struct tstorm_eth_mac_filter_config *mac_filters,
			u16 pf_id)
{
	size_t size = sizeof(struct tstorm_eth_mac_filter_config);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_MAC_FILTER_CONFIG_OFFSET(pf_id);

	__storm_memset_struct(bp, addr, size, (u32 *)mac_filters);
}

static int bnx2x_set_rx_mode_e1x(struct bnx2x *bp,
				 struct bnx2x_rx_mode_ramrod_params *p)
{
	/* update the bp MAC filter structure  */
	u32 mask = (1 << p->cl_id);

	struct tstorm_eth_mac_filter_config *mac_filters =
		(struct tstorm_eth_mac_filter_config *)p->rdata;

	/* initial seeting is drop-all */
	u8 drop_all_ucast = 1, drop_all_mcast = 1;
	u8 accp_all_ucast = 0, accp_all_bcast = 0, accp_all_mcast = 0;
	u8 unmatched_unicast = 0;

    /* In e1x there we only take into account rx acceot flag since tx switching
     * isn't enabled. */
	if (test_bit(BNX2X_ACCEPT_UNICAST, &p->rx_accept_flags))
		/* accept matched ucast */
		drop_all_ucast = 0;

	if (test_bit(BNX2X_ACCEPT_MULTICAST, &p->rx_accept_flags))
		/* accept matched mcast */
		drop_all_mcast = 0;

	if (test_bit(BNX2X_ACCEPT_ALL_UNICAST, &p->rx_accept_flags)) {
		/* accept all mcast */
		drop_all_ucast = 0;
		accp_all_ucast = 1;
	}
	if (test_bit(BNX2X_ACCEPT_ALL_MULTICAST, &p->rx_accept_flags)) {
		/* accept all mcast */
		drop_all_mcast = 0;
		accp_all_mcast = 1;
	}
	if (test_bit(BNX2X_ACCEPT_BROADCAST, &p->rx_accept_flags))
		/* accept (all) bcast */
		accp_all_bcast = 1;
	if (test_bit(BNX2X_ACCEPT_UNMATCHED, &p->rx_accept_flags))
		/* accept unmatched unicasts */
		unmatched_unicast = 1;

	mac_filters->ucast_drop_all = drop_all_ucast ?
		mac_filters->ucast_drop_all | mask :
		mac_filters->ucast_drop_all & ~mask;

	mac_filters->mcast_drop_all = drop_all_mcast ?
		mac_filters->mcast_drop_all | mask :
		mac_filters->mcast_drop_all & ~mask;

	mac_filters->ucast_accept_all = accp_all_ucast ?
		mac_filters->ucast_accept_all | mask :
		mac_filters->ucast_accept_all & ~mask;

	mac_filters->mcast_accept_all = accp_all_mcast ?
		mac_filters->mcast_accept_all | mask :
		mac_filters->mcast_accept_all & ~mask;

	mac_filters->bcast_accept_all = accp_all_bcast ?
		mac_filters->bcast_accept_all | mask :
		mac_filters->bcast_accept_all & ~mask;

	mac_filters->unmatched_unicast = unmatched_unicast ?
		mac_filters->unmatched_unicast | mask :
		mac_filters->unmatched_unicast & ~mask;

	DP(BNX2X_MSG_SP, "drop_ucast 0x%x\ndrop_mcast 0x%x\n accp_ucast 0x%x\n"
					 "accp_mcast 0x%x\naccp_bcast 0x%x\n",
	   mac_filters->ucast_drop_all, mac_filters->mcast_drop_all,
	   mac_filters->ucast_accept_all, mac_filters->mcast_accept_all,
	   mac_filters->bcast_accept_all);

	/* write the MAC filter structure*/
	__storm_memset_mac_filters(bp, mac_filters, p->func_id);

	/* The operation is completed */
	clear_bit(p->state, p->pstate);
	smp_mb__after_clear_bit();

	return 0;
}

/* Setup ramrod data */
static inline void bnx2x_rx_mode_set_rdata_hdr_e2(u32 cid,
				struct eth_classify_header *hdr,
				u8 rule_cnt)
{
	hdr->echo = cid;
	hdr->rule_cnt = rule_cnt;
}

static inline void bnx2x_rx_mode_set_cmd_state_e2(struct bnx2x *bp,
				unsigned long accept_flags,
				struct eth_filter_rules_cmd *cmd,
				bool clear_accept_all)
{
	u16 state;

	/* start with 'drop-all' */
	state = ETH_FILTER_RULES_CMD_UCAST_DROP_ALL |
		ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;

	if (accept_flags) {
		if (test_bit(BNX2X_ACCEPT_UNICAST, &accept_flags))
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;

		if (test_bit(BNX2X_ACCEPT_MULTICAST, &accept_flags))
			state &= ~ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;

		if (test_bit(BNX2X_ACCEPT_ALL_UNICAST, &accept_flags)) {
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;
			state |= ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL;
		}

		if (test_bit(BNX2X_ACCEPT_ALL_MULTICAST, &accept_flags)) {
			state |= ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL;
			state &= ~ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;
		}
		if (test_bit(BNX2X_ACCEPT_BROADCAST, &accept_flags))
			state |= ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL;

		if (test_bit(BNX2X_ACCEPT_UNMATCHED, &accept_flags)) {
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;
			state |= ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED;
		}
		if (test_bit(BNX2X_ACCEPT_ANY_VLAN, &accept_flags))
			state |= ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN;
	}

	/* Clear ACCEPT_ALL_XXX flags for FCoE L2 Queue */
	if (clear_accept_all) {
		state &= ~ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED;
	}

	cmd->state = cpu_to_le16(state);

}

static int bnx2x_set_rx_mode_e2(struct bnx2x *bp,
				struct bnx2x_rx_mode_ramrod_params *p)
{
	struct eth_filter_rules_ramrod_data *data = p->rdata;
	int rc;
	u8 rule_idx = 0;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */

	/* Tx (internal switching) */
	if (test_bit(RAMROD_TX, &p->ramrod_flags)) {
		data->rules[rule_idx].client_id = p->cl_id;
		data->rules[rule_idx].func_id = p->func_id;

		data->rules[rule_idx].cmd_general_data =
			ETH_FILTER_RULES_CMD_TX_CMD;

		bnx2x_rx_mode_set_cmd_state_e2(bp, p->tx_accept_flags,
			&(data->rules[rule_idx++]), false);
	}

	/* Rx */
	if (test_bit(RAMROD_RX, &p->ramrod_flags)) {
		data->rules[rule_idx].client_id = p->cl_id;
		data->rules[rule_idx].func_id = p->func_id;

		data->rules[rule_idx].cmd_general_data =
			ETH_FILTER_RULES_CMD_RX_CMD;

		bnx2x_rx_mode_set_cmd_state_e2(bp, p->rx_accept_flags,
			&(data->rules[rule_idx++]), false);
	}


	/*
	 * If FCoE Queue configuration has been requested configure the Rx and
	 * internal switching modes for this queue in separate rules.
	 *
	 * FCoE queue shell never be set to ACCEPT_ALL packets of any sort:
	 * MCAST_ALL, UCAST_ALL, BCAST_ALL and UNMATCHED.
	 */
	if (test_bit(BNX2X_RX_MODE_FCOE_ETH, &p->rx_mode_flags)) {
		/*  Tx (internal switching) */
		if (test_bit(RAMROD_TX, &p->ramrod_flags)) {
			data->rules[rule_idx].client_id = bnx2x_fcoe(bp, cl_id);
			data->rules[rule_idx].func_id = p->func_id;

			data->rules[rule_idx].cmd_general_data =
						ETH_FILTER_RULES_CMD_TX_CMD;

			bnx2x_rx_mode_set_cmd_state_e2(bp, p->tx_accept_flags,
						     &(data->rules[rule_idx++]),
						       true);
		}

		/* Rx */
		if (test_bit(RAMROD_RX, &p->ramrod_flags)) {
			data->rules[rule_idx].client_id = bnx2x_fcoe(bp, cl_id);
			data->rules[rule_idx].func_id = p->func_id;

			data->rules[rule_idx].cmd_general_data =
						ETH_FILTER_RULES_CMD_RX_CMD;

			bnx2x_rx_mode_set_cmd_state_e2(bp, p->rx_accept_flags,
						     &(data->rules[rule_idx++]),
						       true);
		}
	}

	/*
	 * Set the ramrod header (most importantly - number of rules to
	 * configure).
	 */
	bnx2x_rx_mode_set_rdata_hdr_e2(p->cid, &data->header, rule_idx);

	DP(BNX2X_MSG_SP, "About to configure %d rules, rx_accept_flags 0x%lx, tx_accept_flags 0x%lx\n",
			 data->header.rule_cnt, p->rx_accept_flags,
			 p->tx_accept_flags);

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	/* Send a ramrod */
	rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_FILTER_RULES, p->cid,
			   U64_HI(p->rdata_mapping),
			   U64_LO(p->rdata_mapping),
			   ETH_CONNECTION_TYPE);
	if (rc)
		return rc;

	/* Ramrod completion is pending */
	return 1;
}

static int bnx2x_wait_rx_mode_comp_e2(struct bnx2x *bp,
				      struct bnx2x_rx_mode_ramrod_params *p)
{
	return bnx2x_state_wait(bp, p->state, p->pstate);
}

static int bnx2x_empty_rx_mode_wait(struct bnx2x *bp,
				    struct bnx2x_rx_mode_ramrod_params *p)
{
	/* Do nothing */
	return 0;
}

int bnx2x_config_rx_mode(struct bnx2x *bp,
			 struct bnx2x_rx_mode_ramrod_params *p)
{
	int rc;

	/* Configure the new classification in the chip */
	rc = p->rx_mode_obj->config_rx_mode(bp, p);
	if (rc < 0)
		return rc;

	/* Wait for a ramrod completion if was requested */
	if (test_bit(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = p->rx_mode_obj->wait_comp(bp, p);
		if (rc)
			return rc;
	}

	return rc;
}

void bnx2x_init_rx_mode_obj(struct bnx2x *bp,
			    struct bnx2x_rx_mode_obj *o)
{
	if (CHIP_IS_E1x(bp)) {
		o->wait_comp      = bnx2x_empty_rx_mode_wait;
		o->config_rx_mode = bnx2x_set_rx_mode_e1x;
	} else {
		o->wait_comp      = bnx2x_wait_rx_mode_comp_e2;
		o->config_rx_mode = bnx2x_set_rx_mode_e2;
	}
}

/********************* Multicast verbs: SET, CLEAR ****************************/
static inline u8 bnx2x_mcast_bin_from_mac(u8 *mac)
{
	return (crc32c_le(0, mac, ETH_ALEN) >> 24) & 0xff;
}

struct bnx2x_mcast_mac_elem {
	struct list_head link;
	u8 mac[ETH_ALEN];
	u8 pad[2]; /* For a natural alignment of the following buffer */
};

struct bnx2x_pending_mcast_cmd {
	struct list_head link;
	int type; /* BNX2X_MCAST_CMD_X */
	union {
		struct list_head macs_head;
		u32 macs_num; /* Needed for DEL command */
		int next_bin; /* Needed for RESTORE flow with aprox match */
	} data;

	bool done; /* set to true, when the command has been handled,
		    * practically used in 57712 handling only, where one pending
		    * command may be handled in a few operations. As long as for
		    * other chips every operation handling is completed in a
		    * single ramrod, there is no need to utilize this field.
		    */
};

static int bnx2x_mcast_wait(struct bnx2x *bp,
			    struct bnx2x_mcast_obj *o)
{
	if (bnx2x_state_wait(bp, o->sched_state, o->raw.pstate) ||
			o->raw.wait_comp(bp, &o->raw))
		return -EBUSY;

	return 0;
}

static int bnx2x_mcast_enqueue_cmd(struct bnx2x *bp,
				   struct bnx2x_mcast_obj *o,
				   struct bnx2x_mcast_ramrod_params *p,
				   int cmd)
{
	int total_sz;
	struct bnx2x_pending_mcast_cmd *new_cmd;
	struct bnx2x_mcast_mac_elem *cur_mac = NULL;
	struct bnx2x_mcast_list_elem *pos;
	int macs_list_len = ((cmd == BNX2X_MCAST_CMD_ADD) ?
			     p->mcast_list_len : 0);

	/* If the command is empty ("handle pending commands only"), break */
	if (!p->mcast_list_len)
		return 0;

	total_sz = sizeof(*new_cmd) +
		macs_list_len * sizeof(struct bnx2x_mcast_mac_elem);

	/* Add mcast is called under spin_lock, thus calling with GFP_ATOMIC */
	new_cmd = kzalloc(total_sz, GFP_ATOMIC);

	if (!new_cmd)
		return -ENOMEM;

	DP(BNX2X_MSG_SP, "About to enqueue a new %d command. macs_list_len=%d\n",
	   cmd, macs_list_len);

	INIT_LIST_HEAD(&new_cmd->data.macs_head);

	new_cmd->type = cmd;
	new_cmd->done = false;

	switch (cmd) {
	case BNX2X_MCAST_CMD_ADD:
		cur_mac = (struct bnx2x_mcast_mac_elem *)
			  ((u8 *)new_cmd + sizeof(*new_cmd));

		/* Push the MACs of the current command into the pendig command
		 * MACs list: FIFO
		 */
		list_for_each_entry(pos, &p->mcast_list, link) {
			memcpy(cur_mac->mac, pos->mac, ETH_ALEN);
			list_add_tail(&cur_mac->link, &new_cmd->data.macs_head);
			cur_mac++;
		}

		break;

	case BNX2X_MCAST_CMD_DEL:
		new_cmd->data.macs_num = p->mcast_list_len;
		break;

	case BNX2X_MCAST_CMD_RESTORE:
		new_cmd->data.next_bin = 0;
		break;

	default:
		kfree(new_cmd);
		BNX2X_ERR("Unknown command: %d\n", cmd);
		return -EINVAL;
	}

	/* Push the new pending command to the tail of the pending list: FIFO */
	list_add_tail(&new_cmd->link, &o->pending_cmds_head);

	o->set_sched(o);

	return 1;
}

/**
 * bnx2x_mcast_get_next_bin - get the next set bin (index)
 *
 * @o:
 * @last:	index to start looking from (including)
 *
 * returns the next found (set) bin or a negative value if none is found.
 */
static inline int bnx2x_mcast_get_next_bin(struct bnx2x_mcast_obj *o, int last)
{
	int i, j, inner_start = last % BIT_VEC64_ELEM_SZ;

	for (i = last / BIT_VEC64_ELEM_SZ; i < BNX2X_MCAST_VEC_SZ; i++) {
		if (o->registry.aprox_match.vec[i])
			for (j = inner_start; j < BIT_VEC64_ELEM_SZ; j++) {
				int cur_bit = j + BIT_VEC64_ELEM_SZ * i;
				if (BIT_VEC64_TEST_BIT(o->registry.aprox_match.
						       vec, cur_bit)) {
					return cur_bit;
				}
			}
		inner_start = 0;
	}

	/* None found */
	return -1;
}

/**
 * bnx2x_mcast_clear_first_bin - find the first set bin and clear it
 *
 * @o:
 *
 * returns the index of the found bin or -1 if none is found
 */
static inline int bnx2x_mcast_clear_first_bin(struct bnx2x_mcast_obj *o)
{
	int cur_bit = bnx2x_mcast_get_next_bin(o, 0);

	if (cur_bit >= 0)
		BIT_VEC64_CLEAR_BIT(o->registry.aprox_match.vec, cur_bit);

	return cur_bit;
}

static inline u8 bnx2x_mcast_get_rx_tx_flag(struct bnx2x_mcast_obj *o)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	u8 rx_tx_flag = 0;

	if ((raw->obj_type == BNX2X_OBJ_TYPE_TX) ||
	    (raw->obj_type == BNX2X_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_MULTICAST_RULES_CMD_TX_CMD;

	if ((raw->obj_type == BNX2X_OBJ_TYPE_RX) ||
	    (raw->obj_type == BNX2X_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_MULTICAST_RULES_CMD_RX_CMD;

	return rx_tx_flag;
}

static void bnx2x_mcast_set_one_rule_e2(struct bnx2x *bp,
					struct bnx2x_mcast_obj *o, int idx,
					union bnx2x_mcast_config_data *cfg_data,
					int cmd)
{
	struct bnx2x_raw_obj *r = &o->raw;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(r->rdata);
	u8 func_id = r->func_id;
	u8 rx_tx_add_flag = bnx2x_mcast_get_rx_tx_flag(o);
	int bin;

	if ((cmd == BNX2X_MCAST_CMD_ADD) || (cmd == BNX2X_MCAST_CMD_RESTORE))
		rx_tx_add_flag |= ETH_MULTICAST_RULES_CMD_IS_ADD;

	data->rules[idx].cmd_general_data |= rx_tx_add_flag;

	/* Get a bin and update a bins' vector */
	switch (cmd) {
	case BNX2X_MCAST_CMD_ADD:
		bin = bnx2x_mcast_bin_from_mac(cfg_data->mac);
		BIT_VEC64_SET_BIT(o->registry.aprox_match.vec, bin);
		break;

	case BNX2X_MCAST_CMD_DEL:
		/* If there were no more bins to clear
		 * (bnx2x_mcast_clear_first_bin() returns -1) then we would
		 * clear any (0xff) bin.
		 * See bnx2x_mcast_validate_e2() for explanation when it may
		 * happen.
		 */
		bin = bnx2x_mcast_clear_first_bin(o);
		break;

	case BNX2X_MCAST_CMD_RESTORE:
		bin = cfg_data->bin;
		break;

	default:
		BNX2X_ERR("Unknown command: %d\n", cmd);
		return;
	}

	DP(BNX2X_MSG_SP, "%s bin %d\n",
			 ((rx_tx_add_flag & ETH_MULTICAST_RULES_CMD_IS_ADD) ?
			 "Setting"  : "Clearing"), bin);

	data->rules[idx].bin_id    = (u8)bin;
	data->rules[idx].func_id   = func_id;
	data->rules[idx].engine_id = o->engine_id;
}

/**
 * bnx2x_mcast_handle_restore_cmd_e2 - restore configuration from the registry
 *
 * @bp:		device handle
 * @o:
 * @start_bin:	index in the registry to start from (including)
 * @rdata_idx:	index in the ramrod data to start from
 *
 * returns last handled bin index or -1 if all bins have been handled
 */
static inline int bnx2x_mcast_handle_restore_cmd_e2(
	struct bnx2x *bp, struct bnx2x_mcast_obj *o , int start_bin,
	int *rdata_idx)
{
	int cur_bin, cnt = *rdata_idx;
	union bnx2x_mcast_config_data cfg_data = {0};

	/* go through the registry and configure the bins from it */
	for (cur_bin = bnx2x_mcast_get_next_bin(o, start_bin); cur_bin >= 0;
	    cur_bin = bnx2x_mcast_get_next_bin(o, cur_bin + 1)) {

		cfg_data.bin = (u8)cur_bin;
		o->set_one_rule(bp, o, cnt, &cfg_data,
				BNX2X_MCAST_CMD_RESTORE);

		cnt++;

		DP(BNX2X_MSG_SP, "About to configure a bin %d\n", cur_bin);

		/* Break if we reached the maximum number
		 * of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*rdata_idx = cnt;

	return cur_bin;
}

static inline void bnx2x_mcast_hdl_pending_add_e2(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	struct bnx2x_mcast_mac_elem *pmac_pos, *pmac_pos_n;
	int cnt = *line_idx;
	union bnx2x_mcast_config_data cfg_data = {0};

	list_for_each_entry_safe(pmac_pos, pmac_pos_n, &cmd_pos->data.macs_head,
				 link) {

		cfg_data.mac = &pmac_pos->mac[0];
		o->set_one_rule(bp, o, cnt, &cfg_data, cmd_pos->type);

		cnt++;

		DP(BNX2X_MSG_SP, "About to configure %pM mcast MAC\n",
		   pmac_pos->mac);

		list_del(&pmac_pos->link);

		/* Break if we reached the maximum number
		 * of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*line_idx = cnt;

	/* if no more MACs to configure - we are done */
	if (list_empty(&cmd_pos->data.macs_head))
		cmd_pos->done = true;
}

static inline void bnx2x_mcast_hdl_pending_del_e2(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	int cnt = *line_idx;

	while (cmd_pos->data.macs_num) {
		o->set_one_rule(bp, o, cnt, NULL, cmd_pos->type);

		cnt++;

		cmd_pos->data.macs_num--;

		  DP(BNX2X_MSG_SP, "Deleting MAC. %d left,cnt is %d\n",
				   cmd_pos->data.macs_num, cnt);

		/* Break if we reached the maximum
		 * number of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*line_idx = cnt;

	/* If we cleared all bins - we are done */
	if (!cmd_pos->data.macs_num)
		cmd_pos->done = true;
}

static inline void bnx2x_mcast_hdl_pending_restore_e2(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	cmd_pos->data.next_bin = o->hdl_restore(bp, o, cmd_pos->data.next_bin,
						line_idx);

	if (cmd_pos->data.next_bin < 0)
		/* If o->set_restore returned -1 we are done */
		cmd_pos->done = true;
	else
		/* Start from the next bin next time */
		cmd_pos->data.next_bin++;
}

static inline int bnx2x_mcast_handle_pending_cmds_e2(struct bnx2x *bp,
				struct bnx2x_mcast_ramrod_params *p)
{
	struct bnx2x_pending_mcast_cmd *cmd_pos, *cmd_pos_n;
	int cnt = 0;
	struct bnx2x_mcast_obj *o = p->mcast_obj;

	list_for_each_entry_safe(cmd_pos, cmd_pos_n, &o->pending_cmds_head,
				 link) {
		switch (cmd_pos->type) {
		case BNX2X_MCAST_CMD_ADD:
			bnx2x_mcast_hdl_pending_add_e2(bp, o, cmd_pos, &cnt);
			break;

		case BNX2X_MCAST_CMD_DEL:
			bnx2x_mcast_hdl_pending_del_e2(bp, o, cmd_pos, &cnt);
			break;

		case BNX2X_MCAST_CMD_RESTORE:
			bnx2x_mcast_hdl_pending_restore_e2(bp, o, cmd_pos,
							   &cnt);
			break;

		default:
			BNX2X_ERR("Unknown command: %d\n", cmd_pos->type);
			return -EINVAL;
		}

		/* If the command has been completed - remove it from the list
		 * and free the memory
		 */
		if (cmd_pos->done) {
			list_del(&cmd_pos->link);
			kfree(cmd_pos);
		}

		/* Break if we reached the maximum number of rules */
		if (cnt >= o->max_cmd_len)
			break;
	}

	return cnt;
}

static inline void bnx2x_mcast_hdl_add(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_mcast_ramrod_params *p,
	int *line_idx)
{
	struct bnx2x_mcast_list_elem *mlist_pos;
	union bnx2x_mcast_config_data cfg_data = {0};
	int cnt = *line_idx;

	list_for_each_entry(mlist_pos, &p->mcast_list, link) {
		cfg_data.mac = mlist_pos->mac;
		o->set_one_rule(bp, o, cnt, &cfg_data, BNX2X_MCAST_CMD_ADD);

		cnt++;

		DP(BNX2X_MSG_SP, "About to configure %pM mcast MAC\n",
				 mlist_pos->mac);
	}

	*line_idx = cnt;
}

static inline void bnx2x_mcast_hdl_del(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_mcast_ramrod_params *p,
	int *line_idx)
{
	int cnt = *line_idx, i;

	for (i = 0; i < p->mcast_list_len; i++) {
		o->set_one_rule(bp, o, cnt, NULL, BNX2X_MCAST_CMD_DEL);

		cnt++;

		DP(BNX2X_MSG_SP, "Deleting MAC. %d left\n",
				 p->mcast_list_len - i - 1);
	}

	*line_idx = cnt;
}

/**
 * bnx2x_mcast_handle_current_cmd -
 *
 * @bp:		device handle
 * @p:
 * @cmd:
 * @start_cnt:	first line in the ramrod data that may be used
 *
 * This function is called iff there is enough place for the current command in
 * the ramrod data.
 * Returns number of lines filled in the ramrod data in total.
 */
static inline int bnx2x_mcast_handle_current_cmd(struct bnx2x *bp,
			struct bnx2x_mcast_ramrod_params *p, int cmd,
			int start_cnt)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	int cnt = start_cnt;

	DP(BNX2X_MSG_SP, "p->mcast_list_len=%d\n", p->mcast_list_len);

	switch (cmd) {
	case BNX2X_MCAST_CMD_ADD:
		bnx2x_mcast_hdl_add(bp, o, p, &cnt);
		break;

	case BNX2X_MCAST_CMD_DEL:
		bnx2x_mcast_hdl_del(bp, o, p, &cnt);
		break;

	case BNX2X_MCAST_CMD_RESTORE:
		o->hdl_restore(bp, o, 0, &cnt);
		break;

	default:
		BNX2X_ERR("Unknown command: %d\n", cmd);
		return -EINVAL;
	}

	/* The current command has been handled */
	p->mcast_list_len = 0;

	return cnt;
}

static int bnx2x_mcast_validate_e2(struct bnx2x *bp,
				   struct bnx2x_mcast_ramrod_params *p,
				   int cmd)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	int reg_sz = o->get_registry_size(o);

	switch (cmd) {
	/* DEL command deletes all currently configured MACs */
	case BNX2X_MCAST_CMD_DEL:
		o->set_registry_size(o, 0);
		/* Don't break */

	/* RESTORE command will restore the entire multicast configuration */
	case BNX2X_MCAST_CMD_RESTORE:
		/* Here we set the approximate amount of work to do, which in
		 * fact may be only less as some MACs in postponed ADD
		 * command(s) scheduled before this command may fall into
		 * the same bin and the actual number of bins set in the
		 * registry would be less than we estimated here. See
		 * bnx2x_mcast_set_one_rule_e2() for further details.
		 */
		p->mcast_list_len = reg_sz;
		break;

	case BNX2X_MCAST_CMD_ADD:
	case BNX2X_MCAST_CMD_CONT:
		/* Here we assume that all new MACs will fall into new bins.
		 * However we will correct the real registry size after we
		 * handle all pending commands.
		 */
		o->set_registry_size(o, reg_sz + p->mcast_list_len);
		break;

	default:
		BNX2X_ERR("Unknown command: %d\n", cmd);
		return -EINVAL;

	}

	/* Increase the total number of MACs pending to be configured */
	o->total_pending_num += p->mcast_list_len;

	return 0;
}

static void bnx2x_mcast_revert_e2(struct bnx2x *bp,
				      struct bnx2x_mcast_ramrod_params *p,
				      int old_num_bins)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;

	o->set_registry_size(o, old_num_bins);
	o->total_pending_num -= p->mcast_list_len;
}

/**
 * bnx2x_mcast_set_rdata_hdr_e2 - sets a header values
 *
 * @bp:		device handle
 * @p:
 * @len:	number of rules to handle
 */
static inline void bnx2x_mcast_set_rdata_hdr_e2(struct bnx2x *bp,
					struct bnx2x_mcast_ramrod_params *p,
					u8 len)
{
	struct bnx2x_raw_obj *r = &p->mcast_obj->raw;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(r->rdata);

	data->header.echo = ((r->cid & BNX2X_SWCID_MASK) |
			  (BNX2X_FILTER_MCAST_PENDING << BNX2X_SWCID_SHIFT));
	data->header.rule_cnt = len;
}

/**
 * bnx2x_mcast_refresh_registry_e2 - recalculate the actual number of set bins
 *
 * @bp:		device handle
 * @o:
 *
 * Recalculate the actual number of set bins in the registry using Brian
 * Kernighan's algorithm: it's execution complexity is as a number of set bins.
 *
 * returns 0 for the compliance with bnx2x_mcast_refresh_registry_e1().
 */
static inline int bnx2x_mcast_refresh_registry_e2(struct bnx2x *bp,
						  struct bnx2x_mcast_obj *o)
{
	int i, cnt = 0;
	u64 elem;

	for (i = 0; i < BNX2X_MCAST_VEC_SZ; i++) {
		elem = o->registry.aprox_match.vec[i];
		for (; elem; cnt++)
			elem &= elem - 1;
	}

	o->set_registry_size(o, cnt);

	return 0;
}

static int bnx2x_mcast_setup_e2(struct bnx2x *bp,
				struct bnx2x_mcast_ramrod_params *p,
				int cmd)
{
	struct bnx2x_raw_obj *raw = &p->mcast_obj->raw;
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(raw->rdata);
	int cnt = 0, rc;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	cnt = bnx2x_mcast_handle_pending_cmds_e2(bp, p);

	/* If there are no more pending commands - clear SCHEDULED state */
	if (list_empty(&o->pending_cmds_head))
		o->clear_sched(o);

	/* The below may be true iff there was enough room in ramrod
	 * data for all pending commands and for the current
	 * command. Otherwise the current command would have been added
	 * to the pending commands and p->mcast_list_len would have been
	 * zeroed.
	 */
	if (p->mcast_list_len > 0)
		cnt = bnx2x_mcast_handle_current_cmd(bp, p, cmd, cnt);

	/* We've pulled out some MACs - update the total number of
	 * outstanding.
	 */
	o->total_pending_num -= cnt;

	/* send a ramrod */
	WARN_ON(o->total_pending_num < 0);
	WARN_ON(cnt > o->max_cmd_len);

	bnx2x_mcast_set_rdata_hdr_e2(bp, p, (u8)cnt);

	/* Update a registry size if there are no more pending operations.
	 *
	 * We don't want to change the value of the registry size if there are
	 * pending operations because we want it to always be equal to the
	 * exact or the approximate number (see bnx2x_mcast_validate_e2()) of
	 * set bins after the last requested operation in order to properly
	 * evaluate the size of the next DEL/RESTORE operation.
	 *
	 * Note that we update the registry itself during command(s) handling
	 * - see bnx2x_mcast_set_one_rule_e2(). That's because for 57712 we
	 * aggregate multiple commands (ADD/DEL/RESTORE) into one ramrod but
	 * with a limited amount of update commands (per MAC/bin) and we don't
	 * know in this scope what the actual state of bins configuration is
	 * going to be after this ramrod.
	 */
	if (!o->total_pending_num)
		bnx2x_mcast_refresh_registry_e2(bp, o);

	/*
	 * If CLEAR_ONLY was requested - don't send a ramrod and clear
	 * RAMROD_PENDING status immediately.
	 */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		raw->clear_pending(raw);
		return 0;
	} else {
		/*
		 *  No need for an explicit memory barrier here as long we would
		 *  need to ensure the ordering of writing to the SPQ element
		 *  and updating of the SPQ producer which involves a memory
		 *  read and we will have to put a full memory barrier there
		 *  (inside bnx2x_sp_post()).
		 */

		/* Send a ramrod */
		rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_MULTICAST_RULES,
				   raw->cid, U64_HI(raw->rdata_mapping),
				   U64_LO(raw->rdata_mapping),
				   ETH_CONNECTION_TYPE);
		if (rc)
			return rc;

		/* Ramrod completion is pending */
		return 1;
	}
}

static int bnx2x_mcast_validate_e1h(struct bnx2x *bp,
				    struct bnx2x_mcast_ramrod_params *p,
				    int cmd)
{
	/* Mark, that there is a work to do */
	if ((cmd == BNX2X_MCAST_CMD_DEL) || (cmd == BNX2X_MCAST_CMD_RESTORE))
		p->mcast_list_len = 1;

	return 0;
}

static void bnx2x_mcast_revert_e1h(struct bnx2x *bp,
				       struct bnx2x_mcast_ramrod_params *p,
				       int old_num_bins)
{
	/* Do nothing */
}

#define BNX2X_57711_SET_MC_FILTER(filter, bit) \
do { \
	(filter)[(bit) >> 5] |= (1 << ((bit) & 0x1f)); \
} while (0)

static inline void bnx2x_mcast_hdl_add_e1h(struct bnx2x *bp,
					   struct bnx2x_mcast_obj *o,
					   struct bnx2x_mcast_ramrod_params *p,
					   u32 *mc_filter)
{
	struct bnx2x_mcast_list_elem *mlist_pos;
	int bit;

	list_for_each_entry(mlist_pos, &p->mcast_list, link) {
		bit = bnx2x_mcast_bin_from_mac(mlist_pos->mac);
		BNX2X_57711_SET_MC_FILTER(mc_filter, bit);

		DP(BNX2X_MSG_SP, "About to configure %pM mcast MAC, bin %d\n",
				 mlist_pos->mac, bit);

		/* bookkeeping... */
		BIT_VEC64_SET_BIT(o->registry.aprox_match.vec,
				  bit);
	}
}

static inline void bnx2x_mcast_hdl_restore_e1h(struct bnx2x *bp,
	struct bnx2x_mcast_obj *o, struct bnx2x_mcast_ramrod_params *p,
	u32 *mc_filter)
{
	int bit;

	for (bit = bnx2x_mcast_get_next_bin(o, 0);
	     bit >= 0;
	     bit = bnx2x_mcast_get_next_bin(o, bit + 1)) {
		BNX2X_57711_SET_MC_FILTER(mc_filter, bit);
		DP(BNX2X_MSG_SP, "About to set bin %d\n", bit);
	}
}

/* On 57711 we write the multicast MACs' aproximate match
 * table by directly into the TSTORM's internal RAM. So we don't
 * really need to handle any tricks to make it work.
 */
static int bnx2x_mcast_setup_e1h(struct bnx2x *bp,
				 struct bnx2x_mcast_ramrod_params *p,
				 int cmd)
{
	int i;
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	struct bnx2x_raw_obj *r = &o->raw;

	/* If CLEAR_ONLY has been requested - clear the registry
	 * and clear a pending bit.
	 */
	if (!test_bit(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		u32 mc_filter[MC_HASH_SIZE] = {0};

		/* Set the multicast filter bits before writing it into
		 * the internal memory.
		 */
		switch (cmd) {
		case BNX2X_MCAST_CMD_ADD:
			bnx2x_mcast_hdl_add_e1h(bp, o, p, mc_filter);
			break;

		case BNX2X_MCAST_CMD_DEL:
			DP(BNX2X_MSG_SP,
			   "Invalidating multicast MACs configuration\n");

			/* clear the registry */
			memset(o->registry.aprox_match.vec, 0,
			       sizeof(o->registry.aprox_match.vec));
			break;

		case BNX2X_MCAST_CMD_RESTORE:
			bnx2x_mcast_hdl_restore_e1h(bp, o, p, mc_filter);
			break;

		default:
			BNX2X_ERR("Unknown command: %d\n", cmd);
			return -EINVAL;
		}

		/* Set the mcast filter in the internal memory */
		for (i = 0; i < MC_HASH_SIZE; i++)
			REG_WR(bp, MC_HASH_OFFSET(bp, i), mc_filter[i]);
	} else
		/* clear the registry */
		memset(o->registry.aprox_match.vec, 0,
		       sizeof(o->registry.aprox_match.vec));

	/* We are done */
	r->clear_pending(r);

	return 0;
}

static int bnx2x_mcast_validate_e1(struct bnx2x *bp,
				   struct bnx2x_mcast_ramrod_params *p,
				   int cmd)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	int reg_sz = o->get_registry_size(o);

	switch (cmd) {
	/* DEL command deletes all currently configured MACs */
	case BNX2X_MCAST_CMD_DEL:
		o->set_registry_size(o, 0);
		/* Don't break */

	/* RESTORE command will restore the entire multicast configuration */
	case BNX2X_MCAST_CMD_RESTORE:
		p->mcast_list_len = reg_sz;
		  DP(BNX2X_MSG_SP, "Command %d, p->mcast_list_len=%d\n",
				   cmd, p->mcast_list_len);
		break;

	case BNX2X_MCAST_CMD_ADD:
	case BNX2X_MCAST_CMD_CONT:
		/* Multicast MACs on 57710 are configured as unicast MACs and
		 * there is only a limited number of CAM entries for that
		 * matter.
		 */
		if (p->mcast_list_len > o->max_cmd_len) {
			BNX2X_ERR("Can't configure more than %d multicast MACs on 57710\n",
				  o->max_cmd_len);
			return -EINVAL;
		}
		/* Every configured MAC should be cleared if DEL command is
		 * called. Only the last ADD command is relevant as long as
		 * every ADD commands overrides the previous configuration.
		 */
		DP(BNX2X_MSG_SP, "p->mcast_list_len=%d\n", p->mcast_list_len);
		if (p->mcast_list_len > 0)
			o->set_registry_size(o, p->mcast_list_len);

		break;

	default:
		BNX2X_ERR("Unknown command: %d\n", cmd);
		return -EINVAL;

	}

	/* We want to ensure that commands are executed one by one for 57710.
	 * Therefore each none-empty command will consume o->max_cmd_len.
	 */
	if (p->mcast_list_len)
		o->total_pending_num += o->max_cmd_len;

	return 0;
}

static void bnx2x_mcast_revert_e1(struct bnx2x *bp,
				      struct bnx2x_mcast_ramrod_params *p,
				      int old_num_macs)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;

	o->set_registry_size(o, old_num_macs);

	/* If current command hasn't been handled yet and we are
	 * here means that it's meant to be dropped and we have to
	 * update the number of outstandling MACs accordingly.
	 */
	if (p->mcast_list_len)
		o->total_pending_num -= o->max_cmd_len;
}

static void bnx2x_mcast_set_one_rule_e1(struct bnx2x *bp,
					struct bnx2x_mcast_obj *o, int idx,
					union bnx2x_mcast_config_data *cfg_data,
					int cmd)
{
	struct bnx2x_raw_obj *r = &o->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(r->rdata);

	/* copy mac */
	if ((cmd == BNX2X_MCAST_CMD_ADD) || (cmd == BNX2X_MCAST_CMD_RESTORE)) {
		bnx2x_set_fw_mac_addr(&data->config_table[idx].msb_mac_addr,
				      &data->config_table[idx].middle_mac_addr,
				      &data->config_table[idx].lsb_mac_addr,
				      cfg_data->mac);

		data->config_table[idx].vlan_id = 0;
		data->config_table[idx].pf_id = r->func_id;
		data->config_table[idx].clients_bit_vector =
			cpu_to_le32(1 << r->cl_id);

		SET_FLAG(data->config_table[idx].flags,
			 MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_SET);
	}
}

/**
 * bnx2x_mcast_set_rdata_hdr_e1  - set header values in mac_configuration_cmd
 *
 * @bp:		device handle
 * @p:
 * @len:	number of rules to handle
 */
static inline void bnx2x_mcast_set_rdata_hdr_e1(struct bnx2x *bp,
					struct bnx2x_mcast_ramrod_params *p,
					u8 len)
{
	struct bnx2x_raw_obj *r = &p->mcast_obj->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(r->rdata);

	u8 offset = (CHIP_REV_IS_SLOW(bp) ?
		     BNX2X_MAX_EMUL_MULTI*(1 + r->func_id) :
		     BNX2X_MAX_MULTICAST*(1 + r->func_id));

	data->hdr.offset = offset;
	data->hdr.client_id = 0xff;
	data->hdr.echo = ((r->cid & BNX2X_SWCID_MASK) |
			  (BNX2X_FILTER_MCAST_PENDING << BNX2X_SWCID_SHIFT));
	data->hdr.length = len;
}

/**
 * bnx2x_mcast_handle_restore_cmd_e1 - restore command for 57710
 *
 * @bp:		device handle
 * @o:
 * @start_idx:	index in the registry to start from
 * @rdata_idx:	index in the ramrod data to start from
 *
 * restore command for 57710 is like all other commands - always a stand alone
 * command - start_idx and rdata_idx will always be 0. This function will always
 * succeed.
 * returns -1 to comply with 57712 variant.
 */
static inline int bnx2x_mcast_handle_restore_cmd_e1(
	struct bnx2x *bp, struct bnx2x_mcast_obj *o , int start_idx,
	int *rdata_idx)
{
	struct bnx2x_mcast_mac_elem *elem;
	int i = 0;
	union bnx2x_mcast_config_data cfg_data = {0};

	/* go through the registry and configure the MACs from it. */
	list_for_each_entry(elem, &o->registry.exact_match.macs, link) {
		cfg_data.mac = &elem->mac[0];
		o->set_one_rule(bp, o, i, &cfg_data, BNX2X_MCAST_CMD_RESTORE);

		i++;

		  DP(BNX2X_MSG_SP, "About to configure %pM mcast MAC\n",
				   cfg_data.mac);
	}

	*rdata_idx = i;

	return -1;
}


static inline int bnx2x_mcast_handle_pending_cmds_e1(
	struct bnx2x *bp, struct bnx2x_mcast_ramrod_params *p)
{
	struct bnx2x_pending_mcast_cmd *cmd_pos;
	struct bnx2x_mcast_mac_elem *pmac_pos;
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	union bnx2x_mcast_config_data cfg_data = {0};
	int cnt = 0;


	/* If nothing to be done - return */
	if (list_empty(&o->pending_cmds_head))
		return 0;

	/* Handle the first command */
	cmd_pos = list_first_entry(&o->pending_cmds_head,
				   struct bnx2x_pending_mcast_cmd, link);

	switch (cmd_pos->type) {
	case BNX2X_MCAST_CMD_ADD:
		list_for_each_entry(pmac_pos, &cmd_pos->data.macs_head, link) {
			cfg_data.mac = &pmac_pos->mac[0];
			o->set_one_rule(bp, o, cnt, &cfg_data, cmd_pos->type);

			cnt++;

			DP(BNX2X_MSG_SP, "About to configure %pM mcast MAC\n",
					 pmac_pos->mac);
		}
		break;

	case BNX2X_MCAST_CMD_DEL:
		cnt = cmd_pos->data.macs_num;
		DP(BNX2X_MSG_SP, "About to delete %d multicast MACs\n", cnt);
		break;

	case BNX2X_MCAST_CMD_RESTORE:
		o->hdl_restore(bp, o, 0, &cnt);
		break;

	default:
		BNX2X_ERR("Unknown command: %d\n", cmd_pos->type);
		return -EINVAL;
	}

	list_del(&cmd_pos->link);
	kfree(cmd_pos);

	return cnt;
}

/**
 * bnx2x_get_fw_mac_addr - revert the bnx2x_set_fw_mac_addr().
 *
 * @fw_hi:
 * @fw_mid:
 * @fw_lo:
 * @mac:
 */
static inline void bnx2x_get_fw_mac_addr(__le16 *fw_hi, __le16 *fw_mid,
					 __le16 *fw_lo, u8 *mac)
{
	mac[1] = ((u8 *)fw_hi)[0];
	mac[0] = ((u8 *)fw_hi)[1];
	mac[3] = ((u8 *)fw_mid)[0];
	mac[2] = ((u8 *)fw_mid)[1];
	mac[5] = ((u8 *)fw_lo)[0];
	mac[4] = ((u8 *)fw_lo)[1];
}

/**
 * bnx2x_mcast_refresh_registry_e1 -
 *
 * @bp:		device handle
 * @cnt:
 *
 * Check the ramrod data first entry flag to see if it's a DELETE or ADD command
 * and update the registry correspondingly: if ADD - allocate a memory and add
 * the entries to the registry (list), if DELETE - clear the registry and free
 * the memory.
 */
static inline int bnx2x_mcast_refresh_registry_e1(struct bnx2x *bp,
						  struct bnx2x_mcast_obj *o)
{
	struct bnx2x_raw_obj *raw = &o->raw;
	struct bnx2x_mcast_mac_elem *elem;
	struct mac_configuration_cmd *data =
			(struct mac_configuration_cmd *)(raw->rdata);

	/* If first entry contains a SET bit - the command was ADD,
	 * otherwise - DEL_ALL
	 */
	if (GET_FLAG(data->config_table[0].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE)) {
		int i, len = data->hdr.length;

		/* Break if it was a RESTORE command */
		if (!list_empty(&o->registry.exact_match.macs))
			return 0;

		elem = kcalloc(len, sizeof(*elem), GFP_ATOMIC);
		if (!elem) {
			BNX2X_ERR("Failed to allocate registry memory\n");
			return -ENOMEM;
		}

		for (i = 0; i < len; i++, elem++) {
			bnx2x_get_fw_mac_addr(
				&data->config_table[i].msb_mac_addr,
				&data->config_table[i].middle_mac_addr,
				&data->config_table[i].lsb_mac_addr,
				elem->mac);
			DP(BNX2X_MSG_SP, "Adding registry entry for [%pM]\n",
			   elem->mac);
			list_add_tail(&elem->link,
				      &o->registry.exact_match.macs);
		}
	} else {
		elem = list_first_entry(&o->registry.exact_match.macs,
					struct bnx2x_mcast_mac_elem, link);
		DP(BNX2X_MSG_SP, "Deleting a registry\n");
		kfree(elem);
		INIT_LIST_HEAD(&o->registry.exact_match.macs);
	}

	return 0;
}

static int bnx2x_mcast_setup_e1(struct bnx2x *bp,
				struct bnx2x_mcast_ramrod_params *p,
				int cmd)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	struct bnx2x_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(raw->rdata);
	int cnt = 0, i, rc;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* First set all entries as invalid */
	for (i = 0; i < o->max_cmd_len ; i++)
		SET_FLAG(data->config_table[i].flags,
			 MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_INVALIDATE);

	/* Handle pending commands first */
	cnt = bnx2x_mcast_handle_pending_cmds_e1(bp, p);

	/* If there are no more pending commands - clear SCHEDULED state */
	if (list_empty(&o->pending_cmds_head))
		o->clear_sched(o);

	/* The below may be true iff there were no pending commands */
	if (!cnt)
		cnt = bnx2x_mcast_handle_current_cmd(bp, p, cmd, 0);

	/* For 57710 every command has o->max_cmd_len length to ensure that
	 * commands are done one at a time.
	 */
	o->total_pending_num -= o->max_cmd_len;

	/* send a ramrod */

	WARN_ON(cnt > o->max_cmd_len);

	/* Set ramrod header (in particular, a number of entries to update) */
	bnx2x_mcast_set_rdata_hdr_e1(bp, p, (u8)cnt);

	/* update a registry: we need the registry contents to be always up
	 * to date in order to be able to execute a RESTORE opcode. Here
	 * we use the fact that for 57710 we sent one command at a time
	 * hence we may take the registry update out of the command handling
	 * and do it in a simpler way here.
	 */
	rc = bnx2x_mcast_refresh_registry_e1(bp, o);
	if (rc)
		return rc;

	/*
	 * If CLEAR_ONLY was requested - don't send a ramrod and clear
	 * RAMROD_PENDING status immediately.
	 */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		raw->clear_pending(raw);
		return 0;
	} else {
		/*
		 *  No need for an explicit memory barrier here as long we would
		 *  need to ensure the ordering of writing to the SPQ element
		 *  and updating of the SPQ producer which involves a memory
		 *  read and we will have to put a full memory barrier there
		 *  (inside bnx2x_sp_post()).
		 */

		/* Send a ramrod */
		rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, raw->cid,
				   U64_HI(raw->rdata_mapping),
				   U64_LO(raw->rdata_mapping),
				   ETH_CONNECTION_TYPE);
		if (rc)
			return rc;

		/* Ramrod completion is pending */
		return 1;
	}

}

static int bnx2x_mcast_get_registry_size_exact(struct bnx2x_mcast_obj *o)
{
	return o->registry.exact_match.num_macs_set;
}

static int bnx2x_mcast_get_registry_size_aprox(struct bnx2x_mcast_obj *o)
{
	return o->registry.aprox_match.num_bins_set;
}

static void bnx2x_mcast_set_registry_size_exact(struct bnx2x_mcast_obj *o,
						int n)
{
	o->registry.exact_match.num_macs_set = n;
}

static void bnx2x_mcast_set_registry_size_aprox(struct bnx2x_mcast_obj *o,
						int n)
{
	o->registry.aprox_match.num_bins_set = n;
}

int bnx2x_config_mcast(struct bnx2x *bp,
		       struct bnx2x_mcast_ramrod_params *p,
		       int cmd)
{
	struct bnx2x_mcast_obj *o = p->mcast_obj;
	struct bnx2x_raw_obj *r = &o->raw;
	int rc = 0, old_reg_size;

	/* This is needed to recover number of currently configured mcast macs
	 * in case of failure.
	 */
	old_reg_size = o->get_registry_size(o);

	/* Do some calculations and checks */
	rc = o->validate(bp, p, cmd);
	if (rc)
		return rc;

	/* Return if there is no work to do */
	if ((!p->mcast_list_len) && (!o->check_sched(o)))
		return 0;

	DP(BNX2X_MSG_SP, "o->total_pending_num=%d p->mcast_list_len=%d o->max_cmd_len=%d\n",
	   o->total_pending_num, p->mcast_list_len, o->max_cmd_len);

	/* Enqueue the current command to the pending list if we can't complete
	 * it in the current iteration
	 */
	if (r->check_pending(r) ||
	    ((o->max_cmd_len > 0) && (o->total_pending_num > o->max_cmd_len))) {
		rc = o->enqueue_cmd(bp, p->mcast_obj, p, cmd);
		if (rc < 0)
			goto error_exit1;

		/* As long as the current command is in a command list we
		 * don't need to handle it separately.
		 */
		p->mcast_list_len = 0;
	}

	if (!r->check_pending(r)) {

		/* Set 'pending' state */
		r->set_pending(r);

		/* Configure the new classification in the chip */
		rc = o->config_mcast(bp, p, cmd);
		if (rc < 0)
			goto error_exit2;

		/* Wait for a ramrod completion if was requested */
		if (test_bit(RAMROD_COMP_WAIT, &p->ramrod_flags))
			rc = o->wait_comp(bp, o);
	}

	return rc;

error_exit2:
	r->clear_pending(r);

error_exit1:
	o->revert(bp, p, old_reg_size);

	return rc;
}

static void bnx2x_mcast_clear_sched(struct bnx2x_mcast_obj *o)
{
	smp_mb__before_clear_bit();
	clear_bit(o->sched_state, o->raw.pstate);
	smp_mb__after_clear_bit();
}

static void bnx2x_mcast_set_sched(struct bnx2x_mcast_obj *o)
{
	smp_mb__before_clear_bit();
	set_bit(o->sched_state, o->raw.pstate);
	smp_mb__after_clear_bit();
}

static bool bnx2x_mcast_check_sched(struct bnx2x_mcast_obj *o)
{
	return !!test_bit(o->sched_state, o->raw.pstate);
}

static bool bnx2x_mcast_check_pending(struct bnx2x_mcast_obj *o)
{
	return o->raw.check_pending(&o->raw) || o->check_sched(o);
}

void bnx2x_init_mcast_obj(struct bnx2x *bp,
			  struct bnx2x_mcast_obj *mcast_obj,
			  u8 mcast_cl_id, u32 mcast_cid, u8 func_id,
			  u8 engine_id, void *rdata, dma_addr_t rdata_mapping,
			  int state, unsigned long *pstate, bnx2x_obj_type type)
{
	memset(mcast_obj, 0, sizeof(*mcast_obj));

	bnx2x_init_raw_obj(&mcast_obj->raw, mcast_cl_id, mcast_cid, func_id,
			   rdata, rdata_mapping, state, pstate, type);

	mcast_obj->engine_id = engine_id;

	INIT_LIST_HEAD(&mcast_obj->pending_cmds_head);

	mcast_obj->sched_state = BNX2X_FILTER_MCAST_SCHED;
	mcast_obj->check_sched = bnx2x_mcast_check_sched;
	mcast_obj->set_sched = bnx2x_mcast_set_sched;
	mcast_obj->clear_sched = bnx2x_mcast_clear_sched;

	if (CHIP_IS_E1(bp)) {
		mcast_obj->config_mcast      = bnx2x_mcast_setup_e1;
		mcast_obj->enqueue_cmd       = bnx2x_mcast_enqueue_cmd;
		mcast_obj->hdl_restore       =
			bnx2x_mcast_handle_restore_cmd_e1;
		mcast_obj->check_pending     = bnx2x_mcast_check_pending;

		if (CHIP_REV_IS_SLOW(bp))
			mcast_obj->max_cmd_len = BNX2X_MAX_EMUL_MULTI;
		else
			mcast_obj->max_cmd_len = BNX2X_MAX_MULTICAST;

		mcast_obj->wait_comp         = bnx2x_mcast_wait;
		mcast_obj->set_one_rule      = bnx2x_mcast_set_one_rule_e1;
		mcast_obj->validate          = bnx2x_mcast_validate_e1;
		mcast_obj->revert            = bnx2x_mcast_revert_e1;
		mcast_obj->get_registry_size =
			bnx2x_mcast_get_registry_size_exact;
		mcast_obj->set_registry_size =
			bnx2x_mcast_set_registry_size_exact;

		/* 57710 is the only chip that uses the exact match for mcast
		 * at the moment.
		 */
		INIT_LIST_HEAD(&mcast_obj->registry.exact_match.macs);

	} else if (CHIP_IS_E1H(bp)) {
		mcast_obj->config_mcast  = bnx2x_mcast_setup_e1h;
		mcast_obj->enqueue_cmd   = NULL;
		mcast_obj->hdl_restore   = NULL;
		mcast_obj->check_pending = bnx2x_mcast_check_pending;

		/* 57711 doesn't send a ramrod, so it has unlimited credit
		 * for one command.
		 */
		mcast_obj->max_cmd_len       = -1;
		mcast_obj->wait_comp         = bnx2x_mcast_wait;
		mcast_obj->set_one_rule      = NULL;
		mcast_obj->validate          = bnx2x_mcast_validate_e1h;
		mcast_obj->revert            = bnx2x_mcast_revert_e1h;
		mcast_obj->get_registry_size =
			bnx2x_mcast_get_registry_size_aprox;
		mcast_obj->set_registry_size =
			bnx2x_mcast_set_registry_size_aprox;
	} else {
		mcast_obj->config_mcast      = bnx2x_mcast_setup_e2;
		mcast_obj->enqueue_cmd       = bnx2x_mcast_enqueue_cmd;
		mcast_obj->hdl_restore       =
			bnx2x_mcast_handle_restore_cmd_e2;
		mcast_obj->check_pending     = bnx2x_mcast_check_pending;
		/* TODO: There should be a proper HSI define for this number!!!
		 */
		mcast_obj->max_cmd_len       = 16;
		mcast_obj->wait_comp         = bnx2x_mcast_wait;
		mcast_obj->set_one_rule      = bnx2x_mcast_set_one_rule_e2;
		mcast_obj->validate          = bnx2x_mcast_validate_e2;
		mcast_obj->revert            = bnx2x_mcast_revert_e2;
		mcast_obj->get_registry_size =
			bnx2x_mcast_get_registry_size_aprox;
		mcast_obj->set_registry_size =
			bnx2x_mcast_set_registry_size_aprox;
	}
}

/*************************** Credit handling **********************************/

/**
 * atomic_add_ifless - add if the result is less than a given value.
 *
 * @v:	pointer of type atomic_t
 * @a:	the amount to add to v...
 * @u:	...if (v + a) is less than u.
 *
 * returns true if (v + a) was less than u, and false otherwise.
 *
 */
static inline bool __atomic_add_ifless(atomic_t *v, int a, int u)
{
	int c, old;

	c = atomic_read(v);
	for (;;) {
		if (unlikely(c + a >= u))
			return false;

		old = atomic_cmpxchg((v), c, c + a);
		if (likely(old == c))
			break;
		c = old;
	}

	return true;
}

/**
 * atomic_dec_ifmoe - dec if the result is more or equal than a given value.
 *
 * @v:	pointer of type atomic_t
 * @a:	the amount to dec from v...
 * @u:	...if (v - a) is more or equal than u.
 *
 * returns true if (v - a) was more or equal than u, and false
 * otherwise.
 */
static inline bool __atomic_dec_ifmoe(atomic_t *v, int a, int u)
{
	int c, old;

	c = atomic_read(v);
	for (;;) {
		if (unlikely(c - a < u))
			return false;

		old = atomic_cmpxchg((v), c, c - a);
		if (likely(old == c))
			break;
		c = old;
	}

	return true;
}

static bool bnx2x_credit_pool_get(struct bnx2x_credit_pool_obj *o, int cnt)
{
	bool rc;

	smp_mb();
	rc = __atomic_dec_ifmoe(&o->credit, cnt, 0);
	smp_mb();

	return rc;
}

static bool bnx2x_credit_pool_put(struct bnx2x_credit_pool_obj *o, int cnt)
{
	bool rc;

	smp_mb();

	/* Don't let to refill if credit + cnt > pool_sz */
	rc = __atomic_add_ifless(&o->credit, cnt, o->pool_sz + 1);

	smp_mb();

	return rc;
}

static int bnx2x_credit_pool_check(struct bnx2x_credit_pool_obj *o)
{
	int cur_credit;

	smp_mb();
	cur_credit = atomic_read(&o->credit);

	return cur_credit;
}

static bool bnx2x_credit_pool_always_true(struct bnx2x_credit_pool_obj *o,
					  int cnt)
{
	return true;
}


static bool bnx2x_credit_pool_get_entry(
	struct bnx2x_credit_pool_obj *o,
	int *offset)
{
	int idx, vec, i;

	*offset = -1;

	/* Find "internal cam-offset" then add to base for this object... */
	for (vec = 0; vec < BNX2X_POOL_VEC_SIZE; vec++) {

		/* Skip the current vector if there are no free entries in it */
		if (!o->pool_mirror[vec])
			continue;

		/* If we've got here we are going to find a free entry */
		for (idx = vec * BIT_VEC64_ELEM_SZ, i = 0;
		      i < BIT_VEC64_ELEM_SZ; idx++, i++)

			if (BIT_VEC64_TEST_BIT(o->pool_mirror, idx)) {
				/* Got one!! */
				BIT_VEC64_CLEAR_BIT(o->pool_mirror, idx);
				*offset = o->base_pool_offset + idx;
				return true;
			}
	}

	return false;
}

static bool bnx2x_credit_pool_put_entry(
	struct bnx2x_credit_pool_obj *o,
	int offset)
{
	if (offset < o->base_pool_offset)
		return false;

	offset -= o->base_pool_offset;

	if (offset >= o->pool_sz)
		return false;

	/* Return the entry to the pool */
	BIT_VEC64_SET_BIT(o->pool_mirror, offset);

	return true;
}

static bool bnx2x_credit_pool_put_entry_always_true(
	struct bnx2x_credit_pool_obj *o,
	int offset)
{
	return true;
}

static bool bnx2x_credit_pool_get_entry_always_true(
	struct bnx2x_credit_pool_obj *o,
	int *offset)
{
	*offset = -1;
	return true;
}
/**
 * bnx2x_init_credit_pool - initialize credit pool internals.
 *
 * @p:
 * @base:	Base entry in the CAM to use.
 * @credit:	pool size.
 *
 * If base is negative no CAM entries handling will be performed.
 * If credit is negative pool operations will always succeed (unlimited pool).
 *
 */
static inline void bnx2x_init_credit_pool(struct bnx2x_credit_pool_obj *p,
					  int base, int credit)
{
	/* Zero the object first */
	memset(p, 0, sizeof(*p));

	/* Set the table to all 1s */
	memset(&p->pool_mirror, 0xff, sizeof(p->pool_mirror));

	/* Init a pool as full */
	atomic_set(&p->credit, credit);

	/* The total poll size */
	p->pool_sz = credit;

	p->base_pool_offset = base;

	/* Commit the change */
	smp_mb();

	p->check = bnx2x_credit_pool_check;

	/* if pool credit is negative - disable the checks */
	if (credit >= 0) {
		p->put      = bnx2x_credit_pool_put;
		p->get      = bnx2x_credit_pool_get;
		p->put_entry = bnx2x_credit_pool_put_entry;
		p->get_entry = bnx2x_credit_pool_get_entry;
	} else {
		p->put      = bnx2x_credit_pool_always_true;
		p->get      = bnx2x_credit_pool_always_true;
		p->put_entry = bnx2x_credit_pool_put_entry_always_true;
		p->get_entry = bnx2x_credit_pool_get_entry_always_true;
	}

	/* If base is negative - disable entries handling */
	if (base < 0) {
		p->put_entry = bnx2x_credit_pool_put_entry_always_true;
		p->get_entry = bnx2x_credit_pool_get_entry_always_true;
	}
}

void bnx2x_init_mac_credit_pool(struct bnx2x *bp,
				struct bnx2x_credit_pool_obj *p, u8 func_id,
				u8 func_num)
{
/* TODO: this will be defined in consts as well... */
#define BNX2X_CAM_SIZE_EMUL 5

	int cam_sz;

	if (CHIP_IS_E1(bp)) {
		/* In E1, Multicast is saved in cam... */
		if (!CHIP_REV_IS_SLOW(bp))
			cam_sz = (MAX_MAC_CREDIT_E1 / 2) - BNX2X_MAX_MULTICAST;
		else
			cam_sz = BNX2X_CAM_SIZE_EMUL - BNX2X_MAX_EMUL_MULTI;

		bnx2x_init_credit_pool(p, func_id * cam_sz, cam_sz);

	} else if (CHIP_IS_E1H(bp)) {
		/* CAM credit is equaly divided between all active functions
		 * on the PORT!.
		 */
		if ((func_num > 0)) {
			if (!CHIP_REV_IS_SLOW(bp))
				cam_sz = (MAX_MAC_CREDIT_E1H / (2*func_num));
			else
				cam_sz = BNX2X_CAM_SIZE_EMUL;
			bnx2x_init_credit_pool(p, func_id * cam_sz, cam_sz);
		} else {
			/* this should never happen! Block MAC operations. */
			bnx2x_init_credit_pool(p, 0, 0);
		}

	} else {

		/*
		 * CAM credit is equaly divided between all active functions
		 * on the PATH.
		 */
		if ((func_num > 0)) {
			if (!CHIP_REV_IS_SLOW(bp))
				cam_sz = (MAX_MAC_CREDIT_E2 / func_num);
			else
				cam_sz = BNX2X_CAM_SIZE_EMUL;

			/*
			 * No need for CAM entries handling for 57712 and
			 * newer.
			 */
			bnx2x_init_credit_pool(p, -1, cam_sz);
		} else {
			/* this should never happen! Block MAC operations. */
			bnx2x_init_credit_pool(p, 0, 0);
		}

	}
}

void bnx2x_init_vlan_credit_pool(struct bnx2x *bp,
				 struct bnx2x_credit_pool_obj *p,
				 u8 func_id,
				 u8 func_num)
{
	if (CHIP_IS_E1x(bp)) {
		/*
		 * There is no VLAN credit in HW on 57710 and 57711 only
		 * MAC / MAC-VLAN can be set
		 */
		bnx2x_init_credit_pool(p, 0, -1);
	} else {
		/*
		 * CAM credit is equaly divided between all active functions
		 * on the PATH.
		 */
		if (func_num > 0) {
			int credit = MAX_VLAN_CREDIT_E2 / func_num;
			bnx2x_init_credit_pool(p, func_id * credit, credit);
		} else
			/* this should never happen! Block VLAN operations. */
			bnx2x_init_credit_pool(p, 0, 0);
	}
}

/****************** RSS Configuration ******************/
/**
 * bnx2x_debug_print_ind_table - prints the indirection table configuration.
 *
 * @bp:		driver hanlde
 * @p:		pointer to rss configuration
 *
 * Prints it when NETIF_MSG_IFUP debug level is configured.
 */
static inline void bnx2x_debug_print_ind_table(struct bnx2x *bp,
					struct bnx2x_config_rss_params *p)
{
	int i;

	DP(BNX2X_MSG_SP, "Setting indirection table to:\n");
	DP(BNX2X_MSG_SP, "0x0000: ");
	for (i = 0; i < T_ETH_INDIRECTION_TABLE_SIZE; i++) {
		DP_CONT(BNX2X_MSG_SP, "0x%02x ", p->ind_table[i]);

		/* Print 4 bytes in a line */
		if ((i + 1 < T_ETH_INDIRECTION_TABLE_SIZE) &&
		    (((i + 1) & 0x3) == 0)) {
			DP_CONT(BNX2X_MSG_SP, "\n");
			DP(BNX2X_MSG_SP, "0x%04x: ", i + 1);
		}
	}

	DP_CONT(BNX2X_MSG_SP, "\n");
}

/**
 * bnx2x_setup_rss - configure RSS
 *
 * @bp:		device handle
 * @p:		rss configuration
 *
 * sends on UPDATE ramrod for that matter.
 */
static int bnx2x_setup_rss(struct bnx2x *bp,
			   struct bnx2x_config_rss_params *p)
{
	struct bnx2x_rss_config_obj *o = p->rss_obj;
	struct bnx2x_raw_obj *r = &o->raw;
	struct eth_rss_update_ramrod_data *data =
		(struct eth_rss_update_ramrod_data *)(r->rdata);
	u8 rss_mode = 0;
	int rc;

	memset(data, 0, sizeof(*data));

	DP(BNX2X_MSG_SP, "Configuring RSS\n");

	/* Set an echo field */
	data->echo = (r->cid & BNX2X_SWCID_MASK) |
		     (r->state << BNX2X_SWCID_SHIFT);

	/* RSS mode */
	if (test_bit(BNX2X_RSS_MODE_DISABLED, &p->rss_flags))
		rss_mode = ETH_RSS_MODE_DISABLED;
	else if (test_bit(BNX2X_RSS_MODE_REGULAR, &p->rss_flags))
		rss_mode = ETH_RSS_MODE_REGULAR;

	data->rss_mode = rss_mode;

	DP(BNX2X_MSG_SP, "rss_mode=%d\n", rss_mode);

	/* RSS capabilities */
	if (test_bit(BNX2X_RSS_IPV4, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY;

	if (test_bit(BNX2X_RSS_IPV4_TCP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY;

	if (test_bit(BNX2X_RSS_IPV4_UDP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV4_UDP_CAPABILITY;

	if (test_bit(BNX2X_RSS_IPV6, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY;

	if (test_bit(BNX2X_RSS_IPV6_TCP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY;

	if (test_bit(BNX2X_RSS_IPV6_UDP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV6_UDP_CAPABILITY;

	/* Hashing mask */
	data->rss_result_mask = p->rss_result_mask;

	/* RSS engine ID */
	data->rss_engine_id = o->engine_id;

	DP(BNX2X_MSG_SP, "rss_engine_id=%d\n", data->rss_engine_id);

	/* Indirection table */
	memcpy(data->indirection_table, p->ind_table,
		  T_ETH_INDIRECTION_TABLE_SIZE);

	/* Remember the last configuration */
	memcpy(o->ind_table, p->ind_table, T_ETH_INDIRECTION_TABLE_SIZE);

	/* Print the indirection table */
	if (netif_msg_ifup(bp))
		bnx2x_debug_print_ind_table(bp, p);

	/* RSS keys */
	if (test_bit(BNX2X_RSS_SET_SRCH, &p->rss_flags)) {
		memcpy(&data->rss_key[0], &p->rss_key[0],
		       sizeof(data->rss_key));
		data->capabilities |= ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY;
	}

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	/* Send a ramrod */
	rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_RSS_UPDATE, r->cid,
			   U64_HI(r->rdata_mapping),
			   U64_LO(r->rdata_mapping),
			   ETH_CONNECTION_TYPE);

	if (rc < 0)
		return rc;

	return 1;
}

void bnx2x_get_rss_ind_table(struct bnx2x_rss_config_obj *rss_obj,
			     u8 *ind_table)
{
	memcpy(ind_table, rss_obj->ind_table, sizeof(rss_obj->ind_table));
}

int bnx2x_config_rss(struct bnx2x *bp,
		     struct bnx2x_config_rss_params *p)
{
	int rc;
	struct bnx2x_rss_config_obj *o = p->rss_obj;
	struct bnx2x_raw_obj *r = &o->raw;

	/* Do nothing if only driver cleanup was requested */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags))
		return 0;

	r->set_pending(r);

	rc = o->config_rss(bp, p);
	if (rc < 0) {
		r->clear_pending(r);
		return rc;
	}

	if (test_bit(RAMROD_COMP_WAIT, &p->ramrod_flags))
		rc = r->wait_comp(bp, r);

	return rc;
}


void bnx2x_init_rss_config_obj(struct bnx2x *bp,
			       struct bnx2x_rss_config_obj *rss_obj,
			       u8 cl_id, u32 cid, u8 func_id, u8 engine_id,
			       void *rdata, dma_addr_t rdata_mapping,
			       int state, unsigned long *pstate,
			       bnx2x_obj_type type)
{
	bnx2x_init_raw_obj(&rss_obj->raw, cl_id, cid, func_id, rdata,
			   rdata_mapping, state, pstate, type);

	rss_obj->engine_id  = engine_id;
	rss_obj->config_rss = bnx2x_setup_rss;
}

/********************** Queue state object ***********************************/

/**
 * bnx2x_queue_state_change - perform Queue state change transition
 *
 * @bp:		device handle
 * @params:	parameters to perform the transition
 *
 * returns 0 in case of successfully completed transition, negative error
 * code in case of failure, positive (EBUSY) value if there is a completion
 * to that is still pending (possible only if RAMROD_COMP_WAIT is
 * not set in params->ramrod_flags for asynchronous commands).
 *
 */
int bnx2x_queue_state_change(struct bnx2x *bp,
			     struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	int rc, pending_bit;
	unsigned long *pending = &o->pending;

	/* Check that the requested transition is legal */
	if (o->check_transition(bp, o, params))
		return -EINVAL;

	/* Set "pending" bit */
	pending_bit = o->set_pending(o, params);

	/* Don't send a command if only driver cleanup was requested */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &params->ramrod_flags))
		o->complete_cmd(bp, o, pending_bit);
	else {
		/* Send a ramrod */
		rc = o->send_cmd(bp, params);
		if (rc) {
			o->next_state = BNX2X_Q_STATE_MAX;
			clear_bit(pending_bit, pending);
			smp_mb__after_clear_bit();
			return rc;
		}

		if (test_bit(RAMROD_COMP_WAIT, &params->ramrod_flags)) {
			rc = o->wait_comp(bp, o, pending_bit);
			if (rc)
				return rc;

			return 0;
		}
	}

	return !!test_bit(pending_bit, pending);
}


static int bnx2x_queue_set_pending(struct bnx2x_queue_sp_obj *obj,
				   struct bnx2x_queue_state_params *params)
{
	enum bnx2x_queue_cmd cmd = params->cmd, bit;

	/* ACTIVATE and DEACTIVATE commands are implemented on top of
	 * UPDATE command.
	 */
	if ((cmd == BNX2X_Q_CMD_ACTIVATE) ||
	    (cmd == BNX2X_Q_CMD_DEACTIVATE))
		bit = BNX2X_Q_CMD_UPDATE;
	else
		bit = cmd;

	set_bit(bit, &obj->pending);
	return bit;
}

static int bnx2x_queue_wait_comp(struct bnx2x *bp,
				 struct bnx2x_queue_sp_obj *o,
				 enum bnx2x_queue_cmd cmd)
{
	return bnx2x_state_wait(bp, cmd, &o->pending);
}

/**
 * bnx2x_queue_comp_cmd - complete the state change command.
 *
 * @bp:		device handle
 * @o:
 * @cmd:
 *
 * Checks that the arrived completion is expected.
 */
static int bnx2x_queue_comp_cmd(struct bnx2x *bp,
				struct bnx2x_queue_sp_obj *o,
				enum bnx2x_queue_cmd cmd)
{
	unsigned long cur_pending = o->pending;

	if (!test_and_clear_bit(cmd, &cur_pending)) {
		BNX2X_ERR("Bad MC reply %d for queue %d in state %d pending 0x%lx, next_state %d\n",
			  cmd, o->cids[BNX2X_PRIMARY_CID_INDEX],
			  o->state, cur_pending, o->next_state);
		return -EINVAL;
	}

	if (o->next_tx_only >= o->max_cos)
		/* >= becuase tx only must always be smaller than cos since the
		 * primary connection suports COS 0
		 */
		BNX2X_ERR("illegal value for next tx_only: %d. max cos was %d",
			   o->next_tx_only, o->max_cos);

	DP(BNX2X_MSG_SP,
	   "Completing command %d for queue %d, setting state to %d\n",
	   cmd, o->cids[BNX2X_PRIMARY_CID_INDEX], o->next_state);

	if (o->next_tx_only)  /* print num tx-only if any exist */
		DP(BNX2X_MSG_SP, "primary cid %d: num tx-only cons %d\n",
		   o->cids[BNX2X_PRIMARY_CID_INDEX], o->next_tx_only);

	o->state = o->next_state;
	o->num_tx_only = o->next_tx_only;
	o->next_state = BNX2X_Q_STATE_MAX;

	/* It's important that o->state and o->next_state are
	 * updated before o->pending.
	 */
	wmb();

	clear_bit(cmd, &o->pending);
	smp_mb__after_clear_bit();

	return 0;
}

static void bnx2x_q_fill_setup_data_e2(struct bnx2x *bp,
				struct bnx2x_queue_state_params *cmd_params,
				struct client_init_ramrod_data *data)
{
	struct bnx2x_queue_setup_params *params = &cmd_params->params.setup;

	/* Rx data */

	/* IPv6 TPA supported for E2 and above only */
	data->rx.tpa_en |= test_bit(BNX2X_Q_FLG_TPA_IPV6, &params->flags) *
				CLIENT_INIT_RX_DATA_TPA_EN_IPV6;
}

static void bnx2x_q_fill_init_general_data(struct bnx2x *bp,
				struct bnx2x_queue_sp_obj *o,
				struct bnx2x_general_setup_params *params,
				struct client_init_general_data *gen_data,
				unsigned long *flags)
{
	gen_data->client_id = o->cl_id;

	if (test_bit(BNX2X_Q_FLG_STATS, flags)) {
		gen_data->statistics_counter_id =
					params->stat_id;
		gen_data->statistics_en_flg = 1;
		gen_data->statistics_zero_flg =
			test_bit(BNX2X_Q_FLG_ZERO_STATS, flags);
	} else
		gen_data->statistics_counter_id =
					DISABLE_STATISTIC_COUNTER_ID_VALUE;

	gen_data->is_fcoe_flg = test_bit(BNX2X_Q_FLG_FCOE, flags);
	gen_data->activate_flg = test_bit(BNX2X_Q_FLG_ACTIVE, flags);
	gen_data->sp_client_id = params->spcl_id;
	gen_data->mtu = cpu_to_le16(params->mtu);
	gen_data->func_id = o->func_id;


	gen_data->cos = params->cos;

	gen_data->traffic_type =
		test_bit(BNX2X_Q_FLG_FCOE, flags) ?
		LLFC_TRAFFIC_TYPE_FCOE : LLFC_TRAFFIC_TYPE_NW;

	DP(BNX2X_MSG_SP, "flags: active %d, cos %d, stats en %d\n",
	   gen_data->activate_flg, gen_data->cos, gen_data->statistics_en_flg);
}

static void bnx2x_q_fill_init_tx_data(struct bnx2x_queue_sp_obj *o,
				struct bnx2x_txq_setup_params *params,
				struct client_init_tx_data *tx_data,
				unsigned long *flags)
{
	tx_data->enforce_security_flg =
		test_bit(BNX2X_Q_FLG_TX_SEC, flags);
	tx_data->default_vlan =
		cpu_to_le16(params->default_vlan);
	tx_data->default_vlan_flg =
		test_bit(BNX2X_Q_FLG_DEF_VLAN, flags);
	tx_data->tx_switching_flg =
		test_bit(BNX2X_Q_FLG_TX_SWITCH, flags);
	tx_data->anti_spoofing_flg =
		test_bit(BNX2X_Q_FLG_ANTI_SPOOF, flags);
	tx_data->force_default_pri_flg =
		test_bit(BNX2X_Q_FLG_FORCE_DEFAULT_PRI, flags);

	tx_data->tx_status_block_id = params->fw_sb_id;
	tx_data->tx_sb_index_number = params->sb_cq_index;
	tx_data->tss_leading_client_id = params->tss_leading_cl_id;

	tx_data->tx_bd_page_base.lo =
		cpu_to_le32(U64_LO(params->dscr_map));
	tx_data->tx_bd_page_base.hi =
		cpu_to_le32(U64_HI(params->dscr_map));

	/* Don't configure any Tx switching mode during queue SETUP */
	tx_data->state = 0;
}

static void bnx2x_q_fill_init_pause_data(struct bnx2x_queue_sp_obj *o,
				struct rxq_pause_params *params,
				struct client_init_rx_data *rx_data)
{
	/* flow control data */
	rx_data->cqe_pause_thr_low = cpu_to_le16(params->rcq_th_lo);
	rx_data->cqe_pause_thr_high = cpu_to_le16(params->rcq_th_hi);
	rx_data->bd_pause_thr_low = cpu_to_le16(params->bd_th_lo);
	rx_data->bd_pause_thr_high = cpu_to_le16(params->bd_th_hi);
	rx_data->sge_pause_thr_low = cpu_to_le16(params->sge_th_lo);
	rx_data->sge_pause_thr_high = cpu_to_le16(params->sge_th_hi);
	rx_data->rx_cos_mask = cpu_to_le16(params->pri_map);
}

static void bnx2x_q_fill_init_rx_data(struct bnx2x_queue_sp_obj *o,
				struct bnx2x_rxq_setup_params *params,
				struct client_init_rx_data *rx_data,
				unsigned long *flags)
{
	rx_data->tpa_en = test_bit(BNX2X_Q_FLG_TPA, flags) *
				CLIENT_INIT_RX_DATA_TPA_EN_IPV4;
	rx_data->tpa_en |= test_bit(BNX2X_Q_FLG_TPA_GRO, flags) *
				CLIENT_INIT_RX_DATA_TPA_MODE;
	rx_data->vmqueue_mode_en_flg = 0;

	rx_data->cache_line_alignment_log_size =
		params->cache_line_log;
	rx_data->enable_dynamic_hc =
		test_bit(BNX2X_Q_FLG_DHC, flags);
	rx_data->max_sges_for_packet = params->max_sges_pkt;
	rx_data->client_qzone_id = params->cl_qzone_id;
	rx_data->max_agg_size = cpu_to_le16(params->tpa_agg_sz);

	/* Always start in DROP_ALL mode */
	rx_data->state = cpu_to_le16(CLIENT_INIT_RX_DATA_UCAST_DROP_ALL |
				     CLIENT_INIT_RX_DATA_MCAST_DROP_ALL);

	/* We don't set drop flags */
	rx_data->drop_ip_cs_err_flg = 0;
	rx_data->drop_tcp_cs_err_flg = 0;
	rx_data->drop_ttl0_flg = 0;
	rx_data->drop_udp_cs_err_flg = 0;
	rx_data->inner_vlan_removal_enable_flg =
		test_bit(BNX2X_Q_FLG_VLAN, flags);
	rx_data->outer_vlan_removal_enable_flg =
		test_bit(BNX2X_Q_FLG_OV, flags);
	rx_data->status_block_id = params->fw_sb_id;
	rx_data->rx_sb_index_number = params->sb_cq_index;
	rx_data->max_tpa_queues = params->max_tpa_queues;
	rx_data->max_bytes_on_bd = cpu_to_le16(params->buf_sz);
	rx_data->sge_buff_size = cpu_to_le16(params->sge_buf_sz);
	rx_data->bd_page_base.lo =
		cpu_to_le32(U64_LO(params->dscr_map));
	rx_data->bd_page_base.hi =
		cpu_to_le32(U64_HI(params->dscr_map));
	rx_data->sge_page_base.lo =
		cpu_to_le32(U64_LO(params->sge_map));
	rx_data->sge_page_base.hi =
		cpu_to_le32(U64_HI(params->sge_map));
	rx_data->cqe_page_base.lo =
		cpu_to_le32(U64_LO(params->rcq_map));
	rx_data->cqe_page_base.hi =
		cpu_to_le32(U64_HI(params->rcq_map));
	rx_data->is_leading_rss = test_bit(BNX2X_Q_FLG_LEADING_RSS, flags);

	if (test_bit(BNX2X_Q_FLG_MCAST, flags)) {
		rx_data->approx_mcast_engine_id = params->mcast_engine_id;
		rx_data->is_approx_mcast = 1;
	}

	rx_data->rss_engine_id = params->rss_engine_id;

	/* silent vlan removal */
	rx_data->silent_vlan_removal_flg =
		test_bit(BNX2X_Q_FLG_SILENT_VLAN_REM, flags);
	rx_data->silent_vlan_value =
		cpu_to_le16(params->silent_removal_value);
	rx_data->silent_vlan_mask =
		cpu_to_le16(params->silent_removal_mask);

}

/* initialize the general, tx and rx parts of a queue object */
static void bnx2x_q_fill_setup_data_cmn(struct bnx2x *bp,
				struct bnx2x_queue_state_params *cmd_params,
				struct client_init_ramrod_data *data)
{
	bnx2x_q_fill_init_general_data(bp, cmd_params->q_obj,
				       &cmd_params->params.setup.gen_params,
				       &data->general,
				       &cmd_params->params.setup.flags);

	bnx2x_q_fill_init_tx_data(cmd_params->q_obj,
				  &cmd_params->params.setup.txq_params,
				  &data->tx,
				  &cmd_params->params.setup.flags);

	bnx2x_q_fill_init_rx_data(cmd_params->q_obj,
				  &cmd_params->params.setup.rxq_params,
				  &data->rx,
				  &cmd_params->params.setup.flags);

	bnx2x_q_fill_init_pause_data(cmd_params->q_obj,
				     &cmd_params->params.setup.pause_params,
				     &data->rx);
}

/* initialize the general and tx parts of a tx-only queue object */
static void bnx2x_q_fill_setup_tx_only(struct bnx2x *bp,
				struct bnx2x_queue_state_params *cmd_params,
				struct tx_queue_init_ramrod_data *data)
{
	bnx2x_q_fill_init_general_data(bp, cmd_params->q_obj,
				       &cmd_params->params.tx_only.gen_params,
				       &data->general,
				       &cmd_params->params.tx_only.flags);

	bnx2x_q_fill_init_tx_data(cmd_params->q_obj,
				  &cmd_params->params.tx_only.txq_params,
				  &data->tx,
				  &cmd_params->params.tx_only.flags);

	DP(BNX2X_MSG_SP, "cid %d, tx bd page lo %x hi %x",
			 cmd_params->q_obj->cids[0],
			 data->tx.tx_bd_page_base.lo,
			 data->tx.tx_bd_page_base.hi);
}

/**
 * bnx2x_q_init - init HW/FW queue
 *
 * @bp:		device handle
 * @params:
 *
 * HW/FW initial Queue configuration:
 *      - HC: Rx and Tx
 *      - CDU context validation
 *
 */
static inline int bnx2x_q_init(struct bnx2x *bp,
			       struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	struct bnx2x_queue_init_params *init = &params->params.init;
	u16 hc_usec;
	u8 cos;

	/* Tx HC configuration */
	if (test_bit(BNX2X_Q_TYPE_HAS_TX, &o->type) &&
	    test_bit(BNX2X_Q_FLG_HC, &init->tx.flags)) {
		hc_usec = init->tx.hc_rate ? 1000000 / init->tx.hc_rate : 0;

		bnx2x_update_coalesce_sb_index(bp, init->tx.fw_sb_id,
			init->tx.sb_cq_index,
			!test_bit(BNX2X_Q_FLG_HC_EN, &init->tx.flags),
			hc_usec);
	}

	/* Rx HC configuration */
	if (test_bit(BNX2X_Q_TYPE_HAS_RX, &o->type) &&
	    test_bit(BNX2X_Q_FLG_HC, &init->rx.flags)) {
		hc_usec = init->rx.hc_rate ? 1000000 / init->rx.hc_rate : 0;

		bnx2x_update_coalesce_sb_index(bp, init->rx.fw_sb_id,
			init->rx.sb_cq_index,
			!test_bit(BNX2X_Q_FLG_HC_EN, &init->rx.flags),
			hc_usec);
	}

	/* Set CDU context validation values */
	for (cos = 0; cos < o->max_cos; cos++) {
		DP(BNX2X_MSG_SP, "setting context validation. cid %d, cos %d\n",
				 o->cids[cos], cos);
		DP(BNX2X_MSG_SP, "context pointer %p\n", init->cxts[cos]);
		bnx2x_set_ctx_validation(bp, init->cxts[cos], o->cids[cos]);
	}

	/* As no ramrod is sent, complete the command immediately  */
	o->complete_cmd(bp, o, BNX2X_Q_CMD_INIT);

	mmiowb();
	smp_mb();

	return 0;
}

static inline int bnx2x_q_send_setup_e1x(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	struct client_init_ramrod_data *rdata =
		(struct client_init_ramrod_data *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	int ramrod = RAMROD_CMD_ID_ETH_CLIENT_SETUP;

	/* Clear the ramrod data */
	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data */
	bnx2x_q_fill_setup_data_cmn(bp, params, rdata);

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	return bnx2x_sp_post(bp, ramrod, o->cids[BNX2X_PRIMARY_CID_INDEX],
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), ETH_CONNECTION_TYPE);
}

static inline int bnx2x_q_send_setup_e2(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	struct client_init_ramrod_data *rdata =
		(struct client_init_ramrod_data *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	int ramrod = RAMROD_CMD_ID_ETH_CLIENT_SETUP;

	/* Clear the ramrod data */
	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data */
	bnx2x_q_fill_setup_data_cmn(bp, params, rdata);
	bnx2x_q_fill_setup_data_e2(bp, params, rdata);

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	return bnx2x_sp_post(bp, ramrod, o->cids[BNX2X_PRIMARY_CID_INDEX],
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), ETH_CONNECTION_TYPE);
}

static inline int bnx2x_q_send_setup_tx_only(struct bnx2x *bp,
				  struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	struct tx_queue_init_ramrod_data *rdata =
		(struct tx_queue_init_ramrod_data *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	int ramrod = RAMROD_CMD_ID_ETH_TX_QUEUE_SETUP;
	struct bnx2x_queue_setup_tx_only_params *tx_only_params =
		&params->params.tx_only;
	u8 cid_index = tx_only_params->cid_index;


	if (cid_index >= o->max_cos) {
		BNX2X_ERR("queue[%d]: cid_index (%d) is out of range\n",
			  o->cl_id, cid_index);
		return -EINVAL;
	}

	DP(BNX2X_MSG_SP, "parameters received: cos: %d sp-id: %d\n",
			 tx_only_params->gen_params.cos,
			 tx_only_params->gen_params.spcl_id);

	/* Clear the ramrod data */
	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data */
	bnx2x_q_fill_setup_tx_only(bp, params, rdata);

	DP(BNX2X_MSG_SP, "sending tx-only ramrod: cid %d, client-id %d, sp-client id %d, cos %d\n",
			 o->cids[cid_index], rdata->general.client_id,
			 rdata->general.sp_client_id, rdata->general.cos);

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	return bnx2x_sp_post(bp, ramrod, o->cids[cid_index],
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), ETH_CONNECTION_TYPE);
}

static void bnx2x_q_fill_update_data(struct bnx2x *bp,
				     struct bnx2x_queue_sp_obj *obj,
				     struct bnx2x_queue_update_params *params,
				     struct client_update_ramrod_data *data)
{
	/* Client ID of the client to update */
	data->client_id = obj->cl_id;

	/* Function ID of the client to update */
	data->func_id = obj->func_id;

	/* Default VLAN value */
	data->default_vlan = cpu_to_le16(params->def_vlan);

	/* Inner VLAN stripping */
	data->inner_vlan_removal_enable_flg =
		test_bit(BNX2X_Q_UPDATE_IN_VLAN_REM, &params->update_flags);
	data->inner_vlan_removal_change_flg =
		test_bit(BNX2X_Q_UPDATE_IN_VLAN_REM_CHNG,
			 &params->update_flags);

	/* Outer VLAN sripping */
	data->outer_vlan_removal_enable_flg =
		test_bit(BNX2X_Q_UPDATE_OUT_VLAN_REM, &params->update_flags);
	data->outer_vlan_removal_change_flg =
		test_bit(BNX2X_Q_UPDATE_OUT_VLAN_REM_CHNG,
			 &params->update_flags);

	/* Drop packets that have source MAC that doesn't belong to this
	 * Queue.
	 */
	data->anti_spoofing_enable_flg =
		test_bit(BNX2X_Q_UPDATE_ANTI_SPOOF, &params->update_flags);
	data->anti_spoofing_change_flg =
		test_bit(BNX2X_Q_UPDATE_ANTI_SPOOF_CHNG, &params->update_flags);

	/* Activate/Deactivate */
	data->activate_flg =
		test_bit(BNX2X_Q_UPDATE_ACTIVATE, &params->update_flags);
	data->activate_change_flg =
		test_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG, &params->update_flags);

	/* Enable default VLAN */
	data->default_vlan_enable_flg =
		test_bit(BNX2X_Q_UPDATE_DEF_VLAN_EN, &params->update_flags);
	data->default_vlan_change_flg =
		test_bit(BNX2X_Q_UPDATE_DEF_VLAN_EN_CHNG,
			 &params->update_flags);

	/* silent vlan removal */
	data->silent_vlan_change_flg =
		test_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM_CHNG,
			 &params->update_flags);
	data->silent_vlan_removal_flg =
		test_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM, &params->update_flags);
	data->silent_vlan_value = cpu_to_le16(params->silent_removal_value);
	data->silent_vlan_mask = cpu_to_le16(params->silent_removal_mask);
}

static inline int bnx2x_q_send_update(struct bnx2x *bp,
				      struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	struct client_update_ramrod_data *rdata =
		(struct client_update_ramrod_data *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	struct bnx2x_queue_update_params *update_params =
		&params->params.update;
	u8 cid_index = update_params->cid_index;

	if (cid_index >= o->max_cos) {
		BNX2X_ERR("queue[%d]: cid_index (%d) is out of range\n",
			  o->cl_id, cid_index);
		return -EINVAL;
	}


	/* Clear the ramrod data */
	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data */
	bnx2x_q_fill_update_data(bp, o, update_params, rdata);

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_CLIENT_UPDATE,
			     o->cids[cid_index], U64_HI(data_mapping),
			     U64_LO(data_mapping), ETH_CONNECTION_TYPE);
}

/**
 * bnx2x_q_send_deactivate - send DEACTIVATE command
 *
 * @bp:		device handle
 * @params:
 *
 * implemented using the UPDATE command.
 */
static inline int bnx2x_q_send_deactivate(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_update_params *update = &params->params.update;

	memset(update, 0, sizeof(*update));

	__set_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG, &update->update_flags);

	return bnx2x_q_send_update(bp, params);
}

/**
 * bnx2x_q_send_activate - send ACTIVATE command
 *
 * @bp:		device handle
 * @params:
 *
 * implemented using the UPDATE command.
 */
static inline int bnx2x_q_send_activate(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_update_params *update = &params->params.update;

	memset(update, 0, sizeof(*update));

	__set_bit(BNX2X_Q_UPDATE_ACTIVATE, &update->update_flags);
	__set_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG, &update->update_flags);

	return bnx2x_q_send_update(bp, params);
}

static inline int bnx2x_q_send_update_tpa(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	/* TODO: Not implemented yet. */
	return -1;
}

static inline int bnx2x_q_send_halt(struct bnx2x *bp,
				    struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_HALT,
			     o->cids[BNX2X_PRIMARY_CID_INDEX], 0, o->cl_id,
			     ETH_CONNECTION_TYPE);
}

static inline int bnx2x_q_send_cfc_del(struct bnx2x *bp,
				       struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	u8 cid_idx = params->params.cfc_del.cid_index;

	if (cid_idx >= o->max_cos) {
		BNX2X_ERR("queue[%d]: cid_index (%d) is out of range\n",
			  o->cl_id, cid_idx);
		return -EINVAL;
	}

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_CFC_DEL,
			     o->cids[cid_idx], 0, 0, NONE_CONNECTION_TYPE);
}

static inline int bnx2x_q_send_terminate(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;
	u8 cid_index = params->params.terminate.cid_index;

	if (cid_index >= o->max_cos) {
		BNX2X_ERR("queue[%d]: cid_index (%d) is out of range\n",
			  o->cl_id, cid_index);
		return -EINVAL;
	}

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_TERMINATE,
			     o->cids[cid_index], 0, 0, ETH_CONNECTION_TYPE);
}

static inline int bnx2x_q_send_empty(struct bnx2x *bp,
				     struct bnx2x_queue_state_params *params)
{
	struct bnx2x_queue_sp_obj *o = params->q_obj;

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_EMPTY,
			     o->cids[BNX2X_PRIMARY_CID_INDEX], 0, 0,
			     ETH_CONNECTION_TYPE);
}

static inline int bnx2x_queue_send_cmd_cmn(struct bnx2x *bp,
					struct bnx2x_queue_state_params *params)
{
	switch (params->cmd) {
	case BNX2X_Q_CMD_INIT:
		return bnx2x_q_init(bp, params);
	case BNX2X_Q_CMD_SETUP_TX_ONLY:
		return bnx2x_q_send_setup_tx_only(bp, params);
	case BNX2X_Q_CMD_DEACTIVATE:
		return bnx2x_q_send_deactivate(bp, params);
	case BNX2X_Q_CMD_ACTIVATE:
		return bnx2x_q_send_activate(bp, params);
	case BNX2X_Q_CMD_UPDATE:
		return bnx2x_q_send_update(bp, params);
	case BNX2X_Q_CMD_UPDATE_TPA:
		return bnx2x_q_send_update_tpa(bp, params);
	case BNX2X_Q_CMD_HALT:
		return bnx2x_q_send_halt(bp, params);
	case BNX2X_Q_CMD_CFC_DEL:
		return bnx2x_q_send_cfc_del(bp, params);
	case BNX2X_Q_CMD_TERMINATE:
		return bnx2x_q_send_terminate(bp, params);
	case BNX2X_Q_CMD_EMPTY:
		return bnx2x_q_send_empty(bp, params);
	default:
		BNX2X_ERR("Unknown command: %d\n", params->cmd);
		return -EINVAL;
	}
}

static int bnx2x_queue_send_cmd_e1x(struct bnx2x *bp,
				    struct bnx2x_queue_state_params *params)
{
	switch (params->cmd) {
	case BNX2X_Q_CMD_SETUP:
		return bnx2x_q_send_setup_e1x(bp, params);
	case BNX2X_Q_CMD_INIT:
	case BNX2X_Q_CMD_SETUP_TX_ONLY:
	case BNX2X_Q_CMD_DEACTIVATE:
	case BNX2X_Q_CMD_ACTIVATE:
	case BNX2X_Q_CMD_UPDATE:
	case BNX2X_Q_CMD_UPDATE_TPA:
	case BNX2X_Q_CMD_HALT:
	case BNX2X_Q_CMD_CFC_DEL:
	case BNX2X_Q_CMD_TERMINATE:
	case BNX2X_Q_CMD_EMPTY:
		return bnx2x_queue_send_cmd_cmn(bp, params);
	default:
		BNX2X_ERR("Unknown command: %d\n", params->cmd);
		return -EINVAL;
	}
}

static int bnx2x_queue_send_cmd_e2(struct bnx2x *bp,
				   struct bnx2x_queue_state_params *params)
{
	switch (params->cmd) {
	case BNX2X_Q_CMD_SETUP:
		return bnx2x_q_send_setup_e2(bp, params);
	case BNX2X_Q_CMD_INIT:
	case BNX2X_Q_CMD_SETUP_TX_ONLY:
	case BNX2X_Q_CMD_DEACTIVATE:
	case BNX2X_Q_CMD_ACTIVATE:
	case BNX2X_Q_CMD_UPDATE:
	case BNX2X_Q_CMD_UPDATE_TPA:
	case BNX2X_Q_CMD_HALT:
	case BNX2X_Q_CMD_CFC_DEL:
	case BNX2X_Q_CMD_TERMINATE:
	case BNX2X_Q_CMD_EMPTY:
		return bnx2x_queue_send_cmd_cmn(bp, params);
	default:
		BNX2X_ERR("Unknown command: %d\n", params->cmd);
		return -EINVAL;
	}
}

/**
 * bnx2x_queue_chk_transition - check state machine of a regular Queue
 *
 * @bp:		device handle
 * @o:
 * @params:
 *
 * (not Forwarding)
 * It both checks if the requested command is legal in a current
 * state and, if it's legal, sets a `next_state' in the object
 * that will be used in the completion flow to set the `state'
 * of the object.
 *
 * returns 0 if a requested command is a legal transition,
 *         -EINVAL otherwise.
 */
static int bnx2x_queue_chk_transition(struct bnx2x *bp,
				      struct bnx2x_queue_sp_obj *o,
				      struct bnx2x_queue_state_params *params)
{
	enum bnx2x_q_state state = o->state, next_state = BNX2X_Q_STATE_MAX;
	enum bnx2x_queue_cmd cmd = params->cmd;
	struct bnx2x_queue_update_params *update_params =
		 &params->params.update;
	u8 next_tx_only = o->num_tx_only;

	/*
	 * Forget all pending for completion commands if a driver only state
	 * transition has been requested.
	 */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &params->ramrod_flags)) {
		o->pending = 0;
		o->next_state = BNX2X_Q_STATE_MAX;
	}

	/*
	 * Don't allow a next state transition if we are in the middle of
	 * the previous one.
	 */
	if (o->pending)
		return -EBUSY;

	switch (state) {
	case BNX2X_Q_STATE_RESET:
		if (cmd == BNX2X_Q_CMD_INIT)
			next_state = BNX2X_Q_STATE_INITIALIZED;

		break;
	case BNX2X_Q_STATE_INITIALIZED:
		if (cmd == BNX2X_Q_CMD_SETUP) {
			if (test_bit(BNX2X_Q_FLG_ACTIVE,
				     &params->params.setup.flags))
				next_state = BNX2X_Q_STATE_ACTIVE;
			else
				next_state = BNX2X_Q_STATE_INACTIVE;
		}

		break;
	case BNX2X_Q_STATE_ACTIVE:
		if (cmd == BNX2X_Q_CMD_DEACTIVATE)
			next_state = BNX2X_Q_STATE_INACTIVE;

		else if ((cmd == BNX2X_Q_CMD_EMPTY) ||
			 (cmd == BNX2X_Q_CMD_UPDATE_TPA))
			next_state = BNX2X_Q_STATE_ACTIVE;

		else if (cmd == BNX2X_Q_CMD_SETUP_TX_ONLY) {
			next_state = BNX2X_Q_STATE_MULTI_COS;
			next_tx_only = 1;
		}

		else if (cmd == BNX2X_Q_CMD_HALT)
			next_state = BNX2X_Q_STATE_STOPPED;

		else if (cmd == BNX2X_Q_CMD_UPDATE) {
			/* If "active" state change is requested, update the
			 *  state accordingly.
			 */
			if (test_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG,
				     &update_params->update_flags) &&
			    !test_bit(BNX2X_Q_UPDATE_ACTIVATE,
				      &update_params->update_flags))
				next_state = BNX2X_Q_STATE_INACTIVE;
			else
				next_state = BNX2X_Q_STATE_ACTIVE;
		}

		break;
	case BNX2X_Q_STATE_MULTI_COS:
		if (cmd == BNX2X_Q_CMD_TERMINATE)
			next_state = BNX2X_Q_STATE_MCOS_TERMINATED;

		else if (cmd == BNX2X_Q_CMD_SETUP_TX_ONLY) {
			next_state = BNX2X_Q_STATE_MULTI_COS;
			next_tx_only = o->num_tx_only + 1;
		}

		else if ((cmd == BNX2X_Q_CMD_EMPTY) ||
			 (cmd == BNX2X_Q_CMD_UPDATE_TPA))
			next_state = BNX2X_Q_STATE_MULTI_COS;

		else if (cmd == BNX2X_Q_CMD_UPDATE) {
			/* If "active" state change is requested, update the
			 *  state accordingly.
			 */
			if (test_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG,
				     &update_params->update_flags) &&
			    !test_bit(BNX2X_Q_UPDATE_ACTIVATE,
				      &update_params->update_flags))
				next_state = BNX2X_Q_STATE_INACTIVE;
			else
				next_state = BNX2X_Q_STATE_MULTI_COS;
		}

		break;
	case BNX2X_Q_STATE_MCOS_TERMINATED:
		if (cmd == BNX2X_Q_CMD_CFC_DEL) {
			next_tx_only = o->num_tx_only - 1;
			if (next_tx_only == 0)
				next_state = BNX2X_Q_STATE_ACTIVE;
			else
				next_state = BNX2X_Q_STATE_MULTI_COS;
		}

		break;
	case BNX2X_Q_STATE_INACTIVE:
		if (cmd == BNX2X_Q_CMD_ACTIVATE)
			next_state = BNX2X_Q_STATE_ACTIVE;

		else if ((cmd == BNX2X_Q_CMD_EMPTY) ||
			 (cmd == BNX2X_Q_CMD_UPDATE_TPA))
			next_state = BNX2X_Q_STATE_INACTIVE;

		else if (cmd == BNX2X_Q_CMD_HALT)
			next_state = BNX2X_Q_STATE_STOPPED;

		else if (cmd == BNX2X_Q_CMD_UPDATE) {
			/* If "active" state change is requested, update the
			 * state accordingly.
			 */
			if (test_bit(BNX2X_Q_UPDATE_ACTIVATE_CHNG,
				     &update_params->update_flags) &&
			    test_bit(BNX2X_Q_UPDATE_ACTIVATE,
				     &update_params->update_flags)){
				if (o->num_tx_only == 0)
					next_state = BNX2X_Q_STATE_ACTIVE;
				else /* tx only queues exist for this queue */
					next_state = BNX2X_Q_STATE_MULTI_COS;
			} else
				next_state = BNX2X_Q_STATE_INACTIVE;
		}

		break;
	case BNX2X_Q_STATE_STOPPED:
		if (cmd == BNX2X_Q_CMD_TERMINATE)
			next_state = BNX2X_Q_STATE_TERMINATED;

		break;
	case BNX2X_Q_STATE_TERMINATED:
		if (cmd == BNX2X_Q_CMD_CFC_DEL)
			next_state = BNX2X_Q_STATE_RESET;

		break;
	default:
		BNX2X_ERR("Illegal state: %d\n", state);
	}

	/* Transition is assured */
	if (next_state != BNX2X_Q_STATE_MAX) {
		DP(BNX2X_MSG_SP, "Good state transition: %d(%d)->%d\n",
				 state, cmd, next_state);
		o->next_state = next_state;
		o->next_tx_only = next_tx_only;
		return 0;
	}

	DP(BNX2X_MSG_SP, "Bad state transition request: %d %d\n", state, cmd);

	return -EINVAL;
}

void bnx2x_init_queue_obj(struct bnx2x *bp,
			  struct bnx2x_queue_sp_obj *obj,
			  u8 cl_id, u32 *cids, u8 cid_cnt, u8 func_id,
			  void *rdata,
			  dma_addr_t rdata_mapping, unsigned long type)
{
	memset(obj, 0, sizeof(*obj));

	/* We support only BNX2X_MULTI_TX_COS Tx CoS at the moment */
	BUG_ON(BNX2X_MULTI_TX_COS < cid_cnt);

	memcpy(obj->cids, cids, sizeof(obj->cids[0]) * cid_cnt);
	obj->max_cos = cid_cnt;
	obj->cl_id = cl_id;
	obj->func_id = func_id;
	obj->rdata = rdata;
	obj->rdata_mapping = rdata_mapping;
	obj->type = type;
	obj->next_state = BNX2X_Q_STATE_MAX;

	if (CHIP_IS_E1x(bp))
		obj->send_cmd = bnx2x_queue_send_cmd_e1x;
	else
		obj->send_cmd = bnx2x_queue_send_cmd_e2;

	obj->check_transition = bnx2x_queue_chk_transition;

	obj->complete_cmd = bnx2x_queue_comp_cmd;
	obj->wait_comp = bnx2x_queue_wait_comp;
	obj->set_pending = bnx2x_queue_set_pending;
}

/********************** Function state object *********************************/
enum bnx2x_func_state bnx2x_func_get_state(struct bnx2x *bp,
					   struct bnx2x_func_sp_obj *o)
{
	/* in the middle of transaction - return INVALID state */
	if (o->pending)
		return BNX2X_F_STATE_MAX;

	/*
	 * unsure the order of reading of o->pending and o->state
	 * o->pending should be read first
	 */
	rmb();

	return o->state;
}

static int bnx2x_func_wait_comp(struct bnx2x *bp,
				struct bnx2x_func_sp_obj *o,
				enum bnx2x_func_cmd cmd)
{
	return bnx2x_state_wait(bp, cmd, &o->pending);
}

/**
 * bnx2x_func_state_change_comp - complete the state machine transition
 *
 * @bp:		device handle
 * @o:
 * @cmd:
 *
 * Called on state change transition. Completes the state
 * machine transition only - no HW interaction.
 */
static inline int bnx2x_func_state_change_comp(struct bnx2x *bp,
					       struct bnx2x_func_sp_obj *o,
					       enum bnx2x_func_cmd cmd)
{
	unsigned long cur_pending = o->pending;

	if (!test_and_clear_bit(cmd, &cur_pending)) {
		BNX2X_ERR("Bad MC reply %d for func %d in state %d pending 0x%lx, next_state %d\n",
			  cmd, BP_FUNC(bp), o->state,
			  cur_pending, o->next_state);
		return -EINVAL;
	}

	DP(BNX2X_MSG_SP,
	   "Completing command %d for func %d, setting state to %d\n",
	   cmd, BP_FUNC(bp), o->next_state);

	o->state = o->next_state;
	o->next_state = BNX2X_F_STATE_MAX;

	/* It's important that o->state and o->next_state are
	 * updated before o->pending.
	 */
	wmb();

	clear_bit(cmd, &o->pending);
	smp_mb__after_clear_bit();

	return 0;
}

/**
 * bnx2x_func_comp_cmd - complete the state change command
 *
 * @bp:		device handle
 * @o:
 * @cmd:
 *
 * Checks that the arrived completion is expected.
 */
static int bnx2x_func_comp_cmd(struct bnx2x *bp,
			       struct bnx2x_func_sp_obj *o,
			       enum bnx2x_func_cmd cmd)
{
	/* Complete the state machine part first, check if it's a
	 * legal completion.
	 */
	int rc = bnx2x_func_state_change_comp(bp, o, cmd);
	return rc;
}

/**
 * bnx2x_func_chk_transition - perform function state machine transition
 *
 * @bp:		device handle
 * @o:
 * @params:
 *
 * It both checks if the requested command is legal in a current
 * state and, if it's legal, sets a `next_state' in the object
 * that will be used in the completion flow to set the `state'
 * of the object.
 *
 * returns 0 if a requested command is a legal transition,
 *         -EINVAL otherwise.
 */
static int bnx2x_func_chk_transition(struct bnx2x *bp,
				     struct bnx2x_func_sp_obj *o,
				     struct bnx2x_func_state_params *params)
{
	enum bnx2x_func_state state = o->state, next_state = BNX2X_F_STATE_MAX;
	enum bnx2x_func_cmd cmd = params->cmd;

	/*
	 * Forget all pending for completion commands if a driver only state
	 * transition has been requested.
	 */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &params->ramrod_flags)) {
		o->pending = 0;
		o->next_state = BNX2X_F_STATE_MAX;
	}

	/*
	 * Don't allow a next state transition if we are in the middle of
	 * the previous one.
	 */
	if (o->pending)
		return -EBUSY;

	switch (state) {
	case BNX2X_F_STATE_RESET:
		if (cmd == BNX2X_F_CMD_HW_INIT)
			next_state = BNX2X_F_STATE_INITIALIZED;

		break;
	case BNX2X_F_STATE_INITIALIZED:
		if (cmd == BNX2X_F_CMD_START)
			next_state = BNX2X_F_STATE_STARTED;

		else if (cmd == BNX2X_F_CMD_HW_RESET)
			next_state = BNX2X_F_STATE_RESET;

		break;
	case BNX2X_F_STATE_STARTED:
		if (cmd == BNX2X_F_CMD_STOP)
			next_state = BNX2X_F_STATE_INITIALIZED;
		/* afex ramrods can be sent only in started mode, and only
		 * if not pending for function_stop ramrod completion
		 * for these events - next state remained STARTED.
		 */
		else if ((cmd == BNX2X_F_CMD_AFEX_UPDATE) &&
			 (!test_bit(BNX2X_F_CMD_STOP, &o->pending)))
			next_state = BNX2X_F_STATE_STARTED;

		else if ((cmd == BNX2X_F_CMD_AFEX_VIFLISTS) &&
			 (!test_bit(BNX2X_F_CMD_STOP, &o->pending)))
			next_state = BNX2X_F_STATE_STARTED;
		else if (cmd == BNX2X_F_CMD_TX_STOP)
			next_state = BNX2X_F_STATE_TX_STOPPED;

		break;
	case BNX2X_F_STATE_TX_STOPPED:
		if (cmd == BNX2X_F_CMD_TX_START)
			next_state = BNX2X_F_STATE_STARTED;

		break;
	default:
		BNX2X_ERR("Unknown state: %d\n", state);
	}

	/* Transition is assured */
	if (next_state != BNX2X_F_STATE_MAX) {
		DP(BNX2X_MSG_SP, "Good function state transition: %d(%d)->%d\n",
				 state, cmd, next_state);
		o->next_state = next_state;
		return 0;
	}

	DP(BNX2X_MSG_SP, "Bad function state transition request: %d %d\n",
			 state, cmd);

	return -EINVAL;
}

/**
 * bnx2x_func_init_func - performs HW init at function stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Init HW when the current phase is
 * FW_MSG_CODE_DRV_LOAD_FUNCTION: initialize only FUNCTION-only
 * HW blocks.
 */
static inline int bnx2x_func_init_func(struct bnx2x *bp,
				       const struct bnx2x_func_sp_drv_ops *drv)
{
	return drv->init_hw_func(bp);
}

/**
 * bnx2x_func_init_port - performs HW init at port stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Init HW when the current phase is
 * FW_MSG_CODE_DRV_LOAD_PORT: initialize PORT-only and
 * FUNCTION-only HW blocks.
 *
 */
static inline int bnx2x_func_init_port(struct bnx2x *bp,
				       const struct bnx2x_func_sp_drv_ops *drv)
{
	int rc = drv->init_hw_port(bp);
	if (rc)
		return rc;

	return bnx2x_func_init_func(bp, drv);
}

/**
 * bnx2x_func_init_cmn_chip - performs HW init at chip-common stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Init HW when the current phase is
 * FW_MSG_CODE_DRV_LOAD_COMMON_CHIP: initialize COMMON_CHIP,
 * PORT-only and FUNCTION-only HW blocks.
 */
static inline int bnx2x_func_init_cmn_chip(struct bnx2x *bp,
					const struct bnx2x_func_sp_drv_ops *drv)
{
	int rc = drv->init_hw_cmn_chip(bp);
	if (rc)
		return rc;

	return bnx2x_func_init_port(bp, drv);
}

/**
 * bnx2x_func_init_cmn - performs HW init at common stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Init HW when the current phase is
 * FW_MSG_CODE_DRV_LOAD_COMMON_CHIP: initialize COMMON,
 * PORT-only and FUNCTION-only HW blocks.
 */
static inline int bnx2x_func_init_cmn(struct bnx2x *bp,
				      const struct bnx2x_func_sp_drv_ops *drv)
{
	int rc = drv->init_hw_cmn(bp);
	if (rc)
		return rc;

	return bnx2x_func_init_port(bp, drv);
}

static int bnx2x_func_hw_init(struct bnx2x *bp,
			      struct bnx2x_func_state_params *params)
{
	u32 load_code = params->params.hw_init.load_phase;
	struct bnx2x_func_sp_obj *o = params->f_obj;
	const struct bnx2x_func_sp_drv_ops *drv = o->drv;
	int rc = 0;

	DP(BNX2X_MSG_SP, "function %d  load_code %x\n",
			 BP_ABS_FUNC(bp), load_code);

	/* Prepare buffers for unzipping the FW */
	rc = drv->gunzip_init(bp);
	if (rc)
		return rc;

	/* Prepare FW */
	rc = drv->init_fw(bp);
	if (rc) {
		BNX2X_ERR("Error loading firmware\n");
		goto init_err;
	}

	/* Handle the beginning of COMMON_XXX pases separatelly... */
	switch (load_code) {
	case FW_MSG_CODE_DRV_LOAD_COMMON_CHIP:
		rc = bnx2x_func_init_cmn_chip(bp, drv);
		if (rc)
			goto init_err;

		break;
	case FW_MSG_CODE_DRV_LOAD_COMMON:
		rc = bnx2x_func_init_cmn(bp, drv);
		if (rc)
			goto init_err;

		break;
	case FW_MSG_CODE_DRV_LOAD_PORT:
		rc = bnx2x_func_init_port(bp, drv);
		if (rc)
			goto init_err;

		break;
	case FW_MSG_CODE_DRV_LOAD_FUNCTION:
		rc = bnx2x_func_init_func(bp, drv);
		if (rc)
			goto init_err;

		break;
	default:
		BNX2X_ERR("Unknown load_code (0x%x) from MCP\n", load_code);
		rc = -EINVAL;
	}

init_err:
	drv->gunzip_end(bp);

	/* In case of success, complete the comand immediatelly: no ramrods
	 * have been sent.
	 */
	if (!rc)
		o->complete_cmd(bp, o, BNX2X_F_CMD_HW_INIT);

	return rc;
}

/**
 * bnx2x_func_reset_func - reset HW at function stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Reset HW at FW_MSG_CODE_DRV_UNLOAD_FUNCTION stage: reset only
 * FUNCTION-only HW blocks.
 */
static inline void bnx2x_func_reset_func(struct bnx2x *bp,
					const struct bnx2x_func_sp_drv_ops *drv)
{
	drv->reset_hw_func(bp);
}

/**
 * bnx2x_func_reset_port - reser HW at port stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Reset HW at FW_MSG_CODE_DRV_UNLOAD_PORT stage: reset
 * FUNCTION-only and PORT-only HW blocks.
 *
 *                 !!!IMPORTANT!!!
 *
 * It's important to call reset_port before reset_func() as the last thing
 * reset_func does is pf_disable() thus disabling PGLUE_B, which
 * makes impossible any DMAE transactions.
 */
static inline void bnx2x_func_reset_port(struct bnx2x *bp,
					const struct bnx2x_func_sp_drv_ops *drv)
{
	drv->reset_hw_port(bp);
	bnx2x_func_reset_func(bp, drv);
}

/**
 * bnx2x_func_reset_cmn - reser HW at common stage
 *
 * @bp:		device handle
 * @drv:
 *
 * Reset HW at FW_MSG_CODE_DRV_UNLOAD_COMMON and
 * FW_MSG_CODE_DRV_UNLOAD_COMMON_CHIP stages: reset COMMON,
 * COMMON_CHIP, FUNCTION-only and PORT-only HW blocks.
 */
static inline void bnx2x_func_reset_cmn(struct bnx2x *bp,
					const struct bnx2x_func_sp_drv_ops *drv)
{
	bnx2x_func_reset_port(bp, drv);
	drv->reset_hw_cmn(bp);
}


static inline int bnx2x_func_hw_reset(struct bnx2x *bp,
				      struct bnx2x_func_state_params *params)
{
	u32 reset_phase = params->params.hw_reset.reset_phase;
	struct bnx2x_func_sp_obj *o = params->f_obj;
	const struct bnx2x_func_sp_drv_ops *drv = o->drv;

	DP(BNX2X_MSG_SP, "function %d  reset_phase %x\n", BP_ABS_FUNC(bp),
			 reset_phase);

	switch (reset_phase) {
	case FW_MSG_CODE_DRV_UNLOAD_COMMON:
		bnx2x_func_reset_cmn(bp, drv);
		break;
	case FW_MSG_CODE_DRV_UNLOAD_PORT:
		bnx2x_func_reset_port(bp, drv);
		break;
	case FW_MSG_CODE_DRV_UNLOAD_FUNCTION:
		bnx2x_func_reset_func(bp, drv);
		break;
	default:
		BNX2X_ERR("Unknown reset_phase (0x%x) from MCP\n",
			   reset_phase);
		break;
	}

	/* Complete the comand immediatelly: no ramrods have been sent. */
	o->complete_cmd(bp, o, BNX2X_F_CMD_HW_RESET);

	return 0;
}

static inline int bnx2x_func_send_start(struct bnx2x *bp,
					struct bnx2x_func_state_params *params)
{
	struct bnx2x_func_sp_obj *o = params->f_obj;
	struct function_start_data *rdata =
		(struct function_start_data *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	struct bnx2x_func_start_params *start_params = &params->params.start;

	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data with provided parameters */
	rdata->function_mode = (u8)start_params->mf_mode;
	rdata->sd_vlan_tag   = cpu_to_le16(start_params->sd_vlan_tag);
	rdata->path_id       = BP_PATH(bp);
	rdata->network_cos_mode = start_params->network_cos_mode;

	/*
	 *  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_FUNCTION_START, 0,
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), NONE_CONNECTION_TYPE);
}

static inline int bnx2x_func_send_afex_update(struct bnx2x *bp,
					 struct bnx2x_func_state_params *params)
{
	struct bnx2x_func_sp_obj *o = params->f_obj;
	struct function_update_data *rdata =
		(struct function_update_data *)o->afex_rdata;
	dma_addr_t data_mapping = o->afex_rdata_mapping;
	struct bnx2x_func_afex_update_params *afex_update_params =
		&params->params.afex_update;

	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data with provided parameters */
	rdata->vif_id_change_flg = 1;
	rdata->vif_id = cpu_to_le16(afex_update_params->vif_id);
	rdata->afex_default_vlan_change_flg = 1;
	rdata->afex_default_vlan =
		cpu_to_le16(afex_update_params->afex_default_vlan);
	rdata->allowed_priorities_change_flg = 1;
	rdata->allowed_priorities = afex_update_params->allowed_priorities;

	/*  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */
	DP(BNX2X_MSG_SP,
	   "afex: sending func_update vif_id 0x%x dvlan 0x%x prio 0x%x\n",
	   rdata->vif_id,
	   rdata->afex_default_vlan, rdata->allowed_priorities);

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_FUNCTION_UPDATE, 0,
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), NONE_CONNECTION_TYPE);
}

static
inline int bnx2x_func_send_afex_viflists(struct bnx2x *bp,
					 struct bnx2x_func_state_params *params)
{
	struct bnx2x_func_sp_obj *o = params->f_obj;
	struct afex_vif_list_ramrod_data *rdata =
		(struct afex_vif_list_ramrod_data *)o->afex_rdata;
	struct bnx2x_func_afex_viflists_params *afex_viflist_params =
		&params->params.afex_viflists;
	u64 *p_rdata = (u64 *)rdata;

	memset(rdata, 0, sizeof(*rdata));

	/* Fill the ramrod data with provided parameters */
	rdata->vif_list_index = afex_viflist_params->vif_list_index;
	rdata->func_bit_map = afex_viflist_params->func_bit_map;
	rdata->afex_vif_list_command =
		afex_viflist_params->afex_vif_list_command;
	rdata->func_to_clear = afex_viflist_params->func_to_clear;

	/* send in echo type of sub command */
	rdata->echo = afex_viflist_params->afex_vif_list_command;

	/*  No need for an explicit memory barrier here as long we would
	 *  need to ensure the ordering of writing to the SPQ element
	 *  and updating of the SPQ producer which involves a memory
	 *  read and we will have to put a full memory barrier there
	 *  (inside bnx2x_sp_post()).
	 */

	DP(BNX2X_MSG_SP, "afex: ramrod lists, cmd 0x%x index 0x%x func_bit_map 0x%x func_to_clr 0x%x\n",
	   rdata->afex_vif_list_command, rdata->vif_list_index,
	   rdata->func_bit_map, rdata->func_to_clear);

	/* this ramrod sends data directly and not through DMA mapping */
	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_AFEX_VIF_LISTS, 0,
			     U64_HI(*p_rdata), U64_LO(*p_rdata),
			     NONE_CONNECTION_TYPE);
}

static inline int bnx2x_func_send_stop(struct bnx2x *bp,
				       struct bnx2x_func_state_params *params)
{
	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_FUNCTION_STOP, 0, 0, 0,
			     NONE_CONNECTION_TYPE);
}

static inline int bnx2x_func_send_tx_stop(struct bnx2x *bp,
				       struct bnx2x_func_state_params *params)
{
	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_STOP_TRAFFIC, 0, 0, 0,
			     NONE_CONNECTION_TYPE);
}
static inline int bnx2x_func_send_tx_start(struct bnx2x *bp,
				       struct bnx2x_func_state_params *params)
{
	struct bnx2x_func_sp_obj *o = params->f_obj;
	struct flow_control_configuration *rdata =
		(struct flow_control_configuration *)o->rdata;
	dma_addr_t data_mapping = o->rdata_mapping;
	struct bnx2x_func_tx_start_params *tx_start_params =
		&params->params.tx_start;
	int i;

	memset(rdata, 0, sizeof(*rdata));

	rdata->dcb_enabled = tx_start_params->dcb_enabled;
	rdata->dcb_version = tx_start_params->dcb_version;
	rdata->dont_add_pri_0_en = tx_start_params->dont_add_pri_0_en;

	for (i = 0; i < ARRAY_SIZE(rdata->traffic_type_to_priority_cos); i++)
		rdata->traffic_type_to_priority_cos[i] =
			tx_start_params->traffic_type_to_priority_cos[i];

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_START_TRAFFIC, 0,
			     U64_HI(data_mapping),
			     U64_LO(data_mapping), NONE_CONNECTION_TYPE);
}

static int bnx2x_func_send_cmd(struct bnx2x *bp,
			       struct bnx2x_func_state_params *params)
{
	switch (params->cmd) {
	case BNX2X_F_CMD_HW_INIT:
		return bnx2x_func_hw_init(bp, params);
	case BNX2X_F_CMD_START:
		return bnx2x_func_send_start(bp, params);
	case BNX2X_F_CMD_STOP:
		return bnx2x_func_send_stop(bp, params);
	case BNX2X_F_CMD_HW_RESET:
		return bnx2x_func_hw_reset(bp, params);
	case BNX2X_F_CMD_AFEX_UPDATE:
		return bnx2x_func_send_afex_update(bp, params);
	case BNX2X_F_CMD_AFEX_VIFLISTS:
		return bnx2x_func_send_afex_viflists(bp, params);
	case BNX2X_F_CMD_TX_STOP:
		return bnx2x_func_send_tx_stop(bp, params);
	case BNX2X_F_CMD_TX_START:
		return bnx2x_func_send_tx_start(bp, params);
	default:
		BNX2X_ERR("Unknown command: %d\n", params->cmd);
		return -EINVAL;
	}
}

void bnx2x_init_func_obj(struct bnx2x *bp,
			 struct bnx2x_func_sp_obj *obj,
			 void *rdata, dma_addr_t rdata_mapping,
			 void *afex_rdata, dma_addr_t afex_rdata_mapping,
			 struct bnx2x_func_sp_drv_ops *drv_iface)
{
	memset(obj, 0, sizeof(*obj));

	mutex_init(&obj->one_pending_mutex);

	obj->rdata = rdata;
	obj->rdata_mapping = rdata_mapping;
	obj->afex_rdata = afex_rdata;
	obj->afex_rdata_mapping = afex_rdata_mapping;
	obj->send_cmd = bnx2x_func_send_cmd;
	obj->check_transition = bnx2x_func_chk_transition;
	obj->complete_cmd = bnx2x_func_comp_cmd;
	obj->wait_comp = bnx2x_func_wait_comp;

	obj->drv = drv_iface;
}

/**
 * bnx2x_func_state_change - perform Function state change transition
 *
 * @bp:		device handle
 * @params:	parameters to perform the transaction
 *
 * returns 0 in case of successfully completed transition,
 *         negative error code in case of failure, positive
 *         (EBUSY) value if there is a completion to that is
 *         still pending (possible only if RAMROD_COMP_WAIT is
 *         not set in params->ramrod_flags for asynchronous
 *         commands).
 */
int bnx2x_func_state_change(struct bnx2x *bp,
			    struct bnx2x_func_state_params *params)
{
	struct bnx2x_func_sp_obj *o = params->f_obj;
	int rc;
	enum bnx2x_func_cmd cmd = params->cmd;
	unsigned long *pending = &o->pending;

	mutex_lock(&o->one_pending_mutex);

	/* Check that the requested transition is legal */
	if (o->check_transition(bp, o, params)) {
		mutex_unlock(&o->one_pending_mutex);
		return -EINVAL;
	}

	/* Set "pending" bit */
	set_bit(cmd, pending);

	/* Don't send a command if only driver cleanup was requested */
	if (test_bit(RAMROD_DRV_CLR_ONLY, &params->ramrod_flags)) {
		bnx2x_func_state_change_comp(bp, o, cmd);
		mutex_unlock(&o->one_pending_mutex);
	} else {
		/* Send a ramrod */
		rc = o->send_cmd(bp, params);

		mutex_unlock(&o->one_pending_mutex);

		if (rc) {
			o->next_state = BNX2X_F_STATE_MAX;
			clear_bit(cmd, pending);
			smp_mb__after_clear_bit();
			return rc;
		}

		if (test_bit(RAMROD_COMP_WAIT, &params->ramrod_flags)) {
			rc = o->wait_comp(bp, o, cmd);
			if (rc)
				return rc;

			return 0;
		}
	}

	return !!test_bit(cmd, pending);
}
