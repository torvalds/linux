#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <mach/am_regs.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
//#include <linux/iw7023.h>
#include "deinterlace.h"
#ifdef DET3D
#include "detect3d.h"
#endif

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif

#ifndef DI_CHAN2_CANVAS
#define DI_CHAN2_CANVAS DI_CHAN2_CANVAS0
#endif
#ifndef DI_CHAN2_LUMA_X
#define DI_CHAN2_LUMA_X DI_CHAN2_LUMA_X0
#endif
#ifndef DI_CHAN2_LUMA_Y
#define DI_CHAN2_LUMA_Y  DI_CHAN2_LUMA_Y0
#endif
#ifndef DI_CHAN2_LUMA_RPT_PAT
#define DI_CHAN2_LUMA_RPT_PAT DI_CHAN2_LUMA0_RPT_PAT
#endif

uint di_mtn_1_ctrl1;
uint ei_ctrl0;
uint ei_ctrl1;
uint ei_ctrl2;
#ifdef NEW_DI_V1
uint ei_ctrl3;
#endif
uint nr_ctrl0;
uint nr_ctrl1;
uint nr_ctrl2;
uint nr_ctrl3;
uint mtn_ctrl;
uint mtn_ctrl_char_diff_cnt;
uint mtn_ctrl_low_level;
uint mtn_ctrl_high_level;
uint mtn_ctrl_diff_level;
uint mtn_ctrl1;
uint mtn_ctrl1_reduce;
uint mtn_ctrl1_shift;
uint blend_ctrl;
uint kdeint0;
uint kdeint1;
uint kdeint2;
uint mtn_thre_1_high;
uint mtn_thre_1_low;
uint mtn_thre_2_high;
uint mtn_thre_2_low;

uint blend_ctrl1;
uint blend_ctrl1_char_level;
uint blend_ctrl1_angle_thd;
uint blend_ctrl1_filt_thd;
uint blend_ctrl1_diff_thd;
uint blend_ctrl2;
uint blend_ctrl2_black_level;
uint blend_ctrl2_mtn_no_mov;

uint post_ctrl__di_blend_en;
uint post_ctrl__di_post_repeat;
uint di_pre_ctrl__di_pre_repeat;

uint noise_reduction_level = 2;
static bool cue_enable = 1;

uint field_32lvl;
uint field_22lvl;
pd_detect_threshold_t field_pd_th;
pd_detect_threshold_t win_pd_th[MAX_WIN_NUM];
pd_win_prop_t pd_win_prop[MAX_WIN_NUM];
extern int mpeg2vdin_flag;

static bool frame_dynamic = 0;
MODULE_PARM_DESC(frame_dynamic, "\n frame_dynamic \n");
module_param(frame_dynamic, bool, 0664);

static bool frame_dynamic_dbg = 0;
MODULE_PARM_DESC(frame_dynamic_dbg, "\n frame_dynamic_dbg \n");
module_param(frame_dynamic_dbg, bool, 0664);

static int frame_dynamic_level = 0;
MODULE_PARM_DESC(frame_dynamic_level, "\n frame_dynamic_level \n");
module_param(frame_dynamic_level, int, 0664);

MODULE_PARM_DESC(cue_enable, "\n cue_enable\n");
module_param(cue_enable, bool, 0664);

#ifdef DET3D
static unsigned int det3d_cfg = 0;
#endif
static void init_pd_para(void)
{
    int i;
    pd_detect_threshold_t field_pd_th_tmp =
        {.frame_diff_chg_th = 2,
         .frame_diff_num_chg_th = 50,
         .field_diff_chg_th = 2,
         .field_diff_num_chg_th = 2,
         .frame_diff_skew_th = 5,  /*10*/
         .frame_diff_num_skew_th = 5,  /*10*/
         .field_diff_num_th = 0
        };
    /* win, only check diff_num */
    pd_detect_threshold_t win_pd_th_tmp =
        {.frame_diff_chg_th = 0,
         .frame_diff_num_chg_th = 50,
         .field_diff_chg_th = 0,
         .field_diff_num_chg_th = 2,
         .frame_diff_skew_th = 0,
         .frame_diff_num_skew_th = 0,
         .field_diff_num_th = 5
        };
    /**/
    field_32lvl = 16;
    field_22lvl = 256;

    field_pd_th = field_pd_th_tmp;

    pd_win_prop[0].win_start_x_r = 0; pd_win_prop[0].win_end_x_r = 100; pd_win_prop[0].win_start_y_r = 0;  pd_win_prop[0].win_end_y_r = 15;
    pd_win_prop[1].win_start_x_r = 0; pd_win_prop[1].win_end_x_r = 100; pd_win_prop[1].win_start_y_r = 15; pd_win_prop[1].win_end_y_r = 40;
    pd_win_prop[2].win_start_x_r = 0; pd_win_prop[2].win_end_x_r = 100; pd_win_prop[2].win_start_y_r = 40; pd_win_prop[2].win_end_y_r = 60;
    pd_win_prop[3].win_start_x_r = 0; pd_win_prop[3].win_end_x_r = 100; pd_win_prop[3].win_start_y_r = 60; pd_win_prop[3].win_end_y_r = 85;
    pd_win_prop[4].win_start_x_r = 0; pd_win_prop[4].win_end_x_r = 100; pd_win_prop[4].win_start_y_r = 85; pd_win_prop[4].win_end_y_r = 100;
    for(i=0; i<MAX_WIN_NUM; i++){
        pd_win_prop[i].win_32lvl = 0x10 /*16*/;
        pd_win_prop[i].win_22lvl = 0x300;
        win_pd_th[i] = win_pd_th_tmp;
	if(i==4)
	{
	win_pd_th[i].field_diff_num_th= win_pd_th_tmp.field_diff_num_th <<1;
	}
    }

}

void reset_di_para(void)
{
  	int nr_zone_0 = 4, nr_zone_1 = 8, nr_zone_2 = 12;
    //int nr_hfilt_en = 0;
    int nr_hfilt_mb_en = 0;
    //int mtn_modify_en = 1;
    //int post_mb_en = 0;
    //int blend_mtn_filt_en = 1;
    //int blend_data_filt_en = 1;
    unsigned int nr_strength = 0, nr_gain2 = 0, nr_gain1 = 0, nr_gain0 = 0;

    nr_strength = noise_reduction_level;
    if (nr_strength > 64)
        nr_strength = 64;
    nr_gain2 = 64 - nr_strength;
    nr_gain1 = nr_gain2 - ((nr_gain2 * nr_strength + 32) >> 6);
    nr_gain0 = nr_gain1 - ((nr_gain1 * nr_strength + 32) >> 6);
    nr_ctrl1 = (64 << 24) | (nr_gain2 << 16) | (nr_gain1 << 8) | (nr_gain0 << 0);

#if 1          //if input is pal and ntsc
    ei_ctrl0 =  (255 << 16) |     		// ei_filter.
                  (1 << 8) |        				// ei_threshold.
                  (0 << 2) |         				// ei bypass cf2.
                  (0 << 1);        				// ei bypass far1

    ei_ctrl1 =   (90 << 24) |      		// ei diff
                  (10 << 16) |       				// ei ang45
                  (15 << 8 ) |        				// ei peak.
                   45;             				// ei cross.

    ei_ctrl2 =    (10 << 23) |       		// close2
                  (10 << 16) |       				// close1
                  (10 << 8 ) |       				// far2
                   93;             				// far1
#ifdef NEW_DI_V1
        ei_ctrl3 = 0x80000013;
        di_mtn_1_ctrl1 = 0xa0202015;
#endif
#else       //input is tuner
    ei_ctrl0 =  (255 << 16) |     		// ei_filter.
                  (1 << 8) |        				// ei_threshold.
                  (0 << 2) |         				// ei bypass cf2.
                  (0 << 1);        				// ei bypass far1

    ei_ctrl1 =   ( 90 << 24) |      		// ei diff
                  (10 << 16) |       				// ei ang45
                  (15 << 8 ) |        				// ei peak.
                   128;             				// ei cross.

    ei_ctrl2 =    (10 << 23) |       		// close2
                  (255 << 16) |       				// close1
                  (10 << 8 ) |       				// far2
                   255;             				// far1
#endif
       nr_ctrl0 =     (1 << 31 ) |          									// nr yuv enable.
                       	(1 << 30 ) |          												// nr range. 3 point
                       	(0 << 29 ) |          												// max of 3 point.
                       	(nr_hfilt_en << 28 ) |          									// nr hfilter enable.
                       	(nr_hfilt_mb_en << 27 ) |          									// nr hfilter motion_blur enable.
#ifdef NEW_DI_V1
                                (1 << 25)|//enable nr 2
#endif
                                (nr_zone_2 <<16 ) |   												// zone 2
                       	(nr_zone_1 << 8 ) |    												// zone 1
                       	(nr_zone_0 << 0 ) ;   												// zone 0

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
     	nr_ctrl2 =      ( 8 << 24 ) |         									//intra noise level
                     	( 1 << 16 )  |         												// intra no noise level.
                     	( 10 << 8 ) |          												// inter noise level.
                     	( 1 << 0 ) ;          												// inter no noise level.
#else
     	nr_ctrl2 =      ( 10 << 24 ) |         									//intra noise level
                     	( 1 << 16 )  |         												// intra no noise level.
                     	( 10 << 8 ) |          												// inter noise level.
                     	( 1 << 0 ) ;          												// inter no noise level.
#endif
     	nr_ctrl3 =      ( 16 << 16 ) |         									// if any one of 3 point  mtn larger than 16 don't use 3 point.
                       	720 ;               												// if one line eq cnt is larger than this number, this line is not conunted.


	    mtn_ctrl_char_diff_cnt = 2;
		mtn_ctrl_diff_level = 40;
		mtn_ctrl_high_level = 196;
		mtn_ctrl_low_level = 64;

    /*   	mtn_ctrl =      (1 << 31) |        											// lpf enable.
                         (1 << 30) |        													// mtn uv enable.
                         (mtn_modify_en<< 29) |        										// no mtn modify.
                         (mtn_ctrl_char_diff_cnt<< 24) |        													// char diff count.
                         (mtn_ctrl_diff_level<<16) |        													// black level.
                         (mtn_ctrl_high_level<<8) |         													// white level.
                         (mtn_ctrl_low_level<< 0) ;      							// char diff level.
    */
        mtn_ctrl= 0xe228c440;
        mtn_ctrl1_reduce = 2;
		mtn_ctrl1_shift  = 0;

       	mtn_ctrl1 =       (mtn_ctrl1_reduce<< 8) |        										// mtn shift if mtn modifty_en
                           mtn_ctrl1_shift;              														// mtn reduce before shift.

	   kdeint0 = 25;
	   kdeint1 = 25;
	   kdeint2 = 25;
      // blend_ctrl =     ( post_mb_en << 28 ) |      											// post motion blur enable.
      //                        ( 0 << 27 ) |               													// mtn3p(l, c, r) max.
      //                        ( 0 << 26 ) |               													// mtn3p(l, c, r) min.
      //                        ( 0 << 25 ) |               													// mtn3p(l, c, r) ave.
      //                        ( 1 << 24 ) |               													// mtntopbot max
      //                        ( blend_mtn_filt_en  << 23 ) | 												// blend mtn filter enable.
      //                        ( blend_data_filt_en << 22 ) | 												// blend data filter enable.
      //                        (kdeint0);                              												// kdeint.
        blend_ctrl=0x01f00019;
        blend_ctrl1_char_level = 196;
		blend_ctrl1_angle_thd = 64;
		blend_ctrl1_filt_thd = 40;
		blend_ctrl1_diff_thd = 64;
		blend_ctrl1 =            (blend_ctrl1_char_level<< 24 ) |          												// char level
                         	  ( blend_ctrl1_angle_thd << 16 ) |          														// angle thredhold.
                         	  ( blend_ctrl1_filt_thd<< 8 )  |          														// all_af filt thd.
                         	  ( blend_ctrl1_diff_thd) ;           														// all 4 equal
		blend_ctrl2_black_level = 4;
		blend_ctrl2_mtn_no_mov = 48;
		blend_ctrl2 =            (blend_ctrl2_black_level<< 8 ) |           												// mtn no mov level.
                          	  (blend_ctrl2_mtn_no_mov)    ;             														//black level.

    post_ctrl__di_blend_en=0xff;
    post_ctrl__di_post_repeat=0xff;
    di_pre_ctrl__di_pre_repeat=0xff;

    init_pd_para();
}

