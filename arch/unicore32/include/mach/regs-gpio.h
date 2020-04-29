/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PKUnity General-Purpose Input/Output (GPIO) Registers
 */

/*
 * Voltage Status Reg GPIO_GPLR.
 */
#define GPIO_GPLR	(PKUNITY_GPIO_BASE + 0x0000)
/*
 * Pin Direction Reg GPIO_GPDR.
 */
#define GPIO_GPDR	(PKUNITY_GPIO_BASE + 0x0004)
/*
 * Output Pin Set Reg GPIO_GPSR.
 */
#define GPIO_GPSR	(PKUNITY_GPIO_BASE + 0x0008)
/*
 * Output Pin Clear Reg GPIO_GPCR.
 */
#define GPIO_GPCR	(PKUNITY_GPIO_BASE + 0x000C)
/*
 * Raise Edge Detect Reg GPIO_GRER.
 */
#define GPIO_GRER	(PKUNITY_GPIO_BASE + 0x0010)
/*
 * Fall Edge Detect Reg GPIO_GFER.
 */
#define GPIO_GFER	(PKUNITY_GPIO_BASE + 0x0014)
/*
 * Edge Status Reg GPIO_GEDR.
 */
#define GPIO_GEDR	(PKUNITY_GPIO_BASE + 0x0018)
/*
 * Special Voltage Detect Reg GPIO_GPIR.
 */
#define GPIO_GPIR	(PKUNITY_GPIO_BASE + 0x0020)

#define GPIO_MIN	(0)
#define GPIO_MAX	(27)

#define GPIO_GPIO(Nb)	(0x00000001 << (Nb))	/* GPIO [0..27] */
#define GPIO_GPIO0	GPIO_GPIO(0)	/* GPIO  [0] */
#define GPIO_GPIO1	GPIO_GPIO(1)	/* GPIO  [1] */
#define GPIO_GPIO2	GPIO_GPIO(2)	/* GPIO  [2] */
#define GPIO_GPIO3	GPIO_GPIO(3)	/* GPIO  [3] */
#define GPIO_GPIO4	GPIO_GPIO(4)	/* GPIO  [4] */
#define GPIO_GPIO5	GPIO_GPIO(5)	/* GPIO  [5] */
#define GPIO_GPIO6	GPIO_GPIO(6)	/* GPIO  [6] */
#define GPIO_GPIO7	GPIO_GPIO(7)	/* GPIO  [7] */
#define GPIO_GPIO8	GPIO_GPIO(8)	/* GPIO  [8] */
#define GPIO_GPIO9	GPIO_GPIO(9)	/* GPIO  [9] */
#define GPIO_GPIO10	GPIO_GPIO(10)	/* GPIO [10] */
#define GPIO_GPIO11	GPIO_GPIO(11)	/* GPIO [11] */
#define GPIO_GPIO12	GPIO_GPIO(12)	/* GPIO [12] */
#define GPIO_GPIO13	GPIO_GPIO(13)	/* GPIO [13] */
#define GPIO_GPIO14	GPIO_GPIO(14)	/* GPIO [14] */
#define GPIO_GPIO15	GPIO_GPIO(15)	/* GPIO [15] */
#define GPIO_GPIO16	GPIO_GPIO(16)	/* GPIO [16] */
#define GPIO_GPIO17	GPIO_GPIO(17)	/* GPIO [17] */
#define GPIO_GPIO18	GPIO_GPIO(18)	/* GPIO [18] */
#define GPIO_GPIO19	GPIO_GPIO(19)	/* GPIO [19] */
#define GPIO_GPIO20	GPIO_GPIO(20)	/* GPIO [20] */
#define GPIO_GPIO21	GPIO_GPIO(21)	/* GPIO [21] */
#define GPIO_GPIO22	GPIO_GPIO(22)	/* GPIO [22] */
#define GPIO_GPIO23	GPIO_GPIO(23)	/* GPIO [23] */
#define GPIO_GPIO24	GPIO_GPIO(24)	/* GPIO [24] */
#define GPIO_GPIO25	GPIO_GPIO(25)	/* GPIO [25] */
#define GPIO_GPIO26	GPIO_GPIO(26)	/* GPIO [26] */
#define GPIO_GPIO27	GPIO_GPIO(27)	/* GPIO [27] */

