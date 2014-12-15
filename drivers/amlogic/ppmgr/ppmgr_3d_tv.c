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
#define RECEIVER_NAME "ppmgr"
#define PROVIDER_NAME   "ppmgr"
#define VF_POOL_SIZE 4
extern vfq_t q_ready;
extern vfq_t q_free;
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

extern int get_ppmgr_vertical_sample(void);
extern int get_ppmgr_scale_width(void);
extern int get_ppmgr_view_mode(void);
extern u32 index2canvas(u32 index);
extern void ppmgr_vf_put_dec(vframe_t *vf);
static inline u32 index2canvas_0(u32 index)
{
    const u32 canvas_tab[4] = {
        PPMGR_DOUBLE_CANVAS_INDEX+0, PPMGR_DOUBLE_CANVAS_INDEX+1, PPMGR_DOUBLE_CANVAS_INDEX+2, PPMGR_DOUBLE_CANVAS_INDEX+3
    };
    return canvas_tab[index];
}
static inline u32 index2canvas_1(u32 index)
{
    const u32 canvas_tab[4] = {
        PPMGR_DOUBLE_CANVAS_INDEX+4, PPMGR_DOUBLE_CANVAS_INDEX+5, PPMGR_DOUBLE_CANVAS_INDEX+6, PPMGR_DOUBLE_CANVAS_INDEX+7
    };
    return canvas_tab[index];
}

static void window_clear_3D(ge2d_context_t *context, config_para_ex_t* ge2d_config,int index ,int l ,int t ,int w ,int h);
static int cur_process_type =0;
extern int ppmgr_cutwin_top ;
extern int ppmgr_cutwin_left ;
extern frame_info_t frame_info;
int get_tv_process_type(vframe_t* vf)
{
    int process_type = 0 ;
    int status = get_ppmgr_status();
    if(!vf){
        process_type = TYPE_NONE ;
        return process_type;
    }
    if(status & MODE_3D_ENABLE){
        if(status & MODE_LR_SWITCH){
            process_type = TYPE_LR_SWITCH ;
        }else if(status & MODE_FIELD_DEPTH){
            process_type = TYPE_FILED_DEPTH ;
        }else if(status & MODE_3D_TO_2D_L){
            process_type = TYPE_3D_TO_2D_L ;
        }else if(status & MODE_3D_TO_2D_R){
            process_type = TYPE_3D_TO_2D_R ;
        }else{
            if(status & MODE_LR){
                process_type = TYPE_LR;
            }
            if(status & MODE_BT){
                process_type = TYPE_BT;
            }
            if(status &MODE_2D_TO_3D){
                process_type  =TYPE_2D_TO_3D;
            }
            /*3D auto mode*/
            if(status &MODE_AUTO){
				switch(vf->trans_fmt){
					case TVIN_TFMT_3D_TB:
					process_type = TYPE_BT;
					break;
					case TVIN_TFMT_3D_FP:
					process_type = TYPE_LR;
					break;
					case TVIN_TFMT_3D_LRH_OLOR :
					case TVIN_TFMT_3D_LRH_OLER :
					case TVIN_TFMT_3D_LRH_ELOR :
					case TVIN_TFMT_3D_LRH_ELER :
					process_type = TYPE_LR;
					break;
					default:
					process_type = TYPE_NONE ;
					break;
				}
			}
        }
    }else{
        process_type = TYPE_NONE;
    }
    return process_type;
}

int is_need_cut_window_support(vframe_t* vf)
{
	int ret = 0;
#if 1
	if(vf->type&VIDTYPE_VIU_422){
		ret  = 1;
	}
#else
	ret = 1;
#endif
	return ret ;
}

int is_mvc_need_process(vframe_t* vf)
{
	int ret = 0 ;
	int process_type = cur_process_type;
	switch(process_type){
        //case TYPE_LR_SWITCH:
        case TYPE_3D_TO_2D_L:
        case TYPE_3D_TO_2D_R:
            ret = 1;
            break;
        default:
            break;
	}
	return ret;
}
/*1 local player ; 2 1080P frame ; 3 user enablesetting */
static int is_vertical_sample_enable(vframe_t* vf)
{
	int ret = 0 ;
	int process_type = cur_process_type;
	int status = get_ppmgr_status();
	if(get_ppmgr_vertical_sample()){
		if((process_type == TYPE_LR )
		||((process_type == TYPE_LR_SWITCH )&&(!(status &BT_FORMAT_INDICATOR)))
		||(process_type == TYPE_2D_TO_3D )){
		 	if(!(vf->type&VIDTYPE_VIU_422)){
		 		if((vf->width > 1280)&&(vf->height > 720)){
		 			ret = 1 ;
		 		}
		 	}
		}
	}
	if((!(status &BT_FORMAT_INDICATOR))&&(process_type !=TYPE_BT)){
	if((vf->type &VIDTYPE_INTERLACE_BOTTOM)||(vf->type &VIDTYPE_INTERLACE_TOP)){
		ret = 1 ;
	}
	}
	return ret;
}


int is_local_source(vframe_t* vf)
{
	int ret = 0 ;
    if(vf->type&VIDTYPE_VIU_422){
        ret = 0;
    }else{
    	ret = 1;
    }
    return ret;
}
static int get_input_format(vframe_t* vf)
{
    int format= GE2D_FORMAT_M24_YUV420;
    if(vf->type&VIDTYPE_VIU_422){
        format =  GE2D_FORMAT_S16_YUV422;
    }else if(vf->type&VIDTYPE_VIU_NV21){
    	if(is_vertical_sample_enable(vf)){
	    	 if(vf->type &VIDTYPE_INTERLACE_BOTTOM){
	    	 	format =  GE2D_FORMAT_M24_NV21|(GE2D_FORMAT_M24_NV21B & (3<<3));
	    	 	}else if(vf->type &VIDTYPE_INTERLACE_TOP){
	    	 	format =  GE2D_FORMAT_M24_NV21|(GE2D_FORMAT_M24_NV21T & (3<<3));
	    	 }else{
	        	format =  GE2D_FORMAT_M24_NV21;
	    	 }     	
    	}else{
    		 format =  GE2D_FORMAT_M24_NV21;
    	}    	
    }else{
    	 if(is_vertical_sample_enable(vf)){
    	 	if(vf->type &VIDTYPE_INTERLACE_BOTTOM){
    	 		format =  GE2D_FORMAT_M24_YUV420|(GE2D_FMT_M24_YUV420B & (3<<3));
    	 	}else if(vf->type &VIDTYPE_INTERLACE_TOP){
    	 	format =  GE2D_FORMAT_M24_YUV420|(GE2D_FORMAT_M24_YUV420T & (3<<3));
    	 }else{
    	 		format =  GE2D_FORMAT_M24_YUV420|(GE2D_FORMAT_M24_YUV420T & (3<<3));
    	 	}
    	 }else{
        	format =  GE2D_FORMAT_M24_YUV420;
    	 }
    }
    return format;
}

