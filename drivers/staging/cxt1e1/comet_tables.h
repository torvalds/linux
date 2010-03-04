/*
 * $Id: comet_tables.h,v 1.5 2006/01/02 22:37:31 rickd PMCC4_3_1B $
 */

#ifndef _INC_COMET_TBLS_H_
#define _INC_COMET_TBLS_H_

/*-----------------------------------------------------------------------------
 * comet_tables.h - Waveform Tables for the PM4351 'COMET'
 *
 * Copyright (C) 2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *-----------------------------------------------------------------------------
 * RCS info:
 * RCS revision: $Revision: 1.5 $
 * Last changed on $Date: 2006/01/02 22:37:31 $
 * Changed by $Author: rickd $
 *-----------------------------------------------------------------------------
 * $Log: comet_tables.h,v $
 * Revision 1.5  2006/01/02 22:37:31  rickd
 * Double indexed arrays need sizings to avoid CC errors under
 * gcc 4.0.0
 *
 * Revision 1.4  2005/10/17 23:55:28  rickd
 * The 75 Ohm transmit waveform is not supported on PMCC4.
 *
 * Revision 1.3  2005/09/28 00:10:08  rickd
 * Add GNU License info. Structures moved to -C- file.
 *
 * Revision 1.2  2005/04/28 23:43:04  rickd
 * Add RCS tracking heading.
 *
 *-----------------------------------------------------------------------------
 */


/*****************************************************************************
*
*  Array names:
*
*       TWVLongHaul0DB
*       TWVLongHaul7_5DB
*       TWVLongHaul15DB
*       TWVLongHaul22_5DB
*       TWVShortHaul0
*       TWVShortHaul1
*       TWVShortHaul2
*       TWVShortHaul3
*       TWVShortHaul4
*       TWVShortHaul5
*       TWV_E1_120Ohm
*       TWV_E1_75Ohm    <not supported>
*       T1_Equalizer
*       E1_Equalizer
*
*****************************************************************************/

extern u_int8_t TWVLongHaul0DB[25][5];      /* T1 Long Haul 0 DB */
extern u_int8_t TWVLongHaul7_5DB[25][5];    /* T1 Long Haul 7.5 DB */
extern u_int8_t TWVLongHaul15DB[25][5];     /* T1 Long Haul 15 DB */
extern u_int8_t TWVLongHaul22_5DB[25][5];   /* T1 Long Haul 22.5 DB */
extern u_int8_t TWVShortHaul0[25][5];       /* T1 Short Haul 0-110 ft */
extern u_int8_t TWVShortHaul1[25][5];       /* T1 Short Haul 110-220 ft */
extern u_int8_t TWVShortHaul2[25][5];       /* T1 Short Haul 220-330 ft */
extern u_int8_t TWVShortHaul3[25][5];       /* T1 Short Haul 330-440 ft */
extern u_int8_t TWVShortHaul4[25][5];       /* T1 Short Haul 440-550 ft */
extern u_int8_t TWVShortHaul5[25][5];       /* T1 Short Haul 550-660 ft */
extern u_int8_t TWV_E1_75Ohm[25][5];        /* E1 75 Ohm */
extern u_int8_t TWV_E1_120Ohm[25][5];       /* E1 120 Ohm */
extern u_int32_t T1_Equalizer[256];    /* T1 Receiver Equalizer */
extern u_int32_t E1_Equalizer[256];    /* E1 Receiver Equalizer */

#endif                          /* _INC_COMET_TBLS_H_ */
