// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "otx_cpt_common.h"
#include "otx_cptpf.h"

static char *get_mbox_opcode_str(int msg_opcode)
{
	char *str = "Unknown";

	switch (msg_opcode) {
	case OTX_CPT_MSG_VF_UP:
		str = "UP";
		break;

	case OTX_CPT_MSG_VF_DOWN:
		str = "DOWN";
		break;

	case OTX_CPT_MSG_READY:
		str = "READY";
		break;

	case OTX_CPT_MSG_QLEN:
		str = "QLEN";
		break;

	case OTX_CPT_MSG_QBIND_GRP:
		str = "QBIND_GRP";
		break;

	case OTX_CPT_MSG_VQ_PRIORITY:
		str = "VQ_PRIORITY";
		break;

	case OTX_CPT_MSG_PF_TYPE:
		str = "PF_TYPE";
		break;

	case OTX_CPT_MSG_ACK:
		str = "ACK";
		break;

	case OTX_CPT_MSG_NACK:
		str = "NACK";
		break;
	}

	return str;
}

static void dump_mbox_msg(struct otx_cpt_mbox *mbox_msg, int vf_id)
{
	char raw_data_str[OTX_CPT_MAX_MBOX_DATA_STR_SIZE];

	hex_dump_to_buffer(mbox_msg, sizeof(struct otx_cpt_mbox), 16, 8,
			   raw_data_str, OTX_CPT_MAX_MBOX_DATA_STR_SIZE, false);
	if (vf_id >= 0)
		pr_debug("MBOX opcode %s received from VF%d raw_data %s",
			 get_mbox_opcode_str(mbox_msg->msg), vf_id,
			 raw_data_str);
	else
		pr_debug("MBOX opcode %s received from PF raw_data %s",
			 get_mbox_opcode_str(mbox_msg->msg), raw_data_str);
}

static void otx_cpt_send_msg_to_vf(struct otx_cpt_device *cpt, int vf,
				   struct otx_cpt_mbox *mbx)
{
	/* Writing mbox(0) causes interrupt */
	writeq(mbx->data, cpt->reg_base + OTX_CPT_PF_VFX_MBOXX(vf, 1));
	writeq(mbx->msg, cpt->reg_base + OTX_CPT_PF_VFX_MBOXX(vf, 0));
}

/*
 * ACKs VF's mailbox message
 * @vf: VF to which ACK to be sent
 */
static void otx_cpt_mbox_send_ack(struct otx_cpt_device *cpt, int vf,
			      struct otx_cpt_mbox *mbx)
{
	mbx->data = 0ull;
	mbx->msg = OTX_CPT_MSG_ACK;
	otx_cpt_send_msg_to_vf(cpt, vf, mbx);
}

/* NACKs VF's mailbox message that PF is not able to complete the action */
static void otx_cptpf_mbox_send_nack(struct otx_cpt_device *cpt, int vf,
				     struct otx_cpt_mbox *mbx)
{
	mbx->data = 0ull;
	mbx->msg = OTX_CPT_MSG_NACK;
	otx_cpt_send_msg_to_vf(cpt, vf, mbx);
}

static void otx_cpt_clear_mbox_intr(struct otx_cpt_device *cpt, u32 vf)
{
	/* W1C for the VF */
	writeq(1ull << vf, cpt->reg_base + OTX_CPT_PF_MBOX_INTX(0));
}

/*
 * Configure QLEN/Chunk sizes for VF
 */
static void otx_cpt_cfg_qlen_for_vf(struct otx_cpt_device *cpt, int vf,
				    u32 size)
{
	union otx_cptx_pf_qx_ctl pf_qx_ctl;

	pf_qx_ctl.u = readq(cpt->reg_base + OTX_CPT_PF_QX_CTL(vf));
	pf_qx_ctl.s.size = size;
	pf_qx_ctl.s.cont_err = true;
	writeq(pf_qx_ctl.u, cpt->reg_base + OTX_CPT_PF_QX_CTL(vf));
}

/*
 * Configure VQ priority
 */
static void otx_cpt_cfg_vq_priority(struct otx_cpt_device *cpt, int vf, u32 pri)
{
	union otx_cptx_pf_qx_ctl pf_qx_ctl;

	pf_qx_ctl.u = readq(cpt->reg_base + OTX_CPT_PF_QX_CTL(vf));
	pf_qx_ctl.s.pri = pri;
	writeq(pf_qx_ctl.u, cpt->reg_base + OTX_CPT_PF_QX_CTL(vf));
}

