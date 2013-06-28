#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include "generic_sensor.h"

/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.1.1:
*        add WqCmd_af_continues_pause;
*v0.1.3:
*        add support flash control;
*
*v0.1.5/v0.1.7:
*        fix use v4l2_mbus_framefmt.reserved array overflow in generic_sensor_s_fmt;  
*v0.1.9:
*        fix sensor_find_ctrl may be overflow;
*/
static int version = KERNEL_VERSION(0,1,9);
module_param(version, int, S_IRUGO);


static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define CAMMODULE_NAME    "rk_cam_sensor"

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	    printk(KERN_WARNING fmt , ## arg); } while (0)

#define SENSOR_NAME_STRING() sensor->dev_name

#undef SENSOR_TR
#undef SENSOR_DG
#define SENSOR_TR(format, ...) printk(KERN_ERR "%s(%s:%d): " format"\n", SENSOR_NAME_STRING(),CAMMODULE_NAME,__LINE__, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, "%s(%s:%d): "format"\n", SENSOR_NAME_STRING(),CAMMODULE_NAME,__LINE__,## __VA_ARGS__)


#define CONFIG_SENSOR_I2C_RDWRCHK 0

static const struct rk_sensor_datafmt *generic_sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct rk_sensor_datafmt *fmt,
	int n);

int sensor_write_reg2val1(struct i2c_client *client, u16 reg,u8 val){	
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xffff;
	tmp_reg.val_mask = 0xff;
	tmp_reg.reg = reg;
	tmp_reg.val = val;
	return generic_sensor_write(client, &tmp_reg);
}
int sensor_write_reg2val2(struct i2c_client *client, u16 reg,u16 val){
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xffff;
	tmp_reg.val_mask = 0xffff;
	tmp_reg.reg = reg;
	tmp_reg.val = val;
	return generic_sensor_write(client, &tmp_reg);
}
int sensor_write_reg1val1(struct i2c_client *client, u8 reg,u8 val){
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xff;
	tmp_reg.val_mask = 0xff;
	tmp_reg.reg = reg;
	tmp_reg.val = val;
	return generic_sensor_write(client, &tmp_reg);
}
int sensor_write_reg1val2(struct i2c_client *client, u8 reg,u16 val){
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xff;
	tmp_reg.val_mask = 0xffff;
	tmp_reg.reg = reg;
	tmp_reg.val = val;
	return generic_sensor_write(client, &tmp_reg);
}
int sensor_write_reg0val0(struct i2c_client *client, u8 reg,u16 val) 
{
    struct generic_sensor *sensor = to_generic_sensor(client);

    SENSOR_TR("SENSOR_REGISTER_LEN and SENSOR_VALUE_LEN is 0, please use generic_sensor_write directly!");
    return -1;
}
/* sensor register write */
int generic_sensor_write(struct i2c_client *client,struct rk_sensor_reg* sensor_reg)
{
	int err,cnt = 0,i;
	u8 buf[6];
	struct i2c_msg msg[1];
	u32 i2c_speed;
    struct generic_sensor *sensor = to_generic_sensor(client);
    
	i2c_speed = sensor->info_priv.gI2c_speed;

	err = 0;
	switch(sensor_reg->reg){
		case SEQCMD_WAIT_MS:
            if (in_atomic())
                mdelay(sensor_reg->val);
            else
			    msleep(sensor_reg->val);
			break;
		case SEQCMD_WAIT_US:
			udelay(sensor_reg->val);
			break;
		default:          
            cnt=0;
            for (i=2; i>=0; i--) {
                if(((sensor_reg->reg_mask) & (0xff<<(i*8)))) {
                    buf[cnt++] = ((sensor_reg->reg)>>(i*8))&0xff;
                }
            }
            for (i=2; i>=0; i--) {
                if(((sensor_reg->val_mask) & (0xff<<(i*8)))) {
                    buf[cnt++] = ((sensor_reg->val)>>(i*8))&0xff;
                }
            }
            
			msg->addr = client->addr;
			msg->flags = client->flags;
			msg->buf = buf;
			msg->scl_rate = i2c_speed;		 /* ddl@rock-chips.com : 100kHz */
			msg->read_type = 0; 			  /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */
			msg->len = cnt;
			cnt = 3;
			err = -EAGAIN;
			
			while ((cnt-- > 0) && (err < 0)) {						 /* ddl@rock-chips.com :  Transfer again if transent is failed	 */
				err = i2c_transfer(client->adapter, msg, 1);
			
				if (err >= 0) {
                    err = 0;
					goto write_end;
				} else {
					SENSOR_TR("write reg(0x%x, val:0x%x) failed, try to write again!",sensor_reg->reg, sensor_reg->val);
					udelay(10);
				}
			}

	}

write_end:
	return err;
}

