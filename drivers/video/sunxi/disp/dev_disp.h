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

#ifndef __DEV_DISP_H__
#define __DEV_DISP_H__

struct info_mm {
	void *info_base;	/* Virtual address */
	unsigned long mem_start;	/* Start of frame buffer mem */
	/* (physical address) */
	__u32 mem_len;		/* Length of frame buffer mem */
};

typedef struct {
	__u32 mid;
	__u32 used;
	__u32 status;
	__u32 exit_mode;	/* 0:clean all  1:disable interrupt */
	__bool b_cache[2];
	__bool b_lcd_open[2];
} __disp_drv_t;

struct alloc_struct_t {
	__u32 address; /* Application memory address */
	__u32 size; /* The size of the allocated memory */
	__u32 o_size; /* User application memory size */
	struct alloc_struct_t *next;
};

int disp_open(struct inode *inode, struct file *file);
int disp_release(struct inode *inode, struct file *file);
ssize_t disp_read(struct file *file, char __user *buf, size_t count,
		  loff_t *ppos);
ssize_t disp_write(struct file *file, const char __user *buf, size_t count,
		   loff_t *ppos);
int disp_mmap(struct file *file, struct vm_area_struct *vma);
long disp_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

__s32 disp_create_heap(__u32 pHeapHead, __u32 nHeapSize);
void *disp_malloc(__u32 num_bytes);
void disp_free(void *p);

extern __s32 DRV_disp_int_process(__u32 sel);

extern __s32 DRV_DISP_Init(void);
extern __s32 DRV_DISP_Exit(void);

extern __disp_drv_t g_disp_drv;

extern void hdmi_edid_received(unsigned char *edid, int block);
extern __s32 DRV_lcd_open(__u32 sel);
extern __s32 DRV_lcd_close(__u32 sel);

__s32 disp_set_hdmi_func(__disp_hdmi_func *func);
__s32 disp_get_pll_freq(__u32 pclk, __u32 *pll_freq,  __u32 *pll_2x);

#endif
