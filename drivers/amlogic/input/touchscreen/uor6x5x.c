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

#include <linux/i2c/uor6x5x.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend uor6x5x_early_suspend;
#endif

#define TRUE	1
#define FALSE 	0
#define UOR6X5X_DEBUG  FALSE

#define InitX		0x00
#define InitY		0x10
#define MSRX_2T         0x40
#define MSRY_2T         0x50
#define MSRX_1T         0xC0
#define MSRY_1T         0xD0
#define MSRZ1_1T        0xE0
#define MSRZ2_1T        0xF0

//#define	GESTURE_IN_DRIVER//!!! Turn on Gesture Detection in Driver !!!
#define Tap				1	//Tap
#define RHorizontal		3	//Right horizontal
#define LHorizontal		4	//Left horizontal
#define UVertical			5	//Up vertical
#define DVertical			6	//Down vertical
#define RArc				7	//Right arc
#define LArc				8	//Left arc
#define CWCircle			9	//Clockwise circle
#define CCWCircle		10	//Counter-clockwise circle
#define RPan				11	//Right pan
#define LPan				12	//Left pan
#define DPan				13	//Right pan
#define UPan				14	//Left pan
#define PressTap			15	//Press and tap
#define PinchIn			16	//Pinch in
#define PinchOut			17	//Pinch out

#define R_Threshold 	       11000       //ting
#define R_Threshold2 	450	//ting //	600

#define ZERO_TOUCH	0	
#define ONE_TOUCH	1
#define TWO_TOUCH	2

#define DX_T			48	//	72
#define DY_T			48	//	72
#define DXY_SKIP		0x80

#define NumberFilter		6
#define NumberDrop		4	 //This value must not bigger than (NumberFilter-1)

#define WMT_FILTER_NUM 4

#define FIRSTTOUCHCOUNT		1    //ting
#define ONETOUCHCountAfter2 	500
#define JITTER_THRESHOLD   	       800 //ting
#define MAX_READ_PERIOD		6  //ting
#define FIRST_TWO_TOUCH_FILTER	1
#define JITTER_THRESHOLD_DXDY	24
#define PERIOD_PER_FILTER	1

#define	DROP_POINT_DELAY_J	  msecs_to_jiffies(1)
#define	READ_DATA_DELAY_J	  msecs_to_jiffies(6)  

#define FILTER_FUNC
#define NFilt NumberFilter
#define NDrop NumberDrop

typedef signed char VINT8;
typedef unsigned char VUINT8;
typedef signed short VINT16;
typedef unsigned short VUINT16;
typedef unsigned long VUINT32;
typedef signed long VINT32;

struct uor6x5x_touch_screen_struct {
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
	struct mutex mutex;
	struct work_struct work_q;
	struct uor6x5x_platform_data *pdata;
};

static struct uor6x5x_touch_screen_struct ts;

#ifdef CONFIG_PM
static int uor_suspend(struct i2c_client *client, pm_message_t mesg)
{
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor6x5x.c: uor_suspend() !\n");
	disable_irq(client->irq);
	return 0;
}

static int uor_resume(struct i2c_client *client)
{
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor6x5x.c: uor_resume() !\n");
	enable_irq(client->irq);
	return 0;
}
#else
#define uor_suspend NULL
#define uor_resume  NULL
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void uor_early_suspend(struct early_suspend *h)
{
    struct i2c_client *client = (struct i2c_client *)(h->param);
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor6x5x.c: uor_early_suspend() !\n");
	disable_irq(client->irq);
}

static void uor_late_resume(struct early_suspend *h)
{
    struct i2c_client *client = (struct i2c_client *)(h->param);
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor6x5x.c: uor_late_resume() !\n");
	enable_irq(client->irq);
}
#endif

static int uor_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int uor_remove(struct i2c_client *client);

