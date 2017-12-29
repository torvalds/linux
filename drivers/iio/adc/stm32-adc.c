/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 * License type: GPLv2
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/timer/stm32-lptim-trigger.h>
#include <linux/iio/timer/stm32-timer-trigger.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "stm32-adc-core.h"

/* STM32F4 - Registers for each ADC instance */
#define STM32F4_ADC_SR			0x00
#define STM32F4_ADC_CR1			0x04
#define STM32F4_ADC_CR2			0x08
#define STM32F4_ADC_SMPR1		0x0C
#define STM32F4_ADC_SMPR2		0x10
#define STM32F4_ADC_HTR			0x24
#define STM32F4_ADC_LTR			0x28
#define STM32F4_ADC_SQR1		0x2C
#define STM32F4_ADC_SQR2		0x30
#define STM32F4_ADC_SQR3		0x34
#define STM32F4_ADC_JSQR		0x38
#define STM32F4_ADC_JDR1		0x3C
#define STM32F4_ADC_JDR2		0x40
#define STM32F4_ADC_JDR3		0x44
#define STM32F4_ADC_JDR4		0x48
#define STM32F4_ADC_DR			0x4C

/* STM32F4_ADC_SR - bit fields */
#define STM32F4_STRT			BIT(4)
#define STM32F4_EOC			BIT(1)

/* STM32F4_ADC_CR1 - bit fields */
#define STM32F4_RES_SHIFT		24
#define STM32F4_RES_MASK		GENMASK(25, 24)
#define STM32F4_SCAN			BIT(8)
#define STM32F4_EOCIE			BIT(5)

/* STM32F4_ADC_CR2 - bit fields */
#define STM32F4_SWSTART			BIT(30)
#define STM32F4_EXTEN_SHIFT		28
#define STM32F4_EXTEN_MASK		GENMASK(29, 28)
#define STM32F4_EXTSEL_SHIFT		24
#define STM32F4_EXTSEL_MASK		GENMASK(27, 24)
#define STM32F4_EOCS			BIT(10)
#define STM32F4_DDS			BIT(9)
#define STM32F4_DMA			BIT(8)
#define STM32F4_ADON			BIT(0)

/* STM32H7 - Registers for each ADC instance */
#define STM32H7_ADC_ISR			0x00
#define STM32H7_ADC_IER			0x04
#define STM32H7_ADC_CR			0x08
#define STM32H7_ADC_CFGR		0x0C
#define STM32H7_ADC_SMPR1		0x14
#define STM32H7_ADC_SMPR2		0x18
#define STM32H7_ADC_PCSEL		0x1C
#define STM32H7_ADC_SQR1		0x30
#define STM32H7_ADC_SQR2		0x34
#define STM32H7_ADC_SQR3		0x38
#define STM32H7_ADC_SQR4		0x3C
#define STM32H7_ADC_DR			0x40
#define STM32H7_ADC_CALFACT		0xC4
#define STM32H7_ADC_CALFACT2		0xC8

/* STM32H7_ADC_ISR - bit fields */
#define STM32H7_EOC			BIT(2)
#define STM32H7_ADRDY			BIT(0)

/* STM32H7_ADC_IER - bit fields */
#define STM32H7_EOCIE			STM32H7_EOC

/* STM32H7_ADC_CR - bit fields */
#define STM32H7_ADCAL			BIT(31)
#define STM32H7_ADCALDIF		BIT(30)
#define STM32H7_DEEPPWD			BIT(29)
#define STM32H7_ADVREGEN		BIT(28)
#define STM32H7_LINCALRDYW6		BIT(27)
#define STM32H7_LINCALRDYW5		BIT(26)
#define STM32H7_LINCALRDYW4		BIT(25)
#define STM32H7_LINCALRDYW3		BIT(24)
#define STM32H7_LINCALRDYW2		BIT(23)
#define STM32H7_LINCALRDYW1		BIT(22)
#define STM32H7_ADCALLIN		BIT(16)
#define STM32H7_BOOST			BIT(8)
#define STM32H7_ADSTP			BIT(4)
#define STM32H7_ADSTART			BIT(2)
#define STM32H7_ADDIS			BIT(1)
#define STM32H7_ADEN			BIT(0)

/* STM32H7_ADC_CFGR bit fields */
#define STM32H7_EXTEN_SHIFT		10
#define STM32H7_EXTEN_MASK		GENMASK(11, 10)
#define STM32H7_EXTSEL_SHIFT		5
#define STM32H7_EXTSEL_MASK		GENMASK(9, 5)
#define STM32H7_RES_SHIFT		2
#define STM32H7_RES_MASK		GENMASK(4, 2)
#define STM32H7_DMNGT_SHIFT		0
#define STM32H7_DMNGT_MASK		GENMASK(1, 0)

enum stm32h7_adc_dmngt {
	STM32H7_DMNGT_DR_ONLY,		/* Regular data in DR only */
	STM32H7_DMNGT_DMA_ONESHOT,	/* DMA one shot mode */
	STM32H7_DMNGT_DFSDM,		/* DFSDM mode */
	STM32H7_DMNGT_DMA_CIRC,		/* DMA circular mode */
};

/* STM32H7_ADC_CALFACT - bit fields */
#define STM32H7_CALFACT_D_SHIFT		16
#define STM32H7_CALFACT_D_MASK		GENMASK(26, 16)
#define STM32H7_CALFACT_S_SHIFT		0
#define STM32H7_CALFACT_S_MASK		GENMASK(10, 0)

/* STM32H7_ADC_CALFACT2 - bit fields */
#define STM32H7_LINCALFACT_SHIFT	0
#define STM32H7_LINCALFACT_MASK		GENMASK(29, 0)

/* Number of linear calibration shadow registers / LINCALRDYW control bits */
#define STM32H7_LINCALFACT_NUM		6

/* BOOST bit must be set on STM32H7 when ADC clock is above 20MHz */
#define STM32H7_BOOST_CLKRATE		20000000UL

#define STM32_ADC_MAX_SQ		16	/* SQ1..SQ16 */
#define STM32_ADC_MAX_SMP		7	/* SMPx range is [0..7] */
#define STM32_ADC_TIMEOUT_US		100000
#define STM32_ADC_TIMEOUT	(msecs_to_jiffies(STM32_ADC_TIMEOUT_US / 1000))

#define STM32_DMA_BUFFER_SIZE		PAGE_SIZE

/* External trigger enable */
enum stm32_adc_exten {
	STM32_EXTEN_SWTRIG,
	STM32_EXTEN_HWTRIG_RISING_EDGE,
	STM32_EXTEN_HWTRIG_FALLING_EDGE,
	STM32_EXTEN_HWTRIG_BOTH_EDGES,
};

/* extsel - trigger mux selection value */
enum stm32_adc_extsel {
	STM32_EXT0,
	STM32_EXT1,
	STM32_EXT2,
	STM32_EXT3,
	STM32_EXT4,
	STM32_EXT5,
	STM32_EXT6,
	STM32_EXT7,
	STM32_EXT8,
	STM32_EXT9,
	STM32_EXT10,
	STM32_EXT11,
	STM32_EXT12,
	STM32_EXT13,
	STM32_EXT14,
	STM32_EXT15,
	STM32_EXT16,
	STM32_EXT17,
	STM32_EXT18,
	STM32_EXT19,
	STM32_EXT20,
};

/**
 * struct stm32_adc_trig_info - ADC trigger info
 * @name:		name of the trigger, corresponding to its source
 * @extsel:		trigger selection
 */
struct stm32_adc_trig_info {
	const char *name;
	enum stm32_adc_extsel extsel;
};

/**
 * struct stm32_adc_calib - optional adc calibration data
 * @calfact_s: Calibration offset for single ended channels
 * @calfact_d: Calibration offset in differential
 * @lincalfact: Linearity calibration factor
 */
