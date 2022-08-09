// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
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
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "stm32-adc-core.h"

/* Number of linear calibration shadow registers / LINCALRDYW control bits */
#define STM32H7_LINCALFACT_NUM		6

/* BOOST bit must be set on STM32H7 when ADC clock is above 20MHz */
#define STM32H7_BOOST_CLKRATE		20000000UL

#define STM32_ADC_CH_MAX		20	/* max number of channels */
#define STM32_ADC_CH_SZ			16	/* max channel name size */
#define STM32_ADC_MAX_SQ		16	/* SQ1..SQ16 */
#define STM32_ADC_MAX_SMP		7	/* SMPx range is [0..7] */
#define STM32_ADC_TIMEOUT_US		100000
#define STM32_ADC_TIMEOUT	(msecs_to_jiffies(STM32_ADC_TIMEOUT_US / 1000))
#define STM32_ADC_HW_STOP_DELAY_MS	100
#define STM32_ADC_VREFINT_VOLTAGE	3300

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

enum stm32_adc_int_ch {
	STM32_ADC_INT_CH_NONE = -1,
	STM32_ADC_INT_CH_VDDCORE,
	STM32_ADC_INT_CH_VREFINT,
	STM32_ADC_INT_CH_VBAT,
	STM32_ADC_INT_CH_NB,
};

/**
 * struct stm32_adc_ic - ADC internal channels
 * @name:	name of the internal channel
 * @idx:	internal channel enum index
 */
struct stm32_adc_ic {
	const char *name;
	u32 idx;
};

static const struct stm32_adc_ic stm32_adc_ic[STM32_ADC_INT_CH_NB] = {
	{ "vddcore", STM32_ADC_INT_CH_VDDCORE },
	{ "vrefint", STM32_ADC_INT_CH_VREFINT },
	{ "vbat", STM32_ADC_INT_CH_VBAT },
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
 * @calibrated: Indicates calibration status
 */
struct stm32_adc_calib {
	u32			calfact_s;
	u32			calfact_d;
	u32			lincalfact[STM32H7_LINCALFACT_NUM];
	bool			calibrated;
};

/**
 * struct stm32_adc_regs - stm32 ADC misc registers & bitfield desc
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
 * struct stm32_adc_vrefint - stm32 ADC internal reference voltage data
 * @vrefint_cal:	vrefint calibration value from nvmem
 * @vrefint_data:	vrefint actual value
 */
struct stm32_adc_vrefint {
	u32 vrefint_cal;
	u32 vrefint_data;
};

/**
 * struct stm32_adc_regspec - stm32 registers definition
 * @dr:			data register offset
 * @ier_eoc:		interrupt enable register & eocie bitfield
 * @ier_ovr:		interrupt enable register & overrun bitfield
 * @isr_eoc:		interrupt status register & eoc bitfield
 * @isr_ovr:		interrupt status register & overrun bitfield
 * @sqr:		reference to sequence registers array
 * @exten:		trigger control register & bitfield
 * @extsel:		trigger selection register & bitfield
 * @res:		resolution selection register & bitfield
 * @smpr:		smpr1 & smpr2 registers offset array
 * @smp_bits:		smpr1 & smpr2 index and bitfields
 * @or_vdd:		option register & vddcore bitfield
 * @ccr_vbat:		common register & vbat bitfield
 * @ccr_vref:		common register & vrefint bitfield
 */
struct stm32_adc_regspec {
	const u32 dr;
	const struct stm32_adc_regs ier_eoc;
	const struct stm32_adc_regs ier_ovr;
	const struct stm32_adc_regs isr_eoc;
	const struct stm32_adc_regs isr_ovr;
	const struct stm32_adc_regs *sqr;
	const struct stm32_adc_regs exten;
	const struct stm32_adc_regs extsel;
	const struct stm32_adc_regs res;
	const u32 smpr[2];
	const struct stm32_adc_regs *smp_bits;
	const struct stm32_adc_regs or_vdd;
	const struct stm32_adc_regs ccr_vbat;
	const struct stm32_adc_regs ccr_vref;
};

struct stm32_adc;

/**
 * struct stm32_adc_cfg - stm32 compatible configuration data
 * @regs:		registers descriptions
 * @adc_info:		per instance input channels definitions
 * @trigs:		external trigger sources
 * @clk_required:	clock is required
 * @has_vregready:	vregready status flag presence
 * @prepare:		optional prepare routine (power-up, enable)
 * @start_conv:		routine to start conversions
 * @stop_conv:		routine to stop conversions
 * @unprepare:		optional unprepare routine (disable, power-down)
 * @irq_clear:		routine to clear irqs
 * @smp_cycles:		programmable sampling time (ADC clock cycles)
 * @ts_vrefint_ns:	vrefint minimum sampling time in ns
 */
struct stm32_adc_cfg {
	const struct stm32_adc_regspec	*regs;
	const struct stm32_adc_info	*adc_info;
	struct stm32_adc_trig_info	*trigs;
	bool clk_required;
	bool has_vregready;
	int (*prepare)(struct iio_dev *);
	void (*start_conv)(struct iio_dev *, bool dma);
	void (*stop_conv)(struct iio_dev *);
	void (*unprepare)(struct iio_dev *);
	void (*irq_clear)(struct iio_dev *indio_dev, u32 msk);
	const unsigned int *smp_cycles;
	const unsigned int ts_vrefint_ns;
};

/**
 * struct stm32_adc - private data of each ADC IIO instance
 * @common:		reference to ADC block common data
 * @offset:		ADC instance register offset in ADC block
 * @cfg:		compatible configuration data
 * @completion:		end of single conversion completion
 * @buffer:		data buffer + 8 bytes for timestamp if enabled
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
 * @difsel:		bitmask to set single-ended/differential channel
 * @pcsel:		bitmask to preselect channels on some devices
 * @smpr_val:		sampling time settings (e.g. smpr1 / smpr2)
 * @cal:		optional calibration data on some devices
 * @vrefint:		internal reference voltage data
 * @chan_name:		channel name array
 * @num_diff:		number of differential channels
 * @int_ch:		internal channel indexes array
 */
struct stm32_adc {
	struct stm32_adc_common	*common;
	u32			offset;
	const struct stm32_adc_cfg	*cfg;
	struct completion	completion;
	u16			buffer[STM32_ADC_MAX_SQ + 4] __aligned(8);
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
	u32			difsel;
	u32			pcsel;
	u32			smpr_val[2];
	struct stm32_adc_calib	cal;
	struct stm32_adc_vrefint vrefint;
	char			chan_name[STM32_ADC_CH_MAX][STM32_ADC_CH_SZ];
	u32			num_diff;
	int			int_ch[STM32_ADC_INT_CH_NB];
};

struct stm32_adc_diff_channel {
	u32 vinp;
	u32 vinn;
};

/**
 * struct stm32_adc_info - stm32 ADC, per instance config data
 * @max_channels:	Number of channels
 * @resolutions:	available resolutions
 * @num_res:		number of available resolutions
 */
struct stm32_adc_info {
	int max_channels;
	const unsigned int *resolutions;
	const unsigned int num_res;
};

static const unsigned int stm32f4_adc_resolutions[] = {
	/* sorted values so the index matches RES[1:0] in STM32F4_ADC_CR1 */
	12, 10, 8, 6,
};

/* stm32f4 can have up to 16 channels */
static const struct stm32_adc_info stm32f4_adc_info = {
	.max_channels = 16,
	.resolutions = stm32f4_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32f4_adc_resolutions),
};

