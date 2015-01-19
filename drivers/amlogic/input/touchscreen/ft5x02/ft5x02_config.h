#ifndef __FT5X02_CONFIG_H__
#define __FT5X02_CONFIG_H__
/*ft5x02 config*/

#define FT5X02_KX				142
#define FT5X02_KY				160
#define FT5X02_LEMDA_X			0
#define FT5X02_LEMDA_Y			0
#define FT5X02_RESOLUTION_X	320
#define FT5X02_RESOLUTION_Y	480
#define FT5X02_POS_X			0	/*0-tx is X direct. 1-rx is X direct.*/

/**/
#define FT5X02_STATISTICS_TX_NUM		3
#define FT5X02_FACE_DETECT_PRE_VALUE	20
#define FT5X02_FACE_DETECT_NUM		10
#define FT5X02_FACE_DETECT_LAST_TIME	1000
#define FT5X02_FACE_DETECT_ON			0xc0
#define FT5X02_FACE_DETECT_OFF		0xe0
#define FT5X02_PEAK_VALUE_MIN			150/*The min value to be decided as the big point*/
#define FT5X02_DIFF_VALUE_OVER_NUM	30/*The min big points of the big area*/
#define FT5X02_DIFF_VALUE_PERCENT		7/*reserve for future use*/
#define FT5X02_POINT_AUTO_CLEAR_TIME	3000/*3000ms*/
#define FT5X02_FACE_DETECT_LAST_TIME_HIGH	0x03
#define FT5X02_FACE_DETECT_LAST_TIME_LOW		0xe8
#define FT5X02_MODE					0x01
#define FT5X02_PMODE					0x00
#define FT5X02_FIRMID					0x05
#define FT5X02_STATE					0x01
#define FT5X02_FT5201ID					0x79
#define FT5X02_PERIODACTIVE			0x06

#define FT5X02_THGROUP					120
#define FT5X02_THPEAK					60
#define FT5X02_FACE_DEC_MODE				0x00/*close*/
#define FT5X02_MAX_TOUCH_VALUE_HIGH		0x04
#define FT5X02_MAX_TOUCH_VALUE_LOW		0xb0


#define FT5X02_THCAL 						16	
#define FT5X02_THWATER 					(-60)
#define FT5X02_THFALSE_TOUCH_PEAK 		255
#define FT5X02_THDIFF						160
#define FT5X02_CTRL							1
#define FT5X02_TIMEENTERMONITOR 			10
#define FT5X02_PERIODMONITOR 				16
#define FT5X02_AUTO_CLB_MODE				0xFF
#define FT5X02_DRAW_LINE_TH				250
#define FT5X02_DIFFDATA_HADDLE_VALUE		100

/**/

unsigned char g_ft5x02_tx_num = 12;
unsigned char g_ft5x02_rx_num = 9;
unsigned char g_ft5x02_gain = 0x0a;
unsigned char g_ft5x02_voltage = 0x00;
unsigned char g_ft5x02_scanselect = 2;/*1-3M	2-4.5M 3-6.75M*/

unsigned char g_ft5x02_tx_order[] = {11,10,9,8,7,6,5,4,3,2,1,0};
unsigned char g_ft5x02_tx_offset = 0x01;
unsigned char g_ft5x02_tx_cap[] = {40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40};

unsigned char g_ft5x02_rx_order[] = {0,1,2,3,4,5,6,7,8};
unsigned char g_ft5x02_rx_offset[] = {0x77,0x77,0x77,0x77,0x77,0x77};
unsigned char g_ft5x02_rx_cap[] = {80,80,80,80,80,80,80,80,80,80,80,80};


#endif