struct stm32_adc_calib {
	u32			calfact_s;
	u32			calfact_d;
	u32			lincalfact[STM32H7_LINCALFACT_NUM];
};

/**
 * stm32_adc_regs - stm32 ADC misc registers & bitfield desc
 * @reg:		register offset
 * @mask:		bitfield mask
 * @shift:		left shift
 */
struct stm32_adc_regs {
	int reg;
	int mask;
	int shift;
};

/**
 * stm32_adc_regspec - stm32 registers definition, compatible dependent data
 * @dr:			data register offset
 * @ier_eoc:		interrupt enable register & eocie bitfield
 * @isr_eoc:		interrupt status register & eoc bitfield
 * @sqr:		reference to sequence registers array
 * @exten:		trigger control register & bitfield
 * @extsel:		trigger selection register & bitfield
 * @res:		resolution selection register & bitfield
 * @smpr:		smpr1 & smpr2 registers offset array
 * @smp_bits:		smpr1 & smpr2 index and bitfields
 */
struct stm32_adc_regspec {
	const u32 dr;
	const struct stm32_adc_regs ier_eoc;
	const struct stm32_adc_regs isr_eoc;
	const struct stm32_adc_regs *sqr;
	const struct stm32_adc_regs exten;
	const struct stm32_adc_regs extsel;
	const struct stm32_adc_regs res;
	const u32 smpr[2];
	const struct stm32_adc_regs *smp_bits;
};

struct stm32_adc;

/**
 * stm32_adc_cfg - stm32 compatible configuration data
 * @regs:		registers descriptions
 * @adc_info:		per instance input channels definitions
 * @trigs:		external trigger sources
 * @clk_required:	clock is required
 * @selfcalib:		optional routine for self-calibration
 * @prepare:		optional prepare routine (power-up, enable)
 * @start_conv:		routine to start conversions
 * @stop_conv:		routine to stop conversions
 * @unprepare:		optional unprepare routine (disable, power-down)
 * @smp_cycles:		programmable sampling time (ADC clock cycles)
 */
struct stm32_adc_cfg {
	const struct stm32_adc_regspec	*regs;
	const struct stm32_adc_info	*adc_info;
	struct stm32_adc_trig_info	*trigs;
	bool clk_required;
	int (*selfcalib)(struct stm32_adc *);
	int (*prepare)(struct stm32_adc *);
	void (*start_conv)(struct stm32_adc *, bool dma);
	void (*stop_conv)(struct stm32_adc *);
	void (*unprepare)(struct stm32_adc *);
	const unsigned int *smp_cycles;
};

/**
 * struct stm32_adc - private data of each ADC IIO instance
 * @common:		reference to ADC block common data
 * @offset:		ADC instance register offset in ADC block
 * @cfg:		compatible configuration data
 * @completion:		end of single conversion completion
 * @buffer:		data buffer
 * @clk:		clock for this adc instance
 * @irq:		interrupt for this adc instance
 * @lock:		spinlock
 * @bufi:		data buffer index
 * @num_conv:		expected number of scan conversions
 * @res:		data resolution (e.g. RES bitfield value)
 * @trigger_polarity:	external trigger polarity (e.g. exten)
 * @dma_chan:		dma channel
 * @rx_buf:		dma rx buffer cpu address
 * @rx_dma_buf:		dma rx buffer bus address
 * @rx_buf_sz:		dma rx buffer size
 * @pcsel		bitmask to preselect channels on some devices
 * @smpr_val:		sampling time settings (e.g. smpr1 / smpr2)
 * @cal:		optional calibration data on some devices
 */
struct stm32_adc {
	struct stm32_adc_common	*common;
	u32			offset;
	const struct stm32_adc_cfg	*cfg;
	struct completion	completion;
	u16			buffer[STM32_ADC_MAX_SQ];
	struct clk		*clk;
	int			irq;
	spinlock_t		lock;		/* interrupt lock */
	unsigned int		bufi;
	unsigned int		num_conv;
	u32			res;
	u32			trigger_polarity;
	struct dma_chan		*dma_chan;
	u8			*rx_buf;
	dma_addr_t		rx_dma_buf;
	unsigned int		rx_buf_sz;
	u32			pcsel;
	u32			smpr_val[2];
	struct stm32_adc_calib	cal;
};

/**
 * struct stm32_adc_chan_spec - specification of stm32 adc channel
 * @type:	IIO channel type
 * @channel:	channel number (single ended)
 * @name:	channel name (single ended)
 */
struct stm32_adc_chan_spec {
	enum iio_chan_type	type;
	int			channel;
	const char		*name;
};

/**
 * struct stm32_adc_info - stm32 ADC, per instance config data
 * @channels:		Reference to stm32 channels spec
 * @max_channels:	Number of channels
 * @resolutions:	available resolutions
 * @num_res:		number of available resolutions
 */
struct stm32_adc_info {
	const struct stm32_adc_chan_spec *channels;
	int max_channels;
	const unsigned int *resolutions;
	const unsigned int num_res;
};

/*
 * Input definitions common for all instances:
 * stm32f4 can have up to 16 channels
 * stm32h7 can have up to 20 channels
 */
static const struct stm32_adc_chan_spec stm32_adc_channels[] = {
	{ IIO_VOLTAGE, 0, "in0" },
	{ IIO_VOLTAGE, 1, "in1" },
	{ IIO_VOLTAGE, 2, "in2" },
	{ IIO_VOLTAGE, 3, "in3" },
	{ IIO_VOLTAGE, 4, "in4" },
	{ IIO_VOLTAGE, 5, "in5" },
	{ IIO_VOLTAGE, 6, "in6" },
	{ IIO_VOLTAGE, 7, "in7" },
	{ IIO_VOLTAGE, 8, "in8" },
	{ IIO_VOLTAGE, 9, "in9" },
	{ IIO_VOLTAGE, 10, "in10" },
	{ IIO_VOLTAGE, 11, "in11" },
	{ IIO_VOLTAGE, 12, "in12" },
	{ IIO_VOLTAGE, 13, "in13" },
	{ IIO_VOLTAGE, 14, "in14" },
	{ IIO_VOLTAGE, 15, "in15" },
	{ IIO_VOLTAGE, 16, "in16" },
	{ IIO_VOLTAGE, 17, "in17" },
	{ IIO_VOLTAGE, 18, "in18" },
	{ IIO_VOLTAGE, 19, "in19" },
};

static const unsigned int stm32f4_adc_resolutions[] = {
	/* sorted values so the index matches RES[1:0] in STM32F4_ADC_CR1 */
	12, 10, 8, 6,
};

static const struct stm32_adc_info stm32f4_adc_info = {
	.channels = stm32_adc_channels,
	.max_channels = 16,
	.resolutions = stm32f4_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32f4_adc_resolutions),
};

static const unsigned int stm32h7_adc_resolutions[] = {
	/* sorted values so the index matches RES[2:0] in STM32H7_ADC_CFGR */
	16, 14, 12, 10, 8,
};

static const struct stm32_adc_info stm32h7_adc_info = {
	.channels = stm32_adc_channels,
	.max_channels = 20,
	.resolutions = stm32h7_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32h7_adc_resolutions),
};

/**
 * stm32f4_sq - describe regular sequence registers
 * - L: sequence len (register & bit field)
 * - SQ1..SQ16: sequence entries (register & bit field)
 */
