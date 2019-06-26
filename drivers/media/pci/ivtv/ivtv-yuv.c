/*
    yuv support

    Copyright (C) 2007  Ian Armstrong <ian@iarmst.demon.co.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-udma.h"
#include "ivtv-yuv.h"

/* YUV buffer offsets */
const u32 yuv_offset[IVTV_YUV_BUFFERS] = {
	0x001a8600,
	0x00240400,
	0x002d8200,
	0x00370000,
	0x00029000,
	0x000C0E00,
	0x006B0400,
	0x00748200
};

static int ivtv_yuv_prep_user_dma(struct ivtv *itv, struct ivtv_user_dma *dma,
				  struct ivtv_dma_frame *args)
{
	struct ivtv_dma_page_info y_dma;
	struct ivtv_dma_page_info uv_dma;
	struct yuv_playback_info *yi = &itv->yuv_info;
	u8 frame = yi->draw_frame;
	struct yuv_frame_info *f = &yi->new_frame_info[frame];
	int i;
	int y_pages, uv_pages;
	unsigned long y_buffer_offset, uv_buffer_offset;
	int y_decode_height, uv_decode_height, y_size;

	y_buffer_offset = IVTV_DECODER_OFFSET + yuv_offset[frame];
	uv_buffer_offset = y_buffer_offset + IVTV_YUV_BUFFER_UV_OFFSET;

	y_decode_height = uv_decode_height = f->src_h + f->src_y;

	if (f->offset_y)
		y_buffer_offset += 720 * 16;

	if (y_decode_height & 15)
		y_decode_height = (y_decode_height + 16) & ~15;

	if (uv_decode_height & 31)
		uv_decode_height = (uv_decode_height + 32) & ~31;

	y_size = 720 * y_decode_height;

	/* Still in USE */
	if (dma->SG_length || dma->page_count) {
		IVTV_DEBUG_WARN
		    ("prep_user_dma: SG_length %d page_count %d still full?\n",
		     dma->SG_length, dma->page_count);
		return -EBUSY;
	}

	ivtv_udma_get_page_info (&y_dma, (unsigned long)args->y_source, 720 * y_decode_height);
	ivtv_udma_get_page_info (&uv_dma, (unsigned long)args->uv_source, 360 * uv_decode_height);

	/* Get user pages for DMA Xfer */
	y_pages = get_user_pages_unlocked(y_dma.uaddr,
			y_dma.page_count, &dma->map[0], FOLL_FORCE);
	uv_pages = 0; /* silence gcc. value is set and consumed only if: */
	if (y_pages == y_dma.page_count) {
		uv_pages = get_user_pages_unlocked(uv_dma.uaddr,
				uv_dma.page_count, &dma->map[y_pages],
				FOLL_FORCE);
	}

	if (y_pages != y_dma.page_count || uv_pages != uv_dma.page_count) {
		int rc = -EFAULT;

		if (y_pages == y_dma.page_count) {
			IVTV_DEBUG_WARN
				("failed to map uv user pages, returned %d expecting %d\n",
				 uv_pages, uv_dma.page_count);

			if (uv_pages >= 0) {
				for (i = 0; i < uv_pages; i++)
					put_page(dma->map[y_pages + i]);
				rc = -EFAULT;
			} else {
				rc = uv_pages;
			}
		} else {
			IVTV_DEBUG_WARN
				("failed to map y user pages, returned %d expecting %d\n",
				 y_pages, y_dma.page_count);
		}
		if (y_pages >= 0) {
			for (i = 0; i < y_pages; i++)
				put_page(dma->map[i]);
			/*
			 * Inherit the -EFAULT from rc's
			 * initialization, but allow it to be
			 * overridden by uv_pages above if it was an
			 * actual errno.
			 */
		} else {
			rc = y_pages;
		}
		return rc;
	}

	dma->page_count = y_pages + uv_pages;

	/* Fill & map SG List */
	if (ivtv_udma_fill_sg_list (dma, &uv_dma, ivtv_udma_fill_sg_list (dma, &y_dma, 0)) < 0) {
		IVTV_DEBUG_WARN("could not allocate bounce buffers for highmem userspace buffers\n");
		for (i = 0; i < dma->page_count; i++) {
			put_page(dma->map[i]);
		}
		dma->page_count = 0;
		return -ENOMEM;
	}
	dma->SG_length = pci_map_sg(itv->pdev, dma->SGlist, dma->page_count, PCI_DMA_TODEVICE);

	/* Fill SG Array with new values */
	ivtv_udma_fill_sg_array(dma, y_buffer_offset, uv_buffer_offset, y_size);

	/* If we've offset the y plane, ensure top area is blanked */
	if (f->offset_y && yi->blanking_dmaptr) {
		dma->SGarray[dma->SG_length].size = cpu_to_le32(720*16);
		dma->SGarray[dma->SG_length].src = cpu_to_le32(yi->blanking_dmaptr);
		dma->SGarray[dma->SG_length].dst = cpu_to_le32(IVTV_DECODER_OFFSET + yuv_offset[frame]);
		dma->SG_length++;
	}

	/* Tag SG Array with Interrupt Bit */
	dma->SGarray[dma->SG_length - 1].size |= cpu_to_le32(0x80000000);

	ivtv_udma_sync_for_device(itv);
	return 0;
}

/* We rely on a table held in the firmware - Quick check. */
int ivtv_yuv_filter_check(struct ivtv *itv)
{
	int i, y, uv;

	for (i = 0, y = 16, uv = 4; i < 16; i++, y += 24, uv += 12) {
		if ((read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + y) != i << 16) ||
		    (read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + uv) != i << 16)) {
			IVTV_WARN ("YUV filter table not found in firmware.\n");
			return -1;
		}
	}
	return 0;
}

