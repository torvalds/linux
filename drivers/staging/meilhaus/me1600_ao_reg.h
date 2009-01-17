/**
 * @file me1600_ao_reg.h
 *
 * @brief ME-1600 analog output subdevice register definitions.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ME1600_AO_REG_H_
#define _ME1600_AO_REG_H_

#ifdef __KERNEL__

#define ME1600_CHANNEL_0_REG    0x00	/**< Register to set a digital value on channel 0. */
#define ME1600_CHANNEL_1_REG    0x02	/**< Register to set a digital value on channel 1. */
#define ME1600_CHANNEL_2_REG    0x04	/**< Register to set a digital value on channel 2. */
#define ME1600_CHANNEL_3_REG    0x06	/**< Register to set a digital value on channel 3. */
#define ME1600_CHANNEL_4_REG    0x08	/**< Register to set a digital value on channel 4. */
#define ME1600_CHANNEL_5_REG    0x0A	/**< Register to set a digital value on channel 5. */
#define ME1600_CHANNEL_6_REG    0x0C	/**< Register to set a digital value on channel 6. */
#define ME1600_CHANNEL_7_REG    0x0E	/**< Register to set a digital value on channel 7. */
#define ME1600_CHANNEL_8_REG    0x10	/**< Register to set a digital value on channel 8. */
#define ME1600_CHANNEL_9_REG    0x12	/**< Register to set a digital value on channel 9. */
#define ME1600_CHANNEL_10_REG   0x14	/**< Register to set a digital value on channel 10. */
#define ME1600_CHANNEL_11_REG   0x16	/**< Register to set a digital value on channel 11. */
#define ME1600_CHANNEL_12_REG   0x18	/**< Register to set a digital value on channel 12. */
#define ME1600_CHANNEL_13_REG   0x1A	/**< Register to set a digital value on channel 13. */
#define ME1600_CHANNEL_14_REG   0x1C	/**< Register to set a digital value on channel 14. */
#define ME1600_CHANNEL_15_REG   0x1E	/**< Register to set a digital value on channel 15. */

/* Every channel one bit: bipolar = 0, unipolar = 1 */
#define ME1600_UNI_BI_REG		0x20	/**< Register to switch between unipolar and bipolar. */

/* Every channel one bit (only lower 8 Bits): 0..20mA = 0, 4..20mA = 1 */
#define ME1600_020_420_REG		0x22	/**< Register to switch between the two current ranges. */

/* If a bit is set, the corresponding DAC (4 ports each) is
   not set at the moment you write to an output of it.
   Clearing the bit updates the port. */
#define ME1600_SIM_OUTPUT_REG	0x24	/**< Register to update all channels of a subdevice simultaneously. */

/* Current on/off (only lower 8 bits): off = 0, on  = 1 */
#define ME1600_CURRENT_ON_REG	0x26	/**< Register to swicht between voltage and current output. */

#define ME1600_AO_MAX_DATA		0x0FFF	/**< The maximum digital data accepted by an analog output channel. */

#endif
#endif
