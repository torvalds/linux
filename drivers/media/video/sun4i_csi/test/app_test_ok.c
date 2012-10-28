/*
 * drivers/media/video/sun4i_csi/test/app_test_ok.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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


//#加了点注释

//#Rockie Cheng

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>
#include <linux/videodev2.h>
#include <time.h>
#include <linux/fb.h>
//#include "../../../../video/sunxi/drv_display.h"//modify this
#include "./../../../../video/sunxi/drv_display.h"//modify this

//#define READ_NUM 5000
#define DISPLAY
#define LCD_WIDTH		800
#define LCD_HEIGHT	480

#define CLEAR(x) memset (&(x), 0, sizeof (x))

int count;

struct buffer {
        void *                  start;
        size_t                  length;
};

struct size{
	int width;
	int height;
};

static char *           dev_name        = "/dev/video0";//摄像头设备名
static int              fd              = -1;
struct buffer *         buffers         = NULL;
static unsigned int     n_buffers       = 0;

FILE *file_fd;
static unsigned long file_length;
static unsigned char *file_name;

int disphd;
unsigned int hlay;
int sel = 0;//which screen 0/1
__disp_layer_info_t layer_para;
__disp_video_fb_t video_fb;
__u32 arg[4];

//struct timeval time_test;
//struct timezone tz;

struct size input_size;
struct size disp_size;
int  csi_format;
__disp_pixel_fmt_t  disp_format;
__disp_pixel_mod_t  disp_mode;
__disp_pixel_seq_t	disp_seq;
int	 read_num=100;
int  test_num=10;
int  req_frame_num;
int	 fps=30;
int	 fps_test=0;
int	 invalid_ops=0;
int  invalid_fmt_test=0;
int	 control_test=0;
int  ioctl_test=0;
int	 lost_frame_test=0;
struct test_case{
		int 							  input_width;
		int									input_height;
		int 							  disp_width;
		int									disp_height;
		int 								csi_format;
		__disp_pixel_fmt_t 	disp_format;
		__disp_pixel_mod_t	disp_mode;
		__disp_pixel_seq_t	disp_seq;
};

struct test_case test_case_set[]={
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV420,
		.disp_format	= DISP_FORMAT_YUV420,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 328,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 248,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 312,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 232,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 648,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 488,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 632,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 472,
		.csi_format   = V4L2_PIX_FMT_YUV422P,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUV420,
		.disp_format	= DISP_FORMAT_YUV420,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_NV16,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_UV_COMBINED,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_NV12,
		.disp_format	= DISP_FORMAT_YUV420,
		.disp_mode		= DISP_MOD_NON_MB_UV_COMBINED,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUV420,
		.disp_format	= DISP_FORMAT_YUV420,
		.disp_mode		= DISP_MOD_NON_MB_PLANAR,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_NV16,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_NON_MB_UV_COMBINED,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_NV12,
		.disp_format	= DISP_FORMAT_YUV420,
		.disp_mode		= DISP_MOD_NON_MB_UV_COMBINED,
		.disp_seq			= DISP_SEQ_UVUV,
	},
	{
		.input_width  = 320,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 328,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 320,
		.input_height = 248,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 312,
		.input_height = 240,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 320,
		.input_height = 232,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 648,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 640,
		.input_height = 488,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 632,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 640,
		.input_height = 472,
		.csi_format   = V4L2_PIX_FMT_YUYV,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YUYV,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_YVYU,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_YVYU,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_UYVY,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_UYVY,
	},
	{
		.input_width  = 640,
		.input_height = 480,
		.csi_format   = V4L2_PIX_FMT_VYUY,
		.disp_format	= DISP_FORMAT_YUV422,
		.disp_mode		= DISP_MOD_INTERLEAVED,
		.disp_seq			= DISP_SEQ_VYUY,
	},
};

enum v4l2_ctrl_type qc_ctrl[]=
{
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_HUE,
	V4L2_CID_VFLIP,
	V4L2_CID_HFLIP,
	V4L2_CID_GAIN,
	V4L2_CID_AUTOGAIN,
	V4L2_CID_EXPOSURE,
	V4L2_CID_EXPOSURE_AUTO,
	V4L2_CID_DO_WHITE_BALANCE,
	V4L2_CID_AUTO_WHITE_BALANCE,
	(V4L2_CID_BASE+31)
};

//////////////////////////////////////////////////////
//获取一帧数据
//////////////////////////////////////////////////////
static int read_frame (void)
{
	struct v4l2_buffer buf;
	unsigned int i;


	CLEAR (buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	ioctl (fd, VIDIOC_DQBUF, &buf); //出列采集的帧缓冲


	if((fps_test==1)||(lost_frame_test==1))
	    printf("process image %d sec %d usec\n",buf.timestamp.tv_sec,buf.timestamp.tv_usec);


	assert (buf.index < n_buffers);
//	printf ("buf.index dq is %d,\n",buf.index);
//	printf ("buf.m.offset = 0x%x\n",buf.m.offset);
	//disp_set_addr(320,240,&buf.m.offset);
	disp_set_addr(disp_size.width, disp_size.height,&buf.m.offset);

	//printf ("press ENTER to continue!\n");
	//getchar();
	//fwrite(buffers[buf.index].start, buffers[buf.index].length, 1, file_fd); //将其写入文件中



	if(lost_frame_test==1)
	{
		if(count%31==0)
		{
			printf("count = %d,delay\n",count);
			for(i=0;i<0x1ffffff;i++)
			{


			}
		}
	}

	ioctl (fd, VIDIOC_QBUF, &buf); //再将其入列

	return 1;
}

int disp_int(int w,int h)
{
	/*display start*/
    //unsigned int h,w;
    __u32 id = 0;

    //h= 480;
    //w= 640;

	if((disphd = open("/dev/disp",O_RDWR)) == -1)
	{
		printf("open file /dev/disp fail. \n");
		return 0;
	}

    arg[0] = 0;
    ioctl(disphd, DISP_CMD_LCD_ON, (void*)arg);

    //layer0
    arg[0] = 0;
    arg[1] = DISP_LAYER_WORK_MODE_SCALER;
    hlay = ioctl(disphd, DISP_CMD_LAYER_REQUEST, (void*)arg);
    if(hlay == 0)
    {
        printf("request layer0 fail\n");
        return 0;
    }
	printf("video layer hdl:%d\n", hlay);

    layer_para.mode = DISP_LAYER_WORK_MODE_SCALER;
    layer_para.pipe = 0;
    layer_para.fb.addr[0]       = 0;//your Y address,modify this
    layer_para.fb.addr[1]       = 0; //your C address,modify this
    layer_para.fb.addr[2]       = 0;
    layer_para.fb.size.width    = w;
    layer_para.fb.size.height   = h;
    layer_para.fb.mode          = disp_mode;///DISP_MOD_INTERLEAVED;//DISP_MOD_NON_MB_PLANAR;//DISP_MOD_NON_MB_PLANAR;//DISP_MOD_NON_MB_UV_COMBINED;
    layer_para.fb.format        = disp_format;//DISP_FORMAT_YUV420;//DISP_FORMAT_YUV422;//DISP_FORMAT_YUV420;
    layer_para.fb.br_swap       = 0;
    layer_para.fb.seq           = disp_seq;//DISP_SEQ_UVUV;//DISP_SEQ_YUYV;//DISP_SEQ_YVYU;//DISP_SEQ_UYVY;//DISP_SEQ_VYUY//DISP_SEQ_UVUV
    layer_para.ck_enable        = 0;
    layer_para.alpha_en         = 1;
    layer_para.alpha_val        = 0xff;
    layer_para.src_win.x        = 0;
    layer_para.src_win.y        = 0;
    layer_para.src_win.width    = w;
    layer_para.src_win.height   = h;
    layer_para.scn_win.x        = 0;
    layer_para.scn_win.y        = 0;
    layer_para.scn_win.width    = LCD_WIDTH;//800;
    layer_para.scn_win.height   = LCD_HEIGHT;//480;
	arg[0] = sel;
    arg[1] = hlay;
    arg[2] = (__u32)&layer_para;
    ioctl(disphd,DISP_CMD_LAYER_SET_PARA,(void*)arg);
