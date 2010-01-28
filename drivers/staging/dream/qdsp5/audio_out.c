/* arch/arm/mach-msm/qdsp5/audio_out.c
 *
 * pcm audio output device
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
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/wakelock.h>

#include <linux/msm_audio.h>

#include <asm/atomic.h>
#include <asm/ioctls.h>
#include <mach/msm_adsp.h>

#include "audmgr.h"

#include <mach/qdsp5/qdsp5audppcmdi.h>
#include <mach/qdsp5/qdsp5audppmsg.h>

#include "evlog.h"

#define LOG_AUDIO_EVENTS 1
#define LOG_AUDIO_FAULTS 0

enum {
	EV_NULL,
	EV_OPEN,
	EV_WRITE,
	EV_RETURN,
	EV_IOCTL,
	EV_WRITE_WAIT,
	EV_WAIT_EVENT,
	EV_FILL_BUFFER,
	EV_SEND_BUFFER,
	EV_DSP_EVENT,
	EV_ENABLE,
};

#if (LOG_AUDIO_EVENTS != 1)
static inline void LOG(unsigned id, unsigned arg) {}
#else
static const char *pcm_log_strings[] = {
	"NULL",
	"OPEN",
	"WRITE",
	"RETURN",
	"IOCTL",
	"WRITE_WAIT",
	"WAIT_EVENT",
	"FILL_BUFFER",
	"SEND_BUFFER",
	"DSP_EVENT",
	"ENABLE",
};

DECLARE_LOG(pcm_log, 64, pcm_log_strings);

static int __init _pcm_log_init(void)
{
	return ev_log_init(&pcm_log);
}
module_init(_pcm_log_init);

#define LOG(id,arg) ev_log_write(&pcm_log, id, arg)
#endif





#define BUFSZ (960 * 5)
#define DMASZ (BUFSZ * 2)

#define AUDPP_CMD_CFG_OBJ_UPDATE 0x8000
#define AUDPP_CMD_EQ_FLAG_DIS	0x0000
#define AUDPP_CMD_EQ_FLAG_ENA	-1
#define AUDPP_CMD_IIR_FLAG_DIS	  0x0000
#define AUDPP_CMD_IIR_FLAG_ENA	  -1

#define AUDPP_CMD_IIR_TUNING_FILTER  1
#define AUDPP_CMD_EQUALIZER	2
#define AUDPP_CMD_ADRC	3

#define ADRC_ENABLE  0x0001
#define EQ_ENABLE    0x0002
#define IIR_ENABLE   0x0004

struct adrc_filter {
	uint16_t compression_th;
	uint16_t compression_slope;
	uint16_t rms_time;
	uint16_t attack_const_lsw;
	uint16_t attack_const_msw;
	uint16_t release_const_lsw;
	uint16_t release_const_msw;
	uint16_t adrc_system_delay;
};

struct eqalizer {
	uint16_t num_bands;
	uint16_t eq_params[132];
};

struct rx_iir_filter {
	uint16_t num_bands;
	uint16_t iir_params[48];
};

typedef struct {
	audpp_cmd_cfg_object_params_common common;
	uint16_t eq_flag;
	uint16_t num_bands;
	uint16_t eq_params[132];
} audpp_cmd_cfg_object_params_eq;

typedef struct {
	audpp_cmd_cfg_object_params_common common;
	uint16_t active_flag;
	uint16_t num_bands;
	uint16_t iir_params[48];
} audpp_cmd_cfg_object_params_rx_iir;

struct buffer {
	void *data;
	unsigned size;
	unsigned used;
	unsigned addr;
};

struct audio {
	struct buffer out[2];

	spinlock_t dsp_lock;

	uint8_t out_head;
	uint8_t out_tail;
	uint8_t out_needed; /* number of buffers the dsp is waiting for */

	atomic_t out_bytes;

	struct mutex lock;
	struct mutex write_lock;
	wait_queue_head_t wait;

	/* configuration to use on next enable */
	uint32_t out_sample_rate;
	uint32_t out_channel_mode;
	uint32_t out_weight;
	uint32_t out_buffer_size;

	struct audmgr audmgr;

	/* data allocated for various buffers */
	char *data;
	dma_addr_t phys;

	int opened;
	int enabled;
	int running;
	int stopped; /* set when stopped, cleared on flush */
	unsigned volume;

	struct wake_lock wakelock;
	struct wake_lock idlelock;

	int adrc_enable;
	struct adrc_filter adrc;

	int eq_enable;
	struct eqalizer eq;

	int rx_iir_enable;
	struct rx_iir_filter iir;
};