/* sensor register write buffer */
int generic_sensor_writebuf(struct i2c_client *client, char *buf, int buf_size)
{
	int err=0,cnt = 0;
	struct i2c_msg msg[1];
    struct generic_sensor *sensor = to_generic_sensor(client);
            
	msg->addr = client->addr;
	msg->flags = client->flags;
	msg->buf = buf;
	msg->scl_rate = sensor->info_priv.gI2c_speed;		 /* ddl@rock-chips.com : 100kHz */
	msg->read_type = 0; 			  
	msg->len = buf_size;
	cnt = 3;
	err = -EAGAIN;
	
	while ((cnt-- > 0) && (err < 0)) {						 /* ddl@rock-chips.com :  Transfer again if transent is failed	 */
		err = i2c_transfer(client->adapter, msg, 1);
	
		if (err >= 0) {
            err = 0;
			goto write_end;
		} else {
			SENSOR_TR("generic_sensor_writebuf failed!");
			udelay(10);
		}
	}


write_end:
	return err;
}
int sensor_read_reg1val1(struct i2c_client *client, u8 reg,u8* val){
	
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xff;
	tmp_reg.val_mask = 0xff;
	tmp_reg.reg = reg;
	tmp_reg.val = 0;
	if(generic_sensor_read(client, &tmp_reg)==0){
		*val = (u8)(tmp_reg.val & tmp_reg.val_mask);
	}else{
		return -1;
	}
	return 0;
}
int sensor_read_reg2val1(struct i2c_client *client, u16 reg,u8* val){
	
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xffff;
	tmp_reg.val_mask = 0xff;
	tmp_reg.reg = reg;
	tmp_reg.val = 0;
	if(generic_sensor_read(client, &tmp_reg)==0){
		*val = (u8)(tmp_reg.val & tmp_reg.val_mask);
	}else{
		return -1;
	}
	return 0;
}
int sensor_read_reg2val2(struct i2c_client *client, u16 reg,u16* val){
	
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xffff;
	tmp_reg.val_mask = 0xffff;
	tmp_reg.reg = reg;
	tmp_reg.val = 0;
	if(generic_sensor_read(client, &tmp_reg)==0){
		*val = (u16)(tmp_reg.val & tmp_reg.val_mask);
	}else{
		return -1;
	}
	return 0;
}
int sensor_read_reg1val2(struct i2c_client *client, u8 reg,u16* val){
	
	struct rk_sensor_reg tmp_reg;
    
	tmp_reg.reg_mask = 0xff;
	tmp_reg.val_mask = 0xffff;
	tmp_reg.reg = reg;
	tmp_reg.val = 0;
	if(generic_sensor_read(client, &tmp_reg)==0){
		*val = (u16)(tmp_reg.val & tmp_reg.val_mask);
	}else{
		return -1;
	}
	return 0;
}
int sensor_read_reg0val0(struct i2c_client *client, u8 reg,u16 val) 
{
    struct generic_sensor *sensor = to_generic_sensor(client);

    SENSOR_TR("SENSOR_REGISTER_LEN and SENSOR_VALUE_LEN is 0, please use generic_sensor_read directly!");
    return -1;
}
/* sensor register read */
int generic_sensor_read(struct i2c_client *client, struct rk_sensor_reg* sensor_reg)
{
	int err,cnt = 0,i,bytes;
	u8 buf_reg[3];
	u8 buf_val[3];
	struct i2c_msg msg[2];
	u32 i2c_speed;
    struct generic_sensor *sensor = to_generic_sensor(client);
    
	i2c_speed = sensor->info_priv.gI2c_speed;
	
    cnt=0;            
    for (i=2; i>=0; i--) {
        if((sensor_reg->reg_mask) & (0xff<<(i*8))) {
            buf_reg[cnt++] = ((sensor_reg->reg)>>(i*8))&0xff;
        }
    }
    
	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf_reg;
	msg[0].scl_rate = i2c_speed;		 /* ddl@rock-chips.com : 100kHz */
	msg[0].read_type = 2;	/* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */
	msg[0].len = cnt;

    cnt=0;
    for (i=2; i>=0; i--) {
        if((sensor_reg->val_mask) & (0xff<<(i*8))) {
            cnt++;
        }
    }
    memset(buf_val,0x00,sizeof(buf_val));
    
	msg[1].addr = client->addr;
	msg[1].flags = client->flags|I2C_M_RD;
	msg[1].buf = buf_val;
	msg[1].len = cnt;
	msg[1].scl_rate = i2c_speed;						 /* ddl@rock-chips.com : 100kHz */
	msg[1].read_type = 2;							  /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	cnt = 1;
	err = -EAGAIN;
	while ((cnt-- > 0) && (err < 0)) {						 /* ddl@rock-chips.com :  Transfer again if transent is failed	 */
		err = i2c_transfer(client->adapter, msg, 2);
		if (err >= 0) {            
            sensor_reg->val=0;
            bytes = 0x00;
            for (i=2; i>=0; i--) {
                if((sensor_reg->val_mask) & (0xff<<(i*8))) {
                    sensor_reg->val |= (buf_val[bytes++]<<(i*8));
                }
            }
			err = 0;
            goto read_end;
		} else {
			SENSOR_TR("read reg(0x%x val:0x%x) failed, try to read again!",sensor_reg->reg, sensor_reg->val);
			udelay(10);
		}
	}
read_end:
	return err;
}

