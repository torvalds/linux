// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include <linux/module.h>
#include <net/addrconf.h>
#include <rdma/erdma-abi.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

MODULE_AUTHOR("Cheng Xu <chengyou@linux.alibaba.com>");
MODULE_DESCRIPTION("Alibaba elasticRDMA adapter driver");
MODULE_LICENSE("Dual BSD/GPL");

static int erdma_netdev_event(struct notifier_block *nb, unsigned long event,
			      void *arg)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(arg);
	struct erdma_dev *dev = container_of(nb, struct erdma_dev, netdev_nb);

	if (dev->netdev == NULL || dev->netdev != netdev)
		goto done;

	switch (event) {
	case NETDEV_UP:
		dev->state = IB_PORT_ACTIVE;
		erdma_port_event(dev, IB_EVENT_PORT_ACTIVE);
		break;
	case NETDEV_DOWN:
		dev->state = IB_PORT_DOWN;
		erdma_port_event(dev, IB_EVENT_PORT_ERR);
		break;
	case NETDEV_CHANGEMTU:
		if (dev->mtu != netdev->mtu) {
			erdma_set_mtu(dev, netdev->mtu);
			dev->mtu = netdev->mtu;
		}
		break;
	case NETDEV_REGISTER:
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGEADDR:
	case NETDEV_GOING_DOWN:
	case NETDEV_CHANGE:
	default:
		break;
	}

done:
	return NOTIFY_OK;
}

static int erdma_enum_and_get_netdev(struct erdma_dev *dev)
{
	struct net_device *netdev;
	int ret = -EPROBE_DEFER;

	/* Already binded to a net_device, so we skip. */
	if (dev->netdev)
		return 0;

	rtnl_lock();
	for_each_netdev(&init_net, netdev) {
		/*
		 * In erdma, the paired netdev and ibdev should have the same
		 * MAC address. erdma can get the value from its PCIe bar
		 * registers. Since erdma can not get the paired netdev
		 * reference directly, we do a traverse here to get the paired
		 * netdev.
		 */
		if (ether_addr_equal_unaligned(netdev->perm_addr,
					       dev->attrs.peer_addr)) {
			ret = ib_device_set_netdev(&dev->ibdev, netdev, 1);
			if (ret) {
				rtnl_unlock();
				ibdev_warn(&dev->ibdev,
					   "failed (%d) to link netdev", ret);
				return ret;
			}

			dev->netdev = netdev;
			break;
		}
	}

	rtnl_unlock();

	return ret;
}

static int erdma_device_register(struct erdma_dev *dev)
{
	struct ib_device *ibdev = &dev->ibdev;
	int ret;

	ret = erdma_enum_and_get_netdev(dev);
	if (ret)
		return ret;

	dev->mtu = dev->netdev->mtu;
	addrconf_addr_eui48((u8 *)&ibdev->node_guid, dev->netdev->dev_addr);

	ret = ib_register_device(ibdev, "erdma_%d", &dev->pdev->dev);
	if (ret) {
		dev_err(&dev->pdev->dev,
			"ib_register_device failed: ret = %d\n", ret);
		return ret;
	}

	dev->netdev_nb.notifier_call = erdma_netdev_event;
	ret = register_netdevice_notifier(&dev->netdev_nb);
	if (ret) {
		ibdev_err(&dev->ibdev, "failed to register notifier.\n");
		ib_unregister_device(ibdev);
	}

	return ret;
}

static irqreturn_t erdma_comm_irq_handler(int irq, void *data)
{
	struct erdma_dev *dev = data;

	erdma_cmdq_completion_handler(&dev->cmdq);
	erdma_aeq_event_handler(dev);

	return IRQ_HANDLED;
}

