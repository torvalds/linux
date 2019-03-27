/*
 * Generic XML helper functions
 * Copyright (c) 2012-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "xml-utils.h"


static xml_node_t * get_node_uri_iter(struct xml_node_ctx *ctx,
				      xml_node_t *root, char *uri)
{
	char *end;
	xml_node_t *node;
	const char *name;

	end = strchr(uri, '/');
	if (end)
		*end++ = '\0';

	node = root;
	xml_node_for_each_sibling(ctx, node) {
		xml_node_for_each_check(ctx, node);
		name = xml_node_get_localname(ctx, node);
		if (strcasecmp(name, uri) == 0)
			break;
	}

	if (node == NULL)
		return NULL;

	if (end) {
		return get_node_uri_iter(ctx, xml_node_first_child(ctx, node),
					 end);
	}

	return node;
}


xml_node_t * get_node_uri(struct xml_node_ctx *ctx, xml_node_t *root,
			  const char *uri)
{
	char *search;
	xml_node_t *node;

	search = os_strdup(uri);
	if (search == NULL)
		return NULL;

	node = get_node_uri_iter(ctx, root, search);

	os_free(search);
	return node;
}


static xml_node_t * get_node_iter(struct xml_node_ctx *ctx,
				  xml_node_t *root, const char *path)
{
	char *end;
	xml_node_t *node;
	const char *name;

	end = os_strchr(path, '/');
	if (end)
		*end++ = '\0';

	xml_node_for_each_child(ctx, node, root) {
		xml_node_for_each_check(ctx, node);
		name = xml_node_get_localname(ctx, node);
		if (os_strcasecmp(name, path) == 0)
			break;
	}

	if (node == NULL)
		return NULL;
	if (end)
		return get_node_iter(ctx, node, end);
	return node;
}


xml_node_t * get_node(struct xml_node_ctx *ctx, xml_node_t *root,
		      const char *path)
{
	char *search;
	xml_node_t *node;

	search = os_strdup(path);
	if (search == NULL)
		return NULL;

	node = get_node_iter(ctx, root, search);

	os_free(search);
	return node;
}


xml_node_t * get_child_node(struct xml_node_ctx *ctx, xml_node_t *root,
			    const char *path)
{
	xml_node_t *node;
	xml_node_t *match;

	xml_node_for_each_child(ctx, node, root) {
		xml_node_for_each_check(ctx, node);
		match = get_node(ctx, node, path);
		if (match)
			return match;
	}

	return NULL;
}


xml_node_t * node_from_file(struct xml_node_ctx *ctx, const char *name)
{
	xml_node_t *node;
	char *buf, *buf2, *start;
	size_t len;

	buf = os_readfile(name, &len);
	if (buf == NULL)
		return NULL;
	buf2 = os_realloc(buf, len + 1);
	if (buf2 == NULL) {
		os_free(buf);
		return NULL;
	}
	buf = buf2;
	buf[len] = '\0';

	start = os_strstr(buf, "<!DOCTYPE ");
	if (start) {
		char *pos = start + 1;
		int count = 1;
		while (*pos) {
			if (*pos == '<')
				count++;
			else if (*pos == '>') {
				count--;
				if (count == 0) {
					pos++;
					break;
				}
			}
			pos++;
		}
		if (count == 0) {
			/* Remove DOCTYPE to allow the file to be parsed */
			os_memset(start, ' ', pos - start);
		}
	}

	node = xml_node_from_buf(ctx, buf);
	os_free(buf);

	return node;
}


int node_to_file(struct xml_node_ctx *ctx, const char *fname, xml_node_t *node)
{
	FILE *f;
	char *str;

	str = xml_node_to_str(ctx, node);
	if (str == NULL)
		return -1;

	f = fopen(fname, "w");
	if (!f) {
		os_free(str);
		return -1;
	}

	fprintf(f, "%s\n", str);
	os_free(str);
	fclose(f);

	return 0;
}


