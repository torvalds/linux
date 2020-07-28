/* drivers/input/sensors/access/sc7a30.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
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
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>
#include <linux/ioctl.h>
#include <linux/wakelock.h>

#define SC7A30_ENABLE			1
#define SC7A30_XOUT_L			0x28
#define SC7A30_XOUT_H			0x29
#define SC7A30_YOUT_L			0x2A
#define SC7A30_YOUT_H			0x2B
#define SC7A30_ZOUT_L			0x2C
#define SC7A30_ZOUT_H			0x2D
#define SC7A30_MODE			0x20
#define SC7A30_MODE1			0x21
#define SC7A30_MODE2			0x22
#define SC7A30_MODE3			0x23
#define SC7A30_BOOT			0x24
#define SC7A30_STATUS			0x27
#define SC7A30_50HZ			0x40
#define SC7A30_100HZ			0x50
#define SC7A30_200HZ			0x60
#define SC7A30_400HZ			0x70
#define SC7A30_RANGE			32768

#define CALIBRATION_NUM		20//40
#define AXIS_X_Y_RANGE_LIMIT	200
#define AXIS_X_Y_AVG_LIMIT	400
#define AXIS_Z_RANGE		200
#define AXIS_Z_DFT_G		1000
#define GOTO_CALI		100
#define FAILTO_CALI		101
/* LIS3DH */
#define SC7A30_PRECISION        12
#define SC7A30_BOUNDARY		(0x1 << (SC7A30_PRECISION - 1))
#define SC7A30_GRAVITY_STEP	(SC7A30_RANGE / SC7A30_BOUNDARY)

#define SC7A30_COUNT_AVERAGE	2

#define CFG_GSENSOR_CALIBFILE   "/data/data/com.actions.sensor.calib/files/gsensor_calib.txt"

struct SC7A30_acc {
	int    x;
	int    y;
	int    z;
};

static struct SC7A30_acc offset;
static int calibrated;
static struct i2c_client *sc7a30_client;

struct sensor_axis_average {
		long x_average;
		long y_average;
		long z_average;
		int count;
};

struct Cali_Data {
	//mis p and n
	unsigned char xpmis; //x axis positive mismatch to write
	unsigned char xnmis; //x axis negtive mismatch to write
	unsigned char ypmis;
	unsigned char ynmis;
	unsigned char zpmis;
	unsigned char znmis;
	//off p and n
	unsigned char xpoff; //x axis positive offset to write
	unsigned char xnoff; //x axis negtive offset to write
	unsigned char ypoff;
	unsigned char ynoff;
	unsigned char zpoff;
	unsigned char znoff;
	//mid mis and off
	unsigned char xmmis; //x axis middle mismatch to write
	unsigned char ymmis; //y axis middle mismatch to write
	unsigned char zmmis; //z axis middle mismatch to write
	unsigned char xmoff; //x axis middle offset to write
	unsigned char ymoff; //y axis middle offset to write
	unsigned char zmoff; //z axis middle offset to write
	//output p and n
	signed int xpoutput; //x axis output of positive mismatch
	signed int xnoutput; //x axis output of negtive mismatch
	signed int ypoutput;
	signed int ynoutput;
	signed int zpoutput;
	signed int znoutput;
	//output
	signed int xfoutput; //x axis the best or the temporary output
	signed int yfoutput; //y axis the best or the temporary output
	signed int zfoutput; //z axis the best or the temporary output
	//final and temp flag
	unsigned char xfinalf; //x axis final flag:if 1,calibration finished
	unsigned char yfinalf; //y axis final flag:if 1,calibration finished
	unsigned char zfinalf; //z axis final flag:if 1,calibration finished
	unsigned char xtempf;  //x axis temp flag:if 1,the step calibration finished
	unsigned char ytempf;  //y axis temp flag:if 1,the step calibration finished
	unsigned char ztempf;  //z axis temp flag:if 1,the step calibration finished

	unsigned char xaddmis;	//x axis mismtach register address
	unsigned char yaddmis;	//y axis mismtach register address
	unsigned char zaddmis;	//z axis mismtach register address
	unsigned char xaddoff;	//x axis offset register address
	unsigned char yaddoff;	//y axis offset register address
	unsigned char zaddoff;	//z axis offset register address

	unsigned char (*MisDataSpaceConvert)(unsigned char continuous);	//mismatch space convert function pointer
	unsigned char (*OffDataSpaceConvert)(unsigned char continuous);	//offset space convert function pointer
};

static struct sensor_axis_average axis_average;

static unsigned char Read_Reg(unsigned char reg)
{
	char buffer[3] = {0};
	*buffer = reg;
	sensor_rx_data(sc7a30_client, buffer, 1);
	return buffer[0];
}

static void Read_Output_3axis(unsigned char *acc_buf)
{
	char buffer[3] = {0};
	int index = 0;
	int ret = 0;
	while(1){
			msleep(20);
			*buffer = SC7A30_STATUS;
			//ret = sensor_rx_data(sc7a30_client, buffer,1);
			buffer[0] = Read_Reg(0x27);
			if( (buffer[0] & 0x08) != 0 ) {break;}
	index++;
	if(index > 40)break;
	}
	//6 register data be read out
	*buffer = SC7A30_XOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer, 1);
	acc_buf[0] = buffer[0];
	*buffer = SC7A30_XOUT_H;
	ret = sensor_rx_data(sc7a30_client, buffer, 1);
	acc_buf[1] = buffer[0];

	*buffer = SC7A30_YOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer, 1);
	acc_buf[2] = buffer[0];
	*buffer = SC7A30_YOUT_H;
	ret = sensor_rx_data(sc7a30_client, buffer, 1);
	acc_buf[3] = buffer[0];

	*buffer = SC7A30_ZOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer,1);
	acc_buf[4] = buffer[0];
	*buffer = SC7A30_ZOUT_H;
	ret = sensor_rx_data(sc7a30_client, buffer, 1);
	acc_buf[5] = buffer[0];
}

