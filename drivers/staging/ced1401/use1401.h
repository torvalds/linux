/****************************************************************************
** use1401.h
** Copyright (C) Cambridge Electronic Design Ltd, 1992-2010
** Authors: Paul Cox, Tim Bergel, Greg Smith
** See CVS for revisions.
**
** Because the size of a long is different between 32-bit and 64-bit on some
** systems, we avoid this in this interface.
****************************************************************************/
#ifndef __USE1401_H__
#define __USE1401_H__
#include "machine.h"

/*  Some definitions to make things compatible. If you want to use Use1401 directly */
/*   from a Windows program you should define U14_NOT_DLL, in which case you also */
/*   MUST make sure that your application startup code calls U14InitLib(). */
/*  DLL_USE1401 is defined when you are building the Use1401 dll, not otherwise. */
#ifdef _IS_WINDOWS_
#ifndef U14_NOT_DLL
#ifdef DLL_USE1401
#define U14API(retType) (retType DllExport __stdcall)
#else
#define U14API(retType) (retType DllImport __stdcall)
#endif
#endif

#define U14ERRBASE -500
#define U14LONG long
#endif

#ifdef LINUX
#define U14ERRBASE -1000
#define U14LONG int
#endif

#ifdef _QT
#ifndef U14_NOT_DLL
#undef U14API
#define U14API(retType) (retType __declspec(dllimport) __stdcall)
#endif
#undef U14LONG
#define U14LONG int
#endif

#ifndef U14API
#define U14API(retType) retType
#endif

#ifndef U14LONG
#define U14LONG long
#endif

/* Error codes: We need them here as user space can see them. */
#define U14ERR_NOERROR        0             /*  no problems */

/* Device error codes, but these don't need to be extended - a succession is assumed */
#define U14ERR_STD            4              /*  standard 1401 connected */
#define U14ERR_U1401          5              /*  u1401 connected */
#define U14ERR_PLUS           6              /*  1401 plus connected */
#define U14ERR_POWER          7              /*  Power1401 connected */
#define U14ERR_U14012         8              /*  u1401 mkII connected */
#define U14ERR_POWER2         9
#define U14ERR_U14013        10
#define U14ERR_POWER3        11

/* NBNB Error numbers need shifting as some linux error codes start at 512 */
#define U14ERR(n)             (n+U14ERRBASE)
#define U14ERR_OFF            U14ERR(0)      /* 1401 there but switched off    */
#define U14ERR_NC             U14ERR(-1)     /* 1401 not connected             */
#define U14ERR_ILL            U14ERR(-2)     /* if present it is ill           */
#define U14ERR_NOIF           U14ERR(-3)     /* I/F card missing               */
#define U14ERR_TIME           U14ERR(-4)     /* 1401 failed to come ready      */
#define U14ERR_BADSW          U14ERR(-5)     /* I/F card bad switches          */
#define U14ERR_PTIME          U14ERR(-6)     /* 1401plus failed to come ready  */
#define U14ERR_NOINT          U14ERR(-7)     /* couldn't grab the int vector   */
#define U14ERR_INUSE          U14ERR(-8)     /* 1401 is already in use         */
#define U14ERR_NODMA          U14ERR(-9)     /* couldn't get DMA channel       */
#define U14ERR_BADHAND        U14ERR(-10)    /* handle provided was bad        */
#define U14ERR_BAD1401NUM     U14ERR(-11)    /* 1401 number provided was bad   */

#define U14ERR_NO_SUCH_FN     U14ERR(-20)    /* no such function               */
#define U14ERR_NO_SUCH_SUBFN  U14ERR(-21)    /* no such sub function           */
#define U14ERR_NOOUT          U14ERR(-22)    /* no room in output buffer       */
#define U14ERR_NOIN           U14ERR(-23)    /* no input in buffer             */
#define U14ERR_STRLEN         U14ERR(-24)    /* string longer than buffer      */
#define U14ERR_ERR_STRLEN     U14ERR(-24)    /* string longer than buffer      */
#define U14ERR_LOCKFAIL       U14ERR(-25)    /* failed to lock memory          */
#define U14ERR_UNLOCKFAIL     U14ERR(-26)    /* failed to unlock memory        */
#define U14ERR_ALREADYSET     U14ERR(-27)    /* area already set up            */
#define U14ERR_NOTSET         U14ERR(-28)    /* area not set up                */
#define U14ERR_BADAREA        U14ERR(-29)    /* illegal area number            */
#define U14ERR_FAIL           U14ERR(-30)    /* we failed for some other reason*/

#define U14ERR_NOFILE         U14ERR(-40)    /* command file not found         */
#define U14ERR_READERR        U14ERR(-41)    /* error reading command file     */
#define U14ERR_UNKNOWN        U14ERR(-42)    /* unknown command                */
#define U14ERR_HOSTSPACE      U14ERR(-43)    /* not enough host space to load  */
#define U14ERR_LOCKERR        U14ERR(-44)    /* could not lock resource/command*/
#define U14ERR_CLOADERR       U14ERR(-45)    /* CLOAD command failed           */

