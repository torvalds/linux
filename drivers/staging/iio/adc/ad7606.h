/*
 * AD7606 ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef IIO_ADC_AD7606_H_
#define IIO_ADC_AD7606_H_

/**
 * struct ad7606_chip_info - chip specific information
 * @channels:		channel specification
 * @num_channels:	number of channels
 */

struct ad7606_chip_info {
	const struct iio_chan_spec	*channels;
	unsigned int			num_channels;
};

/**
 * struct ad7606_state - driver instance specific data
 * @lock		protect sensor state
 */

struct ad7606_state {
	struct device			*dev;
	const struct ad7606_chip_info	*chip_info;
	struct regulator		*reg;
	struct work_struct		poll_work;
	wait_queue_head_t		wq_data_avail;
	const struct ad7606_bus_ops	*bops;
	unsigned int			range;
	unsigned int			oversampling;
	bool				done;
	void __iomem			*base_address;

	struct mutex			lock; /* protect sensor state */
	struct gpio_desc		*gpio_convst;
	struct gpio_desc		*gpio_reset;
	struct gpio_desc		*gpio_range;
	struct gpio_desc		*gpio_standby;
	struct gpio_desc		*gpio_frstdata;
	struct gpio_descs		*gpio_os;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * 8 * 16-bit samples + 64-bit timestamp
	 */
	unsigned short			data[12] ____cacheline_aligned;
};

struct ad7606_bus_ops {
	/* more methods added in future? */
	int (*read_block)(struct device *dev, int num, void *data);
};

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const char *name, unsigned int id,
		 const struct ad7606_bus_ops *bops);
int ad7606_remove(struct device *dev, int irq);

enum ad7606_supported_device_ids {
	ID_AD7606_8,
	ID_AD7606_6,
	ID_AD7606_4
};

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops ad7606_pm_ops;
#define AD7606_PM_OPS (&ad7606_pm_ops)
#else
#define AD7606_PM_OPS NULL
#endif

#endif /* IIO_ADC_AD7606_H_ */