static const struct i2c_device_id uor_idtable[] = {
	{ "uor6x5x", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, uor_idtable);

static struct i2c_driver uor_i2c_driver = {
        .driver = {
                .name   = "uor6x5x",
                .owner  = THIS_MODULE,
        },
        .id_table	= uor_idtable,
        .probe          = uor_probe,
        .remove         = __devexit_p(uor_remove),
        .suspend	= uor_suspend,
        .resume 	= uor_resume,	
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

static int uor_get_pendown_state(void)
{
    int state = 0;

    if (ts.pdata && ts.pdata->get_irq_level)
        state = ts.pdata->get_irq_level();

    return state;
}

int xFilter[NFilt], yFilter[NFilt],DxFilter[NFilt], DyFilter[NFilt];
unsigned int XYIndex = 0;
static int RX=1024;

int  XYFilter(int *xFilter, int *yFilter, int Num,int Drop){
	unsigned int i,SumTempX=0,SumTempY=0;
	int Dp,checkSmx,checkSmy,checkBgx,checkBgy;
	int SmX =0, SmY = 0;
	int LaX = 0, LaY = 0;
	int SmInX = 0, SmInY = 0;
	int LaInX = 0, LaInY =0;

	if( (Num <=2) && (Drop > (Num-1)) )
		return FALSE; // not enough to sample
		
	for(i=0;i<Num;i++){
		SumTempX += xFilter[i];
		SumTempY += yFilter[i];
	}
	
	Dp = Drop;

	checkSmx = 0;
	checkSmy = 0;
	checkBgx = 0;
	checkBgy = 0;
	while(Dp>0){
		SmX = 0x0FFF;SmY = 0x0FFF;
		LaX = 0x0;LaY = 0x0;
		SmInX = 0;SmInY = 0;
		LaInX = 0;LaInY =0;
		for(i =  0; i < Num; i++){
			if(checkSmx&(1<<i)){
			}else if(SmX > xFilter[i]){
				SmX = xFilter[i];
				SmInX= i;
			}
			if(checkSmy&(1<<i)){
			}else if(SmY > yFilter[i]){
				SmY = yFilter[i];
				SmInY = i;
			}
			
			if(checkBgx&(1<<i)){
			}else if(LaX < xFilter[i]){
				LaX = xFilter[i];
				LaInX = i;
			}
			
			if(checkBgy&(1<<i)){
			}else if(LaY < yFilter[i]){
				LaY = yFilter[i];
				LaInY = i;
			}
		}
		if(Dp){
			SumTempX-= xFilter[SmInX];
			SumTempX-= xFilter[LaInX];
			SumTempY-= yFilter[SmInY];
			SumTempY-= yFilter[LaInY];
			Dp -= 2;
			if(UOR6X5X_DEBUG)
				printk(KERN_ERR "in filter :SmInX %d,LaInX %d, SmInY %d , LaInY %d!!!\n", SmInX,LaInX, SmInY, LaInY);
		}
		checkSmx |= 1<<SmInX;
		checkSmy |= 1<<SmInY;
		checkBgx |= 1<<LaInX;
		checkBgy |= 1<<LaInY;
	}
	
	xFilter[0] = SumTempX/(Num-Drop);
	yFilter[0] = SumTempY/(Num-Drop);
	
	return TRUE;
}


VINT8 Init_UOR_HW(void){

	VUINT8   i,icdata[2];
	VUINT32   Dx_REF,Dy_REF,Dx_Check,Dy_Check;
	int		  TempDx[NumberFilter],TempDy[NumberFilter];


	for(i=0;i<NumberFilter;i++){
		UOR_IICRead(InitX,icdata,2);
		TempDx[i] = (icdata[0]<<4 | icdata[1]>>4);
        UOR_IICRead(InitY,icdata,2);
		TempDy[i] = (icdata[0]<<4 | icdata[1]>>4);
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "filter test:#%d (x,y)=(%d,%d) !!!\n", i,TempDx[i], TempDy[i]);
	}
	XYFilter(TempDx,TempDy,NumberFilter,2);
    Dx_REF = TempDx[0];
    Dy_REF = TempDy[0];
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "filter result:(x,y)=(%d,%d) !!!\n", Dx_REF, Dy_REF);
	
	i = 0;
	do{

		UOR_IICRead(InitX,icdata,2);
		Dx_Check = abs((icdata[0]<<4 | icdata[1]>>4) - Dx_REF);
		UOR_IICRead(InitY,icdata,2);
		Dy_Check = abs((icdata[0]<<4 | icdata[1]>>4) - Dy_REF);

		i++;

		if(i>NumberFilter)
			return -1;

	}while(Dx_Check > 4 || Dy_Check > 4);

	return 0;
}

#ifdef  GESTURE_IN_DRIVER
VINT8 PressAndTap = 0,n_touch=0,circle_flag = 0;
VUINT8 touch_flag[2]={0,0},direction_flag1[4],flag_pinch = 0,flag_pan = 0,flag_count = 0;
VUINT16 Dx1,Dx2,Dy1,Dy2,X1,X2,Y1,Y2,XX1,XX2,YY1,YY2;
VUINT16 DIS_1T,DIS_PAN,DIS_PINCH;
VUINT8 RightPan = 0, LeftPan = 0, DownPan = 0, UpPan = 0,circle_direction_flag=0;

