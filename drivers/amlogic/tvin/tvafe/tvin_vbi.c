/*
 * TVAFE char device driver.
 *
 * Copyright (c) 2010 Frank zhao <frank.zhao@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/* Standard Linux headers */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
//#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <asm/io.h> /* for virt_to_phys */

/* Amlogic headers */
#include <mach/am_regs.h>
#include <mach/irqs.h>

/* Local include */
#include "tvafe_regs.h"
#include "tvin_vbi.h"

#define VBI_NAME               "vbi"
#define VBI_DRIVER_NAME        "vbi"
#define VBI_MODULE_NAME        "vbi"
#define VBI_DEVICE_NAME        "vbi"
#define VBI_CLASS_NAME         "vbi"

static dev_t vbi_id;
static struct class *vbi_clsp;
static struct vbi_dev_s *vbi_dev;

/******debug********/
static int vbi_dbg_en = 0;
MODULE_PARM_DESC(vbi_dbg_en, "\n vbi_dbg_en\n");
module_param(vbi_dbg_en, int, 0664);

static int capture_print_en = 0;
MODULE_PARM_DESC(capture_print_en, "\n capture_print_en\n");
module_param(capture_print_en, int, 0664);

static int data_print_en = 0;
MODULE_PARM_DESC(data_print_en, "\n data_print_en\n");
module_param(data_print_en, int, 0664);

static int bypass_slicer = 1;
MODULE_PARM_DESC(bypass_slicer, "\n bypass_slicer\n");
module_param(bypass_slicer, int, 0664);

#if 0  //not used now
static void vbi_enable_lines(unsigned short start_line, unsigned short end_line, unsigned char data_type)
{
    int i = 0;

    /*@todo*/
    if ((start_line < VBI_LINE_MIN)) {
        if (vbi_dbg_en)
            pr_info("[vbi..]: start line abnormal!!! line:%d \n", start_line);
        start_line = VBI_LINE_MIN;
    }

    if ((end_line > VBI_LINE_MAX)) {
        if (vbi_dbg_en)
            pr_info("[vbi..]: end line abnormal!!! line:%d \n", end_line);
        end_line = VBI_LINE_MAX;
    }

    for(i = VBI_LINE_MIN; i <= VBI_LINE_MAX ; i++) {
        if ((i < start_line) || (i > end_line)){
            WRITE_APB_REG((CVD2_VBI_DATA_TYPE_LINE7 + i - VBI_LINE_MIN), 0);
            continue;
        }
        if (i == VBI_LINE_MIN) {
            WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE6, data_type);
        } else {
            WRITE_APB_REG((CVD2_VBI_DATA_TYPE_LINE7 + i - VBI_LINE_MIN), data_type);
            if (vbi_dbg_en)
                pr_info("[vbi..]: set line:%d type to 0x%x \n", i, data_type);
        }
    }
}
#endif
#ifdef VBI_ON_M6TV
static void vbi_hw_init(struct vbi_dev_s *devp)
{
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE21, 0x11);
	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_START, 0x00000000);
	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_END, 0x00000025);
	WRITE_APB_REG(CVD2_VSYNC_TIME_CONSTANT, 0x0000004a);
	WRITE_APB_REG(CVD2_VBI_CC_START, 0x00000054);
	WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL, 0x00000015);
}
#else
static void vbi_hw_init(struct vbi_dev_s *devp)
{
	/* vbi memory setting */
	WRITE_APB_REG(ACD_REG_2F, devp->mem_start >> 3);
	WRITE_APB_REG_BITS(ACD_REG_21, ((devp->mem_size >> 3) - 1), 16, 16);
	WRITE_APB_REG_BITS(ACD_REG_21, 0, AML_VBI_START_ADDR_BIT, AML_VBI_START_ADDR_WID);

#if defined(VBI_CC_SUPPORT)
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE21, 0x11);
	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_START, 0x00000000);
	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_END, 0x00000025);
	WRITE_APB_REG(CVD2_VSYNC_TIME_CONSTANT, 0x0000004a);
	WRITE_APB_REG(ACD_REG_22, 0x82080000); // manuel reset vbi
	WRITE_APB_REG(ACD_REG_22, 0x04080000); // vbi reset release, vbi agent enable
	WRITE_APB_REG(CVD2_VBI_CC_START, 0x00000054);
	WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL, 0x00000015);
#endif

#if defined(VBI_TT_SUPPORT)
	//625B
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE6,  0x66);  // > /sys/class/amdbg/reg             //  0x6    0x6
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE7 , 0x66);  // > /sys/class/amdbg/reg             //  0x7    0x7
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE8 , 0x66);  // > /sys/class/amdbg/reg             //  0x8    0x8
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE9 , 0x66);  // > /sys/class/amdbg/reg             //  0x9    0x9
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE10, 0x66);  // > /sys/class/amdbg/reg             //  0xa    0xa
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE11, 0x66);  // > /sys/class/amdbg/reg             //  0xb    0xb
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE12, 0x66);  // > /sys/class/amdbg/reg             //  0xc    0xc
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE13, 0x66);  // > /sys/class/amdbg/reg             //  0xd    0xd
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE14, 0x66);  // > /sys/class/amdbg/reg             //  0xe    0xe
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE15, 0x66);  // > /sys/class/amdbg/reg             //  0xf    0xf
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE16, 0x66);  // > /sys/class/amdbg/reg             //  0x10   0x10
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE17, 0x66);  // > /sys/class/amdbg/reg             //  0x11   0x11
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE18, 0x66);  // > /sys/class/amdbg/reg             //  0x12   0x12
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE19, 0x66);  // > /sys/class/amdbg/reg             //  0x13   0x13
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE20, 0x66);  // > /sys/class/amdbg/reg             //  0x14   0x14
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE21, 0x66);  // > /sys/class/amdbg/reg             //  0x15   0x15
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE22, 0x66);  // > /sys/class/amdbg/reg             //  0x16   0x16
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE23, 0x66);  // > /sys/class/amdbg/reg             //  0x17   0x17
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE24, 0x66);  // > /sys/class/amdbg/reg             //  0x18   0x18
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE25, 0x66);  // > /sys/class/amdbg/reg             //  0x19   0x19
	WRITE_APB_REG(CVD2_VBI_DATA_TYPE_LINE26, 0x66);  // > /sys/class/amdbg/reg             //  0x20   0x20

	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_START, 0x00000000);
	WRITE_APB_REG(CVD2_VSYNC_VBI_LOCKOUT_END, 0x00000025);
	WRITE_APB_REG(CVD2_VSYNC_TIME_CONSTANT, 0x0000004a);
	WRITE_APB_REG(ACD_REG_22, 0x82080000); // manuel reset vbi
	WRITE_APB_REG(ACD_REG_22, 0x04080000); // vbi reset release, vbi agent enable

	WRITE_APB_REG(CVD2_VBI_TT_FRAME_CODE_CTL,0x27);  //echo wa 0x1841 27 > /sys/class/amdbg/reg

	WRITE_APB_REG(CVD2_VBI_TT_DTO_MSB,       0x0d);  //echo wa 0x185b 0x0d > /sys/class/amdbg/reg
	WRITE_APB_REG(CVD2_VBI_TT_DTO_LSB,       0xd6);  //echo wa 0x185c 0xd6 > /sys/class/amdbg/reg

	WRITE_APB_REG(CVD2_VBI_FRAME_START,      0xaa);  //echo wa 0x1861 0xaa > /sys/class/amdbg/reg
	WRITE_APB_REG(CVD2_VBI_TT_START,         0x64);  //echo wa 0x18f7 0x64 > /sys/class/amdbg/reg

	WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL,   0x14);  //echo wa 0x1840 0x14 > /sys/class/amdbg/reg
	WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL,   0x15);  //echo wa 0x1840 0x15 > /sys/class/amdbg/reg

