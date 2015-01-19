#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/gpio.h>

#include <linux/i2c/uor7x5x.h>

//===============================================================//
//======   Touch Panel & Performance Related Parameters   =======//
#define FIRSTTOUCHCOUNT				1//
#define ONETOUCHCountAfter2			20//
#define JITTER_THRESHOLD			1000//­È
#define FIRST_TWO_TOUCH_FILTER		2//
#define JITTER_THRESHOLD_DXDY		80//¦¹­È
#define	MAX_READ_PERIOD				12//ms)//default 12ms
#define	DROP_POINT_DELAY			1//delay time(ms)
#define	SILDING_LENGTH				4//Sliding Window Length
//===============================================================//
//=============   Software Related Define   =============//
#define ZERO_TOUCH	0	
#define ONE_TOUCH	1
#define TWO_TOUCH	2
//===========================================================//
#define ABS(x) ((x)>0?(x):-(x))

typedef signed char VINT8;
typedef unsigned char VUINT8;
typedef signed short VINT16;
typedef unsigned short VUINT16;
typedef unsigned long VUINT32;
typedef signed long VINT32;

struct uor7x5x_touch_screen_struct {
	struct i2c_client *client;
	struct input_dev *dev;
	long xp;
	long yp;
	long pX;
	long pY;
	long pDX;
	long pDY;
	int count;
	int shift;
	unsigned char n_touch;
	
	wait_queue_head_t wq;
	spinlock_t lock;
	struct timer_list	ts_timer;
	unsigned char ges_status;
	unsigned char GesNo;
	struct uor7x5x_platform_data *pdata;
};

static struct uor7x5x_touch_screen_struct ts;

#ifdef CONFIG_PM
int uor_suspend(struct i2c_client *client, pm_message_t mesg)
{
	//disable your interrupt !!!
	disable_irq(client->irq);
	return 0;
}

int uor_resume(struct i2c_client *client)
{
	//enable your interrupt !!!
	enable_irq(client->irq);
	return 0;
}
#else
#define uor_suspend NULL
#define uor_resume  NULL
#endif

static int uor_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int uor_remove(struct i2c_client *client);

