/* esp_scsi.c: ESP SCSI driver.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/irqreturn.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_transport_spi.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME		"esp"
#define PFX DRV_MODULE_NAME	": "
#define DRV_VERSION		"2.000"
#define DRV_MODULE_RELDATE	"April 19, 2007"

/* SCSI bus reset settle time in seconds.  */
static int esp_bus_reset_settle = 3;

static u32 esp_debug;
#define ESP_DEBUG_INTR		0x00000001
#define ESP_DEBUG_SCSICMD	0x00000002
#define ESP_DEBUG_RESET		0x00000004
#define ESP_DEBUG_MSGIN		0x00000008
#define ESP_DEBUG_MSGOUT	0x00000010
#define ESP_DEBUG_CMDDONE	0x00000020
#define ESP_DEBUG_DISCONNECT	0x00000040
#define ESP_DEBUG_DATASTART	0x00000080
#define ESP_DEBUG_DATADONE	0x00000100
#define ESP_DEBUG_RECONNECT	0x00000200
#define ESP_DEBUG_AUTOSENSE	0x00000400
#define ESP_DEBUG_EVENT		0x00000800
#define ESP_DEBUG_COMMAND	0x00001000

#define esp_log_intr(f, a...) \
do {	if (esp_debug & ESP_DEBUG_INTR) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_reset(f, a...) \
do {	if (esp_debug & ESP_DEBUG_RESET) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_msgin(f, a...) \
do {	if (esp_debug & ESP_DEBUG_MSGIN) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_msgout(f, a...) \
do {	if (esp_debug & ESP_DEBUG_MSGOUT) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_cmddone(f, a...) \
do {	if (esp_debug & ESP_DEBUG_CMDDONE) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_disconnect(f, a...) \
do {	if (esp_debug & ESP_DEBUG_DISCONNECT) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_datastart(f, a...) \
do {	if (esp_debug & ESP_DEBUG_DATASTART) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_datadone(f, a...) \
do {	if (esp_debug & ESP_DEBUG_DATADONE) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_reconnect(f, a...) \
do {	if (esp_debug & ESP_DEBUG_RECONNECT) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_autosense(f, a...) \
do {	if (esp_debug & ESP_DEBUG_AUTOSENSE) \
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_event(f, a...) \
do {   if (esp_debug & ESP_DEBUG_EVENT)	\
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_log_command(f, a...) \
do {   if (esp_debug & ESP_DEBUG_COMMAND)	\
		shost_printk(KERN_DEBUG, esp->host, f, ## a);	\
} while (0)

#define esp_read8(REG)		esp->ops->esp_read8(esp, REG)
#define esp_write8(VAL,REG)	esp->ops->esp_write8(esp, VAL, REG)

static void esp_log_fill_regs(struct esp *esp,
			      struct esp_event_ent *p)
{
	p->sreg = esp->sreg;
	p->seqreg = esp->seqreg;
	p->sreg2 = esp->sreg2;
	p->ireg = esp->ireg;
	p->select_state = esp->select_state;
	p->event = esp->event;
}

void scsi_esp_cmd(struct esp *esp, u8 val)
{
	struct esp_event_ent *p;
	int idx = esp->esp_event_cur;

	p = &esp->esp_event_log[idx];
	p->type = ESP_EVENT_TYPE_CMD;
	p->val = val;
	esp_log_fill_regs(esp, p);

	esp->esp_event_cur = (idx + 1) & (ESP_EVENT_LOG_SZ - 1);

	esp_log_command("cmd[%02x]\n", val);
	esp_write8(val, ESP_CMD);
}
EXPORT_SYMBOL(scsi_esp_cmd);

static void esp_send_dma_cmd(struct esp *esp, int len, int max_len, int cmd)
{
	if (esp->flags & ESP_FLAG_USE_FIFO) {
		int i;

		scsi_esp_cmd(esp, ESP_CMD_FLUSH);
		for (i = 0; i < len; i++)
			esp_write8(esp->command_block[i], ESP_FDATA);
		scsi_esp_cmd(esp, cmd);
	} else {
		if (esp->rev == FASHME)
			scsi_esp_cmd(esp, ESP_CMD_FLUSH);
		cmd |= ESP_CMD_DMA;
		esp->ops->send_dma_cmd(esp, esp->command_block_dma,
				       len, max_len, 0, cmd);
	}
}

static void esp_event(struct esp *esp, u8 val)
{
	struct esp_event_ent *p;
	int idx = esp->esp_event_cur;

	p = &esp->esp_event_log[idx];
	p->type = ESP_EVENT_TYPE_EVENT;
	p->val = val;
	esp_log_fill_regs(esp, p);

	esp->esp_event_cur = (idx + 1) & (ESP_EVENT_LOG_SZ - 1);

	esp->event = val;
}

static void esp_dump_cmd_log(struct esp *esp)
{
	int idx = esp->esp_event_cur;
	int stop = idx;

	shost_printk(KERN_INFO, esp->host, "Dumping command log\n");
	do {
		struct esp_event_ent *p = &esp->esp_event_log[idx];

		shost_printk(KERN_INFO, esp->host,
			     "ent[%d] %s val[%02x] sreg[%02x] seqreg[%02x] "
			     "sreg2[%02x] ireg[%02x] ss[%02x] event[%02x]\n",
			     idx,
			     p->type == ESP_EVENT_TYPE_CMD ? "CMD" : "EVENT",
			     p->val, p->sreg, p->seqreg,
			     p->sreg2, p->ireg, p->select_state, p->event);

		idx = (idx + 1) & (ESP_EVENT_LOG_SZ - 1);
	} while (idx != stop);
}

static void esp_flush_fifo(struct esp *esp)
{
	scsi_esp_cmd(esp, ESP_CMD_FLUSH);
	if (esp->rev == ESP236) {
		int lim = 1000;

		while (esp_read8(ESP_FFLAGS) & ESP_FF_FBYTES) {
			if (--lim == 0) {
				shost_printk(KERN_ALERT, esp->host,
					     "ESP_FF_BYTES will not clear!\n");
				break;
			}
			udelay(1);
		}
	}
}

static void hme_read_fifo(struct esp *esp)
{
	int fcnt = esp_read8(ESP_FFLAGS) & ESP_FF_FBYTES;
	int idx = 0;

	while (fcnt--) {
		esp->fifo[idx++] = esp_read8(ESP_FDATA);
		esp->fifo[idx++] = esp_read8(ESP_FDATA);
	}
	if (esp->sreg2 & ESP_STAT2_F1BYTE) {
		esp_write8(0, ESP_FDATA);
		esp->fifo[idx++] = esp_read8(ESP_FDATA);
		scsi_esp_cmd(esp, ESP_CMD_FLUSH);
	}
	esp->fifo_cnt = idx;
}

static void esp_set_all_config3(struct esp *esp, u8 val)
{
	int i;

	for (i = 0; i < ESP_MAX_TARGET; i++)
		esp->target[i].esp_config3 = val;
}

/* Reset the ESP chip, _not_ the SCSI bus. */
static void esp_reset_esp(struct esp *esp)
{
	u8 family_code, version;

	/* Now reset the ESP chip */
	scsi_esp_cmd(esp, ESP_CMD_RC);
	scsi_esp_cmd(esp, ESP_CMD_NULL | ESP_CMD_DMA);
	if (esp->rev == FAST)
		esp_write8(ESP_CONFIG2_FENAB, ESP_CFG2);
	scsi_esp_cmd(esp, ESP_CMD_NULL | ESP_CMD_DMA);

	/* This is the only point at which it is reliable to read
	 * the ID-code for a fast ESP chip variants.
	 */
	esp->max_period = ((35 * esp->ccycle) / 1000);
	if (esp->rev == FAST) {
		version = esp_read8(ESP_UID);
		family_code = (version & 0xf8) >> 3;
		if (family_code == 0x02)
			esp->rev = FAS236;
		else if (family_code == 0x0a)
			esp->rev = FASHME; /* Version is usually '5'. */
		else
			esp->rev = FAS100A;
		esp->min_period = ((4 * esp->ccycle) / 1000);
	} else {
		esp->min_period = ((5 * esp->ccycle) / 1000);
	}
	if (esp->rev == FAS236) {
		/*
		 * The AM53c974 chip returns the same ID as FAS236;
		 * try to configure glitch eater.
		 */
		u8 config4 = ESP_CONFIG4_GE1;
		esp_write8(config4, ESP_CFG4);
		config4 = esp_read8(ESP_CFG4);
		if (config4 & ESP_CONFIG4_GE1) {
			esp->rev = PCSCSI;
			esp_write8(esp->config4, ESP_CFG4);
		}
	}
	esp->max_period = (esp->max_period + 3)>>2;
	esp->min_period = (esp->min_period + 3)>>2;

	esp_write8(esp->config1, ESP_CFG1);
	switch (esp->rev) {
	case ESP100:
		/* nothing to do */
		break;

	case ESP100A:
		esp_write8(esp->config2, ESP_CFG2);
		break;

	case ESP236:
		/* Slow 236 */
		esp_write8(esp->config2, ESP_CFG2);
		esp->prev_cfg3 = esp->target[0].esp_config3;
		esp_write8(esp->prev_cfg3, ESP_CFG3);
		break;

	case FASHME:
		esp->config2 |= (ESP_CONFIG2_HME32 | ESP_CONFIG2_HMEFENAB);
		/* fallthrough... */

	case FAS236:
	case PCSCSI:
		/* Fast 236, AM53c974 or HME */
		esp_write8(esp->config2, ESP_CFG2);
		if (esp->rev == FASHME) {
			u8 cfg3 = esp->target[0].esp_config3;

			cfg3 |= ESP_CONFIG3_FCLOCK | ESP_CONFIG3_OBPUSH;
			if (esp->scsi_id >= 8)
				cfg3 |= ESP_CONFIG3_IDBIT3;
			esp_set_all_config3(esp, cfg3);
		} else {
			u32 cfg3 = esp->target[0].esp_config3;

			cfg3 |= ESP_CONFIG3_FCLK;
			esp_set_all_config3(esp, cfg3);
		}
		esp->prev_cfg3 = esp->target[0].esp_config3;
		esp_write8(esp->prev_cfg3, ESP_CFG3);
		if (esp->rev == FASHME) {
			esp->radelay = 80;
		} else {
			if (esp->flags & ESP_FLAG_DIFFERENTIAL)
				esp->radelay = 0;
			else
				esp->radelay = 96;
		}
		break;

	case FAS100A:
		/* Fast 100a */
		esp_write8(esp->config2, ESP_CFG2);
		esp_set_all_config3(esp,
				    (esp->target[0].esp_config3 |
				     ESP_CONFIG3_FCLOCK));
		esp->prev_cfg3 = esp->target[0].esp_config3;
		esp_write8(esp->prev_cfg3, ESP_CFG3);
		esp->radelay = 32;
		break;

	default:
		break;
	}

	/* Reload the configuration registers */
	esp_write8(esp->cfact, ESP_CFACT);

	esp->prev_stp = 0;
	esp_write8(esp->prev_stp, ESP_STP);

	esp->prev_soff = 0;
	esp_write8(esp->prev_soff, ESP_SOFF);

	esp_write8(esp->neg_defp, ESP_TIMEO);

	/* Eat any bitrot in the chip */
	esp_read8(ESP_INTRPT);
	udelay(100);
}

static void esp_map_dma(struct esp *esp, struct scsi_cmnd *cmd)
{
	struct esp_cmd_priv *spriv = ESP_CMD_PRIV(cmd);
	struct scatterlist *sg = scsi_sglist(cmd);
	int dir = cmd->sc_data_direction;
	int total, i;

	if (dir == DMA_NONE)
		return;

	spriv->u.num_sg = esp->ops->map_sg(esp, sg, scsi_sg_count(cmd), dir);
	spriv->cur_residue = sg_dma_len(sg);
	spriv->cur_sg = sg;

	total = 0;
	for (i = 0; i < spriv->u.num_sg; i++)
		total += sg_dma_len(&sg[i]);
	spriv->tot_residue = total;
}

static dma_addr_t esp_cur_dma_addr(struct esp_cmd_entry *ent,
				   struct scsi_cmnd *cmd)
{
	struct esp_cmd_priv *p = ESP_CMD_PRIV(cmd);

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		return ent->sense_dma +
			(ent->sense_ptr - cmd->sense_buffer);
	}

	return sg_dma_address(p->cur_sg) +
		(sg_dma_len(p->cur_sg) -
		 p->cur_residue);
}

static unsigned int esp_cur_dma_len(struct esp_cmd_entry *ent,
				    struct scsi_cmnd *cmd)
{
	struct esp_cmd_priv *p = ESP_CMD_PRIV(cmd);

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		return SCSI_SENSE_BUFFERSIZE -
			(ent->sense_ptr - cmd->sense_buffer);
	}
	return p->cur_residue;
}

static void esp_advance_dma(struct esp *esp, struct esp_cmd_entry *ent,
			    struct scsi_cmnd *cmd, unsigned int len)
{
	struct esp_cmd_priv *p = ESP_CMD_PRIV(cmd);

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		ent->sense_ptr += len;
		return;
	}

	p->cur_residue -= len;
	p->tot_residue -= len;
	if (p->cur_residue < 0 || p->tot_residue < 0) {
		shost_printk(KERN_ERR, esp->host,
			     "Data transfer overflow.\n");
		shost_printk(KERN_ERR, esp->host,
			     "cur_residue[%d] tot_residue[%d] len[%u]\n",
			     p->cur_residue, p->tot_residue, len);
		p->cur_residue = 0;
		p->tot_residue = 0;
	}
	if (!p->cur_residue && p->tot_residue) {
		p->cur_sg++;
		p->cur_residue = sg_dma_len(p->cur_sg);
	}
}

static void esp_unmap_dma(struct esp *esp, struct scsi_cmnd *cmd)
{
	struct esp_cmd_priv *spriv = ESP_CMD_PRIV(cmd);
	int dir = cmd->sc_data_direction;

	if (dir == DMA_NONE)
		return;

	esp->ops->unmap_sg(esp, scsi_sglist(cmd), spriv->u.num_sg, dir);
}

static void esp_save_pointers(struct esp *esp, struct esp_cmd_entry *ent)
{
	struct scsi_cmnd *cmd = ent->cmd;
	struct esp_cmd_priv *spriv = ESP_CMD_PRIV(cmd);

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		ent->saved_sense_ptr = ent->sense_ptr;
		return;
	}
	ent->saved_cur_residue = spriv->cur_residue;
	ent->saved_cur_sg = spriv->cur_sg;
	ent->saved_tot_residue = spriv->tot_residue;
}

static void esp_restore_pointers(struct esp *esp, struct esp_cmd_entry *ent)
{
	struct scsi_cmnd *cmd = ent->cmd;
	struct esp_cmd_priv *spriv = ESP_CMD_PRIV(cmd);

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		ent->sense_ptr = ent->saved_sense_ptr;
		return;
	}
	spriv->cur_residue = ent->saved_cur_residue;
	spriv->cur_sg = ent->saved_cur_sg;
	spriv->tot_residue = ent->saved_tot_residue;
}

static void esp_check_command_len(struct esp *esp, struct scsi_cmnd *cmd)
{
	if (cmd->cmd_len == 6 ||
	    cmd->cmd_len == 10 ||
	    cmd->cmd_len == 12) {
		esp->flags &= ~ESP_FLAG_DOING_SLOWCMD;
	} else {
		esp->flags |= ESP_FLAG_DOING_SLOWCMD;
	}
}

static void esp_write_tgt_config3(struct esp *esp, int tgt)
{
	if (esp->rev > ESP100A) {
		u8 val = esp->target[tgt].esp_config3;

		if (val != esp->prev_cfg3) {
			esp->prev_cfg3 = val;
			esp_write8(val, ESP_CFG3);
		}
	}
}

static void esp_write_tgt_sync(struct esp *esp, int tgt)
{
	u8 off = esp->target[tgt].esp_offset;
	u8 per = esp->target[tgt].esp_period;

	if (off != esp->prev_soff) {
		esp->prev_soff = off;
		esp_write8(off, ESP_SOFF);
	}
	if (per != esp->prev_stp) {
		esp->prev_stp = per;
		esp_write8(per, ESP_STP);
	}
}

static u32 esp_dma_length_limit(struct esp *esp, u32 dma_addr, u32 dma_len)
{
	if (esp->rev == FASHME) {
		/* Arbitrary segment boundaries, 24-bit counts.  */
		if (dma_len > (1U << 24))
			dma_len = (1U << 24);
	} else {
		u32 base, end;

		/* ESP chip limits other variants by 16-bits of transfer
		 * count.  Actually on FAS100A and FAS236 we could get
		 * 24-bits of transfer count by enabling ESP_CONFIG2_FENAB
		 * in the ESP_CFG2 register but that causes other unwanted
		 * changes so we don't use it currently.
		 */
		if (dma_len > (1U << 16))
			dma_len = (1U << 16);

		/* All of the DMA variants hooked up to these chips
		 * cannot handle crossing a 24-bit address boundary.
		 */
		base = dma_addr & ((1U << 24) - 1U);
		end = base + dma_len;
		if (end > (1U << 24))
			end = (1U <<24);
		dma_len = end - base;
	}
	return dma_len;
}

static int esp_need_to_nego_wide(struct esp_target_data *tp)
{
	struct scsi_target *target = tp->starget;

	return spi_width(target) != tp->nego_goal_width;
}

static int esp_need_to_nego_sync(struct esp_target_data *tp)
{
	struct scsi_target *target = tp->starget;

	/* When offset is zero, period is "don't care".  */
	if (!spi_offset(target) && !tp->nego_goal_offset)
		return 0;

	if (spi_offset(target) == tp->nego_goal_offset &&
	    spi_period(target) == tp->nego_goal_period)
		return 0;

	return 1;
}

static int esp_alloc_lun_tag(struct esp_cmd_entry *ent,
			     struct esp_lun_data *lp)
{
	if (!ent->orig_tag[0]) {
		/* Non-tagged, slot already taken?  */
		if (lp->non_tagged_cmd)
			return -EBUSY;

		if (lp->hold) {
			/* We are being held by active tagged
			 * commands.
			 */
			if (lp->num_tagged)
				return -EBUSY;

			/* Tagged commands completed, we can unplug
			 * the queue and run this untagged command.
			 */
			lp->hold = 0;
		} else if (lp->num_tagged) {
			/* Plug the queue until num_tagged decreases
			 * to zero in esp_free_lun_tag.
			 */
			lp->hold = 1;
			return -EBUSY;
		}

		lp->non_tagged_cmd = ent;
		return 0;
	} else {
		/* Tagged command, see if blocked by a
		 * non-tagged one.
		 */
		if (lp->non_tagged_cmd || lp->hold)
			return -EBUSY;
	}

	BUG_ON(lp->tagged_cmds[ent->orig_tag[1]]);

	lp->tagged_cmds[ent->orig_tag[1]] = ent;
	lp->num_tagged++;

	return 0;
}

static void esp_free_lun_tag(struct esp_cmd_entry *ent,
			     struct esp_lun_data *lp)
{
	if (ent->orig_tag[0]) {
		BUG_ON(lp->tagged_cmds[ent->orig_tag[1]] != ent);
		lp->tagged_cmds[ent->orig_tag[1]] = NULL;
		lp->num_tagged--;
	} else {
		BUG_ON(lp->non_tagged_cmd != ent);
		lp->non_tagged_cmd = NULL;
	}
}

/* When a contingent allegiance conditon is created, we force feed a
 * REQUEST_SENSE command to the device to fetch the sense data.  I
 * tried many other schemes, relying on the scsi error handling layer
 * to send out the REQUEST_SENSE automatically, but this was difficult
 * to get right especially in the presence of applications like smartd
 * which use SG_IO to send out their own REQUEST_SENSE commands.
 */
static void esp_autosense(struct esp *esp, struct esp_cmd_entry *ent)
{
	struct scsi_cmnd *cmd = ent->cmd;
	struct scsi_device *dev = cmd->device;
	int tgt, lun;
	u8 *p, val;

	tgt = dev->id;
	lun = dev->lun;


	if (!ent->sense_ptr) {
		esp_log_autosense("Doing auto-sense for tgt[%d] lun[%d]\n",
				  tgt, lun);

		ent->sense_ptr = cmd->sense_buffer;
		ent->sense_dma = esp->ops->map_single(esp,
						      ent->sense_ptr,
						      SCSI_SENSE_BUFFERSIZE,
						      DMA_FROM_DEVICE);
	}
	ent->saved_sense_ptr = ent->sense_ptr;

	esp->active_cmd = ent;

	p = esp->command_block;
	esp->msg_out_len = 0;

	*p++ = IDENTIFY(0, lun);
	*p++ = REQUEST_SENSE;
	*p++ = ((dev->scsi_level <= SCSI_2) ?
		(lun << 5) : 0);
	*p++ = 0;
	*p++ = 0;
	*p++ = SCSI_SENSE_BUFFERSIZE;
	*p++ = 0;

	esp->select_state = ESP_SELECT_BASIC;

	val = tgt;
	if (esp->rev == FASHME)
		val |= ESP_BUSID_RESELID | ESP_BUSID_CTR32BIT;
	esp_write8(val, ESP_BUSID);

	esp_write_tgt_sync(esp, tgt);
	esp_write_tgt_config3(esp, tgt);

	val = (p - esp->command_block);

	esp_send_dma_cmd(esp, val, 16, ESP_CMD_SELA);
}

static struct esp_cmd_entry *find_and_prep_issuable_command(struct esp *esp)
{
	struct esp_cmd_entry *ent;

	list_for_each_entry(ent, &esp->queued_cmds, list) {
		struct scsi_cmnd *cmd = ent->cmd;
		struct scsi_device *dev = cmd->device;
		struct esp_lun_data *lp = dev->hostdata;

		if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
			ent->tag[0] = 0;
			ent->tag[1] = 0;
			return ent;
		}

		if (!spi_populate_tag_msg(&ent->tag[0], cmd)) {
			ent->tag[0] = 0;
			ent->tag[1] = 0;
		}
		ent->orig_tag[0] = ent->tag[0];
		ent->orig_tag[1] = ent->tag[1];

		if (esp_alloc_lun_tag(ent, lp) < 0)
			continue;

		return ent;
	}

	return NULL;
}

static void esp_maybe_execute_command(struct esp *esp)
{
	struct esp_target_data *tp;
	struct esp_lun_data *lp;
	struct scsi_device *dev;
	struct scsi_cmnd *cmd;
	struct esp_cmd_entry *ent;
	int tgt, lun, i;
	u32 val, start_cmd;
	u8 *p;

	if (esp->active_cmd ||
	    (esp->flags & ESP_FLAG_RESETTING))
		return;

	ent = find_and_prep_issuable_command(esp);
	if (!ent)
		return;

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		esp_autosense(esp, ent);
		return;
	}

	cmd = ent->cmd;
	dev = cmd->device;
	tgt = dev->id;
	lun = dev->lun;
	tp = &esp->target[tgt];
	lp = dev->hostdata;

	list_move(&ent->list, &esp->active_cmds);

	esp->active_cmd = ent;

	esp_map_dma(esp, cmd);
	esp_save_pointers(esp, ent);

	esp_check_command_len(esp, cmd);

	p = esp->command_block;

	esp->msg_out_len = 0;
	if (tp->flags & ESP_TGT_CHECK_NEGO) {
		/* Need to negotiate.  If the target is broken
		 * go for synchronous transfers and non-wide.
		 */
		if (tp->flags & ESP_TGT_BROKEN) {
			tp->flags &= ~ESP_TGT_DISCONNECT;
			tp->nego_goal_period = 0;
			tp->nego_goal_offset = 0;
			tp->nego_goal_width = 0;
			tp->nego_goal_tags = 0;
		}

		/* If the settings are not changing, skip this.  */
		if (spi_width(tp->starget) == tp->nego_goal_width &&
		    spi_period(tp->starget) == tp->nego_goal_period &&
		    spi_offset(tp->starget) == tp->nego_goal_offset) {
			tp->flags &= ~ESP_TGT_CHECK_NEGO;
			goto build_identify;
		}

		if (esp->rev == FASHME && esp_need_to_nego_wide(tp)) {
			esp->msg_out_len =
				spi_populate_width_msg(&esp->msg_out[0],
						       (tp->nego_goal_width ?
							1 : 0));
			tp->flags |= ESP_TGT_NEGO_WIDE;
		} else if (esp_need_to_nego_sync(tp)) {
			esp->msg_out_len =
				spi_populate_sync_msg(&esp->msg_out[0],
						      tp->nego_goal_period,
						      tp->nego_goal_offset);
			tp->flags |= ESP_TGT_NEGO_SYNC;
		} else {
			tp->flags &= ~ESP_TGT_CHECK_NEGO;
		}

		/* Process it like a slow command.  */
		if (tp->flags & (ESP_TGT_NEGO_WIDE | ESP_TGT_NEGO_SYNC))
			esp->flags |= ESP_FLAG_DOING_SLOWCMD;
	}

build_identify:
	/* If we don't have a lun-data struct yet, we're probing
	 * so do not disconnect.  Also, do not disconnect unless
	 * we have a tag on this command.
	 */
	if (lp && (tp->flags & ESP_TGT_DISCONNECT) && ent->tag[0])
		*p++ = IDENTIFY(1, lun);
	else
		*p++ = IDENTIFY(0, lun);

	if (ent->tag[0] && esp->rev == ESP100) {
		/* ESP100 lacks select w/atn3 command, use select
		 * and stop instead.
		 */
		esp->flags |= ESP_FLAG_DOING_SLOWCMD;
	}

	if (!(esp->flags & ESP_FLAG_DOING_SLOWCMD)) {
		start_cmd = ESP_CMD_SELA;
		if (ent->tag[0]) {
			*p++ = ent->tag[0];
			*p++ = ent->tag[1];

			start_cmd = ESP_CMD_SA3;
		}

		for (i = 0; i < cmd->cmd_len; i++)
			*p++ = cmd->cmnd[i];

		esp->select_state = ESP_SELECT_BASIC;
	} else {
		esp->cmd_bytes_left = cmd->cmd_len;
		esp->cmd_bytes_ptr = &cmd->cmnd[0];

		if (ent->tag[0]) {
			for (i = esp->msg_out_len - 1;
			     i >= 0; i--)
				esp->msg_out[i + 2] = esp->msg_out[i];
			esp->msg_out[0] = ent->tag[0];
			esp->msg_out[1] = ent->tag[1];
			esp->msg_out_len += 2;
		}

		start_cmd = ESP_CMD_SELAS;
		esp->select_state = ESP_SELECT_MSGOUT;
	}
	val = tgt;
	if (esp->rev == FASHME)
		val |= ESP_BUSID_RESELID | ESP_BUSID_CTR32BIT;
	esp_write8(val, ESP_BUSID);

	esp_write_tgt_sync(esp, tgt);
	esp_write_tgt_config3(esp, tgt);

	val = (p - esp->command_block);

	if (esp_debug & ESP_DEBUG_SCSICMD) {
		printk("ESP: tgt[%d] lun[%d] scsi_cmd [ ", tgt, lun);
		for (i = 0; i < cmd->cmd_len; i++)
			printk("%02x ", cmd->cmnd[i]);
		printk("]\n");
	}

	esp_send_dma_cmd(esp, val, 16, start_cmd);
}

static struct esp_cmd_entry *esp_get_ent(struct esp *esp)
{
	struct list_head *head = &esp->esp_cmd_pool;
	struct esp_cmd_entry *ret;

	if (list_empty(head)) {
		ret = kzalloc(sizeof(struct esp_cmd_entry), GFP_ATOMIC);
	} else {
		ret = list_entry(head->next, struct esp_cmd_entry, list);
		list_del(&ret->list);
		memset(ret, 0, sizeof(*ret));
	}
	return ret;
}

static void esp_put_ent(struct esp *esp, struct esp_cmd_entry *ent)
{
	list_add(&ent->list, &esp->esp_cmd_pool);
}

static void esp_cmd_is_done(struct esp *esp, struct esp_cmd_entry *ent,
			    struct scsi_cmnd *cmd, unsigned int result)
{
	struct scsi_device *dev = cmd->device;
	int tgt = dev->id;
	int lun = dev->lun;

	esp->active_cmd = NULL;
	esp_unmap_dma(esp, cmd);
	esp_free_lun_tag(ent, dev->hostdata);
	cmd->result = result;

	if (ent->eh_done) {
		complete(ent->eh_done);
		ent->eh_done = NULL;
	}

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		esp->ops->unmap_single(esp, ent->sense_dma,
				       SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
		ent->sense_ptr = NULL;

		/* Restore the message/status bytes to what we actually
		 * saw originally.  Also, report that we are providing
		 * the sense data.
		 */
		cmd->result = ((DRIVER_SENSE << 24) |
			       (DID_OK << 16) |
			       (COMMAND_COMPLETE << 8) |
			       (SAM_STAT_CHECK_CONDITION << 0));

		ent->flags &= ~ESP_CMD_FLAG_AUTOSENSE;
		if (esp_debug & ESP_DEBUG_AUTOSENSE) {
			int i;

			printk("esp%d: tgt[%d] lun[%d] AUTO SENSE[ ",
			       esp->host->unique_id, tgt, lun);
			for (i = 0; i < 18; i++)
				printk("%02x ", cmd->sense_buffer[i]);
			printk("]\n");
		}
	}

	cmd->scsi_done(cmd);

	list_del(&ent->list);
	esp_put_ent(esp, ent);

	esp_maybe_execute_command(esp);
}

static unsigned int compose_result(unsigned int status, unsigned int message,
				   unsigned int driver_code)
{
	return (status | (message << 8) | (driver_code << 16));
}

static void esp_event_queue_full(struct esp *esp, struct esp_cmd_entry *ent)
{
	struct scsi_device *dev = ent->cmd->device;
	struct esp_lun_data *lp = dev->hostdata;

	scsi_track_queue_full(dev, lp->num_tagged - 1);
}

static int esp_queuecommand_lck(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct scsi_device *dev = cmd->device;
	struct esp *esp = shost_priv(dev->host);
	struct esp_cmd_priv *spriv;
	struct esp_cmd_entry *ent;

	ent = esp_get_ent(esp);
	if (!ent)
		return SCSI_MLQUEUE_HOST_BUSY;

	ent->cmd = cmd;

	cmd->scsi_done = done;

	spriv = ESP_CMD_PRIV(cmd);
	spriv->u.dma_addr = ~(dma_addr_t)0x0;

	list_add_tail(&ent->list, &esp->queued_cmds);

	esp_maybe_execute_command(esp);

	return 0;
}

static DEF_SCSI_QCMD(esp_queuecommand)

static int esp_check_gross_error(struct esp *esp)
{
	if (esp->sreg & ESP_STAT_SPAM) {
		/* Gross Error, could be one of:
		 * - top of fifo overwritten
		 * - top of command register overwritten
		 * - DMA programmed with wrong direction
		 * - improper phase change
		 */
		shost_printk(KERN_ERR, esp->host,
			     "Gross error sreg[%02x]\n", esp->sreg);
		/* XXX Reset the chip. XXX */
		return 1;
	}
	return 0;
}

static int esp_check_spur_intr(struct esp *esp)
{
	switch (esp->rev) {
	case ESP100:
	case ESP100A:
		/* The interrupt pending bit of the status register cannot
		 * be trusted on these revisions.
		 */
		esp->sreg &= ~ESP_STAT_INTR;
		break;

	default:
		if (!(esp->sreg & ESP_STAT_INTR)) {
			if (esp->ireg & ESP_INTR_SR)
				return 1;

			/* If the DMA is indicating interrupt pending and the
			 * ESP is not, the only possibility is a DMA error.
			 */
			if (!esp->ops->dma_error(esp)) {
				shost_printk(KERN_ERR, esp->host,
					     "Spurious irq, sreg=%02x.\n",
					     esp->sreg);
				return -1;
			}

			shost_printk(KERN_ERR, esp->host, "DMA error\n");

			/* XXX Reset the chip. XXX */
			return -1;
		}
		break;
	}

	return 0;
}

static void esp_schedule_reset(struct esp *esp)
{
	esp_log_reset("esp_schedule_reset() from %pf\n",
		      __builtin_return_address(0));
	esp->flags |= ESP_FLAG_RESETTING;
	esp_event(esp, ESP_EVENT_RESET);
}

/* In order to avoid having to add a special half-reconnected state
 * into the driver we just sit here and poll through the rest of
 * the reselection process to get the tag message bytes.
 */
static struct esp_cmd_entry *esp_reconnect_with_tag(struct esp *esp,
						    struct esp_lun_data *lp)
{
	struct esp_cmd_entry *ent;
	int i;

	if (!lp->num_tagged) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect w/num_tagged==0\n");
		return NULL;
	}

	esp_log_reconnect("reconnect tag, ");

	for (i = 0; i < ESP_QUICKIRQ_LIMIT; i++) {
		if (esp->ops->irq_pending(esp))
			break;
	}
	if (i == ESP_QUICKIRQ_LIMIT) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect IRQ1 timeout\n");
		return NULL;
	}

	esp->sreg = esp_read8(ESP_STATUS);
	esp->ireg = esp_read8(ESP_INTRPT);

	esp_log_reconnect("IRQ(%d:%x:%x), ",
			  i, esp->ireg, esp->sreg);

	if (esp->ireg & ESP_INTR_DC) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect, got disconnect.\n");
		return NULL;
	}

	if ((esp->sreg & ESP_STAT_PMASK) != ESP_MIP) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect, not MIP sreg[%02x].\n", esp->sreg);
		return NULL;
	}

	/* DMA in the tag bytes... */
	esp->command_block[0] = 0xff;
	esp->command_block[1] = 0xff;
	esp->ops->send_dma_cmd(esp, esp->command_block_dma,
			       2, 2, 1, ESP_CMD_DMA | ESP_CMD_TI);

	/* ACK the message.  */
	scsi_esp_cmd(esp, ESP_CMD_MOK);

	for (i = 0; i < ESP_RESELECT_TAG_LIMIT; i++) {
		if (esp->ops->irq_pending(esp)) {
			esp->sreg = esp_read8(ESP_STATUS);
			esp->ireg = esp_read8(ESP_INTRPT);
			if (esp->ireg & ESP_INTR_FDONE)
				break;
		}
		udelay(1);
	}
	if (i == ESP_RESELECT_TAG_LIMIT) {
		shost_printk(KERN_ERR, esp->host, "Reconnect IRQ2 timeout\n");
		return NULL;
	}
	esp->ops->dma_drain(esp);
	esp->ops->dma_invalidate(esp);

	esp_log_reconnect("IRQ2(%d:%x:%x) tag[%x:%x]\n",
			  i, esp->ireg, esp->sreg,
			  esp->command_block[0],
			  esp->command_block[1]);

	if (esp->command_block[0] < SIMPLE_QUEUE_TAG ||
	    esp->command_block[0] > ORDERED_QUEUE_TAG) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect, bad tag type %02x.\n",
			     esp->command_block[0]);
		return NULL;
	}

	ent = lp->tagged_cmds[esp->command_block[1]];
	if (!ent) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect, no entry for tag %02x.\n",
			     esp->command_block[1]);
		return NULL;
	}

	return ent;
}

