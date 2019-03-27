/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfdt.h>

#include "util.h"

/* These are the operations we support */
enum oper_type {
	OPER_WRITE_PROP,		/* Write a property in a node */
	OPER_CREATE_NODE,		/* Create a new node */
	OPER_REMOVE_NODE,		/* Delete a node */
	OPER_DELETE_PROP,		/* Delete a property in a node */
};

struct display_info {
	enum oper_type oper;	/* operation to perform */
	int type;		/* data type (s/i/u/x or 0 for default) */
	int size;		/* data size (1/2/4) */
	int verbose;		/* verbose output */
	int auto_path;		/* automatically create all path components */
};


/**
 * Report an error with a particular node.
 *
 * @param name		Node name to report error on
 * @param namelen	Length of node name, or -1 to use entire string
 * @param err		Error number to report (-FDT_ERR_...)
 */
static void report_error(const char *name, int namelen, int err)
{
	if (namelen == -1)
		namelen = strlen(name);
	fprintf(stderr, "Error at '%1.*s': %s\n", namelen, name,
		fdt_strerror(err));
}

/**
 * Encode a series of arguments in a property value.
 *
 * @param disp		Display information / options
 * @param arg		List of arguments from command line
 * @param arg_count	Number of arguments (may be 0)
 * @param valuep	Returns buffer containing value
 * @param *value_len	Returns length of value encoded
 */
static int encode_value(struct display_info *disp, char **arg, int arg_count,
			char **valuep, int *value_len)
{
	char *value = NULL;	/* holding area for value */
	int value_size = 0;	/* size of holding area */
	char *ptr;		/* pointer to current value position */
	int len;		/* length of this cell/string/byte */
	int ival;
	int upto;	/* the number of bytes we have written to buf */
	char fmt[3];

	upto = 0;

	if (disp->verbose)
		fprintf(stderr, "Decoding value:\n");

	fmt[0] = '%';
	fmt[1] = disp->type ? disp->type : 'd';
	fmt[2] = '\0';
	for (; arg_count > 0; arg++, arg_count--, upto += len) {
		/* assume integer unless told otherwise */
		if (disp->type == 's')
			len = strlen(*arg) + 1;
		else
			len = disp->size == -1 ? 4 : disp->size;

		/* enlarge our value buffer by a suitable margin if needed */
		if (upto + len > value_size) {
			value_size = (upto + len) + 500;
			value = xrealloc(value, value_size);
		}

		ptr = value + upto;
		if (disp->type == 's') {
			memcpy(ptr, *arg, len);
			if (disp->verbose)
				fprintf(stderr, "\tstring: '%s'\n", ptr);
		} else {
			int *iptr = (int *)ptr;
			sscanf(*arg, fmt, &ival);
			if (len == 4)
				*iptr = cpu_to_fdt32(ival);
			else
				*ptr = (uint8_t)ival;
			if (disp->verbose) {
				fprintf(stderr, "\t%s: %d\n",
					disp->size == 1 ? "byte" :
					disp->size == 2 ? "short" : "int",
					ival);
			}
		}
	}
	*value_len = upto;
	*valuep = value;
	if (disp->verbose)
		fprintf(stderr, "Value size %d\n", upto);
	return 0;
}

#define ALIGN(x)		(((x) + (FDT_TAGSIZE) - 1) & ~((FDT_TAGSIZE) - 1))

static char *_realloc_fdt(char *fdt, int delta)
{
	int new_sz = fdt_totalsize(fdt) + delta;
	fdt = xrealloc(fdt, new_sz);
	fdt_open_into(fdt, fdt, new_sz);
	return fdt;
}

static char *realloc_node(char *fdt, const char *name)
{
	int delta;
	/* FDT_BEGIN_NODE, node name in off_struct and FDT_END_NODE */
	delta = sizeof(struct fdt_node_header) + ALIGN(strlen(name) + 1)
			+ FDT_TAGSIZE;
	return _realloc_fdt(fdt, delta);
}

