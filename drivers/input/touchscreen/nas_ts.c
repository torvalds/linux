/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************************
 * driver/input/touchscreen/hannstar_nas.c
 *Copyright 	:ROCKCHIP  Inc
 *Author	: 	sfm
 *Date		:  2010.2.5
 *This driver use for rk28 chip extern touchscreen. Use i2c IF ,the chip is Hannstar
 *description??
 ********************************************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <mach/board.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <linux/earlysuspend.h>
#define MAX_SUPPORT_POINT	2// //  4
#define PACKGE_BUFLEN		10

#define	CHECK_STATUS	1
//#define	TP_ERROR_RESTART_POWER	1

//#define Singltouch_Mode
//#define	NAS_TP_DEBUG	0
#define SAKURA_DBG                  0
#if SAKURA_DBG 
#define sakura_dbg_msg(fmt,...)       do {                                      \
                                   printk("sakura dbg msg------>"                       \
                                          " (func-->%s ; line-->%d) " fmt, __func__, __LINE__ , ##__VA_ARGS__); \
                                  } while(0)
#define sakura_dbg_report_key_msg(fmt,...)      do{                                                     \
                                                    printk("sakura report " fmt,##__VA_ARGS__);          \
                                                }while(0)
#else
#define sakura_dbg_msg(fmt,...)       do {} while(0)
#define sakura_dbg_report_key_msg(fmt,...)      do{}while(0)
#endif

#define SWAP_Y_POS
#define TOUCH_REPORT_X_MAX        1280//(1255)	//(1280 - 1)//(0xfff)
#define TOUCH_REPORT_Y_MAX        768//(700)	//(768 - 1)//(0xfff)

#define RECORD_PREVIOUS_VALUES
#define FILTER_SAME_POINTS

#define NASTECH_ZERO_TOUCH        0	
#define NASTECH_ONE_TOUCH         1
#define NASTECH_TWO_TOUCH         2
#define NASTECH_PENUP_RECHECK     3
#define NASTECH_DeltaX            100
#define NASTECH_DeltaY            80
#define NASTECH_R_Threshold       4000

/*delay used to check the touch panel current status.*/
#define TS_RELEASE_CHECK_DELAY		(100)
#define TS_CUR_STATUS_PUSH			1
#define TS_CUR_STATUS_RELEASE		0
#define TS_POLL_DELAY_DOWN        (2)  /* ms delay before the first sample */
#define TS_POLL_DELAY_UP          (10)  /* ms delay after pen up, to double check the pen up event */
#define TS_POLL_PERIOD            (30)  /* ms delay between samples */
#define PENUP_DETECT_MAX          (4)   /* How many times we should detect for a penup event. */
#define INT_TEST_TIMES            40

struct touch_point {
	int count;
	int curX;
	int curY;
	int unCalX;
	int unCalY;
	int prevX;
	int prevY;
#ifdef RECORD_PREVIOUS_VALUES
	int sample_time;
	int last_finger;
	int x[2];
	int y[2];
#endif
};

struct point_data {	
	short status;	
	short x;	
	short y;
    short z;
};

struct multitouch_event{
	struct point_data point_data[MAX_SUPPORT_POINT];
	int contactid;
    int validtouch;
};

struct ts_nas {
	struct input_dev	*input;
	char			phys[32];
	struct delayed_work	work;
	struct workqueue_struct *wq;

	struct i2c_client	*client;
    struct multitouch_event mt_event;
	u16			model;

	bool		pendown;
	bool 	 	status;
	int			irq;
	int 		has_relative_report;
//add from nas

	bool                sleepstatus;
	bool                pendown_sent;          /* Whether we've sent out a pendown event */
	bool                pendown_ignore;        /* Sample two points and send out only one point */
	int                 penup_recheck;         /* Pen up re-check times, when it reaches PENUP_DETECTE_MAX, pen up is reported. */
	int                 release_check_time; /* Debounse time for pen down, ms */
	int                 pendown_debounce_time; /* Debounse time for pen down, ms */
	int                 penup_debounce_time;   /* Debounce time for pen up, ms */
	int                 repeat_time;           /* Time between samples, ms */
	struct touch_point  point;
	struct timer_list	status_check_timer;
	int                 reported_finger_count;
	int 				touch_cur_status;

	int			(*get_pendown_state)(void);
	void		(*clear_penirq)(void);
#ifdef	CHECK_STATUS	
	struct delayed_work	work1;
	struct workqueue_struct *wq1;
#endif	
	
};

