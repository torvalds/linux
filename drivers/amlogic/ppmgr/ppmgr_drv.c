/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
#include <linux/amlogic/ppmgr/ppmgr.h>
#include <linux/amlogic/ppmgr/ppmgr_status.h>
#include <linux/platform_device.h>
#include <linux/amlogic/ge2d/ge2d_main.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/amlogic/amlog.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/of_fdt.h>


#include "ppmgr_log.h"
#include "ppmgr_pri.h"
#include "ppmgr_dev.h"
#include <linux/amlogic/ppmgr/ppmgr.h>
#include <linux/amlogic/ppmgr/ppmgr_status.h>
#include <linux/amlogic/amports/video_prot.h>

/***********************************************************************
*
* global status.
*
************************************************************************/
static int ppmgr_enable_flag=0;
static int ppmgr_flag_change = 0;
static int property_change = 0;
static int buff_change = 0;

static platform_type_t platform_type = PLATFORM_MID;
ppmgr_device_t  ppmgr_device;
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
extern void Reset3Dclear(void);
extern void Set3DProcessPara(unsigned mode);
#endif
#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
static bool scaler_pos_reset = false;
#endif

#include "../amports/amports_config.h"


platform_type_t get_platform_type(void)
{
	return	platform_type;
}

int get_bypass_mode(void)
{
    return ppmgr_device.bypass;
}

int get_property_change(void)
{
    return property_change;
}
void set_property_change(int flag)
{
    property_change = flag;
}

int get_buff_change(void)
{
    return buff_change;
}
void set_buff_change(int flag)
{
    buff_change = flag;
}

#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
bool get_scaler_pos_reset(void)
{
    return scaler_pos_reset;
}
void set_scaler_pos_reset(bool flag)
{
    scaler_pos_reset = flag;
}
#endif

int get_ppmgr_status(void) {
    return ppmgr_enable_flag;
}

void set_ppmgr_status(int flag) {
	if(flag != ppmgr_enable_flag){
		ppmgr_flag_change = 1;
	}
    if(flag >= 0){
        ppmgr_enable_flag=flag;
    }
    else {
        ppmgr_enable_flag=0;
    }
}

/***********************************************************************
*
* 3D function.
*
************************************************************************/
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
unsigned get_ppmgr_3dmode(void)
{
    return ppmgr_device.ppmgr_3d_mode;
}

void set_ppmgr_3dmode(unsigned mode)
{
    if(ppmgr_device.ppmgr_3d_mode != mode){
        ppmgr_device.ppmgr_3d_mode = mode;
        Set3DProcessPara(ppmgr_device.ppmgr_3d_mode);
        Reset3Dclear();
        //property_change = 1;
    }
}

unsigned get_ppmgr_viewmode(void)
{
    return ppmgr_device.viewmode;
}

void set_ppmgr_viewmode(unsigned mode)
{
    if((ppmgr_device.viewmode != mode)&&(mode<VIEWMODE_MAX)){
        ppmgr_device.viewmode = mode;
        Reset3Dclear();
        //property_change = 1;
    }
}

unsigned get_ppmgr_scaledown(void)
{
    return ppmgr_device.scale_down;
}

void set_ppmgr_scaledown(unsigned scale_down)
{
    if((ppmgr_device.scale_down != scale_down)&&(scale_down<3)){
        ppmgr_device.scale_down = scale_down;
        Reset3Dclear();
    }
}

unsigned get_ppmgr_direction3d(void)
{
    return ppmgr_device.direction_3d;
}

void set_ppmgr_direction3d(unsigned angle)
{
    if((ppmgr_device.direction_3d != angle)&&(angle<4)){
        ppmgr_device.direction_3d = angle;
        Reset3Dclear();
        //property_change = 1;
    }
}
#endif

/***********************************************************************
*
* Utilities.
*
************************************************************************/
static ssize_t _ppmgr_angle_write(unsigned long val)
{
    unsigned long angle = val;

    if (angle > 3) {
        if (angle == 90)
            angle = 1;
        else if (angle == 180)
            angle = 2;
        else if (angle == 270)
            angle = 3;
        else {
            printk("invalid orientation value\n");
            printk("you should set 0 or 0 for 0 clock wise,");
            printk("1 or 90 for 90 clockwise,2 or 180 for 180 clockwise");
            printk("3 or 270 for 270 clockwise\n");
            return -EINVAL;
        }
    }

    ppmgr_device.global_angle = angle;
    if (!ppmgr_device.use_prot) {
        if (angle != ppmgr_device.angle) {
            property_change = 1;
        }
        ppmgr_device.angle = angle;
        ppmgr_device.videoangle = (ppmgr_device.angle + ppmgr_device.orientation) % 4;
        printk("ppmgr angle:%x,orientation:%x,videoangle:%x \n", ppmgr_device.angle, ppmgr_device.orientation, ppmgr_device.videoangle);
    } else {
        set_video_angle(angle);
        printk("prot angle:%ld\n", angle);
    }
    return 0;
}

