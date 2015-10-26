/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*
* This software is available under the terms of the GNU General Public License
* (GPL) Version 2, available from the file COPYING in the main directory of
* this source tree.
*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/io.h>
#include <linux/netdev_features.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <net/vxlan.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/pkt_sched.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/random.h>
#include <net/ip6_checksum.h>
#include <linux/bitops.h>

#include "qede.h"

static const char version[] = "QLogic QL4xxx 40G/100G Ethernet Driver qede "
			      DRV_MODULE_VERSION "\n";

MODULE_DESCRIPTION("QLogic 40G/100G Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static uint debug;
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static const struct qed_eth_ops *qed_ops;

#define CHIP_NUM_57980S_40		0x1634
#define CHIP_NUM_57980S_10		0x1635
#define CHIP_NUM_57980S_MF		0x1636
#define CHIP_NUM_57980S_100		0x1644
#define CHIP_NUM_57980S_50		0x1654
#define CHIP_NUM_57980S_25		0x1656

#ifndef PCI_DEVICE_ID_NX2_57980E
#define PCI_DEVICE_ID_57980S_40		CHIP_NUM_57980S_40
#define PCI_DEVICE_ID_57980S_10		CHIP_NUM_57980S_10
#define PCI_DEVICE_ID_57980S_MF		CHIP_NUM_57980S_MF
#define PCI_DEVICE_ID_57980S_100	CHIP_NUM_57980S_100
#define PCI_DEVICE_ID_57980S_50		CHIP_NUM_57980S_50
#define PCI_DEVICE_ID_57980S_25		CHIP_NUM_57980S_25
#endif

static const struct pci_device_id qede_pci_tbl[] = {
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_40), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_10), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_MF), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_100), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_50), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_25), 0 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, qede_pci_tbl);

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id);

#define TX_TIMEOUT		(5 * HZ)

static void qede_remove(struct pci_dev *pdev);

static struct pci_driver qede_pci_driver = {
	.name = "qede",
	.id_table = qede_pci_tbl,
	.probe = qede_probe,
	.remove = qede_remove,
};

static
int __init qede_init(void)
{
	int ret;
	u32 qed_ver;

	pr_notice("qede_init: %s\n", version);

	qed_ver = qed_get_protocol_version(QED_PROTOCOL_ETH);
	if (qed_ver !=  QEDE_ETH_INTERFACE_VERSION) {
		pr_notice("Version mismatch [%08x != %08x]\n",
			  qed_ver,
			  QEDE_ETH_INTERFACE_VERSION);
		return -EINVAL;
	}

	qed_ops = qed_get_eth_ops(QEDE_ETH_INTERFACE_VERSION);
	if (!qed_ops) {
		pr_notice("Failed to get qed ethtool operations\n");
		return -EINVAL;
	}

	ret = pci_register_driver(&qede_pci_driver);
	if (ret) {
		pr_notice("Failed to register driver\n");
		qed_put_eth_ops();
		return -EINVAL;
	}

	return 0;
}

static void __exit qede_cleanup(void)
{
	pr_notice("qede_cleanup called\n");

	pci_unregister_driver(&qede_pci_driver);
	qed_put_eth_ops();
}

module_init(qede_init);
module_exit(qede_cleanup);

/* -------------------------------------------------------------------------
 * START OF PROBE / REMOVE
 * -------------------------------------------------------------------------
 */

static struct qede_dev *qede_alloc_etherdev(struct qed_dev *cdev,
					    struct pci_dev *pdev,
					    struct qed_dev_eth_info *info,
					    u32 dp_module,
					    u8 dp_level)
{
	struct net_device *ndev;
	struct qede_dev *edev;

	ndev = alloc_etherdev_mqs(sizeof(*edev),
				  info->num_queues,
				  info->num_queues);
	if (!ndev) {
		pr_err("etherdev allocation failed\n");
		return NULL;
	}

	edev = netdev_priv(ndev);
	edev->ndev = ndev;
	edev->cdev = cdev;
	edev->pdev = pdev;
	edev->dp_module = dp_module;
	edev->dp_level = dp_level;
	edev->ops = qed_ops;

	DP_INFO(edev, "Allocated netdev with 64 tx queues and 64 rx queues\n");

	SET_NETDEV_DEV(ndev, &pdev->dev);

	memcpy(&edev->dev_info, info, sizeof(*info));

	edev->num_tc = edev->dev_info.num_tc;

	return edev;
}

static void qede_init_ndev(struct qede_dev *edev)
{
	struct net_device *ndev = edev->ndev;
	struct pci_dev *pdev = edev->pdev;
	u32 hw_features;

	pci_set_drvdata(pdev, ndev);

	ndev->mem_start = edev->dev_info.common.pci_mem_start;
	ndev->base_addr = ndev->mem_start;
	ndev->mem_end = edev->dev_info.common.pci_mem_end;
	ndev->irq = edev->dev_info.common.pci_irq;

	ndev->watchdog_timeo = TX_TIMEOUT;

	/* user-changeble features */
	hw_features = NETIF_F_GRO | NETIF_F_SG |
		      NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		      NETIF_F_TSO | NETIF_F_TSO6;

	ndev->vlan_features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			      NETIF_F_HIGHDMA;
	ndev->features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HIGHDMA |
			 NETIF_F_HW_VLAN_CTAG_TX;

	ndev->hw_features = hw_features;

	/* Set network device HW mac */
	ether_addr_copy(edev->ndev->dev_addr, edev->dev_info.common.hw_mac);
}

