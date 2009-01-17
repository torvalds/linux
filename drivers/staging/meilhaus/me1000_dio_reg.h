/**
 * @file me1000_dio_reg.h
 *
 * @brief ME-1000 digital i/o register definitions.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
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

#ifndef _ME1000_DIO_REG_H_
# define _ME1000_DIO_REG_H_

# ifdef __KERNEL__

# define ME1000_DIO_NUMBER_CHANNELS	32				/**< The number of channels per DIO port. */
# define ME1000_DIO_NUMBER_PORTS	4				/**< The number of ports per ME-1000. */

// # define ME1000_PORT_A                               0x0000                  /**< Port A base register offset. */
// # define ME1000_PORT_B                               0x0004                  /**< Port B base register offset. */
// # define ME1000_PORT_C                               0x0008                  /**< Port C base register offset. */
// # define ME1000_PORT_D                               0x000C                  /**< Port D base register offset. */
# define ME1000_PORT				0x0000			/**< Base for port's register. */
# define ME1000_PORT_STEP			4				/**< Distance between port's register. */

# define ME1000_PORT_MODE			0x0010			/**< Configuration register to switch the port direction. */
// # define ME1000_PORT_MODE_OUTPUT_A   (1 << 0)                /**< If set, port A is in output, otherwise in input mode. */
// # define ME1000_PORT_MODE_OUTPUT_B   (1 << 1)                /**< If set, port B is in output, otherwise in input mode. */
// # define ME1000_PORT_MODE_OUTPUT_C   (1 << 2)                /**< If set, port C is in output, otherwise in input mode. */
// # define ME1000_PORT_MODE_OUTPUT_D   (1 << 3)                /**< If set, port D is in output, otherwise in input mode. */

# endif	//__KERNEL__
#endif //_ME1000_DIO_REG_H_
