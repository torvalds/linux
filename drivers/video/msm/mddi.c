/*
 * MSM MDDI Transport
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (C) 2007 QUALCOMM Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/platform_data/video-msm_fb.h>
#include "mddi_hw.h"

#define FLAG_DISABLE_HIBERNATION 0x0001
#define FLAG_HAVE_CAPS		 0x0002
#define FLAG_HAS_VSYNC_IRQ	 0x0004
#define FLAG_HAVE_STATUS	 0x0008

#define CMD_GET_CLIENT_CAP     0x0601
#define CMD_GET_CLIENT_STATUS  0x0602

union mddi_rev {
	unsigned char raw[MDDI_REV_BUFFER_SIZE];
	struct mddi_rev_packet hdr;
	struct mddi_client_status status;
	struct mddi_client_caps caps;
	struct mddi_register_access reg;
};

struct reg_read_info {
	struct completion done;
	uint32_t reg;
	uint32_t status;
	uint32_t result;
};

struct mddi_info {
	uint16_t flags;
	uint16_t version;
	char __iomem *base;
	int irq;
	struct clk *clk;
	struct msm_mddi_client_data client_data;

	/* buffer for rev encap packets */
	void *rev_data;
	dma_addr_t rev_addr;
	struct mddi_llentry *reg_write_data;
	dma_addr_t reg_write_addr;
	struct mddi_llentry *reg_read_data;
	dma_addr_t reg_read_addr;
	size_t rev_data_curr;

	spinlock_t int_lock;
	uint32_t int_enable;
	uint32_t got_int;
	wait_queue_head_t int_wait;

	struct mutex reg_write_lock;
	struct mutex reg_read_lock;
	struct reg_read_info *reg_read;

	struct mddi_client_caps caps;
	struct mddi_client_status status;

	void (*power_client)(struct msm_mddi_client_data *, int);

	/* client device published to bind us to the
	 * appropriate mddi_client driver
	 */
	char client_name[20];

	struct platform_device client_pdev;
};

static void mddi_init_rev_encap(struct mddi_info *mddi);

#define mddi_readl(r) readl(mddi->base + (MDDI_##r))
#define mddi_writel(v, r) writel((v), mddi->base + (MDDI_##r))

void mddi_activate_link(struct msm_mddi_client_data *cdata)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);

	mddi_writel(MDDI_CMD_LINK_ACTIVE, CMD);
}

static void mddi_handle_link_list_done(struct mddi_info *mddi)
{
}

static void mddi_reset_rev_encap_ptr(struct mddi_info *mddi)
{
	printk(KERN_INFO "mddi: resetting rev ptr\n");
	mddi->rev_data_curr = 0;
	mddi_writel(mddi->rev_addr, REV_PTR);
	mddi_writel(mddi->rev_addr, REV_PTR);
	mddi_writel(MDDI_CMD_FORCE_NEW_REV_PTR, CMD);
}

