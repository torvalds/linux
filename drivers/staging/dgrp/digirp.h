/************************************************************************
 * HP-UX Realport Daemon interface file.
 *
 * Copyright (C) 1998, by Digi International.  All Rights Reserved.
 ************************************************************************/

#ifndef _DIGIDRP_H
#define _DIGIDRP_H

/************************************************************************
 * This file contains defines for the ioctl() interface to
 * the realport driver.   This ioctl() interface is used by the
 * daemon to set speed setup parameters honored by the driver.
 ************************************************************************/

struct link_struct {
	int lk_fast_rate;  /* Fast line rate to be used
			      when the delay is less-equal
			      to lk_fast_delay */

	int lk_fast_delay; /* Fast line rate delay in
			      milliseconds */

	int lk_slow_rate;  /* Slow line rate to be used when
			      the delay is greater-equal
			      to lk_slow_delay */

	int lk_slow_delay; /* Slow line rate delay in
			      milliseconds */

	int lk_header_size; /* Estimated packet header size
			       when sent across the slowest
			       link.  */
};

#define DIGI_GETLINK	_IOW('e', 103, struct link_struct)	/* Get link parameters */
#define DIGI_SETLINK	_IOW('e', 104, struct link_struct)	/* Set link parameters */


/************************************************************************
 * This module provides application access to special Digi
 * serial line enhancements which are not standard UNIX(tm) features.
 ************************************************************************/

struct	digiflow_struct {
	unsigned char	startc;				/* flow cntl start char	*/
	unsigned char	stopc;				/* flow cntl stop char	*/
};

/************************************************************************
 * Values for digi_flags
 ************************************************************************/
#define DIGI_IXON	0x0001		/* Handle IXON in the FEP	*/
#define DIGI_FAST	0x0002		/* Fast baud rates		*/
#define RTSPACE		0x0004		/* RTS input flow control	*/
#define CTSPACE		0x0008		/* CTS output flow control	*/
#define DSRPACE		0x0010		/* DSR output flow control	*/
#define DCDPACE		0x0020		/* DCD output flow control	*/
#define DTRPACE		0x0040		/* DTR input flow control	*/
#define DIGI_COOK	0x0080		/* Cooked processing done in FEP */
#define DIGI_FORCEDCD	0x0100		/* Force carrier		*/
#define	DIGI_ALTPIN	0x0200		/* Alternate RJ-45 pin config	*/
#define	DIGI_AIXON	0x0400		/* Aux flow control in fep	*/
#define	DIGI_PRINTER	0x0800		/* Hold port open for flow cntrl */
#define DIGI_PP_INPUT	0x1000		/* Change parallel port to input */
#define DIGI_422	0x4000		/* Change parallel port to input */
#define DIGI_RTS_TOGGLE	0x8000		/* Support RTS Toggle		 */


/************************************************************************
 * Values associated with transparent print
 ************************************************************************/
#define DIGI_PLEN	8		/* String length */
#define	DIGI_TSIZ	10		/* Terminal string len */


/************************************************************************
 * Structure used with ioctl commands for DIGI parameters.
 ************************************************************************/
struct digi_struct {
	unsigned short	digi_flags;		/* Flags (see above)	*/
	unsigned short	digi_maxcps;		/* Max printer CPS	*/
	unsigned short	digi_maxchar;		/* Max chars in print queue */
	unsigned short	digi_bufsize;		/* Buffer size		*/
	unsigned char	digi_onlen;		/* Length of ON string	*/
	unsigned char	digi_offlen;		/* Length of OFF string	*/
	char		digi_onstr[DIGI_PLEN];	/* Printer on string	*/
	char		digi_offstr[DIGI_PLEN];	/* Printer off string	*/
	char		digi_term[DIGI_TSIZ];	/* terminal string	*/
};

/************************************************************************
 * Ioctl command arguments for DIGI parameters.
 ************************************************************************/
/* Read params */
#define DIGI_GETA	_IOR('e', 94, struct digi_struct)

/* Set params */
#define DIGI_SETA	_IOW('e', 95, struct digi_struct)

/* Drain & set params	*/
#define DIGI_SETAW	_IOW('e', 96, struct digi_struct)

/* Drain, flush & set params */
#define DIGI_SETAF	_IOW('e', 97, struct digi_struct)

/* Get startc/stopc flow control characters */
#define	DIGI_GETFLOW	_IOR('e', 99, struct digiflow_struct)

/* Set startc/stopc flow control characters */
#define	DIGI_SETFLOW	_IOW('e', 100, struct digiflow_struct)

/* Get Aux. startc/stopc flow control chars */
#define	DIGI_GETAFLOW	_IOR('e', 101, struct digiflow_struct)

/* Set Aux. startc/stopc flow control chars */
#define	DIGI_SETAFLOW	_IOW('e', 102, struct digiflow_struct)

/* Set integer baud rate */
#define	DIGI_SETCUSTOMBAUD	_IOW('e', 106, int)

/* Get integer baud rate */
#define	DIGI_GETCUSTOMBAUD	_IOR('e', 107, int)

#define	DIGI_GEDELAY	_IOR('d', 246, int)	/* Get edelay */
#define	DIGI_SEDELAY	_IOW('d', 247, int)	/* Get edelay */


#endif /* _DIGIDRP_H */
