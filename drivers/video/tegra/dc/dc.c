/*
 * drivers/video/tegra/dc/dc.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/nvhost.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <mach/clk.h>
#include <mach/dc.h>
#include <mach/fb.h>

#include "dc_reg.h"
#include "dc_priv.h"

struct tegra_dc_blend tegra_dc_blend_modes[][DC_N_WINDOWS] = {
	{{.nokey = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .one_win = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .two_win_x = BLEND(NOKEY, FIX, 0x00, 0x00),
	  .two_win_y = BLEND(NOKEY, DEPENDANT, 0x00, 0x00),
	  .three_win_xy = BLEND(NOKEY, FIX, 0x00, 0x00)},
	 {.nokey = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .one_win = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .two_win_x = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .two_win_y = BLEND(NOKEY, DEPENDANT, 0x00, 0x00),
	  .three_win_xy = BLEND(NOKEY, DEPENDANT, 0x00, 0x00)},
	 {.nokey = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .one_win = BLEND(NOKEY, FIX, 0xff, 0xff),
	  .two_win_x = BLEND(NOKEY, ALPHA, 0xff, 0xff),
	  .two_win_y = BLEND(NOKEY, ALPHA, 0xff, 0xff),
	  .three_win_xy = BLEND(NOKEY, ALPHA, 0xff, 0xff)}
	}
};

struct tegra_dc *tegra_dcs[TEGRA_MAX_DC];

DEFINE_MUTEX(tegra_dc_lock);

static inline int tegra_dc_fmt_bpp(int fmt)
{
	switch (fmt) {
	case TEGRA_WIN_FMT_P1:
		return 1;

	case TEGRA_WIN_FMT_P2:
		return 2;

	case TEGRA_WIN_FMT_P4:
		return 4;

	case TEGRA_WIN_FMT_P8:
		return 8;

	case TEGRA_WIN_FMT_B4G4R4A4:
	case TEGRA_WIN_FMT_B5G5R5A:
	case TEGRA_WIN_FMT_B5G6R5:
	case TEGRA_WIN_FMT_AB5G5R5:
		return 16;

	case TEGRA_WIN_FMT_B8G8R8A8:
	case TEGRA_WIN_FMT_R8G8B8A8:
	case TEGRA_WIN_FMT_B6x2G6x2R6x2A8:
	case TEGRA_WIN_FMT_R6x2G6x2B6x2A8:
		return 32;

	case TEGRA_WIN_FMT_YCbCr422:
	case TEGRA_WIN_FMT_YUV422:
	case TEGRA_WIN_FMT_YCbCr420P:
	case TEGRA_WIN_FMT_YUV420P:
	case TEGRA_WIN_FMT_YCbCr422P:
	case TEGRA_WIN_FMT_YUV422P:
	case TEGRA_WIN_FMT_YCbCr422R:
	case TEGRA_WIN_FMT_YUV422R:
	case TEGRA_WIN_FMT_YCbCr422RA:
	case TEGRA_WIN_FMT_YUV422RA:
		/* FIXME: need to know the bpp of these formats */
		return 0;
	}
	return 0;
}

#define DUMP_REG(a) do {			\
	snprintf(buff, sizeof(buff), "%-32s\t%03x\t%08lx\n", \
		 #a, a, tegra_dc_readl(dc, a));		      \
	print(data, buff);				      \
	} while (0)

