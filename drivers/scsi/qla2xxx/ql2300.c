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

static char qla_driver_name[] = "qla2300";

extern unsigned char  fw2300ipx_version[];
extern unsigned char  fw2300ipx_version_str[];
extern unsigned short fw2300ipx_addr01;
extern unsigned short fw2300ipx_code01[];
extern unsigned short fw2300ipx_length01;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2300ipx_code01[0],
		.fwlen		= &fw2300ipx_length01,
		.fwstart	= &fw2300ipx_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl[] = {
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2300",
		.fw_info	= qla_fw_tbl,
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2312",
		.fw_info	= qla_fw_tbl,
	},
};

static struct pci_device_id qla2300_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2300,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[0],
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2312,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[1],
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla2300_pci_tbl);

static int __devinit
qla2300_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla2300_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla2300_pci_driver = {
	.name		= "qla2300",
	.id_table	= qla2300_pci_tbl,
	.probe		= qla2300_probe_one,
	.remove		= __devexit_p(qla2300_remove_one),
};

static int __init
qla2300_init(void)
{
	return pci_module_init(&qla2300_pci_driver);
}

static void __exit
qla2300_exit(void)
{
	pci_unregister_driver(&qla2300_pci_driver);
}

module_init(qla2300_init);
module_exit(qla2300_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP23xx FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