static void Write_Input(char addr, char thedata)
{
	int result;
	result = sensor_write_reg(sc7a30_client, addr, thedata);
}

static void tilt_3axis_mtp(signed int x, signed int y, signed int z)
{
	char buffer[6] = {0};
	unsigned char buffer0[6] = {0};
	unsigned char buffer1[6] = {0};
	signed char mtpread[3]={0};
	signed int xoutp, youtp, zoutp;
	signed int xoutpt, youtpt, zoutpt;
	signed char xtilt, ytilt, ztilt;
	xoutp = youtp = zoutp = 0;
	xoutpt = youtpt = zoutpt = 0;
	xtilt = ytilt = ztilt = 0;
	Read_Output_3axis(buffer0);
	Read_Output_3axis(buffer1);

	xoutpt = ((signed int)((buffer1[1]<<8)|buffer1[0]))>>4;
	youtpt = ((signed int)((buffer1[3]<<8)|buffer1[2]))>>4;
	zoutpt = ((signed int)((buffer1[5]<<8)|buffer1[4]))>>4;

	xoutp = xoutpt-x*16;
	youtp = youtpt-y*16;
	zoutp = zoutpt-z*16;

	*buffer = 0x10;
	sensor_rx_data(sc7a30_client, buffer, 1);
	mtpread[0]=(signed char)buffer[0];

	*buffer = 0x11;
	sensor_rx_data(sc7a30_client, buffer, 1);
	mtpread[1]=(signed char)buffer[0];

	*buffer = 0x12;
	sensor_rx_data(sc7a30_client, buffer, 1);
	mtpread[2]=(signed char)buffer[0];

	xtilt=(signed char)(xoutp/8)+ mtpread[0];
	ytilt=(signed char)(youtp/8)+ mtpread[1];
	ztilt=(signed char)(zoutp/8)+ mtpread[2];

	Write_Input(0x10, xtilt);
	Write_Input(0x11, ytilt);
	Write_Input(0x12, ztilt);

}

static unsigned char forword_MisDataSpaceConvert(unsigned char continuous)
{
	if (continuous >= 128)
		return continuous - 128;
	else
		return 255 - continuous;
}

static unsigned char reverse_MisDataSpaceConvert(unsigned char continuous)
{
	if (continuous >= 128)
		return continuous;
	else
		return 127 - continuous;
}

static unsigned char reverse_OffDataSpaceConvert(unsigned char continuous)
{
		return 127 - continuous;
}


static unsigned char forword_OffDataSpaceConvert(unsigned char continuous)
{
		return continuous;
}

static void check_output_set_finalflag(struct Cali_Data *pcalidata,unsigned char err)
{

	if (abs(pcalidata->xfoutput) < err) {
		//printk("line:%d Xcali finish!Final=%d\n",__LINE__,pcalidata->xfoutput);
		pcalidata->xfinalf=1;
	}
	if (abs(pcalidata->yfoutput) < err) {
		//printk("line:%d Xcali finish!Final=%d\n",__LINE__,pcalidata->yfoutput);
		pcalidata->yfinalf=1;
	}
	if (abs(pcalidata->zfoutput) < err) {
		//printk("line:%d Xcali finish!Final=%d\n",__LINE__,pcalidata->zfoutput);
		pcalidata->zfinalf=1;
	}

}

static void check_finalflag_set_tempflag(struct Cali_Data *pcalidata)
{
	if (pcalidata->xfinalf) { pcalidata->xtempf=1; }
	if (pcalidata->yfinalf) { pcalidata->ytempf=1; }
	if (pcalidata->zfinalf) { pcalidata->ztempf=1; }
}

static unsigned char check_flag_is_return(struct Cali_Data *pcalidata)
{
	if ((pcalidata->xfinalf) && (pcalidata->yfinalf) && (pcalidata->zfinalf))
	{
		//printk("line:%d Allcali finish!\n",__LINE__);
		return 1;//xyz cali ok
	} else 
		return 0;
}

static void updata_midmis_address(struct Cali_Data *pcalidata)
{
	if(pcalidata->xtempf == 0) {
		pcalidata->xmmis = (unsigned char)(((unsigned int)(pcalidata->xpmis) + (unsigned int)(pcalidata->xnmis))/2);
		pcalidata->MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(pcalidata->xaddmis, (*(pcalidata->MisDataSpaceConvert))(pcalidata->xmmis));
	}
	if(pcalidata->ytempf == 0) {
		pcalidata->ymmis = (unsigned char)(((unsigned int)(pcalidata->ypmis) + (unsigned int)(pcalidata->ynmis))/2);
		pcalidata->MisDataSpaceConvert = forword_MisDataSpaceConvert;
		Write_Input(pcalidata->yaddmis, (*(pcalidata->MisDataSpaceConvert))(pcalidata->ymmis));
	}	
	if(pcalidata->ztempf == 0) {
		pcalidata->zmmis = (unsigned char)(((unsigned int)(pcalidata->zpmis) + (unsigned int)(pcalidata->znmis))/2);
		pcalidata->MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(pcalidata->zaddmis, (*(pcalidata->MisDataSpaceConvert))(pcalidata->zmmis));
	}
}

