/*
 * drivers/video/pnx4008/sdum.c
 *
 * Display Update Master support
 *
 * Authors: Grigory Tolstolytkin <gtolstolytkin@ru.mvista.com>
 *          Vitaly Wool <vitalywool@gmail.com>
 * Based on Philips Semiconductors's code
 *
 * Copyrght (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2005 Philips Semiconductors
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>
#include <asm/gpio.h>

#include "sdum.h"
#include "fbcommon.h"
#include "dum.h"

/* Framebuffers we have */

static struct pnx4008_fb_addr {
	int fb_type;
	long addr_offset;
	long fb_length;
} fb_addr[] = {
	[0] = {
		FB_TYPE_YUV, 0, 0xB0000
	},
	[1] = {
		FB_TYPE_RGB, 0xB0000, 0x50000
	},
};

static struct dum_data {
	u32 lcd_phys_start;
	u32 lcd_virt_start;
	u32 slave_phys_base;
	u32 *slave_virt_base;
	int fb_owning_channel[MAX_DUM_CHANNELS];
	struct dumchannel_uf chan_uf_store[MAX_DUM_CHANNELS];
} dum_data;

/* Different local helper functions */

static u32 nof_pixels_dx(struct dum_ch_setup *ch_setup)
{
	return (ch_setup->xmax - ch_setup->xmin + 1);
}

static u32 nof_pixels_dy(struct dum_ch_setup *ch_setup)
{
	return (ch_setup->ymax - ch_setup->ymin + 1);
}

static u32 nof_pixels_dxy(struct dum_ch_setup *ch_setup)
{
	return (nof_pixels_dx(ch_setup) * nof_pixels_dy(ch_setup));
}

static u32 nof_bytes(struct dum_ch_setup *ch_setup)
{
	u32 r = nof_pixels_dxy(ch_setup);
	switch (ch_setup->format) {
	case RGB888:
	case RGB666:
		r *= 4;
		break;

	default:
		r *= 2;
		break;
	}
	return r;
}

static u32 build_command(int disp_no, u32 reg, u32 val)
{
	return ((disp_no << 26) | BIT(25) | (val << 16) | (disp_no << 10) |
		(reg << 0));
}

static u32 build_double_index(int disp_no, u32 val)
{
	return ((disp_no << 26) | (val << 16) | (disp_no << 10) | (val << 0));
}

static void build_disp_window(struct dum_ch_setup * ch_setup, struct disp_window * dw)
{
	dw->ymin = ch_setup->ymin;
	dw->ymax = ch_setup->ymax;
	dw->xmin_l = ch_setup->xmin & 0xFF;
	dw->xmin_h = (ch_setup->xmin & BIT(8)) >> 8;
	dw->xmax_l = ch_setup->xmax & 0xFF;
	dw->xmax_h = (ch_setup->xmax & BIT(8)) >> 8;
}

static int put_channel(struct dumchannel chan)
{
	int i = chan.channelnr;

	if (i < 0 || i > MAX_DUM_CHANNELS)
		return -EINVAL;
	else {
		DUM_CH_MIN(i) = chan.dum_ch_min;
		DUM_CH_MAX(i) = chan.dum_ch_max;
		DUM_CH_CONF(i) = chan.dum_ch_conf;
		DUM_CH_CTRL(i) = chan.dum_ch_ctrl;
	}

	return 0;
}

static void clear_channel(int channr)
{
	struct dumchannel chan;

	chan.channelnr = channr;
	chan.dum_ch_min = 0;
	chan.dum_ch_max = 0;
	chan.dum_ch_conf = 0;
	chan.dum_ch_ctrl = 0;

	put_channel(chan);
}