void SendGestureKey(VUINT8 Gesture ){
		switch(Gesture){
		case Tap:
			ts.GesNo = 'T';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is TAP\r\n");
			break;
		case RHorizontal:
			ts.GesNo = 'R';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is RHorizontal\r\n");
			break;
		case LHorizontal:
			ts.GesNo = 'L';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is LHorizontal\r\n");
			break;
		case UVertical:
			ts.GesNo = 'U';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is UVertical\r\n");
			break;
		case DVertical:
			ts.GesNo = 'D';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is DVertical\r\n");
			break;
		case RArc:
			ts.GesNo = 'A';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is RArc\r\n");
			break;
		case LArc:
			ts.GesNo = 'A';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is LArc\r\n");
			break;
		case CWCircle:
			ts.GesNo = 'C';
			if(UOR6X5X_DEBUG
				printk(KERN_INFO "Gesture is CWCircle\r\n");
			break;
		case CCWCircle:
			ts.GesNo = 'c';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is CCWCircle\r\n");
			break;
		case RPan:
			ts.GesNo = 'r';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is RPan\r\n");
			break;
		case LPan:
			ts.GesNo = 'l';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is LPan\r\n");
			break;
		case DPan:
			ts.GesNo = 'd';		
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is DPan\r\n");
			break;
		case UPan:
			ts.GesNo = 'u';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is UPan\r\n");
			break;
		case PressTap:
			ts.GesNo = 'p';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is PressTap\r\n");
			break;
		case PinchIn:
			ts.GesNo = 'I';
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is PinchIn\r\n");
			break;
		case PinchOut:
			ts.GesNo = 'O';	
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture is PinchOut\r\n");
			break;
		default:
			if(UOR6X5X_DEBUG)
				printk(KERN_INFO "Gesture key is %d \n", Gesture);
			break;
		}
}


VUINT8 check_event(VUINT16 x1,VUINT16 x2,VUINT16 y1,VUINT16 y2,VUINT16 dis){
	
	VUINT8 direction = 0x00;
	VUINT16 absX = 0, absY = 0;
 	//Decide what the direction is from (x1,y1) to (x2,y2)
 	absX = (x1>x2)?(x1-x2):(x2-x1);
	absY = (y1>y2)?(y1-y2):(y2-y1);
	if(absX>dis)
		direction |= (x1 > x2 ? 0x02:0x01);		//0x01:Right	0x02:Left
	else if(absY>dis)
		direction |= (y1 > y2 ? 0x08:0x04);		//0x08:Up		0x04:Down

	return direction;
}

//Gesture decision function
VUINT8 gesture_decision(VUINT8 n_touch,VUINT16 x,VUINT16 y, VUINT16 Dx, VUINT16 Dy){

	VUINT8 gesture =0,flag_d=0,flag_v = 0;
	VUINT16 event_flag = 0;

	DIS_PINCH = 0x28;	//	0x18;
//	DIS_PAN = 2500;
	DIS_PAN = 800;
//	DIS_1T = 500;
	DIS_1T = 150;	//	350;
	

		if(n_touch == 2){
		//Number of touch is 2,
		//clear flags for 1-touch gesture decision.
		touch_flag[0] = 0;
		memset(direction_flag1,0,4);

		if(!touch_flag[1]){ 
			//start a new 2Touch gesture decision flow
			touch_flag[1] = 1;
			PressAndTap = 1;
			XX1 = x;
			YY1 = y;
			Dx1 = Dx;
			Dy1 = Dy;
		}else{
			//Check to see if any 2-Touch event happens.

			XX2 = x;
			YY2 = y;

			//If the variation in Dx or Dy is bigger than DIS_PINCH or the variation in 
			//the coordinate of central point between the two touch is bigger than DIS_PAN,
			//clear the flag PressAndTap.
			if(check_event(XX1,XX2,YY1,YY2,50))
				PressAndTap = 0;

			Dx2 = Dx;
			Dy2 = Dy;
/***********************擋掉跳躍超過距離DXY_SKIP的兩點*****************************************/
			if(check_event(Dx1,Dx2,Dy1,Dy2,DXY_SKIP)){
				Dx1 = Dx2;
				Dy1 = Dy2;
				return 0;
			}
/****************************************************************************************/
			if(flag_v = check_event(Dx1,Dx2,Dy1,Dy2,DIS_PINCH)){
				//If the variation in Dx or Dy is bigger than DIS_PINCH,
				//return gesture PinchIn/PinchOut.
				PressAndTap = 0;
				Dx1 = Dx2;
				Dy1 = Dy2;
				XX1 = XX2;
				YY1 = YY2;
				switch(flag_v){
				case 0x1:
				case 0x4:
					gesture = PinchOut;
				break;
				case 0x2:
				case 0x8:
					gesture = PinchIn;
				break;

				default:
					gesture = 0x2f;
				break;
				}

			}else if(flag_d = check_event(XX1,XX2,YY1,YY2,DIS_PAN)){
				//If the variation in Dx or Dy is smaller than DIS_PINCH and the variation in 
				//the coordinate of central point between the two touch is bigger than DIS_PAN,
				//set the flag RightPan/LeftPan
				PressAndTap = 0;

				Dx1 = Dx2;
				Dy1 = Dy2;
				XX1 = XX2;
				YY1 = YY2;
				switch(flag_d){
				case 0x1:
					RightPan = 1;
				break;
				case 0x2:
					LeftPan = 1;
				break;

				case 0x4:
					DownPan = 1;
				break;
				case 0x8:
					UpPan = 1;
				break;

				default:
					gesture = 0x20;
				break;
				}
			}
		}

	}else if(PressAndTap){
	  	//n_touch is changed from 2 to 1 or from 2 to 0, 
		//and the flag PressAndTap is set.
		gesture = PressTap;
		PressAndTap = 0;
		touch_flag[0] = 0;
		touch_flag[1] = 0;
		memset(direction_flag1,0,4);

	}else if(n_touch == 1){
		//Number of touch is 1,
		//clear flags for 2-touch gesture decision.
		touch_flag[1] = 0;

		if(!touch_flag[0]){ 
			//start a new gesture decision flow
			memset(direction_flag1,0,4);

			touch_flag[0] = 1;
			X1 = x;
			Y1 = y;
		}else{
			//Check to see if any 1-Touch event happens(Up/Down/Right/Left).
			X2 = x;
			Y2 = y;

			if(flag_d = check_event(X1,X2,Y1,Y2,DIS_1T)){
				X1 = X2;
				Y1 = Y2;
				
				direction_flag1[flag_count] = flag_d;

				if(circle_flag){
					//If the previous recognized gesture is circle.	
					event_flag = circle_direction_flag<<4 | direction_flag1[0];
					switch(event_flag){
					case 0x14:
					case 0x42:
					case 0x28:
					case 0x81:
						gesture = CWCircle;
					break;
					case 0x24:
					case 0x41:
					case 0x18:
					case 0x82:
						gesture = CCWCircle;
					break;
					default:
						gesture = 0;
					break;
					}
					circle_direction_flag = direction_flag1[0];
					flag_count=0;
					circle_flag = 1;
				}else if(flag_count == 0){
					flag_count++;
					
				}else if(flag_count <3){
				
					if(direction_flag1[flag_count] != direction_flag1[flag_count-1]){
						flag_count++;
					}
						
				}else if(direction_flag1[flag_count] != direction_flag1[flag_count-1]){
					event_flag = direction_flag1[3] | direction_flag1[2]<<4 | direction_flag1[1] << 8 | direction_flag1[0]<<12;
					switch(event_flag){
					case 0x1428:
					case 0x4281:
					case 0x2814:
					case 0x8142:
						circle_direction_flag = direction_flag1[3];
						gesture = CWCircle;

						flag_count=0;
						circle_flag = 1;
						memset(direction_flag1,0,4);

					break;

					case 0x2418:
					case 0x4182:
					case 0x1824:
					case 0x8241:
						circle_direction_flag = direction_flag1[3];
						gesture = CCWCircle;

						flag_count=0;
						circle_flag = 1;
						memset(direction_flag1,0,4);

					break;
					default:
						gesture = 0;
					break;
					}//switch

				}//if(direction_flag1[3]!=direction_flag1[2])
				
			}
		}
	}else if(n_touch == 0){
		
		if(circle_flag){
			circle_flag=0;
			touch_flag[0] = 0;
			flag_count=0;
			memset(direction_flag1,0,4);
		}
		
		if(direction_flag1[2]){
   			event_flag = direction_flag1[2] | direction_flag1[1] << 4 | direction_flag1[0]<<8;
			switch(event_flag){
			
			case 0x144:
			case 0x142:
			case 0x422:
			case 0x428:
			case 0x288:
			case 0x281:
			case 0x811:
			case 0x814:
				gesture = RArc;
			break;
			
			case 0x244:
			case 0x241:
			case 0x411:
			case 0x418:
			case 0x188:
			case 0x182:
			case 0x822:
			case 0x824:
				gesture = LArc;
			break;

			default:
				gesture = 0;
			break;
			}
		}else if(direction_flag1[1]){
			event_flag = direction_flag1[1] | direction_flag1[0] << 4;
			switch(event_flag){
			case 0x11:
				gesture = RHorizontal;
			break;

			case 0x22:
				gesture = LHorizontal;
			break;
						
			case 0x88:
				gesture = UVertical;
			break;
		
			case 0x44:
				gesture = DVertical;
			break;
		
			case 0x14:
			case 0x42:
			case 0x28:
			case 0x81:
				gesture = RArc;
			break;
		
			case 0x24:
			case 0x41:
			case 0x18:
			case 0x82:
				gesture = LArc;
			break;
			
			default:
				gesture = 0;
			break;
			}
			
		}else if(direction_flag1[0]){
			switch(direction_flag1[0]){
			case 1:
				gesture = RHorizontal;
			break;
			case 2:
				gesture = LHorizontal;
			break;
			case 4:
				gesture = DVertical;
			break;
			case 8:
				gesture = UVertical;
			break;
			}
		}else if(RightPan){
			gesture = RPan;
			RightPan = 0;
		}else if(LeftPan){
			gesture = LPan;
			LeftPan = 0;

		}else if(UpPan){
			gesture = UPan;
			UpPan = 0;
		}else if(DownPan){
			gesture = DPan;
			DownPan = 0;

		}else if(touch_flag[0] && !check_event(X1,X2,Y1,Y2,50)){
			gesture = Tap;
		}
	
		flag_count=0;
		touch_flag[0] = 0;
		touch_flag[1] = 0;
		memset(direction_flag1,0,4);
	}
	return gesture;
}
#endif

struct ST_UOR_BUF {
		unsigned short pXbuf[WMT_FILTER_NUM];
		unsigned short pYbuf[WMT_FILTER_NUM];
		unsigned short pDXbuf[WMT_FILTER_NUM];
		unsigned short pDYbuf[WMT_FILTER_NUM];
};

static unsigned short uor_avg_XY(unsigned short *pData )
{
	int i,j,k=1;
	unsigned int avg;
	
	for(i=WMT_FILTER_NUM-1; i>0;i--){
		for(j=i-1;j>=0;j--){
			if(pData[j] > pData[i]){
				k =pData[j];
				pData[j] = pData[i];
				pData[i] = k;
			}			
		}
	}
	i = WMT_FILTER_NUM/2;
	avg = pData[i]+pData[i-1];

        if(abs(pData[i]-pData[i-1])>350)
           avg=4096;
        else
           avg/=2;
	return avg;
}

static unsigned short uor_avg_DXY(unsigned short *pData )
{
	int i,j,k,swap = 1;
	unsigned int avg;
	
	for(i=WMT_FILTER_NUM-1; i>0;i--){
		for(j=i-1;j>=0;j--){
			if(pData[j] > pData[i]){
				k =pData[j];
				pData[j] = pData[i];
				pData[i] = k;
			}			
		}
	}
	i = WMT_FILTER_NUM/2;
	avg =pData[i]+pData[i-1];
        if(abs(pData[i]-pData[i-1])>12)
           avg=256;
        else
           avg /=2;

	return avg;
}

static void uor_read_XY(unsigned short *X, unsigned short *Y)
{
	int i = 0;
	VUINT8 EpBuf[2];
	struct ST_UOR_BUF uor_point;
	
	for(i=0; i<WMT_FILTER_NUM;i++){
		memset(EpBuf, 0, sizeof(EpBuf));
		UOR_IICRead(MSRX_1T, EpBuf, 2);
		uor_point.pXbuf[i] = (EpBuf[0]<<4)|(EpBuf[1]>>4);
	
		UOR_IICRead(MSRY_1T,  EpBuf, 2);
		uor_point.pYbuf[i] = (EpBuf[0]<<4)|(EpBuf[1]>>4);
	}	

	*X = uor_avg_XY(uor_point.pXbuf);
	*Y = uor_avg_XY(uor_point.pYbuf);

	return;
}

static void uor_read_DXY(unsigned short *DX, unsigned short *DY)
{
	int i = 0;
	VUINT8 EpBuf[4];
	struct ST_UOR_BUF uor_point;
	
	for(i=0; i<WMT_FILTER_NUM;i++){
		memset(EpBuf, 0, sizeof(EpBuf));
		UOR_IICRead(MSRX_2T,  (EpBuf), 3);
		uor_point.pDXbuf[i] = EpBuf[2];
	
		UOR_IICRead(MSRY_2T,  (EpBuf), 3);
		uor_point.pDYbuf[i] = EpBuf[2];
	}	

	*DX = uor_avg_DXY(uor_point.pDXbuf);
	*DY = uor_avg_DXY(uor_point.pDYbuf);

	return;
}

static struct workqueue_struct *queue = NULL;
static struct delayed_work work;
static int FirstTC = 0,OneTCountAfter2 = 0,TWOTouchFlag = 0;
static int two_touch_count = 0, pre_dx = 0, pre_dy = 0;
static int pre_outx1 = 0, pre_outy1 = 0, pre_outx2 = 0, pre_outy2 = 0;

static void uor_read_data(unsigned short *X, unsigned short *Y, 
	unsigned short *DX, unsigned short *DY)
{

	VUINT8 EpBuf[4];
	unsigned short	x, y;
	unsigned short	Dx, Dy;

	memset(EpBuf, 0, sizeof(EpBuf));
	UOR_IICRead(MSRX_1T, EpBuf, 2);
	x= EpBuf[0]; 
	x <<=4;
	x |= (EpBuf[1]>>4);
	
	UOR_IICRead(MSRY_1T,  EpBuf, 2);
	y = EpBuf[0]; 
	y <<=4;
	y |= (EpBuf[1]>>4);
	
	UOR_IICRead(MSRX_2T,  (EpBuf), 3);
	Dx = EpBuf[2];
	
	UOR_IICRead(MSRY_2T,  (EpBuf), 3);
	Dy = EpBuf[2];	
	

	*X = x;
	*Y = y;
	*DX = Dx;
	*DY = Dy;
}

static void uor_read_loop(struct work_struct *data)
{
	VUINT8 EpBuf[4];
	unsigned short SBufx_0 = 0;
	unsigned short SBufx_1 = 0;
	unsigned short SBufx_2 = 0;
	unsigned short SBufy_0 = 0;
	unsigned short SBufy_1 = 0;
	unsigned short SBufy_2 = 0;
	unsigned short SBufxs_0 = 0;
	unsigned short SBufxs_1 = 0;
	unsigned short SBufxs_2 = 0;
	unsigned short SBufys_0 = 0;
	unsigned short SBufys_1 = 0;
	unsigned short SBufys_2 = 0;
	unsigned short x, y , y1, x2, y2;
	unsigned short out_x, out_y;
	unsigned short Dx, Dy, z1, z2, Dx1, Dx2, Dy1, Dy2;
	unsigned int R_touch;
	unsigned int Rt;
	unsigned int nTouch = 0;
	int xy = 0;
	unsigned int slid_index=0;
	unsigned int slid_indexs=0;
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor.c: uor_read_loop() !\n");
	
	while(1){
        if(!uor_get_pendown_state()) {
			#ifdef FILTER_FUNC
			uor_read_XY(&x, &y);
			uor_read_DXY(&Dx, &Dy);
			ts.xp = x-22;
			ts.yp = y-22;
			/*
			//first point
			uor_read_data(&x, &y, &Dx, &Dy);
			xFilter[ts.count] = x;
			yFilter[ts.count] = y;	
			DxFilter[ts.count] = Dx;
			DyFilter[ts.count] = Dy;
			//printk(KERN_ERR "Data before filter:#%d (x,y)=(%d,%d) (dx,dy)=(%d,%d) !!!\n",ts.count , x, y, Dx, Dy);
			ts.count ++;
			//udelay(PERIOD_PER_FILTER);  //ting    //Per Read Point Delay

			while(ts.count < NFilt)//collect other point 
			{
			    uor_read_data(&x, &y, &Dx, &Dy);
				xFilter[ts.count] = x;
				yFilter[ts.count] = y;	
				DxFilter[ts.count] = Dx;
				DyFilter[ts.count] = Dy;
				//printk(KERN_ERR "Data before filter:#%d (x,y)=(%d,%d) (dx,dy)=(%d,%d) !!!\n",ts.count , x, y, Dx, Dy);
				ts.count ++;
			}

			if(!XYFilter(xFilter, yFilter, NFilt,NDrop)){ // no correct point	
			    printk(KERN_ERR "%s: X Y filter error !!!\n",__FUNCTION__);
			}

			ts.xp =xFilter[0];
			ts.yp =yFilter[0];					

			if(!XYFilter(DxFilter, DyFilter, NFilt,NDrop)){ // no correct point
			    printk(KERN_ERR "%s: DX DY filter error !!!\n",__FUNCTION__);
			}
			Dx = DxFilter[0];
			Dy = DyFilter[0];
			ts.count = 0;
			*/
			#else // no filter
			memset(EpBuf, 0, sizeof(EpBuf));
			
			UOR_IICRead(MSRX_1T, EpBuf, 2);
			x= EpBuf[0]; 
			x<<=4;
			x|= (EpBuf[1]>>4);
			
			UOR_IICRead(MSRX_1T, EpBuf, 2);
			x2= EpBuf[0]; 
			x2<<=4;
			x2|= (EpBuf[1]>>4);
			
			UOR_IICRead(MSRY_1T,  EpBuf, 2);
			y1= EpBuf[0]; 
			y1<<=4;
			y1 |= (EpBuf[1]>>4);
			
			UOR_IICRead(MSRY_1T,  EpBuf, 2);
			y2= EpBuf[0]; 
			y2<<=4;
			y2 |= (EpBuf[1]>>4);
			
			if (abs(x-x2)>500)
			    ts.xp = 4095;
			else
			    ts.xp = (x+x2)/2;
				
			if (abs(y1-y2)>500)
			    ts.yp = 4095;
			else
			    ts.yp = (y1+y2)/2;
			    
			UOR_IICRead(MSRX_2T,  (EpBuf), 3);
			Dx1 = EpBuf[2];
			
			UOR_IICRead(MSRX_2T,  (EpBuf), 3);
			Dx2 = EpBuf[2];
			
			UOR_IICRead(MSRY_2T,  (EpBuf), 3);
			Dy1 = EpBuf[2];	
			
			UOR_IICRead(MSRY_2T,  (EpBuf), 3);
			Dy2 = EpBuf[2];	
			
			if ((Dx1-Dx2)>16||(Dx2-Dx1)>16)
			    Dx = 256;
			else
			    Dx =( (Dx1+Dx2)>>1)-20;
				
			if ((Dy1-Dy2)>16 ||(Dy2-Dy1)>16)
			    Dy = 256;
			else
			    Dy = ((Dy1+Dy2)>>1)-20;
			#endif
			

			if(Dx>252 || Dx<16)
				Dx=36;
			
			if(Dy>252 || Dy<16)
				Dy=36;
			

			memset(EpBuf, 0, sizeof(EpBuf));
			UOR_IICRead(MSRZ1_1T,  EpBuf, 2);
			z1 = EpBuf[0]; 
			z1 <<=4;
			z1 |= (EpBuf[1]>>4);

			UOR_IICRead(MSRZ2_1T, EpBuf, 2);
			z2 = EpBuf[0]; 
			z2 <<=4;
			z2 |= (EpBuf[1]>>4);

			if(z1 ==0) {
				z1 =1;//avoid divde by zero
			}
			R_touch =(abs(((z2*x)/z1-x)))/4; //(float)((((float) z2)/((float) z1) -1)*(float)x)/4096;
			Rt =R_touch;

			
			if( ((256>Dx) && (256>Dy)) && ((Dx> DX_T) ||(Dy > DY_T)) && (Rt < R_Threshold2) ) {
				nTouch =  TWO_TOUCH;
			}
			else {
				nTouch = ONE_TOUCH;
			}
			if(UOR6X5X_DEBUG)
				printk(KERN_ERR "%s:after filter (x,y)=(%d,%d) (dx,dy)=(%d,%d) n_touch %d, R_touch %d, (z1,z2)=(%d,%d) !!!\n",__FUNCTION__, ts.xp , ts.yp , Dx, Dy, nTouch, R_touch, z1, z2);
		}
		else {//ting 
		    nTouch =  ZERO_TOUCH;	
			if(UOR6X5X_DEBUG)
				printk("there is no touch!\n");
		}

		if(nTouch == ONE_TOUCH || nTouch == TWO_TOUCH){	// pen down
		    if(nTouch == TWO_TOUCH){
				if(two_touch_count < FIRST_TWO_TOUCH_FILTER){
					if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:filter for first two touch -(x,y)=(%d,%d) (dx,dy)=(%d,%d),count = %d, FIRST_TWO_TOUCH_FILTER = %d  !!!\n",__FUNCTION__, x, y, Dx, Dy,two_touch_count, FIRST_TWO_TOUCH_FILTER);
					two_touch_count++;
					queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
                }
				else if(ts.xp>4000 || ts.yp>4000){
				    queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
				}                                                        	
				else if( (pre_dx!=0) && (pre_dy!=0) && (abs(Dx - pre_dx) > JITTER_THRESHOLD_DXDY) ||(abs( Dy - pre_dy) > JITTER_THRESHOLD_DXDY)){//single touch point 前後差距JITTER_THRESHOLD 則濾點 
				    if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:filter for jitter(dual) --(pre_dx,pre_dy)=(%d,%d) ,(dx,dy)=(%d,%d) , JITTER_THRESHOLD_DXDY = %d !!!\n",__FUNCTION__, pre_dx, pre_dy , Dx, Dy, JITTER_THRESHOLD_DXDY);
					pre_dx = Dx;
					pre_dy = Dy;
					queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
                }
                else{
					if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:report dual touch-- (x,y)=(%d,%d) (dx,dy)=(%d,%d)  !!!\n",__FUNCTION__, x, y, Dx, Dy);
					//report x,y,pressure,dx,dy to Linux/Android
					if(((two_touch_count<3) ||(Dx>80) || (Dy>80))&&(abs(Dx - pre_dx) <20 && abs(Dy - pre_dy )<20)){
                        Dx=pre_dx;
                        Dy=pre_dy;
                        pre_dx =((7* pre_dx)+Dx)>>3;
                	    pre_dy =((7*pre_dy)+Dy)>>3;
                	}
                	 if((pre_dx!=0) && (pre_dy!=0) && (abs(Dx - pre_dx) <8) && (abs(Dy - pre_dy )<8)){
                        Dx=pre_dx;
                        Dy=pre_dy;
                        pre_dx =((7* pre_dx)+Dx)>>3;
                	    pre_dy =((7*pre_dy)+Dy)>>3;
                	}
					
					// if ( (ts.pX!=0) && (ts.pY!=0) && ((ts.xp - ts.pX) <100 && (ts.pX-ts.xp) <100 && (ts.yp - ts.pY )<100 && (ts.pY-ts.yp )<100)){
					if ( (ts.pX!=0) && (ts.pY!=0) && (abs(ts.xp - ts.pX) <1500)  && (abs(ts.yp - ts.pY )<1500)){
					     ts.xp = ts.pX;
						 ts.yp = ts.pY;
					 }
					 
					if(UOR6X5X_DEBUG) {
						printk(KERN_ERR "%s:TWO_TOUCH (cx,cy)=(%d,%d)\n",__FUNCTION__, ts.xp, ts.yp);
						printk(KERN_ERR "%s:TWO_TOUCH (pcx,pcy)=(%d,%d)\n",__FUNCTION__,  ts.pX,  ts.pY);
						printk(KERN_ERR "%s:TWO_TOUCH (dx,dy)=(%d,%d)\n",__FUNCTION__, Dx, Dy);
						printk(KERN_ERR "%s:TWO_TOUCH (pdx,pdy)=(%d,%d)\n",__FUNCTION__, pre_dx,pre_dy);
					}
					int dx_coord =(Dx - DX_T< 0) ? 5 :  (((Dx - 18-22) & 0x00fc)/2)+5;
					int dy_coord =(Dy - DY_T< 0) ? 5 :  (((Dy - 18-22) & 0x00fc)/2)+5;
					if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:TWO_TOUCH (dx_coord,dy_coord)=(%d,%d)\n",__FUNCTION__, dx_coord, dy_coord);

                	input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 600 + (Rt%400));
                	//input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 500+press);
         
					if(slid_index == 2){
						SBufx_0 = SBufx_1;
						SBufx_1 = SBufx_2;
						SBufx_2 = dx_coord;
						dx_coord = (SBufx_0+ SBufx_1+ SBufx_2)/3;
						SBufy_0 = SBufy_1;
						SBufy_1 = SBufy_2;
						SBufy_2 = dy_coord;
						dy_coord = (SBufy_0+ SBufy_1+ SBufy_2)/3;
					}
					else if(slid_index == 1){
						SBufx_1 = SBufx_2;
						SBufx_2 = dx_coord;
						dx_coord = (SBufx_1+ SBufx_2)/2;
						SBufy_1 = SBufy_2;
						SBufy_2 = dy_coord;
						dy_coord = (SBufy_1+ SBufy_2)/2;
						slid_index ++;
					}
					else{
						SBufx_2 = dx_coord;
						SBufy_2 = dy_coord;
						slid_index ++;
					}
                		        
                	xy = 0;
					out_x = ts.xp - dx_coord;
					out_y = ts.yp - dy_coord;
					
                	if(ts.pdata->convert){
                		 xy = ts.pdata->convert(out_x, out_y);
                		 out_x = xy >> 16;
                		 out_y = xy & 0xffff;
                	}
                	
                	if((pre_outx1!=0) && (pre_outy1!=0) && (abs(out_x - pre_outx1) <2) && (abs(out_y - pre_outy1 )<2)){
                        out_x = pre_outx1;
                		out_y = pre_outy1;
                	}
 					if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:TWO_TOUCH (x1,y1)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
								
                	input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x);
                	input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y);
                	input_mt_sync(ts.dev);
                		        
                	input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 600 + (Rt%400));

					pre_outx1 = out_x;
                	pre_outy1 = out_y;
                	//input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 600+press);
                		        
                	xy = 0;
					out_x = ts.xp + dx_coord;
					out_y = ts.yp + dy_coord;
                	if(ts.pdata->convert){
                		 xy = ts.pdata->convert(out_x, out_y);
                		 out_x = xy >> 16;
                		 out_y = xy & 0xffff;
                	}
					
					if((pre_outx2!=0) && (pre_outy2!=0) && (abs(out_x - pre_outx2) <2) && (abs(out_y - pre_outy2 )<2) ){
                        out_x = pre_outx2;
                		out_y = pre_outy2;
                	}   	        
                    if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:TWO_TOUCH (x2,y2)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
					
					input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x);
                	input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y);
                	input_mt_sync(ts.dev);
                		        
                	input_sync(ts.dev);
                		        
                	TWOTouchFlag = 1;
                	OneTCountAfter2 = 0;
                	pre_dx = Dx;
                	pre_dy = Dy;
                	ts.pX = ts.xp;
					ts.pY = ts.yp;
					pre_outx2 = out_x;
                	pre_outy2 = out_y;
 
			if(two_touch_count<5)
				two_touch_count++;
					
					queue_delayed_work(queue, &work, READ_DATA_DELAY_J);
					goto READ_LOOP_OUT;
                }
            }
            else if(nTouch == ONE_TOUCH){
                if((TWOTouchFlag == 1) && (OneTCountAfter2 < ONETOUCHCountAfter2)){
                	if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:filter after two touch -- (x,y)=(%d,%d) ,OneTCountAfter2 = %d, ONETOUCHCountAfter2 = %d !!!\n",__FUNCTION__, x, y, OneTCountAfter2, ONETOUCHCountAfter2);
                	OneTCountAfter2++;
					queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
                }		
				else if((ts.xp>4000||ts.yp>4000)||((TWOTouchFlag == 0) && (FirstTC < FIRSTTOUCHCOUNT)) || (Rt > R_Threshold)){  //ting
                	if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:filter before single touch -- (x,y)=(%d,%d) ,FirstTC = %d, FIRSTTOUCHCOUNT = %d !!!\n",__FUNCTION__, x, y, FirstTC, FIRSTTOUCHCOUNT);
                	FirstTC++;
					queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
                }
                else if( (ts.pX!=0) && (ts.pY!=0) && (abs(ts.xp - ts.pX) > JITTER_THRESHOLD) || (abs(ts.yp - ts.pY) > JITTER_THRESHOLD)){
                	if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:filter for jitter -- (px,py)=(%d,%d) ,(x,y)=(%d,%d) , JITTER_THRESHOLD = %d !!!\n",__FUNCTION__, ts.pX, ts.pY ,x, y, JITTER_THRESHOLD);
                	ts.pX = ts.xp; 
                	ts.pY = ts.yp;
					queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
					goto READ_LOOP_OUT;
                }
                else{
                	if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s: (Pen down)report single touch-- (x,y)=(%d,%d) !!!\n",__FUNCTION__, x, y);
                	//report x,y,pressure,size to Linux/Android
                	if((ts.pX!=0) && (ts.pY!=0) && (abs(ts.xp - ts.pX) <40) && (abs(ts.yp - ts.pY )<40)){
                	    ts.xp = ts.pX;
                	    ts.yp = ts.pY;
                	}

					if(slid_indexs == 2){
						SBufxs_0 = SBufxs_1;
						SBufxs_1 = SBufxs_2;
						SBufxs_2 = ts.xp;
						ts.xp= (SBufxs_0+ SBufxs_1)/4+ (SBufxs_2)/2;
						SBufys_0 = SBufys_1;
						SBufys_1 = SBufys_2;
						SBufys_2 = ts.yp;
						 ts.yp = (SBufys_0+ SBufys_1)/4+( SBufys_2)/2;
					}
					else if(slid_index == 1){
						SBufxs_1 = SBufxs_2;
						SBufxs_2 = ts.xp;
						ts.xp= (SBufxs_1+ SBufxs_2)/2;
						SBufys_1 = SBufys_2;
						SBufys_2 = ts.yp;
						ts.yp= (SBufys_1+ SBufys_2)/2;
						slid_indexs ++;
					}
					else{
						SBufxs_2 = ts.xp;
						SBufys_2 = ts.yp;
						slid_indexs ++;
					}
                	input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 600 + (Rt%400) );
                	//input_report_abs(ts.dev, ABS_MT_WIDTH_MAJOR, 300);
                	                
                	xy = 0;
                	out_x = ts.xp;
                	out_y = ts.yp;
                	if(ts.pdata->convert){
                		 xy = ts.pdata->convert(out_x, out_y);
                		 out_x = xy >> 16;
                		 out_y = xy & 0xffff;
                	}
                	if(UOR6X5X_DEBUG)
						printk(KERN_ERR "%s:ONE_TOUCH (x1,y1)=(%d,%d)\n",__FUNCTION__, out_x, out_y);
                		        
                	input_report_abs(ts.dev, ABS_MT_POSITION_X, out_x);
                	input_report_abs(ts.dev, ABS_MT_POSITION_Y, out_y);
                	input_mt_sync(ts.dev);
                	input_sync(ts.dev);
                	                
                	//save previous single touch point
                	ts.pX = ts.xp; 
                	ts.pY = ts.yp;
					queue_delayed_work(queue, &work, READ_DATA_DELAY_J);
					goto READ_LOOP_OUT;
                }
            }

	    }
	    else if(nTouch == ZERO_TOUCH){	// pen release
		    udelay(250);
	        if(!uor_get_pendown_state()){
			    queue_delayed_work(queue, &work, READ_DATA_DELAY_J);
			    goto READ_LOOP_OUT;
	        }
	        if(UOR6X5X_DEBUG)
				printk(KERN_ERR "%s: (Pen release)!!!\n",__FUNCTION__);
	        input_report_abs(ts.dev, ABS_MT_TOUCH_MAJOR, 0);
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
	        pre_dx = 0;
	        pre_dy = 0;
			pre_outx1 = 0;
			pre_outy1 = 0;
			pre_outx2 = 0;
			pre_outy2 = 0;
			slid_index = 0;
			slid_indexs = 0;
			
			Init_UOR_HW();
	                
			enable_irq(ts.client->irq);
			break;
	    }
	    else{
			if(UOR6X5X_DEBUG)
				printk(KERN_ERR "uor_read_loop(): n_touch state error !!!\n");
	    }
	}		
