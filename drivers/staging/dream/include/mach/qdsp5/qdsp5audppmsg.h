#ifndef QDSP5AUDPPMSG_H
#define QDSP5AUDPPMSG_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

       Q D S P 5  A U D I O   P O S T   P R O C E S S I N G   M S G

GENERAL DESCRIPTION
  Messages sent by AUDPPTASK to ARM

REFERENCES
  None

EXTERNALIZED FUNCTIONS
  None

Copyright(c) 1992 - 2009 by QUALCOMM, Incorporated.

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

 $Header: //source/qcom/qct/multimedia2/Audio/drivers/QDSP5Driver/QDSP5Interface/main/latest/qdsp5audppmsg.h#4 $

===========================================================================*/

/*
 * AUDPPTASK uses audPPuPRlist to send messages to the ARM
 * Location : MEMA
 * Buffer Size : 45
 * No of Buffers in a queue : 5 for gaming audio and 1 for other images
 */

/*
 * MSG to Informs the ARM os Success/Failure of bringing up the decoder
 */

#define AUDPP_MSG_STATUS_MSG		0x0001
#define AUDPP_MSG_STATUS_MSG_LEN	\
	sizeof(audpp_msg_status_msg)

#define AUDPP_MSG_STATUS_SLEEP		0x0000
#define AUDPP_MSG__STATUS_INIT		0x0001
#define AUDPP_MSG_MSG_STATUS_CFG	0x0002
#define AUDPP_MSG_STATUS_PLAY		0x0003

#define AUDPP_MSG_REASON_MIPS	0x0000
#define AUDPP_MSG_REASON_MEM	0x0001

typedef struct{
	unsigned short dec_id;
	unsigned short status;
	unsigned short reason;
} __attribute__((packed)) audpp_msg_status_msg;

/*
 * MSG to communicate the spectrum analyzer output bands to the ARM
 */
#define AUDPP_MSG_SPA_BANDS		0x0002
#define AUDPP_MSG_SPA_BANDS_LEN	\
	sizeof(audpp_msg_spa_bands)

typedef struct {
	unsigned short			current_object;
	unsigned short			spa_band_1;
	unsigned short			spa_band_2;
	unsigned short			spa_band_3;
	unsigned short			spa_band_4;
	unsigned short			spa_band_5;
	unsigned short			spa_band_6;
	unsigned short			spa_band_7;
	unsigned short			spa_band_8;
	unsigned short			spa_band_9;
	unsigned short			spa_band_10;
	unsigned short			spa_band_11;
	unsigned short			spa_band_12;
	unsigned short			spa_band_13;
	unsigned short			spa_band_14;
	unsigned short			spa_band_15;
	unsigned short			spa_band_16;
	unsigned short			spa_band_17;
	unsigned short			spa_band_18;
	unsigned short			spa_band_19;
	unsigned short			spa_band_20;
	unsigned short			spa_band_21;
	unsigned short			spa_band_22;
	unsigned short			spa_band_23;
	unsigned short			spa_band_24;
	unsigned short			spa_band_25;
	unsigned short			spa_band_26;
	unsigned short			spa_band_27;
	unsigned short			spa_band_28;
	unsigned short			spa_band_29;
	unsigned short			spa_band_30;
	unsigned short			spa_band_31;
	unsigned short			spa_band_32;
} __attribute__((packed)) audpp_msg_spa_bands;

/*
 * MSG to communicate the PCM I/O buffer status to ARM
 */
#define  AUDPP_MSG_HOST_PCM_INTF_MSG		0x0003
#define  AUDPP_MSG_HOST_PCM_INTF_MSG_LEN	\
	sizeof(audpp_msg_host_pcm_intf_msg)

#define AUDPP_MSG_HOSTPCM_ID_TX_ARM	0x0000
#define AUDPP_MSG_HOSTPCM_ID_ARM_TX	0x0001
#define AUDPP_MSG_HOSTPCM_ID_RX_ARM	0x0002
#define AUDPP_MSG_HOSTPCM_ID_ARM_RX	0x0003

