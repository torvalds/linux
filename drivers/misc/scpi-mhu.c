/*
 * Driver for the communication with System Control and Power processor using
 * Message Handling Unit present on some AArch64 chips. The driver speaks
 * the SCPI protocol.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scpi.h>
#include <linux/slab.h>
#include <linux/types.h>

#define SCPI_MHU_CHANNEL_SIZE		0x200

#define SCPI_MHU_CHANNEL_LOW		0x0
#define SCPI_MHU_CHANNEL_HIGH		0x1

#define SCPI_MHU_SCP_INT_LOW_OFFSET	0x000
#define SCPI_MHU_SCP_INT_HIGH_OFFSET	0x020
#define SCPI_MHU_CPU_INT_LOW_OFFSET	0x100
#define SCPI_MHU_CPU_INT_HIGH_OFFSET	0x120

#define CMD_HEADER_ID_MASK		0xff
#define CMD_HEADER_SENDER_MASK		0xff00
#define CMD_HEADER_DATA_SIZE_MASK	0x7fe00000

#define SCPI_PID0_OFFSET		0xfe0
#define SCPI_PID1_OFFSET		0xfe4
#define SCPI_PID2_OFFSET		0xfe8
#define SCPI_PID3_OFFSET		0xfec
#define SCPI_PID4_OFFSET		0xfd0

#define SCPI_VER			0x001bb098

#define SCPI_CLOCKS_MAX			5

struct scpi_dev {
	struct device *dev;
	void __iomem *regs;
	void __iomem *payload;
	int irq[SCPI_MHU_CHANNEL_MAX];
	struct work_struct clocks_work;
};

/*
 * Each slot in the MHU has this set of registers:
 * @status - interrupt status. Each asserted interrupt will set a bit
 * in this register.
 * @set - Setting a bit in this register will set the corresponding bit
 * in the status register.
 * @clear - Setting a bit in this register will clear the corresponding
 * bit in the status register.
 */
struct scpi_mhu_regs {
	uint32_t	status;
	uint32_t	reserved1;
	uint32_t	set;
	uint32_t	reserved2;
	uint32_t	clear;
};


/*
 * The payload area is divided in slots of SCPI_MHU_CHANNEL_SIZE size.
 * The SCP->AP slot comes first, followed by the AP->SCP slot. The high
 * priority channel slots follow the low priority channel slots in the
 * normal payload area, with the secure payload being in a different
 * region (secure ram).
 */

struct scpi_command {
	uint8_t id;
	uint8_t channels;	/* channels where this command is valid */
	bool needs_reply;	/* if the command has an answer from SCP */
};

struct scpi_command commands[] = {
	{ SCPI_CMD_SCP_CAPABILITIES,	 1 << SCPI_MHU_CHANNEL_LOW, true },
	{ SCPI_CMD_GET_CLOCKS,		 1 << SCPI_MHU_CHANNEL_LOW, true },
	{ SCPI_CMD_SET_CLOCK_FREQ_INDEX, 1 << SCPI_MHU_CHANNEL_LOW | \
					 1 << SCPI_MHU_CHANNEL_HIGH, false },
	{ SCPI_CMD_SET_CLOCK_FREQ,	 1 << SCPI_MHU_CHANNEL_LOW | \
					 1 << SCPI_MHU_CHANNEL_HIGH, false },
	{ SCPI_CMD_GET_CLOCK_FREQ,	 1 << SCPI_MHU_CHANNEL_LOW | \
					 1 << SCPI_MHU_CHANNEL_HIGH, true },
};

struct scpi_request {
	unsigned int id;
	struct completion done;
	struct list_head list;
	void *data;
};

struct list_head request_list;
unsigned int last_sender_id;

struct workqueue_struct *mhu_wq;

struct scpi_dev *scpi_dev;

#define SCP_COMMAND(cmd, sender, size)	\
			((cmd & CMD_HEADER_ID_MASK) |	\
			((sender << 8) & CMD_HEADER_SENDER_MASK) |	\
			((size << 20) & CMD_HEADER_DATA_SIZE_MASK))

static inline int scpi_get_command(uint32_t header)
{
	return header & CMD_HEADER_ID_MASK;
}

static inline int scpi_get_sender(uint32_t header)
{
	return (header & CMD_HEADER_SENDER_MASK) >> 8;
}

static inline int scpi_get_size(uint32_t header)
{
	return (header & CMD_HEADER_DATA_SIZE_MASK) >> 20;
}

/*
 * Note: The following implementation is missing support for secure channel
 */

/*
 * Send a payload from AP to SCP
 */
