/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/console.h>

#ifdef CONFIG_FB_SUNXI_UMP
#include <ump/ump_kernel_interface.h>
#endif

#include "drv_disp_i.h"
#include "dev_disp.h"
#include "dev_fb.h"
#include "disp_display.h"
#include "disp_lcd.h"
#include "disp_hdmi.h"

fb_info_t g_fbi;
static DEFINE_MUTEX(g_fbi_mutex);

static int screen0_output_type = -1;
module_param(screen0_output_type, int, 0444);
MODULE_PARM_DESC(screen0_output_type, "0:none; 1:lcd; 2:tv; 3:hdmi; 4:vga");

static char *screen0_output_mode;
module_param(screen0_output_mode, charp, 0444);
MODULE_PARM_DESC(screen0_output_mode,
	"tv: pal, pal-svideo, ntsc, ntsc-svideo, pal-m, pal-m-svideo, pal-nc "
	"or pal-nc-svideo "
	"hdmi: <width>x<height><i|p><24|50|60> vga: <width>x<height> "
	"hdmi modes can be prefixed with \"EDID:\". Then EDID will be used, "
	"with the specified mode as a fallback, ie \"EDID:1280x720p60\".");

static int screen1_output_type = -1;
module_param(screen1_output_type, int, 0444);
MODULE_PARM_DESC(screen1_output_type, "0:none; 1:lcd; 2:tv; 3:hdmi; 4:vga");

static char *screen1_output_mode;
module_param(screen1_output_mode, charp, 0444);
MODULE_PARM_DESC(screen1_output_mode, "See screen0_output_mode");

static const char * const tv_mode_names[] = {
	[DISP_TV_MOD_PAL]		= "pal",
	[DISP_TV_MOD_PAL_SVIDEO]	= "pal-svideo",
	[DISP_TV_MOD_NTSC]		= "ntsc",
	[DISP_TV_MOD_NTSC_SVIDEO]	= "ntsc-svideo",
	[DISP_TV_MOD_PAL_M]		= "pal-m",
	[DISP_TV_MOD_PAL_M_SVIDEO]	= "pal-m-svideo",
	[DISP_TV_MOD_PAL_NC]		= "pal-nc",
	[DISP_TV_MOD_PAL_NC_SVIDEO]	= "pal-nc-svideo",
};

static u32 tv_mode_to_frame_rate(u32 mode)
{
	switch (mode) {
	case DISP_TV_MOD_1080P_24HZ:
	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		return 24;
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_576P:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
	case DISP_TV_MOD_720P_50HZ:
	case DISP_TV_MOD_720P_50HZ_3D_FP:
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080P_50HZ:
		return 50;
	default:
		return 60;
	}
}

static int parse_output_mode(char *mode, int type, int fallback, __bool *edid)
{
	u32 i, width, height, interlace, frame_rate;
	char *ep;

	if (type == DISP_OUTPUT_TYPE_TV) {
		for (i = 0; i < ARRAY_SIZE(tv_mode_names); i++) {
			if (tv_mode_names[i] &&
					strcmp(mode, tv_mode_names[i]) == 0)
				return i;
		}
		__wrn("Unsupported mode: %s, ignoring\n", mode);
		return fallback;
	}

	if (type == DISP_OUTPUT_TYPE_HDMI && strncmp(mode, "EDID:", 5) == 0) {
		*edid = true;
		mode += 5;
	}

	width = simple_strtol(mode, &ep, 10);
	if (*ep != 'x') {
		__wrn("Invalid mode string: %s, ignoring\n", mode);
		return fallback;
	}
	height = simple_strtol(ep + 1, &ep, 10);

	if (type == DISP_OUTPUT_TYPE_HDMI) {
		if (*ep == 'i') {
			interlace = 1;
		} else if (*ep == 'p') {
			interlace = 0;
		} else {
			__wrn("Invalid tv-mode string: %s, ignoring\n", mode);
			return fallback;
		}
		frame_rate = simple_strtol(ep + 1, &ep, 10);

		for (i = 0; i < DISP_TV_MODE_NUM; i++) {
			if (tv_mode_to_width(i) == width &&
			    tv_mode_to_height(i) == height &&
			    Disp_get_screen_scan_mode(i) == interlace &&
			    tv_mode_to_frame_rate(i) == frame_rate) {
				return i;
			}
		}
	} else {
		for (i = 0; i < DISP_VGA_MODE_NUM; i++) {
			if (i == DISP_VGA_H1440_V900_RB)
				i = DISP_VGA_H1920_V1080; /* Skip RB modes */

			if (vga_mode_to_width(i) == width &&
			    vga_mode_to_height(i) == height) {
				return i;
			}
		}
	}
	__wrn("Unsupported mode: %s, ignoring\n", mode);
	return fallback;
}

/*
 *          0:ARGB  1:BRGA  2:ABGR  3:RGBA
 *     seq:  ARGB    BRGA    ARGB    BRGA
 * br_swqp:   0       0       1       1
 */
