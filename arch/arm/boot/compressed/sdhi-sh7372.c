/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2010 Kuninori Morimoto
 * Copyright (C) 2010 Simon Horman
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Parts inspired by u-boot
 */

#include <linux/io.h>
#include <mach/mmc.h>
#include <linux/mmc/boot.h>
#include <linux/mmc/tmio.h>

#include "sdhi-shmobile.h"

#define PORT179CR       0xe60520b3
#define PORT180CR       0xe60520b4
#define PORT181CR       0xe60520b5
#define PORT182CR       0xe60520b6
#define PORT183CR       0xe60520b7
#define PORT184CR       0xe60520b8

#define SMSTPCR3        0xe615013c

#define CR_INPUT_ENABLE 0x10
#define CR_FUNCTION1    0x01

#define SDHI1_BASE	(void __iomem *)0xe6860000
#define SDHI_BASE	SDHI1_BASE

/*  SuperH Mobile SDHI loader
 *
 * loads the zImage from an SD card starting from block 0
 * on physical partition 1
 *
 * The image must be start with a vrl4 header and
 * the zImage must start at offset 512 of the image. That is,
 * at block 1 (=byte 512) of physical partition 1
 *
 * Use the following line to write the vrl4 formated zImage
 * to an SD card
 * # dd if=vrl4.out of=/dev/sdx bs=512
 */
asmlinkage void mmc_loader(unsigned short *buf, unsigned long len)
{
	int high_capacity;

	mmc_init_progress();

	mmc_update_progress(MMC_PROGRESS_ENTER);
        /* Initialise SDHI1 */
        /* PORT184CR: GPIO_FN_SDHICMD1 Control */
        __raw_writeb(CR_FUNCTION1, PORT184CR);
        /* PORT179CR: GPIO_FN_SDHICLK1 Control */
        __raw_writeb(CR_INPUT_ENABLE|CR_FUNCTION1, PORT179CR);
        /* PORT181CR: GPIO_FN_SDHID1_3 Control */
        __raw_writeb(CR_FUNCTION1, PORT183CR);
        /* PORT182CR: GPIO_FN_SDHID1_2 Control */
        __raw_writeb(CR_FUNCTION1, PORT182CR);
        /* PORT183CR: GPIO_FN_SDHID1_1 Control */
        __raw_writeb(CR_FUNCTION1, PORT181CR);
        /* PORT180CR: GPIO_FN_SDHID1_0 Control */
        __raw_writeb(CR_FUNCTION1, PORT180CR);

        /* Enable clock to SDHI1 hardware block */
        __raw_writel(__raw_readl(SMSTPCR3) & ~(1 << 13), SMSTPCR3);

	/* setup SDHI hardware */
	mmc_update_progress(MMC_PROGRESS_INIT);
	high_capacity = sdhi_boot_init(SDHI_BASE);
	if (high_capacity < 0)
		goto err;

	mmc_update_progress(MMC_PROGRESS_LOAD);
	/* load kernel */
	if (sdhi_boot_do_read(SDHI_BASE, high_capacity,
			      0, /* Kernel is at block 1 */
			      (len + TMIO_BBS - 1) / TMIO_BBS, buf))
		goto err;

        /* Disable clock to SDHI1 hardware block */
        __raw_writel(__raw_readl(SMSTPCR3) & (1 << 13), SMSTPCR3);

	mmc_update_progress(MMC_PROGRESS_DONE);

	return;
err:
	for(;;);
}
