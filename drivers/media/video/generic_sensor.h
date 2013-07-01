#ifndef __ASM_ARCH_GENERIC_SENSOR_RK_H_
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/stat.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <linux/vmalloc.h>
#include <plat/rk_camera.h>

/* Camera Sensor driver */

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)	((x>y) ? x: y)

#define SENSOR_TR(format, ...) printk(KERN_ERR "%s(%d): " format"\n", SENSOR_NAME_STRING(),__LINE__, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, "%s(%d): "format"\n", SENSOR_NAME_STRING(),__LINE__,## __VA_ARGS__)

//to generic sensor
//a represents a i2c_client type point
#define to_generic_sensor(a) (container_of(i2c_get_clientdata(a), struct generic_sensor, subdev))

//to specific sensor
//a represents a generic_sensor type point
#define to_specific_sensor(a) (container_of(a, struct specific_sensor, common_sensor))

#define SENSOR_INIT_IS_ERR	 (0x00<<28)
#define SENSOR_INIT_IS_OK	 (0x01<<28)

#define SEQCMD_STREAMCHK     0xFA000000
#define SEQCMD_INVALIDATE    0xFB000000
#define SEQCMD_INTERPOLATION 0xFC000000
#define SEQCMD_WAIT_MS	 0xFD000000
#define SEQCMD_WAIT_US	 0xFE000000
#define SEQCMD_END		 0xFF000000

#define SensorReg0Val0(a,b)    {SEQCMD_INVALIDATE,0,0,0}
#define SensorReg1Val1(a,b)    {a,b,0xff,0xff}
#define SensorReg2Val1(a,b)    {a,b,0xffff,0xff}
#define SensorReg2Val2(a,b)    {a,b,0xffff,0xffff}

#define SensorStreamChk        {SEQCMD_STREAMCHK,0,0,0}
#define SensorWaitMs(a) 	   {SEQCMD_WAIT_MS,a,0x00,0x00}
#define SensorWaitUs(a) 	   {SEQCMD_WAIT_US,a,0x00,0x00}
#define SensorEnd			   {SEQCMD_END,0x00,0x00,0x00}

#define CFG_WhiteBalance	               (1<<0)
#define CFG_Brightness	                   (1<<1)
#define CFG_Contrast		               (1<<2)
#define CFG_Saturation                     (1<<3)
#define CFG_Effect		                   (1<<4)
#define CFG_Scene 		                   (1<<5)
#define CFG_DigitalZoom	                   (1<<6)
#define CFG_Focus 		                   (1<<7)
#define CFG_FocusContinues 		           (1<<8)
#define CFG_FocusZone                      (1<<9)
#define CFG_FocusRelative 		           (1<<10)
#define CFG_FocusAbsolute 		           (1<<11)
#define CFG_FACE_DETECT                    (1<<12)
#define CFG_Exposure		               (1<<13)
#define CFG_Flash 		                   (1<<14)
#define CFG_Mirror		                   (1<<15)
#define CFG_Flip			               (1<<16)

#define CFG_FunChk(a,b)                      ((a&b)==b) 
#define CFG_FunDis(a,b)                      (a &= (~b))

enum rk_sensor_sequence_property {
	SEQUENCE_INIT =1,
	SEQUENCE_PREVIEW,
	SEQUENCE_CAPTURE
};

struct rk_sensor_reg {
	unsigned int reg;
	unsigned int val;
	unsigned int reg_mask;
	unsigned int val_mask;
};

struct rk_sensor_seq_info {
	unsigned short w;
	unsigned short h;
	unsigned short fps;
};
struct rk_sensor_sequence {
	struct rk_sensor_seq_info gSeq_info;
	enum rk_sensor_sequence_property property;
	struct rk_sensor_reg *data;
};

/* only one fixed colorspace per pixelcode */
struct rk_sensor_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};


//focus work
enum rk_sensor_focus_wq_cmd
{
	WqCmd_af_invalid = -1,
	WqCmd_af_init =1,
	WqCmd_af_single,
	WqCmd_af_continues,
	WqCmd_af_continues_pause,     /* ddl@rock-chips.com: v0.1.1 */
	WqCmd_af_update_zone,
	WqCmd_af_close,
	WqCmd_af_special_pos,
	WqCmd_af_near_pos,
	WqCmd_af_far_pos
	
};
enum rk_sensor_focus_sensor_wq_result
{
	WqRet_success = 0,
	WqRet_fail = -1,
	WqRet_inval = -2
};

enum rk_sensor_focus_state
{
    FocusState_Inval,
    FocusState_Inited
};

struct rk_sensor_focus_work
{
	struct i2c_client *client;
	struct delayed_work dwork;
	enum rk_sensor_focus_wq_cmd cmd;
	wait_queue_head_t done;
	enum rk_sensor_focus_sensor_wq_result result;
	bool wait;
	int var;	
};

//focus structs
struct rk_sensor_focus_cb{
	int (*sensor_focus_init_cb)(struct i2c_client *client);	
    int (*sensor_af_single_cb)(struct i2c_client *client);
    int (*sensor_af_const_cb)(struct i2c_client *client);
    int (*sensor_af_const_pause_cb)(struct i2c_client *client);
    int (*sensor_af_zoneupdate_cb)(struct i2c_client *client, int *zone_tm_pos);
    int (*sensor_af_close_cb)(struct i2c_client *client);
    int (*sensor_af_near_cb)(struct i2c_client *client);
    int (*sensor_af_far_cb)(struct i2c_client *client);
    int (*sensor_af_specialpos_cb)(struct i2c_client *client,int pos);  
	//
};
//zone from hal is [-1000,-1000,1000,1000],must map to sensor's zone.
struct rk_sensor_focus_zone{
	int lx;
	int ty;
	int rx;
	int dy;
};
struct rk_sensor_focus_op_s{
	struct rk_sensor_focus_cb focus_cb;
	struct workqueue_struct *sensor_wq;
	struct mutex focus_lock;
    unsigned int focus_state;
	unsigned int focus_mode;                //show the focus mode
	struct rk_sensor_focus_zone focus_zone;
	enum rk_sensor_focus_wq_cmd focus_delay;
};


typedef struct rk_sensor_priv_s
{
    int mirror;
	bool snap2preview;
	bool video2preview;
	struct rk_sensor_sequence* winseqe_cur_addr;
	struct rk_sensor_datafmt* datafmt;
	int num_datafmt;
	struct rk_sensor_datafmt curfmt;
	unsigned int funmodule_state;
	//
	struct rk_sensor_sequence* sensor_series;
	int num_series;	

    struct rk_sensor_reg* sensor_SfRstSeqe;
    struct rk_sensor_reg* sensor_CkIdSeqe;
    
	struct rk_sensor_seq_info max_res;//maybe interploted
	struct rk_sensor_seq_info max_real_res;
	struct rk_sensor_seq_info min_res;
	unsigned long bus_parameter;
    unsigned int  *chip_id;
    unsigned int  chip_id_num;
	int chip_ident;
	unsigned int gReg_mask;
	unsigned int gVal_mask;
	unsigned int gI2c_speed;

    bool stream;
	
} rk_sensor_info_priv_t;

