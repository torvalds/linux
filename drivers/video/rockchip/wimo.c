/* drivers/video/wimo.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * author: chenhengming chm@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/fb.h>
#include <plat/ipp.h>
#include "linux/wimo.h"
#include<linux/rk_fb.h>
#define WIMO_MIN_ALLOC               PAGE_SIZE
#define WIMO_IS_PAGE_ALIGNED(addr)   (!((addr) & (~PAGE_MASK)))

#define WIMO_DEBUG                   0
#define WIMO_DEBUG_MSGS              0

#if WIMO_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
		    ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif
#define WIMO_WRITE_FILE

struct win_set {
	volatile u32 y_offset;
	volatile u32 c_offset;
};

struct win0_par {
    u32 refcount;
    u32	pseudo_pal[16];
    u32 y_offset;
    u32 c_offset;
    u32 xpos;         //size in panel
    u32 ypos;
    u32 xsize;        //start point in panel
    u32 ysize;
    u32 format;

    wait_queue_head_t wait;
    struct win_set mirror;
    struct win_set displ;
    struct win_set done;

    u8 par_seted;
    u8 addr_seted;
};


/**
 * struct for global wimo info
 */
struct enc_buff_info{
	u32 bitperpixel;
	u32 mode;
	u32 	xaff;
	u32 	yaff;
	u32 xpos;
	u32 ypos;
	u32 xsize;
	u32 ysize;
	u32 src_y;
	u32 src_uv;
	u32 dst_width;
	u32 dst_height;
	u32 dst_vir_w;
	u32 dst_vir_h;
	u32 y_offset;
	u32 c_offset;
	u32 src_width;
	u32 src_height;
	long long	Frame_Time;
	unsigned char* ui_buffer;
	unsigned char* ui_buffer_map;
	int rotation_flag;
	
	
};
struct wimo_video_info{
	u32 				bitperpixel;
	u32 				mode;
	u32 				xaff;
	u32 				yaff;
	u32 				xpos;
	u32 				ypos;
	u32 				xsize;
	u32 				ysize;
	u32 				src_y;
	u32 				src_uv;
	u32 				dst_width;
	u32 				dst_height;
	u32 				rot_dst_width;
	u32 				rot_dst_height;
	u32 				src_width;
	u32 				src_height;
	u32 				set_flag;
	int 			Last_rotation_flag;
	int 			rotation_flag;
	long long	Last_Frame_Time;
	struct timeval Wimo_RGB_time;
	struct timeval Wimo_YUV_time;
	struct timeval Wimo_Cur_time;
	struct enc_buff_info enc_buff[4];
	u32 buffer_num;
	u32 avail_index;
	u32 work_index;
	volatile u32 avail_frame;
	int test_sign;
	u32 frame_num;

};
struct wimo_audio_info{
	volatile u32			buffer_size;
	volatile u32 			head_offset;
	volatile u32 			tail_offset;
	volatile u32 			data_len;
	u32					nSamplePerFrame;
	u32					nChannel;
	u32					nBytePerSample;
	u32					nBytePerFrame;
	unsigned char*		Out_Buffer;
	struct	timeval		*time_stamp;
	unsigned char 		*audio_data;

};
struct wimo_audio_param{
	u32					buffer_size;
	u32					nSamplePerFrame;
	u32					nChannel;
	u32					nBytePerSample;
	u32					nBytePerFrame;
	u32					Reserverd[7];
};
int rgb_time;
int yuv_time;
struct wimo_info{
	struct wimo_video_info   	video_info;
	struct wimo_audio_info		audio_info;
	struct rw_semaphore 		rw_sem;
	struct miscdevice 			dev;
	int video_start_sign;
	int audio_start_sign;
	int start_sign;
	int volumn_open_sign;
	int rotation_flag;
};
struct mutex video_lock;
struct mutex audio_lock;
struct mutex wimo_lock;
//int wimo_prepare_para(struct fb_info *info);
//int wimo_prepare_buff(struct fb_info *info);
//int wimo_prepare(struct fb_info *info);


static int wimo_count = 0;
static struct wimo_info wimo;
#define wdm_rwsem               (wimo.rw_sem)
extern int (*audio_data_to_wimo)(void* data,int size,int channel);
extern int (*video_data_to_wimo)(struct fb_info *info,u32 yuv_phy[2]);

unsigned long temp_vv;
static int frame_num = 0;
static int Is_Wimo_Loaded()
{
	int ret = -1;
	if(wimo_count > 0)
		ret = 0;
	return ret;
}
static int wimo_start(void)
{
	int ret = 0;
	wimo.start_sign = 1;
	
	return ret;
}
static int wimo_stop(void)
{
	int ret = 0;
	
	wimo.start_sign = 0;
	return ret;
}
static int wimo_set_rotation(int rotation_flag)
{
	int ret = 0;
	
	wimo.rotation_flag = rotation_flag;
	return ret;
}
struct file* wimo_filp;
mm_segment_t old_fs;
struct file* wimo_filp_output;
mm_segment_t old_fs_output;
static int wimo_audio_open(unsigned long *temp)
{
	int ret = 0;
	struct wimo_audio_info *audio_info = &wimo.audio_info;
	struct timeval timeFirst;
		do_gettimeofday(&timeFirst);	
	if(wimo.start_sign == 0)
	{
		printk("Apk didn't start when audio start \n");
		return -1;
	}
	memset(audio_info, 0, sizeof(struct wimo_audio_info));
	wimo.audio_start_sign 			= 1;	
	audio_info->Out_Buffer 		= NULL;

	audio_info->buffer_size 		= 50;
	audio_info->nSamplePerFrame 	= 1024;
	audio_info->nBytePerSample		= 2;
	audio_info->nChannel			= 2;
	audio_info->nBytePerFrame		= 4096;
	
	
	audio_info->head_offset		= 0;
	audio_info->tail_offset 			= 0;
	audio_info->data_len 			= 0;
	
	audio_info->audio_data 	= (unsigned char*)kmalloc(audio_info->buffer_size * audio_info->nBytePerFrame,GFP_KERNEL);
	audio_info->time_stamp 	= (struct timeval*)kmalloc(audio_info->buffer_size * sizeof(struct timeval),GFP_KERNEL);
	memset(audio_info->audio_data, 0,audio_info->buffer_size * audio_info->nBytePerFrame);
	printk("audio_info->buffer_size * audio_info->nBytePerFrame  timestamp %d  audio_info->Out_Buffer	 %x time %lld %x %x\n",
		audio_info->buffer_size * audio_info->nBytePerFrame,audio_info->time_stamp,audio_info->Out_Buffer,(int64_t)timeFirst.tv_sec*1000000ll + timeFirst.tv_usec
		,__pa(audio_info->audio_data),__pa(audio_info->time_stamp));
	#ifdef	WIMO_WRITE_FILE
	wimo_filp = NULL;
	wimo_filp = filp_open("/data/test/input_cmp.pcm",O_CREAT|O_TRUNC|O_RDWR,0);
	if (wimo_filp)
 	{
	  old_fs = get_fs();
	  set_fs(get_ds());
	}
	wimo_filp_output = NULL;
	wimo_filp_output = filp_open("/data/test/output_cmp.pcm",O_CREAT|O_TRUNC|O_RDWR,0);
	if (wimo_filp_output)
 	{
	  old_fs_output = get_fs();
	  set_fs(get_ds());
	}
	#endif
	return ret;
}
static int wimo_audio_close(void)
{
	int ret = 0;
	struct wimo_audio_info *audio_info = &wimo.audio_info;
	#ifdef	WIMO_WRITE_FILE
	set_fs(old_fs);
  	filp_close(wimo_filp,NULL);

	set_fs(old_fs_output);
  	filp_close(wimo_filp_output,NULL);

	#endif
	printk("wimo_audio_close\n");
	kfree((void *)audio_info->time_stamp);
	kfree((void *)audio_info->audio_data);
	
	audio_info->Out_Buffer 	= NULL;
	wimo.audio_start_sign 		= 0;
	return ret;
}
static void wimo_audio_set_para(struct wimo_audio_param *param)
{
	struct wimo_audio_info *audio_info = &wimo.audio_info;
	audio_info->buffer_size 		= param->buffer_size;
	audio_info->nSamplePerFrame 	= param->nSamplePerFrame;
	audio_info->nChannel 			= param->nChannel;
	audio_info->nBytePerSample 	= param->nBytePerSample;
	audio_info->nBytePerFrame 		= param->nBytePerFrame;
	DLOG("param %d %d %d %d %d  audio_info %d %d %d %d %d",
		param->buffer_size,param->nSamplePerFrame,param->nChannel,param->nBytePerSample,param->nBytePerFrame,
		audio_info->buffer_size,audio_info->nSamplePerFrame,audio_info->nChannel,audio_info->nBytePerSample,audio_info->nBytePerFrame);
	return;
}