static void ivtv_yuv_filter(struct ivtv *itv, int h_filter, int v_filter_1, int v_filter_2)
{
	u32 i, line;

	/* If any filter is -1, then don't update it */
	if (h_filter > -1) {
		if (h_filter > 4)
			h_filter = 4;
		i = IVTV_YUV_HORIZONTAL_FILTER_OFFSET + (h_filter * 384);
		for (line = 0; line < 16; line++) {
			write_reg(read_dec(i), 0x02804);
			write_reg(read_dec(i), 0x0281c);
			i += 4;
			write_reg(read_dec(i), 0x02808);
			write_reg(read_dec(i), 0x02820);
			i += 4;
			write_reg(read_dec(i), 0x0280c);
			write_reg(read_dec(i), 0x02824);
			i += 4;
			write_reg(read_dec(i), 0x02810);
			write_reg(read_dec(i), 0x02828);
			i += 4;
			write_reg(read_dec(i), 0x02814);
			write_reg(read_dec(i), 0x0282c);
			i += 8;
			write_reg(0, 0x02818);
			write_reg(0, 0x02830);
		}
		IVTV_DEBUG_YUV("h_filter -> %d\n", h_filter);
	}

	if (v_filter_1 > -1) {
		if (v_filter_1 > 4)
			v_filter_1 = 4;
		i = IVTV_YUV_VERTICAL_FILTER_OFFSET + (v_filter_1 * 192);
		for (line = 0; line < 16; line++) {
			write_reg(read_dec(i), 0x02900);
			i += 4;
			write_reg(read_dec(i), 0x02904);
			i += 8;
			write_reg(0, 0x02908);
		}
		IVTV_DEBUG_YUV("v_filter_1 -> %d\n", v_filter_1);
	}

	if (v_filter_2 > -1) {
		if (v_filter_2 > 4)
			v_filter_2 = 4;
		i = IVTV_YUV_VERTICAL_FILTER_OFFSET + (v_filter_2 * 192);
		for (line = 0; line < 16; line++) {
			write_reg(read_dec(i), 0x0290c);
			i += 4;
			write_reg(read_dec(i), 0x02910);
			i += 8;
			write_reg(0, 0x02914);
		}
		IVTV_DEBUG_YUV("v_filter_2 -> %d\n", v_filter_2);
	}
}

static void ivtv_yuv_handle_horizontal(struct ivtv *itv, struct yuv_frame_info *f)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	u32 reg_2834, reg_2838, reg_283c;
	u32 reg_2844, reg_2854, reg_285c;
	u32 reg_2864, reg_2874, reg_2890;
	u32 reg_2870, reg_2870_base, reg_2870_offset;
	int x_cutoff;
	int h_filter;
	u32 master_width;

	IVTV_DEBUG_WARN
	    ("Adjust to width %d src_w %d dst_w %d src_x %d dst_x %d\n",
	     f->tru_w, f->src_w, f->dst_w, f->src_x, f->dst_x);

	/* How wide is the src image */
	x_cutoff = f->src_w + f->src_x;

	/* Set the display width */
	reg_2834 = f->dst_w;
	reg_2838 = reg_2834;

	/* Set the display position */
	reg_2890 = f->dst_x;

	/* Index into the image horizontally */
	reg_2870 = 0;

	/* 2870 is normally fudged to align video coords with osd coords.
	   If running full screen, it causes an unwanted left shift
	   Remove the fudge if we almost fill the screen.
	   Gradually adjust the offset to avoid the video 'snapping'
	   left/right if it gets dragged through this region.
	   Only do this if osd is full width. */
	if (f->vis_w == 720) {
		if ((f->tru_x - f->pan_x > -1) && (f->tru_x - f->pan_x <= 40) && (f->dst_w >= 680))
			reg_2870 = 10 - (f->tru_x - f->pan_x) / 4;
		else if ((f->tru_x - f->pan_x < 0) && (f->tru_x - f->pan_x >= -20) && (f->dst_w >= 660))
			reg_2870 = (10 + (f->tru_x - f->pan_x) / 2);

		if (f->dst_w >= f->src_w)
			reg_2870 = reg_2870 << 16 | reg_2870;
		else
			reg_2870 = ((reg_2870 & ~1) << 15) | (reg_2870 & ~1);
	}

	if (f->dst_w < f->src_w)
		reg_2870 = 0x000d000e - reg_2870;
	else
		reg_2870 = 0x0012000e - reg_2870;

	/* We're also using 2870 to shift the image left (src_x & negative dst_x) */
	reg_2870_offset = (f->src_x * ((f->dst_w << 21) / f->src_w)) >> 19;

	if (f->dst_w >= f->src_w) {
		x_cutoff &= ~1;
		master_width = (f->src_w * 0x00200000) / (f->dst_w);
		if (master_width * f->dst_w != f->src_w * 0x00200000)
			master_width++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 2;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;

		/* We also need to factor in the scaling
		   (src_w - dst_w) / (src_w / 4) */
		if (f->dst_w > f->src_w)
			reg_2870_base = ((f->dst_w - f->src_w)<<16) / (f->src_w <<14);
		else
			reg_2870_base = 0;

		reg_2870 += (((reg_2870_offset << 14) & 0xFFFF0000) | reg_2870_offset >> 2) + (reg_2870_base << 17 | reg_2870_base);
		reg_2874 = 0;
	} else if (f->dst_w < f->src_w / 2) {
		master_width = (f->src_w * 0x00080000) / f->dst_w;
		if (master_width * f->dst_w != f->src_w * 0x00080000)
			master_width++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 1;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;
		reg_2870 += ((reg_2870_offset << 15) & 0xFFFF0000) | reg_2870_offset;
		reg_2870 += (5 - (((f->src_w + f->src_w / 2) - 1) / f->dst_w)) << 16;
		reg_2874 = 0x00000012;
	} else {
		master_width = (f->src_w * 0x00100000) / f->dst_w;
		if (master_width * f->dst_w != f->src_w * 0x00100000)
			master_width++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 1;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;
		reg_2870 += ((reg_2870_offset << 14) & 0xFFFF0000) | reg_2870_offset >> 1;
		reg_2870 += (5 - (((f->src_w * 3) - 1) / f->dst_w)) << 16;
		reg_2874 = 0x00000001;
	}

	/* Select the horizontal filter */
	if (f->src_w == f->dst_w) {
		/* An exact size match uses filter 0 */
		h_filter = 0;
	} else {
		/* Figure out which filter to use */
		h_filter = ((f->src_w << 16) / f->dst_w) >> 15;
		h_filter = (h_filter >> 1) + (h_filter & 1);
		/* Only an exact size match can use filter 0 */
		h_filter += !h_filter;
	}

	write_reg(reg_2834, 0x02834);
	write_reg(reg_2838, 0x02838);
	IVTV_DEBUG_YUV("Update reg 0x2834 %08x->%08x 0x2838 %08x->%08x\n",
		       yi->reg_2834, reg_2834, yi->reg_2838, reg_2838);

	write_reg(reg_283c, 0x0283c);
	write_reg(reg_2844, 0x02844);

	IVTV_DEBUG_YUV("Update reg 0x283c %08x->%08x 0x2844 %08x->%08x\n",
		       yi->reg_283c, reg_283c, yi->reg_2844, reg_2844);

	write_reg(0x00080514, 0x02840);
	write_reg(0x00100514, 0x02848);
	IVTV_DEBUG_YUV("Update reg 0x2840 %08x->%08x 0x2848 %08x->%08x\n",
		       yi->reg_2840, 0x00080514, yi->reg_2848, 0x00100514);

	write_reg(reg_2854, 0x02854);
	IVTV_DEBUG_YUV("Update reg 0x2854 %08x->%08x \n",
		       yi->reg_2854, reg_2854);

	write_reg(reg_285c, 0x0285c);
	write_reg(reg_2864, 0x02864);
	IVTV_DEBUG_YUV("Update reg 0x285c %08x->%08x 0x2864 %08x->%08x\n",
		       yi->reg_285c, reg_285c, yi->reg_2864, reg_2864);

	write_reg(reg_2874, 0x02874);
	IVTV_DEBUG_YUV("Update reg 0x2874 %08x->%08x\n",
		       yi->reg_2874, reg_2874);

	write_reg(reg_2870, 0x02870);
	IVTV_DEBUG_YUV("Update reg 0x2870 %08x->%08x\n",
		       yi->reg_2870, reg_2870);

	write_reg(reg_2890, 0x02890);
	IVTV_DEBUG_YUV("Update reg 0x2890 %08x->%08x\n",
		       yi->reg_2890, reg_2890);

	/* Only update the filter if we really need to */
	if (h_filter != yi->h_filter) {
		ivtv_yuv_filter(itv, h_filter, -1, -1);
		yi->h_filter = h_filter;
	}
}

