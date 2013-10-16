#define __ICATCH7002_COMMON_CFILE__
#include "icatch7002_common.h"
#include <linux/proc_fs.h>
#include <mach/iomux.h>

#define CONFIG_SENSOR_I2C_SPEED 	300000		 /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED	0
#define CONFIG_SENSOR_I2C_RDWRCHK	0
#define SENSOR_NAME_STRING() sensor->dev_name


#define ICATCHFWNAME "icatch7002boot.bin"

u8 g_Calibration_Option_Def = 0xff;
bool IsZSL = FALSE;

#ifdef CALIBRATION_MODE_FUN
struct icatch_cali_fw_data {
	const char * const fname_option;
	struct firmware const*fw_option;
	const char * const fname_3acali;
	struct firmware const*fw_3acali;
	const char * const fname_lsc;
	struct firmware const*fw_lsc;
	const char * const fname_lscdq;
	struct firmware const*fw_lscdq;
};

struct icatch_cali_fw_data g_cali_fw_data_front = {
	.fname_option = "icatch7002/calibration_option.BIN",
	.fname_3acali = "icatch7002/3ACALI_F.BIN",
	.fname_lsc = "icatch7002/LSC_F.BIN",
	.fname_lscdq = "icatch7002/LSC_DQ_F.BIN",
};

struct icatch_cali_fw_data g_cali_fw_data_back = {
	.fname_option = "icatch7002/calibration_option.BIN",
	.fname_3acali = "icatch7002/3ACALI.BIN",
	.fname_lsc = "icatch7002/LSC.BIN",
	.fname_lscdq = "icatch7002/LSC_DQ.BIN",
};
#endif

#define ICATCH_BOOT_FROM_SPI 0
#define ICATCH_BOOT_FROM_HOST 1
#define ICATCH_BOOT ICATCH_BOOT_FROM_HOST
#define ASUS_CAMERA_SUPPORT 1

#ifndef FAIL
#define FAIL	0
#endif
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

#if 1

static struct rk_sensor_reg rs_reset_data[]={
	{0x1011,0x01},
	{0x001c,0x08},
	{0x001c,0x00},
	{0x1010,0x02},
	{0x1010,0x00},
	{0x1306,0x00},//0 rear,1 front
	{0x1011,0x00},
	SensorEnd
};

static struct rk_sensor_reg fs_reset_data[]={
	{0x1011,0x01},
	{0x001c,0x08},
	{0x001c,0x00},
	{0x1010,0x02},
	{0x1010,0x00},
	{0x1306,0x01},//0 rear,1 front
	{0x1011,0x00},
	SensorEnd
};

static struct rk_sensor_reg init_data[]={
	{SP7K_MSG_COLOR_EFFECT,		0x0}, // normal
	{SP7K_MSG_EV_COMPENSATION,	0x6}, // 0
	{SP7K_MSG_FLASH_MODE,		0x1}, // off
	{SP7K_MSG_FOCUS_MODE,		0x0}, // auto
	{SP7K_MSG_PV_SIZE,			0x0}, // 1280*960
	{SP7K_MSG_SCENE_MODE,		0x0}, // normal
	{SP7K_MSG_WHITE_BALANCE,	0x0}, // auto
	{SP7K_MSG_CAP_ISO,			0x0}, //auto
	{SP7K_MSG_AURA_COLOR_INDEX, 0x0}, // disable
	{SP7K_MSG_PV_CAP_MODE,		0x4}, // idle
	SensorEnd
};
#endif

struct i2c_client *g_icatch_i2c_client = NULL;
#if CONFIG_SENSOR_WhiteBalance
static int icatch_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	u8 set_val = 0;
    struct generic_sensor*sensor = to_generic_sensor(client);

	DEBUG_TRACE("%s: value = %d\n", __FUNCTION__, value);

	if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
	{
		switch(value){
			case 0: //enable auto
				set_val = 0x0;
				break;
			case 1: //incandescent		Tungsten
				set_val = 0x6;
				break;
			case 2: //fluorescent
				set_val = 0x5;
				break;
			case 3: //daylight
				set_val = 0x1;
				break;
			case 4: //cloudy-daylight
				set_val = 0x2;
				break;
			default:
				break;
		}
		//awb
		EXISP_I2C_WhiteBalanceSet(set_val);
		return 0;
	}
	DEBUG_TRACE("\n %s..%s valure = %d is invalidate..	\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
	return -1;
}
#endif

#if CONFIG_SENSOR_Effect
static int icatch_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value, int auravalue)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	u8 set_val = 0;
    struct generic_sensor*sensor = to_generic_sensor(client);
	DEBUG_TRACE("set effect,value = %d ......\n",value);
	if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
	{
		switch(value){
			case 0: //normal
			case 5: //none
				set_val = 0x00;
				break;
			case 1: //aqua
				set_val = 0x01;
				break;
			case 2: //negative
				set_val = 0x02;
				break;
			case 3: //sepia
				set_val = 0x03;
				break;
			case 4: //mono	Grayscale
				set_val = 0x04;
				break;
			case 6: //aura
				set_val = 0x06;
				break;
			case 7: //vintage
				set_val = 0x07;
				break;
			case 8: //vintage2
				set_val = 0x08;
				break;
			case 9: //lomo
				set_val = 0x09;
				break;
			case 10: //red
				set_val = 0x0A;
				break;
			case 11: //blue
				set_val = 0x0B;
				break;
			case 12: //green
				set_val = 0x0C;
				break;
			default:
				set_val = value;
				break;
		}
		EXISP_I2C_ColorEffectSet(set_val);
		if(set_val == 6){
			EXISP_I2C_AuraColorIndexSet(auravalue);
		}
		return 0;
	}
	DEBUG_TRACE("\n%s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
	return -1;
}
#endif

#if CONFIG_SENSOR_Scene
static int icatch_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	u8 set_val = 0;
    struct generic_sensor*sensor = to_generic_sensor(client);
//when scene mod is working , face deteciton and awb and iso are not recomemnded.
	if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
	{
		switch(value){
			case 0: //normal
				set_val = 0x00;
				break;
			case 1: //auto
				set_val = 0x00;
				break;
			case 2: //landscape
				set_val = 0x10;
				break;
			case 3: //night
				set_val = 0x07;
				break;
			case 4: //night_portrait
				set_val = 0x08;
				break;
			case 5: //snow
				set_val = 0x0B;
				break;
			case 6: //sports
				set_val = 0x0C;
				break;
			case 7: //candlelight
				set_val = 0x04;
				break;

			default:
				break;
		}

		EXISP_I2C_SceneModeSet(set_val);
		DEBUG_TRACE("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
		return 0;
	}
	DEBUG_TRACE("\n %s..%s valure = %d is invalidate..	\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
	return -EINVAL;
}
#endif


#if CONFIG_SENSOR_Exposure
static int sensor_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	u8 set_val = 0x0;
    struct generic_sensor*sensor = to_generic_sensor(client);
	if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
	{
		set_val = 6 - value;
		EXISP_I2C_EvSet(set_val);
		DEBUG_TRACE("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
		return 0;
	}
	DEBUG_TRACE("\n %s..%s valure = %d is invalidate..	\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
	return -EINVAL;
}
#endif

#if CONFIG_SENSOR_Mirror
static int sensor_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	return 0;
}
#endif

#if CONFIG_SENSOR_Flip
//off 0x00;mirror 0x01,flip 0x10;
static int sensor_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	return 0;

}
#endif

