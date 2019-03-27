/*******************************************************************************
*
*  Module  : refclock_tsyncpci.c
*  Date    : 09/08/08
*  Purpose : Implements a reference clock driver for the NTP daemon.  This
*            reference clock driver provides a means to communicate with
*            the Spectracom TSYNC PCI timing devices and use them as a time
*            source.
*
*  (C) Copyright 2008 Spectracom Corporation
*
*  This software is provided by Spectracom Corporation 'as is' and
*  any express or implied warranties, including, but not limited to, the
*  implied warranties of merchantability and fitness for a particular purpose
*  are disclaimed.  In no event shall Spectracom Corporation be liable
*  for any direct, indirect, incidental, special, exemplary, or consequential
*  damages (including, but not limited to, procurement of substitute goods
*  or services; loss of use, data, or profits; or business interruption)
*  however caused and on any theory of liability, whether in contract, strict
*  liability, or tort (including negligence or otherwise) arising in any way
*  out of the use of this software, even if advised of the possibility of
*  such damage.
*
*  This software is released for distribution according to the NTP copyright
*  and license contained in html/copyright.html of NTP source.
*
*******************************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_TSYNCPCI)

#include <asm/ioctl.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>


#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"
#include "ntp_calendar.h"


/*******************************************************************************
**
** This driver supports the Spectracom TSYNC PCI GPS receiver.  It requires
** that the tsyncpci.o device driver be installed and loaded.
**
*******************************************************************************/

#define TSYNC_PCI_REVISION "1.11"

/*
** TPRO interface definitions
*/
#define DEVICE      "/dev/tsyncpci"             /* device name */
#define PRECISION   (-20)                       /* precision assumed (1 us) */
#define DESCRIPTION "Spectracom TSYNC-PCI"      /* WRU */

#define SECONDS_1900_TO_1970 (2208988800U)

#define TSYNC_REF_IID               (0x2500)    // SS CAI, REF IID
#define TSYNC_REF_DEST_ID           (0x0001)    // KTS Firmware
#define TSYNC_REF_IN_PYLD_OFF       (0)
#define TSYNC_REF_IN_LEN            (0)
#define TSYNC_REF_OUT_PYLD_OFF      (0)
#define TSYNC_REF_OUT_LEN           (8)
#define TSYNC_REF_MAX_OUT_LEN       (16)
#define TSYNC_REF_PYLD_LEN          (TSYNC_REF_IN_LEN +                     \
                                     TSYNC_REF_MAX_OUT_LEN)
#define TSYNC_REF_LEN               (4)
#define TSYNC_REF_LOCAL             ("LOCL")

#define TSYNC_TMSCL_IID              (0x2301)    // CS CAI, TIMESCALE IID
#define TSYNC_TMSCL_DEST_ID          (0x0001)    // KTS Firmware
#define TSYNC_TMSCL_IN_PYLD_OFF      (0)
#define TSYNC_TMSCL_IN_LEN           (0)
#define TSYNC_TMSCL_OUT_PYLD_OFF     (0)
#define TSYNC_TMSCL_OUT_LEN          (4)
#define TSYNC_TMSCL_MAX_OUT_LEN      (12)
#define TSYNC_TMSCL_PYLD_LEN         (TSYNC_TMSCL_IN_LEN +                    \
                                     TSYNC_TMSCL_MAX_OUT_LEN)

#define TSYNC_LEAP_IID              (0x2307)    // CS CAI, LEAP SEC IID
#define TSYNC_LEAP_DEST_ID          (0x0001)    // KTS Firmware
#define TSYNC_LEAP_IN_PYLD_OFF      (0)
#define TSYNC_LEAP_IN_LEN           (0)
#define TSYNC_LEAP_OUT_PYLD_OFF     (0)
#define TSYNC_LEAP_OUT_LEN          (28)
#define TSYNC_LEAP_MAX_OUT_LEN      (36)
#define TSYNC_LEAP_PYLD_LEN         (TSYNC_LEAP_IN_LEN +                    \
                                     TSYNC_LEAP_MAX_OUT_LEN)