static void ivtv_yuv_handle_vertical(struct ivtv *itv, struct yuv_frame_info *f)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	u32 master_height;
	u32 reg_2918, reg_291c, reg_2920, reg_2928;
	u32 reg_2930, reg_2934, reg_293c;
	u32 reg_2940, reg_2944, reg_294c;
	u32 reg_2950, reg_2954, reg_2958, reg_295c;
	u32 reg_2960, reg_2964, reg_2968, reg_296c;
	u32 reg_289c;
	u32 src_major_y, src_minor_y;
	u32 src_major_uv, src_minor_uv;
	u32 reg_2964_base, reg_2968_base;
	int v_filter_1, v_filter_2;

	IVTV_DEBUG_WARN
	    ("Adjust to height %d src_h %d dst_h %d src_y %d dst_y %d\n",
	     f->tru_h, f->src_h, f->dst_h, f->src_y, f->dst_y);

	/* What scaling mode is being used... */
	IVTV_DEBUG_YUV("Scaling mode Y: %s\n",
		       f->interlaced_y ? "Interlaced" : "Progressive");

	IVTV_DEBUG_YUV("Scaling mode UV: %s\n",
		       f->interlaced_uv ? "Interlaced" : "Progressive");

	/* What is the source video being treated as... */
	IVTV_DEBUG_WARN("Source video: %s\n",
			f->interlaced ? "Interlaced" : "Progressive");

	/* We offset into the image using two different index methods, so split
	   the y source coord into two parts. */
	if (f->src_y < 8) {
		src_minor_uv = f->src_y;
		src_major_uv = 0;
	} else {
		src_minor_uv = 8;
		src_major_uv = f->src_y - 8;
	}

	src_minor_y = src_minor_uv;
	src_major_y = src_major_uv;

	if (f->offset_y)
		src_minor_y += 16;

	if (f->interlaced_y)
		reg_2918 = (f->dst_h << 16) | (f->src_h + src_minor_y);
	else
		reg_2918 = (f->dst_h << 16) | ((f->src_h + src_minor_y) << 1);

	if (f->interlaced_uv)
		reg_291c = (f->dst_h << 16) | ((f->src_h + src_minor_uv) >> 1);
	else
		reg_291c = (f->dst_h << 16) | (f->src_h + src_minor_uv);

	reg_2964_base = (src_minor_y * ((f->dst_h << 16) / f->src_h)) >> 14;
	reg_2968_base = (src_minor_uv * ((f->dst_h << 16) / f->src_h)) >> 14;

	if (f->dst_h / 2 >= f->src_h && !f->interlaced_y) {
		master_height = (f->src_h * 0x00400000) / f->dst_h;
		if ((f->src_h * 0x00400000) - (master_height * f->dst_h) >= f->dst_h / 2)
			master_height++;
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 3;
		reg_2930 = master_height;
		reg_2940 = master_height >> 1;
		reg_2964_base >>= 3;
		reg_2968_base >>= 3;
		reg_296c = 0x00000000;
	} else if (f->dst_h >= f->src_h) {
		master_height = (f->src_h * 0x00400000) / f->dst_h;
		master_height = (master_height >> 1) + (master_height & 1);
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 2;
		reg_2930 = master_height;
		reg_2940 = master_height >> 1;
		reg_296c = 0x00000000;
		if (f->interlaced_y) {
			reg_2964_base >>= 3;
		} else {
			reg_296c++;
			reg_2964_base >>= 2;
		}
		if (f->interlaced_uv)
			reg_2928 >>= 1;
		reg_2968_base >>= 3;
	} else if (f->dst_h >= f->src_h / 2) {
		master_height = (f->src_h * 0x00200000) / f->dst_h;
		master_height = (master_height >> 1) + (master_height & 1);
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 2;
		reg_2930 = master_height;
		reg_2940 = master_height;
		reg_296c = 0x00000101;
		if (f->interlaced_y) {
			reg_2964_base >>= 2;
		} else {
			reg_296c++;
			reg_2964_base >>= 1;
		}
		if (f->interlaced_uv)
			reg_2928 >>= 1;
		reg_2968_base >>= 2;
	} else {
		master_height = (f->src_h * 0x00100000) / f->dst_h;
		master_height = (master_height >> 1) + (master_height & 1);
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 2;
		reg_2930 = master_height;
		reg_2940 = master_height;
		reg_2964_base >>= 1;
		reg_2968_base >>= 2;
		reg_296c = 0x00000102;
	}

	/* FIXME These registers change depending on scaled / unscaled output
	   We really need to work out what they should be */
	if (f->src_h == f->dst_h) {
		reg_2934 = 0x00020000;
		reg_293c = 0x00100000;
		reg_2944 = 0x00040000;
		reg_294c = 0x000b0000;
	} else {
		reg_2934 = 0x00000FF0;
		reg_293c = 0x00000FF0;
		reg_2944 = 0x00000FF0;
		reg_294c = 0x00000FF0;
	}

	/* The first line to be displayed */
	reg_2950 = 0x00010000 + src_major_y;
	if (f->interlaced_y)
		reg_2950 += 0x00010000;
	reg_2954 = reg_2950 + 1;

	reg_2958 = 0x00010000 + (src_major_y >> 1);
	if (f->interlaced_uv)
		reg_2958 += 0x00010000;
	reg_295c = reg_2958 + 1;

	if (yi->decode_height == 480)
		reg_289c = 0x011e0017;
	else
		reg_289c = 0x01500017;

	if (f->dst_y < 0)
		reg_289c = (reg_289c - ((f->dst_y & ~1)<<15))-(f->dst_y >>1);
	else
		reg_289c = (reg_289c + ((f->dst_y & ~1)<<15))+(f->dst_y >>1);

	/* How much of the source to decode.
	   Take into account the source offset */
	reg_2960 = ((src_minor_y + f->src_h + src_major_y) - 1) |
		(((src_minor_uv + f->src_h + src_major_uv - 1) & ~1) << 15);

	/* Calculate correct value for register 2964 */
	if (f->src_h == f->dst_h) {
		reg_2964 = 1;
	} else {
		reg_2964 = 2 + ((f->dst_h << 1) / f->src_h);
		reg_2964 = (reg_2964 >> 1) + (reg_2964 & 1);
	}
	reg_2968 = (reg_2964 << 16) + reg_2964 + (reg_2964 >> 1);
	reg_2964 = (reg_2964 << 16) + reg_2964 + (reg_2964 * 46 / 94);

	/* Okay, we've wasted time working out the correct value,
	   but if we use it, it fouls the the window alignment.
	   Fudge it to what we want... */
	reg_2964 = 0x00010001 + ((reg_2964 & 0x0000FFFF) - (reg_2964 >> 16));
	reg_2968 = 0x00010001 + ((reg_2968 & 0x0000FFFF) - (reg_2968 >> 16));

	/* Deviate further from what it should be. I find the flicker headache
	   inducing so try to reduce it slightly. Leave 2968 as-is otherwise
	   colours foul. */
	if ((reg_2964 != 0x00010001) && (f->dst_h / 2 <= f->src_h))
		reg_2964 = (reg_2964 & 0xFFFF0000) + ((reg_2964 & 0x0000FFFF) / 2);

	if (!f->interlaced_y)
		reg_2964 -= 0x00010001;
	if (!f->interlaced_uv)
		reg_2968 -= 0x00010001;

	reg_2964 += ((reg_2964_base << 16) | reg_2964_base);
	reg_2968 += ((reg_2968_base << 16) | reg_2968_base);

	/* Select the vertical filter */
	if (f->src_h == f->dst_h) {
		/* An exact size match uses filter 0/1 */
		v_filter_1 = 0;
		v_filter_2 = 1;
	} else {
		/* Figure out which filter to use */
		v_filter_1 = ((f->src_h << 16) / f->dst_h) >> 15;
		v_filter_1 = (v_filter_1 >> 1) + (v_filter_1 & 1);
		/* Only an exact size match can use filter 0 */
		v_filter_1 += !v_filter_1;
		v_filter_2 = v_filter_1;
	}

	write_reg(reg_2934, 0x02934);
	write_reg(reg_293c, 0x0293c);
	IVTV_DEBUG_YUV("Update reg 0x2934 %08x->%08x 0x293c %08x->%08x\n",
		       yi->reg_2934, reg_2934, yi->reg_293c, reg_293c);
	write_reg(reg_2944, 0x02944);
	write_reg(reg_294c, 0x0294c);
	IVTV_DEBUG_YUV("Update reg 0x2944 %08x->%08x 0x294c %08x->%08x\n",
		       yi->reg_2944, reg_2944, yi->reg_294c, reg_294c);

	/* Ensure 2970 is 0 (does it ever change ?) */