#endif
	pr_info("[vbi..] %s: vbi hw init.\n", __func__);
}
#endif
static inline int odd_parity_check(int b)
{
	int chk, k;

	chk = (b & 1);
	for (k=0; k<7; k++){
		b >>= 1;
		chk ^= (b & 1);
	}
	return (chk & 1);
}

#ifdef VBI_BUF_DIV_EN
#define vbi_get_byte(rdptr, total_buffer, retbyte) \
{\
	retbyte = *(rdptr); \
	rdptr +=1; \
}
#else
#if 1//for new mem operation
#define vbi_get_byte(rdptr, total_buffer, devp, retbyte) \
{\
	retbyte = *(rdptr); \
	rdptr +=1; \
	if (rdptr > devp->pac_addr_end) \
		rdptr = devp->pac_addr_start; \
	total_buffer--; \
}

#else
#define vbi_get_byte(rdptr, total_buffer, devp, retbyte) \
{\
	retbyte = ioread8(rdptr); \
	rdptr +=1; \
	if (rdptr > devp->pac_addr_end) \
		rdptr = devp->pac_addr_start; \
	total_buffer--; \
}
#endif
#endif
#define vbi_skip_bytes(rdptr, total_buffer, devp, nbytes) \
{\
	rdptr += nbytes;\
	if (rdptr > devp->pac_addr_end)\
		rdptr = devp->pac_addr_start;\
	total_buffer -=nbytes;\
}

#define vbi_get_last_byte_addr(rdptr, devp, nbytes, retaddr)  \
{\
	uint i;\
	u8 *p = rdptr;\
	for (i=0; i<nbytes; i++){\
	p -= 1;\
	if (p < devp->pac_addr_start)\
		p = devp->pac_addr_end;\
	}\
	retaddr = p;\
}

ssize_t vbi_ringbuffer_free(struct vbi_ringbuffer_s *rbuf)
{
	ssize_t free;

	free = rbuf->pread - rbuf->pwrite;

	if (free <= 0) {
		free += rbuf->size;
		if (capture_print_en)
			pr_info("[vbi..] %s: pread: %6d pwrite: %6d\n", __func__,rbuf->pread , rbuf->pwrite);
	}
	return free-1;
}


ssize_t vbi_ringbuffer_write(struct vbi_ringbuffer_s *rbuf, const struct cc_data_s *buf, size_t len)
{
	size_t todo = len;
	size_t split;
	//unsigned int i;
	split = (rbuf->pwrite + len > rbuf->size) ? rbuf->size - rbuf->pwrite : 0;

	if (split > 0) {
		if (capture_print_en)
			pr_info("[vbi..] %s: pwrite: %6d\n", __func__, rbuf->pwrite);
		memcpy((char*)rbuf->data+rbuf->pwrite, (char*)buf, split);
		buf += split;
		todo -= split;
		rbuf->pwrite = 0;
	}
	memcpy((char*)rbuf->data+rbuf->pwrite, (char*)buf, todo);
	rbuf->pwrite = (rbuf->pwrite + todo) % rbuf->size;

	return len;
}


static int vbi_buffer_write(struct vbi_ringbuffer_s *buf,
                   const struct cc_data_s *src, size_t len)
{
	ssize_t free;

	if (!len) {
		if (capture_print_en)
			pr_info("[vbi..] %s: buffer len is zero\n", __func__);
		return 0;
	}
	if (!buf->data) {
		if (capture_print_en)
			pr_info("[vbi..] %s: buffer data pointer is zero\n", __func__);
		return 0;
	}

	free = vbi_ringbuffer_free(buf);
	if (len > free) {
		if (capture_print_en)
			pr_info("[vbi..] %s: buffer overflow ,len: %6d, free: %6d\n", __func__, len, free);
		return -EOVERFLOW;
	}

	return vbi_ringbuffer_write(buf, src, len);
}

