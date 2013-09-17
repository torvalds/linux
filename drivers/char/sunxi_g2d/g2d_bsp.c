/* g2d_bsp.c
 *
 * Copyright (c)	2011 xxxx Electronics
 *					2011 Yupu Tang
 *
 * @ F23 G2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

#include "g2d_regs.h"
#include "g2d_bsp.h"
#include "g2d.h"

static	__u32	base_addr;

#define read_bvalue(offset)			get_bvalue(base_addr + offset)			/* byte input */
#define write_bvalue(offset,value)	put_bvalue(base_addr + offset,value)	/* byte output */
#define read_hvalue(offset)			get_hvalue(base_addr + offset)			/* half word input */
#define write_hvalue(offset,value)	put_hvalue(base_addr + offset,value)	/* half word output */
#define read_wvalue(offset)			get_wvalue(base_addr + offset)			/* word input */
#define write_wvalue(offset,value)	put_wvalue(base_addr + offset,value)	/* word output */

__s32 csc0coeff[12]=
{
	0x4a7,0x1e6f,0x1cbf,0x877,
	0x4a7,0x00,  0x662,	0x3211,
	0x4a7,0x812, 0x00,	0x2eb1
};
__s32 csc1coeff[12]=
{
	0x4a7,0x1e6f,0x1cbf,0x877,
	0x4a7,0x00,  0x662,	0x3211,
	0x4a7,0x812, 0x00,	0x2eb1
};

__s32 csc2coeff[12]=
{
	0x204,0x107,0x64,0x100,		/* YG,YR,YB,YC */
	0x1ED6,0x1F69,0x1C1,0x800,	/* UG,UR,UB,UC */
	0x1E87,0x1C1,0x1FB8,0x800,	/* VG,VR,VB,VC */
};

__s32 csc2coeff_VUVU[12]=
{
	0x204,0x107,0x64,0x100,		/* YG,YR,YB,YC */
	0x1E87,0x1C1,0x1FB8,0x800,	/* VG,VR,VB,VC */
	0x1ED6,0x1F69,0x1C1,0x800,	/* UG,UR,UB,UC */
};

__s32 scalercoeff[64]=
{
	/* Horizontal Filtering Coefficient(0x200-0x27c) */
	0x00004000,0x000140ff,0x00033ffe,0x00043ffd,0x00063efc,0xff083dfc,0x000a3bfb,0xff0d39fb,
	0xff0f37fb,0xff1136fa,0xfe1433fb,0xfe1631fb,0xfd192ffb,0xfd1c2cfb,0xfd1f29fb,0xfc2127fc,
	0xfc2424fc,0xfc2721fc,0xfb291ffd,0xfb2c1cfd,0xfb2f19fd,0xfb3116fe,0xfb3314fe,0xfa3611ff,
	0xfb370fff,0xfb390dff,0xfb3b0a00,0xfc3d08ff,0xfc3e0600,0xfd3f0400,0xfe3f0300,0xff400100,

	/* Vertical Filtering Coefficient(0x280-0x2fc) */
	0x00004000,0x000140ff,0x00033ffe,0x00043ffd,0x00063efc,0xff083dfc,0x000a3bfb,0xff0d39fb,
	0xff0f37fb,0xff1136fa,0xfe1433fb,0xfe1631fb,0xfd192ffb,0xfd1c2cfb,0xfd1f29fb,0xfc2127fc,
	0xfc2424fc,0xfc2721fc,0xfb291ffd,0xfb2c1cfd,0xfb2f19fd,0xfb3116fe,0xfb3314fe,0xfa3611ff,
	0xfb370fff,0xfb390dff,0xfb3b0a00,0xfc3d08ff,0xfc3e0600,0xfd3f0400,0xfe3f0300,0xff400100,
};

/* Set the Color Space Converter Coefficient Parameter */
void csc_coeff_set(void){
	__u32 i,j;

	/* 0x180-0x1ac */
	for(i=0, j=0; i<12; i++,j+=4)
		write_wvalue(G2D_CSC01_ADDR_REG + j, (((csc1coeff[i]&0xFFFF)<<16) | (csc0coeff[i]&0xFFFF)));

	/* 0x1c0-0x1ec */
	for(i=0, j=0; i<12; i++,j+=4)
		write_wvalue(G2D_CSC2_ADDR_REG + j, csc2coeff[i]&0xFFFF);

}

/* Set the Scaling Horizontal/Vertical Filtering Coefficient Parameter */
void scaler_coeff_set(void){
	__u32 i,j;

	/* 0x200-0x2fc */
	for(i=0, j=0; i<64; i++,j+=4)
		write_wvalue(G2D_SCALER_HFILTER_REG + j, scalercoeff[i]);

}

__u32 mixer_set_reg_base(__u32 addr){
	base_addr = addr;
	return 0;
}

/* clear most of the registers value to default */
__u32 mixer_reg_init(void){
	__u32 i;

    for(i=0;i<=0x120;i+=4)
    	write_wvalue(i, 0);

    /* initial the color space converter parameter */
    csc_coeff_set();

    /* initial the scaler coefficient parameter */
    scaler_coeff_set();

	return 0;
}

__u32 mixer_set_fillcolor(__u32 color, __u32 sel){
	__u32 value;

	if(sel == 1){
		value = read_wvalue(G2D_DMA1_CONTROL_REG) | G2D_FILL_ENABLE;
		write_wvalue(G2D_DMA1_CONTROL_REG, value);
		write_wvalue(G2D_DMA1_FILLCOLOR_REG, color);
		}
	else if(sel == 2){
		value = read_wvalue(G2D_DMA2_CONTROL_REG) | G2D_FILL_ENABLE;
		write_wvalue(G2D_DMA2_CONTROL_REG, value);
		write_wvalue(G2D_DMA2_FILLCOLOR_REG, color);
		}
	else if(sel == 3){
		value = read_wvalue(G2D_DMA3_CONTROL_REG) | G2D_FILL_ENABLE;
		write_wvalue(G2D_DMA3_CONTROL_REG, value);
		write_wvalue(G2D_DMA3_FILLCOLOR_REG, color);
		}
	else	{
		value = read_wvalue(G2D_DMA0_CONTROL_REG) | G2D_FILL_ENABLE;
		write_wvalue(G2D_DMA0_CONTROL_REG, value);
		write_wvalue(G2D_DMA0_FILLCOLOR_REG, color);
		}

	return 0;
}