static void _dump_regs(struct tegra_dc *dc, void *data,
		       void (* print)(void *data, const char *str))
{
	int i;
	char buff[256];

	DUMP_REG(DC_CMD_DISPLAY_COMMAND_OPTION0);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND);
	DUMP_REG(DC_CMD_SIGNAL_RAISE);
	DUMP_REG(DC_CMD_INT_STATUS);
	DUMP_REG(DC_CMD_INT_MASK);
	DUMP_REG(DC_CMD_INT_ENABLE);
	DUMP_REG(DC_CMD_INT_TYPE);
	DUMP_REG(DC_CMD_INT_POLARITY);
	DUMP_REG(DC_CMD_SIGNAL_RAISE1);
	DUMP_REG(DC_CMD_SIGNAL_RAISE2);
	DUMP_REG(DC_CMD_SIGNAL_RAISE3);
	DUMP_REG(DC_CMD_STATE_ACCESS);
	DUMP_REG(DC_CMD_STATE_CONTROL);
	DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_CMD_REG_ACT_CONTROL);

	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS0);
	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS1);
	DUMP_REG(DC_DISP_DISP_WIN_OPTIONS);
	DUMP_REG(DC_DISP_MEM_HIGH_PRIORITY);
	DUMP_REG(DC_DISP_MEM_HIGH_PRIORITY_TIMER);
	DUMP_REG(DC_DISP_DISP_TIMING_OPTIONS);
	DUMP_REG(DC_DISP_REF_TO_SYNC);
	DUMP_REG(DC_DISP_SYNC_WIDTH);
	DUMP_REG(DC_DISP_BACK_PORCH);
	DUMP_REG(DC_DISP_DISP_ACTIVE);
	DUMP_REG(DC_DISP_FRONT_PORCH);
	DUMP_REG(DC_DISP_H_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_D);
	DUMP_REG(DC_DISP_V_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE3_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE3_POSITION_A);
	DUMP_REG(DC_DISP_M0_CONTROL);
	DUMP_REG(DC_DISP_M1_CONTROL);
	DUMP_REG(DC_DISP_DI_CONTROL);
	DUMP_REG(DC_DISP_PP_CONTROL);
	DUMP_REG(DC_DISP_PP_SELECT_A);
	DUMP_REG(DC_DISP_PP_SELECT_B);
	DUMP_REG(DC_DISP_PP_SELECT_C);
	DUMP_REG(DC_DISP_PP_SELECT_D);
	DUMP_REG(DC_DISP_DISP_CLOCK_CONTROL);
	DUMP_REG(DC_DISP_DISP_INTERFACE_CONTROL);
	DUMP_REG(DC_DISP_DISP_COLOR_CONTROL);
	DUMP_REG(DC_DISP_SHIFT_CLOCK_OPTIONS);
	DUMP_REG(DC_DISP_DATA_ENABLE_OPTIONS);
	DUMP_REG(DC_DISP_SERIAL_INTERFACE_OPTIONS);
	DUMP_REG(DC_DISP_LCD_SPI_OPTIONS);
	DUMP_REG(DC_DISP_BORDER_COLOR);
	DUMP_REG(DC_DISP_COLOR_KEY0_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY0_UPPER);
	DUMP_REG(DC_DISP_COLOR_KEY1_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY1_UPPER);
	DUMP_REG(DC_DISP_CURSOR_FOREGROUND);
	DUMP_REG(DC_DISP_CURSOR_BACKGROUND);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR_NS);
	DUMP_REG(DC_DISP_CURSOR_POSITION);
	DUMP_REG(DC_DISP_CURSOR_POSITION_NS);
	DUMP_REG(DC_DISP_INIT_SEQ_CONTROL);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_A);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_B);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_C);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_D);
	DUMP_REG(DC_DISP_DC_MCCIF_FIFOCTRL);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0A_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0B_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0C_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY1B_HYST);
	DUMP_REG(DC_DISP_DAC_CRT_CTRL);
	DUMP_REG(DC_DISP_DISP_MISC_CONTROL);


	for (i = 0; i < 3; i++) {
		print(data, "\n");
		snprintf(buff, sizeof(buff), "WINDOW %c:\n", 'A' + i);
		print(data, buff);

		tegra_dc_writel(dc, WINDOW_A_SELECT << i,
				DC_CMD_DISPLAY_WINDOW_HEADER);
		DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
		DUMP_REG(DC_WIN_WIN_OPTIONS);
		DUMP_REG(DC_WIN_BYTE_SWAP);
		DUMP_REG(DC_WIN_BUFFER_CONTROL);
		DUMP_REG(DC_WIN_COLOR_DEPTH);
		DUMP_REG(DC_WIN_POSITION);
		DUMP_REG(DC_WIN_SIZE);
		DUMP_REG(DC_WIN_PRESCALED_SIZE);
		DUMP_REG(DC_WIN_H_INITIAL_DDA);
		DUMP_REG(DC_WIN_V_INITIAL_DDA);
		DUMP_REG(DC_WIN_DDA_INCREMENT);
		DUMP_REG(DC_WIN_LINE_STRIDE);
		DUMP_REG(DC_WIN_BUF_STRIDE);
		DUMP_REG(DC_WIN_BLEND_NOKEY);
		DUMP_REG(DC_WIN_BLEND_1WIN);
		DUMP_REG(DC_WIN_BLEND_2WIN_X);
		DUMP_REG(DC_WIN_BLEND_2WIN_Y);
		DUMP_REG(DC_WIN_BLEND_3WIN_XY);
		DUMP_REG(DC_WINBUF_START_ADDR);
		DUMP_REG(DC_WINBUF_ADDR_H_OFFSET);
		DUMP_REG(DC_WINBUF_ADDR_V_OFFSET);
	}
}