static void audio_prevent_sleep(struct audio *audio)
{
	printk(KERN_INFO "++++++++++++++++++++++++++++++\n");
	wake_lock(&audio->wakelock);
	wake_lock(&audio->idlelock);
}

static void audio_allow_sleep(struct audio *audio)
{
	wake_unlock(&audio->wakelock);
	wake_unlock(&audio->idlelock);
	printk(KERN_INFO "------------------------------\n");
}

static int audio_dsp_out_enable(struct audio *audio, int yes);
static int audio_dsp_send_buffer(struct audio *audio, unsigned id, unsigned len);
static int audio_dsp_set_adrc(struct audio *audio);
static int audio_dsp_set_eq(struct audio *audio);
static int audio_dsp_set_rx_iir(struct audio *audio);

static void audio_dsp_event(void *private, unsigned id, uint16_t *msg);

/* must be called with audio->lock held */
static int audio_enable(struct audio *audio)
{
	struct audmgr_config cfg;
	int rc;

	pr_info("audio_enable()\n");

	if (audio->enabled)
		return 0;

	/* refuse to start if we're not ready */
	if (!audio->out[0].used || !audio->out[1].used)
		return -EIO;

	/* we start buffers 0 and 1, so buffer 0 will be the
	 * next one the dsp will want
	 */
	audio->out_tail = 0;
	audio->out_needed = 0;

	cfg.tx_rate = RPC_AUD_DEF_SAMPLE_RATE_NONE;
	cfg.rx_rate = RPC_AUD_DEF_SAMPLE_RATE_48000;
	cfg.def_method = RPC_AUD_DEF_METHOD_HOST_PCM;
	cfg.codec = RPC_AUD_DEF_CODEC_PCM;
	cfg.snd_method = RPC_SND_METHOD_MIDI;

	audio_prevent_sleep(audio);
	rc = audmgr_enable(&audio->audmgr, &cfg);
	if (rc < 0) {
		audio_allow_sleep(audio);
		return rc;
	}

	if (audpp_enable(-1, audio_dsp_event, audio)) {
		pr_err("audio: audpp_enable() failed\n");
		audmgr_disable(&audio->audmgr);
		audio_allow_sleep(audio);
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
		audio_dsp_out_enable(audio, 0);

		audpp_disable(-1, audio);

		wake_up(&audio->wait);
		audmgr_disable(&audio->audmgr);
		audio->out_needed = 0;
		audio_allow_sleep(audio);
	}
	return 0;
}

/* ------------------- dsp --------------------- */
static void audio_dsp_event(void *private, unsigned id, uint16_t *msg)
{
	struct audio *audio = private;
	struct buffer *frame;
	unsigned long flags;

	LOG(EV_DSP_EVENT, id);
	switch (id) {
	case AUDPP_MSG_HOST_PCM_INTF_MSG: {
		unsigned id = msg[2];
		unsigned idx = msg[3] - 1;

		/* pr_info("audio_dsp_event: HOST_PCM id %d idx %d\n", id, idx); */
		if (id != AUDPP_MSG_HOSTPCM_ID_ARM_RX) {
			pr_err("bogus id\n");
			break;
		}
		if (idx > 1) {
			pr_err("bogus buffer idx\n");
			break;
		}

		spin_lock_irqsave(&audio->dsp_lock, flags);
		if (audio->running) {
			atomic_add(audio->out[idx].used, &audio->out_bytes);
			audio->out[idx].used = 0;

			frame = audio->out + audio->out_tail;
			if (frame->used) {
				audio_dsp_send_buffer(
					audio, audio->out_tail, frame->used);
				audio->out_tail ^= 1;
			} else {
				audio->out_needed++;
			}
			wake_up(&audio->wait);
		}
		spin_unlock_irqrestore(&audio->dsp_lock, flags);
		break;
	}
	case AUDPP_MSG_PCMDMAMISSED:
		pr_info("audio_dsp_event: PCMDMAMISSED %d\n", msg[0]);
		break;
	case AUDPP_MSG_CFG_MSG:
		if (msg[0] == AUDPP_MSG_ENA_ENA) {
			LOG(EV_ENABLE, 1);
			pr_info("audio_dsp_event: CFG_MSG ENABLE\n");
			audio->out_needed = 0;
			audio->running = 1;
			audpp_set_volume_and_pan(5, audio->volume, 0);
			audio_dsp_set_adrc(audio);
			audio_dsp_set_eq(audio);
			audio_dsp_set_rx_iir(audio);
			audio_dsp_out_enable(audio, 1);
		} else if (msg[0] == AUDPP_MSG_ENA_DIS) {
			LOG(EV_ENABLE, 0);
			pr_info("audio_dsp_event: CFG_MSG DISABLE\n");
			audio->running = 0;
		} else {
			pr_err("audio_dsp_event: CFG_MSG %d?\n", msg[0]);
		}
		break;
	default:
		pr_err("audio_dsp_event: UNKNOWN (%d)\n", id);
	}
}

