#ifndef _DI_H
#define _DI_H
/*
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
#define DI_VERSION_OLD        0
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV
#define DI_VERSION_NEW1       1
#elif (MESON_CPU_TYPE > MESON_CPU_TYPE_MESON6TV) && (MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6TVD)
#define DI_VERSION_NEW2       2
#elif (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TVD) || (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)
#define DI_VERSION_NEW3       3
#endif
*/
#undef USE_LIST
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define NEW_KEEP_LAST_FRAME
#endif

#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
#ifndef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
#define CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
#endif
#define D2D3_SUPPORT
#define DET3D
//#define SUPPORT_MPEG_TO_VDIN
#endif
#define SUPPORT_MPEG_TO_VDIN //for all ic after m6c@20140731

#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)
#define NEW_DI_TV
#define NEW_DI_V1 //from m6tvc
#elif (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
#define NEW_DI_TV
#define NEW_DI_V1 //from m6tvc
#define NEW_DI_V2 //from m6tvd(noise meter bug fix,improvement for 2:2 pull down)
#elif (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8 || MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8B)
#define NEW_DI_V1 //from m6tvc
#define NEW_DI_V2 //from m6tvd(noise meter bug fix,improvement for 2:2 pull down)
#elif (MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8B)
#define NEW_DI_V1 //from m6tvc
#define NEW_DI_V2 //from m6tvd(noise meter bug fix,improvement for 2:2 pull down)
#endif

#ifndef CONFIG_VSYNC_RDMA
#ifndef VSYNC_WR_MPEG_REG
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define VSYNC_WR_MPEG_REG(adr,val) WRITE_VCBUS_REG(adr, val)
#define VSYNC_WR_MPEG_REG_BITS(adr, val, start, len)  WRITE_VCBUS_REG_BITS(adr, val, start, len)
#define VSYNC_RD_MPEG_REG(adr) READ_VCBUS_REG(adr)
#else
#define VSYNC_WR_MPEG_REG(adr,val) WRITE_MPEG_REG(adr, val)
#define VSYNC_WR_MPEG_REG_BITS(adr, val, start, len)  WRITE_MPEG_REG_BITS(adr, val, start, len)
#define VSYNC_RD_MPEG_REG(adr) READ_MPEG_REG(adr)
#endif
#endif
#endif


