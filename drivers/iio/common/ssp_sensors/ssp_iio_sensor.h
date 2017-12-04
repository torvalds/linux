/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SSP_IIO_SENSOR_H__
#define __SSP_IIO_SENSOR_H__

#define SSP_CHANNEL_AG(_type, _mod, _index) \
{ \
		.type = _type,\
		.modified = 1,\
		.channel2 = _mod,\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.scan_index = _index,\
		.scan_type = {\
			.sign = 's',\
			.realbits = 16,\
			.storagebits = 16,\
			.shift = 0,\
			.endianness = IIO_LE,\
		},\
}

/* It is defined here as it is a mixed timestamp */
#define SSP_CHAN_TIMESTAMP(_si) {					\
	.type = IIO_TIMESTAMP,						\
	.channel = -1,							\
	.scan_index = _si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,						\
		.storagebits = 64,					\
		},							\
}

#define SSP_MS_PER_S			1000
#define SSP_INVERTED_SCALING_FACTOR	1000000U

#define SSP_FACTOR_WITH_MS \
	(SSP_INVERTED_SCALING_FACTOR * SSP_MS_PER_S)

int ssp_common_buffer_postenable(struct iio_dev *indio_dev);

int ssp_common_buffer_postdisable(struct iio_dev *indio_dev);

int ssp_common_process_data(struct iio_dev *indio_dev, void *buf,
			    unsigned int len, int64_t timestamp);

/* Converts time in ms to frequency */
static inline void ssp_convert_to_freq(u32 time, int *integer_part,
				       int *fractional)
{
	if (time == 0) {
		*fractional = 0;
		*integer_part = 0;
		return;
	}

	*integer_part = SSP_FACTOR_WITH_MS / time;
	*fractional = *integer_part % SSP_INVERTED_SCALING_FACTOR;
	*integer_part = *integer_part / SSP_INVERTED_SCALING_FACTOR;
}

/* Converts frequency to time in ms */
static inline int ssp_convert_to_time(int integer_part, int fractional)
{
	u64 value;

	value = (u64)integer_part * SSP_INVERTED_SCALING_FACTOR + fractional;
	if (value == 0)
		return 0;

	return div64_u64((u64)SSP_FACTOR_WITH_MS, value);
}
#endif /* __SSP_IIO_SENSOR_H__ */