static __s32
parser_disp_init_para(__disp_init_t *init_para)
{
	int value;
	int i;

	memset(init_para, 0, sizeof(__disp_init_t));

	if (script_parser_fetch("disp_init", "disp_init_enable",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.disp_init_enable fail\n");
		return -1;
	}
	init_para->b_init = value;

	if (script_parser_fetch("disp_init", "disp_mode", &value, 1) < 0) {
		__wrn("fetch script data disp_init.disp_mode fail\n");
		return -1;
	}
	init_para->disp_mode = value;

	/* screen0 */
	if (screen0_output_type != -1)
		value = screen0_output_type;
	else if (script_parser_fetch("disp_init", "screen0_output_type",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.screen0_output_type fail\n");
		return -1;
	}

	if (value == 0) {
		init_para->output_type[0] = DISP_OUTPUT_TYPE_NONE;
	} else if (value == 1) {
		init_para->output_type[0] = DISP_OUTPUT_TYPE_LCD;
	} else if (value == 2) {
		init_para->output_type[0] = DISP_OUTPUT_TYPE_TV;
	} else if (value == 3) {
		init_para->output_type[0] = DISP_OUTPUT_TYPE_HDMI;
	} else if (value == 4) {
		init_para->output_type[0] = DISP_OUTPUT_TYPE_VGA;
	} else {
		__wrn("invalid screen0_output_type %d\n",
		      init_para->output_type[0]);
		return -1;
	}

	if (script_parser_fetch("disp_init", "screen0_output_mode",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.screen0_output_mode fail\n");
		return -1;
	}
	if (init_para->output_type[0] == DISP_OUTPUT_TYPE_TV ||
	    init_para->output_type[0] == DISP_OUTPUT_TYPE_HDMI) {
		if (screen0_output_mode) {
			init_para->tv_mode[0] = (__disp_tv_mode_t)
				parse_output_mode(screen0_output_mode,
					init_para->output_type[0], value,
					&gdisp.screen[0].use_edid);
		} else {
			init_para->tv_mode[0] = (__disp_tv_mode_t) value;
		}
	} else if (init_para->output_type[0] == DISP_OUTPUT_TYPE_VGA) {
		if (screen0_output_mode) {
			init_para->vga_mode[0] = (__disp_vga_mode_t)
				parse_output_mode(screen0_output_mode,
					init_para->output_type[0], value,
					&gdisp.screen[0].use_edid);
		} else {
			init_para->vga_mode[0] = (__disp_vga_mode_t) value;
		}
	}

	/* screen1 */
	if (screen1_output_type != -1)
		value = screen1_output_type;
	else if (script_parser_fetch("disp_init", "screen1_output_type",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.screen1_output_type fail\n");
		return -1;
	}

	if (value == 0) {
		init_para->output_type[1] = DISP_OUTPUT_TYPE_NONE;
	} else if (value == 1) {
		init_para->output_type[1] = DISP_OUTPUT_TYPE_LCD;
	} else if (value == 2) {
		init_para->output_type[1] = DISP_OUTPUT_TYPE_TV;
	} else if (value == 3) {
		init_para->output_type[1] = DISP_OUTPUT_TYPE_HDMI;
	} else if (value == 4) {
		init_para->output_type[1] = DISP_OUTPUT_TYPE_VGA;
	} else {
		__wrn("invalid screen1_output_type %d\n",
		      init_para->output_type[1]);
		return -1;
	}

	if (script_parser_fetch("disp_init", "screen1_output_mode",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.screen1_output_mode fail\n");
		return -1;
	}

	if (init_para->output_type[1] == DISP_OUTPUT_TYPE_TV ||
	    init_para->output_type[1] == DISP_OUTPUT_TYPE_HDMI) {
		if (screen1_output_mode) {
			init_para->tv_mode[1] = (__disp_tv_mode_t)
				parse_output_mode(screen1_output_mode,
					init_para->output_type[1], value,
					&gdisp.screen[1].use_edid);
		} else {
			init_para->tv_mode[1] = (__disp_tv_mode_t) value;
		}
	} else if (init_para->output_type[1] == DISP_OUTPUT_TYPE_VGA) {
		if (screen1_output_mode) {
			init_para->vga_mode[1] = (__disp_vga_mode_t)
				parse_output_mode(screen1_output_mode,
					init_para->output_type[1], value,
					&gdisp.screen[1].use_edid);
		} else {
			init_para->vga_mode[1] = (__disp_vga_mode_t) value;
		}
	}

	/* fb0 */
	if (script_parser_fetch("disp_init", "fb0_framebuffer_num",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.fb0_framebuffer_num fail\n");
		return -1;
	}
	init_para->buffer_num[0] = value;

	if (script_parser_fetch("disp_init", "fb0_format", &value, 1) < 0) {
		__wrn("fetch script data disp_init.fb0_format fail\n");
		return -1;
	}
	init_para->format[0] = value;

	if (script_parser_fetch("disp_init", "fb0_pixel_sequence",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.fb0_pixel_sequence fail\n");
		return -1;
	}
	init_para->seq[0] = value;

	if (script_parser_fetch
	    ("disp_init", "fb0_scaler_mode_enable", &value, 1) < 0) {
		__wrn("fetch script data disp_init.fb0_scaler_mode_enable "
		      "fail\n");
		return -1;
	}
	init_para->scaler_mode[0] = value;

	/* fb1 */
	if (script_parser_fetch("disp_init", "fb1_framebuffer_num",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.fb1_framebuffer_num fail\n");
		return -1;
	}
	init_para->buffer_num[1] = value;

	if (script_parser_fetch("disp_init", "fb1_format", &value, 1) < 0) {
		__wrn("fetch script data disp_init.fb1_format fail\n");
		return -1;
	}
	init_para->format[1] = value;

	if (script_parser_fetch("disp_init", "fb1_pixel_sequence",
				&value, 1) < 0) {
		__wrn("fetch script data disp_init.fb1_pixel_sequence fail\n");
		return -1;
	}
	init_para->seq[1] = value;

	if (script_parser_fetch
	    ("disp_init", "fb1_scaler_mode_enable", &value, 1) < 0) {
		__wrn("fetch script data disp_init.fb1_scaler_mode_enable "
		      "fail\n");
		return -1;
	}
	init_para->scaler_mode[1] = value;

	__inf("====display init para begin====\n");
	__inf("b_init:%d\n", init_para->b_init);
	__inf("disp_mode:%d\n\n", init_para->disp_mode);
	for (i = 0; i < 2; i++) {
		__inf("output_type[%d]:%d\n", i, init_para->output_type[i]);
		__inf("tv_mode[%d]:%d\n", i, init_para->tv_mode[i]);
		__inf("vga_mode[%d]:%d\n\n", i, init_para->vga_mode[i]);
	}
	for (i = 0; i < 2; i++) {
		__inf("buffer_num[%d]:%d\n", i, init_para->buffer_num[i]);
		__inf("format[%d]:%d\n", i, init_para->format[i]);
		__inf("seq[%d]:%d\n", i, init_para->seq[i]);
		__inf("br_swap[%d]:%d\n", i, init_para->br_swap[i]);
		__inf("b_scaler_mode[%d]:%d\n", i, init_para->scaler_mode[i]);
	}
	__inf("====display init para end====\n");

	return 0;
}

#ifdef UNUSED
static __s32
fb_draw_colorbar(__u32 base, __u32 width, __u32 height,
		 struct fb_var_screeninfo *var)
{
	__u32 i = 0, j = 0;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width / 4; j++) {
			__u32 offset = 0;

			if (var->bits_per_pixel == 32) {
				offset = width * i + j;
				sys_put_wvalue(base + offset * 4,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->red.length) -
						 1) << var->red.offset));

				offset = width * i + j + width / 4;
				sys_put_wvalue(base + offset * 4,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->green.length) -
						 1) << var->green.offset));

				offset = width * i + j + width / 4 * 2;
				sys_put_wvalue(base + offset * 4,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->blue.length) -
						 1) << var->blue.offset));

				offset = width * i + j + width / 4 * 3;
				sys_put_wvalue(base + offset * 4,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->red.length) -
						 1) << var->red.offset) |
					       (((1 << var->green.length) -
						 1) << var->green.offset));
			} else if (var->bits_per_pixel == 16) {
				offset = width * i + j;
				sys_put_hvalue(base + offset * 2,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->red.length) -
						 1) << var->red.offset));

				offset = width * i + j + width / 4;
				sys_put_hvalue(base + offset * 2,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->green.length) -
						 1) << var->green.offset));

				offset = width * i + j + width / 4 * 2;
				sys_put_hvalue(base + offset * 2,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->blue.length) -
						 1) << var->blue.offset));

				offset = width * i + j + width / 4 * 3;
				sys_put_hvalue(base + offset * 2,
					       (((1 << var->transp.length) -
						 1) << var->transp.offset) |
					       (((1 << var->red.length) -
						 1) << var->red.
						offset) |
					       (((1 << var->green.length) -
						 1) << var->green.offset));
			}
		}
	}

	return 0;
}

static __s32
fb_draw_gray_pictures(__u32 base, __u32 width, __u32 height,
		      struct fb_var_screeninfo *var)
{
	__u32 time = 0;

	for (time = 0; time < 18; time++) {
		__u32 i = 0, j = 0;

		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				__u32 addr = base + (i * width + j) * 4;
				__u32 value = (0xff << 24) |
					((time * 15) << 16) |
					((time * 15) << 8) | (time * 15);

				sys_put_wvalue(addr, value);
			}
		}
		DE_WRN("----%d\n", time * 15);
		msleep(1000 * 5);
	}
	return 0;
}
#endif /* UNUSED */

static int __init Fb_map_video_memory(__u32 fb_id, struct fb_info *info)
{
	unsigned map_size = PAGE_ALIGN(info->fix.smem_len);
	struct page *page;

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
	if (fb_size)
		goto use_reserved_mem;
#endif
	page = alloc_pages(GFP_KERNEL, get_order(map_size));
	if (page != NULL) {
		info->screen_base = page_address(page);
		info->fix.smem_start = virt_to_phys(info->screen_base);
		memset(info->screen_base, 0, info->fix.smem_len);
		__inf("Fb_map_video_memory, pa=0x%08lx size:0x%x\n",
		      info->fix.smem_start, info->fix.smem_len);
		return 0;
	} else {
		__wrn("alloc_pages fail!\n");
		return -ENOMEM;
	}
#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
use_reserved_mem:
	g_fbi.malloc_screen_base[fb_id] = disp_malloc(info->fix.smem_len);
	if (g_fbi.malloc_screen_base[fb_id] == NULL)
		return -ENOMEM;
	info->fix.smem_start = (unsigned long)
					__pa(g_fbi.malloc_screen_base[fb_id]);
	info->screen_base = ioremap_wc(info->fix.smem_start,
				       info->fix.smem_len);
	__inf("Fb_map_video_memory: fb_id=%d, disp_malloc=%p, ioremap_wc=%p\n",
		    fb_id, g_fbi.malloc_screen_base[fb_id], info->screen_base);
	if (!info->screen_base) {
		__wrn("ioremap_wc() failed, falling back to the existing "
		      "cached mapping\n");
		info->screen_base = g_fbi.malloc_screen_base[fb_id];
	}
	memset_io(info->screen_base, 0, info->fix.smem_len);

	__inf("Fb_map_video_memory, pa=0x%08lx size:0x%x\n",
	      info->fix.smem_start, info->fix.smem_len);

	return 0;
#endif
}