static int vdin_en = 0;

static void set_di_inp_fmt_more (
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

static void set_di_inp_mif ( DI_MIF_t  * mif, int urgent, int hold_line);

static void set_di_mem_fmt_more (
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

static void set_di_mem_mif ( DI_MIF_t * mif, int urgent, int hold_line );

static void set_di_if1_fmt_more (
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

static void set_di_if1_mif ( DI_MIF_t * mif, int urgent, int hold_line );

static void set_di_chan2_mif ( DI_MIF_t *mif, int urgent, int hold_line );

static void set_di_if0_mif ( DI_MIF_t *mif, int urgent, int hold_line );


void di_hw_init(void)
{
#ifdef NEW_DI_V1
    Wr(DI_MTN_1_CTRL1, Rd(DI_MTN_1_CTRL1)&(~(1<<31))); //enable old DI mode for m6tv
    Wr(DI_CLKG_CTRL, Rd(DI_CLKG_CTRL)); //di clock gate

    /* fifo size setting from 0x1be60 to 0x1bf20 */
    Wr(VD1_IF0_LUMA_FIFO_SIZE, 0x1bf20);  // 1a63 is vd1_if0_luma_fifo_size
    Wr(VD2_IF0_LUMA_FIFO_SIZE, 0x1bf20);  // 1a83 is vd2_if0_luma_fifo_size
    Wr(DI_INP_LUMA_FIFO_SIZE, 0x1bf20);   // 17d8 is DI_INP_luma_fifo_size
    Wr(DI_MEM_LUMA_FIFO_SIZE, 0x1bf20);   // 17e5 is DI_MEM_luma_fifo_size
    Wr(DI_IF1_LUMA_FIFO_SIZE, 0x1bf20);   // 17f2 is  DI_IF1_luma_fifo_size
    Wr(DI_CHAN2_LUMA_FIFO_SIZE, 0x1bf20); // 17b3 is DI_chan2_luma_fifo_size
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    Wr(DI_PRE_HOLD, (0 << 31) | (31 << 16) | 31);
#else
    Wr(DI_PRE_HOLD, (1 << 31) | (31 << 16) | 31);
#endif
#if defined(CONFIG_ARCH_MESON)
    Wr(DI_NRMTN_CTRL0, 0xb00a0603);
#endif


#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        //need not set DI_CLKG_CTRL, hardware default value of this register is already 0
    //Wr_reg_bits(DI_CLKG_CTRL, 0x0, 0, 2);    // bit 0: 1, no clock; bit 1: 0, auto clock gate
#endif
}

void di_hw_uninit(void)
{
}

unsigned int nr2_en = 0x1;
module_param(nr2_en,uint,0644);
MODULE_PARM_DESC(nr2_en,"\n nr2_en\n");



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
   int pre_field_num, int pre_viu_link, int hold_line, int urgent)
{
    int hist_check_only = 0;
#ifdef NEW_DI_V1
    int nr_w = 0,nr_h = 0;
#endif
    pd32_check_en = 1; // for progressive luma detection

  	hist_check_only = hist_check_en && !nr_en && !mtn_en && !pd22_check_en && !pd32_check_en ;

  	if ( nr_en | mtn_en | pd22_check_en || pd32_check_en )
  	{
       	set_di_mem_mif(di_mem_mif, urgent, hold_line );   		// set urgent 0
       	if ( !vdin_en )
       	set_di_inp_mif( di_inp_mif, urgent, hold_line );   		// set urgent 0
  	}

  	if ( pd22_check_en || hist_check_only )
  	{
       	set_di_chan2_mif(di_chan2_mif, urgent, hold_line);   	// set urgent 0.
       	#ifdef NEW_DI_V1
            Wr(DI_NR_CTRL0, nr_ctrl0 | (cue_enable << 26));
	#else
     	    Wr(DI_NR_CTRL0, nr_ctrl0);
	#endif
  	}else{
            Wr(DI_NR_CTRL0, nr_ctrl0 | (0 << 26));
  	}

  	// set nr wr mif interface.
   	if ( nr_en )
   	{
     	Wr(DI_NRWR_X, (di_nrwr_mif->start_x <<16) | (di_nrwr_mif->end_x));   	// start_x 0 end_x 719.
     	Wr(DI_NRWR_Y, (di_nrwr_mif->start_y <<16) | (di_nrwr_mif->end_y));   	// start_y 0 end_y 239.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
     	Wr(DI_NRWR_CTRL, di_nrwr_mif->canvas_num |                                  // canvas index
                       (urgent<<16));     						        // urgent bit 16
#else
     	Wr(DI_NRWR_CTRL, di_nrwr_mif->canvas_num );     						// canvas index.
     	                                                                    // urgent bit 8
#endif
#if !defined(CONFIG_ARCH_MESON)
     	Wr(DI_NR_CTRL1, nr_ctrl1);
     	Wr(DI_NR_CTRL2, nr_ctrl2);
     	Wr(DI_NR_CTRL3, nr_ctrl3);
#endif
   	}

   	// motion wr mif.
    if (mtn_en )
    {
#ifdef NEW_DI_V1
        Wr(DI_CONTWR_X,    (di_contwr_mif->start_x <<16) | (di_contwr_mif->end_x));   // start_x 0 end_x 719.
        Wr(DI_CONTWR_Y,    (di_contwr_mif->start_y <<16) | (di_contwr_mif->end_y));   // start_y 0 end_y 239.
        Wr(DI_CONTWR_CTRL,  di_contwr_mif->canvas_num |  // canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                          (urgent << 8));       // urgent.
#else
                          (0 << 8));       // urgent.
#endif
        Wr(DI_CONTPRD_X,   (di_contprd_mif->start_x <<16)  | (di_contprd_mif->end_x));   // start_x 0 end_x 719.
        Wr(DI_CONTPRD_Y,   (di_contprd_mif->start_y <<16)  | (di_contprd_mif->end_y));   // start_y 0 end_y 239.
        Wr(DI_CONTP2RD_X,  (di_contp2rd_mif->start_x <<16) | (di_contp2rd_mif->end_x));   // start_x 0 end_x 719.
        Wr(DI_CONTP2RD_Y,  (di_contp2rd_mif->start_y <<16) | (di_contp2rd_mif->end_y));             // start_y 0 end_y 239.
        Wr(DI_CONTRD_CTRL, (di_contprd_mif->canvas_num <<8 )     |          //mtnp canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                          (urgent << 16)                             |          // urgent
#else
                          (1 << 16)                             |          // urgent
#endif
                           di_contp2rd_mif->canvas_num );                    // current field mtn canvas index.

#endif
       	Wr(DI_MTNWR_X, (di_mtnwr_mif->start_x <<16) | (di_mtnwr_mif->end_x));   	// start_x 0 end_x 719.
       	Wr(DI_MTNWR_Y, (di_mtnwr_mif->start_y <<16) | (di_mtnwr_mif->end_y));   	// start_y 0 end_y 239.
       	Wr(DI_MTNWR_CTRL, di_mtnwr_mif->canvas_num |  								// canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                      (urgent << 8));       															// urgent.
#else
                      (0 << 8));       															// urgent.
#endif
#if !defined(CONFIG_ARCH_MESON)
       //	Wr(DI_MTN_CTRL, (1 << 31) | (1 << 30) | (1 << 29) |  (mtn_ctrl_char_diff_cnt<< 24) |  (mtn_ctrl_diff_level<<16) |   (mtn_ctrl_high_level<<8) |    (mtn_ctrl_low_level<< 0));
       //   Wr(DI_MTN_CTRL,0x2300f080);
        Wr(DI_MTN_CTRL,mtn_ctrl);
       	Wr(DI_MTN_CTRL1, (mtn_ctrl1_shift<< 8) |  mtn_ctrl1_reduce);
#endif
    }

#ifdef NEW_DI_V1
	nr_w = (di_nrwr_mif->end_x - di_nrwr_mif->start_x + 1);
	nr_h = (di_nrwr_mif->end_y - di_nrwr_mif->start_y + 1);
        Wr(NR2_FRM_SIZE,(nr_h<<16)|nr_w);
	/*gate for nr*/
	#ifdef NEW_DI_TV
	Wr_reg_bits(NR2_SW_EN,nr2_en,4,1);
	#else
	/*only process sd,avoid affecting sharp*/
	if((nr_h<<1) >= 720 || nr_w >= 1280)            Wr_reg_bits(NR2_SW_EN,0,4,1);
	else
	    Wr_reg_bits(NR2_SW_EN,nr2_en,4,1);
	#endif
	/*enable noise meter*/
	Wr_reg_bits(NR2_SW_EN,1,17,1);
#endif
  	// reset pre
  	Wr(DI_PRE_CTRL, Rd(DI_PRE_CTRL) |
                   1 << 31 );                  						// frame reset for the pre modules.

#if defined(CONFIG_ARCH_MESON)
  	Wr(DI_PRE_CTRL, nr_en |        						// NR enable
                    (mtn_en << 1 ) |        						// MTN_EN
                    (pd32_check_en << 2 ) |        					// check 3:2 pulldown
                    (pd22_check_en << 3 ) |        					// check 2:2 pulldown
                    (1 << 4 ) |        								// 2:2 check mid pixel come from next field after MTN.
                    (hist_check_en << 5 ) |        					// hist check enable
                    (hist_check_only << 6 ) |        				// hist check  use chan2.
                    ((!nr_en) << 7 ) |        						// hist check use data before noise reduction.
                    ((pd22_check_en || hist_check_only) << 8 ) |	// chan 2 enable for 2:2 pull down check.
                    (pd22_check_en << 9) |        					// line buffer 2 enable
                    (0 << 10) |        								// pre drop first.
                    (0 << 11) |        								// pre repeat.
                    (0 << 12) |        								// pre viu link
                    (hold_line << 16) |      						// pre hold line number
                    (pre_field_num << 29) |        					// pre field number.
                    (0x1 << 30 )      								// pre soft rst, pre frame rst.
                   );
#else
  	Wr(DI_PRE_CTRL, nr_en |        						// NR enable
                    (mtn_en << 1 ) |        						// MTN_EN
                    (pd32_check_en << 2 ) |        					// check 3:2 pulldown
                    (pd22_check_en << 3 ) |        					// check 2:2 pulldown
                    (1 << 4 ) |        								// 2:2 check mid pixel come from next field after MTN.
                    (hist_check_en << 5 ) |        					// hist check enable
                    (1 << 6 ) |        								// hist check  use chan2.
                    ((!nr_en) << 7 ) |        						// hist check use data before noise reduction.
                    ((pd22_check_en || hist_check_only) << 8 ) |	// chan 2 enable for 2:2 pull down check.
                    (pd22_check_en << 9) |        					// line buffer 2 enable
                    (0 << 10) |        								// pre drop first.
                    ((di_pre_ctrl__di_pre_repeat!=0xff)?(di_pre_ctrl__di_pre_repeat&0x1):(0 << 11)) |      //pre repeat.
                    (0 << 12) |        								// pre viu link
                    (hold_line << 16) |      						// pre hold line number
                    (1 << 22 ) |                   					// MTN after NR.
                    (pre_field_num << 29) |        					// pre field number.
                    (0x1 << 30 )      								// pre soft rst, pre frame rst.
                   );
#endif
#ifdef SUPPORT_MPEG_TO_VDIN
	if(mpeg2vdin_flag)
		WRITE_MPEG_REG_BITS(DI_PRE_CTRL,1,13,1);// pre sync with vdin vsync
#endif
#ifdef DET3D
    if(det3d_en && (!det3d_cfg)) {
        det3d_enable(1);
	det3d_cfg = 1;
    } else if((!det3d_en) && det3d_cfg) {
        det3d_enable(0);
	det3d_cfg = 0;
    }
#endif
}


static void set_vd1_fmt_more (
		int hfmt_en,
        int hz_yc_ratio,        //2bit
        int hz_ini_phase,       //4bit
        int vfmt_en,
        int vt_yc_ratio,        //2bit
        int vt_ini_phase,       //4bit
        int y_length,
        int c_length,
        int hz_rpt              //1bit
	)
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    VSYNC_WR_MPEG_REG(VIU_VD1_FMT_CTRL, (hz_rpt << 28) |  		// hz rpt pixel
                              (hz_ini_phase << 24) |     	// hz ini phase
                              (0 << 23) |        			// repeat p0 enable
                              (hz_yc_ratio << 21) |     	// hz yc ratio
                              (hfmt_en << 20) |        		// hz enable
                              (1 << 17) |        			// nrpt_phase0 enable
                              (0 << 16) |        			// repeat l0 enable
                              (0 << 12) |        			// skip line num
                              (vt_ini_phase << 8) |     	// vt ini phase
                              (vt_phase_step << 1) |     	// vt phase step (3.4)
                              (vfmt_en << 0)             	// vt enable
                      	);

    VSYNC_WR_MPEG_REG(VIU_VD1_FMT_W, (y_length << 16) |    	// hz format width
                             (c_length << 0)      			// vt format width
                        );
}

static void set_di_inp_fmt_more (int hfmt_en,
                int hz_yc_ratio,        //2bit
                int hz_ini_phase,       //4bit
                int vfmt_en,
                int vt_yc_ratio,        //2bit
                int vt_ini_phase,       //4bit
                int y_length,
                int c_length,
                int hz_rpt              //1bit
    	)
{
	int repeat_l0_en = 1, nrpt_phase0_en = 0;
    int vt_phase_step = (16 >> vt_yc_ratio);

    Wr(DI_INP_FMT_CTRL,
                              (hz_rpt << 28) 		|    		//hz rpt pixel
                              (hz_ini_phase << 24) 	|     		//hz ini phase
                              (0 << 23)         	|        	//repeat p0 enable
                              (hz_yc_ratio << 21)  	|     		//hz yc ratio
                              (hfmt_en << 20)   	|        	//hz enable
                              (nrpt_phase0_en << 17) |        	//nrpt_phase0 enable
                              (repeat_l0_en << 16)	|        	//repeat l0 enable
                              (0 << 12)         	|        	//skip line num
                              (vt_ini_phase << 8)  	|     		//vt ini phase
                              (vt_phase_step << 1) 	|     		//vt phase step (3.4)
                              (vfmt_en << 0)             		//vt enable
              		);

    Wr(DI_INP_FMT_W, (y_length << 16) |        		//hz format width
                             (c_length << 0)                  	//vt format width
                 	);
}

static void set_di_inp_mif ( DI_MIF_t *mif, int urgent,int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;
    unsigned long vt_ini_phase = 0;

    if ( mif->set_separate_en != 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 1;
      	chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;

        if ( mif->output_field_num == 0 )
        	vt_ini_phase = 0xe;
        else
        	vt_ini_phase = 0xa;
    }
    else if ( mif->set_separate_en != 0 && mif->src_field_mode == 0 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x0;
      	chroma0_rpt_loop_pat = 0x0;
    }
    else if ( mif->set_separate_en == 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 1;
      	chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    }
    else
    {
      	chro_rpt_lastl_ctrl =0;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x00;
      	chroma0_rpt_loop_pat = 0x00;
    }


    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    Wr(DI_INP_GEN_REG, (1 << 29)    |     //reset on go field
                                (urgent << 28)			| 		// chroma urgent bit
                                (urgent << 27)          	| 		// luma urgent bit.
                                (1 << 25)                  	| 		// no dummy data.
                                (hold_line << 19)       	| 		// hold lines
                                (1 << 18)                 	| 		// push dummy pixel
                                (demux_mode << 16)      	| 		// demux_mode
                                (bytes_per_pixel << 14)    	|
                                (mif->burst_size_cr << 12)	|
                                (mif->burst_size_cb << 10) 	|
                                (mif->burst_size_y << 8)  	|
                                (chro_rpt_lastl_ctrl << 6) 	|
                                ((mif->set_separate_en!=0) << 1)	|
                                (1 << 0)                     		// cntl_enable
      );
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    if ( mif->set_separate_en == 2 ) {
         // Enable NV12 Display
        Wr_reg_bits(DI_INP_GEN_REG2, 1, 0, 1);
    }
    else{
        Wr_reg_bits(DI_INP_GEN_REG2, 0, 0, 1);
    }
#endif

    // ----------------------
    // Canvas
    // ----------------------
    Wr(DI_INP_CANVAS0, (mif->canvas0_addr2 << 16) 		| 		// cntl_canvas0_addr2
                               (mif->canvas0_addr1 << 8)   			| 		// cntl_canvas0_addr1
                               (mif->canvas0_addr0 << 0)        			// cntl_canvas0_addr0
    );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    Wr(DI_INP_LUMA_X0, (mif->luma_x_end0 << 16) | 				// cntl_luma_x_end0
                               (mif->luma_x_start0 << 0)        			// cntl_luma_x_start0
    	);
    Wr(DI_INP_LUMA_Y0, (mif->luma_y_end0 << 16) | 				// cntl_luma_y_end0
                               (mif->luma_y_start0 << 0)        			// cntl_luma_y_start0
    	);
    Wr(DI_INP_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                               (mif->chroma_x_start0 << 0)
    	);
    Wr(DI_INP_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                               (mif->chroma_y_start0 << 0)
    	);

    // ----------------------
    // Repeat or skip
    // ----------------------
    Wr(DI_INP_RPT_LOOP, (0 << 28) |
                               (0 << 24) |
                               (0 << 20) |
                               (0 << 16) |
                               (chroma0_rpt_loop_start << 12) |
                               (chroma0_rpt_loop_end << 8)  |
                               (luma0_rpt_loop_start << 4)  |
                               (luma0_rpt_loop_end << 0)
        ) ;

    Wr(DI_INP_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    Wr(DI_INP_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    Wr(DI_INP_DUMMY_PIXEL, 0x00808000);
    if ( (mif->set_separate_en != 0) )   // 4:2:0 block mode.
    {
        set_di_inp_fmt_more (
                        1,                									// hfmt_en
                        1,                									// hz_yc_ratio
                        0,                									// hz_ini_phase
                        1,                									// vfmt_en
                        1,                									// vt_yc_ratio
                        vt_ini_phase,      									// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 			// y_length
                        mif->chroma_x_end0 - mif->chroma_x_start0 + 1 , 	// c length
                        0 );                 								// hz repeat.
    }
    else
    {
        set_di_inp_fmt_more (
                        1,                											// hfmt_en
                        1,                											// hz_yc_ratio
                        0,                											// hz_ini_phase
                        0,                											// vfmt_en
                        0,                											// vt_yc_ratio
                        0,                											// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 					// y_length
                        ((mif->luma_x_end0 >>1 ) - (mif->luma_x_start0>>1) + 1),	// c length
                        0 );                 // hz repeat.
    }
}

static void set_di_mem_fmt_more (int hfmt_en,
                int hz_yc_ratio,        //2bit
                int hz_ini_phase,       //4bit
                int vfmt_en,
                int vt_yc_ratio,        //2bit
                int vt_ini_phase,       //4bit
                int y_length,
                int c_length,
                int hz_rpt              //1bit
     	)
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    Wr(DI_MEM_FMT_CTRL,
                              (hz_rpt << 28)       	|     		//hz rpt pixel
                              (hz_ini_phase << 24) 	|     		//hz ini phase
                              (0 << 23)         	|        	//repeat p0 enable
                              (hz_yc_ratio << 21)  	|     		//hz yc ratio
                              (hfmt_en << 20)   	|        	//hz enable
                              (1 << 17)         	|        	//nrpt_phase0 enable
                              (0 << 16)         	|        	//repeat l0 enable
                              (0 << 12)         	|        	//skip line num
                              (vt_ini_phase << 8)  	|     		//vt ini phase
                              (vt_phase_step << 1) 	|     		//vt phase step (3.4)
                              (vfmt_en << 0)             		//vt enable
             	);

    Wr(DI_MEM_FMT_W, (y_length << 16) |        	//hz format width
                             (c_length << 0)                  	//vt format width
            	);
}

#ifdef NEW_DI_V1
static void set_di_chan2_fmt_more (int hfmt_en,
                int hz_yc_ratio,        //2bit
                int hz_ini_phase,       //4bit
                int vfmt_en,
                int vt_yc_ratio,        //2bit
                int vt_ini_phase,       //4bit
                int y_length,
                int c_length,
                int hz_rpt              //1bit
     	)
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    Wr(DI_CHAN2_FMT_CTRL,
                              (hz_rpt << 28)       	|     		//hz rpt pixel
                              (hz_ini_phase << 24) 	|     		//hz ini phase
                              (0 << 23)         	|        	//repeat p0 enable
                              (hz_yc_ratio << 21)  	|     		//hz yc ratio
                              (hfmt_en << 20)   	|        	//hz enable
                              (1 << 17)         	|        	//nrpt_phase0 enable
                              (0 << 16)         	|        	//repeat l0 enable
                              (0 << 12)         	|        	//skip line num
                              (vt_ini_phase << 8)  	|     		//vt ini phase
                              (vt_phase_step << 1) 	|     		//vt phase step (3.4)
                              (vfmt_en << 0)             		//vt enable
             	);

    Wr(DI_CHAN2_FMT_W, (y_length << 16) |        	//hz format width
                             (c_length << 0)                  	//vt format width
            	);
}
#endif

static void set_di_mem_mif ( DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if ( mif->set_separate_en != 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 1;
      	chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    }
    else if ( mif->set_separate_en != 0 && mif->src_field_mode == 0 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x0;
      	chroma0_rpt_loop_pat = 0x0;
    }
    else if ( mif->set_separate_en == 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    }
    else
    {
      	chro_rpt_lastl_ctrl =0;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x00;
      	chroma0_rpt_loop_pat = 0x00;
    }

    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    Wr(DI_MEM_GEN_REG, (1 << 29)    |     //reset on go field
                                (urgent << 28)    			| 		// urgent bit.
                                (urgent << 27)             	| 		// urgent bit.
                                (1 << 25)                  	| 		// no dummy data.
                                (hold_line << 19)     		| 		// hold lines
                                (1 << 18)          			| 		// push dummy pixel
                                (demux_mode << 16)  		| 		// demux_mode
                                (bytes_per_pixel << 14)    	|
                                (mif->burst_size_cr << 12) 	|
                                (mif->burst_size_cb << 10) 	|
                                (mif->burst_size_y << 8)  	|
                                (chro_rpt_lastl_ctrl << 6) 	|
                                ((mif->set_separate_en!=0) << 1)	|
                                (1 << 0)                    	 	// cntl_enable
      );

    // ----------------------
    // Canvas
    // ----------------------
    Wr(DI_MEM_CANVAS0, (mif->canvas0_addr2 << 16)		| 	// cntl_canvas0_addr2
                               (mif->canvas0_addr1 << 8)      		| 	// cntl_canvas0_addr1
                               (mif->canvas0_addr0 << 0)        		// cntl_canvas0_addr0
    	);

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    Wr(DI_MEM_LUMA_X0, (mif->luma_x_end0 << 16) 		| 	// cntl_luma_x_end0
                               (mif->luma_x_start0 << 0)        		// cntl_luma_x_start0
    	);
    Wr(DI_MEM_LUMA_Y0, (mif->luma_y_end0 << 16)   		| 	// cntl_luma_y_end0
                               (mif->luma_y_start0 << 0)        		// cntl_luma_y_start0
    	);
    Wr(DI_MEM_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                               (mif->chroma_x_start0 << 0)
    	);
    Wr(DI_MEM_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                               (mif->chroma_y_start0 << 0)
    	);

    // ----------------------
    // Repeat or skip
    // ----------------------
    Wr(DI_MEM_RPT_LOOP, (0 << 28) |
                               (0   << 24) |
                               (0   << 20) |
                               (0     << 16) |
                               (chroma0_rpt_loop_start << 12) |
                               (chroma0_rpt_loop_end << 8) |
                               (luma0_rpt_loop_start << 4) |
                               (luma0_rpt_loop_end << 0)
        ) ;

    Wr(DI_MEM_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    Wr(DI_MEM_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    Wr(DI_MEM_DUMMY_PIXEL, 0x00808000);
    if ( (mif->set_separate_en != 0))   // 4:2:0 block mode.
    {
        set_di_mem_fmt_more (
                        1,                										// hfmt_en
                        1,                										// hz_yc_ratio
                        0,                										// hz_ini_phase
                        1,                										// vfmt_en
                        1,                										// vt_yc_ratio
                        0,                										// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 				// y_length
                        mif->chroma_x_end0 - mif->chroma_x_start0 + 1, 			// c length
                        0 );                 									// hz repeat.
    } else {
        set_di_mem_fmt_more (
                        1,                											// hfmt_en
                        1,                											// hz_yc_ratio
                        0,                											// hz_ini_phase
                        0,                											// vfmt_en
                        0,                											// vt_yc_ratio
                        0,                											// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 					// y_length
                        ((mif->luma_x_end0 >>1 ) - (mif->luma_x_start0>>1) + 1),  	// c length
                        0 );                 										// hz repeat.
    }
}

static void set_di_if1_fmt_more (int hfmt_en,
                int hz_yc_ratio,        //2bit
                int hz_ini_phase,       //4bit
                int vfmt_en,
                int vt_yc_ratio,        //2bit
                int vt_ini_phase,       //4bit
                int y_length,
                int c_length,
                int hz_rpt              //1bit
                )
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    VSYNC_WR_MPEG_REG(DI_IF1_FMT_CTRL,
                              (hz_rpt << 28)       	|     	//hz rpt pixel
                              (hz_ini_phase << 24) 	|     	//hz ini phase
                              (0 << 23)         	|      	//repeat p0 enable
                              (hz_yc_ratio << 21)	|     	//hz yc ratio
                              (hfmt_en << 20)   	|    	//hz enable
                              (1 << 17)         	|     	//nrpt_phase0 enable
                              (0 << 16)         	|     	//repeat l0 enable
                              (0 << 12)         	|      	//skip line num
                              (vt_ini_phase << 8)  	|     	//vt ini phase
                              (vt_phase_step << 1) 	|     	//vt phase step (3.4)
                              (vfmt_en << 0)             	//vt enable
                   	);

    VSYNC_WR_MPEG_REG(DI_IF1_FMT_W, (y_length << 16) | 		//hz format width
                             (c_length << 0)            	//vt format width
             		);
}

extern int di_vscale_skip_count;

#ifdef DI_POST_SKIP_LINE
static int di_vscale_skip_mode = 0;
static const u32 vpat[] = {0, 0x8, 0x9, 0xa, 0xb, 0xc};

int 	l_luma0_rpt_loop_start = 0;
int 	l_luma0_rpt_loop_end = 0;
int 	l_chroma0_rpt_loop_start = 0;
int 	l_chroma0_rpt_loop_end = 0;
int   l_luma0_rpt_loop_pat = 0;
int   l_chroma0_rpt_loop_pat = 0;

#endif

static void set_di_if1_mif ( DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if ( mif->set_separate_en != 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 1;
      	chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    }
    else if ( mif->set_separate_en != 0 && mif->src_field_mode == 0 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x0;
      	chroma0_rpt_loop_pat = 0x0;
    }
    else if ( mif->set_separate_en == 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    }
    else
    {
      	chro_rpt_lastl_ctrl =0;
#ifdef DI_POST_SKIP_LINE
      	if((di_vscale_skip_mode==1)&&(di_vscale_skip_count > 0)){
          	luma0_rpt_loop_start = 1;
          	luma0_rpt_loop_end = 1;
          	chroma0_rpt_loop_start = 1;
          	chroma0_rpt_loop_end = 1;
      	    luma0_rpt_loop_pat = vpat[di_vscale_skip_count]<<4; //0x00;
      	    chroma0_rpt_loop_pat = vpat[di_vscale_skip_count]<<4; //0x00;
      	}
      	else
#endif
      	{
          	luma0_rpt_loop_start = 0;
          	luma0_rpt_loop_end = 0;
          	chroma0_rpt_loop_start = 0;
          	chroma0_rpt_loop_end = 0;
      	    luma0_rpt_loop_pat = 0x00;
      	    chroma0_rpt_loop_pat = 0x00;
        }
    }

#ifdef DI_POST_SKIP_LINE
    if(di_vscale_skip_mode == 2){ //force pat, for debugging
        luma0_rpt_loop_start = l_luma0_rpt_loop_start;
        luma0_rpt_loop_end = l_luma0_rpt_loop_end;
        chroma0_rpt_loop_start = l_chroma0_rpt_loop_start;
        chroma0_rpt_loop_end = l_chroma0_rpt_loop_end;
        luma0_rpt_loop_pat = l_luma0_rpt_loop_pat;
        chroma0_rpt_loop_pat = l_chroma0_rpt_loop_pat;
    }
#endif

    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG, (1 << 29)    |     //reset on go field
                                (urgent << 28)      		|   // urgent
                                (urgent << 27)        		|  	// luma urgent
                                (1 << 25)       			| 	// no dummy data.
                                (hold_line << 19)        	| 	// hold lines
                                (1 << 18)            		| 	// push dummy pixel
                                (demux_mode << 16)   		| 	// demux_mode
                                (bytes_per_pixel << 14)    	|
                                (mif->burst_size_cr << 12) 	|
                                (mif->burst_size_cb << 10)	|
                                (mif->burst_size_y << 8)   	|
                                (chro_rpt_lastl_ctrl << 6) 	|
                                ((mif->set_separate_en!=0) << 1)	|
                                (1 << 0)                     	// cntl_enable
      	);

    // ----------------------
    // Canvas
    // ----------------------
    VSYNC_WR_MPEG_REG(DI_IF1_CANVAS0, (mif->canvas0_addr2 << 16)	| 	// cntl_canvas0_addr2
                               (mif->canvas0_addr1 << 8)      	| 	// cntl_canvas0_addr1
                               (mif->canvas0_addr0 << 0)        	// cntl_canvas0_addr0
    	);

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    VSYNC_WR_MPEG_REG(DI_IF1_LUMA_X0, (mif->luma_x_end0 << 16) | 		// cntl_luma_x_end0
                               (mif->luma_x_start0 << 0)        	// cntl_luma_x_start0
    	);
    VSYNC_WR_MPEG_REG(DI_IF1_LUMA_Y0, (mif->luma_y_end0 << 16) | 		// cntl_luma_y_end0
                               (mif->luma_y_start0 << 0)        	// cntl_luma_y_start0
    	);
    VSYNC_WR_MPEG_REG(DI_IF1_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                               (mif->chroma_x_start0 << 0)
    	);
    VSYNC_WR_MPEG_REG(DI_IF1_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                               (mif->chroma_y_start0 << 0)
    	);

    // ----------------------
    // Repeat or skip
    // ----------------------
    VSYNC_WR_MPEG_REG(DI_IF1_RPT_LOOP, (0 << 28)	|
                               (0   << 24) 		|
                               (0   << 20) 		|
                               (0     << 16) 	|
                               (chroma0_rpt_loop_start << 12) |
                               (chroma0_rpt_loop_end << 8) |
                               (luma0_rpt_loop_start << 4) |
                               (luma0_rpt_loop_end << 0)
        ) ;

    VSYNC_WR_MPEG_REG(DI_IF1_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    VSYNC_WR_MPEG_REG(DI_IF1_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    VSYNC_WR_MPEG_REG(DI_IF1_DUMMY_PIXEL, 0x00808000);
    if ( (mif->set_separate_en != 0))   // 4:2:0 block mode.
    {
        set_di_if1_fmt_more (
                        1,                										// hfmt_en
                        1,                										// hz_yc_ratio
                        0,                										// hz_ini_phase
                        1,                										// vfmt_en
                        1,                										// vt_yc_ratio
                        0,                										// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 				// y_length
                        mif->chroma_x_end0 - mif->chroma_x_start0 + 1 , 		// c length
                        0 );                 									// hz repeat.
    } else {
        set_di_if1_fmt_more (
                        1,                											// hfmt_en
                        1,                											// hz_yc_ratio
                        0,                											// hz_ini_phase
                        0,                											// vfmt_en
                        0,                											// vt_yc_ratio
                        0,                											// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 					// y_length
                        ((mif->luma_x_end0 >>1 ) - (mif->luma_x_start0>>1) + 1),  	// c length
                        0 );                 // hz repeat.
    }
}

static void set_di_chan2_mif ( DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;

    bytes_per_pixel = mif->set_separate_en ? 0 : ((mif->video_mode == 1) ? 2 : 1);
    demux_mode =  mif->video_mode & 1;

    if (mif->src_field_mode == 1 )
    {
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	luma0_rpt_loop_pat = 0x80;
    }
    else
    {
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0;
    }
    // ----------------------
    // General register
    // ----------------------

    Wr(DI_CHAN2_GEN_REG, (1 << 29)    |     //reset on go field
                                (urgent << 28)            		|   // urgent
                                (urgent << 27)               		|  	// luma urgent
                                (1 << 25)        					| 	// no dummy data.
                                (hold_line << 19)               	| 	// hold lines
                                (1 << 18)                  			| 	// push dummy pixel
                                (demux_mode << 16)           		| 	// demux_mode
                                (bytes_per_pixel << 14)    			|
                                (0 << 12)      						|
                                (0 << 10)      						|
                                (mif->burst_size_y << 8)        	|
                                ( (hold_line == 0 ? 1 : 0 ) << 7 ) 	|  	//manual start.
                                (0 << 6) 							|
                                (0 << 1)      						|
                                (1 << 0)                     			// cntl_enable
      );


    // ----------------------
    // Canvas
    // ----------------------
    Wr(DI_CHAN2_CANVAS, (0 << 16) 		| 		// cntl_canvas0_addr2
                                (0 << 8)      		| 		// cntl_canvas0_addr1
                                (mif->canvas0_addr0 << 0)   // cntl_canvas0_addr0
    );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    Wr(DI_CHAN2_LUMA_X, (mif->luma_x_end0 << 16) 	| 		// cntl_luma_x_end0
                                (mif->luma_x_start0 << 0)        		// cntl_luma_x_start0
    	);
    Wr(DI_CHAN2_LUMA_Y, (mif->luma_y_end0 << 16)  | 		// cntl_luma_y_end0
                                (mif->luma_y_start0 << 0)        		// cntl_luma_y_start0
    	);

    // ----------------------
    // Repeat or skip
    // ----------------------
    Wr(DI_CHAN2_RPT_LOOP, (0 << 28) |
                                (0   << 24) |
                                (0   << 20) |
                                (0   << 16) |
                                (0   << 12) |
                                (0   << 8)  |
                                (luma0_rpt_loop_start << 4)  |
                                (luma0_rpt_loop_end << 0)
    );

    Wr(DI_CHAN2_LUMA_RPT_PAT, luma0_rpt_loop_pat);

    // Dummy pixel value
    Wr(DI_CHAN2_DUMMY_PIXEL, 0x00808000);

#ifdef NEW_DI_V1
    if ( (mif->set_separate_en != 0))   // 4:2:0 block mode.
    {
        set_di_chan2_fmt_more (
                        1,                										// hfmt_en
                        1,                										// hz_yc_ratio
                        0,                										// hz_ini_phase
                        1,                										// vfmt_en
                        1,                										// vt_yc_ratio
                        0,                										// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 				// y_length
                        mif->chroma_x_end0 - mif->chroma_x_start0 + 1, 			// c length
                        0 );                 									// hz repeat.
    } else {
        set_di_chan2_fmt_more (
                        1,                											// hfmt_en
                        1,                											// hz_yc_ratio
                        0,                											// hz_ini_phase
                        0,                											// vfmt_en
                        0,                											// vt_yc_ratio
                        0,                											// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 					// y_length
                        ((mif->luma_x_end0 >>1 ) - (mif->luma_x_start0>>1) + 1),  	// c length
                        0 );                 										// hz repeat.
    }
#endif

}

static void set_di_if0_mif ( DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if ( mif->set_separate_en != 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 1;
      	chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    }
    else if ( mif->set_separate_en != 0 && mif->src_field_mode == 0 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 0;
      	luma0_rpt_loop_end = 0;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
      	luma0_rpt_loop_pat = 0x0;
      	chroma0_rpt_loop_pat = 0x0;
    }
    else if ( mif->set_separate_en == 0 && mif->src_field_mode == 1 )
    {
      	chro_rpt_lastl_ctrl =1;
      	luma0_rpt_loop_start = 1;
      	luma0_rpt_loop_end = 1;
      	chroma0_rpt_loop_start = 0;
      	chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    }
    else
    {
#ifdef DI_POST_SKIP_LINE
      	if((di_vscale_skip_mode==1)&&(di_vscale_skip_count > 0)){
          	luma0_rpt_loop_start = 1;
          	luma0_rpt_loop_end = 1;
          	chroma0_rpt_loop_start = 1;
          	chroma0_rpt_loop_end = 1;
          	luma0_rpt_loop_pat = vpat[di_vscale_skip_count]<<4; //0x00;
          	chroma0_rpt_loop_pat = vpat[di_vscale_skip_count]<<4; //0x00;
      	}
      	else
#endif
        {
          	chro_rpt_lastl_ctrl =0;
          	luma0_rpt_loop_start = 0;
          	luma0_rpt_loop_end = 0;
          	chroma0_rpt_loop_start = 0;
          	chroma0_rpt_loop_end = 0;
      	    luma0_rpt_loop_pat = 0x00;
      	    chroma0_rpt_loop_pat = 0x00;
        }
    }

#ifdef DI_POST_SKIP_LINE
    if(di_vscale_skip_mode == 2){ //force pat, for debugging
        luma0_rpt_loop_start = l_luma0_rpt_loop_start;
        luma0_rpt_loop_end = l_luma0_rpt_loop_end;
        chroma0_rpt_loop_start = l_chroma0_rpt_loop_start;
        chroma0_rpt_loop_end = l_chroma0_rpt_loop_end;
        luma0_rpt_loop_pat = l_luma0_rpt_loop_pat;
        chroma0_rpt_loop_pat = l_chroma0_rpt_loop_pat;
    }
#endif
    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    VSYNC_WR_MPEG_REG(VD1_IF0_GEN_REG, (1 << 29) |              //reset on go field
                                (urgent << 28)      	|   	// urgent
                                (urgent << 27)          	|  		// luma urgent
                                (1 << 25)					| 		// no dummy data.
                                (hold_line << 19)       	| 		// hold lines
                                (1 << 18)               	| 		// push dummy pixel
                                (demux_mode << 16)     		| 		// demux_mode
                                (bytes_per_pixel << 14)    	|
                                (mif->burst_size_cr << 12) 	|
                                (mif->burst_size_cb << 10)	|
                                (mif->burst_size_y << 8)  	|
                                (chro_rpt_lastl_ctrl << 6) 	|
                                ((mif->set_separate_en!=0) << 1)	|
                                (1 << 0)                     		// cntl_enable
      	);

    // ----------------------
    // Canvas
    // ----------------------
    VSYNC_WR_MPEG_REG(VD1_IF0_CANVAS0, (mif->canvas0_addr2 << 16) 		| 	// cntl_canvas0_addr2
                               (mif->canvas0_addr1 << 8)      		| 	// cntl_canvas0_addr1
                               (mif->canvas0_addr0 << 0)        		// cntl_canvas0_addr0
    	);

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_X0, (mif->luma_x_end0 << 16) | 		// cntl_luma_x_end0
                               (mif->luma_x_start0 << 0)        	// cntl_luma_x_start0
    	);
    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_Y0, (mif->luma_y_end0 << 16) | 		// cntl_luma_y_end0
                               (mif->luma_y_start0 << 0)        	// cntl_luma_y_start0
    	);
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                               (mif->chroma_x_start0 << 0)
    	);
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                               (mif->chroma_y_start0 << 0)
    	);

    // ----------------------
    // Repeat or skip
    // ----------------------
    VSYNC_WR_MPEG_REG(VD1_IF0_RPT_LOOP, (0 << 28) 		|
                               (0   << 24) 			|
                               (0   << 20) 			|
                               (0   << 16) 			|
                               (chroma0_rpt_loop_start << 12) |
                               (chroma0_rpt_loop_end << 8) |
                               (luma0_rpt_loop_start << 4) |
                               (luma0_rpt_loop_end << 0)
        ) ;

    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    VSYNC_WR_MPEG_REG(VD1_IF0_DUMMY_PIXEL, 0x00808000);

   	// ----------------------
    // Picture 1 unused
    // ----------------------
    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_X1, 0);                      		// unused
    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_Y1, 0);                           // unused
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_X1, 0);                        // unused
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_Y1, 0);                        // unused
    VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_PSEL, 0);                        	// unused only one picture
    VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_PSEL, 0);                      // unused only one picture

    if ( (mif->set_separate_en != 0))   // 4:2:0 block mode.
    {
        set_vd1_fmt_more (
                        1,                									// hfmt_en
                        1,                									// hz_yc_ratio
                        0,                									// hz_ini_phase
                        1,                									// vfmt_en
                        1,                									// vt_yc_ratio
                        0,                									// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 			// y_length
                        mif->chroma_x_end0 - mif->chroma_x_start0 + 1 , 	// c length
                        0 );                 								// hz repeat.
    }
    else
    {
        set_vd1_fmt_more (
                        1,                											// hfmt_en
                        1,                											// hz_yc_ratio
                        0,                											// hz_ini_phase
                        0,                											// vfmt_en
                        0,                											// vt_yc_ratio
                        0,                											// vt_ini_phase
                        mif->luma_x_end0 - mif->luma_x_start0 + 1, 					// y_length
                        ((mif->luma_x_end0 >>1 ) - (mif->luma_x_start0>>1) + 1) , 	//c length
                        0 );                 										// hz repeat.
    }
}

