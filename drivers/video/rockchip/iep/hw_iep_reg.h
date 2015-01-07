#ifndef IEP_REGS_H
#define IEP_REGS_H
#include "hw_iep_config_addr.h"
#include "iep.h"
#include "iep_drv.h"

struct iep_status {
	uint32_t reserved0   : 1;
	uint32_t scl_sts     : 1;
	uint32_t dil_sts     : 1;
	uint32_t reserved1   : 1;
	uint32_t wyuv_sts    : 1;
	uint32_t ryuv_sts    : 1;
	uint32_t wrgb_sts    : 1;
	uint32_t rrgb_sts    : 1;
	uint32_t voi_sts     : 1;
};

#if defined(CONFIG_IEP_MMU)
struct iep_mmu_status {
	uint32_t paging_enabled         : 1;
	uint32_t page_fault_active      : 1;
	uint32_t stall_active           : 1;
	uint32_t idle                   : 1;
	uint32_t replay_buffer_empty    : 1;
	uint32_t page_fault_is_write    : 1;
	uint32_t page_fault_bus_id      : 5;
};

struct iep_mmu_int_status {
	uint32_t page_fault     : 1;
	uint32_t read_bus_error : 1;
};

enum iep_mmu_cmd {
	MMU_ENABLE_PAGING,
	MMU_DISABLE_PAGING,
	MMU_ENABLE_STALL,
	MMU_DISABLE_STALL,
	MMU_ZAP_CACHE,
	MMU_PAGE_FAULT_DONE,
	MMU_FORCE_RESET
};
#endif

#define      rIEP_CONFIG0      		         (IEP_BASE+IEP_CONFIG0)
#define      rIEP_CONFIG1      		         (IEP_BASE+IEP_CONFIG1)

#define      rIEP_STATUS              	     (IEP_BASE+IEP_STATUS)
#define      rIEP_INT                 	     (IEP_BASE+IEP_INT)
#define      rIEP_FRM_START         	     (IEP_BASE+IEP_FRM_START)
#define      rIEP_SOFT_RST           	     (IEP_BASE+IEP_SOFT_RST)
#define      rIEP_CONF_DONE                  (IEP_BASE+IEP_CONF_DONE)

#define      rIEP_VIR_IMG_WIDTH        	     (IEP_BASE+IEP_VIR_IMG_WIDTH)

#define      rIEP_IMG_SCL_FCT         	     (IEP_BASE+IEP_IMG_SCL_FCT)

#define      rIEP_SRC_IMG_SIZE         	     (IEP_BASE+IEP_SRC_IMG_SIZE)
#define      rIEP_DST_IMG_SIZE         	     (IEP_BASE+IEP_DST_IMG_SIZE)

#define      rIEP_DST_IMG_WIDTH_TILE0  	     (IEP_BASE+IEP_DST_IMG_WIDTH_TILE0)
#define      rIEP_DST_IMG_WIDTH_TILE1  	     (IEP_BASE+IEP_DST_IMG_WIDTH_TILE1)
#define      rIEP_DST_IMG_WIDTH_TILE2  	     (IEP_BASE+IEP_DST_IMG_WIDTH_TILE2)
#define      rIEP_DST_IMG_WIDTH_TILE3  	     (IEP_BASE+IEP_DST_IMG_WIDTH_TILE3)

#define      rIEP_ENH_YUV_CNFG_0       	     (IEP_BASE+IEP_ENH_YUV_CNFG_0)
#define      rIEP_ENH_YUV_CNFG_1       	     (IEP_BASE+IEP_ENH_YUV_CNFG_1)
#define      rIEP_ENH_YUV_CNFG_2       	     (IEP_BASE+IEP_ENH_YUV_CNFG_2)
#define      rIEP_ENH_RGB_CNFG        	     (IEP_BASE+IEP_ENH_RGB_CNFG)
#define      rIEP_ENH_C_COE            	     (IEP_BASE+IEP_ENH_C_COE)

#define      rIEP_SRC_ADDR_YRGB        	     (IEP_BASE+IEP_SRC_ADDR_YRGB)
#define      rIEP_SRC_ADDR_CBCR              (IEP_BASE+IEP_SRC_ADDR_CBCR)
#define      rIEP_SRC_ADDR_CR                (IEP_BASE+IEP_SRC_ADDR_CR)
#define      rIEP_SRC_ADDR_Y1                (IEP_BASE+IEP_SRC_ADDR_Y1)
#define      rIEP_SRC_ADDR_CBCR1             (IEP_BASE+IEP_SRC_ADDR_CBCR1)
#define      rIEP_SRC_ADDR_CR1               (IEP_BASE+IEP_SRC_ADDR_CR1)
#define      rIEP_SRC_ADDR_Y_ITEMP           (IEP_BASE+IEP_SRC_ADDR_Y_ITEMP)
#define      rIEP_SRC_ADDR_CBCR_ITEMP        (IEP_BASE+IEP_SRC_ADDR_CBCR_ITEMP)
#define      rIEP_SRC_ADDR_CR_ITEMP          (IEP_BASE+IEP_SRC_ADDR_CR_ITEMP)
#define      rIEP_SRC_ADDR_Y_FTEMP           (IEP_BASE+IEP_SRC_ADDR_Y_FTEMP)
#define      rIEP_SRC_ADDR_CBCR_FTEMP        (IEP_BASE+IEP_SRC_ADDR_CBCR_FTEMP)
#define      rIEP_SRC_ADDR_CR_FTEMP          (IEP_BASE+IEP_SRC_ADDR_CR_FTEMP)

#define      rIEP_DST_ADDR_YRGB        	     (IEP_BASE+IEP_DST_ADDR_YRGB)
#define      rIEP_DST_ADDR_CBCR              (IEP_BASE+IEP_DST_ADDR_CBCR)
#define      rIEP_DST_ADDR_CR                (IEP_BASE+IEP_DST_ADDR_CR)
#define      rIEP_DST_ADDR_Y1                (IEP_BASE+IEP_DST_ADDR_Y1)
#define      rIEP_DST_ADDR_CBCR1             (IEP_BASE+IEP_DST_ADDR_CBCR1)
#define      rIEP_DST_ADDR_CR1               (IEP_BASE+IEP_DST_ADDR_CR1)
#define      rIEP_DST_ADDR_Y_ITEMP           (IEP_BASE+IEP_DST_ADDR_Y_ITEMP)
#define      rIEP_DST_ADDR_CBCR_ITEMP        (IEP_BASE+IEP_DST_ADDR_CBCR_ITEMP)
#define      rIEP_DST_ADDR_CR_ITEMP          (IEP_BASE+IEP_DST_ADDR_CR_ITEMP)
#define      rIEP_DST_ADDR_Y_FTEMP           (IEP_BASE+IEP_DST_ADDR_Y_FTEMP)
#define      rIEP_DST_ADDR_CBCR_FTEMP        (IEP_BASE+IEP_DST_ADDR_CBCR_FTEMP)
#define      rIEP_DST_ADDR_CR_FTEMP          (IEP_BASE+IEP_DST_ADDR_CR_FTEMP)

