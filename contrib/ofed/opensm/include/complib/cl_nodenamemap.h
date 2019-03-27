/*
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2007 Lawrence Livermore National Lab
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

#ifndef _CL_NODE_NAME_MAP_H_
#define _CL_NODE_NAME_MAP_H_

#include <stdio.h>
#include <stdint.h>
#include <complib/cl_qmap.h>

/* NOTE: this may modify the parameter "nodedesc". */
char *clean_nodedesc(char *nodedesc);

typedef struct _name_map_item {
	cl_map_item_t item;
	uint64_t guid;
	char *name;
} name_map_item_t;

typedef cl_qmap_t nn_map_t;

/**
 * Node name map interface.
 * It is OK to pass NULL for the node_name_map[_fp] parameters.
 */
nn_map_t *open_node_name_map(const char *node_name_map);
void  close_node_name_map(nn_map_t *map);
char *remap_node_name(nn_map_t *map, uint64_t target_guid, char *nodedesc);
	/* NOTE: parameter "nodedesc" may be modified here. */
int parse_node_map(const char *file_name,
		   int (*create)(void *, uint64_t, char *), void *cxt);

#endif				/* _CL_NODE_NAME_MAP_H_ */