#if CONFIG_SENSOR_ISO
static int sensor_set_iso(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	u8 set_val = 0x0;
    struct generic_sensor*sensor = to_generic_sensor(client);
	if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
	{
		set_val = value;
		EXISP_I2C_ISOSet(set_val);
		DEBUG_TRACE("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
		return 0;
	}
	DEBUG_TRACE("\n %s..%s valure = %d is invalidate..	\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
	return -EINVAL;
}
#endif
void __dump_i2c(UINT16 addr_s, UINT16 addr_e);
static void sensor_interrupt_wait_clear()
{
	int loop_cnt = 0;
	
	printk("sensor_interrupt_wait_clear() Entry\n");
	while((icatch_sensor_read(SP7K_RDREG_INT_STS_REG_0) & 0x04) == 0){
		msleep(10);
		if(loop_cnt++ == 100){
			__dump_i2c(0x7200,0x727F);
			__dump_i2c(0x7072,0x7075);
			__dump_i2c(0x72F8,0x72F8);
			__dump_i2c(0x7005,0x7006);
		}
	}
	icatch_sensor_write(SP7K_RDREG_INT_STS_REG_0,(icatch_sensor_read(SP7K_RDREG_INT_STS_REG_0)|0x04));
	printk("sensor_interrupt_wait_clear() Exit\n");
}

#if CONFIG_SENSOR_Wdr
// EXISP_I2C_IspFuncSet(), bit 0 : DWDR
static void sensor_set_wdr(bool Enable)
{
	if(Enable){
		EXISP_I2C_IspFuncSet( icatch_sensor_read(SP7K_REG_BASE|(SP7K_MSG_ISP_FUNCTION&0x7F)) | 0x01);
	}else{
		EXISP_I2C_IspFuncSet( icatch_sensor_read(SP7K_REG_BASE|(SP7K_MSG_ISP_FUNCTION&0x7F)) &~0x01);
	}
}
#endif

#if CONFIG_SENSOR_EDGE
static void sensor_set_edge(bool Enable)
{
	if(Enable){
		EXISP_I2C_IspFuncSet( icatch_sensor_read(SP7K_REG_BASE|(SP7K_MSG_ISP_FUNCTION&0x7F)) | 0x02);
	}else{
		EXISP_I2C_IspFuncSet( icatch_sensor_read(SP7K_REG_BASE|(SP7K_MSG_ISP_FUNCTION&0x7F)) &~0x02);
	}
}
#endif


static int icatch_sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	return 0;
}
static int icatch_sensor_resume(struct soc_camera_device *icd)
{
	return 0;

}
static int icatch_sensor_set_bus_param(struct soc_camera_device *icd,
								unsigned long flags)
{

	return 0;
}


#if CALIBRATION_MODE_FUN
struct proc_dir_entry *g_icatch7002_proc_entry = NULL;
#define PROC_ENTRY_NAME	SENSOR_NAME_STRING()
int g_is_calibrationMode = 0;
char g_procbuff[1024];
union icatch_fw_version {
	__u32 fw;
	struct {
		__u8 b1;
		__u8 b2;
		__u8 b3;
		__u8 b4;
	};
} g_fw_version;
int icatch_proc_read(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len = 0;
	if (off > 0) {
		*eof = 1;
		return 0;
	}

	len = sprintf(page, "fw %02x.%02x.%02x\nis_calibration %d\n",
			g_fw_version.b3, g_fw_version.b2, g_fw_version.b1,
			g_is_calibrationMode);
	return len;
}

int icatch_proc_write(struct file *file, const char __user *buffer,
			   unsigned long count, void *data)
{
	char *ptr = NULL;
	if (count >= sizeof(g_procbuff)) {
		DEBUG_TRACE("%s no space\n", __FUNCTION__);
		return -ENOSPC;
	}

	if (copy_from_user(g_procbuff, buffer, count)) {
		DEBUG_TRACE("%s copy from user fail %d\n", __FUNCTION__, count);
		return -EFAULT;
	}
	g_procbuff[count] = 0;

	if ( (ptr = strstr(g_procbuff, "is_calibration")) == NULL) {
		goto l_ret;
	}

	ptr += strlen("is_calibration");
	while(*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') {
		ptr++;
	}

	switch (*ptr) {
	case '0':
		g_is_calibrationMode = 0;
		DEBUG_TRACE("%s disable calibration mode\n", __FUNCTION__);
		break;
	case '1':
		g_is_calibrationMode = 1;
		DEBUG_TRACE("%s enable calibration mode\n", __FUNCTION__);
		break;
	}

l_ret:
	return count;
}

void icatch_create_proc_entry()
{
    struct generic_sensor*sensor = to_generic_sensor(g_icatch_i2c_client);

	if (g_icatch7002_proc_entry == NULL) {
		DEBUG_TRACE("%s need create_proc_entry\n", __FUNCTION__);
		g_fw_version.fw = 0;
		g_icatch7002_proc_entry = create_proc_entry(PROC_ENTRY_NAME, O_RDWR, NULL);
		if (g_icatch7002_proc_entry) {
			memset(g_procbuff, 0, sizeof(g_procbuff));
			g_icatch7002_proc_entry->read_proc = icatch_proc_read;
			g_icatch7002_proc_entry->write_proc = icatch_proc_write;
		} else {
			DEBUG_TRACE("%s create_proc_entry fail\n", __FUNCTION__);
		}
	}
}

void icatch_remove_proc_entry()
{
    struct generic_sensor*sensor = to_generic_sensor(g_icatch_i2c_client);
	if (g_icatch7002_proc_entry != NULL) {
		remove_proc_entry(PROC_ENTRY_NAME, NULL);
		g_icatch7002_proc_entry = NULL;
	}
}
#endif

#if 1//CONFIG_SENSOR_Focus

void __dump_i2c(UINT16 addr_s, UINT16 addr_e) {
	int size = (addr_e - addr_s + 1);
	int i = 0;
	int soffset = addr_s%16;
	char buf[100] = {0};
	char lbuf[12];

	for (i = 0; i < soffset; i++) {
		if (i == 0) {
			sprintf(lbuf, "%08X:", addr_s / 16 * 16);
			strcat(buf, lbuf);
		}
		sprintf(lbuf, "   ");
		strcat(buf, lbuf);
	}

	size += soffset;
	i = soffset;
	while( i < size) {
		if ((i%16 == 0) && (i != 0)) {
			printk("%s\n", buf);
		}
		if (i%16 == 0) {
			buf[0] = 0;
			sprintf(lbuf, "%08X:", (addr_s + i - soffset) / 16 * 16);
			strcat(buf, lbuf);
		}
		sprintf(lbuf, " %02X", icatch_sensor_read(addr_s + i - soffset));
		strcat(buf, lbuf);
		i++;
	}

	printk("%s\n", buf);
}

static void icatch_mapping_zone(const char * tag, struct rk_sensor_focus_zone *dst, const int *rect)
{
	DEBUG_TRACE("%s: %s set from (%d, %d), (%d, %d)\n",
			__FUNCTION__, tag,
			dst->lx, dst->ty,
			dst->rx, dst->dy);

	dst->lx = ((rect[0] + 1000) * 1024)/2001;
	dst->ty = ((rect[1] + 1000) * 1024)/2001;
	dst->rx = ((rect[2] + 1000) * 1024)/2001;
	dst->dy = ((rect[3] + 1000) * 1024)/2001;

	DEBUG_TRACE("%s: %s to (%d, %d), (%d, %d)\n",
			__FUNCTION__, tag,
			dst->lx, dst->ty,
			dst->rx, dst->dy);
}

//value: disable metering area
//force_set: if not set, it will not drive the sensor
//			to set metering area really if focus is
//			supported.
//rect: set to NULL if don't want to change.
static int icatch_set_metering_zone(
		struct specific_sensor *sensor,
		__s32 value,
		rk_sensor_tae_zone zone,
		int force_set)
{
	int ret = 0, w = 0, iret = 0;

	if ( sensor->isp_priv_info.MeteringAreas != value) {
		DEBUG_TRACE("%s set metering areas to %d\n",
					__FUNCTION__,
					value);
	}

	if (value == 0) {
		if (sensor->isp_priv_info.MeteringAreas > 0) {
			if ( EXISP_TAFTAEROISet(
					TAFTAE_TAE_OFF,
					0,0,0,0,0,0 ) != SUCCESS)
			{
				ret = -EINVAL;
			}
		}
		sensor->isp_priv_info.MeteringAreas = value;
		return ret;
	}

	sensor->isp_priv_info.MeteringAreas = value;

	if (force_set) {
		goto f_set;
	}

	if (sensor_find_ctrl(sensor->common_sensor.ctrls, V4L2_CID_FOCUS_AUTO) ||
		sensor_find_ctrl(sensor->common_sensor.ctrls, V4L2_CID_FOCUS_CONTINUOUS) ||
		sensor_find_ctrl(sensor->common_sensor.ctrls, V4L2_CID_FOCUS_ABSOLUTE) ) {
			return ret;
	}

f_set:
	//set TAE if focus is not supported

	w = sensor->isp_priv_info.tae_zone.rx - sensor->isp_priv_info.tae_zone.lx;
	if (w < 0) {
		w = -w;
	}

	DEBUG_TRACE("%s: set TAE zone to (%d, %d), (%d, %d)\n",
			__FUNCTION__,
			sensor->isp_priv_info.tae_zone.lx,
			sensor->isp_priv_info.tae_zone.ty,
			sensor->isp_priv_info.tae_zone.rx,
			sensor->isp_priv_info.tae_zone.dy);

	if (w == 0) {
		iret = EXISP_TAFTAEROISet(TAFTAE_TAE_ONLY, 0, 0, 0, 0x80, 0x1bf, 0x1bf);
	} else {
		iret = EXISP_TAFTAEROISet(
				TAFTAE_TAE_ONLY,
				0, 0, 0,
				w,
				sensor->isp_priv_info.tae_zone.lx,
				sensor->isp_priv_info.tae_zone.ty);
	}

	if (iret != SUCCESS) {
		ret = -EINVAL;
	}

	return ret;
}

int icatch_sensor_set_auto_focus(struct i2c_client *client, int value,int *tmp_zone)
{
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);

	//u8 zone_x = 0x0,zone_y = 0x0; // 0->0x0f
	UINT16 w = 0;
	int ret = 0;
	int cnt = 100;
	int set_val;

	ret = icatch_set_metering_zone(
			sensor,
			sensor->isp_priv_info.MeteringAreas,
			sensor->isp_priv_info.tae_zone,
			1);
	if (ret) {
		return ret;
	}

    if(value == WqCmd_af_single)
        set_val = 0;
    else if(value == WqCmd_af_update_zone){
        icatch_mapping_zone(
        		"focus zone",
        		&sensor->isp_priv_info.focus_zone,
        		tmp_zone);
        return 0;
    
    }else if(value == WqCmd_af_continues)
        set_val = 3;
    else{
           DEBUG_TRACE("%s:focus value is invalidate!\n",__func__);
    }
    
	EXISP_I2C_FocusModeSet(set_val);

	//EXISP_I2C_AFROITriggerSet();
	if (value != WqCmd_af_far_pos) {
		//EXISP_I2C_ROISwitchSet(01);
		//set the zone
		DEBUG_TRACE("%s: lx = %d,rx = %d,ty = %d,dy = %d\n", __FUNCTION__, sensor->isp_priv_info.focus_zone.lx,sensor->isp_priv_info.focus_zone.rx,sensor->isp_priv_info.focus_zone.ty,sensor->isp_priv_info.focus_zone.dy);
		w = sensor->isp_priv_info.focus_zone.rx - sensor->isp_priv_info.focus_zone.lx;
		//zone_x = (sensor->isp_priv_info.focus_zone.lx << 4) | (sensor->isp_priv_info.focus_zone.rx & 0x0f);
		//zone_y = (sensor->isp_priv_info.focus_zone.ty << 4) | (sensor->isp_priv_info.focus_zone.dy & 0x0f);
		//auto focus
		//sendI2cCmd(client, 0x0E, 0x00);
		if( w != 0) {
			EXISP_TAFTAEROISet(
					TAFTAE_TAF_ONLY,
					w,
					sensor->isp_priv_info.focus_zone.lx,
					sensor->isp_priv_info.focus_zone.ty,
					0, 0, 0);
			DEBUG_TRACE("%s:auto focus, val = %d, w = 0x%x, x = 0x%x y = 0x%x\n",__func__,value, w, sensor->isp_priv_info.focus_zone.lx, sensor->isp_priv_info.focus_zone.ty);
		}else{
			EXISP_TAFTAEROISet(TAFTAE_TAF_ONLY, 0x80, 0x1bf, 0x1bf, 0, 0, 0);
			DEBUG_TRACE("%s:auto focus, all zero, val = %d, size=0x80, x=0x1bf, y=0x1bf\n",__func__,value);
		}
	}

    int staus_value = 0;
    if(value == WqCmd_af_continues)
        //staus_value = 0x10;
        goto icatch_sensor_set_auto_focus_end;
    else if(value == WqCmd_af_single)
        staus_value = 0x0;
        
        
	while (cnt--) {
		if (EXISP_I2C_AFStatusGet() == staus_value) {
			 break;
		}
		msleep(30);
	}

	//__dump_i2c(0x7200, 0x727f);

	if (cnt <= 0) {
		DEBUG_TRACE("%s: focus timeout %d\n",__func__, value);
		__dump_i2c(0x7200, 0x727f);

		__dump_i2c(0x7005, 0x7006);
		return 1;
	}

	if (EXISP_I2C_AFResultGet() != 0) {
		DEBUG_TRACE("%s: focus fail %d\n",__func__, value);
		return 1;
	}
icatch_sensor_set_auto_focus_end:
	DEBUG_TRACE("%s: focus success %d\n\n",__func__, value);
	return 0;
}