#undef DUMP_REG

#ifdef DEBUG
static void dump_regs_print(void *data, const char *str)
{
	struct tegra_dc *dc = data;
	dev_dbg(&dc->ndev->dev, "%s", str);
}

static void dump_regs(struct tegra_dc *dc)
{
	_dump_regs(dc, dc, dump_regs_print);
}
#else

static void dump_regs(struct tegra_dc *dc) {}

#endif

#ifdef CONFIG_DEBUG_FS

static void dbg_regs_print(void *data, const char *str)
{
	struct seq_file *s = data;

	seq_printf(s, "%s", str);
}

#undef DUMP_REG

static int dbg_dc_show(struct seq_file *s, void *unused)
{
	struct tegra_dc *dc = s->private;

	_dump_regs(dc, s, dbg_regs_print);

	return 0;
}


static int dbg_dc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_dc_show, inode->i_private);
}

static const struct file_operations dbg_fops = {
	.open		= dbg_dc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void tegra_dc_dbg_add(struct tegra_dc *dc)
{
	char name[32];

	snprintf(name, sizeof(name), "tegra_dc%d_regs", dc->ndev->id);
	(void) debugfs_create_file(name, S_IRUGO, NULL, dc, &dbg_fops);

}
#else
static void tegra_dc_dbg_add(struct tegra_dc *dc) {}

#endif


static int tegra_dc_add(struct tegra_dc *dc, int index)
{
	int ret = 0;

	mutex_lock(&tegra_dc_lock);
	if (index >= TEGRA_MAX_DC) {
		ret = -EINVAL;
		goto out;
	}

	if (tegra_dcs[index] != NULL) {
		ret = -EBUSY;
		goto out;
	}

	tegra_dcs[index] = dc;

out:
	mutex_unlock(&tegra_dc_lock);

	return ret;
}

struct tegra_dc *tegra_dc_get_dc(unsigned idx)
{
	if (idx < TEGRA_MAX_DC)
		return tegra_dcs[idx];
	else
		return NULL;
}
EXPORT_SYMBOL(tegra_dc_get_dc);

struct tegra_dc_win *tegra_dc_get_window(struct tegra_dc *dc, unsigned win)
{
	if (win >= dc->n_windows)
		return NULL;

	return &dc->windows[win];
}
EXPORT_SYMBOL(tegra_dc_get_window);

/* does not support updating windows on multiple dcs in one call */
int tegra_dc_update_windows(struct tegra_dc_win *windows[], int n)
{
	struct tegra_dc *dc;
	unsigned long update_mask = GENERAL_ACT_REQ;
	unsigned long val;
	int i;

	dc = windows[0]->dc;

	mutex_lock(&dc->lock);

	if (!dc->enabled) {
		mutex_unlock(&dc->lock);
		return -EFAULT;
	}

	for (i = 0; i < n; i++) {
		struct tegra_dc_win *win = windows[i];
		unsigned h_dda;
		unsigned v_dda;
		unsigned stride;

		tegra_dc_writel(dc, WINDOW_A_SELECT << win->idx,
				DC_CMD_DISPLAY_WINDOW_HEADER);

		update_mask |= WIN_A_ACT_REQ << win->idx;

		if (!(win->flags & TEGRA_WIN_FLAG_ENABLED)) {
			tegra_dc_writel(dc, 0, DC_WIN_WIN_OPTIONS);
			continue;
		}

		tegra_dc_writel(dc, win->fmt, DC_WIN_COLOR_DEPTH);
		tegra_dc_writel(dc, 0, DC_WIN_BYTE_SWAP);

		stride = win->w * tegra_dc_fmt_bpp(win->fmt) / 8;

		/* TODO: implement filter on settings */
		h_dda = (win->w * 0x1000) / (win->out_w - 1);
		v_dda = (win->h * 0x1000) / (win->out_h - 1);

		tegra_dc_writel(dc,
				V_POSITION(win->y) | H_POSITION(win->x),
				DC_WIN_POSITION);
		tegra_dc_writel(dc,
				V_SIZE(win->out_h) | H_SIZE(win->out_w),
				DC_WIN_SIZE);
		tegra_dc_writel(dc,
				V_PRESCALED_SIZE(win->out_h) |
				H_PRESCALED_SIZE(stride),
				DC_WIN_PRESCALED_SIZE);
		tegra_dc_writel(dc, 0, DC_WIN_H_INITIAL_DDA);
		tegra_dc_writel(dc, 0, DC_WIN_V_INITIAL_DDA);
		tegra_dc_writel(dc, V_DDA_INC(v_dda) | H_DDA_INC(h_dda),
				DC_WIN_DDA_INCREMENT);
		tegra_dc_writel(dc, stride, DC_WIN_LINE_STRIDE);
		tegra_dc_writel(dc, 0, DC_WIN_BUF_STRIDE);

		val = WIN_ENABLE;
		if (win->flags & TEGRA_WIN_FLAG_COLOR_EXPAND)
			val |= COLOR_EXPAND;
		tegra_dc_writel(dc, val, DC_WIN_WIN_OPTIONS);

		tegra_dc_writel(dc, (unsigned long)win->phys_addr,
				DC_WINBUF_START_ADDR);
		tegra_dc_writel(dc, 0, DC_WINBUF_ADDR_H_OFFSET);
		tegra_dc_writel(dc, 0, DC_WINBUF_ADDR_V_OFFSET);

		win->dirty = 1;

	}

	tegra_dc_writel(dc, update_mask << 8, DC_CMD_STATE_CONTROL);

	val = tegra_dc_readl(dc, DC_CMD_INT_ENABLE);
	val |= FRAME_END_INT;
	tegra_dc_writel(dc, val, DC_CMD_INT_ENABLE);

	val = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	val |= FRAME_END_INT;
	tegra_dc_writel(dc, val, DC_CMD_INT_MASK);

	tegra_dc_writel(dc, update_mask, DC_CMD_STATE_CONTROL);

	mutex_unlock(&dc->lock);

	return 0;
}
EXPORT_SYMBOL(tegra_dc_update_windows);

static bool tegra_dc_windows_are_clean(struct tegra_dc_win *windows[],
					     int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (windows[i]->dirty)
			return false;
	}

	return true;
}

