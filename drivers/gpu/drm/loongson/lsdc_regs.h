/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_REGS_H__
#define __LSDC_REGS_H__

#include <linux/bitops.h>
#include <linux/types.h>

/*
 * PIXEL PLL Reference clock
 */
#define LSDC_PLL_REF_CLK_KHZ            100000

/*
 * Those PLL registers are relative to LSxxxxx_CFG_REG_BASE. xxxxx = 7A1000,
 * 7A2000, 2K2000, 2K1000 etc.
 */

/* LS7A1000 */

#define LS7A1000_PIXPLL0_REG            0x04B0
#define LS7A1000_PIXPLL1_REG            0x04C0

/* The DC, GPU, Graphic Memory Controller share the single gfxpll */
#define LS7A1000_PLL_GFX_REG            0x0490

#define LS7A1000_CONF_REG_BASE          0x10010000

/* LS7A2000 */

#define LS7A2000_PIXPLL0_REG            0x04B0
#define LS7A2000_PIXPLL1_REG            0x04C0

/* The DC, GPU, Graphic Memory Controller share the single gfxpll */
#define LS7A2000_PLL_GFX_REG            0x0490

#define LS7A2000_CONF_REG_BASE          0x10010000

/* For LSDC_CRTCx_CFG_REG */
#define CFG_PIX_FMT_MASK                GENMASK(2, 0)

enum lsdc_pixel_format {
	LSDC_PF_NONE = 0,
	LSDC_PF_XRGB444 = 1,    /* [12 bits] */
	LSDC_PF_XRGB555 = 2,    /* [15 bits] */
	LSDC_PF_XRGB565 = 3,    /* RGB [16 bits] */
	LSDC_PF_XRGB8888 = 4,   /* XRGB [32 bits] */
};

/*
 * Each crtc has two set fb address registers usable, FB_REG_IN_USING bit of
 * LSDC_CRTCx_CFG_REG indicate which fb address register is in using by the
 * CRTC currently. CFG_PAGE_FLIP is used to trigger the switch, the switching
 * will be finished at the very next vblank. Trigger it again if you want to
 * switch back.
 *
 * If FB0_ADDR_REG is in using, we write the address to FB0_ADDR_REG,
 * if FB1_ADDR_REG is in using, we write the address to FB1_ADDR_REG.
 */
#define CFG_PAGE_FLIP                   BIT(7)
#define CFG_OUTPUT_ENABLE               BIT(8)
#define CFG_HW_CLONE                    BIT(9)
/* Indicate witch fb addr reg is in using, currently. read only */
#define FB_REG_IN_USING                 BIT(11)
#define CFG_GAMMA_EN                    BIT(12)

/* The DC get soft reset if this bit changed from "1" to "0", active low */
#define CFG_RESET_N                     BIT(20)
/* If this bit is set, it say that the CRTC stop working anymore, anchored. */
#define CRTC_ANCHORED                   BIT(24)

/*
 * The DMA step of the DC in LS7A2000/LS2K2000 is configurable,
 * setting those bits on ls7a1000 platform make no effect.
 */
#define CFG_DMA_STEP_MASK              GENMASK(17, 16)
#define CFG_DMA_STEP_SHIFT             16
enum lsdc_dma_steps {
	LSDC_DMA_STEP_256_BYTES = 0,
	LSDC_DMA_STEP_128_BYTES = 1,
	LSDC_DMA_STEP_64_BYTES = 2,
	LSDC_DMA_STEP_32_BYTES = 3,
};

#define CFG_VALID_BITS_MASK             GENMASK(20, 0)

/* For LSDC_CRTCx_HSYNC_REG */
#define HSYNC_INV                       BIT(31)
#define HSYNC_EN                        BIT(30)
#define HSYNC_END_MASK                  GENMASK(28, 16)
#define HSYNC_END_SHIFT                 16
#define HSYNC_START_MASK                GENMASK(12, 0)
#define HSYNC_START_SHIFT               0

