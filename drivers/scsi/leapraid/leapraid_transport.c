// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#include <scsi/scsi_host.h>

#include "leapraid_func.h"

static struct leapraid_topo_node *leapraid_transport_topo_node_by_sas_addr(
		struct leapraid_adapter *adapter,
		u64 sas_addr,
		struct leapraid_card_port *card_port)
{
	if (adapter->dev_topo.card.sas_address == sas_addr)
		return &adapter->dev_topo.card;

	return leapraid_exp_find_by_sas_address(adapter, sas_addr, card_port);
}

static u8 leapraid_get_port_id_by_expander(struct leapraid_adapter *adapter,
					   struct sas_rphy *rphy)
{
	struct leapraid_topo_node *topo_node_exp;
	unsigned long flags;
	u8 port_id = 0xFF;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_for_each_entry(topo_node_exp, &adapter->dev_topo.exp_list, list) {
		if (topo_node_exp->rphy == rphy) {
			port_id = topo_node_exp->card_port->port_id;
			break;
		}
	}
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	return port_id;
}

static u8 leapraid_get_port_id_by_end_dev(struct leapraid_adapter *adapter,
					  struct sas_rphy *rphy)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	u8 port_id = 0xFF;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr_and_rphy(
			adapter,
			rphy->identify.sas_address,
			rphy);
	if (sas_dev) {
		port_id = sas_dev->card_port->port_id;
		leapraid_sdev_put(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	return port_id;
}

static u8 leapraid_transport_get_port_id_by_rphy(
		struct leapraid_adapter *adapter,
		struct sas_rphy *rphy)
{
	if (!rphy)
		return 0xFF;

	switch (rphy->identify.device_type) {
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		return leapraid_get_port_id_by_expander(adapter, rphy);
	case SAS_END_DEVICE:
		return leapraid_get_port_id_by_end_dev(adapter, rphy);
	default:
		return 0xFF;
	}
}

static enum sas_linkrate leapraid_transport_convert_phy_link_rate(u8 link_rate)
{
	unsigned int i;

	#define SAS_RATE_12G SAS_LINK_RATE_12_0_GBPS

	const struct linkrate_map {
		u8 in;
		enum sas_linkrate out;
	} linkrate_table[] = {
		{
			LEAPRAID_SAS_NEG_LINK_RATE_1_5,
			SAS_LINK_RATE_1_5_GBPS
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_3_0,
			SAS_LINK_RATE_3_0_GBPS
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_6_0,
			SAS_LINK_RATE_6_0_GBPS
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_12_0,
			SAS_RATE_12G
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_PHY_DISABLED,
			SAS_PHY_DISABLED
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED,
			SAS_LINK_RATE_FAILED
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_PORT_SELECTOR,
			SAS_SATA_PORT_SELECTOR
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_SMP_RESETTING,
			SAS_LINK_RATE_UNKNOWN
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE,
			SAS_LINK_RATE_UNKNOWN
		},
		{
			LEAPRAID_SAS_NEG_LINK_RATE_UNKNOWN_LINK_RATE,
			SAS_LINK_RATE_UNKNOWN
		},
	};

	for (i = 0; i < ARRAY_SIZE(linkrate_table); i++)
		if (linkrate_table[i].in == link_rate)
			return linkrate_table[i].out;

	return SAS_LINK_RATE_UNKNOWN;
}

static void leapraid_set_identify_protocol_flags(u32 dev_info,
						 struct sas_identify *identify)
{
	unsigned int i;

	const struct protocol_mapping {
		u32 mask;
		u32 *target;
		u32 protocol;
	} mappings[] = {
		{
			LEAPRAID_DEVTYP_SSP_INIT,
			&identify->initiator_port_protocols,
			SAS_PROTOCOL_SSP
		},
		{
			LEAPRAID_DEVTYP_STP_INIT,
			&identify->initiator_port_protocols,
			SAS_PROTOCOL_STP
		},
		{
			LEAPRAID_DEVTYP_SMP_INIT,
			&identify->initiator_port_protocols,
			SAS_PROTOCOL_SMP
		},
		{
			LEAPRAID_DEVTYP_SATA_HOST,
			&identify->initiator_port_protocols,
			SAS_PROTOCOL_SATA
		},
		{
			LEAPRAID_DEVTYP_SSP_TGT,
			&identify->target_port_protocols,
			SAS_PROTOCOL_SSP
		},
		{
			LEAPRAID_DEVTYP_STP_TGT,
			&identify->target_port_protocols,
			SAS_PROTOCOL_STP
		},
		{
			LEAPRAID_DEVTYP_SMP_TGT,
			&identify->target_port_protocols,
			SAS_PROTOCOL_SMP
		},
		{
			LEAPRAID_DEVTYP_SATA_DEV,
			&identify->target_port_protocols,
			SAS_PROTOCOL_SATA
		},
	};

	for (i = 0; i < ARRAY_SIZE(mappings); i++)
		if (dev_info & mappings[i].mask && mappings[i].target)
			*mappings[i].target |= mappings[i].protocol;
}

static int leapraid_transport_set_identify(struct leapraid_adapter *adapter,
					   u16 hdl,
					   struct sas_identify *identify)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_pg0;
	u32 dev_info;

	if ((adapter->access_ctrl.shost_recovering &&
	     !adapter->scan_dev_desc.driver_loading) ||
	    adapter->access_ctrl.pcie_recovering) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed, shost_recovering=%d pcie_recovering=%d\n",
			 __func__,
			 adapter->access_ctrl.shost_recovering,
			 adapter->access_ctrl.pcie_recovering);
		return -EFAULT;
	}

	cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
	cfgp2.handle = hdl;
	if (leapraid_op_config_page(adapter, &sas_dev_pg0, cfgp1,
				    cfgp2, GET_SAS_DEVICE_PG0))
		return -ENXIO;

	memset(identify, 0, sizeof(struct sas_identify));
	dev_info = le32_to_cpu(sas_dev_pg0.dev_info);
	identify->sas_address = le64_to_cpu(sas_dev_pg0.sas_address);
	identify->phy_identifier = sas_dev_pg0.phy_num;

	switch (dev_info & LEAPRAID_DEVTYP_MASK_DEV_TYPE) {
	case LEAPRAID_DEVTYP_NO_DEV:
		identify->device_type = SAS_PHY_UNUSED;
		break;
	case LEAPRAID_DEVTYP_END_DEV:
		identify->device_type = SAS_END_DEVICE;
		break;
	case LEAPRAID_DEVTYP_EDGE_EXPANDER:
		identify->device_type = SAS_EDGE_EXPANDER_DEVICE;
		break;
	case LEAPRAID_DEVTYP_FANOUT_EXPANDER:
		identify->device_type = SAS_FANOUT_EXPANDER_DEVICE;
		break;
	}

	leapraid_set_identify_protocol_flags(dev_info, identify);

	return 0;
}

