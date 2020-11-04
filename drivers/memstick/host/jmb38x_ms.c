// SPDX-License-Identifier: GPL-2.0-only
/*
 *  jmb38x_ms.c - JMicron jmb38x MemoryStick card reader
 *
 *  Copyright (C) 2008 Alex Dubov <oakad@yahoo.com>
 */

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/memstick.h>
#include <linux/slab.h>
#include <linux/module.h>

#define DRIVER_NAME "jmb38x_ms"

static bool no_dma;
module_param(no_dma, bool, 0644);

enum {
	DMA_ADDRESS       = 0x00,
	BLOCK             = 0x04,
	DMA_CONTROL       = 0x08,
	TPC_P0            = 0x0c,
	TPC_P1            = 0x10,
	TPC               = 0x14,
	HOST_CONTROL      = 0x18,
	DATA              = 0x1c,
	STATUS            = 0x20,
	INT_STATUS        = 0x24,
	INT_STATUS_ENABLE = 0x28,
	INT_SIGNAL_ENABLE = 0x2c,
	TIMER             = 0x30,
	TIMER_CONTROL     = 0x34,
	PAD_OUTPUT_ENABLE = 0x38,
	PAD_PU_PD         = 0x3c,
	CLOCK_DELAY       = 0x40,
	ADMA_ADDRESS      = 0x44,
	CLOCK_CONTROL     = 0x48,
	LED_CONTROL       = 0x4c,
	VERSION           = 0x50
};

struct jmb38x_ms_host {
	struct jmb38x_ms        *chip;
	void __iomem            *addr;
	spinlock_t              lock;
	struct tasklet_struct   notify;
	int                     id;
	char                    host_id[32];
	int                     irq;
	unsigned int            block_pos;
	unsigned long           timeout_jiffies;
	struct timer_list       timer;
	struct memstick_host	*msh;
	struct memstick_request *req;
	unsigned char           cmd_flags;
	unsigned char           io_pos;
	unsigned char           ifmode;
	unsigned int            io_word[2];
};

struct jmb38x_ms {
	struct pci_dev        *pdev;
	int                   host_cnt;
	struct memstick_host  *hosts[];
};

#define BLOCK_COUNT_MASK       0xffff0000
#define BLOCK_SIZE_MASK        0x00000fff

#define DMA_CONTROL_ENABLE     0x00000001

#define TPC_DATA_SEL           0x00008000
#define TPC_DIR                0x00004000
#define TPC_WAIT_INT           0x00002000
#define TPC_GET_INT            0x00000800
#define TPC_CODE_SZ_MASK       0x00000700
#define TPC_DATA_SZ_MASK       0x00000007

#define HOST_CONTROL_TDELAY_EN 0x00040000
#define HOST_CONTROL_HW_OC_P   0x00010000
#define HOST_CONTROL_RESET_REQ 0x00008000
#define HOST_CONTROL_REI       0x00004000
#define HOST_CONTROL_LED       0x00000400
#define HOST_CONTROL_FAST_CLK  0x00000200
#define HOST_CONTROL_RESET     0x00000100
#define HOST_CONTROL_POWER_EN  0x00000080
#define HOST_CONTROL_CLOCK_EN  0x00000040
#define HOST_CONTROL_REO       0x00000008
#define HOST_CONTROL_IF_SHIFT  4

#define HOST_CONTROL_IF_SERIAL 0x0
#define HOST_CONTROL_IF_PAR4   0x1
#define HOST_CONTROL_IF_PAR8   0x3

#define STATUS_BUSY             0x00080000
#define STATUS_MS_DAT7          0x00040000
#define STATUS_MS_DAT6          0x00020000
#define STATUS_MS_DAT5          0x00010000
#define STATUS_MS_DAT4          0x00008000
#define STATUS_MS_DAT3          0x00004000
#define STATUS_MS_DAT2          0x00002000
#define STATUS_MS_DAT1          0x00001000
#define STATUS_MS_DAT0          0x00000800
#define STATUS_HAS_MEDIA        0x00000400
#define STATUS_FIFO_EMPTY       0x00000200
#define STATUS_FIFO_FULL        0x00000100
#define STATUS_MS_CED           0x00000080
#define STATUS_MS_ERR           0x00000040
#define STATUS_MS_BRQ           0x00000020
#define STATUS_MS_CNK           0x00000001