struct sensor_v4l2ctrl_info_s {
    struct v4l2_queryctrl *qctrl;
    int (*cb)(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
              struct v4l2_ext_control *ext_ctrl);
    struct rk_sensor_reg **sensor_Seqe;
    int cur_value;
    int num_ctrls;
};

struct sensor_v4l2ctrl_usr_s {
    struct v4l2_queryctrl qctrl;
    int (*cb)(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
              struct v4l2_ext_control *ext_ctrl);
    struct rk_sensor_reg **sensor_Seqe;
};

struct sensor_ops_cb_s{
	int (*sensor_softreset_cb)(struct i2c_client *client,struct rk_sensor_reg *series);
	int (*sensor_check_id_cb)(struct i2c_client *client,struct rk_sensor_reg *series);
	int (*sensor_activate_cb)(struct i2c_client *client);
	int (*sensor_deactivate_cb)(struct i2c_client *client);
	int (*sensor_mirror_cb)(struct i2c_client *client, int mirror);
	int (*sensor_flip_cb)(struct i2c_client *client, int flip);
	int (*sensor_face_detect_cb)(struct i2c_client *client, int on);

	int (*sensor_s_fmt_cb_th)(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture);
	int (*sensor_s_fmt_cb_bh)(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture);
	int (*sensor_try_fmt_cb_th)(struct i2c_client *client,struct v4l2_mbus_framefmt *mf);
};
//flash off in fixed time to prevent from too hot , zyc
struct	rk_flash_timer{
	struct soc_camera_device *icd;
	struct hrtimer timer;
};

struct generic_sensor
{
    char dev_name[32];
	struct v4l2_subdev subdev;
	struct i2c_client *client;
	rk_sensor_info_priv_t info_priv;
	int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
	
	bool is_need_tasklock;
	atomic_t tasklock_cnt;
	struct soc_camera_ops *sensor_ops; 
	struct v4l2_queryctrl* sensor_controls;  
	struct sensor_v4l2ctrl_info_s *ctrls;
	struct rk_flash_timer flash_off_timer;
	struct sensor_ops_cb_s sensor_cb;
	struct rk_sensor_focus_op_s sensor_focus;
	struct rk29camera_platform_data *sensor_io_request;
	struct rk29camera_gpio_res *sensor_gpio_res;
	
};

extern int generic_sensor_softreset(struct i2c_client *client, struct rk_sensor_reg *series);
extern int generic_sensor_check_id(struct i2c_client *client, struct rk_sensor_reg *series);
extern int sensor_write_reg2val1(struct i2c_client *client, u16 reg,u8 val);
extern int sensor_write_reg2val2(struct i2c_client *client, u16 reg,u16 val);
extern int sensor_write_reg1val1(struct i2c_client *client, u8 reg,u8 val);
extern int sensor_write_reg1val2(struct i2c_client *client, u8 reg,u16 val);
extern int sensor_read_reg1val1(struct i2c_client *client, u8 reg,u8* val);
extern int sensor_read_reg2val1(struct i2c_client *client, u16 reg,u8* val);
extern int sensor_read_reg1val2(struct i2c_client *client, u8 reg,u16* val);
extern int sensor_read_reg2val2(struct i2c_client *client, u16 reg,u16* val);
extern int generic_sensor_write(struct i2c_client *client,struct rk_sensor_reg* sensor_reg);
extern int generic_sensor_read(struct i2c_client *client, struct rk_sensor_reg* sensor_reg);
extern int generic_sensor_write_array(struct i2c_client *client, struct rk_sensor_reg *regarray);
extern int generic_sensor_get_max_min_res(struct rk_sensor_sequence* res_array,int num,struct rk_sensor_seq_info * max_real_res
										,struct rk_sensor_seq_info * max_res,struct rk_sensor_seq_info *min_res);
extern int generic_sensor_init(struct v4l2_subdev *sd, u32 val);
extern int generic_sensor_enum_frameintervals(struct v4l2_subdev *sd, struct v4l2_frmivalenum *fival);
extern int generic_sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf);
extern int generic_sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on);
extern unsigned long generic_sensor_query_bus_param(struct soc_camera_device *icd);
extern int generic_sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf);
extern int generic_sensor_set_bus_param(struct soc_camera_device *icd,unsigned long flags);
extern int generic_sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
extern int generic_sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
extern int generic_sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl);
extern int generic_sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl);
extern int generic_sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl);
extern int generic_sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl);
extern long generic_sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
extern int generic_sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,enum v4l2_mbus_pixelcode *code);
extern int generic_sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf);
extern int generic_sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id);
extern int generic_sensor_af_workqueue_set(struct soc_camera_device *icd, enum rk_sensor_focus_wq_cmd cmd, int var, bool wait);
extern int generic_sensor_s_stream(struct v4l2_subdev *sd, int enable);
extern int generic_sensor_writebuf(struct i2c_client *client, char *buf, int buf_size);

static inline int sensor_get_full_width_height(int full_resolution, unsigned short *w, unsigned short *h)
{
    switch (full_resolution) 
    {
        case 0x30000:
        {
            *w = 640;
            *h = 480;
            break;
        } 

        case 0x100000:
        {
            *w = 1024;
            *h = 768;
            break;
        }   
        
        case 0x130000:
        {
            *w = 1280;
            *h = 1024;
            break;
        }

        case 0x200000:
        {
            *w = 1600;
            *h = 1200;
            break;
        }

        case 0x300000:
        {
            *w = 2048;
            *h = 1536;
            break;
        }

        case 0x500000:
        {
            *w = 2592;
            *h = 1944;
            break;
        }

        case 0x800000:
        {
            *w = 3264;
            *h = 2448;
            break;
        }
        
        default:
            return -1;
    }

    return 0;
}
static inline int sensor_video_probe(struct soc_camera_device *icd,
				   struct i2c_client *client)
{
	int ret;
	struct generic_sensor *sensor = to_generic_sensor(client);

	/* We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant. */
	if (!icd->dev.parent ||
		to_soc_camera_host(icd->dev.parent)->nr != icd->iface) {
		ret = -ENODEV;
		goto sensor_video_probe_end;
	}

	generic_sensor_softreset(client,sensor->info_priv.sensor_SfRstSeqe);
    ret = generic_sensor_check_id(client,sensor->info_priv.sensor_CkIdSeqe);

sensor_video_probe_end: 
	return ret;
}

static inline int sensor_regarray_check(struct rk_sensor_reg *data, int reg_num)
{
    struct rk_sensor_reg *data_ptr;

    data_ptr = data+reg_num-1;
    if (data_ptr->reg == SEQCMD_END) {
        return 0;
    } else { 
        printk(KERN_ERR "%s(%d): data[%d].reg = 0x%x\n",__FUNCTION__,__LINE__,reg_num-1,data_ptr->reg);
        return -1;
    }
    
}