int get_input_frame(vframe_t* vf , display_frame_t* frame)
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
			case 	TVIN_TFMT_2D:
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
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
			case 	TVIN_TFMT_2D:
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
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
    int status = get_ppmgr_status();
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
			case 	TVIN_TFMT_2D:
			content_top = vf->prop.bbar.top   ;
			content_left = vf->prop.bbar.left  ;
			content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
			content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;

			if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
				if(is_need_cut_window_support(vf)){
					if(content_top >= ppmgr_cutwin_top ){
						frame->content_top = content_top  ;
						frame->content_height = content_height/2  ;
					}else{
						frame->content_top = ppmgr_cutwin_top ;
						frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
					}
					if(content_left >= ppmgr_cutwin_left ){
						frame->content_left = content_left  ;
						frame->content_width = content_width ;
					}else{
						frame->content_left = ppmgr_cutwin_left ;
						frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
					}

				}else{
				frame->content_top = content_top  ;
				frame->content_left = content_left ;
				frame->content_width = content_width    ;
				frame->content_height = content_height/2  ;
				}
			}else{
				if(is_need_cut_window_support(vf)){
					if(content_top >= ppmgr_cutwin_top ){
						frame->content_top = content_top  ;
						frame->content_height = content_height  ;
					}else{
						frame->content_top = ppmgr_cutwin_top ;
						frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
					}
					if(content_left >= ppmgr_cutwin_left ){
						frame->content_left = content_left  ;
						frame->content_width = content_width/2 ;
					}else{
						frame->content_left = ppmgr_cutwin_left ;
						frame->content_width = content_width/2 - 2* (ppmgr_cutwin_left - content_left);
					}
			}else{
				frame->content_top = content_top  ;
				frame->content_left = content_left ;
				frame->content_width = content_width/2    ;
				frame->content_height = content_height  ;
			}
			}
			frame->frame_top =    0;
			frame->frame_left =   0;
			frame->frame_width=   vf->width;
			frame->frame_height = vf->height;
			break;
			case TVIN_TFMT_3D_FP:
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
				if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
					frame->content_top = content_top  ;
					frame->content_left = content_left ;
					frame->content_width = content_width    ;
					frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = content_width/2 - 2* (ppmgr_cutwin_left - content_left);
						}
				}else{
					frame->content_top = content_top  ;
					frame->content_left = content_left ;
					frame->content_width = content_width/2    ;
					frame->content_height = content_height  ;
				}
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
			case 	TVIN_TFMT_2D:
			content_top = 0  ;
			content_left = 0  ;
			content_width = vf->width;    ;
			content_height = vf->height;
			if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
				if(is_need_cut_window_support(vf)){
					if(content_top >= ppmgr_cutwin_top ){
						frame->content_top = content_top  ;
						frame->content_height = content_height/2  ;
					}else{
						frame->content_top = ppmgr_cutwin_top ;
						frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
					}
					if(content_left >= ppmgr_cutwin_left ){
						frame->content_left = content_left  ;
						frame->content_width = content_width ;
					}else{
						frame->content_left = ppmgr_cutwin_left ;
						frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
					}

				}else{
				frame->content_top = content_top  ;
				frame->content_left = content_left ;
				frame->content_width = content_width    ;
				frame->content_height = content_height/2  ;
				}
			}else{
				if(is_need_cut_window_support(vf)){
					if(content_top >= ppmgr_cutwin_top ){
						frame->content_top = content_top  ;
						frame->content_height = content_height  ;
					}else{
						frame->content_top = ppmgr_cutwin_top ;
						frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
					}
					if(content_left >= ppmgr_cutwin_left ){
						frame->content_left = content_left  ;
						frame->content_width = content_width/2 ;
					}else{
						frame->content_left = ppmgr_cutwin_left ;
						frame->content_width = content_width/2 - 2* (ppmgr_cutwin_left - content_left);
					}
			}else{
				frame->content_top = content_top  ;
				frame->content_left = content_left ;
				frame->content_width = content_width/2    ;
				frame->content_height = content_height  ;
			}
			}
			frame->frame_top =    0;
			frame->frame_left =   0;
			frame->frame_width=   vf->width;
			frame->frame_height = vf->height;
			break;
			case TVIN_TFMT_3D_FP:
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
				if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
					frame->content_top = content_top  ;
					frame->content_left = content_left ;
					frame->content_width = content_width    ;
					frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = content_width/2 - 2* (ppmgr_cutwin_left - content_left);
						}
				}else{
					frame->content_top = content_top  ;
					frame->content_left = content_left ;
					frame->content_width = content_width/2    ;
					frame->content_height = content_height  ;
				}
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
    int status = get_ppmgr_status();
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
			case 	TVIN_TFMT_2D:
				content_top = vf->prop.bbar.top   ;
				content_left = vf->prop.bbar.left  ;
				content_width = vf->prop.bbar.right - vf->prop.bbar.left    ;
				content_height = vf->prop.bbar.bottom - vf->prop.bbar.top  ;

				if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top + content_height/2   ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = vf->height/2 + (ppmgr_cutwin_top - content_top);
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
					frame->content_top = content_top + content_height/2 ;
					frame->content_left = content_left ;
					frame->content_width = content_width    ;
					frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_width/2 + content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = vf->width/2  +  (ppmgr_cutwin_left - content_left) ;
							frame->content_width = content_width/2 - 2 *(ppmgr_cutwin_left - content_left);
						}
				}else{
					frame->content_top = content_top  ;
					frame->content_left = content_width/2 + content_left ;
					frame->content_width = content_width/2    ;
					frame->content_height = content_height  ;
				}
				}
				frame->frame_top = 0;
				frame->frame_left = 0;
				frame->frame_width= vf->width;
				frame->frame_height = vf->height;
				break;
			case TVIN_TFMT_3D_FP:
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
				if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top + content_height/2   ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = vf->height/2 + (ppmgr_cutwin_top - content_top);
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
					frame->content_top = content_top + content_height/2 ;
					frame->content_left = content_left ;
					frame->content_width = content_width    ;
					frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_width/2 + content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = vf->width/2  +  (ppmgr_cutwin_left - content_left) ;
							frame->content_width = content_width/2 - 2 *(ppmgr_cutwin_left - content_left);
						}
				}else{
					frame->content_top = content_top  ;
					frame->content_left = content_width/2 + content_left ;
					frame->content_width = content_width/2    ;
					frame->content_height = content_height  ;
				}
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
			case 	TVIN_TFMT_2D:
			content_top = 0  ;
			content_left = 0  ;
			content_width = vf->width;    ;
			content_height = vf->height;
			if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top + content_height/2   ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = vf->height/2 + (ppmgr_cutwin_top - content_top);
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
				frame->content_top = content_top + content_height/2 ;
				frame->content_left = content_left ;
				frame->content_width = content_width    ;
				frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_width/2 + content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = vf->width/2  +  (ppmgr_cutwin_left - content_left) ;
							frame->content_width = content_width/2 - 2 *(ppmgr_cutwin_left - content_left);
						}
			}else{
				frame->content_top = content_top  ;
				frame->content_left = content_width/2 + content_left ;
				frame->content_width = content_width/2    ;
				frame->content_height = content_height  ;
			}
				}
				frame->frame_top = 0;
				frame->frame_left = 0;
				frame->frame_width= vf->width;
				frame->frame_height = vf->height;

			break;
			case TVIN_TFMT_3D_FP:
			case TVIN_TFMT_3D_LRH_OLER:
			case TVIN_TFMT_3D_TB:
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
				if((cur_process_type == TYPE_BT)||(status &BT_FORMAT_INDICATOR)){
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top + content_height/2   ;
							frame->content_height = content_height/2  ;
						}else{
							frame->content_top = vf->height/2 + (ppmgr_cutwin_top - content_top);
							frame->content_height = content_height/2 - 2* (ppmgr_cutwin_top - content_top);
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_left  ;
							frame->content_width = content_width ;
						}else{
							frame->content_left = ppmgr_cutwin_left ;
							frame->content_width = vf->width - 2*ppmgr_cutwin_left ;
						}

					}else{
					frame->content_top = content_top + content_height/2 ;
					frame->content_left = content_left ;
					frame->content_width = content_width    ;
					frame->content_height = content_height/2  ;
					}
				}else{
					if(is_need_cut_window_support(vf)){
						if(content_top >= ppmgr_cutwin_top ){
							frame->content_top = content_top  ;
							frame->content_height = content_height  ;
						}else{
							frame->content_top = ppmgr_cutwin_top ;
							frame->content_height = vf->height - 2*ppmgr_cutwin_top ;;
						}
						if(content_left >= ppmgr_cutwin_left ){
							frame->content_left = content_width/2 + content_left  ;
							frame->content_width = content_width/2 ;
						}else{
							frame->content_left = vf->width/2  +  (ppmgr_cutwin_left - content_left) ;
							frame->content_width = content_width/2 - 2 *(ppmgr_cutwin_left - content_left);
						}
				}else{
					frame->content_top = content_top  ;
					frame->content_left = content_width/2 + content_left ;
					frame->content_width = content_width/2    ;
					frame->content_height = content_height  ;
				}
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



static int get_output_format(int flag)
{
    return frame_info.format;
}

static int get_output_width(int flag)
{
    const vinfo_t *vinfo;
    vinfo = get_current_vinfo();
    switch(flag){
        case 0:
        case 2:
            return frame_info.width;
            break;
        case 1:
            return vinfo->width;
            break;
        default:
            break;
    }
    return 0;
}
void x_offset_adjust(vframe_t* vf , int* offset , int screen_width ,int v_screen_width )
{
	int ww;
	int ll = *offset;
	int trick_mode =0 ;

	ww = screen_width ;

//	if(current_view_mode == VIEWMODE_4_3){
//		return;
//	}
//	printk("pre offset is %d \n" , *offset);
	query_video_status(0 , &trick_mode) ;
    if(trick_mode){
    	return;
    }
	if((vf->width < 960)||((vf->type&VIDTYPE_VIU_422)&&(cur_process_type !=TYPE_2D_TO_3D))||(ww <= v_screen_width)){
		return;
	}
	if(vf->width <= 1280){
		v_screen_width  = 1280;
	}
	*offset=  (((v_screen_width << 16)/ww)*ll) >>16;

//	printk("after offset is %d \n" , *offset);
}



void axis_h_adjust(vframe_t* vf , int* left ,  int* width , int* screen_width ,int v_screen_width )
{
	int l,w ,ww;
	int trick_mode =0 ;

	ww = *screen_width ;

	query_video_status(0 , &trick_mode) ;
    if(trick_mode){
    	return;
    }
	if(((vf->type&VIDTYPE_VIU_422)&&(cur_process_type !=TYPE_2D_TO_3D))||(ww <= v_screen_width)){
		return;
	}
	if(vf->width <= 1280){
		v_screen_width  = 1280;
	}
	l = *left ;
	w =*width;
	*left=  (((v_screen_width << 16)/ww)*l) >>16;
	*width = (((v_screen_width << 16)/ww)*w) >> 16;
	*width = (*width + 1)&(~1);
	*screen_width = v_screen_width ;
}

int get_output_height(int flag)
{
    const vinfo_t *vinfo;
    vinfo = get_current_vinfo();
    switch(flag){
        case 0:
        case 2:
            return frame_info.height;
            break;
        case 1:
            return vinfo->height;
            break;
        default:
            break;
    }
    return frame_info.height;
}

static int get_output_rect_after_ratio(vframe_t* vf ,int* top , int*left , int* width , int* height ,int in_width, int in_height, int output_width,int output_height)
{
    int t,l,w,h;
    int current_view_mode = 0 ;
    w = output_width;
    h = output_height;
    t = 0 ;
    l = 0;
    current_view_mode  = get_ppmgr_view_mode();
    switch(current_view_mode){
    	case VIEWMODE_4_3:
    	//vf->ratio_control = ((3 <<8)/4) << DISP_RATIO_ASPECT_RATIO_BIT;
    	vf->ratio_control = (0xc0 << DISP_RATIO_ASPECT_RATIO_BIT);
    	break;
    	case VIEWMODE_16_9:
    	//vf->ratio_control = ((9 << 8)/16) << DISP_RATIO_ASPECT_RATIO_BIT ;
    	vf->ratio_control = (0x90 << DISP_RATIO_ASPECT_RATIO_BIT) ;
    	break;
    	case VIEWMODE_NORMAL:
    	default:
    	break;
    }

    if (vf->ratio_control) {
        int ar = (vf->ratio_control >> DISP_RATIO_ASPECT_RATIO_BIT) & 0x3ff;
        if ((ar * output_width) > (output_height << 8)) {
            w = (output_height << 8) / ar;
            l = (output_width - w) / 2;
        } else {
            h = (output_width * ar) >> 8;
            t = (output_height - h) / 2;
        }
    } else {
        if ((in_height * output_width) > (output_height *in_width)) {
            w = (output_height * in_width) / in_height;
            l = (output_width - w) / 2;
        } else {
            h = output_width * in_height / in_width;
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


int get_2d_output_rect_after_ratio(int* top , int*left , int* width , int* height ,int in_width, int in_height, int screen_width,int screen_height)
{
    int ww,hh;
    int current_view_mode = 0 ;

    ww = in_width;
    hh = in_height;
#if 0
    if(ww * screen_height   > hh* screen_width){
    	hh = (((ww * screen_height ) << 8)/screen_width) >> 8;
    }else{
    	ww = (((screen_width * hh) << 8) /screen_height) >> 8 ;
    }
#else
	ww = screen_width;
	hh = screen_height;
#endif
    current_view_mode  = get_ppmgr_view_mode();
    switch(current_view_mode){
    	case VIEWMODE_4_3:
		*top = 0;
		*left = ww >>3;
		*width = (ww*3) >>2  ;
		*height = hh;

    	break;
    	case VIEWMODE_16_9:
		*top = 0;
		*left = 0;
		*width = ww ;
		*height = hh;
    	break;
    	case VIEWMODE_NORMAL:
    	default:
		*top = 0;
		*left = 0;
		*width =  in_width ;
		*height = in_height;
    	break;
    }

    return 0;
}

/*for decoder input processing
    1. output window should 1:1 as source frame size
    2. keep the frame ratio
    3. input format should be YUV420 , output format should be YUV444
*/
static void process_none(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cs0,cs1,cs2,cd;
    //new_vf = &vfpool[fill_ptr];
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.frame_width;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
	    new_vf->height= input_frame.frame_height;
	}else{
		new_vf->height= input_frame.frame_height <<1;
	}
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->mode_3d_enable = 0 ;
    //new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(index);
    /* data operating. */
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
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top =    0;
    ge2d_config->src_para.left =   0;
    ge2d_config->src_para.width =  vf->width;
    ge2d_config->src_para.height = vf->height;

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
    ge2d_config->dst_para.width = new_vf->width;
    ge2d_config->dst_para.height = new_vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,input_frame.content_left,input_frame.content_top,input_frame.content_width ,input_frame.content_height,0,0,new_vf->width,new_vf->height);
stretchblt_noalpha(context,input_frame.frame_left,input_frame.frame_top,input_frame.frame_width ,input_frame.frame_height,0,0,new_vf->width,new_vf->height);

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

static int ratio_value = 10; // 0~255
static void process_2d_to_3d(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    int t,l,w,h,w1,h1,w2,h2;
    canvas_t cs0,cs1,cs2,cd;
    unsigned x_offset = 0, y_offset = 0;
    unsigned cut_w = 0,cut_h = 0;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);


//    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 1 ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }

    cut_w = (((input_frame.frame_width<<8) + 0x80) * ratio_value)>>16;
    cut_h = (((input_frame.frame_height<<8) + 0x80) * ratio_value)>>16;
    x_offset = cut_w>>1;
    x_offset = x_offset & 0xfffffffe;
    y_offset = cut_h>>1;
    y_offset = y_offset & 0xfffffffe;

    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    ge2d_config->src_para.height = vf->height;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.frame_width;
    h1 = input_frame.frame_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    //w2 = vf->width ;
    //h2 = vf->height;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

 //   printk("t:%d l:%d w:%d h%d \n",t,l,w,h);
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

//    stretchblt_noalpha(context,0,0,vf->width/2,vf->height,t,l,w,h);
//   stretchblt_noalpha(context,0,0,vf->width,vf->height,l,t,w,h);
    stretchblt_noalpha(context,input_frame.frame_left,input_frame.frame_top,input_frame.frame_width ,input_frame.frame_height,l,t,w,h);

//ROUND_2:
    /* data operating. */
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = y_offset;
    ge2d_config->src_para.left = x_offset;
    ge2d_config->src_para.width = vf->width-cut_w;
    ge2d_config->src_para.height = vf->height-cut_h;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.frame_width;
    h1 = input_frame.frame_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    //w2 = vf->width ;
    //h2 = vf->height;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

//    stretchblt_noalpha(context,0,vf->width/2,vf->width/2,vf->height,t,l,w,h);
//    stretchblt_noalpha(context, x_offset, y_offset, vf->width-cut_w,vf->height-cut_h,l,t,w,h);

   stretchblt_noalpha(context,input_frame.frame_left + x_offset,input_frame.frame_top + y_offset,input_frame.frame_width -cut_w ,input_frame.frame_height-cut_h,l,t,w,h);
    new_vf->width = w2;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;


    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
    return;
}

void process_2d_to_3d_switch(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    int t,l,w,h,w1,h1,w2,h2;
    canvas_t cs0,cs1,cs2,cd;
    unsigned x_offset = 0, y_offset = 0;
    unsigned cut_w = 0,cut_h = 0;

    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
//    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }

    cut_w = (((vf->width<<8) + 0x80) * ratio_value)>>16;
    cut_h = (((vf->height<<8) + 0x80) * ratio_value)>>16;
    x_offset = cut_w>>1;
    x_offset = x_offset & 0xfffffffe;
    y_offset = cut_h>>1;
    y_offset = y_offset & 0xfffffffe;

    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = y_offset;
    ge2d_config->src_para.left = x_offset;
    ge2d_config->src_para.width = vf->width-cut_w;
    ge2d_config->src_para.height = vf->height-cut_h;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = vf->width;
    h1 = vf->height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    //w2 = vf->width ;
    //h2 = vf->height;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

 //   printk("t:%d l:%d w:%d h%d \n",t,l,w,h);
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

//    stretchblt_noalpha(context,0,0,vf->width/2,vf->height,t,l,w,h);
    stretchblt_noalpha(context, x_offset, y_offset, vf->width-cut_w,vf->height-cut_h,l,t,w,h);

//ROUND_2:
    /* data operating. */
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = y_offset;
    ge2d_config->src_para.left = x_offset;
    ge2d_config->src_para.width = vf->width-cut_w;
    ge2d_config->src_para.height = vf->height-cut_h;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = vf->width;
    h1 = vf->height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    //w2 = vf->width ;
    //h2 = vf->height;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

//    stretchblt_noalpha(context,0,vf->width/2,vf->width/2,vf->height,t,l,w,h);
    stretchblt_noalpha(context,0,0,vf->width,vf->height,l,t,w,h);
    new_vf->width = w2;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

/*for 3D video input processing
    1. output window should 1:1 as video layer size
    2. must adjust GE2D operation according with the frame ratio ,then clear ratio control flag
    3. need generate two buffer from source frame
    4. input format should be YUV422 , output format should be YUV444
*/
void process_lr(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    int t,l,w,h,w1,h1,w2,h2;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.content_width;
    new_vf->height= input_frame.content_height;

    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 1;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;

//    printk("canvas 0 is %d , width is %d , height is %d \n" ,vf->canvas0Addr ,cs0.width,  cs0.height ) ;
	ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->src_para.format = get_input_format(vf);
	ge2d_config->src_para.fill_color_en = 0;
	ge2d_config->src_para.fill_mode = 0;
	ge2d_config->src_para.x_rev = 0;
	ge2d_config->src_para.y_rev = 0;
	ge2d_config->src_para.color = 0xffffffff;
	ge2d_config->src_para.top = 0;
	ge2d_config->src_para.left = 0;
	ge2d_config->src_para.width = vf->width;
	if(is_vertical_sample_enable(vf)){
		ge2d_config->src_para.height = vf->height/2;
	}else{
		ge2d_config->src_para.height = vf->height;
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
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2/2;

//    printk("t:%d l:%d w:%d h%d w2:%d h2:%d\n",t,l,w,h,w2,h2);
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,0,0,vf->width/2,vf->height,t,l,w,h);
	    axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
	if(is_vertical_sample_enable(vf)){
    		stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,l,t,w,h);
	}else{
		stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,l,t,w,h);
	}
//ROUND_2:
    /* data operating. */
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas1Addr;

//    printk("canvas 1 is %d , width is %d , height is %d \n" ,vf->canvas1Addr ,cs0.width,  cs0.height ) ;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    if(is_vertical_sample_enable(vf)){
    		ge2d_config->src_para.height = vf->height/2;
	}else{
		ge2d_config->src_para.height = vf->height ;
	}

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2/2;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,vf->width/2 ,0,vf->width/2,vf->height,l,t,w,h);
	axis_h_adjust(vf , &l ,  &w , &w2 , get_ppmgr_scale_width() );
	if(is_vertical_sample_enable(vf)){
    		stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,l,t,w,h);
	}else{
		stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height ,l,t,w,h);
	}
    new_vf->width =  w2;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;
    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

