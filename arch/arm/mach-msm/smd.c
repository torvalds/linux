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
#include <linux/io.h>

#include <mach/msm_smd.h>
#include <mach/msm_iomap.h>
#include <mach/system.h>

#include "smd_private.h"
#include "proc_comm.h"

void (*msm_hw_reset_hook)(void);

#define MODULE_NAME "msm_smd"

enum {
	MSM_SMD_DEBUG = 1U << 0,
	MSM_SMSM_DEBUG = 1U << 0,
};

static int msm_smd_debug_mask;

struct shared_info
{
	int ready;
	unsigned state;
};

static unsigned dummy_state[SMSM_STATE_COUNT];

static struct shared_info smd_info = {
	.state = (unsigned) &dummy_state,
};

module_param_named(debug_mask, msm_smd_debug_mask,
		   int, S_IRUGO | S_IWUSR | S_IWGRP);

void *smem_find(unsigned id, unsigned size);
static void *smem_item(unsigned id, unsigned *size);
static void smd_diag(void);

static unsigned last_heap_free = 0xffffffff;

#define MSM_A2M_INT(n) (MSM_CSR_BASE + 0x400 + (n) * 4)

static inline void notify_other_smsm(void)
{
	writel(1, MSM_A2M_INT(5));
}

static inline void notify_modem_smd(void)
{
	writel(1, MSM_A2M_INT(0));
}

static inline void notify_dsp_smd(void)
{
	writel(1, MSM_A2M_INT(8));
}

