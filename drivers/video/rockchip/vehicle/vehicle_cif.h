/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_CIF_H
#define __VEHICLE_CIF_H

#include "vehicle_cfg.h"
#include "vehicle_cif_regs.h"
#include "../../../media/platform/rockchip/cif/dev.h"
#include <linux/dma-mapping.h>

enum vehicle_rkcif_chip_id {
	CHIP_RK3568_VEHICLE_CIF = 0x0,
	CHIP_RK3588_VEHICLE_CIF,
};

enum rkcif_csi_host_idx {
	RKCIF_MIPI0_CSI2 = 0x0,
	RKCIF_MIPI1_CSI2,
	RKCIF_MIPI2_CSI2,
	RKCIF_MIPI3_CSI2,
	RKCIF_MIPI4_CSI2,
	RKCIF_MIPI5_CSI2,
};

struct vehicle_rkcif_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

struct rk_cif_clk {
	/************clk************/
	struct clk	*clks[RKCIF_MAX_BUS_CLK];
	struct clk	*xvclk;
	int		clks_num;
	/************reset************/
	struct reset_control	*cif_rst[RKCIF_MAX_RESET];
	int		rsts_num;
	/*  spinlock_t lock; */
	bool		on;
};

struct rk_cif_irqinfo {
	unsigned int irq;
	unsigned long cifirq_idx;
	unsigned long cifirq_normal_idx;
	unsigned long cifirq_abnormal_idx;
	unsigned long dmairq_idx;

	/* @csi_overflow_cnt: count of csi overflow irq
	 * @csi_bwidth_lack_cnt: count of csi bandwidth lack irq
	 * @dvp_bus_err_cnt: count of dvp bus err irq
	 * @dvp_overflow_cnt: count dvp overflow irq
	 * @dvp_line_err_cnt: count dvp line err irq
	 * @dvp_pix_err_cnt: count dvp pix err irq
	 * @all_frm_end_cnt: raw frame end count
	 * @all_err_cnt: all err count
	 * @
	 */

	u64 csi_overflow_cnt;
	u64 csi_bwidth_lack_cnt;
	u64 dvp_bus_err_cnt;
	u64 dvp_overflow_cnt;
	u64 dvp_line_err_cnt;
	u64 dvp_pix_err_cnt;
	u64 all_frm_end_cnt;
	u64 all_err_cnt;
	u64 dvp_size_err_cnt;
	u64 dvp_bwidth_lack_cnt;
	u64 csi_size_err_cnt;
};

#define RKCIF_MAX_CSI_CHANNEL	4
struct vehicle_csi_channel_info {
	unsigned char	id;
	unsigned char	enable;	/* capture enable */
	unsigned char	vc;
	unsigned char	data_type;
	unsigned char	crop_en;
	unsigned char	cmd_mode_en;
	unsigned char	fmt_val;
	unsigned int	width;
	unsigned int	height;
	unsigned int	virtual_width;
	unsigned int	crop_st_x;
	unsigned int	crop_st_y;
};

struct vehicle_cif {
	struct		device *dev;
	struct		device_node *phy_node;
	struct		rk_cif_clk clk;
	struct		vehicle_cfg cif_cfg;
	char		*base;  /*cif base addr*/
	//unsigned long cru_base;
	//unsigned long grf_base;
	void __iomem	*cru_base; /*cru base addr*/
	void __iomem	*grf_base; /*grf base addr*/
	void __iomem	*csi2_dphy_base; /*csi2_dphy base addr*/
	void __iomem	*csi2_base; /*csi2 base addr*/
	struct		delayed_work work;

	bool		is_enabled;
	u32		frame_buf[MAX_BUF_NUM];
	u32		current_buf_index;
	u32		last_buf_index;
	u32		active[2];
	int		irq;
	int		csi2_irq1;
	int		csi2_irq2;
	int		drop_frames;
	struct		rk_cif_irqinfo irqinfo;
	const		struct vehicle_cif_reg *cif_regs;
	struct		regmap *regmap_grf;
	struct		regmap *regmap_dphy_grf;
	unsigned int	frame_idx;
	struct	vehicle_rkcif_dummy_buffer	dummy_buf;
	struct csi2_dphy_hw	*dphy_hw;
	int		num_channels;
	int		chip_id;
	int		inf_id;
	unsigned int	csi_host_idx;
	struct		vehicle_csi_channel_info channels[RKCIF_MAX_CSI_CHANNEL];
	spinlock_t	vbq_lock; /* vfd lock */
	bool		interlaced_enable;
	unsigned int	interlaced_offset;
	unsigned int	interlaced_counts;
	unsigned long	*interlaced_buffer;
	atomic_t	reset_status;
	wait_queue_head_t	wq_stopped;
	bool		stopping;
	struct mutex	stream_lock;
	enum rkcif_state	state;
};

int vehicle_cif_init_mclk(struct vehicle_cif *cif);
int vehicle_cif_init(struct vehicle_cif *cif);
int vehicle_cif_deinit(struct vehicle_cif *cif);

int vehicle_cif_reverse_open(struct vehicle_cfg *v_cfg);

int vehicle_cif_reverse_close(void);
int vehicle_wait_cif_reset_done(void);

/* CIF IRQ STAT*/
#define DMA_FRAME_END					(0x01 << 0)
#define LINE_END					(0x01 << 1)
#define IFIFO_OF					(0x01 << 4)
#define DFIFO_OF					(0x01 << 5)
#define PRE_INF_FRAME_END				(0x01 << 8)
#define PST_INF_FRAME_END				(0x01 << 9)

enum rk_camera_signal_polarity {
	RK_CAMERA_DEVICE_SIGNAL_HIGH_LEVEL = 1,
	RK_CAMERA_DEVICE_SIGNAL_LOW_LEVEL = 0,
};

enum rk_camera_device_type {
	RK_CAMERA_DEVICE_BT601_8	= 0x10000011,
	RK_CAMERA_DEVICE_BT601_10	= 0x10000012,
	RK_CAMERA_DEVICE_BT601_12	= 0x10000014,
	RK_CAMERA_DEVICE_BT601_16	= 0x10000018,

	RK_CAMERA_DEVICE_BT656_8	= 0x10000021,
	RK_CAMERA_DEVICE_BT656_10	= 0x10000022,
	RK_CAMERA_DEVICE_BT656_12	= 0x10000024,
	RK_CAMERA_DEVICE_BT656_16	= 0x10000028,

	RK_CAMERA_DEVICE_CVBS_NTSC	= 0x20000001,
	RK_CAMERA_DEVICE_CVBS_PAL	= 0x20000002
};

#endif