void initial_di_pre_aml ( int hsize_pre, int vsize_pre, int hold_line )
{
   	Wr(DI_PRE_SIZE, (hsize_pre -1 ) | ((vsize_pre -1) << 16) );
   	Wr(DI_PRE_CTRL, 0 |        		// NR enable
                    (0 << 1 ) |        			// MTN_EN
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                    (1 << 2 ) |        			// check 3:2 pulldown
#else
                    (0 << 2 ) |        			// check 3:2 pulldown
#endif
					(0 << 3 ) |        			// check 2:2 pulldown
                    (0 << 4 ) |        			// 2:2 check mid pixel come from next field after MTN.
                    (0 << 5 ) |        			// hist check enable
                    (0 << 6 ) |        			// hist check not use chan2.
                    (0 << 7 ) |        			// hist check use data before noise reduction.
                    (0 << 8 ) |        			// chan 2 enable for 2:2 pull down check.
                    (0 << 9 ) |        			// line buffer 2 enable
                    (0 << 10) |        			// pre drop first.
                    (0 << 11) |        			// pre repeat.
                    (0 << 12) |        			// pre viu link
                    (hold_line << 16) |      	// pre hold line number
                    (0 << 29) |        			// pre field number.
                    (0x3 << 30)      			// pre soft rst, pre frame rst.
           	);
#ifdef SUPPORT_MPEG_TO_VDIN
	if(mpeg2vdin_flag)
		WRITE_MPEG_REG_BITS(DI_PRE_CTRL,1,13,1);// pre sync with vdin vsync
#endif
    Wr(DI_MC_22LVL0, (Rd(DI_MC_22LVL0) & 0xffff0000 ) | 256);                //   field 22 level
    Wr(DI_MC_32LVL0, (Rd(DI_MC_32LVL0) & 0xffffff00 ) | 16);       				// field 32 level
}