#if 0
    arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd,DISP_CMD_LAYER_TOP,(void*)arg);
#endif
    arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd,DISP_CMD_LAYER_OPEN,(void*)arg);

#if 1
	int fb_fd;
	unsigned long fb_layer;
	void *addr = NULL;
	fb_fd = open("/dev/fb0", O_RDWR);
	if (ioctl(fb_fd, FBIOGET_LAYER_HDL_0, &fb_layer) == -1) {
		printf("get fb layer handel\n");
	}

	addr = malloc(LCD_WIDTH*LCD_HEIGHT*3);
	memset(addr, 0xff, LCD_WIDTH*LCD_HEIGHT*3);
	write(fb_fd, addr, LCD_WIDTH*LCD_HEIGHT*3);
	//memset(addr, 0x12, 800*480*3);

	printf("fb_layer hdl: %ld\n", fb_layer);
	close(fb_fd);

	arg[0] = 0;
	arg[1] = fb_layer;
	ioctl(disphd, DISP_CMD_LAYER_BOTTOM, (void *)arg);
#endif
}

void disp_start(void)
{
	arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd, DISP_CMD_VIDEO_START,  (void*)arg);
}

void disp_stop(void)
{
	arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd, DISP_CMD_VIDEO_STOP,  (void*)arg);
}