#define U14ERR_TOXXXERR       U14ERR(-60)    /* tohost/1401 failed             */
#define U14ERR_NO386ENH       U14ERR(-80)    /* not 386 enhanced mode          */
#define U14ERR_NO1401DRIV     U14ERR(-81)    /* no device driver               */
#define U14ERR_DRIVTOOOLD     U14ERR(-82)    /* device driver too old          */

#define U14ERR_TIMEOUT        U14ERR(-90)    /* timeout occurred               */

#define U14ERR_BUFF_SMALL     U14ERR(-100)   /* buffer for getstring too small */
#define U14ERR_CBALREADY      U14ERR(-101)   /* there is already a callback    */
#define U14ERR_BADDEREG       U14ERR(-102)   /* bad parameter to deregcallback */
#define U14ERR_NOMEMORY       U14ERR(-103)   /* no memory for allocation       */

#define U14ERR_DRIVCOMMS      U14ERR(-110)   /* failed talking to driver       */
#define U14ERR_OUTOFMEMORY    U14ERR(-111)   /* needed memory and couldnt get it*/

/* / 1401 type codes. */
#define U14TYPE1401           0           /* standard 1401                  */
#define U14TYPEPLUS           1           /* 1401 plus                      */
#define U14TYPEU1401          2           /* u1401                          */
#define U14TYPEPOWER          3           /* power1401                      */
#define U14TYPEU14012         4           /* u1401 mk II                    */
#define U14TYPEPOWER2         5           /* power1401 mk II                */
#define U14TYPEU14013         6           /* u1401-3                        */
#define U14TYPEPOWER3         7           /* power1401-3                    */
#define U14TYPEUNKNOWN        -1          /* dont know                      */

/* Transfer flags to allow driver capabilities to be interrogated */

/* Constants for transfer flags */
#define U14TF_USEDMA          1           /* Transfer flag for use DMA      */
#define U14TF_MULTIA          2           /* Transfer flag for multi areas  */
#define U14TF_FIFO            4           /* for FIFO interface card        */
#define U14TF_USB2            8           /* for USB2 interface and 1401    */
#define U14TF_NOTIFY          16          /* for event notifications        */
#define U14TF_SHORT           32          /* for PCI can short cycle        */
#define U14TF_PCI2            64          /* for new PCI card 1401-70       */
#define U14TF_CIRCTH          128         /* Circular-mode to host          */
#define U14TF_DIAG            256         /* Diagnostics/debug functions    */
#define U14TF_CIRC14          512         /* Circular-mode to 1401          */

/* Definitions of element sizes for DMA transfers - to allow byte-swapping */
#define ESZBYTES              0           /* BYTE element size value        */
#define ESZWORDS              1           /* unsigned short element size value        */
#define ESZLONGS              2           /* long element size value        */
#define ESZUNKNOWN            0           /* unknown element size value     */

/* These define required access types for the debug/diagnostics function */
#define BYTE_SIZE             1           /* 8-bit access                   */
#define WORD_SIZE             2           /* 16-bit access                  */
#define LONG_SIZE             3           /* 32-bit access                  */

/* Stuff used by U14_GetTransfer */
#define GET_TX_MAXENTRIES  257          /* (max length / page size + 1) */

#ifdef _IS_WINDOWS_
#pragma pack(1)

typedef struct                          /* used for U14_GetTransfer results */
{                                          /* Info on a single mapped block */
	U14LONG physical;
	U14LONG size;
} TXENTRY;

typedef struct TGetTxBlock              /* used for U14_GetTransfer results */
{                                               /* matches structure in VXD */
	U14LONG size;
	U14LONG linear;
	short   seg;
	short   reserved;
	short   avail;                      /* number of available entries */
	short   used;                       /* number of used entries */
	TXENTRY entries[GET_TX_MAXENTRIES];       /* Array of mapped block info */
} TGET_TX_BLOCK;

typedef TGET_TX_BLOCK *LPGET_TX_BLOCK;

#pragma pack()
#endif

#ifdef LINUX
typedef struct                          /* used for U14_GetTransfer results */
{                                       /* Info on a single mapped block */
	long long physical;
	long     size;
} TXENTRY;

typedef struct TGetTxBlock              /* used for U14_GetTransfer results */
{                                       /* matches structure in VXD */
	long long linear;                    /* linear address */
	long     size;                       /* total size of the mapped area, holds id when called */
	short    seg;                        /* segment of the address for Win16 */
	short    reserved;
	short    avail;                      /* number of available entries */
	short    used;                       /* number of used entries */
	TXENTRY  entries[GET_TX_MAXENTRIES]; /* Array of mapped block info */
} TGET_TX_BLOCK;
#endif

