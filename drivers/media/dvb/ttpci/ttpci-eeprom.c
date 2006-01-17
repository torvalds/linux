/*
    Retrieve encoded MAC address from 24C16 serial 2-wire EEPROM,
    decode it and store it in the associated adapter struct for
    use by dvb_net.c

    This card appear to have the 24C16 write protect held to ground,
    thus permitting normal read/write operation. Theoretically it
    would be possible to write routines to burn a different (encoded)
    MAC address into the EEPROM.

    Robert Schlabbach	GMX
    Michael Glaum	KVH Industries
    Holger Waechtler	Convergence

    Copyright (C) 2002-2003 Ralph Metzler <rjkm@metzlerbros.de>
			    Metzler Brothers Systementwicklung GbR

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

#include <asm/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/i2c.h>

#include "ttpci-eeprom.h"

#if 1
#define dprintk(x...) do { printk(x); } while (0)
#else
#define dprintk(x...) do { } while (0)
#endif


static int check_mac_tt(u8 *buf)
{
	int i;
	u16 tmp = 0xffff;

	for (i = 0; i < 8; i++) {
		tmp  = (tmp << 8) | ((tmp >> 8) ^ buf[i]);
		tmp ^= (tmp >> 4) & 0x0f;
		tmp ^= (tmp << 12) ^ ((tmp & 0xff) << 5);
	}
	tmp ^= 0xffff;
	return (((tmp >> 8) ^ buf[8]) | ((tmp & 0xff) ^ buf[9]));
}

static int getmac_tt(u8 * decodedMAC, u8 * encodedMAC)
{
	u8 xor[20] = { 0x72, 0x23, 0x68, 0x19, 0x5c, 0xa8, 0x71, 0x2c,
		       0x54, 0xd3, 0x7b, 0xf1, 0x9E, 0x23, 0x16, 0xf6,
		       0x1d, 0x36, 0x64, 0x78};
	u8 data[20];
	int i;

	/* In case there is a sig check failure have the orig contents available */
	memcpy(data, encodedMAC, 20);

	for (i = 0; i < 20; i++)
		data[i] ^= xor[i];
	for (i = 0; i < 10; i++)
		data[i] = ((data[2 * i + 1] << 8) | data[2 * i])
			>> ((data[2 * i + 1] >> 6) & 3);

	if (check_mac_tt(data))
		return -ENODEV;

	decodedMAC[0] = data[2]; decodedMAC[1] = data[1]; decodedMAC[2] = data[0];
	decodedMAC[3] = data[6]; decodedMAC[4] = data[5]; decodedMAC[5] = data[4];
	return 0;
}

static int ttpci_eeprom_read_encodedMAC(struct i2c_adapter *adapter, u8 * encodedMAC)
{
	int ret;
	u8 b0[] = { 0xcc };

	struct i2c_msg msg[] = {
		{ .addr = 0x50, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = 0x50, .flags = I2C_M_RD, .buf = encodedMAC, .len = 20 }
	};

	/* dprintk("%s\n", __FUNCTION__); */

	ret = i2c_transfer(adapter, msg, 2);

	if (ret != 2)		/* Assume EEPROM isn't there */
		return (-ENODEV);

	return 0;
}


int ttpci_eeprom_parse_mac(struct i2c_adapter *adapter, u8 *proposed_mac)
{
	int ret, i;
	u8 encodedMAC[20];
	u8 decodedMAC[6];

	ret = ttpci_eeprom_read_encodedMAC(adapter, encodedMAC);

	if (ret != 0) {		/* Will only be -ENODEV */
		dprintk("Couldn't read from EEPROM: not there?\n");
		memset(proposed_mac, 0, 6);
		return ret;
	}

	ret = getmac_tt(decodedMAC, encodedMAC);
	if( ret != 0 ) {
		dprintk("adapter failed MAC signature check\n");
		dprintk("encoded MAC from EEPROM was " );
		for(i=0; i<19; i++) {
			dprintk( "%.2x:", encodedMAC[i]);
		}
		dprintk("%.2x\n", encodedMAC[19]);
		memset(proposed_mac, 0, 6);
		return ret;
	}

	memcpy(proposed_mac, decodedMAC, 6);
	dprintk("adapter has MAC addr = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		decodedMAC[0], decodedMAC[1], decodedMAC[2],
		decodedMAC[3], decodedMAC[4], decodedMAC[5]);
	return 0;
}

EXPORT_SYMBOL(ttpci_eeprom_parse_mac);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, others");
MODULE_DESCRIPTION("Decode dvb_net MAC address from EEPROM of PCI DVB cards "
		"made by Siemens, Technotrend, Hauppauge");
