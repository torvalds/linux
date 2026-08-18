/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HWS_PCIE_REG_H
#define _HWS_PCIE_REG_H

#include <linux/bits.h>
#include <linux/sizes.h>

#define XDMA_CHANNEL_NUM_MAX (1)
#define MAX_NUM_ENGINES (XDMA_CHANNEL_NUM_MAX * 2)

#define  PCIE_BARADDROFSIZE 4u

#define PCI_BUS_ACCESS_BASE       0x00000000U
#define INT_EN_REG_BASE           (PCI_BUS_ACCESS_BASE + 0x0134U)
#define PCIEBR_EN_REG_BASE        (PCI_BUS_ACCESS_BASE + 0x0148U)
#define PCIE_INT_DEC_REG_BASE     (PCI_BUS_ACCESS_BASE + 0x0138U)

#define HWS_INT_EN_MASK           0x0003FFFFU

#define PCIEBAR_AXI_BASE 0x20000000U

#define CTL_REG_ACC_BASE 0x0
#define PCI_ADDR_TABLE_BASE CTL_REG_ACC_BASE

#define CVBS_IN_BASE              0x00004000U
#define CVBS_IN_BUF_BASE          (CVBS_IN_BASE + (16U * PCIE_BARADDROFSIZE))
#define CVBS_IN_BUF_BASE2         (CVBS_IN_BASE + (50U * PCIE_BARADDROFSIZE))

/* 2 Mib */
#define MAX_L_VIDEO_SIZE            0x200000U

#define PCI_E_BAR_PAGE_SIZE 0x20000000
#define PCI_E_BAR_ADD_MASK 0xE0000000
#define PCI_E_BAR_ADD_LOWMASK 0x1FFFFFFF

#define MAX_VID_CHANNELS            4

#define MAX_MM_VIDEO_SIZE            SZ_4M

#define MAX_VIDEO_HW_W 1920
#define MAX_VIDEO_HW_H 1080
#define MAX_VIDEO_SCALER_SIZE     (1920U * 1080U * 2U)

#define MIN_VAMP_BRIGHTNESS_UNITS   0
#define MAX_VAMP_BRIGHTNESS_UNITS   0xff

#define MIN_VAMP_CONTRAST_UNITS     0
#define MAX_VAMP_CONTRAST_UNITS     0xff

#define MIN_VAMP_SATURATION_UNITS   0
#define MAX_VAMP_SATURATION_UNITS   0xff

#define MIN_VAMP_HUE_UNITS          0
#define MAX_VAMP_HUE_UNITS          0xff

#define HWS_BRIGHTNESS_DEFAULT       0x80
#define HWS_CONTRAST_DEFAULT         0x80
#define HWS_SATURATION_DEFAULT       0x80
#define HWS_HUE_DEFAULT              0x00

/* Core/global status. */
#define HWS_REG_SYS_STATUS            (CVBS_IN_BASE +  0 * PCIE_BARADDROFSIZE)
/* bit3: DMA busy, bit2: int, ... */

#define HWS_SYS_DMA_BUSY_BIT          BIT(3) /* 0x08 = DMA busy flag */

#define HWS_REG_DEC_MODE       (CVBS_IN_BASE +  0 * PCIE_BARADDROFSIZE)
/* Main control register */
#define HWS_REG_CTL            (CVBS_IN_BASE +  4 * PCIE_BARADDROFSIZE)
#define HWS_CTL_IRQ_ENABLE_BIT BIT(0)   /* Global interrupt enable bit */
/*  Write 0x00 to fully reset decoder,
 *  set bit 31=1 to "start run",
 *  low byte=0x13 selects YUYV/BT.709/etc,
 *  in ReadChipId() we also write 0x00 and 0x10 here for chip-ID sequencing.
 */

/* Per-channel done flags. */
#define HWS_REG_INT_STATUS            (CVBS_IN_BASE +  1 * PCIE_BARADDROFSIZE)
#define HWS_SYS_BUSY_BIT          BIT(2)      /* matches old 0x04 test   */