#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define Wr(adr, val) WRITE_VCBUS_REG(adr, val)
#define Rd(adr) READ_VCBUS_REG(adr)
//#define Wr_reg_bits(reg, val, start, len) WRITE_VCBUS_REG_BITS(adr, val, start, len)
#else
#define Wr(adr, val) WRITE_MPEG_REG(adr, val)
#define Rd(adr) READ_MPEG_REG(adr)
//#define Wr_reg_bits(reg, val, start, len) WRITE_MPEG_REG_BITS(adr, val, start, len)
#endif
#define Wr_reg_bits(reg, val, start, len) \
  Wr(reg, (Rd(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))


/************************************
*    di hardware level interface
*************************************/
#define WIN_SIZE_FACTOR     100

#define PD32_PAR_NUM 6
#define PD22_PAR_NUM 6
#define MAX_WIN_NUM             5

typedef struct{
    unsigned field_diff;        /* total pixels difference between current field and previous field */
    unsigned field_diff_num;    /* the number of pixels with big difference between current field and previous field */
    unsigned frame_diff;        /* total pixels difference between current field and previouse-previouse field */
    unsigned frame_diff_num;    /* the number of pixels with big difference between current field and previouse-previous field */
    /**/
    unsigned frame_diff_skew;      /* the difference between current frame_diff and previous frame_diff */
    unsigned frame_diff_num_skew;   /* the difference between current frame_diff_num and previous frame_diff_num */
    /* parameters for detection */
    unsigned field_diff_by_pre;
    unsigned field_diff_by_next;
    unsigned field_diff_num_by_pre;
    unsigned field_diff_num_by_next;
    unsigned frame_diff_by_pre;
    unsigned frame_diff_num_by_pre;
    unsigned frame_diff_skew_ratio;
    unsigned frame_diff_num_skew_ratio;
    /* matching pattern */
    unsigned field_diff_pattern;
    unsigned field_diff_num_pattern;
    unsigned frame_diff_pattern;
    unsigned frame_diff_num_pattern;
}pulldown_detect_info_t;

typedef struct{
        /*
            if frame_diff < threshold, cur_field and pre_pre_field is top/bot or bot/top;
            if field_diff < threshold, cur_field and pre_field is top/bot or bot/top;
         */
    unsigned frame_diff_chg_th;
    unsigned frame_diff_num_chg_th;
    unsigned field_diff_chg_th;
    unsigned field_diff_num_chg_th;
        /*
            if frame_diff_skew < threshold,  pre_field/cur_filed is top/bot
        */
    unsigned frame_diff_skew_th;
    unsigned frame_diff_num_skew_th;
        /*
        */
    unsigned field_diff_num_th;
}pd_detect_threshold_t;

typedef struct{
    uint win_start_x_r;
    uint win_end_x_r;
    uint win_start_y_r;
    uint win_end_y_r;
    uint win_32lvl;
    uint win_22lvl;
    uint pixels_num;
}pd_win_prop_t;

typedef enum {
    PROCESS_FUN_NULL = 0,
    PROCESS_FUN_DI,
    PROCESS_FUN_PD,
    PROCESS_FUN_PROG,
    PROCESS_FUN_BOB
}process_fun_index_t;

typedef enum {
    PULL_DONW_BLEND_0 = 0,//buf1=dup[0]
    PULL_DOWN_BLEND_2 = 1,//buf1=dup[2]
    PULL_DOWN_MTN     = 2,//mtn only
    PULL_DOWN_BUF1    = 3,//do wave with dup[0]
    PULL_DOWN_EI      = 4,//ei only
    PULL_DOWN_NORMAL  = 5,//normal di
}pulldown_mode_t;

typedef struct di_buf_s{
#ifdef D2D3_SUPPORT
    unsigned int dp_buf_adr;
    unsigned int dp_buf_size;
    unsigned int reverse_flag;
#endif
#ifdef USE_LIST
    struct list_head list;
#endif
    vframe_t* vframe;
    int index; /* index in vframe_in_dup[] or vframe_in[], only for type of VFRAME_TYPE_IN */
    int post_proc_flag; /* 0,no post di; 1, normal post di; 2, edge only; 3, dummy */
    int new_format_flag;
    int type;
    int throw_flag;
    int invert_top_bot_flag;
    int seq;
    int pre_ref_count; /* none zero, is used by mem_mif, chan2_mif, or wr_buf*/
    int post_ref_count; /* none zero, is used by post process */
    int queue_index;
    /*below for type of VFRAME_TYPE_LOCAL */
    unsigned int nr_adr;
    int nr_canvas_idx;
    unsigned int mtn_adr;
    int mtn_canvas_idx;
#ifdef NEW_DI_V1
    unsigned int cnt_adr;
    int cnt_canvas_idx;
#endif
    unsigned int canvas_config_flag; /* 0, configed; 1, config type 1 (prog); 2, config type 2 (interlace) */
    unsigned int canvas_config_size; /* bit [31~16] width; bit [15~0] height */
    /* pull down information */
    pulldown_detect_info_t field_pd_info;
    pulldown_detect_info_t win_pd_info[MAX_WIN_NUM];

    unsigned long mtn_info[5];
    pulldown_mode_t pulldown_mode;
    int win_pd_mode[5];
    process_fun_index_t process_fun_index;
    int early_process_fun_index;
    int left_right;/*1,left eye; 0,right eye in field alternative*/
    /*below for type of VFRAME_TYPE_POST*/
    struct di_buf_s* di_buf[2];
    struct di_buf_s* di_buf_dup_p[5]; /* 0~4: n-2, n-1, n, n+1, n+2 ; n is the field to display*/
    struct di_buf_s* di_wr_linked_buf;
}di_buf_t;

extern uint di_mtn_1_ctrl1;
extern uint ei_ctrl0;
extern uint ei_ctrl1;
extern uint ei_ctrl2;
#ifdef NEW_DI_V1
extern uint ei_ctrl3;
#endif
#ifdef DET3D
extern bool det3d_en;
#endif
extern uint nr_ctrl0;
extern uint nr_ctrl1;
extern uint nr_ctrl2;
extern uint nr_ctrl3;
extern uint mtn_ctrl;
extern uint mtn_ctrl_char_diff_cnt;
extern uint mtn_ctrl_low_level;
extern uint mtn_ctrl_high_level;
extern uint mtn_ctrl_diff_level;
extern uint mtn_ctrl1;
extern uint mtn_ctrl1_reduce;
extern uint mtn_ctrl1_shift;
extern uint blend_ctrl;
extern uint kdeint0;
extern uint kdeint1;
extern uint kdeint2;
extern uint reg_mtn_info0;
extern uint reg_mtn_info1;
extern uint reg_mtn_info2;
extern uint reg_mtn_info3;
extern uint reg_mtn_info4;
extern uint mtn_thre_1_low;
extern uint mtn_thre_1_high;
extern uint mtn_thre_2_low;
extern uint mtn_thre_2_high;

extern uint blend_ctrl1;
extern uint blend_ctrl1_char_level;
extern uint blend_ctrl1_angle_thd;
extern uint blend_ctrl1_filt_thd;
extern uint blend_ctrl1_diff_thd;
extern uint blend_ctrl2;
extern uint blend_ctrl2_black_level;
extern uint blend_ctrl2_mtn_no_mov;
extern uint post_ctrl__di_blend_en;
extern uint post_ctrl__di_post_repeat;
extern uint di_pre_ctrl__di_pre_repeat;

extern uint noise_reduction_level;

extern uint field_32lvl;
extern uint field_22lvl;
extern pd_detect_threshold_t field_pd_th;
extern pd_detect_threshold_t win_pd_th[MAX_WIN_NUM];
extern pd_win_prop_t pd_win_prop[MAX_WIN_NUM];

extern int  pd_enable;

extern void di_hw_init(void);

extern void di_hw_uninit(void);

extern int di_vscale_skip_count;
/*
di hardware internal
*/

#if defined(CONFIG_AM_DEINTERLACE_SD_ONLY)
#define MAX_CANVAS_WIDTH 				720
#define MAX_CANVAS_HEIGHT				576
#else
#define MAX_CANVAS_WIDTH 				1920
#define MAX_CANVAS_HEIGHT				1088
#endif

#define DI_BUF_NUM			6

typedef struct DI_MIF_TYPE
{
   unsigned short  	luma_x_start0;
   unsigned short  	luma_x_end0;
   unsigned short  	luma_y_start0;
   unsigned short  	luma_y_end0;
   unsigned short  	chroma_x_start0;
   unsigned short  	chroma_x_end0;
   unsigned short  	chroma_y_start0;
   unsigned short  	chroma_y_end0;
   unsigned        	set_separate_en 	: 2;   	// 1 : y cb cr seperated canvas. 0 : one canvas.
   unsigned        	src_field_mode  	: 1;   	// 1 frame . 0 field.
   unsigned        	video_mode      	: 1;   	// 1 : 4:4:4. 0 : 4:2:2
   unsigned        	output_field_num	: 1;   	// 0 top field  1 bottom field.
   unsigned        	burst_size_y    	: 2;
   unsigned        	burst_size_cb   	: 2;
   unsigned        	burst_size_cr   	: 2;
   unsigned        	canvas0_addr0 		: 8;
   unsigned        	canvas0_addr1 		: 8;
   unsigned        	canvas0_addr2 		: 8;
} DI_MIF_t;

typedef struct DI_SIM_MIF_TYPE
{
   unsigned short  	start_x;
   unsigned short  	end_x;
   unsigned short  	start_y;
   unsigned short  	end_y;
   unsigned short  	canvas_num;
} DI_SIM_MIF_t;

void disable_deinterlace(void);

void disable_pre_deinterlace(void);

void disable_post_deinterlace(void);

int get_di_pre_recycle_buf(void);


void disable_post_deinterlace_2(void);

void enable_di_mode_check_2 (
		int win0_start_x, int win0_end_x, int win0_start_y, int win0_end_y,
		int win1_start_x, int win1_end_x, int win1_start_y, int win1_end_y,
        int win2_start_x, int win2_end_x, int win2_start_y, int win2_end_y,
        int win3_start_x, int win3_end_x, int win3_start_y, int win3_end_y,
        int win4_start_x, int win4_end_x, int win4_start_y, int win4_end_y
	);

void enable_di_pre_aml (
   		DI_MIF_t        *di_inp_mif,
   		DI_MIF_t        *di_mem_mif,
   		DI_MIF_t        *di_chan2_mif,
   		DI_SIM_MIF_t    *di_nrwr_mif,
   		DI_SIM_MIF_t    *di_mtnwr_mif,
#ifdef NEW_DI_V1
   DI_SIM_MIF_t    *di_contp2rd_mif,
   DI_SIM_MIF_t    *di_contprd_mif,
   DI_SIM_MIF_t    *di_contwr_mif,
#endif
   		int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en, int hist_check_en,
   		int pre_field_num, int pre_viu_link, int hold_line, int urgent
   	);


void enable_region_blend (
        int reg0_en, int reg0_start_x, int reg0_end_x, int reg0_start_y, int reg0_end_y, int reg0_mode,
        int reg1_en, int reg1_start_x, int reg1_end_x, int reg1_start_y, int reg1_end_y, int reg1_mode,
        int reg2_en, int reg2_start_x, int reg2_end_x, int reg2_start_y, int reg2_end_y, int reg2_mode,
        int reg3_en, int reg3_start_x, int reg3_end_x, int reg3_start_y, int reg3_end_y, int reg3_mode
    );


void run_deinterlace(unsigned zoom_start_x_lines, unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines,
	unsigned type, int mode, int hold_line);

void deinterlace_init(void);

void initial_di_pre_aml ( int hsize_pre, int vsize_pre, int hold_line );

void initial_di_post_2 ( int hsize_post, int vsize_post, int hold_line ) ;

void enable_di_post_2 (
   DI_MIF_t        *di_buf0_mif,
   DI_MIF_t        *di_buf1_mif,
   DI_SIM_MIF_t    *di_diwr_mif,
   DI_SIM_MIF_t    *di_mtncrd_mif,
   DI_SIM_MIF_t    *di_mtnprd_mif,
   int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
   int post_field_num, int hold_line , int urgent,
   unsigned long * reg_mtn_info);

void di_post_switch_buffer (
   DI_MIF_t        *di_buf0_mif,
   DI_MIF_t        *di_buf1_mif,
   DI_SIM_MIF_t    *di_diwr_mif,
   DI_SIM_MIF_t    *di_mtncrd_mif,
   DI_SIM_MIF_t    *di_mtnprd_mif,
   int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
   int post_field_num, int hold_line, int urgent,
   unsigned long * reg_mtn_info );

void enable_di_post_pd(
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
    int post_field_num, int hold_line, int urgent);

void di_post_switch_buffer_pd(
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
    int post_field_num, int hold_line, int urgent);

void read_pulldown_info(pulldown_detect_info_t* field_pd_info,
                        pulldown_detect_info_t* win_pd_info);

void read_mtn_info(unsigned long* mtn_info, unsigned long* );

void reset_pulldown_state(void);

void cal_pd_parameters(pulldown_detect_info_t* cur_info, pulldown_detect_info_t* pre_info, pulldown_detect_info_t* next_info, pd_detect_threshold_t* pd_th);

void pattern_check_pre_2(int idx, pulldown_detect_info_t* cur_info, pulldown_detect_info_t* pre_info, pulldown_detect_info_t* pre2_info,
                    int* pre_pulldown_mode, int* pre2_pulldown_mode, int* type,
                    pd_detect_threshold_t* pd_th);

void reset_di_para(void);
/* for video reverse */
void di_post_read_reverse(bool reverse);
void di_post_read_reverse_irq(bool reverse);

/* new pd algorithm */
void reset_pd_his(void);
void insert_pd_his(pulldown_detect_info_t* pd_info);
void reset_pd32_status(void);
int detect_pd32(void);

extern unsigned int pd32_match_num;
extern unsigned int pd32_debug_th;
extern unsigned int pd32_diff_num_0_th;
extern unsigned int pd22_th;
extern unsigned int pd22_num_th;
extern int nr_hfilt_en;

/* init for nr */
void di_load_nr_setting(void);

#undef DI_DEBUG

#define DI_LOG_MTNINFO      0x02
#define DI_LOG_PULLDOWN     0x10
#define DI_LOG_BUFFER_STATE     0x20
#define DI_LOG_TIMESTAMP        0x100
#define DI_LOG_PRECISE_TIMESTAMP        0x200
#define DI_LOG_QUEUE        0x40
#define DI_LOG_VFRAME       0x80

extern unsigned int di_log_flag;
extern unsigned int di_debug_flag;

int di_print(const char *fmt, ...);


typedef struct{
    unsigned int adr;
    unsigned int val;
    unsigned short start;
    unsigned short len;
}reg_set_t;

#define REG_SET_MAX_NUM 128
#define FMT_MAX_NUM     32
typedef struct reg_cfg_{
    struct reg_cfg_* next;
    unsigned int source_types_enable; /* each bit corresponds to one source type */
    unsigned int pre_post_type; /* pre, 0; post, 1 */
	unsigned int dtv_defintion_type;/*high defintion,0; stand defintion ,1;common,2*/
    unsigned int sig_fmt_range[FMT_MAX_NUM]; /* {bit[31:16]~bit[15:0]}, include bit[31:16] and bit[15:0]  */
    reg_set_t reg_set[REG_SET_MAX_NUM];
}reg_cfg_t;

int get_current_vscale_skip_count(vframe_t* vf);

void di_set_power_control(unsigned char type, unsigned char enable);

unsigned char di_get_power_control(unsigned char type);

#endif