static char * get_val(struct xml_node_ctx *ctx, xml_node_t *node)
{
	char *val, *pos;

	val = xml_node_get_text(ctx, node);
	if (val == NULL)
		return NULL;
	pos = val;
	while (*pos) {
		if (*pos != ' ' && *pos != '\t' && *pos != '\r' && *pos != '\n')
			return val;
		pos++;
	}

	return NULL;
}


static char * add_path(const char *prev, const char *leaf)
{
	size_t len;
	char *new_uri;

	if (prev == NULL)
		return NULL;

	len = os_strlen(prev) + 1 + os_strlen(leaf) + 1;
	new_uri = os_malloc(len);
	if (new_uri)
		os_snprintf(new_uri, len, "%s/%s", prev, leaf);

	return new_uri;
}


static void node_to_tnds(struct xml_node_ctx *ctx, xml_node_t *out,
			 xml_node_t *in, const char *uri)
{
	xml_node_t *node;
	xml_node_t *tnds;
	const char *name;
	char *val;
	char *new_uri;

	xml_node_for_each_child(ctx, node, in) {
		xml_node_for_each_check(ctx, node);
		name = xml_node_get_localname(ctx, node);

		tnds = xml_node_create(ctx, out, NULL, "Node");
		if (tnds == NULL)
			return;
		xml_node_create_text(ctx, tnds, NULL, "NodeName", name);

		if (uri)
			xml_node_create_text(ctx, tnds, NULL, "Path", uri);

		val = get_val(ctx, node);
		if (val || !xml_node_first_child(ctx, node))
			xml_node_create_text(ctx, tnds, NULL, "Value",
					     val ? val : "");
		xml_node_get_text_free(ctx, val);

		new_uri = add_path(uri, name);
		node_to_tnds(ctx, new_uri ? out : tnds, node, new_uri);
		os_free(new_uri);
	}
}


static int add_ddfname(struct xml_node_ctx *ctx, xml_node_t *parent,
		       const char *urn)
{
	xml_node_t *node;

	node = xml_node_create(ctx, parent, NULL, "RTProperties");
	if (node == NULL)
		return -1;
	node = xml_node_create(ctx, node, NULL, "Type");
	if (node == NULL)
		return -1;
	xml_node_create_text(ctx, node, NULL, "DDFName", urn);
	return 0;
}


xml_node_t * mo_to_tnds(struct xml_node_ctx *ctx, xml_node_t *mo,
			int use_path, const char *urn, const char *ns_uri)
{
	xml_node_t *root;
	xml_node_t *node;
	const char *name;

	root = xml_node_create_root(ctx, ns_uri, NULL, NULL, "MgmtTree");
	if (root == NULL)
		return NULL;

	xml_node_create_text(ctx, root, NULL, "VerDTD", "1.2");

	name = xml_node_get_localname(ctx, mo);

	node = xml_node_create(ctx, root, NULL, "Node");
	if (node == NULL)
		goto fail;
	xml_node_create_text(ctx, node, NULL, "NodeName", name);
	if (urn)
		add_ddfname(ctx, node, urn);

	node_to_tnds(ctx, use_path ? root : node, mo, use_path ? name : NULL);

	return root;

fail:
	xml_node_free(ctx, root);
	return NULL;
}


static xml_node_t * get_first_child_node(struct xml_node_ctx *ctx,
					 xml_node_t *node,
					 const char *name)
{
	const char *lname;
	xml_node_t *child;

	xml_node_for_each_child(ctx, child, node) {
		xml_node_for_each_check(ctx, child);
		lname = xml_node_get_localname(ctx, child);
		if (os_strcasecmp(lname, name) == 0)
			return child;
	}

	return NULL;
}


static char * get_node_text(struct xml_node_ctx *ctx, xml_node_t *node,
			    const char *node_name)
{
	node = get_first_child_node(ctx, node, node_name);
	if (node == NULL)
		return NULL;
	return xml_node_get_text(ctx, node);
}