static void mddi_handle_rev_data(struct mddi_info *mddi, union mddi_rev *rev)
{
	int i;
	struct reg_read_info *ri;

	if ((rev->hdr.length <= MDDI_REV_BUFFER_SIZE - 2) &&
	   (rev->hdr.length >= sizeof(struct mddi_rev_packet) - 2)) {

		switch (rev->hdr.type) {
		case TYPE_CLIENT_CAPS:
			memcpy(&mddi->caps, &rev->caps,
			       sizeof(struct mddi_client_caps));
			mddi->flags |= FLAG_HAVE_CAPS;
			wake_up(&mddi->int_wait);
			break;
		case TYPE_CLIENT_STATUS:
			memcpy(&mddi->status, &rev->status,
			       sizeof(struct mddi_client_status));
			mddi->flags |= FLAG_HAVE_STATUS;
			wake_up(&mddi->int_wait);
			break;
		case TYPE_REGISTER_ACCESS:
			ri = mddi->reg_read;
			if (ri == 0) {
				printk(KERN_INFO "rev: got reg %x = %x without "
						 " pending read\n",
				       rev->reg.register_address,
				       rev->reg.register_data_list);
				break;
			}
			if (ri->reg != rev->reg.register_address) {
				printk(KERN_INFO "rev: got reg %x = %x for "
						 "wrong register, expected "
						 "%x\n",
				       rev->reg.register_address,
				       rev->reg.register_data_list, ri->reg);
				break;
			}
			mddi->reg_read = NULL;
			ri->status = 0;
			ri->result = rev->reg.register_data_list;
			complete(&ri->done);
			break;
		default:
			printk(KERN_INFO "rev: unknown reverse packet: "
					 "len=%04x type=%04x CURR_REV_PTR=%x\n",
			       rev->hdr.length, rev->hdr.type,
			       mddi_readl(CURR_REV_PTR));
			for (i = 0; i < rev->hdr.length + 2; i++) {
				if ((i % 16) == 0)
					printk(KERN_INFO "\n");
				printk(KERN_INFO " %02x", rev->raw[i]);
			}
			printk(KERN_INFO "\n");
			mddi_reset_rev_encap_ptr(mddi);
		}
	} else {
		printk(KERN_INFO "bad rev length, %d, CURR_REV_PTR %x\n",
		       rev->hdr.length, mddi_readl(CURR_REV_PTR));
		mddi_reset_rev_encap_ptr(mddi);
	}
}

static void mddi_wait_interrupt(struct mddi_info *mddi, uint32_t intmask);

static void mddi_handle_rev_data_avail(struct mddi_info *mddi)
{
	uint32_t rev_data_count;
	uint32_t rev_crc_err_count;
	struct reg_read_info *ri;
	size_t prev_offset;
	uint16_t length;

	union mddi_rev *crev = mddi->rev_data + mddi->rev_data_curr;

	/* clear the interrupt */
	mddi_writel(MDDI_INT_REV_DATA_AVAIL, INT);
	rev_data_count = mddi_readl(REV_PKT_CNT);
	rev_crc_err_count = mddi_readl(REV_CRC_ERR);
	if (rev_data_count > 1)
		printk(KERN_INFO "rev_data_count %d\n", rev_data_count);

	if (rev_crc_err_count) {
		printk(KERN_INFO "rev_crc_err_count %d, INT %x\n",
		       rev_crc_err_count,  mddi_readl(INT));
		ri = mddi->reg_read;
		if (ri == 0) {
			printk(KERN_INFO "rev: got crc error without pending "
			       "read\n");
		} else {
			mddi->reg_read = NULL;
			ri->status = -EIO;
			ri->result = -1;
			complete(&ri->done);
		}
	}

	if (rev_data_count == 0)
		return;

	prev_offset = mddi->rev_data_curr;

	length = *((uint8_t *)mddi->rev_data + mddi->rev_data_curr);
	mddi->rev_data_curr++;
	if (mddi->rev_data_curr == MDDI_REV_BUFFER_SIZE)
		mddi->rev_data_curr = 0;
	length += *((uint8_t *)mddi->rev_data + mddi->rev_data_curr) << 8;
	mddi->rev_data_curr += 1 + length;
	if (mddi->rev_data_curr >= MDDI_REV_BUFFER_SIZE)
		mddi->rev_data_curr =
			mddi->rev_data_curr % MDDI_REV_BUFFER_SIZE;

	if (length > MDDI_REV_BUFFER_SIZE - 2) {
		printk(KERN_INFO "mddi: rev data length greater than buffer"
			"size\n");
		mddi_reset_rev_encap_ptr(mddi);
		return;
	}

	if (prev_offset + 2 + length >= MDDI_REV_BUFFER_SIZE) {
		union mddi_rev tmprev;
		size_t rem = MDDI_REV_BUFFER_SIZE - prev_offset;
		memcpy(&tmprev.raw[0], mddi->rev_data + prev_offset, rem);
		memcpy(&tmprev.raw[rem], mddi->rev_data, 2 + length - rem);
		mddi_handle_rev_data(mddi, &tmprev);
	} else {
		mddi_handle_rev_data(mddi, crev);
	}

	if (prev_offset < MDDI_REV_BUFFER_SIZE / 2 &&
	    mddi->rev_data_curr >= MDDI_REV_BUFFER_SIZE / 2) {
		mddi_writel(mddi->rev_addr, REV_PTR);
	}
}