static void erdma_dwqe_resource_init(struct erdma_dev *dev)
{
	int total_pages, type0, type1;

	dev->attrs.grp_num = erdma_reg_read32(dev, ERDMA_REGS_GRP_NUM_REG);

	if (dev->attrs.grp_num < 4)
		dev->attrs.disable_dwqe = true;
	else
		dev->attrs.disable_dwqe = false;

	/* One page contains 4 goups. */
	total_pages = dev->attrs.grp_num * 4;

	if (dev->attrs.grp_num >= ERDMA_DWQE_MAX_GRP_CNT) {
		dev->attrs.grp_num = ERDMA_DWQE_MAX_GRP_CNT;
		type0 = ERDMA_DWQE_TYPE0_CNT;
		type1 = ERDMA_DWQE_TYPE1_CNT / ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
	} else {
		type1 = total_pages / 3;
		type0 = total_pages - type1 - 1;
	}

	dev->attrs.dwqe_pages = type0;
	dev->attrs.dwqe_entries = type1 * ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
}

static int erdma_request_vectors(struct erdma_dev *dev)
{
	int expect_irq_num = min(num_possible_cpus() + 1, ERDMA_NUM_MSIX_VEC);
	int ret;

	ret = pci_alloc_irq_vectors(dev->pdev, 1, expect_irq_num, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&dev->pdev->dev, "request irq vectors failed(%d)\n",
			ret);
		return ret;
	}
	dev->attrs.irq_num = ret;

	return 0;
}

static int erdma_comm_irq_init(struct erdma_dev *dev)
{
	snprintf(dev->comm_irq.name, ERDMA_IRQNAME_SIZE, "erdma-common@pci:%s",
		 pci_name(dev->pdev));
	dev->comm_irq.msix_vector =
		pci_irq_vector(dev->pdev, ERDMA_MSIX_VECTOR_CMDQ);

	cpumask_set_cpu(cpumask_first(cpumask_of_pcibus(dev->pdev->bus)),
			&dev->comm_irq.affinity_hint_mask);
	irq_set_affinity_hint(dev->comm_irq.msix_vector,
			      &dev->comm_irq.affinity_hint_mask);

	return request_irq(dev->comm_irq.msix_vector, erdma_comm_irq_handler, 0,
			   dev->comm_irq.name, dev);
}

static void erdma_comm_irq_uninit(struct erdma_dev *dev)
{
	irq_set_affinity_hint(dev->comm_irq.msix_vector, NULL);
	free_irq(dev->comm_irq.msix_vector, dev);
}

static int erdma_device_init(struct erdma_dev *dev, struct pci_dev *pdev)
{
	int ret;

	erdma_dwqe_resource_init(dev);

	ret = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(ERDMA_PCI_WIDTH));
	if (ret)
		return ret;

	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	return 0;
}

static void erdma_device_uninit(struct erdma_dev *dev)
{
	u32 ctrl = FIELD_PREP(ERDMA_REG_DEV_CTRL_RESET_MASK, 1);

	erdma_reg_write32(dev, ERDMA_REGS_DEV_CTRL_REG, ctrl);
}

static const struct pci_device_id erdma_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALIBABA, 0x107f) },
	{}
};

