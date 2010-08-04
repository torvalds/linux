#ifndef VENDOR_CMD_H_
#define VENDOR_CMD_H_

#define BULK_ALTERNATE_IFACE		(2)
#define ISO_3K_BULK_ALTERNATE_IFACE     (1)
#define REQ_SET_CMD			(0X00)
#define REQ_GET_CMD			(0X80)

enum tlg__analog_audio_standard {
	TLG_TUNE_ASTD_NONE	= 0x00000000,
	TLG_TUNE_ASTD_A2	= 0x00000001,
	TLG_TUNE_ASTD_NICAM	= 0x00000002,
	TLG_TUNE_ASTD_EIAJ	= 0x00000004,
	TLG_TUNE_ASTD_BTSC	= 0x00000008,
	TLG_TUNE_ASTD_FM_US	= 0x00000010,
	TLG_TUNE_ASTD_FM_EUR	= 0x00000020,
	TLG_TUNE_ASTD_ALL	= 0x0000003f
};

/*
 * identifiers for Custom Parameter messages.
 * @typedef cmd_custom_param_id_t
 */
enum cmd_custom_param_id {
	CUST_PARM_ID_NONE		= 0x00,
	CUST_PARM_ID_BRIGHTNESS_CTRL	= 0x01,
	CUST_PARM_ID_CONTRAST_CTRL	= 0x02,
	CUST_PARM_ID_HUE_CTRL		= 0x03,
	CUST_PARM_ID_SATURATION_CTRL	  = 0x04,
	CUST_PARM_ID_AUDIO_SNR_THRESHOLD  = 0x10,
	CUST_PARM_ID_AUDIO_AGC_THRESHOLD  = 0x11,
	CUST_PARM_ID_MAX
};

struct  tuner_custom_parameter_s {
	uint16_t	param_id;	 /*  Parameter identifier  */
	uint16_t	param_value;	 /*  Parameter value	   */
};

struct  tuner_ber_rate_s {
	uint32_t	ber_rate;  /*  BER sample rate in seconds   */
};

struct tuner_atv_sig_stat_s {
	uint32_t	sig_present;
	uint32_t	sig_locked;
	uint32_t	sig_lock_busy;
	uint32_t	sig_strength;	   /*  milliDb	  */
	uint32_t	tv_audio_chan;	  /*  mono/stereo/sap*/
	uint32_t 	mvision_stat;	   /*  macrovision status */
};

struct tuner_dtv_sig_stat_s {
	uint32_t sig_present;   /*  Boolean*/
	uint32_t sig_locked;	/*  Boolean */
	uint32_t sig_lock_busy; /*  Boolean	(Can this time-out?) */
	uint32_t sig_strength;  /*  milliDb*/
};

struct tuner_fm_sig_stat_s {
	uint32_t sig_present;	/* Boolean*/
	uint32_t sig_locked;	 /* Boolean */
	uint32_t sig_lock_busy;  /* Boolean */
	uint32_t sig_stereo_mono;/* TBD*/
	uint32_t sig_strength;   /* milliDb*/
};

enum _tag_tlg_tune_srv_cmd {
	TLG_TUNE_PLAY_SVC_START = 1,
	TLG_TUNE_PLAY_SVC_STOP
};

enum  _tag_tune_atv_audio_mode_caps {
	TLG_TUNE_TVAUDIO_MODE_MONO	= 0x00000001,
	TLG_TUNE_TVAUDIO_MODE_STEREO	= 0x00000002,
	TLG_TUNE_TVAUDIO_MODE_LANG_A	= 0x00000010,/* Primary language*/
	TLG_TUNE_TVAUDIO_MODE_LANG_B	= 0x00000020,/* 2nd avail language*/
	TLG_TUNE_TVAUDIO_MODE_LANG_C	= 0x00000040
};


enum   _tag_tuner_atv_audio_rates {
	ATV_AUDIO_RATE_NONE	= 0x00,/* Audio not supported*/
	ATV_AUDIO_RATE_32K	= 0x01,/* Audio rate = 32 KHz*/
	ATV_AUDIO_RATE_48K	= 0x02, /* Audio rate = 48 KHz*/
	ATV_AUDIO_RATE_31_25K	= 0x04 /* Audio rate = 31.25KHz */
};

