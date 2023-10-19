/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the MaxLinear MxL69x family of combo tuners/demods
 *
 * Copyright (C) 2020 Brad Love <brad@nextdimension.cc>
 *
 * based on code:
 * Copyright (c) 2016 MaxLinear, Inc. All rights reserved
 * which was released under GPL V2
 */

/*****************************************************************************
 *	Defines
 *****************************************************************************
 */
#define MXL_EAGLE_HOST_MSG_HEADER_SIZE  8
#define MXL_EAGLE_FW_MAX_SIZE_IN_KB     76
#define MXL_EAGLE_QAM_FFE_TAPS_LENGTH   16
#define MXL_EAGLE_QAM_SPUR_TAPS_LENGTH  32
#define MXL_EAGLE_QAM_DFE_TAPS_LENGTH   72
#define MXL_EAGLE_ATSC_FFE_TAPS_LENGTH  4096
#define MXL_EAGLE_ATSC_DFE_TAPS_LENGTH  384
#define MXL_EAGLE_VERSION_SIZE          5     /* A.B.C.D-RCx */
#define MXL_EAGLE_FW_LOAD_TIME          50

#define MXL_EAGLE_FW_MAX_SIZE_IN_KB       76
#define MXL_EAGLE_FW_HEADER_SIZE          16
#define MXL_EAGLE_FW_SEGMENT_HEADER_SIZE  8
#define MXL_EAGLE_MAX_I2C_PACKET_SIZE     58
#define MXL_EAGLE_I2C_MHEADER_SIZE        6
#define MXL_EAGLE_I2C_PHEADER_SIZE        2

/* Enum of Eagle family devices */
enum MXL_EAGLE_DEVICE_E {
	MXL_EAGLE_DEVICE_691 = 1,    /* Device Mxl691 */
	MXL_EAGLE_DEVICE_248 = 2,    /* Device Mxl248 */
	MXL_EAGLE_DEVICE_692 = 3,    /* Device Mxl692 */
	MXL_EAGLE_DEVICE_MAX,        /* No such device */
};

#define VER_A   1
#define VER_B   1
#define VER_C   1
#define VER_D   3
#define VER_E   6

/* Enum of Host to Eagle I2C protocol opcodes */
enum MXL_EAGLE_OPCODE_E {
	/* DEVICE */
	MXL_EAGLE_OPCODE_DEVICE_DEMODULATOR_TYPE_SET,
	MXL_EAGLE_OPCODE_DEVICE_MPEG_OUT_PARAMS_SET,
	MXL_EAGLE_OPCODE_DEVICE_POWERMODE_SET,
	MXL_EAGLE_OPCODE_DEVICE_GPIO_DIRECTION_SET,
	MXL_EAGLE_OPCODE_DEVICE_GPO_LEVEL_SET,
	MXL_EAGLE_OPCODE_DEVICE_INTR_MASK_SET,
	MXL_EAGLE_OPCODE_DEVICE_IO_MUX_SET,
	MXL_EAGLE_OPCODE_DEVICE_VERSION_GET,
	MXL_EAGLE_OPCODE_DEVICE_STATUS_GET,
	MXL_EAGLE_OPCODE_DEVICE_GPI_LEVEL_GET,

	/* TUNER */
	MXL_EAGLE_OPCODE_TUNER_CHANNEL_TUNE_SET,
	MXL_EAGLE_OPCODE_TUNER_LOCK_STATUS_GET,
	MXL_EAGLE_OPCODE_TUNER_AGC_STATUS_GET,

	/* ATSC */
	MXL_EAGLE_OPCODE_ATSC_INIT_SET,
	MXL_EAGLE_OPCODE_ATSC_ACQUIRE_CARRIER_SET,
	MXL_EAGLE_OPCODE_ATSC_STATUS_GET,
	MXL_EAGLE_OPCODE_ATSC_ERROR_COUNTERS_GET,
	MXL_EAGLE_OPCODE_ATSC_EQUALIZER_FILTER_DFE_TAPS_GET,
	MXL_EAGLE_OPCODE_ATSC_EQUALIZER_FILTER_FFE_TAPS_GET,