static void updata_midoff_address(struct Cali_Data *pcalidata)
{
	if (pcalidata->xtempf == 0) {
		pcalidata->xmoff = (unsigned char)(((unsigned int)(pcalidata->xpoff) + (unsigned int)(pcalidata->xnoff))/2);
		pcalidata->OffDataSpaceConvert = reverse_OffDataSpaceConvert;
		Write_Input(pcalidata->xaddoff, (*(pcalidata->OffDataSpaceConvert))(pcalidata->xmoff));
	}
	if (pcalidata->ytempf == 0) {
		pcalidata->ymoff = (unsigned char)(((unsigned int)(pcalidata->ypoff) + (unsigned int)(pcalidata->ynoff))/2);
		pcalidata->OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(pcalidata->yaddoff, (*(pcalidata->OffDataSpaceConvert))(pcalidata->ymoff));
	}
	if (pcalidata->ztempf == 0) {
		pcalidata->zmoff = (unsigned char)(((unsigned int)(pcalidata->zpoff) + (unsigned int)(pcalidata->znoff))/2);
		pcalidata->OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(pcalidata->zaddoff, (*(pcalidata->OffDataSpaceConvert))(pcalidata->zmoff));
	}
}

static void updata_mmis_pnfoutput_set_tempflag( struct Cali_Data *pcalidata,
											unsigned char *buf,
										  	signed int xrel,
										  	signed int yrel,
										  	signed int zrel)
{
	
	pcalidata->xfoutput = (signed int)((signed char)buf[1])-xrel;
	pcalidata->yfoutput = (signed int)((signed char)buf[3])-yrel;
	pcalidata->zfoutput  =(signed int)((signed char)buf[5])-zrel;

	if (abs(pcalidata->xfoutput)<25)pcalidata->xtempf=1;
	if (abs(pcalidata->yfoutput)<25)pcalidata->ytempf=1;
	if (abs(pcalidata->zfoutput)<25)pcalidata->ztempf=1;

	if (pcalidata->xtempf == 0)
	{
		if (pcalidata->xfoutput>0) {
			pcalidata->xpoutput = pcalidata->xfoutput;
			pcalidata->xpmis = pcalidata->xmmis;
		}
		else {
			pcalidata->xnoutput = pcalidata->xfoutput;
			pcalidata->xnmis = pcalidata->xmmis;
		}
	}

	if (pcalidata->ytempf == 0)
	{
		if (pcalidata->yfoutput>0){
			pcalidata->ypoutput = pcalidata->yfoutput;
			pcalidata->ypmis = pcalidata->ymmis;
		}
		else{
			pcalidata->ynoutput = pcalidata->yfoutput;
			pcalidata->ynmis = pcalidata->ymmis;
		}
	}

	if(pcalidata->ztempf==0)
	{
		if(pcalidata->zfoutput>0){
			pcalidata->zpoutput = pcalidata->zfoutput;
			pcalidata->zpmis = pcalidata->zmmis;
		}
		else{
			pcalidata->znoutput = pcalidata->zfoutput;
			pcalidata->znmis = pcalidata->zmmis;
		}
	}
}

static void updata_moff_pnfoutput_set_tempflag(	struct Cali_Data *pcalidata,
											unsigned char *buf,
										  	signed int xrel,
										  	signed int yrel,
										  	signed int zrel)
{

	pcalidata->xfoutput = (signed int)((signed char)buf[1])-xrel;
	pcalidata->yfoutput = (signed int)((signed char)buf[3])-yrel;
	pcalidata->zfoutput = (signed int)((signed char)buf[5])-zrel;

	if (abs(pcalidata->xfoutput)<3)pcalidata->xtempf = 1;
	if (abs(pcalidata->yfoutput)<3)pcalidata->ytempf = 1;
	if (abs(pcalidata->zfoutput)<3)pcalidata->ztempf = 1;

	if (pcalidata->xtempf == 0)
	{
		if(pcalidata->xfoutput>0){
			pcalidata->xpoutput = pcalidata->xfoutput;
			pcalidata->xpoff = pcalidata->xmoff;
		} else {
			pcalidata->xnoutput = pcalidata->xfoutput;
			pcalidata->xnoff = pcalidata->xmoff;
		}
	}

	if (pcalidata->ytempf == 0)
	{
		if(pcalidata->yfoutput>0){
			pcalidata->ypoutput = pcalidata->yfoutput;
			pcalidata->ypoff = pcalidata->ymoff;
		} else {
			pcalidata->ynoutput = pcalidata->yfoutput;
			pcalidata->ynoff = pcalidata->ymoff;
		}
	}

	if (pcalidata->ztempf == 0)
	{
		if (pcalidata->zfoutput > 0) {
			pcalidata->zpoutput = pcalidata->zfoutput;
			pcalidata->zpoff = pcalidata->zmoff;
		} else {
			pcalidata->znoutput = pcalidata->zfoutput;
			pcalidata->znoff = pcalidata->zmoff;
		}
	}
}

