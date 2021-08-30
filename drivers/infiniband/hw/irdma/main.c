// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"
#include "../../../net/ethernet/intel/ice/ice.h"

MODULE_ALIAS("i40iw");
MODULE_AUTHOR("Intel Corporation, <e1000-rdma@lists.sourceforge.net>");
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
			   IRDMA_EVENT_TIMEOUT);
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
				struct iidc_qos_params *qos_info)
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
}

static void irdma_iidc_event_handler(struct ice_pf *pf, struct iidc_event *event)
{
	struct irdma_device *iwdev = dev_get_drvdata(&pf->adev->dev);
	struct irdma_l2params l2params = {};

	if (*event->type & BIT(IIDC_EVENT_AFTER_MTU_CHANGE)) {
		ibdev_dbg(&iwdev->ibdev, "CLNT: new MTU = %d\n", iwdev->netdev->mtu);
		if (iwdev->vsi.mtu != iwdev->netdev->mtu) {
			l2params.mtu = iwdev->netdev->mtu;
			l2params.mtu_changed = true;
			irdma_log_invalid_mtu(l2params.mtu, &iwdev->rf->sc_dev);
			irdma_change_l2params(&iwdev->vsi, &l2params);
		}
	} else if (*event->type & BIT(IIDC_EVENT_BEFORE_TC_CHANGE)) {
		if (iwdev->vsi.tc_change_pending)
			return;

		irdma_prep_tc_change(iwdev);
	} else if (*event->type & BIT(IIDC_EVENT_AFTER_TC_CHANGE)) {
		struct iidc_qos_params qos_info = {};

		if (!iwdev->vsi.tc_change_pending)
			return;

		l2params.tc_changed = true;
		ibdev_dbg(&iwdev->ibdev, "CLNT: TC Change\n");
		ice_get_qos_params(pf, &qos_info);
		iwdev->dcb = qos_info.num_tc > 1;
		irdma_fill_qos_info(&l2params, &qos_info);
		irdma_change_l2params(&iwdev->vsi, &l2params);
	} else if (*event->type & BIT(IIDC_EVENT_CRIT_ERR)) {
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
	struct ice_pf *pf = rf->cdev;

	ibdev_warn(&rf->iwdev->ibdev, "Requesting a reset\n");
	ice_rdma_request_reset(pf, IIDC_PFR);
}

/**
 * irdma_lan_register_qset - Register qset with LAN driver
 * @vsi: vsi structure
 * @tc_node: Traffic class node
 */
static enum irdma_status_code irdma_lan_register_qset(struct irdma_sc_vsi *vsi,
						      struct irdma_ws_node *tc_node)
{
	struct irdma_device *iwdev = vsi->back_vsi;
	struct ice_pf *pf = iwdev->rf->cdev;
	struct iidc_rdma_qset_params qset = {};
	int ret;

	qset.qs_handle = tc_node->qs_handle;
	qset.tc = tc_node->traffic_class;
	qset.vport_id = vsi->vsi_idx;
	ret = ice_add_rdma_qset(pf, &qset);
	if (ret) {
		ibdev_dbg(&iwdev->ibdev, "WS: LAN alloc_res for rdma qset failed.\n");
		return IRDMA_ERR_REG_QSET;
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
	struct ice_pf *pf = iwdev->rf->cdev;
	struct iidc_rdma_qset_params qset = {};

	qset.qs_handle = tc_node->qs_handle;
	qset.tc = tc_node->traffic_class;
	qset.vport_id = vsi->vsi_idx;
	qset.teid = tc_node->l2_sched_node_id;

	if (ice_del_rdma_qset(pf, &qset))
		ibdev_dbg(&iwdev->ibdev, "WS: LAN free_res for rdma qset failed.\n");
}

static void irdma_remove(struct auxiliary_device *aux_dev)
{
	struct iidc_auxiliary_dev *iidc_adev = container_of(aux_dev,
							    struct iidc_auxiliary_dev,
							    adev);
	struct ice_pf *pf = iidc_adev->pf;
	struct irdma_device *iwdev = dev_get_drvdata(&aux_dev->dev);

	irdma_ib_unregister_device(iwdev);
	ice_rdma_update_vsi_filter(pf, iwdev->vsi_num, false);

	pr_debug("INIT: Gen2 PF[%d] device remove success\n", PCI_FUNC(pf->pdev->devfn));
}

static void irdma_fill_device_info(struct irdma_device *iwdev, struct ice_pf *pf,
				   struct ice_vsi *vsi)
{
	struct irdma_pci_f *rf = iwdev->rf;

	rf->cdev = pf;
	rf->gen_ops.register_qset = irdma_lan_register_qset;
	rf->gen_ops.unregister_qset = irdma_lan_unregister_qset;
	rf->hw.hw_addr = pf->hw.hw_addr;
	rf->pcidev = pf->pdev;
	rf->msix_count =  pf->num_rdma_msix;
	rf->msix_entries = &pf->msix_entries[pf->rdma_base_vector];
	rf->default_vsi.vsi_idx = vsi->vsi_num;
	rf->protocol_used = IRDMA_ROCE_PROTOCOL_ONLY;
	rf->rdma_ver = IRDMA_GEN_2;
	rf->rsrc_profile = IRDMA_HMC_PROFILE_DEFAULT;
	rf->rst_to = IRDMA_RST_TIMEOUT_HZ;
	rf->gen_ops.request_reset = irdma_request_reset;
	rf->limits_sel = 7;
	rf->iwdev = iwdev;

	iwdev->netdev = vsi->netdev;
	iwdev->vsi_num = vsi->vsi_num;
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
	struct iidc_auxiliary_dev *iidc_adev = container_of(aux_dev,
							    struct iidc_auxiliary_dev,
							    adev);
	struct ice_pf *pf = iidc_adev->pf;
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct iidc_qos_params qos_info = {};
	struct irdma_device *iwdev;
	struct irdma_pci_f *rf;
	struct irdma_l2params l2params = {};
	int err;

	if (!vsi)
		return -EIO;
	iwdev = ib_alloc_device(irdma_device, ibdev);
	if (!iwdev)
		return -ENOMEM;
	iwdev->rf = kzalloc(sizeof(*rf), GFP_KERNEL);
	if (!iwdev->rf) {
		ib_dealloc_device(&iwdev->ibdev);
		return -ENOMEM;
	}

	irdma_fill_device_info(iwdev, pf, vsi);
	rf = iwdev->rf;

	if (irdma_ctrl_init_hw(rf)) {
		err = -EIO;
		goto err_ctrl_init;
	}

	l2params.mtu = iwdev->netdev->mtu;
	ice_get_qos_params(pf, &qos_info);
	irdma_fill_qos_info(&l2params, &qos_info);
	if (irdma_rt_init_hw(iwdev, &l2params)) {
		err = -EIO;
		goto err_rt_init;
	}

	err = irdma_ib_register_device(iwdev);
	if (err)
		goto err_ibreg;

	ice_rdma_update_vsi_filter(pf, iwdev->vsi_num, true);

	ibdev_dbg(&iwdev->ibdev, "INIT: Gen2 PF[%d] device probe success\n", PCI_FUNC(rf->pcidev->devfn));
	dev_set_drvdata(&aux_dev->dev, iwdev);

	return 0;

err_ibreg:
	irdma_rt_deinit_hw(iwdev);
err_rt_init:
	irdma_ctrl_deinit_hw(rf);
err_ctrl_init:
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

static struct iidc_auxiliary_drv irdma_auxiliary_drv = {
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
