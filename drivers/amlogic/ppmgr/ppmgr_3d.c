#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vfp.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/amlogic/ge2d/ge2d_wq.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include "ppmgr_log.h"
#include "ppmgr_pri.h"
#include "ppmgr_dev.h"
#include <linux/amlogic/ppmgr/ppmgr.h>
#include <linux/amlogic/ppmgr/ppmgr_status.h>
#include <linux/io.h>

#define VF_POOL_SIZE 4

static int cur_process_type =0;
static int mask_canvas_index = -1;
static int ppmgr_3d_clear_count = 0;

typedef struct{
    unsigned all_mode;
    unsigned char mode;
    unsigned char src_format;
    unsigned char switch_flag;
    unsigned char _3d_to_2d_use_frame;
    unsigned char _2d_to_3d_type;
    unsigned char double_type;
    unsigned _2d_3d_control;
    unsigned _2d_3d_control_value;
    //unsigned direction; //0: 0 degree, 1: 90, 2: 180, 3:270
}Process3d_t;

typedef struct display_frame_s{
    int frame_top;
    int frame_left;
    int frame_width;
    int frame_height;
    int content_top;
    int content_left;
    int content_width;
    int content_height;
}display_frame_t;

extern void ppmgr_vf_put_dec(vframe_t *vf);
extern u32 index2canvas(u32 index);
extern vfq_t q_ready;
extern vfq_t q_free;

extern int get_bypass_mode(void);

static Process3d_t _3d_process = {0};

void Reset3Dclear(void)
{
    ppmgr_3d_clear_count = VF_POOL_SIZE;
}

void Set3DProcessPara(unsigned mode)
{
    if(_3d_process.all_mode != mode){
        memset(&_3d_process,0,sizeof(Process3d_t));
        _3d_process.all_mode = mode;
        _3d_process.mode = mode & PPMGR_3D_PROCESS_MODE_MASK;
        _3d_process.src_format = (mode & PPMGR_3D_PROCESS_SRC_FOMRAT_MASK)>>PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT;
        _3d_process.switch_flag = (mode & PPMGR_3D_PROCESS_SWITCH_FLAG)?1:0;
        _3d_process._3d_to_2d_use_frame = (mode & PPMGR_3D_PROCESS_3D_TO_2D_SRC_FRAME)?1:0;
        _3d_process._2d_to_3d_type = (mode & PPMGR_3D_PROCESS_2D_TO_3D_MASK)>>PPMGR_3D_PROCESS_2D_TO_3D_SHIFT;
        _3d_process.double_type = (mode & PPMGR_3D_PROCESS_DOUBLE_TYPE)>>PPMGR_3D_PROCESS_DOUBLE_TYPE_SHIFT;
        _3d_process._2d_3d_control = (mode & PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_MASK)>>PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_SHIFT;
        if(_3d_process._2d_3d_control>0){
            _3d_process._2d_3d_control_value = (mode & PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_VALUE_MASK)>>PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_VAULE_SHIFT;
            if((_3d_process.mode == PPMGR_3D_PROCESS_MODE_2D_TO_3D)&&(!_3d_process._2d_3d_control_value))
                _3d_process._2d_3d_control_value = 0x10;
        }else{
            _3d_process._2d_3d_control_value = 0;
            
        }
        //_3d_process.direction = (mode & PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK)>>PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_VAULE_SHIFT;
        //printk("-- ppmgr 3d process 0x%x: mode:%d, src format:%d, switch flag:%d, 3d to 2d frame: %d,2d to 3d:%d, double type:%d, control mode: %d, control value: %d, rotate direction: %d,
        //       _3d_process.all_mode,_3d_process.mode,_3d_process.src_format,_3d_process.switch_flag,
        //       _3d_process._3d_to_2d_use_frame,_3d_process._2d_to_3d_type,_3d_process.double_type,
        //       _3d_process._2d_3d_control,_3d_process._2d_3d_control_value,_3d_process.direction);
    }
}

int get_mid_process_type(vframe_t* vf)
{
    int process_type = 0 ;
    if(!vf){
        process_type = TYPE_NONE ;
        return process_type;
    }

    if(_3d_process.mode == PPMGR_3D_PROCESS_MODE_3D_ENABLE){
        if(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_LR){
            process_type = TYPE_3D_LR;
        }else if(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB){
            process_type = TYPE_3D_TB;
        }else{  // auto mode or others
            switch(vf->trans_fmt){
                case TVIN_TFMT_3D_TB:
                    process_type = TYPE_3D_TB;
                    break;
                case TVIN_TFMT_3D_FP:
                    process_type = TYPE_3D_LR;
                    break;
                case TVIN_TFMT_3D_LRH_OLOR:
                case TVIN_TFMT_3D_LRH_OLER:
                case TVIN_TFMT_3D_LRH_ELOR:
                case TVIN_TFMT_3D_LRH_ELER:
                    process_type = TYPE_3D_LR;
                    break;
                default:
                    process_type = TYPE_NONE;
                    break;
            }
        }            
    }else if(_3d_process.mode == PPMGR_3D_PROCESS_MODE_3D_TO_2D){
        if(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_LR){
            process_type = TYPE_3D_TO_2D_LR;
        }else if(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB){
            process_type = TYPE_3D_TO_2D_TB;
        }else{  // auto mode or others
            switch(vf->trans_fmt){
                case TVIN_TFMT_3D_TB:
                    process_type = TYPE_3D_TO_2D_TB;
                    break;
                case TVIN_TFMT_3D_FP:
                    process_type = TYPE_3D_TO_2D_LR;
                    break;
                case TVIN_TFMT_3D_LRH_OLOR:
                case TVIN_TFMT_3D_LRH_OLER:
                case TVIN_TFMT_3D_LRH_ELOR:
                case TVIN_TFMT_3D_LRH_ELER:
                    process_type = TYPE_3D_TO_2D_LR;
                    break;
                default:
                    process_type = TYPE_NONE;
                    break;
            }
        }   
    }else if(_3d_process.mode == PPMGR_3D_PROCESS_MODE_2D_TO_3D){
        process_type = TYPE_2D_TO_3D;
    }else{
        process_type = TYPE_NONE;
    }
    return process_type;
}

int is_mid_local_source(vframe_t* vf)
{
    int ret = 0 ;
    if(vf->type&VIDTYPE_VIU_422){
        ret = 0;
    }else{
    	ret = 1;
    }
    return ret;
}

int is_mid_mvc_need_process(vframe_t* vf)
{
    int ret = 0 ;
    int process_type = get_mid_process_type(vf);
    switch(process_type){
        //case TYPE_3D_LR:
        case TYPE_3D_TO_2D_LR:
            ret = 1;
            break;
        default:
            break;
    }
    return ret;
}

