#ifndef CSR_TIME_H__
#define CSR_TIME_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************

    NAME
        CsrTime

    DESCRIPTION
        Type to hold a value describing the current system time, which is a
        measure of time elapsed since some arbitrarily defined fixed time
        reference, usually associated with system startup.

*******************************************************************************/
typedef u32 CsrTime;


/*******************************************************************************

    NAME
        CsrTimeUtc

    DESCRIPTION
        Type to hold a value describing a UTC wallclock time expressed in
        seconds and milliseconds elapsed since midnight January 1st 1970.

*******************************************************************************/
typedef struct
{
    u32 sec;
    u16 msec;
} CsrTimeUtc;


/*******************************************************************************

    NAME
        CsrTimeGet

    DESCRIPTION
        Returns the current system time in a low and a high part. The low part
        is expressed in microseconds. The high part is incremented when the low
        part wraps to provide an extended range.

        The caller may provide a NULL pointer as the high parameter. In this case
        the function just returns the low part and ignores the high parameter.

        Although the time is expressed in microseconds the actual resolution is
        platform dependent and can be less. It is recommended that the
        resolution is at least 10 milliseconds.

    PARAMETERS
        high - Pointer to variable that will receive the high part of the
               current system time. Passing NULL is valid.

    RETURNS
        Low part of current system time in microseconds.

*******************************************************************************/
CsrTime CsrTimeGet(CsrTime *high);


/*******************************************************************************

    NAME
        CsrTimeUtcGet

    DESCRIPTION
        Get the current system wallclock time, and optionally the current system
        time in a low and a high part as would have been returned by
        CsrTimeGet.

        Although CsrTimeUtc is expressed in seconds and milliseconds, the actual
        resolution is platform dependent, and can be less. It is recommended
        that the resolution is at least 1 second.

    PARAMETERS
        tod - Pointer to variable that will receive the current system
              wallclock time.
        low - The low part of the current system time in microseconds. Passing
              NULL is valid.
        high - The high part of the current system time in microseconds. Passing
               NULL is valid.

*******************************************************************************/
void CsrTimeUtcGet(CsrTimeUtc *tod, CsrTime *low, CsrTime *high);


/*------------------------------------------------------------------*/
/* CsrTime Macros */
/*------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeAdd
 *
 *  DESCRIPTION
 *      Add two time values. Adding the numbers can overflow the range of a
 *      CsrTime, so the user must be cautious.
 *
 *  RETURNS
 *      CsrTime - the sum of "t1" and "t2".
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeAdd(t1, t2) ((t1) + (t2))

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeSub
 *
 *  DESCRIPTION
 *      Subtract two time values. Subtracting the numbers can provoke an
 *      underflow, so the user must be cautious.
 *
 *  RETURNS
 *      CsrTime - "t1" - "t2".
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeSub(t1, t2)    ((s32) (t1) - (s32) (t2))

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeEq
 *
 *  DESCRIPTION
 *      Compare two time values.
 *
 *  RETURNS
 *      !0 if "t1" equal "t2", else 0.
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeEq(t1, t2) ((t1) == (t2))

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeGt
 *
 *  DESCRIPTION
 *      Compare two time values.
 *
 *  RETURNS
 *      !0 if "t1" is greater than "t2", else 0.
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeGt(t1, t2) (CsrTimeSub((t1), (t2)) > 0)

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeGe
 *
 *  DESCRIPTION
 *      Compare two time values.
 *
 *  RETURNS
 *      !0 if "t1" is greater than, or equal to "t2", else 0.
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeGe(t1, t2) (CsrTimeSub((t1), (t2)) >= 0)

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeLt
 *
 *  DESCRIPTION
 *      Compare two time values.
 *
 *  RETURNS
 *      !0 if "t1" is less than "t2", else 0.
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeLt(t1, t2) (CsrTimeSub((t1), (t2)) < 0)

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrTimeLe
 *
 *  DESCRIPTION
 *      Compare two time values.
 *
 *  RETURNS
 *      !0 if "t1" is less than, or equal to "t2", else 0.
 *
 *----------------------------------------------------------------------------*/
#define CsrTimeLe(t1, t2) (CsrTimeSub((t1), (t2)) <= 0)

#ifdef __cplusplus
}
#endif

#endif