static int scpi_send(struct scpi_dev *scpi, uint8_t channel, uint8_t command,
			uint8_t sender,	char* payload, int size)
{
	/* for communication with SCP we can only use slots 1 or 3
	   corresponding to channel 0 or 1 */
	uint32_t offset = (channel * 2 + 1) * SCPI_MHU_CHANNEL_SIZE;
	struct scpi_mhu_regs *regs;

	regs = (struct scpi_mhu_regs *)(scpi->regs + (channel ?
		SCPI_MHU_CPU_INT_HIGH_OFFSET : SCPI_MHU_CPU_INT_LOW_OFFSET));

	memcpy(scpi->payload + offset, payload, size);
	wmb();
	writel(SCP_COMMAND(command, sender, size), &regs->set);
	return 0;
}

/*
 * Receive a payload from SCP to AP
 */
static int scpi_receive(struct scpi_dev *scpi, uint8_t channel, void *payload, int size)
{
	/* SCP will use slots 0 or 2 corresponding to channel 0 or 1 */
	uint32_t offset = (channel * 2) * SCPI_MHU_CHANNEL_SIZE;

	memcpy(payload, scpi->payload + offset, size);
	return 0;
}

static int scpi_execute_command(struct scpi_dev *scpi, uint8_t cmd, void *payload,
		int size, void *reply_payload, int rsize)
{
	int i, ret = 0;
	uint8_t channel;
	struct scpi_request *req;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (commands[i].id == cmd) {
			break;
		}
	}

	if (i >= ARRAY_SIZE(commands))
		return -EINVAL;

	channel = ffs(commands[i].channels) - 1;
	if (commands[i].needs_reply) {
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			return -ENOMEM;
		/* allocate space for status code */
		req->data = kzalloc(rsize + 4, GFP_KERNEL);
		if (!req->data) {
			ret = -ENOMEM;
			goto free_req;
		}
		req->id = last_sender_id;
		init_completion(&req->done);

		disable_irq(scpi->irq[channel]);
		list_add(&req->list, &request_list);
		enable_irq(scpi->irq[channel]);
	}

	scpi_send(scpi, channel, cmd, last_sender_id, payload, size);

	last_sender_id = (last_sender_id + 1) & CMD_HEADER_SENDER_MASK;

	if (commands[i].needs_reply) {
		wait_for_completion(&req->done);
		/* get the response code */
		memcpy((char*)&ret, req->data, 4);
		/* then the rest of the payload */
		memcpy(reply_payload, req->data + 4, rsize);
		kfree(req->data);
	}
free_req:
	kfree(req);
	return ret;
}

