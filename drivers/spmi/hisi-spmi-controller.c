// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spmi.h>

/*
 * SPMI register addr
 */
#define SPMI_CHANNEL_OFFSET				0x0300
#define SPMI_SLAVE_OFFSET				0x20

#define SPMI_APB_SPMI_CMD_BASE_ADDR			0x0100

#define SPMI_APB_SPMI_WDATA0_BASE_ADDR			0x0104
#define SPMI_APB_SPMI_WDATA1_BASE_ADDR			0x0108
#define SPMI_APB_SPMI_WDATA2_BASE_ADDR			0x010c
#define SPMI_APB_SPMI_WDATA3_BASE_ADDR			0x0110

#define SPMI_APB_SPMI_STATUS_BASE_ADDR			0x0200

#define SPMI_APB_SPMI_RDATA0_BASE_ADDR			0x0204
#define SPMI_APB_SPMI_RDATA1_BASE_ADDR			0x0208
#define SPMI_APB_SPMI_RDATA2_BASE_ADDR			0x020c
#define SPMI_APB_SPMI_RDATA3_BASE_ADDR			0x0210

#define SPMI_PER_DATAREG_BYTE				4
/*
 * SPMI cmd register
 */
#define SPMI_APB_SPMI_CMD_EN				BIT(31)
#define SPMI_APB_SPMI_CMD_TYPE_OFFSET			24
#define SPMI_APB_SPMI_CMD_LENGTH_OFFSET			20
#define SPMI_APB_SPMI_CMD_SLAVEID_OFFSET		16
#define SPMI_APB_SPMI_CMD_ADDR_OFFSET			0

/* Command Opcodes */

enum spmi_controller_cmd_op_code {
	SPMI_CMD_REG_ZERO_WRITE = 0,
	SPMI_CMD_REG_WRITE = 1,
	SPMI_CMD_REG_READ = 2,
	SPMI_CMD_EXT_REG_WRITE = 3,
	SPMI_CMD_EXT_REG_READ = 4,
	SPMI_CMD_EXT_REG_WRITE_L = 5,
	SPMI_CMD_EXT_REG_READ_L = 6,
	SPMI_CMD_REG_RESET = 7,
	SPMI_CMD_REG_SLEEP = 8,
	SPMI_CMD_REG_SHUTDOWN = 9,
	SPMI_CMD_REG_WAKEUP = 10,
};

/*
 * SPMI status register
 */
#define SPMI_APB_TRANS_DONE			BIT(0)
#define SPMI_APB_TRANS_FAIL			BIT(2)

/* Command register fields */
#define SPMI_CONTROLLER_CMD_MAX_BYTE_COUNT	16

/* Maximum number of support PMIC peripherals */
#define SPMI_CONTROLLER_TIMEOUT_US		1000
#define SPMI_CONTROLLER_MAX_TRANS_BYTES		16

struct spmi_controller_dev {
	struct spmi_controller	*controller;
	struct device		*dev;
	void __iomem		*base;
	spinlock_t		lock;
	u32			channel;
};

static int spmi_controller_wait_for_done(struct device *dev,
					 struct spmi_controller_dev *ctrl_dev,
					 void __iomem *base, u8 sid, u16 addr)
{
	u32 timeout = SPMI_CONTROLLER_TIMEOUT_US;
	u32 status, offset;

	offset  = SPMI_APB_SPMI_STATUS_BASE_ADDR;
	offset += SPMI_CHANNEL_OFFSET * ctrl_dev->channel + SPMI_SLAVE_OFFSET * sid;

	do {
		status = readl(base + offset);

		if (status & SPMI_APB_TRANS_DONE) {
			if (status & SPMI_APB_TRANS_FAIL) {
				dev_err(dev, "%s: transaction failed (0x%x)\n",
					__func__, status);
				return -EIO;
			}
			dev_dbg(dev, "%s: status 0x%x\n", __func__, status);
			return 0;
		}
		udelay(1);
	} while (timeout--);

	dev_err(dev, "%s: timeout, status 0x%x\n", __func__, status);
	return -ETIMEDOUT;
}

