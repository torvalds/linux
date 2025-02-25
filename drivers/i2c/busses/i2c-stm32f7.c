// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STMicroelectronics STM32F7 I2C controller
 *
 * This I2C controller is described in the STM32F75xxx and STM32F74xxx Soc
 * reference manual.
 * Please see below a link to the documentation:
 * http://www.st.com/resource/en/reference_manual/dm00124865.pdf
 *
 * Copyright (C) M'boumba Cedric Madianga 2017
 * Copyright (C) STMicroelectronics 2017
 * Author: M'boumba Cedric Madianga <cedric.madianga@gmail.com>
 *
 * This driver is based on i2c-stm32f4.c
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "i2c-stm32.h"

/* STM32F7 I2C registers */
#define STM32F7_I2C_CR1				0x00
#define STM32F7_I2C_CR2				0x04
#define STM32F7_I2C_OAR1			0x08
#define STM32F7_I2C_OAR2			0x0C
#define STM32F7_I2C_PECR			0x20
#define STM32F7_I2C_TIMINGR			0x10
#define STM32F7_I2C_ISR				0x18
#define STM32F7_I2C_ICR				0x1C
#define STM32F7_I2C_RXDR			0x24
#define STM32F7_I2C_TXDR			0x28

/* STM32F7 I2C control 1 */
#define STM32F7_I2C_CR1_PECEN			BIT(23)
#define STM32F7_I2C_CR1_ALERTEN			BIT(22)
#define STM32F7_I2C_CR1_SMBHEN			BIT(20)
#define STM32F7_I2C_CR1_WUPEN			BIT(18)
#define STM32F7_I2C_CR1_SBC			BIT(16)
#define STM32F7_I2C_CR1_RXDMAEN			BIT(15)
#define STM32F7_I2C_CR1_TXDMAEN			BIT(14)
#define STM32F7_I2C_CR1_ANFOFF			BIT(12)
#define STM32F7_I2C_CR1_DNF_MASK		GENMASK(11, 8)
#define STM32F7_I2C_CR1_DNF(n)			(((n) & 0xf) << 8)
#define STM32F7_I2C_CR1_ERRIE			BIT(7)
#define STM32F7_I2C_CR1_TCIE			BIT(6)
#define STM32F7_I2C_CR1_STOPIE			BIT(5)
#define STM32F7_I2C_CR1_NACKIE			BIT(4)
#define STM32F7_I2C_CR1_ADDRIE			BIT(3)
#define STM32F7_I2C_CR1_RXIE			BIT(2)
#define STM32F7_I2C_CR1_TXIE			BIT(1)
#define STM32F7_I2C_CR1_PE			BIT(0)
#define STM32F7_I2C_ALL_IRQ_MASK		(STM32F7_I2C_CR1_ERRIE \
						| STM32F7_I2C_CR1_TCIE \
						| STM32F7_I2C_CR1_STOPIE \
						| STM32F7_I2C_CR1_NACKIE \
						| STM32F7_I2C_CR1_RXIE \
						| STM32F7_I2C_CR1_TXIE)
#define STM32F7_I2C_XFER_IRQ_MASK		(STM32F7_I2C_CR1_TCIE \
						| STM32F7_I2C_CR1_STOPIE \
						| STM32F7_I2C_CR1_NACKIE \
						| STM32F7_I2C_CR1_RXIE \
						| STM32F7_I2C_CR1_TXIE)

/* STM32F7 I2C control 2 */
#define STM32F7_I2C_CR2_PECBYTE			BIT(26)
#define STM32F7_I2C_CR2_RELOAD			BIT(24)
#define STM32F7_I2C_CR2_NBYTES_MASK		GENMASK(23, 16)
#define STM32F7_I2C_CR2_NBYTES(n)		(((n) & 0xff) << 16)
#define STM32F7_I2C_CR2_NACK			BIT(15)
#define STM32F7_I2C_CR2_STOP			BIT(14)
#define STM32F7_I2C_CR2_START			BIT(13)
#define STM32F7_I2C_CR2_HEAD10R			BIT(12)
#define STM32F7_I2C_CR2_ADD10			BIT(11)
#define STM32F7_I2C_CR2_RD_WRN			BIT(10)
#define STM32F7_I2C_CR2_SADD10_MASK		GENMASK(9, 0)
#define STM32F7_I2C_CR2_SADD10(n)		(((n) & \
						STM32F7_I2C_CR2_SADD10_MASK))
#define STM32F7_I2C_CR2_SADD7_MASK		GENMASK(7, 1)
#define STM32F7_I2C_CR2_SADD7(n)		(((n) & 0x7f) << 1)

/* STM32F7 I2C Own Address 1 */
#define STM32F7_I2C_OAR1_OA1EN			BIT(15)
#define STM32F7_I2C_OAR1_OA1MODE		BIT(10)
#define STM32F7_I2C_OAR1_OA1_10_MASK		GENMASK(9, 0)
#define STM32F7_I2C_OAR1_OA1_10(n)		(((n) & \
						STM32F7_I2C_OAR1_OA1_10_MASK))
#define STM32F7_I2C_OAR1_OA1_7_MASK		GENMASK(7, 1)
#define STM32F7_I2C_OAR1_OA1_7(n)		(((n) & 0x7f) << 1)
#define STM32F7_I2C_OAR1_MASK			(STM32F7_I2C_OAR1_OA1_7_MASK \
						| STM32F7_I2C_OAR1_OA1_10_MASK \
						| STM32F7_I2C_OAR1_OA1EN \
						| STM32F7_I2C_OAR1_OA1MODE)

/* STM32F7 I2C Own Address 2 */
#define STM32F7_I2C_OAR2_OA2EN			BIT(15)
#define STM32F7_I2C_OAR2_OA2MSK_MASK		GENMASK(10, 8)
#define STM32F7_I2C_OAR2_OA2MSK(n)		(((n) & 0x7) << 8)
#define STM32F7_I2C_OAR2_OA2_7_MASK		GENMASK(7, 1)
#define STM32F7_I2C_OAR2_OA2_7(n)		(((n) & 0x7f) << 1)
#define STM32F7_I2C_OAR2_MASK			(STM32F7_I2C_OAR2_OA2MSK_MASK \
						| STM32F7_I2C_OAR2_OA2_7_MASK \
						| STM32F7_I2C_OAR2_OA2EN)

/* STM32F7 I2C Interrupt Status */
#define STM32F7_I2C_ISR_ADDCODE_MASK		GENMASK(23, 17)
#define STM32F7_I2C_ISR_ADDCODE_GET(n) \
				(((n) & STM32F7_I2C_ISR_ADDCODE_MASK) >> 17)
#define STM32F7_I2C_ISR_DIR			BIT(16)
#define STM32F7_I2C_ISR_BUSY			BIT(15)
#define STM32F7_I2C_ISR_ALERT			BIT(13)
#define STM32F7_I2C_ISR_PECERR			BIT(11)
#define STM32F7_I2C_ISR_ARLO			BIT(9)
#define STM32F7_I2C_ISR_BERR			BIT(8)
#define STM32F7_I2C_ISR_TCR			BIT(7)
#define STM32F7_I2C_ISR_TC			BIT(6)
#define STM32F7_I2C_ISR_STOPF			BIT(5)
#define STM32F7_I2C_ISR_NACKF			BIT(4)
#define STM32F7_I2C_ISR_ADDR			BIT(3)
#define STM32F7_I2C_ISR_RXNE			BIT(2)
#define STM32F7_I2C_ISR_TXIS			BIT(1)
#define STM32F7_I2C_ISR_TXE			BIT(0)

/* STM32F7 I2C Interrupt Clear */
#define STM32F7_I2C_ICR_ALERTCF			BIT(13)
#define STM32F7_I2C_ICR_PECCF			BIT(11)
#define STM32F7_I2C_ICR_ARLOCF			BIT(9)
#define STM32F7_I2C_ICR_BERRCF			BIT(8)
#define STM32F7_I2C_ICR_STOPCF			BIT(5)
#define STM32F7_I2C_ICR_NACKCF			BIT(4)
#define STM32F7_I2C_ICR_ADDRCF			BIT(3)

/* STM32F7 I2C Timing */
#define STM32F7_I2C_TIMINGR_PRESC(n)		(((n) & 0xf) << 28)
#define STM32F7_I2C_TIMINGR_SCLDEL(n)		(((n) & 0xf) << 20)
#define STM32F7_I2C_TIMINGR_SDADEL(n)		(((n) & 0xf) << 16)
#define STM32F7_I2C_TIMINGR_SCLH(n)		(((n) & 0xff) << 8)
#define STM32F7_I2C_TIMINGR_SCLL(n)		((n) & 0xff)

#define STM32F7_I2C_MAX_LEN			0xff
#define STM32F7_I2C_DMA_LEN_MIN			0x16
enum {
	STM32F7_SLAVE_HOSTNOTIFY,
	STM32F7_SLAVE_7_10_BITS_ADDR,
	STM32F7_SLAVE_7_BITS_ADDR,
	STM32F7_I2C_MAX_SLAVE
};

#define STM32F7_I2C_DNF_DEFAULT			0
#define STM32F7_I2C_DNF_MAX			15

#define STM32F7_I2C_ANALOG_FILTER_DELAY_MIN	50	/* ns */
#define STM32F7_I2C_ANALOG_FILTER_DELAY_MAX	260	/* ns */

#define STM32F7_I2C_RISE_TIME_DEFAULT		25	/* ns */
#define STM32F7_I2C_FALL_TIME_DEFAULT		10	/* ns */

#define STM32F7_PRESC_MAX			BIT(4)
#define STM32F7_SCLDEL_MAX			BIT(4)
#define STM32F7_SDADEL_MAX			BIT(4)
#define STM32F7_SCLH_MAX			BIT(8)
#define STM32F7_SCLL_MAX			BIT(8)

#define STM32F7_AUTOSUSPEND_DELAY		(HZ / 100)

/**
 * struct stm32f7_i2c_regs - i2c f7 registers backup
 * @cr1: Control register 1
 * @cr2: Control register 2
 * @oar1: Own address 1 register
 * @oar2: Own address 2 register
 * @tmgr: Timing register
 */
struct stm32f7_i2c_regs {
	u32 cr1;
	u32 cr2;
	u32 oar1;
	u32 oar2;
	u32 tmgr;
};

/**
 * struct stm32f7_i2c_spec - private i2c specification timing
 * @rate: I2C bus speed (Hz)
 * @fall_max: Max fall time of both SDA and SCL signals (ns)
 * @rise_max: Max rise time of both SDA and SCL signals (ns)
 * @hddat_min: Min data hold time (ns)
 * @vddat_max: Max data valid time (ns)
 * @sudat_min: Min data setup time (ns)
 * @l_min: Min low period of the SCL clock (ns)
 * @h_min: Min high period of the SCL clock (ns)
 */
struct stm32f7_i2c_spec {
	u32 rate;
	u32 fall_max;
	u32 rise_max;
	u32 hddat_min;
	u32 vddat_max;
	u32 sudat_min;
	u32 l_min;
	u32 h_min;
};

/**
 * struct stm32f7_i2c_setup - private I2C timing setup parameters
 * @speed_freq: I2C speed frequency  (Hz)
 * @clock_src: I2C clock source frequency (Hz)
 * @rise_time: Rise time (ns)
 * @fall_time: Fall time (ns)
 * @fmp_clr_offset: Fast Mode Plus clear register offset from set register
 */