static char *realloc_property(char *fdt, int nodeoffset,
		const char *name, int newlen)
{
	int delta = 0;
	int oldlen = 0;

	if (!fdt_get_property(fdt, nodeoffset, name, &oldlen))
		/* strings + property header */
		delta = sizeof(struct fdt_property) + strlen(name) + 1;

	if (newlen > oldlen)
		/* actual value in off_struct */
		delta += ALIGN(newlen) - ALIGN(oldlen);

	return _realloc_fdt(fdt, delta);
}

static int store_key_value(char **blob, const char *node_name,
		const char *property, const char *buf, int len)
{
	int node;
	int err;

	node = fdt_path_offset(*blob, node_name);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	err = fdt_setprop(*blob, node, property, buf, len);
	if (err == -FDT_ERR_NOSPACE) {
		*blob = realloc_property(*blob, node, property, len);
		err = fdt_setprop(*blob, node, property, buf, len);
	}
	if (err) {
		report_error(property, -1, err);
		return -1;
	}
	return 0;
}

/**
 * Create paths as needed for all components of a path
 *
 * Any components of the path that do not exist are created. Errors are
 * reported.
 *
 * @param blob		FDT blob to write into
 * @param in_path	Path to process
 * @return 0 if ok, -1 on error
 */
static int create_paths(char **blob, const char *in_path)
{
	const char *path = in_path;
	const char *sep;
	int node, offset = 0;

	/* skip leading '/' */
	while (*path == '/')
		path++;

	for (sep = path; *sep; path = sep + 1, offset = node) {
		/* equivalent to strchrnul(), but it requires _GNU_SOURCE */
		sep = strchr(path, '/');
		if (!sep)
			sep = path + strlen(path);

		node = fdt_subnode_offset_namelen(*blob, offset, path,
				sep - path);
		if (node == -FDT_ERR_NOTFOUND) {
			*blob = realloc_node(*blob, path);
			node = fdt_add_subnode_namelen(*blob, offset, path,
						       sep - path);
		}
		if (node < 0) {
			report_error(path, sep - path, node);
			return -1;
		}
	}

	return 0;
}

/**
 * Create a new node in the fdt.
 *
 * This will overwrite the node_name string. Any error is reported.
 *
 * TODO: Perhaps create fdt_path_offset_namelen() so we don't need to do this.
 *
 * @param blob		FDT blob to write into
 * @param node_name	Name of node to create
 * @return new node offset if found, or -1 on failure
 */
static int create_node(char **blob, const char *node_name)
{
	int node = 0;
	char *p;

	p = strrchr(node_name, '/');
	if (!p) {
		report_error(node_name, -1, -FDT_ERR_BADPATH);
		return -1;
	}
	*p = '\0';

	*blob = realloc_node(*blob, p + 1);

	if (p > node_name) {
		node = fdt_path_offset(*blob, node_name);
		if (node < 0) {
			report_error(node_name, -1, node);
			return -1;
		}
	}

	node = fdt_add_subnode(*blob, node, p + 1);
	if (node < 0) {
		report_error(p + 1, -1, node);
		return -1;
	}

	return 0;
}

/**
 * Delete a property of a node in the fdt.
 *
 * @param blob		FDT blob to write into
 * @param node_name	Path to node containing the property to delete
 * @param prop_name	Name of property to delete
 * @return 0 on success, or -1 on failure
 */
static int delete_prop(char *blob, const char *node_name, const char *prop_name)
{
	int node = 0;

	node = fdt_path_offset(blob, node_name);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	node = fdt_delprop(blob, node, prop_name);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	return 0;
}

/**
 * Delete a node in the fdt.
 *
 * @param blob		FDT blob to write into
 * @param node_name	Name of node to delete
 * @return 0 on success, or -1 on failure
 */
static int delete_node(char *blob, const char *node_name)
{
	int node = 0;

	node = fdt_path_offset(blob, node_name);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	node = fdt_del_node(blob, node);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	return 0;
}