enum  _tag_tune_atv_vid_res_caps {
	TLG_TUNE_VID_RES_NONE	= 0x00000000,
	TLG_TUNE_VID_RES_720	= 0x00000001,
	TLG_TUNE_VID_RES_704	= 0x00000002,
	TLG_TUNE_VID_RES_360	= 0x00000004
};

enum _tag_tuner_analog_video_format {
	TLG_TUNER_VID_FORMAT_YUV	= 0x00000001,
	TLG_TUNER_VID_FORMAT_YCRCB	= 0x00000002,
	TLG_TUNER_VID_FORMAT_RGB_565	= 0x00000004,
};

enum  tlg_ext_audio_support {
	TLG_EXT_AUDIO_NONE 	= 0x00,/*  No external audio input supported */
	TLG_EXT_AUDIO_LR	= 0x01/*  LR external audio inputs supported*/
};

enum {
	TLG_MODE_NONE			= 0x00, /* No Mode specified*/
	TLG_MODE_ANALOG_TV		= 0x01, /* Analog Television mode*/
	TLG_MODE_ANALOG_TV_UNCOMP	= 0x01, /* Analog Television mode*/
	TLG_MODE_ANALOG_TV_COMP  	= 0x02, /* Analog TV mode (compressed)*/
	TLG_MODE_FM_RADIO		= 0x04, /* FM Radio mode*/
	TLG_MODE_DVB_T			= 0x08, /* Digital TV (DVB-T)*/
};

enum  tlg_signal_sources_t {
	TLG_SIG_SRC_NONE	= 0x00,/* Signal source not specified */
	TLG_SIG_SRC_ANTENNA	= 0x01,/* Signal src is: Antenna */
	TLG_SIG_SRC_CABLE	= 0x02,/* Signal src is: Coax Cable*/
	TLG_SIG_SRC_SVIDEO	= 0x04,/* Signal src is: S_VIDEO   */
	TLG_SIG_SRC_COMPOSITE   = 0x08 /* Signal src is: Composite Video */
};

enum tuner_analog_video_standard {
	TLG_TUNE_VSTD_NONE	= 0x00000000,
	TLG_TUNE_VSTD_NTSC_M	= 0x00000001,
	TLG_TUNE_VSTD_NTSC_M_J	= 0x00000002,/* Japan   */
	TLG_TUNE_VSTD_PAL_B	= 0x00000010,
	TLG_TUNE_VSTD_PAL_D	= 0x00000020,
	TLG_TUNE_VSTD_PAL_G	= 0x00000040,
	TLG_TUNE_VSTD_PAL_H	= 0x00000080,
	TLG_TUNE_VSTD_PAL_I	= 0x00000100,
	TLG_TUNE_VSTD_PAL_M	= 0x00000200,
	TLG_TUNE_VSTD_PAL_N	= 0x00000400,
	TLG_TUNE_VSTD_SECAM_B	= 0x00001000,
	TLG_TUNE_VSTD_SECAM_D	= 0x00002000,
	TLG_TUNE_VSTD_SECAM_G	= 0x00004000,
	TLG_TUNE_VSTD_SECAM_H	= 0x00008000,
	TLG_TUNE_VSTD_SECAM_K	= 0x00010000,
	TLG_TUNE_VSTD_SECAM_K1	= 0x00020000,
	TLG_TUNE_VSTD_SECAM_L	= 0x00040000,
	TLG_TUNE_VSTD_SECAM_L1	= 0x00080000,
	TLG_TUNE_VSTD_PAL_N_COMBO = 0x00100000
};

enum tlg_mode_caps {
	TLG_MODE_CAPS_NONE		= 0x00,  /*  No Mode specified	*/
	TLG_MODE_CAPS_ANALOG_TV_UNCOMP  = 0x01,  /*  Analog TV mode     */
	TLG_MODE_CAPS_ANALOG_TV_COMP	= 0x02,  /*  Analog TV (compressed)*/
	TLG_MODE_CAPS_FM_RADIO		= 0x04,  /*  FM Radio mode	*/
	TLG_MODE_CAPS_DVB_T		= 0x08,  /*  Digital TV (DVB-T)	*/
};