// These define the base date/time of the system clock.  The system time will
// be tracked as the number of seconds from this date/time.
#define TSYNC_TIME_BASE_YEAR        (1970) // earliest acceptable year

#define TSYNC_LCL_STRATUM           (0)

/*
** TSYNC Time Scales type
*/
typedef enum
{
    TIME_SCALE_UTC    = 0,   // Universal Coordinated Time
    TIME_SCALE_TAI    = 1,   // International Atomic Time
    TIME_SCALE_GPS    = 2,   // Global Positioning System
    TIME_SCALE_LOCAL  = 3,   // UTC w/local rules for time zone and DST
    NUM_TIME_SCALES   = 4,   // Number of time scales

    TIME_SCALE_MAX    = 15   // Maximum number of timescales

} TIME_SCALE;

/*
** TSYNC Board Object
*/
typedef struct BoardObj {

  int            file_descriptor;
  unsigned short devid;
  unsigned short options;
  unsigned char  firmware[5];
  unsigned char  FPGA[5];
  unsigned char  driver[7];

} BoardObj;

/*
** TSYNC Time Object
*/
typedef struct TimeObj {

  unsigned char  syncOption;  /* -M option */
  unsigned int   secsDouble;  /* seconds floating pt */
  unsigned char  seconds;     /* seconds whole num */
  unsigned char  minutes;
  unsigned char  hours;
  unsigned short days;
  unsigned short year;
  unsigned short flags;      /* bit 2 SYNC, bit 1 TCODE; all others 0 */

} TimeObj;

/*
** NTP Time Object
*/
typedef struct NtpTimeObj {

    TimeObj        timeObj;
    struct timeval tv;
    unsigned int   refId;

} NtpTimeObj;
/*
** TSYNC Supervisor Reference Object
*/
typedef struct ReferenceObj {

    char time[TSYNC_REF_LEN];
    char pps[TSYNC_REF_LEN];

} ReferenceObj;

/*
** TSYNC Seconds Time Object
*/
typedef struct SecTimeObj
{
    unsigned int seconds;
    unsigned int ns;
}
SecTimeObj;

/*
** TSYNC DOY Time Object
*/
typedef struct DoyTimeObj
{
    unsigned int year;
    unsigned int doy;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int ns;
}
DoyTimeObj;

/*
** TSYNC Leap Second Object
*/
typedef struct LeapSecondObj
{
    int        offset;
    DoyTimeObj utcDate;
}
LeapSecondObj;

/*
 * structures for ioctl interactions with driver
 */
#define DI_PAYLOADS_STARTER_LENGTH 4
typedef struct ioctl_trans_di {

    // input parameters
    uint16_t        dest;
    uint16_t        iid;

    uint32_t        inPayloadOffset;
    uint32_t        inLength;
    uint32_t        outPayloadOffset;
    uint32_t        maxOutLength;

    // output parameters
    uint32_t        actualOutLength;
    int32_t         status;

    // Input and output

    // The payloads field MUST be last in ioctl_trans_di.
    uint8_t         payloads[DI_PAYLOADS_STARTER_LENGTH];

}ioctl_trans_di;

/*
 * structure for looking up a reference ID from a reference name
 */
typedef struct
{
    const char* pRef;           // KTS Reference Name
    const char* pRefId;         // NTP Reference ID

} RefIdLookup;

/*
 * unit control structure
 */
typedef struct  {
    uint32_t refPrefer;         // Reference prefer flag
    uint32_t refId;             // Host peer reference ID
    uint8_t  refStratum;        // Host peer reference stratum

} TsyncUnit;

/*
**  Function prototypes
*/
static void tsync_poll     (int unit, struct peer *);
static void tsync_shutdown (int, struct peer *);
static int  tsync_start    (int, struct peer *);

