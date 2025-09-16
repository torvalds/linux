// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"

MODULE_ALIAS("i40iw");
MODULE_DESCRIPTION("Intel(R) Ethernet Protocol Driver for RDMA");
MODULE_LICENSE("Dual BSD/GPL");

static struct notifier_block irdma_inetaddr_notifier = {
	.notifier_call = irdma_inetaddr_event
};

static struct notifier_block irdma_inetaddr6_notifier = {
	.notifier_call = irdma_inet6addr_event
};

static struct notifier_block irdma_net_notifier = {
	.notifier_call = irdma_net_event
};

static struct notifier_block irdma_netdevice_notifier = {
	.notifier_call = irdma_netdevice_event
};

static void irdma_register_notifiers(void)
{
	register_inetaddr_notifier(&irdma_inetaddr_notifier);
	register_inet6addr_notifier(&irdma_inetaddr6_notifier);
	register_netevent_notifier(&irdma_net_notifier);
	register_netdevice_notifier(&irdma_netdevice_notifier);
}

static void irdma_unregister_notifiers(void)
{
	unregister_netevent_notifier(&irdma_net_notifier);
	unregister_inetaddr_notifier(&irdma_inetaddr_notifier);
	unregister_inet6addr_notifier(&irdma_inetaddr6_notifier);
	unregister_netdevice_notifier(&irdma_netdevice_notifier);
}

static void irdma_prep_tc_change(struct irdma_device *iwdev)
{
	iwdev->vsi.tc_change_pending = true;
	irdma_sc_suspend_resume_qps(&iwdev->vsi, IRDMA_OP_SUSPEND);

	/* Wait for all qp's to suspend */
	wait_event_timeout(iwdev->suspend_wq,
			   !atomic_read(&iwdev->vsi.qp_suspend_reqs),
			   msecs_to_jiffies(IRDMA_EVENT_TIMEOUT_MS));
	irdma_ws_reset(&iwdev->vsi);
}

static void irdma_log_invalid_mtu(u16 mtu, struct irdma_sc_dev *dev)
{
	if (mtu < IRDMA_MIN_MTU_IPV4)
		ibdev_warn(to_ibdev(dev), "MTU setting [%d] too low for RDMA traffic. Minimum MTU is 576 for IPv4\n", mtu);
	else if (mtu < IRDMA_MIN_MTU_IPV6)
		ibdev_warn(to_ibdev(dev), "MTU setting [%d] too low for RDMA traffic. Minimum MTU is 1280 for IPv6\\n", mtu);
}

static void irdma_fill_qos_info(struct irdma_l2params *l2params,
				struct iidc_rdma_qos_params *qos_info)
{
	int i;

	l2params->num_tc = qos_info->num_tc;
	l2params->vsi_prio_type = qos_info->vport_priority_type;
	l2params->vsi_rel_bw = qos_info->vport_relative_bw;
	for (i = 0; i < l2params->num_tc; i++) {
		l2params->tc_info[i].egress_virt_up =
			qos_info->tc_info[i].egress_virt_up;
		l2params->tc_info[i].ingress_virt_up =
			qos_info->tc_info[i].ingress_virt_up;
		l2params->tc_info[i].prio_type = qos_info->tc_info[i].prio_type;
		l2params->tc_info[i].rel_bw = qos_info->tc_info[i].rel_bw;
		l2params->tc_info[i].tc_ctx = qos_info->tc_info[i].tc_ctx;
	}
	for (i = 0; i < IIDC_MAX_USER_PRIORITY; i++)
		l2params->up2tc[i] = qos_info->up2tc[i];
	if (qos_info->pfc_mode == IIDC_DSCP_PFC_MODE) {
		l2params->dscp_mode = true;
		memcpy(l2params->dscp_map, qos_info->dscp_map, sizeof(l2params->dscp_map));
	}
}

static void irdma_iidc_event_handler(struct iidc_rdma_core_dev_info *cdev_info,
				     struct iidc_rdma_event *event)
{
	struct irdma_device *iwdev = dev_get_drvdata(&cdev_info->adev->dev);
	struct irdma_l2params l2params = {};

