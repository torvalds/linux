
/* arch/arm/mach-msm/qdsp5/audpp.c
 *
 * common code to deal with the AUDPP dsp task (audio postproc)
 *
 * Copyright (C) 2008 Google, Inc.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/ioctls.h>
#include <mach/msm_adsp.h>

#include "audmgr.h"

#include <mach/qdsp5/qdsp5audppcmdi.h>
#include <mach/qdsp5/qdsp5audppmsg.h>

/* for queue ids - should be relative to module number*/
#include "adsp.h"

#include "evlog.h"


enum {
	EV_NULL,
	EV_ENABLE,
	EV_DISABLE,
	EV_EVENT,
	EV_DATA,
};

static const char *dsp_log_strings[] = {
	"NULL",
	"ENABLE",
	"DISABLE",
	"EVENT",
	"DATA",
};

DECLARE_LOG(dsp_log, 64, dsp_log_strings);

static int __init _dsp_log_init(void)
{
	return ev_log_init(&dsp_log);
}
module_init(_dsp_log_init);
#define LOG(id,arg) ev_log_write(&dsp_log, id, arg)

static DEFINE_MUTEX(audpp_lock);

#define CH_COUNT 5
#define AUDPP_CLNT_MAX_COUNT 6
#define AUDPP_AVSYNC_INFO_SIZE 7

struct audpp_state {
	struct msm_adsp_module *mod;
	audpp_event_func func[AUDPP_CLNT_MAX_COUNT];
	void *private[AUDPP_CLNT_MAX_COUNT];
	struct mutex *lock;
	unsigned open_count;
	unsigned enabled;

	/* which channels are actually enabled */
	unsigned avsync_mask;

	/* flags, 48 bits sample/bytes counter per channel */
	uint16_t avsync[CH_COUNT * AUDPP_CLNT_MAX_COUNT + 1];
};

struct audpp_state the_audpp_state = {
	.lock = &audpp_lock,
};

int audpp_send_queue1(void *cmd, unsigned len)
{
	return msm_adsp_write(the_audpp_state.mod,
			      QDSP_uPAudPPCmd1Queue, cmd, len);
}
EXPORT_SYMBOL(audpp_send_queue1);

int audpp_send_queue2(void *cmd, unsigned len)
{
	return msm_adsp_write(the_audpp_state.mod,
			      QDSP_uPAudPPCmd2Queue, cmd, len);
}
EXPORT_SYMBOL(audpp_send_queue2);

int audpp_send_queue3(void *cmd, unsigned len)
{
	return msm_adsp_write(the_audpp_state.mod,
			      QDSP_uPAudPPCmd3Queue, cmd, len);
}
EXPORT_SYMBOL(audpp_send_queue3);

static int audpp_dsp_config(int enable)
{
	audpp_cmd_cfg cmd;

	cmd.cmd_id = AUDPP_CMD_CFG;
	cmd.cfg = enable ? AUDPP_CMD_CFG_ENABLE : AUDPP_CMD_CFG_SLEEP;

	return audpp_send_queue1(&cmd, sizeof(cmd));
}

static void audpp_broadcast(struct audpp_state *audpp, unsigned id,
			    uint16_t *msg)
{
	unsigned n;
	for (n = 0; n < AUDPP_CLNT_MAX_COUNT; n++) {
		if (audpp->func[n])
			audpp->func[n] (audpp->private[n], id, msg);
	}
}

static void audpp_notify_clnt(struct audpp_state *audpp, unsigned clnt_id,
			      unsigned id, uint16_t *msg)
{
	if (clnt_id < AUDPP_CLNT_MAX_COUNT && audpp->func[clnt_id])
		audpp->func[clnt_id] (audpp->private[clnt_id], id, msg);
}

