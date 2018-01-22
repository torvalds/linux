/*
* Filename: cregs.c
*
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <linux/completion.h>
#include <linux/slab.h>

#include "rsxx_priv.h"

#define CREG_TIMEOUT_MSEC	10000

typedef void (*creg_cmd_cb)(struct rsxx_cardinfo *card,
			    struct creg_cmd *cmd,
			    int st);

struct creg_cmd {
	struct list_head list;
	creg_cmd_cb cb;
	void *cb_private;
	unsigned int op;
	unsigned int addr;
	int cnt8;
	void *buf;
	unsigned int stream;
	unsigned int status;
};

static struct kmem_cache *creg_cmd_pool;


/*------------ Private Functions --------------*/

#if defined(__LITTLE_ENDIAN)
#define LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN)
#define LITTLE_ENDIAN 0
#else
#error Unknown endianess!!! Aborting...
#endif

static int copy_to_creg_data(struct rsxx_cardinfo *card,
			      int cnt8,
			      void *buf,
			      unsigned int stream)
{
	int i = 0;
	u32 *data = buf;

	if (unlikely(card->eeh_state))
		return -EIO;

	for (i = 0; cnt8 > 0; i++, cnt8 -= 4) {
		/*
		 * Firmware implementation makes it necessary to byte swap on
		 * little endian processors.
		 */
		if (LITTLE_ENDIAN && stream)
			iowrite32be(data[i], card->regmap + CREG_DATA(i));
		else
			iowrite32(data[i], card->regmap + CREG_DATA(i));
	}

	return 0;
}


static int copy_from_creg_data(struct rsxx_cardinfo *card,
				int cnt8,
				void *buf,
				unsigned int stream)
{
	int i = 0;
	u32 *data = buf;

	if (unlikely(card->eeh_state))
		return -EIO;

	for (i = 0; cnt8 > 0; i++, cnt8 -= 4) {
		/*
		 * Firmware implementation makes it necessary to byte swap on
		 * little endian processors.
		 */
		if (LITTLE_ENDIAN && stream)
			data[i] = ioread32be(card->regmap + CREG_DATA(i));
		else
			data[i] = ioread32(card->regmap + CREG_DATA(i));
	}

	return 0;
}

static void creg_issue_cmd(struct rsxx_cardinfo *card, struct creg_cmd *cmd)
{
	int st;

	if (unlikely(card->eeh_state))
		return;

	iowrite32(cmd->addr, card->regmap + CREG_ADD);
	iowrite32(cmd->cnt8, card->regmap + CREG_CNT);

	if (cmd->op == CREG_OP_WRITE) {
		if (cmd->buf) {
			st = copy_to_creg_data(card, cmd->cnt8,
					       cmd->buf, cmd->stream);
			if (st)
				return;
		}
	}

	if (unlikely(card->eeh_state))
		return;

	/* Setting the valid bit will kick off the command. */
	iowrite32(cmd->op, card->regmap + CREG_CMD);
}

static void creg_kick_queue(struct rsxx_cardinfo *card)
{
	if (card->creg_ctrl.active || list_empty(&card->creg_ctrl.queue))
		return;

	card->creg_ctrl.active = 1;
	card->creg_ctrl.active_cmd = list_first_entry(&card->creg_ctrl.queue,
						      struct creg_cmd, list);
	list_del(&card->creg_ctrl.active_cmd->list);
	card->creg_ctrl.q_depth--;

	/*
	 * We have to set the timer before we push the new command. Otherwise,
	 * we could create a race condition that would occur if the timer
	 * was not canceled, and expired after the new command was pushed,
	 * but before the command was issued to hardware.
	 */
	mod_timer(&card->creg_ctrl.cmd_timer,
				jiffies + msecs_to_jiffies(CREG_TIMEOUT_MSEC));

	creg_issue_cmd(card, card->creg_ctrl.active_cmd);
}