void process_bt(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    int t,l,w,h,w1,h1,w2,h2;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.content_width;
    new_vf->height= input_frame.content_height;

    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 1 ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
	if(is_vertical_sample_enable(vf)){
		ge2d_config->src_para.height = vf->height/2;
	}else{
		ge2d_config->src_para.height = vf->height;
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
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
#if 1
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
#else
    w2 = vf->width ;
    h2 = vf->height;
#endif

	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

//    printk("t:%d l:%d w:%d h%d \n",t,l,w,h);
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

 //   stretchblt_noalpha(context,0,0,vf->width,vf->height/2,l,t,w,h);

 //   stretchblt_noalpha(context,0,input_frame.content_top,input_frame.frame_width,input_frame.content_height/2,l,t,w,h);
 	axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
	 if(is_vertical_sample_enable(vf)){
	 	stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,l,t,w,h);
	}else{
	 	stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,l,t,w,h);
	}

//ROUND_2:
    /* data operating. */
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas1Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
	if(is_vertical_sample_enable(vf)){
		ge2d_config->src_para.height = vf->height/2;
	}else{
		ge2d_config->src_para.height = vf->height;
	}

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,0 ,vf->height/2,vf->width,vf->height/2,l,t,w,h);
//   stretchblt_noalpha(context,0,input_frame.frame_height/2,input_frame.frame_width,input_frame.content_height/2,l,t,w,h);
    axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
    if(is_vertical_sample_enable(vf)){
		stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,l,t,w,h);
	}else{
		stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height,l,t,w,h);
	}
    new_vf->width = w2;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