static inline void sensor_v4l2ctrl_info_init (struct sensor_v4l2ctrl_info_s *ptr,
                                        unsigned int id,
                                        enum v4l2_ctrl_type type,
                                        char *name,
                                        int min,
                                        int max,
                                        int step,
                                        int default_val,
                                        int(*cb)(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                 struct v4l2_ext_control *ext_ctrl),
                                        struct rk_sensor_reg ** sensor_seqe
                                        )
{
    ptr->qctrl->id = id;
	ptr->qctrl->type = type;
	strcat(ptr->qctrl->name,name);
	ptr->qctrl->minimum = min;
	ptr->qctrl->maximum = max;
    ptr->qctrl->step = step;
	ptr->qctrl->default_value = default_val;
    ptr->cur_value = default_val;
    ptr->cb = cb;
    ptr->sensor_Seqe = sensor_seqe;
    return;
}

static inline struct sensor_v4l2ctrl_info_s* sensor_find_ctrl(
	struct sensor_v4l2ctrl_info_s *ops, int id)
{
	int i;

	for (i = 0; i < ops[0].num_ctrls; i++)
		if (ops[i].qctrl->id == id)
			return &ops[i];

	return NULL;
}

static inline void v4l2_querymenu_init (struct v4l2_querymenu *ptr,
                                        unsigned int id,
                                        unsigned int index,
                                        char *name,
                                        unsigned int reserved)
{
    ptr->id = id;
	ptr->index = index;
	strcat(ptr->name,name);
	ptr->reserved = reserved;

    return;
}

static inline int sensor_v4l2ctrl_replace_cb(struct generic_sensor *sensor, int id, void *cb)
{
    int i,num;
    struct sensor_v4l2ctrl_info_s* ctrls;

    ctrls = sensor->ctrls;
    num = ctrls->num_ctrls;
    for (i=0; i<num; i++,ctrls++) {
        if (ctrls->qctrl->id == id) {
            ctrls->cb = cb;
            break;
        }
    }

    if (i>=num) {
        printk(KERN_ERR "%s(%d): v4l2_control id(0x%x) isn't exist\n",__FUNCTION__,__LINE__,id);
    } else {
        return 0;
    }
}

static inline int sensor_v4l2ctrl_default_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));    
    int value = ext_ctrl->value;
    int index;

    if ((value < ctrl_info->qctrl->minimum) || (value > ctrl_info->qctrl->maximum)) {
        printk(KERN_ERR "%s(%d): value(0x%x) isn't between in (0x%x,0x%x)\n",__FUNCTION__,__LINE__,value,
            ctrl_info->qctrl->minimum,ctrl_info->qctrl->maximum);
        return -EINVAL;
    }

    index = value - ctrl_info->qctrl->minimum;
    if (ctrl_info->sensor_Seqe && (ctrl_info->sensor_Seqe[index] != NULL)) {
        if (generic_sensor_write_array(client, ctrl_info->sensor_Seqe[index]) != 0) {
            printk(KERN_ERR "%s(%d):  sensor write array sensor_Seqe failed\n",__FUNCTION__,__LINE__);
            return -EINVAL;
        }

        ctrl_info->cur_value = value;
        return 0;
    } else {
        printk(KERN_ERR "%s(%d): ctrl_info(id=0x%x)'s sensor_Seqe is invalidate\n",__FUNCTION__,__LINE__,ctrl_info->qctrl->id);
        return -EINVAL;
    }
}
static inline int sensor_v4l2ctrl_flash_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl)
{
    //struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    int value = ext_ctrl->value;

    if ((value < ctrl_info->qctrl->minimum) || (value > ctrl_info->qctrl->maximum)) {
        printk(KERN_ERR "%s(%d): value(0x%x) isn't between in (0x%x,0x%x)\n",__FUNCTION__,__LINE__,value,
            ctrl_info->qctrl->minimum,ctrl_info->qctrl->maximum);
        return -EINVAL;
    }

    if (value == 3) {		 /* ddl@rock-chips.com: torch */
        generic_sensor_ioctrl(icd, Sensor_Flash, Flash_Torch);   /* Flash On */
    } else {
        generic_sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
    }
    
    ctrl_info->cur_value = value;  /* ddl@rock-chips.com : v0.1.3 */
    
    return 0;
}
static inline int sensor_focus_default_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));	  
	int value = ext_ctrl->value;
	int ret = 0;
	struct generic_sensor* sensor = to_generic_sensor(client);
    
	if ((value < ctrl_info->qctrl->minimum) || (value > ctrl_info->qctrl->maximum)) {
		printk(KERN_ERR "%s(%d): value(0x%x) isn't between in (0x%x,0x%x)\n",__FUNCTION__,__LINE__,value,
			ctrl_info->qctrl->minimum,ctrl_info->qctrl->maximum);
		return -EINVAL;
	}
    
	if(sensor->sensor_focus.focus_state == FocusState_Inval){
		printk(KERN_ERR "%s(%d): focus have not been init success yet\n",__FUNCTION__,__LINE__);
		//set focus delay
		
		switch (ext_ctrl->id)
		{
			case V4L2_CID_FOCUS_ABSOLUTE:
				sensor->sensor_focus.focus_delay = WqCmd_af_special_pos;
				break;
			case V4L2_CID_FOCUS_RELATIVE:
				if (ext_ctrl->value == ctrl_info->qctrl->minimum)
					sensor->sensor_focus.focus_delay = WqCmd_af_near_pos;
				else
					sensor->sensor_focus.focus_delay = WqCmd_af_far_pos;
				break;
			
			case V4L2_CID_FOCUS_AUTO:
				sensor->sensor_focus.focus_delay = WqCmd_af_single;
				break;
			case V4L2_CID_FOCUS_CONTINUOUS:
				sensor->sensor_focus.focus_delay = WqCmd_af_continues;
				break;
			default:
				printk(KERN_ERR "%s(%d):not support this focus mode",__FUNCTION__,__LINE__ );
		}
		return -EINVAL;
	}
	switch (ext_ctrl->id)
	{
		case V4L2_CID_FOCUS_ABSOLUTE:
			{
				if(sensor->sensor_focus.focus_mode ==V4L2_CID_FOCUS_CONTINUOUS){
					//need do something?

				}
				if (ctrl_info->cur_value != value) {
					if (ext_ctrl->value == ctrl_info->qctrl->minimum)
						ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_near_pos, value, true);
					else if(ext_ctrl->value == ctrl_info->qctrl->maximum)
						ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_far_pos, value, true);
					else
						ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_special_pos, value, true);
					if(ret == 0){
						ctrl_info->cur_value = value;
					} else {
						ret = -EINVAL;
						printk(KERN_ERR"\n %s valure = %d is invalidate..	\n",__FUNCTION__,value);
					}
				}
				break;
			}
		case V4L2_CID_FOCUS_RELATIVE:
			{
				if(sensor->sensor_focus.focus_mode ==V4L2_CID_FOCUS_CONTINUOUS){
					//need do something?
				
				}
				if (ctrl_info->cur_value != value) {
					if (ext_ctrl->value == ctrl_info->qctrl->minimum)
						ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_near_pos, value, true);
					else if(ext_ctrl->value == ctrl_info->qctrl->maximum)
						ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_far_pos, value, true);
					if(ret == 0){
						ctrl_info->cur_value = value;
					} else {
						ret = -EINVAL;
						printk(KERN_ERR"\n %s valure = %d is invalidate..	\n",__FUNCTION__,value);
					}
				}
				break;
			}
		case V4L2_CID_FOCUS_AUTO:
			{
                mutex_lock(&sensor->sensor_focus.focus_lock);
                //get focuszone
                sensor->sensor_focus.focus_zone.lx = ext_ctrl->rect[0];
                sensor->sensor_focus.focus_zone.ty = ext_ctrl->rect[1];
                sensor->sensor_focus.focus_zone.rx = ext_ctrl->rect[2];
                sensor->sensor_focus.focus_zone.dy = ext_ctrl->rect[3];
                mutex_unlock(&sensor->sensor_focus.focus_lock);
              
				if(sensor->sensor_focus.focus_mode ==V4L2_CID_FOCUS_CONTINUOUS){
					//need do something?
					//generic_sensor_af_workqueue_set(icd, WqCmd_af_close, value, true);
				}
				if((value==1) || (sensor->sensor_focus.focus_mode==V4L2_CID_FOCUS_AUTO)){
					ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_update_zone, value, true);
					ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_single, value, true);
                    sensor->sensor_focus.focus_mode = V4L2_CID_FOCUS_AUTO;
				}else if(value == 0){
					ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_close, value, true);
				}
				if(ret != 0){
					ret = -EINVAL;
					printk(KERN_ERR"\n %s valure = %d is invalidate..	\n",__FUNCTION__,value);
				}
				break;
				
			}
		case V4L2_CID_FOCUS_CONTINUOUS:
			{
				if((value==1) && (sensor->sensor_focus.focus_mode!=V4L2_CID_FOCUS_CONTINUOUS)){
					//have to close focus firstly?
					//generic_sensor_af_workqueue_set(icd, WqCmd_af_close, value, true);
					ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_continues, value, true);

                    sensor->sensor_focus.focus_mode = V4L2_CID_FOCUS_CONTINUOUS;
				}else if(value ==0){
					ret = generic_sensor_af_workqueue_set(icd, WqCmd_af_close, value, true);
				}
				if(ret != 0){
					ret = -EINVAL;
					printk(KERN_ERR"\n %s valure = %d is invalidate..	\n",__FUNCTION__,value);
				}
				break;
			}
		
	}
	return ret;

}
static inline int sensor_face_detect_default_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));	  
	int value = ext_ctrl->value;
	int ret = 0;
	struct generic_sensor* sensor = to_generic_sensor(client);
	if ((value < ctrl_info->qctrl->minimum) || (value > ctrl_info->qctrl->maximum)) {
		printk(KERN_ERR "%s(%d): value(0x%x) isn't between in (0x%x,0x%x)\n",__FUNCTION__,__LINE__,value,
			ctrl_info->qctrl->minimum,ctrl_info->qctrl->maximum);
		return -EINVAL;
	}
	if(ctrl_info->cur_value != value){
		if(sensor->sensor_cb.sensor_face_detect_cb)
			ret = (sensor->sensor_cb.sensor_face_detect_cb)(client,value);
		if(ret ==0)
			ctrl_info->cur_value = value;
	}
	return ret;
}
#define new_user_v4l2ctrl(ctl_id,ctl_type,ctl_name,ctl_min,ctl_max,ctl_step,default_val,callback,seqe)\
{\
    .qctrl = {\
		.id 	= ctl_id,\
		.type		= ctl_type,\
		.name		= ctl_name,\
		.minimum	= ctl_min,\
		.maximum	= ctl_max,\
		.step		= ctl_step,\
		.default_value = default_val,\
	},\
	.cb = callback,\
	.sensor_Seqe = seqe,\
}
    

