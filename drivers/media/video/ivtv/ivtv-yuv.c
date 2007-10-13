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

const u32 yuv_offset[4] = {
	IVTV_YUV_BUFFER_OFFSET,
	IVTV_YUV_BUFFER_OFFSET_1,
	IVTV_YUV_BUFFER_OFFSET_2,
	IVTV_YUV_BUFFER_OFFSET_3
};

static int ivtv_yuv_prep_user_dma(struct ivtv *itv, struct ivtv_user_dma *dma,
				 struct ivtv_dma_frame *args)
{
	struct ivtv_dma_page_info y_dma;
	struct ivtv_dma_page_info uv_dma;

	int i;
	int y_pages, uv_pages;

	unsigned long y_buffer_offset, uv_buffer_offset;
	int y_decode_height, uv_decode_height, y_size;
	int frame = atomic_read(&itv->yuv_info.next_fill_frame);

	y_buffer_offset = IVTV_DECODER_OFFSET + yuv_offset[frame];
	uv_buffer_offset = y_buffer_offset + IVTV_YUV_BUFFER_UV_OFFSET;

	y_decode_height = uv_decode_height = args->src.height + args->src.top;

	if (y_decode_height < 512-16)
		y_buffer_offset += 720 * 16;

	if (y_decode_height & 15)
		y_decode_height = (y_decode_height + 16) & ~15;

	if (uv_decode_height & 31)
		uv_decode_height = (uv_decode_height + 32) & ~31;

	y_size = 720 * y_decode_height;

	/* Still in USE */
	if (dma->SG_length || dma->page_count) {
		IVTV_DEBUG_WARN("prep_user_dma: SG_length %d page_count %d still full?\n",
				dma->SG_length, dma->page_count);
		return -EBUSY;
	}

	ivtv_udma_get_page_info (&y_dma, (unsigned long)args->y_source, 720 * y_decode_height);
	ivtv_udma_get_page_info (&uv_dma, (unsigned long)args->uv_source, 360 * uv_decode_height);

	/* Get user pages for DMA Xfer */
	down_read(&current->mm->mmap_sem);
	y_pages = get_user_pages(current, current->mm, y_dma.uaddr, y_dma.page_count, 0, 1, &dma->map[0], NULL);
	uv_pages = get_user_pages(current, current->mm, uv_dma.uaddr, uv_dma.page_count, 0, 1, &dma->map[y_pages], NULL);
	up_read(&current->mm->mmap_sem);

	dma->page_count = y_dma.page_count + uv_dma.page_count;

	if (y_pages + uv_pages != dma->page_count) {
		IVTV_DEBUG_WARN("failed to map user pages, returned %d instead of %d\n",
				y_pages + uv_pages, dma->page_count);

		for (i = 0; i < dma->page_count; i++) {
			put_page(dma->map[i]);
		}
		dma->page_count = 0;
		return -EINVAL;
	}

	/* Fill & map SG List */
	if (ivtv_udma_fill_sg_list (dma, &uv_dma, ivtv_udma_fill_sg_list (dma, &y_dma, 0)) < 0) {
		IVTV_DEBUG_WARN("could not allocate bounce buffers for highmem userspace buffers\n");
		for (i = 0; i < dma->page_count; i++) {
			put_page(dma->map[i]);
		}
		dma->page_count = 0;
		return -ENOMEM;
	}
	dma->SG_length = pci_map_sg(itv->dev, dma->SGlist, dma->page_count, PCI_DMA_TODEVICE);

	/* Fill SG Array with new values */
	ivtv_udma_fill_sg_array (dma, y_buffer_offset, uv_buffer_offset, y_size);

	/* If we've offset the y plane, ensure top area is blanked */
	if (args->src.height + args->src.top < 512-16) {
		if (itv->yuv_info.blanking_dmaptr) {
			dma->SGarray[dma->SG_length].size = cpu_to_le32(720*16);
			dma->SGarray[dma->SG_length].src = cpu_to_le32(itv->yuv_info.blanking_dmaptr);
			dma->SGarray[dma->SG_length].dst = cpu_to_le32(IVTV_DECODER_OFFSET + yuv_offset[frame]);
			dma->SG_length++;
		}
	}

	/* Tag SG Array with Interrupt Bit */
	dma->SGarray[dma->SG_length - 1].size |= cpu_to_le32(0x80000000);

	ivtv_udma_sync_for_device(itv);
	return 0;
}

/* We rely on a table held in the firmware - Quick check. */
int ivtv_yuv_filter_check(struct ivtv *itv)
{
	int i, offset_y, offset_uv;

	for (i=0, offset_y = 16, offset_uv = 4; i<16; i++, offset_y += 24, offset_uv += 12) {
		if ((read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + offset_y) != i << 16) ||
		    (read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + offset_uv) != i << 16)) {
			IVTV_WARN ("YUV filter table not found in firmware.\n");
			return -1;
		}
	}
	return 0;
}

static void ivtv_yuv_filter(struct ivtv *itv, int h_filter, int v_filter_1, int v_filter_2)
{
	int filter_index, filter_line;

	/* If any filter is -1, then don't update it */
	if (h_filter > -1) {
		if (h_filter > 4) h_filter = 4;
		filter_index = h_filter * 384;
		filter_line = 0;
		while (filter_line < 16) {
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02804);
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x0281c);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02808);
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02820);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x0280c);
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02824);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02810);
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02828);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x02814);
			write_reg(read_dec(IVTV_YUV_HORIZONTAL_FILTER_OFFSET + filter_index), 0x0282c);
			filter_index += 8;
			write_reg(0, 0x02818);
			write_reg(0, 0x02830);
			filter_line ++;
		}
		IVTV_DEBUG_YUV("h_filter -> %d\n",h_filter);
	}

	if (v_filter_1 > -1) {
		if (v_filter_1 > 4) v_filter_1 = 4;
		filter_index = v_filter_1 * 192;
		filter_line = 0;
		while (filter_line < 16) {
			write_reg(read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + filter_index), 0x02900);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + filter_index), 0x02904);
			filter_index += 8;
			write_reg(0, 0x02908);
			filter_line ++;
		}
		IVTV_DEBUG_YUV("v_filter_1 -> %d\n",v_filter_1);
	}

	if (v_filter_2 > -1) {
		if (v_filter_2 > 4) v_filter_2 = 4;
		filter_index = v_filter_2 * 192;
		filter_line = 0;
		while (filter_line < 16) {
			write_reg(read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + filter_index), 0x0290c);
			filter_index += 4;
			write_reg(read_dec(IVTV_YUV_VERTICAL_FILTER_OFFSET + filter_index), 0x02910);
			filter_index += 8;
			write_reg(0, 0x02914);
			filter_line ++;
		}
		IVTV_DEBUG_YUV("v_filter_2 -> %d\n",v_filter_2);
	}
}