#define      rIEP_DIL_MTN_TAB0               (IEP_BASE+IEP_DIL_MTN_TAB0)
#define      rIEP_DIL_MTN_TAB1               (IEP_BASE+IEP_DIL_MTN_TAB1)
#define      rIEP_DIL_MTN_TAB2               (IEP_BASE+IEP_DIL_MTN_TAB2)
#define      rIEP_DIL_MTN_TAB3               (IEP_BASE+IEP_DIL_MTN_TAB3)
#define      rIEP_DIL_MTN_TAB4               (IEP_BASE+IEP_DIL_MTN_TAB4)
#define      rIEP_DIL_MTN_TAB5               (IEP_BASE+IEP_DIL_MTN_TAB5)
#define      rIEP_DIL_MTN_TAB6               (IEP_BASE+IEP_DIL_MTN_TAB6)
#define      rIEP_DIL_MTN_TAB7               (IEP_BASE+IEP_DIL_MTN_TAB7)

#define      rIEP_ENH_CG_TAB                 (IEP_BASE+IEP_ENH_CG_TAB)

#define      rIEP_YUV_DNS_CRCT_TEMP          (IEP_BASE+IEP_YUV_DNS_CRCT_TEMP)
#define      rIEP_YUV_DNS_CRCT_SPAT          (IEP_BASE+IEP_YUV_DNS_CRCT_SPAT)

#define      rIEP_ENH_DDE_COE0               (IEP_BASE+IEP_ENH_DDE_COE0)
#define      rIEP_ENH_DDE_COE1               (IEP_BASE+IEP_ENH_DDE_COE1)

#define      RAW_rIEP_CONFIG0                (IEP_BASE+RAW_IEP_CONFIG0)
#define      RAW_rIEP_CONFIG1      		     (IEP_BASE+RAW_IEP_CONFIG1)
#define      RAW_rIEP_VIR_IMG_WIDTH          (IEP_BASE+RAW_IEP_VIR_IMG_WIDTH)

#define      RAW_rIEP_IMG_SCL_FCT      	     (IEP_BASE+RAW_IEP_IMG_SCL_FCT)

#define      RAW_rIEP_SRC_IMG_SIZE      	 (IEP_BASE+RAW_IEP_SRC_IMG_SIZE)
#define      RAW_rIEP_DST_IMG_SIZE      	 (IEP_BASE+RAW_IEP_DST_IMG_SIZE)

#define      RAW_rIEP_ENH_YUV_CNFG_0         (IEP_BASE+RAW_IEP_ENH_YUV_CNFG_0)
#define      RAW_rIEP_ENH_YUV_CNFG_1         (IEP_BASE+RAW_IEP_ENH_YUV_CNFG_1)
#define      RAW_rIEP_ENH_YUV_CNFG_2         (IEP_BASE+RAW_IEP_ENH_YUV_CNFG_2)
#define      RAW_rIEP_ENH_RGB_CNFG           (IEP_BASE+RAW_IEP_ENH_RGB_CNFG)

#define      rIEP_CG_TAB_ADDR                 (IEP_BASE+0x0100)

#if defined(CONFIG_IEP_MMU)
#define      rIEP_MMU_BASE                    0x0800
#define      rIEP_MMU_DTE_ADDR                (IEP_MMU_BASE+0x00)
#define      rIEP_MMU_STATUS                  (IEP_MMU_BASE+0x04)
#define      rIEP_MMU_CMD                     (IEP_MMU_BASE+0x08)
#define      rIEP_MMU_PAGE_FAULT_ADDR         (IEP_MMU_BASE+0x0c)
#define      rIEP_MMU_ZAP_ONE_LINE            (IEP_MMU_BASE+0x10)
#define      rIEP_MMU_INT_RAWSTAT             (IEP_MMU_BASE+0x14)
#define      rIEP_MMU_INT_CLEAR               (IEP_MMU_BASE+0x18)
#define      rIEP_MMU_INT_MASK                (IEP_MMU_BASE+0x1c)
#define      rIEP_MMU_INT_STATUS              (IEP_MMU_BASE+0x20)
#define      rIEP_MMU_AUTO_GATING             (IEP_MMU_BASE+0x24)
#endif

/*-----------------------------------------------------------------
//reg bit operation definition
-----------------------------------------------------------------*/
/*iep_config0*/
#define     IEP_REGB_V_REVERSE_DISP_Z(x)      (((x)&0x1 ) << 31 )
#define     IEP_REGB_H_REVERSE_DISP_Z(x)      (((x)&0x1 ) << 30 )
#define     IEP_REGB_SCL_EN_Z(x)              (((x)&0x1 ) << 28 )
#define     IEP_REGB_SCL_SEL_Z(x)             (((x)&0x3 ) << 26 )
#define     IEP_REGB_SCL_UP_COE_SEL_Z(x)      (((x)&0x3 ) << 24 )
#define     IEP_REGB_DIL_EI_SEL_Z(x)          (((x)&0x1 ) << 23 )
#define     IEP_REGB_DIL_EI_RADIUS_Z(x)       (((x)&0x3 ) << 21 )
#define     IEP_REGB_CON_GAM_ORDER_Z(x)       (((x)&0x1 ) << 20 )
#define     IEP_REGB_RGB_ENH_SEL_Z(x)         (((x)&0x3 ) << 18 )
#define     IEP_REGB_RGB_CON_GAM_EN_Z(x)      (((x)&0x1 ) << 17 )
#define     IEP_REGB_RGB_COLOR_ENH_EN_Z(x)    (((x)&0x1 ) << 16 )
#define     IEP_REGB_DIL_EI_SMOOTH_Z(x)       (((x)&0x1 ) << 15 )
#define     IEP_REGB_YUV_ENH_EN_Z(x)          (((x)&0x1 ) << 14 )
#define     IEP_REGB_YUV_DNS_EN_Z(x)          (((x)&0x1 ) << 13 )
#define     IEP_REGB_DIL_EI_MODE_Z(x)         (((x)&0x1 ) << 12 )
#define     IEP_REGB_DIL_HF_EN_Z(x)           (((x)&0x1 ) << 11 )
#define     IEP_REGB_DIL_MODE_Z(x)            (((x)&0x7 ) << 8  )
#define     IEP_REGB_DIL_HF_FCT_Z(x)          (((x)&0x7F) << 1  )
#define     IEP_REGB_LCDC_PATH_EN_Z(x)        (((x)&0x1 ) << 0  )

