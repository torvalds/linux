// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Ltd
 */
#include <dt-bindings/power/qcom-aoss-qmp.h>
#include <linux/clk-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/thermal.h>
#include <linux/slab.h>

#define QMP_DESC_MAGIC			0x0
#define QMP_DESC_VERSION		0x4
#define QMP_DESC_FEATURES		0x8

/* AOP-side offsets */
#define QMP_DESC_UCORE_LINK_STATE	0xc
#define QMP_DESC_UCORE_LINK_STATE_ACK	0x10
#define QMP_DESC_UCORE_CH_STATE		0x14
#define QMP_DESC_UCORE_CH_STATE_ACK	0x18
#define QMP_DESC_UCORE_MBOX_SIZE	0x1c
#define QMP_DESC_UCORE_MBOX_OFFSET	0x20

/* Linux-side offsets */
#define QMP_DESC_MCORE_LINK_STATE	0x24
#define QMP_DESC_MCORE_LINK_STATE_ACK	0x28
#define QMP_DESC_MCORE_CH_STATE		0x2c
#define QMP_DESC_MCORE_CH_STATE_ACK	0x30
#define QMP_DESC_MCORE_MBOX_SIZE	0x34
#define QMP_DESC_MCORE_MBOX_OFFSET	0x38

#define QMP_STATE_UP			GENMASK(15, 0)
#define QMP_STATE_DOWN			GENMASK(31, 16)

#define QMP_MAGIC			0x4d41494c /* mail */
#define QMP_VERSION			1

/* 64 bytes is enough to store the requests and provides padding to 4 bytes */
#define QMP_MSG_LEN			64

#define QMP_NUM_COOLING_RESOURCES	2

static bool qmp_cdev_max_state = 1;

struct qmp_cooling_device {
	struct thermal_cooling_device *cdev;
	struct qmp *qmp;
	char *name;
	bool state;
};

/**
 * struct qmp - driver state for QMP implementation
 * @msgram: iomem referencing the message RAM used for communication
 * @dev: reference to QMP device
 * @mbox_client: mailbox client used to ring the doorbell on transmit
 * @mbox_chan: mailbox channel used to ring the doorbell on transmit
 * @offset: offset within @msgram where messages should be written
 * @size: maximum size of the messages to be transmitted
 * @event: wait_queue for synchronization with the IRQ
 * @tx_lock: provides synchronization between multiple callers of qmp_send()
 * @qdss_clk: QDSS clock hw struct
 * @pd_data: genpd data
 * @cooling_devs: thermal cooling devices
 */
struct qmp {
	void __iomem *msgram;
	struct device *dev;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;

	size_t offset;
	size_t size;

	wait_queue_head_t event;

	struct mutex tx_lock;

	struct clk_hw qdss_clk;
	struct genpd_onecell_data pd_data;
	struct qmp_cooling_device *cooling_devs;
};

struct qmp_pd {
	struct qmp *qmp;
	struct generic_pm_domain pd;
};

#define to_qmp_pd_resource(res) container_of(res, struct qmp_pd, pd)

static void qmp_kick(struct qmp *qmp)
{
	mbox_send_message(qmp->mbox_chan, NULL);
	mbox_client_txdone(qmp->mbox_chan, 0);
}

static bool qmp_magic_valid(struct qmp *qmp)
{
	return readl(qmp->msgram + QMP_DESC_MAGIC) == QMP_MAGIC;
}

static bool qmp_link_acked(struct qmp *qmp)
{
	return readl(qmp->msgram + QMP_DESC_MCORE_LINK_STATE_ACK) == QMP_STATE_UP;
}

static bool qmp_mcore_channel_acked(struct qmp *qmp)
{
	return readl(qmp->msgram + QMP_DESC_MCORE_CH_STATE_ACK) == QMP_STATE_UP;
}

static bool qmp_ucore_channel_up(struct qmp *qmp)
{
	return readl(qmp->msgram + QMP_DESC_UCORE_CH_STATE) == QMP_STATE_UP;
}

