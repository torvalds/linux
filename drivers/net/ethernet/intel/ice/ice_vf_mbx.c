// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"
#include "ice_vf_mbx.h"

/**
 * ice_aq_send_msg_to_vf
 * @hw: pointer to the hardware structure
 * @vfid: VF ID to send msg
 * @v_opcode: opcodes for VF-PF communication
 * @v_retval: return error code
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @cd: pointer to command details
 *
 * Send message to VF driver (0x0802) using mailbox
 * queue and asynchronously sending message via
 * ice_sq_send_cmd() function
 */
int
ice_aq_send_msg_to_vf(struct ice_hw *hw, u16 vfid, u32 v_opcode, u32 v_retval,
		      u8 *msg, u16 msglen, struct ice_sq_cd *cd)
{
	struct ice_aqc_pf_vf_msg *cmd;
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_mbx_opc_send_msg_to_vf);

	cmd = &desc.params.virt;
	cmd->id = cpu_to_le32(vfid);

	desc.cookie_high = cpu_to_le32(v_opcode);
	desc.cookie_low = cpu_to_le32(v_retval);

	if (msglen)
		desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	return ice_sq_send_cmd(hw, &hw->mailboxq, &desc, msg, msglen, cd);
}

static const u32 ice_legacy_aq_to_vc_speed[] = {
	VIRTCHNL_LINK_SPEED_100MB,	/* BIT(0) */
	VIRTCHNL_LINK_SPEED_100MB,
	VIRTCHNL_LINK_SPEED_1GB,
	VIRTCHNL_LINK_SPEED_1GB,
	VIRTCHNL_LINK_SPEED_1GB,
	VIRTCHNL_LINK_SPEED_10GB,
	VIRTCHNL_LINK_SPEED_20GB,
	VIRTCHNL_LINK_SPEED_25GB,
	VIRTCHNL_LINK_SPEED_40GB,
	VIRTCHNL_LINK_SPEED_40GB,
	VIRTCHNL_LINK_SPEED_40GB,
};

/**
 * ice_conv_link_speed_to_virtchnl
 * @adv_link_support: determines the format of the returned link speed
 * @link_speed: variable containing the link_speed to be converted
 *
 * Convert link speed supported by HW to link speed supported by virtchnl.
 * If adv_link_support is true, then return link speed in Mbps. Else return
 * link speed as a VIRTCHNL_LINK_SPEED_* casted to a u32. Note that the caller
 * needs to cast back to an enum virtchnl_link_speed in the case where
 * adv_link_support is false, but when adv_link_support is true the caller can
 * expect the speed in Mbps.
 */
u32 ice_conv_link_speed_to_virtchnl(bool adv_link_support, u16 link_speed)
{
	/* convert a BIT() value into an array index */
	u32 index = fls(link_speed) - 1;

	if (adv_link_support)
		return ice_get_link_speed(index);
	else if (index < ARRAY_SIZE(ice_legacy_aq_to_vc_speed))
		/* Virtchnl speeds are not defined for every speed supported in
		 * the hardware. To maintain compatibility with older AVF
		 * drivers, while reporting the speed the new speed values are
		 * resolved to the closest known virtchnl speeds
		 */
		return ice_legacy_aq_to_vc_speed[index];

	return VIRTCHNL_LINK_SPEED_UNKNOWN;
}