void initial_di_post_2 ( int hsize_post, int vsize_post, int hold_line )
{
   	VSYNC_WR_MPEG_REG(DI_POST_SIZE, (hsize_post -1) | ((vsize_post -1 ) << 16));
   	/* di demo */
   	VSYNC_WR_MPEG_REG(DI_BLEND_REG0_X,((hsize_post-1)>>1));
   	VSYNC_WR_MPEG_REG(DI_BLEND_REG0_Y,(vsize_post-1));
   	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,
                      (0x2 << 20) |      				// top mode. EI only
                       25);              				// KDEINT
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL0, ei_ctrl0);
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL1, ei_ctrl1);
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL2, ei_ctrl2);
#ifdef NEW_DI_V1
	VSYNC_WR_MPEG_REG(DI_EI_CTRL3, ei_ctrl3);
#endif
   	VSYNC_WR_MPEG_REG(DI_POST_CTRL, (0 << 0 ) |        		// line buffer 0 enable
                      (0 << 1)  |        				// line buffer 1 enable
                      (0 << 2)  |        				// ei  enable
                      (0 << 3)  |        				// mtn line buffer enable
                      (0 << 4)  |        				// mtnp read mif enable
                      (0 << 5)  |        				// di blend enble.
                      (0 << 6)  |        				// di mux output enable
                      (0 << 7)  |        				// di write to SDRAM enable.
                      (1 << 8)  |        				// di to VPP enable.
                      (0 << 9)  |        				// mif0 to VPP enable.
                      (0 << 10) |        				// post drop first.
                      (0 << 11) |        				// post repeat.
                      (0 << 12) |        				// post viu link
                      (hold_line << 16) |      			// post hold line number
                      (0 << 29) |        				// post field number.
                      (0x3 << 30)       				// post soft rst  post frame rst.
        );
}

