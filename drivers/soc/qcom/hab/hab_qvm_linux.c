// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_qvm.h"

#include <linux/highmem.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/of.h>
#include <linux/of_platform.h>

static int hab_shmem_remove(struct platform_device *pdev)
{
	return 0;
}

static void hab_shmem_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id hab_shmem_match_table[] = {
	{.compatible = "qvm,guest_shm"},
	{},
};

/* this happens before hypervisor register */
static int hab_shmem_probe(struct platform_device *pdev)
{
	int irq = 0;
	struct resource *mem;
	void __iomem *shmem_base = NULL;
	int ret = 0;

	/* hab in one GVM will not have pchans more than one VM could allowed */
	if (qvm_priv_info.probe_cnt >= min(hab_driver.ndevices, qvm_priv_info.setting_size)) {
		pr_err("no more channel, current %d, maximum %d\n",
			qvm_priv_info.probe_cnt,
			min(hab_driver.ndevices, qvm_priv_info.setting_size));
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		pr_err("no interrupt for the channel %d, error %d\n",
			qvm_priv_info.probe_cnt, irq);
		return irq;
	}
	qvm_priv_info.pchan_settings[qvm_priv_info.probe_cnt].irq = irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		pr_err("can not get io mem resource for channel %d\n",
					qvm_priv_info.probe_cnt);
		return -EINVAL;
	}
	shmem_base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(shmem_base)) {
		pr_err("ioremap failed for channel %d, mem %pK\n",
					qvm_priv_info.probe_cnt, mem);
		return -EINVAL;
	}
	qvm_priv_info.pchan_settings[qvm_priv_info.probe_cnt].factory_addr
			= (unsigned long)((uintptr_t)shmem_base);

	pr_debug("pchan idx %d, hab irq=%d shmem_base=%pK, mem %pK, mem->start %pK\n",
			 qvm_priv_info.probe_cnt, irq, shmem_base, mem, mem->start);

	qvm_priv_info.probe_cnt++;

	return ret;
}

static struct platform_driver hab_shmem_driver = {
	.probe = hab_shmem_probe,
	.remove = hab_shmem_remove,
	.shutdown = hab_shmem_shutdown,
	.driver = {
		.name = "hab_shmem",
		.of_match_table = of_match_ptr(hab_shmem_match_table),
	},
};

static int hab_shmem_init(void)
{
	qvm_priv_info.probe_cnt = 0;
	return platform_driver_register(&hab_shmem_driver);
}

static void hab_shmem_exit(void)
{
	platform_driver_unregister(&hab_shmem_driver);
	qvm_priv_info.probe_cnt = 0;
}

int hab_hypervisor_register_os(void)
{
	hab_driver.b_server_dom = 0;
	hab_shmem_init();

	return 0;
}

int hab_hypervisor_unregister_os(void)
{
	hab_shmem_exit();

	return 0;
}

void habhyp_commdev_dealloc_os(void *commdev)
{
	struct physical_channel *pchan = (struct physical_channel *)commdev;
	struct qvm_channel *dev = pchan->hyp_data;

	dev->guest_ctrl->detach = 0;
}

static irqreturn_t shm_irq_handler(int irq, void *_pchan)
{
	irqreturn_t rc = IRQ_NONE;
	struct physical_channel *pchan = (struct physical_channel *) _pchan;
	struct qvm_channel *dev =
		(struct qvm_channel *) (pchan ? pchan->hyp_data : NULL);

	if (dev && dev->guest_ctrl) {
		int status = dev->guest_ctrl->status;

		if (status & 0xffff) {/*source bitmask indicator*/
			rc = IRQ_HANDLED;
			tasklet_hi_schedule(&dev->os_data->task);
		}
	}
	return rc;
}

/* debug only */
static void work_func(struct work_struct *work)
{
	struct qvm_channel *dev = container_of(work, struct qvm_channel, wdata.work);

	dump_hab(dev->wdata.data);
}

int habhyp_commdev_create_dispatcher(struct physical_channel *pchan)
{
	struct qvm_channel *dev = (struct qvm_channel *)pchan->hyp_data;
	int ret;

	tasklet_init(&dev->os_data->task, physical_channel_rx_dispatch,
		(unsigned long) pchan);

	/* debug */
	dev->wq = create_workqueue("wq_dump");
	INIT_WORK(&dev->wdata.work, work_func);
	dev->wdata.data = 0; /* let the caller wait */
	dev->side_buf = kzalloc(PIPE_SHMEM_SIZE, GFP_KERNEL);

	pr_debug("request_irq: irq = %d, pchan name = %s\n",
			dev->irq, pchan->name);
	ret = request_irq(dev->irq, shm_irq_handler, IRQF_SHARED, pchan->name, pchan);
	if (ret)
		pr_err("request_irq for %s failed: %d\n",
			pchan->name, ret);

	return ret;
}