static int qmp_open(struct qmp *qmp)
{
	int ret;
	u32 val;

	if (!qmp_magic_valid(qmp)) {
		dev_err(qmp->dev, "QMP magic doesn't match\n");
		return -EINVAL;
	}

	val = readl(qmp->msgram + QMP_DESC_VERSION);
	if (val != QMP_VERSION) {
		dev_err(qmp->dev, "unsupported QMP version %d\n", val);
		return -EINVAL;
	}

	qmp->offset = readl(qmp->msgram + QMP_DESC_MCORE_MBOX_OFFSET);
	qmp->size = readl(qmp->msgram + QMP_DESC_MCORE_MBOX_SIZE);
	if (!qmp->size) {
		dev_err(qmp->dev, "invalid mailbox size\n");
		return -EINVAL;
	}

	/* Ack remote core's link state */
	val = readl(qmp->msgram + QMP_DESC_UCORE_LINK_STATE);
	writel(val, qmp->msgram + QMP_DESC_UCORE_LINK_STATE_ACK);

	/* Set local core's link state to up */
	writel(QMP_STATE_UP, qmp->msgram + QMP_DESC_MCORE_LINK_STATE);

	qmp_kick(qmp);

	ret = wait_event_timeout(qmp->event, qmp_link_acked(qmp), HZ);
	if (!ret) {
		dev_err(qmp->dev, "ucore didn't ack link\n");
		goto timeout_close_link;
	}

	writel(QMP_STATE_UP, qmp->msgram + QMP_DESC_MCORE_CH_STATE);

	qmp_kick(qmp);

	ret = wait_event_timeout(qmp->event, qmp_ucore_channel_up(qmp), HZ);
	if (!ret) {
		dev_err(qmp->dev, "ucore didn't open channel\n");
		goto timeout_close_channel;
	}

	/* Ack remote core's channel state */
	writel(QMP_STATE_UP, qmp->msgram + QMP_DESC_UCORE_CH_STATE_ACK);

	qmp_kick(qmp);

	ret = wait_event_timeout(qmp->event, qmp_mcore_channel_acked(qmp), HZ);
	if (!ret) {
		dev_err(qmp->dev, "ucore didn't ack channel\n");
		goto timeout_close_channel;
	}

	return 0;

timeout_close_channel:
	writel(QMP_STATE_DOWN, qmp->msgram + QMP_DESC_MCORE_CH_STATE);

timeout_close_link:
	writel(QMP_STATE_DOWN, qmp->msgram + QMP_DESC_MCORE_LINK_STATE);
	qmp_kick(qmp);

	return -ETIMEDOUT;
}

static void qmp_close(struct qmp *qmp)
{
	writel(QMP_STATE_DOWN, qmp->msgram + QMP_DESC_MCORE_CH_STATE);
	writel(QMP_STATE_DOWN, qmp->msgram + QMP_DESC_MCORE_LINK_STATE);
	qmp_kick(qmp);
}

static irqreturn_t qmp_intr(int irq, void *data)
{
	struct qmp *qmp = data;

	wake_up_all(&qmp->event);

	return IRQ_HANDLED;
}

static bool qmp_message_empty(struct qmp *qmp)
{
	return readl(qmp->msgram + qmp->offset) == 0;
}

/**
 * qmp_send() - send a message to the AOSS
 * @qmp: qmp context
 * @data: message to be sent
 * @len: length of the message
 *
 * Transmit @data to AOSS and wait for the AOSS to acknowledge the message.
 * @len must be a multiple of 4 and not longer than the mailbox size. Access is
 * synchronized by this implementation.
 *
 * Return: 0 on success, negative errno on failure
 */
static int qmp_send(struct qmp *qmp, const void *data, size_t len)
{
	long time_left;
	int ret;

	if (WARN_ON(len + sizeof(u32) > qmp->size))
		return -EINVAL;

	if (WARN_ON(len % sizeof(u32)))
		return -EINVAL;

	mutex_lock(&qmp->tx_lock);

	/* The message RAM only implements 32-bit accesses */
	__iowrite32_copy(qmp->msgram + qmp->offset + sizeof(u32),
			 data, len / sizeof(u32));
	writel(len, qmp->msgram + qmp->offset);

	/* Read back len to confirm data written in message RAM */
	readl(qmp->msgram + qmp->offset);
	qmp_kick(qmp);

	time_left = wait_event_interruptible_timeout(qmp->event,
						     qmp_message_empty(qmp), HZ);
	if (!time_left) {
		dev_err(qmp->dev, "ucore did not ack channel\n");
		ret = -ETIMEDOUT;

		/* Clear message from buffer */
		writel(0, qmp->msgram + qmp->offset);
	} else {
		ret = 0;
	}

	mutex_unlock(&qmp->tx_lock);

	return ret;
}

static int qmp_qdss_clk_prepare(struct clk_hw *hw)
{
	static const char buf[QMP_MSG_LEN] = "{class: clock, res: qdss, val: 1}";
	struct qmp *qmp = container_of(hw, struct qmp, qdss_clk);

	return qmp_send(qmp, buf, sizeof(buf));
}

static void qmp_qdss_clk_unprepare(struct clk_hw *hw)
{
	static const char buf[QMP_MSG_LEN] = "{class: clock, res: qdss, val: 0}";
	struct qmp *qmp = container_of(hw, struct qmp, qdss_clk);

	qmp_send(qmp, buf, sizeof(buf));
}

static const struct clk_ops qmp_qdss_clk_ops = {
	.prepare = qmp_qdss_clk_prepare,
	.unprepare = qmp_qdss_clk_unprepare,
};