static const struct stm32_adc_regs stm32f4_sq[STM32_ADC_MAX_SQ + 1] = {
	/* L: len bit field description to be kept as first element */
	{ STM32F4_ADC_SQR1, GENMASK(23, 20), 20 },
	/* SQ1..SQ16 registers & bit fields (reg, mask, shift) */
	{ STM32F4_ADC_SQR3, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR3, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR3, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR3, GENMASK(19, 15), 15 },
	{ STM32F4_ADC_SQR3, GENMASK(24, 20), 20 },
	{ STM32F4_ADC_SQR3, GENMASK(29, 25), 25 },
	{ STM32F4_ADC_SQR2, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR2, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR2, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR2, GENMASK(19, 15), 15 },
	{ STM32F4_ADC_SQR2, GENMASK(24, 20), 20 },
	{ STM32F4_ADC_SQR2, GENMASK(29, 25), 25 },
	{ STM32F4_ADC_SQR1, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR1, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR1, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR1, GENMASK(19, 15), 15 },
};

/* STM32F4 external trigger sources for all instances */
static struct stm32_adc_trig_info stm32f4_adc_trigs[] = {
	{ TIM1_CH1, STM32_EXT0 },
	{ TIM1_CH2, STM32_EXT1 },
	{ TIM1_CH3, STM32_EXT2 },
	{ TIM2_CH2, STM32_EXT3 },
	{ TIM2_CH3, STM32_EXT4 },
	{ TIM2_CH4, STM32_EXT5 },
	{ TIM2_TRGO, STM32_EXT6 },
	{ TIM3_CH1, STM32_EXT7 },
	{ TIM3_TRGO, STM32_EXT8 },
	{ TIM4_CH4, STM32_EXT9 },
	{ TIM5_CH1, STM32_EXT10 },
	{ TIM5_CH2, STM32_EXT11 },
	{ TIM5_CH3, STM32_EXT12 },
	{ TIM8_CH1, STM32_EXT13 },
	{ TIM8_TRGO, STM32_EXT14 },
	{}, /* sentinel */
};

/**
 * stm32f4_smp_bits[] - describe sampling time register index & bit fields
 * Sorted so it can be indexed by channel number.
 */
static const struct stm32_adc_regs stm32f4_smp_bits[] = {
	/* STM32F4_ADC_SMPR2: smpr[] index, mask, shift for SMP0 to SMP9 */
	{ 1, GENMASK(2, 0), 0 },
	{ 1, GENMASK(5, 3), 3 },
	{ 1, GENMASK(8, 6), 6 },
	{ 1, GENMASK(11, 9), 9 },
	{ 1, GENMASK(14, 12), 12 },
	{ 1, GENMASK(17, 15), 15 },
	{ 1, GENMASK(20, 18), 18 },
	{ 1, GENMASK(23, 21), 21 },
	{ 1, GENMASK(26, 24), 24 },
	{ 1, GENMASK(29, 27), 27 },
	/* STM32F4_ADC_SMPR1, smpr[] index, mask, shift for SMP10 to SMP18 */
	{ 0, GENMASK(2, 0), 0 },
	{ 0, GENMASK(5, 3), 3 },
	{ 0, GENMASK(8, 6), 6 },
	{ 0, GENMASK(11, 9), 9 },
	{ 0, GENMASK(14, 12), 12 },
	{ 0, GENMASK(17, 15), 15 },
	{ 0, GENMASK(20, 18), 18 },
	{ 0, GENMASK(23, 21), 21 },
	{ 0, GENMASK(26, 24), 24 },
};

/* STM32F4 programmable sampling time (ADC clock cycles) */
static const unsigned int stm32f4_adc_smp_cycles[STM32_ADC_MAX_SMP + 1] = {
	3, 15, 28, 56, 84, 112, 144, 480,
};

static const struct stm32_adc_regspec stm32f4_adc_regspec = {
	.dr = STM32F4_ADC_DR,
	.ier_eoc = { STM32F4_ADC_CR1, STM32F4_EOCIE },
	.isr_eoc = { STM32F4_ADC_SR, STM32F4_EOC },
	.sqr = stm32f4_sq,
	.exten = { STM32F4_ADC_CR2, STM32F4_EXTEN_MASK, STM32F4_EXTEN_SHIFT },
	.extsel = { STM32F4_ADC_CR2, STM32F4_EXTSEL_MASK,
		    STM32F4_EXTSEL_SHIFT },
	.res = { STM32F4_ADC_CR1, STM32F4_RES_MASK, STM32F4_RES_SHIFT },
	.smpr = { STM32F4_ADC_SMPR1, STM32F4_ADC_SMPR2 },
	.smp_bits = stm32f4_smp_bits,
};

static const struct stm32_adc_regs stm32h7_sq[STM32_ADC_MAX_SQ + 1] = {
	/* L: len bit field description to be kept as first element */
	{ STM32H7_ADC_SQR1, GENMASK(3, 0), 0 },
	/* SQ1..SQ16 registers & bit fields (reg, mask, shift) */
	{ STM32H7_ADC_SQR1, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR1, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR1, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR1, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR2, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR2, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR2, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR2, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR2, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR3, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR3, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR3, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR3, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR3, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR4, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR4, GENMASK(10, 6), 6 },
};

/* STM32H7 external trigger sources for all instances */
static struct stm32_adc_trig_info stm32h7_adc_trigs[] = {
	{ TIM1_CH1, STM32_EXT0 },
	{ TIM1_CH2, STM32_EXT1 },
	{ TIM1_CH3, STM32_EXT2 },
	{ TIM2_CH2, STM32_EXT3 },
	{ TIM3_TRGO, STM32_EXT4 },
	{ TIM4_CH4, STM32_EXT5 },
	{ TIM8_TRGO, STM32_EXT7 },
	{ TIM8_TRGO2, STM32_EXT8 },
	{ TIM1_TRGO, STM32_EXT9 },
	{ TIM1_TRGO2, STM32_EXT10 },
	{ TIM2_TRGO, STM32_EXT11 },
	{ TIM4_TRGO, STM32_EXT12 },
	{ TIM6_TRGO, STM32_EXT13 },
	{ TIM15_TRGO, STM32_EXT14 },
	{ TIM3_CH4, STM32_EXT15 },
	{ LPTIM1_OUT, STM32_EXT18 },
	{ LPTIM2_OUT, STM32_EXT19 },
	{ LPTIM3_OUT, STM32_EXT20 },
	{},
};

/**
 * stm32h7_smp_bits - describe sampling time register index & bit fields
 * Sorted so it can be indexed by channel number.
 */
static const struct stm32_adc_regs stm32h7_smp_bits[] = {
	/* STM32H7_ADC_SMPR1, smpr[] index, mask, shift for SMP0 to SMP9 */
	{ 0, GENMASK(2, 0), 0 },
	{ 0, GENMASK(5, 3), 3 },
	{ 0, GENMASK(8, 6), 6 },
	{ 0, GENMASK(11, 9), 9 },
	{ 0, GENMASK(14, 12), 12 },
	{ 0, GENMASK(17, 15), 15 },
	{ 0, GENMASK(20, 18), 18 },
	{ 0, GENMASK(23, 21), 21 },
	{ 0, GENMASK(26, 24), 24 },
	{ 0, GENMASK(29, 27), 27 },
	/* STM32H7_ADC_SMPR2, smpr[] index, mask, shift for SMP10 to SMP19 */
	{ 1, GENMASK(2, 0), 0 },
	{ 1, GENMASK(5, 3), 3 },
	{ 1, GENMASK(8, 6), 6 },
	{ 1, GENMASK(11, 9), 9 },
	{ 1, GENMASK(14, 12), 12 },
	{ 1, GENMASK(17, 15), 15 },
	{ 1, GENMASK(20, 18), 18 },
	{ 1, GENMASK(23, 21), 21 },
	{ 1, GENMASK(26, 24), 24 },
	{ 1, GENMASK(29, 27), 27 },
};

/* STM32H7 programmable sampling time (ADC clock cycles, rounded down) */
static const unsigned int stm32h7_adc_smp_cycles[STM32_ADC_MAX_SMP + 1] = {
	1, 2, 8, 16, 32, 64, 387, 810,
};