#ifdef VBI_IRQ_EN
static irqreturn_t vbi_isr(int irq, void *dev_id)
{
	ulong flags;
	struct vbi_dev_s *devp = (struct vbi_dev_s *)dev_id;
	spin_lock_irqsave(&devp->vbi_isr_lock, flags);
	#if 0//no use
	if (devp->vs_delay > 0) {
	devp->vs_delay--;
	devp->current_pac_wptr = READ_APB_REG(ACD_REG_0C);//addr - (devp->mem_start);
	devp->last_pac_wptr = devp->current_pac_wptr;
	devp->pac_addr = devp->pac_addr_start + (devp->current_pac_wptr << 3) - devp->mem_start;  //last data package address
	pr_info("[vbi..]: vsync cnt:%d, wptr: %6d... ... ........\n", devp->vs_delay, devp->current_pac_wptr);
	devp->vbi_start = true;
	spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);
	return IRQ_HANDLED;
	}
	#else
	devp->vbi_start = true;
	#endif
	if (devp->vbi_start == false) {
		spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);
		return IRQ_HANDLED;
	}
	/* Mark tasklet as pending */
	tasklet_schedule(&vbi_dev->tsklt_slicer);

	spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);

	return IRQ_HANDLED;
}
#else
//kuka add begin
void vbi_timer_handler(unsigned long dev_id)
{
	ulong flags;
	struct vbi_dev_s *devp = (struct vbi_dev_s *)dev_id;
	devp->timer.expires = jiffies + VBI_TIMER_INTERVAL;
	add_timer(&devp->timer);

	spin_lock_irqsave(&devp->vbi_isr_lock, flags);
	#if 0//for no use
	if (devp->vs_delay > 0) {
		devp->vs_delay--;
		devp->current_pac_wptr = READ_APB_REG(ACD_REG_0C);//addr - (devp->mem_start);
		devp->last_pac_wptr = devp->current_pac_wptr;
		if(devp->current_pac_wptr != 0){
			devp->pac_addr = devp->pac_addr_start + (devp->current_pac_wptr << 3) - devp->mem_start;  //last data package address
		}
		pr_info("[vbi..]: vsync cnt:%d, wptr: %6d... ... ........\n", devp->vs_delay, devp->current_pac_wptr);
		devp->vbi_start = true;
		spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);
		return ;
	}
	#else
	devp->vbi_start = true;
	#endif
	if (devp->vbi_start == false) {
		spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);
		return ;
	}
	#ifdef VBI_ON_M6TV
	if(READ_APB_REG_BITS(CVD2_VBI_DATA_STATUS, 0,1)){
		*(devp->pac_addr) = READ_APB_REG_BITS(CVD2_VBI_CC_DATA1,CC_DATA0_BIT,CC_DATA0_WID);
		devp->pac_addr++;
		*(devp->pac_addr) = READ_APB_REG_BITS(CVD2_VBI_CC_DATA2,CC_DATA1_BIT,CC_DATA1_WID);
		WRITE_APB_REG_BITS(CVD2_VBI_CC_DATA2,1,2,1);
		WRITE_APB_REG_BITS(CVD2_VBI_CC_DATA2,0,2,1);
		if((devp->pac_addr+2)>= devp->pac_addr_end)
			devp->pac_addr = devp->pac_addr_start;
		else
			devp->pac_addr++;
		tasklet_schedule(&vbi_dev->tsklt_slicer);
	}
	#else
	/* Mark tasklet as pending */
	tasklet_schedule(&vbi_dev->tsklt_slicer);
	#endif
	spin_unlock_irqrestore(&devp->vbi_isr_lock, flags);

	return ;
}
//kuka add end
#endif
#ifdef VBI_ON_M6TV
static void vbi_slicer_task(unsigned long arg)
{
	struct vbi_dev_s *devp = (struct vbi_dev_s *)arg;
	struct cc_data_s sliced_data;
	if(data_print_en)
		pr_info("[vbi..]: read cc data:0x%x;0x%x ...\n",  *(devp->pac_addr-2),*(devp->pac_addr-1));
	sliced_data.b[0] = *(devp->pac_addr-2);
	sliced_data.b[1] = *(devp->pac_addr-1);
	if((devp->pac_addr+1)> devp->pac_addr_end)
		devp->pac_addr = devp->pac_addr_start;
	vbi_buffer_write(&devp->slicer->buffer, &sliced_data, 2);
	if(devp->slicer->buffer.pread != devp->slicer->buffer.pwrite){
		wake_up(&devp->slicer->buffer.queue);
	}
	return;
}
#else
static void vbi_slicer_task(unsigned long arg)
{
	struct vbi_dev_s *devp = (struct vbi_dev_s *)arg;
	unsigned char rbyte = 0;
	unsigned short pre_val = 0;
	int i = 0;
	int bytes_buffer = 0, bytes_buf_backup = 0;
	unsigned char *current_pac_addr, *wr_addr,*rptr,*sync_addr,*addr;
	unsigned char wr_burst = VBI_WRITE_BURST_BYTE;
	uint sync_code = (uint)-1;
	struct cc_data_s sliced_data;
	unsigned int len;
	if (devp->vbi_start == false)
		return;
	rptr = devp->pac_addr;  //backup package data pointer
	devp->current_pac_wptr = READ_APB_REG(ACD_REG_0C);
	if(devp->current_pac_wptr != 0)
		current_pac_addr = devp->pac_addr_start + (devp->current_pac_wptr<<3) - devp->mem_start;
	else
		current_pac_addr = devp->pac_addr_start;

	if(devp->last_pac_wptr != devp->current_pac_wptr) {
		//Go back 8 BEATS! Don't remove this if running with > 1 BEAT VBI burst.
		vbi_get_last_byte_addr(current_pac_addr, devp, wr_burst, wr_addr)
		//if (vbi_dbg_en)
		//pr_info("[vbi..]: Start rptr:0x%p, current_pac_addr:0x%p , wr_addr:0x%p...\n", rptr, current_pac_addr, wr_addr);
	} else {
		wr_addr = current_pac_addr;
		if(vbi_dbg_en)
			pr_info("[vbi..]: last_wptr == current_wptr:0x%x ...\n",  devp->current_pac_wptr);
	}
	devp->last_pac_wptr = devp->current_pac_wptr;  //backup current wptr

	//get tatal bytes
	bytes_buffer = (wr_addr >= devp->pac_addr)?
	(wr_addr - devp->pac_addr):((devp->mem_size + wr_addr) - devp->pac_addr);
	#ifdef VBI_BUF_DIV_EN
	memcpy(devp->pac_addr_end + 1,rptr,bytes_buffer);
	unsigned char *local_rptr = devp->pac_addr_end + 1;
	#endif
	//wordsInBuffer >>=3; /* in 8 bytes word*/
	if(bytes_buffer < VBI_WRITE_BURST_BYTE) {
		if (vbi_dbg_en)
			pr_info("[vbi..]: over range bytes_buffer:0x%x, wr_addr:0x%p,rptr:0x%p ... ...\n", bytes_buffer,wr_addr, rptr);
		goto err_exit;
	}
	bytes_buffer -= VBI_WRITE_BURST_BYTE;//???
	if (vbi_dbg_en)
		pr_info("[vbi..]: Start, bytes_buffer:%d, rptr:0x%p, wr_addr:0x%p, current_pac_addr:0x%p ...\n", bytes_buffer,rptr, wr_addr, current_pac_addr);
	while(bytes_buffer >= -3) {
	#ifdef VBI_BUF_DIV_EN
	vbi_get_byte(local_rptr, bytes_buffer, rbyte)
	vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
	#else
	vbi_get_byte(rptr, bytes_buffer, devp, rbyte)
	#endif
	sync_code <<= 8;
	sync_code |= rbyte;

	if((sync_code & 0xFFFFFF) != 0x00FFFF) {
		if((0 == rbyte) && (bytes_buffer < -2))
		{
			vbi_get_last_byte_addr(rptr, devp, 1, devp->pac_addr)
			//if(vbi_dbg_en)
			//pr_info("[vbi..]:  PRE1 rptr:%p=0x%x, pac_addr:%p ... ...\n", rptr,rbyte, devp->pac_addr);

			goto err_exit;
		} else if((0xFF == rbyte) && (bytes_buffer < -3)) {
			vbi_get_last_byte_addr(rptr, devp, 2, devp->pac_addr)
			//if (vbi_dbg_en)
			//pr_info("[vbi..]: PRE2 rptr:%p=0x%x, pac_addr:%p ... ...\n", rptr,rbyte, devp->pac_addr);

			goto err_exit;
		} else {
			//if (vbi_dbg_en && rbyte != 0)
			//pr_info("[vbi..]:  not find PRE,bytes_buffer:%d rptr:%p,val:%x, pac_addr:%p ... ...\n", bytes_buffer,rptr,rbyte, devp->pac_addr);
			continue;
		}
	}
	vbi_get_last_byte_addr(rptr, devp, 3, sync_addr)

	/* if we don't have packet ID and length byte in the buffer then wait for next time */
	if(bytes_buffer < -3) {
		devp->pac_addr = sync_addr;
		if (vbi_dbg_en)
			pr_info("[vbi..]: wait next vs pac; pac_addr:%p ... ...\n",  devp->pac_addr);
		goto err_exit;
	}
	#ifdef VBI_BUF_DIV_EN
	vbi_get_byte(local_rptr, bytes_buffer, rbyte)  // status
	vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
	#else
	vbi_get_byte(rptr, bytes_buffer, devp, rbyte)
	#endif
	sliced_data.vbi_type = (rbyte>>1) & 0x7;
	sliced_data.field_id = rbyte & 1;
#if defined(VBI_TT_SUPPORT)
	sliced_data->tt_sys = rbyte >> 5;
#endif
	if(sliced_data.vbi_type > MAX_PACKET_TYPE) {
		vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
		if (vbi_dbg_en)
		pr_info("[vbi..]: invalid pac type, go on; pac_addr:%p ... ...\n",  devp->pac_addr);
		continue;
	}
	#ifdef VBI_BUF_DIV_EN
	vbi_get_byte(local_rptr, bytes_buffer, rbyte)  // byte counter
	vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
	#else
	vbi_get_byte(rptr, bytes_buffer, devp, rbyte)
	#endif
	//check range by byte counter
	bytes_buf_backup = bytes_buffer;
	addr = rptr;
	vbi_skip_bytes(rptr, bytes_buffer, devp, rbyte)
	if(bytes_buffer < -3) {
		devp->pac_addr = sync_addr;
		if (vbi_dbg_en)
			pr_info("[vbi..]: wait next vs pac; pac_addr:%p ... ...\n",  devp->pac_addr);
		goto err_exit;
	}
	bytes_buffer = bytes_buf_backup;
	rptr = addr;
	sliced_data.nbytes = rbyte;
	#ifdef VBI_BUF_DIV_EN
	vbi_get_byte(local_rptr, bytes_buffer, rbyte)  // line number
	vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
	#else
	vbi_get_byte(rptr, bytes_buffer, devp, rbyte)
	#endif
	pre_val = (u16)rbyte;
	#ifdef VBI_BUF_DIV_EN
	vbi_get_byte(local_rptr, bytes_buffer, rbyte)
	vbi_skip_bytes(rptr, bytes_buffer, devp, 1)
	#else
	vbi_get_byte(rptr, bytes_buffer, devp, rbyte)
	#endif
	pre_val |= ((u16)rbyte & 0x3 )<< 8;
	sliced_data.line_num = pre_val;
	addr = rptr;

	if((rptr + sliced_data.nbytes) <= vbi_dev->pac_addr_end) {
		#if 1//for new mem operation
		memcpy(&(sliced_data.b[0]), rptr, sliced_data.nbytes);
		#else
		memcpy_fromio(&(sliced_data.b[0]), rptr, sliced_data.nbytes);
		#endif
		//rptr += buf[devp->sli_wr].nbytes;
		//if (vbi_dbg_en)
		//pr_info("[vbi..] %s: normal addr pac = 0x%p ; bytes: %2d ... ... \n", __func__, vbi_dev->pac_addr,(sliced_data.nbytes));
	}else{
		i = (vbi_dev->pac_addr_end - rptr + 1);
		#if 1//for new mem operation
		memcpy(&sliced_data.b[0], rptr, i);
		memcpy(&sliced_data.b[i], vbi_dev->pac_addr_start, (sliced_data.nbytes - i));
		#else
		memcpy_fromio(&sliced_data.b[0], rptr, i);
		//pr_info("[vbi..] %s: over range addr pac = 0x%p; cnt:%d ... ... \n", __func__, vbi_dev->pac_addr, i);
		//rptr = vbi_dev->pac_addr_start;
		memcpy_fromio(&sliced_data.b[i], vbi_dev->pac_addr_start, (sliced_data.nbytes - i));
		//rptr += (buf[devp->sli_wr].nbytes - i);
		//if (vbi_dbg_en)
		//    pr_info("[vbi..] %s: over range addr pac = 0x%p; cnt:%d ... ... \n", __func__, vbi_dev->pac_addr, (buf[devp->sli_wr].nbytes - i));
		#endif
	}

	vbi_skip_bytes(rptr, bytes_buffer, devp, sliced_data.nbytes)  //go to next package
	if (data_print_en) {
		printk("[vbi..]: cnt:%4d, line:%3x; ", bytes_buffer, sliced_data.line_num);
	    	for(i=0; i<sliced_data.nbytes ; i++) {
	        	printk("%2x ", sliced_data.b[i]);
		}
		printk("\n" );
	}
	//capture data to vbi buffer
	len = sizeof(struct cc_data_s);
	vbi_buffer_write(&devp->slicer->buffer, &sliced_data, len);
	if(data_print_en)
		pr_info("[vbi..]%s: sliced_data.field_id : %d; sliced_data.line_num : %d \n", __FUNCTION__, sliced_data.field_id,sliced_data.line_num);
	if(devp->slicer->buffer.pread != devp->slicer->buffer.pwrite){
		wake_up(&devp->slicer->buffer.queue);
	}
	}
	devp->pac_addr = rptr;
	err_exit:

	return;
}
#endif
void vbi_ringbuffer_flush(struct vbi_ringbuffer_s *rbuf)
{
	rbuf->pread = rbuf->pwrite;
	rbuf->error = 0;
}

