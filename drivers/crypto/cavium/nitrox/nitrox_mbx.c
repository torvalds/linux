// SPDX-License-Identifier: GPL-2.0
#include <linux/bitmap.h>
#include <linux/workqueue.h>

#include "nitrox_csr.h"
#include "nitrox_hal.h"
#include "nitrox_dev.h"
#include "nitrox_mbx.h"

#define RING_TO_VFNO(_x, _y)	((_x) / (_y))

/*
 * mbx_msg_type - Mailbox message types
 */
enum mbx_msg_type {
	MBX_MSG_TYPE_NOP,
	MBX_MSG_TYPE_REQ,
	MBX_MSG_TYPE_ACK,
	MBX_MSG_TYPE_NACK,
};

/*
 * mbx_msg_opcode - Mailbox message opcodes
 */
enum mbx_msg_opcode {
	MSG_OP_VF_MODE = 1,
	MSG_OP_VF_UP,
	MSG_OP_VF_DOWN,
	MSG_OP_CHIPID_VFID,
	MSG_OP_MCODE_INFO = 11,
};

struct pf2vf_work {
	struct nitrox_vfdev *vfdev;
	struct nitrox_device *ndev;
	struct work_struct pf2vf_resp;
};

static inline u64 pf2vf_read_mbox(struct nitrox_device *ndev, int ring)
{
	u64 reg_addr;

	reg_addr = NPS_PKT_MBOX_VF_PF_PFDATAX(ring);
	return nitrox_read_csr(ndev, reg_addr);
}

static inline void pf2vf_write_mbox(struct nitrox_device *ndev, u64 value,
				    int ring)
{
	u64 reg_addr;

	reg_addr = NPS_PKT_MBOX_PF_VF_PFDATAX(ring);
	nitrox_write_csr(ndev, reg_addr, value);
}

static void pf2vf_send_response(struct nitrox_device *ndev,
				struct nitrox_vfdev *vfdev)
{
	union mbox_msg msg;

	msg.value = vfdev->msg.value;

	switch (vfdev->msg.opcode) {
	case MSG_OP_VF_MODE:
		msg.data = ndev->mode;
		break;
	case MSG_OP_VF_UP:
		vfdev->nr_queues = vfdev->msg.data;
		atomic_set(&vfdev->state, __NDEV_READY);
		break;
	case MSG_OP_CHIPID_VFID:
		msg.id.chipid = ndev->idx;
		msg.id.vfid = vfdev->vfno;
		break;
	case MSG_OP_VF_DOWN:
		vfdev->nr_queues = 0;
		atomic_set(&vfdev->state, __NDEV_NOT_READY);
		break;
	case MSG_OP_MCODE_INFO:
		msg.data = 0;
		msg.mcode_info.count = 2;
		msg.mcode_info.info = MCODE_TYPE_SE_SSL | (MCODE_TYPE_AE << 5);
		msg.mcode_info.next_se_grp = 1;
		msg.mcode_info.next_ae_grp = 1;
		break;
	default:
		msg.type = MBX_MSG_TYPE_NOP;
		break;
	}

	if (msg.type == MBX_MSG_TYPE_NOP)
		return;

	/* send ACK to VF */
	msg.type = MBX_MSG_TYPE_ACK;
	pf2vf_write_mbox(ndev, msg.value, vfdev->ring);

	vfdev->msg.value = 0;
	atomic64_inc(&vfdev->mbx_resp);
}

static void pf2vf_resp_handler(struct work_struct *work)
{
	struct pf2vf_work *pf2vf_resp = container_of(work, struct pf2vf_work,
						     pf2vf_resp);
	struct nitrox_vfdev *vfdev = pf2vf_resp->vfdev;
	struct nitrox_device *ndev = pf2vf_resp->ndev;

	switch (vfdev->msg.type) {
	case MBX_MSG_TYPE_REQ:
		/* process the request from VF */
		pf2vf_send_response(ndev, vfdev);
		break;
	case MBX_MSG_TYPE_ACK:
	case MBX_MSG_TYPE_NACK:
		break;
	}

	kfree(pf2vf_resp);
}