struct stm32f7_i2c_setup {
	u32 speed_freq;
	u32 clock_src;
	u32 rise_time;
	u32 fall_time;
	u32 fmp_clr_offset;
};

/**
 * struct stm32f7_i2c_timings - private I2C output parameters
 * @node: List entry
 * @presc: Prescaler value
 * @scldel: Data setup time
 * @sdadel: Data hold time
 * @sclh: SCL high period (master mode)
 * @scll: SCL low period (master mode)
 */
struct stm32f7_i2c_timings {
	struct list_head node;
	u8 presc;
	u8 scldel;
	u8 sdadel;
	u8 sclh;
	u8 scll;
};

/**
 * struct stm32f7_i2c_msg - client specific data
 * @addr: 8-bit or 10-bit slave addr, including r/w bit
 * @count: number of bytes to be transferred
 * @buf: data buffer
 * @result: result of the transfer
 * @stop: last I2C msg to be sent, i.e. STOP to be generated
 * @smbus: boolean to know if the I2C IP is used in SMBus mode
 * @size: type of SMBus protocol
 * @read_write: direction of SMBus protocol
 * SMBus block read and SMBus block write - block read process call protocols
 * @smbus_buf: buffer to be used for SMBus protocol transfer. It will
 * contain a maximum of 32 bytes of data + byte command + byte count + PEC
 * This buffer has to be 32-bit aligned to be compliant with memory address
 * register in DMA mode.
 */
struct stm32f7_i2c_msg {
	u16 addr;
	u32 count;
	u8 *buf;
	int result;
	bool stop;
	bool smbus;
	int size;
	char read_write;
	u8 smbus_buf[I2C_SMBUS_BLOCK_MAX + 3] __aligned(4);
};

/**
 * struct stm32f7_i2c_alert - SMBus alert specific data
 * @setup: platform data for the smbus_alert i2c client
 * @ara: I2C slave device used to respond to the SMBus Alert with Alert
 * Response Address
 */
struct stm32f7_i2c_alert {
	struct i2c_smbus_alert_setup setup;
	struct i2c_client *ara;
};

/**
 * struct stm32f7_i2c_dev - private data of the controller
 * @adap: I2C adapter for this controller
 * @dev: device for this controller
 * @base: virtual memory area
 * @complete: completion of I2C message
 * @clk: hw i2c clock
 * @bus_rate: I2C clock frequency of the controller
 * @msg: Pointer to data to be written
 * @msg_num: number of I2C messages to be executed
 * @msg_id: message identifiant
 * @f7_msg: customized i2c msg for driver usage
 * @setup: I2C timing input setup
 * @timing: I2C computed timings
 * @slave: list of slave devices registered on the I2C bus
 * @slave_running: slave device currently used
 * @backup_regs: backup of i2c controller registers (for suspend/resume)
 * @slave_dir: transfer direction for the current slave device
 * @master_mode: boolean to know in which mode the I2C is running (master or
 * slave)
 * @dma: dma data
 * @use_dma: boolean to know if dma is used in the current transfer
 * @regmap: holds SYSCFG phandle for Fast Mode Plus bits
 * @fmp_sreg: register address for setting Fast Mode Plus bits
 * @fmp_creg: register address for clearing Fast Mode Plus bits
 * @fmp_mask: mask for Fast Mode Plus bits in set register
 * @wakeup_src: boolean to know if the device is a wakeup source
 * @smbus_mode: states that the controller is configured in SMBus mode
 * @host_notify_client: SMBus host-notify client
 * @analog_filter: boolean to indicate enabling of the analog filter
 * @dnf_dt: value of digital filter requested via dt
 * @dnf: value of digital filter to apply
 * @alert: SMBus alert specific data
 */
struct stm32f7_i2c_dev {
	struct i2c_adapter adap;
	struct device *dev;
	void __iomem *base;
	struct completion complete;
	struct clk *clk;
	unsigned int bus_rate;
	struct i2c_msg *msg;
	unsigned int msg_num;
	unsigned int msg_id;
	struct stm32f7_i2c_msg f7_msg;
	struct stm32f7_i2c_setup setup;
	struct stm32f7_i2c_timings timing;
	struct i2c_client *slave[STM32F7_I2C_MAX_SLAVE];
	struct i2c_client *slave_running;
	struct stm32f7_i2c_regs backup_regs;
	u32 slave_dir;
	bool master_mode;
	struct stm32_i2c_dma *dma;
	bool use_dma;
	struct regmap *regmap;
	u32 fmp_sreg;
	u32 fmp_creg;
	u32 fmp_mask;
	bool wakeup_src;
	bool smbus_mode;
	struct i2c_client *host_notify_client;
	bool analog_filter;
	u32 dnf_dt;
	u32 dnf;
	struct stm32f7_i2c_alert *alert;
};

/*
 * All these values are coming from I2C Specification, Version 6.0, 4th of
 * April 2014.
 *
 * Table10. Characteristics of the SDA and SCL bus lines for Standard, Fast,
 * and Fast-mode Plus I2C-bus devices
 */
static struct stm32f7_i2c_spec stm32f7_i2c_specs[] = {
	{
		.rate = I2C_MAX_STANDARD_MODE_FREQ,
		.fall_max = 300,
		.rise_max = 1000,
		.hddat_min = 0,
		.vddat_max = 3450,
		.sudat_min = 250,
		.l_min = 4700,
		.h_min = 4000,
	},
	{
		.rate = I2C_MAX_FAST_MODE_FREQ,
		.fall_max = 300,
		.rise_max = 300,
		.hddat_min = 0,
		.vddat_max = 900,
		.sudat_min = 100,
		.l_min = 1300,
		.h_min = 600,
	},
	{
		.rate = I2C_MAX_FAST_MODE_PLUS_FREQ,
		.fall_max = 100,
		.rise_max = 120,
		.hddat_min = 0,
		.vddat_max = 450,
		.sudat_min = 50,
		.l_min = 500,
		.h_min = 260,
	},
};

static const struct stm32f7_i2c_setup stm32f7_setup = {
	.rise_time = STM32F7_I2C_RISE_TIME_DEFAULT,
	.fall_time = STM32F7_I2C_FALL_TIME_DEFAULT,
};

static const struct stm32f7_i2c_setup stm32mp15_setup = {
	.rise_time = STM32F7_I2C_RISE_TIME_DEFAULT,
	.fall_time = STM32F7_I2C_FALL_TIME_DEFAULT,
	.fmp_clr_offset = 0x40,
};

static const struct stm32f7_i2c_setup stm32mp13_setup = {
	.rise_time = STM32F7_I2C_RISE_TIME_DEFAULT,
	.fall_time = STM32F7_I2C_FALL_TIME_DEFAULT,
	.fmp_clr_offset = 0x4,
};

static inline void stm32f7_i2c_set_bits(void __iomem *reg, u32 mask)
{
	writel_relaxed(readl_relaxed(reg) | mask, reg);
}

static inline void stm32f7_i2c_clr_bits(void __iomem *reg, u32 mask)
{
	writel_relaxed(readl_relaxed(reg) & ~mask, reg);
}

static void stm32f7_i2c_disable_irq(struct stm32f7_i2c_dev *i2c_dev, u32 mask)
{
	stm32f7_i2c_clr_bits(i2c_dev->base + STM32F7_I2C_CR1, mask);
}

static struct stm32f7_i2c_spec *stm32f7_get_specs(u32 rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(stm32f7_i2c_specs); i++)
		if (rate <= stm32f7_i2c_specs[i].rate)
			return &stm32f7_i2c_specs[i];

	return ERR_PTR(-EINVAL);
}

#define	RATE_MIN(rate)	((rate) * 8 / 10)
static int stm32f7_i2c_compute_timing(struct stm32f7_i2c_dev *i2c_dev,
				      struct stm32f7_i2c_setup *setup,
				      struct stm32f7_i2c_timings *output)
{
	struct stm32f7_i2c_spec *specs;
	u32 p_prev = STM32F7_PRESC_MAX;
	u32 i2cclk = DIV_ROUND_CLOSEST(NSEC_PER_SEC,
				       setup->clock_src);
	u32 i2cbus = DIV_ROUND_CLOSEST(NSEC_PER_SEC,
				       setup->speed_freq);
	u32 clk_error_prev = i2cbus;
	u32 tsync;
	u32 af_delay_min, af_delay_max;
	u32 dnf_delay;
	u32 clk_min, clk_max;
	int sdadel_min, sdadel_max;
	int scldel_min;
	struct stm32f7_i2c_timings *v, *_v, *s;
	struct list_head solutions;
	u16 p, l, a, h;
	int ret = 0;

	specs = stm32f7_get_specs(setup->speed_freq);
	if (specs == ERR_PTR(-EINVAL)) {
		dev_err(i2c_dev->dev, "speed out of bound {%d}\n",
			setup->speed_freq);
		return -EINVAL;
	}

	if ((setup->rise_time > specs->rise_max) ||
	    (setup->fall_time > specs->fall_max)) {
		dev_err(i2c_dev->dev,
			"timings out of bound Rise{%d>%d}/Fall{%d>%d}\n",
			setup->rise_time, specs->rise_max,
			setup->fall_time, specs->fall_max);
		return -EINVAL;
	}

	i2c_dev->dnf = DIV_ROUND_CLOSEST(i2c_dev->dnf_dt, i2cclk);
	if (i2c_dev->dnf > STM32F7_I2C_DNF_MAX) {
		dev_err(i2c_dev->dev,
			"DNF out of bound %d/%d\n",
			i2c_dev->dnf * i2cclk, STM32F7_I2C_DNF_MAX * i2cclk);
		return -EINVAL;
	}

	/*  Analog and Digital Filters */
	af_delay_min =
		(i2c_dev->analog_filter ?
		 STM32F7_I2C_ANALOG_FILTER_DELAY_MIN : 0);
	af_delay_max =
		(i2c_dev->analog_filter ?
		 STM32F7_I2C_ANALOG_FILTER_DELAY_MAX : 0);
	dnf_delay = i2c_dev->dnf * i2cclk;

	sdadel_min = specs->hddat_min + setup->fall_time -
		af_delay_min - (i2c_dev->dnf + 3) * i2cclk;

	sdadel_max = specs->vddat_max - setup->rise_time -
		af_delay_max - (i2c_dev->dnf + 4) * i2cclk;

	scldel_min = setup->rise_time + specs->sudat_min;

	if (sdadel_min < 0)
		sdadel_min = 0;
	if (sdadel_max < 0)
		sdadel_max = 0;

	dev_dbg(i2c_dev->dev, "SDADEL(min/max): %i/%i, SCLDEL(Min): %i\n",
		sdadel_min, sdadel_max, scldel_min);

	INIT_LIST_HEAD(&solutions);
	/* Compute possible values for PRESC, SCLDEL and SDADEL */
	for (p = 0; p < STM32F7_PRESC_MAX; p++) {
		for (l = 0; l < STM32F7_SCLDEL_MAX; l++) {
			u32 scldel = (l + 1) * (p + 1) * i2cclk;

			if (scldel < scldel_min)
				continue;

			for (a = 0; a < STM32F7_SDADEL_MAX; a++) {
				u32 sdadel = (a * (p + 1) + 1) * i2cclk;

				if (((sdadel >= sdadel_min) &&
				     (sdadel <= sdadel_max)) &&
				    (p != p_prev)) {
					v = kmalloc(sizeof(*v), GFP_KERNEL);
					if (!v) {
						ret = -ENOMEM;
						goto exit;
					}

					v->presc = p;
					v->scldel = l;
					v->sdadel = a;
					p_prev = p;

					list_add_tail(&v->node,
						      &solutions);
					break;
				}
			}

			if (p_prev == p)
				break;
		}
	}