static int audio_dsp_out_enable(struct audio *audio, int yes)
{
	audpp_cmd_pcm_intf cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd_id	= AUDPP_CMD_PCM_INTF_2;
	cmd.object_num	= AUDPP_CMD_PCM_INTF_OBJECT_NUM;
	cmd.config	= AUDPP_CMD_PCM_INTF_CONFIG_CMD_V;
	cmd.intf_type	= AUDPP_CMD_PCM_INTF_RX_ENA_ARMTODSP_V;

	if (yes) {
		cmd.write_buf1LSW	= audio->out[0].addr;
		cmd.write_buf1MSW	= audio->out[0].addr >> 16;
		cmd.write_buf1_len	= audio->out[0].size;
		cmd.write_buf2LSW	= audio->out[1].addr;
		cmd.write_buf2MSW	= audio->out[1].addr >> 16;
		cmd.write_buf2_len	= audio->out[1].size;
		cmd.arm_to_rx_flag	= AUDPP_CMD_PCM_INTF_ENA_V;
		cmd.weight_decoder_to_rx = audio->out_weight;
		cmd.weight_arm_to_rx	= 1;
		cmd.partition_number_arm_to_dsp = 0;
		cmd.sample_rate		= audio->out_sample_rate;
		cmd.channel_mode	= audio->out_channel_mode;
	}

	return audpp_send_queue2(&cmd, sizeof(cmd));
}

static int audio_dsp_send_buffer(struct audio *audio, unsigned idx, unsigned len)
{
	audpp_cmd_pcm_intf_send_buffer cmd;

	cmd.cmd_id		= AUDPP_CMD_PCM_INTF_2;
	cmd.host_pcm_object	= AUDPP_CMD_PCM_INTF_OBJECT_NUM;
	cmd.config		= AUDPP_CMD_PCM_INTF_BUFFER_CMD_V;
	cmd.intf_type		= AUDPP_CMD_PCM_INTF_RX_ENA_ARMTODSP_V;
	cmd.dsp_to_arm_buf_id	= 0;
	cmd.arm_to_dsp_buf_id	= idx + 1;
	cmd.arm_to_dsp_buf_len	= len;

	LOG(EV_SEND_BUFFER, idx);
	return audpp_send_queue2(&cmd, sizeof(cmd));
}

static int audio_dsp_set_adrc(struct audio *audio)
{
	audpp_cmd_cfg_object_params_adrc cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.common.comman_cfg = AUDPP_CMD_CFG_OBJ_UPDATE;
	cmd.common.command_type = AUDPP_CMD_ADRC;

	if (audio->adrc_enable) {
		cmd.adrc_flag = AUDPP_CMD_ADRC_FLAG_ENA;
		cmd.compression_th = audio->adrc.compression_th;
		cmd.compression_slope = audio->adrc.compression_slope;
		cmd.rms_time = audio->adrc.rms_time;
		cmd.attack_const_lsw = audio->adrc.attack_const_lsw;
		cmd.attack_const_msw = audio->adrc.attack_const_msw;
		cmd.release_const_lsw = audio->adrc.release_const_lsw;
		cmd.release_const_msw = audio->adrc.release_const_msw;
		cmd.adrc_system_delay = audio->adrc.adrc_system_delay;
	} else {
		cmd.adrc_flag = AUDPP_CMD_ADRC_FLAG_DIS;
	}
	return audpp_send_queue3(&cmd, sizeof(cmd));
}

