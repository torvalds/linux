// SPDX-License-Identifier: GPL-2.0-only
/*
 * SolidRun DPU driver for control plane
 *
 * Copyright (C) 2022-2023 SolidRun
 *
 * Author: Alvaro Karsz <alvaro.karsz@solid-run.com>
 *
 */

#include <linux/iopoll.h>

#include "snet_vdpa.h"

enum snet_ctrl_opcodes {
	SNET_CTRL_OP_DESTROY = 1,
	SNET_CTRL_OP_READ_VQ_STATE,
	SNET_CTRL_OP_SUSPEND,
	SNET_CTRL_OP_RESUME,
};

#define SNET_CTRL_TIMEOUT	        2000000

#define SNET_CTRL_DATA_SIZE_MASK	0x0000FFFF
#define SNET_CTRL_IN_PROCESS_MASK	0x00010000
#define SNET_CTRL_CHUNK_RDY_MASK	0x00020000
#define SNET_CTRL_ERROR_MASK		0x0FFC0000

#define SNET_VAL_TO_ERR(val)		(-(((val) & SNET_CTRL_ERROR_MASK) >> 18))
#define SNET_EMPTY_CTRL(val)		(((val) & SNET_CTRL_ERROR_MASK) || \
						!((val) & SNET_CTRL_IN_PROCESS_MASK))
#define SNET_DATA_READY(val)		((val) & (SNET_CTRL_ERROR_MASK | SNET_CTRL_CHUNK_RDY_MASK))

/* Control register used to read data from the DPU */
struct snet_ctrl_reg_ctrl {
	/* Chunk size in 4B words */
	u16 data_size;
	/* We are in the middle of a command */
	u16 in_process:1;
	/* A data chunk is ready and can be consumed */
	u16 chunk_ready:1;
	/* Error code */
	u16 error:10;
	/* Saved for future usage */
	u16 rsvd:4;
};

/* Opcode register */
struct snet_ctrl_reg_op {
	u16 opcode;
	/* Only if VQ index is relevant for the command */
	u16 vq_idx;
};

struct snet_ctrl_regs {
	struct snet_ctrl_reg_op op;
	struct snet_ctrl_reg_ctrl ctrl;
	u32 rsvd;
	u32 data[];
};

static struct snet_ctrl_regs __iomem *snet_get_ctrl(struct snet *snet)
{
	return snet->bar + snet->psnet->cfg.ctrl_off;
}

static int snet_wait_for_empty_ctrl(struct snet_ctrl_regs __iomem *regs)
{
	u32 val;

	return readx_poll_timeout(ioread32, &regs->ctrl, val, SNET_EMPTY_CTRL(val), 10,
				  SNET_CTRL_TIMEOUT);
}

static int snet_wait_for_empty_op(struct snet_ctrl_regs __iomem *regs)
{
	u32 val;

	return readx_poll_timeout(ioread32, &regs->op, val, !val, 10, SNET_CTRL_TIMEOUT);
}

static int snet_wait_for_data(struct snet_ctrl_regs __iomem *regs)
{
	u32 val;

	return readx_poll_timeout(ioread32, &regs->ctrl, val, SNET_DATA_READY(val), 10,
				  SNET_CTRL_TIMEOUT);
}

static u32 snet_read32_word(struct snet_ctrl_regs __iomem *ctrl_regs, u16 word_idx)
{
	return ioread32(&ctrl_regs->data[word_idx]);
}

static u32 snet_read_ctrl(struct snet_ctrl_regs __iomem *ctrl_regs)
{
	return ioread32(&ctrl_regs->ctrl);
}

static void snet_write_ctrl(struct snet_ctrl_regs __iomem *ctrl_regs, u32 val)
{
	iowrite32(val, &ctrl_regs->ctrl);
}

static void snet_write_op(struct snet_ctrl_regs __iomem *ctrl_regs, u32 val)
{
	iowrite32(val, &ctrl_regs->op);
}

static int snet_wait_for_dpu_completion(struct snet_ctrl_regs __iomem *ctrl_regs)
{
	/* Wait until the DPU finishes completely.
	 * It will clear the opcode register.
	 */
	return snet_wait_for_empty_op(ctrl_regs);
}

/* Reading ctrl from the DPU:
 * buf_size must be 4B aligned
 *
 * Steps:
 *
 * (1) Verify that the DPU is not in the middle of another operation by
 *     reading the in_process and error bits in the control register.
 * (2) Write the request opcode and the VQ idx in the opcode register
 *     and write the buffer size in the control register.
 * (3) Start readind chunks of data, chunk_ready bit indicates that a
 *     data chunk is available, we signal that we read the data by clearing the bit.
 * (4) Detect that the transfer is completed when the in_process bit
 *     in the control register is cleared or when the an error appears.
 */