static const struct i2c_device_id uor_idtable[] = {
	{ "uor7x5x", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, uor_idtable);

static struct i2c_driver uor_i2c_driver = {
        .driver = {
                .name   = "uor7x5x",
                .owner  = THIS_MODULE,
        },
        .id_table	= uor_idtable,
        .probe          = uor_probe,
        .remove         = __devexit_p(uor_remove),
        .suspend	= uor_suspend,
        .resume		= uor_resume,	
};

VINT8 UOR_IICRead(VUINT8 Command, VUINT8 *readdata, VINT8 nread)
{
	struct i2c_msg msgs1[] = {
		{
			.addr	 	= ts.client->addr,//UOR7x5x
			.flags  	= 0,
			.len		= 1,
			.buf		= &Command,
		},
		{
			.addr	 	= ts.client->addr,//UOR7x5x
			.flags  	= I2C_M_RD,
			.len		= nread,
			.buf		= readdata,
		}
	};

	if (i2c_transfer(ts.client->adapter, msgs1, 2) != 2)//tranfer two messages
	{
		printk(KERN_ERR "[IIC_Read]: i2c_transfer() return error !!!\n");
		return -1;
	}
	return 0; 
}

VINT8 UOR_IICWrite(VUINT8 Command,VUINT8 *data,VINT8 nread)
{
	struct i2c_msg msgs1[] = {
		{
			.addr	 	= ts.client->addr,//UOR7x5x
			.flags  	= 0,
			.len		= (1+nread),
			.buf		= data,
		}
	};
	
	data[0] = Command;
	
	if (i2c_transfer(ts.client->adapter, msgs1, 1) != 1)//tranfer two messages
	{
		printk(KERN_ERR "[IICWrite]: i2c_transfer() return error !!!\n");
		return -1;
	}
	return 0; 
}

static struct workqueue_struct *queue = NULL;
static struct work_struct work;

static int FirstTC = 0,OneTCountAfter2 = 0,TWOTouchFlag = 0;
static int two_touch_count = 0;
static int pre_x1 = 0, pre_y1 = 0, pre_x2 = 0, pre_y2 = 0;

static int uor7x5x_init(void)
{
	unsigned char  buf[16];

	memset(buf, 0, sizeof(buf));
#if 0
	UOR_IICRead(0x01, buf, 2);
	//printk(KERN_ERR "%s: buf[0],buf[1]= 0x%x,0x%x \n",__FUNCTION__, buf[0], buf[1]);


	buf[1] = 0x10;
	buf[2] = 0x10;
	UOR_IICWrite(0x01 | 0x80, buf, 2);

	UOR_IICRead(0x01, buf, 2);
	//printk(KERN_ERR "%s:addr = 0x01, buf[0],buf[1]= 0x%x,0x%x \n",__FUNCTION__, buf[0], buf[1]);
#else
	UOR_IICRead(0x10 | 0x80, buf, 2);
	//printk(KERN_ERR "%s: buf[0],buf[1]= 0x%x,0x%x \n",__FUNCTION__, buf[0], buf[1]);
#endif	
	return 0;
}

static void uor7x5x_read_data(unsigned int	*nTouch, int *x1, int *y1, int *x2, int *y2)
{
	unsigned char	buf[16];
	
	memset(buf, 0, sizeof(buf));
/*
	// 8 bytes data burst read with Firmware Version 1.0 only
	UOR_IICRead(0x16, buf, 2);
	*nTouch = buf[1] >> 6 ;

	UOR_IICRead(0x39, buf, 8);
	*x1 = (buf[1]<<8) | buf[0];
	*y1 = (buf[3]<<8) | buf[2];
	*x2 = (buf[5]<<8) | buf[4];
	*y2 = (buf[7]<<8) | buf[6];
*/
	// 8 bytes compact data burst read with Firmware Version 1.2
	UOR_IICRead(0x37 | 0x80, buf, 8);
	*nTouch = buf[1] >> 6 ;
	
	*x1 = (buf[3]<<4) | (buf[2]>>4);
	*y1 = (buf[2]&0x0f)<<8 | buf[5];
	*x2 = (buf[4]<<4) | (buf[7]>>4);
	*y2 = (buf[7]&0x0f)<<8 | buf[6];
	
	//printk(KERN_ERR "%s: (x1,y1)=(%d,%d) (x2,y2)=(%d,%d) nTouch %d \n",__FUNCTION__, *x1, *y1, *x2, *y2, *nTouch);
}

#define		SILDING_BUFFER_LENGTH	SILDING_LENGTH+1
static int	buf_x1[SILDING_BUFFER_LENGTH];
static int	buf_y1[SILDING_BUFFER_LENGTH];
static int	buf_x2[SILDING_BUFFER_LENGTH];
static int	buf_y2[SILDING_BUFFER_LENGTH];

static int	sliding_count = 0 ;//number of item in buffer
static int	sliding_sum_x1 = 0 ;
static int	sliding_sum_y1 = 0 ;
static int	sliding_sum_x2 = 0 ;
static int	sliding_sum_y2 = 0 ;
static int	sliding_buf_index = 0 ;
//for single touch
static int	buf_x[SILDING_BUFFER_LENGTH];
static int	buf_y[SILDING_BUFFER_LENGTH];
static int	sliding_count_s = 0 ;//number of item in buffer
static int	sliding_sum_x = 0 ;
static int	sliding_sum_y = 0 ;
static int	sliding_buf_index_s = 0 ;

static void silding_init_s(void)
{
	memset(buf_x, 0, sizeof(buf_x));
	memset(buf_y, 0, sizeof(buf_y));

	sliding_count_s = 0 ;
	sliding_sum_x = 0 ;
	sliding_sum_y = 0 ;
	sliding_buf_index_s = 0 ;
}

static void silding_avg_s(int *x, int *y)
{
	//printk(KERN_ERR "before sliding:count %d, buf_index %d, (sum_x,sum_y)=(%d,%d) \n",sliding_count_s, sliding_buf_index_s, sliding_sum_x, sliding_sum_y);
	if(sliding_count_s == 0)//first point
	{
		sliding_count_s++;
		sliding_sum_x = *x ;
		sliding_sum_y = *y ;
		buf_x[sliding_buf_index_s] = *x ;
		buf_y[sliding_buf_index_s] = *y ;
		sliding_buf_index_s++;
	}
	else if(sliding_count_s < SILDING_LENGTH)//buffer not full
	{
		sliding_count_s++;
		sliding_sum_x += *x ;
		sliding_sum_y += *y ;
		buf_x[sliding_buf_index_s] = *x ;
		buf_y[sliding_buf_index_s] = *y ;
		sliding_buf_index_s = (sliding_buf_index_s + 1)%SILDING_LENGTH;
		*x = sliding_sum_x / sliding_count_s;
		*y = sliding_sum_y / sliding_count_s;		
	}
	else//buffer full
	{
		sliding_sum_x += *x ;
		sliding_sum_y += *y ;
		sliding_sum_x -= buf_x[sliding_buf_index_s];
		sliding_sum_y -= buf_y[sliding_buf_index_s];
		buf_x[sliding_buf_index_s] = *x ;
		buf_y[sliding_buf_index_s] = *y ;
		sliding_buf_index_s = (sliding_buf_index_s + 1)%SILDING_LENGTH;
		*x = sliding_sum_x / SILDING_LENGTH;
		*y = sliding_sum_y / SILDING_LENGTH;
	}
	//printk(KERN_ERR "after sliding:count %d, buf_index %d, (sum_x,sum_y)=(%d,%d) \n",sliding_count_s, sliding_buf_index_s, sliding_sum_x, sliding_sum_y);
}

static void silding_init(void)
{
	memset(buf_x1, 0, sizeof(buf_x1));
	memset(buf_y1, 0, sizeof(buf_y1));
	memset(buf_x2, 0, sizeof(buf_x2));
	memset(buf_y2, 0, sizeof(buf_y2));

	sliding_count = 0 ;
	sliding_sum_x1 = 0 ;
	sliding_sum_y1 = 0 ;
	sliding_sum_x2 = 0 ;
	sliding_sum_y2 = 0 ;
	sliding_buf_index = 0 ;
}

static void silding_avg(int *x1, int *y1, int *x2, int *y2)
{
	//printk(KERN_ERR "before sliding:count %d, buf_index %d, (sum_x1,sum_y1)=(%d,%d) (sum_x2,sum_y2)=(%d,%d)\n",sliding_count, sliding_buf_index, sliding_sum_x1, sliding_sum_y1, sliding_sum_x2, sliding_sum_y2);
	if(sliding_count == 0)//first point
	{
		sliding_count ++;
		sliding_sum_x1 = *x1 ;
		sliding_sum_y1 = *y1 ;
		sliding_sum_x2 = *x2 ;
		sliding_sum_y2 = *y2 ;
		buf_x1[sliding_buf_index] = *x1 ;
		buf_y1[sliding_buf_index] = *y1 ;
		buf_x2[sliding_buf_index] = *x2 ;
		buf_y2[sliding_buf_index] = *y2 ;
		sliding_buf_index ++;
	}
	else if(sliding_count < SILDING_LENGTH)//buffer not full
	{
		sliding_count ++;
		sliding_sum_x1 += *x1 ;
		sliding_sum_y1 += *y1 ;
		sliding_sum_x2 += *x2 ;
		sliding_sum_y2 += *y2 ;
		buf_x1[sliding_buf_index] = *x1 ;
		buf_y1[sliding_buf_index] = *y1 ;
		buf_x2[sliding_buf_index] = *x2 ;
		buf_y2[sliding_buf_index] = *y2 ;
		sliding_buf_index = (sliding_buf_index + 1)%SILDING_LENGTH;
		*x1 = sliding_sum_x1 / sliding_count;
		*y1 = sliding_sum_y1 / sliding_count;		
		*x2 = sliding_sum_x2 / sliding_count;
		*y2 = sliding_sum_y2 / sliding_count;
		
	}
	else//buffer full
	{
		sliding_sum_x1 += *x1 ;
		sliding_sum_y1 += *y1 ;
		sliding_sum_x2 += *x2 ;
		sliding_sum_y2 += *y2 ;
		sliding_sum_x1 -= buf_x1[sliding_buf_index];
		sliding_sum_y1 -= buf_y1[sliding_buf_index];
		sliding_sum_x2 -= buf_x2[sliding_buf_index];
		sliding_sum_y2 -= buf_y2[sliding_buf_index];		
		buf_x1[sliding_buf_index] = *x1 ;
		buf_y1[sliding_buf_index] = *y1 ;
		buf_x2[sliding_buf_index] = *x2 ;
		buf_y2[sliding_buf_index] = *y2 ;
		sliding_buf_index = (sliding_buf_index + 1)%SILDING_LENGTH;
		*x1 = sliding_sum_x1 / SILDING_LENGTH;
		*y1 = sliding_sum_y1 / SILDING_LENGTH;
		*x2 = sliding_sum_x2 / SILDING_LENGTH;
		*y2 = sliding_sum_y2 / SILDING_LENGTH;		
	}
	//printk(KERN_ERR "after sliding:count %d, buf_index %d, (sum_x1,sum_y1)=(%d,%d) (sum_x2,sum_y2)=(%d,%d)\n",sliding_count, sliding_buf_index, sliding_sum_x1, sliding_sum_y1, sliding_sum_x2, sliding_sum_y2);
}

static irqreturn_t uor_isr(int irq,void *dev_id)
{
	struct i2c_client *client = (struct i2c_client *)dev_id;
	
	//printk(KERN_ERR "uor.c: uor_isr\n");
	disable_irq_nosync(client->irq);
	queue_work(queue, &work);
	//printk(KERN_ERR "uor_isr ok!\n");
	return IRQ_HANDLED;
}

static void uor_read_loop(struct work_struct *data)
{
	int  x = 0, y = 0;
	unsigned int nTouch = 0;
	int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	int silding_x1 = 0, silding_x2 = 0, silding_y1 = 0, silding_y2 = 0;
	int silding_x = 0, silding_y = 0;
	int out_x = 0, out_y = 0, xy = 0;
	//printk(KERN_ERR "uor.c: uor_read_loop() !\n");

	while(1)
	{
		uor7x5x_read_data(&nTouch, &x1, &y1, &x2, &y2);
		//printk(KERN_ERR "%s: (x1,y1)=(%d,%d) (x2,y2)=(%d,%d) nTouch %d \n",__FUNCTION__, x1, y1, x2, y2, nTouch);
		if(nTouch == ONE_TOUCH)
		{
			if(x2==0 && y2==0)
			{
				x = x1;
				y = y1;
			}
			else if(x1!=0 && y1!=0 && x2!=0 && y2!=0)//nTouch = 1 & have two point value
			{
				continue;
			}
			else
			{
				x = x2;
				y = y2;
			}
		}
		
		ts.xp = x;
		ts.yp = y;
		x = ts.xp;
		y = ts.yp;
		
		if(nTouch == TWO_TOUCH)
		{
			silding_init_s();
			if(two_touch_count < FIRST_TWO_TOUCH_FILTER){
				//printk(KERN_ERR "%s:filter for first two touch -(x,y)=(%d,%d) (dx,dy)=(%d,%d),count = %d, FIRST_TWO_TOUCH_FILTER = %d  !!!\n",__FUNCTION__, x, y, Dx, Dy,two_touch_count, FIRST_TWO_TOUCH_FILTER);
				two_touch_count++;
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else if(x1==0 || y1 ==0 || x2==0 || y2==0 ){
				//printk(KERN_ERR "%s:filter for zero value(dual) --(x1,y1)=(%d,%d) ,(x2,y2)=(%d,%d) !!!\n",__FUNCTION__, x1, y1 , x2, y2);
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else if( (pre_x1!=0) && (pre_y1!=0) && ((ABS(pre_x1 - x1) > JITTER_THRESHOLD )|| (ABS(pre_y1 - y1) > JITTER_THRESHOLD ) )){
				//printk(KERN_ERR "%s:filter for jitter(dual) -- (pre_x1,pre_y1)=(%d,%d) ,(x1,y1)=(%d,%d) , JITTER_THRESHOLD = %d !!!\n",__FUNCTION__, pre_x1, pre_y1 , x1, y1, JITTER_THRESHOLD);
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else if( (pre_x2!=0) && (pre_y2!=0) && ((ABS(pre_x2 - x2) > JITTER_THRESHOLD )|| (ABS(pre_y2 - y2) > JITTER_THRESHOLD ) )){
				//printk(KERN_ERR "%s:filter for jitter(dual) -- (pre_x2,pre_y2)=(%d,%d) ,(x2,y2)=(%d,%d) , JITTER_THRESHOLD = %d !!!\n",__FUNCTION__, pre_x2, pre_y2 , x2, y2, JITTER_THRESHOLD);
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else{
			        //report dual touch p1 ,p2 to Linux/Android
			        //printk(KERN_ERR "%s:report dual touch for android (x1,y1)=(%d,%d) (x2,y2)=(%d,%d)  !!!\n",__FUNCTION__, x1, y1, x2, y2);
			        
			        //sliding avgerage
			        silding_x1 = x1;
			        silding_y1 = y1;
			        silding_x2 = x2;
			        silding_y2 = y2;
			        silding_avg(&silding_x1, &silding_y1, &silding_x2, &silding_y2);
			        //printk(KERN_ERR "%s: after silding avg:(x1,y1)=(%d,%d) (x2,y2)=(%d,%d)\n",__FUNCTION__, silding_x1, silding_y1, silding_x2, silding_y2 );
			        
			        input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 800 + ((x1+y1)%200));
			        //input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 500+press);
			        
			        xy = 0;
			        out_x = silding_x1;
			        out_y = silding_y1;
			        if(ts.pdata->convert){
			        	xy = ts.pdata->convert(out_x, out_y);
			        	out_x = xy >> 16;
			        	out_y = xy & 0xffff;
			        }
			        //printk(KERN_ERR "%s:TWO_TOUCH (x1,y1)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
			        
			        input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x);
			        input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y);
			        input_mt_sync(ts.dev);
			        
			        input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 800 + ((x1+y1)%200));
			        //input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 600+press);
			        
			        xy = 0;
			        out_x = silding_x2;
			        out_y = silding_y2;
			        if(ts.pdata->convert){
			        	xy = ts.pdata->convert(out_x, out_y);
			        	out_x = xy >> 16;
			        	out_y = xy & 0xffff;
			        }
			        //printk(KERN_ERR "%s:TWO_TOUCH (x2,y2)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
			        
			        input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x );
			        input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y );
			        input_mt_sync(ts.dev);
			        
			        input_sync(ts.dev);
			        
			        TWOTouchFlag = 1;
			        OneTCountAfter2 = 0;
			        
			        pre_x1 = x1;
			        pre_y1 = y1;
			        pre_x2 = x2;
			        pre_y2 = y2;
			        msleep(MAX_READ_PERIOD);
			}
		}
		else if(nTouch == ONE_TOUCH){
			silding_init();	
			if((TWOTouchFlag == 1) && (OneTCountAfter2 < ONETOUCHCountAfter2)){
				//printk(KERN_ERR "%s:filter after two touch -- (x,y)=(%d,%d) ,OneTCountAfter2 = %d, ONETOUCHCountAfter2 = %d !!!\n",__FUNCTION__, x, y, OneTCountAfter2, ONETOUCHCountAfter2);
				OneTCountAfter2++;
				pre_x1 = 0;
				pre_y1 = 0;
				pre_x2 = 0;
				pre_y2 = 0;
				
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}		
			else if((TWOTouchFlag == 0) && (FirstTC < FIRSTTOUCHCOUNT)){
				//printk(KERN_ERR "%s:filter before single touch -- (x,y)=(%d,%d) ,FirstTC = %d, FIRSTTOUCHCOUNT = %d !!!\n",__FUNCTION__, x, y, FirstTC, FIRSTTOUCHCOUNT);
				FirstTC++;
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else if( (ts.pX!=0) && (ts.pY!=0) && (ts.xp - ts.pX > JITTER_THRESHOLD || ts.pX - ts.xp > JITTER_THRESHOLD || ts.pY - ts.yp > JITTER_THRESHOLD || ts.yp - ts.pY > JITTER_THRESHOLD)){
				//printk(KERN_ERR "%s:filter for jitter -- (px,py)=(%d,%d) ,(x,y)=(%d,%d) , JITTER_THRESHOLD = %d !!!\n",__FUNCTION__, ts.pX, ts.pY ,x, y, JITTER_THRESHOLD);
				msleep(DROP_POINT_DELAY);
				continue;//re-start the loop
			}
			else{
			        //printk(KERN_ERR "%s:report single touch-- (x,y)=(%d,%d) !!!\n",__FUNCTION__, x, y);
			        //report x,y,pressure,size to Linux/Android
			        
			        //sliding avgerage
			        silding_x = ts.xp;
			        silding_y = ts.yp;
			        silding_avg_s(&silding_x , &silding_y);
			        //printk(KERN_ERR "%s: after silding avg:(x,y)=(%d,%d) \n",__FUNCTION__, silding_x, silding_y);
			        
			        input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 800 + ((ts.xp+ts.yp)%200) );
			        //input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 300);
			        
			        //printk(KERN_ERR "%s:ONE_TOUCH (silding_x,silding_y)=(%d,%d)\n",__FUNCTION__, silding_x, silding_y);
			        xy = 0;
			        out_x = silding_x;
			        out_y = silding_y;
			        if(ts.pdata->convert){
			        	xy = ts.pdata->convert(out_x, out_y);
			        	out_x = xy >> 16;
			        	out_y = xy & 0xffff;
			        }
			        //printk(KERN_ERR "%s:ONE_TOUCH (x1,y1)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
			        
			        input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x);
			        input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y);
			        input_mt_sync(ts.dev);
			        input_sync(ts.dev);
			        
			        //save previous single touch point
			        ts.pX = ts.xp; 
			        ts.pY = ts.yp;
			        pre_x1 = 0;
			        pre_y1 = 0;
			        pre_x2 = 0;
			        pre_y2 = 0;
			        
			        msleep(MAX_READ_PERIOD);
			}
		}
		else if(nTouch == ZERO_TOUCH){
			input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 0 );
			//input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(ts.dev);
			input_sync(ts.dev);
			
			//reset filter parameters
			FirstTC = 0;
			OneTCountAfter2 = 0;
			TWOTouchFlag = 0;
			two_touch_count = 0;
			ts.xp= 0;
			ts.yp = 0;
			ts.pX = 0;
			ts.pY = 0;
			
			pre_x1 = 0;
			pre_y1 = 0;
			pre_x2 = 0;
			pre_y2 = 0;
			silding_init();
			silding_init_s();
			msleep(1);
			
			//set interrupt to high by software
			//gpio_direction_output(GPIO_00_PIN,1);
			//gpio_direction_input(GPIO_00_PIN);
			enable_irq(ts.client->irq);
			break;
		}
		else{
		        printk(KERN_ERR "uor_read_loop(): n_touch state error !!!\n");
		}
	}	
	//printk(KERN_ERR "%s: exit while loop !!!\n",__FUNCTION__ );	
}