#endif

int icatch_sensor_init(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;
	struct soc_camera_link *icl = to_soc_camera_link(icd);
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
  //  struct isp_data * sensor = spnsor->isp_priv_info;
	struct v4l2_queryctrl* qctrl;
	int pid = 0;
	u8 sensorid = 0;
	char* sensorid_str = NULL;
	struct sensor_v4l2ctrl_info_s* qctrl_info;

	sensorid_str = strstr(dev_name(icd->pdev),"front");
	if(sensorid_str)
	    sensorid = 1;
	 else
	    sensorid = 0;

#if CALIBRATION_MODE_FUN
	if (g_is_calibrationMode == 1 ) {
		if (sensorid == SENSOR_ID_REAR)
			sensorid = SENSOR_ID_REAR_CALIBRATION;

		DEBUG_TRACE("%s CALIBRATION MODE is enable, SENSOR_ID:%d\n", __FUNCTION__, sensorid);
	}
	sensor->isp_priv_info.sensor_id = sensorid;
#endif

	DEBUG_TRACE("@@NY@@%s: %d\n", __FUNCTION__, sensorid);
	sensor->isp_priv_info.curRes = -1;
	sensor->isp_priv_info.curPreviewCapMode = IDLE_MODE;
	sensor->isp_priv_info.had_setprvsize = 0;
	sensor->isp_priv_info.hdr = false;

#if CALIBRATION_MODE_FUN
	sensor->isp_priv_info.rk_query_PreviewCapMode = IDLE_MODE;
#endif
	g_icatch_i2c_client = client;


#if 0
		//get id check
{
	int count = 200;
		while(count--){
		pid = icatch_sensor_read(0x0004);
		DEBUG_TRACE("\n %s	pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
		mdelay(1000);
		}
}
#endif

	if(icatch_load_fw(icd,sensorid)<0){
		DEBUG_TRACE("icatch7002 load sensor %d firmware failed!!-->%s:%d\n",sensorid, __FUNCTION__, __LINE__);
		return -ENODEV;
	}
#if 0
	if(sensorid == SENSOR_ID_FRONT){
		// front camera
		icatch_sensor_write_array((void*)fs_reset_data);
	}
	else{
		// rear camera
		icatch_sensor_write_array((void*)rs_reset_data);
	}

	//50Hz
	EXISP_I2C_BandSelectionSet(0x01);
	//DEBUG_TRACE("%s Set BandSelection to 50Hz\n", __FUNCTION__);

	//get id check
	mdelay(100);
	pid = EXISP_I2C_RearSensorIdGet();
	DEBUG_TRACE("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
	if (pid != SENSOR_ID) {
		DEBUG_TRACE("error: %s mismatched pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
		return -ENODEV;
	}
#endif

	//50Hz
	EXISP_I2C_BandSelectionSet(0x01);
	/* sensor sensor information for initialization  */
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_DO_WHITE_BALANCE);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.whiteBalance = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_BRIGHTNESS);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.brightness = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_EFFECT);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.effect = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_EXPOSURE);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl){
		sensor->isp_priv_info.exposure = qctrl->default_value;
		}


	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_SATURATION);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.saturation = qctrl->default_value;

		
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_CONTRAST);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.contrast = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_HFLIP);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.mirror = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_VFLIP);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.flip = qctrl->default_value;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_SCENE);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl){
		sensor->isp_priv_info.scene = qctrl->default_value;
		}
#if	CONFIG_SENSOR_AntiBanding
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_ANTIBANDING);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
        sensor->isp_priv_info.antibanding = qctrl->default_value;
#endif
#if CONFIG_SENSOR_WhiteBalanceLock
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_WHITEBALANCE_LOCK);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
        sensor->isp_priv_info.WhiteBalanceLock = qctrl->default_value;
#endif
#if CONFIG_SENSOR_ExposureLock
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_EXPOSURE_LOCK);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
        sensor->isp_priv_info.ExposureLock = qctrl->default_value;
#endif
#if CONFIG_SENSOR_MeteringAreas
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_METERING_AREAS);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
        sensor->isp_priv_info.MeteringAreas = qctrl->default_value;
#endif
#if CONFIG_SENSOR_Wdr
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_WDR);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
        sensor->isp_priv_info.Wdr = qctrl->default_value;
#endif
#if CONFIG_SENSOR_EDGE
	sensor_set_edge(1);
#endif
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_ZOOM_ABSOLUTE);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.digitalzoom = qctrl->default_value;
	/* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
	//qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_AUTO);
#if 0
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_FOCUS_AUTO);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.auto_focus = SENSOR_AF_AUTO;

	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_FACEDETECT);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.face = qctrl->default_value;
#endif
#if CONFIG_SENSOR_ISO
	qctrl_info =sensor_find_ctrl(sgensor->ctrls, V4L2_CID_ISO);
	if(qctrl_info)
	qctrl = qctrl_info->qctrl;
	if (qctrl)
		sensor->isp_priv_info.iso = qctrl->default_value;
#endif
	return 0;
}



/* sensor register read */
static int icatch_sensor_burst_read( u16 reg, u16 len, __u8 *buffer)
{
    struct generic_sensor*sensor = to_generic_sensor(g_icatch_i2c_client);

	int err,cnt;
	u8 buf[2];
	struct i2c_msg msg[2];
	struct i2c_client* client =g_icatch_i2c_client;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);
	msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;		 /* ddl@rock-chips.com : 100kHz */
	msg[0].read_type = 2;	/* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	msg[1].addr = client->addr;
	msg[1].flags = client->flags|I2C_M_RD;
	msg[1].buf = buffer;
	msg[1].len = len;
	msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;						 /* ddl@rock-chips.com : 100kHz */
	msg[1].read_type = 2;							  /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	cnt = 3;
	err = -EAGAIN;
	while ((cnt-- > 0) && (err < 0)) {						 /* ddl@rock-chips.com :  Transfer again if transent is failed	 */
		err = i2c_transfer(client->adapter, &msg[0], 1);

		if (err >= 0) {
			break;
		} else {
			DEBUG_TRACE("\n %s write reg(0x%x, val:0x%x) failed, try to write again!\n",SENSOR_NAME_STRING(),reg, buf[0]);
			udelay(10);
		}
	}

	if(err <0)
		return -FAIL;

	cnt = 3;
	err = -EAGAIN;
	while ((cnt-- > 0) && (err < 0)) {						 /* ddl@rock-chips.com :  Transfer again if transent is failed	 */
		err = i2c_transfer(client->adapter, &msg[1], 1);

		if (err >= 0) {
			break;
		} else {
			DEBUG_TRACE("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, buf[0]);
			udelay(10);
		}
	}
	if(err >=0)
		return 0;
	else
		return -FAIL;
}

struct icatch_dumpreg_dsc {
	__u16 addr;
	__u16 len;
	unsigned long user_ptr;
};

static int icatch_dump_regs(void __user * arg)
{
	int err, ret = 0;
	struct icatch_dumpreg_dsc dsc;
	__u8 *buf = NULL;

	err = !access_ok(VERIFY_READ,
			(void __user *)arg, sizeof(struct icatch_dumpreg_dsc));

	if (err) {
		DEBUG_TRACE("%s(%d) error to access argument\n",
			__FUNCTION__, __LINE__);
		return -EACCES;
	}

	if (copy_from_user(
		&dsc, arg,
		sizeof(struct icatch_dumpreg_dsc))) {
		return -EFAULT;
	}

	if (dsc.len == 0) {
		DEBUG_TRACE("%s length is 0\n", __FUNCTION__);
		return -EINVAL;
	}

	DEBUG_TRACE("%s add=0x%04x len=%d\n",
			__FUNCTION__,
			dsc.addr, dsc.len);

	if ( !access_ok(VERIFY_WRITE,
		(void __user *)dsc.user_ptr, dsc.len)) {
		DEBUG_TRACE("%s(%d) error to access user_ptr\n",
			__FUNCTION__, __LINE__);
		return -EACCES;
	}

	buf = kmalloc( dsc.len, GFP_KERNEL);
	if (buf == NULL) {
		return -ENOMEM;
	}

	//dump
	ret = icatch_sensor_burst_read(dsc.addr, dsc.len, buf);
	if (ret < 0) {
		DEBUG_TRACE("%s(%d) error read these regs\n",
			__FUNCTION__, __LINE__);
		goto l_exit;
	}


	if (copy_to_user(dsc.user_ptr, buf, dsc.len)) {
		DEBUG_TRACE("%s(%d) fail to copy to user_ptr\n",
			__FUNCTION__, __LINE__);
		ret = -EFAULT;
		goto l_exit;
	}

l_exit:
	if (buf != NULL)
		kfree(buf);
	DEBUG_TRACE("%s(%d) return value %d\n",
			__FUNCTION__, __LINE__,
			ret);
	return ret;
}

