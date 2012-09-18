/* ced_ioctl.h
 IOCTL calls for the CED1401 driver
 Copyright (C) 2010 Cambridge Electronic Design Ltd
 Author Greg P Smith (greg@ced.co.uk)

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef __CED_IOCTL_H__
#define __CED_IOCTL_H__
#include <asm/ioctl.h>

/// dma modes, only MODE_CHAR and MODE_LINEAR are used in this driver
#define MODE_CHAR           0
#define MODE_LINEAR         1

/****************************************************************************
** TypeDefs
*****************************************************************************/

typedef unsigned short TBLOCKENTRY; // index the blk transfer table 0-7

typedef struct TransferDesc
{
    long long   lpvBuff;            // address of transfer area (for 64 or 32 bit)
    unsigned int dwLength;          // length of the area
    TBLOCKENTRY wAreaNum;           // number of transfer area to set up
    short       eSize;              // element size - is tohost flag for circular
} TRANSFERDESC;

typedef TRANSFERDESC*   LPTRANSFERDESC;

typedef struct TransferEvent
{
    unsigned int dwStart;           // offset into the area
    unsigned int dwLength;          // length of the region
    unsigned short wAreaNum;        // the area number
    unsigned short wFlags;          // bit 0 set for toHost
    int iSetEvent;                  // could be dummy in LINUX
} TRANSFEREVENT;

#define MAX_TRANSFER_SIZE  0x4000       /* Maximum data bytes per IRP */
#define MAX_AREA_LENGTH    0x100000     /* Maximum size of transfer area */
#define MAX_TRANSAREAS 8                /* definitions for dma set up  */

typedef struct TGetSelfTest
{
    int code;                           // self-test error code
    int x,y;                            // additional information
} TGET_SELFTEST;

/// Debug block used for several commands. Not all fields are used for all commands.
typedef struct TDbgBlock
{
    int iAddr;                          // the address in the 1401
    int iRepeats;                       // number of repeats
    int iWidth;                         // width in bytes 1, 2, 4
    int iDefault;                       // default value
    int iMask;                          // mask to apply
    int iData;                          // data for poke, result for peek
} TDBGBLOCK;

/// Used to collect information about a circular block from the device driver
typedef struct TCircBlock
{
    unsigned int nArea;                 // the area to collect information from
    unsigned int dwOffset;              // offset into the area to the available block
    unsigned int dwSize;                // size of the area
} TCIRCBLOCK;

/// Used to clollect the 1401 status
typedef struct TCSBlock
{
    unsigned int uiState;
    unsigned int uiError;
} TCSBLOCK;

// As seen by the user, an ioctl call looks like:
// int ioctl(int fd, unsigned long cmd, char* argp);
// We will then have all sorts of variants on this that can be used
// to pass stuff to our driver. We will generate macros for each type
// of call so as to provide some sort of type safety in the calling:
#define CED_MAGIC_IOC 0xce

// NBNB: READ and WRITE are from the point of view of the device, not user.
typedef struct ced_ioc_string
{
    int nChars;
    char buffer[256];
} CED_IOC_STRING;

#define  IOCTL_CED_SENDSTRING(n)        _IOC(_IOC_WRITE, CED_MAGIC_IOC, 2, n)

#define  IOCTL_CED_RESET1401            _IO(CED_MAGIC_IOC, 3)
#define  IOCTL_CED_GETCHAR              _IO(CED_MAGIC_IOC, 4)
#define  IOCTL_CED_SENDCHAR             _IO(CED_MAGIC_IOC, 5)
#define  IOCTL_CED_STAT1401             _IO(CED_MAGIC_IOC, 6)
#define  IOCTL_CED_LINECOUNT            _IO(CED_MAGIC_IOC, 7)
#define  IOCTL_CED_GETSTRING(nMax)      _IOC(_IOC_READ, CED_MAGIC_IOC, 8, nMax)

#define  IOCTL_CED_SETTRANSFER          _IOW(CED_MAGIC_IOC, 11, TRANSFERDESC)
#define  IOCTL_CED_UNSETTRANSFER        _IO(CED_MAGIC_IOC, 12)
#define  IOCTL_CED_SETEVENT             _IOW(CED_MAGIC_IOC,13, TRANSFEREVENT)
#define  IOCTL_CED_GETOUTBUFSPACE       _IO(CED_MAGIC_IOC, 14)
#define  IOCTL_CED_GETBASEADDRESS       _IO(CED_MAGIC_IOC, 15)
#define  IOCTL_CED_GETDRIVERREVISION    _IO(CED_MAGIC_IOC, 16)