/***********************************************************************
*
* class property info.
*
************************************************************************/

#define    	PPMGR_CLASS_NAME   				"ppmgr"
static int parse_para(const char *para, int para_num, int *result)
{
    char *endp;
    const char *startp = para;
    int *out = result;
    int len = 0, count = 0;

    if (!startp) {
        return 0;
    }

    len = strlen(startp);

    do {
        //filter space out
        while (startp && (isspace(*startp) || !isgraph(*startp)) && len) {
            startp++;
            len--;
        }

        if (len == 0) {
            break;
        }

        *out++ = simple_strtol(startp, &endp, 0);

        len -= endp - startp;
        startp = endp;
        count++;

    } while ((endp) && (count < para_num) && (len > 0));

    return count;
}

static ssize_t show_ppmgr_info(struct class *cla,struct class_attribute *attr,char *buf)
{
    char *bstart;
    unsigned int bsize;
    get_ppmgr_buf_info(&bstart,&bsize);
    return snprintf(buf,80,"buffer:\n start:%x.\tsize:%d\n",(unsigned int)bstart,bsize/(1024*1024));
}

static ssize_t angle_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"current angel is %d\n",ppmgr_device.global_angle);

}

static ssize_t angle_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    unsigned long angle = simple_strtoul(buf, &endp, 0);

    if (angle > 3 || angle < 0) {
        size = endp - buf;
        return count;
    }

    if (_ppmgr_angle_write(angle) < 0) {
        return -EINVAL;
    }
    size = endp - buf;
    return count;
}

int get_use_prot(void) {
    return ppmgr_device.use_prot;
}
EXPORT_SYMBOL(get_use_prot);

static ssize_t disable_prot_show(struct class *cla, struct class_attribute *attr, char *buf) {
    return snprintf(buf, 40, "%d\n", ppmgr_device.disable_prot);
}

static ssize_t disable_prot_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count) {
    size_t r;
    u32 s_value;
    r = sscanf(buf, "%d", &s_value);
    if (s_value != 0 && s_value != 1) {
        return -EINVAL;
    }
    ppmgr_device.disable_prot = s_value;
    return strnlen(buf, count);
}

static ssize_t orientation_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    //ppmgr_device_t* ppmgr_dev=(ppmgr_device_t*)cla;
    return snprintf(buf,80,"current orientation is %d\n",ppmgr_device.orientation*90);
}

/* set the initial orientation for video, it should be set before video start. */
static ssize_t orientation_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t ret = -EINVAL, size;
    char *endp;
    unsigned angle  =  simple_strtoul(buf, &endp, 0);
    //if(property_change) return ret;
    if(angle>3) {
        if(angle==90) angle=1;
        else if(angle==180) angle=2;
        else if(angle==270) angle=3;
        else {
            printk("invalid orientation value\n");
            printk("you should set 0 or 0 for 0 clock wise,");
            printk("1 or 90 for 90 clockwise,2 or 180 for 180 clockwise");
            printk("3 or 270 for 270 clockwise\n");
            return ret;
        }
    }
    ppmgr_device.orientation = angle;
    ppmgr_device.videoangle = (ppmgr_device.angle+ ppmgr_device.orientation)%4;
    printk("angle:%d,orientation:%d,videoangle:%d \n",ppmgr_device.angle ,
        ppmgr_device.orientation, ppmgr_device.videoangle);
    size = endp - buf;
    return count;
}

static ssize_t bypass_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    //ppmgr_device_t* ppmgr_dev=(ppmgr_device_t*)cla;
    return snprintf(buf,80,"current bypass is %d\n",ppmgr_device.bypass);
}

static ssize_t bypass_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;

    ppmgr_device.bypass = simple_strtoul(buf, &endp, 0);
    size = endp - buf;
    return count;
}


static ssize_t rect_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"rotate rect:\nl:%d,t:%d,w:%d,h:%d\n",
			ppmgr_device.left,ppmgr_device.top,ppmgr_device.width,ppmgr_device.height);
}