/* write a array of registers  */
 int generic_sensor_write_array(struct i2c_client *client, struct rk_sensor_reg *regarray)
{
	int err = 0, cnt;
	int i = 0;
    bool streamchk;
#if CONFIG_SENSOR_I2C_RDWRCHK
	struct rk_sensor_reg check_reg;
#endif
	struct generic_sensor *sensor = to_generic_sensor(client);

    if (regarray[0].reg == SEQCMD_STREAMCHK) {
        streamchk = true;
        i = 1;
    } else {
        streamchk = false;
        i = 0;
    }

	cnt = 0;
	while ((regarray[i].reg != SEQCMD_END) && (regarray[i].reg != SEQCMD_INTERPOLATION))
	{
        if (streamchk) {
            if (sensor->info_priv.stream == false) {
                err = -1;
                SENSOR_DG("sensor is stream off, write array terminated!");
                break;
            }
        }
    
		if((sensor->info_priv.gReg_mask != 0) /*&& (regarray[i].reg_mask != 0)*/)
			regarray[i].reg_mask = sensor->info_priv.gReg_mask;
		if((sensor->info_priv.gVal_mask != 0) /* && (regarray[i].val_mask != 0)*/)
			regarray[i].val_mask = sensor->info_priv.gVal_mask;
		err = generic_sensor_write(client, &(regarray[i])); 
		if (err < 0)
		{
			if (cnt-- > 0) {
				SENSOR_TR("write failed current reg:0x%x, Write array again !",regarray[i].reg);
				i = 0;
				continue;
			} else {
				SENSOR_TR("write array failed!");
				err = -EPERM;
				goto sensor_write_array_end;
			}
		} else {
		#if CONFIG_SENSOR_I2C_RDWRCHK
			check_reg.reg_mask = regarray[i].reg_mask;
			check_reg.val_mask = regarray[i].val_mask;
			check_reg.reg = regarray[i].reg;
			check_reg.val =0;
			generic_sensor_read(client, &check_reg);
			if (check_reg.val!= regarray[i].val)
				SENSOR_TR("Reg:0x%x write(0x%x, 0x%x) fail", regarray[i].reg, regarray[i].val, check_reg.val );
		#endif
		}

		i++;
	}


sensor_write_array_end:
	return err;
}
#if CONFIG_SENSOR_I2C_RDWRCHK
int generic_sensor_readchk_array(struct i2c_client *client, struct rk_sensor_reg  *regarray)
{
	int cnt;
	int i = 0;
	struct rk_sensor_reg check_reg;
    struct generic_sensor *sensor = to_generic_sensor(client);

	cnt = 0;
	while (regarray[i].reg != SEQCMD_END)
	{
		check_reg.reg_mask = regarray[i].reg_mask;
		check_reg.val_mask = regarray[i].val_mask;
		check_reg.reg = regarray[i].reg;
		check_reg.val =0;
		generic_sensor_read(client, &check_reg);
		if (check_reg.val!= regarray[i].val)
			SENSOR_TR("Reg:0x%x write(0x%x, 0x%x) fail", regarray[i].reg, regarray[i].val, check_reg.val );

		i++;
	}
	return 0;
}
#endif

int generic_sensor_get_max_min_res(struct rk_sensor_sequence* res_array,int num,struct rk_sensor_seq_info * max_real_res,
										struct rk_sensor_seq_info * max_res,struct rk_sensor_seq_info *min_res){
	int array_index = 0,err = 0;
    
	max_real_res->w = max_res->w = 0;
	max_real_res->h = max_res->h =0;
	min_res->w =  min_res->h = 10000;
	if(!res_array || num <=0){
		printk("resolution array not valid");
		err = -1;
		goto get_end;
    }
    
	//serch min_res
	while(array_index <num) {
		if(res_array->data && res_array->data[0].reg != SEQCMD_END){
			if(res_array->gSeq_info.w < min_res->w ||res_array->gSeq_info.h < min_res->h){
					memcpy(min_res,&(res_array->gSeq_info),sizeof(struct rk_sensor_seq_info));
			}
			if((res_array->gSeq_info.w > max_real_res->w ||res_array->gSeq_info.h > max_real_res->h) 
				&& (res_array->data[0].reg != SEQCMD_INTERPOLATION)){
					memcpy(max_real_res,&(res_array->gSeq_info),sizeof(struct rk_sensor_seq_info));
			}
			if((res_array->gSeq_info.w > max_res->w ||res_array->gSeq_info.h > max_res->h) 
				&& (res_array->data[0].reg == SEQCMD_INTERPOLATION)){
					memcpy(max_res,&(res_array->gSeq_info),sizeof(struct rk_sensor_seq_info));
			}
		} 
		
		array_index++;
		res_array++;
		
	}
	if((max_res->w < max_real_res->w) || (max_res->h < max_real_res->h)){
		max_res->w = max_real_res->w;
		max_res->h = max_real_res->h;
	}
	printk("min_w = %d,min_h = %d ,max_real_w = %d,max_real_h = %d,max_w = %d,max_h =%d\n",
				min_res->w,min_res->h,max_real_res->w,max_real_res->h,max_res->w,max_res->h);
	err = 0;
get_end:
	return err;
}


