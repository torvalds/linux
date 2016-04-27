/*
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/clk.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/ipu-v3-pre.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pre-regs.h"

struct ipu_pre_data {
	unsigned int id;
	struct device *dev;
	void __iomem *base;
	struct clk *clk;

	struct mutex mutex;	/* for in_use */
	spinlock_t lock;	/* for register access */

	struct list_head list;

	struct gen_pool *iram_pool;
	unsigned long double_buffer_size;
	unsigned long double_buffer_base;
	unsigned long double_buffer_paddr;

	bool in_use;
	bool enabled;
};

static LIST_HEAD(pre_list);
static DEFINE_SPINLOCK(pre_list_lock);

static inline void pre_write(struct ipu_pre_data *pre,
			u32 value, unsigned int offset)
{
	writel(value, pre->base + offset);
}

static inline u32 pre_read(struct ipu_pre_data *pre, unsigned offset)
{
	return readl(pre->base + offset);
}

static struct ipu_pre_data *get_pre(unsigned int id)
{
	struct ipu_pre_data *pre;
	unsigned long lock_flags;

	spin_lock_irqsave(&pre_list_lock, lock_flags);
	list_for_each_entry(pre, &pre_list, list) {
		if (pre->id == id) {
			spin_unlock_irqrestore(&pre_list_lock, lock_flags);
			return pre;
		}
	}
	spin_unlock_irqrestore(&pre_list_lock, lock_flags);

	return NULL;
}

int ipu_pre_alloc(int ipu_id, ipu_channel_t channel)
{
	struct ipu_pre_data *pre;
	int i, fixed;

	if (channel == MEM_BG_SYNC) {
		fixed = ipu_id ? 3 : 0;
		pre = get_pre(fixed);
		if (pre) {
			mutex_lock(&pre->mutex);
			if (!pre->in_use) {
				pre->in_use = true;
				mutex_unlock(&pre->mutex);
				return pre->id;
			}
			mutex_unlock(&pre->mutex);
		}
		return pre ? -EBUSY : -ENOENT;
	}

	for (i = 1; i < 3; i++) {
		pre = get_pre(i);
		if (!pre)
			continue;
		mutex_lock(&pre->mutex);
		if (!pre->in_use) {
			pre->in_use = true;
			mutex_unlock(&pre->mutex);
			return pre->id;
		}
		mutex_unlock(&pre->mutex);
	}

	return pre ? -EBUSY : -ENOENT;
}
EXPORT_SYMBOL(ipu_pre_alloc);

void ipu_pre_free(unsigned int *id)
{
	struct ipu_pre_data *pre;

	pre = get_pre(*id);
	if (!pre)
		return;

	mutex_lock(&pre->mutex);
	pre->in_use = false;
	mutex_unlock(&pre->mutex);

	*id = -1;
}
EXPORT_SYMBOL(ipu_pre_free);

unsigned long ipu_pre_alloc_double_buffer(unsigned int id, unsigned int size)
{
	struct ipu_pre_data *pre = get_pre(id);

	if (!pre)
		return -ENOENT;

	if (!size)
		return -EINVAL;

	pre->double_buffer_base = gen_pool_alloc(pre->iram_pool, size);
	if (!pre->double_buffer_base) {
		dev_err(pre->dev, "double buffer allocate failed\n");
		return -ENOMEM;
	}
	pre->double_buffer_size = size;

	pre->double_buffer_paddr = gen_pool_virt_to_phys(pre->iram_pool,
							 pre->double_buffer_base);

	return pre->double_buffer_paddr;
}
EXPORT_SYMBOL(ipu_pre_alloc_double_buffer);

void ipu_pre_free_double_buffer(unsigned int id)
{
	struct ipu_pre_data *pre = get_pre(id);

	if (!pre)
		return;

	if (pre->double_buffer_base) {
		gen_pool_free(pre->iram_pool,
			      pre->double_buffer_base,
			      pre->double_buffer_size);
		pre->double_buffer_base  = 0;
		pre->double_buffer_size  = 0;
		pre->double_buffer_paddr = 0;
	}
}
EXPORT_SYMBOL(ipu_pre_free_double_buffer);

