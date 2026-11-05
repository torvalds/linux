// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD9910 SPI DDS (Direct Digital Synthesizer) driver
 *
 * Copyright 2026 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device-id/of.h>
#include <linux/device-id/spi.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Register addresses */
#define AD9910_REG_CFR1			0x00
#define AD9910_REG_CFR2			0x01
#define AD9910_REG_CFR3			0x02
#define AD9910_REG_AUX_DAC		0x03
#define AD9910_REG_IO_UPDATE_RATE	0x04
#define AD9910_REG_FTW			0x07
#define AD9910_REG_POW			0x08
#define AD9910_REG_ASF			0x09
#define AD9910_REG_MULTICHIP_SYNC	0x0A
#define AD9910_REG_DRG_LIMIT		0x0B
#define AD9910_REG_DRG_STEP		0x0C
#define AD9910_REG_DRG_RATE		0x0D
#define AD9910_REG_PROFILE0		0x0E
#define AD9910_REG_PROFILE1		0x0F
#define AD9910_REG_PROFILE2		0x10
#define AD9910_REG_PROFILE3		0x11
#define AD9910_REG_PROFILE4		0x12
#define AD9910_REG_PROFILE5		0x13
#define AD9910_REG_PROFILE6		0x14
#define AD9910_REG_PROFILE7		0x15
#define AD9910_REG_RAM			0x16

#define AD9910_REG_NUM_CACHED		0x16
#define AD9910_REG_PROFILE(x)		(AD9910_REG_PROFILE0 + (x))

/* CFR1 bit definitions */
#define AD9910_CFR1_RAM_ENABLE_MSK		BIT(31)
#define AD9910_CFR1_RAM_PLAYBACK_DEST_MSK	GENMASK(30, 29)
#define AD9910_CFR1_OSK_MANUAL_EXT_CTL_MSK	BIT(23)
#define AD9910_CFR1_INV_SINC_EN_MSK		BIT(22)
#define AD9910_CFR1_INT_PROFILE_CTL_MSK		GENMASK(20, 17)
#define AD9910_CFR1_SELECT_SINE_MSK		BIT(16)
#define AD9910_CFR1_LOAD_LRR_IO_UPDATE_MSK	BIT(15)
#define AD9910_CFR1_AUTOCLR_DIG_RAMP_ACCUM_MSK	BIT(14)
#define AD9910_CFR1_AUTOCLR_PHASE_ACCUM_MSK	BIT(13)
#define AD9910_CFR1_CLEAR_DIG_RAMP_ACCUM_MSK	BIT(12)
#define AD9910_CFR1_CLEAR_PHASE_ACCUM_MSK	BIT(11)
#define AD9910_CFR1_LOAD_ARR_IO_UPDATE_MSK	BIT(10)
#define AD9910_CFR1_OSK_ENABLE_MSK		BIT(9)
#define AD9910_CFR1_SELECT_AUTO_OSK_MSK		BIT(8)
#define AD9910_CFR1_DIGITAL_POWER_DOWN_MSK	BIT(7)
#define AD9910_CFR1_DAC_POWER_DOWN_MSK		BIT(6)
#define AD9910_CFR1_REFCLK_INPUT_POWER_DOWN_MSK	BIT(5)
#define AD9910_CFR1_AUX_DAC_POWER_DOWN_MSK	BIT(4)
#define AD9910_CFR1_EXT_POWER_DOWN_CTL_MSK	BIT(3)
#define AD9910_CFR1_SDIO_INPUT_ONLY_MSK		BIT(1)
#define AD9910_CFR1_LSB_FIRST_MSK		BIT(0)

/* DIGITAL and REFCLK_INPUT powerdown bits are effective without I/O update */
#define AD9910_CFR1_SW0_POWER_DOWN_MSK	(AD9910_CFR1_DIGITAL_POWER_DOWN_MSK | \
					 AD9910_CFR1_REFCLK_INPUT_POWER_DOWN_MSK)
/* DAC and AUX_DAC powerdown require I/O update */
#define AD9910_CFR1_SW1_POWER_DOWN_MSK	(AD9910_CFR1_DAC_POWER_DOWN_MSK | \
					 AD9910_CFR1_AUX_DAC_POWER_DOWN_MSK)

#define AD9910_CFR1_SW_POWER_DOWN_MSK	(AD9910_CFR1_SW0_POWER_DOWN_MSK | \
					 AD9910_CFR1_SW1_POWER_DOWN_MSK)

/* CFR2 bit definitions */
#define AD9910_CFR2_AMP_SCALE_SINGLE_TONE_MSK	BIT(24)
#define AD9910_CFR2_INTERNAL_IO_UPDATE_MSK	BIT(23)
#define AD9910_CFR2_SYNC_CLK_EN_MSK		BIT(22)
#define AD9910_CFR2_DRG_DEST_MSK		GENMASK(21, 20)
#define AD9910_CFR2_DRG_ENABLE_MSK		BIT(19)
#define AD9910_CFR2_DRG_NO_DWELL_HIGH_MSK	BIT(18)
#define AD9910_CFR2_DRG_NO_DWELL_LOW_MSK	BIT(17)
#define AD9910_CFR2_DRG_NO_DWELL_MSK		GENMASK(18, 17)
#define AD9910_CFR2_READ_EFFECTIVE_FTW_MSK	BIT(16)
#define AD9910_CFR2_IO_UPDATE_RATE_CTL_MSK	GENMASK(15, 14)
#define AD9910_CFR2_PDCLK_ENABLE_MSK		BIT(11)
#define AD9910_CFR2_PDCLK_INVERT_MSK		BIT(10)
#define AD9910_CFR2_TXENABLE_INVERT_MSK		BIT(9)
#define AD9910_CFR2_MATCHED_LATENCY_EN_MSK	BIT(7)
#define AD9910_CFR2_DATA_ASM_HOLD_LAST_MSK	BIT(6)
#define AD9910_CFR2_SYNC_TIMING_VAL_DISABLE_MSK	BIT(5)
#define AD9910_CFR2_PARALLEL_DATA_PORT_EN_MSK	BIT(4)
#define AD9910_CFR2_FM_GAIN_MSK			GENMASK(3, 0)

/* CFR3 bit definitions */
#define AD9910_CFR3_OPEN_MSK			(BIT(27) | GENMASK(18, 16))
#define AD9910_CFR3_DRV0_MSK			GENMASK(29, 28)
#define AD9910_CFR3_VCO_SEL_MSK			GENMASK(26, 24)
#define AD9910_CFR3_ICP_MSK			GENMASK(21, 19)
#define AD9910_CFR3_REFCLK_DIV_BYPASS_MSK	BIT(15)
#define AD9910_CFR3_REFCLK_DIV_RESETB_MSK	BIT(14)
#define AD9910_CFR3_PFD_RESET_MSK		BIT(10)
#define AD9910_CFR3_PLL_EN_MSK			BIT(8)
#define AD9910_CFR3_N_MSK			GENMASK(7, 1)

/* Auxiliary DAC Control Register Bits */
#define AD9910_AUX_DAC_FSC_MSK			GENMASK(7, 0)

/* POW Register Bits */
#define AD9910_POW_PP_LSB_MSK			GENMASK(7, 0)

/* ASF Register Bits */
#define AD9910_ASF_RAMP_RATE_MSK		GENMASK(31, 16)
#define AD9910_ASF_SCALE_FACTOR_MSK		GENMASK(15, 2)
#define AD9910_ASF_SCALE_FACTOR_PP_LSB_MSK	GENMASK(7, 2)
#define AD9910_ASF_STEP_SIZE_MSK		GENMASK(1, 0)

/* Multichip Sync Register Bits */
#define AD9910_MC_SYNC_VALIDATION_DELAY_MSK	GENMASK(31, 28)
#define AD9910_MC_SYNC_RECEIVER_ENABLE_MSK	BIT(27)
#define AD9910_MC_SYNC_GENERATOR_ENABLE_MSK	BIT(26)
#define AD9910_MC_SYNC_GENERATOR_POLARITY_MSK	BIT(25)
#define AD9910_MC_SYNC_STATE_PRESET_MSK		GENMASK(23, 18)
#define AD9910_MC_SYNC_OUTPUT_DELAY_MSK		GENMASK(15, 11)
#define AD9910_MC_SYNC_INPUT_DELAY_MSK		GENMASK(7, 3)

/* Digital Ramp Limit Register */
#define AD9910_DRG_LIMIT_UPPER_MSK		GENMASK_ULL(63, 32)
#define AD9910_DRG_LIMIT_LOWER_MSK		GENMASK_ULL(31, 0)

/* Digital Ramp Step Register */
#define AD9910_DRG_STEP_DEC_MSK			GENMASK_ULL(63, 32)
#define AD9910_DRG_STEP_INC_MSK			GENMASK_ULL(31, 0)

/* Digital Ramp Rate Register */
#define AD9910_DRG_RATE_DEC_MSK			GENMASK(31, 16)
#define AD9910_DRG_RATE_INC_MSK			GENMASK(15, 0)

/* Profile Register Format (Single Tone Mode) */
#define AD9910_PROFILE_ST_ASF_MSK		GENMASK_ULL(61, 48)
#define AD9910_PROFILE_ST_POW_MSK		GENMASK_ULL(47, 32)
#define AD9910_PROFILE_ST_FTW_MSK		GENMASK_ULL(31, 0)

/* Profile Register Format (RAM Mode) */
#define AD9910_PROFILE_RAM_OPEN_MSK		GENMASK_ULL(61, 56)
#define AD9910_PROFILE_RAM_STEP_RATE_MSK	GENMASK_ULL(55, 40)
#define AD9910_PROFILE_RAM_END_ADDR_MSK		GENMASK_ULL(39, 30)
#define AD9910_PROFILE_RAM_START_ADDR_MSK	GENMASK_ULL(23, 14)
#define AD9910_PROFILE_RAM_NO_DWELL_HIGH_MSK	BIT_ULL(5)
#define AD9910_PROFILE_RAM_ZERO_CROSSING_MSK	BIT_ULL(3)
#define AD9910_PROFILE_RAM_MODE_CONTROL_MSK	GENMASK_ULL(2, 0)

/* Device constants */
#define AD9910_PI_NANORAD		3141592653UL
#define AD9910_PI_PICORAD		3141592653590ULL

#define AD9910_MAX_SYSCLK_HZ		(1000 * HZ_PER_MHZ)
#define AD9910_MAX_PHASE_MICRORAD	(AD9910_PI_NANORAD / 500)

#define AD9910_ASF_MAX			FIELD_MAX(AD9910_PROFILE_ST_ASF_MSK)
#define AD9910_ASF_PP_LSB_MAX		FIELD_MAX(AD9910_ASF_SCALE_FACTOR_PP_LSB_MSK)
#define AD9910_POW_MAX			FIELD_MAX(AD9910_PROFILE_ST_POW_MSK)
#define AD9910_POW_PP_LSB_MAX		FIELD_MAX(AD9910_POW_PP_LSB_MSK)
#define AD9910_STEP_RATE_MAX		FIELD_MAX(AD9910_DRG_RATE_DEC_MSK)
#define AD9910_NUM_PROFILES		8

#define AD9910_RAM_FW_MAGIC		0x00AD9910
#define AD9910_RAM_FW_V1		0x0001
#define AD9910_RAM_SIZE_MAX_WORDS	1024
#define AD9910_RAM_WORD_SIZE		sizeof(u32)
#define AD9910_RAM_SIZE_MAX_BYTES	(AD9910_RAM_SIZE_MAX_WORDS * AD9910_RAM_WORD_SIZE)
#define AD9910_RAM_ADDR_MAX		(AD9910_RAM_SIZE_MAX_WORDS - 1)

