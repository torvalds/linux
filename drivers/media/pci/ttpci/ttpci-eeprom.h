/*
    Retrieve encoded MAC address from ATMEL ttpci_eeprom serial 2-wire EEPROM,
    decode it and store it in associated adapter net device

    Robert Schlabbach	GMX
    Michael Glaum	KVH Industries
    Holger Waechtler	Convergence

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __TTPCI_EEPROM_H__
#define __TTPCI_EEPROM_H__

#include <linux/types.h>
#include <linux/i2c.h>

extern int ttpci_eeprom_decode_mac(u8 *decodedMAC, u8 *encodedMAC);
extern int ttpci_eeprom_parse_mac(struct i2c_adapter *adapter, u8 *propsed_mac);

#endif