static int audio_dsp_set_eq(struct audio *audio)
{
	audpp_cmd_cfg_object_params_eq cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.common.comman_cfg = AUDPP_CMD_CFG_OBJ_UPDATE;
	cmd.common.command_type = AUDPP_CMD_EQUALIZER;

	if (audio->eq_enable) {
		cmd.eq_flag = AUDPP_CMD_EQ_FLAG_ENA;
		cmd.num_bands = audio->eq.num_bands;
		memcpy(&cmd.eq_params, audio->eq.eq_params,
		       sizeof(audio->eq.eq_params));
	} else {
		cmd.eq_flag = AUDPP_CMD_EQ_FLAG_DIS;
	}
	return audpp_send_queue3(&cmd, sizeof(cmd));
}

static int audio_dsp_set_rx_iir(struct audio *audio)
{
	audpp_cmd_cfg_object_params_rx_iir cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.common.comman_cfg = AUDPP_CMD_CFG_OBJ_UPDATE;
	cmd.common.command_type = AUDPP_CMD_IIR_TUNING_FILTER;

	if (audio->rx_iir_enable) {
		cmd.active_flag = AUDPP_CMD_IIR_FLAG_ENA;
		cmd.num_bands = audio->iir.num_bands;
		memcpy(&cmd.iir_params, audio->iir.iir_params,
		       sizeof(audio->iir.iir_params));
	} else {
		cmd.active_flag = AUDPP_CMD_IIR_FLAG_DIS;
	}

	return audpp_send_queue3(&cmd, sizeof(cmd));
}

/* ------------------- device --------------------- */

static int audio_enable_adrc(struct audio *audio, int enable)
{
	if (audio->adrc_enable != enable) {
		audio->adrc_enable = enable;
		if (audio->running)
			audio_dsp_set_adrc(audio);
	}
	return 0;
}

static int audio_enable_eq(struct audio *audio, int enable)
{
	if (audio->eq_enable != enable) {
		audio->eq_enable = enable;
		if (audio->running)
			audio_dsp_set_eq(audio);
	}
	return 0;
}

static int audio_enable_rx_iir(struct audio *audio, int enable)
{
	if (audio->rx_iir_enable != enable) {
		audio->rx_iir_enable = enable;
		if (audio->running)
			audio_dsp_set_rx_iir(audio);
	}
	return 0;
}

static void audio_flush(struct audio *audio)
{
	audio->out[0].used = 0;
	audio->out[1].used = 0;
	audio->out_head = 0;
	audio->out_tail = 0;
	audio->stopped = 0;
}

