// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/time64.h>

#include "ipu6.h"
#include "ipu6-bus.h"
#include "ipu6-dma.h"
#include "ipu6-buttress.h"
#include "ipu6-platform-buttress-regs.h"

#define BOOTLOADER_STATUS_OFFSET       0x15c

#define BOOTLOADER_MAGIC_KEY		0xb00710ad

#define ENTRY	BUTTRESS_IU2CSECSR_IPC_PEER_COMP_ACTIONS_RST_PHASE1
#define EXIT	BUTTRESS_IU2CSECSR_IPC_PEER_COMP_ACTIONS_RST_PHASE2
#define QUERY	BUTTRESS_IU2CSECSR_IPC_PEER_QUERIED_IP_COMP_ACTIONS_RST_PHASE

#define BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX	10

#define BUTTRESS_POWER_TIMEOUT_US		(200 * USEC_PER_MSEC)

#define BUTTRESS_CSE_BOOTLOAD_TIMEOUT_US	(5 * USEC_PER_SEC)
#define BUTTRESS_CSE_AUTHENTICATE_TIMEOUT_US	(10 * USEC_PER_SEC)
#define BUTTRESS_CSE_FWRESET_TIMEOUT_US		(100 * USEC_PER_MSEC)

#define BUTTRESS_IPC_TX_TIMEOUT_MS		MSEC_PER_SEC
#define BUTTRESS_IPC_RX_TIMEOUT_MS		MSEC_PER_SEC
#define BUTTRESS_IPC_VALIDITY_TIMEOUT_US	(1 * USEC_PER_SEC)
#define BUTTRESS_TSC_SYNC_TIMEOUT_US		(5 * USEC_PER_MSEC)

#define BUTTRESS_IPC_RESET_RETRY		2000
#define BUTTRESS_CSE_IPC_RESET_RETRY	4
#define BUTTRESS_IPC_CMD_SEND_RETRY	1

#define BUTTRESS_MAX_CONSECUTIVE_IRQS	100

static const u32 ipu6_adev_irq_mask[2] = {
	BUTTRESS_ISR_IS_IRQ,
	BUTTRESS_ISR_PS_IRQ
};

int ipu6_buttress_ipc_reset(struct ipu6_device *isp,
			    struct ipu6_buttress_ipc *ipc)
{
	unsigned int retries = BUTTRESS_IPC_RESET_RETRY;
	struct ipu6_buttress *b = &isp->buttress;
	u32 val = 0, csr_in_clr;

	if (!isp->secure_mode) {
		dev_dbg(&isp->pdev->dev, "Skip IPC reset for non-secure mode");
		return 0;
	}

	mutex_lock(&b->ipc_mutex);

	/* Clear-by-1 CSR (all bits), corresponding internal states. */
	val = readl(isp->base + ipc->csr_in);
	writel(val, isp->base + ipc->csr_in);

	/* Set peer CSR bit IPC_PEER_COMP_ACTIONS_RST_PHASE1 */
	writel(ENTRY, isp->base + ipc->csr_out);
	/*
	 * Clear-by-1 all CSR bits EXCEPT following
	 * bits:
	 * A. IPC_PEER_COMP_ACTIONS_RST_PHASE1.
	 * B. IPC_PEER_COMP_ACTIONS_RST_PHASE2.
	 * C. Possibly custom bits, depending on
	 * their role.
	 */
	csr_in_clr = BUTTRESS_IU2CSECSR_IPC_PEER_DEASSERTED_REG_VALID_REQ |
		BUTTRESS_IU2CSECSR_IPC_PEER_ACKED_REG_VALID |
		BUTTRESS_IU2CSECSR_IPC_PEER_ASSERTED_REG_VALID_REQ | QUERY;

