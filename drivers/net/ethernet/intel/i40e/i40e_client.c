// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/list.h>
#include <linux/errno.h>

#include "i40e.h"
#include "i40e_prototype.h"
#include "i40e_client.h"

static const char i40e_client_interface_version_str[] = I40E_CLIENT_VERSION_STR;
static struct i40e_client *registered_client;
static LIST_HEAD(i40e_devices);
static DEFINE_MUTEX(i40e_device_mutex);

static int i40e_client_virtchnl_send(struct i40e_info *ldev,
				     struct i40e_client *client,
				     u32 vf_id, u8 *msg, u16 len);

static int i40e_client_setup_qvlist(struct i40e_info *ldev,
				    struct i40e_client *client,
				    struct i40e_qvlist_info *qvlist_info);

static void i40e_client_request_reset(struct i40e_info *ldev,
				      struct i40e_client *client,
				      u32 reset_level);

static int i40e_client_update_vsi_ctxt(struct i40e_info *ldev,
				       struct i40e_client *client,
				       bool is_vf, u32 vf_id,
				       u32 flag, u32 valid_flag);

static struct i40e_ops i40e_lan_ops = {
	.virtchnl_send = i40e_client_virtchnl_send,
	.setup_qvlist = i40e_client_setup_qvlist,
	.request_reset = i40e_client_request_reset,
	.update_vsi_ctxt = i40e_client_update_vsi_ctxt,
};

/**
 * i40e_client_get_params - Get the params that can change at runtime
 * @vsi: the VSI with the message
 * @params: client param struct
 *
 **/
static
int i40e_client_get_params(struct i40e_vsi *vsi, struct i40e_params *params)
{
	struct i40e_dcbx_config *dcb_cfg = &vsi->back->hw.local_dcbx_config;
	int i = 0;

	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		u8 tc = dcb_cfg->etscfg.prioritytable[i];
		u16 qs_handle;

		/* If TC is not enabled for VSI use TC0 for UP */
		if (!(vsi->tc_config.enabled_tc & BIT(tc)))
			tc = 0;

		qs_handle = le16_to_cpu(vsi->info.qs_handle[tc]);
		params->qos.prio_qos[i].tc = tc;
		params->qos.prio_qos[i].qs_handle = qs_handle;
		if (qs_handle == I40E_AQ_VSI_QS_HANDLE_INVALID) {
			dev_err(&vsi->back->pdev->dev, "Invalid queue set handle for TC = %d, vsi id = %d\n",
				tc, vsi->id);
			return -EINVAL;
		}
	}

	params->mtu = vsi->netdev->mtu;
	return 0;
}

/**
 * i40e_notify_client_of_vf_msg - call the client vf message callback
 * @vsi: the VSI with the message
 * @vf_id: the absolute VF id that sent the message
 * @msg: message buffer
 * @len: length of the message
 *
 * If there is a client to this VSI, call the client
 **/
void
i40e_notify_client_of_vf_msg(struct i40e_vsi *vsi, u32 vf_id, u8 *msg, u16 len)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_client_instance *cdev = pf->cinst;

	if (!cdev || !cdev->client)
		return;
	if (!cdev->client->ops || !cdev->client->ops->virtchnl_receive) {
		dev_dbg(&pf->pdev->dev,
			"Cannot locate client instance virtual channel receive routine\n");
		return;
	}
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state)) {
		dev_dbg(&pf->pdev->dev, "Client is not open, abort virtchnl_receive\n");
		return;
	}
	cdev->client->ops->virtchnl_receive(&cdev->lan_info, cdev->client,
					    vf_id, msg, len);
}

/**
 * i40e_notify_client_of_l2_param_changes - call the client notify callback
 * @vsi: the VSI with l2 param changes
 *
 * If there is a client to this VSI, call the client
 **/
void i40e_notify_client_of_l2_param_changes(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_client_instance *cdev = pf->cinst;
	struct i40e_params params;

	if (!cdev || !cdev->client)
		return;
	if (!cdev->client->ops || !cdev->client->ops->l2_param_change) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance l2_param_change routine\n");
		return;
	}
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state)) {
		dev_dbg(&vsi->back->pdev->dev, "Client is not open, abort l2 param change\n");
		return;
	}
	memset(&params, 0, sizeof(params));
	i40e_client_get_params(vsi, &params);
	memcpy(&cdev->lan_info.params, &params, sizeof(struct i40e_params));
	cdev->client->ops->l2_param_change(&cdev->lan_info, cdev->client,
					   &params);
}