void process_lr_switch(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
#if 0
    vframe_t* new_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = &vfpool[fill_ptr];
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.frame_width;
    new_vf->height= input_frame.frame_height;

    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(index);
    /* data operating. */
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
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    ge2d_config->src_para.height = vf->height;

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
    ge2d_config->dst_para.width = new_vf->width;
    ge2d_config->dst_para.height = new_vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,0,0,vf->width/2,vf->height,new_vf->width/2,0,new_vf->width/2,new_vf->height);

    stretchblt_noalpha(context,input_frame.content_left,0,input_frame.content_width/2,input_frame.content_height, new_vf->width/2,0,input_frame.content_width/2 ,input_frame.frame_height);

ROUND2:
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

 //   stretchblt_noalpha(context,vf->width/2 ,0,vf->width/2,vf->height,0,0,new_vf->width/2,new_vf->height);

    stretchblt_noalpha(context,input_frame.frame_width/2,0,input_frame.content_width/2,input_frame.content_height, input_frame.content_left,0,input_frame.content_width/2 ,input_frame.frame_height);

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
#else
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    int t,l,w,h,w1,h1,w2,h2;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.content_width;
    new_vf->height= input_frame.content_height;

    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 1 ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
	if(is_vertical_sample_enable(vf)){
		ge2d_config->src_para.height = vf->height/2;
	}else{
    ge2d_config->src_para.height = vf->height;
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
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);


	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

 //   printk("t:%d l:%d w:%d h%d \n",t,l,w,h);
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,0,0,vf->width/2,vf->height,t,l,w,h);

	axis_h_adjust(vf , &l ,  &w , &w2 , get_ppmgr_scale_width() );
	if(is_vertical_sample_enable(vf)){
    		stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,l,t,w,h);
	}else{
stretchblt_noalpha(context,r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height,l,t,w,h);
	}