static ssize_t rect_write(struct class *cla,struct class_attribute *attr,const char *buf, size_t count)
{
    char* errstr="data error,access string is \"left,top,width,height\"\n";
    char* strp=(char*)buf;
    char* endp;
    int value_array[4];
    static int buflen;
    static char* tokenlen;
    int i;
    buflen=strlen(buf);
    value_array[0]=value_array[1]=value_array[2]=value_array[3]= -1;

    for(i=0;i<4;i++) {
        if(buflen==0) {
            printk(errstr);
            return  -EINVAL;
        }
        tokenlen=strnchr(strp,buflen,',');
        if(tokenlen!=NULL) *tokenlen='\0';
        value_array[i]= simple_strtoul(strp,&endp,0);
        if((endp-strp)>(tokenlen-strp)) break;
        if(tokenlen!=NULL)  {
            *tokenlen=',';
            strp= tokenlen+1;
            buflen=strlen(strp);
        }  else
            break;
    }

    if(value_array[0]>=0) ppmgr_device.left= value_array[0];
    if(value_array[1]>=0) ppmgr_device.left= value_array[1];
    if(value_array[2]>0) ppmgr_device.left= value_array[2];
    if(value_array[3]>0) ppmgr_device.left= value_array[3];

    return count;
}

static ssize_t disp_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"disp width is %d ; disp height is %d \n",ppmgr_device.disp_width, ppmgr_device.disp_height);
}
static void set_disp_para(const char *para)
{
    int parsed[2];

    if (likely(parse_para(para, 2, parsed) == 2)) {
        int w, h;
        w = parsed[0] ;
        h = parsed[1];
        if((ppmgr_device.disp_width != w)||(ppmgr_device.disp_height != h))
            buff_change = 1;
        ppmgr_device.disp_width = w ;
        ppmgr_device.disp_height =  h ;
    }
}

static ssize_t disp_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    set_disp_para(buf);
    return count;
}

#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
extern int video_scaler_notify(int flag);
extern void amvideo_set_scaler_para(int x, int y, int w, int h,int flag);

static ssize_t ppscaler_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"current ppscaler mode is %s\n",(ppmgr_device.ppscaler_flag)?"enabled":"disabled");
}

static ssize_t ppscaler_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    int flag = simple_strtoul(buf, &endp, 0);
    if((flag<2)&&(flag != ppmgr_device.ppscaler_flag)){
        if(flag)
            video_scaler_notify(1);
        else
            video_scaler_notify(0);
        ppmgr_device.ppscaler_flag = flag;
        if(ppmgr_device.ppscaler_flag == 0)
            set_scaler_pos_reset(true);
    }
    size = endp - buf;
    return count;
}


static void set_ppscaler_para(const char *para)
{
    int parsed[5];

    if (likely(parse_para(para, 5, parsed) == 5)) {
        ppmgr_device.scale_h_start = parsed[0];
        ppmgr_device.scale_v_start = parsed[1];
        ppmgr_device.scale_h_end = parsed[2];
        ppmgr_device.scale_v_end = parsed[3];
        amvideo_set_scaler_para(ppmgr_device.scale_h_start,ppmgr_device.scale_v_start,
                                ppmgr_device.scale_h_end-ppmgr_device.scale_h_start+1,
                                ppmgr_device.scale_v_end-ppmgr_device.scale_v_start+1,parsed[4]);
    }
}

static ssize_t ppscaler_rect_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"ppscaler rect:\nx:%d,y:%d,w:%d,h:%d\n",
            ppmgr_device.scale_h_start,ppmgr_device.scale_v_start,
            ppmgr_device.scale_h_end-ppmgr_device.scale_h_start+1,
            ppmgr_device.scale_v_end-ppmgr_device.scale_v_start+1);
}

static ssize_t ppscaler_rect_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    set_ppscaler_para(buf);
    return count;
}
#endif

static ssize_t receiver_read(struct class *cla,struct class_attribute *attr,char *buf)
{
	if(ppmgr_device.receiver==1)
		return snprintf(buf,80,"video stream out to video4linux\n");
	else
		return snprintf(buf,80,"video stream out to vlayer\n");
}

static ssize_t receiver_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t size;
	char *endp;
    if(buf[0]!='0'&&buf[0]!='1') {
		printk("device to whitch the video stream decoded\n");
		printk("0: to video layer\n");
		printk("1: to amlogic video4linux /dev/video10\n");
		return 0;
	}
	ppmgr_device.receiver = simple_strtoul(buf, &endp, 0);
	vf_ppmgr_reset(0);
	size = endp - buf;
	return count;
}

static ssize_t platform_type_read(struct class *cla,struct class_attribute *attr,char *buf)
{
	if(platform_type ==PLATFORM_TV){
		return snprintf(buf,80,"current platform is TV\n");
	}else if(platform_type ==PLATFORM_MID){
		return snprintf(buf,80,"current platform is MID\n");
	}else if(platform_type ==PLATFORM_MID_VERTICAL){
        	return snprintf(buf,80,"current platform is vertical MID\n");
	}else{
		return snprintf(buf,80,"current platform is MBX\n");
	}
}

