// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <asm/cpu_device_id.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#include "ipu7.h"
#include "ipu7-bus.h"
#include "ipu7-buttress.h"
#include "ipu7-buttress-regs.h"

#define BOOTLOADER_STATUS_OFFSET	BUTTRESS_REG_FW_BOOT_PARAMS7

#define BOOTLOADER_MAGIC_KEY		0xb00710adU

#define ENTRY	BUTTRESS_IU2CSECSR_IPC_PEER_COMP_ACTIONS_RST_PHASE1
#define EXIT	BUTTRESS_IU2CSECSR_IPC_PEER_COMP_ACTIONS_RST_PHASE2
#define QUERY	BUTTRESS_IU2CSECSR_IPC_PEER_QUERIED_IP_COMP_ACTIONS_RST_PHASE

#define BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX	10U

#define BUTTRESS_POWER_TIMEOUT_US		(200 * USEC_PER_MSEC)

#define BUTTRESS_CSE_BOOTLOAD_TIMEOUT_US	(5 * USEC_PER_SEC)
#define BUTTRESS_CSE_AUTHENTICATE_TIMEOUT_US	(10 * USEC_PER_SEC)
#define BUTTRESS_CSE_FWRESET_TIMEOUT_US		(100 * USEC_PER_MSEC)

#define BUTTRESS_IPC_TX_TIMEOUT_MS		MSEC_PER_SEC
#define BUTTRESS_IPC_RX_TIMEOUT_MS		MSEC_PER_SEC
#define BUTTRESS_IPC_VALIDITY_TIMEOUT_US	(1 * USEC_PER_SEC)
#define BUTTRESS_TSC_SYNC_TIMEOUT_US		(5 * USEC_PER_MSEC)

#define BUTTRESS_IPC_RESET_RETRY		2000U
#define BUTTRESS_CSE_IPC_RESET_RETRY		4U
#define BUTTRESS_IPC_CMD_SEND_RETRY		1U

struct ipu7_ipc_buttress_msg {
	u32 cmd;
	u32 expected_resp;
	bool require_resp;
	u8 cmd_size;
};

static const u32 ipu7_adev_irq_mask[2] = {
	BUTTRESS_IRQ_IS_IRQ,
	BUTTRESS_IRQ_PS_IRQ
};

int ipu_buttress_ipc_reset(struct ipu7_device *isp,
			   struct ipu_buttress_ipc *ipc)
{
	unsigned int retries = BUTTRESS_IPC_RESET_RETRY;
	struct ipu_buttress *b = &isp->buttress;
	struct device *dev = &isp->pdev->dev;
	u32 val = 0, csr_in_clr;

	if (!isp->secure_mode) {
		dev_dbg(dev, "Skip IPC reset for non-secure mode\n");
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
				dev_dbg(dev,
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
			dev_dbg_ratelimited(dev, "Unexpected CSR 0x%x\n", val);
			break;
		}
	} while (retries--);

	mutex_unlock(&b->ipc_mutex);
	dev_err(dev, "Timed out while waiting for CSE\n");

	return -ETIMEDOUT;
}

static void ipu_buttress_ipc_validity_close(struct ipu7_device *isp,
					    struct ipu_buttress_ipc *ipc)
{
	writel(BUTTRESS_IU2CSECSR_IPC_PEER_DEASSERTED_REG_VALID_REQ,
	       isp->base + ipc->csr_out);
}

static int
ipu_buttress_ipc_validity_open(struct ipu7_device *isp,
			       struct ipu_buttress_ipc *ipc)
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
		ipu_buttress_ipc_validity_close(isp, ipc);
	}

	return ret;
}

static void ipu_buttress_ipc_recv(struct ipu7_device *isp,
				  struct ipu_buttress_ipc *ipc, u32 *ipc_msg)
{
	if (ipc_msg)
		*ipc_msg = readl(isp->base + ipc->data0_in);
	writel(0, isp->base + ipc->db0_in);
}

static int ipu_buttress_ipc_send_msg(struct ipu7_device *isp,
				     struct ipu7_ipc_buttress_msg *msg)
{
	unsigned long tx_timeout_jiffies, rx_timeout_jiffies;
	unsigned int retry = BUTTRESS_IPC_CMD_SEND_RETRY;
	struct ipu_buttress *b = &isp->buttress;
	struct ipu_buttress_ipc *ipc = &b->cse;
	struct device *dev = &isp->pdev->dev;
	int tout;
	u32 val;
	int ret;

	mutex_lock(&b->ipc_mutex);

	ret = ipu_buttress_ipc_validity_open(isp, ipc);
	if (ret) {
		dev_err(dev, "IPC validity open failed\n");
		goto out;
	}