/* does not support syncing windows on multiple dcs in one call */
int tegra_dc_sync_windows(struct tegra_dc_win *windows[], int n)
{
	if (n < 1 || n > DC_N_WINDOWS)
		return -EINVAL;

	if (!windows[0]->dc->enabled)
		return -EFAULT;

	return wait_event_interruptible_timeout(windows[0]->dc->wq,
					 tegra_dc_windows_are_clean(windows, n),
					 HZ);
}
EXPORT_SYMBOL(tegra_dc_sync_windows);

void tegra_dc_set_blending(struct tegra_dc *dc, struct tegra_dc_blend *blend)
{
	int i;

	for (i = 0; i < DC_N_WINDOWS; i++) {
		tegra_dc_writel(dc, WINDOW_A_SELECT << i,
				DC_CMD_DISPLAY_WINDOW_HEADER);
		tegra_dc_writel(dc, blend[i].nokey, DC_WIN_BLEND_NOKEY);
		tegra_dc_writel(dc, blend[i].one_win, DC_WIN_BLEND_1WIN);
		tegra_dc_writel(dc, blend[i].two_win_x, DC_WIN_BLEND_2WIN_X);
		tegra_dc_writel(dc, blend[i].two_win_y, DC_WIN_BLEND_2WIN_Y);
		tegra_dc_writel(dc, blend[i].three_win_xy,
				DC_WIN_BLEND_3WIN_XY);
	}
}
EXPORT_SYMBOL(tegra_dc_set_blending);

