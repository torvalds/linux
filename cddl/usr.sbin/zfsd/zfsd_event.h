/*-
 * Copyright (c) 2011, 2012, 2013, 2014, 2016 Spectra Logic Corporation
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
 * \file dev_ctl_event.h
 *
 * \brief Class hierarchy used to express events received via
 *        the devdctl API.
 *
 * Header requirements:
 *    #include <string>
 *    #include <list>
 *    #include <map>
 *
 *    #include <devdctl/guid.h>
 *    #include <devdctl/event.h>
 */

#ifndef _ZFSD_EVENT_H_
#define	_ZFSD_EVENT_H_

/*============================ Namespace Control =============================*/
using std::string;

/*=========================== Forward Declarations ===========================*/
struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

/*--------------------------------- ZfsEvent ---------------------------------*/
class ZfsEvent : public DevdCtl::ZfsEvent
{
public:
	/** Specialized DevdCtlEvent object factory for ZFS events. */
	static BuildMethod Builder;

	virtual DevdCtl::Event *DeepCopy() const;

	/**
	 * Interpret and perform any actions necessary to
	 * consume the event.
	 * \return True if this event should be queued for later reevaluation
	 */
	virtual bool Process()		  const;

protected:
	/** DeepCopy Constructor. */
	ZfsEvent(const ZfsEvent &src);

	/** Constructor */
	ZfsEvent(Type, DevdCtl::NVPairMap &, const string &);

	/**
	 * Detach any spares that are no longer needed, but were not
	 * automatically detached by the kernel
	 */
	virtual void CleanupSpares()	  const;
	virtual void ProcessPoolEvent()	  const;
	static VdevCallback_t TryDetach;
};

class GeomEvent : public DevdCtl::GeomEvent
{
public:
	static BuildMethod Builder;

	virtual DevdCtl::Event *DeepCopy() const; 

	virtual bool Process()		  const;

protected:
	/** DeepCopy Constructor. */
	GeomEvent(const GeomEvent &src);

	/** Constructor */
	GeomEvent(Type, DevdCtl::NVPairMap &, const string &);

	/**
	 * Attempt to match the ZFS labeled device at devPath with an active
	 * CaseFile for a missing vdev.  If a CaseFile is found, attempt
	 * to re-integrate the device with its pool.
	 *
	 * \param devPath    The devfs path to the potential leaf vdev.
	 * \param physPath   The physical path string reported by the device
	 *                   at devPath.
	 * \param devConfig  The ZFS label information found on the device
	 *                   at devPath.
	 *
	 * \return  true if the event that caused the online action can
	 *          be considered consumed.
	 */
	static bool	    OnlineByLabel(const string &devPath,
					  const string& physPath,
					  nvlist_t *devConfig);

	/**
	 * \brief Read and return label information for a device.
	 *
	 * \param devFd     The device from which to read ZFS label information.
	 * \param inUse     The device is part of an active or potentially
	 *                  active configuration.
	 * \param degraded  The device label indicates the vdev is not healthy.
	 *
	 * \return  If label information is available, an nvlist describing
	 *          the vdev configuraiton found on the device specified by
	 *          devFd.  Otherwise NULL.
	 */
	static nvlist_t    *ReadLabel(int devFd, bool &inUse, bool &degraded);

};
#endif /*_ZFSD_EVENT_H_ */
