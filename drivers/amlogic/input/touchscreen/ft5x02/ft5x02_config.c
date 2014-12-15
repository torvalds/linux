#include "ft5x02_config.h"
#include "ft5x02_ts.h"

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>

#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>


/*Init param register address*/
/*factory mode register*/
#define FT5x02_REG_TX_NUM				0x03
#define FT5x02_REG_RX_NUM				0x04
#define FT5x02_REG_VOLTAGE				0x05
#define FT5x02_REG_RX_START			0x06
#define FT5x02_REG_GAIN					0x07
#define FT5X02_REG_SCAN_SELECT		0xe8
#define FT5x02_REG_TX_ORDER_START		0x50
#define FT5x02_REG_TX_CAP_START		0x78
#define FT5x02_REG_TX_OFFSET_START	0xBF
#define FT5x02_REG_RX_ORDER_START		0xea
#define FT5x02_REG_RX_CAP_START		0xA0
#define FT5x02_REG_RX_OFFSET_START	0xD3

#define FT5x02_REG_DEVICE_MODE		0x00

/*work mode register*/
#define FT5X02_REG_THGROUP						(0x00+0x80)
#define FT5X02_REG_THPEAK 						(0x01+0x80)
#define FT5X02_REG_THCAL 						(0x02+0x80)
#define FT5X02_REG_THWATER 					(0x03+0x80)
#define FT5X02_REG_THFALSE_TOUCH_PEAK 		(0x04+0x80)
#define FT5X02_REG_THDIFF						(0x05+0x80)
#define FT5X02_REG_CTRL							(0x06+0x80)
#define FT5X02_REG_TIMEENTERMONITOR 			(0x07+0x80)
#define FT5X02_REG_PERIODACTIVE				(0x08+0x80)
#define FT5X02_REG_PERIODMONITOR 				(0x09+0x80)
#define FT5x02_REG_RESOLUTION_X_H				(0x18+0x80)
#define FT5x02_REG_RESOLUTION_X_L				(0x19+0x80)
#define FT5x02_REG_RESOLUTION_Y_H				(0x1a+0x80)
#define FT5x02_REG_RESOLUTION_Y_L				(0x1b+0x80)
#define FT5X02_REG_KX_H						(0x1c+0x80)
#define FT5X02_REG_KX_L							(0x1d+0x80)
#define FT5X02_REG_KY_H						0x9e
#define FT5X02_REG_KY_L							(0x1f+0x80)
#define FT5X02_REG_AUTO_CLB_MODE				(0x20+0x80)
#define FT5X02_REG_LIB_VERSION_H				(0x21+0x80)
#define FT5X02_REG_LIB_VERSION_L				(0x22+0x80)
#define FT5X02_REG_CIPHER						(0x23+0x80)
#define FT5X02_REG_MODE						(0x24+0x80)
#define FT5X02_REG_PMODE						(0x25+0x80)
#define FT5X02_REG_FIRMID						(0x26+0x80)
#define FT5X02_REG_STATE						(0x27+0x80)
#define FT5X02_REG_STATIC_TH					(0x2b+0x80)
#define FT5X02_REG_DRAW_LINE_TH				0xae
#define FT5X02_REG_FACE_DEC_MODE				(0x33+0x80)
#define FT5X02_REG_MAX_TOUCH_VALUE_HIGH		(0x34+0x80)
#define FT5X02_REG_MAX_TOUCH_VALUE_LOW		(0x35+0x80)

#define FT5X02_REG_POS_X						(0x40+0x80)
#define FT5X02_REG_LEMDA_X						(0x41+0x80)
#define FT5X02_REG_LEMDA_Y						(0x42+0x80)
#define FT5X02_REG_STATISTICS_TX_NUM			(0x43+0x80)
#define FT5X02_REG_FACE_DETECT_PRE_VALUE		(0x44+0x80)
#define FT5X02_REG_FACE_DETECT_NUM			(0x45+0x80)
#define FT5X02_REG_FACE_DETECT_LAST_TIME_H	(0x46+0x80)
#define FT5X02_REG_FACE_DETECT_LAST_TIME_L	(0x47+0x80)
#define FT5X02_REG_FACE_DETECT_ON				(0x48+0x80)
#define FT5X02_REG_FACE_DETECT_OFF			(0x49+0x80)
#define FT5X02_REG_PEAK_VALUE_MIN				(0x4a+0x80)
#define FT5X02_REG_DIFF_VALUE_OVER_NUM		(0x4b+0x80)
//#define FT5X02_REG_DIFF_VALUE_PERCENT		
#define FT5X02_REG_POINT_AUTO_CLEAR_TIME_H	(0x4c+0x80)
#define FT5X02_REG_POINT_AUTO_CLEAR_TIME_L	(0x4d+0x80)
#define FT5X02_REG_DIFFDATA_HADDLE_VALUE       0xce//(0x4e+0x80)


/**/
#define FT5x02_REG_TEST_MODE			0x04
#define FT5x02_REG_TEST_MODE_2		0x05
#define FT5x02_TX_TEST_MODE_1			0x28
#define FT5x02_RX_TEST_MODE_1			0x1E
#define FT5x02_FACTORYMODE_VALUE		0x40
#define FT5x02_WORKMODE_VALUE		0x00

/*set tx order
*@txNO:		offset from tx order start
*@txNO1:	tx NO.
*/
static int ft5x02_set_tx_order(struct i2c_client * client, u8 txNO, u8 txNO1)
{
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1)
		ReCode = ft5x02_write_reg(client, FT5x02_REG_TX_ORDER_START + txNO,
						txNO1);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0)
			ReCode = ft5x02_write_reg(client,
					FT5x02_REG_TX_ORDER_START + txNO - FT5x02_TX_TEST_MODE_1,
					txNO1);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	return ReCode;
}