static inline void vbi_slicer_state_set(struct vbi_dev_s *dev, int state)
{
	spin_lock_irq(&dev->lock);
	dev->slicer->state = state;
	spin_unlock_irq(&dev->lock);
}

static inline int vbi_slicer_reset(struct vbi_dev_s *dev)
{
	if (dev->slicer->state < VBI_STATE_SET)
		return 0;

	dev->slicer->type = VBI_TYPE_NULL;
	vbi_slicer_state_set(dev, VBI_STATE_ALLOCATED);
	return 0;
}

static int vbi_slicer_stop(struct vbi_slicer_s *vbi_slicer)
{
	if (vbi_slicer->state < VBI_STATE_GO)
		return 0;
	vbi_slicer->state = VBI_STATE_DONE;

	vbi_ringbuffer_flush(&vbi_slicer->buffer);

	return 0;
}

static int vbi_slicer_free(struct vbi_dev_s *vbi_dev,
                  struct vbi_slicer_s *vbi_slicer)
{
	mutex_lock(&vbi_dev->mutex);
	mutex_lock(&vbi_slicer->mutex);

	vbi_slicer_stop(vbi_slicer);
	vbi_slicer_reset(vbi_dev);

	if (vbi_slicer->buffer.data) {
		void *mem = vbi_slicer->buffer.data;

		spin_lock_irq(&vbi_dev->lock);
		vbi_slicer->buffer.data = NULL;
		spin_unlock_irq(&vbi_dev->lock);
		vfree(mem);
	}