static int creg_queue_cmd(struct rsxx_cardinfo *card,
			  unsigned int op,
			  unsigned int addr,
			  unsigned int cnt8,
			  void *buf,
			  int stream,
			  creg_cmd_cb callback,
			  void *cb_private)
{
	struct creg_cmd *cmd;

	/* Don't queue stuff up if we're halted. */
	if (unlikely(card->halt))
		return -EINVAL;

	if (card->creg_ctrl.reset)
		return -EAGAIN;

	if (cnt8 > MAX_CREG_DATA8)
		return -EINVAL;

	cmd = kmem_cache_alloc(creg_cmd_pool, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	INIT_LIST_HEAD(&cmd->list);

	cmd->op		= op;
	cmd->addr	= addr;
	cmd->cnt8	= cnt8;
	cmd->buf	= buf;
	cmd->stream	= stream;
	cmd->cb		= callback;
	cmd->cb_private = cb_private;
	cmd->status	= 0;

	spin_lock_bh(&card->creg_ctrl.lock);
	list_add_tail(&cmd->list, &card->creg_ctrl.queue);
	card->creg_ctrl.q_depth++;
	creg_kick_queue(card);
	spin_unlock_bh(&card->creg_ctrl.lock);

	return 0;
}

static void creg_cmd_timed_out(struct timer_list *t)
{
	struct rsxx_cardinfo *card = from_timer(card, t, creg_ctrl.cmd_timer);
	struct creg_cmd *cmd;

	spin_lock(&card->creg_ctrl.lock);
	cmd = card->creg_ctrl.active_cmd;
	card->creg_ctrl.active_cmd = NULL;
	spin_unlock(&card->creg_ctrl.lock);

	if (cmd == NULL) {
		card->creg_ctrl.creg_stats.creg_timeout++;
		dev_warn(CARD_TO_DEV(card),
			"No active command associated with timeout!\n");
		return;
	}

	if (cmd->cb)
		cmd->cb(card, cmd, -ETIMEDOUT);

	kmem_cache_free(creg_cmd_pool, cmd);


	spin_lock(&card->creg_ctrl.lock);
	card->creg_ctrl.active = 0;
	creg_kick_queue(card);
	spin_unlock(&card->creg_ctrl.lock);
}


static void creg_cmd_done(struct work_struct *work)
{
	struct rsxx_cardinfo *card;
	struct creg_cmd *cmd;
	int st = 0;

	card = container_of(work, struct rsxx_cardinfo,
			    creg_ctrl.done_work);

	/*
	 * The timer could not be cancelled for some reason,
	 * race to pop the active command.
	 */
	if (del_timer_sync(&card->creg_ctrl.cmd_timer) == 0)
		card->creg_ctrl.creg_stats.failed_cancel_timer++;

	spin_lock_bh(&card->creg_ctrl.lock);
	cmd = card->creg_ctrl.active_cmd;
	card->creg_ctrl.active_cmd = NULL;
	spin_unlock_bh(&card->creg_ctrl.lock);

	if (cmd == NULL) {
		dev_err(CARD_TO_DEV(card),
			"Spurious creg interrupt!\n");
		return;
	}

	card->creg_ctrl.creg_stats.stat = ioread32(card->regmap + CREG_STAT);
	cmd->status = card->creg_ctrl.creg_stats.stat;
	if ((cmd->status & CREG_STAT_STATUS_MASK) == 0) {
		dev_err(CARD_TO_DEV(card),
			"Invalid status on creg command\n");
		/*
		 * At this point we're probably reading garbage from HW. Don't
		 * do anything else that could mess up the system and let
		 * the sync function return an error.
		 */
		st = -EIO;
		goto creg_done;
	} else if (cmd->status & CREG_STAT_ERROR) {
		st = -EIO;
	}

	if ((cmd->op == CREG_OP_READ)) {
		unsigned int cnt8 = ioread32(card->regmap + CREG_CNT);

		/* Paranoid Sanity Checks */
		if (!cmd->buf) {
			dev_err(CARD_TO_DEV(card),
				"Buffer not given for read.\n");
			st = -EIO;
			goto creg_done;
		}
		if (cnt8 != cmd->cnt8) {
			dev_err(CARD_TO_DEV(card),
				"count mismatch\n");
			st = -EIO;
			goto creg_done;
		}

		st = copy_from_creg_data(card, cnt8, cmd->buf, cmd->stream);
	}

creg_done:
	if (cmd->cb)
		cmd->cb(card, cmd, st);

	kmem_cache_free(creg_cmd_pool, cmd);

	spin_lock_bh(&card->creg_ctrl.lock);
	card->creg_ctrl.active = 0;
	creg_kick_queue(card);
	spin_unlock_bh(&card->creg_ctrl.lock);
}

static void creg_reset(struct rsxx_cardinfo *card)
{
	struct creg_cmd *cmd = NULL;
	struct creg_cmd *tmp;
	unsigned long flags;

	/*
	 * mutex_trylock is used here because if reset_lock is taken then a
	 * reset is already happening. So, we can just go ahead and return.
	 */
	if (!mutex_trylock(&card->creg_ctrl.reset_lock))
		return;

	card->creg_ctrl.reset = 1;
	spin_lock_irqsave(&card->irq_lock, flags);
	rsxx_disable_ier_and_isr(card, CR_INTR_CREG | CR_INTR_EVENT);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	dev_warn(CARD_TO_DEV(card),
		"Resetting creg interface for recovery\n");

	/* Cancel outstanding commands */
	spin_lock_bh(&card->creg_ctrl.lock);
	list_for_each_entry_safe(cmd, tmp, &card->creg_ctrl.queue, list) {
		list_del(&cmd->list);
		card->creg_ctrl.q_depth--;
		if (cmd->cb)
			cmd->cb(card, cmd, -ECANCELED);
		kmem_cache_free(creg_cmd_pool, cmd);
	}

	cmd = card->creg_ctrl.active_cmd;
	card->creg_ctrl.active_cmd = NULL;
	if (cmd) {
		if (timer_pending(&card->creg_ctrl.cmd_timer))
			del_timer_sync(&card->creg_ctrl.cmd_timer);

		if (cmd->cb)
			cmd->cb(card, cmd, -ECANCELED);
		kmem_cache_free(creg_cmd_pool, cmd);

		card->creg_ctrl.active = 0;
	}
	spin_unlock_bh(&card->creg_ctrl.lock);

	card->creg_ctrl.reset = 0;
	spin_lock_irqsave(&card->irq_lock, flags);
	rsxx_enable_ier_and_isr(card, CR_INTR_CREG | CR_INTR_EVENT);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	mutex_unlock(&card->creg_ctrl.reset_lock);
}

/* Used for synchronous accesses */
struct creg_completion {
	struct completion	*cmd_done;
	int			st;
	u32			creg_status;
};

static void creg_cmd_done_cb(struct rsxx_cardinfo *card,
			     struct creg_cmd *cmd,
			     int st)
{
	struct creg_completion *cmd_completion;

	cmd_completion = cmd->cb_private;
	BUG_ON(!cmd_completion);

	cmd_completion->st = st;
	cmd_completion->creg_status = cmd->status;
	complete(cmd_completion->cmd_done);
}

static int __issue_creg_rw(struct rsxx_cardinfo *card,
			   unsigned int op,
			   unsigned int addr,
			   unsigned int cnt8,
			   void *buf,
			   int stream,
			   unsigned int *hw_stat)
{
	DECLARE_COMPLETION_ONSTACK(cmd_done);
	struct creg_completion completion;
	unsigned long timeout;
	int st;

	completion.cmd_done = &cmd_done;
	completion.st = 0;
	completion.creg_status = 0;

	st = creg_queue_cmd(card, op, addr, cnt8, buf, stream, creg_cmd_done_cb,
			    &completion);
	if (st)
		return st;

	/*
	 * This timeout is necessary for unresponsive hardware. The additional
	 * 20 seconds to used to guarantee that each cregs requests has time to
	 * complete.
	 */
	timeout = msecs_to_jiffies(CREG_TIMEOUT_MSEC *
				   card->creg_ctrl.q_depth + 20000);

	/*
	 * The creg interface is guaranteed to complete. It has a timeout
	 * mechanism that will kick in if hardware does not respond.
	 */
	st = wait_for_completion_timeout(completion.cmd_done, timeout);
	if (st == 0) {
		/*
		 * This is really bad, because the kernel timer did not
		 * expire and notify us of a timeout!
		 */
		dev_crit(CARD_TO_DEV(card),
			"cregs timer failed\n");
		creg_reset(card);
		return -EIO;
	}

	*hw_stat = completion.creg_status;

	if (completion.st) {
		/*
		* This read is needed to verify that there has not been any
		* extreme errors that might have occurred, i.e. EEH. The
		* function iowrite32 will not detect EEH errors, so it is
		* necessary that we recover if such an error is the reason
		* for the timeout. This is a dummy read.
		*/
		ioread32(card->regmap + SCRATCH);

		dev_warn(CARD_TO_DEV(card),
			"creg command failed(%d x%08x)\n",
			completion.st, addr);
		return completion.st;
	}

	return 0;
}

static int issue_creg_rw(struct rsxx_cardinfo *card,
			 u32 addr,
			 unsigned int size8,
			 void *data,
			 int stream,
			 int read)
{
	unsigned int hw_stat;
	unsigned int xfer;
	unsigned int op;
	int st;

	op = read ? CREG_OP_READ : CREG_OP_WRITE;

	do {
		xfer = min_t(unsigned int, size8, MAX_CREG_DATA8);

		st = __issue_creg_rw(card, op, addr, xfer,
				     data, stream, &hw_stat);
		if (st)
			return st;

		data   = (char *)data + xfer;
		addr  += xfer;
		size8 -= xfer;
	} while (size8);

	return 0;
}

/* ---------------------------- Public API ---------------------------------- */
int rsxx_creg_write(struct rsxx_cardinfo *card,
			u32 addr,
			unsigned int size8,
			void *data,
			int byte_stream)
{
	return issue_creg_rw(card, addr, size8, data, byte_stream, 0);
}

int rsxx_creg_read(struct rsxx_cardinfo *card,
		       u32 addr,
		       unsigned int size8,
		       void *data,
		       int byte_stream)
{
	return issue_creg_rw(card, addr, size8, data, byte_stream, 1);
}

int rsxx_get_card_state(struct rsxx_cardinfo *card, unsigned int *state)
{
	return rsxx_creg_read(card, CREG_ADD_CARD_STATE,
				  sizeof(*state), state, 0);
}

int rsxx_get_card_size8(struct rsxx_cardinfo *card, u64 *size8)
{
	unsigned int size;
	int st;

	st = rsxx_creg_read(card, CREG_ADD_CARD_SIZE,
				sizeof(size), &size, 0);
	if (st)
		return st;

	*size8 = (u64)size * RSXX_HW_BLK_SIZE;
	return 0;
}

int rsxx_get_num_targets(struct rsxx_cardinfo *card,
			     unsigned int *n_targets)
{
	return rsxx_creg_read(card, CREG_ADD_NUM_TARGETS,
				  sizeof(*n_targets), n_targets, 0);
}

int rsxx_get_card_capabilities(struct rsxx_cardinfo *card,
				   u32 *capabilities)
{
	return rsxx_creg_read(card, CREG_ADD_CAPABILITIES,
				  sizeof(*capabilities), capabilities, 0);
}

int rsxx_issue_card_cmd(struct rsxx_cardinfo *card, u32 cmd)
{
	return rsxx_creg_write(card, CREG_ADD_CARD_CMD,
				   sizeof(cmd), &cmd, 0);
}


/*----------------- HW Log Functions -------------------*/
static void hw_log_msg(struct rsxx_cardinfo *card, const char *str, int len)
{
	static char level;

	/*
	 * New messages start with "<#>", where # is the log level. Messages
	 * that extend past the log buffer will use the previous level
	 */
	if ((len > 3) && (str[0] == '<') && (str[2] == '>')) {
		level = str[1];
		str += 3; /* Skip past the log level. */
		len -= 3;
	}

	switch (level) {
	case '0':
		dev_emerg(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '1':
		dev_alert(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '2':
		dev_crit(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '3':
		dev_err(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '4':
		dev_warn(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '5':
		dev_notice(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '6':
		dev_info(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	case '7':
		dev_dbg(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	default:
		dev_info(CARD_TO_DEV(card), "HW: %.*s", len, str);
		break;
	}
}

/*
 * The substrncpy function copies the src string (which includes the
 * terminating '\0' character), up to the count into the dest pointer.
 * Returns the number of bytes copied to dest.
 */
static int substrncpy(char *dest, const char *src, int count)
{
	int max_cnt = count;

	while (count) {
		count--;
		*dest = *src;
		if (*dest == '\0')
			break;
		src++;
		dest++;
	}
	return max_cnt - count;
}


static void read_hw_log_done(struct rsxx_cardinfo *card,
			     struct creg_cmd *cmd,
			     int st)
{
	char *buf;
	char *log_str;
	int cnt;
	int len;
	int off;

	buf = cmd->buf;
	off = 0;

	/* Failed getting the log message */
	if (st)
		return;

	while (off < cmd->cnt8) {
		log_str = &card->log.buf[card->log.buf_len];
		cnt = min(cmd->cnt8 - off, LOG_BUF_SIZE8 - card->log.buf_len);
		len = substrncpy(log_str, &buf[off], cnt);

		off += len;
		card->log.buf_len += len;

		/*
		 * Flush the log if we've hit the end of a message or if we've
		 * run out of buffer space.
		 */
		if ((log_str[len - 1] == '\0')  ||
		    (card->log.buf_len == LOG_BUF_SIZE8)) {
			if (card->log.buf_len != 1) /* Don't log blank lines. */
				hw_log_msg(card, card->log.buf,
					   card->log.buf_len);
			card->log.buf_len = 0;
		}

	}

	if (cmd->status & CREG_STAT_LOG_PENDING)
		rsxx_read_hw_log(card);
}

int rsxx_read_hw_log(struct rsxx_cardinfo *card)
{
	int st;

	st = creg_queue_cmd(card, CREG_OP_READ, CREG_ADD_LOG,
			    sizeof(card->log.tmp), card->log.tmp,
			    1, read_hw_log_done, NULL);
	if (st)
		dev_err(CARD_TO_DEV(card),
			"Failed getting log text\n");

	return st;
}

/*-------------- IOCTL REG Access ------------------*/
static int issue_reg_cmd(struct rsxx_cardinfo *card,
			 struct rsxx_reg_access *cmd,
			 int read)
{
	unsigned int op = read ? CREG_OP_READ : CREG_OP_WRITE;

	return __issue_creg_rw(card, op, cmd->addr, cmd->cnt, cmd->data,
			       cmd->stream, &cmd->stat);
}

int rsxx_reg_access(struct rsxx_cardinfo *card,
			struct rsxx_reg_access __user *ucmd,
			int read)
{
	struct rsxx_reg_access cmd;
	int st;

	st = copy_from_user(&cmd, ucmd, sizeof(cmd));
	if (st)
		return -EFAULT;

	if (cmd.cnt > RSXX_MAX_REG_CNT)
		return -EFAULT;

	st = issue_reg_cmd(card, &cmd, read);
	if (st)
		return st;

	st = put_user(cmd.stat, &ucmd->stat);
	if (st)
		return -EFAULT;

	if (read) {
		st = copy_to_user(ucmd->data, cmd.data, cmd.cnt);
		if (st)
			return -EFAULT;
	}

	return 0;
}

void rsxx_eeh_save_issued_creg(struct rsxx_cardinfo *card)
{
	struct creg_cmd *cmd = NULL;

	cmd = card->creg_ctrl.active_cmd;
	card->creg_ctrl.active_cmd = NULL;

	if (cmd) {
		del_timer_sync(&card->creg_ctrl.cmd_timer);

		spin_lock_bh(&card->creg_ctrl.lock);
		list_add(&cmd->list, &card->creg_ctrl.queue);
		card->creg_ctrl.q_depth++;
		card->creg_ctrl.active = 0;
		spin_unlock_bh(&card->creg_ctrl.lock);
	}
}

void rsxx_kick_creg_queue(struct rsxx_cardinfo *card)
{
	spin_lock_bh(&card->creg_ctrl.lock);
	if (!list_empty(&card->creg_ctrl.queue))
		creg_kick_queue(card);
	spin_unlock_bh(&card->creg_ctrl.lock);
}

/*------------ Initialization & Setup --------------*/
int rsxx_creg_setup(struct rsxx_cardinfo *card)
{
	card->creg_ctrl.active_cmd = NULL;

	card->creg_ctrl.creg_wq =
			create_singlethread_workqueue(DRIVER_NAME"_creg");
	if (!card->creg_ctrl.creg_wq)
		return -ENOMEM;

	INIT_WORK(&card->creg_ctrl.done_work, creg_cmd_done);
	mutex_init(&card->creg_ctrl.reset_lock);
	INIT_LIST_HEAD(&card->creg_ctrl.queue);
	spin_lock_init(&card->creg_ctrl.lock);
	timer_setup(&card->creg_ctrl.cmd_timer, creg_cmd_timed_out, 0);

	return 0;
}

void rsxx_creg_destroy(struct rsxx_cardinfo *card)
{
	struct creg_cmd *cmd;
	struct creg_cmd *tmp;
	int cnt = 0;

	/* Cancel outstanding commands */
	spin_lock_bh(&card->creg_ctrl.lock);
	list_for_each_entry_safe(cmd, tmp, &card->creg_ctrl.queue, list) {
		list_del(&cmd->list);
		if (cmd->cb)
			cmd->cb(card, cmd, -ECANCELED);
		kmem_cache_free(creg_cmd_pool, cmd);
		cnt++;
	}

	if (cnt)
		dev_info(CARD_TO_DEV(card),
			"Canceled %d queue creg commands\n", cnt);

	cmd = card->creg_ctrl.active_cmd;
	card->creg_ctrl.active_cmd = NULL;
	if (cmd) {
		if (timer_pending(&card->creg_ctrl.cmd_timer))
			del_timer_sync(&card->creg_ctrl.cmd_timer);

		if (cmd->cb)
			cmd->cb(card, cmd, -ECANCELED);
		dev_info(CARD_TO_DEV(card),
			"Canceled active creg command\n");
		kmem_cache_free(creg_cmd_pool, cmd);
	}
	spin_unlock_bh(&card->creg_ctrl.lock);

	cancel_work_sync(&card->creg_ctrl.done_work);
}


int rsxx_creg_init(void)
{
	creg_cmd_pool = KMEM_CACHE(creg_cmd, SLAB_HWCACHE_ALIGN);
	if (!creg_cmd_pool)
		return -ENOMEM;

	return 0;
}

void rsxx_creg_cleanup(void)
{
	kmem_cache_destroy(creg_cmd_pool);
}