/*set tx order
*@txNO:		offset from tx order start
*@pTxNo:	return value of tx NO.
*/
static int ft5x02_get_tx_order(struct i2c_client * client, u8 txNO, u8 *pTxNo)
{
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1)
		ReCode = ft5x02_read_reg(client, FT5x02_REG_TX_ORDER_START + txNO,
						pTxNo);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if(ReCode >= 0)
			ReCode =  ft5x02_read_reg(client,
					FT5x02_REG_TX_ORDER_START + txNO - FT5x02_TX_TEST_MODE_1,
					pTxNo);	
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	return ReCode;
}

/*set tx cap
*@txNO: 	tx NO.
*@cap_value:	value of cap
*/
static int ft5x02_set_tx_cap(struct i2c_client * client, u8 txNO, u8 cap_value)
{
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1)
		ReCode = ft5x02_write_reg(client, FT5x02_REG_TX_CAP_START + txNO,
						cap_value);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0)
			ReCode = ft5x02_write_reg(client,
					FT5x02_REG_TX_CAP_START + txNO - FT5x02_TX_TEST_MODE_1,
					cap_value);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	return ReCode;
}

/*get tx cap*/
static int ft5x02_get_tx_cap(struct i2c_client * client, u8 txNO, u8 *pCap)
{
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1)
		ReCode =  ft5x02_read_reg(client, FT5x02_REG_TX_CAP_START + txNO,
					pCap);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0)
			ReCode = ft5x02_read_reg(client,
					FT5x02_REG_TX_CAP_START + txNO - FT5x02_TX_TEST_MODE_1,
					pCap);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	return ReCode;
}

/*set tx offset*/
static int ft5x02_set_tx_offset(struct i2c_client * client, u8 txNO, u8 offset_value)
{
	unsigned char temp=0;
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1) {
		ReCode = ft5x02_read_reg(client,
				FT5x02_REG_TX_OFFSET_START + (txNO>>1), &temp);
		if (ReCode >= 0) {
			if (txNO%2 == 0)
				ReCode = ft5x02_write_reg(client,
							FT5x02_REG_TX_OFFSET_START + (txNO>>1),
							(temp&0xf0) + (offset_value&0x0f));	
			else
				ReCode = ft5x02_write_reg(client,
							FT5x02_REG_TX_OFFSET_START + (txNO>>1),
							(temp&0x0f) + (offset_value<<4));	
		}
	} else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0) {
			ReCode = ft5x02_read_reg(client,
				FT5x02_REG_DEVICE_MODE+((txNO-FT5x02_TX_TEST_MODE_1)>>1),
				&temp);	/*enter Test mode 2*/
			if (ReCode >= 0) {
				if(txNO%2 == 0)
					ReCode = ft5x02_write_reg(client,
						FT5x02_REG_TX_OFFSET_START+((txNO-FT5x02_TX_TEST_MODE_1)>>1),
						(temp&0xf0)+(offset_value&0x0f));	
				else
					ReCode = ft5x02_write_reg(client,
						FT5x02_REG_TX_OFFSET_START+((txNO-FT5x02_TX_TEST_MODE_1)>>1),
						(temp&0xf0)+(offset_value<<4));	
			}
		}
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	
	return ReCode;
}

/*get tx offset*/
static int ft5x02_get_tx_offset(struct i2c_client * client, u8 txNO, u8 *pOffset)
{
	unsigned char temp=0;
	unsigned char ReCode = 0;
	if (txNO < FT5x02_TX_TEST_MODE_1)
		ReCode = ft5x02_read_reg(client,
				FT5x02_REG_TX_OFFSET_START + (txNO>>1), &temp);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0)
			ReCode = ft5x02_read_reg(client,
						FT5x02_REG_TX_OFFSET_START+((txNO-FT5x02_TX_TEST_MODE_1)>>1),
						&temp);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}

	if (ReCode >= 0)
		(txNO%2 == 0) ? (*pOffset = (temp&0x0f)) : (*pOffset = (temp>>4));
	return ReCode;
}

/*set rx order*/
static int ft5x02_set_rx_order(struct i2c_client * client, u8 rxNO, u8 rxNO1)
{
	unsigned char ReCode = 0;
	ReCode = ft5x02_write_reg(client, FT5x02_REG_RX_ORDER_START + rxNO,
						rxNO1);
	return ReCode;
}

/*get rx order*/
static int ft5x02_get_rx_order(struct i2c_client * client, u8 rxNO, u8 *prxNO1)
{
	unsigned char ReCode = 0;
	ReCode = ft5x02_read_reg(client, FT5x02_REG_RX_ORDER_START + rxNO,
						prxNO1);
	return ReCode;
}

/*set rx cap*/
static int ft5x02_set_rx_cap(struct i2c_client * client, u8 rxNO, u8 cap_value)
{
	unsigned char ReCode = 0;
	if (rxNO < FT5x02_RX_TEST_MODE_1)
		ReCode = ft5x02_write_reg(client, FT5x02_REG_RX_CAP_START + rxNO,
						cap_value);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if(ReCode >= 0)
			ReCode = ft5x02_write_reg(client,
					FT5x02_REG_RX_CAP_START + rxNO - FT5x02_RX_TEST_MODE_1,
					cap_value);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	
	return ReCode;
}

/*get rx cap*/
static int ft5x02_get_rx_cap(struct i2c_client * client, u8 rxNO, u8 *pCap)
{
	unsigned char ReCode = 0;
	if (rxNO < FT5x02_RX_TEST_MODE_1)
		ReCode = ft5x02_read_reg(client, FT5x02_REG_RX_CAP_START + rxNO,
						pCap);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if(ReCode >= 0)
			ReCode = ft5x02_read_reg(client,
					FT5x02_REG_RX_CAP_START + rxNO - FT5x02_RX_TEST_MODE_1,
					pCap);
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	
	return ReCode;
}