	tx_timeout_jiffies = msecs_to_jiffies(BUTTRESS_IPC_TX_TIMEOUT_MS);
	rx_timeout_jiffies = msecs_to_jiffies(BUTTRESS_IPC_RX_TIMEOUT_MS);

try:
	reinit_completion(&ipc->send_complete);
	if (msg->require_resp)
		reinit_completion(&ipc->recv_complete);

	dev_dbg(dev, "IPC command: 0x%x\n", msg->cmd);
	writel(msg->cmd, isp->base + ipc->data0_out);
	val = BUTTRESS_IU2CSEDB0_BUSY | msg->cmd_size;
	writel(val, isp->base + ipc->db0_out);

	tout = wait_for_completion_timeout(&ipc->send_complete,
					   tx_timeout_jiffies);
	if (!tout) {
		dev_err(dev, "send IPC response timeout\n");
		if (!retry--) {
			ret = -ETIMEDOUT;
			goto out;
		}

		/* Try again if CSE is not responding on first try */
		writel(0, isp->base + ipc->db0_out);
		goto try;
	}

	if (!msg->require_resp) {
		ret = -EIO;
		goto out;
	}

	tout = wait_for_completion_timeout(&ipc->recv_complete,
					   rx_timeout_jiffies);
	if (!tout) {
		dev_err(dev, "recv IPC response timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (ipc->nack_mask &&
	    (ipc->recv_data & ipc->nack_mask) == ipc->nack) {
		dev_err(dev, "IPC NACK for cmd 0x%x\n", msg->cmd);
		ret = -EIO;
		goto out;
	}

	if (ipc->recv_data != msg->expected_resp) {
		dev_err(dev,
			"expected resp: 0x%x, IPC response: 0x%x\n",
			msg->expected_resp, ipc->recv_data);
		ret = -EIO;
		goto out;
	}

	dev_dbg(dev, "IPC commands done\n");

out:
	ipu_buttress_ipc_validity_close(isp, ipc);
	mutex_unlock(&b->ipc_mutex);

	return ret;
}

static int ipu_buttress_ipc_send(struct ipu7_device *isp,
				 u32 ipc_msg, u32 size, bool require_resp,
				 u32 expected_resp)
{
	struct ipu7_ipc_buttress_msg msg = {
		.cmd = ipc_msg,
		.cmd_size = size,
		.require_resp = require_resp,
		.expected_resp = expected_resp,
	};

	return ipu_buttress_ipc_send_msg(isp, &msg);
}

static irqreturn_t ipu_buttress_call_isr(struct ipu7_bus_device *adev)
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

irqreturn_t ipu_buttress_isr(int irq, void *isp_ptr)
{
	struct ipu7_device *isp = isp_ptr;
	struct ipu7_bus_device *adev[] = { isp->isys, isp->psys };
	struct ipu_buttress *b = &isp->buttress;
	struct device *dev = &isp->pdev->dev;
	irqreturn_t ret = IRQ_NONE;
	u32 pb_irq, pb_local_irq;
	u32 disable_irqs = 0;
	u32 irq_status;
	unsigned int i;

	pm_runtime_get_noresume(dev);

	pb_irq = readl(isp->pb_base + INTERRUPT_STATUS);
	writel(pb_irq, isp->pb_base + INTERRUPT_STATUS);

	/* check btrs ATS, CFI and IMR errors, BIT(0) is unused for IPU */
	pb_local_irq = readl(isp->pb_base + BTRS_LOCAL_INTERRUPT_MASK);
	if (pb_local_irq & ~BIT(0)) {
		dev_warn(dev, "PB interrupt status 0x%x local 0x%x\n", pb_irq,
			 pb_local_irq);
		dev_warn(dev, "Details: %x %x %x %x %x %x %x %x\n",
			 readl(isp->pb_base + ATS_ERROR_LOG1),
			 readl(isp->pb_base + ATS_ERROR_LOG2),
			 readl(isp->pb_base + CFI_0_ERROR_LOG),
			 readl(isp->pb_base + CFI_1_ERROR_LOGGING),
			 readl(isp->pb_base + IMR_ERROR_LOGGING_LOW),
			 readl(isp->pb_base + IMR_ERROR_LOGGING_HIGH),
			 readl(isp->pb_base + IMR_ERROR_LOGGING_CFI_1_LOW),
			 readl(isp->pb_base + IMR_ERROR_LOGGING_CFI_1_HIGH));
	}

	irq_status = readl(isp->base + BUTTRESS_REG_IRQ_STATUS);
	if (!irq_status) {
		pm_runtime_put_noidle(dev);
		return IRQ_NONE;
	}

	do {
		writel(irq_status, isp->base + BUTTRESS_REG_IRQ_CLEAR);

		for (i = 0; i < ARRAY_SIZE(ipu7_adev_irq_mask); i++) {
			irqreturn_t r = ipu_buttress_call_isr(adev[i]);

			if (!(irq_status & ipu7_adev_irq_mask[i]))
				continue;

			if (r == IRQ_WAKE_THREAD) {
				ret = IRQ_WAKE_THREAD;
				disable_irqs |= ipu7_adev_irq_mask[i];
			} else if (ret == IRQ_NONE && r == IRQ_HANDLED) {
				ret = IRQ_HANDLED;
			}
		}

		if (irq_status & (BUTTRESS_IRQS | BUTTRESS_IRQ_SAI_VIOLATION) &&
		    ret == IRQ_NONE)
			ret = IRQ_HANDLED;

		if (irq_status & BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING) {
			dev_dbg(dev, "BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING\n");
			ipu_buttress_ipc_recv(isp, &b->cse, &b->cse.recv_data);
			complete(&b->cse.recv_complete);
		}

		if (irq_status & BUTTRESS_IRQ_CSE_CSR_SET)
			dev_dbg(dev, "BUTTRESS_IRQ_CSE_CSR_SET\n");

		if (irq_status & BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE) {
			dev_dbg(dev, "BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE\n");
			complete(&b->cse.send_complete);
		}

		if (irq_status & BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ)
			dev_dbg(dev, "BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ\n");

		if (irq_status & BUTTRESS_IRQ_SAI_VIOLATION &&
		    ipu_buttress_get_secure_mode(isp))
			dev_err(dev, "BUTTRESS_IRQ_SAI_VIOLATION\n");

		irq_status = readl(isp->base + BUTTRESS_REG_IRQ_STATUS);
	} while (irq_status);

	if (disable_irqs)
		writel(BUTTRESS_IRQS & ~disable_irqs,
		       isp->base + BUTTRESS_REG_IRQ_ENABLE);

	pm_runtime_put(dev);

	return ret;
}

irqreturn_t ipu_buttress_isr_threaded(int irq, void *isp_ptr)
{
	struct ipu7_device *isp = isp_ptr;
	struct ipu7_bus_device *adev[] = { isp->isys, isp->psys };
	const struct ipu7_auxdrv_data *drv_data = NULL;
	irqreturn_t ret = IRQ_NONE;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ipu7_adev_irq_mask) && adev[i]; i++) {
		drv_data = adev[i]->auxdrv_data;
		if (!drv_data)
			continue;

		if (drv_data->wake_isr_thread &&
		    drv_data->isr_threaded(adev[i]) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}

	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_IRQ_ENABLE);

	return ret;
}