static inline void Fb_unmap_video_memory(__u32 fb_id, struct fb_info *info)
{
	unsigned map_size = PAGE_ALIGN(info->fix.smem_len);
#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
	if (fb_size) {
		if ((void *)info->screen_base !=
					g_fbi.malloc_screen_base[fb_id]) {
			__inf("Fb_unmap_video_memory: fb_id=%d, iounmap(%p)\n",
				fb_id, info->screen_base);
			iounmap(info->screen_base);
		}
		__inf("Fb_unmap_video_memory: fb_id=%d, disp_free(%p)\n",
				fb_id, g_fbi.malloc_screen_base[fb_id]);
		disp_free(g_fbi.malloc_screen_base[fb_id]);
	} else
#endif
		free_pages((unsigned long)info->screen_base,
			   get_order(map_size));
}

/*
 * todo.
 */
static __s32
disp_fb_to_var(__disp_pixel_fmt_t format, __disp_pixel_seq_t seq,
	       __bool br_swap, struct fb_var_screeninfo *var)
{
	if (format == DISP_FORMAT_ARGB8888) {
		var->bits_per_pixel = 32;
		var->transp.length = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		if (seq == DISP_SEQ_ARGB && br_swap == 0) { /* argb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
			var->transp.offset = var->red.offset + var->red.length;
		} else if (seq == DISP_SEQ_BGRA && br_swap == 0) { /* bgra */
			var->transp.offset = 0;
			var->red.offset =
				var->transp.offset + var->transp.length;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
		} else if (seq == DISP_SEQ_ARGB && br_swap == 1) { /* abgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
			var->transp.offset =
				var->blue.offset + var->blue.length;
		} else if (seq == DISP_SEQ_BGRA && br_swap == 1) { /* rgba */
			var->transp.offset = 0;
			var->blue.offset =
				var->transp.offset + var->transp.length;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		}
	} else if (format == DISP_FORMAT_RGB888) {
		var->bits_per_pixel = 24;
		var->transp.length = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		if (br_swap == 0) { /* rgb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else { /* bgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
		}
	} else if (format == DISP_FORMAT_RGB655) {
		var->bits_per_pixel = 16;
		var->transp.length = 0;
		var->red.length = 6;
		var->green.length = 5;
		var->blue.length = 5;
		if (br_swap == 0) { /* rgb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else { /* bgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
		}
	} else if (format == DISP_FORMAT_RGB565) {
		var->bits_per_pixel = 16;
		var->transp.length = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		if (br_swap == 0) { /* rgb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else { /* bgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
		}
	} else if (format == DISP_FORMAT_RGB556) {
		var->bits_per_pixel = 16;
		var->transp.length = 0;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 6;
		if (br_swap == 0) { /* rgb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else { /* bgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset = var->blue.offset + var->blue.length;
		}
	} else if (format == DISP_FORMAT_ARGB1555) {
		var->bits_per_pixel = 16;
		var->transp.length = 1;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 5;
		if (br_swap == 0) { /* rgb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
			var->transp.offset = var->red.offset + var->red.length;
		} else { /* bgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
			var->transp.offset =
				var->blue.offset + var->blue.length;
		}
	} else if (format == DISP_FORMAT_RGBA5551) {
		var->bits_per_pixel = 16;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 5;
		var->transp.length = 1;
		if (br_swap == 0) { /* rgba */
			var->transp.offset = 0;
			var->blue.offset =
				var->transp.offset + var->transp.length;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else { /* bgra */
			var->transp.offset = 0;
			var->red.offset =
				var->transp.offset + var->transp.length;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
		}
	} else if (format == DISP_FORMAT_ARGB4444) {
		var->bits_per_pixel = 16;
		var->transp.length = 4;
		var->red.length = 4;
		var->green.length = 4;
		var->blue.length = 4;
		if (br_swap == 0) { /* argb */
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
			var->transp.offset = var->red.offset + var->red.length;
		} else { /* abgr */
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset =
				var->green.offset + var->green.length;
			var->transp.offset =
				var->blue.offset + var->blue.length;
		}
	}

	return 0;
}

/*
 * todo
 */
static __s32
var_to_disp_fb(__disp_fb_t *fb, struct fb_var_screeninfo *var,
	       struct fb_fix_screeninfo *fix)
{
	if (var->nonstd == 0) { /* argb */
		var->reserved[0] = DISP_MOD_INTERLEAVED;
		var->reserved[1] = DISP_FORMAT_ARGB8888;
		var->reserved[2] = DISP_SEQ_ARGB;
		var->reserved[3] = 0;

		switch (var->bits_per_pixel) {
		case 1:
			var->red.offset = var->green.offset = var->blue.offset =
			    0;
			var->red.length = var->green.length = var->blue.length =
			    1;
			var->reserved[1] = DISP_FORMAT_1BPP;
			break;

		case 2:
			var->red.offset = var->green.offset = var->blue.offset =
			    0;
			var->red.length = var->green.length = var->blue.length =
			    2;
			var->reserved[1] = DISP_FORMAT_2BPP;
			break;

		case 4:
			var->red.offset = var->green.offset = var->blue.offset =
			    0;
			var->red.length = var->green.length = var->blue.length =
			    4;
			var->reserved[1] = DISP_FORMAT_4BPP;
			break;

		case 8:
			var->red.offset = var->green.offset = var->blue.offset =
			    0;
			var->red.length = var->green.length = var->blue.length =
			    8;
			var->reserved[1] = DISP_FORMAT_8BPP;
			break;

		case 16:
			if (var->red.length == 6 && var->green.length == 5 &&
			    var->blue.length == 5) {
				var->reserved[1] = DISP_FORMAT_RGB655;
				if (var->red.offset == 10 &&
				    var->green.offset == 5 &&
				    var->blue.offset == 0) { /* rgb */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				} else if (var->blue.offset == 11 &&
					   var->green.offset == 6 &&
					   var->red.offset == 0) { /* bgr */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 1;
				} else {
					__wrn("invalid RGB655 format"
					      "<red.offset:%d,green.offset:%d,"
					      "blue.offset:%d>\n",
					      var->red.offset,
					      var->green.offset,
					      var->blue.offset);
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				}

			} else if (var->red.length == 5 &&
				   var->green.length == 6 &&
				   var->blue.length == 5) {
				var->reserved[1] = DISP_FORMAT_RGB565;
				if (var->red.offset == 11 &&
				    var->green.offset == 5 &&
				    var->blue.offset == 0) { /* rgb */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				} else if (var->blue.offset == 11 &&
					   var->green.offset == 5 &&
					   var->red.offset == 0) { /* bgr */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 1;
				} else {
					__wrn("invalid RGB565 format"
					      "<red.offset:%d,green.offset:%d,"
					      "blue.offset:%d>\n",
					      var->red.offset,
					      var->green.offset,
					      var->blue.offset);
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				}
			} else if (var->red.length == 5 &&
				   var->green.length == 5 &&
				   var->blue.length == 6) {
				var->reserved[1] = DISP_FORMAT_RGB556;
				if (var->red.offset == 11 &&
				    var->green.offset == 6 &&
				    var->blue.offset == 0) { /* rgb */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				} else if (var->blue.offset == 10 &&
					   var->green.offset == 5 &&
					   var->red.offset == 0) { /* bgr */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 1;
				} else {
					__wrn("invalid RGB556 format"
					      "<red.offset:%d,green.offset:%d,"
					      "blue.offset:%d>\n",
					      var->red.offset,
					      var->green.offset,
					      var->blue.offset);
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				}
			} else if (var->transp.length == 1 &&
				   var->red.length == 5 &&
				   var->green.length == 5 &&
				   var->blue.length == 5) {
				var->reserved[1] = DISP_FORMAT_ARGB1555;
				if (var->transp.offset == 15 &&
				    var->red.offset == 10 &&
				    var->green.offset == 5 &&
				    var->blue.offset == 0) { /* argb */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				} else if (var->transp.offset == 15 &&
					   var->blue.offset == 10 &&
					   var->green.offset == 5 &&
					   var->red.offset == 0) { /* abgr */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 1;
				} else {
					__wrn("invalid ARGB1555 format"
					      "<transp.offset:%d,red.offset:%d,"
					      "green.offset:%d,"
					      "blue.offset:%d>\n",
					      var->transp.offset,
					      var->red.offset,
					      var->green.offset,
					      var->blue.offset);
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				}
			} else if (var->transp.length == 4 &&
				   var->red.length == 4 &&
				   var->green.length == 4 &&
				   var->blue.length == 4) {
				var->reserved[1] = DISP_FORMAT_ARGB4444;
				if (var->transp.offset == 12 &&
				    var->red.offset == 8 &&
				    var->green.offset == 4 &&
				    var->blue.offset == 0) { /* argb */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				} else if (var->transp.offset == 12 &&
					   var->blue.offset == 8 &&
					   var->green.offset == 4 &&
					   var->red.offset == 0) { /* abgr */
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 1;
				} else {
					__wrn("invalid ARGB4444 format"
					      "<transp.offset:%d,red.offset:%d,"
					      "green.offset:%d,blue.offset:%d>"
					      "\n", var->transp.offset,
					     var->red.offset, var->green.offset,
					     var->blue.offset);
					var->reserved[2] = DISP_SEQ_ARGB;
					var->reserved[3] = 0;
				}
			} else {
				__wrn("invalid bits_per_pixel :%d\n",
				      var->bits_per_pixel);
				return -EINVAL;
			}
			break;

		case 24:
			var->red.length = 8;
			var->green.length = 8;
			var->blue.length = 8;
			var->reserved[1] = DISP_FORMAT_RGB888;
			if (var->red.offset == 16 &&
			    var->green.offset == 8 &&
			    var->blue.offset == 0) { /* rgb */
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 0;
			} else if (var->blue.offset == 16 &&
				   var->green.offset == 8 &&
				   var->red.offset == 0) { /* bgr */
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 1;
			} else {
				__wrn("invalid RGB888 format"
				      "<red.offset:%d,green.offset:%d,"
				      "blue.offset:%d>\n",
				      var->red.offset, var->green.offset,
				      var->blue.offset);
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 0;
			}
			break;

		case 32:
			var->transp.length = 8;
			var->red.length = 8;
			var->green.length = 8;
			var->blue.length = 8;
			if (var->transp.offset == var->blue.offset ||
			    var->transp.offset == var->red.offset) {
				var->reserved[1] = DISP_FORMAT_ARGB888;
				__inf("Mode:     ARGB888");
			} else {
				var->reserved[1] = DISP_FORMAT_ARGB8888;
				__inf("Mode:     ARGB8888");
			}

			if (var->red.offset == 16 &&
			    var->green.offset == 8 &&
			    var->blue.offset == 0) { /* argb */
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 0;
			} else if (var->blue.offset == 24 &&
				   var->green.offset == 16 &&
				   var->red.offset == 8) { /* bgra */
				var->reserved[2] = DISP_SEQ_BGRA;
				var->reserved[3] = 0;
			} else if (var->blue.offset == 16 &&
				   var->green.offset == 8 &&
				   var->red.offset == 0) { /* abgr */
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 1;
			} else if (var->red.offset == 24 &&
				   var->green.offset == 16 &&
				   var->blue.offset == 8) { /* rgba */
				var->reserved[2] = DISP_SEQ_BGRA;
				var->reserved[3] = 1;
			} else {
				__wrn("invalid argb format"
				      "<transp.offset:%d,red.offset:%d,"
				      "green.offset:%d,blue.offset:%d>\n",
				      var->transp.offset, var->red.offset,
				      var->green.offset, var->blue.offset);
				var->reserved[2] = DISP_SEQ_ARGB;
				var->reserved[3] = 0;
			}
			break;

		default:
			__wrn("invalid bits_per_pixel :%d\n",
			      var->bits_per_pixel);
			return -EINVAL;
		}
	}

	fb->mode = var->reserved[0];
	fb->format = var->reserved[1];
	fb->seq = var->reserved[2];
	fb->br_swap = var->reserved[3];
	fb->size.width = var->xres_virtual;

	fix->line_length = (var->xres_virtual * var->bits_per_pixel) / 8;

	return 0;
}

static int Fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int Fb_release(struct fb_info *info, int user)
{
	return 0;
}

static int Fb_wait_for_vsync(struct fb_info *info)
{
	unsigned long count;
	__u32 sel = 0;
	int ret;

	for (sel = 0; sel < 2; sel++) {
		if (((sel == 0) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1)) ||
		    ((sel == 1) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0))) {

			if (BSP_disp_get_output_type(sel) ==
			    DISP_OUTPUT_TYPE_NONE) {
				return 0;
			}

			count = g_fbi.wait_count[sel];
			ret = wait_event_interruptible_timeout(g_fbi.wait[sel],
							       count !=
							       g_fbi.
							       wait_count[sel],
							       msecs_to_jiffies
							       (50));
			if (ret == 0) {
				__inf("timeout\n");
				return -ETIMEDOUT;
			}
		}
	}

	return 0;
}

