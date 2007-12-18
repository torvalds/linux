/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"

/*
 * Tree building functions
 */

struct property *build_property(char *name, struct data val, char *label)
{
	struct property *new = xmalloc(sizeof(*new));

	new->name = name;
	new->val = val;

	new->next = NULL;

	new->label = label;

	return new;
}

struct property *chain_property(struct property *first, struct property *list)
{
	assert(first->next == NULL);

	first->next = list;
	return first;
}

struct property *reverse_properties(struct property *first)
{
	struct property *p = first;
	struct property *head = NULL;
	struct property *next;

	while (p) {
		next = p->next;
		p->next = head;
		head = p;
		p = next;
	}
	return head;
}

struct node *build_node(struct property *proplist, struct node *children)
{
	struct node *new = xmalloc(sizeof(*new));
	struct node *child;

	memset(new, 0, sizeof(*new));

	new->proplist = reverse_properties(proplist);
	new->children = children;

	for_each_child(new, child) {
		child->parent = new;
	}

	return new;
}

struct node *name_node(struct node *node, char *name, char * label)
{
	assert(node->name == NULL);

	node->name = name;

	node->label = label;

	return node;
}

struct node *chain_node(struct node *first, struct node *list)
{
	assert(first->next_sibling == NULL);

	first->next_sibling = list;
	return first;
}

void add_property(struct node *node, struct property *prop)
{
	struct property **p;

	prop->next = NULL;

	p = &node->proplist;
	while (*p)
		p = &((*p)->next);

	*p = prop;
}

void add_child(struct node *parent, struct node *child)
{
	struct node **p;

	child->next_sibling = NULL;

	p = &parent->children;
	while (*p)
		p = &((*p)->next_sibling);

	*p = child;
}

struct reserve_info *build_reserve_entry(u64 address, u64 size, char *label)
{
	struct reserve_info *new = xmalloc(sizeof(*new));

	new->re.address = address;
	new->re.size = size;

	new->next = NULL;

	new->label = label;

	return new;
}

struct reserve_info *chain_reserve_entry(struct reserve_info *first,
					struct reserve_info *list)
{
	assert(first->next == NULL);

	first->next = list;
	return first;
}

struct reserve_info *add_reserve_entry(struct reserve_info *list,
				      struct reserve_info *new)
{
	struct reserve_info *last;

	new->next = NULL;

	if (! list)
		return new;

	for (last = list; last->next; last = last->next)
		;

	last->next = new;

	return list;
}

struct boot_info *build_boot_info(struct reserve_info *reservelist,
				  struct node *tree)
{
	struct boot_info *bi;

	bi = xmalloc(sizeof(*bi));
	bi->reservelist = reservelist;
	bi->dt = tree;

	return bi;
}

/*
 * Tree accessor functions
 */

const char *get_unitname(struct node *node)
{
	if (node->name[node->basenamelen] == '\0')
		return "";
	else
		return node->name + node->basenamelen + 1;
}

struct property *get_property(struct node *node, const char *propname)
{
	struct property *prop;

	for_each_property(node, prop)
		if (streq(prop->name, propname))
			return prop;

	return NULL;
}

cell_t propval_cell(struct property *prop)
{
	assert(prop->val.len == sizeof(cell_t));
	return be32_to_cpu(*((cell_t *)prop->val.val));
}

struct node *get_subnode(struct node *node, const char *nodename)
{
	struct node *child;

	for_each_child(node, child)
		if (streq(child->name, nodename))
			return child;

	return NULL;
}

struct node *get_node_by_path(struct node *tree, const char *path)
{
	const char *p;
	struct node *child;

	if (!path || ! (*path))
		return tree;

	while (path[0] == '/')
		path++;

	p = strchr(path, '/');

	for_each_child(tree, child) {
		if (p && strneq(path, child->name, p-path))
			return get_node_by_path(child, p+1);
		else if (!p && streq(path, child->name))
			return child;
	}

	return NULL;
}

struct node *get_node_by_label(struct node *tree, const char *label)
{
	struct node *child, *node;

	assert(label && (strlen(label) > 0));

	if (tree->label && streq(tree->label, label))
		return tree;

	for_each_child(tree, child) {
		node = get_node_by_label(child, label);
		if (node)
			return node;
	}

	return NULL;
}

struct node *get_node_by_phandle(struct node *tree, cell_t phandle)
{
	struct node *child, *node;

	assert((phandle != 0) && (phandle != -1));

	if (tree->phandle == phandle)
		return tree;

	for_each_child(tree, child) {
		node = get_node_by_phandle(child, phandle);
		if (node)
			return node;
	}

	return NULL;
}

struct node *get_node_by_ref(struct node *tree, const char *ref)
{
	if (ref[0] == '/')
		return get_node_by_path(tree, ref);
	else
		return get_node_by_label(tree, ref);
}

cell_t get_node_phandle(struct node *root, struct node *node)
{
	static cell_t phandle = 1; /* FIXME: ick, static local */

	if ((node->phandle != 0) && (node->phandle != -1))
		return node->phandle;

	assert(! get_property(node, "linux,phandle"));

	while (get_node_by_phandle(root, phandle))
		phandle++;

	node->phandle = phandle;
	add_property(node,
		     build_property("linux,phandle",
				    data_append_cell(empty_data, phandle),
				    NULL));

	return node->phandle;
}