/* PRE register configurations */
int ipu_pre_set_ctrl(unsigned int id, struct ipu_pre_context *config)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;
	int ret = 0;

	if (!pre)
		return -EINVAL;

	if (!pre->enabled)
		clk_prepare_enable(pre->clk);

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, BF_PRE_CTRL_TPR_RESET_SEL(1), HW_PRE_CTRL_SET);

	if (config->repeat)
		pre_write(pre, BF_PRE_CTRL_EN_REPEAT(1), HW_PRE_CTRL_SET);
	else
		pre_write(pre, BM_PRE_CTRL_EN_REPEAT, HW_PRE_CTRL_CLR);

	if (config->vflip)
		pre_write(pre, BF_PRE_CTRL_VFLIP(1), HW_PRE_CTRL_SET);
	else
		pre_write(pre, BM_PRE_CTRL_VFLIP, HW_PRE_CTRL_CLR);

	if (config->handshake_en) {
		pre_write(pre, BF_PRE_CTRL_HANDSHAKE_EN(1), HW_PRE_CTRL_SET);
		if (config->hsk_abort_en)
			pre_write(pre, BF_PRE_CTRL_HANDSHAKE_ABORT_SKIP_EN(1),
				  HW_PRE_CTRL_SET);
		else
			pre_write(pre, BM_PRE_CTRL_HANDSHAKE_ABORT_SKIP_EN,
				  HW_PRE_CTRL_CLR);

		switch (config->hsk_line_num) {
		case 0 /* 4 lines */:
			pre_write(pre, BM_PRE_CTRL_HANDSHAKE_LINE_NUM,
				  HW_PRE_CTRL_CLR);
			break;
		case 1 /* 8 lines */:
			pre_write(pre, BM_PRE_CTRL_HANDSHAKE_LINE_NUM,
				  HW_PRE_CTRL_CLR);
			pre_write(pre, BF_PRE_CTRL_HANDSHAKE_LINE_NUM(1),
				  HW_PRE_CTRL_SET);
			break;
		case 2 /* 16 lines */:
			pre_write(pre, BM_PRE_CTRL_HANDSHAKE_LINE_NUM,
				  HW_PRE_CTRL_CLR);
			pre_write(pre, BF_PRE_CTRL_HANDSHAKE_LINE_NUM(2),
				  HW_PRE_CTRL_SET);
			break;
		default:
			dev_err(pre->dev, "invalid hanshake line number\n");
			ret = -EINVAL;
			goto err;
		}
	} else
		pre_write(pre, BM_PRE_CTRL_HANDSHAKE_EN, HW_PRE_CTRL_CLR);


	switch (config->prefetch_mode) {
	case 0:
		pre_write(pre, BM_PRE_CTRL_BLOCK_EN, HW_PRE_CTRL_CLR);
		break;
	case 1:
		pre_write(pre, BF_PRE_CTRL_BLOCK_EN(1), HW_PRE_CTRL_SET);
		switch (config->block_size) {
		case 0:
			pre_write(pre, BM_PRE_CTRL_BLOCK_16, HW_PRE_CTRL_CLR);
			break;
		case 1:
			pre_write(pre, BF_PRE_CTRL_BLOCK_16(1), HW_PRE_CTRL_SET);
			break;
		default:
			dev_err(pre->dev, "invalid block size for pre\n");
			ret = -EINVAL;
			goto err;
		}
		break;
	default:
		dev_err(pre->dev, "invalid prefech mode for pre\n");
		ret = -EINVAL;
		goto err;
	}

	switch (config->interlaced) {
	case 0: /* progressive mode */
		pre_write(pre, BM_PRE_CTRL_SO, HW_PRE_CTRL_CLR);
		break;
	case 2: /* interlaced mode: Pal */
		pre_write(pre, BF_PRE_CTRL_SO(1), HW_PRE_CTRL_SET);
		pre_write(pre, BM_PRE_CTRL_INTERLACED_FIELD, HW_PRE_CTRL_CLR);
		break;
	case 3: /* interlaced mode: NTSC */
		pre_write(pre, BF_PRE_CTRL_SO(1), HW_PRE_CTRL_SET);
		pre_write(pre, BF_PRE_CTRL_INTERLACED_FIELD(1), HW_PRE_CTRL_SET);
		break;
	default:
		dev_err(pre->dev, "invalid interlaced or progressive mode\n");
		ret = -EINVAL;
		goto err;
	}

	if (config->sdw_update)
		pre_write(pre, BF_PRE_CTRL_SDW_UPDATE(1), HW_PRE_CTRL_SET);
	else
		pre_write(pre, BM_PRE_CTRL_SDW_UPDATE, HW_PRE_CTRL_CLR);