#define AD9910_RAM_ENABLED(st)		\
	FIELD_GET(AD9910_CFR1_RAM_ENABLE_MSK, (st)->reg[AD9910_REG_CFR1].val32)

#define AD9910_DEST_FREQUENCY		0
#define AD9910_DEST_PHASE		1
#define AD9910_DEST_AMPLITUDE		2
#define AD9910_DEST_POLAR		3

/* PLL constants */
#define AD9910_PLL_MIN_N		12
#define AD9910_PLL_MAX_N		127

#define AD9910_PLL_IN_MIN_FREQ_HZ	(3307 * HZ_PER_KHZ)
#define AD9910_PLL_IN_MAX_FREQ_HZ	(60 * HZ_PER_MHZ)

#define AD9910_PLL_OUT_MIN_FREQ_HZ	(420 * HZ_PER_MHZ)
#define AD9910_PLL_OUT_MAX_FREQ_HZ	AD9910_MAX_SYSCLK_HZ

#define AD9910_VCO0_RANGE_AUTO_MAX_HZ	(457 * HZ_PER_MHZ)
#define AD9910_VCO1_RANGE_AUTO_MAX_HZ	(530 * HZ_PER_MHZ)
#define AD9910_VCO2_RANGE_AUTO_MAX_HZ	(632 * HZ_PER_MHZ)
#define AD9910_VCO3_RANGE_AUTO_MAX_HZ	(775 * HZ_PER_MHZ)
#define AD9910_VCO4_RANGE_AUTO_MAX_HZ	(897 * HZ_PER_MHZ)
#define AD9910_VCO_RANGE_NUM		6

#define AD9910_ICP_MIN_uA		212
#define AD9910_ICP_MAX_uA		387
#define AD9910_ICP_STEP_uA		25

#define AD9910_DAC_IOUT_MAX_uA		31590
#define AD9910_DAC_IOUT_DEFAULT_uA	20070
#define AD9910_DAC_IOUT_MIN_uA		8640

/* altcurrent ABI is in mA */
#define AD9910_NANO_MILLIAMP_PER_MICROAMP	1000000UL
#define AD9910_PICO_MILLIAMP_PER_MICROAMP	1000000000UL

#define AD9910_REFDIV2_MIN_FREQ_HZ	(25 * HZ_PER_MHZ)
#define AD9910_REFDIV2_MAX_FREQ_HZ	(1900 * HZ_PER_MHZ)

#define AD9910_WAKEUP_DELAY_us		1000	/* 1ms: datasheet Table 1 */
#define AD9910_RESET_DELAY_us		1	/* 5 sysclk cycles: < 1us */

#define AD9910_SPI_DATA_IDX		1
#define AD9910_SPI_DATA_LEN_MAX		AD9910_RAM_SIZE_MAX_BYTES
#define AD9910_SPI_MESSAGE_LEN_MAX	(AD9910_SPI_DATA_IDX + AD9910_SPI_DATA_LEN_MAX)
#define AD9910_SPI_READ_MSK		BIT(7)
#define AD9910_SPI_ADDR_MSK		GENMASK(4, 0)

/**
 * enum ad9910_channel - AD9910 channel identifiers in priority order
 *
 * @AD9910_CHANNEL_PHY: Physical output channel
 * @AD9910_CHANNEL_PROFILE_0: Profile 0 output channel
 * @AD9910_CHANNEL_PROFILE_1: Profile 1 output channel
 * @AD9910_CHANNEL_PROFILE_2: Profile 2 output channel
 * @AD9910_CHANNEL_PROFILE_3: Profile 3 output channel
 * @AD9910_CHANNEL_PROFILE_4: Profile 4 output channel
 * @AD9910_CHANNEL_PROFILE_5: Profile 5 output channel
 * @AD9910_CHANNEL_PROFILE_6: Profile 6 output channel
 * @AD9910_CHANNEL_PROFILE_7: Profile 7 output channel
 * @AD9910_CHANNEL_PARALLEL: Parallel Data output channel
 * @AD9910_CHANNEL_PARALLEL_POLAR: Parallel Polar Data output channel
 * @AD9910_CHANNEL_DRG: Digital Ramp Generator output channel
 * @AD9910_CHANNEL_DRG_RAMP_UP: DRG ramp up channel
 * @AD9910_CHANNEL_DRG_RAMP_DOWN: DRG ramp down channel
 * @AD9910_CHANNEL_RAM: RAM control output channel
 * @AD9910_CHANNEL_OSK: Output Shift Keying output channel
 */
enum ad9910_channel {
	AD9910_CHANNEL_PHY = 100,
	AD9910_CHANNEL_PROFILE_0 = 110,
	AD9910_CHANNEL_PROFILE_1 = 111,
	AD9910_CHANNEL_PROFILE_2 = 112,
	AD9910_CHANNEL_PROFILE_3 = 113,
	AD9910_CHANNEL_PROFILE_4 = 114,
	AD9910_CHANNEL_PROFILE_5 = 115,
	AD9910_CHANNEL_PROFILE_6 = 116,
	AD9910_CHANNEL_PROFILE_7 = 117,
	AD9910_CHANNEL_PARALLEL = 120,
	AD9910_CHANNEL_PARALLEL_POLAR = 121,
	AD9910_CHANNEL_DRG = 130,
	AD9910_CHANNEL_DRG_RAMP_UP = 131,
	AD9910_CHANNEL_DRG_RAMP_DOWN = 132,
	AD9910_CHANNEL_RAM = 140,
	AD9910_CHANNEL_OSK = 150,
};

/**
 * struct ad9910_ram_fw - AD9910 RAM firmware format
 * @magic:	Magic number for RAM firmware validation
 * @version:	Firmware version number
 * @wcount:	Number of RAM words to be written
 * @crc:	CRC32 checksum of the RAM data for integrity verification
 * @cfr1:	Value of CFR1 register to be configured (not all fields are
 *		used, but this is included here for convenience)
 * @profiles:	Array of RAM profile configurations
 * @words:	Array of RAM words to be written. Data pattern should be set in
 *		reverse order and wcount specifies the number of words in this
 *		array
 */
struct ad9910_ram_fw {
	__be32 magic;
	__be16 version;
	__be16 wcount;
	__be32 crc;
	__be32 cfr1;
	__be64 profiles[AD9910_NUM_PROFILES];
	__be32 words[] __counted_by_be(wcount);
} __packed;

enum {
	AD9910_CHAN_IDX_PHY,
	AD9910_CHAN_IDX_PROFILE_0,
	AD9910_CHAN_IDX_PROFILE_1,
	AD9910_CHAN_IDX_PROFILE_2,
	AD9910_CHAN_IDX_PROFILE_3,
	AD9910_CHAN_IDX_PROFILE_4,
	AD9910_CHAN_IDX_PROFILE_5,
	AD9910_CHAN_IDX_PROFILE_6,
	AD9910_CHAN_IDX_PROFILE_7,
	AD9910_CHAN_IDX_PARALLEL_AMP,
	AD9910_CHAN_IDX_PARALLEL_PHASE,
	AD9910_CHAN_IDX_PARALLEL_FREQ,
	AD9910_CHAN_IDX_PARALLEL_POLAR_AMP,
	AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE,
	AD9910_CHAN_IDX_DRG_FREQ,
	AD9910_CHAN_IDX_DRG_PHASE,
	AD9910_CHAN_IDX_DRG_AMP,
	AD9910_CHAN_IDX_DRG_FREQ_RAMP_UP,
	AD9910_CHAN_IDX_DRG_FREQ_RAMP_DOWN,
	AD9910_CHAN_IDX_DRG_PHASE_RAMP_UP,
	AD9910_CHAN_IDX_DRG_PHASE_RAMP_DOWN,
	AD9910_CHAN_IDX_DRG_AMP_RAMP_UP,
	AD9910_CHAN_IDX_DRG_AMP_RAMP_DOWN,
	AD9910_CHAN_IDX_RAM,
	AD9910_CHAN_IDX_OSK,
};

enum {
	AD9910_POWERDOWN,
	AD9910_DWELL_EN,
	AD9910_ROC,
	AD9910_ROC_AVAIL,
};

struct ad9910_data {
	u32 sysclk_freq_hz;
	u32 output_current_uA;

	u16 pll_charge_pump_current;
	u8 refclk_out_drv;
	bool pll_enabled;
};

union ad9910_reg {
	u64 val64;
	u32 val32;
	u16 val16;
};

struct ad9910_state {
	struct spi_device *spi;
	struct clk *refclk;
	struct fw_upload *ram_fwu;

	struct gpio_desc *gpio_pwdown;
	struct gpio_desc *gpio_update;
	struct gpio_descs *gpio_profile;

	/* cached registers */
	union ad9910_reg reg[AD9910_REG_NUM_CACHED];

	/*
	 * alternate profile registers used to store RAM profile settings when
	 * RAM mode is disabled and Single Tone profile settings when RAM mode
	 * is enabled.
	 */
	u64 reg_profile[AD9910_NUM_PROFILES];

	/* Lock for accessing device registers and state variables */
	struct mutex lock;

	struct ad9910_data data;
	u8 profile;

	bool ram_fwu_cancel;
	char ram_fwu_name[20];

	/*
	 * RAM loading requires a reasonable amount of bytes, at the same time
	 * DMA capable SPI drivers requires the transfer buffers to live in
	 * their own cache lines.
	 */
	u8 tx_buf[AD9910_SPI_MESSAGE_LEN_MAX] __aligned(IIO_DMA_MINALIGN);
};

/**
 * ad9910_rational_scale() - Perform scaling of input given a reference.
 * @input: The input value to be scaled.
 * @scale: The numerator of the scaling factor.
 * @reference: The denominator of the scaling factor.
 *
 * Closest rounding with mul_u64_add_u64_div_u64
 *
 * Return: The scaled value.
 */
static u64 ad9910_rational_scale(u64 input, u64 scale, u64 reference)
{
	return mul_u64_add_u64_div_u64(input, scale, reference >> 1, reference);
}

static inline u64 ad9910_ram_profile_val(struct ad9910_state *st)
{
	if (AD9910_RAM_ENABLED(st))
		return st->reg[AD9910_REG_PROFILE(st->profile)].val64;
	else
		return st->reg_profile[st->profile];
}

static inline u64 ad9910_st_profile_val(struct ad9910_state *st, u8 profile)
{
	if (AD9910_RAM_ENABLED(st))
		return st->reg_profile[profile];
	else
		return st->reg[AD9910_REG_PROFILE(profile)].val64;
}

static int ad9910_io_update(struct ad9910_state *st)
{
	if (st->gpio_update) {
		gpiod_set_value_cansleep(st->gpio_update, 1);
		fsleep(1);
		gpiod_set_value_cansleep(st->gpio_update, 0);
	}

	return 0;
}

static int ad9910_spi_read(struct ad9910_state *st, u8 reg, void *data,
			   size_t len)
{
	u8 inst = AD9910_SPI_READ_MSK | FIELD_PREP(AD9910_SPI_ADDR_MSK, reg);

	return spi_write_then_read(st->spi, &inst, sizeof(inst), data, len);
}