#define INT_STATUS_TPC_ERR      0x00080000
#define INT_STATUS_CRC_ERR      0x00040000
#define INT_STATUS_TIMER_TO     0x00020000
#define INT_STATUS_HSK_TO       0x00010000
#define INT_STATUS_ANY_ERR      0x00008000
#define INT_STATUS_FIFO_WRDY    0x00000080
#define INT_STATUS_FIFO_RRDY    0x00000040
#define INT_STATUS_MEDIA_OUT    0x00000010
#define INT_STATUS_MEDIA_IN     0x00000008
#define INT_STATUS_DMA_BOUNDARY 0x00000004
#define INT_STATUS_EOTRAN       0x00000002
#define INT_STATUS_EOTPC        0x00000001

#define INT_STATUS_ALL          0x000f801f

#define PAD_OUTPUT_ENABLE_MS  0x0F3F

#define PAD_PU_PD_OFF         0x7FFF0000
#define PAD_PU_PD_ON_MS_SOCK0 0x5f8f0000
#define PAD_PU_PD_ON_MS_SOCK1 0x0f0f0000

#define CLOCK_CONTROL_BY_MMIO 0x00000008
#define CLOCK_CONTROL_40MHZ   0x00000001
#define CLOCK_CONTROL_50MHZ   0x00000002
#define CLOCK_CONTROL_60MHZ   0x00000010
#define CLOCK_CONTROL_62_5MHZ 0x00000004
#define CLOCK_CONTROL_OFF     0x00000000

#define PCI_CTL_CLOCK_DLY_ADDR   0x000000b0

enum {
	CMD_READY    = 0x01,
	FIFO_READY   = 0x02,
	REG_DATA     = 0x04,
	DMA_DATA     = 0x08
};

static unsigned int jmb38x_ms_read_data(struct jmb38x_ms_host *host,
					unsigned char *buf, unsigned int length)
{
	unsigned int off = 0;

	while (host->io_pos && length) {
		buf[off++] = host->io_word[0] & 0xff;
		host->io_word[0] >>= 8;
		length--;
		host->io_pos--;
	}

	if (!length)
		return off;

	while (!(STATUS_FIFO_EMPTY & readl(host->addr + STATUS))) {
		if (length < 4)
			break;
		*(unsigned int *)(buf + off) = __raw_readl(host->addr + DATA);
		length -= 4;
		off += 4;
	}

	if (length
	    && !(STATUS_FIFO_EMPTY & readl(host->addr + STATUS))) {
		host->io_word[0] = readl(host->addr + DATA);
		for (host->io_pos = 4; host->io_pos; --host->io_pos) {
			buf[off++] = host->io_word[0] & 0xff;
			host->io_word[0] >>= 8;
			length--;
			if (!length)
				break;
		}
	}

	return off;
}

static unsigned int jmb38x_ms_read_reg_data(struct jmb38x_ms_host *host,
					    unsigned char *buf,
					    unsigned int length)
{
	unsigned int off = 0;

	while (host->io_pos > 4 && length) {
		buf[off++] = host->io_word[0] & 0xff;
		host->io_word[0] >>= 8;
		length--;
		host->io_pos--;
	}

	if (!length)
		return off;

	while (host->io_pos && length) {
		buf[off++] = host->io_word[1] & 0xff;
		host->io_word[1] >>= 8;
		length--;
		host->io_pos--;
	}

	return off;
}

static unsigned int jmb38x_ms_write_data(struct jmb38x_ms_host *host,
					 unsigned char *buf,
					 unsigned int length)
{
	unsigned int off = 0;

	if (host->io_pos) {
		while (host->io_pos < 4 && length) {
			host->io_word[0] |=  buf[off++] << (host->io_pos * 8);
			host->io_pos++;
			length--;
		}
	}

	if (host->io_pos == 4
	    && !(STATUS_FIFO_FULL & readl(host->addr + STATUS))) {
		writel(host->io_word[0], host->addr + DATA);
		host->io_pos = 0;
		host->io_word[0] = 0;
	} else if (host->io_pos) {
		return off;
	}

	if (!length)
		return off;

	while (!(STATUS_FIFO_FULL & readl(host->addr + STATUS))) {
		if (length < 4)
			break;

		__raw_writel(*(unsigned int *)(buf + off),
			     host->addr + DATA);
		length -= 4;
		off += 4;
	}

	switch (length) {
	case 3:
		host->io_word[0] |= buf[off + 2] << 16;
		host->io_pos++;
		fallthrough;
	case 2:
		host->io_word[0] |= buf[off + 1] << 8;
		host->io_pos++;
		fallthrough;
	case 1:
		host->io_word[0] |= buf[off];
		host->io_pos++;
	}

	off += host->io_pos;

	return off;
}