	if (*event->type & BIT(IIDC_RDMA_EVENT_AFTER_MTU_CHANGE)) {
		ibdev_dbg(&iwdev->ibdev, "CLNT: new MTU = %d\n", iwdev->netdev->mtu);
		if (iwdev->vsi.mtu != iwdev->netdev->mtu) {
			l2params.mtu = iwdev->netdev->mtu;
			l2params.mtu_changed = true;
			irdma_log_invalid_mtu(l2params.mtu, &iwdev->rf->sc_dev);
			irdma_change_l2params(&iwdev->vsi, &l2params);
		}
	} else if (*event->type & BIT(IIDC_RDMA_EVENT_BEFORE_TC_CHANGE)) {
		if (iwdev->vsi.tc_change_pending)
			return;

		irdma_prep_tc_change(iwdev);
	} else if (*event->type & BIT(IIDC_RDMA_EVENT_AFTER_TC_CHANGE)) {
		struct iidc_rdma_priv_dev_info *iidc_priv = cdev_info->iidc_priv;

		if (!iwdev->vsi.tc_change_pending)
			return;

		l2params.tc_changed = true;
		ibdev_dbg(&iwdev->ibdev, "CLNT: TC Change\n");

		irdma_fill_qos_info(&l2params, &iidc_priv->qos_info);
		if (iwdev->rf->protocol_used != IRDMA_IWARP_PROTOCOL_ONLY)
			iwdev->dcb_vlan_mode =
				l2params.num_tc > 1 && !l2params.dscp_mode;
		irdma_change_l2params(&iwdev->vsi, &l2params);
	} else if (*event->type & BIT(IIDC_RDMA_EVENT_CRIT_ERR)) {
		ibdev_warn(&iwdev->ibdev, "ICE OICR event notification: oicr = 0x%08x\n",
			   event->reg);
		if (event->reg & IRDMAPFINT_OICR_PE_CRITERR_M) {
			u32 pe_criterr;

			pe_criterr = readl(iwdev->rf->sc_dev.hw_regs[IRDMA_GLPE_CRITERR]);
#define IRDMA_Q1_RESOURCE_ERR 0x0001024d
			if (pe_criterr != IRDMA_Q1_RESOURCE_ERR) {
				ibdev_err(&iwdev->ibdev, "critical PE Error, GLPE_CRITERR=0x%08x\n",
					  pe_criterr);
				iwdev->rf->reset = true;
			} else {
				ibdev_warn(&iwdev->ibdev, "Q1 Resource Check\n");
			}
		}
		if (event->reg & IRDMAPFINT_OICR_HMC_ERR_M) {
			ibdev_err(&iwdev->ibdev, "HMC Error\n");
			iwdev->rf->reset = true;
		}
		if (event->reg & IRDMAPFINT_OICR_PE_PUSH_M) {
			ibdev_err(&iwdev->ibdev, "PE Push Error\n");
			iwdev->rf->reset = true;
		}
		if (iwdev->rf->reset)
			iwdev->rf->gen_ops.request_reset(iwdev->rf);
	}
}

/**
 * irdma_request_reset - Request a reset
 * @rf: RDMA PCI function
 */
static void irdma_request_reset(struct irdma_pci_f *rf)
{
	ibdev_warn(&rf->iwdev->ibdev, "Requesting a reset\n");
	ice_rdma_request_reset(rf->cdev, IIDC_FUNC_RESET);
}

/**
 * irdma_lan_register_qset - Register qset with LAN driver
 * @vsi: vsi structure
 * @tc_node: Traffic class node
 */
static int irdma_lan_register_qset(struct irdma_sc_vsi *vsi,
				   struct irdma_ws_node *tc_node)
{
	struct irdma_device *iwdev = vsi->back_vsi;
	struct iidc_rdma_core_dev_info *cdev_info;
	struct iidc_rdma_qset_params qset = {};
	int ret;

	cdev_info = iwdev->rf->cdev;
	qset.qs_handle = tc_node->qs_handle;
	qset.tc = tc_node->traffic_class;
	qset.vport_id = vsi->vsi_idx;
	ret = ice_add_rdma_qset(cdev_info, &qset);
	if (ret) {
		ibdev_dbg(&iwdev->ibdev, "WS: LAN alloc_res for rdma qset failed.\n");
		return ret;
	}

	tc_node->l2_sched_node_id = qset.teid;
	vsi->qos[tc_node->user_pri].l2_sched_node_id = qset.teid;

	return 0;
}

/**
 * irdma_lan_unregister_qset - Unregister qset with LAN driver
 * @vsi: vsi structure
 * @tc_node: Traffic class node
 */
static void irdma_lan_unregister_qset(struct irdma_sc_vsi *vsi,
				      struct irdma_ws_node *tc_node)
{
	struct irdma_device *iwdev = vsi->back_vsi;
	struct iidc_rdma_core_dev_info *cdev_info;
	struct iidc_rdma_qset_params qset = {};