/* For LSDC_CRTCx_VSYNC_REG */
#define VSYNC_INV                       BIT(31)
#define VSYNC_EN                        BIT(30)
#define VSYNC_END_MASK                  GENMASK(27, 16)
#define VSYNC_END_SHIFT                 16
#define VSYNC_START_MASK                GENMASK(11, 0)
#define VSYNC_START_SHIFT               0

/*********** CRTC0 ***********/
#define LSDC_CRTC0_CFG_REG              0x1240
#define LSDC_CRTC0_FB0_ADDR_LO_REG      0x1260
#define LSDC_CRTC0_FB0_ADDR_HI_REG      0x15A0
#define LSDC_CRTC0_STRIDE_REG           0x1280
#define LSDC_CRTC0_FB_ORIGIN_REG        0x1300
#define LSDC_CRTC0_HDISPLAY_REG         0x1400
#define LSDC_CRTC0_HSYNC_REG            0x1420
#define LSDC_CRTC0_VDISPLAY_REG         0x1480
#define LSDC_CRTC0_VSYNC_REG            0x14A0
#define LSDC_CRTC0_GAMMA_INDEX_REG      0x14E0
#define LSDC_CRTC0_GAMMA_DATA_REG       0x1500
#define LSDC_CRTC0_FB1_ADDR_LO_REG      0x1580
#define LSDC_CRTC0_FB1_ADDR_HI_REG      0x15C0

/*********** CRTC1 ***********/
#define LSDC_CRTC1_CFG_REG              0x1250
#define LSDC_CRTC1_FB0_ADDR_LO_REG      0x1270
#define LSDC_CRTC1_FB0_ADDR_HI_REG      0x15B0
#define LSDC_CRTC1_STRIDE_REG           0x1290
#define LSDC_CRTC1_FB_ORIGIN_REG        0x1310
#define LSDC_CRTC1_HDISPLAY_REG         0x1410
#define LSDC_CRTC1_HSYNC_REG            0x1430
#define LSDC_CRTC1_VDISPLAY_REG         0x1490
#define LSDC_CRTC1_VSYNC_REG            0x14B0
#define LSDC_CRTC1_GAMMA_INDEX_REG      0x14F0
#define LSDC_CRTC1_GAMMA_DATA_REG       0x1510
#define LSDC_CRTC1_FB1_ADDR_LO_REG      0x1590
#define LSDC_CRTC1_FB1_ADDR_HI_REG      0x15D0

/* For LSDC_CRTCx_DVO_CONF_REG */
#define PHY_CLOCK_POL                   BIT(9)
#define PHY_CLOCK_EN                    BIT(8)
#define PHY_DE_POL                      BIT(1)
#define PHY_DATA_EN                     BIT(0)

/*********** DVO0 ***********/
#define LSDC_CRTC0_DVO_CONF_REG         0x13C0

/*********** DVO1 ***********/
#define LSDC_CRTC1_DVO_CONF_REG         0x13D0

/*
 * All of the DC variants has the hardware which record the scan position
 * of the CRTC, [31:16] : current X position, [15:0] : current Y position
 */
#define LSDC_CRTC0_SCAN_POS_REG         0x14C0
#define LSDC_CRTC1_SCAN_POS_REG         0x14D0

/*
 * LS7A2000 has Sync Deviation register.
 */
#define SYNC_DEVIATION_EN               BIT(31)
#define SYNC_DEVIATION_NUM              GENMASK(12, 0)
#define LSDC_CRTC0_SYNC_DEVIATION_REG   0x1B80
#define LSDC_CRTC1_SYNC_DEVIATION_REG   0x1B90

/*
 * In gross, LSDC_CRTC1_XXX_REG - LSDC_CRTC0_XXX_REG = 0x10, but not all of
 * the registers obey this rule, LSDC_CURSORx_XXX_REG just don't honor this.
 * This is the root cause we can't untangle the code by manpulating offset
 * of the register access simply. Our hardware engineers are lack experiance
 * when they design this...
 */