__u32 mixer_bpp_count(__u32 format)
{
	__u32 bpp = 32;
	switch (format)
	{
		case G2D_FMT_1BPP_MONO:
		case G2D_FMT_1BPP_PALETTE:
			bpp = 1;break;

		case G2D_FMT_2BPP_MONO:
		case G2D_FMT_2BPP_PALETTE:
			bpp = 2;break;

		case G2D_FMT_4BPP_MONO:
		case G2D_FMT_4BPP_PALETTE:
			bpp = 4;break;

		case G2D_FMT_8BPP_MONO:
		case G2D_FMT_8BPP_PALETTE:
		case G2D_FMT_PYUV422UVC:
		case G2D_FMT_PYUV420UVC:
		case G2D_FMT_PYUV411UVC:
		case G2D_FMT_PYUV422:
		case G2D_FMT_PYUV420:
		case G2D_FMT_PYUV411:
			bpp = 8;break;

		case G2D_FMT_IYUV422:
		case G2D_FMT_RGB565:
		case G2D_FMT_BGR565:
		case G2D_FMT_ARGB1555:
		case G2D_FMT_ABGR1555:
		case G2D_FMT_RGBA5551:
		case G2D_FMT_BGRA5551:
		case G2D_FMT_ARGB4444:
		case G2D_FMT_ABGR4444:
		case G2D_FMT_RGBA4444:
		case G2D_FMT_BGRA4444:
			bpp = 16;break;

		case G2D_FMT_ARGB_AYUV8888:
		case G2D_FMT_BGRA_VUYA8888:
		case G2D_FMT_ABGR_AVUY8888:
		case G2D_FMT_RGBA_YUVA8888:
		case G2D_FMT_XRGB8888:
		case G2D_FMT_BGRX8888:
		case G2D_FMT_XBGR8888:
		case G2D_FMT_RGBX8888:
			bpp = 32;break;

		default:
			bpp = 32;break;
	}
	return bpp;

}

__u32 mixer_in_fmtseq_set(__u32 format,__u32 pixel_seq)
{
	__u32 val = 32;
	switch (format)
	{
		case G2D_FMT_1BPP_MONO:
		case G2D_FMT_1BPP_PALETTE:
			if	   (pixel_seq == G2D_SEQ_1BPP_LITTER_LITTER)
				val = 0x3A;
			else if(pixel_seq == G2D_SEQ_1BPP_BIG_LITTER)
				val = 0x1A;
			else if(pixel_seq == G2D_SEQ_1BPP_LITTER_BIG)
				val = 0x2A;
			else
				val = 0xA;
			break;

		case G2D_FMT_2BPP_MONO:
		case G2D_FMT_2BPP_PALETTE:
			if	   (pixel_seq == G2D_SEQ_2BPP_LITTER_LITTER)
				val = 0x39;
			else if(pixel_seq == G2D_SEQ_2BPP_BIG_LITTER)
				val = 0x19;
			else if(pixel_seq == G2D_SEQ_2BPP_LITTER_BIG)
				val = 0x29;
			else
				val = 0x9;
			break;

		case G2D_FMT_4BPP_MONO:
		case G2D_FMT_4BPP_PALETTE:
			if	   (pixel_seq == G2D_SEQ_P01234567)
				val = 0x38;
			else if(pixel_seq == G2D_SEQ_P67452301)
				val = 0x18;
			else if(pixel_seq == G2D_SEQ_P10325476)
				val = 0x28;
			else
				val = 0x8;
			break;

		case G2D_FMT_8BPP_MONO:
		case G2D_FMT_8BPP_PALETTE:
			if(pixel_seq == G2D_SEQ_P0123)
				val = 0x17;
			else
				val = 0x7;
			break;

		case G2D_FMT_PYUV422UVC:
		case G2D_FMT_PYUV420UVC:
		case G2D_FMT_PYUV411UVC:
				val = 0x6;
			break;

		case G2D_FMT_IYUV422:
			if(pixel_seq == G2D_SEQ_YVYU)
				val = 0x14;
			else
				val = 0x4;
			break;
		case G2D_FMT_RGB565:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x13;
			else
				val = 0x3;
			break;
		case G2D_FMT_BGR565:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x93;
			else
				val = 0x83;
			break;
		case G2D_FMT_ARGB1555:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x12;
			else
				val = 0x2;
			break;
		case G2D_FMT_ABGR1555:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x92;
			else
				val = 0x82;
			break;
		case G2D_FMT_RGBA5551:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0xb2;
			else
				val = 0xa2;
			break;
		case G2D_FMT_BGRA5551:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x32;
			else
				val = 0x22;
			break;
		case G2D_FMT_ARGB4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x11;
			else
				val = 0x01;
			break;
		case G2D_FMT_ABGR4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x91;
			else
				val = 0x81;
			break;
		case G2D_FMT_RGBA4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0xb1;
			else
				val = 0xa1;
			break;
		case G2D_FMT_BGRA4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x31;
			else
				val = 0x21;
			break;
		case G2D_FMT_ARGB_AYUV8888:
		case G2D_FMT_XRGB8888:
			val = 0x0;
			break;
		case G2D_FMT_BGRA_VUYA8888:
		case G2D_FMT_BGRX8888:
			val = 0x20;
			break;
		case G2D_FMT_ABGR_AVUY8888:
		case G2D_FMT_XBGR8888:
			val = 0x80;
			break;
		case G2D_FMT_RGBA_YUVA8888:
		case G2D_FMT_RGBX8888:
			val = 0xa0;
			break;

		default:
			val = 0;break;
	}
	return val<<8;

}

__u32 mixer_in_csc_set(__u32 format)
{
	__u32 val = 0;
	switch (format)
	{
		case G2D_FMT_IYUV422:
			val = 0x11;
			break;
		case G2D_FMT_PYUV422UVC:
			val = 0x21;
			break;
		case G2D_FMT_PYUV420UVC:
			val = 0x31;
			break;
		case G2D_FMT_PYUV411UVC:
			val = 0x41;
			break;

		default:
			val = 0;break;
	}

	return val;
}

