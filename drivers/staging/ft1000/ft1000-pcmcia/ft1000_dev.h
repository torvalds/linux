//---------------------------------------------------------------------------
// FT1000 driver for Flarion Flash OFDM NIC Device
//
// Copyright (C) 2002 Flarion Technologies, All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option) any
// later version. This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place -
// Suite 330, Boston, MA 02111-1307, USA.
//---------------------------------------------------------------------------
//
// File:         ft1000_dev.h
//
// Description:    Register definitions and bit masks for the FT1000 NIC
//
// History:
// 2/5/02     Ivan Bohannon      Written.
// 8/29/02    Whc                Ported to Linux.
//
//---------------------------------------------------------------------------
#ifndef _FT1000_DEVH_
#define _FT1000_DEVH_

//---------------------------------------------------------------------------
//
// Function:   ft1000_read_reg
// Descripton: This function will read the value of a given ASIC register.
// Input:
//     dev    - device structure
//     offset - ASIC register offset
// Output:
//     data   - ASIC register value
//
//---------------------------------------------------------------------------
static inline u16 ft1000_read_reg (struct net_device *dev, u16 offset) {
    u16 data = 0;

    data = inw(dev->base_addr + offset);

    return (data);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_write_reg
// Descripton: This function will set the value for a given ASIC register.
// Input:
//     dev    - device structure
//     offset - ASIC register offset
//     value  - value to write
// Output:
//     None.
//
//---------------------------------------------------------------------------
static inline void ft1000_write_reg (struct net_device *dev, u16 offset, u16 value) {
    outw (value, dev->base_addr + offset);
}

#endif // _FT1000_DEVH_

