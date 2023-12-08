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
	u32 speed;

	if (adv_link_support)
		switch (link_speed) {
		case ICE_AQ_LINK_SPEED_10MB:
			speed = ICE_LINK_SPEED_10MBPS;
			break;
		case ICE_AQ_LINK_SPEED_100MB:
			speed = ICE_LINK_SPEED_100MBPS;
			break;
		case ICE_AQ_LINK_SPEED_1000MB:
			speed = ICE_LINK_SPEED_1000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_2500MB:
			speed = ICE_LINK_SPEED_2500MBPS;
			break;
		case ICE_AQ_LINK_SPEED_5GB:
			speed = ICE_LINK_SPEED_5000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_10GB:
			speed = ICE_LINK_SPEED_10000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_20GB:
			speed = ICE_LINK_SPEED_20000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_25GB:
			speed = ICE_LINK_SPEED_25000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_40GB:
			speed = ICE_LINK_SPEED_40000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_50GB:
			speed = ICE_LINK_SPEED_50000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_100GB:
			speed = ICE_LINK_SPEED_100000MBPS;
			break;
		default:
			speed = ICE_LINK_SPEED_UNKNOWN;
			break;
		}
	else
		/* Virtchnl speeds are not defined for every speed supported in
		 * the hardware. To maintain compatibility with older AVF
		 * drivers, while reporting the speed the new speed values are
		 * resolved to the closest known virtchnl speeds
		 */
		switch (link_speed) {
		case ICE_AQ_LINK_SPEED_10MB:
		case ICE_AQ_LINK_SPEED_100MB:
			speed = (u32)VIRTCHNL_LINK_SPEED_100MB;
			break;
		case ICE_AQ_LINK_SPEED_1000MB:
		case ICE_AQ_LINK_SPEED_2500MB:
		case ICE_AQ_LINK_SPEED_5GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_1GB;
			break;
		case ICE_AQ_LINK_SPEED_10GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_10GB;
			break;
		case ICE_AQ_LINK_SPEED_20GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_20GB;
			break;
		case ICE_AQ_LINK_SPEED_25GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_25GB;
			break;
		case ICE_AQ_LINK_SPEED_40GB:
		case ICE_AQ_LINK_SPEED_50GB:
		case ICE_AQ_LINK_SPEED_100GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_40GB;
			break;
		default:
			speed = (u32)VIRTCHNL_LINK_SPEED_UNKNOWN;
			break;
		}

	return speed;
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
 * the algorithm can be run for the first time for that interrupt. This can be
 * done via ice_mbx_reset_snapshot().
 *
 * 3. For every message read by the caller from the MBX Queue, the caller must
 * call the detection algorithm's entry function ice_mbx_vf_state_handler().
 * Before every call to ice_mbx_vf_state_handler() the struct ice_mbx_data is
 * filled as it is required to be passed to the algorithm.
 *
 * 4. Every time a message is read from the MBX queue, a VFId is received which
 * is passed to the state handler. The boolean output is_malvf of the state
 * handler ice_mbx_vf_state_handler() serves as an indicator to the caller
 * whether this VF is malicious or not.
 *
 * 5. When a VF is identified to be malicious, the caller can send a message
 * to the system administrator. The caller can invoke ice_mbx_report_malvf()
 * to help determine if a malicious VF is to be reported or not. This function
 * requires the caller to maintain a global bitmap to track all malicious VFs
 * and pass that to ice_mbx_report_malvf() along with the VFID which was identified
 * to be malicious by ice_mbx_vf_state_handler().
 *
 * 6. The global bitmap maintained by PF can be cleared completely if PF is in
 * reset or the bit corresponding to a VF can be cleared if that VF is in reset.
 * When a VF is shut down and brought back up, we assume that the new VF
 * brought up is not malicious and hence report it if found malicious.
 *
 * 7. The function ice_mbx_reset_snapshot() is called to reset the information
 * in ice_mbx_snapshot for every new mailbox interrupt handled.
 *
 * 8. The memory allocated for variables in ice_mbx_snapshot is de-allocated
 * when driver is unloaded.
 */
#define ICE_RQ_DATA_MASK(rq_data) ((rq_data) & PF_MBX_ARQH_ARQH_M)
/* Using the highest value for an unsigned 16-bit value 0xFFFF to indicate that
 * the max messages check must be ignored in the algorithm
 */