	vbi_slicer_state_set(vbi_dev, VBI_STATE_FREE);
	wake_up(&vbi_slicer->buffer.queue);
	mutex_unlock(&vbi_slicer->mutex);
	mutex_unlock(&vbi_dev->mutex);

	return 0;
}

static void vbi_ringbuffer_init(struct vbi_ringbuffer_s *rbuf, void *data, size_t len)
{
	rbuf->pread = rbuf->pwrite = 0;
	if(data == NULL)
		rbuf->data = vmalloc(len);
	else
		rbuf->data = data;
	rbuf->size = len;
	rbuf->error = 0;

	init_waitqueue_head(&rbuf->queue);

	spin_lock_init(&(rbuf->lock));
}

void vbi_ringbuffer_reset(struct vbi_ringbuffer_s *rbuf)
{
	rbuf->pread = rbuf->pwrite = 0;
	rbuf->error = 0;
}

static int vbi_set_buffer_size(struct vbi_dev_s *dev,
                      unsigned long size)
{
	struct vbi_slicer_s *vbi_slicer = dev->slicer;
	struct vbi_ringbuffer_s *buf = &vbi_slicer->buffer;
	void *newmem;
	void *oldmem;

	if (buf->size == size) {
		pr_info("[vbi..] %s: buf->size == size \n", __func__);
		return 0;
	}
	if (!size) {
		pr_info("[vbi..] %s: buffer size is 0!!! \n", __func__);
		return -EINVAL;
	}
	if (vbi_slicer->state >= VBI_STATE_GO) {
		pr_info("[vbi..] %s: vbi_slicer busy!!! \n", __func__);
		return -EBUSY;
	}

	newmem = vmalloc(size);
	if (!newmem) {
		pr_info("[vbi..] %s: get memory error!!! \n", __func__);
		return -ENOMEM;
	}

	oldmem = buf->data;

	spin_lock_irq(&dev->lock);
	buf->data = newmem;
	buf->size = size;

	/* reset and not flush in case the buffer shrinks */
	vbi_ringbuffer_reset(buf);
	spin_unlock_irq(&dev->lock);

	vfree(oldmem);

	return 0;
}

static int vbi_slicer_start(struct vbi_dev_s *dev)
{
	struct vbi_slicer_s *vbi_slicer = dev->slicer;
	void *mem;

	if (vbi_slicer->state < VBI_STATE_SET)
		return -EINVAL;

	if (vbi_slicer->state >= VBI_STATE_GO)
		vbi_slicer_stop(vbi_slicer);

	if (!vbi_slicer->buffer.data) {
		mem = vmalloc(vbi_slicer->buffer.size);
		if (!mem) {
			pr_info("[vbi..] %s: get memory error!!! \n", __func__);
			return -ENOMEM;
		}
		spin_lock_irq(&dev->lock);
		vbi_slicer->buffer.data = mem;
		spin_unlock_irq(&dev->lock);
	}

	vbi_ringbuffer_flush(&vbi_slicer->buffer);

	vbi_slicer_state_set(dev, VBI_STATE_GO);

	return 0;
}

static int vbi_slicer_set(struct vbi_dev_s *vbi_dev,
                 struct vbi_slicer_s *vbi_slicer)
{
	vbi_slicer_stop(vbi_slicer);