/*iep_conig1*/
#define     IEP_REGB_GLB_ALPHA_Z(x)           (((x)&0xff) << 24 )
#define     IEP_REGB_RGB2YUV_INPUT_CLIP_Z(x)  (((x)&0x1 ) << 23 )
#define     IEP_REGB_YUV2RGB_INPUT_CLIP_Z(x)  (((x)&0x1 ) << 22 )
#define     IEP_REGB_RGB_TO_YUV_EN_Z(x)       (((x)&0x1 ) << 21 )
#define     IEP_REGB_YUV_TO_RGB_EN_Z(x)       (((x)&0x1 ) << 20 )
#define     IEP_REGB_RGB2YUV_COE_SEL_Z(x)     (((x)&0x3 ) << 18 )
#define     IEP_REGB_YUV2RGB_COE_SEL_Z(x)     (((x)&0x3 ) << 16 )
#define     IEP_REGB_DITHER_DOWN_EN_Z(x)      (((x)&0x1 ) << 15 )
#define     IEP_REGB_DITHER_UP_EN_Z(x)        (((x)&0x1 ) << 14 )
#define     IEP_REGB_DST_YUV_SWAP_Z(x)        (((x)&0x3 ) << 12 )
#define     IEP_REGB_DST_RGB_SWAP_Z(x)        (((x)&0x3 ) << 10 )
#define     IEP_REGB_DST_FMT_Z(x)             (((x)&0x3 ) << 8  )
#define     IEP_REGB_SRC_YUV_SWAP_Z(x)        (((x)&0x3 ) << 4  )
#define     IEP_REGB_SRC_RGB_SWAP_Z(x)        (((x)&0x3 ) << 2  )
#define     IEP_REGB_SRC_FMT_Z(x)             (((x)&0x3 ) << 0  )

/*iep_int*/
#define     IEP_REGB_FRAME_END_INT_CLR_Z(x)   (((x)&0x1 ) << 16 )
#define     IEP_REGB_FRAME_END_INT_EN_Z(x)    (((x)&0x1 ) << 8  )

/*frm_start*/
#define     IEP_REGB_FRM_START_Z(x)           (((x)&0x01 ) << 0 )

/*soft_rst*/
#define     IEP_REGB_SOFT_RST_Z(x)            (((x)&0x01 ) << 0 )

/*iep_vir_img_width*/
#define     IEP_REGB_DST_VIR_LINE_WIDTH_Z(x)  (((x)&0xffff) << 16 )
#define     IEP_REGB_SRC_VIR_LINE_WIDTH_Z(x)  (((x)&0xffff) << 0  )

/*iep_img_scl_fct*/
#define     IEP_REGB_SCL_VRT_FCT_Z(x)         (((x)&0xffff) << 16 )
#define     IEP_REGB_SCL_HRZ_FCT_Z(x)         (((x)&0xffff) << 0  )

/*iep_src_img_size*/
#define     IEP_REGB_SRC_IMG_HEIGHT_Z(x)      (((x)&0x1fff) << 16 )
#define     IEP_REGB_SRC_IMG_WIDTH_Z(x)       (((x)&0x1fff) << 0  )
/*iep_dst_img_size*/
#define     IEP_REGB_DST_IMG_HEIGHT_Z(x)      (((x)&0x1fff) << 16 )
#define     IEP_REGB_DST_IMG_WIDTH_Z(x)       (((x)&0x1fff) << 0  )

/*dst_img_width_tile0/1/2/3*/
#define     IEP_REGB_DST_IMG_WIDTH_TILE0_Z(x) (((x)&0x3ff ) << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE1_Z(x) (((x)&0x3ff ) << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE2_Z(x) (((x)&0x3ff ) << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE3_Z(x) (((x)&0x3ff ) << 0  )

/*iep_enh_yuv_cnfg0*/
#define     IEP_REGB_SAT_CON_Z(x)             (((x)&0x1ff ) << 16 )
#define     IEP_REGB_CONTRAST_Z(x)            (((x)&0xff ) <<  8  )
#define     IEP_REGB_BRIGHTNESS_Z(x)          (((x)&0x3f ) <<  0  )
/*iep_enh_yuv_cnfg1*/
#define     IEP_REGB_COS_HUE_Z(x)             (((x)&0xff ) <<  8  )
#define     IEP_REGB_SIN_HUE_Z(x)             (((x)&0xff ) <<  0  )
/*iep_enh_yuv_cnfg2*/
#define     IEP_REGB_VIDEO_MODE_Z(x)          (((x)&0x3  ) <<  24 )
#define     IEP_REGB_COLOR_BAR_V_Z(x)         (((x)&0xff ) <<  16 )
#define     IEP_REGB_COLOR_BAR_U_Z(x)         (((x)&0xff ) <<  8  )
#define     IEP_REGB_COLOR_BAR_Y_Z(x)         (((x)&0xff ) <<  0  )
/*iep_enh_rgb_cnfg*/
#define     IEP_REGB_YUV_DNS_LUMA_SPAT_SEL_Z(x)   (((x)&0x3  ) <<  30 )
#define     IEP_REGB_YUV_DNS_LUMA_TEMP_SEL_Z(x)   (((x)&0x3  ) <<  28 )
#define     IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL_Z(x) (((x)&0x3  ) <<  26 )
#define     IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL_Z(x) (((x)&0x3  ) <<  24 )
#define     IEP_REGB_ENH_THRESHOLD_Z(x)       (((x)&0xff ) <<  16 )
#define     IEP_REGB_ENH_ALPHA_Z(x)           (((x)&0x3f ) <<  8  )
#define     IEP_REGB_ENH_RADIUS_Z(x)          (((x)&0x3  ) <<  0  )
/*iep_enh_c_coe*/
#define     IEP_REGB_ENH_C_COE_Z(x)           (((x)&0x7f ) <<  0  )
/*dil_mtn_tab*/
#define     IEP_REGB_DIL_MTN_TAB0_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB0_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB0_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB0_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB1_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB1_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB1_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB1_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB2_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB2_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB2_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB2_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB3_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB3_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB3_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB3_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB4_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB4_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB4_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB4_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB5_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB5_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB5_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB5_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB6_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB6_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB6_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB6_3_Z(x)      (((x)&0x7f ) <<  24 )

#define     IEP_REGB_DIL_MTN_TAB7_0_Z(x)      (((x)&0x7f ) <<  0  )
#define     IEP_REGB_DIL_MTN_TAB7_1_Z(x)      (((x)&0x7f ) <<  8  )
#define     IEP_REGB_DIL_MTN_TAB7_2_Z(x)      (((x)&0x7f ) <<  16 )
#define     IEP_REGB_DIL_MTN_TAB7_3_Z(x)      (((x)&0x7f ) <<  24 )