int disp_on()
{
		arg[0] = 0;
    ioctl(disphd, DISP_CMD_LCD_ON, (void*)arg);
}

int disp_set_addr(int w,int h,int *addr)
{
#if 0
	layer_para.fb.addr[0]       = *addr;//your Y address,modify this
    layer_para.fb.addr[1]       = *addr+w*h; //your C address,modify this
    layer_para.fb.addr[2]       = *addr+w*h*3/2;

    arg[0] = sel;
    arg[1] = hlay;
    arg[2] = (__u32)&layer_para;
    ioctl(disphd,DISP_CMD_LAYER_SET_PARA,(void*)arg);
#endif
	__disp_video_fb_t  fb_addr;
	memset(&fb_addr, 0, sizeof(__disp_video_fb_t));

	fb_addr.interlace       = 0;
	fb_addr.top_field_first = 0;
	fb_addr.frame_rate      = 25;
	fb_addr.addr[0] = *addr;
//	fb_addr.addr[1] = *addr + w * h;
//	fb_addr.addr[2] = *addr + w*h*3/2;


	switch(csi_format){
		case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_VYUY:
    	fb_addr.addr[1]       = *addr+w*h; //your C address,modify this
    	fb_addr.addr[2]       = *addr+w*h*3/2;
    	break;
    case V4L2_PIX_FMT_YUV420:
    	fb_addr.addr[1]       = *addr+w*h; //your C address,modify this
    	fb_addr.addr[2]       = *addr+w*h*5/4;
    	break;
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_HM12:
    	fb_addr.addr[1]       = *addr+w*h; //your C address,modify this
    	fb_addr.addr[2]       = layer_para.fb.addr[1];
    	break;

    default:
    	printf("csi_format is not found!\n");
    	break;

  	}

  	fb_addr.id = 0;  //TODO

    arg[0] = sel;
    arg[1] = hlay;
    arg[2] = (__u32)&fb_addr;
    ioctl(disphd, DISP_CMD_VIDEO_SET_FB, (void*)arg);
}

int disp_quit()
{
	__u32 arg[4];
	arg[0] = 0;
    ioctl(disphd, DISP_CMD_LCD_OFF, (void*)arg);

    arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd, DISP_CMD_LAYER_CLOSE,  (void*)arg);

    arg[0] = sel;
    arg[1] = hlay;
    ioctl(disphd, DISP_CMD_LAYER_RELEASE,  (void*)arg);
    close (disphd);
}

