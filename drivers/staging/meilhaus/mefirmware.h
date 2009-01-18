/**
 * @file mefirmware.h
 *
 * @brief Definitions of the firmware handling functions.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
 */

/***************************************************************************
 *   Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)     *
 *   Copyright (C) 2007 by Krzysztof Gantzke k.gantzke@meilhaus.de         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef _MEFIRMWARE_H
# define _MEFIRMWARE_H

# ifdef __KERNEL__

#define ME_ERRNO_FIRMWARE		-1

/**
* Registry
*/
#define ME_XILINX_CS1_REG		0x00C8

/**
* Flags (bits)
*/

#define ME_FIRMWARE_BUSY_FLAG	0x00000020
#define ME_FIRMWARE_DONE_FLAG	0x00000004
#define ME_FIRMWARE_CS_WRITE	0x00000100

#define ME_PLX_PCI_ACTIVATE		0x43

int me_xilinx_download(unsigned long register_base_control,
		       unsigned long register_base_data,
		       struct device *dev, const char *firmware_name);

# endif	//__KERNEL__

#endif //_MEFIRMWARE_H