static int otx_cpt_bind_vq_to_grp(struct otx_cpt_device *cpt, u8 q, u8 grp)
{
	struct device *dev = &cpt->pdev->dev;
	struct otx_cpt_eng_grp_info *eng_grp;
	union otx_cptx_pf_qx_ctl pf_qx_ctl;
	struct otx_cpt_ucode *ucode;

	if (q >= cpt->max_vfs) {
		dev_err(dev, "Requested queue %d is > than maximum avail %d",
			q, cpt->max_vfs);
		return -EINVAL;
	}

	if (grp >= OTX_CPT_MAX_ENGINE_GROUPS) {
		dev_err(dev, "Requested group %d is > than maximum avail %d",
			grp, OTX_CPT_MAX_ENGINE_GROUPS);
		return -EINVAL;
	}

	eng_grp = &cpt->eng_grps.grp[grp];
	if (!eng_grp->is_enabled) {
		dev_err(dev, "Requested engine group %d is disabled", grp);
		return -EINVAL;
	}

	pf_qx_ctl.u = readq(cpt->reg_base + OTX_CPT_PF_QX_CTL(q));
	pf_qx_ctl.s.grp = grp;
	writeq(pf_qx_ctl.u, cpt->reg_base + OTX_CPT_PF_QX_CTL(q));

	if (eng_grp->mirror.is_ena)
		ucode = &eng_grp->g->grp[eng_grp->mirror.idx].ucode[0];
	else
		ucode = &eng_grp->ucode[0];

	if (otx_cpt_uc_supports_eng_type(ucode, OTX_CPT_SE_TYPES))
		return OTX_CPT_SE_TYPES;
	else if (otx_cpt_uc_supports_eng_type(ucode, OTX_CPT_AE_TYPES))
		return OTX_CPT_AE_TYPES;
	else
		return BAD_OTX_CPTVF_TYPE;
}

/* Interrupt handler to handle mailbox messages from VFs */
static void otx_cpt_handle_mbox_intr(struct otx_cpt_device *cpt, int vf)
{
	int vftype = 0;
	struct otx_cpt_mbox mbx = {};
	struct device *dev = &cpt->pdev->dev;
	/*
	 * MBOX[0] contains msg
	 * MBOX[1] contains data
	 */
	mbx.msg  = readq(cpt->reg_base + OTX_CPT_PF_VFX_MBOXX(vf, 0));
	mbx.data = readq(cpt->reg_base + OTX_CPT_PF_VFX_MBOXX(vf, 1));

	dump_mbox_msg(&mbx, vf);

	switch (mbx.msg) {
	case OTX_CPT_MSG_VF_UP:
		mbx.msg  = OTX_CPT_MSG_VF_UP;
		mbx.data = cpt->vfs_enabled;
		otx_cpt_send_msg_to_vf(cpt, vf, &mbx);
		break;
	case OTX_CPT_MSG_READY:
		mbx.msg  = OTX_CPT_MSG_READY;
		mbx.data = vf;
		otx_cpt_send_msg_to_vf(cpt, vf, &mbx);
		break;
	case OTX_CPT_MSG_VF_DOWN:
		/* First msg in VF teardown sequence */
		otx_cpt_mbox_send_ack(cpt, vf, &mbx);
		break;
	case OTX_CPT_MSG_QLEN:
		otx_cpt_cfg_qlen_for_vf(cpt, vf, mbx.data);
		otx_cpt_mbox_send_ack(cpt, vf, &mbx);
		break;
	case OTX_CPT_MSG_QBIND_GRP:
		vftype = otx_cpt_bind_vq_to_grp(cpt, vf, (u8)mbx.data);
		if ((vftype != OTX_CPT_AE_TYPES) &&
		    (vftype != OTX_CPT_SE_TYPES)) {
			dev_err(dev, "VF%d binding to eng group %llu failed",
				vf, mbx.data);
			otx_cptpf_mbox_send_nack(cpt, vf, &mbx);
		} else {
			mbx.msg = OTX_CPT_MSG_QBIND_GRP;
			mbx.data = vftype;
			otx_cpt_send_msg_to_vf(cpt, vf, &mbx);
		}
		break;
	case OTX_CPT_MSG_PF_TYPE:
		mbx.msg = OTX_CPT_MSG_PF_TYPE;
		mbx.data = cpt->pf_type;
		otx_cpt_send_msg_to_vf(cpt, vf, &mbx);
		break;
	case OTX_CPT_MSG_VQ_PRIORITY:
		otx_cpt_cfg_vq_priority(cpt, vf, mbx.data);
		otx_cpt_mbox_send_ack(cpt, vf, &mbx);
		break;
	default:
		dev_err(&cpt->pdev->dev, "Invalid msg from VF%d, msg 0x%llx\n",
			vf, mbx.msg);
		break;
	}
}

void otx_cpt_mbox_intr_handler (struct otx_cpt_device *cpt, int mbx)
{
	u64 intr;
	u8  vf;

	intr = readq(cpt->reg_base + OTX_CPT_PF_MBOX_INTX(0));
	pr_debug("PF interrupt mbox%d mask 0x%llx\n", mbx, intr);
	for (vf = 0; vf < cpt->max_vfs; vf++) {
		if (intr & (1ULL << vf)) {
			otx_cpt_handle_mbox_intr(cpt, vf);
			otx_cpt_clear_mbox_intr(cpt, vf);
		}
	}
}