	/* QAM */
	MXL_EAGLE_OPCODE_QAM_PARAMS_SET,
	MXL_EAGLE_OPCODE_QAM_RESTART_SET,
	MXL_EAGLE_OPCODE_QAM_STATUS_GET,
	MXL_EAGLE_OPCODE_QAM_ERROR_COUNTERS_GET,
	MXL_EAGLE_OPCODE_QAM_CONSTELLATION_VALUE_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_FFE_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_SPUR_START_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_SPUR_END_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_TAPS_NUMBER_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_START_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_MIDDLE_GET,
	MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_END_GET,

	/* OOB */
	MXL_EAGLE_OPCODE_OOB_PARAMS_SET,
	MXL_EAGLE_OPCODE_OOB_RESTART_SET,
	MXL_EAGLE_OPCODE_OOB_ERROR_COUNTERS_GET,
	MXL_EAGLE_OPCODE_OOB_STATUS_GET,

	/* SMA */
	MXL_EAGLE_OPCODE_SMA_INIT_SET,
	MXL_EAGLE_OPCODE_SMA_PARAMS_SET,
	MXL_EAGLE_OPCODE_SMA_TRANSMIT_SET,
	MXL_EAGLE_OPCODE_SMA_RECEIVE_GET,

	/* DEBUG */
	MXL_EAGLE_OPCODE_INTERNAL,

	MXL_EAGLE_OPCODE_MAX = 70,
};

/* Enum of Host to Eagle I2C protocol opcodes */
static const char * const MXL_EAGLE_OPCODE_STRING[] = {
	/* DEVICE */
	"DEVICE_DEMODULATOR_TYPE_SET",
	"DEVICE_MPEG_OUT_PARAMS_SET",
	"DEVICE_POWERMODE_SET",
	"DEVICE_GPIO_DIRECTION_SET",
	"DEVICE_GPO_LEVEL_SET",
	"DEVICE_INTR_MASK_SET",
	"DEVICE_IO_MUX_SET",
	"DEVICE_VERSION_GET",
	"DEVICE_STATUS_GET",
	"DEVICE_GPI_LEVEL_GET",

	/* TUNER */
	"TUNER_CHANNEL_TUNE_SET",
	"TUNER_LOCK_STATUS_GET",
	"TUNER_AGC_STATUS_GET",

	/* ATSC */
	"ATSC_INIT_SET",
	"ATSC_ACQUIRE_CARRIER_SET",
	"ATSC_STATUS_GET",
	"ATSC_ERROR_COUNTERS_GET",
	"ATSC_EQUALIZER_FILTER_DFE_TAPS_GET",
	"ATSC_EQUALIZER_FILTER_FFE_TAPS_GET",

	/* QAM */
	"QAM_PARAMS_SET",
	"QAM_RESTART_SET",
	"QAM_STATUS_GET",
	"QAM_ERROR_COUNTERS_GET",
	"QAM_CONSTELLATION_VALUE_GET",
	"QAM_EQUALIZER_FILTER_FFE_GET",
	"QAM_EQUALIZER_FILTER_SPUR_START_GET",
	"QAM_EQUALIZER_FILTER_SPUR_END_GET",
	"QAM_EQUALIZER_FILTER_DFE_TAPS_NUMBER_GET",
	"QAM_EQUALIZER_FILTER_DFE_START_GET",
	"QAM_EQUALIZER_FILTER_DFE_MIDDLE_GET",
	"QAM_EQUALIZER_FILTER_DFE_END_GET",

	/* OOB */
	"OOB_PARAMS_SET",
	"OOB_RESTART_SET",
	"OOB_ERROR_COUNTERS_GET",
	"OOB_STATUS_GET",

	/* SMA */
	"SMA_INIT_SET",
	"SMA_PARAMS_SET",
	"SMA_TRANSMIT_SET",
	"SMA_RECEIVE_GET",

	/* DEBUG */
	"INTERNAL",
};

/* Enum of Callabck function types */
enum MXL_EAGLE_CB_TYPE_E {
	MXL_EAGLE_CB_FW_DOWNLOAD = 0,
};

