/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Low-level I/O functions.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_HWIO_H
#define WFX_HWIO_H

#include <linux/types.h>

struct wfx_dev;

int wfx_data_read(struct wfx_dev *wdev, void *buf, size_t buf_len);
int wfx_data_write(struct wfx_dev *wdev, const void *buf, size_t buf_len);

int sram_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len);
int sram_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len);

int ahb_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len);
int ahb_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len);

int sram_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val);
int sram_reg_write(struct wfx_dev *wdev, u32 addr, u32 val);

int ahb_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val);
int ahb_reg_write(struct wfx_dev *wdev, u32 addr, u32 val);

#define CFG_ERR_SPI_FRAME          0x00000001 // only with SPI
#define CFG_ERR_SDIO_BUF_MISMATCH  0x00000001 // only with SDIO
#define CFG_ERR_BUF_UNDERRUN       0x00000002
#define CFG_ERR_DATA_IN_TOO_LARGE  0x00000004
#define CFG_ERR_HOST_NO_OUT_QUEUE  0x00000008
#define CFG_ERR_BUF_OVERRUN        0x00000010
#define CFG_ERR_DATA_OUT_TOO_LARGE 0x00000020
#define CFG_ERR_HOST_NO_IN_QUEUE   0x00000040
#define CFG_ERR_HOST_CRC_MISS      0x00000080 // only with SDIO
#define CFG_SPI_IGNORE_CS          0x00000080 // only with SPI
#define CFG_BYTE_ORDER_MASK        0x00000300 // only writable with SPI
#define     CFG_BYTE_ORDER_BADC    0x00000000
#define     CFG_BYTE_ORDER_DCBA    0x00000100
#define     CFG_BYTE_ORDER_ABCD    0x00000200 // SDIO always use this value
#define CFG_DIRECT_ACCESS_MODE     0x00000400
#define CFG_PREFETCH_AHB           0x00000800
#define CFG_DISABLE_CPU_CLK        0x00001000
#define CFG_PREFETCH_SRAM          0x00002000
#define CFG_CPU_RESET              0x00004000
#define CFG_SDIO_DISABLE_IRQ       0x00008000 // only with SDIO
#define CFG_IRQ_ENABLE_DATA        0x00010000
#define CFG_IRQ_ENABLE_WRDY        0x00020000
#define CFG_CLK_RISE_EDGE          0x00040000
#define CFG_SDIO_DISABLE_CRC_CHK   0x00080000 // only with SDIO
#define CFG_RESERVED               0x00F00000
#define CFG_DEVICE_ID_MAJOR        0x07000000
#define CFG_DEVICE_ID_RESERVED     0x78000000
#define CFG_DEVICE_ID_TYPE         0x80000000
int config_reg_read(struct wfx_dev *wdev, u32 *val);
int config_reg_write(struct wfx_dev *wdev, u32 val);
int config_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val);

#define CTRL_NEXT_LEN_MASK   0x00000FFF
#define CTRL_WLAN_WAKEUP     0x00001000
#define CTRL_WLAN_READY      0x00002000
int control_reg_read(struct wfx_dev *wdev, u32 *val);
int control_reg_write(struct wfx_dev *wdev, u32 val);
int control_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val);

#define IGPR_RW          0x80000000
#define IGPR_INDEX       0x7F000000
#define IGPR_VALUE       0x00FFFFFF
int igpr_reg_read(struct wfx_dev *wdev, int index, u32 *val);
int igpr_reg_write(struct wfx_dev *wdev, int index, u32 val);

#endif /* WFX_HWIO_H */