	if (list_empty(&solutions)) {
		dev_err(i2c_dev->dev, "no Prescaler solution\n");
		ret = -EPERM;
		goto exit;
	}

	tsync = af_delay_min + dnf_delay + (2 * i2cclk);
	s = NULL;
	clk_max = NSEC_PER_SEC / RATE_MIN(setup->speed_freq);
	clk_min = NSEC_PER_SEC / setup->speed_freq;

	/*
	 * Among Prescaler possibilities discovered above figures out SCL Low
	 * and High Period. Provided:
	 * - SCL Low Period has to be higher than SCL Clock Low Period
	 *   defined by I2C Specification. I2C Clock has to be lower than
	 *   (SCL Low Period - Analog/Digital filters) / 4.
	 * - SCL High Period has to be lower than SCL Clock High Period
	 *   defined by I2C Specification
	 * - I2C Clock has to be lower than SCL High Period
	 */
	list_for_each_entry(v, &solutions, node) {
		u32 prescaler = (v->presc + 1) * i2cclk;

		for (l = 0; l < STM32F7_SCLL_MAX; l++) {
			u32 tscl_l = (l + 1) * prescaler + tsync;

			if ((tscl_l < specs->l_min) ||
			    (i2cclk >=
			     ((tscl_l - af_delay_min - dnf_delay) / 4))) {
				continue;
			}

			for (h = 0; h < STM32F7_SCLH_MAX; h++) {
				u32 tscl_h = (h + 1) * prescaler + tsync;
				u32 tscl = tscl_l + tscl_h +
					setup->rise_time + setup->fall_time;

				if ((tscl >= clk_min) && (tscl <= clk_max) &&
				    (tscl_h >= specs->h_min) &&
				    (i2cclk < tscl_h)) {
					int clk_error = tscl - i2cbus;

					if (clk_error < 0)
						clk_error = -clk_error;

					if (clk_error < clk_error_prev) {
						clk_error_prev = clk_error;
						v->scll = l;
						v->sclh = h;
						s = v;
					}
				}
			}
		}
	}

	if (!s) {
		dev_err(i2c_dev->dev, "no solution at all\n");
		ret = -EPERM;
		goto exit;
	}

	output->presc = s->presc;
	output->scldel = s->scldel;
	output->sdadel = s->sdadel;
	output->scll = s->scll;
	output->sclh = s->sclh;

	dev_dbg(i2c_dev->dev,
		"Presc: %i, scldel: %i, sdadel: %i, scll: %i, sclh: %i\n",
		output->presc,
		output->scldel, output->sdadel,
		output->scll, output->sclh);

exit:
	/* Release list and memory */
	list_for_each_entry_safe(v, _v, &solutions, node) {
		list_del(&v->node);
		kfree(v);
	}

	return ret;
}

static u32 stm32f7_get_lower_rate(u32 rate)
{
	int i = ARRAY_SIZE(stm32f7_i2c_specs);

	while (--i)
		if (stm32f7_i2c_specs[i].rate < rate)
			break;

	return stm32f7_i2c_specs[i].rate;
}

static int stm32f7_i2c_setup_timing(struct stm32f7_i2c_dev *i2c_dev,
				    struct stm32f7_i2c_setup *setup)
{
	struct i2c_timings timings, *t = &timings;
	int ret = 0;

	t->bus_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
	t->scl_rise_ns = i2c_dev->setup.rise_time;
	t->scl_fall_ns = i2c_dev->setup.fall_time;

	i2c_parse_fw_timings(i2c_dev->dev, t, false);

	if (t->bus_freq_hz > I2C_MAX_FAST_MODE_PLUS_FREQ) {
		dev_err(i2c_dev->dev, "Invalid bus speed (%i>%i)\n",
			t->bus_freq_hz, I2C_MAX_FAST_MODE_PLUS_FREQ);
		return -EINVAL;
	}

	setup->speed_freq = t->bus_freq_hz;
	i2c_dev->setup.rise_time = t->scl_rise_ns;
	i2c_dev->setup.fall_time = t->scl_fall_ns;
	i2c_dev->dnf_dt = t->digital_filter_width_ns;
	setup->clock_src = clk_get_rate(i2c_dev->clk);

	if (!setup->clock_src) {
		dev_err(i2c_dev->dev, "clock rate is 0\n");
		return -EINVAL;
	}

	if (!of_property_read_bool(i2c_dev->dev->of_node, "i2c-digital-filter"))
		i2c_dev->dnf_dt = STM32F7_I2C_DNF_DEFAULT;

	do {
		ret = stm32f7_i2c_compute_timing(i2c_dev, setup,
						 &i2c_dev->timing);
		if (ret) {
			dev_err(i2c_dev->dev,
				"failed to compute I2C timings.\n");
			if (setup->speed_freq <= I2C_MAX_STANDARD_MODE_FREQ)
				break;
			setup->speed_freq =
				stm32f7_get_lower_rate(setup->speed_freq);
			dev_warn(i2c_dev->dev,
				 "downgrade I2C Speed Freq to (%i)\n",
				 setup->speed_freq);
		}
	} while (ret);

	if (ret) {
		dev_err(i2c_dev->dev, "Impossible to compute I2C timings.\n");
		return ret;
	}

	i2c_dev->analog_filter = of_property_read_bool(i2c_dev->dev->of_node,
						       "i2c-analog-filter");

	dev_dbg(i2c_dev->dev, "I2C Speed(%i), Clk Source(%i)\n",
		setup->speed_freq, setup->clock_src);
	dev_dbg(i2c_dev->dev, "I2C Rise(%i) and Fall(%i) Time\n",
		setup->rise_time, setup->fall_time);
	dev_dbg(i2c_dev->dev, "I2C Analog Filter(%s), DNF(%i)\n",
		(i2c_dev->analog_filter ? "On" : "Off"), i2c_dev->dnf);

	i2c_dev->bus_rate = setup->speed_freq;

	return 0;
}

static void stm32f7_i2c_disable_dma_req(struct stm32f7_i2c_dev *i2c_dev)
{
	void __iomem *base = i2c_dev->base;
	u32 mask = STM32F7_I2C_CR1_RXDMAEN | STM32F7_I2C_CR1_TXDMAEN;

	stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR1, mask);
}

static void stm32f7_i2c_dma_callback(void *arg)
{
	struct stm32f7_i2c_dev *i2c_dev = (struct stm32f7_i2c_dev *)arg;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	struct device *dev = dma->chan_using->device->dev;

	stm32f7_i2c_disable_dma_req(i2c_dev);
	dma_unmap_single(dev, dma->dma_buf, dma->dma_len, dma->dma_data_dir);
	complete(&dma->dma_complete);
}

static void stm32f7_i2c_hw_config(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_timings *t = &i2c_dev->timing;
	u32 timing = 0;

	/* Timing settings */
	timing |= STM32F7_I2C_TIMINGR_PRESC(t->presc);
	timing |= STM32F7_I2C_TIMINGR_SCLDEL(t->scldel);
	timing |= STM32F7_I2C_TIMINGR_SDADEL(t->sdadel);
	timing |= STM32F7_I2C_TIMINGR_SCLH(t->sclh);
	timing |= STM32F7_I2C_TIMINGR_SCLL(t->scll);
	writel_relaxed(timing, i2c_dev->base + STM32F7_I2C_TIMINGR);

	/* Configure the Analog Filter */
	if (i2c_dev->analog_filter)
		stm32f7_i2c_clr_bits(i2c_dev->base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_ANFOFF);
	else
		stm32f7_i2c_set_bits(i2c_dev->base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_ANFOFF);

	/* Program the Digital Filter */
	stm32f7_i2c_clr_bits(i2c_dev->base + STM32F7_I2C_CR1,
			     STM32F7_I2C_CR1_DNF_MASK);
	stm32f7_i2c_set_bits(i2c_dev->base + STM32F7_I2C_CR1,
			     STM32F7_I2C_CR1_DNF(i2c_dev->dnf));

	stm32f7_i2c_set_bits(i2c_dev->base + STM32F7_I2C_CR1,
			     STM32F7_I2C_CR1_PE);
}

static void stm32f7_i2c_write_tx_data(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	void __iomem *base = i2c_dev->base;

	if (f7_msg->count) {
		writeb_relaxed(*f7_msg->buf++, base + STM32F7_I2C_TXDR);
		f7_msg->count--;
	}
}

static void stm32f7_i2c_read_rx_data(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	void __iomem *base = i2c_dev->base;

	if (f7_msg->count) {
		*f7_msg->buf++ = readb_relaxed(base + STM32F7_I2C_RXDR);
		f7_msg->count--;
	} else {
		/* Flush RX buffer has no data is expected */
		readb_relaxed(base + STM32F7_I2C_RXDR);
	}
}

static void stm32f7_i2c_reload(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	u32 cr2;

	if (i2c_dev->use_dma)
		f7_msg->count -= STM32F7_I2C_MAX_LEN;

	cr2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR2);

	cr2 &= ~STM32F7_I2C_CR2_NBYTES_MASK;
	if (f7_msg->count > STM32F7_I2C_MAX_LEN) {
		cr2 |= STM32F7_I2C_CR2_NBYTES(STM32F7_I2C_MAX_LEN);
	} else {
		cr2 &= ~STM32F7_I2C_CR2_RELOAD;
		cr2 |= STM32F7_I2C_CR2_NBYTES(f7_msg->count);
	}

	writel_relaxed(cr2, i2c_dev->base + STM32F7_I2C_CR2);
}

static void stm32f7_i2c_smbus_reload(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	u32 cr2;
	u8 *val;

	/*
	 * For I2C_SMBUS_BLOCK_DATA && I2C_SMBUS_BLOCK_PROC_CALL, the first
	 * data received inform us how many data will follow.
	 */
	stm32f7_i2c_read_rx_data(i2c_dev);

	/*
	 * Update NBYTES with the value read to continue the transfer
	 */
	val = f7_msg->buf - sizeof(u8);
	f7_msg->count = *val;
	cr2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR2);
	cr2 &= ~(STM32F7_I2C_CR2_NBYTES_MASK | STM32F7_I2C_CR2_RELOAD);
	cr2 |= STM32F7_I2C_CR2_NBYTES(f7_msg->count);
	writel_relaxed(cr2, i2c_dev->base + STM32F7_I2C_CR2);
}

static void stm32f7_i2c_release_bus(struct i2c_adapter *i2c_adap)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(i2c_adap);

	stm32f7_i2c_clr_bits(i2c_dev->base + STM32F7_I2C_CR1,
			     STM32F7_I2C_CR1_PE);

	stm32f7_i2c_hw_config(i2c_dev);
}

