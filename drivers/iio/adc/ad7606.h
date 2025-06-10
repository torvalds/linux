/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7606 ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#ifndef IIO_ADC_AD7606_H_
#define IIO_ADC_AD7606_H_

#define AD760X_MAX_CHANNELS	16

#define AD7616_CONFIGURATION_REGISTER	0x02
#define AD7616_OS_MASK			GENMASK(4, 2)
#define AD7616_BURST_MODE		BIT(6)
#define AD7616_SEQEN_MODE		BIT(5)
#define AD7616_RANGE_CH_A_ADDR_OFF	0x04
#define AD7616_RANGE_CH_B_ADDR_OFF	0x06
/*
 * Range of channels from a group are stored in 2 registers.
 * 0, 1, 2, 3 in a register followed by 4, 5, 6, 7 in second register.
 * For channels from second group(8-15) the order is the same, only with
 * an offset of 2 for register address.
 */
#define AD7616_RANGE_CH_ADDR(ch)	((ch) >> 2)
/* The range of the channel is stored in 2 bits */
#define AD7616_RANGE_CH_MSK(ch)		(0b11 << (((ch) & 0b11) * 2))
#define AD7616_RANGE_CH_MODE(ch, mode)	((mode) << ((((ch) & 0b11)) * 2))

#define AD7606_CONFIGURATION_REGISTER	0x02
#define AD7606_SINGLE_DOUT		0x00

/*
 * Range for AD7606B channels are stored in registers starting with address 0x3.
 * Each register stores range for 2 channels(4 bits per channel).
 */
#define AD7606_RANGE_CH_MSK(ch)		(GENMASK(3, 0) << (4 * ((ch) & 0x1)))
#define AD7606_RANGE_CH_MODE(ch, mode)	\
	((GENMASK(3, 0) & (mode)) << (4 * ((ch) & 0x1)))
#define AD7606_RANGE_CH_ADDR(ch)	(0x03 + ((ch) >> 1))
#define AD7606_OS_MODE			0x08

#define AD760X_CHANNEL(num, mask_sep, mask_type, mask_all,	\
		mask_sep_avail, mask_all_avail, bits) {		\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = num,					\
		.address = num,					\
		.info_mask_separate = mask_sep,			\
		.info_mask_separate_available =			\
			mask_sep_avail,				\
		.info_mask_shared_by_type = mask_type,		\
		.info_mask_shared_by_all = mask_all,		\
		.info_mask_shared_by_all_available =		\
			mask_all_avail,				\
		.scan_index = num,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = (bits),			\
			.storagebits = (bits) > 16 ? 32 : 16,	\
			.endianness = IIO_CPU,			\
		},						\
}

#define AD7606_SW_CHANNEL(num, bits)			\
	AD760X_CHANNEL(num,				\
		/* mask separate */			\
		BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_SCALE),		\
		/* mask type */				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		/* mask all */				\
		0,					\
		/* mask separate available */		\
		BIT(IIO_CHAN_INFO_SCALE),		\
		/* mask all available */		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		bits)

#define AD7605_CHANNEL(num)				\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_RAW),	\
		BIT(IIO_CHAN_INFO_SCALE), 0, 0, 0, 16)

#define AD7606_CHANNEL(num, bits)			\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_RAW),	\
		BIT(IIO_CHAN_INFO_SCALE),		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		0, 0, bits)

#define AD7616_CHANNEL(num)	AD7606_SW_CHANNEL(num, 16)

#define AD7606_BI_CHANNEL(num)				\
	AD760X_CHANNEL(num, 0,				\
		BIT(IIO_CHAN_INFO_SCALE),		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),  \
		0, 0, 16)

#define AD7606_BI_SW_CHANNEL(num)			\
	AD760X_CHANNEL(num,				\
		/* mask separate */			\
		BIT(IIO_CHAN_INFO_SCALE),		\
		/* mask type */				\
		0,					\
		/* mask all */				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		/* mask separate available */		\
		BIT(IIO_CHAN_INFO_SCALE),		\
		/* mask all available */		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		16)

