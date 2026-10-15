// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024-2026 Arm Ltd.

#include <linux/arm_mpam.h>
#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <acpi/pcc.h>
#include <asm/mpam.h>

#include "mpam_internal.h"

#define MPAM_MSC_TOKEN_MASK		GENMASK_U32(27, 18)
#define MPAM_MSC_PROT_ID_MASK		GENMASK_U32(17, 10)

#define MPAM_FB_PROTOCOL_ID		0x1a

#define MPAM_PROTOCOL_VERSION_CMD	0x0
#define MPAM_FB_VERSION_MAJOR_MASK		GENMASK_U32(31, 16)
#define MPAM_FB_VERSION_MINOR_MASK		GENMASK_U32(15, 0)

#define MPAM_MSC_READ_CMD		0x4
#define MPAM_MSC_WRITE_CMD		0x5

#define MPAM_FB_PROT_HEADER_LEN		sizeof(u32)
/* The longest message is MPAM_MSC_WRITE, with 4 parameters. */
#define MPAM_FB_MAX_MSG_SIZE		(4 * sizeof(u32))

#define MPAM_FB_SUCCESS		 0
#define MPAM_FB_ERR_NOT_SUPPORTED	-1
#define MPAM_FB_ERR_INVALID_PARAMETERS	-2
#define MPAM_FB_ERR_DENIED		-3
#define MPAM_FB_ERR_NOT_FOUND		-4
#define MPAM_FB_ERR_OUT_OF_RANGE	-5
#define MPAM_FB_ERR_BUSY		-6
#define MPAM_FB_ERR_COMMS_ERROR		-7
#define MPAM_FB_ERR_GENERIC_ERROR	-8
#define MPAM_FB_ERR_HW_ERROR		-9
#define MPAM_FB_ERR_PROTOCOL_ERROR	-10
#define MPAM_FB_ERR_IN_USE		-11

static atomic_t mpam_fb_token = ATOMIC_INIT(0);

