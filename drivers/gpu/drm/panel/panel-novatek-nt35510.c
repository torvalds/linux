// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT35510 panel driver
 * Copyright (C) 2020 Linus Walleij <linus.walleij@linaro.org>
 * Based on code by Robert Teather (C) 2012 Samsung
 *
 * This display driver (and I refer to the physical component NT35510,
 * not this Linux kernel software driver) can handle:
 * 480x864, 480x854, 480x800, 480x720 and 480x640 pixel displays.
 * It has 480x840x24bit SRAM embedded for storing a frame.
 * When powered on the display is by default in 480x800 mode.
 *
 * The actual panels using this component have different names, but
 * the code needed to set up and configure the panel will be similar,
 * so they should all use the NT35510 driver with appropriate configuration
 * per-panel, e.g. for physical size.
 *
 * This driver is for the DSI interface to panels using the NT35510.
 *
 * The NT35510 can also use an RGB (DPI) interface combined with an
 * I2C or SPI interface for setting up the NT35510. If this is needed
 * this panel driver should be refactored to also support that use
 * case.
 */
#include <linux/backlight.h>
#include <linux/bitops.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define NT35510_CMD_CORRECT_GAMMA BIT(0)
#define NT35510_CMD_CONTROL_DISPLAY BIT(1)
#define NT35510_CMD_SETVCMOFF BIT(2)

#define MCS_CMD_MAUCCTR		0xF0 /* Manufacturer command enable */
#define MCS_CMD_READ_ID1	0xDA
#define MCS_CMD_READ_ID2	0xDB
#define MCS_CMD_READ_ID3	0xDC
#define MCS_CMD_MTP_READ_SETTING 0xF8 /* Uncertain about name */
#define MCS_CMD_MTP_READ_PARAM 0xFF /* Uncertain about name */

/*
 * These manufacturer commands are available after we enable manufacturer
 * command set (MCS) for page 0.
 */
#define NT35510_P0_DOPCTR 0xB1
#define NT35510_P0_SDHDTCTR 0xB6
#define NT35510_P0_GSEQCTR 0xB7
#define NT35510_P0_SDEQCTR 0xB8
#define NT35510_P0_SDVPCTR 0xBA
#define NT35510_P0_DPFRCTR1 0xBD
#define NT35510_P0_DPFRCTR2 0xBE
#define NT35510_P0_DPFRCTR3 0xBF
#define NT35510_P0_DPMCTR12 0xCC

#define NT35510_P0_DOPCTR_LEN 2
#define NT35510_P0_GSEQCTR_LEN 2
#define NT35510_P0_SDEQCTR_LEN 4
#define NT35510_P0_SDVPCTR_LEN 1
#define NT35510_P0_DPFRCTR1_LEN 5
#define NT35510_P0_DPFRCTR2_LEN 5
#define NT35510_P0_DPFRCTR3_LEN 5
#define NT35510_P0_DPMCTR12_LEN 3

#define NT35510_DOPCTR_0_RAMKP BIT(7) /* Contents kept in sleep */
#define NT35510_DOPCTR_0_DSITE BIT(6) /* Enable TE signal */
#define NT35510_DOPCTR_0_DSIG BIT(5) /* Enable generic read/write */
#define NT35510_DOPCTR_0_DSIM BIT(4) /* Enable video mode on DSI */
#define NT35510_DOPCTR_0_EOTP BIT(3) /* Support EoTP */
#define NT35510_DOPCTR_0_N565 BIT(2) /* RGB or BGR pixel format */
#define NT35510_DOPCTR_1_TW_PWR_SEL BIT(4) /* TE power selector */
#define NT35510_DOPCTR_1_CRGB BIT(3) /* RGB or BGR byte order */
#define NT35510_DOPCTR_1_CTB BIT(2) /* Vertical scanning direction */
#define NT35510_DOPCTR_1_CRL BIT(1) /* Source driver data shift */
#define NT35510_P0_SDVPCTR_PRG BIT(2) /* 0 = normal operation, 1 = VGLO */
#define NT35510_P0_SDVPCTR_AVDD 0 /* source driver output = AVDD */
#define NT35510_P0_SDVPCTR_OFFCOL 1 /* source driver output = off color */
#define NT35510_P0_SDVPCTR_AVSS 2 /* source driver output = AVSS */
#define NT35510_P0_SDVPCTR_HI_Z 3 /* source driver output = High impedance */

/*
 * These manufacturer commands are available after we enable manufacturer
 * command set (MCS) for page 1.
 */
#define NT35510_P1_SETAVDD 0xB0
#define NT35510_P1_SETAVEE 0xB1
#define NT35510_P1_SETVCL 0xB2
#define NT35510_P1_SETVGH 0xB3
#define NT35510_P1_SETVRGH 0xB4
#define NT35510_P1_SETVGL 0xB5
#define NT35510_P1_BT1CTR 0xB6
#define NT35510_P1_BT2CTR 0xB7
#define NT35510_P1_BT3CTR 0xB8
#define NT35510_P1_BT4CTR 0xB9 /* VGH boosting times/freq */
#define NT35510_P1_BT5CTR 0xBA
#define NT35510_P1_PFMCTR 0xBB
#define NT35510_P1_SETVGP 0xBC
#define NT35510_P1_SETVGN 0xBD
#define NT35510_P1_SETVCMOFF 0xBE
#define NT35510_P1_VGHCTR 0xBF /* VGH output ctrl */
#define NT35510_P1_SET_GAMMA_RED_POS 0xD1
#define NT35510_P1_SET_GAMMA_GREEN_POS 0xD2
#define NT35510_P1_SET_GAMMA_BLUE_POS 0xD3
#define NT35510_P1_SET_GAMMA_RED_NEG 0xD4
#define NT35510_P1_SET_GAMMA_GREEN_NEG 0xD5
#define NT35510_P1_SET_GAMMA_BLUE_NEG 0xD6

/* AVDD and AVEE setting 3 bytes */
#define NT35510_P1_AVDD_LEN 3
#define NT35510_P1_AVEE_LEN 3
#define NT35510_P1_VCL_LEN 3
#define NT35510_P1_VGH_LEN 3
#define NT35510_P1_VGL_LEN 3
#define NT35510_P1_VGP_LEN 3
#define NT35510_P1_VGN_LEN 3
#define NT35510_P1_VCMOFF_LEN 2
/* BT1CTR thru BT5CTR setting 3 bytes */
#define NT35510_P1_BT1CTR_LEN 3
#define NT35510_P1_BT2CTR_LEN 3
#define NT35510_P1_BT3CTR_LEN 3
#define NT35510_P1_BT4CTR_LEN 3
#define NT35510_P1_BT5CTR_LEN 3
/* 52 gamma parameters times two per color: positive and negative */
#define NT35510_P1_GAMMA_LEN 52

