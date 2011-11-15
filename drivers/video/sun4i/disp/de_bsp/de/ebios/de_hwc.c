
#include "de_be.h"

__s32 DE_BE_HWC_Enable(__u32 sel,__bool enable)
{
    __u32 tmp;

    tmp = DE_BE_RUINT32(sel, DE_BE_MODE_CTL_OFF);
    tmp &= (~(1<<16));
    DE_BE_WUINT32(sel, DE_BE_MODE_CTL_OFF , tmp | (enable<<16));

    return 0;
}

__s32  DE_BE_HWC_Set_Pos(__u32 sel, __disp_pos_t *pos)
{
    __u32 tmp;

    tmp = DE_BE_RUINT32(sel, DE_BE_HWC_CRD_CTL_OFF);
    DE_BE_WUINT32(sel, DE_BE_HWC_CRD_CTL_OFF,(tmp & 0xf800f800)|(pos->y & 0x7ff)<<16|(pos->x & 0x7ff));

    return 0;
}

__s32 DE_BE_HWC_Get_Pos(__u32 sel, __disp_pos_t *pos)
{
    __u32 readval;

    readval = DE_BE_RUINT32(sel, DE_BE_HWC_CRD_CTL_OFF);
    pos->y = (readval & 0x07ff0000)>>16;
    pos->x = (readval & 0x07ff);

    return 0;

}

__s32 DE_BE_HWC_Set_Palette(__u32 sel, __u32 address,__u32 offset,__u32 size)
{
    __u16 i;
    __u32 read_val;
	__u32 reg_addr;

	reg_addr = DE_BE_HWC_PALETTE_TABLE_ADDR_OFF+offset;

    for(i = 0; i < size; i = i + 4)
    {
       read_val = DE_RUINT32( address + i);
       DE_BE_WUINT32(sel, reg_addr, read_val);
	   reg_addr = reg_addr + 4;
    }

    return 0;
}

__s32 DE_BE_HWC_Set_Src(__u32 sel, de_hwc_src_t *hwc_pat)
{
    __u32 tmp;
    __u32 x_size=0,y_size=0,pixel_fmt=0;
    __u32 i;
    __u32 size;

	switch (hwc_pat->mode)
	{
		 case DE_H32_V32_8BPP:
		      x_size = DE_N32PIXELS;
		      y_size = DE_N32PIXELS;
		      pixel_fmt = DE_IF8BPP;
		      size = 32*32;
		 	  break;

		 case DE_H64_V64_2BPP:
		      x_size = DE_N64PIXELS;
		      y_size = DE_N64PIXELS;
		      pixel_fmt = DE_IF2BPP;
		      size = 64*64/4;
		 	  break;

		 case DE_H64_V32_4BPP:
		      x_size = DE_N64PIXELS;
		      y_size = DE_N32PIXELS;
		      pixel_fmt = DE_IF4BPP;
		      size = 64*32/2;
		 	  break;

		 case DE_H32_V64_4BPP:
		      x_size = DE_N32PIXELS;
		      y_size = DE_N64PIXELS;
		      pixel_fmt = DE_IF4BPP;
		      size = 32*64/2;
		 	  break;

		 default:
		 	break;
	}

	if(hwc_pat->paddr & 0x3)//地址非32位对齐
	{
		for(i = 0; i < size; i+=4)
		{
			__u32 value = 0;

			tmp = DE_RUINT8(hwc_pat->paddr + i );
			value = tmp;
			tmp = DE_RUINT8(hwc_pat->paddr + i + 1 );
			value |= (tmp<<8);
			tmp = DE_RUINT8(hwc_pat->paddr + i + 2);
			value |= (tmp<<16);
			tmp = DE_RUINT8(hwc_pat->paddr + i + 3);
			value |= (tmp<<24);
			DE_BE_WUINT32(sel, DE_BE_HWC_MEMORY_ADDR_OFF + i, value);
		}
	}
	else
	{
		for(i = 0; i < size; i+=4)
		{
			tmp = DE_RUINT32(hwc_pat->paddr + i );
			DE_BE_WUINT32(sel, DE_BE_HWC_MEMORY_ADDR_OFF + i, tmp);
		}
	}

    tmp = DE_BE_RUINT32(sel, DE_BE_HWC_FRMBUF_OFF);
    DE_BE_WUINT32(sel, DE_BE_HWC_FRMBUF_OFF,(tmp & 0xffffffc3) | (x_size<<2) | (y_size<<4));//xsize and ysize

    tmp=DE_BE_RUINT32(sel, DE_BE_HWC_FRMBUF_OFF);
    DE_BE_WUINT32(sel, DE_BE_HWC_FRMBUF_OFF, (tmp & 0xfffffffc) | pixel_fmt);//format

    tmp = DE_BE_RUINT32(sel, DE_BE_HWC_CRD_CTL_OFF);
    DE_BE_WUINT32(sel, DE_BE_HWC_CRD_CTL_OFF, (tmp & 0x07ff07ff ) | 0 << 27 | 0<<11);  //offset
    return 0;
}