static void leapraid_transport_exp_set_edev(struct leapraid_adapter *adapter,
					    void *data_out,
					    struct sas_expander_device *edev)
{
	struct leapraid_smp_passthrough_rep *smp_passthrough_rep;
	struct leapraid_rep_manu_reply *rep_manu_reply;
	u8 *component_id;

	smp_passthrough_rep =
		(void *)&adapter->driver_cmds.transport_cmd.reply;
	if (le16_to_cpu(smp_passthrough_rep->resp_data_len) !=
	    sizeof(struct leapraid_rep_manu_reply))
		return;

	rep_manu_reply = data_out + sizeof(struct leapraid_rep_manu_request);

	memcpy(edev->vendor_id, rep_manu_reply->vendor_identification,
	       SAS_EXPANDER_VENDOR_ID_LEN);
	edev->vendor_id[SAS_EXPANDER_VENDOR_ID_LEN] = '\0';

	memcpy(edev->product_id, rep_manu_reply->product_identification,
	       SAS_EXPANDER_PRODUCT_ID_LEN);
	edev->product_id[SAS_EXPANDER_PRODUCT_ID_LEN] = '\0';

	memcpy(edev->product_rev, rep_manu_reply->product_revision_level,
	       SAS_EXPANDER_PRODUCT_REV_LEN);
	edev->product_rev[SAS_EXPANDER_PRODUCT_REV_LEN] = '\0';

	edev->level = rep_manu_reply->sas_format & 1;
	if (edev->level) {
		memcpy(edev->component_vendor_id,
		       rep_manu_reply->comp_vendor_identification,
		       SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN);
		edev->component_vendor_id[
			SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN] = '\0';

		component_id = (u8 *)&rep_manu_reply->component_id;
		edev->component_id = component_id[0] << 8 | component_id[1];
		edev->component_revision_id =
			rep_manu_reply->component_revision_level;
	}
}

static int leapraid_transport_exp_report_manu(struct leapraid_adapter *adapter,
					      u64 sas_address,
					      struct sas_expander_device *edev,
					      u8 port_id)
{
	struct leapraid_smp_passthrough_req *smp_passthrough_req;
	struct leapraid_rep_manu_request *rep_manu_request;
	dma_addr_t h2c_dma_addr;
	dma_addr_t c2h_dma_addr;
	bool issue_reset = false;
	void *data_out = NULL;
	u16 inter_taskid;
	size_t c2h_size;
	size_t h2c_size;
	void *psge;
	int rc;

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.pcie_recovering) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed, shost_recovering=%d pcie_recovering=%d\n",
			 __func__,
			 adapter->access_ctrl.shost_recovering,
			 adapter->access_ctrl.pcie_recovering);
		return -EFAULT;
	}

	mutex_lock(&adapter->driver_cmds.transport_cmd.mutex);
	adapter->driver_cmds.transport_cmd.status = LEAPRAID_CMD_PENDING;
	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_SHORT,
					  __func__);
	if (rc)
		goto out_cleanup;

	h2c_size = sizeof(struct leapraid_rep_manu_request);
	c2h_size = sizeof(struct leapraid_rep_manu_reply);
	data_out = dma_alloc_coherent(&adapter->pdev->dev,
				      h2c_size + c2h_size,
				      &h2c_dma_addr,
				      GFP_ATOMIC);
	if (!data_out) {
		rc = -ENOMEM;
		goto out_cleanup;
	}

	rep_manu_request = data_out;
	rep_manu_request->smp_frame_type =
		SMP_REPORT_MANUFACTURER_INFORMATION_FRAME_TYPE;
	rep_manu_request->function = SMP_REPORT_MANUFACTURER_INFORMATION_FUNC;
	rep_manu_request->allocated_response_length = 0;
	rep_manu_request->request_length = 0;

	inter_taskid = adapter->driver_cmds.transport_cmd.inter_taskid;
	smp_passthrough_req = leapraid_get_task_desc(adapter, inter_taskid);
	memset(smp_passthrough_req, 0,
	       sizeof(struct leapraid_smp_passthrough_req));
	smp_passthrough_req->func = LEAPRAID_FUNC_SMP_PASSTHROUGH;
	smp_passthrough_req->physical_port = port_id;
	smp_passthrough_req->sas_address = cpu_to_le64(sas_address);
	smp_passthrough_req->req_data_len = cpu_to_le16(h2c_size);
	psge = &smp_passthrough_req->sgl;
	c2h_dma_addr = h2c_dma_addr + sizeof(struct leapraid_rep_manu_request);
	leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr, h2c_size,
			       c2h_dma_addr, c2h_size);

	init_completion(&adapter->driver_cmds.transport_cmd.done);
	leapraid_fire_task(adapter, inter_taskid);
	wait_for_completion_timeout(&adapter->driver_cmds.transport_cmd.done,
				    LEAPRAID_TRANSPORT_CMD_TIMEOUT * HZ);
	if (!(adapter->driver_cmds.transport_cmd.status & LEAPRAID_CMD_DONE)) {
		rc = -ETIMEDOUT;
		dev_err(&adapter->pdev->dev,
			"%s: SMP passthrough timeout, st=0x%x\n",
			__func__, adapter->driver_cmds.transport_cmd.status);
		leapraid_log_req_context(adapter, inter_taskid,
					 smp_passthrough_req);
		if (!(adapter->driver_cmds.transport_cmd.status &
		      LEAPRAID_CMD_RESET))
			issue_reset = true;

		goto hard_reset;
	}

	if (adapter->driver_cmds.transport_cmd.status &
	    LEAPRAID_CMD_REPLY_VALID)
		leapraid_transport_exp_set_edev(adapter, data_out, edev);

