/*******************************************************************
 **
 ** Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 **
 **  Description:
 **		this file decode logo data according to it's type.
 **
 **  Author: Amlogic Software
 **  Created: 10/08/23
 **
 *******************************************************************
 **
 ** function :we will not use interrupt mode ,and ge2d device only process one cmd here.
 **
 *******************************************************************/
 #include "logo.h"
 #include "dev_ge2d.h"
 #include	"amlogo_log.h"
 #include <linux/amlogic/amlog.h>
ge2d_context_t*	dev_ge2d_setup(void* para)
{
	config_para_t  *config=(config_para_t*)para;
	static ge2d_context_t  context;	

	if(NULL==config) return NULL;

	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"current ge2d type:%d\n",config->src_dst_type);	
	if(0==ge2d_context_config(&context,config))
	{
		return &context;
	}else{
		return NULL;
	}
	
}
int	dev_ge2d_cmd(ge2d_context_t *context ,int  cmd,src_dst_info_t  *info)
{
	ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(context);
	ge2d_config_t	*cfg=&context->config;
	rectangle_t *src=&info->src_rect,*dst=&info->dst_rect;
	unsigned int color=info->color;

	ge2d_cmd_cfg->sc_hsc_en = 0;
	ge2d_cmd_cfg->sc_vsc_en = 0;
	ge2d_cmd_cfg->hsc_rpt_p0_num = 0;
	ge2d_cmd_cfg->vsc_rpt_l0_num = 0;
	ge2d_cmd_cfg->hsc_div_en = 0; 
	switch (cmd)
	{	
		case	CMD_FILLRECT:
		cfg->src1_data.def_color=color;
		ge2d_cmd_cfg->dst_x_start  = dst->x;
    		ge2d_cmd_cfg->dst_x_end    = dst->x+dst->w-1;
    		ge2d_cmd_cfg->dst_y_start  = dst->y;
    		ge2d_cmd_cfg->dst_y_end    = dst->y+dst->h-1;

		ge2d_cmd_cfg->src1_x_start  = ge2d_cmd_cfg->dst_x_start;
    		ge2d_cmd_cfg->src1_x_end    = ge2d_cmd_cfg->dst_x_end ;
    		ge2d_cmd_cfg->src1_y_start= ge2d_cmd_cfg->dst_y_start ;
    		ge2d_cmd_cfg->src1_y_end= ge2d_cmd_cfg->dst_y_end;

    		ge2d_cmd_cfg->src1_fill_color_en = 1;
		break;
		case	CMD_BITBLT:
		ge2d_cmd_cfg->src1_x_start = src->x;
    		ge2d_cmd_cfg->src1_x_end   = src->x+src->w-1;
    		ge2d_cmd_cfg->src1_y_start = src->y;
    		ge2d_cmd_cfg->src1_y_end   = src->y+src->h-1;

    		ge2d_cmd_cfg->dst_x_start = dst->x;
    		ge2d_cmd_cfg->dst_x_end   = dst->x+src->w-1;
    		ge2d_cmd_cfg->dst_y_start = dst->y;
    		ge2d_cmd_cfg->dst_y_end   = dst->y+src->h-1;
		
		break;
		case	CMD_STRETCH_BLIT:
              ge2d_cmd_cfg->src1_x_start = src->x;
              ge2d_cmd_cfg->src1_x_end   = src->x+src->w-1;
              ge2d_cmd_cfg->src1_y_start = src->y;
              ge2d_cmd_cfg->src1_y_end   = src->y+src->h-1;
          
              ge2d_cmd_cfg->dst_x_start  = dst->x;
              ge2d_cmd_cfg->dst_x_end    = dst->x+dst->w-1;
              ge2d_cmd_cfg->dst_y_start  = dst->y;
              ge2d_cmd_cfg->dst_y_end    = dst->y+dst->h-1;
          
              ge2d_cmd_cfg->sc_hsc_en = 1;
              ge2d_cmd_cfg->sc_vsc_en = 1;
              ge2d_cmd_cfg->hsc_rpt_p0_num = 1;
              ge2d_cmd_cfg->vsc_rpt_l0_num = 1;
              ge2d_cmd_cfg->hsc_div_en = 1; 
                     

    		break;	
	}
	ge2d_cmd_cfg->color_blend_mode = OPERATION_LOGIC;
    	ge2d_cmd_cfg->color_logic_op   = LOGIC_OPERATION_COPY;  
    	ge2d_cmd_cfg->alpha_blend_mode = OPERATION_LOGIC;
    	ge2d_cmd_cfg->alpha_logic_op   = LOGIC_OPERATION_COPY; 
	ge2d_set_src1_data(&cfg->src1_data);
	ge2d_set_src1_gen(&cfg->src1_gen);
	ge2d_set_src2_dst_data(&cfg->src2_dst_data);	
	ge2d_set_src2_dst_gen(&cfg->src2_dst_gen);
	ge2d_set_dp_gen(&cfg->dp_gen);
	ge2d_set_src1_scale_coef(FILTER_TYPE_TRIANGLE, FILTER_TYPE_TRIANGLE);
		
	ge2d_set_cmd(ge2d_cmd_cfg);
	ge2d_wait_done();
	memset(ge2d_cmd_cfg,0,sizeof(ge2d_cmd_t));
	return 0;
}
	