void tegra_dc_setup_clk(struct tegra_dc *dc, struct clk *clk)
{
	/* clock setup for DSI/HDMI to go here */
}

static int tegra_dc_program_mode(struct tegra_dc *dc, struct tegra_dc_mode *mode)
{
	unsigned long val;
	unsigned long rate;
	unsigned long div;

	tegra_dc_writel(dc, 0x0, DC_DISP_DISP_TIMING_OPTIONS);
	tegra_dc_writel(dc, mode->h_ref_to_sync | (mode->v_ref_to_sync << 16),
			DC_DISP_REF_TO_SYNC);
	tegra_dc_writel(dc, mode->h_sync_width | (mode->v_sync_width << 16),
			DC_DISP_SYNC_WIDTH);
	tegra_dc_writel(dc, mode->h_back_porch | (mode->v_back_porch << 16),
			DC_DISP_BACK_PORCH);
	tegra_dc_writel(dc, mode->h_active | (mode->v_active << 16),
			DC_DISP_DISP_ACTIVE);
	tegra_dc_writel(dc, mode->h_front_porch | (mode->v_front_porch << 16),
			DC_DISP_FRONT_PORCH);

	tegra_dc_writel(dc, DE_SELECT_ACTIVE | DE_CONTROL_NORMAL,
			DC_DISP_DATA_ENABLE_OPTIONS);

	/* TODO: MIPI/CRT/HDMI clock cals */

	val = DISP_DATA_FORMAT_DF1P1C;

	if (dc->out->align == TEGRA_DC_ALIGN_MSB)
		val |= DISP_DATA_ALIGNMENT_MSB;
	else
		val |= DISP_DATA_ALIGNMENT_LSB;

	if (dc->out->order == TEGRA_DC_ORDER_RED_BLUE)
		val |= DISP_DATA_ORDER_RED_BLUE;
	else
		val |= DISP_DATA_ORDER_BLUE_RED;

	tegra_dc_writel(dc, val, DC_DISP_DISP_INTERFACE_CONTROL);

	rate = clk_get_rate(dc->clk);

	div = ((rate * 2 + mode->pclk / 2) / mode->pclk) - 2;

	if (rate * 2 / (div + 2) < (mode->pclk / 100 * 99) ||
	    rate * 2 / (div + 2) > (mode->pclk / 100 * 109)) {
		dev_err(&dc->ndev->dev,
			"can't divide %ld clock to %d -1/+9%% %ld %d %d\n",
			rate, mode->pclk,
			rate / div, (mode->pclk / 100 * 99),
			(mode->pclk / 100 * 109));
		return -EINVAL;
	}

	tegra_dc_writel(dc, 0x00010001,
			DC_DISP_SHIFT_CLOCK_OPTIONS);
	tegra_dc_writel(dc, PIXEL_CLK_DIVIDER_PCD1 | SHIFT_CLK_DIVIDER(div),
			DC_DISP_DISP_CLOCK_CONTROL);

	return 0;
}


