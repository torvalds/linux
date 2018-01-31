/* SPDX-License-Identifier: GPL-2.0 */
#include<linux/rk_screen.h>


#define CRU_LVDS_CON0   	0x0150
#define LVDS_SWING_SEL(x) 	(((x)&1)<<12)//0:250mv-450mv;1:150mv-250mv
#define LVDS_CBS_COL_SEL(x)	(((x)&3)<<10)// 1:18-bit lvds 2:24-bit lvds;  3:all lvds power down
#define LVDS_OUTPUT_EN(x)	(((x)&1)<<9) //0:lvds; 1:lvttl
#define LVDS_PLL_PWD_EN(x)	(((x)&1)<<8) //0:enable; 1:disable
#define LVDS_CBG_PWD_EN(x)	(((x)&1)<<7) //0:disable; 1:enable
#define LVDS_OUTPUT_LOAD_SEL(X) (((X)&3)<<4) //0:3pf; 1:6pf;  2:10pf;  3:15pf
#define LVDS_INPUT_FORMAT(x)	(((x)&1)<<3) //0:MSB is on D7; 1:MSB is on D0;
#define LVDS_OUTPUT_FORMAT(x)	(((x)&3)<<1) //0:LVDS_8BIT_1;  1:LVDS_8BIT_2;  2:LVDS_8BIT_3;   3:LVDS_6BIT
#define LVDS_DATA_SEL(x)	(((x)&1)<<0) //0:from lcdc; 1:from ebc;


#define	m_SWING_SEL 		(1<<12)
#define m_CBS_COL_SEL		(3<<10)
#define m_OUTPUT_EN		(1<<9)
#define m_PLL_PWD_EN		(1<<8)
#define m_CBG_PWD_EN 		(1<<7)
#define m_OUTPUT_LOAD_SEL 	(3<<4) 
#define m_INPUT_FORMAT 		(1<<3)
#define m_OUTPUT_FORMAT  	(3<<1)
#define m_DATA_SEL 		(1<<0)


enum{
	OUT_DISABLE=0,
	OUT_ENABLE,
};

