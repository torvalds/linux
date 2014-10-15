/*
 * TVHDMI char device driver for M6TV chip of AMLOGIC INC.
 *
 * Copyright (C) 2012 AMLOGIC, INC. All Rights Reserved.
 * Author: Rain Zhang <rain.zhang@amlogic.com>
 * Author: Xiaofei Zhu <xiaofei.zhu@amlogic.com>
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
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

/* Amlogic headers */
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/tvin/tvin.h>
#include <linux/amlogic/amports/canvas.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amports/vframe.h>

/* Local include */
#include "../tvin_frontend.h"
#include "hdmirx_drv.h"           /* For user used */
#include "hdmi_rx_reg.h"
#if CEC_FUNC_ENABLE
#include "hdmirx_cec.h"
#endif


#define TVHDMI_NAME               "hdmirx"
#define TVHDMI_DRIVER_NAME        "hdmirx"
#define TVHDMI_MODULE_NAME        "hdmirx"
#define TVHDMI_DEVICE_NAME        "hdmirx"
#define TVHDMI_CLASS_NAME         "hdmirx"
#define INIT_FLAG_NOT_LOAD 0x80
#define HDMI_DE_REPEAT_DONE_FLAG 0xF0


/* 50ms timer for hdmirx main loop (HDMI_STATE_CHECK_FREQ is 20) */
#define TIMER_STATE_CHECK              (1*HZ/HDMI_STATE_CHECK_FREQ)


static unsigned char init_flag = 0;
static dev_t	hdmirx_devno;
static struct class	*hdmirx_clsp;
extern void clk_init(void);
static int open_flage = 0;
struct hdmirx_dev_s *devp_hdmirx_suspend;
extern void clk_off(void);
extern void hdmirx_wr_top (unsigned long addr, unsigned long data);
int resume_flag = 0;
static int force_colorspace = 0;
int cur_colorspace = 0xff;
static int hdmi_yuv444_enable = 1;
int hdmirx_de_repeat_enable = 1;

MODULE_PARM_DESC(resume_flag, "\n resume_flag \n");
module_param(resume_flag, int, 0664);

MODULE_PARM_DESC(force_colorspace, "\n force_colorspace \n");
module_param(force_colorspace, int, 0664);

MODULE_PARM_DESC(cur_colorspace, "\n cur_colorspace \n");
module_param(cur_colorspace, int, 0664);

module_param(hdmi_yuv444_enable, int, 0664);
MODULE_PARM_DESC(hdmi_yuv444_enable, "hdmi_yuv444_enable");

module_param(hdmirx_de_repeat_enable, int, 0664);
MODULE_PARM_DESC(hdmirx_de_repeat_enable, "hdmirx_do_de_repeat_enable");

typedef struct hdmirx_dev_s {
	int                         index;
	dev_t                       devt;
	struct cdev                 cdev;
	struct device               *dev;
	struct tvin_parm_s          param;
	struct timer_list           timer;
	tvin_frontend_t             frontend;
} hdmirx_dev_t;

static unsigned first_bit_set(uint32_t data)
{
	unsigned n = 32;

	if (data != 0)
	{
		for (n = 0; (data & 1) == 0; n++)
		{
			data >>= 1;
		}
	}
	return n;
}

uint32_t get(uint32_t data, uint32_t mask)
{
	return (data & mask) >> first_bit_set(mask);
}

uint32_t set(uint32_t data, uint32_t mask, uint32_t value)
{
	return ((value << first_bit_set(mask)) & mask) | (data & ~mask);
}


void hdmirx_timer_handler(unsigned long arg)
{
	struct hdmirx_dev_s *devp = (struct hdmirx_dev_s *)arg;

	hdmirx_hw_monitor();
#if CEC_FUNC_ENABLE
	hdmirx_cec_rx_monitor();
	hdmirx_cec_tx_monitor();
#endif
	devp->timer.expires = jiffies + TIMER_STATE_CHECK;
	add_timer(&devp->timer);
}

int hdmirx_dec_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	if ((port >= TVIN_PORT_HDMI0) && (port <= TVIN_PORT_HDMI7)) {
		//pr_info("%s port:%x supported \n", __FUNCTION__, port);
		return 0;
	} else {
		//pr_info("%s port:%x not supported \n", __FUNCTION__, port);
		return -1;
	}
}

int hdmirx_dec_open(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	struct hdmirx_dev_s *devp;

	open_flage = 1;
	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	devp_hdmirx_suspend = container_of(fe, struct hdmirx_dev_s, frontend);
	devp->param.port = port;
	hdmirx_hw_enable();
	hdmirx_hw_init(port);
	/* timer */
	init_timer(&devp->timer);
	devp->timer.data = (ulong)devp;
	devp->timer.function = hdmirx_timer_handler;
	devp->timer.expires = jiffies + TIMER_STATE_CHECK;
	add_timer(&devp->timer);
	pr_info("%s port:%x ok\n",__FUNCTION__, port);
	return 0;
}