static const unsigned int stm32h7_adc_resolutions[] = {
	/* sorted values so the index matches RES[2:0] in STM32H7_ADC_CFGR */
	16, 14, 12, 10, 8,
};

/* stm32h7 can have up to 20 channels */
static const struct stm32_adc_info stm32h7_adc_info = {
	.max_channels = STM32_ADC_CH_MAX,
	.resolutions = stm32h7_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32h7_adc_resolutions),
};

/*
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

/*
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
	.ier_ovr = { STM32F4_ADC_CR1, STM32F4_OVRIE },
	.isr_eoc = { STM32F4_ADC_SR, STM32F4_EOC },
	.isr_ovr = { STM32F4_ADC_SR, STM32F4_OVR },
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

/*
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
	.ier_ovr = { STM32H7_ADC_IER, STM32H7_OVRIE },
	.isr_eoc = { STM32H7_ADC_ISR, STM32H7_EOC },
	.isr_ovr = { STM32H7_ADC_ISR, STM32H7_OVR },
	.sqr = stm32h7_sq,
	.exten = { STM32H7_ADC_CFGR, STM32H7_EXTEN_MASK, STM32H7_EXTEN_SHIFT },
	.extsel = { STM32H7_ADC_CFGR, STM32H7_EXTSEL_MASK,
		    STM32H7_EXTSEL_SHIFT },
	.res = { STM32H7_ADC_CFGR, STM32H7_RES_MASK, STM32H7_RES_SHIFT },
	.smpr = { STM32H7_ADC_SMPR1, STM32H7_ADC_SMPR2 },
	.smp_bits = stm32h7_smp_bits,
};

static const struct stm32_adc_regspec stm32mp1_adc_regspec = {
	.dr = STM32H7_ADC_DR,
	.ier_eoc = { STM32H7_ADC_IER, STM32H7_EOCIE },
	.ier_ovr = { STM32H7_ADC_IER, STM32H7_OVRIE },
	.isr_eoc = { STM32H7_ADC_ISR, STM32H7_EOC },
	.isr_ovr = { STM32H7_ADC_ISR, STM32H7_OVR },
	.sqr = stm32h7_sq,
	.exten = { STM32H7_ADC_CFGR, STM32H7_EXTEN_MASK, STM32H7_EXTEN_SHIFT },
	.extsel = { STM32H7_ADC_CFGR, STM32H7_EXTSEL_MASK,
		    STM32H7_EXTSEL_SHIFT },
	.res = { STM32H7_ADC_CFGR, STM32H7_RES_MASK, STM32H7_RES_SHIFT },
	.smpr = { STM32H7_ADC_SMPR1, STM32H7_ADC_SMPR2 },
	.smp_bits = stm32h7_smp_bits,
	.or_vdd = { STM32MP1_ADC2_OR, STM32MP1_VDDCOREEN },
	.ccr_vbat = { STM32H7_ADC_CCR, STM32H7_VBATEN },
	.ccr_vref = { STM32H7_ADC_CCR, STM32H7_VREFEN },
};

/*
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

static void stm32_adc_set_bits_common(struct stm32_adc *adc, u32 reg, u32 bits)
{
	spin_lock(&adc->common->lock);
	writel_relaxed(readl_relaxed(adc->common->base + reg) | bits,
		       adc->common->base + reg);
	spin_unlock(&adc->common->lock);
}

static void stm32_adc_clr_bits(struct stm32_adc *adc, u32 reg, u32 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);
	stm32_adc_writel(adc, reg, stm32_adc_readl(adc, reg) & ~bits);
	spin_unlock_irqrestore(&adc->lock, flags);
}

static void stm32_adc_clr_bits_common(struct stm32_adc *adc, u32 reg, u32 bits)
{
	spin_lock(&adc->common->lock);
	writel_relaxed(readl_relaxed(adc->common->base + reg) & ~bits,
		       adc->common->base + reg);
	spin_unlock(&adc->common->lock);
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

static void stm32_adc_ovr_irq_enable(struct stm32_adc *adc)
{
	stm32_adc_set_bits(adc, adc->cfg->regs->ier_ovr.reg,
			   adc->cfg->regs->ier_ovr.mask);
}

static void stm32_adc_ovr_irq_disable(struct stm32_adc *adc)
{
	stm32_adc_clr_bits(adc, adc->cfg->regs->ier_ovr.reg,
			   adc->cfg->regs->ier_ovr.mask);
}

static void stm32_adc_set_res(struct stm32_adc *adc)
{
	const struct stm32_adc_regs *res = &adc->cfg->regs->res;
	u32 val;

	val = stm32_adc_readl(adc, res->reg);
	val = (val & ~res->mask) | (adc->res << res->shift);
	stm32_adc_writel(adc, res->reg, val);
}

static int stm32_adc_hw_stop(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct stm32_adc *adc = iio_priv(indio_dev);

	if (adc->cfg->unprepare)
		adc->cfg->unprepare(indio_dev);

	clk_disable_unprepare(adc->clk);

	return 0;
}

static int stm32_adc_hw_start(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = clk_prepare_enable(adc->clk);
	if (ret)
		return ret;

	stm32_adc_set_res(adc);

	if (adc->cfg->prepare) {
		ret = adc->cfg->prepare(indio_dev);
		if (ret)
			goto err_clk_dis;
	}

	return 0;

err_clk_dis:
	clk_disable_unprepare(adc->clk);

	return ret;
}

static void stm32_adc_int_ch_enable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	u32 i;

	for (i = 0; i < STM32_ADC_INT_CH_NB; i++) {
		if (adc->int_ch[i] == STM32_ADC_INT_CH_NONE)
			continue;

		switch (i) {
		case STM32_ADC_INT_CH_VDDCORE:
			dev_dbg(&indio_dev->dev, "Enable VDDCore\n");
			stm32_adc_set_bits(adc, adc->cfg->regs->or_vdd.reg,
					   adc->cfg->regs->or_vdd.mask);
			break;
		case STM32_ADC_INT_CH_VREFINT:
			dev_dbg(&indio_dev->dev, "Enable VREFInt\n");
			stm32_adc_set_bits_common(adc, adc->cfg->regs->ccr_vref.reg,
						  adc->cfg->regs->ccr_vref.mask);
			break;
		case STM32_ADC_INT_CH_VBAT:
			dev_dbg(&indio_dev->dev, "Enable VBAT\n");
			stm32_adc_set_bits_common(adc, adc->cfg->regs->ccr_vbat.reg,
						  adc->cfg->regs->ccr_vbat.mask);
			break;
		}
	}
}

static void stm32_adc_int_ch_disable(struct stm32_adc *adc)
{
	u32 i;

	for (i = 0; i < STM32_ADC_INT_CH_NB; i++) {
		if (adc->int_ch[i] == STM32_ADC_INT_CH_NONE)
			continue;

		switch (i) {
		case STM32_ADC_INT_CH_VDDCORE:
			stm32_adc_clr_bits(adc, adc->cfg->regs->or_vdd.reg,
					   adc->cfg->regs->or_vdd.mask);
			break;
		case STM32_ADC_INT_CH_VREFINT:
			stm32_adc_clr_bits_common(adc, adc->cfg->regs->ccr_vref.reg,
						  adc->cfg->regs->ccr_vref.mask);
			break;
		case STM32_ADC_INT_CH_VBAT:
			stm32_adc_clr_bits_common(adc, adc->cfg->regs->ccr_vbat.reg,
						  adc->cfg->regs->ccr_vbat.mask);
			break;
		}
	}
}

/**
 * stm32f4_adc_start_conv() - Start conversions for regular channels.
 * @indio_dev: IIO device instance
 * @dma: use dma to transfer conversion result
 *
 * Start conversions for regular channels.
 * Also take care of normal or DMA mode. Circular DMA may be used for regular
 * conversions, in IIO buffer modes. Otherwise, use ADC interrupt with direct
 * DR read instead (e.g. read_raw, or triggered buffer mode without DMA).
 */
