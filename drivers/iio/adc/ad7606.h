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

#define AD7606_CALIB_GAIN(ch)		(0x09 + (ch))
#define AD7606_CALIB_GAIN_MASK		GENMASK(5, 0)
#define AD7606_CALIB_OFFSET(ch)		(0x11 + (ch))
#define AD7606_CALIB_PHASE(ch)		(0x19 + (ch))

struct ad7606_state;

typedef int (*ad7606_scale_setup_cb_t)(struct iio_dev *indio_dev,
				       struct iio_chan_spec *chan);
typedef int (*ad7606_sw_setup_cb_t)(struct iio_dev *indio_dev);

/**
 * struct ad7606_chip_info - chip specific information
 * @max_samplerate:	maximum supported sample rate
 * @name:		device name
 * @bits:		data width in bits
 * @num_adc_channels:	the number of physical voltage inputs
 * @scale_setup_cb:	callback to setup the scales for each channel
 * @sw_setup_cb:	callback to setup the software mode if available.
 * @oversampling_avail:	pointer to the array which stores the available
 *			oversampling ratios.
 * @oversampling_num:	number of elements stored in oversampling_avail array
 * @os_req_reset:	some devices require a reset to update oversampling
 * @init_delay_ms:	required delay in milliseconds for initialization
 *			after a restart
 * @offload_storagebits: storage bits used by the offload hw implementation
 * @calib_gain_avail:   chip supports gain calibration
 * @calib_offset_avail: pointer to offset calibration range/limits array
 * @calib_phase_avail:  pointer to phase calibration range/limits array
 */
struct ad7606_chip_info {
	unsigned int			max_samplerate;
	const char			*name;
	unsigned int			bits;
	unsigned int			num_adc_channels;
	ad7606_scale_setup_cb_t		scale_setup_cb;
	ad7606_sw_setup_cb_t		sw_setup_cb;
	const unsigned int		*oversampling_avail;
	unsigned int			oversampling_num;
	bool				os_req_reset;
	unsigned long			init_delay_ms;
	u8				offload_storagebits;
	bool				calib_gain_avail;
	const int			*calib_offset_avail;
	const int			(*calib_phase_avail)[2];
};

/**
 * struct ad7606_chan_info - channel configuration
 * @scale_avail:	pointer to the array which stores the available scales
 * @num_scales:		number of elements stored in the scale_avail array
 * @range:		voltage range selection, selects which scale to apply
 * @reg_offset:		offset for the register value, to be applied when
 *			writing the value of 'range' to the register value
 * @r_gain:		gain resistor value in ohms, to be set to match the
 *                      external r_filter value
 */
struct ad7606_chan_info {
#define AD760X_MAX_SCALES		16
	const unsigned int		(*scale_avail)[2];
	unsigned int			num_scales;
	unsigned int			range;
	unsigned int			reg_offset;
	unsigned int			r_gain;
};

/**
 * struct ad7606_state - driver instance specific data
 * @dev:		pointer to kernel device
 * @chip_info:		entry in the table of chips that describes this device
 * @bops:		bus operations (SPI or parallel)
 * @chan_info:		scale configuration for channels
 * @oversampling:	oversampling selection
 * @cnvst_pwm:		pointer to the PWM device connected to the cnvst pin
 * @base_address:	address from where to read data in parallel operation
 * @sw_mode_en:		software mode enabled
 * @oversampling_avail:	pointer to the array which stores the available
 *			oversampling ratios.
 * @num_os_ratios:	number of elements stored in oversampling_avail array
 * @back:		pointer to the iio_backend structure, if used
 * @write_scale:	pointer to the function which writes the scale
 * @write_os:		pointer to the function which writes the os
 * @lock:		protect sensor state from concurrent accesses to GPIOs
 * @gpio_convst:	GPIO descriptor for conversion start signal (CONVST)
 * @gpio_reset:		GPIO descriptor for device hard-reset
 * @gpio_range:		GPIO descriptor for range selection
 * @gpio_standby:	GPIO descriptor for stand-by signal (STBY),
 *			controls power-down mode of device
 * @gpio_frstdata:	GPIO descriptor for reading from device when data
 *			is being read on the first channel
 * @gpio_os:		GPIO descriptors to control oversampling on the device
 * @trig:		The IIO trigger associated with the device.
 * @completion:		completion to indicate end of conversion
 * @data:		buffer for reading data from the device
 * @offload_en:		SPI offload enabled
 * @bus_data:		bus-specific variables
 * @d16:		be16 buffer for reading data from the device
 */
struct ad7606_state {
	struct device			*dev;
	const struct ad7606_chip_info	*chip_info;
	const struct ad7606_bus_ops	*bops;
	struct ad7606_chan_info		chan_info[AD760X_MAX_CHANNELS];
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

	bool				offload_en;
	void				*bus_data;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 * 16 * 16-bit samples for AD7616
	 * 8 * 32-bit samples for AD7616C-18 (and similar)
	 */
	struct {
		union {
			u16 buf16[16];
			u32 buf32[8];
		};
		aligned_s64 timestamp;
	} data __aligned(IIO_DMA_MINALIGN);
	__be16				d16[2];
};

/**
 * struct ad7606_bus_ops - driver bus operations
 * @iio_backend_config:	function pointer for configuring the iio_backend for
 *			the compatibles that use it
 * @read_block:		function pointer for reading blocks of data
 * @sw_mode_config:	pointer to a function which configured the device
 *			for software mode
 * @offload_config:     function pointer for configuring offload support,
 *			where any
 * @reg_read:		function pointer for reading spi register
 * @reg_write:		function pointer for writing spi register
 * @update_scan_mode:	function pointer for handling the calls to iio_info's
 *			update_scan mode when enabling/disabling channels.
 * @rd_wr_cmd:		pointer to the function which calculates the spi address
 */
struct ad7606_bus_ops {
	/* more methods added in future? */
	int (*iio_backend_config)(struct device *dev, struct iio_dev *indio_dev);
	int (*offload_config)(struct device *dev, struct iio_dev *indio_dev);
	int (*read_block)(struct device *dev, int num, void *data);
	int (*sw_mode_config)(struct iio_dev *indio_dev);
	int (*reg_read)(struct ad7606_state *st, unsigned int addr);
	int (*reg_write)(struct ad7606_state *st,
				unsigned int addr,
				unsigned int val);
	int (*update_scan_mode)(struct iio_dev *indio_dev, const unsigned long *scan_mask);
	u16 (*rd_wr_cmd)(int addr, char is_write_op);
};

/**
 * struct ad7606_bus_info - aggregate ad7606_chip_info and ad7606_bus_ops
 * @chip_info:		entry in the table of chips that describes this device
 * @bops:		bus operations (SPI or parallel)
 */
struct ad7606_bus_info {
	const struct ad7606_chip_info	*chip_info;
	const struct ad7606_bus_ops	*bops;
};

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const struct ad7606_chip_info *info,
		 const struct ad7606_bus_ops *bops);

int ad7606_reset(struct ad7606_state *st);
int ad7606_pwm_set_swing(struct ad7606_state *st);
int ad7606_pwm_set_low(struct ad7606_state *st);

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
