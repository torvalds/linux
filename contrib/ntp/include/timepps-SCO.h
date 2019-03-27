/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1999-2000			       *
 *								       *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and with or without fee is hereby *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name        *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,        *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any	       *
 * purpose. It is provided "as is" without express or implied          *
 * warranty.							       *
 *								       *
 ***********************************************************************
 *								       *
 * This header file complies with "Pulse-Per-Second API for UNIX-like  *
 * Operating Systems, Version 1.0", rfc2783. Credit is due Jeff Mogul  *
 * and Marc Brett, from whom much of this code was shamelessly stolen. *
 *								       *
 * this modified timepps.h can be used to provide a PPSAPI interface   *
 * to a machine running SCO Unix.				       *
 *								       *
 ***********************************************************************
 *								       *
 * A full PPSAPI interface to the SCO Unix kernel would be better, but *
 * this at least removes the necessity for special coding from the NTP *
 * NTP drivers. 						       *
 *								       *
 ***********************************************************************
 *								       *
 * Some of this include file					       *
 * Copyright (c) 1999 by Ulrich Windl,				       *
 *	based on code by Reg Clemens <reg@dwf.com>		       *
 *		based on code by Poul-Henning Kamp <phk@FreeBSD.org>   *
 *								       *
 ***********************************************************************
 *								       *
 * "THE BEER-WARE LICENSE" (Revision 42):                              *
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this      *
 * notice you can do whatever you want with this stuff. If we meet some*
 * day, and you think this stuff is worth it, you can buy me a beer    *
 * in return.	Poul-Henning Kamp				       *
 *								       *
 **********************************************************************/

/*SCO UNIX version, TIOCDCDTIMESTAMP assumed to exist. */

#ifndef _SYS_TIMEPPS_H_
#define _SYS_TIMEPPS_H_

#include <termios.h>	/* to get TIOCDCDTIMESTAMP */

/* Implementation note: the logical states ``assert'' and ``clear''
 * are implemented in terms of the UART register, i.e. ``assert''
 * means the bit is set.
 */

/*
 * The following definitions are architecture independent
 */

#define PPS_API_VERS_1	1		/* API version number */
#define PPS_JAN_1970	2208988800UL	/* 1970 - 1900 in seconds */
#define PPS_NANOSECOND	1000000000L	/* one nanosecond in decimal */
#define PPS_FRAC	4294967296.	/* 2^32 as a double */

#define PPS_NORMALIZE(x)	/* normalize timespec */ \
	do { \
		if ((x).tv_nsec >= PPS_NANOSECOND) { \
			(x).tv_nsec -= PPS_NANOSECOND; \
			(x).tv_sec++; \
		} else if ((x).tv_nsec < 0) { \
			(x).tv_nsec += PPS_NANOSECOND; \
			(x).tv_sec--; \
		} \
	} while (0)

#define PPS_TSPECTONTP(x)	/* convert timespec to l_fp */ \
	do { \
		double d_temp; \
	\
		(x).integral += (unsigned int)PPS_JAN_1970; \
		d_temp = (x).fractional * PPS_FRAC / PPS_NANOSECOND; \
		if (d_temp >= PPS_FRAC) \
			(x).integral++; \
		(x).fractional = (unsigned int)d_temp; \
	} while (0)

/*
 * Device/implementation parameters (mode)
 */

#define PPS_CAPTUREASSERT	0x01	/* capture assert events */
#define PPS_CAPTURECLEAR	0x02	/* capture clear events */
#define PPS_CAPTUREBOTH 	0x03	/* capture assert and clear events */

#define PPS_OFFSETASSERT	0x10	/* apply compensation for assert ev. */
#define PPS_OFFSETCLEAR 	0x20	/* apply compensation for clear ev. */
#define PPS_OFFSETBOTH		0x30	/* apply compensation for both */

#define PPS_CANWAIT		0x100	/* Can we wait for an event? */
#define PPS_CANPOLL		0x200	/* "This bit is reserved for */

/*
 * Kernel actions (mode)
 */

#define PPS_ECHOASSERT		0x40	/* feed back assert event to output */
#define PPS_ECHOCLEAR		0x80	/* feed back clear event to output */

/*
 * Timestamp formats (tsformat)
 */

#define PPS_TSFMT_TSPEC 	0x1000	/* select timespec format */
#define PPS_TSFMT_NTPFP 	0x2000	/* select NTP format */

