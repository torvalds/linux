/* linux/arch/arm/plat-s5p/include/plat/fimc.h
 *
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Platform header file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __ASM_PLAT_FIMC_H
#define __ASM_PLAT_FIMC_H __FILE__

#include <linux/videodev2.h>

#define FIMC_SRC_MAX_W		4224
#define FIMC_SRC_MAX_H		4224
#define FLITE_MAX_NUM		2

struct platform_device;

/* For exnternal camera device */
enum fimc_cam_type {
	CAM_TYPE_ITU	= 0,
	CAM_TYPE_MIPI	= 1,
};

enum fimc_cam_format {
	ITU_601_YCBCR422_8BIT	= (1 << 31),
	ITU_656_YCBCR422_8BIT	= (0 << 31),
	ITU_601_YCBCR422_16BIT	= (1 << 29),
	MIPI_CSI_YCBCR422_8BIT	= 0x1e,
	MIPI_CSI_RAW8		= 0x2a,
	MIPI_CSI_RAW10		= 0x2b,
	MIPI_CSI_RAW12		= 0x2c,
	MIPI_USER_DEF_PACKET_1	= 0x30,	/* User defined Byte-based packet 1 */
};

enum fimc_cam_order422 {
	CAM_ORDER422_8BIT_YCBYCR	= (0 << 14),
	CAM_ORDER422_8BIT_YCRYCB	= (1 << 14),
	CAM_ORDER422_8BIT_CBYCRY	= (2 << 14),
	CAM_ORDER422_8BIT_CRYCBY	= (3 << 14),
	CAM_ORDER422_16BIT_Y4CBCRCBCR	= (0 << 14),
	CAM_ORDER422_16BIT_Y4CRCBCRCB	= (1 << 14),
};

enum fimc_cam_index {
	CAMERA_PAR_A	= 0,
	CAMERA_PAR_B	= 1,
	CAMERA_CSI_C	= 2,
	CAMERA_CSI_D	= 3,
	CAMERA_WB	= 4,
	CAMERA_WB_B	= 5,
	CAMERA_PATTERN	= 6,
};

enum flite_index {
	FLITE_IDX_A = 0,
	FLITE_IDX_B = 1,
};

/* struct s3c_platform_camera: abstraction for input camera */
struct s3c_platform_camera {
	/*
	 * ITU cam A,B: 0,1
	 * CSI-2 cam C: 2
	 */
	enum fimc_cam_index		id;

	enum fimc_cam_type		type;		/* ITU or MIPI */
	enum fimc_cam_format		fmt;		/* input format */
	enum fimc_cam_order422		order422;	/* YCBCR422 order for ITU */
	u32				pixelformat;	/* default fourcc */

	int				i2c_busnum;
	int				(*get_i2c_busnum)(void);
	struct i2c_board_info		*info;
	struct v4l2_subdev		*sd;

	const char			srclk_name[16];	/* source of mclk name */
	const char			clk_name[16];	/* mclk name */
	const char*			(*get_clk_name)(void);	/* mclk name */
	u32				clk_rate;	/* mclk ratio */
	struct clk			*clk;		/* mclk */
	int				line_length;	/* max length */
	int				width;		/* default resol */
	int				height;		/* default resol */
	struct v4l2_rect		window;		/* real cut region from source */

	int				mipi_lanes;	/* MIPI data lanes */
	int				mipi_settle;	/* MIPI settle */
	int				mipi_align;	/* MIPI data align: 24/32 */

	/* Polarity: 1 if inverted polarity used */
	int				inv_pclk;
	int				inv_vsync;
	int				inv_href;
	int				inv_hsync;

	int				initialized;
	/* The cam needs reset before start streaming   */
	int				reset_camera;

	/* Board specific power pin control */
	int				(*cam_power)(int onoff);
	enum flite_index		flite_id;
	bool				use_isp;
	int				sensor_index;
};

/* For camera interface driver */
struct s3c_platform_fimc {
	enum fimc_cam_index		default_cam;		/* index of default cam */
#ifdef CONFIG_ARCH_EXYNOS4
	struct s3c_platform_camera	*camera[7];		/* FIXME */
#else
	struct s3c_platform_camera	*camera[5];		/* FIXME */
#endif
	int				hw_ver;
	bool				use_cam;

	void				(*cfg_gpio)(struct platform_device *pdev);
	int				(*clk_on)(struct platform_device *pdev, struct clk **clk);
	int				(*clk_off)(struct platform_device *pdev, struct clk **clk);
};

extern void s3c_fimc0_set_platdata(struct s3c_platform_fimc *fimc);
extern void s3c_fimc1_set_platdata(struct s3c_platform_fimc *fimc);
extern void s3c_fimc2_set_platdata(struct s3c_platform_fimc *fimc);
#ifdef CONFIG_ARCH_EXYNOS4
extern void s3c_fimc3_set_platdata(struct s3c_platform_fimc *fimc);
#endif

/* defined by architecture to configure gpio */
extern void s3c_fimc0_cfg_gpio(struct platform_device *pdev);
extern void s3c_fimc1_cfg_gpio(struct platform_device *pdev);
extern void s3c_fimc2_cfg_gpio(struct platform_device *pdev);
#ifdef CONFIG_ARCH_EXYNOS4
extern void s3c_fimc3_cfg_gpio(struct platform_device *pdev);
#endif
/* platform specific clock functions */
extern int s3c_fimc_clk_on(struct platform_device *pdev, struct clk **clk);
extern int s3c_fimc_clk_off(struct platform_device *pdev, struct clk **clk);

#ifdef CONFIG_DRM_EXYNOS_FIMD_WB
#include <linux/notifier.h>
extern int fimc_register_client(struct notifier_block *nb);
extern int fimc_unregister_client(struct notifier_block *nb);
#endif

#endif /*__ASM_PLAT_FIMC_H */
