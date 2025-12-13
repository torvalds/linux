// SPDX-License-Identifier: GPL-2.0-only
/*
 *  TI FlashMedia driver
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * Special thanks to Carlos Corbacho for providing various MemoryStick cards
 * that made this driver possible.
 */

#include <linux/tifm.h>
#include <linux/memstick.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <asm/io.h>

#define DRIVER_NAME "tifm_ms"

static bool no_dma;
module_param(no_dma, bool, 0644);

/*
 * Some control bits of TIFM appear to conform to Sony's reference design,
 * so I'm just assuming they all are.
 */

#define TIFM_MS_STAT_DRQ     0x04000
#define TIFM_MS_STAT_MSINT   0x02000
#define TIFM_MS_STAT_RDY     0x01000
#define TIFM_MS_STAT_CRC     0x00200
#define TIFM_MS_STAT_TOE     0x00100
#define TIFM_MS_STAT_EMP     0x00020
#define TIFM_MS_STAT_FUL     0x00010
#define TIFM_MS_STAT_CED     0x00008
#define TIFM_MS_STAT_ERR     0x00004
#define TIFM_MS_STAT_BRQ     0x00002
#define TIFM_MS_STAT_CNK     0x00001

#define TIFM_MS_SYS_DMA      0x10000
#define TIFM_MS_SYS_RESET    0x08000
#define TIFM_MS_SYS_SRAC     0x04000
#define TIFM_MS_SYS_INTEN    0x02000
#define TIFM_MS_SYS_NOCRC    0x01000
#define TIFM_MS_SYS_INTCLR   0x00800
#define TIFM_MS_SYS_MSIEN    0x00400
#define TIFM_MS_SYS_FCLR     0x00200
#define TIFM_MS_SYS_FDIR     0x00100
#define TIFM_MS_SYS_DAM      0x00080
#define TIFM_MS_SYS_DRM      0x00040
#define TIFM_MS_SYS_DRQSL    0x00020
#define TIFM_MS_SYS_REI      0x00010
#define TIFM_MS_SYS_REO      0x00008
#define TIFM_MS_SYS_BSY_MASK 0x00007

#define TIFM_MS_SYS_FIFO     (TIFM_MS_SYS_INTEN | TIFM_MS_SYS_MSIEN \
			      | TIFM_MS_SYS_FCLR | TIFM_MS_SYS_BSY_MASK)

/* Hardware flags */
enum {
	CMD_READY  = 0x01,
	FIFO_READY = 0x02,
	CARD_INT   = 0x04
};

struct tifm_ms {
	struct tifm_dev         *dev;
	struct timer_list       timer;
	struct memstick_request *req;
	struct tasklet_struct   notify;
	unsigned int            mode_mask;
	unsigned int            block_pos;
	unsigned long           timeout_jiffies;
	unsigned char           eject:1,
				use_dma:1;
	unsigned char           cmd_flags;
	unsigned char           io_pos;
	unsigned int            io_word;
};

static unsigned int tifm_ms_read_data(struct tifm_ms *host,
				      unsigned char *buf, unsigned int length)
{
	struct tifm_dev *sock = host->dev;
	unsigned int off = 0;

	while (host->io_pos && length) {
		buf[off++] = host->io_word & 0xff;
		host->io_word >>= 8;
		length--;
		host->io_pos--;
	}

	if (!length)
		return off;

	while (!(TIFM_MS_STAT_EMP & readl(sock->addr + SOCK_MS_STATUS))) {
		if (length < 4)
			break;
		*(unsigned int *)(buf + off) = __raw_readl(sock->addr
							   + SOCK_MS_DATA);
		length -= 4;
		off += 4;
	}

	if (length
	    && !(TIFM_MS_STAT_EMP & readl(sock->addr + SOCK_MS_STATUS))) {
		host->io_word = readl(sock->addr + SOCK_MS_DATA);
		for (host->io_pos = 4; host->io_pos; --host->io_pos) {
			buf[off++] = host->io_word & 0xff;
			host->io_word >>= 8;
			length--;
			if (!length)
				break;
		}
	}

	return off;
}

