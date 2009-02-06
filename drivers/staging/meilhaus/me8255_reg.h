/**
 * @file me8255_reg.h
 *
 * @brief 8255 counter register definitions.
 * @note Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

#ifndef _ME8255_REG_H_
#define _ME8255_REG_H_

#ifdef __KERNEL__

#define ME8255_NUMBER_CHANNELS		8		/**< The number of channels per 8255 port. */

#define ME1400AB_PORT_A_0			0x0000	/**< Port 0 offset. */
#define ME1400AB_PORT_A_1			0x0001	/**< Port 1 offset. */
#define ME1400AB_PORT_A_2			0x0002	/**< Port 2 offset. */
#define ME1400AB_PORT_A_CTRL		0x0003	/**< Control register for 8255 A. */

#define ME1400AB_PORT_B_0			0x0008	/**< Port 0 offset. */
#define ME1400AB_PORT_B_1			0x0009	/**< Port 1 offset. */
#define ME1400AB_PORT_B_2			0x000A	/**< Port 2 offset. */
#define ME1400AB_PORT_B_CTRL		0x000B	/**< Control register for 8255 B. */

#define ME1400CD_PORT_A_0			0x0000	/**< Port 0 offset. */
#define ME1400CD_PORT_A_1			0x0001	/**< Port 1 offset. */
#define ME1400CD_PORT_A_2			0x0002	/**< Port 2 offset. */
#define ME1400CD_PORT_A_CTRL		0x0003	/**< Control register for 8255 A. */

#define ME1400CD_PORT_B_0			0x0040	/**< Port 0 offset. */
#define ME1400CD_PORT_B_1			0x0041	/**< Port 1 offset. */
#define ME1400CD_PORT_B_2			0x0042	/**< Port 2 offset. */
#define ME1400CD_PORT_B_CTRL		0x0043	/**< Control register for 8255 B. */

#define ME8255_MODE_OOO				0x80	/**< Port 2 = Output, Port 1 = Output, Port 0 = Output */
#define ME8255_MODE_IOO				0x89	/**< Port 2 = Input,  Port 1 = Output, Port 0 = Output */
#define ME8255_MODE_OIO				0x82	/**< Port 2 = Output, Port 1 = Input,  Port 0 = Output */
#define ME8255_MODE_IIO				0x8B	/**< Port 2 = Input,  Port 1 = Input,  Port 0 = Output */
#define ME8255_MODE_OOI				0x90	/**< Port 2 = Output, Port 1 = Output, Port 0 = Input */
#define ME8255_MODE_IOI				0x99	/**< Port 2 = Input,  Port 1 = Output, Port 0 = Input */
#define ME8255_MODE_OII				0x92	/**< Port 2 = Output, Port 1 = Input,  Port 0 = Input */
#define ME8255_MODE_III				0x9B	/**< Port 2 = Input,  Port 1 = Input,  Port 0 = Input */

#define ME8255_PORT_0_OUTPUT		0x1		/**< If set in mirror then port 0 is in output mode. */
#define ME8255_PORT_1_OUTPUT		0x2		/**< If set in mirror then port 1 is in output mode. */
#define ME8255_PORT_2_OUTPUT		0x4		/**< If set in mirror then port 2 is in output mode. */

#endif
#endif