static int is_vertical_sample_enable(vframe_t* vf)
{
    int ret = 0 ;
    if((vf->type &VIDTYPE_INTERLACE_BOTTOM)||(vf->type &VIDTYPE_INTERLACE_TOP)){
        ret = 1 ;
    }
    return ret;
}


static int get_input_format(vframe_t* vf)
{
    int format= GE2D_FORMAT_M24_YUV420;
    if(vf->type&VIDTYPE_VIU_NV21)
        format =  GE2D_FORMAT_M24_NV21;
    else
        format =  GE2D_FORMAT_M24_YUV420;
    if(vf->type&VIDTYPE_VIU_422){
        format =  GE2D_FORMAT_S16_YUV422;
    }else{
        if(is_vertical_sample_enable(vf)){
            if(vf->type &VIDTYPE_INTERLACE_BOTTOM){
                format =  format|(GE2D_FMT_M24_YUV420B & (3<<3));
            }else if(vf->type &VIDTYPE_INTERLACE_TOP){
                format =  format|(GE2D_FORMAT_M24_YUV420T & (3<<3));
            }
        }else{
            if(vf->type&VIDTYPE_VIU_NV21)
                format =  GE2D_FORMAT_M24_NV21;
            else
                format =  GE2D_FORMAT_M24_YUV420;
        }
    }
    return format;
}


static int get_input_frame(vframe_t* vf , display_frame_t* frame)
{
    if(frame == NULL){
        return -1;
    }
    if(vf->type & VIDTYPE_MVC){
	frame->content_top = vf->left_eye.start_y   ;
	frame->content_left = vf->left_eye.start_x ;
	frame->content_width = vf->left_eye.width    ;
	frame->content_height = vf->left_eye.height  ;
	frame->frame_top =    vf->left_eye.start_y ;
	frame->frame_left =   vf->left_eye.start_x;
	frame->frame_width=   vf->left_eye.width ;
	frame->frame_height = vf->left_eye.height;
	return 0;
    }
/*tv in case , need detect the black bar*/
    if((vf->prop.bbar.bottom)&&(vf->prop.bbar.right)
      &&(vf->prop.bbar.right > vf->prop.bbar.left)
      &&(vf->prop.bbar.bottom > vf->prop.bbar.top )){
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                frame->content_top = vf->prop.bbar.top   ;
                frame->content_left = vf->prop.bbar.left  ;
                frame->content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                frame->content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->left_eye.start_y   ;
                frame->content_left = vf->left_eye.start_x ;
                frame->content_width = vf->left_eye.width    ;
                frame->content_height = vf->left_eye.height  ;
                frame->frame_top =    vf->left_eye.start_y ;
                frame->frame_left =   vf->left_eye.start_x;
                frame->frame_width=   vf->left_eye.width ;
                frame->frame_height = vf->left_eye.height;
                break;
            default:
                frame->content_top = vf->prop.bbar.top   ;
                frame->content_left = vf->prop.bbar.left  ;
                frame->content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                frame->content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
        }
//	    printk("full_frame: format  is %d , top is %d , left is %d , width is %d , height is %d\n",vf->trans_fmt ,frame->content_top ,frame->content_left,frame->content_width,frame->content_height);
    }else{
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                frame->content_top = 0  ;
                frame->content_left = 0  ;
                frame->content_width = vf->width;    ;
                frame->content_height = vf->height;
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->left_eye.start_y   ;
                frame->content_left = vf->left_eye.start_x ;
                frame->content_width = vf->left_eye.width    ;
                frame->content_height = vf->left_eye.height  ;
                frame->frame_top =    vf->left_eye.start_y ;
                frame->frame_left =   vf->left_eye.start_x;
                frame->frame_width=   vf->left_eye.width ;
                frame->frame_height = vf->left_eye.height;
                break;
            default:
                frame->content_top = 0  ;
                frame->content_left = 0  ;
                frame->content_width = vf->width;
                frame->content_height = vf->height;
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
        }
    }
    return 0;
}

static int get_input_l_frame(vframe_t* vf , display_frame_t* frame)
{
    int content_top ,content_left ,content_width ,content_height;
    if(frame == NULL){
        return -1;
    }
    if(vf->type & VIDTYPE_MVC){
	frame->content_top = vf->left_eye.start_y   ;
	frame->content_left = vf->left_eye.start_x ;
	frame->content_width = vf->left_eye.width    ;
	frame->content_height = vf->left_eye.height  ;
	frame->frame_top =    vf->left_eye.start_y;
	frame->frame_left =   vf->left_eye.start_x;
	frame->frame_width=   vf->left_eye.width;
	frame->frame_height = vf->left_eye.height;
	return 0;
    }
/*tv in case , need detect the black bar*/
    if((vf->prop.bbar.bottom)&&(vf->prop.bbar.right)
      &&(vf->prop.bbar.right > vf->prop.bbar.left)
      &&(vf->prop.bbar.bottom > vf->prop.bbar.top )){
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                content_top = vf->prop.bbar.top   ;
                content_left = vf->prop.bbar.left  ;
                content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->left_eye.start_y   ;
                frame->content_left = vf->left_eye.start_x ;
                frame->content_width = vf->left_eye.width    ;
                frame->content_height = vf->left_eye.height  ;
                frame->frame_top =    vf->left_eye.start_y;
                frame->frame_left =   vf->left_eye.start_x;
                frame->frame_width=   vf->left_eye.width;
                frame->frame_height = vf->left_eye.height;
                break;
            default:
                content_top = vf->prop.bbar.top   ;
                content_left = vf->prop.bbar.left  ;
                content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
        }
//	    printk("lframe: format  is %d , top is %d , left is %d , width is %d , height is %d\n",vf->trans_fmt ,frame->content_top ,frame->content_left,frame->content_width,frame->content_height);
    }else{
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                content_top = 0  ;
                content_left = 0  ;
                content_width = vf->width;    ;
                content_height = vf->height;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top =    0;
                frame->frame_left =   0;

                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->left_eye.start_y   ;
                frame->content_left = vf->left_eye.start_x ;
                frame->content_width = vf->left_eye.width    ;
                frame->content_height = vf->left_eye.height  ;
                frame->frame_top =    vf->left_eye.start_y;
                frame->frame_left =   vf->left_eye.start_x;
                frame->frame_width=   vf->left_eye.width;
                frame->frame_height = vf->left_eye.height;
                break;
            default:
                content_top = 0  ;
                content_left = 0  ;
                content_width = vf->width;    ;
                content_height = vf->height;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top =    0;
                frame->frame_left =   0;
                frame->frame_width=   vf->width;
                frame->frame_height = vf->height;
                break;
        } 
    }
    return 0;
}