int main_test (void)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	unsigned int i;
	enum v4l2_buf_type type;
	struct v4l2_cropcap cropcap;

	//fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);//打开设备
	//fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);//打开设备
	//close (fd);
	fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);//打开设备

	if(invalid_ops)
	{
		if(-1 == ioctl (fd, 0xff, &cropcap))
			printf("invalid_ops return error\n");
	}
	if(ioctl_test==1)
	{
			//Test VIDIOC_QUERYCAP
			if (-1 == ioctl (fd, VIDIOC_QUERYCAP, &cap))//获取摄像头参数
				printf("VIDIOC_QUERYCAP error!\n");

			printf("cap.driver=%s\n",cap.driver);
			printf("cap.card=%s\n",cap.card);
			printf("cap.bus_info=%s\n",cap.bus_info);
			printf("cap.version=%d\n",cap.version);
			printf("cap.capabilities=%d\n",cap.capabilities);

		//Test VIDIOC_ENUMINPUT,VIDIOC_S_INPUT,VIDIOC_G_INPUT
			struct v4l2_input inp;

			for(i=0;i<2;i++)
			{
				inp.index = i;
				if (-1 == ioctl (fd, VIDIOC_ENUMINPUT, &inp))//获取输入参数
					printf("VIDIOC_ENUMINPUT error!\n");
				if (inp.type == V4L2_INPUT_TYPE_CAMERA)
					printf("enuminput type is V4L2_INPUT_TYPE_CAMERA!\n");

				if (-1 == ioctl (fd, VIDIOC_S_INPUT, &inp))	//设置输入index
					printf("VIDIOC_S_INPUT error!\n");

				if (-1 == ioctl (fd, VIDIOC_G_INPUT, &inp))	//获取输入index
					printf("VIDIOC_G_INPUT error!\n");
				printf("input index is %d\n",inp.index);
			}
		//Test VIDIOC_ENUM_FMT
			struct v4l2_fmtdesc fmtdesc;

			fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			for(i=0;i<12;i++)
			{
				fmtdesc.index = i;
				if (-1 == ioctl (fd, VIDIOC_ENUM_FMT, &fmtdesc))//获取格式参数
					{
						printf("VIDIOC_ENUM_FMT error!\n");
						continue;
					}
				printf("**************************************************************\n");
				printf("format index = %d, name = %s, v4l2 pixel format = %x\n",i,fmtdesc.description,fmtdesc.pixelformat);
			}
	}

//		printf("%s %d\n",__FILE__,__LINE__);
		//Test VIDIOC_S_FMT
			CLEAR (fmt);
			fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width       = input_size.width; //320;
			fmt.fmt.pix.height      = input_size.height; //240;
			fmt.fmt.pix.pixelformat = csi_format;//V4L2_PIX_FMT_YUV422P;//V4L2_PIX_FMT_NV12;//V4L2_PIX_FMT_YUYV;
			fmt.fmt.pix.field       = V4L2_FIELD_NONE;
			int ret = -1;


			if(invalid_fmt_test==1)
			{
				printf("Try V4L2_PIX_FMT_YUV410\n");
				fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_YUV410;
				if (-1 == ioctl (fd, VIDIOC_S_FMT, &fmt)) //设置错误图像格式
				{
					printf("VIDIOC_S_FMT error!\n");
				}

				printf("Try V4L2_PIX_FMT_YVU420\n");
				fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_YVU420;
				if (-1 == ioctl (fd, VIDIOC_S_FMT, &fmt)) //设置错误图像格式
				{
					printf("VIDIOC_S_FMT error!\n");
				}

				printf("Try V4L2_PIX_FMT_NV16\n");
				fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_NV16;
			}





			if (-1 == ioctl (fd, VIDIOC_S_FMT, &fmt)) //设置图像格式
			{
					printf("VIDIOC_S_FMT error!\n");
					ret = -1;
					return ret;
					//goto close;
			}

			disp_size.width = fmt.fmt.pix.width;
			disp_size.height = fmt.fmt.pix.height;