//ROUND_2:
    /* data operating. */
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas1Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    if(is_vertical_sample_enable(vf)){
    		ge2d_config->src_para.height = vf->height/2;
	}else{
    ge2d_config->src_para.height = vf->height;
	}

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.content_width;
    h1 = input_frame.content_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
//    stretchblt_noalpha(context,vf->width/2 ,0,vf->width/2,vf->height,l,t,w,h);
	    axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
	if(is_vertical_sample_enable(vf)){
    		stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,l,t,w,h);
	}else{
	stretchblt_noalpha(context,l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,l,t,w,h);
	}
    new_vf->width = w2;
    new_vf->height = h2 ;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;
    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);

#endif
}

extern int get_depth(void);

static int x_phase = 2048; // >0: image move to left. <0: image move to right, 0~7 is phase value, 8-30 is offset
static void process_field_depth(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    int t,l,w,h,w1,h1,w2,h2;
    int ll , tt ,ww , hh;
    int src_l , src_t, src_w ,src_h , dst_l,dst_t,dst_w , dst_h;
    int x_offset = 0;
    int type = 0 ;
    unsigned cur_phase = 0;
    unsigned cut_w = 0;
    unsigned temp_w = 0;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
//    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 1 ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    x_phase = get_depth();

//    if(x_phase<0){
//        x_offset = -(((unsigned)x_phase&0xfffffff)>>8);
//        cur_phase = x_phase&0xff;
//        if(cur_phase){
//            x_offset--;
//            cur_phase = 0x100-cur_phase;
//        }
//        cut_w = -x_offset;
//    }else if(x_phase>0){
//        x_offset = ((x_phase&0xfffffff)>>8);
//        cur_phase = x_phase&0xff;
//        cut_w = x_offset;
//    }

	if(x_phase & 0x10000000){
		type  = 0 ;
	}else{
		type =  1;
	}


    new_vf->canvas0Addr = index2canvas_0(index);
    new_vf->canvas1Addr = index2canvas_1(index);
//ROUND_1:
    /* data operating. */
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

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
	if(is_vertical_sample_enable(vf)){
		ge2d_config->src_para.height = vf->height/2;
	}else{
		ge2d_config->src_para.height = vf->height;
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
    t = 0;
    l = 0;
    w = 0;
    h = 0;

    if(is_need_cut_window_support(vf)){
    	w1 = input_frame.frame_width - 2*ppmgr_cutwin_left ;
    	h1 = input_frame.frame_height - 2*ppmgr_cutwin_top;
    }else{
    w1 = input_frame.frame_width;
    h1 = input_frame.frame_height ;
    }
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2/2;
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
    x_phase  &= 0xfffffff;
    x_offset_adjust(vf , &x_phase,w2 ,get_ppmgr_scale_width());

	x_offset = ((x_phase&0xfffffff)>>8);
	x_offset &= ~1;
	cur_phase = x_phase&0xff;
	cut_w = x_offset;

	axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );

	if(is_need_cut_window_support(vf)){
		ll = input_frame.frame_left + ppmgr_cutwin_left ;
		tt = input_frame.frame_top + ppmgr_cutwin_top;
		ww = input_frame.frame_width - 2*ppmgr_cutwin_left ;
		hh = input_frame.frame_height - 2*ppmgr_cutwin_top;
	}else{
		ll = input_frame.frame_left ;
		tt = input_frame.frame_top;
		ww = input_frame.frame_width;
		hh = input_frame.frame_height;
	}
	if(is_vertical_sample_enable(vf)){
		stretchblt_noalpha(context,ll,tt,ww ,hh/2,l,t,w,h);
	}else{
		stretchblt_noalpha(context,ll,tt,ww ,hh,l,t,w,h);
	}

//ROUND_2:
    /* data operating. */
  //  if(w1 >= 1280 ){
  if (0){
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
    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    ge2d_config->src_para.height = vf->height;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    w1 = input_frame.frame_width;
    h1 = input_frame.frame_height ;
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);

    temp_w = (input_frame.frame_width-cut_w)*w2/w1;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,h1,w2,h2);
	}else{
	      get_output_rect_after_ratio(vf,&t,&l,&w,&h,w1,2*h1,w2,h2);
	}
		t >>=1;
		h >>=1;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2/2;
    if(ge2d_config->hf_init_phase){
        ge2d_config->hf_rpt_num = 1;
        ge2d_config->hf_init_phase = cur_phase<<16;
    }
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
   stretchblt_noalpha(context,(x_offset<0)?0:(input_frame.frame_left+x_offset) ,input_frame.frame_top,input_frame.frame_width - cut_w ,input_frame.frame_height,(x_offset<0)?(l+w2-temp_w):l,t,temp_w,h);
}else{
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
    ge2d_config->src_para.width = w2;
    ge2d_config->src_para.height = h2/2;

    canvas_read(new_vf->canvas0Addr&0xff,&cd);

    ge2d_config->src_planes[0].addr = cd.addr;
    ge2d_config->src_planes[0].w = cd.width;
    ge2d_config->src_planes[0].h = cd.height;

    canvas_read(new_vf->canvas1Addr&0xff,&cd);

    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas1Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    if(type == 0){
	    src_t = 0;
	    src_l =  x_offset;
	    src_w = (w2- x_offset);
	    src_h = h2/2;

	    dst_t = 0 ;
	    dst_l = 0;
	    dst_w = src_w;
	    dst_h = h2/2;
	}else{
	    src_t = 0;
	    src_l =  0;
	    src_w =(w2- x_offset);
	    src_h = h2/2;

	    dst_t = 0 ;
	    dst_l = x_offset ;
	    dst_w = src_w;
	    dst_h = h2/2;
	}
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = w2;
    ge2d_config->dst_para.height = h2/2;
    if(ge2d_config->hf_init_phase){
        ge2d_config->hf_rpt_num = 1;
        ge2d_config->hf_init_phase = cur_phase<<16;
    }
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

   stretchblt_noalpha(context,src_l , src_t, src_w ,src_h , dst_l,dst_t,dst_w , dst_h);

   if(type == 0){
	    src_t = 0;
	    src_l =  0;
	    src_w =  x_offset;
	    src_h = h2/2;

	    dst_t = 0 ;
	    dst_l = w2 - x_offset;
	    dst_w = x_offset;
	    dst_h = h2/2;

	}else{
	    src_t = 0;
	    src_l =  w2 - x_offset;
	    src_w =  x_offset;
	    src_h = h2/2;

	    dst_t = 0 ;
	    dst_l = 0 ;
	    dst_w = x_offset;
	    dst_h = h2/2;
	}
	if(src_w > 0){
		window_clear_3D(context ,ge2d_config ,new_vf->canvas0Addr , src_l ,src_t ,src_w ,src_h);
		window_clear_3D(context ,ge2d_config ,new_vf->canvas1Addr , dst_l ,dst_t ,dst_w ,dst_h);
}
}
    new_vf->width = w2;
    new_vf->height = h2 ;
    new_vf->ratio_control = 0;

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
    return;
}