static void stm32f4_adc_start_conv(struct iio_dev *indio_dev, bool dma)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

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

static void stm32f4_adc_stop_conv(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	stm32_adc_clr_bits(adc, STM32F4_ADC_CR2, STM32F4_EXTEN_MASK);
	stm32_adc_clr_bits(adc, STM32F4_ADC_SR, STM32F4_STRT);

	stm32_adc_clr_bits(adc, STM32F4_ADC_CR1, STM32F4_SCAN);
	stm32_adc_clr_bits(adc, STM32F4_ADC_CR2,
			   STM32F4_ADON | STM32F4_DMA | STM32F4_DDS);
}

static void stm32f4_adc_irq_clear(struct iio_dev *indio_dev, u32 msk)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	stm32_adc_clr_bits(adc, adc->cfg->regs->isr_eoc.reg, msk);
}

static void stm32h7_adc_start_conv(struct iio_dev *indio_dev, bool dma)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
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

static void stm32h7_adc_stop_conv(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
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

static void stm32h7_adc_irq_clear(struct iio_dev *indio_dev, u32 msk)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	/* On STM32H7 IRQs are cleared by writing 1 into ISR register */
	stm32_adc_set_bits(adc, adc->cfg->regs->isr_eoc.reg, msk);
}

static int stm32h7_adc_exit_pwr_down(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;
	u32 val;

	/* Exit deep power down, then enable ADC voltage regulator */
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADVREGEN);

	if (adc->common->rate > STM32H7_BOOST_CLKRATE)
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Wait for startup time */
	if (!adc->cfg->has_vregready) {
		usleep_range(10, 20);
		return 0;
	}

	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_ISR, val,
					   val & STM32MP1_VREGREADY, 100,
					   STM32_ADC_TIMEOUT_US);
	if (ret) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
		dev_err(&indio_dev->dev, "Failed to exit power down\n");
	}

	return ret;
}