static void smd_diag(void)
{
	char *x;

	x = smem_find(ID_DIAG_ERR_MSG, SZ_DIAG_ERR_MSG);
	if (x != 0) {
		x[SZ_DIAG_ERR_MSG - 1] = 0;
		pr_info("smem: DIAG '%s'\n", x);
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

extern int (*msm_check_for_modem_crash)(void);

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

#define SMD_SS_CLOSED		0x00000000
#define SMD_SS_OPENING		0x00000001
#define SMD_SS_OPENED		0x00000002
#define SMD_SS_FLUSHING		0x00000003
#define SMD_SS_CLOSING		0x00000004
#define SMD_SS_RESET		0x00000005
#define SMD_SS_RESET_OPENING	0x00000006

#define SMD_BUF_SIZE		8192
#define SMD_CHANNELS		64

#define SMD_HEADER_SIZE		20


/* the spinlock is used to synchronize between the
** irq handler and code that mutates the channel
** list or fiddles with channel state
*/
static DEFINE_SPINLOCK(smd_lock);
static DEFINE_SPINLOCK(smem_lock);

/* the mutex is used during open() and close()
** operations to avoid races while creating or
** destroying smd_channel structures
*/
static DEFINE_MUTEX(smd_creation_mutex);

static int smd_initialized;

struct smd_alloc_elm {
	char name[20];
	uint32_t cid;
	uint32_t ctype;
	uint32_t ref_count;
};

struct smd_half_channel {
	unsigned state;
	unsigned char fDSR;
	unsigned char fCTS;
	unsigned char fCD;
	unsigned char fRI;
	unsigned char fHEAD;
	unsigned char fTAIL;
	unsigned char fSTATE;
	unsigned char fUNUSED;
	unsigned tail;
	unsigned head;
} __attribute__((packed));

struct smd_shared_v1 {
	struct smd_half_channel ch0;
	unsigned char data0[SMD_BUF_SIZE];
	struct smd_half_channel ch1;
	unsigned char data1[SMD_BUF_SIZE];
};

struct smd_shared_v2 {
	struct smd_half_channel ch0;
	struct smd_half_channel ch1;
};	

struct smd_channel {
	volatile struct smd_half_channel *send;
	volatile struct smd_half_channel *recv;
	unsigned char *send_data;
	unsigned char *recv_data;

	unsigned fifo_mask;
	unsigned fifo_size;
	unsigned current_packet;
	unsigned n;

	struct list_head ch_list;

	void *priv;
	void (*notify)(void *priv, unsigned flags);

	int (*read)(smd_channel_t *ch, void *data, int len);
	int (*write)(smd_channel_t *ch, const void *data, int len);
	int (*read_avail)(smd_channel_t *ch);
	int (*write_avail)(smd_channel_t *ch);

	void (*update_state)(smd_channel_t *ch);
	unsigned last_state;
	void (*notify_other_cpu)(void);
	unsigned type;

	char name[32];
	struct platform_device pdev;
};

static LIST_HEAD(smd_ch_closed_list);
static LIST_HEAD(smd_ch_list); /* todo: per-target lists */

static unsigned char smd_ch_allocated[64];
static struct work_struct probe_work;

#define SMD_TYPE_MASK		0x0FF
#define SMD_TYPE_APPS_MODEM	0x000
#define SMD_TYPE_APPS_DSP	0x001
#define SMD_TYPE_MODEM_DSP	0x002

#define SMD_KIND_MASK		0xF00
#define SMD_KIND_UNKNOWN	0x000
#define SMD_KIND_STREAM		0x100
#define SMD_KIND_PACKET		0x200

static void smd_alloc_channel(const char *name, uint32_t cid, uint32_t type);

static void smd_channel_probe_worker(struct work_struct *work)
{
	struct smd_alloc_elm *shared;
	unsigned type;
	unsigned n;

	shared = smem_find(ID_CH_ALLOC_TBL, sizeof(*shared) * 64);
	if (!shared) {
		pr_err("smd: cannot find allocation table\n");
		return;
	}
	for (n = 0; n < 64; n++) {
		if (smd_ch_allocated[n])
			continue;
		if (!shared[n].ref_count)
			continue;
		if (!shared[n].name[0])
			continue;
		type = shared[n].ctype & SMD_TYPE_MASK;
		if ((type == SMD_TYPE_APPS_MODEM) ||
		    (type == SMD_TYPE_APPS_DSP))
			smd_alloc_channel(shared[n].name,
					  shared[n].cid,
					  shared[n].ctype);
		smd_ch_allocated[n] = 1;
	}
}

static char *chstate(unsigned n)
{
	switch (n) {
	case SMD_SS_CLOSED:
		return "CLOSED";
	case SMD_SS_OPENING:
		return "OPENING";
	case SMD_SS_OPENED:
		return "OPENED";
	case SMD_SS_FLUSHING:
		return "FLUSHING";
	case SMD_SS_CLOSING:
		return "CLOSING";
	case SMD_SS_RESET:
		return "RESET";
	case SMD_SS_RESET_OPENING:
		return "ROPENING";
	default:
		return "UNKNOWN";
	}
}

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
	ch->recv->fTAIL = 1;
}

/* basic read interface to ch_read_{buffer,done} used
** by smd_*_read() and update_packet_state()
** will read-and-discard if the _data pointer is null
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

	pr_info("SMD: ch %d %s -> %s\n", ch->n,
		chstate(last), chstate(next));

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

static irqreturn_t smd_irq_handler(int irq, void *data)
{
	handle_smd_irq(&smd_ch_list, notify_modem_smd);
	return IRQ_HANDLED;
}

static void smd_fake_irq_handler(unsigned long arg)
{
	smd_irq_handler(0, NULL);
}

static DECLARE_TASKLET(smd_fake_irq_tasklet, smd_fake_irq_handler, 0);

void smd_sleep_exit(void)
{
	unsigned long flags;
	struct smd_channel *ch;
	unsigned tmp;
	int need_int = 0;

	spin_lock_irqsave(&smd_lock, flags);
	list_for_each_entry(ch, &smd_ch_list, ch_list) {
		if (ch_is_open(ch)) {
			if (ch->recv->fHEAD) {
				if (msm_smd_debug_mask & MSM_SMD_DEBUG)
					pr_info("smd_sleep_exit ch %d fHEAD "
						"%x %x %x\n",
						ch->n, ch->recv->fHEAD,
						ch->recv->head, ch->recv->tail);
				need_int = 1;
				break;
			}
			if (ch->recv->fTAIL) {
				if (msm_smd_debug_mask & MSM_SMD_DEBUG)
					pr_info("smd_sleep_exit ch %d fTAIL "
						"%x %x %x\n",
						ch->n, ch->recv->fTAIL,
						ch->send->head, ch->send->tail);
				need_int = 1;
				break;
			}
			if (ch->recv->fSTATE) {
				if (msm_smd_debug_mask & MSM_SMD_DEBUG)
					pr_info("smd_sleep_exit ch %d fSTATE %x"
						"\n", ch->n, ch->recv->fSTATE);
				need_int = 1;
				break;
			}
			tmp = ch->recv->state;
			if (tmp != ch->last_state) {
				if (msm_smd_debug_mask & MSM_SMD_DEBUG)
					pr_info("smd_sleep_exit ch %d "
						"state %x != %x\n",
						ch->n, tmp, ch->last_state);
				need_int = 1;
				break;
			}
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

static int smd_alloc_v2(struct smd_channel *ch)
{
	struct smd_shared_v2 *shared2;
	void *buffer;
	unsigned buffer_sz;

	shared2 = smem_alloc(SMEM_SMD_BASE_ID + ch->n, sizeof(*shared2));
	buffer = smem_item(SMEM_SMD_FIFO_BASE_ID + ch->n, &buffer_sz);

	if (!buffer)
		return -1;

	/* buffer must be a power-of-two size */
	if (buffer_sz & (buffer_sz - 1))
		return -1;

	buffer_sz /= 2;
	ch->send = &shared2->ch0;
	ch->recv = &shared2->ch1;
	ch->send_data = buffer;
	ch->recv_data = buffer + buffer_sz;
	ch->fifo_size = buffer_sz;
	return 0;
}