static int Fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	__u32 sel = 0;

	//__inf("Fb_pan_display\n");

	for (sel = 0; sel < 2; sel++) {
		if (((sel == 0) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1)) ||
		    ((sel == 1) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0))) {
			__s32 layer_hdl = g_fbi.layer_hdl[info->node][sel];
			__disp_layer_info_t layer_para;
			__u32 buffer_num = 1;
			__u32 y_offset = 0;

			if (g_fbi.fb_mode[info->node] ==
			    FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS)
				if (sel != var->reserved[0])
					return -1;

			if (g_fbi.fb_mode[info->node] ==
			    FB_MODE_DUAL_SAME_SCREEN_TB)
				buffer_num = 2;
			if ((sel == 0) &&
			    (g_fbi.fb_mode[info->node] ==
				FB_MODE_DUAL_SAME_SCREEN_TB))
				y_offset = var->yres / 2;

			BSP_disp_layer_get_para(sel, layer_hdl, &layer_para);

			if (layer_para.mode == DISP_LAYER_WORK_MODE_SCALER) {
				layer_para.src_win.x = var->xoffset;
				layer_para.src_win.y = var->yoffset + y_offset;
				layer_para.src_win.width = var->xres;
				layer_para.src_win.height =
					var->yres / buffer_num;

				BSP_disp_layer_set_src_window(sel, layer_hdl,
							      &(layer_para.
								src_win));
			} else {
				layer_para.src_win.x = var->xoffset;
				layer_para.src_win.y = var->yoffset + y_offset;
				layer_para.src_win.width = var->xres;
				layer_para.src_win.height =
				    var->yres / buffer_num;

				layer_para.scn_win.width = var->xres;
				layer_para.scn_win.height =
					var->yres / buffer_num;

				BSP_disp_layer_set_src_window(sel, layer_hdl,
							      &(layer_para.
								src_win));
				BSP_disp_layer_set_screen_window(sel, layer_hdl,
								 &(layer_para.
								   scn_win));
			}
		}
	}

	// Fb_wait_for_vsync(info);

	return 0;
}

/*
 * todo
 */