static void stm32h7_adc_enter_pwr_down(struct stm32_adc *adc)
{
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Setting DEEPPWD disables ADC vreg and clears ADVREGEN */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
}

static int stm32h7_adc_enable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;
	u32 val;

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADEN);

	/* Poll for ADRDY to be set (after adc startup time) */
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_ISR, val,
					   val & STM32H7_ADRDY,
					   100, STM32_ADC_TIMEOUT_US);
	if (ret) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADDIS);
		dev_err(&indio_dev->dev, "Failed to enable ADC\n");
	} else {
		/* Clear ADRDY by writing one */
		stm32_adc_set_bits(adc, STM32H7_ADC_ISR, STM32H7_ADRDY);
	}

	return ret;
}

static void stm32h7_adc_disable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
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
 * @indio_dev: IIO device instance
 * Note: Must be called once ADC is enabled, so LINCALRDYW[1..6] are writable
 */
static int stm32h7_adc_read_selfcalib(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int i, ret;
	u32 lincalrdyw_mask, val;

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
			return ret;
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
	adc->cal.calibrated = true;

	return 0;
}

/**
 * stm32h7_adc_restore_selfcalib() - Restore saved self-calibration result
 * @indio_dev: IIO device instance
 * Note: ADC must be enabled, with no on-going conversions.
 */
static int stm32h7_adc_restore_selfcalib(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
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

/*
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
 * stm32h7_adc_selfcalib() - Procedure to calibrate ADC
 * @indio_dev: IIO device instance
 * Note: Must be called once ADC is out of power down.
 */
static int stm32h7_adc_selfcalib(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;
	u32 val;

	if (adc->cal.calibrated)
		return true;

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
		goto out;
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
		goto out;
	}

out:
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR,
			   STM32H7_ADCALDIF | STM32H7_ADCALLIN);

	return ret;
}

/**
 * stm32h7_adc_prepare() - Leave power down mode to enable ADC.
 * @indio_dev: IIO device instance
 * Leave power down mode.
 * Configure channels as single ended or differential before enabling ADC.
 * Enable ADC.
 * Restore calibration data.
 * Pre-select channels that may be used in PCSEL (required by input MUX / IO):
 * - Only one input is selected for single ended (e.g. 'vinp')
 * - Two inputs are selected for differential channels (e.g. 'vinp' & 'vinn')
 */
static int stm32h7_adc_prepare(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int calib, ret;

	ret = stm32h7_adc_exit_pwr_down(indio_dev);
	if (ret)
		return ret;

	ret = stm32h7_adc_selfcalib(indio_dev);
	if (ret < 0)
		goto pwr_dwn;
	calib = ret;

	stm32_adc_int_ch_enable(indio_dev);

	stm32_adc_writel(adc, STM32H7_ADC_DIFSEL, adc->difsel);

	ret = stm32h7_adc_enable(indio_dev);
	if (ret)
		goto ch_disable;

	/* Either restore or read calibration result for future reference */
	if (calib)
		ret = stm32h7_adc_restore_selfcalib(indio_dev);
	else
		ret = stm32h7_adc_read_selfcalib(indio_dev);
	if (ret)
		goto disable;

	stm32_adc_writel(adc, STM32H7_ADC_PCSEL, adc->pcsel);

	return 0;

disable:
	stm32h7_adc_disable(indio_dev);
ch_disable:
	stm32_adc_int_ch_disable(adc);
pwr_dwn:
	stm32h7_adc_enter_pwr_down(adc);

	return ret;
}

static void stm32h7_adc_unprepare(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	stm32_adc_writel(adc, STM32H7_ADC_PCSEL, 0);
	stm32h7_adc_disable(indio_dev);
	stm32_adc_int_ch_disable(adc);
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
 * @indio_dev: IIO device structure
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
	struct device *dev = indio_dev->dev.parent;
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	long timeout;
	u32 val;
	int ret;

	reinit_completion(&adc->completion);

	adc->bufi = 0;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

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

	adc->cfg->start_conv(indio_dev, false);

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

	adc->cfg->stop_conv(indio_dev);

	stm32_adc_conv_irq_disable(adc);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

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
	case IIO_CHAN_INFO_PROCESSED:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		if (chan->type == IIO_VOLTAGE)
			ret = stm32_adc_single_conv(indio_dev, chan, val);
		else
			ret = -EINVAL;

		if (mask == IIO_CHAN_INFO_PROCESSED && adc->vrefint.vrefint_cal)
			*val = STM32_ADC_VREFINT_VOLTAGE * adc->vrefint.vrefint_cal / *val;

		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		if (chan->differential) {
			*val = adc->common->vref_mv * 2;
			*val2 = chan->scan_type.realbits;
		} else {
			*val = adc->common->vref_mv;
			*val2 = chan->scan_type.realbits;
		}
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->differential)
			/* ADC_full_scale / 2 */
			*val = -((1 << chan->scan_type.realbits) / 2);
		else
			*val = 0;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static void stm32_adc_irq_clear(struct iio_dev *indio_dev, u32 msk)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	adc->cfg->irq_clear(indio_dev, msk);
}

