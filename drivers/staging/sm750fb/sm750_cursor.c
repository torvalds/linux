#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include "sm750.h"
#include "sm750_cursor.h"



#define POKE32(addr, data) \
writel((data), cursor->mmio + (addr))

/* cursor control for voyager and 718/750*/
#define HWC_ADDRESS                         0x0
#define HWC_ADDRESS_ENABLE                  BIT(31)
#define HWC_ADDRESS_EXT                     BIT(27)
#define HWC_ADDRESS_CS                      BIT(26)
#define HWC_ADDRESS_ADDRESS_MASK            0x3ffffff

#define HWC_LOCATION                        0x4
#define HWC_LOCATION_TOP                    BIT(27)
#define HWC_LOCATION_Y_SHIFT                16
#define HWC_LOCATION_Y_MASK                 (0x7ff << 16)
#define HWC_LOCATION_LEFT                   BIT(11)
#define HWC_LOCATION_X_MASK                 0x7ff

#define HWC_COLOR_12                        0x8
#define HWC_COLOR_12_2_RGB565_SHIFT         16
#define HWC_COLOR_12_2_RGB565_MASK          (0xffff << 16)
#define HWC_COLOR_12_1_RGB565_MASK          0xffff

#define HWC_COLOR_3                         0xC
#define HWC_COLOR_3_RGB565_MASK             0xffff


/* hw_cursor_xxx works for voyager,718 and 750 */
void sm750_hw_cursor_enable(struct lynx_cursor *cursor)
{
	u32 reg;

	reg = (cursor->offset & HWC_ADDRESS_ADDRESS_MASK) | HWC_ADDRESS_ENABLE;
	POKE32(HWC_ADDRESS, reg);
}
void sm750_hw_cursor_disable(struct lynx_cursor *cursor)
{
	POKE32(HWC_ADDRESS, 0);
}

void sm750_hw_cursor_setSize(struct lynx_cursor *cursor,
						int w, int h)
{
	cursor->w = w;
	cursor->h = h;
}
void sm750_hw_cursor_setPos(struct lynx_cursor *cursor,
						int x, int y)
{
	u32 reg;

	reg = (((y << HWC_LOCATION_Y_SHIFT) & HWC_LOCATION_Y_MASK) |
		(x & HWC_LOCATION_X_MASK));
	POKE32(HWC_LOCATION, reg);
}
void sm750_hw_cursor_setColor(struct lynx_cursor *cursor,
						u32 fg, u32 bg)
{
	u32 reg = (fg << HWC_COLOR_12_2_RGB565_SHIFT) &
		HWC_COLOR_12_2_RGB565_MASK;

	POKE32(HWC_COLOR_12, reg | (bg & HWC_COLOR_12_1_RGB565_MASK));
	POKE32(HWC_COLOR_3, 0xffe0);
}

void sm750_hw_cursor_setData(struct lynx_cursor *cursor,
			u16 rop, const u8 *pcol, const u8 *pmsk)
{
	int i, j, count, pitch, offset;
	u8 color, mask, opr;
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

	for (i = 0; i < count; i++) {
		color = *pcol++;
		mask = *pmsk++;
		data = 0;

		for (j = 0; j < 8; j++) {
			if (mask & (0x80>>j)) {
				if (rop == ROP_XOR)
					opr = mask ^ color;
				else
					opr = mask & color;

				/* 2 stands for forecolor and 1 for backcolor */
				data |= ((opr & (0x80>>j))?2:1)<<(j*2);
			}
		}
		iowrite16(data, pbuffer);

		/* assume pitch is 1,2,4,8,...*/
		if ((i + 1) % pitch == 0) {
			/* need a return */
			pstart += offset;
			pbuffer = pstart;
		} else {
			pbuffer += sizeof(u16);
		}

	}


}


void sm750_hw_cursor_setData2(struct lynx_cursor *cursor,
			u16 rop, const u8 *pcol, const u8 *pmsk)
{
	int i, j, count, pitch, offset;
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

	for (i = 0; i < count; i++) {
		color = *pcol++;
		mask = *pmsk++;
		data = 0;

		for (j = 0; j < 8; j++) {
			if (mask & (1<<j))
				data |= ((color & (1<<j))?1:2)<<(j*2);
		}
		iowrite16(data, pbuffer);

		/* assume pitch is 1,2,4,8,...*/
		if (!(i&(pitch-1))) {
			/* need a return */
			pstart += offset;
			pbuffer = pstart;
		} else {
			pbuffer += sizeof(u16);
		}

	}
}