/*set rx offset*/
static int ft5x02_set_rx_offset(struct i2c_client * client, u8 rxNO, u8 offset_value)
{
	unsigned char temp=0;
	unsigned char ReCode = 0;
	if (rxNO < FT5x02_RX_TEST_MODE_1) {
		ReCode = ft5x02_read_reg(client,
				FT5x02_REG_RX_OFFSET_START + (rxNO>>1), &temp);
		if (ReCode >= 0) {
			if (rxNO%2 == 0)
				ReCode = ft5x02_write_reg(client,
							FT5x02_REG_RX_OFFSET_START + (rxNO>>1),
							(temp&0xf0) + (offset_value&0x0f));	
			else
				ReCode = ft5x02_write_reg(client,
							FT5x02_REG_RX_OFFSET_START + (rxNO>>1),
							(temp&0x0f) + (offset_value<<4));	
		}
	}
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0) {
			ReCode = ft5x02_read_reg(client,
				FT5x02_REG_DEVICE_MODE+((rxNO-FT5x02_RX_TEST_MODE_1)>>1),
				&temp);	/*enter Test mode 2*/
			if (ReCode >= 0) {
				if (rxNO%2 == 0)
					ReCode = ft5x02_write_reg(client,
						FT5x02_REG_RX_OFFSET_START+((rxNO-FT5x02_RX_TEST_MODE_1)>>1),
						(temp&0xf0)+(offset_value&0x0f));	
				else
					ReCode = ft5x02_write_reg(client,
						FT5x02_REG_RX_OFFSET_START+((rxNO-FT5x02_RX_TEST_MODE_1)>>1),
						(temp&0xf0)+(offset_value<<4));	
			}
		}
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}
	
	return ReCode;
}

/*get rx offset*/
static int ft5x02_get_rx_offset(struct i2c_client * client, u8 rxNO, u8 *pOffset)
{
	unsigned char temp = 0;
	unsigned char ReCode = 0;
	if (rxNO < FT5x02_RX_TEST_MODE_1)
		ReCode = ft5x02_read_reg(client,
				FT5x02_REG_RX_OFFSET_START + (rxNO>>1), &temp);
	else {
		ReCode = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
						FT5x02_REG_TEST_MODE_2<<4);	/*enter Test mode 2*/
		if (ReCode >= 0)
			ReCode = ft5x02_read_reg(client,
						FT5x02_REG_RX_OFFSET_START+((rxNO-FT5x02_RX_TEST_MODE_1)>>1),
						&temp);
		
		ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE,
			FT5x02_REG_TEST_MODE<<4);	/*enter Test mode*/
	}

	if (ReCode >= 0) {
		if (0 == (rxNO%2))
			*pOffset = (temp&0x0f);
		else
			*pOffset = (temp>>4);
	}
	
	return ReCode;
}

/*set tx num*/
static int ft5x02_set_tx_num(struct i2c_client *client, u8 txnum)
{
	return ft5x02_write_reg(client, FT5x02_REG_TX_NUM, txnum);
}

/*get tx num*/
static int ft5x02_get_tx_num(struct i2c_client *client, u8 *ptxnum)
{
	return ft5x02_read_reg(client, FT5x02_REG_TX_NUM, ptxnum);
}

/*set rx num*/
static int ft5x02_set_rx_num(struct i2c_client *client, u8 rxnum)
{
	return ft5x02_write_reg(client, FT5x02_REG_RX_NUM, rxnum);
}

/*get rx num*/
static int ft5x02_get_rx_num(struct i2c_client *client, u8 *prxnum)
{
	return ft5x02_read_reg(client, FT5x02_REG_RX_NUM, prxnum);
}

/*set resolution*/
static int ft5x02_set_Resolution(struct i2c_client *client, u16 x, u16 y)
{
	unsigned char cRet = 0;
	cRet &= ft5x02_write_reg(client,
			FT5x02_REG_RESOLUTION_X_H, ((unsigned char)(x>>8)));
	cRet &= ft5x02_write_reg(client,
			FT5x02_REG_RESOLUTION_X_L, ((unsigned char)(x&0x00ff)));

	cRet &= ft5x02_write_reg(client,
			FT5x02_REG_RESOLUTION_Y_H, ((unsigned char)(y>>8)));
	cRet &= ft5x02_write_reg(client,
			FT5x02_REG_RESOLUTION_Y_L, ((unsigned char)(y&0x00ff)));

	return cRet;
}

/*get resolution*/
static int ft5x02_get_Resolution(struct i2c_client *client,
			u16 *px, u16 *py)
{
	unsigned char cRet = 0, temp1 = 0, temp2 = 0;
	cRet &= ft5x02_read_reg(client,
			FT5x02_REG_RESOLUTION_X_H, &temp1);
	cRet &= ft5x02_read_reg(client,
			FT5x02_REG_RESOLUTION_X_L, &temp2);
	(*px) = (((u16)temp1) << 8) | ((u16)temp2);

	cRet &= ft5x02_read_reg(client,
			FT5x02_REG_RESOLUTION_Y_H, &temp1);
	cRet &= ft5x02_read_reg(client,
			FT5x02_REG_RESOLUTION_Y_L, &temp2);
	(*py) = (((u16)temp1) << 8) | ((u16)temp2);

	return cRet;
}


/*set voltage*/
static int ft5x02_set_vol(struct i2c_client *client, u8 Vol)
{
	return  ft5x02_write_reg(client, FT5x02_REG_VOLTAGE, Vol);
}

