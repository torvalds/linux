#ifndef QDSP5AUDPPCMDI_H
#define QDSP5AUDPPCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    A U D I O   P O S T   P R O C E S S I N G  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by AUDPP Task

REFERENCES
  None

EXTERNALIZED FUNCTIONS
  None

Copyright(c) 1992 - 2008 by QUALCOMM, Incorporated.

This software is licensed under the terms of the GNU General Public
License version 2, as published by the Free Software Foundation, and
may be copied, distributed, and modified under those terms.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
/*===========================================================================

                      EDIT HISTORY FOR FILE

This section contains comments describing changes made to this file.
Notice that changes are listed in reverse chronological order.

$Header: //source/qcom/qct/multimedia2/Audio/drivers/QDSP5Driver/QDSP5Interface/main/latest/qdsp5audppcmdi.h#2 $

===========================================================================*/

/*
 * ARM to AUDPPTASK Commands
 *
 * ARM uses three command queues to communicate with AUDPPTASK
 * 1)uPAudPPCmd1Queue : Used for more frequent and shorter length commands
 * 	Location : MEMA
 * 	Buffer Size : 6 words
 * 	No of buffers in a queue : 20 for gaming audio and 5 for other images
 * 2)uPAudPPCmd2Queue : Used for commands which are not much lengthier
 * 	Location : MEMA
 * 	Buffer Size : 23
 * 	No of buffers in a queue : 2
 * 3)uPAudOOCmd3Queue : Used for lengthier and more frequent commands
 * 	Location : MEMA
 * 	Buffer Size : 145
 * 	No of buffers in a queue : 3
 */

/*
 * Commands Related to uPAudPPCmd1Queue
 */

/*
 * Command Structure to enable or disable the active decoders
 */

#define AUDPP_CMD_CFG_DEC_TYPE 		0x0001
#define AUDPP_CMD_CFG_DEC_TYPE_LEN 	sizeof(audpp_cmd_cfg_dec_type)

/* Enable the decoder */
#define AUDPP_CMD_DEC_TYPE_M           	0x000F

#define AUDPP_CMD_ENA_DEC_V         	0x4000
#define AUDPP_CMD_DIS_DEC_V        	0x0000
#define AUDPP_CMD_DEC_STATE_M          	0x4000

#define AUDPP_CMD_UPDATDE_CFG_DEC	0x8000
#define AUDPP_CMD_DONT_UPDATE_CFG_DEC	0x0000


/* Type specification of cmd_cfg_dec */

typedef struct {
	unsigned short cmd_id;
	unsigned short dec0_cfg;
	unsigned short dec1_cfg;
	unsigned short dec2_cfg;
	unsigned short dec3_cfg;
	unsigned short dec4_cfg;
} __attribute__((packed)) audpp_cmd_cfg_dec_type;

/*
 * Command Structure to Pause , Resume and flushes the selected audio decoders
 */

#define AUDPP_CMD_DEC_CTRL		0x0002
#define AUDPP_CMD_DEC_CTRL_LEN		sizeof(audpp_cmd_dec_ctrl)

/* Decoder control commands for pause, resume and flush */
#define AUDPP_CMD_FLUSH_V         		0x2000

#define AUDPP_CMD_PAUSE_V		        0x4000
#define AUDPP_CMD_RESUME_V		        0x0000

#define AUDPP_CMD_UPDATE_V		        0x8000
#define AUDPP_CMD_IGNORE_V		        0x0000


/* Type Spec for decoder control command*/

typedef struct {
	unsigned short cmd_id;
	unsigned short dec0_ctrl;
	unsigned short dec1_ctrl;
	unsigned short dec2_ctrl;
	unsigned short dec3_ctrl;
	unsigned short dec4_ctrl;
} __attribute__((packed)) audpp_cmd_dec_ctrl;

/*
 * Command Structure to Configure the AVSync FeedBack Mechanism
 */

#define AUDPP_CMD_AVSYNC	0x0003
#define AUDPP_CMD_AVSYNC_LEN	sizeof(audpp_cmd_avsync)

typedef struct {
	unsigned short cmd_id;
	unsigned short object_number;
	unsigned short interrupt_interval_lsw;
	unsigned short interrupt_interval_msw;
} __attribute__((packed)) audpp_cmd_avsync;