void hdmirx_dec_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
	struct hdmirx_dev_s *devp;
	struct tvin_parm_s *parm;

	open_flage = 1;
	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	devp_hdmirx_suspend = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	parm->info.fmt = fmt;
	parm->info.status = TVIN_SIG_STATUS_STABLE;
	pr_info("%s fmt:%d ok\n",__FUNCTION__, fmt);
}

void hdmirx_dec_stop(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	struct hdmirx_dev_s *devp;
	struct tvin_parm_s *parm;

	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	parm->info.fmt = TVIN_SIG_FMT_NULL;
	parm->info.status = TVIN_SIG_STATUS_NULL;
	to_init_state();
	pr_info("%s ok\n",__FUNCTION__);
}

void hdmirx_dec_close(struct tvin_frontend_s *fe)
{
	struct hdmirx_dev_s *devp;
	struct tvin_parm_s *parm;

	open_flage = 0;
	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	del_timer_sync(&devp->timer);
	hdmirx_hw_uninit();
	hdmirx_hw_disable(0);
	parm->info.fmt = TVIN_SIG_FMT_NULL;
	parm->info.status = TVIN_SIG_STATUS_NULL;
	pr_info("%s ok\n",__FUNCTION__);
}

/* interrupt handler */
int hdmirx_dec_isr(struct tvin_frontend_s *fe, unsigned int hcnt64)
{
	struct hdmirx_dev_s *devp;
	struct tvin_parm_s *parm;

	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	/* if there is any error or overflow, do some reset, then rerurn -1;*/
	if ((parm->info.status != TVIN_SIG_STATUS_STABLE) ||
	    (parm->info.fmt == TVIN_SIG_FMT_NULL)) {
		return -1;
	}
	return 0;
}

static int  hdmi_dec_callmaster(enum tvin_port_e port,struct tvin_frontend_s *fe)
{

	int status = 0;
	switch(port)
	{
		case TVIN_PORT_HDMI0:
			status = (hdmirx_rd_top(HDMIRX_TOP_HPD_PWR5V)>>(20 + 0)&0x1)==0x1;
			break;
		case TVIN_PORT_HDMI1:
			status = (hdmirx_rd_top(HDMIRX_TOP_HPD_PWR5V)>>(20 + 1)&0x1)==0x1;
			break;
		case TVIN_PORT_HDMI2:
			status = (hdmirx_rd_top(HDMIRX_TOP_HPD_PWR5V)>>(20 + 2)&0x1)==0x1;
			break;
		case TVIN_PORT_HDMI3:
		    status = (hdmirx_rd_top(HDMIRX_TOP_HPD_PWR5V)>>(20 + 3)&0x1)==0x1;
		    break;
		default:
			status = 1;
	}
	return status;

}
static struct tvin_decoder_ops_s hdmirx_dec_ops = {
	.support    = hdmirx_dec_support,
	.open       = hdmirx_dec_open,
	.start      = hdmirx_dec_start,
	.stop       = hdmirx_dec_stop,
	.close      = hdmirx_dec_close,
	.decode_isr = hdmirx_dec_isr,
	.callmaster_det = hdmi_dec_callmaster,
};

bool hdmirx_is_nosig(struct tvin_frontend_s *fe)
{
	bool ret = 0;

	ret = hdmirx_hw_is_nosig();
	return ret;
}

bool hdmirx_fmt_chg(struct tvin_frontend_s *fe)
{
    bool ret = false;
	enum tvin_sig_fmt_e fmt = TVIN_SIG_FMT_NULL;
	struct hdmirx_dev_s *devp;
	struct tvin_parm_s *parm;

#if 1

	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	if (!hdmirx_hw_pll_lock()) {
	    ret = true;
	} else {
 		fmt = hdmirx_hw_get_fmt();
		if(fmt != parm->info.fmt)
		{
			pr_info("hdmirx fmt: %d --> %d\n", parm->info.fmt, fmt);
			parm->info.fmt = fmt;
			ret = true;
#if 0
			ret = true;
#else
#if 0
			if (video_format_change()) {
				printk("vic change, return ture\n");
				ret = true;
			} else {
				printk("vic change, pixel not change, return false\n");
				ret = false;
			}
#endif
#endif
		} else {
		    ret = false;
		}
	}
#else
	devp = container_of(fe, struct hdmirx_dev_s, frontend);
	parm = &devp->param;
	if (!hdmirx_hw_pll_lock()) {
	    ret = true;
	} else {
		ret = video_format_change();
	}

#endif
	return ret;

}