__u32 mixer_out_fmtseq_set(__u32 format,__u32 pixel_seq)
{
	__u32 val = 0;
	switch (format)
	{
		case G2D_FMT_1BPP_MONO:
			if	   (pixel_seq == G2D_SEQ_1BPP_LITTER_LITTER)
				val = 0x38A;
			else if(pixel_seq == G2D_SEQ_1BPP_BIG_LITTER)
				val = 0x18A;
			else if(pixel_seq == G2D_SEQ_1BPP_LITTER_BIG)
				val = 0x28A;
			else
				val = 0x8A;
			break;

		case G2D_FMT_2BPP_MONO:
			if	   (pixel_seq == G2D_SEQ_2BPP_LITTER_LITTER)
				val = 0x389;
			else if(pixel_seq == G2D_SEQ_2BPP_BIG_LITTER)
				val = 0x189;
			else if(pixel_seq == G2D_SEQ_2BPP_LITTER_BIG)
				val = 0x289;
			else
				val = 0x89;
			break;

		case G2D_FMT_4BPP_MONO:
			if	   (pixel_seq == G2D_SEQ_P01234567)
				val = 0x388;
			else if(pixel_seq == G2D_SEQ_P67452301)
				val = 0x188;
			else if(pixel_seq == G2D_SEQ_P10325476)
				val = 0x288;
			else
				val = 0x88;
			break;

		case G2D_FMT_8BPP_MONO:
			if(pixel_seq == G2D_SEQ_P0123)
				val = 0x187;
			else
				val = 0x87;
			break;
		case G2D_FMT_PYUV422:
			val = 0x86;
			break;
		case G2D_FMT_PYUV422UVC:
			val = 0x85;
			break;
		case G2D_FMT_PYUV420UVC:
			val = 0x8b;
			break;
		case G2D_FMT_PYUV420:
			val = 0x8c;
			break;
		case G2D_FMT_PYUV411UVC:
			val = 0x8d;
			break;
		case G2D_FMT_PYUV411:
			val = 0x8e;
			break;

		case G2D_FMT_IYUV422:
			if(pixel_seq == G2D_SEQ_YVYU)
				val = 0x184;
			else
				val = 0x84;
			break;
		case G2D_FMT_RGB565:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x183;
			else
				val = 0x3;
			break;
		case G2D_FMT_BGR565:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x983;
			else
				val = 0x883;
			break;
		case G2D_FMT_ARGB1555:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x182;
			else
				val = 0x82;
			break;
		case G2D_FMT_ABGR1555:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x982;
			else
				val = 0x882;
			break;
		case G2D_FMT_RGBA5551:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0xb82;
			else
				val = 0xa82;
			break;
		case G2D_FMT_BGRA5551:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x382;
			else
				val = 0x282;
			break;
		case G2D_FMT_ARGB4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x181;
			else
				val = 0x81;
			break;
		case G2D_FMT_ABGR4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x981;
			else
				val = 0x881;
			break;
		case G2D_FMT_RGBA4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0xb81;
			else
				val = 0xa81;
			break;
		case G2D_FMT_BGRA4444:
			if(pixel_seq == G2D_SEQ_P01)
				val = 0x381;
			else
				val = 0x281;
			break;
		case G2D_FMT_ARGB_AYUV8888:
		case G2D_FMT_XRGB8888:
			val = 0x80;
			break;
		case G2D_FMT_BGRA_VUYA8888:
		case G2D_FMT_BGRX8888:
			val = 0x280;
			break;
		case G2D_FMT_ABGR_AVUY8888:
		case G2D_FMT_XBGR8888:
			val = 0x880;
			break;
		case G2D_FMT_RGBA_YUVA8888:
		case G2D_FMT_RGBX8888:
			val = 0xa80;
			break;

		default:
			val = 80;break;
	}
	return val;

}

__u64 mixer_get_addr(__u32 buffer_addr, __u32 format, __u32 stride, __u32 x, __u32 y){
	__u32	bpp = 0;
	__u64	addr = 0;

	bpp = mixer_bpp_count(format);
	addr = (__u64)(buffer_addr-0x40000000)*8 + (__u64)((stride * y + x)*bpp);//bits

	return addr;
}

__u32 mixer_get_irq(void){
	__u32 reg_val = 0;

	reg_val |= read_wvalue(G2D_STATUS_REG);

	return reg_val;
}

__u32 mixer_clear_init(void){

	write_wvalue(G2D_STATUS_REG, 0x300);
	write_wvalue(G2D_CONTROL_REG, 0x0);

	return 0;
}

__u32 mixer_set_rotate_reg(__u32 flag){
	__u32 rot = 0;

	if	   (flag & G2D_BLT_FLIP_HORIZONTAL	)rot = 0x10;
	else if(flag & G2D_BLT_FLIP_VERTICAL	)rot = 0x20;
	else if(flag & G2D_BLT_ROTATE90			)rot = 0x50;
	else if(flag & G2D_BLT_ROTATE180		)rot = 0x30;
	else if(flag & G2D_BLT_ROTATE270		)rot = 0x60;
	else if(flag & G2D_BLT_MIRROR45			)rot = 0x70;
	else if(flag & G2D_BLT_MIRROR135		)rot = 0x40;
	else rot = 0;

	return rot;
}

__s32 mixer_fillrectangle(g2d_fillrect *para){
	__u32 reg_val = 0;
	__u64 addr_val;
	__s32 result = 0;

	mixer_reg_init();/* initial mixer register */

	/* channel0 is the fill surface */
	write_wvalue(G2D_DMA0_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));

	/* globe alpha mode */
	if(para->flag & G2D_FIL_PLANE_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x4;
	}
	else if(para->flag & G2D_FIL_MULTI_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x8;
	}
	reg_val |= 0x1;
	write_wvalue(G2D_DMA0_CONTROL_REG, reg_val);
	mixer_set_fillcolor(para->color,0);
	if((para->flag & G2D_FIL_PLANE_ALPHA) || (para->flag & G2D_FIL_PIXEL_ALPHA) || (para->flag & G2D_FIL_MULTI_ALPHA))
	{
		/* channel3 is the dst surface */
		addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_rect.x,para->dst_rect.y);
		reg_val = (addr_val>>32)&0xF;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val<<24);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA3_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA3_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
		write_wvalue(G2D_DMA3_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));
		reg_val = read_wvalue(G2D_DMA3_CONTROL_REG);

		/* palette format */
		if (para->dst_image.format>0x19)
		{
			reg_val |= 0x2;
		}
		reg_val |= G2D_IDMA_ENABLE | mixer_in_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq);
		write_wvalue(G2D_DMA3_CONTROL_REG, reg_val);
		write_wvalue(G2D_CK_CONTROL_REG, 0x1);
	}
	write_wvalue(G2D_ROP_INDEX0_REG, 0x840);

	/* output surface is the dst surface */
	write_wvalue(G2D_OUTPUT_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));

	addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_rect.x,para->dst_rect.y);
	reg_val = (addr_val>>32)&0xF;/* high addr in bits */
	write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
	reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
	write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);

	write_wvalue(G2D_OUTPUT0_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
	write_wvalue(G2D_OUTPUT_CONTROL_REG, mixer_out_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq));

	/* start */
	write_wvalue(G2D_CONTROL_REG, 0);
	write_wvalue(G2D_CONTROL_REG, 0x303);
	result = g2d_wait_cmd_finish();

	return result;
}

/*
 * Special fast path for just a simple copy/conversion between
 * ARGB8888, XRGB8888 and RGB565 formats.
 */