static int esp_reconnect(struct esp *esp)
{
	struct esp_cmd_entry *ent;
	struct esp_target_data *tp;
	struct esp_lun_data *lp;
	struct scsi_device *dev;
	int target, lun;

	BUG_ON(esp->active_cmd);
	if (esp->rev == FASHME) {
		/* FASHME puts the target and lun numbers directly
		 * into the fifo.
		 */
		target = esp->fifo[0];
		lun = esp->fifo[1] & 0x7;
	} else {
		u8 bits = esp_read8(ESP_FDATA);

		/* Older chips put the lun directly into the fifo, but
		 * the target is given as a sample of the arbitration
		 * lines on the bus at reselection time.  So we should
		 * see the ID of the ESP and the one reconnecting target
		 * set in the bitmap.
		 */
		if (!(bits & esp->scsi_id_mask))
			goto do_reset;
		bits &= ~esp->scsi_id_mask;
		if (!bits || (bits & (bits - 1)))
			goto do_reset;

		target = ffs(bits) - 1;
		lun = (esp_read8(ESP_FDATA) & 0x7);

		scsi_esp_cmd(esp, ESP_CMD_FLUSH);
		if (esp->rev == ESP100) {
			u8 ireg = esp_read8(ESP_INTRPT);
			/* This chip has a bug during reselection that can
			 * cause a spurious illegal-command interrupt, which
			 * we simply ACK here.  Another possibility is a bus
			 * reset so we must check for that.
			 */
			if (ireg & ESP_INTR_SR)
				goto do_reset;
		}
		scsi_esp_cmd(esp, ESP_CMD_NULL);
	}

	esp_write_tgt_sync(esp, target);
	esp_write_tgt_config3(esp, target);

	scsi_esp_cmd(esp, ESP_CMD_MOK);

	if (esp->rev == FASHME)
		esp_write8(target | ESP_BUSID_RESELID | ESP_BUSID_CTR32BIT,
			   ESP_BUSID);

	tp = &esp->target[target];
	dev = __scsi_device_lookup_by_target(tp->starget, lun);
	if (!dev) {
		shost_printk(KERN_ERR, esp->host,
			     "Reconnect, no lp tgt[%u] lun[%u]\n",
			     target, lun);
		goto do_reset;
	}
	lp = dev->hostdata;

	ent = lp->non_tagged_cmd;
	if (!ent) {
		ent = esp_reconnect_with_tag(esp, lp);
		if (!ent)
			goto do_reset;
	}

	esp->active_cmd = ent;

	if (ent->flags & ESP_CMD_FLAG_ABORT) {
		esp->msg_out[0] = ABORT_TASK_SET;
		esp->msg_out_len = 1;
		scsi_esp_cmd(esp, ESP_CMD_SATN);
	}

	esp_event(esp, ESP_EVENT_CHECK_PHASE);
	esp_restore_pointers(esp, ent);
	esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
	return 1;

do_reset:
	esp_schedule_reset(esp);
	return 0;
}