#define NT35510_WRCTRLD_BCTRL BIT(5)
#define NT35510_WRCTRLD_A BIT(4)
#define NT35510_WRCTRLD_DD BIT(3)
#define NT35510_WRCTRLD_BL BIT(2)
#define NT35510_WRCTRLD_DB BIT(1)
#define NT35510_WRCTRLD_G BIT(0)

#define NT35510_WRCABC_OFF 0
#define NT35510_WRCABC_UI_MODE 1
#define NT35510_WRCABC_STILL_MODE 2
#define NT35510_WRCABC_MOVING_MODE 3

/**
 * struct nt35510_config - the display-specific NT35510 configuration
 *
 * Some of the settings provide an array of bytes, A, B C which mean:
 * A = normal / idle off mode
 * B = idle on mode
 * C = partial / idle off mode
 *
 * Gamma correction arrays are 10bit numbers, two consecutive bytes
 * makes out one point on the gamma correction curve. The points are
 * not linearly placed along the X axis, we get points 0, 1, 3, 5
 * 7, 11, 15, 23, 31, 47, 63, 95, 127, 128, 160, 192, 208, 224, 232,
 * 240, 244, 248, 250, 252, 254, 255. The voltages tuples form
 * V0, V1, V3 ... V255, with 0x0000 being the lowest voltage and
 * 0x03FF being the highest voltage.
 *
 * Each value must be strictly higher than the previous value forming
 * a rising curve like this:
 *
 * ^
 * |                                        V255
 * |                                 V254
 * |                         ....
 * |                    V5
 * |           V3
 * |     V1
 * | V0
 * +------------------------------------------->
 *
 * The details about all settings can be found in the NT35510 Application
 * Note.
 */