struct ad7606_state;

typedef int (*ad7606_scale_setup_cb_t)(struct iio_dev *indio_dev,
				       struct iio_chan_spec *chan, int ch);
typedef int (*ad7606_sw_setup_cb_t)(struct iio_dev *indio_dev);

/**
 * struct ad7606_chip_info - chip specific information
 * @channels:		channel specification
 * @max_samplerate:	maximum supported samplerate
 * @name		device name
 * @num_channels:	number of channels
 * @num_adc_channels	the number of channels the ADC actually inputs.
 * @scale_setup_cb:	callback to setup the scales for each channel
 * @sw_setup_cb:	callback to setup the software mode if available.
 * @oversampling_avail	pointer to the array which stores the available
 *			oversampling ratios.
 * @oversampling_num	number of elements stored in oversampling_avail array
 * @os_req_reset	some devices require a reset to update oversampling
 * @init_delay_ms	required delay in milliseconds for initialization
 *			after a restart
 */
struct ad7606_chip_info {
	const struct iio_chan_spec	*channels;
	unsigned int			max_samplerate;
	const char			*name;
	unsigned int			num_adc_channels;
	unsigned int			num_channels;
	ad7606_scale_setup_cb_t		scale_setup_cb;
	ad7606_sw_setup_cb_t		sw_setup_cb;
	const unsigned int		*oversampling_avail;
	unsigned int			oversampling_num;
	bool				os_req_reset;
	unsigned long			init_delay_ms;
};

/**
 * struct ad7606_chan_scale - channel scale configuration
 * @scale_avail		pointer to the array which stores the available scales
 * @num_scales		number of elements stored in the scale_avail array
 * @range		voltage range selection, selects which scale to apply
 * @reg_offset		offset for the register value, to be applied when
 *			writing the value of 'range' to the register value
 */
struct ad7606_chan_scale {
#define AD760X_MAX_SCALES		16
	const unsigned int		(*scale_avail)[2];
	unsigned int			num_scales;
	unsigned int			range;
	unsigned int			reg_offset;
};

/**
 * struct ad7606_state - driver instance specific data
 * @dev		pointer to kernel device
 * @chip_info		entry in the table of chips that describes this device
 * @bops		bus operations (SPI or parallel)
 * @chan_scales		scale configuration for channels
 * @oversampling	oversampling selection
 * @cnvst_pwm		pointer to the PWM device connected to the cnvst pin
 * @base_address	address from where to read data in parallel operation
 * @sw_mode_en		software mode enabled
 * @oversampling_avail	pointer to the array which stores the available
 *			oversampling ratios.
 * @num_os_ratios	number of elements stored in oversampling_avail array
 * @write_scale		pointer to the function which writes the scale
 * @write_os		pointer to the function which writes the os
 * @lock		protect sensor state from concurrent accesses to GPIOs
 * @gpio_convst	GPIO descriptor for conversion start signal (CONVST)
 * @gpio_reset		GPIO descriptor for device hard-reset
 * @gpio_range		GPIO descriptor for range selection
 * @gpio_standby	GPIO descriptor for stand-by signal (STBY),
 *			controls power-down mode of device
 * @gpio_frstdata	GPIO descriptor for reading from device when data
 *			is being read on the first channel
 * @gpio_os		GPIO descriptors to control oversampling on the device
 * @complete		completion to indicate end of conversion
 * @trig		The IIO trigger associated with the device.
 * @data		buffer for reading data from the device
 * @d16			be16 buffer for reading data from the device
 */
