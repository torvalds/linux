// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/rockchip/rockchip_thunderboot_service.h>
#include <soc/rockchip/rockchip-mailbox.h>

#define CMD_MCU_STATUS		(0x0000f00d)
#define MCU_STATUS_DONE		(0xdeadbeef)

struct rk_tb_serv {
	struct device *dev;
	struct mbox_chan *mbox_rx_chan;
	struct mbox_client mbox_cl;
	struct reset_control *rsts;
	phys_addr_t mem_start;
	size_t mem_size;
};

static atomic_t mcu_done = ATOMIC_INIT(0);
static LIST_HEAD(clients_list);
static DEFINE_SPINLOCK(lock);

bool rk_tb_mcu_is_done(void)
{
	return atomic_read(&mcu_done);
}
EXPORT_SYMBOL(rk_tb_mcu_is_done);

int rk_tb_client_register_cb(struct rk_tb_client *client)
{
	if (!client || !client->cb)
		return -EINVAL;

	spin_lock(&lock);
	if (rk_tb_mcu_is_done()) {
		spin_unlock(&lock);
		client->cb(client->data);
		return 0;
	}

	list_add_tail(&client->node, &clients_list);
	spin_unlock(&lock);

	return 0;
}
EXPORT_SYMBOL(rk_tb_client_register_cb);

static void do_mcu_done(struct rk_tb_serv *serv)
{
	struct rk_tb_client *client, *client_s;
	struct rockchip_mbox_msg msg;

	rockchip_mbox_read_msg(serv->mbox_rx_chan, &msg);
	if (msg.cmd == CMD_MCU_STATUS && msg.data == MCU_STATUS_DONE) {
		void *start, *end;

		/* make sure mcu is wfi */
		udelay(15);
		reset_control_assert(serv->rsts);

		start = phys_to_virt(serv->mem_start);
		end = start + serv->mem_size;
		free_reserved_area(start, end, -1, "rtos");

		spin_lock(&lock);
		if (atomic_read(&mcu_done)) {
			spin_unlock(&lock);
			return;
		}

		atomic_set(&mcu_done, 1);
		list_for_each_entry_safe(client, client_s, &clients_list, node) {
			spin_unlock(&lock);
			if (client->cb)
				client->cb(client->data);
			spin_lock(&lock);
			list_del(&client->node);
		}
		spin_unlock(&lock);
	}
}

static void rk_tb_rx_callback(struct mbox_client *mbox_cl, void *message)
{
	struct rk_tb_serv *serv = dev_get_drvdata(mbox_cl->dev);

	do_mcu_done(serv);
	mbox_free_channel(serv->mbox_rx_chan);
}

static int rk_tb_serv_probe(struct platform_device *pdev)
{
	struct rk_tb_serv *serv;
	struct mbox_client *mbox_cl;
	struct device_node *mem;
	struct resource reg;
	int ret;

	serv = devm_kzalloc(&pdev->dev, sizeof(*serv), GFP_KERNEL);
	if (!serv)
		return -ENOMEM;

	mem = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!mem) {
		dev_err(&pdev->dev, "missing \"memory-region\" property\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(mem, 0, &reg);
	of_node_put(mem);
	if (ret) {
		dev_err(&pdev->dev, "missing \"reg\" property\n");
		return -ENODEV;
	}

	serv->mem_start = reg.start;
	serv->mem_size = resource_size(&reg);

	serv->rsts = devm_reset_control_array_get_optional_exclusive(&pdev->dev);
	if (IS_ERR(serv->rsts) && PTR_ERR(serv->rsts) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, serv);

	mbox_cl = &serv->mbox_cl;
	mbox_cl->dev = &pdev->dev;
	mbox_cl->rx_callback = rk_tb_rx_callback;
	serv->mbox_rx_chan = mbox_request_channel_byname(mbox_cl, "amp-rx");
	if (IS_ERR(serv->mbox_rx_chan)) {
		dev_err(mbox_cl->dev, "failed to request mbox rx chan\n");
		return PTR_ERR(serv->mbox_rx_chan);
	}

	do_mcu_done(serv);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rk_tb_serv_dt_match[] = {
	{ .compatible = "rockchip,thunder-boot-service" },
	{},
};
#endif

static struct platform_driver rk_tb_serv_driver = {
	.probe		= rk_tb_serv_probe,
	.driver		= {
		.name		= "rockchip_thunder_boot_service",
		.of_match_table	= rk_tb_serv_dt_match,
	},
};

static int __init rk_tb_serv_init(void)
{
	return platform_driver_register(&rk_tb_serv_driver);
}

arch_initcall(rk_tb_serv_init);