static irqreturn_t stm32_adc_threaded_isr(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	u32 status = stm32_adc_readl(adc, regs->isr_eoc.reg);
	u32 mask = stm32_adc_readl(adc, regs->ier_eoc.reg);

	/* Check ovr status right now, as ovr mask should be already disabled */
	if (status & regs->isr_ovr.mask) {
		/*
		 * Clear ovr bit to avoid subsequent calls to IRQ handler.
		 * This requires to stop ADC first. OVR bit state in ISR,
		 * is propaged to CSR register by hardware.
		 */
		adc->cfg->stop_conv(indio_dev);
		stm32_adc_irq_clear(indio_dev, regs->isr_ovr.mask);
		dev_err(&indio_dev->dev, "Overrun, stopping: restart needed\n");
		return IRQ_HANDLED;
	}

	if (!(status & mask))
		dev_err_ratelimited(&indio_dev->dev,
				    "Unexpected IRQ: IER=0x%08x, ISR=0x%08x\n",
				    mask, status);

	return IRQ_NONE;
}

static irqreturn_t stm32_adc_isr(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	u32 status = stm32_adc_readl(adc, regs->isr_eoc.reg);
	u32 mask = stm32_adc_readl(adc, regs->ier_eoc.reg);

	if (!(status & mask))
		return IRQ_WAKE_THREAD;

	if (status & regs->isr_ovr.mask) {
		/*
		 * Overrun occurred on regular conversions: data for wrong
		 * channel may be read. Unconditionally disable interrupts
		 * to stop processing data and print error message.
		 * Restarting the capture can be done by disabling, then
		 * re-enabling it (e.g. write 0, then 1 to buffer/enable).
		 */
		stm32_adc_ovr_irq_disable(adc);
		stm32_adc_conv_irq_disable(adc);
		return IRQ_WAKE_THREAD;
	}

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
	unsigned int rx_buf_sz = STM32_DMA_BUFFER_SIZE;

	/*
	 * dma cyclic transfers are used, buffer is split into two periods.
	 * There should be :
	 * - always one buffer (period) dma is working on
	 * - one buffer (period) driver can push data.
	 */
	watermark = min(watermark, val * (unsigned)(sizeof(u16)));
	adc->rx_buf_sz = min(rx_buf_sz, watermark * 2 * adc->num_conv);

	return 0;
}

static int stm32_adc_update_scan_mode(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	adc->num_conv = bitmap_weight(scan_mask, indio_dev->masklength);

	ret = stm32_adc_conf_scan_seq(indio_dev, scan_mask);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
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
 * @indio_dev: IIO device structure
 * @reg: register offset
 * @writeval: value to write
 * @readval: value to read
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
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (!readval)
		stm32_adc_writel(adc, reg, writeval);
	else
		*readval = stm32_adc_readl(adc, reg);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

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
	struct stm32_adc *adc = iio_priv(indio_dev);
	int residue = stm32_adc_dma_residue(adc);

	/*
	 * In DMA mode the trigger services of IIO are not used
	 * (e.g. no call to iio_trigger_poll).
	 * Calling irq handler associated to the hardware trigger is not
	 * relevant as the conversions have already been done. Data
	 * transfers are performed directly in DMA callback instead.
	 * This implementation avoids to call trigger irq handler that
	 * may sleep, in an atomic context (DMA irq handler context).
	 */
	dev_dbg(&indio_dev->dev, "%s bufi=%d\n", __func__, adc->bufi);

	while (residue >= indio_dev->scan_bytes) {
		u16 *buffer = (u16 *)&adc->rx_buf[adc->bufi];

		iio_push_to_buffers(indio_dev, buffer);

		residue -= indio_dev->scan_bytes;
		adc->bufi += indio_dev->scan_bytes;
		if (adc->bufi >= adc->rx_buf_sz)
			adc->bufi = 0;
	}
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
		dmaengine_terminate_sync(adc->dma_chan);
		return ret;
	}

	/* Issue pending DMA requests */
	dma_async_issue_pending(adc->dma_chan);

	return 0;
}

static int stm32_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = stm32_adc_set_trig(indio_dev, indio_dev->trig);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't set trigger\n");
		goto err_pm_put;
	}

	ret = stm32_adc_dma_start(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't start dma\n");
		goto err_clr_trig;
	}

	/* Reset adc buffer index */
	adc->bufi = 0;

	stm32_adc_ovr_irq_enable(adc);

	if (!adc->dma_chan)
		stm32_adc_conv_irq_enable(adc);

	adc->cfg->start_conv(indio_dev, !!adc->dma_chan);

	return 0;

err_clr_trig:
	stm32_adc_set_trig(indio_dev, NULL);