static int get_input_r_frame(vframe_t* vf , display_frame_t* frame)
{
    int content_top ,content_left ,content_width ,content_height;
    if(frame == NULL){
        return -1;
    }
    if(vf->type & VIDTYPE_MVC){
        frame->content_top = vf->right_eye.start_y   ;
        frame->content_left = vf->right_eye.start_x ;
        frame->content_width = vf->right_eye.width    ;
        frame->content_height = vf->right_eye.height  ;
        frame->frame_top =    vf->right_eye.start_y;
        frame->frame_left =   vf->right_eye.start_x;
        frame->frame_width=   vf->right_eye.width;
        frame->frame_height = vf->right_eye.height;
        return 0;
    }
/*tv in case , need detect the black bar*/
    if((vf->prop.bbar.bottom)&&(vf->prop.bbar.right)
      &&(vf->prop.bbar.right > vf->prop.bbar.left)
      &&(vf->prop.bbar.bottom > vf->prop.bbar.top )){
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                content_top = vf->prop.bbar.top   ;
                content_left = vf->prop.bbar.left  ;
                content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top + content_height/2 ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_width/2 + content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top = 0;
                frame->frame_left = 0;
                frame->frame_width= vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->right_eye.start_y   ;
                frame->content_left = vf->right_eye.start_x ;
                frame->content_width = vf->right_eye.width    ;
                frame->content_height = vf->right_eye.height  ;
                frame->frame_top =    vf->right_eye.start_y;
                frame->frame_left =   vf->right_eye.start_x;
                frame->frame_width=   vf->right_eye.width;
                frame->frame_height = vf->right_eye.height;
                break;
            default:
                content_top = vf->prop.bbar.top   ;
                content_left = vf->prop.bbar.left  ;
                content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
                content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top + content_height/2 ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_width/2 + content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top = 0;
                frame->frame_left = 0;
                frame->frame_width= vf->width;
                frame->frame_height = vf->height;
                break;
        }
//		printk("rframe : format  is %d , top is %d , left is %d , width is %d , height is %d\n",vf->trans_fmt ,frame->content_top ,frame->content_left,frame->content_width,frame->content_height);
    }else{
        switch(vf->trans_fmt){
            case TVIN_TFMT_2D:
                content_top = 0  ;
                content_left = 0  ;
                content_width = vf->width;    ;
                content_height = vf->height;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top + content_height/2 ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_width/2 + content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top = 0;
                frame->frame_left = 0;
                frame->frame_width= vf->width;
                frame->frame_height = vf->height;
                break;
            case TVIN_TFMT_3D_FP:
                frame->content_top = vf->right_eye.start_y   ;
                frame->content_left = vf->right_eye.start_x ;
                frame->content_width = vf->right_eye.width    ;
                frame->content_height = vf->right_eye.height  ;
                frame->frame_top =    vf->right_eye.start_y;
                frame->frame_left =   vf->right_eye.start_x;
                frame->frame_width=   vf->right_eye.width;
                frame->frame_height = vf->right_eye.height;
                break;
            default:
                content_top = 0  ;
                content_left = 0  ;
                content_width = vf->width;    ;
                content_height = vf->height;
                if((cur_process_type == TYPE_3D_TB)||(cur_process_type == TYPE_3D_TO_2D_TB)
                    ||(_3d_process.src_format == PPMGR_3D_PROCESS_SRC_FOMRAT_TB)){
                    frame->content_top = content_top + content_height/2 ;
                    frame->content_left = content_left ;
                    frame->content_width = content_width    ;
                    frame->content_height = content_height/2  ;
                }else{
                    frame->content_top = content_top  ;
                    frame->content_left = content_width/2 + content_left ;
                    frame->content_width = content_width/2    ;
                    frame->content_height = content_height  ;
                }
                frame->frame_top = 0;
                frame->frame_left = 0;
                frame->frame_width= vf->width;
                frame->frame_height = vf->height;
                break;
        }
    }
    return 0;
}

static int get_output_rect_after_ratio(vframe_t* vf ,int* top , int*left , int* width , int* height ,int in_width, int in_height, int output_width,int output_height,unsigned angle)
{
    int t = 0,l = 0,w = 0,h = 0;
    int current_view_mode = 0 ;
    int ar = 0x100;
    unsigned char  doublemode = _3d_process.double_type;

    w = output_width;
    h = output_height;
    if(doublemode == PPMGR_3D_PROCESS_DOUBLE_TYPE_HOR){
        in_width = in_width<<1;
    }else if(doublemode == PPMGR_3D_PROCESS_DOUBLE_TYPE_VER){
        in_height = in_height<<1;
    }
    if (vf->ratio_control) {
        ar = (vf->ratio_control >> DISP_RATIO_ASPECT_RATIO_BIT) & 0x3ff;
        if(doublemode == PPMGR_3D_PROCESS_DOUBLE_TYPE_HOR){
            ar = ar>>1;
        }else if(doublemode == PPMGR_3D_PROCESS_DOUBLE_TYPE_VER){
            ar = ar<<1;
        }
    }else{
        ar = (in_height<<8)/in_width;
    }
    current_view_mode  = get_ppmgr_viewmode();
    if(current_view_mode == VIEWMODE_NORMAL){
        if(output_width>output_height){  //panel 1280*768 case
            if(angle&1){
                if((in_width*16) == (in_height*9)){
                    current_view_mode = VIEWMODE_FULL;
                }
            }else{
                if((in_width*9) == (in_height*16)){
                    current_view_mode = VIEWMODE_FULL;
                }
            }
        }else{                          //panel 800*1280 case
            if(angle&1){
                if((in_width*9) == (in_height*16)){
                    current_view_mode = VIEWMODE_FULL;
                }
            }else{
                if((in_width*16) == (in_height*9)){
                    current_view_mode = VIEWMODE_FULL;
                }
            }
        }   
    }

    switch(current_view_mode){
        case VIEWMODE_4_3:
            ar = 0xc0;
            break;
        case VIEWMODE_16_9:
            ar = 0x90;
            break;
        case VIEWMODE_FULL:
            *top = 0;
            *left = 0;
            *width = output_width;
            *height = output_height;
            return 0;
        case VIEWMODE_1_1:
            if(angle&1){
                int swap = in_width;
                in_width = in_height;
                in_height = swap;
            }
            break;
        case VIEWMODE_NORMAL:
        default:
            break;
    }

    if(angle&1)
        ar = 0x10000/ar;
    if ((ar * output_width) > (output_height << 8)) {
        if((current_view_mode == VIEWMODE_1_1)&&(in_height<=output_height)){
            l = (output_width - in_width)>>1;
            t = (output_height - in_height)>>1;
        }else{
            w = (output_height << 8) / ar;
            l = (output_width - w) / 2;
        }
    } else {
        if((current_view_mode == VIEWMODE_1_1)&&(in_width<=output_width)){
            l = (output_width - in_width)>>1;
            t = (output_height - in_height)>>1;
        }else{
            h = (output_width * ar) >> 8;
            t = (output_height - h) / 2;
        }
    }
    l &= ~1;
    t &= ~1;
    w =  output_width - 2*l;
    h  =  output_height - 2*t;
    *top = t;
    *left = l;
    *width = w;
    *height = h;
    return 0;
}