static int isys_d2d_power(struct device *dev, bool on)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	int ret = 0;
	u32 target = on ? BUTTRESS_D2D_PWR_ACK : 0U;
	u32 val;

	dev_dbg(dev, "power %s isys d2d.\n", on ? "UP" : "DOWN");
	val = readl(isp->base + BUTTRESS_REG_D2D_CTL);
	if ((val & BUTTRESS_D2D_PWR_ACK) == target) {
		dev_info(dev, "d2d already in %s state.\n",
			 on ? "UP" : "DOWN");
		return 0;
	}

	val = on ? val | BUTTRESS_D2D_PWR_EN : val & (~BUTTRESS_D2D_PWR_EN);
	writel(val, isp->base + BUTTRESS_REG_D2D_CTL);
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_D2D_CTL,
				 val, (val & BUTTRESS_D2D_PWR_ACK) == target,
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret)
		dev_err(dev, "power %s d2d timeout. status: 0x%x\n",
			on ? "UP" : "DOWN", val);

	return ret;
}

static void isys_nde_control(struct device *dev, bool on)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	u32 val, value, scale, valid, resvec;
	u32 nde_reg;

	if (on) {
		value = BUTTRESS_NDE_VAL_ACTIVE;
		scale = BUTTRESS_NDE_SCALE_ACTIVE;
		valid = BUTTRESS_NDE_VALID_ACTIVE;
	} else {
		value = BUTTRESS_NDE_VAL_DEFAULT;
		scale = BUTTRESS_NDE_SCALE_DEFAULT;
		valid = BUTTRESS_NDE_VALID_DEFAULT;
	}

	/* only set the fabrics resource ownership for ipu8 */
	nde_reg = is_ipu8(isp->hw_ver) ? IPU8_BUTTRESS_REG_NDE_CONTROL :
		IPU7_BUTTRESS_REG_NDE_CONTROL;
	resvec = is_ipu8(isp->hw_ver) ? 0x2 : 0xe;
	val = FIELD_PREP(NDE_VAL_MASK, value) |
		FIELD_PREP(NDE_SCALE_MASK, scale) |
		FIELD_PREP(NDE_VALID_MASK, valid) |
		FIELD_PREP(NDE_RESVEC_MASK, resvec);

	writel(val, isp->base + nde_reg);
}