static unsigned int jmb38x_ms_write_reg_data(struct jmb38x_ms_host *host,
					     unsigned char *buf,
					     unsigned int length)
{
	unsigned int off = 0;

	while (host->io_pos < 4 && length) {
		host->io_word[0] &= ~(0xff << (host->io_pos * 8));
		host->io_word[0] |=  buf[off++] << (host->io_pos * 8);
		host->io_pos++;
		length--;
	}

	if (!length)
		return off;

	while (host->io_pos < 8 && length) {
		host->io_word[1] &= ~(0xff << (host->io_pos * 8));
		host->io_word[1] |=  buf[off++] << (host->io_pos * 8);
		host->io_pos++;
		length--;
	}

	return off;
}

static int jmb38x_ms_transfer_data(struct jmb38x_ms_host *host)
{
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

	while (length) {
		unsigned int p_off;

		if (host->req->long_data) {
			pg = nth_page(sg_page(&host->req->sg),
				      off >> PAGE_SHIFT);
			p_off = offset_in_page(off);
			p_cnt = PAGE_SIZE - p_off;
			p_cnt = min(p_cnt, length);

			local_irq_save(flags);
			buf = kmap_atomic(pg) + p_off;
		} else {
			buf = host->req->data + host->block_pos;
			p_cnt = host->req->data_len - host->block_pos;
		}

		if (host->req->data_dir == WRITE)
			t_size = !(host->cmd_flags & REG_DATA)
				 ? jmb38x_ms_write_data(host, buf, p_cnt)
				 : jmb38x_ms_write_reg_data(host, buf, p_cnt);
		else
			t_size = !(host->cmd_flags & REG_DATA)
				 ? jmb38x_ms_read_data(host, buf, p_cnt)
				 : jmb38x_ms_read_reg_data(host, buf, p_cnt);

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

	if (!length && host->req->data_dir == WRITE) {
		if (host->cmd_flags & REG_DATA) {
			writel(host->io_word[0], host->addr + TPC_P0);
			writel(host->io_word[1], host->addr + TPC_P1);
		} else if (host->io_pos) {
			writel(host->io_word[0], host->addr + DATA);
		}
	}

	return length;
}

static int jmb38x_ms_issue_cmd(struct memstick_host *msh)
{
	struct jmb38x_ms_host *host = memstick_priv(msh);
	unsigned int data_len, cmd, t_val;

	if (!(STATUS_HAS_MEDIA & readl(host->addr + STATUS))) {
		dev_dbg(&msh->dev, "no media status\n");
		host->req->error = -ETIME;
		return host->req->error;
	}

	dev_dbg(&msh->dev, "control %08x\n", readl(host->addr + HOST_CONTROL));
	dev_dbg(&msh->dev, "status %08x\n", readl(host->addr + INT_STATUS));
	dev_dbg(&msh->dev, "hstatus %08x\n", readl(host->addr + STATUS));

	host->cmd_flags = 0;
	host->block_pos = 0;
	host->io_pos = 0;
	host->io_word[0] = 0;
	host->io_word[1] = 0;

	cmd = host->req->tpc << 16;
	cmd |= TPC_DATA_SEL;

	if (host->req->data_dir == READ)
		cmd |= TPC_DIR;

	if (host->req->need_card_int) {
		if (host->ifmode == MEMSTICK_SERIAL)
			cmd |= TPC_GET_INT;
		else
			cmd |= TPC_WAIT_INT;
	}

	if (!no_dma)
		host->cmd_flags |= DMA_DATA;

	if (host->req->long_data) {
		data_len = host->req->sg.length;
	} else {
		data_len = host->req->data_len;
		host->cmd_flags &= ~DMA_DATA;
	}

	if (data_len <= 8) {
		cmd &= ~(TPC_DATA_SEL | 0xf);
		host->cmd_flags |= REG_DATA;
		cmd |= data_len & 0xf;
		host->cmd_flags &= ~DMA_DATA;
	}

	if (host->cmd_flags & DMA_DATA) {
		if (1 != dma_map_sg(&host->chip->pdev->dev, &host->req->sg, 1,
				    host->req->data_dir == READ
				    ? DMA_FROM_DEVICE
				    : DMA_TO_DEVICE)) {
			host->req->error = -ENOMEM;
			return host->req->error;
		}
		data_len = sg_dma_len(&host->req->sg);
		writel(sg_dma_address(&host->req->sg),
		       host->addr + DMA_ADDRESS);
		writel(((1 << 16) & BLOCK_COUNT_MASK)
		       | (data_len & BLOCK_SIZE_MASK),
		       host->addr + BLOCK);
		writel(DMA_CONTROL_ENABLE, host->addr + DMA_CONTROL);
	} else if (!(host->cmd_flags & REG_DATA)) {
		writel(((1 << 16) & BLOCK_COUNT_MASK)
		       | (data_len & BLOCK_SIZE_MASK),
		       host->addr + BLOCK);
		t_val = readl(host->addr + INT_STATUS_ENABLE);
		t_val |= host->req->data_dir == READ
			 ? INT_STATUS_FIFO_RRDY
			 : INT_STATUS_FIFO_WRDY;

		writel(t_val, host->addr + INT_STATUS_ENABLE);
		writel(t_val, host->addr + INT_SIGNAL_ENABLE);
	} else {
		cmd &= ~(TPC_DATA_SEL | 0xf);
		host->cmd_flags |= REG_DATA;
		cmd |= data_len & 0xf;

		if (host->req->data_dir == WRITE) {
			jmb38x_ms_transfer_data(host);
			writel(host->io_word[0], host->addr + TPC_P0);
			writel(host->io_word[1], host->addr + TPC_P1);
		}
	}

	mod_timer(&host->timer, jiffies + host->timeout_jiffies);
	writel(HOST_CONTROL_LED | readl(host->addr + HOST_CONTROL),
	       host->addr + HOST_CONTROL);
	host->req->error = 0;

	writel(cmd, host->addr + TPC);
	dev_dbg(&msh->dev, "executing TPC %08x, len %x\n", cmd, data_len);

	return 0;
}

static void jmb38x_ms_complete_cmd(struct memstick_host *msh, int last)
{
	struct jmb38x_ms_host *host = memstick_priv(msh);
	unsigned int t_val = 0;
	int rc;

	del_timer(&host->timer);

	dev_dbg(&msh->dev, "c control %08x\n",
		readl(host->addr + HOST_CONTROL));
	dev_dbg(&msh->dev, "c status %08x\n",
		readl(host->addr + INT_STATUS));
	dev_dbg(&msh->dev, "c hstatus %08x\n", readl(host->addr + STATUS));

	host->req->int_reg = readl(host->addr + STATUS) & 0xff;

	writel(0, host->addr + BLOCK);
	writel(0, host->addr + DMA_CONTROL);

	if (host->cmd_flags & DMA_DATA) {
		dma_unmap_sg(&host->chip->pdev->dev, &host->req->sg, 1,
			     host->req->data_dir == READ
			     ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	} else {
		t_val = readl(host->addr + INT_STATUS_ENABLE);
		if (host->req->data_dir == READ)
			t_val &= ~INT_STATUS_FIFO_RRDY;
		else
			t_val &= ~INT_STATUS_FIFO_WRDY;

		writel(t_val, host->addr + INT_STATUS_ENABLE);
		writel(t_val, host->addr + INT_SIGNAL_ENABLE);
	}

	writel((~HOST_CONTROL_LED) & readl(host->addr + HOST_CONTROL),
	       host->addr + HOST_CONTROL);

	if (!last) {
		do {
			rc = memstick_next_req(msh, &host->req);
		} while (!rc && jmb38x_ms_issue_cmd(msh));
	} else {
		do {
			rc = memstick_next_req(msh, &host->req);
			if (!rc)
				host->req->error = -ETIME;
		} while (!rc);
	}
}

static irqreturn_t jmb38x_ms_isr(int irq, void *dev_id)
{
	struct memstick_host *msh = dev_id;
	struct jmb38x_ms_host *host = memstick_priv(msh);
	unsigned int irq_status;

	spin_lock(&host->lock);
	irq_status = readl(host->addr + INT_STATUS);
	dev_dbg(&host->chip->pdev->dev, "irq_status = %08x\n", irq_status);
	if (irq_status == 0 || irq_status == (~0)) {
		spin_unlock(&host->lock);
		return IRQ_NONE;
	}

	if (host->req) {
		if (irq_status & INT_STATUS_ANY_ERR) {
			if (irq_status & INT_STATUS_CRC_ERR)
				host->req->error = -EILSEQ;
			else if (irq_status & INT_STATUS_TPC_ERR) {
				dev_dbg(&host->chip->pdev->dev, "TPC_ERR\n");
				jmb38x_ms_complete_cmd(msh, 0);
			} else
				host->req->error = -ETIME;
		} else {
			if (host->cmd_flags & DMA_DATA) {
				if (irq_status & INT_STATUS_EOTRAN)
					host->cmd_flags |= FIFO_READY;
			} else {
				if (irq_status & (INT_STATUS_FIFO_RRDY
						  | INT_STATUS_FIFO_WRDY))
					jmb38x_ms_transfer_data(host);

				if (irq_status & INT_STATUS_EOTRAN) {
					jmb38x_ms_transfer_data(host);
					host->cmd_flags |= FIFO_READY;
				}
			}

			if (irq_status & INT_STATUS_EOTPC) {
				host->cmd_flags |= CMD_READY;
				if (host->cmd_flags & REG_DATA) {
					if (host->req->data_dir == READ) {
						host->io_word[0]
							= readl(host->addr
								+ TPC_P0);
						host->io_word[1]
							= readl(host->addr
								+ TPC_P1);
						host->io_pos = 8;

						jmb38x_ms_transfer_data(host);
					}
					host->cmd_flags |= FIFO_READY;
				}
			}
		}
	}

	if (irq_status & (INT_STATUS_MEDIA_IN | INT_STATUS_MEDIA_OUT)) {
		dev_dbg(&host->chip->pdev->dev, "media changed\n");
		memstick_detect_change(msh);
	}

	writel(irq_status, host->addr + INT_STATUS);

	if (host->req
	    && (((host->cmd_flags & CMD_READY)
		 && (host->cmd_flags & FIFO_READY))
		|| host->req->error))
		jmb38x_ms_complete_cmd(msh, 0);

	spin_unlock(&host->lock);
	return IRQ_HANDLED;
}

static void jmb38x_ms_abort(struct timer_list *t)
{
	struct jmb38x_ms_host *host = from_timer(host, t, timer);
	struct memstick_host *msh = host->msh;
	unsigned long flags;

	dev_dbg(&host->chip->pdev->dev, "abort\n");
	spin_lock_irqsave(&host->lock, flags);
	if (host->req) {
		host->req->error = -ETIME;
		jmb38x_ms_complete_cmd(msh, 0);
	}
	spin_unlock_irqrestore(&host->lock, flags);
}

static void jmb38x_ms_req_tasklet(unsigned long data)
{
	struct memstick_host *msh = (struct memstick_host *)data;
	struct jmb38x_ms_host *host = memstick_priv(msh);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&host->lock, flags);
	if (!host->req) {
		do {
			rc = memstick_next_req(msh, &host->req);
			dev_dbg(&host->chip->pdev->dev, "tasklet req %d\n", rc);
		} while (!rc && jmb38x_ms_issue_cmd(msh));
	}
	spin_unlock_irqrestore(&host->lock, flags);
}

static void jmb38x_ms_dummy_submit(struct memstick_host *msh)
{
	return;
}

static void jmb38x_ms_submit_req(struct memstick_host *msh)
{
	struct jmb38x_ms_host *host = memstick_priv(msh);

	tasklet_schedule(&host->notify);
}

static int jmb38x_ms_reset(struct jmb38x_ms_host *host)
{
	int cnt;

	writel(HOST_CONTROL_RESET_REQ | HOST_CONTROL_CLOCK_EN
	       | readl(host->addr + HOST_CONTROL),
	       host->addr + HOST_CONTROL);

	for (cnt = 0; cnt < 20; ++cnt) {
		if (!(HOST_CONTROL_RESET_REQ
		      & readl(host->addr + HOST_CONTROL)))
			goto reset_next;

		ndelay(20);
	}
	dev_dbg(&host->chip->pdev->dev, "reset_req timeout\n");

reset_next:
	writel(HOST_CONTROL_RESET | HOST_CONTROL_CLOCK_EN
	       | readl(host->addr + HOST_CONTROL),
	       host->addr + HOST_CONTROL);

	for (cnt = 0; cnt < 20; ++cnt) {
		if (!(HOST_CONTROL_RESET
		      & readl(host->addr + HOST_CONTROL)))
			goto reset_ok;

		ndelay(20);
	}
	dev_dbg(&host->chip->pdev->dev, "reset timeout\n");
	return -EIO;

reset_ok:
	writel(INT_STATUS_ALL, host->addr + INT_SIGNAL_ENABLE);
	writel(INT_STATUS_ALL, host->addr + INT_STATUS_ENABLE);
	return 0;
}

static int jmb38x_ms_set_param(struct memstick_host *msh,
			       enum memstick_param param,
			       int value)
{
	struct jmb38x_ms_host *host = memstick_priv(msh);
	unsigned int host_ctl = readl(host->addr + HOST_CONTROL);
	unsigned int clock_ctl = CLOCK_CONTROL_BY_MMIO, clock_delay = 0;
	int rc = 0;

	switch (param) {
	case MEMSTICK_POWER:
		if (value == MEMSTICK_POWER_ON) {
			rc = jmb38x_ms_reset(host);
			if (rc)
				return rc;

			host_ctl = 7;
			host_ctl |= HOST_CONTROL_POWER_EN
				 | HOST_CONTROL_CLOCK_EN;
			writel(host_ctl, host->addr + HOST_CONTROL);

			writel(host->id ? PAD_PU_PD_ON_MS_SOCK1
					: PAD_PU_PD_ON_MS_SOCK0,
			       host->addr + PAD_PU_PD);

			writel(PAD_OUTPUT_ENABLE_MS,
			       host->addr + PAD_OUTPUT_ENABLE);

			msleep(10);
			dev_dbg(&host->chip->pdev->dev, "power on\n");
		} else if (value == MEMSTICK_POWER_OFF) {
			host_ctl &= ~(HOST_CONTROL_POWER_EN
				      | HOST_CONTROL_CLOCK_EN);
			writel(host_ctl, host->addr +  HOST_CONTROL);
			writel(0, host->addr + PAD_OUTPUT_ENABLE);
			writel(PAD_PU_PD_OFF, host->addr + PAD_PU_PD);
			dev_dbg(&host->chip->pdev->dev, "power off\n");
		} else
			return -EINVAL;
		break;
	case MEMSTICK_INTERFACE:
		dev_dbg(&host->chip->pdev->dev,
			"Set Host Interface Mode to %d\n", value);
		host_ctl &= ~(HOST_CONTROL_FAST_CLK | HOST_CONTROL_REI |
			      HOST_CONTROL_REO);
		host_ctl |= HOST_CONTROL_TDELAY_EN | HOST_CONTROL_HW_OC_P;
		host_ctl &= ~(3 << HOST_CONTROL_IF_SHIFT);

		if (value == MEMSTICK_SERIAL) {
			host_ctl |= HOST_CONTROL_IF_SERIAL
				    << HOST_CONTROL_IF_SHIFT;
			host_ctl |= HOST_CONTROL_REI;
			clock_ctl |= CLOCK_CONTROL_40MHZ;
			clock_delay = 0;
		} else if (value == MEMSTICK_PAR4) {
			host_ctl |= HOST_CONTROL_FAST_CLK;
			host_ctl |= HOST_CONTROL_IF_PAR4
				    << HOST_CONTROL_IF_SHIFT;
			host_ctl |= HOST_CONTROL_REO;
			clock_ctl |= CLOCK_CONTROL_40MHZ;
			clock_delay = 4;
		} else if (value == MEMSTICK_PAR8) {
			host_ctl |= HOST_CONTROL_FAST_CLK;
			host_ctl |= HOST_CONTROL_IF_PAR8
				    << HOST_CONTROL_IF_SHIFT;
			clock_ctl |= CLOCK_CONTROL_50MHZ;
			clock_delay = 0;
		} else
			return -EINVAL;

		writel(host_ctl, host->addr + HOST_CONTROL);
		writel(CLOCK_CONTROL_OFF, host->addr + CLOCK_CONTROL);
		writel(clock_ctl, host->addr + CLOCK_CONTROL);
		pci_write_config_byte(host->chip->pdev,
				      PCI_CTL_CLOCK_DLY_ADDR + 1,
				      clock_delay);
		host->ifmode = value;
		break;
	};
	return 0;
}

#define PCI_PMOS0_CONTROL		0xae
#define  PMOS0_ENABLE			0x01
#define  PMOS0_OVERCURRENT_LEVEL_2_4V	0x06
#define  PMOS0_EN_OVERCURRENT_DEBOUNCE	0x40
#define  PMOS0_SW_LED_POLARITY_ENABLE	0x80
#define  PMOS0_ACTIVE_BITS (PMOS0_ENABLE | PMOS0_EN_OVERCURRENT_DEBOUNCE | \
			    PMOS0_OVERCURRENT_LEVEL_2_4V)