enum poseidon_vendor_cmds {
	LAST_CMD_STAT		= 0x00,
	GET_CHIP_ID		= 0x01,
	GET_FW_ID		= 0x02,
	PRODUCT_CAPS		= 0x03,

	TUNE_MODE_CAP_ATV	= 0x10,
	TUNE_MODE_CAP_ATVCOMP	= 0X10,
	TUNE_MODE_CAP_DVBT	= 0x10,
	TUNE_MODE_CAP_FM	= 0x10,
	TUNE_MODE_SELECT	= 0x11,
	TUNE_FREQ_SELECT	= 0x12,
	SGNL_SRC_SEL		= 0x13,

	VIDEO_STD_SEL		= 0x14,
	VIDEO_STREAM_FMT_SEL	= 0x15,
	VIDEO_ROSOLU_AVAIL	= 0x16,
	VIDEO_ROSOLU_SEL	= 0x17,
	VIDEO_CONT_PROTECT	= 0x20,

	VCR_TIMING_MODSEL	= 0x21,
	EXT_AUDIO_CAP		= 0x22,
	EXT_AUDIO_SEL		= 0x23,
	TEST_PATTERN_SEL	= 0x24,
	VBI_DATA_SEL		= 0x25,
	AUDIO_SAMPLE_RATE_CAP   = 0x28,
	AUDIO_SAMPLE_RATE_SEL   = 0x29,
	TUNER_AUD_MODE		= 0x2a,
	TUNER_AUD_MODE_AVAIL	= 0x2b,
	TUNER_AUD_ANA_STD	= 0x2c,
	TUNER_CUSTOM_PARAMETER	= 0x2f,

	DVBT_TUNE_MODE_SEL	= 0x30,
	DVBT_BANDW_CAP		= 0x31,
	DVBT_BANDW_SEL		= 0x32,
	DVBT_GUARD_INTERV_CAP   = 0x33,
	DVBT_GUARD_INTERV_SEL   = 0x34,
	DVBT_MODULATION_CAP	= 0x35,
	DVBT_MODULATION_SEL	= 0x36,
	DVBT_INNER_FEC_RATE_CAP = 0x37,
	DVBT_INNER_FEC_RATE_SEL = 0x38,
	DVBT_TRANS_MODE_CAP	= 0x39,
	DVBT_TRANS_MODE_SEL	= 0x3a,
	DVBT_SEARCH_RANG	= 0x3c,

	TUNER_SETUP_ANALOG	= 0x40,
	TUNER_SETUP_DIGITAL	= 0x41,
	TUNER_SETUP_FM_RADIO	= 0x42,
	TAKE_REQUEST		= 0x43, /* Take effect of the command */
	PLAY_SERVICE		= 0x44, /* Play start or Play stop */
	TUNER_STATUS		= 0x45,
	TUNE_PROP_DVBT		= 0x46,
	ERR_RATE_STATS		= 0x47,
	TUNER_BER_RATE		= 0x48,

	SCAN_CAPS		= 0x50,
	SCAN_SETUP		= 0x51,
	SCAN_SERVICE		= 0x52,
	SCAN_STATS		= 0x53,

	PID_SET			= 0x58,
	PID_UNSET		= 0x59,
	PID_LIST		= 0x5a,

	IRD_CAP			= 0x60,
	IRD_MODE_SEL		= 0x61,
	IRD_SETUP		= 0x62,

	PTM_MODE_CAP		= 0x70,
	PTM_MODE_SEL		= 0x71,
	PTM_SERVICE		= 0x72,
	TUNER_REG_SCRIPT	= 0x73,
	CMD_CHIP_RST		= 0x74,
};

enum tlg_bw {
	TLG_BW_5 = 5,
	TLG_BW_6 = 6,
	TLG_BW_7 = 7,
	TLG_BW_8 = 8,
	TLG_BW_12 = 12,
	TLG_BW_15 = 15
};

struct cmd_firmware_vers_s {
	uint8_t	 fw_rev_major;
	uint8_t	 fw_rev_minor;
	uint16_t fw_patch;
};
#endif /* VENDOR_CMD_H_ */