struct nt35510_config {
	/**
	 * @width_mm: physical panel width [mm]
	 */
	u32 width_mm;
	/**
	 * @height_mm: physical panel height [mm]
	 */
	u32 height_mm;
	/**
	 * @mode: the display mode. This is only relevant outside the panel
	 * in video mode: in command mode this is configuring the internal
	 * timing in the display controller.
	 */
	const struct drm_display_mode mode;
	/**
	 * @mode_flags: DSI operation mode related flags
	 */
	unsigned long mode_flags;
	/**
	 * @cmds: enable DSI commands
	 */
	u32 cmds;
	/**
	 * @avdd: setting for AVDD ranging from 0x00 = 6.5V to 0x14 = 4.5V
	 * in 0.1V steps the default is 0x05 which means 6.0V
	 */
	u8 avdd[NT35510_P1_AVDD_LEN];
	/**
	 * @bt1ctr: setting for boost power control for the AVDD step-up
	 * circuit (1)
	 * bits 0..2 in the lower nibble controls PCK, the booster clock
	 * frequency for the step-up circuit:
	 * 0 = Hsync/32
	 * 1 = Hsync/16
	 * 2 = Hsync/8
	 * 3 = Hsync/4
	 * 4 = Hsync/2
	 * 5 = Hsync
	 * 6 = Hsync x 2
	 * 7 = Hsync x 4
	 * bits 4..6 in the upper nibble controls BTP, the boosting
	 * amplification for the step-up circuit:
	 * 0 = Disable
	 * 1 = 1.5 x VDDB
	 * 2 = 1.66 x VDDB
	 * 3 = 2 x VDDB
	 * 4 = 2.5 x VDDB
	 * 5 = 3 x VDDB
	 * The defaults are 4 and 4 yielding 0x44
	 */
	u8 bt1ctr[NT35510_P1_BT1CTR_LEN];
	/**
	 * @avee: setting for AVEE ranging from 0x00 = -6.5V to 0x14 = -4.5V
	 * in 0.1V steps the default is 0x05 which means -6.0V
	 */
	u8 avee[NT35510_P1_AVEE_LEN];
	/**
	 * @bt2ctr: setting for boost power control for the AVEE step-up
	 * circuit (2)
	 * bits 0..2 in the lower nibble controls NCK, the booster clock
	 * frequency, the values are the same as for PCK in @bt1ctr.
	 * bits 4..5 in the upper nibble controls BTN, the boosting
	 * amplification for the step-up circuit.
	 * 0 = Disable
	 * 1 = -1.5 x VDDB
	 * 2 = -2 x VDDB
	 * 3 = -2.5 x VDDB
	 * 4 = -3 x VDDB
	 * The defaults are 4 and 3 yielding 0x34
	 */
	u8 bt2ctr[NT35510_P1_BT2CTR_LEN];
	/**
	 * @vcl: setting for VCL ranging from 0x00 = -2.5V to 0x11 = -4.0V
	 * in 1V steps, the default is 0x00 which means -2.5V
	 */
	u8 vcl[NT35510_P1_VCL_LEN];
	/**
	 * @bt3ctr: setting for boost power control for the VCL step-up
	 * circuit (3)
	 * bits 0..2 in the lower nibble controls CLCK, the booster clock
	 * frequency, the values are the same as for PCK in @bt1ctr.
	 * bits 4..5 in the upper nibble controls BTCL, the boosting
	 * amplification for the step-up circuit.
	 * 0 = Disable
	 * 1 = -0.5 x VDDB
	 * 2 = -1 x VDDB
	 * 3 = -2 x VDDB
	 * The defaults are 4 and 2 yielding 0x24
	 */
	u8 bt3ctr[NT35510_P1_BT3CTR_LEN];
	/**
	 * @vgh: setting for VGH ranging from 0x00 = 7.0V to 0x0B = 18.0V
	 * in 1V steps, the default is 0x08 which means 15V
	 */
	u8 vgh[NT35510_P1_VGH_LEN];
	/**
	 * @bt4ctr: setting for boost power control for the VGH step-up
	 * circuit (4)
	 * bits 0..2 in the lower nibble controls HCK, the booster clock
	 * frequency, the values are the same as for PCK in @bt1ctr.
	 * bits 4..5 in the upper nibble controls BTH, the boosting
	 * amplification for the step-up circuit.
	 * 0 = AVDD + VDDB
	 * 1 = AVDD - AVEE
	 * 2 = AVDD - AVEE + VDDB
	 * 3 = AVDD x 2 - AVEE
	 * The defaults are 4 and 3 yielding 0x34
	 */
	u8 bt4ctr[NT35510_P1_BT4CTR_LEN];
	/**
	 * @vgl: setting for VGL ranging from 0x00 = -2V to 0x0f = -15V in
	 * 1V steps, the default is 0x08 which means -10V
	 */
	u8 vgl[NT35510_P1_VGL_LEN];
	/**
	 * @bt5ctr: setting for boost power control for the VGL step-up
	 * circuit (5)
	 * bits 0..2 in the lower nibble controls LCK, the booster clock
	 * frequency, the values are the same as for PCK in @bt1ctr.
	 * bits 4..5 in the upper nibble controls BTL, the boosting
	 * amplification for the step-up circuit.
	 * 0 = AVEE + VCL
	 * 1 = AVEE - AVDD
	 * 2 = AVEE + VCL - AVDD
	 * 3 = AVEE x 2 - AVDD
	 * The defaults are 3 and 2 yielding 0x32
	 */
	u8 bt5ctr[NT35510_P1_BT5CTR_LEN];
	/**
	 * @vgp: setting for VGP, the positive gamma divider voltages
	 * VGMP the high voltage and VGSP the low voltage.
	 * The first byte contains bit 8 of VGMP and VGSP in bits 4 and 0
	 * The second byte contains bit 0..7 of VGMP
	 * The third byte contains bit 0..7 of VGSP
	 * VGMP 0x00 = 3.0V .. 0x108 = 6.3V in steps of 12.5mV
	 * VGSP 0x00 = 0V .. 0x111 = 3.7V in steps of 12.5mV
	 */
	u8 vgp[NT35510_P1_VGP_LEN];
	/**
	 * @vgn: setting for VGN, the negative gamma divider voltages,
	 * same layout of bytes as @vgp.
	 */
	u8 vgn[NT35510_P1_VGN_LEN];
	/**
	 * @vcmoff: setting the DC VCOM offset voltage
	 * The first byte contains bit 8 of VCM in bit 0 and VCMOFFSEL in bit 4.
	 * The second byte contains bits 0..7 of VCM.
	 * VCMOFFSEL the common voltage offset mode.
	 * VCMOFFSEL 0x00 = VCOM .. 0x01 Gamma.
	 * The default is 0x00.
	 * VCM the VCOM output voltage (VCMOFFSEL = 0) or the internal register
	 * offset for gamma voltage (VCMOFFSEL = 1).
	 * VCM 0x00 = 0V/0 .. 0x118 = 3.5V/280 in steps of 12.5mV/1step
	 * The default is 0x00 = 0V/0.
	 */
	u8 vcmoff[NT35510_P1_VCMOFF_LEN];
	/**
	 * @dopctr: setting optional control for display
	 * ERR bits 0..1 in the first byte is the ERR pin output signal setting.
	 * 0 = Disable, ERR pin output low
	 * 1 = ERR pin output CRC error only
	 * 2 = ERR pin output ECC error only
	 * 3 = ERR pin output CRC and ECC error
	 * The default is 0.
	 * N565 bit 2 in the first byte is the 16-bit/pixel format selection.
	 * 0 = R[4:0] + G[5:3] & G[2:0] + B[4:0]
	 * 1 = G[2:0] + R[4:0] & B[4:0] + G[5:3]
	 * The default is 0.
	 * DIS_EoTP_HS bit 3 in the first byte is "DSI protocol violation" error
	 * reporting.
	 * 0 = reporting when error
	 * 1 = not reporting when error
	 * DSIM bit 4 in the first byte is the video mode data type enable
	 * 0 = Video mode data type disable
	 * 1 = Video mode data type enable
	 * The default is 0.
	 * DSIG bit 5 int the first byte is the generic r/w data type enable
	 * 0 = Generic r/w disable
	 * 1 = Generic r/w enable
	 * The default is 0.
	 * DSITE bit 6 in the first byte is TE line enable
	 * 0 = TE line is disabled
	 * 1 = TE line is enabled
	 * The default is 0.
	 * RAMKP bit 7 in the first byte is the frame memory keep/loss in
	 * sleep-in mode
	 * 0 = contents loss in sleep-in
	 * 1 = contents keep in sleep-in
	 * The default is 0.
	 * CRL bit 1 in the second byte is the source driver data shift
	 * direction selection. This bit is XOR operation with bit RSMX
	 * of 3600h command.
	 * 0 (RMSX = 0) = S1 -> S1440
	 * 0 (RMSX = 1) = S1440 -> S1
	 * 1 (RMSX = 0) = S1440 -> S1
	 * 1 (RMSX = 1) = S1 -> S1440
	 * The default is 0.
	 * CTB bit 2 in the second byte is the vertical scanning direction
	 * selection for gate control signals. This bit is XOR operation
	 * with bit ML of 3600h command.
	 * 0 (ML = 0) = Forward (top -> bottom)
	 * 0 (ML = 1) = Reverse (bottom -> top)
	 * 1 (ML = 0) = Reverse (bottom -> top)
	 * 1 (ML = 1) = Forward (top -> bottom)
	 * The default is 0.
	 * CRGB bit 3 in the second byte is RGB-BGR order selection. This
	 * bit is XOR operation with bit RGB of 3600h command.
	 * 0 (RGB = 0) = RGB/Normal
	 * 0 (RGB = 1) = BGR/RB swap
	 * 1 (RGB = 0) = BGR/RB swap
	 * 1 (RGB = 1) = RGB/Normal
	 * The default is 0.
	 * TE_PWR_SEL bit 4 in the second byte is the TE output voltage
	 * level selection (only valid when DSTB_SEL = 0 or DSTB_SEL = 1,
	 * VSEL = High and VDDI = 1.665~3.3V).
	 * 0 = TE output voltage level is VDDI
	 * 1 = TE output voltage level is VDDA
	 * The default is 0.
	 */
	u8 dopctr[NT35510_P0_DOPCTR_LEN];
	/**
	 * @madctl: Memory data access control
	 * RSMY bit 0 is flip vertical. Flips the display image top to down.
	 * RSMX bit 1 is flip horizontal. Flips the display image left to right.
	 * MH bit 2 is the horizontal refresh order.
	 * RGB bit 3 is the RGB-BGR order.
	 * 0 = RGB color sequence
	 * 1 = BGR color sequence
	 * ML bit 4 is the vertical refresh order.
	 * MV bit 5 is the row/column exchange.
	 * MX bit 6 is the column address order.
	 * MY bit 7 is the row address order.
	 */
	u8 madctl;
	/**
	 * @sdhdtctr: source output data hold time
	 * 0x00..0x3F = 0..31.5us in steps of 0.5us
	 * The default is 0x05 = 2.5us.
	 */
	u8 sdhdtctr;
	/**
	 * @gseqctr: EQ control for gate signals
	 * GFEQ_XX[3:0]: time setting of EQ step for falling edge in steps
	 * of 0.5us.
	 * The default is 0x07 = 3.5us
	 * GREQ_XX[7:4]: time setting of EQ step for rising edge in steps
	 * of 0.5us.
	 * The default is 0x07 = 3.5us
	 */
	u8 gseqctr[NT35510_P0_GSEQCTR_LEN];
	/**
	 * @sdeqctr: Source driver control settings, first byte is
	 * 0 for mode 1 and 1 for mode 2. Mode 1 uses two steps and
	 * mode 2 uses three steps meaning EQS3 is not used in mode
	 * 1. Mode 2 is default. The last three parameters are EQS1, EQS2
	 * and EQS3, setting the rise time for each equalizer step:
	 * 0x00 = 0.0 us to 0x0f = 7.5 us in steps of 0.5us. The default
	 * is 0x07 = 3.5 us.
	 */
	u8 sdeqctr[NT35510_P0_SDEQCTR_LEN];
	/**
	 * @sdvpctr: power/voltage behaviour during vertical porch time
	 */
	u8 sdvpctr;
	/**
	 * @t1: the number of pixel clocks on one scanline, range
	 * 0x100 (258 ticks) .. 0x3FF (1024 ticks) so the value + 1
	 * clock ticks.
	 */
	u16 t1;
	/**
	 * @vbp: vertical back porch toward the PANEL note: not toward
	 * the DSI host; these are separate interfaces, in from DSI host
	 * and out to the panel.
	 */
	u8 vbp;
	/**
	 * @vfp: vertical front porch toward the PANEL.
	 */
	u8 vfp;
	/**
	 * @psel: pixel clock divisor: 0 = 1, 1 = 2, 2 = 4, 3 = 8.
	 */
	u8 psel;
	/**
	 * @dpmctr12: Display timing control 12
	 * Byte 1 bit 4 selects LVGL voltage level: 0 = VGLX, 1 = VGL_REG
	 * Byte 1 bit 1 selects gate signal mode: 0 = non-overlap, 1 = overlap
	 * Byte 1 bit 0 selects output signal control R/L swap, 0 = normal
	 * 1 = swap all O->E, L->R
	 * Byte 2 is CLW delay clock for CK O/E and CKB O/E signals:
	 * 0x00 = 0us .. 0xFF = 12.75us in 0.05us steps
	 * Byte 3 is FTI_H0 delay time for STP O/E signals:
	 * 0x00 = 0us .. 0xFF = 12.75us in 0.05us steps
	 */
	u8 dpmctr12[NT35510_P0_DPMCTR12_LEN];
	/**
	 * @gamma_corr_pos_r: Red gamma correction parameters, positive
	 */
	u8 gamma_corr_pos_r[NT35510_P1_GAMMA_LEN];
	/**
	 * @gamma_corr_pos_g: Green gamma correction parameters, positive
	 */
	u8 gamma_corr_pos_g[NT35510_P1_GAMMA_LEN];
	/**
	 * @gamma_corr_pos_b: Blue gamma correction parameters, positive
	 */
	u8 gamma_corr_pos_b[NT35510_P1_GAMMA_LEN];
	/**
	 * @gamma_corr_neg_r: Red gamma correction parameters, negative
	 */
	u8 gamma_corr_neg_r[NT35510_P1_GAMMA_LEN];
	/**
	 * @gamma_corr_neg_g: Green gamma correction parameters, negative
	 */
	u8 gamma_corr_neg_g[NT35510_P1_GAMMA_LEN];
	/**
	 * @gamma_corr_neg_b: Blue gamma correction parameters, negative
	 */
	u8 gamma_corr_neg_b[NT35510_P1_GAMMA_LEN];
	/**
	 * @wrdisbv: write display brightness
	 * 0x00 value means the lowest brightness and 0xff value means
	 * the highest brightness.
	 * The default is 0x00.
	 */
	u8 wrdisbv;
	/**
	 * @wrctrld: write control display
	 * G bit 0 selects gamma curve: 0 = Manual, 1 = Automatic
	 * DB bit 1 selects display brightness: 0 = Manual, 1 = Automatic
	 * BL bit 2 controls backlight control: 0 = Off, 1 = On
	 * DD bit 3 controls display dimming: 0 = Off, 1 = On
	 * A bit 4 controls LABC block: 0 = Off, 1 = On
	 * BCTRL bit 5 controls brightness block: 0 = Off, 1 = On
	 */
	u8 wrctrld;
	/**
	 * @wrcabc: write content adaptive brightness control
	 * There is possible to use 4 different modes for content adaptive
	 * image functionality:
	 * 0: Off
	 * 1: User Interface Image (UI-Mode)
	 * 2: Still Picture Image (Still-Mode)
	 * 3: Moving Picture Image (Moving-Mode)
	 * The default is 0
	 */
	u8 wrcabc;
	/**
	 * @wrcabcmb: write CABC minimum brightness
	 * Set the minimum brightness value of the display for CABC
	 * function.
	 * 0x00 value means the lowest brightness for CABC and 0xff
	 * value means the highest brightness for CABC.
	 * The default is 0x00.
	 */
	u8 wrcabcmb;
};