#define CRTC_PIPE_OFFSET                0x10

/*
 * There is only one hardware cursor unit in LS7A1000 and LS2K1000, let
 * CFG_HW_CLONE_EN bit be "1" could eliminate this embarrassment, we made
 * it on custom clone mode application. While LS7A2000 has two hardware
 * cursor unit which is good enough.
 */
#define CURSOR_FORMAT_MASK              GENMASK(1, 0)
#define CURSOR_FORMAT_SHIFT             0
enum lsdc_cursor_format {
	CURSOR_FORMAT_DISABLE = 0,
	CURSOR_FORMAT_MONOCHROME = 1,   /* masked */
	CURSOR_FORMAT_ARGB8888 = 2,     /* A8R8G8B8 */
};

/*
 * LS7A1000 and LS2K1000 only support 32x32, LS2K2000 and LS7A2000 support
 * 64x64, but it seems that setting this bit make no harms on LS7A1000, it
 * just don't take effects.
 */
#define CURSOR_SIZE_SHIFT               2
enum lsdc_cursor_size {
	CURSOR_SIZE_32X32 = 0,
	CURSOR_SIZE_64X64 = 1,
};

#define CURSOR_LOCATION_SHIFT           4
enum lsdc_cursor_location {
	CURSOR_ON_CRTC0 = 0,
	CURSOR_ON_CRTC1 = 1,
};

#define LSDC_CURSOR0_CFG_REG            0x1520
#define LSDC_CURSOR0_ADDR_LO_REG        0x1530
#define LSDC_CURSOR0_ADDR_HI_REG        0x15e0
#define LSDC_CURSOR0_POSITION_REG       0x1540  /* [31:16] Y, [15:0] X */
#define LSDC_CURSOR0_BG_COLOR_REG       0x1550  /* background color */
#define LSDC_CURSOR0_FG_COLOR_REG       0x1560  /* foreground color */

#define LSDC_CURSOR1_CFG_REG            0x1670
#define LSDC_CURSOR1_ADDR_LO_REG        0x1680
#define LSDC_CURSOR1_ADDR_HI_REG        0x16e0
#define LSDC_CURSOR1_POSITION_REG       0x1690  /* [31:16] Y, [15:0] X */
#define LSDC_CURSOR1_BG_COLOR_REG       0x16A0  /* background color */
#define LSDC_CURSOR1_FG_COLOR_REG       0x16B0  /* foreground color */

/*
 * DC Interrupt Control Register, 32bit, Address Offset: 1570
 *
 * Bits 15:0 inidicate the interrupt status
 * Bits 31:16 control enable interrupts corresponding to bit 15:0 or not
 * Write 1 to enable, write 0 to disable
 *
 * RF: Read Finished
 * IDBU: Internal Data Buffer Underflow
 * IDBFU: Internal Data Buffer Fatal Underflow
 * CBRF: Cursor Buffer Read Finished Flag, no use.
 * FBRF0: CRTC-0 reading from its framebuffer finished.
 * FBRF1: CRTC-1 reading from its framebuffer finished.
 *
 * +-------+--------------------------+-------+--------+--------+-------+
 * | 31:27 |         26:16            | 15:11 |   10   |   9    |   8   |
 * +-------+--------------------------+-------+--------+--------+-------+
 * |  N/A  | Interrupt Enable Control |  N/A  | IDBFU0 | IDBFU1 | IDBU0 |
 * +-------+--------------------------+-------+--------+--------+-------+
 *
 * +-------+-------+-------+------+--------+--------+--------+--------+
 * |   7   |   6   |   5   |  4   |   3    |   2    |   1    |   0    |
 * +-------+-------+-------+------+--------+--------+--------+--------+
 * | IDBU1 | FBRF0 | FBRF1 | CRRF | HSYNC0 | VSYNC0 | HSYNC1 | VSYNC1 |
 * +-------+-------+-------+------+--------+--------+--------+--------+
 *
 * unfortunately, CRTC0's interrupt is mess with CRTC1's interrupt in one
 * register again.
 */

