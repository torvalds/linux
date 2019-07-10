/* SPDX-License-Identifier: GPL-2.0-only */
/* Hardware definitions for SSI.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 */

#ifndef __OMAP_SSI_REGS_H__
#define __OMAP_SSI_REGS_H__

/*
 * SSI SYS registers
 */
#define SSI_REVISION_REG    0
#  define SSI_REV_MAJOR    0xf0
#  define SSI_REV_MINOR    0xf
#define SSI_SYSCONFIG_REG    0x10
#  define SSI_AUTOIDLE    (1 << 0)
#  define SSI_SOFTRESET    (1 << 1)
#  define SSI_SIDLEMODE_FORCE  0
#  define SSI_SIDLEMODE_NO    (1 << 3)
#  define SSI_SIDLEMODE_SMART  (1 << 4)
#  define SSI_SIDLEMODE_MASK  0x18
#  define SSI_MIDLEMODE_FORCE  0
#  define SSI_MIDLEMODE_NO    (1 << 12)
#  define SSI_MIDLEMODE_SMART  (1 << 13)
#  define SSI_MIDLEMODE_MASK  0x3000
#define SSI_SYSSTATUS_REG    0x14
#  define SSI_RESETDONE    1
#define SSI_MPU_STATUS_REG(port, irq)  (0x808 + ((port) * 0x10) + ((irq) * 2))
#define SSI_MPU_ENABLE_REG(port, irq)  (0x80c + ((port) * 0x10) + ((irq) * 8))
#  define SSI_DATAACCEPT(channel)    (1 << (channel))
#  define SSI_DATAAVAILABLE(channel)  (1 << ((channel) + 8))
#  define SSI_DATAOVERRUN(channel)    (1 << ((channel) + 16))
#  define SSI_ERROROCCURED      (1 << 24)
#  define SSI_BREAKDETECTED    (1 << 25)
#define SSI_GDD_MPU_IRQ_STATUS_REG  0x0800
#define SSI_GDD_MPU_IRQ_ENABLE_REG  0x0804
#  define SSI_GDD_LCH(channel)  (1 << (channel))
#define SSI_WAKE_REG(port)    (0xc00 + ((port) * 0x10))
#define SSI_CLEAR_WAKE_REG(port)  (0xc04 + ((port) * 0x10))
#define SSI_SET_WAKE_REG(port)    (0xc08 + ((port) * 0x10))
#  define SSI_WAKE(channel)  (1 << (channel))
#  define SSI_WAKE_MASK    0xff

/*
 * SSI SST registers
 */
#define SSI_SST_ID_REG      0
#define SSI_SST_MODE_REG    4
#  define SSI_MODE_VAL_MASK  3
#  define SSI_MODE_SLEEP    0
#  define SSI_MODE_STREAM    1
#  define SSI_MODE_FRAME    2
#  define SSI_MODE_MULTIPOINTS  3
#define SSI_SST_FRAMESIZE_REG    8
#  define SSI_FRAMESIZE_DEFAULT  31
#define SSI_SST_TXSTATE_REG    0xc
#  define  SSI_TXSTATE_IDLE  0
#define SSI_SST_BUFSTATE_REG    0x10
#  define  SSI_FULL(channel)  (1 << (channel))
#define SSI_SST_DIVISOR_REG    0x18
#  define SSI_MAX_DIVISOR    127
#define SSI_SST_BREAK_REG    0x20
#define SSI_SST_CHANNELS_REG    0x24
#  define SSI_CHANNELS_DEFAULT  4
#define SSI_SST_ARBMODE_REG    0x28
#  define SSI_ARBMODE_ROUNDROBIN  0
#  define SSI_ARBMODE_PRIORITY  1
#define SSI_SST_BUFFER_CH_REG(channel)  (0x80 + ((channel) * 4))
#define SSI_SST_SWAPBUF_CH_REG(channel)  (0xc0 + ((channel) * 4))

/*
 * SSI SSR registers
 */
#define SSI_SSR_ID_REG      0
#define SSI_SSR_MODE_REG    4
#define SSI_SSR_FRAMESIZE_REG    8
#define SSI_SSR_RXSTATE_REG    0xc
#define SSI_SSR_BUFSTATE_REG    0x10
#  define SSI_NOTEMPTY(channel)  (1 << (channel))
#define SSI_SSR_BREAK_REG    0x1c
#define SSI_SSR_ERROR_REG    0x20
#define SSI_SSR_ERRORACK_REG    0x24
#define SSI_SSR_OVERRUN_REG    0x2c
#define SSI_SSR_OVERRUNACK_REG    0x30
#define SSI_SSR_TIMEOUT_REG    0x34
#  define SSI_TIMEOUT_DEFAULT  0
#define SSI_SSR_CHANNELS_REG    0x28
#define SSI_SSR_BUFFER_CH_REG(channel)  (0x80 + ((channel) * 4))
#define SSI_SSR_SWAPBUF_CH_REG(channel)  (0xc0 + ((channel) * 4))

