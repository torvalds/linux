// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <asm/barrier.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/time64.h>
#include <linux/remoteproc/mtk_scp.h>

#include "mtk_common.h"

#define SCP_TIMEOUT_US		(2000 * USEC_PER_MSEC)

/**
 * scp_ipi_register() - register an ipi function
 *
 * @scp:	mtk_scp structure
 * @id:		IPI ID
 * @handler:	IPI handler
 * @priv:	private data for IPI handler
 *
 * Register an ipi function to receive ipi interrupt from SCP.
 *
 * Return: 0 if ipi registers successfully, -error on error.
 */
int scp_ipi_register(struct mtk_scp *scp,
		     u32 id,
		     scp_ipi_handler_t handler,
		     void *priv)
{
	if (!scp)
		return -EPROBE_DEFER;

	if (WARN_ON(id >= SCP_IPI_MAX) || WARN_ON(handler == NULL))
		return -EINVAL;

	scp_ipi_lock(scp, id);
	scp->ipi_desc[id].handler = handler;
	scp->ipi_desc[id].priv = priv;
	scp_ipi_unlock(scp, id);

	return 0;
}
EXPORT_SYMBOL_GPL(scp_ipi_register);

/**
 * scp_ipi_unregister() - unregister an ipi function
 *
 * @scp:	mtk_scp structure
 * @id:		IPI ID
 *
 * Unregister an ipi function to receive ipi interrupt from SCP.
 */
void scp_ipi_unregister(struct mtk_scp *scp, u32 id)
{
	if (!scp)
		return;

	if (WARN_ON(id >= SCP_IPI_MAX))
		return;

	scp_ipi_lock(scp, id);
	scp->ipi_desc[id].handler = NULL;
	scp->ipi_desc[id].priv = NULL;
	scp_ipi_unlock(scp, id);
}
EXPORT_SYMBOL_GPL(scp_ipi_unregister);

/*
 * scp_memcpy_aligned() - Copy src to dst, where dst is in SCP SRAM region.
 *
 * @dst:	Pointer to the destination buffer, should be in SCP SRAM region.
 * @src:	Pointer to the source buffer.
 * @len:	Length of the source buffer to be copied.
 *
 * Since AP access of SCP SRAM don't support byte write, this always write a
 * full word at a time, and may cause some extra bytes to be written at the
 * beginning & ending of dst.
 */
void scp_memcpy_aligned(void __iomem *dst, const void *src, unsigned int len)
{
	void __iomem *ptr;
	u32 val;
	unsigned int i = 0, remain;

	if (!IS_ALIGNED((unsigned long)dst, 4)) {
		ptr = (void __iomem *)ALIGN_DOWN((unsigned long)dst, 4);
		i = 4 - (dst - ptr);
		val = readl_relaxed(ptr);
		memcpy((u8 *)&val + (4 - i), src, i);
		writel_relaxed(val, ptr);
	}

	__iowrite32_copy(dst + i, src + i, (len - i) / 4);
	remain = (len - i) % 4;

	if (remain > 0) {
		val = readl_relaxed(dst + len - remain);
		memcpy(&val, src + len - remain, remain);
		writel_relaxed(val, dst + len - remain);
	}
}
EXPORT_SYMBOL_GPL(scp_memcpy_aligned);

/**
 * scp_ipi_lock() - Lock before operations of an IPI ID
 *
 * @scp:	mtk_scp structure
 * @id:		IPI ID
 *
 * Note: This should not be used by drivers other than mtk_scp.
 */
void scp_ipi_lock(struct mtk_scp *scp, u32 id)
{
	if (WARN_ON(id >= SCP_IPI_MAX))
		return;
	mutex_lock(&scp->ipi_desc[id].lock);
}
EXPORT_SYMBOL_GPL(scp_ipi_lock);

/**
 * scp_ipi_unlock() - Unlock after operations of an IPI ID
 *
 * @scp:	mtk_scp structure
 * @id:		IPI ID
 *
 * Note: This should not be used by drivers other than mtk_scp.
 */
void scp_ipi_unlock(struct mtk_scp *scp, u32 id)
{
	if (WARN_ON(id >= SCP_IPI_MAX))
		return;
	mutex_unlock(&scp->ipi_desc[id].lock);
}
EXPORT_SYMBOL_GPL(scp_ipi_unlock);

/**
 * scp_ipi_send() - send data from AP to scp.
 *
 * @scp:	mtk_scp structure
 * @id:		IPI ID
 * @buf:	the data buffer
 * @len:	the data buffer length
 * @wait:	number of msecs to wait for ack. 0 to skip waiting.
 *
 * This function is thread-safe. When this function returns,
 * SCP has received the data and starts the processing.
 * When the processing completes, IPI handler registered
 * by scp_ipi_register will be called in interrupt context.
 *
 * Return: 0 if sending data successfully, -error on error.
 **/
int scp_ipi_send(struct mtk_scp *scp, u32 id, void *buf, unsigned int len,
		 unsigned int wait)
{
	struct mtk_share_obj __iomem *send_obj = scp->send_buf;
	u32 val;
	int ret;
	const struct mtk_scp_sizes_data *scp_sizes;

	scp_sizes = scp->data->scp_sizes;

	if (WARN_ON(id <= SCP_IPI_INIT) || WARN_ON(id >= SCP_IPI_MAX) ||
	    WARN_ON(id == SCP_IPI_NS_SERVICE) ||
	    WARN_ON(len > scp_sizes->ipi_share_buffer_size) || WARN_ON(!buf))
		return -EINVAL;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(scp->dev, "failed to enable clock\n");
		return ret;
	}

	mutex_lock(&scp->send_lock);

	 /* Wait until SCP receives the last command */
	ret = readl_poll_timeout_atomic(scp->cluster->reg_base + scp->data->host_to_scp_reg,
					val, !val, 0, SCP_TIMEOUT_US);
	if (ret) {
		dev_err(scp->dev, "%s: IPI timeout!\n", __func__);
		goto unlock_mutex;
	}

	scp_memcpy_aligned(&send_obj->share_buf, buf, len);

	writel(len, &send_obj->len);
	writel(id, &send_obj->id);

	scp->ipi_id_ack[id] = false;
	/* send the command to SCP */
	writel(scp->data->host_to_scp_int_bit,
	       scp->cluster->reg_base + scp->data->host_to_scp_reg);

	if (wait) {
		/* wait for SCP's ACK */
		ret = wait_event_timeout(scp->ack_wq,
					 scp->ipi_id_ack[id],
					 msecs_to_jiffies(wait));
		scp->ipi_id_ack[id] = false;
		if (WARN(!ret, "scp ipi %d ack time out !", id))
			ret = -EIO;
		else
			ret = 0;
	}

unlock_mutex:
	mutex_unlock(&scp->send_lock);
	clk_disable_unprepare(scp->clk);

	return ret;
}
EXPORT_SYMBOL_GPL(scp_ipi_send);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek scp IPI interface");