static int auto_calibration_instant(signed int x, signed int y, signed int z)
{

	unsigned char count=0,cyclecount=0;
	unsigned char acc_buf[6];
        
	struct Cali_Data calidata={0};

	calidata.xaddmis = 0x40;
	calidata.yaddmis = 0x41;
	calidata.zaddmis = 0x42;
	calidata.xaddoff = 0x47;
	calidata.yaddoff = 0x48;
	calidata.zaddoff = 0x49;
#ifdef PRINT
	printf("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(UINT)calidata.xfinalf,(UINT)calidata.xtempf,
				(UINT)calidata.yfinalf,(UINT)calidata.ytempf,
				(UINT)calidata.zfinalf,(UINT)calidata.ztempf
				);
#endif

	Read_Output_3axis(acc_buf);
	calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
	calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
	calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
	check_output_set_finalflag(&calidata,2);
	if(check_flag_is_return(&calidata)){
		printk("step1:=file=%s,line=%d\n",__FILE__,__LINE__);
		return 1;
	}
#ifdef PRINT
	printf("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(UINT)calidata.xfinalf,(UINT)calidata.xtempf,
				(UINT)calidata.yfinalf,(UINT)calidata.ytempf,
				(UINT)calidata.zfinalf,(UINT)calidata.ztempf
				);
#endif

	if (calidata.xfinalf == 0) {
		Write_Input(calidata.xaddoff, 0x3f);//cali mis under off=0x3f
		Write_Input(0x10, 0); //tilt clear
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.xaddmis, (*(calidata.MisDataSpaceConvert))(255)); // x mis to max
	}
	if (calidata.yfinalf == 0) {
		Write_Input(calidata.yaddoff, 0x3f);//cali mis under off=0x3f
		Write_Input(0x11, 0); //tilt clear
		calidata.MisDataSpaceConvert = forword_MisDataSpaceConvert;
		Write_Input(calidata.yaddmis, (*(calidata.MisDataSpaceConvert))(255)); // y mis to max
	}
	if (calidata.zfinalf == 0){
		Write_Input(calidata.zaddoff, 0x3f);//cali mis under off=0x3f
		Write_Input(0x12, 0); //tilt clear
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.zaddmis, (*(calidata.MisDataSpaceConvert))(255)); // z mis to max
	}

	Read_Output_3axis(acc_buf);
	calidata.xpoutput=calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
	calidata.ypoutput=calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
	calidata.zpoutput=calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
	printk("step 2 xnoutput = %d ynoutput = %d znoutput = %d \n",calidata.xnoutput,calidata.ynoutput,calidata.znoutput);
	if ((calidata.xpoutput<-25) || (calidata.ypoutput<-25) || (calidata.zpoutput<-25)){
		printk("step2:=file=%s,line=%d\n",__FILE__,__LINE__);
		sensor_write_reg(sc7a30_client,0x13,0x01);//allen                                         
		Write_Input(0x1e, 0x15);  //保存校准寄存器的修改
		mdelay(300);
		// Write_Input(0x1e, 0);
		return 0; 
	}

	if (calidata.xfinalf == 0) {
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.xaddmis, (*(calidata.MisDataSpaceConvert))(0)); // x mis to min
	}
	if (calidata.yfinalf == 0) {
		calidata.MisDataSpaceConvert = forword_MisDataSpaceConvert;
		Write_Input(calidata.yaddmis, (*(calidata.MisDataSpaceConvert))(0)); // y mis to min
	}
	if (calidata.zfinalf == 0) {
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.zaddmis, (*(calidata.MisDataSpaceConvert))(0)); // z mis to min
	}
	Read_Output_3axis(acc_buf);
	calidata.xnoutput=calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
	calidata.ynoutput=calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
	calidata.znoutput=calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
	printk("step 2 xnoutput = %d ynoutput = %d znoutput = %d \n",calidata.xnoutput,calidata.ynoutput,calidata.znoutput);
	if ((calidata.xnoutput>25) || (calidata.ynoutput>25) || (calidata.znoutput>25)) {
		printk("step2:=file=%s,line=%d\n",__FILE__,__LINE__);
		sensor_write_reg(sc7a30_client,0x13,0x01);
		Write_Input(0x1e, 0x15);  
		mdelay(300);

		return 0; 
	}

	if (abs(calidata.xpoutput)<=abs(calidata.xnoutput)) {
		calidata.xfoutput=calidata.xpoutput;
		calidata.xmmis=255;
	} else {
		calidata.xfoutput=calidata.xnoutput;
		calidata.xmmis=0; 
	}
	if (abs(calidata.ypoutput)<=abs(calidata.ynoutput)) {
		calidata.yfoutput=calidata.ypoutput;
		calidata.ymmis=255;
	} else {
		calidata.yfoutput=calidata.ynoutput;
		calidata.ymmis=0; 
	}
	if (abs(calidata.zpoutput)<=abs(calidata.znoutput)) {
		calidata.zfoutput=calidata.zpoutput;
		calidata.zmmis=255;
	} else {
		calidata.zfoutput=calidata.znoutput;
		calidata.zmmis=0; 
	}

	if (calidata.xfinalf == 0) {
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.xaddmis, (*(calidata.MisDataSpaceConvert))(calidata.xmmis)); 
	}
	if (calidata.yfinalf == 0) {
		calidata.MisDataSpaceConvert = forword_MisDataSpaceConvert;
		Write_Input(calidata.yaddmis, (*(calidata.MisDataSpaceConvert))(calidata.ymmis)); 
	}
	if (calidata.zfinalf == 0) {
		calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
		Write_Input(calidata.zaddmis, (*(calidata.MisDataSpaceConvert))(calidata.zmmis)); 
	}
	check_output_set_finalflag(&calidata,2);

	if(abs(calidata.xfoutput)<25) calidata.xtempf=1;
	if(abs(calidata.yfoutput)<25) calidata.ytempf=1;
	if(abs(calidata.zfoutput)<25) calidata.ztempf=1;

	calidata.xpmis=calidata.ypmis=calidata.zpmis=255;
	calidata.xnmis=calidata.ynmis=calidata.znmis=0;
	check_finalflag_set_tempflag(&calidata);
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	cyclecount=0;
	while(1){
		if (++cyclecount > 20)
			break;

		if((calidata.xtempf)&&(calidata.ytempf)&&(calidata.ztempf))break;						
		updata_midmis_address(&calidata);
		Read_Output_3axis(acc_buf);
		calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
		calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
		calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
#ifdef PRINT
		printk("xp%4d=%4d,xm%4d=%4d,xn%4d=%4d,      yp%4d=%4d,ym%4d=%4d,yn%4d=%4d,      zp%4d=%4d,zm%4d=%4d,zn%4d=%4d\n\r",
				calidata.xpoutput,(unsigned int)calidata.xpmis,
				calidata.xfoutput,(unsigned int)calidata.xmmis,
				calidata.xnoutput,(unsigned int)calidata.xnmis,
				calidata.ypoutput,(unsigned int)calidata.ypmis,
				calidata.yfoutput,(unsigned int)calidata.ymmis,
				calidata.ynoutput,(unsigned int)calidata.ynmis,
				calidata.zpoutput,(unsigned int)calidata.zpmis,
				calidata.zfoutput,(unsigned int)calidata.zmmis,
				calidata.znoutput,(unsigned int)calidata.znmis
				);
#endif
		updata_mmis_pnfoutput_set_tempflag(&calidata,acc_buf,x,y,z);
		check_output_set_finalflag(&calidata,2);
		if(check_flag_is_return(&calidata))return 1;
	}
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif

	calidata.xtempf=calidata.ytempf=calidata.ztempf=1;
	if((calidata.xmmis>0)&&(calidata.xmmis<255))calidata.xtempf=0;
	if((calidata.ymmis>0)&&(calidata.ymmis<255))calidata.ytempf=0;
	if((calidata.zmmis>0)&&(calidata.zmmis<255))calidata.ztempf=0;
	calidata.xpmis=calidata.xnmis=calidata.xmmis;
	calidata.ypmis=calidata.ynmis=calidata.ymmis;
	calidata.zpmis=calidata.znmis=calidata.zmmis;
	for(count = 0; count < 3; count++)
	{
		if(calidata.xtempf==0){
			calidata.xpmis = calidata.xmmis + count - 1;
			if((calidata.xpmis>calidata.xmmis)&&(calidata.xpmis==128))calidata.xpmis = calidata.xmmis + count-1 + 1;
			if((calidata.xpmis<calidata.xmmis)&&(calidata.xpmis==127))calidata.xpmis = calidata.xmmis + count-1 - 1;
			calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
			Write_Input(calidata.xaddmis, (*(calidata.MisDataSpaceConvert))(calidata.xpmis));
		}
		if(calidata.ytempf==0){
			calidata.ypmis = calidata.ymmis + count - 1;
			if((calidata.ypmis>calidata.ymmis)&&(calidata.ypmis==128))calidata.ypmis = calidata.ymmis + count-1 + 1;
			if((calidata.ypmis<calidata.ymmis)&&(calidata.ypmis==127))calidata.ypmis = calidata.ymmis + count-1 - 1;
			calidata.MisDataSpaceConvert = forword_MisDataSpaceConvert;
			Write_Input(calidata.yaddmis, (*(calidata.MisDataSpaceConvert))(calidata.ypmis));
		}
		if(calidata.ztempf==0){
			calidata.zpmis = calidata.zmmis + count - 1;
			if((calidata.zpmis>calidata.zmmis)&&(calidata.zpmis==128))calidata.zpmis = calidata.zmmis + count-1 + 1;
			if((calidata.zpmis<calidata.zmmis)&&(calidata.zpmis==127))calidata.zpmis = calidata.zmmis + count-1 - 1;
			calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
			Write_Input(calidata.zaddmis, (*(calidata.MisDataSpaceConvert))(calidata.zpmis));
		}
		Read_Output_3axis(acc_buf);
		if(abs((signed int)((signed char)acc_buf[1])-x)<abs(calidata.xfoutput)){
			calidata.xnmis=calidata.xpmis;
			calidata.xfoutput= (signed int)((signed char)acc_buf[1])-x;
		}
		if(abs((signed int)((signed char)acc_buf[3])-y)<abs(calidata.yfoutput)){
			calidata.ynmis=calidata.ypmis;
			calidata.yfoutput= (signed int)((signed char)acc_buf[3])-y;
		}
		if(abs((signed int)((signed char)acc_buf[5])-z)<abs(calidata.zfoutput)){
			calidata.znmis=calidata.zpmis;
			calidata.zfoutput= (signed int)((signed char)acc_buf[5])-z;
		}
		if(calidata.xtempf==0){
			calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
			Write_Input(calidata.xaddmis, (*(calidata.MisDataSpaceConvert))(calidata.xnmis));
		}
		if(calidata.ytempf==0){
			calidata.MisDataSpaceConvert = forword_MisDataSpaceConvert;
			Write_Input(calidata.yaddmis, (*(calidata.MisDataSpaceConvert))(calidata.ynmis));
		}
		if(calidata.ztempf==0){
			calidata.MisDataSpaceConvert = reverse_MisDataSpaceConvert;
			Write_Input(calidata.zaddmis, (*(calidata.MisDataSpaceConvert))(calidata.znmis));	
		}
#ifdef PRINT
		printk("L%4d:xf=%4d,xmis=%4d,yf=%4d,ymis=%4d,zf=%4d,zmis=%4d\n\r",__LINE__,
					(signed int)((signed char)acc_buf[1])-x,(unsigned int)calidata.xpmis,		
					(signed int)((signed char)acc_buf[3])-y,(unsigned int)calidata.ypmis,
					(signed int)((signed char)acc_buf[5])-z,(unsigned int)calidata.zpmis
					);
#endif

	}
	//Write_Input(AddMis, (*MisDataSpaceConvert)(FinaloutputMisConfiguration));

	calidata.xpoff=calidata.ypoff=calidata.zpoff=0x7f;
	calidata.xnoff=calidata.ynoff=calidata.znoff=0;
	calidata.xtempf=calidata.ytempf=calidata.ztempf=0;
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	check_finalflag_set_tempflag(&calidata);
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	//offset max
	if(calidata.xtempf==0){
		calidata.OffDataSpaceConvert = reverse_OffDataSpaceConvert;
		Write_Input(calidata.xaddoff, (*(calidata.OffDataSpaceConvert))(calidata.xpoff)); // x off to max
	}
	if(calidata.ytempf==0){
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.yaddoff, (*(calidata.OffDataSpaceConvert))(calidata.xpoff)); // y off to max
	}
	if(calidata.ztempf==0){
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.zaddoff, (*(calidata.OffDataSpaceConvert))(calidata.xpoff)); // z off to max
	}
	Read_Output_3axis(acc_buf);
	calidata.xpoutput=calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
	calidata.ypoutput=calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
	calidata.zpoutput=calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	check_output_set_finalflag(&calidata,2);
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	//offset min
	if (calidata.xtempf == 0) {
		calidata.OffDataSpaceConvert = reverse_OffDataSpaceConvert;
		Write_Input(calidata.xaddoff, (*(calidata.OffDataSpaceConvert))(calidata.xnoff)); // x off to min
	}
	if (calidata.ytempf == 0) {
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.yaddoff, (*(calidata.OffDataSpaceConvert))(calidata.ynoff)); // y off to min
	}
	if (calidata.ztempf == 0) {
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.zaddoff, (*(calidata.OffDataSpaceConvert))(calidata.znoff)); // z off to min
	}
	Read_Output_3axis(acc_buf);
	calidata.xnoutput=calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
	calidata.ynoutput=calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
	calidata.znoutput=calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	check_output_set_finalflag(&calidata,2);
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	if (abs(calidata.xpoutput)<=abs(calidata.xnoutput)) {
		calidata.xfoutput=calidata.xpoutput;
		calidata.xmoff=calidata.xpoff;
	} else {
		calidata.xfoutput=calidata.xnoutput;
		calidata.xmoff=calidata.xnoff; 
	}
	if (abs(calidata.ypoutput)<=abs(calidata.ynoutput)) {
		calidata.yfoutput=calidata.ypoutput;
		calidata.ymoff=calidata.ypoff;
	} else {
		calidata.yfoutput=calidata.ynoutput;
		calidata.ymoff=calidata.ynoff; 
	}
	if (abs(calidata.zpoutput)<=abs(calidata.znoutput)) {
		calidata.zfoutput=calidata.zpoutput;
		calidata.zmoff=calidata.zpoff;
	} else {
		calidata.zfoutput=calidata.znoutput;
		calidata.zmoff=calidata.znoff; 
	}
	if (calidata.xtempf==0) {
		calidata.OffDataSpaceConvert = reverse_OffDataSpaceConvert;
		Write_Input(calidata.xaddoff, (*(calidata.OffDataSpaceConvert))(calidata.xmoff));
	}
	if (calidata.ytempf==0) { 
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.yaddoff, (*(calidata.OffDataSpaceConvert))(calidata.ymoff));
	}
	if (calidata.ztempf==0) { 
		calidata.OffDataSpaceConvert = forword_OffDataSpaceConvert;
		Write_Input(calidata.zaddoff, (*(calidata.OffDataSpaceConvert))(calidata.zmoff));
	}
	if ((calidata.xpoutput>0 && calidata.xnoutput>0)||(calidata.xpoutput<0 && calidata.xnoutput<0)) {
		calidata.xfinalf=1;
	}

	if((calidata.ypoutput>0 && calidata.ynoutput>0)||(calidata.ypoutput<0 && calidata.ynoutput<0)){
		calidata.yfinalf=1;
	}

	if((calidata.zpoutput>0 && calidata.znoutput>0)||(calidata.zpoutput<0 && calidata.znoutput<0)){
		calidata.zfinalf=1;
	}

	check_finalflag_set_tempflag(&calidata);
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif
	cyclecount=0;
	while(1){
		if(++cyclecount>20)break;

		if((calidata.xtempf)&&(calidata.ytempf)&&(calidata.ztempf))break;
		updata_midoff_address(&calidata);
		Read_Output_3axis(acc_buf);
		calidata.xfoutput=(signed int)((signed char)acc_buf[1])-x;
		calidata.yfoutput=(signed int)((signed char)acc_buf[3])-y;
		calidata.zfoutput=(signed int)((signed char)acc_buf[5])-z;
#ifdef PRINT
		printk("xp%4d=%4d,xm%4d=%4d,xn%4d=%4d,      yp%4d=%4d,ym%4d=%4d,yn%4d=%4d,      zp%4d=%4d,zm%4d=%4d,zn%4d=%4d\n\r",
				calidata.xpoutput,(unsigned int)calidata.xpoff,
				calidata.xfoutput,(unsigned int)calidata.xmoff,
				calidata.xnoutput,(unsigned int)calidata.xnoff,
				calidata.ypoutput,(unsigned int)calidata.ypoff,
				calidata.yfoutput,(unsigned int)calidata.ymoff,
				calidata.ynoutput,(unsigned int)calidata.ynoff,
				calidata.zpoutput,(unsigned int)calidata.zpoff,
				calidata.zfoutput,(unsigned int)calidata.zmoff,
				calidata.znoutput,(unsigned int)calidata.znoff
				);
#endif
		updata_moff_pnfoutput_set_tempflag(&calidata,acc_buf,x,y,z);
		check_output_set_finalflag(&calidata,2);
		if(check_flag_is_return(&calidata))return 1;
	}