#define AUDPP_MSG_SAMP_FREQ_INDX_96000	0x0000
#define AUDPP_MSG_SAMP_FREQ_INDX_88200	0x0001
#define AUDPP_MSG_SAMP_FREQ_INDX_64000	0x0002
#define AUDPP_MSG_SAMP_FREQ_INDX_48000	0x0003
#define AUDPP_MSG_SAMP_FREQ_INDX_44100	0x0004
#define AUDPP_MSG_SAMP_FREQ_INDX_32000	0x0005
#define AUDPP_MSG_SAMP_FREQ_INDX_24000	0x0006
#define AUDPP_MSG_SAMP_FREQ_INDX_22050	0x0007
#define AUDPP_MSG_SAMP_FREQ_INDX_16000	0x0008
#define AUDPP_MSG_SAMP_FREQ_INDX_12000	0x0009
#define AUDPP_MSG_SAMP_FREQ_INDX_11025	0x000A
#define AUDPP_MSG_SAMP_FREQ_INDX_8000	0x000B

#define AUDPP_MSG_CHANNEL_MODE_MONO		0x0001
#define AUDPP_MSG_CHANNEL_MODE_STEREO	0x0002

typedef struct{
	unsigned short obj_num;
	unsigned short numbers_of_samples;
	unsigned short host_pcm_id;
	unsigned short buf_indx;
	unsigned short samp_freq_indx;
	unsigned short channel_mode;
} __attribute__((packed)) audpp_msg_host_pcm_intf_msg;


/*
 * MSG to communicate 3D position of the source and listener , source volume
 * source rolloff, source orientation
 */

#define AUDPP_MSG_QAFX_POS		0x0004
#define AUDPP_MSG_QAFX_POS_LEN		\
	sizeof(audpp_msg_qafx_pos)

typedef struct {
	unsigned short	current_object;
	unsigned short	x_pos_lis_msw;
	unsigned short	x_pos_lis_lsw;
	unsigned short	y_pos_lis_msw;
	unsigned short	y_pos_lis_lsw;
	unsigned short	z_pos_lis_msw;
	unsigned short	z_pos_lis_lsw;
	unsigned short	x_fwd_msw;
	unsigned short	x_fwd_lsw;
	unsigned short	y_fwd_msw;
	unsigned short	y_fwd_lsw;
	unsigned short	z_fwd_msw;
	unsigned short	z_fwd_lsw;
	unsigned short 	x_up_msw;
	unsigned short	x_up_lsw;
	unsigned short 	y_up_msw;
	unsigned short	y_up_lsw;
	unsigned short 	z_up_msw;
	unsigned short	z_up_lsw;
	unsigned short 	x_vel_lis_msw;
	unsigned short 	x_vel_lis_lsw;
	unsigned short 	y_vel_lis_msw;
	unsigned short 	y_vel_lis_lsw;
	unsigned short 	z_vel_lis_msw;
	unsigned short 	z_vel_lis_lsw;
	unsigned short	threed_enable_flag;
	unsigned short 	volume;
	unsigned short	x_pos_source_msw;
	unsigned short	x_pos_source_lsw;
	unsigned short	y_pos_source_msw;
	unsigned short	y_pos_source_lsw;
	unsigned short	z_pos_source_msw;
	unsigned short	z_pos_source_lsw;
	unsigned short	max_dist_0_msw;
	unsigned short	max_dist_0_lsw;
	unsigned short	min_dist_0_msw;
	unsigned short	min_dist_0_lsw;
	unsigned short	roll_off_factor;
	unsigned short	mute_after_max_flag;
	unsigned short	x_vel_source_msw;
	unsigned short	x_vel_source_lsw;
	unsigned short	y_vel_source_msw;
	unsigned short	y_vel_source_lsw;
	unsigned short	z_vel_source_msw;
	unsigned short	z_vel_source_lsw;
} __attribute__((packed)) audpp_msg_qafx_pos;

/*
 * MSG to provide AVSYNC feedback from DSP to ARM
 */

#define AUDPP_MSG_AVSYNC_MSG		0x0005
#define AUDPP_MSG_AVSYNC_MSG_LEN	\
	sizeof(audpp_msg_avsync_msg)