	cdev_info = iwdev->rf->cdev;
	qset.qs_handle = tc_node->qs_handle;
	qset.tc = tc_node->traffic_class;
	qset.vport_id = vsi->vsi_idx;
	qset.teid = tc_node->l2_sched_node_id;

	if (ice_del_rdma_qset(cdev_info, &qset))
		ibdev_dbg(&iwdev->ibdev, "WS: LAN free_res for rdma qset failed.\n");
}

static int irdma_init_interrupts(struct irdma_pci_f *rf, struct iidc_rdma_core_dev_info *cdev)
{
	int i;

	rf->msix_count = num_online_cpus() + IRDMA_NUM_AEQ_MSIX;
	rf->msix_entries = kcalloc(rf->msix_count, sizeof(*rf->msix_entries),
				   GFP_KERNEL);
	if (!rf->msix_entries)
		return -ENOMEM;

	for (i = 0; i < rf->msix_count; i++)
		if (ice_alloc_rdma_qvector(cdev, &rf->msix_entries[i]))
			break;

	if (i < IRDMA_MIN_MSIX) {
		while (--i >= 0)
			ice_free_rdma_qvector(cdev, &rf->msix_entries[i]);

		kfree(rf->msix_entries);
		return -ENOMEM;
	}

	rf->msix_count = i;

	return 0;
}

static void irdma_deinit_interrupts(struct irdma_pci_f *rf, struct iidc_rdma_core_dev_info *cdev)
{
	int i;

	for (i = 0; i < rf->msix_count; i++)
		ice_free_rdma_qvector(cdev, &rf->msix_entries[i]);

	kfree(rf->msix_entries);
}

static void irdma_remove(struct auxiliary_device *aux_dev)
{
	struct irdma_device *iwdev = auxiliary_get_drvdata(aux_dev);
	struct iidc_rdma_core_auxiliary_dev *iidc_adev;
	struct iidc_rdma_core_dev_info *cdev_info;

	iidc_adev = container_of(aux_dev, struct iidc_rdma_core_auxiliary_dev, adev);
	cdev_info = iidc_adev->cdev_info;

	ice_rdma_update_vsi_filter(cdev_info, iwdev->vsi_num, false);
	irdma_ib_unregister_device(iwdev);
	irdma_deinit_interrupts(iwdev->rf, cdev_info);

	kfree(iwdev->rf);

	pr_debug("INIT: Gen2 PF[%d] device remove success\n", PCI_FUNC(cdev_info->pdev->devfn));
}

static void irdma_fill_device_info(struct irdma_device *iwdev,
				   struct iidc_rdma_core_dev_info *cdev_info)
{
	struct iidc_rdma_priv_dev_info *iidc_priv = cdev_info->iidc_priv;
	struct irdma_pci_f *rf = iwdev->rf;

	rf->sc_dev.hw = &rf->hw;
	rf->iwdev = iwdev;
	rf->cdev = cdev_info;
	rf->hw.hw_addr = iidc_priv->hw_addr;
	rf->pcidev = cdev_info->pdev;
	rf->hw.device = &rf->pcidev->dev;
	rf->pf_id = iidc_priv->pf_id;
	rf->gen_ops.register_qset = irdma_lan_register_qset;
	rf->gen_ops.unregister_qset = irdma_lan_unregister_qset;

	rf->default_vsi.vsi_idx = iidc_priv->vport_id;
	rf->protocol_used =
		cdev_info->rdma_protocol == IIDC_RDMA_PROTOCOL_ROCEV2 ?
		IRDMA_ROCE_PROTOCOL_ONLY : IRDMA_IWARP_PROTOCOL_ONLY;
	rf->rdma_ver = IRDMA_GEN_2;
	rf->rsrc_profile = IRDMA_HMC_PROFILE_DEFAULT;
	rf->rst_to = IRDMA_RST_TIMEOUT_HZ;
	rf->gen_ops.request_reset = irdma_request_reset;
	rf->limits_sel = 7;
	rf->iwdev = iwdev;

	mutex_init(&iwdev->ah_tbl_lock);