// return value: -1 means erro; others means res_array array index
//se_w & set_h have been set to between MAX and MIN
static int sensor_try_fmt(struct i2c_client *client,unsigned int *set_w,unsigned int *set_h){
	int array_index = 0;
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct rk_sensor_sequence* res_array = sensor->info_priv.sensor_series;
	int num = sensor->info_priv.num_series;
	int tmp_w = 10000,tmp_h = 10000,tmp_index = -1;
	int resolution_diff_min=10000*10000,resolution_diff;

	while(array_index < num) {        
        if ((res_array->data) && (res_array->data[0].reg != SEQCMD_END)) {
            
            if(res_array->property == SEQUENCE_INIT) {
				tmp_index = array_index;
				array_index++;
				res_array++;
				continue;
            }

            resolution_diff = abs(res_array->gSeq_info.w*res_array->gSeq_info.h - (*set_w)*(*set_h));
            if (resolution_diff<resolution_diff_min) {
                tmp_w = res_array->gSeq_info.w;
				tmp_h = res_array->gSeq_info.h;
				tmp_index = array_index;

                resolution_diff_min = resolution_diff;
            }
            
		}
        array_index++;
	    res_array++;
	}
	*set_w = tmp_w;
	*set_h =  tmp_h;
	//only has the init array
	if((tmp_w == 10000) && (tmp_index != -1)){        
		SENSOR_TR("have not other series meet the requirement except init_serie,array_index = %d",tmp_index);
		*set_w = sensor->info_priv.sensor_series[tmp_index].gSeq_info.w;
		*set_h = sensor->info_priv.sensor_series[tmp_index].gSeq_info.h;
		goto try_end;
	}
	if((*set_w > sensor->info_priv.max_real_res.w) || (*set_h > sensor->info_priv.max_real_res.h)){
		SENSOR_DG("it is a interpolation resolution!(%dx%d:%dx%d)",sensor->info_priv.max_real_res.w
					,sensor->info_priv.max_real_res.h,*set_w,*set_h);
		*set_w = sensor->info_priv.max_real_res.w;
		*set_h = sensor->info_priv.max_real_res.h;
		//find the max_real_res index
		res_array = sensor->info_priv.sensor_series;
		array_index = 0;
		tmp_index = -1;
		while(array_index < num){
			if((res_array->data) && (res_array->data[0].reg != SEQCMD_END) && (*set_w  ==res_array->gSeq_info.w) && (*set_h ==res_array->gSeq_info.h)){
				if((res_array->property != SEQUENCE_INIT)){
					tmp_index = array_index;
					break;
				}else{
					tmp_index = array_index;
				}
			}
			array_index++;
			res_array++ ;
		}
		
	}
try_end:
	return tmp_index;
}
int generic_sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor *sensor = to_generic_sensor(client);
    const struct rk_sensor_datafmt *fmt;
    int ret = 0;
    unsigned int set_w,set_h,ori_w,ori_h;
    
    ori_w = mf->width;
    ori_h = mf->height;
    
    fmt = generic_sensor_find_datafmt(mf->code, sensor->info_priv.datafmt,
    						sensor->info_priv.num_datafmt);
    if (fmt == NULL) {
        fmt = &(sensor->info_priv.curfmt);
        mf->code = fmt->code;
    }
    /* ddl@rock-chips.com : It is query max resolution only. */
    if (mf->reserved[6] == 0xfefe5a5a) {
        mf->height = sensor->info_priv.max_res.h ;
        mf->width = sensor->info_priv.max_res.w;
        ret = 0;
        SENSOR_DG("Query resolution: %dx%d",mf->width, mf->height);
        goto generic_sensor_try_fmt_end;
    }
 //use this to filter unsupported resolutions
   if (sensor->sensor_cb.sensor_try_fmt_cb_th){
   	ret = sensor->sensor_cb.sensor_try_fmt_cb_th(client, mf);
	if(ret < 0)
		goto generic_sensor_try_fmt_end;
   	}
    if (mf->height > sensor->info_priv.max_res.h)
        mf->height = sensor->info_priv.max_res.h;
    else if (mf->height < sensor->info_priv.min_res.h)
        mf->height = sensor->info_priv.min_res.h;

    if (mf->width > sensor->info_priv.max_res.w)
        mf->width = sensor->info_priv.max_res.w;
    else if (mf->width < sensor->info_priv.min_res.w)
        mf->width = sensor->info_priv.min_res.w;
    set_w = mf->width;
    set_h = mf->height;    
    ret = sensor_try_fmt(client,&set_w,&set_h);
    mf->width = set_w;
    mf->height = set_h;
    mf->colorspace = fmt->colorspace;
    SENSOR_DG("%dx%d is the closest for %dx%d",ori_w,ori_h,set_w,set_h);
generic_sensor_try_fmt_end:
    return ret;
}

int generic_sensor_enum_frameintervals(struct v4l2_subdev *sd, struct v4l2_frmivalenum *fival){
	int err = 0,index_tmp;
    unsigned int set_w,set_h;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct generic_sensor *sensor = to_generic_sensor(client);

	if (fival->height > sensor->info_priv.max_res.h|| fival->width > sensor->info_priv.max_res.w){
        SENSOR_TR("this resolution(%dx%d) isn't support!",fival->width,fival->height);
        err = -1;
        goto enum_frameintervals_end;
	}
	set_w = fival->width;
    set_h = fival->height;
    index_tmp = sensor_try_fmt(client,&set_w,&set_h);
    fival->discrete.denominator = sensor->info_priv.sensor_series[index_tmp].gSeq_info.fps;
    fival->discrete.numerator = 1000;
    fival->reserved[1] = (set_w<<16)|set_h;
    fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;

    SENSOR_DG("%dx%d(real:%dx%d) framerate: %d",fival->width,fival->height,set_w,set_h,fival->discrete.denominator);
enum_frameintervals_end:
	return err;
}

static enum hrtimer_restart generic_flash_off_func(struct hrtimer *timer){
	struct rk_flash_timer *fps_timer = container_of(timer, struct rk_flash_timer, timer);

