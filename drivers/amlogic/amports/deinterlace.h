#ifndef __DEINTERLACE__
#define __DEINTERLACE__

#if defined(CONFIG_AM_DEINTERLACE_SD_ONLY)
#define MAX_CANVAS_WIDTH                720
#define MAX_CANVAS_HEIGHT               576
#else
#define MAX_CANVAS_WIDTH                1920
#define MAX_CANVAS_HEIGHT               1088
#endif

#define DI_BUF_NUM          6

typedef struct DI_MIF_TYPE {
    unsigned short   luma_x_start0;
    unsigned short   luma_x_end0;
    unsigned short   luma_y_start0;
    unsigned short   luma_y_end0;
    unsigned short   chroma_x_start0;
    unsigned short   chroma_x_end0;
    unsigned short   chroma_y_start0;
    unsigned short   chroma_y_end0;
    unsigned         set_separate_en     : 1;    // 1 : y cb cr seperated canvas. 0 : one canvas.
    unsigned         src_field_mode      : 1;    // 1 frame . 0 field.
    unsigned         video_mode          : 1;    // 1 : 4:4:4. 0 : 4:2:2
    unsigned         output_field_num    : 1;    // 0 top field  1 bottom field.
    unsigned         burst_size_y        : 2;
    unsigned         burst_size_cb       : 2;
    unsigned         burst_size_cr       : 2;
    unsigned         canvas0_addr0       : 8;
    unsigned         canvas0_addr1       : 8;
    unsigned         canvas0_addr2       : 8;
} DI_MIF_t;

typedef struct DI_SIM_MIF_TYPE {
    unsigned short   start_x;
    unsigned short   end_x;
    unsigned short   start_y;
    unsigned short   end_y;
    unsigned short   canvas_num;
} DI_SIM_MIF_t;

void disable_deinterlace(void);

void disable_pre_deinterlace(void);

void disable_post_deinterlace(void);

int get_deinterlace_mode(void);

void set_deinterlace_mode(int mode);

#if defined(CONFIG_ARCH_MESON2)
int get_noise_reduction_level(void);
void set_noise_reduction_level(int level);
#endif

int get_di_pre_recycle_buf(void);

const vframe_provider_t * get_vfp(void);

vframe_t *peek_di_out_buf(void);

void inc_field_counter(void);

void set_post_di_mem(int mode);

void initial_di_prepost(int hsize_pre, int vsize_pre, int hsize_post, int vsize_post, int hold_line);

void initial_di_pre(int hsize_pre, int vsize_pre, int hold_line);

void initial_di_post(int hsize_post, int vsize_post, int hold_line);

void enable_di_mode_check(
    int win0_start_x, int win0_end_x, int win0_start_y, int win0_end_y,
    int win1_start_x, int win1_end_x, int win1_start_y, int win1_end_y,
    int win2_start_x, int win2_end_x, int win2_start_y, int win2_end_y,
    int win3_start_x, int win3_end_x, int win3_start_y, int win3_end_y,
    int win4_start_x, int win4_end_x, int win4_start_y, int win4_end_y,
    int win0_32lvl,   int win1_32lvl, int win2_32lvl, int win3_32lvl, int win4_32lvl,
    int win0_22lvl,   int win1_22lvl, int win2_22lvl, int win3_22lvl, int win4_22lvl,
    int field_32lvl,  int field_22lvl
);

void enable_di_prepost_full(
    DI_MIF_t        *di_inp_mif,
    DI_MIF_t        *di_mem_mif,
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_MIF_t        *di_chan2_mif,
    DI_SIM_MIF_t    *di_nrwr_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtnwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en, int hist_check_en,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, int nr_hfilt_mb_en, int mtn_modify_en,
    int blend_mtn_filt_en, int blend_data_filt_en, int post_mb_en,
#endif
    int post_field_num, int pre_field_num, int prepost_link, int hold_line
);

void set_di_inp_fmt_more(
    int hfmt_en,
    int hz_yc_ratio,        //2bit
    int hz_ini_phase,       //4bit
    int vfmt_en,
    int vt_yc_ratio,        //2bit
    int vt_ini_phase,       //4bit
    int y_length,
    int c_length,
    int hz_rpt              //1bit
);

void set_di_inp_mif(DI_MIF_t  * mif, int urgent, int hold_line);

void set_di_mem_fmt_more(
    int hfmt_en,
    int hz_yc_ratio,        //2bit
    int hz_ini_phase,       //4bit
    int vfmt_en,
    int vt_yc_ratio,        //2bit
    int vt_ini_phase,       //4bit
    int y_length,
    int c_length,
    int hz_rpt              //1bit
);

void set_di_mem_mif(DI_MIF_t * mif, int urgent, int hold_line);

void set_di_if1_fmt_more(
    int hfmt_en,
    int hz_yc_ratio,        //2bit
    int hz_ini_phase,       //4bit
    int vfmt_en,
    int vt_yc_ratio,        //2bit
    int vt_ini_phase,       //4bit
    int y_length,
    int c_length,
    int hz_rpt              //1bit
);

void set_di_if1_mif(DI_MIF_t * mif, int urgent, int hold_line);

void set_di_chan2_mif(DI_MIF_t *mif, int urgent, int hold_line);

void set_di_if0_mif(DI_MIF_t *mif, int urgent, int hold_line);

void enable_di_pre(
    DI_MIF_t        *di_inp_mif,
    DI_MIF_t        *di_mem_mif,
    DI_MIF_t        *di_chan2_mif,
    DI_SIM_MIF_t    *di_nrwr_mif,
    DI_SIM_MIF_t    *di_mtnwr_mif,
    int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en, int hist_check_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, int nr_hfilt_mb_en, int mtn_modify_en,
#endif
    int pre_field_num, int pre_viu_link, int hold_line
);

void enable_di_post(
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int blend_mtn_filt_en, int blend_data_filt_en, int post_mb_en,
#endif
    int post_field_num, int hold_line
);

void enable_region_blend(
    int reg0_en, int reg0_start_x, int reg0_end_x, int reg0_start_y, int reg0_end_y, int reg0_mode,
    int reg1_en, int reg1_start_x, int reg1_end_x, int reg1_start_y, int reg1_end_y, int reg1_mode,
    int reg2_en, int reg2_start_x, int reg2_end_x, int reg2_start_y, int reg2_end_y, int reg2_mode,
    int reg3_en, int reg3_start_x, int reg3_end_x, int reg3_start_y, int reg3_end_y, int reg3_mode
);

void set_vdin_par(int flag, vframe_t *buf);

void di_pre_process(void);

void run_deinterlace(unsigned zoom_start_x_lines, unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines,
                     unsigned type, int mode, int hold_line);

void deinterlace_init(void);

#endif