static ssize_t platform_type_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t size;
	char *endp;
	platform_type = simple_strtoul(buf, &endp, 0);
	size = endp - buf;
	return count;
}

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
static ssize_t _3dmode_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"current 3d mode is 0x%x\n",ppmgr_device.ppmgr_3d_mode);
}

static ssize_t _3dmode_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    unsigned mode = simple_strtoul(buf, &endp, 0);
    set_ppmgr_3dmode(mode);
    size = endp - buf;
    return count;
}

static ssize_t viewmode_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    const char *viewmode_str[] = {"normal", "full", "4:3","16:9","1:1"};
    return snprintf(buf,80,"current view mode is %d:%s\n",ppmgr_device.viewmode,viewmode_str[ppmgr_device.viewmode]);
}

static ssize_t viewmode_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    unsigned mode = simple_strtoul(buf, &endp, 0);
    set_ppmgr_viewmode(mode);
    size = endp - buf;
    return count;
}

static ssize_t doublemode_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    const char *doublemode_str[] = {"normal", "horizontal double", "vertical double"};
    unsigned mode = get_ppmgr_3dmode();
    mode = ((mode & PPMGR_3D_PROCESS_DOUBLE_TYPE)>>PPMGR_3D_PROCESS_DOUBLE_TYPE_SHIFT);
    return snprintf(buf,80,"current 3d double scale mode is %d:%s\n",mode,doublemode_str[mode]);
}

static ssize_t doublemode_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    unsigned flag = simple_strtoul(buf, &endp, 0);
    unsigned mode = get_ppmgr_3dmode();
    mode = (mode & (~PPMGR_3D_PROCESS_DOUBLE_TYPE))|((flag<<PPMGR_3D_PROCESS_DOUBLE_TYPE_SHIFT)&(PPMGR_3D_PROCESS_DOUBLE_TYPE));
    set_ppmgr_3dmode(mode);
    size = endp - buf;
    return count;
}

static ssize_t switchmode_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    const char *switchmode_str[] = {"disable", "enable"};
    unsigned mode = get_ppmgr_3dmode();
    unsigned flag = (mode & PPMGR_3D_PROCESS_SWITCH_FLAG)?1:0;
    return snprintf(buf,80,"current 3d switch mode is %d:%s\n",flag,switchmode_str[flag]);
}

static ssize_t switchmode_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    int flag = simple_strtoul(buf, &endp, 0);
    unsigned mode = get_ppmgr_3dmode();
    if(!flag)
        mode = mode & (~PPMGR_3D_PROCESS_SWITCH_FLAG);
    else
        mode = mode | PPMGR_3D_PROCESS_SWITCH_FLAG;
    set_ppmgr_3dmode(mode);
    size = endp - buf;
    return count;
}

static ssize_t direction_3d_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    const char *direction_str[] = {"0 degree", "90 degree", "180 degree","270 degree"};
    //unsigned mode = get_ppmgr_3dmode();
    //mode = ((mode & PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK)>>PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_VAULE_SHIFT);
    //return snprintf(buf,80,"current 3d direction is %d:%s\n",mode,direction_str[mode]);
    unsigned angle = get_ppmgr_direction3d();
    return snprintf(buf,80,"current 3d direction is %d:%s\n",angle,direction_str[angle]);
}

static ssize_t direction_3d_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    int flag = simple_strtoul(buf, &endp, 0);
    //unsigned mode = get_ppmgr_3dmode();
    //mode = (mode & (~PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK))|((flag<<PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_VAULE_SHIFT)&(PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK));
    //set_ppmgr_3dmode(mode);
    set_ppmgr_direction3d(flag);
    size = endp - buf;
    return count;
}

static ssize_t scale_down_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    const char *value_str[] = {"noraml", "div 2", "div 3","div 4"};
    unsigned mode = ppmgr_device.scale_down;
    return snprintf(buf,80,"current scale down value is %d:%s\n",mode+1,value_str[mode]);
}

static ssize_t scale_down_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;
    unsigned mode = simple_strtoul(buf, &endp, 0);
    set_ppmgr_scaledown(mode);
    size = endp - buf;
    return count;
}

/******************************************************************
					3D TV usage
*********************************************************************/

frame_info_t frame_info;

