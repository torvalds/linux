/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (C)  2003 Christoph Hellwig.
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "qla_def.h"

static char qla_driver_name[] = "qla2200";

extern unsigned char  fw2200tp_version[];
extern unsigned char  fw2200tp_version_str[];
extern unsigned short fw2200tp_addr01;
extern unsigned short fw2200tp_code01[];
extern unsigned short fw2200tp_length01;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2200tp_code01[0],
		.fwlen		= &fw2200tp_length01,
		.fwstart	= &fw2200tp_addr01,
	},

	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl = {
	.drv_name	= qla_driver_name,

	.isp_name	= "ISP2200",
	.fw_info	= qla_fw_tbl,
};

static struct pci_device_id qla2200_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2200,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl,
	},

	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla2200_pci_tbl);

static int __devinit
qla2200_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla2200_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla2200_pci_driver = {
	.name		= "qla2200",
	.id_table	= qla2200_pci_tbl,
	.probe		= qla2200_probe_one,
	.remove		= __devexit_p(qla2200_remove_one),
};

static int __init
qla2200_init(void)
{
	return pci_module_init(&qla2200_pci_driver);
}

static void __exit
qla2200_exit(void)
{
	pci_unregister_driver(&qla2200_pci_driver);
}

module_init(qla2200_init);
module_exit(qla2200_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP22xx FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