/**
 * i40e_client_release_qvlist - release MSI-X vector mapping for client
 * @ldev: pointer to L2 context.
 *
 **/
static void i40e_client_release_qvlist(struct i40e_info *ldev)
{
	struct i40e_qvlist_info *qvlist_info = ldev->qvlist_info;
	u32 i;

	if (!ldev->qvlist_info)
		return;

	for (i = 0; i < qvlist_info->num_vectors; i++) {
		struct i40e_pf *pf = ldev->pf;
		struct i40e_qv_info *qv_info;
		u32 reg_idx;

		qv_info = &qvlist_info->qv_info[i];
		if (!qv_info)
			continue;
		reg_idx = I40E_PFINT_LNKLSTN(qv_info->v_idx - 1);
		wr32(&pf->hw, reg_idx, I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK);
	}
	kfree(ldev->qvlist_info);
	ldev->qvlist_info = NULL;
}

/**
 * i40e_notify_client_of_netdev_close - call the client close callback
 * @vsi: the VSI with netdev closed
 * @reset: true when close called due to a reset pending
 *
 * If there is a client to this netdev, call the client with close
 **/
void i40e_notify_client_of_netdev_close(struct i40e_vsi *vsi, bool reset)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_client_instance *cdev = pf->cinst;

	if (!cdev || !cdev->client)
		return;
	if (!cdev->client->ops || !cdev->client->ops->close) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance close routine\n");
		return;
	}
	cdev->client->ops->close(&cdev->lan_info, cdev->client, reset);
	clear_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state);
	i40e_client_release_qvlist(&cdev->lan_info);
}

/**
 * i40e_notify_client_of_vf_reset - call the client vf reset callback
 * @pf: PF device pointer
 * @vf_id: asolute id of VF being reset
 *
 * If there is a client attached to this PF, notify when a VF is reset
 **/
void i40e_notify_client_of_vf_reset(struct i40e_pf *pf, u32 vf_id)
{
	struct i40e_client_instance *cdev = pf->cinst;

	if (!cdev || !cdev->client)
		return;
	if (!cdev->client->ops || !cdev->client->ops->vf_reset) {
		dev_dbg(&pf->pdev->dev,
			"Cannot locate client instance VF reset routine\n");
		return;
	}
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED,  &cdev->state)) {
		dev_dbg(&pf->pdev->dev, "Client is not open, abort vf-reset\n");
		return;
	}
	cdev->client->ops->vf_reset(&cdev->lan_info, cdev->client, vf_id);
}

/**
 * i40e_notify_client_of_vf_enable - call the client vf notification callback
 * @pf: PF device pointer
 * @num_vfs: the number of VFs currently enabled, 0 for disable
 *
 * If there is a client attached to this PF, call its VF notification routine
 **/
void i40e_notify_client_of_vf_enable(struct i40e_pf *pf, u32 num_vfs)
{
	struct i40e_client_instance *cdev = pf->cinst;

	if (!cdev || !cdev->client)
		return;
	if (!cdev->client->ops || !cdev->client->ops->vf_enable) {
		dev_dbg(&pf->pdev->dev,
			"Cannot locate client instance VF enable routine\n");
		return;
	}
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED,
		      &cdev->state)) {
		dev_dbg(&pf->pdev->dev, "Client is not open, abort vf-enable\n");
		return;
	}
	cdev->client->ops->vf_enable(&cdev->lan_info, cdev->client, num_vfs);
}

/**
 * i40e_vf_client_capable - ask the client if it likes the specified VF
 * @pf: PF device pointer
 * @vf_id: the VF in question
 *
 * If there is a client of the specified type attached to this PF, call
 * its vf_capable routine
 **/
int i40e_vf_client_capable(struct i40e_pf *pf, u32 vf_id)
{
	struct i40e_client_instance *cdev = pf->cinst;
	int capable = false;

	if (!cdev || !cdev->client)
		goto out;
	if (!cdev->client->ops || !cdev->client->ops->vf_capable) {
		dev_dbg(&pf->pdev->dev,
			"Cannot locate client instance VF capability routine\n");
		goto out;
	}
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state))
		goto out;

	capable = cdev->client->ops->vf_capable(&cdev->lan_info,
						cdev->client,
						vf_id);
out:
	return capable;
}

void i40e_client_update_msix_info(struct i40e_pf *pf)
{
	struct i40e_client_instance *cdev = pf->cinst;

	if (!cdev || !cdev->client)
		return;

	cdev->lan_info.msix_count = pf->num_iwarp_msix;
	cdev->lan_info.msix_entries = &pf->msix_entries[pf->iwarp_base_vector];
}