#define PCI_PMOS1_CONTROL		0xbd
#define  PMOS1_ACTIVE_BITS		0x4a
#define PCI_CLOCK_CTL			0xb9

static int jmb38x_ms_pmos(struct pci_dev *pdev, int flag)
{
	unsigned char val;

	pci_read_config_byte(pdev, PCI_PMOS0_CONTROL, &val);
	if (flag)
		val |= PMOS0_ACTIVE_BITS;
	else
		val &= ~PMOS0_ACTIVE_BITS;
	pci_write_config_byte(pdev, PCI_PMOS0_CONTROL, val);
	dev_dbg(&pdev->dev, "JMB38x: set PMOS0 val 0x%x\n", val);

	if (pci_resource_flags(pdev, 1)) {
		pci_read_config_byte(pdev, PCI_PMOS1_CONTROL, &val);
		if (flag)
			val |= PMOS1_ACTIVE_BITS;
		else
			val &= ~PMOS1_ACTIVE_BITS;
		pci_write_config_byte(pdev, PCI_PMOS1_CONTROL, val);
		dev_dbg(&pdev->dev, "JMB38x: set PMOS1 val 0x%x\n", val);
	}

	pci_read_config_byte(pdev, PCI_CLOCK_CTL, &val);
	pci_write_config_byte(pdev, PCI_CLOCK_CTL, val & ~0x0f);
	pci_write_config_byte(pdev, PCI_CLOCK_CTL, val | 0x01);
	dev_dbg(&pdev->dev, "Clock Control by PCI config is disabled!\n");

        return 0;
}

