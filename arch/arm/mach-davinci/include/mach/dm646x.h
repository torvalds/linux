/*
 * Chip specific defines for DM646x SoC
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_DM646X_H
#define __ASM_ARCH_DM646X_H

#include <mach/hardware.h>
#include <mach/asp.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/davinci_emac.h>
#include <media/davinci/vpif_types.h>

#define DM646X_EMAC_BASE		(0x01C80000)
#define DM646X_EMAC_MDIO_BASE		(DM646X_EMAC_BASE + 0x4000)
#define DM646X_EMAC_CNTRL_OFFSET	(0x0000)
#define DM646X_EMAC_CNTRL_MOD_OFFSET	(0x1000)
#define DM646X_EMAC_CNTRL_RAM_OFFSET	(0x2000)
#define DM646X_EMAC_CNTRL_RAM_SIZE	(0x2000)

#define DM646X_ASYNC_EMIF_CONTROL_BASE	0x20008000
#define DM646X_ASYNC_EMIF_CS2_SPACE_BASE 0x42000000

void __init dm646x_init(void);
void __init dm646x_init_mcasp0(struct snd_platform_data *pdata);
void __init dm646x_init_mcasp1(struct snd_platform_data *pdata);
int __init dm646x_init_edma(struct edma_rsv_info *rsv);

void dm646x_video_init(void);

void dm646x_setup_vpif(struct vpif_display_config *,
		       struct vpif_capture_config *);

#endif /* __ASM_ARCH_DM646X_H */