static int spmi_read_cmd(struct spmi_controller *ctrl,
			 u8 opc, u8 slave_id, u16 slave_addr, u8 *__buf, size_t bc)
{
	struct spmi_controller_dev *spmi_controller = dev_get_drvdata(&ctrl->dev);
	u32 chnl_ofst = SPMI_CHANNEL_OFFSET * spmi_controller->channel;
	unsigned long flags;
	u8 *buf = __buf;
	u32 cmd, data;
	int rc;
	u8 op_code, i;

	if (bc > SPMI_CONTROLLER_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"spmi_controller supports 1..%d bytes per trans, but:%zu requested\n",
			SPMI_CONTROLLER_MAX_TRANS_BYTES, bc);
		return  -EINVAL;
	}

	switch (opc) {
	case SPMI_CMD_READ:
		op_code = SPMI_CMD_REG_READ;
		break;
	case SPMI_CMD_EXT_READ:
		op_code = SPMI_CMD_EXT_REG_READ;
		break;
	case SPMI_CMD_EXT_READL:
		op_code = SPMI_CMD_EXT_REG_READ_L;
		break;
	default:
		dev_err(&ctrl->dev, "invalid read cmd 0x%x\n", opc);
		return -EINVAL;
	}

	cmd = SPMI_APB_SPMI_CMD_EN |
	     (op_code << SPMI_APB_SPMI_CMD_TYPE_OFFSET) |
	     ((bc - 1) << SPMI_APB_SPMI_CMD_LENGTH_OFFSET) |
	     ((slave_id & 0xf) << SPMI_APB_SPMI_CMD_SLAVEID_OFFSET) |  /* slvid */
	     ((slave_addr & 0xffff)  << SPMI_APB_SPMI_CMD_ADDR_OFFSET); /* slave_addr */

	spin_lock_irqsave(&spmi_controller->lock, flags);

	writel(cmd, spmi_controller->base + chnl_ofst + SPMI_APB_SPMI_CMD_BASE_ADDR);

	rc = spmi_controller_wait_for_done(&ctrl->dev, spmi_controller,
					   spmi_controller->base, slave_id, slave_addr);
	if (rc)
		goto done;

	for (i = 0; bc > i * SPMI_PER_DATAREG_BYTE; i++) {
		data = readl(spmi_controller->base + chnl_ofst +
			     SPMI_SLAVE_OFFSET * slave_id +
			     SPMI_APB_SPMI_RDATA0_BASE_ADDR +
			     i * SPMI_PER_DATAREG_BYTE);
		data = be32_to_cpu((__be32 __force)data);
		if ((bc - i * SPMI_PER_DATAREG_BYTE) >> 2) {
			memcpy(buf, &data, sizeof(data));
			buf += sizeof(data);
		} else {
			memcpy(buf, &data, bc % SPMI_PER_DATAREG_BYTE);
			buf += (bc % SPMI_PER_DATAREG_BYTE);
		}
	}

done:
	spin_unlock_irqrestore(&spmi_controller->lock, flags);
	if (rc)
		dev_err(&ctrl->dev,
			"spmi read wait timeout op:0x%x slave_id:%d slave_addr:0x%x bc:%zu\n",
			opc, slave_id, slave_addr, bc + 1);
	else
		dev_dbg(&ctrl->dev, "%s: id:%d slave_addr:0x%x, read value: %*ph\n",
			__func__, slave_id, slave_addr, (int)bc, __buf);

	return rc;
}