/*
 * Kernel discipline actions (not used in Solaris)
 */

#define PPS_KC_HARDPPS		0	/* enable kernel consumer */
#define PPS_KC_HARDPPS_PLL	1	/* phase-lock mode */
#define PPS_KC_HARDPPS_FLL	2	/* frequency-lock mode */

/*
 * Type definitions
 */

typedef unsigned long pps_seq_t;	/* sequence number */

typedef struct ntp_fp {
	unsigned int	integral;
	unsigned int	fractional;
} ntp_fp_t;				/* NTP-compatible time stamp */

typedef union pps_timeu {		/* timestamp format */
	struct timespec tspec;
	ntp_fp_t	ntpfp;
	unsigned long	longpad[3];
} pps_timeu_t;				/* generic data type to represent time stamps */

/*
 * Timestamp information structure
 */

typedef struct pps_info {
	pps_seq_t	assert_sequence;	/* seq. num. of assert event */
	pps_seq_t	clear_sequence; 	/* seq. num. of clear event */
	pps_timeu_t	assert_tu;		/* time of assert event */
	pps_timeu_t	clear_tu;		/* time of clear event */
	int		current_mode;		/* current mode bits */
} pps_info_t;

#define assert_timestamp	assert_tu.tspec
#define clear_timestamp 	clear_tu.tspec

#define assert_timestamp_ntpfp	assert_tu.ntpfp
#define clear_timestamp_ntpfp	clear_tu.ntpfp

/*
 * Parameter structure
 */

typedef struct pps_params {
	int		api_version;	/* API version # */
	int		mode;		/* mode bits */
	pps_timeu_t assert_off_tu;	/* offset compensation for assert */
	pps_timeu_t clear_off_tu;	/* offset compensation for clear */
} pps_params_t;

#define assert_offset		assert_off_tu.tspec
#define clear_offset		clear_off_tu.tspec

#define assert_offset_ntpfp	assert_off_tu.ntpfp
#define clear_offset_ntpfp	clear_off_tu.ntpfp

/*
 * The following definitions are architecture-dependent
 */

#define PPS_CAP (PPS_CAPTUREASSERT | PPS_OFFSETASSERT | PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP)
#define PPS_RO	(PPS_CANWAIT | PPS_CANPOLL | PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP)

typedef struct {
	int filedes;		/* file descriptor */
	pps_params_t params;	/* PPS parameters set by user */
	struct timeval	tv_save;
	pps_seq_t serial;
} pps_unit_t;

typedef pps_unit_t* pps_handle_t; /* pps handlebars */

/*
 *------ Here begins the implementation-specific part! ------
 */

#include <errno.h>

/*
 * create PPS handle from file descriptor
 */

static inline int
time_pps_create(
	int filedes,		/* file descriptor */
	pps_handle_t *handle	/* returned handle */
	)
{
	int one = 1;

	/*
	 * Check for valid arguments and attach PPS signal.
	 */

	if (!handle) {
		errno = EFAULT;
		return (-1);	/* null pointer */
	}

	/*
	 * Allocate and initialize default unit structure.
	 */

	*handle = malloc(sizeof(pps_unit_t));
	if (!(*handle)) {
		errno = EBADF;
		return (-1);	/* what, no memory? */
	}

	memset(*handle, 0, sizeof(pps_unit_t));
	(*handle)->filedes = filedes;
	(*handle)->params.api_version = PPS_API_VERS_1;
	(*handle)->params.mode = PPS_CAPTUREASSERT | PPS_TSFMT_TSPEC;
	return (0);
}

/*
 * release PPS handle
 */

static inline int
time_pps_destroy(
	pps_handle_t handle
	)
{
	/*
	 * Check for valid arguments and detach PPS signal.
	 */

	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	free(handle);
	return (0);
}

/*
 * set parameters for handle
 */

