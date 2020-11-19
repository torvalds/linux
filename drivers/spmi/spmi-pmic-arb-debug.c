// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2012-2018, 2020, The Linux Foundation. All rights reserved. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>

/* PMIC Arbiter debug register offsets */
#define PMIC_ARB_DEBUG_CMD0		0x00
#define PMIC_ARB_DEBUG_CMD1		0x04
#define PMIC_ARB_DEBUG_CMD2		0x08
#define PMIC_ARB_DEBUG_CMD3		0x0C
#define PMIC_ARB_DEBUG_STATUS		0x14
#define PMIC_ARB_DEBUG_WDATA(n)		(0x18 + 4 * (n))
#define PMIC_ARB_DEBUG_RDATA(n)		(0x38 + 4 * (n))

/* Transaction status flag bits */
enum pmic_arb_chnl_status {
	PMIC_ARB_STATUS_DONE		= BIT(0),
	PMIC_ARB_STATUS_FAILURE		= BIT(1),
	PMIC_ARB_STATUS_DENIED		= BIT(2),
	PMIC_ARB_STATUS_DROPPED		= BIT(3),
};

/* Command Opcodes */
enum pmic_arb_cmd_op_code {
	PMIC_ARB_OP_EXT_WRITEL		= 0,
	PMIC_ARB_OP_EXT_READL		= 1,
	PMIC_ARB_OP_EXT_WRITE		= 2,
	PMIC_ARB_OP_RESET		= 3,
	PMIC_ARB_OP_SLEEP		= 4,
	PMIC_ARB_OP_SHUTDOWN		= 5,
	PMIC_ARB_OP_WAKEUP		= 6,
	PMIC_ARB_OP_AUTHENTICATE	= 7,
	PMIC_ARB_OP_MSTR_READ		= 8,
	PMIC_ARB_OP_MSTR_WRITE		= 9,
	PMIC_ARB_OP_EXT_READ		= 13,
	PMIC_ARB_OP_WRITE		= 14,
	PMIC_ARB_OP_READ		= 15,
	PMIC_ARB_OP_ZERO_WRITE		= 16,
};

#define PMIC_ARB_TIMEOUT_US		100
#define PMIC_ARB_MAX_TRANS_BYTES	8
#define PMIC_ARB_MAX_SID		0xF

/**
 * spmi_pmic_arb_debug - SPMI PMIC Arbiter debug object
 *
 * @addr:		base address of SPMI PMIC arbiter debug module
 * @lock:		lock to synchronize accesses.
 */
struct spmi_pmic_arb_debug {
	void __iomem		*addr;
	raw_spinlock_t		lock;
	struct clk		*clock;
};

static inline void pmic_arb_debug_write(struct spmi_pmic_arb_debug *pa,
				u32 offset, u32 val)
{
	writel_relaxed(val, pa->addr + offset);
}

static inline u32 pmic_arb_debug_read(struct spmi_pmic_arb_debug *pa,
				u32 offset)
{
	return readl_relaxed(pa->addr + offset);
}

/* pa->lock must be held by the caller. */
static int pmic_arb_debug_wait_for_done(struct spmi_controller *ctrl)
{
	struct spmi_pmic_arb_debug *pa = spmi_controller_get_drvdata(ctrl);
	u32 status = 0;
	u32 timeout = PMIC_ARB_TIMEOUT_US;

	while (timeout--) {
		status = pmic_arb_debug_read(pa, PMIC_ARB_DEBUG_STATUS);

		if (status & PMIC_ARB_STATUS_DONE) {
			if (status & PMIC_ARB_STATUS_DENIED) {
				dev_err(&ctrl->dev, "%s: transaction denied (0x%x)\n",
					__func__, status);
				return -EPERM;
			}

			if (status & PMIC_ARB_STATUS_FAILURE) {
				dev_err(&ctrl->dev, "%s: transaction failed (0x%x)\n",
					__func__, status);
				return -EIO;
			}

			if (status & PMIC_ARB_STATUS_DROPPED) {
				dev_err(&ctrl->dev, "%s: transaction dropped (0x%x)\n",
					__func__, status);
				return -EIO;
			}

			return 0;
		}
		udelay(1);
	}

	dev_err(&ctrl->dev, "%s: timeout, status 0x%x\n", __func__, status);
	return -ETIMEDOUT;
}