static int Fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	__disp_pixel_fmt_t fmt;
	int dummy, sel;
	__inf("Fb_check_var: %dx%d %dbits\n", var->xres, var->yres,
	      var->bits_per_pixel);

	for (sel = 0; sel < 2; sel++) {
		if (!(((sel == 0) &&
		       (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1)) ||
		      ((sel == 1) &&
		       (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0))) ||
		    g_fbi.disp_init.output_type[sel] != DISP_OUTPUT_TYPE_HDMI)
			continue;

		/* Check that pll is found */
		if (disp_get_pll_freq(
			fb_videomode_pixclock_to_hdmi_pclk(var->pixclock),
				&dummy, &dummy))
			return -EINVAL;
	}

	switch (var->bits_per_pixel) {
	case 16:
		if (var->transp.length == 1 && var->transp.offset == 15)
			fmt = DISP_FORMAT_ARGB1555;
		else if (var->transp.length == 1 && var->transp.offset == 0)
			fmt = DISP_FORMAT_RGBA5551;
		else if (var->transp.length == 4)
			fmt = DISP_FORMAT_ARGB4444;
		else if (var->red.length == 6)
			fmt = DISP_FORMAT_RGB655;
		else if (var->green.length == 6)
			fmt = DISP_FORMAT_RGB565;
		else if (var->blue.length == 6)
			fmt = DISP_FORMAT_RGB556;
		else
			return -EINVAL;

		disp_fb_to_var(fmt, DISP_SEQ_P10, 0, var);
		break;
	case 24:
		disp_fb_to_var(DISP_FORMAT_RGB888, DISP_SEQ_ARGB, 0, var);
		break;
	case 32:
		disp_fb_to_var(DISP_FORMAT_ARGB8888, DISP_SEQ_ARGB, 0, var);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * todo
 */
static int Fb_set_par(struct fb_info *info)
{
	__u32 sel = 0;

	__inf("Fb_set_par: %dx%d %dbits\n", info->var.xres, info->var.yres,
	      info->var.bits_per_pixel);

	for (sel = 0; sel < 2; sel++) {
		if (((sel == 0) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1)) ||
		    ((sel == 1) &&
		     (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0))) {
			struct fb_var_screeninfo *var = &info->var;
			struct fb_fix_screeninfo *fix = &info->fix;
			bool mode_changed = false;
			__s32 layer_hdl = g_fbi.layer_hdl[info->node][sel];
			__disp_layer_info_t layer_para;
			__u32 buffer_num = 1;
			__u32 y_offset = 0;


			if (g_fbi.disp_init.output_type[sel] ==
					DISP_OUTPUT_TYPE_HDMI) {
				struct fb_videomode new_mode;
				struct fb_videomode old_mode;
				fb_var_to_videomode(&new_mode, var);
				var->yres_virtual = new_mode.yres *
						g_fbi.fb_para[sel].buffer_num;
				BSP_disp_get_videomode(sel, &old_mode);
				if (!fb_mode_is_equal(&new_mode, &old_mode)) {
					mode_changed = (BSP_disp_set_videomode(
							sel, &new_mode) == 0);

				}
			}

			if (g_fbi.fb_mode[info->node] ==
			    FB_MODE_DUAL_SAME_SCREEN_TB)
				buffer_num = 2;

			if ((sel == 0) && (g_fbi.fb_mode[info->node] ==
			     FB_MODE_DUAL_SAME_SCREEN_TB))
				y_offset = var->yres / 2;

			BSP_disp_layer_get_para(sel, layer_hdl, &layer_para);

			var_to_disp_fb(&(layer_para.fb), var, fix);
			layer_para.src_win.x = var->xoffset;
			layer_para.src_win.y = var->yoffset + y_offset;
			layer_para.src_win.width = var->xres;
			layer_para.src_win.height = var->yres / buffer_num;
			if (layer_para.mode != DISP_LAYER_WORK_MODE_SCALER ||
					mode_changed) {
				layer_para.scn_win.width =
					layer_para.src_win.width;
				layer_para.scn_win.height =
					layer_para.src_win.height;
			}
			BSP_disp_layer_set_para(sel, layer_hdl, &layer_para);
		}
	}
	return 0;
}

static inline __u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	__u32 mask = ((1 << bf->length) - 1) << bf->offset;
	return ((val >> (8 - bf->length)) << bf->offset) & mask;
}

static int Fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			unsigned blue, unsigned transp, struct fb_info *info)
{
	__u32 val;
	__u32 ret = 0;
	__u32 sel = 0;

	switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < 256) {
			for (sel = 0; sel < 2; sel++) {
				if (((sel == 0) && (g_fbi.fb_mode[info->node] !=
						    FB_MODE_SCREEN1)) ||
				    ((sel == 1) && (g_fbi.fb_mode[info->node] !=
						    FB_MODE_SCREEN0))) {
					val = (transp << 24) | (red << 16) |
						(green << 8) | blue;
					BSP_disp_set_palette_table(sel, &val,
								   regno * 4,
								   4);
				}
			}
		} else {
			ret = -EINVAL;
		}
		break;
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			val = convert_bitfield(transp, &info->var.transp) |
			    convert_bitfield(red, &info->var.red) |
			    convert_bitfield(green, &info->var.green) |
			    convert_bitfield(blue, &info->var.blue);
			__inf("Fb_setcolreg,regno=%2d,a=%2X,r=%2X,g=%2X,b=%2X, "
			      "result=%08X\n", regno, transp, red, green, blue,
			      val);
			((__u32 *) info->pseudo_palette)[regno] = val;
		} else {
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int Fb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	unsigned int j, r = 0;
	unsigned char hred, hgreen, hblue, htransp = 0xff;
	unsigned short *red, *green, *blue, *transp;

	__inf("Fb_setcmap, cmap start:%d len:%d, %dbpp\n", cmap->start,
	      cmap->len, info->var.bits_per_pixel);

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;

	for (j = 0; j < cmap->len; j++) {
		hred = *red++;
		hgreen = *green++;
		hblue = *blue++;
		if (transp)
			htransp = (*transp++) & 0xff;
		else
			htransp = 0xff;

		r = Fb_setcolreg(cmap->start + j, hred, hgreen, hblue, htransp,
				 info);
		if (r)
			return r;
	}

	return 0;
}

static int
Fb_blank(int blank_mode, struct fb_info *info)
{
	__u32 sel = 0;
	int ret = 0;

	__inf("Fb_blank,mode:%d\n", blank_mode);

	switch (blank_mode)	{
	case FB_BLANK_POWERDOWN:
		disp_suspend(3, 3);
		break;
	case FB_BLANK_UNBLANK:
		disp_resume(3, 3);
		/* fall through */
	case FB_BLANK_NORMAL:
		for (sel = 0; sel < 2; sel++) {
			if (((sel == 0) && (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1))
			 || ((sel == 1) && (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0))) {
				__s32 layer_hdl = g_fbi.layer_hdl[info->node][sel];

				if (blank_mode == FB_BLANK_NORMAL)
					BSP_disp_layer_close(sel, layer_hdl);
				else
					BSP_disp_layer_open(sel, layer_hdl);
			}
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int Fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	// __inf("Fb_cursor\n");

	return -EINVAL;
}

__s32 DRV_disp_int_process(__u32 sel)
{
	g_fbi.wait_count[sel]++;
	wake_up_interruptible(&g_fbi.wait[sel]);

	return 0;
}

#ifdef CONFIG_FB_SUNXI_UMP
int (*disp_get_ump_secure_id) (struct fb_info *info, fb_info_t *g_fbi,
			       unsigned long arg, int buf);
EXPORT_SYMBOL(disp_get_ump_secure_id);
#endif

static int Fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	int secure_id_buf_num = 0;
	unsigned long layer_hdl = 0;

	switch (cmd) {
	case FBIOGET_LAYER_HDL_0:
		if (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN1) {
			layer_hdl = g_fbi.layer_hdl[info->node][0];
			ret = copy_to_user((void __user *)arg, &layer_hdl,
					   sizeof(unsigned long));
		} else {
			ret = -1;
		}
		break;

	case FBIOGET_LAYER_HDL_1:
		if (g_fbi.fb_mode[info->node] != FB_MODE_SCREEN0) {
			layer_hdl = g_fbi.layer_hdl[info->node][1];
			ret = copy_to_user((void __user *)arg, &layer_hdl,
					   sizeof(unsigned long));
		} else {
			ret = -1;
		}
		break;

#if 0
	case FBIOGET_VBLANK:
		{
			struct fb_vblank vblank;
			__disp_tcon_timing_t tt;
			__u32 line = 0;
			__u32 sel;

			sel = (g_fbi.fb_mode[info->node] == FB_MODE_SCREEN1) ?
				1 : 0;
			line = BSP_disp_get_cur_line(sel);
			BSP_disp_get_timing(sel, &tt);

			memset(&vblank, 0, sizeof(struct fb_vblank));
			vblank.flags |= FB_VBLANK_HAVE_VBLANK;
			vblank.flags |= FB_VBLANK_HAVE_VSYNC;

			if (line <= (tt.ver_total_time - tt.ver_pixels))
				vblank.flags |= FB_VBLANK_VBLANKING;

			if ((line > tt.ver_front_porch) &&
			    (line < (tt.ver_front_porch + tt.ver_sync_time)))
				vblank.flags |= FB_VBLANK_VSYNCING;

			if (copy_to_user((void __user *)arg, &vblank,
					 sizeof(struct fb_vblank)))
				ret = -EFAULT;

			break;
		}
#endif

	case FBIO_WAITFORVSYNC:
		{
			ret = Fb_wait_for_vsync(info);
			break;
		}

#ifdef CONFIG_FB_SUNXI_UMP
	case GET_UMP_SECURE_ID_BUF2:	/* flow trough */
		secure_id_buf_num++;
	case GET_UMP_SECURE_ID_BUF1:	/* flow trough */
		secure_id_buf_num++;
	case GET_UMP_SECURE_ID_SUNXI_FB:
		{
			if (!disp_get_ump_secure_id)
				request_module("disp_ump");
			if (disp_get_ump_secure_id)
				return disp_get_ump_secure_id(info, &g_fbi, arg,
							      secure_id_buf_num);
			else
				return -ENOTSUPP;
		}
#endif

	default:
		//__inf("not supported fb io cmd:%x\n", cmd);
		break;
	}
	return ret;
}

static struct fb_ops dispfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = Fb_open,
	.fb_release = Fb_release,
	.fb_pan_display = Fb_pan_display,
	.fb_ioctl = Fb_ioctl,
	.fb_check_var = Fb_check_var,
	.fb_set_par = Fb_set_par,
	.fb_setcolreg = Fb_setcolreg,
	.fb_setcmap = Fb_setcmap,
	.fb_blank = Fb_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = Fb_cursor,
};