static int ad9910_spi_write(struct ad9910_state *st, u8 reg, size_t len,
			    bool update)
{
	int ret;

	st->tx_buf[0] = FIELD_PREP(AD9910_SPI_ADDR_MSK, reg);
	ret = spi_write(st->spi, st->tx_buf, AD9910_SPI_DATA_IDX + len);
	if (ret)
		return ret;

	if (update)
		return ad9910_io_update(st);

	return 0;
}

#define AD9910_REG_READ_FN(nb)						\
static int ad9910_reg##nb##_read(struct ad9910_state *st, u8 reg,	\
				 u##nb * data)				\
{									\
	__be##nb be_data;						\
	int ret;							\
									\
	ret = ad9910_spi_read(st, reg, &be_data, sizeof(be_data));	\
	if (ret)							\
		return ret;						\
									\
	*data = be##nb##_to_cpu(be_data);				\
	return ret;							\
}

AD9910_REG_READ_FN(16)
AD9910_REG_READ_FN(32)
AD9910_REG_READ_FN(64)

#define AD9910_REG_WRITE_FN(nb)						\
static int ad9910_reg##nb##_write(struct ad9910_state *st, u8 reg,	\
				  u##nb data, bool update)		\
{									\
	int ret;							\
									\
	put_unaligned_be##nb(data, &st->tx_buf[AD9910_SPI_DATA_IDX]);	\
	ret = ad9910_spi_write(st, reg, sizeof(data), update);		\
	if (ret)							\
		return ret;						\
									\
	st->reg[reg].val##nb = data;					\
	return ret;							\
}

AD9910_REG_WRITE_FN(16)
AD9910_REG_WRITE_FN(32)
AD9910_REG_WRITE_FN(64)

#define AD9910_REG_UPDATE_FN(nb)					\
static int ad9910_reg##nb##_update(struct ad9910_state *st,		\
				   u8 reg, u##nb mask,			\
				   u##nb data, bool update)		\
{									\
	u##nb reg_val = (st->reg[reg].val##nb & ~mask) | (data & mask);	\
									\
	if (reg_val == st->reg[reg].val##nb && !update)			\
		return 0;						\
									\
	return ad9910_reg##nb##_write(st, reg, reg_val, update);	\
}

AD9910_REG_UPDATE_FN(16)
AD9910_REG_UPDATE_FN(32)
AD9910_REG_UPDATE_FN(64)

static int ad9910_set_dac_current(struct ad9910_state *st, u32 val_uA,
				  bool update)
{
	u32 code;

	/* FSC = (86.4 / Rset) * (1 + CODE/96) where Rset = 10k ohms */
	val_uA = clamp(val_uA, AD9910_DAC_IOUT_MIN_uA, AD9910_DAC_IOUT_MAX_uA);
	code = DIV_ROUND_CLOSEST(val_uA - AD9910_DAC_IOUT_MIN_uA, 90);
	st->data.output_current_uA = AD9910_DAC_IOUT_MIN_uA + code * 90;

	return ad9910_reg32_write(st, AD9910_REG_AUX_DAC, code, update);
}

static int ad9910_set_sysclk_freq(struct ad9910_state *st, u32 freq_hz,
				  bool update)
{
	struct device *dev = &st->spi->dev;
	unsigned long refclk_freq_hz;
	u32 sysclk_freq_hz;
	u32 tmp32, vco_sel;
	int ret;

	if (!freq_hz || freq_hz > AD9910_MAX_SYSCLK_HZ)
		return -EINVAL;

	refclk_freq_hz = clk_get_rate(st->refclk);
	if (st->data.pll_enabled) {
		if (refclk_freq_hz < AD9910_PLL_IN_MIN_FREQ_HZ ||
		    refclk_freq_hz > AD9910_PLL_IN_MAX_FREQ_HZ) {
			dev_err(dev,
				"REF_CLK freq %lu Hz is out of PLL input range\n",
				refclk_freq_hz);
			return -ERANGE;
		}

		tmp32 = DIV_ROUND_CLOSEST(freq_hz, refclk_freq_hz);
		tmp32 = clamp(tmp32, DIV_ROUND_UP(AD9910_PLL_OUT_MIN_FREQ_HZ, refclk_freq_hz),
			      AD9910_PLL_OUT_MAX_FREQ_HZ / refclk_freq_hz);
		tmp32 = clamp(tmp32, AD9910_PLL_MIN_N, AD9910_PLL_MAX_N);
		sysclk_freq_hz = refclk_freq_hz * tmp32;

		if (sysclk_freq_hz <= AD9910_VCO0_RANGE_AUTO_MAX_HZ)
			vco_sel = 0;
		else if (sysclk_freq_hz <= AD9910_VCO1_RANGE_AUTO_MAX_HZ)
			vco_sel = 1;
		else if (sysclk_freq_hz <= AD9910_VCO2_RANGE_AUTO_MAX_HZ)
			vco_sel = 2;
		else if (sysclk_freq_hz <= AD9910_VCO3_RANGE_AUTO_MAX_HZ)
			vco_sel = 3;
		else if (sysclk_freq_hz <= AD9910_VCO4_RANGE_AUTO_MAX_HZ)
			vco_sel = 4;
		else
			vco_sel = 5;

		ret = ad9910_reg32_update(st, AD9910_REG_CFR3,
					  AD9910_CFR3_N_MSK | AD9910_CFR3_VCO_SEL_MSK,
					  FIELD_PREP(AD9910_CFR3_N_MSK, tmp32) |
					  FIELD_PREP(AD9910_CFR3_VCO_SEL_MSK, vco_sel),
					  update);
		if (ret)
			return ret;
	} else {
		if (refclk_freq_hz < AD9910_REFDIV2_MIN_FREQ_HZ ||
		    refclk_freq_hz > AD9910_REFDIV2_MAX_FREQ_HZ) {
			dev_err(dev,
				"REF_CLK freq %lu Hz is out of divider range\n",
				refclk_freq_hz);
			return -ERANGE;
		}

		tmp32 = DIV_ROUND_CLOSEST(refclk_freq_hz, freq_hz);
		tmp32 = clamp(tmp32, 1U, 2U);
		sysclk_freq_hz = refclk_freq_hz / tmp32;
		tmp32 = AD9910_CFR3_VCO_SEL_MSK |
			FIELD_PREP(AD9910_CFR3_REFCLK_DIV_BYPASS_MSK, tmp32 % 2);
		ret = ad9910_reg32_update(st, AD9910_REG_CFR3,
					  AD9910_CFR3_VCO_SEL_MSK |
					  AD9910_CFR3_REFCLK_DIV_BYPASS_MSK,
					  tmp32, update);
		if (ret)
			return ret;
	}

	st->data.sysclk_freq_hz = sysclk_freq_hz;

	return 0;
}

static int ad9910_profile_set(struct ad9910_state *st, u8 profile)
{
	DECLARE_BITMAP(values, BITS_PER_TYPE(profile));

	st->profile = profile;
	values[0] = profile;
	gpiod_multi_set_value_cansleep(st->gpio_profile, values);

	return 0;
}

static bool ad9910_sw_powerdown_get(struct ad9910_state *st)
{
	return FIELD_GET(AD9910_CFR1_SW_POWER_DOWN_MSK,
			 st->reg[AD9910_REG_CFR1].val32) ? true : false;
}

static int ad9910_sw_powerdown_set(struct ad9910_state *st, bool enable)
{
	int ret;

	if (ad9910_sw_powerdown_get(st) == enable)
		return 0;

	/*
	 * When powering down, the DAC and AUX_DAC (SW1) must be powered down
	 * first, as they require an I/O update to take effect. The opposite is
	 * true when powering up, the DAC and AUX_DAC must be powered up last,
	 * i.e., after the DIGITAL and REFCLK_INPUT (SW0) power up.
	 */
	if (enable) {
		ret = ad9910_reg32_update(st, AD9910_REG_CFR1,
					  AD9910_CFR1_SW1_POWER_DOWN_MSK,
					  AD9910_CFR1_SW1_POWER_DOWN_MSK,
					  true);
		if (ret)
			return ret;

		return ad9910_reg32_update(st, AD9910_REG_CFR1,
					   AD9910_CFR1_SW0_POWER_DOWN_MSK,
					   AD9910_CFR1_SW0_POWER_DOWN_MSK,
					   false);
	}

	ret = ad9910_reg32_update(st, AD9910_REG_CFR1,
				  AD9910_CFR1_SW0_POWER_DOWN_MSK, 0, false);
	if (ret)
		return ret;

	return ad9910_reg32_update(st, AD9910_REG_CFR1,
				   AD9910_CFR1_SW1_POWER_DOWN_MSK, 0, true);
}

static ssize_t ad9910_ext_info_read(struct iio_dev *indio_dev,
				    uintptr_t private,
				    const struct iio_chan_spec *chan,
				    char *buf)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	int val;

	guard(mutex)(&st->lock);

	switch (private) {
	case AD9910_POWERDOWN:
		val = ad9910_sw_powerdown_get(st);
		break;
	case AD9910_DWELL_EN:
		if (chan->channel == AD9910_CHANNEL_DRG_RAMP_UP)
			val = FIELD_GET(AD9910_CFR2_DRG_NO_DWELL_HIGH_MSK,
					st->reg[AD9910_REG_CFR2].val32) ? 0 : 1;
		else
			val = FIELD_GET(AD9910_CFR2_DRG_NO_DWELL_LOW_MSK,
					st->reg[AD9910_REG_CFR2].val32) ? 0 : 1;
		break;
	default:
		return -EINVAL;
	}

	return iio_format_value(buf, IIO_VAL_INT, 1, &val);
}