#ifdef PRINT
	printk("L%4d:xff=%4d,xtf=%4d,yff=%4d,ytf=%4d,zff=%4d,ztf=%4d\n\r",__LINE__,
				(unsigned int)calidata.xfinalf,(unsigned int)calidata.xtempf,
				(unsigned int)calidata.yfinalf,(unsigned int)calidata.ytempf,
				(unsigned int)calidata.zfinalf,(unsigned int)calidata.ztempf
				);
#endif

	return 1;
}

static __maybe_unused int auto_calibration_instant_mtp(signed int x, signed int y, signed int z)
{
	unsigned char readbuf[3]={0};
	unsigned char buffer[6] = {0};

	signed int xoutp,youtp,zoutp;
	unsigned char xfinalf,yfinalf,zfinalf;
	int reg_13 = 0;

	xoutp=youtp=zoutp=0;
	xfinalf=yfinalf=zfinalf=0;    
	if (auto_calibration_instant(x,y,z) == 0)
         { 
		printk("auto_calibration_instant ==0 \n");
		sensor_write_reg(sc7a30_client, 0x1e,0x05);
		mdelay(100);
		sensor_write_reg(sc7a30_client, 0x13,0x01);

		sensor_write_reg(sc7a30_client, 0x1e,0x15);
		mdelay(300);
		return 0;
	}

	//msleep(20);
	tilt_3axis_mtp(x,y,z);
	Read_Output_3axis(buffer);
	xoutp=(signed int)((signed char)buffer[1])-x;
	youtp=(signed int)((signed char)buffer[3])-y;
	zoutp=(signed int)((signed char)buffer[5])-z;

	if(abs(xoutp) < 2){xfinalf=1;}
	if(abs(youtp) < 2){yfinalf=1;}
	if(abs(zoutp) < 2){zfinalf=1;}

	//*tbuffer = 0x10;
	//sensor_rx_data(sc7a30_client, tbuffer,1);
	readbuf[0]= Read_Reg(0x10);
	//*tbuffer = 0x40;
	//sensor_rx_data(sc7a30_client, tbuffer,1);
	readbuf[1]= Read_Reg(0x40);
	//*tbuffer = 0x47;
	//sensor_rx_data(sc7a30_client, tbuffer,1);
	readbuf[2]= Read_Reg(0x47);
	printk("L%4d:xtilt=%4d,xmis=%4d,xoff=%4d\n\r",__LINE__,
			(unsigned int)readbuf[0],
			(unsigned int)readbuf[1],
			(unsigned int)readbuf[2]
			);

	readbuf[0]= Read_Reg(0x11);
	readbuf[1]= Read_Reg(0x41);
	readbuf[2]= Read_Reg(0x48);
	printk("L%4d:ytilt=%4d,ymis=%4d,yoff=%4d\n\r",__LINE__,
			(unsigned int)readbuf[0],
			(unsigned int)readbuf[1],
			(unsigned int)readbuf[2]
			);
	readbuf[0]= Read_Reg(0x12);
	readbuf[1]= Read_Reg(0x42);
	readbuf[2]= Read_Reg(0x49);
	printk("L%4d:ztilt=%4d,zmis=%4d,zoff=%4d\n\r",__LINE__,
			(unsigned int)readbuf[0],
			(unsigned int)readbuf[1],
			(unsigned int)readbuf[2]
			);

	if(xfinalf && yfinalf && zfinalf)
	{
		sensor_write_reg(sc7a30_client,0x13,0x01);
		reg_13 = sensor_read_reg(sc7a30_client,0x13);
		printk("line %d  reg_13 = %x\n",__LINE__,reg_13);
		Write_Input(0x1e, 0x15);
		mdelay(300);
		printk(KERN_INFO "run calibration finished\n");

		return 1;
	} else {
		sensor_write_reg(sc7a30_client,0x13,0x01);//allen MTP
		reg_13 = sensor_read_reg(sc7a30_client,0x13);
		printk("line %d  reg_13 = %x\n",__LINE__,reg_13);
		Write_Input(0x1e, 0x15);
		mdelay(300);                                                                           
 
		return 0;
       }
}

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = 0x07;

	if (enable)
	{
		status = SC7A30_ENABLE;	//sc7a30
		sensor->ops->ctrl_data |= SC7A30_400HZ;
	} else
		status = ~SC7A30_ENABLE;	//sc7a30

	printk("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	
	if(result)
		printk("%s:fail to active sensor\n",__func__);
	
	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	//mutex_lock(&(sensor->allen_mutex) );//allen

	printk("aaaaa %s:line=%d\n",__func__,__LINE__);
	
	result = sensor->ops->active(client,0,0);
	if (result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	calibrated = 0;
	sc7a30_client = client;
	sensor->status_cur = SENSOR_OFF;
	//sensor->time_of_cali =0;//allen
	offset.x=offset.y=offset.z=0;
	sensor_write_reg(client, SC7A30_BOOT, 0x80);
	mdelay(20);
	result = sensor_write_reg(client, SC7A30_MODE, 0x07);

	sensor_write_reg(client, SC7A30_MODE3, 0x88);      
//	result = sensor_write_reg(client, SC7A30_MODE, 0x77);
        
	//register_test(); 
	if(result)
	{
		printk("aaaaa %s:line=%d,error\n",__func__,__LINE__);
		return result;
	} 

	if(sensor->pdata->irq_enable)	//open interrupt
	{
		result = sensor_write_reg(client, SC7A30_MODE2, 0x10);
		result = sensor_write_reg(client, 0x25, 0x02);

		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}
	}
	memset(&axis_average, 0, sizeof(struct sensor_axis_average));

	return result;
}

