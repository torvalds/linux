/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-2017 Broadcom
 */

#ifndef _USB_BRCM_COMMON_INIT_H
#define _USB_BRCM_COMMON_INIT_H

#include <linux/regmap.h>

#define USB_CTLR_MODE_HOST 0
#define USB_CTLR_MODE_DEVICE 1
#define USB_CTLR_MODE_DRD 2
#define USB_CTLR_MODE_TYPEC_PD 3

enum brcmusb_reg_sel {
	BRCM_REGS_CTRL = 0,
	BRCM_REGS_XHCI_EC,
	BRCM_REGS_XHCI_GBL,
	BRCM_REGS_USB_PHY,
	BRCM_REGS_USB_MDIO,
	BRCM_REGS_BDC_EC,
	BRCM_REGS_MAX
};

#define USB_CTRL_REG(base, reg)	((void __iomem *)base + USB_CTRL_##reg)
#define USB_XHCI_EC_REG(base, reg) ((void __iomem *)base + USB_XHCI_EC_##reg)
#define USB_CTRL_MASK(reg, field) \
	USB_CTRL_##reg##_##field##_MASK
#define USB_CTRL_SET(base, reg, field)	\
	brcm_usb_ctrl_set(USB_CTRL_REG(base, reg),	\
			  USB_CTRL_##reg##_##field##_MASK)
#define USB_CTRL_UNSET(base, reg, field)	\
	brcm_usb_ctrl_unset(USB_CTRL_REG(base, reg),		\
			    USB_CTRL_##reg##_##field##_MASK)

struct  brcm_usb_init_params;

struct brcm_usb_init_ops {
	void (*init_ipp)(struct brcm_usb_init_params *params);
	void (*init_common)(struct brcm_usb_init_params *params);
	void (*init_eohci)(struct brcm_usb_init_params *params);
	void (*init_xhci)(struct brcm_usb_init_params *params);
	void (*uninit_common)(struct brcm_usb_init_params *params);
	void (*uninit_eohci)(struct brcm_usb_init_params *params);
	void (*uninit_xhci)(struct brcm_usb_init_params *params);
	int  (*get_dual_select)(struct brcm_usb_init_params *params);
	void (*set_dual_select)(struct brcm_usb_init_params *params, int mode);
};

struct  brcm_usb_init_params {
	void __iomem *regs[BRCM_REGS_MAX];
	int ioc;
	int ipp;
	int mode;
	u32 family_id;
	u32 product_id;
	int selected_family;
	const char *family_name;
	const u32 *usb_reg_bits_map;
	const struct brcm_usb_init_ops *ops;
	struct regmap *syscon_piarbctl;
	bool wake_enabled;
	bool suspend_with_clocks;
};

void brcm_usb_dvr_init_4908(struct brcm_usb_init_params *params);
void brcm_usb_dvr_init_7445(struct brcm_usb_init_params *params);
void brcm_usb_dvr_init_7216(struct brcm_usb_init_params *params);
void brcm_usb_dvr_init_7211b0(struct brcm_usb_init_params *params);

static inline u32 brcm_usb_readl(void __iomem *addr)
{
	/*
	 * MIPS endianness is configured by boot strap, which also reverses all
	 * bus endianness (i.e., big-endian CPU + big endian bus ==> native
	 * endian I/O).
	 *
	 * Other architectures (e.g., ARM) either do not support big endian, or
	 * else leave I/O in little endian mode.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(addr);
	else
		return readl_relaxed(addr);
}

static inline void brcm_usb_writel(u32 val, void __iomem *addr)
{
	/* See brcmnand_readl() comments */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, addr);
	else
		writel_relaxed(val, addr);
}

static inline void brcm_usb_ctrl_unset(void __iomem *reg, u32 mask)
{
	brcm_usb_writel(brcm_usb_readl(reg) & ~(mask), reg);
};

static inline void brcm_usb_ctrl_set(void __iomem *reg, u32 mask)
{
	brcm_usb_writel(brcm_usb_readl(reg) | (mask), reg);
};

static inline void brcm_usb_init_ipp(struct brcm_usb_init_params *ini)
{
	if (ini->ops->init_ipp)
		ini->ops->init_ipp(ini);
}

static inline void brcm_usb_init_common(struct brcm_usb_init_params *ini)
{
	if (ini->ops->init_common)
		ini->ops->init_common(ini);
}

static inline void brcm_usb_init_eohci(struct brcm_usb_init_params *ini)
{
	if (ini->ops->init_eohci)
		ini->ops->init_eohci(ini);
}

static inline void brcm_usb_init_xhci(struct brcm_usb_init_params *ini)
{
	if (ini->ops->init_xhci)
		ini->ops->init_xhci(ini);
}

static inline void brcm_usb_uninit_common(struct brcm_usb_init_params *ini)
{
	if (ini->ops->uninit_common)
		ini->ops->uninit_common(ini);
}

static inline void brcm_usb_uninit_eohci(struct brcm_usb_init_params *ini)
{
	if (ini->ops->uninit_eohci)
		ini->ops->uninit_eohci(ini);
}

static inline void brcm_usb_uninit_xhci(struct brcm_usb_init_params *ini)
{
	if (ini->ops->uninit_xhci)
		ini->ops->uninit_xhci(ini);
}

static inline int brcm_usb_get_dual_select(struct brcm_usb_init_params *ini)
{
	if (ini->ops->get_dual_select)
		return ini->ops->get_dual_select(ini);
	return 0;
}

static inline void brcm_usb_set_dual_select(struct brcm_usb_init_params *ini,
	int mode)
{
	if (ini->ops->set_dual_select)
		ini->ops->set_dual_select(ini, mode);
}

#endif /* _USB_BRCM_COMMON_INIT_H */