hard_reset:
	if (issue_reset) {
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
			 __func__, __LINE__);
		rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
	}
out_cleanup:
	adapter->driver_cmds.transport_cmd.status = LEAPRAID_CMD_NOT_USED;
	if (data_out)
		dma_free_coherent(&adapter->pdev->dev, h2c_size + c2h_size,
				  data_out, h2c_dma_addr);

	mutex_unlock(&adapter->driver_cmds.transport_cmd.mutex);
	return rc;
}

static void leapraid_transport_del_port(struct leapraid_adapter *adapter,
					struct leapraid_sas_port *sas_port)
{
	dev_info(&sas_port->port->dev,
		 "Remove port: SAS addr=0x%016llx\n",
		 (unsigned long long)sas_port->remote_identify.sas_address);
	switch (sas_port->remote_identify.device_type) {
	case SAS_END_DEVICE:
		leapraid_sas_dev_remove_by_sas_address(
			adapter,
			sas_port->remote_identify.sas_address,
			sas_port->card_port);
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		leapraid_exp_rm(adapter, sas_port->remote_identify.sas_address,
				sas_port->card_port);
		break;
	default:
		break;
	}
}

static void leapraid_transport_del_phy(struct leapraid_adapter *adapter,
				       struct leapraid_sas_port *sas_port,
				       struct leapraid_card_phy *card_phy)
{
	dev_info(&card_phy->phy->dev,
		 "Remove PHY: SAS addr=0x%016llx, phy=%d\n",
		 (unsigned long long)sas_port->remote_identify.sas_address,
		 card_phy->phy_id);
	list_del(&card_phy->port_siblings);
	sas_port->phys_num--;
	sas_port_delete_phy(sas_port->port, card_phy->phy);
	card_phy->phy_is_assigned = 0;
}

static void leapraid_transport_add_phy(struct leapraid_adapter *adapter,
				       struct leapraid_sas_port *sas_port,
				       struct leapraid_card_phy *card_phy)
{
	dev_info(&card_phy->phy->dev,
		 "Add PHY: SAS addr=0x%016llx, phy=%d\n",
		 (unsigned long long)sas_port->remote_identify.sas_address,
		 card_phy->phy_id);
	list_add_tail(&card_phy->port_siblings, &sas_port->phy_list);
	sas_port->phys_num++;
	sas_port_add_phy(sas_port->port, card_phy->phy);
	card_phy->phy_is_assigned = 1;
}

void leapraid_transport_attach_phy_to_port(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node,
		struct leapraid_card_phy *card_phy,
		u64 sas_address,
		struct leapraid_card_port *card_port)
{
	struct leapraid_sas_port *sas_port;
	struct leapraid_card_phy *card_phy_srch;

	if (card_phy->phy_is_assigned)
		return;

	if (!card_port)
		return;

	list_for_each_entry(sas_port, &topo_node->sas_port_list, port_list) {
		if (sas_port->remote_identify.sas_address != sas_address)
			continue;

		if (sas_port->card_port != card_port)
			continue;

		list_for_each_entry(card_phy_srch, &sas_port->phy_list,
				    port_siblings) {
			if (card_phy_srch == card_phy)
				return;
		}
		leapraid_transport_add_phy(adapter, sas_port, card_phy);
		return;
	}
}

void leapraid_transport_detach_phy_to_port(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node,
		struct leapraid_card_phy *target_card_phy)
{
	struct leapraid_sas_port *sas_port, *sas_port_next;
	struct leapraid_card_phy *cur_card_phy;

	if (!target_card_phy->phy_is_assigned)
		return;

	list_for_each_entry_safe(sas_port, sas_port_next,
				 &topo_node->sas_port_list, port_list) {
		list_for_each_entry(cur_card_phy, &sas_port->phy_list,
				    port_siblings) {
			if (cur_card_phy != target_card_phy)
				continue;

			if (sas_port->phys_num == 1 &&
			    !adapter->access_ctrl.shost_recovering)
				leapraid_transport_del_port(adapter, sas_port);
			else
				leapraid_transport_del_phy(adapter, sas_port,
							   target_card_phy);
			return;
		}
	}
}

static void leapraid_detach_phy_from_old_port(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node,
		u64 sas_address,
		struct leapraid_card_port *card_port)
{
	int i;

	for (i = 0; i < topo_node->phys_num; i++) {
		if (topo_node->card_phy[i].remote_identify.sas_address !=
			sas_address ||
		    topo_node->card_phy[i].card_port != card_port)
			continue;
		if (topo_node->card_phy[i].phy_is_assigned)
			leapraid_transport_detach_phy_to_port(
				adapter,
				topo_node,
				&topo_node->card_phy[i]);
	}
}

static struct leapraid_sas_port *leapraid_prepare_sas_port(
		struct leapraid_adapter *adapter,
		u16 handle, u64 sas_address,
		struct leapraid_card_port *card_port,
		struct leapraid_topo_node **out_topo_node)
{
	struct leapraid_topo_node *topo_node;
	struct leapraid_sas_port *sas_port;
	unsigned long flags;