	do {
		usleep_range(400, 500);
		val = readl(isp->base + ipc->csr_in);
		switch (val) {
		case ENTRY | EXIT:
		case ENTRY | EXIT | QUERY:
			/*
			 * 1) Clear-by-1 CSR bits
			 * (IPC_PEER_COMP_ACTIONS_RST_PHASE1,
			 * IPC_PEER_COMP_ACTIONS_RST_PHASE2).
			 * 2) Set peer CSR bit
			 * IPC_PEER_QUERIED_IP_COMP_ACTIONS_RST_PHASE.
			 */
			writel(ENTRY | EXIT, isp->base + ipc->csr_in);
			writel(QUERY, isp->base + ipc->csr_out);
			break;
		case ENTRY:
		case ENTRY | QUERY:
			/*
			 * 1) Clear-by-1 CSR bits
			 * (IPC_PEER_COMP_ACTIONS_RST_PHASE1,
			 * IPC_PEER_QUERIED_IP_COMP_ACTIONS_RST_PHASE).
			 * 2) Set peer CSR bit
			 * IPC_PEER_COMP_ACTIONS_RST_PHASE1.
			 */
			writel(ENTRY | QUERY, isp->base + ipc->csr_in);
			writel(ENTRY, isp->base + ipc->csr_out);
			break;
		case EXIT:
		case EXIT | QUERY:
			/*
			 * Clear-by-1 CSR bit
			 * IPC_PEER_COMP_ACTIONS_RST_PHASE2.
			 * 1) Clear incoming doorbell.
			 * 2) Clear-by-1 all CSR bits EXCEPT following
			 * bits:
			 * A. IPC_PEER_COMP_ACTIONS_RST_PHASE1.
			 * B. IPC_PEER_COMP_ACTIONS_RST_PHASE2.
			 * C. Possibly custom bits, depending on
			 * their role.
			 * 3) Set peer CSR bit
			 * IPC_PEER_COMP_ACTIONS_RST_PHASE2.
			 */
			writel(EXIT, isp->base + ipc->csr_in);
			writel(0, isp->base + ipc->db0_in);
			writel(csr_in_clr, isp->base + ipc->csr_in);
			writel(EXIT, isp->base + ipc->csr_out);

			/*
			 * Read csr_in again to make sure if RST_PHASE2 is done.
			 * If csr_in is QUERY, it should be handled again.
			 */
			usleep_range(200, 300);
			val = readl(isp->base + ipc->csr_in);
			if (val & QUERY) {
				dev_dbg(&isp->pdev->dev,
					"RST_PHASE2 retry csr_in = %x\n", val);
				break;
			}
			mutex_unlock(&b->ipc_mutex);
			return 0;
		case QUERY:
			/*
			 * 1) Clear-by-1 CSR bit
			 * IPC_PEER_QUERIED_IP_COMP_ACTIONS_RST_PHASE.
			 * 2) Set peer CSR bit
			 * IPC_PEER_COMP_ACTIONS_RST_PHASE1
			 */
			writel(QUERY, isp->base + ipc->csr_in);
			writel(ENTRY, isp->base + ipc->csr_out);
			break;
		default:
			dev_dbg_ratelimited(&isp->pdev->dev,
					    "Unexpected CSR 0x%x\n", val);
			break;
		}
	} while (retries--);

	mutex_unlock(&b->ipc_mutex);
	dev_err(&isp->pdev->dev, "Timed out while waiting for CSE\n");

	return -ETIMEDOUT;
}

static void ipu6_buttress_ipc_validity_close(struct ipu6_device *isp,
					     struct ipu6_buttress_ipc *ipc)
{
	writel(BUTTRESS_IU2CSECSR_IPC_PEER_DEASSERTED_REG_VALID_REQ,
	       isp->base + ipc->csr_out);
}

static int
ipu6_buttress_ipc_validity_open(struct ipu6_device *isp,
				struct ipu6_buttress_ipc *ipc)
{
	unsigned int mask = BUTTRESS_IU2CSECSR_IPC_PEER_ACKED_REG_VALID;
	void __iomem *addr;
	int ret;
	u32 val;

	writel(BUTTRESS_IU2CSECSR_IPC_PEER_ASSERTED_REG_VALID_REQ,
	       isp->base + ipc->csr_out);

	addr = isp->base + ipc->csr_in;
	ret = readl_poll_timeout(addr, val, val & mask, 200,
				 BUTTRESS_IPC_VALIDITY_TIMEOUT_US);
	if (ret) {
		dev_err(&isp->pdev->dev, "CSE validity timeout 0x%x\n", val);
		ipu6_buttress_ipc_validity_close(isp, ipc);
	}

	return ret;
}

static void ipu6_buttress_ipc_recv(struct ipu6_device *isp,
				   struct ipu6_buttress_ipc *ipc, u32 *ipc_msg)
{
	if (ipc_msg)
		*ipc_msg = readl(isp->base + ipc->data0_in);
	writel(0, isp->base + ipc->db0_in);
}