static int stm32f7_i2c_wait_free_bus(struct stm32f7_i2c_dev *i2c_dev)
{
	u32 status;
	int ret;

	ret = readl_relaxed_poll_timeout(i2c_dev->base + STM32F7_I2C_ISR,
					 status,
					 !(status & STM32F7_I2C_ISR_BUSY),
					 10, 1000);
	if (!ret)
		return 0;

	stm32f7_i2c_release_bus(&i2c_dev->adap);

	return -EBUSY;
}

static void stm32f7_i2c_xfer_msg(struct stm32f7_i2c_dev *i2c_dev,
				 struct i2c_msg *msg)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	void __iomem *base = i2c_dev->base;
	u32 cr1, cr2;
	int ret;

	f7_msg->addr = msg->addr;
	f7_msg->buf = msg->buf;
	f7_msg->count = msg->len;
	f7_msg->result = 0;
	f7_msg->stop = (i2c_dev->msg_id >= i2c_dev->msg_num - 1);

	reinit_completion(&i2c_dev->complete);

	cr1 = readl_relaxed(base + STM32F7_I2C_CR1);
	cr2 = readl_relaxed(base + STM32F7_I2C_CR2);

	/* Set transfer direction */
	cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
	if (msg->flags & I2C_M_RD)
		cr2 |= STM32F7_I2C_CR2_RD_WRN;

	/* Set slave address */
	cr2 &= ~(STM32F7_I2C_CR2_HEAD10R | STM32F7_I2C_CR2_ADD10);
	if (msg->flags & I2C_M_TEN) {
		cr2 &= ~STM32F7_I2C_CR2_SADD10_MASK;
		cr2 |= STM32F7_I2C_CR2_SADD10(f7_msg->addr);
		cr2 |= STM32F7_I2C_CR2_ADD10;
	} else {
		cr2 &= ~STM32F7_I2C_CR2_SADD7_MASK;
		cr2 |= STM32F7_I2C_CR2_SADD7(f7_msg->addr);
	}

	/* Set nb bytes to transfer and reload if needed */
	cr2 &= ~(STM32F7_I2C_CR2_NBYTES_MASK | STM32F7_I2C_CR2_RELOAD);
	if (f7_msg->count > STM32F7_I2C_MAX_LEN) {
		cr2 |= STM32F7_I2C_CR2_NBYTES(STM32F7_I2C_MAX_LEN);
		cr2 |= STM32F7_I2C_CR2_RELOAD;
	} else {
		cr2 |= STM32F7_I2C_CR2_NBYTES(f7_msg->count);
	}

	/* Enable NACK, STOP, error and transfer complete interrupts */
	cr1 |= STM32F7_I2C_CR1_ERRIE | STM32F7_I2C_CR1_TCIE |
		STM32F7_I2C_CR1_STOPIE | STM32F7_I2C_CR1_NACKIE;

	/* Clear DMA req and TX/RX interrupt */
	cr1 &= ~(STM32F7_I2C_CR1_RXIE | STM32F7_I2C_CR1_TXIE |
			STM32F7_I2C_CR1_RXDMAEN | STM32F7_I2C_CR1_TXDMAEN);

	/* Configure DMA or enable RX/TX interrupt */
	i2c_dev->use_dma = false;
	if (i2c_dev->dma && f7_msg->count >= STM32F7_I2C_DMA_LEN_MIN) {
		ret = stm32_i2c_prep_dma_xfer(i2c_dev->dev, i2c_dev->dma,
					      msg->flags & I2C_M_RD,
					      f7_msg->count, f7_msg->buf,
					      stm32f7_i2c_dma_callback,
					      i2c_dev);
		if (!ret)
			i2c_dev->use_dma = true;
		else
			dev_warn(i2c_dev->dev, "can't use DMA\n");
	}

	if (!i2c_dev->use_dma) {
		if (msg->flags & I2C_M_RD)
			cr1 |= STM32F7_I2C_CR1_RXIE;
		else
			cr1 |= STM32F7_I2C_CR1_TXIE;
	} else {
		if (msg->flags & I2C_M_RD)
			cr1 |= STM32F7_I2C_CR1_RXDMAEN;
		else
			cr1 |= STM32F7_I2C_CR1_TXDMAEN;
	}

	/* Configure Start/Repeated Start */
	cr2 |= STM32F7_I2C_CR2_START;

	i2c_dev->master_mode = true;

	/* Write configurations registers */
	writel_relaxed(cr1, base + STM32F7_I2C_CR1);
	writel_relaxed(cr2, base + STM32F7_I2C_CR2);
}

static int stm32f7_i2c_smbus_xfer_msg(struct stm32f7_i2c_dev *i2c_dev,
				      unsigned short flags, u8 command,
				      union i2c_smbus_data *data)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	struct device *dev = i2c_dev->dev;
	void __iomem *base = i2c_dev->base;
	u32 cr1, cr2;
	int i, ret;

	f7_msg->result = 0;
	reinit_completion(&i2c_dev->complete);

	cr2 = readl_relaxed(base + STM32F7_I2C_CR2);
	cr1 = readl_relaxed(base + STM32F7_I2C_CR1);

	/* Set transfer direction */
	cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
	if (f7_msg->read_write)
		cr2 |= STM32F7_I2C_CR2_RD_WRN;

	/* Set slave address */
	cr2 &= ~(STM32F7_I2C_CR2_ADD10 | STM32F7_I2C_CR2_SADD7_MASK);
	cr2 |= STM32F7_I2C_CR2_SADD7(f7_msg->addr);

	f7_msg->smbus_buf[0] = command;
	switch (f7_msg->size) {
	case I2C_SMBUS_QUICK:
		f7_msg->stop = true;
		f7_msg->count = 0;
		break;
	case I2C_SMBUS_BYTE:
		f7_msg->stop = true;
		f7_msg->count = 1;
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (f7_msg->read_write) {
			f7_msg->stop = false;
			f7_msg->count = 1;
			cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
		} else {
			f7_msg->stop = true;
			f7_msg->count = 2;
			f7_msg->smbus_buf[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (f7_msg->read_write) {
			f7_msg->stop = false;
			f7_msg->count = 1;
			cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
		} else {
			f7_msg->stop = true;
			f7_msg->count = 3;
			f7_msg->smbus_buf[1] = data->word & 0xff;
			f7_msg->smbus_buf[2] = data->word >> 8;
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (f7_msg->read_write) {
			f7_msg->stop = false;
			f7_msg->count = 1;
			cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
		} else {
			f7_msg->stop = true;
			if (data->block[0] > I2C_SMBUS_BLOCK_MAX ||
			    !data->block[0]) {
				dev_err(dev, "Invalid block write size %d\n",
					data->block[0]);
				return -EINVAL;
			}
			f7_msg->count = data->block[0] + 2;
			for (i = 1; i < f7_msg->count; i++)
				f7_msg->smbus_buf[i] = data->block[i - 1];
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		f7_msg->stop = false;
		f7_msg->count = 3;
		f7_msg->smbus_buf[1] = data->word & 0xff;
		f7_msg->smbus_buf[2] = data->word >> 8;
		cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
		f7_msg->read_write = I2C_SMBUS_READ;
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		f7_msg->stop = false;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX - 1) {
			dev_err(dev, "Invalid block write size %d\n",
				data->block[0]);
			return -EINVAL;
		}
		f7_msg->count = data->block[0] + 2;
		for (i = 1; i < f7_msg->count; i++)
			f7_msg->smbus_buf[i] = data->block[i - 1];
		cr2 &= ~STM32F7_I2C_CR2_RD_WRN;
		f7_msg->read_write = I2C_SMBUS_READ;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		/* Rely on emulated i2c transfer (through master_xfer) */
		return -EOPNOTSUPP;
	default:
		dev_err(dev, "Unsupported smbus protocol %d\n", f7_msg->size);
		return -EOPNOTSUPP;
	}

	f7_msg->buf = f7_msg->smbus_buf;

	/* Configure PEC */
	if ((flags & I2C_CLIENT_PEC) && f7_msg->size != I2C_SMBUS_QUICK) {
		cr1 |= STM32F7_I2C_CR1_PECEN;
		if (!f7_msg->read_write) {
			cr2 |= STM32F7_I2C_CR2_PECBYTE;
			f7_msg->count++;
		}
	} else {
		cr1 &= ~STM32F7_I2C_CR1_PECEN;
		cr2 &= ~STM32F7_I2C_CR2_PECBYTE;
	}

	/* Set number of bytes to be transferred */
	cr2 &= ~(STM32F7_I2C_CR2_NBYTES_MASK | STM32F7_I2C_CR2_RELOAD);
	cr2 |= STM32F7_I2C_CR2_NBYTES(f7_msg->count);

	/* Enable NACK, STOP, error and transfer complete interrupts */
	cr1 |= STM32F7_I2C_CR1_ERRIE | STM32F7_I2C_CR1_TCIE |
		STM32F7_I2C_CR1_STOPIE | STM32F7_I2C_CR1_NACKIE;

	/* Clear DMA req and TX/RX interrupt */
	cr1 &= ~(STM32F7_I2C_CR1_RXIE | STM32F7_I2C_CR1_TXIE |
			STM32F7_I2C_CR1_RXDMAEN | STM32F7_I2C_CR1_TXDMAEN);

	/* Configure DMA or enable RX/TX interrupt */
	i2c_dev->use_dma = false;
	if (i2c_dev->dma && f7_msg->count >= STM32F7_I2C_DMA_LEN_MIN) {
		ret = stm32_i2c_prep_dma_xfer(i2c_dev->dev, i2c_dev->dma,
					      cr2 & STM32F7_I2C_CR2_RD_WRN,
					      f7_msg->count, f7_msg->buf,
					      stm32f7_i2c_dma_callback,
					      i2c_dev);
		if (!ret)
			i2c_dev->use_dma = true;
		else
			dev_warn(i2c_dev->dev, "can't use DMA\n");
	}

	if (!i2c_dev->use_dma) {
		if (cr2 & STM32F7_I2C_CR2_RD_WRN)
			cr1 |= STM32F7_I2C_CR1_RXIE;
		else
			cr1 |= STM32F7_I2C_CR1_TXIE;
	} else {
		if (cr2 & STM32F7_I2C_CR2_RD_WRN)
			cr1 |= STM32F7_I2C_CR1_RXDMAEN;
		else
			cr1 |= STM32F7_I2C_CR1_TXDMAEN;
	}

	/* Set Start bit */
	cr2 |= STM32F7_I2C_CR2_START;

	i2c_dev->master_mode = true;

	/* Write configurations registers */
	writel_relaxed(cr1, base + STM32F7_I2C_CR1);
	writel_relaxed(cr2, base + STM32F7_I2C_CR2);

	return 0;
}

static void stm32f7_i2c_smbus_rep_start(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	void __iomem *base = i2c_dev->base;
	u32 cr1, cr2;
	int ret;

	cr2 = readl_relaxed(base + STM32F7_I2C_CR2);
	cr1 = readl_relaxed(base + STM32F7_I2C_CR1);

	/* Set transfer direction */
	cr2 |= STM32F7_I2C_CR2_RD_WRN;

	switch (f7_msg->size) {
	case I2C_SMBUS_BYTE_DATA:
		f7_msg->count = 1;
		break;
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		f7_msg->count = 2;
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_BLOCK_PROC_CALL:
		f7_msg->count = 1;
		cr2 |= STM32F7_I2C_CR2_RELOAD;
		break;
	}

	f7_msg->buf = f7_msg->smbus_buf;
	f7_msg->stop = true;

	/* Add one byte for PEC if needed */
	if (cr1 & STM32F7_I2C_CR1_PECEN) {
		cr2 |= STM32F7_I2C_CR2_PECBYTE;
		f7_msg->count++;
	}

	/* Set number of bytes to be transferred */
	cr2 &= ~(STM32F7_I2C_CR2_NBYTES_MASK);
	cr2 |= STM32F7_I2C_CR2_NBYTES(f7_msg->count);

	/*
	 * Configure RX/TX interrupt:
	 */
	cr1 &= ~(STM32F7_I2C_CR1_RXIE | STM32F7_I2C_CR1_TXIE);
	cr1 |= STM32F7_I2C_CR1_RXIE;

	/*
	 * Configure DMA or enable RX/TX interrupt:
	 * For I2C_SMBUS_BLOCK_DATA and I2C_SMBUS_BLOCK_PROC_CALL we don't use
	 * dma as we don't know in advance how many data will be received
	 */
	cr1 &= ~(STM32F7_I2C_CR1_RXIE | STM32F7_I2C_CR1_TXIE |
		 STM32F7_I2C_CR1_RXDMAEN | STM32F7_I2C_CR1_TXDMAEN);

	i2c_dev->use_dma = false;
	if (i2c_dev->dma && f7_msg->count >= STM32F7_I2C_DMA_LEN_MIN &&
	    f7_msg->size != I2C_SMBUS_BLOCK_DATA &&
	    f7_msg->size != I2C_SMBUS_BLOCK_PROC_CALL) {
		ret = stm32_i2c_prep_dma_xfer(i2c_dev->dev, i2c_dev->dma,
					      cr2 & STM32F7_I2C_CR2_RD_WRN,
					      f7_msg->count, f7_msg->buf,
					      stm32f7_i2c_dma_callback,
					      i2c_dev);

		if (!ret)
			i2c_dev->use_dma = true;
		else
			dev_warn(i2c_dev->dev, "can't use DMA\n");
	}

	if (!i2c_dev->use_dma)
		cr1 |= STM32F7_I2C_CR1_RXIE;
	else
		cr1 |= STM32F7_I2C_CR1_RXDMAEN;

	/* Configure Repeated Start */
	cr2 |= STM32F7_I2C_CR2_START;

	/* Write configurations registers */
	writel_relaxed(cr1, base + STM32F7_I2C_CR1);
	writel_relaxed(cr2, base + STM32F7_I2C_CR2);
}

static int stm32f7_i2c_smbus_check_pec(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	u8 count, internal_pec, received_pec;

	internal_pec = readl_relaxed(i2c_dev->base + STM32F7_I2C_PECR);

	switch (f7_msg->size) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		received_pec = f7_msg->smbus_buf[1];
		break;
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		received_pec = f7_msg->smbus_buf[2];
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_BLOCK_PROC_CALL:
		count = f7_msg->smbus_buf[0];
		received_pec = f7_msg->smbus_buf[count];
		break;
	default:
		dev_err(i2c_dev->dev, "Unsupported smbus protocol for PEC\n");
		return -EINVAL;
	}

	if (internal_pec != received_pec) {
		dev_err(i2c_dev->dev, "Bad PEC 0x%02x vs. 0x%02x\n",
			internal_pec, received_pec);
		return -EBADMSG;
	}

	return 0;
}

static bool stm32f7_i2c_is_addr_match(struct i2c_client *slave, u32 addcode)
{
	u32 addr;

	if (!slave)
		return false;

	if (slave->flags & I2C_CLIENT_TEN) {
		/*
		 * For 10-bit addr, addcode = 11110XY with
		 * X = Bit 9 of slave address
		 * Y = Bit 8 of slave address
		 */
		addr = slave->addr >> 8;
		addr |= 0x78;
		if (addr == addcode)
			return true;
	} else {
		addr = slave->addr & 0x7f;
		if (addr == addcode)
			return true;
	}

	return false;
}

static void stm32f7_i2c_slave_start(struct stm32f7_i2c_dev *i2c_dev)
{
	struct i2c_client *slave = i2c_dev->slave_running;
	void __iomem *base = i2c_dev->base;
	u32 mask;
	u8 value = 0;

	if (i2c_dev->slave_dir) {
		/* Notify i2c slave that new read transfer is starting */
		i2c_slave_event(slave, I2C_SLAVE_READ_REQUESTED, &value);

		/*
		 * Disable slave TX config in case of I2C combined message
		 * (I2C Write followed by I2C Read)
		 */
		mask = STM32F7_I2C_CR2_RELOAD;
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR2, mask);
		mask = STM32F7_I2C_CR1_SBC | STM32F7_I2C_CR1_RXIE |
		       STM32F7_I2C_CR1_TCIE;
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR1, mask);

		/* Enable TX empty, STOP, NACK interrupts */
		mask =  STM32F7_I2C_CR1_STOPIE | STM32F7_I2C_CR1_NACKIE |
			STM32F7_I2C_CR1_TXIE;
		stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, mask);

		/* Write 1st data byte */
		writel_relaxed(value, base + STM32F7_I2C_TXDR);
	} else {
		/* Notify i2c slave that new write transfer is starting */
		i2c_slave_event(slave, I2C_SLAVE_WRITE_REQUESTED, &value);

		/* Set reload mode to be able to ACK/NACK each received byte */
		mask = STM32F7_I2C_CR2_RELOAD;
		stm32f7_i2c_set_bits(base + STM32F7_I2C_CR2, mask);

		/*
		 * Set STOP, NACK, RX empty and transfer complete interrupts.*
		 * Set Slave Byte Control to be able to ACK/NACK each data
		 * byte received
		 */
		mask =  STM32F7_I2C_CR1_STOPIE | STM32F7_I2C_CR1_NACKIE |
			STM32F7_I2C_CR1_SBC | STM32F7_I2C_CR1_RXIE |
			STM32F7_I2C_CR1_TCIE;
		stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, mask);
	}
}

