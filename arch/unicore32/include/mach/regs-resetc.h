/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PKUnity Reset Controller (RC) Registers
 */
/*
 * Software Reset Register
 */
#define RESETC_SWRR	(PKUNITY_RESETC_BASE + 0x0000)
/*
 * Reset Status Register
 */
#define RESETC_RSSR	(PKUNITY_RESETC_BASE + 0x0004)

/*
 * Software Reset Bit
 */
#define RESETC_SWRR_SRB		FIELD(1, 1, 0)

/*
 * Hardware Reset
 */
#define RESETC_RSSR_HWR		FIELD(1, 1, 0)
/*
 * Software Reset
 */
#define RESETC_RSSR_SWR		FIELD(1, 1, 1)
/*
 * Watchdog Reset
 */
#define RESETC_RSSR_WDR		FIELD(1, 1, 2)
/*
 * Sleep Mode Reset
 */
#define RESETC_RSSR_SMR		FIELD(1, 1, 3)

