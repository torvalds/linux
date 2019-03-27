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
 * \file vdev_iterator.h
 *
 * VdevIterator class definition.
 *
 * Header requirements:
 *
 *    #include <list>
 */
#ifndef	_VDEV_ITERATOR_H_
#define	_VDEV_ITERATOR_H_

/*=========================== Forward Declarations ===========================*/
struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

class Vdev;

/*============================= Class Definitions ============================*/
/*------------------------------- VdevIterator -------------------------------*/
typedef bool VdevCallback_t(Vdev &vdev, void *cbArg);

/**
 * \brief VdevIterator provides mechanisms for traversing and searching
 *        the leaf vdevs contained in a ZFS pool configuration.
 */
class VdevIterator
{
public:
	/**
	 * \brief Instantiate a VdevIterator for the given ZFS pool.
	 *
	 * \param pool  The ZFS pool to traverse/search.
	 */
	VdevIterator(zpool_handle_t *pool);

	/**
	 * \brief Instantiate a VdevIterator for the given ZFS pool.
	 *
	 * \param poolConfig  The configuration data for the ZFS pool
	 *                    to traverse/search.
	 */
	VdevIterator(nvlist_t *poolConfig);

	/**
	 * \brief Reset this iterator's cursor so that Next() will
	 *        report the first member of the pool.
	 */
	void      Reset();

	/**
	 * \brief Report the leaf vdev at this iterator's cursor and increment
	 *        the cursor to the next leaf pool member.
	 */
	nvlist_t *Next();

	/**
	 * \brief Traverse the entire pool configuration starting its
	 *        first member, returning a vdev object with the given
	 *        vdev GUID if found.
	 *
	 * \param vdevGUID  The vdev GUID of the vdev object to find.
	 *
	 * \return  A Vdev object for the matching vdev if found.  Otherwise
	 *          NULL.
	 *
	 * Upon return, the VdevIterator's cursor points to the vdev just
	 * past the returned vdev or end() if no matching vdev is found.
	 */
	nvlist_t *Find(DevdCtl::Guid vdevGUID);

	/**
	 * \brief Perform the specified operation on each leaf member of
	 *        a pool's vdev membership.
	 *
	 * \param cb     Callback function to execute for each member.
	 * \param cbArg  Argument to pass to cb.
	 */
	void	  Each(VdevCallback_t *cb, void *cbArg);

private:
	nvlist_t                *m_poolConfig;
	std::list<nvlist_t *>	 m_vdevQueue;
};

#endif	/* _VDEV_ITERATOR_H_ */