//		printf("%s %d\n",__FILE__,__LINE__);
		if(ioctl_test==1)
		{
			printf("**************************************************************\n");
			printf("fmt.type = %d\n",fmt.type);
			printf("fmt.fmt.pix.width = %d\n",fmt.fmt.pix.width);
			printf("fmt.fmt.pix.height = %d\n",fmt.fmt.pix.height);
			printf("fmt.fmt.pix.pixelformat = %x\n",fmt.fmt.pix.pixelformat);
			printf("fmt.fmt.pix.field = %d\n",fmt.fmt.pix.field);
		}
		//Test VIDIOC_G_FMT



		if (-1 == ioctl (fd, VIDIOC_G_FMT, &fmt)) //获取图像格式
		{
				printf("VIDIOC_G_FMT error!\n");
		}
		else
		{
			printf("**************************************************************\n");
			printf("resolution got from sensor = %d*%d\n",fmt.fmt.pix.width,fmt.fmt.pix.height);
			printf("**************************************************************\n");
		}


		if(ioctl_test==1)
		{
			printf("**************************************************************\n");
			printf("fmt.fmt.pix.width = %d\n",fmt.fmt.pix.width);
			printf("fmt.fmt.pix.height = %d\n",fmt.fmt.pix.height);
			printf("fmt.fmt.pix.pixelformat = %x\n",fmt.fmt.pix.pixelformat);
			printf("fmt.fmt.pix.field = %d\n",fmt.fmt.pix.field);
			printf("fmt.fmt.pix.bytesperline = %d\n",fmt.fmt.pix.bytesperline);
			printf("fmt.fmt.pix.sizeimage = %d\n",fmt.fmt.pix.sizeimage);
		}

	if(fps_test==1)
	{
		//Test VIDIOC_G_PARM
			struct v4l2_streamparm parms;
			parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (-1 == ioctl (fd, VIDIOC_G_PARM, &parms)) //获取帧率
					printf ("VIDIOC_G_PARM error\n");

			printf("numerator = %d\n",parms.parm.capture.timeperframe.numerator);
			printf("denominator = %d\n",parms.parm.capture.timeperframe.denominator);


//		//Test VIDIOC_S_PARM
//			parms.parm.capture.timeperframe.denominator = fps;//
//
//			if (-1 == ioctl (fd, VIDIOC_S_PARM, &parms)) //获取帧率
//					printf ("VIDIOC_G_PARM error\n");
//
//		//Test VIDIOC_G_PARM
//			if (-1 == ioctl (fd, VIDIOC_G_PARM, &parms)) //获取帧率
//					printf ("VIDIOC_G_PARM error\n");
//
//			printf("numerator = %d\n",parms.parm.capture.timeperframe.numerator);
//			printf("denominator = %d\n",parms.parm.capture.timeperframe.denominator);
	}

	//count=read_num;

	struct v4l2_requestbuffers req;
	CLEAR (req);
	req.count               = req_frame_num;
	req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory              = V4L2_MEMORY_MMAP;


	ioctl (fd, VIDIOC_REQBUFS, &req); //申请缓冲，count是申请的数量

	buffers = calloc (req.count, sizeof (*buffers));//内存中建立对应空间

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
	{
	   struct v4l2_buffer buf;   //驱动中的一帧
	   CLEAR (buf);
	   buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	   buf.memory      = V4L2_MEMORY_MMAP;
	   buf.index       = n_buffers;

	   if (-1 == ioctl (fd, VIDIOC_QUERYBUF, &buf)) //映射用户空间
			printf ("VIDIOC_QUERYBUF error\n");

	   buffers[n_buffers].length = buf.length;
	   buffers[n_buffers].start  = mmap (NULL /* start anywhere */,    //通过mmap建立映射关系
								         buf.length,
								         PROT_READ | PROT_WRITE /* required */,
								         MAP_SHARED /* recommended */,
								         fd, buf.m.offset);

	   if (MAP_FAILED == buffers[n_buffers].start)
			printf ("mmap failed\n");
	}

	for (i = 0; i < n_buffers; ++i)
	{
	   struct v4l2_buffer buf;
	   CLEAR (buf);

	   buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	   buf.memory      = V4L2_MEMORY_MMAP;
	   buf.index       = i;

	   if (-1 == ioctl (fd, VIDIOC_QBUF, &buf))//申请到的缓冲进入列队
		printf ("VIDIOC_QBUF failed\n");
	}

