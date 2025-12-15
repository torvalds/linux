/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright 2025 NXP
 */

#ifndef __IMX8MP_AIPSTZ_H
#define __IMX8MP_AIPSTZ_H

/* consumer type - master or peripheral */
#define IMX8MP_AIPSTZ_MASTER		0x0
#define IMX8MP_AIPSTZ_PERIPH		0x1

/* master configuration options */
#define IMX8MP_AIPSTZ_MPL		(1 << 0)
#define IMX8MP_AIPSTZ_MTW		(1 << 1)
#define IMX8MP_AIPSTZ_MTR		(1 << 2)
#define IMX8MP_AIPSTZ_MBW		(1 << 3)

/* peripheral configuration options */
#define IMX8MP_AIPSTZ_TP		(1 << 0)
#define IMX8MP_AIPSTZ_WP		(1 << 1)
#define IMX8MP_AIPSTZ_SP		(1 << 2)
#define IMX8MP_AIPSTZ_BW		(1 << 3)

/* master ID definitions */
#define IMX8MP_AIPSTZ_EDMA		0 /* AUDIOMIX EDMA */
#define IMX8MP_AIPSTZ_CA53		1 /* Cortex-A53 cluster */
#define IMX8MP_AIPSTZ_SDMA2		3 /* AUDIOMIX SDMA2 */
#define IMX8MP_AIPSTZ_SDMA3		3 /* AUDIOMIX SDMA3 */
#define IMX8MP_AIPSTZ_HIFI4		5 /* HIFI4 DSP */
#define IMX8MP_AIPSTZ_CM7		6 /* Cortex-M7 */

#endif /* __IMX8MP_AIPSTZ_H */