#define LSDC_INT_REG                    0x1570

#define INT_CRTC0_VSYNC                 BIT(2)
#define INT_CRTC0_HSYNC                 BIT(3)
#define INT_CRTC0_RF                    BIT(6)
#define INT_CRTC0_IDBU                  BIT(8)
#define INT_CRTC0_IDBFU                 BIT(10)

#define INT_CRTC1_VSYNC                 BIT(0)
#define INT_CRTC1_HSYNC                 BIT(1)
#define INT_CRTC1_RF                    BIT(5)
#define INT_CRTC1_IDBU                  BIT(7)
#define INT_CRTC1_IDBFU                 BIT(9)

#define INT_CRTC0_VSYNC_EN              BIT(18)
#define INT_CRTC0_HSYNC_EN              BIT(19)
#define INT_CRTC0_RF_EN                 BIT(22)
#define INT_CRTC0_IDBU_EN               BIT(24)
#define INT_CRTC0_IDBFU_EN              BIT(26)

#define INT_CRTC1_VSYNC_EN              BIT(16)
#define INT_CRTC1_HSYNC_EN              BIT(17)
#define INT_CRTC1_RF_EN                 BIT(21)
#define INT_CRTC1_IDBU_EN               BIT(23)
#define INT_CRTC1_IDBFU_EN              BIT(25)

#define INT_STATUS_MASK                 GENMASK(15, 0)

/*
 * LS7A1000/LS7A2000 have 4 gpios which are used to emulated I2C.
 * They are under control of the LS7A_DC_GPIO_DAT_REG and LS7A_DC_GPIO_DIR_REG
 * register, Those GPIOs has no relationship whth the GPIO hardware on the
 * bridge chip itself. Those offsets are relative to DC register base address
 *
 * LS2k1000 don't have those registers, they use hardware i2c or general GPIO
 * emulated i2c from linux i2c subsystem.
 *
 * GPIO data register, address offset: 0x1650
 *   +---------------+-----------+-----------+
 *   | 7 | 6 | 5 | 4 |  3  |  2  |  1  |  0  |
 *   +---------------+-----------+-----------+
 *   |               |    DVO1   |    DVO0   |
 *   +      N/A      +-----------+-----------+
 *   |               | SCL | SDA | SCL | SDA |
 *   +---------------+-----------+-----------+
 */
#define LS7A_DC_GPIO_DAT_REG            0x1650

/*
 *  GPIO Input/Output direction control register, address offset: 0x1660
 */
#define LS7A_DC_GPIO_DIR_REG            0x1660

/*
 *  LS7A2000 has two built-in HDMI Encoder and one VGA encoder
 */

/*
 * Number of continuous packets may be present
 * in HDMI hblank and vblank zone, should >= 48
 */
#define LSDC_HDMI0_ZONE_REG             0x1700
#define LSDC_HDMI1_ZONE_REG             0x1710

#define HDMI_H_ZONE_IDLE_SHIFT          0
#define HDMI_V_ZONE_IDLE_SHIFT          16

/* HDMI Iterface Control Reg */
#define HDMI_INTERFACE_EN               BIT(0)
#define HDMI_PACKET_EN                  BIT(1)
#define HDMI_AUDIO_EN                   BIT(2)
/*
 * Preamble:
 * Immediately preceding each video data period or data island period is the
 * preamble. This is a sequence of eight identical control characters that
 * indicate whether the upcoming data period is a video data period or is a
 * data island. The values of CTL0, CTL1, CTL2, and CTL3 indicate the type of
 * data period that follows.
 */
#define HDMI_VIDEO_PREAMBLE_MASK        GENMASK(7, 4)
#define HDMI_VIDEO_PREAMBLE_SHIFT       4
/* 1: hw i2c, 0: gpio emu i2c, shouldn't put in LSDC_HDMIx_INTF_CTRL_REG */
#define HW_I2C_EN                       BIT(8)
#define HDMI_CTL_PERIOD_MODE            BIT(9)
#define LSDC_HDMI0_INTF_CTRL_REG        0x1720
#define LSDC_HDMI1_INTF_CTRL_REG        0x1730