static const struct stm32_adc_regspec stm32h7_adc_regspec = {
	.dr = STM32H7_ADC_DR,
	.ier_eoc = { STM32H7_ADC_IER, STM32H7_EOCIE },
	.isr_eoc = { STM32H7_ADC_ISR, STM32H7_EOC },
	.sqr = stm32h7_sq,
	.exten = { STM32H7_ADC_CFGR, STM32H7_EXTEN_MASK, STM32H7_EXTEN_SHIFT },
	.extsel = { STM32H7_ADC_CFGR, STM32H7_EXTSEL_MASK,
		    STM32H7_EXTSEL_SHIFT },
	.res = { STM32H7_ADC_CFGR, STM32H7_RES_MASK, STM32H7_RES_SHIFT },
	.smpr = { STM32H7_ADC_SMPR1, STM32H7_ADC_SMPR2 },
	.smp_bits = stm32h7_smp_bits,
};

/**
 * STM32 ADC registers access routines
 * @adc: stm32 adc instance
 * @reg: reg offset in adc instance
 *
 * Note: All instances share same base, with 0x0, 0x100 or 0x200 offset resp.
 * for adc1, adc2 and adc3.
 */
static u32 stm32_adc_readl(struct stm32_adc *adc, u32 reg)
{
	return readl_relaxed(adc->common->base + adc->offset + reg);
}

#define stm32_adc_readl_addr(addr)	stm32_adc_readl(adc, addr)

#define stm32_adc_readl_poll_timeout(reg, val, cond, sleep_us, timeout_us) \
	readx_poll_timeout(stm32_adc_readl_addr, reg, val, \
			   cond, sleep_us, timeout_us)

static u16 stm32_adc_readw(struct stm32_adc *adc, u32 reg)
{
	return readw_relaxed(adc->common->base + adc->offset + reg);
}

static void stm32_adc_writel(struct stm32_adc *adc, u32 reg, u32 val)
{
	writel_relaxed(val, adc->common->base + adc->offset + reg);
}

static void stm32_adc_set_bits(struct stm32_adc *adc, u32 reg, u32 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);
	stm32_adc_writel(adc, reg, stm32_adc_readl(adc, reg) | bits);
	spin_unlock_irqrestore(&adc->lock, flags);
}

static void stm32_adc_clr_bits(struct stm32_adc *adc, u32 reg, u32 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);
	stm32_adc_writel(adc, reg, stm32_adc_readl(adc, reg) & ~bits);
	spin_unlock_irqrestore(&adc->lock, flags);
}

/**
 * stm32_adc_conv_irq_enable() - Enable end of conversion interrupt
 * @adc: stm32 adc instance
 */
static void stm32_adc_conv_irq_enable(struct stm32_adc *adc)
{
	stm32_adc_set_bits(adc, adc->cfg->regs->ier_eoc.reg,
			   adc->cfg->regs->ier_eoc.mask);
};

/**
 * stm32_adc_conv_irq_disable() - Disable end of conversion interrupt
 * @adc: stm32 adc instance
 */
static void stm32_adc_conv_irq_disable(struct stm32_adc *adc)
{
	stm32_adc_clr_bits(adc, adc->cfg->regs->ier_eoc.reg,
			   adc->cfg->regs->ier_eoc.mask);
}

static void stm32_adc_set_res(struct stm32_adc *adc)
{
	const struct stm32_adc_regs *res = &adc->cfg->regs->res;
	u32 val;

	val = stm32_adc_readl(adc, res->reg);
	val = (val & ~res->mask) | (adc->res << res->shift);
	stm32_adc_writel(adc, res->reg, val);
}

/**
 * stm32f4_adc_start_conv() - Start conversions for regular channels.
 * @adc: stm32 adc instance
 * @dma: use dma to transfer conversion result
 *
 * Start conversions for regular channels.
 * Also take care of normal or DMA mode. Circular DMA may be used for regular
 * conversions, in IIO buffer modes. Otherwise, use ADC interrupt with direct
 * DR read instead (e.g. read_raw, or triggered buffer mode without DMA).
 */
static void stm32f4_adc_start_conv(struct stm32_adc *adc, bool dma)
{
	stm32_adc_set_bits(adc, STM32F4_ADC_CR1, STM32F4_SCAN);

	if (dma)
		stm32_adc_set_bits(adc, STM32F4_ADC_CR2,
				   STM32F4_DMA | STM32F4_DDS);

	stm32_adc_set_bits(adc, STM32F4_ADC_CR2, STM32F4_EOCS | STM32F4_ADON);

	/* Wait for Power-up time (tSTAB from datasheet) */
	usleep_range(2, 3);

	/* Software start ? (e.g. trigger detection disabled ?) */
	if (!(stm32_adc_readl(adc, STM32F4_ADC_CR2) & STM32F4_EXTEN_MASK))
		stm32_adc_set_bits(adc, STM32F4_ADC_CR2, STM32F4_SWSTART);
}

static void stm32f4_adc_stop_conv(struct stm32_adc *adc)
{
	stm32_adc_clr_bits(adc, STM32F4_ADC_CR2, STM32F4_EXTEN_MASK);
	stm32_adc_clr_bits(adc, STM32F4_ADC_SR, STM32F4_STRT);

	stm32_adc_clr_bits(adc, STM32F4_ADC_CR1, STM32F4_SCAN);
	stm32_adc_clr_bits(adc, STM32F4_ADC_CR2,
			   STM32F4_ADON | STM32F4_DMA | STM32F4_DDS);
}

static void stm32h7_adc_start_conv(struct stm32_adc *adc, bool dma)
{
	enum stm32h7_adc_dmngt dmngt;
	unsigned long flags;
	u32 val;

	if (dma)
		dmngt = STM32H7_DMNGT_DMA_CIRC;
	else
		dmngt = STM32H7_DMNGT_DR_ONLY;

	spin_lock_irqsave(&adc->lock, flags);
	val = stm32_adc_readl(adc, STM32H7_ADC_CFGR);
	val = (val & ~STM32H7_DMNGT_MASK) | (dmngt << STM32H7_DMNGT_SHIFT);
	stm32_adc_writel(adc, STM32H7_ADC_CFGR, val);
	spin_unlock_irqrestore(&adc->lock, flags);

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADSTART);
}

static void stm32h7_adc_stop_conv(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADSTP);

	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & (STM32H7_ADSTART)),
					   100, STM32_ADC_TIMEOUT_US);
	if (ret)
		dev_warn(&indio_dev->dev, "stop failed\n");

	stm32_adc_clr_bits(adc, STM32H7_ADC_CFGR, STM32H7_DMNGT_MASK);
}

static void stm32h7_adc_exit_pwr_down(struct stm32_adc *adc)
{
	/* Exit deep power down, then enable ADC voltage regulator */
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADVREGEN);

	if (adc->common->rate > STM32H7_BOOST_CLKRATE)
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Wait for startup time */
	usleep_range(10, 20);
}

static void stm32h7_adc_enter_pwr_down(struct stm32_adc *adc)
{
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Setting DEEPPWD disables ADC vreg and clears ADVREGEN */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
}

static int stm32h7_adc_enable(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	/* Clear ADRDY by writing one, then enable ADC */
	stm32_adc_set_bits(adc, STM32H7_ADC_ISR, STM32H7_ADRDY);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADEN);

	/* Poll for ADRDY to be set (after adc startup time) */
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_ISR, val,
					   val & STM32H7_ADRDY,
					   100, STM32_ADC_TIMEOUT_US);
	if (ret) {
		stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_ADEN);
		dev_err(&indio_dev->dev, "Failed to enable ADC\n");
	}

	return ret;
}

static void stm32h7_adc_disable(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	/* Disable ADC and wait until it's effectively disabled */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADDIS);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADEN), 100,
					   STM32_ADC_TIMEOUT_US);
	if (ret)
		dev_warn(&indio_dev->dev, "Failed to disable\n");
}