static int smd_alloc_v1(struct smd_channel *ch)
{
	struct smd_shared_v1 *shared1;
	shared1 = smem_alloc(ID_SMD_CHANNELS + ch->n, sizeof(*shared1));
	if (!shared1) {
		pr_err("smd_alloc_channel() cid %d does not exist\n", ch->n);
		return -1;
	}
	ch->send = &shared1->ch0;
	ch->recv = &shared1->ch1;
	ch->send_data = shared1->data0;
	ch->recv_data = shared1->data1;
	ch->fifo_size = SMD_BUF_SIZE;
	return 0;
}


static void smd_alloc_channel(const char *name, uint32_t cid, uint32_t type)
{
	struct smd_channel *ch;

	ch = kzalloc(sizeof(struct smd_channel), GFP_KERNEL);
	if (ch == 0) {
		pr_err("smd_alloc_channel() out of memory\n");
		return;
	}
	ch->n = cid;

	if (smd_alloc_v2(ch) && smd_alloc_v1(ch)) {
		kfree(ch);
		return;
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

	pr_info("smd_alloc_channel() cid=%02d size=%05d '%s'\n",
		ch->n, ch->fifo_size, ch->name);

	mutex_lock(&smd_creation_mutex);
	list_add(&ch->ch_list, &smd_ch_closed_list);
	mutex_unlock(&smd_creation_mutex);

	platform_device_register(&ch->pdev);
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
	list_add(&ch->ch_list, &smd_ch_list);

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

	pr_info("smd_close(%p)\n", ch);

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

static void *smem_item(unsigned id, unsigned *size)
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
	if (modm & SMSM_RESET) {
		handle_modem_crash();
	}
	do_smd_probe();

	spin_unlock_irqrestore(&smem_lock, flags);
	return IRQ_HANDLED;
}

int smsm_change_state(enum smsm_state_item item,
		      uint32_t clear_mask, uint32_t set_mask)
{
	unsigned long flags;
	unsigned state;
	unsigned addr = smd_info.state + item * 4;

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
	struct msm_dem_slave_data *ptr = smem_alloc(SMEM_APPS_DEM_SLAVE_DATA,
						    sizeof(*ptr));
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

	ptr = smem_alloc(SMEM_SMSM_SLEEP_DELAY, sizeof(*ptr));
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

#define MAX_NUM_SLEEP_CLIENTS		64
#define MAX_SLEEP_NAME_LEN		8

#define NUM_GPIO_INT_REGISTERS		6
#define GPIO_SMEM_NUM_GROUPS		2
#define GPIO_SMEM_MAX_PC_INTERRUPTS	8

struct tramp_gpio_save {
	unsigned int enable;
	unsigned int detect;
	unsigned int polarity;
};

struct tramp_gpio_smem {
	uint16_t num_fired[GPIO_SMEM_NUM_GROUPS];
	uint16_t fired[GPIO_SMEM_NUM_GROUPS][GPIO_SMEM_MAX_PC_INTERRUPTS];
	uint32_t enabled[NUM_GPIO_INT_REGISTERS];
	uint32_t detection[NUM_GPIO_INT_REGISTERS];
	uint32_t polarity[NUM_GPIO_INT_REGISTERS];
};


void smsm_print_sleep_info(void)
{
	unsigned long flags;
	uint32_t *ptr;
	struct tramp_gpio_smem *gpio;
	struct smsm_interrupt_info *int_info;


	spin_lock_irqsave(&smem_lock, flags);

	ptr = smem_alloc(SMEM_SMSM_SLEEP_DELAY, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SMSM_SLEEP_DELAY: %x\n", *ptr);

	ptr = smem_alloc(SMEM_SMSM_LIMIT_SLEEP, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SMSM_LIMIT_SLEEP: %x\n", *ptr);

	ptr = smem_alloc(SMEM_SLEEP_POWER_COLLAPSE_DISABLED, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SLEEP_POWER_COLLAPSE_DISABLED: %x\n", *ptr);

#ifndef CONFIG_ARCH_MSM_SCORPION
	int_info = smem_alloc(SMEM_SMSM_INT_INFO, sizeof(*int_info));
	if (int_info)
		pr_info("SMEM_SMSM_INT_INFO %x %x %x\n",
			int_info->interrupt_mask,
			int_info->pending_interrupts,
			int_info->wakeup_reason);

	gpio = smem_alloc(SMEM_GPIO_INT, sizeof(*gpio));
	if (gpio) {
		int i;
		for (i = 0; i < NUM_GPIO_INT_REGISTERS; i++)
			pr_info("SMEM_GPIO_INT: %d: e %x d %x p %x\n",
				i, gpio->enabled[i], gpio->detection[i],
				gpio->polarity[i]);

		for (i = 0; i < GPIO_SMEM_NUM_GROUPS; i++)
			pr_info("SMEM_GPIO_INT: %d: f %d: %d %d...\n",
				i, gpio->num_fired[i], gpio->fired[i][0],
				gpio->fired[i][1]);
	}
#else
#endif
	spin_unlock_irqrestore(&smem_lock, flags);
}

int smd_core_init(void)
{
	int r;
	pr_info("smd_core_init()\n");

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

	r = request_irq(INT_A9_M2A_0, smd_irq_handler,
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

	/* check for any SMD channels that may already exist */
	do_smd_probe();

	/* indicate that we're up and running */
	smsm_change_state(SMSM_STATE_APPS,
			  ~0, SMSM_INIT | SMSM_SMDINIT | SMSM_RPCINIT | SMSM_RUN);
#ifdef CONFIG_ARCH_MSM_SCORPION
	smsm_change_state(SMSM_STATE_APPS_DEM, ~0, 0);
#endif

	pr_info("smd_core_init() done\n");

	return 0;
}

#if defined(CONFIG_DEBUG_FS)

static int dump_ch(char *buf, int max, struct smd_channel *ch)
{
	volatile struct smd_half_channel *s = ch->send;
	volatile struct smd_half_channel *r = ch->recv;

	return scnprintf(
		buf, max,
		"ch%02d:"
		" %8s(%05d/%05d) %c%c%c%c%c%c%c <->"
		" %8s(%05d/%05d) %c%c%c%c%c%c%c\n", ch->n,
		chstate(s->state), s->tail, s->head,
		s->fDSR ? 'D' : 'd',
		s->fCTS ? 'C' : 'c',
		s->fCD ? 'C' : 'c',
		s->fRI ? 'I' : 'i',
		s->fHEAD ? 'W' : 'w',
		s->fTAIL ? 'R' : 'r',
		s->fSTATE ? 'S' : 's',
		chstate(r->state), r->tail, r->head,
		r->fDSR ? 'D' : 'd',
		r->fCTS ? 'R' : 'r',
		r->fCD ? 'C' : 'c',
		r->fRI ? 'I' : 'i',
		r->fHEAD ? 'W' : 'w',
		r->fTAIL ? 'R' : 'r',
		r->fSTATE ? 'S' : 's'
		);
}

static int debug_read_stat(char *buf, int max)
{
	char *msg;
	int i = 0;

	msg = smem_find(ID_DIAG_ERR_MSG, SZ_DIAG_ERR_MSG);

	if (raw_smsm_get_state(SMSM_STATE_MODEM) & SMSM_RESET)
		i += scnprintf(buf + i, max - i,
			       "smsm: ARM9 HAS CRASHED\n");

	i += scnprintf(buf + i, max - i, "smsm: a9: %08x a11: %08x\n",
		       raw_smsm_get_state(SMSM_STATE_MODEM),
		       raw_smsm_get_state(SMSM_STATE_APPS));
#ifdef CONFIG_ARCH_MSM_SCORPION
	i += scnprintf(buf + i, max - i, "smsm dem: apps: %08x modem: %08x "
		       "qdsp6: %08x power: %08x time: %08x\n",
		       raw_smsm_get_state(SMSM_STATE_APPS_DEM),
		       raw_smsm_get_state(SMSM_STATE_MODEM_DEM),
		       raw_smsm_get_state(SMSM_STATE_QDSP6_DEM),
		       raw_smsm_get_state(SMSM_STATE_POWER_MASTER_DEM),
		       raw_smsm_get_state(SMSM_STATE_TIME_MASTER_DEM));
#endif
	if (msg) {
		msg[SZ_DIAG_ERR_MSG - 1] = 0;
		i += scnprintf(buf + i, max - i, "diag: '%s'\n", msg);
	}
	return i;
}

static int debug_read_mem(char *buf, int max)
{
	unsigned n;
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	struct smem_heap_entry *toc = shared->heap_toc;
	int i = 0;

	i += scnprintf(buf + i, max - i,
		       "heap: init=%d free=%d remain=%d\n",
		       shared->heap_info.initialized,
		       shared->heap_info.free_offset,
		       shared->heap_info.heap_remaining);

	for (n = 0; n < SMEM_NUM_ITEMS; n++) {
		if (toc[n].allocated == 0)
			continue;
		i += scnprintf(buf + i, max - i,
			       "%04d: offset %08x size %08x\n",
			       n, toc[n].offset, toc[n].size);
	}
	return i;
}

static int debug_read_ch(char *buf, int max)
{
	struct smd_channel *ch;
	unsigned long flags;
	int i = 0;

	spin_lock_irqsave(&smd_lock, flags);
	list_for_each_entry(ch, &smd_ch_list, ch_list)
		i += dump_ch(buf + i, max - i, ch);
	list_for_each_entry(ch, &smd_ch_closed_list, ch_list)
		i += dump_ch(buf + i, max - i, ch);
	spin_unlock_irqrestore(&smd_lock, flags);

	return i;
}

static int debug_read_version(char *buf, int max)
{
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	unsigned version = shared->version[VERSION_MODEM];
	return sprintf(buf, "%d.%d\n", version >> 16, version & 0xffff);
}

static int debug_read_build_id(char *buf, int max)
{
	unsigned size;
	void *data;

	data = smem_item(SMEM_HW_SW_BUILD_ID, &size);
	if (!data)
		return 0;

	if (size >= max)
		size = max;
	memcpy(buf, data, size);

	return size;
}

static int debug_read_alloc_tbl(char *buf, int max)
{
	struct smd_alloc_elm *shared;
	int n, i = 0;

	shared = smem_find(ID_CH_ALLOC_TBL, sizeof(*shared) * 64);

	for (n = 0; n < 64; n++) {
		if (shared[n].ref_count == 0)
			continue;
		i += scnprintf(buf + i, max - i,
			       "%03d: %-20s cid=%02d type=%03d "
			       "kind=%02d ref_count=%d\n",
			       n, shared[n].name, shared[n].cid,
			       shared[n].ctype & 0xff,
			       (shared[n].ctype >> 8) & 0xf,
			       shared[n].ref_count);
	}

	return i;
}

static int debug_boom(char *buf, int max)
{
	unsigned ms = 5000;
	msm_proc_comm(PCOM_RESET_MODEM, &ms, 0);
	return 0;
}

#define DEBUG_BUFMAX 4096
static char debug_buffer[DEBUG_BUFMAX];

static ssize_t debug_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	int (*fill)(char *buf, int max) = file->private_data;
	int bsize = fill(debug_buffer, DEBUG_BUFMAX);
	return simple_read_from_buffer(buf, count, ppos, debug_buffer, bsize);
}

static int debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations debug_ops = {
	.read = debug_read,
	.open = debug_open,
};

static void debug_create(const char *name, mode_t mode,
			 struct dentry *dent,
			 int (*fill)(char *buf, int max))
{
	debugfs_create_file(name, mode, dent, fill, &debug_ops);
}

static void smd_debugfs_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("smd", 0);
	if (IS_ERR(dent))
		return;

	debug_create("ch", 0444, dent, debug_read_ch);
	debug_create("stat", 0444, dent, debug_read_stat);
	debug_create("mem", 0444, dent, debug_read_mem);
	debug_create("version", 0444, dent, debug_read_version);
	debug_create("tbl", 0444, dent, debug_read_alloc_tbl);
	debug_create("build", 0444, dent, debug_read_build_id);
	debug_create("boom", 0444, dent, debug_boom);
}
#else
static void smd_debugfs_init(void) {}
#endif

static int __init msm_smd_probe(struct platform_device *pdev)
{
	pr_info("smd_init()\n");

	INIT_WORK(&probe_work, smd_channel_probe_worker);

	if (smd_core_init()) {
		pr_err("smd_core_init() failed\n");
		return -1;
	}

	do_smd_probe();

	msm_check_for_modem_crash = check_for_modem_crash;

	smd_debugfs_init();
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
