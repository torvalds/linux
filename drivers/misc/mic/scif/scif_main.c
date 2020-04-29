// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Intel SCIF driver.
 */
#include <linux/module.h>
#include <linux/idr.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "../bus/scif_bus.h"
#include "scif_peer_bus.h"
#include "scif_main.h"
#include "scif_map.h"

struct scif_info scif_info = {
	.mdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "scif",
		.fops = &scif_fops,
	}
};

struct scif_dev *scif_dev;
struct kmem_cache *unaligned_cache;
static atomic_t g_loopb_cnt;

/* Runs in the context of intr_wq */
static void scif_intr_bh_handler(struct work_struct *work)
{
	struct scif_dev *scifdev =
			container_of(work, struct scif_dev, intr_bh);

	if (scifdev_self(scifdev))
		scif_loopb_msg_handler(scifdev, scifdev->qpairs);
	else
		scif_nodeqp_intrhandler(scifdev, scifdev->qpairs);
}

int scif_setup_intr_wq(struct scif_dev *scifdev)
{
	if (!scifdev->intr_wq) {
		snprintf(scifdev->intr_wqname, sizeof(scifdev->intr_wqname),
			 "SCIF INTR %d", scifdev->node);
		scifdev->intr_wq =
			alloc_ordered_workqueue(scifdev->intr_wqname, 0);
		if (!scifdev->intr_wq)
			return -ENOMEM;
		INIT_WORK(&scifdev->intr_bh, scif_intr_bh_handler);
	}
	return 0;
}

void scif_destroy_intr_wq(struct scif_dev *scifdev)
{
	if (scifdev->intr_wq) {
		destroy_workqueue(scifdev->intr_wq);
		scifdev->intr_wq = NULL;
	}
}

irqreturn_t scif_intr_handler(int irq, void *data)
{
	struct scif_dev *scifdev = data;
	struct scif_hw_dev *sdev = scifdev->sdev;

	sdev->hw_ops->ack_interrupt(sdev, scifdev->db);
	queue_work(scifdev->intr_wq, &scifdev->intr_bh);
	return IRQ_HANDLED;
}

static void scif_qp_setup_handler(struct work_struct *work)
{
	struct scif_dev *scifdev = container_of(work, struct scif_dev,
						qp_dwork.work);
	struct scif_hw_dev *sdev = scifdev->sdev;
	dma_addr_t da = 0;
	int err;

	if (scif_is_mgmt_node()) {
		struct mic_bootparam *bp = sdev->dp;

		da = bp->scif_card_dma_addr;
		scifdev->rdb = bp->h2c_scif_db;
	} else {
		struct mic_bootparam __iomem *bp = sdev->rdp;

		da = readq(&bp->scif_host_dma_addr);
		scifdev->rdb = ioread8(&bp->c2h_scif_db);
	}
	if (da) {
		err = scif_qp_response(da, scifdev);
		if (err)
			dev_err(&scifdev->sdev->dev,
				"scif_qp_response err %d\n", err);
	} else {
		schedule_delayed_work(&scifdev->qp_dwork,
				      msecs_to_jiffies(1000));
	}
}

static int scif_setup_scifdev(void)
{
	/* We support a maximum of 129 SCIF nodes including the mgmt node */
#define MAX_SCIF_NODES 129
	int i;
	u8 num_nodes = MAX_SCIF_NODES;

	scif_dev = kcalloc(num_nodes, sizeof(*scif_dev), GFP_KERNEL);
	if (!scif_dev)
		return -ENOMEM;
	for (i = 0; i < num_nodes; i++) {
		struct scif_dev *scifdev = &scif_dev[i];

		scifdev->node = i;
		scifdev->exit = OP_IDLE;
		init_waitqueue_head(&scifdev->disconn_wq);
		mutex_init(&scifdev->lock);
		INIT_WORK(&scifdev->peer_add_work, scif_add_peer_device);
		INIT_DELAYED_WORK(&scifdev->p2p_dwork,
				  scif_poll_qp_state);
		INIT_DELAYED_WORK(&scifdev->qp_dwork,
				  scif_qp_setup_handler);
		INIT_LIST_HEAD(&scifdev->p2p);
		RCU_INIT_POINTER(scifdev->spdev, NULL);
	}
	return 0;
}

static void scif_destroy_scifdev(void)
{
	kfree(scif_dev);
	scif_dev = NULL;
}

static int scif_probe(struct scif_hw_dev *sdev)
{
	struct scif_dev *scifdev = &scif_dev[sdev->dnode];
	int rc;

	dev_set_drvdata(&sdev->dev, sdev);
	scifdev->sdev = sdev;

	if (1 == atomic_add_return(1, &g_loopb_cnt)) {
		struct scif_dev *loopb_dev = &scif_dev[sdev->snode];

		loopb_dev->sdev = sdev;
		rc = scif_setup_loopback_qp(loopb_dev);
		if (rc)
			goto exit;
	}

	rc = scif_setup_intr_wq(scifdev);
	if (rc)
		goto destroy_loopb;
	rc = scif_setup_qp(scifdev);
	if (rc)
		goto destroy_intr;
	scifdev->db = sdev->hw_ops->next_db(sdev);
	scifdev->cookie = sdev->hw_ops->request_irq(sdev, scif_intr_handler,
						    "SCIF_INTR", scifdev,
						    scifdev->db);
	if (IS_ERR(scifdev->cookie)) {
		rc = PTR_ERR(scifdev->cookie);
		goto free_qp;
	}
	if (scif_is_mgmt_node()) {
		struct mic_bootparam *bp = sdev->dp;

		bp->c2h_scif_db = scifdev->db;
		bp->scif_host_dma_addr = scifdev->qp_dma_addr;
	} else {
		struct mic_bootparam __iomem *bp = sdev->rdp;

		iowrite8(scifdev->db, &bp->h2c_scif_db);
		writeq(scifdev->qp_dma_addr, &bp->scif_card_dma_addr);
	}
	schedule_delayed_work(&scifdev->qp_dwork,
			      msecs_to_jiffies(1000));
	return rc;
free_qp:
	scif_free_qp(scifdev);
destroy_intr:
	scif_destroy_intr_wq(scifdev);
destroy_loopb:
	if (atomic_dec_and_test(&g_loopb_cnt))
		scif_destroy_loopback_qp(&scif_dev[sdev->snode]);
exit:
	return rc;
}