static int mpam_fb_to_linux_errno(int mpam_fb_code)
{
	switch (mpam_fb_code) {
	case MPAM_FB_ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case MPAM_FB_ERR_INVALID_PARAMETERS:
		return -EINVAL;
	case MPAM_FB_ERR_NOT_FOUND:
		return -ENOENT;
	case MPAM_FB_ERR_OUT_OF_RANGE:
		return -ERANGE;
	case MPAM_FB_ERR_BUSY:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static void mpam_fb_build_version_message(unsigned int token,
					  void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory __iomem *pcc_shmem = msg_buf;

	/* .signature is filled by the platform */
	writel_relaxed(PCC_CMD_COMPLETION_NOTIFY, &pcc_shmem->flags);
	writel_relaxed(MPAM_FB_PROT_HEADER_LEN, &pcc_shmem->length);
	writel_relaxed(MPAM_PROTOCOL_VERSION_CMD |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);
}

static void mpam_fb_build_read_message(int msc_fb_id, int reg,
				       unsigned int token,
				       void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory __iomem *pcc_shmem = msg_buf;
	struct mpam_fb_read_payload {
		u32 msc_fb_id;
		u32 flags;
		u32 reg_offset;
	} __packed __iomem *payload = msg_buf + sizeof(*pcc_shmem);
	int msg_size = MPAM_FB_PROT_HEADER_LEN + sizeof(*payload);

	/* .signature is filled by the platform */
	writel_relaxed(PCC_CMD_COMPLETION_NOTIFY, &pcc_shmem->flags);
	writel_relaxed(msg_size, &pcc_shmem->length);
	writel_relaxed(MPAM_MSC_READ_CMD |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);

	writel_relaxed(msc_fb_id, &payload->msc_fb_id);
	writel_relaxed(0, &payload->flags);
	writel_relaxed(reg, &payload->reg_offset);
}

static void mpam_fb_build_write_message(int msc_fb_id, int reg, u32 val,
					unsigned int token,
					void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory __iomem *pcc_shmem = msg_buf;
	struct mpam_fb_write_payload {
		u32 msc_fb_id;
		u32 flags;
		u32 reg_offset;
		u32 value;
	} __packed __iomem *payload = msg_buf + sizeof(*pcc_shmem);
	int msg_size = MPAM_FB_PROT_HEADER_LEN + sizeof(*payload);

	/* .signature is filled by the platform */
	writel_relaxed(PCC_CMD_COMPLETION_NOTIFY, &pcc_shmem->flags);
	writel_relaxed(msg_size, &pcc_shmem->length);
	writel_relaxed(MPAM_MSC_WRITE_CMD |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);

	writel_relaxed(msc_fb_id, &payload->msc_fb_id);
	writel_relaxed(0, &payload->flags);
	writel_relaxed(reg, &payload->reg_offset);
	writel_relaxed(val, &payload->value);
}

static int mpam_fb_send_request(struct mpam_msc *msc, u32 msc_fb_id,
				u16 reg, u32 *result, int mpam_fb_command)
{
	unsigned int token = atomic_inc_return(&mpam_fb_token);
	struct acpi_pcct_ext_pcc_shared_memory __iomem *pcc_shmem;
	struct mpam_pcc_chan *pcc_chan;
	struct pcc_mbox_chan *chan;
	void __iomem *payload_ofs;
	int mpam_fb_err = 0;
	u32 status;
	int ret;

	pcc_chan = msc->pcc_chan;
	if (!pcc_chan)
		return -ENODEV;

	chan = pcc_chan->pcc_chan;

	/* prune token to fit into the 10 bits inside the command register */
	token = FIELD_GET(MPAM_MSC_TOKEN_MASK,
			  FIELD_PREP(MPAM_MSC_TOKEN_MASK, token));

	mutex_lock(&pcc_chan->pcc_chan_lock);

	switch (mpam_fb_command) {
	case MPAM_PROTOCOL_VERSION_CMD:
		mpam_fb_build_version_message(token, chan->shmem);
		break;
	case MPAM_MSC_READ_CMD:
		mpam_fb_build_read_message(msc_fb_id, reg, token, chan->shmem);
		break;
	case MPAM_MSC_WRITE_CMD:
		mpam_fb_build_write_message(msc_fb_id, reg, *result,
					    token, chan->shmem);
		break;
	default:
		dev_err(&msc->pdev->dev, "unsupported MPAM-Fb command %d\n",
			mpam_fb_command);
		ret = -EINVAL;
		goto out_err;
	}

	dma_wmb();
	ret = mbox_send_message(chan->mchan, NULL);
	if (ret < 0)
		goto out_err;

	pcc_shmem = chan->shmem;
	payload_ofs = chan->shmem + sizeof(*pcc_shmem);
	status = readl(&pcc_shmem->command);
	if (FIELD_GET(MPAM_MSC_TOKEN_MASK, status) != token) {
		ret = -ETIMEDOUT;
		goto out_err;
	}

	mpam_fb_err = readl(payload_ofs + 0x0);
	if (mpam_fb_err < 0) {
		ret = mpam_fb_to_linux_errno(mpam_fb_err);
		goto out_err;
	}

	if (mpam_fb_command != MPAM_MSC_WRITE_CMD)
		*result = readl(payload_ofs + 0x4);

	mutex_unlock(&pcc_chan->pcc_chan_lock);

	return 0;

out_err:
	mutex_unlock(&pcc_chan->pcc_chan_lock);

	mpam_fb_disable_mpam(ret, mpam_fb_err);

	/*
	 * TODO: Error codes are filtered in higher layers, return something
	 * that is known there for now, to actually report them.
	 */
	return -EIO;
}

int mpam_fb_send_read_request(struct mpam_msc *msc, u16 reg, u32 *result)
{
	return mpam_fb_send_request(msc, msc->fb_id, reg, result,
				    MPAM_MSC_READ_CMD);
}

int mpam_fb_send_write_request(struct mpam_msc *msc, u16 reg, u32 value)
{
	return mpam_fb_send_request(msc, msc->fb_id, reg, &value,
				    MPAM_MSC_WRITE_CMD);
}

/* We only support MPAM-Fb protocol version 1.x */
int mpam_fb_check_protocol_version(struct mpam_msc *msc)
{
	u32 version;
	int ret;

	ret = mpam_fb_send_request(msc, 0, 0, &version,
				   MPAM_PROTOCOL_VERSION_CMD);
	if (ret)
		return ret;

	if (FIELD_GET(MPAM_FB_VERSION_MAJOR_MASK, version) != 1) {
		pr_err("Incompatible MPAM-Fb protocol version %d.%d\n",
		       FIELD_GET(MPAM_FB_VERSION_MAJOR_MASK, version),
		       FIELD_GET(MPAM_FB_VERSION_MINOR_MASK, version));

		return -EINVAL;
	}

	return 0;
}

int mpam_fb_check_shared_buffer_size(struct mpam_msc *msc)
{
	int min_buffer_size = MPAM_FB_MAX_MSG_SIZE +
			      sizeof(struct acpi_pcct_ext_pcc_shared_memory);

	if (msc->pcc_chan->pcc_chan->shmem_size < min_buffer_size) {
		pr_err("MPAM-Fb PCC channel size too small.\n");

		return -ENOMEM;
	}

	return 0;
}
