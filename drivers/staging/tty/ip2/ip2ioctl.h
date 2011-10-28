/*******************************************************************************
*
*   (c) 1998 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Driver constants for configuration and tuning
*
*   NOTES:
*
*******************************************************************************/

#ifndef IP2IOCTL_H
#define IP2IOCTL_H

//*************
//* Constants *
//*************

// High baud rates (if not defined elsewhere.
#ifndef B153600   
#	define B153600   0010005
#endif
#ifndef B307200   
#	define B307200   0010006
#endif
#ifndef B921600   
#	define B921600   0010007
#endif

#endif