void di_post_switch_buffer (
   DI_MIF_t        *di_buf0_mif,
   DI_MIF_t        *di_buf1_mif,
   DI_SIM_MIF_t    *di_diwr_mif,
   DI_SIM_MIF_t    *di_mtncrd_mif,
   DI_SIM_MIF_t    *di_mtnprd_mif,
   int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
   int post_field_num, int hold_line, int urgent,
   unsigned long * reg_mtn_info )
{
  	int ei_only;
  	int buf1_en;

    /**/
  	ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en );
  	buf1_en =  ( !ei_only && (di_ddr_en || di_vpp_en ) );

  	if ( ei_en || di_vpp_en || di_ddr_en )
  	{
    VSYNC_WR_MPEG_REG(VD1_IF0_CANVAS0, (di_buf0_mif->canvas0_addr2 << 16) 		| 	// cntl_canvas0_addr2
                               (di_buf0_mif->canvas0_addr1 << 8)      		| 	// cntl_canvas0_addr1
                               (di_buf0_mif->canvas0_addr0 << 0)        		// cntl_canvas0_addr0
    	);
  	}

  	if ( !ei_only && (di_ddr_en || di_vpp_en ) )
  	{
    VSYNC_WR_MPEG_REG(DI_IF1_CANVAS0, (di_buf1_mif->canvas0_addr2 << 16)	| 	// cntl_canvas0_addr2
                               (di_buf1_mif->canvas0_addr1 << 8)      	| 	// cntl_canvas0_addr1
                               (di_buf1_mif->canvas0_addr0 << 0)        	// cntl_canvas0_addr0
    	);
  	}

   	// motion for current display field.
    if ( blend_mtn_en )
    {

        VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num <<8 )|//mtnp canvas index.
					 (urgent << 16) |// urgent
					 di_mtncrd_mif->canvas_num ); // current field mtn canvas index.

    }

    if ( di_ddr_en )
    {
       VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |               							// canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                        (urgent << 16));            													// urgent.
#else
                        (urgent << 8));            													// urgent.
