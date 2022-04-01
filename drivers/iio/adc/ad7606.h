/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7606 ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#ifndef IIO_ADC_AD7606_H_
#define IIO_ADC_AD7606_H_

#define AD760X_CHANNEL(num, mask_sep, mask_type, mask_all) {	\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = num,					\
		.address = num,					\
		.info_mask_separate = mask_sep,			\
		.info_mask_shared_by_type = mask_type,		\
		.info_mask_shared_by_all = mask_all,		\
		.scan_index = num,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 16,				\
			.storagebits = 16,			\
			.endianness = IIO_CPU,			\
		},						\
}

#define AD7605_CHANNEL(num)				\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_RAW),	\
		BIT(IIO_CHAN_INFO_SCALE), 0)

#define AD7606_CHANNEL(num)				\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_RAW),	\
		BIT(IIO_CHAN_INFO_SCALE),		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO))

#define AD7616_CHANNEL(num)	\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),\
		0, BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO))

/**
 * struct ad7606_chip_info - chip specific information
 * @channels:		channel specification
 * @num_channels:	number of channels
 * @oversampling_avail	pointer to the array which stores the available
 *			oversampling ratios.
 * @oversampling_num	number of elements stored in oversampling_avail array
 * @os_req_reset	some devices require a reset to update oversampling
 * @init_delay_ms	required delay in miliseconds for initialization
 *			after a restart
 */
struct ad7606_chip_info {
	const struct iio_chan_spec	*channels;
	unsigned int			num_channels;
	const unsigned int		*oversampling_avail;
	unsigned int			oversampling_num;
	bool				os_req_reset;
	unsigned long			init_delay_ms;
};

/**
 * struct ad7606_state - driver instance specific data
 * @dev		pointer to kernel device
 * @chip_info		entry in the table of chips that describes this device
 * @reg		regulator info for the power supply of the device
 * @bops		bus operations (SPI or parallel)
 * @range		voltage range selection, selects which scale to apply
 * @oversampling	oversampling selection
 * @base_address	address from where to read data in parallel operation
 * @sw_mode_en		software mode enabled
 * @scale_avail		pointer to the array which stores the available scales
 * @num_scales		number of elements stored in the scale_avail array
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
	struct regulator		*reg;
	const struct ad7606_bus_ops	*bops;
	unsigned int			range[16];
	unsigned int			oversampling;
	void __iomem			*base_address;
	bool				sw_mode_en;
	const unsigned int		*scale_avail;
	unsigned int			num_scales;
	const unsigned int		*oversampling_avail;
	unsigned int			num_os_ratios;
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
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * 16 * 16-bit samples + 64-bit timestamp
	 */
	unsigned short			data[20] ____cacheline_aligned;
	__be16				d16[2];
};

/**
 * struct ad7606_bus_ops - driver bus operations
 * @read_block		function pointer for reading blocks of data
 * @sw_mode_config:	pointer to a function which configured the device
 *			for software mode
 * @reg_read	function pointer for reading spi register
 * @reg_write	function pointer for writing spi register
 * @write_mask	function pointer for write spi register with mask
 * @rd_wr_cmd	pointer to the function which calculates the spi address
 */
struct ad7606_bus_ops {
	/* more methods added in future? */
	int (*read_block)(struct device *dev, int num, void *data);
	int (*sw_mode_config)(struct iio_dev *indio_dev);
	int (*reg_read)(struct ad7606_state *st, unsigned int addr);
	int (*reg_write)(struct ad7606_state *st,
				unsigned int addr,
				unsigned int val);
	int (*write_mask)(struct ad7606_state *st,
				 unsigned int addr,
				 unsigned long mask,
				 unsigned int val);
	u16 (*rd_wr_cmd)(int addr, char isWriteOp);
};

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const char *name, unsigned int id,
		 const struct ad7606_bus_ops *bops);

enum ad7606_supported_device_ids {
	ID_AD7605_4,
	ID_AD7606_8,
	ID_AD7606_6,
	ID_AD7606_4,
	ID_AD7606B,
	ID_AD7616,
};

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops ad7606_pm_ops;
#define AD7606_PM_OPS (&ad7606_pm_ops)
#else
#define AD7606_PM_OPS NULL
#endif

#endif /* IIO_ADC_AD7606_H_ */
