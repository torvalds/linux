
/*
  Copyright (c), 2004-2005,2007-2010 Trident Microsystems, Inc.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.
  * Neither the name of Trident Microsystems nor Hauppauge Computer Works
    nor the names of its contributors may be used to endorse or promote
	products derived from this software without specific prior written
	permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

 DRXJ specific header file

 Authors: Dragan Savic, Milos Nikolic, Mihajlo Katona, Tao Ding, Paul Janssen
*/

#ifndef __DRXJ_H__
#define __DRXJ_H__
/*-------------------------------------------------------------------------
INCLUDES
-------------------------------------------------------------------------*/

#include "drx_driver.h"
#include "drx_dap_fasi.h"

/* Check DRX-J specific dap condition */
/* Multi master mode and short addr format only will not work.
   RMW, CRC reset, broadcast and switching back to single master mode
   cannot be done with short addr only in multi master mode. */
#if ((DRXDAP_SINGLE_MASTER == 0) && (DRXDAPFASI_LONG_ADDR_ALLOWED == 0))
#error "Multi master mode and short addressing only is an illegal combination"
	*;			/* Generate a fatal compiler error to make sure it stops here,
				   this is necesarry because not all compilers stop after a #error. */
#endif

/*-------------------------------------------------------------------------
TYPEDEFS
-------------------------------------------------------------------------*/
/*============================================================================*/
/*============================================================================*/
/*== code support ============================================================*/
/*============================================================================*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*== SCU cmd if  =============================================================*/
/*============================================================================*/
/*============================================================================*/

	struct drxjscu_cmd {
		u16 command;
			/*< Command number */
		u16 parameter_len;
			/*< Data length in byte */
		u16 result_len;
			/*< result length in byte */
		u16 *parameter;
			/*< General purpous param */
		u16 *result;
			/*< General purpous param */};

/*============================================================================*/
/*============================================================================*/
/*== CTRL CFG related data structures ========================================*/
/*============================================================================*/
/*============================================================================*/

/* extra intermediate lock state for VSB,QAM,NTSC */
#define DRXJ_DEMOD_LOCK       (DRX_LOCK_STATE_1)

/* OOB lock states */
#define DRXJ_OOB_AGC_LOCK     (DRX_LOCK_STATE_1)	/* analog gain control lock */
#define DRXJ_OOB_SYNC_LOCK    (DRX_LOCK_STATE_2)	/* digital gain control lock */

/* Intermediate powermodes for DRXJ */
#define DRXJ_POWER_DOWN_MAIN_PATH   DRX_POWER_MODE_8
#define DRXJ_POWER_DOWN_CORE        DRX_POWER_MODE_9
#define DRXJ_POWER_DOWN_PLL         DRX_POWER_MODE_10

/* supstition for GPIO FNC mux */
#define APP_O                 (0x0000)

/*#define DRX_CTRL_BASE         (0x0000)*/

#define DRXJ_CTRL_CFG_BASE    (0x1000)
	enum drxj_cfg_type {
		DRXJ_CFG_AGC_RF = DRXJ_CTRL_CFG_BASE,
		DRXJ_CFG_AGC_IF,
		DRXJ_CFG_AGC_INTERNAL,
		DRXJ_CFG_PRE_SAW,
		DRXJ_CFG_AFE_GAIN,
		DRXJ_CFG_SYMBOL_CLK_OFFSET,
		DRXJ_CFG_ACCUM_CR_RS_CW_ERR,
		DRXJ_CFG_FEC_MERS_SEQ_COUNT,
		DRXJ_CFG_OOB_MISC,
		DRXJ_CFG_SMART_ANT,
		DRXJ_CFG_OOB_PRE_SAW,
		DRXJ_CFG_VSB_MISC,
		DRXJ_CFG_RESET_PACKET_ERR,

		/* ATV (FM) */
		DRXJ_CFG_ATV_OUTPUT,	/* also for FM (SIF control) but not likely */
		DRXJ_CFG_ATV_MISC,
		DRXJ_CFG_ATV_EQU_COEF,
		DRXJ_CFG_ATV_AGC_STATUS,	/* also for FM ( IF,RF, audioAGC ) */

		DRXJ_CFG_MPEG_OUTPUT_MISC,
		DRXJ_CFG_HW_CFG,
		DRXJ_CFG_OOB_LO_POW,

		DRXJ_CFG_MAX	/* dummy, never to be used */};

