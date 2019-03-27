/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *    Implementation of osm_pkt_randomizer_t.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <vendor/osm_pkt_randomizer.h>
#include <stdlib.h>
#include <string.h>

#ifndef __WIN__
#include <sys/time.h>
#include <unistd.h>
#endif

/**********************************************************************
 * Return TRUE if the path is in a fault path, and FALSE otherwise.
 * By in a fault path the meaning is that there is a path in the fault
 * paths that the given path includes it.
 * E.g: if there is a fault path: 0,1,4
 * For the given path: 0,1,4,7 the return value will be TRUE, also for
 * the given path: 0,1,4 the return value will be TRUE, but for
 * the given paths: 0,1 or 0,3,1,4 - the return value will be FALSE.
 **********************************************************************/
boolean_t
__osm_pkt_randomizer_is_path_in_fault_paths(IN osm_log_t * p_log,
					    IN osm_dr_path_t * p_dr_path,
					    IN osm_pkt_randomizer_t *
					    p_pkt_rand)
{
	boolean_t res = FALSE, found_path;
	osm_dr_path_t *p_found_dr_path;
	uint8_t ind1, ind2;

	OSM_LOG_ENTER(p_log);

	for (ind1 = 0; ind1 < p_pkt_rand->num_paths_initialized; ind1++) {
		found_path = TRUE;
		p_found_dr_path = &(p_pkt_rand->fault_dr_paths[ind1]);
		/* if the hop count of the found path is greater than the
		   hop count of the input path - then it is not part of it.
		   Check the next path. */
		if (p_found_dr_path->hop_count > p_dr_path->hop_count)
			continue;

		/* go over all the ports in the found path and see if they match
		   the ports in the input path */
		for (ind2 = 0; ind2 <= p_found_dr_path->hop_count; ind2++)
			if (p_found_dr_path->path[ind2] !=
			    p_dr_path->path[ind2])
				found_path = FALSE;

		/* If found_path is TRUE  then there is a full match of the path */
		if (found_path == TRUE) {
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"Given path is in a fault path\n");
			res = TRUE;
			break;
		}
	}

	OSM_LOG_EXIT(p_log);
	return res;
}

/**********************************************************************
 * For a given dr_path - return TRUE if the path should be dropped,
 * return FALSE otherwise.
 * The check uses random criteria in order to determine whether or not
 * the path should be dropped.
 * First - if not all paths are initialized, it randomally chooses if
 * to use this path as a fault path or not.
 * Second - if the path is in the fault paths (meaning - it is equal
 * to or includes one of the fault paths) - then it randomally chooses
 * if to drop it or not.
 **********************************************************************/
boolean_t
__osm_pkt_randomizer_process_path(IN osm_log_t * p_log,
				  IN osm_pkt_randomizer_t * p_pkt_rand,
				  IN osm_dr_path_t * p_dr_path)
{
	boolean_t res = FALSE;
	static boolean_t rand_value_init = FALSE;
	static int rand_value;
	boolean_t in_fault_paths;
	uint8_t i;
	char buf[BUF_SIZE];
	char line[BUF_SIZE];

	OSM_LOG_ENTER(p_log);

	if (rand_value_init == FALSE) {
		int seed;
#ifdef __WIN__
		SYSTEMTIME st;
#else
		struct timeval tv;
		struct timezone tz;
#endif				/*  __WIN__ */

		/* initiate the rand_value according to timeofday */
		rand_value_init = TRUE;

#ifdef __WIN__
		GetLocalTime(&st);
		seed = st.wMilliseconds;
#else
		gettimeofday(&tv, &tz);
		seed = tv.tv_usec;
#endif				/*  __WIN__ */

		srand(seed);
	}

	/* If the hop_count is 1 - then this is a mad down to our local port - don't drop it */
	if (p_dr_path->hop_count <= 1)
		goto Exit;

	rand_value = rand();

	sprintf(buf, "Path: ");
	/* update the dr_path into the buf */
	for (i = 0; i <= p_dr_path->hop_count; i++) {
		sprintf(line, "[%X]", p_dr_path->path[i]);
		strcat(buf, line);
	}

	/* Check if the path given is in one of the fault paths */
	in_fault_paths =
	    __osm_pkt_randomizer_is_path_in_fault_paths(p_log, p_dr_path,
							p_pkt_rand);

	/* Check if all paths are initialized */
	if (p_pkt_rand->num_paths_initialized <
	    p_pkt_rand->osm_pkt_num_unstable_links) {
		/* Not all packets are initialized. */
		if (in_fault_paths == FALSE) {
			/* the path is not in the false paths. Check using the rand value
			   if to update it there or not. */
			if (rand_value %
			    (p_pkt_rand->osm_pkt_unstable_link_rate) == 0) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"%s added to the fault_dr_paths list\n"
					"\t\t\t rand_value:%u, unstable_link_rate:%u \n",
					buf, rand_value,
					p_pkt_rand->osm_pkt_unstable_link_rate);

				/* update the path in the fault paths */
				memcpy(&
				       (p_pkt_rand->
					fault_dr_paths[p_pkt_rand->
						       num_paths_initialized]),
				       p_dr_path, sizeof(osm_dr_path_t));
				p_pkt_rand->num_paths_initialized++;
				in_fault_paths = TRUE;
			}
		}
	}

	if (in_fault_paths == FALSE) {
		/* If in_fault_paths is FALSE - just ignore the path */
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "%s not in fault paths\n", buf);
		goto Exit;
	}

	/* The path is in the fault paths. Need to choose (randomally if to drop it
	   or not. */
	rand_value = rand();

	if (rand_value % (p_pkt_rand->osm_pkt_drop_rate) == 0) {
		/* drop the current packet */
		res = TRUE;
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "Dropping path:%s\n", buf);
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return res;
}

