// SPDX-License-Identifier: GPL-2.0-only
/*
 * SCSI low-level driver for the 53c94 SCSI bus adaptor found
 * on Power Macintosh computers, controlling the external SCSI chain.
 * We assume the 53c94 is connected to a DBDMA (descriptor-based DMA)
 * controller.
 *
 * Paul Mackerras, August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pgtable.h>
#include <asm/dbdma.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/macio.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "mac53c94.h"

enum fsc_phase {
	idle,
	selecting,
	dataing,
	completing,
	busfreeing,
};

struct fsc_state {
	struct	mac53c94_regs __iomem *regs;
	int	intr;
	struct	dbdma_regs __iomem *dma;
	int	dmaintr;
	int	clk_freq;
	struct	Scsi_Host *host;
	struct scsi_cmnd *request_q;
	struct scsi_cmnd *request_qtail;
	struct scsi_cmnd *current_req;		/* req we're currently working on */
	enum fsc_phase phase;		/* what we're currently trying to do */
	struct dbdma_cmd *dma_cmds;	/* space for dbdma commands, aligned */
	void	*dma_cmd_space;
	struct	pci_dev *pdev;
	dma_addr_t dma_addr;
	struct macio_dev *mdev;
};

static void mac53c94_init(struct fsc_state *);
static void mac53c94_start(struct fsc_state *);
static void mac53c94_interrupt(int, void *);
static irqreturn_t do_mac53c94_interrupt(int, void *);
static void cmd_done(struct fsc_state *, int result);
static void set_dma_cmds(struct fsc_state *, struct scsi_cmnd *);


static int mac53c94_queue_lck(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct fsc_state *state;

#if 0
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		int i;
		printk(KERN_DEBUG "mac53c94_queue %p: command is", cmd);
		for (i = 0; i < cmd->cmd_len; ++i)
			printk(KERN_CONT " %.2x", cmd->cmnd[i]);
		printk(KERN_CONT "\n");
		printk(KERN_DEBUG "use_sg=%d request_bufflen=%d request_buffer=%p\n",
		       scsi_sg_count(cmd), scsi_bufflen(cmd), scsi_sglist(cmd));
	}
#endif

	cmd->scsi_done = done;
	cmd->host_scribble = NULL;

	state = (struct fsc_state *) cmd->device->host->hostdata;

	if (state->request_q == NULL)
		state->request_q = cmd;
	else
		state->request_qtail->host_scribble = (void *) cmd;
	state->request_qtail = cmd;

	if (state->phase == idle)
		mac53c94_start(state);

	return 0;
}

static DEF_SCSI_QCMD(mac53c94_queue)

static int mac53c94_host_reset(struct scsi_cmnd *cmd)
{
	struct fsc_state *state = (struct fsc_state *) cmd->device->host->hostdata;
	struct mac53c94_regs __iomem *regs = state->regs;
	struct dbdma_regs __iomem *dma = state->dma;
	unsigned long flags;

	spin_lock_irqsave(cmd->device->host->host_lock, flags);

	writel((RUN|PAUSE|FLUSH|WAKE) << 16, &dma->control);
	writeb(CMD_SCSI_RESET, &regs->command);	/* assert RST */
	udelay(100);			/* leave it on for a while (>= 25us) */
	writeb(CMD_RESET, &regs->command);
	udelay(20);
	mac53c94_init(state);
	writeb(CMD_NOP, &regs->command);

	spin_unlock_irqrestore(cmd->device->host->host_lock, flags);
	return SUCCESS;
}

static void mac53c94_init(struct fsc_state *state)
{
	struct mac53c94_regs __iomem *regs = state->regs;
	struct dbdma_regs __iomem *dma = state->dma;
	int x;

	writeb(state->host->this_id | CF1_PAR_ENABLE, &regs->config1);
	writeb(TIMO_VAL(250), &regs->sel_timeout);	/* 250ms */
	writeb(CLKF_VAL(state->clk_freq), &regs->clk_factor);
	writeb(CF2_FEATURE_EN, &regs->config2);
	writeb(0, &regs->config3);
	writeb(0, &regs->sync_period);
	writeb(0, &regs->sync_offset);
	x = readb(&regs->interrupt);
	writel((RUN|PAUSE|FLUSH|WAKE) << 16, &dma->control);
}

/*
 * Start the next command for a 53C94.
 * Should be called with interrupts disabled.
 */