static int erdma_probe_dev(struct pci_dev *pdev)
{
	struct erdma_dev *dev;
	int bars, err;
	u32 version;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed(%d)\n", err);
		return err;
	}

	pci_set_master(pdev);

	dev = ib_alloc_device(erdma_dev, ibdev);
	if (!dev) {
		dev_err(&pdev->dev, "ib_alloc_device failed\n");
		err = -ENOMEM;
		goto err_disable_device;
	}

	pci_set_drvdata(pdev, dev);
	dev->pdev = pdev;
	dev->attrs.numa_node = dev_to_node(&pdev->dev);

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
	if (bars != ERDMA_BAR_MASK || err) {
		err = err ? err : -EINVAL;
		goto err_ib_device_release;
	}

	dev->func_bar_addr = pci_resource_start(pdev, ERDMA_FUNC_BAR);
	dev->func_bar_len = pci_resource_len(pdev, ERDMA_FUNC_BAR);

	dev->func_bar =
		devm_ioremap(&pdev->dev, dev->func_bar_addr, dev->func_bar_len);
	if (!dev->func_bar) {
		dev_err(&pdev->dev, "devm_ioremap failed.\n");
		err = -EFAULT;
		goto err_release_bars;
	}

	version = erdma_reg_read32(dev, ERDMA_REGS_VERSION_REG);
	if (version == 0) {
		/* we knows that it is a non-functional function. */
		err = -ENODEV;
		goto err_iounmap_func_bar;
	}

	err = erdma_device_init(dev, pdev);
	if (err)
		goto err_iounmap_func_bar;

	err = erdma_request_vectors(dev);
	if (err)
		goto err_iounmap_func_bar;

	err = erdma_comm_irq_init(dev);
	if (err)
		goto err_free_vectors;

	err = erdma_aeq_init(dev);
	if (err)
		goto err_uninit_comm_irq;

	err = erdma_cmdq_init(dev);
	if (err)
		goto err_uninit_aeq;

	err = erdma_ceqs_init(dev);
	if (err)
		goto err_uninit_cmdq;

	erdma_finish_cmdq_init(dev);

	return 0;

err_uninit_cmdq:
	erdma_device_uninit(dev);
	erdma_cmdq_destroy(dev);

err_uninit_aeq:
	erdma_aeq_destroy(dev);

err_uninit_comm_irq:
	erdma_comm_irq_uninit(dev);

err_free_vectors:
	pci_free_irq_vectors(dev->pdev);

err_iounmap_func_bar:
	devm_iounmap(&pdev->dev, dev->func_bar);

err_release_bars:
	pci_release_selected_regions(pdev, bars);

err_ib_device_release:
	ib_dealloc_device(&dev->ibdev);

err_disable_device:
	pci_disable_device(pdev);

	return err;
}

static void erdma_remove_dev(struct pci_dev *pdev)
{
	struct erdma_dev *dev = pci_get_drvdata(pdev);

	erdma_ceqs_uninit(dev);

	erdma_device_uninit(dev);

	erdma_cmdq_destroy(dev);
	erdma_aeq_destroy(dev);
	erdma_comm_irq_uninit(dev);
	pci_free_irq_vectors(dev->pdev);

	devm_iounmap(&pdev->dev, dev->func_bar);
	pci_release_selected_regions(pdev, ERDMA_BAR_MASK);

	ib_dealloc_device(&dev->ibdev);

	pci_disable_device(pdev);
}

#define ERDMA_GET_CAP(name, cap) FIELD_GET(ERDMA_CMD_DEV_CAP_##name##_MASK, cap)

static int erdma_dev_attrs_init(struct erdma_dev *dev)
{
	int err;
	u64 req_hdr, cap0, cap1;

	erdma_cmdq_build_reqhdr(&req_hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_QUERY_DEVICE);

	err = erdma_post_cmd_wait(&dev->cmdq, &req_hdr, sizeof(req_hdr), &cap0,
				  &cap1);
	if (err)
		return err;

	dev->attrs.max_cqe = 1 << ERDMA_GET_CAP(MAX_CQE, cap0);
	dev->attrs.max_mr_size = 1ULL << ERDMA_GET_CAP(MAX_MR_SIZE, cap0);
	dev->attrs.max_mw = 1 << ERDMA_GET_CAP(MAX_MW, cap1);
	dev->attrs.max_recv_wr = 1 << ERDMA_GET_CAP(MAX_RECV_WR, cap0);
	dev->attrs.local_dma_key = ERDMA_GET_CAP(DMA_LOCAL_KEY, cap1);
	dev->attrs.cc = ERDMA_GET_CAP(DEFAULT_CC, cap1);
	dev->attrs.max_qp = ERDMA_NQP_PER_QBLOCK * ERDMA_GET_CAP(QBLOCK, cap1);
	dev->attrs.max_mr = dev->attrs.max_qp << 1;
	dev->attrs.max_cq = dev->attrs.max_qp << 1;

	dev->attrs.max_send_wr = ERDMA_MAX_SEND_WR;
	dev->attrs.max_ord = ERDMA_MAX_ORD;
	dev->attrs.max_ird = ERDMA_MAX_IRD;
	dev->attrs.max_send_sge = ERDMA_MAX_SEND_SGE;
	dev->attrs.max_recv_sge = ERDMA_MAX_RECV_SGE;
	dev->attrs.max_sge_rd = ERDMA_MAX_SGE_RD;
	dev->attrs.max_pd = ERDMA_MAX_PD;

	dev->res_cb[ERDMA_RES_TYPE_PD].max_cap = ERDMA_MAX_PD;
	dev->res_cb[ERDMA_RES_TYPE_STAG_IDX].max_cap = dev->attrs.max_mr;

	erdma_cmdq_build_reqhdr(&req_hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_QUERY_FW_INFO);

	err = erdma_post_cmd_wait(&dev->cmdq, &req_hdr, sizeof(req_hdr), &cap0,
				  &cap1);
	if (!err)
		dev->attrs.fw_version =
			FIELD_GET(ERDMA_CMD_INFO0_FW_VER_MASK, cap0);

	return err;
}