void process_3d_to_2d_l(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cs0,cs1,cs2,cd;
    int t,l,w,h,w1,h1,w2,h2;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);

    new_vf->width= input_frame.content_width;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
	    new_vf->height= input_frame.content_height;
	}else{
		new_vf->height= input_frame.content_height <<1;
	}
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    if(is_need_cut_window_support(vf)){
    	w1 = input_frame.frame_width - 2*ppmgr_cutwin_left ;
    	h1 = input_frame.frame_height - 2*ppmgr_cutwin_top;
    }else{
    w1 = new_vf->width;
    h1 = new_vf->height ;
    }

    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    get_2d_output_rect_after_ratio(&t,&l,&w,&h,w1,h1,w2,h2);
    new_vf->width  = w + 2*l ;
    new_vf->height = h + 2*t;
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->mode_3d_enable = 0 ;
    //new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(index);
    /* data operating. */
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
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
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
    ge2d_config->src_para.height = vf->height;
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
    ge2d_config->dst_para.width = new_vf->width;
    ge2d_config->dst_para.height = new_vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
    axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
    if(is_vertical_sample_enable(vf)){
    	stretchblt_noalpha(context, l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height/2,l,t,w,h);
	}else{
    stretchblt_noalpha(context, l_frame.content_left,l_frame.content_top,l_frame.content_width,l_frame.content_height,l,t,w,h);
	}

    new_vf->width = w2 ;

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