/*get voltage*/
static int ft5x02_get_vol(struct i2c_client *client, u8 *pVol)
{
	return ft5x02_read_reg(client, FT5x02_REG_VOLTAGE, pVol);
}

/*set gain*/
static int ft5x02_set_gain(struct i2c_client *client, u8 Gain)
{
	return ft5x02_write_reg(client, FT5x02_REG_GAIN, Gain);
}

/*get gain*/
static int ft5x02_get_gain(struct i2c_client *client, u8 *pGain)
{
	return ft5x02_read_reg(client, FT5x02_REG_GAIN, pGain);
}
#if 0
static int ft5x02_set_statistics_tx_num(struct i2c_client *client, u8 txnum)
{
	return ft5x02_write_reg(client, FT5X02_REG_STATISTICS_TX_NUM,
			txnum);
}

static int ft5x02_get_statistics_tx_num(struct i2c_client *client, u8 *ptxnum)
{
	return ft5x02_read_reg(client, FT5X02_REG_STATISTICS_TX_NUM,
			ptxnum);
}
#endif
static int ft5x02_set_face_detect_pre_value(struct i2c_client *client, u8 prevalue)
{
	return ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_PRE_VALUE,
			prevalue);
}

static int ft5x02_get_face_detect_pre_value(struct i2c_client *client, u8 *pprevalue)
{
	return ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_PRE_VALUE,
			pprevalue);
}

static int ft5x02_set_face_detect_num(struct i2c_client *client, u8 num)
{
	return ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_NUM,
			num);
}

static int ft5x02_get_face_detect_num(struct i2c_client *client, u8 *pnum)
{
	return ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_NUM,
			pnum);
}

static int ft5x02_set_face_detect_last_time(struct i2c_client *client, u16 lasttime)
{
	int err = 0;
	u8 temp1 = 0, temp2 = 0;

	temp1 = lasttime >> 8;
	temp2 = lasttime;
	err = ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_LAST_TIME_H,
			temp1);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write face detect last time high.\n",
			__func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_LAST_TIME_L,
			temp2);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write face detect last time low.\n",
			__func__);
		return err;
	}
	return err;
}
static int ft5x02_get_face_detect_last_time(struct i2c_client *client, u16 *plasttime)
{
	int err = 0;
	u8 temp1 = 0, temp2 = 0;
	err = ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_LAST_TIME_H,
			&temp1);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not read face detect last time high.\n",
			__func__);
		return err;
	}
	err = ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_LAST_TIME_L,
			&temp2);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not read face detect last time low.\n",
			__func__);
		return err;
	}
	*plasttime = ((u16)temp1<<8) + (u16)temp2;

	return err;
}

static int ft5x02_set_face_detect_on(struct i2c_client *client, u8 on)
{
	return ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_ON,
			on);
}

static int ft5x02_get_face_detect_on(struct i2c_client *client, u8 *pon)
{
	return ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_ON,
			pon);
}

static int ft5x02_set_face_detect_off(struct i2c_client *client, u8 off)
{
	return ft5x02_write_reg(client, FT5X02_REG_FACE_DETECT_OFF,
			off);
}
static int ft5x02_get_face_detect_off(struct i2c_client *client, u8 *poff)
{
	return ft5x02_read_reg(client, FT5X02_REG_FACE_DETECT_OFF,
			poff);
}

static int ft5x02_set_peak_value_min(struct i2c_client *client, u8 min)
{
	return ft5x02_write_reg(client, FT5X02_REG_PEAK_VALUE_MIN,
			min);
}

static int ft5x02_get_peak_value_min(struct i2c_client *client, u8 *pmin)
{
	return ft5x02_read_reg(client, FT5X02_REG_PEAK_VALUE_MIN,
			pmin);
}

static int ft5x02_set_diff_value_over_num(struct i2c_client *client, u8 num)
{
	return ft5x02_write_reg(client, FT5X02_REG_DIFF_VALUE_OVER_NUM,
			num);
}
static int ft5x02_get_diff_value_over_num(struct i2c_client *client, u8 *pnum)
{
	return ft5x02_read_reg(client, FT5X02_REG_DIFF_VALUE_OVER_NUM,
			pnum);
}

static int ft5x02_set_diff_value_percent(struct i2c_client *client, u8 value)
{
	
//	return ft5x02_write_reg(client, FT5X02_REG_DIFF_VALUE_PERCENT,
//			value);
return 0;
}
static int ft5x02_get_diff_value_percent(struct i2c_client *client, u8 *pvalue)
{
//	return ft5x02_read_reg(client, FT5X02_REG_DIFF_VALUE_PERCENT,
//			pvalue);
return 0;
}

static int ft5x02_set_point_auto_clear_time(struct i2c_client *client, u16 value)
{
	int err = 0;
	u8 temp1 = 0, temp2 = 0;

	temp1 = value >> 8;
	temp2 = value;
	err = ft5x02_write_reg(client, FT5X02_REG_POINT_AUTO_CLEAR_TIME_H,
			temp1);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write point auot clean time high.\n",
			__func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_POINT_AUTO_CLEAR_TIME_L,
			temp2);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write point auot clean time low.\n",
			__func__);
		return err;
	}
	return err;
}
static int ft5x02_get_point_auto_clear_time(struct i2c_client *client, u16 *pvalue)
{
	int err = 0;
	u8 temp1 = 0, temp2 = 0;
	err = ft5x02_read_reg(client, FT5X02_REG_POINT_AUTO_CLEAR_TIME_H,
			&temp1);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write point auot clean time high.\n",
			__func__);
		return err;
	}
	err = ft5x02_read_reg(client, FT5X02_REG_POINT_AUTO_CLEAR_TIME_L,
			&temp2);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not write point auot clean time low.\n",
			__func__);
		return err;
	}
	*pvalue = ((u16)temp1<<8) + (u16)temp2;
	return err;

}

