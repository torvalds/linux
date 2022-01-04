/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Retrieve encoded MAC address from ATMEL ttpci_eeprom serial 2-wire EEPROM,
    decode it and store it in associated adapter net device

    Robert Schlabbach	GMX
    Michael Glaum	KVH Industries
    Holger Waechtler	Convergence


*/

#ifndef __TTPCI_EEPROM_H__
#define __TTPCI_EEPROM_H__

#include <linux/types.h>
#include <linux/i2c.h>

extern int ttpci_eeprom_decode_mac(u8 *decodedMAC, u8 *encodedMAC);
extern int ttpci_eeprom_parse_mac(struct i2c_adapter *adapter, u8 *propsed_mac);

#endif