static void wimo_audio_set_vol(u32 temp_data)
{
	wimo.volumn_open_sign = temp_data;
	return;
}
static int wimo_audio_find_frame()
{
	struct wimo_audio_info *audio_info = &wimo.audio_info;
	int ret = 0;
	if(audio_info->data_len >= 1)
	{
		audio_info->Out_Buffer = (void*)(&audio_info->audio_data[audio_info->head_offset * audio_info->nBytePerFrame]);
		//wimo_filp_output->f_op->write(wimo_filp_output, audio_info->Out_Buffer, audio_info->nBytePerFrame, &wimo_filp_output->f_pos);
		audio_info->data_len --;
		audio_info->head_offset ++;
		audio_info->head_offset %= audio_info->buffer_size;
	}
	else
	{
		ret = -1;
	}
	
	return ret;
}
static int wimo_audio_prepare(void* data,int size,int channel)
{
	int ret = 0;
	mutex_lock(&audio_lock);
	
	if(wimo.audio_start_sign && wimo.start_sign)
	{
		struct timeval timeFirst;
		unsigned long* temp = (unsigned long*)data;
		struct wimo_audio_info *audio_info = &wimo.audio_info;
		unsigned char* buf = data;
		do_gettimeofday(&timeFirst);	
		memcpy((void*)(&audio_info->audio_data[audio_info->tail_offset * audio_info->nBytePerFrame]), (void*)data,size);
		memcpy((void*)(&audio_info->time_stamp[audio_info->tail_offset]),(void*)(&timeFirst),sizeof(struct timeval));
		audio_info->tail_offset ++;
		audio_info->tail_offset %= audio_info->buffer_size;
		wimo_filp->f_op->write(wimo_filp, data, size, &wimo_filp->f_pos);
		if(audio_info->data_len + 1 < audio_info->buffer_size)
		{
			audio_info->data_len ++;
		}
		else
		{
			audio_info->head_offset=audio_info->tail_offset;
			audio_info->data_len = audio_info->buffer_size;
		}
//		if(wimo.volumn_open_sign==0)
//		{
//			memset((void*)data,0,size);
//
//		}
	}		
	else
	{
		//DLOG("wimo_video not open\n");
		ret = AUDIO_ENCODER_CLOSED;
	}
	if(wimo.start_sign && wimo.volumn_open_sign == 0)
		memset((void*)data,0,size);
	
	mutex_unlock(&audio_lock);
	return ret;
}
/*
	wimo_video can be opened once at a time.
	Other user must wait for last open closed.
*/
static int wimo_video_open(unsigned long *temp)
{
	struct wimo_video_info *video_info = &wimo.video_info;
	int i;
	int ret =0 ;
	printk("wimo_video_open\n");
	if(wimo.start_sign == 0)
	{
		printk("Apk didn't start when video start ");
		return -1;
	}
	if(wimo.video_start_sign == 1)
	{
		printk("last wimo_video still opened ,something wrong\n");
		
	}
	memset(&wimo.video_info,0,sizeof(struct wimo_video_info));
	if(wimo.rotation_flag < 0 )
		DLOG("wimo_set_buff rotation_flag < 0,maybe something wrong\n");
		
	video_info->Last_rotation_flag = -1;
	video_info->rotation_flag = wimo.rotation_flag;
 	for(i = 0; i < temp[3]; i++)
	{
		video_info->enc_buff[i].ui_buffer= (unsigned char*)temp[i+4];
		video_info->enc_buff[i].ui_buffer_map= ioremap(temp[i+4],temp[2]);
		if(video_info->enc_buff[i].ui_buffer_map == NULL)
		{
			printk("can't map a buffer for ui\n");
			return -EFAULT;
		}
	}
	video_info->buffer_num = temp[3];
	video_info->dst_width 	= (temp[0] + 15) & 0xfff0;
	video_info->dst_height 	= (temp[1] + 15) & 0xfff0;
	video_info->xsize 		= video_info->dst_width;
	video_info->ysize 		= video_info->dst_height;
	wimo.video_start_sign = 1;	
	printk("video_info->dst_width %d video_info->dst_height %d num %d %x ui_buffer %x %x   size %d map %x %x\n",
		video_info->dst_width,video_info->dst_height,video_info->buffer_num,(long)&wimo.video_info.buffer_num,
		video_info->enc_buff[0].ui_buffer,video_info->enc_buff[1].ui_buffer,temp[2],video_info->enc_buff[0].ui_buffer_map
		,video_info->enc_buff[1].ui_buffer_map);
	rgb_time = yuv_time = 0;
	return ret;
}
/*
close wimo_video .
release resources.

*/
static int wimo_video_close(struct wimo_video_info *video_info)
{
	int i ;
	int ret = 0;
	if(video_info->buffer_num && wimo.video_start_sign && wimo.start_sign)
		printk("something is okay with wimo video in close");
		
	else
		printk("somethint wrong with wimo video in close");
	wimo.video_start_sign = 0;
	DLOG("video_info->dst_width %d video_info->dst_height %d ui_buffer %x %x  ui_buffer_map %x %x\n",
		video_info->dst_width,video_info->dst_height,video_info->enc_buff[0].ui_buffer,video_info->enc_buff[1].ui_buffer,
		video_info->enc_buff[0].ui_buffer_map,video_info->enc_buff[1].ui_buffer_map);
	
	for(i = 0; i < 4; i++)
	{	
		DLOG("closebuf ui_buffer %x ui_buffer_map %x  ui_buffer_map addr %x wimo_info.buffer_num %d i %d\n",
			(unsigned int)video_info->enc_buff[i].ui_buffer,(unsigned int)video_info->enc_buff[i].ui_buffer_map,(unsigned long)(&video_info->enc_buff[i].ui_buffer_map),video_info->buffer_num,i);
		if(video_info->enc_buff[i].ui_buffer_map!=0)
		{
			iounmap(video_info->enc_buff[i].ui_buffer_map);
			video_info->enc_buff[i].ui_buffer_map = 0;
			video_info->enc_buff[i].ui_buffer = 0;
		}
	}
	return ret;
}

