
#ifndef __RKXX_REMOTECTL_H__
#define __RKXX_REMOTECTL_H__
#include <linux/input.h>

/********************************************************************
**                            宏定义                                *
********************************************************************/
#define TIME_BIT0_MIN  625  /*Bit0  1.125ms*/
#define TIME_BIT0_MAX  1625

#define TIME_BIT1_MIN  1700  /*Bit1  2.25ms*/
#define TIME_BIT1_MAX  3000

#define TIME_PRE_MIN   13000   /*4500*/
#define TIME_PRE_MAX   14000   /*5500*/           /*PreLoad 4.5+0.56 = 5.06ms*/

#define TIME_RPT_MIN   95000   /*101000*/
#define TIME_RPT_MAX   98000   /*103000*/         /*Repeat  105-2.81=102.19ms*/  //110-9-2.25-0.56=98.19ms

#define TIME_SEQ1_MIN   10000   /*2650*/
#define TIME_SEQ1_MAX   12000   /*3000*/           /*sequence  2.25+0.56=2.81ms*/ //11.25ms

#define TIME_SEQ2_MIN   40000   /*101000*/
#define TIME_SEQ2_MAX   47000   /*103000*/         /*Repeat  105-2.81=102.19ms*/  //110-9-2.25-0.56=98.19ms

/********************************************************************
**                          结构定义                                *
********************************************************************/
typedef enum _RMC_STATE
{
    RMC_IDLE,
    RMC_PRELOAD,
    RMC_USERCODE,
    RMC_GETDATA,
    RMC_SEQUENCE
}eRMC_STATE;


struct RKxx_remotectl_platform_data {
	//struct rkxx_remotectl_button *buttons;
	int nbuttons;
	int rep;
	int gpio;
	int active_low;
	int timer;
	int wakeup;
	void (*set_iomux)(void);
};

#endif