static int ipu6_buttress_ipc_send_bulk(struct ipu6_device *isp,
				       struct ipu6_ipc_buttress_bulk_msg *msgs,
				       u32 size)
{
	unsigned long tx_timeout_jiffies, rx_timeout_jiffies;
	unsigned int i, retry = BUTTRESS_IPC_CMD_SEND_RETRY;
	struct ipu6_buttress *b = &isp->buttress;
	struct ipu6_buttress_ipc *ipc = &b->cse;
	u32 val;
	int ret;
	int tout;

	mutex_lock(&b->ipc_mutex);

	ret = ipu6_buttress_ipc_validity_open(isp, ipc);
	if (ret) {
		dev_err(&isp->pdev->dev, "IPC validity open failed\n");
		goto out;
	}

	tx_timeout_jiffies = msecs_to_jiffies(BUTTRESS_IPC_TX_TIMEOUT_MS);
	rx_timeout_jiffies = msecs_to_jiffies(BUTTRESS_IPC_RX_TIMEOUT_MS);

	for (i = 0; i < size; i++) {
		reinit_completion(&ipc->send_complete);
		if (msgs[i].require_resp)
			reinit_completion(&ipc->recv_complete);

		dev_dbg(&isp->pdev->dev, "bulk IPC command: 0x%x\n",
			msgs[i].cmd);
		writel(msgs[i].cmd, isp->base + ipc->data0_out);
		val = BUTTRESS_IU2CSEDB0_BUSY | msgs[i].cmd_size;
		writel(val, isp->base + ipc->db0_out);

		tout = wait_for_completion_timeout(&ipc->send_complete,
						   tx_timeout_jiffies);
		if (!tout) {
			dev_err(&isp->pdev->dev, "send IPC response timeout\n");
			if (!retry--) {
				ret = -ETIMEDOUT;
				goto out;
			}

			/* Try again if CSE is not responding on first try */
			writel(0, isp->base + ipc->db0_out);
			i--;
			continue;
		}

		retry = BUTTRESS_IPC_CMD_SEND_RETRY;

		if (!msgs[i].require_resp)
			continue;

		tout = wait_for_completion_timeout(&ipc->recv_complete,
						   rx_timeout_jiffies);
		if (!tout) {
			dev_err(&isp->pdev->dev, "recv IPC response timeout\n");
			ret = -ETIMEDOUT;
			goto out;
		}

		if (ipc->nack_mask &&
		    (ipc->recv_data & ipc->nack_mask) == ipc->nack) {
			dev_err(&isp->pdev->dev,
				"IPC NACK for cmd 0x%x\n", msgs[i].cmd);
			ret = -EIO;
			goto out;
		}

		if (ipc->recv_data != msgs[i].expected_resp) {
			dev_err(&isp->pdev->dev,
				"expected resp: 0x%x, IPC response: 0x%x ",
				msgs[i].expected_resp, ipc->recv_data);
			ret = -EIO;
			goto out;
		}
	}

	dev_dbg(&isp->pdev->dev, "bulk IPC commands done\n");

out:
	ipu6_buttress_ipc_validity_close(isp, ipc);
	mutex_unlock(&b->ipc_mutex);
	return ret;
}

static int
ipu6_buttress_ipc_send(struct ipu6_device *isp,
		       u32 ipc_msg, u32 size, bool require_resp,
		       u32 expected_resp)
{
	struct ipu6_ipc_buttress_bulk_msg msg = {
		.cmd = ipc_msg,
		.cmd_size = size,
		.require_resp = require_resp,
		.expected_resp = expected_resp,
	};

	return ipu6_buttress_ipc_send_bulk(isp, &msg, 1);
}

static irqreturn_t ipu6_buttress_call_isr(struct ipu6_bus_device *adev)
{
	irqreturn_t ret = IRQ_WAKE_THREAD;

	if (!adev || !adev->auxdrv || !adev->auxdrv_data)
		return IRQ_NONE;

	if (adev->auxdrv_data->isr)
		ret = adev->auxdrv_data->isr(adev);

	if (ret == IRQ_WAKE_THREAD && !adev->auxdrv_data->isr_threaded)
		ret = IRQ_NONE;

	return ret;
}