/* Enum of power supply types */
enum MXL_EAGLE_POWER_SUPPLY_SOURCE_E {
	MXL_EAGLE_POWER_SUPPLY_SOURCE_SINGLE,   /* Single supply of 3.3V */
	MXL_EAGLE_POWER_SUPPLY_SOURCE_DUAL,     /* Dual supply, 1.8V & 3.3V */
};

/* Enum of I/O pad drive modes */
enum MXL_EAGLE_IO_MUX_DRIVE_MODE_E {
	MXL_EAGLE_IO_MUX_DRIVE_MODE_1X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_2X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_3X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_4X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_5X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_6X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_7X,
	MXL_EAGLE_IO_MUX_DRIVE_MODE_8X,
};

/* Enum of demodulator types. Used for selection of demodulator
 * type in relevant devices, e.g. ATSC vs. QAM in Mxl691
 */
enum MXL_EAGLE_DEMOD_TYPE_E {
	MXL_EAGLE_DEMOD_TYPE_QAM,    /* Mxl248 or Mxl692 */
	MXL_EAGLE_DEMOD_TYPE_OOB,    /* Mxl248 only */
	MXL_EAGLE_DEMOD_TYPE_ATSC    /* Mxl691 or Mxl692 */
};

/* Enum of power modes. Used for initial
 * activation, or for activating sleep mode
 */
enum MXL_EAGLE_POWER_MODE_E {
	MXL_EAGLE_POWER_MODE_SLEEP,
	MXL_EAGLE_POWER_MODE_ACTIVE
};

/* Enum of GPIOs, used in device GPIO APIs */
enum MXL_EAGLE_GPIO_NUMBER_E {
	MXL_EAGLE_GPIO_NUMBER_0,
	MXL_EAGLE_GPIO_NUMBER_1,
	MXL_EAGLE_GPIO_NUMBER_2,
	MXL_EAGLE_GPIO_NUMBER_3,
	MXL_EAGLE_GPIO_NUMBER_4,
	MXL_EAGLE_GPIO_NUMBER_5,
	MXL_EAGLE_GPIO_NUMBER_6
};

/* Enum of GPIO directions, used in GPIO direction configuration API */
enum MXL_EAGLE_GPIO_DIRECTION_E {
	MXL_EAGLE_GPIO_DIRECTION_INPUT,
	MXL_EAGLE_GPIO_DIRECTION_OUTPUT
};

/* Enum of GPIO level, used in device GPIO APIs */
enum MXL_EAGLE_GPIO_LEVEL_E {
	MXL_EAGLE_GPIO_LEVEL_LOW,
	MXL_EAGLE_GPIO_LEVEL_HIGH,
};

/* Enum of I/O Mux function, used in device I/O mux configuration API */
enum MXL_EAGLE_IOMUX_FUNCTION_E {
	MXL_EAGLE_IOMUX_FUNC_FEC_LOCK,
	MXL_EAGLE_IOMUX_FUNC_MERR,
};

/* Enum of MPEG Data format, used in MPEG and OOB output configuration */
enum MXL_EAGLE_MPEG_DATA_FORMAT_E {
	MXL_EAGLE_DATA_SERIAL_LSB_1ST = 0,
	MXL_EAGLE_DATA_SERIAL_MSB_1ST,

	MXL_EAGLE_DATA_SYNC_WIDTH_BIT = 0,
	MXL_EAGLE_DATA_SYNC_WIDTH_BYTE
};

/* Enum of MPEG Clock format, used in MPEG and OOB output configuration */
enum MXL_EAGLE_MPEG_CLOCK_FORMAT_E {
	MXL_EAGLE_CLOCK_ACTIVE_HIGH = 0,
	MXL_EAGLE_CLOCK_ACTIVE_LOW,

	MXL_EAGLE_CLOCK_POSITIVE  = 0,
	MXL_EAGLE_CLOCK_NEGATIVE,

	MXL_EAGLE_CLOCK_IN_PHASE = 0,
	MXL_EAGLE_CLOCK_INVERTED,
};