//mg gamma
unsigned short y_gamma[]={
 0 ,   0 ,   1 ,   1 ,       
 2 ,   3 ,   3 ,   4 ,   5 ,   6 ,   6 ,   7 ,       
 8 ,   9 ,   10 ,   10 ,   11 ,   12 ,   13 ,   14 ,       
 15 ,   15 ,   16 ,   17 ,   18 ,   19 ,   20 ,   21 ,       
 21 ,   22 ,   23 ,   24 ,   25 ,   26 ,   27 ,   28 ,       
 28 ,   29 ,   30 ,   31 ,   32 ,   33 ,   34 ,   35 ,       
 36 ,   37 ,   38 ,   38 ,   39 ,   40 ,   41 ,   42 ,       
 43 ,   44 ,   45 ,   46 ,   47 ,   48 ,   49 ,   50 ,       
 51 ,   52 ,   52 ,   53 ,   54 ,   55 ,   56 ,   57 ,       
 58 ,   59 ,   60 ,   61 ,   62 ,   63 ,   64 ,   65 ,       
 66 ,   67 ,   68 ,   69 ,   70 ,   71 ,   72 ,   73 ,       
 74 ,   75 ,   76 ,   77 ,   78 ,   79 ,   80 ,   81 ,       
 82 ,   83 ,   84 ,   85 ,   86 ,   87 ,   88 ,   89 ,       
 90 ,   91 ,   92 ,   93 ,   94 ,   95 ,   96 ,   97 ,       
 98 ,   99 ,   100 ,   101 ,   102 ,   103 ,   104 ,   105 ,       
 106 ,   107 ,   108 ,   109 ,   110 ,   111 ,   112 ,   113 ,       
 114 ,   115 ,   116 ,   117 ,   118 ,   119 ,   120 ,   121 ,       
 122 ,   123 ,   124 ,   125 ,   126 ,   127 ,   128 ,   129 ,       
 130 ,   132 ,   133 ,   134 ,   135 ,   136 ,   137 ,   138 ,       
 139 ,   140 ,   141 ,   142 ,   143 ,   144 ,   145 ,   146 ,       
 147 ,   148 ,   149 ,   150 ,   151 ,   152 ,   154 ,   155 ,       
 156 ,   157 ,   158 ,   159 ,   160 ,   161 ,   162 ,   163 ,       
 164 ,   165 ,   166 ,   167 ,   168 ,   169 ,   171 ,   172 ,       
 173 ,   174 ,   175 ,   176 ,   177 ,   178 ,   179 ,   180 ,       
 181 ,   182 ,   183 ,   184 ,   186 ,   187 ,   188 ,   189 ,       
 190 ,   191 ,   192 ,   193 ,   194 ,   195 ,   196 ,   197 ,       
 199 ,   200 ,   201 ,   202 ,   203 ,   204 ,   205 ,   206 ,       
 207 ,   208 ,   209 ,   210 ,   212 ,   213 ,   214 ,   215 ,       
 216 ,   217 ,   218 ,   219 ,   220 ,   221 ,   222 ,   224 ,       
 225 ,   226 ,   227 ,   228 ,   229 ,   230 ,   231 ,   232 ,       
 233 ,   235 ,   236 ,   237 };