irqreturn_t ipu6_buttress_isr(int irq, void *isp_ptr)
{
	struct ipu6_device *isp = isp_ptr;
	struct ipu6_bus_device *adev[] = { isp->isys, isp->psys };
	struct ipu6_buttress *b = &isp->buttress;
	u32 reg_irq_sts = BUTTRESS_REG_ISR_STATUS;
	irqreturn_t ret = IRQ_NONE;
	u32 disable_irqs = 0;
	u32 irq_status;
	u32 i, count = 0;
	int active;

	active = pm_runtime_get_if_active(&isp->pdev->dev);
	if (!active)
		return IRQ_NONE;

	irq_status = readl(isp->base + reg_irq_sts);
	if (irq_status == 0 || WARN_ON_ONCE(irq_status == 0xffffffffu)) {
		if (active > 0)
			pm_runtime_put_noidle(&isp->pdev->dev);
		return IRQ_NONE;
	}

	do {
		writel(irq_status, isp->base + BUTTRESS_REG_ISR_CLEAR);

		for (i = 0; i < ARRAY_SIZE(ipu6_adev_irq_mask); i++) {
			irqreturn_t r = ipu6_buttress_call_isr(adev[i]);

			if (!(irq_status & ipu6_adev_irq_mask[i]))
				continue;

			if (r == IRQ_WAKE_THREAD) {
				ret = IRQ_WAKE_THREAD;
				disable_irqs |= ipu6_adev_irq_mask[i];
			} else if (ret == IRQ_NONE && r == IRQ_HANDLED) {
				ret = IRQ_HANDLED;
			}
		}

		if ((irq_status & BUTTRESS_EVENT) && ret == IRQ_NONE)
			ret = IRQ_HANDLED;

		if (irq_status & BUTTRESS_ISR_IPC_FROM_CSE_IS_WAITING) {
			dev_dbg(&isp->pdev->dev,
				"BUTTRESS_ISR_IPC_FROM_CSE_IS_WAITING\n");
			ipu6_buttress_ipc_recv(isp, &b->cse, &b->cse.recv_data);
			complete(&b->cse.recv_complete);
		}

		if (irq_status & BUTTRESS_ISR_IPC_EXEC_DONE_BY_CSE) {
			dev_dbg(&isp->pdev->dev,
				"BUTTRESS_ISR_IPC_EXEC_DONE_BY_CSE\n");
			complete(&b->cse.send_complete);
		}

		if (irq_status & BUTTRESS_ISR_SAI_VIOLATION &&
		    ipu6_buttress_get_secure_mode(isp))
			dev_err(&isp->pdev->dev,
				"BUTTRESS_ISR_SAI_VIOLATION\n");

		if (irq_status & (BUTTRESS_ISR_IS_FATAL_MEM_ERR |
				  BUTTRESS_ISR_PS_FATAL_MEM_ERR))
			dev_err(&isp->pdev->dev,
				"BUTTRESS_ISR_FATAL_MEM_ERR\n");

		if (irq_status & BUTTRESS_ISR_UFI_ERROR)
			dev_err(&isp->pdev->dev, "BUTTRESS_ISR_UFI_ERROR\n");

		if (++count == BUTTRESS_MAX_CONSECUTIVE_IRQS) {
			dev_err(&isp->pdev->dev, "too many consecutive IRQs\n");
			ret = IRQ_NONE;
			break;
		}

		irq_status = readl(isp->base + reg_irq_sts);
	} while (irq_status);

	if (disable_irqs)
		writel(BUTTRESS_IRQS & ~disable_irqs,
		       isp->base + BUTTRESS_REG_ISR_ENABLE);

	if (active > 0)
		pm_runtime_put(&isp->pdev->dev);

	return ret;
}

irqreturn_t ipu6_buttress_isr_threaded(int irq, void *isp_ptr)
{
	struct ipu6_device *isp = isp_ptr;
	struct ipu6_bus_device *adev[] = { isp->isys, isp->psys };
	const struct ipu6_auxdrv_data *drv_data = NULL;
	irqreturn_t ret = IRQ_NONE;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ipu6_adev_irq_mask) && adev[i]; i++) {
		drv_data = adev[i]->auxdrv_data;
		if (!drv_data)
			continue;

		if (drv_data->wake_isr_thread &&
		    drv_data->isr_threaded(adev[i]) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}

	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_ISR_ENABLE);

	return ret;
}