__s32 Display_Fb_Request(__u32 fb_id, __disp_fb_create_para_t * fb_para)
{
	struct fb_info *info = NULL;
	__s32 hdl = 0;
	__disp_layer_info_t layer_para;
	__u32 sel;
	__u32 xres, yres;

	__inf("Display_Fb_Request,fb_id:%d\n", fb_id);

	info = g_fbi.fbinfo[fb_id];

	xres = fb_para->width;
	yres = fb_para->height;

	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.xres = xres;
	info->var.yres = yres;
	info->var.xres_virtual = xres;
	info->var.yres_virtual = yres * fb_para->buffer_num;
	info->fix.line_length =
		(fb_para->width * info->var.bits_per_pixel) >> 3;
	info->fix.smem_len = PAGE_ALIGN(
		info->fix.line_length * fb_para->height * fb_para->buffer_num);
	Fb_map_video_memory(fb_id, info);

	for (sel = 0; sel < 2; sel++) {
		if (((sel == 0) && (fb_para->fb_mode != FB_MODE_SCREEN1)) ||
		    ((sel == 1) && (fb_para->fb_mode != FB_MODE_SCREEN0))) {
			__u32 y_offset = 0, src_width = xres, src_height = yres;

			if (((sel == 0) &&
			     (fb_para->fb_mode == FB_MODE_SCREEN0 ||
			      fb_para->fb_mode ==
			      FB_MODE_DUAL_SAME_SCREEN_TB)) ||
			    ((sel == 1) &&
			     (fb_para->fb_mode == FB_MODE_SCREEN1)) ||
			    ((sel == fb_para->primary_screen_id) &&
			     (fb_para->fb_mode ==
			      FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS))) {

				struct fb_videomode mode;
				if (BSP_disp_get_videomode(sel, &mode) == 0) {
					fb_videomode_to_var(&info->var, &mode);
					info->var.yres_virtual =
						mode.yres * fb_para->buffer_num;
				}
			}

			if (fb_para->fb_mode == FB_MODE_DUAL_SAME_SCREEN_TB) {
				src_height = yres / 2;
				if (sel == 0)
					y_offset = yres / 2;
			}

			memset(&layer_para, 0, sizeof(__disp_layer_info_t));
			layer_para.mode = fb_para->mode;
			layer_para.scn_win.width = src_width;
			layer_para.scn_win.height = src_height;
			if (fb_para->fb_mode ==
			    FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS) {
				if (sel != fb_para->primary_screen_id) {
					layer_para.mode =
					    DISP_LAYER_WORK_MODE_SCALER;
					layer_para.scn_win.width =
					    fb_para->aux_output_width;
					layer_para.scn_win.height =
					    fb_para->aux_output_height;
				} else if (fb_para->mode ==
					   DISP_LAYER_WORK_MODE_SCALER) {
					layer_para.scn_win.width =
					    fb_para->output_width;
					layer_para.scn_win.height =
					    fb_para->output_height;
				}
			} else if (fb_para->mode ==
				   DISP_LAYER_WORK_MODE_SCALER) {
				layer_para.scn_win.width =
				    fb_para->output_width;
				layer_para.scn_win.height =
				    fb_para->output_height;
			}

			hdl = BSP_disp_layer_request(sel, layer_para.mode);

			layer_para.pipe = 0;
			layer_para.alpha_en = 1;
			layer_para.alpha_val = 0xff;
			layer_para.ck_enable = 0;
			layer_para.src_win.x = 0;
			layer_para.src_win.y = y_offset;
			layer_para.src_win.width = src_width;
			layer_para.src_win.height = src_height;
			layer_para.scn_win.x = 0;
			layer_para.scn_win.y = 0;
			var_to_disp_fb(&(layer_para.fb), &(info->var),
				       &(info->fix));
			layer_para.fb.addr[0] = (__u32) info->fix.smem_start;
			layer_para.fb.addr[1] = 0;
			layer_para.fb.addr[2] = 0;
			layer_para.fb.size.width = fb_para->width;
			layer_para.fb.size.height = fb_para->height;
			layer_para.fb.cs_mode = DISP_BT601;
			layer_para.b_from_screen = 0;
			BSP_disp_layer_set_para(sel, hdl, &layer_para);

			BSP_disp_layer_open(sel, hdl);

			g_fbi.layer_hdl[fb_id][sel] = hdl;
		}
	}

	g_fbi.fb_enable[fb_id] = 1;
	g_fbi.fb_mode[fb_id] = fb_para->fb_mode;
	memcpy(&g_fbi.fb_para[fb_id], fb_para, sizeof(__disp_fb_create_para_t));

	return DIS_SUCCESS;
}

__s32 Display_Fb_Release(__u32 fb_id)
{
	struct fb_info *info = g_fbi.fbinfo[fb_id];
	__u32 sel = 0;

	__inf("Display_Fb_Release, fb_id:%d\n", fb_id);

	if (!g_fbi.fb_enable[fb_id])
		return DIS_SUCCESS;

	for (sel = 0; sel < 2; sel++)
		if (((sel == 0) && (g_fbi.fb_mode[fb_id] != FB_MODE_SCREEN1)) ||
		    ((sel == 1) && (g_fbi.fb_mode[fb_id] != FB_MODE_SCREEN0))) {
			__s32 layer_hdl = g_fbi.layer_hdl[fb_id][sel];

			BSP_disp_layer_release(sel, layer_hdl);
		}

	g_fbi.layer_hdl[fb_id][0] = 0;
	g_fbi.layer_hdl[fb_id][1] = 0;
	g_fbi.fb_mode[fb_id] = FB_MODE_SCREEN0;
	memset(&g_fbi.fb_para[fb_id], 0, sizeof(__disp_fb_create_para_t));
	g_fbi.fb_enable[fb_id] = 0;

	fb_dealloc_cmap(&info->cmap);
	Fb_unmap_video_memory(fb_id, info);

	return DIS_SUCCESS;
}

__s32 Display_Fb_get_para(__u32 fb_id, __disp_fb_create_para_t *fb_para)
{
	__inf("Display_Fb_Release, fb_id:%d\n", fb_id);

	if ((fb_id >= 0) && g_fbi.fb_enable[fb_id]) {
		memcpy(fb_para, &g_fbi.fb_para[fb_id],
		       sizeof(__disp_fb_create_para_t));

		return DIS_SUCCESS;
	} else {
		__wrn("invalid paras fb_id:%d in Display_Fb_get_para\n", fb_id);
		return DIS_FAIL;
	}
}