/*
 * Command Structure to enable or disable(sleep) the   AUDPPTASK
 */

#define AUDPP_CMD_CFG	0x0004
#define AUDPP_CMD_CFG_LEN	sizeof(audpp_cmd_cfg)

#define AUDPP_CMD_CFG_SLEEP   				0x0000
#define AUDPP_CMD_CFG_ENABLE  				0xFFFF

typedef struct {
	unsigned short cmd_id;
	unsigned short cfg;
} __attribute__((packed)) audpp_cmd_cfg;

/*
 * Command Structure to Inject or drop the specified no of samples
 */

#define AUDPP_CMD_ADJUST_SAMP		0x0005
#define AUDPP_CMD_ADJUST_SAMP_LEN	sizeof(audpp_cmd_adjust_samp)

#define AUDPP_CMD_SAMP_DROP		-1
#define AUDPP_CMD_SAMP_INSERT		0x0001

#define AUDPP_CMD_NUM_SAMPLES		0x0001

typedef struct {
	unsigned short cmd_id;
	unsigned short object_no;
	signed short sample_insert_or_drop;
	unsigned short num_samples;
} __attribute__((packed)) audpp_cmd_adjust_samp;

/*
 * Command Structure to Configure AVSync Feedback Mechanism
 */

#define AUDPP_CMD_AVSYNC_CMD_2		0x0006
#define AUDPP_CMD_AVSYNC_CMD_2_LEN	sizeof(audpp_cmd_avsync_cmd_2)

typedef struct {
	unsigned short cmd_id;
	unsigned short object_number;
	unsigned short interrupt_interval_lsw;
	unsigned short interrupt_interval_msw;
	unsigned short sample_counter_dlsw;
	unsigned short sample_counter_dmsw;
	unsigned short sample_counter_msw;
	unsigned short byte_counter_dlsw;
	unsigned short byte_counter_dmsw;
	unsigned short byte_counter_msw;
} __attribute__((packed)) audpp_cmd_avsync_cmd_2;

/*
 * Command Structure to Configure AVSync Feedback Mechanism
 */

#define AUDPP_CMD_AVSYNC_CMD_3		0x0007
#define AUDPP_CMD_AVSYNC_CMD_3_LEN	sizeof(audpp_cmd_avsync_cmd_3)

typedef struct {
	unsigned short cmd_id;
	unsigned short object_number;
	unsigned short interrupt_interval_lsw;
	unsigned short interrupt_interval_msw;
	unsigned short sample_counter_dlsw;
	unsigned short sample_counter_dmsw;
	unsigned short sample_counter_msw;
	unsigned short byte_counter_dlsw;
	unsigned short byte_counter_dmsw;
	unsigned short byte_counter_msw;
} __attribute__((packed)) audpp_cmd_avsync_cmd_3;

#define AUDPP_CMD_ROUTING_MODE      0x0008
#define AUDPP_CMD_ROUTING_MODE_LEN  \
sizeof(struct audpp_cmd_routing_mode)

struct audpp_cmd_routing_mode {
	unsigned short cmd_id;
	unsigned short object_number;
	unsigned short routing_mode;
} __attribute__((packed));

/*
 * Commands Related to uPAudPPCmd2Queue
 */

/*
 * Command Structure to configure Per decoder Parameters (Common)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS 		0x0000
#define AUDPP_CMD_CFG_ADEC_PARAMS_COMMON_LEN	\
	sizeof(audpp_cmd_cfg_adec_params_common)

#define AUDPP_CMD_STATUS_MSG_FLAG_ENA_FCM	0x4000
#define AUDPP_CMD_STATUS_MSG_FLAG_DIS_FCM	0x0000

#define AUDPP_CMD_STATUS_MSG_FLAG_ENA_DCM	0x8000
#define AUDPP_CMD_STATUS_MSG_FLAG_DIS_DCM	0x0000

/* Sampling frequency*/
#define  AUDPP_CMD_SAMP_RATE_96000 	0x0000
#define  AUDPP_CMD_SAMP_RATE_88200 	0x0001
#define  AUDPP_CMD_SAMP_RATE_64000 	0x0002
#define  AUDPP_CMD_SAMP_RATE_48000 	0x0003
#define  AUDPP_CMD_SAMP_RATE_44100 	0x0004
#define  AUDPP_CMD_SAMP_RATE_32000 	0x0005
#define  AUDPP_CMD_SAMP_RATE_24000 	0x0006
#define  AUDPP_CMD_SAMP_RATE_22050 	0x0007
#define  AUDPP_CMD_SAMP_RATE_16000 	0x0008
#define  AUDPP_CMD_SAMP_RATE_12000 	0x0009
#define  AUDPP_CMD_SAMP_RATE_11025 	0x000A
#define  AUDPP_CMD_SAMP_RATE_8000  	0x000B