/**
 * stm32h7_adc_read_selfcalib() - read calibration shadow regs, save result
 * @adc: stm32 adc instance
 */
static int stm32h7_adc_read_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int i, ret;
	u32 lincalrdyw_mask, val;

	/* Enable adc so LINCALRDYW1..6 bits are writable */
	ret = stm32h7_adc_enable(adc);
	if (ret)
		return ret;

	/* Read linearity calibration */
	lincalrdyw_mask = STM32H7_LINCALRDYW6;
	for (i = STM32H7_LINCALFACT_NUM - 1; i >= 0; i--) {
		/* Clear STM32H7_LINCALRDYW[6..1]: transfer calib to CALFACT2 */
		stm32_adc_clr_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);

		/* Poll: wait calib data to be ready in CALFACT2 register */
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   !(val & lincalrdyw_mask),
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to read calfact\n");
			goto disable;
		}

		val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT2);
		adc->cal.lincalfact[i] = (val & STM32H7_LINCALFACT_MASK);
		adc->cal.lincalfact[i] >>= STM32H7_LINCALFACT_SHIFT;

		lincalrdyw_mask >>= 1;
	}

	/* Read offset calibration */
	val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT);
	adc->cal.calfact_s = (val & STM32H7_CALFACT_S_MASK);
	adc->cal.calfact_s >>= STM32H7_CALFACT_S_SHIFT;
	adc->cal.calfact_d = (val & STM32H7_CALFACT_D_MASK);
	adc->cal.calfact_d >>= STM32H7_CALFACT_D_SHIFT;

disable:
	stm32h7_adc_disable(adc);

	return ret;
}

/**
 * stm32h7_adc_restore_selfcalib() - Restore saved self-calibration result
 * @adc: stm32 adc instance
 * Note: ADC must be enabled, with no on-going conversions.
 */
static int stm32h7_adc_restore_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int i, ret;
	u32 lincalrdyw_mask, val;

	val = (adc->cal.calfact_s << STM32H7_CALFACT_S_SHIFT) |
		(adc->cal.calfact_d << STM32H7_CALFACT_D_SHIFT);
	stm32_adc_writel(adc, STM32H7_ADC_CALFACT, val);

	lincalrdyw_mask = STM32H7_LINCALRDYW6;
	for (i = STM32H7_LINCALFACT_NUM - 1; i >= 0; i--) {
		/*
		 * Write saved calibration data to shadow registers:
		 * Write CALFACT2, and set LINCALRDYW[6..1] bit to trigger
		 * data write. Then poll to wait for complete transfer.
		 */
		val = adc->cal.lincalfact[i] << STM32H7_LINCALFACT_SHIFT;
		stm32_adc_writel(adc, STM32H7_ADC_CALFACT2, val);
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   val & lincalrdyw_mask,
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to write calfact\n");
			return ret;
		}

		/*
		 * Read back calibration data, has two effects:
		 * - It ensures bits LINCALRDYW[6..1] are kept cleared
		 *   for next time calibration needs to be restored.
		 * - BTW, bit clear triggers a read, then check data has been
		 *   correctly written.
		 */
		stm32_adc_clr_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   !(val & lincalrdyw_mask),
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to read calfact\n");
			return ret;
		}
		val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT2);
		if (val != adc->cal.lincalfact[i] << STM32H7_LINCALFACT_SHIFT) {
			dev_err(&indio_dev->dev, "calfact not consistent\n");
			return -EIO;
		}

		lincalrdyw_mask >>= 1;
	}

	return 0;
}

/**
 * Fixed timeout value for ADC calibration.
 * worst cases:
 * - low clock frequency
 * - maximum prescalers
 * Calibration requires:
 * - 131,072 ADC clock cycle for the linear calibration
 * - 20 ADC clock cycle for the offset calibration
 *
 * Set to 100ms for now
 */
#define STM32H7_ADC_CALIB_TIMEOUT_US		100000

/**
 * stm32h7_adc_selfcalib() - Procedure to calibrate ADC (from power down)
 * @adc: stm32 adc instance
 * Exit from power down, calibrate ADC, then return to power down.
 */
static int stm32h7_adc_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	stm32h7_adc_exit_pwr_down(adc);

	/*
	 * Select calibration mode:
	 * - Offset calibration for single ended inputs
	 * - No linearity calibration (do it later, before reading it)
	 */
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_ADCALDIF);
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_ADCALLIN);

	/* Start calibration, then wait for completion */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADCAL);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADCAL), 100,
					   STM32H7_ADC_CALIB_TIMEOUT_US);
	if (ret) {
		dev_err(&indio_dev->dev, "calibration failed\n");
		goto pwr_dwn;
	}

	/*
	 * Select calibration mode, then start calibration:
	 * - Offset calibration for differential input
	 * - Linearity calibration (needs to be done only once for single/diff)
	 *   will run simultaneously with offset calibration.
	 */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR,
			   STM32H7_ADCALDIF | STM32H7_ADCALLIN);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADCAL);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADCAL), 100,
					   STM32H7_ADC_CALIB_TIMEOUT_US);
	if (ret) {
		dev_err(&indio_dev->dev, "calibration failed\n");
		goto pwr_dwn;
	}

	stm32_adc_clr_bits(adc, STM32H7_ADC_CR,
			   STM32H7_ADCALDIF | STM32H7_ADCALLIN);

	/* Read calibration result for future reference */
	ret = stm32h7_adc_read_selfcalib(adc);

pwr_dwn:
	stm32h7_adc_enter_pwr_down(adc);

	return ret;
}

/**
 * stm32h7_adc_prepare() - Leave power down mode to enable ADC.
 * @adc: stm32 adc instance
 * Leave power down mode.
 * Enable ADC.
 * Restore calibration data.
 * Pre-select channels that may be used in PCSEL (required by input MUX / IO).
 */
static int stm32h7_adc_prepare(struct stm32_adc *adc)
{
	int ret;

	stm32h7_adc_exit_pwr_down(adc);

	ret = stm32h7_adc_enable(adc);
	if (ret)
		goto pwr_dwn;

	ret = stm32h7_adc_restore_selfcalib(adc);
	if (ret)
		goto disable;

	stm32_adc_writel(adc, STM32H7_ADC_PCSEL, adc->pcsel);

	return 0;

disable:
	stm32h7_adc_disable(adc);
pwr_dwn:
	stm32h7_adc_enter_pwr_down(adc);

	return ret;
}

static void stm32h7_adc_unprepare(struct stm32_adc *adc)
{
	stm32h7_adc_disable(adc);
	stm32h7_adc_enter_pwr_down(adc);
}

/**
 * stm32_adc_conf_scan_seq() - Build regular channels scan sequence
 * @indio_dev: IIO device
 * @scan_mask: channels to be converted
 *
 * Conversion sequence :
 * Apply sampling time settings for all channels.
 * Configure ADC scan sequence based on selected channels in scan_mask.
 * Add channels to SQR registers, from scan_mask LSB to MSB, then
 * program sequence len.
 */
static int stm32_adc_conf_scan_seq(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_regs *sqr = adc->cfg->regs->sqr;
	const struct iio_chan_spec *chan;
	u32 val, bit;
	int i = 0;

	/* Apply sampling time settings */
	stm32_adc_writel(adc, adc->cfg->regs->smpr[0], adc->smpr_val[0]);
	stm32_adc_writel(adc, adc->cfg->regs->smpr[1], adc->smpr_val[1]);

	for_each_set_bit(bit, scan_mask, indio_dev->masklength) {
		chan = indio_dev->channels + bit;
		/*
		 * Assign one channel per SQ entry in regular
		 * sequence, starting with SQ1.
		 */
		i++;
		if (i > STM32_ADC_MAX_SQ)
			return -EINVAL;

		dev_dbg(&indio_dev->dev, "%s chan %d to SQ%d\n",
			__func__, chan->channel, i);

		val = stm32_adc_readl(adc, sqr[i].reg);
		val &= ~sqr[i].mask;
		val |= chan->channel << sqr[i].shift;
		stm32_adc_writel(adc, sqr[i].reg, val);
	}

	if (!i)
		return -EINVAL;

	/* Sequence len */
	val = stm32_adc_readl(adc, sqr[0].reg);
	val &= ~sqr[0].mask;
	val |= ((i - 1) << sqr[0].shift);
	stm32_adc_writel(adc, sqr[0].reg, val);

	return 0;
}