/*
* /struct enum drxj_cfg_smart_ant_io * smart antenna i/o.
*/
enum drxj_cfg_smart_ant_io {
	DRXJ_SMT_ANT_OUTPUT = 0,
	DRXJ_SMT_ANT_INPUT
};

/*
* /struct struct drxj_cfg_smart_ant * Set smart antenna.
*/
	struct drxj_cfg_smart_ant {
		enum drxj_cfg_smart_ant_io io;
		u16 ctrl_data;
	};

/*
* /struct DRXJAGCSTATUS_t
* AGC status information from the DRXJ-IQM-AF.
*/
struct drxj_agc_status {
	u16 IFAGC;
	u16 RFAGC;
	u16 digital_agc;
};

/* DRXJ_CFG_AGC_RF, DRXJ_CFG_AGC_IF */

/*
* /struct enum drxj_agc_ctrl_mode * Available AGCs modes in the DRXJ.
*/
	enum drxj_agc_ctrl_mode {
		DRX_AGC_CTRL_AUTO = 0,
		DRX_AGC_CTRL_USER,
		DRX_AGC_CTRL_OFF};

/*
* /struct struct drxj_cfg_agc * Generic interface for all AGCs present on the DRXJ.
*/
	struct drxj_cfg_agc {
		enum drx_standard standard;	/* standard for which these settings apply */
		enum drxj_agc_ctrl_mode ctrl_mode;	/* off, user, auto          */
		u16 output_level;	/* range dependent on AGC   */
		u16 min_output_level;	/* range dependent on AGC   */
		u16 max_output_level;	/* range dependent on AGC   */
		u16 speed;	/* range dependent on AGC   */
		u16 top;	/* rf-agc take over point   */
		u16 cut_off_current;	/* rf-agc is accelerated if output current
					   is below cut-off current                */};

/* DRXJ_CFG_PRE_SAW */

/*
* /struct struct drxj_cfg_pre_saw * Interface to configure pre SAW sense.
*/
	struct drxj_cfg_pre_saw {
		enum drx_standard standard;	/* standard to which these settings apply */
		u16 reference;	/* pre SAW reference value, range 0 .. 31 */
		bool use_pre_saw;	/* true algorithms must use pre SAW sense */};

/* DRXJ_CFG_AFE_GAIN */

/*
* /struct struct drxj_cfg_afe_gain * Interface to configure gain of AFE (LNA + PGA).
*/
	struct drxj_cfg_afe_gain {
		enum drx_standard standard;	/* standard to which these settings apply */
		u16 gain;	/* gain in 0.1 dB steps, DRXJ range 140 .. 335 */};

/*
* /struct drxjrs_errors
* Available failure information in DRXJ_FEC_RS.
*
* Container for errors that are received in the most recently finished measurment period
*
*/
	struct drxjrs_errors {
		u16 nr_bit_errors;
				/*< no of pre RS bit errors          */
		u16 nr_symbol_errors;
				/*< no of pre RS symbol errors       */
		u16 nr_packet_errors;
				/*< no of pre RS packet errors       */
		u16 nr_failures;
				/*< no of post RS failures to decode */
		u16 nr_snc_par_fail_count;
				/*< no of post RS bit erros          */
	};

/*
* /struct struct drxj_cfg_vsb_misc * symbol error rate
*/
	struct drxj_cfg_vsb_misc {
		u32 symb_error;
			      /*< symbol error rate sps */};

/*
* /enum enum drxj_mpeg_output_clock_rate * Mpeg output clock rate.
*
*/
	enum drxj_mpeg_start_width {
		DRXJ_MPEG_START_WIDTH_1CLKCYC,
		DRXJ_MPEG_START_WIDTH_8CLKCYC};

/*
* /enum enum drxj_mpeg_output_clock_rate * Mpeg output clock rate.
*
*/
	enum drxj_mpeg_output_clock_rate {
		DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_75973K,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_50625K,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_37968K,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_30375K,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_25313K,
		DRXJ_MPEGOUTPUT_CLOCK_RATE_21696K};

/*
* /struct DRXJCfgMisc_t
* Change TEI bit of MPEG output
* reverse MPEG output bit order
* set MPEG output clock rate
*/
	struct drxj_cfg_mpeg_output_misc {
		bool disable_tei_handling;	      /*< if true pass (not change) TEI bit */
		bool bit_reverse_mpeg_outout;	      /*< if true, parallel: msb on MD0; serial: lsb out first */
		enum drxj_mpeg_output_clock_rate mpeg_output_clock_rate;
						      /*< set MPEG output clock rate that overwirtes the derived one from symbol rate */
		enum drxj_mpeg_start_width mpeg_start_width;  /*< set MPEG output start width */};