/*	write_reg(0,0x02970); */
/*	IVTV_DEBUG_YUV("Update reg 0x2970 %08x->%08x\n", yi->reg_2970, 0); */

	write_reg(reg_2930, 0x02938);
	write_reg(reg_2930, 0x02930);
	IVTV_DEBUG_YUV("Update reg 0x2930 %08x->%08x 0x2938 %08x->%08x\n",
		       yi->reg_2930, reg_2930, yi->reg_2938, reg_2930);

	write_reg(reg_2928, 0x02928);
	write_reg(reg_2928 + 0x514, 0x0292C);
	IVTV_DEBUG_YUV("Update reg 0x2928 %08x->%08x 0x292c %08x->%08x\n",
		       yi->reg_2928, reg_2928, yi->reg_292c, reg_2928 + 0x514);

	write_reg(reg_2920, 0x02920);
	write_reg(reg_2920 + 0x514, 0x02924);
	IVTV_DEBUG_YUV("Update reg 0x2920 %08x->%08x 0x2924 %08x->%08x\n",
		       yi->reg_2920, reg_2920, yi->reg_2924, reg_2920 + 0x514);

	write_reg(reg_2918, 0x02918);
	write_reg(reg_291c, 0x0291C);
	IVTV_DEBUG_YUV("Update reg 0x2918 %08x->%08x 0x291C %08x->%08x\n",
		       yi->reg_2918, reg_2918, yi->reg_291c, reg_291c);

	write_reg(reg_296c, 0x0296c);
	IVTV_DEBUG_YUV("Update reg 0x296c %08x->%08x\n",
		       yi->reg_296c, reg_296c);

	write_reg(reg_2940, 0x02948);
	write_reg(reg_2940, 0x02940);
	IVTV_DEBUG_YUV("Update reg 0x2940 %08x->%08x 0x2948 %08x->%08x\n",
		       yi->reg_2940, reg_2940, yi->reg_2948, reg_2940);

	write_reg(reg_2950, 0x02950);
	write_reg(reg_2954, 0x02954);
	IVTV_DEBUG_YUV("Update reg 0x2950 %08x->%08x 0x2954 %08x->%08x\n",
		       yi->reg_2950, reg_2950, yi->reg_2954, reg_2954);

	write_reg(reg_2958, 0x02958);
	write_reg(reg_295c, 0x0295C);
	IVTV_DEBUG_YUV("Update reg 0x2958 %08x->%08x 0x295C %08x->%08x\n",
		       yi->reg_2958, reg_2958, yi->reg_295c, reg_295c);

	write_reg(reg_2960, 0x02960);
	IVTV_DEBUG_YUV("Update reg 0x2960 %08x->%08x \n",
		       yi->reg_2960, reg_2960);

	write_reg(reg_2964, 0x02964);
	write_reg(reg_2968, 0x02968);
	IVTV_DEBUG_YUV("Update reg 0x2964 %08x->%08x 0x2968 %08x->%08x\n",
		       yi->reg_2964, reg_2964, yi->reg_2968, reg_2968);

	write_reg(reg_289c, 0x0289c);
	IVTV_DEBUG_YUV("Update reg 0x289c %08x->%08x\n",
		       yi->reg_289c, reg_289c);

	/* Only update filter 1 if we really need to */
	if (v_filter_1 != yi->v_filter_1) {
		ivtv_yuv_filter(itv, -1, v_filter_1, -1);
		yi->v_filter_1 = v_filter_1;
	}

	/* Only update filter 2 if we really need to */
	if (v_filter_2 != yi->v_filter_2) {
		ivtv_yuv_filter(itv, -1, -1, v_filter_2);
		yi->v_filter_2 = v_filter_2;
	}
}