static irqreturn_t mddi_isr(int irq, void *data)
{
	struct msm_mddi_client_data *cdata = data;
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	uint32_t active, status;

	spin_lock(&mddi->int_lock);

	active = mddi_readl(INT);
	status = mddi_readl(STAT);

	mddi_writel(active, INT);

	/* ignore any interrupts we have disabled */
	active &= mddi->int_enable;

	mddi->got_int |= active;
	wake_up(&mddi->int_wait);

	if (active & MDDI_INT_PRI_LINK_LIST_DONE) {
		mddi->int_enable &= (~MDDI_INT_PRI_LINK_LIST_DONE);
		mddi_handle_link_list_done(mddi);
	}
	if (active & MDDI_INT_REV_DATA_AVAIL)
		mddi_handle_rev_data_avail(mddi);

	if (active & ~MDDI_INT_NEED_CLEAR)
		mddi->int_enable &= ~(active & ~MDDI_INT_NEED_CLEAR);

	if (active & MDDI_INT_LINK_ACTIVE) {
		mddi->int_enable &= (~MDDI_INT_LINK_ACTIVE);
		mddi->int_enable |= MDDI_INT_IN_HIBERNATION;
	}

	if (active & MDDI_INT_IN_HIBERNATION) {
		mddi->int_enable &= (~MDDI_INT_IN_HIBERNATION);
		mddi->int_enable |= MDDI_INT_LINK_ACTIVE;
	}

	mddi_writel(mddi->int_enable, INTEN);
	spin_unlock(&mddi->int_lock);

	return IRQ_HANDLED;
}

static long mddi_wait_interrupt_timeout(struct mddi_info *mddi,
					uint32_t intmask, int timeout)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mddi->int_lock, irq_flags);
	mddi->got_int &= ~intmask;
	mddi->int_enable |= intmask;
	mddi_writel(mddi->int_enable, INTEN);
	spin_unlock_irqrestore(&mddi->int_lock, irq_flags);
	return wait_event_timeout(mddi->int_wait, mddi->got_int & intmask,
				  timeout);
}

static void mddi_wait_interrupt(struct mddi_info *mddi, uint32_t intmask)
{
	if (mddi_wait_interrupt_timeout(mddi, intmask, HZ/10) == 0)
		printk(KERN_INFO "mddi_wait_interrupt %d, timeout "
		       "waiting for %x, INT = %x, STAT = %x gotint = %x\n",
		       current->pid, intmask, mddi_readl(INT), mddi_readl(STAT),
		       mddi->got_int);
}