static int qmp_qdss_clk_add(struct qmp *qmp)
{
	static const struct clk_init_data qdss_init = {
		.ops = &qmp_qdss_clk_ops,
		.name = "qdss",
	};
	int ret;

	qmp->qdss_clk.init = &qdss_init;
	ret = clk_hw_register(qmp->dev, &qmp->qdss_clk);
	if (ret < 0) {
		dev_err(qmp->dev, "failed to register qdss clock\n");
		return ret;
	}

	ret = of_clk_add_hw_provider(qmp->dev->of_node, of_clk_hw_simple_get,
				     &qmp->qdss_clk);
	if (ret < 0) {
		dev_err(qmp->dev, "unable to register of clk hw provider\n");
		clk_hw_unregister(&qmp->qdss_clk);
	}

	return ret;
}

static void qmp_qdss_clk_remove(struct qmp *qmp)
{
	of_clk_del_provider(qmp->dev->of_node);
	clk_hw_unregister(&qmp->qdss_clk);
}

static int qmp_pd_power_toggle(struct qmp_pd *res, bool enable)
{
	char buf[QMP_MSG_LEN] = {};

	snprintf(buf, sizeof(buf),
		 "{class: image, res: load_state, name: %s, val: %s}",
		 res->pd.name, enable ? "on" : "off");
	return qmp_send(res->qmp, buf, sizeof(buf));
}

static int qmp_pd_power_on(struct generic_pm_domain *domain)
{
	return qmp_pd_power_toggle(to_qmp_pd_resource(domain), true);
}

static int qmp_pd_power_off(struct generic_pm_domain *domain)
{
	return qmp_pd_power_toggle(to_qmp_pd_resource(domain), false);
}

static const char * const sdm845_resources[] = {
	[AOSS_QMP_LS_CDSP] = "cdsp",
	[AOSS_QMP_LS_LPASS] = "adsp",
	[AOSS_QMP_LS_MODEM] = "modem",
	[AOSS_QMP_LS_SLPI] = "slpi",
	[AOSS_QMP_LS_SPSS] = "spss",
	[AOSS_QMP_LS_VENUS] = "venus",
};

static int qmp_pd_add(struct qmp *qmp)
{
	struct genpd_onecell_data *data = &qmp->pd_data;
	struct device *dev = qmp->dev;
	struct qmp_pd *res;
	size_t num = ARRAY_SIZE(sdm845_resources);
	int ret;
	int i;

	res = devm_kcalloc(dev, num, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	data->domains = devm_kcalloc(dev, num, sizeof(*data->domains),
				     GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		res[i].qmp = qmp;
		res[i].pd.name = sdm845_resources[i];
		res[i].pd.power_on = qmp_pd_power_on;
		res[i].pd.power_off = qmp_pd_power_off;

		ret = pm_genpd_init(&res[i].pd, NULL, true);
		if (ret < 0) {
			dev_err(dev, "failed to init genpd\n");
			goto unroll_genpds;
		}

		data->domains[i] = &res[i].pd;
	}

	data->num_domains = i;

	ret = of_genpd_add_provider_onecell(dev->of_node, data);
	if (ret < 0)
		goto unroll_genpds;

	return 0;

unroll_genpds:
	for (i--; i >= 0; i--)
		pm_genpd_remove(data->domains[i]);

	return ret;
}

static void qmp_pd_remove(struct qmp *qmp)
{
	struct genpd_onecell_data *data = &qmp->pd_data;
	struct device *dev = qmp->dev;
	int i;

	of_genpd_del_provider(dev->of_node);

	for (i = 0; i < data->num_domains; i++)
		pm_genpd_remove(data->domains[i]);
}

static int qmp_cdev_get_max_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	*state = qmp_cdev_max_state;
	return 0;
}

static int qmp_cdev_get_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	struct qmp_cooling_device *qmp_cdev = cdev->devdata;

	*state = qmp_cdev->state;
	return 0;
}

static int qmp_cdev_set_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long state)
{
	struct qmp_cooling_device *qmp_cdev = cdev->devdata;
	char buf[QMP_MSG_LEN] = {};
	bool cdev_state;
	int ret;

	/* Normalize state */
	cdev_state = !!state;

	if (qmp_cdev->state == state)
		return 0;

	snprintf(buf, sizeof(buf),
		 "{class: volt_flr, event:zero_temp, res:%s, value:%s}",
			qmp_cdev->name,
			cdev_state ? "on" : "off");

	ret = qmp_send(qmp_cdev->qmp, buf, sizeof(buf));

	if (!ret)
		qmp_cdev->state = cdev_state;

	return ret;
}

static struct thermal_cooling_device_ops qmp_cooling_device_ops = {
	.get_max_state = qmp_cdev_get_max_state,
	.get_cur_state = qmp_cdev_get_cur_state,
	.set_cur_state = qmp_cdev_set_cur_state,
};