/* Capture enable switches. */
/* bit0-3: CH0-CH3 video enable */
#define HWS_REG_VCAP_ENABLE           (CVBS_IN_BASE +  2 * PCIE_BARADDROFSIZE)
/* bits0-3: signal present, bits8-11: interlace */
#define HWS_REG_ACTIVE_STATUS          (CVBS_IN_BASE +  5  * PCIE_BARADDROFSIZE)
/* bits0-3: HDCP detected */
#define HWS_REG_HDCP_STATUS            (CVBS_IN_BASE +  8  * PCIE_BARADDROFSIZE)
#define HWS_REG_DMA_MAX_SIZE   (CVBS_IN_BASE +  9 * PCIE_BARADDROFSIZE)

/* Buffer addresses (written once during init/reset). */
/* Base of host-visible buffer. */
#define HWS_REG_VBUF1_ADDR            (CVBS_IN_BASE + 25 * PCIE_BARADDROFSIZE)
/* Per-channel DMA address. */
#define HWS_REG_DMA_ADDR(ch)          (CVBS_IN_BASE + (26 + (ch)) * PCIE_BARADDROFSIZE)

/* Per-channel live buffer toggles (read-only). */
#define HWS_REG_VBUF_TOGGLE(ch)       (CVBS_IN_BASE + (32 + (ch)) * PCIE_BARADDROFSIZE)
/*
 * Returns 0 or 1 = which half of the video ring the DMA engine is
 * currently filling for channel *ch* (0-3).
 */

/* Per-interrupt bits (video 0-3). */
#define HWS_INT_VDONE_BIT(ch)     BIT(ch)         /* 0x01,0x02,0x04,0x08  */

#define HWS_REG_INT_ACK           (CVBS_IN_BASE + 0x4000 + 1 * PCIE_BARADDROFSIZE)

/* 16-bit W | 16-bit H. */
#define HWS_REG_IN_RES(ch)             (CVBS_IN_BASE + (90  + (ch) * 2) * PCIE_BARADDROFSIZE)
/* B|C|H|S packed bytes. */
#define HWS_REG_BCHS(ch)               (CVBS_IN_BASE + (91  + (ch) * 2) * PCIE_BARADDROFSIZE)

/* Input fps. */
#define HWS_REG_FRAME_RATE(ch)         (CVBS_IN_BASE + (110 + (ch))    * PCIE_BARADDROFSIZE)
/* Programmed out W|H. */
#define HWS_REG_OUT_RES(ch)            (CVBS_IN_BASE + (120 + (ch))    * PCIE_BARADDROFSIZE)
/* Programmed out fps. */
#define HWS_REG_OUT_FRAME_RATE(ch)     (CVBS_IN_BASE + (130 + (ch))    * PCIE_BARADDROFSIZE)

/* Device version/port ID/subversion register. */
#define HWS_REG_DEVICE_INFO   (CVBS_IN_BASE +  88 * PCIE_BARADDROFSIZE)
/*
 * Reading this 32-bit word returns:
 *   bits 7:0   = "device version"
 *   bits 15:8  = "device sub-version"
 *   bits 23:24 = "HW key / port ID" etc.
 *   bits 31:28 = "support YV12" flags
 */

/* Convenience aliases for individual channels. */
#define HWS_REG_VBUF_TOGGLE_CH0       HWS_REG_VBUF_TOGGLE(0)
#define HWS_REG_VBUF_TOGGLE_CH1       HWS_REG_VBUF_TOGGLE(1)
#define HWS_REG_VBUF_TOGGLE_CH2       HWS_REG_VBUF_TOGGLE(2)
#define HWS_REG_VBUF_TOGGLE_CH3       HWS_REG_VBUF_TOGGLE(3)

#endif /* _HWS_PCIE_REG_H */
