#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/errno.h>
#include<linux/string.h>
#include<linux/mm.h>
#include<linux/slab.h>
#include<linux/delay.h>
#include<linux/fb.h>
#include<linux/ioport.h>
#include<linux/init.h>
#include<linux/pci.h>
#include<linux/vmalloc.h>
#include<linux/pagemap.h>
#include <linux/console.h>
#include<linux/platform_device.h>
#include<linux/screen_info.h>

#include "sm750.h"
#include "sm750_help.h"
#include "sm750_cursor.h"


#define PEEK32(addr) \
readl(cursor->mmio + (addr))

#define POKE32(addr,data) \
writel((data),cursor->mmio + (addr))

/* cursor control for voyager and 718/750*/
#define HWC_ADDRESS                         0x0
#define HWC_ADDRESS_ENABLE                  31:31
#define HWC_ADDRESS_ENABLE_DISABLE          0
#define HWC_ADDRESS_ENABLE_ENABLE           1
#define HWC_ADDRESS_EXT                     27:27
#define HWC_ADDRESS_EXT_LOCAL               0
#define HWC_ADDRESS_EXT_EXTERNAL            1
#define HWC_ADDRESS_CS                      26:26
#define HWC_ADDRESS_CS_0                    0
#define HWC_ADDRESS_CS_1                    1
#define HWC_ADDRESS_ADDRESS                 25:0

#define HWC_LOCATION                        0x4
#define HWC_LOCATION_TOP                    27:27
#define HWC_LOCATION_TOP_INSIDE             0
#define HWC_LOCATION_TOP_OUTSIDE            1
#define HWC_LOCATION_Y                      26:16
#define HWC_LOCATION_LEFT                   11:11
#define HWC_LOCATION_LEFT_INSIDE            0
#define HWC_LOCATION_LEFT_OUTSIDE           1
#define HWC_LOCATION_X                      10:0

#define HWC_COLOR_12                        0x8
#define HWC_COLOR_12_2_RGB565               31:16
#define HWC_COLOR_12_1_RGB565               15:0

#define HWC_COLOR_3                         0xC
#define HWC_COLOR_3_RGB565                  15:0


/* hw_cursor_xxx works for voyager,718 and 750 */
void hw_cursor_enable(struct lynx_cursor * cursor)
{
	u32 reg;
	reg = FIELD_VALUE(0,HWC_ADDRESS,ADDRESS,cursor->offset)|
			FIELD_SET(0,HWC_ADDRESS,EXT,LOCAL)|
			FIELD_SET(0,HWC_ADDRESS,ENABLE,ENABLE);
	POKE32(HWC_ADDRESS,reg);
}
void hw_cursor_disable(struct lynx_cursor * cursor)
{
	POKE32(HWC_ADDRESS,0);
}

void hw_cursor_setSize(struct lynx_cursor * cursor,
						int w,int h)
{
	cursor->w = w;
	cursor->h = h;
}
void hw_cursor_setPos(struct lynx_cursor * cursor,
						int x,int y)
{
	u32 reg;
	reg = FIELD_VALUE(0,HWC_LOCATION,Y,y)|
			FIELD_VALUE(0,HWC_LOCATION,X,x);
	POKE32(HWC_LOCATION,reg);
}
void hw_cursor_setColor(struct lynx_cursor * cursor,
						u32 fg,u32 bg)
{
	POKE32(HWC_COLOR_12,(fg<<16)|(bg&0xffff));
	POKE32(HWC_COLOR_3,0xffe0);
}

void hw_cursor_setData(struct lynx_cursor * cursor,
			u16 rop,const u8* pcol,const u8* pmsk)
{
	int i,j,count,pitch,offset;
	u8 color,mask,opr;
	u16 data;
	void __iomem *pbuffer, *pstart;

	/*  in byte*/
	pitch = cursor->w >> 3;

	/* in byte	*/
	count = pitch * cursor->h;

	/* in byte */
	offset = cursor->maxW * 2 / 8;

	data = 0;
	pstart = cursor->vstart;
	pbuffer = pstart;

/*
	if(odd &1){
		hw_cursor_setData2(cursor,rop,pcol,pmsk);
	}
	odd++;
	if(odd > 0xfffffff0)
		odd=0;
*/

	for(i=0;i<count;i++)
	{
		color = *pcol++;
		mask = *pmsk++;
		data = 0;

		/* either method below works well,
		 * but method 2 shows no lag
		 * and method 1 seems a bit wrong*/
#if 0
		if(rop == ROP_XOR)
			opr = mask ^ color;
		else
			opr = mask & color;

		for(j=0;j<8;j++)
		{

			if(opr & (0x80 >> j))
			{	//use fg color,id = 2
				data |= 2 << (j*2);
			}else{
				//use bg color,id = 1
				data |= 1 << (j*2);
			}
		}
#else
		for(j=0;j<8;j++){
			if(mask & (0x80>>j)){
				if(rop == ROP_XOR)
					opr = mask ^ color;
				else
					opr = mask & color;

				/* 2 stands for forecolor and 1 for backcolor */
				data |= ((opr & (0x80>>j))?2:1)<<(j*2);
			}
		}
#endif
		iowrite16(data, pbuffer);

		/* assume pitch is 1,2,4,8,...*/
#if 0
		if(!((i+1)&(pitch-1)))   /* below line equal to is line */
#else
		if((i+1) % pitch == 0)
#endif
		{
			/* need a return */
			pstart += offset;
			pbuffer = pstart;
		}else{
			pbuffer += sizeof(u16);
		}

	}


}


void hw_cursor_setData2(struct lynx_cursor * cursor,
			u16 rop,const u8* pcol,const u8* pmsk)
{
	int i,j,count,pitch,offset;
	u8 color, mask;
	u16 data;
	void __iomem *pbuffer, *pstart;

	/*  in byte*/
	pitch = cursor->w >> 3;

	/* in byte	*/
	count = pitch * cursor->h;

	/* in byte */
	offset = cursor->maxW * 2 / 8;

	data = 0;
	pstart = cursor->vstart;
	pbuffer = pstart;

	for(i=0;i<count;i++)
	{
		color = *pcol++;
		mask = *pmsk++;
		data = 0;

		/* either method below works well, but method 2 shows no lag */
#if 0
		if(rop == ROP_XOR)
			opr = mask ^ color;
		else
			opr = mask & color;

		for(j=0;j<8;j++)
		{

			if(opr & (0x80 >> j))
			{	//use fg color,id = 2
				data |= 2 << (j*2);
			}else{
				//use bg color,id = 1
				data |= 1 << (j*2);
			}
		}
#else
		for(j=0;j<8;j++){
			if(mask & (1<<j))
				data |= ((color & (1<<j))?1:2)<<(j*2);
		}
#endif
		iowrite16(data, pbuffer);

		/* assume pitch is 1,2,4,8,...*/
		if(!(i&(pitch-1)))
		//if((i+1) % pitch == 0)
		{
			/* need a return */
			pstart += offset;
			pbuffer = pstart;
		}else{
			pbuffer += sizeof(u16);
		}

	}
}