static int ipu7_buttress_powerup(struct device *dev,
				 const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	u32 val, exp_sts;
	int ret = 0;

	if (!ctrl)
		return 0;

	mutex_lock(&isp->buttress.power_mutex);

	exp_sts = ctrl->pwr_sts_on << ctrl->pwr_sts_shift;
	if (ctrl->subsys_id == IPU_IS) {
		ret = isys_d2d_power(dev, true);
		if (ret)
			goto out_power;
		isys_nde_control(dev, true);
	}

	/* request clock resource ownership */
	val = readl(isp->base + BUTTRESS_REG_SLEEP_LEVEL_CFG);
	val |= ctrl->ovrd_clk;
	writel(val, isp->base + BUTTRESS_REG_SLEEP_LEVEL_CFG);
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_SLEEP_LEVEL_STS,
				 val, (val & ctrl->own_clk_ack),
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret)
		dev_warn(dev, "request clk ownership timeout. status 0x%x\n",
			 val);

	val = ctrl->ratio << ctrl->ratio_shift | ctrl->cdyn << ctrl->cdyn_shift;

	dev_dbg(dev, "set 0x%x to %s_WORKPOINT_REQ.\n", val,
		ctrl->subsys_id == IPU_IS ? "IS" : "PS");
	writel(val, isp->base + ctrl->freq_ctl);

	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATUS,
				 val, ((val & ctrl->pwr_sts_mask) == exp_sts),
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "%s power up timeout with status: 0x%x\n",
			ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
		goto out_power;
	}

	dev_dbg(dev, "%s power up successfully. status: 0x%x\n",
		ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);

	/* release clock resource ownership */
	val = readl(isp->base + BUTTRESS_REG_SLEEP_LEVEL_CFG);
	val &= ~ctrl->ovrd_clk;
	writel(val, isp->base + BUTTRESS_REG_SLEEP_LEVEL_CFG);

out_power:
	mutex_unlock(&isp->buttress.power_mutex);

	return ret;
}

static int ipu7_buttress_powerdown(struct device *dev,
				   const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	u32 val, exp_sts;
	int ret = 0;

	if (!ctrl)
		return 0;

	mutex_lock(&isp->buttress.power_mutex);

	exp_sts = ctrl->pwr_sts_off << ctrl->pwr_sts_shift;
	val = 0x8U << ctrl->ratio_shift;

	dev_dbg(dev, "set 0x%x to %s_WORKPOINT_REQ.\n", val,
		ctrl->subsys_id == IPU_IS ? "IS" : "PS");
	writel(val, isp->base + ctrl->freq_ctl);
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATUS,
				 val, ((val & ctrl->pwr_sts_mask) == exp_sts),
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "%s power down timeout with status: 0x%x\n",
			ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
		goto out_power;
	}

	dev_dbg(dev, "%s power down successfully. status: 0x%x\n",
		ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
out_power:
	if (ctrl->subsys_id == IPU_IS && !ret) {
		isys_d2d_power(dev, false);
		isys_nde_control(dev, false);
	}

	mutex_unlock(&isp->buttress.power_mutex);

	return ret;
}

static int ipu8_buttress_powerup(struct device *dev,
				 const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	u32 sleep_level_reg = BUTTRESS_REG_SLEEP_LEVEL_STS;
	u32 val, exp_sts;
	int ret = 0;

	if (!ctrl)
		return 0;

	mutex_lock(&isp->buttress.power_mutex);
	exp_sts = ctrl->pwr_sts_on << ctrl->pwr_sts_shift;
	if (ctrl->subsys_id == IPU_IS) {
		ret = isys_d2d_power(dev, true);
		if (ret)
			goto out_power;
		isys_nde_control(dev, true);
	}

	/* request ps_pll when psys freq > 400Mhz */
	if (ctrl->subsys_id == IPU_PS && ctrl->ratio > 0x10) {
		writel(1, isp->base + BUTTRESS_REG_PS_PLL_ENABLE);
		ret = readl_poll_timeout(isp->base + sleep_level_reg,
					 val, (val & ctrl->own_clk_ack),
					 100, BUTTRESS_POWER_TIMEOUT_US);
		if (ret)
			dev_warn(dev, "ps_pll req ack timeout. status 0x%x\n",
				 val);
	}

	val = ctrl->ratio << ctrl->ratio_shift | ctrl->cdyn << ctrl->cdyn_shift;
	dev_dbg(dev, "set 0x%x to %s_WORKPOINT_REQ.\n", val,
		ctrl->subsys_id == IPU_IS ? "IS" : "PS");
	writel(val, isp->base + ctrl->freq_ctl);
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATUS,
				 val, ((val & ctrl->pwr_sts_mask) == exp_sts),
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "%s power up timeout with status: 0x%x\n",
			ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
		goto out_power;
	}

	dev_dbg(dev, "%s power up successfully. status: 0x%x\n",
		ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
out_power:
	mutex_unlock(&isp->buttress.power_mutex);

	return ret;
}