err_pm_put:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int stm32_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;

	adc->cfg->stop_conv(indio_dev);
	if (!adc->dma_chan)
		stm32_adc_conv_irq_disable(adc);

	stm32_adc_ovr_irq_disable(adc);

	if (adc->dma_chan)
		dmaengine_terminate_sync(adc->dma_chan);

	if (stm32_adc_set_trig(indio_dev, NULL))
		dev_err(&indio_dev->dev, "Can't clear trigger\n");

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
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

	/* reset buffer index */
	adc->bufi = 0;
	iio_push_to_buffers_with_timestamp(indio_dev, adc->buffer,
					   pf->timestamp);
	iio_trigger_notify_done(indio_dev->trig);

	/* re-enable eoc irq */
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

	/*
	 * For vrefint channel, ensure that the sampling time cannot
	 * be lower than the one specified in the datasheet
	 */
	if (channel == adc->int_ch[STM32_ADC_INT_CH_VREFINT])
		smp_ns = max(smp_ns, adc->cfg->ts_vrefint_ns);

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
				    struct iio_chan_spec *chan, u32 vinp,
				    u32 vinn, int scan_index, bool differential)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	char *name = adc->chan_name[vinp];

	chan->type = IIO_VOLTAGE;
	chan->channel = vinp;
	if (differential) {
		chan->differential = 1;
		chan->channel2 = vinn;
		snprintf(name, STM32_ADC_CH_SZ, "in%d-in%d", vinp, vinn);
	} else {
		snprintf(name, STM32_ADC_CH_SZ, "in%d", vinp);
	}
	chan->datasheet_name = name;
	chan->scan_index = scan_index;
	chan->indexed = 1;
	if (chan->channel == adc->int_ch[STM32_ADC_INT_CH_VREFINT])
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED);
	else
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					 BIT(IIO_CHAN_INFO_OFFSET);
	chan->scan_type.sign = 'u';
	chan->scan_type.realbits = adc->cfg->adc_info->resolutions[adc->res];
	chan->scan_type.storagebits = 16;
	chan->ext_info = stm32_adc_ext_info;

	/* pre-build selected channels mask */
	adc->pcsel |= BIT(chan->channel);
	if (differential) {
		/* pre-build diff channels mask */
		adc->difsel |= BIT(chan->channel);
		/* Also add negative input to pre-selected channels */
		adc->pcsel |= BIT(chan->channel2);
	}
}

static int stm32_adc_get_legacy_chan_count(struct iio_dev *indio_dev, struct stm32_adc *adc)
{
	struct device_node *node = indio_dev->dev.of_node;
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	int num_channels = 0, ret;

	ret = of_property_count_u32_elems(node, "st,adc-channels");
	if (ret > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Bad st,adc-channels?\n");
		return -EINVAL;
	} else if (ret > 0) {
		num_channels += ret;
	}

	ret = of_property_count_elems_of_size(node, "st,adc-diff-channels",
					      sizeof(struct stm32_adc_diff_channel));
	if (ret > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Bad st,adc-diff-channels?\n");
		return -EINVAL;
	} else if (ret > 0) {
		adc->num_diff = ret;
		num_channels += ret;
	}

	/* Optional sample time is provided either for each, or all channels */
	ret = of_property_count_u32_elems(node, "st,min-sample-time-nsecs");
	if (ret > 1 && ret != num_channels) {
		dev_err(&indio_dev->dev, "Invalid st,min-sample-time-nsecs\n");
		return -EINVAL;
	}

	return num_channels;
}

static int stm32_adc_legacy_chan_init(struct iio_dev *indio_dev,
				      struct stm32_adc *adc,
				      struct iio_chan_spec *channels)
{
	struct device_node *node = indio_dev->dev.of_node;
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	struct stm32_adc_diff_channel diff[STM32_ADC_CH_MAX];
	u32 num_diff = adc->num_diff;
	int size = num_diff * sizeof(*diff) / sizeof(u32);
	int scan_index = 0, val, ret, i;
	struct property *prop;
	const __be32 *cur;
	u32 smp = 0;

	if (num_diff) {
		ret = of_property_read_u32_array(node, "st,adc-diff-channels",
						 (u32 *)diff, size);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to get diff channels %d\n", ret);
			return ret;
		}

		for (i = 0; i < num_diff; i++) {
			if (diff[i].vinp >= adc_info->max_channels ||
			    diff[i].vinn >= adc_info->max_channels) {
				dev_err(&indio_dev->dev, "Invalid channel in%d-in%d\n",
					diff[i].vinp, diff[i].vinn);
				return -EINVAL;
			}

			stm32_adc_chan_init_one(indio_dev, &channels[scan_index],
						diff[i].vinp, diff[i].vinn,
						scan_index, true);
			scan_index++;
		}
	}

	of_property_for_each_u32(node, "st,adc-channels", prop, cur, val) {
		if (val >= adc_info->max_channels) {
			dev_err(&indio_dev->dev, "Invalid channel %d\n", val);
			return -EINVAL;
		}

		/* Channel can't be configured both as single-ended & diff */
		for (i = 0; i < num_diff; i++) {
			if (val == diff[i].vinp) {
				dev_err(&indio_dev->dev, "channel %d misconfigured\n",	val);
				return -EINVAL;
			}
		}
		stm32_adc_chan_init_one(indio_dev, &channels[scan_index], val,
					0, scan_index, false);
		scan_index++;
	}

	for (i = 0; i < scan_index; i++) {
		/*
		 * Using of_property_read_u32_index(), smp value will only be
		 * modified if valid u32 value can be decoded. This allows to
		 * get either no value, 1 shared value for all indexes, or one
		 * value per channel.
		 */
		of_property_read_u32_index(node, "st,min-sample-time-nsecs", i, &smp);

		/* Prepare sampling time settings */
		stm32_adc_smpr_init(adc, channels[i].channel, smp);
	}

	return scan_index;
}

static int stm32_adc_populate_int_ch(struct iio_dev *indio_dev, const char *ch_name,
				     int chan)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	u16 vrefint;
	int i, ret;

	for (i = 0; i < STM32_ADC_INT_CH_NB; i++) {
		if (!strncmp(stm32_adc_ic[i].name, ch_name, STM32_ADC_CH_SZ)) {
			adc->int_ch[i] = chan;

			if (stm32_adc_ic[i].idx != STM32_ADC_INT_CH_VREFINT)
				continue;

			/* Get calibration data for vrefint channel */
			ret = nvmem_cell_read_u16(&indio_dev->dev, "vrefint", &vrefint);
			if (ret && ret != -ENOENT) {
				return dev_err_probe(indio_dev->dev.parent, ret,
						     "nvmem access error\n");
			}
			if (ret == -ENOENT)
				dev_dbg(&indio_dev->dev, "vrefint calibration not found\n");
			else
				adc->vrefint.vrefint_cal = vrefint;
		}
	}

	return 0;
}