    generic_sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
	return 0;
}
/* Find a data format by a pixel code in an array */
static const struct rk_sensor_datafmt *generic_sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct rk_sensor_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}
int generic_sensor_softreset(struct i2c_client *client, struct rk_sensor_reg *series) {
    int ret = 0;
    struct generic_sensor *sensor = to_generic_sensor(client);
    

    if (sensor->sensor_cb.sensor_softreset_cb)
         sensor->sensor_cb.sensor_softreset_cb(client,series);
    
	/* soft reset */
    ret = generic_sensor_write_array(client,series);
	if (ret != 0) {
		SENSOR_TR("soft reset failed\n");
		ret = -ENODEV;
	}
    msleep(1);
	return ret;
    
}
int generic_sensor_check_id(struct i2c_client *client, struct rk_sensor_reg *series)
{
	int ret,pid = 0,i;
	struct generic_sensor *sensor = to_generic_sensor(client);

    if (sensor->sensor_cb.sensor_check_id_cb)
          pid = sensor->sensor_cb.sensor_check_id_cb(client,series);

    /* check if it is an sensor sensor */
    while (series->reg != SEQCMD_END) {

        pid <<= 8;
        
        if (sensor->info_priv.gReg_mask != 0x00) 
            series->reg_mask = sensor->info_priv.gReg_mask;
        if (sensor->info_priv.gVal_mask != 0x00)
            series->val_mask = sensor->info_priv.gVal_mask;
        
        ret = generic_sensor_read(client, series);
        if (ret != 0) {
    		SENSOR_TR("read chip id failed");
    		ret = -ENODEV;
    		goto check_end;
    	}

        pid |= series->val;
        series++;
    }
    
	SENSOR_DG("pid = 0x%x", pid);

    for (i=0; i<sensor->info_priv.chip_id_num; i++) {
        if (pid == sensor->info_priv.chip_id[i]) {
            sensor->model = sensor->info_priv.chip_ident;    
            break;
        }
    }
    
	if (sensor->model != sensor->info_priv.chip_ident) {
		SENSOR_TR("error: mismatched   pid = 0x%x\n", pid);
		ret = -ENODEV;
		goto check_end;
	} else {
        ret = 0;
	}
    
check_end:
	return ret;
}

int generic_sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
    struct rk29camera_platform_data *pdata = icl->priv_usr;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct generic_sensor *sensor = to_generic_sensor(client);
	int ret = 0;

	SENSOR_DG("%s cmd(%d) on(%d)\n",__FUNCTION__,cmd,on);
	switch (cmd)
	{
        case Sensor_Power:
        {
			if (icl->power) {
				ret = icl->power(icd->pdev, on);
			} else {
			    SENSOR_TR("haven't power callback");
                ret = -EINVAL;
			}
			break;
		}
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
			} else {
			    SENSOR_TR("haven't power down callback");
                ret = -EINVAL;
			}
			break;
		}
		case Sensor_Flash:
		{
			if (pdata && pdata->sensor_ioctrl) {
				pdata->sensor_ioctrl(icd->pdev,Cam_Flash, on);
				if(on==Flash_On){
                    mdelay(5);
					//flash off after 2 secs
					hrtimer_cancel(&(sensor->flash_off_timer.timer));
					hrtimer_start(&(sensor->flash_off_timer.timer),ktime_set(0, 2000*1000*1000),HRTIMER_MODE_REL);
				}
			}
			break;
		}
		default:
		{
			SENSOR_TR("%s cmd(%d) is unknown!",__FUNCTION__,cmd);
			break;
		}
	}
    
	return ret;
}

int generic_sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_device *icd = client->dev.platform_data;
	struct generic_sensor *sensor = to_generic_sensor(client);
	int array_index = 0;
	int num = sensor->info_priv.num_series;    
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    struct rk29camera_platform_data *pdata = icl->priv_usr;
    struct rkcamera_platform_data *sensor_device=NULL,*new_camera;

    new_camera = pdata->register_dev_new;
    while (strstr(new_camera->dev_name,"end")==NULL) {
        if (strcmp(dev_name(icd->pdev), new_camera->dev_name) == 0) {
            sensor_device = new_camera;
            break;
        }
        new_camera++;
    }
    
    /* ddl@rock-chips.com : i2c speed is config in new_camera_device_ex macro */
    if (sensor_device) {
        sensor->info_priv.gI2c_speed = sensor_device->i2c_rate;
        sensor->info_priv.mirror = sensor_device->mirror;
    }
    
    if (((sensor_device!=NULL) && Sensor_HasBeen_PwrOff(sensor_device->pwdn_info)) 
        || (sensor_device == NULL)) {  
        
        //softreset callback
        ret =  generic_sensor_softreset(client,sensor->info_priv.sensor_SfRstSeqe);
    	if(ret != 0){
    		SENSOR_TR("soft reset failed!");
    		goto sensor_INIT_ERR;
    	}
        
    	while(array_index < num){
    		if(sensor->info_priv.sensor_series[array_index].property == SEQUENCE_INIT)
    			break;
    		array_index++;
    	}
    	if(generic_sensor_write_array(client, sensor->info_priv.sensor_series[array_index].data)!=0){
    		SENSOR_TR("write init array failed!");
    		ret = -1;
    		goto sensor_INIT_ERR;
    	}
        if (sensor_device!=NULL) {
            sensor_device->pwdn_info &= 0xfe;
            if (sensor->sensor_cb.sensor_mirror_cb)
                sensor->sensor_cb.sensor_mirror_cb(client, sensor->info_priv.mirror&0x01);
            if (sensor->sensor_cb.sensor_flip_cb)
                sensor->sensor_cb.sensor_flip_cb(client, sensor->info_priv.mirror&0x02);
        }
        sensor->info_priv.winseqe_cur_addr = sensor->info_priv.sensor_series + array_index;

        //set focus status ,init focus
        sensor->sensor_focus.focus_state = FocusState_Inval;
    	sensor->sensor_focus.focus_mode = WqCmd_af_invalid;
    	sensor->sensor_focus.focus_delay = WqCmd_af_invalid;    	
	
    }
    
    if (sensor->sensor_cb.sensor_activate_cb)
        sensor->sensor_cb.sensor_activate_cb(client);
   

    if (sensor->flash_off_timer.timer.function==NULL)
        sensor->flash_off_timer.timer.function = generic_flash_off_func;
    
	sensor->info_priv.funmodule_state |= SENSOR_INIT_IS_OK;
    
	return 0;
