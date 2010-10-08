/*
 * tree.c: Basic device tree traversal/scanning for the Linux
 *         prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/ldc.h>

static phandle prom_node_to_node(const char *type, phandle node)
{
	unsigned long args[5];

	args[0] = (unsigned long) type;
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (phandle) args[4];
}

/* Return the child of node 'node' or zero if no this node has no
 * direct descendent.
 */
inline phandle __prom_getchild(phandle node)
{
	return prom_node_to_node("child", node);
}

inline phandle prom_getchild(phandle node)
{
	phandle cnode;

	if (node == -1)
		return 0;
	cnode = __prom_getchild(node);
	if (cnode == -1)
		return 0;
	return cnode;
}
EXPORT_SYMBOL(prom_getchild);

inline phandle prom_getparent(phandle node)
{
	phandle cnode;

	if (node == -1)
		return 0;
	cnode = prom_node_to_node("parent", node);
	if (cnode == -1)
		return 0;
	return cnode;
}

/* Return the next sibling of node 'node' or zero if no more siblings
 * at this level of depth in the tree.
 */
inline phandle __prom_getsibling(phandle node)
{
	return prom_node_to_node(prom_peer_name, node);
}

inline phandle prom_getsibling(phandle node)
{
	phandle sibnode;

	if (node == -1)
		return 0;
	sibnode = __prom_getsibling(node);
	if (sibnode == -1)
		return 0;

	return sibnode;
}
EXPORT_SYMBOL(prom_getsibling);

/* Return the length in bytes of property 'prop' at node 'node'.
 * Return -1 on error.
 */
inline int prom_getproplen(phandle node, const char *prop)
{
	unsigned long args[6];

	if (!node || !prop)
		return -1;

	args[0] = (unsigned long) "getproplen";
	args[1] = 2;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = (unsigned long) prop;
	args[5] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[5];
}
EXPORT_SYMBOL(prom_getproplen);

/* Acquire a property 'prop' at node 'node' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
inline int prom_getproperty(phandle node, const char *prop,
			    char *buffer, int bufsize)
{
	unsigned long args[8];
	int plen;

	plen = prom_getproplen(node, prop);
	if ((plen > bufsize) || (plen == 0) || (plen == -1))
		return -1;

	args[0] = (unsigned long) prom_getprop_name;
	args[1] = 4;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = (unsigned long) prop;
	args[5] = (unsigned long) buffer;
	args[6] = bufsize;
	args[7] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[7];
}
EXPORT_SYMBOL(prom_getproperty);

/* Acquire an integer property and return its value.  Returns -1
 * on failure.
 */
inline int prom_getint(phandle node, const char *prop)
{
	int intprop;

	if (prom_getproperty(node, prop, (char *) &intprop, sizeof(int)) != -1)
		return intprop;

	return -1;
}
EXPORT_SYMBOL(prom_getint);

/* Acquire an integer property, upon error return the passed default
 * integer.
 */

int prom_getintdefault(phandle node, const char *property, int deflt)
{
	int retval;

	retval = prom_getint(node, property);
	if (retval == -1)
		return deflt;

	return retval;
}
EXPORT_SYMBOL(prom_getintdefault);

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int prom_getbool(phandle node, const char *prop)
{
	int retval;

	retval = prom_getproplen(node, prop);
	if (retval == -1)
		return 0;
	return 1;
}
EXPORT_SYMBOL(prom_getbool);

/* Acquire a property whose value is a string, returns a null
 * string on error.  The char pointer is the user supplied string
 * buffer.
 */
void prom_getstring(phandle node, const char *prop, char *user_buf,
		int ubuf_size)
{
	int len;

	len = prom_getproperty(node, prop, user_buf, ubuf_size);
	if (len != -1)
		return;
	user_buf[0] = 0;
}
EXPORT_SYMBOL(prom_getstring);

/* Does the device at node 'node' have name 'name'?
 * YES = 1   NO = 0
 */
int prom_nodematch(phandle node, const char *name)
{
	char namebuf[128];
	prom_getproperty(node, "name", namebuf, sizeof(namebuf));
	if (strcmp(namebuf, name) == 0)
		return 1;
	return 0;
}