static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte ,s16 off)
{
    s64 result;
	result = (((s16)((high_byte << 8) + low_byte)) >>4);
	result -= off;
	result = result* SC7A30_GRAVITY_STEP;

	return (int)result;
}

static int gsensor_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	if (sensor->status_cur == SENSOR_ON) {
		/* Report acceleration sensor information */
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
		input_sync(sensor->input_dev);
		//printk("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
	}
	return 0;
}

#define GSENSOR_MIN  		2
static int sensor_report_value(struct i2c_client *client)
{

	struct sensor_private_data *sensor =
	(struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x,y,z;
	struct sensor_axis axis;
	char value = 0;
	//SC7A30_load_user_calibration(client);
	//printk("---------------in--------------------\n");
#if 0
	memset(buffer1, 0, 3);
	memset(buffer2, 0, 3);

	*buffer1 = SC7A30_STATUS;
	ret = sensor_rx_data(sc7a30_client, buffer1,1);
	buffer1[0] &= 0x08;
	if(!buffer1[0])
		return ret;

	*buffer1 = SC7A30_XOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer1,1);  
        *buffer2 = SC7A30_XOUT_H;
	ret = sensor_rx_data(client, buffer2,1);
	if (ret < 0)
		return ret;
	x = sensor_convert_data(sensor->client, buffer2[0], buffer1[0],0);	//buffer[1]:high bit 

	*buffer1 = SC7A30_YOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer1,1);
	*buffer2 = SC7A30_YOUT_H;
	ret = sensor_rx_data(client, buffer2,1);
	if (ret < 0)
		return ret;
	y = sensor_convert_data(sensor->client, buffer2[0], buffer1[0],0);
	*buffer1 = SC7A30_ZOUT_L;
	ret = sensor_rx_data(sc7a30_client, buffer1,1);
	*buffer2 = SC7A30_ZOUT_H;
	ret = sensor_rx_data(client, buffer2,1);
	if (ret < 0)
		return ret;
	z = sensor_convert_data(sensor->client, buffer2[0], buffer1[0],0);