/* pa->lock must be held by the caller. */
static int pmic_arb_debug_issue_command(struct spmi_controller *ctrl, u8 opc,
				u8 sid, u16 addr, size_t len)
{
	struct spmi_pmic_arb_debug *pa = spmi_controller_get_drvdata(ctrl);
	u16 pid       = (addr >> 8) & 0xFF;
	u16 offset    = addr & 0xFF;
	u8 byte_count = len - 1;

	if (byte_count >= PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev, "pmic-arb supports 1 to %d bytes per transaction, but %zu requested\n",
			PMIC_ARB_MAX_TRANS_BYTES, len);
		return  -EINVAL;
	}

	if (sid > PMIC_ARB_MAX_SID) {
		dev_err(&ctrl->dev, "pmic-arb supports sid 0 to %u, but %u requested\n",
			PMIC_ARB_MAX_SID, sid);
		return  -EINVAL;
	}

	pmic_arb_debug_write(pa, PMIC_ARB_DEBUG_CMD3, offset);
	pmic_arb_debug_write(pa, PMIC_ARB_DEBUG_CMD2, pid);
	pmic_arb_debug_write(pa, PMIC_ARB_DEBUG_CMD1, (byte_count << 4) | sid);

	/* Start the transaction */
	pmic_arb_debug_write(pa, PMIC_ARB_DEBUG_CMD0, opc << 1);

	return pmic_arb_debug_wait_for_done(ctrl);
}

/* Non-data command */
static int pmic_arb_debug_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	dev_dbg(&ctrl->dev, "cmd op:0x%x sid:%d\n", opc, sid);

	/* Check for valid non-data command */
	if (opc < SPMI_CMD_RESET || opc > SPMI_CMD_WAKEUP)
		return -EINVAL;

	return -EOPNOTSUPP;
}

static int pmic_arb_debug_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
				u16 addr, u8 *buf, size_t len)
{
	struct spmi_pmic_arb_debug *pa = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	int i, rc;

	/* Check the opcode */
	if (opc >= 0x60 && opc <= 0x7F)
		opc = PMIC_ARB_OP_READ;
	else if (opc >= 0x20 && opc <= 0x2F)
		opc = PMIC_ARB_OP_EXT_READ;
	else if (opc >= 0x38 && opc <= 0x3F)
		opc = PMIC_ARB_OP_EXT_READL;
	else
		return -EINVAL;

	rc = clk_prepare_enable(pa->clock);
	if (rc) {
		pr_err("%s: failed to enable core clock, rc=%d\n",
			__func__, rc);
		return rc;
	}
	raw_spin_lock_irqsave(&pa->lock, flags);

	rc = pmic_arb_debug_issue_command(ctrl, opc, sid, addr, len);
	if (rc)
		goto done;

	/* Read data from FIFO */
	for (i = 0; i < len; i++)
		buf[i] = pmic_arb_debug_read(pa, PMIC_ARB_DEBUG_RDATA(i));
done:
	raw_spin_unlock_irqrestore(&pa->lock, flags);
	clk_disable_unprepare(pa->clock);

	return rc;
}

static int pmic_arb_debug_write_cmd(struct spmi_controller *ctrl, u8 opc,
				u8 sid, u16 addr, const u8 *buf, size_t len)
{
	struct spmi_pmic_arb_debug *pa = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	int i, rc;

	if (len > PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev, "pmic-arb supports 1 to %d bytes per transaction, but %zu requested\n",
			PMIC_ARB_MAX_TRANS_BYTES, len);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x40 && opc <= 0x5F)
		opc = PMIC_ARB_OP_WRITE;
	else if (opc >= 0x00 && opc <= 0x0F)
		opc = PMIC_ARB_OP_EXT_WRITE;
	else if (opc >= 0x30 && opc <= 0x37)
		opc = PMIC_ARB_OP_EXT_WRITEL;
	else if (opc >= 0x80)
		opc = PMIC_ARB_OP_ZERO_WRITE;
	else
		return -EINVAL;

	rc = clk_prepare_enable(pa->clock);
	if (rc) {
		pr_err("%s: failed to enable core clock, rc=%d\n",
			__func__, rc);
		return rc;
	}
	raw_spin_lock_irqsave(&pa->lock, flags);

	/* Write data to FIFO */
	for (i = 0; i < len; i++)
		pmic_arb_debug_write(pa, PMIC_ARB_DEBUG_WDATA(i), buf[i]);

	rc = pmic_arb_debug_issue_command(ctrl, opc, sid, addr, len);

	raw_spin_unlock_irqrestore(&pa->lock, flags);
	clk_disable_unprepare(pa->clock);

	return rc;
}

