/*
 *  intelmid.h - Intel Sound card driver for MID
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Harsha Priya <priya.harsha@intel.com>
 *		Vinod Koul <vinod.koul@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
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
 *  ALSA driver header for Intel MAD chipset
 */
#ifndef __INTELMID_H
#define __INTELMID_H

#include <linux/time.h>

#define DRIVER_NAME_MFLD "msic_audio"
#define DRIVER_NAME_MRST "pmic_audio"
#define DRIVER_NAME "intelmid_audio"
#define PMIC_SOUND_IRQ_TYPE_MASK (1 << 15)
#define AUDINT_BASE (0xFFFFEFF8 + (6 * sizeof(u8)))
#define REG_IRQ
/* values #defined   */
/* will differ for different hw - to be taken from config  */
#define MAX_DEVICES		1
#define MIN_RATE		8000
#define MAX_RATE		48000
#define MAX_BUFFER		(800*1024) /* for PCM */
#define MIN_BUFFER		(800*1024)
#define MAX_PERIODS		(1024*2)
#define MIN_PERIODS		1
#define MAX_PERIOD_BYTES MAX_BUFFER
#define MIN_PERIOD_BYTES 32
/*#define MIN_PERIOD_BYTES 160*/
#define MAX_MUTE		1
#define MIN_MUTE		0
#define MONO_CNTL		1
#define STEREO_CNTL		2
#define MIN_CHANNEL		1
#define MAX_CHANNEL_AMIC	2
#define MAX_CHANNEL_DMIC	4
#define FIFO_SIZE		0 /* fifo not being used */
#define INTEL_MAD		"Intel MAD"
#define MAX_CTRL_MRST		7
#define MAX_CTRL_MFLD		2
#define MAX_CTRL		7
#define MAX_VENDORS		4
/* TODO +6 db */
#define MAX_VOL		64
/* TODO -57 db */
#define MIN_VOL		0
#define PLAYBACK_COUNT  1
#define CAPTURE_COUNT	1

extern int	sst_card_vendor_id;

struct mad_jack {
	struct snd_jack jack;
	int jack_status;
	struct timeval buttonpressed;
	struct timeval  buttonreleased;
};

struct mad_jack_msg_wq {
	u8 intsts;
	struct snd_intelmad *intelmaddata;
	struct work_struct	wq;

};

/**
 * struct snd_intelmad - intelmad driver structure
 *
 * @card: ptr to the card details
 * @card_index: sound card index
 * @card_id: sound card id detected
 * @sstdrv_ops: ptr to sst driver ops
 * @pdev: ptr to platfrom device
 * @irq: interrupt number detected
 * @pmic_status: Device status of sound card
 * @int_base: ptr to MMIO interrupt region
 * @output_sel: device slected as o/p
 * @input_sel: device slected as i/p
 * @master_mute: master mute status
 * @jack: jack status
 * @playback_cnt: active pb streams
 * @capture_cnt: active cp streams
 * @mad_jack_msg: wq struct for jack interrupt processing
 * @mad_jack_wq: wq for jack interrupt processing
 * @jack_prev_state: Previos state of jack detected
 * @cpu_id: current cpu id loaded for
 */
struct snd_intelmad {
	struct snd_card	*card; /* ptr to the card details */
	int		card_index;/*  card index  */
	char		*card_id; /* card id */
	struct intel_sst_card_ops *sstdrv_ops;/* ptr to sst driver ops */
	struct platform_device *pdev;
	int irq;
	int pmic_status;
	void __iomem *int_base;
	int output_sel;
	int input_sel;
	int master_mute;
	struct mad_jack jack[4];
	int playback_cnt;
	int capture_cnt;
	struct mad_jack_msg_wq  mad_jack_msg;
	struct workqueue_struct *mad_jack_wq;
	u8 jack_prev_state;
	unsigned int cpu_id;
};

struct snd_control_val {
	int	playback_vol_max;
	int	playback_vol_min;
	int	capture_vol_max;
	int	capture_vol_min;
};

struct mad_stream_pvt {
	int			stream_status;
	int			stream_ops;
	struct snd_pcm_substream *substream;
	struct pcm_stream_info stream_info;
	ssize_t		dbg_cum_bytes;
	enum snd_sst_device_type device;
};

enum mad_drv_status {
    INIT = 1,
    STARTED,
    RUNNING,
    PAUSED,
    DROPPED,
};

enum mad_pmic_status {
	PMIC_UNINIT = 1,
	PMIC_INIT,
};
enum _widget_ctrl {
	OUTPUT_SEL = 1,
	INPUT_SEL,
	PLAYBACK_VOL,
	PLAYBACK_MUTE,
	CAPTURE_VOL,
	CAPTURE_MUTE,
	MASTER_MUTE
};

void period_elapsed(void *mad_substream);
int snd_intelmad_alloc_stream(struct snd_pcm_substream *substream);
int snd_intelmad_init_stream(struct snd_pcm_substream *substream);

int sst_sc_reg_access(struct sc_reg_access *sc_access,
					int type, int num_val);
#define CPU_CHIP_LINCROFT       1 /* System running lincroft */
#define CPU_CHIP_PENWELL        2 /* System running penwell */

extern struct snd_control_val intelmad_ctrl_val[];
extern struct snd_kcontrol_new snd_intelmad_controls_mrst[];
extern struct snd_kcontrol_new snd_intelmad_controls_mfld[];
extern struct snd_pmic_ops *intelmad_vendor_ops[];

#endif /* __INTELMID_H */