int scpi_exec_command(uint8_t cmd, void *payload, int size,
		void *reply_payload, int rsize)
{
	if (scpi_dev)
		return scpi_execute_command(scpi_dev, cmd, payload, size,
				reply_payload, rsize);

	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(scpi_exec_command);

static irqreturn_t scpi_irq_handler(int irq, void *arg)
{
	int i, sender, size;
	struct scpi_dev *scpi = arg;
	uint32_t response;
	struct scpi_request *req, *n;
	struct scpi_mhu_regs *regs = NULL;

	uint32_t offset[] = { SCPI_MHU_SCP_INT_LOW_OFFSET,
			      SCPI_MHU_SCP_INT_HIGH_OFFSET };

	for (i = 0; i < SCPI_MHU_CHANNEL_MAX; i++) {
		if (scpi->irq[i] == irq) {
			regs = (struct scpi_mhu_regs *)(scpi->regs + offset[i]);
			break;
		}
	}

	if (!regs)
		return IRQ_NONE;

	response = readl(&regs->status);
	sender = scpi_get_sender(response);
	size = scpi_get_size(response);

	list_for_each_entry_safe(req, n, &request_list, list) {
		if (req->id == sender) {
			scpi_receive(scpi, i, req->data, size);
			list_del(&req->list);
			complete(&req->done);
		}
	}
	writel(~0, &regs->clear);
	return IRQ_HANDLED;
}

static void scpi_cleanup(struct scpi_dev *scpi)
{
	int i;

	destroy_workqueue(mhu_wq);

	for (i = 0; i < SCPI_MHU_CHANNEL_MAX; i++) {
		if (scpi->irq[i])
			devm_free_irq(scpi->dev, scpi->irq[i], scpi);
	}

	if (scpi->regs)
		iounmap(scpi->regs);

	kfree(scpi);
	scpi_dev = NULL;
}

static void scpi_init_clocks(struct work_struct *work)
{
	int err, i;
	uint16_t clock_count = 0;
	uint32_t clock_freq;
	char buf[4];
	struct scpi_dev *scpi = container_of(work, struct scpi_dev, clocks_work);

	memset(buf, 0, 4);
	err = scpi_execute_command(scpi, SCPI_CMD_GET_CLOCKS, NULL, 0, buf, 2);
	if (err) {
		dev_info(scpi->dev, "command failed to execute: %d\n", err);
		return;
	}
	clock_count = (buf[1] << 8) | buf[0];
	if (clock_count > SCPI_CLOCKS_MAX) {
		dev_info(scpi->dev, "truncating number of supported clocks from %d to %d\n",
			clock_count, SCPI_CLOCKS_MAX);
		clock_count = SCPI_CLOCKS_MAX;
	}

	dev_info(scpi->dev, "initialising %d clocks\n", clock_count);
	for (i = 3; i < clock_count; i++) {
		buf[0] = i;
		buf[1] = 0;
		err = scpi_execute_command(scpi, SCPI_CMD_GET_CLOCK_FREQ,
					&buf, 2, &buf, 4);
		if (err)
			dev_info(scpi->dev, "failed to get freq for clock %d: %d\n", i, err);
		else {
			clock_freq = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
			dev_info(scpi->dev, "clock id %d = %d\n", i, clock_freq);
		}
	}

	return;
}

static int scpi_mhu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct scpi_dev *scpi;
	int err, irq, i;
	uint32_t ver;

	scpi = kzalloc(sizeof(*scpi), GFP_KERNEL);
	if (!scpi)
		return -ENOMEM;

	scpi->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		err = -EINVAL;
		goto fail;
	}
	scpi->regs = ioremap_nocache(res->start, resource_size(res));
	if (!scpi->regs) {
		dev_err(&pdev->dev, "failed to map scpi control block memory\n");
		err = -ENOMEM;
		goto fail;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		err = -EINVAL;
		goto fail;
	}
	scpi->payload = devm_ioremap_resource(scpi->dev, res);
	if (!scpi->payload) {
		dev_err(&pdev->dev, "failed to map scpi payload memory\n");
		err = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < SCPI_MHU_CHANNEL_MAX; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			dev_err(&pdev->dev, "failed to get irq for channel %d\n", i);
			err = -EINVAL;
			goto fail;
		}
		err = devm_request_irq(&pdev->dev, irq, scpi_irq_handler,
				0, dev_name(&pdev->dev), scpi);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to request irq for channel %d\n", i);
			goto fail;
		}
		scpi->irq[i] = irq;
	}

	/* check that we are talking with the correct MHU part */
	ver = readl(scpi->regs + SCPI_PID0_OFFSET) & 0xff;
	ver = ((readl(scpi->regs + SCPI_PID1_OFFSET) & 0xff) << 8) | ver;
	ver = ((readl(scpi->regs + SCPI_PID2_OFFSET) & 0xff) << 16) | ver;
	ver = ((readl(scpi->regs + SCPI_PID3_OFFSET) & 0xff) << 24) | ver;

	if (ver != SCPI_VER || readl(scpi->regs + SCPI_PID4_OFFSET) != 0x4) {
		dev_err(&pdev->dev, "invalid MHU version found: 0x%x\n", ver);
		err = -ENXIO;
		goto fail;
	}

	dev_info(&pdev->dev, "MHU version 0x%x using interrupts %d and %d\n",
		ver, scpi->irq[0], scpi->irq[1]);
	platform_set_drvdata(pdev, scpi);

	INIT_LIST_HEAD(&request_list);
	last_sender_id = 0;

	mhu_wq = alloc_workqueue("scpi_mhu", WQ_UNBOUND | WQ_SYSFS, 0);
	if (!mhu_wq) {
		dev_err(&pdev->dev, "failed to create workqueue\n");
	} else {
		INIT_WORK(&scpi->clocks_work, scpi_init_clocks);
		queue_work(mhu_wq, &scpi->clocks_work);
	}

	scpi_dev = scpi;

	return 0;
fail:
	scpi_cleanup(scpi);
	return err;
}

static int scpi_mhu_remove(struct platform_device *pdev)
{
	struct scpi_dev *scpi = platform_get_drvdata(pdev);
	if (scpi)
		scpi_cleanup(scpi);

	return 0;
}

static struct of_device_id scpi_mhu_of_match[] = {
	{ .compatible	= "arm,scpi-mhu" },
	{},
};
MODULE_DEVICE_TABLE(of, scpi_mhu_of_match);

static struct platform_driver scpi_mhu_driver = {
	.probe	= scpi_mhu_probe,
	.remove	= scpi_mhu_remove,
	.driver = {
		.name	= "scpi-mhu",
		.owner	= THIS_MODULE,
		.of_match_table = scpi_mhu_of_match,
	},
};

static int __init scpi_mhu_init(void)
{
	return platform_driver_register(&scpi_mhu_driver);
}

static void __exit scpi_mhu_exit(void)
{
	platform_driver_unregister(&scpi_mhu_driver);
}

module_init(scpi_mhu_init);
module_exit(scpi_mhu_exit);

MODULE_AUTHOR("Liviu Dudau");
MODULE_DESCRIPTION("ARM SCPI MHU driver");
MODULE_LICENSE("GPL v2");