static int icatch_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl,struct v4l2_queryctrl *qctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	int ret = 0;

	switch (ext_ctrl->id)
	{
		case V4L2_CID_SCENE:
			{
				ext_ctrl->value = sensor->isp_priv_info.scene;
				break;
			}
#if CONFIG_SENSOR_AntiBanding
        case V4L2_CID_ANTIBANDING:
            {
                ext_ctrl->value = sensor->isp_priv_info.antibanding;
                break;
            }
#endif
#if CONFIG_SENSOR_WhiteBalanceLock
        case V4L2_CID_WHITEBALANCE_LOCK:
            {
                ext_ctrl->value = sensor->isp_priv_info.WhiteBalanceLock;
                break;
            }
#endif
#if CONFIG_SENSOR_ExposureLock
		case V4L2_CID_EXPOSURE_LOCK:
            {
                ext_ctrl->value = sensor->isp_priv_info.ExposureLock;
                break;
            }
#endif
#if CONFIG_SENSOR_MeteringAreas
		case V4L2_CID_METERING_AREAS:
            {
                ext_ctrl->value = sensor->isp_priv_info.MeteringAreas;
                break;
            }
#endif
#if CONFIG_SENSOR_Wdr
		case V4L2_CID_WDR:
			{
				ext_ctrl->value = sensor->isp_priv_info.Wdr;
				break;
			}
#endif
#if CONFIG_SENSOR_EDGE
		case V4L2_CID_EDGE:
			{
				ext_ctrl->value = EXISP_I2C_CapEdgeInfoGet();
				break;
			}
#endif
		case V4L2_CID_EFFECT:
			{
				ext_ctrl->value = sensor->isp_priv_info.effect;
				break;
			}
		case V4L2_CID_ZOOM_ABSOLUTE:
			{
				ext_ctrl->value = sensor->isp_priv_info.digitalzoom;
				break;
			}
		case V4L2_CID_ZOOM_RELATIVE:
			{
				return -EINVAL;
			}
		case V4L2_CID_FOCUS_ABSOLUTE:
			{
				return -EINVAL;
			}
		case V4L2_CID_FOCUS_RELATIVE:
			{
				return -EINVAL;
			}
		case V4L2_CID_FLASH:
			{
				ext_ctrl->value = sensor->isp_priv_info.flash;
				break;
			}
		case V4L2_CID_FACEDETECT:
			{
				ext_ctrl->value =sensor->isp_priv_info.face ;
				break;
			}
#if CONFIG_SENSOR_ISO
		case V4L2_CID_ISO:
		{
			ext_ctrl->value = sensor->isp_priv_info.iso;
			if(ext_ctrl->value == 0){
				ext_ctrl->value = icatch_sensor_read(SP7K_RDREG_ISO_H);
				ext_ctrl->value <<= 8;
				ext_ctrl->value |= icatch_sensor_read(SP7K_RDREG_ISO_L);
			}
			break;
		}
#endif
#if CONFIG_SENSOR_JPEG_EXIF
		case V4L2_CID_JPEG_EXIF:
		{
			RkExifInfo *pExitInfo = (RkExifInfo *)ext_ctrl->value;

			UINT8 ucExpTimeNumerator;
			UINT32 ulExpTimeDenominator;
			UINT8 ucExpTimeCompensation;
			UINT16 usLensFocalLength;
			UINT16 usIsoInfo;
			UINT8 ucFlashInfo;

			EXISP_ExifInfoGet(
				&ucExpTimeNumerator,
				&ulExpTimeDenominator,
				&ucExpTimeCompensation,
				&usLensFocalLength,
				&usIsoInfo,
				&ucFlashInfo);

			pExitInfo->ExposureTime.num = ucExpTimeNumerator;
			pExitInfo->ExposureTime.denom = ulExpTimeDenominator;
			//pExitInfo->Flash = ucFlashInfo;
			pExitInfo->ISOSpeedRatings = usIsoInfo;
			pExitInfo->FocalPlaneYResolution.num = EXISP_I2C_CapEdgeInfoGet();
			pExitInfo->ExposureBiasValue.num = sensor->isp_priv_info.exposure ;
			pExitInfo->ExposureBiasValue.denom = 3;

			break;
		}
#endif

#if CONFIG_SENSOR_DUMPREGS
		case V4L2_CID_DUMPREGS:
		{
			//static long icatch_dump_regs(void __user * arg);
			return icatch_dump_regs(ext_ctrl->string);
		}
#endif
		default :
		    ret = -1;
			break;
	}
	return ret;
}


static int icatch_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl,struct v4l2_queryctrl *qctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	int val_offset;
	int ret = 0;

	val_offset = 0;
	switch (ext_ctrl->id)
	{
#if CONFIG_SENSOR_Scene
		case V4L2_CID_SCENE:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.scene)
				{
					if (icatch_set_scene(icd, qctrl,ext_ctrl->value) != 0)
						return -EINVAL;
					sensor->isp_priv_info.scene = ext_ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_AntiBanding
		case V4L2_CID_ANTIBANDING:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.antibanding)
				{
					if(ext_ctrl->value){
						EXISP_I2C_BandSelectionSet(2);	//60Hz
					}else{
						EXISP_I2C_BandSelectionSet(1);	//50Hz
					}
					sensor->isp_priv_info.antibanding = ext_ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_WhiteBalanceLock
		case V4L2_CID_WHITEBALANCE_LOCK:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.WhiteBalanceLock)
				{
					if(ext_ctrl->value){
						EXISP_I2C_VendreqCmdSet(4);
					}else{
						EXISP_I2C_VendreqCmdSet(6);
					}
					sensor->isp_priv_info.WhiteBalanceLock = ext_ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_ExposureLock
		case V4L2_CID_EXPOSURE_LOCK:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.ExposureLock)
				{
					if(ext_ctrl->value){
						EXISP_I2C_VendreqCmdSet(3);
					}else{
						EXISP_I2C_VendreqCmdSet(5);
					}
					sensor->isp_priv_info.ExposureLock = ext_ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_MeteringAreas
		case V4L2_CID_METERING_AREAS:
			{
				if (ext_ctrl->value) {
					icatch_mapping_zone(
						"metering zone",
						&sensor->isp_priv_info.tae_zone,
						ext_ctrl->rect);
				}
				ret = icatch_set_metering_zone(
							sensor,
							ext_ctrl->value,
							sensor->isp_priv_info.tae_zone,
							0);
				sensor->isp_priv_info.MeteringAreas = ext_ctrl->value;
				break;
			}
#endif
#if CONFIG_SENSOR_Wdr
		case V4L2_CID_WDR:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.Wdr)
				{
					sensor_set_wdr(ext_ctrl->value);
					sensor->isp_priv_info.Wdr = ext_ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Effect
		case V4L2_CID_EFFECT:
			{
				if (ext_ctrl->value != sensor->isp_priv_info.effect)
				{
					if (icatch_set_effect(icd, qctrl,ext_ctrl->value,ext_ctrl->rect[0]) != 0)
						return -EINVAL;
					sensor->isp_priv_info.effect= ext_ctrl->value;
				}
				if(sensor->isp_priv_info.effect == 6)
				{
					EXISP_I2C_AuraColorIndexSet(ext_ctrl->rect[0]);
				}
				break;
			}
#endif
#if CONFIG_SENSOR_DigitalZoom
		case V4L2_CID_ZOOM_ABSOLUTE:
			{
				DEBUG_TRACE("V4L2_CID_ZOOM_ABSOLUTE ...... ext_ctrl->value = %d\n",ext_ctrl->value);
				if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum)){
					return -EINVAL;
				}

				if (ext_ctrl->value != sensor->isp_priv_info.digitalzoom)
				{
					val_offset = ext_ctrl->value -sensor->isp_priv_info.digitalzoom;

					if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
						return -EINVAL;
					sensor->isp_priv_info.digitalzoom += val_offset;

					DEBUG_TRACE("%s digitalzoom is %x\n",sgensor->dev_name,  sensor->isp_priv_info.digitalzoom);
				}

				break;
			}
		case V4L2_CID_ZOOM_RELATIVE:
			{
				if (ext_ctrl->value)
				{
					if (sensor_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
						return -EINVAL;
					sensor->isp_priv_info.digitalzoom += ext_ctrl->value;

					DEBUG_TRACE("%s digitalzoom is %x\n", sgensor->dev_name, sensor->isp_priv_info.digitalzoom);
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Flash
		case V4L2_CID_FLASH:
			{
				if (sensor_set_flash(icd, qctrl,ext_ctrl->value) != 0)
					return -EINVAL;
				sensor->info_priv.flash = ext_ctrl->value;

				DEBUG_TRACE("%s flash is %x\n",sgensor->dev_name, sensor->isp_priv_info.flash);
				break;
			}
#endif
#if CONFIG_SENSOR_FACE_DETECT
	case V4L2_CID_FACEDETECT:
		{
			if(sensor->isp_priv_info.face != ext_ctrl->value){
				if (sensor_set_face_detect(client, ext_ctrl->value) != 0)
					return -EINVAL;
				sensor->isp_priv_info.face = ext_ctrl->value;
				DEBUG_TRACE("%s face value is %x\n",sgensor->dev_name, sensor->isp_priv_info.face);
				}
			break;
		}
#endif
#if CONFIG_SENSOR_ISO
	case V4L2_CID_ISO:
	{
		if (sensor->isp_priv_info.iso != ext_ctrl->value) {
			if (sensor_set_iso(icd, qctrl, ext_ctrl->value) != 0) {
				return -EINVAL;
			}
			sensor->isp_priv_info.iso = ext_ctrl->value;
			DEBUG_TRACE("%s set ISO to %d\n", sgensor->dev_name, sensor->isp_priv_info.iso);
		}
		break;
	}
#endif
		default:
		    ret = -1;
			break;
	}

	return ret;
}

static int icatch_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl,struct v4l2_queryctrl *qctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
    int ret  = 0;


	switch (ctrl->id)
	{
		case V4L2_CID_BRIGHTNESS:
			{
				ctrl->value = sensor->isp_priv_info.brightness;
				break;
			}
		case V4L2_CID_SATURATION:
			{
				ctrl->value = sensor->isp_priv_info.saturation;
				break;
			}
		case V4L2_CID_CONTRAST:
			{
				ctrl->value = sensor->isp_priv_info.contrast;
				break;
			}
		case V4L2_CID_DO_WHITE_BALANCE:
			{
				ctrl->value = sensor->isp_priv_info.whiteBalance;
				break;
			}
		case V4L2_CID_EXPOSURE:
			{
				ctrl->value = sensor->isp_priv_info.exposure;
				break;
			}
		case V4L2_CID_HFLIP:
			{
				ctrl->value = sensor->isp_priv_info.mirror;
				break;
			}
		case V4L2_CID_VFLIP:
			{
				ctrl->value = sensor->isp_priv_info.flip;
				break;
			}
		case V4L2_CID_ZOOM_ABSOLUTE:
			{
				ctrl->value = sensor->isp_priv_info.digitalzoom;
				break;
			}
		default :
		        ret = -1;
				break;
	}
	return ret;
}