/* Modify the supplied coordinate information to fit the visible osd area */
static u32 ivtv_yuv_window_setup(struct ivtv *itv, struct yuv_frame_info *f)
{
	struct yuv_frame_info *of = &itv->yuv_info.old_frame_info;
	int osd_crop;
	u32 osd_scale;
	u32 yuv_update = 0;

	/* Sorry, but no negative coords for src */
	if (f->src_x < 0)
		f->src_x = 0;
	if (f->src_y < 0)
		f->src_y = 0;

	/* Can only reduce width down to 1/4 original size */
	if ((osd_crop = f->src_w - 4 * f->dst_w) > 0) {
		f->src_x += osd_crop / 2;
		f->src_w = (f->src_w - osd_crop) & ~3;
		f->dst_w = f->src_w / 4;
		f->dst_w += f->dst_w & 1;
	}

	/* Can only reduce height down to 1/4 original size */
	if (f->src_h / f->dst_h >= 2) {
		/* Overflow may be because we're running progressive,
		   so force mode switch */
		f->interlaced_y = 1;
		/* Make sure we're still within limits for interlace */
		if ((osd_crop = f->src_h - 4 * f->dst_h) > 0) {
			/* If we reach here we'll have to force the height. */
			f->src_y += osd_crop / 2;
			f->src_h = (f->src_h - osd_crop) & ~3;
			f->dst_h = f->src_h / 4;
			f->dst_h += f->dst_h & 1;
		}
	}

	/* If there's nothing to safe to display, we may as well stop now */
	if ((int)f->dst_w <= 2 || (int)f->dst_h <= 2 ||
	    (int)f->src_w <= 2 || (int)f->src_h <= 2) {
		return IVTV_YUV_UPDATE_INVALID;
	}

	/* Ensure video remains inside OSD area */
	osd_scale = (f->src_h << 16) / f->dst_h;

	if ((osd_crop = f->pan_y - f->dst_y) > 0) {
		/* Falls off the upper edge - crop */
		f->src_y += (osd_scale * osd_crop) >> 16;
		f->src_h -= (osd_scale * osd_crop) >> 16;
		f->dst_h -= osd_crop;
		f->dst_y = 0;
	} else {
		f->dst_y -= f->pan_y;
	}

	if ((osd_crop = f->dst_h + f->dst_y - f->vis_h) > 0) {
		/* Falls off the lower edge - crop */
		f->dst_h -= osd_crop;
		f->src_h -= (osd_scale * osd_crop) >> 16;
	}

	osd_scale = (f->src_w << 16) / f->dst_w;

	if ((osd_crop = f->pan_x - f->dst_x) > 0) {
		/* Fall off the left edge - crop */
		f->src_x += (osd_scale * osd_crop) >> 16;
		f->src_w -= (osd_scale * osd_crop) >> 16;
		f->dst_w -= osd_crop;
		f->dst_x = 0;
	} else {
		f->dst_x -= f->pan_x;
	}

	if ((osd_crop = f->dst_w + f->dst_x - f->vis_w) > 0) {
		/* Falls off the right edge - crop */
		f->dst_w -= osd_crop;
		f->src_w -= (osd_scale * osd_crop) >> 16;
	}

	if (itv->yuv_info.track_osd) {
		/* The OSD can be moved. Track to it */
		f->dst_x += itv->yuv_info.osd_x_offset;
		f->dst_y += itv->yuv_info.osd_y_offset;
	}

	/* Width & height for both src & dst must be even.
	   Same for coordinates. */
	f->dst_w &= ~1;
	f->dst_x &= ~1;

	f->src_w += f->src_x & 1;
	f->src_x &= ~1;

	f->src_w &= ~1;
	f->dst_w &= ~1;

	f->dst_h &= ~1;
	f->dst_y &= ~1;

	f->src_h += f->src_y & 1;
	f->src_y &= ~1;

	f->src_h &= ~1;
	f->dst_h &= ~1;

	/* Due to rounding, we may have reduced the output size to <1/4 of
	   the source. Check again, but this time just resize. Don't change
	   source coordinates */
	if (f->dst_w < f->src_w / 4) {
		f->src_w &= ~3;
		f->dst_w = f->src_w / 4;
		f->dst_w += f->dst_w & 1;
	}
	if (f->dst_h < f->src_h / 4) {
		f->src_h &= ~3;
		f->dst_h = f->src_h / 4;
		f->dst_h += f->dst_h & 1;
	}

	/* Check again. If there's nothing to safe to display, stop now */
	if ((int)f->dst_w <= 2 || (int)f->dst_h <= 2 ||
	    (int)f->src_w <= 2 || (int)f->src_h <= 2) {
		return IVTV_YUV_UPDATE_INVALID;
	}

	/* Both x offset & width are linked, so they have to be done together */
	if ((of->dst_w != f->dst_w) || (of->src_w != f->src_w) ||
	    (of->dst_x != f->dst_x) || (of->src_x != f->src_x) ||
	    (of->pan_x != f->pan_x) || (of->vis_w != f->vis_w)) {
		yuv_update |= IVTV_YUV_UPDATE_HORIZONTAL;
	}

	if ((of->src_h != f->src_h) || (of->dst_h != f->dst_h) ||
	    (of->dst_y != f->dst_y) || (of->src_y != f->src_y) ||
	    (of->pan_y != f->pan_y) || (of->vis_h != f->vis_h) ||
	    (of->lace_mode != f->lace_mode) ||
	    (of->interlaced_y != f->interlaced_y) ||
	    (of->interlaced_uv != f->interlaced_uv)) {
		yuv_update |= IVTV_YUV_UPDATE_VERTICAL;
	}

	return yuv_update;
}