static void mddi_init_rev_encap(struct mddi_info *mddi)
{
	memset(mddi->rev_data, 0xee, MDDI_REV_BUFFER_SIZE);
	mddi_writel(mddi->rev_addr, REV_PTR);
	mddi_writel(MDDI_CMD_FORCE_NEW_REV_PTR, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
}

void mddi_set_auto_hibernate(struct msm_mddi_client_data *cdata, int on)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	mddi_writel(MDDI_CMD_POWERDOWN, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_IN_HIBERNATION);
	mddi_writel(MDDI_CMD_HIBERNATE | !!on, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
}


static uint16_t mddi_init_registers(struct mddi_info *mddi)
{
	mddi_writel(0x0001, VERSION);
	mddi_writel(MDDI_HOST_BYTES_PER_SUBFRAME, BPS);
	mddi_writel(0x0003, SPM); /* subframes per media */
	mddi_writel(0x0005, TA1_LEN);
	mddi_writel(MDDI_HOST_TA2_LEN, TA2_LEN);
	mddi_writel(0x0096, DRIVE_HI);
	/* 0x32 normal, 0x50 for Toshiba display */
	mddi_writel(0x0050, DRIVE_LO);
	mddi_writel(0x003C, DISP_WAKE); /* wakeup counter */
	mddi_writel(MDDI_HOST_REV_RATE_DIV, REV_RATE_DIV);

	mddi_writel(MDDI_REV_BUFFER_SIZE, REV_SIZE);
	mddi_writel(MDDI_MAX_REV_PKT_SIZE, REV_ENCAP_SZ);

	/* disable periodic rev encap */
	mddi_writel(MDDI_CMD_PERIODIC_REV_ENCAP, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);

	if (mddi_readl(PAD_CTL) == 0) {
		/* If we are turning on band gap, need to wait 5us before
		 * turning on the rest of the PAD */
		mddi_writel(0x08000, PAD_CTL);
		udelay(5);
	}

	/* Recommendation from PAD hw team */
	mddi_writel(0xa850f, PAD_CTL);


	/* Need an even number for counts */
	mddi_writel(0x60006, DRIVER_START_CNT);

	mddi_set_auto_hibernate(&mddi->client_data, 0);

	mddi_writel(MDDI_CMD_DISP_IGNORE, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);

	mddi_init_rev_encap(mddi);
	return mddi_readl(CORE_VER) & 0xffff;
}

static void mddi_suspend(struct msm_mddi_client_data *cdata)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	/* turn off the client */
	if (mddi->power_client)
		mddi->power_client(&mddi->client_data, 0);
	/* turn off the link */
	mddi_writel(MDDI_CMD_RESET, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	/* turn off the clock */
	clk_disable(mddi->clk);
}

static void mddi_resume(struct msm_mddi_client_data *cdata)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	mddi_set_auto_hibernate(&mddi->client_data, 0);
	/* turn on the client */
	if (mddi->power_client)
		mddi->power_client(&mddi->client_data, 1);
	/* turn on the clock */
	clk_enable(mddi->clk);
	/* set up the local registers */
	mddi->rev_data_curr = 0;
	mddi_init_registers(mddi);
	mddi_writel(mddi->int_enable, INTEN);
	mddi_writel(MDDI_CMD_LINK_ACTIVE, CMD);
	mddi_writel(MDDI_CMD_SEND_RTD, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	mddi_set_auto_hibernate(&mddi->client_data, 1);
}

static int mddi_get_client_caps(struct mddi_info *mddi)
{
	int i, j;

	/* clear any stale interrupts */
	mddi_writel(0xffffffff, INT);

	mddi->int_enable = MDDI_INT_LINK_ACTIVE |
			   MDDI_INT_IN_HIBERNATION |
			   MDDI_INT_PRI_LINK_LIST_DONE |
			   MDDI_INT_REV_DATA_AVAIL |
			   MDDI_INT_REV_OVERFLOW |
			   MDDI_INT_REV_OVERWRITE |
			   MDDI_INT_RTD_FAILURE;
	mddi_writel(mddi->int_enable, INTEN);

	mddi_writel(MDDI_CMD_LINK_ACTIVE, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);

	for (j = 0; j < 3; j++) {
		/* the toshiba vga panel does not respond to get
		 * caps unless you SEND_RTD, but the first SEND_RTD
		 * will fail...
		 */
		for (i = 0; i < 4; i++) {
			uint32_t stat;

			mddi_writel(MDDI_CMD_SEND_RTD, CMD);
			mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
			stat = mddi_readl(STAT);
			printk(KERN_INFO "mddi cmd send rtd: int %x, stat %x, "
					"rtd val %x\n", mddi_readl(INT), stat,
					mddi_readl(RTD_VAL));
			if ((stat & MDDI_STAT_RTD_MEAS_FAIL) == 0)
				break;
			msleep(1);
		}

		mddi_writel(CMD_GET_CLIENT_CAP, CMD);
		mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
		wait_event_timeout(mddi->int_wait, mddi->flags & FLAG_HAVE_CAPS,
				   HZ / 100);

		if (mddi->flags & FLAG_HAVE_CAPS)
			break;
		printk(KERN_INFO "mddi_init, timeout waiting for caps\n");
	}
	return mddi->flags & FLAG_HAVE_CAPS;
}

/* link must be active when this is called */
int mddi_check_status(struct mddi_info *mddi)
{
	int ret = -1, retry = 3;
	mutex_lock(&mddi->reg_read_lock);
	mddi_writel(MDDI_CMD_PERIODIC_REV_ENCAP | 1, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);

	do {
		mddi->flags &= ~FLAG_HAVE_STATUS;
		mddi_writel(CMD_GET_CLIENT_STATUS, CMD);
		mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
		wait_event_timeout(mddi->int_wait,
				   mddi->flags & FLAG_HAVE_STATUS,
				   HZ / 100);

		if (mddi->flags & FLAG_HAVE_STATUS) {
			if (mddi->status.crc_error_count)
				printk(KERN_INFO "mddi status: crc_error "
					"count: %d\n",
					mddi->status.crc_error_count);
			else
				ret = 0;
			break;
		} else
			printk(KERN_INFO "mddi status: failed to get client "
				"status\n");
		mddi_writel(MDDI_CMD_SEND_RTD, CMD);
		mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	} while (--retry);

	mddi_writel(MDDI_CMD_PERIODIC_REV_ENCAP | 0, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	mutex_unlock(&mddi->reg_read_lock);
	return ret;
}


void mddi_remote_write(struct msm_mddi_client_data *cdata, uint32_t val,
		       uint32_t reg)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	struct mddi_llentry *ll;
	struct mddi_register_access *ra;

	mutex_lock(&mddi->reg_write_lock);

	ll = mddi->reg_write_data;

	ra = &(ll->u.r);
	ra->length = 14 + 4;
	ra->type = TYPE_REGISTER_ACCESS;
	ra->client_id = 0;
	ra->read_write_info = MDDI_WRITE | 1;
	ra->crc16 = 0;

	ra->register_address = reg;
	ra->register_data_list = val;

	ll->flags = 1;
	ll->header_count = 14;
	ll->data_count = 4;
	ll->data = mddi->reg_write_addr + offsetof(struct mddi_llentry,
						   u.r.register_data_list);
	ll->next = 0;
	ll->reserved = 0;

	mddi_writel(mddi->reg_write_addr, PRI_PTR);

	mddi_wait_interrupt(mddi, MDDI_INT_PRI_LINK_LIST_DONE);
	mutex_unlock(&mddi->reg_write_lock);
}

uint32_t mddi_remote_read(struct msm_mddi_client_data *cdata, uint32_t reg)
{
	struct mddi_info *mddi = container_of(cdata, struct mddi_info,
					      client_data);
	struct mddi_llentry *ll;
	struct mddi_register_access *ra;
	struct reg_read_info ri;
	unsigned s;
	int retry_count = 2;
	unsigned long irq_flags;

	mutex_lock(&mddi->reg_read_lock);

	ll = mddi->reg_read_data;

	ra = &(ll->u.r);
	ra->length = 14;
	ra->type = TYPE_REGISTER_ACCESS;
	ra->client_id = 0;
	ra->read_write_info = MDDI_READ | 1;
	ra->crc16 = 0;

	ra->register_address = reg;

	ll->flags = 0x11;
	ll->header_count = 14;
	ll->data_count = 0;
	ll->data = 0;
	ll->next = 0;
	ll->reserved = 0;

	s = mddi_readl(STAT);

	ri.reg = reg;
	ri.status = -1;

	do {
		init_completion(&ri.done);
		mddi->reg_read = &ri;
		mddi_writel(mddi->reg_read_addr, PRI_PTR);

		mddi_wait_interrupt(mddi, MDDI_INT_PRI_LINK_LIST_DONE);

		/* Enable Periodic Reverse Encapsulation. */
		mddi_writel(MDDI_CMD_PERIODIC_REV_ENCAP | 1, CMD);
		mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
		if (wait_for_completion_timeout(&ri.done, HZ/10) == 0 &&
		    !ri.done.done) {
			printk(KERN_INFO "mddi_remote_read(%x) timeout "
					 "(%d %d %d)\n",
			       reg, ri.status, ri.result, ri.done.done);
			spin_lock_irqsave(&mddi->int_lock, irq_flags);
			mddi->reg_read = NULL;
			spin_unlock_irqrestore(&mddi->int_lock, irq_flags);
			ri.status = -1;
			ri.result = -1;
		}
		if (ri.status == 0)
			break;

		mddi_writel(MDDI_CMD_SEND_RTD, CMD);
		mddi_writel(MDDI_CMD_LINK_ACTIVE, CMD);
		mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
		printk(KERN_INFO "mddi_remote_read: failed, sent "
		       "MDDI_CMD_SEND_RTD: int %x, stat %x, rtd val %x "
		       "curr_rev_ptr %x\n", mddi_readl(INT), mddi_readl(STAT),
		       mddi_readl(RTD_VAL), mddi_readl(CURR_REV_PTR));
	} while (retry_count-- > 0);
	/* Disable Periodic Reverse Encapsulation. */
	mddi_writel(MDDI_CMD_PERIODIC_REV_ENCAP | 0, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	mddi->reg_read = NULL;
	mutex_unlock(&mddi->reg_read_lock);
	return ri.result;
}

static struct mddi_info mddi_info[2];

static int mddi_clk_setup(struct platform_device *pdev, struct mddi_info *mddi,
			  unsigned long clk_rate)
{
	int ret;

	/* set up the clocks */
	mddi->clk = clk_get(&pdev->dev, "mddi_clk");
	if (IS_ERR(mddi->clk)) {
		printk(KERN_INFO "mddi: failed to get clock\n");
		return PTR_ERR(mddi->clk);
	}
	ret =  clk_enable(mddi->clk);
	if (ret)
		goto fail;
	ret = clk_set_rate(mddi->clk, clk_rate);
	if (ret)
		goto fail;
	return 0;

fail:
	clk_put(mddi->clk);
	return ret;
}

static int __init mddi_rev_data_setup(struct mddi_info *mddi)
{
	void *dma;
	dma_addr_t dma_addr;

	/* set up dma buffer */
	dma = dma_alloc_coherent(NULL, 0x1000, &dma_addr, GFP_KERNEL);
	if (dma == 0)
		return -ENOMEM;
	mddi->rev_data = dma;
	mddi->rev_data_curr = 0;
	mddi->rev_addr = dma_addr;
	mddi->reg_write_data = dma + MDDI_REV_BUFFER_SIZE;
	mddi->reg_write_addr = dma_addr + MDDI_REV_BUFFER_SIZE;
	mddi->reg_read_data = mddi->reg_write_data + 1;
	mddi->reg_read_addr = mddi->reg_write_addr +
			      sizeof(*mddi->reg_write_data);
	return 0;
}

static int mddi_probe(struct platform_device *pdev)
{
	struct msm_mddi_platform_data *pdata = pdev->dev.platform_data;
	struct mddi_info *mddi = &mddi_info[pdev->id];
	struct resource *resource;
	int ret, i;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		printk(KERN_ERR "mddi: no associated mem resource!\n");
		return -ENOMEM;
	}
	mddi->base = ioremap(resource->start, resource_size(resource));
	if (!mddi->base) {
		printk(KERN_ERR "mddi: failed to remap base!\n");
		ret = -EINVAL;
		goto error_ioremap;
	}
	resource = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!resource) {
		printk(KERN_ERR "mddi: no associated irq resource!\n");
		ret = -EINVAL;
		goto error_get_irq_resource;
	}
	mddi->irq = resource->start;
	printk(KERN_INFO "mddi: init() base=0x%p irq=%d\n", mddi->base,
	       mddi->irq);
	mddi->power_client = pdata->power_client;

	mutex_init(&mddi->reg_write_lock);
	mutex_init(&mddi->reg_read_lock);
	spin_lock_init(&mddi->int_lock);
	init_waitqueue_head(&mddi->int_wait);

	ret = mddi_clk_setup(pdev, mddi, pdata->clk_rate);
	if (ret) {
		printk(KERN_ERR "mddi: failed to setup clock!\n");
		goto error_clk_setup;
	}

	ret = mddi_rev_data_setup(mddi);
	if (ret) {
		printk(KERN_ERR "mddi: failed to setup rev data!\n");
		goto error_rev_data;
	}

	mddi->int_enable = 0;
	mddi_writel(mddi->int_enable, INTEN);
	ret = request_irq(mddi->irq, mddi_isr, 0, "mddi",
			  &mddi->client_data);
	if (ret) {
		printk(KERN_ERR "mddi: failed to request enable irq!\n");
		goto error_request_irq;
	}

	/* turn on the mddi client bridge chip */
	if (mddi->power_client)
		mddi->power_client(&mddi->client_data, 1);

	/* initialize the mddi registers */
	mddi_set_auto_hibernate(&mddi->client_data, 0);
	mddi_writel(MDDI_CMD_RESET, CMD);
	mddi_wait_interrupt(mddi, MDDI_INT_NO_CMD_PKTS_PEND);
	mddi->version = mddi_init_registers(mddi);
	if (mddi->version < 0x20) {
		printk(KERN_ERR "mddi: unsupported version 0x%x\n",
		       mddi->version);
		ret = -ENODEV;
		goto error_mddi_version;
	}

	/* read the capabilities off the client */
	if (!mddi_get_client_caps(mddi)) {
		printk(KERN_INFO "mddi: no client found\n");
		/* power down the panel */
		mddi_writel(MDDI_CMD_POWERDOWN, CMD);
		printk(KERN_INFO "mddi powerdown: stat %x\n", mddi_readl(STAT));
		msleep(100);
		printk(KERN_INFO "mddi powerdown: stat %x\n", mddi_readl(STAT));
		return 0;
	}
	mddi_set_auto_hibernate(&mddi->client_data, 1);

	if (mddi->caps.Mfr_Name == 0 && mddi->caps.Product_Code == 0)
		pdata->fixup(&mddi->caps.Mfr_Name, &mddi->caps.Product_Code);

	mddi->client_pdev.id = 0;
	for (i = 0; i < pdata->num_clients; i++) {
		if (pdata->client_platform_data[i].product_id ==
		    (mddi->caps.Mfr_Name << 16 | mddi->caps.Product_Code)) {
			mddi->client_data.private_client_data =
				pdata->client_platform_data[i].client_data;
			mddi->client_pdev.name =
				pdata->client_platform_data[i].name;
			mddi->client_pdev.id =
				pdata->client_platform_data[i].id;
			/* XXX: possibly set clock */
			break;
		}
	}

	if (i >= pdata->num_clients)
		mddi->client_pdev.name = "mddi_c_dummy";
	printk(KERN_INFO "mddi: registering panel %s\n",
		mddi->client_pdev.name);

	mddi->client_data.suspend = mddi_suspend;
	mddi->client_data.resume = mddi_resume;
	mddi->client_data.activate_link = mddi_activate_link;
	mddi->client_data.remote_write = mddi_remote_write;
	mddi->client_data.remote_read = mddi_remote_read;
	mddi->client_data.auto_hibernate = mddi_set_auto_hibernate;
	mddi->client_data.fb_resource = pdata->fb_resource;
	if (pdev->id == 0)
		mddi->client_data.interface_type = MSM_MDDI_PMDH_INTERFACE;
	else if (pdev->id == 1)
		mddi->client_data.interface_type = MSM_MDDI_EMDH_INTERFACE;
	else {
		printk(KERN_ERR "mddi: can not determine interface %d!\n",
		       pdev->id);
		ret = -EINVAL;
		goto error_mddi_interface;
	}

	mddi->client_pdev.dev.platform_data = &mddi->client_data;
	printk(KERN_INFO "mddi: publish: %s\n", mddi->client_name);
	platform_device_register(&mddi->client_pdev);
	return 0;

error_mddi_interface:
error_mddi_version:
	free_irq(mddi->irq, 0);
error_request_irq:
	dma_free_coherent(NULL, 0x1000, mddi->rev_data, mddi->rev_addr);
error_rev_data:
error_clk_setup:
error_get_irq_resource:
	iounmap(mddi->base);
error_ioremap:

	printk(KERN_INFO "mddi: mddi_init() failed (%d)\n", ret);
	return ret;
}


static struct platform_driver mddi_driver = {
	.probe = mddi_probe,
	.driver = { .name = "msm_mddi" },
};

static int __init _mddi_init(void)
{
	return platform_driver_register(&mddi_driver);
}

module_init(_mddi_init);