static int esp_finish_select(struct esp *esp)
{
	struct esp_cmd_entry *ent;
	struct scsi_cmnd *cmd;
	u8 orig_select_state;

	orig_select_state = esp->select_state;

	/* No longer selecting.  */
	esp->select_state = ESP_SELECT_NONE;

	esp->seqreg = esp_read8(ESP_SSTEP) & ESP_STEP_VBITS;
	ent = esp->active_cmd;
	cmd = ent->cmd;

	if (esp->ops->dma_error(esp)) {
		/* If we see a DMA error during or as a result of selection,
		 * all bets are off.
		 */
		esp_schedule_reset(esp);
		esp_cmd_is_done(esp, ent, cmd, (DID_ERROR << 16));
		return 0;
	}

	esp->ops->dma_invalidate(esp);

	if (esp->ireg == (ESP_INTR_RSEL | ESP_INTR_FDONE)) {
		struct esp_target_data *tp = &esp->target[cmd->device->id];

		/* Carefully back out of the selection attempt.  Release
		 * resources (such as DMA mapping & TAG) and reset state (such
		 * as message out and command delivery variables).
		 */
		if (!(ent->flags & ESP_CMD_FLAG_AUTOSENSE)) {
			esp_unmap_dma(esp, cmd);
			esp_free_lun_tag(ent, cmd->device->hostdata);
			tp->flags &= ~(ESP_TGT_NEGO_SYNC | ESP_TGT_NEGO_WIDE);
			esp->flags &= ~ESP_FLAG_DOING_SLOWCMD;
			esp->cmd_bytes_ptr = NULL;
			esp->cmd_bytes_left = 0;
		} else {
			esp->ops->unmap_single(esp, ent->sense_dma,
					       SCSI_SENSE_BUFFERSIZE,
					       DMA_FROM_DEVICE);
			ent->sense_ptr = NULL;
		}

		/* Now that the state is unwound properly, put back onto
		 * the issue queue.  This command is no longer active.
		 */
		list_move(&ent->list, &esp->queued_cmds);
		esp->active_cmd = NULL;

		/* Return value ignored by caller, it directly invokes
		 * esp_reconnect().
		 */
		return 0;
	}

	if (esp->ireg == ESP_INTR_DC) {
		struct scsi_device *dev = cmd->device;

		/* Disconnect.  Make sure we re-negotiate sync and
		 * wide parameters if this target starts responding
		 * again in the future.
		 */
		esp->target[dev->id].flags |= ESP_TGT_CHECK_NEGO;

		scsi_esp_cmd(esp, ESP_CMD_ESEL);
		esp_cmd_is_done(esp, ent, cmd, (DID_BAD_TARGET << 16));
		return 1;
	}

	if (esp->ireg == (ESP_INTR_FDONE | ESP_INTR_BSERV)) {
		/* Selection successful.  On pre-FAST chips we have
		 * to do a NOP and possibly clean out the FIFO.
		 */
		if (esp->rev <= ESP236) {
			int fcnt = esp_read8(ESP_FFLAGS) & ESP_FF_FBYTES;

			scsi_esp_cmd(esp, ESP_CMD_NULL);

			if (!fcnt &&
			    (!esp->prev_soff ||
			     ((esp->sreg & ESP_STAT_PMASK) != ESP_DIP)))
				esp_flush_fifo(esp);
		}

		/* If we are doing a slow command, negotiation, etc.
		 * we'll do the right thing as we transition to the
		 * next phase.
		 */
		esp_event(esp, ESP_EVENT_CHECK_PHASE);
		return 0;
	}

	shost_printk(KERN_INFO, esp->host,
		     "Unexpected selection completion ireg[%x]\n", esp->ireg);
	esp_schedule_reset(esp);
	return 0;
}