static void stm32f7_i2c_slave_addr(struct stm32f7_i2c_dev *i2c_dev)
{
	void __iomem *base = i2c_dev->base;
	u32 isr, addcode, dir, mask;
	int i;

	isr = readl_relaxed(i2c_dev->base + STM32F7_I2C_ISR);
	addcode = STM32F7_I2C_ISR_ADDCODE_GET(isr);
	dir = isr & STM32F7_I2C_ISR_DIR;

	for (i = 0; i < STM32F7_I2C_MAX_SLAVE; i++) {
		if (stm32f7_i2c_is_addr_match(i2c_dev->slave[i], addcode)) {
			i2c_dev->slave_running = i2c_dev->slave[i];
			i2c_dev->slave_dir = dir;

			/* Start I2C slave processing */
			stm32f7_i2c_slave_start(i2c_dev);

			/* Clear ADDR flag */
			mask = STM32F7_I2C_ICR_ADDRCF;
			writel_relaxed(mask, base + STM32F7_I2C_ICR);
			break;
		}
	}
}

static int stm32f7_i2c_get_slave_id(struct stm32f7_i2c_dev *i2c_dev,
				    struct i2c_client *slave, int *id)
{
	int i;

	for (i = 0; i < STM32F7_I2C_MAX_SLAVE; i++) {
		if (i2c_dev->slave[i] == slave) {
			*id = i;
			return 0;
		}
	}

	dev_err(i2c_dev->dev, "Slave 0x%x not registered\n", slave->addr);

	return -ENODEV;
}

static int stm32f7_i2c_get_free_slave_id(struct stm32f7_i2c_dev *i2c_dev,
					 struct i2c_client *slave, int *id)
{
	struct device *dev = i2c_dev->dev;
	int i;

	/*
	 * slave[STM32F7_SLAVE_HOSTNOTIFY] support only SMBus Host address (0x8)
	 * slave[STM32F7_SLAVE_7_10_BITS_ADDR] supports 7-bit and 10-bit slave address
	 * slave[STM32F7_SLAVE_7_BITS_ADDR] supports 7-bit slave address only
	 */
	if (i2c_dev->smbus_mode && (slave->addr == 0x08)) {
		if (i2c_dev->slave[STM32F7_SLAVE_HOSTNOTIFY])
			goto fail;
		*id = STM32F7_SLAVE_HOSTNOTIFY;
		return 0;
	}

	for (i = STM32F7_I2C_MAX_SLAVE - 1; i > STM32F7_SLAVE_HOSTNOTIFY; i--) {
		if ((i == STM32F7_SLAVE_7_BITS_ADDR) &&
		    (slave->flags & I2C_CLIENT_TEN))
			continue;
		if (!i2c_dev->slave[i]) {
			*id = i;
			return 0;
		}
	}

fail:
	dev_err(dev, "Slave 0x%x could not be registered\n", slave->addr);

	return -EINVAL;
}

static bool stm32f7_i2c_is_slave_registered(struct stm32f7_i2c_dev *i2c_dev)
{
	int i;

	for (i = 0; i < STM32F7_I2C_MAX_SLAVE; i++) {
		if (i2c_dev->slave[i])
			return true;
	}

	return false;
}

static bool stm32f7_i2c_is_slave_busy(struct stm32f7_i2c_dev *i2c_dev)
{
	int i, busy;

	busy = 0;
	for (i = 0; i < STM32F7_I2C_MAX_SLAVE; i++) {
		if (i2c_dev->slave[i])
			busy++;
	}

	return i == busy;
}

static irqreturn_t stm32f7_i2c_slave_isr_event(struct stm32f7_i2c_dev *i2c_dev)
{
	void __iomem *base = i2c_dev->base;
	u32 cr2, status, mask;
	u8 val;
	int ret;

	status = readl_relaxed(i2c_dev->base + STM32F7_I2C_ISR);

	/* Slave transmitter mode */
	if (status & STM32F7_I2C_ISR_TXIS) {
		i2c_slave_event(i2c_dev->slave_running,
				I2C_SLAVE_READ_PROCESSED,
				&val);

		/* Write data byte */
		writel_relaxed(val, base + STM32F7_I2C_TXDR);
	}

	/* Transfer Complete Reload for Slave receiver mode */
	if (status & STM32F7_I2C_ISR_TCR || status & STM32F7_I2C_ISR_RXNE) {
		/*
		 * Read data byte then set NBYTES to receive next byte or NACK
		 * the current received byte
		 */
		val = readb_relaxed(i2c_dev->base + STM32F7_I2C_RXDR);
		ret = i2c_slave_event(i2c_dev->slave_running,
				      I2C_SLAVE_WRITE_RECEIVED,
				      &val);
		if (!ret) {
			cr2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR2);
			cr2 |= STM32F7_I2C_CR2_NBYTES(1);
			writel_relaxed(cr2, i2c_dev->base + STM32F7_I2C_CR2);
		} else {
			mask = STM32F7_I2C_CR2_NACK;
			stm32f7_i2c_set_bits(base + STM32F7_I2C_CR2, mask);
		}
	}

	/* NACK received */
	if (status & STM32F7_I2C_ISR_NACKF) {
		dev_dbg(i2c_dev->dev, "<%s>: Receive NACK\n", __func__);
		writel_relaxed(STM32F7_I2C_ICR_NACKCF, base + STM32F7_I2C_ICR);
	}

	/* STOP received */
	if (status & STM32F7_I2C_ISR_STOPF) {
		/* Disable interrupts */
		stm32f7_i2c_disable_irq(i2c_dev, STM32F7_I2C_XFER_IRQ_MASK);

		if (i2c_dev->slave_dir) {
			/*
			 * Flush TX buffer in order to not used the byte in
			 * TXDR for the next transfer
			 */
			mask = STM32F7_I2C_ISR_TXE;
			stm32f7_i2c_set_bits(base + STM32F7_I2C_ISR, mask);
		}

		/* Clear STOP flag */
		writel_relaxed(STM32F7_I2C_ICR_STOPCF, base + STM32F7_I2C_ICR);

		/* Notify i2c slave that a STOP flag has been detected */
		i2c_slave_event(i2c_dev->slave_running, I2C_SLAVE_STOP, &val);

		i2c_dev->slave_running = NULL;
	}

	/* Address match received */
	if (status & STM32F7_I2C_ISR_ADDR)
		stm32f7_i2c_slave_addr(i2c_dev);

	return IRQ_HANDLED;
}