bool hdmirx_pll_lock(struct tvin_frontend_s *fe)
{
	bool ret = true;

	ret = hdmirx_hw_pll_lock();
	return ret;
}

enum tvin_sig_fmt_e hdmirx_get_fmt(struct tvin_frontend_s *fe)
{
	enum tvin_sig_fmt_e fmt = TVIN_SIG_FMT_NULL;

	fmt = hdmirx_hw_get_fmt();
	return fmt;
}
#define FORCE_YUV	1
#define FORCE_RGB	2

extern unsigned char is_frame_packing(void);
extern unsigned char is_alternative(void);

void hdmirx_get_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
	unsigned char _3d_structure, _3d_ext_data;
	enum tvin_sig_fmt_e sig_fmt;
	prop->dvi_info = hdmirx_hw_get_dvi_info();

	switch (hdmirx_hw_get_color_fmt()) {
	case 1:
		prop->color_format = TVIN_YUV444;
		break;
	case 3:
		prop->color_format = TVIN_YUYV422;
		break;
	case 0:
	default:
		prop->color_format = TVIN_RGB444;
		break;
	}
	prop->dest_cfmt = TVIN_YUV422;
	if(force_colorspace == FORCE_YUV)
		if(prop->color_format == TVIN_RGB444)
			prop->color_format = TVIN_YUV444;
	if(force_colorspace == FORCE_RGB)
		prop->color_format = TVIN_RGB444;

	sig_fmt = hdmirx_hw_get_fmt();
	if(((sig_fmt == TVIN_SIG_FMT_HDMI_1920X1080P_60HZ) ||
		(sig_fmt == TVIN_SIG_FMT_HDMI_1920X1080P_50HZ)) &&
		hdmi_yuv444_enable &&
		(prop->color_format == TVIN_RGB444))
		prop->dest_cfmt = TVIN_YUV444;
	cur_colorspace = prop->color_format;
	prop->trans_fmt = TVIN_TFMT_2D;
	if (hdmirx_hw_get_3d_structure(&_3d_structure, &_3d_ext_data) >= 0) {
		if (_3d_structure == 0x0) {        /* frame packing */
			prop->trans_fmt = TVIN_TFMT_3D_FP;
		} else if (_3d_structure == 0x1) {   /* field alternative */
			prop->trans_fmt = TVIN_TFMT_3D_FA;
		} else if (_3d_structure == 0x2) {   /* line alternative */
			prop->trans_fmt = TVIN_TFMT_3D_LA;
		} else if (_3d_structure == 0x3) {   /* side-by-side full */
			prop->trans_fmt = TVIN_TFMT_3D_LRF;
		} else if (_3d_structure == 0x4) {   /* L + depth */
			prop->trans_fmt = TVIN_TFMT_3D_LD;
		} else if (_3d_structure == 0x5) {   /* L + depth + graphics + graphics-depth */
			prop->trans_fmt = TVIN_TFMT_3D_LDGD;
		} else if (_3d_structure == 0x6) {   /* top-and-bot */
			prop->trans_fmt = TVIN_TFMT_3D_TB;
		} else if (_3d_structure == 0x8) {  /* Side-by-Side half */
			switch (_3d_ext_data) {
		        case 0x5:	/*Odd/Left picture, Even/Right picture*/
				prop->trans_fmt = TVIN_TFMT_3D_LRH_OLER;
				break;
		        case 0x6:	/*Even/Left picture, Odd/Right picture*/
				prop->trans_fmt = TVIN_TFMT_3D_LRH_ELOR;
				break;
		        case 0x7:	/*Even/Left picture, Even/Right picture*/
				prop->trans_fmt = TVIN_TFMT_3D_LRH_ELER;
				break;
		        case 0x4:	/*Odd/Left picture, Odd/Right picture*/
		        default:
				prop->trans_fmt = TVIN_TFMT_3D_LRH_OLOR;
				break;
			}
		}
	}
	if( is_frame_packing()) {
		prop->trans_fmt = TVIN_TFMT_3D_FP;
	} else if( is_alternative()) {
		prop->trans_fmt = TVIN_TFMT_3D_LA;
	}
	/* 1: no repeat; 2: repeat 1 times; 3: repeat two; ... */
	prop->decimation_ratio = (hdmirx_hw_get_pixel_repeat() - 1) | HDMI_DE_REPEAT_DONE_FLAG;

	//patch for 4k*2k fmt buffer limit
	if(TVIN_SIG_FMT_HDMI_4096_2160_00HZ == sig_fmt) {
		prop->hs = 64;
		prop->he = 64;
		prop->vs = 0;
		prop->ve = 0;
		prop->scaling4h = 1080;
	} else if(TVIN_SIG_FMT_HDMI_3840_2160_00HZ == sig_fmt) {
		prop->scaling4h = 1080;
	}
}