static int esp_data_bytes_sent(struct esp *esp, struct esp_cmd_entry *ent,
			       struct scsi_cmnd *cmd)
{
	int fifo_cnt, ecount, bytes_sent, flush_fifo;

	fifo_cnt = esp_read8(ESP_FFLAGS) & ESP_FF_FBYTES;
	if (esp->prev_cfg3 & ESP_CONFIG3_EWIDE)
		fifo_cnt <<= 1;

	ecount = 0;
	if (!(esp->sreg & ESP_STAT_TCNT)) {
		ecount = ((unsigned int)esp_read8(ESP_TCLOW) |
			  (((unsigned int)esp_read8(ESP_TCMED)) << 8));
		if (esp->rev == FASHME)
			ecount |= ((unsigned int)esp_read8(FAS_RLO)) << 16;
		if (esp->rev == PCSCSI && (esp->config2 & ESP_CONFIG2_FENAB))
			ecount |= ((unsigned int)esp_read8(ESP_TCHI)) << 16;
	}

	bytes_sent = esp->data_dma_len;
	bytes_sent -= ecount;

	/*
	 * The am53c974 has a DMA 'pecularity'. The doc states:
	 * In some odd byte conditions, one residual byte will
	 * be left in the SCSI FIFO, and the FIFO Flags will
	 * never count to '0 '. When this happens, the residual
	 * byte should be retrieved via PIO following completion
	 * of the BLAST operation.
	 */
	if (fifo_cnt == 1 && ent->flags & ESP_CMD_FLAG_RESIDUAL) {
		size_t count = 1;
		size_t offset = bytes_sent;
		u8 bval = esp_read8(ESP_FDATA);

		if (ent->flags & ESP_CMD_FLAG_AUTOSENSE)
			ent->sense_ptr[bytes_sent] = bval;
		else {
			struct esp_cmd_priv *p = ESP_CMD_PRIV(cmd);
			u8 *ptr;

			ptr = scsi_kmap_atomic_sg(p->cur_sg, p->u.num_sg,
						  &offset, &count);
			if (likely(ptr)) {
				*(ptr + offset) = bval;
				scsi_kunmap_atomic_sg(ptr);
			}
		}
		bytes_sent += fifo_cnt;
		ent->flags &= ~ESP_CMD_FLAG_RESIDUAL;
	}
	if (!(ent->flags & ESP_CMD_FLAG_WRITE))
		bytes_sent -= fifo_cnt;

	flush_fifo = 0;
	if (!esp->prev_soff) {
		/* Synchronous data transfer, always flush fifo. */
		flush_fifo = 1;
	} else {
		if (esp->rev == ESP100) {
			u32 fflags, phase;

			/* ESP100 has a chip bug where in the synchronous data
			 * phase it can mistake a final long REQ pulse from the
			 * target as an extra data byte.  Fun.
			 *
			 * To detect this case we resample the status register
			 * and fifo flags.  If we're still in a data phase and
			 * we see spurious chunks in the fifo, we return error
			 * to the caller which should reset and set things up
			 * such that we only try future transfers to this
			 * target in synchronous mode.
			 */
			esp->sreg = esp_read8(ESP_STATUS);
			phase = esp->sreg & ESP_STAT_PMASK;
			fflags = esp_read8(ESP_FFLAGS);

			if ((phase == ESP_DOP &&
			     (fflags & ESP_FF_ONOTZERO)) ||
			    (phase == ESP_DIP &&
			     (fflags & ESP_FF_FBYTES)))
				return -1;
		}
		if (!(ent->flags & ESP_CMD_FLAG_WRITE))
			flush_fifo = 1;
	}

