/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/nls.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>

#include "fjes.h"

#define MAJ 1
#define MIN 0
#define DRV_VERSION __stringify(MAJ) "." __stringify(MIN)
#define DRV_NAME	"fjes"
char fjes_driver_name[] = DRV_NAME;
char fjes_driver_version[] = DRV_VERSION;
static const char fjes_driver_string[] =
		"FUJITSU Extended Socket Network Device Driver";
static const char fjes_copyright[] =
		"Copyright (c) 2015 FUJITSU LIMITED";

MODULE_AUTHOR("Taku Izumi <izumi.taku@jp.fujitsu.com>");
MODULE_DESCRIPTION("FUJITSU Extended Socket Network Device Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int fjes_request_irq(struct fjes_adapter *);
static void fjes_free_irq(struct fjes_adapter *);

static int fjes_open(struct net_device *);
static int fjes_close(struct net_device *);
static int fjes_setup_resources(struct fjes_adapter *);
static void fjes_free_resources(struct fjes_adapter *);
static irqreturn_t fjes_intr(int, void*);

static int fjes_acpi_add(struct acpi_device *);
static int fjes_acpi_remove(struct acpi_device *);
static acpi_status fjes_get_acpi_resource(struct acpi_resource *, void*);

static int fjes_probe(struct platform_device *);
static int fjes_remove(struct platform_device *);

static int fjes_sw_init(struct fjes_adapter *);
static void fjes_netdev_setup(struct net_device *);

static const struct acpi_device_id fjes_acpi_ids[] = {
	{"PNP0C02", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fjes_acpi_ids);

static struct acpi_driver fjes_acpi_driver = {
	.name = DRV_NAME,
	.class = DRV_NAME,
	.owner = THIS_MODULE,
	.ids = fjes_acpi_ids,
	.ops = {
		.add = fjes_acpi_add,
		.remove = fjes_acpi_remove,
	},
};

static struct platform_driver fjes_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = fjes_probe,
	.remove = fjes_remove,
};

static struct resource fjes_resource[] = {
	{
		.flags = IORESOURCE_MEM,
		.start = 0,
		.end = 0,
	},
	{
		.flags = IORESOURCE_IRQ,
		.start = 0,
		.end = 0,
	},
};

static int fjes_acpi_add(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};
	char str_buf[sizeof(FJES_ACPI_SYMBOL) + 1];
	struct platform_device *plat_dev;
	union acpi_object *str;
	acpi_status status;
	int result;

	status = acpi_evaluate_object(device->handle, "_STR", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	str = buffer.pointer;
	result = utf16s_to_utf8s((wchar_t *)str->string.pointer,
				 str->string.length, UTF16_LITTLE_ENDIAN,
				 str_buf, sizeof(str_buf) - 1);
	str_buf[result] = 0;

	if (strncmp(FJES_ACPI_SYMBOL, str_buf, strlen(FJES_ACPI_SYMBOL)) != 0) {
		kfree(buffer.pointer);
		return -ENODEV;
	}
	kfree(buffer.pointer);

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     fjes_get_acpi_resource, fjes_resource);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* create platform_device */
	plat_dev = platform_device_register_simple(DRV_NAME, 0, fjes_resource,
						   ARRAY_SIZE(fjes_resource));
	device->driver_data = plat_dev;

	return 0;
}

static int fjes_acpi_remove(struct acpi_device *device)
{
	struct platform_device *plat_dev;

	plat_dev = (struct platform_device *)acpi_driver_data(device);
	platform_device_unregister(plat_dev);

	return 0;
}

static acpi_status
fjes_get_acpi_resource(struct acpi_resource *acpi_res, void *data)
{
	struct acpi_resource_address32 *addr;
	struct acpi_resource_irq *irq;
	struct resource *res = data;

	switch (acpi_res->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		addr = &acpi_res->data.address32;
		res[0].start = addr->address.minimum;
		res[0].end = addr->address.minimum +
			addr->address.address_length - 1;
		break;

	case ACPI_RESOURCE_TYPE_IRQ:
		irq = &acpi_res->data.irq;
		if (irq->interrupt_count != 1)
			return AE_ERROR;
		res[1].start = irq->interrupts[0];
		res[1].end = irq->interrupts[0];
		break;

	default:
		break;
	}

	return AE_OK;
}

static int fjes_request_irq(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int result = -1;

	if (!adapter->irq_registered) {
		result = request_irq(adapter->hw.hw_res.irq, fjes_intr,
				     IRQF_SHARED, netdev->name, adapter);
		if (result)
			adapter->irq_registered = false;
		else
			adapter->irq_registered = true;
	}

	return result;
}