bool hdmirx_check_frame_skip(struct tvin_frontend_s *fe)
{
	return hdmirx_hw_check_frame_skip();
}

static struct tvin_state_machine_ops_s hdmirx_sm_ops = {
	.nosig            = hdmirx_is_nosig,
	.fmt_changed      = hdmirx_fmt_chg,
	.get_fmt          = hdmirx_get_fmt,
	.fmt_config       = NULL,
	.adc_cal          = NULL,
	.pll_lock         = hdmirx_pll_lock,
	.get_sig_propery  = hdmirx_get_sig_propery,
	.vga_set_param    = NULL,
	.vga_get_param    = NULL,
	.check_frame_skip = hdmirx_check_frame_skip,
};



/**
 *hdmirx device driver
 */
static int hdmirx_open(struct inode *inode, struct file *file)
{
	hdmirx_dev_t *devp;

	devp = container_of(inode->i_cdev, hdmirx_dev_t, cdev);
	file->private_data = devp;
	return 0;
}

static int hdmirx_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}


static long hdmirx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	return ret;
}

/*
 * File operations structure
 * Defined in linux/fs.h
 */
static struct file_operations hdmirx_fops = {
	.owner		= THIS_MODULE,
	.open		= hdmirx_open,
	.release	= hdmirx_release,
	.unlocked_ioctl	= hdmirx_ioctl,
};

/* attr */
static unsigned char *hdmirx_log_buf     = NULL;
static unsigned int  hdmirx_log_wr_pos   = 0;
static unsigned int  hdmirx_log_rd_pos   = 0;
static unsigned int  hdmirx_log_buf_size = 0;
static DEFINE_SPINLOCK (hdmirx_print_lock);
#define DEF_LOG_BUF_SIZE (1024*128)
#define PRINT_TEMP_BUF_SIZE 128

void hdmirx_powerdown(const char* buf, int size)
{
	char tmpbuf[128];
	int i = 0;

	while((buf[i]) && (buf[i] != ',') && (buf[i] != ' ')) {
		tmpbuf[i]=buf[i];
		i++;
	}
	tmpbuf[i] = 0;
	if(strncmp(tmpbuf, "powerdown", 9)==0){
		if (open_flage == 1) {
			del_timer_sync(&devp_hdmirx_suspend->timer);
			WRITE_MPEG_REG(HHI_HDMIRX_CLK_CNTL,0x0);
		}
		pr_info("[hdmirx]: hdmirx power down\n");
  	}
}

int hdmirx_print_buf(char* buf, int len)
{
	unsigned long flags;
	int pos;
	int hdmirx_log_rd_pos_;

	if (hdmirx_log_buf_size == 0)
		return 0;
	spin_lock_irqsave(&hdmirx_print_lock, flags);
	hdmirx_log_rd_pos_ = hdmirx_log_rd_pos;
	if (hdmirx_log_wr_pos >= hdmirx_log_rd_pos)
		hdmirx_log_rd_pos_ += hdmirx_log_buf_size;
	for (pos = 0; pos < len && hdmirx_log_wr_pos < (hdmirx_log_rd_pos_ - 1); pos++, hdmirx_log_wr_pos++){
		if (hdmirx_log_wr_pos >= hdmirx_log_buf_size)
			hdmirx_log_buf[hdmirx_log_wr_pos - hdmirx_log_buf_size] = buf[pos];
		else
			hdmirx_log_buf[hdmirx_log_wr_pos] = buf[pos];
	}
	if (hdmirx_log_wr_pos >= hdmirx_log_buf_size)
		hdmirx_log_wr_pos-=hdmirx_log_buf_size;
	spin_unlock_irqrestore(&hdmirx_print_lock, flags);
	return pos;
}

int hdmirx_print(const char *fmt, ...)
{
	va_list args;
	int avail = PRINT_TEMP_BUF_SIZE;
	char buf[PRINT_TEMP_BUF_SIZE];
	int pos = 0;
	int len = 0;

	if (hdmirx_log_flag & 1) {
		va_start(args, fmt);
		vprintk(fmt, args);
		va_end(args);
		return 0;
	}
	if(hdmirx_log_buf_size == 0)
		return 0;

	//len += snprintf(buf+len, avail-len, "%d:",log_seq++);
	len += snprintf(buf + len, avail - len, "[%u] ", (unsigned int)jiffies);
	va_start(args, fmt);
	len += vsnprintf(buf + len, avail - len, fmt, args);
	va_end(args);
	if ((avail-len) <= 0) {
		buf[PRINT_TEMP_BUF_SIZE - 1] = '\0';
	}
	pos = hdmirx_print_buf(buf, len);
	return pos;
}

