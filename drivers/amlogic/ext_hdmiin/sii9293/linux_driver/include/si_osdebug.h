/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the
 * GNU General Public License for more details.
*/

#ifndef __SI_OSDEBUG_H__
#define __SI_OSDEBUG_H__


void SiiOsDebugPrint(const char *pFileName, uint32_t iLineNum, uint8_t printFlags, const char *pszFormat, ...);

#define SII_DEBUG_CONFIG_NO_FILE_LINE
#ifdef SII_DEBUG_CONFIG_NO_FILE_LINE //(
#define SII_DEBUG_PRINT(printFlags,...) SiiOsDebugPrint(NULL,0,printFlags,__VA_ARGS__)
#define DEBUG_PRINT(printFlags,...) SiiOsDebugPrint(NULL,0,printFlags,__VA_ARGS__)
#else //)(
#define SII_DEBUG_PRINT(printFlags,...) SiiOsDebugPrint(__FILE__,__LINE__,printFlags,__VA_ARGS__)
#define DEBUG_PRINT(printFlags,...) SiiOsDebugPrint(__FILE__,__LINE__,printFlags,__VA_ARGS__)
#endif //)

#define SII_PRINT_FULL(printFlags,...) SiiOsDebugPrint(__FILE__,__LINE__,printFlags,__VA_ARGS__)
#define SII_PRINT(printFlags,...) SiiOsDebugPrint(NULL,0,printFlags,__VA_ARGS__)

#define MSG_ALWAYS              0x00
#define MSG_ERR                 0x00
#define MSG_STAT                0x01
#define MSG_DBG                 0x02
#define MSG_PRINT_ALL           0xFF

#endif // #ifndef __SI_OSDEBUG_H__
