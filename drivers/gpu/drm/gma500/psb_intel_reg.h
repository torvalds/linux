/*
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __PSB_INTEL_REG_H__
#define __PSB_INTEL_REG_H__

/*
 * GPIO regs
 */
#define GPIOA			0x5010
#define GPIOB			0x5014
#define GPIOC			0x5018
#define GPIOD			0x501c
#define GPIOE			0x5020
#define GPIOF			0x5024
#define GPIOG			0x5028
#define GPIOH			0x502c
# define GPIO_CLOCK_DIR_MASK		(1 << 0)
# define GPIO_CLOCK_DIR_IN		(0 << 1)
# define GPIO_CLOCK_DIR_OUT		(1 << 1)
# define GPIO_CLOCK_VAL_MASK		(1 << 2)
# define GPIO_CLOCK_VAL_OUT		(1 << 3)
# define GPIO_CLOCK_VAL_IN		(1 << 4)
# define GPIO_CLOCK_PULLUP_DISABLE	(1 << 5)
# define GPIO_DATA_DIR_MASK		(1 << 8)
# define GPIO_DATA_DIR_IN		(0 << 9)
# define GPIO_DATA_DIR_OUT		(1 << 9)
# define GPIO_DATA_VAL_MASK		(1 << 10)
# define GPIO_DATA_VAL_OUT		(1 << 11)
# define GPIO_DATA_VAL_IN		(1 << 12)
# define GPIO_DATA_PULLUP_DISABLE	(1 << 13)

#define GMBUS0			0x5100 /* clock/port select */
#define   GMBUS_RATE_100KHZ	(0<<8)
#define   GMBUS_RATE_50KHZ	(1<<8)
#define   GMBUS_RATE_400KHZ	(2<<8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ	(3<<8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT	(1<<7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_PORT_DISABLED	0
#define   GMBUS_PORT_SSC	1
#define   GMBUS_PORT_VGADDC	2
#define   GMBUS_PORT_PANEL	3
#define   GMBUS_PORT_DPC	4 /* HDMIC */
#define   GMBUS_PORT_DPB	5 /* SDVO, HDMIB */
				  /* 6 reserved */
#define   GMBUS_PORT_DPD	7 /* HDMID */
#define   GMBUS_NUM_PORTS       8
#define GMBUS1			0x5104 /* command/status */
#define   GMBUS_SW_CLR_INT	(1<<31)
#define   GMBUS_SW_RDY		(1<<30)
#define   GMBUS_ENT		(1<<29) /* enable timeout */
#define   GMBUS_CYCLE_NONE	(0<<25)
#define   GMBUS_CYCLE_WAIT	(1<<25)
#define   GMBUS_CYCLE_INDEX	(2<<25)
#define   GMBUS_CYCLE_STOP	(4<<25)
#define   GMBUS_BYTE_COUNT_SHIFT 16
#define   GMBUS_SLAVE_INDEX_SHIFT 8
#define   GMBUS_SLAVE_ADDR_SHIFT 1
#define   GMBUS_SLAVE_READ	(1<<0)
#define   GMBUS_SLAVE_WRITE	(0<<0)
#define GMBUS2			0x5108 /* status */
#define   GMBUS_INUSE		(1<<15)
#define   GMBUS_HW_WAIT_PHASE	(1<<14)
#define   GMBUS_STALL_TIMEOUT	(1<<13)
#define   GMBUS_INT		(1<<12)
#define   GMBUS_HW_RDY		(1<<11)
#define   GMBUS_SATOER		(1<<10)
#define   GMBUS_ACTIVE		(1<<9)
#define GMBUS3			0x510c /* data buffer bytes 3-0 */
#define GMBUS4			0x5110 /* interrupt mask (Pineview+) */
#define   GMBUS_SLAVE_TIMEOUT_EN (1<<4)
#define   GMBUS_NAK_EN		(1<<3)
#define   GMBUS_IDLE_EN		(1<<2)
#define   GMBUS_HW_WAIT_EN	(1<<1)
#define   GMBUS_HW_RDY_EN	(1<<0)
#define GMBUS5			0x5120 /* byte index */
#define   GMBUS_2BYTE_INDEX_EN	(1<<31)

#define BLC_PWM_CTL		0x61254
#define BLC_PWM_CTL2		0x61250
#define  PWM_ENABLE		(1 << 31)
#define  PWM_LEGACY_MODE	(1 << 30)
#define  PWM_PIPE_B		(1 << 29)
#define BLC_PWM_CTL_C		0x62254
#define BLC_PWM_CTL2_C		0x62250
#define BACKLIGHT_MODULATION_FREQ_SHIFT		(17)
/*
 * This is the most significant 15 bits of the number of backlight cycles in a
 * complete cycle of the modulated backlight control.
 *
 * The actual value is this field multiplied by two.
 */
#define BACKLIGHT_MODULATION_FREQ_MASK	(0x7fff << 17)
#define BLM_LEGACY_MODE			(1 << 16)
/*
 * This is the number of cycles out of the backlight modulation cycle for which
 * the backlight is on.
 *
 * This field must be no greater than the number of cycles in the complete
 * backlight modulation cycle.
 */
#define BACKLIGHT_DUTY_CYCLE_SHIFT	(0)
#define BACKLIGHT_DUTY_CYCLE_MASK	(0xffff)

#define I915_GCFGC			0xf0
#define I915_LOW_FREQUENCY_ENABLE	(1 << 7)
#define I915_DISPLAY_CLOCK_190_200_MHZ	(0 << 4)
#define I915_DISPLAY_CLOCK_333_MHZ	(4 << 4)
#define I915_DISPLAY_CLOCK_MASK		(7 << 4)

#define I855_HPLLCC			0xc0
#define I855_CLOCK_CONTROL_MASK		(3 << 0)
#define I855_CLOCK_133_200		(0 << 0)
#define I855_CLOCK_100_200		(1 << 0)
#define I855_CLOCK_100_133		(2 << 0)
#define I855_CLOCK_166_250		(3 << 0)

/* I830 CRTC registers */
#define HTOTAL_A		0x60000
#define HBLANK_A		0x60004
#define HSYNC_A			0x60008
#define VTOTAL_A		0x6000c
#define VBLANK_A		0x60010
#define VSYNC_A			0x60014
#define PIPEASRC		0x6001c
#define BCLRPAT_A		0x60020
#define VSYNCSHIFT_A		0x60028

#define HTOTAL_B		0x61000
#define HBLANK_B		0x61004
#define HSYNC_B			0x61008
#define VTOTAL_B		0x6100c
#define VBLANK_B		0x61010
#define VSYNC_B			0x61014
#define PIPEBSRC		0x6101c
#define BCLRPAT_B		0x61020
#define VSYNCSHIFT_B		0x61028

#define HTOTAL_C		0x62000
#define HBLANK_C		0x62004
#define HSYNC_C			0x62008
#define VTOTAL_C		0x6200c
#define VBLANK_C		0x62010
#define VSYNC_C			0x62014
#define PIPECSRC		0x6201c
#define BCLRPAT_C		0x62020
#define VSYNCSHIFT_C		0x62028

#define PP_STATUS		0x61200
# define PP_ON				(1 << 31)
/*
 * Indicates that all dependencies of the panel are on:
 *
 * - PLL enabled
 * - pipe enabled
 * - LVDS/DVOB/DVOC on
 */
#define PP_READY			(1 << 30)
#define PP_SEQUENCE_NONE		(0 << 28)
#define PP_SEQUENCE_ON			(1 << 28)
#define PP_SEQUENCE_OFF			(2 << 28)
#define PP_SEQUENCE_MASK		0x30000000
#define PP_CONTROL		0x61204
#define POWER_TARGET_ON			(1 << 0)

#define LVDSPP_ON		0x61208
#define LVDSPP_OFF		0x6120c
#define PP_CYCLE		0x61210

#define PP_ON_DELAYS		0x61208		/* Cedartrail */
#define PP_OFF_DELAYS		0x6120c		/* Cedartrail */

