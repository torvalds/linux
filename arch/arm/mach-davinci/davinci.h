/*
 * This file contains the processor specific definitions
 * of the TI DM644x, DM355, DM365, and DM646x.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated
 * Copyright (c) 2007 Deep Root Systems, LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __DAVINCI_H
#define __DAVINCI_H

#include <linux/clk.h>
#include <linux/videodev2.h>
#include <linux/davinci_emac.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/platform_data/davinci_asp.h>
#include <linux/platform_data/keyscan-davinci.h>
#include <mach/hardware.h>
#include <mach/edma.h>

#include <media/davinci/vpfe_capture.h>
#include <media/davinci/vpif_types.h>
#include <media/davinci/vpss.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_venc.h>
#include <media/davinci/vpbe.h>
#include <media/davinci/vpbe_osd.h>

#define DAVINCI_SYSTEM_MODULE_BASE	0x01c40000
#define SYSMOD_VIDCLKCTL		0x38
#define SYSMOD_VPSS_CLKCTL		0x44
#define SYSMOD_VDD3P3VPWDN		0x48
#define SYSMOD_VSCLKDIS			0x6c
#define SYSMOD_PUPDCTL1			0x7c

extern void __iomem *davinci_sysmod_base;
#define DAVINCI_SYSMOD_VIRT(x)	(davinci_sysmod_base + (x))
void davinci_map_sysmod(void);

/* DM355 base addresses */
#define DM355_ASYNC_EMIF_CONTROL_BASE	0x01e10000
#define DM355_ASYNC_EMIF_DATA_CE0_BASE	0x02000000

#define ASP1_TX_EVT_EN	1
#define ASP1_RX_EVT_EN	2

/* DM365 base addresses */
#define DM365_ASYNC_EMIF_CONTROL_BASE	0x01d10000
#define DM365_ASYNC_EMIF_DATA_CE0_BASE	0x02000000
#define DM365_ASYNC_EMIF_DATA_CE1_BASE	0x04000000

/* DM644x base addresses */
#define DM644X_ASYNC_EMIF_CONTROL_BASE	0x01e00000
#define DM644X_ASYNC_EMIF_DATA_CE0_BASE 0x02000000
#define DM644X_ASYNC_EMIF_DATA_CE1_BASE 0x04000000
#define DM644X_ASYNC_EMIF_DATA_CE2_BASE 0x06000000
#define DM644X_ASYNC_EMIF_DATA_CE3_BASE 0x08000000

/* DM646x base addresses */
#define DM646X_ASYNC_EMIF_CONTROL_BASE	0x20008000
#define DM646X_ASYNC_EMIF_CS2_SPACE_BASE 0x42000000

/* DM355 function declarations */
void __init dm355_init(void);
void dm355_init_spi0(unsigned chipselect_mask,
		const struct spi_board_info *info, unsigned len);
void __init dm355_init_asp1(u32 evt_enable, struct snd_platform_data *pdata);
void dm355_set_vpfe_config(struct vpfe_config *cfg);

/* DM365 function declarations */
void __init dm365_init(void);
void __init dm365_init_asp(struct snd_platform_data *pdata);
void __init dm365_init_vc(struct snd_platform_data *pdata);
void __init dm365_init_ks(struct davinci_ks_platform_data *pdata);
void __init dm365_init_rtc(void);
void dm365_init_spi0(unsigned chipselect_mask,
			const struct spi_board_info *info, unsigned len);
void dm365_set_vpfe_config(struct vpfe_config *cfg);

/* DM644x function declarations */
void __init dm644x_init(void);
void __init dm644x_init_asp(struct snd_platform_data *pdata);
int __init dm644x_init_video(struct vpfe_config *, struct vpbe_config *);

/* DM646x function declarations */
void __init dm646x_init(void);
void __init dm646x_init_mcasp0(struct snd_platform_data *pdata);
void __init dm646x_init_mcasp1(struct snd_platform_data *pdata);
int __init dm646x_init_edma(struct edma_rsv_info *rsv);
void dm646x_video_init(void);
void dm646x_setup_vpif(struct vpif_display_config *,
		       struct vpif_capture_config *);
#endif /*__DAVINCI_H */