READ_LOOP_OUT:	                 
    return;
}

static irqreturn_t uor_isr(int irq,void *dev_id)
{
	struct i2c_client *client = (struct i2c_client *)dev_id;
	
 	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor.c: uor_isr\n");
	udelay(250);
	if(uor_get_pendown_state()){//ting debounce
	    return IRQ_HANDLED;
	}
        
	disable_irq_nosync(client->irq);
	//queue_work(queue, &work);
	queue_delayed_work(queue, &work, DROP_POINT_DELAY_J);
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor_isr ok!\n");
	return IRQ_HANDLED;
}

static int uor_register_input(void)
{
    int ret;
    struct input_dev *	input_device;
    
    input_device = input_allocate_device();
    if (!input_device) {
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "Unable to allocate the input device !!\n");
    	return -ENOMEM;
    }
    
    input_device->name = "UOR-touch";
    
    ts.dev = input_device;
    set_bit(EV_SYN, ts.dev->evbit);
    set_bit(EV_ABS, ts.dev->evbit);
    
    input_set_abs_params(ts.dev, ABS_MT_TOUCH_MAJOR, 0, 1000, 0, 0);
    //input_set_abs_params(codec_ts_input, ABS_MT_WIDTH_MAJOR, 0, 1000, 0, 0);
    
    int max_x = ts.pdata->abs_xmax ? ts.pdata->abs_xmax : 4096;
    input_set_abs_params(ts.dev, ABS_MT_POSITION_X, 0, max_x, 0, 0);
    
    int max_y = ts.pdata->abs_ymax ? ts.pdata->abs_ymax : 4096;
    input_set_abs_params(ts.dev, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
    
    ret = input_register_device(ts.dev);
    if (ret) {
    	input_free_device(ts.dev);
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "%s: unabled to register input device, ret = %d\n", __FUNCTION__, ret);
    	return ret;
    }
    return 0;
}