	sas_port = kzalloc_obj(*sas_port);
	if (!sas_port) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed alloc sas_port\n", __func__);
		return NULL;
	}

	INIT_LIST_HEAD(&sas_port->port_list);
	INIT_LIST_HEAD(&sas_port->phy_list);

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	topo_node = leapraid_transport_topo_node_by_sas_addr(adapter,
							     sas_address,
							     card_port);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	if (!topo_node) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to locate parent for SAS 0x%016llx!\n",
			__func__, sas_address);
		goto out_cleanup;
	}

	if (leapraid_transport_set_identify(adapter, handle,
					    &sas_port->remote_identify))
		goto out_cleanup;

	if (sas_port->remote_identify.device_type == SAS_PHY_UNUSED) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed, device type is SAS_PHY_UNUSED\n",
			 __func__);
		goto out_cleanup;
	}

	sas_port->card_port = card_port;
	*out_topo_node = topo_node;

	return sas_port;

out_cleanup:
	kfree(sas_port);
	return NULL;
}

static int leapraid_bind_phys_and_vphy(struct leapraid_adapter *adapter,
				       struct leapraid_sas_port *sas_port,
				       struct leapraid_topo_node *topo_node,
				       struct leapraid_card_port *card_port,
				       struct leapraid_vphy **out_vphy)
{
	struct leapraid_vphy *vphy = NULL;
	int i;

	for (i = 0; i < topo_node->phys_num; i++) {
		if (topo_node->card_phy[i].remote_identify.sas_address !=
			sas_port->remote_identify.sas_address ||
		    topo_node->card_phy[i].card_port != card_port)
			continue;

		list_add_tail(&topo_node->card_phy[i].port_siblings,
			      &sas_port->phy_list);
		sas_port->phys_num++;

		if (topo_node->hdl <= adapter->dev_topo.card.phys_num) {
			if (!topo_node->card_phy[i].vphy) {
				card_port->phy_mask |= BIT(i);
				continue;
			}

			vphy = leapraid_get_vphy_by_phy(card_port, i);
			if (!vphy)
				return LEAPRAID_OPERATION_FAILED;
		}
	}

	*out_vphy = vphy;
	return sas_port->phys_num ? 0 : LEAPRAID_OPERATION_FAILED;
}

static struct sas_rphy *leapraid_create_and_register_rphy(
		struct leapraid_adapter *adapter,
		struct leapraid_sas_port *sas_port,
		struct leapraid_topo_node *topo_node,
		struct leapraid_card_port *card_port,
		struct leapraid_vphy *vphy)
{
	struct leapraid_sas_dev *sas_dev = NULL;
	struct leapraid_card_phy *card_phy;
	struct sas_port *port;
	struct sas_rphy *rphy;

	if (sas_port->remote_identify.device_type == SAS_END_DEVICE) {
		sas_dev = leapraid_get_sas_dev_by_addr(
				adapter,
				sas_port->remote_identify.sas_address,
				card_port);
		if (!sas_dev)
			return NULL;
		sas_dev->pend_sas_rphy_add = 1;
	}

	if (!topo_node->parent_dev) {
		dev_warn(&adapter->pdev->dev,
			 "%s: topo_node parent device is NULL\n",  __func__);
		goto cleanup_sas_dev;
	}

	port = sas_port_alloc_num(topo_node->parent_dev);
	if (!port) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to allocate SAS port\n", __func__);
		goto cleanup_sas_dev;
	}

	if (sas_port_add(port)) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to add SAS port\n", __func__);
		goto out_delete_port;
	}

	list_for_each_entry(card_phy, &sas_port->phy_list, port_siblings) {
		sas_port_add_phy(port, card_phy->phy);
		card_phy->phy_is_assigned = 1;
		card_phy->card_port = card_port;
	}

	if (sas_dev) {
		rphy = sas_end_device_alloc(port);
		sas_dev->rphy = rphy;

		if (topo_node->hdl <= adapter->dev_topo.card.phys_num) {
			if (!vphy)
				card_port->sas_address = sas_dev->sas_addr;
			else
				vphy->sas_address = sas_dev->sas_addr;
		}
	} else {
		rphy = sas_expander_alloc(
				port,
				sas_port->remote_identify.device_type);
		if (topo_node->hdl <= adapter->dev_topo.card.phys_num)
			card_port->sas_address =
				sas_port->remote_identify.sas_address;
	}

	if (!rphy) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to allocate RPHY\n", __func__);
		goto cleanup_card_phy;
	}

	rphy->identify = sas_port->remote_identify;

	if (sas_rphy_add(rphy)) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to add RPHY\n", __func__);
		if (sas_dev)
			sas_dev->rphy = NULL;

		sas_rphy_free(rphy);
		goto cleanup_card_phy;
	}
	sas_port->port = port;

	if (sas_dev) {
		sas_dev->pend_sas_rphy_add = 0;
		leapraid_sdev_put(sas_dev);
	}

	return rphy;

cleanup_card_phy:
	if (topo_node->hdl <= adapter->dev_topo.card.phys_num) {
		if (!vphy)
			card_port->sas_address = 0;
		else
			vphy->sas_address = 0;
	}

	list_for_each_entry(card_phy, &sas_port->phy_list, port_siblings) {
		card_phy->phy_is_assigned = 0;
		card_phy->card_port = NULL;
	}

out_delete_port:
	sas_port_delete(port);

cleanup_sas_dev:
	if (sas_dev) {
		sas_dev->pend_sas_rphy_add = 0;
		leapraid_sdev_put(sas_dev);
	}
	return NULL;
}

struct leapraid_sas_port *leapraid_transport_port_add(
		struct leapraid_adapter *adapter,
		u16 hdl, u64 sas_address,
		struct leapraid_card_port *card_port)
{
	struct leapraid_card_phy *card_phy, *card_phy_next;
	struct leapraid_topo_node *topo_node = NULL;
	struct leapraid_sas_port *sas_port;
	struct leapraid_vphy *vphy = NULL;
	struct sas_rphy *rphy;
	unsigned long flags;