/*
* /enum enum drxj_xtal_freq * Supported external crystal reference frequency.
*/
	enum drxj_xtal_freq {
		DRXJ_XTAL_FREQ_RSVD,
		DRXJ_XTAL_FREQ_27MHZ,
		DRXJ_XTAL_FREQ_20P25MHZ,
		DRXJ_XTAL_FREQ_4MHZ};

/*
* /enum enum drxj_xtal_freq * Supported external crystal reference frequency.
*/
	enum drxji2c_speed {
		DRXJ_I2C_SPEED_400KBPS,
		DRXJ_I2C_SPEED_100KBPS};

/*
* /struct struct drxj_cfg_hw_cfg * Get hw configuration, such as crystal reference frequency, I2C speed, etc...
*/
	struct drxj_cfg_hw_cfg {
		enum drxj_xtal_freq xtal_freq;
				   /*< crystal reference frequency */
		enum drxji2c_speed i2c_speed;
				   /*< 100 or 400 kbps */};

/*
 *  DRXJ_CFG_ATV_MISC
 */
	struct drxj_cfg_atv_misc {
		s16 peak_filter;	/* -8 .. 15 */
		u16 noise_filter;	/* 0 .. 15 */};

/*
 *  struct drxj_cfg_oob_misc */
#define   DRXJ_OOB_STATE_RESET                                        0x0
#define   DRXJ_OOB_STATE_AGN_HUNT                                     0x1
#define   DRXJ_OOB_STATE_DGN_HUNT                                     0x2
#define   DRXJ_OOB_STATE_AGC_HUNT                                     0x3
#define   DRXJ_OOB_STATE_FRQ_HUNT                                     0x4
#define   DRXJ_OOB_STATE_PHA_HUNT                                     0x8
#define   DRXJ_OOB_STATE_TIM_HUNT                                     0x10
#define   DRXJ_OOB_STATE_EQU_HUNT                                     0x20
#define   DRXJ_OOB_STATE_EQT_HUNT                                     0x30
#define   DRXJ_OOB_STATE_SYNC                                         0x40

struct drxj_cfg_oob_misc {
	struct drxj_agc_status agc;
	bool eq_lock;
	bool sym_timing_lock;
	bool phase_lock;
	bool freq_lock;
	bool dig_gain_lock;
	bool ana_gain_lock;
	u8 state;
};

/*
 *  Index of in array of coef
 */
	enum drxj_cfg_oob_lo_power {
		DRXJ_OOB_LO_POW_MINUS0DB = 0,
		DRXJ_OOB_LO_POW_MINUS5DB,
		DRXJ_OOB_LO_POW_MINUS10DB,
		DRXJ_OOB_LO_POW_MINUS15DB,
		DRXJ_OOB_LO_POW_MAX};

/*
 *  DRXJ_CFG_ATV_EQU_COEF
 */
	struct drxj_cfg_atv_equ_coef {
		s16 coef0;	/* -256 .. 255 */
		s16 coef1;	/* -256 .. 255 */
		s16 coef2;	/* -256 .. 255 */
		s16 coef3;	/* -256 .. 255 */};

/*
 *  Index of in array of coef
 */
	enum drxj_coef_array_index {
		DRXJ_COEF_IDX_MN = 0,
		DRXJ_COEF_IDX_FM,
		DRXJ_COEF_IDX_L,
		DRXJ_COEF_IDX_LP,
		DRXJ_COEF_IDX_BG,
		DRXJ_COEF_IDX_DK,
		DRXJ_COEF_IDX_I,
		DRXJ_COEF_IDX_MAX};

/*
 *  DRXJ_CFG_ATV_OUTPUT
 */

/*
* /enum DRXJAttenuation_t
* Attenuation setting for SIF AGC.
*
*/
	enum drxjsif_attenuation {
		DRXJ_SIF_ATTENUATION_0DB,
		DRXJ_SIF_ATTENUATION_3DB,
		DRXJ_SIF_ATTENUATION_6DB,
		DRXJ_SIF_ATTENUATION_9DB};

/*
* /struct struct drxj_cfg_atv_output * SIF attenuation setting.
*
*/
struct drxj_cfg_atv_output {
	bool enable_cvbs_output;	/* true= enabled */
	bool enable_sif_output;	/* true= enabled */
	enum drxjsif_attenuation sif_attenuation;
};