err:
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	if (!pre->enabled)
		clk_disable_unprepare(pre->clk);

	return ret;
}
EXPORT_SYMBOL(ipu_pre_set_ctrl);

static void ipu_pre_irq_mask(struct ipu_pre_data *pre,
			     unsigned long mask, bool clear)
{
	if (clear) {
		pre_write(pre, mask & 0x1f, HW_PRE_IRQ_MASK_CLR);
		return;
	}
	pre_write(pre, mask & 0x1f, HW_PRE_IRQ_MASK_SET);
}

static int ipu_pre_buf_set(unsigned int id, unsigned long cur_buf,
			   unsigned long next_buf)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, cur_buf, HW_PRE_CUR_BUF);
	pre_write(pre, next_buf, HW_PRE_NEXT_BUF);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}

static int ipu_pre_plane_buf_off_set(unsigned int id,
				     unsigned long sec_buf_off,
				     unsigned long trd_buf_off)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre || sec_buf_off & BM_PRE_U_BUF_OFFSET_RSVD0 ||
	    trd_buf_off & BM_PRE_V_BUF_OFFSET_RSVD0)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, sec_buf_off, HW_PRE_U_BUF_OFFSET);
	pre_write(pre, trd_buf_off, HW_PRE_V_BUF_OFFSET);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}

static int ipu_pre_tpr_set(unsigned int id, unsigned int tile_fmt)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;
	unsigned int tpr_ctrl, fmt;

	if (!pre)
		return -EINVAL;

	switch (tile_fmt) {
	case 0x0: /* Bypass */
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x0);
		break;
	case IPU_PIX_FMT_GPU32_SB_ST:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x10);
		break;
	case IPU_PIX_FMT_GPU16_SB_ST:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x11);
		break;
	case IPU_PIX_FMT_GPU32_ST:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x20);
		break;
	case IPU_PIX_FMT_GPU16_ST:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x21);
		break;
	case IPU_PIX_FMT_GPU32_SB_SRT:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x50);
		break;
	case IPU_PIX_FMT_GPU16_SB_SRT:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x51);
		break;
	case IPU_PIX_FMT_GPU32_SRT:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x60);
		break;
	case IPU_PIX_FMT_GPU16_SRT:
		fmt = BF_PRE_TPR_CTRL_TILE_FORMAT(0x61);
		break;
	default:
		dev_err(pre->dev, "invalid tile fmt for pre\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&pre->lock, lock_flags);
	tpr_ctrl = pre_read(pre, HW_PRE_TPR_CTRL);
	tpr_ctrl &= ~BM_PRE_TPR_CTRL_TILE_FORMAT;
	tpr_ctrl |= fmt;
	pre_write(pre, tpr_ctrl, HW_PRE_TPR_CTRL);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}

static int ipu_pre_set_shift(int id, unsigned int offset, unsigned int width)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, offset, HW_PRE_PREFETCH_ENGINE_SHIFT_OFFSET);
	pre_write(pre, width,  HW_PRE_PREFETCH_ENGINE_SHIFT_WIDTH);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}