/* Update the scaling register to the requested value */
void ivtv_yuv_work_handler(struct ivtv *itv)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	struct yuv_frame_info f;
	int frame = yi->update_frame;
	u32 yuv_update;

	IVTV_DEBUG_YUV("Update yuv registers for frame %d\n", frame);
	f = yi->new_frame_info[frame];

	if (yi->track_osd) {
		/* Snapshot the osd pan info */
		f.pan_x = yi->osd_x_pan;
		f.pan_y = yi->osd_y_pan;
		f.vis_w = yi->osd_vis_w;
		f.vis_h = yi->osd_vis_h;
	} else {
		/* Not tracking the osd, so assume full screen */
		f.pan_x = 0;
		f.pan_y = 0;
		f.vis_w = 720;
		f.vis_h = yi->decode_height;
	}

	/* Calculate the display window coordinates. Exit if nothing left */
	if (!(yuv_update = ivtv_yuv_window_setup(itv, &f)))
		return;

	if (yuv_update & IVTV_YUV_UPDATE_INVALID) {
		write_reg(0x01008080, 0x2898);
	} else if (yuv_update) {
		write_reg(0x00108080, 0x2898);

		if (yuv_update & IVTV_YUV_UPDATE_HORIZONTAL)
			ivtv_yuv_handle_horizontal(itv, &f);

		if (yuv_update & IVTV_YUV_UPDATE_VERTICAL)
			ivtv_yuv_handle_vertical(itv, &f);
	}
	yi->old_frame_info = f;
}

static void ivtv_yuv_init(struct ivtv *itv)
{
	struct yuv_playback_info *yi = &itv->yuv_info;

	IVTV_DEBUG_YUV("ivtv_yuv_init\n");

	/* Take a snapshot of the current register settings */
	yi->reg_2834 = read_reg(0x02834);
	yi->reg_2838 = read_reg(0x02838);
	yi->reg_283c = read_reg(0x0283c);
	yi->reg_2840 = read_reg(0x02840);
	yi->reg_2844 = read_reg(0x02844);
	yi->reg_2848 = read_reg(0x02848);
	yi->reg_2854 = read_reg(0x02854);
	yi->reg_285c = read_reg(0x0285c);
	yi->reg_2864 = read_reg(0x02864);
	yi->reg_2870 = read_reg(0x02870);
	yi->reg_2874 = read_reg(0x02874);
	yi->reg_2898 = read_reg(0x02898);
	yi->reg_2890 = read_reg(0x02890);

	yi->reg_289c = read_reg(0x0289c);
	yi->reg_2918 = read_reg(0x02918);
	yi->reg_291c = read_reg(0x0291c);
	yi->reg_2920 = read_reg(0x02920);
	yi->reg_2924 = read_reg(0x02924);
	yi->reg_2928 = read_reg(0x02928);
	yi->reg_292c = read_reg(0x0292c);
	yi->reg_2930 = read_reg(0x02930);
	yi->reg_2934 = read_reg(0x02934);
	yi->reg_2938 = read_reg(0x02938);
	yi->reg_293c = read_reg(0x0293c);
	yi->reg_2940 = read_reg(0x02940);
	yi->reg_2944 = read_reg(0x02944);
	yi->reg_2948 = read_reg(0x02948);
	yi->reg_294c = read_reg(0x0294c);
	yi->reg_2950 = read_reg(0x02950);
	yi->reg_2954 = read_reg(0x02954);
	yi->reg_2958 = read_reg(0x02958);
	yi->reg_295c = read_reg(0x0295c);
	yi->reg_2960 = read_reg(0x02960);
	yi->reg_2964 = read_reg(0x02964);
	yi->reg_2968 = read_reg(0x02968);
	yi->reg_296c = read_reg(0x0296c);
	yi->reg_2970 = read_reg(0x02970);

	yi->v_filter_1 = -1;
	yi->v_filter_2 = -1;
	yi->h_filter = -1;

	/* Set some valid size info */
	yi->osd_x_offset = read_reg(0x02a04) & 0x00000FFF;
	yi->osd_y_offset = (read_reg(0x02a04) >> 16) & 0x00000FFF;

	/* Bit 2 of reg 2878 indicates current decoder output format
	   0 : NTSC    1 : PAL */
	if (read_reg(0x2878) & 4)
		yi->decode_height = 576;
	else
		yi->decode_height = 480;

	if (!itv->osd_info) {
		yi->osd_vis_w = 720 - yi->osd_x_offset;
		yi->osd_vis_h = yi->decode_height - yi->osd_y_offset;
	} else {
		/* If no visible size set, assume full size */
		if (!yi->osd_vis_w)
			yi->osd_vis_w = 720 - yi->osd_x_offset;

		if (!yi->osd_vis_h) {
			yi->osd_vis_h = yi->decode_height - yi->osd_y_offset;
		} else if (yi->osd_vis_h + yi->osd_y_offset > yi->decode_height) {
			/* If output video standard has changed, requested height may
			   not be legal */
			IVTV_DEBUG_WARN("Clipping yuv output - fb size (%d) exceeds video standard limit (%d)\n",
					yi->osd_vis_h + yi->osd_y_offset,
					yi->decode_height);
			yi->osd_vis_h = yi->decode_height - yi->osd_y_offset;
		}
	}

	/* We need a buffer for blanking when Y plane is offset - non-fatal if we can't get one */
	yi->blanking_ptr = kzalloc(720 * 16, GFP_ATOMIC|__GFP_NOWARN);
	if (yi->blanking_ptr) {
		yi->blanking_dmaptr = pci_map_single(itv->pdev, yi->blanking_ptr, 720*16, PCI_DMA_TODEVICE);
	} else {
		yi->blanking_dmaptr = 0;
		IVTV_DEBUG_WARN("Failed to allocate yuv blanking buffer\n");
	}

	/* Enable YUV decoder output */
	write_reg_sync(0x01, IVTV_REG_VDM);

	set_bit(IVTV_F_I_DECODING_YUV, &itv->i_flags);
	atomic_set(&yi->next_dma_frame, 0);
}

/* Get next available yuv buffer on PVR350 */
static void ivtv_yuv_next_free(struct ivtv *itv)
{
	int draw, display;
	struct yuv_playback_info *yi = &itv->yuv_info;

	if (atomic_read(&yi->next_dma_frame) == -1)
		ivtv_yuv_init(itv);

	draw = atomic_read(&yi->next_fill_frame);
	display = atomic_read(&yi->next_dma_frame);

	if (display > draw)
		display -= IVTV_YUV_BUFFERS;

	if (draw - display >= yi->max_frames_buffered)
		draw = (u8)(draw - 1) % IVTV_YUV_BUFFERS;
	else
		yi->new_frame_info[draw].update = 0;

	yi->draw_frame = draw;
}