#endif
    }
   	if ( ei_only == 0)
   	{
#if defined(CONFIG_ARCH_MESON)
      	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,  (Rd(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20 )))) | // clean some bit we need to set.
                              (blend_mtn_en << 26 ) |   													// blend mtn enable.
                              (0 << 25 ) |   																// blend with the mtn of the pre display field and next display field.
                              (1 << 24 ) |   																// blend with pre display field.
                              (blend_mode << 20)    														// motion adaptive blend.
               );
#else

	//VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (blend_ctrl&(~(3<<20))&~(0xff))|(blend_mode<<20)|kdeint);
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (blend_ctrl&0xffcfff00)| (blend_mode<<20) | (0xff&kdeint0));
    //if (di_pre_stru.di_wr_buf->mtn_info[4] > di_pre_stru.di_wr_buf->mtn_info[3] & di_pre_stru.di_wr_buf->mtn_info[3] > di_pre_stru.di_wr_buf->mtn_info[2])
    if((reg_mtn_info[0]>mtn_thre_1_high)&(reg_mtn_info[4]<mtn_thre_2_low)){
 	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,((blend_ctrl&0xffcfff00) | (blend_mode<<20)| (0xff&kdeint1)));}
    //if((reg_mtn_info[0]<mtn_thre_1_low)&(reg_mtn_info[4]<mtn_thre_2_low)){
    //VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,(0x19700000 | kdeint1));}
	if(reg_mtn_info[4]>mtn_thre_2_high){
	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,((blend_ctrl&0xffcfff00) | (blend_mode<<20)| (0xff&kdeint2)));
	}
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL1, (blend_ctrl1_char_level<< 24 ) |    ( blend_ctrl1_angle_thd << 16 ) |    ( blend_ctrl1_filt_thd<< 8 )  |    ( blend_ctrl1_diff_thd));
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL2,   (blend_ctrl2_black_level<< 8 ) |     (blend_ctrl2_mtn_no_mov)  );
#ifdef NEW_DI_V1
//    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, Rd(DI_BLEND_CTRL)&(~(1<<31)));
#endif
#endif
   	}

    VSYNC_WR_MPEG_REG_BITS(DI_POST_CTRL, post_field_num, 29, 1);
}

void enable_di_post_2 (
   DI_MIF_t        *di_buf0_mif,
   DI_MIF_t        *di_buf1_mif,
   DI_SIM_MIF_t    *di_diwr_mif,
   DI_SIM_MIF_t    *di_mtncrd_mif,
   DI_SIM_MIF_t    *di_mtnprd_mif,
   int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
   int post_field_num, int hold_line, int urgent,
   unsigned long * reg_mtn_info )
{
  	int ei_only;
  	int buf1_en;

    /* make these 3 register can be run-time changed */
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL0, ei_ctrl0);
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL1, ei_ctrl1);
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL2, ei_ctrl2);
#ifdef NEW_DI_V1
   	VSYNC_WR_MPEG_REG(DI_EI_CTRL3, ei_ctrl3);
#endif
    /**/
  	ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en );
  	buf1_en =  ( !ei_only && (di_ddr_en || di_vpp_en ) );

  	if ( ei_en || di_vpp_en || di_ddr_en )
  	{
     	set_di_if0_mif( di_buf0_mif, di_vpp_en, hold_line );
  	}

  	if ( !ei_only && (di_ddr_en || di_vpp_en ) )
  	{
     	set_di_if1_mif( di_buf1_mif, di_vpp_en, hold_line );
  	}
//printk("%s: ei_only %d,buf1_en %d,ei_en %d,di_vpp_en %d,di_ddr_en %d,blend_mtn_en %d,blend_mode %d.\n",
			 //__func__,ei_only,buf1_en,ei_en,di_vpp_en,di_ddr_en,blend_mtn_en,blend_mode);
   	// motion for current display field.
    if ( blend_mtn_en )
    {
        VSYNC_WR_MPEG_REG(DI_MTNPRD_X, (di_mtnprd_mif->start_x <<16) | (di_mtnprd_mif->end_x));   			// start_x 0 end_x 719.
        VSYNC_WR_MPEG_REG(DI_MTNPRD_Y, (di_mtnprd_mif->start_y <<16) | (di_mtnprd_mif->end_y));   			// start_y 0 end_y 239.
      	VSYNC_WR_MPEG_REG(DI_MTNCRD_X, (di_mtncrd_mif->start_x <<16) | (di_mtncrd_mif->end_x));   				// start_x 0 end_x 719.
      	VSYNC_WR_MPEG_REG(DI_MTNCRD_Y, (di_mtncrd_mif->start_y <<16) | (di_mtncrd_mif->end_y));             	// start_y 0 end_y 239.
	VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num <<8 ) |									//mtnp canvas index.
					 (urgent << 16) |// urgent
					 di_mtncrd_mif->canvas_num );
                                         // current field mtn canvas index
    }

    if ( di_ddr_en )
    {
       VSYNC_WR_MPEG_REG(DI_DIWR_X, (di_diwr_mif->start_x <<16) | (di_diwr_mif->end_x));   				// start_x 0 end_x 719.
       VSYNC_WR_MPEG_REG(DI_DIWR_Y, (di_diwr_mif->start_y <<16) | (di_diwr_mif->end_y *2 + 1 ));         	// start_y 0 end_y 479.
       VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |               							// canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                        (urgent << 16));            													// urgent.
#else
                        (urgent << 8));            													// urgent.
#endif
    }

   	if ( ei_only == 0)
   	{
#if defined(CONFIG_ARCH_MESON)
      	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,  (Rd(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20 )))) | // clean some bit we need to set.
                              (blend_mtn_en << 26 ) |   													// blend mtn enable.
                              (0 << 25 ) |   																// blend with the mtn of the pre display field and next display field.
                              (1 << 24 ) |   																// blend with pre display field.
                              (blend_mode << 20)    														// motion adaptive blend.
               );
#else

	//VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (blend_ctrl&(~(3<<20))&~(0xff))|(blend_mode<<20)|kdeint);
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (blend_ctrl&0xffcfff00)|(blend_mode<<20)|(0xff&kdeint0));
    //if (di_pre_stru.di_wr_buf->mtn_info[4] > di_pre_stru.di_wr_buf->mtn_info[3] & di_pre_stru.di_wr_buf->mtn_info[3] > di_pre_stru.di_wr_buf->mtn_info[2])
    if((reg_mtn_info[0]>mtn_thre_1_high)&(reg_mtn_info[4]<mtn_thre_2_low)){
 	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,((blend_ctrl&0xffcfff00) | (blend_mode<<20)| (0xff&kdeint1)));
 	}
    //if((reg_mtn_info[0]<mtn_thre_1_low)&(reg_mtn_info[4]<mtn_thre_2_low)){
    //VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,(0x19700000 | kdeint1));}
	if(reg_mtn_info[4]>mtn_thre_2_high){
	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL,((blend_ctrl&0xffcfff00) | (blend_mode<<20)| (0xff&kdeint2)));
	}
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL1, (blend_ctrl1_char_level<< 24 ) |    ( blend_ctrl1_angle_thd << 16 ) |    ( blend_ctrl1_filt_thd<< 8 )  |    ( blend_ctrl1_diff_thd));
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL2,   (blend_ctrl2_black_level<< 8 ) |     (blend_ctrl2_mtn_no_mov)  );
#ifdef NEW_DI_V1
//    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, Rd(DI_BLEND_CTRL)&(~(1<<31)));
#endif
#endif
   	}

#if defined(CONFIG_ARCH_MESON)
   	VSYNC_WR_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0 ) | 		// line buffer 0 enable
                      (0 << 1)  |        							// line buffer 1 enable
                      (ei_en << 2) |        						// ei  enable
                      (blend_mtn_en << 3) |        					// mtn line buffer enable
                      (blend_mtn_en  << 4) |        				// mtnp read mif enable
                      ((post_ctrl__di_blend_en!=0xff)?(post_ctrl__di_blend_en&0x1):(blend_en << 5)) |        						// di blend enble.
                      (1 << 6) |        							// di mux output enable
                      (di_ddr_en << 7) |        					// di write to SDRAM enable.
                      (di_vpp_en << 8) |        					// di to VPP enable.
                      (0 << 9) |        							// mif0 to VPP enable.
                      (0 << 10) |        							// post drop first.
                      (0 << 11) |        							// post repeat.
                      (1 << 12) |        							// post viu link
                      (hold_line << 16) |       					// post hold line number
                      (post_field_num << 29) |        				// post field number.
                      (0x1 << 30 )       							// post soft rst  post frame rst.
        );