/*
   DRXJ_CFG_ATV_AGC_STATUS (get only)
*/
/* TODO : AFE interface not yet finished, subject to change */
	struct drxj_cfg_atv_agc_status {
		u16 rf_agc_gain;	/* 0 .. 877 uA */
		u16 if_agc_gain;	/* 0 .. 877  uA */
		s16 video_agc_gain;	/* -75 .. 1972 in 0.1 dB steps */
		s16 audio_agc_gain;	/* -4 .. 1020 in 0.1 dB steps */
		u16 rf_agc_loop_gain;	/* 0 .. 7 */
		u16 if_agc_loop_gain;	/* 0 .. 7 */
		u16 video_agc_loop_gain;	/* 0 .. 7 */};

/*============================================================================*/
/*============================================================================*/
/*== CTRL related data structures ============================================*/
/*============================================================================*/
/*============================================================================*/

/* NONE */

/*============================================================================*/
/*============================================================================*/

/*========================================*/
/*
* /struct struct drxj_data * DRXJ specific attributes.
*
* Global data container for DRXJ specific data.
*
*/
	struct drxj_data {
		/* device capabilties (determined during drx_open()) */
		bool has_lna;		  /*< true if LNA (aka PGA) present */
		bool has_oob;		  /*< true if OOB supported */
		bool has_ntsc;		  /*< true if NTSC supported */
		bool has_btsc;		  /*< true if BTSC supported */
		bool has_smatx;	  /*< true if mat_tx is available */
		bool has_smarx;	  /*< true if mat_rx is available */
		bool has_gpio;		  /*< true if GPIO is available */
		bool has_irqn;		  /*< true if IRQN is available */
		/* A1/A2/A... */
		u8 mfx;		  /*< metal fix */

		/* tuner settings */
		bool mirror_freq_spect_oob;/*< tuner inversion (true = tuner mirrors the signal */

		/* standard/channel settings */
		enum drx_standard standard;	  /*< current standard information                     */
		enum drx_modulation constellation;
					  /*< current constellation                            */
		s32 frequency; /*< center signal frequency in KHz                   */
		enum drx_bandwidth curr_bandwidth;
					  /*< current channel bandwidth                        */
		enum drx_mirror mirror;	  /*< current channel mirror                           */

		/* signal quality information */
		u32 fec_bits_desired;	  /*< BER accounting period                            */
		u16 fec_vd_plen;	  /*< no of trellis symbols: VD SER measurement period */
		u16 qam_vd_prescale;	  /*< Viterbi Measurement Prescale                     */
		u16 qam_vd_period;	  /*< Viterbi Measurement period                       */
		u16 fec_rs_plen;	  /*< defines RS BER measurement period                */
		u16 fec_rs_prescale;	  /*< ReedSolomon Measurement Prescale                 */
		u16 fec_rs_period;	  /*< ReedSolomon Measurement period                   */
		bool reset_pkt_err_acc;	  /*< Set a flag to reset accumulated packet error     */
		u16 pkt_err_acc_start;	  /*< Set a flag to reset accumulated packet error     */

		/* HI configuration */
		u16 hi_cfg_timing_div;	  /*< HI Configure() parameter 2                       */
		u16 hi_cfg_bridge_delay;	  /*< HI Configure() parameter 3                       */
		u16 hi_cfg_wake_up_key;	  /*< HI Configure() parameter 4                       */
		u16 hi_cfg_ctrl;	  /*< HI Configure() parameter 5                       */
		u16 hi_cfg_transmit;	  /*< HI Configure() parameter 6                       */

		/* UIO configuration */
		enum drxuio_mode uio_sma_rx_mode;/*< current mode of SmaRx pin                        */
		enum drxuio_mode uio_sma_tx_mode;/*< current mode of SmaTx pin                        */
		enum drxuio_mode uio_gpio_mode; /*< current mode of ASEL pin                         */
		enum drxuio_mode uio_irqn_mode; /*< current mode of IRQN pin                         */

		/* IQM fs frequecy shift and inversion */
		u32 iqm_fs_rate_ofs;	   /*< frequency shifter setting after setchannel      */
		bool pos_image;	   /*< Ture: positive image                            */
		/* IQM RC frequecy shift */
		u32 iqm_rc_rate_ofs;	   /*< frequency shifter setting after setchannel      */

		/* ATV configuration */
		u32 atv_cfg_changed_flags; /*< flag: flags cfg changes */
		s16 atv_top_equ0[DRXJ_COEF_IDX_MAX];	     /*< shadow of ATV_TOP_EQU0__A */
		s16 atv_top_equ1[DRXJ_COEF_IDX_MAX];	     /*< shadow of ATV_TOP_EQU1__A */
		s16 atv_top_equ2[DRXJ_COEF_IDX_MAX];	     /*< shadow of ATV_TOP_EQU2__A */
		s16 atv_top_equ3[DRXJ_COEF_IDX_MAX];	     /*< shadow of ATV_TOP_EQU3__A */
		bool phase_correction_bypass;/*< flag: true=bypass */
		s16 atv_top_vid_peak;	  /*< shadow of ATV_TOP_VID_PEAK__A */
		u16 atv_top_noise_th;	  /*< shadow of ATV_TOP_NOISE_TH__A */
		bool enable_cvbs_output;  /*< flag CVBS ouput enable */
		bool enable_sif_output;	  /*< flag SIF ouput enable */
		 enum drxjsif_attenuation sif_attenuation;
					  /*< current SIF att setting */
		/* Agc configuration for QAM and VSB */
		struct drxj_cfg_agc qam_rf_agc_cfg; /*< qam RF AGC config */
		struct drxj_cfg_agc qam_if_agc_cfg; /*< qam IF AGC config */
		struct drxj_cfg_agc vsb_rf_agc_cfg; /*< vsb RF AGC config */
		struct drxj_cfg_agc vsb_if_agc_cfg; /*< vsb IF AGC config */

		/* PGA gain configuration for QAM and VSB */
		u16 qam_pga_cfg;	  /*< qam PGA config */
		u16 vsb_pga_cfg;	  /*< vsb PGA config */

		/* Pre SAW configuration for QAM and VSB */
		struct drxj_cfg_pre_saw qam_pre_saw_cfg;
					  /*< qam pre SAW config */
		struct drxj_cfg_pre_saw vsb_pre_saw_cfg;
					  /*< qam pre SAW config */

		/* Version information */
		char v_text[2][12];	  /*< allocated text versions */
		struct drx_version v_version[2]; /*< allocated versions structs */
		struct drx_version_list v_list_elements[2];
					  /*< allocated version list */

		/* smart antenna configuration */
		bool smart_ant_inverted;

		/* Tracking filter setting for OOB */
		u16 oob_trk_filter_cfg[8];
		bool oob_power_on;

		/* MPEG static bitrate setting */
		u32 mpeg_ts_static_bitrate;  /*< bitrate static MPEG output */
		bool disable_te_ihandling;  /*< MPEG TS TEI handling */
		bool bit_reverse_mpeg_outout;/*< MPEG output bit order */
		 enum drxj_mpeg_output_clock_rate mpeg_output_clock_rate;
					    /*< MPEG output clock rate */
		 enum drxj_mpeg_start_width mpeg_start_width;
					    /*< MPEG Start width */

		/* Pre SAW & Agc configuration for ATV */
		struct drxj_cfg_pre_saw atv_pre_saw_cfg;
					  /*< atv pre SAW config */
		struct drxj_cfg_agc atv_rf_agc_cfg; /*< atv RF AGC config */
		struct drxj_cfg_agc atv_if_agc_cfg; /*< atv IF AGC config */
		u16 atv_pga_cfg;	  /*< atv pga config    */

		u32 curr_symbol_rate;

		/* pin-safe mode */
		bool pdr_safe_mode;	    /*< PDR safe mode activated      */
		u16 pdr_safe_restore_val_gpio;
		u16 pdr_safe_restore_val_v_sync;
		u16 pdr_safe_restore_val_sma_rx;
		u16 pdr_safe_restore_val_sma_tx;

		/* OOB pre-saw value */
		u16 oob_pre_saw;
		enum drxj_cfg_oob_lo_power oob_lo_pow;

		struct drx_aud_data aud_data;
				    /*< audio storage                  */};