/*******************************************************************/

/*for decoder input processing
    1. output window should 1:1 as source frame size
    2. keep the frame ratio
    3. input format should be YUV420 , output format should be YUV444
*/
static void process_none(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
	
}

//static int ratio_value = 10; // 0~255
// for 90 degree and 270 degree, use interlace mode to output mix data.
void process_2d_to_3d_ex(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config,unsigned angle)
{
    vframe_t *new_vf;
    ppframe_t *pp_vf;
    display_frame_t input_frame;
    canvas_t cs0,cs1,cs2,cd;
    int t,l,w,h;
    int canvas_width = ppmgr_device.canvas_width;
    int canvas_height = ppmgr_device.canvas_height;
    unsigned char switch_flag = _3d_process.switch_flag;
    int x_offset = 0, dir = 0;
    unsigned scale_down = get_ppmgr_scaledown()+1;
    int pic_struct = 0;

    new_vf = vfq_pop(&q_free);
    if(_3d_process._2d_3d_control == PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_LEFT_MOVE){
        x_offset = _3d_process._2d_3d_control_value;
    }else if(_3d_process._2d_3d_control == PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_RIGHT_MOVE){
        x_offset = _3d_process._2d_3d_control_value;
        dir = 1;
    }else{
        x_offset = 0;
    }

    if (unlikely((!new_vf) || (!vf)))
        return;

    //int interlace_mode = vf->type & VIDTYPE_TYPEMASK;

    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    new_vf->ratio_control = ((scale_down>1)?DISP_RATIO_FORCE_FULL_STRETCH:DISP_RATIO_FORCE_NORMALWIDE)|DISP_RATIO_FORCECONFIG;
    new_vf->duration = vf->duration;
    new_vf->duration_pulldown = vf->duration_pulldown;
    new_vf->pts = vf->pts;
    if(ppmgr_device.disp_width>ppmgr_device.disp_height)
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;        
    else
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE;
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(pp_vf->index);

    get_input_frame(vf,&input_frame);

    new_vf->width= ppmgr_device.disp_width;
    new_vf->height= ppmgr_device.disp_height;
    
    if(ppmgr_3d_clear_count>0){
        //clear rect
        memset(ge2d_config,0,sizeof(config_para_ex_t));
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;
    
        canvas_read(new_vf->canvas0Addr&0xff,&cd);
        ge2d_config->src_planes[0].addr = cd.addr;
        ge2d_config->src_planes[0].w = cd.width;
        ge2d_config->src_planes[0].h = cd.height;
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = canvas_width;
        ge2d_config->src_para.height = canvas_height;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;
        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = canvas_width;
        ge2d_config->dst_para.height = canvas_height;
        
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            ppmgr_vf_put_dec(vf);
            vfq_push(&q_free, new_vf);
            return;
        }
        fillrect(context, 0, 0, canvas_width, canvas_height, 0x008080ff);
        ppmgr_3d_clear_count--;        
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420T & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444|(switch_flag?(GE2D_FORMAT_M24_YUV420B & (3<<3)):(GE2D_FORMAT_M24_YUV420T & (3<<3)));
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height>>1;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    //printk("--first frame ex: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);
   
    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
        }
    }

    if(is_vertical_sample_enable(vf)){
       stretchblt_noalpha(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height/2,
           l,t/2,w,h/2);
    }else{
       stretchblt_noalpha(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,(pic_struct)?(input_frame.content_height/2):input_frame.content_height,
           l,t/2,w,h/2);
    }

    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444|(switch_flag?(GE2D_FORMAT_M24_YUV420T & (3<<3)):(GE2D_FORMAT_M24_YUV420B & (3<<3)));
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height>>1;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {

        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(x_offset){     
        if(angle&1){
            int dst_h = 0;
            if(dir){  //move down
                if((t+x_offset+h)<=new_vf->height){ //only pan
                    if(angle == 3){
                        t = t - x_offset;
                        t &= ~1;    
                    }else{
                        t = t + x_offset;
                        t &= ~1;  
                    }       
                }else if((t+x_offset)<new_vf->height){  //need cut 
                    t = t + x_offset;
                    t &= ~1;
                    dst_h = new_vf->height - t;
                    input_frame.content_width = ((((input_frame.content_width*dst_h) <<8)+0x80)/h)>>8;
                    h = dst_h;
                    if(angle == 3){
                        t = 0;
                    }
                }else{
                    printk("++2d->3d error, out of range1.\n");
                }
            }else{//move up
                if(t>=x_offset){ //only pan
                    if(angle == 3){
                        t = t + x_offset;
                        t &= ~1;    
                    }else{
                        t = t - x_offset;
                        t &= ~1;  
                    }        
                }else if((t+h)>x_offset){  //need cut 
                    int src_w = 0, src_l = 0;
                    t = 0;
                    dst_h = h + t - x_offset;
                    src_w = ((((input_frame.content_width*dst_h) <<8)+0x80)/h)>>8;
                    h = dst_h;
                    src_l = input_frame.content_left + input_frame.content_width - src_w;
                    src_l &= ~1;
                    input_frame.content_width = input_frame.content_width + input_frame.content_left - src_l;
                    input_frame.content_left = src_l;
                    if(angle == 3){
                        t = t + x_offset;
                    }
                }else{
                    printk("++2d->3d error, out of range2.\n");
                }
            }
        }else{
            int dst_w = 0;
            if(dir){  //move right
                if((l+x_offset+w)<=new_vf->width){ //only pan
                    if((angle == 1)||(angle == 2)){
                        l = l - x_offset;
                        l &= ~1;    
                    }else{
                        l = l + x_offset;
                        l &= ~1;  
                    }       
                }else if((l+x_offset)<new_vf->width){  //need cut 
                    l = l + x_offset;
                    l &= ~1;
                    dst_w = new_vf->width - l;
                    input_frame.content_width = ((((input_frame.content_width*dst_w) <<8)+0x80)/w)>>8;
                    w = dst_w;
                    if((angle == 1)||(angle == 2)){
                        l = 0;
                    }
                }else{
                    printk("++2d->3d error, out of range1.\n");
                }
            }else{//move left
                if(l>=x_offset){ //only pan
                    if((angle == 1)||(angle == 2)){
                        l = l + x_offset;
                        l &= ~1;    
                    }else{
                        l = l - x_offset;
                        l &= ~1;  
                    }        
                }else if((l+w)>x_offset){  //need cut 
                    int src_w = 0, src_l = 0;
                    l = 0;
                    dst_w = w + l - x_offset;
                    src_w = ((((input_frame.content_width*dst_w) <<8)+0x80)/w)>>8;
                    w = dst_w;
                    src_l = input_frame.content_left + input_frame.content_width - src_w;
                    src_l &= ~1;
                    input_frame.content_width = input_frame.content_width + input_frame.content_left - src_l;
                    input_frame.content_left = src_l;
                    if((angle == 1)||(angle == 2)){
                        l = l + x_offset;
                    }
                }else{
                    printk("++2d->3d error, out of range2.\n");
                }
            }
        }
    }

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
            new_vf->width = new_vf->width/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
            new_vf->height = new_vf->height/scale_down;
        }
    }

    if(is_vertical_sample_enable(vf)){
       stretchblt_noalpha(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height/2,
           l,t/2,w,h/2);
    }else{
       stretchblt_noalpha(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,(pic_struct)?(input_frame.content_height/2):input_frame.content_height,
           l,t/2,w,h/2);
    }

    //printk("--second frame ex: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