#if defined(CONFIG_IEP_MMU)
/*mmu*/
#define     IEP_REGB_MMU_STATUS_PAGING_ENABLE_Z(x)              (((x)&0x01) << 0)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_ACTIVE_Z(x)          (((x)&0x01) << 1)
#define     IEP_REGB_MMU_STATUS_STALL_ACTIVE_Z(x)               (((x)&0x01) << 2)
#define     IEP_REGB_MMU_STATUS_IDLE_Z(x)                       (((x)&0x01) << 3)
#define     IEP_REGB_MMU_STATUS_REPLAY_BUFFER_EMPTY_Z(x)        (((x)&0x01) << 4)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_IS_WRITE_Z(x)        (((x)&0x01) << 5)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_BUS_ID_Z(x)          (((x)&0x1F) << 6)

#define     IEP_REGB_MMU_CMD_Z(x)                               (((x)&0x07) << 0)

#define     IEP_REGB_MMU_ZAP_ONE_LINE_Z(x)                      (((x)&0x01) << 0)

#define     IEP_REGB_MMU_INT_RAWSTAT_PAGE_FAULT_Z(x)            (((x)&0x01) << 0)
#define     IEP_REGB_MMU_INT_RAWSTAT_READ_BUS_ERROR_Z(x)        (((x)&0x01) << 1)

#define     IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR_Z(x)        (((x)&0x01) << 0)
#define     IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR_Z(x)    (((x)&0x01) << 1)

#define     IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN_Z(x)        (((x)&0x01) << 0)
#define     IEP_REGB_MMU_INT_MASK_READ_BUS_ERROR_INT_EN_Z(x)    (((x)&0x01) << 1)

#define     IEP_REGB_MMU_INT_STATUS_PAGE_FAULT_Z(x)             (((x)&0x01) << 0)
#define     IEP_REGB_MMU_INT_STATUS_READ_BUS_ERROR_Z(x)         (((x)&0x01) << 1)

#define     IEP_REGB_MMU_AUTO_GATING_Z(x)                       (((x)&0x01) << 0)
#endif

/*iep_config0*/
#define     IEP_REGB_V_REVERSE_DISP_Y      (0x1  << 31 )
#define     IEP_REGB_H_REVERSE_DISP_Y      (0x1  << 30 )
#define     IEP_REGB_SCL_EN_Y              (0x1  << 28 )
#define     IEP_REGB_SCL_SEL_Y             (0x3  << 26 )
#define     IEP_REGB_SCL_UP_COE_SEL_Y      (0x3  << 24 )
#define     IEP_REGB_DIL_EI_SEL_Y          (0x1  << 23 )
#define     IEP_REGB_DIL_EI_RADIUS_Y       (0x3  << 21 )
#define     IEP_REGB_CON_GAM_ORDER_Y       (0x1  << 20 )
#define     IEP_REGB_RGB_ENH_SEL_Y         (0x3  << 18 )
#define     IEP_REGB_RGB_CON_GAM_EN_Y      (0x1  << 17 )
#define     IEP_REGB_RGB_COLOR_ENH_EN_Y    (0x1  << 16 )
#define     IEP_REGB_DIL_EI_SMOOTH_Y       (0x1  << 15 )
#define     IEP_REGB_YUV_ENH_EN_Y          (0x1  << 14 )
#define     IEP_REGB_YUV_DNS_EN_Y          (0x1  << 13 )
#define     IEP_REGB_DIL_EI_MODE_Y         (0x1  << 12 )
#define     IEP_REGB_DIL_HF_EN_Y           (0x1  << 11 )
#define     IEP_REGB_DIL_MODE_Y            (0x7  << 8  )
#define     IEP_REGB_DIL_HF_FCT_Y          (0x7F << 1  )
#define     IEP_REGB_LCDC_PATH_EN_Y        (0x1  << 0  )

/*iep_conig1*/
#define     IEP_REGB_GLB_ALPHA_Y           (0xff << 24 )
#define     IEP_REGB_RGB2YUV_INPUT_CLIP_Y  (0x1  << 23 )
#define     IEP_REGB_YUV2RGB_INPUT_CLIP_Y  (0x1  << 22 )
#define     IEP_REGB_RGB_TO_YUV_EN_Y       (0x1  << 21 )
#define     IEP_REGB_YUV_TO_RGB_EN_Y       (0x1  << 20 )
#define     IEP_REGB_RGB2YUV_COE_SEL_Y     (0x3  << 18 )
#define     IEP_REGB_YUV2RGB_COE_SEL_Y     (0x3  << 16 )
#define     IEP_REGB_DITHER_DOWN_EN_Y      (0x1  << 15 )
#define     IEP_REGB_DITHER_UP_EN_Y        (0x1  << 14 )
#define     IEP_REGB_DST_YUV_SWAP_Y        (0x3  << 12 )
#define     IEP_REGB_DST_RGB_SWAP_Y        (0x3  << 10 )
#define     IEP_REGB_DST_FMT_Y             (0x3  << 8  )
#define     IEP_REGB_SRC_YUV_SWAP_Y        (0x3  << 4  )
#define     IEP_REGB_SRC_RGB_SWAP_Y        (0x3  << 2  )
#define     IEP_REGB_SRC_FMT_Y             (0x3  << 0  )

/*iep_int*/
#define     IEP_REGB_FRAME_END_INT_CLR_Y   (0x1  << 16 )
#define     IEP_REGB_FRAME_END_INT_EN_Y    (0x1  << 8  )

/*frm_start*/
#define     IEP_REGB_FRM_START_Y           (0x1  << 0  )

/*soft_rst*/
#define     IEP_REGB_SOFT_RST_Y            (0x1  << 0  )

/*iep_vir_img_width*/
#define     IEP_REGB_DST_VIR_LINE_WIDTH_Y  (0xffff << 16 )
#define     IEP_REGB_SRC_VIR_LINE_WIDTH_Y  (0xffff << 0  )

/*iep_img_scl_fct*/
#define     IEP_REGB_SCL_VRT_FCT_Y         (0xffff << 16 )
#define     IEP_REGB_SCL_HRZ_FCT_Y         (0xffff << 0  )

/*iep_src_img_size*/
#define     IEP_REGB_SRC_IMG_HEIGHT_Y      (0x1fff << 16 )
#define     IEP_REGB_SRC_IMG_WIDTH_Y       (0x1fff << 0  )
/*iep_dst_img_size*/
#define     IEP_REGB_DST_IMG_HEIGHT_Y      (0x1fff << 16 )
#define     IEP_REGB_DST_IMG_WIDTH_Y       (0x1fff << 0  )

/*dst_img_width_tile0/1/2/3*/
#define     IEP_REGB_DST_IMG_WIDTH_TILE0_Y (0x3ff  << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE1_Y (0x3ff  << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE2_Y (0x3ff  << 0  )
#define     IEP_REGB_DST_IMG_WIDTH_TILE3_Y (0x3ff  << 0  )

