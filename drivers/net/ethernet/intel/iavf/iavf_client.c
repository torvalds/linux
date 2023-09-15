// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/list.h>
#include <linux/errno.h>

#include "iavf.h"
#include "iavf_prototype.h"
#include "iavf_client.h"

static
const char iavf_client_interface_version_str[] = IAVF_CLIENT_VERSION_STR;
static struct iavf_client *vf_registered_client;
static LIST_HEAD(iavf_devices);
static DEFINE_MUTEX(iavf_device_mutex);

static u32 iavf_client_virtchnl_send(struct iavf_info *ldev,
				     struct iavf_client *client,
				     u8 *msg, u16 len);

static int iavf_client_setup_qvlist(struct iavf_info *ldev,
				    struct iavf_client *client,
				    struct iavf_qvlist_info *qvlist_info);

static struct iavf_ops iavf_lan_ops = {
	.virtchnl_send = iavf_client_virtchnl_send,
	.setup_qvlist = iavf_client_setup_qvlist,
};

/**
 * iavf_client_get_params - retrieve relevant client parameters
 * @vsi: VSI with parameters
 * @params: client param struct
 **/
static
void iavf_client_get_params(struct iavf_vsi *vsi, struct iavf_params *params)
{
	int i;

	memset(params, 0, sizeof(struct iavf_params));
	params->mtu = vsi->netdev->mtu;
	params->link_up = vsi->back->link_up;

	for (i = 0; i < IAVF_MAX_USER_PRIORITY; i++) {
		params->qos.prio_qos[i].tc = 0;
		params->qos.prio_qos[i].qs_handle = vsi->qs_handle;
	}
}

/**
 * iavf_notify_client_message - call the client message receive callback
 * @vsi: the VSI associated with this client
 * @msg: message buffer
 * @len: length of message
 *
 * If there is a client to this VSI, call the client
 **/
void iavf_notify_client_message(struct iavf_vsi *vsi, u8 *msg, u16 len)
{
	struct iavf_client_instance *cinst;

	if (!vsi)
		return;

	cinst = vsi->back->cinst;
	if (!cinst || !cinst->client || !cinst->client->ops ||
	    !cinst->client->ops->virtchnl_receive) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance virtchnl_receive function\n");
		return;
	}
	cinst->client->ops->virtchnl_receive(&cinst->lan_info,  cinst->client,
					     msg, len);
}

/**
 * iavf_notify_client_l2_params - call the client notify callback
 * @vsi: the VSI with l2 param changes
 *
 * If there is a client to this VSI, call the client
 **/
void iavf_notify_client_l2_params(struct iavf_vsi *vsi)
{
	struct iavf_client_instance *cinst;
	struct iavf_params params;

	if (!vsi)
		return;

	cinst = vsi->back->cinst;

	if (!cinst || !cinst->client || !cinst->client->ops ||
	    !cinst->client->ops->l2_param_change) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance l2_param_change function\n");
		return;
	}
	iavf_client_get_params(vsi, &params);
	cinst->lan_info.params = params;
	cinst->client->ops->l2_param_change(&cinst->lan_info, cinst->client,
					    &params);
}

/**
 * iavf_notify_client_open - call the client open callback
 * @vsi: the VSI with netdev opened
 *
 * If there is a client to this netdev, call the client with open
 **/
void iavf_notify_client_open(struct iavf_vsi *vsi)
{
	struct iavf_adapter *adapter = vsi->back;
	struct iavf_client_instance *cinst = adapter->cinst;
	int ret;

	if (!cinst || !cinst->client || !cinst->client->ops ||
	    !cinst->client->ops->open) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance open function\n");
		return;
	}
	if (!(test_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state))) {
		ret = cinst->client->ops->open(&cinst->lan_info, cinst->client);
		if (!ret)
			set_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state);
	}
}

/**
 * iavf_client_release_qvlist - send a message to the PF to release rdma qv map
 * @ldev: pointer to L2 context.
 *
 * Return 0 on success or < 0 on error
 **/
static int iavf_client_release_qvlist(struct iavf_info *ldev)
{
	struct iavf_adapter *adapter = ldev->vf;
	enum iavf_status err;

	if (adapter->aq_required)
		return -EAGAIN;

	err = iavf_aq_send_msg_to_pf(&adapter->hw,
				     VIRTCHNL_OP_RELEASE_RDMA_IRQ_MAP,
				     IAVF_SUCCESS, NULL, 0, NULL);

	if (err)
		dev_err(&adapter->pdev->dev,
			"Unable to send RDMA vector release message to PF, error %d, aq status %d\n",
			err, adapter->hw.aq.asq_last_status);

	return err;
}