/**
 * struct nt35510 - state container for the NT35510 panel
 */
struct nt35510 {
	/**
	 * @dev: the container device
	 */
	struct device *dev;
	/**
	 * @conf: the specific panel configuration, as the NT35510
	 * can be combined with many physical panels, they can have
	 * different physical dimensions and gamma correction etc,
	 * so this is stored in the config.
	 */
	const struct nt35510_config *conf;
	/**
	 * @panel: the DRM panel object for the instance
	 */
	struct drm_panel panel;
	/**
	 * @supplies: regulators supplying the panel
	 */
	struct regulator_bulk_data supplies[2];
	/**
	 * @reset_gpio: the reset line
	 */
	struct gpio_desc *reset_gpio;
};

/* Manufacturer command has strictly this byte sequence */
static const u8 nt35510_mauc_mtp_read_param[] = { 0xAA, 0x55, 0x25, 0x01 };
static const u8 nt35510_mauc_mtp_read_setting[] = { 0x01, 0x02, 0x00, 0x20,
						    0x33, 0x13, 0x00, 0x40,
						    0x00, 0x00, 0x23, 0x02 };
static const u8 nt35510_mauc_select_page_0[] = { 0x55, 0xAA, 0x52, 0x08, 0x00 };
static const u8 nt35510_mauc_select_page_1[] = { 0x55, 0xAA, 0x52, 0x08, 0x01 };
static const u8 nt35510_vgh_on[] = { 0x01 };

static inline struct nt35510 *panel_to_nt35510(struct drm_panel *panel)
{
	return container_of(panel, struct nt35510, panel);
}

#define NT35510_ROTATE_0_SETTING	0x02
#define NT35510_ROTATE_180_SETTING	0x00

static int nt35510_send_long(struct nt35510 *nt, struct mipi_dsi_device *dsi,
			     u8 cmd, u8 cmdlen, const u8 *seq)
{
	const u8 *seqp = seq;
	int cmdwritten = 0;
	int chunk = cmdlen;
	int ret;

	if (chunk > 15)
		chunk = 15;
	ret = mipi_dsi_dcs_write(dsi, cmd, seqp, chunk);
	if (ret < 0) {
		dev_err(nt->dev, "error sending DCS command seq cmd %02x\n", cmd);
		return ret;
	}
	cmdwritten += chunk;
	seqp += chunk;

	while (cmdwritten < cmdlen) {
		chunk = cmdlen - cmdwritten;
		if (chunk > 15)
			chunk = 15;
		ret = mipi_dsi_generic_write(dsi, seqp, chunk);
		if (ret < 0) {
			dev_err(nt->dev, "error sending generic write seq %02x\n", cmd);
			return ret;
		}
		cmdwritten += chunk;
		seqp += chunk;
	}
	dev_dbg(nt->dev, "sent command %02x %02x bytes\n", cmd, cmdlen);
	return 0;
}