/*
get video frame 
it return -1  when no frame is availiable
*/
static int wimo_video_find_frame(unsigned long* temp_data,struct wimo_video_info *video_info)
{
	
	int ret = 0;
	do_gettimeofday(&video_info->Wimo_Cur_time);
	DLOG("wimo_find_frame video_info %x video_info->buffer_num %d avail_index %d avail_frame %d work_index %d  testIgn %d time %lld curtime %lld last_time %lld %lld %lld yuv rgb time %lld %lld  yuv_time %d\n",
		(unsigned long)video_info,video_info->buffer_num, video_info->avail_index,video_info->avail_frame,video_info->work_index,video_info->test_sign,video_info->enc_buff[video_info->avail_index].Frame_Time,
		(long long)video_info->Wimo_Cur_time.tv_sec * 1000000ll + video_info->Wimo_Cur_time.tv_usec,video_info->Last_Frame_Time,video_info->enc_buff[video_info->avail_index].Frame_Time,
		video_info->enc_buff[1-video_info->avail_index].Frame_Time,(long long)video_info->Wimo_YUV_time.tv_sec * 1000000ll + video_info->Wimo_YUV_time.tv_usec
		,(long long)video_info->Wimo_RGB_time.tv_sec * 1000000ll + video_info->Wimo_RGB_time.tv_usec,yuv_time);
	if(video_info->avail_frame)
	{
		if(video_info->Last_Frame_Time >  video_info->enc_buff[video_info->avail_index].Frame_Time)
		{
			
			DLOG("wimo_find_frame error avail_index %d avail_frame %d work_index %d  testIgn %d time %lld curtime %lld last_time %lld %lld %lld yuv rgb time %lld %lld  yuv_time %d\n",
				video_info->avail_index,video_info->avail_frame,video_info->work_index,video_info->test_sign,video_info->enc_buff[video_info->avail_index].Frame_Time,
			(long long)video_info->Wimo_Cur_time.tv_sec * 1000000ll + video_info->Wimo_Cur_time.tv_usec,video_info->Last_Frame_Time,video_info->enc_buff[video_info->avail_index].Frame_Time,
				video_info->enc_buff[1-video_info->avail_index].Frame_Time,(long long)video_info->Wimo_YUV_time.tv_sec * 1000000ll + video_info->Wimo_YUV_time.tv_usec
				,(long long)video_info->Wimo_RGB_time.tv_sec * 1000000ll + video_info->Wimo_RGB_time.tv_usec,yuv_time);
			video_info->test_sign = 2;
		}
		video_info->Last_Frame_Time = video_info->enc_buff[video_info->avail_index].Frame_Time;
		
	}
	else
	{
		if(((long long)video_info->Wimo_Cur_time.tv_sec * 1000000ll + video_info->Wimo_Cur_time.tv_usec  >   video_info->Last_Frame_Time + 1000000ll || video_info->frame_num < 30) && video_info->frame_num!=0)
		{
			
			DLOG("wimo_find_frame wait  second avail_index %d avail_frame %d work_index %d  testIgn %d time %lld curtime %lld last_time %lld %lld %lld yuv rgb time %lld %lld  yuv_time %d\n",
				video_info->avail_index,video_info->avail_frame,video_info->work_index,
				video_info->test_sign,video_info->enc_buff[video_info->avail_index].Frame_Time,
			(long long)video_info->Wimo_Cur_time.tv_sec * 1000000ll + video_info->Wimo_Cur_time.tv_usec,video_info->Last_Frame_Time,video_info->enc_buff[video_info->avail_index].Frame_Time,
				video_info->enc_buff[1- video_info->avail_index].Frame_Time,(long long)video_info->Wimo_YUV_time.tv_sec * 1000000ll + video_info->Wimo_YUV_time.tv_usec
				,(long long)video_info->Wimo_RGB_time.tv_sec * 1000000ll + video_info->Wimo_RGB_time.tv_usec,yuv_time);
			video_info->test_sign = 3;
			video_info->Last_Frame_Time =(long long)video_info->Wimo_Cur_time.tv_sec * 1000000ll + video_info->Wimo_Cur_time.tv_usec;
			if(video_info->mode == 0 )
			{
				video_info->Wimo_RGB_time.tv_sec = video_info->Wimo_Cur_time.tv_sec;
				video_info->Wimo_RGB_time.tv_usec = video_info->Wimo_Cur_time.tv_usec;
			}
			
			video_info->avail_index--;
			video_info->avail_index %= video_info->buffer_num;
			video_info->avail_frame++;
			
		}
		else
		{
			video_info->test_sign = 4;
			DLOG ("no avail frame \n");
			return -1;
		}
		
	}
	temp_data[0] = (video_info->enc_buff[video_info->avail_index].bitperpixel << 16);
	temp_data[0] |= video_info->enc_buff[video_info->avail_index].mode;
	
	temp_data[1] = video_info->avail_index ;
	memcpy(&temp_data[2],&video_info->enc_buff[video_info->avail_index].Frame_Time,8);
	temp_data[4] = video_info->enc_buff[video_info->avail_index].rotation_flag| (yuv_time<<16);
	temp_data[5] = video_info->Wimo_Cur_time.tv_sec;
	temp_data[6] = video_info->Wimo_Cur_time.tv_usec;

	video_info->avail_index++;
	video_info->avail_index %= video_info->buffer_num;
	video_info->avail_frame--;
	video_info->frame_num++;
	yuv_time = rgb_time = 0;
	
	return ret;
}

