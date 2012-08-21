#ifndef CSR_TIME_H__
#define CSR_TIME_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/types.h>

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

#ifdef __cplusplus
}
#endif

#endif