/*
 * Type specification of cmd_adec_cfg sent to all decoder
 */

typedef struct {
  unsigned short cmd_id;
  unsigned short  length;
  unsigned short  dec_id;
  unsigned short  status_msg_flag;
  unsigned short  decoder_frame_counter_msg_period;
  unsigned short  input_sampling_frequency;
} __attribute__((packed)) audpp_cmd_cfg_adec_params_common;

/*
 * Command Structure to configure Per decoder Parameters (Wav)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS_WAV_LEN	\
	sizeof(audpp_cmd_cfg_adec_params_wav)


#define AUDPP_CMD_WAV_STEREO_CFG_MONO	0x0001
#define AUDPP_CMD_WAV_STEREO_CFG_STEREO	0x0002

#define AUDPP_CMD_WAV_PCM_WIDTH_8	0x0000
#define AUDPP_CMD_WAV_PCM_WIDTH_16	0x0001
#define AUDPP_CMD_WAV_PCM_WIDTH_32	0x0002

typedef struct {
	audpp_cmd_cfg_adec_params_common		common;
	unsigned short					stereo_cfg;
	unsigned short					pcm_width;
	unsigned short 					sign;
} __attribute__((packed)) audpp_cmd_cfg_adec_params_wav;

/*
 * Command Structure to configure Per decoder Parameters (ADPCM)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS_ADPCM_LEN	\
	sizeof(audpp_cmd_cfg_adec_params_adpcm)


#define	AUDPP_CMD_ADPCM_STEREO_CFG_MONO		0x0001
#define AUDPP_CMD_ADPCM_STEREO_CFG_STEREO	0x0002

typedef struct {
	audpp_cmd_cfg_adec_params_common		common;
	unsigned short					stereo_cfg;
	unsigned short 					block_size;
} __attribute__((packed)) audpp_cmd_cfg_adec_params_adpcm;

/*
 * Command Structure to configure Per decoder Parameters (MP3)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS_MP3_LEN	\
	sizeof(audpp_cmd_cfg_adec_params_mp3)

typedef struct {
   audpp_cmd_cfg_adec_params_common    common;
} __attribute__((packed)) audpp_cmd_cfg_adec_params_mp3;


/*
 * Command Structure to configure Per decoder Parameters (AAC)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS_AAC_LEN	\
	sizeof(audpp_cmd_cfg_adec_params_aac)


#define AUDPP_CMD_AAC_FORMAT_ADTS		-1
#define	AUDPP_CMD_AAC_FORMAT_RAW		0x0000
#define	AUDPP_CMD_AAC_FORMAT_PSUEDO_RAW		0x0001
#define	AUDPP_CMD_AAC_FORMAT_LOAS		0x0002

#define AUDPP_CMD_AAC_AUDIO_OBJECT_LC		0x0002
#define AUDPP_CMD_AAC_AUDIO_OBJECT_LTP		0x0004
#define AUDPP_CMD_AAC_AUDIO_OBJECT_ERLC	0x0011

#define AUDPP_CMD_AAC_SBR_ON_FLAG_ON		0x0001
#define AUDPP_CMD_AAC_SBR_ON_FLAG_OFF		0x0000

#define AUDPP_CMD_AAC_SBR_PS_ON_FLAG_ON		0x0001
#define AUDPP_CMD_AAC_SBR_PS_ON_FLAG_OFF	0x0000

typedef struct {
  audpp_cmd_cfg_adec_params_common	common;
  signed short				format;
  unsigned short			audio_object;
  unsigned short			ep_config;
  unsigned short                        aac_section_data_resilience_flag;
  unsigned short                        aac_scalefactor_data_resilience_flag;
  unsigned short                        aac_spectral_data_resilience_flag;
  unsigned short                        sbr_on_flag;
  unsigned short                        sbr_ps_on_flag;
  unsigned short                        dual_mono_mode;
  unsigned short                        channel_configuration;
} __attribute__((packed)) audpp_cmd_cfg_adec_params_aac;

/*
 * Command Structure to configure Per decoder Parameters (V13K)
 */