/**
 * stm32_adc_get_trig_extsel() - Get external trigger selection
 * @trig: trigger
 *
 * Returns trigger extsel value, if trig matches, -EINVAL otherwise.
 */
static int stm32_adc_get_trig_extsel(struct iio_dev *indio_dev,
				     struct iio_trigger *trig)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int i;

	/* lookup triggers registered by stm32 timer trigger driver */
	for (i = 0; adc->cfg->trigs[i].name; i++) {
		/**
		 * Checking both stm32 timer trigger type and trig name
		 * should be safe against arbitrary trigger names.
		 */
		if ((is_stm32_timer_trigger(trig) ||
		     is_stm32_lptim_trigger(trig)) &&
		    !strcmp(adc->cfg->trigs[i].name, trig->name)) {
			return adc->cfg->trigs[i].extsel;
		}
	}

	return -EINVAL;
}

/**
 * stm32_adc_set_trig() - Set a regular trigger
 * @indio_dev: IIO device
 * @trig: IIO trigger
 *
 * Set trigger source/polarity (e.g. SW, or HW with polarity) :
 * - if HW trigger disabled (e.g. trig == NULL, conversion launched by sw)
 * - if HW trigger enabled, set source & polarity
 */
static int stm32_adc_set_trig(struct iio_dev *indio_dev,
			      struct iio_trigger *trig)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	u32 val, extsel = 0, exten = STM32_EXTEN_SWTRIG;
	unsigned long flags;
	int ret;

	if (trig) {
		ret = stm32_adc_get_trig_extsel(indio_dev, trig);
		if (ret < 0)
			return ret;

		/* set trigger source and polarity (default to rising edge) */
		extsel = ret;
		exten = adc->trigger_polarity + STM32_EXTEN_HWTRIG_RISING_EDGE;
	}

	spin_lock_irqsave(&adc->lock, flags);
	val = stm32_adc_readl(adc, adc->cfg->regs->exten.reg);
	val &= ~(adc->cfg->regs->exten.mask | adc->cfg->regs->extsel.mask);
	val |= exten << adc->cfg->regs->exten.shift;
	val |= extsel << adc->cfg->regs->extsel.shift;
	stm32_adc_writel(adc,  adc->cfg->regs->exten.reg, val);
	spin_unlock_irqrestore(&adc->lock, flags);

	return 0;
}

static int stm32_adc_set_trig_pol(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  unsigned int type)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	adc->trigger_polarity = type;

	return 0;
}

static int stm32_adc_get_trig_pol(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	return adc->trigger_polarity;
}

static const char * const stm32_trig_pol_items[] = {
	"rising-edge", "falling-edge", "both-edges",
};

static const struct iio_enum stm32_adc_trig_pol = {
	.items = stm32_trig_pol_items,
	.num_items = ARRAY_SIZE(stm32_trig_pol_items),
	.get = stm32_adc_get_trig_pol,
	.set = stm32_adc_set_trig_pol,
};

/**
 * stm32_adc_single_conv() - Performs a single conversion
 * @indio_dev: IIO device
 * @chan: IIO channel
 * @res: conversion result
 *
 * The function performs a single conversion on a given channel:
 * - Apply sampling time settings
 * - Program sequencer with one channel (e.g. in SQ1 with len = 1)
 * - Use SW trigger
 * - Start conversion, then wait for interrupt completion.
 */
static int stm32_adc_single_conv(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 int *res)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	long timeout;
	u32 val;
	int ret;

	reinit_completion(&adc->completion);

	adc->bufi = 0;

	if (adc->cfg->prepare) {
		ret = adc->cfg->prepare(adc);
		if (ret)
			return ret;
	}

	/* Apply sampling time settings */
	stm32_adc_writel(adc, regs->smpr[0], adc->smpr_val[0]);
	stm32_adc_writel(adc, regs->smpr[1], adc->smpr_val[1]);

	/* Program chan number in regular sequence (SQ1) */
	val = stm32_adc_readl(adc, regs->sqr[1].reg);
	val &= ~regs->sqr[1].mask;
	val |= chan->channel << regs->sqr[1].shift;
	stm32_adc_writel(adc, regs->sqr[1].reg, val);

	/* Set regular sequence len (0 for 1 conversion) */
	stm32_adc_clr_bits(adc, regs->sqr[0].reg, regs->sqr[0].mask);

	/* Trigger detection disabled (conversion can be launched in SW) */
	stm32_adc_clr_bits(adc, regs->exten.reg, regs->exten.mask);

	stm32_adc_conv_irq_enable(adc);

	adc->cfg->start_conv(adc, false);

	timeout = wait_for_completion_interruptible_timeout(
					&adc->completion, STM32_ADC_TIMEOUT);
	if (timeout == 0) {
		ret = -ETIMEDOUT;
	} else if (timeout < 0) {
		ret = timeout;
	} else {
		*res = adc->buffer[0];
		ret = IIO_VAL_INT;
	}

	adc->cfg->stop_conv(adc);

	stm32_adc_conv_irq_disable(adc);

	if (adc->cfg->unprepare)
		adc->cfg->unprepare(adc);

	return ret;
}

