/*
 * QLogic ISP2322 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation (www.qlogic.com)
 *
 * Released under GPL v2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "qla_def.h"

static char qla_driver_name[] = "qla2322";

extern unsigned char  fw2322ipx_version[];
extern unsigned char  fw2322ipx_version_str[];
extern unsigned short fw2322ipx_addr01;
extern unsigned short fw2322ipx_code01[];
extern unsigned short fw2322ipx_length01;
extern unsigned long rseqipx_code_addr01;
extern unsigned short rseqipx_code01[];
extern unsigned short rseqipx_code_length01;
extern unsigned long xseqipx_code_addr01;
extern unsigned short xseqipx_code01[];
extern unsigned short xseqipx_code_length01;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2322ipx_code01[0],
		.fwlen		= &fw2322ipx_length01,
		.fwstart	= &fw2322ipx_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &rseqipx_code01[0],
		.fwlen		= &rseqipx_code_length01,
		.lfwstart	= &rseqipx_code_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &xseqipx_code01[0],
		.fwlen		= &xseqipx_code_length01,
		.lfwstart	= &xseqipx_code_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl[] = {
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2322",
		.fw_info	= qla_fw_tbl,
	},
};

static struct pci_device_id qla2322_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2322,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[0],
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla2322_pci_tbl);

static int __devinit
qla2322_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla2322_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla2322_pci_driver = {
	.name		= "qla2322",
	.id_table	= qla2322_pci_tbl,
	.probe		= qla2322_probe_one,
	.remove		= __devexit_p(qla2322_remove_one),
};

static int __init
qla2322_init(void)
{
	return pci_module_init(&qla2322_pci_driver);
}

static void __exit
qla2322_exit(void)
{
	pci_unregister_driver(&qla2322_pci_driver);
}

module_init(qla2322_init);
module_exit(qla2322_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP2322 FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