static unsigned int tifm_ms_write_data(struct tifm_ms *host,
				       unsigned char *buf, unsigned int length)
{
	struct tifm_dev *sock = host->dev;
	unsigned int off = 0;

	if (host->io_pos) {
		while (host->io_pos < 4 && length) {
			host->io_word |=  buf[off++] << (host->io_pos * 8);
			host->io_pos++;
			length--;
		}
	}

	if (host->io_pos == 4
	    && !(TIFM_MS_STAT_FUL & readl(sock->addr + SOCK_MS_STATUS))) {
		writel(TIFM_MS_SYS_FDIR | readl(sock->addr + SOCK_MS_SYSTEM),
		       sock->addr + SOCK_MS_SYSTEM);
		writel(host->io_word, sock->addr + SOCK_MS_DATA);
		host->io_pos = 0;
		host->io_word = 0;
	} else if (host->io_pos) {
		return off;
	}

	if (!length)
		return off;

	while (!(TIFM_MS_STAT_FUL & readl(sock->addr + SOCK_MS_STATUS))) {
		if (length < 4)
			break;
		writel(TIFM_MS_SYS_FDIR | readl(sock->addr + SOCK_MS_SYSTEM),
		       sock->addr + SOCK_MS_SYSTEM);
		__raw_writel(*(unsigned int *)(buf + off),
			     sock->addr + SOCK_MS_DATA);
		length -= 4;
		off += 4;
	}

	switch (length) {
	case 3:
		host->io_word |= buf[off + 2] << 16;
		host->io_pos++;
		fallthrough;
	case 2:
		host->io_word |= buf[off + 1] << 8;
		host->io_pos++;
		fallthrough;
	case 1:
		host->io_word |= buf[off];
		host->io_pos++;
	}

	off += host->io_pos;

	return off;
}

static unsigned int tifm_ms_transfer_data(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	unsigned int length;
	unsigned int off;
	unsigned int t_size, p_cnt;
	unsigned char *buf;
	struct page *pg;
	unsigned long flags = 0;

	if (host->req->long_data) {
		length = host->req->sg.length - host->block_pos;
		off = host->req->sg.offset + host->block_pos;
	} else {
		length = host->req->data_len - host->block_pos;
		off = 0;
	}
	dev_dbg(&sock->dev, "fifo data transfer, %d, %d\n", length,
		host->block_pos);

	while (length) {
		unsigned int p_off;

		if (host->req->long_data) {
			pg = sg_page(&host->req->sg) + (off >> PAGE_SHIFT);
			p_off = offset_in_page(off);
			p_cnt = PAGE_SIZE - p_off;
			p_cnt = min(p_cnt, length);

			local_irq_save(flags);
			buf = kmap_atomic(pg) + p_off;
		} else {
			buf = host->req->data + host->block_pos;
			p_cnt = host->req->data_len - host->block_pos;
		}

		t_size = host->req->data_dir == WRITE
			 ? tifm_ms_write_data(host, buf, p_cnt)
			 : tifm_ms_read_data(host, buf, p_cnt);

		if (host->req->long_data) {
			kunmap_atomic(buf - p_off);
			local_irq_restore(flags);
		}

		if (!t_size)
			break;
		host->block_pos += t_size;
		length -= t_size;
		off += t_size;
	}

	dev_dbg(&sock->dev, "fifo data transfer, %d remaining\n", length);
	if (!length && (host->req->data_dir == WRITE)) {
		if (host->io_pos) {
			writel(TIFM_MS_SYS_FDIR
			       | readl(sock->addr + SOCK_MS_SYSTEM),
			       sock->addr + SOCK_MS_SYSTEM);
			writel(host->io_word, sock->addr + SOCK_MS_DATA);
		}
		writel(TIFM_MS_SYS_FDIR
		       | readl(sock->addr + SOCK_MS_SYSTEM),
		       sock->addr + SOCK_MS_SYSTEM);
		writel(0, sock->addr + SOCK_MS_DATA);
	} else {
		readl(sock->addr + SOCK_MS_DATA);
	}

	return length;
}