struct ad7606_state {
	struct device			*dev;
	const struct ad7606_chip_info	*chip_info;
	const struct ad7606_bus_ops	*bops;
	struct ad7606_chan_scale	chan_scales[AD760X_MAX_CHANNELS];
	unsigned int			oversampling;
	struct pwm_device		*cnvst_pwm;
	void __iomem			*base_address;
	bool				sw_mode_en;
	const unsigned int		*oversampling_avail;
	unsigned int			num_os_ratios;
	struct iio_backend		*back;
	int (*write_scale)(struct iio_dev *indio_dev, int ch, int val);
	int (*write_os)(struct iio_dev *indio_dev, int val);

	struct mutex			lock; /* protect sensor state */
	struct gpio_desc		*gpio_convst;
	struct gpio_desc		*gpio_reset;
	struct gpio_desc		*gpio_range;
	struct gpio_desc		*gpio_standby;
	struct gpio_desc		*gpio_frstdata;
	struct gpio_descs		*gpio_os;
	struct iio_trigger		*trig;
	struct completion		completion;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 * 16 * 16-bit samples + 64-bit timestamp - for AD7616
	 * 8 * 32-bit samples + 64-bit timestamp - for AD7616C-18 (and similar)
	 */
	union {
		u16 buf16[20];
		u32 buf32[10];
	} data __aligned(IIO_DMA_MINALIGN);
	__be16				d16[2];
};

/**
 * struct ad7606_bus_ops - driver bus operations
 * @iio_backend_config	function pointer for configuring the iio_backend for
 *			the compatibles that use it
 * @read_block		function pointer for reading blocks of data
 * @sw_mode_config:	pointer to a function which configured the device
 *			for software mode
 * @reg_read	function pointer for reading spi register
 * @reg_write	function pointer for writing spi register
 * @write_mask	function pointer for write spi register with mask
 * @update_scan_mode	function pointer for handling the calls to iio_info's update_scan
 *			mode when enabling/disabling channels.
 * @rd_wr_cmd	pointer to the function which calculates the spi address
 */
struct ad7606_bus_ops {
	/* more methods added in future? */
	int (*iio_backend_config)(struct device *dev, struct iio_dev *indio_dev);
	int (*read_block)(struct device *dev, int num, void *data);
	int (*sw_mode_config)(struct iio_dev *indio_dev);
	int (*reg_read)(struct ad7606_state *st, unsigned int addr);
	int (*reg_write)(struct ad7606_state *st,
				unsigned int addr,
				unsigned int val);
	int (*update_scan_mode)(struct iio_dev *indio_dev, const unsigned long *scan_mask);
	u16 (*rd_wr_cmd)(int addr, char isWriteOp);
};

/**
 * struct ad7606_bus_info - agregate ad7606_chip_info and ad7606_bus_ops
 * @chip_info		entry in the table of chips that describes this device
 * @bops		bus operations (SPI or parallel)
 */
struct ad7606_bus_info {
	const struct ad7606_chip_info	*chip_info;
	const struct ad7606_bus_ops	*bops;
};

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const struct ad7606_chip_info *info,
		 const struct ad7606_bus_ops *bops);

int ad7606_reset(struct ad7606_state *st);

extern const struct ad7606_chip_info ad7605_4_info;
extern const struct ad7606_chip_info ad7606_8_info;
extern const struct ad7606_chip_info ad7606_6_info;
extern const struct ad7606_chip_info ad7606_4_info;
extern const struct ad7606_chip_info ad7606b_info;
extern const struct ad7606_chip_info ad7606c_16_info;
extern const struct ad7606_chip_info ad7606c_18_info;
extern const struct ad7606_chip_info ad7607_info;
extern const struct ad7606_chip_info ad7608_info;
extern const struct ad7606_chip_info ad7609_info;
extern const struct ad7606_chip_info ad7616_info;

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops ad7606_pm_ops;
#define AD7606_PM_OPS (&ad7606_pm_ops)
#else
#define AD7606_PM_OPS NULL
#endif

#endif /* IIO_ADC_AD7606_H_ */