static int ft5x02_set_kx(struct i2c_client *client, u16 value)
{
	int err = 0;
	err = ft5x02_write_reg(client, FT5X02_REG_KX_H,
			value >> 8);
	if (err < 0)
		dev_err(&client->dev, "%s:set kx high failed\n",
				__func__);
	err = ft5x02_write_reg(client, FT5X02_REG_KX_L,
			value);
	if (err < 0)
		dev_err(&client->dev, "%s:set kx low failed\n",
				__func__);

	return err;
}

static int ft5x02_get_kx(struct i2c_client *client, u16 *pvalue)
{
	int err = 0;
	u8 tmp1, tmp2;
	err = ft5x02_read_reg(client, FT5X02_REG_KX_H,
			&tmp1);
	if (err < 0)
		dev_err(&client->dev, "%s:get kx high failed\n",
				__func__);
	err = ft5x02_read_reg(client, FT5X02_REG_KX_L,
			&tmp2);
	if (err < 0)
		dev_err(&client->dev, "%s:get kx low failed\n",
				__func__);

	*pvalue = ((u16)tmp1<<8) + (u16)tmp2;
	return err;
}
static int ft5x02_set_ky(struct i2c_client *client, u16 value)
{
	int err = 0;
	err = ft5x02_write_reg(client, FT5X02_REG_KY_H,
			value >> 8);
	if (err < 0)
		dev_err(&client->dev, "%s:set ky high failed\n",
				__func__);
	err = ft5x02_write_reg(client, FT5X02_REG_KY_L,
			value);
	if (err < 0)
		dev_err(&client->dev, "%s:set ky low failed\n",
				__func__);

	return err;
}

static int ft5x02_get_ky(struct i2c_client *client, u16 *pvalue)
{
	int err = 0;
	u8 tmp1, tmp2;
	err = ft5x02_read_reg(client, FT5X02_REG_KY_H,
			&tmp1);
	if (err < 0)
		dev_err(&client->dev, "%s:get ky high failed\n",
				__func__);
	err = ft5x02_read_reg(client, FT5X02_REG_KY_L,
			&tmp2);
	if (err < 0)
		dev_err(&client->dev, "%s:get ky low failed\n",
				__func__);

	*pvalue = ((u16)tmp1<<8) + (u16)tmp2;
	return err;
}
static int ft5x02_set_lemda_x(struct i2c_client *client, u8 value)
{
	return ft5x02_write_reg(client, FT5X02_REG_LEMDA_X,
			value);
}

static int ft5x02_get_lemda_x(struct i2c_client *client, u8 *pvalue)
{
	return ft5x02_read_reg(client, FT5X02_REG_LEMDA_X,
			pvalue);
}
static int ft5x02_set_lemda_y(struct i2c_client *client, u8 value)
{
	return ft5x02_write_reg(client, FT5X02_REG_LEMDA_Y,
			value);
}

static int ft5x02_get_lemda_y(struct i2c_client *client, u8 *pvalue)
{
	return ft5x02_read_reg(client, FT5X02_REG_LEMDA_Y,
			pvalue);
}
static int ft5x02_set_pos_x(struct i2c_client *client, u8 value)
{
	return ft5x02_write_reg(client, FT5X02_REG_POS_X,
			value);
}

static int ft5x02_get_pos_x(struct i2c_client *client, u8 *pvalue)
{
	return ft5x02_read_reg(client, FT5X02_REG_POS_X,
			pvalue);
}

static int ft5x02_set_scan_select(struct i2c_client *client, u8 value)
{
	return ft5x02_write_reg(client, FT5X02_REG_SCAN_SELECT,
			value);
}

static int ft5x02_get_scan_select(struct i2c_client *client, u8 *pvalue)
{
	return ft5x02_read_reg(client, FT5X02_REG_SCAN_SELECT,
			pvalue);
}

static int ft5x02_set_other_param(struct i2c_client *client)
{
	int err = 0;
	err = ft5x02_write_reg(client, FT5X02_REG_THGROUP, (u8)(FT5X02_THGROUP>>2));
	if (err < 0) {
		dev_err(&client->dev, "%s:write THGROUP failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_THPEAK, FT5X02_THPEAK);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THPEAK failed.\n",
				__func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_THCAL, FT5X02_THCAL);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THCAL failed.\n",
				__func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_THWATER, FT5X02_THWATER);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THWATER failed.\n",
				__func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_THFALSE_TOUCH_PEAK,
			FT5X02_THFALSE_TOUCH_PEAK);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THFALSE_TOUCH_PEAK failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_THDIFF, FT5X02_THDIFF);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THDIFF failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_CTRL, FT5X02_CTRL);
	if (err < 0) {
		dev_err(&client->dev, "%s:write CTRL failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_TIMEENTERMONITOR,
			FT5X02_TIMEENTERMONITOR);
	if (err < 0) {
		dev_err(&client->dev, "%s:write TIMEENTERMONITOR failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_PERIODACTIVE,
			FT5X02_PERIODACTIVE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PERIODACTIVE failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_PERIODMONITOR,
			FT5X02_PERIODMONITOR);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PERIODMONITOR failed.\n", __func__);
		return err;
	}
	
	err = ft5x02_write_reg(client, FT5X02_REG_AUTO_CLB_MODE,
			FT5X02_AUTO_CLB_MODE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write AUTO_CLB_MODE failed.\n", __func__);
		return err;
	}

	err = ft5x02_write_reg(client, FT5X02_REG_MODE, FT5X02_MODE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MODE failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_PMODE, FT5X02_PMODE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PMODE failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_FIRMID, FT5X02_FIRMID);
	if (err < 0) {
		dev_err(&client->dev, "%s:write FIRMID failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_STATE, FT5X02_STATE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write STATE failed.\n", __func__);
		return err;
	}
	
	err = ft5x02_write_reg(client, FT5X02_REG_MAX_TOUCH_VALUE_HIGH,
			FT5X02_MAX_TOUCH_VALUE_HIGH);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MAX_TOUCH_VALUE_HIGH failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_MAX_TOUCH_VALUE_LOW,
			FT5X02_MAX_TOUCH_VALUE_LOW);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MAX_TOUCH_VALUE_LOW failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_FACE_DEC_MODE,
			FT5X02_FACE_DEC_MODE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write FACE_DEC_MODE failed.\n", __func__);
		return err;
	}
	err = ft5x02_write_reg(client, FT5X02_REG_DRAW_LINE_TH,
			FT5X02_DRAW_LINE_TH);
	if (err < 0) {
		dev_err(&client->dev, "%s:write DRAW_LINE_TH failed.\n", __func__);
		return err;
	}

	err = ft5x02_write_reg(client, FT5X02_REG_DIFFDATA_HADDLE_VALUE,
			FT5X02_DIFFDATA_HADDLE_VALUE);
	if (err < 0) {
		dev_err(&client->dev, "%s:write DIFFDATA_HADDLE_VALUE failed.\n", __func__);
		return err;
	}
	return err;
}