static int do_fdtput(struct display_info *disp, const char *filename,
		    char **arg, int arg_count)
{
	char *value = NULL;
	char *blob;
	char *node;
	int len, ret = 0;

	blob = utilfdt_read(filename);
	if (!blob)
		return -1;

	switch (disp->oper) {
	case OPER_WRITE_PROP:
		/*
		 * Convert the arguments into a single binary value, then
		 * store them into the property.
		 */
		assert(arg_count >= 2);
		if (disp->auto_path && create_paths(&blob, *arg))
			return -1;
		if (encode_value(disp, arg + 2, arg_count - 2, &value, &len) ||
			store_key_value(&blob, *arg, arg[1], value, len))
			ret = -1;
		break;
	case OPER_CREATE_NODE:
		for (; ret >= 0 && arg_count--; arg++) {
			if (disp->auto_path)
				ret = create_paths(&blob, *arg);
			else
				ret = create_node(&blob, *arg);
		}
		break;
	case OPER_REMOVE_NODE:
		for (; ret >= 0 && arg_count--; arg++)
			ret = delete_node(blob, *arg);
		break;
	case OPER_DELETE_PROP:
		node = *arg;
		for (arg++; ret >= 0 && arg_count-- > 1; arg++)
			ret = delete_prop(blob, node, *arg);
		break;
	}
	if (ret >= 0) {
		fdt_pack(blob);
		ret = utilfdt_write(filename, blob);
	}

	free(blob);

	if (value) {
		free(value);
	}

	return ret;
}

/* Usage related data. */
static const char usage_synopsis[] =
	"write a property value to a device tree\n"
	"	fdtput <options> <dt file> <node> <property> [<value>...]\n"
	"	fdtput -c <options> <dt file> [<node>...]\n"
	"	fdtput -r <options> <dt file> [<node>...]\n"
	"	fdtput -d <options> <dt file> <node> [<property>...]\n"
	"\n"
	"The command line arguments are joined together into a single value.\n"
	USAGE_TYPE_MSG;
static const char usage_short_opts[] = "crdpt:v" USAGE_COMMON_SHORT_OPTS;
static struct option const usage_long_opts[] = {
	{"create",           no_argument, NULL, 'c'},
	{"remove",	     no_argument, NULL, 'r'},
	{"delete",	     no_argument, NULL, 'd'},
	{"auto-path",        no_argument, NULL, 'p'},
	{"type",              a_argument, NULL, 't'},
	{"verbose",          no_argument, NULL, 'v'},
	USAGE_COMMON_LONG_OPTS,
};
static const char * const usage_opts_help[] = {
	"Create nodes if they don't already exist",
	"Delete nodes (and any subnodes) if they already exist",
	"Delete properties if they already exist",
	"Automatically create nodes as needed for the node path",
	"Type of data",
	"Display each value decoded from command line",
	USAGE_COMMON_OPTS_HELP
};

int main(int argc, char *argv[])
{
	int opt;
	struct display_info disp;
	char *filename = NULL;

	memset(&disp, '\0', sizeof(disp));
	disp.size = -1;
	disp.oper = OPER_WRITE_PROP;
	while ((opt = util_getopt_long()) != EOF) {
		/*
		 * TODO: add options to:
		 * - rename node
		 * - pack fdt before writing
		 * - set amount of free space when writing
		 */
		switch (opt) {
		case_USAGE_COMMON_FLAGS

		case 'c':
			disp.oper = OPER_CREATE_NODE;
			break;
		case 'r':
			disp.oper = OPER_REMOVE_NODE;
			break;
		case 'd':
			disp.oper = OPER_DELETE_PROP;
			break;
		case 'p':
			disp.auto_path = 1;
			break;
		case 't':
			if (utilfdt_decode_type(optarg, &disp.type,
					&disp.size))
				usage("Invalid type string");
			break;

		case 'v':
			disp.verbose = 1;
			break;
		}
	}

	if (optind < argc)
		filename = argv[optind++];
	if (!filename)
		usage("missing filename");

	argv += optind;
	argc -= optind;

	if (disp.oper == OPER_WRITE_PROP) {
		if (argc < 1)
			usage("missing node");
		if (argc < 2)
			usage("missing property");
	}

	if (disp.oper == OPER_DELETE_PROP)
		if (argc < 1)
			usage("missing node");

	if (do_fdtput(&disp, filename, argv, argc))
		return 1;
	return 0;
}
