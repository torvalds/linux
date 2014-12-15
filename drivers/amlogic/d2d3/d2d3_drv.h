/*******************************************************************
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *  File name: d2d3_drv.h
 *  Description: IO function, structure, enum, used in d2d3 sub-module processing
 *******************************************************************/

#ifndef D2D3_H_
#define D2D3_H_

#define D2D3_FLAG_NULL                          0x00000000
#define D2D3_BYPASS                             0x00000001
#define D2D3_DEV_INIT                           0x00000002
#define D2D3_REG				0x00000004


#define D2D3_CANVAS_MAX_WIDTH                   256
#define D2D3_CANVAS_MAX_HEIGH                   1080

#define D2D3_CANVAS_MAX_CNT                     2

#define D2D3_HORIZONTAL_PIXLE_MAX               256

#define D2D3_SCU18_MIN_SCALE_FACTOR             0
#define D2D3_SCU18_MAX_SCALE_FACTOR             3


#define D2d3_Wr_reg_bits(reg,val,start,len)          WRITE_CBUS_REG_BITS(reg,val,start,len)
#define D2d3_Wr(reg,val)                             WRITE_CBUS_REG(reg,val)
#define D2d3_Rd(reg)                                 READ_CBUS_REG(reg)

typedef enum d2d3_path_e{
        NRW_VPP = 0,    //dpg= 0,dbr=1
        NRW_VDR,        //dpg=0,dbr=0
        VDR_VDR,        //dpg=1,dbr=0
        VDR_VPP,        //dpg=1,dbr=1
        VPP_VPP,        //dpg=2,dbr=1
}d2d3_path_t;

typedef enum d2d3_dbr_mode_e{
        FIELD_INTERLEAVED_LEFT_RIGHT = 0,
        FIELD_INTERLEAVED_RIGHT_LEFT,
        LINE_INTERLEAVED,
        HALF_LINE_INTERLEAVED,
        PIXEL_INTERLEAVED,
        HALF_LINE_INTERLEAVED_DOUBLE_SIZE,
}d2d3_dbr_mode_t;
#define D2D3_DPG_MUX_NULL               0
#define D2D3_DPG_MUX_VDIN0              1
#define D2D3_DPG_MUX_VDIN1              2
#define D2D3_DPG_MUX_NRW                3
#define D2D3_DPG_MUX_VDR                4
#define D2D3_DPG_MUX_VPP                5

#define D2D3_DBR_MUX_NULL               0
#define D2D3_DBR_MUX_VDR                1
#define D2D3_DBR_MUX_VPP                2

typedef struct d2d3_scu18_param_s{
        int dbr_scu18_step_en;
        int dbr_scu18_isize_x;
        int dbr_scu18_isize_y;
        int dbr_scu18_iniph_h;
        int dbr_scu18_iniph_v;
}d2d3_scu18_param_t;

typedef struct d2d3_param_s{
        unsigned int        input_w;
        unsigned int        input_h;
        unsigned int        output_w;
        unsigned int        output_h;
        unsigned int        reverse_flag;
        	 short      depth;
        unsigned short      dpg_path;
        unsigned short      dbr_path;
        enum d2d3_dbr_mode_e dbr_mode;
        //unsigned short    dbr_mode;
}d2d3_param_t;

typedef struct d2d3_depth_s{
        unsigned int        dpg_size_x;
        unsigned int        dpg_size_y;
        unsigned short      dpg_scale_x;
        unsigned short      dpg_scale_y;
        unsigned int        depthsize_x;
        unsigned int        depthsize_y;
        unsigned int        dbr_size_x;
        unsigned int        dbr_size_y;
        unsigned short      dbr_scale_x;
        unsigned short      dbr_scale_y;
}d2d3_depth_t;

/* d2d3 device structure */
typedef struct d2d3_dev_s {
        unsigned int        flag;
        int                 index;
        dev_t               devt;
        struct cdev         cdev;
        struct device       *dev;
        char                vfm_name[12];
        unsigned int        irq;
        //spinlock_t	    buf_lock;
        /* d2d3 memory */
        unsigned int        mem_start;
        unsigned int        mem_size;
        unsigned int        canvas_h;
        unsigned int        canvas_w;
        unsigned int        canvas_max_size;
        unsigned int        canvas_max_num;
        unsigned int        dpg_canvas_idx;
        unsigned int        dbr_canvas_idx;
        /*buffer management*/
        unsigned int        dpg_addr;
        unsigned int        dbr_addr;
        /*d2d3 parameters*/
        struct d2d3_param_s param;
} d2d3_dev_t;

extern void get_real_display_size(unsigned int *w,unsigned int *h);
extern void d2d3_config_dpg_canvas(struct d2d3_dev_s *devp);
extern void d2d3_config_dbr_canvas(struct d2d3_dev_s *devp);
extern void d2d3_canvas_reverse(bool reverse);
extern void d2d3_config(d2d3_dev_t *devp,d2d3_param_t *parm);
extern void d2d3_enable_hw(bool enable);
extern void d2d3_enable_path(bool enable, d2d3_param_t *parm);
extern void d2d3_set_def_config(d2d3_param_t *parm);
extern void d2d3_update_canvas(struct d2d3_dev_s *devp);
extern void d2d3_canvas_init(struct d2d3_dev_s *devp);
extern int d2d3_depth_adjust(short depth);
#endif /* D2D3_H_ */

