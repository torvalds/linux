/****************************************************************************
** use1401.c
** Copyright (C) Cambridge Electronic Design Ltd, 1992-2010
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** Contact CED: Cambridge Electronic Design Limited, Science Park, Milton Road
**              Cambridge, CB6 0FE.
**              www.ced.co.uk
**              greg@ced.co.uk
**
**  Title:      USE1401.C
**  Version:    4.00
**  Author:     Paul Cox, Tim Bergel, Greg Smith
**
** The code was vigorously pruned in DEC 2010 to remove the macintosh options
** and to get rid of the 16-bit support. It has also been aligned with the
** Linux version. See CVS for revisions. This will work for Win 9x onwards.
****************************************************************************
**
** Notes on Windows interface to driver
** ************************************
**
** Under Windows 9x and NT, Use1401 uses DeviceIoControl to get access to
** the 1401 driver. This has parameters for the device handle, the function
** code, an input pointer and byte count, an output pointer and byte count
** and a pointer to a unsigned int to hold the output byte count. Note that input
** and output are from the point-of-view of the driver, so the output stuff
** is used to read values from the 1401, not send to the 1401. The use of
** these parameters varies with the function in use and the operating
** system; there are five separate DIOC calls SendString, GetString and
** SetTransferArea all have their own specialised calls, the rest use the
** Status1401 or Control1401 functions.
**
** There are two basic styles of DIOC call used, one for Win9x VxD drivers
** and one for NT Kernel-mode and WDM drivers (see below for tables showing
** the different parameters used. The array bUseNTDIOC[] selects between
** these two calling styles.
**
** Function codes
** In Win3.x, simple function codes from 0 to 40 were used, shifted left 8
** bits with a sub-function code in the lower 8 bits. These were also used
** in the Windows 95 driver, though we had to add 1 to the code value to
** avoid problems (Open from CreateFile is zero), and the sub-function code
** is now unused. We found that this gave some problems with Windows 98
** as the function code values are reserved by microsoft, so we switched to
** using the NT function codes instead. The NT codes are generated using the
** CTL_CODE macro, essentially this gives 0x80012000 | (func << 2), where
** func is the original 0 to 34 value. The driver will handle both types of
** code and Use1432 only uses the NT codes if it knows the driver is new
** enough. The array bUseNTCodes[] holds flags on the type of codes required.
** GPS/TDB Dec 2010: we removed the bUseNTCodes array as this is always true
** as we no longer support ancient versions.
**
** The CreateFile and CloseFile function calls are also handled
** by DIOC, using the special function codes 0 and -1 respectively.
**
** Input pointer and buffer size
** These are intended for data sent to the device driver. In nearly all cases
** they are unused in calls to the Win95 driver, the NT driver uses them
** for all information sent to the driver. The table below shows the pointer
** and byte count used for the various calls:
**
**                      Win 95                  Win NT
** SendString           NULL, 0                 pStr, nStr
** GetString            NULL, 0                 NULL, 0
** SetTransferArea      pBuf, nBuf (unused?)    pDesc, nDesc
** GetTransfer          NULL, 0                 NULL, 0
** Status1401           NULL, 0                 NULL, 0
** Control1401          NULL, 0                 pBlk, nBlk
**
** pStr and nStr are pointers to a char buffer and the buffer length for
** string I/O, note that these are temporary buffers owned by the DLL, not
** application memory, pBuf and nBuf are the transfer area buffer (I think
** these are unused), pDesc and nDesc are the TRANSFERDESC structure, pBlk
** and nBlk are the TCSBLOCK structure.
**
**
** Output pointer and buffer size
** These are intended for data read from the device driver. These are used
** for almost all information sent to the Win95 driver, the NT driver uses
** them for information read from the driver, chiefly the error code. The
** table below shows the pointer and byte count used for the various calls:
**
**                      Win 95                  Win NT
** SendString           pStr, nStr              pPar, nPar
** GetString            pStr, nStr+2            pStr, nStr+2
** SetTransferArea      pDesc, nDesc            pPar, nPar
** GetTransfer          pGet, nGet              pGet, nGet
** Status1401           pBlk, nBlk              pPar, nPar
** Control1401          pBlk, nBlk              pPar, nPar
**
** pStr and nStr are pointers to a char buffer and the buffer length for
** string I/O, the +2 for GetString refers to two spare bytes at the start
** used to hold the string length and returning an error code for NT. Note
** again that these are (and must be) DLL-owned temporary buffers. pPar
** and nPar are a PARAM structure used in NT (it holds an error code and a 
** TCSBLOCK structure). pDesc and nDesc are the VXTRANSFERDESC structure,
** pBlk and nBlk are the TCSBLOCK structure. pGet and nGet indicate the
** TGET_TX_BLOCK structure used for GetTransfer.
**
**
** The output byte count
** Both drivers return the output buffer size here, regardless of the actual
** bytes output. This is used to check that we did get through to the driver.
**
** Multiple 1401s
** **************
**
** We have code that tries to support the use of multiple 1401s, but there
** are problems: The lDriverVersion and lDriverType variables are global, not
** per-1401 (a particular problem as the U14 functions that use them don't
** have a hand parameter). In addition, the mechansim for finding a free
** 1401 depends upon the 1401 device driver open operation failing if it's
** already in use, which doesn't always happen, particularly with the VxDs.
** The code in TryToOpen tries to fix this by relying on TYPEOF1401 to detect
** the 1401-in-use state - the VxDs contain special code to help this. This is
** working OK but multiple 1401 support works better with the Win2000 drivers.
**
** USB driver
** **********
**
** The USB driver, which runs on both Win98 and NT2000, uses the NT-style
** calling convention, both for the DIOC codes and the DIOC parameters. The
** TryToOpen function has been altered to look for an NT driver first in
** the appropriate circumstances, and to set the driver DIOC flags up in
** the correct state.
**
** Adding a new 1401 type - now almost nothing to do
** *************************************************
**
** The 1401 types are defined by a set of U14TYPExxxx codes in USE1401.H.
** You should add a new one of these to keep things tidy for applications.
**
** DRIVERET_MAX (below) specifies the maximum allowed type code from the
** 1401 driver; I have set this high to accommodate as yet undesigned 1401
** types. Similarly, as long as the command file names follow the ARM,
** ARN, ARO sequence, these are calculated by the ExtForType function, so
** you don't need to do anything here either.
**
** Version number
** **************
** The new U14InitLib() function returns 0 if the OS is incapable of use,
** otherwise is returns the version of the USE1401 library. This is done
** in three parts: Major(31-24).Minor(23-16).Revision.(15-0) (brackets are
** the bits used). The Major number starts at 2 for the first revision with
** the U14InitLib() function. Changes to the Major version means that we
** have broken backwards compatibility. Minor number changes mean that we
** have added new functionality that does not break backwards compatibility.
** we starts at 0. Revision changes mean we have fixed something. Each index
** returns to 0 when a higher one changes.
*/
#define U14LIB_MAJOR 4
#define U14LIB_MINOR 0
#define U14LIB_REVISION 0
#define U14LIB_VERSION ((U14LIB_MAJOR<<24) | (U14LIB_MINOR<<16) | U14LIB_REVISION)

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "USE1401.H"

#ifdef _IS_WINDOWS_
#include <io.h>
#include <windows.h>
#pragma warning(disable: 4100) /* Disable "Unused formal parameter" warning */
#include <assert.h>
#include "process.h"


#define sprintf wsprintf
#define PATHSEP '\\'
#define PATHSEPSTR "\\"
#define DEFCMDPATH "\\1401\\"   // default command path if all else fails
#define MINDRIVERMAJREV 1       // minimum driver revision level we need
#define __packed                // does nothing in Windows

#include "use14_ioc.h"          // links to device driver stuff
#endif

#ifdef LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/time.h>
#include <sched.h>
#include <libgen.h>
#define PATHSEP '/'
#define PATHSEPSTR "/"
#define DEFCMDPATH "/var/1401/" // default command path if all else fails
#define MINDRIVERMAJREV 2       // minimum driver revision level we need

#include "ced_ioctl.h"          // links to device driver stuff
#endif

#define MAX1401         8       // The number of 1401s that can be supported

/*
** These are the 1401 type codes returned by the driver, they are a slightly
** odd sequence & start for reasons of compatibility with the DOS driver.
** The maximum code value is the upper limit of 1401 device types.
*/
#define DRIVRET_STD     4       // Codes for 1401 types matching driver values
#define DRIVRET_U1401   5       // This table does not need extending, as
#define DRIVRET_PLUS    6       // we can calculate values now.
#define DRIVRET_POWER   7       // but we need all of these values still
#define DRIVRET_MAX     26      // Maximum tolerated code - future designs

/*
** These variables store data that will be used to generate the last
** error string. For now, a string will hold the 1401 command file name.
*/
static char szLastName[20];     // additional text information

/*
** Information stored per handle. NBNB, driverType and DriverVersion used to be
** only stored once for all handles... i.e. nonsensical. This change means that
** three U14...() calls now include handles that were previously void. We have
** set a constructor and a destructor call for the library (see the end) to
** initialise important structures, or call use1401_load().
*/
static short asDriverType[MAX1401] = {0};
static int lLastDriverVersion = U14ERR_NO1401DRIV;
static int lLastDriverType = U14TYPEUNKNOWN;
static int alDriverVersion[MAX1401];            // version/type of each driver
static int alTimeOutPeriod[MAX1401];            // timeout time in milliseconds
static short asLastRetCode[MAX1401];            // last code from a fn call
static short asType1401[MAX1401] = {0};         // The type of the 1401
static BOOL abGrabbed[MAX1401] = {0};           // Flag for grabbed, set true by grab1401
static int iAttached = 0;                       // counts process attaches so can let go

#ifdef _IS_WINDOWS_
/****************************************************************************
** Windows NT Specific Variables and internal types
****************************************************************************/
static HANDLE aHand1401[MAX1401] = {0};         // handles for 1401s
static HANDLE aXferEvent[MAX1401] = {0};        // transfer events for the 1401s
static LPVOID apAreas[MAX1401][MAX_TRANSAREAS]; // Locked areas
static unsigned int  auAreas[MAX1401][MAX_TRANSAREAS]; // Size of locked areas
static BOOL   bWindows9x = FALSE;               // if we are Windows 95 or better
#ifdef _WIN64
#define USE_NT_DIOC(ind) TRUE
#else
static BOOL   abUseNTDIOC[MAX1401];             // Use NT-style DIOC parameters */
#define USE_NT_DIOC(ind) abUseNTDIOC[ind]
#endif

#endif

#ifdef LINUX
static int aHand1401[MAX1401] = {0};    // handles for 1401s
#define INVALID_HANDLE_VALUE 0          // to avoid code differences
#endif


/*
** The CmdHead relates to backwards compatibility with ancient Microsoft (and Sperry!)
** versions of BASIC, where this header was needed so we could load a command into
** memory.
*/
#pragma pack(1)                 // pack our structure
typedef struct CmdHead          // defines header block on command
{                               // for PC commands
   char   acBasic[5];           // BASIC information - needed to align things
   unsigned short   wBasicSz;             // size as seen by BASIC
   unsigned short   wCmdSize;             // size of the following info
} __packed CMDHEAD;
#pragma pack()                  // back to normal

/*
** The rest of the header looks like this...
**  int    iRelPnt;             relocation pointer... actual start
**  char   acName[8];           string holding the command name
**  BYTE   bMonRev;             monitor revision level
**  BYTE   bCmdRev;             command revision level
*/

typedef CMDHEAD *LPCMDHEAD;     // pointer to a command header

#define  MAXSTRLEN   255        // maximum string length we use
#define  TOHOST      FALSE
#define  TO1401      TRUE

static short CheckHandle(short h)
{
    if ((h < 0) || (h >= MAX1401))  // must be legal range...
        return U14ERR_BADHAND;
    if (aHand1401[h] <= 0)          // must be open
        return U14ERR_BADHAND;
    return U14ERR_NOERROR;
}

#ifdef _IS_WINDOWS_
/****************************************************************************
** U14Status1401    Used for functions which do not pass any data in but
**                  get data back
****************************************************************************/
static short U14Status1401(short sHand, LONG lCode, TCSBLOCK* pBlk)
{
    unsigned int dwBytes = 0;

    if ((sHand < 0) || (sHand >= MAX1401))  /* Check parameters */
        return U14ERR_BADHAND;
#ifndef _WIN64
    if (!USE_NT_DIOC(sHand)) 
    {   /* Windows 9x DIOC methods? */
        if (DeviceIoControl(aHand1401[sHand], lCode, NULL, 0, pBlk,sizeof(TCSBLOCK),&dwBytes,NULL))
            return (short)((dwBytes>=sizeof(TCSBLOCK)) ? U14ERR_NOERROR : U14ERR_DRIVCOMMS);
        else
            return (short)GetLastError();
    }
    else
#endif
    {                                       /* Windows NT or USB driver */
        PARAMBLK rWork;
        rWork.sState = U14ERR_DRIVCOMMS;
        if (DeviceIoControl(aHand1401[sHand], lCode, NULL, 0, &rWork,sizeof(PARAMBLK),&dwBytes,NULL) &&
            (dwBytes >= sizeof(PARAMBLK)))
        {
            *pBlk = rWork.csBlock;
            return rWork.sState;
        }
    }

    return U14ERR_DRIVCOMMS;
}