static int ppmgr_view_mode = 0 ;
static int ppmgr_vertical_sample =1 ;
static int ppmgr_scale_width = 800 ;
int ppmgr_cutwin_top = 0;
int ppmgr_cutwin_left = 0;
int get_ppmgr_change_notify(void)
{
	if(ppmgr_flag_change){
		ppmgr_flag_change = 0 ;
		return 1;
	}else{
		return 0;
	}
}

int get_ppmgr_view_mode(void)
{
	return ppmgr_view_mode;
}
int get_ppmgr_vertical_sample(void)
{
	return ppmgr_vertical_sample;
}
int get_ppmgr_scale_width(void)
{
	return ppmgr_scale_width;
}

static int depth = 3200;  /*12.5 pixels*/
void set_depth(int para)
{
	depth = para;
}
int get_depth(void)
{
	return depth;
}
static ssize_t read_depth(struct class *cla,struct class_attribute *attr,char *buf)
{
	 return snprintf(buf,80,"current depth is %d\n",depth);
}
static ssize_t write_depth(struct class *cla, struct class_attribute *attr, const char *buf,
                                size_t count)
{
    int r;
    char *endp;

    r = simple_strtoul(buf, &endp, 0);
	printk("r is %d\n" ,r);
	set_depth(r) ;
    return count;
}
static ssize_t read_view_mode(struct class *cla,struct class_attribute *attr,char *buf)
{
	 return snprintf(buf,80,"current view mode is %d\n",ppmgr_view_mode);
}
static ssize_t write_view_mode(struct class *cla, struct class_attribute *attr, const char *buf,
                                size_t count)
{
    int r;
    char *endp;

    r = simple_strtoul(buf, &endp, 0);
    ppmgr_view_mode = r ;
    return count;
}

static ssize_t read_vertical_sample(struct class *cla,struct class_attribute *attr,char *buf)
{
	 return snprintf(buf,80,"ppmgr_vertical_sample %d\n",ppmgr_vertical_sample);
}
static ssize_t write_vertical_sample(struct class *cla, struct class_attribute *attr, const char *buf,
                                size_t count)
{
    int r;
    char *endp;

    r = simple_strtoul(buf, &endp, 0);
    ppmgr_vertical_sample = r ;
    return count;
}
static ssize_t read_scale_width(struct class *cla,struct class_attribute *attr,char *buf)
{
	 return snprintf(buf,80,"ppmgr_scale_width is %d\n",ppmgr_scale_width);
}
static ssize_t write_scale_width(struct class *cla, struct class_attribute *attr, const char *buf,
                                size_t count)
{
    int r;
    char *endp;

    r = simple_strtoul(buf, &endp, 0);
    ppmgr_scale_width = r ;
    return count;
}
static void set_cut_window(const char *para)
{
  int parsed[2];

  if (likely(parse_para(para, 2, parsed) == 2)) {
	      int top ,left;
	      top = parsed[0] ;
	      left = parsed[1];
	      ppmgr_cutwin_top = top ;
	      ppmgr_cutwin_left = left ;
     }
 }

static ssize_t cut_win_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return snprintf(buf, 80, "cut win top is %d ; cut win left is %d \n", ppmgr_cutwin_top,ppmgr_cutwin_left);
}

static ssize_t cut_win_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                size_t count)
{
	set_cut_window(buf);
    return strnlen(buf, count);
}

#endif
static ssize_t mirror_read(struct class *cla,struct class_attribute *attr,char *buf)
{
	if(ppmgr_device.mirror_flag == 1)
		return snprintf(buf,80,"currnet mirror mode is l-r mirror mode. value is: %d.\n",ppmgr_device.mirror_flag);
	else if(ppmgr_device.mirror_flag == 2)
		return snprintf(buf,80,"currnet mirror mode is t-b mirror mode. value is: %d.\n",ppmgr_device.mirror_flag);
	else
		return snprintf(buf,80,"currnet mirror mode is normal mode. value is: %d.\n",ppmgr_device.mirror_flag);
}

static ssize_t mirror_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t size;
	char *endp;
	ppmgr_device.mirror_flag = simple_strtoul(buf, &endp, 0);
	if (ppmgr_device.mirror_flag > 2)
		ppmgr_device.mirror_flag = 0;
	size = endp - buf;
	return count;
}

/**************************************************************
 			3DTV usage
*******************************************************************/
extern int  vf_ppmgr_get_states(vframe_states_t *states);

static ssize_t ppmgr_vframe_states_show(struct class *cla, struct class_attribute* attr, char* buf)
{
    int ret = 0;
    vframe_states_t states;

    if (vf_ppmgr_get_states(&states) == 0) {
        ret += sprintf(buf + ret, "vframe_pool_size=%d\n", states.vf_pool_size);
        ret += sprintf(buf + ret, "vframe buf_free_num=%d\n", states.buf_free_num);
        ret += sprintf(buf + ret, "vframe buf_recycle_num=%d\n", states.buf_recycle_num);
        ret += sprintf(buf + ret, "vframe buf_avail_num=%d\n", states.buf_avail_num);

    } else {
        ret += sprintf(buf + ret, "vframe no states\n");
    }

    return ret;
}

