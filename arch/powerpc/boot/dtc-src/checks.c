/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2007.
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

#ifdef TRACE_CHECKS
#define TRACE(c, ...) \
	do { \
		fprintf(stderr, "=== %s: ", (c)->name); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} while (0)
#else
#define TRACE(c, fmt, ...)	do { } while (0)
#endif

enum checklevel {
	IGNORE = 0,
	WARN = 1,
	ERROR = 2,
};

enum checkstatus {
	UNCHECKED = 0,
	PREREQ,
	PASSED,
	FAILED,
};

struct check;

typedef void (*tree_check_fn)(struct check *c, struct node *dt);
typedef void (*node_check_fn)(struct check *c, struct node *dt, struct node *node);
typedef void (*prop_check_fn)(struct check *c, struct node *dt,
			      struct node *node, struct property *prop);

struct check {
	const char *name;
	tree_check_fn tree_fn;
	node_check_fn node_fn;
	prop_check_fn prop_fn;
	void *data;
	enum checklevel level;
	enum checkstatus status;
	int inprogress;
	int num_prereqs;
	struct check **prereq;
};

#define CHECK(nm, tfn, nfn, pfn, d, lvl, ...) \
	static struct check *nm##_prereqs[] = { __VA_ARGS__ }; \
	static struct check nm = { \
		.name = #nm, \
		.tree_fn = (tfn), \
		.node_fn = (nfn), \
		.prop_fn = (pfn), \
		.data = (d), \
		.level = (lvl), \
		.status = UNCHECKED, \
		.num_prereqs = ARRAY_SIZE(nm##_prereqs), \
		.prereq = nm##_prereqs, \
	};

#define TREE_CHECK(nm, d, lvl, ...) \
	CHECK(nm, check_##nm, NULL, NULL, d, lvl, __VA_ARGS__)
#define NODE_CHECK(nm, d, lvl, ...) \
	CHECK(nm, NULL, check_##nm, NULL, d, lvl, __VA_ARGS__)
#define PROP_CHECK(nm, d, lvl, ...) \
	CHECK(nm, NULL, NULL, check_##nm, d, lvl, __VA_ARGS__)
#define BATCH_CHECK(nm, lvl, ...) \
	CHECK(nm, NULL, NULL, NULL, NULL, lvl, __VA_ARGS__)

#ifdef __GNUC__
static inline void check_msg(struct check *c, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
#endif
static inline void check_msg(struct check *c, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	if ((c->level < WARN) || (c->level <= quiet))
		return; /* Suppress message */

	fprintf(stderr, "%s (%s): ",
		(c->level == ERROR) ? "ERROR" : "Warning", c->name);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

#define FAIL(c, ...) \
	do { \
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__); \
		(c)->status = FAILED; \
		check_msg((c), __VA_ARGS__); \
	} while (0)

static void check_nodes_props(struct check *c, struct node *dt, struct node *node)
{
	struct node *child;
	struct property *prop;

	TRACE(c, "%s", node->fullpath);
	if (c->node_fn)
		c->node_fn(c, dt, node);

	if (c->prop_fn)
		for_each_property(node, prop) {
			TRACE(c, "%s\t'%s'", node->fullpath, prop->name);
			c->prop_fn(c, dt, node, prop);
		}

	for_each_child(node, child)
		check_nodes_props(c, dt, child);
}

static int run_check(struct check *c, struct node *dt)
{
	int error = 0;
	int i;

	assert(!c->inprogress);

	if (c->status != UNCHECKED)
		goto out;

	c->inprogress = 1;

	for (i = 0; i < c->num_prereqs; i++) {
		struct check *prq = c->prereq[i];
		error |= run_check(prq, dt);
		if (prq->status != PASSED) {
			c->status = PREREQ;
			check_msg(c, "Failed prerequisite '%s'",
				  c->prereq[i]->name);
		}
	}

	if (c->status != UNCHECKED)
		goto out;

	if (c->node_fn || c->prop_fn)
		check_nodes_props(c, dt, dt);

	if (c->tree_fn)
		c->tree_fn(c, dt);
	if (c->status == UNCHECKED)
		c->status = PASSED;

	TRACE(c, "\tCompleted, status %d", c->status);

out:
	c->inprogress = 0;
	if ((c->status != PASSED) && (c->level == ERROR))
		error = 1;
	return error;
}

/*
 * Utility check functions
 */

static void check_is_string(struct check *c, struct node *root,
			    struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (!data_is_one_string(prop->val))
		FAIL(c, "\"%s\" property in %s is not a string",
		     propname, node->fullpath);
}
#define CHECK_IS_STRING(nm, propname, lvl) \
	CHECK(nm, NULL, check_is_string, NULL, (propname), (lvl))

static void check_is_cell(struct check *c, struct node *root,
			  struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (prop->val.len != sizeof(cell_t))
		FAIL(c, "\"%s\" property in %s is not a single cell",
		     propname, node->fullpath);
}
#define CHECK_IS_CELL(nm, propname, lvl) \
	CHECK(nm, NULL, check_is_cell, NULL, (propname), (lvl))

/*
 * Structural check functions
 */

static void check_duplicate_node_names(struct check *c, struct node *dt,
				       struct node *node)
{
	struct node *child, *child2;

	for_each_child(node, child)
		for (child2 = child->next_sibling;
		     child2;
		     child2 = child2->next_sibling)
			if (streq(child->name, child2->name))
				FAIL(c, "Duplicate node name %s",
				     child->fullpath);
}
NODE_CHECK(duplicate_node_names, NULL, ERROR);

static void check_duplicate_property_names(struct check *c, struct node *dt,
					   struct node *node)
{
	struct property *prop, *prop2;

	for_each_property(node, prop)
		for (prop2 = prop->next; prop2; prop2 = prop2->next)
			if (streq(prop->name, prop2->name))
				FAIL(c, "Duplicate property name %s in %s",
				     prop->name, node->fullpath);
}
NODE_CHECK(duplicate_property_names, NULL, ERROR);

static void check_explicit_phandles(struct check *c, struct node *root,
					  struct node *node)
{
	struct property *prop;
	struct node *other;
	cell_t phandle;

	prop = get_property(node, "linux,phandle");
	if (! prop)
		return; /* No phandle, that's fine */

	if (prop->val.len != sizeof(cell_t)) {
		FAIL(c, "%s has bad length (%d) linux,phandle property",
		     node->fullpath, prop->val.len);
		return;
	}

	phandle = propval_cell(prop);
	if ((phandle == 0) || (phandle == -1)) {
		FAIL(c, "%s has invalid linux,phandle value 0x%x",
		     node->fullpath, phandle);
		return;
	}

	other = get_node_by_phandle(root, phandle);
	if (other) {
		FAIL(c, "%s has duplicated phandle 0x%x (seen before at %s)",
		     node->fullpath, phandle, other->fullpath);
		return;
	}

	node->phandle = phandle;
}
NODE_CHECK(explicit_phandles, NULL, ERROR);

static void check_name_properties(struct check *c, struct node *root,
				  struct node *node)
{
	struct property *prop;

	prop = get_property(node, "name");
	if (!prop)
		return; /* No name property, that's fine */

	if ((prop->val.len != node->basenamelen+1)
	    || (memcmp(prop->val.val, node->name, node->basenamelen) != 0))
		FAIL(c, "\"name\" property in %s is incorrect (\"%s\" instead"
		     " of base node name)", node->fullpath, prop->val.val);
}
CHECK_IS_STRING(name_is_string, "name", ERROR);
NODE_CHECK(name_properties, NULL, ERROR, &name_is_string);

/*
 * Reference fixup functions
 */

static void fixup_phandle_references(struct check *c, struct node *dt,
				     struct node *node, struct property *prop)
{
      struct marker *m = prop->val.markers;
      struct node *refnode;
      cell_t phandle;

      for_each_marker_of_type(m, REF_PHANDLE) {
	      assert(m->offset + sizeof(cell_t) <= prop->val.len);

	      refnode = get_node_by_ref(dt, m->ref);
	      if (! refnode) {
		      FAIL(c, "Reference to non-existent node or label \"%s\"\n",
			   m->ref);
		      continue;
	      }

	      phandle = get_node_phandle(dt, refnode);
	      *((cell_t *)(prop->val.val + m->offset)) = cpu_to_be32(phandle);
      }
}
CHECK(phandle_references, NULL, NULL, fixup_phandle_references, NULL, ERROR,
      &duplicate_node_names, &explicit_phandles);

static void fixup_path_references(struct check *c, struct node *dt,
				  struct node *node, struct property *prop)
{
	struct marker *m = prop->val.markers;
	struct node *refnode;
	char *path;

	for_each_marker_of_type(m, REF_PATH) {
		assert(m->offset <= prop->val.len);

		refnode = get_node_by_ref(dt, m->ref);
		if (!refnode) {
			FAIL(c, "Reference to non-existent node or label \"%s\"\n",
			     m->ref);
			continue;
		}

		path = refnode->fullpath;
		prop->val = data_insert_at_marker(prop->val, m, path,
						  strlen(path) + 1);
	}
}
CHECK(path_references, NULL, NULL, fixup_path_references, NULL, ERROR,
      &duplicate_node_names);

/*
 * Semantic checks
 */
CHECK_IS_CELL(address_cells_is_cell, "#address-cells", WARN);
CHECK_IS_CELL(size_cells_is_cell, "#size-cells", WARN);
CHECK_IS_CELL(interrupt_cells_is_cell, "#interrupt-cells", WARN);

CHECK_IS_STRING(device_type_is_string, "device_type", WARN);
CHECK_IS_STRING(model_is_string, "model", WARN);
CHECK_IS_STRING(status_is_string, "status", WARN);

static void fixup_addr_size_cells(struct check *c, struct node *dt,
				  struct node *node)
{
	struct property *prop;

	node->addr_cells = -1;
	node->size_cells = -1;

	prop = get_property(node, "#address-cells");
	if (prop)
		node->addr_cells = propval_cell(prop);

	prop = get_property(node, "#size-cells");
	if (prop)
		node->size_cells = propval_cell(prop);
}
CHECK(addr_size_cells, NULL, fixup_addr_size_cells, NULL, NULL, WARN,
      &address_cells_is_cell, &size_cells_is_cell);

#define node_addr_cells(n) \
	(((n)->addr_cells == -1) ? 2 : (n)->addr_cells)
#define node_size_cells(n) \
	(((n)->size_cells == -1) ? 1 : (n)->size_cells)

static void check_reg_format(struct check *c, struct node *dt,
			     struct node *node)
{
	struct property *prop;
	int addr_cells, size_cells, entrylen;

	prop = get_property(node, "reg");
	if (!prop)
		return; /* No "reg", that's fine */

	if (!node->parent) {
		FAIL(c, "Root node has a \"reg\" property");
		return;
	}

	if (prop->val.len == 0)
		FAIL(c, "\"reg\" property in %s is empty", node->fullpath);

	addr_cells = node_addr_cells(node->parent);
	size_cells = node_size_cells(node->parent);
	entrylen = (addr_cells + size_cells) * sizeof(cell_t);

	if ((prop->val.len % entrylen) != 0)
		FAIL(c, "\"reg\" property in %s has invalid length (%d bytes) "
		     "(#address-cells == %d, #size-cells == %d)",
		     node->fullpath, prop->val.len, addr_cells, size_cells);
}
NODE_CHECK(reg_format, NULL, WARN, &addr_size_cells);

static void check_ranges_format(struct check *c, struct node *dt,
				struct node *node)
{
	struct property *prop;
	int c_addr_cells, p_addr_cells, c_size_cells, p_size_cells, entrylen;

	prop = get_property(node, "ranges");
	if (!prop)
		return;

	if (!node->parent) {
		FAIL(c, "Root node has a \"ranges\" property");
		return;
	}

	p_addr_cells = node_addr_cells(node->parent);
	p_size_cells = node_size_cells(node->parent);
	c_addr_cells = node_addr_cells(node);
	c_size_cells = node_size_cells(node);
	entrylen = (p_addr_cells + c_addr_cells + c_size_cells) * sizeof(cell_t);

	if (prop->val.len == 0) {
		if (p_addr_cells != c_addr_cells)
			FAIL(c, "%s has empty \"ranges\" property but its "
			     "#address-cells (%d) differs from %s (%d)",
			     node->fullpath, c_addr_cells, node->parent->fullpath,
			     p_addr_cells);
		if (p_size_cells != c_size_cells)
			FAIL(c, "%s has empty \"ranges\" property but its "
			     "#size-cells (%d) differs from %s (%d)",
			     node->fullpath, c_size_cells, node->parent->fullpath,
			     p_size_cells);
	} else if ((prop->val.len % entrylen) != 0) {
		FAIL(c, "\"ranges\" property in %s has invalid length (%d bytes) "
		     "(parent #address-cells == %d, child #address-cells == %d, "
		     "#size-cells == %d)", node->fullpath, prop->val.len,
		     p_addr_cells, c_addr_cells, c_size_cells);
	}
}
NODE_CHECK(ranges_format, NULL, WARN, &addr_size_cells);

/*
 * Style checks
 */
static void check_avoid_default_addr_size(struct check *c, struct node *dt,
					  struct node *node)
{
	struct property *reg, *ranges;

	if (!node->parent)
		return; /* Ignore root node */

	reg = get_property(node, "reg");
	ranges = get_property(node, "ranges");

	if (!reg && !ranges)
		return;

	if ((node->parent->addr_cells == -1))
		FAIL(c, "Relying on default #address-cells value for %s",
		     node->fullpath);

	if ((node->parent->size_cells == -1))
		FAIL(c, "Relying on default #size-cells value for %s",
		     node->fullpath);
}
NODE_CHECK(avoid_default_addr_size, NULL, WARN, &addr_size_cells);

static void check_obsolete_chosen_interrupt_controller(struct check *c,
						       struct node *dt)
{
	struct node *chosen;
	struct property *prop;

	chosen = get_node_by_path(dt, "/chosen");
	if (!chosen)
		return;

	prop = get_property(chosen, "interrupt-controller");
	if (prop)
		FAIL(c, "/chosen has obsolete \"interrupt-controller\" "
		     "property");
}
TREE_CHECK(obsolete_chosen_interrupt_controller, NULL, WARN);

static struct check *check_table[] = {
	&duplicate_node_names, &duplicate_property_names,
	&name_is_string, &name_properties,
	&explicit_phandles,
	&phandle_references, &path_references,

	&address_cells_is_cell, &size_cells_is_cell, &interrupt_cells_is_cell,
	&device_type_is_string, &model_is_string, &status_is_string,

	&addr_size_cells, &reg_format, &ranges_format,

	&avoid_default_addr_size,
	&obsolete_chosen_interrupt_controller,
};

int check_semantics(struct node *dt, int outversion, int boot_cpuid_phys);

void process_checks(int force, struct boot_info *bi,
		    int checkflag, int outversion, int boot_cpuid_phys)
{
	struct node *dt = bi->dt;
	int i;
	int error = 0;

	for (i = 0; i < ARRAY_SIZE(check_table); i++) {
		struct check *c = check_table[i];

		if (c->level != IGNORE)
			error = error || run_check(c, dt);
	}

	if (error) {
		if (!force) {
			fprintf(stderr, "ERROR: Input tree has errors, aborting "
				"(use -f to force output)\n");
			exit(2);
		} else if (quiet < 3) {
			fprintf(stderr, "Warning: Input tree has errors, "
				"output forced\n");
		}
	}

	if (checkflag) {
		if (error) {
			fprintf(stderr, "Warning: Skipping semantic checks due to structural errors\n");
		} else {
			if (!check_semantics(bi->dt, outversion,
					     boot_cpuid_phys))
				fprintf(stderr, "Warning: Input tree has semantic errors\n");
		}
	}
}

/*
 * Semantic check functions
 */

#define ERRMSG(...) if (quiet < 2) fprintf(stderr, "ERROR: " __VA_ARGS__)
#define WARNMSG(...) if (quiet < 1) fprintf(stderr, "Warning: " __VA_ARGS__)

#define DO_ERR(...) do {ERRMSG(__VA_ARGS__); ok = 0; } while (0)

#define CHECK_HAVE(node, propname) \
	do { \
		if (! (prop = get_property((node), (propname)))) \
			DO_ERR("Missing \"%s\" property in %s\n", (propname), \
				(node)->fullpath); \
	} while (0);

#define CHECK_HAVE_WARN(node, propname) \
	do { \
		if (! (prop  = get_property((node), (propname)))) \
			WARNMSG("%s has no \"%s\" property\n", \
				(node)->fullpath, (propname)); \
	} while (0)

#define CHECK_HAVE_STRING(node, propname) \
	do { \
		CHECK_HAVE((node), (propname)); \
		if (prop && !data_is_one_string(prop->val)) \
			DO_ERR("\"%s\" property in %s is not a string\n", \
				(propname), (node)->fullpath); \
	} while (0)

#define CHECK_HAVE_STREQ(node, propname, value) \
	do { \
		CHECK_HAVE_STRING((node), (propname)); \
		if (prop && !streq(prop->val.val, (value))) \
			DO_ERR("%s has wrong %s, %s (should be %s\n", \
				(node)->fullpath, (propname), \
				prop->val.val, (value)); \
	} while (0)

#define CHECK_HAVE_ONECELL(node, propname) \
	do { \
		CHECK_HAVE((node), (propname)); \
		if (prop && (prop->val.len != sizeof(cell_t))) \
			DO_ERR("\"%s\" property in %s has wrong size %d (should be 1 cell)\n", (propname), (node)->fullpath, prop->val.len); \
	} while (0)

#define CHECK_HAVE_WARN_ONECELL(node, propname) \
	do { \
		CHECK_HAVE_WARN((node), (propname)); \
		if (prop && (prop->val.len != sizeof(cell_t))) \
			DO_ERR("\"%s\" property in %s has wrong size %d (should be 1 cell)\n", (propname), (node)->fullpath, prop->val.len); \
	} while (0)

#define CHECK_HAVE_WARN_PHANDLE(xnode, propname, root) \
	do { \
		struct node *ref; \
		CHECK_HAVE_WARN_ONECELL((xnode), (propname)); \
		if (prop) {\
			cell_t phandle = propval_cell(prop); \
			if ((phandle == 0) || (phandle == -1)) { \
				DO_ERR("\"%s\" property in %s contains an invalid phandle %x\n", (propname), (xnode)->fullpath, phandle); \
			} else { \
				ref = get_node_by_phandle((root), propval_cell(prop)); \
				if (! ref) \
					DO_ERR("\"%s\" property in %s refers to non-existant phandle %x\n", (propname), (xnode)->fullpath, propval_cell(prop)); \
			} \
		} \
	} while (0)

#define CHECK_HAVE_WARN_STRING(node, propname) \
	do { \
		CHECK_HAVE_WARN((node), (propname)); \
		if (prop && !data_is_one_string(prop->val)) \
			DO_ERR("\"%s\" property in %s is not a string\n", \
				(propname), (node)->fullpath); \
	} while (0)

static int check_root(struct node *root)
{
	struct property *prop;
	int ok = 1;

	CHECK_HAVE_STRING(root, "model");
	CHECK_HAVE_WARN(root, "compatible");

	return ok;
}

static int check_cpus(struct node *root, int outversion, int boot_cpuid_phys)
{
	struct node *cpus, *cpu;
	struct property *prop;
	struct node *bootcpu = NULL;
	int ok = 1;

	cpus = get_subnode(root, "cpus");
	if (! cpus) {
		ERRMSG("Missing /cpus node\n");
		return 0;
	}

	if (cpus->addr_cells != 1)
		DO_ERR("%s has bad #address-cells value %d (should be 1)\n",
		       cpus->fullpath, cpus->addr_cells);
	if (cpus->size_cells != 0)
		DO_ERR("%s has bad #size-cells value %d (should be 0)\n",
		       cpus->fullpath, cpus->size_cells);

	for_each_child(cpus, cpu) {
		CHECK_HAVE_STREQ(cpu, "device_type", "cpu");

		CHECK_HAVE_ONECELL(cpu, "reg");
		if (prop) {
			cell_t unitnum;
			char *eptr;

			unitnum = strtol(get_unitname(cpu), &eptr, 16);
			if (*eptr) {
				WARNMSG("%s has bad format unit name %s (should be CPU number\n",
					cpu->fullpath, get_unitname(cpu));
			} else if (unitnum != propval_cell(prop)) {
				WARNMSG("%s unit name \"%s\" does not match \"reg\" property <%x>\n",
				       cpu->fullpath, get_unitname(cpu),
				       propval_cell(prop));
			}
		}

/* 		CHECK_HAVE_ONECELL(cpu, "d-cache-line-size"); */
/* 		CHECK_HAVE_ONECELL(cpu, "i-cache-line-size"); */
		CHECK_HAVE_ONECELL(cpu, "d-cache-size");
		CHECK_HAVE_ONECELL(cpu, "i-cache-size");

		CHECK_HAVE_WARN_ONECELL(cpu, "clock-frequency");
		CHECK_HAVE_WARN_ONECELL(cpu, "timebase-frequency");

		prop = get_property(cpu, "linux,boot-cpu");
		if (prop) {
			if (prop->val.len)
				WARNMSG("\"linux,boot-cpu\" property in %s is non-empty\n",
					cpu->fullpath);
			if (bootcpu)
				DO_ERR("Multiple boot cpus (%s and %s)\n",
				       bootcpu->fullpath, cpu->fullpath);
			else
				bootcpu = cpu;
		}
	}

	if (outversion < 2) {
		if (! bootcpu)
			WARNMSG("No cpu has \"linux,boot-cpu\" property\n");
	} else {
		if (bootcpu)
			WARNMSG("\"linux,boot-cpu\" property is deprecated in blob version 2 or higher\n");
		if (boot_cpuid_phys == 0xfeedbeef)
			WARNMSG("physical boot CPU not set.  Use -b option to set\n");
	}

	return ok;
}

static int check_memory(struct node *root)
{
	struct node *mem;
	struct property *prop;
	int nnodes = 0;
	int ok = 1;

	for_each_child(root, mem) {
		if (! strneq(mem->name, "memory", mem->basenamelen))
			continue;

		nnodes++;

		CHECK_HAVE_STREQ(mem, "device_type", "memory");
		CHECK_HAVE(mem, "reg");
	}

	if (nnodes == 0) {
		ERRMSG("No memory nodes\n");
		return 0;
	}

	return ok;
}

int check_semantics(struct node *dt, int outversion, int boot_cpuid_phys)
{
	int ok = 1;

	ok = ok && check_root(dt);
	ok = ok && check_cpus(dt, outversion, boot_cpuid_phys);
	ok = ok && check_memory(dt);
	if (! ok)
		return 0;

	return 1;
}