static int snet_ctrl_read_from_dpu(struct snet *snet, u16 opcode, u16 vq_idx, void *buffer,
				   u32 buf_size)
{
	struct pci_dev *pdev = snet->pdev;
	struct snet_ctrl_regs __iomem *regs = snet_get_ctrl(snet);
	u32 *bfr_ptr = (u32 *)buffer;
	u32 val;
	u16 buf_words;
	int ret;
	u16 words, i, tot_words = 0;

	/* Supported for config 2+ */
	if (!SNET_CFG_VER(snet, 2))
		return -EOPNOTSUPP;

	if (!IS_ALIGNED(buf_size, 4))
		return -EINVAL;

	mutex_lock(&snet->ctrl_lock);

	buf_words = buf_size / 4;

	/* Make sure control register is empty */
	ret = snet_wait_for_empty_ctrl(regs);
	if (ret) {
		SNET_WARN(pdev, "Timeout waiting for previous control data to be consumed\n");
		goto exit;
	}

	/* We need to write the buffer size in the control register, and the opcode + vq index in
	 * the opcode register.
	 * We use a spinlock to serialize the writes.
	 */
	spin_lock(&snet->ctrl_spinlock);

	snet_write_ctrl(regs, buf_words);
	snet_write_op(regs, opcode | (vq_idx << 16));

	spin_unlock(&snet->ctrl_spinlock);

	while (buf_words != tot_words) {
		ret = snet_wait_for_data(regs);
		if (ret) {
			SNET_WARN(pdev, "Timeout waiting for control data\n");
			goto exit;
		}

		val = snet_read_ctrl(regs);

		/* Error? */
		if (val & SNET_CTRL_ERROR_MASK) {
			ret = SNET_VAL_TO_ERR(val);
			SNET_WARN(pdev, "Error while reading control data from DPU, err %d\n", ret);
			goto exit;
		}

		words = min_t(u16, val & SNET_CTRL_DATA_SIZE_MASK, buf_words - tot_words);

		for (i = 0; i < words; i++) {
			*bfr_ptr = snet_read32_word(regs, i);
			bfr_ptr++;
		}

		tot_words += words;

		/* Is the job completed? */
		if (!(val & SNET_CTRL_IN_PROCESS_MASK))
			break;

		/* Clear the chunk ready bit and continue */
		val &= ~SNET_CTRL_CHUNK_RDY_MASK;
		snet_write_ctrl(regs, val);
	}

	ret = snet_wait_for_dpu_completion(regs);
	if (ret)
		SNET_WARN(pdev, "Timeout waiting for the DPU to complete a control command\n");

exit:
	mutex_unlock(&snet->ctrl_lock);
	return ret;
}

/* Send a control message to the DPU using the old mechanism
 * used with config version 1.
 */
static int snet_send_ctrl_msg_old(struct snet *snet, u32 opcode)
{
	struct pci_dev *pdev = snet->pdev;
	struct snet_ctrl_regs __iomem *regs = snet_get_ctrl(snet);
	int ret;

	mutex_lock(&snet->ctrl_lock);

	/* Old mechanism uses just 1 register, the opcode register.
	 * Make sure that the opcode register is empty, and that the DPU isn't
	 * processing an old message.
	 */
	ret = snet_wait_for_empty_op(regs);
	if (ret) {
		SNET_WARN(pdev, "Timeout waiting for previous control message to be ACKed\n");
		goto exit;
	}

	/* Write the message */
	snet_write_op(regs, opcode);

	/* DPU ACKs the message by clearing the opcode register */
	ret = snet_wait_for_empty_op(regs);
	if (ret)
		SNET_WARN(pdev, "Timeout waiting for a control message to be ACKed\n");

exit:
	mutex_unlock(&snet->ctrl_lock);
	return ret;
}

/* Send a control message to the DPU.
 * A control message is a message without payload.
 */
static int snet_send_ctrl_msg(struct snet *snet, u16 opcode, u16 vq_idx)
{
	struct pci_dev *pdev = snet->pdev;
	struct snet_ctrl_regs __iomem *regs = snet_get_ctrl(snet);
	u32 val;
	int ret;

	/* If config version is not 2+, use the old mechanism */
	if (!SNET_CFG_VER(snet, 2))
		return snet_send_ctrl_msg_old(snet, opcode);

	mutex_lock(&snet->ctrl_lock);

	/* Make sure control register is empty */
	ret = snet_wait_for_empty_ctrl(regs);
	if (ret) {
		SNET_WARN(pdev, "Timeout waiting for previous control data to be consumed\n");
		goto exit;
	}

	/* We need to clear the control register and write the opcode + vq index in the opcode
	 * register.
	 * We use a spinlock to serialize the writes.
	 */
	spin_lock(&snet->ctrl_spinlock);

	snet_write_ctrl(regs, 0);
	snet_write_op(regs, opcode | (vq_idx << 16));

	spin_unlock(&snet->ctrl_spinlock);

	/* The DPU ACKs control messages by setting the chunk ready bit
	 * without data.
	 */
	ret = snet_wait_for_data(regs);
	if (ret) {
		SNET_WARN(pdev, "Timeout waiting for control message to be ACKed\n");
		goto exit;
	}

	/* Check for errors */
	val = snet_read_ctrl(regs);
	ret = SNET_VAL_TO_ERR(val);

	/* Clear the chunk ready bit */
	val &= ~SNET_CTRL_CHUNK_RDY_MASK;
	snet_write_ctrl(regs, val);

	ret = snet_wait_for_dpu_completion(regs);
	if (ret)
		SNET_WARN(pdev, "Timeout waiting for DPU to complete a control command, err %d\n",
			  ret);

exit:
	mutex_unlock(&snet->ctrl_lock);
	return ret;
}

void snet_ctrl_clear(struct snet *snet)
{
	struct snet_ctrl_regs __iomem *regs = snet_get_ctrl(snet);

	snet_write_op(regs, 0);
}

int snet_destroy_dev(struct snet *snet)
{
	return snet_send_ctrl_msg(snet, SNET_CTRL_OP_DESTROY, 0);
}

int snet_read_vq_state(struct snet *snet, u16 idx, struct vdpa_vq_state *state)
{
	return snet_ctrl_read_from_dpu(snet, SNET_CTRL_OP_READ_VQ_STATE, idx, state,
				       sizeof(*state));
}

int snet_suspend_dev(struct snet *snet)
{
	return snet_send_ctrl_msg(snet, SNET_CTRL_OP_SUSPEND, 0);
}

int snet_resume_dev(struct snet *snet)
{
	return snet_send_ctrl_msg(snet, SNET_CTRL_OP_RESUME, 0);
}