/* Set up frame according to ivtv_dma_frame parameters */
static void ivtv_yuv_setup_frame(struct ivtv *itv, struct ivtv_dma_frame *args)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	u8 frame = yi->draw_frame;
	u8 last_frame = (u8)(frame - 1) % IVTV_YUV_BUFFERS;
	struct yuv_frame_info *nf = &yi->new_frame_info[frame];
	struct yuv_frame_info *of = &yi->new_frame_info[last_frame];
	int lace_threshold = yi->lace_threshold;

	/* Preserve old update flag in case we're overwriting a queued frame */
	int update = nf->update;

	/* Take a snapshot of the yuv coordinate information */
	nf->src_x = args->src.left;
	nf->src_y = args->src.top;
	nf->src_w = args->src.width;
	nf->src_h = args->src.height;
	nf->dst_x = args->dst.left;
	nf->dst_y = args->dst.top;
	nf->dst_w = args->dst.width;
	nf->dst_h = args->dst.height;
	nf->tru_x = args->dst.left;
	nf->tru_w = args->src_width;
	nf->tru_h = args->src_height;

	/* Are we going to offset the Y plane */
	nf->offset_y = (nf->tru_h + nf->src_x < 512 - 16) ? 1 : 0;

	nf->update = 0;
	nf->interlaced_y = 0;
	nf->interlaced_uv = 0;
	nf->delay = 0;
	nf->sync_field = 0;
	nf->lace_mode = yi->lace_mode & IVTV_YUV_MODE_MASK;

	if (lace_threshold < 0)
		lace_threshold = yi->decode_height - 1;

	/* Work out the lace settings */
	switch (nf->lace_mode) {
	case IVTV_YUV_MODE_PROGRESSIVE: /* Progressive mode */
		nf->interlaced = 0;
		if (nf->tru_h < 512 || (nf->tru_h > 576 && nf->tru_h < 1021))
			nf->interlaced_y = 0;
		else
			nf->interlaced_y = 1;

		if (nf->tru_h < 1021 && (nf->dst_h >= nf->src_h / 2))
			nf->interlaced_uv = 0;
		else
			nf->interlaced_uv = 1;
		break;

	case IVTV_YUV_MODE_AUTO:
		if (nf->tru_h <= lace_threshold || nf->tru_h > 576 || nf->tru_w > 720) {
			nf->interlaced = 0;
			if ((nf->tru_h < 512) ||
			    (nf->tru_h > 576 && nf->tru_h < 1021) ||
			    (nf->tru_w > 720 && nf->tru_h < 1021))
				nf->interlaced_y = 0;
			else
				nf->interlaced_y = 1;
			if (nf->tru_h < 1021 && (nf->dst_h >= nf->src_h / 2))
				nf->interlaced_uv = 0;
			else
				nf->interlaced_uv = 1;
		} else {
			nf->interlaced = 1;
			nf->interlaced_y = 1;
			nf->interlaced_uv = 1;
		}
		break;

	case IVTV_YUV_MODE_INTERLACED: /* Interlace mode */
	default:
		nf->interlaced = 1;
		nf->interlaced_y = 1;
		nf->interlaced_uv = 1;
		break;
	}

	if (memcmp(&yi->old_frame_info_args, nf, sizeof(*nf))) {
		yi->old_frame_info_args = *nf;
		nf->update = 1;
		IVTV_DEBUG_YUV("Requesting reg update for frame %d\n", frame);
	}

	nf->update |= update;
	nf->sync_field = yi->lace_sync_field;
	nf->delay = nf->sync_field != of->sync_field;
}

/* Frame is complete & ready for display */
void ivtv_yuv_frame_complete(struct ivtv *itv)
{
	atomic_set(&itv->yuv_info.next_fill_frame,
			(itv->yuv_info.draw_frame + 1) % IVTV_YUV_BUFFERS);
}

static int ivtv_yuv_udma_frame(struct ivtv *itv, struct ivtv_dma_frame *args)
{
	DEFINE_WAIT(wait);
	int rc = 0;
	int got_sig = 0;
	/* DMA the frame */
	mutex_lock(&itv->udma.lock);

	if ((rc = ivtv_yuv_prep_user_dma(itv, &itv->udma, args)) != 0) {
		mutex_unlock(&itv->udma.lock);
		return rc;
	}

	ivtv_udma_prepare(itv);
	prepare_to_wait(&itv->dma_waitq, &wait, TASK_INTERRUPTIBLE);
	/* if no UDMA is pending and no UDMA is in progress, then the DMA
	   is finished */
	while (test_bit(IVTV_F_I_UDMA_PENDING, &itv->i_flags) ||
	       test_bit(IVTV_F_I_UDMA, &itv->i_flags)) {
		/* don't interrupt if the DMA is in progress but break off
		   a still pending DMA. */
		got_sig = signal_pending(current);
		if (got_sig && test_and_clear_bit(IVTV_F_I_UDMA_PENDING, &itv->i_flags))
			break;
		got_sig = 0;
		schedule();
	}
	finish_wait(&itv->dma_waitq, &wait);

	/* Unmap Last DMA Xfer */
	ivtv_udma_unmap(itv);

	if (got_sig) {
		IVTV_DEBUG_INFO("User stopped YUV UDMA\n");
		mutex_unlock(&itv->udma.lock);
		return -EINTR;
	}

	ivtv_yuv_frame_complete(itv);

	mutex_unlock(&itv->udma.lock);
	return rc;
}

/* Setup frame according to V4L2 parameters */
void ivtv_yuv_setup_stream_frame(struct ivtv *itv)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	struct ivtv_dma_frame dma_args;

	ivtv_yuv_next_free(itv);

	/* Copy V4L2 parameters to an ivtv_dma_frame struct... */
	dma_args.y_source = NULL;
	dma_args.uv_source = NULL;
	dma_args.src.left = 0;
	dma_args.src.top = 0;
	dma_args.src.width = yi->v4l2_src_w;
	dma_args.src.height = yi->v4l2_src_h;
	dma_args.dst = yi->main_rect;
	dma_args.src_width = yi->v4l2_src_w;
	dma_args.src_height = yi->v4l2_src_h;

	/* ... and use the same setup routine as ivtv_yuv_prep_frame */
	ivtv_yuv_setup_frame(itv, &dma_args);

	if (!itv->dma_data_req_offset)
		itv->dma_data_req_offset = yuv_offset[yi->draw_frame];
}