static int ft5x02_get_other_param(struct i2c_client *client)
{
	int err = 0;
	u8 value = 0x00;
	err = ft5x02_read_reg(client, FT5X02_REG_THGROUP, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THGROUP failed.\n", __func__);
		return err;
	} else 
		DBG("THGROUP=%02x\n", value<<2);
	err = ft5x02_read_reg(client, FT5X02_REG_THPEAK, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THPEAK failed.\n",
				__func__);
		return err;
	} else 
		DBG("THPEAK=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_THCAL, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THCAL failed.\n",
				__func__);
		return err;
	} else 
		DBG("THCAL=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_THWATER, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THWATER failed.\n",
				__func__);
		return err;
	} else 
		DBG("THWATER=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_THFALSE_TOUCH_PEAK,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THFALSE_TOUCH_PEAK failed.\n", __func__);
		return err;
	} else 
		DBG("THFALSE_TOUCH_PEAK=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_THDIFF, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write THDIFF failed.\n", __func__);
		return err;
	} else 
		DBG("THDIFF=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_CTRL, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write CTRL failed.\n", __func__);
		return err;
	} else 
		DBG("CTRL=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_TIMEENTERMONITOR,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write TIMEENTERMONITOR failed.\n", __func__);
		return err;
	} else 
		DBG("TIMEENTERMONITOR=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_PERIODACTIVE,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PERIODACTIVE failed.\n", __func__);
		return err;
	} else 
		DBG("PERIODACTIVE=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_PERIODMONITOR,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PERIODMONITOR failed.\n", __func__);
		return err;
	} else 
		DBG("PERIODMONITOR=%02x\n", value);
	
	err = ft5x02_read_reg(client, FT5X02_REG_CIPHER,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write CIPHER failed.\n", __func__);
		return err;
	} else 
		DBG("CIPHER=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_MODE, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MODE failed.\n", __func__);
		return err;
	} else 
		DBG("MODE=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_PMODE, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write PMODE failed.\n", __func__);
		return err;
	} else 
		DBG("PMODE=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_FIRMID, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write FIRMID failed.\n", __func__);
		return err;
	} else 
		DBG("FIRMID=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_STATE, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write STATE failed.\n", __func__);
		return err;
	} else 
		DBG("STATE=%02x\n", value);
	
	err = ft5x02_read_reg(client, FT5X02_REG_MAX_TOUCH_VALUE_HIGH,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MAX_TOUCH_VALUE_HIGH failed.\n", __func__);
		return err;
	} else 
		DBG("MAX_TOUCH_VALUE_HIGH=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_MAX_TOUCH_VALUE_LOW,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write MAX_TOUCH_VALUE_LOW failed.\n", __func__);
		return err;
	} else 
		DBG("MAX_TOUCH_VALUE_LOW=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_FACE_DEC_MODE,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write FACE_DEC_MODE failed.\n", __func__);
		return err;
	} else 
		DBG("FACE_DEC_MODE=%02x\n", value);
	err = ft5x02_read_reg(client, FT5X02_REG_DRAW_LINE_TH,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write DRAW_LINE_TH failed.\n", __func__);
		return err;
	} else 
		DBG("DRAW_LINE_TH=%02x\n", value);
	
	err = ft5x02_read_reg(client, FT5X02_REG_DIFFDATA_HADDLE_VALUE,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:write DIFFDATA_HADDLE_VALUE failed.\n", __func__);
		return err;
	} else 
		DBG("DIFFDATA_HADDLE_VALUE=%02x\n", value);
	return err;
}
int ft5x02_get_ic_param(struct i2c_client *client)
{
	int err = 0;
	int i = 0;
	u8 value = 0x00;
	u16 xvalue = 0x0000, yvalue = 0x0000;
	
	/*enter factory mode*/
	err = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE, FT5x02_FACTORYMODE_VALUE);
	if (err < 0) {
		dev_err(&client->dev, "%s:enter factory mode failed.\n", __func__);
		goto RETURN_WORK;
	}
	
	for (i = 0; i < g_ft5x02_tx_num; i++) {
		DBG("tx%d:", i);
		/*get tx order*/
		err = ft5x02_get_tx_order(client, i, &value);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not get tx%d order.\n",
					__func__, i);
			goto RETURN_WORK;
		}
		DBG("order=%d ", value);
		/*get tx cap*/
		err = ft5x02_get_tx_cap(client, i, &value);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not get tx%d cap.\n",
					__func__, i);
			goto RETURN_WORK;
		}
		DBG("cap=%02x\n", value);
	}
	/*get tx offset*/
	err = ft5x02_get_tx_offset(client, 0, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get tx 0 offset.\n",
				__func__);
		goto RETURN_WORK;
	} else
		DBG("tx offset = %02x\n", value);

	/*get rx offset and cap*/
	for (i = 0; i < g_ft5x02_rx_num; i++) {
		/*get rx order*/
		DBG("rx%d:", i);
		err = ft5x02_get_rx_order(client, i, &value);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not get rx%d order.\n",
					__func__, i);
			goto RETURN_WORK;
		}
		DBG("order=%d ", value);
		/*get rx cap*/
		err = ft5x02_get_rx_cap(client, i, &value);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not get rx%d cap.\n",
					__func__, i);
			goto RETURN_WORK;
		}
		DBG("cap=%02x ", value);
		err = ft5x02_get_rx_offset(client, i, &value);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not get rx offset.\n",
				__func__);
			goto RETURN_WORK;
		}
		DBG("offset=%02x\n", value);
	}

	/*get scan select*/
	err = ft5x02_get_scan_select(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get scan select.\n",
			__func__);
		goto RETURN_WORK;
	} else
		DBG("scan select = %02x\n", value);
	
	/*get tx number*/
	err = ft5x02_get_tx_num(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get tx num.\n",
			__func__);
		goto RETURN_WORK;
	} else
		DBG("tx num = %02x\n", value);
	/*get rx number*/
	err = ft5x02_get_rx_num(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get rx num.\n",
			__func__);
		goto RETURN_WORK;
	} else
		DBG("rx num = %02x\n", value);
	
	/*get gain*/
	err = ft5x02_get_gain(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get gain.\n",
			__func__);
		goto RETURN_WORK;
	} else
		DBG("gain = %02x\n", value);
	/*get voltage*/
	err = ft5x02_get_vol(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get voltage.\n",
			__func__);
		goto RETURN_WORK;
	} else
		DBG("voltage = %02x\n", value);