static int __maybe_unused jmb38x_ms_suspend(struct device *dev)
{
	struct jmb38x_ms *jm = dev_get_drvdata(dev);

	int cnt;

	for (cnt = 0; cnt < jm->host_cnt; ++cnt) {
		if (!jm->hosts[cnt])
			break;
		memstick_suspend_host(jm->hosts[cnt]);
	}

	device_wakeup_disable(dev);

	return 0;
}

static int __maybe_unused jmb38x_ms_resume(struct device *dev)
{
	struct jmb38x_ms *jm = dev_get_drvdata(dev);
	int rc;

	jmb38x_ms_pmos(to_pci_dev(dev), 1);

	for (rc = 0; rc < jm->host_cnt; ++rc) {
		if (!jm->hosts[rc])
			break;
		memstick_resume_host(jm->hosts[rc]);
		memstick_detect_change(jm->hosts[rc]);
	}

	return 0;
}

static int jmb38x_ms_count_slots(struct pci_dev *pdev)
{
	int cnt, rc = 0;

	for (cnt = 0; cnt < PCI_STD_NUM_BARS; ++cnt) {
		if (!(IORESOURCE_MEM & pci_resource_flags(pdev, cnt)))
			break;

		if (256 != pci_resource_len(pdev, cnt))
			break;

		++rc;
	}
	return rc;
}

