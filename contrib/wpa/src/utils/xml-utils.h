/*
 * Generic XML helper functions
 * Copyright (c) 2012-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef XML_UTILS_H
#define XML_UTILS_H

struct xml_node_ctx;
typedef struct xml_node xml_node_t;
typedef struct xml_namespace_foo xml_namespace_t;

/* XML library wrappers */

int xml_validate(struct xml_node_ctx *ctx, xml_node_t *node,
		 const char *xml_schema_fname, char **ret_err);
int xml_validate_dtd(struct xml_node_ctx *ctx, xml_node_t *node,
		     const char *dtd_fname, char **ret_err);
void xml_node_free(struct xml_node_ctx *ctx, xml_node_t *node);
xml_node_t * xml_node_get_parent(struct xml_node_ctx *ctx, xml_node_t *node);
xml_node_t * xml_node_from_buf(struct xml_node_ctx *ctx, const char *buf);
const char * xml_node_get_localname(struct xml_node_ctx *ctx,
				    xml_node_t *node);
char * xml_node_to_str(struct xml_node_ctx *ctx, xml_node_t *node);
void xml_node_detach(struct xml_node_ctx *ctx, xml_node_t *node);
void xml_node_add_child(struct xml_node_ctx *ctx, xml_node_t *parent,
			xml_node_t *child);
xml_node_t * xml_node_create_root(struct xml_node_ctx *ctx, const char *ns_uri,
				  const char *ns_prefix,
				  xml_namespace_t **ret_ns, const char *name);
xml_node_t * xml_node_create(struct xml_node_ctx *ctx, xml_node_t *parent,
			     xml_namespace_t *ns, const char *name);
xml_node_t * xml_node_create_text(struct xml_node_ctx *ctx,
				  xml_node_t *parent, xml_namespace_t *ns,
				  const char *name, const char *value);
xml_node_t * xml_node_create_text_ns(struct xml_node_ctx *ctx,
				     xml_node_t *parent, const char *ns_uri,
				     const char *name, const char *value);
void xml_node_set_text(struct xml_node_ctx *ctx, xml_node_t *node,
		       const char *value);
int xml_node_add_attr(struct xml_node_ctx *ctx, xml_node_t *node,
		      xml_namespace_t *ns, const char *name, const char *value);
char * xml_node_get_attr_value(struct xml_node_ctx *ctx, xml_node_t *node,
			       char *name);
char * xml_node_get_attr_value_ns(struct xml_node_ctx *ctx, xml_node_t *node,
				  const char *ns_uri, char *name);
void xml_node_get_attr_value_free(struct xml_node_ctx *ctx, char *val);
xml_node_t * xml_node_first_child(struct xml_node_ctx *ctx,
				  xml_node_t *parent);
xml_node_t * xml_node_next_sibling(struct xml_node_ctx *ctx,
				   xml_node_t *node);
int xml_node_is_element(struct xml_node_ctx *ctx, xml_node_t *node);
char * xml_node_get_text(struct xml_node_ctx *ctx, xml_node_t *node);
void xml_node_get_text_free(struct xml_node_ctx *ctx, char *val);
char * xml_node_get_base64_text(struct xml_node_ctx *ctx, xml_node_t *node,
				int *ret_len);
xml_node_t * xml_node_copy(struct xml_node_ctx *ctx, xml_node_t *node);

#define xml_node_for_each_child(ctx, child, parent) \
for (child = xml_node_first_child(ctx, parent); \
     child; \
     child = xml_node_next_sibling(ctx, child))

#define xml_node_for_each_sibling(ctx, node) \
for (; \
     node; \
     node = xml_node_next_sibling(ctx, node))

#define xml_node_for_each_check(ctx, child) \
if (!xml_node_is_element(ctx, child)) \
	continue


struct xml_node_ctx * xml_node_init_ctx(void *upper_ctx,
					const void *env);
void xml_node_deinit_ctx(struct xml_node_ctx *ctx);


xml_node_t * get_node_uri(struct xml_node_ctx *ctx, xml_node_t *root,
			  const char *uri);
xml_node_t * get_node(struct xml_node_ctx *ctx, xml_node_t *root,
		      const char *path);
xml_node_t * get_child_node(struct xml_node_ctx *ctx, xml_node_t *root,
			    const char *path);
xml_node_t * node_from_file(struct xml_node_ctx *ctx, const char *name);
int node_to_file(struct xml_node_ctx *ctx, const char *fname, xml_node_t *node);
xml_node_t * mo_to_tnds(struct xml_node_ctx *ctx, xml_node_t *mo,
			int use_path, const char *urn, const char *ns_uri);
xml_node_t * tnds_to_mo(struct xml_node_ctx *ctx, xml_node_t *tnds);

xml_node_t * soap_build_envelope(struct xml_node_ctx *ctx, xml_node_t *node);
xml_node_t * soap_get_body(struct xml_node_ctx *ctx, xml_node_t *soap);

#endif /* XML_UTILS_H */