sensor_INIT_ERR:
	sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	if(sensor->sensor_cb.sensor_deactivate_cb)
		sensor->sensor_cb.sensor_deactivate_cb(client);
	return ret;
}
 int generic_sensor_set_bus_param(struct soc_camera_device *icd,
								unsigned long flags)
{

	return 0;
}

unsigned long generic_sensor_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct generic_sensor *sensor = to_generic_sensor(client);
	
	unsigned long flags = sensor->info_priv.bus_parameter;

	return soc_camera_apply_sensor_flags(icl, flags);
}
int generic_sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_device *icd = client->dev.platform_data;
	struct generic_sensor *sensor = to_generic_sensor(client);

	mf->width	= icd->user_width;
	mf->height	= icd->user_height;
	mf->code	= sensor->info_priv.curfmt.code;
	mf->colorspace	= sensor->info_priv.curfmt.colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}
int generic_sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct rk_sensor_datafmt *fmt=NULL;
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct rk_sensor_sequence *winseqe_set_addr=NULL;
    struct sensor_v4l2ctrl_info_s *v4l2ctrl_info=NULL;
    bool is_capture=(mf->reserved[0]==0xfefe5a5a)?true:false;    /* ddl@rock-chips.com : v0.1.5 */ 
    int ret=0;

    fmt =generic_sensor_find_datafmt(mf->code, sensor->info_priv.datafmt,
    	                                sensor->info_priv.num_datafmt);
    if (!fmt) {
        ret = -EINVAL;
        goto sensor_s_fmt_end;
    }
    
    // get the proper series and write the array
    ret =generic_sensor_try_fmt(sd, mf);
    winseqe_set_addr = sensor->info_priv.sensor_series+ret;

    ret = 0;    
    if(sensor->info_priv.winseqe_cur_addr->data != winseqe_set_addr->data){

        if (sensor->sensor_cb.sensor_s_fmt_cb_th)
            ret |= sensor->sensor_cb.sensor_s_fmt_cb_th(client, mf, is_capture);
        
        v4l2ctrl_info = sensor_find_ctrl(sensor->ctrls,V4L2_CID_FLASH); /* ddl@rock-chips.com: v0.1.3 */        
        if (v4l2ctrl_info!=NULL) {   
            if (is_capture) { 
                if ((v4l2ctrl_info->cur_value == 2) || (v4l2ctrl_info->cur_value == 1)) {
                    generic_sensor_ioctrl(icd, Sensor_Flash, 1);                    
                }
            } else {
                generic_sensor_ioctrl(icd, Sensor_Flash, 0); 
            }
        }
       
        ret |= generic_sensor_write_array(client, winseqe_set_addr->data);
        if (ret != 0) {
            SENSOR_TR("set format capability failed");
            goto sensor_s_fmt_end;
        }

        if (sensor->sensor_cb.sensor_s_fmt_cb_bh)
            ret |= sensor->sensor_cb.sensor_s_fmt_cb_bh(client, mf, is_capture);
        sensor->info_priv.winseqe_cur_addr  = winseqe_set_addr;
        SENSOR_DG("Sensor output is changed to %dx%d",winseqe_set_addr->gSeq_info.w,winseqe_set_addr->gSeq_info.h);
    } else {
        SENSOR_DG("Sensor output is still %dx%d",winseqe_set_addr->gSeq_info.w,winseqe_set_addr->gSeq_info.h);
    }
	 
//video or capture special process
sensor_s_fmt_end:
	return ret;
}
 int generic_sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct generic_sensor *sensor = to_generic_sensor(client);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident = sensor->info_priv.chip_ident;		/* ddl@rock-chips.com :  Return OV2655	identifier */
	id->revision = 0;

	return 0;
}
 
int generic_sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct sensor_v4l2ctrl_info_s *ctrl_info;
    int ret = 0;

    ctrl_info = sensor_find_ctrl(sensor->ctrls,ctrl->id);
    if (!ctrl_info) {
        SENSOR_TR("v4l2_control id(0x%x) is invalidate",ctrl->id);
        ret = -EINVAL;
    } else {
        ctrl->value = ctrl_info->cur_value;
    }

    return ret;
}

int generic_sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor_v4l2ctrl_info_s *ctrl_info;
    struct v4l2_ext_control ext_ctrl;
    int ret = 0;

    ctrl_info = sensor_find_ctrl(sensor->ctrls,ctrl->id);
    if (!ctrl_info) {
        SENSOR_TR("v4l2_control id(0x%x) is invalidate",ctrl->id);
        ret = -EINVAL;
    } else {

        ext_ctrl.id = ctrl->id;
        ext_ctrl.value = ctrl->value;
        
        if (ctrl_info->cb) {
            ret = (ctrl_info->cb)(icd,ctrl_info, &ext_ctrl);
        } else {
            SENSOR_TR("v4l2_control id(0x%x) callback isn't exist",ctrl->id);
            ret = -EINVAL;
        }
    }

    return ret;
}

int generic_sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct sensor_v4l2ctrl_info_s *ctrl_info;
    int ret = 0;

    ctrl_info = sensor_find_ctrl(sensor->ctrls,ext_ctrl->id);
    if (!ctrl_info) {
        SENSOR_TR("v4l2_control id(0x%x) is invalidate",ext_ctrl->id);
        ret = -EINVAL;
    } else {
        ext_ctrl->value = ctrl_info->cur_value;
    }

    return ret;
}