static void fjes_free_irq(struct fjes_adapter *adapter)
{
	struct fjes_hw *hw = &adapter->hw;

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_ALL, true);

	if (adapter->irq_registered) {
		free_irq(adapter->hw.hw_res.irq, adapter);
		adapter->irq_registered = false;
	}
}

static const struct net_device_ops fjes_netdev_ops = {
	.ndo_open		= fjes_open,
	.ndo_stop		= fjes_close,
};

/* fjes_open - Called when a network interface is made active */
static int fjes_open(struct net_device *netdev)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int result;

	if (adapter->open_guard)
		return -ENXIO;

	result = fjes_setup_resources(adapter);
	if (result)
		goto err_setup_res;

	hw->txrx_stop_req_bit = 0;
	hw->epstop_req_bit = 0;

	fjes_hw_capture_interrupt_status(hw);

	result = fjes_request_irq(adapter);
	if (result)
		goto err_req_irq;

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_ALL, false);

	netif_tx_start_all_queues(netdev);
	netif_carrier_on(netdev);

	return 0;

err_req_irq:
	fjes_free_irq(adapter);

err_setup_res:
	fjes_free_resources(adapter);
	return result;
}

/* fjes_close - Disables a network interface */
static int fjes_close(struct net_device *netdev)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int epidx;

	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);

	fjes_hw_raise_epstop(hw);

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		adapter->hw.ep_shm_info[epidx].tx.info->v1i.rx_status &=
			~FJES_RX_POLL_WORK;
	}

	fjes_free_irq(adapter);

	fjes_hw_wait_epstop(hw);

	fjes_free_resources(adapter);

	return 0;
}

static int fjes_setup_resources(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ep_share_mem_info *buf_pair;
	struct fjes_hw *hw = &adapter->hw;
	int result;
	int epidx;

	mutex_lock(&hw->hw_info.lock);
	result = fjes_hw_request_info(hw);
	switch (result) {
	case 0:
		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			hw->ep_shm_info[epidx].es_status =
			    hw->hw_info.res_buf->info.info[epidx].es_status;
			hw->ep_shm_info[epidx].zone =
			    hw->hw_info.res_buf->info.info[epidx].zone;
		}
		break;
	default:
	case -ENOMSG:
	case -EBUSY:
		adapter->force_reset = true;

		mutex_unlock(&hw->hw_info.lock);
		return result;
	}
	mutex_unlock(&hw->hw_info.lock);

	for (epidx = 0; epidx < (hw->max_epid); epidx++) {
		if ((epidx != hw->my_epid) &&
		    (hw->ep_shm_info[epidx].es_status ==
		     FJES_ZONING_STATUS_ENABLE)) {
			fjes_hw_raise_interrupt(hw, epidx,
						REG_ICTL_MASK_INFO_UPDATE);
		}
	}

	msleep(FJES_OPEN_ZONE_UPDATE_WAIT * hw->max_epid);

	for (epidx = 0; epidx < (hw->max_epid); epidx++) {
		if (epidx == hw->my_epid)
			continue;

		buf_pair = &hw->ep_shm_info[epidx];

		fjes_hw_setup_epbuf(&buf_pair->tx, netdev->dev_addr,
				    netdev->mtu);

		if (fjes_hw_epid_is_same_zone(hw, epidx)) {
			mutex_lock(&hw->hw_info.lock);
			result =
			fjes_hw_register_buff_addr(hw, epidx, buf_pair);
			mutex_unlock(&hw->hw_info.lock);

			switch (result) {
			case 0:
				break;
			case -ENOMSG:
			case -EBUSY:
			default:
				adapter->force_reset = true;
				return result;
			}
		}
	}

	return 0;
}

static void fjes_free_resources(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct fjes_device_command_param param;
	struct ep_share_mem_info *buf_pair;
	struct fjes_hw *hw = &adapter->hw;
	bool reset_flag = false;
	int result;
	int epidx;

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		mutex_lock(&hw->hw_info.lock);
		result = fjes_hw_unregister_buff_addr(hw, epidx);
		mutex_unlock(&hw->hw_info.lock);

		if (result)
			reset_flag = true;

		buf_pair = &hw->ep_shm_info[epidx];

		fjes_hw_setup_epbuf(&buf_pair->tx,
				    netdev->dev_addr, netdev->mtu);

		clear_bit(epidx, &hw->txrx_stop_req_bit);
	}

	if (reset_flag || adapter->force_reset) {
		result = fjes_hw_reset(hw);

		adapter->force_reset = false;

		if (result)
			adapter->open_guard = true;

		hw->hw_info.buffer_share_bit = 0;

		memset((void *)&param, 0, sizeof(param));

		param.req_len = hw->hw_info.req_buf_size;
		param.req_start = __pa(hw->hw_info.req_buf);
		param.res_len = hw->hw_info.res_buf_size;
		param.res_start = __pa(hw->hw_info.res_buf);
		param.share_start = __pa(hw->hw_info.share->ep_status);

		fjes_hw_init_command_registers(hw, &param);
	}
}