	if (flush_fifo)
		esp_flush_fifo(esp);

	return bytes_sent;
}

static void esp_setsync(struct esp *esp, struct esp_target_data *tp,
			u8 scsi_period, u8 scsi_offset,
			u8 esp_stp, u8 esp_soff)
{
	spi_period(tp->starget) = scsi_period;
	spi_offset(tp->starget) = scsi_offset;
	spi_width(tp->starget) = (tp->flags & ESP_TGT_WIDE) ? 1 : 0;

	if (esp_soff) {
		esp_stp &= 0x1f;
		esp_soff |= esp->radelay;
		if (esp->rev >= FAS236) {
			u8 bit = ESP_CONFIG3_FSCSI;
			if (esp->rev >= FAS100A)
				bit = ESP_CONFIG3_FAST;

			if (scsi_period < 50) {
				if (esp->rev == FASHME)
					esp_soff &= ~esp->radelay;
				tp->esp_config3 |= bit;
			} else {
				tp->esp_config3 &= ~bit;
			}
			esp->prev_cfg3 = tp->esp_config3;
			esp_write8(esp->prev_cfg3, ESP_CFG3);
		}
	}

	tp->esp_period = esp->prev_stp = esp_stp;
	tp->esp_offset = esp->prev_soff = esp_soff;

	esp_write8(esp_soff, ESP_SOFF);
	esp_write8(esp_stp, ESP_STP);

	tp->flags &= ~(ESP_TGT_NEGO_SYNC | ESP_TGT_CHECK_NEGO);

	spi_display_xfer_agreement(tp->starget);
}

static void esp_msgin_reject(struct esp *esp)
{
	struct esp_cmd_entry *ent = esp->active_cmd;
	struct scsi_cmnd *cmd = ent->cmd;
	struct esp_target_data *tp;
	int tgt;

	tgt = cmd->device->id;
	tp = &esp->target[tgt];

	if (tp->flags & ESP_TGT_NEGO_WIDE) {
		tp->flags &= ~(ESP_TGT_NEGO_WIDE | ESP_TGT_WIDE);

		if (!esp_need_to_nego_sync(tp)) {
			tp->flags &= ~ESP_TGT_CHECK_NEGO;
			scsi_esp_cmd(esp, ESP_CMD_RATN);
		} else {
			esp->msg_out_len =
				spi_populate_sync_msg(&esp->msg_out[0],
						      tp->nego_goal_period,
						      tp->nego_goal_offset);
			tp->flags |= ESP_TGT_NEGO_SYNC;
			scsi_esp_cmd(esp, ESP_CMD_SATN);
		}
		return;
	}

	if (tp->flags & ESP_TGT_NEGO_SYNC) {
		tp->flags &= ~(ESP_TGT_NEGO_SYNC | ESP_TGT_CHECK_NEGO);
		tp->esp_period = 0;
		tp->esp_offset = 0;
		esp_setsync(esp, tp, 0, 0, 0, 0);
		scsi_esp_cmd(esp, ESP_CMD_RATN);
		return;
	}

	esp->msg_out[0] = ABORT_TASK_SET;
	esp->msg_out_len = 1;
	scsi_esp_cmd(esp, ESP_CMD_SATN);
}

static void esp_msgin_sdtr(struct esp *esp, struct esp_target_data *tp)
{
	u8 period = esp->msg_in[3];
	u8 offset = esp->msg_in[4];
	u8 stp;

	if (!(tp->flags & ESP_TGT_NEGO_SYNC))
		goto do_reject;

	if (offset > 15)
		goto do_reject;

	if (offset) {
		int one_clock;

		if (period > esp->max_period) {
			period = offset = 0;
			goto do_sdtr;
		}
		if (period < esp->min_period)
			goto do_reject;

		one_clock = esp->ccycle / 1000;
		stp = DIV_ROUND_UP(period << 2, one_clock);
		if (stp && esp->rev >= FAS236) {
			if (stp >= 50)
				stp--;
		}
	} else {
		stp = 0;
	}

	esp_setsync(esp, tp, period, offset, stp, offset);
	return;

do_reject:
	esp->msg_out[0] = MESSAGE_REJECT;
	esp->msg_out_len = 1;
	scsi_esp_cmd(esp, ESP_CMD_SATN);
	return;

do_sdtr:
	tp->nego_goal_period = period;
	tp->nego_goal_offset = offset;
	esp->msg_out_len =
		spi_populate_sync_msg(&esp->msg_out[0],
				      tp->nego_goal_period,
				      tp->nego_goal_offset);
	scsi_esp_cmd(esp, ESP_CMD_SATN);
}

static void esp_msgin_wdtr(struct esp *esp, struct esp_target_data *tp)
{
	int size = 8 << esp->msg_in[3];
	u8 cfg3;

	if (esp->rev != FASHME)
		goto do_reject;

	if (size != 8 && size != 16)
		goto do_reject;

	if (!(tp->flags & ESP_TGT_NEGO_WIDE))
		goto do_reject;

	cfg3 = tp->esp_config3;
	if (size == 16) {
		tp->flags |= ESP_TGT_WIDE;
		cfg3 |= ESP_CONFIG3_EWIDE;
	} else {
		tp->flags &= ~ESP_TGT_WIDE;
		cfg3 &= ~ESP_CONFIG3_EWIDE;
	}
	tp->esp_config3 = cfg3;
	esp->prev_cfg3 = cfg3;
	esp_write8(cfg3, ESP_CFG3);

	tp->flags &= ~ESP_TGT_NEGO_WIDE;

	spi_period(tp->starget) = 0;
	spi_offset(tp->starget) = 0;
	if (!esp_need_to_nego_sync(tp)) {
		tp->flags &= ~ESP_TGT_CHECK_NEGO;
		scsi_esp_cmd(esp, ESP_CMD_RATN);
	} else {
		esp->msg_out_len =
			spi_populate_sync_msg(&esp->msg_out[0],
					      tp->nego_goal_period,
					      tp->nego_goal_offset);
		tp->flags |= ESP_TGT_NEGO_SYNC;
		scsi_esp_cmd(esp, ESP_CMD_SATN);
	}
	return;

do_reject:
	esp->msg_out[0] = MESSAGE_REJECT;
	esp->msg_out_len = 1;
	scsi_esp_cmd(esp, ESP_CMD_SATN);
}

static void esp_msgin_extended(struct esp *esp)
{
	struct esp_cmd_entry *ent = esp->active_cmd;
	struct scsi_cmnd *cmd = ent->cmd;
	struct esp_target_data *tp;
	int tgt = cmd->device->id;

	tp = &esp->target[tgt];
	if (esp->msg_in[2] == EXTENDED_SDTR) {
		esp_msgin_sdtr(esp, tp);
		return;
	}
	if (esp->msg_in[2] == EXTENDED_WDTR) {
		esp_msgin_wdtr(esp, tp);
		return;
	}

	shost_printk(KERN_INFO, esp->host,
		     "Unexpected extended msg type %x\n", esp->msg_in[2]);

	esp->msg_out[0] = ABORT_TASK_SET;
	esp->msg_out_len = 1;
	scsi_esp_cmd(esp, ESP_CMD_SATN);
}

/* Analyze msgin bytes received from target so far.  Return non-zero
 * if there are more bytes needed to complete the message.
 */
static int esp_msgin_process(struct esp *esp)
{
	u8 msg0 = esp->msg_in[0];
	int len = esp->msg_in_len;

	if (msg0 & 0x80) {
		/* Identify */
		shost_printk(KERN_INFO, esp->host,
			     "Unexpected msgin identify\n");
		return 0;
	}

	switch (msg0) {
	case EXTENDED_MESSAGE:
		if (len == 1)
			return 1;
		if (len < esp->msg_in[1] + 2)
			return 1;
		esp_msgin_extended(esp);
		return 0;

	case IGNORE_WIDE_RESIDUE: {
		struct esp_cmd_entry *ent;
		struct esp_cmd_priv *spriv;
		if (len == 1)
			return 1;

		if (esp->msg_in[1] != 1)
			goto do_reject;

		ent = esp->active_cmd;
		spriv = ESP_CMD_PRIV(ent->cmd);

		if (spriv->cur_residue == sg_dma_len(spriv->cur_sg)) {
			spriv->cur_sg--;
			spriv->cur_residue = 1;
		} else
			spriv->cur_residue++;
		spriv->tot_residue++;
		return 0;
	}
	case NOP:
		return 0;
	case RESTORE_POINTERS:
		esp_restore_pointers(esp, esp->active_cmd);
		return 0;
	case SAVE_POINTERS:
		esp_save_pointers(esp, esp->active_cmd);
		return 0;

	case COMMAND_COMPLETE:
	case DISCONNECT: {
		struct esp_cmd_entry *ent = esp->active_cmd;

		ent->message = msg0;
		esp_event(esp, ESP_EVENT_FREE_BUS);
		esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
		return 0;
	}
	case MESSAGE_REJECT:
		esp_msgin_reject(esp);
		return 0;

	default:
	do_reject:
		esp->msg_out[0] = MESSAGE_REJECT;
		esp->msg_out_len = 1;
		scsi_esp_cmd(esp, ESP_CMD_SATN);
		return 0;
	}
}