static int icatch_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl,struct v4l2_queryctrl *qctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	struct soc_camera_device *icd = client->dev.platform_data;
    int ret  = 0;

	switch (ctrl->id)
	{
#if CONFIG_SENSOR_Brightness
		case V4L2_CID_BRIGHTNESS:
			{
				if (ctrl->value != sensor->isp_priv_info.brightness)
				{
					if (sensor_set_brightness(icd, qctrl,ctrl->value) != 0)
					{
						return -EINVAL;
					}
					sensor->isp_priv_info.brightness = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Exposure
		case V4L2_CID_EXPOSURE:
			{
				if (ctrl->value != sensor->isp_priv_info.exposure)
				{
					if (sensor_set_exposure(icd, qctrl,ctrl->value) != 0)
					{
						return -EINVAL;
					}
					sensor->isp_priv_info.exposure = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Saturation
		case V4L2_CID_SATURATION:
			{
				if (ctrl->value != sensor->isp_priv_info.saturation)
				{
					if (sensor_set_saturation(icd, qctrl,ctrl->value) != 0)
					{
						return -EINVAL;
					}
					sensor->isp_priv_info.saturation = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Contrast
		case V4L2_CID_CONTRAST:
			{
				if (ctrl->value != sensor->isp_priv_info.contrast)
				{
					if (sensor_set_contrast(icd, qctrl,ctrl->value) != 0)
					{
						return -EINVAL;
					}
					sensor->isp_priv_info.contrast = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_WhiteBalance
		case V4L2_CID_DO_WHITE_BALANCE:
			{
				if (ctrl->value != sensor->isp_priv_info.whiteBalance)
				{
					if (icatch_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
					{
						return -EINVAL;
					}
					sensor->isp_priv_info.whiteBalance = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Mirror
		case V4L2_CID_HFLIP:
			{
				if (ctrl->value != sensor->isp_priv_info.mirror)
				{
					if (sensor_set_mirror(icd, qctrl,ctrl->value) != 0)
						return -EINVAL;
					sensor->isp_priv_info.mirror = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Flip
		case V4L2_CID_VFLIP:
			{
				if (ctrl->value != sensor->isp_priv_info.flip)
				{
					if (sensor_set_flip(icd, qctrl,ctrl->value) != 0)
						return -EINVAL;
					sensor->isp_priv_info.flip = ctrl->value;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_DigitalZoom
		case V4L2_CID_ZOOM_ABSOLUTE:
			{
				int val_offset = 0;
				DEBUG_TRACE("V4L2_CID_ZOOM_ABSOLUTE ...... ctrl->value = %d\n",ctrl->value);
				if ((ctrl->value < qctrl->minimum) || (ctrl->value > qctrl->maximum)){
					return -EINVAL;
					}

				if (ctrl->value != sensor->isp_priv_info.digitalzoom)
				{
					val_offset = ctrl->value -sensor->isp_priv_info.digitalzoom;

					if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
						return -EINVAL;
					sensor->isp_priv_info.digitalzoom += val_offset;

					DEBUG_TRACE("%s digitalzoom is %x\n",sgensor->dev_name,  sensor->isp_priv_info.digitalzoom);
				}

				break;
			}
		case V4L2_CID_ZOOM_RELATIVE:
			{
				if (ctrl->value)
				{
					if (sensor_set_digitalzoom(icd, qctrl,&ctrl->value) != 0)
						return -EINVAL;
					sensor->isp_priv_info.digitalzoom += ctrl->value;

					DEBUG_TRACE("%s digitalzoom is %x\n", sgensor->dev_name, sensor->isp_priv_info.digitalzoom);
				}
				break;
			}
#endif
		default:
		    ret = -1;
			break;
	}

	return ret;
}

int sensor_set_get_control_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl,bool is_set)
{
    int ret = 0;
    struct v4l2_control ctrl;
    
    if(is_set){
    // belong to ext_control ?
        ret = icatch_s_ext_control(icd, ext_ctrl,ctrl_info->qctrl);
        if(ret == 0){
	        ctrl_info->cur_value = ext_ctrl->value;
	        goto sensor_set_get_control_cb_end;
	        }
    // belong to control?
        ctrl.id = ext_ctrl->id;
        ctrl.value = ext_ctrl->value;
	    ret = icatch_s_control(soc_camera_to_subdev(icd), &ctrl,ctrl_info->qctrl);
        if(ret == 0){
	        ctrl_info->cur_value = ctrl.value;
	        goto sensor_set_get_control_cb_end;
	        }

    }else{
    // belong to ext_control ?
        ret = icatch_g_ext_control(icd, ext_ctrl,ctrl_info->qctrl);

        if(ret == 0){
	        ctrl_info->cur_value = ext_ctrl->value;
	        goto sensor_set_get_control_cb_end;
	        }
    // belong to control?
        ctrl.id = ext_ctrl->id;
	    ret = icatch_g_control(soc_camera_to_subdev(icd), &ctrl,ctrl_info->qctrl);
        if(ret == 0){
	        ctrl_info->cur_value = ctrl.value;
	        goto sensor_set_get_control_cb_end;
	        }

    }

sensor_set_get_control_cb_end:   
    return ret;
    

}
static int sensor_hdr_exposure(struct i2c_client *client, unsigned int code)
{
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	//printk("sensor_hdr_exposure_cb: %d %d\n",code,sensor->isp_priv_info.exposure);
	switch (code)
	{
		case RK_VIDEOBUF_HDR_EXPOSURE_MINUS_1:
		{
			//if( (sensor->isp_priv_info.exposure - 1 >= -6) && (sensor->isp_priv_info.exposure - 1 <= 6) ){
			//	EXISP_I2C_EvSet(6 - sensor->isp_priv_info.exposure - 1);
			//}
			printk("RK_VIDEOBUF_HDR_EXPOSURE_MINUS_1\n");
			if(IsZSL){
				printk("EXISP_ImageCapSet(IMAGE_CAP_HDR)\n");
				EXISP_ImageCapSet(IMAGE_CAP_HDR);
				sensor_interrupt_wait_clear();
			}
			break;
		}

		case RK_VIDEOBUF_HDR_EXPOSURE_NORMAL:
		{
			//EXISP_I2C_EvSet(6 - sensor->isp_priv_info.exposure);
			sensor->isp_priv_info.hdr = true;
			break;
		}

		case RK_VIDEOBUF_HDR_EXPOSURE_PLUS_1:
		{
			//if( (sensor->isp_priv_info.exposure + 1 >= -6) && (sensor->isp_priv_info.exposure + 1 <= 6) ){
			//	EXISP_I2C_EvSet(6 - sensor->isp_priv_info.exposure + 1);
			//}
			break;
		}

		case RK_VIDEOBUF_HDR_EXPOSURE_FINISH:
		{
			printk("HDR_EXPOSURE_FINISH\n");
			sensor->isp_priv_info.hdr = FALSE;
			//EXISP_I2C_EvSet(6 - sensor->isp_priv_info.exposure);
			if(IsZSL){
				u8 res_sel = 0;
				if(sensor->isp_priv_info.curRes == OUTPUT_QSXGA){
					res_sel = 0xA;
				}else if(sensor->isp_priv_info.curRes == OUTPUT_QUADVGA){
					res_sel = 0;
				}
				printk("HDR_EXPOSURE_FINISH Switch resolution\n");
				EXISP_PvSizeSet(res_sel);
				sensor_interrupt_wait_clear();
			}
			break;
		}
		default:
			break;
	}
	
	return 0;
}

 long icatch_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	switch (cmd){
		case RK29_CAM_SUBDEV_HDR_EXPOSURE:
		{
			sensor_hdr_exposure(client,(unsigned int)arg);
			break;
		}
		
		default:
		{
			//SENSOR_TR("%s %s cmd(0x%x) is unknown !\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}

	return 0;
}



 int icatch_s_fmt(struct i2c_client *client, struct v4l2_mbus_framefmt *mf,bool is_capture)
{
	 const struct sensor_datafmt *fmt;
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	 //const struct v4l2_queryctrl *qctrl;
	 //struct soc_camera_device *icd = client->dev.platform_data;
	 int ret=0;
	 int set_w,set_h;
	 int supported_size = sensor->isp_priv_info.outputSize;
	 int res_set = 0;
	 u8 preview_cap_mode = 0;//0 is preview mode
	 set_w = mf->width;
	 set_h = mf->height;
	 if (((set_w <= 176) && (set_h <= 144)) && (supported_size & OUTPUT_QCIF))
	 {
		 set_w = 176;
		 set_h = 144;
		 res_set = OUTPUT_QCIF;
	 }
	 else if (((set_w <= 320) && (set_h <= 240)) && (supported_size & OUTPUT_QVGA))
	 {
		 set_w = 320;
		 set_h = 240;
		 res_set = OUTPUT_QVGA;

	 }
	 else if (((set_w <= 352) && (set_h<= 288)) && (supported_size & OUTPUT_CIF))
	 {
		 set_w = 352;
		 set_h = 288;
		 res_set = OUTPUT_CIF;

	 }
	 else if (((set_w <= 640) && (set_h <= 480)) && (supported_size & OUTPUT_VGA))
	 {
		 set_w = 640;
		 set_h = 480;
		 res_set = OUTPUT_VGA;

	 }
	 else if (((set_w <= 800) && (set_h <= 600)) && (supported_size & OUTPUT_SVGA))
	 {
		 set_w = 800;
		 set_h = 600;
		 res_set = OUTPUT_SVGA;

	 }
	 else if (((set_w <= 1024) && (set_h <= 768)) && (supported_size & OUTPUT_XGA))
	 {
		 set_w = 1024;
		 set_h = 768;
		 res_set = OUTPUT_XGA;

	 }
	 else if (((set_w <= 1280) && (set_h <= 720)) && (supported_size & OUTPUT_720P))
	 {
		 set_w = 1280;
		 set_h = 720;
		 res_set = OUTPUT_720P;
	 }

	 else if (((set_w <= 1280) && (set_h <= 960)) && (supported_size & OUTPUT_QUADVGA))
	 {
		 set_w = 1280;
		 set_h = 960;
		 res_set = OUTPUT_QUADVGA;
	 }
	 else if (((set_w <= 1280) && (set_h <= 1024)) && (supported_size & OUTPUT_XGA))
	 {
		 set_w = 1280;
		 set_h = 1024;
		 res_set = OUTPUT_XGA;
	 }
	 else if (((set_w <= 1600) && (set_h <= 1200)) && (supported_size & OUTPUT_UXGA))
	 {
		 set_w = 1600;
		 set_h = 1200;
		 res_set = OUTPUT_UXGA;
	 }
	 else if (((set_w <= 1920) && (set_h <= 1080)) && (supported_size & OUTPUT_1080P))
	 {
		 set_w = 1920;
		 set_h = 1080;
		 res_set = OUTPUT_1080P;
	 }
	 else if (((set_w <= 2048) && (set_h <= 1536)) && (supported_size & OUTPUT_QXGA))
	 {
		 set_w = 2048;
		 set_h = 1536;
		 res_set = OUTPUT_QXGA;
	 }
	 else if (((set_w <= 2592) && (set_h <= 1944)) && (supported_size & OUTPUT_QSXGA))
	 {
		 set_w = 2592;
		 set_h = 1944;
		 res_set = OUTPUT_QSXGA;
	 }
	 else if (((set_w <= 3264) && (set_h <= 2448)) && (supported_size & OUTPUT_QUXGA))
	 {
		 set_w = 3264;
		 set_h = 2448;
		 res_set = OUTPUT_QUXGA;
	 }
	 else
	 {
		 set_w = 1280;
		 set_h = 960;
		 res_set = OUTPUT_QUADVGA;
	 }

	 //if(res_set != sensor->isp_priv_info.curRes)
	 //  sensor_set_isp_output_res(client,res_set);
	 //res will be setted
	 sensor->isp_priv_info.curRes = res_set;
	 if(is_capture)
	    sensor->isp_priv_info.curPreviewCapMode = CAPTURE_MODE;
     else
	    sensor->isp_priv_info.curPreviewCapMode = PREVIEW_MODE;

	 mf->width = set_w;
	 mf->height = set_h;
	 //enter capture or preview mode
	 //EXISP_I2C_PvCapModeSet(preview_cap_mode);
	 DEBUG_TRACE("%s:setw = %d,seth = %d\n",__func__,set_w,set_h);

 sensor_s_fmt_end:
	 return ret;
 }

int icatch_enum_framesizes(struct v4l2_subdev *sd, struct v4l2_frmsizeenum *fsize)
{
	int err = 0,i=0,num = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);

	printk("icatch_enum_framesizes fsize->reserved[0](%x) fsize->reserved[1](%x)\n",fsize->reserved[0],fsize->reserved[1]);
	if( (fsize->reserved[1] & 0xff) == 0x01){
		IsZSL = TRUE;
	}else{
		IsZSL = FALSE;
	}

	//get supported framesize num
	
	if (fsize->index >= sensor->isp_priv_info.supportedSizeNum) {
		err = -1;
		goto end;
	}

	if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QCIF))
	{
		
		fsize->discrete.width = 176;
		fsize->discrete.height = 144;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QVGA))
	{
		fsize->discrete.width = 320;
		fsize->discrete.height = 240;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_CIF) )
	{
	
		fsize->discrete.width = 352;
		fsize->discrete.height = 288;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_VGA))
	{
	
		fsize->discrete.width = 640;
		fsize->discrete.height = 480;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_SVGA))
	{
	
		fsize->discrete.width = 800;
		fsize->discrete.height = 600;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_XGA))
	{
	
		fsize->discrete.width = 1024;
		fsize->discrete.height = 768;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_720P))
	{
	
		fsize->discrete.width = 1280;
		fsize->discrete.height = 720;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QUADVGA))
	{
		fsize->discrete.width = 1280;
		fsize->discrete.height = 960;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_XGA))
	{
		fsize->discrete.width = 1280;
		fsize->discrete.height = 1024;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_UXGA) )
	{
		fsize->discrete.width = 1600;
		fsize->discrete.height = 1200;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_1080P))
	{
		fsize->discrete.width = 1920;
		fsize->discrete.height = 1080;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QXGA))
	{
		fsize->discrete.width = 2048;
		fsize->discrete.height = 1536;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QSXGA))
	{
	
		fsize->discrete.width = 2592;
		fsize->discrete.height = 1944;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	}
	else if ((sensor->isp_priv_info.supportedSize[fsize->index] & OUTPUT_QUXGA))
	{
	
		fsize->discrete.width = 3264;
		fsize->discrete.height = 2448;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	} else {
		err = -1;
	}

	
	