unsigned short y_gamma900[]={
0 ,   0 ,   1 ,   2 ,       
 3 ,   4 ,   4 ,   5 ,   6 ,   7 ,   8 ,   9 ,       
 10 ,   11 ,   12 ,   12 ,   13 ,   14 ,   15 ,   16 ,       
 17 ,   18 ,   19 ,   20 ,   21 ,   22 ,   23 ,   23 ,       
 24 ,   25 ,   26 ,   27 ,   28 ,   29 ,   30 ,   31 ,       
 32 ,   33 ,   34 ,   35 ,   36 ,   37 ,   38 ,   39 ,       
 40 ,   41 ,   42 ,   42 ,   43 ,   44 ,   45 ,   46 ,       
 47 ,   48 ,   49 ,   50 ,   51 ,   52 ,   53 ,   54 ,       
 55 ,   56 ,   57 ,   58 ,   59 ,   60 ,   61 ,   62 ,       
 63 ,   64 ,   65 ,   66 ,   67 ,   68 ,   69 ,   70 ,       
 71 ,   72 ,   73 ,   74 ,   75 ,   76 ,   77 ,   78 ,       
 79 ,   80 ,   81 ,   82 ,   83 ,   84 ,   85 ,   86 ,       
 87 ,   88 ,   89 ,   90 ,   91 ,   92 ,   93 ,   94 ,       
 95 ,   96 ,   97 ,   98 ,   99 ,   100 ,   101 ,   102 ,       
 103 ,   104 ,   105 ,   106 ,   107 ,   108 ,   109 ,   110 ,       
 111 ,   112 ,   113 ,   114 ,   115 ,   116 ,   117 ,   118 ,       
 119 ,   120 ,   121 ,   122 ,   123 ,   124 ,   125 ,   126 ,       
 127 ,   128 ,   129 ,   130 ,   131 ,   132 ,   133 ,   134 ,       
 135 ,   136 ,   137 ,   138 ,   139 ,   140 ,   141 ,   142 ,       
 143 ,   144 ,   145 ,   146 ,   147 ,   148 ,   149 ,   150 ,       
 152 ,   153 ,   154 ,   155 ,   156 ,   157 ,   158 ,   159 ,       
 160 ,   161 ,   162 ,   163 ,   164 ,   165 ,   166 ,   167 ,       
 168 ,   169 ,   170 ,   171 ,   172 ,   173 ,   174 ,   175 ,       
 176 ,   177 ,   178 ,   179 ,   180 ,   181 ,   182 ,   183 ,       
 185 ,   186 ,   187 ,   188 ,   189 ,   190 ,   191 ,   192 ,       
 193 ,   194 ,   195 ,   196 ,   197 ,   198 ,   199 ,   200 ,       
 201 ,   202 ,   203 ,   204 ,   205 ,   206 ,   207 ,   208 ,       
 209 ,   210 ,   212 ,   213 ,   214 ,   215 ,   216 ,   217 ,       
 218 ,   219 ,   220 ,   221 ,   222 ,   223 ,   224 ,   225 ,       
 226 ,   227 ,   228 ,   229 ,   230 ,   231 ,   232 ,   233 ,       
 235 ,   236 ,   237 ,   238  };  


unsigned short y_gamma800[]={
  0 ,   0 ,   0 ,   0 ,   1 ,   1 ,       
 2 ,   2 ,   3 ,   3 ,   4 ,   5 ,   5 ,   6 ,       
 6 ,   7 ,   8 ,   8 ,   9 ,   9 ,   10 ,   11 ,       
 11 ,   12 ,   13 ,   13 ,   14 ,   15 ,   16 ,   16 ,       
 17 ,   18 ,   19 ,   19 ,   20 ,   21 ,   22 ,   22 ,       
 23 ,   24 ,   25 ,   25 ,   26 ,   27 ,   28 ,   29 ,       
 29 ,   30 ,   31 ,   32 ,   33 ,   34 ,   34 ,   35 ,       
 36 ,   37 ,   38 ,   39 ,   40 ,   40 ,   41 ,   42 ,       
 43 ,   44 ,   45 ,   46 ,   47 ,   47 ,   48 ,   49 ,       
 50 ,   51 ,   52 ,   53 ,   54 ,   55 ,   56 ,   57 ,       
 58 ,   58 ,   59 ,   60 ,   61 ,   62 ,   63 ,   64 ,       
 65 ,   66 ,   67 ,   68 ,   69 ,   70 ,   71 ,   72 ,       
 73 ,   74 ,   75 ,   76 ,   77 ,   78 ,   79 ,   80 ,       
 81 ,   82 ,   83 ,   84 ,   85 ,   86 ,   87 ,   88 ,       
 89 ,   90 ,   91 ,   92 ,   93 ,   94 ,   95 ,   96 ,       
 97 ,   98 ,   99 ,   100 ,   101 ,   102 ,   103 ,   104 ,       
 105 ,   106 ,   107 ,   108 ,   109 ,   110 ,   111 ,   113 ,       
 114 ,   115 ,   116 ,   117 ,   118 ,   119 ,   120 ,   121 ,       
 122 ,   123 ,   124 ,   125 ,   127 ,   128 ,   129 ,   130 ,       
 131 ,   132 ,   133 ,   134 ,   135 ,   136 ,   137 ,   139 ,       
 140 ,   141 ,   142 ,   143 ,   144 ,   145 ,   146 ,   147 ,       
 149 ,   150 ,   151 ,   152 ,   153 ,   154 ,   155 ,   157 ,       
 158 ,   159 ,   160 ,   161 ,   162 ,   163 ,   164 ,   166 ,       
 167 ,   168 ,   169 ,   170 ,   171 ,   173 ,   174 ,   175 ,       
 176 ,   177 ,   178 ,   180 ,   181 ,   182 ,   183 ,   184 ,       
 185 ,   187 ,   188 ,   189 ,   190 ,   191 ,   192 ,   194 ,       
 195 ,   196 ,   197 ,   198 ,   200 ,   201 ,   202 ,   203 ,       
 204 ,   206 ,   207 ,   208 ,   209 ,   210 ,   212 ,   213 ,       
 214 ,   215 ,   216 ,   218 ,   219 ,   220 ,   221 ,   222 ,       
 224 ,   225 ,   226 ,   227 ,   229 ,   230 ,   231 ,   232 ,       
 233 ,   235 ,   236 ,   237 ,   238 ,   240 ,   241 ,   242 ,       
 243 ,   245 ,   246 ,   247 ,   248 ,   250 ,   251 ,   252 ,       
 253 ,   255 ,   256 ,   257 ,   258 ,   260 ,   261 ,   262  
};