/*
 * SSI GDD registers
 */
#define SSI_GDD_HW_ID_REG    0
#define SSI_GDD_PPORT_ID_REG    0x10
#define SSI_GDD_MPORT_ID_REG    0x14
#define SSI_GDD_PPORT_SR_REG    0x20
#define SSI_GDD_MPORT_SR_REG    0x24
#  define SSI_ACTIVE_LCH_NUM_MASK  0xff
#define SSI_GDD_TEST_REG    0x40
#  define SSI_TEST      1
#define SSI_GDD_GCR_REG      0x100
#  define  SSI_CLK_AUTOGATING_ON  (1 << 3)
#  define  SSI_FREE    (1 << 2)
#  define  SSI_SWITCH_OFF    (1 << 0)
#define SSI_GDD_GRST_REG    0x200
#  define SSI_SWRESET    1
#define SSI_GDD_CSDP_REG(channel)  (0x800 + ((channel) * 0x40))
#  define SSI_DST_BURST_EN_MASK  0xc000
#  define SSI_DST_SINGLE_ACCESS0  0
#  define SSI_DST_SINGLE_ACCESS  (1 << 14)
#  define SSI_DST_BURST_4x32_BIT  (2 << 14)
#  define SSI_DST_BURST_8x32_BIT  (3 << 14)
#  define SSI_DST_MASK    0x1e00
#  define SSI_DST_MEMORY_PORT  (8 << 9)
#  define SSI_DST_PERIPHERAL_PORT  (9 << 9)
#  define SSI_SRC_BURST_EN_MASK  0x180
#  define SSI_SRC_SINGLE_ACCESS0  0
#  define SSI_SRC_SINGLE_ACCESS  (1 << 7)
#  define SSI_SRC_BURST_4x32_BIT  (2 << 7)
#  define SSI_SRC_BURST_8x32_BIT  (3 << 7)
#  define SSI_SRC_MASK    0x3c
#  define SSI_SRC_MEMORY_PORT  (8 << 2)
#  define SSI_SRC_PERIPHERAL_PORT  (9 << 2)
#  define SSI_DATA_TYPE_MASK  3
#  define SSI_DATA_TYPE_S32  2
#define SSI_GDD_CCR_REG(channel)  (0x802 + ((channel) * 0x40))
#  define SSI_DST_AMODE_MASK  (3 << 14)
#  define SSI_DST_AMODE_CONST  0
#  define SSI_DST_AMODE_POSTINC  (1 << 12)
#  define SSI_SRC_AMODE_MASK  (3 << 12)
#  define SSI_SRC_AMODE_CONST  0
#  define SSI_SRC_AMODE_POSTINC  (1 << 12)
#  define SSI_CCR_ENABLE    (1 << 7)
#  define SSI_CCR_SYNC_MASK  0x1f
#define SSI_GDD_CICR_REG(channel)  (0x804 + ((channel) * 0x40))
#  define SSI_BLOCK_IE    (1 << 5)
#  define SSI_HALF_IE    (1 << 2)
#  define SSI_TOUT_IE    (1 << 0)
#define SSI_GDD_CSR_REG(channel)  (0x806 + ((channel) * 0x40))
#  define SSI_CSR_SYNC    (1 << 6)
#  define SSI_CSR_BLOCK    (1 << 5)
#  define SSI_CSR_HALF    (1 << 2)
#  define SSI_CSR_TOUR    (1 << 0)
#define SSI_GDD_CSSA_REG(channel)  (0x808 + ((channel) * 0x40))
#define SSI_GDD_CDSA_REG(channel)  (0x80c + ((channel) * 0x40))
#define SSI_GDD_CEN_REG(channel)  (0x810 + ((channel) * 0x40))
#define SSI_GDD_CSAC_REG(channel)  (0x818 + ((channel) * 0x40))
#define SSI_GDD_CDAC_REG(channel)  (0x81a + ((channel) * 0x40))
#define SSI_GDD_CLNK_CTRL_REG(channel)  (0x828 + ((channel) * 0x40))
#  define SSI_ENABLE_LNK    (1 << 15)
#  define SSI_STOP_LNK    (1 << 14)
#  define SSI_NEXT_CH_ID_MASK  0xf

#endif /* __OMAP_SSI_REGS_H__ */