#ifdef __cplusplus
extern "C" {
#endif

U14API(int)   U14WhenToTimeOut(short hand);         /*  when to timeout in ms */
U14API(short)	U14PassedTime(int iTime);             /*  non-zero if iTime passed */

U14API(short)	U14LastErrCode(short hand);

U14API(short)	U14Open1401(short n1401);
U14API(short)	U14Close1401(short hand);
U14API(short)	U14Reset1401(short hand);
U14API(short)	U14ForceReset(short hand);
U14API(short)	U14TypeOf1401(short hand);
U14API(short)	U14NameOf1401(short hand, char *pBuf, unsigned short wMax);

U14API(short)	U14Stat1401(short hand);
U14API(short)	U14CharCount(short hand);
U14API(short)	U14LineCount(short hand);

U14API(short)	U14SendString(short hand, const char *pString);
U14API(short)	U14GetString(short hand, char *pBuffer, unsigned short wMaxLen);
U14API(short)	U14SendChar(short hand, char cChar);
U14API(short)	U14GetChar(short hand, char *pcChar);

U14API(short)	U14LdCmd(short hand, const char *command);
U14API(DWORD) U14Ld(short hand, const char *vl, const char *str);

U14API(short)	U14SetTransArea(short hand, unsigned short wArea, void *pvBuff,
					DWORD dwLength, short eSz);
U14API(short)	U14UnSetTransfer(short hand, unsigned short wArea);
U14API(short)	U14SetTransferEvent(short hand, unsigned short wArea, BOOL bEvent,
					BOOL bToHost, DWORD dwStart, DWORD dwLength);
U14API(int)   U14TestTransferEvent(short hand, unsigned short wArea);
U14API(int)   U14WaitTransferEvent(short hand, unsigned short wArea, int msTimeOut);
U14API(short)	U14GetTransfer(short hand, TGET_TX_BLOCK *pTransBlock);

U14API(short)	U14ToHost(short hand, char *pAddrHost, DWORD dwSize, DWORD dw1401,
								short eSz);
U14API(short)	U14To1401(short hand, const char *pAddrHost, DWORD dwSize, DWORD dw1401,
								short eSz);

U14API(short)	U14SetCircular(short hand, unsigned short wArea, BOOL bToHost, void *pvBuff,
							DWORD dwLength);

U14API(int)   U14GetCircBlk(short hand, unsigned short wArea, DWORD *pdwOffs);
U14API(int)   U14FreeCircBlk(short hand, unsigned short wArea, DWORD dwOffs, DWORD dwSize,
							DWORD *pdwOffs);

U14API(short)	U14StrToLongs(const char *pszBuff, U14LONG *palNums, short sMaxLongs);
U14API(short)	U14LongsFrom1401(short hand, U14LONG *palBuff, short sMaxLongs);

U14API(void)  U14SetTimeout(short hand, int lTimeout);
U14API(int)   U14GetTimeout(short hand);
U14API(short)	U14OutBufSpace(short hand);
U14API(int)   U14BaseAddr1401(short hand);
U14API(int)   U14DriverVersion(short hand);
U14API(int)   U14DriverType(short hand);
U14API(short)	U14DriverName(short hand, char *pBuf, unsigned short wMax);
U14API(short)	U14GetUserMemorySize(short hand, DWORD *pMemorySize);
U14API(short)	U14KillIO1401(short hand);

U14API(short)	U14BlkTransState(short hand);
U14API(short)	U14StateOf1401(short hand);

U14API(short)	U14Grab1401(short hand);
U14API(short)	U14Free1401(short hand);
U14API(short)	U14Peek1401(short hand, DWORD dwAddr, int nSize, int nRepeats);
U14API(short)	U14Poke1401(short hand, DWORD dwAddr, DWORD dwValue, int nSize, int nRepeats);
U14API(short)	U14Ramp1401(short hand, DWORD dwAddr, DWORD dwDef, DWORD dwEnable, int nSize, int nRepeats);
U14API(short)	U14RampAddr(short hand, DWORD dwDef, DWORD dwEnable, int nSize, int nRepeats);
U14API(short)	U14StopDebugLoop(short hand);
U14API(short)	U14GetDebugData(short hand, U14LONG *plValue);

U14API(short)	U14StartSelfTest(short hand);
U14API(short)	U14CheckSelfTest(short hand, U14LONG *pData);
U14API(short)	U14TransferFlags(short hand);
U14API(void)  U14GetErrorString(short nErr, char *pStr, unsigned short wMax);
U14API(int)   U14MonitorRev(short hand);
U14API(void)  U14CloseAll(void);

U14API(short)	U14WorkingSet(DWORD dwMinKb, DWORD dwMaxKb);
U14API(int)   U14InitLib(void);

#ifdef __cplusplus
}
#endif

#endif /* End of ifndef __USE1401_H__ */