static struct proc_dir_entry *nas_tp_debug_ctl_entry;
static char nas_tp_debug_flag = 0;

extern void nas_reset(void);
static void nas_early_resume(struct early_suspend *h)
{
	//nas_reset();
}
static struct early_suspend nastech_early_suspend =
{
	.resume = nas_early_resume
};
#ifdef	CHECK_STATUS
static void nas_status_check_timer(unsigned long data)
{
	unsigned char buf[4]={0};
	struct ts_nas *ts_dev = (struct ts_nas *)data;
	int ret=0;
	//printk("nas_status_check_timer ...\n");
		queue_delayed_work(ts_dev->wq1, &ts_dev->work1, 0);
	ts_dev->status_check_timer.expires  = jiffies + msecs_to_jiffies(1000);
	add_timer(&ts_dev->status_check_timer);
}

#define FB_DISPLAY_ON_PIN RK29_PIN6_PD0
extern void rk29_lcd_reset(void);
static void nas_status_check_work(struct work_struct *work1)
{
	unsigned char buf[4]={0};
	struct ts_nas *ts_dev =
		container_of(to_delayed_work(work1), struct ts_nas, work1);
	int ret=0;

	if (GPIO_LOW == gpio_get_value(FB_DISPLAY_ON_PIN))
		return;

	//printk("nas_status_check_work ...");
	ret = i2c_master_reg8_recv( ts_dev->client, 0xFC, buf, 1, 200*1000);
	//printk(" ret = %d\n", ret);
	if( ret <0)
	{
		printk("nas status error, ret=%d \nnas_reset()\n", ret);
#ifdef	TP_ERROR_RESTART_POWER		
		rk29_lcd_reset();
		msleep(100);
#endif		
		nas_reset();
	}
}	
#endif
/** Get point from touch pannel */
static int nastech_ts_get_point(struct ts_nas *ts_dev, unsigned short *pX, unsigned short *pY)
{
	unsigned short xpos, ypos;
	unsigned short xpos0, ypos0;
	unsigned char event;
	unsigned char Finger, hwFinger;
	unsigned char buf[26];
	//unsigned char buf[13]; // for 2 finger touch, only used the first 13 bytes
	int ret;
	int nTouch = 0;
	struct i2c_client *client = ts_dev->client;
	struct input_dev *input = ts_dev->input;

	memset(buf, 0xff, sizeof(buf));
	//ret = nastech_read_regs(client, NASTECH_READ_POS, buf, sizeof(buf));
	ret = i2c_master_reg8_recv( ts_dev->client, 0xF9, buf, 26, 200*1000);

	hwFinger=buf[3];

	//-------------------------------------------------------------------------------------------
	//xpos=(unsigned short)(buf[7]*0x100);
	xpos=(unsigned short)((buf[7]&0x0f)<<8 );
	xpos=xpos|buf[8];
	//ypos=(unsigned short)(buf[5]*0x100);
	ypos=(unsigned short)((buf[5]&0x0f)<<8 );
	ypos=ypos|buf[6];

	//xpos0=(unsigned short)(buf[11]*0x100);
	xpos0=(unsigned short)((buf[11]&0x0f)<<8 );
	xpos0=xpos0|buf[12];
	//ypos0=(unsigned short)(buf[9]*0x100);
	ypos0=(unsigned short)((buf[9]&0x0f)<<8 );
	ypos0=ypos0|buf[10];
	//-------------------------------------------------------------------------------------------
	
//#ifdef NAS_TP_DEBUG
	if (nas_tp_debug_flag == 1)
		printk(KERN_INFO "read from TP: (%d,%d), (%d,%d)\n", xpos, ypos, xpos0, ypos0);
//#endif
unsigned short   ypos_pre,ratio;



	*pX = xpos;
	*pY = ypos;

#ifdef TOUCH_REPORT_ONLY_ONE_POINT
	if(xpos==0x0FFF || ypos==0x0FFF)
	{
		Finger = NASTECH_ZERO_TOUCH;
	}
	else
	{
		Finger = NASTECH_ONE_TOUCH;
	}
#else

	if(xpos==0x0FFF || ypos==0x0FFF)
	{
		Finger = NASTECH_ZERO_TOUCH;
	}
	else
	{
		Finger = NASTECH_ONE_TOUCH;
	}
	
	if(xpos0!=0x0FFF && ypos0!=0x0FFF)
	{
		Finger = NASTECH_TWO_TOUCH;
	}

#ifdef SWAP_Y_POS
ypos = TOUCH_REPORT_Y_MAX - ypos;
	if(ypos < 0)
		ypos = 0;
		
	 if (ypos<262)
{
   ratio=ypos;
 
                ypos= y_gamma800[ratio];
            
}
 if (ypos>506)
{
    
   ratio= 767-ypos ;
         ypos=767-y_gamma800[ratio];
               
            
}


	if (xpos<262)
{
   ratio=xpos;
 
                xpos= y_gamma800[ratio];
            
}
if (xpos>1017)
{
   ratio=1279-xpos;
 
                xpos= 1279-y_gamma800[ratio];
            
}


	//ypos=report_y_correction(y_pre); //mg for coorection	
	
	ypos0 = TOUCH_REPORT_Y_MAX - ypos0;
	if(ypos0 < 0)
		ypos0 = 0;
		
	 if (ypos0<262)
{
   ratio=ypos0;
 
                ypos0= y_gamma800[ratio];
            
}
 if (ypos0>506)
{
    
   ratio= 767-ypos0 ;
         ypos0=767-y_gamma800[ratio];
               
            
}


	if (xpos0<262)
{
   ratio=xpos0;
 
                xpos0= y_gamma800[ratio];
            
}
if (xpos>1017)
{
   ratio=1279-xpos0;
 
                xpos0= 1279-y_gamma800[ratio];
            
}	 
#endif
//#ifdef NAS_TP_DEBUG
	if (nas_tp_debug_flag == 1) {
		if (NASTECH_TWO_TOUCH == Finger) {
			printk(KERN_INFO "original: (%d,%d), (%d,%d)\n", xpos, ypos, xpos0, ypos0);
		} else if (NASTECH_ONE_TOUCH == Finger) {
			printk(KERN_INFO "original: (%d,%d)\n", xpos, ypos);
		}
	}
//#endif
	//printk("nastech_ts_get_point: Finger = %d!\n",Finger);
#ifdef RECORD_PREVIOUS_VALUES
	if (ts_dev->point.last_finger == Finger)
	{
		switch(Finger)
		{
		case NASTECH_ZERO_TOUCH:
			break;
		case NASTECH_ONE_TOUCH:
			if (ts_dev->point.x[0] == xpos && ts_dev->point.y[0] == ypos)
				return Finger;
			break;
		case NASTECH_TWO_TOUCH:
			if ((ts_dev->point.x[0] == xpos && ts_dev->point.y[0] == ypos)
				&& (ts_dev->point.x[1] == xpos0 && ts_dev->point.y[1] == ypos0))
				return Finger;
			break;
		}
		ts_dev->point.x[0] = xpos;
		ts_dev->point.y[0] = ypos;
		ts_dev->point.x[1] = xpos0;
		ts_dev->point.y[1] = ypos0;
	}
	else
	{
		ts_dev->point.last_finger = Finger;
		ts_dev->point.x[0] = xpos;
		ts_dev->point.y[0] = ypos;
		ts_dev->point.x[1] = xpos0;
		ts_dev->point.y[1] = ypos0;
	}
#endif
	{
		int z,w;
		if(!Finger)
		{
			z=0;
			w=0;
		}
		else
		{
			z=255;
			w=15;
		}

		ts_dev->touch_cur_status = TS_CUR_STATUS_PUSH;
		
		if(Finger>1)
		{
			input_report_abs(input, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(input, ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(input, ABS_MT_POSITION_X, xpos);
			input_report_abs(input, ABS_MT_POSITION_Y, ypos);
			input_report_key(ts_dev->input, BTN_TOUCH, 1);
			input_mt_sync(input);
			
			input_report_abs(input, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(input, ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(input, ABS_MT_POSITION_X, xpos0);
			input_report_abs(input, ABS_MT_POSITION_Y, ypos0);
			input_report_key(ts_dev->input, BTN_2, 1);
			input_mt_sync(input);
		}
		else
		{
			if (1 == Finger)
			{
				input_report_abs(input, ABS_MT_POSITION_X, xpos);
				input_report_abs(input, ABS_MT_POSITION_Y, ypos);
				input_report_abs(input, ABS_MT_TOUCH_MAJOR, z);
				input_report_abs(input, ABS_MT_WIDTH_MAJOR, w);
				input_report_key(ts_dev->input, BTN_TOUCH, 1);
				input_mt_sync(input);
				//printk("pen press (x,y):(%d,%d)\n",xpos,ypos);
			}
			else
			{
			//	printk("nastech_ts_get_point: released......!\n");
				input_report_abs(input, ABS_MT_POSITION_X, xpos);
				input_report_abs(input, ABS_MT_POSITION_Y, ypos);
				input_report_abs(input, ABS_MT_TOUCH_MAJOR, z);
				input_report_abs(input, ABS_MT_WIDTH_MAJOR, w);
				input_report_key(ts_dev->input, BTN_TOUCH, 0);
				input_mt_sync(input);
				ts_dev->touch_cur_status = TS_CUR_STATUS_RELEASE;
			}
			
			if(ts_dev->reported_finger_count > 1)
			{
				input_report_abs(input, ABS_MT_POSITION_X, xpos);
				input_report_abs(input, ABS_MT_POSITION_Y, ypos);
				input_report_abs(input, ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(input, ABS_MT_WIDTH_MAJOR, 0);
				input_report_key(ts_dev->input, BTN_2, 0);
				input_mt_sync(input);
			}
		}
		ts_dev->reported_finger_count = Finger;
	}
	input_sync(input);
#endif
	return (int)Finger;
}

static int nastech_sample_new_point(struct ts_nas *ts_dev)
{
	unsigned short	x, y;
	unsigned int nTouch = 0;
	
	struct i2c_client *client = ts_dev->client;
	struct touch_point *point = &ts_dev->point;
	
	nTouch = nastech_ts_get_point(ts_dev, &x, &y);
	point->unCalX = x;
	point->unCalY = y;

	if (unlikely(NASTECH_ZERO_TOUCH == nTouch)) {
		//printk(KERN_INFO "<======Pen released at (%d, %d)\n", point->prevX, point->prevY);
		point->curX = point->curY = 0;
		point->count = 0;
	} else {
		point->curX = x;
		point->curY = y;
#ifdef TOUCHPANEL_SWAP_XY
		{
			int temp = point->curX;
			point->curX = point->curY;
			point->curY = temp;
		}
#endif
		if(nTouch == NASTECH_ONE_TOUCH) {
			if( !ts_dev->pendown_sent || (abs(point->curX-point->prevX) < NASTECH_DeltaX)
				&& (abs(point->curY - point->prevY) < NASTECH_DeltaY) ){ // is it a movement ? 
				// update pX, pY and send new point
				point->prevX = point->curX;
				point->prevY = point->curY;
			} else {
				printk(KERN_INFO "use the old touch: prev=(%d,%d), cur=(%d,%d)\n",
						point->prevX, point->prevY, point->curX, point->curY);
				// use old one
				point->curX = point->prevX;
				point->curY = point->prevY;
			}
		}
	}
	return nTouch;
}
int nas_get_pendown_state(void)
{
	return 0;
}

static void nas_report_event(struct ts_nas *ts,struct multitouch_event *tc)
{
	struct input_dev *input = ts->input;
    int i,pandown = 0;
	dev_dbg(&ts->client->dev, "UP\n");
		
    for(i=0; i<MAX_SUPPORT_POINT;i++){			
        if(tc->point_data[i].status >= 0){
            pandown |= tc->point_data[i].status;
            input_report_abs(input, ABS_MT_TRACKING_ID, i);							
            input_report_abs(input, ABS_MT_TOUCH_MAJOR, tc->point_data[i].status);				
            input_report_abs(input, ABS_MT_WIDTH_MAJOR, 0);	
            input_report_abs(input, ABS_MT_POSITION_X, tc->point_data[i].x);				
            input_report_abs(input, ABS_MT_POSITION_Y, tc->point_data[i].y);				
            input_mt_sync(input);	

            sakura_dbg_report_key_msg("ABS_MT_TRACKING_ID = %x, ABS_MT_TOUCH_MAJOR = %x\n ABS_MT_POSITION_X = %x, ABS_MT_POSITION_Y = %x\n",i,tc->point_data[i].status,tc->point_data[i].x,tc->point_data[i].y);
#if defined(CONFIG_HANNSTAR_DEBUG)
			printk("hannstar nas Px = [%d],Py = [%d] \n",tc->point_data[i].x,tc->point_data[i].y);
#endif

            if(tc->point_data[i].status == 0)					
            	tc->point_data[i].status--;			
        }
        
    }	

    ts->pendown = pandown;
    input_sync(input);
}


static inline int nas_check_firmwork(struct ts_nas *ts)
{
    int data;
    int len = 10;
    char buf[10] = {0x03 , 0x03 , 0x0a , 0x01 , 'D' , 0x00 , 0x00 , 0x00 , 0x00 , 0x00};
	int i;
    short contactid=0;

    data = i2c_master_normal_send(ts->client, buf,len, 200*1000);

	if(data < 0){
		dev_err(&ts->client->dev, "i2c io error %d \n", data);
		return data;
	}

	data = i2c_master_normal_recv(ts->client, buf,len, 200*1000);

	if(data < 0){
		dev_err(&ts->client->dev, "i2c io error %d \n", data);
		return data;
	}

	printk("nas reg[5] = %c ,reg[6] = %c, reg[7] = %c, reg[8] = %c\n" , buf[5],buf[6],buf[7],buf[8]);
	printk("nas reg[5] = %x ,reg[6] = %x, reg[7] = %x, reg[8] = %x\n" , buf[5],buf[6],buf[7],buf[8]);
    return data;
}


static inline int nas_read_values(struct ts_nas *ts, struct multitouch_event *tc)
{
    int data, j;
    int len = 10;
	char buff[26];
	int ret;
	int i;
	unsigned short xpos, ypos, xpos0, ypos0;
	memset(buff, 0xff, sizeof(buff));
	ret = i2c_master_reg8_recv( ts->client, 0xF9, buff, 26, 200*1000);
/*
	printk("ret = %d, buff[]=\n", ret);
	for(i=0; i<26; i++)
	{
		if(i %8 == 0)
			printk("\n");
		printk("0x%02x ", buff[i]);
	}
*/
	//-------------------------------------------------------------------------------------------
	//xpos=(unsigned short)(buf[7]*0x100);
	xpos=(unsigned short)((buff[7]&0x0f)<<8 );
	xpos=xpos|buff[8];
	//ypos=(unsigned short)(buf[5]*0x100);
	ypos=(unsigned short)((buff[5]&0x0f)<<8 );
	ypos=ypos|buff[6];

	//xpos0=(unsigned short)(buf[11]*0x100);
	xpos0=(unsigned short)((buff[11]&0x0f)<<8 );
	xpos0=xpos0|buff[12];
	//ypos0=(unsigned short)(buf[9]*0x100);
	ypos0=(unsigned short)((buff[9]&0x0f)<<8 );
	ypos0=ypos0|buff[10];
	//-------------------------------------------------------------------------------------------
	
//	printk("read from TP: (%d,%d), (%d,%d)\n", xpos, ypos, xpos0, ypos0);
    return 10;
}


static void nas_work(struct work_struct *work)
{
	struct ts_nas *ts =
		container_of(to_delayed_work(work), struct ts_nas, work);
	struct multitouch_event *tc = &ts->mt_event;
	int rt;
#if	0    
	rt = nas_read_values(ts,tc);
    
    if(rt < 0)
        goto out;
	
#if defined (Singltouch_Mode)
    nas_report_single_event(ts,tc);
#else
    nas_report_event(ts,tc);
#endif
#endif

//	if (ts->sleepstatus == 1)
//		return;
	
	switch(nastech_sample_new_point(ts)) {
	case NASTECH_ZERO_TOUCH:
		break;
	case NASTECH_ONE_TOUCH:
	case NASTECH_TWO_TOUCH:
#if	0
		{
			/* periodly sample the next points */
			del_timer(&ts->repeat_timer);
			ts->repeat_timer.expires = jiffies + msecs_to_jiffies(ts->repeat_time);
			add_timer(&ts->repeat_timer);
		}
#endif
		break;	
	default:
		break;
	}
out:               
#if	1
		enable_irq(ts->irq);
#else
	if (ts->pendown){
		queue_delayed_work(ts->wq, &ts->work, msecs_to_jiffies(10));
		ts->pendown = 0;
	}
	else{
		enable_irq(ts->irq);
	}
#endif
}

static irqreturn_t nas_irq(int irq, void *handle)
{
	struct ts_nas *ts = handle;
	//printk("enter nas_irq()\n");
#if 1
	if (!ts->get_pendown_state || likely(ts->get_pendown_state())) {
		//printk("disable_irq_nosync()\n");
		disable_irq_nosync(ts->irq);
		queue_delayed_work(ts->wq, &ts->work, 0);
	}

#endif
	return IRQ_HANDLED;
}

static void nas_free_irq(struct ts_nas *ts)
{
	free_irq(ts->irq, ts);
	if (cancel_delayed_work_sync(&ts->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(ts->irq);
	}
}


static ssize_t nas_tp_debug_ctl(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	char val;

	if (copy_from_user(&val, buffer, 1))
		return -EFAULT;

	if (val != '0' && val != '1')
		return -EINVAL;

	nas_tp_debug_flag = val - '0';

	return count;
}

static int __devinit nas_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct ts_nas *ts;
	struct nas_platform_data *pdata = pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err;
	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ts = kzalloc(sizeof(struct ts_nas), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->irq = client->irq;
	ts->input = input_dev;
	ts->status =0 ;// fjp add by 2010-9-30
	ts->pendown = 0; // fjp add by 2010-10-06

	ts->wq = create_rt_workqueue("nas_wq");
	INIT_DELAYED_WORK(&ts->work, nas_work);
	
#ifdef	CHECK_STATUS
	ts->wq1 = create_rt_workqueue("nas_wq1");
	INIT_DELAYED_WORK(&ts->work1, nas_status_check_work);
#endif	
	ts->model             = pdata->model;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = "nas Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	ts->has_relative_report = 0;
	input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_2)] = BIT_MASK(BTN_2); //jaocbchen for dual
#if	0
	input_set_abs_params(input_dev, ABS_X, 0, CONFIG_HANNSTAR_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, CONFIG_HANNSTAR_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0X, 0, CONFIG_HANNSTAR_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0Y, 0, CONFIG_HANNSTAR_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,0, CONFIG_HANNSTAR_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, CONFIG_HANNSTAR_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);   
#else
	input_set_abs_params(input_dev, ABS_X, 0, TOUCH_REPORT_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, TOUCH_REPORT_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0X, 0, TOUCH_REPORT_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0Y, 0, TOUCH_REPORT_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, TOUCH_REPORT_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, TOUCH_REPORT_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
#endif
	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	if (!ts->irq) {
		dev_dbg(&ts->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		ts->irq = gpio_to_irq(ts->irq);
	}
//miaozh modify
	err = request_irq(ts->irq, nas_irq, GPIOEdgelFalling,
			client->dev.driver->name, ts);
//	err = request_irq(ts->irq, nas_irq, 0,
//			client->dev.driver->name, ts);
	
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}
	
	if (err < 0)
		goto err_free_irq;
#if 0
	err = set_irq_type(ts->irq,IRQ_TYPE_LEVEL_LOW);
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}
	if (err < 0)
		goto err_free_irq;
#endif
	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);

	nas_check_firmwork(ts);
	
	nas_tp_debug_ctl_entry = create_proc_entry("nas_tp_debug_ctl", 0644, NULL);
	if (nas_tp_debug_ctl_entry) {
		nas_tp_debug_ctl_entry->read_proc = NULL;
		nas_tp_debug_ctl_entry->write_proc = nas_tp_debug_ctl;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&nastech_early_suspend);
#endif

#ifdef	CHECK_STATUS
	setup_timer(&ts->status_check_timer, nas_status_check_timer, (unsigned long)ts);
	ts->status_check_timer.expires  = jiffies + msecs_to_jiffies(1000);
	add_timer(&ts->status_check_timer);
#endif	
	return 0;

 err_free_irq:
	nas_free_irq(ts);
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
 err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

static int __devexit nas_remove(struct i2c_client *client)
{
	struct ts_nas *ts = i2c_get_clientdata(client);
	struct nas_platform_data *pdata = client->dev.platform_data;

	nas_free_irq(ts);

	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();

	input_unregister_device(ts->input);
	kfree(ts);
	
	if (nas_tp_debug_ctl_entry)
		remove_proc_entry("nas_tp_debug_ctl", NULL);

	return 0;
}

static struct i2c_device_id nas_idtable[] = {
	{ "nas_touch", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, nas_idtable);

static struct i2c_driver nas_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "nas_touch"
	},
	.id_table	= nas_idtable,
	.probe		= nas_probe,
	.remove		= __devexit_p(nas_remove),
};

static void __init nas_init_async(void *unused, async_cookie_t cookie)
{
	printk("--------> %s <-------------\n",__func__);
	i2c_add_driver(&nas_driver);
}

static int __init nas_init(void)
{
	async_schedule(nas_init_async, NULL);
	return 0;
}

static void __exit nas_exit(void)
{
	return i2c_del_driver(&nas_driver);
}
module_init(nas_init);
module_exit(nas_exit);
MODULE_LICENSE("GPL");