static void mac53c94_start(struct fsc_state *state)
{
	struct scsi_cmnd *cmd;
	struct mac53c94_regs __iomem *regs = state->regs;
	int i;

	if (state->phase != idle || state->current_req != NULL)
		panic("inappropriate mac53c94_start (state=%p)", state);
	if (state->request_q == NULL)
		return;
	state->current_req = cmd = state->request_q;
	state->request_q = (struct scsi_cmnd *) cmd->host_scribble;

	/* Off we go */
	writeb(0, &regs->count_lo);
	writeb(0, &regs->count_mid);
	writeb(0, &regs->count_hi);
	writeb(CMD_NOP + CMD_DMA_MODE, &regs->command);
	udelay(1);
	writeb(CMD_FLUSH, &regs->command);
	udelay(1);
	writeb(cmd->device->id, &regs->dest_id);
	writeb(0, &regs->sync_period);
	writeb(0, &regs->sync_offset);

	/* load the command into the FIFO */
	for (i = 0; i < cmd->cmd_len; ++i)
		writeb(cmd->cmnd[i], &regs->fifo);

	/* do select without ATN XXX */
	writeb(CMD_SELECT, &regs->command);
	state->phase = selecting;

	set_dma_cmds(state, cmd);
}

static irqreturn_t do_mac53c94_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	struct Scsi_Host *dev = ((struct fsc_state *) dev_id)->current_req->device->host;
	
	spin_lock_irqsave(dev->host_lock, flags);
	mac53c94_interrupt(irq, dev_id);
	spin_unlock_irqrestore(dev->host_lock, flags);
	return IRQ_HANDLED;
}