/*iep_enh_yuv_cnfg0*/
#define     IEP_REGB_SAT_CON_Y             (0x1ff  <<  16)
#define     IEP_REGB_CONTRAST_Y            (0xff  <<  8 )
#define     IEP_REGB_BRIGHTNESS_Y          (0x3f  <<  0 )
/*iep_enh_yuv_cnfg1*/
#define     IEP_REGB_COS_HUE_Y             (0xff  <<  8 )
#define     IEP_REGB_SIN_HUE_Y             (0xff  <<  0 )
/*iep_enh_yuv_cnfg2*/
#define     IEP_REGB_VIDEO_MODE_Y          (0x3   <<  24)
#define     IEP_REGB_COLOR_BAR_V_Y         (0xff  <<  16)
#define     IEP_REGB_COLOR_BAR_U_Y         (0xff  <<  8 )
#define     IEP_REGB_COLOR_BAR_Y_Y         (0xff  <<  0 )
/*iep_enh_rgb_cnfg*/
#define     IEP_REGB_YUV_DNS_LUMA_SPAT_SEL_Y (0x3   <<  30)
#define     IEP_REGB_YUV_DNS_LUMA_TEMP_SEL_Y (0x3   <<  28)
#define     IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL_Y (0x3  <<  26)
#define     IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL_Y (0x3  <<  24)
#define     IEP_REGB_ENH_THRESHOLD_Y       (0xff  <<  16)
#define     IEP_REGB_ENH_ALPHA_Y           (0x3f  <<  8 )
#define     IEP_REGB_ENH_RADIUS_Y          (0x3   <<  0 )
/*iep_enh_c_coe*/
#define     IEP_REGB_ENH_C_COE_Y           (0x7f  <<  0 )
/*dil_mtn_tab*/
#define     IEP_REGB_DIL_MTN_TAB0_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB0_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB0_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB0_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB1_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB1_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB1_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB1_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB2_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB2_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB2_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB2_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB3_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB3_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB3_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB3_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB4_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB4_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB4_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB4_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB5_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB5_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB5_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB5_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB6_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB6_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB6_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB6_3_Y      (0x7f  <<  24 )

#define     IEP_REGB_DIL_MTN_TAB7_0_Y      (0x7f  <<  0  )
#define     IEP_REGB_DIL_MTN_TAB7_1_Y      (0x7f  <<  8  )
#define     IEP_REGB_DIL_MTN_TAB7_2_Y      (0x7f  <<  16 )
#define     IEP_REGB_DIL_MTN_TAB7_3_Y      (0x7f  <<  24 )

#if defined(CONFIG_IEP_MMU)
/*mmu*/
#define     IEP_REGB_MMU_STATUS_PAGING_ENABLE_Y              (0x01 << 0)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_ACTIVE_Y          (0x01 << 1)
#define     IEP_REGB_MMU_STATUS_STALL_ACTIVE_Y               (0x01 << 2)
#define     IEP_REGB_MMU_STATUS_IDLE_Y                       (0x01 << 3)
#define     IEP_REGB_MMU_STATUS_REPLAY_BUFFER_EMPTY_Y        (0x01 << 4)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_IS_WRITE_Y        (0x01 << 5)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_BUS_ID_Y          (0x1F << 6)

#define     IEP_REGB_MMU_CMD_Y                               (0x07 << 0)

#define     IEP_REGB_MMU_ZAP_ONE_LINE_Y                      (0x01 << 0)

#define     IEP_REGB_MMU_INT_RAWSTAT_PAGE_FAULT_Y            (0x01 << 0)
#define     IEP_REGB_MMU_INT_RAWSTAT_READ_BUS_ERROR_Y        (0x01 << 1)

#define     IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR_Y        (0x01 << 0)
#define     IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR_Y    (0x01 << 1)

#define     IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN_Y        (0x01 << 0)
#define     IEP_REGB_MMU_INT_MASK_READ_BUS_ERROR_INT_EN_Y    (0x01 << 1)

#define     IEP_REGB_MMU_INT_STATUS_PAGE_FAULT_Y             (0x01 << 0)
#define     IEP_REGB_MMU_INT_STATUS_READ_BUS_ERROR_Y         (0x01 << 1)

#define     IEP_REGB_MMU_AUTO_GATING_Y                       (0x01 << 0)

/*offset*/
#define     IEP_REGB_MMU_STATUS_PAGING_ENABLE_F              (0)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_ACTIVE_F          (1)
#define     IEP_REGB_MMU_STATUS_STALL_ACTIVE_F               (2)
#define     IEP_REGB_MMU_STATUS_IDLE_F                       (3)
#define     IEP_REGB_MMU_STATUS_REPLAY_BUFFER_EMPTY_F        (4)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_IS_WRITE_F        (5)
#define     IEP_REGB_MMU_STATUS_PAGE_FAULT_BUS_ID_F          (6)
#endif

/*-----------------------------------------------------------------
MaskRegBits32(addr, y, z),Register configure
-----------------------------------------------------------------*/
/*iep_config0*/
#define     IEP_REGB_V_REVERSE_DISP(base, x)      ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_V_REVERSE_DISP_Y,IEP_REGB_V_REVERSE_DISP_Z(x))
#define     IEP_REGB_H_REVERSE_DISP(base, x)      ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_H_REVERSE_DISP_Y,IEP_REGB_H_REVERSE_DISP_Z(x))
#define     IEP_REGB_SCL_EN(base, x)              ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_SCL_EN_Y,IEP_REGB_SCL_EN_Z(x))
#define     IEP_REGB_SCL_SEL(base, x)             ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_SCL_SEL_Y,IEP_REGB_SCL_SEL_Z(x))
#define     IEP_REGB_SCL_UP_COE_SEL(base, x)      ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_SCL_UP_COE_SEL_Y,IEP_REGB_SCL_UP_COE_SEL_Z(x))
#define     IEP_REGB_DIL_EI_SEL(base, x)          ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_EI_SEL_Y,IEP_REGB_DIL_EI_SEL_Z(x))
#define     IEP_REGB_DIL_EI_RADIUS(base, x)       ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_EI_RADIUS_Y,IEP_REGB_DIL_EI_RADIUS_Z(x))
#define     IEP_REGB_CON_GAM_ORDER(base, x)       ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_CON_GAM_ORDER_Y,IEP_REGB_CON_GAM_ORDER_Z(x))
#define     IEP_REGB_RGB_ENH_SEL(base, x)         ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_RGB_ENH_SEL_Y,IEP_REGB_RGB_ENH_SEL_Z(x))
#define     IEP_REGB_RGB_CON_GAM_EN(base, x)      ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_RGB_CON_GAM_EN_Y,IEP_REGB_RGB_CON_GAM_EN_Z(x))
#define     IEP_REGB_RGB_COLOR_ENH_EN(base, x)    ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_RGB_COLOR_ENH_EN_Y,IEP_REGB_RGB_COLOR_ENH_EN_Z(x))
#define     IEP_REGB_DIL_EI_SMOOTH(base, x)       ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_EI_SMOOTH_Y,IEP_REGB_DIL_EI_SMOOTH_Z(x))
#define     IEP_REGB_YUV_ENH_EN(base, x)          ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_YUV_ENH_EN_Y,IEP_REGB_YUV_ENH_EN_Z(x))
#define     IEP_REGB_YUV_DNS_EN(base, x)          ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_YUV_DNS_EN_Y,IEP_REGB_YUV_DNS_EN_Z(x))
#define     IEP_REGB_DIL_EI_MODE(base, x)         ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_EI_MODE_Y,IEP_REGB_DIL_EI_MODE_Z(x))
#define     IEP_REGB_DIL_HF_EN(base, x)           ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_HF_EN_Y,IEP_REGB_DIL_HF_EN_Z(x))
#define     IEP_REGB_DIL_MODE(base, x)            ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_MODE_Y,IEP_REGB_DIL_MODE_Z(x))
#define     IEP_REGB_DIL_HF_FCT(base, x)          ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_DIL_HF_FCT_Y,IEP_REGB_DIL_HF_FCT_Z(x))
#define     IEP_REGB_LCDC_PATH_EN(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG0,rIEP_CONFIG0,IEP_REGB_LCDC_PATH_EN_Y,IEP_REGB_LCDC_PATH_EN_Z(x))