static struct memstick_host *jmb38x_ms_alloc_host(struct jmb38x_ms *jm, int cnt)
{
	struct memstick_host *msh;
	struct jmb38x_ms_host *host;

	msh = memstick_alloc_host(sizeof(struct jmb38x_ms_host),
				  &jm->pdev->dev);
	if (!msh)
		return NULL;

	host = memstick_priv(msh);
	host->msh = msh;
	host->chip = jm;
	host->addr = ioremap(pci_resource_start(jm->pdev, cnt),
			     pci_resource_len(jm->pdev, cnt));
	if (!host->addr)
		goto err_out_free;

	spin_lock_init(&host->lock);
	host->id = cnt;
	snprintf(host->host_id, sizeof(host->host_id), DRIVER_NAME ":slot%d",
		 host->id);
	host->irq = jm->pdev->irq;
	host->timeout_jiffies = msecs_to_jiffies(1000);

	tasklet_init(&host->notify, jmb38x_ms_req_tasklet, (unsigned long)msh);
	msh->request = jmb38x_ms_submit_req;
	msh->set_param = jmb38x_ms_set_param;

	msh->caps = MEMSTICK_CAP_PAR4 | MEMSTICK_CAP_PAR8;

	timer_setup(&host->timer, jmb38x_ms_abort, 0);