/****************************************************************************
** U14Control1401   Used for functions which pass data in and only expect
**                  an error code back
****************************************************************************/
static short U14Control1401(short sHand, LONG lCode, TCSBLOCK* pBlk)
{
    unsigned int dwBytes = 0;

    if ((sHand < 0) || (sHand >= MAX1401))              /* Check parameters */
        return U14ERR_BADHAND;

#ifndef _WIN64
    if (!USE_NT_DIOC(sHand))                    
    {                            /* Windows 9x DIOC methods */
        if (DeviceIoControl(aHand1401[sHand], lCode, NULL, 0, pBlk, sizeof(TCSBLOCK), &dwBytes, NULL))
            return (short)(dwBytes >= sizeof(TCSBLOCK) ? U14ERR_NOERROR : U14ERR_DRIVCOMMS);
        else
            return (short)GetLastError();
    }
    else
#endif
    {                            /* Windows NT or later */
        PARAMBLK rWork;
        rWork.sState = U14ERR_DRIVCOMMS;
        if (DeviceIoControl(aHand1401[sHand], lCode, pBlk, sizeof(TCSBLOCK), &rWork, sizeof(PARAMBLK), &dwBytes, NULL) &&
            (dwBytes >= sizeof(PARAMBLK)))
            return rWork.sState;
    }

    return U14ERR_DRIVCOMMS;
}
#endif

/****************************************************************************
** SafeTickCount
** Gets time in approximately units of a millisecond.
*****************************************************************************/
static long SafeTickCount()
{
#ifdef _IS_WINDOWS_
    return GetTickCount();
#endif
#ifdef LINUX
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec*1000 + tv.tv_usec/1000);
#endif
}

/****************************************************************************
** A utility routine to get the command file extension for a given type
** of 1401. We assume the type code is vaguely legal.
****************************************************************************/
static int ExtForType(short sType, char* szExt)
{
    szExt[0] = 0;                       /* Default return is a blank string */
    switch (sType)
    {
    case U14TYPE1401: strcpy(szExt, ".CMD");  break;    // Standard 1401
    case U14TYPEPLUS: strcpy(szExt, ".GXC");  break;    // 1401 plus
    default:               // All others are in a predictable sequence
        strcpy(szExt, ".ARM");
            szExt[3] = (char)('M' + sType - U14TYPEU1401);
        if (szExt[3] > 'Z')             // Wrap round to ARA after ARZ
                szExt[3] = (char)(szExt[3] - 26);
    }
    return 0;
}

/****************************************************************************
**   U14WhenToTimeOut
**       Returns the time to time out in time units suitable for the machine
** we are running on  ie millsecs for pc/linux, or Mac/
****************************************************************************/
U14API(int) U14WhenToTimeOut(short hand)
{
    int iNow = SafeTickCount();
    if ((hand >= 0) && (hand < MAX1401))
        iNow += alTimeOutPeriod[hand];
    return iNow;
}

/****************************************************************************
** U14PassedTime
** Returns non zero if the timed passed in has been passed 0 if not
****************************************************************************/
U14API(short) U14PassedTime(int lCheckTime)
{
    return (short)((SafeTickCount()-lCheckTime) > 0);
}

/****************************************************************************
** TranslateString
** Tidies up string that U14GetString returns. Converts all the commas in a
** string to spaces. Removes terminating CR character. May do more in future.
****************************************************************************/
static void TranslateString(char* pStr)
{
    int i = 0;
    while (pStr[i])
    {
        if (pStr[i] == ',')
            pStr[i] = ' ';              /* convert comma to space */
        ++i;
    }

    if ((i > 0) && (pStr[i-1] == '\n'))  /* kill terminating LF */
        pStr[i-1] = (char)0;
}

/****************************************************************************
** U14StrToLongs
** Converts a string to an array of longs and returns the number of values
****************************************************************************/
U14API(short) U14StrToLongs(const char* pszBuff, U14LONG *palNums, short sMaxLongs)
{
    unsigned short wChInd = 0;                // index into source
    short sLgInd = 0;               // index into result longs

    while (pszBuff[wChInd] &&       // until we get to end of string...
           (sLgInd < sMaxLongs))    // ...or filled the buffer
    {
        // Why not use a C Library converter?
        switch (pszBuff[wChInd])
        {
        case '-':
        case '0': case '1':   case '2': case '3':   case '4':
        case '5': case '6':   case '7': case '8':   case '9':
            {
                BOOL bDone = FALSE; // true at end of number
                int iSign = 1;      // sign of number
                long lValue = 0;

                while ((!bDone) && pszBuff[wChInd])
                {
                    switch (pszBuff[wChInd])
                    {
                    case '-':
                        iSign = -1; // swap sign
                        break;

                    case '0': case '1':   case '2': case '3':   case '4':
                    case '5': case '6':   case '7': case '8':   case '9':
                        lValue *= 10;   // move to next digit base 10
                        lValue += ((int)pszBuff[wChInd]-(int)'0');
                        break;

                    default:        // end of number
                        bDone = TRUE;
                        break;
                    }
                    wChInd++;       // move onto next character
                }
                palNums[sLgInd] = lValue * iSign;
                sLgInd++;
            }
            break;

        default:
            wChInd++;               // look at next char
            break;
        }
    }
    return (sLgInd);
}


/****************************************************************************
** U14LongsFrom1401
** Gets the next waiting line from the 1401 and converts it longs
** Returns the number of numbers read or an error.
****************************************************************************/
U14API(short) U14LongsFrom1401(short hand, U14LONG *palBuff, short sMaxLongs)
{
    char szWork[MAXSTRLEN];
    short sResult = U14GetString(hand, szWork, MAXSTRLEN);/* get reply from 1401   */
    if (sResult == U14ERR_NOERROR)                  /* if no error convert   */
        sResult = U14StrToLongs(szWork, palBuff, sMaxLongs);
    return sResult;
}

/****************************************************************************
**   U14CheckErr
**   Sends the ERR command to the 1401 and gets the result. Returns 0, a
**   negative error code, or the first error value.
****************************************************************************/
U14API(short) U14CheckErr(short hand)
{
    short sResult = U14SendString(hand, ";ERR;");
    if (sResult == U14ERR_NOERROR)
    {
        U14LONG er[3];
        sResult = U14LongsFrom1401(hand, er, 3);
        if (sResult > 0)
        {
            sResult = (short)er[0];        /* Either zero or an error value */
#ifdef _DEBUG
            if (er[0] != 0)
            {
                char szMsg[50];
                sprintf(szMsg, "U14CheckErr returned %d,%d\n", er[0], er[1]);
                OutputDebugString(szMsg);
            }
#endif
        }
        else
        {
            if (sResult == 0)
                sResult = U14ERR_TIMEOUT;      /* No numbers equals timeout */
        }
    }

    return sResult;
}

/****************************************************************************
** U14LastErrCode
** Returns the last code from the driver. This is for Windows where all calls
** go through the Control and Status routines, so we can save any error.
****************************************************************************/
U14API(short) U14LastErrCode(short hand)
{
    if ((hand < 0) || (hand >= MAX1401))
        return U14ERR_BADHAND;
    return asLastRetCode[hand];
}

/****************************************************************************
** U14SetTimeout
** Set the timeout period for 1401 comms in milliseconds
****************************************************************************/
U14API(void) U14SetTimeout(short hand, int lTimeOut)
{
    if ((hand < 0) || (hand >= MAX1401))
        return;
    alTimeOutPeriod[hand] = lTimeOut;
}

/****************************************************************************
** U14GetTimeout
** Get the timeout period for 1401 comms in milliseconds
****************************************************************************/
U14API(int) U14GetTimeout(short hand)
{
    if ((hand < 0) || (hand >= MAX1401))
        return U14ERR_BADHAND;
    return alTimeOutPeriod[hand];
}

/****************************************************************************
** U14OutBufSpace
** Return the space in the output buffer, or an error.
****************************************************************************/
U14API(short) U14OutBufSpace(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_GETOUTBUFSPACE,&csBlock);
    if (sErr == U14ERR_NOERROR)
        sErr = csBlock.ints[0];
    return sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_GetOutBufSpace(aHand1401[hand]) : sErr;
#endif
}


/****************************************************************************
** U14BaseAddr1401
** Returns the 1401 base address or an error code. Meaningless nowadays
****************************************************************************/
U14API(int) U14BaseAddr1401(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    int iError = U14Status1401(hand, U14_GETBASEADDRESS,&csBlock);
    if (iError == U14ERR_NOERROR)
        iError = csBlock.longs[0];
    return iError;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_GetBaseAddress(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14StateOf1401
** Return error state, either NOERROR or a negative code.
****************************************************************************/
U14API(short) U14StateOf1401(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_STATEOF1401, &csBlock);
    if (sErr == U14ERR_NOERROR)
    {
        sErr = csBlock.ints[0];      // returned 1401 state
        if ((sErr >= DRIVRET_STD) && (sErr <= DRIVRET_MAX))
            sErr = U14ERR_NOERROR;
    }
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        sErr = (short)CED_StateOf1401(aHand1401[hand]);
        if ((sErr >= DRIVRET_STD) && (sErr <= DRIVRET_MAX))
            sErr = U14ERR_NOERROR;
    }
#endif
    return sErr;
}

/****************************************************************************
** U14DriverVersion
** Returns the driver version. Hi word is major revision, low word is minor.
** If you pass in a silly handle (like -1), we return the version of the last
** driver we know of (to cope with PCI and no 1401 attached).
****************************************************************************/
U14API(int) U14DriverVersion(short hand)
{
    return CheckHandle(hand) != U14ERR_NOERROR ? lLastDriverVersion : alDriverVersion[hand];
}

/****************************************************************************
** U14DriverType
** Returns the driver type. The type, 0=ISA/NU-Bus, 1=PCI, 2=USB, 3=HSS
** If you pass in a silly handle (like -1), we return the type of the last
** driver we know of (to cope with PCI and no 1401 attached).
****************************************************************************/
U14API(int) U14DriverType(short hand)
{
    return CheckHandle(hand) != U14ERR_NOERROR ? lLastDriverType : asDriverType[hand];
}

/****************************************************************************
** U14DriverName
** Returns the driver type as 3 character (ISA, PCI, USB or HSS))
****************************************************************************/
U14API(short) U14DriverName(short hand, char* pBuf, unsigned short wMax)
{
    char* pName;
    *pBuf = 0;                             // Start off with a blank string
    switch (U14DriverType(hand))           // Results according to type
    {
    case 0:  pName = "ISA"; break;
    case 1:  pName = "PCI"; break;
    case 2:  pName = "USB"; break;
    case 3:  pName = "HSS"; break;
    default: pName = "???"; break;
    }
    strncpy(pBuf, pName, wMax);            // Copy the correct name to return

    return U14ERR_NOERROR;
}

/****************************************************************************
** U14BlkTransState
** Returns 0 no transfer in progress, 1 transfer in progress or an error code
****************************************************************************/
U14API(short) U14BlkTransState(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_BLKTRANSSTATE, &csBlock);
    if (sErr == U14ERR_NOERROR)
        sErr = csBlock.ints[0];
    return sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_BlkTransState(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14Grab1401
** Take control of the 1401 for diagnostics purposes. USB does nothing.
****************************************************************************/
U14API(short) U14Grab1401(short hand)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
#ifdef _IS_WINDOWS_
        if (abGrabbed[hand])            // 1401 should not have been grabbed
            sErr = U14ERR_ALREADYSET;   // Error code defined for this
        else
        {
            TCSBLOCK csBlock;
            sErr = U14Control1401(hand, U14_GRAB1401, &csBlock);
        }
#endif
#ifdef LINUX
        // 1401 should not have been grabbed
        sErr = abGrabbed[hand] ? U14ERR_ALREADYSET : CED_Grab1401(aHand1401[hand]);
#endif
        if (sErr == U14ERR_NOERROR)
            abGrabbed[hand] = TRUE;
    }
    return sErr;
}

/****************************************************************************
** U14Free1401
****************************************************************************/
U14API(short)  U14Free1401(short hand)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
#ifdef _IS_WINDOWS_
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
            TCSBLOCK csBlock;
            sErr = U14Control1401(hand, U14_FREE1401, &csBlock);
        }
        else
            sErr = U14ERR_NOTSET;
#endif
#ifdef LINUX
        // 1401 should not have been grabbed
        sErr = abGrabbed[hand] ? CED_Free1401(aHand1401[hand]) : U14ERR_NOTSET;
#endif
        if (sErr == U14ERR_NOERROR)
            abGrabbed[hand] = FALSE;
    }
    return sErr;
}

