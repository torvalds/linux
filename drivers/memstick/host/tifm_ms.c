/*
 *  TI FlashMedia driver
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Special thanks to Carlos Corbacho for providing various MemoryStick cards
 * that made this driver possible.
 *
 */

#include <linux/tifm.h>
#include <linux/memstick.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/log2.h>
#include <asm/io.h>

#define DRIVER_NAME "tifm_ms"
#define DRIVER_VERSION "0.1"

static int no_dma;
module_param(no_dma, bool, 0644);

#define TIFM_MS_TIMEOUT      0x00100
#define TIFM_MS_BADCRC       0x00200
#define TIFM_MS_EOTPC        0x01000
#define TIFM_MS_INT          0x02000

/* The meaning of the bit majority in this constant is unknown. */
#define TIFM_MS_SERIAL       0x04010

#define TIFM_MS_SYS_LATCH    0x00100
#define TIFM_MS_SYS_NOT_RDY  0x00800
#define TIFM_MS_SYS_DATA     0x10000

/* Hardware flags */
enum {
	CMD_READY  = 0x0001,
	FIFO_READY = 0x0002,
	CARD_READY = 0x0004,
	DATA_CARRY = 0x0008
};

struct tifm_ms {
	struct tifm_dev         *dev;
	unsigned short          eject:1,
				no_dma:1;
	unsigned short          cmd_flags;
	unsigned int            mode_mask;
	unsigned int            block_pos;
	unsigned long           timeout_jiffies;

	struct timer_list       timer;
	struct memstick_request *req;
	unsigned int            io_word;
};

static void tifm_ms_read_fifo(struct tifm_ms *host, unsigned int fifo_offset,
			      struct page *pg, unsigned int page_off,
			      unsigned int length)
{
	struct tifm_dev *sock = host->dev;
	unsigned int cnt = 0, off = 0;
	unsigned char *buf = kmap_atomic(pg, KM_BIO_DST_IRQ) + page_off;

	if (host->cmd_flags & DATA_CARRY) {
		while ((fifo_offset & 3) && length) {
			buf[off++] = host->io_word & 0xff;
			host->io_word >>= 8;
			length--;
			fifo_offset++;
		}
		if (!(fifo_offset & 3))
			host->cmd_flags &= ~DATA_CARRY;
		if (!length)
			return;
	}

	do {
		host->io_word = readl(sock->addr + SOCK_FIFO_ACCESS
				      + fifo_offset);
		cnt = 4;
		while (length && cnt) {
			buf[off++] = (host->io_word >> 8) & 0xff;
			cnt--;
			length--;
		}
		fifo_offset += 4 - cnt;
	} while (length);

	if (cnt)
		host->cmd_flags |= DATA_CARRY;

	kunmap_atomic(buf - page_off, KM_BIO_DST_IRQ);
}

static void tifm_ms_write_fifo(struct tifm_ms *host, unsigned int fifo_offset,
			       struct page *pg, unsigned int page_off,
			       unsigned int length)
{
	struct tifm_dev *sock = host->dev;
	unsigned int cnt = 0, off = 0;
	unsigned char *buf = kmap_atomic(pg, KM_BIO_SRC_IRQ) + page_off;

	if (host->cmd_flags & DATA_CARRY) {
		while (fifo_offset & 3) {
			host->io_word |= buf[off++] << (8 * (fifo_offset & 3));
			length--;
			fifo_offset++;
		}
		if (!(fifo_offset & 3)) {
			writel(host->io_word, sock->addr + SOCK_FIFO_ACCESS
			       + fifo_offset - 4);

			host->cmd_flags &= ~DATA_CARRY;
		}
		if (!length)
			return;
	}

	do {
		cnt = 4;
		host->io_word = 0;
		while (length && cnt) {
			host->io_word |= buf[off++] << (4 - cnt);
			cnt--;
			length--;
		}
		fifo_offset += 4 - cnt;
		if (!cnt)
			writel(host->io_word, sock->addr + SOCK_FIFO_ACCESS
					      + fifo_offset - 4);

	} while (length);

	if (cnt)
		host->cmd_flags |= DATA_CARRY;

	kunmap_atomic(buf - page_off, KM_BIO_SRC_IRQ);
}