int generic_sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct sensor_v4l2ctrl_info_s *ctrl_info;
    int ret = 0;
    
    ctrl_info = sensor_find_ctrl(sensor->ctrls,ext_ctrl->id);
    if (!ctrl_info) {
        SENSOR_TR("v4l2_ext_control id(0x%x) is invalidate",ext_ctrl->id);
        ret = -EINVAL;
    } else {        
        if (ctrl_info->cb) {
            ret = (ctrl_info->cb)(icd,ctrl_info, ext_ctrl);
        } else {
            SENSOR_TR("v4l2_ext_control id(0x%x) callback isn't exist",ext_ctrl->id);
            ret = -EINVAL;
        }
    }
    return 0;
}
  int generic_sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
 {
	 struct i2c_client *client = v4l2_get_subdevdata(sd);
	 struct soc_camera_device *icd = client->dev.platform_data;
     //struct generic_sensor *sensor = to_generic_sensor(client);
	 int i, error_cnt=0, error_idx=-1;
 
	 for (i=0; i<ext_ctrl->count; i++) {
		 if (generic_sensor_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
			 error_cnt++;
			 error_idx = i;
		 }
	 }
 
	 if (error_cnt > 1)
		 error_idx = ext_ctrl->count;
 
	 if (error_idx != -1) {
		 ext_ctrl->error_idx = error_idx;
		 return -EINVAL;
	 } else {
		 return 0;
	 }
 }
int generic_sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    //struct generic_sensor*sensor = to_generic_sensor(client);
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        
        if (generic_sensor_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}
 
 
long generic_sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct generic_sensor*sensor = to_generic_sensor(client);
    int ret = 0;
    int i;
    bool flash_attach=false;
    struct rkcamera_platform_data *new_camera;

    SENSOR_DG("%s cmd: 0x%x ",__FUNCTION__,cmd);
    switch (cmd)
    {
        case RK29_CAM_SUBDEV_DEACTIVATE:
        {
            if(sensor->sensor_cb.sensor_deactivate_cb)
                sensor->sensor_cb.sensor_deactivate_cb(client);
            break;
        }

        case RK29_CAM_SUBDEV_IOREQUEST:
        {
            sensor->sensor_io_request = (struct rk29camera_platform_data*)arg;           
            if (sensor->sensor_io_request != NULL) { 
                sensor->sensor_gpio_res = NULL;
                for (i=0; i<RK29_CAM_SUPPORT_NUMS;i++) {
                    if (sensor->sensor_io_request->gpio_res[i].dev_name && 
                        (strcmp(sensor->sensor_io_request->gpio_res[i].dev_name, dev_name(icd->pdev)) == 0)) {
                        sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[i];
                    }
                }                
            } else {
                SENSOR_TR("RK29_CAM_SUBDEV_IOREQUEST fail");
                ret = -EINVAL;
                goto sensor_ioctl_end;
            }
            /* ddl@rock-chips.com : if gpio_flash havn't been set in board-xxx.c, sensor driver must notify is not support flash control 
               for this project */
            if (sensor->sensor_gpio_res) {                
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    flash_attach = false;
                } else { 
                    flash_attach = true;
                }
            }

            new_camera = sensor->sensor_io_request->register_dev_new;
            while (strstr(new_camera->dev_name,"end")==NULL) {
                if (strcmp(dev_name(icd->pdev), new_camera->dev_name) == 0) {
                    if (new_camera->flash){
                        flash_attach = true;
                    } else { 
                        flash_attach = false;
                    }
                    break;
                }
                new_camera++;
            }

            if (flash_attach==false) {
                for (i = 0; i < icd->ops->num_controls; i++) {
                    if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                        sensor->sensor_controls[i].id |= 0x80000000;         			
                    }
                }
            } else {
                for (i = 0; i < icd->ops->num_controls; i++) {
                    if(V4L2_CID_FLASH == (icd->ops->controls[i].id&0x7fffffff)){
                        sensor->sensor_controls[i].id &= 0x7fffffff;
                    }               
                }
            }
            break;
        }
        default:
        {
            SENSOR_TR("%s cmd(0x%x) is unknown !\n",__FUNCTION__,cmd);
            break;
        }
    }
sensor_ioctl_end:
    return ret;
}
 
int generic_sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
	 enum v4l2_mbus_pixelcode *code)
{

    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor*sensor = to_generic_sensor(client);
    if (index >= sensor->info_priv.num_datafmt)
        return -EINVAL;