static int nt35510_read_id(struct nt35510 *nt)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	u8 id1, id2, id3;
	int ret;

	ret = mipi_dsi_dcs_read(dsi, MCS_CMD_READ_ID1, &id1, 1);
	if (ret < 0) {
		dev_err(nt->dev, "could not read MTP ID1\n");
		return ret;
	}
	ret = mipi_dsi_dcs_read(dsi, MCS_CMD_READ_ID2, &id2, 1);
	if (ret < 0) {
		dev_err(nt->dev, "could not read MTP ID2\n");
		return ret;
	}
	ret = mipi_dsi_dcs_read(dsi, MCS_CMD_READ_ID3, &id3, 1);
	if (ret < 0) {
		dev_err(nt->dev, "could not read MTP ID3\n");
		return ret;
	}

	/*
	 * Multi-Time Programmable (?) memory contains manufacturer
	 * ID (e.g. Hydis 0x55), driver ID (e.g. NT35510 0xc0) and
	 * version.
	 */
	dev_info(nt->dev, "MTP ID manufacturer: %02x version: %02x driver: %02x\n", id1, id2, id3);

	return 0;
}

/**
 * nt35510_setup_power() - set up power config in page 1
 * @nt: the display instance to set up
 */
static int nt35510_setup_power(struct nt35510 *nt)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	int ret;

	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETAVDD,
				NT35510_P1_AVDD_LEN,
				nt->conf->avdd);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_BT1CTR,
				NT35510_P1_BT1CTR_LEN,
				nt->conf->bt1ctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETAVEE,
				NT35510_P1_AVEE_LEN,
				nt->conf->avee);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_BT2CTR,
				NT35510_P1_BT2CTR_LEN,
				nt->conf->bt2ctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVCL,
				NT35510_P1_VCL_LEN,
				nt->conf->vcl);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_BT3CTR,
				NT35510_P1_BT3CTR_LEN,
				nt->conf->bt3ctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVGH,
				NT35510_P1_VGH_LEN,
				nt->conf->vgh);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_BT4CTR,
				NT35510_P1_BT4CTR_LEN,
				nt->conf->bt4ctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_VGHCTR,
				ARRAY_SIZE(nt35510_vgh_on),
				nt35510_vgh_on);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVGL,
				NT35510_P1_VGL_LEN,
				nt->conf->vgl);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_BT5CTR,
				NT35510_P1_BT5CTR_LEN,
				nt->conf->bt5ctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVGP,
				NT35510_P1_VGP_LEN,
				nt->conf->vgp);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVGN,
				NT35510_P1_VGN_LEN,
				nt->conf->vgn);
	if (ret)
		return ret;

	if (nt->conf->cmds & NT35510_CMD_SETVCMOFF) {
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SETVCMOFF,
					NT35510_P1_VCMOFF_LEN,
					nt->conf->vcmoff);
		if (ret)
			return ret;
	}

	/* Typically 10 ms */
	usleep_range(10000, 20000);

	return 0;
}

/**
 * nt35510_setup_display() - set up display config in page 0
 * @nt: the display instance to set up
 */
static int nt35510_setup_display(struct nt35510 *nt)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	const struct nt35510_config *conf = nt->conf;
	u8 dpfrctr[NT35510_P0_DPFRCTR1_LEN];
	int ret;

	ret = nt35510_send_long(nt, dsi, NT35510_P0_DOPCTR,
				NT35510_P0_DOPCTR_LEN,
				conf->dopctr);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_ADDRESS_MODE, &conf->madctl,
				 sizeof(conf->madctl));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, NT35510_P0_SDHDTCTR, &conf->sdhdtctr,
				 sizeof(conf->sdhdtctr));
	if (ret < 0)
		return ret;

	ret = nt35510_send_long(nt, dsi, NT35510_P0_GSEQCTR,
				NT35510_P0_GSEQCTR_LEN,
				conf->gseqctr);
	if (ret)
		return ret;

	ret = nt35510_send_long(nt, dsi, NT35510_P0_SDEQCTR,
				NT35510_P0_SDEQCTR_LEN,
				conf->sdeqctr);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, NT35510_P0_SDVPCTR,
				 &conf->sdvpctr, 1);
	if (ret < 0)
		return ret;

	/*
	 * Display timing control for active and idle off mode:
	 * the first byte contains
	 * the two high bits of T1A and second byte the low 8 bits, and
	 * the valid range is 0x100 (257) to 0x3ff (1023) representing
	 * 258..1024 (+1) pixel clock ticks for one scanline. At 20MHz pixel
	 * clock this covers the range of 12.90us .. 51.20us in steps of
	 * 0.05us, the default is 0x184 (388) representing 389 ticks.
	 * The third byte is VBPDA, vertical back porch display active
	 * and the fourth VFPDA, vertical front porch display active,
	 * both given in number of scanlines in the range 0x02..0xff
	 * for 2..255 scanlines. The fifth byte is 2 bits selecting
	 * PSEL for active and idle off mode, how much the 20MHz clock
	 * is divided by 0..3.  This needs to be adjusted to get the right
	 * frame rate.
	 */
	dpfrctr[0] = (conf->t1 >> 8) & 0xFF;
	dpfrctr[1] = conf->t1 & 0xFF;
	/* Vertical back porch */
	dpfrctr[2] = conf->vbp;
	/* Vertical front porch */
	dpfrctr[3] = conf->vfp;
	dpfrctr[4] = conf->psel;
	ret = nt35510_send_long(nt, dsi, NT35510_P0_DPFRCTR1,
				NT35510_P0_DPFRCTR1_LEN,
				dpfrctr);
	if (ret)
		return ret;
	/* For idle and partial idle off mode we decrease front porch by one */
	dpfrctr[3]--;
	ret = nt35510_send_long(nt, dsi, NT35510_P0_DPFRCTR2,
				NT35510_P0_DPFRCTR2_LEN,
				dpfrctr);
	if (ret)
		return ret;
	ret = nt35510_send_long(nt, dsi, NT35510_P0_DPFRCTR3,
				NT35510_P0_DPFRCTR3_LEN,
				dpfrctr);
	if (ret)
		return ret;

	/* Enable TE on vblank */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret)
		return ret;

	/* Turn on the pads? */
	ret = nt35510_send_long(nt, dsi, NT35510_P0_DPMCTR12,
				NT35510_P0_DPMCTR12_LEN,
				conf->dpmctr12);
	if (ret)
		return ret;

	return 0;
}

static int nt35510_set_brightness(struct backlight_device *bl)
{
	struct nt35510 *nt = bl_get_data(bl);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	u8 brightness = bl->props.brightness;
	int ret;

	dev_dbg(nt->dev, "set brightness %d\n", brightness);
	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				 &brightness,
				 sizeof(brightness));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops nt35510_bl_ops = {
	.update_status = nt35510_set_brightness,
};