#define PFIT_CONTROL		0x61230
#define PFIT_ENABLE			(1 << 31)
#define PFIT_PIPE_MASK			(3 << 29)
#define PFIT_PIPE_SHIFT			29
#define PFIT_SCALING_MODE_PILLARBOX	(1 << 27)
#define PFIT_SCALING_MODE_LETTERBOX	(3 << 26)
#define VERT_INTERP_DISABLE		(0 << 10)
#define VERT_INTERP_BILINEAR		(1 << 10)
#define VERT_INTERP_MASK		(3 << 10)
#define VERT_AUTO_SCALE			(1 << 9)
#define HORIZ_INTERP_DISABLE		(0 << 6)
#define HORIZ_INTERP_BILINEAR		(1 << 6)
#define HORIZ_INTERP_MASK		(3 << 6)
#define HORIZ_AUTO_SCALE		(1 << 5)
#define PANEL_8TO6_DITHER_ENABLE	(1 << 3)

#define PFIT_PGM_RATIOS		0x61234
#define PFIT_VERT_SCALE_MASK			0xfff00000
#define PFIT_HORIZ_SCALE_MASK			0x0000fff0

#define PFIT_AUTO_RATIOS	0x61238

#define DPLL_A			0x06014
#define DPLL_B			0x06018
#define DPLL_VCO_ENABLE			(1 << 31)
#define DPLL_DVO_HIGH_SPEED		(1 << 30)
#define DPLL_SYNCLOCK_ENABLE		(1 << 29)
#define DPLL_VGA_MODE_DIS		(1 << 28)
#define DPLLB_MODE_DAC_SERIAL		(1 << 26)	/* i915 */
#define DPLLB_MODE_LVDS			(2 << 26)	/* i915 */
#define DPLL_MODE_MASK			(3 << 26)
#define DPLL_DAC_SERIAL_P2_CLOCK_DIV_10	(0 << 24)	/* i915 */
#define DPLL_DAC_SERIAL_P2_CLOCK_DIV_5	(1 << 24)	/* i915 */
#define DPLLB_LVDS_P2_CLOCK_DIV_14	(0 << 24)	/* i915 */
#define DPLLB_LVDS_P2_CLOCK_DIV_7	(1 << 24)	/* i915 */
#define DPLL_P2_CLOCK_DIV_MASK		0x03000000	/* i915 */
#define DPLL_FPA0h1_P1_POST_DIV_MASK	0x00ff0000	/* i915 */
#define DPLL_LOCK			(1 << 15)	/* CDV */

/*
 *  The i830 generation, in DAC/serial mode, defines p1 as two plus this
 * bitfield, or just 2 if PLL_P1_DIVIDE_BY_TWO is set.
 */
# define DPLL_FPA01_P1_POST_DIV_MASK_I830	0x001f0000
/*
 * The i830 generation, in LVDS mode, defines P1 as the bit number set within
 * this field (only one bit may be set).
 */
#define DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS	0x003f0000
#define DPLL_FPA01_P1_POST_DIV_SHIFT	16
#define PLL_P2_DIVIDE_BY_4		(1 << 23)	/* i830, required
							 * in DVO non-gang */
# define PLL_P1_DIVIDE_BY_TWO		(1 << 21)	/* i830 */
#define PLL_REF_INPUT_DREFCLK		(0 << 13)
#define PLL_REF_INPUT_TVCLKINA		(1 << 13)	/* i830 */
#define PLL_REF_INPUT_TVCLKINBC		(2 << 13)	/* SDVO
								 * TVCLKIN */
#define PLLB_REF_INPUT_SPREADSPECTRUMIN	(3 << 13)
#define PLL_REF_INPUT_MASK		(3 << 13)
#define PLL_LOAD_PULSE_PHASE_SHIFT	9
/*
 * Parallel to Serial Load Pulse phase selection.
 * Selects the phase for the 10X DPLL clock for the PCIe
 * digital display port. The range is 4 to 13; 10 or more
 * is just a flip delay. The default is 6
 */
#define PLL_LOAD_PULSE_PHASE_MASK	(0xf << PLL_LOAD_PULSE_PHASE_SHIFT)
#define DISPLAY_RATE_SELECT_FPA1	(1 << 8)

/*
 * SDVO multiplier for 945G/GM. Not used on 965.
 *
 * DPLL_MD_UDI_MULTIPLIER_MASK
 */
#define SDVO_MULTIPLIER_MASK		0x000000ff
#define SDVO_MULTIPLIER_SHIFT_HIRES	4
#define SDVO_MULTIPLIER_SHIFT_VGA	0

/*
 * PLL_MD
 */
/* Pipe A SDVO/UDI clock multiplier/divider register for G965. */
#define DPLL_A_MD		0x0601c
/* Pipe B SDVO/UDI clock multiplier/divider register for G965. */
#define DPLL_B_MD		0x06020
/*
 * UDI pixel divider, controlling how many pixels are stuffed into a packet.
 *
 * Value is pixels minus 1.  Must be set to 1 pixel for SDVO.
 */
#define DPLL_MD_UDI_DIVIDER_MASK	0x3f000000
#define DPLL_MD_UDI_DIVIDER_SHIFT	24
/* UDI pixel divider for VGA, same as DPLL_MD_UDI_DIVIDER_MASK. */
#define DPLL_MD_VGA_UDI_DIVIDER_MASK	0x003f0000
#define DPLL_MD_VGA_UDI_DIVIDER_SHIFT	16
/*
 * SDVO/UDI pixel multiplier.
 *
 * SDVO requires that the bus clock rate be between 1 and 2 Ghz, and the bus
 * clock rate is 10 times the DPLL clock.  At low resolution/refresh rate
 * modes, the bus rate would be below the limits, so SDVO allows for stuffing
 * dummy bytes in the datastream at an increased clock rate, with both sides of
 * the link knowing how many bytes are fill.
 *
 * So, for a mode with a dotclock of 65Mhz, we would want to double the clock
 * rate to 130Mhz to get a bus rate of 1.30Ghz.  The DPLL clock rate would be
 * set to 130Mhz, and the SDVO multiplier set to 2x in this register and
 * through an SDVO command.
 *
 * This register field has values of multiplication factor minus 1, with
 * a maximum multiplier of 5 for SDVO.
 */
#define DPLL_MD_UDI_MULTIPLIER_MASK	0x00003f00
#define DPLL_MD_UDI_MULTIPLIER_SHIFT	8
/*
 * SDVO/UDI pixel multiplier for VGA, same as DPLL_MD_UDI_MULTIPLIER_MASK.
 * This best be set to the default value (3) or the CRT won't work. No,
 * I don't entirely understand what this does...
 */
#define DPLL_MD_VGA_UDI_MULTIPLIER_MASK	0x0000003f
#define DPLL_MD_VGA_UDI_MULTIPLIER_SHIFT 0

#define DPLL_TEST		0x606c
#define DPLLB_TEST_SDVO_DIV_1		(0 << 22)
#define DPLLB_TEST_SDVO_DIV_2		(1 << 22)
#define DPLLB_TEST_SDVO_DIV_4		(2 << 22)
#define DPLLB_TEST_SDVO_DIV_MASK	(3 << 22)
#define DPLLB_TEST_N_BYPASS		(1 << 19)
#define DPLLB_TEST_M_BYPASS		(1 << 18)
#define DPLLB_INPUT_BUFFER_ENABLE	(1 << 16)
#define DPLLA_TEST_N_BYPASS		(1 << 3)
#define DPLLA_TEST_M_BYPASS		(1 << 2)
#define DPLLA_INPUT_BUFFER_ENABLE	(1 << 0)

