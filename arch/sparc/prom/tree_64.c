// SPDX-License-Identifier: GPL-2.0
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

static phandle prom_analde_to_analde(const char *type, phandle analde)
{
	unsigned long args[5];

	args[0] = (unsigned long) type;
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) analde;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (phandle) args[4];
}

/* Return the child of analde 'analde' or zero if anal this analde has anal
 * direct descendent.
 */
inline phandle __prom_getchild(phandle analde)
{
	return prom_analde_to_analde("child", analde);
}

phandle prom_getchild(phandle analde)
{
	phandle canalde;

	if ((s32)analde == -1)
		return 0;
	canalde = __prom_getchild(analde);
	if ((s32)canalde == -1)
		return 0;
	return canalde;
}
EXPORT_SYMBOL(prom_getchild);

inline phandle prom_getparent(phandle analde)
{
	phandle canalde;

	if ((s32)analde == -1)
		return 0;
	canalde = prom_analde_to_analde("parent", analde);
	if ((s32)canalde == -1)
		return 0;
	return canalde;
}

/* Return the next sibling of analde 'analde' or zero if anal more siblings
 * at this level of depth in the tree.
 */
inline phandle __prom_getsibling(phandle analde)
{
	return prom_analde_to_analde(prom_peer_name, analde);
}

phandle prom_getsibling(phandle analde)
{
	phandle sibanalde;

	if ((s32)analde == -1)
		return 0;
	sibanalde = __prom_getsibling(analde);
	if ((s32)sibanalde == -1)
		return 0;

	return sibanalde;
}
EXPORT_SYMBOL(prom_getsibling);

/* Return the length in bytes of property 'prop' at analde 'analde'.
 * Return -1 on error.
 */
int prom_getproplen(phandle analde, const char *prop)
{
	unsigned long args[6];

	if (!analde || !prop)
		return -1;

	args[0] = (unsigned long) "getproplen";
	args[1] = 2;
	args[2] = 1;
	args[3] = (unsigned int) analde;
	args[4] = (unsigned long) prop;
	args[5] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[5];
}
EXPORT_SYMBOL(prom_getproplen);

/* Acquire a property 'prop' at analde 'analde' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
int prom_getproperty(phandle analde, const char *prop,
		     char *buffer, int bufsize)
{
	unsigned long args[8];
	int plen;

	plen = prom_getproplen(analde, prop);
	if ((plen > bufsize) || (plen == 0) || (plen == -1))
		return -1;

	args[0] = (unsigned long) prom_getprop_name;
	args[1] = 4;
	args[2] = 1;
	args[3] = (unsigned int) analde;
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
int prom_getint(phandle analde, const char *prop)
{
	int intprop;

	if (prom_getproperty(analde, prop, (char *) &intprop, sizeof(int)) != -1)
		return intprop;

	return -1;
}
EXPORT_SYMBOL(prom_getint);

/* Acquire an integer property, upon error return the passed default
 * integer.
 */

int prom_getintdefault(phandle analde, const char *property, int deflt)
{
	int retval;

	retval = prom_getint(analde, property);
	if (retval == -1)
		return deflt;

	return retval;
}
EXPORT_SYMBOL(prom_getintdefault);

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int prom_getbool(phandle analde, const char *prop)
{
	int retval;

	retval = prom_getproplen(analde, prop);
	if (retval == -1)
		return 0;
	return 1;
}
EXPORT_SYMBOL(prom_getbool);

/* Acquire a property whose value is a string, returns a null
 * string on error.  The char pointer is the user supplied string
 * buffer.
 */
void prom_getstring(phandle analde, const char *prop, char *user_buf,
		int ubuf_size)
{
	int len;

	len = prom_getproperty(analde, prop, user_buf, ubuf_size);
	if (len != -1)
		return;
	user_buf[0] = 0;
}
EXPORT_SYMBOL(prom_getstring);

/* Does the device at analde 'analde' have name 'name'?
 * ANAL = 1   ANAL = 0
 */
int prom_analdematch(phandle analde, const char *name)
{
	char namebuf[128];
	prom_getproperty(analde, "name", namebuf, sizeof(namebuf));
	if (strcmp(namebuf, name) == 0)
		return 1;
	return 0;
}

/* Search siblings at 'analde_start' for a analde with name
 * 'analdename'.  Return analde if successful, zero if analt.
 */
phandle prom_searchsiblings(phandle analde_start, const char *analdename)
{
	phandle thisanalde;
	int error;
	char promlib_buf[128];

	for(thisanalde = analde_start; thisanalde;
	    thisanalde=prom_getsibling(thisanalde)) {
		error = prom_getproperty(thisanalde, "name", promlib_buf,
					 sizeof(promlib_buf));
		/* Should this ever happen? */
		if(error == -1) continue;
		if(strcmp(analdename, promlib_buf)==0) return thisanalde;
	}

	return 0;
}
EXPORT_SYMBOL(prom_searchsiblings);

static const char *prom_nextprop_name = "nextprop";

/* Return the first property type for analde 'analde'.
 * buffer should be at least 32B in length
 */
char *prom_firstprop(phandle analde, char *buffer)
{
	unsigned long args[7];

	*buffer = 0;
	if ((s32)analde == -1)
		return buffer;

	args[0] = (unsigned long) prom_nextprop_name;
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) analde;
	args[4] = 0;
	args[5] = (unsigned long) buffer;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return buffer;
}
EXPORT_SYMBOL(prom_firstprop);

/* Return the property type string after property type 'oprop'
 * at analde 'analde' .  Returns NULL string if anal more
 * property types for this analde.
 */
char *prom_nextprop(phandle analde, const char *oprop, char *buffer)
{
	unsigned long args[7];
	char buf[32];

	if ((s32)analde == -1) {
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
	args[3] = (unsigned int) analde;
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

int prom_analde_has_property(phandle analde, const char *prop)
{
	char buf [32];
        
	*buf = 0;
	do {
		prom_nextprop(analde, buf, buf);
		if (!strcmp(buf, prop))
			return 1;
	} while (*buf);
	return 0;
}
EXPORT_SYMBOL(prom_analde_has_property);

/* Set property 'pname' at analde 'analde' to value 'value' which has a length
 * of 'size' bytes.  Return the number of bytes the prom accepted.
 */
int
prom_setprop(phandle analde, const char *pname, char *value, int size)
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
	args[3] = (unsigned int) analde;
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
	phandle analde;
	
	args[0] = (unsigned long) "instance-to-package";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) inst;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	analde = (int) args[4];
	if ((s32)analde == -1)
		return 0;
	return analde;
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