static int log_init(int bufsize)
{
	if (bufsize == 0) {
		if (hdmirx_log_buf) {
			kfree(hdmirx_log_buf);
			hdmirx_log_buf = NULL;
			hdmirx_log_buf_size = 0;
			hdmirx_log_rd_pos = 0;
			hdmirx_log_wr_pos = 0;
		}
	}
	if ((bufsize >= 1024) && (hdmirx_log_buf == NULL)) {
		hdmirx_log_buf_size = 0;
		hdmirx_log_rd_pos = 0;
		hdmirx_log_wr_pos = 0;
		hdmirx_log_buf = kmalloc(bufsize, GFP_KERNEL);
		if (hdmirx_log_buf) {
			hdmirx_log_buf_size = bufsize;
		}
	}
	return 0;
}

static ssize_t show_log(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long flags;
	ssize_t read_size=0;

	if (hdmirx_log_buf_size == 0)
		return 0;
	spin_lock_irqsave(&hdmirx_print_lock, flags);
	if (hdmirx_log_rd_pos < hdmirx_log_wr_pos) {
		read_size = hdmirx_log_wr_pos-hdmirx_log_rd_pos;
	} else if (hdmirx_log_rd_pos > hdmirx_log_wr_pos) {
		read_size = hdmirx_log_buf_size-hdmirx_log_rd_pos;
	}
	if (read_size > PAGE_SIZE)
		read_size = PAGE_SIZE;
	if (read_size > 0)
		memcpy(buf, hdmirx_log_buf+hdmirx_log_rd_pos, read_size);
	hdmirx_log_rd_pos += read_size;
	if (hdmirx_log_rd_pos >= hdmirx_log_buf_size)
		hdmirx_log_rd_pos = 0;
	spin_unlock_irqrestore(&hdmirx_print_lock, flags);
	return read_size;
}

static ssize_t store_log(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	unsigned long flags;

	if (strncmp(buf, "bufsize", 7) == 0) {
		tmp = simple_strtoul(buf + 7, NULL, 10);
		spin_lock_irqsave(&hdmirx_print_lock, flags);
		log_init(tmp);
		spin_unlock_irqrestore(&hdmirx_print_lock, flags);
		printk("hdmirx_store:set bufsize tmp %d %d\n",tmp, hdmirx_log_buf_size);
	} else {
		hdmirx_print(0, "%s", buf);
	}
	return 16;
}


static ssize_t hdmirx_debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t hdmirx_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	hdmirx_debug(buf, count);
	hdmirx_powerdown(buf, count);
	return count;
}

static ssize_t hdmirx_edid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return hdmirx_read_edid_buf(buf, PAGE_SIZE);
}

static ssize_t hdmirx_edid_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	hdmirx_fill_edid_buf(buf, count);
	return count;
}

static ssize_t hdmirx_key_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return hdmirx_read_key_buf(buf, PAGE_SIZE);
}

static ssize_t hdmirx_key_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	hdmirx_fill_key_buf(buf, count);
	return count;
}

static ssize_t show_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	return hdmirx_hw_dump_reg(buf, PAGE_SIZE);
}

static ssize_t cec_get_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t cec_set_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(debug, S_IWUSR | S_IWUGO | S_IWOTH , hdmirx_debug_show, hdmirx_debug_store);
static DEVICE_ATTR(edid,  S_IWUSR | S_IRUGO, hdmirx_edid_show, hdmirx_edid_store);
static DEVICE_ATTR(key,   S_IWUSR | S_IRUGO, hdmirx_key_show, hdmirx_key_store);
static DEVICE_ATTR(log,   S_IWUGO | S_IRUGO, show_log, store_log);
static DEVICE_ATTR(reg,   S_IWUGO | S_IRUGO, show_reg, NULL);
static DEVICE_ATTR(cec,   S_IWUGO | S_IRUGO, cec_get_state, cec_set_state);

static int hdmirx_add_cdev(struct cdev *cdevp, struct file_operations *fops,
		int minor)
{
	int ret;
	dev_t devno = MKDEV(MAJOR(hdmirx_devno), minor);

	cdev_init(cdevp, fops);
	cdevp->owner = THIS_MODULE;
	ret = cdev_add(cdevp, devno, 1);
	return ret;
}