RETURN_WORK:	
	/*enter work mode*/
	err = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE, FT5x02_WORKMODE_VALUE);
	if (err < 0) {
		dev_err(&client->dev, "%s:enter work mode failed.\n", __func__);
		goto ERR_EXIT;
	}

	/*get resolution*/
	err = ft5x02_get_Resolution(client, &xvalue, &yvalue);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get resolution.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("resolution X = %d Y = %d\n", xvalue, yvalue);
	/*get face detect pre value*/
	err = ft5x02_get_face_detect_pre_value(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev,
				"%s:could not get face detect pre value.\n",
				__func__);
		goto ERR_EXIT;
	} else
		DBG("detect pre value = %02x\n", value);
	/*get face detect num*/
	err = ft5x02_get_face_detect_num(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get face detect num.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("face detect num = %02x\n", value);
	/*get face detect last time*/
	err = ft5x02_get_face_detect_last_time(client,
			&xvalue);
	if (err < 0) {
		dev_err(&client->dev,
			"%s:could not get face detect last time.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("face detect last time = %d\n", xvalue);
	/*get face detect on*/
	err = ft5x02_get_face_detect_on(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get face detect on.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("face detect on = %02x\n", value);
	/*get face detect on*/
	err = ft5x02_get_face_detect_off(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get face detect off.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("face detect off = %02x\n", value);
	/*get min peak value*/
	err = ft5x02_get_peak_value_min(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get min peak value.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("peak value min = %02x\n", value);
	/*get diff value over num*/
	err = ft5x02_get_diff_value_over_num(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get diff value over num.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("diff value over num = %02x\n", value);
	/*get diff value percent*/
	err = ft5x02_get_diff_value_percent(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get diff value percent.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("diff value percent = %02x\n", value);
	/*get point auto clear time*/
	err = ft5x02_get_point_auto_clear_time(client,
			&xvalue);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get point auto clear time.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("point auto clear time = %d\n", xvalue);

	/*get kx*/
	err = ft5x02_get_kx(client, &xvalue);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get kx.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("kx = %02x\n", xvalue);
	/*get ky*/
	err = ft5x02_get_ky(client, &xvalue);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get ky.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("ky = %02x\n", xvalue);
	/*get lemda x*/
	err = ft5x02_get_lemda_x(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get lemda x.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("lemda x = %02x\n", value);
	/*get lemda y*/
	err = ft5x02_get_lemda_y(client,
			&value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get lemda y.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("lemda y = %02x\n", value);
	/*get pos x*/
	err = ft5x02_get_pos_x(client, &value);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not get pos x.\n",
			__func__);
		goto ERR_EXIT;
	} else
		DBG("pos x = %02x\n", value);

	err = ft5x02_get_other_param(client);
	
ERR_EXIT:
	return err;
}

int ft5x02_Init_IC_Param(struct i2c_client *client)
{
	int err = 0;
	int i = 0;
	
	/*enter factory mode*/
	err = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE, FT5x02_FACTORYMODE_VALUE);
	if (err < 0) {
		dev_err(&client->dev, "%s:enter factory mode failed.\n", __func__);
		goto RETURN_WORK;
	}
	
	for (i = 0; i < g_ft5x02_tx_num; i++) {
		if (g_ft5x02_tx_order[i] != 0xFF) {
			/*set tx order*/
			err = ft5x02_set_tx_order(client, i, g_ft5x02_tx_order[i]);
			if (err < 0) {
				dev_err(&client->dev, "%s:could not set tx%d order.\n",
						__func__, i);
				goto RETURN_WORK;
			}
		}
		/*set tx cap*/
		err = ft5x02_set_tx_cap(client, i, g_ft5x02_tx_cap[i]);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not set tx%d cap.\n",
					__func__, i);
			goto RETURN_WORK;
		}
	}
	/*set tx offset*/
	err = ft5x02_set_tx_offset(client, 0, g_ft5x02_tx_offset);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set tx 0 offset.\n",
				__func__);
		goto RETURN_WORK;
	}

	/*set rx offset and cap*/
	for (i = 0; i < g_ft5x02_rx_num; i++) {
		/*set rx order*/
		err = ft5x02_set_rx_order(client, i, g_ft5x02_rx_order[i]);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not set rx%d order.\n",
					__func__, i);
			goto RETURN_WORK;
		}
		/*set rx cap*/
		err = ft5x02_set_rx_cap(client, i, g_ft5x02_rx_cap[i]);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not set rx%d cap.\n",
					__func__, i);
			goto RETURN_WORK;
		}
	}
	for (i = 0; i < g_ft5x02_rx_num/2; i++) {
		err = ft5x02_set_rx_offset(client, i*2, g_ft5x02_rx_offset[i]>>4);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not set rx offset.\n",
				__func__);
			goto RETURN_WORK;
		}
		err = ft5x02_set_rx_offset(client, i*2+1, g_ft5x02_rx_offset[i]&0x0F);
		if (err < 0) {
			dev_err(&client->dev, "%s:could not set rx offset.\n",
				__func__);
			goto RETURN_WORK;
		}
	}

	/*set scan select*/
	err = ft5x02_set_scan_select(client, g_ft5x02_scanselect);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set scan select.\n",
			__func__);
		goto RETURN_WORK;
	}
	
	/*set tx number*/
	err = ft5x02_set_tx_num(client, g_ft5x02_tx_num);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set tx num.\n",
			__func__);
		goto RETURN_WORK;
	}
	/*set rx number*/
	err = ft5x02_set_rx_num(client, g_ft5x02_rx_num);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set rx num.\n",
			__func__);
		goto RETURN_WORK;
	}
	
	/*set gain*/
	err = ft5x02_set_gain(client, g_ft5x02_gain);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set gain.\n",
			__func__);
		goto RETURN_WORK;
	}
	/*set voltage*/
	err = ft5x02_set_vol(client, g_ft5x02_voltage);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set voltage.\n",
			__func__);
		goto RETURN_WORK;
	}