/**
 * i40e_client_add_instance - add a client instance struct to the instance list
 * @pf: pointer to the board struct
 * @client: pointer to a client struct in the client list.
 * @existing: if there was already an existing instance
 *
 **/
static void i40e_client_add_instance(struct i40e_pf *pf)
{
	struct i40e_client_instance *cdev = NULL;
	struct netdev_hw_addr *mac = NULL;
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];

	if (!registered_client || pf->cinst)
		return;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return;

	cdev->lan_info.pf = (void *)pf;
	cdev->lan_info.netdev = vsi->netdev;
	cdev->lan_info.pcidev = pf->pdev;
	cdev->lan_info.fid = pf->hw.pf_id;
	cdev->lan_info.ftype = I40E_CLIENT_FTYPE_PF;
	cdev->lan_info.hw_addr = pf->hw.hw_addr;
	cdev->lan_info.ops = &i40e_lan_ops;
	cdev->lan_info.version.major = I40E_CLIENT_VERSION_MAJOR;
	cdev->lan_info.version.minor = I40E_CLIENT_VERSION_MINOR;
	cdev->lan_info.version.build = I40E_CLIENT_VERSION_BUILD;
	cdev->lan_info.fw_maj_ver = pf->hw.aq.fw_maj_ver;
	cdev->lan_info.fw_min_ver = pf->hw.aq.fw_min_ver;
	cdev->lan_info.fw_build = pf->hw.aq.fw_build;
	set_bit(__I40E_CLIENT_INSTANCE_NONE, &cdev->state);

	if (i40e_client_get_params(vsi, &cdev->lan_info.params)) {
		kfree(cdev);
		cdev = NULL;
		return;
	}

	mac = list_first_entry(&cdev->lan_info.netdev->dev_addrs.list,
			       struct netdev_hw_addr, list);
	if (mac)
		ether_addr_copy(cdev->lan_info.lanmac, mac->addr);
	else
		dev_err(&pf->pdev->dev, "MAC address list is empty!\n");

	cdev->client = registered_client;
	pf->cinst = cdev;

	i40e_client_update_msix_info(pf);
}

/**
 * i40e_client_del_instance - removes a client instance from the list
 * @pf: pointer to the board struct
 *
 **/
static
void i40e_client_del_instance(struct i40e_pf *pf)
{
	kfree(pf->cinst);
	pf->cinst = NULL;
}

/**
 * i40e_client_subtask - client maintenance work
 * @pf: board private structure
 **/
void i40e_client_subtask(struct i40e_pf *pf)
{
	struct i40e_client *client = registered_client;
	struct i40e_client_instance *cdev;
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	int ret = 0;

	if (!test_and_clear_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state))
		return;
	cdev = pf->cinst;

	/* If we're down or resetting, just bail */
	if (test_bit(__I40E_DOWN, pf->state) ||
	    test_bit(__I40E_CONFIG_BUSY, pf->state))
		return;

	if (!client || !cdev)
		return;

	/* Here we handle client opens. If the client is down, and
	 * the netdev is registered, then open the client.
	 */
	if (!test_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state)) {
		if (vsi->netdev_registered &&
		    client->ops && client->ops->open) {
			set_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state);
			ret = client->ops->open(&cdev->lan_info, client);
			if (ret) {
				/* Remove failed client instance */
				clear_bit(__I40E_CLIENT_INSTANCE_OPENED,
					  &cdev->state);
				i40e_client_del_instance(pf);
			}
		}
	}

	/* enable/disable PE TCP_ENA flag based on netdev down/up
	 */
	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		i40e_client_update_vsi_ctxt(&cdev->lan_info, client,
					    0, 0, 0,
					    I40E_CLIENT_VSI_FLAG_TCP_ENABLE);
	else
		i40e_client_update_vsi_ctxt(&cdev->lan_info, client,
					    0, 0,
					    I40E_CLIENT_VSI_FLAG_TCP_ENABLE,
					    I40E_CLIENT_VSI_FLAG_TCP_ENABLE);
}

/**
 * i40e_lan_add_device - add a lan device struct to the list of lan devices
 * @pf: pointer to the board struct
 *
 * Returns 0 on success or none 0 on error
 **/
