/* arch/arm/mach-msm/smd.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

#include <mach/msm_smd.h>
#include <mach/system.h>

#include "smd_private.h"
#include "proc_comm.h"

#if defined(CONFIG_ARCH_QSD8X50)
#define CONFIG_QDSP6 1
#endif

void (*msm_hw_reset_hook)(void);

#define MODULE_NAME "msm_smd"

enum {
	MSM_SMD_DEBUG = 1U << 0,
	MSM_SMSM_DEBUG = 1U << 0,
};

static int msm_smd_debug_mask;

struct shared_info {
	int ready;
	unsigned state;
};

static unsigned dummy_state[SMSM_STATE_COUNT];

static struct shared_info smd_info = {
	.state = (unsigned) &dummy_state,
};

module_param_named(debug_mask, msm_smd_debug_mask,
		   int, S_IRUGO | S_IWUSR | S_IWGRP);

static unsigned last_heap_free = 0xffffffff;

static inline void notify_other_smsm(void)
{
	msm_a2m_int(5);
#ifdef CONFIG_QDSP6
	msm_a2m_int(8);
#endif
}

static inline void notify_modem_smd(void)
{
	msm_a2m_int(0);
}

static inline void notify_dsp_smd(void)
{
	msm_a2m_int(8);
}

static void smd_diag(void)
{
	char *x;

	x = smem_find(ID_DIAG_ERR_MSG, SZ_DIAG_ERR_MSG);
	if (x != 0) {
		x[SZ_DIAG_ERR_MSG - 1] = 0;
		pr_debug("DIAG '%s'\n", x);
	}
}

/* call when SMSM_RESET flag is set in the A9's smsm_state */
static void handle_modem_crash(void)
{
	pr_err("ARM9 has CRASHED\n");
	smd_diag();

	/* hard reboot if possible */
	if (msm_hw_reset_hook)
		msm_hw_reset_hook();

	/* in this case the modem or watchdog should reboot us */
	for (;;)
		;
}

uint32_t raw_smsm_get_state(enum smsm_state_item item)
{
	return readl(smd_info.state + item * 4);
}

static int check_for_modem_crash(void)
{
	if (raw_smsm_get_state(SMSM_STATE_MODEM) & SMSM_RESET) {
		handle_modem_crash();
		return -1;
	}
	return 0;
}

/* the spinlock is used to synchronize between the
 * irq handler and code that mutates the channel
 * list or fiddles with channel state
 */
DEFINE_SPINLOCK(smd_lock);
DEFINE_SPINLOCK(smem_lock);

/* the mutex is used during open() and close()
 * operations to avoid races while creating or
 * destroying smd_channel structures
 */
static DEFINE_MUTEX(smd_creation_mutex);

static int smd_initialized;

LIST_HEAD(smd_ch_closed_list);
LIST_HEAD(smd_ch_list_modem);
LIST_HEAD(smd_ch_list_dsp);

static unsigned char smd_ch_allocated[64];
static struct work_struct probe_work;

/* how many bytes are available for reading */
static int smd_stream_read_avail(struct smd_channel *ch)
{
	return (ch->recv->head - ch->recv->tail) & ch->fifo_mask;
}

/* how many bytes we are free to write */
static int smd_stream_write_avail(struct smd_channel *ch)
{
	return ch->fifo_mask -
		((ch->send->head - ch->send->tail) & ch->fifo_mask);
}

static int smd_packet_read_avail(struct smd_channel *ch)
{
	if (ch->current_packet) {
		int n = smd_stream_read_avail(ch);
		if (n > ch->current_packet)
			n = ch->current_packet;
		return n;
	} else {
		return 0;
	}
}

static int smd_packet_write_avail(struct smd_channel *ch)
{
	int n = smd_stream_write_avail(ch);
	return n > SMD_HEADER_SIZE ? n - SMD_HEADER_SIZE : 0;
}

static int ch_is_open(struct smd_channel *ch)
{
	return (ch->recv->state == SMD_SS_OPENED) &&
		(ch->send->state == SMD_SS_OPENED);
}

