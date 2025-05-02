/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 */

#ifndef _FTHD_HW_H
#define _FTHD_HW_H

#include <linux/pci.h>

/* Used after most PCI Link IO writes */
static inline void fthd_hw_pci_post(struct fthd_private *dev_priv)
{
	pci_write_config_dword(dev_priv->pdev, 0, 0x12345678);
}

#define FTHD_S2_REG_READ(offset) _FTHD_S2_REG_READ(dev_priv, (offset))
#define FTHD_S2_REG_WRITE(val, offset) _FTHD_S2_REG_WRITE(dev_priv, (val), (offset))

#define FTHD_S2_MEM_READ(offset) _FTHD_S2_MEM_READ(dev_priv, (offset))
#define FTHD_S2_MEM_WRITE(val, offset) _FTHD_S2_MEM_WRITE(dev_priv, (val), (offset))
#define FTHD_S2_MEMCPY_TOIO(offset, buf, len) _FTHD_S2_MEMCPY_TOIO(dev_priv, (buf), (offset), (len))
#define FTHD_S2_MEMCPY_FROMIO(buf, offset, len) _FTHD_S2_MEMCPY_FROMIO(dev_priv, (buf), (offset), (len))

#define FTHD_ISP_REG_READ(offset) _FTHD_ISP_REG_READ(dev_priv, (offset))
#define FTHD_ISP_REG_WRITE(val, offset) _FTHD_ISP_REG_WRITE(dev_priv, (val), (offset))

static inline u32 _FTHD_S2_REG_READ(struct fthd_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->s2_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 IO read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "Link IO read at %u\n", offset);
	return ioread32(dev_priv->s2_io + offset);
}

static inline void _FTHD_S2_REG_WRITE(struct fthd_private *dev_priv, u32 val,
				      u32 offset)
{
	if (offset >= dev_priv->s2_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 IO write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "S2 IO write at %u\n", offset);
	iowrite32(val, dev_priv->s2_io + offset);
	fthd_hw_pci_post(dev_priv);
}

static inline u32 _FTHD_S2_MEM_READ(struct fthd_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->s2_mem_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 MEM read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "Link IO read at %u\n", offset);
	return ioread32(dev_priv->s2_mem + offset);
}

static inline void _FTHD_S2_MEM_WRITE(struct fthd_private *dev_priv, u32 val,
				      u32 offset)
{
	if (offset >= dev_priv->s2_mem_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 MEM write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "S2 IO write at %u\n", offset);
	iowrite32(val, dev_priv->s2_mem + offset);
}

static inline void _FTHD_S2_MEMCPY_TOIO(struct fthd_private *dev_priv, const void *buf,
					u32 offset, int len)
{
	memcpy_toio(dev_priv->s2_mem + offset, buf, len);
}


static inline void _FTHD_S2_MEMCPY_FROMIO(struct fthd_private *dev_priv, void *buf,
					  u32 offset, int len)
{
	memcpy_fromio(buf, dev_priv->s2_mem + offset, len);
}

static inline u32 _FTHD_ISP_REG_READ(struct fthd_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->isp_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"ISP IO read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "ISP IO read at %u\n", offset);
	return ioread32(dev_priv->isp_io + offset);
}

static inline void _FTHD_ISP_REG_WRITE(struct fthd_private *dev_priv, u32 val,
				       u32 offset)
{
	if (offset >= dev_priv->isp_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"ISP IO write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "Dev IO write at %u\n", offset);
	iowrite32(val, dev_priv->isp_io + offset);
	fthd_hw_pci_post(dev_priv);
}

extern int fthd_irq_enable(struct fthd_private *dev_priv);
extern int fthd_irq_disable(struct fthd_private *dev_priv);
extern int fthd_hw_init(struct fthd_private *dev_priv);
extern void fthd_hw_deinit(struct fthd_private *priv);
extern void fthd_ddr_phy_save_regs(struct fthd_private *dev_priv);
extern void fthd_ddr_phy_restore_regs(struct fthd_private *dev_priv);
#endif
