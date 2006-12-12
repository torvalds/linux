#ifndef DIBX000_COMMON_H
#define DIBX000_COMMON_H

enum dibx000_i2c_interface {
	DIBX000_I2C_INTERFACE_TUNER    = 0,
	DIBX000_I2C_INTERFACE_GPIO_1_2 = 1,
	DIBX000_I2C_INTERFACE_GPIO_3_4 = 2
};

struct dibx000_i2c_master {
#define DIB3000MC 1
#define DIB7000   2
#define DIB7000P  11
#define DIB7000MC 12
	u16 device_rev;

	enum dibx000_i2c_interface selected_interface;

//	struct i2c_adapter  tuner_i2c_adap;
	struct i2c_adapter  gated_tuner_i2c_adap;

	struct i2c_adapter *i2c_adap;
	u8                  i2c_addr;

	u16 base_reg;
};

extern int dibx000_init_i2c_master(struct dibx000_i2c_master *mst, u16 device_rev, struct i2c_adapter *i2c_adap, u8 i2c_addr);
extern struct i2c_adapter * dibx000_get_i2c_adapter(struct dibx000_i2c_master *mst, enum dibx000_i2c_interface intf, int gating);
extern void dibx000_exit_i2c_master(struct dibx000_i2c_master *mst);

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
	/* defines the capabilities of this AGC-setting - using the BAND_-defines*/
	u8  band_caps;

	u16 setup;

	u16 inv_gain;
	u16 time_stabiliz;

	u8  alpha_level;
	u16 thlock;

	u8  wbd_inv;
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
	u32   internal;
	u32   sampling;

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
};

enum dibx000_adc_states {
	DIBX000_SLOW_ADC_ON = 0,
	DIBX000_SLOW_ADC_OFF,
	DIBX000_ADC_ON,
	DIBX000_ADC_OFF,
	DIBX000_VBG_ENABLE,
	DIBX000_VBG_DISABLE,
};

#define BW_INDEX_TO_KHZ(v) ( (v) == BANDWIDTH_8_MHZ  ? 8000 : \
			     (v) == BANDWIDTH_7_MHZ  ? 7000 : \
			     (v) == BANDWIDTH_6_MHZ  ? 6000 : 8000 )

/* Chip output mode. */
#define OUTMODE_HIGH_Z                      0
#define OUTMODE_MPEG2_PAR_GATED_CLK         1
#define OUTMODE_MPEG2_PAR_CONT_CLK          2
#define OUTMODE_MPEG2_SERIAL                7
#define OUTMODE_DIVERSITY                   4
#define OUTMODE_MPEG2_FIFO                  5

/* I hope I can get rid of the following kludge in the near future */
struct dibx000_ofdm_channel {
	u32 RF_kHz;
	u8  Bw;
	s16 nfft;
	s16 guard;
	s16 nqam;
	s16 vit_hrch;
	s16 vit_select_hp;
	s16 vit_alpha;
	s16 vit_code_rate_hp;
	s16 vit_code_rate_lp;
	u8  intlv_native;
};

#define FEP2DIB(fep,ch) \
	(ch)->RF_kHz           = (fep)->frequency / 1000; \
	(ch)->Bw               = (fep)->u.ofdm.bandwidth; \
	(ch)->nfft             = (fep)->u.ofdm.transmission_mode == TRANSMISSION_MODE_AUTO ? -1 : (fep)->u.ofdm.transmission_mode; \
	(ch)->guard            = (fep)->u.ofdm.guard_interval == GUARD_INTERVAL_AUTO ? -1 : (fep)->u.ofdm.guard_interval; \
	(ch)->nqam             = (fep)->u.ofdm.constellation == QAM_AUTO ? -1 : (fep)->u.ofdm.constellation == QAM_64 ? 2 : (fep)->u.ofdm.constellation; \
	(ch)->vit_hrch         = 0; /* linux-dvb is not prepared for HIERARCHICAL TRANSMISSION */ \
	(ch)->vit_select_hp    = 1; \
	(ch)->vit_alpha        = 1; \
	(ch)->vit_code_rate_hp = (fep)->u.ofdm.code_rate_HP == FEC_AUTO ? -1 : (fep)->u.ofdm.code_rate_HP; \
	(ch)->vit_code_rate_lp = (fep)->u.ofdm.code_rate_LP == FEC_AUTO ? -1 : (fep)->u.ofdm.code_rate_LP; \
	(ch)->intlv_native     = 1;

#define INIT_OFDM_CHANNEL(ch) do {\
	(ch)->Bw               = 0;  \
	(ch)->nfft             = -1; \
	(ch)->guard            = -1; \
	(ch)->nqam             = -1; \
	(ch)->vit_hrch         = -1; \
	(ch)->vit_select_hp    = -1; \
	(ch)->vit_alpha        = -1; \
	(ch)->vit_code_rate_hp = -1; \
	(ch)->vit_code_rate_lp = -1; \
} while (0)

#endif