static void process_2d_to_3d(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config,unsigned angle)
{
    vframe_t *new_vf;
    ppframe_t *pp_vf;
    display_frame_t input_frame;
    canvas_t cs0,cs1,cs2,cd,cm;
    int t,l,w,h;
    unsigned char switch_flag = _3d_process.switch_flag;
    int x_offset = 0, dir = 0;
    int canvas_width = ppmgr_device.canvas_width;
    int canvas_height = ppmgr_device.canvas_height;
    int scale_down = get_ppmgr_scaledown()+1;
    int pic_struct = 0;

    new_vf = vfq_pop(&q_free);
    if(_3d_process._2d_3d_control == PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_LEFT_MOVE){
        x_offset = _3d_process._2d_3d_control_value;
    }else if(_3d_process._2d_3d_control == PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_RIGHT_MOVE){
        x_offset = _3d_process._2d_3d_control_value;
        dir = 1;
    }else{
        x_offset = 0;
    }

    if (unlikely((!new_vf) || (!vf)))
        return;

    //int interlace_mode = vf->type & VIDTYPE_TYPEMASK;

    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    new_vf->ratio_control = ((scale_down>1)?DISP_RATIO_FORCE_FULL_STRETCH:DISP_RATIO_FORCE_NORMALWIDE)|DISP_RATIO_FORCECONFIG;
    new_vf->duration = vf->duration;
    new_vf->duration_pulldown = vf->duration_pulldown;
    new_vf->pts = vf->pts;
    if(ppmgr_device.disp_width>ppmgr_device.disp_height)
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;        
    else
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE;       
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(pp_vf->index);

    get_input_frame(vf,&input_frame);

    new_vf->width= ppmgr_device.disp_width;
    new_vf->height= ppmgr_device.disp_height;

    if(ppmgr_3d_clear_count>0){
        //clear rect
        memset(ge2d_config,0,sizeof(config_para_ex_t));
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;
    
        canvas_read(new_vf->canvas0Addr&0xff,&cd);
        ge2d_config->src_planes[0].addr = cd.addr;
        ge2d_config->src_planes[0].w = cd.width;
        ge2d_config->src_planes[0].h = cd.height;
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = canvas_width;
        ge2d_config->src_para.height = canvas_height;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;
        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = canvas_width;
        ge2d_config->dst_para.height = canvas_height;
        
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            ppmgr_vf_put_dec(vf);
            vfq_push(&q_free, new_vf);
            return;
        }
        fillrect(context, 0, 0, canvas_width, canvas_height, 0x008080ff);
        ppmgr_3d_clear_count--;        
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420T & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(mask_canvas_index,&cm);
    ge2d_config->src2_planes[0].addr = cm.addr;
    ge2d_config->src2_planes[0].w = cm.width;
    ge2d_config->src2_planes[0].h = cm.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_key.key_enable = 1;
    ge2d_config->src2_key.key_mask = 0x00ffffff;
    ge2d_config->src2_key.key_mode = (switch_flag)?1:0;
    ge2d_config->src2_key.key_color = 0xff000000;
    ge2d_config->src2_para.canvas_index=mask_canvas_index;
    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src2_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->src2_para.fill_color_en = 0;
    ge2d_config->src2_para.fill_mode = 0;
    ge2d_config->src2_para.x_rev = 0;
    ge2d_config->src2_para.y_rev = 0;
    ge2d_config->src2_para.color = 0x00808000;
    ge2d_config->src2_para.top = 0;
    ge2d_config->src2_para.left = 0;
    ge2d_config->src2_para.width = canvas_width;
    ge2d_config->src2_para.height = canvas_height;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    //printk("--first frame: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);
    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
        }
    }

    if(is_vertical_sample_enable(vf)){
       blend(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height/2,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }else{
       blend(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,(pic_struct)?(input_frame.content_height/2):input_frame.content_height,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }

    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(mask_canvas_index,&cm);
    ge2d_config->src2_planes[0].addr = cm.addr;
    ge2d_config->src2_planes[0].w = cm.width;
    ge2d_config->src2_planes[0].h = cm.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_key.key_enable = 1;
    ge2d_config->src2_key.key_mask = 0x00ffffff;
    ge2d_config->src2_key.key_mode = (switch_flag)?0:1;
    ge2d_config->src2_key.key_color = 0xff000000;
    ge2d_config->src2_para.canvas_index=mask_canvas_index;
    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src2_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->src2_para.fill_color_en = 0;
    ge2d_config->src2_para.fill_mode = 0;
    ge2d_config->src2_para.x_rev = 0;
    ge2d_config->src2_para.y_rev = 0;
    ge2d_config->src2_para.color = 0x00808000;
    ge2d_config->src2_para.top = 0;
    ge2d_config->src2_para.left = 0;
    ge2d_config->src2_para.width = canvas_width;
    ge2d_config->src2_para.height = canvas_height;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(x_offset){     
        if(angle&1){
            int dst_h = 0;
            if(dir){  //move down
                if((t+x_offset+h)<=new_vf->height){ //only pan
                    if(angle == 3){
                        t = t - x_offset;
                        t &= ~1;    
                    }else{
                        t = t + x_offset;
                        t &= ~1;  
                    }       
                }else if((t+x_offset)<new_vf->height){  //need cut 
                    t = t + x_offset;
                    t &= ~1;
                    dst_h = new_vf->height - t;
                    input_frame.content_width = ((((input_frame.content_width*dst_h) <<8)+0x80)/h)>>8;
                    h = dst_h;
                    if(angle == 3){
                        t = 0;
                    }
                }else{
                    printk("++2d->3d error, out of range1.\n");
                }
            }else{//move up
                if(t>=x_offset){ //only pan
                    if(angle == 3){
                        t = t + x_offset;
                        t &= ~1;    
                    }else{
                        t = t - x_offset;
                        t &= ~1;  
                    }        
                }else if((t+h)>x_offset){  //need cut 
                    int src_w = 0, src_l = 0;
                    t = 0;
                    dst_h = h + t - x_offset;
                    src_w = ((((input_frame.content_width*dst_h) <<8)+0x80)/h)>>8;
                    h = dst_h;
                    src_l = input_frame.content_left + input_frame.content_width - src_w;
                    src_l &= ~1;
                    input_frame.content_width = input_frame.content_width + input_frame.content_left - src_l;
                    input_frame.content_left = src_l;
                    if(angle == 3){
                        t = t + x_offset;
                    }
                }else{
                    printk("++2d->3d error, out of range2.\n");
                }
            }
        }else{
            int dst_w = 0;
            if(dir){  //move right
                if((l+x_offset+w)<=new_vf->width){ //only pan
                    if(angle == 2){
                        l = l - x_offset;
                        l &= ~1;    
                    }else{
                        l = l + x_offset;
                        l &= ~1;  
                    }       
                }else if((l+x_offset)<new_vf->width){  //need cut 
                    l = l + x_offset;
                    l &= ~1;
                    dst_w = new_vf->width - l;
                    input_frame.content_width = ((((input_frame.content_width*dst_w) <<8)+0x80)/w)>>8;
                    w = dst_w;
                    if(angle == 2){
                        l = 0;
                    }
                }else{
                    printk("++2d->3d error, out of range1.\n");
                }
            }else{//move left
                if(l>=x_offset){ //only pan
                    if(angle == 2){
                        l = l + x_offset;
                        l &= ~1;    
                    }else{
                        l = l - x_offset;
                        l &= ~1;  
                    }        
                }else if((l+w)>x_offset){  //need cut 
                    int src_w = 0, src_l = 0;
                    l = 0;
                    dst_w = w + l - x_offset;
                    src_w = ((((input_frame.content_width*dst_w) <<8)+0x80)/w)>>8;
                    w = dst_w;
                    src_l = input_frame.content_left + input_frame.content_width - src_w;
                    src_l &= ~1;
                    input_frame.content_width = input_frame.content_width + input_frame.content_left - src_l;
                    input_frame.content_left = src_l;
                    if(angle == 2){
                        l = l + x_offset;
                    }
                }else{
                    printk("++2d->3d error, out of range2.\n");
                }
            }
        }
    }

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
            new_vf->width = new_vf->width/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
            new_vf->height = new_vf->height/scale_down;
        }
    }

    if(is_vertical_sample_enable(vf)){
       blend(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height/2,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }else{
       blend(context,input_frame.content_left,input_frame.content_top,input_frame.content_width,(pic_struct)?(input_frame.content_height/2):input_frame.content_height,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }

    //printk("--second frame: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      input_frame.content_left,input_frame.content_top,input_frame.content_width,input_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

/*for 3D video input processing
    1. output window should 1:1 as video layer size
    2. must adjust GE2D operation according with the frame ratio ,then clear ratio control flag
    3. need generate two buffer from source frame
    4. input format should be YUV422 , output format should be YUV444
*/

// for 90 degree and 270 degree, use interlace mode to output mix data.
void process_3d_ex(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config,unsigned angle)
{
    vframe_t *new_vf;
    ppframe_t *pp_vf;
    display_frame_t l_frame,r_frame;
    canvas_t cs0,cs1,cs2,cd;
    int t,l,w,h;
    unsigned char switch_flag = _3d_process.switch_flag;
    int canvas_width = ppmgr_device.canvas_width;
    int canvas_height = ppmgr_device.canvas_height;
    int scale_down = get_ppmgr_scaledown()+1;    
    int pic_struct = 0;
	
    new_vf = vfq_pop(&q_free);
    if (unlikely((!new_vf) || (!vf)))
        return;

    //int interlace_mode = vf->type & VIDTYPE_TYPEMASK;

    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    new_vf->ratio_control = ((scale_down>1)?DISP_RATIO_FORCE_FULL_STRETCH:DISP_RATIO_FORCE_NORMALWIDE)|DISP_RATIO_FORCECONFIG;
    new_vf->duration = vf->duration;
    new_vf->duration_pulldown = vf->duration_pulldown;
    new_vf->pts = vf->pts;
    if(ppmgr_device.disp_width>ppmgr_device.disp_height)
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;        
    else
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE;
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(pp_vf->index);

    //get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);

    new_vf->width= ppmgr_device.disp_width;
    new_vf->height= ppmgr_device.disp_height;
    
    if(ppmgr_3d_clear_count>0){
        //clear rect
        memset(ge2d_config,0,sizeof(config_para_ex_t));
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;
    
        canvas_read(new_vf->canvas0Addr&0xff,&cd);
        ge2d_config->src_planes[0].addr = cd.addr;
        ge2d_config->src_planes[0].w = cd.width;
        ge2d_config->src_planes[0].h = cd.height;
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = canvas_width;
        ge2d_config->src_para.height = canvas_height;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;
        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = canvas_width;
        ge2d_config->dst_para.height = canvas_height;
        
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            ppmgr_vf_put_dec(vf);
            vfq_push(&q_free, new_vf);
            return;
        }
        fillrect(context, 0, 0, canvas_width, canvas_height, 0x008080ff);
        ppmgr_3d_clear_count--;
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420T & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444|(switch_flag?(GE2D_FORMAT_M24_YUV420B & (3<<3)):(GE2D_FORMAT_M24_YUV420T & (3<<3)));
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height>>1;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){

        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
        }
    }

    //printk("--l frame ex: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    if(is_vertical_sample_enable(vf)){
       stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,
           l,t/2,w,h/2);
    }else{
       stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,(pic_struct)?(l_frame.content_height/2):l_frame.content_height,
           l,t/2,w,h/2);
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420B & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas1Addr&0xff,&cs0);
    canvas_read((vf->canvas1Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas1Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas1Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444|(switch_flag?(GE2D_FORMAT_M24_YUV420T & (3<<3)):(GE2D_FORMAT_M24_YUV420B & (3<<3)));
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height>>1;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
            new_vf->width = new_vf->width/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
            new_vf->height = new_vf->height/scale_down;
        }
    }

    //printk("--r frame ex: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    if(is_vertical_sample_enable(vf)){
       stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,
           l,t/2,w,h/2);
    }else{
       stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,(pic_struct)?(r_frame.content_height/2):r_frame.content_height,
           l,t/2,w,h/2);
    }

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