static int qmp_cooling_device_add(struct qmp *qmp,
				  struct qmp_cooling_device *qmp_cdev,
				  struct device_node *node)
{
	char *cdev_name = (char *)node->name;

	qmp_cdev->qmp = qmp;
	qmp_cdev->state = !qmp_cdev_max_state;
	qmp_cdev->name = cdev_name;
	qmp_cdev->cdev = devm_thermal_of_cooling_device_register
				(qmp->dev, node,
				cdev_name,
				qmp_cdev, &qmp_cooling_device_ops);

	if (IS_ERR(qmp_cdev->cdev))
		dev_err(qmp->dev, "unable to register %s cooling device\n",
			cdev_name);

	return PTR_ERR_OR_ZERO(qmp_cdev->cdev);
}

static int qmp_cooling_devices_register(struct qmp *qmp)
{
	struct device_node *np, *child;
	int count = QMP_NUM_COOLING_RESOURCES;
	int ret;

	np = qmp->dev->of_node;

	qmp->cooling_devs = devm_kcalloc(qmp->dev, count,
					 sizeof(*qmp->cooling_devs),
					 GFP_KERNEL);

	if (!qmp->cooling_devs)
		return -ENOMEM;

	for_each_available_child_of_node(np, child) {
		if (!of_find_property(child, "#cooling-cells", NULL))
			continue;
		ret = qmp_cooling_device_add(qmp, &qmp->cooling_devs[count++],
					     child);
		if (ret)
			goto unroll;
	}

	return 0;

unroll:
	while (--count >= 0)
		thermal_cooling_device_unregister
			(qmp->cooling_devs[count].cdev);

	return ret;
}

static void qmp_cooling_devices_remove(struct qmp *qmp)
{
	int i;

	for (i = 0; i < QMP_NUM_COOLING_RESOURCES; i++)
		thermal_cooling_device_unregister(qmp->cooling_devs[i].cdev);
}

static int qmp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct qmp *qmp;
	int irq;
	int ret;

	qmp = devm_kzalloc(&pdev->dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = &pdev->dev;
	init_waitqueue_head(&qmp->event);
	mutex_init(&qmp->tx_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qmp->msgram = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(qmp->msgram))
		return PTR_ERR(qmp->msgram);

	qmp->mbox_client.dev = &pdev->dev;
	qmp->mbox_client.knows_txdone = true;
	qmp->mbox_chan = mbox_request_channel(&qmp->mbox_client, 0);
	if (IS_ERR(qmp->mbox_chan)) {
		dev_err(&pdev->dev, "failed to acquire ipc mailbox\n");
		return PTR_ERR(qmp->mbox_chan);
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, qmp_intr, IRQF_ONESHOT,
			       "aoss-qmp", qmp);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request interrupt\n");
		goto err_free_mbox;
	}

	ret = qmp_open(qmp);
	if (ret < 0)
		goto err_free_mbox;

	ret = qmp_qdss_clk_add(qmp);
	if (ret)
		goto err_close_qmp;

	ret = qmp_pd_add(qmp);
	if (ret)
		goto err_remove_qdss_clk;

	ret = qmp_cooling_devices_register(qmp);
	if (ret)
		dev_err(&pdev->dev, "failed to register aoss cooling devices\n");

	platform_set_drvdata(pdev, qmp);

	return 0;

err_remove_qdss_clk:
	qmp_qdss_clk_remove(qmp);
err_close_qmp:
	qmp_close(qmp);
err_free_mbox:
	mbox_free_channel(qmp->mbox_chan);

	return ret;
}

static int qmp_remove(struct platform_device *pdev)
{
	struct qmp *qmp = platform_get_drvdata(pdev);

	qmp_qdss_clk_remove(qmp);
	qmp_pd_remove(qmp);
	qmp_cooling_devices_remove(qmp);

	qmp_close(qmp);
	mbox_free_channel(qmp->mbox_chan);

	return 0;
}

static const struct of_device_id qmp_dt_match[] = {
	{ .compatible = "qcom,sc7180-aoss-qmp", },
	{ .compatible = "qcom,sdm845-aoss-qmp", },
	{ .compatible = "qcom,sm8150-aoss-qmp", },
	{ .compatible = "qcom,sm8250-aoss-qmp", },
	{}
};
MODULE_DEVICE_TABLE(of, qmp_dt_match);

static struct platform_driver qmp_driver = {
	.driver = {
		.name		= "qcom_aoss_qmp",
		.of_match_table	= qmp_dt_match,
	},
	.probe = qmp_probe,
	.remove	= qmp_remove,
};
module_platform_driver(qmp_driver);

MODULE_DESCRIPTION("Qualcomm AOSS QMP driver");
MODULE_LICENSE("GPL v2");