#define ICE_IGNORE_MAX_MSG_CNT	0xFFFF

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
 * @vf_id: relative virtual function ID
 * @new_state: new algorithm state
 * @is_malvf: boolean output to indicate if VF is malicious
 *
 * This function tracks the number of asynchronous messages
 * sent per VF and marks the VF as malicious if it exceeds
 * the permissible number of messages to send.
 */
static int
ice_mbx_detect_malvf(struct ice_hw *hw, u16 vf_id,
		     enum ice_mbx_snapshot_state *new_state,
		     bool *is_malvf)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;

	if (vf_id >= snap->mbx_vf.vfcntr_len)
		return -EIO;

	/* increment the message count in the VF array */
	snap->mbx_vf.vf_cntr[vf_id]++;

	if (snap->mbx_vf.vf_cntr[vf_id] >= ICE_ASYNC_VF_MSG_THRESHOLD)
		*is_malvf = true;

	/* continue to iterate through the mailbox snapshot */
	ice_mbx_traverse(hw, new_state);

	return 0;
}

/**
 * ice_mbx_reset_snapshot - Reset mailbox snapshot structure
 * @snap: pointer to mailbox snapshot structure in the ice_hw struct
 *
 * Reset the mailbox snapshot structure and clear VF counter array.
 */
static void ice_mbx_reset_snapshot(struct ice_mbx_snapshot *snap)
{
	u32 vfcntr_len;

	if (!snap || !snap->mbx_vf.vf_cntr)
		return;

	/* Clear VF counters. */
	vfcntr_len = snap->mbx_vf.vfcntr_len;
	if (vfcntr_len)
		memset(snap->mbx_vf.vf_cntr, 0,
		       (vfcntr_len * sizeof(*snap->mbx_vf.vf_cntr)));

	/* Reset mailbox snapshot for a new capture. */
	memset(&snap->mbx_buf, 0, sizeof(snap->mbx_buf));
	snap->mbx_buf.state = ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT;
}

/**
 * ice_mbx_vf_state_handler - Handle states of the overflow algorithm
 * @hw: pointer to the HW struct
 * @mbx_data: pointer to structure containing mailbox data
 * @vf_id: relative virtual function (VF) ID
 * @is_malvf: boolean output to indicate if VF is malicious
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
ice_mbx_vf_state_handler(struct ice_hw *hw,
			 struct ice_mbx_data *mbx_data, u16 vf_id,
			 bool *is_malvf)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;
	struct ice_mbx_snap_buffer_data *snap_buf;
	struct ice_ctl_q_info *cq = &hw->mailboxq;
	enum ice_mbx_snapshot_state new_state;
	int status = 0;

	if (!is_malvf || !mbx_data)
		return -EINVAL;

	/* When entering the mailbox state machine assume that the VF
	 * is not malicious until detected.
	 */
	*is_malvf = false;

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
			status = ice_mbx_detect_malvf(hw, vf_id, &new_state, is_malvf);
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
		status = ice_mbx_detect_malvf(hw, vf_id, &new_state, is_malvf);
		break;

	default:
		new_state = ICE_MAL_VF_DETECT_STATE_INVALID;
		status = -EIO;
	}

	snap_buf->state = new_state;

	return status;
}

/**
 * ice_mbx_report_malvf - Track and note malicious VF
 * @hw: pointer to the HW struct
 * @all_malvfs: all malicious VFs tracked by PF
 * @bitmap_len: length of bitmap in bits
 * @vf_id: relative virtual function ID of the malicious VF
 * @report_malvf: boolean to indicate if malicious VF must be reported
 *
 * This function will update a bitmap that keeps track of the malicious
 * VFs attached to the PF. A malicious VF must be reported only once if
 * discovered between VF resets or loading so the function checks
 * the input vf_id against the bitmap to verify if the VF has been
 * detected in any previous mailbox iterations.
 */
int
ice_mbx_report_malvf(struct ice_hw *hw, unsigned long *all_malvfs,
		     u16 bitmap_len, u16 vf_id, bool *report_malvf)
{
	if (!all_malvfs || !report_malvf)
		return -EINVAL;

	*report_malvf = false;

	if (bitmap_len < hw->mbx_snapshot.mbx_vf.vfcntr_len)
		return -EINVAL;

	if (vf_id >= bitmap_len)
		return -EIO;

	/* If the vf_id is found in the bitmap set bit and boolean to true */
	if (!test_and_set_bit(vf_id, all_malvfs))
		*report_malvf = true;

	return 0;
}