#define ADPA			0x61100
#define ADPA_DAC_ENABLE			(1 << 31)
#define ADPA_DAC_DISABLE		0
#define ADPA_PIPE_SELECT_MASK		(1 << 30)
#define ADPA_PIPE_A_SELECT		0
#define ADPA_PIPE_B_SELECT		(1 << 30)
#define ADPA_USE_VGA_HVPOLARITY		(1 << 15)
#define ADPA_SETS_HVPOLARITY		0
#define ADPA_VSYNC_CNTL_DISABLE		(1 << 11)
#define ADPA_VSYNC_CNTL_ENABLE		0
#define ADPA_HSYNC_CNTL_DISABLE		(1 << 10)
#define ADPA_HSYNC_CNTL_ENABLE		0
#define ADPA_VSYNC_ACTIVE_HIGH		(1 << 4)
#define ADPA_VSYNC_ACTIVE_LOW		0
#define ADPA_HSYNC_ACTIVE_HIGH		(1 << 3)
#define ADPA_HSYNC_ACTIVE_LOW		0

#define FPA0			0x06040
#define FPA1			0x06044
#define FPB0			0x06048
#define FPB1			0x0604c
#define FP_N_DIV_MASK			0x003f0000
#define FP_N_DIV_SHIFT			16
#define FP_M1_DIV_MASK			0x00003f00
#define FP_M1_DIV_SHIFT			8
#define FP_M2_DIV_MASK			0x0000003f
#define FP_M2_DIV_SHIFT			0

#define PORT_HOTPLUG_EN		0x61110
#define HDMIB_HOTPLUG_INT_EN		(1 << 29)
#define HDMIC_HOTPLUG_INT_EN		(1 << 28)
#define HDMID_HOTPLUG_INT_EN		(1 << 27)
#define SDVOB_HOTPLUG_INT_EN		(1 << 26)
#define SDVOC_HOTPLUG_INT_EN		(1 << 25)
#define TV_HOTPLUG_INT_EN		(1 << 18)
#define CRT_HOTPLUG_INT_EN		(1 << 9)
#define CRT_HOTPLUG_FORCE_DETECT	(1 << 3)
/* CDV.. */
#define CRT_HOTPLUG_ACTIVATION_PERIOD_64	(1 << 8)
#define CRT_HOTPLUG_DAC_ON_TIME_2M		(0 << 7)
#define CRT_HOTPLUG_DAC_ON_TIME_4M		(1 << 7)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_40		(0 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_50		(1 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_60		(2 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_70		(3 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_MASK	(3 << 5)
#define CRT_HOTPLUG_DETECT_DELAY_1G		(0 << 4)
#define CRT_HOTPLUG_DETECT_DELAY_2G		(1 << 4)
#define CRT_HOTPLUG_DETECT_VOLTAGE_325MV	(0 << 2)
#define CRT_HOTPLUG_DETECT_VOLTAGE_475MV	(1 << 2)
#define CRT_HOTPLUG_DETECT_MASK			0x000000F8

#define PORT_HOTPLUG_STAT	0x61114
#define CRT_HOTPLUG_INT_STATUS		(1 << 11)
#define TV_HOTPLUG_INT_STATUS		(1 << 10)
#define CRT_HOTPLUG_MONITOR_MASK	(3 << 8)
#define CRT_HOTPLUG_MONITOR_COLOR	(3 << 8)
#define CRT_HOTPLUG_MONITOR_MONO	(2 << 8)
#define CRT_HOTPLUG_MONITOR_NONE	(0 << 8)
#define SDVOC_HOTPLUG_INT_STATUS	(1 << 7)
#define SDVOB_HOTPLUG_INT_STATUS	(1 << 6)

#define SDVOB			0x61140
#define SDVOC			0x61160
#define SDVO_ENABLE			(1 << 31)
#define SDVO_PIPE_B_SELECT		(1 << 30)
#define SDVO_STALL_SELECT		(1 << 29)
#define SDVO_INTERRUPT_ENABLE		(1 << 26)
#define SDVO_COLOR_RANGE_16_235		(1 << 8)
#define SDVO_AUDIO_ENABLE		(1 << 6)

/**
 * 915G/GM SDVO pixel multiplier.
 *
 * Programmed value is multiplier - 1, up to 5x.
 *
 * DPLL_MD_UDI_MULTIPLIER_MASK
 */
#define SDVO_PORT_MULTIPLY_MASK		(7 << 23)
#define SDVO_PORT_MULTIPLY_SHIFT	23
#define SDVO_PHASE_SELECT_MASK		(15 << 19)
#define SDVO_PHASE_SELECT_DEFAULT	(6 << 19)
#define SDVO_CLOCK_OUTPUT_INVERT	(1 << 18)
#define SDVOC_GANG_MODE			(1 << 16)
#define SDVO_BORDER_ENABLE		(1 << 7)
#define SDVOB_PCIE_CONCURRENCY		(1 << 3)
#define SDVO_DETECTED			(1 << 2)
/* Bits to be preserved when writing */
#define SDVOB_PRESERVE_MASK		((1 << 17) | (1 << 16) | (1 << 14))
#define SDVOC_PRESERVE_MASK		(1 << 17)

/*
 * This register controls the LVDS output enable, pipe selection, and data
 * format selection.
 *
 * All of the clock/data pairs are force powered down by power sequencing.
 */
#define LVDS			0x61180
/*
 * Enables the LVDS port.  This bit must be set before DPLLs are enabled, as
 * the DPLL semantics change when the LVDS is assigned to that pipe.
 */
#define LVDS_PORT_EN			(1 << 31)
/* Selects pipe B for LVDS data.  Must be set on pre-965. */
#define LVDS_PIPEB_SELECT		(1 << 30)

/* Turns on border drawing to allow centered display. */
#define LVDS_BORDER_EN			(1 << 15)

/*
 * Enables the A0-A2 data pairs and CLKA, containing 18 bits of color data per
 * pixel.
 */
#define LVDS_A0A2_CLKA_POWER_MASK	(3 << 8)
#define LVDS_A0A2_CLKA_POWER_DOWN	(0 << 8)
#define LVDS_A0A2_CLKA_POWER_UP		(3 << 8)
/*
 * Controls the A3 data pair, which contains the additional LSBs for 24 bit
 * mode.  Only enabled if LVDS_A0A2_CLKA_POWER_UP also indicates it should be
 * on.
 */
#define LVDS_A3_POWER_MASK		(3 << 6)
#define LVDS_A3_POWER_DOWN		(0 << 6)
#define LVDS_A3_POWER_UP		(3 << 6)
/*
 * Controls the CLKB pair.  This should only be set when LVDS_B0B3_POWER_UP
 * is set.
 */
#define LVDS_CLKB_POWER_MASK		(3 << 4)
#define LVDS_CLKB_POWER_DOWN		(0 << 4)
#define LVDS_CLKB_POWER_UP		(3 << 4)
/*
 * Controls the B0-B3 data pairs.  This must be set to match the DPLL p2
 * setting for whether we are in dual-channel mode.  The B3 pair will
 * additionally only be powered up when LVDS_A3_POWER_UP is set.
 */
#define LVDS_B0B3_POWER_MASK		(3 << 2)
#define LVDS_B0B3_POWER_DOWN		(0 << 2)
#define LVDS_B0B3_POWER_UP		(3 << 2)

#define PIPEACONF		0x70008
#define PIPEACONF_ENABLE		(1 << 31)
#define PIPEACONF_DISABLE		0
#define PIPEACONF_DOUBLE_WIDE		(1 << 30)
#define PIPECONF_ACTIVE			(1 << 30)
#define I965_PIPECONF_ACTIVE		(1 << 30)
#define PIPECONF_DSIPLL_LOCK		(1 << 29)
#define PIPEACONF_SINGLE_WIDE		0
#define PIPEACONF_PIPE_UNLOCKED		0
#define PIPEACONF_DSR			(1 << 26)
#define PIPEACONF_PIPE_LOCKED		(1 << 25)
#define PIPEACONF_PALETTE		0
#define PIPECONF_FORCE_BORDER		(1 << 25)
#define PIPEACONF_GAMMA			(1 << 24)
#define PIPECONF_PROGRESSIVE		(0 << 21)
#define PIPECONF_INTERLACE_W_FIELD_INDICATION	(6 << 21)
#define PIPECONF_INTERLACE_FIELD_0_ONLY		(7 << 21)
#define PIPECONF_PLANE_OFF		(1 << 19)
#define PIPECONF_CURSOR_OFF		(1 << 18)