static int uor_register_input(void)
{
    int ret;
    struct input_dev *	input_device;
    
    input_device = input_allocate_device();
    if (!input_device) {
    	printk(KERN_ERR "Unable to allocate the input device !!\n");
    	return -ENOMEM;
    }
    
    input_device->name = "UOR Touchscreen";
    
    ts.dev = input_device;
    __set_bit(EV_ABS, ts.dev->evbit);
    __set_bit(EV_SYN, ts.dev->evbit);
    
    input_set_abs_params(ts.dev, ABS_MT_TOUCH_MAJOR, 0, 1000, 0, 0);
    //input_set_abs_params(codec_ts_input, ABS_MT_WIDTH_MAJOR, 0, 1000, 0, 0);
    
    int max_x = ts.pdata->abs_xmax ? ts.pdata->abs_xmax : 4095;
    input_set_abs_params(ts.dev, ABS_MT_POSITION_X, 0, max_x, 0, 0);
    
    int max_y = ts.pdata->abs_ymax ? ts.pdata->abs_ymax : 4095;
    input_set_abs_params(ts.dev, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
    
    ret = input_register_device(ts.dev);
    if (ret) {
    	input_free_device(ts.dev);
    	printk(KERN_ERR "%s: unabled to register input device, ret = %d\n", __FUNCTION__, ret);
    	return ret;
    }
    return 0;
}

static int __init uor_init(void)
{
	int ret;
	
	//printk(KERN_ERR "uor.c: uor_init() !\n");
		
	memset(&ts, 0, sizeof(struct uor7x5x_touch_screen_struct));//init data struct ts

	ret = i2c_add_driver(&uor_i2c_driver);
	if(ret < 0)
		printk(KERN_ERR "uor.c: i2c_add_driver() fail in uor_init()!\n");	
	silding_init();	
	silding_init_s();
	return ret;
}

static int __devinit uor_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
        int err = 0;
        struct uor7x5x_platform_data *pdata = pdata = client->dev.platform_data;
        
        printk(KERN_ERR "uor.c: uor_probe() !\n");
	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	        return -EIO;
	        
	queue = create_singlethread_workqueue("uor-touch-screen-read-loop");
	INIT_WORK(&work, uor_read_loop);
	
	if (pdata->init_irq)
		pdata->init_irq();
	ts.client = client; // save the client we get
	ts.pdata = pdata;	
	
	if (uor_register_input() < 0) {
		dev_err(&client->dev, "register input fail!\n");
		return -ENOMEM;
	}
	
	//uor7x5x_init();
	
	//printk(KERN_ERR "driver name is <%s>,client->irq is %d,INT_GPIO_0 is %d\n",client->dev.driver->name, client->irq, INT_GPIO_0);
	err = request_irq(client->irq, uor_isr, IRQF_TRIGGER_FALLING,client->dev.driver->name, client);
	if(err < 0){
		printk(KERN_ERR "uor.c: Could not allocate GPIO intrrupt for touch screen !!!\n");
		free_irq(client->irq, client);
		err = -ENOMEM;
		return err;	
	}
	printk(KERN_ERR "uor_probe ok!\n");
	return err;
}

static int __devexit uor_remove(struct i2c_client *client)
{
	free_irq(client->irq, client);
	return 0;
}

static void __exit uor_exit(void)
{
	i2c_del_driver(&uor_i2c_driver);  
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(uor_init);
#else
module_init(uor_init);
#endif
module_exit(uor_exit);

MODULE_DESCRIPTION("UOR Touchscreen driver");
MODULE_AUTHOR("Ming-Wei Chang <mingwei@uutek.com.tw>");
MODULE_LICENSE("GPL");