/**
 * ice_mbx_clear_malvf - Clear VF bitmap and counter for VF ID
 * @snap: pointer to the mailbox snapshot structure
 * @all_malvfs: all malicious VFs tracked by PF
 * @bitmap_len: length of bitmap in bits
 * @vf_id: relative virtual function ID of the malicious VF
 *
 * In case of a VF reset, this function can be called to clear
 * the bit corresponding to the VF ID in the bitmap tracking all
 * malicious VFs attached to the PF. The function also clears the
 * VF counter array at the index of the VF ID. This is to ensure
 * that the new VF loaded is not considered malicious before going
 * through the overflow detection algorithm.
 */
int
ice_mbx_clear_malvf(struct ice_mbx_snapshot *snap, unsigned long *all_malvfs,
		    u16 bitmap_len, u16 vf_id)
{
	if (!snap || !all_malvfs)
		return -EINVAL;

	if (bitmap_len < snap->mbx_vf.vfcntr_len)
		return -EINVAL;

	/* Ensure VF ID value is not larger than bitmap or VF counter length */
	if (vf_id >= bitmap_len || vf_id >= snap->mbx_vf.vfcntr_len)
		return -EIO;

	/* Clear VF ID bit in the bitmap tracking malicious VFs attached to PF */
	clear_bit(vf_id, all_malvfs);

	/* Clear the VF counter in the mailbox snapshot structure for that VF ID.
	 * This is to ensure that if a VF is unloaded and a new one brought back
	 * up with the same VF ID for a snapshot currently in traversal or detect
	 * state the counter for that VF ID does not increment on top of existing
	 * values in the mailbox overflow detection algorithm.
	 */
	snap->mbx_vf.vf_cntr[vf_id] = 0;

	return 0;
}

/**
 * ice_mbx_init_snapshot - Initialize mailbox snapshot structure
 * @hw: pointer to the hardware structure
 * @vf_count: number of VFs allocated on a PF
 *
 * Clear the mailbox snapshot structure and allocate memory
 * for the VF counter array based on the number of VFs allocated
 * on that PF.
 *
 * Assumption: This function will assume ice_get_caps() has already been
 * called to ensure that the vf_count can be compared against the number
 * of VFs supported as defined in the functional capabilities of the device.
 */
int ice_mbx_init_snapshot(struct ice_hw *hw, u16 vf_count)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;

	/* Ensure that the number of VFs allocated is non-zero and
	 * is not greater than the number of supported VFs defined in
	 * the functional capabilities of the PF.
	 */
	if (!vf_count || vf_count > hw->func_caps.num_allocd_vfs)
		return -EINVAL;

	snap->mbx_vf.vf_cntr = devm_kcalloc(ice_hw_to_dev(hw), vf_count,
					    sizeof(*snap->mbx_vf.vf_cntr),
					    GFP_KERNEL);
	if (!snap->mbx_vf.vf_cntr)
		return -ENOMEM;

	/* Setting the VF counter length to the number of allocated
	 * VFs for given PF's functional capabilities.
	 */
	snap->mbx_vf.vfcntr_len = vf_count;

	/* Clear mbx_buf in the mailbox snaphot structure and setting the
	 * mailbox snapshot state to a new capture.
	 */
	memset(&snap->mbx_buf, 0, sizeof(snap->mbx_buf));
	snap->mbx_buf.state = ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT;

	return 0;
}

/**
 * ice_mbx_deinit_snapshot - Free mailbox snapshot structure
 * @hw: pointer to the hardware structure
 *
 * Clear the mailbox snapshot structure and free the VF counter array.
 */
void ice_mbx_deinit_snapshot(struct ice_hw *hw)
{
	struct ice_mbx_snapshot *snap = &hw->mbx_snapshot;

	/* Free VF counter array and reset VF counter length */
	devm_kfree(ice_hw_to_dev(hw), snap->mbx_vf.vf_cntr);
	snap->mbx_vf.vfcntr_len = 0;

	/* Clear mbx_buf in the mailbox snaphot structure */
	memset(&snap->mbx_buf, 0, sizeof(snap->mbx_buf));
}