#define PIPEBCONF		0x71008
#define PIPEBCONF_ENABLE		(1 << 31)
#define PIPEBCONF_DISABLE		0
#define PIPEBCONF_DOUBLE_WIDE		(1 << 30)
#define PIPEBCONF_DISABLE		0
#define PIPEBCONF_GAMMA			(1 << 24)
#define PIPEBCONF_PALETTE		0

#define PIPECCONF		0x72008

#define PIPEBGCMAXRED		0x71010
#define PIPEBGCMAXGREEN		0x71014
#define PIPEBGCMAXBLUE		0x71018

#define PIPEASTAT		0x70024
#define PIPEBSTAT		0x71024
#define PIPECSTAT		0x72024
#define PIPE_VBLANK_INTERRUPT_STATUS		(1UL << 1)
#define PIPE_START_VBLANK_INTERRUPT_STATUS	(1UL << 2)
#define PIPE_VBLANK_CLEAR			(1 << 1)
#define PIPE_VBLANK_STATUS			(1 << 1)
#define PIPE_TE_STATUS				(1UL << 6)
#define PIPE_DPST_EVENT_STATUS			(1UL << 7)
#define PIPE_VSYNC_CLEAR			(1UL << 9)
#define PIPE_VSYNC_STATUS			(1UL << 9)
#define PIPE_HDMI_AUDIO_UNDERRUN_STATUS		(1UL << 10)
#define PIPE_HDMI_AUDIO_BUFFER_DONE_STATUS	(1UL << 11)
#define PIPE_VBLANK_INTERRUPT_ENABLE		(1UL << 17)
#define PIPE_START_VBLANK_INTERRUPT_ENABLE	(1UL << 18)
#define PIPE_TE_ENABLE				(1UL << 22)
#define PIPE_LEGACY_BLC_EVENT_ENABLE		(1UL << 22)
#define PIPE_DPST_EVENT_ENABLE			(1UL << 23)
#define PIPE_VSYNC_ENABL			(1UL << 25)
#define PIPE_HDMI_AUDIO_UNDERRUN		(1UL << 26)
#define PIPE_HDMI_AUDIO_BUFFER_DONE		(1UL << 27)
#define PIPE_FIFO_UNDERRUN			(1UL << 31)
#define PIPE_HDMI_AUDIO_INT_MASK		(PIPE_HDMI_AUDIO_UNDERRUN | \
						PIPE_HDMI_AUDIO_BUFFER_DONE)
#define PIPE_EVENT_MASK ((1 << 29)|(1 << 28)|(1 << 27)|(1 << 26)|(1 << 24)|(1 << 23)|(1 << 22)|(1 << 21)|(1 << 20)|(1 << 16))
#define PIPE_VBLANK_MASK ((1 << 25)|(1 << 24)|(1 << 18)|(1 << 17))
#define HISTOGRAM_INT_CONTROL		0x61268
#define HISTOGRAM_BIN_DATA		0X61264
#define HISTOGRAM_LOGIC_CONTROL		0x61260
#define PWM_CONTROL_LOGIC		0x61250
#define PIPE_HOTPLUG_INTERRUPT_STATUS		(1UL << 10)
#define HISTOGRAM_INTERRUPT_ENABLE		(1UL << 31)
#define HISTOGRAM_LOGIC_ENABLE			(1UL << 31)
#define PWM_LOGIC_ENABLE			(1UL << 31)
#define PWM_PHASEIN_ENABLE			(1UL << 25)
#define PWM_PHASEIN_INT_ENABLE			(1UL << 24)
#define PWM_PHASEIN_VB_COUNT			0x00001f00
#define PWM_PHASEIN_INC				0x0000001f
#define HISTOGRAM_INT_CTRL_CLEAR		(1UL << 30)
#define DPST_YUV_LUMA_MODE			0

struct dpst_ie_histogram_control {
	union {
		uint32_t data;
		struct {
			uint32_t bin_reg_index:7;
			uint32_t reserved:4;
			uint32_t bin_reg_func_select:1;
			uint32_t sync_to_phase_in:1;
			uint32_t alt_enhancement_mode:2;
			uint32_t reserved1:1;
			uint32_t sync_to_phase_in_count:8;
			uint32_t histogram_mode_select:1;
			uint32_t reserved2:4;
			uint32_t ie_pipe_assignment:1;
			uint32_t ie_mode_table_enabled:1;
			uint32_t ie_histogram_enable:1;
		};
	};
};

struct dpst_guardband {
	union {
		uint32_t data;
		struct {
			uint32_t guardband:22;
			uint32_t guardband_interrupt_delay:8;
			uint32_t interrupt_status:1;
			uint32_t interrupt_enable:1;
		};
	};
};

#define PIPEAFRAMEHIGH		0x70040
#define PIPEAFRAMEPIXEL		0x70044
#define PIPEBFRAMEHIGH		0x71040
#define PIPEBFRAMEPIXEL		0x71044
#define PIPECFRAMEHIGH		0x72040
#define PIPECFRAMEPIXEL		0x72044
#define PIPE_FRAME_HIGH_MASK	0x0000ffff
#define PIPE_FRAME_HIGH_SHIFT	0
#define PIPE_FRAME_LOW_MASK	0xff000000
#define PIPE_FRAME_LOW_SHIFT	24
#define PIPE_PIXEL_MASK		0x00ffffff
#define PIPE_PIXEL_SHIFT	0

#define FW_BLC_SELF		0x20e0 
#define FW_BLC_SELF_EN          (1<<15)

#define DSPARB			0x70030
#define DSPFW1			0x70034
#define DSP_FIFO_SR_WM_MASK		0xFF800000
#define DSP_FIFO_SR_WM_SHIFT		23
#define CURSOR_B_FIFO_WM_MASK		0x003F0000
#define CURSOR_B_FIFO_WM_SHIFT		16
#define DSPFW2			0x70038
#define CURSOR_A_FIFO_WM_MASK		0x3F00
#define CURSOR_A_FIFO_WM_SHIFT		8
#define DSP_PLANE_C_FIFO_WM_MASK	0x7F
#define DSP_PLANE_C_FIFO_WM_SHIFT	0
#define DSPFW3			0x7003c
#define DSPFW4			0x70050
#define DSPFW5			0x70054
#define DSP_PLANE_B_FIFO_WM1_SHIFT	24
#define DSP_PLANE_A_FIFO_WM1_SHIFT	16
#define CURSOR_B_FIFO_WM1_SHIFT		8
#define CURSOR_FIFO_SR_WM1_SHIFT	0
#define DSPFW6			0x70058
#define DSPCHICKENBIT		0x70400
#define DSPACNTR		0x70180
#define DSPBCNTR		0x71180
#define DSPCCNTR		0x72180
#define DISPLAY_PLANE_ENABLE			(1 << 31)
#define DISPLAY_PLANE_DISABLE			0
#define DISPPLANE_GAMMA_ENABLE			(1 << 30)
#define DISPPLANE_GAMMA_DISABLE			0
#define DISPPLANE_PIXFORMAT_MASK		(0xf << 26)
#define DISPPLANE_8BPP				(0x2 << 26)
#define DISPPLANE_15_16BPP			(0x4 << 26)
#define DISPPLANE_16BPP				(0x5 << 26)
#define DISPPLANE_32BPP_NO_ALPHA		(0x6 << 26)
#define DISPPLANE_32BPP				(0x7 << 26)
#define DISPPLANE_STEREO_ENABLE			(1 << 25)
#define DISPPLANE_STEREO_DISABLE		0
#define DISPPLANE_SEL_PIPE_MASK			(1 << 24)
#define DISPPLANE_SEL_PIPE_POS			24
#define DISPPLANE_SEL_PIPE_A			0
#define DISPPLANE_SEL_PIPE_B			(1 << 24)
#define DISPPLANE_SRC_KEY_ENABLE		(1 << 22)
#define DISPPLANE_SRC_KEY_DISABLE		0
#define DISPPLANE_LINE_DOUBLE			(1 << 20)
#define DISPPLANE_NO_LINE_DOUBLE		0
#define DISPPLANE_STEREO_POLARITY_FIRST		0
#define DISPPLANE_STEREO_POLARITY_SECOND	(1 << 18)
/* plane B only */
#define DISPPLANE_ALPHA_TRANS_ENABLE		(1 << 15)
#define DISPPLANE_ALPHA_TRANS_DISABLE		0
#define DISPPLANE_SPRITE_ABOVE_DISPLAYA		0
#define DISPPLANE_SPRITE_ABOVE_OVERLAY		(1)
#define DISPPLANE_BOTTOM			(4)