int tegra_dc_set_mode(struct tegra_dc *dc, const struct tegra_dc_mode *mode)
{
	memcpy(&dc->mode, mode, sizeof(dc->mode));

	return 0;
}
EXPORT_SYMBOL(tegra_dc_set_mode);

static void tegra_dc_set_out(struct tegra_dc *dc, struct tegra_dc_out *out)
{
	dc->out = out;

	if (out->n_modes > 0)
		tegra_dc_set_mode(dc, &dc->out->modes[0]);

	switch (out->type) {
	case TEGRA_DC_OUT_RGB:
		dc->out_ops = &tegra_dc_rgb_ops;
		break;

	default:
		dc->out_ops = NULL;
		break;
	}

	if (dc->out_ops && dc->out_ops->init)
		dc->out_ops->init(dc);

}


static irqreturn_t tegra_dc_irq(int irq, void *ptr)
{
	struct tegra_dc *dc = ptr;
	unsigned long status;
	unsigned long val;
	int i;

	status = tegra_dc_readl(dc, DC_CMD_INT_STATUS);
	tegra_dc_writel(dc, status, DC_CMD_INT_STATUS);

	if (status & FRAME_END_INT) {
		int completed = 0;
		int dirty = 0;

		val = tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);
		for (i = 0; i < DC_N_WINDOWS; i++) {
			if (!(val & (WIN_A_ACT_REQ << i))) {
				dc->windows[i].dirty = 0;
				completed = 1;
			} else {
				dirty = 1;
			}
		}

		if (!dirty) {
			val = tegra_dc_readl(dc, DC_CMD_INT_ENABLE);
			val &= ~FRAME_END_INT;
			tegra_dc_writel(dc, val, DC_CMD_INT_ENABLE);
		}

		if (completed)
			wake_up(&dc->wq);
	}

	return IRQ_HANDLED;
}