static long audio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct audio *audio = file->private_data;
	int rc;

	if (cmd == AUDIO_GET_STATS) {
		struct msm_audio_stats stats;
		stats.byte_count = atomic_read(&audio->out_bytes);
		if (copy_to_user((void*) arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}
	if (cmd == AUDIO_SET_VOLUME) {
		unsigned long flags;
		spin_lock_irqsave(&audio->dsp_lock, flags);
		audio->volume = arg;
		if (audio->running)
			audpp_set_volume_and_pan(6, arg, 0);
		spin_unlock_irqrestore(&audio->dsp_lock, flags);
	}

	LOG(EV_IOCTL, cmd);
	mutex_lock(&audio->lock);
	switch (cmd) {
	case AUDIO_START:
		rc = audio_enable(audio);
		break;
	case AUDIO_STOP:
		rc = audio_disable(audio);
		audio->stopped = 1;
		break;
	case AUDIO_FLUSH:
		if (audio->stopped) {
			/* Make sure we're stopped and we wake any threads
			 * that might be blocked holding the write_lock.
			 * While audio->stopped write threads will always
			 * exit immediately.
			 */
			wake_up(&audio->wait);
			mutex_lock(&audio->write_lock);
			audio_flush(audio);
			mutex_unlock(&audio->write_lock);
		}
	case AUDIO_SET_CONFIG: {
		struct msm_audio_config config;
		if (copy_from_user(&config, (void*) arg, sizeof(config))) {
			rc = -EFAULT;
			break;
		}
		if (config.channel_count == 1) {
			config.channel_count = AUDPP_CMD_PCM_INTF_MONO_V;
		} else if (config.channel_count == 2) {
			config.channel_count= AUDPP_CMD_PCM_INTF_STEREO_V;
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
		config.buffer_size = BUFSZ;
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
		if (copy_to_user((void*) arg, &config, sizeof(config))) {
			rc = -EFAULT;
		} else {
			rc = 0;
		}
		break;
	}
	default:
		rc = -EINVAL;
	}
	mutex_unlock(&audio->lock);
	return rc;
}

static ssize_t audio_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	return -EINVAL;
}

static inline int rt_policy(int policy)
{
	if (unlikely(policy == SCHED_FIFO) || unlikely(policy == SCHED_RR))
		return 1;
	return 0;
}

static inline int task_has_rt_policy(struct task_struct *p)
{
	return rt_policy(p->policy);
}

static ssize_t audio_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	struct sched_param s = { .sched_priority = 1 };
	struct audio *audio = file->private_data;
	unsigned long flags;
	const char __user *start = buf;
	struct buffer *frame;
	size_t xfer;
	int old_prio = current->rt_priority;
	int old_policy = current->policy;
	int cap_nice = cap_raised(current_cap(), CAP_SYS_NICE);
	int rc = 0;

	LOG(EV_WRITE, count | (audio->running << 28) | (audio->stopped << 24));

	/* just for this write, set us real-time */
	if (!task_has_rt_policy(current)) {
		struct cred *new = prepare_creds();
		cap_raise(new->cap_effective, CAP_SYS_NICE);
		commit_creds(new);
		sched_setscheduler(current, SCHED_RR, &s);
	}

	mutex_lock(&audio->write_lock);
	while (count > 0) {
		frame = audio->out + audio->out_head;

		LOG(EV_WAIT_EVENT, 0);
		rc = wait_event_interruptible(audio->wait,
					      (frame->used == 0) || (audio->stopped));
		LOG(EV_WAIT_EVENT, 1);

		if (rc < 0)
			break;
		if (audio->stopped) {
			rc = -EBUSY;
			break;
		}
		xfer = count > frame->size ? frame->size : count;
		if (copy_from_user(frame->data, buf, xfer)) {
			rc = -EFAULT;
			break;
		}
		frame->used = xfer;
		audio->out_head ^= 1;
		count -= xfer;
		buf += xfer;

		spin_lock_irqsave(&audio->dsp_lock, flags);
		LOG(EV_FILL_BUFFER, audio->out_head ^ 1);
		frame = audio->out + audio->out_tail;
		if (frame->used && audio->out_needed) {
			audio_dsp_send_buffer(audio, audio->out_tail, frame->used);
			audio->out_tail ^= 1;
			audio->out_needed--;
		}
		spin_unlock_irqrestore(&audio->dsp_lock, flags);
	}

	mutex_unlock(&audio->write_lock);

	/* restore scheduling policy and priority */
	if (!rt_policy(old_policy)) {
		struct sched_param v = { .sched_priority = old_prio };
		sched_setscheduler(current, old_policy, &v);
		if (likely(!cap_nice)) {
			struct cred *new = prepare_creds();
			cap_lower(new->cap_effective, CAP_SYS_NICE);
			commit_creds(new);
			sched_setscheduler(current, SCHED_RR, &s);
		}
	}

	LOG(EV_RETURN,(buf > start) ? (buf - start) : rc);
	if (buf > start)
		return buf - start;
	return rc;
}

static int audio_release(struct inode *inode, struct file *file)
{
	struct audio *audio = file->private_data;

	LOG(EV_OPEN, 0);
	mutex_lock(&audio->lock);
	audio_disable(audio);
	audio_flush(audio);
	audio->opened = 0;
	mutex_unlock(&audio->lock);
	return 0;
}

static struct audio the_audio;