#if 1
 int wimo_prepare_para(struct fb_info *info,struct wimo_video_info *video_info,struct enc_buff_info *enc_buff)
{
	
	//struct rk29fb_inf *inf = dev_get_drvdata(info->device);
	struct rk_fb_inf *inf = dev_get_drvdata(info->device);
	struct fb_var_screeninfo *var = &info->var;
	int ret = 0;
	struct layer_par *par = NULL;	
	struct fb_fix_screeninfo * fix = &info->fix;
	struct rk_lcdc_device_driver *dev_drv = (struct rk_lcdc_device_driver *)info->par;		
	int layer_id = 0;		
	layer_id = get_fb_layer_id(fix);		
	if (layer_id < 0) 
	{			
		return -ENODEV;		
	} 
	else 
	{			
		par = dev_drv->layer_par[layer_id];		
	}
	video_info->mode = inf->video_mode;
	video_info->src_width =  var->xres_virtual;
	video_info->src_height = var->yres_virtual/2;
	if(inf->video_mode == 0 )
	{
		

		if(par->format == 1)
		{
			enc_buff->bitperpixel = 16;
			enc_buff->rotation_flag = video_info->rotation_flag;
		}
		else
		{
			enc_buff->bitperpixel =32;// wimo_info.bitperpixel_fb1;
			enc_buff->rotation_flag = video_info->rotation_flag + 8;
		}
		enc_buff->dst_width = video_info->dst_width;
		enc_buff->dst_height = video_info->dst_height;
		
		
		
		enc_buff->src_width = video_info->src_width;
		enc_buff->src_height = video_info->src_height;

		
		enc_buff->dst_vir_w = video_info->dst_width;
		enc_buff->dst_vir_h = video_info->dst_height;
		enc_buff->y_offset = enc_buff->c_offset = 0;
		enc_buff->mode = 0;
		
		enc_buff->src_y = info->fix.smem_start + par->y_offset;//info->screen_base + par->y_offset;//info_buffer[8];
		enc_buff->src_uv = info->fix.smem_start + par->y_offset + video_info->src_width * video_info->src_height;//dst_width*dst_height;//+ par->y_offset + dst_width*dst_height;//info_buffer[9];
		enc_buff->Frame_Time = (long long)video_info->Wimo_RGB_time.tv_sec * 1000000ll + video_info->Wimo_RGB_time.tv_usec;
		
		if(0)//rotation_sign == IPP_ROT_90  || rotation_sign == IPP_ROT_270)
		{
			if(enc_buff->dst_vir_w > enc_buff->dst_vir_h)
			{
				enc_buff->dst_height = enc_buff->dst_vir_h;
				enc_buff->dst_width = ((enc_buff->dst_vir_h * enc_buff->dst_vir_h) / enc_buff->dst_vir_w) & 0xfffc;
				if(par->format == 1)
					enc_buff->c_offset = enc_buff->y_offset = ((enc_buff->dst_vir_w - enc_buff->dst_width ) )  & 0xfffc; 

				else
				enc_buff->c_offset = enc_buff->y_offset = ((enc_buff->dst_vir_w - enc_buff->dst_width ) *2)  & 0xfffc; 
				
			}
			else
			{
				enc_buff->dst_width = enc_buff->dst_vir_w;
				enc_buff->dst_height = ((enc_buff->dst_vir_w * enc_buff->dst_vir_w) / enc_buff->dst_vir_h) ;
				enc_buff->y_offset = (enc_buff->dst_vir_h - enc_buff->dst_height ) *  enc_buff->dst_vir_w * 2; 
				enc_buff->c_offset = (enc_buff->dst_vir_h - enc_buff->dst_height ) * enc_buff->dst_vir_w ;  ; 
			}
		}
	//	printk("width height %d %d  vir_w %d vir_h %d y_offset %d c_offset %d rotation_sign %d\n",enc_buff->dst_width,enc_buff->dst_height,enc_buff->dst_vir_w,enc_buff->dst_vir_h,enc_buff->y_offset,
		//	enc_buff->c_offset,rotation_sign);
	//	printk("mode 0 enc_buff.src0.w %d %d enc_buff %x enc_buff->mode %d bit %d format %d\n",enc_buff->src_width,enc_buff->src_height,(unsigned long)enc_buff
		//	,enc_buff->mode,enc_buff->bitperpixel,par->format);

	}	
	DLOG(" test %d %d %d %d %d %d %x %x\n",enc_buff->dst_vir_w,enc_buff->dst_width,enc_buff->dst_vir_h,enc_buff->dst_height
		,video_info->Last_rotation_flag , enc_buff->rotation_flag,
		video_info->enc_buff[0].ui_buffer_map,video_info->enc_buff[1].ui_buffer_map);
	if((enc_buff->dst_vir_w != enc_buff->dst_width  || enc_buff->dst_vir_h != enc_buff->dst_height )   && video_info->Last_rotation_flag != enc_buff->rotation_flag)//wimo_info.Last_rotation_flag != enc_buff->rotation_sign)
	{
		int i;
		if(enc_buff->rotation_flag & 0x8)
		{
			for(i = 0; i < video_info->buffer_num; i++)
			{
				memset(video_info->enc_buff[i].ui_buffer_map,0,enc_buff->dst_vir_w * enc_buff->dst_vir_h * 4);//dst_width*dst_height);
			}
		}
		else if((enc_buff->rotation_flag & 0x8 )== 0)
		{
			for(i = 0; i < video_info->buffer_num; i++)
			{
				memset(video_info->enc_buff[i].ui_buffer_map,0,enc_buff->dst_vir_w * enc_buff->dst_vir_h * 2);//dst_width*dst_height);
			}
		}
	//	memset(ui_buffer_map,0,enc_buff->dst_vir_w * enc_buff->dst_vir_h * 4);//dst_width*dst_height);
		//memset(ui_buffer_map + enc_buff->dst_height * enc_buff->dst_width, 0x80,enc_buff->dst_height * enc_buff->dst_width / 2);//dst_width*dst_height,0x80,dst_width*dst_height/2);
	//	printk("rotation change wimo_info.Last_rotation_flag %d enc_buff->rotation_sign %d\n",wimo_info.Last_rotation_flag,enc_buff->rotation_sign);
	}
	video_info->Last_rotation_flag = enc_buff->rotation_flag;
	
	return ret;
}
 int wimo_prepare_buff(struct enc_buff_info *enc_buff)
{
	
	struct rk29_ipp_req overlay_req;
	struct rk29_ipp_req overlay_req_1;
	int err = 0;
	
	memset(&overlay_req, 0 , sizeof(overlay_req));
	memset(&overlay_req_1, 0 , sizeof(overlay_req_1));
	//printk("inf->video_mode %d\n",inf->video_mode);	
	if(enc_buff->mode == 0 )
	{
		
		overlay_req.src0.YrgbMst = enc_buff->src_y;//info->fix.smem_start + par->y_offset;//info->screen_base + par->y_offset;//info_buffer[8];
		overlay_req.src0.CbrMst =  enc_buff->src_uv;//info->fix.smem_start + par->y_offset + enc_buff->src_width * enc_buff->src_height;//dst_width*dst_height;//+ par->y_offset + dst_width*dst_height;//info_buffer[9];
		overlay_req.src0.w = enc_buff->src_width ;//dst_width;//info_buffer[2];
		overlay_req.src0.h = enc_buff->src_height ;//dst_height;//info_buffer[3];
		overlay_req.src0.fmt = (enc_buff->bitperpixel == 16) ? 1 : 0;//3;
		overlay_req.dst0.YrgbMst = (unsigned long)(enc_buff->ui_buffer + enc_buff->y_offset);//overlay_buffer + info_buffer[4] + info_buffer[5] * dst_width;
		overlay_req.dst0.CbrMst = (unsigned long)(enc_buff->ui_buffer + enc_buff->dst_vir_w * enc_buff->dst_height +  enc_buff->c_offset);//dst_width*dst_height;//(unsigned char*) overlay_buffer + dst_width*dst_height +info_buffer[4] + (  info_buffer[5] * dst_width) / 2;// info_buffer[6] * info_buffer[7];

		overlay_req.dst0.w = enc_buff->dst_width ;//dst_width;//info_buffer[6];
		overlay_req.dst0.h = enc_buff->dst_height ;//dst_height;//info_buffer[7];
		overlay_req.dst0.fmt = (enc_buff->bitperpixel== 16) ? 1 : 0;//3;3;
		overlay_req.deinterlace_enable = 0;
		overlay_req.timeout = 1000000;
		overlay_req.src_vir_w = enc_buff->src_width ;//dst_width;//info_buffer[2];
		overlay_req.dst_vir_w = enc_buff->dst_vir_w ;//dst_width; 

		
		//printk("mode 0 overlay_req.src0.w %d %d enc_buff %x\n",overlay_req.src0.w,overlay_req.src0.h,(unsigned long)enc_buff);
		overlay_req.flag = enc_buff->rotation_flag & 0x7;
		err = ipp_blit_sync(&overlay_req);
		if(err)
				goto WIMO_IPP_ERROR;
				//printk("mode 0err %d w h fmt time out %d %d %d %d",err,overlay_req.src0.w,overlay_req.src0.h,overlay_req.src0.fmt,overlay_req.timeout);
		//		printk("overlay_req.dst0.w %d overlay_req.dst0.h %d overlay_req.src0.w %d overlay_req.src0.h %d\n",overlay_req.dst0.w,overlay_req.dst0.h,overlay_req.src0.w,overlay_req.src0.h);

		
		
		
		
	}	
	else
	{

	
		if(enc_buff->xaff * 3 < enc_buff->xsize || enc_buff->yaff * 3 < enc_buff->ysize)// 3time or bigger scale up
		{
			unsigned long mid_width, mid_height;
			struct rk29_ipp_req overlay_req_1;
			if(enc_buff->xaff * 3 < enc_buff->xsize)
				mid_width = enc_buff->xaff * 3;
			else
				mid_width = enc_buff->xsize;
			
			if(enc_buff->yaff * 3 < enc_buff->ysize)
				mid_height =  enc_buff->yaff * 3;
			else
				mid_height = enc_buff->ysize;
			overlay_req.src0.YrgbMst = enc_buff->src_y;//info_buffer[8];
			overlay_req.src0.CbrMst = enc_buff->src_uv;//info_buffer[9];
			overlay_req.src0.w = enc_buff->xaff;// info_buffer[2];
			overlay_req.src0.h = enc_buff->yaff;// info_buffer[3];
			overlay_req.src0.fmt = 3;
			overlay_req.dst0.YrgbMst = (unsigned long)(enc_buff->ui_buffer + 2048000) ;//info_buffer[4] + info_buffer[5] * dst_width;   //小尺寸片源需要2次放大，所以将buffer的后半段用于缓存
			overlay_req.dst0.CbrMst = (unsigned long)( (unsigned char*) enc_buff->ui_buffer + mid_height * mid_width + 2048000);
		
			overlay_req.dst0.w = mid_width;//info_buffer[6];
			overlay_req.dst0.h = mid_height;//info_buffer[7];
			overlay_req.dst0.fmt = 3;
			overlay_req.timeout = 100000;
			overlay_req.src_vir_w = enc_buff->xaff;//info_buffer[2];
			overlay_req.dst_vir_w = mid_width;//dst_width;
			overlay_req.flag = IPP_ROT_0;




			overlay_req_1.src0.YrgbMst = (unsigned long)(enc_buff->ui_buffer + 2048000);
			overlay_req_1.src0.CbrMst = (unsigned long)((unsigned char*) enc_buff->ui_buffer + mid_height * mid_width + 2048000);
			overlay_req_1.src0.w = mid_width;
			overlay_req_1.src0.h = mid_height;// info_buffer[3];
			overlay_req_1.src0.fmt = 3;
			overlay_req_1.dst0.YrgbMst = (unsigned long)(enc_buff->ui_buffer + ((enc_buff->xpos + 1)&0xfffe) + enc_buff->ypos * enc_buff->dst_width);//info_buffer[4] + info_buffer[5] * dst_width;
			overlay_req_1.dst0.CbrMst = (unsigned long)((unsigned char*) enc_buff->ui_buffer + enc_buff->dst_width * enc_buff->dst_height + 
				((enc_buff->xpos + 1)&0xfffe) + ( enc_buff->ypos  / 2)* enc_buff->dst_width) ;
			
			overlay_req_1.dst0.w = enc_buff->xsize;//info_buffer[6];
			overlay_req_1.dst0.h = enc_buff->ysize;//info_buffer[7];
			overlay_req_1.dst0.fmt = 3;
			overlay_req_1.timeout = 100000;
			overlay_req_1.src_vir_w = mid_width;//info_buffer[2];
			overlay_req_1.dst_vir_w = enc_buff->dst_vir_w;//dst_width;
			overlay_req_1.flag = IPP_ROT_0;
			

			err = ipp_blit_sync(&overlay_req);
			if(err)
				goto WIMO_IPP_ERROR;
			dmac_flush_range(enc_buff->ui_buffer_map,enc_buff->ui_buffer_map + enc_buff->dst_height * enc_buff->dst_width * 3/2);//dst_width*dst_height*3/2);
			err = ipp_blit_sync(&overlay_req_1);
			if(err)
				goto WIMO_IPP_ERROR;
		//	printk("err1 %d w h fmt time out %d %d %d %d workindex %d encbuff %x xaff %d yaff %d xsize %d ysize %d",
			//	err,overlay_req.src0.w,overlay_req.src0.h,overlay_req.src0.fmt,overlay_req.timeout,wimo_info.work_index,(unsigned long)enc_buff
			//	,enc_buff->xaff,enc_buff->yaff,enc_buff->xsize,enc_buff->ysize);
		}
		else
		{
			overlay_req.src0.YrgbMst = enc_buff->src_y;//info_buffer[8];
			overlay_req.src0.CbrMst = enc_buff->src_uv;//info_buffer[9];
			overlay_req.src0.w = enc_buff->xaff;// info_buffer[2];
			overlay_req.src0.h = enc_buff->yaff;// info_buffer[3];
			overlay_req.src0.fmt = 3;
			overlay_req.dst0.YrgbMst = (unsigned long)(enc_buff->ui_buffer + ((enc_buff->xpos + 1)&0xfffe) + enc_buff->ypos * enc_buff->dst_width);//info_buffer[4] + info_buffer[5] * dst_width;
			overlay_req.dst0.CbrMst = (unsigned long)((unsigned char*) enc_buff->ui_buffer +  ((enc_buff->xpos + 1)&0xfffe) + 
				( enc_buff->ypos  / 2)* enc_buff->dst_width +  enc_buff->dst_width *  enc_buff->dst_height);
			
			overlay_req.dst0.w = enc_buff->xsize;//wimo_info.xsize;//wimo_info.xaff;// wimo_info.xsize;//info_buffer[6];
			overlay_req.dst0.h = (enc_buff->ysize + 1) & 0xfffe;//(wimo_info.ysize + 1) & 0xfffe;//wimo_info.yaff;//(wimo_info.ysize + 1) & 0xfffe;//info_buffer[7];
			overlay_req.dst0.fmt = 3;
			overlay_req.timeout = 100000;
			overlay_req.src_vir_w = (enc_buff->xaff + 15) & 0xfff0;//enc_buff->xaff;//info_buffer[2];
			overlay_req.dst_vir_w = enc_buff->dst_vir_w;//wimo_info.dst_width;//wimo_info.yaff;//wimo_info.dst_width;//dst_width;
			overlay_req.flag = IPP_ROT_0;
			
			
			dmac_flush_range(enc_buff->ui_buffer_map,enc_buff->ui_buffer_map + enc_buff->dst_height *enc_buff->dst_width * 3/2);//dst_width*dst_height*3/2);
			err = ipp_blit_sync(&overlay_req);
			//printk("mode 1 overlay_req.src0.w %d %d overlay_req.dst0.w %d %d overlay_req.src_vir_w %d %d overlay_req.src0.YrgbMst %x %x overlay_req.dst0.YrgbMst %x %x ui_buffer %x pixel%x %x   %x  %x\n",
			//	overlay_req.src0.w,overlay_req.src0.h,overlay_req.dst0.w,overlay_req.dst0.h,overlay_req.src_vir_w ,overlay_req.dst_vir_w,overlay_req.src0.YrgbMst,overlay_req.src0.CbrMst,
			//	overlay_req.dst0.YrgbMst,overlay_req.dst0.CbrMst,ui_buffer,ui_buffer_map[0],ui_buffer_map[1],ui_buffer_map[1024],ui_buffer_map[1025]);
			if(err)
				goto WIMO_IPP_ERROR;
			
				//printk("err %d w h fmt time out %d %d %d %d",err,overlay_req.src0.w,overlay_req.src0.h,overlay_req.src0.fmt,overlay_req.timeout);
		}
	}
	
	
       
	return 0;
WIMO_IPP_ERROR:
	
	return -1;
}