static void tegra_dc_init(struct tegra_dc *dc)
{
	tegra_dc_writel(dc, 0x00000100, DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	if (dc->ndev->id == 0)
		tegra_dc_writel(dc, 0x0000011a, DC_CMD_CONT_SYNCPT_VSYNC);
	else
		tegra_dc_writel(dc, 0x0000011b, DC_CMD_CONT_SYNCPT_VSYNC);
	tegra_dc_writel(dc, 0x00004700, DC_CMD_INT_TYPE);
	tegra_dc_writel(dc, 0x0001c700, DC_CMD_INT_POLARITY);
	tegra_dc_writel(dc, 0x00000020, DC_DISP_MEM_HIGH_PRIORITY);
	tegra_dc_writel(dc, 0x00000001, DC_DISP_MEM_HIGH_PRIORITY_TIMER);

	tegra_dc_writel(dc, 0x0001c702, DC_CMD_INT_MASK);
	tegra_dc_writel(dc, 0x0001c700, DC_CMD_INT_ENABLE);

	if (dc->mode.pclk)
		tegra_dc_program_mode(dc, &dc->mode);
}

static void _tegra_dc_enable(struct tegra_dc *dc)
{
	if (dc->out && dc->out->enable)
		dc->out->enable();

	tegra_dc_setup_clk(dc, dc->clk);

	clk_enable(dc->host1x_clk);
	clk_enable(dc->clk);
	tegra_periph_reset_deassert(dc->clk);
	enable_irq(dc->irq);

	tegra_dc_init(dc);

	if (dc->out_ops && dc->out_ops->enable)
		dc->out_ops->enable(dc);

	tegra_dc_set_blending(dc, tegra_dc_blend_modes[0]);
}

void tegra_dc_enable(struct tegra_dc *dc)
{
	mutex_lock(&dc->lock);

	if (!dc->enabled) {
		_tegra_dc_enable(dc);
		dc->enabled = true;
	}

	mutex_unlock(&dc->lock);
}

static void _tegra_dc_disable(struct tegra_dc *dc)
{
	if (dc->out_ops && dc->out_ops->disable)
		dc->out_ops->disable(dc);

	disable_irq(dc->irq);
	tegra_periph_reset_assert(dc->clk);
	clk_disable(dc->clk);
	clk_disable(dc->host1x_clk);

	if (dc->out && dc->out->disable)
		dc->out->disable();
}


void tegra_dc_disable(struct tegra_dc *dc)
{
	mutex_lock(&dc->lock);

	if (dc->enabled) {
		dc->enabled = false;
		_tegra_dc_disable(dc);
	}

	mutex_unlock(&dc->lock);
}

static int tegra_dc_probe(struct nvhost_device *ndev)
{
	struct tegra_dc *dc;
	struct clk *clk;
	struct clk *host1x_clk;
	struct resource	*res;
	struct resource *base_res;
	struct resource *fb_mem = NULL;
	int ret = 0;
	void __iomem *base;
	int irq;
	int i;

	if (!ndev->dev.platform_data) {
		dev_err(&ndev->dev, "no platform data\n");
		return -ENOENT;
	}

	dc = kzalloc(sizeof(struct tegra_dc), GFP_KERNEL);
	if (!dc) {
		dev_err(&ndev->dev, "can't allocate memory for tegra_dc\n");
		return -ENOMEM;
	}

	irq = nvhost_get_irq_byname(ndev, "irq");
	if (irq <= 0) {
		dev_err(&ndev->dev, "no irq\n");
		ret = -ENOENT;
		goto err_free;
	}

	res = nvhost_get_resource_byname(ndev, IORESOURCE_MEM, "regs");
	if (!res) {
		dev_err(&ndev->dev, "no mem resource\n");
		ret = -ENOENT;
		goto err_free;
	}

	base_res = request_mem_region(res->start, resource_size(res), ndev->name);
	if (!base_res) {
		dev_err(&ndev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto err_free;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(&ndev->dev, "registers can't be mapped\n");
		ret = -EBUSY;
		goto err_release_resource_reg;
	}

	fb_mem = nvhost_get_resource_byname(ndev, IORESOURCE_MEM, "fbmem");

	host1x_clk = clk_get(&ndev->dev, "host1x");
	if (IS_ERR_OR_NULL(host1x_clk)) {
		dev_err(&ndev->dev, "can't get host1x clock\n");
		ret = -ENOENT;
		goto err_iounmap_reg;
	}

	clk = clk_get(&ndev->dev, NULL);
	if (IS_ERR_OR_NULL(clk)) {
		dev_err(&ndev->dev, "can't get clock\n");
		ret = -ENOENT;

		goto err_put_host1x_clk;
	}

	dc->clk = clk;
	dc->host1x_clk = host1x_clk;
	dc->base_res = base_res;
	dc->base = base;
	dc->irq = irq;
	dc->ndev = ndev;
	dc->pdata = ndev->dev.platform_data;

	if (dc->pdata->flags & TEGRA_DC_FLAG_ENABLED)
		dc->enabled = true;

	mutex_init(&dc->lock);
	init_waitqueue_head(&dc->wq);

	dc->n_windows = DC_N_WINDOWS;
	for (i = 0; i < dc->n_windows; i++) {
		dc->windows[i].idx = i;
		dc->windows[i].dc = dc;
	}

	if (request_irq(irq, tegra_dc_irq, IRQF_DISABLED,
			dev_name(&ndev->dev), dc)) {
		dev_err(&ndev->dev, "request_irq %d failed\n", irq);
		ret = -EBUSY;
		goto err_put_clk;
	}

	/* hack to ballence enable_irq calls in _tegra_dc_enable() */
	disable_irq(dc->irq);

	ret = tegra_dc_add(dc, ndev->id);
	if (ret < 0) {
		dev_err(&ndev->dev, "can't add dc\n");
		goto err_free_irq;
	}

	nvhost_set_drvdata(ndev, dc);

	if (dc->pdata->default_out)
		tegra_dc_set_out(dc, dc->pdata->default_out);
	else
		dev_err(&ndev->dev, "No default output specified.  Leaving output disabled.\n");

	if (dc->enabled)
		_tegra_dc_enable(dc);

	tegra_dc_dbg_add(dc);

	dev_info(&ndev->dev, "probed\n");

	if (dc->pdata->fb) {
		dc->fb = tegra_fb_register(ndev, dc, dc->pdata->fb, fb_mem);
		if (IS_ERR_OR_NULL(dc->fb))
			dc->fb = NULL;
	}

	if (dc->out_ops && dc->out_ops->detect)
		dc->out_ops->detect(dc);

	return 0;

err_free_irq:
	free_irq(irq, dc);
err_put_clk:
	clk_put(clk);
err_put_host1x_clk:
	clk_put(host1x_clk);
err_iounmap_reg:
	iounmap(base);
	if (fb_mem)
		release_resource(fb_mem);
err_release_resource_reg:
	release_resource(base_res);
err_free:
	kfree(dc);

	return ret;
}

static int tegra_dc_remove(struct nvhost_device *ndev)
{
	struct tegra_dc *dc = nvhost_get_drvdata(ndev);

	if (dc->fb) {
		tegra_fb_unregister(dc->fb);
		if (dc->fb_mem)
			release_resource(dc->fb_mem);
	}


	if (dc->enabled)
		_tegra_dc_disable(dc);

	free_irq(dc->irq, dc);
	clk_put(dc->clk);
	clk_put(dc->host1x_clk);
	iounmap(dc->base);
	if (dc->fb_mem)
		release_resource(dc->base_res);
	kfree(dc);
	return 0;
}

#ifdef CONFIG_PM
static int tegra_dc_suspend(struct nvhost_device *ndev, pm_message_t state)
{
	struct tegra_dc *dc = nvhost_get_drvdata(ndev);

	dev_info(&ndev->dev, "suspend\n");

	if (dc->enabled)
		_tegra_dc_disable(dc);

	return 0;
}

static int tegra_dc_resume(struct nvhost_device *ndev)
{
	struct tegra_dc *dc = nvhost_get_drvdata(ndev);
	struct tegra_dc_win *wins[DC_N_WINDOWS];
	int i;

	dev_info(&ndev->dev, "resume\n");

	if (dc->enabled) {
		for (i = 0; i < dc->n_windows; i++)
			wins[i] = &dc->windows[i];

		_tegra_dc_enable(dc);

		tegra_dc_update_windows(wins, dc->n_windows);
	}

	return 0;
}

#endif

extern int suspend_set(const char *val, struct kernel_param *kp)
{
	if (!strcmp(val, "dump"))
		dump_regs(tegra_dcs[0]);
#ifdef CONFIG_PM
	else if (!strcmp(val, "suspend"))
		tegra_dc_suspend(tegra_dcs[0]->ndev, PMSG_SUSPEND);
	else if (!strcmp(val, "resume"))
		tegra_dc_resume(tegra_dcs[0]->ndev);
#endif

	return 0;
}

extern int suspend_get(char *buffer, struct kernel_param *kp)
{
	return 0;
}

int suspend;

module_param_call(suspend, suspend_set, suspend_get, &suspend, 0644);

struct nvhost_driver tegra_dc_driver = {
	.driver = {
		.name = "tegradc",
		.owner = THIS_MODULE,
	},
	.probe = tegra_dc_probe,
	.remove = tegra_dc_remove,
#ifdef CONFIG_PM
	.suspend = tegra_dc_suspend,
	.resume = tegra_dc_resume,
#endif
};

static int __init tegra_dc_module_init(void)
{
	return nvhost_driver_register(&tegra_dc_driver);
}

static void __exit tegra_dc_module_exit(void)
{
	nvhost_driver_unregister(&tegra_dc_driver);
}

module_exit(tegra_dc_module_exit);
late_initcall(tegra_dc_module_init);