int i40e_lan_add_device(struct i40e_pf *pf)
{
	struct i40e_device *ldev;
	int ret = 0;

	mutex_lock(&i40e_device_mutex);
	list_for_each_entry(ldev, &i40e_devices, list) {
		if (ldev->pf == pf) {
			ret = -EEXIST;
			goto out;
		}
	}
	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev) {
		ret = -ENOMEM;
		goto out;
	}
	ldev->pf = pf;
	INIT_LIST_HEAD(&ldev->list);
	list_add(&ldev->list, &i40e_devices);
	dev_info(&pf->pdev->dev, "Added LAN device PF%d bus=0x%02x dev=0x%02x func=0x%02x\n",
		 pf->hw.pf_id, pf->hw.bus.bus_id,
		 pf->hw.bus.device, pf->hw.bus.func);

	/* If a client has already been registered, we need to add an instance
	 * of it to our new LAN device.
	 */
	if (registered_client)
		i40e_client_add_instance(pf);

	/* Since in some cases register may have happened before a device gets
	 * added, we can schedule a subtask to go initiate the clients if
	 * they can be launched at probe time.
	 */
	set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
	i40e_service_event_schedule(pf);

out:
	mutex_unlock(&i40e_device_mutex);
	return ret;
}

/**
 * i40e_lan_del_device - removes a lan device from the device list
 * @pf: pointer to the board struct
 *
 * Returns 0 on success or non-0 on error
 **/
int i40e_lan_del_device(struct i40e_pf *pf)
{
	struct i40e_device *ldev, *tmp;
	int ret = -ENODEV;

	/* First, remove any client instance. */
	i40e_client_del_instance(pf);

	mutex_lock(&i40e_device_mutex);
	list_for_each_entry_safe(ldev, tmp, &i40e_devices, list) {
		if (ldev->pf == pf) {
			dev_info(&pf->pdev->dev, "Deleted LAN device PF%d bus=0x%02x dev=0x%02x func=0x%02x\n",
				 pf->hw.pf_id, pf->hw.bus.bus_id,
				 pf->hw.bus.device, pf->hw.bus.func);
			list_del(&ldev->list);
			kfree(ldev);
			ret = 0;
			break;
		}
	}
	mutex_unlock(&i40e_device_mutex);
	return ret;
}

/**
 * i40e_client_release - release client specific resources
 * @client: pointer to the registered client
 *
 **/
static void i40e_client_release(struct i40e_client *client)
{
	struct i40e_client_instance *cdev;
	struct i40e_device *ldev;
	struct i40e_pf *pf;

	mutex_lock(&i40e_device_mutex);
	list_for_each_entry(ldev, &i40e_devices, list) {
		pf = ldev->pf;
		cdev = pf->cinst;
		if (!cdev)
			continue;

		while (test_and_set_bit(__I40E_SERVICE_SCHED,
					pf->state))
			usleep_range(500, 1000);

		if (test_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state)) {
			if (client->ops && client->ops->close)
				client->ops->close(&cdev->lan_info, client,
						   false);
			i40e_client_release_qvlist(&cdev->lan_info);
			clear_bit(__I40E_CLIENT_INSTANCE_OPENED, &cdev->state);

			dev_warn(&pf->pdev->dev,
				 "Client %s instance for PF id %d closed\n",
				 client->name, pf->hw.pf_id);
		}
		/* delete the client instance */
		i40e_client_del_instance(pf);
		dev_info(&pf->pdev->dev, "Deleted client instance of Client %s\n",
			 client->name);
		clear_bit(__I40E_SERVICE_SCHED, pf->state);
	}
	mutex_unlock(&i40e_device_mutex);
}

/**
 * i40e_client_prepare - prepare client specific resources
 * @client: pointer to the registered client
 *
 **/
static void i40e_client_prepare(struct i40e_client *client)
{
	struct i40e_device *ldev;
	struct i40e_pf *pf;

	mutex_lock(&i40e_device_mutex);
	list_for_each_entry(ldev, &i40e_devices, list) {
		pf = ldev->pf;
		i40e_client_add_instance(pf);
		/* Start the client subtask */
		set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
		i40e_service_event_schedule(pf);
	}
	mutex_unlock(&i40e_device_mutex);
}

/**
 * i40e_client_virtchnl_send - TBD
 * @ldev: pointer to L2 context
 * @client: Client pointer
 * @vf_id: absolute VF identifier
 * @msg: message buffer
 * @len: length of message buffer
 *
 * Return 0 on success or < 0 on error
 **/