static int erdma_res_cb_init(struct erdma_dev *dev)
{
	int i, j;

	for (i = 0; i < ERDMA_RES_CNT; i++) {
		dev->res_cb[i].next_alloc_idx = 1;
		spin_lock_init(&dev->res_cb[i].lock);
		dev->res_cb[i].bitmap =
			bitmap_zalloc(dev->res_cb[i].max_cap, GFP_KERNEL);
		if (!dev->res_cb[i].bitmap)
			goto err;
	}

	return 0;

err:
	for (j = 0; j < i; j++)
		bitmap_free(dev->res_cb[j].bitmap);

	return -ENOMEM;
}

static void erdma_res_cb_free(struct erdma_dev *dev)
{
	int i;

	for (i = 0; i < ERDMA_RES_CNT; i++)
		bitmap_free(dev->res_cb[i].bitmap);
}

static const struct ib_device_ops erdma_device_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_ERDMA,
	.uverbs_abi_ver = ERDMA_ABI_VERSION,

	.alloc_mr = erdma_ib_alloc_mr,
	.alloc_pd = erdma_alloc_pd,
	.alloc_ucontext = erdma_alloc_ucontext,
	.create_cq = erdma_create_cq,
	.create_qp = erdma_create_qp,
	.dealloc_pd = erdma_dealloc_pd,
	.dealloc_ucontext = erdma_dealloc_ucontext,
	.dereg_mr = erdma_dereg_mr,
	.destroy_cq = erdma_destroy_cq,
	.destroy_qp = erdma_destroy_qp,
	.get_dma_mr = erdma_get_dma_mr,
	.get_port_immutable = erdma_get_port_immutable,
	.iw_accept = erdma_accept,
	.iw_add_ref = erdma_qp_get_ref,
	.iw_connect = erdma_connect,
	.iw_create_listen = erdma_create_listen,
	.iw_destroy_listen = erdma_destroy_listen,
	.iw_get_qp = erdma_get_ibqp,
	.iw_reject = erdma_reject,
	.iw_rem_ref = erdma_qp_put_ref,
	.map_mr_sg = erdma_map_mr_sg,
	.mmap = erdma_mmap,
	.mmap_free = erdma_mmap_free,
	.modify_qp = erdma_modify_qp,
	.post_recv = erdma_post_recv,
	.post_send = erdma_post_send,
	.poll_cq = erdma_poll_cq,
	.query_device = erdma_query_device,
	.query_gid = erdma_query_gid,
	.query_port = erdma_query_port,
	.query_qp = erdma_query_qp,
	.req_notify_cq = erdma_req_notify_cq,
	.reg_user_mr = erdma_reg_user_mr,

	INIT_RDMA_OBJ_SIZE(ib_cq, erdma_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, erdma_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, erdma_ucontext, ibucontext),
	INIT_RDMA_OBJ_SIZE(ib_qp, erdma_qp, ibqp),
};