end:
	return err;
}


/*-------------------------------------------------------------------------
 *	Function Name : EXISP_I2C_PvSizeSet
 *	ucParam:
	 0x00	1280x960
	 0x01	3264x2448
	 0x02	1920x1080
	 0x03	320x240(reserved)
	 0x04	1280x720
	 0x05	1040x780
	 0x06	2080x1560
	 0x07	3648x2736
	 0x08	4160x3120
	 0x09	3360x1890
	 0x0A	2592x1944
	 0x0B	640x480
	 0x0C	1408x1408
	 0x0D	1920x1088
 *	Return : None
 *------------------------------------------------------------------------*/

static int icatch_set_isp_output_res(struct i2c_client *client,enum ISP_OUTPUT_RES outputSize){
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
#if 1	
	u8 res_sel = 0;
	switch(outputSize) {
	case OUTPUT_QCIF:
	case OUTPUT_HQVGA:
	case OUTPUT_QVGA:
	case OUTPUT_CIF:
	case OUTPUT_VGA:
	case OUTPUT_SVGA:
	case OUTPUT_720P:
	case OUTPUT_QUADVGA:
		res_sel = 0;
		break;
	case OUTPUT_XGA:
	case OUTPUT_SXGA:
	case OUTPUT_UXGA:
	case OUTPUT_1080P:
	    res_sel = 0x02;
	case OUTPUT_QXGA:
	case OUTPUT_QSXGA:
		res_sel = 0x0A;// non-zsl single
		break;
	case OUTPUT_QUXGA:
		res_sel = 0x01;// non-zsl single
		break;
	default:
		DEBUG_TRACE("%s %s  isp not support this resolution!\n",sgensor->dev_name,__FUNCTION__);
		break;
	}
#endif
    int cnt = 16;
	//preview mode set
	if((sensor->isp_priv_info.curPreviewCapMode == CAPTURE_MODE)
	    /*(outputSize == OUTPUT_QSXGA) || (outputSize == OUTPUT_QSXGA)*/){
	    //in capture mode , isp output full size if size have not been set.
		if(sensor->isp_priv_info.hdr == FALSE){
			if(IsZSL){
					printk("IsZSL EXISP_PvSizeSet(0x0A)\n");
					    EXISP_PvSizeSet(res_sel);
					//polling until AE ready
					while (((EXISP_I2C_3AStatusGet() & 0x1) == 0) && (cnt -- > 0)) {
						DEBUG_TRACE("%s %s  polling AE ready\n",sgensor->dev_name,__FUNCTION__);
						mdelay(50);
					}
					sensor_interrupt_wait_clear();
			}else{
				printk("Is not ZSL HdrEn FALSE EXISP_ImageCapSet(IMAGE_CAP_NONZSL_SINGLE)\n");
				icatch_sensor_write(SP7K_RDREG_INT_STS_REG_0,icatch_sensor_read(SP7K_RDREG_INT_STS_REG_0));
				EXISP_ImageCapSet(IMAGE_CAP_NONZSL_SINGLE);
				sensor_interrupt_wait_clear();
			}
		}else{
			printk("HdrEn EXISP_ImageCapSet(IMAGE_CAP_HDR)\n");
			EXISP_ImageCapSet(IMAGE_CAP_HDR);
			sensor_interrupt_wait_clear();
		}
		//sensor->isp_priv_info.curPreviewCapMode = CAPTURE_NONE_ZSL_MODE;
	}
	else{
		EXISP_PvSizeSet(res_sel);
		//polling until AE ready
		while (((EXISP_I2C_3AStatusGet() & 0x1) == 0) && (cnt -- > 0)) {
			DEBUG_TRACE("%s %s  polling AE ready\n",sgensor->dev_name,__FUNCTION__);
			mdelay(50);
		}
		sensor_interrupt_wait_clear();
		//sensor->isp_priv_info.curPreviewCapMode = PREVIEW_MODE;
#if 1
		DEBUG_TRACE("\n %s  pid = 0x%x\n", sgensor->dev_name, EXISP_I2C_VendorIdGet);
		DEBUG_TRACE("fw version is 0x%x\n ",EXISP_I2C_FWVersionGet());

		DEBUG_TRACE("front id= 0x%x,rear id = 0x%x\n ",EXISP_I2C_FrontSensorIdGet(),EXISP_I2C_RearSensorIdGet());
#endif
	}
	return 0;
}

 int icatch_s_stream(struct v4l2_subdev *sd, int enable){
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_device *icd = client->dev.platform_data;
    struct generic_sensor*sgensor = to_generic_sensor(client);
    struct specific_sensor*sensor = to_specific_sensor(sgensor);
	DEBUG_TRACE("%s:enable = %d\n",__func__,enable);
	DEBUG_TRACE("@@NY@@%s: client %0x\n", __FUNCTION__, client);
	if(enable == 0){
		//   sensor_set_face_detect(client,0);
		//  sensor_af_workqueue_set(icd,0,0,true);
#if CALIBRATION_MODE_FUN
		if (g_is_calibrationMode)
			EXISP_I2C_PvStreamSet(0); // stream off
#endif
		sensor->isp_priv_info.curPreviewCapMode = IDLE_MODE;
	}else{
#if CALIBRATION_MODE_FUN
		if (g_is_calibrationMode &&
		    sensor->isp_priv_info.sensor_id ==  SENSOR_ID_REAR_CALIBRATION &&
		    sensor->isp_priv_info.rk_query_PreviewCapMode == PREVIEW_MODE) {
			EXISP_PvSizeSet(0x0A);
			sensor->isp_priv_info.curPreviewCapMode = PREVIEW_MODE;
			EXISP_I2C_PvStreamSet(1); // stream on
			return 0;
		}
#endif
		printk(KERN_ERR"icatch_set_isp_output_res: curRes(%x)\n",sensor->isp_priv_info.curRes);
		//sensor_set_face_detect(client,1);
		icatch_set_isp_output_res(client, sensor->isp_priv_info.curRes);


#if CALIBRATION_MODE_FUN
		if (g_is_calibrationMode)
			EXISP_I2C_PvStreamSet(1); // stream on
#endif
	}

	 return 0;
}