	if (!card_port) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid card_port\n", __func__);
		return NULL;
	}

	sas_port = leapraid_prepare_sas_port(adapter, hdl, sas_address,
					     card_port, &topo_node);
	if (!sas_port)
		return NULL;

	leapraid_detach_phy_from_old_port(
		adapter,
		topo_node,
		sas_port->remote_identify.sas_address,
		card_port);

	if (leapraid_bind_phys_and_vphy(adapter, sas_port, topo_node,
					card_port, &vphy)) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to bind phy to vphy\n", __func__);
		goto out_fail;
	}

	rphy = leapraid_create_and_register_rphy(adapter, sas_port, topo_node,
						 card_port, vphy);
	if (!rphy) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to create rphy\n", __func__);
		goto out_fail;
	}

	dev_info(&rphy->dev,
		 "%s: Added dev: hdl=0x%04x, SAS addr=0x%016llx\n",
		 __func__, hdl,
		 (unsigned long long)sas_port->remote_identify.sas_address);

	sas_port->rphy = rphy;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_add_tail(&sas_port->port_list, &topo_node->sas_port_list);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	if (sas_port->remote_identify.device_type ==
	    SAS_EDGE_EXPANDER_DEVICE ||
	    sas_port->remote_identify.device_type ==
	    SAS_FANOUT_EXPANDER_DEVICE)
		leapraid_transport_exp_report_manu(
			adapter,
			sas_port->remote_identify.sas_address,
			rphy_to_expander_device(rphy),
			card_port->port_id);

	return sas_port;

out_fail:
	list_for_each_entry_safe(card_phy, card_phy_next,
				 &sas_port->phy_list, port_siblings) {
		if (topo_node->hdl <= adapter->dev_topo.card.phys_num &&
		    !card_phy->vphy)
			card_port->phy_mask &= ~BIT(card_phy->phy_id);

		list_del_init(&card_phy->port_siblings);
	}

	kfree(sas_port);
	return NULL;
}

static struct leapraid_sas_port *leapraid_find_and_remove_sas_port(
		struct leapraid_topo_node *topo_node,
		u64 sas_address,
		struct leapraid_card_port *remove_card_port,
		bool *found)
{
	struct leapraid_sas_port *sas_port, *sas_port_next;

	list_for_each_entry_safe(sas_port, sas_port_next,
				 &topo_node->sas_port_list, port_list) {
		if (sas_port->remote_identify.sas_address != sas_address)
			continue;

		if (sas_port->card_port != remove_card_port)
			continue;

		*found = true;
		list_del(&sas_port->port_list);
		return sas_port;
	}
	return NULL;
}

static void leapraid_cleanup_card_port_and_vphys(
		struct leapraid_adapter *adapter,
		u64 sas_address,
		struct leapraid_card_port *remove_card_port)
{
	struct leapraid_card_port *card_port, *card_port_next;
	struct leapraid_vphy *vphy, *vphy_next;

	if (remove_card_port->vphys_mask) {
		list_for_each_entry_safe(vphy, vphy_next,
					 &remove_card_port->vphys_list, list) {
			if (vphy->sas_address != sas_address)
				continue;

			dev_dbg(&adapter->pdev->dev,
				"%s: Remove vphy=%p from port=%p port_id=%d\n",
				__func__, vphy, remove_card_port,
				remove_card_port->port_id);

			remove_card_port->vphys_mask &= ~vphy->phy_mask;
			list_del(&vphy->list);
			kfree(vphy);
		}

		if (!remove_card_port->vphys_mask &&
		    !remove_card_port->sas_address) {
			dev_dbg(&adapter->pdev->dev,
				"%s: Remove empty hba_port: %p, port_id=%d\n",
				__func__,
				remove_card_port,
				remove_card_port->port_id);
			list_del(&remove_card_port->list);
			kfree(remove_card_port);
			remove_card_port = NULL;
		}
	}

	list_for_each_entry_safe(card_port, card_port_next,
				 &adapter->dev_topo.card_port_list, list) {
		if (card_port != remove_card_port)
			continue;

		if (card_port->sas_address != sas_address)
			continue;

		if (!remove_card_port->vphys_mask) {
			dev_dbg(&adapter->pdev->dev,
				"%s: Remove hba_port: %p, port_id=%d\n",
				__func__, card_port, card_port->port_id);
			list_del(&card_port->list);
			kfree(card_port);
		} else {
			dev_dbg(&adapter->pdev->dev,
				"Clear sas_address of port=%p, port_id=%d\n",
				card_port, card_port->port_id);
			remove_card_port->sas_address = 0;
		}
		break;
	}
}

static void leapraid_clear_topo_node_phys(struct leapraid_topo_node *topo_node,
					  u64 sas_address)
{
	int i;

	for (i = 0; i < topo_node->phys_num; i++)
		if (topo_node->card_phy[i].remote_identify.sas_address ==
		    sas_address) {
			memset(&topo_node->card_phy[i].remote_identify, 0,
			       sizeof(struct sas_identify));
			topo_node->card_phy[i].vphy = 0;
		}
}

void leapraid_transport_port_remove(
		struct leapraid_adapter *adapter,
		u64 sas_address,
		u64 sas_address_parent,
		struct leapraid_card_port *remove_card_port)
{
	struct leapraid_card_phy *card_phy, *card_phy_next;
	struct leapraid_sas_port *sas_port;
	struct leapraid_topo_node *topo_node;
	unsigned long flags;
	bool found = false;