#define AUDPP_CMD_CFG_ADEC_PARAMS_V13K_LEN	\
	sizeof(struct audpp_cmd_cfg_adec_params_v13k)


#define AUDPP_CMD_STEREO_CFG_MONO		0x0001
#define AUDPP_CMD_STEREO_CFG_STEREO		0x0002

struct audpp_cmd_cfg_adec_params_v13k {
   audpp_cmd_cfg_adec_params_common    	common;
   unsigned short			stereo_cfg;
} __attribute__((packed));

#define AUDPP_CMD_CFG_ADEC_PARAMS_EVRC_LEN \
	sizeof(struct audpp_cmd_cfg_adec_params_evrc)

struct audpp_cmd_cfg_adec_params_evrc {
	audpp_cmd_cfg_adec_params_common common;
	unsigned short stereo_cfg;
} __attribute__ ((packed));

/*
 * Command Structure to configure the  HOST PCM interface
 */

#define AUDPP_CMD_PCM_INTF	0x0001
#define AUDPP_CMD_PCM_INTF_2	0x0002
#define AUDPP_CMD_PCM_INTF_LEN	sizeof(audpp_cmd_pcm_intf)

#define AUDPP_CMD_PCM_INTF_MONO_V		        0x0001
#define AUDPP_CMD_PCM_INTF_STEREO_V         	0x0002

/* These two values differentiate the two types of commands that could be issued
 * Interface configuration command and Buffer update command */

#define AUDPP_CMD_PCM_INTF_CONFIG_CMD_V	       	0x0000
#define AUDPP_CMD_PCM_INTF_BUFFER_CMD_V	        -1

#define AUDPP_CMD_PCM_INTF_RX_ENA_M              0x000F
#define AUDPP_CMD_PCM_INTF_RX_ENA_ARMTODSP_V     0x0008
#define AUDPP_CMD_PCM_INTF_RX_ENA_DSPTOARM_V     0x0004

/* These flags control the enabling and disabling of the interface together
 *  with host interface bit mask. */

#define AUDPP_CMD_PCM_INTF_ENA_V            -1
#define AUDPP_CMD_PCM_INTF_DIS_V            0x0000


#define  AUDPP_CMD_PCM_INTF_FULL_DUPLEX           0x0
#define  AUDPP_CMD_PCM_INTF_HALF_DUPLEX_TODSP     0x1


#define  AUDPP_CMD_PCM_INTF_OBJECT_NUM           0x5
#define  AUDPP_CMD_PCM_INTF_COMMON_OBJECT_NUM    0x6


typedef struct {
	unsigned short  cmd_id;
	unsigned short  object_num;
	signed short  config;
	unsigned short  intf_type;

	/* DSP -> ARM Configuration */
	unsigned short  read_buf1LSW;
	unsigned short  read_buf1MSW;
	unsigned short  read_buf1_len;

	unsigned short  read_buf2LSW;
	unsigned short  read_buf2MSW;
	unsigned short  read_buf2_len;
	/*   0:HOST_PCM_INTF disable
	**  0xFFFF: HOST_PCM_INTF enable
	*/
	signed short  dsp_to_arm_flag;
	unsigned short  partition_number;

	/* ARM -> DSP Configuration */
	unsigned short  write_buf1LSW;
	unsigned short  write_buf1MSW;
	unsigned short  write_buf1_len;

	unsigned short  write_buf2LSW;
	unsigned short  write_buf2MSW;
	unsigned short  write_buf2_len;

	/*   0:HOST_PCM_INTF disable
	**  0xFFFF: HOST_PCM_INTF enable
	*/
	signed short  arm_to_rx_flag;
	unsigned short  weight_decoder_to_rx;
	unsigned short  weight_arm_to_rx;

	unsigned short  partition_number_arm_to_dsp;
	unsigned short  sample_rate;
	unsigned short  channel_mode;
} __attribute__((packed)) audpp_cmd_pcm_intf;