int icatch_request_firmware(const struct firmware ** fw){
	int ret = 0;
	if( request_firmware(fw, ICATCHFWNAME, &g_icatch_i2c_client->dev) !=0){
		DEBUG_TRACE("%s:%d, request firmware erro,please check firmware!\n");
		ret = -1;
	}else{
		ret = 0;
	}
	return ret;
}

void icatch_release_firmware(const struct firmware * fw){
	if(fw)
		release_firmware(fw);
}

void icatch_sensor_power_ctr(struct soc_camera_device *icd ,int on,int power_mode){
#if 0
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	struct rk29camera_platform_data* pdata = (struct rk29camera_platform_data*)(to_soc_camera_host(icd->dev.parent)->v4l2_dev.dev->platform_data);
	if(!on){
		//power down
		if(icl->power)
			icl->power(icd->pdev,0);
		if(pdata && pdata->sensor_mclk)
			pdata->sensor_mclk(RK29_CAM_PLATFORM_DEV_ID,0,24*1000*1000);
		if(icl->powerdown)
			icl->powerdown(icd->pdev,0);
		iomux_set(GPIO1_A4);
		iomux_set(GPIO1_A5) ;
		iomux_set(GPIO1_A6);
		iomux_set(GPIO1_A7);
		iomux_set(GPIO3_B6);
		iomux_set(GPIO3_B7);
		iomux_set(GPIO0_C0);
		gpio_set_value(RK30_PIN1_PA4,0);
		gpio_set_value(RK30_PIN1_PA5,0);
		gpio_set_value(RK30_PIN1_PA6,0);	// for clk 24M
		gpio_set_value(RK30_PIN1_PA7,0);
		gpio_set_value(RK30_PIN3_PB6,0);
		gpio_set_value(RK30_PIN3_PB7,0);
		gpio_set_value(RK30_PIN0_PC0,0);
		//msleep(500);
	}else{
		//power ON
		gpio_set_value(RK30_PIN1_PA6,1);	// for clk 24M
		
		if(icl->power)
			icl->power(icd->pdev,1);
		if(icl->powerdown){
			if(power_mode == 0)//from spi
				icl->powerdown(icd->pdev,1);
			else
				icl->powerdown(icd->pdev,0);
			}
		if(pdata && pdata->sensor_mclk)
			pdata->sensor_mclk(RK29_CAM_PLATFORM_DEV_ID,1,24*1000*1000);
			//reset , reset pin low ,then high
		if (icl->reset)
			icl->reset(icd->pdev);
		if(power_mode == 0)//from spi
			icl->powerdown(icd->pdev,0);
		iomux_set(SPI0_CLK);
		iomux_set(SPI0_RXD);
		iomux_set(SPI0_TXD);
		iomux_set(SPI0_CS0);
		iomux_set(I2C3_SDA);
		iomux_set(I2C3_SCL);
		msleep(100);
	}
	#endif
}

#if CALIBRATION_MODE_FUN
#if 0
void __dump(const u8 *data, size_t size) {
	size_t i = 0;
	char buf[100] = {0};
	char lbuf[12];

	while( i < size) {
		if ((i%16 == 0) && (i != 0)) {
			DEBUG_TRACE("%s\n", buf);
		}
		if (i%16 == 0) {
			buf[0] = 0;
			sprintf(lbuf, "%08X:", i);
			strcat(buf, lbuf);
		}
		sprintf(lbuf, " %02X", *(data + i));
		strcat(buf, lbuf);
		i++;
	}

	DEBUG_TRACE("%s\n", buf);
}
#endif

struct icatch_cali_fw_data * icatch_load_cali_fw_data(u8 sensorid) {

	if (g_is_calibrationMode) {
		return NULL;
	}

	struct icatch_cali_fw_data * fw = NULL;
	switch (sensorid) {
	case SENSOR_ID_FRONT:
		fw = &g_cali_fw_data_front;
		break;
	case SENSOR_ID_REAR:
		fw = &g_cali_fw_data_back;
		break;
	default:
		break;
	}
	if (fw == NULL)
		return NULL;

	if (request_firmware(
		&(fw->fw_option),
		fw->fname_option,
		&g_icatch_i2c_client->dev) != 0) {
		DEBUG_TRACE("%s: load calibration fw data: %s fail", __FUNCTION__, fw->fname_option);
		fw->fw_option = NULL;
	}

	if (request_firmware(
		&(fw->fw_3acali),
		fw->fname_3acali,
		&g_icatch_i2c_client->dev) != 0) {
		DEBUG_TRACE("%s: load calibration fw data: %s fail", __FUNCTION__, fw->fname_3acali);
		fw->fw_3acali = NULL;
	}
#if 0
	else {
		DEBUG_TRACE("%s: dump %s (size: %d)\n", __FUNCTION__, fw->fname_3acali, fw->fw_3acali->size);
		__dump(fw->fw_3acali->data, fw->fw_3acali->size);
	}
#endif

	if (request_firmware(
		&(fw->fw_lsc),
		fw->fname_lsc,
		&g_icatch_i2c_client->dev) != 0) {
		DEBUG_TRACE("%s: load calibration fw data: %s fail", __FUNCTION__, fw->fname_lsc);
		fw->fw_lsc = NULL;
	}
#if 0
	else {
		DEBUG_TRACE("%s: dump %s (size: %d)\n", __FUNCTION__, fw->fname_lsc, fw->fw_lsc->size);
		__dump(fw->fw_lsc->data, fw->fw_lsc->size);
	}
#endif

	if (request_firmware(
		&(fw->fw_lscdq),
		fw->fname_lscdq,
		&g_icatch_i2c_client->dev) != 0) {
		DEBUG_TRACE("%s: load calibration fw data: %s fail", __FUNCTION__, fw->fname_lscdq);
		fw->fw_lscdq = NULL;
	}
#if 0
	else {
		DEBUG_TRACE("%s: dump %s (size: %d)\n", __FUNCTION__, fw->fname_lscdq, fw->fw_lscdq->size);
		__dump(fw->fw_lscdq->data, fw->fw_lscdq->size);
	}
#endif