static struct device * hdmirx_create_device(struct device *parent, int id)
{
	dev_t devno = MKDEV(MAJOR(hdmirx_devno),  id);
	return device_create(hdmirx_clsp, parent, devno, NULL, "%s0",
			TVHDMI_DEVICE_NAME);
	/* @to do this after Middleware API modified */
	/*return device_create(hdmirx_clsp, parent, devno, NULL, "%s",
	  TVHDMI_DEVICE_NAME); */
}

static void hdmirx_delete_device(int minor)
{
	dev_t devno = MKDEV(MAJOR(hdmirx_devno), minor);
	device_destroy(hdmirx_clsp, devno);
}

unsigned char *pEdid_buffer;

static int hdmirx_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct hdmirx_dev_s *hdevp;

	log_init(DEF_LOG_BUF_SIZE);
	pEdid_buffer = (unsigned char*) pdev->dev.platform_data;

	/* allocate memory for the per-device structure */
	hdevp = kmalloc(sizeof(struct hdmirx_dev_s), GFP_KERNEL);
	if (!hdevp) {
		pr_info("hdmirx:failed to allocate memory for hdmirx device\n");
		ret = -ENOMEM;
		goto fail_kmalloc_hdev;
	}
	memset(hdevp, 0, sizeof(struct hdmirx_dev_s));
	/*@to get from bsp*/
	if(pdev->id == -1){
		hdevp->index = 0;
	} else {
		pr_info("%s: failed to get device id\n", __func__);
		goto fail_get_id;
	}
	/* create cdev and reigser with sysfs */
	ret = hdmirx_add_cdev(&hdevp->cdev, &hdmirx_fops, hdevp->index);
	if (ret) {
		pr_info("%s: failed to add cdev\n", __func__);
		goto fail_add_cdev;
	}
	/* create /dev nodes */
	hdevp->dev = hdmirx_create_device(&pdev->dev, hdevp->index);
	if (IS_ERR(hdevp->dev)) {
		pr_info("hdmirx: failed to create device node\n");
		ret = PTR_ERR(hdevp->dev);
		goto fail_create_device;
	}
	/*create sysfs attribute files*/
	ret = device_create_file(hdevp->dev, &dev_attr_debug);
	if(ret < 0) {
		pr_info("hdmirx: fail to create debug attribute file\n");
		goto fail_create_debug_file;
	}
	ret = device_create_file(hdevp->dev, &dev_attr_edid);
	if(ret < 0) {
		pr_info("hdmirx: fail to create edid attribute file\n");
		goto fail_create_edid_file;
	}
	ret = device_create_file(hdevp->dev, &dev_attr_key);
	if(ret < 0) {
		pr_info("hdmirx: fail to create key attribute file\n");
		goto fail_create_key_file;
	}
	ret = device_create_file(hdevp->dev, &dev_attr_log);
	if(ret < 0) {
		pr_info("hdmirx: fail to create log attribute file\n");
		goto fail_create_log_file;
	}
	ret = device_create_file(hdevp->dev, &dev_attr_reg);
	if(ret < 0) {
		pr_info("hdmirx: fail to create reg attribute file\n");
		goto fail_create_reg_file;
	}
	ret = device_create_file(hdevp->dev, &dev_attr_cec);
	if(ret < 0) {
		pr_info("hdmirx: fail to create cec attribute file\n");
		goto fail_create_cec_file;
	}
	/* frontend */
	tvin_frontend_init(&hdevp->frontend, &hdmirx_dec_ops, &hdmirx_sm_ops, hdevp->index);
	sprintf(hdevp->frontend.name, "%s", TVHDMI_NAME);
	tvin_reg_frontend(&hdevp->frontend);

	hdmirx_hw_enable();
    /* set all hpd status  */
	hdmirx_default_hpd(1);

	dev_set_drvdata(hdevp->dev, hdevp);
	pr_info("hdmirx: driver probe ok\n");
	return 0;


fail_create_cec_file:
	device_remove_file(hdevp->dev, &dev_attr_reg);
fail_create_reg_file:
	device_remove_file(hdevp->dev, &dev_attr_log);
fail_create_log_file:
	device_remove_file(hdevp->dev, &dev_attr_key);
fail_create_key_file:
	device_remove_file(hdevp->dev, &dev_attr_edid);
fail_create_edid_file:
	device_remove_file(hdevp->dev, &dev_attr_debug);
fail_create_debug_file:
	hdmirx_delete_device(hdevp->index);
fail_create_device:
	cdev_del(&hdevp->cdev);
fail_add_cdev:
fail_get_id:
	kfree(hdevp);
fail_kmalloc_hdev:
	return ret;

}