static ssize_t ad9910_ext_info_write(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     const char *buf, size_t len)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u32 val32;
	int ret;

	ret = kstrtou32(buf, 10, &val32);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	switch (private) {
	case AD9910_POWERDOWN:
		ret = ad9910_sw_powerdown_set(st, val32 ? true : false);
		if (ret)
			return ret;
		break;
	case AD9910_DWELL_EN:
		if (chan->channel == AD9910_CHANNEL_DRG_RAMP_UP) {
			val32 = val32 ? 0 : AD9910_CFR2_DRG_NO_DWELL_HIGH_MSK;
			ret = ad9910_reg32_update(st, AD9910_REG_CFR2,
						  AD9910_CFR2_DRG_NO_DWELL_HIGH_MSK,
						  val32, true);
			if (ret)
				return ret;
		} else {
			val32 = val32 ? 0 : AD9910_CFR2_DRG_NO_DWELL_LOW_MSK;
			ret = ad9910_reg32_update(st, AD9910_REG_CFR2,
						  AD9910_CFR2_DRG_NO_DWELL_LOW_MSK,
						  val32, true);
			if (ret)
				return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static ssize_t ad9910_drg_roc_read(struct iio_dev *indio_dev, uintptr_t private,
				   const struct iio_chan_spec *chan,
				   char *buf)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u64 roc64;
	u32 rate;

	guard(mutex)(&st->lock);

	switch (chan->channel) {
	case AD9910_CHANNEL_DRG_RAMP_UP:
		roc64 = FIELD_GET(AD9910_DRG_STEP_INC_MSK,
				  st->reg[AD9910_REG_DRG_STEP].val64);
		rate = FIELD_GET(AD9910_DRG_RATE_INC_MSK,
				 st->reg[AD9910_REG_DRG_RATE].val32);
		break;
	case AD9910_CHANNEL_DRG_RAMP_DOWN:
		roc64 = FIELD_GET(AD9910_DRG_STEP_DEC_MSK,
				  st->reg[AD9910_REG_DRG_STEP].val64);
		rate = FIELD_GET(AD9910_DRG_RATE_DEC_MSK,
				 st->reg[AD9910_REG_DRG_RATE].val32);
		break;
	default:
		return -EINVAL;
	}

	if (!rate)
		return -ERANGE;

	roc64 *= st->data.sysclk_freq_hz;
	return sysfs_emit(buf, "%llu\n", div_u64(roc64, 4 * rate));
}

static ssize_t ad9910_drg_roc_write(struct iio_dev *indio_dev,
				    uintptr_t private,
				    const struct iio_chan_spec *chan,
				    const char *buf, size_t len)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u64 tmp64;
	u32 rate;
	int ret;

	ret = kstrtou64(buf, 10, &tmp64);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	switch (chan->channel) {
	case AD9910_CHANNEL_DRG_RAMP_UP:
		rate = FIELD_GET(AD9910_DRG_RATE_INC_MSK,
				 st->reg[AD9910_REG_DRG_RATE].val32);
		if (!rate)
			return -ERANGE;

		tmp64 = ad9910_rational_scale(tmp64, 4 * rate, st->data.sysclk_freq_hz);
		tmp64 = min_t(u64, tmp64, U32_MAX);

		ret = ad9910_reg64_update(st, AD9910_REG_DRG_STEP,
					  AD9910_DRG_STEP_INC_MSK,
					  FIELD_PREP(AD9910_DRG_STEP_INC_MSK, tmp64),
					  true);
		if (ret)
			return ret;
		break;
	case AD9910_CHANNEL_DRG_RAMP_DOWN:
		rate = FIELD_GET(AD9910_DRG_RATE_DEC_MSK,
				 st->reg[AD9910_REG_DRG_RATE].val32);
		if (!rate)
			return -ERANGE;

		tmp64 = ad9910_rational_scale(tmp64, 4 * rate, st->data.sysclk_freq_hz);
		tmp64 = min_t(u64, tmp64, U32_MAX);

		ret = ad9910_reg64_update(st, AD9910_REG_DRG_STEP,
					  AD9910_DRG_STEP_DEC_MSK,
					  FIELD_PREP(AD9910_DRG_STEP_DEC_MSK, tmp64),
					  true);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static const u32 ad9910_osk_raw_step[] = {
	0,		/* no step: manual mode (NOT pin controlled) */
	1,		/* step size 1: automatic mode (pin controlled) */
	2,		/* step size 2: automatic mode (pin controlled) */
	4,		/* step size 4: automatic mode (pin controlled) */
	8,		/* step size 8: automatic mode (pin controlled) */
	GENMASK(13, 0),	/* max step: manual mode (pin controlled) */
};

static ssize_t ad9910_osk_attrs_read(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     char *buf)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	bool auto_en, pinctrl_en;
	u32 rate, step;
	u64 roc64;

	guard(mutex)(&st->lock);

	rate = FIELD_GET(AD9910_ASF_RAMP_RATE_MSK, st->reg[AD9910_REG_ASF].val32);
	if (!rate)
		return -ERANGE;

	switch (private) {
	case AD9910_ROC:
		auto_en = FIELD_GET(AD9910_CFR1_SELECT_AUTO_OSK_MSK,
				    st->reg[AD9910_REG_CFR1].val32);
		pinctrl_en = FIELD_GET(AD9910_CFR1_OSK_MANUAL_EXT_CTL_MSK,
				       st->reg[AD9910_REG_CFR1].val32);
		if (auto_en) {
			step = FIELD_GET(AD9910_ASF_STEP_SIZE_MSK,
					 st->reg[AD9910_REG_ASF].val32);
			step = ad9910_osk_raw_step[step + 1];
		} else if (pinctrl_en) {
			step = ad9910_osk_raw_step[ARRAY_SIZE(ad9910_osk_raw_step) - 1];
		} else {
			step = ad9910_osk_raw_step[0];
		}

		roc64 = div_u64((u64)step * st->data.sysclk_freq_hz, 4 * rate);
		return sysfs_emit(buf, "%llu\n", roc64);
	case AD9910_ROC_AVAIL: {
		ssize_t len = 0;

		for (unsigned int i = 0; i < ARRAY_SIZE(ad9910_osk_raw_step); i++) {
			roc64 = div_u64((u64)ad9910_osk_raw_step[i] * st->data.sysclk_freq_hz,
					4 * rate);
			len += sysfs_emit_at(buf, len, "%llu ", roc64);
		}

		buf[len - 1] = '\n'; /* replace last space with a newline */
		return len;
	}
	default:
		return -EINVAL;
	}
}

static ssize_t ad9910_osk_attrs_write(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u32 idx, reg_val, rate;
	u64 step;
	int ret;

	ret = kstrtou64(buf, 10, &step);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	rate = FIELD_GET(AD9910_ASF_RAMP_RATE_MSK, st->reg[AD9910_REG_ASF].val32);
	if (!rate)
		return -ERANGE;

	switch (private) {
	case AD9910_ROC:
		step = ad9910_rational_scale(step, 4 * rate,
					     st->data.sysclk_freq_hz);
		step = min(step, AD9910_ASF_MAX);
		idx = find_closest(step, ad9910_osk_raw_step,
				   ARRAY_SIZE(ad9910_osk_raw_step));
		if (idx == ARRAY_SIZE(ad9910_osk_raw_step) - 1) {
			/* manual mode: pin-controlled */
			reg_val = AD9910_CFR1_OSK_MANUAL_EXT_CTL_MSK;
		} else if (idx == 0) {
			/* manual mode, NOT pin-controlled */
			reg_val = 0;
		} else {
			/* auto mode: pin-controlled */
			reg_val = FIELD_PREP(AD9910_ASF_STEP_SIZE_MSK, idx - 1);
			ret = ad9910_reg32_update(st, AD9910_REG_ASF,
						  AD9910_ASF_STEP_SIZE_MSK,
						  reg_val, false);
			if (ret)
				return ret;

			reg_val = AD9910_CFR1_SELECT_AUTO_OSK_MSK;
		}

		ret = ad9910_reg32_update(st, AD9910_REG_CFR1,
					  AD9910_CFR1_SELECT_AUTO_OSK_MSK |
					  AD9910_CFR1_OSK_MANUAL_EXT_CTL_MSK,
					  reg_val, true);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static const struct iio_chan_spec_ext_info ad9910_phy_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad9910_ext_info_read,
		.write = ad9910_ext_info_write,
		.private = AD9910_POWERDOWN,
		.shared = IIO_SEPARATE,
	},
	{ }
};

static const struct iio_chan_spec_ext_info ad9910_drg_ramp_ext_info[] = {
	{
		.name = "dwell_en",
		.read = ad9910_ext_info_read,
		.write = ad9910_ext_info_write,
		.private = AD9910_DWELL_EN,
		.shared = IIO_SEPARATE,
	},
	{
		.name = "raw_roc",
		.read = ad9910_drg_roc_read,
		.write = ad9910_drg_roc_write,
		.private = AD9910_ROC,
		.shared = IIO_SEPARATE,
	},
	{ }
};

static const struct iio_chan_spec_ext_info ad9910_osk_ext_info[] = {
	{
		.name = "raw_roc",
		.read = ad9910_osk_attrs_read,
		.write = ad9910_osk_attrs_write,
		.private = AD9910_ROC,
		.shared = IIO_SEPARATE,
	},
	{
		.name = "raw_roc_available",
		.read = ad9910_osk_attrs_read,
		.private = AD9910_ROC_AVAIL,
		.shared = IIO_SEPARATE,
	},
	{ }
};

#define AD9910_PROFILE_CHAN(idx) {				\
	.type = IIO_ALTCURRENT,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = AD9910_CHANNEL_PROFILE_ ## idx,		\
	.address = AD9910_CHAN_IDX_PROFILE_ ## idx,		\
	.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |	\
			      BIT(IIO_CHAN_INFO_FREQUENCY) |	\
			      BIT(IIO_CHAN_INFO_PHASE) |	\
			      BIT(IIO_CHAN_INFO_RAW),		\
	.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],	\
}

static const struct iio_chan_spec ad9910_channels[] = {
	[AD9910_CHAN_IDX_PHY] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PHY,
		.address = AD9910_CHAN_IDX_PHY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.ext_info = ad9910_phy_ext_info,
	},
	[AD9910_CHAN_IDX_PROFILE_0] = AD9910_PROFILE_CHAN(0),
	[AD9910_CHAN_IDX_PROFILE_1] = AD9910_PROFILE_CHAN(1),
	[AD9910_CHAN_IDX_PROFILE_2] = AD9910_PROFILE_CHAN(2),
	[AD9910_CHAN_IDX_PROFILE_3] = AD9910_PROFILE_CHAN(3),
	[AD9910_CHAN_IDX_PROFILE_4] = AD9910_PROFILE_CHAN(4),
	[AD9910_CHAN_IDX_PROFILE_5] = AD9910_PROFILE_CHAN(5),
	[AD9910_CHAN_IDX_PROFILE_6] = AD9910_PROFILE_CHAN(6),
	[AD9910_CHAN_IDX_PROFILE_7] = AD9910_PROFILE_CHAN(7),
	[AD9910_CHAN_IDX_PARALLEL_AMP] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PARALLEL,
		.address = AD9910_CHAN_IDX_PARALLEL_AMP,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_PARALLEL_PHASE] = {
		.type = IIO_PHASE,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PARALLEL,
		.address = AD9910_CHAN_IDX_PARALLEL_PHASE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_PARALLEL_FREQ] = {
		.type = IIO_FREQUENCY,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PARALLEL,
		.address = AD9910_CHAN_IDX_PARALLEL_FREQ,
		.info_mask_separate = BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_PARALLEL_POLAR_AMP] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PARALLEL_POLAR,
		.address = AD9910_CHAN_IDX_PARALLEL_POLAR_AMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE] = {
		.type = IIO_PHASE,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_PARALLEL_POLAR,
		.address = AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_DRG_FREQ] = {
		.type = IIO_FREQUENCY,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG,
		.address = AD9910_CHAN_IDX_DRG_FREQ,
		.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_DRG_PHASE] = {
		.type = IIO_PHASE,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG,
		.address = AD9910_CHAN_IDX_DRG_PHASE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_DRG_AMP] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG,
		.address = AD9910_CHAN_IDX_DRG_AMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_DRG_FREQ_RAMP_UP] = {
		.type = IIO_FREQUENCY,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_UP,
		.address = AD9910_CHAN_IDX_DRG_FREQ_RAMP_UP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_FREQ],
	},
	[AD9910_CHAN_IDX_DRG_FREQ_RAMP_DOWN] = {
		.type = IIO_FREQUENCY,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_DOWN,
		.address = AD9910_CHAN_IDX_DRG_FREQ_RAMP_DOWN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_FREQ],
	},
	[AD9910_CHAN_IDX_DRG_PHASE_RAMP_UP] = {
		.type = IIO_PHASE,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_UP,
		.address = AD9910_CHAN_IDX_DRG_PHASE_RAMP_UP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_PHASE],
	},
	[AD9910_CHAN_IDX_DRG_PHASE_RAMP_DOWN] = {
		.type = IIO_PHASE,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_DOWN,
		.address = AD9910_CHAN_IDX_DRG_PHASE_RAMP_DOWN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_PHASE],
	},
	[AD9910_CHAN_IDX_DRG_AMP_RAMP_UP] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_UP,
		.address = AD9910_CHAN_IDX_DRG_AMP_RAMP_UP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_AMP],
	},
	[AD9910_CHAN_IDX_DRG_AMP_RAMP_DOWN] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_DRG_RAMP_DOWN,
		.address = AD9910_CHAN_IDX_DRG_AMP_RAMP_DOWN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_drg_ramp_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_DRG_AMP],
	},
	[AD9910_CHAN_IDX_RAM] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_RAM,
		.address = AD9910_CHAN_IDX_RAM,
		.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |
				      BIT(IIO_CHAN_INFO_FREQUENCY) |
				      BIT(IIO_CHAN_INFO_PHASE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
	[AD9910_CHAN_IDX_OSK] = {
		.type = IIO_ALTCURRENT,
		.indexed = 1,
		.output = 1,
		.channel = AD9910_CHANNEL_OSK,
		.address = AD9910_CHAN_IDX_OSK,
		.info_mask_separate = BIT(IIO_CHAN_INFO_ENABLE) |
				      BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad9910_osk_ext_info,
		.parent = &ad9910_channels[AD9910_CHAN_IDX_PHY],
	},
};