/* Search siblings at 'node_start' for a node with name
 * 'nodename'.  Return node if successful, zero if not.
 */
phandle prom_searchsiblings(phandle node_start, const char *nodename)
{
	phandle thisnode;
	int error;
	char promlib_buf[128];

	for(thisnode = node_start; thisnode;
	    thisnode=prom_getsibling(thisnode)) {
		error = prom_getproperty(thisnode, "name", promlib_buf,
					 sizeof(promlib_buf));
		/* Should this ever happen? */
		if(error == -1) continue;
		if(strcmp(nodename, promlib_buf)==0) return thisnode;
	}

	return 0;
}
EXPORT_SYMBOL(prom_searchsiblings);

static const char *prom_nextprop_name = "nextprop";

/* Return the first property type for node 'node'.
 * buffer should be at least 32B in length
 */
inline char *prom_firstprop(phandle node, char *buffer)
{
	unsigned long args[7];

	*buffer = 0;
	if (node == -1)
		return buffer;

	args[0] = (unsigned long) prom_nextprop_name;
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = 0;
	args[5] = (unsigned long) buffer;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return buffer;
}
EXPORT_SYMBOL(prom_firstprop);

/* Return the property type string after property type 'oprop'
 * at node 'node' .  Returns NULL string if no more
 * property types for this node.
 */
inline char *prom_nextprop(phandle node, const char *oprop, char *buffer)
{
	unsigned long args[7];
	char buf[32];

	if (node == -1) {
		*buffer = 0;
		return buffer;
	}
	if (oprop == buffer) {
		strcpy (buf, oprop);
		oprop = buf;
	}

	args[0] = (unsigned long) prom_nextprop_name;
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = (unsigned long) oprop;
	args[5] = (unsigned long) buffer;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return buffer;
}
EXPORT_SYMBOL(prom_nextprop);

phandle prom_finddevice(const char *name)
{
	unsigned long args[5];

	if (!name)
		return 0;
	args[0] = (unsigned long) "finddevice";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned long) name;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[4];
}
EXPORT_SYMBOL(prom_finddevice);

int prom_node_has_property(phandle node, const char *prop)
{
	char buf [32];
        
	*buf = 0;
	do {
		prom_nextprop(node, buf, buf);
		if (!strcmp(buf, prop))
			return 1;
	} while (*buf);
	return 0;
}
EXPORT_SYMBOL(prom_node_has_property);

/* Set property 'pname' at node 'node' to value 'value' which has a length
 * of 'size' bytes.  Return the number of bytes the prom accepted.
 */
int
prom_setprop(phandle node, const char *pname, char *value, int size)
{
	unsigned long args[8];

	if (size == 0)
		return 0;
	if ((pname == 0) || (value == 0))
		return 0;
	
#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled) {
		ldom_set_var(pname, value);
		return 0;
	}
#endif
	args[0] = (unsigned long) "setprop";
	args[1] = 4;
	args[2] = 1;
	args[3] = (unsigned int) node;
	args[4] = (unsigned long) pname;
	args[5] = (unsigned long) value;
	args[6] = size;
	args[7] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[7];
}
EXPORT_SYMBOL(prom_setprop);

inline phandle prom_inst2pkg(int inst)
{
	unsigned long args[5];
	phandle node;
	
	args[0] = (unsigned long) "instance-to-package";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) inst;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	node = (int) args[4];
	if (node == -1)
		return 0;
	return node;
}

/* Return 'node' assigned to a particular prom 'path'
 * FIXME: Should work for v0 as well
 */
phandle prom_pathtoinode(const char *path)
{
	phandle node;
	int inst;

	inst = prom_devopen (path);
	if (inst == 0)
		return 0;
	node = prom_inst2pkg(inst);
	prom_devclose(inst);
	if (node == -1)
		return 0;
	return node;
}

int prom_ihandle2path(int handle, char *buffer, int bufsize)
{
	unsigned long args[7];

	args[0] = (unsigned long) "instance-to-path";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) handle;
	args[4] = (unsigned long) buffer;
	args[5] = bufsize;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[6];
}