__s32 Display_get_disp_init_para(__disp_init_t *init_para)
{
	memcpy(init_para, &g_fbi.disp_init, sizeof(__disp_init_t));

	return 0;
}

__s32 Display_set_fb_timing(__u32 sel)
{
	__u8 fb_id = 0;

	for (fb_id = 0; fb_id < SUNXI_MAX_FB; fb_id++) {
		__disp_fb_create_para_t *fb_para = &g_fbi.fb_para[fb_id];
		__fb_mode_t fb_mode = g_fbi.fb_mode[fb_id];
		struct fb_var_screeninfo *var = &g_fbi.fbinfo[sel]->var;
		if (g_fbi.fb_enable[fb_id]) {
			if (((sel == 0) &&
			     (fb_mode == FB_MODE_SCREEN0 ||
				 fb_mode == FB_MODE_DUAL_SAME_SCREEN_TB)) ||
			    ((sel == 1) &&
			     (fb_mode == FB_MODE_SCREEN1)) ||
			    ((sel == fb_para->primary_screen_id) &&
			     (fb_mode ==
			      FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS))) {

				struct fb_videomode mode;
				if (BSP_disp_get_videomode(sel, &mode) == 0) {
					fb_videomode_to_var(var, &mode);
					var->yres_virtual = mode.yres *
						fb_para->buffer_num;
				}
			}
		}
	}

	return 0;
}

void hdmi_edid_received(unsigned char *edid, int block_count)
{
	struct fb_event event;
	struct fb_modelist *m, *n;
	int dummy;
	__u32 sel = 0;
	__u32 block = 0;
	LIST_HEAD(old_modelist);

	mutex_lock(&g_fbi_mutex);
	for (sel = 0; sel < 2; sel++) {
		struct fb_info *fbi = g_fbi.fbinfo[sel];
		int err = 0;

		if (g_fbi.disp_init.output_type[sel] != DISP_OUTPUT_TYPE_HDMI)
			continue;

		if (g_fbi.fb_registered[sel]) {
			if (!lock_fb_info(fbi))
				continue;

			console_lock();
		}

		for (block = 0; block < block_count; block++) {
			if (block == 0) {
				if (fbi->monspecs.modedb != NULL) {
					fb_destroy_modedb(fbi->monspecs.modedb);
					fbi->monspecs.modedb = NULL;
				}

				fb_edid_to_monspecs(edid, &fbi->monspecs);
			} else {
				fb_edid_add_monspecs(edid + 0x80 * block, &fbi->monspecs);
			}
		}

		if (fbi->monspecs.modedb_len == 0) {
			/*
			 * Should not happen? Avoid panics and skip in this
			 * case.
			 */
			if (g_fbi.fb_registered[sel]) {
				console_unlock();
				unlock_fb_info(fbi);
			}

			WARN_ON(fbi->monspecs.modedb_len == 0);
			continue;
		}

		list_splice(&fbi->modelist, &old_modelist);

		fb_videomode_to_modelist(fbi->monspecs.modedb,
					 fbi->monspecs.modedb_len,
					 &fbi->modelist);

		/* Filter out modes which we cannot do */
		list_for_each_entry_safe(m, n, &fbi->modelist, list) {
			if (disp_get_pll_freq(
				fb_videomode_pixclock_to_hdmi_pclk(
					m->mode.pixclock), &dummy, &dummy)) {
				list_del(&m->list);
				kfree(m);
			}
		}
		/* Are there any usable modes left? */
		if (list_empty(&fbi->modelist)) {
			list_splice(&old_modelist, &fbi->modelist);
			pr_warn("EDID: No modes with good pixelclock found\n");
			continue;
		}

		/*
		 * Tell framebuffer users that modelist was replaced. This is
		 * to avoid use of old removed modes and to avoid panics.
		 */
		event.info = fbi;
		err = fb_notifier_call_chain(FB_EVENT_NEW_MODELIST, &event);

		fb_destroy_modelist(&old_modelist);

		if (g_fbi.fb_registered[sel]) {
			console_unlock();
			unlock_fb_info(fbi);
		}

		WARN_ON(err);
	}
	mutex_unlock(&g_fbi_mutex);
}
EXPORT_SYMBOL(hdmi_edid_received);

/* ??? --libv */
extern unsigned long fb_start;
extern unsigned long fb_size;