static int stm32_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		if (chan->type == IIO_VOLTAGE)
			ret = stm32_adc_single_conv(indio_dev, chan, val);
		else
			ret = -EINVAL;
		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		*val = adc->common->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static irqreturn_t stm32_adc_isr(int irq, void *data)
{
	struct stm32_adc *adc = data;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	u32 status = stm32_adc_readl(adc, regs->isr_eoc.reg);

	if (status & regs->isr_eoc.mask) {
		/* Reading DR also clears EOC status flag */
		adc->buffer[adc->bufi] = stm32_adc_readw(adc, regs->dr);
		if (iio_buffer_enabled(indio_dev)) {
			adc->bufi++;
			if (adc->bufi >= adc->num_conv) {
				stm32_adc_conv_irq_disable(adc);
				iio_trigger_poll(indio_dev->trig);
			}
		} else {
			complete(&adc->completion);
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/**
 * stm32_adc_validate_trigger() - validate trigger for stm32 adc
 * @indio_dev: IIO device
 * @trig: new trigger
 *
 * Returns: 0 if trig matches one of the triggers registered by stm32 adc
 * driver, -EINVAL otherwise.
 */
static int stm32_adc_validate_trigger(struct iio_dev *indio_dev,
				      struct iio_trigger *trig)
{
	return stm32_adc_get_trig_extsel(indio_dev, trig) < 0 ? -EINVAL : 0;
}

static int stm32_adc_set_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	unsigned int watermark = STM32_DMA_BUFFER_SIZE / 2;

	/*
	 * dma cyclic transfers are used, buffer is split into two periods.
	 * There should be :
	 * - always one buffer (period) dma is working on
	 * - one buffer (period) driver can push with iio_trigger_poll().
	 */
	watermark = min(watermark, val * (unsigned)(sizeof(u16)));
	adc->rx_buf_sz = watermark * 2;

	return 0;
}

static int stm32_adc_update_scan_mode(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	adc->num_conv = bitmap_weight(scan_mask, indio_dev->masklength);

	ret = stm32_adc_conf_scan_seq(indio_dev, scan_mask);
	if (ret)
		return ret;

	return 0;
}

static int stm32_adc_of_xlate(struct iio_dev *indio_dev,
			      const struct of_phandle_args *iiospec)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

/**
 * stm32_adc_debugfs_reg_access - read or write register value
 *
 * To read a value from an ADC register:
 *   echo [ADC reg offset] > direct_reg_access
 *   cat direct_reg_access
 *
 * To write a value in a ADC register:
 *   echo [ADC_reg_offset] [value] > direct_reg_access
 */
static int stm32_adc_debugfs_reg_access(struct iio_dev *indio_dev,
					unsigned reg, unsigned writeval,
					unsigned *readval)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	if (!readval)
		stm32_adc_writel(adc, reg, writeval);
	else
		*readval = stm32_adc_readl(adc, reg);

	return 0;
}

static const struct iio_info stm32_adc_iio_info = {
	.read_raw = stm32_adc_read_raw,
	.validate_trigger = stm32_adc_validate_trigger,
	.hwfifo_set_watermark = stm32_adc_set_watermark,
	.update_scan_mode = stm32_adc_update_scan_mode,
	.debugfs_reg_access = stm32_adc_debugfs_reg_access,
	.of_xlate = stm32_adc_of_xlate,
};

static unsigned int stm32_adc_dma_residue(struct stm32_adc *adc)
{
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(adc->dma_chan,
				     adc->dma_chan->cookie,
				     &state);
	if (status == DMA_IN_PROGRESS) {
		/* Residue is size in bytes from end of buffer */
		unsigned int i = adc->rx_buf_sz - state.residue;
		unsigned int size;

		/* Return available bytes */
		if (i >= adc->bufi)
			size = i - adc->bufi;
		else
			size = adc->rx_buf_sz + i - adc->bufi;

		return size;
	}

	return 0;
}

static void stm32_adc_dma_buffer_done(void *data)
{
	struct iio_dev *indio_dev = data;

	iio_trigger_poll_chained(indio_dev->trig);
}

static int stm32_adc_dma_start(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int ret;

	if (!adc->dma_chan)
		return 0;

	dev_dbg(&indio_dev->dev, "%s size=%d watermark=%d\n", __func__,
		adc->rx_buf_sz, adc->rx_buf_sz / 2);

	/* Prepare a DMA cyclic transaction */
	desc = dmaengine_prep_dma_cyclic(adc->dma_chan,
					 adc->rx_dma_buf,
					 adc->rx_buf_sz, adc->rx_buf_sz / 2,
					 DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = stm32_adc_dma_buffer_done;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_all(adc->dma_chan);
		return ret;
	}

	/* Issue pending DMA requests */
	dma_async_issue_pending(adc->dma_chan);

	return 0;
}

static int stm32_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	if (adc->cfg->prepare) {
		ret = adc->cfg->prepare(adc);
		if (ret)
			return ret;
	}

	ret = stm32_adc_set_trig(indio_dev, indio_dev->trig);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't set trigger\n");
		goto err_unprepare;
	}

	ret = stm32_adc_dma_start(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't start dma\n");
		goto err_clr_trig;
	}

	ret = iio_triggered_buffer_postenable(indio_dev);
	if (ret < 0)
		goto err_stop_dma;

	/* Reset adc buffer index */
	adc->bufi = 0;

	if (!adc->dma_chan)
		stm32_adc_conv_irq_enable(adc);

	adc->cfg->start_conv(adc, !!adc->dma_chan);

	return 0;

err_stop_dma:
	if (adc->dma_chan)
		dmaengine_terminate_all(adc->dma_chan);
err_clr_trig:
	stm32_adc_set_trig(indio_dev, NULL);
err_unprepare:
	if (adc->cfg->unprepare)
		adc->cfg->unprepare(adc);

	return ret;
}

static int stm32_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	adc->cfg->stop_conv(adc);
	if (!adc->dma_chan)
		stm32_adc_conv_irq_disable(adc);

	ret = iio_triggered_buffer_predisable(indio_dev);
	if (ret < 0)
		dev_err(&indio_dev->dev, "predisable failed\n");

	if (adc->dma_chan)
		dmaengine_terminate_all(adc->dma_chan);

	if (stm32_adc_set_trig(indio_dev, NULL))
		dev_err(&indio_dev->dev, "Can't clear trigger\n");

	if (adc->cfg->unprepare)
		adc->cfg->unprepare(adc);

	return ret;
}

static const struct iio_buffer_setup_ops stm32_adc_buffer_setup_ops = {
	.postenable = &stm32_adc_buffer_postenable,
	.predisable = &stm32_adc_buffer_predisable,
};

static irqreturn_t stm32_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct stm32_adc *adc = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "%s bufi=%d\n", __func__, adc->bufi);

	if (!adc->dma_chan) {
		/* reset buffer index */
		adc->bufi = 0;
		iio_push_to_buffers_with_timestamp(indio_dev, adc->buffer,
						   pf->timestamp);
	} else {
		int residue = stm32_adc_dma_residue(adc);

		while (residue >= indio_dev->scan_bytes) {
			u16 *buffer = (u16 *)&adc->rx_buf[adc->bufi];

			iio_push_to_buffers_with_timestamp(indio_dev, buffer,
							   pf->timestamp);
			residue -= indio_dev->scan_bytes;
			adc->bufi += indio_dev->scan_bytes;
			if (adc->bufi >= adc->rx_buf_sz)
				adc->bufi = 0;
		}
	}

	iio_trigger_notify_done(indio_dev->trig);

	/* re-enable eoc irq */
	if (!adc->dma_chan)
		stm32_adc_conv_irq_enable(adc);

	return IRQ_HANDLED;
}

static const struct iio_chan_spec_ext_info stm32_adc_ext_info[] = {
	IIO_ENUM("trigger_polarity", IIO_SHARED_BY_ALL, &stm32_adc_trig_pol),
	{
		.name = "trigger_polarity_available",
		.shared = IIO_SHARED_BY_ALL,
		.read = iio_enum_available_read,
		.private = (uintptr_t)&stm32_adc_trig_pol,
	},
	{},
};

static int stm32_adc_of_get_resolution(struct iio_dev *indio_dev)
{
	struct device_node *node = indio_dev->dev.of_node;
	struct stm32_adc *adc = iio_priv(indio_dev);
	unsigned int i;
	u32 res;

	if (of_property_read_u32(node, "assigned-resolution-bits", &res))
		res = adc->cfg->adc_info->resolutions[0];

	for (i = 0; i < adc->cfg->adc_info->num_res; i++)
		if (res == adc->cfg->adc_info->resolutions[i])
			break;
	if (i >= adc->cfg->adc_info->num_res) {
		dev_err(&indio_dev->dev, "Bad resolution: %u bits\n", res);
		return -EINVAL;
	}

	dev_dbg(&indio_dev->dev, "Using %u bits resolution\n", res);
	adc->res = i;

	return 0;
}

static void stm32_adc_smpr_init(struct stm32_adc *adc, int channel, u32 smp_ns)
{
	const struct stm32_adc_regs *smpr = &adc->cfg->regs->smp_bits[channel];
	u32 period_ns, shift = smpr->shift, mask = smpr->mask;
	unsigned int smp, r = smpr->reg;

	/* Determine sampling time (ADC clock cycles) */
	period_ns = NSEC_PER_SEC / adc->common->rate;
	for (smp = 0; smp <= STM32_ADC_MAX_SMP; smp++)
		if ((period_ns * adc->cfg->smp_cycles[smp]) >= smp_ns)
			break;
	if (smp > STM32_ADC_MAX_SMP)
		smp = STM32_ADC_MAX_SMP;

	/* pre-build sampling time registers (e.g. smpr1, smpr2) */
	adc->smpr_val[r] = (adc->smpr_val[r] & ~mask) | (smp << shift);
}