static void mac53c94_interrupt(int irq, void *dev_id)
{
	struct fsc_state *state = (struct fsc_state *) dev_id;
	struct mac53c94_regs __iomem *regs = state->regs;
	struct dbdma_regs __iomem *dma = state->dma;
	struct scsi_cmnd *cmd = state->current_req;
	int nb, stat, seq, intr;
	static int mac53c94_errors;

	/*
	 * Apparently, reading the interrupt register unlatches
	 * the status and sequence step registers.
	 */
	seq = readb(&regs->seqstep);
	stat = readb(&regs->status);
	intr = readb(&regs->interrupt);

#if 0
	printk(KERN_DEBUG "mac53c94_intr, intr=%x stat=%x seq=%x phase=%d\n",
	       intr, stat, seq, state->phase);
#endif

	if (intr & INTR_RESET) {
		/* SCSI bus was reset */
		printk(KERN_INFO "external SCSI bus reset detected\n");
		writeb(CMD_NOP, &regs->command);
		writel(RUN << 16, &dma->control);	/* stop dma */
		cmd_done(state, DID_RESET << 16);
		return;
	}
	if (intr & INTR_ILL_CMD) {
		printk(KERN_ERR "53c94: invalid cmd, intr=%x stat=%x seq=%x phase=%d\n",
		       intr, stat, seq, state->phase);
		cmd_done(state, DID_ERROR << 16);
		return;
	}
	if (stat & STAT_ERROR) {
#if 0
		/* XXX these seem to be harmless? */
		printk("53c94: bad error, intr=%x stat=%x seq=%x phase=%d\n",
		       intr, stat, seq, state->phase);
#endif
		++mac53c94_errors;
		writeb(CMD_NOP + CMD_DMA_MODE, &regs->command);
	}
	if (cmd == 0) {
		printk(KERN_DEBUG "53c94: interrupt with no command active?\n");
		return;
	}
	if (stat & STAT_PARITY) {
		printk(KERN_ERR "mac53c94: parity error\n");
		cmd_done(state, DID_PARITY << 16);
		return;
	}
	switch (state->phase) {
	case selecting:
		if (intr & INTR_DISCONNECT) {
			/* selection timed out */
			cmd_done(state, DID_BAD_TARGET << 16);
			return;
		}
		if (intr != INTR_BUS_SERV + INTR_DONE) {
			printk(KERN_DEBUG "got intr %x during selection\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		if ((seq & SS_MASK) != SS_DONE) {
			printk(KERN_DEBUG "seq step %x after command\n", seq);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		writeb(CMD_NOP, &regs->command);
		/* set DMA controller going if any data to transfer */
		if ((stat & (STAT_MSG|STAT_CD)) == 0
		    && (scsi_sg_count(cmd) > 0 || scsi_bufflen(cmd))) {
			nb = cmd->SCp.this_residual;
			if (nb > 0xfff0)
				nb = 0xfff0;
			cmd->SCp.this_residual -= nb;
			writeb(nb, &regs->count_lo);
			writeb(nb >> 8, &regs->count_mid);
			writeb(CMD_DMA_MODE + CMD_NOP, &regs->command);
			writel(virt_to_phys(state->dma_cmds), &dma->cmdptr);
			writel((RUN << 16) | RUN, &dma->control);
			writeb(CMD_DMA_MODE + CMD_XFER_DATA, &regs->command);
			state->phase = dataing;
			break;
		} else if ((stat & STAT_PHASE) == STAT_CD + STAT_IO) {
			/* up to status phase already */
			writeb(CMD_I_COMPLETE, &regs->command);
			state->phase = completing;
		} else {
			printk(KERN_DEBUG "in unexpected phase %x after cmd\n",
			       stat & STAT_PHASE);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		break;

	case dataing:
		if (intr != INTR_BUS_SERV) {
			printk(KERN_DEBUG "got intr %x before status\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		if (cmd->SCp.this_residual != 0
		    && (stat & (STAT_MSG|STAT_CD)) == 0) {
			/* Set up the count regs to transfer more */
			nb = cmd->SCp.this_residual;
			if (nb > 0xfff0)
				nb = 0xfff0;
			cmd->SCp.this_residual -= nb;
			writeb(nb, &regs->count_lo);
			writeb(nb >> 8, &regs->count_mid);
			writeb(CMD_DMA_MODE + CMD_NOP, &regs->command);
			writeb(CMD_DMA_MODE + CMD_XFER_DATA, &regs->command);
			break;
		}
		if ((stat & STAT_PHASE) != STAT_CD + STAT_IO) {
			printk(KERN_DEBUG "intr %x before data xfer complete\n", intr);
		}
		writel(RUN << 16, &dma->control);	/* stop dma */
		scsi_dma_unmap(cmd);
		/* should check dma status */
		writeb(CMD_I_COMPLETE, &regs->command);
		state->phase = completing;
		break;
	case completing:
		if (intr != INTR_DONE) {
			printk(KERN_DEBUG "got intr %x on completion\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		cmd->SCp.Status = readb(&regs->fifo);
		cmd->SCp.Message = readb(&regs->fifo);
		writeb(CMD_ACCEPT_MSG, &regs->command);
		state->phase = busfreeing;
		break;
	case busfreeing:
		if (intr != INTR_DISCONNECT) {
			printk(KERN_DEBUG "got intr %x when expected disconnect\n", intr);
		}
		cmd_done(state, (DID_OK << 16) + (cmd->SCp.Message << 8)
			 + cmd->SCp.Status);
		break;
	default:
		printk(KERN_DEBUG "don't know about phase %d\n", state->phase);
	}
}

static void cmd_done(struct fsc_state *state, int result)
{
	struct scsi_cmnd *cmd;

	cmd = state->current_req;
	if (cmd != 0) {
		cmd->result = result;
		(*cmd->scsi_done)(cmd);
		state->current_req = NULL;
	}
	state->phase = idle;
	mac53c94_start(state);
}

/*
 * Set up DMA commands for transferring data.
 */
static void set_dma_cmds(struct fsc_state *state, struct scsi_cmnd *cmd)
{
	int i, dma_cmd, total, nseg;
	struct scatterlist *scl;
	struct dbdma_cmd *dcmds;
	dma_addr_t dma_addr;
	u32 dma_len;

	nseg = scsi_dma_map(cmd);
	BUG_ON(nseg < 0);
	if (!nseg)
		return;

	dma_cmd = cmd->sc_data_direction == DMA_TO_DEVICE ?
			OUTPUT_MORE : INPUT_MORE;
	dcmds = state->dma_cmds;
	total = 0;

	scsi_for_each_sg(cmd, scl, nseg, i) {
		dma_addr = sg_dma_address(scl);
		dma_len = sg_dma_len(scl);
		if (dma_len > 0xffff)
			panic("mac53c94: scatterlist element >= 64k");
		total += dma_len;
		dcmds->req_count = cpu_to_le16(dma_len);
		dcmds->command = cpu_to_le16(dma_cmd);
		dcmds->phy_addr = cpu_to_le32(dma_addr);
		dcmds->xfer_status = 0;
		++dcmds;
	}

	dma_cmd += OUTPUT_LAST - OUTPUT_MORE;
	dcmds[-1].command = cpu_to_le16(dma_cmd);
	dcmds->command = cpu_to_le16(DBDMA_STOP);
	cmd->SCp.this_residual = total;
}

static struct scsi_host_template mac53c94_template = {
	.proc_name	= "53c94",
	.name		= "53C94",
	.queuecommand	= mac53c94_queue,
	.eh_host_reset_handler = mac53c94_host_reset,
	.can_queue	= 1,
	.this_id	= 7,
	.sg_tablesize	= SG_ALL,
	.max_segment_size = 65535,
};

static int mac53c94_probe(struct macio_dev *mdev, const struct of_device_id *match)
{
	struct device_node *node = macio_get_of_node(mdev);
	struct pci_dev *pdev = macio_get_pci_dev(mdev);
	struct fsc_state *state;
	struct Scsi_Host *host;
	void *dma_cmd_space;
	const unsigned char *clkprop;
	int proplen, rc = -ENODEV;

	if (macio_resource_count(mdev) != 2 || macio_irq_count(mdev) != 2) {
		printk(KERN_ERR "mac53c94: expected 2 addrs and intrs"
		       " (got %d/%d)\n",
		       macio_resource_count(mdev), macio_irq_count(mdev));
		return -ENODEV;
	}

	if (macio_request_resources(mdev, "mac53c94") != 0) {
       		printk(KERN_ERR "mac53c94: unable to request memory resources");
		return -EBUSY;
	}

       	host = scsi_host_alloc(&mac53c94_template, sizeof(struct fsc_state));
	if (host == NULL) {
		printk(KERN_ERR "mac53c94: couldn't register host");
		rc = -ENOMEM;
		goto out_release;
	}

	state = (struct fsc_state *) host->hostdata;
	macio_set_drvdata(mdev, state);
	state->host = host;
	state->pdev = pdev;
	state->mdev = mdev;

	state->regs = (struct mac53c94_regs __iomem *)
		ioremap(macio_resource_start(mdev, 0), 0x1000);
	state->intr = macio_irq(mdev, 0);
	state->dma = (struct dbdma_regs __iomem *)
		ioremap(macio_resource_start(mdev, 1), 0x1000);
	state->dmaintr = macio_irq(mdev, 1);
	if (state->regs == NULL || state->dma == NULL) {
		printk(KERN_ERR "mac53c94: ioremap failed for %pOF\n", node);
		goto out_free;
	}

	clkprop = of_get_property(node, "clock-frequency", &proplen);
       	if (clkprop == NULL || proplen != sizeof(int)) {
       		printk(KERN_ERR "%pOF: can't get clock frequency, "
       		       "assuming 25MHz\n", node);
       		state->clk_freq = 25000000;
       	} else
       		state->clk_freq = *(int *)clkprop;

       	/* Space for dma command list: +1 for stop command,
       	 * +1 to allow for aligning.
	 * XXX FIXME: Use DMA consistent routines
	 */
       	dma_cmd_space = kmalloc_array(host->sg_tablesize + 2,
					     sizeof(struct dbdma_cmd),
					     GFP_KERNEL);
       	if (dma_cmd_space == 0) {
       		printk(KERN_ERR "mac53c94: couldn't allocate dma "
       		       "command space for %pOF\n", node);
		rc = -ENOMEM;
       		goto out_free;
       	}
	state->dma_cmds = (struct dbdma_cmd *)DBDMA_ALIGN(dma_cmd_space);
	memset(state->dma_cmds, 0, (host->sg_tablesize + 1)
	       * sizeof(struct dbdma_cmd));
	state->dma_cmd_space = dma_cmd_space;

	mac53c94_init(state);

	if (request_irq(state->intr, do_mac53c94_interrupt, 0, "53C94",state)) {
		printk(KERN_ERR "mac53C94: can't get irq %d for %pOF\n",
		       state->intr, node);
		goto out_free_dma;
	}

	rc = scsi_add_host(host, &mdev->ofdev.dev);
	if (rc != 0)
		goto out_release_irq;

	scsi_scan_host(host);
	return 0;

 out_release_irq:
	free_irq(state->intr, state);
 out_free_dma:
	kfree(state->dma_cmd_space);
 out_free:
	if (state->dma != NULL)
		iounmap(state->dma);
	if (state->regs != NULL)
		iounmap(state->regs);
	scsi_host_put(host);
 out_release:
	macio_release_resources(mdev);

	return rc;
}

static int mac53c94_remove(struct macio_dev *mdev)
{
	struct fsc_state *fp = (struct fsc_state *)macio_get_drvdata(mdev);
	struct Scsi_Host *host = fp->host;

	scsi_remove_host(host);

	free_irq(fp->intr, fp);

	if (fp->regs)
		iounmap(fp->regs);
	if (fp->dma)
		iounmap(fp->dma);
	kfree(fp->dma_cmd_space);

	scsi_host_put(host);

	macio_release_resources(mdev);

	return 0;
}


static struct of_device_id mac53c94_match[] = 
{
	{
	.name 		= "53c94",
	},
	{},
};
MODULE_DEVICE_TABLE (of, mac53c94_match);

static struct macio_driver mac53c94_driver = 
{
	.driver = {
		.name 		= "mac53c94",
		.owner		= THIS_MODULE,
		.of_match_table	= mac53c94_match,
	},
	.probe		= mac53c94_probe,
	.remove		= mac53c94_remove,
};


static int __init init_mac53c94(void)
{
	return macio_register_driver(&mac53c94_driver);
}

static void __exit exit_mac53c94(void)
{
	return macio_unregister_driver(&mac53c94_driver);
}

module_init(init_mac53c94);
module_exit(exit_mac53c94);

MODULE_DESCRIPTION("PowerMac 53c94 SCSI driver");
MODULE_AUTHOR("Paul Mackerras <paulus@samba.org>");
MODULE_LICENSE("GPL");
