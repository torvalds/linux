#ifndef DIBX000_COMMON_H
#define DIBX000_COMMON_H

enum dibx000_i2c_interface {
	DIBX000_I2C_INTERFACE_TUNER = 0,
	DIBX000_I2C_INTERFACE_GPIO_1_2 = 1,
	DIBX000_I2C_INTERFACE_GPIO_3_4 = 2
};

struct dibx000_i2c_master {
#define DIB3000MC 1
#define DIB7000   2
#define DIB7000P  11
#define DIB7000MC 12
#define DIB8000   13
	u16 device_rev;

	enum dibx000_i2c_interface selected_interface;

//      struct i2c_adapter  tuner_i2c_adap;
	struct i2c_adapter gated_tuner_i2c_adap;

	struct i2c_adapter *i2c_adap;
	u8 i2c_addr;

	u16 base_reg;
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

#define BAND_LBAND 0x01
#define BAND_UHF   0x02
#define BAND_VHF   0x04
#define BAND_SBAND 0x08
#define BAND_FM	   0x10

#define BAND_OF_FREQUENCY(freq_kHz) ( (freq_kHz) <= 115000 ? BAND_FM : \
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

#define BANDWIDTH_TO_KHZ(v) ( (v) == BANDWIDTH_8_MHZ  ? 8000 : \
			     (v) == BANDWIDTH_7_MHZ  ? 7000 : \
			     (v) == BANDWIDTH_6_MHZ  ? 6000 : 8000 )

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

#endif