/* provide a pointer and length to readable data in the fifo */
static unsigned ch_read_buffer(struct smd_channel *ch, void **ptr)
{
	unsigned head = ch->recv->head;
	unsigned tail = ch->recv->tail;
	*ptr = (void *) (ch->recv_data + tail);

	if (tail <= head)
		return head - tail;
	else
		return ch->fifo_size - tail;
}

/* advance the fifo read pointer after data from ch_read_buffer is consumed */
static void ch_read_done(struct smd_channel *ch, unsigned count)
{
	BUG_ON(count > smd_stream_read_avail(ch));
	ch->recv->tail = (ch->recv->tail + count) & ch->fifo_mask;
	ch->send->fTAIL = 1;
}

/* basic read interface to ch_read_{buffer,done} used
 * by smd_*_read() and update_packet_state()
 * will read-and-discard if the _data pointer is null
 */
static int ch_read(struct smd_channel *ch, void *_data, int len)
{
	void *ptr;
	unsigned n;
	unsigned char *data = _data;
	int orig_len = len;

	while (len > 0) {
		n = ch_read_buffer(ch, &ptr);
		if (n == 0)
			break;

		if (n > len)
			n = len;
		if (_data)
			memcpy(data, ptr, n);

		data += n;
		len -= n;
		ch_read_done(ch, n);
	}

	return orig_len - len;
}

static void update_stream_state(struct smd_channel *ch)
{
	/* streams have no special state requiring updating */
}

static void update_packet_state(struct smd_channel *ch)
{
	unsigned hdr[5];
	int r;

	/* can't do anything if we're in the middle of a packet */
	if (ch->current_packet != 0)
		return;

	/* don't bother unless we can get the full header */
	if (smd_stream_read_avail(ch) < SMD_HEADER_SIZE)
		return;

	r = ch_read(ch, hdr, SMD_HEADER_SIZE);
	BUG_ON(r != SMD_HEADER_SIZE);

	ch->current_packet = hdr[0];
}

/* provide a pointer and length to next free space in the fifo */
static unsigned ch_write_buffer(struct smd_channel *ch, void **ptr)
{
	unsigned head = ch->send->head;
	unsigned tail = ch->send->tail;
	*ptr = (void *) (ch->send_data + head);

	if (head < tail) {
		return tail - head - 1;
	} else {
		if (tail == 0)
			return ch->fifo_size - head - 1;
		else
			return ch->fifo_size - head;
	}
}

/* advace the fifo write pointer after freespace
 * from ch_write_buffer is filled
 */
static void ch_write_done(struct smd_channel *ch, unsigned count)
{
	BUG_ON(count > smd_stream_write_avail(ch));
	ch->send->head = (ch->send->head + count) & ch->fifo_mask;
	ch->send->fHEAD = 1;
}

static void ch_set_state(struct smd_channel *ch, unsigned n)
{
	if (n == SMD_SS_OPENED) {
		ch->send->fDSR = 1;
		ch->send->fCTS = 1;
		ch->send->fCD = 1;
	} else {
		ch->send->fDSR = 0;
		ch->send->fCTS = 0;
		ch->send->fCD = 0;
	}
	ch->send->state = n;
	ch->send->fSTATE = 1;
	ch->notify_other_cpu();
}

static void do_smd_probe(void)
{
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	if (shared->heap_info.free_offset != last_heap_free) {
		last_heap_free = shared->heap_info.free_offset;
		schedule_work(&probe_work);
	}
}

static void smd_state_change(struct smd_channel *ch,
			     unsigned last, unsigned next)
{
	ch->last_state = next;

	pr_debug("ch %d %d -> %d\n", ch->n, last, next);

	switch (next) {
	case SMD_SS_OPENING:
		ch->recv->tail = 0;
	case SMD_SS_OPENED:
		if (ch->send->state != SMD_SS_OPENED)
			ch_set_state(ch, SMD_SS_OPENED);
		ch->notify(ch->priv, SMD_EVENT_OPEN);
		break;
	case SMD_SS_FLUSHING:
	case SMD_SS_RESET:
		/* we should force them to close? */
	default:
		ch->notify(ch->priv, SMD_EVENT_CLOSE);
	}
}