static int put_cmd_string(struct cmdstring cmds)
{
	u16 *cmd_str_virtaddr;
	u32 *cmd_ptr0_virtaddr;
	u32 cmd_str_physaddr;

	int i = cmds.channelnr;

	if (i < 0 || i > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if ((cmd_ptr0_virtaddr =
		  (int *)ioremap_nocache(DUM_COM_BASE,
					 sizeof(int) * MAX_DUM_CHANNELS)) ==
		 NULL)
		return -EIOREMAPFAILED;
	else {
		cmd_str_physaddr = ioread32(&cmd_ptr0_virtaddr[cmds.channelnr]);
		if ((cmd_str_virtaddr =
		     (u16 *) ioremap_nocache(cmd_str_physaddr,
					     sizeof(cmds))) == NULL) {
			iounmap(cmd_ptr0_virtaddr);
			return -EIOREMAPFAILED;
		} else {
			int t;
			for (t = 0; t < 8; t++)
				iowrite16(*((u16 *)&cmds.prestringlen + t),
					  cmd_str_virtaddr + t);

			for (t = 0; t < cmds.prestringlen / 2; t++)
				 iowrite16(*((u16 *)&cmds.precmd + t),
					   cmd_str_virtaddr + t + 8);

			for (t = 0; t < cmds.poststringlen / 2; t++)
				iowrite16(*((u16 *)&cmds.postcmd + t),
					  cmd_str_virtaddr + t + 8 +
					  	cmds.prestringlen / 2);

			iounmap(cmd_ptr0_virtaddr);
			iounmap(cmd_str_virtaddr);
		}
	}

	return 0;
}

static u32 dum_ch_setup(int ch_no, struct dum_ch_setup * ch_setup)
{
	struct cmdstring cmds_c;
	struct cmdstring *cmds = &cmds_c;
	struct disp_window dw;
	int standard;
	u32 orientation = 0;
	struct dumchannel chan = { 0 };
	int ret;

	if ((ch_setup->xmirror) || (ch_setup->ymirror) || (ch_setup->rotate)) {
		standard = 0;

		orientation = BIT(1);	/* always set 9-bit-bus */
		if (ch_setup->xmirror)
			orientation |= BIT(4);
		if (ch_setup->ymirror)
			orientation |= BIT(3);
		if (ch_setup->rotate)
			orientation |= BIT(0);
	} else
		standard = 1;

	cmds->channelnr = ch_no;

	/* build command string header */
	if (standard) {
		cmds->prestringlen = 32;
		cmds->poststringlen = 0;
	} else {
		cmds->prestringlen = 48;
		cmds->poststringlen = 16;
	}

	cmds->format =
	    (u16) ((ch_setup->disp_no << 4) | (BIT(3)) | (ch_setup->format));
	cmds->reserved = 0x0;
	cmds->startaddr_low = (ch_setup->minadr & 0xFFFF);
	cmds->startaddr_high = (ch_setup->minadr >> 16);

	if ((ch_setup->minadr == 0) && (ch_setup->maxadr == 0)
	    && (ch_setup->xmin == 0)
	    && (ch_setup->ymin == 0) && (ch_setup->xmax == 0)
	    && (ch_setup->ymax == 0)) {
		cmds->pixdatlen_low = 0;
		cmds->pixdatlen_high = 0;
	} else {
		u32 nbytes = nof_bytes(ch_setup);
		cmds->pixdatlen_low = (nbytes & 0xFFFF);
		cmds->pixdatlen_high = (nbytes >> 16);
	}

	if (ch_setup->slave_trans)
		cmds->pixdatlen_high |= BIT(15);

	/* build pre-string */
	build_disp_window(ch_setup, &dw);

	if (standard) {
		cmds->precmd[0] =
		    build_command(ch_setup->disp_no, DISP_XMIN_L_REG, 0x99);
		cmds->precmd[1] =
		    build_command(ch_setup->disp_no, DISP_XMIN_L_REG,
				  dw.xmin_l);
		cmds->precmd[2] =
		    build_command(ch_setup->disp_no, DISP_XMIN_H_REG,
				  dw.xmin_h);
		cmds->precmd[3] =
		    build_command(ch_setup->disp_no, DISP_YMIN_REG, dw.ymin);
		cmds->precmd[4] =
		    build_command(ch_setup->disp_no, DISP_XMAX_L_REG,
				  dw.xmax_l);
		cmds->precmd[5] =
		    build_command(ch_setup->disp_no, DISP_XMAX_H_REG,
				  dw.xmax_h);
		cmds->precmd[6] =
		    build_command(ch_setup->disp_no, DISP_YMAX_REG, dw.ymax);
		cmds->precmd[7] =
		    build_double_index(ch_setup->disp_no, DISP_PIXEL_REG);
	} else {
		if (dw.xmin_l == ch_no)
			cmds->precmd[0] =
			    build_command(ch_setup->disp_no, DISP_XMIN_L_REG,
					  0x99);
		else
			cmds->precmd[0] =
			    build_command(ch_setup->disp_no, DISP_XMIN_L_REG,
					  ch_no);

		cmds->precmd[1] =
		    build_command(ch_setup->disp_no, DISP_XMIN_L_REG,
				  dw.xmin_l);
		cmds->precmd[2] =
		    build_command(ch_setup->disp_no, DISP_XMIN_H_REG,
				  dw.xmin_h);
		cmds->precmd[3] =
		    build_command(ch_setup->disp_no, DISP_YMIN_REG, dw.ymin);
		cmds->precmd[4] =
		    build_command(ch_setup->disp_no, DISP_XMAX_L_REG,
				  dw.xmax_l);
		cmds->precmd[5] =
		    build_command(ch_setup->disp_no, DISP_XMAX_H_REG,
				  dw.xmax_h);
		cmds->precmd[6] =
		    build_command(ch_setup->disp_no, DISP_YMAX_REG, dw.ymax);
		cmds->precmd[7] =
		    build_command(ch_setup->disp_no, DISP_1_REG, orientation);
		cmds->precmd[8] =
		    build_double_index(ch_setup->disp_no, DISP_PIXEL_REG);
		cmds->precmd[9] =
		    build_double_index(ch_setup->disp_no, DISP_PIXEL_REG);
		cmds->precmd[0xA] =
		    build_double_index(ch_setup->disp_no, DISP_PIXEL_REG);
		cmds->precmd[0xB] =
		    build_double_index(ch_setup->disp_no, DISP_PIXEL_REG);
		cmds->postcmd[0] =
		    build_command(ch_setup->disp_no, DISP_1_REG, BIT(1));
		cmds->postcmd[1] =
		    build_command(ch_setup->disp_no, DISP_DUMMY1_REG, 1);
		cmds->postcmd[2] =
		    build_command(ch_setup->disp_no, DISP_DUMMY1_REG, 2);
		cmds->postcmd[3] =
		    build_command(ch_setup->disp_no, DISP_DUMMY1_REG, 3);
	}

	if ((ret = put_cmd_string(cmds_c)) != 0) {
		return ret;
	}

	chan.channelnr = cmds->channelnr;
	chan.dum_ch_min = ch_setup->dirtybuffer + ch_setup->minadr;
	chan.dum_ch_max = ch_setup->dirtybuffer + ch_setup->maxadr;
	chan.dum_ch_conf = 0x002;
	chan.dum_ch_ctrl = 0x04;

	put_channel(chan);

	return 0;
}

static u32 display_open(int ch_no, int auto_update, u32 * dirty_buffer,
			u32 * frame_buffer, u32 xpos, u32 ypos, u32 w, u32 h)
{

	struct dum_ch_setup k;
	int ret;

	/* keep width & height within display area */
	if ((xpos + w) > DISP_MAX_X_SIZE)
		w = DISP_MAX_X_SIZE - xpos;

	if ((ypos + h) > DISP_MAX_Y_SIZE)
		h = DISP_MAX_Y_SIZE - ypos;

	/* assume 1 display only */
	k.disp_no = 0;
	k.xmin = xpos;
	k.ymin = ypos;
	k.xmax = xpos + (w - 1);
	k.ymax = ypos + (h - 1);

	/* adjust min and max values if necessary */
	if (k.xmin > DISP_MAX_X_SIZE - 1)
		k.xmin = DISP_MAX_X_SIZE - 1;
	if (k.ymin > DISP_MAX_Y_SIZE - 1)
		k.ymin = DISP_MAX_Y_SIZE - 1;

	if (k.xmax > DISP_MAX_X_SIZE - 1)
		k.xmax = DISP_MAX_X_SIZE - 1;
	if (k.ymax > DISP_MAX_Y_SIZE - 1)
		k.ymax = DISP_MAX_Y_SIZE - 1;

	k.xmirror = 0;
	k.ymirror = 0;
	k.rotate = 0;
	k.minadr = (u32) frame_buffer;
	k.maxadr = (u32) frame_buffer + (((w - 1) << 10) | ((h << 2) - 2));
	k.pad = PAD_1024;
	k.dirtybuffer = (u32) dirty_buffer;
	k.format = RGB888;
	k.hwdirty = 0;
	k.slave_trans = 0;

	ret = dum_ch_setup(ch_no, &k);

	return ret;
}

static void lcd_reset(void)
{
	u32 *dum_pio_base = (u32 *)IO_ADDRESS(PNX4008_PIO_BASE);

	udelay(1);
	iowrite32(BIT(19), &dum_pio_base[2]);
	udelay(1);
	iowrite32(BIT(19), &dum_pio_base[1]);
	udelay(1);
}

static int dum_init(struct platform_device *pdev)
{
	struct clk *clk;

	/* enable DUM clock */
	clk = clk_get(&pdev->dev, "dum_ck");
	if (IS_ERR(clk)) {
		printk(KERN_ERR "pnx4008_dum: Unable to access DUM clock\n");
		return PTR_ERR(clk);
	}

	clk_set_rate(clk, 1);
	clk_put(clk);

	DUM_CTRL = V_DUM_RESET;

	/* set priority to "round-robin". All other params to "false" */
	DUM_CONF = BIT(9);

	/* Display 1 */
	DUM_WTCFG1 = PNX4008_DUM_WT_CFG;
	DUM_RTCFG1 = PNX4008_DUM_RT_CFG;
	DUM_TCFG = PNX4008_DUM_T_CFG;

	return 0;
}

static void dum_chan_init(void)
{
	int i = 0, ch = 0;
	u32 *cmdptrs;
	u32 *cmdstrings;

	DUM_COM_BASE =
		CMDSTRING_BASEADDR + BYTES_PER_CMDSTRING * NR_OF_CMDSTRINGS;

	if ((cmdptrs =
	     (u32 *) ioremap_nocache(DUM_COM_BASE,
				     sizeof(u32) * NR_OF_CMDSTRINGS)) == NULL)
		return;

	for (ch = 0; ch < NR_OF_CMDSTRINGS; ch++)
		iowrite32(CMDSTRING_BASEADDR + BYTES_PER_CMDSTRING * ch,
			  cmdptrs + ch);

	for (ch = 0; ch < MAX_DUM_CHANNELS; ch++)
		clear_channel(ch);

	/* Clear the cmdstrings */
	cmdstrings =
	    (u32 *)ioremap_nocache(*cmdptrs,
				   BYTES_PER_CMDSTRING * NR_OF_CMDSTRINGS);

	if (!cmdstrings)
		goto out;

	for (i = 0; i < NR_OF_CMDSTRINGS * BYTES_PER_CMDSTRING / sizeof(u32);
	     i++)
		iowrite32(0, cmdstrings + i);

	iounmap((u32 *)cmdstrings);

out:
	iounmap((u32 *)cmdptrs);
}

static void lcd_init(void)
{
	lcd_reset();

	DUM_OUTP_FORMAT1 = 0; /* RGB666 */

	udelay(1);
	iowrite32(V_LCD_STANDBY_OFF, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_USE_9BIT_BUS, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_SYNC_RISE_L, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_SYNC_RISE_H, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_SYNC_FALL_L, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_SYNC_FALL_H, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_SYNC_ENABLE, dum_data.slave_virt_base);
	udelay(1);
	iowrite32(V_LCD_DISPLAY_ON, dum_data.slave_virt_base);
	udelay(1);
}

/* Interface exported to framebuffer drivers */

int pnx4008_get_fb_addresses(int fb_type, void **virt_addr,
			     dma_addr_t *phys_addr, int *fb_length)
{
	int i;
	int ret = -1;
	for (i = 0; i < ARRAY_SIZE(fb_addr); i++)
		if (fb_addr[i].fb_type == fb_type) {
			*virt_addr = (void *)(dum_data.lcd_virt_start +
					fb_addr[i].addr_offset);
			*phys_addr =
			    dum_data.lcd_phys_start + fb_addr[i].addr_offset;
			*fb_length = fb_addr[i].fb_length;
			ret = 0;
			break;
		}

	return ret;
}

EXPORT_SYMBOL(pnx4008_get_fb_addresses);

int pnx4008_alloc_dum_channel(int dev_id)
{
	int i = 0;

	while ((i < MAX_DUM_CHANNELS) && (dum_data.fb_owning_channel[i] != -1))
		i++;

	if (i == MAX_DUM_CHANNELS)
		return -ENORESOURCESLEFT;
	else {
		dum_data.fb_owning_channel[i] = dev_id;
		return i;
	}
}

EXPORT_SYMBOL(pnx4008_alloc_dum_channel);

int pnx4008_free_dum_channel(int channr, int dev_id)
{
	if (channr < 0 || channr > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[channr] != dev_id)
		return -EFBNOTOWNER;
	else {
		clear_channel(channr);
		dum_data.fb_owning_channel[channr] = -1;
	}

	return 0;
}

EXPORT_SYMBOL(pnx4008_free_dum_channel);

int pnx4008_put_dum_channel_uf(struct dumchannel_uf chan_uf, int dev_id)
{
	int i = chan_uf.channelnr;
	int ret;

	if (i < 0 || i > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[i] != dev_id)
		return -EFBNOTOWNER;
	else if ((ret =
		  display_open(chan_uf.channelnr, 0, chan_uf.dirty,
			       chan_uf.source, chan_uf.y_offset,
			       chan_uf.x_offset, chan_uf.height,
			       chan_uf.width)) != 0)
		return ret;
	else {
		dum_data.chan_uf_store[i].dirty = chan_uf.dirty;
		dum_data.chan_uf_store[i].source = chan_uf.source;
		dum_data.chan_uf_store[i].x_offset = chan_uf.x_offset;
		dum_data.chan_uf_store[i].y_offset = chan_uf.y_offset;
		dum_data.chan_uf_store[i].width = chan_uf.width;
		dum_data.chan_uf_store[i].height = chan_uf.height;
	}

	return 0;
}

EXPORT_SYMBOL(pnx4008_put_dum_channel_uf);

int pnx4008_set_dum_channel_sync(int channr, int val, int dev_id)
{
	if (channr < 0 || channr > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[channr] != dev_id)
		return -EFBNOTOWNER;
	else {
		if (val == CONF_SYNC_ON) {
			DUM_CH_CONF(channr) |= CONF_SYNCENABLE;
			DUM_CH_CONF(channr) |= DUM_CHANNEL_CFG_SYNC_MASK |
				DUM_CHANNEL_CFG_SYNC_MASK_SET;
		} else if (val == CONF_SYNC_OFF)
			DUM_CH_CONF(channr) &= ~CONF_SYNCENABLE;
		else
			return -EINVAL;
	}

	return 0;
}

EXPORT_SYMBOL(pnx4008_set_dum_channel_sync);

int pnx4008_set_dum_channel_dirty_detect(int channr, int val, int dev_id)
{
	if (channr < 0 || channr > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[channr] != dev_id)
		return -EFBNOTOWNER;
	else {
		if (val == CONF_DIRTYDETECTION_ON)
			DUM_CH_CONF(channr) |= CONF_DIRTYENABLE;
		else if (val == CONF_DIRTYDETECTION_OFF)
			DUM_CH_CONF(channr) &= ~CONF_DIRTYENABLE;
		else
			return -EINVAL;
	}

	return 0;
}

EXPORT_SYMBOL(pnx4008_set_dum_channel_dirty_detect);

#if 0 /* Functions not used currently, but likely to be used in future */

static int get_channel(struct dumchannel *p_chan)
{
	int i = p_chan->channelnr;

	if (i < 0 || i > MAX_DUM_CHANNELS)
		return -EINVAL;
	else {
		p_chan->dum_ch_min = DUM_CH_MIN(i);
		p_chan->dum_ch_max = DUM_CH_MAX(i);
		p_chan->dum_ch_conf = DUM_CH_CONF(i);
		p_chan->dum_ch_stat = DUM_CH_STAT(i);
		p_chan->dum_ch_ctrl = 0;	/* WriteOnly control register */
	}

	return 0;
}

int pnx4008_get_dum_channel_uf(struct dumchannel_uf *p_chan_uf, int dev_id)
{
	int i = p_chan_uf->channelnr;

	if (i < 0 || i > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[i] != dev_id)
		return -EFBNOTOWNER;
	else {
		p_chan_uf->dirty = dum_data.chan_uf_store[i].dirty;
		p_chan_uf->source = dum_data.chan_uf_store[i].source;
		p_chan_uf->x_offset = dum_data.chan_uf_store[i].x_offset;
		p_chan_uf->y_offset = dum_data.chan_uf_store[i].y_offset;
		p_chan_uf->width = dum_data.chan_uf_store[i].width;
		p_chan_uf->height = dum_data.chan_uf_store[i].height;
	}

	return 0;
}

EXPORT_SYMBOL(pnx4008_get_dum_channel_uf);

int pnx4008_get_dum_channel_config(int channr, int dev_id)
{
	int ret;
	struct dumchannel chan;

	if (channr < 0 || channr > MAX_DUM_CHANNELS)
		return -EINVAL;
	else if (dum_data.fb_owning_channel[channr] != dev_id)
		return -EFBNOTOWNER;
	else {
		chan.channelnr = channr;
		if ((ret = get_channel(&chan)) != 0)
			return ret;
	}

	return (chan.dum_ch_conf & DUM_CHANNEL_CFG_MASK);
}

EXPORT_SYMBOL(pnx4008_get_dum_channel_config);

int pnx4008_force_update_dum_channel(int channr, int dev_id)
{
	if (channr < 0 || channr > MAX_DUM_CHANNELS)
		return -EINVAL;

	else if (dum_data.fb_owning_channel[channr] != dev_id)
		return -EFBNOTOWNER;
	else
		DUM_CH_CTRL(channr) = CTRL_SETDIRTY;

	return 0;
}

EXPORT_SYMBOL(pnx4008_force_update_dum_channel);

#endif

int pnx4008_sdum_mmap(struct fb_info *info, struct vm_area_struct *vma,
		      struct device *dev)
{
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;

	if (off < info->fix.smem_len) {
		vma->vm_pgoff += 1;
		return dma_mmap_writecombine(dev, vma,
				(void *)dum_data.lcd_virt_start,
				dum_data.lcd_phys_start,
				FB_DMA_SIZE);
	}
	return -EINVAL;
}

EXPORT_SYMBOL(pnx4008_sdum_mmap);

int pnx4008_set_dum_exit_notification(int dev_id)
{
	int i;

	for (i = 0; i < MAX_DUM_CHANNELS; i++)
		if (dum_data.fb_owning_channel[i] == dev_id)
			return -ERESOURCESNOTFREED;

	return 0;
}

EXPORT_SYMBOL(pnx4008_set_dum_exit_notification);

/* Platform device driver for DUM */

static int sdum_suspend(struct platform_device *pdev, pm_message_t state)
{
	int retval = 0;
	struct clk *clk;

	clk = clk_get(0, "dum_ck");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 0);
		clk_put(clk);
	} else
		retval = PTR_ERR(clk);

	/* disable BAC */
	DUM_CTRL = V_BAC_DISABLE_IDLE;

	/* LCD standby & turn off display */
	lcd_reset();

	return retval;
}

