#ifndef DIBX000_COMMON_H
#define DIBX000_COMMON_H

enum dibx000_i2c_interface {
	DIBX000_I2C_INTERFACE_TUNER = 0,
	DIBX000_I2C_INTERFACE_GPIO_1_2 = 1,
	DIBX000_I2C_INTERFACE_GPIO_3_4 = 2,
	DIBX000_I2C_INTERFACE_GPIO_6_7 = 3
};

struct dibx000_i2c_master {
#define DIB3000MC 1
#define DIB7000   2
#define DIB7000P  11
#define DIB7000MC 12
#define DIB8000   13
	u16 device_rev;

	enum dibx000_i2c_interface selected_interface;

/*	struct i2c_adapter  tuner_i2c_adap; */
	struct i2c_adapter gated_tuner_i2c_adap;
	struct i2c_adapter master_i2c_adap_gpio12;
	struct i2c_adapter master_i2c_adap_gpio34;
	struct i2c_adapter master_i2c_adap_gpio67;

	struct i2c_adapter *i2c_adap;
	u8 i2c_addr;

	u16 base_reg;

	/* for the I2C transfer */
	struct i2c_msg msg[34];
	u8 i2c_write_buffer[8];
	u8 i2c_read_buffer[2];
	struct mutex i2c_buffer_lock;
};

extern int dibx000_init_i2c_master(struct dibx000_i2c_master *mst,
					u16 device_rev, struct i2c_adapter *i2c_adap,
					u8 i2c_addr);
extern struct i2c_adapter *dibx000_get_i2c_adapter(struct dibx000_i2c_master
							*mst,
							enum dibx000_i2c_interface
							intf, int gating);
extern void dibx000_exit_i2c_master(struct dibx000_i2c_master *mst);
extern void dibx000_reset_i2c_master(struct dibx000_i2c_master *mst);
extern int dibx000_i2c_set_speed(struct i2c_adapter *i2c_adap, u16 speed);

extern u32 systime(void);

#define BAND_LBAND 0x01
#define BAND_UHF   0x02
#define BAND_VHF   0x04
#define BAND_SBAND 0x08
#define BAND_FM	   0x10
#define BAND_CBAND 0x20

#define BAND_OF_FREQUENCY(freq_kHz) ((freq_kHz) <= 170000 ? BAND_CBAND : \
									(freq_kHz) <= 115000 ? BAND_FM : \
									(freq_kHz) <= 250000 ? BAND_VHF : \
									(freq_kHz) <= 863000 ? BAND_UHF : \
									(freq_kHz) <= 2000000 ? BAND_LBAND : BAND_SBAND )

struct dibx000_agc_config {
	/* defines the capabilities of this AGC-setting - using the BAND_-defines */
	u8 band_caps;

	u16 setup;

	u16 inv_gain;
	u16 time_stabiliz;

	u8 alpha_level;
	u16 thlock;

	u8 wbd_inv;
	u16 wbd_ref;
	u8 wbd_sel;
	u8 wbd_alpha;

	u16 agc1_max;
	u16 agc1_min;
	u16 agc2_max;
	u16 agc2_min;

	u8 agc1_pt1;
	u8 agc1_pt2;
	u8 agc1_pt3;

	u8 agc1_slope1;
	u8 agc1_slope2;

	u8 agc2_pt1;
	u8 agc2_pt2;

	u8 agc2_slope1;
	u8 agc2_slope2;

	u8 alpha_mant;
	u8 alpha_exp;

	u8 beta_mant;
	u8 beta_exp;

	u8 perform_agc_softsplit;

	struct {
		u16 min;
		u16 max;
		u16 min_thres;
		u16 max_thres;
	} split;
};

struct dibx000_bandwidth_config {
	u32 internal;
	u32 sampling;

	u8 pll_prediv;
	u8 pll_ratio;
	u8 pll_range;
	u8 pll_reset;
	u8 pll_bypass;

	u8 enable_refdiv;
	u8 bypclk_div;
	u8 IO_CLK_en_core;
	u8 ADClkSrc;
	u8 modulo;

	u16 sad_cfg;

	u32 ifreq;
	u32 timf;

	u32 xtal_hz;
};

enum dibx000_adc_states {
	DIBX000_SLOW_ADC_ON = 0,
	DIBX000_SLOW_ADC_OFF,
	DIBX000_ADC_ON,
	DIBX000_ADC_OFF,
	DIBX000_VBG_ENABLE,
	DIBX000_VBG_DISABLE,
};

#define BANDWIDTH_TO_KHZ(v) ((v) == BANDWIDTH_8_MHZ  ? 8000 : \
				(v) == BANDWIDTH_7_MHZ  ? 7000 : \
				(v) == BANDWIDTH_6_MHZ  ? 6000 : 8000)

#define BANDWIDTH_TO_INDEX(v) ( \
	(v) == 8000 ? BANDWIDTH_8_MHZ : \
		(v) == 7000 ? BANDWIDTH_7_MHZ : \
		(v) == 6000 ? BANDWIDTH_6_MHZ : BANDWIDTH_8_MHZ )