static irqreturn_t fjes_intr(int irq, void *data)
{
	struct fjes_adapter *adapter = data;
	struct fjes_hw *hw = &adapter->hw;
	irqreturn_t ret;
	u32 icr;

	icr = fjes_hw_capture_interrupt_status(hw);

	if (icr & REG_IS_MASK_IS_ASSERT)
		ret = IRQ_HANDLED;
	else
		ret = IRQ_NONE;

	return ret;
}

/* fjes_probe - Device Initialization Routine */
static int fjes_probe(struct platform_device *plat_dev)
{
	struct fjes_adapter *adapter;
	struct net_device *netdev;
	struct resource *res;
	struct fjes_hw *hw;
	int err;

	err = -ENOMEM;
	netdev = alloc_netdev_mq(sizeof(struct fjes_adapter), "es%d",
				 NET_NAME_UNKNOWN, fjes_netdev_setup,
				 FJES_MAX_QUEUES);

	if (!netdev)
		goto err_out;

	SET_NETDEV_DEV(netdev, &plat_dev->dev);

	dev_set_drvdata(&plat_dev->dev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->plat_dev = plat_dev;
	hw = &adapter->hw;
	hw->back = adapter;

	/* setup the private structure */
	err = fjes_sw_init(adapter);
	if (err)
		goto err_free_netdev;

	adapter->force_reset = false;
	adapter->open_guard = false;

	res = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
	hw->hw_res.start = res->start;
	hw->hw_res.size = res->end - res->start + 1;
	hw->hw_res.irq = platform_get_irq(plat_dev, 0);
	err = fjes_hw_init(&adapter->hw);
	if (err)
		goto err_free_netdev;

	/* setup MAC address (02:00:00:00:00:[epid])*/
	netdev->dev_addr[0] = 2;
	netdev->dev_addr[1] = 0;
	netdev->dev_addr[2] = 0;
	netdev->dev_addr[3] = 0;
	netdev->dev_addr[4] = 0;
	netdev->dev_addr[5] = hw->my_epid; /* EPID */

	err = register_netdev(netdev);
	if (err)
		goto err_hw_exit;

	netif_carrier_off(netdev);

	return 0;

err_hw_exit:
	fjes_hw_exit(&adapter->hw);
err_free_netdev:
	free_netdev(netdev);
err_out:
	return err;
}

/* fjes_remove - Device Removal Routine */
static int fjes_remove(struct platform_device *plat_dev)
{
	struct net_device *netdev = dev_get_drvdata(&plat_dev->dev);
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;

	unregister_netdev(netdev);

	fjes_hw_exit(hw);

	free_netdev(netdev);

	return 0;
}

static int fjes_sw_init(struct fjes_adapter *adapter)
{
	return 0;
}

/* fjes_netdev_setup - netdevice initialization routine */
static void fjes_netdev_setup(struct net_device *netdev)
{
	ether_setup(netdev);

	netdev->watchdog_timeo = FJES_TX_RETRY_INTERVAL;
	netdev->netdev_ops = &fjes_netdev_ops;
	netdev->mtu = fjes_support_mtu[0];
	netdev->flags |= IFF_BROADCAST;
	netdev->features |= NETIF_F_HW_CSUM | NETIF_F_HW_VLAN_CTAG_FILTER;
}

/* fjes_init_module - Driver Registration Routine */
static int __init fjes_init_module(void)
{
	int result;

	pr_info("%s - version %s - %s\n",
		fjes_driver_string, fjes_driver_version, fjes_copyright);

	result = platform_driver_register(&fjes_driver);
	if (result < 0)
		return result;

	result = acpi_bus_register_driver(&fjes_acpi_driver);
	if (result < 0)
		goto fail_acpi_driver;

	return 0;

fail_acpi_driver:
	platform_driver_unregister(&fjes_driver);
	return result;
}

module_init(fjes_init_module);

/* fjes_exit_module - Driver Exit Cleanup Routine */
static void __exit fjes_exit_module(void)
{
	acpi_bus_unregister_driver(&fjes_acpi_driver);
	platform_driver_unregister(&fjes_driver);
}

module_exit(fjes_exit_module);
