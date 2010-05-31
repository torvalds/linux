/* arch/arm/mach-msm/qdsp5/audio_mp3.c
 *
 * mp3 audio output device
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2008 HTC Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>

#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/ioctls.h>
#include <mach/msm_adsp.h>

#include <linux/msm_audio.h>

#include "audmgr.h"

#include <mach/qdsp5/qdsp5audppcmdi.h>
#include <mach/qdsp5/qdsp5audppmsg.h>
#include <mach/qdsp5/qdsp5audplaycmdi.h>
#include <mach/qdsp5/qdsp5audplaymsg.h>

/* for queue ids - should be relative to module number*/
#include "adsp.h"

#ifdef DEBUG
#define dprintk(format, arg...) \
printk(KERN_DEBUG format, ## arg)
#else
#define dprintk(format, arg...) do {} while (0)
#endif

/* Size must be power of 2 */
#define BUFSZ_MAX 32768
#define BUFSZ_MIN 4096
#define DMASZ_MAX (BUFSZ_MAX * 2)
#define DMASZ_MIN (BUFSZ_MIN * 2)

#define AUDPLAY_INVALID_READ_PTR_OFFSET	0xFFFF
#define AUDDEC_DEC_MP3 2

#define PCM_BUFSZ_MIN 4800	/* Hold one stereo MP3 frame */
#define PCM_BUF_MAX_COUNT 5	/* DSP only accepts 5 buffers at most
				   but support 2 buffers currently */
#define ROUTING_MODE_FTRT 1
#define ROUTING_MODE_RT 2
/* Decoder status received from AUDPPTASK */
#define  AUDPP_DEC_STATUS_SLEEP	0
#define	 AUDPP_DEC_STATUS_INIT  1
#define  AUDPP_DEC_STATUS_CFG   2
#define  AUDPP_DEC_STATUS_PLAY  3

struct buffer {
	void *data;
	unsigned size;
	unsigned used;		/* Input usage actual DSP produced PCM size  */
	unsigned addr;
};

struct audio {
	struct buffer out[2];

	spinlock_t dsp_lock;

	uint8_t out_head;
	uint8_t out_tail;
	uint8_t out_needed; /* number of buffers the dsp is waiting for */
	unsigned out_dma_sz;

	atomic_t out_bytes;

	struct mutex lock;
	struct mutex write_lock;
	wait_queue_head_t write_wait;

	/* Host PCM section */
	struct buffer in[PCM_BUF_MAX_COUNT];
	struct mutex read_lock;
	wait_queue_head_t read_wait;	/* Wait queue for read */
	char *read_data;	/* pointer to reader buffer */
	dma_addr_t read_phys;	/* physical address of reader buffer */
	uint8_t read_next;	/* index to input buffers to be read next */
	uint8_t fill_next;	/* index to buffer that DSP should be filling */
	uint8_t pcm_buf_count;	/* number of pcm buffer allocated */
	/* ---- End of Host PCM section */

	struct msm_adsp_module *audplay;

	/* configuration to use on next enable */
	uint32_t out_sample_rate;
	uint32_t out_channel_mode;

	struct audmgr audmgr;

	/* data allocated for various buffers */
	char *data;
	dma_addr_t phys;

	int rflush; /* Read  flush */
	int wflush; /* Write flush */
	int opened;
	int enabled;
	int running;
	int stopped; /* set when stopped, cleared on flush */
	int pcm_feedback;
	int buf_refresh;

	int reserved; /* A byte is being reserved */
	char rsv_byte; /* Handle odd length user data */

	unsigned volume;

	uint16_t dec_id;
	uint32_t read_ptr_offset;
};

static int auddec_dsp_config(struct audio *audio, int enable);
static void audpp_cmd_cfg_adec_params(struct audio *audio);
static void audpp_cmd_cfg_routing_mode(struct audio *audio);
static void audplay_send_data(struct audio *audio, unsigned needed);
static void audplay_config_hostpcm(struct audio *audio);
static void audplay_buffer_refresh(struct audio *audio);
static void audio_dsp_event(void *private, unsigned id, uint16_t *msg);

/* must be called with audio->lock held */
static int audio_enable(struct audio *audio)
{
	struct audmgr_config cfg;
	int rc;

	pr_info("audio_enable()\n");

	if (audio->enabled)
		return 0;

	audio->out_tail = 0;
	audio->out_needed = 0;

	cfg.tx_rate = RPC_AUD_DEF_SAMPLE_RATE_NONE;
	cfg.rx_rate = RPC_AUD_DEF_SAMPLE_RATE_48000;
	cfg.def_method = RPC_AUD_DEF_METHOD_PLAYBACK;
	cfg.codec = RPC_AUD_DEF_CODEC_MP3;
	cfg.snd_method = RPC_SND_METHOD_MIDI;

	rc = audmgr_enable(&audio->audmgr, &cfg);
	if (rc < 0)
		return rc;

	if (msm_adsp_enable(audio->audplay)) {
		pr_err("audio: msm_adsp_enable(audplay) failed\n");
		audmgr_disable(&audio->audmgr);
		return -ENODEV;
	}

	if (audpp_enable(audio->dec_id, audio_dsp_event, audio)) {
		pr_err("audio: audpp_enable() failed\n");
		msm_adsp_disable(audio->audplay);
		audmgr_disable(&audio->audmgr);
		return -ENODEV;
	}

	audio->enabled = 1;
	return 0;
}

/* must be called with audio->lock held */
static int audio_disable(struct audio *audio)
{
	pr_info("audio_disable()\n");
	if (audio->enabled) {
		audio->enabled = 0;
		auddec_dsp_config(audio, 0);
		wake_up(&audio->write_wait);
		wake_up(&audio->read_wait);
		msm_adsp_disable(audio->audplay);
		audpp_disable(audio->dec_id, audio);
		audmgr_disable(&audio->audmgr);
		audio->out_needed = 0;
	}
	return 0;
}

/* ------------------- dsp --------------------- */
static void audio_update_pcm_buf_entry(struct audio *audio, uint32_t *payload)
{
	uint8_t index;
	unsigned long flags;

	if (audio->rflush) {
		audio->buf_refresh = 1;
		return;
	}
	spin_lock_irqsave(&audio->dsp_lock, flags);
	for (index = 0; index < payload[1]; index++) {
		if (audio->in[audio->fill_next].addr ==
		    payload[2 + index * 2]) {
			pr_info("audio_update_pcm_buf_entry: in[%d] ready\n",
				audio->fill_next);
			audio->in[audio->fill_next].used =
			  payload[3 + index * 2];
			if ((++audio->fill_next) == audio->pcm_buf_count)
				audio->fill_next = 0;

		} else {
			pr_err
			    ("audio_update_pcm_buf_entry: expected=%x ret=%x\n"
			     , audio->in[audio->fill_next].addr,
			     payload[1 + index * 2]);
			break;
		}
	}
	if (audio->in[audio->fill_next].used == 0) {
		audplay_buffer_refresh(audio);
	} else {
		pr_info("audio_update_pcm_buf_entry: read cannot keep up\n");
		audio->buf_refresh = 1;
	}
	wake_up(&audio->read_wait);
	spin_unlock_irqrestore(&audio->dsp_lock, flags);

}

static void audplay_dsp_event(void *data, unsigned id, size_t len,
			      void (*getevent) (void *ptr, size_t len))
{
	struct audio *audio = data;
	uint32_t msg[28];
	getevent(msg, sizeof(msg));

	dprintk("audplay_dsp_event: msg_id=%x\n", id);

	switch (id) {
	case AUDPLAY_MSG_DEC_NEEDS_DATA:
		audplay_send_data(audio, 1);
		break;

	case AUDPLAY_MSG_BUFFER_UPDATE:
		audio_update_pcm_buf_entry(audio, msg);
		break;

	default:
		pr_err("unexpected message from decoder \n");
		break;
	}
}

static void audio_dsp_event(void *private, unsigned id, uint16_t *msg)
{
	struct audio *audio = private;

	switch (id) {
	case AUDPP_MSG_STATUS_MSG:{
			unsigned status = msg[1];

			switch (status) {
			case AUDPP_DEC_STATUS_SLEEP:
				pr_info("decoder status: sleep \n");
				break;

			case AUDPP_DEC_STATUS_INIT:
				pr_info("decoder status: init \n");
				audpp_cmd_cfg_routing_mode(audio);
				break;

			case AUDPP_DEC_STATUS_CFG:
				pr_info("decoder status: cfg \n");
				break;
			case AUDPP_DEC_STATUS_PLAY:
				pr_info("decoder status: play \n");
				if (audio->pcm_feedback) {
					audplay_config_hostpcm(audio);
					audplay_buffer_refresh(audio);
				}
				break;
			default:
				pr_err("unknown decoder status \n");
				break;
			}
      break;
		}
	case AUDPP_MSG_CFG_MSG:
		if (msg[0] == AUDPP_MSG_ENA_ENA) {
			pr_info("audio_dsp_event: CFG_MSG ENABLE\n");
			auddec_dsp_config(audio, 1);
			audio->out_needed = 0;
			audio->running = 1;
			audpp_set_volume_and_pan(audio->dec_id, audio->volume,
						 0);
			audpp_avsync(audio->dec_id, 22050);
		} else if (msg[0] == AUDPP_MSG_ENA_DIS) {
			pr_info("audio_dsp_event: CFG_MSG DISABLE\n");
			audpp_avsync(audio->dec_id, 0);
			audio->running = 0;
		} else {
			pr_err("audio_dsp_event: CFG_MSG %d?\n", msg[0]);
		}
		break;
	case AUDPP_MSG_ROUTING_ACK:
		pr_info("audio_dsp_event: ROUTING_ACK mode=%d\n", msg[1]);
		audpp_cmd_cfg_adec_params(audio);
		break;

	case AUDPP_MSG_FLUSH_ACK:
		dprintk("%s: FLUSH_ACK\n", __func__);
		audio->wflush = 0;
		audio->rflush = 0;
		if (audio->pcm_feedback)
			audplay_buffer_refresh(audio);
		break;

	default:
		pr_err("audio_dsp_event: UNKNOWN (%d)\n", id);
	}

}


struct msm_adsp_ops audplay_adsp_ops = {
	.event = audplay_dsp_event,
};


#define audplay_send_queue0(audio, cmd, len) \
	msm_adsp_write(audio->audplay, QDSP_uPAudPlay0BitStreamCtrlQueue, \
		       cmd, len)

static int auddec_dsp_config(struct audio *audio, int enable)
{
	audpp_cmd_cfg_dec_type cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd_id = AUDPP_CMD_CFG_DEC_TYPE;
	if (enable)
		cmd.dec0_cfg = AUDPP_CMD_UPDATDE_CFG_DEC |
			       AUDPP_CMD_ENA_DEC_V |
			       AUDDEC_DEC_MP3;
	else
		cmd.dec0_cfg = AUDPP_CMD_UPDATDE_CFG_DEC |
			       AUDPP_CMD_DIS_DEC_V;

	return audpp_send_queue1(&cmd, sizeof(cmd));
}

static void audpp_cmd_cfg_adec_params(struct audio *audio)
{
	audpp_cmd_cfg_adec_params_mp3 cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.common.cmd_id = AUDPP_CMD_CFG_ADEC_PARAMS;
	cmd.common.length = AUDPP_CMD_CFG_ADEC_PARAMS_MP3_LEN;
	cmd.common.dec_id = audio->dec_id;
	cmd.common.input_sampling_frequency = audio->out_sample_rate;

	audpp_send_queue2(&cmd, sizeof(cmd));
}

static void audpp_cmd_cfg_routing_mode(struct audio *audio)
{
	struct audpp_cmd_routing_mode cmd;
	pr_info("audpp_cmd_cfg_routing_mode()\n");
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd_id = AUDPP_CMD_ROUTING_MODE;
	cmd.object_number = audio->dec_id;
	if (audio->pcm_feedback)
		cmd.routing_mode = ROUTING_MODE_FTRT;
	else
		cmd.routing_mode = ROUTING_MODE_RT;

	audpp_send_queue1(&cmd, sizeof(cmd));
}

static int audplay_dsp_send_data_avail(struct audio *audio,
					unsigned idx, unsigned len)
{
	audplay_cmd_bitstream_data_avail cmd;

	cmd.cmd_id		= AUDPLAY_CMD_BITSTREAM_DATA_AVAIL;
	cmd.decoder_id		= audio->dec_id;
	cmd.buf_ptr		= audio->out[idx].addr;
	cmd.buf_size		= len/2;
	cmd.partition_number	= 0;
	return audplay_send_queue0(audio, &cmd, sizeof(cmd));
}

static void audplay_buffer_refresh(struct audio *audio)
{
	struct audplay_cmd_buffer_refresh refresh_cmd;

	refresh_cmd.cmd_id = AUDPLAY_CMD_BUFFER_REFRESH;
	refresh_cmd.num_buffers = 1;
	refresh_cmd.buf0_address = audio->in[audio->fill_next].addr;
	refresh_cmd.buf0_length = audio->in[audio->fill_next].size -
	  (audio->in[audio->fill_next].size % 576);	/* Mp3 frame size */
	refresh_cmd.buf_read_count = 0;
	pr_info("audplay_buffer_fresh: buf0_addr=%x buf0_len=%d\n",
		refresh_cmd.buf0_address, refresh_cmd.buf0_length);
	(void)audplay_send_queue0(audio, &refresh_cmd, sizeof(refresh_cmd));
}

static void audplay_config_hostpcm(struct audio *audio)
{
	struct audplay_cmd_hpcm_buf_cfg cfg_cmd;

	pr_info("audplay_config_hostpcm()\n");
	cfg_cmd.cmd_id = AUDPLAY_CMD_HPCM_BUF_CFG;
	cfg_cmd.max_buffers = 1;
	cfg_cmd.byte_swap = 0;
	cfg_cmd.hostpcm_config = (0x8000) | (0x4000);
	cfg_cmd.feedback_frequency = 1;
	cfg_cmd.partition_number = 0;
	(void)audplay_send_queue0(audio, &cfg_cmd, sizeof(cfg_cmd));

}

static void audplay_send_data(struct audio *audio, unsigned needed)
{
	struct buffer *frame;
	unsigned long flags;

	spin_lock_irqsave(&audio->dsp_lock, flags);
	if (!audio->running)
		goto done;

	if (audio->wflush) {
		audio->out_needed = 1;
		goto done;
	}

	if (needed && !audio->wflush) {
		/* We were called from the callback because the DSP
		 * requested more data.  Note that the DSP does want
		 * more data, and if a buffer was in-flight, mark it
		 * as available (since the DSP must now be done with
		 * it).
		 */
		audio->out_needed = 1;
		frame = audio->out + audio->out_tail;
		if (frame->used == 0xffffffff) {
		  dprintk("frame %d free\n", audio->out_tail);
		  frame->used = 0;
		  audio->out_tail ^= 1;
		  wake_up(&audio->write_wait);
		}
	}

	if (audio->out_needed) {
		/* If the DSP currently wants data and we have a
		 * buffer available, we will send it and reset
		 * the needed flag.  We'll mark the buffer as in-flight
		 * so that it won't be recycled until the next buffer
		 * is requested
		 */

		frame = audio->out + audio->out_tail;
		if (frame->used) {
		  BUG_ON(frame->used == 0xffffffff);
		  dprintk("frame %d busy\n", audio->out_tail);
		  audplay_dsp_send_data_avail(audio, audio->out_tail,
					      frame->used);
		  frame->used = 0xffffffff;
		  audio->out_needed = 0;
		}
	}
done:
	spin_unlock_irqrestore(&audio->dsp_lock, flags);
}

/* ------------------- device --------------------- */

static void audio_flush(struct audio *audio)
{
	audio->out[0].used = 0;
	audio->out[1].used = 0;
	audio->out_head = 0;
	audio->out_tail = 0;
	audio->reserved = 0;
	atomic_set(&audio->out_bytes, 0);
}

static void audio_flush_pcm_buf(struct audio *audio)
{
	uint8_t index;

	for (index = 0; index < PCM_BUF_MAX_COUNT; index++)
		audio->in[index].used = 0;

	audio->read_next = 0;
	audio->fill_next = 0;
}

static void audio_ioport_reset(struct audio *audio)
{
	/* Make sure read/write thread are free from
	 * sleep and knowing that system is not able
	 * to process io request at the moment
	 */
	wake_up(&audio->write_wait);
	mutex_lock(&audio->write_lock);
	audio_flush(audio);
	mutex_unlock(&audio->write_lock);
	wake_up(&audio->read_wait);
	mutex_lock(&audio->read_lock);
	audio_flush_pcm_buf(audio);
	mutex_unlock(&audio->read_lock);
}

static long audio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct audio *audio = file->private_data;
	int rc = 0;

	pr_info("audio_ioctl() cmd = %d\n", cmd);

	if (cmd == AUDIO_GET_STATS) {
		struct msm_audio_stats stats;
		stats.byte_count = audpp_avsync_byte_count(audio->dec_id);
		stats.sample_count = audpp_avsync_sample_count(audio->dec_id);
		if (copy_to_user((void *) arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}
	if (cmd == AUDIO_SET_VOLUME) {
		unsigned long flags;
		spin_lock_irqsave(&audio->dsp_lock, flags);
		audio->volume = arg;
		if (audio->running)
			audpp_set_volume_and_pan(audio->dec_id, arg, 0);
		spin_unlock_irqrestore(&audio->dsp_lock, flags);
		return 0;
	}
	mutex_lock(&audio->lock);
	switch (cmd) {
	case AUDIO_START:
		rc = audio_enable(audio);
		break;
	case AUDIO_STOP:
		rc = audio_disable(audio);
		audio->stopped = 1;
		audio_ioport_reset(audio);
		audio->stopped = 0;
		break;
	case AUDIO_FLUSH:
		dprintk("%s: AUDIO_FLUSH\n", __func__);
		audio->rflush = 1;
		audio->wflush = 1;
		audio_ioport_reset(audio);
		audio->rflush = 0;
		audio->wflush = 0;

		if (audio->buf_refresh) {
			audio->buf_refresh = 0;
			audplay_buffer_refresh(audio);
		}
		break;

	case AUDIO_SET_CONFIG: {
		struct msm_audio_config config;
		if (copy_from_user(&config, (void *) arg, sizeof(config))) {
			rc = -EFAULT;
			break;
		}
		if (config.channel_count == 1) {
			config.channel_count = AUDPP_CMD_PCM_INTF_MONO_V;
		} else if (config.channel_count == 2) {
			config.channel_count = AUDPP_CMD_PCM_INTF_STEREO_V;
		} else {
			rc = -EINVAL;
			break;
		}
		audio->out_sample_rate = config.sample_rate;
		audio->out_channel_mode = config.channel_count;
		rc = 0;
		break;
	}
	case AUDIO_GET_CONFIG: {
		struct msm_audio_config config;
		config.buffer_size = (audio->out_dma_sz >> 1);
		config.buffer_count = 2;
		config.sample_rate = audio->out_sample_rate;
		if (audio->out_channel_mode == AUDPP_CMD_PCM_INTF_MONO_V) {
			config.channel_count = 1;
		} else {
			config.channel_count = 2;
		}
		config.unused[0] = 0;
		config.unused[1] = 0;
		config.unused[2] = 0;
		config.unused[3] = 0;
		if (copy_to_user((void *) arg, &config, sizeof(config))) {
			rc = -EFAULT;
		} else {
			rc = 0;
		}
		break;
	}
	case AUDIO_GET_PCM_CONFIG:{
		struct msm_audio_pcm_config config;
		config.pcm_feedback = 0;
		config.buffer_count = PCM_BUF_MAX_COUNT;
		config.buffer_size = PCM_BUFSZ_MIN;
		if (copy_to_user((void *)arg, &config,
			 sizeof(config)))
			rc = -EFAULT;
		else
			rc = 0;
		break;
	}
	case AUDIO_SET_PCM_CONFIG:{
			struct msm_audio_pcm_config config;
			if (copy_from_user
			    (&config, (void *)arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}
			if ((config.buffer_count > PCM_BUF_MAX_COUNT) ||
			    (config.buffer_count == 1))
				config.buffer_count = PCM_BUF_MAX_COUNT;

			if (config.buffer_size < PCM_BUFSZ_MIN)
				config.buffer_size = PCM_BUFSZ_MIN;

			/* Check if pcm feedback is required */
			if ((config.pcm_feedback) && (!audio->read_data)) {
				pr_info("ioctl: allocate PCM buffer %d\n",
					config.buffer_count *
					config.buffer_size);
				audio->read_data =
				    dma_alloc_coherent(NULL,
						       config.buffer_size *
						       config.buffer_count,
						       &audio->read_phys,
						       GFP_KERNEL);
				if (!audio->read_data) {
					pr_err("audio_mp3: malloc pcm buf failed\n");
					rc = -1;
				} else {
					uint8_t index;
					uint32_t offset = 0;
					audio->pcm_feedback = 1;
					audio->buf_refresh = 0;
					audio->pcm_buf_count =
					    config.buffer_count;
					audio->read_next = 0;
					audio->fill_next = 0;

					for (index = 0;
					     index < config.buffer_count;
					     index++) {
						audio->in[index].data =
						    audio->read_data + offset;
						audio->in[index].addr =
						    audio->read_phys + offset;
						audio->in[index].size =
						    config.buffer_size;
						audio->in[index].used = 0;
						offset += config.buffer_size;
					}
					rc = 0;
				}
			} else {
				rc = 0;
			}
			break;
		}
	case AUDIO_PAUSE:
		dprintk("%s: AUDIO_PAUSE %ld\n", __func__, arg);
		rc = audpp_pause(audio->dec_id, (int) arg);
		break;
	default:
		rc = -EINVAL;
	}
	mutex_unlock(&audio->lock);
	return rc;
}

static ssize_t audio_read(struct file *file, char __user *buf, size_t count,
			  loff_t *pos)
{
	struct audio *audio = file->private_data;
	const char __user *start = buf;
	int rc = 0;

	if (!audio->pcm_feedback)
		return 0; /* PCM feedback disabled. Nothing to read */

	mutex_lock(&audio->read_lock);
	pr_info("audio_read() %d \n", count);
	while (count > 0) {
		rc = wait_event_interruptible(audio->read_wait,
					      (audio->in[audio->read_next].
					       used > 0) || (audio->stopped)
						   || (audio->rflush));

		if (rc < 0)
			break;

		if (audio->stopped || audio->rflush) {
			rc = -EBUSY;
			break;
		}

		if (count < audio->in[audio->read_next].used) {
			/* Read must happen in frame boundary. Since
			 * driver does not know frame size, read count
			 * must be greater or equal
			 * to size of PCM samples
			 */
			pr_info("audio_read: no partial frame done reading\n");
			break;
		} else {
			pr_info("audio_read: read from in[%d]\n",
				audio->read_next);
			if (copy_to_user
			    (buf, audio->in[audio->read_next].data,
			     audio->in[audio->read_next].used)) {
				pr_err("audio_read: invalid addr %x \n",
				       (unsigned int)buf);
				rc = -EFAULT;
				break;
			}
			count -= audio->in[audio->read_next].used;
			buf += audio->in[audio->read_next].used;
			audio->in[audio->read_next].used = 0;
			if ((++audio->read_next) == audio->pcm_buf_count)
				audio->read_next = 0;
			if (audio->in[audio->read_next].used == 0)
				break; /* No data ready at this moment
					* Exit while loop to prevent
					* output thread sleep too long
					*/
		}
	}

	/* don't feed output buffer to HW decoder during flushing
	 * buffer refresh command will be sent once flush completes
	 * send buf refresh command here can confuse HW decoder
	 */
	if (audio->buf_refresh && !audio->rflush) {
		audio->buf_refresh = 0;
		pr_info("audio_read: kick start pcm feedback again\n");
		audplay_buffer_refresh(audio);
	}

	mutex_unlock(&audio->read_lock);

	if (buf > start)
		rc = buf - start;

	pr_info("audio_read: read %d bytes\n", rc);
	return rc;
}

static ssize_t audio_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	struct audio *audio = file->private_data;
	const char __user *start = buf;
	struct buffer *frame;
	size_t xfer;
	char *cpy_ptr;
	int rc = 0;
	unsigned dsize;

	mutex_lock(&audio->write_lock);
	while (count > 0) {
		frame = audio->out + audio->out_head;
		cpy_ptr = frame->data;
		dsize = 0;
		rc = wait_event_interruptible(audio->write_wait,
					      (frame->used == 0)
					      || (audio->stopped)
						  || (audio->wflush));
		if (rc < 0)
			break;
		if (audio->stopped || audio->wflush) {
			rc = -EBUSY;
			break;
		}

		if (audio->reserved) {
			dprintk("%s: append reserved byte %x\n",
				__func__, audio->rsv_byte);
			*cpy_ptr = audio->rsv_byte;
			xfer = (count > (frame->size - 1)) ?
				frame->size - 1 : count;
			cpy_ptr++;
			dsize = 1;
			audio->reserved = 0;
		} else
			xfer = (count > frame->size) ? frame->size : count;

		if (copy_from_user(cpy_ptr, buf, xfer)) {
			rc = -EFAULT;
			break;
		}

		dsize += xfer;
		if (dsize & 1) {
			audio->rsv_byte = ((char *) frame->data)[dsize - 1];
			dprintk("%s: odd length buf reserve last byte %x\n",
				__func__, audio->rsv_byte);
			audio->reserved = 1;
			dsize--;
		}
		count -= xfer;
		buf += xfer;

		if (dsize > 0) {
			audio->out_head ^= 1;
			frame->used = dsize;
			audplay_send_data(audio, 0);
		}
	}
	mutex_unlock(&audio->write_lock);
	if (buf > start)
		return buf - start;
	return rc;
}

static int audio_release(struct inode *inode, struct file *file)
{
	struct audio *audio = file->private_data;

	dprintk("audio_release()\n");

	mutex_lock(&audio->lock);
	audio_disable(audio);
	audio_flush(audio);
	audio_flush_pcm_buf(audio);
	msm_adsp_put(audio->audplay);
	audio->audplay = NULL;
	audio->opened = 0;
	audio->reserved = 0;
	dma_free_coherent(NULL, audio->out_dma_sz, audio->data, audio->phys);
	audio->data = NULL;
	if (audio->read_data != NULL) {
		dma_free_coherent(NULL,
				  audio->in[0].size * audio->pcm_buf_count,
				  audio->read_data, audio->read_phys);
		audio->read_data = NULL;
	}
	audio->pcm_feedback = 0;
	mutex_unlock(&audio->lock);
	return 0;
}

static struct audio the_mp3_audio;

static int audio_open(struct inode *inode, struct file *file)
{
	struct audio *audio = &the_mp3_audio;
	int rc;
	unsigned pmem_sz;

	mutex_lock(&audio->lock);

	if (audio->opened) {
		pr_err("audio: busy\n");
		rc = -EBUSY;
		goto done;
	}

	pmem_sz = DMASZ_MAX;

	while (pmem_sz >= DMASZ_MIN) {
		audio->data = dma_alloc_coherent(NULL, pmem_sz,
						 &audio->phys, GFP_KERNEL);
		if (audio->data)
			break;
		else if (pmem_sz == DMASZ_MIN) {
			pr_err("audio: could not allocate DMA buffers\n");
			rc = -ENOMEM;
			goto done;
		} else
			pmem_sz >>= 1;
	}

	dprintk("%s: allocated %d bytes DMA buffer\n", __func__, pmem_sz);

	rc = audmgr_open(&audio->audmgr);
	if (rc) {
		dma_free_coherent(NULL, pmem_sz,
		audio->data, audio->phys);
		goto done;
	}

	rc = msm_adsp_get("AUDPLAY0TASK", &audio->audplay, &audplay_adsp_ops,
			  audio);
	if (rc) {
		pr_err("audio: failed to get audplay0 dsp module\n");
		dma_free_coherent(NULL, pmem_sz,
		audio->data, audio->phys);
		audmgr_close(&audio->audmgr);
		goto done;
	}

	audio->out_dma_sz = pmem_sz;
	pmem_sz >>= 1; /* Shift by 1 to get size of ping pong buffer */

	audio->out_sample_rate = 44100;
	audio->out_channel_mode = AUDPP_CMD_PCM_INTF_STEREO_V;
	audio->dec_id = 0;

	audio->out[0].data = audio->data + 0;
	audio->out[0].addr = audio->phys + 0;
	audio->out[0].size = pmem_sz;

	audio->out[1].data = audio->data + pmem_sz;
	audio->out[1].addr = audio->phys + pmem_sz;
	audio->out[1].size = pmem_sz;

	audio->volume = 0x2000;	/* equal to Q13 number 1.0 Unit Gain */

	audio_flush(audio);

	file->private_data = audio;
	audio->opened = 1;
	rc = 0;
done:
	mutex_unlock(&audio->lock);
	return rc;
}

static struct file_operations audio_mp3_fops = {
	.owner		= THIS_MODULE,
	.open		= audio_open,
	.release	= audio_release,
	.read		= audio_read,
	.write		= audio_write,
	.unlocked_ioctl	= audio_ioctl,
};

struct miscdevice audio_mp3_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_mp3",
	.fops	= &audio_mp3_fops,
};

static int __init audio_init(void)
{
	mutex_init(&the_mp3_audio.lock);
	mutex_init(&the_mp3_audio.write_lock);
	mutex_init(&the_mp3_audio.read_lock);
	spin_lock_init(&the_mp3_audio.dsp_lock);
	init_waitqueue_head(&the_mp3_audio.write_wait);
	init_waitqueue_head(&the_mp3_audio.read_wait);
	the_mp3_audio.read_data = NULL;
	return misc_register(&audio_mp3_misc);
}

device_initcall(audio_init);