static int spmi_pmic_arb_debug_probe(struct platform_device *pdev)
{
	struct spmi_pmic_arb_debug *pa;
	struct spmi_controller *ctrl;
	struct resource *res;
	int rc;
	u32 fuse_val, fuse_bit;
	void __iomem *fuse_addr;
	bool is_disable_fuse = true;

	/* Check if the debug bus is enabled or disabled by a fuse. */
	rc = of_property_read_u32(pdev->dev.of_node, "qcom,fuse-disable-bit",
				  &fuse_bit);
	if (rc) {
		is_disable_fuse = false;
		rc = of_property_read_u32(pdev->dev.of_node,
					  "qcom,fuse-enable-bit",
					  &fuse_bit);
	}
	if (!rc) {
		if (fuse_bit > 31) {
			dev_err(&pdev->dev, "qcom,fuse-%s-bit supports values 0 to 31, but %u specified\n",
				is_disable_fuse ? "disable" : "enable",
				fuse_bit);
			return -EINVAL;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "fuse");
		if (!res) {
			dev_err(&pdev->dev, "fuse address not specified\n");
			return -EINVAL;
		}

		fuse_addr = ioremap(res->start, resource_size(res));
		if (!fuse_addr)
			return -EINVAL;

		fuse_val = readl_relaxed(fuse_addr);
		iounmap(fuse_addr);

		if (!!(fuse_val & BIT(fuse_bit)) == is_disable_fuse) {
			dev_err(&pdev->dev, "SPMI PMIC arbiter debug bus disabled by fuse\n");
			return -ENODEV;
		}
	}


	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*pa));
	if (!ctrl)
		return -ENOMEM;

	pa = spmi_controller_get_drvdata(ctrl);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	if (!res) {
		dev_err(&pdev->dev, "core address not specified\n");
		rc = -EINVAL;
		goto err_put_ctrl;
	}

	pa->addr = devm_ioremap_resource(&ctrl->dev, res);
	if (IS_ERR(pa->addr)) {
		rc = PTR_ERR(pa->addr);
		goto err_put_ctrl;
	}

	if (of_find_property(pdev->dev.of_node, "clock-names", NULL)) {
		pa->clock = devm_clk_get(&pdev->dev, "core_clk");
		if (IS_ERR(pa->clock)) {
			rc = PTR_ERR(pa->clock);
			if (rc != -EPROBE_DEFER)
				dev_err(&pdev->dev, "unable to request core clock, rc=%d\n",
					rc);
			goto err_put_ctrl;
		}
	}

	platform_set_drvdata(pdev, ctrl);
	raw_spin_lock_init(&pa->lock);

	ctrl->cmd = pmic_arb_debug_cmd;
	ctrl->read_cmd = pmic_arb_debug_read_cmd;
	ctrl->write_cmd = pmic_arb_debug_write_cmd;

	rc = spmi_controller_add(ctrl);
	if (rc)
		goto err_put_ctrl;

	dev_info(&ctrl->dev, "SPMI PMIC arbiter debug bus controller added\n");

	return 0;

err_put_ctrl:
	spmi_controller_put(ctrl);
	return rc;
}

static int spmi_pmic_arb_debug_remove(struct platform_device *pdev)
{
	struct spmi_controller *ctrl = platform_get_drvdata(pdev);

	spmi_controller_remove(ctrl);
	spmi_controller_put(ctrl);

	return 0;
}

static const struct of_device_id spmi_pmic_arb_debug_match_table[] = {
	{ .compatible = "qcom,spmi-pmic-arb-debug", },
	{},
};
MODULE_DEVICE_TABLE(of, spmi_pmic_arb_debug_match_table);

static struct platform_driver spmi_pmic_arb_debug_driver = {
	.probe		= spmi_pmic_arb_debug_probe,
	.remove		= spmi_pmic_arb_debug_remove,
	.driver		= {
		.name	= "spmi_pmic_arb_debug",
		.of_match_table = spmi_pmic_arb_debug_match_table,
	},
};

module_platform_driver(spmi_pmic_arb_debug_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spmi_pmic_arb_debug");
