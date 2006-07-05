/*
 * Copyright (C) 2005 Philips Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA, or http://www.gnu.org/licenses/gpl.html
*/

#define MAX_DUM_CHANNELS	64

#define RGB_MEM_WINDOW(x) (0x10000000 + (x)*0x00100000)

#define QCIF_OFFSET(x) (((x) == 0) ? 0x00000: ((x) == 1) ? 0x30000: -1)
#define CIF_OFFSET(x)  (((x) == 0) ? 0x00000: ((x) == 1) ? 0x60000: -1)

#define CTRL_SETDIRTY	 	(0x00000001)
#define CONF_DIRTYENABLE	(0x00000020)
#define CONF_SYNCENABLE		(0x00000004)

#define DIRTY_ENABLED(conf)	((conf) & 0x0020)
#define SYNC_ENABLED(conf) 	((conf) & 0x0004)

/* Display 1 & 2 Write Timing Configuration */
#define PNX4008_DUM_WT_CFG		0x00372000

/* Display 1 & 2 Read Timing Configuration */
#define PNX4008_DUM_RT_CFG		0x00003A47

/* DUM Transit State Timing Configuration */
#define PNX4008_DUM_T_CFG		0x1D	/* 29 HCLK cycles */

/* DUM Sync count clock divider */
#define PNX4008_DUM_CLK_DIV		0x02DD

/* Memory size for framebuffer, allocated through dma_alloc_writecombine().
 * Must be PAGE aligned
 */
#define FB_DMA_SIZE (PAGE_ALIGN(SZ_1M + PAGE_SIZE))

#define OFFSET_RGBBUFFER (0xB0000)
#define OFFSET_YUVBUFFER (0x00000)

#define YUVBUFFER (lcd_video_start + OFFSET_YUVBUFFER)
#define RGBBUFFER (lcd_video_start + OFFSET_RGBBUFFER)

#define CMDSTRING_BASEADDR	(0x00C000)	/* iram */
#define BYTES_PER_CMDSTRING	(0x80)
#define NR_OF_CMDSTRINGS	(64)

#define MAX_NR_PRESTRINGS (0x40)
#define MAX_NR_POSTSTRINGS (0x40)

/* various mask definitions */
#define DUM_CLK_ENABLE 0x01
#define DUM_CLK_DISABLE 0
#define DUM_DECODE_MASK 0x1FFFFFFF
#define DUM_CHANNEL_CFG_MASK 0x01FF
#define DUM_CHANNEL_CFG_SYNC_MASK 0xFFFE00FF
#define DUM_CHANNEL_CFG_SYNC_MASK_SET 0x0CA00

#define SDUM_RETURNVAL_BASE (0x500)

#define CONF_SYNC_OFF		(0x602)
#define CONF_SYNC_ON		(0x603)

#define CONF_DIRTYDETECTION_OFF	(0x600)
#define CONF_DIRTYDETECTION_ON	(0x601)

/* Set the corresponding bit. */
#define BIT(n) (0x1U << (n))

struct dumchannel_uf {
	int channelnr;
	u32 *dirty;
	u32 *source;
	u32 x_offset;
	u32 y_offset;
	u32 width;
	u32 height;
};

enum {
	FB_TYPE_YUV,
	FB_TYPE_RGB
};

struct cmdstring {
	int channelnr;
	uint16_t prestringlen;
	uint16_t poststringlen;
	uint16_t format;
	uint16_t reserved;
	uint16_t startaddr_low;
	uint16_t startaddr_high;
	uint16_t pixdatlen_low;
	uint16_t pixdatlen_high;
	u32 precmd[MAX_NR_PRESTRINGS];
	u32 postcmd[MAX_NR_POSTSTRINGS];

};

struct dumchannel {
	int channelnr;
	int dum_ch_min;
	int dum_ch_max;
	int dum_ch_conf;
	int dum_ch_stat;
	int dum_ch_ctrl;
};

int pnx4008_alloc_dum_channel(int dev_id);
int pnx4008_free_dum_channel(int channr, int dev_id);

int pnx4008_get_dum_channel_uf(struct dumchannel_uf *pChan_uf, int dev_id);
int pnx4008_put_dum_channel_uf(struct dumchannel_uf chan_uf, int dev_id);

int pnx4008_set_dum_channel_sync(int channr, int val, int dev_id);
int pnx4008_set_dum_channel_dirty_detect(int channr, int val, int dev_id);

int pnx4008_force_dum_update_channel(int channr, int dev_id);

int pnx4008_get_dum_channel_config(int channr, int dev_id);

int pnx4008_sdum_mmap(struct fb_info *info, struct vm_area_struct *vma, struct device *dev);
int pnx4008_set_dum_exit_notification(int dev_id);

int pnx4008_get_fb_addresses(int fb_type, void **virt_addr,
			     dma_addr_t * phys_addr, int *fb_length);