static void audpp_dsp_event(void *data, unsigned id, size_t len,
			    void (*getevent)(void *ptr, size_t len))
{
	struct audpp_state *audpp = data;
	uint16_t msg[8];

	if (id == AUDPP_MSG_AVSYNC_MSG) {
		getevent(audpp->avsync, sizeof(audpp->avsync));

		/* mask off any channels we're not watching to avoid
		 * cases where we might get one last update after
		 * disabling avsync and end up in an odd state when
		 * we next read...
		 */
		audpp->avsync[0] &= audpp->avsync_mask;
		return;
	}

	getevent(msg, sizeof(msg));

	LOG(EV_EVENT, (id << 16) | msg[0]);
	LOG(EV_DATA, (msg[1] << 16) | msg[2]);

	switch (id) {
	case AUDPP_MSG_STATUS_MSG:{
			unsigned cid = msg[0];
			pr_info("audpp: status %d %d %d\n", cid, msg[1],
				msg[2]);
			if ((cid < 5) && audpp->func[cid])
				audpp->func[cid] (audpp->private[cid], id, msg);
			break;
		}
	case AUDPP_MSG_HOST_PCM_INTF_MSG:
		if (audpp->func[5])
			audpp->func[5] (audpp->private[5], id, msg);
		break;
	case AUDPP_MSG_PCMDMAMISSED:
		pr_err("audpp: DMA missed obj=%x\n", msg[0]);
		break;
	case AUDPP_MSG_CFG_MSG:
		if (msg[0] == AUDPP_MSG_ENA_ENA) {
			pr_info("audpp: ENABLE\n");
			audpp->enabled = 1;
			audpp_broadcast(audpp, id, msg);
		} else if (msg[0] == AUDPP_MSG_ENA_DIS) {
			pr_info("audpp: DISABLE\n");
			audpp->enabled = 0;
			audpp_broadcast(audpp, id, msg);
		} else {
			pr_err("audpp: invalid config msg %d\n", msg[0]);
		}
		break;
	case AUDPP_MSG_ROUTING_ACK:
		audpp_broadcast(audpp, id, msg);
		break;
	case AUDPP_MSG_FLUSH_ACK:
		audpp_notify_clnt(audpp, msg[0], id, msg);
		break;
	default:
	  pr_info("audpp: unhandled msg id %x\n", id);
	}
}

static struct msm_adsp_ops adsp_ops = {
	.event = audpp_dsp_event,
};

static void audpp_fake_event(struct audpp_state *audpp, int id,
			     unsigned event, unsigned arg)
{
	uint16_t msg[1];
	msg[0] = arg;
	audpp->func[id] (audpp->private[id], event, msg);
}

int audpp_enable(int id, audpp_event_func func, void *private)
{
	struct audpp_state *audpp = &the_audpp_state;
	int res = 0;

	if (id < -1 || id > 4)
		return -EINVAL;

	if (id == -1)
		id = 5;

	mutex_lock(audpp->lock);
	if (audpp->func[id]) {
		res = -EBUSY;
		goto out;
	}

	audpp->func[id] = func;
	audpp->private[id] = private;

	LOG(EV_ENABLE, 1);
	if (audpp->open_count++ == 0) {
		pr_info("audpp: enable\n");
		res = msm_adsp_get("AUDPPTASK", &audpp->mod, &adsp_ops, audpp);
		if (res < 0) {
			pr_err("audpp: cannot open AUDPPTASK\n");
			audpp->open_count = 0;
			audpp->func[id] = NULL;
			audpp->private[id] = NULL;
			goto out;
		}
		LOG(EV_ENABLE, 2);
		msm_adsp_enable(audpp->mod);
		audpp_dsp_config(1);
	} else {
		unsigned long flags;
		local_irq_save(flags);
		if (audpp->enabled)
			audpp_fake_event(audpp, id,
					 AUDPP_MSG_CFG_MSG, AUDPP_MSG_ENA_ENA);
		local_irq_restore(flags);
	}

	res = 0;
out:
	mutex_unlock(audpp->lock);
	return res;
}
EXPORT_SYMBOL(audpp_enable);

void audpp_disable(int id, void *private)
{
	struct audpp_state *audpp = &the_audpp_state;
	unsigned long flags;

	if (id < -1 || id > 4)
		return;

	if (id == -1)
		id = 5;

	mutex_lock(audpp->lock);
	LOG(EV_DISABLE, 1);
	if (!audpp->func[id])
		goto out;
	if (audpp->private[id] != private)
		goto out;

	local_irq_save(flags);
	audpp_fake_event(audpp, id, AUDPP_MSG_CFG_MSG, AUDPP_MSG_ENA_DIS);
	audpp->func[id] = NULL;
	audpp->private[id] = NULL;
	local_irq_restore(flags);

	if (--audpp->open_count == 0) {
		pr_info("audpp: disable\n");
		LOG(EV_DISABLE, 2);
		audpp_dsp_config(0);
		msm_adsp_disable(audpp->mod);
		msm_adsp_put(audpp->mod);
		audpp->mod = NULL;
	}
out:
	mutex_unlock(audpp->lock);
}
EXPORT_SYMBOL(audpp_disable);