static int __init uor_init(void)
{
	int ret = 0;
	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor.c: uor_init() !\n");

	memset(&ts, 0, sizeof(struct uor6x5x_touch_screen_struct));//init data struct ts

	ret = i2c_add_driver(&uor_i2c_driver);
	if(ret < 0) {
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "uor.c: i2c_add_driver() fail in uor_init()!\n");
		return ret;
	}	

	ret = Init_UOR_HW();
	if(ret < 0) {
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "uor.c: Init_UOR_HW() fail in uor_init()!\n");
		return ret;
	}

	return ret;
}

static int __devinit uor_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
    int err = 0;
    struct uor6x5x_platform_data *pdata = pdata = client->dev.platform_data;

	if(UOR6X5X_DEBUG)
		printk(KERN_ERR "uor.c: uor_probe() !\n");
	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	        return -EIO;
	        
	queue = create_singlethread_workqueue("uor-touch-screen-read-loop");
	//INIT_WORK(&work, uor_read_loop);
	INIT_DELAYED_WORK(&work, uor_read_loop);
	
	if (pdata->init_irq)
		pdata->init_irq();
	ts.client = client; // save the client we get	
	ts.pdata = pdata;

	if (uor_register_input() < 0) {
		dev_err(&client->dev, "register input fail!\n");
		return -ENOMEM;
	}

	err = request_irq(client->irq, uor_isr, IRQF_TRIGGER_FALLING,client->dev.driver->name, client);
	if(err < 0){
		input_free_device(ts.dev);
		if(UOR6X5X_DEBUG)
			printk(KERN_ERR "uor.c: Could not allocate GPIO intrrupt for touch screen !!!\n");
		free_irq(client->irq, client);
		err = -ENOMEM;
		return err;	
	}
	#ifdef CONFIG_HAS_EARLYSUSPEND
    uor6x5x_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    uor6x5x_early_suspend.suspend = uor_early_suspend;
    uor6x5x_early_suspend.resume = uor_late_resume;
    uor6x5x_early_suspend.param = client;
	register_early_suspend(&uor6x5x_early_suspend);
	#endif
	if(UOR6X5X_DEBUG)
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