	vbi_slicer_state_set(vbi_dev, VBI_STATE_SET);

	return 0;//vbi_slicer_start(vbi_dev);
}

ssize_t vbi_ringbuffer_avail(struct vbi_ringbuffer_s *rbuf)
{
	ssize_t avail;

	avail = rbuf->pwrite - rbuf->pread;
	if (avail < 0)
		avail += rbuf->size;
	return avail;
}

int vbi_ringbuffer_empty(struct vbi_ringbuffer_s *rbuf)
{
	return (rbuf->pread == rbuf->pwrite);
}

ssize_t vbi_ringbuffer_read_user(struct vbi_ringbuffer_s *rbuf, u8 __user *buf, size_t len)
{
	size_t todo = len;
	size_t split;
	split = (rbuf->pread + len > rbuf->size) ? rbuf->size - rbuf->pread : 0;
	if (split > 0) {
		if (copy_to_user(buf, (char*)rbuf->data+rbuf->pread, split))
			return -EFAULT;
		buf += split;
		todo -= split;
		rbuf->pread = 0;
	}
	if (copy_to_user(buf, (char*)rbuf->data+rbuf->pread, todo))
		return -EFAULT;
	rbuf->pread = (rbuf->pread + todo) % rbuf->size;

	return len;
}

static ssize_t vbi_buffer_read(struct vbi_ringbuffer_s *src,
                      int non_blocking, char __user *buf,
                      size_t count, loff_t *ppos)
{
	size_t todo;
	ssize_t avail;
	ssize_t ret = 0;

	if (!src->data) {
		pr_info("[vbi..] %s: data null \n", __func__);
		return 0;
	}

	if (src->error) {
		ret = src->error;
		vbi_ringbuffer_flush(src);
		pr_info("[vbi..] %s: data error \n", __func__);
		return ret;
	}

	for (todo = count; todo > 0; todo -= ret) {
	if (non_blocking && vbi_ringbuffer_empty(src)) {
		ret = -EWOULDBLOCK;
		pr_info("[vbi..] %s: buffer empty, non_blocking: 0x%x, empty?: %d \n", __func__, non_blocking, vbi_ringbuffer_empty(src));
		break;
	}

	ret = wait_event_interruptible(src->queue, !vbi_ringbuffer_empty(src) || (src->error != 0));
	if(vbi_dbg_en)
		pr_info("[vbi...]%s:src->pread = %d;src->pwrite = %d\n",__FUNCTION__,src->pread,src->pwrite);
	if (ret < 0) {
		pr_info("[vbi..] %s: read wait_event_interruptible error \n", __func__);
		break;
	}

	if (src->error) {
		ret = src->error;
		vbi_ringbuffer_flush(src);
		pr_info("[vbi..] %s: read buffer error \n", __func__);
		break;
	}

	avail = vbi_ringbuffer_avail(src);
	if (avail > todo)
		avail = todo;

	ret = vbi_ringbuffer_read_user(src, buf, avail);
	if (ret < 0) {
		pr_info("[vbi..] %s: vbi_ringbuffer_read_user error \n", __func__);
		break;
	}
	buf += ret;
	}
	if ((count - todo) <= 0)
		pr_info("[vbi..] %s: read error!! counter: %x or %x \n", __func__,(count - todo) , ret);

	return (count - todo) ? (count - todo) : ret;
}

static int vbi_read(struct file *file, char __user *buf, size_t count,
        loff_t *ppos)
{
	struct vbi_dev_s *vbi_dev = file->private_data;
	struct vbi_slicer_s *vbi_slicer = vbi_dev->slicer;
	int ret;

	if (mutex_lock_interruptible(&vbi_slicer->mutex)) {
		pr_info("[vbi..] %s: slicer mutex error \n", __func__);
		return -ERESTARTSYS;
	}

	ret = vbi_buffer_read(&vbi_slicer->buffer,
	file->f_flags & O_NONBLOCK,
	buf, count, ppos);

	mutex_unlock(&vbi_slicer->mutex);

	return ret;
}

static int vbi_open(struct inode *inode, struct file *file)
{
	struct vbi_dev_s *vbi_dev;// = file->private_data;

	pr_info("[vbi..] %s: open start. \n", __func__);

	vbi_dev = container_of(inode->i_cdev, struct vbi_dev_s, cdev);
	file->private_data = vbi_dev;

	if (mutex_lock_interruptible(&vbi_dev->mutex)) {
		pr_info("[vbi..] %s: dev mutex_lock_interruptible error \n", __func__);
		return -ERESTARTSYS;
	}

	mutex_init(&vbi_dev->slicer->mutex);

	vbi_ringbuffer_init(&vbi_dev->slicer->buffer, NULL, VBI_DEFAULT_BUFFER_SIZE);  //set default buffer size--8KByte
	vbi_dev->slicer->type = 0;
	vbi_slicer_state_set(vbi_dev, VBI_STATE_ALLOCATED);

	mutex_unlock(&vbi_dev->mutex);

	pr_info("[vbi..]%s: open device ok. \n", __func__);

	return 0;
}

static int vbi_release(struct inode *inode, struct file *file)
{
	struct vbi_dev_s *vbi_dev = file->private_data;
	struct vbi_slicer_s *vbi_slicer = vbi_dev->slicer;
	int ret;

	ret = vbi_slicer_free(vbi_dev, vbi_slicer);
	#ifdef 	VBI_IRQ_EN
	/* free irq */
	free_irq(vbi_dev->vs_irq, (void *)vbi_dev);
	#else
	del_timer_sync(&vbi_dev->timer);
	#endif
	//WRITE_APB_REG(ACD_REG_22, 0x82080000); // manuel reset vbi
	WRITE_APB_REG(ACD_REG_22, 0x06080000); // vbi reset release, vbi agent enable
	//WRITE_APB_REG(CVD2_VBI_CC_START, 0x00000054);
	WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL, 0x00000014);
	pr_info("[vbi..] %s: disable vbi function \n", __func__);

	pr_info("[vbi..]%s: device release OK. \n", __func__);

	return ret;
}