static int erdma_ib_device_add(struct pci_dev *pdev)
{
	struct erdma_dev *dev = pci_get_drvdata(pdev);
	struct ib_device *ibdev = &dev->ibdev;
	u64 mac;
	int ret;

	ret = erdma_dev_attrs_init(dev);
	if (ret)
		return ret;

	ibdev->node_type = RDMA_NODE_RNIC;
	memcpy(ibdev->node_desc, ERDMA_NODE_DESC, sizeof(ERDMA_NODE_DESC));

	/*
	 * Current model (one-to-one device association):
	 * One ERDMA device per net_device or, equivalently,
	 * per physical port.
	 */
	ibdev->phys_port_cnt = 1;
	ibdev->num_comp_vectors = dev->attrs.irq_num - 1;

	ib_set_device_ops(ibdev, &erdma_device_ops);

	INIT_LIST_HEAD(&dev->cep_list);

	spin_lock_init(&dev->lock);
	xa_init_flags(&dev->qp_xa, XA_FLAGS_ALLOC1);
	xa_init_flags(&dev->cq_xa, XA_FLAGS_ALLOC1);
	dev->next_alloc_cqn = 1;
	dev->next_alloc_qpn = 1;

	ret = erdma_res_cb_init(dev);
	if (ret)
		return ret;

	spin_lock_init(&dev->db_bitmap_lock);
	bitmap_zero(dev->sdb_page, ERDMA_DWQE_TYPE0_CNT);
	bitmap_zero(dev->sdb_entry, ERDMA_DWQE_TYPE1_CNT);

	atomic_set(&dev->num_ctx, 0);

	mac = erdma_reg_read32(dev, ERDMA_REGS_NETDEV_MAC_L_REG);
	mac |= (u64)erdma_reg_read32(dev, ERDMA_REGS_NETDEV_MAC_H_REG) << 32;

	u64_to_ether_addr(mac, dev->attrs.peer_addr);

	ret = erdma_device_register(dev);
	if (ret)
		goto err_out;

	return 0;

err_out:
	xa_destroy(&dev->qp_xa);
	xa_destroy(&dev->cq_xa);

	erdma_res_cb_free(dev);

	return ret;
}

static void erdma_ib_device_remove(struct pci_dev *pdev)
{
	struct erdma_dev *dev = pci_get_drvdata(pdev);

	unregister_netdevice_notifier(&dev->netdev_nb);
	ib_unregister_device(&dev->ibdev);

	erdma_res_cb_free(dev);
	xa_destroy(&dev->qp_xa);
	xa_destroy(&dev->cq_xa);
}

static int erdma_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret;

	ret = erdma_probe_dev(pdev);
	if (ret)
		return ret;

	ret = erdma_ib_device_add(pdev);
	if (ret) {
		erdma_remove_dev(pdev);
		return ret;
	}

	return 0;
}

static void erdma_remove(struct pci_dev *pdev)
{
	erdma_ib_device_remove(pdev);
	erdma_remove_dev(pdev);
}

static struct pci_driver erdma_pci_driver = {
	.name = DRV_MODULE_NAME,
	.id_table = erdma_pci_tbl,
	.probe = erdma_probe,
	.remove = erdma_remove
};

MODULE_DEVICE_TABLE(pci, erdma_pci_tbl);

static __init int erdma_init_module(void)
{
	int ret;

	ret = erdma_cm_init();
	if (ret)
		return ret;

	ret = pci_register_driver(&erdma_pci_driver);
	if (ret)
		erdma_cm_exit();

	return ret;
}

static void __exit erdma_exit_module(void)
{
	pci_unregister_driver(&erdma_pci_driver);

	erdma_cm_exit();
}

module_init(erdma_init_module);
module_exit(erdma_exit_module);
