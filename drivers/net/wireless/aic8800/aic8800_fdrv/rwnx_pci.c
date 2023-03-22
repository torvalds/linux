/**
 ******************************************************************************
 *
 * @file rwnx_pci.c
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#include <linux/pci.h>
#include <linux/module.h>

#include "rwnx_defs.h"
#include "rwnx_dini.h"
#include "rwnx_v7.h"

#define PCI_VENDOR_ID_DINIGROUP              0x17DF
#define PCI_DEVICE_ID_DINIGROUP_DNV6_F2PCIE  0x1907

#define PCI_DEVICE_ID_XILINX_CEVA_VIRTEX7    0x7011

static const struct pci_device_id rwnx_pci_ids[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_DINIGROUP, PCI_DEVICE_ID_DINIGROUP_DNV6_F2PCIE)},
    {PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_XILINX_CEVA_VIRTEX7)},
    {0,}
};


/* Uncomment this for depmod to create module alias */
/* We don't want this on development platform */
//MODULE_DEVICE_TABLE(pci, rwnx_pci_ids);

static int rwnx_pci_probe(struct pci_dev *pci_dev,
                          const struct pci_device_id *pci_id)
{
    struct rwnx_plat *rwnx_plat = NULL;
    void *drvdata;
    int ret = -ENODEV;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (pci_id->vendor == PCI_VENDOR_ID_DINIGROUP) {
        ret = rwnx_dini_platform_init(pci_dev, &rwnx_plat);
    } else if (pci_id->vendor == PCI_VENDOR_ID_XILINX) {
        ret = rwnx_v7_platform_init(pci_dev, &rwnx_plat);
    }

    if (ret)
        return ret;

    rwnx_plat->pci_dev = pci_dev;

    ret = rwnx_platform_init(rwnx_plat, &drvdata);
    pci_set_drvdata(pci_dev, drvdata);

    if (ret)
        rwnx_plat->deinit(rwnx_plat);

    return ret;
}

static void rwnx_pci_remove(struct pci_dev *pci_dev)
{
    struct rwnx_hw *rwnx_hw;
    struct rwnx_plat *rwnx_plat;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    rwnx_hw = pci_get_drvdata(pci_dev);
    rwnx_plat = rwnx_hw->plat;

    rwnx_platform_deinit(rwnx_hw);
    rwnx_plat->deinit(rwnx_plat);

    pci_set_drvdata(pci_dev, NULL);
}

static struct pci_driver rwnx_pci_drv = {
    .name     = KBUILD_MODNAME,
    .id_table = rwnx_pci_ids,
    .probe    = rwnx_pci_probe,
    .remove   = rwnx_pci_remove
};

int rwnx_pci_register_drv(void)
{
    return pci_register_driver(&rwnx_pci_drv);
}

void rwnx_pci_unregister_drv(void)
{
    pci_unregister_driver(&rwnx_pci_drv);
}