int wimo_video_prepare(struct fb_info *info,u32 yuv_phy[2])
{
	int ret = 0;
	static int comin_time = 0;
	if(wimo_count < 1)
		return WIMO_COUNT_ZERO;
	mutex_lock(&video_lock);
	DLOG("before wimo prepare para wimo_count %d wimo.video_start_sign %d  %d %d %x %x\n"
			,wimo_count,wimo.video_start_sign,wimo.start_sign,wimo.video_info.avail_frame,wimo.video_info.buffer_num,(long)&wimo.video_info.buffer_num);
	if(wimo.video_start_sign && wimo.start_sign)
	{
		struct wimo_video_info *video_info = &wimo.video_info;
		struct enc_buff_info *enc_buff = &video_info->enc_buff[video_info->work_index];	
		//struct rk29fb_inf *inf = dev_get_drvdata(info->device);
		struct rk_fb_inf *inf = dev_get_drvdata(info->device);
		int ret;
		if(comin_time == 1)
		{
			printk("ENCODER_BUFFER_FULL wimo.video_info.avail_frame %d wimo.video_info.buffer_num %d\n",wimo.video_info.avail_frame,wimo.video_info.buffer_num);
		//	ret = ENCODER_BUFFER_FULL;
		//	goto buffer_full;
		}
		comin_time++;
		if((wimo.video_info.avail_frame>=wimo.video_info.buffer_num-1) )
		{
			DLOG("ENCODER_BUFFER_FULL wimo.video_info.avail_frame %d wimo.video_info.buffer_num %d\n",wimo.video_info.avail_frame,wimo.video_info.buffer_num);
			ret = ENCODER_BUFFER_FULL;
			goto buffer_full;
		}
		
		if(inf->video_mode == 0)
		{
				do_gettimeofday(&wimo.video_info.Wimo_RGB_time);
				rgb_time++;
		}
		else
		{	
			printk("videomode = %d err \n",inf->video_mode);
		}
		
		
		wimo_prepare_para(info,video_info,enc_buff);
		ret = wimo_prepare_buff(enc_buff);
		if(ret == 0)
		{
			video_info->avail_frame++;
			//wimo_info.avail_frame%=(wimo_info.buffer_num + 1);
			video_info->work_index++;
			video_info->work_index%= video_info->buffer_num;
			DLOG("prepare video_info->avail_frame %d video_info->work_index %d video_info->avail_index %d\n",video_info->avail_frame,video_info->work_index,video_info->avail_index);
	//		if(((wimo_info.avail_index + wimo_info.avail_frame) % wimo_info.buffer_num)!=wimo_info.work_index)
			//	DLOG("wimo_prepare_buff 2 test_sign %d avail_index %d avail_frame %d work_index %d mode %d %d time %lld %lld  bitperpixel %d src_width %d src_height %d dst_width %d dst_height %d dst_y %x dst_uv %x\n",
			//	wimo_info.test_sign,wimo_info.avail_index,wimo_info.avail_frame,wimo_info.work_index,enc_buff[wimo_info.avail_index].mode,enc_buff[1-wimo_info.avail_index].mode,
			//	wimo_info.enc_buff[wimo_info.avail_index].Frame_Time,
			//		wimo_info.enc_buff[1-wimo_info.avail_index].Frame_Time,	
			//		enc_buff->bitperpixel,enc_buff->src_width,enc_buff->src_height,enc_buff->dst_width,enc_buff->dst_height
			//		,overlay_req.dst0.YrgbMst,overlay_req.dst0.CbrMst);
		}
		else
		{
			DLOG("Wimo_IPP_ERROR err %d  enc_buff.src0.w %x %x enc_buff %x enc_buff->mode %x format %x workindex %x xaff %x yaff %x xsize %x ysize %x yuv_num %d %x %x %x %x\n"
				,ret,enc_buff->src_width,enc_buff->src_height,(u32)enc_buff,enc_buff->mode,enc_buff->bitperpixel,video_info->work_index,
				enc_buff->xaff,enc_buff->yaff,enc_buff->xsize,
				enc_buff->ysize,yuv_time,enc_buff->ui_buffer_map[0],enc_buff->ui_buffer_map[1],enc_buff->ui_buffer_map[2],enc_buff->ui_buffer_map[3]);
		}
	}
	else
	{
		//DLOG("wimo_video not open\n");
		ret = VIDEO_ENCODER_CLOSED;
	}
	mutex_unlock(&video_lock);
	
	return ret;
buffer_full:
	mutex_unlock(&video_lock);
	return ret;
}
#endif