static int i40e_client_virtchnl_send(struct i40e_info *ldev,
				     struct i40e_client *client,
				     u32 vf_id, u8 *msg, u16 len)
{
	struct i40e_pf *pf = ldev->pf;
	struct i40e_hw *hw = &pf->hw;
	i40e_status err;

	err = i40e_aq_send_msg_to_vf(hw, vf_id, VIRTCHNL_OP_IWARP,
				     0, msg, len, NULL);
	if (err)
		dev_err(&pf->pdev->dev, "Unable to send iWarp message to VF, error %d, aq status %d\n",
			err, hw->aq.asq_last_status);

	return err;
}

/**
 * i40e_client_setup_qvlist
 * @ldev: pointer to L2 context.
 * @client: Client pointer.
 * @qvlist_info: queue and vector list
 *
 * Return 0 on success or < 0 on error
 **/
static int i40e_client_setup_qvlist(struct i40e_info *ldev,
				    struct i40e_client *client,
				    struct i40e_qvlist_info *qvlist_info)
{
	struct i40e_pf *pf = ldev->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_qv_info *qv_info;
	u32 v_idx, i, reg_idx, reg;

	ldev->qvlist_info = kzalloc(struct_size(ldev->qvlist_info, qv_info,
				    qvlist_info->num_vectors - 1), GFP_KERNEL);
	if (!ldev->qvlist_info)
		return -ENOMEM;
	ldev->qvlist_info->num_vectors = qvlist_info->num_vectors;

	for (i = 0; i < qvlist_info->num_vectors; i++) {
		qv_info = &qvlist_info->qv_info[i];
		if (!qv_info)
			continue;
		v_idx = qv_info->v_idx;

		/* Validate vector id belongs to this client */
		if ((v_idx >= (pf->iwarp_base_vector + pf->num_iwarp_msix)) ||
		    (v_idx < pf->iwarp_base_vector))
			goto err;

		ldev->qvlist_info->qv_info[i] = *qv_info;
		reg_idx = I40E_PFINT_LNKLSTN(v_idx - 1);

		if (qv_info->ceq_idx == I40E_QUEUE_INVALID_IDX) {
			/* Special case - No CEQ mapped on this vector */
			wr32(hw, reg_idx, I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK);
		} else {
			reg = (qv_info->ceq_idx &
			       I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK) |
			       (I40E_QUEUE_TYPE_PE_CEQ <<
			       I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT);
			wr32(hw, reg_idx, reg);

			reg = (I40E_PFINT_CEQCTL_CAUSE_ENA_MASK |
			       (v_idx << I40E_PFINT_CEQCTL_MSIX_INDX_SHIFT) |
			       (qv_info->itr_idx <<
				I40E_PFINT_CEQCTL_ITR_INDX_SHIFT) |
			       (I40E_QUEUE_END_OF_LIST <<
				I40E_PFINT_CEQCTL_NEXTQ_INDX_SHIFT));
			wr32(hw, I40E_PFINT_CEQCTL(qv_info->ceq_idx), reg);
		}
		if (qv_info->aeq_idx != I40E_QUEUE_INVALID_IDX) {
			reg = (I40E_PFINT_AEQCTL_CAUSE_ENA_MASK |
			       (v_idx << I40E_PFINT_AEQCTL_MSIX_INDX_SHIFT) |
			       (qv_info->itr_idx <<
				I40E_PFINT_AEQCTL_ITR_INDX_SHIFT));

			wr32(hw, I40E_PFINT_AEQCTL, reg);
		}
	}
	/* Mitigate sync problems with iwarp VF driver */
	i40e_flush(hw);
	return 0;
err:
	kfree(ldev->qvlist_info);
	ldev->qvlist_info = NULL;
	return -EINVAL;
}

/**
 * i40e_client_request_reset
 * @ldev: pointer to L2 context.
 * @client: Client pointer.
 * @reset_level: reset level
 **/
static void i40e_client_request_reset(struct i40e_info *ldev,
				      struct i40e_client *client,
				      u32 reset_level)
{
	struct i40e_pf *pf = ldev->pf;

	switch (reset_level) {
	case I40E_CLIENT_RESET_LEVEL_PF:
		set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
		break;
	case I40E_CLIENT_RESET_LEVEL_CORE:
		set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
		break;
	default:
		dev_warn(&pf->pdev->dev,
			 "Client for PF id %d requested an unsupported reset: %d.\n",
			 pf->hw.pf_id, reset_level);
		break;
	}

	i40e_service_event_schedule(pf);
}