static int audio_open(struct inode *inode, struct file *file)
{
	struct audio *audio = &the_audio;
	int rc;

	mutex_lock(&audio->lock);

	if (audio->opened) {
		pr_err("audio: busy\n");
		rc = -EBUSY;
		goto done;
	}

	if (!audio->data) {
		audio->data = dma_alloc_coherent(NULL, DMASZ,
						 &audio->phys, GFP_KERNEL);
		if (!audio->data) {
			pr_err("audio: could not allocate DMA buffers\n");
			rc = -ENOMEM;
			goto done;
		}
	}

	rc = audmgr_open(&audio->audmgr);
	if (rc)
		goto done;

	audio->out_buffer_size = BUFSZ;
	audio->out_sample_rate = 44100;
	audio->out_channel_mode = AUDPP_CMD_PCM_INTF_STEREO_V;
	audio->out_weight = 100;

	audio->out[0].data = audio->data + 0;
	audio->out[0].addr = audio->phys + 0;
	audio->out[0].size = BUFSZ;

	audio->out[1].data = audio->data + BUFSZ;
	audio->out[1].addr = audio->phys + BUFSZ;
	audio->out[1].size = BUFSZ;

	audio->volume = 0x2000;

	audio_flush(audio);

	file->private_data = audio;
	audio->opened = 1;
	rc = 0;
	LOG(EV_OPEN, 1);
done:
	mutex_unlock(&audio->lock);
	return rc;
}

static long audpp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct audio *audio = file->private_data;
	int rc = 0, enable;
	uint16_t enable_mask;

	mutex_lock(&audio->lock);
	switch (cmd) {
	case AUDIO_ENABLE_AUDPP:
		if (copy_from_user(&enable_mask, (void *) arg, sizeof(enable_mask)))
			goto out_fault;

		enable = (enable_mask & ADRC_ENABLE)? 1 : 0;
		audio_enable_adrc(audio, enable);
		enable = (enable_mask & EQ_ENABLE)? 1 : 0;
		audio_enable_eq(audio, enable);
		enable = (enable_mask & IIR_ENABLE)? 1 : 0;
		audio_enable_rx_iir(audio, enable);
		break;

	case AUDIO_SET_ADRC:
		if (copy_from_user(&audio->adrc, (void*) arg, sizeof(audio->adrc)))
			goto out_fault;
		break;

	case AUDIO_SET_EQ:
		if (copy_from_user(&audio->eq, (void*) arg, sizeof(audio->eq)))
			goto out_fault;
		break;

	case AUDIO_SET_RX_IIR:
		if (copy_from_user(&audio->iir, (void*) arg, sizeof(audio->iir)))
			goto out_fault;
		break;

	default:
		rc = -EINVAL;
	}

	goto out;

 out_fault:
	rc = -EFAULT;
 out:
	mutex_unlock(&audio->lock);
	return rc;
}

static int audpp_open(struct inode *inode, struct file *file)
{
	struct audio *audio = &the_audio;

	file->private_data = audio;
	return 0;
}

static struct file_operations audio_fops = {
	.owner		= THIS_MODULE,
	.open		= audio_open,
	.release	= audio_release,
	.read		= audio_read,
	.write		= audio_write,
	.unlocked_ioctl	= audio_ioctl,
};

static struct file_operations audpp_fops = {
	.owner		= THIS_MODULE,
	.open		= audpp_open,
	.unlocked_ioctl	= audpp_ioctl,
};

struct miscdevice audio_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_pcm_out",
	.fops	= &audio_fops,
};

struct miscdevice audpp_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_pcm_ctl",
	.fops	= &audpp_fops,
};

static int __init audio_init(void)
{
	mutex_init(&the_audio.lock);
	mutex_init(&the_audio.write_lock);
	spin_lock_init(&the_audio.dsp_lock);
	init_waitqueue_head(&the_audio.wait);
	wake_lock_init(&the_audio.wakelock, WAKE_LOCK_SUSPEND, "audio_pcm");
	wake_lock_init(&the_audio.idlelock, WAKE_LOCK_IDLE, "audio_pcm_idle");
	return (misc_register(&audio_misc) || misc_register(&audpp_misc));
}

device_initcall(audio_init);