/*-------------------------------------------------------------------------
Access MACROS
-------------------------------------------------------------------------*/
/*
* \brief Compilable references to attributes
* \param d pointer to demod instance
*
* Used as main reference to an attribute field.
* Can be used by both macro implementation and function implementation.
* These macros are defined to avoid duplication of code in macro and function
* definitions that handle access of demod common or extended attributes.
*
*/

#define DRXJ_ATTR_BTSC_DETECT(d)                       \
			(((struct drxj_data *)(d)->my_ext_attr)->aud_data.btsc_detect)

/*-------------------------------------------------------------------------
DEFINES
-------------------------------------------------------------------------*/

/*
* \def DRXJ_NTSC_CARRIER_FREQ_OFFSET
* \brief Offset from picture carrier to centre frequency in kHz, in RF domain
*
* For NTSC standard.
* NTSC channels are listed by their picture carrier frequency (Fpc).
* The function DRX_CTRL_SET_CHANNEL requires the centre frequency as input.
* In case the tuner module is not used the DRX-J requires that the tuner is
* tuned to the centre frequency of the channel:
*
* Fcentre = Fpc + DRXJ_NTSC_CARRIER_FREQ_OFFSET
*
*/
#define DRXJ_NTSC_CARRIER_FREQ_OFFSET           ((s32)(1750))