int ipu6_buttress_power(struct device *dev,
			const struct ipu6_buttress_ctrl *ctrl, bool on)
{
	struct ipu6_device *isp = to_ipu6_bus_device(dev)->isp;
	u32 pwr_sts, val;
	int ret;

	if (!ctrl)
		return 0;

	mutex_lock(&isp->buttress.power_mutex);

	if (!on) {
		val = 0;
		pwr_sts = ctrl->pwr_sts_off << ctrl->pwr_sts_shift;
	} else {
		val = BUTTRESS_FREQ_CTL_START |
			FIELD_PREP(BUTTRESS_FREQ_CTL_RATIO_MASK,
				   ctrl->ratio) |
			FIELD_PREP(BUTTRESS_FREQ_CTL_QOS_FLOOR_MASK,
				   ctrl->qos_floor) |
			BUTTRESS_FREQ_CTL_ICCMAX_LEVEL;

		pwr_sts = ctrl->pwr_sts_on << ctrl->pwr_sts_shift;
	}

	writel(val, isp->base + ctrl->freq_ctl);

	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATE,
				 val, (val & ctrl->pwr_sts_mask) == pwr_sts,
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret)
		dev_err(&isp->pdev->dev,
			"Change power status timeout with 0x%x\n", val);

	mutex_unlock(&isp->buttress.power_mutex);

	return ret;
}

bool ipu6_buttress_get_secure_mode(struct ipu6_device *isp)
{
	u32 val;

	val = readl(isp->base + BUTTRESS_REG_SECURITY_CTL);

	return val & BUTTRESS_SECURITY_CTL_FW_SECURE_MODE;
}

bool ipu6_buttress_auth_done(struct ipu6_device *isp)
{
	u32 val;

	if (!isp->secure_mode)
		return true;

	val = readl(isp->base + BUTTRESS_REG_SECURITY_CTL);
	val = FIELD_GET(BUTTRESS_SECURITY_CTL_FW_SETUP_MASK, val);

	return val == BUTTRESS_SECURITY_CTL_AUTH_DONE;
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_auth_done, "INTEL_IPU6");

int ipu6_buttress_reset_authentication(struct ipu6_device *isp)
{
	int ret;
	u32 val;

	if (!isp->secure_mode) {
		dev_dbg(&isp->pdev->dev, "Skip auth for non-secure mode\n");
		return 0;
	}

	writel(BUTTRESS_FW_RESET_CTL_START, isp->base +
	       BUTTRESS_REG_FW_RESET_CTL);

	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_FW_RESET_CTL, val,
				 val & BUTTRESS_FW_RESET_CTL_DONE, 500,
				 BUTTRESS_CSE_FWRESET_TIMEOUT_US);
	if (ret) {
		dev_err(&isp->pdev->dev,
			"Time out while resetting authentication state\n");
		return ret;
	}

	dev_dbg(&isp->pdev->dev, "FW reset for authentication done\n");
	writel(0, isp->base + BUTTRESS_REG_FW_RESET_CTL);
	/* leave some time for HW restore */
	usleep_range(800, 1000);

	return 0;
}