/* Chip output mode. */
#define OUTMODE_HIGH_Z              0
#define OUTMODE_MPEG2_PAR_GATED_CLK 1
#define OUTMODE_MPEG2_PAR_CONT_CLK  2
#define OUTMODE_MPEG2_SERIAL        7
#define OUTMODE_DIVERSITY           4
#define OUTMODE_MPEG2_FIFO          5
#define OUTMODE_ANALOG_ADC          6

#define INPUT_MODE_OFF                0x11
#define INPUT_MODE_DIVERSITY          0x12
#define INPUT_MODE_MPEG               0x13

enum frontend_tune_state {
	CT_TUNER_START = 10,
	CT_TUNER_STEP_0,
	CT_TUNER_STEP_1,
	CT_TUNER_STEP_2,
	CT_TUNER_STEP_3,
	CT_TUNER_STEP_4,
	CT_TUNER_STEP_5,
	CT_TUNER_STEP_6,
	CT_TUNER_STEP_7,
	CT_TUNER_STOP,

	CT_AGC_START = 20,
	CT_AGC_STEP_0,
	CT_AGC_STEP_1,
	CT_AGC_STEP_2,
	CT_AGC_STEP_3,
	CT_AGC_STEP_4,
	CT_AGC_STOP,

	CT_DEMOD_START = 30,
	CT_DEMOD_STEP_1,
	CT_DEMOD_STEP_2,
	CT_DEMOD_STEP_3,
	CT_DEMOD_STEP_4,
	CT_DEMOD_STEP_5,
	CT_DEMOD_STEP_6,
	CT_DEMOD_STEP_7,
	CT_DEMOD_STEP_8,
	CT_DEMOD_STEP_9,
	CT_DEMOD_STEP_10,
	CT_DEMOD_SEARCH_NEXT = 41,
	CT_DEMOD_STEP_LOCKED,
	CT_DEMOD_STOP,

	CT_DONE = 100,
	CT_SHUTDOWN,

};

struct dvb_frontend_parametersContext {
#define CHANNEL_STATUS_PARAMETERS_UNKNOWN   0x01
#define CHANNEL_STATUS_PARAMETERS_SET       0x02
	u8 status;
	u32 tune_time_estimation[2];
	s32 tps_available;
	u16 tps[9];
};

#define FE_STATUS_TUNE_FAILED          0
#define FE_STATUS_TUNE_TIMED_OUT      -1
#define FE_STATUS_TUNE_TIME_TOO_SHORT -2
#define FE_STATUS_TUNE_PENDING        -3
#define FE_STATUS_STD_SUCCESS         -4
#define FE_STATUS_FFT_SUCCESS         -5
#define FE_STATUS_DEMOD_SUCCESS       -6
#define FE_STATUS_LOCKED              -7
#define FE_STATUS_DATA_LOCKED         -8

#define FE_CALLBACK_TIME_NEVER 0xffffffff

#define ABS(x) ((x < 0) ? (-x) : (x))

#define DATA_BUS_ACCESS_MODE_8BIT                 0x01
#define DATA_BUS_ACCESS_MODE_16BIT                0x02
#define DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT 0x10

struct dibGPIOFunction {
#define BOARD_GPIO_COMPONENT_BUS_ADAPTER 1
#define BOARD_GPIO_COMPONENT_DEMOD       2
	u8 component;

#define BOARD_GPIO_FUNCTION_BOARD_ON      1
#define BOARD_GPIO_FUNCTION_BOARD_OFF     2
#define BOARD_GPIO_FUNCTION_COMPONENT_ON  3
#define BOARD_GPIO_FUNCTION_COMPONENT_OFF 4
#define BOARD_GPIO_FUNCTION_SUBBAND_PWM   5
#define BOARD_GPIO_FUNCTION_SUBBAND_GPIO   6
	u8 function;

/* mask, direction and value are used specify which GPIO to change GPIO0
 * is LSB and possible GPIO31 is MSB.  The same bit-position as in the
 * mask is used for the direction and the value. Direction == 1 is OUT,
 * 0 == IN. For direction "OUT" value is either 1 or 0, for direction IN
 * value has no meaning.
 *
 * In case of BOARD_GPIO_FUNCTION_PWM mask is giving the GPIO to be
 * used to do the PWM. Direction gives the PWModulator to be used.
 * Value gives the PWM value in device-dependent scale.
 */
	u32 mask;
	u32 direction;
	u32 value;
};

#define MAX_NB_SUBBANDS   8
struct dibSubbandSelection {
	u8  size; /* Actual number of subbands. */
	struct {
		u16 f_mhz;
		struct dibGPIOFunction gpio;
	} subband[MAX_NB_SUBBANDS];
};

#define DEMOD_TIMF_SET    0x00
#define DEMOD_TIMF_GET    0x01
#define DEMOD_TIMF_UPDATE 0x02

#endif