#else
	char buffer[6] = {0};
	memset(buffer, 0, 6);

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	*buffer = SC7A30_XOUT_L | 0x80;
	ret = sensor_rx_data(sc7a30_client, buffer, 6);
	if (ret < 0) {
		printk("%s, %d, sensor rx data failed\n", __func__, __LINE__);
		return ret;
	}

	//this gsensor need 6 bytes buffer
	x = sensor_convert_data(sensor->client, buffer[1], buffer[0], 0);  //buffer[1]:high bit 
	y = sensor_convert_data(sensor->client, buffer[3], buffer[2], 0);
	z = sensor_convert_data(sensor->client, buffer[5], buffer[4], 0);
#endif

	axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
	axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z; 
	axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;

#if 0
	axis_average.x_average += axis.x;
	axis_average.y_average += axis.y;
	axis_average.z_average += axis.z;
	axis_average.count++;

	if(axis_average.count >= SC7A30_COUNT_AVERAGE)
	{
		axis.x = axis_average.x_average / axis_average.count;
		axis.y = axis_average.y_average / axis_average.count;
		axis.z = axis_average.z_average / axis_average.count;

		printk( "%s: axis = %d  %d  %d \n", __func__, axis.x, axis.y, axis.z);

		memset(&axis_average, 0, sizeof(struct sensor_axis_average));
		
		//Report event only while value is changed to save some power
		if((abs(sensor->axis.x - axis.x) > GSENSOR_MIN) || (abs(sensor->axis.y - axis.y) > GSENSOR_MIN) || (abs(sensor->axis.z - axis.z) > GSENSOR_MIN))
		{
			gsensor_report_value(client, &axis);

			/* \BB\A5\B3\E2\B5ػ\BA\B4\E6\CA\FD\BE\DD. */
			mutex_lock(&(sensor->data_mutex) );
			sensor->axis = axis;
			mutex_unlock(&(sensor->data_mutex) );
		}
	}