void scif_stop(struct scif_dev *scifdev)
{
	struct scif_dev *dev;
	int i;

	for (i = scif_info.maxid; i >= 0; i--) {
		dev = &scif_dev[i];
		if (scifdev_self(dev))
			continue;
		scif_handle_remove_node(i);
	}
}

static void scif_remove(struct scif_hw_dev *sdev)
{
	struct scif_dev *scifdev = &scif_dev[sdev->dnode];

	if (scif_is_mgmt_node()) {
		struct mic_bootparam *bp = sdev->dp;

		bp->c2h_scif_db = -1;
		bp->scif_host_dma_addr = 0x0;
	} else {
		struct mic_bootparam __iomem *bp = sdev->rdp;

		iowrite8(-1, &bp->h2c_scif_db);
		writeq(0x0, &bp->scif_card_dma_addr);
	}
	if (scif_is_mgmt_node()) {
		scif_disconnect_node(scifdev->node, true);
	} else {
		scif_info.card_initiated_exit = true;
		scif_stop(scifdev);
	}
	if (atomic_dec_and_test(&g_loopb_cnt))
		scif_destroy_loopback_qp(&scif_dev[sdev->snode]);
	if (scifdev->cookie) {
		sdev->hw_ops->free_irq(sdev, scifdev->cookie, scifdev);
		scifdev->cookie = NULL;
	}
	scif_destroy_intr_wq(scifdev);
	cancel_delayed_work(&scifdev->qp_dwork);
	scif_free_qp(scifdev);
	scifdev->rdb = -1;
	scifdev->sdev = NULL;
}

static struct scif_hw_dev_id id_table[] = {
	{ MIC_SCIF_DEV, SCIF_DEV_ANY_ID },
	{ 0 },
};

static struct scif_driver scif_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = scif_probe,
	.remove = scif_remove,
};

static int _scif_init(void)
{
	int rc;

	mutex_init(&scif_info.eplock);
	spin_lock_init(&scif_info.rmalock);
	spin_lock_init(&scif_info.nb_connect_lock);
	spin_lock_init(&scif_info.port_lock);
	mutex_init(&scif_info.conflock);
	mutex_init(&scif_info.connlock);
	mutex_init(&scif_info.fencelock);
	INIT_LIST_HEAD(&scif_info.uaccept);
	INIT_LIST_HEAD(&scif_info.listen);
	INIT_LIST_HEAD(&scif_info.zombie);
	INIT_LIST_HEAD(&scif_info.connected);
	INIT_LIST_HEAD(&scif_info.disconnected);
	INIT_LIST_HEAD(&scif_info.rma);
	INIT_LIST_HEAD(&scif_info.rma_tc);
	INIT_LIST_HEAD(&scif_info.mmu_notif_cleanup);
	INIT_LIST_HEAD(&scif_info.fence);
	INIT_LIST_HEAD(&scif_info.nb_connect_list);
	init_waitqueue_head(&scif_info.exitwq);
	scif_info.rma_tc_limit = SCIF_RMA_TEMP_CACHE_LIMIT;
	scif_info.en_msg_log = 0;
	scif_info.p2p_enable = 1;
	rc = scif_setup_scifdev();
	if (rc)
		goto error;
	unaligned_cache = kmem_cache_create("Unaligned_DMA",
					    SCIF_KMEM_UNALIGNED_BUF_SIZE,
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (!unaligned_cache) {
		rc = -ENOMEM;
		goto free_sdev;
	}
	INIT_WORK(&scif_info.misc_work, scif_misc_handler);
	INIT_WORK(&scif_info.mmu_notif_work, scif_mmu_notif_handler);
	INIT_WORK(&scif_info.conn_work, scif_conn_handler);
	idr_init(&scif_ports);
	return 0;
free_sdev:
	scif_destroy_scifdev();
error:
	return rc;
}

static void _scif_exit(void)
{
	idr_destroy(&scif_ports);
	kmem_cache_destroy(unaligned_cache);
	scif_destroy_scifdev();
}

static int __init scif_init(void)
{
	struct miscdevice *mdev = &scif_info.mdev;
	int rc;

	_scif_init();
	iova_cache_get();
	rc = scif_peer_bus_init();
	if (rc)
		goto exit;
	rc = scif_register_driver(&scif_driver);
	if (rc)
		goto peer_bus_exit;
	rc = misc_register(mdev);
	if (rc)
		goto unreg_scif;
	scif_init_debugfs();
	return 0;
unreg_scif:
	scif_unregister_driver(&scif_driver);
peer_bus_exit:
	scif_peer_bus_exit();
exit:
	_scif_exit();
	return rc;
}

static void __exit scif_exit(void)
{
	scif_exit_debugfs();
	misc_deregister(&scif_info.mdev);
	scif_unregister_driver(&scif_driver);
	scif_peer_bus_exit();
	iova_cache_put();
	_scif_exit();
}

module_init(scif_init);
module_exit(scif_exit);

MODULE_DEVICE_TABLE(scif, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) SCIF driver");
MODULE_LICENSE("GPL v2");