/**
 * iavf_notify_client_close - call the client close callback
 * @vsi: the VSI with netdev closed
 * @reset: true when close called due to reset pending
 *
 * If there is a client to this netdev, call the client with close
 **/
void iavf_notify_client_close(struct iavf_vsi *vsi, bool reset)
{
	struct iavf_adapter *adapter = vsi->back;
	struct iavf_client_instance *cinst = adapter->cinst;

	if (!cinst || !cinst->client || !cinst->client->ops ||
	    !cinst->client->ops->close) {
		dev_dbg(&vsi->back->pdev->dev,
			"Cannot locate client instance close function\n");
		return;
	}
	cinst->client->ops->close(&cinst->lan_info, cinst->client, reset);
	iavf_client_release_qvlist(&cinst->lan_info);
	clear_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state);
}

/**
 * iavf_client_add_instance - add a client instance to the instance list
 * @adapter: pointer to the board struct
 *
 * Returns cinst ptr on success, NULL on failure
 **/
static struct iavf_client_instance *
iavf_client_add_instance(struct iavf_adapter *adapter)
{
	struct iavf_client_instance *cinst = NULL;
	struct iavf_vsi *vsi = &adapter->vsi;
	struct netdev_hw_addr *mac = NULL;
	struct iavf_params params;

	if (!vf_registered_client)
		goto out;

	if (adapter->cinst) {
		cinst = adapter->cinst;
		goto out;
	}

	cinst = kzalloc(sizeof(*cinst), GFP_KERNEL);
	if (!cinst)
		goto out;

	cinst->lan_info.vf = (void *)adapter;
	cinst->lan_info.netdev = vsi->netdev;
	cinst->lan_info.pcidev = adapter->pdev;
	cinst->lan_info.fid = 0;
	cinst->lan_info.ftype = IAVF_CLIENT_FTYPE_VF;
	cinst->lan_info.hw_addr = adapter->hw.hw_addr;
	cinst->lan_info.ops = &iavf_lan_ops;
	cinst->lan_info.version.major = IAVF_CLIENT_VERSION_MAJOR;
	cinst->lan_info.version.minor = IAVF_CLIENT_VERSION_MINOR;
	cinst->lan_info.version.build = IAVF_CLIENT_VERSION_BUILD;
	iavf_client_get_params(vsi, &params);
	cinst->lan_info.params = params;
	set_bit(__IAVF_CLIENT_INSTANCE_NONE, &cinst->state);

	cinst->lan_info.msix_count = adapter->num_rdma_msix;
	cinst->lan_info.msix_entries =
			&adapter->msix_entries[adapter->rdma_base_vector];

	mac = list_first_entry(&cinst->lan_info.netdev->dev_addrs.list,
			       struct netdev_hw_addr, list);
	if (mac)
		ether_addr_copy(cinst->lan_info.lanmac, mac->addr);
	else
		dev_err(&adapter->pdev->dev, "MAC address list is empty!\n");

	cinst->client = vf_registered_client;
	adapter->cinst = cinst;
out:
	return cinst;
}

/**
 * iavf_client_del_instance - removes a client instance from the list
 * @adapter: pointer to the board struct
 *
 **/
static
void iavf_client_del_instance(struct iavf_adapter *adapter)
{
	kfree(adapter->cinst);
	adapter->cinst = NULL;
}

/**
 * iavf_client_subtask - client maintenance work
 * @adapter: board private structure
 **/
void iavf_client_subtask(struct iavf_adapter *adapter)
{
	struct iavf_client *client = vf_registered_client;
	struct iavf_client_instance *cinst;
	int ret = 0;

	if (adapter->state < __IAVF_DOWN)
		return;

	/* first check client is registered */
	if (!client)
		return;

	/* Add the client instance to the instance list */
	cinst = iavf_client_add_instance(adapter);
	if (!cinst)
		return;

	dev_info(&adapter->pdev->dev, "Added instance of Client %s\n",
		 client->name);

	if (!test_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state)) {
		/* Send an Open request to the client */

		if (client->ops && client->ops->open)
			ret = client->ops->open(&cinst->lan_info, client);
		if (!ret)
			set_bit(__IAVF_CLIENT_INSTANCE_OPENED,
				&cinst->state);
		else
			/* remove client instance */
			iavf_client_del_instance(adapter);
	}
}

/**
 * iavf_lan_add_device - add a lan device struct to the list of lan devices
 * @adapter: pointer to the board struct
 *
 * Returns 0 on success or none 0 on error
 **/
