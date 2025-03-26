// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/if_ether.h>
#include "fnic.h"

static struct fnic_pcie_device fnic_pcie_device_table[] = {
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno", PCI_SUBDEVICE_ID_CISCO_VASONA,
	 "VIC 1280"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno", PCI_SUBDEVICE_ID_CISCO_COTATI,
	 "VIC 1240"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno",
	 PCI_SUBDEVICE_ID_CISCO_LEXINGTON, "VIC 1225"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno", PCI_SUBDEVICE_ID_CISCO_ICEHOUSE,
	 "VIC 1285"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno",
	 PCI_SUBDEVICE_ID_CISCO_KIRKWOODLAKE, "VIC 1225T"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno",
	 PCI_SUBDEVICE_ID_CISCO_SUSANVILLE, "VIC 1227"},
	{PCI_DEVICE_ID_CISCO_SERENO, "Sereno", PCI_SUBDEVICE_ID_CISCO_TORRANCE,
	 "VIC 1227T"},

	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_CALISTOGA,
	 "VIC 1340"},
	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_MOUNTAINVIEW,
	 "VIC 1380"},
	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN,
	 "C3260-SIOC"},
	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_CLEARLAKE,
	 "VIC 1385"},
	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN2,
	 "C3260-SIOC"},
	{PCI_DEVICE_ID_CISCO_CRUZ, "Cruz", PCI_SUBDEVICE_ID_CISCO_CLAREMONT,
	 "VIC 1387"},

	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BRADBURY,
	 "VIC 1457"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_BRENTWOOD, "VIC 1455"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_BURLINGAME, "VIC 1487"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BAYSIDE,
	 "VIC 1485"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_BAKERSFIELD, "VIC 1440"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_BOONVILLE, "VIC 1480"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BENICIA,
	 "VIC 1495"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BEAUMONT,
	 "VIC 1497"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BRISBANE,
	 "VIC 1467"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega", PCI_SUBDEVICE_ID_CISCO_BENTON,
	 "VIC 1477"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_TWIN_RIVER, "VIC 14425"},
	{PCI_DEVICE_ID_CISCO_BODEGA, "Bodega",
	 PCI_SUBDEVICE_ID_CISCO_TWIN_PEAK, "VIC 14825"},

	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_BERN,
	 "VIC 15420"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",
	 PCI_SUBDEVICE_ID_CISCO_STOCKHOLM, "VIC 15428"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_KRAKOW,
	 "VIC 15411"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",
	 PCI_SUBDEVICE_ID_CISCO_LUCERNE, "VIC 15231"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_TURKU,
	 "VIC 15238"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_GENEVA,
	 "VIC 15422"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",
	 PCI_SUBDEVICE_ID_CISCO_HELSINKI, "VIC 15235"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",
	 PCI_SUBDEVICE_ID_CISCO_GOTHENBURG, "VIC 15425"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",
	 PCI_SUBDEVICE_ID_CISCO_TURKU_PLUS, "VIC 15237"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_ZURICH,
	 "VIC 15230"},
	{PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly", PCI_SUBDEVICE_ID_CISCO_RIGA,
	 "VIC 15427"},

	{0,}
};

int fnic_get_desc_by_devid(struct pci_dev *pdev, char **desc,
						   char **subsys_desc)
{
	unsigned short device = PCI_DEVICE_ID_CISCO_VIC_FC;
	int max = ARRAY_SIZE(fnic_pcie_device_table);
	struct fnic_pcie_device *t = fnic_pcie_device_table;
	int index = 0;

	if (pdev->device != device)
		return 1;

	while (t->device != 0) {
		if (memcmp
			((char *) &pdev->subsystem_device,
			 (char *) &t->subsystem_device, sizeof(short)) == 0)
			break;
		t++;
		index++;
	}

	if (index >= max - 1) {
		*desc = NULL;
		*subsys_desc = NULL;
		return 1;
	}

	*desc = fnic_pcie_device_table[index].desc;
	*subsys_desc = fnic_pcie_device_table[index].subsys_desc;
	return 0;
}
