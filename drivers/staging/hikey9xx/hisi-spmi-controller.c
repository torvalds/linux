// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/spmi.h>

#define SPMI_CONTROLLER_NAME		"spmi_controller"

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

/*
 * @base base address of the PMIC Arbiter core registers.
 * @rdbase, @wrbase base address of the PMIC Arbiter read core registers.
 *     For HW-v1 these are equal to base.
 *     For HW-v2, the value is the same in eeraly probing, in order to read
 *     PMIC_ARB_CORE registers, then chnls, and obsrvr are set to
 *     PMIC_ARB_CORE_REGISTERS and PMIC_ARB_CORE_REGISTERS_OBS respectivly.
 * @intr base address of the SPMI interrupt control registers
 * @ppid_2_chnl_tbl lookup table f(SID, Periph-ID) -> channel num
 *      entry is only valid if corresponding bit is set in valid_ppid_bitmap.
 * @valid_ppid_bitmap bit is set only for valid ppids.
 * @fmt_cmd formats a command to be set into PMIC_ARBq_CHNLn_CMD
 * @chnl_ofst calculates offset of the base of a channel reg space
 * @ee execution environment id
 * @irq_acc0_init_val initial value of the interrupt accumulator at probe time.
 *      Use for an HW workaround. On handling interrupts, the first accumulator
 *      register will be compared against this value, and bits which are set at
 *      boot will be ignored.
 * @reserved_chnl entry of ppid_2_chnl_tbl that this driver should never touch.
 *      value is positive channel number or negative to mark it unused.
 */
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
	u32 status = 0;
	u32 timeout = SPMI_CONTROLLER_TIMEOUT_US;
	u32 offset;

	offset  = SPMI_APB_SPMI_STATUS_BASE_ADDR;
	offset += SPMI_CHANNEL_OFFSET * ctrl_dev->channel + SPMI_SLAVE_OFFSET * sid;

	while (timeout--) {
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
	}

	dev_err(dev, "%s: timeout, status 0x%x\n", __func__, status);
	return -ETIMEDOUT;
}

static int spmi_read_cmd(struct spmi_controller *ctrl,
			 u8 opc, u8 sid, u16 addr, u8 *__buf, size_t bc)
{
	struct spmi_controller_dev *spmi_controller = dev_get_drvdata(&ctrl->dev);
	unsigned long flags;
	u8 *buf = __buf;
	u32 cmd, data;
	int rc;
	u32 chnl_ofst = SPMI_CHANNEL_OFFSET * spmi_controller->channel;
	u8 op_code, i;

	if (bc > SPMI_CONTROLLER_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"spmi_controller supports 1..%d bytes per trans, but:%ld requested",
			SPMI_CONTROLLER_MAX_TRANS_BYTES, bc);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc == SPMI_CMD_READ) {
		op_code = SPMI_CMD_REG_READ;
	} else if (opc == SPMI_CMD_EXT_READ) {
		op_code = SPMI_CMD_EXT_REG_READ;
	} else if (opc == SPMI_CMD_EXT_READL) {
		op_code = SPMI_CMD_EXT_REG_READ_L;
	} else {
		dev_err(&ctrl->dev, "invalid read cmd 0x%x", opc);
		return -EINVAL;
	}

	cmd = SPMI_APB_SPMI_CMD_EN |
	     (op_code << SPMI_APB_SPMI_CMD_TYPE_OFFSET) |
	     ((bc - 1) << SPMI_APB_SPMI_CMD_LENGTH_OFFSET) |
	     ((sid & 0xf) << SPMI_APB_SPMI_CMD_SLAVEID_OFFSET) |  /* slvid */
	     ((addr & 0xffff)  << SPMI_APB_SPMI_CMD_ADDR_OFFSET); /* slave_addr */

	spin_lock_irqsave(&spmi_controller->lock, flags);

	writel(cmd, spmi_controller->base + chnl_ofst + SPMI_APB_SPMI_CMD_BASE_ADDR);

	rc = spmi_controller_wait_for_done(&ctrl->dev, spmi_controller,
					   spmi_controller->base, sid, addr);
	if (rc)
		goto done;

	i = 0;
	do {
		data = readl(spmi_controller->base + chnl_ofst + SPMI_SLAVE_OFFSET * sid + SPMI_APB_SPMI_RDATA0_BASE_ADDR + i * SPMI_PER_DATAREG_BYTE);
		data = be32_to_cpu((__be32)data);
		if ((bc - i * SPMI_PER_DATAREG_BYTE) >> 2) {
			memcpy(buf, &data, sizeof(data));
			buf += sizeof(data);
		} else {
			memcpy(buf, &data, bc % SPMI_PER_DATAREG_BYTE);
			buf += (bc % SPMI_PER_DATAREG_BYTE);
		}
		i++;
	} while (bc > i * SPMI_PER_DATAREG_BYTE);

done:
	spin_unlock_irqrestore(&spmi_controller->lock, flags);
	if (rc)
		dev_err(&ctrl->dev,
			"spmi read wait timeout op:0x%x sid:%d addr:0x%x bc:%ld\n",
			opc, sid, addr, bc + 1);
	else
		dev_dbg(&ctrl->dev, "%s: id:%d addr:0x%x, read value: %*ph\n",
			__func__, sid, addr, (int)bc, __buf);

	return rc;
}