static void tifm_ms_move_block(struct tifm_ms *host, unsigned int length)
{
	unsigned int t_size;
	unsigned int off = host->req->sg.offset + host->block_pos;
	unsigned int p_off, p_cnt;
	struct page *pg;
	unsigned long flags;

	dev_dbg(&host->dev->dev, "moving block\n");
	local_irq_save(flags);
	t_size = length;
	while (t_size) {
		pg = nth_page(sg_page(&host->req->sg), off >> PAGE_SHIFT);
		p_off = offset_in_page(off);
		p_cnt = PAGE_SIZE - p_off;
		p_cnt = min(p_cnt, t_size);

		if (host->req->data_dir == WRITE)
			tifm_ms_write_fifo(host, length - t_size,
					   pg, p_off, p_cnt);
		else
			tifm_ms_read_fifo(host, length - t_size,
					  pg, p_off, p_cnt);

		t_size -= p_cnt;
	}
	local_irq_restore(flags);
}

static int tifm_ms_transfer_data(struct tifm_ms *host, int skip)
{
	struct tifm_dev *sock = host->dev;
	unsigned int length = host->req->sg.length - host->block_pos;

	if (!length)
		return 1;

	if (length > TIFM_FIFO_SIZE)
		length = TIFM_FIFO_SIZE;

	if (!skip) {
		tifm_ms_move_block(host, length);
		host->block_pos += length;
	}

	if ((host->req->data_dir == READ)
	    && (host->block_pos == host->req->sg.length))
		return 1;

	writel(ilog2(length) - 2, sock->addr + SOCK_FIFO_PAGE_SIZE);
	if (host->req->data_dir == WRITE)
		writel((1 << 8) | TIFM_DMA_TX, sock->addr + SOCK_DMA_CONTROL);
	else
		writel((1 << 8), sock->addr + SOCK_DMA_CONTROL);

	return 0;
}