/*
 * This power-on sequence
 */
static int nt35510_power_on(struct nt35510 *nt)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt->supplies), nt->supplies);
	if (ret < 0) {
		dev_err(nt->dev, "unable to enable regulators\n");
		return ret;
	}

	/* Toggle RESET in accordance with datasheet page 370 */
	if (nt->reset_gpio) {
		gpiod_set_value(nt->reset_gpio, 1);
		/* Active min 10 us according to datasheet, let's say 20 */
		usleep_range(20, 1000);
		gpiod_set_value(nt->reset_gpio, 0);
		/*
		 * 5 ms during sleep mode, 120 ms during sleep out mode
		 * according to datasheet, let's use 120-140 ms.
		 */
		usleep_range(120000, 140000);
	}

	ret = nt35510_send_long(nt, dsi, MCS_CMD_MTP_READ_PARAM,
				ARRAY_SIZE(nt35510_mauc_mtp_read_param),
				nt35510_mauc_mtp_read_param);
	if (ret)
		return ret;

	ret = nt35510_send_long(nt, dsi, MCS_CMD_MTP_READ_SETTING,
				ARRAY_SIZE(nt35510_mauc_mtp_read_setting),
				nt35510_mauc_mtp_read_setting);
	if (ret)
		return ret;

	nt35510_read_id(nt);

	/* Set up stuff in  manufacturer control, page 1 */
	ret = nt35510_send_long(nt, dsi, MCS_CMD_MAUCCTR,
				ARRAY_SIZE(nt35510_mauc_select_page_1),
				nt35510_mauc_select_page_1);
	if (ret)
		return ret;

	ret = nt35510_setup_power(nt);
	if (ret)
		return ret;

	if (nt->conf->cmds & NT35510_CMD_CORRECT_GAMMA) {
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_RED_POS,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_pos_r);
		if (ret)
			return ret;
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_GREEN_POS,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_pos_g);
		if (ret)
			return ret;
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_BLUE_POS,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_pos_b);
		if (ret)
			return ret;
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_RED_NEG,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_neg_r);
		if (ret)
			return ret;
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_GREEN_NEG,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_neg_g);
		if (ret)
			return ret;
		ret = nt35510_send_long(nt, dsi, NT35510_P1_SET_GAMMA_BLUE_NEG,
					NT35510_P1_GAMMA_LEN,
					nt->conf->gamma_corr_neg_b);
		if (ret)
			return ret;
	}

	/* Set up stuff in  manufacturer control, page 0 */
	ret = nt35510_send_long(nt, dsi, MCS_CMD_MAUCCTR,
				ARRAY_SIZE(nt35510_mauc_select_page_0),
				nt35510_mauc_select_page_0);
	if (ret)
		return ret;

	ret = nt35510_setup_display(nt);
	if (ret)
		return ret;

	return 0;
}

static int nt35510_power_off(struct nt35510 *nt)
{
	int ret;

	ret = regulator_bulk_disable(ARRAY_SIZE(nt->supplies), nt->supplies);
	if (ret)
		return ret;

	if (nt->reset_gpio)
		gpiod_set_value(nt->reset_gpio, 1);

	return 0;
}

static int nt35510_unprepare(struct drm_panel *panel)
{
	struct nt35510 *nt = panel_to_nt35510(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(nt->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}
	usleep_range(10000, 20000);

	/* Enter sleep mode */
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(nt->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	/* Wait 4 frames, how much is that 5ms in the vendor driver */
	usleep_range(5000, 10000);

	ret = nt35510_power_off(nt);
	if (ret)
		return ret;

	return 0;
}

static int nt35510_prepare(struct drm_panel *panel)
{
	struct nt35510 *nt = panel_to_nt35510(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(nt->dev);
	int ret;

	ret = nt35510_power_on(nt);
	if (ret)
		return ret;

	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(nt->dev, "failed to exit sleep mode (%d)\n", ret);
		return ret;
	}
	/* Up to 120 ms */
	usleep_range(120000, 150000);

	if (nt->conf->cmds & NT35510_CMD_CONTROL_DISPLAY) {
		ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					 &nt->conf->wrctrld,
					 sizeof(nt->conf->wrctrld));
		if (ret < 0)
			return ret;

		ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_POWER_SAVE,
					 &nt->conf->wrcabc,
					 sizeof(nt->conf->wrcabc));
		if (ret < 0)
			return ret;

		ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
					 &nt->conf->wrcabcmb,
					 sizeof(nt->conf->wrcabcmb));
		if (ret < 0)
			return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(nt->dev, "failed to turn display on (%d)\n", ret);
		return ret;
	}
	/* Some 10 ms */
	usleep_range(10000, 20000);

	return 0;
}

static int nt35510_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct nt35510 *nt = panel_to_nt35510(panel);
	struct drm_display_mode *mode;
	struct drm_display_info *info;

	info = &connector->display_info;
	info->width_mm = nt->conf->width_mm;
	info->height_mm = nt->conf->height_mm;
	mode = drm_mode_duplicate(connector->dev, &nt->conf->mode);
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	mode->width_mm = nt->conf->width_mm;
	mode->height_mm = nt->conf->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs nt35510_drm_funcs = {
	.unprepare = nt35510_unprepare,
	.prepare = nt35510_prepare,
	.get_modes = nt35510_get_modes,
};

static int nt35510_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt35510 *nt;
	int ret;

	nt = devm_drm_panel_alloc(dev, struct nt35510, panel,
				  &nt35510_drm_funcs,
				  DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(nt))
		return PTR_ERR(nt);

	mipi_dsi_set_drvdata(dsi, nt);
	nt->dev = dev;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/*
	 * Datasheet suggests max HS rate for NT35510 is 250 MHz
	 * (period time 4ns, see figure 7.6.4 page 365) and max LP rate is
	 * 20 MHz (period time 50ns, see figure 7.6.6. page 366).
	 * However these frequencies appear in source code for the Hydis
	 * HVA40WV1 panel and setting up the LP frequency makes the panel
	 * not work.
	 *
	 * TODO: if other panels prove to be closer to the datasheet,
	 * maybe make this a per-panel config in struct nt35510_config?
	 */
	dsi->hs_rate = 349440000;
	dsi->lp_rate = 9600000;

	/*
	 * Every new incarnation of this display must have a unique
	 * data entry for the system in this driver.
	 */
	nt->conf = of_device_get_match_data(dev);
	if (!nt->conf) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	dsi->mode_flags = nt->conf->mode_flags;

	nt->supplies[0].supply = "vdd"; /* 2.3-4.8 V */
	nt->supplies[1].supply = "vddi"; /* 1.65-3.3V */
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(nt->supplies),
				      nt->supplies);
	if (ret < 0)
		return ret;
	ret = regulator_set_voltage(nt->supplies[0].consumer,
				    2300000, 4800000);
	if (ret)
		return ret;
	ret = regulator_set_voltage(nt->supplies[1].consumer,
				    1650000, 3300000);
	if (ret)
		return ret;

	nt->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(nt->reset_gpio)) {
		dev_err(dev, "error getting RESET GPIO\n");
		return PTR_ERR(nt->reset_gpio);
	}

	/*
	 * First, try to locate an external backlight (such as on GPIO)
	 * if this fails, assume we will want to use the internal backlight
	 * control.
	 */
	ret = drm_panel_of_backlight(&nt->panel);
	if (ret) {
		dev_err(dev, "error getting external backlight %d\n", ret);
		return ret;
	}
	if (!nt->panel.backlight) {
		struct backlight_device *bl;

		bl = devm_backlight_device_register(dev, "nt35510", dev, nt,
						    &nt35510_bl_ops, NULL);
		if (IS_ERR(bl)) {
			dev_err(dev, "failed to register backlight device\n");
			return PTR_ERR(bl);
		}
		bl->props.max_brightness = 255;
		if (nt->conf->cmds & NT35510_CMD_CONTROL_DISPLAY)
			bl->props.brightness = nt->conf->wrdisbv;
		else
			bl->props.brightness = 255;
		bl->props.power = BACKLIGHT_POWER_OFF;
		nt->panel.backlight = bl;
	}

	drm_panel_add(&nt->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&nt->panel);

	return 0;
}

