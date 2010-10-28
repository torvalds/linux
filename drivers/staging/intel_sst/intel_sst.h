#ifndef __INTEL_SST_H__
#define __INTEL_SST_H__
/*
 *  intel_sst.h - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This driver exposes the audio engine functionalities to the ALSA
 *	and middleware.
 *  This file is shared between the SST and MAD drivers
 */

#define SST_CARD_NAMES "intel_mid_card"

/* control list Pmic & Lpe */
/* Input controls */
enum port_status {
	ACTIVATE = 1,
	DEACTIVATE,
};

/* Card states */
enum sst_card_states {
	SND_CARD_UN_INIT = 0,
	SND_CARD_INIT_DONE,
};

enum sst_controls {
	SST_SND_ALLOC =			0x1000,
	SST_SND_PAUSE =			0x1001,
	SST_SND_RESUME =		0x1002,
	SST_SND_DROP =			0x1003,
	SST_SND_FREE =			0x1004,
	SST_SND_BUFFER_POINTER =	0x1005,
	SST_SND_STREAM_INIT =		0x1006,
	SST_SND_START	 =		0x1007,
	SST_SND_STREAM_PROCESS =	0x1008,
	SST_MAX_CONTROLS =		0x1008,
	SST_CONTROL_BASE =		0x1000,
	SST_ENABLE_RX_TIME_SLOT =	0x1009,
};

enum SND_CARDS {
	SND_FS = 0,
	SND_MX,
	SND_NC,
	SND_MSIC
};

struct pcm_stream_info {
	int str_id;
	void *mad_substream;
	void (*period_elapsed) (void *mad_substream);
	unsigned long long buffer_ptr;
	int sfreq;
};

struct snd_pmic_ops {
	int card_status;
	int master_mute;
	int num_channel;
	int input_dev_id;
	int mute_status;
	int pb_on;
	int cap_on;
	int output_dev_id;
	int (*set_input_dev) (u8 value);
	int (*set_output_dev) (u8 value);

	int (*set_mute) (int dev_id, u8 value);
	int (*get_mute) (int dev_id, u8 *value);

	int (*set_vol) (int dev_id, int value);
	int (*get_vol) (int dev_id, int *value);

	int (*init_card) (void);
	int (*set_pcm_audio_params)
		(int sfreq, int word_size , int num_channel);
	int (*set_pcm_voice_params) (void);
	int (*set_voice_port) (int status);
	int (*set_audio_port) (int status);

	int (*power_up_pmic_pb) (unsigned int port);
	int (*power_up_pmic_cp) (unsigned int port);
	int (*power_down_pmic_pb) (void);
	int (*power_down_pmic_cp) (void);
	int (*power_down_pmic) (void);
};

struct intel_sst_card_ops {
	char *module_name;
	unsigned int  vendor_id;
	int (*control_set) (int control_element, void *value);
	struct snd_pmic_ops *scard_ops;
};

/* modified for generic access */
struct sc_reg_access {
	u16 reg_addr;
	u8 value;
	u8 mask;
};
enum sc_reg_access_type {
	PMIC_READ = 0,
	PMIC_WRITE,
	PMIC_READ_MODIFY,
};

int register_sst_card(struct intel_sst_card_ops *card);
void unregister_sst_card(struct intel_sst_card_ops *card);
#endif /* __INTEL_SST_H__ */