void nitrox_pf2vf_mbox_handler(struct nitrox_device *ndev)
{
	DECLARE_BITMAP(csr, BITS_PER_TYPE(u64));
	struct nitrox_vfdev *vfdev;
	struct pf2vf_work *pfwork;
	u64 value, reg_addr;
	u32 i;
	int vfno;

	/* loop for VF(0..63) */
	reg_addr = NPS_PKT_MBOX_INT_LO;
	value = nitrox_read_csr(ndev, reg_addr);
	bitmap_from_u64(csr, value);
	for_each_set_bit(i, csr, BITS_PER_TYPE(csr)) {
		/* get the vfno from ring */
		vfno = RING_TO_VFNO(i, ndev->iov.max_vf_queues);
		vfdev = ndev->iov.vfdev + vfno;
		vfdev->ring = i;
		/* fill the vf mailbox data */
		vfdev->msg.value = pf2vf_read_mbox(ndev, vfdev->ring);
		pfwork = kzalloc(sizeof(*pfwork), GFP_ATOMIC);
		if (!pfwork)
			continue;

		pfwork->vfdev = vfdev;
		pfwork->ndev = ndev;
		INIT_WORK(&pfwork->pf2vf_resp, pf2vf_resp_handler);
		queue_work(ndev->iov.pf2vf_wq, &pfwork->pf2vf_resp);
		/* clear the corresponding vf bit */
		nitrox_write_csr(ndev, reg_addr, BIT_ULL(i));
	}

	/* loop for VF(64..127) */
	reg_addr = NPS_PKT_MBOX_INT_HI;
	value = nitrox_read_csr(ndev, reg_addr);
	bitmap_from_u64(csr, value);
	for_each_set_bit(i, csr, BITS_PER_TYPE(csr)) {
		/* get the vfno from ring */
		vfno = RING_TO_VFNO(i + 64, ndev->iov.max_vf_queues);
		vfdev = ndev->iov.vfdev + vfno;
		vfdev->ring = (i + 64);
		/* fill the vf mailbox data */
		vfdev->msg.value = pf2vf_read_mbox(ndev, vfdev->ring);

		pfwork = kzalloc(sizeof(*pfwork), GFP_ATOMIC);
		if (!pfwork)
			continue;

		pfwork->vfdev = vfdev;
		pfwork->ndev = ndev;
		INIT_WORK(&pfwork->pf2vf_resp, pf2vf_resp_handler);
		queue_work(ndev->iov.pf2vf_wq, &pfwork->pf2vf_resp);
		/* clear the corresponding vf bit */
		nitrox_write_csr(ndev, reg_addr, BIT_ULL(i));
	}
}

int nitrox_mbox_init(struct nitrox_device *ndev)
{
	struct nitrox_vfdev *vfdev;
	int i;

	ndev->iov.vfdev = kcalloc(ndev->iov.num_vfs,
				  sizeof(struct nitrox_vfdev), GFP_KERNEL);
	if (!ndev->iov.vfdev)
		return -ENOMEM;

	for (i = 0; i < ndev->iov.num_vfs; i++) {
		vfdev = ndev->iov.vfdev + i;
		vfdev->vfno = i;
	}

	/* allocate pf2vf response workqueue */
	ndev->iov.pf2vf_wq = alloc_workqueue("nitrox_pf2vf", 0, 0);
	if (!ndev->iov.pf2vf_wq) {
		kfree(ndev->iov.vfdev);
		ndev->iov.vfdev = NULL;
		return -ENOMEM;
	}
	/* enable pf2vf mailbox interrupts */
	enable_pf2vf_mbox_interrupts(ndev);

	return 0;
}

void nitrox_mbox_cleanup(struct nitrox_device *ndev)
{
	/* disable pf2vf mailbox interrupts */
	disable_pf2vf_mbox_interrupts(ndev);
	/* destroy workqueue */
	if (ndev->iov.pf2vf_wq)
		destroy_workqueue(ndev->iov.pf2vf_wq);

	kfree(ndev->iov.vfdev);
	ndev->iov.pf2vf_wq = NULL;
	ndev->iov.vfdev = NULL;
}