void process_3d_to_2d_r(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cs0,cs1,cs2,cd;
    int t,l,w,h,w1,h1,w2,h2;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
    get_input_frame(vf,&input_frame);
    get_input_l_frame(vf,&l_frame);
    get_input_r_frame(vf,&r_frame);
    new_vf->width= input_frame.content_width;
	if(!(vf->type & VIDTYPE_PRE_INTERLACE)){
		new_vf->height= input_frame.content_height;
	}else{
		new_vf->height= input_frame.content_height <<1;
	}
    t = 0;
    l = 0;
    w = 0;
    h = 0;
    if(is_need_cut_window_support(vf)){
    	w1 = input_frame.frame_width - 2*ppmgr_cutwin_left ;
    	h1 = input_frame.frame_height - 2*ppmgr_cutwin_top;
    }else{
    w1 = new_vf->width;
    h1 = new_vf->height ;
    }
    w2 = get_output_width(1) ;
    h2 = get_output_height(1);
    get_2d_output_rect_after_ratio(&t,&l,&w,&h,w1,h1,w2,h2);
    new_vf->width  = w + 2*l ;
    new_vf->height = h + 2*t;
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->mode_3d_enable = 0 ;
    //new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(index);
    /* data operating. */
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
    ge2d_config->src_para.canvas_index=vf->canvas1Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(vf);
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
    ge2d_config->src_para.height = vf->height;
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
    ge2d_config->dst_para.width = new_vf->width;
    ge2d_config->dst_para.height = new_vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }

    axis_h_adjust(vf, &l ,  &w , &w2 ,  get_ppmgr_scale_width() );
    if(is_vertical_sample_enable(vf)){
    	stretchblt_noalpha(context, r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height/2,l,t,w,h);
	}else{
    stretchblt_noalpha(context, r_frame.content_left,r_frame.content_top,r_frame.content_width,r_frame.content_height,l,t,w,h);
	}
    new_vf->width = w2 ;

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

/*for camera input processing*/
void process_camera_input(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    vframe_t* new_vf;
    ppframe_t *pp_vf;
    int index;
    canvas_t cs0,cs1,cs2,cd;
    new_vf = vfq_pop(&q_free);

    if (unlikely((!new_vf) || (!vf)))
        return;  
    pp_vf = to_ppframe(new_vf);
    pp_vf->dec_frame = NULL;
    memcpy(new_vf , vf, sizeof(vframe_t));
 //   new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;/*vf->type;*/
    new_vf->type = VIDTYPE_VIU_444|VIDTYPE_VIU_SINGLE_PLANE |  VIDTYPE_PROGRESSIVE ;
    new_vf->mode_3d_enable = 0 ;
    new_vf->width = get_output_width(2);
    new_vf->height = get_output_height(2);

    index = pp_vf->index;
    if(index < 0){
        printk("======decoder is full\n");
        //return -1;
    }
    new_vf->canvas0Addr = new_vf->canvas1Addr = index2canvas(index);

    /* data operating. */
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
    ge2d_config->src_para.canvas_index=vf->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = GE2D_FORMAT_M24_YUV420;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = vf->width;
    ge2d_config->src_para.height = vf->height;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index=new_vf->canvas0Addr;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = get_output_format(0);
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = new_vf->width;
    ge2d_config->dst_para.height = new_vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
    stretchblt_noalpha(context,0,0,vf->width,vf->height,0,0,new_vf->width,new_vf->height);

    ppmgr_vf_put_dec(vf);
    vfq_push(&q_ready, new_vf);
}

static void buffer_clear_2D(ge2d_context_t *context, config_para_ex_t* ge2d_config,int index)
{
    int i ,t,l,w,h1,h2;
    int current_view_mode;

    t = 0;
    l = 0;
    //w = get_output_width(1) ;
    //h = get_output_height(1);
    w = 1920 ;
    h1 = 1088;
    h2 = 544;

	current_view_mode  = get_ppmgr_view_mode();
    for (i = 0; i < VF_POOL_SIZE; i++){
    	if((index >= 0)&&(index != i)){
    		continue;
    	}
    	if((index >=0 )&&(current_view_mode == VIEWMODE_4_3)){
    		w = 240;
    	}
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;

        ge2d_config->src_para.canvas_index=PPMGR_CANVAS_INDEX + i;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = w;
        ge2d_config->src_para.height = h1;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.canvas_index=PPMGR_CANVAS_INDEX + i;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;


        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = w;
        ge2d_config->dst_para.height = h1;
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            return;
        }
        fillrect(context ,l,t,w,h1,0x00808000 )    ;
    }
}