/*
 **  BUFFER UPDATE COMMAND
 */
#define AUDPP_CMD_PCM_INTF_SEND_BUF_PARAMS_LEN	\
	sizeof(audpp_cmd_pcm_intf_send_buffer)

typedef struct {
  unsigned short  cmd_id;
  unsigned short  host_pcm_object;
  /* set config = 0xFFFF for configuration*/
  signed short  config;
  unsigned short  intf_type;
  unsigned short  dsp_to_arm_buf_id;
  unsigned short  arm_to_dsp_buf_id;
  unsigned short  arm_to_dsp_buf_len;
} __attribute__((packed)) audpp_cmd_pcm_intf_send_buffer;


/*
 * Commands Related to uPAudPPCmd3Queue
 */

/*
 * Command Structure to configure post processing params (Commmon)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS		0x0000
#define AUDPP_CMD_CFG_OBJECT_PARAMS_COMMON_LEN		\
	sizeof(audpp_cmd_cfg_object_params_common)

#define AUDPP_CMD_OBJ0_UPDATE		0x8000
#define AUDPP_CMD_OBJ0_DONT_UPDATE	0x0000

#define AUDPP_CMD_OBJ1_UPDATE		0x8000
#define AUDPP_CMD_OBJ1_DONT_UPDATE	0x0000

#define AUDPP_CMD_OBJ2_UPDATE		0x8000
#define AUDPP_CMD_OBJ2_DONT_UPDATE	0x0000

#define AUDPP_CMD_OBJ3_UPDATE		0x8000
#define AUDPP_CMD_OBJ3_DONT_UPDATE	0x0000

#define AUDPP_CMD_OBJ4_UPDATE		0x8000
#define AUDPP_CMD_OBJ4_DONT_UPDATE	0x0000

#define AUDPP_CMD_HPCM_UPDATE		0x8000
#define AUDPP_CMD_HPCM_DONT_UPDATE	0x0000

#define AUDPP_CMD_COMMON_CFG_UPDATE		0x8000
#define AUDPP_CMD_COMMON_CFG_DONT_UPDATE	0x0000

typedef struct {
	unsigned short  cmd_id;
	unsigned short	obj0_cfg;
	unsigned short	obj1_cfg;
	unsigned short	obj2_cfg;
	unsigned short	obj3_cfg;
	unsigned short	obj4_cfg;
	unsigned short	host_pcm_obj_cfg;
	unsigned short	comman_cfg;
	unsigned short  command_type;
} __attribute__((packed)) audpp_cmd_cfg_object_params_common;

/*
 * Command Structure to configure post processing params (Volume)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_VOLUME_LEN		\
	sizeof(audpp_cmd_cfg_object_params_volume)

typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	unsigned short					volume;
	unsigned short					pan;
} __attribute__((packed)) audpp_cmd_cfg_object_params_volume;

/*
 * Command Structure to configure post processing params (PCM Filter) --DOUBT
 */

typedef struct {
	unsigned short			numerator_b0_filter_lsw;
	unsigned short			numerator_b0_filter_msw;
	unsigned short			numerator_b1_filter_lsw;
	unsigned short			numerator_b1_filter_msw;
	unsigned short			numerator_b2_filter_lsw;
	unsigned short			numerator_b2_filter_msw;
} __attribute__((packed)) numerator;

typedef struct {
	unsigned short			denominator_a0_filter_lsw;
	unsigned short			denominator_a0_filter_msw;
	unsigned short			denominator_a1_filter_lsw;
	unsigned short			denominator_a1_filter_msw;
} __attribute__((packed)) denominator;

typedef struct {
	unsigned short			shift_factor_0;
} __attribute__((packed)) shift_factor;

typedef struct {
	unsigned short			pan_filter_0;
} __attribute__((packed)) pan;

typedef struct {
		numerator		numerator_filter;
		denominator		denominator_filter;
		shift_factor		shift_factor_filter;
		pan			pan_filter;
} __attribute__((packed)) filter_1;