/* Attempt to dma a frame from a user buffer */
int ivtv_yuv_udma_stream_frame(struct ivtv *itv, void __user *src)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	struct ivtv_dma_frame dma_args;
	int res;

	ivtv_yuv_setup_stream_frame(itv);

	/* We only need to supply source addresses for this */
	dma_args.y_source = src;
	dma_args.uv_source = src + 720 * ((yi->v4l2_src_h + 31) & ~31);
	/* Wait for frame DMA. Note that serialize_lock is locked,
	   so to allow other processes to access the driver while
	   we are waiting unlock first and later lock again. */
	mutex_unlock(&itv->serialize_lock);
	res = ivtv_yuv_udma_frame(itv, &dma_args);
	mutex_lock(&itv->serialize_lock);
	return res;
}

/* IVTV_IOC_DMA_FRAME ioctl handler */
int ivtv_yuv_prep_frame(struct ivtv *itv, struct ivtv_dma_frame *args)
{
	int res;

/*	IVTV_DEBUG_INFO("yuv_prep_frame\n"); */
	ivtv_yuv_next_free(itv);
	ivtv_yuv_setup_frame(itv, args);
	/* Wait for frame DMA. Note that serialize_lock is locked,
	   so to allow other processes to access the driver while
	   we are waiting unlock first and later lock again. */
	mutex_unlock(&itv->serialize_lock);
	res = ivtv_yuv_udma_frame(itv, args);
	mutex_lock(&itv->serialize_lock);
	return res;
}

void ivtv_yuv_close(struct ivtv *itv)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	int h_filter, v_filter_1, v_filter_2;

	IVTV_DEBUG_YUV("ivtv_yuv_close\n");
	mutex_unlock(&itv->serialize_lock);
	ivtv_waitq(&itv->vsync_waitq);
	mutex_lock(&itv->serialize_lock);

	yi->running = 0;
	atomic_set(&yi->next_dma_frame, -1);
	atomic_set(&yi->next_fill_frame, 0);

	/* Reset registers we have changed so mpeg playback works */

	/* If we fully restore this register, the display may remain active.
	   Restore, but set one bit to blank the video. Firmware will always
	   clear this bit when needed, so not a problem. */
	write_reg(yi->reg_2898 | 0x01000000, 0x2898);

	write_reg(yi->reg_2834, 0x02834);
	write_reg(yi->reg_2838, 0x02838);
	write_reg(yi->reg_283c, 0x0283c);
	write_reg(yi->reg_2840, 0x02840);
	write_reg(yi->reg_2844, 0x02844);
	write_reg(yi->reg_2848, 0x02848);
	write_reg(yi->reg_2854, 0x02854);
	write_reg(yi->reg_285c, 0x0285c);
	write_reg(yi->reg_2864, 0x02864);
	write_reg(yi->reg_2870, 0x02870);
	write_reg(yi->reg_2874, 0x02874);
	write_reg(yi->reg_2890, 0x02890);
	write_reg(yi->reg_289c, 0x0289c);

	write_reg(yi->reg_2918, 0x02918);
	write_reg(yi->reg_291c, 0x0291c);
	write_reg(yi->reg_2920, 0x02920);
	write_reg(yi->reg_2924, 0x02924);
	write_reg(yi->reg_2928, 0x02928);
	write_reg(yi->reg_292c, 0x0292c);
	write_reg(yi->reg_2930, 0x02930);
	write_reg(yi->reg_2934, 0x02934);
	write_reg(yi->reg_2938, 0x02938);
	write_reg(yi->reg_293c, 0x0293c);
	write_reg(yi->reg_2940, 0x02940);
	write_reg(yi->reg_2944, 0x02944);
	write_reg(yi->reg_2948, 0x02948);
	write_reg(yi->reg_294c, 0x0294c);
	write_reg(yi->reg_2950, 0x02950);
	write_reg(yi->reg_2954, 0x02954);
	write_reg(yi->reg_2958, 0x02958);
	write_reg(yi->reg_295c, 0x0295c);
	write_reg(yi->reg_2960, 0x02960);
	write_reg(yi->reg_2964, 0x02964);
	write_reg(yi->reg_2968, 0x02968);
	write_reg(yi->reg_296c, 0x0296c);
	write_reg(yi->reg_2970, 0x02970);

	/* Prepare to restore filters */

	/* First the horizontal filter */
	if ((yi->reg_2834 & 0x0000FFFF) == (yi->reg_2834 >> 16)) {
		/* An exact size match uses filter 0 */
		h_filter = 0;
	} else {
		/* Figure out which filter to use */
		h_filter = ((yi->reg_2834 << 16) / (yi->reg_2834 >> 16)) >> 15;
		h_filter = (h_filter >> 1) + (h_filter & 1);
		/* Only an exact size match can use filter 0. */
		h_filter += !h_filter;
	}

	/* Now the vertical filter */
	if ((yi->reg_2918 & 0x0000FFFF) == (yi->reg_2918 >> 16)) {
		/* An exact size match uses filter 0/1 */
		v_filter_1 = 0;
		v_filter_2 = 1;
	} else {
		/* Figure out which filter to use */
		v_filter_1 = ((yi->reg_2918 << 16) / (yi->reg_2918 >> 16)) >> 15;
		v_filter_1 = (v_filter_1 >> 1) + (v_filter_1 & 1);
		/* Only an exact size match can use filter 0 */
		v_filter_1 += !v_filter_1;
		v_filter_2 = v_filter_1;
	}

	/* Now restore the filters */
	ivtv_yuv_filter(itv, h_filter, v_filter_1, v_filter_2);

	/* and clear a few registers */
	write_reg(0, 0x02814);
	write_reg(0, 0x0282c);
	write_reg(0, 0x02904);
	write_reg(0, 0x02910);

	/* Release the blanking buffer */
	if (yi->blanking_ptr) {
		kfree(yi->blanking_ptr);
		yi->blanking_ptr = NULL;
		pci_unmap_single(itv->pdev, yi->blanking_dmaptr, 720*16, PCI_DMA_TODEVICE);
	}

	/* Invalidate the old dimension information */
	yi->old_frame_info.src_w = 0;
	yi->old_frame_info.src_h = 0;
	yi->old_frame_info_args.src_w = 0;
	yi->old_frame_info_args.src_h = 0;

	/* All done. */
	clear_bit(IVTV_F_I_DECODING_YUV, &itv->i_flags);
}