static irqreturn_t stm32f7_i2c_isr_event(int irq, void *data)
{
	struct stm32f7_i2c_dev *i2c_dev = data;
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	void __iomem *base = i2c_dev->base;
	u32 status, mask;
	int ret = IRQ_HANDLED;

	/* Check if the interrupt if for a slave device */
	if (!i2c_dev->master_mode) {
		ret = stm32f7_i2c_slave_isr_event(i2c_dev);
		return ret;
	}

	status = readl_relaxed(i2c_dev->base + STM32F7_I2C_ISR);

	/* Tx empty */
	if (status & STM32F7_I2C_ISR_TXIS)
		stm32f7_i2c_write_tx_data(i2c_dev);

	/* RX not empty */
	if (status & STM32F7_I2C_ISR_RXNE)
		stm32f7_i2c_read_rx_data(i2c_dev);

	/* NACK received */
	if (status & STM32F7_I2C_ISR_NACKF) {
		dev_dbg(i2c_dev->dev, "<%s>: Receive NACK (addr %x)\n",
			__func__, f7_msg->addr);
		writel_relaxed(STM32F7_I2C_ICR_NACKCF, base + STM32F7_I2C_ICR);
		if (i2c_dev->use_dma) {
			stm32f7_i2c_disable_dma_req(i2c_dev);
			dmaengine_terminate_async(dma->chan_using);
		}
		f7_msg->result = -ENXIO;
	}

	/* STOP detection flag */
	if (status & STM32F7_I2C_ISR_STOPF) {
		/* Disable interrupts */
		if (stm32f7_i2c_is_slave_registered(i2c_dev))
			mask = STM32F7_I2C_XFER_IRQ_MASK;
		else
			mask = STM32F7_I2C_ALL_IRQ_MASK;
		stm32f7_i2c_disable_irq(i2c_dev, mask);

		/* Clear STOP flag */
		writel_relaxed(STM32F7_I2C_ICR_STOPCF, base + STM32F7_I2C_ICR);

		if (i2c_dev->use_dma && !f7_msg->result) {
			ret = IRQ_WAKE_THREAD;
		} else {
			i2c_dev->master_mode = false;
			complete(&i2c_dev->complete);
		}
	}

	/* Transfer complete */
	if (status & STM32F7_I2C_ISR_TC) {
		if (f7_msg->stop) {
			mask = STM32F7_I2C_CR2_STOP;
			stm32f7_i2c_set_bits(base + STM32F7_I2C_CR2, mask);
		} else if (i2c_dev->use_dma && !f7_msg->result) {
			ret = IRQ_WAKE_THREAD;
		} else if (f7_msg->smbus) {
			stm32f7_i2c_smbus_rep_start(i2c_dev);
		} else {
			i2c_dev->msg_id++;
			i2c_dev->msg++;
			stm32f7_i2c_xfer_msg(i2c_dev, i2c_dev->msg);
		}
	}

	if (status & STM32F7_I2C_ISR_TCR) {
		if (f7_msg->smbus)
			stm32f7_i2c_smbus_reload(i2c_dev);
		else
			stm32f7_i2c_reload(i2c_dev);
	}

	return ret;
}

static irqreturn_t stm32f7_i2c_isr_event_thread(int irq, void *data)
{
	struct stm32f7_i2c_dev *i2c_dev = data;
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	u32 status;
	int ret;

	/*
	 * Wait for dma transfer completion before sending next message or
	 * notity the end of xfer to the client
	 */
	ret = wait_for_completion_timeout(&i2c_dev->dma->dma_complete, HZ);
	if (!ret) {
		dev_dbg(i2c_dev->dev, "<%s>: Timed out\n", __func__);
		stm32f7_i2c_disable_dma_req(i2c_dev);
		dmaengine_terminate_async(dma->chan_using);
		f7_msg->result = -ETIMEDOUT;
	}

	status = readl_relaxed(i2c_dev->base + STM32F7_I2C_ISR);

	if (status & STM32F7_I2C_ISR_TC) {
		if (f7_msg->smbus) {
			stm32f7_i2c_smbus_rep_start(i2c_dev);
		} else {
			i2c_dev->msg_id++;
			i2c_dev->msg++;
			stm32f7_i2c_xfer_msg(i2c_dev, i2c_dev->msg);
		}
	} else {
		i2c_dev->master_mode = false;
		complete(&i2c_dev->complete);
	}

	return IRQ_HANDLED;
}

static irqreturn_t stm32f7_i2c_isr_error(int irq, void *data)
{
	struct stm32f7_i2c_dev *i2c_dev = data;
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	void __iomem *base = i2c_dev->base;
	struct device *dev = i2c_dev->dev;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	u32 status;

	status = readl_relaxed(i2c_dev->base + STM32F7_I2C_ISR);

	/* Bus error */
	if (status & STM32F7_I2C_ISR_BERR) {
		dev_err(dev, "<%s>: Bus error accessing addr 0x%x\n",
			__func__, f7_msg->addr);
		writel_relaxed(STM32F7_I2C_ICR_BERRCF, base + STM32F7_I2C_ICR);
		stm32f7_i2c_release_bus(&i2c_dev->adap);
		f7_msg->result = -EIO;
	}

	/* Arbitration loss */
	if (status & STM32F7_I2C_ISR_ARLO) {
		dev_dbg(dev, "<%s>: Arbitration loss accessing addr 0x%x\n",
			__func__, f7_msg->addr);
		writel_relaxed(STM32F7_I2C_ICR_ARLOCF, base + STM32F7_I2C_ICR);
		f7_msg->result = -EAGAIN;
	}

	if (status & STM32F7_I2C_ISR_PECERR) {
		dev_err(dev, "<%s>: PEC error in reception accessing addr 0x%x\n",
			__func__, f7_msg->addr);
		writel_relaxed(STM32F7_I2C_ICR_PECCF, base + STM32F7_I2C_ICR);
		f7_msg->result = -EINVAL;
	}

	if (status & STM32F7_I2C_ISR_ALERT) {
		dev_dbg(dev, "<%s>: SMBus alert received\n", __func__);
		writel_relaxed(STM32F7_I2C_ICR_ALERTCF, base + STM32F7_I2C_ICR);
		i2c_handle_smbus_alert(i2c_dev->alert->ara);
		return IRQ_HANDLED;
	}

	if (!i2c_dev->slave_running) {
		u32 mask;
		/* Disable interrupts */
		if (stm32f7_i2c_is_slave_registered(i2c_dev))
			mask = STM32F7_I2C_XFER_IRQ_MASK;
		else
			mask = STM32F7_I2C_ALL_IRQ_MASK;
		stm32f7_i2c_disable_irq(i2c_dev, mask);
	}

	/* Disable dma */
	if (i2c_dev->use_dma) {
		stm32f7_i2c_disable_dma_req(i2c_dev);
		dmaengine_terminate_async(dma->chan_using);
	}

	i2c_dev->master_mode = false;
	complete(&i2c_dev->complete);

	return IRQ_HANDLED;
}

static int stm32f7_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg msgs[], int num)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(i2c_adap);
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	unsigned long time_left;
	int ret;

	i2c_dev->msg = msgs;
	i2c_dev->msg_num = num;
	i2c_dev->msg_id = 0;
	f7_msg->smbus = false;

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	ret = stm32f7_i2c_wait_free_bus(i2c_dev);
	if (ret)
		goto pm_free;

	stm32f7_i2c_xfer_msg(i2c_dev, msgs);

	time_left = wait_for_completion_timeout(&i2c_dev->complete,
						i2c_dev->adap.timeout);
	ret = f7_msg->result;
	if (ret) {
		if (i2c_dev->use_dma)
			dmaengine_synchronize(dma->chan_using);

		/*
		 * It is possible that some unsent data have already been
		 * written into TXDR. To avoid sending old data in a
		 * further transfer, flush TXDR in case of any error
		 */
		writel_relaxed(STM32F7_I2C_ISR_TXE,
			       i2c_dev->base + STM32F7_I2C_ISR);
		goto pm_free;
	}

	if (!time_left) {
		dev_dbg(i2c_dev->dev, "Access to slave 0x%x timed out\n",
			i2c_dev->msg->addr);
		if (i2c_dev->use_dma)
			dmaengine_terminate_sync(dma->chan_using);
		stm32f7_i2c_wait_free_bus(i2c_dev);
		ret = -ETIMEDOUT;
	}

pm_free:
	pm_runtime_mark_last_busy(i2c_dev->dev);
	pm_runtime_put_autosuspend(i2c_dev->dev);

	return (ret < 0) ? ret : num;
}

static int stm32f7_i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
				  unsigned short flags, char read_write,
				  u8 command, int size,
				  union i2c_smbus_data *data)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(adapter);
	struct stm32f7_i2c_msg *f7_msg = &i2c_dev->f7_msg;
	struct stm32_i2c_dma *dma = i2c_dev->dma;
	struct device *dev = i2c_dev->dev;
	unsigned long timeout;
	int i, ret;

	f7_msg->addr = addr;
	f7_msg->size = size;
	f7_msg->read_write = read_write;
	f7_msg->smbus = true;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = stm32f7_i2c_wait_free_bus(i2c_dev);
	if (ret)
		goto pm_free;

	ret = stm32f7_i2c_smbus_xfer_msg(i2c_dev, flags, command, data);
	if (ret)
		goto pm_free;

	timeout = wait_for_completion_timeout(&i2c_dev->complete,
					      i2c_dev->adap.timeout);
	ret = f7_msg->result;
	if (ret) {
		if (i2c_dev->use_dma)
			dmaengine_synchronize(dma->chan_using);

		/*
		 * It is possible that some unsent data have already been
		 * written into TXDR. To avoid sending old data in a
		 * further transfer, flush TXDR in case of any error
		 */
		writel_relaxed(STM32F7_I2C_ISR_TXE,
			       i2c_dev->base + STM32F7_I2C_ISR);
		goto pm_free;
	}

	if (!timeout) {
		dev_dbg(dev, "Access to slave 0x%x timed out\n", f7_msg->addr);
		if (i2c_dev->use_dma)
			dmaengine_terminate_sync(dma->chan_using);
		stm32f7_i2c_wait_free_bus(i2c_dev);
		ret = -ETIMEDOUT;
		goto pm_free;
	}

	/* Check PEC */
	if ((flags & I2C_CLIENT_PEC) && size != I2C_SMBUS_QUICK && read_write) {
		ret = stm32f7_i2c_smbus_check_pec(i2c_dev);
		if (ret)
			goto pm_free;
	}

	if (read_write && size != I2C_SMBUS_QUICK) {
		switch (size) {
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			data->byte = f7_msg->smbus_buf[0];
		break;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			data->word = f7_msg->smbus_buf[0] |
				(f7_msg->smbus_buf[1] << 8);
		break;
		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
		for (i = 0; i <= f7_msg->smbus_buf[0]; i++)
			data->block[i] = f7_msg->smbus_buf[i];
		break;
		default:
			dev_err(dev, "Unsupported smbus transaction\n");
			ret = -EINVAL;
		}
	}