static int stm32_adc_generic_chan_init(struct iio_dev *indio_dev,
				       struct stm32_adc *adc,
				       struct iio_chan_spec *channels)
{
	struct device_node *node = indio_dev->dev.of_node;
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	struct device_node *child;
	const char *name;
	int val, scan_index = 0, ret;
	bool differential;
	u32 vin[2];

	for_each_available_child_of_node(node, child) {
		ret = of_property_read_u32(child, "reg", &val);
		if (ret) {
			dev_err(&indio_dev->dev, "Missing channel index %d\n", ret);
			goto err;
		}

		ret = of_property_read_string(child, "label", &name);
		/* label is optional */
		if (!ret) {
			if (strlen(name) >= STM32_ADC_CH_SZ) {
				dev_err(&indio_dev->dev, "Label %s exceeds %d characters\n",
					name, STM32_ADC_CH_SZ);
				ret = -EINVAL;
				goto err;
			}
			strncpy(adc->chan_name[val], name, STM32_ADC_CH_SZ);
			ret = stm32_adc_populate_int_ch(indio_dev, name, val);
			if (ret)
				goto err;
		} else if (ret != -EINVAL) {
			dev_err(&indio_dev->dev, "Invalid label %d\n", ret);
			goto err;
		}

		if (val >= adc_info->max_channels) {
			dev_err(&indio_dev->dev, "Invalid channel %d\n", val);
			ret = -EINVAL;
			goto err;
		}

		differential = false;
		ret = of_property_read_u32_array(child, "diff-channels", vin, 2);
		/* diff-channels is optional */
		if (!ret) {
			differential = true;
			if (vin[0] != val || vin[1] >= adc_info->max_channels) {
				dev_err(&indio_dev->dev, "Invalid channel in%d-in%d\n",
					vin[0], vin[1]);
				goto err;
			}
		} else if (ret != -EINVAL) {
			dev_err(&indio_dev->dev, "Invalid diff-channels property %d\n", ret);
			goto err;
		}

		stm32_adc_chan_init_one(indio_dev, &channels[scan_index], val,
					vin[1], scan_index, differential);

		ret = of_property_read_u32(child, "st,min-sample-time-ns", &val);
		/* st,min-sample-time-ns is optional */
		if (!ret) {
			stm32_adc_smpr_init(adc, channels[scan_index].channel, val);
			if (differential)
				stm32_adc_smpr_init(adc, vin[1], val);
		} else if (ret != -EINVAL) {
			dev_err(&indio_dev->dev, "Invalid st,min-sample-time-ns property %d\n",
				ret);
			goto err;
		}

		scan_index++;
	}

	return scan_index;

err:
	of_node_put(child);

	return ret;
}

static int stm32_adc_chan_of_init(struct iio_dev *indio_dev, bool timestamping)
{
	struct device_node *node = indio_dev->dev.of_node;
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	struct iio_chan_spec *channels;
	int scan_index = 0, num_channels = 0, ret, i;
	bool legacy = false;

	for (i = 0; i < STM32_ADC_INT_CH_NB; i++)
		adc->int_ch[i] = STM32_ADC_INT_CH_NONE;

	num_channels = of_get_available_child_count(node);
	/* If no channels have been found, fallback to channels legacy properties. */
	if (!num_channels) {
		legacy = true;

		ret = stm32_adc_get_legacy_chan_count(indio_dev, adc);
		if (!ret) {
			dev_err(indio_dev->dev.parent, "No channel found\n");
			return -ENODATA;
		} else if (ret < 0) {
			return ret;
		}

		num_channels = ret;
	}

	if (num_channels > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Channel number [%d] exceeds %d\n",
			num_channels, adc_info->max_channels);
		return -EINVAL;
	}

	if (timestamping)
		num_channels++;

	channels = devm_kcalloc(&indio_dev->dev, num_channels,
				sizeof(struct iio_chan_spec), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	if (legacy)
		ret = stm32_adc_legacy_chan_init(indio_dev, adc, channels);
	else
		ret = stm32_adc_generic_chan_init(indio_dev, adc, channels);
	if (ret < 0)
		return ret;
	scan_index = ret;

	if (timestamping) {
		struct iio_chan_spec *timestamp = &channels[scan_index];

		timestamp->type = IIO_TIMESTAMP;
		timestamp->channel = -1;
		timestamp->scan_index = scan_index;
		timestamp->scan_type.sign = 's';
		timestamp->scan_type.realbits = 64;
		timestamp->scan_type.storagebits = 64;

		scan_index++;
	}

	indio_dev->num_channels = scan_index;
	indio_dev->channels = channels;

	return 0;
}

