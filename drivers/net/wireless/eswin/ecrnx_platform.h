/**
 ******************************************************************************
 *
 * @file ecrnx_platorm.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_PLAT_H_
#define _ECRNX_PLAT_H_

#include <linux/pci.h>

#define ECRNX_CONFIG_FW_NAME             "wifi_ecr6600u.cfg"
#define ECRNX_PHY_CONFIG_TRD_NAME        "ecrnx_trident.ini"
#define ECRNX_PHY_CONFIG_KARST_NAME      "ecrnx_karst.ini"
#define ECRNX_AGC_FW_NAME                "agcram.bin"
#define ECRNX_LDPC_RAM_NAME              "ldpcram.bin"
#define ECRNX_CATAXIA_FW_NAME            "cataxia.fw"
#ifdef CONFIG_ECRNX_SOFTMAC
#define ECRNX_MAC_FW_BASE_NAME           "lmacfw"
#elif defined CONFIG_ECRNX_FULLMAC
#define ECRNX_MAC_FW_BASE_NAME           "fmacfw"
#elif defined CONFIG_ECRNX_FHOST
#define ECRNX_MAC_FW_BASE_NAME           "fhostfw"
#endif /* CONFIG_ECRNX_SOFTMAC */

#ifdef CONFIG_ECRNX_TL4
#define ECRNX_MAC_FW_NAME ECRNX_MAC_FW_BASE_NAME".hex"
#else
#define ECRNX_MAC_FW_NAME  ECRNX_MAC_FW_BASE_NAME".ihex"
#define ECRNX_MAC_FW_NAME2 ECRNX_MAC_FW_BASE_NAME".bin"
#endif

#define ECRNX_FCU_FW_NAME                "fcuram.bin"

/**
 * Type of memory to access (cf ecrnx_plat.get_address)
 *
 * @ECRNX_ADDR_CPU To access memory of the embedded CPU
 * @ECRNX_ADDR_SYSTEM To access memory/registers of one subsystem of the
 * embedded system
 *
 */
enum ecrnx_platform_addr {
    ECRNX_ADDR_CPU,
    ECRNX_ADDR_SYSTEM,
    ECRNX_ADDR_MAX,
};

struct ecrnx_hw;

/**
 * struct ecrnx_plat - Operation pointers for ECRNX PCI platform
 *
 * @pci_dev: pointer to pci dev
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *          ipc started)
 * @enable: Configure communication with the fw (i.e. configure the transfers
 *         enable and register interrupt)
 * @disable: Stop communication with the fw
 * @deinit: Free all ressources allocated for the embedded platform
 * @get_address: Return the virtual address to access the requested address on
 *              the platform.
 * @ack_irq: Acknowledge the irq at link level.
 * @get_config_reg: Return the list (size + pointer) of registers to restore in
 * order to reload the platform while keeping the current configuration.
 *
 * @priv Private data for the link driver
 */
struct ecrnx_plat {
    struct pci_dev *pci_dev;
    bool enabled;

    int (*enable)(struct ecrnx_hw *ecrnx_hw);
    int (*disable)(struct ecrnx_hw *ecrnx_hw);
    void (*deinit)(struct ecrnx_plat *ecrnx_plat);
    u8* (*get_address)(struct ecrnx_plat *ecrnx_plat, int addr_name,
                       unsigned int offset);
    void (*ack_irq)(struct ecrnx_plat *ecrnx_plat);
    int (*get_config_reg)(struct ecrnx_plat *ecrnx_plat, const u32 **list);

    u8 priv[0] __aligned(sizeof(void *));
};

#define ECRNX_ADDR(plat, base, offset)           \
    plat->get_address(plat, base, offset)

#define ECRNX_REG_READ(plat, base, offset)               \
    readl(plat->get_address(plat, base, offset))

#define ECRNX_REG_WRITE(val, plat, base, offset)         \
    writel(val, plat->get_address(plat, base, offset))

#ifdef CONFIG_ECRNX_ESWIN
int ecrnx_platform_init(void *ecrnx_plat, void **platform_data);
#else
int ecrnx_platform_init(struct ecrnx_plat *ecrnx_plat, void **platform_data);
#endif
void ecrnx_platform_deinit(struct ecrnx_hw *ecrnx_hw);

int ecrnx_platform_on(struct ecrnx_hw *ecrnx_hw, void *config);
void ecrnx_platform_off(struct ecrnx_hw *ecrnx_hw, void **config);

int ecrnx_platform_register_drv(void);
void ecrnx_platform_unregister_drv(void);

#ifndef CONFIG_ECRNX_ESWIN
static inline struct device *ecrnx_platform_get_dev(struct ecrnx_plat *ecrnx_plat)
{
    return &(ecrnx_plat->pci_dev->dev);
}

static inline unsigned int ecrnx_platform_get_irq(struct ecrnx_plat *ecrnx_plat)
{
    return ecrnx_plat->pci_dev->irq;
}
#else


#ifdef CONFIG_ECRNX_ESWIN_SDIO
extern struct device *eswin_sdio_get_dev(void *plat);
static inline struct device *ecrnx_platform_get_dev(void *ecrnx_plat)
{
    return eswin_sdio_get_dev(ecrnx_plat);
}
#endif


#ifdef CONFIG_ECRNX_ESWIN_USB
struct device *eswin_usb_get_dev(void *plat);
static inline struct device *ecrnx_platform_get_dev(void *ecrnx_plat)
{
    return eswin_usb_get_dev(ecrnx_plat);
}
#endif


#endif
#endif /* _ECRNX_PLAT_H_ */
