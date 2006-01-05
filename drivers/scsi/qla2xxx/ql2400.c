/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "qla_def.h"

static char qla_driver_name[] = "qla2400";

extern uint32_t fw2400_version_str[];
extern uint32_t fw2400_addr01;
extern uint32_t fw2400_code01[];
extern uint32_t fw2400_length01;
extern uint32_t fw2400_addr02;
extern uint32_t fw2400_code02[];
extern uint32_t fw2400_length02;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= (unsigned short *)&fw2400_code01[0],
		.fwlen		= (unsigned short *)&fw2400_length01,
		.lfwstart	= (unsigned long *)&fw2400_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= (unsigned short *)&fw2400_code02[0],
		.fwlen		= (unsigned short *)&fw2400_length02,
		.lfwstart	= (unsigned long *)&fw2400_addr02,
	},
	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl[] = {
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2422",
		.fw_info	= qla_fw_tbl,
		.fw_fname	= "ql2400_fw.bin",
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2432",
		.fw_info	= qla_fw_tbl,
		.fw_fname	= "ql2400_fw.bin",
	},
};

static struct pci_device_id qla24xx_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2422,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[0],
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2432,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[1],
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla24xx_pci_tbl);

static int __devinit
qla24xx_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla24xx_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla24xx_pci_driver = {
	.name		= "qla2400",
	.id_table	= qla24xx_pci_tbl,
	.probe		= qla24xx_probe_one,
	.remove		= __devexit_p(qla24xx_remove_one),
};

static int __init
qla24xx_init(void)
{
	return pci_module_init(&qla24xx_pci_driver);
}

static void __exit
qla24xx_exit(void)
{
	pci_unregister_driver(&qla24xx_pci_driver);
}

module_init(qla24xx_init);
module_exit(qla24xx_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP24xx FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