/* This function converts from 32b param to two params of level and module
 * Input 32b decoding:
 * b31 - enable all NOTICE prints. NOTICE prints are for deviation from the
 * 'happy' flow, e.g. memory allocation failed.
 * b30 - enable all INFO prints. INFO prints are for major steps in the flow
 * and provide important parameters.
 * b29-b0 - per-module bitmap, where each bit enables VERBOSE prints of that
 * module. VERBOSE prints are for tracking the specific flow in low level.
 *
 * Notice that the level should be that of the lowest required logs.
 */
static void qede_config_debug(uint debug, u32 *p_dp_module, u8 *p_dp_level)
{
	*p_dp_level = QED_LEVEL_NOTICE;
	*p_dp_module = 0;

	if (debug & QED_LOG_VERBOSE_MASK) {
		*p_dp_level = QED_LEVEL_VERBOSE;
		*p_dp_module = (debug & 0x3FFFFFFF);
	} else if (debug & QED_LOG_INFO_MASK) {
		*p_dp_level = QED_LEVEL_INFO;
	} else if (debug & QED_LOG_NOTICE_MASK) {
		*p_dp_level = QED_LEVEL_NOTICE;
	}
}

static void qede_update_pf_params(struct qed_dev *cdev)
{
	struct qed_pf_params pf_params;

	/* 16 rx + 16 tx */
	memset(&pf_params, 0, sizeof(struct qed_pf_params));
	pf_params.eth_pf_params.num_cons = 32;
	qed_ops->common->update_pf_params(cdev, &pf_params);
}

enum qede_probe_mode {
	QEDE_PROBE_NORMAL,
};

static int __qede_probe(struct pci_dev *pdev, u32 dp_module, u8 dp_level,
			enum qede_probe_mode mode)
{
	struct qed_slowpath_params params;
	struct qed_dev_eth_info dev_info;
	struct qede_dev *edev;
	struct qed_dev *cdev;
	int rc;

	if (unlikely(dp_level & QED_LEVEL_INFO))
		pr_notice("Starting qede probe\n");

	cdev = qed_ops->common->probe(pdev, QED_PROTOCOL_ETH,
				      dp_module, dp_level);
	if (!cdev) {
		rc = -ENODEV;
		goto err0;
	}

	qede_update_pf_params(cdev);

	/* Start the Slowpath-process */
	memset(&params, 0, sizeof(struct qed_slowpath_params));
	params.int_mode = QED_INT_MODE_MSIX;
	params.drv_major = QEDE_MAJOR_VERSION;
	params.drv_minor = QEDE_MINOR_VERSION;
	params.drv_rev = QEDE_REVISION_VERSION;
	params.drv_eng = QEDE_ENGINEERING_VERSION;
	strlcpy(params.name, "qede LAN", QED_DRV_VER_STR_SIZE);
	rc = qed_ops->common->slowpath_start(cdev, &params);
	if (rc) {
		pr_notice("Cannot start slowpath\n");
		goto err1;
	}

	/* Learn information crucial for qede to progress */
	rc = qed_ops->fill_dev_info(cdev, &dev_info);
	if (rc)
		goto err2;

	edev = qede_alloc_etherdev(cdev, pdev, &dev_info, dp_module,
				   dp_level);
	if (!edev) {
		rc = -ENOMEM;
		goto err2;
	}

	qede_init_ndev(edev);

	edev->ops->common->set_id(cdev, edev->ndev->name, DRV_MODULE_VERSION);

	DP_INFO(edev, "Ending successfully qede probe\n");

	return 0;

err2:
	qed_ops->common->slowpath_stop(cdev);
err1:
	qed_ops->common->remove(cdev);
err0:
	return rc;
}

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	u32 dp_module = 0;
	u8 dp_level = 0;

	qede_config_debug(debug, &dp_module, &dp_level);

	return __qede_probe(pdev, dp_module, dp_level,
			    QEDE_PROBE_NORMAL);
}

enum qede_remove_mode {
	QEDE_REMOVE_NORMAL,
};

static void __qede_remove(struct pci_dev *pdev, enum qede_remove_mode mode)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct qede_dev *edev = netdev_priv(ndev);
	struct qed_dev *cdev = edev->cdev;

	DP_INFO(edev, "Starting qede_remove\n");

	edev->ops->common->set_power_state(cdev, PCI_D0);

	pci_set_drvdata(pdev, NULL);

	free_netdev(ndev);

	/* Use global ops since we've freed edev */
	qed_ops->common->slowpath_stop(cdev);
	qed_ops->common->remove(cdev);

	pr_notice("Ending successfully qede_remove\n");
}

static void qede_remove(struct pci_dev *pdev)
{
	__qede_remove(pdev, QEDE_REMOVE_NORMAL);
}