/*
**  Helper functions
*/
static void ApplyTimeOffset    (DoyTimeObj* pDt, int off);
static void SecTimeFromDoyTime (SecTimeObj* pSt, DoyTimeObj* pDt);
static void DoyTimeFromSecTime (DoyTimeObj* pDt, SecTimeObj* pSt);

/*
**  Transfer vector
*/
struct refclock refclock_tsyncpci = {
    tsync_start,    /* start up driver */
    tsync_shutdown, /* shut down driver */
    tsync_poll,     /* transmit poll message */
    noentry,        /* not used (old tsync_control) */
    noentry,        /* initialize driver (not used) */
    noentry,        /* not used (old tsync_buginfo) */
    NOFLAGS         /* not used */
};

/*
 * Reference ID lookup table
 */
static RefIdLookup RefIdLookupTbl[] =
{
    {"gps",  "GPS"},
    {"ir",   "IRIG"},
    {"hvq",  "HVQ"},
    {"frq",  "FREQ"},
    {"mdm",  "ACTS"},
    {"epp",  "PPS"},
    {"ptp",  "PTP"},
    {"asc",  "ATC"},
    {"hst0", "USER"},
    {"hst",  TSYNC_REF_LOCAL},
    {"self", TSYNC_REF_LOCAL},
    {NULL,   NULL}
};

/*******************************************************************************
**          IOCTL DEFINITIONS
*******************************************************************************/
#define IOCTL_TPRO_ID            't'
#define IOCTL_TPRO_OPEN          _IOWR(IOCTL_TPRO_ID, 0,  BoardObj)
#define IOCTL_TPRO_GET_NTP_TIME  _IOWR(IOCTL_TPRO_ID, 25, NtpTimeObj)
#define IOCTL_TSYNC_GET          _IOWR(IOCTL_TPRO_ID, 26, ioctl_trans_di)

/******************************************************************************
 *
 * Function:    tsync_start()
 * Description: Used to intialize the Spectracom TSYNC reference driver.
 *
 * Parameters:
 *     IN:  unit - not used.
 *         *peer - pointer to this reference clock's peer structure
 *     Returns: 0 - unsuccessful
 *              1 - successful
 *
*******************************************************************************/
static int tsync_start(int unit, struct peer *peer)
{
    struct refclockproc *pp;
    TsyncUnit           *up;


    /*
    **  initialize reference clock and peer parameters
    */
    pp                = peer->procptr;
    pp->clockdesc     = DESCRIPTION;
    pp->io.clock_recv = noentry;
    pp->io.srcclock   = peer;
    pp->io.datalen    = 0;
    peer->precision   = PRECISION;

    // Allocate and initialize unit structure
    if (!(up = (TsyncUnit*)emalloc(sizeof(TsyncUnit))))
    {
        return (0);
    }

    // Store reference preference
    up->refPrefer = peer->flags & FLAG_PREFER;

    // Initialize reference stratum level and ID
    up->refStratum = STRATUM_UNSPEC;
    strncpy((char *)&up->refId, TSYNC_REF_LOCAL, TSYNC_REF_LEN);

    // Attach unit structure
    pp->unitptr = (caddr_t)up;

    /* Declare our refId as local in the beginning because we do not know
     * what our actual refid is yet.
     */
    strncpy((char *)&pp->refid, TSYNC_REF_LOCAL, TSYNC_REF_LEN);

    return (1);

} /* End - tsync_start() */

/*******************************************************************************
**
** Function:    tsync_shutdown()
** Description: Handles anything related to shutting down the reference clock
**              driver. Nothing at this point in time.
**
** Parameters:
**     IN:  unit - not used.
**         *peer - pointer to this reference clock's peer structure
**     Returns: none.
**
*******************************************************************************/
static void tsync_shutdown(int unit, struct peer *peer)
{

} /* End - tsync_shutdown() */