#else
   	VSYNC_WR_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0 ) | 		// line buffer 0 enable
                      (0 << 1)  |        							// line buffer 1 enable
                      (ei_en << 2) |        						// ei  enable
                      (blend_mtn_en << 3) |        					// mtn line buffer enable
                      (blend_mtn_en  << 4) |        				// mtnp read mif enable
                      ((post_ctrl__di_blend_en!=0xff)?(post_ctrl__di_blend_en&0x1):(blend_en << 5)) |        						// di blend enble.
                      (1 << 6) |        							// di mux output enable
                      (di_ddr_en << 7) |        					// di write to SDRAM enable.
                      (di_vpp_en << 8) |        					// di to VPP enable.
                      (0 << 9) |        							// mif0 to VPP enable.
                      (0 << 10) |        							// post drop first.
                      ((post_ctrl__di_post_repeat!=0xff)?(post_ctrl__di_post_repeat&0x1):(0 << 11)) |      // post repeat.
                      (di_vpp_en << 12) |   						// post viu link
                      (hold_line << 16) |       					// post hold line number
                      (post_field_num << 29) |        				// post field number.
                      (0x1 << 30 )       							// post soft rst  post frame rst.
        );
#endif
#ifdef NEW_DI_V1
        VSYNC_WR_MPEG_REG(DI_EI_CTRL3, ei_ctrl3);
#endif
}
#if 0
void di_post_switch_buffer_pd (
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
    int post_field_num, int hold_line, int urgent)
{
    int blend_mtn_filt_en = 1;
    int blend_data_filt_en = 1;
    int post_mb_en = 0;
    int ei_only;
    int buf1_en;

    ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
    buf1_en = (!ei_only && (di_ddr_en || di_vpp_en));

    if (ei_en || di_vpp_en || di_ddr_en) {
    VSYNC_WR_MPEG_REG(VD1_IF0_CANVAS0, (di_buf0_mif->canvas0_addr2 << 16) 		| 	// cntl_canvas0_addr2
                               (di_buf0_mif->canvas0_addr1 << 8)      		| 	// cntl_canvas0_addr1
                               (di_buf0_mif->canvas0_addr0 << 0)        		// cntl_canvas0_addr0
    	);
  	}

    if (!ei_only && (di_ddr_en || di_vpp_en)) {
    VSYNC_WR_MPEG_REG(DI_IF1_CANVAS0, (di_buf1_mif->canvas0_addr2 << 16)	| 	// cntl_canvas0_addr2
                               (di_buf1_mif->canvas0_addr1 << 8)      	| 	// cntl_canvas0_addr1
                               (di_buf1_mif->canvas0_addr0 << 0)        	// cntl_canvas0_addr0
    	);
  	}


    // motion for current display field.
    if (blend_mtn_en) {
	VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num << 8) | //mtnp canvas index.
					 (urgent << 16) | // urgent
					 di_mtncrd_mif->canvas_num);
                                         // current field mtn canvas index.
    }

    if (di_ddr_en) {
        VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |                                           // canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                       (urgent << 16));                                                              // urgent.
#else
                       (urgent << 8));                                                              // urgent.
#endif
    }

    if (ei_only == 0) {
#if defined(CONFIG_ARCH_MESON)
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (Rd(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20)))) |   // clean some bit we need to set.
                       (blend_mtn_en << 26) |                                                        // blend mtn enable.
                       (0 << 25) |                                                                   // blend with the mtn of the pre display field and next display field.
                       (1 << 24) |                                                                   // blend with pre display field.
                       (blend_mode << 20)                                                            // motion adaptive blend.
                      );
#else
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, 
	#ifdef NEW_DI_V1
       		       (1<<31) |        //enable new ei(remove from m8b)
        #endif
                       (post_mb_en << 28) |                                                   // post motion blur enable.
                       (0 << 27) |                                                                    // mtn3p(l, c, r) max.
                       (0 << 26) |                                                                    // mtn3p(l, c, r) min.
                       (0 << 25) |                                                                    // mtn3p(l, c, r) ave.
                       (1 << 24) |                                                                    // mtntopbot max
                       (blend_mtn_filt_en  << 23) |                                                   // blend mtn filter enable.
                       (blend_data_filt_en << 22) |                                                   // blend data filter enable.
                       (blend_mode << 20) |                                                       // motion adaptive blend.
                       25                                                                            // kdeint.
                      );
#endif
    }
    VSYNC_WR_MPEG_REG_BITS(DI_POST_CTRL, post_field_num, 29, 1);
}

void enable_di_post_pd(
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
    int post_field_num, int hold_line, int urgent)
{
    int blend_mtn_filt_en = 1;
    int blend_data_filt_en = 1;
    int post_mb_en = 0;
    int ei_only;
    int buf1_en;

    ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
    buf1_en = (!ei_only && (di_ddr_en || di_vpp_en));

    if (ei_en || di_vpp_en || di_ddr_en) {
        set_di_if0_mif(di_buf0_mif, di_vpp_en, hold_line);
    }

    if (!ei_only && (di_ddr_en || di_vpp_en)) {
        set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);
    }

    // motion for current display field.
    if (blend_mtn_en) {
        VSYNC_WR_MPEG_REG(DI_MTNPRD_X, (di_mtnprd_mif->start_x << 16) | (di_mtnprd_mif->end_x));           // start_x 0 end_x 719.
        VSYNC_WR_MPEG_REG(DI_MTNPRD_Y, (di_mtnprd_mif->start_y << 16) | (di_mtnprd_mif->end_y));           // start_y 0 end_y 239.
        VSYNC_WR_MPEG_REG(DI_MTNCRD_X, (di_mtncrd_mif->start_x << 16) | (di_mtncrd_mif->end_x));               // start_x 0 end_x 719.
        VSYNC_WR_MPEG_REG(DI_MTNCRD_Y, (di_mtncrd_mif->start_y << 16) | (di_mtncrd_mif->end_y));               // start_y 0 end_y 239.
	VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num << 8) |//mtnp canvas index.
					 (urgent << 16) |  // urgent
					 di_mtncrd_mif->canvas_num);
                                         // current field mtn canvas index.
    }

    if (di_ddr_en) {
        VSYNC_WR_MPEG_REG(DI_DIWR_X, (di_diwr_mif->start_x << 16) | (di_diwr_mif->end_x));                  // start_x 0 end_x 719.
        VSYNC_WR_MPEG_REG(DI_DIWR_Y, (di_diwr_mif->start_y << 16) | (di_diwr_mif->end_y * 2 + 1));          // start_y 0 end_y 479.
        VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |                                           // canvas index.
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                       (urgent << 16));                                                              // urgent.
#else
                       (urgent << 8));                                                              // urgent.
#endif
    }

    if (ei_only == 0) {
#if defined(CONFIG_ARCH_MESON)
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, (Rd(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20)))) |   // clean some bit we need to set.
                       (blend_mtn_en << 26) |                                                        // blend mtn enable.
                       (0 << 25) |                                                                   // blend with the mtn of the pre display field and next display field.
                       (1 << 24) |                                                                   // blend with pre display field.
                       (blend_mode << 20)                                                            // motion adaptive blend.
                      );
#else
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, 
     	#ifdef NEW_DI_V1
       		       (1<<31)   |      //enable new ei(remove from m8b)
       	#endif
        	       (post_mb_en << 28) |                                                   // post motion blur enable.
                       (0 << 27) |                                                                    // mtn3p(l, c, r) max.
                       (0 << 26) |                                                                    // mtn3p(l, c, r) min.
                       (0 << 25) |                                                                    // mtn3p(l, c, r) ave.
                       (1 << 24) |                                                                    // mtntopbot max
                       (blend_mtn_filt_en  << 23) |                                                   // blend mtn filter enable.
                       (blend_data_filt_en << 22) |                                                   // blend data filter enable.
                       (blend_mode << 20) |                                                       // motion adaptive blend.
                       25                                                                            // kdeint.
                      );
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL1, (196 << 24) |                                                        // char level
                       (64 << 16) |                                                                   // angle thredhold.
                       (40 << 8)  |                                                                   // all_af filt thd.
                       (64));                                                                         // all 4 equal
        VSYNC_WR_MPEG_REG(DI_BLEND_CTRL2, (4 << 8) |                                                           // mtn no mov level.
                       (48));                                                                        //black level.
#endif
    }

#if defined(CONFIG_ARCH_MESON)
    VSYNC_WR_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0) |        // line buffer 0 enable
                   (0 << 1)  |                                   // line buffer 1 enable
                   (ei_en << 2) |                                // ei  enable
                   (blend_mtn_en << 3) |                         // mtn line buffer enable
                   (blend_mtn_en  << 4) |                        // mtnp read mif enable
                   (blend_en << 5) |                             // di blend enble.
                   (1 << 6) |                                    // di mux output enable
                   (di_ddr_en << 7) |                            // di write to SDRAM enable.
                   (di_vpp_en << 8) |                            // di to VPP enable.
                   (0 << 9) |                                    // mif0 to VPP enable.
                   (0 << 10) |                                   // post drop first.
                   (0 << 11) |                                   // post repeat.
                   (1 << 12) |                                   // post viu link
                   (hold_line << 16) |                           // post hold line number
                   (post_field_num << 29) |                      // post field number.
                   (0x1 << 30)                                   // post soft rst  post frame rst.
                  );
#else
    VSYNC_WR_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0) |        // line buffer 0 enable
                   (0 << 1)  |                                   // line buffer 1 enable
                   (ei_en << 2) |                                // ei  enable
                   (blend_mtn_en << 3) |                         // mtn line buffer enable
                   (blend_mtn_en  << 4) |                        // mtnp read mif enable
                   (blend_en << 5) |                             // di blend enble.
                   (1 << 6) |                                    // di mux output enable
                   (di_ddr_en << 7) |                            // di write to SDRAM enable.
                   (di_vpp_en << 8) |                            // di to VPP enable.
                   (0 << 9) |                                    // mif0 to VPP enable.
                   (0 << 10) |                                   // post drop first.
                   (0 << 11) |                                   // post repeat.
                   (di_vpp_en << 12) |                           // post viu link
                   (hold_line << 16) |                           // post hold line number
                   (post_field_num << 29) |                      // post field number.
                   (0x1 << 30)                                   // post soft rst  post frame rst.
                  );
#endif
}
#endif

void disable_post_deinterlace_2(void)
{
	VSYNC_WR_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
    VSYNC_WR_MPEG_REG(DI_POST_SIZE, (32-1) | ((128-1) << 16));
    VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG, 0x3 << 30);
    //VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG, Rd(DI_IF1_GEN_REG) & 0xfffffffe);
}