/*iep_conig1*/
#define     IEP_REGB_GLB_ALPHA(base, x)           ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_GLB_ALPHA_Y,IEP_REGB_GLB_ALPHA_Z(x))
#define     IEP_REGB_RGB2YUV_INPUT_CLIP(base, x)  ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_RGB2YUV_INPUT_CLIP_Y,IEP_REGB_RGB2YUV_INPUT_CLIP_Z(x))
#define     IEP_REGB_YUV2RGB_INPUT_CLIP(base, x)  ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_YUV2RGB_INPUT_CLIP_Y,IEP_REGB_YUV2RGB_INPUT_CLIP_Z(x))
#define     IEP_REGB_RGB_TO_YUV_EN(base, x)       ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_RGB_TO_YUV_EN_Y,IEP_REGB_RGB_TO_YUV_EN_Z(x))
#define     IEP_REGB_YUV_TO_RGB_EN(base, x)       ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_YUV_TO_RGB_EN_Y,IEP_REGB_YUV_TO_RGB_EN_Z(x))
#define     IEP_REGB_RGB2YUV_COE_SEL(base, x)     ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_RGB2YUV_COE_SEL_Y,IEP_REGB_RGB2YUV_COE_SEL_Z(x))
#define     IEP_REGB_YUV2RGB_COE_SEL(base, x)     ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_YUV2RGB_COE_SEL_Y,IEP_REGB_YUV2RGB_COE_SEL_Z(x))
#define     IEP_REGB_DITHER_DOWN_EN(base, x)      ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_DITHER_DOWN_EN_Y,IEP_REGB_DITHER_DOWN_EN_Z(x))
#define     IEP_REGB_DITHER_UP_EN(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_DITHER_UP_EN_Y,IEP_REGB_DITHER_UP_EN_Z(x))
#define     IEP_REGB_DST_YUV_SWAP(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_DST_YUV_SWAP_Y,IEP_REGB_DST_YUV_SWAP_Z(x))
#define     IEP_REGB_DST_RGB_SWAP(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_DST_RGB_SWAP_Y,IEP_REGB_DST_RGB_SWAP_Z(x))
#define     IEP_REGB_DST_FMT(base, x)             ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_DST_FMT_Y,IEP_REGB_DST_FMT_Z(x))
#define     IEP_REGB_SRC_YUV_SWAP(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_SRC_YUV_SWAP_Y,IEP_REGB_SRC_YUV_SWAP_Z(x))
#define     IEP_REGB_SRC_RGB_SWAP(base, x)        ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_SRC_RGB_SWAP_Y,IEP_REGB_SRC_RGB_SWAP_Z(x))
#define     IEP_REGB_SRC_FMT(base, x)             ConfRegBits32(base, RAW_rIEP_CONFIG1,rIEP_CONFIG1,IEP_REGB_SRC_FMT_Y,IEP_REGB_SRC_FMT_Z(x))

/*iep_int*/
#define     IEP_REGB_FRAME_END_INT_CLR(base, x)   MaskRegBits32(base, rIEP_INT,IEP_REGB_FRAME_END_INT_CLR_Y,IEP_REGB_FRAME_END_INT_CLR_Z(x))
#define     IEP_REGB_FRAME_END_INT_EN(base, x)    MaskRegBits32(base, rIEP_INT,IEP_REGB_FRAME_END_INT_EN_Y,IEP_REGB_FRAME_END_INT_EN_Z(x))

/*frm_start*/
#define     IEP_REGB_FRM_START(base, x)           WriteReg32(base, rIEP_FRM_START,x)

/*soft_rst*/
#define     IEP_REGB_SOFT_RST(base, x)            WriteReg32(base, rIEP_SOFT_RST,x)

/*iep_vir_img_width*/
#define     IEP_REGB_DST_VIR_LINE_WIDTH(base, x)  ConfRegBits32(base, RAW_rIEP_VIR_IMG_WIDTH,rIEP_VIR_IMG_WIDTH,IEP_REGB_DST_VIR_LINE_WIDTH_Y,IEP_REGB_DST_VIR_LINE_WIDTH_Z(x))
#define     IEP_REGB_SRC_VIR_LINE_WIDTH(base, x)  ConfRegBits32(base, RAW_rIEP_VIR_IMG_WIDTH,rIEP_VIR_IMG_WIDTH,IEP_REGB_SRC_VIR_LINE_WIDTH_Y,IEP_REGB_SRC_VIR_LINE_WIDTH_Z(x))

/*iep_img_scl_fct*/
#define     IEP_REGB_SCL_VRT_FCT(base, x)         ConfRegBits32(base, RAW_rIEP_IMG_SCL_FCT,rIEP_IMG_SCL_FCT,IEP_REGB_SCL_VRT_FCT_Y,IEP_REGB_SCL_VRT_FCT_Z(x))
#define     IEP_REGB_SCL_HRZ_FCT(base, x)         ConfRegBits32(base, RAW_rIEP_IMG_SCL_FCT,rIEP_IMG_SCL_FCT,IEP_REGB_SCL_HRZ_FCT_Y,IEP_REGB_SCL_HRZ_FCT_Z(x))

/*iep_src_img_size*/
#define     IEP_REGB_SRC_IMG_HEIGHT(base, x)      ConfRegBits32(base, RAW_rIEP_SRC_IMG_SIZE,rIEP_SRC_IMG_SIZE,IEP_REGB_SRC_IMG_HEIGHT_Y,IEP_REGB_SRC_IMG_HEIGHT_Z(x))
#define     IEP_REGB_SRC_IMG_WIDTH(base, x)       ConfRegBits32(base, RAW_rIEP_SRC_IMG_SIZE,rIEP_SRC_IMG_SIZE,IEP_REGB_SRC_IMG_WIDTH_Y,IEP_REGB_SRC_IMG_WIDTH_Z(x))
//iep_dst_img_size
#define     IEP_REGB_DST_IMG_HEIGHT(base, x)      ConfRegBits32(base, RAW_rIEP_DST_IMG_SIZE,rIEP_DST_IMG_SIZE,IEP_REGB_DST_IMG_HEIGHT_Y,IEP_REGB_DST_IMG_HEIGHT_Z(x))
#define     IEP_REGB_DST_IMG_WIDTH(base, x)       ConfRegBits32(base, RAW_rIEP_DST_IMG_SIZE,rIEP_DST_IMG_SIZE,IEP_REGB_DST_IMG_WIDTH_Y,IEP_REGB_DST_IMG_WIDTH_Z(x))