static void stm32_adc_chan_init_one(struct iio_dev *indio_dev,
				    struct iio_chan_spec *chan,
				    const struct stm32_adc_chan_spec *channel,
				    int scan_index, u32 smp)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	chan->type = channel->type;
	chan->channel = channel->channel;
	chan->datasheet_name = channel->name;
	chan->scan_index = scan_index;
	chan->indexed = 1;
	chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
	chan->scan_type.sign = 'u';
	chan->scan_type.realbits = adc->cfg->adc_info->resolutions[adc->res];
	chan->scan_type.storagebits = 16;
	chan->ext_info = stm32_adc_ext_info;

	/* Prepare sampling time settings */
	stm32_adc_smpr_init(adc, chan->channel, smp);

	/* pre-build selected channels mask */
	adc->pcsel |= BIT(chan->channel);
}

static int stm32_adc_chan_of_init(struct iio_dev *indio_dev)
{
	struct device_node *node = indio_dev->dev.of_node;
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	struct property *prop;
	const __be32 *cur;
	struct iio_chan_spec *channels;
	int scan_index = 0, num_channels, ret;
	u32 val, smp = 0;

	num_channels = of_property_count_u32_elems(node, "st,adc-channels");
	if (num_channels < 0 ||
	    num_channels > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Bad st,adc-channels?\n");
		return num_channels < 0 ? num_channels : -EINVAL;
	}

	/* Optional sample time is provided either for each, or all channels */
	ret = of_property_count_u32_elems(node, "st,min-sample-time-nsecs");
	if (ret > 1 && ret != num_channels) {
		dev_err(&indio_dev->dev, "Invalid st,min-sample-time-nsecs\n");
		return -EINVAL;
	}

	channels = devm_kcalloc(&indio_dev->dev, num_channels,
				sizeof(struct iio_chan_spec), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	of_property_for_each_u32(node, "st,adc-channels", prop, cur, val) {
		if (val >= adc_info->max_channels) {
			dev_err(&indio_dev->dev, "Invalid channel %d\n", val);
			return -EINVAL;
		}

		/*
		 * Using of_property_read_u32_index(), smp value will only be
		 * modified if valid u32 value can be decoded. This allows to
		 * get either no value, 1 shared value for all indexes, or one
		 * value per channel.
		 */
		of_property_read_u32_index(node, "st,min-sample-time-nsecs",
					   scan_index, &smp);

		stm32_adc_chan_init_one(indio_dev, &channels[scan_index],
					&adc_info->channels[val],
					scan_index, smp);
		scan_index++;
	}

	indio_dev->num_channels = scan_index;
	indio_dev->channels = channels;

	return 0;
}

static int stm32_adc_dma_request(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct dma_slave_config config;
	int ret;

	adc->dma_chan = dma_request_slave_channel(&indio_dev->dev, "rx");
	if (!adc->dma_chan)
		return 0;

	adc->rx_buf = dma_alloc_coherent(adc->dma_chan->device->dev,
					 STM32_DMA_BUFFER_SIZE,
					 &adc->rx_dma_buf, GFP_KERNEL);
	if (!adc->rx_buf) {
		ret = -ENOMEM;
		goto err_release;
	}

	/* Configure DMA channel to read data register */
	memset(&config, 0, sizeof(config));
	config.src_addr = (dma_addr_t)adc->common->phys_base;
	config.src_addr += adc->offset + adc->cfg->regs->dr;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	ret = dmaengine_slave_config(adc->dma_chan, &config);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dma_free_coherent(adc->dma_chan->device->dev, STM32_DMA_BUFFER_SIZE,
			  adc->rx_buf, adc->rx_dma_buf);
err_release:
	dma_release_channel(adc->dma_chan);

	return ret;
}

static int stm32_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	struct stm32_adc *adc;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->common = dev_get_drvdata(pdev->dev.parent);
	spin_lock_init(&adc->lock);
	init_completion(&adc->completion);
	adc->cfg = (const struct stm32_adc_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &stm32_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_HARDWARE_TRIGGERED;

	platform_set_drvdata(pdev, adc);

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &adc->offset);
	if (ret != 0) {
		dev_err(&pdev->dev, "missing reg property\n");
		return -EINVAL;
	}

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return adc->irq;
	}

	ret = devm_request_irq(&pdev->dev, adc->irq, stm32_adc_isr,
			       0, pdev->name, adc);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return ret;
	}

	adc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(adc->clk)) {
		ret = PTR_ERR(adc->clk);
		if (ret == -ENOENT && !adc->cfg->clk_required) {
			adc->clk = NULL;
		} else {
			dev_err(&pdev->dev, "Can't get clock\n");
			return ret;
		}
	}

	if (adc->clk) {
		ret = clk_prepare_enable(adc->clk);
		if (ret < 0) {
			dev_err(&pdev->dev, "clk enable failed\n");
			return ret;
		}
	}

	ret = stm32_adc_of_get_resolution(indio_dev);
	if (ret < 0)
		goto err_clk_disable;
	stm32_adc_set_res(adc);

	if (adc->cfg->selfcalib) {
		ret = adc->cfg->selfcalib(adc);
		if (ret)
			goto err_clk_disable;
	}

	ret = stm32_adc_chan_of_init(indio_dev);
	if (ret < 0)
		goto err_clk_disable;

	ret = stm32_adc_dma_request(indio_dev);
	if (ret < 0)
		goto err_clk_disable;

	ret = iio_triggered_buffer_setup(indio_dev,
					 &iio_pollfunc_store_time,
					 &stm32_adc_trigger_handler,
					 &stm32_adc_buffer_setup_ops);
	if (ret) {
		dev_err(&pdev->dev, "buffer setup failed\n");
		goto err_dma_disable;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "iio dev register failed\n");
		goto err_buffer_cleanup;
	}

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);

err_dma_disable:
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}
err_clk_disable:
	if (adc->clk)
		clk_disable_unprepare(adc->clk);

	return ret;
}

static int stm32_adc_remove(struct platform_device *pdev)
{
	struct stm32_adc *adc = platform_get_drvdata(pdev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}
	if (adc->clk)
		clk_disable_unprepare(adc->clk);

	return 0;
}

static const struct stm32_adc_cfg stm32f4_adc_cfg = {
	.regs = &stm32f4_adc_regspec,
	.adc_info = &stm32f4_adc_info,
	.trigs = stm32f4_adc_trigs,
	.clk_required = true,
	.start_conv = stm32f4_adc_start_conv,
	.stop_conv = stm32f4_adc_stop_conv,
	.smp_cycles = stm32f4_adc_smp_cycles,
};

static const struct stm32_adc_cfg stm32h7_adc_cfg = {
	.regs = &stm32h7_adc_regspec,
	.adc_info = &stm32h7_adc_info,
	.trigs = stm32h7_adc_trigs,
	.selfcalib = stm32h7_adc_selfcalib,
	.start_conv = stm32h7_adc_start_conv,
	.stop_conv = stm32h7_adc_stop_conv,
	.prepare = stm32h7_adc_prepare,
	.unprepare = stm32h7_adc_unprepare,
	.smp_cycles = stm32h7_adc_smp_cycles,
};

static const struct of_device_id stm32_adc_of_match[] = {
	{ .compatible = "st,stm32f4-adc", .data = (void *)&stm32f4_adc_cfg },
	{ .compatible = "st,stm32h7-adc", .data = (void *)&stm32h7_adc_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_adc_of_match);

static struct platform_driver stm32_adc_driver = {
	.probe = stm32_adc_probe,
	.remove = stm32_adc_remove,
	.driver = {
		.name = "stm32-adc",
		.of_match_table = stm32_adc_of_match,
	},
};
module_platform_driver(stm32_adc_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 ADC IIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-adc");