typedef struct {
		numerator		numerator_filter[2];
		denominator		denominator_filter[2];
		shift_factor		shift_factor_filter[2];
		pan			pan_filter[2];
} __attribute__((packed)) filter_2;

typedef struct {
		numerator		numerator_filter[3];
		denominator		denominator_filter[3];
		shift_factor		shift_factor_filter[3];
		pan			pan_filter[3];
} __attribute__((packed)) filter_3;

typedef struct {
		numerator		numerator_filter[4];
		denominator		denominator_filter[4];
		shift_factor		shift_factor_filter[4];
		pan			pan_filter[4];
} __attribute__((packed)) filter_4;

#define AUDPP_CMD_CFG_OBJECT_PARAMS_PCM_LEN		\
	sizeof(audpp_cmd_cfg_object_params_pcm)


typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	unsigned short				active_flag;
	unsigned short 				num_bands;
	union {
		filter_1			filter_1_params;
		filter_2			filter_2_params;
		filter_3			filter_3_params;
		filter_4			filter_4_params;
	} __attribute__((packed)) params_filter;
} __attribute__((packed)) audpp_cmd_cfg_object_params_pcm;


/*
 * Command Structure to configure post processing parameters (equalizer)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_EQALIZER_LEN		\
	sizeof(audpp_cmd_cfg_object_params_eqalizer)

typedef struct {
	unsigned short			numerator_coeff_0_lsw;
	unsigned short			numerator_coeff_0_msw;
	unsigned short			numerator_coeff_1_lsw;
	unsigned short			numerator_coeff_1_msw;
	unsigned short			numerator_coeff_2_lsw;
	unsigned short			numerator_coeff_2_msw;
} __attribute__((packed)) eq_numerator;

typedef struct {
	unsigned short			denominator_coeff_0_lsw;
	unsigned short			denominator_coeff_0_msw;
	unsigned short			denominator_coeff_1_lsw;
	unsigned short			denominator_coeff_1_msw;
} __attribute__((packed)) eq_denominator;

typedef struct {
	unsigned short			shift_factor;
} __attribute__((packed)) eq_shiftfactor;

typedef struct {
	eq_numerator	numerator;
	eq_denominator	denominator;
	eq_shiftfactor	shiftfactor;
} __attribute__((packed)) eq_coeff_1;

typedef struct {
	eq_numerator	numerator[2];
	eq_denominator	denominator[2];
	eq_shiftfactor	shiftfactor[2];
} __attribute__((packed)) eq_coeff_2;

typedef struct {
	eq_numerator	numerator[3];
	eq_denominator	denominator[3];
	eq_shiftfactor	shiftfactor[3];
} __attribute__((packed)) eq_coeff_3;

typedef struct {
	eq_numerator	numerator[4];
	eq_denominator	denominator[4];
	eq_shiftfactor	shiftfactor[4];
} __attribute__((packed)) eq_coeff_4;

typedef struct {
	eq_numerator	numerator[5];
	eq_denominator	denominator[5];
	eq_shiftfactor	shiftfactor[5];
} __attribute__((packed)) eq_coeff_5;

typedef struct {
	eq_numerator	numerator[6];
	eq_denominator	denominator[6];
	eq_shiftfactor	shiftfactor[6];
} __attribute__((packed)) eq_coeff_6;

typedef struct {
	eq_numerator	numerator[7];
	eq_denominator	denominator[7];
	eq_shiftfactor	shiftfactor[7];
} __attribute__((packed)) eq_coeff_7;

typedef struct {
	eq_numerator	numerator[8];
	eq_denominator	denominator[8];
	eq_shiftfactor	shiftfactor[8];
} __attribute__((packed)) eq_coeff_8;

typedef struct {
	eq_numerator	numerator[9];
	eq_denominator	denominator[9];
	eq_shiftfactor	shiftfactor[9];
} __attribute__((packed)) eq_coeff_9;

typedef struct {
	eq_numerator	numerator[10];
	eq_denominator	denominator[10];
	eq_shiftfactor	shiftfactor[10];
} __attribute__((packed)) eq_coeff_10;

typedef struct {
	eq_numerator	numerator[11];
	eq_denominator	denominator[11];
	eq_shiftfactor	shiftfactor[11];
} __attribute__((packed)) eq_coeff_11;

typedef struct {
	eq_numerator	numerator[12];
	eq_denominator	denominator[12];
	eq_shiftfactor	shiftfactor[12];
} __attribute__((packed)) eq_coeff_12;


typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	unsigned short				eq_flag;
	unsigned short				num_bands;
	union {
		eq_coeff_1	eq_coeffs_1;
		eq_coeff_2	eq_coeffs_2;
		eq_coeff_3	eq_coeffs_3;
		eq_coeff_4	eq_coeffs_4;
		eq_coeff_5	eq_coeffs_5;
		eq_coeff_6	eq_coeffs_6;
		eq_coeff_7	eq_coeffs_7;
		eq_coeff_8	eq_coeffs_8;
		eq_coeff_9	eq_coeffs_9;
		eq_coeff_10	eq_coeffs_10;
		eq_coeff_11	eq_coeffs_11;
		eq_coeff_12	eq_coeffs_12;
	} __attribute__((packed)) eq_coeff;
} __attribute__((packed)) audpp_cmd_cfg_object_params_eqalizer;


/*
 * Command Structure to configure post processing parameters (ADRC)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_ADRC_LEN		\
	sizeof(audpp_cmd_cfg_object_params_adrc)


#define AUDPP_CMD_ADRC_FLAG_DIS		0x0000
#define AUDPP_CMD_ADRC_FLAG_ENA		-1

typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	signed short				adrc_flag;
	unsigned short				compression_th;
	unsigned short				compression_slope;
	unsigned short				rms_time;
	unsigned short				attack_const_lsw;
	unsigned short				attack_const_msw;
	unsigned short				release_const_lsw;
	unsigned short				release_const_msw;
	unsigned short				adrc_system_delay;
} __attribute__((packed)) audpp_cmd_cfg_object_params_adrc;

/*
 * Command Structure to configure post processing parameters(Spectrum Analizer)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_SPECTRAM_LEN		\
	sizeof(audpp_cmd_cfg_object_params_spectram)


typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	unsigned short				sample_interval;
	unsigned short				num_coeff;
} __attribute__((packed)) audpp_cmd_cfg_object_params_spectram;

/*
 * Command Structure to configure post processing parameters (QConcert)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_QCONCERT_LEN		\
	sizeof(audpp_cmd_cfg_object_params_qconcert)


#define AUDPP_CMD_QCON_ENA_FLAG_ENA		-1
#define AUDPP_CMD_QCON_ENA_FLAG_DIS		0x0000

#define AUDPP_CMD_QCON_OP_MODE_HEADPHONE	-1
#define AUDPP_CMD_QCON_OP_MODE_SPEAKER_FRONT	0x0000
#define AUDPP_CMD_QCON_OP_MODE_SPEAKER_SIDE	0x0001
#define AUDPP_CMD_QCON_OP_MODE_SPEAKER_DESKTOP	0x0002

#define AUDPP_CMD_QCON_GAIN_UNIT			0x7FFF
#define AUDPP_CMD_QCON_GAIN_SIX_DB			0x4027


#define AUDPP_CMD_QCON_EXPANSION_MAX		0x7FFF


typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	signed short				enable_flag;
	signed short				output_mode;
	signed short				gain;
	signed short				expansion;
	signed short				delay;
	unsigned short				stages_per_mode;
} __attribute__((packed)) audpp_cmd_cfg_object_params_qconcert;

/*
 * Command Structure to configure post processing parameters (Side Chain)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_SIDECHAIN_LEN		\
	sizeof(audpp_cmd_cfg_object_params_sidechain)


#define AUDPP_CMD_SIDECHAIN_ACTIVE_FLAG_DIS	0x0000
#define AUDPP_CMD_SIDECHAIN_ACTIVE_FLAG_ENA	-1

typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	signed short				active_flag;
	unsigned short				num_bands;
	union {
		filter_1			filter_1_params;
		filter_2			filter_2_params;
		filter_3			filter_3_params;
		filter_4			filter_4_params;
	} __attribute__((packed)) params_filter;
} __attribute__((packed)) audpp_cmd_cfg_object_params_sidechain;


/*
 * Command Structure to configure post processing parameters (QAFX)
 */