/* Enum of MPEG Clock speeds, used in MPEG output configuration */
enum MXL_EAGLE_MPEG_CLOCK_RATE_E {
	MXL_EAGLE_MPEG_CLOCK_54MHZ,
	MXL_EAGLE_MPEG_CLOCK_40_5MHZ,
	MXL_EAGLE_MPEG_CLOCK_27MHZ,
	MXL_EAGLE_MPEG_CLOCK_13_5MHZ,
};

/* Enum of Interrupt mask bit, used in host interrupt configuration */
enum MXL_EAGLE_INTR_MASK_BITS_E {
	MXL_EAGLE_INTR_MASK_DEMOD = 0,
	MXL_EAGLE_INTR_MASK_SMA_RX = 1,
	MXL_EAGLE_INTR_MASK_WDOG = 31
};

/* Enum of QAM Demodulator type, used in QAM configuration */
enum MXL_EAGLE_QAM_DEMOD_ANNEX_TYPE_E {
	MXL_EAGLE_QAM_DEMOD_ANNEX_B,    /* J.83B */
	MXL_EAGLE_QAM_DEMOD_ANNEX_A,    /* DVB-C */
};

/* Enum of QAM Demodulator modulation, used in QAM configuration and status */
enum MXL_EAGLE_QAM_DEMOD_QAM_TYPE_E {
	MXL_EAGLE_QAM_DEMOD_QAM16,
	MXL_EAGLE_QAM_DEMOD_QAM64,
	MXL_EAGLE_QAM_DEMOD_QAM256,
	MXL_EAGLE_QAM_DEMOD_QAM1024,
	MXL_EAGLE_QAM_DEMOD_QAM32,
	MXL_EAGLE_QAM_DEMOD_QAM128,
	MXL_EAGLE_QAM_DEMOD_QPSK,
	MXL_EAGLE_QAM_DEMOD_AUTO,
};

/* Enum of Demodulator IQ setup, used in QAM, OOB configuration and status */
enum MXL_EAGLE_IQ_FLIP_E {
	MXL_EAGLE_DEMOD_IQ_NORMAL,
	MXL_EAGLE_DEMOD_IQ_FLIPPED,
	MXL_EAGLE_DEMOD_IQ_AUTO,
};

/* Enum of OOB Demodulator symbol rates, used in OOB configuration */
enum MXL_EAGLE_OOB_DEMOD_SYMB_RATE_E {
	MXL_EAGLE_OOB_DEMOD_SYMB_RATE_0_772MHZ,  /* ANSI/SCTE 55-2 0.772 MHz */
	MXL_EAGLE_OOB_DEMOD_SYMB_RATE_1_024MHZ,  /* ANSI/SCTE 55-1 1.024 MHz */
	MXL_EAGLE_OOB_DEMOD_SYMB_RATE_1_544MHZ,  /* ANSI/SCTE 55-2 1.544 MHz */
};

/* Enum of tuner channel tuning mode */
enum MXL_EAGLE_TUNER_CHANNEL_TUNE_MODE_E {
	MXL_EAGLE_TUNER_CHANNEL_TUNE_MODE_VIEW,    /* Normal "view" mode */
	MXL_EAGLE_TUNER_CHANNEL_TUNE_MODE_SCAN,    /* Fast "scan" mode */
};

/* Enum of tuner bandwidth */
enum MXL_EAGLE_TUNER_BW_E {
	MXL_EAGLE_TUNER_BW_6MHZ,
	MXL_EAGLE_TUNER_BW_7MHZ,
	MXL_EAGLE_TUNER_BW_8MHZ,
};

/* Enum of tuner bandwidth */
enum MXL_EAGLE_JUNCTION_TEMPERATURE_E {
	MXL_EAGLE_JUNCTION_TEMPERATURE_BELOW_0_CELSIUS          = 0,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_0_TO_14_CELSIUS  = 1,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_14_TO_28_CELSIUS = 3,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_28_TO_42_CELSIUS = 2,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_42_TO_57_CELSIUS = 6,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_57_TO_71_CELSIUS = 7,
	MXL_EAGLE_JUNCTION_TEMPERATURE_BETWEEN_71_TO_85_CELSIUS = 5,
	MXL_EAGLE_JUNCTION_TEMPERATURE_ABOVE_85_CELSIUS         = 4,
};