__s32 Fb_Init(__u32 from)
{
	__s32 i;
	__bool need_open_hdmi = 0;
	__disp_fb_create_para_t fb_para = {
		.primary_screen_id = 0,
	};
	static DEFINE_MUTEX(fb_init_mutex);
	static bool first_time = true;

	mutex_lock(&fb_init_mutex);
	if (first_time) { /* First call ? */
		DRV_DISP_Init();

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
		__inf("fbmem: fb_start=%lu, fb_size=%lu\n", fb_start, fb_size);
		disp_create_heap((unsigned long)(__va(fb_start)), fb_size);
#endif

		for (i = 0; i < SUNXI_MAX_FB; i++) {
			g_fbi.fbinfo[i] = framebuffer_alloc(0, g_fbi.dev);
			INIT_LIST_HEAD(&g_fbi.fbinfo[i]->modelist);
			g_fbi.fbinfo[i]->fbops = &dispfb_ops;
			g_fbi.fbinfo[i]->flags = 0;
			g_fbi.fbinfo[i]->device = g_fbi.dev;
			g_fbi.fbinfo[i]->par = &g_fbi;
			g_fbi.fbinfo[i]->var.xoffset = 0;
			g_fbi.fbinfo[i]->var.yoffset = 0;
			g_fbi.fbinfo[i]->var.xres = 800;
			g_fbi.fbinfo[i]->var.yres = 480;
			g_fbi.fbinfo[i]->var.xres_virtual = 800;
			g_fbi.fbinfo[i]->var.yres_virtual = 480 * 2;
			g_fbi.fbinfo[i]->var.nonstd = 0;
			g_fbi.fbinfo[i]->var.grayscale = 0;
			g_fbi.fbinfo[i]->var.bits_per_pixel = 32;
			g_fbi.fbinfo[i]->var.transp.length = 8;
			g_fbi.fbinfo[i]->var.red.length = 8;
			g_fbi.fbinfo[i]->var.green.length = 8;
			g_fbi.fbinfo[i]->var.blue.length = 8;
			g_fbi.fbinfo[i]->var.transp.offset = 24;
			g_fbi.fbinfo[i]->var.red.offset = 16;
			g_fbi.fbinfo[i]->var.green.offset = 8;
			g_fbi.fbinfo[i]->var.blue.offset = 0;
			g_fbi.fbinfo[i]->var.activate = FB_ACTIVATE_FORCE;
			g_fbi.fbinfo[i]->fix.type = FB_TYPE_PACKED_PIXELS;
			g_fbi.fbinfo[i]->fix.type_aux = 0;
			g_fbi.fbinfo[i]->fix.visual = FB_VISUAL_TRUECOLOR;
			g_fbi.fbinfo[i]->fix.xpanstep = 1;
			g_fbi.fbinfo[i]->fix.ypanstep = 1;
			g_fbi.fbinfo[i]->fix.ywrapstep = 0;
			g_fbi.fbinfo[i]->fix.accel = FB_ACCEL_NONE;
			g_fbi.fbinfo[i]->fix.line_length =
				g_fbi.fbinfo[i]->var.xres_virtual * 4;
			g_fbi.fbinfo[i]->fix.smem_len = PAGE_ALIGN(
				g_fbi.fbinfo[i]->fix.line_length *
				g_fbi.fbinfo[i]->var.yres_virtual * 2);
			g_fbi.fbinfo[i]->screen_base = NULL;
			g_fbi.fbinfo[i]->pseudo_palette =
				g_fbi.pseudo_palette[i];
			g_fbi.fbinfo[i]->fix.smem_start = 0x0;
			g_fbi.fbinfo[i]->fix.mmio_start = 0;
			g_fbi.fbinfo[i]->fix.mmio_len = 0;

			if (fb_alloc_cmap(&g_fbi.fbinfo[i]->cmap, 256, 1) < 0)
				return -ENOMEM;
		}
		parser_disp_init_para(&(g_fbi.disp_init));
		first_time = false;
	}
	mutex_unlock(&fb_init_mutex);

	if (g_fbi.disp_init.b_init) {
		__u32 sel = 0;

		for (sel = 0; sel < 2; sel++) {
			if (((sel == 0) && (g_fbi.disp_init.disp_mode !=
					    DISP_INIT_MODE_SCREEN1)) ||
			    ((sel == 1) && (g_fbi.disp_init.disp_mode !=
					    DISP_INIT_MODE_SCREEN0))) {
				if (g_fbi.disp_init.output_type[sel] ==
				    DISP_OUTPUT_TYPE_HDMI)
					need_open_hdmi = 1;
			}
		}
	}

	__inf("Fb_Init: %d %d\n", from, need_open_hdmi);

	if (need_open_hdmi == 1 && from == SUNXI_LCD)
		/* it is called from lcd driver, but hdmi need to be opened */
		return 0;
	else if (need_open_hdmi == 0 && from == SUNXI_HDMI)
		/* it is called from hdmi driver, but hdmi need not be opened */
		return 0;

	if (g_fbi.disp_init.b_init) {
		__u32 fb_num = 0, sel = 0;

		for (sel = 0; sel < 2; sel++) {
			if (((sel == 0) && (g_fbi.disp_init.disp_mode !=
					    DISP_INIT_MODE_SCREEN1)) ||
			    ((sel == 1) && (g_fbi.disp_init.disp_mode !=
					    DISP_INIT_MODE_SCREEN0))) {
				if (g_fbi.disp_init.output_type[sel] ==
				    DISP_OUTPUT_TYPE_LCD) {
					DRV_lcd_open(sel);
				} else if (g_fbi.disp_init.output_type[sel] ==
					   DISP_OUTPUT_TYPE_TV) {
					BSP_disp_tv_set_mode(sel,
							     g_fbi.disp_init.
							     tv_mode[sel]);
					BSP_disp_tv_open(sel);
				} else if (g_fbi.disp_init.output_type[sel] ==
					   DISP_OUTPUT_TYPE_HDMI) {
					BSP_disp_hdmi_set_mode(sel,
							       g_fbi.disp_init.
							       tv_mode[sel]);
					BSP_disp_hdmi_open(sel,
						gdisp.screen[sel].use_edid);
				} else if (g_fbi.disp_init.output_type[sel] ==
					   DISP_OUTPUT_TYPE_VGA) {
					BSP_disp_vga_set_mode(sel,
							      g_fbi.disp_init.
							      vga_mode[sel]);
					BSP_disp_vga_open(sel);
				}
			}
		}

		fb_num = (g_fbi.disp_init.disp_mode ==
			  DISP_INIT_MODE_TWO_DIFF_SCREEN) ? 2 : 1;
		for (i = 0; i < fb_num; i++) {
			__u32 screen_id = i;

			disp_fb_to_var(g_fbi.disp_init.format[i],
				       g_fbi.disp_init.seq[i],
				       g_fbi.disp_init.br_swap[i],
				       &(g_fbi.fbinfo[i]->var));

			if (g_fbi.disp_init.disp_mode ==
			    DISP_INIT_MODE_SCREEN1)
				screen_id = 1;

			fb_para.buffer_num = g_fbi.disp_init.buffer_num[i];
			fb_para.width = BSP_disp_get_screen_width(screen_id);
			fb_para.height = BSP_disp_get_screen_height(screen_id);
			fb_para.output_width =
				BSP_disp_get_screen_width(screen_id);
			fb_para.output_height =
				BSP_disp_get_screen_height(screen_id);
			fb_para.mode = (g_fbi.disp_init.scaler_mode[i] == 0) ?
				DISP_LAYER_WORK_MODE_NORMAL :
				DISP_LAYER_WORK_MODE_SCALER;
			if (g_fbi.disp_init.disp_mode ==
			    DISP_INIT_MODE_SCREEN0) {
				fb_para.fb_mode = FB_MODE_SCREEN0;
			} else if (g_fbi.disp_init.disp_mode ==
				   DISP_INIT_MODE_SCREEN1) {
				fb_para.fb_mode = FB_MODE_SCREEN1;
			} else if (g_fbi.disp_init.disp_mode ==
				   DISP_INIT_MODE_TWO_DIFF_SCREEN) {
				if (i == 0)
					fb_para.fb_mode = FB_MODE_SCREEN0;
				else
					fb_para.fb_mode = FB_MODE_SCREEN1;
			} else if (g_fbi.disp_init.disp_mode ==
				   DISP_INIT_MODE_TWO_SAME_SCREEN) {
				fb_para.fb_mode = FB_MODE_DUAL_SAME_SCREEN_TB;
				fb_para.height *= 2;
				fb_para.output_height *= 2;
			} else if (g_fbi.disp_init.disp_mode ==
				   DISP_INIT_MODE_TWO_DIFF_SCREEN_SAME_CONTENTS) {
				fb_para.fb_mode =
				    FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS;
				fb_para.output_width =
				    BSP_disp_get_screen_width(fb_para.
							      primary_screen_id);
				fb_para.output_height =
				    BSP_disp_get_screen_height(fb_para.
							       primary_screen_id);
				fb_para.aux_output_width =
				    BSP_disp_get_screen_width(1 -
							      fb_para.
							      primary_screen_id);
				fb_para.aux_output_height =
				    BSP_disp_get_screen_height(1 -
							       fb_para.
							       primary_screen_id);
			}
			Display_Fb_Request(i, &fb_para);

#if 0
			fb_draw_colorbar((__u32)g_fbi.fbinfo[i]->screen_base,
					 fb_para.width, fb_para.height *
					 fb_para.buffer_num,
					 &(g_fbi.fbinfo[i]->var));
#endif
		}

		mutex_lock(&g_fbi_mutex);
		for (i = 0; i < SUNXI_MAX_FB; i++) {
			/* Register framebuffers after they are initialized */
			register_framebuffer(g_fbi.fbinfo[i]);
			g_fbi.fb_registered[i] = true;
		}
		mutex_unlock(&g_fbi_mutex);

		if (g_fbi.disp_init.scaler_mode[0])
			BSP_disp_print_reg(0, DISP_REG_SCALER0);

		if (g_fbi.disp_init.scaler_mode[1])
			BSP_disp_print_reg(0, DISP_REG_SCALER1);

		if (g_fbi.disp_init.disp_mode != DISP_INIT_MODE_SCREEN1) {
			BSP_disp_print_reg(0, DISP_REG_IMAGE0);
			BSP_disp_print_reg(0, DISP_REG_LCDC0);
			if ((g_fbi.disp_init.output_type[0] ==
			     DISP_OUTPUT_TYPE_TV) ||
			    (g_fbi.disp_init.output_type[0] ==
			     DISP_OUTPUT_TYPE_VGA))
				BSP_disp_print_reg(0, DISP_REG_TVEC0);
		}
		if (g_fbi.disp_init.disp_mode != DISP_INIT_MODE_SCREEN0) {
			BSP_disp_print_reg(0, DISP_REG_IMAGE1);
			BSP_disp_print_reg(0, DISP_REG_LCDC1);
			if ((g_fbi.disp_init.output_type[1] ==
			     DISP_OUTPUT_TYPE_TV) ||
			    (g_fbi.disp_init.output_type[1] ==
			     DISP_OUTPUT_TYPE_VGA))
				BSP_disp_print_reg(0, DISP_REG_TVEC1);
		}
		BSP_disp_print_reg(0, DISP_REG_CCMU);
		BSP_disp_print_reg(0, DISP_REG_PWM);
		BSP_disp_print_reg(0, DISP_REG_PIOC);
	}

	__inf("Fb_Init: END\n");
	return 0;
}
EXPORT_SYMBOL(Fb_Init);

__s32 Fb_Exit(void)
{
	__u8 fb_id = 0;

	for (fb_id = 0; fb_id < SUNXI_MAX_FB; fb_id++) {
		if (g_fbi.fbinfo[fb_id] == NULL)
			continue;

		Display_Fb_Release(fb_id);

		unregister_framebuffer(g_fbi.fbinfo[fb_id]);
		framebuffer_release(g_fbi.fbinfo[fb_id]);
		g_fbi.fbinfo[fb_id] = NULL;
	}

	return 0;
}