	if (!request_irq(host->irq, jmb38x_ms_isr, IRQF_SHARED, host->host_id,
			 msh))
		return msh;

	iounmap(host->addr);
err_out_free:
	kfree(msh);
	return NULL;
}

static void jmb38x_ms_free_host(struct memstick_host *msh)
{
	struct jmb38x_ms_host *host = memstick_priv(msh);

	free_irq(host->irq, msh);
	iounmap(host->addr);
	memstick_free_host(msh);
}

static int jmb38x_ms_probe(struct pci_dev *pdev,
			   const struct pci_device_id *dev_id)
{
	struct jmb38x_ms *jm;
	int pci_dev_busy = 0;
	int rc, cnt;

	rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRIVER_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	jmb38x_ms_pmos(pdev, 1);

	cnt = jmb38x_ms_count_slots(pdev);
	if (!cnt) {
		rc = -ENODEV;
		pci_dev_busy = 1;
		goto err_out_int;
	}

	jm = kzalloc(sizeof(struct jmb38x_ms)
		     + cnt * sizeof(struct memstick_host *), GFP_KERNEL);
	if (!jm) {
		rc = -ENOMEM;
		goto err_out_int;
	}

	jm->pdev = pdev;
	jm->host_cnt = cnt;
	pci_set_drvdata(pdev, jm);

	for (cnt = 0; cnt < jm->host_cnt; ++cnt) {
		jm->hosts[cnt] = jmb38x_ms_alloc_host(jm, cnt);
		if (!jm->hosts[cnt])
			break;

		rc = memstick_add_host(jm->hosts[cnt]);

		if (rc) {
			jmb38x_ms_free_host(jm->hosts[cnt]);
			jm->hosts[cnt] = NULL;
			break;
		}
	}

	if (cnt)
		return 0;

	rc = -ENODEV;

	pci_set_drvdata(pdev, NULL);
	kfree(jm);
err_out_int:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}