    *code = sensor->info_priv.datafmt[index].code;
    return 0;
} 
 static void sensor_af_workqueue(struct work_struct *work)
{
	struct rk_sensor_focus_work *sensor_work = container_of(work, struct rk_sensor_focus_work, dwork.work);
	struct i2c_client *client = sensor_work->client;
	struct generic_sensor*sensor = to_generic_sensor(client);
	//struct rk_sensor_focus_cmd_info cmdinfo;
	int zone_tm_pos[4];
	int ret = 0;
	
	SENSOR_DG("%s Enter, cmd:0x%x",__FUNCTION__,sensor_work->cmd);
	
	switch (sensor_work->cmd) 
	{
		case WqCmd_af_init:
		{
            if (sensor->sensor_focus.focus_state == FocusState_Inval) { 
    			if(sensor->sensor_focus.focus_cb.sensor_focus_init_cb !=NULL) {
    				ret = (sensor->sensor_focus.focus_cb.sensor_focus_init_cb)(client);
    			}
    			if (ret < 0) {
    				SENSOR_TR("WqCmd_af_init is failed in sensor_af_workqueue!");
    			} else {
    				if(sensor->sensor_focus.focus_delay != WqCmd_af_invalid) {
    					generic_sensor_af_workqueue_set(client->dev.platform_data,sensor->sensor_focus.focus_delay,0,false);
    					sensor->sensor_focus.focus_delay = WqCmd_af_invalid;
    				}
                    sensor->sensor_focus.focus_state = FocusState_Inited;
    				sensor_work->result = WqRet_success;
    			}
            } else {
                sensor_work->result = WqRet_success;
                SENSOR_DG("sensor af have been inited, WqCmd_af_init is ignore!");
            }
			break;
		}
		case WqCmd_af_single:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_single_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_single_cb)(client);
			}
			if (ret < 0) {
				SENSOR_TR("%s Sensor_af_single is failed in sensor_af_workqueue!",SENSOR_NAME_STRING());
				sensor_work->result = WqRet_fail;
			} else {
				sensor_work->result = WqRet_success;
			}
			break;
		}
		case WqCmd_af_near_pos:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_near_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_near_cb)(client);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else{ 
			   sensor_work->result = WqRet_success;
				}
			break;
		}
		case WqCmd_af_far_pos:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_far_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_far_cb)(client);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		}
		case WqCmd_af_special_pos:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_specialpos_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_specialpos_cb)(client,sensor_work->var);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		}
		case WqCmd_af_continues:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_const_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_const_cb)(client);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		}
        case WqCmd_af_continues_pause:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_const_pause_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_const_pause_cb)(client);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		} 
		case WqCmd_af_update_zone:
		{
            mutex_lock(&sensor->sensor_focus.focus_lock);
            zone_tm_pos[0] = sensor->sensor_focus.focus_zone.lx;
        	zone_tm_pos[1] = sensor->sensor_focus.focus_zone.ty;
        	zone_tm_pos[2] = sensor->sensor_focus.focus_zone.rx;
        	zone_tm_pos[3] = sensor->sensor_focus.focus_zone.dy;
            mutex_unlock(&sensor->sensor_focus.focus_lock);
            
			if(sensor->sensor_focus.focus_cb.sensor_af_zoneupdate_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_zoneupdate_cb)(client,zone_tm_pos);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		}
		case WqCmd_af_close:
		{
			if(sensor->sensor_focus.focus_cb.sensor_af_close_cb!=NULL){
				ret = (sensor->sensor_focus.focus_cb.sensor_af_close_cb)(client);
			}
			if (ret < 0)
			   sensor_work->result = WqRet_fail;
			else 
			   sensor_work->result = WqRet_success;
			break;
		}
		default:
			SENSOR_TR("Unknow command(%d) in %s af workqueue!",sensor_work->cmd,SENSOR_NAME_STRING());
			break;
	}
    
//set_end:    
	if (sensor_work->wait == false) {
		kfree((void*)sensor_work);
	} else {
		wake_up(&sensor_work->done); 
	}
	return;
}

 int generic_sensor_af_workqueue_set(struct soc_camera_device *icd, enum rk_sensor_focus_wq_cmd cmd, int var, bool wait)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct generic_sensor*sensor = to_generic_sensor(client);
	struct rk_sensor_focus_work *wk;
	int ret=0;

	if (sensor->sensor_focus.sensor_wq == NULL) { 
		ret = -EINVAL;
		goto sensor_af_workqueue_set_end;
	}

	wk = kzalloc(sizeof(struct rk_sensor_focus_work), GFP_KERNEL);
	if (wk) {
		wk->client = client;
		INIT_DELAYED_WORK(&wk->dwork, sensor_af_workqueue);
		wk->cmd = cmd;
		wk->result = WqRet_inval;
		wk->wait = wait;
		wk->var = var;
		init_waitqueue_head(&wk->done);

        SENSOR_DG("generic_sensor_af_workqueue_set:  cmd: %d",cmd);
        
		/* ddl@rock-chips.com: 
		* video_lock is been locked in v4l2_ioctl function, but auto focus may slow,
		* As a result any other ioctl calls will proceed very, very slowly since each call
		* will have to wait for the AF to finish. Camera preview is pause,because VIDIOC_QBUF 
		* and VIDIOC_DQBUF is sched. so unlock video_lock here.
		*/
		if (wait == true) {
			queue_delayed_work(sensor->sensor_focus.sensor_wq,&(wk->dwork),0);
			mutex_unlock(&icd->video_lock); 					
			if (wait_event_timeout(wk->done, (wk->result != WqRet_inval), msecs_to_jiffies(5000)) == 0) {
				SENSOR_TR("af cmd(%d) is timeout!",cmd);						 
			}
			flush_workqueue(sensor->sensor_focus.sensor_wq);
			ret = wk->result;
			kfree((void*)wk);
			mutex_lock(&icd->video_lock);  
		} else {
			queue_delayed_work(sensor->sensor_focus.sensor_wq,&(wk->dwork),msecs_to_jiffies(10));
		}
	} else {
		SENSOR_TR("af cmd(%d) ingore,because struct sensor_work malloc failed!",cmd);
		ret = -1;
	}
sensor_af_workqueue_set_end:
	return ret;
} 


int generic_sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct generic_sensor *sensor = to_generic_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    SENSOR_DG("s_stream: %d %d",enable,sensor->sensor_focus.focus_state);
	if (enable == 1) {
		sensor->info_priv.stream = true;
    	if (sensor->sensor_focus.sensor_wq) {
			if (sensor->sensor_focus.focus_state == FocusState_Inval) {
                generic_sensor_af_workqueue_set(icd, WqCmd_af_init, 0, false);				
			}
        }
	} else if (enable == 0) {
	    sensor->info_priv.stream = false;
		if (sensor->sensor_focus.sensor_wq)
            flush_workqueue(sensor->sensor_focus.sensor_wq);
		
	}
	return 0;
} 