static inline int
time_pps_setparams(
	pps_handle_t handle,
	const pps_params_t *params
	)
{
	int	mode, mode_in;
	/*
	 * Check for valid arguments and set parameters.
	 */

	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}

	if (!params) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}

	/*
	 * There was no reasonable consensu in the API working group.
	 * I require `api_version' to be set!
	 */

	if (params->api_version != PPS_API_VERS_1) {
		errno = EINVAL;
		return(-1);
	}

	/*
	 * only settable modes are PPS_CAPTUREASSERT and PPS_OFFSETASSERT
	 */

	mode_in = params->mode;

	/* turn off read-only bits */

	mode_in &= ~PPS_RO;

	/* test remaining bits, should only have captureassert and/or offsetassert */

	if (mode_in & ~(PPS_CAPTUREASSERT | PPS_OFFSETASSERT)) {
		errno = EOPNOTSUPP;
		return(-1);
	}

	/*
	 * ok, ready to go.
	 */

	mode = handle->params.mode;
	memcpy(&handle->params, params, sizeof(pps_params_t));
	handle->params.api_version = PPS_API_VERS_1;
	handle->params.mode = mode | mode_in;
	return (0);
}

/*
 * get parameters for handle
 */

static inline int
time_pps_getparams(
	pps_handle_t handle,
	pps_params_t *params
	)
{
	/*
	 * Check for valid arguments and get parameters.
	 */

	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}

	if (!params) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}

	memcpy(params, &handle->params, sizeof(pps_params_t));
	return (0);
}

/* (
 * get capabilities for handle
 */

static inline int
time_pps_getcap(
	pps_handle_t handle,
	int *mode
	)
{
	/*
	 * Check for valid arguments and get capabilities.
	 */

	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}

	if (!mode) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}
	*mode = PPS_CAP;
	return (0);
}

/*
 * Fetch timestamps
 */

static inline int
time_pps_fetch(
	pps_handle_t handle,
	const int tsformat,
	pps_info_t *ppsinfo,
	const struct timespec *timeout
	)
{
	struct timeval tv;
	pps_info_t infobuf;

	/*
	 * Check for valid arguments and fetch timestamps
	 */

	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}

	if (!ppsinfo) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}

	/*
	 * nb. PPS_CANWAIT is NOT set by the implementation, we can totally
	 * ignore the timeout variable.
	 */

	memset(&infobuf, 0, sizeof(infobuf));

	/*
	 * if not captureassert, nothing to return.
	 */

	if (!handle->params.mode & PPS_CAPTUREASSERT) {
		memcpy(ppsinfo, &infobuf, sizeof(pps_info_t));
		return (0);
	}

	if (ioctl(instance->filedes, TIOCDCDTIMESTAMP, &tv) < 0) {
		perror("time_pps_fetch:");
		errno = EOPNOTSUPP;
		return(-1);
	}

	/*
	 * fake serial here
	 */

	if (tv.tv_sec != handle->tv_save.tv_sec || tv.tv_usec != handle->tv_save.tv_usec) {
		handle->tv_save = tv;
		handle->serial++;
	}

	/*
	 * Apply offsets as specified. Note that only assert timestamps
	 * are captured by this interface.
	 */

	infobuf.assert_sequence = handle->serial;
	infobuf.assert_timestamp.tv_sec = tv.tv_sec;
	infobuf.assert_timestamp.tv_nsec = tv.tv_usec * 1000;

	if (handle->params.mode & PPS_OFFSETASSERT) {
		infobuf.assert_timestamp.tv_sec  += handle->params.assert_offset.tv_sec;
		infobuf.assert_timestamp.tv_nsec += handle->params.assert_offset.tv_nsec;
		PPS_NORMALIZE(infobuf.assert_timestamp);
	}

	/*
	 * Translate to specified format
	 */

	switch (tsformat) {
	case PPS_TSFMT_TSPEC:
		break;		 /* timespec format requires no translation */

	case PPS_TSFMT_NTPFP:	/* NTP format requires conversion to fraction form */
		PPS_TSPECTONTP(infobuf.assert_timestamp_ntpfp);
		break;

	default:
		errno = EINVAL;
		return (-1);
	}

	infobuf.current_mode = handle->params.mode;
	memcpy(ppsinfo, &infobuf, sizeof(pps_info_t));
	return (0);
}

/*
 * specify kernel consumer
 */

static inline int
time_pps_kcbind(
	pps_handle_t handle,
	const int kernel_consumer,
	const int edge, const int tsformat
	)
{
	/*
	 * Check for valid arguments and bind kernel consumer
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (geteuid() != 0) {
		errno = EPERM;
		return (-1);	/* must be superuser */
	}
	errno = EOPNOTSUPP;
	return(-1);
}

#endif /* _SYS_TIMEPPS_H_ */
