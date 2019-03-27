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

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <config.h>
#include <errno.h>

#include <complib/cl_nodenamemap.h>
#include <complib/cl_math.h>

#define PARSE_NODE_MAP_BUFLEN  256

static int parse_node_map_wrap(const char *file_name,
			       int (*create)(void *, uint64_t, char *),
			       void *cxt,
			       char *linebuf,
			       unsigned int linebuflen);

static int map_name(void *cxt, uint64_t guid, char *p)
{
	cl_qmap_t *map = cxt;
	name_map_item_t *item;

	p = strtok(p, "\"#");
	if (!p)
		return 0;

	item = malloc(sizeof(*item));
	if (!item)
		return -1;
	item->guid = guid;
	item->name = strdup(p);
	cl_qmap_insert(map, item->guid, (cl_map_item_t *) item);
	return 0;
}

nn_map_t *open_node_name_map(const char *node_name_map)
{
	nn_map_t *map;
	char linebuf[PARSE_NODE_MAP_BUFLEN + 1];

	if (!node_name_map) {
#ifdef HAVE_DEFAULT_NODENAME_MAP
		struct stat buf;
		node_name_map = HAVE_DEFAULT_NODENAME_MAP;
		if (stat(node_name_map, &buf))
			return NULL;
#else
		return NULL;
#endif				/* HAVE_DEFAULT_NODENAME_MAP */
	}

	map = malloc(sizeof(*map));
	if (!map)
		return NULL;
	cl_qmap_init(map);

	memset(linebuf, '\0', PARSE_NODE_MAP_BUFLEN + 1);
	if (parse_node_map_wrap(node_name_map, map_name, map,
				linebuf, PARSE_NODE_MAP_BUFLEN)) {
		if (errno == EIO) {
			fprintf(stderr,
				"WARNING failed to parse node name map "
				"\"%s\"\n",
				node_name_map);
			fprintf(stderr,
				"WARNING failed line: \"%s\"\n",
				linebuf);
		}
		else
			fprintf(stderr,
				"WARNING failed to open node name map "
				"\"%s\" (%s)\n",
				node_name_map, strerror(errno));
		close_node_name_map(map);
		return NULL;
	}

	return map;
}

void close_node_name_map(nn_map_t * map)
{
	name_map_item_t *item = NULL;

	if (!map)
		return;

	item = (name_map_item_t *) cl_qmap_head(map);
	while (item != (name_map_item_t *) cl_qmap_end(map)) {
		item = (name_map_item_t *) cl_qmap_remove(map, item->guid);
		free(item->name);
		free(item);
		item = (name_map_item_t *) cl_qmap_head(map);
	}
	free(map);
}

char *remap_node_name(nn_map_t * map, uint64_t target_guid, char *nodedesc)
{
	char *rc = NULL;
	name_map_item_t *item = NULL;

	if (!map)
		goto done;

	item = (name_map_item_t *) cl_qmap_get(map, target_guid);
	if (item != (name_map_item_t *) cl_qmap_end(map))
		rc = strdup(item->name);

done:
	if (rc == NULL)
		rc = strdup(clean_nodedesc(nodedesc));
	return (rc);
}

char *clean_nodedesc(char *nodedesc)
{
	int i = 0;

	nodedesc[63] = '\0';
	while (nodedesc[i]) {
		if (!isprint(nodedesc[i]))
			nodedesc[i] = ' ';
		i++;
	}

	return (nodedesc);
}

static int parse_node_map_wrap(const char *file_name,
			       int (*create) (void *, uint64_t, char *),
			       void *cxt,
			       char *linebuf,
			       unsigned int linebuflen)
{
	char line[PARSE_NODE_MAP_BUFLEN];
	FILE *f;

	if (!(f = fopen(file_name, "r")))
		return -1;

	while (fgets(line, sizeof(line), f)) {
		uint64_t guid;
		char *p, *e;

		p = line;
		while (isspace(*p))
			p++;
		if (*p == '\0' || *p == '\n' || *p == '#')
			continue;

		guid = strtoull(p, &e, 0);
		if (e == p || (!isspace(*e) && *e != '#' && *e != '\0')) {
			fclose(f);
			errno = EIO;
			if (linebuf) {
				memcpy(linebuf, line,
				       MIN(PARSE_NODE_MAP_BUFLEN, linebuflen));
				e = strpbrk(linebuf, "\n");
				if (e)
					*e = '\0';
			}
			return -1;
		}

		p = e;
		while (isspace(*p))
			p++;

		e = strpbrk(p, "\n");
		if (e)
			*e = '\0';

		if (create(cxt, guid, p)) {
			fclose(f);
			return -1;
		}
	}

	fclose(f);
	return 0;
}

int parse_node_map(const char *file_name,
		   int (*create) (void *, uint64_t, char *), void *cxt)
{
	return parse_node_map_wrap(file_name, create, cxt, NULL, 0);
}