int ipu6_buttress_map_fw_image(struct ipu6_bus_device *sys,
			       const struct firmware *fw, struct sg_table *sgt)
{
	bool is_vmalloc = is_vmalloc_addr(fw->data);
	struct pci_dev *pdev = sys->isp->pdev;
	struct page **pages;
	const void *addr;
	unsigned long n_pages;
	unsigned int i;
	int ret;

	if (!is_vmalloc && !virt_addr_valid(fw->data))
		return -EDOM;

	n_pages = PFN_UP(fw->size);

	pages = kmalloc_array(n_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	addr = fw->data;
	for (i = 0; i < n_pages; i++) {
		struct page *p = is_vmalloc ?
			vmalloc_to_page(addr) : virt_to_page(addr);

		if (!p) {
			ret = -ENOMEM;
			goto out;
		}
		pages[i] = p;
		addr += PAGE_SIZE;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, n_pages, 0, fw->size,
					GFP_KERNEL);
	if (ret) {
		ret = -ENOMEM;
		goto out;
	}

	ret = dma_map_sgtable(&pdev->dev, sgt, DMA_TO_DEVICE, 0);
	if (ret) {
		sg_free_table(sgt);
		goto out;
	}

	ret = ipu6_dma_map_sgtable(sys, sgt, DMA_TO_DEVICE, 0);
	if (ret) {
		dma_unmap_sgtable(&pdev->dev, sgt, DMA_TO_DEVICE, 0);
		sg_free_table(sgt);
		goto out;
	}

	ipu6_dma_sync_sgtable(sys, sgt);

out:
	kfree(pages);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_map_fw_image, "INTEL_IPU6");

void ipu6_buttress_unmap_fw_image(struct ipu6_bus_device *sys,
				  struct sg_table *sgt)
{
	struct pci_dev *pdev = sys->isp->pdev;

	ipu6_dma_unmap_sgtable(sys, sgt, DMA_TO_DEVICE, 0);
	dma_unmap_sgtable(&pdev->dev, sgt, DMA_TO_DEVICE, 0);
	sg_free_table(sgt);
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_unmap_fw_image, "INTEL_IPU6");

int ipu6_buttress_authenticate(struct ipu6_device *isp)
{
	struct ipu6_buttress *b = &isp->buttress;
	struct ipu6_psys_pdata *psys_pdata;
	u32 data, mask, done, fail;
	int ret;

	if (!isp->secure_mode) {
		dev_dbg(&isp->pdev->dev, "Skip auth for non-secure mode\n");
		return 0;
	}

	psys_pdata = isp->psys->pdata;

	mutex_lock(&b->auth_mutex);

	if (ipu6_buttress_auth_done(isp)) {
		ret = 0;
		goto out_unlock;
	}

	/*
	 * Write address of FIT table to FW_SOURCE register
	 * Let's use fw address. I.e. not using FIT table yet
	 */
	data = lower_32_bits(isp->psys->pkg_dir_dma_addr);
	writel(data, isp->base + BUTTRESS_REG_FW_SOURCE_BASE_LO);

	data = upper_32_bits(isp->psys->pkg_dir_dma_addr);
	writel(data, isp->base + BUTTRESS_REG_FW_SOURCE_BASE_HI);

	/*
	 * Write boot_load into IU2CSEDATA0
	 * Write sizeof(boot_load) | 0x2 << CLIENT_ID to
	 * IU2CSEDB.IU2CSECMD and set IU2CSEDB.IU2CSEBUSY as
	 */
	dev_info(&isp->pdev->dev, "Sending BOOT_LOAD to CSE\n");

	ret = ipu6_buttress_ipc_send(isp,
				     BUTTRESS_IU2CSEDATA0_IPC_BOOT_LOAD,
				     1, true,
				     BUTTRESS_CSE2IUDATA0_IPC_BOOT_LOAD_DONE);
	if (ret) {
		dev_err(&isp->pdev->dev, "CSE boot_load failed\n");
		goto out_unlock;
	}

	mask = BUTTRESS_SECURITY_CTL_FW_SETUP_MASK;
	done = BUTTRESS_SECURITY_CTL_FW_SETUP_DONE;
	fail = BUTTRESS_SECURITY_CTL_AUTH_FAILED;
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_SECURITY_CTL, data,
				 ((data & mask) == done ||
				  (data & mask) == fail), 500,
				 BUTTRESS_CSE_BOOTLOAD_TIMEOUT_US);
	if (ret) {
		dev_err(&isp->pdev->dev, "CSE boot_load timeout\n");
		goto out_unlock;
	}

	if ((data & mask) == fail) {
		dev_err(&isp->pdev->dev, "CSE auth failed\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = readl_poll_timeout(psys_pdata->base + BOOTLOADER_STATUS_OFFSET,
				 data, data == BOOTLOADER_MAGIC_KEY, 500,
				 BUTTRESS_CSE_BOOTLOAD_TIMEOUT_US);
	if (ret) {
		dev_err(&isp->pdev->dev, "Unexpected magic number 0x%x\n",
			data);
		goto out_unlock;
	}

	/*
	 * Write authenticate_run into IU2CSEDATA0
	 * Write sizeof(boot_load) | 0x2 << CLIENT_ID to
	 * IU2CSEDB.IU2CSECMD and set IU2CSEDB.IU2CSEBUSY as
	 */
	dev_info(&isp->pdev->dev, "Sending AUTHENTICATE_RUN to CSE\n");
	ret = ipu6_buttress_ipc_send(isp,
				     BUTTRESS_IU2CSEDATA0_IPC_AUTH_RUN,
				     1, true,
				     BUTTRESS_CSE2IUDATA0_IPC_AUTH_RUN_DONE);
	if (ret) {
		dev_err(&isp->pdev->dev, "CSE authenticate_run failed\n");
		goto out_unlock;
	}

	done = BUTTRESS_SECURITY_CTL_AUTH_DONE;
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_SECURITY_CTL, data,
				 ((data & mask) == done ||
				  (data & mask) == fail), 500,
				 BUTTRESS_CSE_AUTHENTICATE_TIMEOUT_US);
	if (ret) {
		dev_err(&isp->pdev->dev, "CSE authenticate timeout\n");
		goto out_unlock;
	}

	if ((data & mask) == fail) {
		dev_err(&isp->pdev->dev, "CSE boot_load failed\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	dev_info(&isp->pdev->dev, "CSE authenticate_run done\n");

out_unlock:
	mutex_unlock(&b->auth_mutex);

	return ret;
}

static int ipu6_buttress_send_tsc_request(struct ipu6_device *isp)
{
	u32 val, mask, done;
	int ret;

	mask = BUTTRESS_PWR_STATE_HH_STATUS_MASK;

	writel(BUTTRESS_FABRIC_CMD_START_TSC_SYNC,
	       isp->base + BUTTRESS_REG_FABRIC_CMD);

	val = readl(isp->base + BUTTRESS_REG_PWR_STATE);
	val = FIELD_GET(mask, val);
	if (val == BUTTRESS_PWR_STATE_HH_STATE_ERR) {
		dev_err(&isp->pdev->dev, "Start tsc sync failed\n");
		return -EINVAL;
	}

	done = BUTTRESS_PWR_STATE_HH_STATE_DONE;
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATE, val,
				 FIELD_GET(mask, val) == done, 500,
				 BUTTRESS_TSC_SYNC_TIMEOUT_US);
	if (ret)
		dev_err(&isp->pdev->dev, "Start tsc sync timeout\n");

	return ret;
}

int ipu6_buttress_start_tsc_sync(struct ipu6_device *isp)
{
	unsigned int i;

	for (i = 0; i < BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX; i++) {
		u32 val;
		int ret;

		ret = ipu6_buttress_send_tsc_request(isp);
		if (ret != -ETIMEDOUT)
			return ret;

		val = readl(isp->base + BUTTRESS_REG_TSW_CTL);
		val = val | BUTTRESS_TSW_CTL_SOFT_RESET;
		writel(val, isp->base + BUTTRESS_REG_TSW_CTL);
		val = val & ~BUTTRESS_TSW_CTL_SOFT_RESET;
		writel(val, isp->base + BUTTRESS_REG_TSW_CTL);
	}

	dev_err(&isp->pdev->dev, "TSC sync failed (timeout)\n");

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_start_tsc_sync, "INTEL_IPU6");

void ipu6_buttress_tsc_read(struct ipu6_device *isp, u64 *val)
{
	u32 tsc_hi_1, tsc_hi_2, tsc_lo;
	unsigned long flags;

	local_irq_save(flags);
	tsc_hi_1 = readl(isp->base + BUTTRESS_REG_TSC_HI);
	tsc_lo = readl(isp->base + BUTTRESS_REG_TSC_LO);
	tsc_hi_2 = readl(isp->base + BUTTRESS_REG_TSC_HI);
	if (tsc_hi_1 == tsc_hi_2) {
		*val = (u64)tsc_hi_1 << 32 | tsc_lo;
	} else {
		/* Check if TSC has rolled over */
		if (tsc_lo & BIT(31))
			*val = (u64)tsc_hi_1 << 32 | tsc_lo;
		else
			*val = (u64)tsc_hi_2 << 32 | tsc_lo;
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_tsc_read, "INTEL_IPU6");

u64 ipu6_buttress_tsc_ticks_to_ns(u64 ticks, const struct ipu6_device *isp)
{
	u64 ns = ticks * 10000;

	/*
	 * converting TSC tick count to ns is calculated by:
	 * Example (TSC clock frequency is 19.2MHz):
	 * ns = ticks * 1000 000 000 / 19.2Mhz
	 *    = ticks * 1000 000 000 / 19200000Hz
	 *    = ticks * 10000 / 192 ns
	 */
	return div_u64(ns, isp->buttress.ref_clk);
}
EXPORT_SYMBOL_NS_GPL(ipu6_buttress_tsc_ticks_to_ns, "INTEL_IPU6");

void ipu6_buttress_restore(struct ipu6_device *isp)
{
	struct ipu6_buttress *b = &isp->buttress;

	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_ISR_CLEAR);
	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_ISR_ENABLE);
	writel(b->wdt_cached_value, isp->base + BUTTRESS_REG_WDT);
}