static void handle_smd_irq(struct list_head *list, void (*notify)(void))
{
	unsigned long flags;
	struct smd_channel *ch;
	int do_notify = 0;
	unsigned ch_flags;
	unsigned tmp;

	spin_lock_irqsave(&smd_lock, flags);
	list_for_each_entry(ch, list, ch_list) {
		ch_flags = 0;
		if (ch_is_open(ch)) {
			if (ch->recv->fHEAD) {
				ch->recv->fHEAD = 0;
				ch_flags |= 1;
				do_notify |= 1;
			}
			if (ch->recv->fTAIL) {
				ch->recv->fTAIL = 0;
				ch_flags |= 2;
				do_notify |= 1;
			}
			if (ch->recv->fSTATE) {
				ch->recv->fSTATE = 0;
				ch_flags |= 4;
				do_notify |= 1;
			}
		}
		tmp = ch->recv->state;
		if (tmp != ch->last_state)
			smd_state_change(ch, ch->last_state, tmp);
		if (ch_flags) {
			ch->update_state(ch);
			ch->notify(ch->priv, SMD_EVENT_DATA);
		}
	}
	if (do_notify)
		notify();
	spin_unlock_irqrestore(&smd_lock, flags);
	do_smd_probe();
}

static irqreturn_t smd_modem_irq_handler(int irq, void *data)
{
	handle_smd_irq(&smd_ch_list_modem, notify_modem_smd);
	return IRQ_HANDLED;
}

#if defined(CONFIG_QDSP6)
static irqreturn_t smd_dsp_irq_handler(int irq, void *data)
{
	handle_smd_irq(&smd_ch_list_dsp, notify_dsp_smd);
	return IRQ_HANDLED;
}
#endif

static void smd_fake_irq_handler(unsigned long arg)
{
	handle_smd_irq(&smd_ch_list_modem, notify_modem_smd);
	handle_smd_irq(&smd_ch_list_dsp, notify_dsp_smd);
}

static DECLARE_TASKLET(smd_fake_irq_tasklet, smd_fake_irq_handler, 0);

static inline int smd_need_int(struct smd_channel *ch)
{
	if (ch_is_open(ch)) {
		if (ch->recv->fHEAD || ch->recv->fTAIL || ch->recv->fSTATE)
			return 1;
		if (ch->recv->state != ch->last_state)
			return 1;
	}
	return 0;
}