#ifdef DISPLAY
				disp_int(disp_size.width,disp_size.height);
				disp_start();
#endif

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == ioctl (fd, VIDIOC_STREAMON, &type)) //开始捕捉图像数据
		printf ("VIDIOC_STREAMON failed\n");
	else
		printf ("VIDIOC_STREAMON ok\n");

	if (-1 == ioctl (fd, VIDIOC_STREAMON, &type)) //开始捕捉图像数据
		printf ("VIDIOC_STREAMON failed\n");
	else
		printf ("VIDIOC_STREAMON ok\n");

  count = read_num;

	while(count-->0)
//	while(1)
	{
		//gettimeofday(&time_test,&tz);



		for (;;) //这一段涉及到异步IO
		{
		   fd_set fds;
		   struct timeval tv;
		   int r;

		   FD_ZERO (&fds);//将指定的文件描述符集清空
		   FD_SET (fd, &fds);//在文件描述符集合中增加一个新的文件描述符

		   /* Timeout. */
		   tv.tv_sec = 2;
		   tv.tv_usec = 0;

		   r = select (fd + 1, &fds, NULL, NULL, &tv);//判断是否可读（即摄像头是否准备好），tv是定时

		   if (-1 == r) {
			if (EINTR == errno)
			 continue;
			printf ("select err\n");
								}
		   if (0 == r) {
			fprintf (stderr, "select timeout\n");
			exit (EXIT_FAILURE);
								}

#ifdef DISPLAY
      if(count==read_num-1)
      	disp_on();
#endif
		   if (read_frame ())//如果可读，执行read_frame ()函数，并跳出循环
		   break;
		}
	}

	if(control_test==1)
	{
		struct v4l2_queryctrl qc;
		struct v4l2_control ctrl;

		for(i=0;i<sizeof(qc_ctrl);i++)
		{
			CLEAR(qc);
			qc.id = qc_ctrl[i];
			if (-1 == ioctl (fd, VIDIOC_QUERYCTRL, &qc))
			{
				printf("VIDIOC_QUERYCTRL %s failed!\n",qc.name);
				continue;
			}
			else
			{
				printf("**************************************************************\n");
				printf("Name:%s\n",qc.name);
				printf("qc.min = %d\n",qc.minimum);
				printf("qc.max = %d\n",qc.maximum);
				printf("qc.step = %d\n",qc.step);
				printf("qc.default_value = %d\n",qc.default_value);
			}

			ctrl.id = qc.id;
			ctrl.value = qc.maximum;
			if (-1 == ioctl (fd, VIDIOC_S_CTRL, &ctrl))
				printf("VIDIOC_S_CTRL %s failed!\n",qc.name);
			else
			{
				printf("%s set to max\n",qc.name);

			}

			ctrl.value = 0;
			if (-1 == ioctl (fd, VIDIOC_G_CTRL, &ctrl))
				printf("VIDIOC_G_CTRL %s failed!\n",qc.name);
			else
			{
				printf("Name:%s\n",qc.name);
				printf("read ctrl.value = %d\n",ctrl.value);
			}

			printf("press ENTER to continue!\n");
			getchar();

			ctrl.value = qc.minimum;
			if (-1 == ioctl (fd, VIDIOC_S_CTRL, &ctrl))
				printf("VIDIOC_S_CTRL %s failed!\n",qc.name);
			else
			{
				printf("%s set to min\n",qc.name);
			}

			ctrl.value = 0;
			if (-1 == ioctl (fd, VIDIOC_G_CTRL, &ctrl))
				printf("VIDIOC_G_CTRL %s failed!\n",qc.name);
			else
			{
				printf("Name:%s\n",qc.name);
				printf("read ctrl.value = %d\n",ctrl.value);
			}

			printf("press ENTER to continue!\n");
			getchar();

			ctrl.value = qc.default_value;
			if (-1 == ioctl (fd, VIDIOC_S_CTRL, &ctrl))
				printf("VIDIOC_S_CTRL %s failed!\n",qc.name);
			else
			{
				printf("%s set to default_value\n",qc.name);
			}

		}
	}