/* Struct passed in optional callback used during FW download */
struct MXL_EAGLE_FW_DOWNLOAD_CB_PAYLOAD_T {
	u32  total_len;
	u32  downloaded_len;
};

/* Struct used of I2C protocol between host and Eagle, internal use only */
struct __packed MXL_EAGLE_HOST_MSG_HEADER_T {
	u8   opcode;
	u8   seqnum;
	u8   payload_size;
	u8   status;
	u32  checksum;
};

/* Device version information struct */
struct __packed MXL_EAGLE_DEV_VER_T {
	u8   chip_id;
	u8   firmware_ver[MXL_EAGLE_VERSION_SIZE];
	u8   mxlware_ver[MXL_EAGLE_VERSION_SIZE];
};

/* Xtal configuration struct */
struct __packed MXL_EAGLE_DEV_XTAL_T {
	u8   xtal_cap;           /* accepted range is 1..31 pF. Default is 26 */
	u8   clk_out_enable;
	u8   clk_out_div_enable;   /* clock out freq is xtal freq / 6 */
	u8   xtal_sharing_enable; /* if enabled set xtal_cap to 25 pF */
	u8   xtal_calibration_enable;  /* enable for master, disable for slave */
};

/* GPIO direction struct, internally used in GPIO configuration API */
struct __packed MXL_EAGLE_DEV_GPIO_DIRECTION_T {
	u8   gpio_number;
	u8   gpio_direction;
};

/* GPO level struct, internally used in GPIO configuration API */
struct __packed MXL_EAGLE_DEV_GPO_LEVEL_T {
	u8   gpio_number;
	u8   gpo_level;
};

/* Device Status struct */
struct MXL_EAGLE_DEV_STATUS_T {
	u8   temperature;
	u8   demod_type;
	u8   power_mode;
	u8   cpu_utilization_percent;
};

/* Device interrupt configuration struct */
struct __packed MXL_EAGLE_DEV_INTR_CFG_T {
	u32  intr_mask;
	u8   edge_trigger;
	u8   positive_trigger;
	u8   global_enable_interrupt;
};

/* MPEG pad drive parameters, used on MPEG output configuration */
/* See MXL_EAGLE_IO_MUX_DRIVE_MODE_E */
struct MXL_EAGLE_MPEG_PAD_DRIVE_T {
	u8   pad_drv_mpeg_syn;
	u8   pad_drv_mpeg_dat;
	u8   pad_drv_mpeg_val;
	u8   pad_drv_mpeg_clk;
};

/* MPEGOUT parameter struct, used in MPEG output configuration */
struct MXL_EAGLE_MPEGOUT_PARAMS_T {
	u8   mpeg_parallel;
	u8   msb_first;
	u8   mpeg_sync_pulse_width;    /* See MXL_EAGLE_MPEG_DATA_FORMAT_E */
	u8   mpeg_valid_pol;
	u8   mpeg_sync_pol;
	u8   mpeg_clk_pol;
	u8   mpeg3wire_mode_enable;
	u8   mpeg_clk_freq;
	struct MXL_EAGLE_MPEG_PAD_DRIVE_T mpeg_pad_drv;
};

/* QAM Demodulator parameters struct, used in QAM params configuration */
struct __packed MXL_EAGLE_QAM_DEMOD_PARAMS_T {
	u8   annex_type;
	u8   qam_type;
	u8   iq_flip;
	u8   search_range_idx;
	u8   spur_canceller_enable;
	u32  symbol_rate_hz;
	u32  symbol_rate_256qam_hz;
};

/* QAM Demodulator status */
struct MXL_EAGLE_QAM_DEMOD_STATUS_T {
	u8   annex_type;
	u8   qam_type;
	u8   iq_flip;
	u8   interleaver_depth_i;
	u8   interleaver_depth_j;
	u8   qam_locked;
	u8   fec_locked;
	u8   mpeg_locked;
	u16  snr_db_tenths;
	s16  timing_offset;
	s32  carrier_offset_hz;
};

