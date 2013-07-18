#include<linux/rk_screen.h>


#define CRU_LVDS_CON0   	0x0150
#define LVDS_SWING_SEL 		(1<<12)
#define LVDS_CBS_COL_SEL(x)	((x&3)<<10)
#define LVDS_OUTPUT_EN	    	(1<<9)
#define LVDS_PLL_PWR_EN	    	(1<<8)
#define LVDS_CBG_PWR_EN	    	(1<<7)
#define LVDS_CH_LOAD   		(1<<4)
#define LVDS_INPUT_FORMAT	(1<<3)
#define LVDS_OUTPUT_FORMAT(x)	(((x)&3)<<1)
#define LVDS_DATA_SEL		(1<<0)

enum{
	OUT_DISABLE=0,
	OUT_ENABLE,
};

