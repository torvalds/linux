/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7606 ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#ifndef IIO_ADC_AD7606_H_
#define IIO_ADC_AD7606_H_

/**
 * struct ad7606_chip_info - chip specific information
 * @channels:		channel specification
 * @num_channels:	number of channels
 * @oversampling_avail	pointer to the array which stores the available
 *			oversampling ratios.
 * @oversampling_num	number of elements stored in oversampling_avail array
 * @os_req_reset	some devices require a reset to update oversampling
 */
struct ad7606_chip_info {
	const struct iio_chan_spec	*channels;
	unsigned int			num_channels;
	const unsigned int		*oversampling_avail;
	unsigned int			oversampling_num;
	bool				os_req_reset;
};

/**
 * struct ad7606_state - driver instance specific data
 * @dev		pointer to kernel device
 * @chip_info		entry in the table of chips that describes this device
 * @reg		regulator info for the the power supply of the device
 * @bops		bus operations (SPI or parallel)
 * @range		voltage range selection, selects which scale to apply
 * @oversampling	oversampling selection
 * @base_address	address from where to read data in parallel operation
 * @scale_avail		pointer to the array which stores the available scales
 * @num_scales		number of elements stored in the scale_avail array
 * @oversampling_avail	pointer to the array which stores the available
 *			oversampling ratios.
 * @num_os_ratios	number of elements stored in oversampling_avail array
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
 */
struct ad7606_state {
	struct device			*dev;
	const struct ad7606_chip_info	*chip_info;
	struct regulator		*reg;
	const struct ad7606_bus_ops	*bops;
	unsigned int			range;
	unsigned int			oversampling;
	void __iomem			*base_address;
	const unsigned int		*scale_avail;
	unsigned int			num_scales;
	const unsigned int		*oversampling_avail;
	unsigned int			num_os_ratios;

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
};

/**
 * struct ad7606_bus_ops - driver bus operations
 * @read_block		function pointer for reading blocks of data
 */
struct ad7606_bus_ops {
	/* more methods added in future? */
	int (*read_block)(struct device *dev, int num, void *data);
};

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const char *name, unsigned int id,
		 const struct ad7606_bus_ops *bops);

enum ad7606_supported_device_ids {
	ID_AD7605_4,
	ID_AD7606_8,
	ID_AD7606_6,
	ID_AD7606_4,
	ID_AD7616,
};

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops ad7606_pm_ops;
#define AD7606_PM_OPS (&ad7606_pm_ops)
#else
#define AD7606_PM_OPS NULL
#endif

#endif /* IIO_ADC_AD7606_H_ */