#define AUDPP_CMD_CFG_OBJECT_PARAMS_QAFX_LEN		\
	sizeof(audpp_cmd_cfg_object_params_qafx)

#define AUDPP_CMD_QAFX_ENA_DISA		0x0000
#define AUDPP_CMD_QAFX_ENA_ENA_CFG	-1
#define AUDPP_CMD_QAFX_ENA_DIS_CFG	0x0001

#define AUDPP_CMD_QAFX_CMD_TYPE_ENV	0x0100
#define AUDPP_CMD_QAFX_CMD_TYPE_OBJ	0x0010
#define AUDPP_CMD_QAFX_CMD_TYPE_QUERY	0x1000

#define AUDPP_CMD_QAFX_CMDS_ENV_OP_MODE	0x0100
#define AUDPP_CMD_QAFX_CMDS_ENV_LIS_POS	0x0101
#define AUDPP_CMD_QAFX_CMDS_ENV_LIS_ORI	0x0102
#define AUDPP_CMD_QAFX_CMDS_ENV_LIS_VEL	0X0103
#define AUDPP_CMD_QAFX_CMDS_ENV_ENV_RES	0x0107

#define AUDPP_CMD_QAFX_CMDS_OBJ_SAMP_FREQ	0x0010
#define AUDPP_CMD_QAFX_CMDS_OBJ_VOL		0x0011
#define AUDPP_CMD_QAFX_CMDS_OBJ_DIST		0x0012
#define AUDPP_CMD_QAFX_CMDS_OBJ_POS		0x0013
#define AUDPP_CMD_QAFX_CMDS_OBJ_VEL		0x0014