	if (!remove_card_port)
		return;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);

	topo_node = leapraid_transport_topo_node_by_sas_addr(adapter,
							     sas_address_parent,
							     remove_card_port);
	if (!topo_node) {
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);
		return;
	}

	sas_port = leapraid_find_and_remove_sas_port(topo_node, sas_address,
						     remove_card_port, &found);

	if (!found) {
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);
		return;
	}

	if (topo_node->hdl <= adapter->dev_topo.card.phys_num &&
	    adapter->adapter_attr.enable_mp)
		leapraid_cleanup_card_port_and_vphys(adapter, sas_address,
						     remove_card_port);

	leapraid_clear_topo_node_phys(topo_node, sas_address);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	list_for_each_entry_safe(card_phy, card_phy_next,
				 &sas_port->phy_list, port_siblings) {
		card_phy->phy_is_assigned = 0;
		if (!adapter->access_ctrl.host_removing)
			sas_port_delete_phy(sas_port->port, card_phy->phy);

		list_del(&card_phy->port_siblings);
	}

	if (!adapter->access_ctrl.host_removing)
		sas_port_delete(sas_port->port);

	dev_dbg(&adapter->pdev->dev,
		"%s: Removed sas_port for SAS addr=0x%016llx\n",
		__func__, (unsigned long long)sas_address);

	kfree(sas_port);
}

static void leapraid_init_sas_or_exp_phy(struct leapraid_adapter *adapter,
					 struct leapraid_card_phy *card_phy,
					 struct sas_phy *phy,
					 struct leapraid_sas_phy_p0 *phy_pg0,
					 struct leapraid_exp_p1 *exp_pg1)
{
	if (exp_pg1 && phy_pg0)
		return;

	if (!exp_pg1 && !phy_pg0)
		return;

	phy->identify = card_phy->identify;
	phy->identify.phy_identifier = card_phy->phy_id;
	phy->negotiated_linkrate = phy_pg0 ?
		leapraid_transport_convert_phy_link_rate(
			phy_pg0->neg_link_rate &
			LEAPRAID_SAS_NEG_LINK_RATE_MASK_PHYSICAL) :
		 leapraid_transport_convert_phy_link_rate(
			exp_pg1->neg_link_rate &
			LEAPRAID_SAS_NEG_LINK_RATE_MASK_PHYSICAL);
	phy->minimum_linkrate_hw = phy_pg0 ?
		 leapraid_transport_convert_phy_link_rate(
			phy_pg0->hw_link_rate &
			LEAPRAID_SAS_HWRATE_MIN_RATE_MASK) :
		 leapraid_transport_convert_phy_link_rate(
			exp_pg1->hw_link_rate &
			LEAPRAID_SAS_HWRATE_MIN_RATE_MASK);
	phy->maximum_linkrate_hw = phy_pg0 ?
		 leapraid_transport_convert_phy_link_rate(
			phy_pg0->hw_link_rate >>
			 LEAPRAID_SAS_NEG_LINK_RATE_SHIFT) :
		 leapraid_transport_convert_phy_link_rate(
			exp_pg1->hw_link_rate >>
			 LEAPRAID_SAS_NEG_LINK_RATE_SHIFT);
	phy->minimum_linkrate = phy_pg0 ?
		 leapraid_transport_convert_phy_link_rate(
			phy_pg0->p_link_rate &
			LEAPRAID_SAS_PRATE_MIN_RATE_MASK) :
		 leapraid_transport_convert_phy_link_rate(
			exp_pg1->p_link_rate &
			LEAPRAID_SAS_PRATE_MIN_RATE_MASK);
	phy->maximum_linkrate = phy_pg0 ?
		 leapraid_transport_convert_phy_link_rate(
			phy_pg0->p_link_rate >>
			 LEAPRAID_SAS_NEG_LINK_RATE_SHIFT) :
		 leapraid_transport_convert_phy_link_rate(
			exp_pg1->p_link_rate >>
			 LEAPRAID_SAS_NEG_LINK_RATE_SHIFT);
	phy->hostdata = card_phy->card_port;
}

void leapraid_transport_add_card_phy(struct leapraid_adapter *adapter,
				     struct leapraid_card_phy *card_phy,
				     struct leapraid_sas_phy_p0 *phy_pg0,
				     struct device *parent_dev)
{
	struct sas_phy *phy;
	int ret;

	INIT_LIST_HEAD(&card_phy->port_siblings);
	phy = sas_phy_alloc(parent_dev, card_phy->phy_id);
	if (!phy) {
		dev_err(&adapter->pdev->dev,
			"%s: sas_phy_alloc failed!\n", __func__);
		return;
	}

	if (leapraid_transport_set_identify(adapter, card_phy->hdl,
					    &card_phy->identify)) {
		dev_err(&adapter->pdev->dev,
			"%s: Set PHY handle identify failed!\n", __func__);
		sas_phy_free(phy);
		return;
	}

	card_phy->attached_hdl = le16_to_cpu(phy_pg0->attached_dev_hdl);
	if (card_phy->attached_hdl) {
		ret = leapraid_transport_set_identify(
				adapter,
				card_phy->attached_hdl,
				&card_phy->remote_identify);
		if (ret) {
			dev_err(&adapter->pdev->dev,
				"%s: Set PHY attached hdl identify failed!\n",
				__func__);
			sas_phy_free(phy);
			return;
		}
	}

	leapraid_init_sas_or_exp_phy(adapter, card_phy, phy, phy_pg0, NULL);

	if (sas_phy_add(phy)) {
		dev_err(&adapter->pdev->dev,
			"%s: SAS PHY add failed!\n", __func__);
		sas_phy_free(phy);
		return;
	}

	card_phy->phy = phy;
}

int leapraid_transport_add_exp_phy(struct leapraid_adapter *adapter,
				   struct leapraid_card_phy *card_phy,
				   struct leapraid_exp_p1 *exp_pg1,
				   struct device *parent_dev)
{
	struct sas_phy *phy;
	int ret;

	INIT_LIST_HEAD(&card_phy->port_siblings);
	phy = sas_phy_alloc(parent_dev, card_phy->phy_id);
	if (!phy) {
		dev_err(&adapter->pdev->dev,
			"%s: sas_phy_alloc failed!\n", __func__);
		return -EFAULT;
	}

