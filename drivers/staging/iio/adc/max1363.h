#ifndef _MAX1363_H_
#define  _MAX1363_H_

#define MAX1363_SETUP_BYTE(a) ((a) | 0x80)

/* There is a fair bit more defined here than currently
 * used, but the intention is to support everything these
 * chips do in the long run */

/* see data sheets */
/* max1363 and max1236, max1237, max1238, max1239 */
#define MAX1363_SETUP_AIN3_IS_AIN3_REF_IS_VDD	0x00
#define MAX1363_SETUP_AIN3_IS_REF_EXT_TO_REF	0x20
#define MAX1363_SETUP_AIN3_IS_AIN3_REF_IS_INT	0x40
#define MAX1363_SETUP_AIN3_IS_REF_REF_IS_INT	0x60
#define MAX1363_SETUP_POWER_UP_INT_REF		0x10
#define MAX1363_SETUP_POWER_DOWN_INT_REF	0x00

/* think about includeing max11600 etc - more settings */
#define MAX1363_SETUP_EXT_CLOCK			0x08
#define MAX1363_SETUP_INT_CLOCK			0x00
#define MAX1363_SETUP_UNIPOLAR			0x00
#define MAX1363_SETUP_BIPOLAR			0x04
#define MAX1363_SETUP_RESET			0x00
#define MAX1363_SETUP_NORESET			0x02
/* max1363 only - though don't care on others.
 * For now monitor modes are not implemented as the relevant
 * line is not connected on my test board.
 * The definitions are here as I intend to add this soon.
 */
#define MAX1363_SETUP_MONITOR_SETUP		0x01

/* Specific to the max1363 */
#define MAX1363_MON_RESET_CHAN(a) (1 << ((a) + 4))
#define MAX1363_MON_INT_ENABLE			0x01

/* defined for readability reasons */
/* All chips */
#define MAX1363_CONFIG_BYTE(a) ((a))

#define MAX1363_CONFIG_SE			0x01
#define MAX1363_CONFIG_DE			0x00
#define MAX1363_CONFIG_SCAN_TO_CS		0x00
#define MAX1363_CONFIG_SCAN_SINGLE_8		0x20
#define MAX1363_CONFIG_SCAN_MONITOR_MODE	0x40
#define MAX1363_CONFIG_SCAN_SINGLE_1		0x60
/* max123{6-9} only */
#define MAX1236_SCAN_MID_TO_CHANNEL		0x40

/* max1363 only - merely part of channel selects or don't care for others*/
#define MAX1363_CONFIG_EN_MON_MODE_READ 0x18

#define MAX1363_CHANNEL_SEL(a) ((a) << 1)

/* max1363 strictly 0x06 - but doesn't matter */
#define MAX1363_CHANNEL_SEL_MASK		0x1E
#define MAX1363_SCAN_MASK			0x60
#define MAX1363_SE_DE_MASK			0x01

/**
 * struct max1363_mode - scan mode information
 * @conf:	The corresponding value of the configuration register
 * @modemask:	Bit mask corresponding to channels enabled in this mode
 */
struct max1363_mode {
	int8_t		conf;
	long		modemask;
};

#define MAX1363_MODE_SINGLE(_num, _mask) {				\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_SINGLE_1			\
			| MAX1363_CONFIG_SE,				\
			.modemask = _mask,				\
			}

#define MAX1363_MODE_SCAN_TO_CHANNEL(_num, _mask) {			\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_TO_CS			\
			| MAX1363_CONFIG_SE,				\
			.modemask = _mask,				\
			}


/* note not available for max1363 hence naming */
#define MAX1236_MODE_SCAN_MID_TO_CHANNEL(_mid, _num, _mask) {		\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1236_SCAN_MID_TO_CHANNEL			\
			| MAX1363_CONFIG_SE,				\
			.modemask = _mask				\
}

#define MAX1363_MODE_DIFF_SINGLE(_nump, _numm, _mask) {			\
		.conf = MAX1363_CHANNEL_SEL(_nump)			\
			| MAX1363_CONFIG_SCAN_SINGLE_1			\
			| MAX1363_CONFIG_DE,				\
			.modemask = _mask				\
			}

/* Can't think how to automate naming so specify for now */
#define MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(_num, _numvals, _mask) { \
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_TO_CS			\
			| MAX1363_CONFIG_DE,				\
			.modemask = _mask				\
			}

/* note only available for max1363 hence naming */
#define MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(_num, _numvals, _mask) { \
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1236_SCAN_MID_TO_CHANNEL			\
			| MAX1363_CONFIG_SE,				\
			.modemask = _mask				\
}

/* This may seem an overly long winded way to do this, but at least it makes
 * clear what all the various options actually do. Alternative suggestions
 * that don't require user to have intimate knowledge of the chip welcomed.
 */