#define new_usr_v4l2menu(menu_id,menu_idx,menu_name,menu_rev)\
    {\
        .id = menu_id,\
        .index = menu_idx,\
        .name = menu_name,\
        .reserved = menu_rev,\
    }

#define sensor_init_parameters_default_code() static void sensor_init_parameters(struct specific_sensor* spsensor,struct soc_camera_device *icd)\
{ \
    int num,i; \
    struct rk_sensor_sequence *sensor_series; \
    struct v4l2_queryctrl *controls, *control; \
    struct sensor_v4l2ctrl_info_s *ctrls; \
    struct v4l2_querymenu *menus,*menu; \
    struct soc_camera_link *icl = to_soc_camera_link(icd); \
    struct rk29camera_platform_data *pdata = icl->priv_usr; \
    struct rkcamera_platform_data *sensor_device=NULL,*new_camera; \
    struct rk_sensor_reg *reg_data; \
    int config_flash = 0;\
    int sensor_config;\
 \
    if (pdata == NULL) {\
        printk("WARNING: Camera sensor device is registered in board by CONFIG_SENSOR_XX,\n"\
               "Please register camera sesnor deivce in struct rkcamera_platform_data new_camera[]\n");\
        BUG();\
    }\
    sensor_config = SensorConfiguration;\
    new_camera = pdata->register_dev_new; \
    while (strstr(new_camera->dev_name,"end")==NULL) { \
        if (strcmp(dev_name(icd->pdev), new_camera->dev_name) == 0) { \
            sensor_device = new_camera; \
            break; \
        } \
        new_camera++; \
    } \
 \
    if(sensor_device && sensor_device->flash)\
 	    config_flash = 1;\
    spsensor->common_sensor.info_priv.gReg_mask = 0x00; \
    spsensor->common_sensor.info_priv.gVal_mask = 0x00; \
    for (i=0; i<SENSOR_REGISTER_LEN; i++) \
	    spsensor->common_sensor.info_priv.gReg_mask |= (0xff<<(i*8));  \
    for (i=0; i<SENSOR_VALUE_LEN; i++) \
	    spsensor->common_sensor.info_priv.gVal_mask |= (0xff<<(i*8));  \
	spsensor->common_sensor.info_priv.gI2c_speed = 100000;  \
    if (sensor_regarray_check(sensor_softreset_data, sizeof(sensor_softreset_data)/sizeof(struct rk_sensor_reg))==0) { \
        spsensor->common_sensor.info_priv.sensor_SfRstSeqe = sensor_softreset_data; \
    } else { \
        SENSOR_TR("sensor_softreset_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_softreset_data"); \
        BUG(); \
    } \
    if (sensor_regarray_check(sensor_check_id_data, sizeof(sensor_check_id_data)/sizeof(struct rk_sensor_reg))==0) { \
        spsensor->common_sensor.info_priv.sensor_CkIdSeqe= sensor_check_id_data; \
    } else { \
        SENSOR_TR("sensor_check_id_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_check_id_data"); \
        BUG(); \
    } \
    spsensor->common_sensor.sensor_cb.sensor_activate_cb = sensor_activate_cb;  \
	spsensor->common_sensor.sensor_cb.sensor_deactivate_cb = sensor_deactivate_cb; \
	spsensor->common_sensor.sensor_cb.sensor_mirror_cb = sensor_mirror_cb; \
	spsensor->common_sensor.sensor_cb.sensor_flip_cb = sensor_flip_cb; \
    spsensor->common_sensor.sensor_cb.sensor_s_fmt_cb_th = sensor_s_fmt_cb_th; \
    spsensor->common_sensor.sensor_cb.sensor_s_fmt_cb_bh = sensor_s_fmt_cb_bh; \
	spsensor->common_sensor.sensor_cb.sensor_try_fmt_cb_th = sensor_try_fmt_cb_th;\
    spsensor->common_sensor.sensor_cb.sensor_softreset_cb = sensor_softrest_usr_cb;\
	spsensor->common_sensor.sensor_cb.sensor_check_id_cb = sensor_check_id_usr_cb;\
    if (CFG_FunChk(sensor_config,CFG_FACE_DETECT)) \
	spsensor->common_sensor.sensor_cb.sensor_face_detect_cb = sensor_face_detect_usr_cb; \
    else \
	spsensor->common_sensor.sensor_cb.sensor_face_detect_cb = NULL; \
 \
    num = 4;  \
    if (sensor_720p[0].reg != SEQCMD_END) {  \
        num++;  \
    }  \
    if (sensor_1080p[0].reg != SEQCMD_END) {  \
        num++;  \
    } \
 \
    if (sensor_device && (sensor_device->resolution > CONS(SENSOR_NAME,_FULL_RESOLUTION))) \
        num++; \
 \
    sensor_series = (struct rk_sensor_sequence*)kzalloc(sizeof(struct rk_sensor_sequence)*num,GFP_KERNEL);  \
    if (sensor_series == NULL) {  \
        SENSOR_TR("malloc sensor_series failed! n");  \
        BUG();  \
    } else {  \
        spsensor->common_sensor.info_priv.sensor_series = sensor_series;  \
        spsensor->common_sensor.info_priv.num_series = num;  \
 \
        sensor_series->gSeq_info.w = SENSOR_PREVIEW_W;  \
        sensor_series->gSeq_info.h = SENSOR_PREVIEW_H;  \
        sensor_series->gSeq_info.fps = SENSOR_PREVIEW_FPS;  \
        sensor_series->property = SEQUENCE_INIT;  \
        if (sensor_regarray_check(sensor_init_data, sizeof(sensor_init_data)/sizeof(struct rk_sensor_reg))==0) { \
            sensor_series->data = sensor_init_data; \
        } else { \
            SENSOR_TR("sensor_init_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_int_data"); \
            BUG(); \
        } \
 \
        sensor_series++;  \
        sensor_series->gSeq_info.w = SENSOR_PREVIEW_W;  \
        sensor_series->gSeq_info.h = SENSOR_PREVIEW_H;  \
        sensor_series->gSeq_info.fps = SENSOR_PREVIEW_FPS;  \
        sensor_series->property = SEQUENCE_PREVIEW;  \
        if (sensor_regarray_check(sensor_preview_data, sizeof(sensor_preview_data)/sizeof(struct rk_sensor_reg))==0) { \
            sensor_series->data = sensor_preview_data; \
        } else { \
            SENSOR_TR("sensor_preview_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_preview_data"); \
            BUG(); \
        } \
 \
        sensor_series++;  \
        if (sensor_get_full_width_height(CONS(SENSOR_NAME,_FULL_RESOLUTION),&sensor_series->gSeq_info.w,&sensor_series->gSeq_info.h) == 0) {  \
            sensor_series->gSeq_info.fps = SENSOR_FULLRES_L_FPS; \
            sensor_series->property = SEQUENCE_CAPTURE; \
            if (sensor_regarray_check(sensor_fullres_lowfps_data, sizeof(sensor_fullres_lowfps_data)/sizeof(struct rk_sensor_reg))==0) { \
                sensor_series->data = sensor_fullres_lowfps_data; \
            } else { \
                SENSOR_TR("sensor_fullres_lowfps_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_fullres_lowfps_data"); \
                BUG(); \
            } \
        } else {  \
            SENSOR_TR("generic_sensor_get_width_height failed!");  \
            BUG();  \
        }  \
 \
        sensor_series++;  \
        sensor_series->gSeq_info.w = (sensor_series-1)->gSeq_info.w; \
        sensor_series->gSeq_info.h = (sensor_series-1)->gSeq_info.h; \
        sensor_series->gSeq_info.fps = SENSOR_FULLRES_H_FPS;  \
        sensor_series->property = SEQUENCE_PREVIEW; \
        if (sensor_regarray_check(sensor_fullres_highfps_data, sizeof(sensor_fullres_highfps_data)/sizeof(struct rk_sensor_reg))==0) { \
            sensor_series->data = sensor_fullres_highfps_data; \
        } else { \
            SENSOR_TR("sensor_fullres_highfps_data haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_fullres_highfps_data"); \
            BUG(); \
        } \
 \
        if (sensor_device && (sensor_device->resolution > CONS(SENSOR_NAME,_FULL_RESOLUTION))) { \
            sensor_series++; \
            if (sensor_get_full_width_height(sensor_device->resolution,&sensor_series->gSeq_info.w,&sensor_series->gSeq_info.h) == 0) { \
                sensor_series->gSeq_info.fps = SENSOR_FULLRES_L_FPS; \
                sensor_series->property = SEQUENCE_CAPTURE; \
                reg_data = kzalloc(sizeof(struct rk_sensor_reg)*2,GFP_KERNEL); \
                if (reg_data == NULL) { \
                    SENSOR_TR("kzalloc interpolate reg_data failed"); \
                } else { \
                    sensor_series->data = reg_data; \
                    reg_data->reg = SEQCMD_INTERPOLATION; \
                    reg_data++; \
                    reg_data->reg = SEQCMD_END; \
                } \
            } else {  \
                SENSOR_TR("generic_sensor_get_width_height failed!");  \
                BUG();  \
            } \
        } \
 \
        if (sensor_720p[0].reg != SEQCMD_END) {  \
            sensor_series++;  \
            sensor_series->gSeq_info.w = 1280;  \
            sensor_series->gSeq_info.h = 720;  \
            sensor_series->gSeq_info.fps = SENSOR_720P_FPS;  \
            sensor_series->property = SEQUENCE_PREVIEW;  \
            if (sensor_regarray_check(sensor_720p, sizeof(sensor_720p)/sizeof(struct rk_sensor_reg))==0) { \
                sensor_series->data = sensor_720p; \
            } else { \
                SENSOR_TR("sensor_720p haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_720p"); \
                BUG(); \
            } \
        }  \
 \
        if (sensor_1080p[0].reg != SEQCMD_END) {  \
            sensor_series++;  \
            sensor_series->gSeq_info.w = 1920;  \
            sensor_series->gSeq_info.h = 1080;  \
            sensor_series->gSeq_info.fps = SENSOR_1080P_FPS;  \
            sensor_series->property = SEQUENCE_PREVIEW;  \
            if (sensor_regarray_check(sensor_1080p, sizeof(sensor_1080p)/sizeof(struct rk_sensor_reg))==0) { \
                sensor_series->data = sensor_1080p; \
            } else { \
                SENSOR_TR("sensor_1080p haven't SensorEnd flag! Please fill SensorEnd in the last of sensor_1080p"); \
                BUG(); \
            } \
        }  \
    } \
 \
    if (CFG_FunChk(sensor_config,CFG_Focus)) { \
        spsensor->common_sensor.sensor_focus.sensor_wq = create_workqueue(SENSOR_NAME_STRING(_af_workqueue)); \
        if (spsensor->common_sensor.sensor_focus.sensor_wq == NULL) {\
            SENSOR_TR("%s create fail, so auto focus is disable!", SENSOR_NAME_STRING(_af_workqueue));\
            CFG_FunDis(sensor_config,CFG_Focus);\
            CFG_FunDis(sensor_config,CFG_FocusContinues);\
            CFG_FunDis(sensor_config,CFG_FocusZone);\
            CFG_FunDis(sensor_config,CFG_FocusRelative);\
            CFG_FunDis(sensor_config,CFG_FocusAbsolute);\
        }\
    } else {\
        spsensor->common_sensor.sensor_focus.sensor_wq = NULL;\
        CFG_FunDis(sensor_config,CFG_FocusContinues);\
        CFG_FunDis(sensor_config,CFG_FocusZone);\
        CFG_FunDis(sensor_config,CFG_FocusRelative);\
        CFG_FunDis(sensor_config,CFG_FocusAbsolute);\
    }\
\
	spsensor->common_sensor.info_priv.bus_parameter = SENSOR_BUS_PARAM; \
	spsensor->common_sensor.info_priv.chip_ident = SENSOR_V4L2_IDENT; \
    spsensor->common_sensor.info_priv.chip_id = SensorChipID;\
    spsensor->common_sensor.info_priv.chip_id_num = sizeof(SensorChipID)/sizeof(SensorChipID[0]);\
\
    generic_sensor_get_max_min_res(spsensor->common_sensor.info_priv.sensor_series ,  \
                                   spsensor->common_sensor.info_priv.num_series,  \
                                   &(spsensor->common_sensor.info_priv.max_real_res),  \
								   &(spsensor->common_sensor.info_priv.max_res),  \
								   &(spsensor->common_sensor.info_priv.min_res));  \
 \
    num = 0;\
    for (i=0; i<32; i++)\
        if (SensorConfiguration & (1<<i))\
            num++;\
    num += sizeof(sensor_controls)/sizeof(struct sensor_v4l2ctrl_usr_s);  \
    num += config_flash;\
    controls = (struct v4l2_queryctrl*)kzalloc(sizeof(struct v4l2_queryctrl)*num,GFP_KERNEL);  \
    if (controls == NULL) {  \
        SENSOR_TR("kzalloc struct v4l2_queryctrl(%d) failed",num);  \
        BUG();  \
    }  \
    spsensor->common_sensor.sensor_controls = controls;  \
    sensor_ops.controls = controls;  \
    sensor_ops.num_controls = num;  \
  \
    ctrls = (struct sensor_v4l2ctrl_info_s*)kzalloc(sizeof(struct sensor_v4l2ctrl_info_s)*num,GFP_KERNEL);  \
    if (ctrls == NULL) {  \
        SENSOR_TR("kzalloc struct sensor_v4l2ctrl_info_s(%d) failed",num);  \
        BUG();  \
    }  \
    spsensor->common_sensor.ctrls = ctrls;  \
    for (i=0; i<num; i++) {  \
        ctrls->qctrl = controls; \
        ctrls->num_ctrls = num;  \
        ctrls++;  \
        controls++; \
    }  \
    controls = spsensor->common_sensor.sensor_controls; \
    ctrls = spsensor->common_sensor.ctrls;  \
 \
    num = 0;  \
    num += (CFG_FunChk(sensor_config,CFG_WhiteBalance)*5 + CFG_FunChk(sensor_config,CFG_Effect)*6 + CFG_FunChk(sensor_config,CFG_Scene)*2 + config_flash*4); \
    num += sizeof(sensor_menus)/sizeof(struct v4l2_querymenu);  \
    menus = (struct v4l2_querymenu*)kzalloc(sizeof(struct v4l2_querymenu)*num,GFP_KERNEL);  \
    if (menus == NULL) {  \
        SENSOR_TR("kzalloc struct v4l2_querymenu(%d) failed",num);  \
        BUG();  \
    }  \
    sensor_ops.menus = menus;  \
    sensor_ops.num_menus = num;  \
 \
    sensor_ops.suspend = sensor_suspend;  \
    sensor_ops.resume = sensor_resume;  \
    sensor_ops.set_bus_param = generic_sensor_set_bus_param;  \
	sensor_ops.query_bus_param = generic_sensor_query_bus_param;  \
 \
    if (sizeof(sensor_ZoomSeqe)/sizeof(struct rk_sensor_reg *))\
        sensor_ZoomSeqe[0] = NULL;\
\
    if (CFG_FunChk(sensor_config,CFG_WhiteBalance)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_DO_WHITE_BALANCE,V4L2_CTRL_TYPE_MENU,  \
                            "White Balance Control",0,4,1,0,sensor_v4l2ctrl_default_cb,sensor_WhiteBalanceSeqe);  \
        controls++; \
        ctrls++;  \
 \
        v4l2_querymenu_init(menus,V4L2_CID_DO_WHITE_BALANCE,0,"auto",0); \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_DO_WHITE_BALANCE,1,"incandescent",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_DO_WHITE_BALANCE,2,"fluorescent",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_DO_WHITE_BALANCE,3,"daylight",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_DO_WHITE_BALANCE,4,"cloudy-daylight",0);  \
        menus++;  \
    }  \
 \
    if (CFG_FunChk(sensor_config,CFG_Brightness)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_BRIGHTNESS,V4L2_CTRL_TYPE_INTEGER,  \
                            "Brightness Control",-3,2,1,0,sensor_v4l2ctrl_default_cb,sensor_BrightnessSeqe);  \
        controls++;  \
        ctrls++;  \
    }  \
    if (CFG_FunChk(sensor_config,CFG_Effect)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_EFFECT,V4L2_CTRL_TYPE_MENU, \
                            "Effect Control",0,5,1,0,sensor_v4l2ctrl_default_cb,sensor_EffectSeqe); \
        controls++; \
        ctrls++;  \
 \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,0,"none",0);  \
        menus++; \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,1,"mono",0);  \
        menus++; \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,2,"negative",0); \
        menus++; \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,3,"sepia",0); \
        menus++; \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,4,"posterize",0); \
        menus++; \
        v4l2_querymenu_init(menus,V4L2_CID_EFFECT,5,"aqua",0); \
        menus++; \
    } \
    if (CFG_FunChk(sensor_config,CFG_Exposure)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_EXPOSURE,V4L2_CTRL_TYPE_INTEGER, \
                            "Exposure Control",0,6,1,0,sensor_v4l2ctrl_default_cb,sensor_ExposureSeqe);  \
        controls++;  \
        ctrls++;  \
    }  \
	if (CFG_FunChk(sensor_config,CFG_Saturation)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_SATURATION,V4L2_CTRL_TYPE_INTEGER,  \
                            "Saturation Control",0,2,1,0,sensor_v4l2ctrl_default_cb,sensor_SaturationSeqe);  \
        controls++;  \
        ctrls++;  \
	}  \
	if (CFG_FunChk(sensor_config,CFG_Contrast)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_CONTRAST,V4L2_CTRL_TYPE_INTEGER,  \
                            "Contrast Control",-3,3,1,0,sensor_v4l2ctrl_default_cb,sensor_ContrastSeqe);  \
        controls++;  \
        ctrls++;  \
	}  \
	if (CFG_FunChk(sensor_config,CFG_Mirror)) {  \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_HFLIP,V4L2_CTRL_TYPE_BOOLEAN, \
                            "Mirror Control",0,1,1,0,sensor_v4l2ctrl_mirror_cb,NULL); \
        controls++;  \
        ctrls++;  \
	}  \
	if (CFG_FunChk(sensor_config,CFG_Flip)) {  \
        ctrls->qctrl = controls; \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_VFLIP,V4L2_CTRL_TYPE_BOOLEAN,  \
                            "Flip Control",0,1,1,0,sensor_v4l2ctrl_flip_cb,NULL); \
        controls++;  \
        ctrls++;  \
	}  \
    if (CFG_FunChk(sensor_config,CFG_Scene)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_SCENE,V4L2_CTRL_TYPE_MENU,  \
                            "Scene Control",0,1,1,0,sensor_v4l2ctrl_default_cb,sensor_SceneSeqe);  \
        controls++;  \
        ctrls++;  \
  \
        v4l2_querymenu_init(menus,V4L2_CID_SCENE,0,"auto",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_SCENE,1,"night",0);  \
        menus++;  \
    }  \
    if (CFG_FunChk(sensor_config,CFG_Focus)) { \
	   sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FOCUS_AUTO,V4L2_CTRL_TYPE_BOOLEAN,  \
						   "Focus Control",0,2,1,0,sensor_focus_default_cb,NULL);  \
	   controls++;  \
	   ctrls++;  \
    } \
 \
    if (CFG_FunChk(sensor_config,CFG_FocusRelative)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FOCUS_RELATIVE,V4L2_CTRL_TYPE_INTEGER,  \
                            "Focus Control",-1,1,1,0,sensor_focus_default_cb,NULL);  \
        controls++;  \
        ctrls++;  \
   	} \
  \
   if (CFG_FunChk(sensor_config,CFG_FocusAbsolute)) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FOCUS_ABSOLUTE,V4L2_CTRL_TYPE_INTEGER,  \
                            "Focus Control",0,255,1,125,sensor_focus_default_cb,NULL);  \
        controls++;  \
        ctrls++;  \
    }  \
    if (CFG_FunChk(sensor_config,CFG_FocusZone)) { \
   	    sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FOCUSZONE,V4L2_CTRL_TYPE_BOOLEAN,  \
   						"Focus Control",0,1,1,0,NULL,NULL);  \
   	    controls++;  \
   	    ctrls++;  \
    } \
   if (CFG_FunChk(sensor_config,CFG_FocusContinues)) { \
	   sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FOCUS_CONTINUOUS,V4L2_CTRL_TYPE_BOOLEAN,  \
						   "Focus Control",0,1,1,0,sensor_focus_default_cb,NULL);  \
	   controls++;  \
	   ctrls++;  \
    } \
   if (CFG_FunChk(sensor_config,CFG_FACE_DETECT)) { \
	   sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FACEDETECT,V4L2_CTRL_TYPE_BOOLEAN,  \
						   "FaceDEt Control",0,1,1,0,sensor_face_detect_default_cb,NULL);  \
	   controls++;  \
	   ctrls++;  \
	} \
    if (config_flash) { \
        sensor_v4l2ctrl_info_init(ctrls,V4L2_CID_FLASH,V4L2_CTRL_TYPE_MENU,  \
                            "Flash Control",0,3,1,0,sensor_v4l2ctrl_flash_cb,NULL);  \
        controls++;  \
        ctrls++;  \
  \
        v4l2_querymenu_init(menus,V4L2_CID_FLASH,0,"off",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_FLASH,1,"auto",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_FLASH,2,"on",0);  \
        menus++;  \
        v4l2_querymenu_init(menus,V4L2_CID_FLASH,3,"torch",0);  \
        menus++;  \
    }  \
  \
    for (i=0; i<(sizeof(sensor_controls)/sizeof(struct sensor_v4l2ctrl_usr_s)); i++) { \
 \
        control = spsensor->common_sensor.sensor_controls; \
        while (control < controls) \
        { \
            if (control->id == sensor_controls[i].qctrl.id) { \
                control->id = 0xffffffff; \
            } \
            control++; \
        } \
 \
        memcpy(controls, &sensor_controls[i].qctrl,sizeof(struct v4l2_queryctrl));  \
        controls++;  \
 \
        ctrls->sensor_Seqe = sensor_controls[i].sensor_Seqe; \
        ctrls->cur_value = sensor_controls[i].qctrl.default_value; \
        ctrls->cb = sensor_controls[i].cb; \
        ctrls++; \
    }  \
 \
    for (i=0; i<(sizeof(sensor_menus)/sizeof(struct v4l2_querymenu)); i++) { \
        num = sensor_ops.num_menus - sizeof(sensor_menus)/sizeof(struct v4l2_querymenu); \
        menu = sensor_ops.menus; \
        while (num--) { \
            if (menu->id == sensor_menus[i].id) { \
                menu->id = 0xffffffff; \
            } \
            menu++; \
        } \
 \
        memcpy(menus, &sensor_menus[i],sizeof(struct v4l2_querymenu));  \
        menus++;  \
    }  \
 \
	spsensor->common_sensor.info_priv.datafmt = sensor_colour_fmts;  \
	spsensor->common_sensor.info_priv.num_datafmt = ARRAY_SIZE(sensor_colour_fmts);  \
	spsensor->common_sensor.sensor_ops = &sensor_ops;  \
	icd->ops		= &sensor_ops;  \
	spsensor->common_sensor.info_priv.curfmt= sensor_colour_fmts[0];  \
 \
    if (config_flash) { \
    	hrtimer_init(&(spsensor->common_sensor.flash_off_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);  \
        spsensor->common_sensor.flash_off_timer.icd = icd; \
    } \
    if(CFG_FunChk(sensor_config,CFG_Focus)) { \
		mutex_init(&spsensor->common_sensor.sensor_focus.focus_lock); \
		spsensor->common_sensor.sensor_focus.focus_mode = WqCmd_af_invalid; \
		spsensor->common_sensor.sensor_focus.focus_state = FocusState_Inval;\
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_focus_init_cb = sensor_focus_init_usr_cb; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_single_cb = sensor_focus_af_single_usr_cb; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_near_cb = sensor_focus_af_near_usr_cb; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_far_cb = sensor_focus_af_far_usr_cb; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_specialpos_cb = sensor_focus_af_specialpos_usr_cb; \
		if(CFG_FunChk(sensor_config,CFG_FocusContinues)) {\
			spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_const_cb = sensor_focus_af_const_usr_cb; \
			spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_const_pause_cb = sensor_focus_af_const_pause_usr_cb; \
		}\
		if(CFG_FunChk(sensor_config,CFG_FocusZone)) \
			spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_zoneupdate_cb = sensor_focus_af_zoneupdate_usr_cb; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_close_cb = sensor_focus_af_close_usr_cb; \
	}else{ \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_focus_init_cb = NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_single_cb = NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_near_cb = NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_far_cb =NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_specialpos_cb =NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_const_cb =NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_zoneupdate_cb =NULL; \
		spsensor->common_sensor.sensor_focus.focus_cb.sensor_af_close_cb =NULL; \
	} \
 \
    memcpy(spsensor->common_sensor.dev_name,dev_name(icd->pdev), sizeof(spsensor->common_sensor.dev_name)-1); \
    sensor_init_parameters_user(spsensor,icd);  \
}