static void ivtv_yuv_handle_horizontal(struct ivtv *itv, struct yuv_frame_info *window)
{
	u32 reg_2834, reg_2838, reg_283c;
	u32 reg_2844, reg_2854, reg_285c;
	u32 reg_2864, reg_2874, reg_2890;
	u32 reg_2870, reg_2870_base, reg_2870_offset;
	int x_cutoff;
	int h_filter;
	u32 master_width;

	IVTV_DEBUG_WARN( "Need to adjust to width %d src_w %d dst_w %d src_x %d dst_x %d\n",
			 window->tru_w, window->src_w, window->dst_w,window->src_x, window->dst_x);

	/* How wide is the src image */
	x_cutoff  = window->src_w + window->src_x;

	/* Set the display width */
	reg_2834 = window->dst_w;
	reg_2838 = reg_2834;

	/* Set the display position */
	reg_2890 = window->dst_x;

	/* Index into the image horizontally */
	reg_2870 = 0;

	/* 2870 is normally fudged to align video coords with osd coords.
	   If running full screen, it causes an unwanted left shift
	   Remove the fudge if we almost fill the screen.
	   Gradually adjust the offset to avoid the video 'snapping'
	   left/right if it gets dragged through this region.
	   Only do this if osd is full width. */
	if (window->vis_w == 720) {
		if ((window->tru_x - window->pan_x > -1) && (window->tru_x - window->pan_x <= 40) && (window->dst_w >= 680)){
			reg_2870 = 10 - (window->tru_x - window->pan_x) / 4;
		}
		else if ((window->tru_x - window->pan_x < 0) && (window->tru_x - window->pan_x >= -20) && (window->dst_w >= 660)) {
			reg_2870 = (10 + (window->tru_x - window->pan_x) / 2);
		}

		if (window->dst_w >= window->src_w)
			reg_2870 = reg_2870 << 16 | reg_2870;
		else
			reg_2870 = ((reg_2870 & ~1) << 15) | (reg_2870 & ~1);
	}

	if (window->dst_w < window->src_w)
		reg_2870 = 0x000d000e - reg_2870;
	else
		reg_2870 = 0x0012000e - reg_2870;

	/* We're also using 2870 to shift the image left (src_x & negative dst_x) */
	reg_2870_offset = (window->src_x*((window->dst_w << 21)/window->src_w))>>19;

	if (window->dst_w >= window->src_w) {
		x_cutoff &= ~1;
		master_width = (window->src_w * 0x00200000) / (window->dst_w);
		if (master_width * window->dst_w != window->src_w * 0x00200000) master_width ++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 2;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;

		/* We also need to factor in the scaling
		   (src_w - dst_w) / (src_w / 4) */
		if (window->dst_w > window->src_w)
			reg_2870_base = ((window->dst_w - window->src_w)<<16) / (window->src_w <<14);
		else
			reg_2870_base = 0;

		reg_2870 += (((reg_2870_offset << 14) & 0xFFFF0000) | reg_2870_offset >> 2) + (reg_2870_base << 17 | reg_2870_base);
		reg_2874 = 0;
	}
	else if (window->dst_w < window->src_w / 2) {
		master_width = (window->src_w * 0x00080000) / window->dst_w;
		if (master_width * window->dst_w != window->src_w * 0x00080000) master_width ++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 1;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;
		reg_2870 += (((reg_2870_offset << 15) & 0xFFFF0000) | reg_2870_offset);
		reg_2870 += (5 - (((window->src_w + window->src_w / 2) - 1) / window->dst_w)) << 16;
		reg_2874 = 0x00000012;
	}
	else {
		master_width = (window->src_w * 0x00100000) / window->dst_w;
		if (master_width * window->dst_w != window->src_w * 0x00100000) master_width ++;
		reg_2834 = (reg_2834 << 16) | x_cutoff;
		reg_2838 = (reg_2838 << 16) | x_cutoff;
		reg_283c = master_width >> 2;
		reg_2844 = master_width >> 1;
		reg_2854 = master_width;
		reg_285c = master_width >> 1;
		reg_2864 = master_width >> 1;
		reg_2870 += (((reg_2870_offset << 14) & 0xFFFF0000) | reg_2870_offset >> 1);
		reg_2870 += (5 - (((window->src_w * 3) - 1) / window->dst_w)) << 16;
		reg_2874 = 0x00000001;
	}

	/* Select the horizontal filter */
	if (window->src_w == window->dst_w) {
		/* An exact size match uses filter 0 */
		h_filter = 0;
	}
	else {
		/* Figure out which filter to use */
		h_filter = ((window->src_w << 16) / window->dst_w) >> 15;
		h_filter = (h_filter >> 1) + (h_filter & 1);
		/* Only an exact size match can use filter 0 */
		if (h_filter == 0) h_filter = 1;
	}

	write_reg(reg_2834, 0x02834);
	write_reg(reg_2838, 0x02838);
	IVTV_DEBUG_YUV("Update reg 0x2834 %08x->%08x 0x2838 %08x->%08x\n",itv->yuv_info.reg_2834, reg_2834, itv->yuv_info.reg_2838, reg_2838);

	write_reg(reg_283c, 0x0283c);
	write_reg(reg_2844, 0x02844);

	IVTV_DEBUG_YUV("Update reg 0x283c %08x->%08x 0x2844 %08x->%08x\n",itv->yuv_info.reg_283c, reg_283c, itv->yuv_info.reg_2844, reg_2844);

	write_reg(0x00080514, 0x02840);
	write_reg(0x00100514, 0x02848);
	IVTV_DEBUG_YUV("Update reg 0x2840 %08x->%08x 0x2848 %08x->%08x\n",itv->yuv_info.reg_2840, 0x00080514, itv->yuv_info.reg_2848, 0x00100514);

	write_reg(reg_2854, 0x02854);
	IVTV_DEBUG_YUV("Update reg 0x2854 %08x->%08x \n",itv->yuv_info.reg_2854, reg_2854);

	write_reg(reg_285c, 0x0285c);
	write_reg(reg_2864, 0x02864);
	IVTV_DEBUG_YUV("Update reg 0x285c %08x->%08x 0x2864 %08x->%08x\n",itv->yuv_info.reg_285c, reg_285c, itv->yuv_info.reg_2864, reg_2864);

	write_reg(reg_2874, 0x02874);
	IVTV_DEBUG_YUV("Update reg 0x2874 %08x->%08x\n",itv->yuv_info.reg_2874, reg_2874);

	write_reg(reg_2870, 0x02870);
	IVTV_DEBUG_YUV("Update reg 0x2870 %08x->%08x\n",itv->yuv_info.reg_2870, reg_2870);

	write_reg( reg_2890,0x02890);
	IVTV_DEBUG_YUV("Update reg 0x2890 %08x->%08x\n",itv->yuv_info.reg_2890, reg_2890);

	/* Only update the filter if we really need to */
	if (h_filter != itv->yuv_info.h_filter) {
		ivtv_yuv_filter (itv,h_filter,-1,-1);
		itv->yuv_info.h_filter = h_filter;
	}
}