static int ipu_pre_prefetch(unsigned int id,
			    unsigned int read_burst,
			    unsigned int input_bpp,
			    unsigned int input_pixel_fmt,
			    bool shift_bypass,
			    bool field_inverse,
			    bool tpr_coor_offset_en,
			    struct ipu_rect output_size,
			    unsigned int input_width,
			    unsigned int input_height,
			    unsigned int input_active_width,
			    unsigned int interlaced,
			    int interlace_offset)
{
	unsigned int prefetch_ctrl = 0;
	unsigned int input_y_pitch = 0, input_uv_pitch = 0;
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_PREFETCH_EN(1);
	switch (read_burst) {
	case 0x0:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_RD_NUM_BYTES(0x0);
		break;
	case 0x1:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_RD_NUM_BYTES(0x1);
		break;
	case 0x2:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_RD_NUM_BYTES(0x2);
		break;
	case 0x3:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_RD_NUM_BYTES(0x3);
		break;
	case 0x4:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_RD_NUM_BYTES(0x4);
		break;
	default:
		spin_unlock_irqrestore(&pre->lock, lock_flags);
		dev_err(pre->dev, "invalid read burst for prefetch engine\n");
		return -EINVAL;
	}

	switch (input_bpp) {
	case 8:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_ACTIVE_BPP(0x0);
		break;
	case 16:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_ACTIVE_BPP(0x1);
		break;
	case 32:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_ACTIVE_BPP(0x2);
		break;
	case 64:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_ACTIVE_BPP(0x3);
		break;
	default:
		spin_unlock_irqrestore(&pre->lock, lock_flags);
		dev_err(pre->dev, "invalid input bpp for prefetch engine\n");
		return -EINVAL;
	}

	switch (input_pixel_fmt) {
	case 0x1: /* tile */
	case 0x0: /* generic data */
	case IPU_PIX_FMT_RGB666:
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGRA4444:
	case IPU_PIX_FMT_BGRA5551:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_GBR24:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_ABGR32:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
	case IPU_PIX_FMT_YUV444:
	case IPU_PIX_FMT_AYUV:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x0);
		input_y_pitch = input_width * (input_bpp >> 3);
		if (interlaced && input_pixel_fmt != 0x1)
			input_y_pitch *= 2;
		break;
	case IPU_PIX_FMT_YUV444P:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x1);
		input_y_pitch  = input_width;
		input_uv_pitch = input_width;
		break;
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x2);
		input_y_pitch  = input_width;
		input_uv_pitch = input_width >> 1;
		break;
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x3);
		input_y_pitch  = input_width;
		input_uv_pitch = input_width >> 1;
		break;
	case PRE_PIX_FMT_NV61:
		prefetch_ctrl |= BM_PRE_PREFETCH_ENGINE_CTRL_PARTIAL_UV_SWAP;
	case IPU_PIX_FMT_NV16:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x4);
		input_y_pitch  = input_width;
		input_uv_pitch = input_width;
		break;
	case PRE_PIX_FMT_NV21:
		prefetch_ctrl |= BM_PRE_PREFETCH_ENGINE_CTRL_PARTIAL_UV_SWAP;
	case IPU_PIX_FMT_NV12:
		prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_INPUT_PIXEL_FORMAT(0x5);
		input_y_pitch  = input_width;
		input_uv_pitch = input_width;
		break;
	default:
		spin_unlock_irqrestore(&pre->lock, lock_flags);
		dev_err(pre->dev, "invalid input pixel format for prefetch engine\n");
		return -EINVAL;
	}

	prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_SHIFT_BYPASS(shift_bypass ? 1 : 0);
	prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_FIELD_INVERSE(field_inverse ? 1 : 0);
	prefetch_ctrl |= BF_PRE_PREFETCH_ENGINE_CTRL_TPR_COOR_OFFSET_EN(tpr_coor_offset_en ? 1 : 0);

	pre_write(pre, BF_PRE_PREFETCH_ENGINE_INPUT_SIZE_INPUT_WIDTH(input_active_width) |
		  BF_PRE_PREFETCH_ENGINE_INPUT_SIZE_INPUT_HEIGHT(input_height),
		  HW_PRE_PREFETCH_ENGINE_INPUT_SIZE);

	if (tpr_coor_offset_en)
		pre_write(pre, BF_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC_OUTPUT_SIZE_ULC_X(output_size.left) |
			     BF_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC_OUTPUT_SIZE_ULC_Y(output_size.top),
			     HW_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC);

	pre_write(pre, BF_PRE_PREFETCH_ENGINE_PITCH_INPUT_Y_PITCH(input_y_pitch) |
		     BF_PRE_PREFETCH_ENGINE_PITCH_INPUT_UV_PITCH(input_uv_pitch),
		     HW_PRE_PREFETCH_ENGINE_PITCH);

	pre_write(pre, BF_PRE_PREFETCH_ENGINE_INTERLACE_OFFSET_INTERLACE_OFFSET(interlace_offset), HW_PRE_PREFETCH_ENGINE_INTERLACE_OFFSET);

	pre_write(pre, prefetch_ctrl, HW_PRE_PREFETCH_ENGINE_CTRL);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}