#define HDMI_PHY_EN                     BIT(0)
#define HDMI_PHY_RESET_N                BIT(1)
#define HDMI_PHY_TERM_L_EN              BIT(8)
#define HDMI_PHY_TERM_H_EN              BIT(9)
#define HDMI_PHY_TERM_DET_EN            BIT(10)
#define HDMI_PHY_TERM_STATUS            BIT(11)
#define LSDC_HDMI0_PHY_CTRL_REG         0x1800
#define LSDC_HDMI1_PHY_CTRL_REG         0x1810

/* High level duration need > 1us */
#define HDMI_PLL_ENABLE                 BIT(0)
#define HDMI_PLL_LOCKED                 BIT(16)
/* Bypass the software configured values, using default source from somewhere */
#define HDMI_PLL_BYPASS                 BIT(17)

#define HDMI_PLL_IDF_SHIFT              1
#define HDMI_PLL_IDF_MASK               GENMASK(5, 1)
#define HDMI_PLL_LF_SHIFT               6
#define HDMI_PLL_LF_MASK                GENMASK(12, 6)
#define HDMI_PLL_ODF_SHIFT              13
#define HDMI_PLL_ODF_MASK               GENMASK(15, 13)
#define LSDC_HDMI0_PHY_PLL_REG          0x1820
#define LSDC_HDMI1_PHY_PLL_REG          0x1830

/* LS7A2000/LS2K2000 has hpd status reg, while the two hdmi's status
 * located at the one register again.
 */
#define LSDC_HDMI_HPD_STATUS_REG        0x1BA0
#define HDMI0_HPD_FLAG                  BIT(0)
#define HDMI1_HPD_FLAG                  BIT(1)

#define LSDC_HDMI0_PHY_CAL_REG          0x18C0
#define LSDC_HDMI1_PHY_CAL_REG          0x18D0

/* AVI InfoFrame */
#define LSDC_HDMI0_AVI_CONTENT0         0x18E0
#define LSDC_HDMI1_AVI_CONTENT0         0x18D0
#define LSDC_HDMI0_AVI_CONTENT1         0x1900
#define LSDC_HDMI1_AVI_CONTENT1         0x1910
#define LSDC_HDMI0_AVI_CONTENT2         0x1920
#define LSDC_HDMI1_AVI_CONTENT2         0x1930
#define LSDC_HDMI0_AVI_CONTENT3         0x1940
#define LSDC_HDMI1_AVI_CONTENT3         0x1950

/* 1: enable avi infoframe packet, 0: disable avi infoframe packet */
#define AVI_PKT_ENABLE                  BIT(0)
/* 1: send one every two frame, 0: send one each frame */
#define AVI_PKT_SEND_FREQ               BIT(1)
/*
 * 1: write 1 to flush avi reg content0 ~ content3 to the packet to be send,
 * The hardware will clear this bit automatically.
 */
#define AVI_PKT_UPDATE                  BIT(2)

#define LSDC_HDMI0_AVI_INFO_CRTL_REG    0x1960
#define LSDC_HDMI1_AVI_INFO_CRTL_REG    0x1970

/*
 * LS7A2000 has the hardware which count the number of vblank generated
 */
#define LSDC_CRTC0_VSYNC_COUNTER_REG    0x1A00
#define LSDC_CRTC1_VSYNC_COUNTER_REG    0x1A10

/*
 * LS7A2000 has the audio hardware associate with the HDMI encoder.
 */
#define LSDC_HDMI0_AUDIO_PLL_LO_REG     0x1A20
#define LSDC_HDMI1_AUDIO_PLL_LO_REG     0x1A30

#define LSDC_HDMI0_AUDIO_PLL_HI_REG     0x1A40
#define LSDC_HDMI1_AUDIO_PLL_HI_REG     0x1A50

#endif