static int spmi_write_cmd(struct spmi_controller *ctrl,
			  u8 opc, u8 sid, u16 addr, const u8 *__buf, size_t bc)
{
	struct spmi_controller_dev *spmi_controller = dev_get_drvdata(&ctrl->dev);
	const u8 *buf = __buf;
	unsigned long flags;
	u32 cmd, data;
	int rc;
	u32 chnl_ofst = SPMI_CHANNEL_OFFSET * spmi_controller->channel;
	u8 op_code, i;

	if (bc > SPMI_CONTROLLER_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"spmi_controller supports 1..%d bytes per trans, but:%ld requested",
			SPMI_CONTROLLER_MAX_TRANS_BYTES, bc);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc == SPMI_CMD_WRITE) {
		op_code = SPMI_CMD_REG_WRITE;
	} else if (opc == SPMI_CMD_EXT_WRITE) {
		op_code = SPMI_CMD_EXT_REG_WRITE;
	} else if (opc == SPMI_CMD_EXT_WRITEL) {
		op_code = SPMI_CMD_EXT_REG_WRITE_L;
	} else {
		dev_err(&ctrl->dev, "invalid write cmd 0x%x", opc);
		return -EINVAL;
	}

	cmd = SPMI_APB_SPMI_CMD_EN |
	      (op_code << SPMI_APB_SPMI_CMD_TYPE_OFFSET) |
	      ((bc - 1) << SPMI_APB_SPMI_CMD_LENGTH_OFFSET) |
	      ((sid & 0xf) << SPMI_APB_SPMI_CMD_SLAVEID_OFFSET) |  /* slvid */
	      ((addr & 0xffff)  << SPMI_APB_SPMI_CMD_ADDR_OFFSET); /* slave_addr */

	/* Write data to FIFOs */
	spin_lock_irqsave(&spmi_controller->lock, flags);

	i = 0;
	do {
		data = 0;
		if ((bc - i * SPMI_PER_DATAREG_BYTE) >> 2) {
			memcpy(&data, buf, sizeof(data));
			buf += sizeof(data);
		} else {
			memcpy(&data, buf, bc % SPMI_PER_DATAREG_BYTE);
			buf += (bc % SPMI_PER_DATAREG_BYTE);
		}

		writel((u32)cpu_to_be32(data),
		       spmi_controller->base + chnl_ofst + SPMI_APB_SPMI_WDATA0_BASE_ADDR + SPMI_PER_DATAREG_BYTE * i);
		i++;
	} while (bc > i * SPMI_PER_DATAREG_BYTE);

	/* Start the transaction */
	writel(cmd, spmi_controller->base + chnl_ofst + SPMI_APB_SPMI_CMD_BASE_ADDR);

	rc = spmi_controller_wait_for_done(&ctrl->dev, spmi_controller,
					   spmi_controller->base, sid, addr);
	spin_unlock_irqrestore(&spmi_controller->lock, flags);

	if (rc)
		dev_err(&ctrl->dev, "spmi write wait timeout op:0x%x sid:%d addr:0x%x bc:%ld\n",
			opc, sid, addr, bc);
	else
		dev_dbg(&ctrl->dev, "%s: id:%d addr:0x%x, wrote value: %*ph\n",
			__func__, sid, addr, (int)bc, __buf);

	return rc;
}

static int spmi_controller_probe(struct platform_device *pdev)
{
	struct spmi_controller_dev *spmi_controller;
	struct spmi_controller *ctrl;
	struct resource *iores;
	int ret = 0;

	dev_info(&pdev->dev, "HISI SPMI probe\n");

	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*spmi_controller));
	if (!ctrl) {
		dev_err(&pdev->dev, "can not allocate spmi_controller data\n");
		return -ENOMEM;
	}
	spmi_controller = spmi_controller_get_drvdata(ctrl);
	spmi_controller->controller = ctrl;

	/* NOTE: driver uses the static register mapping */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores) {
		dev_err(&pdev->dev, "can not get resource!\n");
		return -EINVAL;
	}

	spmi_controller->base = ioremap(iores->start, resource_size(iores));
	if (!spmi_controller->base) {
		dev_err(&pdev->dev, "can not remap base addr!\n");
		return -EADDRNOTAVAIL;
	}
	dev_dbg(&pdev->dev, "spmi_add_controller base addr=0x%lx!\n",
		(unsigned long)spmi_controller->base);

	/* Get properties from the device tree */
	ret = of_property_read_u32(pdev->dev.of_node, "spmi-channel",
				   &spmi_controller->channel);
	if (ret) {
		dev_err(&pdev->dev, "can not get channel\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, spmi_controller);
	dev_set_drvdata(&ctrl->dev, spmi_controller);

	spin_lock_init(&spmi_controller->lock);

	ctrl->nr = spmi_controller->channel;
	ctrl->dev.parent = pdev->dev.parent;
	ctrl->dev.of_node = of_node_get(pdev->dev.of_node);

	/* Callbacks */
	ctrl->read_cmd = spmi_read_cmd;
	ctrl->write_cmd = spmi_write_cmd;

	ret = spmi_controller_add(ctrl);
	if (ret)
		goto err_add_controller;

	dev_info(&pdev->dev, "spmi_add_controller initialized\n");
	return 0;

err_add_controller:
	dev_err(&pdev->dev, "spmi_add_controller failed!\n");
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int spmi_del_controller(struct platform_device *pdev)
{
	struct spmi_controller *ctrl = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	spmi_controller_remove(ctrl);
	return 0;
}

static const struct of_device_id spmi_controller_match_table[] = {
	{	.compatible = "hisilicon,spmi-controller",
	},
	{}
};
MODULE_DEVICE_TABLE(of, spmi_controller_match_table);

static struct platform_driver spmi_controller_driver = {
	.probe		= spmi_controller_probe,
	.remove		= spmi_del_controller,
	.driver		= {
		.name	= SPMI_CONTROLLER_NAME,
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
MODULE_ALIAS("platform:spmi_controlller");