static struct class_attribute ppmgr_class_attrs[] = {
    __ATTR(info,
           S_IRUGO | S_IWUSR,
           show_ppmgr_info,
           NULL),
    __ATTR(angle,
           S_IRUGO | S_IWUSR | S_IWGRP,
           angle_read,
           angle_write),
    __ATTR(rect,
           S_IRUGO | S_IWUSR,
           rect_read,
           rect_write),
    __ATTR(bypass,
           S_IRUGO | S_IWUSR,
           bypass_read,
           bypass_write),

    __ATTR(disp,
           S_IRUGO | S_IWUSR | S_IWGRP,
           disp_read,
           disp_write),

    __ATTR(orientation,
           S_IRUGO | S_IWUSR,
           orientation_read,
           orientation_write),
#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
    __ATTR(ppscaler,
           S_IRUGO | S_IWUSR | S_IWGRP,
           ppscaler_read,
           ppscaler_write),
    __ATTR(ppscaler_rect,
           S_IRUGO | S_IWUSR | S_IWGRP,
           ppscaler_rect_read,
           ppscaler_rect_write),
#endif
       __ATTR(vtarget,
           S_IRUGO | S_IWUSR | S_IWGRP,
           receiver_read,
           receiver_write),
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    __ATTR(ppmgr_3d_mode,
           S_IRUGO | S_IWUSR,
           _3dmode_read,
           _3dmode_write),
    __ATTR(viewmode,
           S_IRUGO | S_IWUSR,
           viewmode_read,
           viewmode_write),
    __ATTR(doublemode,
           S_IRUGO | S_IWUSR,
           doublemode_read,
           doublemode_write),
    __ATTR(switchmode,
           S_IRUGO | S_IWUSR,
           switchmode_read,
           switchmode_write),
    __ATTR(direction_3d,
           S_IRUGO | S_IWUSR,
           direction_3d_read,
           direction_3d_write),
    __ATTR(scale_down,
           S_IRUGO | S_IWUSR,
           scale_down_read,
           scale_down_write),
    __ATTR(depth,
			S_IRUGO | S_IWUSR,
			read_depth,
			write_depth),
    __ATTR(view_mode,
			S_IRUGO | S_IWUSR,
			read_view_mode,
			write_view_mode),
    __ATTR(vertical_sample,
			S_IRUGO | S_IWUSR,
			read_vertical_sample,
			write_vertical_sample),
    __ATTR(scale_width,
			S_IRUGO | S_IWUSR,
			read_scale_width,
			write_scale_width),
    __ATTR(axis,
    		S_IRUGO | S_IWUSR,
		    cut_win_show,
		    cut_win_store),
#endif

    __ATTR(platform_type,
           S_IRUGO | S_IWUSR,
           platform_type_read,
           platform_type_write),

    __ATTR(mirror,
           S_IRUGO | S_IWUSR,
           mirror_read,
           mirror_write),
    __ATTR_RO(ppmgr_vframe_states),
    __ATTR(disable_prot,
           S_IRUGO | S_IWUSR,
           disable_prot_show,
           disable_prot_store),
    __ATTR_NULL
};

static struct class ppmgr_class = {
    .name = PPMGR_CLASS_NAME,
    .class_attrs = ppmgr_class_attrs,
};

struct class* init_ppmgr_cls() {
    int  ret=0;
    ret = class_register(&ppmgr_class);
    if(ret<0 )
    {
        amlog_level(LOG_LEVEL_HIGH,"error create ppmgr class\r\n");
        return NULL;
    }
    return &ppmgr_class;
}

/***********************************************************************
*
* file op section.
*
************************************************************************/

void set_ppmgr_buf_info(char* start,unsigned int size) {
    ppmgr_device.buffer_start=(char*)start;
    ppmgr_device.buffer_size=size;
}

void get_ppmgr_buf_info(char** start,unsigned int* size) {
    *start=ppmgr_device.buffer_start;
    *size=ppmgr_device.buffer_size;
}

static int ppmgr_open(struct inode *inode, struct file *file)
{
    ppmgr_device.open_count++;
    return 0;
}