static int esp_process_event(struct esp *esp)
{
	int write, i;

again:
	write = 0;
	esp_log_event("process event %d phase %x\n",
		      esp->event, esp->sreg & ESP_STAT_PMASK);
	switch (esp->event) {
	case ESP_EVENT_CHECK_PHASE:
		switch (esp->sreg & ESP_STAT_PMASK) {
		case ESP_DOP:
			esp_event(esp, ESP_EVENT_DATA_OUT);
			break;
		case ESP_DIP:
			esp_event(esp, ESP_EVENT_DATA_IN);
			break;
		case ESP_STATP:
			esp_flush_fifo(esp);
			scsi_esp_cmd(esp, ESP_CMD_ICCSEQ);
			esp_event(esp, ESP_EVENT_STATUS);
			esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
			return 1;

		case ESP_MOP:
			esp_event(esp, ESP_EVENT_MSGOUT);
			break;

		case ESP_MIP:
			esp_event(esp, ESP_EVENT_MSGIN);
			break;

		case ESP_CMDP:
			esp_event(esp, ESP_EVENT_CMD_START);
			break;

		default:
			shost_printk(KERN_INFO, esp->host,
				     "Unexpected phase, sreg=%02x\n",
				     esp->sreg);
			esp_schedule_reset(esp);
			return 0;
		}
		goto again;
		break;

	case ESP_EVENT_DATA_IN:
		write = 1;
		/* fallthru */

	case ESP_EVENT_DATA_OUT: {
		struct esp_cmd_entry *ent = esp->active_cmd;
		struct scsi_cmnd *cmd = ent->cmd;
		dma_addr_t dma_addr = esp_cur_dma_addr(ent, cmd);
		unsigned int dma_len = esp_cur_dma_len(ent, cmd);

		if (esp->rev == ESP100)
			scsi_esp_cmd(esp, ESP_CMD_NULL);

		if (write)
			ent->flags |= ESP_CMD_FLAG_WRITE;
		else
			ent->flags &= ~ESP_CMD_FLAG_WRITE;

		if (esp->ops->dma_length_limit)
			dma_len = esp->ops->dma_length_limit(esp, dma_addr,
							     dma_len);
		else
			dma_len = esp_dma_length_limit(esp, dma_addr, dma_len);

		esp->data_dma_len = dma_len;

		if (!dma_len) {
			shost_printk(KERN_ERR, esp->host,
				     "DMA length is zero!\n");
			shost_printk(KERN_ERR, esp->host,
				     "cur adr[%08llx] len[%08x]\n",
				     (unsigned long long)esp_cur_dma_addr(ent, cmd),
				     esp_cur_dma_len(ent, cmd));
			esp_schedule_reset(esp);
			return 0;
		}

		esp_log_datastart("start data addr[%08llx] len[%u] write(%d)\n",
				  (unsigned long long)dma_addr, dma_len, write);

		esp->ops->send_dma_cmd(esp, dma_addr, dma_len, dma_len,
				       write, ESP_CMD_DMA | ESP_CMD_TI);
		esp_event(esp, ESP_EVENT_DATA_DONE);
		break;
	}
	case ESP_EVENT_DATA_DONE: {
		struct esp_cmd_entry *ent = esp->active_cmd;
		struct scsi_cmnd *cmd = ent->cmd;
		int bytes_sent;

		if (esp->ops->dma_error(esp)) {
			shost_printk(KERN_INFO, esp->host,
				     "data done, DMA error, resetting\n");
			esp_schedule_reset(esp);
			return 0;
		}

		if (ent->flags & ESP_CMD_FLAG_WRITE) {
			/* XXX parity errors, etc. XXX */

			esp->ops->dma_drain(esp);
		}
		esp->ops->dma_invalidate(esp);

		if (esp->ireg != ESP_INTR_BSERV) {
			/* We should always see exactly a bus-service
			 * interrupt at the end of a successful transfer.
			 */
			shost_printk(KERN_INFO, esp->host,
				     "data done, not BSERV, resetting\n");
			esp_schedule_reset(esp);
			return 0;
		}

		bytes_sent = esp_data_bytes_sent(esp, ent, cmd);

		esp_log_datadone("data done flgs[%x] sent[%d]\n",
				 ent->flags, bytes_sent);

		if (bytes_sent < 0) {
			/* XXX force sync mode for this target XXX */
			esp_schedule_reset(esp);
			return 0;
		}

		esp_advance_dma(esp, ent, cmd, bytes_sent);
		esp_event(esp, ESP_EVENT_CHECK_PHASE);
		goto again;
	}

	case ESP_EVENT_STATUS: {
		struct esp_cmd_entry *ent = esp->active_cmd;

		if (esp->ireg & ESP_INTR_FDONE) {
			ent->status = esp_read8(ESP_FDATA);
			ent->message = esp_read8(ESP_FDATA);
			scsi_esp_cmd(esp, ESP_CMD_MOK);
		} else if (esp->ireg == ESP_INTR_BSERV) {
			ent->status = esp_read8(ESP_FDATA);
			ent->message = 0xff;
			esp_event(esp, ESP_EVENT_MSGIN);
			return 0;
		}

		if (ent->message != COMMAND_COMPLETE) {
			shost_printk(KERN_INFO, esp->host,
				     "Unexpected message %x in status\n",
				     ent->message);
			esp_schedule_reset(esp);
			return 0;
		}

		esp_event(esp, ESP_EVENT_FREE_BUS);
		esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
		break;
	}
	case ESP_EVENT_FREE_BUS: {
		struct esp_cmd_entry *ent = esp->active_cmd;
		struct scsi_cmnd *cmd = ent->cmd;

		if (ent->message == COMMAND_COMPLETE ||
		    ent->message == DISCONNECT)
			scsi_esp_cmd(esp, ESP_CMD_ESEL);

		if (ent->message == COMMAND_COMPLETE) {
			esp_log_cmddone("Command done status[%x] message[%x]\n",
					ent->status, ent->message);
			if (ent->status == SAM_STAT_TASK_SET_FULL)
				esp_event_queue_full(esp, ent);

			if (ent->status == SAM_STAT_CHECK_CONDITION &&
			    !(ent->flags & ESP_CMD_FLAG_AUTOSENSE)) {
				ent->flags |= ESP_CMD_FLAG_AUTOSENSE;
				esp_autosense(esp, ent);
			} else {
				esp_cmd_is_done(esp, ent, cmd,
						compose_result(ent->status,
							       ent->message,
							       DID_OK));
			}
		} else if (ent->message == DISCONNECT) {
			esp_log_disconnect("Disconnecting tgt[%d] tag[%x:%x]\n",
					   cmd->device->id,
					   ent->tag[0], ent->tag[1]);

			esp->active_cmd = NULL;
			esp_maybe_execute_command(esp);
		} else {
			shost_printk(KERN_INFO, esp->host,
				     "Unexpected message %x in freebus\n",
				     ent->message);
			esp_schedule_reset(esp);
			return 0;
		}
		if (esp->active_cmd)
			esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
		break;
	}
	case ESP_EVENT_MSGOUT: {
		scsi_esp_cmd(esp, ESP_CMD_FLUSH);

		if (esp_debug & ESP_DEBUG_MSGOUT) {
			int i;
			printk("ESP: Sending message [ ");
			for (i = 0; i < esp->msg_out_len; i++)
				printk("%02x ", esp->msg_out[i]);
			printk("]\n");
		}

		if (esp->rev == FASHME) {
			int i;

			/* Always use the fifo.  */
			for (i = 0; i < esp->msg_out_len; i++) {
				esp_write8(esp->msg_out[i], ESP_FDATA);
				esp_write8(0, ESP_FDATA);
			}
			scsi_esp_cmd(esp, ESP_CMD_TI);
		} else {
			if (esp->msg_out_len == 1) {
				esp_write8(esp->msg_out[0], ESP_FDATA);
				scsi_esp_cmd(esp, ESP_CMD_TI);
			} else if (esp->flags & ESP_FLAG_USE_FIFO) {
				for (i = 0; i < esp->msg_out_len; i++)
					esp_write8(esp->msg_out[i], ESP_FDATA);
				scsi_esp_cmd(esp, ESP_CMD_TI);
			} else {
				/* Use DMA. */
				memcpy(esp->command_block,
				       esp->msg_out,
				       esp->msg_out_len);

				esp->ops->send_dma_cmd(esp,
						       esp->command_block_dma,
						       esp->msg_out_len,
						       esp->msg_out_len,
						       0,
						       ESP_CMD_DMA|ESP_CMD_TI);
			}
		}
		esp_event(esp, ESP_EVENT_MSGOUT_DONE);
		break;
	}
	case ESP_EVENT_MSGOUT_DONE:
		if (esp->rev == FASHME) {
			scsi_esp_cmd(esp, ESP_CMD_FLUSH);
		} else {
			if (esp->msg_out_len > 1)
				esp->ops->dma_invalidate(esp);
		}

		if (!(esp->ireg & ESP_INTR_DC)) {
			if (esp->rev != FASHME)
				scsi_esp_cmd(esp, ESP_CMD_NULL);
		}
		esp_event(esp, ESP_EVENT_CHECK_PHASE);
		goto again;
	case ESP_EVENT_MSGIN:
		if (esp->ireg & ESP_INTR_BSERV) {
			if (esp->rev == FASHME) {
				if (!(esp_read8(ESP_STATUS2) &
				      ESP_STAT2_FEMPTY))
					scsi_esp_cmd(esp, ESP_CMD_FLUSH);
			} else {
				scsi_esp_cmd(esp, ESP_CMD_FLUSH);
				if (esp->rev == ESP100)
					scsi_esp_cmd(esp, ESP_CMD_NULL);
			}
			scsi_esp_cmd(esp, ESP_CMD_TI);
			esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
			return 1;
		}
		if (esp->ireg & ESP_INTR_FDONE) {
			u8 val;

			if (esp->rev == FASHME)
				val = esp->fifo[0];
			else
				val = esp_read8(ESP_FDATA);
			esp->msg_in[esp->msg_in_len++] = val;

			esp_log_msgin("Got msgin byte %x\n", val);

			if (!esp_msgin_process(esp))
				esp->msg_in_len = 0;

			if (esp->rev == FASHME)
				scsi_esp_cmd(esp, ESP_CMD_FLUSH);

			scsi_esp_cmd(esp, ESP_CMD_MOK);

			if (esp->event != ESP_EVENT_FREE_BUS)
				esp_event(esp, ESP_EVENT_CHECK_PHASE);
		} else {
			shost_printk(KERN_INFO, esp->host,
				     "MSGIN neither BSERV not FDON, resetting");
			esp_schedule_reset(esp);
			return 0;
		}
		break;
	case ESP_EVENT_CMD_START:
		memcpy(esp->command_block, esp->cmd_bytes_ptr,
		       esp->cmd_bytes_left);
		esp_send_dma_cmd(esp, esp->cmd_bytes_left, 16, ESP_CMD_TI);
		esp_event(esp, ESP_EVENT_CMD_DONE);
		esp->flags |= ESP_FLAG_QUICKIRQ_CHECK;
		break;
	case ESP_EVENT_CMD_DONE:
		esp->ops->dma_invalidate(esp);
		if (esp->ireg & ESP_INTR_BSERV) {
			esp_event(esp, ESP_EVENT_CHECK_PHASE);
			goto again;
		}
		esp_schedule_reset(esp);
		return 0;
		break;

	case ESP_EVENT_RESET:
		scsi_esp_cmd(esp, ESP_CMD_RS);
		break;

	default:
		shost_printk(KERN_INFO, esp->host,
			     "Unexpected event %x, resetting\n", esp->event);
		esp_schedule_reset(esp);
		return 0;
		break;
	}
	return 1;
}