/**
 * i40e_client_update_vsi_ctxt
 * @ldev: pointer to L2 context.
 * @client: Client pointer.
 * @is_vf: if this for the VF
 * @vf_id: if is_vf true this carries the vf_id
 * @flag: Any device level setting that needs to be done for PE
 * @valid_flag: Bits in this match up and enable changing of flag bits
 *
 * Return 0 on success or < 0 on error
 **/
static int i40e_client_update_vsi_ctxt(struct i40e_info *ldev,
				       struct i40e_client *client,
				       bool is_vf, u32 vf_id,
				       u32 flag, u32 valid_flag)
{
	struct i40e_pf *pf = ldev->pf;
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	struct i40e_vsi_context ctxt;
	bool update = true;
	i40e_status err;

	/* TODO: for now do not allow setting VF's VSI setting */
	if (is_vf)
		return -EINVAL;

	ctxt.seid = pf->main_vsi_seid;
	ctxt.pf_num = pf->hw.pf_id;
	err = i40e_aq_get_vsi_params(&pf->hw, &ctxt, NULL);
	ctxt.flags = I40E_AQ_VSI_TYPE_PF;
	if (err) {
		dev_info(&pf->pdev->dev,
			 "couldn't get PF vsi config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, err),
			 i40e_aq_str(&pf->hw,
				     pf->hw.aq.asq_last_status));
		return -ENOENT;
	}

	if ((valid_flag & I40E_CLIENT_VSI_FLAG_TCP_ENABLE) &&
	    (flag & I40E_CLIENT_VSI_FLAG_TCP_ENABLE)) {
		ctxt.info.valid_sections =
			cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
		ctxt.info.queueing_opt_flags |= I40E_AQ_VSI_QUE_OPT_TCP_ENA;
	} else if ((valid_flag & I40E_CLIENT_VSI_FLAG_TCP_ENABLE) &&
		  !(flag & I40E_CLIENT_VSI_FLAG_TCP_ENABLE)) {
		ctxt.info.valid_sections =
			cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
		ctxt.info.queueing_opt_flags &= ~I40E_AQ_VSI_QUE_OPT_TCP_ENA;
	} else {
		update = false;
		dev_warn(&pf->pdev->dev,
			 "Client for PF id %d request an unsupported Config: %x.\n",
			 pf->hw.pf_id, flag);
	}

	if (update) {
		err = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "update VSI ctxt for PE failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, err),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
		}
	}
	return err;
}

/**
 * i40e_register_client - Register a i40e client driver with the L2 driver
 * @client: pointer to the i40e_client struct
 *
 * Returns 0 on success or non-0 on error
 **/
int i40e_register_client(struct i40e_client *client)
{
	int ret = 0;

	if (!client) {
		ret = -EIO;
		goto out;
	}

	if (strlen(client->name) == 0) {
		pr_info("i40e: Failed to register client with no name\n");
		ret = -EIO;
		goto out;
	}

	if (registered_client) {
		pr_info("i40e: Client %s has already been registered!\n",
			client->name);
		ret = -EEXIST;
		goto out;
	}

	if ((client->version.major != I40E_CLIENT_VERSION_MAJOR) ||
	    (client->version.minor != I40E_CLIENT_VERSION_MINOR)) {
		pr_info("i40e: Failed to register client %s due to mismatched client interface version\n",
			client->name);
		pr_info("Client is using version: %02d.%02d.%02d while LAN driver supports %s\n",
			client->version.major, client->version.minor,
			client->version.build,
			i40e_client_interface_version_str);
		ret = -EIO;
		goto out;
	}

	registered_client = client;

	i40e_client_prepare(client);

	pr_info("i40e: Registered client %s\n", client->name);
out:
	return ret;
}
EXPORT_SYMBOL(i40e_register_client);

/**
 * i40e_unregister_client - Unregister a i40e client driver with the L2 driver
 * @client: pointer to the i40e_client struct
 *
 * Returns 0 on success or non-0 on error
 **/
int i40e_unregister_client(struct i40e_client *client)
{
	int ret = 0;

	if (registered_client != client) {
		pr_info("i40e: Client %s has not been registered\n",
			client->name);
		ret = -ENODEV;
		goto out;
	}
	registered_client = NULL;
	/* When a unregister request comes through we would have to send
	 * a close for each of the client instances that were opened.
	 * client_release function is called to handle this.
	 */
	i40e_client_release(client);

	pr_info("i40e: Unregistered client %s\n", client->name);
out:
	return ret;
}
EXPORT_SYMBOL(i40e_unregister_client);