static int sdum_resume(struct platform_device *pdev)
{
	int retval = 0;
	struct clk *clk;

	clk = clk_get(0, "dum_ck");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 1);
		clk_put(clk);
	} else
		retval = PTR_ERR(clk);

	/* wait for BAC disable */
	DUM_CTRL = V_BAC_DISABLE_TRIG;

	while (DUM_CTRL & BAC_ENABLED)
		udelay(10);

	/* re-init LCD */
	lcd_init();

	/* enable BAC and reset MUX */
	DUM_CTRL = V_BAC_ENABLE;
	udelay(1);
	DUM_CTRL = V_MUX_RESET;
	return 0;
}

static int __devinit sdum_probe(struct platform_device *pdev)
{
	int ret = 0, i = 0;

	/* map frame buffer */
	dum_data.lcd_virt_start = (u32) dma_alloc_writecombine(&pdev->dev,
						       FB_DMA_SIZE,
						       &dum_data.lcd_phys_start,
						       GFP_KERNEL);

	if (!dum_data.lcd_virt_start) {
		ret = -ENOMEM;
		goto out_3;
	}

	/* map slave registers */
	dum_data.slave_phys_base = PNX4008_DUM_SLAVE_BASE;
	dum_data.slave_virt_base =
	    (u32 *) ioremap_nocache(dum_data.slave_phys_base, sizeof(u32));

	if (dum_data.slave_virt_base == NULL) {
		ret = -ENOMEM;
		goto out_2;
	}

	/* initialize DUM and LCD display */
	ret = dum_init(pdev);
	if (ret)
		goto out_1;

	dum_chan_init();
	lcd_init();

	DUM_CTRL = V_BAC_ENABLE;
	udelay(1);
	DUM_CTRL = V_MUX_RESET;

	/* set decode address and sync clock divider */
	DUM_DECODE = dum_data.lcd_phys_start & DUM_DECODE_MASK;
	DUM_CLK_DIV = PNX4008_DUM_CLK_DIV;

	for (i = 0; i < MAX_DUM_CHANNELS; i++)
		dum_data.fb_owning_channel[i] = -1;

	/*setup wakeup interrupt */
	start_int_set_rising_edge(SE_DISP_SYNC_INT);
	start_int_ack(SE_DISP_SYNC_INT);
	start_int_umask(SE_DISP_SYNC_INT);

	return 0;

out_1:
	iounmap((void *)dum_data.slave_virt_base);
out_2:
	dma_free_writecombine(&pdev->dev, FB_DMA_SIZE,
			(void *)dum_data.lcd_virt_start,
			dum_data.lcd_phys_start);
out_3:
	return ret;
}

static int sdum_remove(struct platform_device *pdev)
{
	struct clk *clk;

	start_int_mask(SE_DISP_SYNC_INT);

	clk = clk_get(0, "dum_ck");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 0);
		clk_put(clk);
	}

	iounmap((void *)dum_data.slave_virt_base);

	dma_free_writecombine(&pdev->dev, FB_DMA_SIZE,
			(void *)dum_data.lcd_virt_start,
			dum_data.lcd_phys_start);

	return 0;
}

static struct platform_driver sdum_driver = {
	.driver = {
		.name = "pnx4008-sdum",
	},
	.probe = sdum_probe,
	.remove = sdum_remove,
	.suspend = sdum_suspend,
	.resume = sdum_resume,
};

module_platform_driver(sdum_driver);

MODULE_LICENSE("GPL");
