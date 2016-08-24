/*
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#ifndef __SPI_BCM_QSPI_H__
#define __SPI_BCM_QSPI_H__

#include <linux/types.h>
#include <linux/io.h>

/* MSPI Interrupt masks */
#define INTR_MSPI_HALTED_MASK			BIT(6)
#define INTR_MSPI_DONE_MASK			BIT(5)

#define MSPI_INTERRUPTS_ALL		       \
	(INTR_MSPI_DONE_MASK |		       \
	 INTR_MSPI_HALTED_MASK)

struct platform_device;
struct dev_pm_ops;

struct bcm_qspi_soc_intc;

/* Read controller register*/
static inline u32 bcm_qspi_readl(bool be, void __iomem *addr)
{
	if (be)
		return ioread32be(addr);
	else
		return readl_relaxed(addr);
}

/* Write controller register*/
static inline void bcm_qspi_writel(bool be,
				   unsigned int data, void __iomem *addr)
{
	if (be)
		iowrite32be(data, addr);
	else
		writel_relaxed(data, addr);
}

/* The common driver functions to be called by the SoC platform driver */
int bcm_qspi_probe(struct platform_device *pdev,
		   struct bcm_qspi_soc_intc *soc_intc);
int bcm_qspi_remove(struct platform_device *pdev);

/* pm_ops used by the SoC platform driver called on PM suspend/resume */
extern const struct dev_pm_ops bcm_qspi_pm_ops;

#endif /* __SPI_BCM_QSPI_H__ */