static int hdmirx_remove(struct platform_device *pdev)
{
	struct hdmirx_dev_s *hdevp;

	hdevp = platform_get_drvdata(pdev);
	device_remove_file(hdevp->dev, &dev_attr_debug);
	device_remove_file(hdevp->dev, &dev_attr_edid);
	device_remove_file(hdevp->dev, &dev_attr_key);
	device_remove_file(hdevp->dev, &dev_attr_log);
	device_remove_file(hdevp->dev, &dev_attr_reg);
	device_remove_file(hdevp->dev, &dev_attr_cec);
	tvin_unreg_frontend(&hdevp->frontend);
	hdmirx_delete_device(hdevp->index);
	cdev_del(&hdevp->cdev);
	kfree(hdevp);
	pr_info("hdmirx: driver removed ok.\n");
	return 0;
}

#ifdef CONFIG_PM
static int hdmirx_suspend(struct platform_device *pdev, pm_message_t state)
{
	int i = 0;

	pr_info("[hdmirx]: hdmirx_suspend\n");
	if (open_flage == 1) {
		pr_info("[hdmirx]: suspend--step1111\n");
		if (resume_flag == 0)
		del_timer_sync(&devp_hdmirx_suspend->timer);
		pr_info("[hdmirx]: suspend--step2\n");
		pr_info("[hdmirx]: suspend--step3\n");
		for (i = 0; i < 5000; i++) {
		}
		pr_info("[hdmirx]: suspend--step4\n");
	}
	pr_info("[hdmirx]: suspend--step5\n");
	clk_off();
	pr_info("[hdmirx]: suspend success\n");
	return 0;
}

static int hdmirx_resume(struct platform_device *pdev)
{
	unsigned int data32;
	int i;

	//hdmirx_hw_enable();
	pr_info("hdmirx: resume module\n");

	/* DWC clock enable */
	//Wr_reg_bits(HHI_GCLK_MPEG0, 1, 21, 1);  // Turn on clk_hdmirx_pclk, also = sysclk
	WRITE_MPEG_REG(HHI_GCLK_MPEG0, (READ_MPEG_REG(HHI_GCLK_MPEG0) | (1 << 21)));
	// Enable APB3 fail on error
	//*((volatile unsigned long *) P_HDMIRX_CTRL_PORT)          |= (1 << 15);   // APB3 to HDMIRX-TOP err_en
	//*((volatile unsigned long *) (P_HDMIRX_CTRL_PORT+0x10))   |= (1 << 15);   // APB3 to HDMIRX-DWC err_en

	//turn on clocks: md, cfg...

	data32  = 0;
	data32 |= 0 << 25;  // [26:25] HDMIRX mode detection clock mux select: osc_clk
	data32 |= 1 << 24;  // [24]    HDMIRX mode detection clock enable
	data32 |= 0 << 16;  // [22:16] HDMIRX mode detection clock divider
	data32 |= 3 << 9;   // [10: 9] HDMIRX config clock mux select: fclk_div5=400MHz
	data32 |= 1 << 8;   // [    8] HDMIRX config clock enable
	data32 |= 3 << 0;   // [ 6: 0] HDMIRX config clock divider: 400/4=100MHz
	WRITE_MPEG_REG(HHI_HDMIRX_CLK_CNTL,     data32);

	data32  = 0;
	data32 |= 2             << 25;  // [26:25] HDMIRX ACR ref clock mux select: fclk_div5
	data32 |= rx.ctrl.acr_mode      << 24;  // [24]    HDMIRX ACR ref clock enable
	data32 |= 0             << 16;  // [22:16] HDMIRX ACR ref clock divider
	data32 |= 2             << 9;   // [10: 9] HDMIRX audmeas clock mux select: fclk_div5
	data32 |= 1             << 8;   // [    8] HDMIRX audmeas clock enable
	data32 |= 1             << 0;   // [ 6: 0] HDMIRX audmeas clock divider: 400/2 = 200MHz
	WRITE_MPEG_REG(HHI_HDMIRX_AUD_CLK_CNTL, data32);
	pr_info("hdmirx: resume module---1\n");

	for (i = 0; i < 5000; i++) {
	}

	data32  = 0;
	data32 |= 1 << 17;  // [17]     audfifo_rd_en
	data32 |= 1 << 16;  // [16]     pktfifo_rd_en
	data32 |= 1 << 2;   // [2]      hdmirx_cecclk_en
	data32 |= 0 << 1;   // [1]      bus_clk_inv
	data32 |= 0 << 0;   // [0]      hdmi_clk_inv
	if (resume_flag == 0)
	hdmirx_wr_top( 0x1, data32);    // DEFAULT: {32'h0}
	pr_info("hdmirx: resume module---2\n");

	for (i = 0; i < 5000; i++) {
	}
	if ((resume_flag == 0) && (open_flage == 1))
	add_timer(&devp_hdmirx_suspend->timer);
	pr_info("hdmirx: resume module---end,open_flage:%d\n",open_flage);

	return 0;

}
#endif

