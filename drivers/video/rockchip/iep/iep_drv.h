#ifndef IEP_DRV_H_
#define IEP_DRV_H_

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>

#if defined(CONFIG_RK_IOMMU)
#include <linux/rockchip-iovmm.h>
#include <linux/dma-buf.h>
#endif
#include "iep.h"

#define IEP_REG_LEN         0x100
#define IEP_CMD_REG_LEN     0xE
#define IEP_ADD_REG_LEN     0xE0
#define IEP_RAW_REG_LEN     0xA
#define IEP_SYS_REG_LEN     0x6
#define IEP_CNF_REG_LEN     0x2

#define IEP_CNF_REG_BASE    0x0
#define IEP_SYS_REG_BASE    0x2
#define IEP_CMD_REG_BASE    0x8
#define IEP_ADD_REG_BASE    0x20
#define IEP_RAW_REG_BASE    0x16

struct iep_parameter_req {
	struct iep_img src;
	struct iep_img dst;
};

struct iep_parameter_deinterlace {
	struct iep_img src1;
	struct iep_img dst1;

	struct iep_img src_itemp;
	struct iep_img src_ftemp;

	struct iep_img dst_itemp;
	struct iep_img dst_ftemp;

	u8 dein_mode;

	// deinterlace high frequency
	u8 dein_high_fre_en;
	u8 dein_high_fre_fct;

	// deinterlace edge interpolation
	u8 dein_ei_mode;
	u8 dein_ei_smooth;
	u8 dein_ei_sel;
	u8 dein_ei_radius;
};

struct iep_parameter_enhance {
	u8 yuv_3D_denoise_en;

	u8 yuv_enhance_en;
	float yuv_enh_saturation; //0-1.992
	float yuv_enh_contrast; //0-1.992
	s8 yuv_enh_brightness; //-32<brightness<31
	s8 yuv_enh_hue_angle; //0-30,value is 0 - 30

	u8 video_mode; //0-3
	u8 color_bar_y; //0-127
	u8 color_bar_u; //0-127
	u8 color_bar_v; //0-127

	u8 rgb_enhance_en;

	u8 rgb_cg_en; //sw_rgb_con_gam_en
	double cg_rr;
	double cg_rg;
	double cg_rb;
	u8 rgb_color_enhance_en; //sw_rgb_color_enh_en
	float rgb_enh_coe; //0-3.96875
};

struct iep_parameter_scale {
	u8 scale_up_mode;
};

struct iep_parameter_convert {
	u8 dither_up_en;
	u8 dither_down_en; //not to be used

	u8 yuv2rgb_mode;
	u8 rgb2yuv_mode;

	u8 global_alpha_value;

	u8 rgb2yuv_clip_en;
	u8 yuv2rgb_clip_en;
};

typedef struct iep_session {
	/* a linked list of data so we can access them for debugging */
	struct list_head    list_session;
	/* a linked list of register data waiting for process */
	struct list_head    waiting;
	/* a linked list of register data in ready */
	struct list_head    ready;
	/* a linked list of register data in processing */
	struct list_head    running;
	/* all coommand this thread done */
	atomic_t            done;
	wait_queue_head_t   wait;
	pid_t               pid;
	atomic_t            task_running;
	atomic_t            num_done;
} iep_session;

typedef struct iep_service_info {
	struct mutex        lock;
	struct timer_list	timer;          /* timer for power off */
	struct list_head	waiting;        /* link to link_reg in struct iep_reg */
	atomic_t            waitcnt;
	struct list_head    ready;          /* link to link_reg in struct iep_reg */
	struct list_head	running;        /* link to link_reg in struct iep_reg */
	struct list_head	done;           /* link to link_reg in struct iep_reg */
	struct list_head	session;        /* link to list_session in struct vpu_session */
	atomic_t		    total_running;

	struct iep_reg      *reg;
	bool                enable;

	struct mutex	    mutex;  // mutex

	struct iep_iommu_info *iommu_info;

	struct device *iommu_dev;
	u32 alloc_type;
} iep_service_info;

struct iep_reg {
	iep_session *session;
	struct list_head 	session_link;      /* link to rga service session */
	struct list_head 	status_link;       /* link to register set list */
	uint32_t 			reg[0x300];
	bool                dpi_en;
	int                 off_x;
	int                 off_y;
	int                 act_width;
	int                 act_height;
	int                 vir_width;
	int                 vir_height;
	int                 layer;
	unsigned int        format;
	struct list_head    mem_region_list;
};

struct iep_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	unsigned long iova;              /* virtual address for iommu */
	unsigned long len;
	int hdl;
};

#endif