static int wimo_open(struct inode *inode, struct file *file)
{
    int ret = 0;

    DLOG("wimo_open current %u \n", current->pid);
    
    return ret;
}

static int wimo_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0;
	DLOG("wimo_mmap do  nothing current->pid %u\n",current->pid);
//error:
	return ret;
}

static int wimo_release(struct inode *inode, struct file *file)
{
    DLOG("wimo_release do  nothing current->pid %u\n",current->pid);


    return 0;
}

static long wimo_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long  ret = 0;
//	DLOG("wimo_ioctl cmd %d \n",cmd);
	switch (cmd) {
	case  WIMO_START:	
		{
			{
				
				mutex_lock(&wimo_lock);
				wimo_start();
				mutex_unlock(&wimo_lock);
				printk("wimo_ioctl WIMO_START wimo.start_sign %d\n",wimo.start_sign);
			}
		}
		break;
	case  WIMO_STOP:
		{
			{
				mutex_lock(&wimo_lock);
				wimo_stop();
				mutex_unlock(&wimo_lock);
				printk("wimo_ioctl WIMO_STOP wimo.start_sign %d\n",wimo.start_sign);
			}
			{
				
			}
		}
		break;
	case  WIMO_SET_ROTATION:			
		break;
	case  WIMO_VIDEO_OPEN:		
		
		{
			unsigned long temp[6];
			
			if (copy_from_user(temp, (void*)arg, 24))
				return -EFAULT;
			mutex_lock(&video_lock);
			ret = wimo_video_open(temp);
			mutex_unlock(&video_lock);
		}
		
		break;
	case  WIMO_VIDEO_CLOSE:	
		
		{
			mutex_lock(&video_lock);
			ret = wimo_video_close(&wimo.video_info);
			mutex_unlock(&video_lock);
		}
		
		break;
	case  WIMO_VIDEO_GET_BUF:		
		
		{
			
			unsigned long temp_data[8];
			mutex_lock(&video_lock);
			
			if(wimo.video_start_sign == 1 && wimo.start_sign == 1 && wimo.video_info.buffer_num)
			{
				
				ret = wimo_video_find_frame(temp_data,&wimo.video_info);
				if(ret == 0)
				{
					yuv_time =	rgb_time = 0;
				}
			}	
			else
			{
				ret = -1111;
				DLOG("WIMO_VIDEO_GET_BUF starg_sign %d start_video_sign %d",wimo.start_sign,wimo.video_start_sign);
			}
			mutex_unlock(&video_lock);
			if (copy_to_user((void*)arg, temp_data, 28))
				return -EFAULT;
		}
		
		break;
		
	case  WIMO_AUDIO_OPEN:		
		
		{
			unsigned long temp[6];
			struct wimo_audio_info *audio_info = &wimo.audio_info;
			if (copy_from_user(temp, (void*)arg, 24))
				return -EFAULT;
			mutex_lock(&audio_lock);
			ret = wimo_audio_open(temp);
			mutex_unlock(&audio_lock);
			printk(" wimo_audio_open ret %daudio_info->Out_Buffer	 %x  temp[0] %x audio_info->audio_data %x audio_info->time_stamp  %x  len = 3",
				ret,(unsigned long)audio_info->Out_Buffer	,(unsigned long)(temp[0]),audio_info->audio_data,audio_info->time_stamp );
		}
		
		break;
	case  WIMO_AUDIO_CLOSE:	
		
		{
			printk("WIMO_AUDIO_CLOSE\n");
			mutex_lock(&audio_lock);
			ret = wimo_audio_close();
			mutex_unlock(&audio_lock);
		}
		
		break;
	case  WIMO_AUDIO_SET_PARA:	
		
		{
			struct wimo_audio_param temp_data;
			if (copy_from_user((void*)(&temp_data), (void*)arg, sizeof(struct wimo_audio_param)))
				return -EFAULT;
			mutex_lock(&audio_lock);
			wimo_audio_set_para(&temp_data);
			mutex_unlock(&audio_lock);
		}
		
		break;
	case  WIMO_AUDIO_SET_VOL:	
		
		{
			unsigned long temp_data;
			
			if (copy_from_user(&temp_data, (void*)arg, 4))
				return -EFAULT;
			//printk("WIMO_AUDIO_SET_VOL set vol %d\n",temp_data);
			mutex_lock(&audio_lock);
			wimo.volumn_open_sign = temp_data;
			mutex_unlock(&audio_lock);
		}
		
		break;	
	case  WIMO_AUDIO_GET_VOL:
		{
			//printk("WIMO_AUDIO_SET_VOL get vol %d\n",wimo.volumn_open_sign);	
			if(copy_to_user((void*)arg,&wimo.volumn_open_sign,4))
				return -EFAULT;
		}
		break;
	case  WIMO_AUDIO_GET_BUF:		
		
		{
			struct wimo_audio_info *audio_info = &wimo.audio_info;
			mutex_lock(&audio_lock); 
			if(wimo.audio_start_sign == 1 && wimo.start_sign == 1 && wimo.audio_info.buffer_size)
			{
				if(audio_info->data_len >= 1)
				{
					struct timeval timeFirst;
					do_gettimeofday(&timeFirst);	
					audio_info->Out_Buffer = (void*)(&audio_info->audio_data[audio_info->head_offset * audio_info->nBytePerFrame]);
					
					
					wimo_filp_output->f_op->write(wimo_filp_output, audio_info->Out_Buffer, audio_info->nBytePerFrame, &wimo_filp_output->f_pos);
					if (copy_to_user((void*)arg,audio_info->Out_Buffer,  4096))
						ret = -EFAULT;
					if (copy_to_user((void*)arg+4096,(void*)(&audio_info->time_stamp[audio_info->head_offset]),  8))
						ret = -EFAULT;
					if (copy_to_user((void*)arg+4104,&timeFirst,  8))
						ret = -EFAULT;
					if (copy_to_user((void*)arg+4112,&audio_info->head_offset,  8))
						ret = -EFAULT;
					audio_info->data_len --;
					audio_info->head_offset ++;
					audio_info->head_offset %= audio_info->buffer_size;
				}
				else
				{
					ret = -1;
				}
			}	
			else
			{
				ret = -1111;
			}
			
			mutex_unlock(&audio_lock);
		}
		
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

struct file_operations wimo_fops = {
	.open = wimo_open,
	.mmap = wimo_mmap,
	.unlocked_ioctl = wimo_ioctl,
	.release = wimo_release,
};

#if WIMO_DEBUG
static ssize_t debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debug_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	wdm_region *region, *tmp_region;
	const int debug_bufmax = 4096;
	static char buffer[4096];
	int n = 0;

	DLOG("debug open\n");
	n = scnprintf(buffer, debug_bufmax,
		      "pid #: mapped regions (offset, len, used, post) ...\n");
	down_read(&wdm_rwsem);
     {
        n += scnprintf(buffer + n, debug_bufmax - n,
                "(%d,%d,%d,%d) ",
                region->index, region->pfn, region->used, region->post);
	}
	up_read(&wdm_rwsem);
	n++;
	buffer[n] = 0;
	return simple_read_from_buffer(buf, count, ppos, buffer, n);
}