#define  IOCTL_CED_GETTRANSFER          _IOR(CED_MAGIC_IOC,17, TGET_TX_BLOCK)
#define  IOCTL_CED_KILLIO1401           _IO(CED_MAGIC_IOC,18)
#define  IOCTL_CED_BLKTRANSSTATE        _IO(CED_MAGIC_IOC,19)

#define  IOCTL_CED_STATEOF1401          _IO(CED_MAGIC_IOC,23)
#define  IOCTL_CED_GRAB1401             _IO(CED_MAGIC_IOC,25)
#define  IOCTL_CED_FREE1401             _IO(CED_MAGIC_IOC,26)
#define  IOCTL_CED_STARTSELFTEST        _IO(CED_MAGIC_IOC,31)
#define  IOCTL_CED_CHECKSELFTEST        _IOR(CED_MAGIC_IOC,32, TGET_SELFTEST)
#define  IOCTL_CED_TYPEOF1401           _IO(CED_MAGIC_IOC,33)
#define  IOCTL_CED_TRANSFERFLAGS        _IO(CED_MAGIC_IOC,34)

#define  IOCTL_CED_DBGPEEK              _IOW(CED_MAGIC_IOC,35, TDBGBLOCK)
#define  IOCTL_CED_DBGPOKE              _IOW(CED_MAGIC_IOC,36, TDBGBLOCK)
#define  IOCTL_CED_DBGRAMPDATA          _IOW(CED_MAGIC_IOC,37, TDBGBLOCK)
#define  IOCTL_CED_DBGRAMPADDR          _IOW(CED_MAGIC_IOC,38, TDBGBLOCK)
#define  IOCTL_CED_DBGGETDATA           _IOR(CED_MAGIC_IOC,39, TDBGBLOCK)
#define  IOCTL_CED_DBGSTOPLOOP          _IO(CED_MAGIC_IOC,40)
#define  IOCTL_CED_FULLRESET            _IO(CED_MAGIC_IOC,41)
#define  IOCTL_CED_SETCIRCULAR          _IOW(CED_MAGIC_IOC,42, TRANSFERDESC)
#define  IOCTL_CED_GETCIRCBLOCK         _IOWR(CED_MAGIC_IOC,43, TCIRCBLOCK)
#define  IOCTL_CED_FREECIRCBLOCK        _IOWR(CED_MAGIC_IOC,44, TCIRCBLOCK)
#define  IOCTL_CED_WAITEVENT            _IO(CED_MAGIC_IOC, 45)
#define  IOCTL_CED_TESTEVENT            _IO(CED_MAGIC_IOC, 46)

#ifndef __KERNEL__
// If nothing said about return value, it is a U14ERR_... error code (U14ERR_NOERROR for none)
inline int CED_SendString(int fh, const char* szText, int n){return ioctl(fh, IOCTL_CED_SENDSTRING(n), szText);}

inline int CED_Reset1401(int fh){return ioctl(fh, IOCTL_CED_RESET1401);}

inline int CED_GetChar(int fh){return ioctl(fh, IOCTL_CED_GETCHAR);}
// Return the singe character or a -ve error code.

inline int CED_Stat1401(int fh){return ioctl(fh, IOCTL_CED_STAT1401);}
// Return character count in input buffer

inline int CED_SendChar(int fh, char c){return ioctl(fh, IOCTL_CED_SENDCHAR, c);}

inline int CED_LineCount(int fh){return ioctl(fh, IOCTL_CED_LINECOUNT);}

inline int CED_GetString(int fh, char* szText, int nMax){return ioctl(fh, IOCTL_CED_GETSTRING(nMax), szText);}
// return the count of characters returned. If the string was terminated by CR or 0, then the 0 is part
// of the count. Otherwise, we will add a zero if there is room, but it is not included in the count.
// The return value is 0 if there was nothing to read.

inline int CED_GetOutBufSpace(int fh){return ioctl(fh, IOCTL_CED_GETOUTBUFSPACE);}
// returns space in the output buffer.

inline int CED_GetBaseAddress(int fh){return ioctl(fh, IOCTL_CED_GETBASEADDRESS);}
// This always returns -1 as not implemented.

inline int CED_GetDriverRevision(int fh){return ioctl(fh, IOCTL_CED_GETDRIVERREVISION);}
// returns the major revision <<16 | minor revision.

inline int CED_SetTransfer(int fh, TRANSFERDESC* pTD){return ioctl(fh, IOCTL_CED_SETTRANSFER, pTD);}

inline int CED_UnsetTransfer(int fh, int nArea){return ioctl(fh, IOCTL_CED_UNSETTRANSFER, nArea);}