/* The mailbox overflow detection algorithm helps to check if there
 * is a possibility of a malicious VF transmitting too many MBX messages to the
 * PF.
 * 1. The mailbox snapshot structure, ice_mbx_snapshot, is initialized during
 * driver initialization in ice_init_hw() using ice_mbx_init_snapshot().
 * The struct ice_mbx_snapshot helps to track and traverse a static window of
 * messages within the mailbox queue while looking for a malicious VF.
 *
 * 2. When the caller starts processing its mailbox queue in response to an
 * interrupt, the structure ice_mbx_snapshot is expected to be cleared before
 * the algorithm can be run for the first time for that interrupt. This
 * requires calling ice_mbx_reset_snapshot() as well as calling
 * ice_mbx_reset_vf_info() for each VF tracking structure.
 *
 * 3. For every message read by the caller from the MBX Queue, the caller must
 * call the detection algorithm's entry function ice_mbx_vf_state_handler().
 * Before every call to ice_mbx_vf_state_handler() the struct ice_mbx_data is
 * filled as it is required to be passed to the algorithm.
 *
 * 4. Every time a message is read from the MBX queue, a tracking structure
 * for the VF must be passed to the state handler. The boolean output
 * report_malvf from ice_mbx_vf_state_handler() serves as an indicator to the
 * caller whether it must report this VF as malicious or not.
 *
 * 5. When a VF is identified to be malicious, the caller can send a message
 * to the system administrator.
 *
 * 6. The PF is responsible for maintaining the struct ice_mbx_vf_info
 * structure for each VF. The PF should clear the VF tracking structure if the
 * VF is reset. When a VF is shut down and brought back up, we will then
 * assume that the new VF is not malicious and may report it again if we
 * detect it again.
 *
 * 7. The function ice_mbx_reset_snapshot() is called to reset the information
 * in ice_mbx_snapshot for every new mailbox interrupt handled.
 */
#define ICE_RQ_DATA_MASK(rq_data) ((rq_data) & PF_MBX_ARQH_ARQH_M)
/* Using the highest value for an unsigned 16-bit value 0xFFFF to indicate that
 * the max messages check must be ignored in the algorithm
 */
#define ICE_IGNORE_MAX_MSG_CNT	0xFFFF

/**
 * ice_mbx_reset_snapshot - Reset mailbox snapshot structure
 * @snap: pointer to the mailbox snapshot
 */
static void ice_mbx_reset_snapshot(struct ice_mbx_snapshot *snap)
{
	struct ice_mbx_vf_info *vf_info;

	/* Clear mbx_buf in the mailbox snaphot structure and setting the
	 * mailbox snapshot state to a new capture.
	 */
	memset(&snap->mbx_buf, 0, sizeof(snap->mbx_buf));
	snap->mbx_buf.state = ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT;

	/* Reset message counts for all VFs to zero */
	list_for_each_entry(vf_info, &snap->mbx_vf, list_entry)
		vf_info->msg_count = 0;
}

/**
 * ice_mbx_traverse - Pass through mailbox snapshot
 * @hw: pointer to the HW struct
 * @new_state: new algorithm state
 *
 * Traversing the mailbox static snapshot without checking
 * for malicious VFs.
 */
static void
ice_mbx_traverse(struct ice_hw *hw,
		 enum ice_mbx_snapshot_state *new_state)
{
	struct ice_mbx_snap_buffer_data *snap_buf;
	u32 num_iterations;

	snap_buf = &hw->mbx_snapshot.mbx_buf;

	/* As mailbox buffer is circular, applying a mask
	 * on the incremented iteration count.
	 */
	num_iterations = ICE_RQ_DATA_MASK(++snap_buf->num_iterations);

	/* Checking either of the below conditions to exit snapshot traversal:
	 * Condition-1: If the number of iterations in the mailbox is equal to
	 * the mailbox head which would indicate that we have reached the end
	 * of the static snapshot.
	 * Condition-2: If the maximum messages serviced in the mailbox for a
	 * given interrupt is the highest possible value then there is no need
	 * to check if the number of messages processed is equal to it. If not
	 * check if the number of messages processed is greater than or equal
	 * to the maximum number of mailbox entries serviced in current work item.
	 */
	if (num_iterations == snap_buf->head ||
	    (snap_buf->max_num_msgs_mbx < ICE_IGNORE_MAX_MSG_CNT &&
	     ++snap_buf->num_msg_proc >= snap_buf->max_num_msgs_mbx))
		*new_state = ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT;
}

/**
 * ice_mbx_detect_malvf - Detect malicious VF in snapshot
 * @hw: pointer to the HW struct
 * @vf_info: mailbox tracking structure for a VF
 * @new_state: new algorithm state
 * @is_malvf: boolean output to indicate if VF is malicious
 *
 * This function tracks the number of asynchronous messages
 * sent per VF and marks the VF as malicious if it exceeds
 * the permissible number of messages to send.
 */