/*
* \def DRXJ_PAL_SECAM_BG_CARRIER_FREQ_OFFSET
* \brief Offset from picture carrier to centre frequency in kHz, in RF domain
*
* For PAL/SECAM - BG standard. This define is needed in case the tuner module
* is NOT used. PAL/SECAM channels are listed by their picture carrier frequency (Fpc).
* The DRX-J requires that the tuner is tuned to:
* Fpc + DRXJ_PAL_SECAM_BG_CARRIER_FREQ_OFFSET
*
* In case the tuner module is used the drxdriver takes care of this.
* In case the tuner module is NOT used the application programmer must take
* care of this.
*
*/
#define DRXJ_PAL_SECAM_BG_CARRIER_FREQ_OFFSET   ((s32)(2375))

/*
* \def DRXJ_PAL_SECAM_DKIL_CARRIER_FREQ_OFFSET
* \brief Offset from picture carrier to centre frequency in kHz, in RF domain
*
* For PAL/SECAM - DK, I, L standards. This define is needed in case the tuner module
* is NOT used. PAL/SECAM channels are listed by their picture carrier frequency (Fpc).
* The DRX-J requires that the tuner is tuned to:
* Fpc + DRXJ_PAL_SECAM_DKIL_CARRIER_FREQ_OFFSET
*
* In case the tuner module is used the drxdriver takes care of this.
* In case the tuner module is NOT used the application programmer must take
* care of this.
*
*/
#define DRXJ_PAL_SECAM_DKIL_CARRIER_FREQ_OFFSET ((s32)(2775))

/*
* \def DRXJ_PAL_SECAM_LP_CARRIER_FREQ_OFFSET
* \brief Offset from picture carrier to centre frequency in kHz, in RF domain
*
* For PAL/SECAM - LP standard. This define is needed in case the tuner module
* is NOT used. PAL/SECAM channels are listed by their picture carrier frequency (Fpc).
* The DRX-J requires that the tuner is tuned to:
* Fpc + DRXJ_PAL_SECAM_LP_CARRIER_FREQ_OFFSET
*
* In case the tuner module is used the drxdriver takes care of this.
* In case the tuner module is NOT used the application programmer must take
* care of this.
*/
#define DRXJ_PAL_SECAM_LP_CARRIER_FREQ_OFFSET   ((s32)(-3255))

/*
* \def DRXJ_FM_CARRIER_FREQ_OFFSET
* \brief Offset from sound carrier to centre frequency in kHz, in RF domain
*
* For FM standard.
* FM channels are listed by their sound carrier frequency (Fsc).
* The function DRX_CTRL_SET_CHANNEL requires the Ffm frequency (see below) as
* input.
* In case the tuner module is not used the DRX-J requires that the tuner is
* tuned to the Ffm frequency of the channel.
*
* Ffm = Fsc + DRXJ_FM_CARRIER_FREQ_OFFSET
*
*/
#define DRXJ_FM_CARRIER_FREQ_OFFSET             ((s32)(-3000))

/* Revision types -------------------------------------------------------*/

#define DRXJ_TYPE_ID (0x3946000DUL)

/* Macros ---------------------------------------------------------------*/

/* Convert OOB lock status to string */
#define DRXJ_STR_OOB_LOCKSTATUS(x) ( \
	(x == DRX_NEVER_LOCK) ? "Never" : \
	(x == DRX_NOT_LOCKED) ? "No" : \
	(x == DRX_LOCKED) ? "Locked" : \
	(x == DRX_LOCK_STATE_1) ? "AGC lock" : \
	(x == DRX_LOCK_STATE_2) ? "sync lock" : \
	"(Invalid)")

#endif				/* __DRXJ_H__ */