static void buffer_clear_3D(ge2d_context_t *context, config_para_ex_t* ge2d_config,int index)
{
    int i ,t,l,w,h1,h2;
    t = 0;
    l = 0;
    w = 1920 ;
    h1 = 1088;
    h2 = 544;
    for (i = 0; i < VF_POOL_SIZE; i++){
    	if((index >= 0)&&(index != i)){
    		continue;
    	}
        ge2d_config->alu_const_color= 0;//0x000000ff;
        ge2d_config->bitmask_en  = 0;
        ge2d_config->src1_gb_alpha = 0;//0xff;
        ge2d_config->dst_xy_swap = 0;

        ge2d_config->src_key.key_enable = 0;
        ge2d_config->src_key.key_mask = 0;
        ge2d_config->src_key.key_mode = 0;
        ge2d_config->src_para.canvas_index=PPMGR_DOUBLE_CANVAS_INDEX + i;
        ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->src_para.fill_color_en = 0;
        ge2d_config->src_para.fill_mode = 0;
        ge2d_config->src_para.x_rev = 0;
        ge2d_config->src_para.y_rev = 0;
        ge2d_config->src_para.color = 0;
        ge2d_config->src_para.top = 0;
        ge2d_config->src_para.left = 0;
        ge2d_config->src_para.width = w;
        ge2d_config->src_para.height = h2;

        ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
        ge2d_config->dst_para.canvas_index=PPMGR_DOUBLE_CANVAS_INDEX +  i;
        ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

        ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
        ge2d_config->dst_para.fill_color_en = 0;
        ge2d_config->dst_para.fill_mode = 0;
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
        ge2d_config->dst_para.color = 0;


        ge2d_config->dst_para.top = 0;
        ge2d_config->dst_para.left = 0;
        ge2d_config->dst_para.width = w;
        ge2d_config->dst_para.height = h2;
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            return;
        }
        fillrect(context ,l,t,w,h2,0x00808000 )    ;

       ge2d_config->src_para.canvas_index=PPMGR_DOUBLE_CANVAS_INDEX + 4 + i;
        ge2d_config->dst_para.canvas_index=PPMGR_DOUBLE_CANVAS_INDEX + 4 + i;
        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            printk("++ge2d configing error.\n");
            return;
        }
        fillrect(context ,l,t,w,h2,0x00808000 )    ;
    }
}

static void window_clear_3D(ge2d_context_t *context, config_para_ex_t* ge2d_config,int index ,int l ,int t ,int w ,int h)
{
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_para.canvas_index=  index;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = 1920;
    ge2d_config->src_para.height = 544;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index= index;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.format = GE2D_FORMAT_S24_YUV444;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;


    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = 1920;
    ge2d_config->dst_para.height = 544;
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
    fillrect(context ,l,t,w,h,0x00808000 )    ;
}
void ppmgr_buffer_clear(ge2d_context_t *context, config_para_ex_t* ge2d_config)
{
    switch(cur_process_type){
    	case TYPE_NONE:
    	case TYPE_3D_TO_2D_L:
    	case TYPE_3D_TO_2D_R:
    	buffer_clear_2D(context ,ge2d_config , -1)	;
    	break;

    	case TYPE_2D_TO_3D:
    	case TYPE_LR:
    	case TYPE_BT:
    	case TYPE_LR_SWITCH:
    	case TYPE_FILED_DEPTH:
    	buffer_clear_3D(context ,ge2d_config, -1)	;
    	break;
    	default:
    	break;
    }
}

void ppmgr_index_clear(ge2d_context_t *context, config_para_ex_t* ge2d_config ,int index)
{
    switch(cur_process_type){
    	case TYPE_NONE:
    	case TYPE_3D_TO_2D_L:
    	case TYPE_3D_TO_2D_R:
    	buffer_clear_2D(context ,ge2d_config,index)	;
    	break;

    	case TYPE_2D_TO_3D:
    	case TYPE_LR:
    	case TYPE_BT:
    	case TYPE_LR_SWITCH:
    	case TYPE_FILED_DEPTH:
    	buffer_clear_3D(context ,ge2d_config,index)	;
    	break;
    	default:
    	break;
    }
}




void ppmgr_vf_3d_tv(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    display_frame_t input_frame ;
    display_frame_t l_frame ,r_frame ;
    canvas_t cd;
    int process_type = get_tv_process_type(vf);
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
    switch(process_type){
        case TYPE_NONE           :
 //           printk("process  none type\n");
// 		enable_vscaler();
            process_none(vf,context,ge2d_config);
            break;
        case TYPE_2D_TO_3D       :
 //           printk("process 2d to 3d type\n");
  //          process_2d_to_3d(vf,context,ge2d_config);
            //process_none(vf,context,ge2d_config);
 //           disable_vscaler();
            process_field_depth(vf,context,ge2d_config);
            break;
        case TYPE_LR             :
 //           printk("process  lr type\n");
// 		disable_vscaler();
            process_lr(vf,context,ge2d_config);
            break;
        case TYPE_BT             :
 //           printk("process  bt type\n");
// 		disable_vscaler();
            process_bt(vf,context,ge2d_config);
            break;
        case TYPE_LR_SWITCH      :
 //           printk("process  lr switch type\n");
// 		disable_vscaler();
            process_lr_switch(vf,context,ge2d_config);
            break;
        case TYPE_FILED_DEPTH    :
//            printk("process field depth type\n");
 //           process_2d_to_3d(vf,context,ge2d_config);
// 		disable_vscaler();
            process_field_depth(vf,context,ge2d_config);
            //process_none(vf,context,ge2d_config);
            break;
        case TYPE_3D_TO_2D_L     :
//            printk("process  3d to 2d l type\n");
//		enable_vscaler();
            process_3d_to_2d_l(vf,context,ge2d_config);
            break;
        case TYPE_3D_TO_2D_R     :
//        	enable_vscaler();
 //           printk("process  3d to  2d r type\n");
            process_3d_to_2d_r(vf,context,ge2d_config);
            break;
        default:
        break;
    }
}
