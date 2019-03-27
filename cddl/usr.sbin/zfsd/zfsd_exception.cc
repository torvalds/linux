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
 */

/**
 * \file zfsd_exception
 *
 * Implementation of the ZfsdException class.
 */
#include <sys/cdefs.h>
#include <sys/fs/zfs.h>

#include <syslog.h>

#include <string>
#include <list>
#include <sstream>

#include <devdctl/exception.h>
#include <devdctl/guid.h>

#include <libzfs.h>

#include "vdev.h"
#include "zfsd_exception.h"

__FBSDID("$FreeBSD$");
/*============================ Namespace Control =============================*/
using std::endl;
using std::string;
using std::stringstream;

/*=========================== Class Implementations ==========================*/
/*------------------------------- ZfsdException ------------------------------*/
ZfsdException::ZfsdException(const char *fmt, ...)
 : DevdCtl::Exception(),
   m_poolConfig(NULL),
   m_vdevConfig(NULL)
{
	va_list ap;

	va_start(ap, fmt);
	FormatLog(fmt, ap);
	va_end(ap);
}

ZfsdException::ZfsdException(zpool_handle_t *pool, const char *fmt, ...)
 : DevdCtl::Exception(),
   m_poolConfig(zpool_get_config(pool, NULL)),
   m_vdevConfig(NULL)
{
	va_list ap;

	va_start(ap, fmt);
	FormatLog(fmt, ap);
	va_end(ap);
}

ZfsdException::ZfsdException(nvlist_t *poolConfig, const char *fmt, ...)
 : DevdCtl::Exception(),
   m_poolConfig(poolConfig),
   m_vdevConfig(NULL)
{
	va_list ap;

	va_start(ap, fmt);
	FormatLog(fmt, ap);
	va_end(ap);
}

void
ZfsdException::Log() const
{
	stringstream output;

	if (m_poolConfig != NULL) {

		output << "Pool ";

		char *poolName;
		if (nvlist_lookup_string(m_poolConfig, ZPOOL_CONFIG_POOL_NAME,
				     &poolName) == 0)
			output << poolName;
		else
			output << "Unknown";
		output << ": ";
	}

	if (m_vdevConfig != NULL) {

		if (m_poolConfig != NULL) {
			Vdev vdev(m_poolConfig, m_vdevConfig);

			output << "Vdev " <<  vdev.GUID() << ": ";
		} else {
			Vdev vdev(m_vdevConfig);

			output << "Pool " <<  vdev.PoolGUID() << ": ";
			output << "Vdev " <<  vdev.GUID() << ": ";
		}
	}

	output << m_log << endl;
	syslog(LOG_ERR, "%s", output.str().c_str());
}