boolean_t
osm_pkt_randomizer_mad_drop(IN osm_log_t * p_log,
			    IN osm_pkt_randomizer_t * p_pkt_randomizer,
			    IN const ib_mad_t * p_mad)
{
	const ib_smp_t *p_smp;
	boolean_t res = FALSE;
	osm_dr_path_t dr_path;

	OSM_LOG_ENTER(p_log);

	p_smp = (ib_smp_t *) p_mad;

	if (p_smp->mgmt_class != IB_MCLASS_SUBN_DIR)
		/* This is a lid route mad. Don't drop it */
		goto Exit;

	osm_dr_path_init(&dr_path, p_smp->hop_count, p_smp->initial_path);

	if (__osm_pkt_randomizer_process_path
	    (p_log, p_pkt_randomizer, &dr_path)) {
		/* the mad should be dropped o */
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"mad TID: 0x%" PRIx64 " is being dropped\n",
			cl_ntoh64(p_smp->trans_id));
		res = TRUE;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return res;
}

ib_api_status_t
osm_pkt_randomizer_init(IN OUT osm_pkt_randomizer_t ** pp_pkt_randomizer,
			IN osm_log_t * p_log)
{
	uint8_t tmp;
	ib_api_status_t res = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	*pp_pkt_randomizer = malloc(sizeof(osm_pkt_randomizer_t));
	if (*pp_pkt_randomizer == NULL) {
		res = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}
	memset(*pp_pkt_randomizer, 0, sizeof(osm_pkt_randomizer_t));
	(*pp_pkt_randomizer)->num_paths_initialized = 0;

	tmp = atol(getenv("OSM_PKT_DROP_RATE"));
	(*pp_pkt_randomizer)->osm_pkt_drop_rate = tmp;

	if (getenv("OSM_PKT_NUM_UNSTABLE_LINKS") != NULL
	    && (tmp = atol(getenv("OSM_PKT_NUM_UNSTABLE_LINKS"))) > 0)
		(*pp_pkt_randomizer)->osm_pkt_num_unstable_links = tmp;
	else
		(*pp_pkt_randomizer)->osm_pkt_num_unstable_links = 1;

	if (getenv("OSM_PKT_UNSTABLE_LINK_RATE") != NULL
	    && (tmp = atol(getenv("OSM_PKT_UNSTABLE_LINK_RATE"))) > 0)
		(*pp_pkt_randomizer)->osm_pkt_unstable_link_rate = tmp;
	else
		(*pp_pkt_randomizer)->osm_pkt_unstable_link_rate = 20;

	OSM_LOG(p_log, OSM_LOG_VERBOSE, "Using OSM_PKT_DROP_RATE=%u \n"
		"\t\t\t\t OSM_PKT_NUM_UNSTABLE_LINKS=%u \n"
		"\t\t\t\t OSM_PKT_UNSTABLE_LINK_RATE=%u \n",
		(*pp_pkt_randomizer)->osm_pkt_drop_rate,
		(*pp_pkt_randomizer)->osm_pkt_num_unstable_links,
		(*pp_pkt_randomizer)->osm_pkt_unstable_link_rate);

	/* allocate the fault_dr_paths variable */
	/* It is the number of the paths that will be saved as fault = osm_pkt_num_unstable_links */
	(*pp_pkt_randomizer)->fault_dr_paths = malloc(sizeof(osm_dr_path_t) *
						      (*pp_pkt_randomizer)->
						      osm_pkt_num_unstable_links);
	if ((*pp_pkt_randomizer)->fault_dr_paths == NULL) {
		res = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

	memset((*pp_pkt_randomizer)->fault_dr_paths, 0,
	       sizeof(osm_dr_path_t) *
	       (*pp_pkt_randomizer)->osm_pkt_num_unstable_links);

Exit:
	OSM_LOG_EXIT(p_log);
	return (res);
}

void
osm_pkt_randomizer_destroy(IN OUT osm_pkt_randomizer_t ** pp_pkt_randomizer,
			   IN osm_log_t * p_log)
{
	OSM_LOG_ENTER(p_log);

	if (*pp_pkt_randomizer != NULL) {
		free((*pp_pkt_randomizer)->fault_dr_paths);
		free(*pp_pkt_randomizer);
	}
	OSM_LOG_EXIT(p_log);
}