static int ipu_pre_store(unsigned int id,
			 bool store_en,
			 unsigned int write_burst,
			 unsigned int output_bpp,
			 /* this means the output
			  * width by prefetch
			  */
			 unsigned int input_width,
			 unsigned int input_height,
			 unsigned int out_pitch,
			 unsigned int output_addr)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned int store_ctrl = 0;
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_STORE_EN(store_en ? 1 : 0);

	if (store_en) {
		switch (write_burst) {
		case 0x0:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_WR_NUM_BYTES(0x0);
			break;
		case 0x1:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_WR_NUM_BYTES(0x1);
			break;
		case 0x2:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_WR_NUM_BYTES(0x2);
			break;
		case 0x3:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_WR_NUM_BYTES(0x3);
			break;
		case 0x4:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_WR_NUM_BYTES(0x4);
			break;
		default:
			spin_unlock_irqrestore(&pre->lock, lock_flags);
			dev_err(pre->dev, "invalid write burst value for store engine\n");
			return -EINVAL;
		}

		switch (output_bpp) {
		case 8:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_OUTPUT_ACTIVE_BPP(0x0);
			break;
		case 16:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_OUTPUT_ACTIVE_BPP(0x1);
			break;
		case 32:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_OUTPUT_ACTIVE_BPP(0x2);
			break;
		case 64:
			store_ctrl |= BF_PRE_STORE_ENGINE_CTRL_OUTPUT_ACTIVE_BPP(0x3);
			break;
		default:
			spin_unlock_irqrestore(&pre->lock, lock_flags);
			dev_err(pre->dev, "invalid ouput bpp for store engine\n");
			return -EINVAL;
		}

		pre_write(pre, BF_PRE_STORE_ENGINE_SIZE_INPUT_TOTAL_WIDTH(input_width) |
			     BF_PRE_STORE_ENGINE_SIZE_INPUT_TOTAL_HEIGHT(input_height),
			     HW_PRE_STORE_ENGINE_SIZE);

		pre_write(pre, BF_PRE_STORE_ENGINE_PITCH_OUT_PITCH(out_pitch),
			     HW_PRE_STORE_ENGINE_PITCH);

		pre_write(pre, BF_PRE_STORE_ENGINE_ADDR_OUT_BASE_ADDR(output_addr),
			     HW_PRE_STORE_ENGINE_ADDR);
	}

	pre_write(pre, store_ctrl, HW_PRE_STORE_ENGINE_CTRL);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}
/* End */