/****************************************************************************
** U14Peek1401
** DESCRIPTION  Cause the 1401 to do one or more peek operations.
** If lRepeats is zero, the loop will continue until U14StopDebugLoop
** is called. After the peek is done, use U14GetDebugData to retrieve
** the results of the peek.
****************************************************************************/
U14API(short) U14Peek1401(short hand, unsigned int dwAddr, int nSize, int nRepeats)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
#ifdef _IS_WINDOWS_
            TCSBLOCK csBlock;
            csBlock.longs[0] = (long)dwAddr;
            csBlock.longs[1] = nSize;
            csBlock.longs[2] = nRepeats;
            sErr = U14Control1401(hand, U14_DBGPEEK, &csBlock);
#endif
#ifdef LINUX
            TDBGBLOCK dbb;
            dbb.iAddr = (int)dwAddr;
            dbb.iWidth = nSize;
            dbb.iRepeats = nRepeats;
            sErr = CED_DbgPeek(aHand1401[hand], &dbb);
#endif
        }
        else
            sErr = U14ERR_NOTSET;
    }
    return sErr;
}

/****************************************************************************
** U14Poke1401
** DESCRIPTION  Cause the 1401 to do one or more poke operations.
** If lRepeats is zero, the loop will continue until U14StopDebugLoop
** is called.
****************************************************************************/
U14API(short) U14Poke1401(short hand, unsigned int dwAddr, unsigned int dwValue,
                                      int nSize, int nRepeats)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
#ifdef _IS_WINDOWS_
            TCSBLOCK csBlock;
            csBlock.longs[0] = (long)dwAddr;
            csBlock.longs[1] = nSize;
            csBlock.longs[2] = nRepeats;
            csBlock.longs[3] = (long)dwValue;
            sErr = U14Control1401(hand, U14_DBGPOKE, &csBlock);
#endif
#ifdef LINUX
            TDBGBLOCK dbb;
            dbb.iAddr = (int)dwAddr;
            dbb.iWidth = nSize;
            dbb.iRepeats= nRepeats;
            dbb.iData = (int)dwValue;
            sErr = CED_DbgPoke(aHand1401[hand], &dbb);
#endif
        }
        else
            sErr = U14ERR_NOTSET;
    }
    return sErr;
}

/****************************************************************************
** U14Ramp1401
** DESCRIPTION  Cause the 1401 to loop, writing a ramp to a location.
** If lRepeats is zero, the loop will continue until U14StopDebugLoop.
****************************************************************************/
U14API(short) U14Ramp1401(short hand, unsigned int dwAddr, unsigned int dwDef, unsigned int dwEnable,
                                      int nSize, int nRepeats)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
#ifdef _IS_WINDOWS_
            TCSBLOCK csBlock;
            csBlock.longs[0] = (long)dwAddr;
            csBlock.longs[1] = (long)dwDef;
            csBlock.longs[2] = (long)dwEnable;
            csBlock.longs[3] = nSize;
            csBlock.longs[4] = nRepeats;
            sErr = U14Control1401(hand, U14_DBGRAMPDATA, &csBlock);
#endif
#ifdef LINUX
            TDBGBLOCK dbb;
            dbb.iAddr = (int)dwAddr;
            dbb.iDefault = (int)dwDef;
            dbb.iMask = (int)dwEnable;
            dbb.iWidth = nSize;
            dbb.iRepeats = nRepeats;
            sErr = CED_DbgRampAddr(aHand1401[hand], &dbb);
#endif
        }
        else
            sErr = U14ERR_NOTSET;
    }
    return sErr;
}

/****************************************************************************
** U14RampAddr
** DESCRIPTION  Cause the 1401 to loop, reading from a ramping location.
** If lRepeats is zero, the loop will continue until U14StopDebugLoop
****************************************************************************/
U14API(short) U14RampAddr(short hand, unsigned int dwDef, unsigned int dwEnable,
                                      int nSize, int nRepeats)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
#ifdef _IS_WINDOWS_
            TCSBLOCK csBlock;
            csBlock.longs[0] = (long)dwDef;
            csBlock.longs[1] = (long)dwEnable;
            csBlock.longs[2] = nSize;
            csBlock.longs[3] = nRepeats;
            sErr = U14Control1401(hand, U14_DBGRAMPADDR, &csBlock);
#endif
#ifdef LINUX
            TDBGBLOCK dbb;
            dbb.iDefault = (int)dwDef;
            dbb.iMask = (int)dwEnable;
            dbb.iWidth = nSize;
            dbb.iRepeats = nRepeats;
            sErr = CED_DbgRampAddr(aHand1401[hand], &dbb);
#endif
        }
        else
            sErr = U14ERR_NOTSET;
    }
    return sErr;
}

/****************************************************************************
**    U14StopDebugLoop
**    DESCRIPTION Stops a peek\poke\ramp that, with repeats set to zero,
**    will otherwise continue forever.
****************************************************************************/
U14API(short) U14StopDebugLoop(short hand)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
#ifdef _IS_WINDOWS_
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
            TCSBLOCK csBlock;
            sErr = U14Control1401(hand, U14_DBGSTOPLOOP, &csBlock);
        }
        else
            sErr = U14ERR_NOTSET;
    }
#endif
#ifdef LINUX
        sErr = abGrabbed[hand] ? CED_DbgStopLoop(aHand1401[hand]) : U14ERR_NOTSET;
#endif
    return sErr;
}

/****************************************************************************
** U14GetDebugData
** DESCRIPTION Returns the result from a previous peek operation.
****************************************************************************/
U14API(short) U14GetDebugData(short hand, U14LONG* plValue)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
        if (abGrabbed[hand])    // 1401 should have been grabbed
        {
#ifdef _IS_WINDOWS_
            TCSBLOCK csBlock;
            sErr = U14Status1401(hand, U14_DBGGETDATA, &csBlock);
            if (sErr == U14ERR_NOERROR)
                *plValue = csBlock.longs[0];    // Return the data
#endif
#ifdef LINUX
            TDBGBLOCK dbb;
            sErr = CED_DbgGetData(aHand1401[hand], &dbb);
            if (sErr == U14ERR_NOERROR)
                *plValue = dbb.iData;                     /* Return the data */
#endif
        }
        else
            sErr = U14ERR_NOTSET;
    }
    return sErr;
}

/****************************************************************************
** U14StartSelfTest
****************************************************************************/
U14API(short) U14StartSelfTest(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    return U14Control1401(hand, U14_STARTSELFTEST, &csBlock);
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_StartSelfTest(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14CheckSelfTest
****************************************************************************/
U14API(short) U14CheckSelfTest(short hand, U14LONG *pData)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_CHECKSELFTEST, &csBlock);
    if (sErr == U14ERR_NOERROR)
    {
        pData[0] = csBlock.longs[0];        /* Return the results to user */
        pData[1] = csBlock.longs[1];
        pData[2] = csBlock.longs[2];
    }
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)                /* Check parameters */
    {
        TGET_SELFTEST gst;
        sErr = CED_CheckSelfTest(aHand1401[hand], &gst);
        if (sErr == U14ERR_NOERROR)
        {
            pData[0] = gst.code;        /* Return the results to user */
            pData[1] = gst.x;
            pData[2] = gst.y;
        }
    }
#endif
    return sErr;
}

/****************************************************************************
** U14GetUserMemorySize
****************************************************************************/
U14API(short) U14GetUserMemorySize(short hand, unsigned int *pMemorySize)
{
    // The original 1401 used a different command for getting the size
    short sErr = U14SendString(hand, (asType1401[hand] == U14TYPE1401) ? "MEMTOP;" : "MEMTOP,?;");
    *pMemorySize = 0;         /* if we get error then leave size set at 0  */
    if (sErr == U14ERR_NOERROR)
    {
        U14LONG alLimits[4];
        sErr = U14LongsFrom1401(hand, alLimits, 4);
        if (sErr > 0)              /* +ve sErr is the number of values read */
        {
            sErr = U14ERR_NOERROR;                  /* All OK, flag success */
            if (asType1401[hand] == U14TYPE1401)    /* result for standard  */
                *pMemorySize = alLimits[0] - alLimits[1]; /* memtop-membot */
            else
                *pMemorySize = alLimits[0];   /* result for plus or u1401  */
        }
    }
    return sErr;
}

/****************************************************************************
** U14TypeOf1401
** Returns the type of the 1401, maybe unknown
****************************************************************************/
U14API(short) U14TypeOf1401(short hand)
{
    if ((hand < 0) || (hand >= MAX1401))                /* Check parameters */
        return U14ERR_BADHAND;
    else
        return asType1401[hand];
}

/****************************************************************************
** U14NameOf1401
** Returns the type of the 1401 as a string, blank if unknown
****************************************************************************/
U14API(short) U14NameOf1401(short hand, char* pBuf, unsigned short wMax)
{
    short sErr = CheckHandle(hand);
    if (sErr == U14ERR_NOERROR)
    {
    char* pName;
    switch (asType1401[hand])               // Results according to type
    {
    case U14TYPE1401:  pName = "Std 1401"; break;
    case U14TYPEPLUS:  pName = "1401plus"; break;
    case U14TYPEU1401: pName = "micro1401"; break;
    case U14TYPEPOWER: pName = "Power1401"; break;
    case U14TYPEU14012:pName = "Micro1401 mk II"; break;
    case U14TYPEPOWER2:pName = "Power1401 mk II"; break;
    case U14TYPEU14013:pName = "Micro1401-3"; break;
    case U14TYPEPOWER3:pName = "Power1401-3"; break;
    default:           pName = "Unknown";
    }
        strncpy(pBuf, pName, wMax);
    }
    return sErr;
}