#define sensor_v4l2_struct_initialization()  static struct v4l2_subdev_core_ops sensor_subdev_core_ops = {\
	.init		= generic_sensor_init,\
	.g_ctrl 	= generic_sensor_g_control,\
	.s_ctrl 	= generic_sensor_s_control,\
	.g_ext_ctrls		  = generic_sensor_g_ext_controls,\
	.s_ext_ctrls		  = generic_sensor_s_ext_controls,\
	.g_chip_ident	= generic_sensor_g_chip_ident,\
	.ioctl = generic_sensor_ioctl,\
};\
\
static struct v4l2_subdev_video_ops sensor_subdev_video_ops = {\
	.s_mbus_fmt = generic_sensor_s_fmt,\
	.g_mbus_fmt = generic_sensor_g_fmt,\
	.try_mbus_fmt	= generic_sensor_try_fmt,\
	.enum_mbus_fmt	= generic_sensor_enum_fmt,\
	.enum_frameintervals = generic_sensor_enum_frameintervals,\
	.s_stream   = generic_sensor_s_stream,\
};\
static struct v4l2_subdev_ops sensor_subdev_ops = {\
	.core	= &sensor_subdev_core_ops,\
	.video = &sensor_subdev_video_ops,\
};\
\
static const struct i2c_device_id sensor_id[] = {\
	{SENSOR_NAME_STRING(), 0 },\
    {"\0",0}\
};\
\
MODULE_DEVICE_TABLE(i2c, sensor_id);