static irqreturn_t ipu_pre_irq_handle(int irq, void *dev_id)
{
	struct ipu_pre_data *pre = dev_id;
	unsigned int irq_stat, axi_id = 0;

	spin_lock(&pre->lock);
	irq_stat = pre_read(pre, HW_PRE_IRQ);

	if (irq_stat & BM_PRE_IRQ_HANDSHAKE_ABORT_IRQ) {
		dev_warn(pre->dev, "handshake abort\n");
		pre_write(pre, BM_PRE_IRQ_HANDSHAKE_ABORT_IRQ, HW_PRE_IRQ_CLR);
	}

	if (irq_stat & BM_PRE_IRQ_TPR_RD_NUM_BYTES_OVFL_IRQ) {
		dev_warn(pre->dev, "tpr read num bytes overflow\n");
		pre_write(pre, BM_PRE_IRQ_TPR_RD_NUM_BYTES_OVFL_IRQ,
				HW_PRE_IRQ_CLR);
	}

	if (irq_stat & BM_PRE_IRQ_HANDSHAKE_ERROR_IRQ) {
		dev_warn(pre->dev, "handshake error\n");
		pre_write(pre, BM_PRE_IRQ_HANDSHAKE_ERROR_IRQ, HW_PRE_IRQ_CLR);
	}

	axi_id = (irq_stat & BM_PRE_IRQ_AXI_ERROR_ID) >>
					BP_PRE_IRQ_AXI_ERROR_ID;
	if (irq_stat & BM_PRE_IRQ_AXI_WRITE_ERROR) {
		dev_warn(pre->dev, "AXI%d write error\n", axi_id);
		pre_write(pre, BM_PRE_IRQ_AXI_WRITE_ERROR, HW_PRE_IRQ_CLR);
	}

	if (irq_stat & BM_PRE_IRQ_AXI_READ_ERROR) {
		dev_warn(pre->dev, "AXI%d read error\n", axi_id);
		pre_write(pre, BM_PRE_IRQ_AXI_READ_ERROR, HW_PRE_IRQ_CLR);
	}
	spin_unlock(&pre->lock);

	return IRQ_HANDLED;
}

static void ipu_pre_out_of_reset(unsigned int id)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return;

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, BF_PRE_CTRL_SFTRST(1) | BF_PRE_CTRL_CLKGATE(1),
		  HW_PRE_CTRL_CLR);
	spin_unlock_irqrestore(&pre->lock, lock_flags);
}

int ipu_pre_config(int id, struct ipu_pre_context *config)
{
	int ret = 0;
	struct ipu_pre_data *pre = get_pre(id);

	if (!config || !pre)
		return -EINVAL;

	config->store_addr = pre->double_buffer_paddr;

	if (!pre->enabled)
		clk_prepare_enable(pre->clk);

	ipu_pre_out_of_reset(id);

	ret = ipu_pre_plane_buf_off_set(id, config->sec_buf_off,
					config->trd_buf_off);
	if (ret < 0)
		goto out;

	ret = ipu_pre_tpr_set(id, config->tile_fmt);
	if (ret < 0)
		goto out;

	ret = ipu_pre_buf_set(id, config->cur_buf, config->next_buf);
	if (ret < 0)
		goto out;

	ret = ipu_pre_set_shift(id, config->prefetch_shift_offset,
				config->prefetch_shift_width);
	if (ret < 0)
		goto out;

	ret = ipu_pre_prefetch(id, config->read_burst, config->prefetch_input_bpp,
			config->prefetch_input_pixel_fmt, config->shift_bypass,
			config->field_inverse, config->tpr_coor_offset_en,
			config->prefetch_output_size, config->prefetch_input_width,
			config->prefetch_input_height,
			config->prefetch_input_active_width,
			config->interlaced,
			config->interlace_offset);
	if (ret < 0)
		goto out;

	ret = ipu_pre_store(id, config->store_en,
			config->write_burst, config->store_output_bpp,
			config->prefetch_output_size.width, config->prefetch_output_size.height,
			config->store_pitch,
			config->store_addr);
	if (ret < 0)
		goto out;

	ipu_pre_irq_mask(pre, BM_PRE_IRQ_HANDSHAKE_ABORT_IRQ |
			      BM_PRE_IRQ_TPR_RD_NUM_BYTES_OVFL_IRQ |
			      BM_PRE_IRQ_HANDSHAKE_ERROR_IRQ, false);
out:
	if (!pre->enabled)
		clk_disable_unprepare(pre->clk);

	return ret;
}
EXPORT_SYMBOL(ipu_pre_config);