/******************************************************************************
 *
 * Function:    tsync_poll()
 * Description: Retrieve time from the TSYNC device.
 *
 * Parameters:
 *     IN:  unit - not used.
 *         *peer - pointer to this reference clock's peer structure
 *     Returns: none.
 *
*******************************************************************************/
static void tsync_poll(int unit, struct peer *peer)
{
    char                 device[32];
    struct refclockproc *pp;
    struct calendar      jt;
    TsyncUnit           *up;
    unsigned char        synch;
    double               seconds;
    int                  err;
    int                  err1;
    int                  err2;
    int                  err3;
    int                  i;
    int                  j;
    unsigned int         itAllocationLength;
    unsigned int         itAllocationLength1;
    unsigned int         itAllocationLength2;
    NtpTimeObj           TimeContext;
    BoardObj             hBoard;
    char                 timeRef[TSYNC_REF_LEN + 1];
    char                 ppsRef [TSYNC_REF_LEN + 1];
    TIME_SCALE           tmscl = TIME_SCALE_UTC;
    LeapSecondObj        leapSec;
    ioctl_trans_di      *it;
    ioctl_trans_di      *it1;
    ioctl_trans_di      *it2;
    l_fp                 offset;
    l_fp                 ltemp;
    ReferenceObj *	 pRefObj;


    /* Construct the device name */
    sprintf(device, "%s%d", DEVICE, (int)peer->refclkunit);

    printf("Polling device number %d...\n", (int)peer->refclkunit);

    /* Open the TSYNC device */
    hBoard.file_descriptor = open(device, O_RDONLY | O_NDELAY, 0777);

    /* If error opening TSYNC device... */
    if (hBoard.file_descriptor < 0)
    {
        msyslog(LOG_ERR, "Couldn't open device");
        return;
    }

    /* If error while initializing the board... */
    if (ioctl(hBoard.file_descriptor, IOCTL_TPRO_OPEN, &hBoard) < 0)
    {
        msyslog(LOG_ERR, "Couldn't initialize device");
        close(hBoard.file_descriptor);
        return;
    }

    /* Allocate memory for ioctl message */
    itAllocationLength =
        (sizeof(ioctl_trans_di) - DI_PAYLOADS_STARTER_LENGTH) +
        TSYNC_REF_IN_LEN + TSYNC_REF_MAX_OUT_LEN;

    it = (ioctl_trans_di*)alloca(itAllocationLength);
    if (it == NULL) {
        msyslog(LOG_ERR, "Couldn't allocate transaction memory - Reference");
        return;
    }

    /* Build SS_GetRef ioctl message */
    it->dest             = TSYNC_REF_DEST_ID;
    it->iid              = TSYNC_REF_IID;
    it->inPayloadOffset  = TSYNC_REF_IN_PYLD_OFF;
    it->inLength         = TSYNC_REF_IN_LEN;
    it->outPayloadOffset = TSYNC_REF_OUT_PYLD_OFF;
    it->maxOutLength     = TSYNC_REF_MAX_OUT_LEN;
    it->actualOutLength  = 0;
    it->status           = 0;
    memset(it->payloads, 0, TSYNC_REF_MAX_OUT_LEN);

    /* Read the reference from the TSYNC-PCI device */
    err = ioctl(hBoard.file_descriptor,
                 IOCTL_TSYNC_GET,
                (char *)it);

    /* Allocate memory for ioctl message */
    itAllocationLength1 =
        (sizeof(ioctl_trans_di) - DI_PAYLOADS_STARTER_LENGTH) +
        TSYNC_TMSCL_IN_LEN + TSYNC_TMSCL_MAX_OUT_LEN;

    it1 = (ioctl_trans_di*)alloca(itAllocationLength1);
    if (it1 == NULL) {
        msyslog(LOG_ERR, "Couldn't allocate transaction memory - Time Scale");
        return;
    }

    /* Build CS_GetTimeScale ioctl message */
    it1->dest             = TSYNC_TMSCL_DEST_ID;
    it1->iid              = TSYNC_TMSCL_IID;
    it1->inPayloadOffset  = TSYNC_TMSCL_IN_PYLD_OFF;
    it1->inLength         = TSYNC_TMSCL_IN_LEN;
    it1->outPayloadOffset = TSYNC_TMSCL_OUT_PYLD_OFF;
    it1->maxOutLength     = TSYNC_TMSCL_MAX_OUT_LEN;
    it1->actualOutLength  = 0;
    it1->status           = 0;
    memset(it1->payloads, 0, TSYNC_TMSCL_MAX_OUT_LEN);

    /* Read the Time Scale info from the TSYNC-PCI device */
    err1 = ioctl(hBoard.file_descriptor,
                 IOCTL_TSYNC_GET,
                 (char *)it1);

    /* Allocate memory for ioctl message */
    itAllocationLength2 =
        (sizeof(ioctl_trans_di) - DI_PAYLOADS_STARTER_LENGTH) +
        TSYNC_LEAP_IN_LEN + TSYNC_LEAP_MAX_OUT_LEN;

    it2 = (ioctl_trans_di*)alloca(itAllocationLength2);
    if (it2 == NULL) {
        msyslog(LOG_ERR, "Couldn't allocate transaction memory - Leap Second");
        return;
    }

    /* Build CS_GetLeapSec ioctl message */
    it2->dest             = TSYNC_LEAP_DEST_ID;
    it2->iid              = TSYNC_LEAP_IID;
    it2->inPayloadOffset  = TSYNC_LEAP_IN_PYLD_OFF;
    it2->inLength         = TSYNC_LEAP_IN_LEN;
    it2->outPayloadOffset = TSYNC_LEAP_OUT_PYLD_OFF;
    it2->maxOutLength     = TSYNC_LEAP_MAX_OUT_LEN;
    it2->actualOutLength  = 0;
    it2->status           = 0;
    memset(it2->payloads, 0, TSYNC_LEAP_MAX_OUT_LEN);

    /* Read the leap seconds info from the TSYNC-PCI device */
    err2 = ioctl(hBoard.file_descriptor,
                 IOCTL_TSYNC_GET,
                 (char *)it2);

    pp = peer->procptr;
    up = (TsyncUnit*)pp->unitptr;

    /* Read the time from the TSYNC-PCI device */
    err3 = ioctl(hBoard.file_descriptor,
                 IOCTL_TPRO_GET_NTP_TIME,
                 (char *)&TimeContext);

    /* Close the TSYNC device */
    close(hBoard.file_descriptor);

    // Check for errors
    if ((err < 0) ||(err1 < 0) || (err2 < 0) || (err3 < 0) ||
        (it->status != 0) || (it1->status != 0) || (it2->status != 0) ||
        (it->actualOutLength  != TSYNC_REF_OUT_LEN) ||
        (it1->actualOutLength != TSYNC_TMSCL_OUT_LEN) ||
        (it2->actualOutLength != TSYNC_LEAP_OUT_LEN)) {
        refclock_report(peer, CEVNT_FAULT);
        return;
    }

    // Extract reference identifiers from ioctl payload
    memset(timeRef, '\0', sizeof(timeRef));
    memset(ppsRef, '\0', sizeof(ppsRef));
    pRefObj = (void *)it->payloads;
    memcpy(timeRef, pRefObj->time, TSYNC_REF_LEN);
    memcpy(ppsRef, pRefObj->pps, TSYNC_REF_LEN);

    // Extract the Clock Service Time Scale and convert to correct byte order
    memcpy(&tmscl, it1->payloads, sizeof(tmscl));
    tmscl = ntohl(tmscl);

    // Extract leap second info from ioctl payload and perform byte swapping
    for (i = 0; i < (sizeof(leapSec) / 4); i++)
    {
        for (j = 0; j < 4; j++)
        {
            ((unsigned char*)&leapSec)[(i * 4) + j] =
                    ((unsigned char*)(it2->payloads))[(i * 4) + (3 - j)];
        }
    }

    // Determine time reference ID from reference name
    for (i = 0; RefIdLookupTbl[i].pRef != NULL; i++)
    {
       // Search RefID table
       if (strstr(timeRef, RefIdLookupTbl[i].pRef) != NULL)
       {
          // Found the matching string
          break;
       }
    }

    // Determine pps reference ID from reference name
    for (j = 0; RefIdLookupTbl[j].pRef != NULL; j++)
    {
       // Search RefID table
       if (strstr(ppsRef, RefIdLookupTbl[j].pRef) != NULL)
       {
          // Found the matching string
          break;
       }
    }

    // Determine synchronization state from flags
    synch = (TimeContext.timeObj.flags == 0x4) ? 1 : 0;

    // Pull seconds information from time object
    seconds = (double) (TimeContext.timeObj.secsDouble);
    seconds /= (double) 1000000.0;

    /*
    ** Convert the number of microseconds to double and then place in the
    ** peer's last received long floating point format.
    */
    DTOLFP(((double)TimeContext.tv.tv_usec / 1000000.0), &pp->lastrec);

    /*
    ** The specTimeStamp is the number of seconds since 1/1/1970, while the
    ** peer's lastrec time should be compatible with NTP which is seconds since
    ** 1/1/1900.  So Add the number of seconds between 1900 and 1970 to the
    ** specTimeStamp and place in the peer's lastrec long floating point struct.
    */
    pp->lastrec.Ul_i.Xl_ui += (unsigned int)TimeContext.tv.tv_sec +
                                            SECONDS_1900_TO_1970;

    pp->polls++;

    /*
    **  set the reference clock object
    */
    sprintf(pp->a_lastcode, "%03d %02d:%02d:%02.6f",
            TimeContext.timeObj.days, TimeContext.timeObj.hours,
            TimeContext.timeObj.minutes, seconds);

    pp->lencode = strlen (pp->a_lastcode);
    pp->day     = TimeContext.timeObj.days;
    pp->hour    = TimeContext.timeObj.hours;
    pp->minute  = TimeContext.timeObj.minutes;
    pp->second  = (int) seconds;
    seconds     = (seconds - (double) (pp->second / 1.0)) * 1000000000;
    pp->nsec    = (long) seconds;

    /*
    **  calculate year start
    */
    jt.year       = TimeContext.timeObj.year;
    jt.yearday    = 1;
    jt.monthday   = 1;
    jt.month      = 1;
    jt.hour       = 0;
    jt.minute     = 0;
    jt.second     = 0;
    pp->yearstart = caltontp(&jt);

    // Calculate and report reference clock offset
    offset.l_ui = (long)(((pp->day - 1) * 24) + pp->hour + GMT);
    offset.l_ui = (offset.l_ui * 60) + (long)pp->minute;
    offset.l_ui = (offset.l_ui * 60) + (long)pp->second;
    offset.l_ui = offset.l_ui + (long)pp->yearstart;
    offset.l_uf = 0;
    DTOLFP(pp->nsec / 1e9, &ltemp);
    L_ADD(&offset, &ltemp);
    refclock_process_offset(pp, offset, pp->lastrec,
                            pp->fudgetime1);

    // KTS in sync
    if (synch) {
        // Subtract leap second info by one second to determine effective day
        ApplyTimeOffset(&(leapSec.utcDate), -1);

        // If there is a leap second today and the KTS is using a time scale
        // which handles leap seconds then
        if ((tmscl != TIME_SCALE_GPS) && (tmscl != TIME_SCALE_TAI) &&
            (leapSec.utcDate.year == (unsigned int)TimeContext.timeObj.year) &&
            (leapSec.utcDate.doy  == (unsigned int)TimeContext.timeObj.days))
        {
            // If adding a second
            if (leapSec.offset == 1)
            {
                pp->leap = LEAP_ADDSECOND;
            }
            // Else if removing a second
            else if (leapSec.offset == -1)
            {
                pp->leap = LEAP_DELSECOND;
            }
            // Else report no leap second pending (no handling of offsets
            // other than +1 or -1)
            else
            {
                pp->leap = LEAP_NOWARNING;
            }
        }
        // Else report no leap second pending
        else
        {
            pp->leap = LEAP_NOWARNING;
        }

        peer->leap = pp->leap;
        refclock_report(peer, CEVNT_NOMINAL);

        // If reference name reported, then not in holdover
        if ((RefIdLookupTbl[i].pRef != NULL) &&
            (RefIdLookupTbl[j].pRef != NULL))
        {
            // Determine if KTS being synchronized by host (identified as
            // "LOCL")
            if ((strcmp(RefIdLookupTbl[i].pRefId, TSYNC_REF_LOCAL) == 0) ||
                (strcmp(RefIdLookupTbl[j].pRefId, TSYNC_REF_LOCAL) == 0))
            {
                // Clear prefer flag
                peer->flags &= ~FLAG_PREFER;

                // Set reference clock stratum level as unusable
                pp->stratum   = STRATUM_UNSPEC;
                peer->stratum = pp->stratum;

                // If a valid peer is available
                if ((sys_peer != NULL) && (sys_peer != peer))
                {
                    // Store reference peer stratum level and ID
                    up->refStratum = sys_peer->stratum;
                    up->refId      = addr2refid(&sys_peer->srcadr);
                }
            }
            else
            {
                // Restore prefer flag
                peer->flags |= up->refPrefer;

                // Store reference stratum as local clock
                up->refStratum = TSYNC_LCL_STRATUM;
                strncpy((char *)&up->refId, RefIdLookupTbl[j].pRefId,
                    TSYNC_REF_LEN);

                // Set reference clock stratum level as local clock
                pp->stratum   = TSYNC_LCL_STRATUM;
                peer->stratum = pp->stratum;
            }

            // Update reference name
            strncpy((char *)&pp->refid, RefIdLookupTbl[j].pRefId,
                TSYNC_REF_LEN);
            peer->refid = pp->refid;
        }
        // Else in holdover
        else
        {
            // Restore prefer flag
            peer->flags |= up->refPrefer;

            // Update reference ID to saved ID
            pp->refid   = up->refId;
            peer->refid = pp->refid;

            // Update stratum level to saved stratum level
            pp->stratum   = up->refStratum;
            peer->stratum = pp->stratum;
        }
    }
    // Else KTS not in sync
    else {
        // Place local identifier in peer RefID
        strncpy((char *)&pp->refid, TSYNC_REF_LOCAL, TSYNC_REF_LEN);
        peer->refid = pp->refid;

        // Report not in sync
        pp->leap   = LEAP_NOTINSYNC;
        peer->leap = pp->leap;
    }

    if (pp->coderecv == pp->codeproc) {
        refclock_report(peer, CEVNT_TIMEOUT);
        return;
    }

    record_clock_stats(&peer->srcadr, pp->a_lastcode);
    refclock_receive(peer);

    /* Increment the number of times the reference has been polled */
    pp->polls++;

} /* End - tsync_poll() */


