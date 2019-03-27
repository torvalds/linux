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
 * \file zpool_list.h
 *
 * ZpoolList class definition.  ZpoolList is a standard container
 * allowing filtering and iteration of imported ZFS pool information.
 *
 * Header requirements:
 *
 *    #include <list>
 *    #include <string>
 */
#ifndef	_ZPOOL_LIST_H_
#define	_ZPOOL_LIST_H_

/*============================ Namespace Control =============================*/
using std::string;

/*=========================== Forward Declarations ===========================*/
struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

class Vdev;

/*============================= Class Definitions ============================*/
/*--------------------------------- ZpoolList --------------------------------*/
class ZpoolList;
typedef bool PoolFilter_t(zpool_handle_t *pool, nvlist_t *poolConfig,
			  void *filterArg);

/**
 * \brief Container of imported ZFS pool data.
 *
 * ZpoolList is a convenience class that converts libzfs's ZFS
 * pool methods into a standard list container.
 */
class ZpoolList : public std::list<zpool_handle_t *>
{
public:
	/**
	 * \brief Utility ZpoolList construction filter that causes all
	 *        pools known to the system to be included in the
	 *        instantiated ZpoolList.
	 */
	static PoolFilter_t ZpoolAll;

	/**
	 * \brief Utility ZpoolList construction filter that causes only
	 *        a pool known to the system and having the specified GUID
	 *        to be included in the instantiated ZpoolList.
	 */
	static PoolFilter_t ZpoolByGUID;

	/**
	 * \brief Utility ZpoolList construction filter that causes only
	 *        pools known to the system and having the specified name
	 *        to be included in the instantiated ZpoolList.
	 */
	static PoolFilter_t ZpoolByName;

	/**
	 * \brief ZpoolList contructor
	 *
	 * \param filter     The filter function to use when constructing
	 *                   the ZpoolList.  This may be one of the static
	 *                   utility filters defined for ZpoolList or a
	 *                   user defined function.
	 * \param filterArg  A single argument to pass into the filter function
	 *                   when it is invoked on each candidate pool.
	 */
	ZpoolList(PoolFilter_t *filter = ZpoolAll, void *filterArg = NULL);
	~ZpoolList();

private:
	/**
	 * \brief Helper routine used to populate the internal
	 *        data store of ZFS pool objects using libzfs's
	 *        zpool_iter() function.
	 *
	 * \param pool  The ZFS pool object to filter.
	 * \param data  User argument passed through zpool_iter().
	 */
	static int LoadIterator(zpool_handle_t *pool, void *data);

	/**
	 * \brief The filter with which this ZpoolList was constructed.
	 */
	PoolFilter_t *m_filter;

	/**
	 * \brief The filter argument with which this ZpoolList was
	 *        constructed.
	 */
	void	     *m_filterArg;
};

#endif	/* _ZPOOL_ITERATOR_H_ */