#define DSPABASE		0x70184
#define DSPALINOFF		0x70184
#define DSPASTRIDE		0x70188

#define DSPBBASE		0x71184
#define DSPBLINOFF		0X71184
#define DSPBADDR		DSPBBASE
#define DSPBSTRIDE		0x71188

#define DSPCBASE		0x72184
#define DSPCLINOFF		0x72184
#define DSPCSTRIDE		0x72188

#define DSPAKEYVAL		0x70194
#define DSPAKEYMASK		0x70198

#define DSPAPOS			0x7018C	/* reserved */
#define DSPASIZE		0x70190
#define DSPBPOS			0x7118C
#define DSPBSIZE		0x71190
#define DSPCPOS			0x7218C
#define DSPCSIZE		0x72190

#define DSPASURF		0x7019C
#define DSPATILEOFF		0x701A4

#define DSPBSURF		0x7119C
#define DSPBTILEOFF		0x711A4

#define DSPCSURF		0x7219C
#define DSPCTILEOFF		0x721A4
#define DSPCKEYMAXVAL		0x721A0
#define DSPCKEYMINVAL		0x72194
#define DSPCKEYMSK		0x72198

#define VGACNTRL		0x71400
#define VGA_DISP_DISABLE		(1 << 31)
#define VGA_2X_MODE			(1 << 30)
#define VGA_PIPE_B_SELECT		(1 << 29)

/*
 * Overlay registers
 */
#define OV_C_OFFSET		0x08000
#define OV_OVADD		0x30000
#define OV_DOVASTA		0x30008
# define OV_PIPE_SELECT			((1 << 6)|(1 << 7))
# define OV_PIPE_SELECT_POS		6
# define OV_PIPE_A			0
# define OV_PIPE_C			1
#define OV_OGAMC5		0x30010
#define OV_OGAMC4		0x30014
#define OV_OGAMC3		0x30018
#define OV_OGAMC2		0x3001C
#define OV_OGAMC1		0x30020
#define OV_OGAMC0		0x30024
#define OVC_OVADD		0x38000
#define OVC_DOVCSTA		0x38008
#define OVC_OGAMC5		0x38010
#define OVC_OGAMC4		0x38014
#define OVC_OGAMC3		0x38018
#define OVC_OGAMC2		0x3801C
#define OVC_OGAMC1		0x38020
#define OVC_OGAMC0		0x38024

/*
 * Some BIOS scratch area registers.  The 845 (and 830?) store the amount
 * of video memory available to the BIOS in SWF1.
 */
#define SWF0			0x71410
#define SWF1			0x71414
#define SWF2			0x71418
#define SWF3			0x7141c
#define SWF4			0x71420
#define SWF5			0x71424
#define SWF6			0x71428

/*
 * 855 scratch registers.
 */
#define SWF00			0x70410
#define SWF01			0x70414
#define SWF02			0x70418
#define SWF03			0x7041c
#define SWF04			0x70420
#define SWF05			0x70424
#define SWF06			0x70428

#define SWF10			SWF0
#define SWF11			SWF1
#define SWF12			SWF2
#define SWF13			SWF3
#define SWF14			SWF4
#define SWF15			SWF5
#define SWF16			SWF6

#define SWF30			0x72414
#define SWF31			0x72418
#define SWF32			0x7241c


/*
 * Palette registers
 */
#define PALETTE_A		0x0a000
#define PALETTE_B		0x0a800
#define PALETTE_C		0x0ac00

/* Cursor A & B regs */
#define CURACNTR		0x70080
#define CURSOR_MODE_DISABLE		0x00
#define CURSOR_MODE_64_32B_AX		0x07
#define CURSOR_MODE_64_ARGB_AX		((1 << 5) | CURSOR_MODE_64_32B_AX)
#define MCURSOR_GAMMA_ENABLE		(1 << 26)
#define CURABASE		0x70084
#define CURAPOS			0x70088
#define CURSOR_POS_MASK			0x007FF
#define CURSOR_POS_SIGN			0x8000
#define CURSOR_X_SHIFT			0
#define CURSOR_Y_SHIFT			16
#define CURBCNTR		0x700c0
#define CURBBASE		0x700c4
#define CURBPOS			0x700c8
#define CURCCNTR		0x700e0
#define CURCBASE		0x700e4
#define CURCPOS			0x700e8

/*
 * Interrupt Registers
 */
#define IER			0x020a0
#define IIR			0x020a4
#define IMR			0x020a8
#define ISR			0x020ac

/*
 * MOORESTOWN delta registers
 */
#define MRST_DPLL_A		0x0f014
#define MDFLD_DPLL_B		0x0f018
#define MDFLD_INPUT_REF_SEL		(1 << 14)
#define MDFLD_VCO_SEL			(1 << 16)
#define DPLLA_MODE_LVDS			(2 << 26)	/* mrst */
#define MDFLD_PLL_LATCHEN		(1 << 28)
#define MDFLD_PWR_GATE_EN		(1 << 30)
#define MDFLD_P1_MASK			(0x1FF << 17)
#define MRST_FPA0		0x0f040
#define MRST_FPA1		0x0f044
#define MDFLD_DPLL_DIV0		0x0f048
#define MDFLD_DPLL_DIV1		0x0f04c
#define MRST_PERF_MODE		0x020f4

/*
 * MEDFIELD HDMI registers
 */
#define HDMIPHYMISCCTL		0x61134
#define HDMI_PHY_POWER_DOWN		0x7f
#define HDMIB_CONTROL		0x61140
#define HDMIB_PORT_EN			(1 << 31)
#define HDMIB_PIPE_B_SELECT		(1 << 30)
#define HDMIB_NULL_PACKET		(1 << 9)
#define HDMIB_HDCP_PORT			(1 << 5)

/* #define LVDS			0x61180 */
#define MRST_PANEL_8TO6_DITHER_ENABLE	(1 << 25)
#define MRST_PANEL_24_DOT_1_FORMAT	(1 << 24)
#define LVDS_A3_POWER_UP_0_OUTPUT	(1 << 6)

#define MIPI			0x61190
#define MIPI_C			0x62190
#define MIPI_PORT_EN			(1 << 31)
/* Turns on border drawing to allow centered display. */
#define SEL_FLOPPED_HSTX		(1 << 23)
#define PASS_FROM_SPHY_TO_AFE		(1 << 16)
#define MIPI_BORDER_EN			(1 << 15)
#define MIPIA_3LANE_MIPIC_1LANE		0x1
#define MIPIA_2LANE_MIPIC_2LANE		0x2
#define TE_TRIGGER_DSI_PROTOCOL		(1 << 2)
#define TE_TRIGGER_GPIO_PIN		(1 << 3)
#define MIPI_TE_COUNT		0x61194

/* #define PP_CONTROL	0x61204 */
#define POWER_DOWN_ON_RESET		(1 << 1)

/* #define PFIT_CONTROL	0x61230 */
#define PFIT_PIPE_SELECT		(3 << 29)
#define PFIT_PIPE_SELECT_SHIFT		(29)

/* #define BLC_PWM_CTL		0x61254 */
#define MRST_BACKLIGHT_MODULATION_FREQ_SHIFT	(16)
#define MRST_BACKLIGHT_MODULATION_FREQ_MASK	(0xffff << 16)

/* #define PIPEACONF 0x70008 */
#define PIPEACONF_PIPE_STATE		(1 << 30)
/* #define DSPACNTR		0x70180 */