static void esp_reset_cleanup_one(struct esp *esp, struct esp_cmd_entry *ent)
{
	struct scsi_cmnd *cmd = ent->cmd;

	esp_unmap_dma(esp, cmd);
	esp_free_lun_tag(ent, cmd->device->hostdata);
	cmd->result = DID_RESET << 16;

	if (ent->flags & ESP_CMD_FLAG_AUTOSENSE) {
		esp->ops->unmap_single(esp, ent->sense_dma,
				       SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
		ent->sense_ptr = NULL;
	}

	cmd->scsi_done(cmd);
	list_del(&ent->list);
	esp_put_ent(esp, ent);
}

static void esp_clear_hold(struct scsi_device *dev, void *data)
{
	struct esp_lun_data *lp = dev->hostdata;

	BUG_ON(lp->num_tagged);
	lp->hold = 0;
}

static void esp_reset_cleanup(struct esp *esp)
{
	struct esp_cmd_entry *ent, *tmp;
	int i;

	list_for_each_entry_safe(ent, tmp, &esp->queued_cmds, list) {
		struct scsi_cmnd *cmd = ent->cmd;

		list_del(&ent->list);
		cmd->result = DID_RESET << 16;
		cmd->scsi_done(cmd);
		esp_put_ent(esp, ent);
	}

	list_for_each_entry_safe(ent, tmp, &esp->active_cmds, list) {
		if (ent == esp->active_cmd)
			esp->active_cmd = NULL;
		esp_reset_cleanup_one(esp, ent);
	}

	BUG_ON(esp->active_cmd != NULL);

	/* Force renegotiation of sync/wide transfers.  */
	for (i = 0; i < ESP_MAX_TARGET; i++) {
		struct esp_target_data *tp = &esp->target[i];

		tp->esp_period = 0;
		tp->esp_offset = 0;
		tp->esp_config3 &= ~(ESP_CONFIG3_EWIDE |
				     ESP_CONFIG3_FSCSI |
				     ESP_CONFIG3_FAST);
		tp->flags &= ~ESP_TGT_WIDE;
		tp->flags |= ESP_TGT_CHECK_NEGO;

		if (tp->starget)
			__starget_for_each_device(tp->starget, NULL,
						  esp_clear_hold);
	}
	esp->flags &= ~ESP_FLAG_RESETTING;
}

/* Runs under host->lock */
static void __esp_interrupt(struct esp *esp)
{
	int finish_reset, intr_done;
	u8 phase;

       /*
	* Once INTRPT is read STATUS and SSTEP are cleared.
	*/
	esp->sreg = esp_read8(ESP_STATUS);
	esp->seqreg = esp_read8(ESP_SSTEP);
	esp->ireg = esp_read8(ESP_INTRPT);

	if (esp->flags & ESP_FLAG_RESETTING) {
		finish_reset = 1;
	} else {
		if (esp_check_gross_error(esp))
			return;

		finish_reset = esp_check_spur_intr(esp);
		if (finish_reset < 0)
			return;
	}

	if (esp->ireg & ESP_INTR_SR)
		finish_reset = 1;

	if (finish_reset) {
		esp_reset_cleanup(esp);
		if (esp->eh_reset) {
			complete(esp->eh_reset);
			esp->eh_reset = NULL;
		}
		return;
	}

	phase = (esp->sreg & ESP_STAT_PMASK);
	if (esp->rev == FASHME) {
		if (((phase != ESP_DIP && phase != ESP_DOP) &&
		     esp->select_state == ESP_SELECT_NONE &&
		     esp->event != ESP_EVENT_STATUS &&
		     esp->event != ESP_EVENT_DATA_DONE) ||
		    (esp->ireg & ESP_INTR_RSEL)) {
			esp->sreg2 = esp_read8(ESP_STATUS2);
			if (!(esp->sreg2 & ESP_STAT2_FEMPTY) ||
			    (esp->sreg2 & ESP_STAT2_F1BYTE))
				hme_read_fifo(esp);
		}
	}

	esp_log_intr("intr sreg[%02x] seqreg[%02x] "
		     "sreg2[%02x] ireg[%02x]\n",
		     esp->sreg, esp->seqreg, esp->sreg2, esp->ireg);

	intr_done = 0;

	if (esp->ireg & (ESP_INTR_S | ESP_INTR_SATN | ESP_INTR_IC)) {
		shost_printk(KERN_INFO, esp->host,
			     "unexpected IREG %02x\n", esp->ireg);
		if (esp->ireg & ESP_INTR_IC)
			esp_dump_cmd_log(esp);

		esp_schedule_reset(esp);
	} else {
		if (!(esp->ireg & ESP_INTR_RSEL)) {
			/* Some combination of FDONE, BSERV, DC.  */
			if (esp->select_state != ESP_SELECT_NONE)
				intr_done = esp_finish_select(esp);
		} else if (esp->ireg & ESP_INTR_RSEL) {
			if (esp->active_cmd)
				(void) esp_finish_select(esp);
			intr_done = esp_reconnect(esp);
		}
	}
	while (!intr_done)
		intr_done = esp_process_event(esp);
}

irqreturn_t scsi_esp_intr(int irq, void *dev_id)
{
	struct esp *esp = dev_id;
	unsigned long flags;
	irqreturn_t ret;

	spin_lock_irqsave(esp->host->host_lock, flags);
	ret = IRQ_NONE;
	if (esp->ops->irq_pending(esp)) {
		ret = IRQ_HANDLED;
		for (;;) {
			int i;

			__esp_interrupt(esp);
			if (!(esp->flags & ESP_FLAG_QUICKIRQ_CHECK))
				break;
			esp->flags &= ~ESP_FLAG_QUICKIRQ_CHECK;

			for (i = 0; i < ESP_QUICKIRQ_LIMIT; i++) {
				if (esp->ops->irq_pending(esp))
					break;
			}
			if (i == ESP_QUICKIRQ_LIMIT)
				break;
		}
	}
	spin_unlock_irqrestore(esp->host->host_lock, flags);

	return ret;
}
EXPORT_SYMBOL(scsi_esp_intr);

static void esp_get_revision(struct esp *esp)
{
	u8 val;

	esp->config1 = (ESP_CONFIG1_PENABLE | (esp->scsi_id & 7));
	if (esp->config2 == 0) {
		esp->config2 = (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY);
		esp_write8(esp->config2, ESP_CFG2);

		val = esp_read8(ESP_CFG2);
		val &= ~ESP_CONFIG2_MAGIC;

		esp->config2 = 0;
		if (val != (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY)) {
			/*
			 * If what we write to cfg2 does not come back,
			 * cfg2 is not implemented.
			 * Therefore this must be a plain esp100.
			 */
			esp->rev = ESP100;
			return;
		}
	}

	esp_set_all_config3(esp, 5);
	esp->prev_cfg3 = 5;
	esp_write8(esp->config2, ESP_CFG2);
	esp_write8(0, ESP_CFG3);
	esp_write8(esp->prev_cfg3, ESP_CFG3);

	val = esp_read8(ESP_CFG3);
	if (val != 5) {
		/* The cfg2 register is implemented, however
		 * cfg3 is not, must be esp100a.
		 */
		esp->rev = ESP100A;
	} else {
		esp_set_all_config3(esp, 0);
		esp->prev_cfg3 = 0;
		esp_write8(esp->prev_cfg3, ESP_CFG3);

		/* All of cfg{1,2,3} implemented, must be one of
		 * the fas variants, figure out which one.
		 */
		if (esp->cfact == 0 || esp->cfact > ESP_CCF_F5) {
			esp->rev = FAST;
			esp->sync_defp = SYNC_DEFP_FAST;
		} else {
			esp->rev = ESP236;
		}
	}
}

static void esp_init_swstate(struct esp *esp)
{
	int i;

	INIT_LIST_HEAD(&esp->queued_cmds);
	INIT_LIST_HEAD(&esp->active_cmds);
	INIT_LIST_HEAD(&esp->esp_cmd_pool);

	/* Start with a clear state, domain validation (via ->slave_configure,
	 * spi_dv_device()) will attempt to enable SYNC, WIDE, and tagged
	 * commands.
	 */
	for (i = 0 ; i < ESP_MAX_TARGET; i++) {
		esp->target[i].flags = 0;
		esp->target[i].nego_goal_period = 0;
		esp->target[i].nego_goal_offset = 0;
		esp->target[i].nego_goal_width = 0;
		esp->target[i].nego_goal_tags = 0;
	}
}

/* This places the ESP into a known state at boot time. */
static void esp_bootup_reset(struct esp *esp)
{
	u8 val;

	/* Reset the DMA */
	esp->ops->reset_dma(esp);

	/* Reset the ESP */
	esp_reset_esp(esp);

	/* Reset the SCSI bus, but tell ESP not to generate an irq */
	val = esp_read8(ESP_CFG1);
	val |= ESP_CONFIG1_SRRDISAB;
	esp_write8(val, ESP_CFG1);

	scsi_esp_cmd(esp, ESP_CMD_RS);
	udelay(400);

	esp_write8(esp->config1, ESP_CFG1);

	/* Eat any bitrot in the chip and we are done... */
	esp_read8(ESP_INTRPT);
}

static void esp_set_clock_params(struct esp *esp)
{
	int fhz;
	u8 ccf;

	/* This is getting messy but it has to be done correctly or else
	 * you get weird behavior all over the place.  We are trying to
	 * basically figure out three pieces of information.
	 *
	 * a) Clock Conversion Factor
	 *
	 *    This is a representation of the input crystal clock frequency
	 *    going into the ESP on this machine.  Any operation whose timing
	 *    is longer than 400ns depends on this value being correct.  For
	 *    example, you'll get blips for arbitration/selection during high
	 *    load or with multiple targets if this is not set correctly.
	 *
	 * b) Selection Time-Out
	 *
	 *    The ESP isn't very bright and will arbitrate for the bus and try
	 *    to select a target forever if you let it.  This value tells the
	 *    ESP when it has taken too long to negotiate and that it should
	 *    interrupt the CPU so we can see what happened.  The value is
	 *    computed as follows (from NCR/Symbios chip docs).
	 *
	 *          (Time Out Period) *  (Input Clock)
	 *    STO = ----------------------------------
	 *          (8192) * (Clock Conversion Factor)
	 *
	 *    We use a time out period of 250ms (ESP_BUS_TIMEOUT).
	 *
	 * c) Imperical constants for synchronous offset and transfer period
         *    register values
	 *
	 *    This entails the smallest and largest sync period we could ever
	 *    handle on this ESP.
	 */
	fhz = esp->cfreq;

	ccf = ((fhz / 1000000) + 4) / 5;
	if (ccf == 1)
		ccf = 2;

	/* If we can't find anything reasonable, just assume 20MHZ.
	 * This is the clock frequency of the older sun4c's where I've
	 * been unable to find the clock-frequency PROM property.  All
	 * other machines provide useful values it seems.
	 */
	if (fhz <= 5000000 || ccf < 1 || ccf > 8) {
		fhz = 20000000;
		ccf = 4;
	}

	esp->cfact = (ccf == 8 ? 0 : ccf);
	esp->cfreq = fhz;
	esp->ccycle = ESP_HZ_TO_CYCLE(fhz);
	esp->ctick = ESP_TICK(ccf, esp->ccycle);
	esp->neg_defp = ESP_NEG_DEFP(fhz, ccf);
	esp->sync_defp = SYNC_DEFP_SLOW;
}

static const char *esp_chip_names[] = {
	"ESP100",
	"ESP100A",
	"ESP236",
	"FAS236",
	"FAS100A",
	"FAST",
	"FASHME",
	"AM53C974",
};

static struct scsi_transport_template *esp_transport_template;

int scsi_esp_register(struct esp *esp, struct device *dev)
{
	static int instance;
	int err;

	if (!esp->num_tags)
		esp->num_tags = ESP_DEFAULT_TAGS;
	else if (esp->num_tags >= ESP_MAX_TAG)
		esp->num_tags = ESP_MAX_TAG - 1;
	esp->host->transportt = esp_transport_template;
	esp->host->max_lun = ESP_MAX_LUN;
	esp->host->cmd_per_lun = 2;
	esp->host->unique_id = instance;

	esp_set_clock_params(esp);

	esp_get_revision(esp);

	esp_init_swstate(esp);

	esp_bootup_reset(esp);

	dev_printk(KERN_INFO, dev, "esp%u: regs[%1p:%1p] irq[%u]\n",
		   esp->host->unique_id, esp->regs, esp->dma_regs,
		   esp->host->irq);
	dev_printk(KERN_INFO, dev,
		   "esp%u: is a %s, %u MHz (ccf=%u), SCSI ID %u\n",
		   esp->host->unique_id, esp_chip_names[esp->rev],
		   esp->cfreq / 1000000, esp->cfact, esp->scsi_id);

	/* Let the SCSI bus reset settle. */
	ssleep(esp_bus_reset_settle);

	err = scsi_add_host(esp->host, dev);
	if (err)
		return err;

	instance++;

	scsi_scan_host(esp->host);

	return 0;
}
EXPORT_SYMBOL(scsi_esp_register);

void scsi_esp_unregister(struct esp *esp)
{
	scsi_remove_host(esp->host);
}
EXPORT_SYMBOL(scsi_esp_unregister);

static int esp_target_alloc(struct scsi_target *starget)
{
	struct esp *esp = shost_priv(dev_to_shost(&starget->dev));
	struct esp_target_data *tp = &esp->target[starget->id];

	tp->starget = starget;

	return 0;
}

static void esp_target_destroy(struct scsi_target *starget)
{
	struct esp *esp = shost_priv(dev_to_shost(&starget->dev));
	struct esp_target_data *tp = &esp->target[starget->id];

	tp->starget = NULL;
}

static int esp_slave_alloc(struct scsi_device *dev)
{
	struct esp *esp = shost_priv(dev->host);
	struct esp_target_data *tp = &esp->target[dev->id];
	struct esp_lun_data *lp;

	lp = kzalloc(sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;
	dev->hostdata = lp;

	spi_min_period(tp->starget) = esp->min_period;
	spi_max_offset(tp->starget) = 15;

	if (esp->flags & ESP_FLAG_WIDE_CAPABLE)
		spi_max_width(tp->starget) = 1;
	else
		spi_max_width(tp->starget) = 0;

	return 0;
}

static int esp_slave_configure(struct scsi_device *dev)
{
	struct esp *esp = shost_priv(dev->host);
	struct esp_target_data *tp = &esp->target[dev->id];

	if (dev->tagged_supported)
		scsi_change_queue_depth(dev, esp->num_tags);

	tp->flags |= ESP_TGT_DISCONNECT;

	if (!spi_initial_dv(dev->sdev_target))
		spi_dv_device(dev);

	return 0;
}

static void esp_slave_destroy(struct scsi_device *dev)
{
	struct esp_lun_data *lp = dev->hostdata;

	kfree(lp);
	dev->hostdata = NULL;
}

static int esp_eh_abort_handler(struct scsi_cmnd *cmd)
{
	struct esp *esp = shost_priv(cmd->device->host);
	struct esp_cmd_entry *ent, *tmp;
	struct completion eh_done;
	unsigned long flags;

	/* XXX This helps a lot with debugging but might be a bit
	 * XXX much for the final driver.
	 */
	spin_lock_irqsave(esp->host->host_lock, flags);
	shost_printk(KERN_ERR, esp->host, "Aborting command [%p:%02x]\n",
		     cmd, cmd->cmnd[0]);
	ent = esp->active_cmd;
	if (ent)
		shost_printk(KERN_ERR, esp->host,
			     "Current command [%p:%02x]\n",
			     ent->cmd, ent->cmd->cmnd[0]);
	list_for_each_entry(ent, &esp->queued_cmds, list) {
		shost_printk(KERN_ERR, esp->host, "Queued command [%p:%02x]\n",
			     ent->cmd, ent->cmd->cmnd[0]);
	}
	list_for_each_entry(ent, &esp->active_cmds, list) {
		shost_printk(KERN_ERR, esp->host, " Active command [%p:%02x]\n",
			     ent->cmd, ent->cmd->cmnd[0]);
	}
	esp_dump_cmd_log(esp);
	spin_unlock_irqrestore(esp->host->host_lock, flags);

	spin_lock_irqsave(esp->host->host_lock, flags);

	ent = NULL;
	list_for_each_entry(tmp, &esp->queued_cmds, list) {
		if (tmp->cmd == cmd) {
			ent = tmp;
			break;
		}
	}

	if (ent) {
		/* Easiest case, we didn't even issue the command
		 * yet so it is trivial to abort.
		 */
		list_del(&ent->list);

		cmd->result = DID_ABORT << 16;
		cmd->scsi_done(cmd);

		esp_put_ent(esp, ent);

		goto out_success;
	}

	init_completion(&eh_done);

	ent = esp->active_cmd;
	if (ent && ent->cmd == cmd) {
		/* Command is the currently active command on
		 * the bus.  If we already have an output message
		 * pending, no dice.
		 */
		if (esp->msg_out_len)
			goto out_failure;

		/* Send out an abort, encouraging the target to
		 * go to MSGOUT phase by asserting ATN.
		 */
		esp->msg_out[0] = ABORT_TASK_SET;
		esp->msg_out_len = 1;
		ent->eh_done = &eh_done;

		scsi_esp_cmd(esp, ESP_CMD_SATN);
	} else {
		/* The command is disconnected.  This is not easy to
		 * abort.  For now we fail and let the scsi error
		 * handling layer go try a scsi bus reset or host
		 * reset.
		 *
		 * What we could do is put together a scsi command
		 * solely for the purpose of sending an abort message
		 * to the target.  Coming up with all the code to
		 * cook up scsi commands, special case them everywhere,
		 * etc. is for questionable gain and it would be better
		 * if the generic scsi error handling layer could do at
		 * least some of that for us.
		 *
		 * Anyways this is an area for potential future improvement
		 * in this driver.
		 */
		goto out_failure;
	}

	spin_unlock_irqrestore(esp->host->host_lock, flags);

	if (!wait_for_completion_timeout(&eh_done, 5 * HZ)) {
		spin_lock_irqsave(esp->host->host_lock, flags);
		ent->eh_done = NULL;
		spin_unlock_irqrestore(esp->host->host_lock, flags);

		return FAILED;
	}

	return SUCCESS;

out_success:
	spin_unlock_irqrestore(esp->host->host_lock, flags);
	return SUCCESS;

out_failure:
	/* XXX This might be a good location to set ESP_TGT_BROKEN
	 * XXX since we know which target/lun in particular is
	 * XXX causing trouble.
	 */
	spin_unlock_irqrestore(esp->host->host_lock, flags);
	return FAILED;
}

static int esp_eh_bus_reset_handler(struct scsi_cmnd *cmd)
{
	struct esp *esp = shost_priv(cmd->device->host);
	struct completion eh_reset;
	unsigned long flags;

	init_completion(&eh_reset);

	spin_lock_irqsave(esp->host->host_lock, flags);

	esp->eh_reset = &eh_reset;

	/* XXX This is too simple... We should add lots of
	 * XXX checks here so that if we find that the chip is
	 * XXX very wedged we return failure immediately so
	 * XXX that we can perform a full chip reset.
	 */
	esp->flags |= ESP_FLAG_RESETTING;
	scsi_esp_cmd(esp, ESP_CMD_RS);

	spin_unlock_irqrestore(esp->host->host_lock, flags);

	ssleep(esp_bus_reset_settle);

	if (!wait_for_completion_timeout(&eh_reset, 5 * HZ)) {
		spin_lock_irqsave(esp->host->host_lock, flags);
		esp->eh_reset = NULL;
		spin_unlock_irqrestore(esp->host->host_lock, flags);

		return FAILED;
	}

	return SUCCESS;
}

/* All bets are off, reset the entire device.  */
static int esp_eh_host_reset_handler(struct scsi_cmnd *cmd)
{
	struct esp *esp = shost_priv(cmd->device->host);
	unsigned long flags;

	spin_lock_irqsave(esp->host->host_lock, flags);
	esp_bootup_reset(esp);
	esp_reset_cleanup(esp);
	spin_unlock_irqrestore(esp->host->host_lock, flags);

	ssleep(esp_bus_reset_settle);

	return SUCCESS;
}

static const char *esp_info(struct Scsi_Host *host)
{
	return "esp";
}

struct scsi_host_template scsi_esp_template = {
	.module			= THIS_MODULE,
	.name			= "esp",
	.info			= esp_info,
	.queuecommand		= esp_queuecommand,
	.target_alloc		= esp_target_alloc,
	.target_destroy		= esp_target_destroy,
	.slave_alloc		= esp_slave_alloc,
	.slave_configure	= esp_slave_configure,
	.slave_destroy		= esp_slave_destroy,
	.eh_abort_handler	= esp_eh_abort_handler,
	.eh_bus_reset_handler	= esp_eh_bus_reset_handler,
	.eh_host_reset_handler	= esp_eh_host_reset_handler,
	.can_queue		= 7,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.use_clustering		= ENABLE_CLUSTERING,
	.max_sectors		= 0xffff,
	.skip_settle_delay	= 1,
	.use_blk_tags		= 1,
};
EXPORT_SYMBOL(scsi_esp_template);

static void esp_get_signalling(struct Scsi_Host *host)
{
	struct esp *esp = shost_priv(host);
	enum spi_signal_type type;

	if (esp->flags & ESP_FLAG_DIFFERENTIAL)
		type = SPI_SIGNAL_HVD;
	else
		type = SPI_SIGNAL_SE;

	spi_signalling(host) = type;
}

static void esp_set_offset(struct scsi_target *target, int offset)
{
	struct Scsi_Host *host = dev_to_shost(target->dev.parent);
	struct esp *esp = shost_priv(host);
	struct esp_target_data *tp = &esp->target[target->id];

	if (esp->flags & ESP_FLAG_DISABLE_SYNC)
		tp->nego_goal_offset = 0;
	else
		tp->nego_goal_offset = offset;
	tp->flags |= ESP_TGT_CHECK_NEGO;
}

static void esp_set_period(struct scsi_target *target, int period)
{
	struct Scsi_Host *host = dev_to_shost(target->dev.parent);
	struct esp *esp = shost_priv(host);
	struct esp_target_data *tp = &esp->target[target->id];

	tp->nego_goal_period = period;
	tp->flags |= ESP_TGT_CHECK_NEGO;
}

static void esp_set_width(struct scsi_target *target, int width)
{
	struct Scsi_Host *host = dev_to_shost(target->dev.parent);
	struct esp *esp = shost_priv(host);
	struct esp_target_data *tp = &esp->target[target->id];

	tp->nego_goal_width = (width ? 1 : 0);
	tp->flags |= ESP_TGT_CHECK_NEGO;
}

static struct spi_function_template esp_transport_ops = {
	.set_offset		= esp_set_offset,
	.show_offset		= 1,
	.set_period		= esp_set_period,
	.show_period		= 1,
	.set_width		= esp_set_width,
	.show_width		= 1,
	.get_signalling		= esp_get_signalling,
};

static int __init esp_init(void)
{
	BUILD_BUG_ON(sizeof(struct scsi_pointer) <
		     sizeof(struct esp_cmd_priv));

	esp_transport_template = spi_attach_transport(&esp_transport_ops);
	if (!esp_transport_template)
		return -ENODEV;

	return 0;
}

static void __exit esp_exit(void)
{
	spi_release_transport(esp_transport_template);
}

MODULE_DESCRIPTION("ESP SCSI driver core");
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_param(esp_bus_reset_settle, int, 0);
MODULE_PARM_DESC(esp_bus_reset_settle,
		 "ESP scsi bus reset delay in seconds");

module_param(esp_debug, int, 0);
MODULE_PARM_DESC(esp_debug,
"ESP bitmapped debugging message enable value:\n"
"	0x00000001	Log interrupt events\n"
"	0x00000002	Log scsi commands\n"
"	0x00000004	Log resets\n"
"	0x00000008	Log message in events\n"
"	0x00000010	Log message out events\n"
"	0x00000020	Log command completion\n"
"	0x00000040	Log disconnects\n"
"	0x00000080	Log data start\n"
"	0x00000100	Log data done\n"
"	0x00000200	Log reconnects\n"
"	0x00000400	Log auto-sense data\n"
);

module_init(esp_init);
module_exit(esp_exit);