typedef struct {
	unsigned short	active_flag;
	unsigned short	num_samples_counter0_HSW;
	unsigned short	num_samples_counter0_MSW;
	unsigned short	num_samples_counter0_LSW;
	unsigned short	num_bytes_counter0_HSW;
	unsigned short	num_bytes_counter0_MSW;
	unsigned short	num_bytes_counter0_LSW;
	unsigned short	samp_freq_obj_0;
	unsigned short	samp_freq_obj_1;
	unsigned short	samp_freq_obj_2;
	unsigned short	samp_freq_obj_3;
	unsigned short	samp_freq_obj_4;
	unsigned short	samp_freq_obj_5;
	unsigned short	samp_freq_obj_6;
	unsigned short	samp_freq_obj_7;
	unsigned short	samp_freq_obj_8;
	unsigned short	samp_freq_obj_9;
	unsigned short	samp_freq_obj_10;
	unsigned short	samp_freq_obj_11;
	unsigned short	samp_freq_obj_12;
	unsigned short	samp_freq_obj_13;
	unsigned short	samp_freq_obj_14;
	unsigned short	samp_freq_obj_15;
	unsigned short	num_samples_counter4_HSW;
	unsigned short	num_samples_counter4_MSW;
	unsigned short	num_samples_counter4_LSW;
	unsigned short	num_bytes_counter4_HSW;
	unsigned short	num_bytes_counter4_MSW;
	unsigned short	num_bytes_counter4_LSW;
} __attribute__((packed)) audpp_msg_avsync_msg;

/*
 * MSG to provide PCM DMA Missed feedback from the DSP to ARM
 */

#define  AUDPP_MSG_PCMDMAMISSED	0x0006
#define  AUDPP_MSG_PCMDMAMISSED_LEN	\
	sizeof(audpp_msg_pcmdmamissed);

typedef struct{
	/*
	** Bit 0	0 = PCM DMA not missed for object 0
	**        1 = PCM DMA missed for object0
	** Bit 1	0 = PCM DMA not missed for object 1
	**        1 = PCM DMA missed for object1
	** Bit 2	0 = PCM DMA not missed for object 2
	**        1 = PCM DMA missed for object2
	** Bit 3	0 = PCM DMA not missed for object 3
	**        1 = PCM DMA missed for object3
	** Bit 4	0 = PCM DMA not missed for object 4
	**        1 = PCM DMA missed for object4
	*/
	unsigned short pcmdmamissed;
} __attribute__((packed)) audpp_msg_pcmdmamissed;

/*
 * MSG to AUDPP enable or disable feedback form DSP to ARM
 */

#define AUDPP_MSG_CFG_MSG	0x0007
#define AUDPP_MSG_CFG_MSG_LEN	\
    sizeof(audpp_msg_cfg_msg)

#define AUDPP_MSG_ENA_ENA	0xFFFF
#define AUDPP_MSG_ENA_DIS	0x0000

typedef struct{
	/*   Enabled  - 0xffff
	**  Disabled - 0
	*/
	unsigned short enabled;
} __attribute__((packed)) audpp_msg_cfg_msg;

/*
 * MSG to communicate the reverb  per object volume
 */

#define AUDPP_MSG_QREVERB_VOLUME	0x0008
#define AUDPP_MSG_QREVERB_VOLUME_LEN	\
	sizeof(audpp_msg_qreverb_volume)


typedef struct {
	unsigned short	obj_0_gain;
	unsigned short	obj_1_gain;
	unsigned short	obj_2_gain;
	unsigned short	obj_3_gain;
	unsigned short	obj_4_gain;
	unsigned short	hpcm_obj_volume;
} __attribute__((packed)) audpp_msg_qreverb_volume;

#define AUDPP_MSG_ROUTING_ACK 0x0009
#define AUDPP_MSG_ROUTING_ACK_LEN \
  sizeof(struct audpp_msg_routing_ack)

struct audpp_msg_routing_ack {
	unsigned short dec_id;
	unsigned short routing_mode;
} __attribute__((packed));

#define AUDPP_MSG_FLUSH_ACK 0x000A

#endif /* QDSP5AUDPPMSG_H */