static xml_node_t * add_mo_node(struct xml_node_ctx *ctx, xml_node_t *root,
				xml_node_t *node, const char *uri)
{
	char *nodename, *value, *path;
	xml_node_t *parent;

	nodename = get_node_text(ctx, node, "NodeName");
	if (nodename == NULL)
		return NULL;
	value = get_node_text(ctx, node, "Value");

	if (root == NULL) {
		root = xml_node_create_root(ctx, NULL, NULL, NULL,
					    nodename);
		if (root && value)
			xml_node_set_text(ctx, root, value);
	} else {
		if (uri == NULL) {
			xml_node_get_text_free(ctx, nodename);
			xml_node_get_text_free(ctx, value);
			return NULL;
		}
		path = get_node_text(ctx, node, "Path");
		if (path)
			uri = path;
		parent = get_node_uri(ctx, root, uri);
		xml_node_get_text_free(ctx, path);
		if (parent == NULL) {
			printf("Could not find URI '%s'\n", uri);
			xml_node_get_text_free(ctx, nodename);
			xml_node_get_text_free(ctx, value);
			return NULL;
		}
		if (value)
			xml_node_create_text(ctx, parent, NULL, nodename,
					     value);
		else
			xml_node_create(ctx, parent, NULL, nodename);
	}

	xml_node_get_text_free(ctx, nodename);
	xml_node_get_text_free(ctx, value);

	return root;
}


static xml_node_t * tnds_to_mo_iter(struct xml_node_ctx *ctx, xml_node_t *root,
				    xml_node_t *node, const char *uri)
{
	xml_node_t *child;
	const char *name;
	char *nodename;

	xml_node_for_each_sibling(ctx, node) {
		xml_node_for_each_check(ctx, node);

		nodename = get_node_text(ctx, node, "NodeName");
		if (nodename == NULL)
			return NULL;

		name = xml_node_get_localname(ctx, node);
		if (strcmp(name, "Node") == 0) {
			if (root && !uri) {
				printf("Invalid TNDS tree structure - "
				       "multiple top level nodes\n");
				xml_node_get_text_free(ctx, nodename);
				return NULL;
			}
			root = add_mo_node(ctx, root, node, uri);
		}

		child = get_first_child_node(ctx, node, "Node");
		if (child) {
			if (uri == NULL)
				tnds_to_mo_iter(ctx, root, child, nodename);
			else {
				char *new_uri;
				new_uri = add_path(uri, nodename);
				tnds_to_mo_iter(ctx, root, child, new_uri);
				os_free(new_uri);
			}
		}
		xml_node_get_text_free(ctx, nodename);
	}

	return root;
}


xml_node_t * tnds_to_mo(struct xml_node_ctx *ctx, xml_node_t *tnds)
{
	const char *name;
	xml_node_t *node;

	name = xml_node_get_localname(ctx, tnds);
	if (name == NULL || os_strcmp(name, "MgmtTree") != 0)
		return NULL;

	node = get_first_child_node(ctx, tnds, "Node");
	if (!node)
		return NULL;
	return tnds_to_mo_iter(ctx, NULL, node, NULL);
}


xml_node_t * soap_build_envelope(struct xml_node_ctx *ctx, xml_node_t *node)
{
	xml_node_t *envelope, *body;
	xml_namespace_t *ns;

	envelope = xml_node_create_root(
		ctx, "http://www.w3.org/2003/05/soap-envelope", "soap12", &ns,
		"Envelope");
	if (envelope == NULL)
		return NULL;
	body = xml_node_create(ctx, envelope, ns, "Body");
	xml_node_add_child(ctx, body, node);
	return envelope;
}


xml_node_t * soap_get_body(struct xml_node_ctx *ctx, xml_node_t *soap)
{
	xml_node_t *body, *child;

	body = get_node_uri(ctx, soap, "Envelope/Body");
	if (body == NULL)
		return NULL;
	xml_node_for_each_child(ctx, child, body) {
		xml_node_for_each_check(ctx, child);
		return child;
	}
	return NULL;
}