	if (leapraid_transport_set_identify(adapter, card_phy->hdl,
					    &card_phy->identify)) {
		dev_err(&adapter->pdev->dev,
			"%s: Set PHY hdl identify failed!\n", __func__);
		sas_phy_free(phy);
		return -EFAULT;
	}

	card_phy->attached_hdl = le16_to_cpu(exp_pg1->attached_dev_hdl);
	if (card_phy->attached_hdl) {
		ret = leapraid_transport_set_identify(
				adapter,
				card_phy->attached_hdl,
				&card_phy->remote_identify);
		if (ret)
			dev_warn(&adapter->pdev->dev,
				 "%s: Set PHY attached hdl identify failed!\n",
				 __func__);
	}

	leapraid_init_sas_or_exp_phy(adapter, card_phy, phy, NULL, exp_pg1);

	if (sas_phy_add(phy)) {
		dev_err(&adapter->pdev->dev,
			"%s: SAS PHY add failed!\n", __func__);
		sas_phy_free(phy);
		return -EFAULT;
	}

	card_phy->phy = phy;
	return 0;
}

void leapraid_transport_update_links(
		struct leapraid_adapter *adapter,
		u64 sas_address, u16 hdl, u8 phy_index,
		u8 link_rate, struct leapraid_card_port *target_card_port)
{
	struct leapraid_topo_node *topo_node;
	struct leapraid_card_phy *card_phy;
	struct leapraid_card_port *card_port;
	unsigned long flags;

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.pcie_recovering)
		return;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	topo_node = leapraid_transport_topo_node_by_sas_addr(adapter,
							     sas_address,
							     target_card_port);
	if (!topo_node) {
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);
		return;
	}

	card_phy = &topo_node->card_phy[phy_index];
	card_phy->attached_hdl = hdl;
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	if (hdl && link_rate >= LEAPRAID_SAS_NEG_LINK_RATE_1_5) {
		leapraid_transport_set_identify(adapter, hdl,
						&card_phy->remote_identify);
		if (topo_node->hdl <= adapter->dev_topo.card.phys_num &&
		    adapter->adapter_attr.enable_mp) {
			list_for_each_entry(card_port,
					    &adapter->dev_topo.card_port_list,
					    list) {
				if (card_port->sas_address == sas_address &&
				    card_port == target_card_port)
					card_port->phy_mask |=
						BIT(card_phy->phy_id);
			}
		}
		leapraid_transport_attach_phy_to_port(
			adapter,
			topo_node,
			card_phy,
			card_phy->remote_identify.sas_address,
			target_card_port);
	} else {
		memset(&card_phy->remote_identify, 0,
		       sizeof(struct sas_identify));
	}

	if (card_phy->phy)
		card_phy->phy->negotiated_linkrate =
			leapraid_transport_convert_phy_link_rate(link_rate);
}

static int leapraid_dma_map_buffer(struct device *dev, struct bsg_buffer *buf,
				   dma_addr_t *dma_addr,
				   size_t *dma_len, void **p)
{
	if (buf->sg_cnt > 1) {
		*p = dma_alloc_coherent(dev, buf->payload_len, dma_addr,
					GFP_KERNEL);
		if (!*p)
			return -ENOMEM;

		*dma_len = buf->payload_len;
	} else {
		if (!dma_map_sg(dev, buf->sg_list, 1, DMA_BIDIRECTIONAL)) {
			dev_err(dev,
				"%s: Failed to map sg buffer for dma\n",
				__func__);
			return -ENOMEM;
		}

		*dma_addr = sg_dma_address(buf->sg_list);
		*dma_len = sg_dma_len(buf->sg_list);
		*p = NULL;
	}
	return 0;
}

static void leapraid_dma_unmap_buffer(struct device *dev,
				      struct bsg_buffer *buf,
				      dma_addr_t dma_addr,
				      void *p)
{
	if (p)
		dma_free_coherent(dev, buf->payload_len, p, dma_addr);
	else
		dma_unmap_sg(dev, buf->sg_list, 1, DMA_BIDIRECTIONAL);
}

static void leapraid_build_smp_task(struct leapraid_adapter *adapter,
				    struct sas_rphy *rphy,
				    dma_addr_t h2c_dma_addr, size_t h2c_size,
				    dma_addr_t c2h_dma_addr, size_t c2h_size)
{
	struct leapraid_smp_passthrough_req *smp_passthrough_req;
	u16 inter_taskid;
	void *psge;

	inter_taskid = adapter->driver_cmds.transport_cmd.inter_taskid;
	smp_passthrough_req = leapraid_get_task_desc(adapter, inter_taskid);
	memset(smp_passthrough_req, 0, sizeof(*smp_passthrough_req));

	smp_passthrough_req->func = LEAPRAID_FUNC_SMP_PASSTHROUGH;
	smp_passthrough_req->physical_port =
		leapraid_transport_get_port_id_by_rphy(adapter, rphy);
	smp_passthrough_req->sas_address = (rphy) ?
		cpu_to_le64(rphy->identify.sas_address) :
		cpu_to_le64(adapter->dev_topo.card.sas_address);
	smp_passthrough_req->req_data_len =
		cpu_to_le16(h2c_size - LEAPRAID_SMP_FRAME_HEADER_SIZE);
	psge = &smp_passthrough_req->sgl;
	leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr,
			       h2c_size - LEAPRAID_SMP_FRAME_HEADER_SIZE,
			       c2h_dma_addr,
			       c2h_size - LEAPRAID_SMP_FRAME_HEADER_SIZE);
}