void enable_di_mode_check_2( int win0_start_x, int win0_end_x, int win0_start_y, int win0_end_y,
                        int win1_start_x, int win1_end_x, int win1_start_y, int win1_end_y,
                        int win2_start_x, int win2_end_x, int win2_start_y, int win2_end_y,
                        int win3_start_x, int win3_end_x, int win3_start_y, int win3_end_y,
                        int win4_start_x, int win4_end_x, int win4_start_y, int win4_end_y
                        )
{
    pd_win_prop[0].pixels_num = (win0_end_x-win0_start_x)*(win0_end_y-win0_start_y);
    pd_win_prop[1].pixels_num = (win1_end_x-win1_start_x)*(win1_end_y-win1_start_y);
    pd_win_prop[2].pixels_num = (win2_end_x-win2_start_x)*(win2_end_y-win2_start_y);
    pd_win_prop[3].pixels_num = (win3_end_x-win3_start_x)*(win3_end_y-win3_start_y);
    pd_win_prop[4].pixels_num = (win4_end_x-win4_start_x)*(win4_end_y-win4_start_y);

    Wr(DI_MC_REG0_X, (win0_start_x <<16) |     		// start_x
                       win0_end_x );       						// end_x
    Wr(DI_MC_REG0_Y, (win0_start_y <<16) |     		// start_y
                       win0_end_y );        					// end_x
    Wr(DI_MC_REG1_X, (win1_start_x <<16) |     		// start_x
                       win1_end_x );       						// end_x
    Wr(DI_MC_REG1_Y, (win1_start_y <<16) |     		// start_y
                       win1_end_y );        					// end_x
    Wr(DI_MC_REG2_X, (win2_start_x <<16) |     		// start_x
                       win2_end_x );       						// end_x
    Wr(DI_MC_REG2_Y, (win2_start_y <<16) |     		// start_y
                       win2_end_y );        					// end_x
    Wr(DI_MC_REG3_X, (win3_start_x <<16) |     		// start_x
                       win3_end_x );       						// end_x
    Wr(DI_MC_REG3_Y, (win3_start_y <<16) |     		// start_y
                       win3_end_y );        					// end_x
    Wr(DI_MC_REG4_X, (win4_start_x <<16) |     		// start_x
                       win4_end_x );       						// end_x
    Wr(DI_MC_REG4_Y, (win4_start_y <<16) |     		// start_y
                       win4_end_y );        					// end_x

    Wr(DI_MC_32LVL1, pd_win_prop[3].win_32lvl |          			//region 3
                     (pd_win_prop[4].win_32lvl << 8));   						//region 4
    Wr(DI_MC_32LVL0, field_32lvl        |   		//field 32 level
                     (pd_win_prop[0].win_32lvl << 8)  |   					//region 0
                     (pd_win_prop[1].win_32lvl << 16) |   					//region 1
                     (pd_win_prop[2].win_32lvl << 24));  						//region 2.
    Wr(DI_MC_22LVL0,  field_22lvl  |           		// field 22 level
                     (pd_win_prop[0].win_22lvl << 16));   					// region 0.

    Wr(DI_MC_22LVL1,  pd_win_prop[1].win_22lvl  |           		// region 1
                     (pd_win_prop[2].win_22lvl << 16));   					// region 2.

    Wr(DI_MC_22LVL2, pd_win_prop[3].win_22lvl  |           		// region 3
                     (pd_win_prop[4].win_22lvl << 16));   					// region 4.
    Wr(DI_MC_CTRL, 0x1f);            				// enable region level
}

static int fdn[5] = {0};
void read_pulldown_info(pulldown_detect_info_t* field_pd_info,
                        pulldown_detect_info_t* win_pd_info)
{
    int i;
    unsigned long pd_info[6];
    unsigned long tmp;
    Wr(DI_INFO_ADDR, 0 );
    for ( i  = 0; i < 6; i++)
    {
       	pd_info[i] = Rd(DI_INFO_DATA);
    }
    memset(field_pd_info, 0, sizeof(pulldown_detect_info_t));
    field_pd_info->field_diff       = pd_info[2];
    field_pd_info->field_diff_num   = pd_info[4]&0xffffff;
    field_pd_info->frame_diff       = pd_info[0];
    field_pd_info->frame_diff_num   = pd_info[1]&0xffffff;

    fdn[0] = fdn[1];
    fdn[1] = fdn[2];
    fdn[2] = fdn[3];
    fdn[3] = fdn[4];
    fdn[4] = field_pd_info->frame_diff_num;
    //if (fdn[0] || fdn[1] || fdn[2] || fdn[3] || fdn[4])
    if(frame_dynamic_dbg)
		printk("\n fdn[4]= %x",fdn[4]);
	if(frame_dynamic_level == 0)
		fdn[4] = fdn[4]&0xffff00;
	else if(frame_dynamic_level == 1)
		fdn[4] = fdn[4]&0xfffe00;
	else if(frame_dynamic_level == 2)
		fdn[4] = fdn[4]&0xfffc00;
	else if(frame_dynamic_level == 3)
		fdn[4] = fdn[4]&0xfff800;
	else if(frame_dynamic_level == 4)
		fdn[4] = fdn[4]&0xfff000;
    else
		fdn[4] = fdn[4]&0xffff00;
	if ((fdn[0]&0xffff00) || fdn[1] || fdn[2] || fdn[3] || fdn[4])
        frame_dynamic = true;
    else
        frame_dynamic = false;

    for(i = 0; i< MAX_WIN_NUM; i++){
        memset(&(win_pd_info[i]), 0, sizeof(pulldown_detect_info_t));
    }
    for(i = 0; i< MAX_WIN_NUM; i++){
        win_pd_info[i].frame_diff = Rd(DI_INFO_DATA);
    }
    for(i = 0; i< MAX_WIN_NUM; i++){
        win_pd_info[i].field_diff = Rd(DI_INFO_DATA);
    }
    for(i = 0; i< MAX_WIN_NUM; i++){
        tmp = Rd(DI_INFO_DATA); /* luma */
    }
    for(i = 0; i< MAX_WIN_NUM; i++){
        win_pd_info[i].frame_diff_num = Rd(DI_INFO_DATA)&0xffffff;
    }
    for(i = 0; i< MAX_WIN_NUM; i++){
        win_pd_info[i].field_diff_num = (Rd(DI_INFO_DATA)&0xfffff)<<4;
    }
}


void read_mtn_info(unsigned long* mtn_info, unsigned long * reg_mtn_info)
{
    int i;

    Wr(DI_INFO_ADDR, 64 );
    for ( i  = 0; i < 5; i++)
    {
       	mtn_info[i] = Rd(DI_INFO_DATA);
		    if(di_log_flag&DI_LOG_MTNINFO){
		        di_print("mtn_info[%d]=%lx \n", 64+i,mtn_info[i]);
		    }
    }
	reg_mtn_info[0] = mtn_info[0];
	reg_mtn_info[1] = mtn_info[1];
	reg_mtn_info[2] = mtn_info[2];
	reg_mtn_info[3] = mtn_info[3];
	reg_mtn_info[4] = mtn_info[4];

    Wr(DI_INFO_ADDR, 0 );
    reg_mtn_info[5] = Rd(DI_INFO_DATA);
	reg_mtn_info[6] = Rd(DI_INFO_DATA);



    return;
}
void di_post_read_reverse(bool reverse)
{
#if ((MESON_CPU_TYPE ==  MESON_CPU_TYPE_MESON6TV)||	(MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TVD	))
    if(reverse) {
        Wr_reg_bits(DI_IF1_GEN_REG2,    3, 2, 2);
        Wr_reg_bits(VD1_IF0_GEN_REG2, 0xf, 2, 4);
        Wr_reg_bits(VD2_IF0_GEN_REG2, 0xf, 2, 4);
    } else {
        Wr_reg_bits(DI_IF1_GEN_REG2,  0, 2, 2);
	Wr_reg_bits(VD1_IF0_GEN_REG2, 0, 2, 4);
	Wr_reg_bits(VD2_IF0_GEN_REG2, 0, 2, 4);
    }
#endif    
}
void di_post_read_reverse_irq(bool reverse)
{
#if ((MESON_CPU_TYPE ==  MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TVD	))
    if(reverse) {
        VSYNC_WR_MPEG_REG_BITS(DI_IF1_GEN_REG2,    3, 2, 2);
        VSYNC_WR_MPEG_REG_BITS(VD1_IF0_GEN_REG2, 0xf, 2, 4);
        VSYNC_WR_MPEG_REG_BITS(VD2_IF0_GEN_REG2, 0xf, 2, 4);
	VSYNC_WR_MPEG_REG_BITS(DI_MTNRD_CTRL, 0xf, 17,4);
    } else {
        VSYNC_WR_MPEG_REG_BITS(DI_IF1_GEN_REG2,  0, 2, 2);
	VSYNC_WR_MPEG_REG_BITS(VD1_IF0_GEN_REG2, 0, 2, 4);
	VSYNC_WR_MPEG_REG_BITS(VD2_IF0_GEN_REG2, 0, 2, 4);
	VSYNC_WR_MPEG_REG_BITS(DI_MTNRD_CTRL, 0, 17,4);
    }
#endif    
}

static unsigned char pre_power_on = 0;
//static unsigned char post_power_on = 0;
void di_set_power_control(unsigned char type, unsigned char enable)
{
		if(di_debug_flag&0x20){
		    return;
		}
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(type==0){
        //WRITE_MPEG_REG_BITS(HHI_VPU_MEM_PD_REG0, enable?0:3, 26, 2); //di pre
        switch_vpu_mem_pd_vmod(VPU_DI_PRE, enable?VPU_MEM_POWER_ON:VPU_MEM_POWER_DOWN);
        pre_power_on = enable;
    }
    else{
        //WRITE_MPEG_REG_BITS(HHI_VPU_MEM_PD_REG0, enable?0:3, 28, 2); //di post
#if 0
        switch_vpu_mem_pd_vmod(VPU_DI_POST, enable?VPU_MEM_POWER_ON:VPU_MEM_POWER_DOWN);
        post_power_on = enable;
#else
//let video.c handle it
#endif
    }
#endif
}

unsigned char di_get_power_control(unsigned char type)
{
    if(type==0){
        return pre_power_on;
    }
    else{
#if 1
//let video.c handle it
        return 1;
#else
        return post_power_on;
#endif
    }

}

void di_load_nr_setting()
{
#ifdef NEW_DI_V1
    Wr(NR2_3DEN_MODE, 0x77);
    Wr(NR2_SNR_SAD_CFG, 0x134f);
    Wr(NR2_MATNR_SNR_NRM_GAIN, 0x0);
    Wr(NR2_MATNR_SNR_LPF_CFG, 0xc1b86);
    Wr(NR2_MATNR_SNR_USF_GAIN, 0x404);
    Wr(NR2_MATNR_SNR_EDGE2B, 0xff08);
    Wr(NR2_MATNR_BETA_EGAIN, 0x4040);
    Wr(NR2_MATNR_YBETA_SCL, 0xff2000);
    Wr(NR2_MATNR_CBETA_SCL, 0xff2000);
    Wr(NR2_MATNR_MTN_CRTL2, 0x32020);
    Wr(NR2_MATNR_MTN_GAIN, 0xffffffff);
    Wr(NR2_MATNR_DEGHOST, 0x133);
#endif    
}

#ifdef DI_POST_SKIP_LINE
MODULE_PARM_DESC(di_vscale_skip_mode, "\n di_vscale_skip_mode\n");
module_param(di_vscale_skip_mode, uint, 0664);

MODULE_PARM_DESC(l_luma0_rpt_loop_start, "\n l_luma0_rpt_loop_start\n");
module_param(l_luma0_rpt_loop_start, uint, 0664);

MODULE_PARM_DESC(l_luma0_rpt_loop_end, "\n l_luma0_rpt_loop_end\n");
module_param(l_luma0_rpt_loop_end, uint, 0664);

MODULE_PARM_DESC(l_chroma0_rpt_loop_start, "\n l_chroma0_rpt_loop_start\n");
module_param(l_chroma0_rpt_loop_start, uint, 0664);

MODULE_PARM_DESC(l_chroma0_rpt_loop_end, "\n l_chroma0_rpt_loop_end\n");
module_param(l_chroma0_rpt_loop_end, uint, 0664);

MODULE_PARM_DESC(l_luma0_rpt_loop_pat, "\n l_luma0_rpt_loop_pat\n");
module_param(l_luma0_rpt_loop_pat, uint, 0664);

MODULE_PARM_DESC(l_chroma0_rpt_loop_pat, "\n l_chroma0_rpt_loop_pat\n");
module_param(l_chroma0_rpt_loop_pat, uint, 0664);
#endif
