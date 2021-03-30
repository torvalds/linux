/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common bus abstraction layer.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_BUS_H
#define WFX_BUS_H

#define WFX_REG_CONFIG        0x0
#define WFX_REG_CONTROL       0x1
#define WFX_REG_IN_OUT_QUEUE  0x2
#define WFX_REG_AHB_DPORT     0x3
#define WFX_REG_BASE_ADDR     0x4
#define WFX_REG_SRAM_DPORT    0x5
#define WFX_REG_SET_GEN_R_W   0x6
#define WFX_REG_FRAME_OUT     0x7

struct hwbus_ops {
	int (*copy_from_io)(void *bus_priv, unsigned int addr,
			    void *dst, size_t count);
	int (*copy_to_io)(void *bus_priv, unsigned int addr,
			  const void *src, size_t count);
	int (*irq_subscribe)(void *bus_priv);
	int (*irq_unsubscribe)(void *bus_priv);
	void (*lock)(void *bus_priv);
	void (*unlock)(void *bus_priv);
	size_t (*align_size)(void *bus_priv, size_t size);
};

extern struct sdio_driver wfx_sdio_driver;
extern struct spi_driver wfx_spi_driver;

#endif