pm_free:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static void stm32f7_i2c_enable_wakeup(struct stm32f7_i2c_dev *i2c_dev,
				      bool enable)
{
	void __iomem *base = i2c_dev->base;
	u32 mask = STM32F7_I2C_CR1_WUPEN;

	if (!i2c_dev->wakeup_src)
		return;

	if (enable) {
		device_set_wakeup_enable(i2c_dev->dev, true);
		stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, mask);
	} else {
		device_set_wakeup_enable(i2c_dev->dev, false);
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR1, mask);
	}
}

static int stm32f7_i2c_reg_slave(struct i2c_client *slave)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(slave->adapter);
	void __iomem *base = i2c_dev->base;
	struct device *dev = i2c_dev->dev;
	u32 oar1, oar2, mask;
	int id, ret;

	if (slave->flags & I2C_CLIENT_PEC) {
		dev_err(dev, "SMBus PEC not supported in slave mode\n");
		return -EINVAL;
	}

	if (stm32f7_i2c_is_slave_busy(i2c_dev)) {
		dev_err(dev, "Too much slave registered\n");
		return -EBUSY;
	}

	ret = stm32f7_i2c_get_free_slave_id(i2c_dev, slave, &id);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (!stm32f7_i2c_is_slave_registered(i2c_dev))
		stm32f7_i2c_enable_wakeup(i2c_dev, true);

	switch (id) {
	case 0:
		/* Slave SMBus Host */
		i2c_dev->slave[id] = slave;
		break;

	case 1:
		/* Configure Own Address 1 */
		oar1 = readl_relaxed(i2c_dev->base + STM32F7_I2C_OAR1);
		oar1 &= ~STM32F7_I2C_OAR1_MASK;
		if (slave->flags & I2C_CLIENT_TEN) {
			oar1 |= STM32F7_I2C_OAR1_OA1_10(slave->addr);
			oar1 |= STM32F7_I2C_OAR1_OA1MODE;
		} else {
			oar1 |= STM32F7_I2C_OAR1_OA1_7(slave->addr);
		}
		oar1 |= STM32F7_I2C_OAR1_OA1EN;
		i2c_dev->slave[id] = slave;
		writel_relaxed(oar1, i2c_dev->base + STM32F7_I2C_OAR1);
		break;

	case 2:
		/* Configure Own Address 2 */
		oar2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_OAR2);
		oar2 &= ~STM32F7_I2C_OAR2_MASK;
		if (slave->flags & I2C_CLIENT_TEN) {
			ret = -EOPNOTSUPP;
			goto pm_free;
		}

		oar2 |= STM32F7_I2C_OAR2_OA2_7(slave->addr);
		oar2 |= STM32F7_I2C_OAR2_OA2EN;
		i2c_dev->slave[id] = slave;
		writel_relaxed(oar2, i2c_dev->base + STM32F7_I2C_OAR2);
		break;

	default:
		dev_err(dev, "I2C slave id not supported\n");
		ret = -ENODEV;
		goto pm_free;
	}

	/* Enable ACK */
	stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR2, STM32F7_I2C_CR2_NACK);

	/* Enable Address match interrupt, error interrupt and enable I2C  */
	mask = STM32F7_I2C_CR1_ADDRIE | STM32F7_I2C_CR1_ERRIE |
		STM32F7_I2C_CR1_PE;
	stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, mask);

	ret = 0;
pm_free:
	if (!stm32f7_i2c_is_slave_registered(i2c_dev))
		stm32f7_i2c_enable_wakeup(i2c_dev, false);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int stm32f7_i2c_unreg_slave(struct i2c_client *slave)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(slave->adapter);
	void __iomem *base = i2c_dev->base;
	u32 mask;
	int id, ret;

	ret = stm32f7_i2c_get_slave_id(i2c_dev, slave, &id);
	if (ret)
		return ret;

	WARN_ON(!i2c_dev->slave[id]);

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	if (id == 1) {
		mask = STM32F7_I2C_OAR1_OA1EN;
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_OAR1, mask);
	} else if (id == 2) {
		mask = STM32F7_I2C_OAR2_OA2EN;
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_OAR2, mask);
	}

	i2c_dev->slave[id] = NULL;

	if (!stm32f7_i2c_is_slave_registered(i2c_dev)) {
		stm32f7_i2c_disable_irq(i2c_dev, STM32F7_I2C_ALL_IRQ_MASK);
		stm32f7_i2c_enable_wakeup(i2c_dev, false);
	}

	pm_runtime_mark_last_busy(i2c_dev->dev);
	pm_runtime_put_autosuspend(i2c_dev->dev);

	return 0;
}

static int stm32f7_i2c_write_fm_plus_bits(struct stm32f7_i2c_dev *i2c_dev,
					  bool enable)
{
	int ret;

	if (i2c_dev->bus_rate <= I2C_MAX_FAST_MODE_FREQ ||
	    IS_ERR_OR_NULL(i2c_dev->regmap))
		/* Optional */
		return 0;

	if (i2c_dev->fmp_sreg == i2c_dev->fmp_creg)
		ret = regmap_update_bits(i2c_dev->regmap,
					 i2c_dev->fmp_sreg,
					 i2c_dev->fmp_mask,
					 enable ? i2c_dev->fmp_mask : 0);
	else
		ret = regmap_write(i2c_dev->regmap,
				   enable ? i2c_dev->fmp_sreg :
					    i2c_dev->fmp_creg,
				   i2c_dev->fmp_mask);

	return ret;
}

static int stm32f7_i2c_setup_fm_plus_bits(struct platform_device *pdev,
					  struct stm32f7_i2c_dev *i2c_dev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	i2c_dev->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg-fmp");
	if (IS_ERR(i2c_dev->regmap))
		/* Optional */
		return 0;

	ret = of_property_read_u32_index(np, "st,syscfg-fmp", 1,
					 &i2c_dev->fmp_sreg);
	if (ret)
		return ret;

	i2c_dev->fmp_creg = i2c_dev->fmp_sreg +
			       i2c_dev->setup.fmp_clr_offset;

	return of_property_read_u32_index(np, "st,syscfg-fmp", 2,
					  &i2c_dev->fmp_mask);
}

static int stm32f7_i2c_enable_smbus_host(struct stm32f7_i2c_dev *i2c_dev)
{
	struct i2c_adapter *adap = &i2c_dev->adap;
	void __iomem *base = i2c_dev->base;
	struct i2c_client *client;

	client = i2c_new_slave_host_notify_device(adap);
	if (IS_ERR(client))
		return PTR_ERR(client);

	i2c_dev->host_notify_client = client;

	/* Enable SMBus Host address */
	stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, STM32F7_I2C_CR1_SMBHEN);

	return 0;
}

static void stm32f7_i2c_disable_smbus_host(struct stm32f7_i2c_dev *i2c_dev)
{
	void __iomem *base = i2c_dev->base;

	if (i2c_dev->host_notify_client) {
		/* Disable SMBus Host address */
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_SMBHEN);
		i2c_free_slave_host_notify_device(i2c_dev->host_notify_client);
	}
}

static int stm32f7_i2c_enable_smbus_alert(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_alert *alert;
	struct i2c_adapter *adap = &i2c_dev->adap;
	struct device *dev = i2c_dev->dev;
	void __iomem *base = i2c_dev->base;

	alert = devm_kzalloc(dev, sizeof(*alert), GFP_KERNEL);
	if (!alert)
		return -ENOMEM;

	alert->ara = i2c_new_smbus_alert_device(adap, &alert->setup);
	if (IS_ERR(alert->ara))
		return PTR_ERR(alert->ara);

	i2c_dev->alert = alert;

	/* Enable SMBus Alert */
	stm32f7_i2c_set_bits(base + STM32F7_I2C_CR1, STM32F7_I2C_CR1_ALERTEN);

	return 0;
}

static void stm32f7_i2c_disable_smbus_alert(struct stm32f7_i2c_dev *i2c_dev)
{
	struct stm32f7_i2c_alert *alert = i2c_dev->alert;
	void __iomem *base = i2c_dev->base;

	if (alert) {
		/* Disable SMBus Alert */
		stm32f7_i2c_clr_bits(base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_ALERTEN);
		i2c_unregister_device(alert->ara);
	}
}

static u32 stm32f7_i2c_func(struct i2c_adapter *adap)
{
	struct stm32f7_i2c_dev *i2c_dev = i2c_get_adapdata(adap);

	u32 func = I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_SLAVE |
		   I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		   I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
		   I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		   I2C_FUNC_SMBUS_PROC_CALL | I2C_FUNC_SMBUS_PEC |
		   I2C_FUNC_SMBUS_I2C_BLOCK;

	if (i2c_dev->smbus_mode)
		func |= I2C_FUNC_SMBUS_HOST_NOTIFY;

	return func;
}

static const struct i2c_algorithm stm32f7_i2c_algo = {
	.master_xfer = stm32f7_i2c_xfer,
	.smbus_xfer = stm32f7_i2c_smbus_xfer,
	.functionality = stm32f7_i2c_func,
	.reg_slave = stm32f7_i2c_reg_slave,
	.unreg_slave = stm32f7_i2c_unreg_slave,
};