#ifdef CONFIG_OF
static const struct of_device_id hdmirx_dt_match[]={
    {
        .compatible     = "amlogic,hdmirx",
    },
    {},
};
#else
#define hdmirx_dt_match NULL
#endif

static struct platform_driver hdmirx_driver = {
	.probe      = hdmirx_probe,
	.remove     = hdmirx_remove,
#ifdef CONFIG_PM
	.suspend    = hdmirx_suspend,
	.resume     = hdmirx_resume,
#endif
	.driver     = {
		.name   = TVHDMI_DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = hdmirx_dt_match,
	}
};

void hdmirx_irq_init(void);
static int __init hdmirx_init(void)
{
	int ret = 0;
	if(init_flag & INIT_FLAG_NOT_LOAD)
		return 0;

	ret = alloc_chrdev_region(&hdmirx_devno, 0, 1, TVHDMI_NAME);
	if (ret < 0) {
		pr_info("hdmirx: failed to allocate major number\n");
		goto fail_alloc_cdev_region;
	}

	hdmirx_clsp = class_create(THIS_MODULE, TVHDMI_NAME);
	if (IS_ERR(hdmirx_clsp)) {
		pr_info(KERN_ERR "hdmirx: can't get hdmirx_clsp\n");
		ret = PTR_ERR(hdmirx_clsp);
		goto fail_class_create;
	}

	ret = platform_driver_register(&hdmirx_driver);
	if (ret != 0) {
		pr_info("failed to register hdmirx module, error %d\n", ret);
		ret = -ENODEV;
		goto fail_pdrv_register;
	}
	pr_info("hdmirx: hdmirx_init.\n");

	hdmirx_irq_init();
	return 0;

fail_pdrv_register:
	class_destroy(hdmirx_clsp);
fail_class_create:
	unregister_chrdev_region(hdmirx_devno, 1);
fail_alloc_cdev_region:
	return ret;

}

static void __exit hdmirx_exit(void)
{
	class_destroy(hdmirx_clsp);
	unregister_chrdev_region(hdmirx_devno, 1);
	platform_driver_unregister(&hdmirx_driver);
	pr_info("hdmirx: hdmirx_exit.\n");
}

#if 0
/**
* besides characters defined in seperator,
* '\"' are used as seperator;
* and any characters in '\"' will not act as seperator
*/
static char* next_token_ex(char* seperator, char *buf, unsigned size, unsigned offset, unsigned *token_len, unsigned *token_offset)
{
	char *pToken = NULL;
	char last_seperator = 0;
	char trans_char_flag = 0;
	if(buf){
		for (;offset<size;offset++){
			int ii=0;
			char ch;
			if (buf[offset] == '\\'){
				trans_char_flag = 1;
				continue;
			}
			while(((ch=seperator[ii++])!=buf[offset])&&(ch)){
			}
			if (ch){
				if (!pToken){
					continue;
				}
				else {
					if (last_seperator != '"'){
						*token_len = (unsigned)(buf + offset - pToken);
						*token_offset = offset;
						return pToken;
					}
				}
			}
			else if (!pToken){
				if (trans_char_flag&&(buf[offset] == '"'))
				last_seperator = buf[offset];
				pToken = &buf[offset];
			}
			else if ((trans_char_flag&&(buf[offset] == '"'))&&(last_seperator == '"')){
				*token_len = (unsigned)(buf + offset - pToken - 2);
				*token_offset = offset + 1;
				return pToken + 1;
			}
			trans_char_flag = 0;
		}
		if (pToken) {
			*token_len = (unsigned)(buf + offset - pToken);
			*token_offset = offset;
		}
	}
	return pToken;
}

static  int __init hdmirx_boot_para_setup(char *s)
{
	char separator[]={' ',',',';',0x0};
	char *token;
	unsigned token_len, token_offset, offset=0;
	int size = strlen(s);
	do{
		token=next_token_ex(separator, s, size, offset, &token_len, &token_offset);
		if(token){
			if((token_len==3) && (strncmp(token, "off", token_len)==0)){
				init_flag|=INIT_FLAG_NOT_LOAD;
			}
		}
		offset=token_offset;
	}while(token);
	return 0;
}

__setup("hdmirx=",hdmirx_boot_para_setup);
#endif

module_init(hdmirx_init);
module_exit(hdmirx_exit);

MODULE_DESCRIPTION("AMLOGIC HDMIRX driver");
MODULE_LICENSE("GPL");

