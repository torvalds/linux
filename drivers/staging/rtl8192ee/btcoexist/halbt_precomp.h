/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/
#ifndef	__HALBT_PRECOMP_H__
#define __HALBT_PRECOMP_H__
/*************************************************************
 * include files
 *************************************************************/
#include "../wifi.h"
#include "../efuse.h"
#include "../base.h"
#include "../regd.h"
#include "../cam.h"
#include "../ps.h"
#include "../pci.h"

#include "halbtcoutsrc.h"


#include "halbtc8192e2ant.h"
#include "halbtc8723b1ant.h"
#include "halbtc8723b2ant.h"
#include "halbtc8821a2ant.h"
#include "halbtc8821a1ant.h"

#define	MASKBYTE0			0xff
#define	MASKBYTE1			0xff00
#define	MASKBYTE2			0xff0000
#define	MASKBYTE3			0xff000000
#define	MASKHWORD			0xffff0000
#define	MASKLWORD			0x0000ffff
#define	MASKDWORD			0xffffffff
#define	MASK12BITS			0xfff
#define	MASKH4BITS			0xf0000000
#define MASKOFDM_D			0xffc00000
#define	MASKCCK				0x3f3f3f3f

#endif	/* __HALBT_PRECOMP_H__ */