int iavf_lan_add_device(struct iavf_adapter *adapter)
{
	struct iavf_device *ldev;
	int ret = 0;

	mutex_lock(&iavf_device_mutex);
	list_for_each_entry(ldev, &iavf_devices, list) {
		if (ldev->vf == adapter) {
			ret = -EEXIST;
			goto out;
		}
	}
	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev) {
		ret = -ENOMEM;
		goto out;
	}
	ldev->vf = adapter;
	INIT_LIST_HEAD(&ldev->list);
	list_add(&ldev->list, &iavf_devices);
	dev_info(&adapter->pdev->dev, "Added LAN device bus=0x%02x dev=0x%02x func=0x%02x\n",
		 adapter->hw.bus.bus_id, adapter->hw.bus.device,
		 adapter->hw.bus.func);

	/* Since in some cases register may have happened before a device gets
	 * added, we can schedule a subtask to go initiate the clients.
	 */
	adapter->flags |= IAVF_FLAG_SERVICE_CLIENT_REQUESTED;

out:
	mutex_unlock(&iavf_device_mutex);
	return ret;
}

/**
 * iavf_lan_del_device - removes a lan device from the device list
 * @adapter: pointer to the board struct
 *
 * Returns 0 on success or non-0 on error
 **/
int iavf_lan_del_device(struct iavf_adapter *adapter)
{
	struct iavf_device *ldev, *tmp;
	int ret = -ENODEV;

	mutex_lock(&iavf_device_mutex);
	list_for_each_entry_safe(ldev, tmp, &iavf_devices, list) {
		if (ldev->vf == adapter) {
			dev_info(&adapter->pdev->dev,
				 "Deleted LAN device bus=0x%02x dev=0x%02x func=0x%02x\n",
				 adapter->hw.bus.bus_id, adapter->hw.bus.device,
				 adapter->hw.bus.func);
			list_del(&ldev->list);
			kfree(ldev);
			ret = 0;
			break;
		}
	}

	mutex_unlock(&iavf_device_mutex);
	return ret;
}

/**
 * iavf_client_release - release client specific resources
 * @client: pointer to the registered client
 *
 **/
static void iavf_client_release(struct iavf_client *client)
{
	struct iavf_client_instance *cinst;
	struct iavf_device *ldev;
	struct iavf_adapter *adapter;

	mutex_lock(&iavf_device_mutex);
	list_for_each_entry(ldev, &iavf_devices, list) {
		adapter = ldev->vf;
		cinst = adapter->cinst;
		if (!cinst)
			continue;
		if (test_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state)) {
			if (client->ops && client->ops->close)
				client->ops->close(&cinst->lan_info, client,
						   false);
			iavf_client_release_qvlist(&cinst->lan_info);
			clear_bit(__IAVF_CLIENT_INSTANCE_OPENED, &cinst->state);

			dev_warn(&adapter->pdev->dev,
				 "Client %s instance closed\n", client->name);
		}
		/* delete the client instance */
		iavf_client_del_instance(adapter);
		dev_info(&adapter->pdev->dev, "Deleted client instance of Client %s\n",
			 client->name);
	}
	mutex_unlock(&iavf_device_mutex);
}

/**
 * iavf_client_prepare - prepare client specific resources
 * @client: pointer to the registered client
 *
 **/
static void iavf_client_prepare(struct iavf_client *client)
{
	struct iavf_device *ldev;
	struct iavf_adapter *adapter;

	mutex_lock(&iavf_device_mutex);
	list_for_each_entry(ldev, &iavf_devices, list) {
		adapter = ldev->vf;
		/* Signal the watchdog to service the client */
		adapter->flags |= IAVF_FLAG_SERVICE_CLIENT_REQUESTED;
	}
	mutex_unlock(&iavf_device_mutex);
}

/**
 * iavf_client_virtchnl_send - send a message to the PF instance
 * @ldev: pointer to L2 context.
 * @client: Client pointer.
 * @msg: pointer to message buffer
 * @len: message length
 *
 * Return 0 on success or < 0 on error
 **/
static u32 iavf_client_virtchnl_send(struct iavf_info *ldev,
				     struct iavf_client *client,
				     u8 *msg, u16 len)
{
	struct iavf_adapter *adapter = ldev->vf;
	enum iavf_status err;

	if (adapter->aq_required)
		return -EAGAIN;

	err = iavf_aq_send_msg_to_pf(&adapter->hw, VIRTCHNL_OP_RDMA,
				     IAVF_SUCCESS, msg, len, NULL);
	if (err)
		dev_err(&adapter->pdev->dev, "Unable to send RDMA message to PF, error %d, aq status %d\n",
			err, adapter->hw.aq.asq_last_status);

	return err;
}