static int ad9910_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u64 tmp64;
	u32 tmp32;

	guard(mutex)(&st->lock);

	switch (info) {
	case IIO_CHAN_INFO_ENABLE:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			if (ad9910_sw_powerdown_get(st)) {
				*val = 0;
			} else {
				tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
				*val = (tmp32 == st->profile);
			}
			break;
		case AD9910_CHANNEL_DRG:
			tmp32 = FIELD_GET(AD9910_CFR2_DRG_DEST_MSK,
					  st->reg[AD9910_REG_CFR2].val32);
			if (tmp32 == (chan->address - AD9910_CHAN_IDX_DRG_FREQ))
				*val = FIELD_GET(AD9910_CFR2_DRG_ENABLE_MSK,
						 st->reg[AD9910_REG_CFR2].val32);
			else
				*val = 0;
			break;
		case AD9910_CHANNEL_RAM:
			*val = FIELD_GET(AD9910_CFR1_RAM_ENABLE_MSK,
					 st->reg[AD9910_REG_CFR1].val32);
			break;
		case AD9910_CHANNEL_OSK:
			*val = FIELD_GET(AD9910_CFR1_OSK_ENABLE_MSK,
					 st->reg[AD9910_REG_CFR1].val32);
			break;
		default:
			return -EINVAL;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_FREQUENCY:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			tmp64 = FIELD_GET(AD9910_PROFILE_ST_FTW_MSK,
					  ad9910_st_profile_val(st, tmp32));
			break;
		case AD9910_CHANNEL_RAM:
			tmp64 = st->reg[AD9910_REG_FTW].val32;
			break;
		default:
			return -EINVAL;
		}
		tmp64 *= st->data.sysclk_freq_hz;
		*val = tmp64 >> 32;
		*val2 = ((tmp64 & GENMASK_ULL(31, 0)) * MICRO) >> 32;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_PHASE:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			tmp64 = FIELD_GET(AD9910_PROFILE_ST_POW_MSK,
					  ad9910_st_profile_val(st, tmp32));
			break;
		case AD9910_CHANNEL_RAM:
			tmp64 = st->reg[AD9910_REG_POW].val16;
			break;
		default:
			return -EINVAL;
		}
		tmp32 = (tmp64 * AD9910_MAX_PHASE_MICRORAD) >> 16;
		*val = tmp32 / MICRO;
		*val2 = tmp32 % MICRO;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_RAW:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			*val = FIELD_GET(AD9910_PROFILE_ST_ASF_MSK,
					 ad9910_st_profile_val(st, tmp32));
			return IIO_VAL_INT;
		case AD9910_CHANNEL_DRG_RAMP_UP:
			tmp64 = FIELD_GET(AD9910_DRG_LIMIT_UPPER_MSK,
					  st->reg[AD9910_REG_DRG_LIMIT].val64);
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_INT_64;
		case AD9910_CHANNEL_DRG_RAMP_DOWN:
			tmp64 = FIELD_GET(AD9910_DRG_LIMIT_LOWER_MSK,
					  st->reg[AD9910_REG_DRG_LIMIT].val64);
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_INT_64;
		case AD9910_CHANNEL_OSK:
			*val = FIELD_GET(AD9910_ASF_SCALE_FACTOR_MSK,
					 st->reg[AD9910_REG_ASF].val32);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->channel) {
		case AD9910_CHANNEL_PHY:
			*val = st->data.sysclk_freq_hz;
			return IIO_VAL_INT;
		case AD9910_CHANNEL_DRG_RAMP_UP:
			tmp32 = FIELD_GET(AD9910_DRG_RATE_INC_MSK,
					  st->reg[AD9910_REG_DRG_RATE].val32);
			break;
		case AD9910_CHANNEL_DRG_RAMP_DOWN:
			tmp32 = FIELD_GET(AD9910_DRG_RATE_DEC_MSK,
					  st->reg[AD9910_REG_DRG_RATE].val32);
			break;
		case AD9910_CHANNEL_RAM:
			tmp32 = FIELD_GET(AD9910_PROFILE_RAM_STEP_RATE_MSK,
					  ad9910_ram_profile_val(st));
			break;
		case AD9910_CHANNEL_OSK:
			tmp32 = FIELD_GET(AD9910_ASF_RAMP_RATE_MSK,
					  st->reg[AD9910_REG_ASF].val32);
			break;
		default:
			return -EINVAL;
		}
		if (!tmp32)
			return -ERANGE;
		tmp32 *= 4;
		*val = st->data.sysclk_freq_hz / tmp32;
		*val2 = div_u64((u64)(st->data.sysclk_freq_hz % tmp32) * MICRO, tmp32);
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case AD9910_CHAN_IDX_PHY:
			tmp64 = (u64)st->data.output_current_uA *
				AD9910_NANO_MILLIAMP_PER_MICROAMP;
			*val = 0;
			*val2 = tmp64 >> 14;
			return IIO_VAL_INT_PLUS_NANO;
		case AD9910_CHAN_IDX_PARALLEL_PHASE:
			*val = 0;
			*val2 = AD9910_PI_NANORAD >> 15;
			return IIO_VAL_INT_PLUS_NANO;
		case AD9910_CHAN_IDX_PARALLEL_FREQ:
			tmp32 = FIELD_GET(AD9910_CFR2_FM_GAIN_MSK,
					  st->reg[AD9910_REG_CFR2].val32);
			tmp64 = (u64)st->data.sysclk_freq_hz << tmp32;
			tmp64 = ad9910_rational_scale(tmp64, NANO, BIT_ULL(32));
			*val = div_s64_rem(tmp64, NANO, val2);
			return IIO_VAL_INT_PLUS_NANO;
		case AD9910_CHAN_IDX_PARALLEL_POLAR_AMP:
			tmp64 = (u64)st->data.output_current_uA *
				AD9910_NANO_MILLIAMP_PER_MICROAMP;
			*val = 0;
			*val2 = tmp64 >> 8;
			return IIO_VAL_INT_PLUS_NANO;
		case AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE:
			*val = 0;
			*val2 = AD9910_PI_NANORAD >> 7;
			return IIO_VAL_INT_PLUS_NANO;
		case AD9910_CHAN_IDX_DRG_FREQ:
			tmp64 = ad9910_rational_scale(st->data.sysclk_freq_hz,
						      PICO, BIT_ULL(32));
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_DECIMAL64_PICO;
		case AD9910_CHAN_IDX_DRG_PHASE:
			tmp64 = DIV_U64_ROUND_CLOSEST(AD9910_PI_PICORAD, BIT(31));
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_DECIMAL64_PICO;
		case AD9910_CHAN_IDX_DRG_AMP:
			tmp64 = (u64)st->data.output_current_uA *
				AD9910_PICO_MILLIAMP_PER_MICROAMP;
			tmp64 = DIV_U64_ROUND_CLOSEST(tmp64 >> 1, BIT(31));
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_DECIMAL64_PICO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->address) {
		case AD9910_CHAN_IDX_PARALLEL_FREQ:
			tmp64 = (u64)st->reg[AD9910_REG_FTW].val32 * MICRO;
			tmp64 >>= FIELD_GET(AD9910_CFR2_FM_GAIN_MSK,
					    st->reg[AD9910_REG_CFR2].val32);
			iio_val_s64_decompose(tmp64, val, val2);
			return IIO_VAL_DECIMAL64_MICRO;
		case AD9910_CHAN_IDX_PARALLEL_POLAR_AMP:
			tmp32 = FIELD_GET(AD9910_ASF_SCALE_FACTOR_PP_LSB_MSK,
					  st->reg[AD9910_REG_ASF].val32);
			iio_val_s64_decompose(MICRO * tmp32 >> 6, val, val2);
			return IIO_VAL_DECIMAL64_MICRO;
		case AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE:
			tmp32 = FIELD_GET(AD9910_POW_PP_LSB_MSK,
					  st->reg[AD9910_REG_POW].val16);
			iio_val_s64_decompose(MICRO * tmp32 >> 8, val, val2);
			return IIO_VAL_DECIMAL64_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad9910_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	u64 tmp64;
	u32 tmp32;
	int ret, i;

	guard(mutex)(&st->lock);

	switch (info) {
	case IIO_CHAN_INFO_ENABLE:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			if (!val) {
				if (tmp32 != st->profile)
					return 0; /* nothing to do */

				return ad9910_sw_powerdown_set(st, true);
			}

			ret = ad9910_sw_powerdown_set(st, false);
			if (ret)
				return ret;

			return ad9910_profile_set(st, tmp32);
		case AD9910_CHANNEL_DRG:
			tmp32 = chan->address - AD9910_CHAN_IDX_DRG_FREQ;
			if (val) {
				tmp32 = AD9910_CFR2_DRG_ENABLE_MSK |
					FIELD_PREP(AD9910_CFR2_DRG_DEST_MSK, tmp32);
			} else {
				if (tmp32 != FIELD_GET(AD9910_CFR2_DRG_DEST_MSK,
						       st->reg[AD9910_REG_CFR2].val32))
					return 0; /* nothing to do */
				tmp32 = 0;
			}

			return ad9910_reg32_update(st, AD9910_REG_CFR2,
						   AD9910_CFR2_DRG_DEST_MSK |
						   AD9910_CFR2_DRG_ENABLE_MSK,
						   tmp32, true);
		case AD9910_CHANNEL_RAM:
			if (AD9910_RAM_ENABLED(st) == !!val)
				return 0;

			/* swap profile configs */
			for (i = 0; i < AD9910_NUM_PROFILES; i++) {
				tmp64 = st->reg[AD9910_REG_PROFILE(i)].val64;
				ret = ad9910_reg64_write(st,
							 AD9910_REG_PROFILE(i),
							 st->reg_profile[i],
							 false);
				if (ret)
					break;
				st->reg_profile[i] = tmp64;
			}

			if (ret) {
				/*
				 * After the write failure, profiles 0..i-1 were
				 * already swapped in SW, but Hw registers are
				 * still pending an IO update, so swap them back
				 * in SW to keep the state consistent.
				 */
				while (i--) {
					tmp64 = st->reg[AD9910_REG_PROFILE(i)].val64;
					st->reg[AD9910_REG_PROFILE(i)].val64 = st->reg_profile[i];
					st->reg_profile[i] = tmp64;
				}
				return ret;
			}

			tmp32 = FIELD_PREP(AD9910_CFR1_RAM_ENABLE_MSK, !!val);
			return ad9910_reg32_update(st, AD9910_REG_CFR1,
						   AD9910_CFR1_RAM_ENABLE_MSK,
						   tmp32, true);
		case AD9910_CHANNEL_OSK:
			tmp32 = FIELD_PREP(AD9910_CFR1_OSK_ENABLE_MSK, !!val);
			return ad9910_reg32_update(st, AD9910_REG_CFR1,
						   AD9910_CFR1_OSK_ENABLE_MSK,
						   tmp32, true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_FREQUENCY:
		if (val < 0 || val2 < 0 || val >= st->data.sysclk_freq_hz / 2)
			return -EINVAL;

		tmp64 = ad9910_rational_scale((u64)val * MICRO + val2, BIT_ULL(32),
					      (u64)MICRO * st->data.sysclk_freq_hz);
		tmp64 = min_t(u64, tmp64, U32_MAX);
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			if (AD9910_RAM_ENABLED(st)) {
				FIELD_MODIFY(AD9910_PROFILE_ST_FTW_MSK,
					     &st->reg_profile[tmp32], tmp64);
				return 0;
			}
			tmp64 = FIELD_PREP(AD9910_PROFILE_ST_FTW_MSK, tmp64);
			return ad9910_reg64_update(st, AD9910_REG_PROFILE(tmp32),
						   AD9910_PROFILE_ST_FTW_MSK,
						   tmp64, true);
		case AD9910_CHANNEL_RAM:
			return ad9910_reg32_write(st, AD9910_REG_FTW, tmp64, true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_PHASE:
		if (val < 0 || val2 < 0)
			return -EINVAL;

		tmp64 = (u64)val * MICRO + val2;
		if (tmp64 >= AD9910_MAX_PHASE_MICRORAD)
			return -EINVAL;

		tmp64 = DIV_U64_ROUND_CLOSEST(tmp64 << 16, AD9910_MAX_PHASE_MICRORAD);
		tmp64 = min(tmp64, AD9910_POW_MAX);

		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;

			if (AD9910_RAM_ENABLED(st)) {
				FIELD_MODIFY(AD9910_PROFILE_ST_POW_MSK,
					     &st->reg_profile[tmp32], tmp64);
				return 0;
			}

			tmp64 = FIELD_PREP(AD9910_PROFILE_ST_POW_MSK, tmp64);
			return ad9910_reg64_update(st, AD9910_REG_PROFILE(tmp32),
						   AD9910_PROFILE_ST_POW_MSK,
						   tmp64, true);
		case AD9910_CHANNEL_RAM:
			return ad9910_reg16_write(st, AD9910_REG_POW, tmp64, true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
			if (val < 0)
				return -EINVAL;

			tmp32 = chan->channel - AD9910_CHANNEL_PROFILE_0;
			tmp64 = min_t(u64, val, AD9910_ASF_MAX);

			if (AD9910_RAM_ENABLED(st)) {
				FIELD_MODIFY(AD9910_PROFILE_ST_ASF_MSK,
					     &st->reg_profile[tmp32], tmp64);
				return 0;
			}

			tmp64 = FIELD_PREP(AD9910_PROFILE_ST_ASF_MSK, tmp64);
			return ad9910_reg64_update(st, AD9910_REG_PROFILE(tmp32),
						   AD9910_PROFILE_ST_ASF_MSK,
						   tmp64, true);
		case AD9910_CHANNEL_DRG_RAMP_UP: {
			s64 val64 = iio_val_s64_compose(val, val2);

			if (val64 < 0)
				return -EINVAL;

			tmp64 = min_t(u64, val64, U32_MAX);
			tmp64 = FIELD_PREP(AD9910_DRG_LIMIT_UPPER_MSK, tmp64);
			return ad9910_reg64_update(st, AD9910_REG_DRG_LIMIT,
						   AD9910_DRG_LIMIT_UPPER_MSK,
						   tmp64, true);
		}
		case AD9910_CHANNEL_DRG_RAMP_DOWN: {
			s64 val64 = iio_val_s64_compose(val, val2);

			if (val64 < 0)
				return -EINVAL;

			tmp64 = min_t(u64, val64, U32_MAX);
			tmp64 = FIELD_PREP(AD9910_DRG_LIMIT_LOWER_MSK, tmp64);
			return ad9910_reg64_update(st, AD9910_REG_DRG_LIMIT,
						   AD9910_DRG_LIMIT_LOWER_MSK,
						   tmp64, true);
		}
		case AD9910_CHANNEL_OSK:
			if (val < 0)
				return -EINVAL;

			tmp32 = min_t(u32, val, AD9910_ASF_MAX);
			tmp32 = FIELD_PREP(AD9910_ASF_SCALE_FACTOR_MSK, tmp32);
			return ad9910_reg32_update(st, AD9910_REG_ASF,
						   AD9910_ASF_SCALE_FACTOR_MSK,
						   tmp32, true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->channel == AD9910_CHANNEL_PHY)
			return ad9910_set_sysclk_freq(st, val, true);

		if (val < 0 || val2 < 0 || val > st->data.sysclk_freq_hz / 4)
			return -EINVAL;

		tmp64 = ((u64)val * MICRO + val2) * 4;
		if (!tmp64)
			return -EINVAL;

		tmp64 = DIV64_U64_ROUND_CLOSEST((u64)st->data.sysclk_freq_hz * MICRO, tmp64);
		tmp32 = clamp(tmp64, 1U, AD9910_STEP_RATE_MAX);

		switch (chan->channel) {
		case AD9910_CHANNEL_DRG_RAMP_UP:
			tmp32 = FIELD_PREP(AD9910_DRG_RATE_INC_MSK, tmp32);
			return ad9910_reg32_update(st, AD9910_REG_DRG_RATE,
						   AD9910_DRG_RATE_INC_MSK,
						   tmp32, true);
		case AD9910_CHANNEL_DRG_RAMP_DOWN:
			tmp32 = FIELD_PREP(AD9910_DRG_RATE_DEC_MSK, tmp32);
			return ad9910_reg32_update(st, AD9910_REG_DRG_RATE,
						   AD9910_DRG_RATE_DEC_MSK,
						   tmp32, true);
		case AD9910_CHANNEL_RAM:
			if (!AD9910_RAM_ENABLED(st)) {
				FIELD_MODIFY(AD9910_PROFILE_RAM_STEP_RATE_MSK,
					     &st->reg_profile[st->profile], tmp32);
				return 0;
			}

			tmp64 = FIELD_PREP(AD9910_PROFILE_RAM_STEP_RATE_MSK, tmp32);
			return ad9910_reg64_update(st, AD9910_REG_PROFILE(st->profile),
						   AD9910_PROFILE_RAM_STEP_RATE_MSK,
						   tmp64, true);
		case AD9910_CHANNEL_OSK:
			return ad9910_reg32_update(st, AD9910_REG_ASF,
						   AD9910_ASF_RAMP_RATE_MSK,
						   FIELD_PREP(AD9910_ASF_RAMP_RATE_MSK, tmp32),
						   true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case AD9910_CHAN_IDX_PHY:
			if (val != 0 || val2 < 0)
				return -EINVAL;

			tmp32 = DIV_U64_ROUND_CLOSEST((u64)val2 << 14,
						      AD9910_NANO_MILLIAMP_PER_MICROAMP);
			return ad9910_set_dac_current(st, tmp32, true);
		case AD9910_CHAN_IDX_PARALLEL_FREQ:
			if (val < 0 || val2 < 0)
				return -EINVAL;

			tmp64 = ad9910_rational_scale((u64)val * NANO + val2, BIT_ULL(32),
						      (u64)st->data.sysclk_freq_hz * NANO);
			tmp64 = roundup_pow_of_two(clamp(tmp64, 1ULL, BIT_ULL(15)));
			tmp32 = FIELD_PREP(AD9910_CFR2_FM_GAIN_MSK, ilog2(tmp64));
			return ad9910_reg32_update(st, AD9910_REG_CFR2,
						   AD9910_CFR2_FM_GAIN_MSK,
						   tmp32, true);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET: {
		s64 val64 = iio_val_s64_compose(val, val2);

		if (val64 < 0)
			return -EINVAL;

		tmp64 = val64;
		switch (chan->address) {
		case AD9910_CHAN_IDX_PARALLEL_FREQ:
			tmp32 = BIT(FIELD_GET(AD9910_CFR2_FM_GAIN_MSK,
					      st->reg[AD9910_REG_CFR2].val32));
			tmp64 = ad9910_rational_scale(tmp64, tmp32, MICRO);
			tmp64 = min_t(u64, tmp64, U32_MAX);
			return ad9910_reg32_write(st, AD9910_REG_FTW, tmp64, true);
		case AD9910_CHAN_IDX_PARALLEL_POLAR_AMP:
			tmp64 = ad9910_rational_scale(tmp64, BIT(6), MICRO);
			tmp64 = min_t(u64, tmp64, AD9910_ASF_PP_LSB_MAX);
			tmp32 = FIELD_PREP(AD9910_ASF_SCALE_FACTOR_PP_LSB_MSK, tmp64);
			return ad9910_reg32_update(st, AD9910_REG_ASF,
						   AD9910_ASF_SCALE_FACTOR_PP_LSB_MSK,
						   tmp32, true);
		case AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE:
			tmp64 = ad9910_rational_scale(tmp64, BIT(8), MICRO);
			tmp64 = min_t(u64, tmp64, AD9910_POW_PP_LSB_MAX);
			tmp32 = FIELD_PREP(AD9910_POW_PP_LSB_MSK, tmp64);
			return ad9910_reg16_update(st, AD9910_REG_POW,
						   AD9910_POW_PP_LSB_MSK,
						   tmp32, true);
		default:
			return -EINVAL;
		}
	}
	default:
		return -EINVAL;
	}
}

static int ad9910_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_FREQUENCY:
	case IIO_CHAN_INFO_PHASE:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_RAW:
		switch (chan->channel) {
		case AD9910_CHANNEL_PROFILE_0 ... AD9910_CHANNEL_PROFILE_7:
		case AD9910_CHANNEL_OSK:
			return IIO_VAL_INT;
		case AD9910_CHANNEL_DRG_RAMP_UP:
		case AD9910_CHANNEL_DRG_RAMP_DOWN:
			return IIO_VAL_INT_64;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->channel == AD9910_CHANNEL_PHY)
			return IIO_VAL_INT;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		return IIO_VAL_DECIMAL64_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad9910_debugfs_reg_access(struct iio_dev *indio_dev,
				     unsigned int reg, u64 writeval,
				     u64 *readval)
{
	struct ad9910_state *st = iio_priv(indio_dev);
	union ad9910_reg tmp;
	int ret;

	if (reg >= AD9910_REG_RAM)
		return -EINVAL;

	guard(mutex)(&st->lock);

	switch (reg) {
	case AD9910_REG_DRG_LIMIT:
	case AD9910_REG_DRG_STEP:
	case AD9910_REG_PROFILE0 ... AD9910_REG_PROFILE7:
		if (!readval)
			return ad9910_reg64_write(st, reg, writeval, true);

		ret = ad9910_reg64_read(st, reg, &tmp.val64);
		if (ret)
			return ret;
		*readval = tmp.val64;
		return 0;
	case AD9910_REG_POW:
		if (!readval)
			return ad9910_reg16_write(st, reg, writeval, true);

		ret = ad9910_reg16_read(st, reg, &tmp.val16);
		if (ret)
			return ret;
		*readval = tmp.val16;
		return 0;
	default:
		if (!readval)
			return ad9910_reg32_write(st, reg, writeval, true);

		ret = ad9910_reg32_read(st, reg, &tmp.val32);
		if (ret)
			return ret;
		*readval = tmp.val32;
		return 0;
	}
}

static const char * const ad9910_channel_str[] = {
	[AD9910_CHAN_IDX_PHY] = "phy",
	[AD9910_CHAN_IDX_PROFILE_0] = "profile0",
	[AD9910_CHAN_IDX_PROFILE_1] = "profile1",
	[AD9910_CHAN_IDX_PROFILE_2] = "profile2",
	[AD9910_CHAN_IDX_PROFILE_3] = "profile3",
	[AD9910_CHAN_IDX_PROFILE_4] = "profile4",
	[AD9910_CHAN_IDX_PROFILE_5] = "profile5",
	[AD9910_CHAN_IDX_PROFILE_6] = "profile6",
	[AD9910_CHAN_IDX_PROFILE_7] = "profile7",
	[AD9910_CHAN_IDX_PARALLEL_AMP] = "parallel_amplitude",
	[AD9910_CHAN_IDX_PARALLEL_PHASE] = "parallel_phase",
	[AD9910_CHAN_IDX_PARALLEL_FREQ] = "parallel_frequency",
	[AD9910_CHAN_IDX_PARALLEL_POLAR_AMP] = "parallel_polar_amplitude",
	[AD9910_CHAN_IDX_PARALLEL_POLAR_PHASE] = "parallel_polar_phase",
	[AD9910_CHAN_IDX_DRG_FREQ] = "drg_frequency",
	[AD9910_CHAN_IDX_DRG_PHASE] = "drg_phase",
	[AD9910_CHAN_IDX_DRG_AMP] = "drg_amplitude",
	[AD9910_CHAN_IDX_DRG_FREQ_RAMP_UP] = "drg_rising_frequency",
	[AD9910_CHAN_IDX_DRG_FREQ_RAMP_DOWN] = "drg_falling_frequency",
	[AD9910_CHAN_IDX_DRG_PHASE_RAMP_UP] = "drg_rising_phase",
	[AD9910_CHAN_IDX_DRG_PHASE_RAMP_DOWN] = "drg_falling_phase",
	[AD9910_CHAN_IDX_DRG_AMP_RAMP_UP] = "drg_rising_amplitude",
	[AD9910_CHAN_IDX_DRG_AMP_RAMP_DOWN] = "drg_falling_amplitude",
	[AD9910_CHAN_IDX_RAM] = "ram",
	[AD9910_CHAN_IDX_OSK] = "osk",
};

static int ad9910_read_label(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     char *label)
{
	return sysfs_emit(label, "%s\n", ad9910_channel_str[chan->address]);
}

static enum fw_upload_err ad9910_ram_fwu_prepare(struct fw_upload *fw_upload,
						 const u8 *data, u32 size)
{
	struct ad9910_state *st = fw_upload->dd_handle;
	const struct ad9910_ram_fw *fw_data = (const struct ad9910_ram_fw *)data;
	size_t wcount, bcount;

	scoped_guard(mutex, &st->lock) {
		/* cancel requests before prepare() are ignored */
		st->ram_fwu_cancel = false;
	}

	if (size < sizeof(struct ad9910_ram_fw))
		return FW_UPLOAD_ERR_INVALID_SIZE;

	if (get_unaligned_be32(&fw_data->magic) != AD9910_RAM_FW_MAGIC)
		return FW_UPLOAD_ERR_FW_INVALID;

	if (get_unaligned_be16(&fw_data->version) != AD9910_RAM_FW_V1)
		return FW_UPLOAD_ERR_FW_INVALID;

	wcount = get_unaligned_be16(&fw_data->wcount);
	bcount = size - sizeof(struct ad9910_ram_fw);
	if (wcount > AD9910_RAM_SIZE_MAX_WORDS ||
	    bcount != (wcount * AD9910_RAM_WORD_SIZE))
		return FW_UPLOAD_ERR_INVALID_SIZE;

	bcount += sizeof(fw_data->cfr1) + sizeof(fw_data->profiles);
	if (crc32(0, &fw_data->cfr1, bcount) != get_unaligned_be32(&fw_data->crc))
		return FW_UPLOAD_ERR_FW_INVALID;

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err ad9910_ram_fwu_write(struct fw_upload *fw_upload,
					       const u8 *data, u32 offset,
					       u32 size, u32 *written)
{
	const struct ad9910_ram_fw *fw_data = (const struct ad9910_ram_fw *)data;
	struct ad9910_state *st = fw_upload->dd_handle;
	int ret, ret2, idx, wcount;
	u64 tmp64, backup;

	if (offset != 0)
		return FW_UPLOAD_ERR_INVALID_SIZE;

	guard(mutex)(&st->lock);

	if (st->ram_fwu_cancel)
		return FW_UPLOAD_ERR_CANCELED;

	if (AD9910_RAM_ENABLED(st))
		return FW_UPLOAD_ERR_HW_ERROR;

	for (idx = 0; idx < AD9910_NUM_PROFILES; idx++)
		st->reg_profile[idx] = get_unaligned_be64(&fw_data->profiles[idx]) |
				       AD9910_PROFILE_RAM_OPEN_MSK;

	ret = ad9910_reg32_update(st, AD9910_REG_CFR1,
				  AD9910_CFR1_RAM_PLAYBACK_DEST_MSK |
				  AD9910_CFR1_INT_PROFILE_CTL_MSK,
				  get_unaligned_be32(&fw_data->cfr1), true);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	wcount = get_unaligned_be16(&fw_data->wcount);
	if (!wcount) {
		*written = size;
		return FW_UPLOAD_ERR_NONE; /* nothing else to write */
	}

	ret = ad9910_profile_set(st, st->profile);
	if (ret)
		return FW_UPLOAD_ERR_HW_ERROR;

	/* backup profile register and update it with required address range */
	backup = st->reg[AD9910_REG_PROFILE(st->profile)].val64;
	tmp64 = AD9910_PROFILE_RAM_STEP_RATE_MSK |
		FIELD_PREP(AD9910_PROFILE_RAM_START_ADDR_MSK, 0) |
		FIELD_PREP(AD9910_PROFILE_RAM_END_ADDR_MSK, wcount - 1);
	ret = ad9910_reg64_write(st, AD9910_REG_PROFILE(st->profile), tmp64, true);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	/* copy data words into spi buffer and transfer */
	memcpy(&st->tx_buf[1], fw_data->words, wcount * AD9910_RAM_WORD_SIZE);
	ret = ad9910_spi_write(st, AD9910_REG_RAM,
			       wcount * AD9910_RAM_WORD_SIZE, false);

	/* restore active single tone profile regardless */
	st->reg[AD9910_REG_PROFILE(st->profile)].val64 = backup;
	ret2 = ad9910_reg64_write(st, AD9910_REG_PROFILE(st->profile), backup, true);
	if (ret || ret2)
		return FW_UPLOAD_ERR_RW_ERROR;

	*written = size;

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err ad9910_ram_fwu_poll_complete(struct fw_upload *fw_upload)
{
	return FW_UPLOAD_ERR_NONE;
}

static void ad9910_ram_fwu_cancel(struct fw_upload *fw_upload)
{
	struct ad9910_state *st = fw_upload->dd_handle;

	guard(mutex)(&st->lock);
	st->ram_fwu_cancel = true;
}

static void ad9910_ram_fwu_unregister(void *data)
{
	firmware_upload_unregister(data);
}

static const struct fw_upload_ops ad9910_ram_fwu_ops = {
	.prepare = ad9910_ram_fwu_prepare,
	.write = ad9910_ram_fwu_write,
	.poll_complete = ad9910_ram_fwu_poll_complete,
	.cancel = ad9910_ram_fwu_cancel,
};

static const struct iio_info ad9910_info = {
	.read_raw = ad9910_read_raw,
	.write_raw = ad9910_write_raw,
	.write_raw_get_fmt = ad9910_write_raw_get_fmt,
	.read_label = ad9910_read_label,
	.debugfs_reg64_access = &ad9910_debugfs_reg_access,
};

static int ad9910_cfg_sysclk(struct ad9910_state *st, bool update)
{
	u32 cfr3 = AD9910_CFR3_OPEN_MSK;
	u32 tmp32;

	cfr3 |= FIELD_PREP(AD9910_CFR3_DRV0_MSK, st->data.refclk_out_drv);

	if (st->data.pll_enabled) {
		tmp32 = st->data.pll_charge_pump_current - AD9910_ICP_MIN_uA;
		tmp32 = DIV_ROUND_CLOSEST(tmp32, AD9910_ICP_STEP_uA);
		cfr3 |= FIELD_PREP(AD9910_CFR3_ICP_MSK, tmp32) |
			AD9910_CFR3_PLL_EN_MSK;
	} else {
		cfr3 |= AD9910_CFR3_ICP_MSK |
			AD9910_CFR3_REFCLK_DIV_RESETB_MSK |
			AD9910_CFR3_PFD_RESET_MSK;
	}
	st->reg[AD9910_REG_CFR3].val32 = cfr3;

	return ad9910_set_sysclk_freq(st, AD9910_MAX_SYSCLK_HZ, update);
}

static int ad9910_parse_fw(struct ad9910_state *st)
{
	static const char * const refclk_out_drv0[] = {
		"disabled", "low", "medium", "high",
	};
	struct device *dev = &st->spi->dev;
	const char *prop;
	u32 tmp;
	int ret;

	st->data.pll_enabled = device_property_read_bool(dev, "adi,pll-enable");
	if (st->data.pll_enabled) {
		prop = "adi,charge-pump-current-microamp";
		if (device_property_present(dev, prop)) {
			ret = device_property_read_u32(dev, prop, &tmp);
			if (ret)
				return dev_err_probe(dev, ret, "property read: %s\n", prop);

			if (tmp < AD9910_ICP_MIN_uA || tmp > AD9910_ICP_MAX_uA)
				return dev_err_probe(dev, -ERANGE,
						     "invalid charge pump current %u\n", tmp);
		} else {
			tmp = AD9910_ICP_MIN_uA;
		}
		st->data.pll_charge_pump_current = tmp;

		prop = "adi,refclk-out-drive-strength";
		if (device_property_present(dev, prop)) {
			ret = device_property_match_property_string(dev, prop,
								    refclk_out_drv0,
								    ARRAY_SIZE(refclk_out_drv0));
			if (ret < 0)
				return dev_err_probe(dev, ret, "property read: %s\n", prop);

			st->data.refclk_out_drv = ret;
		}
	}

	return 0;
}

static void ad9910_sw_powerdown_action(void *data)
{
	ad9910_sw_powerdown_set(data, true);
}

static void ad9910_hw_powerdown_action(void *data)
{
	struct ad9910_state *st = data;

	gpiod_set_value_cansleep(st->gpio_pwdown, 1);
}

static int ad9910_setup(struct device *dev, struct ad9910_state *st,
			struct reset_control *dev_rst)
{
	int ret;

	ret = reset_control_assert(dev_rst);
	if (ret)
		return ret;

	fsleep(AD9910_RESET_DELAY_us);

	ret = reset_control_deassert(dev_rst);
	if (ret)
		return ret;

	ret = ad9910_reg32_write(st, AD9910_REG_CFR1,
				 (st->spi->mode & SPI_3WIRE ? 0 :
				 AD9910_CFR1_SDIO_INPUT_ONLY_MSK), false);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, ad9910_sw_powerdown_action, st);
	if (ret)
		return ret;

	ret = ad9910_reg32_write(st, AD9910_REG_CFR2,
				 AD9910_CFR2_AMP_SCALE_SINGLE_TONE_MSK |
				 AD9910_CFR2_SYNC_TIMING_VAL_DISABLE_MSK |
				 AD9910_CFR2_DRG_NO_DWELL_MSK |
				 AD9910_CFR2_DATA_ASM_HOLD_LAST_MSK |
				 AD9910_CFR2_SYNC_CLK_EN_MSK |
				 AD9910_CFR2_PDCLK_ENABLE_MSK, false);
	if (ret)
		return ret;

	ret = ad9910_cfg_sysclk(st, false);
	if (ret)
		return ret;

	ret = ad9910_set_dac_current(st, AD9910_DAC_IOUT_DEFAULT_uA, false);
	if (ret)
		return ret;

	/* configure step rate with default values */
	ret = ad9910_reg32_write(st, AD9910_REG_ASF,
				 FIELD_PREP(AD9910_ASF_RAMP_RATE_MSK, 1),
				 false);
	if (ret)
		return ret;

	ret = ad9910_reg32_write(st, AD9910_REG_DRG_RATE,
				 FIELD_PREP(AD9910_DRG_RATE_DEC_MSK, 1) |
				 FIELD_PREP(AD9910_DRG_RATE_INC_MSK, 1),
				 false);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < AD9910_NUM_PROFILES; i++) {
		st->reg_profile[i] = AD9910_PROFILE_RAM_OPEN_MSK;
		st->reg_profile[i] |= FIELD_PREP(AD9910_PROFILE_RAM_STEP_RATE_MSK, 1);
		st->reg_profile[i] |= FIELD_PREP(AD9910_PROFILE_RAM_END_ADDR_MSK,
						 AD9910_RAM_ADDR_MAX);
	}

	return ad9910_io_update(st);
}

static inline const char *ad9910_frequency_source_get(struct ad9910_state *st)
{
	bool ram_en, mode_en;

	guard(mutex)(&st->lock);

	/* RAM enabled and data destination is frequency */
	ram_en = AD9910_RAM_ENABLED(st);
	if (ram_en && AD9910_DEST_FREQUENCY ==
		      FIELD_GET(AD9910_CFR1_RAM_PLAYBACK_DEST_MSK,
				st->reg[AD9910_REG_CFR1].val32))
		return ad9910_channel_str[AD9910_CHAN_IDX_RAM];

	/* DRG enabled and data destination is frequency */
	mode_en = FIELD_GET(AD9910_CFR2_DRG_ENABLE_MSK,
			    st->reg[AD9910_REG_CFR2].val32);
	if (mode_en && AD9910_DEST_FREQUENCY ==
		       FIELD_GET(AD9910_CFR2_DRG_DEST_MSK,
				 st->reg[AD9910_REG_CFR2].val32))
		return ad9910_channel_str[AD9910_CHAN_IDX_DRG_FREQ];

	/* FTW: RAM enabled and data destination is phase, amplitude, or polar */
	if (ram_en)
		return ad9910_channel_str[AD9910_CHAN_IDX_RAM];

	/* single tone profiles */
	return ad9910_channel_str[AD9910_CHAN_IDX_PROFILE_0 + st->profile];
}

static int ad9910_frequency_source_show(struct seq_file *s, void *ignored)
{
	seq_printf(s, "%s\n", ad9910_frequency_source_get(s->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ad9910_frequency_source);

static inline const char *ad9910_phase_source_get(struct ad9910_state *st)
{
	bool ram_en, mode_en;
	u32 destination;

	guard(mutex)(&st->lock);

	/* RAM enabled and data destination is phase or polar  */
	ram_en = AD9910_RAM_ENABLED(st);
	if (ram_en) {
		destination = FIELD_GET(AD9910_CFR1_RAM_PLAYBACK_DEST_MSK,
					st->reg[AD9910_REG_CFR1].val32);
		if (destination == AD9910_DEST_PHASE ||
		    destination == AD9910_DEST_POLAR)
			return ad9910_channel_str[AD9910_CHAN_IDX_RAM];
	}

	/* DRG enabled and data destination is phase */
	mode_en = FIELD_GET(AD9910_CFR2_DRG_ENABLE_MSK,
			    st->reg[AD9910_REG_CFR2].val32);
	if (mode_en && AD9910_DEST_PHASE ==
		       FIELD_GET(AD9910_CFR2_DRG_DEST_MSK,
				 st->reg[AD9910_REG_CFR2].val32))
		return ad9910_channel_str[AD9910_CHAN_IDX_DRG_PHASE];

	/* POW: RAM enabled and data destination is frequency or amplitude */
	if (ram_en)
		return ad9910_channel_str[AD9910_CHAN_IDX_RAM];

	/* single tone profiles */
	return ad9910_channel_str[AD9910_CHAN_IDX_PROFILE_0 + st->profile];
}

static int ad9910_phase_source_show(struct seq_file *s, void *ignored)
{
	seq_printf(s, "%s\n", ad9910_phase_source_get(s->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ad9910_phase_source);

static inline const char *ad9910_amplitude_source_get(struct ad9910_state *st)
{
	bool ram_en, mode_en;
	u32 destination;

	guard(mutex)(&st->lock);

	/* OSK enabled */
	mode_en = FIELD_GET(AD9910_CFR1_OSK_ENABLE_MSK,
			    st->reg[AD9910_REG_CFR1].val32);
	if (mode_en)
		return ad9910_channel_str[AD9910_CHAN_IDX_OSK];

	/* RAM enabled and data destination is amplitude or polar */
	ram_en = AD9910_RAM_ENABLED(st);
	if (ram_en) {
		destination = FIELD_GET(AD9910_CFR1_RAM_PLAYBACK_DEST_MSK,
					st->reg[AD9910_REG_CFR1].val32);
		if (destination == AD9910_DEST_AMPLITUDE ||
		    destination == AD9910_DEST_POLAR)
			return ad9910_channel_str[AD9910_CHAN_IDX_RAM];
	}

	/* DRG enabled and data destination is amplitude */
	mode_en = FIELD_GET(AD9910_CFR2_DRG_ENABLE_MSK,
			    st->reg[AD9910_REG_CFR2].val32);
	if (mode_en && AD9910_DEST_AMPLITUDE ==
		       FIELD_GET(AD9910_CFR2_DRG_DEST_MSK,
				 st->reg[AD9910_REG_CFR2].val32))
		return ad9910_channel_str[AD9910_CHAN_IDX_DRG_AMP];

	/* only way to control amplitude at this point is through OSK */
	if (ram_en)
		return ad9910_channel_str[AD9910_CHAN_IDX_OSK];

	/* single tone profiles */
	return ad9910_channel_str[AD9910_CHAN_IDX_PROFILE_0 + st->profile];
}

static int ad9910_amplitude_source_show(struct seq_file *s, void *ignored)
{
	seq_printf(s, "%s\n", ad9910_amplitude_source_get(s->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ad9910_amplitude_source);

static inline void ad9910_debugfs_init(struct ad9910_state *st,
				       struct iio_dev *indio_dev)
{
	struct dentry *d = iio_get_debugfs_dentry(indio_dev);

	debugfs_create_file("frequency_source", 0400, d, st,
			    &ad9910_frequency_source_fops);
	debugfs_create_file("phase_source", 0400, d, st,
			    &ad9910_phase_source_fops);
	debugfs_create_file("amplitude_source", 0400, d, st,
			    &ad9910_amplitude_source_fops);
}

static int ad9910_probe(struct spi_device *spi)
{
	static const char * const supplies[] = {
		"dvdd-io33", "avdd33", "dvdd18", "avdd18",
	};
	struct device *dev = &spi->dev;
	struct reset_control *dev_rst;
	struct gpio_desc *io_rst_gpio;
	struct iio_dev *indio_dev;
	struct ad9910_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	indio_dev->name = "ad9910";
	indio_dev->info = &ad9910_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad9910_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad9910_channels);

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(supplies), supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	st->refclk = devm_clk_get_enabled(dev, "ref_clk");
	if (IS_ERR(st->refclk))
		return dev_err_probe(dev, PTR_ERR(st->refclk),
				     "Failed to get reference clock\n");

	dev_rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(dev_rst))
		return dev_err_probe(dev, PTR_ERR(dev_rst),
				     "failed to get device reset control\n");

	/*
	 * The IO RESET pin is not used in this driver, as we assume that all
	 * SPI transfers are complete, but if it is wired up, we need to make
	 * sure it is not floating. We can use either a reset controller or a
	 * GPIO for this.
	 */
	io_rst_gpio = devm_gpiod_get_optional(dev, "io-reset", GPIOD_OUT_LOW);
	if (IS_ERR(io_rst_gpio))
		return dev_err_probe(dev, PTR_ERR(io_rst_gpio),
				     "failed to get io reset gpio\n");

	st->gpio_update = devm_gpiod_get_optional(dev, "update", GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_update))
		return dev_err_probe(dev, PTR_ERR(st->gpio_update),
				     "failed to get update gpio\n");

	st->gpio_profile = devm_gpiod_get_array_optional(dev, "profile",
							 GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_profile))
		return dev_err_probe(dev, PTR_ERR(st->gpio_profile),
				     "failed to get profile gpios\n");

	if (st->gpio_profile && st->gpio_profile->ndescs != 3)
		return dev_err_probe(dev, -EINVAL,
				     "invalid number of profile gpios\n");

	st->gpio_pwdown = devm_gpiod_get_optional(dev, "powerdown",
						  GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_pwdown))
		return dev_err_probe(dev, PTR_ERR(st->gpio_pwdown),
				     "failed to get powerdown gpio\n");

	ret = devm_add_action_or_reset(dev, ad9910_hw_powerdown_action, st);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to add hw powerdown action\n");

	fsleep(AD9910_WAKEUP_DELAY_us);

	ret = ad9910_parse_fw(st);
	if (ret)
		return ret;

	ret = ad9910_setup(dev, st, dev_rst);
	if (ret)
		return dev_err_probe(dev, ret, "device setup failed\n");

	snprintf(st->ram_fwu_name, sizeof(st->ram_fwu_name), "%s:ram",
		 dev_name(&indio_dev->dev));
	st->ram_fwu = firmware_upload_register(THIS_MODULE, dev, st->ram_fwu_name,
					       &ad9910_ram_fwu_ops, st);
	if (IS_ERR(st->ram_fwu))
		return dev_err_probe(dev, PTR_ERR(st->ram_fwu),
				     "failed to register ram upload ops\n");

	ret = devm_add_action_or_reset(dev, ad9910_ram_fwu_unregister, st->ram_fwu);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to add ram upload unregister action\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ret;

	ad9910_debugfs_init(st, indio_dev);

	return 0;
}

static const struct spi_device_id ad9910_id[] = {
	{ .name = "ad9910" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad9910_id);

static const struct of_device_id ad9910_of_match[] = {
	{ .compatible = "adi,ad9910" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad9910_of_match);

static struct spi_driver ad9910_driver = {
	.driver = {
		.name = "ad9910",
		.of_match_table = ad9910_of_match,
	},
	.probe = ad9910_probe,
	.id_table = ad9910_id,
};
module_spi_driver(ad9910_driver);

MODULE_AUTHOR("Rodrigo Alencar <rodrigo.alencar@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9910 DDS driver");
MODULE_LICENSE("GPL");