static __s32 mixer_simple_blt(g2d_blt *para)
{
	__u32 reg_val;
	__u64 addr_val;
	__s32 result = 0;

	/* Initial setup, clear all G2D hardware registers */
	mixer_reg_init();

	/*
	 * In the case if source and destination buffers are overlapping, we
	 * want to set a suitable scan order for getting correct results in
	 * the use cases such as scrolling.
	 *
	 * Seems like trying to set horizontal scan order has no effect. But
	 * at least setting vertical scan order allows to handle all types of
	 * overlapped blits, except when both of the following conditions are
	 * met at the same time:
	 *   1) the destination buffer is shifted right relative to the source
	 *      buffer by more than 1 pixel
	 *   2) there is no vertical shift (src_y == dst_y)
	 *
	 * If the buffers are not overlapping, then the scan order does not
	 * matter at all.
	 */
	if (para->src_image.addr[0] == para->dst_image.addr[0] &&
					para->src_rect.y < para->dst_y) {
		write_wvalue(G2D_SCAN_ORDER_REG, G2D_DOWN_TOP_LR);
		/* dst_y has to point at the bottom scanline, not the top one */
		para->dst_y += para->src_rect.h - 1;
	}

	/* Configure source surface */
	addr_val = mixer_get_addr(para->src_image.addr[0],
				  para->src_image.format, para->src_image.w,
				  para->src_rect.x, para->src_rect.y);
	reg_val = (addr_val >> 32) & 0xF; /* high addr in bits */
	write_wvalue(G2D_DMA_HADDR_REG, reg_val);
	reg_val = addr_val & 0xFFFFFFFF; /* low addr in bits */
	write_wvalue(G2D_DMA0_LADDR_REG, reg_val);
	write_wvalue(G2D_DMA0_STRIDE_REG, para->src_image.w *
				mixer_bpp_count(para->src_image.format));
	write_wvalue(G2D_DMA0_SIZE_REG, (para->src_rect.w - 1) |
					((para->src_rect.h - 1) << 16));
	reg_val = read_wvalue(G2D_DMA0_CONTROL_REG) | G2D_IDMA_ENABLE;
	reg_val |= mixer_in_fmtseq_set(para->src_image.format,
				       para->src_image.pixel_seq);
	/* Opaque source, need to set alpha channel to 0xFF */
	if (para->src_image.format == G2D_FMT_XRGB8888)
		reg_val |= (0xFF << 24) | 0x4;
	write_wvalue(G2D_DMA0_CONTROL_REG, reg_val);

	/*
	 * Clear low bits before converting to RGB565 in order to avoid funny
	 * rounding and data corruption for "RGB565 -> RGB565" copy operation.
	 * The value in G2D_DMA1_FILLCOLOR_REG happens to be processed as a
	 * constant for bitwise AND operation with the intermediate ARGB8888
	 * format inside of the G2D pipeline in the default configuration.
	 */
	if (para->dst_image.format == G2D_FMT_RGB565)
		write_wvalue(G2D_DMA1_FILLCOLOR_REG, 0xFFF8FCF8);
	else
		write_wvalue(G2D_DMA1_FILLCOLOR_REG, 0xFFFFFFFF);
	write_wvalue(G2D_DMA2_FILLCOLOR_REG, 0xFFFFFFFF);

	/* Configure output surface */
	write_wvalue(G2D_OUTPUT_SIZE_REG, (para->src_rect.w - 1) |
					  ((para->src_rect.h - 1) << 16));
	addr_val = mixer_get_addr(para->dst_image.addr[0],
				  para->dst_image.format, para->dst_image.w,
				  para->dst_x, para->dst_y);
	reg_val = (addr_val >> 32) & 0xF; /* high addr in bits */
	write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
	reg_val = addr_val & 0xFFFFFFFF; /* low addr in bits */
	write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);
	write_wvalue(G2D_OUTPUT0_STRIDE_REG, para->dst_image.w *
				mixer_bpp_count(para->dst_image.format));
	reg_val = mixer_out_fmtseq_set(para->dst_image.format,
				       para->dst_image.pixel_seq);
	write_wvalue(G2D_OUTPUT_CONTROL_REG, reg_val);

	/* Start */
	write_wvalue(G2D_CONTROL_REG, 0x0);
	write_wvalue(G2D_CONTROL_REG, 0x303);
	/* Wait for completion */
	result = g2d_wait_cmd_finish();

	return result;
}

