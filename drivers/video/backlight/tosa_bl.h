/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TOSA_BL_H
#define _TOSA_BL_H

struct spi_device;
extern int tosa_bl_enable(struct spi_device *spi, int enable);

#endif