#define sensor_probe_default_code() static int sensor_probe(struct i2c_client *client,\
			 const struct i2c_device_id *did)\
{\
	struct specific_sensor *spsensor=NULL;\
	struct soc_camera_device *icd = client->dev.platform_data;\
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);\
	struct soc_camera_link *icl;\
	int ret=0;\
\
	if (!icd) {\
		dev_err(&client->dev, "%s: missing soc-camera data!\n",SENSOR_NAME_STRING());\
		ret = -EINVAL;\
        goto sensor_probe_end;\
	}\
\
	icl = to_soc_camera_link(icd);\
	if (!icl) {\
		SENSOR_TR("driver needs platform data! But it is failed\n");\
		ret = -EINVAL;\
        goto sensor_probe_end;\
	}\
\
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {\
		SENSOR_TR("I2C-Adapter doesn't support I2C_FUNC_I2C\n");\
		ret =  -EIO;\
        goto sensor_probe_end;\
	}\
\
	spsensor = kzalloc(sizeof(struct specific_sensor), GFP_KERNEL);\
	if (!spsensor) {\
		ret = -ENOMEM;\
        SENSOR_TR("kzalloc failed\n");\
        goto sensor_probe_end;\
	}\
\
	v4l2_i2c_subdev_init(&spsensor->common_sensor.subdev, client, &sensor_subdev_ops);\
    sensor_init_parameters(spsensor,icd);\