#define MRST_DSPABASE		0x7019c
#define MRST_DSPBBASE		0x7119c
#define MDFLD_DSPCBASE		0x7219c

/*
 * Moorestown registers.
 */

/*
 *	MIPI IP registers
 */
#define MIPIC_REG_OFFSET		0x800

#define DEVICE_READY_REG		0xb000
#define LP_OUTPUT_HOLD				(1 << 16)
#define EXIT_ULPS_DEV_READY			0x3
#define LP_OUTPUT_HOLD_RELEASE			0x810000
# define ENTERING_ULPS				(2 << 1)
# define EXITING_ULPS				(1 << 1)
# define ULPS_MASK				(3 << 1)
# define BUS_POSSESSION				(1 << 3)
#define INTR_STAT_REG			0xb004
#define RX_SOT_ERROR				(1 << 0)
#define RX_SOT_SYNC_ERROR			(1 << 1)
#define RX_ESCAPE_MODE_ENTRY_ERROR		(1 << 3)
#define RX_LP_TX_SYNC_ERROR			(1 << 4)
#define RX_HS_RECEIVE_TIMEOUT_ERROR		(1 << 5)
#define RX_FALSE_CONTROL_ERROR			(1 << 6)
#define RX_ECC_SINGLE_BIT_ERROR			(1 << 7)
#define RX_ECC_MULTI_BIT_ERROR			(1 << 8)
#define RX_CHECKSUM_ERROR			(1 << 9)
#define RX_DSI_DATA_TYPE_NOT_RECOGNIZED		(1 << 10)
#define RX_DSI_VC_ID_INVALID			(1 << 11)
#define TX_FALSE_CONTROL_ERROR			(1 << 12)
#define TX_ECC_SINGLE_BIT_ERROR			(1 << 13)
#define TX_ECC_MULTI_BIT_ERROR			(1 << 14)
#define TX_CHECKSUM_ERROR			(1 << 15)
#define TX_DSI_DATA_TYPE_NOT_RECOGNIZED		(1 << 16)
#define TX_DSI_VC_ID_INVALID			(1 << 17)
#define HIGH_CONTENTION				(1 << 18)
#define LOW_CONTENTION				(1 << 19)
#define DPI_FIFO_UNDER_RUN			(1 << 20)
#define HS_TX_TIMEOUT				(1 << 21)
#define LP_RX_TIMEOUT				(1 << 22)
#define TURN_AROUND_ACK_TIMEOUT			(1 << 23)
#define ACK_WITH_NO_ERROR			(1 << 24)
#define HS_GENERIC_WR_FIFO_FULL			(1 << 27)
#define LP_GENERIC_WR_FIFO_FULL			(1 << 28)
#define SPL_PKT_SENT				(1 << 30)
#define INTR_EN_REG			0xb008
#define DSI_FUNC_PRG_REG		0xb00c
#define DPI_CHANNEL_NUMBER_POS			0x03
#define DBI_CHANNEL_NUMBER_POS			0x05
#define FMT_DPI_POS				0x07
#define FMT_DBI_POS				0x0A
#define DBI_DATA_WIDTH_POS			0x0D

/* DPI PIXEL FORMATS */
#define RGB_565_FMT				0x01	/* RGB 565 FORMAT */
#define RGB_666_FMT				0x02	/* RGB 666 FORMAT */
#define LRGB_666_FMT				0x03	/* RGB LOOSELY PACKED
							 * 666 FORMAT
							 */
#define RGB_888_FMT				0x04	/* RGB 888 FORMAT */
#define VIRTUAL_CHANNEL_NUMBER_0		0x00	/* Virtual channel 0 */
#define VIRTUAL_CHANNEL_NUMBER_1		0x01	/* Virtual channel 1 */
#define VIRTUAL_CHANNEL_NUMBER_2		0x02	/* Virtual channel 2 */
#define VIRTUAL_CHANNEL_NUMBER_3		0x03	/* Virtual channel 3 */

#define DBI_NOT_SUPPORTED			0x00	/* command mode
							 * is not supported
							 */
#define DBI_DATA_WIDTH_16BIT			0x01	/* 16 bit data */
#define DBI_DATA_WIDTH_9BIT			0x02	/* 9 bit data */
#define DBI_DATA_WIDTH_8BIT			0x03	/* 8 bit data */
#define DBI_DATA_WIDTH_OPT1			0x04	/* option 1 */
#define DBI_DATA_WIDTH_OPT2			0x05	/* option 2 */

#define HS_TX_TIMEOUT_REG		0xb010
#define LP_RX_TIMEOUT_REG		0xb014
#define TURN_AROUND_TIMEOUT_REG		0xb018
#define DEVICE_RESET_REG		0xb01C
#define DPI_RESOLUTION_REG		0xb020
#define RES_V_POS				0x10
#define DBI_RESOLUTION_REG		0xb024 /* Reserved for MDFLD */
#define HORIZ_SYNC_PAD_COUNT_REG	0xb028
#define HORIZ_BACK_PORCH_COUNT_REG	0xb02C
#define HORIZ_FRONT_PORCH_COUNT_REG	0xb030
#define HORIZ_ACTIVE_AREA_COUNT_REG	0xb034
#define VERT_SYNC_PAD_COUNT_REG		0xb038
#define VERT_BACK_PORCH_COUNT_REG	0xb03c
#define VERT_FRONT_PORCH_COUNT_REG	0xb040
#define HIGH_LOW_SWITCH_COUNT_REG	0xb044
#define DPI_CONTROL_REG			0xb048
#define DPI_SHUT_DOWN				(1 << 0)
#define DPI_TURN_ON				(1 << 1)
#define DPI_COLOR_MODE_ON			(1 << 2)
#define DPI_COLOR_MODE_OFF			(1 << 3)
#define DPI_BACK_LIGHT_ON			(1 << 4)
#define DPI_BACK_LIGHT_OFF			(1 << 5)
#define DPI_LP					(1 << 6)
#define DPI_DATA_REG			0xb04c
#define DPI_BACK_LIGHT_ON_DATA			0x07
#define DPI_BACK_LIGHT_OFF_DATA			0x17
#define INIT_COUNT_REG			0xb050
#define MAX_RET_PAK_REG			0xb054
#define VIDEO_FMT_REG			0xb058
#define COMPLETE_LAST_PCKT			(1 << 2)
#define EOT_DISABLE_REG			0xb05c
#define ENABLE_CLOCK_STOPPING			(1 << 1)
#define LP_BYTECLK_REG			0xb060
#define LP_GEN_DATA_REG			0xb064
#define HS_GEN_DATA_REG			0xb068
#define LP_GEN_CTRL_REG			0xb06C
#define HS_GEN_CTRL_REG			0xb070
#define DCS_CHANNEL_NUMBER_POS		0x6
#define MCS_COMMANDS_POS		0x8
#define WORD_COUNTS_POS			0x8
#define MCS_PARAMETER_POS			0x10
#define GEN_FIFO_STAT_REG		0xb074
#define HS_DATA_FIFO_FULL			(1 << 0)
#define HS_DATA_FIFO_HALF_EMPTY			(1 << 1)
#define HS_DATA_FIFO_EMPTY			(1 << 2)
#define LP_DATA_FIFO_FULL			(1 << 8)
#define LP_DATA_FIFO_HALF_EMPTY			(1 << 9)
#define LP_DATA_FIFO_EMPTY			(1 << 10)
#define HS_CTRL_FIFO_FULL			(1 << 16)
#define HS_CTRL_FIFO_HALF_EMPTY			(1 << 17)
#define HS_CTRL_FIFO_EMPTY			(1 << 18)
#define LP_CTRL_FIFO_FULL			(1 << 24)
#define LP_CTRL_FIFO_HALF_EMPTY			(1 << 25)
#define LP_CTRL_FIFO_EMPTY			(1 << 26)
#define DBI_FIFO_EMPTY				(1 << 27)
#define DPI_FIFO_EMPTY				(1 << 28)
#define HS_LS_DBI_ENABLE_REG		0xb078
#define TXCLKESC_REG			0xb07c
#define DPHY_PARAM_REG			0xb080
#define DBI_BW_CTRL_REG			0xb084
#define CLK_LANE_SWT_REG		0xb088