#else
	gsensor_report_value(client, &axis);

	/* \BB\A5\B3\E2\B5ػ\BA\B4\E6\CA\FD\BE\DD. */
	mutex_lock(&(sensor->data_mutex) );
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex) );
#endif
	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{

		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		printk("%s:sensor int status :0x%x\n",__func__,value);
	}
	return ret;
}

struct sensor_operate gsensor_sc7a30_ops = {
	.name				= "gs_sc7a30",
	.type				= SENSOR_TYPE_ACCEL,			//sensor type and it should be correct
	.id_i2c				= ACCEL_ID_SC7A30,			//i2c id number
	.read_reg			= SC7A30_XOUT_H,			//read data
	.read_len			= 1,					//data length
	.id_reg				= SENSOR_UNKNOW_DATA,			//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,			//device id
	.precision			= SC7A30_PRECISION,			//12 bit
	.ctrl_reg 			= SC7A30_MODE,			        //enable or disable SC7A30_MODE
	.int_status_reg 		= SENSOR_UNKNOW_DATA,			//intterupt status register
	.range				= {-SC7A30_RANGE,SC7A30_RANGE},	//range
	.trig				= IRQF_TRIGGER_HIGH|IRQF_ONESHOT,
	.active				= sensor_active,
	.init				= sensor_init,
	.report 			= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_sc7a30_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_sc7a30_ops);
}

static int gsensor_sc7a30_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_sc7a30_ops);
}

static const struct i2c_device_id gsensor_sc7a30_id[] = {
	{"gs_sc7a30", ACCEL_ID_SC7A30},
	{}
};

static struct i2c_driver gsensor_sc7a30_driver = {
	.probe = gsensor_sc7a30_probe,
	.remove = gsensor_sc7a30_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_sc7a30_id,
	.driver = {
		.name = "gsensor_sc7a30",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_sc7a30_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("sc7a30 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");