/****************************************************************************
** U14TransferFlags
**  Returns the driver block transfer flags.
**  Bits can be set - see U14TF_ constants in use1401.h
*****************************************************************************/
U14API(short) U14TransferFlags(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_TRANSFERFLAGS, &csBlock);
    return (sErr == U14ERR_NOERROR) ? (short)csBlock.ints[0] : sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_TransferFlags(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** GetDriverVersion
** Actually reads driver version from the device driver.
** Hi word is major revision, low word is minor revision.
** Assumes that hand has been checked. Also codes driver type in bits 24 up.
*****************************************************************************/
static int GetDriverVersion(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    int iErr = U14Status1401(hand, U14_GETDRIVERREVISION, &csBlock);
    if (iErr == U14ERR_NOERROR)
        iErr = csBlock.longs[0];
    return iErr;
#endif
#ifdef LINUX
    return CED_GetDriverRevision(aHand1401[hand]);
#endif
}

/****************************************************************************
** U14MonitorRev
** Returns the 1401 monitor revision number.
** The number returned is the minor revision - the part after the
** decimal point - plus the major revision times 1000.
*****************************************************************************/
U14API(int) U14MonitorRev(short hand)
{
    int iRev = 0;
    int iErr = CheckHandle(hand);
    if (iErr != U14ERR_NOERROR)                 // Check open and in use
        return iErr;

    if (asType1401[hand] >= U14TYPEPOWER2)      // The Power2 onwards can give us the monitor
    {                                           //  revision directly for all versions
        iErr = U14SendString(hand, "INFO,S,28;");
        if (iErr == U14ERR_NOERROR)
        {
            U14LONG lVals[2];                   // Read a single number being the revision
            iErr = U14LongsFrom1401(hand, lVals, 1);
            if (iErr > 0)
            {
                iErr = U14ERR_NOERROR;
                iRev = lVals[0];                // This is the minor part of the revision
                iRev += asType1401[hand] * 10000;
            }
        }
    }
    else
    {                                           /* Do it the hard way for older hardware */
        iErr = U14SendString(hand, ";CLIST;");     /* ask for command levels */
        if (iErr == U14ERR_NOERROR)
        {     
            while (iErr == U14ERR_NOERROR)
            {
                char wstr[50];
                iErr = U14GetString(hand, wstr, 45);
                if (iErr == U14ERR_NOERROR)
                {
                    char *pstr = strstr(wstr,"RESET");  /* Is this the RESET command? */
                    if ((pstr == wstr) && (wstr[5] == ' '))
                    {
                        char *pstr2;
                        size_t l;
                        pstr += 6;       /* Move past RESET and followinmg char */
                        l = strlen(pstr);       /* The length of text remaining */
                        while (((pstr[l-1] == ' ') || (pstr[l-1] == 13)) && (l > 0))
                        {
                            pstr[l-1] = 0;         /* Tidy up string at the end */
                            l--;                  /* by removing spaces and CRs */
                        }
                        pstr2 = strchr(pstr, '.');    /* Find the decimal point */
                        if (pstr2 != NULL)                /* If we found the DP */
                        {
                            *pstr2 = 0;                /* End pstr string at DP */
                            pstr2++;              /* Now past the decimal point */
                            iRev = atoi(pstr2);   /* Get the number after point */
                        }
                        iRev += (atoi(pstr) * 1000);    /* Add first bit * 1000 */
                    }
                    if ((strlen(wstr) < 3) && (wstr[0] == ' '))
                        break;              /* Spot the last line of results */
                }
            }
        }
    }
    if (iErr == U14ERR_NOERROR)            /* Return revision if no error */
        iErr = iRev;

    return iErr;
}

/****************************************************************************
** U14TryToOpen     Tries to open the 1401 number passed
**  Note : This will succeed with NT driver even if no I/F card or
**         1401 switched off, so we check state and close the driver
**         if the state is unsatisfactory in U14Open1401.
****************************************************************************/
#ifdef _IS_WINDOWS_
#define U14NAMEOLD "\\\\.\\CED_140%d"
#define U14NAMENEW "\\\\.\\CED%d"
static short U14TryToOpen(int n1401, long* plRetVal, short* psHandle)
{
    short sErr = U14ERR_NOERROR;
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    unsigned int dwErr = 0;
    int nFirst, nLast, nDev = 0;        /* Used for the search for a 1401 */
    BOOL bOldName = FALSE;               /* start by looking for a modern driver */

    if (n1401 == 0)                             /* If we need to look for a 1401 */
    {
        nFirst = 1;                             /* Set the search range */
        nLast = MAX1401;                        /* through all the possible 1401s */
    }
    else
        nFirst = nLast = n1401;                 /* Otherwise just one 1401 */

    while (hDevice == INVALID_HANDLE_VALUE)     /* Loop to try for a 1401 */
    {
        for (nDev = nFirst; nDev <= nLast; nDev++)
        {
            char szDevName[40];                 /* name of the device to open */
            sprintf(szDevName, bOldName ? U14NAMEOLD : U14NAMENEW, nDev);
            hDevice = CreateFile(szDevName, GENERIC_WRITE | GENERIC_READ,
                                 0, 0,          /* Unshared mode does nothing as this is a device */
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            if (hDevice != INVALID_HANDLE_VALUE)/* Check 1401 if opened */
            {
                TCSBLOCK csBlock;
                assert(aHand1401[nDev-1] == INVALID_HANDLE_VALUE);  // assert if already open
                aHand1401[nDev-1] = hDevice;    /* Save handle for now */

#ifndef _WIN64
                // Use DIOC method if not windows 9x or if using new device name
                abUseNTDIOC[nDev-1] = (BOOL)(!bWindows9x || !bOldName);
#endif
                sErr = U14Status1401((short)(nDev-1), U14_TYPEOF1401, &csBlock);
                if (sErr == U14ERR_NOERROR)
                {
                    *plRetVal = csBlock.ints[0];
                    if (csBlock.ints[0] == U14ERR_INUSE)/* Prevent multi opens */
                    {
                        CloseHandle(hDevice);   /* treat as open failure */
                        hDevice = INVALID_HANDLE_VALUE;
                        aHand1401[nDev-1] = INVALID_HANDLE_VALUE;
                        sErr = U14ERR_INUSE;
                    }
                    else
                        break;                  /* Exit from for loop on success */
                }
                else
                {
                    CloseHandle(hDevice);       /* Give up if func fails */
                    hDevice = INVALID_HANDLE_VALUE;
                    aHand1401[nDev-1] = INVALID_HANDLE_VALUE;
                }
            }
            else
            {
                unsigned int dwe = GetLastError();     /* Get error code otherwise */
                if ((dwe != ERROR_FILE_NOT_FOUND) || (dwErr == 0))
                    dwErr = dwe;                /* Ignore repeats of 'not found' */
            }
        }

        if ((hDevice == INVALID_HANDLE_VALUE) &&/* No device found, and... */
            (bWindows9x) &&                     /* ...old names are allowed, and... */
            (bOldName == FALSE))                /* ...not tried old names yet */
            bOldName = TRUE;                    /* Set flag and go round again */
        else
            break;                              /* otherwise that's all folks */
    }

    if (hDevice != INVALID_HANDLE_VALUE)        /* If we got our device open */
        *psHandle = (short)(nDev-1);            /* return 1401 number opened */
    else
    {
        if (dwErr == ERROR_FILE_NOT_FOUND)      /* Sort out the error codes */
            sErr = U14ERR_NO1401DRIV;           /* if file not found */
        else if (dwErr == ERROR_NOT_SUPPORTED)
            sErr = U14ERR_DRIVTOOOLD;           /* if DIOC not supported */
        else if (dwErr == ERROR_ACCESS_DENIED)
            sErr = U14ERR_INUSE;
        else
            sErr = U14ERR_DRIVCOMMS;            /* otherwise assume comms problem */
    }
    return sErr;
}
#endif
#ifdef LINUX
static short U14TryToOpen(int n1401, long* plRetVal, short* psHandle)
{
    short sErr = U14ERR_NOERROR;
    int fh = 0;                             // will be 1401 handle
    int iErr = 0;
    int nFirst, nLast, nDev = 0;            // Used for the search for a 1401

    if (n1401 == 0)                         // If we need to look for a 1401
    {
        nFirst = 1;                             /* Set the search range */
        nLast = MAX1401;                        /* through all the possible 1401s */
    }
    else
        nFirst = nLast = n1401;                 /* Otherwise just one 1401 */

    for (nDev = nFirst; nDev <= nLast; nDev++)
    {
        char szDevName[40];                 // name of the device to open
        sprintf(szDevName,"/dev/cedusb/%d", nDev-1);
        fh = open(szDevName, O_RDWR);       // can only be opened once at a time
        if (fh > 0)                         // Check 1401 if opened
        {
            int iType1401 = CED_TypeOf1401(fh); // get 1401 type
            aHand1401[nDev-1] = fh;         // Save handle for now
            if (iType1401 >= 0)
            {
                *plRetVal = iType1401;
                 break;                     // Exit from for loop on success
            }
            else
            {
                close(fh);                  // Give up if func fails
                fh = 0;
                aHand1401[nDev-1] = 0;
            }
        }
        else
        {
            if (((errno != ENODEV) && (errno != ENOENT)) || (iErr == 0))
                iErr = errno;                // Ignore repeats of 'not found'
        }
    }


    if (fh)                                 // If we got our device open
        *psHandle = (short)(nDev-1);        // return 1401 number opened
    else
    {
        if ((iErr == ENODEV) || (iErr == ENOENT)) // Sort out the error codes
            sErr = U14ERR_NO1401DRIV;       // if file not found
        else if (iErr == EBUSY)
            sErr = U14ERR_INUSE;
        else
            sErr = U14ERR_DRIVCOMMS;        // otherwise assume comms problem
    }

    return sErr;
}
#endif
/****************************************************************************
** U14Open1401
** Tries to get the 1401 for use by this application
*****************************************************************************/
U14API(short) U14Open1401(short n1401)
{
    long     lRetVal = -1;
    short    sErr;
    short    hand = 0;
    
    if ((n1401 < 0) || (n1401 > MAX1401))       // must check the 1401 number
        return U14ERR_BAD1401NUM;

    szLastName[0] = 0;          /* initialise the error info string */

    sErr = U14TryToOpen(n1401, &lRetVal, &hand);
    if (sErr == U14ERR_NOERROR)
    {
        long lDriverVersion = GetDriverVersion(hand);   /* get driver revision */
        long lDriverRev = -1;
		if (lDriverVersion >= 0)                    /* can use it if all OK */
        {
            lLastDriverType = (lDriverVersion >> 24) & 0x000000FF;
            asDriverType[hand] = (short)lLastDriverType;    /* Drv type */
            lLastDriverVersion = lDriverVersion & 0x00FFFFFF;
            alDriverVersion[hand] = lLastDriverVersion;     /* Actual version */
            lDriverRev = ((lDriverVersion>>16) & 0x00FF);    /* use hi word */
        }
        else
        {
            U14Close1401(hand);    /* If there is a problem we should close */
            return (short)lDriverVersion;      /* and return the error code */
        }
    
        if (lDriverRev < MINDRIVERMAJREV)       /* late enough version?     */
        {
            U14Close1401(hand);    /* If there is a problem we should close */
            return U14ERR_DRIVTOOOLD;           /* too old                  */
        }
    
        asLastRetCode[hand] = U14ERR_NOERROR; /* Initialise this 1401s info */
        abGrabbed[hand] = FALSE;          /* we are not in single step mode */
        U14SetTimeout(hand, 3000);      /* set 3 seconds as default timeout */

        switch (lRetVal)
        {
        case DRIVRET_STD:  asType1401[hand] = U14TYPE1401; break;      /* Some we do by hand */
        case DRIVRET_U1401:asType1401[hand] = U14TYPEU1401; break;
        case DRIVRET_PLUS: asType1401[hand] = U14TYPEPLUS; break;
        default:  // For the power upwards, we can calculate the codes
                if ((lRetVal >= DRIVRET_POWER) && (lRetVal <= DRIVRET_MAX))
                    asType1401[hand] = (short)(lRetVal - (DRIVRET_POWER - U14TYPEPOWER));
                else
                    asType1401[hand] = U14TYPEUNKNOWN;
                break;
            }
        U14KillIO1401(hand);                     /* resets the 1401 buffers */

        if (asType1401[hand] != U14TYPEUNKNOWN)   /* If all seems OK so far */
        {
            sErr = U14CheckErr(hand);        /* we can check 1401 comms now */
            if (sErr != 0)                       /* If this failed to go OK */
                U14Reset1401(hand); /* Reset the 1401 to try to sort it out */
        }

        sErr = U14StateOf1401(hand);/* Get the state of the 1401 for return */
        if (sErr == U14ERR_NOERROR)
            sErr = hand;                 /* return the handle if no problem */
        else
            U14Close1401(hand);    /* If there is a problem we should close */
    }

    return sErr;
}


/****************************************************************************
** U14Close1401
** Closes the 1401 so someone else can use it.
****************************************************************************/
U14API(short) U14Close1401(short hand)
{
    int j;
    int iAreaMask = 0;                          // Mask for active areas
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)                 // Check open and in use
        return sErr;

    for (j = 0; j<MAX_TRANSAREAS; ++j)
    {
        TGET_TX_BLOCK gtb;
        int iReturn = U14GetTransfer(hand, &gtb);   // get area information
        if (iReturn == U14ERR_NOERROR)          // ignore if any problem
            if (gtb.used)
                iAreaMask |= (1 << j);          // set a bit for each used area
    }

    if (iAreaMask)                              // if any areas are in use
    {
        U14Reset1401(hand);                     // in case an active transfer running
        for (j = 0; j < MAX_TRANSAREAS; ++j)    // Locate locked areas
            if (iAreaMask & (1 << j))           // And kill off any transfers
                U14UnSetTransfer(hand, (unsigned short)j);
    }

#ifdef _IS_WINDOWS_
    if (aXferEvent[hand])                       // if this 1401 has an open event handle
    {
        CloseHandle(aXferEvent[hand]);          // close down the handle
        aXferEvent[hand] = NULL;                // and mark it as gone
    }

    if (CloseHandle(aHand1401[hand]))
#endif
#ifdef LINUX
    if (close(aHand1401[hand]) == 0)            // make sure that close works
#endif
    {
        aHand1401[hand] = INVALID_HANDLE_VALUE;
        asType1401[hand] = U14TYPEUNKNOWN;
        return U14ERR_NOERROR;
    }
    else
        return U14ERR_BADHAND;     /* BUGBUG GetLastError() ? */
}

/**************************************************************************
**
** Look for open 1401s and attempt to close them down. 32-bit windows only.
**************************************************************************/
U14API(void) U14CloseAll(void)
{
    int i;
    for (i = 0; i < MAX1401; i++)       // Tidy up and make safe
        if (aHand1401[i] != INVALID_HANDLE_VALUE)
            U14Close1401((short)i);     // Last ditch close 1401
}

/****************************************************************************
** U14Reset1401
** Resets the 1401
****************************************************************************/
U14API(short) U14Reset1401(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    return U14Control1401(hand, U14_RESET1401, &csBlock);
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_Reset1401(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14ForceReset
**    Sets the 1401 full reset flag, so that next call to Reset1401 will
**     always cause a genuine reset.
*****************************************************************************/
U14API(short) U14ForceReset(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    return U14Control1401(hand, U14_FULLRESET, &csBlock);
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_FullReset(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14KillIO1401
**    Removes any pending IO from the buffers.
*****************************************************************************/
U14API(short) U14KillIO1401(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    return U14Control1401(hand, U14_KILLIO1401, &csBlock);
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_KillIO1401(aHand1401[hand]) : sErr;
#endif
}


/****************************************************************************
** U14SendString
** Send characters to the 1401
*****************************************************************************/
U14API(short) U14SendString(short hand, const char* pString)
{
    int nChars;                     // length we are sending
    long lTimeOutTicks;             // when to time out
    BOOL bSpaceToSend;              // space to send yet
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;

    nChars = (int)strlen(pString);  // get string length we want to send
    if (nChars > MAXSTRLEN)
        return U14ERR_STRLEN;       // String too long

#ifdef _IS_WINDOWS_
    // To get here we must wait for the buffer to have some space
    lTimeOutTicks = U14WhenToTimeOut(hand);
    do
    {
        bSpaceToSend = (BOOL)((long)U14OutBufSpace(hand) >= nChars);
    }
    while (!bSpaceToSend && !U14PassedTime(lTimeOutTicks));

    if (!bSpaceToSend)             /* Last-ditch attempt to avoid timeout */
    {           /* This can happen with anti-virus or network activity! */
        int i;
        for (i = 0; (i < 4) && (!bSpaceToSend); ++i)
        {
            Sleep(25);       /* Give other threads a chance for a while */
            bSpaceToSend = (BOOL)((long)U14OutBufSpace(hand) >= nChars);
        }
    }

    if (asLastRetCode[hand] == U14ERR_NOERROR)      /* no errors? */
    {
        if (bSpaceToSend)
        {
            PARAMBLK    rData;
            unsigned int       dwBytes;
            char        tstr[MAXSTRLEN+5];          /* Buffer for chars */

            if ((hand < 0) || (hand >= MAX1401))
                sErr = U14ERR_BADHAND;
            else
            {
                strcpy(tstr, pString);              /* Into local buf */
#ifndef _WIN64
                if (!USE_NT_DIOC(hand))             /* Using WIN 95 driver access? */
                {
                    int iOK = DeviceIoControl(aHand1401[hand], (unsigned int)U14_SENDSTRING,
                                    NULL, 0, tstr, nChars,
                                    &dwBytes, NULL);
                    if (iOK)
                        sErr = (dwBytes >= (unsigned int)nChars) ? U14ERR_NOERROR : U14ERR_DRIVCOMMS;
                    else
                        sErr = (short)GetLastError();
                }
                else
#endif
                {
                    int iOK = DeviceIoControl(aHand1401[hand],(unsigned int)U14_SENDSTRING,
                                    tstr, nChars,
                                    &rData,sizeof(PARAMBLK),&dwBytes,NULL);
                    if (iOK && (dwBytes >= sizeof(PARAMBLK)))
                        sErr = rData.sState;
                    else
                        sErr = U14ERR_DRIVCOMMS;
                }

                if (sErr != U14ERR_NOERROR) // If we have had a comms error
                    U14ForceReset(hand);    //  make sure we get real reset
            }

            return sErr;

        }
        else
        {
            U14ForceReset(hand);                //  make sure we get real reset
            return U14ERR_TIMEOUT;
        }
    }
    else
        return asLastRetCode[hand];
#endif
#ifdef LINUX
    // Just try to send it and see what happens!
    sErr = CED_SendString(aHand1401[hand], pString, nChars);
    if (sErr != U14ERR_NOOUT)       // if any result except "no room in output"...
    {
        if (sErr != U14ERR_NOERROR) // if a problem...
             U14ForceReset(hand);   // ...make sure we get real reset next time
        return sErr;                // ... we are done as nothing we can do
    }

    // To get here we must wait for the buffer to have some space
    lTimeOutTicks = U14WhenToTimeOut(hand);
    do
    {
        bSpaceToSend = (BOOL)((long)U14OutBufSpace(hand) >= nChars);
        if (!bSpaceToSend)
            sched_yield();          // let others have fun while we wait
    }
    while (!bSpaceToSend && !U14PassedTime(lTimeOutTicks));

    if (asLastRetCode[hand] == U14ERR_NOERROR)                /* no errors? */
    {
        if (bSpaceToSend)
        {
            sErr = CED_SendString(aHand1401[hand], pString, nChars);
            if (sErr != U14ERR_NOERROR) // If we have had a comms error
                U14ForceReset(hand);    //  make sure we get real reset
            return sErr;
        }
        else
        {
            U14ForceReset(hand);                //  make sure we get real reset
            return U14ERR_TIMEOUT;
        }
    }
    else
        return asLastRetCode[hand];
#endif
}

/****************************************************************************
** U14SendChar
** Send character to the 1401
*****************************************************************************/
U14API(short) U14SendChar(short hand, char cChar)
{
#ifdef _IS_WINDOWS_
    char sz[2]=" ";                         // convert to a string and send
    sz[0] = cChar;
    sz[1] = 0;
    return(U14SendString(hand, sz));        // String routines are better
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_SendChar(aHand1401[hand], cChar) : sErr;
#endif
}

/****************************************************************************
** U14GetString
** Get a string from the 1401. Returns a null terminated string.
** The string is all the characters up to the next CR in the buffer
** or the end of the buffer if that comes first. This only returns text
** if there is a CR in the buffer. The terminating CR character is removed.
** wMaxLen  Is the size of the buffer and must be at least 2 or an error.
** Returns  U14ERR_NOERR if OK with the result in the string or a negative
**          error code. Any error from the device causes us to set up for
**          a full reset.
****************************************************************************/
U14API(short) U14GetString(short hand, char* pBuffer, unsigned short wMaxLen)
{
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)             // If an error...
        return sErr;                        // ...bail out!

#ifdef _IS_WINDOWS_
    if (wMaxLen>1)                          // we need space for terminating 0
    {
        BOOL bLineToGet;                    // true when a line to get
        long lTimeOutTicks = U14WhenToTimeOut(hand);
        do
            bLineToGet = (BOOL)(U14LineCount(hand) != 0);
        while (!bLineToGet && !U14PassedTime(lTimeOutTicks));

        if (!bLineToGet)             /* Last-ditch attempt to avoid timeout */
        {           /* This can happen with anti-virus or network activity! */
            int i;
            for (i = 0; (i < 4) && (!bLineToGet); ++i)
            {
                Sleep(25);       /* Give other threads a chance for a while */
                bLineToGet = (BOOL)(U14LineCount(hand) != 0);
            }
        }

        if (bLineToGet)
        {
            if (asLastRetCode[hand] == U14ERR_NOERROR)     /* all ok so far */
            {
                unsigned int       dwBytes = 0;
                *((unsigned short *)pBuffer) = wMaxLen;       /* set up length */
#ifndef _WIN64
                if (!USE_NT_DIOC(hand))             /* Win 95 DIOC here ? */
                {
                    char tstr[MAXSTRLEN+5];         /* Buffer for Win95 chars */
                    int iOK;

                    if (wMaxLen > MAXSTRLEN)        /* Truncate length */
                        wMaxLen = MAXSTRLEN;    

                    *((unsigned short *)tstr) = wMaxLen;      /* set len */

                    iOK = DeviceIoControl(aHand1401[hand],(unsigned int)U14_GETSTRING,
                                    NULL, 0, tstr, wMaxLen+sizeof(short),
                                    &dwBytes, NULL);
                    if (iOK)                        /* Device IO control OK ? */
                    {
                        if (dwBytes >= 0)           /* If driver OK */
                        {
                            strcpy(pBuffer, tstr);
                            sErr = U14ERR_NOERROR;
                        }
                        else
                            sErr = U14ERR_DRIVCOMMS;
                    }
                    else
                    {
                        sErr = (short)GetLastError();
                        if (sErr > 0)               /* Errors are -ve */
                            sErr = (short)-sErr;
                    }
                }
                else
#endif
                {       /* Here for NT, the DLL must own the buffer */
                    HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE,wMaxLen+sizeof(short));
                    if (hMem)
                    {
                        char* pMem = (char*)GlobalLock(hMem);
                        if (pMem)
                        {
                            int iOK = DeviceIoControl(aHand1401[hand],(unsigned int)U14_GETSTRING,
                                            NULL, 0, pMem, wMaxLen+sizeof(short),
                                            &dwBytes, NULL);
                            if (iOK)                /* Device IO control OK ? */
                            {
                                if (dwBytes >= wMaxLen)
                                {
                                    strcpy(pBuffer, pMem+sizeof(short));
                                    sErr = *((SHORT*)pMem);
                                }
                                else
                                    sErr = U14ERR_DRIVCOMMS;
                            }
                            else
                                sErr = U14ERR_DRIVCOMMS;

                            GlobalUnlock(hMem);
                        }
                        else
                            sErr = U14ERR_OUTOFMEMORY;

                        GlobalFree(hMem);
                    }
                    else
                        sErr = U14ERR_OUTOFMEMORY;
                }

                if (sErr == U14ERR_NOERROR)     // If all OK...
                    TranslateString(pBuffer);   // ...convert any commas to spaces
                else                            // If we have had a comms error...
                    U14ForceReset(hand);        // ...make sure we get real reset

            }
            else
                sErr = asLastRetCode[hand];
        }
        else
        {
            sErr = U14ERR_TIMEOUT;
            U14ForceReset(hand);            //  make sure we get real reset
        }
    }
    else
        sErr = U14ERR_BUFF_SMALL;
    return sErr;
#endif
#ifdef LINUX
    if (wMaxLen>1)                          // we need space for terminating 0
    {
        BOOL bLineToGet;                    // true when a line to get
        long lTimeOutTicks = U14WhenToTimeOut(hand);
        do
        {
            bLineToGet = (BOOL)(U14LineCount(hand) != 0);
            if (!bLineToGet)
                sched_yield();

        }
        while (!bLineToGet && !U14PassedTime(lTimeOutTicks));

        if (bLineToGet)
        {
            sErr = CED_GetString(aHand1401[hand], pBuffer, wMaxLen-1);   // space for terminator
            if (sErr >=0)                    // if we were OK...
            {
                if (sErr >= wMaxLen)         // this should NOT happen unless
                    sErr = U14ERR_DRIVCOMMS; // ...driver Comms are very bad
                else
                {
                    pBuffer[sErr] = 0;      // OK, so terminate the string...
                    TranslateString(pBuffer);  // ...and convert commas to spaces.
                }
            }

            if (sErr < U14ERR_NOERROR)       // If we have had a comms error
                U14ForceReset(hand);            //  make sure we get real reset
        }
        else
        {
            sErr = U14ERR_TIMEOUT;
            U14ForceReset(hand);            //  make sure we get real reset
        }
    }
    else
        sErr = U14ERR_BUFF_SMALL;

    return sErr >= U14ERR_NOERROR ? U14ERR_NOERROR : sErr;
#endif
}

/****************************************************************************
** U14GetChar
** Get a character from the 1401. CR returned as CR.
*****************************************************************************/
U14API(short) U14GetChar(short hand, char* pcChar)
{
#ifdef _IS_WINDOWS_
    char sz[2];                             // read a very short string
    short sErr = U14GetString(hand, sz, 2); // read one char and nul terminate it
    *pcChar = sz[0];    // copy to result, NB char translate done by GetString
    if (sErr == U14ERR_NOERROR)
    {                                       // undo translate of CR to zero
        if (*pcChar == '\0')                // by converting back
            *pcChar = '\n';                 // What a nasty thing to have to do
    }
    return sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)             // Check parameters
        return sErr;
    sErr = CED_GetChar(aHand1401[hand]);    // get one char, if available
    if (sErr >= 0)
    {
        *pcChar = (char)sErr;              // return if it we have one
        return U14ERR_NOERROR;              // say all OK
    }
    else
        return sErr;
#endif
}

/****************************************************************************
** U14Stat1401
** Returns 0 for no lines or error or non zero for something waiting
****************************************************************************/
U14API(short) U14Stat1401(short hand)
{
    return ((short)(U14LineCount(hand) > 0));
}

/****************************************************************************
** U14CharCount
** Returns the number of characters in the input buffer
*****************************************************************************/
U14API(short) U14CharCount(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_STAT1401, &csBlock);
    if (sErr == U14ERR_NOERROR)
        sErr = csBlock.ints[0];
    return sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_Stat1401(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14LineCount
** Returns the number of CR characters in the input buffer
*****************************************************************************/
U14API(short) U14LineCount(short hand)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14Status1401(hand, U14_LINECOUNT, &csBlock);
    if (sErr == U14ERR_NOERROR)
        sErr = csBlock.ints[0];
    return sErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_LineCount(aHand1401[hand]) : sErr;
#endif
}

/****************************************************************************
** U14GetErrorString
** Converts error code supplied to a decent descriptive string.
** NOTE: This function may use some extra information stored
**       internally in the DLL. This information is stored on a
**       per-process basis, but it might be altered if you call
**       other functions after getting an error and before using
**       this function.
****************************************************************************/
U14API(void)  U14GetErrorString(short nErr, char* pStr, unsigned short wMax)
{
    char    wstr[150];

    switch (nErr)              /* Basically, we do this with a switch block */
    {
    case U14ERR_OFF:
        sprintf(wstr, "The 1401 is apparently switched off (code %d)", nErr);
        break;

    case U14ERR_NC:
        sprintf(wstr, "The 1401 is not connected to the interface card (code %d)", nErr);
        break;

    case U14ERR_ILL:
        sprintf(wstr, "The 1401 is not working correctly (code %d)", nErr);
        break;

    case U14ERR_NOIF:
        sprintf(wstr, "The 1401 interface card was not detected (code %d)", nErr);
        break;

    case U14ERR_TIME:
        sprintf(wstr, "The 1401 fails to become ready for use (code %d)", nErr);
        break;

    case U14ERR_BADSW:
        sprintf(wstr, "The 1401 interface card jumpers are incorrect (code %d)", nErr);
        break;

    case U14ERR_NOINT:
        sprintf(wstr, "The 1401 interrupt is not available for use (code %d)", nErr);
        break;

    case U14ERR_INUSE:
        sprintf(wstr, "The 1401 is already in use by another program (code %d)", nErr);
        break;

    case U14ERR_NODMA:
        sprintf(wstr, "The 1401 DMA channel is not available for use (code %d)", nErr);
        break;

    case U14ERR_BADHAND:
        sprintf(wstr, "The application supplied an incorrect 1401 handle (code %d)", nErr);
        break;

    case U14ERR_BAD1401NUM:
        sprintf(wstr, "The application used an incorrect 1401 number (code %d)", nErr);
        break;

    case U14ERR_NO_SUCH_FN:
        sprintf(wstr, "The code passed to the 1401 driver is invalid (code %d)", nErr);
        break;

    case U14ERR_NO_SUCH_SUBFN:
        sprintf(wstr, "The sub-code passed to the 1401 driver is invalid (code %d)", nErr);
        break;

    case U14ERR_NOOUT:
        sprintf(wstr, "No room in buffer for characters for the 1401 (code %d)", nErr);
        break;

    case U14ERR_NOIN:
        sprintf(wstr, "No characters from the 1401 are available (code %d)", nErr);
        break;

    case U14ERR_STRLEN:
        sprintf(wstr, "A string sent to or read from the 1401 was too long (code %d)", nErr);
        break;

    case U14ERR_LOCKFAIL:
        sprintf(wstr, "Failed to lock host memory for data transfer (code %d)", nErr);
        break;

    case U14ERR_UNLOCKFAIL:
        sprintf(wstr, "Failed to unlock host memory after data transfer (code %d)", nErr);
        break;

    case U14ERR_ALREADYSET:
        sprintf(wstr, "The transfer area used is already set up (code %d)", nErr);
        break;

    case U14ERR_NOTSET:
        sprintf(wstr, "The transfer area used has not been set up (code %d)", nErr);
        break;

    case U14ERR_BADAREA:
        sprintf(wstr, "The transfer area number is incorrect (code %d)", nErr);
        break;

    case U14ERR_NOFILE:
        sprintf(wstr, "The command file %s could not be opened (code %d)", szLastName, nErr);
        break;

    case U14ERR_READERR:
        sprintf(wstr, "The command file %s could not be read (code %d)", szLastName, nErr);
        break;

    case U14ERR_UNKNOWN:
        sprintf(wstr, "The %s command resource could not be found (code %d)", szLastName, nErr);
        break;

    case U14ERR_HOSTSPACE:
        sprintf(wstr, "Unable to allocate memory for loading command %s (code %d)", szLastName, nErr);
        break;

    case U14ERR_LOCKERR:
        sprintf(wstr, "Unable to lock memory for loading command %s (code %d)", szLastName, nErr);
        break;

    case U14ERR_CLOADERR:
        sprintf(wstr, "Error in loading command %s, bad command format (code %d)", szLastName, nErr);
        break;

    case U14ERR_TOXXXERR:
        sprintf(wstr, "Error detected after data transfer to or from the 1401 (code %d)", nErr);
        break;

    case U14ERR_NO386ENH:
        sprintf(wstr, "Windows 3.1 is not running in 386 enhanced mode (code %d)", nErr);
        break;

    case U14ERR_NO1401DRIV:
        sprintf(wstr, "The 1401 device driver cannot be found (code %d)\nUSB:   check plugged in and powered\nOther: not installed?", nErr);
        break;

    case U14ERR_DRIVTOOOLD:
        sprintf(wstr, "The 1401 device driver is too old for use (code %d)", nErr);
        break;

    case U14ERR_TIMEOUT:
        sprintf(wstr, "Character transmissions to the 1401 timed-out (code %d)", nErr);
        break;

    case U14ERR_BUFF_SMALL:
        sprintf(wstr, "Buffer for text from the 1401 was too small (code %d)", nErr);
        break;

    case U14ERR_CBALREADY:
        sprintf(wstr, "1401 monitor callback already set up (code %d)", nErr);
        break;

    case U14ERR_BADDEREG:
        sprintf(wstr, "1401 monitor callback deregister invalid (code %d)", nErr);
        break;

    case U14ERR_DRIVCOMMS:
        sprintf(wstr, "1401 device driver communications failed (code %d)", nErr);
        break;

    case U14ERR_OUTOFMEMORY:
        sprintf(wstr, "Failed to allocate or lock memory for text from the 1401 (code %d)", nErr);
        break;

    default:
        sprintf(wstr, "1401 error code %d returned; this code is unknown", nErr);
        break;

    }
    if ((unsigned short)strlen(wstr) >= wMax-1)  /* Check for string being too long */
        wstr[wMax-1] = 0;                          /* and truncate it if so */
    strcpy(pStr, wstr);                       /* Return the error string */
}

/***************************************************************************
** U14GetTransfer
** Get a TGET_TX_BLOCK describing a transfer area (held in the block)
***************************************************************************/
U14API(short) U14GetTransfer(short hand, TGET_TX_BLOCK *pTransBlock)
{
    short sErr = CheckHandle(hand);
#ifdef _IS_WINDOWS_
    if (sErr == U14ERR_NOERROR)
    { 
        unsigned int dwBytes = 0;
        BOOL bOK = DeviceIoControl(aHand1401[hand], (unsigned int)U14_GETTRANSFER, NULL, 0, pTransBlock,
                              sizeof(TGET_TX_BLOCK), &dwBytes, NULL);
    
        if (bOK && (dwBytes >= sizeof(TGET_TX_BLOCK)))
            sErr = U14ERR_NOERROR;
        else
            sErr = U14ERR_DRIVCOMMS;
    }
    return sErr;
#endif
#ifdef LINUX
    return (sErr == U14ERR_NOERROR) ? CED_GetTransfer(aHand1401[hand], pTransBlock) : sErr;
#endif
}
/////////////////////////////////////////////////////////////////////////////
// U14WorkingSet
// For Win32 only, adjusts process working set so that minimum is at least
//  dwMinKb and maximum is at least dwMaxKb.
// Return value is zero if all went OK, or a code from 1 to 3 indicating the
//  cause of the failure:
//
//     1 unable to access process (insufficient rights?)
//     2 unable to read process working set
//     3 unable to set process working set - bad parameters?
U14API(short) U14WorkingSet(unsigned int dwMinKb, unsigned int dwMaxKb)
{
#ifdef _IS_WINDOWS_
    short sRetVal = 0;                      // 0 means all is OK
    HANDLE hProcess;
    unsigned int dwVer = GetVersion();
	if (dwVer & 0x80000000)                 // is this not NT?
        return 0;                           // then give up right now

    // Now attempt to get information on working set size
    hProcess = OpenProcess(STANDARD_RIGHTS_REQUIRED |
                                  PROCESS_QUERY_INFORMATION |
                                  PROCESS_SET_QUOTA,
                                  FALSE, _getpid());
    if (hProcess)
    {
        SIZE_T dwMinSize,dwMaxSize;
        if (GetProcessWorkingSetSize(hProcess, &dwMinSize, &dwMaxSize))
        {
            unsigned int dwMin = dwMinKb << 10;    // convert from kb to bytes
            unsigned int dwMax = dwMaxKb << 10;

            // if we get here, we have managed to read the current size
            if (dwMin > dwMinSize)          // need to change sizes?
                dwMinSize = dwMin;

            if (dwMax > dwMaxSize)
                dwMaxSize = dwMax;

            if (!SetProcessWorkingSetSize(hProcess, dwMinSize, dwMaxSize))
                sRetVal = 3;                // failed to change size
        }
        else
            sRetVal = 2;                    // failed to read original size

        CloseHandle(hProcess);
    }
    else
        sRetVal = 1;            // failed to get handle

    return sRetVal;
#endif
#ifdef LINUX
    if (dwMinKb | dwMaxKb)
    {
        // to stop compiler moaning
    }
    return U14ERR_NOERROR;
#endif
}

/****************************************************************************
** U14UnSetTransfer  Cancels a transfer area
** wArea    The index of a block previously used in by SetTransfer
*****************************************************************************/
U14API(short) U14UnSetTransfer(short hand, unsigned short wArea)
{
    short sErr = CheckHandle(hand);
#ifdef _IS_WINDOWS_
    if (sErr == U14ERR_NOERROR)
    {
       TCSBLOCK csBlock;
       csBlock.ints[0] = (short)wArea;       /* Area number into control block */
       sErr = U14Control1401(hand, U14_UNSETTRANSFER, &csBlock);  /* Free area */
   
       VirtualUnlock(apAreas[hand][wArea], auAreas[hand][wArea]);/* Unlock */
       apAreas[hand][wArea] = NULL;                         /* Clear locations */
       auAreas[hand][wArea] = 0;
    }
    return sErr;
#endif
#ifdef LINUX
    return (sErr == U14ERR_NOERROR) ? CED_UnsetTransfer(aHand1401[hand], wArea) : sErr;
#endif
}

/****************************************************************************
** U14SetTransArea      Sets an area up to be used for transfers
** unsigned short  wArea     The area number to set up
** void *pvBuff    The address of the buffer for the data.
** unsigned int dwLength  The length of the buffer for the data
** short eSz       The element size (used for byte swapping on the Mac)
****************************************************************************/
U14API(short) U14SetTransArea(short hand, unsigned short wArea, void *pvBuff,
                                          unsigned int dwLength, short eSz)
{
    TRANSFERDESC td;
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;
    if (wArea >= MAX_TRANSAREAS)                    // Is this a valid area number
        return U14ERR_BADAREA;

#ifdef _IS_WINDOWS_
    assert(apAreas[hand][wArea] == NULL);
    assert(auAreas[hand][wArea] == 0);

    apAreas[hand][wArea] = pvBuff;                  /* Save data for later */
    auAreas[hand][wArea] = dwLength;

    if (!VirtualLock(pvBuff, dwLength))             /* Lock using WIN32 calls */
    {
        apAreas[hand][wArea] = NULL;                /* Clear locations */
        auAreas[hand][wArea] = 0;
        return U14ERR_LOCKERR;                      /* VirtualLock failed */
    }
#ifndef _WIN64
    if (!USE_NT_DIOC(hand))                         /* Use Win 9x DIOC? */
    {
        unsigned int dwBytes;
        VXTRANSFERDESC vxDesc;                      /* Structure to pass to VXD */
        vxDesc.wArea = wArea;                       /* Copy across simple params */
        vxDesc.dwLength = dwLength;

        // Check we are not asking an old driver for more than area 0
        if ((wArea != 0) && (U14DriverVersion(hand) < 0x00010002L))
            sErr = U14ERR_DRIVTOOOLD;
        else
        {
            vxDesc.dwAddrOfs = (unsigned int)pvBuff;       /* 32 bit offset */
            vxDesc.wAddrSel  = 0;

            if (DeviceIoControl(aHand1401[hand], (unsigned int)U14_SETTRANSFER,
                                pvBuff,dwLength,    /* Will translate pointer */
                                &vxDesc,sizeof(VXTRANSFERDESC),
                                &dwBytes,NULL))
            {
                if (dwBytes >= sizeof(VXTRANSFERDESC)) /* Driver OK ? */
                    sErr = U14ERR_NOERROR;
                else
                    sErr = U14ERR_DRIVCOMMS;        /* Else never got there */
            }
            else
                sErr = (short)GetLastError();
        }
    }
    else
#endif
    {
        PARAMBLK rWork;
        unsigned int dwBytes;
        td.wArea = wArea;     /* Pure NT - put data into struct */
        td.lpvBuff = pvBuff;
        td.dwLength = dwLength;
        td.eSize = 0;                // Dummy element size

        if (DeviceIoControl(aHand1401[hand],(unsigned int)U14_SETTRANSFER,
                            &td,sizeof(TRANSFERDESC),
                            &rWork,sizeof(PARAMBLK),&dwBytes,NULL))
        {
            if (dwBytes >= sizeof(PARAMBLK))    // maybe error from driver?
                sErr = rWork.sState;            // will report any error
            else
                sErr = U14ERR_DRIVCOMMS;        // Else never got there
        }
        else
            sErr = U14ERR_DRIVCOMMS;
    }

    if (sErr != U14ERR_NOERROR)
    {
        if (sErr != U14ERR_LOCKERR)             // unless lock failed...
            VirtualUnlock(pvBuff, dwLength);    // ...release the lock
        apAreas[hand][wArea] = NULL;            // Clear locations
        auAreas[hand][wArea] = 0;
    }

    return sErr;
#endif
#ifdef LINUX
    // The strange cast is so that it works in 64 and 32-bit linux as long is 64-bits
    // in the 64 bit version.
    td.lpvBuff = (long long)((unsigned long)pvBuff);
    td.wAreaNum = wArea;
    td.dwLength = dwLength;
    td.eSize = eSz;                // Dummy element size
    return CED_SetTransfer(aHand1401[hand], &td);
#endif
}

/****************************************************************************
** U14SetTransferEvent  Sets an event for notification of application
** wArea       The transfer area index, from 0 to MAXAREAS-1
**    bEvent      True to create an event, false to remove it
**    bToHost     Set 0 for notification on to1401 tranfers, 1 for
**                notification of transfers to the host PC
**    dwStart     The offset of the sub-area of interest
**    dwLength    The size of the sub-area of interest
**
** The device driver will set the event supplied to the signalled state
** whenever a DMA transfer to/from the specified area is completed. The
** transfer has to be in the direction specified by bToHost, and overlap
** that part of the whole transfer area specified by dwStart and dwLength.
** It is important that this function is called with bEvent false to release
** the event once 1401 activity is finished.
**
** Returns 1 if an event handle exists, 0 if all OK and no event handle or
** a negative code for an error.
****************************************************************************/
U14API(short) U14SetTransferEvent(short hand, unsigned short wArea, BOOL bEvent,
                                  BOOL bToHost, unsigned int dwStart, unsigned int dwLength)
{
#ifdef _IS_WINDOWS_
    TCSBLOCK csBlock;
    short sErr = U14TransferFlags(hand);        // see if we can handle events
    if (sErr >= U14ERR_NOERROR)                 // check handle is OK
    {
        bEvent = bEvent && ((sErr & U14TF_NOTIFY) != 0); // remove request if we cannot do events
        if (wArea >= MAX_TRANSAREAS)            // Check a valid area...
            return U14ERR_BADAREA;              // ...and bail of not

        // We can hold an event for each area, so see if we need to change the
        // state of the event.
        if ((bEvent != 0) != (aXferEvent[hand] != 0))    // change of event state?
        {
            if (bEvent)                         // want one and none present
                aXferEvent[hand] = CreateEvent(NULL, FALSE, FALSE, NULL);
            else
            {
                CloseHandle(aXferEvent[hand]);  // clear the existing event
                aXferEvent[hand] = NULL;        // and clear handle
            }
        }

        // We have to store the parameters differently for 64-bit operations
        //  because a handle is 64 bits long. The drivers know of this and
        //  handle the information appropriately.
#ifdef _WIN64
        csBlock.longs[0] = wArea;               // Pass paramaters into the driver...
        if (bToHost != 0)                       // The direction flag is held in the
            csBlock.longs[0] |= 0x10000;        //  upper word of the transfer area value
        *((HANDLE*)&csBlock.longs[1]) = aXferEvent[hand];  // The event handle is 64-bits
        csBlock.longs[3] = dwStart;             // Thankfully these two remain
        csBlock.longs[4] = dwLength;            //  as unsigned 32-bit values
#else
        csBlock.longs[0] = wArea;               // pass paramaters into the driver...
        csBlock.longs[1] = (long)aXferEvent[hand];    // ...especially the event handle
        csBlock.longs[2] = bToHost;
        csBlock.longs[3] = dwStart;
        csBlock.longs[4] = dwLength;
#endif
        sErr = U14Control1401(hand, U14_SETTRANSEVENT, &csBlock);
        if (sErr == U14ERR_NOERROR)
            sErr = (short)(aXferEvent[hand] != NULL);    // report if we have a flag
    }

    return sErr;
#endif
#ifdef LINUX
    TRANSFEREVENT te;
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;

    if (wArea >= MAX_TRANSAREAS)            // Is this a valid area number
        return U14ERR_BADAREA;

    te.wAreaNum = wArea;                    // copy parameters to the control block
    te.wFlags = bToHost ? 1 : 0;            // bit 0 sets the direction
    te.dwStart = dwStart;                   // start offset of the event area
    te.dwLength = dwLength;                 // size of the event area
    te.iSetEvent = bEvent;                  // in Windows, this creates/destroys the event
    return CED_SetEvent(aHand1401[hand], &te);
#endif
}

/****************************************************************************
** U14TestTransferEvent
** Would a U14WaitTransferEvent() call return immediately? return 1 if so,
** 0 if not or a negative code if a problem.
****************************************************************************/
U14API(int) U14TestTransferEvent(short hand, unsigned short wArea)
{
#ifdef _IS_WINDOWS_
    int iErr = CheckHandle(hand);
    if (iErr == U14ERR_NOERROR)
    {
        if (aXferEvent[hand])           // if a handle is set...
            iErr = WaitForSingleObject(aXferEvent[hand], 0) == WAIT_OBJECT_0;
    }
    return iErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_TestEvent(aHand1401[hand], wArea) : sErr;
#endif
}

/****************************************************************************
** U14WaitTransferEvent
** Wait for a transfer event with a timeout.
** msTimeOut is 0 for an infinite wait, else it is the maximum time to wait
**           in milliseconds in range 0-0x00ffffff.
** Returns   If no event handle then return immediately. Else return 1 if
**           timed out or 0=event, and a negative code if a problem.
****************************************************************************/
U14API(int) U14WaitTransferEvent(short hand, unsigned short wArea, int msTimeOut)
{
#ifdef _IS_WINDOWS_
    int iErr = CheckHandle(hand);
    if (iErr == U14ERR_NOERROR)
    {
        if (aXferEvent[hand])
        {
            if (msTimeOut == 0)
                msTimeOut = INFINITE;
            iErr = WaitForSingleObject(aXferEvent[hand], msTimeOut) != WAIT_OBJECT_0;
        }
        else
            iErr = TRUE;                // say we timed out if no event
    }
    return iErr;
#endif
#ifdef LINUX
    short sErr = CheckHandle(hand);
    return (sErr == U14ERR_NOERROR) ? CED_WaitEvent(aHand1401[hand], wArea, msTimeOut) : sErr;
#endif
}

/****************************************************************************
** U14SetCircular    Sets an area up for circular DMA transfers
** unsigned short  wArea          The area number to set up
** BOOL  bToHost        Sets the direction of data transfer
** void *pvBuff        The address of the buffer for the data
** unsigned int dwLength       The length of the buffer for the data
****************************************************************************/
U14API(short) U14SetCircular(short hand, unsigned short wArea, BOOL bToHost,
									void *pvBuff, unsigned int dwLength)
{
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;

    if (wArea >= MAX_TRANSAREAS)         /* Is this a valid area number */
        return U14ERR_BADAREA;

	if (!bToHost)             /* For now, support tohost transfers only */
        return U14ERR_BADAREA;            /* best error code I can find */
#ifdef _IS_WINDOWS_
    assert(apAreas[hand][wArea] == NULL);
    assert(auAreas[hand][wArea] == 0);

    apAreas[hand][wArea] = pvBuff;              /* Save data for later */
    auAreas[hand][wArea] = dwLength;

    if (!VirtualLock(pvBuff, dwLength))      /* Lock using WIN32 calls */
        sErr = U14ERR_LOCKERR;                    /* VirtualLock failed */
    else
    {
        PARAMBLK rWork;
        unsigned int dwBytes;
        TRANSFERDESC txDesc;
        txDesc.wArea = wArea;             /* Pure NT - put data into struct */
        txDesc.lpvBuff = pvBuff;
        txDesc.dwLength = dwLength;
        txDesc.eSize = (short)bToHost;       /* Use this for direction flag */
   
        if (DeviceIoControl(aHand1401[hand],(unsigned int)U14_SETCIRCULAR,
                           &txDesc, sizeof(TRANSFERDESC),
                           &rWork, sizeof(PARAMBLK),&dwBytes,NULL))
        {
           if (dwBytes >= sizeof(PARAMBLK))          /* error from driver? */
               sErr = rWork.sState;         /* No, just return driver data */
           else
               sErr = U14ERR_DRIVCOMMS;            /* Else never got there */
        }
        else
            sErr = U14ERR_DRIVCOMMS;
    }

    if (sErr != U14ERR_NOERROR)
    {
        if (sErr != U14ERR_LOCKERR)
            VirtualUnlock(pvBuff, dwLength);         /* Release NT lock */
        apAreas[hand][wArea] = NULL;                 /* Clear locations */
        auAreas[hand][wArea] = 0;
    }

    return sErr;
#endif
#ifdef LINUX
    else
    {
        TRANSFERDESC td;
        td.lpvBuff = (long long)((unsigned long)pvBuff);
        td.wAreaNum = wArea;
        td.dwLength = dwLength;
        td.eSize = (short)bToHost;       /* Use this for direction flag */
        return CED_SetCircular(aHand1401[hand], &td);
    }
#endif
}

/****************************************************************************
** Function  GetCircBlk returns the size (& start offset) of the next
**           available block of circular data.
****************************************************************************/
U14API(int) U14GetCircBlk(short hand, unsigned short wArea, unsigned int *pdwOffs)
{
    int lErr = CheckHandle(hand);
    if (lErr != U14ERR_NOERROR)
        return lErr;

    if (wArea >= MAX_TRANSAREAS)            // Is this a valid area number?
        return U14ERR_BADAREA;
    else
    {
#ifdef _IS_WINDOWS_
        PARAMBLK rWork;
        TCSBLOCK csBlock;
        unsigned int dwBytes;
        csBlock.longs[0] = wArea;               // Area number into control block
        rWork.sState = U14ERR_DRIVCOMMS;
        if (DeviceIoControl(aHand1401[hand], (unsigned int)U14_GETCIRCBLK, &csBlock, sizeof(TCSBLOCK), &rWork, sizeof(PARAMBLK), &dwBytes, NULL) &&
           (dwBytes >= sizeof(PARAMBLK)))
            lErr = rWork.sState;
        else
            lErr = U14ERR_DRIVCOMMS;
   
        if (lErr == U14ERR_NOERROR)             // Did everything go OK?
        {                                       // Yes, we can pass the results back
            lErr = rWork.csBlock.longs[1];      // Return the block information
            *pdwOffs = rWork.csBlock.longs[0];  // Offset is first in array
        }
#endif
#ifdef LINUX
        TCIRCBLOCK cb;
        cb.nArea = wArea;                       // Area number into control block
        cb.dwOffset = 0;
        cb.dwSize = 0;
        lErr = CED_GetCircBlock(aHand1401[hand], &cb);
        if (lErr == U14ERR_NOERROR)             // Did everything go OK?
        {                                       // Yes, we can pass the results back
            lErr = cb.dwSize;                   // return the size
            *pdwOffs = cb.dwOffset;             // and the offset
        }
#endif
    }
    return lErr;
}

/****************************************************************************
** Function  FreeCircBlk marks the specified area of memory as free for
**           resuse for circular transfers and returns the size (& start
**           offset) of the next available block of circular data.
****************************************************************************/
U14API(int) U14FreeCircBlk(short hand, unsigned short wArea, unsigned int dwOffs, unsigned int dwSize,
                                        unsigned int *pdwOffs)
{
    int lErr = CheckHandle(hand);
    if (lErr != U14ERR_NOERROR)
        return lErr;

    if (wArea < MAX_TRANSAREAS)                 // Is this a valid area number
    {
#ifdef _IS_WINDOWS_
        PARAMBLK rWork;
        TCSBLOCK csBlock;
        unsigned int dwBytes;
        csBlock.longs[0] = wArea;               // Area number into control block
        csBlock.longs[1] = dwOffs;
        csBlock.longs[2] = dwSize;
        rWork.sState = U14ERR_DRIVCOMMS;
        if (DeviceIoControl(aHand1401[hand], (unsigned int)U14_FREECIRCBLK, &csBlock, sizeof(TCSBLOCK),
                           &rWork, sizeof(PARAMBLK), &dwBytes, NULL) &&
           (dwBytes >= sizeof(PARAMBLK)))
           lErr = rWork.sState;
        else
           lErr = U14ERR_DRIVCOMMS;
       if (lErr == U14ERR_NOERROR)             // Did everything work OK?
       {                                       // Yes, we can pass the results back
           lErr = rWork.csBlock.longs[1];      // Return the block information
           *pdwOffs = rWork.csBlock.longs[0];  // Offset is first in array
       }
#endif
#ifdef LINUX
        TCIRCBLOCK cb;
        cb.nArea = wArea;                       // Area number into control block
        cb.dwOffset = dwOffs;
        cb.dwSize = dwSize;
    
        lErr = CED_FreeCircBlock(aHand1401[hand], &cb);
        if (lErr == U14ERR_NOERROR)             // Did everything work OK?
        {                                       // Yes, we can pass the results back
            lErr = cb.dwSize;                   // Return the block information
            *pdwOffs = cb.dwOffset;             // Offset is first in array
        }
#endif
    }
    else
        lErr = U14ERR_BADAREA;

    return lErr;
}

/****************************************************************************
** Transfer
** Transfer moves data to 1401 or to host
** Assumes memory is allocated and locked,
** which it should be to get a pointer
*****************************************************************************/
static short Transfer(short hand, BOOL bTo1401, char* pData,
                       unsigned int dwSize, unsigned int dw1401, short eSz)
{
    char strcopy[MAXSTRLEN+1];          // to hold copy of work string
    short sResult = U14SetTransArea(hand, 0, (void *)pData, dwSize, eSz);
    if (sResult == U14ERR_NOERROR)      // no error
    {
        sprintf(strcopy,                // data offset is always 0
                "TO%s,$%X,$%X,0;", bTo1401 ? "1401" : "HOST", dw1401, dwSize);

        U14SendString(hand, strcopy);   // send transfer string

        sResult = U14CheckErr(hand);    // Use ERR command to check for done
        if (sResult > 0)
            sResult = U14ERR_TOXXXERR;  // If a 1401 error, use this code

        U14UnSetTransfer(hand, 0);
    }
    return sResult;
}

/****************************************************************************
** Function  ToHost transfers data into the host from the 1401
****************************************************************************/
U14API(short) U14ToHost(short hand, char* pAddrHost, unsigned int dwSize,
                                            unsigned int dw1401, short eSz)
{
    short sErr = CheckHandle(hand);
    if ((sErr == U14ERR_NOERROR) && dwSize) // TOHOST is a constant
        sErr = Transfer(hand, TOHOST, pAddrHost, dwSize, dw1401, eSz);
    return sErr;
}

/****************************************************************************
** Function  To1401 transfers data into the 1401 from the host
****************************************************************************/
U14API(short) U14To1401(short hand, const char* pAddrHost,unsigned int dwSize,
                                    unsigned int dw1401, short eSz)
{
    short sErr = CheckHandle(hand);
    if ((sErr == U14ERR_NOERROR) && dwSize) // TO1401 is a constant
        sErr = Transfer(hand, TO1401, (char*)pAddrHost, dwSize, dw1401, eSz);
    return sErr;
}

/****************************************************************************
** Function  LdCmd    Loads a command from a full path or just a file
*****************************************************************************/
#ifdef _IS_WINDOWS_
#define file_exist(name) (_access(name, 0) != -1)
#define file_open(name) _lopen(name, OF_READ)
#define file_close(h)   _lclose(h)
#define file_seek(h, pos) _llseek(h, pos, FILE_BEGIN) 
#define file_read(h, buffer, size) (_lread(h, buffer, size) == size)
#endif
#ifdef LINUX
#define file_exist(name) (access(name, F_OK) != -1)
#define file_open(name) open(name, O_RDONLY)
#define file_close(h)   close(h)
#define file_seek(h, pos) lseek(h, pos, SEEK_SET) 
#define file_read(h, buffer, size) (read(h, buffer, size) == (ssize_t)size)
static unsigned int GetModuleFileName(void* dummy, char* buffer, int max)
{
    // The following works for Linux systems with a /proc file system.
    char szProcPath[32];
    sprintf(szProcPath, "/proc/%d/exe", getpid());  // attempt to read link
    if (readlink(szProcPath, buffer, max) != -1)
    {
        dirname (buffer);
        strcat  (buffer, "/");
        return strlen(buffer);
    }
    return 0;
}
#endif

U14API(short) U14LdCmd(short hand, const char* command)
{
    char strcopy[MAXSTRLEN+1];      // to hold copy of work string
    BOOL bGotIt = FALSE;            // have we found the command file?
    int iFHandle;                   // file handle of command
#define FNSZ 260
    char filnam[FNSZ];              // space to build name in
    char szCmd[25];                 // just the command name with extension

    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;

    if (strchr(command, '.') != NULL)       // see if we have full name
    {
        if (file_exist(command))            // If the file exists
        {
            strcpy(filnam, command);        // use name as is
            bGotIt = TRUE;                  // Flag no more searching
        }
        else                                // not found, get file name for search
        {
            char* pStr = strrchr(command, PATHSEP);  // Point to last separator
            if (pStr != NULL)               // Check we got it
            {
                pStr++;                     // move past the backslash
                strcpy(szCmd, pStr);        // copy file name as is
            }
            else
                strcpy(szCmd, command);     // use as is
        }
    }
    else    // File extension not supplied, so build the command file name
    {
        char szExt[8];
        strcpy(szCmd, command);             // Build command file name
        ExtForType(asType1401[hand], szExt);// File extension string
        strcat(szCmd, szExt);               // add it to the end
    }

    // Next place to look is in the 1401 folder in the same place as the
    // application was run from.
    if (!bGotIt)                            // Still not got it?
    {
        unsigned int dwLen = GetModuleFileName(NULL, filnam, FNSZ); // Get app path
        if (dwLen > 0)                      // and use it as path if found
        {
            char* pStr = strrchr(filnam, PATHSEP);    // Point to last separator
            if (pStr != NULL)
            {
                *(++pStr) = 0;                  // Terminate string there
                if (strlen(filnam) < FNSZ-6)    // make sure we have space
                {
                    strcat(filnam, "1401" PATHSEPSTR);  // add in 1401 subdir
                    strcat(filnam,szCmd);
                    bGotIt = (BOOL)file_exist(filnam);  // See if file exists
                }
            }
        }
    }

    // Next place to look is in whatever path is set by the 1401DIR environment
    // variable, if it exists.
    if (!bGotIt)                            // Need to do more searches?/
    {
        char* pStr = getenv("1401DIR");     // Try to find environment var
        if (pStr != NULL)                   // and use it as path if found
        {
            strcpy(filnam, pStr);                   // Use path in environment
            if (filnam[strlen(filnam)-1] != PATHSEP)// We need separator
                strcat(filnam, PATHSEPSTR);
            strcat(filnam, szCmd);
            bGotIt = (BOOL)file_exist(filnam); // Got this one?
        }
    }

    // Last place to look is the default location.
    if (!bGotIt)                        // Need to do more searches?
    {
        strcpy(filnam, DEFCMDPATH);     // Use default path
        strcat(filnam, szCmd);
        bGotIt = file_exist(filnam);    // Got this one?
    }

    iFHandle = file_open(filnam);
    if (iFHandle == -1)
        sErr = U14ERR_NOFILE;
    else
    {                                   // first read in the header block
        CMDHEAD rCmdHead;               // to hold the command header
        if (file_read(iFHandle, &rCmdHead, sizeof(CMDHEAD)))
        {
            size_t nComSize = rCmdHead.wCmdSize;
            char* pMem = malloc(nComSize);
            if (pMem != NULL)
            {
                file_seek(iFHandle, sizeof(CMDHEAD));
                if (file_read(iFHandle, pMem, (UINT)nComSize))
                {
                    sErr = U14SetTransArea(hand, 0, (void *)pMem, (unsigned int)nComSize, ESZBYTES);
                    if (sErr == U14ERR_NOERROR)
                    {
                        sprintf(strcopy, "CLOAD,0,$%X;", (int)nComSize);
                        sErr = U14SendString(hand, strcopy);
                        if (sErr == U14ERR_NOERROR)
                        {
                            sErr = U14CheckErr(hand);     // Use ERR to check for done
                            if (sErr > 0)
                                sErr = U14ERR_CLOADERR;   // If an error, this code
                        }
                        U14UnSetTransfer(hand, 0);  // release transfer area
                    }
                }
                else
                    sErr = U14ERR_READERR;
                free(pMem);
            }
            else
                sErr = U14ERR_HOSTSPACE;    // memory allocate failed
        }
        else
            sErr = U14ERR_READERR;

        file_close(iFHandle);               // close the file
    }

    return sErr;
}


/****************************************************************************
** Ld
** Loads a command into the 1401
** Returns NOERROR code or a long with error in lo word and index of
** command that failed in high word
****************************************************************************/
U14API(unsigned int) U14Ld(short hand, const char* vl, const char* str)
{
    unsigned int dwIndex = 0;              // index to current command
    long lErr = U14ERR_NOERROR;     // what the error was that went wrong
    char strcopy[MAXSTRLEN+1];      // stores unmodified str parameter
    char szFExt[8];                 // The command file extension
    short sErr = CheckHandle(hand);
    if (sErr != U14ERR_NOERROR)
        return sErr;

    ExtForType(asType1401[hand], szFExt);   // File extension string
    strcpy(strcopy, str);               // to avoid changing original

    // now break out one command at a time and see if loaded
    if (*str)                           // if anything there
    {
        BOOL bDone = FALSE;             // true when finished all commands
        int iLoop1 = 0;                 // Point at start of string for command name
        int iLoop2 = 0;                 // and at start of str parameter
        do                              // repeat until end of str
        {
            char filnam[MAXSTRLEN+1];   // filename to use
            char szFName[MAXSTRLEN+1];  // filename work string

            if (!strcopy[iLoop1])       // at the end of the string?
                bDone = TRUE;           // set the finish flag

            if (bDone || (strcopy[iLoop1] == ','))  // end of cmd?
            {
                U14LONG er[5];                  // Used to read back error results
                ++dwIndex;                      // Keep count of command number, first is 1
                szFName[iLoop2]=(char)0;        // null terminate name of command

                strncpy(szLastName, szFName, sizeof(szLastName));    // Save for error info
                szLastName[sizeof(szLastName)-1] = 0;
                strncat(szLastName, szFExt, sizeof(szLastName));     // with extension included
                szLastName[sizeof(szLastName)-1] = 0;

                U14SendString(hand, szFName);   // ask if loaded
                U14SendString(hand, ";ERR;");   // add err return

                lErr = U14LongsFrom1401(hand, er, 5);
                if (lErr > 0)
                {
                    lErr = U14ERR_NOERROR;
                    if (er[0] == 255)           // if command not loaded at all
                    {
                        if (vl && *vl)          // if we have a path name
                        {
                            strcpy(filnam, vl);
                            if (strchr("\\/:", filnam[strlen(filnam)-1]) == NULL)
                                strcat(filnam, PATHSEPSTR); // add separator if none found
                            strcat(filnam, szFName);    // add the file name
                            strcat(filnam, szFExt);     // and extension
                        }
                        else
                            strcpy(filnam, szFName);    // simple name

                        lErr = U14LdCmd(hand, filnam);  // load cmd
                        if (lErr != U14ERR_NOERROR)     // spot any errors
                            bDone = TRUE;               // give up if an error
                    }
                }
                else
                    bDone = TRUE;       // give up if an error

                iLoop2 = 0;             // Reset pointer to command name string
                ++iLoop1;               // and move on through str parameter
            }
            else
                szFName[iLoop2++] = strcopy[iLoop1++];  // no command end, so copy 1 char
        }
        while (!bDone);
    }

    if (lErr == U14ERR_NOERROR)
    {
        szLastName[0] = 0;      // No error, so clean out command name here
        return lErr;
    }
    else
        return ((dwIndex<<16) | ((unsigned int)lErr & 0x0000FFFF));
}

// Initialise the library (if not initialised) and return the library version
U14API(int) U14InitLib(void)
{
    int iRetVal = U14LIB_VERSION;
    if (iAttached == 0)         // only do this the first time please
    {
        int i;
#ifdef _IS_WINDOWS_
        int j;
        unsigned int   dwVersion = GetVersion();
        bWindows9x = FALSE;                  // Assume not Win9x

        if (dwVersion & 0x80000000)                 // if not windows NT
        {
            if ((LOBYTE(LOWORD(dwVersion)) < 4) &&  // if Win32s or...
                 (HIBYTE(LOWORD(dwVersion)) < 95))  // ...below Windows 95
            iRetVal = 0;                            // We do not support this
        else
            bWindows9x = TRUE;                      // Flag we have Win9x
        }
#endif
        
        for (i = 0; i < MAX1401; i++)               // initialise the device area
        {
            aHand1401[i] = INVALID_HANDLE_VALUE;    // Clear handle values
            asType1401[i] = U14TYPEUNKNOWN;         // and 1401 type codes
            alTimeOutPeriod[i] = 3000;              // 3 second timeouts
#ifdef _IS_WINDOWS_
#ifndef _WIN64
            abUseNTDIOC[i] = (BOOL)!bWindows9x;
#endif
            aXferEvent[i] = NULL;                   // there are no Xfer events
            for (j = 0; j < MAX_TRANSAREAS; j++)    // Clear out locked area info
            {
                apAreas[i][j] = NULL;
                auAreas[i][j] = 0;
            }
#endif
        }
    }
    return iRetVal;
}

///--------------------------------------------------------------------------------
/// Functions called when the library is loaded and unloaded to give us a chance to
/// setup the library.


#ifdef _IS_WINDOWS_
#ifndef U14_NOT_DLL
/****************************************************************************
** FUNCTION: DllMain(HANDLE, unsigned int, LPVOID)
** LibMain is called by Windows when the DLL is initialized, Thread Attached,
** and other times. Refer to SDK documentation, as to the different ways this
** may be called.
****************************************************************************/
INT APIENTRY DllMain(HANDLE hInst, unsigned int ul_reason_being_called, LPVOID lpReserved)
{
    int iRetVal = 1;

    switch (ul_reason_being_called)
    {
    case DLL_PROCESS_ATTACH:
        iRetVal = U14InitLib() > 0;         // does nothing if iAttached != 0
        ++iAttached;                        // count times attached
        break;

    case DLL_PROCESS_DETACH:
        if (--iAttached == 0)               // last man out?
            U14CloseAll();                  // release all open handles
        break;
    }
    return iRetVal;

    UNREFERENCED_PARAMETER(lpReserved);
}
#endif
#endif
#ifdef LINUX
void __attribute__((constructor)) use1401_load(void)
{
    U14InitLib();
    ++iAttached;
}

void __attribute__((destructor)) use1401_unload(void)
{
        if (--iAttached == 0)               // last man out?
            U14CloseAll();                  // release all open handles
}
#endif