/*
 * MIPI Adapter registers
 */
#define MIPI_CONTROL_REG		0xb104
#define MIPI_2X_CLOCK_BITS			((1 << 0) | (1 << 1))
#define MIPI_DATA_ADDRESS_REG		0xb108
#define MIPI_DATA_LENGTH_REG		0xb10C
#define MIPI_COMMAND_ADDRESS_REG	0xb110
#define MIPI_COMMAND_LENGTH_REG		0xb114
#define MIPI_READ_DATA_RETURN_REG0	0xb118
#define MIPI_READ_DATA_RETURN_REG1	0xb11C
#define MIPI_READ_DATA_RETURN_REG2	0xb120
#define MIPI_READ_DATA_RETURN_REG3	0xb124
#define MIPI_READ_DATA_RETURN_REG4	0xb128
#define MIPI_READ_DATA_RETURN_REG5	0xb12C
#define MIPI_READ_DATA_RETURN_REG6	0xb130
#define MIPI_READ_DATA_RETURN_REG7	0xb134
#define MIPI_READ_DATA_VALID_REG	0xb138

/* DBI COMMANDS */
#define soft_reset			0x01
/*
 *	The display module performs a software reset.
 *	Registers are written with their SW Reset default values.
 */
#define get_power_mode			0x0a
/*
 *	The display module returns the current power mode
 */
#define get_address_mode		0x0b
/*
 *	The display module returns the current status.
 */
#define get_pixel_format		0x0c
/*
 *	This command gets the pixel format for the RGB image data
 *	used by the interface.
 */
#define get_display_mode		0x0d
/*
 *	The display module returns the Display Image Mode status.
 */
#define get_signal_mode			0x0e
/*
 *	The display module returns the Display Signal Mode.
 */
#define get_diagnostic_result		0x0f
/*
 *	The display module returns the self-diagnostic results following
 *	a Sleep Out command.
 */
#define enter_sleep_mode		0x10
/*
 *	This command causes the display module to enter the Sleep mode.
 *	In this mode, all unnecessary blocks inside the display module are
 *	disabled except interface communication. This is the lowest power
 *	mode the display module supports.
 */
#define exit_sleep_mode			0x11
/*
 *	This command causes the display module to exit Sleep mode.
 *	All blocks inside the display module are enabled.
 */
#define enter_partial_mode		0x12
/*
 *	This command causes the display module to enter the Partial Display
 *	Mode. The Partial Display Mode window is described by the
 *	set_partial_area command.
 */
#define enter_normal_mode		0x13
/*
 *	This command causes the display module to enter the Normal mode.
 *	Normal Mode is defined as Partial Display mode and Scroll mode are off
 */
#define exit_invert_mode		0x20
/*
 *	This command causes the display module to stop inverting the image
 *	data on the display device. The frame memory contents remain unchanged.
 *	No status bits are changed.
 */
#define enter_invert_mode		0x21
/*
 *	This command causes the display module to invert the image data only on
 *	the display device. The frame memory contents remain unchanged.
 *	No status bits are changed.
 */
#define set_gamma_curve			0x26
/*
 *	This command selects the desired gamma curve for the display device.
 *	Four fixed gamma curves are defined in section DCS spec.
 */
#define set_display_off			0x28
/* ************************************************************************* *\
This command causes the display module to stop displaying the image data
on the display device. The frame memory contents remain unchanged.
No status bits are changed.
\* ************************************************************************* */
#define set_display_on			0x29
/* ************************************************************************* *\
This command causes the display module to start displaying the image data
on the display device. The frame memory contents remain unchanged.
No status bits are changed.
\* ************************************************************************* */
#define set_column_address		0x2a
/*
 *	This command defines the column extent of the frame memory accessed by
 *	the hostprocessor with the read_memory_continue and
 *	write_memory_continue commands.
 *	No status bits are changed.
 */
#define set_page_addr			0x2b
/*
 *	This command defines the page extent of the frame memory accessed by
 *	the host processor with the write_memory_continue and
 *	read_memory_continue command.
 *	No status bits are changed.
 */
#define write_mem_start			0x2c
/*
 *	This command transfers image data from the host processor to the
 *	display modules frame memory starting at the pixel location specified
 *	by preceding set_column_address and set_page_address commands.
 */
#define set_partial_area		0x30
/*
 *	This command defines the Partial Display mode s display area.
 *	There are two parameters associated with this command, the first
 *	defines the Start Row (SR) and the second the End Row (ER). SR and ER
 *	refer to the Frame Memory Line Pointer.
 */
#define set_scroll_area			0x33
/*
 *	This command defines the display modules Vertical Scrolling Area.
 */
#define set_tear_off			0x34
/*
 *	This command turns off the display modules Tearing Effect output
 *	signal on the TE signal line.
 */
#define set_tear_on			0x35
/*
 *	This command turns on the display modules Tearing Effect output signal
 *	on the TE signal line.
 */
#define set_address_mode		0x36
/*
 *	This command sets the data order for transfers from the host processor
 *	to display modules frame memory,bits B[7:5] and B3, and from the
 *	display modules frame memory to the display device, bits B[2:0] and B4.
 */
#define set_scroll_start		0x37
/*
 *	This command sets the start of the vertical scrolling area in the frame
 *	memory. The vertical scrolling area is fully defined when this command
 *	is used with the set_scroll_area command The set_scroll_start command
 *	has one parameter, the Vertical Scroll Pointer. The VSP defines the
 *	line in the frame memory that is written to the display device as the
 *	first line of the vertical scroll area.
 */
#define exit_idle_mode			0x38
/*
 *	This command causes the display module to exit Idle mode.
 */
#define enter_idle_mode			0x39
/*
 *	This command causes the display module to enter Idle Mode.
 *	In Idle Mode, color expression is reduced. Colors are shown on the
 *	display device using the MSB of each of the R, G and B color
 *	components in the frame memory
 */
#define set_pixel_format		0x3a
/*
 *	This command sets the pixel format for the RGB image data used by the
 *	interface.
 *	Bits D[6:4]  DPI Pixel Format Definition
 *	Bits D[2:0]  DBI Pixel Format Definition
 *	Bits D7 and D3 are not used.
 */
#define DCS_PIXEL_FORMAT_3bpp		0x1
#define DCS_PIXEL_FORMAT_8bpp		0x2
#define DCS_PIXEL_FORMAT_12bpp		0x3
#define DCS_PIXEL_FORMAT_16bpp		0x5
#define DCS_PIXEL_FORMAT_18bpp		0x6
#define DCS_PIXEL_FORMAT_24bpp		0x7

#define write_mem_cont			0x3c

/*
 *	This command transfers image data from the host processor to the
 *	display module's frame memory continuing from the pixel location
 *	following the previous write_memory_continue or write_memory_start
 *	command.
 */
#define set_tear_scanline		0x44
/*
 *	This command turns on the display modules Tearing Effect output signal
 *	on the TE signal line when the display module reaches line N.
 */
#define get_scanline			0x45
/*
 *	The display module returns the current scanline, N, used to update the
 *	 display device. The total number of scanlines on a display device is
 *	defined as VSYNC + VBP + VACT + VFP.The first scanline is defined as
 *	the first line of V Sync and is denoted as Line 0.
 *	When in Sleep Mode, the value returned by get_scanline is undefined.
 */