static int stm32f7_i2c_probe(struct platform_device *pdev)
{
	struct stm32f7_i2c_dev *i2c_dev;
	const struct stm32f7_i2c_setup *setup;
	struct resource *res;
	struct i2c_adapter *adap;
	struct reset_control *rst;
	dma_addr_t phy_addr;
	int irq_error, irq_event, ret;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);
	phy_addr = (dma_addr_t)res->start;

	irq_event = platform_get_irq(pdev, 0);
	if (irq_event <= 0)
		return irq_event ? : -ENOENT;

	irq_error = platform_get_irq(pdev, 1);
	if (irq_error <= 0)
		return irq_error ? : -ENOENT;

	i2c_dev->wakeup_src = of_property_read_bool(pdev->dev.of_node,
						    "wakeup-source");

	i2c_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c_dev->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c_dev->clk),
				     "Failed to get controller clock\n");

	ret = clk_prepare_enable(i2c_dev->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare_enable clock\n");
		return ret;
	}

	rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(rst),
				    "Error: Missing reset ctrl\n");
		goto clk_free;
	}
	reset_control_assert(rst);
	udelay(2);
	reset_control_deassert(rst);

	i2c_dev->dev = &pdev->dev;

	ret = devm_request_threaded_irq(&pdev->dev, irq_event,
					stm32f7_i2c_isr_event,
					stm32f7_i2c_isr_event_thread,
					IRQF_ONESHOT,
					pdev->name, i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq event %i\n",
			irq_event);
		goto clk_free;
	}

	ret = devm_request_irq(&pdev->dev, irq_error, stm32f7_i2c_isr_error, 0,
			       pdev->name, i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq error %i\n",
			irq_error);
		goto clk_free;
	}

	setup = of_device_get_match_data(&pdev->dev);
	if (!setup) {
		dev_err(&pdev->dev, "Can't get device data\n");
		ret = -ENODEV;
		goto clk_free;
	}
	i2c_dev->setup = *setup;

	ret = stm32f7_i2c_setup_timing(i2c_dev, &i2c_dev->setup);
	if (ret)
		goto clk_free;

	/* Setup Fast mode plus if necessary */
	if (i2c_dev->bus_rate > I2C_MAX_FAST_MODE_FREQ) {
		ret = stm32f7_i2c_setup_fm_plus_bits(pdev, i2c_dev);
		if (ret)
			goto clk_free;
		ret = stm32f7_i2c_write_fm_plus_bits(i2c_dev, true);
		if (ret)
			goto clk_free;
	}

	adap = &i2c_dev->adap;
	i2c_set_adapdata(adap, i2c_dev);
	snprintf(adap->name, sizeof(adap->name), "STM32F7 I2C(%pa)",
		 &res->start);
	adap->owner = THIS_MODULE;
	adap->timeout = 2 * HZ;
	adap->retries = 3;
	adap->algo = &stm32f7_i2c_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	init_completion(&i2c_dev->complete);

	/* Init DMA config if supported */
	i2c_dev->dma = stm32_i2c_dma_request(i2c_dev->dev, phy_addr,
					     STM32F7_I2C_TXDR,
					     STM32F7_I2C_RXDR);
	if (IS_ERR(i2c_dev->dma)) {
		ret = PTR_ERR(i2c_dev->dma);
		/* DMA support is optional, only report other errors */
		if (ret != -ENODEV)
			goto fmp_clear;
		dev_dbg(i2c_dev->dev, "No DMA option: fallback using interrupts\n");
		i2c_dev->dma = NULL;
	}

	if (i2c_dev->wakeup_src) {
		device_set_wakeup_capable(i2c_dev->dev, true);

		ret = dev_pm_set_wake_irq(i2c_dev->dev, irq_event);
		if (ret) {
			dev_err(i2c_dev->dev, "Failed to set wake up irq\n");
			goto clr_wakeup_capable;
		}
	}

	platform_set_drvdata(pdev, i2c_dev);

	pm_runtime_set_autosuspend_delay(i2c_dev->dev,
					 STM32F7_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(i2c_dev->dev);
	pm_runtime_set_active(i2c_dev->dev);
	pm_runtime_enable(i2c_dev->dev);

	pm_runtime_get_noresume(&pdev->dev);

	stm32f7_i2c_hw_config(i2c_dev);

	i2c_dev->smbus_mode = of_property_read_bool(pdev->dev.of_node, "smbus");

	ret = i2c_add_adapter(adap);
	if (ret)
		goto pm_disable;

	if (i2c_dev->smbus_mode) {
		ret = stm32f7_i2c_enable_smbus_host(i2c_dev);
		if (ret) {
			dev_err(i2c_dev->dev,
				"failed to enable SMBus Host-Notify protocol (%d)\n",
				ret);
			goto i2c_adapter_remove;
		}
	}

	if (of_property_read_bool(pdev->dev.of_node, "smbus-alert")) {
		ret = stm32f7_i2c_enable_smbus_alert(i2c_dev);
		if (ret) {
			dev_err(i2c_dev->dev,
				"failed to enable SMBus alert protocol (%d)\n",
				ret);
			goto i2c_disable_smbus_host;
		}
	}

	dev_info(i2c_dev->dev, "STM32F7 I2C-%d bus adapter\n", adap->nr);

	pm_runtime_mark_last_busy(i2c_dev->dev);
	pm_runtime_put_autosuspend(i2c_dev->dev);

	return 0;

i2c_disable_smbus_host:
	stm32f7_i2c_disable_smbus_host(i2c_dev);

i2c_adapter_remove:
	i2c_del_adapter(adap);

pm_disable:
	pm_runtime_put_noidle(i2c_dev->dev);
	pm_runtime_disable(i2c_dev->dev);
	pm_runtime_set_suspended(i2c_dev->dev);
	pm_runtime_dont_use_autosuspend(i2c_dev->dev);

	if (i2c_dev->wakeup_src)
		dev_pm_clear_wake_irq(i2c_dev->dev);

clr_wakeup_capable:
	if (i2c_dev->wakeup_src)
		device_set_wakeup_capable(i2c_dev->dev, false);

	if (i2c_dev->dma) {
		stm32_i2c_dma_free(i2c_dev->dma);
		i2c_dev->dma = NULL;
	}

fmp_clear:
	stm32f7_i2c_write_fm_plus_bits(i2c_dev, false);

clk_free:
	clk_disable_unprepare(i2c_dev->clk);

	return ret;
}

static int stm32f7_i2c_remove(struct platform_device *pdev)
{
	struct stm32f7_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	stm32f7_i2c_disable_smbus_alert(i2c_dev);
	stm32f7_i2c_disable_smbus_host(i2c_dev);

	i2c_del_adapter(&i2c_dev->adap);
	pm_runtime_get_sync(i2c_dev->dev);

	if (i2c_dev->wakeup_src) {
		dev_pm_clear_wake_irq(i2c_dev->dev);
		/*
		 * enforce that wakeup is disabled and that the device
		 * is marked as non wakeup capable
		 */
		device_init_wakeup(i2c_dev->dev, false);
	}

	pm_runtime_put_noidle(i2c_dev->dev);
	pm_runtime_disable(i2c_dev->dev);
	pm_runtime_set_suspended(i2c_dev->dev);
	pm_runtime_dont_use_autosuspend(i2c_dev->dev);

	if (i2c_dev->dma) {
		stm32_i2c_dma_free(i2c_dev->dma);
		i2c_dev->dma = NULL;
	}

	stm32f7_i2c_write_fm_plus_bits(i2c_dev, false);

	clk_disable_unprepare(i2c_dev->clk);

	return 0;
}

static int __maybe_unused stm32f7_i2c_runtime_suspend(struct device *dev)
{
	struct stm32f7_i2c_dev *i2c_dev = dev_get_drvdata(dev);

	if (!stm32f7_i2c_is_slave_registered(i2c_dev))
		clk_disable(i2c_dev->clk);

	return 0;
}

static int __maybe_unused stm32f7_i2c_runtime_resume(struct device *dev)
{
	struct stm32f7_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int ret;

	if (!stm32f7_i2c_is_slave_registered(i2c_dev)) {
		ret = clk_enable(i2c_dev->clk);
		if (ret) {
			dev_err(dev, "failed to enable clock\n");
			return ret;
		}
	}

	return 0;
}

static int __maybe_unused stm32f7_i2c_regs_backup(struct stm32f7_i2c_dev *i2c_dev)
{
	int ret;
	struct stm32f7_i2c_regs *backup_regs = &i2c_dev->backup_regs;

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	backup_regs->cr1 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR1);
	backup_regs->cr2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR2);
	backup_regs->oar1 = readl_relaxed(i2c_dev->base + STM32F7_I2C_OAR1);
	backup_regs->oar2 = readl_relaxed(i2c_dev->base + STM32F7_I2C_OAR2);
	backup_regs->tmgr = readl_relaxed(i2c_dev->base + STM32F7_I2C_TIMINGR);
	stm32f7_i2c_write_fm_plus_bits(i2c_dev, false);

	pm_runtime_put_sync(i2c_dev->dev);

	return ret;
}

static int __maybe_unused stm32f7_i2c_regs_restore(struct stm32f7_i2c_dev *i2c_dev)
{
	u32 cr1;
	int ret;
	struct stm32f7_i2c_regs *backup_regs = &i2c_dev->backup_regs;

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	cr1 = readl_relaxed(i2c_dev->base + STM32F7_I2C_CR1);
	if (cr1 & STM32F7_I2C_CR1_PE)
		stm32f7_i2c_clr_bits(i2c_dev->base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_PE);

	writel_relaxed(backup_regs->tmgr, i2c_dev->base + STM32F7_I2C_TIMINGR);
	writel_relaxed(backup_regs->cr1 & ~STM32F7_I2C_CR1_PE,
		       i2c_dev->base + STM32F7_I2C_CR1);
	if (backup_regs->cr1 & STM32F7_I2C_CR1_PE)
		stm32f7_i2c_set_bits(i2c_dev->base + STM32F7_I2C_CR1,
				     STM32F7_I2C_CR1_PE);
	writel_relaxed(backup_regs->cr2, i2c_dev->base + STM32F7_I2C_CR2);
	writel_relaxed(backup_regs->oar1, i2c_dev->base + STM32F7_I2C_OAR1);
	writel_relaxed(backup_regs->oar2, i2c_dev->base + STM32F7_I2C_OAR2);
	stm32f7_i2c_write_fm_plus_bits(i2c_dev, true);

	pm_runtime_put_sync(i2c_dev->dev);

	return ret;
}

static int __maybe_unused stm32f7_i2c_suspend(struct device *dev)
{
	struct stm32f7_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int ret;

	i2c_mark_adapter_suspended(&i2c_dev->adap);

	if (!device_may_wakeup(dev) && !device_wakeup_path(dev)) {
		ret = stm32f7_i2c_regs_backup(i2c_dev);
		if (ret < 0) {
			i2c_mark_adapter_resumed(&i2c_dev->adap);
			return ret;
		}

		pinctrl_pm_select_sleep_state(dev);
		pm_runtime_force_suspend(dev);
	}

	return 0;
}

static int __maybe_unused stm32f7_i2c_resume(struct device *dev)
{
	struct stm32f7_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int ret;

	if (!device_may_wakeup(dev) && !device_wakeup_path(dev)) {
		ret = pm_runtime_force_resume(dev);
		if (ret < 0)
			return ret;
		pinctrl_pm_select_default_state(dev);

		ret = stm32f7_i2c_regs_restore(i2c_dev);
		if (ret < 0)
			return ret;
	}

	i2c_mark_adapter_resumed(&i2c_dev->adap);

	return 0;
}

static const struct dev_pm_ops stm32f7_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32f7_i2c_runtime_suspend,
			   stm32f7_i2c_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(stm32f7_i2c_suspend, stm32f7_i2c_resume)
};

static const struct of_device_id stm32f7_i2c_match[] = {
	{ .compatible = "st,stm32f7-i2c", .data = &stm32f7_setup},
	{ .compatible = "st,stm32mp15-i2c", .data = &stm32mp15_setup},
	{ .compatible = "st,stm32mp13-i2c", .data = &stm32mp13_setup},
	{},
};
MODULE_DEVICE_TABLE(of, stm32f7_i2c_match);

static struct platform_driver stm32f7_i2c_driver = {
	.driver = {
		.name = "stm32f7-i2c",
		.of_match_table = stm32f7_i2c_match,
		.pm = &stm32f7_i2c_pm_ops,
	},
	.probe = stm32f7_i2c_probe,
	.remove = stm32f7_i2c_remove,
};

module_platform_driver(stm32f7_i2c_driver);

MODULE_AUTHOR("M'boumba Cedric Madianga <cedric.madianga@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32F7 I2C driver");
MODULE_LICENSE("GPL v2");
