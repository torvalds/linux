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

/* BSPI interrupt masks */
#define INTR_BSPI_LR_OVERREAD_MASK		BIT(4)
#define INTR_BSPI_LR_SESSION_DONE_MASK		BIT(3)
#define INTR_BSPI_LR_IMPATIENT_MASK		BIT(2)
#define INTR_BSPI_LR_SESSION_ABORTED_MASK	BIT(1)
#define INTR_BSPI_LR_FULLNESS_REACHED_MASK	BIT(0)

#define BSPI_LR_INTERRUPTS_DATA		       \
	(INTR_BSPI_LR_SESSION_DONE_MASK |	       \
	 INTR_BSPI_LR_FULLNESS_REACHED_MASK)

#define BSPI_LR_INTERRUPTS_ERROR               \
	(INTR_BSPI_LR_OVERREAD_MASK |	       \
	 INTR_BSPI_LR_IMPATIENT_MASK |	       \
	 INTR_BSPI_LR_SESSION_ABORTED_MASK)

#define BSPI_LR_INTERRUPTS_ALL                 \
	(BSPI_LR_INTERRUPTS_ERROR |	       \
	 BSPI_LR_INTERRUPTS_DATA)

/* MSPI Interrupt masks */
#define INTR_MSPI_HALTED_MASK			BIT(6)
#define INTR_MSPI_DONE_MASK			BIT(5)

#define MSPI_INTERRUPTS_ALL		       \
	(INTR_MSPI_DONE_MASK |		       \
	 INTR_MSPI_HALTED_MASK)

#define QSPI_INTERRUPTS_ALL                    \
	(MSPI_INTERRUPTS_ALL |		       \
	 BSPI_LR_INTERRUPTS_ALL)

struct platform_device;
struct dev_pm_ops;

enum {
	MSPI_DONE = 0x1,
	BSPI_DONE = 0x2,
	BSPI_ERR = 0x4,
	MSPI_BSPI_DONE = 0x7
};

struct bcm_qspi_soc_intc {
	void (*bcm_qspi_int_ack)(struct bcm_qspi_soc_intc *soc_intc, int type);
	void (*bcm_qspi_int_set)(struct bcm_qspi_soc_intc *soc_intc, int type,
				 bool en);
	u32 (*bcm_qspi_get_int_status)(struct bcm_qspi_soc_intc *soc_intc);
};

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

static inline u32 get_qspi_mask(int type)
{
	switch (type) {
	case MSPI_DONE:
		return INTR_MSPI_DONE_MASK;
	case BSPI_DONE:
		return BSPI_LR_INTERRUPTS_ALL;
	case MSPI_BSPI_DONE:
		return QSPI_INTERRUPTS_ALL;
	case BSPI_ERR:
		return BSPI_LR_INTERRUPTS_ERROR;
	}

	return 0;
}

/* The common driver functions to be called by the SoC platform driver */
int bcm_qspi_probe(struct platform_device *pdev,
		   struct bcm_qspi_soc_intc *soc_intc);
int bcm_qspi_remove(struct platform_device *pdev);

/* pm_ops used by the SoC platform driver called on PM suspend/resume */
extern const struct dev_pm_ops bcm_qspi_pm_ops;

#endif /* __SPI_BCM_QSPI_H__ */