/* QAM Demodulator error counters */
struct MXL_EAGLE_QAM_DEMOD_ERROR_COUNTERS_T {
	u32  corrected_code_words;
	u32  uncorrected_code_words;
	u32  total_code_words_received;
	u32  corrected_bits;
	u32  error_mpeg_frames;
	u32  mpeg_frames_received;
	u32  erasures;
};

/* QAM Demodulator constellation point */
struct MXL_EAGLE_QAM_DEMOD_CONSTELLATION_VAL_T {
	s16  i_value[12];
	s16  q_value[12];
};

/* QAM Demodulator equalizer filter taps */
struct MXL_EAGLE_QAM_DEMOD_EQU_FILTER_T {
	s16  ffe_taps[MXL_EAGLE_QAM_FFE_TAPS_LENGTH];
	s16  spur_taps[MXL_EAGLE_QAM_SPUR_TAPS_LENGTH];
	s16  dfe_taps[MXL_EAGLE_QAM_DFE_TAPS_LENGTH];
	u8   ffe_leading_tap_index;
	u8   dfe_taps_number;
};

/* OOB Demodulator parameters struct, used in OOB params configuration */
struct __packed MXL_EAGLE_OOB_DEMOD_PARAMS_T {
	u8   symbol_rate;
	u8   iq_flip;
	u8   clk_pol;
};

/* OOB Demodulator error counters */
struct MXL_EAGLE_OOB_DEMOD_ERROR_COUNTERS_T {
	u32  corrected_packets;
	u32  uncorrected_packets;
	u32  total_packets_received;
};

/* OOB status */
struct __packed MXL_EAGLE_OOB_DEMOD_STATUS_T {
	u16  snr_db_tenths;
	s16  timing_offset;
	s32  carrier_offsetHz;
	u8   qam_locked;
	u8   fec_locked;
	u8   mpeg_locked;
	u8   retune_required;
	u8   iq_flip;
};

/* ATSC Demodulator status */
struct __packed MXL_EAGLE_ATSC_DEMOD_STATUS_T {
	s16  snr_db_tenths;
	s16  timing_offset;
	s32  carrier_offset_hz;
	u8   frame_lock;
	u8   atsc_lock;
	u8   fec_lock;
};

/* ATSC Demodulator error counters */
struct MXL_EAGLE_ATSC_DEMOD_ERROR_COUNTERS_T {
	u32  error_packets;
	u32  total_packets;
	u32  error_bytes;
};

/* ATSC Demodulator equalizers filter taps */
struct __packed MXL_EAGLE_ATSC_DEMOD_EQU_FILTER_T {
	s16  ffe_taps[MXL_EAGLE_ATSC_FFE_TAPS_LENGTH];
	s8   dfe_taps[MXL_EAGLE_ATSC_DFE_TAPS_LENGTH];
};

/* Tuner AGC Status */
struct __packed MXL_EAGLE_TUNER_AGC_STATUS_T {
	u8   locked;
	u16  raw_agc_gain;    /* AGC gain [dB] = rawAgcGain / 2^6 */
	s16  rx_power_db_hundredths;
};

/* Tuner channel tune parameters */
struct __packed MXL_EAGLE_TUNER_CHANNEL_PARAMS_T {
	u32  freq_hz;
	u8   tune_mode;
	u8   bandwidth;
};

/* Tuner channel lock indications */
struct __packed MXL_EAGLE_TUNER_LOCK_STATUS_T {
	u8   rf_pll_locked;
	u8   ref_pll_locked;
};

/* Smart antenna parameters  used in Smart antenna params configuration */
struct __packed MXL_EAGLE_SMA_PARAMS_T {
	u8   full_duplex_enable;
	u8   rx_disable;
	u8   idle_logic_high;
};

/* Smart antenna message format */
struct __packed MXL_EAGLE_SMA_MESSAGE_T {
	u32  payload_bits;
	u8   total_num_bits;
};