static void nt35510_remove(struct mipi_dsi_device *dsi)
{
	struct nt35510 *nt = mipi_dsi_get_drvdata(dsi);
	int ret;

	mipi_dsi_detach(dsi);
	/* Power off */
	ret = nt35510_power_off(nt);
	if (ret)
		dev_err(&dsi->dev, "Failed to power off\n");

	drm_panel_remove(&nt->panel);
}

/*
 * These gamma correction values are 10bit tuples, so only bits 0 and 1 is
 * ever used in the first byte. They form a positive and negative gamma
 * correction curve for each color, values must be strictly higher for each
 * step on the curve. As can be seen these default curves goes from 0x0001
 * to 0x03FE.
 */
#define NT35510_GAMMA_POS_DEFAULT 0x00, 0x01, 0x00, 0x43, 0x00, \
		0x6B, 0x00, 0x87, 0x00, 0xA3, 0x00, 0xCE, 0x00, 0xF1, 0x01, \
		0x27, 0x01, 0x53, 0x01, 0x98, 0x01, 0xCE, 0x02, 0x22, 0x02, \
		0x83, 0x02, 0x78, 0x02, 0x9E, 0x02, 0xDD, 0x03, 0x00, 0x03, \
		0x2E, 0x03, 0x54, 0x03, 0x7F, 0x03, 0x95, 0x03, 0xB3, 0x03, \
		0xC2, 0x03, 0xE1, 0x03, 0xF1, 0x03, 0xFE

#define NT35510_GAMMA_NEG_DEFAULT 0x00, 0x01, 0x00, 0x43, 0x00, \
		0x6B, 0x00, 0x87, 0x00, 0xA3, 0x00, 0xCE, 0x00, 0xF1, 0x01, \
		0x27, 0x01, 0x53, 0x01, 0x98, 0x01, 0xCE, 0x02, 0x22, 0x02, \
		0x43, 0x02, 0x50, 0x02, 0x9E, 0x02, 0xDD, 0x03, 0x00, 0x03, \
		0x2E, 0x03, 0x54, 0x03, 0x7F, 0x03, 0x95, 0x03, 0xB3, 0x03, \
		0xC2, 0x03, 0xE1, 0x03, 0xF1, 0x03, 0xFE

/*
 * The Hydis HVA40WV1 panel
 */
static const struct nt35510_config nt35510_hydis_hva40wv1 = {
	.width_mm = 52,
	.height_mm = 86,
	/**
	 * As the Hydis panel is used in command mode, the porches etc
	 * are settings programmed internally into the NT35510 controller
	 * and generated toward the physical display. As the panel is not
	 * used in video mode, these are not really exposed to the DSI
	 * host.
	 *
	 * Display frame rate control:
	 * Frame rate = (20 MHz / 1) / (389 * (7 + 50 + 800)) ~= 60 Hz
	 */
	.mode = {
		/* The internal pixel clock of the NT35510 is 20 MHz */
		.clock = 20000,
		.hdisplay = 480,
		.hsync_start = 480 + 2, /* HFP = 2 */
		.hsync_end = 480 + 2 + 0, /* HSync = 0 */
		.htotal = 480 + 2 + 0 + 5, /* HBP = 5 */
		.vdisplay = 800,
		.vsync_start = 800 + 2, /* VFP = 2 */
		.vsync_end = 800 + 2 + 0, /* VSync = 0 */
		.vtotal = 800 + 2 + 0 + 5, /* VBP = 5 */
		.flags = 0,
	},
	.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.cmds = NT35510_CMD_CORRECT_GAMMA,
	/* 0x09: AVDD = 5.6V */
	.avdd = { 0x09, 0x09, 0x09 },
	/* 0x34: PCK = Hsync/2, BTP = 2 x VDDB */
	.bt1ctr = { 0x34, 0x34, 0x34 },
	/* 0x09: AVEE = -5.6V */
	.avee = { 0x09, 0x09, 0x09 },
	/* 0x24: NCK = Hsync/2, BTN =  -2 x VDDB */
	.bt2ctr = { 0x24, 0x24, 0x24 },
	/* VBCLA: -2.5V, VBCLB: -2.5V, VBCLC: -2.5V */
	.vcl = { 0x00, 0x00, 0x00 },
	/* 0x24: CLCK = Hsync/2, BTN =  -1 x VDDB */
	.bt3ctr = { 0x24, 0x24, 0x24 },
	/* 0x05 = 12V */
	.vgh = { 0x05, 0x05, 0x05 },
	/* 0x24: NCKA = Hsync/2, VGH = 2 x AVDD - AVEE */
	.bt4ctr = { 0x24, 0x24, 0x24 },
	/* 0x0B = -13V */
	.vgl = { 0x0B, 0x0B, 0x0B },
	/* 0x24: LCKA = Hsync, VGL = AVDD + VCL - AVDD */
	.bt5ctr = { 0x24, 0x24, 0x24 },
	/* VGMP: 0x0A3 = 5.0375V, VGSP = 0V */
	.vgp = { 0x00, 0xA3, 0x00 },
	/* VGMP: 0x0A3 = 5.0375V, VGSP = 0V */
	.vgn = { 0x00, 0xA3, 0x00 },
	/* VCMOFFSEL = VCOM voltage offset mode, VCM = 0V */
	.vcmoff = { 0x00, 0x00 },
	/* Enable TE, EoTP and RGB pixel format */
	.dopctr = { NT35510_DOPCTR_0_DSITE | NT35510_DOPCTR_0_EOTP |
		    NT35510_DOPCTR_0_N565, NT35510_DOPCTR_1_CTB },
	.madctl = NT35510_ROTATE_0_SETTING,
	/* 0x0A: SDT = 5 us */
	.sdhdtctr = 0x0A,
	/* EQ control for gate signals, 0x00 = 0 us */
	.gseqctr = { 0x00, 0x00 },
	/* SDEQCTR: source driver EQ mode 2, 2.5 us rise time on each step */
	.sdeqctr = { 0x01, 0x05, 0x05, 0x05 },
	/* SDVPCTR: Normal operation off color during v porch */
	.sdvpctr = 0x01,
	/* T1: number of pixel clocks on one scanline: 0x184 = 389 clocks */
	.t1 = 0x0184,
	/* VBP: vertical back porch toward the panel */
	.vbp = 7,
	/* VFP: vertical front porch toward the panel */
	.vfp = 50,
	/* PSEL: divide pixel clock 20MHz with 1 (no clock downscaling) */
	.psel = 0,
	/* DPTMCTR12: 0x03: LVGL = VGLX, overlap mode, swap R->L O->E */
	.dpmctr12 = { 0x03, 0x00, 0x00, },
	/* Default gamma correction values */
	.gamma_corr_pos_r = { NT35510_GAMMA_POS_DEFAULT },
	.gamma_corr_pos_g = { NT35510_GAMMA_POS_DEFAULT },
	.gamma_corr_pos_b = { NT35510_GAMMA_POS_DEFAULT },
	.gamma_corr_neg_r = { NT35510_GAMMA_NEG_DEFAULT },
	.gamma_corr_neg_g = { NT35510_GAMMA_NEG_DEFAULT },
	.gamma_corr_neg_b = { NT35510_GAMMA_NEG_DEFAULT },
};