enum max1363_channels {
	max1363_in0, max1363_in1, max1363_in2, max1363_in3,
	max1363_in4, max1363_in5, max1363_in6, max1363_in7,
	max1363_in8, max1363_in9, max1363_in10, max1363_in11,

	max1363_in0min1, max1363_in2min3,
	max1363_in4min5, max1363_in6min7,
	max1363_in8min9, max1363_in10min11,

	max1363_in1min0, max1363_in3min2,
	max1363_in5min4, max1363_in7min6,
	max1363_in9min8, max1363_in11min10,
};

/* This must be maintained along side the max1363_mode_table in max1363_core */
enum max1363_modes {
	/* Single read of a single channel */
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _s8, _s9, _s10, _s11,
	/* Differential single read */
	d0m1, d2m3, d4m5, d6m7, d8m9, d10m11,
	d1m0, d3m2, d5m4, d7m6, d9m8, d11m10,
	/* Scan to channel and mid to channel where overlapping */
	s0to1, s0to2, s2to3, s0to3, s0to4, s0to5, s0to6,
	s6to7, s0to7, s6to8, s0to8, s6to9,
	s0to9, s6to10, s0to10, s6to11, s0to11,
	/* Differential scan to channel and mid to channel where overlapping */
	d0m1to2m3, d0m1to4m5, d0m1to6m7, d6m7to8m9,
	d0m1to8m9, d6m7to10m11, d0m1to10m11, d1m0to3m2,
	d1m0to5m4, d1m0to7m6, d7m6to9m8, d1m0to9m8,
	d7m6to11m10, d1m0to11m10,
};

/**
 * struct max1363_chip_info - chip specifc information
 * @name:		indentification string for chip
 * @num_inputs:		number of physical inputs on chip
 * @bits:		accuracy of the adc in bits
 * @int_vref_mv:	the internal reference voltage
 * @monitor_mode:	whether the chip supports monitor interrupts
 * @mode_list:		array of available scan modes
 * @num_modes:		the number of scan modes available
 * @default_mode:	the scan mode in which the chip starts up
 */
struct max1363_chip_info {
	u8				num_inputs;
	u8				bits;
	u16				int_vref_mv;
	bool				monitor_mode;
	const enum max1363_modes	*mode_list;
	int				num_modes;
	enum max1363_modes		default_mode;
	struct attribute_group		*dev_attrs;
	struct attribute_group		*scan_attrs;
};

/**
 * struct max1363_state - driver instance specific data
 * @indio_dev:		the industrial I/O device
 * @client:		i2c_client
 * @setupbyte:		cache of current device setup byte
 * @configbyte:		cache of current device config byte
 * @chip_info:		chip model specific constants, available modes etc
 * @current_mode:	the scan mode of this chip
 * @requestedmask:	a valid requested set of channels
 * @poll_work:		bottom half of polling interrupt handler
 * @protect_ring:	used to ensure only one polling bh running at a time
 * @reg:		supply regulator
 * @monitor_on:		whether monitor mode is enabled
 * @monitor_speed:	parameter corresponding to device monitor speed setting
 * @mask_high:		bitmask for enabled high thresholds
 * @mask_low:		bitmask for enabled low thresholds
 * @thresh_high:	high threshold values
 * @thresh_low:		low threshold values
 * @last_timestamp:	timestamp of last event interrupt
 * @thresh_work:	bh work structure for event handling
 */
struct max1363_state {
	struct iio_dev			*indio_dev;
	struct i2c_client		*client;
	u8				setupbyte;
	u8				configbyte;
	const struct max1363_chip_info	*chip_info;
	const struct max1363_mode	*current_mode;
	u32				requestedmask;
	struct work_struct		poll_work;
	atomic_t			protect_ring;
	struct iio_trigger		*trig;
	struct regulator		*reg;

	/* Using monitor modes and buffer at the same time is
	   currently not supported */
	bool				monitor_on;
	unsigned int			monitor_speed:3;
	u8				mask_high;
	u8				mask_low;
	/* 4x unipolar first then the fours bipolar ones */
	s16				thresh_high[8];
	s16				thresh_low[8];
	s64				last_timestamp;
	struct work_struct		thresh_work;
};

const struct max1363_mode
*max1363_match_mode(u32 mask, const struct max1363_chip_info *ci);

int max1363_set_scan_mode(struct max1363_state *st);

#ifdef CONFIG_MAX1363_RING_BUFFER

int max1363_single_channel_from_ring(long mask, struct max1363_state *st);
int max1363_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void max1363_ring_cleanup(struct iio_dev *indio_dev);

#else /* CONFIG_MAX1363_RING_BUFFER */

int max1363_single_channel_from_ring(long mask, struct max1363_state *st)
{
	return -EINVAL;
}

static inline int
max1363_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void max1363_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_MAX1363_RING_BUFFER */
#endif /* _MAX1363_H_ */