	return fw;
}

void icatch_free_cali_fw_data(struct icatch_cali_fw_data * data) {
	if (data == NULL)
		return ;

	if (data->fw_option != NULL) {
		release_firmware(data->fw_option);
	}

	if (data->fw_3acali != NULL) {
		release_firmware(data->fw_3acali);
	}

	if (data->fw_lsc != NULL) {
		release_firmware(data->fw_lsc);
	}

	if (data->fw_lscdq != NULL) {
		release_firmware(data->fw_lscdq);
	}
}
#endif

//#include "BOOT_OV5693_126MHz(075529).c"
 int icatch_load_fw(struct soc_camera_device *icd,u8 sensorid){
	 struct firmware *fw =NULL;
#if CALIBRATION_MODE_FUN
	 struct icatch_cali_fw_data * cali_data = NULL;
#endif
	 int ret = 0;
	 icatch_sensor_power_ctr(icd,0,ICATCH_BOOT);
	 icatch_sensor_power_ctr(icd,1,ICATCH_BOOT);
	 if(ICATCH_BOOT == ICATCH_BOOT_FROM_HOST){
		 DEBUG_TRACE("@@NY@@%s: %d\n", __FUNCTION__, sensorid);
		 if(icatch_request_firmware(&fw)!= 0){
			 ret = -1;
			 goto icatch_load_fw_out;
		 }

#if CALIBRATION_MODE_FUN
		 g_fw_version.fw = *((__u32*)(fw->data + fw->size - 4));

		 cali_data = icatch_load_cali_fw_data(sensorid);
		 if (cali_data != NULL) {
			 DEBUG_TRACE("%s:%d,load calibration fw data success !!!!\n",__func__,__LINE__);
			 ret = EXISP_LoadCodeStart(
					ICATCH_BOOT,
					sensorid,
					g_is_calibrationMode,
					(u8*)(fw->data),
					cali_data->fw_option ? (u8*)(cali_data->fw_option->data) : &g_Calibration_Option_Def,
					cali_data->fw_3acali ? (u8*)(cali_data->fw_3acali->data) : NULL,
					cali_data->fw_lsc ? (u8*)(cali_data->fw_lsc->data) : NULL,
					cali_data->fw_lscdq ? (u8*)(cali_data->fw_lscdq->data) : NULL);
		 } else {
			 DEBUG_TRACE("%s:%d,load calibration fw data fail !!!!\n",__func__,__LINE__);
			 ret = EXISP_LoadCodeStart(
					ICATCH_BOOT,
					sensorid,
					g_is_calibrationMode,
					(u8*)(fw->data),
					&g_Calibration_Option_Def,
					NULL,
					NULL,
					NULL);
		 }
#else
		 ret = EXISP_LoadCodeStart(
				ICATCH_BOOT,
				sensorid,
				0,
				(u8*)(fw->data),
				&g_Calibration_Option_Def,
				NULL,
				NULL,
				NULL);
#endif

		 if (ret != SUCCESS) {
			 DEBUG_TRACE("%s:%d,load firmware failed !!!!\n",__func__,__LINE__);
			 ret = -1;
		 } else {
			 ret = 0;
		 }

		 icatch_release_firmware(fw);
		 icatch_free_cali_fw_data(cali_data);

		 if(ret < 0)
			 goto icatch_load_fw_out;

	 }else{
	#if 1
		 BB_WrSPIFlash(0xffffff);
		 
		 icatch_sensor_power_ctr(icd,0,0);
		 icatch_sensor_power_ctr(icd,1,0);
		 gpio_set_value(RK30_PIN1_PA6,0);	// for clk 24M
	#endif
	}
	// msleep(100);
	 return 0;
icatch_load_fw_out:
	return ret;
}
 

int icatch_get_rearid_by_lowlevelmode(struct soc_camera_device *icd,UINT16 *rear_id)
{
	static int ret = 0;
	static int done = 0;
	static UINT16 _rear_id = 0;

	static const struct rk_sensor_reg reset_1[]={
		{0x1011, 0x01},	/* CPU suspend */
		{0x0084, 0x14},  /* To sensor clock divider */
		{0x0034, 0xFF},  /* Turn on all clock */
		{0x9032, 0x00},
		{0x9033, 0x10},
		{0x9030, 0x3f},
		{0x9031, 0x04},
		{0x9034, 0xf2},
		{0x9035, 0x04},
		{0x9032, 0x10},
		{0x00,  0x00},
	};

	//tmrUsWait(10000); /*10ms*/

	static const struct rk_sensor_reg reset_2[] = {
		{0x9032, 0x30},
		{0x00,  0x00},
	};

	//tmrUsWait(10000); /*10ms*/


	static const struct rk_sensor_reg reset_3[] = {
		/*End - Power on sensor & enable clock */
		{0x9008, 0x00},
		{0x9009, 0x00},
		{0x900A, 0x00},
		{0x900B, 0x00},

		/*Start - I2C Read*/
		{0x923C, 0x01},  /* Sub address enable */
		{0x9238, 0x30},  /* Sub address enable */
		{0x9240, 0x6C},  /* Slave address      */
		{0x9200, 0x03},  /* Read mode          */
		{0x9210, 0x00},  /* Register addr MSB  */
		{0x9212, 0x00},  /* Register addr LSB  */
		{0x9204, 0x01},  /* Trigger I2C read   */
		{0x00,  0x00},
	};

	//	tmrUsWait(2000);/*2ms*/

	DEBUG_TRACE("%s: entry!\n", __FUNCTION__);

	if (done == 1) {
		if (rear_id != NULL)
			*rear_id = _rear_id;
		return ret;
	}

	icatch_sensor_power_ctr(icd,0,ICATCH_BOOT);
	icatch_sensor_power_ctr(icd,1,ICATCH_BOOT);

	if (icatch_sensor_write_array(reset_1) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}
	msleep(10);

	if (icatch_sensor_write_array(reset_2) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(10);
	if (icatch_sensor_write_array(reset_3) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(2);
	_rear_id = (UINT16)icatch_sensor_read(0x9211) << 8;
	_rear_id += icatch_sensor_read(0x9213);
	DEBUG_TRACE("%s: rear_id = 0x%04X\n", __FUNCTION__, _rear_id);
	*rear_id = _rear_id;


l_ret:
	icatch_sensor_power_ctr(icd,0,ICATCH_BOOT);
	done = 1;
	return ret;
}


int icatch_get_frontid_by_lowlevelmode(struct soc_camera_device *icd,UINT16 *front_id)
{
	static int ret = 0;
	static int done = 0;
	static UINT16 _front_id = 0;

	static const struct rk_sensor_reg reset_1[]={
		{ 0x1011, 0x01,0x0,0x0},  /* CPU Suspend */

		{ 0x0084, 0x14},  /* To sensor clock divider */
		{ 0x0034, 0xFF},  /* Turn on all clock */
		{ 0x9030, 0x3f},
		{ 0x9031, 0x04},
		{ 0x9034, 0xf2},
		{ 0x9035, 0x04},
		{ 0x9033, 0x04},
		{ 0x9032, 0x3c},
    	SensorEnd   
	};

	//tmrUsWait(10000); /* 10ms */

	static const struct rk_sensor_reg reset_2[] = {
		{0x9033, 0x00},
    	SensorEnd
	};

	//tmrUsWait(10000); /*10ms*/


	static const struct rk_sensor_reg reset_3[] = {
		{ 0x9033, 0x04},
		{ 0x9032, 0x3e},
    	SensorEnd
	};

	//tmrUsWait(10000); /* 10ms */

	static const struct rk_sensor_reg reset_4[] = {
		{ 0x9032, 0x3c},
		/*End - Power on sensor & enable clock */

		/*Start - I2C Read ID*/
		{ 0x9138, 0x30},  /* Sub address enable */
		{ 0x9140, 0x90},  /* Slave address      */
		{ 0x9100, 0x03},  /* Read mode          */
		{ 0x9110, 0x00},  /* Register addr MSB  */
		{ 0x9112, 0x00}, /* Register addr LSB  */
		{ 0x9104, 0x01 }, /* Trigger I2C read   */
    	SensorEnd
	};

	//tmrUsWait(100);   /* 0.1ms */

	static const struct rk_sensor_reg reset_5[] = {
		{ 0x9110, 0x00},  /* Register addr MSB  */
		{ 0x9112, 0x01},  /* Register addr LSB  */
		{ 0x9104, 0x01},  /* Trigger I2C read   */
    	SensorEnd
	};

	DEBUG_TRACE("%s: entry!\n", __FUNCTION__);

	if (done == 1) {
		if (front_id != NULL)
			*front_id = _front_id;
		return ret;
	}

	icatch_sensor_power_ctr(icd,0,ICATCH_BOOT);
	icatch_sensor_power_ctr(icd,1,ICATCH_BOOT);

	if (icatch_sensor_write_array(reset_1) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}
	msleep(10);

	if (icatch_sensor_write_array(reset_2) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(10);
	if (icatch_sensor_write_array(reset_3) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(10);


	if (icatch_sensor_write_array(reset_4) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(1);
	_front_id = (UINT16)icatch_sensor_read(0x9111) << 8;

	if (icatch_sensor_write_array(reset_5) != 0) {
		ret = -ENXIO;
		goto l_ret;
	}

	msleep(1);
	_front_id += icatch_sensor_read(0x9111);
	DEBUG_TRACE("%s: front_id = 0x%04X\n", __FUNCTION__, _front_id);
	*front_id = _front_id;

l_ret:
	icatch_sensor_power_ctr(icd,0,ICATCH_BOOT);
	done = 1;
	return ret;
}