close:
	if (-1 == ioctl (fd, VIDIOC_STREAMOFF, &type)) //停止捕捉图像数据
		printf ("VIDIOC_STREAMOFF failed\n");
	else
		printf ("VIDIOC_STREAMOFF ok\n");

	if (-1 == ioctl (fd, VIDIOC_STREAMOFF, &type)) //停止捕捉图像数据
		printf ("VIDIOC_STREAMOFF failed\n");
	else
		printf ("VIDIOC_STREAMOFF ok\n");

	if(read_num==1)
	{
	   printf("press ENTER key to continue!\n");
	   getchar();
	}

unmap:
	for (i = 0; i < n_buffers; ++i) {
		if (-1 == munmap (buffers[i].start, buffers[i].length)) {
			printf ("munmap error");
		}
	}
	disp_stop();
	disp_quit();



	close (fd);

	return 0;
}

int
main(void)
{
		int i;
		struct test_case *test_ptr;

		test_num=1;
		read_num=200;

		req_frame_num = 5;
		input_size.width = 1280;//1600;//640;
		input_size.height = 1024;//1200;//480;
//		disp_size.width = 1280;//1600;//640;
//		disp_size.height = 1024;//1200;//480;
		csi_format=V4L2_PIX_FMT_NV12;
		disp_format=DISP_FORMAT_YUV420;
		disp_mode=DISP_MOD_NON_MB_UV_COMBINED;
		disp_seq=DISP_SEQ_UVUV;

printf("********************************************************************Read stream test start,capture 1000 frames,press to continue\n");
		getchar();

		read_num = 1000;
		main_test();

//printf("********************************************************************fps test start,press to continue\n");
//		getchar();
//
//		fps_test=1;
//		read_num=30;
//		main_test();
//		fps_test=0;

//printf("********************************************************************IOCTL invalid test start,press to continue\n");
//		getchar();
//		invalid_ops=1;
//		main_test();
//		invalid_ops=0;
//
//printf("********************************************************************Try and set invalid format test start,press to continue\n");
//		getchar();
//
//		invalid_fmt_test=1;
//		main_test();
//		invalid_fmt_test=0;
//
//
//printf("********************************************************************ENUMFMT,SETFMT,GETFMT test start,press to continue\n");
//		getchar();
//		ioctl_test=1;
//		main_test();
//		ioctl_test=0;
//
//
//printf("********************************************************************Read one frame test start,capture 1 frame,press to continue\n");
//		getchar();
//		read_num=1;
//		main_test();
//
//printf("********************************************************************Read stream test start,capture 1000 frames,press to continue\n");
//		getchar();
//
//		read_num = 1000;
//		main_test();

//printf("********************************************************************Req buffer test start,press to continue\n");
//		getchar();
//
//		read_num = 100;
//		for(i=1;i<7;i++)
//		{
//			printf("Req buffer count = %d, capture 100 frames\n",i);
//			req_frame_num = i;
//			main_test();
//			printf("press to continue\n");
//			getchar();
//		}
//
//
//
//printf("********************************************************************V4L2 control test start,press to continue\n");
//	 	getchar();
//	 	control_test=1;
//	 	csi_format=V4L2_PIX_FMT_NV16;
//		disp_format=DISP_FORMAT_YUV422;
//		disp_mode=DISP_MOD_NON_MB_UV_COMBINED;
//		disp_seq=DISP_SEQ_UVUV;
//	 	main_test();
//		control_test=0;
//
printf("********************************************************************resolution and format test start,press to continue\n");
		getchar();

		read_num=200;

		for(i=0;i<30;i++)		//16 //30
		{
			test_ptr = &test_case_set[i];
			input_size.width = test_ptr->input_width;
			input_size.height = test_ptr->input_height;
//			disp_size.width = test_ptr->disp_width;
//			disp_size.height = test_ptr->disp_height;
			csi_format = test_ptr->csi_format;
			disp_format = test_ptr->disp_format;
			disp_mode=test_ptr->disp_mode;
			disp_seq=test_ptr->disp_seq;

			printf("***************************************************************************************\ninput size:%dx%d\n",
			input_size.width,input_size.height);

			switch(csi_format){
			case V4L2_PIX_FMT_YUV422P:
    		printf("format: V4L2_PIX_FMT_YUV422P\n");
    		break;
    	case V4L2_PIX_FMT_YUV420:
    		printf("format: V4L2_PIX_FMT_YUV420\n");
    		break;
    	case V4L2_PIX_FMT_NV16:
    		printf("format: V4L2_PIX_FMT_NV16\n");
    		break;
    	case V4L2_PIX_FMT_NV12:
    		printf("format: V4L2_PIX_FMT_NV12\n");
    		break;
    	case V4L2_PIX_FMT_HM12:
    		printf("format: V4L2_PIX_FMT_HM12\n");
    		break;
    	case V4L2_PIX_FMT_YUYV:
    		printf("format: V4L2_PIX_FMT_YUYV\n");
    		break;
    	case V4L2_PIX_FMT_YVYU:
    		printf("format: V4L2_PIX_FMT_YVYU\n");
    		break;
    	case V4L2_PIX_FMT_UYVY:
    		printf("format: V4L2_PIX_FMT_UYVY\n");
    		break;
    	case V4L2_PIX_FMT_VYUY:
    		printf("format: V4L2_PIX_FMT_VYUY\n");
    		break;
    	default:
    		printf("format: error\n");
    		break;
    	}

			printf("***************************************************************************************\n");
			main_test();
			printf("press to continue\n");
			getchar();
		}

printf("********************************************************************lost frame test start,press to continue\n");
		getchar();
		lost_frame_test=1;
		fps=30;
		read_num=200;
		req_frame_num = 4;
		input_size.width = 640;
		input_size.height = 480;
		disp_size.width = 640;
		disp_size.height = 480;
		csi_format=V4L2_PIX_FMT_NV16;
		disp_format=DISP_FORMAT_YUV422;
		disp_mode=DISP_MOD_NON_MB_UV_COMBINED;
		disp_seq=DISP_SEQ_UVUV;
		main_test();
//
//
//printf("********************************************************************fps test start,press to continue\n");
//		getchar();
//
//		printf("set fps to 30fps\n");
//		fps_test=1;
//		fps=30;
//		read_num=30;
//		req_frame_num = 4;
//		input_size.width = 640;
//		input_size.height = 480;
//		disp_size.width = 640;
//		disp_size.height = 480;
//		csi_format=V4L2_PIX_FMT_NV16;
//		disp_format=DISP_FORMAT_YUV422;
//		disp_mode=DISP_MOD_NON_MB_UV_COMBINED;
//		disp_seq=DISP_SEQ_UVUV;
//		main_test();
//
//
//
//		printf("set fps to 15fps\n");
//		fps=15;
//		read_num=30;
//		req_frame_num = 4;
//		input_size.width = 640;
//		input_size.height = 480;
//		disp_size.width = 640;
//		disp_size.height = 480;
//		csi_format=V4L2_PIX_FMT_NV16;
//		disp_format=DISP_FORMAT_YUV422;
//		disp_mode=DISP_MOD_NON_MB_UV_COMBINED;
//		disp_seq=DISP_SEQ_UVUV;
//		main_test();

printf("********************************************************************test done,press to end\n");
		getchar();



	exit (EXIT_SUCCESS);
	return 0;
}
