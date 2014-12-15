#include <linux/amlogic/ge2d/ge2d.h>

static inline void _stretchblt(ge2d_context_t *wq,
                int src_x, int src_y, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h, int block)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);
            
    ge2d_cmd_cfg->src1_x_start = src_x;
    ge2d_cmd_cfg->src1_x_end   = src_x+src_w-1;
    ge2d_cmd_cfg->src1_y_start = src_y;
    ge2d_cmd_cfg->src1_y_end   = src_y+src_h-1;

    ge2d_cmd_cfg->dst_x_start  = dst_x;
    ge2d_cmd_cfg->dst_x_end    = dst_x+dst_w-1;
    ge2d_cmd_cfg->dst_y_start  = dst_y;
    ge2d_cmd_cfg->dst_y_end    = dst_y+dst_h-1;

    ge2d_cmd_cfg->sc_hsc_en = 1;
    ge2d_cmd_cfg->sc_vsc_en = 1;
    ge2d_cmd_cfg->hsc_rpt_p0_num = 1;
    ge2d_cmd_cfg->vsc_rpt_l0_num = 1;
    ge2d_cmd_cfg->hsc_div_en = 1; 
    
    ge2d_cmd_cfg->color_blend_mode = OPERATION_LOGIC;
    ge2d_cmd_cfg->color_logic_op   = LOGIC_OPERATION_COPY;  
    ge2d_cmd_cfg->alpha_blend_mode = OPERATION_LOGIC;
    ge2d_cmd_cfg->alpha_logic_op   = LOGIC_OPERATION_COPY; 
    ge2d_cmd_cfg->wait_done_flag   = block;

    ge2d_wq_add_work(wq);
}

void stretchblt(ge2d_context_t *wq,
                int src_x, int src_y, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h)
{
    _stretchblt(wq,
                src_x, src_y, src_w, src_h,
                dst_x, dst_y, dst_w, dst_h, 1);
}
EXPORT_SYMBOL(stretchblt);
void stretchblt_noblk(ge2d_context_t *wq,
                int src_x, int src_y, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h)
{
    _stretchblt(wq,
                src_x, src_y, src_w, src_h,
                dst_x, dst_y, dst_w, dst_h, 0);
}

static inline void _stretchblt_noalpha(ge2d_context_t *wq,
                        int src_x, int src_y, int src_w, int src_h,
                        int dst_x, int dst_y, int dst_w, int dst_h, int blk)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);
    ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);

    if( dp_gen_cfg->alu_const_color != 0xff)	
    {
    	dp_gen_cfg->alu_const_color = 0xff;
	wq->config.update_flag |= UPDATE_DP_GEN;		
    }

    
            
    ge2d_cmd_cfg->src1_x_start = src_x;
    ge2d_cmd_cfg->src1_x_end   = src_x+src_w-1;
    ge2d_cmd_cfg->src1_y_start = src_y;
    ge2d_cmd_cfg->src1_y_end   = src_y+src_h-1;

    ge2d_cmd_cfg->dst_x_start  = dst_x;
    ge2d_cmd_cfg->dst_x_end    = dst_x+dst_w-1;
    ge2d_cmd_cfg->dst_y_start  = dst_y;
    ge2d_cmd_cfg->dst_y_end    = dst_y+dst_h-1;

    ge2d_cmd_cfg->sc_hsc_en = 1;
    ge2d_cmd_cfg->sc_vsc_en = 1;
    ge2d_cmd_cfg->hsc_rpt_p0_num = 1;
    ge2d_cmd_cfg->vsc_rpt_l0_num = 1;
    ge2d_cmd_cfg->hsc_div_en = 1; 
    
    ge2d_cmd_cfg->color_blend_mode = OPERATION_LOGIC;
    ge2d_cmd_cfg->color_logic_op   = LOGIC_OPERATION_COPY;  
    ge2d_cmd_cfg->alpha_blend_mode = OPERATION_LOGIC;
    ge2d_cmd_cfg->alpha_logic_op   = LOGIC_OPERATION_SET; 
    ge2d_cmd_cfg->wait_done_flag   = 1;

    ge2d_wq_add_work(wq);
}

void stretchblt_noalpha(ge2d_context_t *wq,
                        int src_x, int src_y, int src_w, int src_h,
                        int dst_x, int dst_y, int dst_w, int dst_h)
{
    _stretchblt_noalpha(wq,
                        src_x, src_y, src_w, src_h,
                        dst_x, dst_y, dst_w, dst_h, 1);
}

void stretchblt_noalpha_noblk(ge2d_context_t *wq,
                        int src_x, int src_y, int src_w, int src_h,
                        int dst_x, int dst_y, int dst_w, int dst_h)
{
    _stretchblt_noalpha(wq,
                        src_x, src_y, src_w, src_h,
                        dst_x, dst_y, dst_w, dst_h, 0);
}