static int
ice_mbx_detect_malvf(struct ice_hw *hw, struct ice_mbx_vf_info *vf_info,
		     enum ice_mbx_snapshot_state *new_state,
		     bool *is_malvf)
{
	/* increment the message count for this VF */
	vf_info->msg_count++;

	if (vf_info->msg_count >= ICE_ASYNC_VF_MSG_THRESHOLD)
		*is_malvf = true;

	/* continue to iterate through the mailbox snapshot */
	ice_mbx_traverse(hw, new_state);

	return 0;
}

/**
 * ice_mbx_vf_dec_trig_e830 - Decrements the VF mailbox queue counter
 * @hw: pointer to the HW struct
 * @event: pointer to the control queue receive event
 *
 * This function triggers to decrement the counter
 * MBX_VF_IN_FLIGHT_MSGS_AT_PF_CNT when the driver replenishes
 * the buffers at the PF mailbox queue.
 */
void ice_mbx_vf_dec_trig_e830(const struct ice_hw *hw,
			      const struct ice_rq_event_info *event)
{
	u16 vfid = le16_to_cpu(event->desc.retval);

	wr32(hw, E830_MBX_VF_DEC_TRIG(vfid), 1);
}

/**
 * ice_mbx_vf_clear_cnt_e830 - Clear the VF mailbox queue count
 * @hw: pointer to the HW struct
 * @vf_id: VF ID in the PF space
 *
 * This function clears the counter MBX_VF_IN_FLIGHT_MSGS_AT_PF_CNT, and should
 * be called when a VF is created and on VF reset.
 */
void ice_mbx_vf_clear_cnt_e830(const struct ice_hw *hw, u16 vf_id)
{
	u32 reg = rd32(hw, E830_MBX_VF_IN_FLIGHT_MSGS_AT_PF_CNT(vf_id));

	wr32(hw, E830_MBX_VF_DEC_TRIG(vf_id), reg);
}

/**
 * ice_mbx_vf_state_handler - Handle states of the overflow algorithm
 * @hw: pointer to the HW struct
 * @mbx_data: pointer to structure containing mailbox data
 * @vf_info: mailbox tracking structure for the VF in question
 * @report_malvf: boolean output to indicate whether VF should be reported
 *
 * The function serves as an entry point for the malicious VF
 * detection algorithm by handling the different states and state
 * transitions of the algorithm:
 * New snapshot: This state is entered when creating a new static
 * snapshot. The data from any previous mailbox snapshot is
 * cleared and a new capture of the mailbox head and tail is
 * logged. This will be the new static snapshot to detect
 * asynchronous messages sent by VFs. On capturing the snapshot
 * and depending on whether the number of pending messages in that
 * snapshot exceed the watermark value, the state machine enters
 * traverse or detect states.
 * Traverse: If pending message count is below watermark then iterate
 * through the snapshot without any action on VF.
 * Detect: If pending message count exceeds watermark traverse
 * the static snapshot and look for a malicious VF.
 */