__s32 mixer_blt(g2d_blt *para){
	__u32 bppnum = 0;
	__u32 reg_val = 0;
	__u64 addr_val;
	__s32 result = 0;
	__u32 i,j;

	/* Common formats are handled with a special fast path */
	if (para->flag == G2D_BLT_NONE &&
			(para->src_image.format == G2D_FMT_ARGB_AYUV8888 ||
			 para->src_image.format == G2D_FMT_XRGB8888 ||
			 para->src_image.format == G2D_FMT_RGB565) &&
			(para->dst_image.format == G2D_FMT_ARGB_AYUV8888 ||
			 para->dst_image.format == G2D_FMT_XRGB8888 ||
			 para->dst_image.format == G2D_FMT_RGB565))
		return mixer_simple_blt(para);

	mixer_reg_init();/* initial mixer register */
	if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A)&&(para->dst_image.pixel_seq == G2D_SEQ_VUVU)){
		for(i=0, j=0; i<12; i++,j+=4)write_wvalue(G2D_CSC2_ADDR_REG + j, csc2coeff_VUVU[i]&0xFFFF);/* 0x1c0-0x1ec */
	}

	/* src surface */
	addr_val = mixer_get_addr(para->src_image.addr[0],para->src_image.format,para->src_image.w,para->src_rect.x,para->src_rect.y);
	reg_val = (addr_val>>32)&0xF;/* high addr in bits */
	write_wvalue(G2D_DMA_HADDR_REG, reg_val);
	reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
	write_wvalue(G2D_DMA0_LADDR_REG, reg_val);
	write_wvalue(G2D_DMA0_STRIDE_REG, para->src_image.w*mixer_bpp_count(para->src_image.format));
	write_wvalue(G2D_DMA0_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
	reg_val = read_wvalue(G2D_DMA0_CONTROL_REG);
	reg_val |= mixer_in_fmtseq_set(para->src_image.format,para->src_image.pixel_seq) | G2D_IDMA_ENABLE;

	/* rgbx/bgrx/xrgb/xbgr format */
	if((para->src_image.format>0x03)&&(para->src_image.format<0x08))
	{
		reg_val |= (0xFF<<24)|0x4;
	}

	/* palette format */
	if (para->src_image.format>0x1C)
	{
		reg_val |= 0x2;
	}

	/* globe alpha mode */
	if(para->flag & G2D_BLT_PLANE_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x4;
	}
	else if(para->flag & G2D_BLT_MULTI_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x8;
	}

	/* rotate/mirror */
	reg_val |= mixer_set_rotate_reg(para->flag);
	write_wvalue(G2D_DMA0_CONTROL_REG, reg_val);
	reg_val = mixer_in_csc_set(para->src_image.format);
	write_wvalue(G2D_CSC0_CONTROL_REG, reg_val);
	reg_val = mixer_in_csc_set(para->dst_image.format);
	write_wvalue(G2D_CSC1_CONTROL_REG, reg_val);

	/* pyuv422/420/411uvc */
	if((para->src_image.format>0x16)&&(para->src_image.format<0x1A))
	{
		if(para->src_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
		else bppnum = 8;
		if(para->src_image.format == G2D_FMT_PYUV420UVC)
			addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*(para->src_rect.y/2)+para->src_rect.x)*bppnum);
		else addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*para->src_rect.y+para->src_rect.x)*bppnum);
		reg_val = read_wvalue(G2D_DMA_HADDR_REG);
		reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA1_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA1_STRIDE_REG, para->src_image.w*bppnum);
		write_wvalue(G2D_DMA1_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
		reg_val = read_wvalue(G2D_DMA1_CONTROL_REG);
		reg_val |= (5<<8) | G2D_IDMA_ENABLE;

		/* rotate/mirror */
		reg_val |= mixer_set_rotate_reg(para->flag);
		write_wvalue(G2D_DMA1_CONTROL_REG, reg_val);
	}

	/* pyuv422/420/411uvc */
	if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
	{
		if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
		else bppnum = 8;
		if(para->dst_image.format == G2D_FMT_PYUV420UVC)
			 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_y/2)+para->dst_x)*bppnum);
		else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_y+para->dst_x)*bppnum);
		reg_val = read_wvalue(G2D_DMA_HADDR_REG);
		reg_val |= ((addr_val>>32)&0xF)<<16;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA2_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA2_STRIDE_REG, para->dst_image.w*bppnum);
		write_wvalue(G2D_DMA2_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
		reg_val = read_wvalue(G2D_DMA2_CONTROL_REG);
		reg_val |= (5<<8) | G2D_IDMA_ENABLE;
		write_wvalue(G2D_DMA2_CONTROL_REG, reg_val);
	}
	write_wvalue(G2D_DMA1_FILLCOLOR_REG, 0xFFFFFFFF);
	write_wvalue(G2D_DMA2_FILLCOLOR_REG, 0xFFFFFFFF);

	/* channel3 is dst surface */
	addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_x,para->dst_y);
	reg_val = read_wvalue(G2D_DMA_HADDR_REG);
	reg_val |= ((addr_val>>32)&0xF)<<24;/* high addr in bits */
	write_wvalue(G2D_DMA_HADDR_REG, reg_val);
	reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
	write_wvalue(G2D_DMA3_LADDR_REG, reg_val);
	write_wvalue(G2D_DMA3_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
	if((para->flag & G2D_BLT_ROTATE90) || (para->flag & G2D_BLT_ROTATE270))
		 write_wvalue(G2D_DMA3_SIZE_REG, (para->src_rect.h -1) | ((para->src_rect.w -1)<<16));
	else write_wvalue(G2D_DMA3_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
	reg_val = read_wvalue(G2D_DMA3_CONTROL_REG);
	reg_val |= mixer_in_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq) | G2D_IDMA_ENABLE;

	/* rgbx/bgrx/xrgb/xbgr format */
	if((para->dst_image.format>0x03)&&(para->dst_image.format<0x08))
	{
		reg_val |= (0xFF<<24)|0x4;
	}
	write_wvalue(G2D_DMA3_CONTROL_REG, reg_val);

	/* colorkey */
	if (para->flag & G2D_BLT_SRC_COLORKEY)
	{
		reg_val = 0x3;
	}
	else if(para->flag & G2D_BLT_DST_COLORKEY)
	{
		reg_val = 0x5;
	}
	else if((para->flag & G2D_BLT_PIXEL_ALPHA)||(para->flag & G2D_BLT_PLANE_ALPHA)||(para->flag & G2D_BLT_MULTI_ALPHA))
	{
		reg_val = 0x1;
	}
	else {reg_val = 0x0;}
	write_wvalue(G2D_CK_CONTROL_REG, reg_val);
	write_wvalue(G2D_CK_MINCOLOR_REG, para->color);
	write_wvalue(G2D_CK_MAXCOLOR_REG, para->color);

	/* output surface is the dst surface */
	if((para->flag & G2D_BLT_ROTATE90) || (para->flag & G2D_BLT_ROTATE270))
	{
		write_wvalue(G2D_OUTPUT_SIZE_REG, (para->src_rect.h -1) | ((para->src_rect.w -1)<<16));
	}
	else
	{
		write_wvalue(G2D_OUTPUT_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
	}
	addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_x,para->dst_y);
	reg_val = (addr_val>>32)&0xF;/* high addr in bits */
	write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
	reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
	write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);
	write_wvalue(G2D_OUTPUT0_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
	reg_val = mixer_out_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq);
	write_wvalue(G2D_OUTPUT_CONTROL_REG, reg_val);

	if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
	{
		if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
		else bppnum = 8;
		if(para->dst_image.format == G2D_FMT_PYUV420UVC)
			 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_y/2)+para->dst_x)*bppnum);
		else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_y+para->dst_x)*bppnum);
		reg_val = read_wvalue(G2D_OUTPUT_HADDR_REG);
		reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
		write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_OUTPUT1_LADDR_REG, reg_val);
		write_wvalue(G2D_OUTPUT1_STRIDE_REG, para->dst_image.w*bppnum);
		write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);
	}
	if((para->dst_image.format>0x11)&&(para->dst_image.format<0x1D))write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);
	if((para->flag & G2D_BLT_PIXEL_ALPHA)|(para->flag & G2D_BLT_PLANE_ALPHA)|(para->flag & G2D_BLT_MULTI_ALPHA)|(para->flag & G2D_BLT_SRC_COLORKEY)|(para->flag & G2D_BLT_DST_COLORKEY))
		write_wvalue(G2D_OALPHA_CONTROL_REG, 0x80);/* 0x40: A2 area keep the dst alpha,0x80: A2 area keep the src+dst(1-src) alpha value */

	/* start */
	write_wvalue(G2D_CONTROL_REG, 0x0);
	write_wvalue(G2D_CONTROL_REG, 0x303);
	result = g2d_wait_cmd_finish();

	return result;
}