\
    ret = sensor_video_probe(icd, client);\
\
sensor_probe_end:\
    if (ret != 0) {\
        if (icd->ops) {\
            if (icd->ops->controls) {\
                kfree(icd->ops->controls);\
                icd->ops->controls = NULL;\
            }\
            if (icd->ops->menus) {\
                kfree(icd->ops->menus);\
                icd->ops->menus = NULL;\
            }\
    	    icd->ops = NULL;\
        }\
        i2c_set_clientdata(client, NULL);\
    	client->driver = NULL;\
        if (spsensor) {\
            kfree(spsensor);\
        }\
    	spsensor = NULL;\
    }\
	return ret;\
}

#define sensor_remove_default_code() static int sensor_remove(struct i2c_client *client)\
{\
	struct generic_sensor*sensor = to_generic_sensor(client);\
	struct soc_camera_device *icd = client->dev.platform_data;\
	struct specific_sensor *spsensor = to_specific_sensor(sensor);\
	int sensor_config;\
\
    sensor_config = SensorConfiguration;\
	if(CFG_FunChk(sensor_config,CFG_Focus)){ \
		if (sensor->sensor_focus.sensor_wq) {\
			destroy_workqueue(sensor->sensor_focus.sensor_wq);\
			sensor->sensor_focus.sensor_wq = NULL;\
		}\
	}\
    if (icd->ops) {\
        if (icd->ops->controls) {\
            kfree(icd->ops->controls);\
            icd->ops->controls = NULL;\
        }\
        if (icd->ops->menus) {\
            kfree(icd->ops->menus);\
            icd->ops->menus = NULL;\
        }\
	    icd->ops = NULL;\
    }\
	i2c_set_clientdata(client, NULL);\
	client->driver = NULL;\
    if (spsensor) {\
        kfree(spsensor);\
    }\
	spsensor = NULL;\
	return 0;\
}


#define sensor_driver_default_module_code() static struct i2c_driver sensor_i2c_driver = {\
	.driver = {\
		.name = SENSOR_NAME_STRING(),\
	},\
	.probe		= sensor_probe,\
	.remove 	= sensor_remove,\
	.id_table	= sensor_id,\
};\
\
static int __init sensor_mod_init(void)\
{\
	return i2c_add_driver(&sensor_i2c_driver);\
}\
\
static void __exit sensor_mod_exit(void)\
{\
	i2c_del_driver(&sensor_i2c_driver);\
}\
\
device_initcall_sync(sensor_mod_init);\
module_exit(sensor_mod_exit);\
\
MODULE_DESCRIPTION(SENSOR_NAME_STRING(sensor driver));\
MODULE_AUTHOR("<ddl@rock-chips.com,zyc@rock-chips.com>");\
MODULE_LICENSE("GPL");
#endif