	iwdev->netdev = iidc_priv->netdev;
	iwdev->vsi_num = iidc_priv->vport_id;
	iwdev->init_state = INITIAL_STATE;
	iwdev->roce_cwnd = IRDMA_ROCE_CWND_DEFAULT;
	iwdev->roce_ackcreds = IRDMA_ROCE_ACKCREDS_DEFAULT;
	iwdev->rcv_wnd = IRDMA_CM_DEFAULT_RCV_WND_SCALED;
	iwdev->rcv_wscale = IRDMA_CM_DEFAULT_RCV_WND_SCALE;
	if (rf->protocol_used == IRDMA_ROCE_PROTOCOL_ONLY)
		iwdev->roce_mode = true;
}

static int irdma_probe(struct auxiliary_device *aux_dev, const struct auxiliary_device_id *id)
{
	struct iidc_rdma_core_auxiliary_dev *iidc_adev;
	struct iidc_rdma_core_dev_info *cdev_info;
	struct iidc_rdma_priv_dev_info *iidc_priv;
	struct irdma_l2params l2params = {};
	struct irdma_device *iwdev;
	struct irdma_pci_f *rf;
	int err;

	iidc_adev = container_of(aux_dev, struct iidc_rdma_core_auxiliary_dev, adev);
	cdev_info = iidc_adev->cdev_info;
	iidc_priv = cdev_info->iidc_priv;

	iwdev = ib_alloc_device(irdma_device, ibdev);
	if (!iwdev)
		return -ENOMEM;
	iwdev->rf = kzalloc(sizeof(*rf), GFP_KERNEL);
	if (!iwdev->rf) {
		ib_dealloc_device(&iwdev->ibdev);
		return -ENOMEM;
	}

	irdma_fill_device_info(iwdev, cdev_info);
	rf = iwdev->rf;

	err = irdma_init_interrupts(rf, cdev_info);
	if (err)
		goto err_init_interrupts;

	err = irdma_ctrl_init_hw(rf);
	if (err)
		goto err_ctrl_init;

	l2params.mtu = iwdev->netdev->mtu;
	irdma_fill_qos_info(&l2params, &iidc_priv->qos_info);
	if (iwdev->rf->protocol_used != IRDMA_IWARP_PROTOCOL_ONLY)
		iwdev->dcb_vlan_mode = l2params.num_tc > 1 && !l2params.dscp_mode;

	err = irdma_rt_init_hw(iwdev, &l2params);
	if (err)
		goto err_rt_init;

	err = irdma_ib_register_device(iwdev);
	if (err)
		goto err_ibreg;

	ice_rdma_update_vsi_filter(cdev_info, iwdev->vsi_num, true);

	ibdev_dbg(&iwdev->ibdev, "INIT: Gen2 PF[%d] device probe success\n", PCI_FUNC(rf->pcidev->devfn));
	auxiliary_set_drvdata(aux_dev, iwdev);

	return 0;

err_ibreg:
	irdma_rt_deinit_hw(iwdev);
err_rt_init:
	irdma_ctrl_deinit_hw(rf);
err_ctrl_init:
	irdma_deinit_interrupts(rf, cdev_info);
err_init_interrupts:
	kfree(iwdev->rf);
	ib_dealloc_device(&iwdev->ibdev);

	return err;
}

static const struct auxiliary_device_id irdma_auxiliary_id_table[] = {
	{.name = "ice.iwarp", },
	{.name = "ice.roce", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, irdma_auxiliary_id_table);

static struct iidc_rdma_core_auxiliary_drv irdma_auxiliary_drv = {
	.adrv = {
	    .id_table = irdma_auxiliary_id_table,
	    .probe = irdma_probe,
	    .remove = irdma_remove,
	},
	.event_handler = irdma_iidc_event_handler,
};

static int __init irdma_init_module(void)
{
	int ret;

	ret = auxiliary_driver_register(&i40iw_auxiliary_drv);
	if (ret) {
		pr_err("Failed i40iw(gen_1) auxiliary_driver_register() ret=%d\n",
		       ret);
		return ret;
	}

	ret = auxiliary_driver_register(&irdma_auxiliary_drv.adrv);
	if (ret) {
		auxiliary_driver_unregister(&i40iw_auxiliary_drv);
		pr_err("Failed irdma auxiliary_driver_register() ret=%d\n",
		       ret);
		return ret;
	}

	irdma_register_notifiers();

	return 0;
}

static void __exit irdma_exit_module(void)
{
	irdma_unregister_notifiers();
	auxiliary_driver_unregister(&irdma_auxiliary_drv.adrv);
	auxiliary_driver_unregister(&i40iw_auxiliary_drv);
}

module_init(irdma_init_module);
module_exit(irdma_exit_module);