/*dst_img_width_tile0/1/2/3*/
#define     IEP_REGB_DST_IMG_WIDTH_TILE0(base, x) WriteReg32(base, rIEP_DST_IMG_WIDTH_TILE0,x)
#define     IEP_REGB_DST_IMG_WIDTH_TILE1(base, x) WriteReg32(base, rIEP_DST_IMG_WIDTH_TILE1,x)
#define     IEP_REGB_DST_IMG_WIDTH_TILE2(base, x) WriteReg32(base, rIEP_DST_IMG_WIDTH_TILE2,x)
#define     IEP_REGB_DST_IMG_WIDTH_TILE3(base, x) WriteReg32(base, rIEP_DST_IMG_WIDTH_TILE3,x)

/*iep_enh_yuv_cnfg0*/
#define     IEP_REGB_SAT_CON(base, x)             ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_0,rIEP_ENH_YUV_CNFG_0,IEP_REGB_SAT_CON_Y,IEP_REGB_SAT_CON_Z(x))
#define     IEP_REGB_CONTRAST(base, x)            ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_0,rIEP_ENH_YUV_CNFG_0,IEP_REGB_CONTRAST_Y,IEP_REGB_CONTRAST_Z(x))
#define     IEP_REGB_BRIGHTNESS(base, x)          ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_0,rIEP_ENH_YUV_CNFG_0,IEP_REGB_BRIGHTNESS_Y,IEP_REGB_BRIGHTNESS_Z(x))
/*iep_enh_yuv_cnfg1*/
#define     IEP_REGB_COS_HUE(base, x)             ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_1,rIEP_ENH_YUV_CNFG_1,IEP_REGB_COS_HUE_Y,IEP_REGB_COS_HUE_Z(x))
#define     IEP_REGB_SIN_HUE(base, x)             ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_1,rIEP_ENH_YUV_CNFG_1,IEP_REGB_SIN_HUE_Y,IEP_REGB_SIN_HUE_Z(x))
/*iep_enh_yuv_cnfg2*/
#define     IEP_REGB_VIDEO_MODE(base, x)          ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_2,rIEP_ENH_YUV_CNFG_2,IEP_REGB_VIDEO_MODE_Y,IEP_REGB_VIDEO_MODE_Z(x))
#define     IEP_REGB_COLOR_BAR_V(base, x)         ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_2,rIEP_ENH_YUV_CNFG_2,IEP_REGB_COLOR_BAR_V_Y,IEP_REGB_COLOR_BAR_V_Z(x))
#define     IEP_REGB_COLOR_BAR_U(base, x)         ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_2,rIEP_ENH_YUV_CNFG_2,IEP_REGB_COLOR_BAR_U_Y,IEP_REGB_COLOR_BAR_U_Z(x))
#define     IEP_REGB_COLOR_BAR_Y(base, x)         ConfRegBits32(base, RAW_rIEP_ENH_YUV_CNFG_2,rIEP_ENH_YUV_CNFG_2,IEP_REGB_COLOR_BAR_Y_Y,IEP_REGB_COLOR_BAR_Y_Z(x))
/*iep_enh_rgb_cnfg*/
#define     IEP_REGB_YUV_DNS_LUMA_SPAT_SEL(base, x) ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_YUV_DNS_LUMA_SPAT_SEL_Y,IEP_REGB_YUV_DNS_LUMA_SPAT_SEL_Z(x))
#define     IEP_REGB_YUV_DNS_LUMA_TEMP_SEL(base, x) ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_YUV_DNS_LUMA_TEMP_SEL_Y,IEP_REGB_YUV_DNS_LUMA_TEMP_SEL_Z(x))
#define     IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL(base, x) ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL_Y,IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL_Z(x))
#define     IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL(base, x) ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL_Y,IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL_Z(x))
#define     IEP_REGB_ENH_THRESHOLD(base, x)       ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_ENH_THRESHOLD_Y,IEP_REGB_ENH_THRESHOLD_Z(x))
#define     IEP_REGB_ENH_ALPHA(base, x)           ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_ENH_ALPHA_Y,IEP_REGB_ENH_ALPHA_Z(x))
#define     IEP_REGB_ENH_RADIUS(base, x)          ConfRegBits32(base, RAW_rIEP_ENH_RGB_CNFG,rIEP_ENH_RGB_CNFG,IEP_REGB_ENH_RADIUS_Y,IEP_REGB_ENH_RADIUS_Z(x))
/*iep_enh_c_coe*/
#define     IEP_REGB_ENH_C_COE(base, x)           WriteReg32(base, rIEP_ENH_C_COE,x)
/*src_addr*/
#define     IEP_REGB_SRC_ADDR_YRGB(base, x)       WriteReg32(base, rIEP_SRC_ADDR_YRGB, x)
#define     IEP_REGB_SRC_ADDR_CBCR(base, x)       WriteReg32(base, rIEP_SRC_ADDR_CBCR, x)
#define     IEP_REGB_SRC_ADDR_CR(base, x)         WriteReg32(base, rIEP_SRC_ADDR_CR, x)
#define     IEP_REGB_SRC_ADDR_Y1(base, x)         WriteReg32(base, rIEP_SRC_ADDR_Y1, x)
#define     IEP_REGB_SRC_ADDR_CBCR1(base, x)      WriteReg32(base, rIEP_SRC_ADDR_CBCR1, x)
#define     IEP_REGB_SRC_ADDR_CR1(base, x)        WriteReg32(base, rIEP_SRC_ADDR_CR1, x)
#define     IEP_REGB_SRC_ADDR_Y_ITEMP(base, x)    WriteReg32(base, rIEP_SRC_ADDR_Y_ITEMP, x)
#define     IEP_REGB_SRC_ADDR_CBCR_ITEMP(base, x) WriteReg32(base, rIEP_SRC_ADDR_CBCR_ITEMP, x)
#define     IEP_REGB_SRC_ADDR_CR_ITEMP(base, x)   WriteReg32(base, rIEP_SRC_ADDR_CR_ITEMP, x)
#define     IEP_REGB_SRC_ADDR_Y_FTEMP(base, x)    WriteReg32(base, rIEP_SRC_ADDR_Y_FTEMP, x)
#define     IEP_REGB_SRC_ADDR_CBCR_FTEMP(base, x) WriteReg32(base, rIEP_SRC_ADDR_CBCR_FTEMP, x)
#define     IEP_REGB_SRC_ADDR_CR_FTEMP(base, x)   WriteReg32(base, rIEP_SRC_ADDR_CR_FTEMP, x)
/*dst_addr*/
#define     IEP_REGB_DST_ADDR_YRGB(base, x)       WriteReg32(base, rIEP_DST_ADDR_YRGB,x)
#define     IEP_REGB_DST_ADDR_CBCR(base, x)       WriteReg32(base, rIEP_DST_ADDR_CBCR, x)
#define     IEP_REGB_DST_ADDR_CR(base, x)         WriteReg32(base, rIEP_DST_ADDR_CR, x)
#define     IEP_REGB_DST_ADDR_Y1(base, x)         WriteReg32(base, rIEP_DST_ADDR_Y1, x)
#define     IEP_REGB_DST_ADDR_CBCR1(base, x)      WriteReg32(base, rIEP_DST_ADDR_CBCR1, x)
#define     IEP_REGB_DST_ADDR_CR1(base, x)        WriteReg32(base, rIEP_DST_ADDR_CR1, x)
#define     IEP_REGB_DST_ADDR_Y_ITEMP(base, x)    WriteReg32(base, rIEP_DST_ADDR_Y_ITEMP, x)
#define     IEP_REGB_DST_ADDR_CBCR_ITEMP(base, x) WriteReg32(base, rIEP_DST_ADDR_CBCR_ITEMP, x)
#define     IEP_REGB_DST_ADDR_CR_ITEMP(base, x)   WriteReg32(base, rIEP_DST_ADDR_CR_ITEMP, x)
#define     IEP_REGB_DST_ADDR_Y_FTEMP(base, x)    WriteReg32(base, rIEP_DST_ADDR_Y_FTEMP, x)
#define     IEP_REGB_DST_ADDR_CBCR_FTEMP(base, x) WriteReg32(base, rIEP_DST_ADDR_CBCR_FTEMP, x)
#define     IEP_REGB_DST_ADDR_CR_FTEMP(base, x)   WriteReg32(base, rIEP_DST_ADDR_CR_FTEMP, x)