////////////////////////////////////////////////////////////////////////////////
// Function:    ApplyTimeOffset
// Description: The ApplyTimeOffset function adds an offset (in seconds) to a
//              specified date and time.  The specified date and time is passed
//              back after being modified.
//
// Assumptions: 1. Every fourth year is a leap year.  Therefore, this function
//                 is only accurate through Feb 28, 2100.
////////////////////////////////////////////////////////////////////////////////
void ApplyTimeOffset(DoyTimeObj* pDt, int off)
{
    SecTimeObj st;                  // Time, in seconds


    // Convert date and time to seconds
    SecTimeFromDoyTime(&st, pDt);

    // Apply offset
    st.seconds = (int)((signed long long)st.seconds + (signed long long)off);

    // Convert seconds to date and time
    DoyTimeFromSecTime(pDt, &st);

} // End ApplyTimeOffset


////////////////////////////////////////////////////////////////////////////////
// Function:    SecTimeFromDoyTime
// Description: The SecTimeFromDoyTime function converts a specified date
//              and time into a count of seconds since the base time.  This
//              function operates across the range Base Time to Max Time for
//              the system.
//
// Assumptions: 1. A leap year is any year evenly divisible by 4.  Therefore,
//                 this function is only accurate through Feb 28, 2100.
//              2. Conversion does not account for leap seconds.
////////////////////////////////////////////////////////////////////////////////
void SecTimeFromDoyTime(SecTimeObj* pSt, DoyTimeObj* pDt)
{
    unsigned int yrs;               // Years
    unsigned int lyrs;              // Leap years


    // Start with accumulated time of 0
    pSt->seconds  = 0;

    // Calculate the number of years and leap years
    yrs           = pDt->year - TSYNC_TIME_BASE_YEAR;
    lyrs          = (yrs + 1) / 4;

    // Convert leap years and years
    pSt->seconds += lyrs           * SECSPERLEAPYEAR;
    pSt->seconds += (yrs - lyrs)   * SECSPERYEAR;

    // Convert days, hours, minutes and seconds
    pSt->seconds += (pDt->doy - 1) * SECSPERDAY;
    pSt->seconds += pDt->hour      * SECSPERHR;
    pSt->seconds += pDt->minute    * SECSPERMIN;
    pSt->seconds += pDt->second;

    // Copy the subseconds count
    pSt->ns       = pDt->ns;

} // End SecTimeFromDoyTime


