/**
 ******************************************************************************
 *
 * @file rwnx_v7.c - Support for v7 platform
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#include "rwnx_v7.h"
#include "rwnx_defs.h"
#include "rwnx_irqs.h"
#include "reg_access.h"
#include "hal_desc.h"

struct rwnx_v7
{
    u8 *pci_bar0_vaddr;
    u8 *pci_bar1_vaddr;
};

static int rwnx_v7_platform_enable(struct rwnx_hw *rwnx_hw)
{
    int ret;

    /* sched_setscheduler on ONESHOT threaded irq handler for BCNs ? */
    ret = request_irq(rwnx_hw->plat->pci_dev->irq, rwnx_irq_hdlr, 0,
                      "rwnx", rwnx_hw);
    return ret;
}

static int rwnx_v7_platform_disable(struct rwnx_hw *rwnx_hw)
{
    free_irq(rwnx_hw->plat->pci_dev->irq, rwnx_hw);
    return 0;
}

static void rwnx_v7_platform_deinit(struct rwnx_plat *rwnx_plat)
{
    #ifdef CONFIG_PCI
    struct rwnx_v7 *rwnx_v7 = (struct rwnx_v7 *)rwnx_plat->priv;

    pci_disable_device(rwnx_plat->pci_dev);
    iounmap(rwnx_v7->pci_bar0_vaddr);
    iounmap(rwnx_v7->pci_bar1_vaddr);
    pci_release_regions(rwnx_plat->pci_dev);
    pci_clear_master(rwnx_plat->pci_dev);
    pci_disable_msi(rwnx_plat->pci_dev);
    #endif
    kfree(rwnx_plat);
}

static u8* rwnx_v7_get_address(struct rwnx_plat *rwnx_plat, int addr_name,
                               unsigned int offset)
{
    struct rwnx_v7 *rwnx_v7 = (struct rwnx_v7 *)rwnx_plat->priv;

    if (WARN(addr_name >= RWNX_ADDR_MAX, "Invalid address %d", addr_name))
        return NULL;

    if (addr_name == RWNX_ADDR_CPU)
        return rwnx_v7->pci_bar0_vaddr + offset;
    else
        return rwnx_v7->pci_bar1_vaddr + offset;
}

static void rwnx_v7_ack_irq(struct rwnx_plat *rwnx_plat)
{

}

static const u32 rwnx_v7_config_reg[] = {
    NXMAC_DEBUG_PORT_SEL_ADDR,
    SYSCTRL_DIAG_CONF_ADDR,
    SYSCTRL_PHYDIAG_CONF_ADDR,
    SYSCTRL_RIUDIAG_CONF_ADDR,
    RF_V7_DIAGPORT_CONF1_ADDR,
};

static const u32 rwnx_v7_he_config_reg[] = {
    SYSCTRL_DIAG_CONF0,
    SYSCTRL_DIAG_CONF1,
    SYSCTRL_DIAG_CONF2,
    SYSCTRL_DIAG_CONF3,
};

static int rwnx_v7_get_config_reg(struct rwnx_plat *rwnx_plat, const u32 **list)
{
    u32 fpga_sign;

    if (!list)
        return 0;

    fpga_sign = RWNX_REG_READ(rwnx_plat, RWNX_ADDR_SYSTEM, SYSCTRL_SIGNATURE_ADDR);
    if (__FPGA_TYPE(fpga_sign) == 0xc0ca) {
        *list = rwnx_v7_he_config_reg;
        return ARRAY_SIZE(rwnx_v7_he_config_reg);
    } else {
        *list = rwnx_v7_config_reg;
        return ARRAY_SIZE(rwnx_v7_config_reg);
    }
}


/**
 * rwnx_v7_platform_init - Initialize the DINI platform
 *
 * @pci_dev PCI device
 * @rwnx_plat Pointer on struct rwnx_stat * to be populated
 *
 * @return 0 on success, < 0 otherwise
 *
 * Allocate and initialize a rwnx_plat structure for the dini platform.
 */
int rwnx_v7_platform_init(struct pci_dev *pci_dev, struct rwnx_plat **rwnx_plat)
{
    struct rwnx_v7 *rwnx_v7;
    u16 pci_cmd;
    int ret = 0;

    *rwnx_plat = kzalloc(sizeof(struct rwnx_plat) + sizeof(struct rwnx_v7),
                        GFP_KERNEL);
    if (!*rwnx_plat)
        return -ENOMEM;

    rwnx_v7 = (struct rwnx_v7 *)(*rwnx_plat)->priv;

    /* Hotplug fixups */
    pci_read_config_word(pci_dev, PCI_COMMAND, &pci_cmd);
    pci_cmd |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
    pci_write_config_word(pci_dev, PCI_COMMAND, pci_cmd);
    pci_write_config_byte(pci_dev, PCI_CACHE_LINE_SIZE, L1_CACHE_BYTES >> 2);

    if ((ret = pci_enable_device(pci_dev))) {
        dev_err(&(pci_dev->dev), "pci_enable_device failed\n");
        goto out_enable;
    }

    pci_set_master(pci_dev);

    if ((ret = pci_request_regions(pci_dev, KBUILD_MODNAME))) {
        dev_err(&(pci_dev->dev), "pci_request_regions failed\n");
        goto out_request;
    }

    #ifdef CONFIG_PCI
    if (pci_enable_msi(pci_dev))
    {
        dev_err(&(pci_dev->dev), "pci_enable_msi failed\n");
        goto out_msi;

    }
    #endif

    if (!(rwnx_v7->pci_bar0_vaddr = (u8 *)pci_ioremap_bar(pci_dev, 0))) {
        dev_err(&(pci_dev->dev), "pci_ioremap_bar(%d) failed\n", 0);
        ret = -ENOMEM;
        goto out_bar0;
    }
    if (!(rwnx_v7->pci_bar1_vaddr = (u8 *)pci_ioremap_bar(pci_dev, 1))) {
        dev_err(&(pci_dev->dev), "pci_ioremap_bar(%d) failed\n", 1);
        ret = -ENOMEM;
        goto out_bar1;
    }

    (*rwnx_plat)->enable = rwnx_v7_platform_enable;
    (*rwnx_plat)->disable = rwnx_v7_platform_disable;
    (*rwnx_plat)->deinit = rwnx_v7_platform_deinit;
    (*rwnx_plat)->get_address = rwnx_v7_get_address;
    (*rwnx_plat)->ack_irq = rwnx_v7_ack_irq;
    (*rwnx_plat)->get_config_reg = rwnx_v7_get_config_reg;

    return 0;

  out_bar1:
    iounmap(rwnx_v7->pci_bar0_vaddr);
  out_bar0:
#ifdef CONFIG_PCI
    pci_disable_msi(pci_dev);
  out_msi:
#endif
    pci_release_regions(pci_dev);
  out_request:
#ifdef CONFIG_PCI
    pci_clear_master(pci_dev);
#endif
    pci_disable_device(pci_dev);
  out_enable:
    kfree(*rwnx_plat);
    return ret;
}