static struct file_operations debug_fops = {
	.read = debug_read,
	.open = debug_open,
};
#endif

int wimo_setup(struct wimo_platform_data *pdata)
{
	int err = 0;
    DLOG("wimo_setup pdata->name %s\n",pdata->name);
    if (wimo_count) {
		printk(KERN_ALERT "Only one wimo driver can be register!\n");
        goto err_cant_register_device;
    }

    memset(&wimo, 0, sizeof(struct wimo_info));

    wimo.rotation_flag = IPP_ROT_0;  	

    init_rwsem(&wdm_rwsem);
    wimo.dev.name = pdata->name;
    wimo.dev.minor = MISC_DYNAMIC_MINOR;
    wimo.dev.fops = &wimo_fops;
   
    err = misc_register(&wimo.dev);
    if (err) {
        printk(KERN_ALERT "Unable to register wimo driver!\n");
        goto err_cant_register_device;
    }
    mutex_init(&video_lock);	
    mutex_init(&audio_lock);		
	mutex_init(&wimo_lock);
    audio_data_to_wimo = wimo_audio_prepare;
    video_data_to_wimo = wimo_video_prepare;
    printk("set audio_data_to_wimo %x %x\n",(unsigned long)wimo_audio_prepare,(unsigned long)audio_data_to_wimo);
    #if WIMO_DEBUG
    debugfs_create_file(pdata->name, S_IFREG | S_IRUGO, NULL, (void *)wimo.dev.minor,
                        &debug_fops);
    #endif
    printk("%s: %d initialized\n", pdata->name, wimo.dev.minor);
    wimo_count++;
	return 0;
  
//err_no_mem_for_metadata:
	misc_deregister(&wimo.dev);
err_cant_register_device:
	return -1;
}