/* MCS or Generic COMMANDS */
/* MCS/generic data type */
#define GEN_SHORT_WRITE_0	0x03  /* generic short write, no parameters */
#define GEN_SHORT_WRITE_1	0x13  /* generic short write, 1 parameters */
#define GEN_SHORT_WRITE_2	0x23  /* generic short write, 2 parameters */
#define GEN_READ_0		0x04  /* generic read, no parameters */
#define GEN_READ_1		0x14  /* generic read, 1 parameters */
#define GEN_READ_2		0x24  /* generic read, 2 parameters */
#define GEN_LONG_WRITE		0x29  /* generic long write */
#define MCS_SHORT_WRITE_0	0x05  /* MCS short write, no parameters */
#define MCS_SHORT_WRITE_1	0x15  /* MCS short write, 1 parameters */
#define MCS_READ		0x06  /* MCS read, no parameters */
#define MCS_LONG_WRITE		0x39  /* MCS long write */
/* MCS/generic commands */
/* TPO MCS */
#define write_display_profile		0x50
#define write_display_brightness	0x51
#define write_ctrl_display		0x53
#define write_ctrl_cabc			0x55
  #define UI_IMAGE		0x01
  #define STILL_IMAGE		0x02
  #define MOVING_IMAGE		0x03
#define write_hysteresis		0x57
#define write_gamma_setting		0x58
#define write_cabc_min_bright		0x5e
#define write_kbbc_profile		0x60
/* TMD MCS */
#define tmd_write_display_brightness 0x8c

/*
 *	This command is used to control ambient light, panel backlight
 *	brightness and gamma settings.
 */
#define BRIGHT_CNTL_BLOCK_ON	(1 << 5)
#define AMBIENT_LIGHT_SENSE_ON	(1 << 4)
#define DISPLAY_DIMMING_ON	(1 << 3)
#define BACKLIGHT_ON		(1 << 2)
#define DISPLAY_BRIGHTNESS_AUTO	(1 << 1)
#define GAMMA_AUTO		(1 << 0)

/* DCS Interface Pixel Formats */
#define DCS_PIXEL_FORMAT_3BPP	0x1
#define DCS_PIXEL_FORMAT_8BPP	0x2
#define DCS_PIXEL_FORMAT_12BPP	0x3
#define DCS_PIXEL_FORMAT_16BPP	0x5
#define DCS_PIXEL_FORMAT_18BPP	0x6
#define DCS_PIXEL_FORMAT_24BPP	0x7
/* ONE PARAMETER READ DATA */
#define addr_mode_data		0xfc
#define diag_res_data		0x00
#define disp_mode_data		0x23
#define pxl_fmt_data		0x77
#define pwr_mode_data		0x74
#define sig_mode_data		0x00
/* TWO PARAMETERS READ DATA */
#define scanline_data1		0xff
#define scanline_data2		0xff
#define NON_BURST_MODE_SYNC_PULSE	0x01	/* Non Burst Mode
						 * with Sync Pulse
						 */
#define NON_BURST_MODE_SYNC_EVENTS	0x02	/* Non Burst Mode
						 * with Sync events
						 */
#define BURST_MODE			0x03	/* Burst Mode */
#define DBI_COMMAND_BUFFER_SIZE		0x240   /* 0x32 */    /* 0x120 */
						/* Allocate at least
						 * 0x100 Byte with 32
						 * byte alignment
						 */
#define DBI_DATA_BUFFER_SIZE		0x120	/* Allocate at least
						 * 0x100 Byte with 32
						 * byte alignment
						 */
#define DBI_CB_TIME_OUT			0xFFFF

#define GEN_FB_TIME_OUT			2000

#define SKU_83				0x01
#define SKU_100				0x02
#define SKU_100L			0x04
#define SKU_BYPASS			0x08

/* Some handy macros for playing with bitfields. */
#define PSB_MASK(high, low) (((1<<((high)-(low)+1))-1)<<(low))
#define SET_FIELD(value, field) (((value) << field ## _SHIFT) & field ## _MASK)
#define GET_FIELD(word, field) (((word)  & field ## _MASK) >> field ## _SHIFT)

#define _PIPE(pipe, a, b) ((a) + (pipe)*((b)-(a)))

/* PCI config space */

#define SB_PCKT         0x02100 /* cedarview */
# define SB_OPCODE_MASK                         PSB_MASK(31, 16)
# define SB_OPCODE_SHIFT                        16
# define SB_OPCODE_READ                         0
# define SB_OPCODE_WRITE                        1
# define SB_DEST_MASK                           PSB_MASK(15, 8)
# define SB_DEST_SHIFT                          8
# define SB_DEST_DPLL                           0x88
# define SB_BYTE_ENABLE_MASK                    PSB_MASK(7, 4)
# define SB_BYTE_ENABLE_SHIFT                   4
# define SB_BUSY                                (1 << 0)

#define DSPCLK_GATE_D		0x6200
# define VRHUNIT_CLOCK_GATE_DISABLE		(1 << 28) /* Fixed value on CDV */
# define DPOUNIT_CLOCK_GATE_DISABLE		(1 << 11)
# define DPIOUNIT_CLOCK_GATE_DISABLE		(1 << 6)

#define RAMCLK_GATE_D		0x6210

/* 32-bit value read/written from the DPIO reg. */
#define SB_DATA		0x02104 /* cedarview */
/* 32-bit address of the DPIO reg to be read/written. */
#define SB_ADDR		0x02108 /* cedarview */
#define DPIO_CFG	0x02110 /* cedarview */
# define DPIO_MODE_SELECT_1			(1 << 3)
# define DPIO_MODE_SELECT_0			(1 << 2)
# define DPIO_SFR_BYPASS			(1 << 1)
/* reset is active low */
# define DPIO_CMN_RESET_N			(1 << 0)

/* Cedarview sideband registers */
#define _SB_M_A			0x8008
#define _SB_M_B			0x8028
#define SB_M(pipe) _PIPE(pipe, _SB_M_A, _SB_M_B)
# define SB_M_DIVIDER_MASK			(0xFF << 24)
# define SB_M_DIVIDER_SHIFT			24

#define _SB_N_VCO_A		0x8014
#define _SB_N_VCO_B		0x8034
#define SB_N_VCO(pipe) _PIPE(pipe, _SB_N_VCO_A, _SB_N_VCO_B)
#define SB_N_VCO_SEL_MASK			PSB_MASK(31, 30)
#define SB_N_VCO_SEL_SHIFT			30
#define SB_N_DIVIDER_MASK			PSB_MASK(29, 26)
#define SB_N_DIVIDER_SHIFT			26
#define SB_N_CB_TUNE_MASK			PSB_MASK(25, 24)
#define SB_N_CB_TUNE_SHIFT			24

/* the bit 14:13 is used to select between the different reference clock for Pipe A/B */
#define SB_REF_DPLLA		0x8010
#define SB_REF_DPLLB		0x8030
#define	REF_CLK_MASK		(0x3 << 13)
#define REF_CLK_CORE		(0 << 13)
#define REF_CLK_DPLL		(1 << 13)
#define REF_CLK_DPLLA		(2 << 13)
/* For the DPLL B, it will use the reference clk from DPLL A when using (2 << 13) */

#define _SB_REF_A		0x8018
#define _SB_REF_B		0x8038
#define SB_REF_SFR(pipe)	_PIPE(pipe, _SB_REF_A, _SB_REF_B)

#define _SB_P_A			0x801c
#define _SB_P_B			0x803c
#define SB_P(pipe) _PIPE(pipe, _SB_P_A, _SB_P_B)
#define SB_P2_DIVIDER_MASK			PSB_MASK(31, 30)
#define SB_P2_DIVIDER_SHIFT			30
#define SB_P2_10				0 /* HDMI, DP, DAC */
#define SB_P2_5				1 /* DAC */
#define SB_P2_14				2 /* LVDS single */
#define SB_P2_7				3 /* LVDS double */
#define SB_P1_DIVIDER_MASK			PSB_MASK(15, 12)
#define SB_P1_DIVIDER_SHIFT			12

#define PSB_LANE0		0x120
#define PSB_LANE1		0x220
#define PSB_LANE2		0x2320
#define PSB_LANE3		0x2420

#define LANE_PLL_MASK		(0x7 << 20)
#define LANE_PLL_ENABLE		(0x3 << 20)
#define LANE_PLL_PIPE(p)	(((p) == 0) ? (1 << 21) : (0 << 21))


#endif
