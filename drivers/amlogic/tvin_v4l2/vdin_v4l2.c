#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mm.h>

#include <linux/amlogic/tvin/tvin_v4l2.h>

static struct vdin_v4l2_ops_s ops = {NULL};

int vdin_reg_v4l2(vdin_v4l2_ops_t *v4l2_ops)
{
        void * ret = 0;
        if(!v4l2_ops)
                return -1;
        ret = memcpy(&ops,v4l2_ops,sizeof(vdin_v4l2_ops_t));
        if(ret)
                return 0;
        return -1;
}
EXPORT_SYMBOL(vdin_reg_v4l2);

void vdin_unreg_v4l2(void)
{
        memset(&ops,0,sizeof(vdin_v4l2_ops_t));
}
EXPORT_SYMBOL(vdin_unreg_v4l2);


int v4l2_vdin_ops_init(vdin_v4l2_ops_t *vdin_v4l2p)
{
        void * ret = 0;
        if(!vdin_v4l2p)
                return -1;
        ret = memcpy(vdin_v4l2p,&ops,sizeof(vdin_v4l2_ops_t));
        if(ret)
                return 0;
        return -1;
}
EXPORT_SYMBOL(v4l2_vdin_ops_init);

vdin_v4l2_ops_t *get_vdin_v4l2_ops()
{
        if((ops.start_tvin_service != NULL) && (ops.stop_tvin_service != NULL))
                return &ops;
        else{
                //pr_err("[vdin..]%s: vdin v4l2 operation haven't registered.",__func__);
                return NULL;
        }
}
EXPORT_SYMBOL(get_vdin_v4l2_ops);

const char *cam_cmd_to_str(cam_command_t cmd)
{
	switch(cmd){
		case CAM_COMMAND_INIT:
			return "CAM_COMMAND_INIT";
		        break;
		case CAM_COMMAND_GET_STATE:
			return "CAM_COMMAND_GET_STATE";
			break;
                case CAM_COMMAND_SCENES:
			return "CAM_COMMAND_SCENES";
			break;
                case CAM_COMMAND_EFFECT:
			return "CAM_COMMAND_EFFECT";
			break;
                case CAM_COMMAND_AWB:
			return "CAM_COMMAND_AWB";
			break;
                case CAM_COMMAND_MWB:
			return "CAM_COMMAND_MWB";
			break;
                case CAM_COMMAND_SET_WORK_MODE:
			return "CAM_COMMAND_SET_WORK_MODE";
			break;
                case CAM_COMMAND_AE_ON:
			return "CAM_COMMAND_AE_ON";
			break;
                case CAM_COMMAND_AE_OFF:
			return "CAM_COMMAND_AE_OFF";
			break;
		case CAM_COMMAND_SET_AE_LEVEL:
			return "CAM_COMMAND_SET_AE_LEVEL";
			break;
                case CAM_COMMAND_AF:
			return "CAM_COMMAND_AF";
			break;
                case CAM_COMMAND_FULLSCAN:
		       	return "CAM_COMMAND_FULLSCAN";
			break;
		case CAM_COMMAND_TOUCH_FOCUS:
			return "CAM_COMMAND_TOUCH_FOCUS";
			break;
		case CAM_COMMAND_CONTINUOUS_FOCUS_ON:
			return "CAM_COMMAND_CONTINUOUS_FOCUS_ON";
			break;
                case CAM_COMMAND_CONTINUOUS_FOCUS_OFF:
			return "CAM_COMMAND_CONTINUOUS_FOCUS_OFF";
			break;
		case CAM_COMMAND_BACKGROUND_FOCUS_ON:
			return "CAM_COMMAND_BACKGROUND_FOCUS_ON";
			break;
		case CAM_COMMAND_BACKGROUND_FOCUS_OFF:
			return "CAM_COMMAND_BACKGROUND_FOCUS_OFF";
			break;
		case CAM_COMMAND_SET_FLASH_MODE:
        	        return "CAM_COMMAND_SET_FLASH_MODE";
			break;
                case CAM_COMMAND_TORCH:
			return "CAM_COMMAND_TORCH";
			break;
		case CMD_ISP_BYPASS:
			return "CMD_ISP_BYPASS";
			break;
		default:
			break;
	}
	return NULL;
}
EXPORT_SYMBOL(cam_cmd_to_str);
