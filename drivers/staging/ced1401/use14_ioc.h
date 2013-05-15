/* use14_ioc.h
** definitions of use1401 module stuff that is shared between use1401 and the driver.
** Copyright (C) Cambridge Electronic Design Limited 2010
** Author Greg P Smith
************************************************************************************/
#ifndef __USE14_IOC_H__
#define __USE14_IOC_H__

#define  MAX_TRANSAREAS   8   /* The number of transfer areas supported by driver */

#define i386
#include "winioctl.h"                   /* needed so we can access driver   */

/*
** Defines for IOCTL functions to ask driver to perform. These must be matched
** in both use1401 and in the driver. The IOCTL code contains a command
** identifier, plus other information about the device, the type of access
** with which the file must have been opened, and the type of buffering.
** The IOCTL function codes from 0x80 to 0xFF are for developer use.
*/
#define  FILE_DEVICE_CED1401    0x8001
						FNNUMBASE              0x800

#define  U14_OPEN1401            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE,               \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_CLOSE1401           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+1,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SENDSTRING          CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+2,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS

#define  U14_RESET1401           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+3,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETCHAR             CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+4,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SENDCHAR            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+5,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STAT1401            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+6,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_LINECOUNT           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+7,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETSTRING           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+8,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_REGCALLBACK         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+9,             \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETMONITORBUF       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+10,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SETTRANSFER         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+11,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_UNSETTRANSFER       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+12,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SETTRANSEVENT       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+13,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETOUTBUFSPACE      CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+14,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETBASEADDRESS      CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+15,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETDRIVERREVISION   CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+16,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETTRANSFER         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+17,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_KILLIO1401          CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+18,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_BLKTRANSSTATE       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+19,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_BYTECOUNT           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+20,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_ZEROBLOCKCOUNT      CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+21,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STOPCIRCULAR        CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+22,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STATEOF1401         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+23,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_REGISTERS1401       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+24,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GRAB1401            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+25,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_FREE1401            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+26,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STEP1401            CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+27,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SET1401REGISTERS    CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+28,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STEPTILL1401        CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+29,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SETORIN             CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+30,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_STARTSELFTEST       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+31,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_CHECKSELFTEST       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+32,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_TYPEOF1401          CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+33,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_TRANSFERFLAGS       CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+34,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGPEEK             CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+35,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGPOKE             CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+36,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGRAMPDATA         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+37,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGRAMPADDR         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+38,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGGETDATA          CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+39,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_DBGSTOPLOOP         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+40,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_FULLRESET           CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+41,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_SETCIRCULAR         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+42,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_GETCIRCBLK          CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+43,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

#define  U14_FREECIRCBLK         CTL_CODE(FILE_DEVICE_CED1401,     \
						FNNUMBASE+44,            \
						METHOD_BUFFERED,         \
						FILE_ANY_ACCESS)

/*--------------- Structures that are shared with the driver ------------- */
#pragma pack(1)

typedef struct                  /* used for get/set standard 1401 registers */
{
	short   sPC;
	char    A;
	char    X;
	char    Y;
	char    stat;
	char    rubbish;
} T1401REGISTERS;

typedef union     /* to communicate with 1401 driver status & control funcs */
{
	char           chrs[22];
	short          ints[11];
	long           longs[5];
	T1401REGISTERS registers;
} TCSBLOCK;

typedef TCSBLOCK*  LPTCSBLOCK;

typedef struct paramBlk
{
	 short       sState;
	 TCSBLOCK    csBlock;
} PARAMBLK;

typedef PARAMBLK*   PPARAMBLK;

typedef struct TransferDesc          /* Structure and type for SetTransArea */
{
	WORD        wArea;            /* number of transfer area to set up       */
	void FAR *lpvBuff;          /* address of transfer area                */
	DWORD       dwLength;         /* length of area to set up                */
	short       eSize;            /* size to move (for swapping on MAC)      */
} TRANSFERDESC;

typedef TRANSFERDESC FAR *LPTRANSFERDESC;

/* This is the structure used to set up a transfer area */
typedef struct VXTransferDesc    /* use1401.c and use1432x.x use only       */
{
	WORD        wArea;            /* number of transfer area to set up       */
	WORD        wAddrSel;         /* 16 bit selector for area                */
	DWORD       dwAddrOfs;        /* 32 bit offset for area start            */
	DWORD       dwLength;         /* length of area to set up                */
} VXTRANSFERDESC;

#pragma pack()

#endif