static int ipu8_buttress_powerdown(struct device *dev,
				   const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;
	u32 val, exp_sts;
	int ret = 0;

	if (!ctrl)
		return 0;

	mutex_lock(&isp->buttress.power_mutex);
	exp_sts = ctrl->pwr_sts_off << ctrl->pwr_sts_shift;

	if (ctrl->subsys_id == IPU_PS)
		val = 0x10U << ctrl->ratio_shift;
	else
		val = 0x8U << ctrl->ratio_shift;

	dev_dbg(dev, "set 0x%x to %s_WORKPOINT_REQ.\n", val,
		ctrl->subsys_id == IPU_IS ? "IS" : "PS");
	writel(val, isp->base + ctrl->freq_ctl);
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATUS,
				 val, ((val & ctrl->pwr_sts_mask) == exp_sts),
				 100, BUTTRESS_POWER_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "%s power down timeout with status: 0x%x\n",
			ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
		goto out_power;
	}

	dev_dbg(dev, "%s power down successfully. status: 0x%x\n",
		ctrl->subsys_id == IPU_IS ? "IS" : "PS", val);
out_power:
	if (ctrl->subsys_id == IPU_IS && !ret) {
		isys_d2d_power(dev, false);
		isys_nde_control(dev, false);
	}

	if (ctrl->subsys_id == IPU_PS) {
		val = readl(isp->base + BUTTRESS_REG_SLEEP_LEVEL_STS);
		if (val & ctrl->own_clk_ack)
			writel(0, isp->base + BUTTRESS_REG_PS_PLL_ENABLE);
	}
	mutex_unlock(&isp->buttress.power_mutex);

	return ret;
}

int ipu_buttress_powerup(struct device *dev,
			 const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;

	if (is_ipu8(isp->hw_ver))
		return ipu8_buttress_powerup(dev, ctrl);

	return ipu7_buttress_powerup(dev, ctrl);
}

int ipu_buttress_powerdown(struct device *dev,
			   const struct ipu_buttress_ctrl *ctrl)
{
	struct ipu7_device *isp = to_ipu7_bus_device(dev)->isp;

	if (is_ipu8(isp->hw_ver))
		return ipu8_buttress_powerdown(dev, ctrl);

	return ipu7_buttress_powerdown(dev, ctrl);
}

bool ipu_buttress_get_secure_mode(struct ipu7_device *isp)
{
	u32 val;

	val = readl(isp->base + BUTTRESS_REG_SECURITY_CTL);

	return val & BUTTRESS_SECURITY_CTL_FW_SECURE_MODE;
}

bool ipu_buttress_auth_done(struct ipu7_device *isp)
{
	u32 val;

	if (!isp->secure_mode)
		return true;

	val = readl(isp->base + BUTTRESS_REG_SECURITY_CTL);
	val = FIELD_GET(BUTTRESS_SECURITY_CTL_FW_SETUP_MASK, val);

	return val == BUTTRESS_SECURITY_CTL_AUTH_DONE;
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_auth_done, "INTEL_IPU7");

int ipu_buttress_get_isys_freq(struct ipu7_device *isp, u32 *freq)
{
	u32 reg_val;
	int ret;

	ret = pm_runtime_get_sync(&isp->isys->auxdev.dev);
	if (ret < 0) {
		pm_runtime_put(&isp->isys->auxdev.dev);
		dev_err(&isp->pdev->dev, "Runtime PM failed (%d)\n", ret);
		return ret;
	}

	reg_val = readl(isp->base + BUTTRESS_REG_IS_WORKPOINT_REQ);

	pm_runtime_put(&isp->isys->auxdev.dev);

	if (is_ipu8(isp->hw_ver))
		*freq = (reg_val & BUTTRESS_IS_FREQ_CTL_RATIO_MASK) * 25;
	else
		*freq = (reg_val & BUTTRESS_IS_FREQ_CTL_RATIO_MASK) * 50 / 3;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_get_isys_freq, "INTEL_IPU7");

int ipu_buttress_get_psys_freq(struct ipu7_device *isp, u32 *freq)
{
	u32 reg_val;
	int ret;

	ret = pm_runtime_get_sync(&isp->psys->auxdev.dev);
	if (ret < 0) {
		pm_runtime_put(&isp->psys->auxdev.dev);
		dev_err(&isp->pdev->dev, "Runtime PM failed (%d)\n", ret);
		return ret;
	}

	reg_val = readl(isp->base + BUTTRESS_REG_PS_WORKPOINT_REQ);

	pm_runtime_put(&isp->psys->auxdev.dev);

	reg_val &= BUTTRESS_PS_FREQ_CTL_RATIO_MASK;
	*freq = BUTTRESS_PS_FREQ_RATIO_STEP * reg_val;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_get_psys_freq, "INTEL_IPU7");

