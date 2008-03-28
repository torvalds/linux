/*
 *
 *  $Id$
 *
 *  Copyright (C) 2007 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _PVRUSB2_FX2_CMD_H_
#define _PVRUSB2_FX2_CMD_H_

#define FX2CMD_MEM_WRITE_DWORD  0x01u
#define FX2CMD_MEM_READ_DWORD   0x02u

#define FX2CMD_MEM_READ_64BYTES 0x28u

#define FX2CMD_REG_WRITE        0x04u
#define FX2CMD_REG_READ         0x05u
#define FX2CMD_MEMSEL           0x06u

#define FX2CMD_I2C_WRITE        0x08u
#define FX2CMD_I2C_READ         0x09u

#define FX2CMD_GET_USB_SPEED    0x0bu

#define FX2CMD_STREAMING_ON     0x36u
#define FX2CMD_STREAMING_OFF    0x37u

#define FX2CMD_FWPOST1          0x52u

#define FX2CMD_POWER_OFF        0xdcu
#define FX2CMD_POWER_ON         0xdeu

#define FX2CMD_DEEP_RESET       0xddu

#define FX2CMD_GET_EEPROM_ADDR  0xebu
#define FX2CMD_GET_IR_CODE      0xecu

#define FX2CMD_HCW_DEMOD_RESETIN       0xf0u
#define FX2CMD_HCW_DTV_STREAMING_ON    0xf1u
#define FX2CMD_HCW_DTV_STREAMING_OFF   0xf2u

#define FX2CMD_ONAIR_DTV_STREAMING_ON  0xa0u
#define FX2CMD_ONAIR_DTV_STREAMING_OFF 0xa1u
#define FX2CMD_ONAIR_DTV_POWER_ON      0xa2u
#define FX2CMD_ONAIR_DTV_POWER_OFF     0xa3u

#endif /* _PVRUSB2_FX2_CMD_H_ */

/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