inline int CED_SetEvent(int fh, TRANSFEREVENT* pTE){return ioctl(fh, IOCTL_CED_SETEVENT, pTE);}

inline int CED_GetTransfer(int fh, TGET_TX_BLOCK* pTX){return ioctl(fh, IOCTL_CED_GETTRANSFER, pTX);}

inline int CED_KillIO1401(int fh){return ioctl(fh, IOCTL_CED_KILLIO1401);}

inline int CED_BlkTransState(int fh){return ioctl(fh, IOCTL_CED_BLKTRANSSTATE);}
// returns 0 if no active DMA, 1 if active

inline int CED_StateOf1401(int fh){return ioctl(fh, IOCTL_CED_STATEOF1401);}

inline int CED_Grab1401(int fh){return ioctl(fh, IOCTL_CED_GRAB1401);}
inline int CED_Free1401(int fh){return ioctl(fh, IOCTL_CED_FREE1401);}

inline int CED_StartSelfTest(int fh){return ioctl(fh, IOCTL_CED_STARTSELFTEST);}
inline int CED_CheckSelfTest(int fh, TGET_SELFTEST* pGST){return ioctl(fh, IOCTL_CED_CHECKSELFTEST, pGST);}

inline int CED_TypeOf1401(int fh){return ioctl(fh, IOCTL_CED_TYPEOF1401);}
inline int CED_TransferFlags(int fh){return ioctl(fh, IOCTL_CED_TRANSFERFLAGS);}

inline int CED_DbgPeek(int fh, TDBGBLOCK* pDB){return ioctl(fh, IOCTL_CED_DBGPEEK, pDB);}
inline int CED_DbgPoke(int fh, TDBGBLOCK* pDB){return ioctl(fh, IOCTL_CED_DBGPOKE, pDB);}
inline int CED_DbgRampData(int fh, TDBGBLOCK* pDB){return ioctl(fh, IOCTL_CED_DBGRAMPDATA, pDB);}
inline int CED_DbgRampAddr(int fh, TDBGBLOCK* pDB){return ioctl(fh, IOCTL_CED_DBGRAMPADDR, pDB);}
inline int CED_DbgGetData(int fh, TDBGBLOCK* pDB){return ioctl(fh, IOCTL_CED_DBGGETDATA, pDB);}
inline int CED_DbgStopLoop(int fh){return ioctl(fh, IOCTL_CED_DBGSTOPLOOP);}

inline int CED_FullReset(int fh){return ioctl(fh, IOCTL_CED_FULLRESET);}

inline int CED_SetCircular(int fh, TRANSFERDESC* pTD){return ioctl(fh, IOCTL_CED_SETCIRCULAR, pTD);}
inline int CED_GetCircBlock(int fh, TCIRCBLOCK* pCB){return ioctl(fh, IOCTL_CED_GETCIRCBLOCK, pCB);}
inline int CED_FreeCircBlock(int fh, TCIRCBLOCK* pCB){return ioctl(fh, IOCTL_CED_FREECIRCBLOCK, pCB);}

inline int CED_WaitEvent(int fh, int nArea, int msTimeOut){return ioctl(fh, IOCTL_CED_WAITEVENT, (nArea & 0xff)|(msTimeOut << 8));}
inline int CED_TestEvent(int fh, int nArea){return ioctl(fh, IOCTL_CED_TESTEVENT, nArea);}
#endif

#ifdef NOTWANTEDYET
#define  IOCTL_CED_REGCALLBACK          _IO(CED_MAGIC_IOC,9)    // Not used
#define  IOCTL_CED_GETMONITORBUF        _IO(CED_MAGIC_IOC,10)   // Not used

#define  IOCTL_CED_BYTECOUNT            _IO(CED_MAGIC_IOC,20)   // Not used
#define  IOCTL_CED_ZEROBLOCKCOUNT       _IO(CED_MAGIC_IOC,21)   // Not used
#define  IOCTL_CED_STOPCIRCULAR         _IO(CED_MAGIC_IOC,22)   // Not used

#define  IOCTL_CED_REGISTERS1401        _IO(CED_MAGIC_IOC,24)   // Not used
#define  IOCTL_CED_STEP1401             _IO(CED_MAGIC_IOC,27)   // Not used
#define  IOCTL_CED_SET1401REGISTERS     _IO(CED_MAGIC_IOC,28)   // Not used
#define  IOCTL_CED_STEPTILL1401         _IO(CED_MAGIC_IOC,29)   // Not used
#define  IOCTL_CED_SETORIN              _IO(CED_MAGIC_IOC,30)   // Not used

#endif

// __CED_IOCTL_H__
#endif