static long ppmgr_ioctl(struct file *file,
                        unsigned int cmd, ulong args)
{
    void  __user* argp =(void __user*)args;
    int ret = 0;
#if 0
    ge2d_context_t *context=(ge2d_context_t *)filp->private_data;
    config_para_t     ge2d_config;
    ge2d_para_t  para ;
    int flag;
    frame_info_t frame_info;
#endif
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    unsigned mode = 0;
    int flag = 0;
#endif
    switch (cmd)
    {
#if 0
        case PPMGR_IOC_CONFIG_FRAME:
            copy_from_user(&frame_info,argp,sizeof(frame_info_t));
            break;
#endif
        case PPMGR_IOC_GET_ANGLE:
            put_user(ppmgr_device.angle,(unsigned int *)argp);
            break;
        case PPMGR_IOC_SET_ANGLE:
            ret = _ppmgr_angle_write(args);
            break;
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
        case PPMGR_IOC_ENABLE_PP:
            mode=(int)argp;
            platform_type_t plarform_type;
            plarform_type = get_platform_type();
            if( plarform_type == PLATFORM_TV){
            	set_ppmgr_status(mode);
            }else{
          	  set_ppmgr_3dmode(mode);
         	}
            break;
        case PPMGR_IOC_VIEW_MODE:
            mode=(int)argp;
            set_ppmgr_viewmode(mode);
            break;
        case PPMGR_IOC_HOR_VER_DOUBLE:
            flag = (int)argp;
            mode = get_ppmgr_3dmode();
            mode = (mode & (~PPMGR_3D_PROCESS_DOUBLE_TYPE))|((flag<<PPMGR_3D_PROCESS_DOUBLE_TYPE_SHIFT)&(PPMGR_3D_PROCESS_DOUBLE_TYPE));
            set_ppmgr_3dmode(mode);
            break;
        case PPMGR_IOC_SWITCHMODE:
            flag = (int)argp;
            mode = get_ppmgr_3dmode();
            if(flag)
                mode = mode & PPMGR_3D_PROCESS_SWITCH_FLAG ;
            else
                mode = mode & (~PPMGR_3D_PROCESS_SWITCH_FLAG);
            set_ppmgr_3dmode(mode);
            break;
        case PPMGR_IOC_3D_DIRECTION:
            flag = (int)argp;
            //mode = get_ppmgr_3dmode();
            //mode = (mode & (~PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK))|((flag<<PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_VAULE_SHIFT)&(PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK));
            //set_ppmgr_3dmode(mode);
            set_ppmgr_direction3d(flag);
            break;
        case PPMGR_IOC_3D_SCALE_DOWN:
            mode=(int)argp;
            set_ppmgr_scaledown(mode);
            break;
#endif
        default :
            return -ENOIOCTLCMD;

    }
    return ret;
}

static int ppmgr_release(struct inode *inode, struct file *file)
{
#ifdef CONFIG_ARCH_MESON
    ge2d_context_t *context=(ge2d_context_t *)file->private_data;

    if(context && (0==destroy_ge2d_work_queue(context)))
    {
        ppmgr_device.open_count--;
        return 0;
    }
    amlog_level(LOG_LEVEL_LOW,"release one ppmgr device\n");
    return -1;
#else
    return 0;
#endif
}

/***********************************************************************
*
* file op initintg section.
*
************************************************************************/

static const struct file_operations ppmgr_fops = {
    .owner   = THIS_MODULE,
    .open    = ppmgr_open,
    .unlocked_ioctl  = ppmgr_ioctl,
    .release = ppmgr_release,
};