/* Debug: critical section? */
void hab_pipe_read_dump(struct physical_channel *pchan)
{
	struct qvm_channel *dev = (struct qvm_channel *)pchan->hyp_data;
	char str[250];
	int i;
	struct dbg_items *its = dev->dbg_itms;

	struct hab_shared_buf *sh_buf = dev->rx_buf;
	uint32_t buf_size = PIPE_SHMEM_SIZE;

	snprintf(str, sizeof(str),
		"index 0x%X rd_cnt %d wr_cnt %d size %d data_addr %lX",
		dev->pipe_ep->rx_info.index,
		sh_buf->rd_count,
		sh_buf->wr_count,
		sh_buf->size,
		&sh_buf->data[0]);
	dump_hab_buf(str, strlen(str)+1);

	/* trace history buffer dump */
	snprintf(str, sizeof(str), "dbg hist buffer index %d\n", its->idx);
	dump_hab_buf(str, strlen(str)+1);

	for (i = 0; i < DBG_ITEM_SIZE; i++) {
		struct dbg_item *it = &its->it[i];

		snprintf(str, sizeof(str),
		"it %d: rd %d wr %d va %lX index 0x%X size %d ret %d\n",
		i, it->rd_cnt, it->wr_cnt, it->va, it->index, it->sz, it->ret);
		dump_hab_buf(str, strlen(str)+1);
	}

	/* !!!! to end the readable string */
	str[0] = str[1] = str[2] = str[3] = 33;
	dump_hab_buf(str, 4); /* separator */

	dump_hab_buf((void *)sh_buf->data, buf_size);

	str[0] = str[1] = str[2] = str[3] = str[4] = str[5] = str[6] =
		str[7] = 33; /* !!!! to end the readable string */
	dump_hab_buf(str, 16); /* separator */

	dump_hab_buf(dev->side_buf, buf_size);
}

void dump_hab_wq(struct physical_channel *pchan)
{
	struct qvm_channel *dev  = pchan->hyp_data;

	dev->wdata.data = pchan->habdev->id;
	queue_work(dev->wq, &dev->wdata.work);
}

int hab_stat_log(struct physical_channel **pchans, int pchan_cnt, char *dest,
			int dest_size)
{
	return 0;
};

/* The input is already va now */
inline unsigned long hab_shmem_factory_va(unsigned long factory_addr)
{
	return factory_addr;
}

/* to get the shmem data region virtual address */
char *hab_shmem_attach(struct qvm_channel *dev, const char *name,
	uint32_t pipe_alloc_pages)
{
	struct qvm_plugin_info *qvm_priv = hab_driver.hyp_priv;
	uint64_t paddr;
	char *shmdata;
	int ret = 0;

	/* no more vdev-shmem for more pchan considering the 1:1 rule */
	if (qvm_priv->curr >= qvm_priv->probe_cnt) {
		pr_err("pchan guest factory setting %d overflow probe cnt %d\n",
			qvm_priv->curr, qvm_priv->probe_cnt);
		ret = -1;
		goto err;
	}

	paddr = get_guest_ctrl_paddr(dev,
			qvm_priv->pchan_settings[qvm_priv->curr].factory_addr,
			qvm_priv->pchan_settings[qvm_priv->curr].irq,
			name,
			pipe_alloc_pages);

	dev->guest_ctrl = memremap(paddr,
		(dev->guest_factory->size + 1) * PAGE_SIZE, MEMREMAP_WB);
		/* page size should be 4KB */
	if (!dev->guest_ctrl) {
		ret = -ENOMEM;
		goto err;
	}

	shmdata = (char *)dev->guest_ctrl + PAGE_SIZE;

	pr_debug("ctrl page 0x%llx mapped at 0x%pK, idx %d\n",
			paddr, dev->guest_ctrl, dev->guest_ctrl->idx);
	pr_debug("data buffer mapped at 0x%pK\n", shmdata);
	dev->idx = dev->guest_ctrl->idx;
	qvm_priv->curr++;
	return shmdata;

err:
	return ERR_PTR(ret);
}