////////////////////////////////////////////////////////////////////////////////
// Function:    DoyTimeFromSecTime
// Description: The DoyTimeFromSecTime function converts a specified count
//              of seconds since the start of our base time into a SecTimeObj
//              structure.
//
// Assumptions: 1. A leap year is any year evenly divisible by 4.  Therefore,
//                 this function is only accurate through Feb 28, 2100.
//              2. Conversion does not account for leap seconds.
////////////////////////////////////////////////////////////////////////////////
void DoyTimeFromSecTime(DoyTimeObj* pDt, SecTimeObj* pSt)
{
    signed long long secs;          // Seconds accumulator variable
    unsigned int     yrs;           // Years accumulator variable
    unsigned int     doys;          // Days accumulator variable
    unsigned int     hrs;           // Hours accumulator variable
    unsigned int     mins;          // Minutes accumulator variable


    // Convert the seconds count into a signed 64-bit number for calculations
    secs  = (signed long long)(pSt->seconds);

    // Calculate the number of 4 year chunks
    yrs   = (unsigned int)((secs /
                           ((SECSPERYEAR * 3) + SECSPERLEAPYEAR)) * 4);
    secs %= ((SECSPERYEAR * 3) + SECSPERLEAPYEAR);

    // If there is at least a normal year worth of time left
    if (secs >= SECSPERYEAR)
    {
        // Increment the number of years and subtract a normal year of time
        yrs++;
        secs -= SECSPERYEAR;
    }

    // If there is still at least a normal year worth of time left
    if (secs >= SECSPERYEAR)
    {
        // Increment the number of years and subtract a normal year of time
        yrs++;
        secs -= SECSPERYEAR;
    }

    // If there is still at least a leap year worth of time left
    if (secs >= SECSPERLEAPYEAR)
    {
        // Increment the number of years and subtract a leap year of time
        yrs++;
        secs -= SECSPERLEAPYEAR;
    }

    // Calculate the day of year as the number of days left, then add 1
    // because months start on the 1st.
    doys  = (unsigned int)((secs / SECSPERDAY) + 1);
    secs %= SECSPERDAY;

    // Calculate the hour
    hrs   = (unsigned int)(secs / SECSPERHR);
    secs %= SECSPERHR;

    // Calculate the minute
    mins  = (unsigned int)(secs / SECSPERMIN);
    secs %= SECSPERMIN;

    // Fill in the doytime structure
    pDt->year   = yrs + TSYNC_TIME_BASE_YEAR;
    pDt->doy    = doys;
    pDt->hour   = hrs;
    pDt->minute = mins;
    pDt->second = (unsigned int)secs;
    pDt->ns     = pSt->ns;

} // End DoyTimeFromSecTime

#else
int refclock_tsyncpci_bs;
#endif /* REFCLOCK */