int ipu_buttress_reset_authentication(struct ipu7_device *isp)
{
	struct device *dev = &isp->pdev->dev;
	int ret;
	u32 val;

	if (!isp->secure_mode) {
		dev_dbg(dev, "Skip auth for non-secure mode\n");
		return 0;
	}

	writel(BUTTRESS_FW_RESET_CTL_START, isp->base +
	       BUTTRESS_REG_FW_RESET_CTL);

	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_FW_RESET_CTL, val,
				 val & BUTTRESS_FW_RESET_CTL_DONE, 500,
				 BUTTRESS_CSE_FWRESET_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Time out while resetting authentication state\n");
		return ret;
	}

	dev_dbg(dev, "FW reset for authentication done\n");
	writel(0, isp->base + BUTTRESS_REG_FW_RESET_CTL);
	/* leave some time for HW restore */
	usleep_range(800, 1000);

	return 0;
}

int ipu_buttress_authenticate(struct ipu7_device *isp)
{
	struct ipu_buttress *b = &isp->buttress;
	struct device *dev = &isp->pdev->dev;
	u32 data, mask, done, fail;
	int ret;

	if (!isp->secure_mode) {
		dev_dbg(dev, "Skip auth for non-secure mode\n");
		return 0;
	}

	mutex_lock(&b->auth_mutex);

	if (ipu_buttress_auth_done(isp)) {
		ret = 0;
		goto out_unlock;
	}

	/*
	 * BUTTRESS_REG_FW_SOURCE_BASE needs to be set with FW CPD
	 * package address for secure mode.
	 */

	writel(isp->cpd_fw->size, isp->base + BUTTRESS_REG_FW_SOURCE_SIZE);
	writel(sg_dma_address(isp->psys->fw_sgt.sgl),
	       isp->base + BUTTRESS_REG_FW_SOURCE_BASE);

	/*
	 * Write boot_load into IU2CSEDATA0
	 * Write sizeof(boot_load) | 0x2 << CLIENT_ID to
	 * IU2CSEDB.IU2CSECMD and set IU2CSEDB.IU2CSEBUSY as
	 */
	dev_info(dev, "Sending BOOT_LOAD to CSE\n");
	ret = ipu_buttress_ipc_send(isp, BUTTRESS_IU2CSEDATA0_IPC_BOOT_LOAD,
				    1, true,
				    BUTTRESS_CSE2IUDATA0_IPC_BOOT_LOAD_DONE);
	if (ret) {
		dev_err(dev, "CSE boot_load failed\n");
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
		dev_err(dev, "CSE boot_load timeout\n");
		goto out_unlock;
	}

	if ((data & mask) == fail) {
		dev_err(dev, "CSE auth failed\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = readl_poll_timeout(isp->base + BOOTLOADER_STATUS_OFFSET,
				 data, data == BOOTLOADER_MAGIC_KEY, 500,
				 BUTTRESS_CSE_BOOTLOAD_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Unexpected magic number 0x%x\n", data);
		goto out_unlock;
	}

	/*
	 * Write authenticate_run into IU2CSEDATA0
	 * Write sizeof(boot_load) | 0x2 << CLIENT_ID to
	 * IU2CSEDB.IU2CSECMD and set IU2CSEDB.IU2CSEBUSY as
	 */
	dev_info(dev, "Sending AUTHENTICATE_RUN to CSE\n");
	ret = ipu_buttress_ipc_send(isp, BUTTRESS_IU2CSEDATA0_IPC_AUTH_RUN,
				    1, true,
				    BUTTRESS_CSE2IUDATA0_IPC_AUTH_RUN_DONE);
	if (ret) {
		dev_err(dev, "CSE authenticate_run failed\n");
		goto out_unlock;
	}

	done = BUTTRESS_SECURITY_CTL_AUTH_DONE;
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_SECURITY_CTL, data,
				 ((data & mask) == done ||
				  (data & mask) == fail), 500,
				 BUTTRESS_CSE_AUTHENTICATE_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "CSE authenticate timeout\n");
		goto out_unlock;
	}

	if ((data & mask) == fail) {
		dev_err(dev, "CSE boot_load failed\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	dev_info(dev, "CSE authenticate_run done\n");

out_unlock:
	mutex_unlock(&b->auth_mutex);

	return ret;
}

static int ipu_buttress_send_tsc_request(struct ipu7_device *isp)
{
	u32 val, mask, done;
	int ret;

	mask = BUTTRESS_PWR_STATUS_HH_STATUS_MASK;

	writel(BUTTRESS_TSC_CMD_START_TSC_SYNC,
	       isp->base + BUTTRESS_REG_TSC_CMD);

	val = readl(isp->base + BUTTRESS_REG_PWR_STATUS);
	val = FIELD_GET(mask, val);
	if (val == BUTTRESS_PWR_STATUS_HH_STATE_ERR) {
		dev_err(&isp->pdev->dev, "Start tsc sync failed\n");
		return -EINVAL;
	}

	done = BUTTRESS_PWR_STATUS_HH_STATE_DONE;
	ret = readl_poll_timeout(isp->base + BUTTRESS_REG_PWR_STATUS, val,
				 FIELD_GET(mask, val) == done, 500,
				 BUTTRESS_TSC_SYNC_TIMEOUT_US);
	if (ret)
		dev_err(&isp->pdev->dev, "Start tsc sync timeout\n");

	return ret;
}

int ipu_buttress_start_tsc_sync(struct ipu7_device *isp)
{
	void __iomem *base = isp->base;
	unsigned int i;
	u32 val;

	if (is_ipu8(isp->hw_ver)) {
		for (i = 0; i < BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX; i++) {
			val = readl(base + BUTTRESS_REG_PB_TIMESTAMP_VALID);
			if (val == 1)
				return 0;
			usleep_range(40, 50);
		}

		dev_err(&isp->pdev->dev, "PB HH sync failed (valid %u)\n", val);
		return -ETIMEDOUT;
	}

	if (is_ipu7p5(isp->hw_ver)) {
		val = readl(base + BUTTRESS_REG_TSC_CTL);
		val |= BUTTRESS_SEL_PB_TIMESTAMP;
		writel(val, base + BUTTRESS_REG_TSC_CTL);

		for (i = 0; i < BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX; i++) {
			val = readl(base + BUTTRESS_REG_PB_TIMESTAMP_VALID);
			if (val == 1)
				return 0;
			usleep_range(40, 50);
		}

		dev_err(&isp->pdev->dev, "PB HH sync failed (valid %u)\n", val);

		return -ETIMEDOUT;
	}

	for (i = 0; i < BUTTRESS_TSC_SYNC_RESET_TRIAL_MAX; i++) {
		int ret;

		ret = ipu_buttress_send_tsc_request(isp);
		if (ret != -ETIMEDOUT)
			return ret;

		val = readl(base + BUTTRESS_REG_TSC_CTL);
		val = val | BUTTRESS_TSW_WA_SOFT_RESET;
		writel(val, base + BUTTRESS_REG_TSC_CTL);
		val = val & (~BUTTRESS_TSW_WA_SOFT_RESET);
		writel(val, base + BUTTRESS_REG_TSC_CTL);
	}

	dev_err(&isp->pdev->dev, "TSC sync failed (timeout)\n");

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_start_tsc_sync, "INTEL_IPU7");

void ipu_buttress_tsc_read(struct ipu7_device *isp, u64 *val)
{
	unsigned long flags;
	u32 tsc_hi, tsc_lo;

	local_irq_save(flags);
	if (is_ipu7(isp->hw_ver)) {
		tsc_lo = readl(isp->base + BUTTRESS_REG_TSC_LO);
		tsc_hi = readl(isp->base + BUTTRESS_REG_TSC_HI);
	} else {
		tsc_lo = readl(isp->base + BUTTRESS_REG_PB_TIMESTAMP_LO);
		tsc_hi = readl(isp->base + BUTTRESS_REG_PB_TIMESTAMP_HI);
	}
	*val = (u64)tsc_hi << 32 | tsc_lo;
	local_irq_restore(flags);
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_tsc_read, "INTEL_IPU7");

u64 ipu_buttress_tsc_ticks_to_ns(u64 ticks, const struct ipu7_device *isp)
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
EXPORT_SYMBOL_NS_GPL(ipu_buttress_tsc_ticks_to_ns, "INTEL_IPU7");

/* trigger uc control to wakeup fw */
void ipu_buttress_wakeup_is_uc(const struct ipu7_device *isp)
{
	u32 val;

	val = readl(isp->base + BUTTRESS_REG_DRV_IS_UCX_CONTROL_STATUS);
	val |= UCX_CTL_WAKEUP;
	writel(val, isp->base + BUTTRESS_REG_DRV_IS_UCX_CONTROL_STATUS);
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_wakeup_is_uc, "INTEL_IPU7");

void ipu_buttress_wakeup_ps_uc(const struct ipu7_device *isp)
{
	u32 val;

	val = readl(isp->base + BUTTRESS_REG_DRV_PS_UCX_CONTROL_STATUS);
	val |= UCX_CTL_WAKEUP;
	writel(val, isp->base + BUTTRESS_REG_DRV_PS_UCX_CONTROL_STATUS);
}
EXPORT_SYMBOL_NS_GPL(ipu_buttress_wakeup_ps_uc, "INTEL_IPU7");

static const struct x86_cpu_id ipu_misc_cfg_exclusion[] = {
	X86_MATCH_VFM_STEPS(INTEL_PANTHERLAKE_L, 0x1, 0x1, 0),
	{},
};

static void ipu_buttress_setup(struct ipu7_device *isp)
{
	struct device *dev = &isp->pdev->dev;
	u32 val;

	/* program PB BAR */
#define WRXREQOP_OVRD_VAL_MASK  GENMASK(22, 19)
	writel(0, isp->pb_base + GLOBAL_INTERRUPT_MASK);
	val = readl(isp->pb_base + BAR2_MISC_CONFIG);
	if (is_ipu7(isp->hw_ver) || x86_match_cpu(ipu_misc_cfg_exclusion))
		val |= 0x100U;
	else
		val |= FIELD_PREP(WRXREQOP_OVRD_VAL_MASK, 0xf) |
			BIT(18) | 0x100U;

	writel(val, isp->pb_base + BAR2_MISC_CONFIG);
	val = readl(isp->pb_base + BAR2_MISC_CONFIG);

	if (is_ipu8(isp->hw_ver)) {
		writel(BIT(13), isp->pb_base + TLBID_HASH_ENABLE_63_32);
		writel(BIT(9), isp->pb_base + TLBID_HASH_ENABLE_95_64);
		dev_dbg(dev, "IPU8 TLBID_HASH %x %x\n",
			readl(isp->pb_base + TLBID_HASH_ENABLE_63_32),
			readl(isp->pb_base + TLBID_HASH_ENABLE_95_64));
	} else if (is_ipu7p5(isp->hw_ver)) {
		writel(BIT(14), isp->pb_base + TLBID_HASH_ENABLE_63_32);
		writel(BIT(9), isp->pb_base + TLBID_HASH_ENABLE_95_64);
		dev_dbg(dev, "IPU7P5 TLBID_HASH %x %x\n",
			readl(isp->pb_base + TLBID_HASH_ENABLE_63_32),
			readl(isp->pb_base + TLBID_HASH_ENABLE_95_64));
	} else {
		writel(BIT(22), isp->pb_base + TLBID_HASH_ENABLE_63_32);
		writel(BIT(1), isp->pb_base + TLBID_HASH_ENABLE_127_96);
		dev_dbg(dev, "TLBID_HASH %x %x\n",
			readl(isp->pb_base + TLBID_HASH_ENABLE_63_32),
			readl(isp->pb_base + TLBID_HASH_ENABLE_127_96));
	}

	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_IRQ_CLEAR);
	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_IRQ_MASK);
	writel(BUTTRESS_IRQS, isp->base + BUTTRESS_REG_IRQ_ENABLE);
	/* LNL SW workaround for PS PD hang when PS sub-domain during PD */
	writel(PS_FSM_CG, isp->base + BUTTRESS_REG_CG_CTRL_BITS);
}