RETURN_WORK:	
	/*enter work mode*/
	err = ft5x02_write_reg(client, FT5x02_REG_DEVICE_MODE, FT5x02_WORKMODE_VALUE);
	if (err < 0) {
		dev_err(&client->dev, "%s:enter work mode failed.\n", __func__);
		goto ERR_EXIT;
	}

	/*set resolution*/
	err = ft5x02_set_Resolution(client, FT5X02_RESOLUTION_X,
				FT5X02_RESOLUTION_Y);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set resolution.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set face detect pre value*/
	err = ft5x02_set_face_detect_pre_value(client,
			FT5X02_FACE_DETECT_PRE_VALUE);
	if (err < 0) {
		dev_err(&client->dev,
				"%s:could not set face detect pre value.\n",
				__func__);
		goto ERR_EXIT;
	}
	/*set face detect num*/
	err = ft5x02_set_face_detect_num(client,
			FT5X02_FACE_DETECT_NUM);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set face detect num.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set face detect last time*/
	err = ft5x02_set_face_detect_last_time(client,
			FT5X02_FACE_DETECT_LAST_TIME);
	if (err < 0) {
		dev_err(&client->dev,
			"%s:could not set face detect last time.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set face detect on*/
	err = ft5x02_set_face_detect_on(client,
			FT5X02_FACE_DETECT_ON);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set face detect on.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set face detect on*/
	err = ft5x02_set_face_detect_off(client,
			FT5X02_FACE_DETECT_OFF);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set face detect off.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set min peak value*/
	err = ft5x02_set_peak_value_min(client,
			FT5X02_PEAK_VALUE_MIN);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set min peak value.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set diff value over num*/
	err = ft5x02_set_diff_value_over_num(client,
			FT5X02_DIFF_VALUE_OVER_NUM);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set diff value over num.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set diff value percent*/
	err = ft5x02_set_diff_value_percent(client,
			FT5X02_DIFF_VALUE_PERCENT);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set diff value percent.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set point auto clear time*/
	err = ft5x02_set_point_auto_clear_time(client,
			FT5X02_POINT_AUTO_CLEAR_TIME);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set point auto clear time.\n",
			__func__);
		goto ERR_EXIT;
	}

	/*set kx*/
	err = ft5x02_set_kx(client, FT5X02_KX);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set kx.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set ky*/
	err = ft5x02_set_ky(client, FT5X02_KY);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set ky.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set lemda x*/
	err = ft5x02_set_lemda_x(client,
			FT5X02_LEMDA_X);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set lemda x.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set lemda y*/
	err = ft5x02_set_lemda_y(client,
			FT5X02_LEMDA_Y);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set lemda y.\n",
			__func__);
		goto ERR_EXIT;
	}
	/*set pos x*/
	err = ft5x02_set_pos_x(client, FT5X02_POS_X);
	if (err < 0) {
		dev_err(&client->dev, "%s:could not set pos x.\n",
			__func__);
		goto ERR_EXIT;
	}

	err = ft5x02_set_other_param(client);
	
ERR_EXIT:
	return err;
}