static int spmi_write_cmd(struct spmi_controller *ctrl,
			  u8 opc, u8 slave_id, u16 slave_addr, const u8 *__buf, size_t bc)
{
	struct spmi_controller_dev *spmi_controller = dev_get_drvdata(&ctrl->dev);
	u32 chnl_ofst = SPMI_CHANNEL_OFFSET * spmi_controller->channel;
	const u8 *buf = __buf;
	unsigned long flags;
	u32 cmd, data;
	int rc;
	u8 op_code, i;

	if (bc > SPMI_CONTROLLER_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"spmi_controller supports 1..%d bytes per trans, but:%zu requested\n",
			SPMI_CONTROLLER_MAX_TRANS_BYTES, bc);
		return  -EINVAL;
	}

	switch (opc) {
	case SPMI_CMD_WRITE:
		op_code = SPMI_CMD_REG_WRITE;
		break;
	case SPMI_CMD_EXT_WRITE:
		op_code = SPMI_CMD_EXT_REG_WRITE;
		break;
	case SPMI_CMD_EXT_WRITEL:
		op_code = SPMI_CMD_EXT_REG_WRITE_L;
		break;
	default:
		dev_err(&ctrl->dev, "invalid write cmd 0x%x\n", opc);
		return -EINVAL;
	}

	cmd = SPMI_APB_SPMI_CMD_EN |
	      (op_code << SPMI_APB_SPMI_CMD_TYPE_OFFSET) |
	      ((bc - 1) << SPMI_APB_SPMI_CMD_LENGTH_OFFSET) |
	      ((slave_id & 0xf) << SPMI_APB_SPMI_CMD_SLAVEID_OFFSET) |
	      ((slave_addr & 0xffff)  << SPMI_APB_SPMI_CMD_ADDR_OFFSET);

	/* Write data to FIFOs */
	spin_lock_irqsave(&spmi_controller->lock, flags);

	for (i = 0; bc > i * SPMI_PER_DATAREG_BYTE; i++) {
		data = 0;
		if ((bc - i * SPMI_PER_DATAREG_BYTE) >> 2) {
			memcpy(&data, buf, sizeof(data));
			buf += sizeof(data);
		} else {
			memcpy(&data, buf, bc % SPMI_PER_DATAREG_BYTE);
			buf += (bc % SPMI_PER_DATAREG_BYTE);
		}

		writel((u32 __force)cpu_to_be32(data),
		       spmi_controller->base + chnl_ofst +
		       SPMI_APB_SPMI_WDATA0_BASE_ADDR +
		       SPMI_PER_DATAREG_BYTE * i);
	}

	/* Start the transaction */
	writel(cmd, spmi_controller->base + chnl_ofst + SPMI_APB_SPMI_CMD_BASE_ADDR);

	rc = spmi_controller_wait_for_done(&ctrl->dev, spmi_controller,
					   spmi_controller->base, slave_id,
					   slave_addr);
	spin_unlock_irqrestore(&spmi_controller->lock, flags);

	if (rc)
		dev_err(&ctrl->dev, "spmi write wait timeout op:0x%x slave_id:%d slave_addr:0x%x bc:%zu\n",
			opc, slave_id, slave_addr, bc);
	else
		dev_dbg(&ctrl->dev, "%s: id:%d slave_addr:0x%x, wrote value: %*ph\n",
			__func__, slave_id, slave_addr, (int)bc, __buf);

	return rc;
}

static int spmi_controller_probe(struct platform_device *pdev)
{
	struct spmi_controller_dev *spmi_controller;
	struct spmi_controller *ctrl;
	struct resource *iores;
	int ret;

	ctrl = devm_spmi_controller_alloc(&pdev->dev, sizeof(*spmi_controller));
	if (IS_ERR(ctrl)) {
		dev_err(&pdev->dev, "can not allocate spmi_controller data\n");
		return PTR_ERR(ctrl);
	}
	spmi_controller = spmi_controller_get_drvdata(ctrl);
	spmi_controller->controller = ctrl;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores) {
		dev_err(&pdev->dev, "can not get resource!\n");
		return -EINVAL;
	}

	spmi_controller->base = devm_ioremap(&pdev->dev, iores->start,
					     resource_size(iores));
	if (!spmi_controller->base) {
		dev_err(&pdev->dev, "can not remap base addr!\n");
		return -EADDRNOTAVAIL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "hisilicon,spmi-channel",
				   &spmi_controller->channel);
	if (ret) {
		dev_err(&pdev->dev, "can not get channel\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, spmi_controller);
	dev_set_drvdata(&ctrl->dev, spmi_controller);

	spin_lock_init(&spmi_controller->lock);

	ctrl->dev.parent = pdev->dev.parent;
	ctrl->dev.of_node = of_node_get(pdev->dev.of_node);

	/* Callbacks */
	ctrl->read_cmd = spmi_read_cmd;
	ctrl->write_cmd = spmi_write_cmd;

	ret = devm_spmi_controller_add(&pdev->dev, ctrl);
	if (ret) {
		dev_err(&pdev->dev, "spmi_controller_add failed with error %d!\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id spmi_controller_match_table[] = {
	{
		.compatible = "hisilicon,kirin970-spmi-controller",
	},
	{}
};
MODULE_DEVICE_TABLE(of, spmi_controller_match_table);

static struct platform_driver spmi_controller_driver = {
	.probe		= spmi_controller_probe,
	.driver		= {
		.name	= "hisi_spmi_controller",
		.of_match_table = spmi_controller_match_table,
	},
};

static int __init spmi_controller_init(void)
{
	return platform_driver_register(&spmi_controller_driver);
}
postcore_initcall(spmi_controller_init);

static void __exit spmi_controller_exit(void)
{
	platform_driver_unregister(&spmi_controller_driver);
}
module_exit(spmi_controller_exit);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:spmi_controller");