static int tifm_ms_issue_cmd(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	unsigned int data_len, cmd, sys_param;

	host->cmd_flags = 0;
	host->block_pos = 0;
	host->io_pos = 0;
	host->io_word = 0;
	host->cmd_flags = 0;

	host->use_dma = !no_dma;

	if (host->req->long_data) {
		data_len = host->req->sg.length;
		if (!is_power_of_2(data_len))
			host->use_dma = 0;
	} else {
		data_len = host->req->data_len;
		host->use_dma = 0;
	}

	writel(TIFM_FIFO_INT_SETALL,
	       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
	writel(TIFM_FIFO_ENABLE,
	       sock->addr + SOCK_FIFO_CONTROL);

	if (host->use_dma) {
		if (1 != tifm_map_sg(sock, &host->req->sg, 1,
				     host->req->data_dir == READ
				     ? DMA_FROM_DEVICE
				     : DMA_TO_DEVICE)) {
			host->req->error = -ENOMEM;
			return host->req->error;
		}
		data_len = sg_dma_len(&host->req->sg);

		writel(ilog2(data_len) - 2,
		       sock->addr + SOCK_FIFO_PAGE_SIZE);
		writel(TIFM_FIFO_INTMASK,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);
		sys_param = TIFM_DMA_EN | (1 << 8);
		if (host->req->data_dir == WRITE)
			sys_param |= TIFM_DMA_TX;

		writel(TIFM_FIFO_INTMASK,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);

		writel(sg_dma_address(&host->req->sg),
		       sock->addr + SOCK_DMA_ADDRESS);
		writel(sys_param, sock->addr + SOCK_DMA_CONTROL);
	} else {
		writel(host->mode_mask | TIFM_MS_SYS_FIFO,
		       sock->addr + SOCK_MS_SYSTEM);

		writel(TIFM_FIFO_MORE,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);
	}

	mod_timer(&host->timer, jiffies + host->timeout_jiffies);
	writel(TIFM_CTRL_LED | readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);
	host->req->error = 0;

	sys_param = readl(sock->addr + SOCK_MS_SYSTEM);
	sys_param |= TIFM_MS_SYS_INTCLR;

	if (host->use_dma)
		sys_param |= TIFM_MS_SYS_DMA;
	else
		sys_param &= ~TIFM_MS_SYS_DMA;

	writel(sys_param, sock->addr + SOCK_MS_SYSTEM);

	cmd = (host->req->tpc & 0xf) << 12;
	cmd |= data_len;
	writel(cmd, sock->addr + SOCK_MS_COMMAND);

	dev_dbg(&sock->dev, "executing TPC %x, %x\n", cmd, sys_param);
	return 0;
}

static void tifm_ms_complete_cmd(struct tifm_ms *host)
{
	struct tifm_dev *sock = host->dev;
	struct memstick_host *msh = tifm_get_drvdata(sock);
	int rc;

	timer_delete(&host->timer);

	host->req->int_reg = readl(sock->addr + SOCK_MS_STATUS) & 0xff;
	host->req->int_reg = (host->req->int_reg & 1)
			     | ((host->req->int_reg << 4) & 0xe0);

	writel(TIFM_FIFO_INT_SETALL,
	       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
	writel(TIFM_DMA_RESET, sock->addr + SOCK_DMA_CONTROL);

	if (host->use_dma) {
		tifm_unmap_sg(sock, &host->req->sg, 1,
			      host->req->data_dir == READ
			      ? DMA_FROM_DEVICE
			      : DMA_TO_DEVICE);
	}

	writel((~TIFM_CTRL_LED) & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);

	dev_dbg(&sock->dev, "TPC complete\n");
	do {
		rc = memstick_next_req(msh, &host->req);
	} while (!rc && tifm_ms_issue_cmd(host));
}

static int tifm_ms_check_status(struct tifm_ms *host)
{
	if (!host->req->error) {
		if (!(host->cmd_flags & CMD_READY))
			return 1;
		if (!(host->cmd_flags & FIFO_READY))
			return 1;
		if (host->req->need_card_int
		    && !(host->cmd_flags & CARD_INT))
			return 1;
	}
	return 0;
}

/* Called from interrupt handler */
static void tifm_ms_data_event(struct tifm_dev *sock)
{
	struct tifm_ms *host;
	unsigned int fifo_status = 0, host_status = 0;
	int rc = 1;

	spin_lock(&sock->lock);
	host = memstick_priv((struct memstick_host *)tifm_get_drvdata(sock));
	fifo_status = readl(sock->addr + SOCK_DMA_FIFO_STATUS);
	host_status = readl(sock->addr + SOCK_MS_STATUS);
	dev_dbg(&sock->dev,
		"data event: fifo_status %x, host_status %x, flags %x\n",
		fifo_status, host_status, host->cmd_flags);

	if (host->req) {
		if (host->use_dma && (fifo_status & 1)) {
			host->cmd_flags |= FIFO_READY;
			rc = tifm_ms_check_status(host);
		}
		if (!host->use_dma && (fifo_status & TIFM_FIFO_MORE)) {
			if (!tifm_ms_transfer_data(host)) {
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
		if (host_status & TIFM_MS_STAT_TOE)
			host->req->error = -ETIME;
		else if (host_status & TIFM_MS_STAT_CRC)
			host->req->error = -EILSEQ;

		if (host_status & TIFM_MS_STAT_RDY)
			host->cmd_flags |= CMD_READY;

		if (host_status & TIFM_MS_STAT_MSINT)
			host->cmd_flags |= CARD_INT;

		rc = tifm_ms_check_status(host);

	}

	writel(TIFM_MS_SYS_INTCLR | readl(sock->addr + SOCK_MS_SYSTEM),
	       sock->addr + SOCK_MS_SYSTEM);

	if (!rc)
		tifm_ms_complete_cmd(host);

	spin_unlock(&sock->lock);
	return;
}

static void tifm_ms_req_tasklet(unsigned long data)
{
	struct memstick_host *msh = (struct memstick_host *)data;
	struct tifm_ms *host = memstick_priv(msh);
	struct tifm_dev *sock = host->dev;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&sock->lock, flags);
	if (!host->req) {
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
	}
	spin_unlock_irqrestore(&sock->lock, flags);
}

static void tifm_ms_dummy_submit(struct memstick_host *msh)
{
	return;
}

static void tifm_ms_submit_req(struct memstick_host *msh)
{
	struct tifm_ms *host = memstick_priv(msh);

	tasklet_schedule(&host->notify);
}

static int tifm_ms_set_param(struct memstick_host *msh,
			     enum memstick_param param,
			     int value)
{
	struct tifm_ms *host = memstick_priv(msh);
	struct tifm_dev *sock = host->dev;

	switch (param) {
	case MEMSTICK_POWER:
		/* also affected by media detection mechanism */
		if (value == MEMSTICK_POWER_ON) {
			host->mode_mask = TIFM_MS_SYS_SRAC | TIFM_MS_SYS_REI;
			writel(TIFM_MS_SYS_RESET, sock->addr + SOCK_MS_SYSTEM);
			writel(TIFM_MS_SYS_FCLR | TIFM_MS_SYS_INTCLR,
			       sock->addr + SOCK_MS_SYSTEM);
			writel(0xffffffff, sock->addr + SOCK_MS_STATUS);
		} else if (value == MEMSTICK_POWER_OFF) {
			writel(TIFM_MS_SYS_FCLR | TIFM_MS_SYS_INTCLR,
			       sock->addr + SOCK_MS_SYSTEM);
			writel(0xffffffff, sock->addr + SOCK_MS_STATUS);
		} else
			return -EINVAL;
		break;
	case MEMSTICK_INTERFACE:
		if (value == MEMSTICK_SERIAL) {
			host->mode_mask = TIFM_MS_SYS_SRAC | TIFM_MS_SYS_REI;
			writel((~TIFM_CTRL_FAST_CLK)
			       & readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		} else if (value == MEMSTICK_PAR4) {
			host->mode_mask = 0;
			writel(TIFM_CTRL_FAST_CLK
			       | readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		} else
			return -EINVAL;
		break;
	}

	return 0;
}

static void tifm_ms_abort(struct timer_list *t)
{
	struct tifm_ms *host = timer_container_of(host, t, timer);

	dev_dbg(&host->dev->dev, "status %x\n",
		readl(host->dev->addr + SOCK_MS_STATUS));
	printk(KERN_ERR
	       "%s : card failed to respond for a long period of time "
	       "(%x, %x)\n",
	       dev_name(&host->dev->dev), host->req ? host->req->tpc : 0,
	       host->cmd_flags);

	tifm_eject(host->dev);
}

static int tifm_ms_probe(struct tifm_dev *sock)
{
	struct memstick_host *msh;
	struct tifm_ms *host;
	int rc = -EIO;

	if (!(TIFM_SOCK_STATE_OCCUPIED
	      & readl(sock->addr + SOCK_PRESENT_STATE))) {
		printk(KERN_WARNING "%s : card gone, unexpectedly\n",
		       dev_name(&sock->dev));
		return rc;
	}

	msh = memstick_alloc_host(sizeof(struct tifm_ms), &sock->dev);
	if (!msh)
		return -ENOMEM;

	host = memstick_priv(msh);
	tifm_set_drvdata(sock, msh);
	host->dev = sock;
	host->timeout_jiffies = msecs_to_jiffies(1000);

	timer_setup(&host->timer, tifm_ms_abort, 0);
	tasklet_init(&host->notify, tifm_ms_req_tasklet, (unsigned long)msh);

	msh->request = tifm_ms_submit_req;
	msh->set_param = tifm_ms_set_param;
	sock->card_event = tifm_ms_card_event;
	sock->data_event = tifm_ms_data_event;
	if (tifm_has_ms_pif(sock))
		msh->caps |= MEMSTICK_CAP_PAR4;

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

	msh->request = tifm_ms_dummy_submit;
	tasklet_kill(&host->notify);
	spin_lock_irqsave(&sock->lock, flags);
	host->eject = 1;
	if (host->req) {
		timer_delete(&host->timer);
		writel(TIFM_FIFO_INT_SETALL,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
		writel(TIFM_DMA_RESET, sock->addr + SOCK_DMA_CONTROL);
		if (host->use_dma)
			tifm_unmap_sg(sock, &host->req->sg, 1,
				      host->req->data_dir == READ
				      ? DMA_TO_DEVICE
				      : DMA_FROM_DEVICE);
		host->req->error = -ETIME;

		do {
			rc = memstick_next_req(msh, &host->req);
			if (!rc)
				host->req->error = -ETIME;
		} while (!rc);
	}
	spin_unlock_irqrestore(&sock->lock, flags);

	memstick_remove_host(msh);
	memstick_free_host(msh);
}

#ifdef CONFIG_PM

static int tifm_ms_suspend(struct tifm_dev *sock, pm_message_t state)
{
	struct memstick_host *msh = tifm_get_drvdata(sock);

	memstick_suspend_host(msh);
	return 0;
}

static int tifm_ms_resume(struct tifm_dev *sock)
{
	struct memstick_host *msh = tifm_get_drvdata(sock);

	memstick_resume_host(msh);
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

module_init(tifm_ms_init);
module_exit(tifm_ms_exit);