/**
 * iavf_client_setup_qvlist - send a message to the PF to setup rdma qv map
 * @ldev: pointer to L2 context.
 * @client: Client pointer.
 * @qvlist_info: queue and vector list
 *
 * Return 0 on success or < 0 on error
 **/
static int iavf_client_setup_qvlist(struct iavf_info *ldev,
				    struct iavf_client *client,
				    struct iavf_qvlist_info *qvlist_info)
{
	struct virtchnl_rdma_qvlist_info *v_qvlist_info;
	struct iavf_adapter *adapter = ldev->vf;
	struct iavf_qv_info *qv_info;
	enum iavf_status err;
	u32 v_idx, i;
	size_t msg_size;

	if (adapter->aq_required)
		return -EAGAIN;

	/* A quick check on whether the vectors belong to the client */
	for (i = 0; i < qvlist_info->num_vectors; i++) {
		qv_info = &qvlist_info->qv_info[i];
		if (!qv_info)
			continue;
		v_idx = qv_info->v_idx;
		if ((v_idx >=
		    (adapter->rdma_base_vector + adapter->num_rdma_msix)) ||
		    (v_idx < adapter->rdma_base_vector))
			return -EINVAL;
	}

	v_qvlist_info = (struct virtchnl_rdma_qvlist_info *)qvlist_info;
	msg_size = virtchnl_struct_size(v_qvlist_info, qv_info,
					v_qvlist_info->num_vectors);

	adapter->client_pending |= BIT(VIRTCHNL_OP_CONFIG_RDMA_IRQ_MAP);
	err = iavf_aq_send_msg_to_pf(&adapter->hw,
				VIRTCHNL_OP_CONFIG_RDMA_IRQ_MAP, IAVF_SUCCESS,
				(u8 *)v_qvlist_info, msg_size, NULL);

	if (err) {
		dev_err(&adapter->pdev->dev,
			"Unable to send RDMA vector config message to PF, error %d, aq status %d\n",
			err, adapter->hw.aq.asq_last_status);
		goto out;
	}

	err = -EBUSY;
	for (i = 0; i < 5; i++) {
		msleep(100);
		if (!(adapter->client_pending &
		      BIT(VIRTCHNL_OP_CONFIG_RDMA_IRQ_MAP))) {
			err = 0;
			break;
		}
	}
out:
	return err;
}

/**
 * iavf_register_client - Register a iavf client driver with the L2 driver
 * @client: pointer to the iavf_client struct
 *
 * Returns 0 on success or non-0 on error
 **/
int iavf_register_client(struct iavf_client *client)
{
	int ret = 0;

	if (!client) {
		ret = -EIO;
		goto out;
	}

	if (strlen(client->name) == 0) {
		pr_info("iavf: Failed to register client with no name\n");
		ret = -EIO;
		goto out;
	}

	if (vf_registered_client) {
		pr_info("iavf: Client %s has already been registered!\n",
			client->name);
		ret = -EEXIST;
		goto out;
	}

	if ((client->version.major != IAVF_CLIENT_VERSION_MAJOR) ||
	    (client->version.minor != IAVF_CLIENT_VERSION_MINOR)) {
		pr_info("iavf: Failed to register client %s due to mismatched client interface version\n",
			client->name);
		pr_info("Client is using version: %02d.%02d.%02d while LAN driver supports %s\n",
			client->version.major, client->version.minor,
			client->version.build,
			iavf_client_interface_version_str);
		ret = -EIO;
		goto out;
	}

	vf_registered_client = client;

	iavf_client_prepare(client);

	pr_info("iavf: Registered client %s with return code %d\n",
		client->name, ret);
out:
	return ret;
}
EXPORT_SYMBOL(iavf_register_client);

/**
 * iavf_unregister_client - Unregister a iavf client driver with the L2 driver
 * @client: pointer to the iavf_client struct
 *
 * Returns 0 on success or non-0 on error
 **/
int iavf_unregister_client(struct iavf_client *client)
{
	int ret = 0;

	/* When a unregister request comes through we would have to send
	 * a close for each of the client instances that were opened.
	 * client_release function is called to handle this.
	 */
	iavf_client_release(client);

	if (vf_registered_client != client) {
		pr_info("iavf: Client %s has not been registered\n",
			client->name);
		ret = -ENODEV;
		goto out;
	}
	vf_registered_client = NULL;
	pr_info("iavf: Unregistered client %s\n", client->name);
out:
	return ret;
}
EXPORT_SYMBOL(iavf_unregister_client);