void smd_sleep_exit(void)
{
	unsigned long flags;
	struct smd_channel *ch;
	int need_int = 0;

	spin_lock_irqsave(&smd_lock, flags);
	list_for_each_entry(ch, &smd_ch_list_modem, ch_list) {
		if (smd_need_int(ch)) {
			need_int = 1;
			break;
		}
	}
	list_for_each_entry(ch, &smd_ch_list_dsp, ch_list) {
		if (smd_need_int(ch)) {
			need_int = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&smd_lock, flags);
	do_smd_probe();

	if (need_int) {
		if (msm_smd_debug_mask & MSM_SMD_DEBUG)
			pr_info("smd_sleep_exit need interrupt\n");
		tasklet_schedule(&smd_fake_irq_tasklet);
	}
}


void smd_kick(smd_channel_t *ch)
{
	unsigned long flags;
	unsigned tmp;

	spin_lock_irqsave(&smd_lock, flags);
	ch->update_state(ch);
	tmp = ch->recv->state;
	if (tmp != ch->last_state) {
		ch->last_state = tmp;
		if (tmp == SMD_SS_OPENED)
			ch->notify(ch->priv, SMD_EVENT_OPEN);
		else
			ch->notify(ch->priv, SMD_EVENT_CLOSE);
	}
	ch->notify(ch->priv, SMD_EVENT_DATA);
	ch->notify_other_cpu();
	spin_unlock_irqrestore(&smd_lock, flags);
}

static int smd_is_packet(int chn, unsigned type)
{
	type &= SMD_KIND_MASK;
	if (type == SMD_KIND_PACKET)
		return 1;
	if (type == SMD_KIND_STREAM)
		return 0;

	/* older AMSS reports SMD_KIND_UNKNOWN always */
	if ((chn > 4) || (chn == 1))
		return 1;
	else
		return 0;
}

static int smd_stream_write(smd_channel_t *ch, const void *_data, int len)
{
	void *ptr;
	const unsigned char *buf = _data;
	unsigned xfer;
	int orig_len = len;

	if (len < 0)
		return -EINVAL;

	while ((xfer = ch_write_buffer(ch, &ptr)) != 0) {
		if (!ch_is_open(ch))
			break;
		if (xfer > len)
			xfer = len;
		memcpy(ptr, buf, xfer);
		ch_write_done(ch, xfer);
		len -= xfer;
		buf += xfer;
		if (len == 0)
			break;
	}

	ch->notify_other_cpu();

	return orig_len - len;
}

static int smd_packet_write(smd_channel_t *ch, const void *_data, int len)
{
	unsigned hdr[5];

	if (len < 0)
		return -EINVAL;

	if (smd_stream_write_avail(ch) < (len + SMD_HEADER_SIZE))
		return -ENOMEM;

	hdr[0] = len;
	hdr[1] = hdr[2] = hdr[3] = hdr[4] = 0;

	smd_stream_write(ch, hdr, sizeof(hdr));
	smd_stream_write(ch, _data, len);

	return len;
}

static int smd_stream_read(smd_channel_t *ch, void *data, int len)
{
	int r;

	if (len < 0)
		return -EINVAL;

	r = ch_read(ch, data, len);
	if (r > 0)
		ch->notify_other_cpu();

	return r;
}

static int smd_packet_read(smd_channel_t *ch, void *data, int len)
{
	unsigned long flags;
	int r;

	if (len < 0)
		return -EINVAL;

	if (len > ch->current_packet)
		len = ch->current_packet;

	r = ch_read(ch, data, len);
	if (r > 0)
		ch->notify_other_cpu();

	spin_lock_irqsave(&smd_lock, flags);
	ch->current_packet -= r;
	update_packet_state(ch);
	spin_unlock_irqrestore(&smd_lock, flags);

	return r;
}

static int smd_alloc_channel(const char *name, uint32_t cid, uint32_t type)
{
	struct smd_channel *ch;

	ch = kzalloc(sizeof(struct smd_channel), GFP_KERNEL);
	if (ch == 0) {
		pr_err("smd_alloc_channel() out of memory\n");
		return -1;
	}
	ch->n = cid;

	if (_smd_alloc_channel(ch)) {
		kfree(ch);
		return -1;
	}

	ch->fifo_mask = ch->fifo_size - 1;
	ch->type = type;

	if ((type & SMD_TYPE_MASK) == SMD_TYPE_APPS_MODEM)
		ch->notify_other_cpu = notify_modem_smd;
	else
		ch->notify_other_cpu = notify_dsp_smd;

	if (smd_is_packet(cid, type)) {
		ch->read = smd_packet_read;
		ch->write = smd_packet_write;
		ch->read_avail = smd_packet_read_avail;
		ch->write_avail = smd_packet_write_avail;
		ch->update_state = update_packet_state;
	} else {
		ch->read = smd_stream_read;
		ch->write = smd_stream_write;
		ch->read_avail = smd_stream_read_avail;
		ch->write_avail = smd_stream_write_avail;
		ch->update_state = update_stream_state;
	}

	if ((type & 0xff) == 0)
		memcpy(ch->name, "SMD_", 4);
	else
		memcpy(ch->name, "DSP_", 4);
	memcpy(ch->name + 4, name, 20);
	ch->name[23] = 0;
	ch->pdev.name = ch->name;
	ch->pdev.id = -1;

	pr_debug("smd_alloc_channel() cid=%02d size=%05d '%s'\n",
		ch->n, ch->fifo_size, ch->name);

	mutex_lock(&smd_creation_mutex);
	list_add(&ch->ch_list, &smd_ch_closed_list);
	mutex_unlock(&smd_creation_mutex);

	platform_device_register(&ch->pdev);
	return 0;
}

static void smd_channel_probe_worker(struct work_struct *work)
{
	struct smd_alloc_elm *shared;
	unsigned ctype;
	unsigned type;
	unsigned n;

	shared = smem_find(ID_CH_ALLOC_TBL, sizeof(*shared) * 64);
	if (!shared) {
		pr_err("cannot find allocation table\n");
		return;
	}
	for (n = 0; n < 64; n++) {
		if (smd_ch_allocated[n])
			continue;
		if (!shared[n].ref_count)
			continue;
		if (!shared[n].name[0])
			continue;
		ctype = shared[n].ctype;
		type = ctype & SMD_TYPE_MASK;

		/* DAL channels are stream but neither the modem,
		 * nor the DSP correctly indicate this.  Fixup manually.
		 */
		if (!memcmp(shared[n].name, "DAL", 3))
			ctype = (ctype & (~SMD_KIND_MASK)) | SMD_KIND_STREAM;

		type = shared[n].ctype & SMD_TYPE_MASK;
		if ((type == SMD_TYPE_APPS_MODEM) ||
		    (type == SMD_TYPE_APPS_DSP))
			if (!smd_alloc_channel(shared[n].name, shared[n].cid, ctype))
				smd_ch_allocated[n] = 1;
	}
}

static void do_nothing_notify(void *priv, unsigned flags)
{
}

struct smd_channel *smd_get_channel(const char *name)
{
	struct smd_channel *ch;

	mutex_lock(&smd_creation_mutex);
	list_for_each_entry(ch, &smd_ch_closed_list, ch_list) {
		if (!strcmp(name, ch->name)) {
			list_del(&ch->ch_list);
			mutex_unlock(&smd_creation_mutex);
			return ch;
		}
	}
	mutex_unlock(&smd_creation_mutex);

	return NULL;
}

int smd_open(const char *name, smd_channel_t **_ch,
	     void *priv, void (*notify)(void *, unsigned))
{
	struct smd_channel *ch;
	unsigned long flags;

	if (smd_initialized == 0) {
		pr_info("smd_open() before smd_init()\n");
		return -ENODEV;
	}

	ch = smd_get_channel(name);
	if (!ch)
		return -ENODEV;

	if (notify == 0)
		notify = do_nothing_notify;

	ch->notify = notify;
	ch->current_packet = 0;
	ch->last_state = SMD_SS_CLOSED;
	ch->priv = priv;

	*_ch = ch;

	spin_lock_irqsave(&smd_lock, flags);

	if ((ch->type & SMD_TYPE_MASK) == SMD_TYPE_APPS_MODEM)
		list_add(&ch->ch_list, &smd_ch_list_modem);
	else
		list_add(&ch->ch_list, &smd_ch_list_dsp);

	/* If the remote side is CLOSING, we need to get it to
	 * move to OPENING (which we'll do by moving from CLOSED to
	 * OPENING) and then get it to move from OPENING to
	 * OPENED (by doing the same state change ourselves).
	 *
	 * Otherwise, it should be OPENING and we can move directly
	 * to OPENED so that it will follow.
	 */
	if (ch->recv->state == SMD_SS_CLOSING) {
		ch->send->head = 0;
		ch_set_state(ch, SMD_SS_OPENING);
	} else {
		ch_set_state(ch, SMD_SS_OPENED);
	}
	spin_unlock_irqrestore(&smd_lock, flags);
	smd_kick(ch);

	return 0;
}

int smd_close(smd_channel_t *ch)
{
	unsigned long flags;

	if (ch == 0)
		return -1;

	spin_lock_irqsave(&smd_lock, flags);
	ch->notify = do_nothing_notify;
	list_del(&ch->ch_list);
	ch_set_state(ch, SMD_SS_CLOSED);
	spin_unlock_irqrestore(&smd_lock, flags);

	mutex_lock(&smd_creation_mutex);
	list_add(&ch->ch_list, &smd_ch_closed_list);
	mutex_unlock(&smd_creation_mutex);

	return 0;
}

int smd_read(smd_channel_t *ch, void *data, int len)
{
	return ch->read(ch, data, len);
}

int smd_write(smd_channel_t *ch, const void *data, int len)
{
	return ch->write(ch, data, len);
}

int smd_write_atomic(smd_channel_t *ch, const void *data, int len)
{
	unsigned long flags;
	int res;
	spin_lock_irqsave(&smd_lock, flags);
	res = ch->write(ch, data, len);
	spin_unlock_irqrestore(&smd_lock, flags);
	return res;
}

int smd_read_avail(smd_channel_t *ch)
{
	return ch->read_avail(ch);
}

int smd_write_avail(smd_channel_t *ch)
{
	return ch->write_avail(ch);
}

int smd_wait_until_readable(smd_channel_t *ch, int bytes)
{
	return -1;
}

int smd_wait_until_writable(smd_channel_t *ch, int bytes)
{
	return -1;
}

int smd_cur_packet_size(smd_channel_t *ch)
{
	return ch->current_packet;
}


/* ------------------------------------------------------------------------- */

void *smem_alloc(unsigned id, unsigned size)
{
	return smem_find(id, size);
}

void *smem_item(unsigned id, unsigned *size)
{
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	struct smem_heap_entry *toc = shared->heap_toc;

	if (id >= SMEM_NUM_ITEMS)
		return 0;

	if (toc[id].allocated) {
		*size = toc[id].size;
		return (void *) (MSM_SHARED_RAM_BASE + toc[id].offset);
	} else {
		*size = 0;
	}

	return 0;
}

void *smem_find(unsigned id, unsigned size_in)
{
	unsigned size;
	void *ptr;

	ptr = smem_item(id, &size);
	if (!ptr)
		return 0;

	size_in = ALIGN(size_in, 8);
	if (size_in != size) {
		pr_err("smem_find(%d, %d): wrong size %d\n",
		       id, size_in, size);
		return 0;
	}

	return ptr;
}

static irqreturn_t smsm_irq_handler(int irq, void *data)
{
	unsigned long flags;
	unsigned apps, modm;

	spin_lock_irqsave(&smem_lock, flags);

	apps = raw_smsm_get_state(SMSM_STATE_APPS);
	modm = raw_smsm_get_state(SMSM_STATE_MODEM);

	if (msm_smd_debug_mask & MSM_SMSM_DEBUG)
		pr_info("<SM %08x %08x>\n", apps, modm);
	if (modm & SMSM_RESET)
		handle_modem_crash();

	do_smd_probe();

	spin_unlock_irqrestore(&smem_lock, flags);
	return IRQ_HANDLED;
}

int smsm_change_state(enum smsm_state_item item,
		      uint32_t clear_mask, uint32_t set_mask)
{
	unsigned long addr = smd_info.state + item * 4;
	unsigned long flags;
	unsigned state;

	if (!smd_info.ready)
		return -EIO;

	spin_lock_irqsave(&smem_lock, flags);

	if (raw_smsm_get_state(SMSM_STATE_MODEM) & SMSM_RESET)
		handle_modem_crash();

	state = (readl(addr) & ~clear_mask) | set_mask;
	writel(state, addr);

	if (msm_smd_debug_mask & MSM_SMSM_DEBUG)
		pr_info("smsm_change_state %d %x\n", item, state);
	notify_other_smsm();

	spin_unlock_irqrestore(&smem_lock, flags);

	return 0;
}

uint32_t smsm_get_state(enum smsm_state_item item)
{
	unsigned long flags;
	uint32_t rv;

	spin_lock_irqsave(&smem_lock, flags);

	rv = readl(smd_info.state + item * 4);

	if (item == SMSM_STATE_MODEM && (rv & SMSM_RESET))
		handle_modem_crash();

	spin_unlock_irqrestore(&smem_lock, flags);

	return rv;
}

#ifdef CONFIG_ARCH_MSM_SCORPION

int smsm_set_sleep_duration(uint32_t delay)
{
	struct msm_dem_slave_data *ptr;

	ptr = smem_find(SMEM_APPS_DEM_SLAVE_DATA, sizeof(*ptr));
	if (ptr == NULL) {
		pr_err("smsm_set_sleep_duration <SM NO APPS_DEM_SLAVE_DATA>\n");
		return -EIO;
	}
	if (msm_smd_debug_mask & MSM_SMSM_DEBUG)
		pr_info("smsm_set_sleep_duration %d -> %d\n",
		       ptr->sleep_time, delay);
	ptr->sleep_time = delay;
	return 0;
}

#else

int smsm_set_sleep_duration(uint32_t delay)
{
	uint32_t *ptr;

	ptr = smem_find(SMEM_SMSM_SLEEP_DELAY, sizeof(*ptr));
	if (ptr == NULL) {
		pr_err("smsm_set_sleep_duration <SM NO SLEEP_DELAY>\n");
		return -EIO;
	}
	if (msm_smd_debug_mask & MSM_SMSM_DEBUG)
		pr_info("smsm_set_sleep_duration %d -> %d\n",
		       *ptr, delay);
	*ptr = delay;
	return 0;
}

#endif

int smd_core_init(void)
{
	int r;

	/* wait for essential items to be initialized */
	for (;;) {
		unsigned size;
		void *state;
		state = smem_item(SMEM_SMSM_SHARED_STATE, &size);
		if (size == SMSM_V1_SIZE || size == SMSM_V2_SIZE) {
			smd_info.state = (unsigned)state;
			break;
		}
	}

	smd_info.ready = 1;

	r = request_irq(INT_A9_M2A_0, smd_modem_irq_handler,
			IRQF_TRIGGER_RISING, "smd_dev", 0);
	if (r < 0)
		return r;
	r = enable_irq_wake(INT_A9_M2A_0);
	if (r < 0)
		pr_err("smd_core_init: enable_irq_wake failed for A9_M2A_0\n");

	r = request_irq(INT_A9_M2A_5, smsm_irq_handler,
			IRQF_TRIGGER_RISING, "smsm_dev", 0);
	if (r < 0) {
		free_irq(INT_A9_M2A_0, 0);
		return r;
	}
	r = enable_irq_wake(INT_A9_M2A_5);
	if (r < 0)
		pr_err("smd_core_init: enable_irq_wake failed for A9_M2A_5\n");

#if defined(CONFIG_QDSP6)
	r = request_irq(INT_ADSP_A11, smd_dsp_irq_handler,
			IRQF_TRIGGER_RISING, "smd_dsp", 0);
	if (r < 0) {
		free_irq(INT_A9_M2A_0, 0);
		free_irq(INT_A9_M2A_5, 0);
		return r;
	}
#endif

	/* check for any SMD channels that may already exist */
	do_smd_probe();

	/* indicate that we're up and running */
	smsm_change_state(SMSM_STATE_APPS,
			  ~0, SMSM_INIT | SMSM_SMDINIT | SMSM_RPCINIT | SMSM_RUN);
#ifdef CONFIG_ARCH_MSM_SCORPION
	smsm_change_state(SMSM_STATE_APPS_DEM, ~0, 0);
#endif

	return 0;
}

static int __devinit msm_smd_probe(struct platform_device *pdev)
{
	/*
	 * If we haven't waited for the ARM9 to boot up till now,
	 * then we need to wait here. Otherwise this should just
	 * return immediately.
	 */
	proc_comm_boot_wait();

	INIT_WORK(&probe_work, smd_channel_probe_worker);

	if (smd_core_init()) {
		pr_err("smd_core_init() failed\n");
		return -1;
	}

	do_smd_probe();

	msm_check_for_modem_crash = check_for_modem_crash;

	msm_init_last_radio_log(THIS_MODULE);

	smd_initialized = 1;

	return 0;
}

static struct platform_driver msm_smd_driver = {
	.probe = msm_smd_probe,
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init msm_smd_init(void)
{
	return platform_driver_register(&msm_smd_driver);
}

module_init(msm_smd_init);

MODULE_DESCRIPTION("MSM Shared Memory Core");
MODULE_AUTHOR("Brian Swetland <swetland@google.com>");
MODULE_LICENSE("GPL");