static void ivtv_yuv_handle_vertical(struct ivtv *itv, struct yuv_frame_info *window)
{
	u32 master_height;
	u32 reg_2918, reg_291c, reg_2920, reg_2928;
	u32 reg_2930, reg_2934, reg_293c;
	u32 reg_2940, reg_2944, reg_294c;
	u32 reg_2950, reg_2954, reg_2958, reg_295c;
	u32 reg_2960, reg_2964, reg_2968, reg_296c;
	u32 reg_289c;
	u32 src_y_major_y, src_y_minor_y;
	u32 src_y_major_uv, src_y_minor_uv;
	u32 reg_2964_base, reg_2968_base;
	int v_filter_1, v_filter_2;

	IVTV_DEBUG_WARN("Need to adjust to height %d src_h %d dst_h %d src_y %d dst_y %d\n",
		window->tru_h, window->src_h, window->dst_h,window->src_y, window->dst_y);

	/* What scaling mode is being used... */
	if (window->interlaced_y) {
		IVTV_DEBUG_YUV("Scaling mode Y: Interlaced\n");
	}
	else {
		IVTV_DEBUG_YUV("Scaling mode Y: Progressive\n");
	}

	if (window->interlaced_uv) {
		IVTV_DEBUG_YUV("Scaling mode UV: Interlaced\n");
	}
	else {
		IVTV_DEBUG_YUV("Scaling mode UV: Progressive\n");
	}

	/* What is the source video being treated as... */
	if (itv->yuv_info.frame_interlaced) {
		IVTV_DEBUG_WARN("Source video: Interlaced\n");
	}
	else {
		IVTV_DEBUG_WARN("Source video: Non-interlaced\n");
	}

	/* We offset into the image using two different index methods, so split
	   the y source coord into two parts. */
	if (window->src_y < 8) {
		src_y_minor_uv = window->src_y;
		src_y_major_uv = 0;
	}
	else {
		src_y_minor_uv = 8;
		src_y_major_uv = window->src_y - 8;
	}

	src_y_minor_y = src_y_minor_uv;
	src_y_major_y = src_y_major_uv;

	if (window->offset_y) src_y_minor_y += 16;

	if (window->interlaced_y)
		reg_2918 = (window->dst_h << 16) | (window->src_h + src_y_minor_y);
	else
		reg_2918 = (window->dst_h << 16) | ((window->src_h + src_y_minor_y) << 1);

	if (window->interlaced_uv)
		reg_291c = (window->dst_h << 16) | ((window->src_h + src_y_minor_uv) >> 1);
	else
		reg_291c = (window->dst_h << 16) | (window->src_h + src_y_minor_uv);

	reg_2964_base = (src_y_minor_y * ((window->dst_h << 16)/window->src_h)) >> 14;
	reg_2968_base = (src_y_minor_uv * ((window->dst_h << 16)/window->src_h)) >> 14;

	if (window->dst_h / 2 >= window->src_h && !window->interlaced_y) {
		master_height = (window->src_h * 0x00400000) / window->dst_h;
		if ((window->src_h * 0x00400000) - (master_height * window->dst_h) >= window->dst_h / 2) master_height ++;
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 3;
		reg_2930 = master_height;
		reg_2940 = master_height >> 1;
		reg_2964_base >>= 3;
		reg_2968_base >>= 3;
		reg_296c = 0x00000000;
	}
	else if (window->dst_h >= window->src_h) {
		master_height = (window->src_h * 0x00400000) / window->dst_h;
		master_height = (master_height >> 1) + (master_height & 1);
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 2;
		reg_2930 = master_height;
		reg_2940 = master_height >> 1;
		reg_296c = 0x00000000;
		if (window->interlaced_y) {
			reg_2964_base >>= 3;
		}
		else {
			reg_296c ++;
			reg_2964_base >>= 2;
		}
		if (window->interlaced_uv) reg_2928 >>= 1;
		reg_2968_base >>= 3;
	}
	else if (window->dst_h >= window->src_h / 2) {
		master_height = (window->src_h * 0x00200000) / window->dst_h;
		master_height = (master_height >> 1) + (master_height & 1);
		reg_2920 = master_height >> 2;
		reg_2928 = master_height >> 2;
		reg_2930 = master_height;
		reg_2940 = master_height;
		reg_296c = 0x00000101;
		if (window->interlaced_y) {
			reg_2964_base >>= 2;
		}
		else {
			reg_296c ++;
			reg_2964_base >>= 1;
		}
		if (window->interlaced_uv) reg_2928 >>= 1;
		reg_2968_base >>= 2;
	}
	else {
		master_height = (window->src_h * 0x00100000) / window->dst_h;
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
	if (window->src_h == window->dst_h){
		reg_2934 = 0x00020000;
		reg_293c = 0x00100000;
		reg_2944 = 0x00040000;
		reg_294c = 0x000b0000;
	}
	else {
		reg_2934 = 0x00000FF0;
		reg_293c = 0x00000FF0;
		reg_2944 = 0x00000FF0;
		reg_294c = 0x00000FF0;
	}

	/* The first line to be displayed */
	reg_2950 = 0x00010000 + src_y_major_y;
	if (window->interlaced_y) reg_2950 += 0x00010000;
	reg_2954 = reg_2950 + 1;

	reg_2958 = 0x00010000 + (src_y_major_y >> 1);
	if (window->interlaced_uv) reg_2958 += 0x00010000;
	reg_295c = reg_2958 + 1;

	if (itv->yuv_info.decode_height == 480)
		reg_289c = 0x011e0017;
	else
		reg_289c = 0x01500017;

	if (window->dst_y < 0)
		reg_289c = (reg_289c - ((window->dst_y & ~1)<<15))-(window->dst_y >>1);
	else
		reg_289c = (reg_289c + ((window->dst_y & ~1)<<15))+(window->dst_y >>1);

	/* How much of the source to decode.
	   Take into account the source offset */
	reg_2960 = ((src_y_minor_y + window->src_h + src_y_major_y) - 1 ) |
			((((src_y_minor_uv + window->src_h + src_y_major_uv) - 1) & ~1) << 15);

	/* Calculate correct value for register 2964 */
	if (window->src_h == window->dst_h)
		reg_2964 = 1;
	else {
		reg_2964 = 2 + ((window->dst_h << 1) / window->src_h);
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
	if ((reg_2964 != 0x00010001) && (window->dst_h / 2 <= window->src_h))
		reg_2964 = (reg_2964 & 0xFFFF0000) + ((reg_2964 & 0x0000FFFF)/2);

	if (!window->interlaced_y) reg_2964 -= 0x00010001;
	if (!window->interlaced_uv) reg_2968 -= 0x00010001;

	reg_2964 += ((reg_2964_base << 16) | reg_2964_base);
	reg_2968 += ((reg_2968_base << 16) | reg_2968_base);

	/* Select the vertical filter */
	if (window->src_h == window->dst_h) {
		/* An exact size match uses filter 0/1 */
		v_filter_1 = 0;
		v_filter_2 = 1;
	}
	else {
		/* Figure out which filter to use */
		v_filter_1 = ((window->src_h << 16) / window->dst_h) >> 15;
		v_filter_1 = (v_filter_1 >> 1) + (v_filter_1 & 1);
		/* Only an exact size match can use filter 0 */
		if (v_filter_1 == 0) v_filter_1 = 1;
		v_filter_2 = v_filter_1;
	}

	write_reg(reg_2934, 0x02934);
	write_reg(reg_293c, 0x0293c);
	IVTV_DEBUG_YUV("Update reg 0x2934 %08x->%08x 0x293c %08x->%08x\n",itv->yuv_info.reg_2934, reg_2934, itv->yuv_info.reg_293c, reg_293c);
	write_reg(reg_2944, 0x02944);
	write_reg(reg_294c, 0x0294c);
	IVTV_DEBUG_YUV("Update reg 0x2944 %08x->%08x 0x294c %08x->%08x\n",itv->yuv_info.reg_2944, reg_2944, itv->yuv_info.reg_294c, reg_294c);

	/* Ensure 2970 is 0 (does it ever change ?) */
/*	write_reg(0,0x02970); */
/*	IVTV_DEBUG_YUV("Update reg 0x2970 %08x->%08x\n",itv->yuv_info.reg_2970, 0); */

	write_reg(reg_2930, 0x02938);
	write_reg(reg_2930, 0x02930);
	IVTV_DEBUG_YUV("Update reg 0x2930 %08x->%08x 0x2938 %08x->%08x\n",itv->yuv_info.reg_2930, reg_2930, itv->yuv_info.reg_2938, reg_2930);

	write_reg(reg_2928, 0x02928);
	write_reg(reg_2928+0x514, 0x0292C);
	IVTV_DEBUG_YUV("Update reg 0x2928 %08x->%08x 0x292c %08x->%08x\n",itv->yuv_info.reg_2928, reg_2928, itv->yuv_info.reg_292c, reg_2928+0x514);

	write_reg(reg_2920, 0x02920);
	write_reg(reg_2920+0x514, 0x02924);
	IVTV_DEBUG_YUV("Update reg 0x2920 %08x->%08x 0x2924 %08x->%08x\n",itv->yuv_info.reg_2920, reg_2920, itv->yuv_info.reg_2924, 0x514+reg_2920);

	write_reg (reg_2918,0x02918);
	write_reg (reg_291c,0x0291C);
	IVTV_DEBUG_YUV("Update reg 0x2918 %08x->%08x 0x291C %08x->%08x\n",itv->yuv_info.reg_2918,reg_2918,itv->yuv_info.reg_291c,reg_291c);

	write_reg(reg_296c, 0x0296c);
	IVTV_DEBUG_YUV("Update reg 0x296c %08x->%08x\n",itv->yuv_info.reg_296c, reg_296c);

	write_reg(reg_2940, 0x02948);
	write_reg(reg_2940, 0x02940);
	IVTV_DEBUG_YUV("Update reg 0x2940 %08x->%08x 0x2948 %08x->%08x\n",itv->yuv_info.reg_2940, reg_2940, itv->yuv_info.reg_2948, reg_2940);

	write_reg(reg_2950, 0x02950);
	write_reg(reg_2954, 0x02954);
	IVTV_DEBUG_YUV("Update reg 0x2950 %08x->%08x 0x2954 %08x->%08x\n",itv->yuv_info.reg_2950, reg_2950, itv->yuv_info.reg_2954, reg_2954);

	write_reg(reg_2958, 0x02958);
	write_reg(reg_295c, 0x0295C);
	IVTV_DEBUG_YUV("Update reg 0x2958 %08x->%08x 0x295C %08x->%08x\n",itv->yuv_info.reg_2958, reg_2958, itv->yuv_info.reg_295c, reg_295c);

	write_reg(reg_2960, 0x02960);
	IVTV_DEBUG_YUV("Update reg 0x2960 %08x->%08x \n",itv->yuv_info.reg_2960, reg_2960);

	write_reg(reg_2964, 0x02964);
	write_reg(reg_2968, 0x02968);
	IVTV_DEBUG_YUV("Update reg 0x2964 %08x->%08x 0x2968 %08x->%08x\n",itv->yuv_info.reg_2964, reg_2964, itv->yuv_info.reg_2968, reg_2968);

	write_reg( reg_289c,0x0289c);
	IVTV_DEBUG_YUV("Update reg 0x289c %08x->%08x\n",itv->yuv_info.reg_289c, reg_289c);

	/* Only update filter 1 if we really need to */
	if (v_filter_1 != itv->yuv_info.v_filter_1) {
		ivtv_yuv_filter (itv,-1,v_filter_1,-1);
		itv->yuv_info.v_filter_1 = v_filter_1;
	}

	/* Only update filter 2 if we really need to */
	if (v_filter_2 != itv->yuv_info.v_filter_2) {
		ivtv_yuv_filter (itv,-1,-1,v_filter_2);
		itv->yuv_info.v_filter_2 = v_filter_2;
	}

}

/* Modify the supplied coordinate information to fit the visible osd area */
static u32 ivtv_yuv_window_setup (struct ivtv *itv, struct yuv_frame_info *window)
{
	int osd_crop, lace_threshold;
	u32 osd_scale;
	u32 yuv_update = 0;

	lace_threshold = itv->yuv_info.lace_threshold;
	if (lace_threshold < 0)
		lace_threshold = itv->yuv_info.decode_height - 1;

	/* Work out the lace settings */
	switch (itv->yuv_info.lace_mode) {
		case IVTV_YUV_MODE_PROGRESSIVE: /* Progressive mode */
			itv->yuv_info.frame_interlaced = 0;
			if (window->tru_h < 512 || (window->tru_h > 576 && window->tru_h < 1021))
				window->interlaced_y = 0;
			else
				window->interlaced_y = 1;

			if (window->tru_h < 1021 && (window->dst_h >= window->src_h /2))
				window->interlaced_uv = 0;
			else
				window->interlaced_uv = 1;
			break;

		case IVTV_YUV_MODE_AUTO:
			if (window->tru_h <= lace_threshold || window->tru_h > 576 || window->tru_w > 720){
				itv->yuv_info.frame_interlaced = 0;
				if ((window->tru_h < 512) ||
				  (window->tru_h > 576 && window->tru_h < 1021) ||
				  (window->tru_w > 720 && window->tru_h < 1021))
					window->interlaced_y = 0;
				else
					window->interlaced_y = 1;

				if (window->tru_h < 1021 && (window->dst_h >= window->src_h /2))
					window->interlaced_uv = 0;
				else
					window->interlaced_uv = 1;
			}
			else {
				itv->yuv_info.frame_interlaced = 1;
				window->interlaced_y = 1;
				window->interlaced_uv = 1;
			}
			break;

			case IVTV_YUV_MODE_INTERLACED: /* Interlace mode */
		default:
			itv->yuv_info.frame_interlaced = 1;
			window->interlaced_y = 1;
			window->interlaced_uv = 1;
			break;
	}

	/* Sorry, but no negative coords for src */
	if (window->src_x < 0) window->src_x = 0;
	if (window->src_y < 0) window->src_y = 0;

	/* Can only reduce width down to 1/4 original size */
	if ((osd_crop = window->src_w - ( 4 * window->dst_w )) > 0) {
		window->src_x += osd_crop / 2;
		window->src_w = (window->src_w - osd_crop) & ~3;
		window->dst_w = window->src_w / 4;
		window->dst_w += window->dst_w & 1;
	}

	/* Can only reduce height down to 1/4 original size */
	if (window->src_h / window->dst_h >= 2) {
		/* Overflow may be because we're running progressive, so force mode switch */
		window->interlaced_y = 1;
		/* Make sure we're still within limits for interlace */
		if ((osd_crop = window->src_h - ( 4 * window->dst_h )) > 0) {
			/* If we reach here we'll have to force the height. */
			window->src_y += osd_crop / 2;
			window->src_h = (window->src_h - osd_crop) & ~3;
			window->dst_h = window->src_h / 4;
			window->dst_h += window->dst_h & 1;
		}
	}

	/* If there's nothing to safe to display, we may as well stop now */
	if ((int)window->dst_w <= 2 || (int)window->dst_h <= 2 || (int)window->src_w <= 2 || (int)window->src_h <= 2) {
		return 0;
	}

	/* Ensure video remains inside OSD area */
	osd_scale = (window->src_h << 16) / window->dst_h;

	if ((osd_crop = window->pan_y - window->dst_y) > 0) {
		/* Falls off the upper edge - crop */
		window->src_y += (osd_scale * osd_crop) >> 16;
		window->src_h -= (osd_scale * osd_crop) >> 16;
		window->dst_h -= osd_crop;
		window->dst_y = 0;
	}
	else {
		window->dst_y -= window->pan_y;
	}

	if ((osd_crop = window->dst_h + window->dst_y - window->vis_h) > 0) {
		/* Falls off the lower edge - crop */
		window->dst_h -= osd_crop;
		window->src_h -= (osd_scale * osd_crop) >> 16;
	}

	osd_scale = (window->src_w << 16) / window->dst_w;

	if ((osd_crop = window->pan_x - window->dst_x) > 0) {
		/* Fall off the left edge - crop */
		window->src_x += (osd_scale * osd_crop) >> 16;
		window->src_w -= (osd_scale * osd_crop) >> 16;
		window->dst_w -= osd_crop;
		window->dst_x = 0;
	}
	else {
		window->dst_x -= window->pan_x;
	}

	if ((osd_crop = window->dst_w + window->dst_x - window->vis_w) > 0) {
		/* Falls off the right edge - crop */
		window->dst_w -= osd_crop;
		window->src_w -= (osd_scale * osd_crop) >> 16;
	}

	/* The OSD can be moved. Track to it */
	window->dst_x += itv->yuv_info.osd_x_offset;
	window->dst_y += itv->yuv_info.osd_y_offset;

	/* Width & height for both src & dst must be even.
	   Same for coordinates. */
	window->dst_w &= ~1;
	window->dst_x &= ~1;

	window->src_w += window->src_x & 1;
	window->src_x &= ~1;

	window->src_w &= ~1;
	window->dst_w &= ~1;

	window->dst_h &= ~1;
	window->dst_y &= ~1;

	window->src_h += window->src_y & 1;
	window->src_y &= ~1;

	window->src_h &= ~1;
	window->dst_h &= ~1;

	/* Due to rounding, we may have reduced the output size to <1/4 of the source
	   Check again, but this time just resize. Don't change source coordinates */
	if (window->dst_w < window->src_w / 4) {
		window->src_w &= ~3;
		window->dst_w = window->src_w / 4;
		window->dst_w += window->dst_w & 1;
	}
	if (window->dst_h < window->src_h / 4) {
		window->src_h &= ~3;
		window->dst_h = window->src_h / 4;
		window->dst_h += window->dst_h & 1;
	}

	/* Check again. If there's nothing to safe to display, stop now */
	if ((int)window->dst_w <= 2 || (int)window->dst_h <= 2 || (int)window->src_w <= 2 || (int)window->src_h <= 2) {
		return 0;
	}

	/* Both x offset & width are linked, so they have to be done together */
	if ((itv->yuv_info.old_frame_info.dst_w != window->dst_w) ||
	    (itv->yuv_info.old_frame_info.src_w != window->src_w) ||
	    (itv->yuv_info.old_frame_info.dst_x != window->dst_x) ||
	    (itv->yuv_info.old_frame_info.src_x != window->src_x) ||
	    (itv->yuv_info.old_frame_info.pan_x != window->pan_x) ||
	    (itv->yuv_info.old_frame_info.vis_w != window->vis_w)) {
		yuv_update |= IVTV_YUV_UPDATE_HORIZONTAL;
	}

	if ((itv->yuv_info.old_frame_info.src_h != window->src_h) ||
	    (itv->yuv_info.old_frame_info.dst_h != window->dst_h) ||
	    (itv->yuv_info.old_frame_info.dst_y != window->dst_y) ||
	    (itv->yuv_info.old_frame_info.src_y != window->src_y) ||
	    (itv->yuv_info.old_frame_info.pan_y != window->pan_y) ||
	    (itv->yuv_info.old_frame_info.vis_h != window->vis_h) ||
	    (itv->yuv_info.old_frame_info.lace_mode != window->lace_mode) ||
	    (itv->yuv_info.old_frame_info.interlaced_y != window->interlaced_y) ||
	    (itv->yuv_info.old_frame_info.interlaced_uv != window->interlaced_uv)) {
		yuv_update |= IVTV_YUV_UPDATE_VERTICAL;
	}

	return yuv_update;
}

/* Update the scaling register to the requested value */
void ivtv_yuv_work_handler (struct ivtv *itv)
{
	struct yuv_frame_info window;
	u32 yuv_update;

	int frame = itv->yuv_info.update_frame;

/*	IVTV_DEBUG_YUV("Update yuv registers for frame %d\n",frame); */
	memcpy(&window, &itv->yuv_info.new_frame_info[frame], sizeof (window));

	/* Update the osd pan info */
	window.pan_x = itv->yuv_info.osd_x_pan;
	window.pan_y = itv->yuv_info.osd_y_pan;
	window.vis_w = itv->yuv_info.osd_vis_w;
	window.vis_h = itv->yuv_info.osd_vis_h;

	/* Calculate the display window coordinates. Exit if nothing left */
	if (!(yuv_update = ivtv_yuv_window_setup (itv, &window)))
		return;

	/* Update horizontal settings */
	if (yuv_update & IVTV_YUV_UPDATE_HORIZONTAL)
		ivtv_yuv_handle_horizontal(itv, &window);

	if (yuv_update & IVTV_YUV_UPDATE_VERTICAL)
		ivtv_yuv_handle_vertical(itv, &window);

	memcpy(&itv->yuv_info.old_frame_info, &window, sizeof (itv->yuv_info.old_frame_info));
}

static void ivtv_yuv_init (struct ivtv *itv)
{
	IVTV_DEBUG_YUV("ivtv_yuv_init\n");

	/* Take a snapshot of the current register settings */
	itv->yuv_info.reg_2834 = read_reg(0x02834);
	itv->yuv_info.reg_2838 = read_reg(0x02838);
	itv->yuv_info.reg_283c = read_reg(0x0283c);
	itv->yuv_info.reg_2840 = read_reg(0x02840);
	itv->yuv_info.reg_2844 = read_reg(0x02844);
	itv->yuv_info.reg_2848 = read_reg(0x02848);
	itv->yuv_info.reg_2854 = read_reg(0x02854);
	itv->yuv_info.reg_285c = read_reg(0x0285c);
	itv->yuv_info.reg_2864 = read_reg(0x02864);
	itv->yuv_info.reg_2870 = read_reg(0x02870);
	itv->yuv_info.reg_2874 = read_reg(0x02874);
	itv->yuv_info.reg_2898 = read_reg(0x02898);
	itv->yuv_info.reg_2890 = read_reg(0x02890);

	itv->yuv_info.reg_289c = read_reg(0x0289c);
	itv->yuv_info.reg_2918 = read_reg(0x02918);
	itv->yuv_info.reg_291c = read_reg(0x0291c);
	itv->yuv_info.reg_2920 = read_reg(0x02920);
	itv->yuv_info.reg_2924 = read_reg(0x02924);
	itv->yuv_info.reg_2928 = read_reg(0x02928);
	itv->yuv_info.reg_292c = read_reg(0x0292c);
	itv->yuv_info.reg_2930 = read_reg(0x02930);
	itv->yuv_info.reg_2934 = read_reg(0x02934);
	itv->yuv_info.reg_2938 = read_reg(0x02938);
	itv->yuv_info.reg_293c = read_reg(0x0293c);
	itv->yuv_info.reg_2940 = read_reg(0x02940);
	itv->yuv_info.reg_2944 = read_reg(0x02944);
	itv->yuv_info.reg_2948 = read_reg(0x02948);
	itv->yuv_info.reg_294c = read_reg(0x0294c);
	itv->yuv_info.reg_2950 = read_reg(0x02950);
	itv->yuv_info.reg_2954 = read_reg(0x02954);
	itv->yuv_info.reg_2958 = read_reg(0x02958);
	itv->yuv_info.reg_295c = read_reg(0x0295c);
	itv->yuv_info.reg_2960 = read_reg(0x02960);
	itv->yuv_info.reg_2964 = read_reg(0x02964);
	itv->yuv_info.reg_2968 = read_reg(0x02968);
	itv->yuv_info.reg_296c = read_reg(0x0296c);
	itv->yuv_info.reg_2970 = read_reg(0x02970);

	itv->yuv_info.v_filter_1 = -1;
	itv->yuv_info.v_filter_2 = -1;
	itv->yuv_info.h_filter = -1;

	/* Set some valid size info */
	itv->yuv_info.osd_x_offset = read_reg(0x02a04) & 0x00000FFF;
	itv->yuv_info.osd_y_offset = (read_reg(0x02a04) >> 16) & 0x00000FFF;

	/* Bit 2 of reg 2878 indicates current decoder output format
	   0 : NTSC    1 : PAL */
	if (read_reg(0x2878) & 4)
		itv->yuv_info.decode_height = 576;
	else
		itv->yuv_info.decode_height = 480;

	/* If no visible size set, assume full size */
	if (!itv->yuv_info.osd_vis_w)
		itv->yuv_info.osd_vis_w = 720 - itv->yuv_info.osd_x_offset;

	if (!itv->yuv_info.osd_vis_h) {
		itv->yuv_info.osd_vis_h = itv->yuv_info.decode_height - itv->yuv_info.osd_y_offset;
	} else {
		/* If output video standard has changed, requested height may
		not be legal */
		if (itv->yuv_info.osd_vis_h + itv->yuv_info.osd_y_offset > itv->yuv_info.decode_height) {
			IVTV_DEBUG_WARN("Clipping yuv output - fb size (%d) exceeds video standard limit (%d)\n",
					itv->yuv_info.osd_vis_h + itv->yuv_info.osd_y_offset,
					itv->yuv_info.decode_height);
			itv->yuv_info.osd_vis_h = itv->yuv_info.decode_height - itv->yuv_info.osd_y_offset;
		}
	}

	/* We need a buffer for blanking when Y plane is offset - non-fatal if we can't get one */
	itv->yuv_info.blanking_ptr = kzalloc(720*16,GFP_KERNEL);
	if (itv->yuv_info.blanking_ptr) {
		itv->yuv_info.blanking_dmaptr = pci_map_single(itv->dev, itv->yuv_info.blanking_ptr, 720*16, PCI_DMA_TODEVICE);
	}
	else {
		itv->yuv_info.blanking_dmaptr = 0;
		IVTV_DEBUG_WARN ("Failed to allocate yuv blanking buffer\n");
	}

	IVTV_DEBUG_WARN("Enable video output\n");
	write_reg_sync(0x00108080, 0x2898);

	/* Enable YUV decoder output */
	write_reg_sync(0x01, IVTV_REG_VDM);

	set_bit(IVTV_F_I_DECODING_YUV, &itv->i_flags);
	atomic_set(&itv->yuv_info.next_dma_frame,0);
}

int ivtv_yuv_prep_frame(struct ivtv *itv, struct ivtv_dma_frame *args)
{
	DEFINE_WAIT(wait);
	int rc = 0;
	int got_sig = 0;
	int frame, next_fill_frame, last_fill_frame;
	int register_update = 0;

	IVTV_DEBUG_INFO("yuv_prep_frame\n");

	if (atomic_read(&itv->yuv_info.next_dma_frame) == -1) ivtv_yuv_init(itv);

	frame = atomic_read(&itv->yuv_info.next_fill_frame);
	next_fill_frame = (frame + 1) & 0x3;
	last_fill_frame = (atomic_read(&itv->yuv_info.next_dma_frame)+1) & 0x3;

	if (next_fill_frame != last_fill_frame && last_fill_frame != frame) {
		/* Buffers are full - Overwrite the last frame */
		next_fill_frame = frame;
		frame = (frame - 1) & 3;
		register_update = itv->yuv_info.new_frame_info[frame].update;
	}

	/* Take a snapshot of the yuv coordinate information */
	itv->yuv_info.new_frame_info[frame].src_x = args->src.left;
	itv->yuv_info.new_frame_info[frame].src_y = args->src.top;
	itv->yuv_info.new_frame_info[frame].src_w = args->src.width;
	itv->yuv_info.new_frame_info[frame].src_h = args->src.height;
	itv->yuv_info.new_frame_info[frame].dst_x = args->dst.left;
	itv->yuv_info.new_frame_info[frame].dst_y = args->dst.top;
	itv->yuv_info.new_frame_info[frame].dst_w = args->dst.width;
	itv->yuv_info.new_frame_info[frame].dst_h = args->dst.height;
	itv->yuv_info.new_frame_info[frame].tru_x = args->dst.left;
	itv->yuv_info.new_frame_info[frame].tru_w = args->src_width;
	itv->yuv_info.new_frame_info[frame].tru_h = args->src_height;

	/* Snapshot field order */
	itv->yuv_info.sync_field[frame] = itv->yuv_info.lace_sync_field;

	/* Are we going to offset the Y plane */
	if (args->src.height + args->src.top < 512-16)
		itv->yuv_info.new_frame_info[frame].offset_y = 1;
	else
		itv->yuv_info.new_frame_info[frame].offset_y = 0;

	/* Snapshot the osd pan info */
	itv->yuv_info.new_frame_info[frame].pan_x = itv->yuv_info.osd_x_pan;
	itv->yuv_info.new_frame_info[frame].pan_y = itv->yuv_info.osd_y_pan;
	itv->yuv_info.new_frame_info[frame].vis_w = itv->yuv_info.osd_vis_w;
	itv->yuv_info.new_frame_info[frame].vis_h = itv->yuv_info.osd_vis_h;

	itv->yuv_info.new_frame_info[frame].update = 0;
	itv->yuv_info.new_frame_info[frame].interlaced_y = 0;
	itv->yuv_info.new_frame_info[frame].interlaced_uv = 0;
	itv->yuv_info.new_frame_info[frame].lace_mode = itv->yuv_info.lace_mode;

	if (memcmp (&itv->yuv_info.old_frame_info_args, &itv->yuv_info.new_frame_info[frame],
	    sizeof (itv->yuv_info.new_frame_info[frame]))) {
		memcpy(&itv->yuv_info.old_frame_info_args, &itv->yuv_info.new_frame_info[frame], sizeof (itv->yuv_info.old_frame_info_args));
		itv->yuv_info.new_frame_info[frame].update = 1;
/*		IVTV_DEBUG_YUV ("Requesting register update for frame %d\n",frame); */
	}

	itv->yuv_info.new_frame_info[frame].update |= register_update;

	/* Should this frame be delayed ? */
	if (itv->yuv_info.sync_field[frame] != itv->yuv_info.sync_field[(frame - 1) & 3])
		itv->yuv_info.field_delay[frame] = 1;
	else
		itv->yuv_info.field_delay[frame] = 0;

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
	while (itv->i_flags & (IVTV_F_I_UDMA_PENDING | IVTV_F_I_UDMA)) {
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

	atomic_set(&itv->yuv_info.next_fill_frame, next_fill_frame);

	mutex_unlock(&itv->udma.lock);
	return rc;
}

void ivtv_yuv_close(struct ivtv *itv)
{
	int h_filter, v_filter_1, v_filter_2;

	IVTV_DEBUG_YUV("ivtv_yuv_close\n");
	ivtv_waitq(&itv->vsync_waitq);

	atomic_set(&itv->yuv_info.next_dma_frame, -1);
	atomic_set(&itv->yuv_info.next_fill_frame, 0);

	/* Reset registers we have changed so mpeg playback works */

	/* If we fully restore this register, the display may remain active.
	   Restore, but set one bit to blank the video. Firmware will always
	   clear this bit when needed, so not a problem. */
	write_reg(itv->yuv_info.reg_2898 | 0x01000000, 0x2898);

	write_reg(itv->yuv_info.reg_2834, 0x02834);
	write_reg(itv->yuv_info.reg_2838, 0x02838);
	write_reg(itv->yuv_info.reg_283c, 0x0283c);
	write_reg(itv->yuv_info.reg_2840, 0x02840);
	write_reg(itv->yuv_info.reg_2844, 0x02844);
	write_reg(itv->yuv_info.reg_2848, 0x02848);
	write_reg(itv->yuv_info.reg_2854, 0x02854);
	write_reg(itv->yuv_info.reg_285c, 0x0285c);
	write_reg(itv->yuv_info.reg_2864, 0x02864);
	write_reg(itv->yuv_info.reg_2870, 0x02870);
	write_reg(itv->yuv_info.reg_2874, 0x02874);
	write_reg(itv->yuv_info.reg_2890, 0x02890);
	write_reg(itv->yuv_info.reg_289c, 0x0289c);

	write_reg(itv->yuv_info.reg_2918, 0x02918);
	write_reg(itv->yuv_info.reg_291c, 0x0291c);
	write_reg(itv->yuv_info.reg_2920, 0x02920);
	write_reg(itv->yuv_info.reg_2924, 0x02924);
	write_reg(itv->yuv_info.reg_2928, 0x02928);
	write_reg(itv->yuv_info.reg_292c, 0x0292c);
	write_reg(itv->yuv_info.reg_2930, 0x02930);
	write_reg(itv->yuv_info.reg_2934, 0x02934);
	write_reg(itv->yuv_info.reg_2938, 0x02938);
	write_reg(itv->yuv_info.reg_293c, 0x0293c);
	write_reg(itv->yuv_info.reg_2940, 0x02940);
	write_reg(itv->yuv_info.reg_2944, 0x02944);
	write_reg(itv->yuv_info.reg_2948, 0x02948);
	write_reg(itv->yuv_info.reg_294c, 0x0294c);
	write_reg(itv->yuv_info.reg_2950, 0x02950);
	write_reg(itv->yuv_info.reg_2954, 0x02954);
	write_reg(itv->yuv_info.reg_2958, 0x02958);
	write_reg(itv->yuv_info.reg_295c, 0x0295c);
	write_reg(itv->yuv_info.reg_2960, 0x02960);
	write_reg(itv->yuv_info.reg_2964, 0x02964);
	write_reg(itv->yuv_info.reg_2968, 0x02968);
	write_reg(itv->yuv_info.reg_296c, 0x0296c);
	write_reg(itv->yuv_info.reg_2970, 0x02970);

	/* Prepare to restore filters */

	/* First the horizontal filter */
	if ((itv->yuv_info.reg_2834 & 0x0000FFFF) == (itv->yuv_info.reg_2834 >> 16)) {
		/* An exact size match uses filter 0 */
		h_filter = 0;
	}
	else {
		/* Figure out which filter to use */
		h_filter = ((itv->yuv_info.reg_2834 << 16) / (itv->yuv_info.reg_2834 >> 16)) >> 15;
		h_filter = (h_filter >> 1) + (h_filter & 1);
		/* Only an exact size match can use filter 0. */
		if (h_filter < 1) h_filter = 1;
	}

	/* Now the vertical filter */
	if ((itv->yuv_info.reg_2918 & 0x0000FFFF) == (itv->yuv_info.reg_2918 >> 16)) {
		/* An exact size match uses filter 0/1 */
		v_filter_1 = 0;
		v_filter_2 = 1;
	}
	else {
		/* Figure out which filter to use */
		v_filter_1 = ((itv->yuv_info.reg_2918 << 16) / (itv->yuv_info.reg_2918 >> 16)) >> 15;
		v_filter_1 = (v_filter_1 >> 1) + (v_filter_1 & 1);
		/* Only an exact size match can use filter 0 */
		if (v_filter_1 == 0) v_filter_1 = 1;
		v_filter_2 = v_filter_1;
	}

	/* Now restore the filters */
	ivtv_yuv_filter (itv,h_filter,v_filter_1,v_filter_2);

	/* and clear a few registers */
	write_reg(0, 0x02814);
	write_reg(0, 0x0282c);
	write_reg(0, 0x02904);
	write_reg(0, 0x02910);

	/* Release the blanking buffer */
	if (itv->yuv_info.blanking_ptr) {
		kfree (itv->yuv_info.blanking_ptr);
		itv->yuv_info.blanking_ptr = NULL;
		pci_unmap_single(itv->dev, itv->yuv_info.blanking_dmaptr, 720*16, PCI_DMA_TODEVICE);
	}

	/* Invalidate the old dimension information */
	itv->yuv_info.old_frame_info.src_w = 0;
	itv->yuv_info.old_frame_info.src_h = 0;
	itv->yuv_info.old_frame_info_args.src_w = 0;
	itv->yuv_info.old_frame_info_args.src_h = 0;

	/* All done. */
	clear_bit(IVTV_F_I_DECODING_YUV, &itv->i_flags);
}