void ipu_buttress_restore(struct ipu7_device *isp)
{
	struct ipu_buttress *b = &isp->buttress;

	ipu_buttress_setup(isp);

	writel(b->wdt_cached_value, isp->base + BUTTRESS_REG_IDLE_WDT);
}

int ipu_buttress_init(struct ipu7_device *isp)
{
	int ret, ipc_reset_retry = BUTTRESS_CSE_IPC_RESET_RETRY;
	struct ipu_buttress *b = &isp->buttress;
	struct device *dev = &isp->pdev->dev;
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

	isp->secure_mode = ipu_buttress_get_secure_mode(isp);
	val = readl(isp->base + BUTTRESS_REG_IPU_SKU);
	dev_info(dev, "IPU%u SKU %u in %s mode mask 0x%x\n", val & 0xfU,
		 (val >> 4) & 0x7U, isp->secure_mode ? "secure" : "non-secure",
		 readl(isp->base + BUTTRESS_REG_CAMERA_MASK));
	b->wdt_cached_value = readl(isp->base + BUTTRESS_REG_IDLE_WDT);
	b->ref_clk = 384;

	ipu_buttress_setup(isp);

	/* Retry couple of times in case of CSE initialization is delayed */
	do {
		ret = ipu_buttress_ipc_reset(isp, &b->cse);
		if (ret) {
			dev_warn(dev, "IPC reset protocol failed, retrying\n");
		} else {
			dev_dbg(dev, "IPC reset done\n");
			return 0;
		}
	} while (ipc_reset_retry--);

	dev_err(dev, "IPC reset protocol failed\n");

	mutex_destroy(&b->power_mutex);
	mutex_destroy(&b->auth_mutex);
	mutex_destroy(&b->cons_mutex);
	mutex_destroy(&b->ipc_mutex);

	return ret;
}

void ipu_buttress_exit(struct ipu7_device *isp)
{
	struct ipu_buttress *b = &isp->buttress;

	writel(0, isp->base + BUTTRESS_REG_IRQ_ENABLE);
	mutex_destroy(&b->power_mutex);
	mutex_destroy(&b->auth_mutex);
	mutex_destroy(&b->cons_mutex);
	mutex_destroy(&b->ipc_mutex);
}