static const struct nt35510_config nt35510_frida_frd400b25025 = {
	.width_mm = 52,
	.height_mm = 86,
	.mode = {
		.clock = 23000,
		.hdisplay = 480,
		.hsync_start = 480 + 34, /* HFP = 34 */
		.hsync_end = 480 + 34 + 2, /* HSync = 2 */
		.htotal = 480 + 34 + 2 + 34, /* HBP = 34 */
		.vdisplay = 800,
		.vsync_start = 800 + 15, /* VFP = 15 */
		.vsync_end = 800 + 15 + 12, /* VSync = 12 */
		.vtotal = 800 + 15 + 12 + 15, /* VBP = 15 */
		.flags = 0,
	},
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_LPM,
	.cmds = NT35510_CMD_CONTROL_DISPLAY | NT35510_CMD_SETVCMOFF,
	/* 0x03: AVDD = 6.2V */
	.avdd = { 0x03, 0x03, 0x03 },
	/* 0x46: PCK = 2 x Hsync, BTP = 2.5 x VDDB */
	.bt1ctr = { 0x46, 0x46, 0x46 },
	/* 0x03: AVEE = -6.2V */
	.avee = { 0x03, 0x03, 0x03 },
	/* 0x36: PCK = 2 x Hsync, BTP =  2 x VDDB */
	.bt2ctr = { 0x36, 0x36, 0x36 },
	/* VBCLA: -2.5V, VBCLB: -2.5V, VBCLC: -3.5V */
	.vcl = { 0x00, 0x00, 0x02 },
	/* 0x26: CLCK = 2 x Hsync, BTN =  -1 x VDDB */
	.bt3ctr = { 0x26, 0x26, 0x26 },
	/* 0x09 = 16V */
	.vgh = { 0x09, 0x09, 0x09 },
	/* 0x36: HCK = 2 x Hsync, VGH = 2 x AVDD - AVEE */
	.bt4ctr = { 0x36, 0x36, 0x36 },
	/* 0x08 = -10V */
	.vgl = { 0x08, 0x08, 0x08 },
	/* 0x26: LCK = 2 x Hsync, VGL = AVDD + VCL - AVDD */
	.bt5ctr = { 0x26, 0x26, 0x26 },
	/* VGMP: 0x080 = 4.6V, VGSP = 0V */
	.vgp = { 0x00, 0x80, 0x00 },
	/* VGMP: 0x080 = 4.6V, VGSP = 0V */
	.vgn = { 0x00, 0x80, 0x00 },
	/* VCMOFFSEL = VCOM voltage offset mode, VCM = -1V */
	.vcmoff = { 0x00, 0x50 },
	.dopctr = { NT35510_DOPCTR_0_RAMKP | NT35510_DOPCTR_0_DSITE |
		NT35510_DOPCTR_0_DSIG | NT35510_DOPCTR_0_DSIM |
		NT35510_DOPCTR_0_EOTP | NT35510_DOPCTR_0_N565, 0 },
	.madctl = NT35510_ROTATE_180_SETTING,
	/* 0x03: SDT = 1.5 us */
	.sdhdtctr = 0x03,
	/* EQ control for gate signals, 0x00 = 0 us */
	.gseqctr = { 0x00, 0x00 },
	/* SDEQCTR: source driver EQ mode 2, 1 us rise time on each step */
	.sdeqctr = { 0x01, 0x02, 0x02, 0x02 },
	/* SDVPCTR: Normal operation off color during v porch */
	.sdvpctr = 0x01,
	/* T1: number of pixel clocks on one scanline: 0x184 = 389 clocks */
	.t1 = 0x0184,
	/* VBP: vertical back porch toward the panel */
	.vbp = 0x1C,
	/* VFP: vertical front porch toward the panel */
	.vfp = 0x1C,
	/* PSEL: divide pixel clock 23MHz with 1 (no clock downscaling) */
	.psel = 0,
	/* DPTMCTR12: 0x03: LVGL = VGLX, overlap mode, swap R->L O->E */
	.dpmctr12 = { 0x03, 0x00, 0x00, },
	/* write display brightness */
	.wrdisbv = 0x7f,
	/* write control display */
	.wrctrld = NT35510_WRCTRLD_BCTRL | NT35510_WRCTRLD_DD |
			NT35510_WRCTRLD_BL,
	/* write content adaptive brightness control */
	.wrcabc = NT35510_WRCABC_STILL_MODE,
	/* write CABC minimum brightness */
	.wrcabcmb = 0xff,
};

static const struct of_device_id nt35510_of_match[] = {
	{
		.compatible = "frida,frd400b25025",
		.data = &nt35510_frida_frd400b25025,
	},
	{
		.compatible = "hydis,hva40wv1",
		.data = &nt35510_hydis_hva40wv1,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, nt35510_of_match);

static struct mipi_dsi_driver nt35510_driver = {
	.probe = nt35510_probe,
	.remove = nt35510_remove,
	.driver = {
		.name = "panel-novatek-nt35510",
		.of_match_table = nt35510_of_match,
	},
};
module_mipi_dsi_driver(nt35510_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("NT35510-based panel driver");
MODULE_LICENSE("GPL v2");