#define BAD_ID(id) ((id < 0) || (id >= CH_COUNT))

void audpp_avsync(int id, unsigned rate)
{
	unsigned long flags;
	audpp_cmd_avsync cmd;

	if (BAD_ID(id))
		return;

	local_irq_save(flags);
	if (rate)
		the_audpp_state.avsync_mask |= (1 << id);
	else
		the_audpp_state.avsync_mask &= (~(1 << id));
	the_audpp_state.avsync[0] &= the_audpp_state.avsync_mask;
	local_irq_restore(flags);

	cmd.cmd_id = AUDPP_CMD_AVSYNC;
	cmd.object_number = id;
	cmd.interrupt_interval_lsw = rate;
	cmd.interrupt_interval_msw = rate >> 16;
	audpp_send_queue1(&cmd, sizeof(cmd));
}
EXPORT_SYMBOL(audpp_avsync);

unsigned audpp_avsync_sample_count(int id)
{
	uint16_t *avsync = the_audpp_state.avsync;
	unsigned val;
	unsigned long flags;
	unsigned mask;

	if (BAD_ID(id))
		return 0;

	mask = 1 << id;
	id = id * AUDPP_AVSYNC_INFO_SIZE + 2;
	local_irq_save(flags);
	if (avsync[0] & mask)
		val = (avsync[id] << 16) | avsync[id + 1];
	else
		val = 0;
	local_irq_restore(flags);

	return val;
}
EXPORT_SYMBOL(audpp_avsync_sample_count);

unsigned audpp_avsync_byte_count(int id)
{
	uint16_t *avsync = the_audpp_state.avsync;
	unsigned val;
	unsigned long flags;
	unsigned mask;

	if (BAD_ID(id))
		return 0;

	mask = 1 << id;
	id = id * AUDPP_AVSYNC_INFO_SIZE + 5;
	local_irq_save(flags);
	if (avsync[0] & mask)
		val = (avsync[id] << 16) | avsync[id + 1];
	else
		val = 0;
	local_irq_restore(flags);

	return val;
}
EXPORT_SYMBOL(audpp_avsync_byte_count);

#define AUDPP_CMD_CFG_OBJ_UPDATE 0x8000
#define AUDPP_CMD_VOLUME_PAN 0

int audpp_set_volume_and_pan(unsigned id, unsigned volume, int pan)
{
	/* cmd, obj_cfg[7], cmd_type, volume, pan */
	uint16_t cmd[11];

	if (id > 6)
		return -EINVAL;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = AUDPP_CMD_CFG_OBJECT_PARAMS;
	cmd[1 + id] = AUDPP_CMD_CFG_OBJ_UPDATE;
	cmd[8] = AUDPP_CMD_VOLUME_PAN;
	cmd[9] = volume;
	cmd[10] = pan;

	return audpp_send_queue3(cmd, sizeof(cmd));
}
EXPORT_SYMBOL(audpp_set_volume_and_pan);

int audpp_pause(unsigned id, int pause)
{
	/* pause 1 = pause 0 = resume */
	u16 pause_cmd[AUDPP_CMD_DEC_CTRL_LEN / sizeof(unsigned short)];

	if (id >= CH_COUNT)
		return -EINVAL;

	memset(pause_cmd, 0, sizeof(pause_cmd));

	pause_cmd[0] = AUDPP_CMD_DEC_CTRL;
	if (pause == 1)
		pause_cmd[1 + id] = AUDPP_CMD_UPDATE_V | AUDPP_CMD_PAUSE_V;
	else if (pause == 0)
		pause_cmd[1 + id] = AUDPP_CMD_UPDATE_V | AUDPP_CMD_RESUME_V;
	else
		return -EINVAL;

	return audpp_send_queue1(pause_cmd, sizeof(pause_cmd));
}
EXPORT_SYMBOL(audpp_pause);

int audpp_flush(unsigned id)
{
	u16 flush_cmd[AUDPP_CMD_DEC_CTRL_LEN / sizeof(unsigned short)];

	if (id >= CH_COUNT)
		return -EINVAL;

	memset(flush_cmd, 0, sizeof(flush_cmd));

	flush_cmd[0] = AUDPP_CMD_DEC_CTRL;
	flush_cmd[1 + id] = AUDPP_CMD_UPDATE_V | AUDPP_CMD_FLUSH_V;

	return audpp_send_queue1(flush_cmd, sizeof(flush_cmd));
}
EXPORT_SYMBOL(audpp_flush);