typedef struct {
	audpp_cmd_cfg_object_params_common 	common;
	signed short				enable;
	unsigned short				command_type;
	unsigned short				num_commands;
	unsigned short				commands;
} __attribute__((packed)) audpp_cmd_cfg_object_params_qafx;

/*
 * Command Structure to enable , disable or configure the reverberation effect
 * (Common)
 */

#define AUDPP_CMD_REVERB_CONFIG		0x0001
#define	AUDPP_CMD_REVERB_CONFIG_COMMON_LEN	\
	sizeof(audpp_cmd_reverb_config_common)

#define AUDPP_CMD_ENA_ENA	0xFFFF
#define AUDPP_CMD_ENA_DIS	0x0000
#define AUDPP_CMD_ENA_CFG	0x0001

#define AUDPP_CMD_CMD_TYPE_ENV		0x0104
#define AUDPP_CMD_CMD_TYPE_OBJ		0x0015
#define AUDPP_CMD_CMD_TYPE_QUERY	0x1000


typedef struct {
	unsigned short			cmd_id;
	unsigned short			enable;
	unsigned short			cmd_type;
} __attribute__((packed)) audpp_cmd_reverb_config_common;

/*
 * Command Structure to enable , disable or configure the reverberation effect
 * (ENV-0x0104)
 */

#define	AUDPP_CMD_REVERB_CONFIG_ENV_104_LEN	\
	sizeof(audpp_cmd_reverb_config_env_104)

typedef struct {
	audpp_cmd_reverb_config_common	common;
	unsigned short			env_gain;
	unsigned short			decay_msw;
	unsigned short			decay_lsw;
	unsigned short			decay_timeratio_msw;
	unsigned short			decay_timeratio_lsw;
	unsigned short			delay_time;
	unsigned short			reverb_gain;
	unsigned short			reverb_delay;
} __attribute__((packed)) audpp_cmd_reverb_config_env_104;

/*
 * Command Structure to enable , disable or configure the reverberation effect
 * (ENV-0x0015)
 */

#define	AUDPP_CMD_REVERB_CONFIG_ENV_15_LEN	\
	sizeof(audpp_cmd_reverb_config_env_15)

typedef struct {
	audpp_cmd_reverb_config_common	common;
	unsigned short			object_num;
	unsigned short			absolute_gain;
} __attribute__((packed)) audpp_cmd_reverb_config_env_15;


#endif /* QDSP5AUDPPCMDI_H */