int  init_ppmgr_device(void)
{
    int  ret=0;

    strcpy(ppmgr_device.name,"ppmgr");
    ret=register_chrdev(0,ppmgr_device.name,&ppmgr_fops);
    if(ret <=0)
    {
        amlog_level(LOG_LEVEL_HIGH,"register ppmgr device error\r\n");
        return  ret ;
    }
    ppmgr_device.major=ret;
    ppmgr_device.dbg_enable=0;

    ppmgr_device.angle=0;
    ppmgr_device.bypass =0 ;
    ppmgr_device.videoangle=0;
    ppmgr_device.orientation=0;
#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
    ppmgr_device.ppscaler_flag = 0;
    ppmgr_device.scale_h_start = 0;
    ppmgr_device.scale_h_end = 0;
    ppmgr_device.scale_v_start = 0;
    ppmgr_device.scale_v_end = 0;
    scaler_pos_reset = false;
#endif
	ppmgr_device.receiver=0;
	ppmgr_device.receiver_format = (GE2D_FORMAT_M24_NV21|GE2D_LITTLE_ENDIAN);
    ppmgr_device.display_mode = 0;
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    ppmgr_device.ppmgr_3d_mode = EXTERNAL_MODE_3D_DISABLE ;
    ppmgr_device.direction_3d = 0;
    ppmgr_device.viewmode = VIEWMODE_NORMAL;
    ppmgr_device.scale_down = 0;
#endif
    ppmgr_device.mirror_flag  = 0;
    ppmgr_device.canvas_width = ppmgr_device.canvas_height = 0;
    amlog_level(LOG_LEVEL_LOW,"ppmgr_dev major:%d\r\n",ret);

    if((ppmgr_device.cla = init_ppmgr_cls())==NULL) return -1;
    ppmgr_device.dev=device_create(ppmgr_device.cla,NULL,MKDEV(ppmgr_device.major,0),NULL,ppmgr_device.name);
    if (IS_ERR(ppmgr_device.dev)) {
        amlog_level(LOG_LEVEL_HIGH,"create ppmgr device error\n");
        goto unregister_dev;
    }
    buff_change = 0;
    ppmgr_register();
    if(ppmgr_buffer_init(0) < 0) goto unregister_dev;
    //if(start_vpp_task()<0) return -1;
    ppmgr_device.use_prot = 1;
#if HAS_VPU_PROT
    ppmgr_device.disable_prot = 0;
#else
    ppmgr_device.disable_prot = 1;
#endif
    ppmgr_device.global_angle = 0;
    ppmgr_device.started = 0;
    return 0;

unregister_dev:
    class_unregister(ppmgr_device.cla);
    return -1;
}

int uninit_ppmgr_device(void)
{
    stop_ppmgr_task();

    if(ppmgr_device.cla)
    {
        if(ppmgr_device.dev)
            device_destroy(ppmgr_device.cla, MKDEV(ppmgr_device.major, 0));
        class_unregister(ppmgr_device.cla);
    }

    unregister_chrdev(ppmgr_device.major, ppmgr_device.name);
    return  0;
}

/*******************************************************************
 *
 * interface for Linux driver
 *
 * ******************************************************************/

MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0xff, LOG_LEVEL_DESC, LOG_MASK_DESC);

static struct platform_device *ppmgr_dev0 = NULL;
static struct resource memobj;
/* for driver. */
static int ppmgr_driver_probe(struct platform_device *pdev)
{
    char* buf_start;
    unsigned int buf_size;
    struct resource *mem;
    int idx;

#if 0
    if (!(mem = platform_get_resource(pdev, IORESOURCE_MEM, 0)))
    {
        amlog_level(LOG_LEVEL_HIGH, "ppmgr memory resource undefined.\n");
        return -EFAULT;
    }
#else
    mem = &memobj;
    idx = find_reserve_block(pdev->dev.of_node->name,0);
    if(idx < 0){
	 amlog_level(LOG_LEVEL_HIGH, "ppmgr memory resource undefined.\n");
        return -EFAULT;
    }
    mem->start = (phys_addr_t)get_reserve_block_addr(idx);
    mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(idx)-1;
#endif
    buf_start = (char *)mem->start;
    buf_size = mem->end - mem->start + 1;
    set_ppmgr_buf_info((char *)mem->start,buf_size);
    init_ppmgr_device();
    return 0;
}

static int ppmgr_drv_remove(struct platform_device *plat_dev)
{
    //struct rtc_device *rtc = platform_get_drvdata(plat_dev);
    //rtc_device_unregister(rtc);
    //device_remove_file(&plat_dev->dev, &dev_attr_irq);
    uninit_ppmgr_device();
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_ppmgr_dt_match[]={
	{	.compatible = "amlogic,ppmgr",
	},
	{},
};
#else
#define amlogic_ppmgr_dt_match NULL
#endif

/* general interface for a linux driver .*/
struct platform_driver ppmgr_drv = {
    .probe  = ppmgr_driver_probe,
    .remove = ppmgr_drv_remove,
    .driver = {
        .name = "ppmgr",
        .owner = THIS_MODULE,
        .of_match_table = amlogic_ppmgr_dt_match,
    }
};

static int __init
ppmgr_init_module(void)
{
    int err;

    amlog_level(LOG_LEVEL_HIGH,"ppmgr_init\n");
    if ((err = platform_driver_register(&ppmgr_drv))) {
        return err;
    }

    return err;

}

static void __exit
ppmgr_remove_module(void)
{
    platform_device_put(ppmgr_dev0);
    platform_driver_unregister(&ppmgr_drv);
    amlog_level(LOG_LEVEL_HIGH,"ppmgr module removed.\n");
}

module_init(ppmgr_init_module);
module_exit(ppmgr_remove_module);

MODULE_DESCRIPTION("AMLOGIC  ppmgr driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("aml-sh <kasin.li@amlogic.com>");