static int stm32_adc_dma_request(struct device *dev, struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct dma_slave_config config;
	int ret;

	adc->dma_chan = dma_request_chan(dev, "rx");
	if (IS_ERR(adc->dma_chan)) {
		ret = PTR_ERR(adc->dma_chan);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret,
					     "DMA channel request failed with\n");

		/* DMA is optional: fall back to IRQ mode */
		adc->dma_chan = NULL;
		return 0;
	}

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
	irqreturn_t (*handler)(int irq, void *p) = NULL;
	struct stm32_adc *adc;
	bool timestamping = false;
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
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &stm32_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_HARDWARE_TRIGGERED;

	platform_set_drvdata(pdev, indio_dev);

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &adc->offset);
	if (ret != 0) {
		dev_err(&pdev->dev, "missing reg property\n");
		return -EINVAL;
	}

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0)
		return adc->irq;

	ret = devm_request_threaded_irq(&pdev->dev, adc->irq, stm32_adc_isr,
					stm32_adc_threaded_isr,
					0, pdev->name, indio_dev);
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

	ret = stm32_adc_of_get_resolution(indio_dev);
	if (ret < 0)
		return ret;

	ret = stm32_adc_dma_request(dev, indio_dev);
	if (ret < 0)
		return ret;

	if (!adc->dma_chan) {
		/* For PIO mode only, iio_pollfunc_store_time stores a timestamp
		 * in the primary trigger IRQ handler and stm32_adc_trigger_handler
		 * runs in the IRQ thread to push out buffer along with timestamp.
		 */
		handler = &stm32_adc_trigger_handler;
		timestamping = true;
	}

	ret = stm32_adc_chan_of_init(indio_dev, timestamping);
	if (ret < 0)
		goto err_dma_disable;

	ret = iio_triggered_buffer_setup(indio_dev,
					 &iio_pollfunc_store_time, handler,
					 &stm32_adc_buffer_setup_ops);
	if (ret) {
		dev_err(&pdev->dev, "buffer setup failed\n");
		goto err_dma_disable;
	}

	/* Get stm32-adc-core PM online */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, STM32_ADC_HW_STOP_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = stm32_adc_hw_start(dev);
	if (ret)
		goto err_buffer_cleanup;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "iio dev register failed\n");
		goto err_hw_stop;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_hw_stop:
	stm32_adc_hw_stop(dev);

err_buffer_cleanup:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	iio_triggered_buffer_cleanup(indio_dev);

err_dma_disable:
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}

	return ret;
}

static int stm32_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct stm32_adc *adc = iio_priv(indio_dev);

	pm_runtime_get_sync(&pdev->dev);
	iio_device_unregister(indio_dev);
	stm32_adc_hw_stop(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}

	return 0;
}

#if defined(CONFIG_PM_SLEEP)
static int stm32_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	if (iio_buffer_enabled(indio_dev))
		stm32_adc_buffer_predisable(indio_dev);

	return pm_runtime_force_suspend(dev);
}

static int stm32_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	if (!iio_buffer_enabled(indio_dev))
		return 0;

	ret = stm32_adc_update_scan_mode(indio_dev,
					 indio_dev->active_scan_mask);
	if (ret < 0)
		return ret;

	return stm32_adc_buffer_postenable(indio_dev);
}
#endif

#if defined(CONFIG_PM)
static int stm32_adc_runtime_suspend(struct device *dev)
{
	return stm32_adc_hw_stop(dev);
}

static int stm32_adc_runtime_resume(struct device *dev)
{
	return stm32_adc_hw_start(dev);
}
#endif

static const struct dev_pm_ops stm32_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_adc_suspend, stm32_adc_resume)
	SET_RUNTIME_PM_OPS(stm32_adc_runtime_suspend, stm32_adc_runtime_resume,
			   NULL)
};

static const struct stm32_adc_cfg stm32f4_adc_cfg = {
	.regs = &stm32f4_adc_regspec,
	.adc_info = &stm32f4_adc_info,
	.trigs = stm32f4_adc_trigs,
	.clk_required = true,
	.start_conv = stm32f4_adc_start_conv,
	.stop_conv = stm32f4_adc_stop_conv,
	.smp_cycles = stm32f4_adc_smp_cycles,
	.irq_clear = stm32f4_adc_irq_clear,
};

static const struct stm32_adc_cfg stm32h7_adc_cfg = {
	.regs = &stm32h7_adc_regspec,
	.adc_info = &stm32h7_adc_info,
	.trigs = stm32h7_adc_trigs,
	.start_conv = stm32h7_adc_start_conv,
	.stop_conv = stm32h7_adc_stop_conv,
	.prepare = stm32h7_adc_prepare,
	.unprepare = stm32h7_adc_unprepare,
	.smp_cycles = stm32h7_adc_smp_cycles,
	.irq_clear = stm32h7_adc_irq_clear,
};

static const struct stm32_adc_cfg stm32mp1_adc_cfg = {
	.regs = &stm32mp1_adc_regspec,
	.adc_info = &stm32h7_adc_info,
	.trigs = stm32h7_adc_trigs,
	.has_vregready = true,
	.start_conv = stm32h7_adc_start_conv,
	.stop_conv = stm32h7_adc_stop_conv,
	.prepare = stm32h7_adc_prepare,
	.unprepare = stm32h7_adc_unprepare,
	.smp_cycles = stm32h7_adc_smp_cycles,
	.irq_clear = stm32h7_adc_irq_clear,
	.ts_vrefint_ns = 4300,
};

static const struct of_device_id stm32_adc_of_match[] = {
	{ .compatible = "st,stm32f4-adc", .data = (void *)&stm32f4_adc_cfg },
	{ .compatible = "st,stm32h7-adc", .data = (void *)&stm32h7_adc_cfg },
	{ .compatible = "st,stm32mp1-adc", .data = (void *)&stm32mp1_adc_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_adc_of_match);

static struct platform_driver stm32_adc_driver = {
	.probe = stm32_adc_probe,
	.remove = stm32_adc_remove,
	.driver = {
		.name = "stm32-adc",
		.of_match_table = stm32_adc_of_match,
		.pm = &stm32_adc_pm_ops,
	},
};
module_platform_driver(stm32_adc_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 ADC IIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-adc");