static int tifm_ms_issue_cmd(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	unsigned char *data;
	unsigned int data_len = 0, cmd = 0, cmd_mask = 0, cnt, tval = 0;

	host->cmd_flags = 0;

	if (host->req->io_type == MEMSTICK_IO_SG) {
		if (!host->no_dma) {
			if (1 != tifm_map_sg(sock, &host->req->sg, 1,
					     host->req->data_dir == READ
					     ? PCI_DMA_FROMDEVICE
					     : PCI_DMA_TODEVICE)) {
				host->req->error = -ENOMEM;
				return host->req->error;
			}
			data_len = sg_dma_len(&host->req->sg);
		} else
			data_len = host->req->sg.length;

		writel(TIFM_FIFO_INT_SETALL,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
		writel(TIFM_FIFO_ENABLE,
		       sock->addr + SOCK_FIFO_CONTROL);
		writel(TIFM_FIFO_INTMASK,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);

		if (!host->no_dma) {
			writel(ilog2(data_len) - 2,
			       sock->addr + SOCK_FIFO_PAGE_SIZE);
			writel(sg_dma_address(&host->req->sg),
			       sock->addr + SOCK_DMA_ADDRESS);
			if (host->req->data_dir == WRITE)
				writel((1 << 8) | TIFM_DMA_TX | TIFM_DMA_EN,
				       sock->addr + SOCK_DMA_CONTROL);
			else
				writel((1 << 8) | TIFM_DMA_EN,
				       sock->addr + SOCK_DMA_CONTROL);
		} else {
			tifm_ms_transfer_data(host,
					      host->req->data_dir == READ);
		}

		cmd_mask = readl(sock->addr + SOCK_MS_SYSTEM);
		cmd_mask |= TIFM_MS_SYS_DATA | TIFM_MS_SYS_NOT_RDY;
		writel(cmd_mask, sock->addr + SOCK_MS_SYSTEM);
	} else if (host->req->io_type == MEMSTICK_IO_VAL) {
		data = host->req->data;
		data_len = host->req->data_len;

		cmd_mask = host->mode_mask | 0x2607; /* unknown constant */

		if (host->req->data_dir == WRITE) {
			cmd_mask |= TIFM_MS_SYS_LATCH;
			writel(cmd_mask, sock->addr + SOCK_MS_SYSTEM);
			for (cnt = 0; (data_len - cnt) >= 4; cnt += 4) {
				writel(TIFM_MS_SYS_LATCH
				       | readl(sock->addr + SOCK_MS_SYSTEM),
				       sock->addr + SOCK_MS_SYSTEM);
				__raw_writel(*(unsigned int *)(data + cnt),
					     sock->addr + SOCK_MS_DATA);
				dev_dbg(&sock->dev, "writing %x\n",
					*(int *)(data + cnt));
			}
			switch (data_len - cnt) {
			case 3:
				tval |= data[cnt + 2] << 16;
			case 2:
				tval |= data[cnt + 1] << 8;
			case 1:
				tval |= data[cnt];
				writel(TIFM_MS_SYS_LATCH
				       | readl(sock->addr + SOCK_MS_SYSTEM),
				       sock->addr + SOCK_MS_SYSTEM);
				writel(tval, sock->addr + SOCK_MS_DATA);
				dev_dbg(&sock->dev, "writing %x\n", tval);
			}

			writel(TIFM_MS_SYS_LATCH
			       | readl(sock->addr + SOCK_MS_SYSTEM),
			       sock->addr + SOCK_MS_SYSTEM);
			writel(0, sock->addr + SOCK_MS_DATA);
			dev_dbg(&sock->dev, "writing %x\n", 0);

		} else
			writel(cmd_mask, sock->addr + SOCK_MS_SYSTEM);

		cmd_mask = readl(sock->addr + SOCK_MS_SYSTEM);
		cmd_mask &= ~TIFM_MS_SYS_DATA;
		cmd_mask |= TIFM_MS_SYS_NOT_RDY;
		dev_dbg(&sock->dev, "mask %x\n", cmd_mask);
		writel(cmd_mask, sock->addr + SOCK_MS_SYSTEM);
	} else
		BUG();

	mod_timer(&host->timer, jiffies + host->timeout_jiffies);
	writel(TIFM_CTRL_LED | readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);
	host->req->error = 0;

	cmd = (host->req->tpc & 0xf) << 12;
	cmd |= data_len;
	writel(cmd, sock->addr + SOCK_MS_COMMAND);

	dev_dbg(&sock->dev, "executing TPC %x, %x\n", cmd, cmd_mask);
	return 0;
}

static void tifm_ms_complete_cmd(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	struct memstick_host *msh = tifm_get_drvdata(sock);
	unsigned int tval = 0, data_len;
	unsigned char *data;
	int rc;

	del_timer(&host->timer);
	if (host->req->io_type == MEMSTICK_IO_SG) {
		if (!host->no_dma)
			tifm_unmap_sg(sock, &host->req->sg, 1,
				      host->req->data_dir == READ
				      ? PCI_DMA_FROMDEVICE
				      : PCI_DMA_TODEVICE);
	} else if (host->req->io_type == MEMSTICK_IO_VAL) {
		writel(~TIFM_MS_SYS_DATA & readl(sock->addr + SOCK_MS_SYSTEM),
		       sock->addr + SOCK_MS_SYSTEM);

		data = host->req->data;
		data_len = host->req->data_len;

		if (host->req->data_dir == READ) {
			for (rc = 0; (data_len - rc) >= 4; rc += 4)
				*(int *)(data + rc)
					= __raw_readl(sock->addr
						      + SOCK_MS_DATA);

			if (data_len - rc)
				tval = readl(sock->addr + SOCK_MS_DATA);
			switch (data_len - rc) {
			case 3:
				data[rc + 2] = (tval >> 16) & 0xff;
			case 2:
				data[rc + 1] = (tval >> 8) & 0xff;
			case 1:
				data[rc] = tval & 0xff;
			}
			readl(sock->addr + SOCK_MS_DATA);
		}
	}

	writel((~TIFM_CTRL_LED) & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);

	do {
		rc = memstick_next_req(msh, &host->req);
	} while (!rc && tifm_ms_issue_cmd(host));
}

static int tifm_ms_check_status(struct tifm_ms *host)
{
	if (!host->req->error) {
		if (!(host->cmd_flags & CMD_READY))
			return 1;
		if ((host->req->io_type == MEMSTICK_IO_SG)
		    && !(host->cmd_flags & FIFO_READY))
			return 1;
		if (host->req->need_card_int
		    && !(host->cmd_flags & CARD_READY))
			return 1;
	}
	return 0;
}

/* Called from interrupt handler */
static void tifm_ms_data_event(struct tifm_dev *sock)
{
	struct tifm_ms *host;
	unsigned int fifo_status = 0;
	int rc = 1;

	spin_lock(&sock->lock);
	host = memstick_priv((struct memstick_host *)tifm_get_drvdata(sock));
	fifo_status = readl(sock->addr + SOCK_DMA_FIFO_STATUS);
	dev_dbg(&sock->dev, "data event: fifo_status %x, flags %x\n",
		fifo_status, host->cmd_flags);

	if (host->req) {
		if (fifo_status & TIFM_FIFO_READY) {
			if (!host->no_dma || tifm_ms_transfer_data(host, 0)) {
				host->cmd_flags |= FIFO_READY;
				rc = tifm_ms_check_status(host);
			}
		}
	}

	writel(fifo_status, sock->addr + SOCK_DMA_FIFO_STATUS);
	if (!rc)
		tifm_ms_complete_cmd(host);

	spin_unlock(&sock->lock);
}


/* Called from interrupt handler */
static void tifm_ms_card_event(struct tifm_dev *sock)
{
	struct tifm_ms *host;
	unsigned int host_status = 0;
	int rc = 1;

	spin_lock(&sock->lock);
	host = memstick_priv((struct memstick_host *)tifm_get_drvdata(sock));
	host_status = readl(sock->addr + SOCK_MS_STATUS);
	dev_dbg(&sock->dev, "host event: host_status %x, flags %x\n",
		host_status, host->cmd_flags);

	if (host->req) {
		if (host_status & TIFM_MS_TIMEOUT)
			host->req->error = -ETIME;
		else if (host_status & TIFM_MS_BADCRC)
			host->req->error = -EILSEQ;

		if (host->req->error) {
			writel(TIFM_FIFO_INT_SETALL,
			       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
			writel(TIFM_DMA_RESET, sock->addr + SOCK_DMA_CONTROL);
		}

		if (host_status & TIFM_MS_EOTPC)
			host->cmd_flags |= CMD_READY;
		if (host_status & TIFM_MS_INT)
			host->cmd_flags |= CARD_READY;

		rc = tifm_ms_check_status(host);

	}

	writel(TIFM_MS_SYS_NOT_RDY | readl(sock->addr + SOCK_MS_SYSTEM),
	       sock->addr + SOCK_MS_SYSTEM);
	writel((~TIFM_MS_SYS_DATA) & readl(sock->addr + SOCK_MS_SYSTEM),
	       sock->addr + SOCK_MS_SYSTEM);

	if (!rc)
		tifm_ms_complete_cmd(host);

	spin_unlock(&sock->lock);
	return;
}

static void tifm_ms_request(struct memstick_host *msh)
{
	struct tifm_ms *host = memstick_priv(msh);
	struct tifm_dev *sock = host->dev;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&sock->lock, flags);
	if (host->req) {
		printk(KERN_ERR "%s : unfinished request detected\n",
		       sock->dev.bus_id);
		spin_unlock_irqrestore(&sock->lock, flags);
		tifm_eject(host->dev);
		return;
	}

	if (host->eject) {
		do {
			rc = memstick_next_req(msh, &host->req);
			if (!rc)
				host->req->error = -ETIME;
		} while (!rc);
		spin_unlock_irqrestore(&sock->lock, flags);
		return;
	}

	do {
		rc = memstick_next_req(msh, &host->req);
	} while (!rc && tifm_ms_issue_cmd(host));

	spin_unlock_irqrestore(&sock->lock, flags);
	return;
}

static void tifm_ms_set_param(struct memstick_host *msh,
			      enum memstick_param param,
			      int value)
{
	struct tifm_ms *host = memstick_priv(msh);
	struct tifm_dev *sock = host->dev;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);

	switch (param) {
	case MEMSTICK_POWER:
		/* this is set by card detection mechanism */
		break;
	case MEMSTICK_INTERFACE:
		if (value == MEMSTICK_SERIAL) {
			host->mode_mask = TIFM_MS_SERIAL;
			writel((~TIFM_CTRL_FAST_CLK)
			       & readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		} else if (value == MEMSTICK_PARALLEL) {
			host->mode_mask = 0;
			writel(TIFM_CTRL_FAST_CLK
			       | readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		}
		break;
	};

	spin_unlock_irqrestore(&sock->lock, flags);
}

static void tifm_ms_abort(unsigned long data)
{
	struct tifm_ms *host = (struct tifm_ms *)data;

	dev_dbg(&host->dev->dev, "status %x\n",
		readl(host->dev->addr + SOCK_MS_STATUS));
	printk(KERN_ERR
	       "%s : card failed to respond for a long period of time "
	       "(%x, %x)\n",
	       host->dev->dev.bus_id, host->req ? host->req->tpc : 0,
	       host->cmd_flags);

	tifm_eject(host->dev);
}

static int tifm_ms_initialize_host(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	struct memstick_host *msh = tifm_get_drvdata(sock);

	host->mode_mask = TIFM_MS_SERIAL;
	writel(0x8000, sock->addr + SOCK_MS_SYSTEM);
	writel(0x0200 | TIFM_MS_SYS_NOT_RDY, sock->addr + SOCK_MS_SYSTEM);
	writel(0xffffffff, sock->addr + SOCK_MS_STATUS);
	if (tifm_has_ms_pif(sock))
		msh->caps |= MEMSTICK_CAP_PARALLEL;

	return 0;
}

static int tifm_ms_probe(struct tifm_dev *sock)
{
	struct memstick_host *msh;
	struct tifm_ms *host;
	int rc = -EIO;

	if (!(TIFM_SOCK_STATE_OCCUPIED
	      & readl(sock->addr + SOCK_PRESENT_STATE))) {
		printk(KERN_WARNING "%s : card gone, unexpectedly\n",
		       sock->dev.bus_id);
		return rc;
	}

	msh = memstick_alloc_host(sizeof(struct tifm_ms), &sock->dev);
	if (!msh)
		return -ENOMEM;

	host = memstick_priv(msh);
	tifm_set_drvdata(sock, msh);
	host->dev = sock;
	host->timeout_jiffies = msecs_to_jiffies(1000);
	host->no_dma = no_dma;

	setup_timer(&host->timer, tifm_ms_abort, (unsigned long)host);

	msh->request = tifm_ms_request;
	msh->set_param = tifm_ms_set_param;
	sock->card_event = tifm_ms_card_event;
	sock->data_event = tifm_ms_data_event;
	rc = tifm_ms_initialize_host(host);

	if (!rc)
		rc = memstick_add_host(msh);
	if (!rc)
		return 0;

	memstick_free_host(msh);
	return rc;
}

static void tifm_ms_remove(struct tifm_dev *sock)
{
	struct memstick_host *msh = tifm_get_drvdata(sock);
	struct tifm_ms *host = memstick_priv(msh);
	int rc = 0;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);
	host->eject = 1;
	if (host->req) {
		del_timer(&host->timer);
		writel(TIFM_FIFO_INT_SETALL,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
		writel(TIFM_DMA_RESET, sock->addr + SOCK_DMA_CONTROL);
		if ((host->req->io_type == MEMSTICK_IO_SG) && !host->no_dma)
			tifm_unmap_sg(sock, &host->req->sg, 1,
				      host->req->data_dir == READ
				      ? PCI_DMA_TODEVICE
				      : PCI_DMA_FROMDEVICE);
		host->req->error = -ETIME;

		do {
			rc = memstick_next_req(msh, &host->req);
			if (!rc)
				host->req->error = -ETIME;
		} while (!rc);
	}
	spin_unlock_irqrestore(&sock->lock, flags);

	memstick_remove_host(msh);

	writel(0x0200 | TIFM_MS_SYS_NOT_RDY, sock->addr + SOCK_MS_SYSTEM);
	writel(0xffffffff, sock->addr + SOCK_MS_STATUS);

	memstick_free_host(msh);
}

#ifdef CONFIG_PM

static int tifm_ms_suspend(struct tifm_dev *sock, pm_message_t state)
{
	return 0;
}

static int tifm_ms_resume(struct tifm_dev *sock)
{
	struct memstick_host *msh = tifm_get_drvdata(sock);
	struct tifm_ms *host = memstick_priv(msh);

	tifm_ms_initialize_host(host);
	memstick_detect_change(msh);

	return 0;
}

#else

#define tifm_ms_suspend NULL
#define tifm_ms_resume NULL

#endif /* CONFIG_PM */

static struct tifm_device_id tifm_ms_id_tbl[] = {
	{ TIFM_TYPE_MS }, { 0 }
};

static struct tifm_driver tifm_ms_driver = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE
	},
	.id_table = tifm_ms_id_tbl,
	.probe    = tifm_ms_probe,
	.remove   = tifm_ms_remove,
	.suspend  = tifm_ms_suspend,
	.resume   = tifm_ms_resume
};

static int __init tifm_ms_init(void)
{
	return tifm_register_driver(&tifm_ms_driver);
}

static void __exit tifm_ms_exit(void)
{
	tifm_unregister_driver(&tifm_ms_driver);
}

MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("TI FlashMedia MemoryStick driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(tifm, tifm_ms_id_tbl);
MODULE_VERSION(DRIVER_VERSION);

module_init(tifm_ms_init);
module_exit(tifm_ms_exit);