void process_3d(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config,unsigned angle)
{
    vframe_t *new_vf;
    ppframe_t *pp_vf;
    display_frame_t l_frame,r_frame;
    canvas_t cs0,cs1,cs2,cd,cm;
    int t,l,w,h;
    unsigned char  switch_flag = _3d_process.switch_flag;
    int canvas_width = ppmgr_device.canvas_width;
    int canvas_height = ppmgr_device.canvas_height;
    int scale_down = get_ppmgr_scaledown()+1;    
    int pic_struct = 0;

    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;

    //int interlace_mode = vf->type & VIDTYPE_TYPEMASK;

    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    new_vf->ratio_control = ((scale_down>1)?DISP_RATIO_FORCE_FULL_STRETCH:DISP_RATIO_FORCE_NORMALWIDE)|DISP_RATIO_FORCECONFIG;
    new_vf->duration = vf->duration;
    new_vf->duration_pulldown = vf->duration_pulldown;
    new_vf->pts = vf->pts;
    if(ppmgr_device.disp_width>ppmgr_device.disp_height)
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;        
    else
        new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE;
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(pp_vf->index);

    //get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);

    new_vf->width= ppmgr_device.disp_width;
    new_vf->height= ppmgr_device.disp_height;

    if(ppmgr_3d_clear_count>0){
        //clear rect
        memset(ge2d_config,0,sizeof(config_para_ex_t));
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;
    
        canvas_read(new_vf->canvas0Addr&0xff,&cd);
        ge2d_config->src_planes[0].addr = cd.addr;
        ge2d_config->src_planes[0].w = cd.width;
        ge2d_config->src_planes[0].h = cd.height;
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = canvas_width;
        ge2d_config->src_para.height = canvas_height;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;
        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = canvas_width;
        ge2d_config->dst_para.height = canvas_height;
        
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            ppmgr_vf_put_dec(vf);
            vfq_push(&q_free, new_vf);
            return;
        }
        fillrect(context, 0, 0, canvas_width, canvas_height, 0x008080ff);
        ppmgr_3d_clear_count--;
    }

    /* data operating. */
    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420T & (3<<3));
    }else{
        pic_struct = 0;
    }

    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cs0);
    canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(mask_canvas_index,&cm);
    ge2d_config->src2_planes[0].addr = cm.addr;
    ge2d_config->src2_planes[0].w = cm.width;
    ge2d_config->src2_planes[0].h = cm.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_key.key_enable = 1;
    ge2d_config->src2_key.key_mask = 0x00ffffff;
    ge2d_config->src2_key.key_mode = (switch_flag)?1:0;
    ge2d_config->src2_key.key_color = 0xff000000;
    ge2d_config->src2_para.canvas_index=mask_canvas_index;
    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src2_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->src2_para.fill_color_en = 0;
    ge2d_config->src2_para.fill_mode = 0;
    ge2d_config->src2_para.x_rev = 0;
    ge2d_config->src2_para.y_rev = 0;
    ge2d_config->src2_para.color = 0x00808000;
    ge2d_config->src2_para.top = 0;
    ge2d_config->src2_para.left = 0;
    ge2d_config->src2_para.width = canvas_width;
    ge2d_config->src2_para.height = canvas_height;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
        }
    }

    //printk("--l frame: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    if(is_vertical_sample_enable(vf)){
       blend(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }else{
       blend(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,(pic_struct)?(l_frame.content_height/2):l_frame.content_height,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (GE2D_FORMAT_M24_YUV420B & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas1Addr&0xff,&cs0);
    canvas_read((vf->canvas1Addr>>8)&0xff,&cs1);
    canvas_read((vf->canvas1Addr>>16)&0xff,&cs2);
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(mask_canvas_index,&cm);
    ge2d_config->src2_planes[0].addr = cm.addr;
    ge2d_config->src2_planes[0].w = cm.width;
    ge2d_config->src2_planes[0].h = cm.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_key.key_color = 0;
    ge2d_config->src_para.canvas_index=vf->canvas1Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_key.key_enable = 1;
    ge2d_config->src2_key.key_mask = 0x00ffffff;
    ge2d_config->src2_key.key_mode = (switch_flag)?0:1;
    ge2d_config->src2_key.key_color = 0xff000000;
    ge2d_config->src2_para.canvas_index=mask_canvas_index;
    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src2_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->src2_para.fill_color_en = 0;
    ge2d_config->src2_para.fill_mode = 0;
    ge2d_config->src2_para.x_rev = 0;
    ge2d_config->src2_para.y_rev = 0;
    ge2d_config->src2_para.color = 0x00808000;
    ge2d_config->src2_para.top = 0;
    ge2d_config->src2_para.left = 0;
    ge2d_config->src2_para.width = canvas_width;
    ge2d_config->src2_para.height = canvas_height;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(scale_down>1){
        if(angle&1){
            l = ((new_vf->width/scale_down) - (w/scale_down))/2;
            w = w/scale_down;
            new_vf->width = new_vf->width/scale_down;
        }else{
            t = ((new_vf->height/scale_down) - (h/scale_down))/2;
            h = h/scale_down;
            new_vf->height = new_vf->height/scale_down;
        }
    }

    //printk("--r frame: %d,%d,%d,%d.out put:%d,%d,%d,%d. frame size: %d,%d.\n",
    //      r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height,
    //      l,t,w,h,new_vf->width,new_vf->height);

    if(is_vertical_sample_enable(vf)){
       blend(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }else{
       blend(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,(pic_struct)?(r_frame.content_height/2):r_frame.content_height,
           l,t,w,h,l,t,w,h,
           blendop(OPERATION_ADD,COLOR_FACTOR_ONE,COLOR_FACTOR_ZERO,OPERATION_ADD,ALPHA_FACTOR_ZERO,ALPHA_FACTOR_ZERO));
    }

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

//void process_bt(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config,int swith_flag)
//{
//	
//}

static void process_field_depth(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{

}

void process_3d_to_2d(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t *new_vf;
    ppframe_t *pp_vf;
    //int index;
    //display_frame_t input_frame ;
    display_frame_t src_frame;
    canvas_t cs0,cs1,cs2,cd;
    int t,l,w,h;
    unsigned char l_r = _3d_process._3d_to_2d_use_frame;
    unsigned angle = get_ppmgr_direction3d();//_3d_process.direction;
    int canvas_width = ppmgr_device.canvas_width;
    int canvas_height = ppmgr_device.canvas_height;
    int scale_down = get_ppmgr_scaledown()+1;   
    int pic_struct = 0;

    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;

    //int interlace_mode = vf->type & VIDTYPE_TYPEMASK;

    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    new_vf->ratio_control = ((scale_down>1)?DISP_RATIO_FORCE_FULL_STRETCH:DISP_RATIO_FORCE_NORMALWIDE)|DISP_RATIO_FORCECONFIG;
    new_vf->duration = vf->duration;
    new_vf->duration_pulldown = vf->duration_pulldown;
    new_vf->pts = vf->pts;
    //new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE;
    new_vf->type = VIDTYPE_VIU_444 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(pp_vf->index);

    //get_input_frame(vf,&input_frame);
    if(!l_r)
        get_input_l_frame(vf,&src_frame);
    else
        get_input_r_frame(vf,&src_frame);

    new_vf->width= ppmgr_device.disp_width;
    new_vf->height= ppmgr_device.disp_height;

    if(ppmgr_3d_clear_count>0){
        //clear rect        
        memset(ge2d_config,0,sizeof(config_para_ex_t));
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;
    
        canvas_read(new_vf->canvas0Addr&0xff,&cd);
        ge2d_config->src_planes[0].addr = cd.addr;
        ge2d_config->src_planes[0].w = cd.width;
        ge2d_config->src_planes[0].h = cd.height;
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = canvas_width;
        ge2d_config->src_para.height = canvas_height;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;
        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = canvas_width;
        ge2d_config->dst_para.height = canvas_height;
        
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            ppmgr_vf_put_dec(vf);
            vfq_push(&q_free, new_vf);
            return;
        }
        fillrect(context, 0, 0, canvas_width, canvas_height, 0x008080ff);
        ppmgr_3d_clear_count--;
    }

    if(vf->type & VIDTYPE_MVC){
        pic_struct = (l_r)?(GE2D_FORMAT_M24_YUV420B & (3<<3)):(GE2D_FORMAT_M24_YUV420T & (3<<3));
    }else{
        pic_struct = 0;
    }
    /* data operating. */
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    if(l_r){
        canvas_read(vf->canvas1Addr&0xff,&cs0);
        canvas_read((vf->canvas1Addr>>8)&0xff,&cs1);
        canvas_read((vf->canvas1Addr>>16)&0xff,&cs2);
    }else{
        canvas_read(vf->canvas0Addr&0xff,&cs0);
        canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
        canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);
    }
    ge2d_config->src_planes[0].addr = cs0.addr;
    ge2d_config->src_planes[0].w = cs0.width;
    ge2d_config->src_planes[0].h = cs0.height;
    ge2d_config->src_planes[1].addr = cs1.addr;
    ge2d_config->src_planes[1].w = cs1.width;
    ge2d_config->src_planes[1].h = cs1.height;
    ge2d_config->src_planes[2].addr = cs2.addr;
    ge2d_config->src_planes[2].w = cs2.width;
    ge2d_config->src_planes[2].h = cs2.height;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_para.canvas_index=(l_r)?vf->canvas1Addr:vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf)|pic_struct;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    if(is_vertical_sample_enable(vf)){
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = vf->height/2;
    }else{
        ge2d_config->src_para.width = vf->width;
        ge2d_config->src_para.height = (pic_struct)?(vf->height/2):vf->height;
    }

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = canvas_width;
    ge2d_config->dst_para.height = canvas_height;

    if(angle==1){
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.x_rev = 1;
    }else if(angle==2){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev=1;        
    }else if(angle==3)  {
        ge2d_config->dst_xy_swap=1;
        ge2d_config->dst_para.y_rev=1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    get_output_rect_after_ratio(vf,&t,&l,&w,&h,vf->width,vf->height,new_vf->width,new_vf->height,angle);

    if(scale_down>1){
        t = ((new_vf->height/scale_down) - (h/scale_down))/2;
        h = h/scale_down;
        new_vf->height = new_vf->height/scale_down;
        l = ((new_vf->width/scale_down) - (w/scale_down))/2;
        w = w/scale_down;
        new_vf->width = new_vf->width/scale_down;
    }

    if(is_vertical_sample_enable(vf)){
        stretchblt_noalpha(context, src_frame.content_left,src_frame.content_top,src_frame.content_width,src_frame.content_height/2,l,t,w,h);
    }else{
        stretchblt_noalpha(context, src_frame.content_left,src_frame.content_top,src_frame.content_width,(pic_struct)?(src_frame.content_height/2):src_frame.content_height,l,t,w,h);
    }

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

/********************************************************************/

void ppmgr_vf_3d(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{  
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cd;
    int cur_angle = 0;
    int process_type = get_mid_process_type(vf);
    cur_process_type = process_type;
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);


    canvas_read(vf->canvas0Addr&0xff,&cd);
    if(vf->type&VIDTYPE_VIU_422){
         cd.width >>=1;
    }
    if(((input_frame.content_left +input_frame.content_width )> cd.width )
      ||((input_frame.content_top +input_frame.content_height ) > cd.height )
      ||((input_frame.frame_left +input_frame.frame_width )> cd.width )
      ||((input_frame.frame_top +input_frame.frame_height ) > cd.height )){
        ppmgr_vf_put_dec(vf);
        printk("case 1: vdin canvas setting is not compatible with vframe!!!\n");
        return ;

    }

    if(((l_frame.content_left +l_frame.content_width )> cd.width )
      ||((l_frame.content_top +l_frame.content_height ) > cd.height )
      ||((l_frame.frame_left +l_frame.frame_width )> cd.width )
      ||((l_frame.frame_top +l_frame.frame_height ) > cd.height )){
        ppmgr_vf_put_dec(vf);
        printk("case 2: vdin canvas setting is not compatible with vframe!!!\n");
        return ;
    }

    if(((r_frame.content_left +r_frame.content_width )> cd.width )
      ||((r_frame.content_top +r_frame.content_height ) > cd.height )
      ||((r_frame.frame_left +r_frame.frame_width )> cd.width )
      ||((r_frame.frame_top +r_frame.frame_height ) > cd.height )){
        ppmgr_vf_put_dec(vf);
        printk("case 3:vdin canvas setting is not compatible with vframe!!!\n");
        return ;
    }
    cur_angle = get_ppmgr_direction3d();//_3d_process.direction;
    switch(process_type){
        case TYPE_NONE:
 //           printk("process  none type\n");
            process_none(vf,context,ge2d_config);
            break;
        case TYPE_2D_TO_3D:
 //           printk("process 2d to 3d type\n");
            //if(_3d_process._2d_to_3d_type == PPMGR_3D_PROCESS_2D_TO_3D_NORMAL)
            if(cur_angle&1)
                process_2d_to_3d_ex(vf,context,ge2d_config,cur_angle);
            else
                process_2d_to_3d(vf,context,ge2d_config,cur_angle);
            //else
            //    process_field_depth(vf,context,ge2d_config);
            break;
        case TYPE_3D_LR:
        case TYPE_3D_TB:
            //printk("process  3d type\n");
            if(cur_angle&1)
                process_3d_ex(vf,context,ge2d_config,cur_angle);
            else
                process_3d(vf,context,ge2d_config,cur_angle);
            break;
        case TYPE_3D_TO_2D_LR:
        case TYPE_3D_TO_2D_TB:
//            printk("process  3d to 2d type\n");
            process_3d_to_2d(vf,context,ge2d_config);
            break;
        default:
            break;
    }
}

int Init3DBuff(int canvas_id)
{
    void __iomem * mask_start = NULL;
    unsigned char mask = 0xff;
    canvas_t canvas_mask;	
    int k = 0;
    unsigned char *buff = NULL;

    mask_canvas_index = canvas_id;
    canvas_read(mask_canvas_index,&canvas_mask);
    mask_start = ioremap_wc(canvas_mask.addr,canvas_mask.width*canvas_mask.height);
    buff = (unsigned char*)mask_start;

    while(k<canvas_mask.width*canvas_mask.height){
        buff[k] = mask;
        mask = ~mask;
        k++;
    }
    iounmap(mask_start);
    return 0;
}