/*dil_mtn_tab*/
#define     IEP_REGB_DIL_MTN_TAB0(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB0,x)
#define     IEP_REGB_DIL_MTN_TAB1(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB1,x)
#define     IEP_REGB_DIL_MTN_TAB2(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB2,x)
#define     IEP_REGB_DIL_MTN_TAB3(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB3,x)
#define     IEP_REGB_DIL_MTN_TAB4(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB4,x)
#define     IEP_REGB_DIL_MTN_TAB5(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB5,x)
#define     IEP_REGB_DIL_MTN_TAB6(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB6,x)
#define     IEP_REGB_DIL_MTN_TAB7(base, x)      WriteReg32(base, rIEP_DIL_MTN_TAB7,x)

#define     IEP_REGB_STATUS(base)               ReadReg32(base, rIEP_STATUS)

#if defined(CONFIG_IEP_MMU)
/*mmu*/
#define     IEP_REGB_MMU_DTE_ADDR(base, x)                          WriteReg32(base, rIEP_MMU_DTE_ADDR, x)
#define     IEP_REGB_MMU_STATUS(base)                               ReadReg32(base, rIEP_MMU_STATUS)

#define     IEP_REGB_MMU_CMD(base, x)                               MaskRegBits32(base, rIEP_MMU_CMD, IEP_REGB_MMU_CMD_Y, IEP_REGB_MMU_CMD_Z(x))

#define     IEP_REGB_MMU_PAGE_FAULT_ADDR(base)                      ReadReg32(base, rIEP_MMU_PAGE_FAULT_ADDR)

#define     IEP_REGB_MMU_ZAP_ONE_LINE(base, x)                      MaskRegBits32(base, rIEP_MMU_ZAP_ONE_LINE, \
                                                                    IEP_REGB_MMU_ZAP_ONE_LINE_Y, \
                                                                    IEP_REGB_MMU_ZAP_ONE_LINE_Z(x))

#define     IEP_REGB_MMU_INT_RAWSTAT(base)                          ReadReg32(base, rIEP_MMU_INT_RAWSTAT)

#define     IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR(base, x)        MaskRegBits32(base, rIEP_MMU_INT_CLEAR, \
                                                                    IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR_Y, \
                                                                    IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR_Z(x))
#define     IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR(base, x)    MaskRegBits32(base, rIEP_MMU_INT_CLEAR, \
                                                                    IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR_Y, \
                                                                    IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR_Z(x))

#define     IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN(base, x)        MaskRegBits32(base, rIEP_MMU_INT_MASK, \
                                                                    IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN_Y, \
                                                                    IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN_Z(x))
#define     IEP_REGB_MMU_INT_MASK_READ_BUS_ERROR_INT_EN(base, x)    MaskRegBits32(base, rIEP_MMU_INT_MASK, \
                                                                    IEP_REGB_MMU_INT_MASK_READ_BUS_ERROR_INT_EN_Y, \
                                                                    IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN_Z(x))

#define     IEP_REGB_MMU_INT_STATUS(base)                           ReadReg32(base, rIEP_MMU_INT_STATUS)

#define     IEP_REGB_MMU_AUTO_GATING(base, x)                       MaskRegBits32(base, rIEP_MMU_AUTO_GATING, \
                                                                    IEP_REGB_MMU_AUTO_GATING_Y, \
                                                                    IEP_REGB_MMU_AUTO_GATING_Z(x))
#endif

void iep_config_lcdc_path(struct IEP_MSG *iep_msg);

/* system control, directly operating the device registers.*/
/* parameter @base need to be set to device base address. */
void iep_soft_rst(void *base);
void iep_config_done(void *base);
void iep_config_frm_start(void *base);
int iep_probe_int(void *base);
void iep_config_frame_end_int_clr(void *base);
void iep_config_frame_end_int_en(void *base);
struct iep_status iep_get_status(void *base);
#if defined(CONFIG_IEP_MMU)
struct iep_mmu_int_status iep_probe_mmu_int_status(void *base);
void iep_config_mmu_page_fault_int_en(void *base, bool en);
void iep_config_mmu_page_fault_int_clr(void *base);
void iep_config_mmu_read_bus_error_int_clr(void *base);
uint32_t iep_probe_mmu_page_fault_addr(void *base);
void iep_config_mmu_cmd(void *base, enum iep_mmu_cmd cmd);
void iep_config_mmu_dte_addr(void *base, uint32_t addr);
#endif
int iep_get_deinterlace_mode(void *base);
void iep_set_deinterlace_mode(int mode, void *base);
void iep_switch_input_address(void *base);

/* generating a series of iep registers copy to the session private buffer */
void iep_config(iep_session *session, struct IEP_MSG *iep_msg);

/*#define IEP_PRINT_INFO*/
#endif