static void jmb38x_ms_remove(struct pci_dev *dev)
{
	struct jmb38x_ms *jm = pci_get_drvdata(dev);
	struct jmb38x_ms_host *host;
	int cnt;
	unsigned long flags;

	for (cnt = 0; cnt < jm->host_cnt; ++cnt) {
		if (!jm->hosts[cnt])
			break;

		host = memstick_priv(jm->hosts[cnt]);

		jm->hosts[cnt]->request = jmb38x_ms_dummy_submit;
		tasklet_kill(&host->notify);
		writel(0, host->addr + INT_SIGNAL_ENABLE);
		writel(0, host->addr + INT_STATUS_ENABLE);
		dev_dbg(&jm->pdev->dev, "interrupts off\n");
		spin_lock_irqsave(&host->lock, flags);
		if (host->req) {
			host->req->error = -ETIME;
			jmb38x_ms_complete_cmd(jm->hosts[cnt], 1);
		}
		spin_unlock_irqrestore(&host->lock, flags);

		memstick_remove_host(jm->hosts[cnt]);
		dev_dbg(&jm->pdev->dev, "host removed\n");

		jmb38x_ms_free_host(jm->hosts[cnt]);
	}

	jmb38x_ms_pmos(dev, 0);

	pci_set_drvdata(dev, NULL);
	pci_release_regions(dev);
	pci_disable_device(dev);
	kfree(jm);
}

static struct pci_device_id jmb38x_ms_id_tbl [] = {
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB38X_MS) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB385_MS) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB390_MS) },
	{ }
};

static SIMPLE_DEV_PM_OPS(jmb38x_ms_pm_ops, jmb38x_ms_suspend, jmb38x_ms_resume);

static struct pci_driver jmb38x_ms_driver = {
	.name = DRIVER_NAME,
	.id_table = jmb38x_ms_id_tbl,
	.probe = jmb38x_ms_probe,
	.remove = jmb38x_ms_remove,
	.driver.pm = &jmb38x_ms_pm_ops,
};

module_pci_driver(jmb38x_ms_driver);

MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("JMicron jmb38x MemoryStick driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, jmb38x_ms_id_tbl);