//static int vbi_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
static long vbi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	unsigned long buffer_size_t;

	struct vbi_dev_s *vbi_dev = file->private_data;
	struct vbi_slicer_s *vbi_slicer = vbi_dev->slicer;

	if (mutex_lock_interruptible(&vbi_dev->mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case VBI_IOC_START:
		if (mutex_lock_interruptible(&vbi_slicer->mutex)) {
			mutex_unlock(&vbi_dev->mutex);
			pr_info("[vbi..] %s: slicer mutex error \n", __func__);
			return -ERESTARTSYS;
		}
		vbi_hw_init(vbi_dev);
		#ifdef VBI_IRQ_EN
		vbi_dev->vs_irq = INT_VDIN0_VSYNC;
		spin_lock_init(&vbi_dev->vbi_isr_lock);//vbi_dev->vbi_isr_lock = SPIN_LOCK_UNLOCKED;
		/* request irq */
		snprintf(vbi_dev->irq_name, sizeof(vbi_dev->irq_name),  "vbi%d-irq", vbi_dev->index);
		ret = request_irq(vbi_dev->vs_irq, vbi_isr, IRQF_SHARED, vbi_dev->irq_name, (void *)vbi_dev);
		#else
		spin_lock_init(&vbi_dev->vbi_isr_lock);
		init_timer(&vbi_dev->timer);
		vbi_dev->timer.data = (unsigned long)vbi_dev;
		vbi_dev->timer.function = vbi_timer_handler;
		vbi_dev->timer.expires = jiffies + (VBI_TIMER_INTERVAL);
		add_timer(&vbi_dev->timer);
		#endif

		if(ret < 0) {
			pr_err("[vbi..] %s: request_irq fail \n", __func__);
		}

		if (vbi_slicer->state < VBI_STATE_SET)
			ret = -EINVAL;
		else
			ret = vbi_slicer_start(vbi_dev);

		vbi_dev->vbi_start = false;  //enable data capture function
		vbi_dev->vs_delay = 4;

		mutex_unlock(&vbi_slicer->mutex);
		pr_info("[vbi..] %s: start slicer state:%d \n", __func__, vbi_slicer->state);
		break;

	case VBI_IOC_STOP:
		if (mutex_lock_interruptible(&vbi_slicer->mutex)) {
		mutex_unlock(&vbi_dev->mutex);
		pr_info("[vbi..] %s: slicer mutex error \n", __func__);
		return -ERESTARTSYS;
		}
		ret = vbi_slicer_stop(vbi_slicer);
		#if 0//avoid double free_irq or del_timer_sync
		#ifdef 	VBI_IRQ_EN
		/* free irq */
		free_irq(vbi_dev->vs_irq, (void *)vbi_dev);
		#else
		del_timer_sync(&vbi_dev->timer);
		#endif
		#endif
		//WRITE_APB_REG(ACD_REG_22, 0x82080000); // manuel reset vbi
		WRITE_APB_REG(ACD_REG_22, 0x06080000); // vbi reset release, vbi agent enable
		//WRITE_APB_REG(CVD2_VBI_CC_START, 0x00000054);
		WRITE_APB_REG(CVD2_VBI_FRAME_CODE_CTL, 0x00000014);
		pr_info("[vbi..] %s: disable vbi function \n", __func__);

		mutex_unlock(&vbi_slicer->mutex);
		pr_info("[vbi..] %s: stop slicer state:%d \n", __func__, vbi_slicer->state);
		break;

	case VBI_IOC_SET_FILTER:
		if (mutex_lock_interruptible(&vbi_slicer->mutex)) {
			mutex_unlock(&vbi_dev->mutex);
			pr_info("[vbi..] %s: slicer mutex error \n", __func__);
			return -ERESTARTSYS;
		}
		if (copy_from_user(&vbi_slicer->type, argp, sizeof(int))){
			ret = -EFAULT;
			break;
		}
		ret = vbi_slicer_set(vbi_dev, vbi_slicer);
		mutex_unlock(&vbi_slicer->mutex);
		pr_info("[vbi..] %s: set slicer to %d ,state:%d\n", __func__, vbi_slicer->type, vbi_slicer->state);
		break;

	case VBI_IOC_S_BUF_SIZE:
		if (mutex_lock_interruptible(&vbi_slicer->mutex)) {
			mutex_unlock(&vbi_dev->mutex);
			pr_info("[vbi..] %s: slicer mutex error \n", __func__);
			return -ERESTARTSYS;
		}
		if (copy_from_user(&buffer_size_t, argp, sizeof(int))){
			ret = -EFAULT;
			break;
		}
		ret = vbi_set_buffer_size(vbi_dev, buffer_size_t);
		mutex_unlock(&vbi_slicer->mutex);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&vbi_dev->mutex);

	return ret;
}

/* vbi package data capture */
static int vbi_mmap(struct file *file, struct vm_area_struct * vma)
{
	unsigned long start, len, off;
	unsigned long pfn, size;
	vbi_dev_t *devp = file->private_data;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT)) {
		return -EINVAL;
	}

	/* capture the vbi data  */
	start = ((devp->mem_start)) & PAGE_MASK;
	pr_info("[vbi..]: cat start add:0x%x ,size:%x \n", ((devp->mem_start)), (devp->mem_size));
	len = PAGE_ALIGN((start & ~PAGE_MASK) + (devp->mem_size));

	off = vma->vm_pgoff << PAGE_SHIFT;

	pr_info("[vbi..]: vend:%lx,vm_start:%lx, len:%lx  \n", (vma->vm_end), vma->vm_start, len);
	//if ((vma->vm_end - vma->vm_start + off) > len) {
	//    pr_info("[vbi..]: error!! vend:%p,vm_start:%p, len:%x  \n", (vma->vm_end), vma->vm_start, len);
	//    return -EINVAL;
	//}
	//pr_info("[vbi..]: cat mem_size add:0x%x \n", ((devp->mem_size) << 3));
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO | VM_RESERVED;

	size = vma->vm_end - vma->vm_start;
	pfn  = off >> PAGE_SHIFT;

	if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
	return -EAGAIN;

	return 0;
}