__s32 mixer_stretchblt(g2d_stretchblt *para){
	__u32 bppnum = 0;
	__u32 reg_val = 0;
	__u32 reg_tmp = 0;
	__u64 addr_val;
	__u32 cnt,sinw,soutw,scaler_inx,scaler_outx,scaler_outy,i;
	__s32 result = 0;

	mixer_reg_init();/* initial mixer register */

	/* src surface */
	write_wvalue(G2D_DMA0_STRIDE_REG, para->src_image.w*mixer_bpp_count(para->src_image.format));
	reg_val = read_wvalue(G2D_DMA0_CONTROL_REG);
	reg_val |= mixer_in_fmtseq_set(para->src_image.format,para->src_image.pixel_seq) | G2D_IDMA_ENABLE;

	/* rgbx/bgrx/xrgb/xbgr format */
	if((para->src_image.format>0x03)&&(para->src_image.format<0x08))
	{
		reg_val |= (0xFF<<24)|0x4;
	}

	/* palette format */
	if (para->src_image.format>0x1C)
	{
		reg_val |= 0x2;
	}

	/* globe alpha mode */
	if(para->flag & G2D_BLT_PLANE_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x4;
	}
	else if(para->flag & G2D_BLT_MULTI_ALPHA)
	{
		reg_val |= (para->alpha<<24)|0x8;
	}

	/* rotate/mirror */
	reg_val |= mixer_set_rotate_reg(para->flag);
	write_wvalue(G2D_DMA0_CONTROL_REG, reg_val);
	reg_val = mixer_in_csc_set(para->src_image.format);
	write_wvalue(G2D_CSC0_CONTROL_REG, reg_val);
	reg_val = mixer_in_csc_set(para->dst_image.format);
	write_wvalue(G2D_CSC1_CONTROL_REG, reg_val);

	/* sacler setting */
	write_wvalue(G2D_SCALER_CONTROL_REG,G2D_SCALER_4TAP4 | G2D_SCALER_ENABLE);
	write_wvalue(G2D_SCALER_HPHASE_REG,0);
	write_wvalue(G2D_SCALER_VPHASE_REG,0);
	write_wvalue(G2D_ROP_INDEX0_REG, 0x840);

	/* channel3 is dst surface */
	write_wvalue(G2D_DMA3_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
	reg_val = read_wvalue(G2D_DMA3_CONTROL_REG);
	reg_val |= mixer_in_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq) | G2D_IDMA_ENABLE;

	/* rgbx/bgrx/xrgb/xbgr format */
	if((para->src_image.format>0x03)&&(para->src_image.format<0x08))
	{
		reg_val |= (0xFF<<24)|0x4;
	}
	write_wvalue(G2D_DMA3_CONTROL_REG, reg_val);

	/* colorkey */
	if (para->flag & G2D_BLT_SRC_COLORKEY)
	{
		reg_val = 0x3;
	}
	else if(para->flag & G2D_BLT_DST_COLORKEY)
	{
		reg_val = 0x5;
	}
	else if((para->flag & G2D_BLT_PIXEL_ALPHA)||(para->flag & G2D_BLT_PLANE_ALPHA)||(para->flag & G2D_BLT_MULTI_ALPHA))
	{
		reg_val = 1;
	}
	else {reg_val = 0x0;}
	write_wvalue(G2D_CK_CONTROL_REG, reg_val);
	write_wvalue(G2D_CK_MINCOLOR_REG, para->color);
	write_wvalue(G2D_CK_MAXCOLOR_REG, para->color);

	write_wvalue(G2D_OUTPUT0_STRIDE_REG, para->dst_image.w*mixer_bpp_count(para->dst_image.format));
	reg_val = mixer_out_fmtseq_set(para->dst_image.format,para->dst_image.pixel_seq);
	write_wvalue(G2D_OUTPUT_CONTROL_REG, reg_val);
	if((para->flag & G2D_BLT_PIXEL_ALPHA)|(para->flag & G2D_BLT_PLANE_ALPHA)|(para->flag & G2D_BLT_MULTI_ALPHA)|(para->flag & G2D_BLT_SRC_COLORKEY)|(para->flag & G2D_BLT_DST_COLORKEY))
		write_wvalue(G2D_OALPHA_CONTROL_REG, 0x80);/* 0x40: A2 area keep the dst alpha,0x80: A2 area keep the src+dst(1-src) alpha value */

	/* output width lager than 1024 pixel width */
	if(para->dst_rect.w>0x400)
	{
		/* scaler up divide the output into 1024 pixel width part */
		cnt = para->dst_rect.w/1024;
		cnt = (para->dst_rect.w%1024)?cnt:cnt-1;
		sinw = (para->src_rect.w/para->dst_rect.w)<<10;
		sinw |= ((para->src_rect.w%para->dst_rect.w)<<10)/para->dst_rect.w;
		scaler_inx = para->src_rect.x;
		scaler_outx = para->dst_rect.x;
		if((para->flag & G2D_BLT_ROTATE90))
			 scaler_outy = para->dst_rect.y + para->dst_rect.w - 0x401;
		else scaler_outy = para->dst_rect.y;
		for(i = 0; i<cnt; i++)
		{
			/* DMA0 */
			addr_val = mixer_get_addr(para->src_image.addr[0],para->src_image.format,para->src_image.w,scaler_inx,para->src_rect.y);
			reg_val = (addr_val>>32)&0xF;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA0_LADDR_REG, reg_val);
			if(sinw<1)sinw = 1;
			write_wvalue(G2D_DMA0_SIZE_REG, (sinw -1) | ((para->src_rect.h -1)<<16));

			/* DMA1 pyuv422/420/411uvc */
			if((para->src_image.format>0x16)&&(para->src_image.format<0x1A))
			{
				if(para->src_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
				else bppnum = 8;
				if(para->src_image.format == G2D_FMT_PYUV420UVC)
					 addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*(para->src_rect.y/2)+scaler_inx)*bppnum);
				else addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*para->src_rect.y+scaler_inx)*bppnum);
				reg_val = read_wvalue(G2D_DMA_HADDR_REG);
				reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
				write_wvalue(G2D_DMA_HADDR_REG, reg_val);
				reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
				write_wvalue(G2D_DMA1_LADDR_REG, reg_val);
				write_wvalue(G2D_DMA1_STRIDE_REG, para->src_image.w*bppnum);
				if(sinw<1)sinw = 1;
				write_wvalue(G2D_DMA1_SIZE_REG, (sinw -1) | ((para->src_rect.h -1)<<16));
				reg_val = read_wvalue(G2D_DMA1_CONTROL_REG);
				reg_val |= (5<<8) | G2D_IDMA_ENABLE;

				/* rotate/mirror */
				reg_val |= mixer_set_rotate_reg(para->flag);
				write_wvalue(G2D_DMA1_CONTROL_REG, reg_val);
			}

			/* scaler setting */
			write_wvalue(G2D_SCALER_SIZE_REG,(0x400 - 1) | ((para->dst_rect.h - 1)<<16));
			reg_val = (sinw/0x400)<<16;
			reg_tmp = (sinw%0x400);
			reg_val |= (reg_tmp<<16)/0x400;
			write_wvalue(G2D_SCALER_HFACTOR_REG,reg_val);

			reg_val = (para->src_rect.h/para->dst_rect.h)<<16;
			reg_tmp = (para->src_rect.h%para->dst_rect.h);
			reg_val |= (reg_tmp<<16)/para->dst_rect.h;
			write_wvalue(G2D_SCALER_VFACTOR_REG,reg_val);

			/* DMA2 pyuv422/420/411uvc */
			if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
			{
				if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
				else bppnum = 8;
				if(para->dst_image.format == G2D_FMT_PYUV420UVC)
					 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_rect.y/2) + scaler_outx)*bppnum);
				else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_rect.y + scaler_outx)*bppnum);
				reg_val = read_wvalue(G2D_DMA_HADDR_REG);
				reg_val |= ((addr_val>>32)&0xF)<<16;/* high addr in bits */
				write_wvalue(G2D_DMA_HADDR_REG, reg_val);
				reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
				write_wvalue(G2D_DMA2_LADDR_REG, reg_val);
				write_wvalue(G2D_DMA2_STRIDE_REG, para->dst_image.w*bppnum);
				write_wvalue(G2D_DMA2_SIZE_REG, (0x400 -1) | ((para->dst_rect.h -1)<<16));
				reg_val = read_wvalue(G2D_DMA2_CONTROL_REG);
				reg_val |= (5<<8) | G2D_IDMA_ENABLE;
				write_wvalue(G2D_DMA2_CONTROL_REG, reg_val);
			}

			/* DMA3 */
			addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,scaler_outx,para->dst_rect.y);
			reg_val = read_wvalue(G2D_DMA_HADDR_REG);
			reg_val = reg_val&0xF0FFFFFF;
			reg_val |= ((addr_val>>32)&0xF)<<24;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA3_LADDR_REG, reg_val);
			write_wvalue(G2D_DMA3_SIZE_REG, (0x400 -1) | ((para->dst_rect.h -1)<<16));

			/* OUT */
			if((para->flag & G2D_BLT_ROTATE90) || (para->flag & G2D_BLT_ROTATE270))
				 write_wvalue(G2D_OUTPUT_SIZE_REG, (para->dst_rect.h -1) | ((0x400 -1)<<16));
			else write_wvalue(G2D_OUTPUT_SIZE_REG, (0x400 -1) | ((para->dst_rect.h -1)<<16));
			addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,scaler_outx,scaler_outy);
			reg_val = (addr_val>>32)&0xF;/* high addr in bits */
			write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);

			/* OUT1 */
			if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
			{
				if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
				else bppnum = 8;
				if(para->dst_image.format == G2D_FMT_PYUV420UVC)
					 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(scaler_outy/2) + scaler_outx)*bppnum);
				else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*scaler_outy + scaler_outx)*bppnum);
				reg_val = read_wvalue(G2D_OUTPUT_HADDR_REG);
				reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
				write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
				reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
				write_wvalue(G2D_OUTPUT1_LADDR_REG, reg_val);
				write_wvalue(G2D_OUTPUT1_STRIDE_REG, para->dst_image.w*bppnum);
				write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);
			}
			if((para->dst_image.format>0x11)&&(para->dst_image.format<0x1D))write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);

			scaler_inx +=sinw;
			if((para->flag & G2D_BLT_ROTATE90))
			{
				scaler_outy -=0x400;
				scaler_outx = para->dst_rect.x;
			}
			else if((para->flag & G2D_BLT_ROTATE270))
			{
				scaler_outy +=0x400;
				scaler_outx = para->dst_rect.x;
			}
			else
			{
				scaler_outy = para->dst_rect.y;
				scaler_outx +=0x400;
			}

			/* start */
			write_wvalue(G2D_CONTROL_REG, 0x0);
			write_wvalue(G2D_CONTROL_REG, 0x303);
			result |= g2d_wait_cmd_finish();
			if(result!=0)return result;
		}

		/* last block */
		soutw = para->dst_rect.w - 0x400*cnt;
		sinw = para->src_rect.w - sinw*cnt;

		/* DMA0 */
		addr_val = mixer_get_addr(para->src_image.addr[0],para->src_image.format,para->src_image.w,scaler_inx,para->src_rect.y);
		reg_val = (addr_val>>32)&0xF;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA0_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA0_SIZE_REG, (sinw -1) | ((para->src_rect.h -1)<<16));

		/* DMA1 pyuv422/420/411uvc */
		if((para->src_image.format>0x16)&&(para->src_image.format<0x1A))
		{
			if(para->src_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->src_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*(para->src_rect.y/2)+scaler_inx)*bppnum);
			else addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*para->src_rect.y+scaler_inx)*bppnum);
			reg_val = read_wvalue(G2D_DMA_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA1_LADDR_REG, reg_val);
			write_wvalue(G2D_DMA1_STRIDE_REG, para->src_image.w*bppnum);
			if(sinw<1)sinw = 1;
			write_wvalue(G2D_DMA1_SIZE_REG, (sinw -1) | ((para->src_rect.h -1)<<16));
			reg_val = read_wvalue(G2D_DMA1_CONTROL_REG);
			reg_val |= (5<<8) | G2D_IDMA_ENABLE;

			/* rotate/mirror */
			reg_val |= mixer_set_rotate_reg(para->flag);
			write_wvalue(G2D_DMA1_CONTROL_REG, reg_val);
		}

		/* scaler setting */
		if(soutw<1)soutw = 1;
		write_wvalue(G2D_SCALER_SIZE_REG,(soutw- 1) | ((para->dst_rect.h - 1)<<16));
		reg_val = (sinw/soutw)<<16;
		reg_tmp = (sinw%soutw);
		reg_val |= (reg_tmp<<16)/soutw;
		write_wvalue(G2D_SCALER_HFACTOR_REG,reg_val);

		reg_val = (para->src_rect.h/para->dst_rect.h)<<16;
		reg_tmp = (para->src_rect.h%para->dst_rect.h);
		reg_val |= (reg_tmp<<16)/para->dst_rect.h;
		write_wvalue(G2D_SCALER_VFACTOR_REG,reg_val);

		/* DMA2 pyuv422/420/411uvc */
		if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
		{
			if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->dst_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_rect.y/2) + scaler_outx)*bppnum);
			else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_rect.y + scaler_outx)*bppnum);
			reg_val = read_wvalue(G2D_DMA_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<16;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA2_LADDR_REG, reg_val);
			write_wvalue(G2D_DMA2_STRIDE_REG, para->dst_image.w*bppnum);
			if(soutw<1)soutw = 1;
			write_wvalue(G2D_DMA2_SIZE_REG, (soutw -1) | ((para->dst_rect.h -1)<<16));
			reg_val = read_wvalue(G2D_DMA2_CONTROL_REG);
			reg_val |= (5<<8) | G2D_IDMA_ENABLE;
			write_wvalue(G2D_DMA2_CONTROL_REG, reg_val);
		}

		/* DMA3 */
		addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,scaler_outx,para->dst_rect.y);
		reg_val = read_wvalue(G2D_DMA_HADDR_REG);
		reg_val = reg_val&0xF0FFFFFF;
		reg_val |= ((addr_val>>32)&0xF)<<24;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA3_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA3_SIZE_REG, (soutw - 1) | ((para->dst_rect.h -1)<<16));

		/* OUT */
		if((para->flag & G2D_BLT_ROTATE90) || (para->flag & G2D_BLT_ROTATE270))
			 write_wvalue(G2D_OUTPUT_SIZE_REG, (para->dst_rect.h -1) | ((soutw - 1)<<16));
		else write_wvalue(G2D_OUTPUT_SIZE_REG, (soutw - 1) | ((para->dst_rect.h -1)<<16));
		if((para->flag & G2D_BLT_ROTATE270))
			 addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,scaler_outx,scaler_outy);
		else addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,scaler_outx,para->dst_rect.y);
		reg_val = (addr_val>>32)&0xF;/* high addr in bits */
		write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);

		/* OUT1 */
		if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
		{
			if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->dst_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_rect.y/2) + scaler_outx)*bppnum);
			else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_rect.y + scaler_outx)*bppnum);
			reg_val = read_wvalue(G2D_OUTPUT_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
			write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_OUTPUT1_LADDR_REG, reg_val);
			write_wvalue(G2D_OUTPUT1_STRIDE_REG, para->dst_image.w*bppnum);
			write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);
		}
		if((para->dst_image.format>0x11)&&(para->dst_image.format<0x1D))write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);

		/* start */
		write_wvalue(G2D_CONTROL_REG, 0x0);
		write_wvalue(G2D_CONTROL_REG, 0x303);
		result |= g2d_wait_cmd_finish();
	}

	/* output width smaller than 1024 pixel width */
	else
	{
		addr_val = mixer_get_addr(para->src_image.addr[0],para->src_image.format,para->src_image.w,para->src_rect.x,para->src_rect.y);
		reg_val = (addr_val>>32)&0xF;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA0_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA0_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));

		write_wvalue(G2D_SCALER_SIZE_REG,(para->dst_rect.w - 1) | ((para->dst_rect.h -1)<<16));
		reg_val = (para->src_rect.w/para->dst_rect.w)<<16;
		reg_tmp = (para->src_rect.w%para->dst_rect.w);
		reg_val |= (reg_tmp<<16)/para->dst_rect.w;
		write_wvalue(G2D_SCALER_HFACTOR_REG,reg_val);
		reg_val = (para->src_rect.h/para->dst_rect.h)<<16;
		reg_tmp = (para->src_rect.h%para->dst_rect.h);
		reg_val |= (reg_tmp<<16)/para->dst_rect.h;
		write_wvalue(G2D_SCALER_VFACTOR_REG,reg_val);

		/* pyuv422/420/411uvc */
		if((para->src_image.format>0x16)&&(para->src_image.format<0x1A))
		{
			if(para->src_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->src_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*(para->src_rect.y/2)+para->src_rect.x)*bppnum);
			else addr_val = (__u64)(para->src_image.addr[1]-0x40000000)*8+(__u64)((para->src_image.w*para->src_rect.y+para->src_rect.x)*bppnum);
			reg_val = read_wvalue(G2D_DMA_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA1_LADDR_REG, reg_val);
			write_wvalue(G2D_DMA1_STRIDE_REG, para->src_image.w*bppnum);
			write_wvalue(G2D_DMA1_SIZE_REG, (para->src_rect.w -1) | ((para->src_rect.h -1)<<16));
			reg_val = read_wvalue(G2D_DMA1_CONTROL_REG);
			reg_val |= (5<<8) | G2D_IDMA_ENABLE;
			write_wvalue(G2D_DMA1_CONTROL_REG, reg_val);
		}

		/* pyuv422/420/411uvc */
		if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
		{
			if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->dst_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_rect.y/2)+para->dst_rect.x)*bppnum);
			else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_rect.y+para->dst_rect.x)*bppnum);
			reg_val = read_wvalue(G2D_DMA_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<16;/* high addr in bits */
			write_wvalue(G2D_DMA_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_DMA2_LADDR_REG, reg_val);
			write_wvalue(G2D_DMA2_STRIDE_REG, para->dst_image.w*bppnum);
			write_wvalue(G2D_DMA2_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));
			reg_val = read_wvalue(G2D_DMA2_CONTROL_REG);
			reg_val |= (5<<8) | G2D_IDMA_ENABLE;
			write_wvalue(G2D_DMA2_CONTROL_REG, reg_val);
		}

		addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_rect.x,para->dst_rect.y);
		reg_val = read_wvalue(G2D_DMA_HADDR_REG);
		reg_val |= ((addr_val>>32)&0xF)<<24;/* high addr in bits */
		write_wvalue(G2D_DMA_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_DMA3_LADDR_REG, reg_val);
		write_wvalue(G2D_DMA3_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));

		/* output surface is the dst surface */
		if((para->flag & G2D_BLT_ROTATE90) || (para->flag & G2D_BLT_ROTATE270))
			 write_wvalue(G2D_OUTPUT_SIZE_REG, (para->dst_rect.h -1) | ((para->dst_rect.w -1)<<16));
		else write_wvalue(G2D_OUTPUT_SIZE_REG, (para->dst_rect.w -1) | ((para->dst_rect.h -1)<<16));
		addr_val = mixer_get_addr(para->dst_image.addr[0],para->dst_image.format,para->dst_image.w,para->dst_rect.x,para->dst_rect.y);
		reg_val = (addr_val>>32)&0xF;/* high addr in bits */
		write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
		reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
		write_wvalue(G2D_OUTPUT0_LADDR_REG, reg_val);

		if((para->dst_image.format>0x16)&&(para->dst_image.format<0x1A))
		{
			if(para->dst_image.format == G2D_FMT_PYUV411UVC) bppnum = 4;
			else bppnum = 8;
			if(para->dst_image.format == G2D_FMT_PYUV420UVC)
				 addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*(para->dst_rect.y/2)+para->dst_rect.x)*bppnum);
			else addr_val = (__u64)(para->dst_image.addr[1]-0x40000000)*8+(__u64)((para->dst_image.w*para->dst_rect.y+para->dst_rect.x)*bppnum);
			reg_val = read_wvalue(G2D_OUTPUT_HADDR_REG);
			reg_val |= ((addr_val>>32)&0xF)<<8;/* high addr in bits */
			write_wvalue(G2D_OUTPUT_HADDR_REG, reg_val);
			reg_val = addr_val&0xFFFFFFFF;/* low addr in bits */
			write_wvalue(G2D_OUTPUT1_LADDR_REG, reg_val);
			write_wvalue(G2D_OUTPUT1_STRIDE_REG, para->dst_image.w*bppnum);
			write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);
		}
		if((para->dst_image.format>0x11)&&(para->dst_image.format<0x1D))write_wvalue(G2D_CSC2_CONTROL_REG, 0x1);

		/* start */
		write_wvalue(G2D_CONTROL_REG, 0x0);
		write_wvalue(G2D_CONTROL_REG, 0x303);
		result = g2d_wait_cmd_finish();
	}

	return result;
}

__u32 mixer_set_palette(g2d_palette *para){
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(para->size > 0x400)
    {
    	para->size = 0x400;
    }

	psrc_cur = para->pbuffer;
	pdest_cur = (__u32*)(base_addr+G2D_PALETTE_TAB_REG);
	pdest_end = pdest_cur + (para->size>>2);

    while(pdest_cur < pdest_end)
    {
		*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

   return 0;
}