int ipu_pre_enable(int id)
{
	int ret = 0;
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	if (pre->enabled)
		return 0;

	clk_prepare_enable(pre->clk);

	/* start the pre engine */
	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, BF_PRE_CTRL_ENABLE(1), HW_PRE_CTRL_SET);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	pre->enabled = true;

	return ret;
}
EXPORT_SYMBOL(ipu_pre_enable);

int ipu_pre_sdw_update(int id)
{
	int ret = 0;
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return -EINVAL;

	if (!pre->enabled)
		clk_prepare_enable(pre->clk);

	/* start the pre engine */
	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, BF_PRE_CTRL_SDW_UPDATE(1), HW_PRE_CTRL_SET);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	if (!pre->enabled)
		clk_disable_unprepare(pre->clk);

	return ret;
}
EXPORT_SYMBOL(ipu_pre_sdw_update);

void ipu_pre_disable(int id)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned long lock_flags;

	if (!pre)
		return;

	if (!pre->enabled)
		return;

	/* stop the pre engine */
	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, BF_PRE_CTRL_ENABLE(1), HW_PRE_CTRL_CLR);
	pre_write(pre, BF_PRE_CTRL_SDW_UPDATE(1), HW_PRE_CTRL_SET);
	pre_write(pre, BF_PRE_CTRL_SFTRST(1), HW_PRE_CTRL_SET);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	clk_disable_unprepare(pre->clk);

	pre->enabled = false;
}
EXPORT_SYMBOL(ipu_pre_disable);

int ipu_pre_set_fb_buffer(int id, bool resolve,
			  unsigned long fb_paddr,
			  unsigned int y_res,
			  unsigned int x_crop,
			  unsigned int y_crop,
			  unsigned int sec_buf_off,
			  unsigned int trd_buf_off)
{
	struct ipu_pre_data *pre = get_pre(id);
	unsigned int store_stat, store_block_y;
	unsigned long lock_flags;
	bool update = true;

	if (!pre)
		return -EINVAL;

	spin_lock_irqsave(&pre->lock, lock_flags);
	pre_write(pre, fb_paddr, HW_PRE_NEXT_BUF);
	pre_write(pre, sec_buf_off, HW_PRE_U_BUF_OFFSET);
	pre_write(pre, trd_buf_off, HW_PRE_V_BUF_OFFSET);
	pre_write(pre, BF_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC_OUTPUT_SIZE_ULC_X(x_crop) |
		       BF_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC_OUTPUT_SIZE_ULC_Y(y_crop),
		  HW_PRE_PREFETCH_ENGINE_OUTPUT_SIZE_ULC);

	/*
	 * Update shadow only when store engine runs out of the problematic
	 * window to workaround the SoC design bug recorded by errata ERR009624.
	 */
	if (y_res > IPU_PRE_SMALL_LINE) {
		unsigned long timeout = jiffies + msecs_to_jiffies(20);

		do {
			if (time_after(jiffies, timeout)) {
				update = false;
				dev_warn(pre->dev, "timeout waiting for PRE "
					"to run out of problematic window for "
					"shadow update\n");
				break;
			}

			store_stat = pre_read(pre, HW_PRE_STORE_ENGINE_STATUS);
			store_block_y = (store_stat &
				BM_PRE_STORE_ENGINE_STATUS_STORE_BLOCK_Y) >>
				BP_PRE_STORE_ENGINE_STATUS_STORE_BLOCK_Y;
		} while (store_block_y >=
			 (resolve ? DIV_ROUND_UP(y_res, 4) - 1 : y_res - 2) ||
			  store_block_y == 0);
	}

	if (update)
		pre_write(pre, BF_PRE_CTRL_SDW_UPDATE(1), HW_PRE_CTRL_SET);
	spin_unlock_irqrestore(&pre->lock, lock_flags);

	return 0;
}
EXPORT_SYMBOL(ipu_pre_set_fb_buffer);