static unsigned int vbi_poll(struct file *file, poll_table *wait)
{
	vbi_dev_t *devp = file->private_data;
	struct vbi_slicer_s *vbi_slicer = devp->slicer;
	unsigned int mask = 0;

	if (!vbi_slicer) {
		pr_info("[vbi..] %s: slicer null \n", __func__);
		return -EINVAL;
	}
	poll_wait(file, &vbi_slicer->buffer.queue, wait);

	if (vbi_slicer->state != VBI_STATE_GO &&
		vbi_slicer->state != VBI_STATE_DONE &&
		vbi_slicer->state != VBI_STATE_TIMEDOUT)
		return 0;

	if (vbi_slicer->buffer.error)
		mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

	if (!vbi_ringbuffer_empty(&vbi_slicer->buffer))
		mask |= (POLLIN | POLLRDNORM | POLLPRI);

	return mask;
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations vbi_fops = {
	.owner   = THIS_MODULE,         /* Owner */
	.open    = vbi_open,            /* Open method */
	.release = vbi_release,         /* Release method */
	.unlocked_ioctl   = vbi_ioctl,           /* Ioctl method */
	.mmap    = vbi_mmap,
	.read    = vbi_read,
	.poll    = vbi_poll,
	/* ... */
};

static int vbi_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	ret = alloc_chrdev_region(&vbi_id, 0, 1, VBI_NAME);
	if (ret < 0) {
		pr_err("[vbi..]: failed to allocate major number\n");
		return 0;
	}

	vbi_clsp = class_create(THIS_MODULE, VBI_NAME);
	if (IS_ERR(vbi_clsp)){
		pr_err("[vbi..]: can't get vbi_clsp\n");
		unregister_chrdev_region(vbi_id, 1);
		return PTR_ERR(vbi_clsp);
	}

	/* allocate memory for the per-device structure */
	vbi_dev = kmalloc(sizeof(struct vbi_dev_s), GFP_KERNEL);
	if (!vbi_dev){
		pr_err("[vbi..]: failed to allocate memory for vbi device\n");
		return -ENOMEM;
	}

	/* connect the file operations with cdev */
	cdev_init(&vbi_dev->cdev, &vbi_fops);
	vbi_dev->cdev.owner = THIS_MODULE;
	/* connect the major/minor number to the cdev */
	ret = cdev_add(&vbi_dev->cdev, vbi_id, 1);
	if (ret) {
		pr_err("[vbi..]: failed to add device\n");
		/* @todo do with error */
		return ret;
	}
	/* create /dev nodes */
	vbi_dev->dev = device_create(vbi_clsp, NULL, MKDEV(MAJOR(vbi_id), 0), NULL, "%s", VBI_NAME);
	if (IS_ERR(vbi_dev->dev)) {
		pr_err("[vbi..]: failed to create device node\n");
		cdev_del(&vbi_dev->cdev);
		kfree(vbi_dev);
		return PTR_ERR(vbi_dev->dev);
	}

	/* get device memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("[vbi..]: can't get memory resource\n");
		return -EFAULT;
	}
	vbi_dev->mem_start = res->start;
	#if 1
	vbi_dev->mem_size = res->end - res->start + 1;
	#else
	vbi_dev->mem_size = VBI_MEM_SIZE;
	#endif
	pr_info("[vbi..]: start_addr is:0x%x, size is:0x%x \n", vbi_dev->mem_start, vbi_dev->mem_size);

	/* remap the package vbi hardware address for our conversion */
	vbi_dev->pac_addr_start = ioremap_nocache(vbi_dev->mem_start, vbi_dev->mem_size);
	memset(vbi_dev->pac_addr_start,0,vbi_dev->mem_size);//kuku add
	#ifdef VBI_BUF_DIV_EN
	vbi_dev->mem_size = vbi_dev->mem_size/2;
	#endif
	vbi_dev->pac_addr_end = vbi_dev->pac_addr_start + vbi_dev->mem_size - 1;
	if (vbi_dev->pac_addr_start == NULL)
		pr_err("[vbi..]: ioremap error!!! \n");
	else
		pr_info("[vbi..]: vbi_dev->pac_addr_start=0x%p, end:0x%p, size:0x%x .......  \n", vbi_dev->pac_addr_start, vbi_dev->pac_addr_end, vbi_dev->mem_size);
	vbi_dev->pac_addr = vbi_dev->pac_addr_start;

	mutex_init(&vbi_dev->mutex);
	spin_lock_init(&vbi_dev->lock);

	/* Initialize tasklet */
	tasklet_init(&vbi_dev->tsklt_slicer, vbi_slicer_task, (unsigned long)vbi_dev);

	vbi_dev->vbi_start = false;
	vbi_dev->vs_delay = 4;

	vbi_dev->slicer = vmalloc(sizeof(struct vbi_slicer_s));
	if (!vbi_dev->slicer) {
		pr_err("[vbi..]: vmalloc error!!! \n");
		return -ENOMEM;
	}
	vbi_dev->slicer->buffer.data = NULL;
	vbi_dev->slicer->state = VBI_STATE_FREE;

	pr_info("[vbi..]: driver probe ok\n");

	return 0;
}

static int vbi_remove(struct platform_device *pdev)
{
	tasklet_kill(&vbi_dev->tsklt_slicer);

	if (vbi_dev->pac_addr_start)
		iounmap(vbi_dev->pac_addr_start);
	vfree(vbi_dev->slicer);
	device_destroy(vbi_clsp, MKDEV(MAJOR(vbi_id), 0));
	cdev_del(&vbi_dev->cdev);
	kfree(vbi_dev);
	class_destroy(vbi_clsp);
	unregister_chrdev_region(vbi_id, 0);

	pr_info("[vbi..] : driver removed ok.\n");

	return 0;
}

static struct platform_driver vbi_driver = {
	.probe      = vbi_probe,
	.remove     = vbi_remove,
	.driver     = {
	.name   = VBI_DRIVER_NAME,
	}
};

static int __init vbi_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&vbi_driver);
	if (ret != 0) {
		pr_err("[vbi..] failed to register vbi module, error %d\n", ret);
		return -ENODEV;
	}

	return ret;
}

static void __exit vbi_exit(void)
{
	platform_driver_unregister(&vbi_driver);
}

module_init(vbi_init);
module_exit(vbi_exit);

MODULE_DESCRIPTION("AMLOGIC vbi driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("frank  <frank.zhao@amlogic.com>");