static int wimo_probe(struct platform_device *pdev)
{
	struct wimo_platform_data *pdata;
	printk("wimo_probe\n");
	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to probe wimo!\n");
		return -1;
	}
	pdata = pdev->dev.platform_data;
	return wimo_setup(pdata);
}

static int wimo_remove(struct platform_device *pdev)
{
    if (!pdev || !pdev->dev.platform_data) {
    	printk(KERN_ALERT "Unable to remove wimo!\n");
    	return -1;
    }
	mutex_destroy(&wimo_lock);
    mutex_destroy(&audio_lock);	
    mutex_destroy(&video_lock);	
    if (wimo_count) {
	    misc_deregister(&wimo.dev);
        wimo_count--;
    } else {
		printk(KERN_ALERT "no wimo to remove!\n");
    }
	return 0;
}

static struct platform_driver wimo_driver = {
	.probe  = wimo_probe,
	.remove = wimo_remove,
	.driver = {.name = "wimo"}
	
};


static int __init wimo_init(void)
{
	printk("wimo_init\n");
	return platform_driver_register(&wimo_driver);
}

static void __exit wimo_exit(void)
{
	platform_driver_unregister(&wimo_driver);
}

module_init(wimo_init);
module_exit(wimo_exit);

#if	0//def CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_wimo_show(struct seq_file *s, void *v)
{
	if (wimo_count) {
		seq_printf(s, "wimo opened\n");
	} else {
		seq_printf(s, "wimo closed\n");
        return 0;
	}

    down_read(&wdm_rwsem);
    	{
    	}

    up_read(&wdm_rwsem);
    return 0;
}

static int proc_wimo_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_wimo_show, NULL);
}

static const struct file_operations proc_wimo_fops = {
	.open		= proc_wimo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init wimo_proc_init(void)
{
	proc_create("wimo", 0, NULL, &proc_wimo_fops);
	return 0;

}
late_initcall(wimo_proc_init);
#endif /* CONFIG_PROC_FS */