static int ipu_pre_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ipu_pre_data *pre;
	struct resource *res;
	unsigned long lock_flags;
	int id, irq, err;

	pre = devm_kzalloc(&pdev->dev, sizeof(*pre), GFP_KERNEL);
	if (!pre)
		return -ENOMEM;
	pre->dev = &pdev->dev;

	id = of_alias_get_id(np, "pre");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get PRE id\n");
		return id;
	}
	pre->id = id;

	mutex_init(&pre->mutex);
	spin_lock_init(&pre->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pre->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pre->base))
		return PTR_ERR(pre->base);

	pre->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pre->clk)) {
		dev_err(&pdev->dev, "failed to get the pre clk\n");
		return PTR_ERR(pre->clk);
	}

	irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(&pdev->dev, irq, ipu_pre_irq_handle,
			       IRQF_TRIGGER_RISING, pdev->name, pre);
	if (err) {
		dev_err(&pdev->dev, "failed to request pre irq\n");
		return err;
	}

	pre->iram_pool = of_get_named_gen_pool(pdev->dev.of_node, "ocram", 0);
	if (!pre->iram_pool) {
		dev_err(&pdev->dev, "no iram exist for pre\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&pre_list_lock, lock_flags);
	list_add_tail(&pre->list, &pre_list);
	spin_unlock_irqrestore(&pre_list_lock, lock_flags);

	ipu_pre_alloc_double_buffer(pre->id, IPU_PRE_MAX_WIDTH * 8 * IPU_PRE_MAX_BPP);

	/* PRE GATE ON */
	clk_prepare_enable(pre->clk);
	pre_write(pre, BF_PRE_CTRL_SFTRST(1) | BF_PRE_CTRL_CLKGATE(1),
		  HW_PRE_CTRL_CLR);
	pre_write(pre, 0xf, HW_PRE_IRQ_MASK);
	clk_disable_unprepare(pre->clk);

	platform_set_drvdata(pdev, pre);

	dev_info(&pdev->dev, "driver probed\n");

	return 0;
}

static int ipu_pre_remove(struct platform_device *pdev)
{
	struct ipu_pre_data *pre = platform_get_drvdata(pdev);
	unsigned long lock_flags;

	if (pre->iram_pool && pre->double_buffer_base) {
		gen_pool_free(pre->iram_pool,
			      pre->double_buffer_base,
			      pre->double_buffer_size);
	}

	spin_lock_irqsave(&pre_list_lock, lock_flags);
	list_del(&pre->list);
	spin_unlock_irqrestore(&pre_list_lock, lock_flags);

	return 0;
}

static const struct of_device_id imx_ipu_pre_dt_ids[] = {
	{ .compatible = "fsl,imx6q-pre", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ipu_pre_dt_ids);

static struct platform_driver ipu_pre_driver = {
	.driver = {
			.name = "imx-pre",
			.of_match_table = of_match_ptr(imx_ipu_pre_dt_ids),
		  },
	.probe  = ipu_pre_probe,
	.remove = ipu_pre_remove,
};

static int __init ipu_pre_init(void)
{
	return platform_driver_register(&ipu_pre_driver);
}
subsys_initcall(ipu_pre_init);

static void __exit ipu_pre_exit(void)
{
	platform_driver_unregister(&ipu_pre_driver);
}
module_exit(ipu_pre_exit);

MODULE_DESCRIPTION("i.MX PRE driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
