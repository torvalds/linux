/*-
 * Copyright (c) 2011, 2012, 2013 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

/**
 * \file zfsd_exception.h
 *
 * Definition of the ZfsdException class hierarchy.  All exceptions
 * explicitly thrown by Zfsd are defined here.
 *
 * Header requirements:
 *     #include <string>
 *
 *     #include <devdctl/exception.h>
 */
#ifndef	_ZFSD_EXCEPTION_H_
#define	_ZFSD_EXCEPTION_H_

/*=========================== Forward Declarations ===========================*/
struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

/*============================= Class Definitions ============================*/
/*------------------------------- ZfsdException ------------------------------*/
/**
 * \brief Class allowing unified reporting/logging of exceptional events.
 */
class ZfsdException : public DevdCtl::Exception
{
public:
	/**
	 * \brief ZfsdException constructor allowing arbitrary string
	 *        data to be reported.
	 *
	 * \param fmt  Printf-like string format specifier.
	 */
	ZfsdException(const char *fmt, ...);

	/**
	 * \brief ZfsdException constructor allowing arbitrary string
	 *        data to be reported and associated with the configuration
	 *        data for a ZFS pool.
	 *
	 * \param pool  Pool handle describing the pool to which this
	 *              exception is associated.
	 * \param fmt   Printf-like string format specifier.
	 *
	 * Instantiation with this method is used to report global
	 * pool errors.
	 */
	ZfsdException(zpool_handle_t *pool, const char *, ...);

	/**
	 * \brief ZfsdException constructor allowing arbitrary string
	 *        data to be reported and associated with the configuration
	 *        data for a ZFS pool.
	 *
	 * \param poolConfig  Pool configuration describing the pool to
	 *                    which this exception is associated.
	 * \param fmt         Printf-like string format specifier.
	 *
	 * Instantiation with this method is used to report global
	 * pool errors.
	 */
	ZfsdException(nvlist_t *poolConfig, const char *, ...);

	/**
	 * \brief Emit exception data to syslog(3).
	 */
	virtual void Log() const;
private:
	nvlist_t     *m_poolConfig;
	nvlist_t     *m_vdevConfig;
};

#endif /* _ZFSD_EXCEPTION_H_ */
