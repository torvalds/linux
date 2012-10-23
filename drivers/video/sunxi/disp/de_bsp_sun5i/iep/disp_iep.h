#ifndef __DISP_IEP_H__
#define __DISP_IEP_H__

#include "../de/disp_display.h"
#include "de_iep.h"
#include "../de/disp_event.h"

#define CLK_IEP_AHB_ON      0x00000008
#define CLK_IEP_MOD_ON 		0x00000080
#define CLK_IEP_DRAM_ON     0x00000800

#define CLK_IEP_AHB_OFF		(~(CLK_IEP_AHB_ON	    ))
#define CLK_IEP_MOD_OFF 	(~(CLK_IEP_MOD_ON	    ))
#define CLK_IEP_DRAM_OFF 	(~(CLK_IEP_DRAM_ON	    ))

#define DE_FLICKER_USED 				0x01000000
#define DE_FLICKER_USED_MASK 			(~(DE_FLICKER_USED))
#define DE_FLICKER_REQUIRED 			0x02000000
#define DE_FLICKER_REQUIRED_MASK 		(~(DE_FLICKER_REQUIRED))
#define DRC_USED 						0x04000000
#define DRC_USED_MASK 					(~(DRC_USED))
#define DRC_REQUIRED					0x08000000
#define DRC_REQUIRED_MASK 				(~(DRC_REQUIRED))
#define DE_FLICKER_NEED_CLOSED 			0x10000000
#define DE_FLICKER_NEED_CLOSED_MASK 	(~(DE_FLICKER_NEED_CLOSED))
#define DRC_NEED_CLOSED 				0x20000000
#define DRC_NEED_CLOSED_MASK			(~(DRC_NEED_CLOSED))

//for power saving mode alg0
#define IEP_LH_PWRSV_NUM 24

typedef struct
{
	__u32 			mod;

	//drc
	//__u32           drc_en;
	__u32  			drc_win_en;
	__disp_rect_t   drc_win;
	__u32           adjust_en;
	__u32           lgc_autoload_dis;
	__u32           waitframe;
	__u32           runframe;
	__u32           valid_width;
	__u32           valid_height;

	//lh
	__u32           lgc_base_add;
	__u8            lh_thres_val[IEP_LH_THRES_NUM];

	//de-flicker
	//__u32           deflicker_en;
	__u32           deflicker_win_en;
	__disp_rect_t   deflicker_win;

}__disp_iep_t;

typedef struct
{
	__u8 			min_adj_index_hist[IEP_LH_PWRSV_NUM];
	__u32           user_bl;
}__disp_pwrsv_t;

extern __s32 Disp_iep_init(__u32 sel);
extern __s32 Disp_iep_exit(__u32 sel);
extern __s32 Disp_drc_enable(__u32 sel, __u32 en);
extern __s32 Disp_drc_init(__u32 sel);
extern __s32 Disp_de_flicker_enable(__u32 sel, __u32 en);
extern __s32 Disp_de_flicker_init(__u32 sel);
extern __s32 iep_clk_init(__u32 sel);
extern __s32 iep_clk_exit(__u32 sel);
extern __s32 iep_clk_open(__u32 sel);
extern __s32 iep_clk_close(__u32 sel);
extern __s32 IEP_Operation_In_Vblanking(__u32 sel, __u32 tcon_index);
extern __s32 Disp_drc_proc(__u32 sel, __u32 tcon_index);
extern __s32 Disp_drc_close_proc(__u32 sel, __u32 tcon_index);
extern __s32 Disp_de_flicker_proc(__u32 sel, __u32 tcon_index);
extern __s32 Disp_de_flicker_close_proc(__u32 sel, __u32 tcon_index);

extern __s32 DE_IEP_Set_Reg_Base(__u32 sel, __u32 base);

#endif