int ipu6_buttress_init(struct ipu6_device *isp)
{
	int ret, ipc_reset_retry = BUTTRESS_CSE_IPC_RESET_RETRY;
	struct ipu6_buttress *b = &isp->buttress;
	u32 val;

	mutex_init(&b->power_mutex);
	mutex_init(&b->auth_mutex);
	mutex_init(&b->cons_mutex);
	mutex_init(&b->ipc_mutex);
	init_completion(&b->cse.send_complete);
	init_completion(&b->cse.recv_complete);

	b->cse.nack = BUTTRESS_CSE2IUDATA0_IPC_NACK;
	b->cse.nack_mask = BUTTRESS_CSE2IUDATA0_IPC_NACK_MASK;
	b->cse.csr_in = BUTTRESS_REG_CSE2IUCSR;
	b->cse.csr_out = BUTTRESS_REG_IU2CSECSR;
	b->cse.db0_in = BUTTRESS_REG_CSE2IUDB0;
	b->cse.db0_out = BUTTRESS_REG_IU2CSEDB0;
	b->cse.data0_in = BUTTRESS_REG_CSE2IUDATA0;
	b->cse.data0_out = BUTTRESS_REG_IU2CSEDATA0;

	INIT_LIST_HEAD(&b->constraints);

	isp->secure_mode = ipu6_buttress_get_secure_mode(isp);
	dev_dbg(&isp->pdev->dev, "IPU6 in %s mode touch 0x%x mask 0x%x\n",
		isp->secure_mode ? "secure" : "non-secure",
		readl(isp->base + BUTTRESS_REG_SECURITY_TOUCH),
		readl(isp->base + BUTTRESS_REG_CAMERA_MASK));

	b->wdt_cached_value = readl(isp->base + BUTTRESS_REG_WDT);
	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_ISR_CLEAR);
	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_ISR_ENABLE);

	/* get ref_clk frequency by reading the indication in btrs control */
	val = readl(isp->base + BUTTRESS_REG_BTRS_CTRL);
	val = FIELD_GET(BUTTRESS_REG_BTRS_CTRL_REF_CLK_IND, val);

	switch (val) {
	case 0x0:
		b->ref_clk = 240;
		break;
	case 0x1:
		b->ref_clk = 192;
		break;
	case 0x2:
		b->ref_clk = 384;
		break;
	default:
		dev_warn(&isp->pdev->dev,
			 "Unsupported ref clock, use 19.2Mhz by default.\n");
		b->ref_clk = 192;
		break;
	}

	/* Retry couple of times in case of CSE initialization is delayed */
	do {
		ret = ipu6_buttress_ipc_reset(isp, &b->cse);
		if (ret) {
			dev_warn(&isp->pdev->dev,
				 "IPC reset protocol failed, retrying\n");
		} else {
			dev_dbg(&isp->pdev->dev, "IPC reset done\n");
			return 0;
		}
	} while (ipc_reset_retry--);

	dev_err(&isp->pdev->dev, "IPC reset protocol failed\n");

	mutex_destroy(&b->power_mutex);
	mutex_destroy(&b->auth_mutex);
	mutex_destroy(&b->cons_mutex);
	mutex_destroy(&b->ipc_mutex);

	return ret;
}

void ipu6_buttress_exit(struct ipu6_device *isp)
{
	struct ipu6_buttress *b = &isp->buttress;

	writel(0, isp->base + BUTTRESS_REG_ISR_ENABLE);

	mutex_destroy(&b->power_mutex);
	mutex_destroy(&b->auth_mutex);
	mutex_destroy(&b->cons_mutex);
	mutex_destroy(&b->ipc_mutex);
}
