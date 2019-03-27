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
 * \file vdev.h
 *
 * Definition of the Vdev class.
 *
 * Header requirements:
 *
 *    #include <string>
 *    #include <list>
 *
 *    #include <devdctl/guid.h>
 */
#ifndef	_VDEV_H_
#define	_VDEV_H_

/*=========================== Forward Declarations ===========================*/
struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

/*============================= Class Definitions ============================*/
/*----------------------------------- Vdev -----------------------------------*/
/**
 * \brief Wrapper class for a vdev's name/value configuration list
 *        simplifying access to commonly used vdev attributes.
 */
class Vdev
{
public:
	/**
	 * \brief Instantiate a vdev object for a vdev that is a member
	 *        of an imported pool.
	 *
	 * \param pool        The pool object containing the vdev with
	 *                    configuration data provided in vdevConfig.
	 * \param vdevConfig  Vdev configuration data.
	 *
	 * This method should be used whenever dealing with vdev's
	 * enumerated via the ZpoolList class.  The in-core configuration
	 * data for a vdev does not contain all of the items found in
	 * the on-disk label.  This requires the vdev class to augment
	 * the data in vdevConfig with data found in the pool object.
	 */
	Vdev(zpool_handle_t *pool, nvlist_t *vdevConfig);

	/**
	 * \brief Instantiate a vdev object for a vdev that is a member
	 *        of a pool configuration.
	 *
	 * \param poolConfig  The pool configuration containing the vdev
	 *                    configuration data provided in vdevConfig.
	 * \param vdevConfig  Vdev configuration data.
	 *
	 * This method should be used whenever dealing with vdev's
	 * enumerated via the ZpoolList class.  The in-core configuration
	 * data for a vdev does not contain all of the items found in
	 * the on-disk label.  This requires the vdev class to augment
	 * the data in vdevConfig with data found in the pool object.
	 */
	Vdev(nvlist_t *poolConfig, nvlist_t *vdevConfig);

	/**
	 * \brief Instantiate a vdev object from a ZFS label stored on
	 *        the device.
	 *
	 * \param vdevConfig  The name/value list retrieved by reading
	 *                    the label information on a leaf vdev.
	 */
	Vdev(nvlist_t *vdevConfig);

	/**
	 * \brief No-op copy constructor for nonexistent vdevs.
	 */
	Vdev();

	/**
	 * \brief No-op virtual destructor, since this class has virtual
	 *        functions.
	 */
	virtual ~Vdev();
	bool			DoesNotExist()	const;

	/**
	 * \brief Return a list of the vdev's children.
	 */
	std::list<Vdev>		 Children();

	virtual DevdCtl::Guid	 GUID()		const;
	bool			 IsSpare()	const;
	virtual DevdCtl::Guid	 PoolGUID()	const;
	virtual vdev_state	 State()	const;
	std::string		 Path()		const;
	virtual std::string	 PhysicalPath()	const;
	std::string		 GUIDString()	const;
	nvlist_t		*PoolConfig()	const;
	nvlist_t		*Config()	const;
	Vdev			 Parent();
	Vdev			 RootVdev();
	std::string		 Name(zpool_handle_t *, bool verbose)	const;
	bool			 IsSpare();
	bool			 IsAvailableSpare()	const;
	bool			 IsActiveSpare()	const;
	bool			 IsResilvering()	const;

private:
	void			 VdevLookupGuid();
	bool			 VdevLookupPoolGuid();
	DevdCtl::Guid		 m_poolGUID;
	DevdCtl::Guid		 m_vdevGUID;
	nvlist_t		*m_poolConfig;
	nvlist_t		*m_config;
};

//- Special objects -----------------------------------------------------------
extern Vdev NonexistentVdev;

//- Vdev Inline Public Methods ------------------------------------------------
inline Vdev::~Vdev()
{
}

inline DevdCtl::Guid
Vdev::PoolGUID() const
{
	return (m_poolGUID);
}

inline DevdCtl::Guid
Vdev::GUID() const
{
	return (m_vdevGUID);
}

inline nvlist_t *
Vdev::PoolConfig() const
{
	return (m_poolConfig);
}

inline nvlist_t *
Vdev::Config() const
{
	return (m_config);
}

inline bool
Vdev::DoesNotExist() const
{
	return (m_config == NULL);
}

#endif /* _VDEV_H_ */