static int leapraid_send_smp_req(struct leapraid_adapter *adapter)
{
	const struct leapraid_smp_passthrough_req *smp_passthrough_req;
	u16 inter_taskid;

	dev_dbg(&adapter->pdev->dev,
		"%s: Sending smp request\n", __func__);
	inter_taskid = adapter->driver_cmds.transport_cmd.inter_taskid;
	smp_passthrough_req = leapraid_get_task_desc(adapter, inter_taskid);
	init_completion(&adapter->driver_cmds.transport_cmd.done);
	leapraid_fire_task(adapter, inter_taskid);
	wait_for_completion_timeout(&adapter->driver_cmds.transport_cmd.done,
				    LEAPRAID_TRANSPORT_CMD_TIMEOUT * HZ);
	if (!(adapter->driver_cmds.transport_cmd.status & LEAPRAID_CMD_DONE)) {
		dev_err(&adapter->pdev->dev, "%s: timeout, st=0x%x\n",
			__func__, adapter->driver_cmds.transport_cmd.status);
		leapraid_log_req_context(adapter, inter_taskid,
					 smp_passthrough_req);
		if (!(adapter->driver_cmds.transport_cmd.status &
		      LEAPRAID_CMD_RESET)) {
			dev_dbg(&adapter->pdev->dev,
				"%s:%d: call hard_reset\n",
				__func__, __LINE__);
			leapraid_hard_reset_handler(adapter, FULL_RESET);
			return -ETIMEDOUT;
		}
	}

	dev_dbg(&adapter->pdev->dev, "%s: SMP request complete\n", __func__);
	if (!(adapter->driver_cmds.transport_cmd.status &
	      LEAPRAID_CMD_REPLY_VALID)) {
		dev_err(&adapter->pdev->dev,
			"%s: SMP request no reply\n", __func__);
		return -ENXIO;
	}

	return 0;
}

static void leapraid_handle_smp_rep(struct leapraid_adapter *adapter,
				    struct bsg_job *job, void *addr_in,
				    unsigned int *reslen)
{
	struct leapraid_smp_passthrough_rep *smp_passthrough_rep;

	smp_passthrough_rep =
		(void *)&adapter->driver_cmds.transport_cmd.reply;

	dev_dbg(&adapter->pdev->dev, "%s: Response data len=%d\n",
		__func__, le16_to_cpu(smp_passthrough_rep->resp_data_len));

	memcpy(job->reply, smp_passthrough_rep, sizeof(*smp_passthrough_rep));
	job->reply_len = sizeof(*smp_passthrough_rep);
	*reslen = le16_to_cpu(smp_passthrough_rep->resp_data_len);

	if (addr_in)
		sg_copy_from_buffer(job->reply_payload.sg_list,
				    job->reply_payload.sg_cnt, addr_in,
				    job->reply_payload.payload_len);
}

static void leapraid_transport_smp_handler(struct bsg_job *job,
					   struct Scsi_Host *shost,
					   struct sas_rphy *rphy)
{
	struct leapraid_adapter *adapter = shost_priv(shost);
	dma_addr_t c2h_dma_addr;
	dma_addr_t h2c_dma_addr;
	void *addr_in = NULL;
	void *addr_out = NULL;
	size_t c2h_size;
	size_t h2c_size;
	int rc;
	unsigned int reslen = 0;

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.pcie_recovering) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed, shost_recovering=%d pcie_recovering=%d\n",
			__func__,
			adapter->access_ctrl.shost_recovering,
			adapter->access_ctrl.pcie_recovering);
		rc = -EFAULT;
		goto exit_bsg_job;
	}

	rc = mutex_lock_interruptible(&adapter->driver_cmds
				      .transport_cmd.mutex);
	if (rc)
		goto exit_bsg_job;

	adapter->driver_cmds.transport_cmd.status = LEAPRAID_CMD_PENDING;
	rc = leapraid_dma_map_buffer(&adapter->pdev->dev,
				     &job->request_payload,
				     &h2c_dma_addr, &h2c_size, &addr_out);
	if (rc)
		goto release_lock;

	if (h2c_size < LEAPRAID_SMP_FRAME_HEADER_SIZE) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid SMP request payload size=%zu\n",
			__func__, h2c_size);
		rc = -EINVAL;
		goto free_req_buf;
	}

	if (addr_out)
		sg_copy_to_buffer(job->request_payload.sg_list,
				  job->request_payload.sg_cnt, addr_out,
				  job->request_payload.payload_len);

	rc = leapraid_dma_map_buffer(&adapter->pdev->dev, &job->reply_payload,
				     &c2h_dma_addr, &c2h_size, &addr_in);
	if (rc)
		goto free_req_buf;

	if (c2h_size < LEAPRAID_SMP_FRAME_HEADER_SIZE) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid SMP reply payload size=%zu\n",
			__func__, c2h_size);
		rc = -EINVAL;
		goto free_rep_buf;
	}

	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_SHORT,
					  __func__);
	if (rc)
		goto free_rep_buf;

	leapraid_build_smp_task(adapter, rphy, h2c_dma_addr,
				h2c_size, c2h_dma_addr, c2h_size);

	rc = leapraid_send_smp_req(adapter);
	if (rc)
		goto free_rep_buf;

	leapraid_handle_smp_rep(adapter, job, addr_in, &reslen);

free_rep_buf:
	leapraid_dma_unmap_buffer(&adapter->pdev->dev, &job->reply_payload,
				  c2h_dma_addr, addr_in);
free_req_buf:
	leapraid_dma_unmap_buffer(&adapter->pdev->dev, &job->request_payload,
				  h2c_dma_addr, addr_out);
release_lock:
	adapter->driver_cmds.transport_cmd.status = LEAPRAID_CMD_NOT_USED;
	mutex_unlock(&adapter->driver_cmds.transport_cmd.mutex);
exit_bsg_job:
	bsg_job_done(job, rc, reslen);
}

struct sas_function_template leapraid_transport_functions = {
	.smp_handler = leapraid_transport_smp_handler,
};

struct scsi_transport_template *leapraid_transport_template;