int
ice_mbx_vf_state_handler(struct ice_hw *hw, struct ice_mbx_data *mbx_data,
			 struct ice_mbx_vf_info *vf_info, bool *report_malvf)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;
	struct ice_mbx_snap_buffer_data *snap_buf;
	struct ice_ctl_q_info *cq = &hw->mailboxq;
	enum ice_mbx_snapshot_state new_state;
	bool is_malvf = false;
	int status = 0;

	if (!report_malvf || !mbx_data || !vf_info)
		return -EINVAL;

	*report_malvf = false;

	/* When entering the mailbox state machine assume that the VF
	 * is not malicious until detected.
	 */
	 /* Checking if max messages allowed to be processed while servicing current
	  * interrupt is not less than the defined AVF message threshold.
	  */
	if (mbx_data->max_num_msgs_mbx <= ICE_ASYNC_VF_MSG_THRESHOLD)
		return -EINVAL;

	/* The watermark value should not be lesser than the threshold limit
	 * set for the number of asynchronous messages a VF can send to mailbox
	 * nor should it be greater than the maximum number of messages in the
	 * mailbox serviced in current interrupt.
	 */
	if (mbx_data->async_watermark_val < ICE_ASYNC_VF_MSG_THRESHOLD ||
	    mbx_data->async_watermark_val > mbx_data->max_num_msgs_mbx)
		return -EINVAL;

	new_state = ICE_MAL_VF_DETECT_STATE_INVALID;
	snap_buf = &snap->mbx_buf;

	switch (snap_buf->state) {
	case ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT:
		/* Clear any previously held data in mailbox snapshot structure. */
		ice_mbx_reset_snapshot(snap);

		/* Collect the pending ARQ count, number of messages processed and
		 * the maximum number of messages allowed to be processed from the
		 * Mailbox for current interrupt.
		 */
		snap_buf->num_pending_arq = mbx_data->num_pending_arq;
		snap_buf->num_msg_proc = mbx_data->num_msg_proc;
		snap_buf->max_num_msgs_mbx = mbx_data->max_num_msgs_mbx;

		/* Capture a new static snapshot of the mailbox by logging the
		 * head and tail of snapshot and set num_iterations to the tail
		 * value to mark the start of the iteration through the snapshot.
		 */
		snap_buf->head = ICE_RQ_DATA_MASK(cq->rq.next_to_clean +
						  mbx_data->num_pending_arq);
		snap_buf->tail = ICE_RQ_DATA_MASK(cq->rq.next_to_clean - 1);
		snap_buf->num_iterations = snap_buf->tail;

		/* Pending ARQ messages returned by ice_clean_rq_elem
		 * is the difference between the head and tail of the
		 * mailbox queue. Comparing this value against the watermark
		 * helps to check if we potentially have malicious VFs.
		 */
		if (snap_buf->num_pending_arq >=
		    mbx_data->async_watermark_val) {
			new_state = ICE_MAL_VF_DETECT_STATE_DETECT;
			status = ice_mbx_detect_malvf(hw, vf_info, &new_state, &is_malvf);
		} else {
			new_state = ICE_MAL_VF_DETECT_STATE_TRAVERSE;
			ice_mbx_traverse(hw, &new_state);
		}
		break;

	case ICE_MAL_VF_DETECT_STATE_TRAVERSE:
		new_state = ICE_MAL_VF_DETECT_STATE_TRAVERSE;
		ice_mbx_traverse(hw, &new_state);
		break;

	case ICE_MAL_VF_DETECT_STATE_DETECT:
		new_state = ICE_MAL_VF_DETECT_STATE_DETECT;
		status = ice_mbx_detect_malvf(hw, vf_info, &new_state, &is_malvf);
		break;

	default:
		new_state = ICE_MAL_VF_DETECT_STATE_INVALID;
		status = -EIO;
	}

	snap_buf->state = new_state;

	/* Only report VFs as malicious the first time we detect it */
	if (is_malvf && !vf_info->malicious) {
		vf_info->malicious = 1;
		*report_malvf = true;
	}

	return status;
}

/**
 * ice_mbx_clear_malvf - Clear VF mailbox info
 * @vf_info: the mailbox tracking structure for a VF
 *
 * In case of a VF reset, this function shall be called to clear the VF's
 * current mailbox tracking state.
 */
void ice_mbx_clear_malvf(struct ice_mbx_vf_info *vf_info)
{
	vf_info->malicious = 0;
	vf_info->msg_count = 0;
}

/**
 * ice_mbx_init_vf_info - Initialize a new VF mailbox tracking info
 * @hw: pointer to the hardware structure
 * @vf_info: the mailbox tracking info structure for a VF
 *
 * Initialize a VF mailbox tracking info structure and insert it into the
 * snapshot list.
 *
 * If you remove the VF, you must also delete the associated VF info structure
 * from the linked list.
 */
void ice_mbx_init_vf_info(struct ice_hw *hw, struct ice_mbx_vf_info *vf_info)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;

	ice_mbx_clear_malvf(vf_info);
	list_add(&vf_info->list_entry, &snap->mbx_vf);
}

/**
 * ice_mbx_init_snapshot - Initialize mailbox snapshot data
 * @hw: pointer to the hardware structure
 *
 * Clear the mailbox snapshot structure and initialize the VF mailbox list.
 */
void ice_mbx_init_snapshot(struct ice_hw *hw)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;

	INIT_LIST_HEAD(&snap->mbx_vf);
	ice_mbx_reset_snapshot(snap);
}
