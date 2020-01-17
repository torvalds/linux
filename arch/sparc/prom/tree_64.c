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

static phandle prom_yesde_to_yesde(const char *type, phandle yesde)
{
	unsigned long args[5];

	args[0] = (unsigned long) type;
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) yesde;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (phandle) args[4];
}

/* Return the child of yesde 'yesde' or zero if yes this yesde has yes
 * direct descendent.
 */
inline phandle __prom_getchild(phandle yesde)
{
	return prom_yesde_to_yesde("child", yesde);
}

phandle prom_getchild(phandle yesde)
{
	phandle cyesde;

	if ((s32)yesde == -1)
		return 0;
	cyesde = __prom_getchild(yesde);
	if ((s32)cyesde == -1)
		return 0;
	return cyesde;
}
EXPORT_SYMBOL(prom_getchild);

inline phandle prom_getparent(phandle yesde)
{
	phandle cyesde;

	if ((s32)yesde == -1)
		return 0;
	cyesde = prom_yesde_to_yesde("parent", yesde);
	if ((s32)cyesde == -1)
		return 0;
	return cyesde;
}

/* Return the next sibling of yesde 'yesde' or zero if yes more siblings
 * at this level of depth in the tree.
 */
inline phandle __prom_getsibling(phandle yesde)
{
	return prom_yesde_to_yesde(prom_peer_name, yesde);
}

phandle prom_getsibling(phandle yesde)
{
	phandle sibyesde;

	if ((s32)yesde == -1)
		return 0;
	sibyesde = __prom_getsibling(yesde);
	if ((s32)sibyesde == -1)
		return 0;

	return sibyesde;
}
EXPORT_SYMBOL(prom_getsibling);

/* Return the length in bytes of property 'prop' at yesde 'yesde'.
 * Return -1 on error.
 */
int prom_getproplen(phandle yesde, const char *prop)
{
	unsigned long args[6];

	if (!yesde || !prop)
		return -1;

	args[0] = (unsigned long) "getproplen";
	args[1] = 2;
	args[2] = 1;
	args[3] = (unsigned int) yesde;
	args[4] = (unsigned long) prop;
	args[5] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[5];
}
EXPORT_SYMBOL(prom_getproplen);

/* Acquire a property 'prop' at yesde 'yesde' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
int prom_getproperty(phandle yesde, const char *prop,
		     char *buffer, int bufsize)
{
	unsigned long args[8];
	int plen;

	plen = prom_getproplen(yesde, prop);
	if ((plen > bufsize) || (plen == 0) || (plen == -1))
		return -1;

	args[0] = (unsigned long) prom_getprop_name;
	args[1] = 4;
	args[2] = 1;
	args[3] = (unsigned int) yesde;
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
int prom_getint(phandle yesde, const char *prop)
{
	int intprop;

	if (prom_getproperty(yesde, prop, (char *) &intprop, sizeof(int)) != -1)
		return intprop;

	return -1;
}
EXPORT_SYMBOL(prom_getint);

/* Acquire an integer property, upon error return the passed default
 * integer.
 */

int prom_getintdefault(phandle yesde, const char *property, int deflt)
{
	int retval;

	retval = prom_getint(yesde, property);
	if (retval == -1)
		return deflt;

	return retval;
}
EXPORT_SYMBOL(prom_getintdefault);

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int prom_getbool(phandle yesde, const char *prop)
{
	int retval;

	retval = prom_getproplen(yesde, prop);
	if (retval == -1)
		return 0;
	return 1;
}
EXPORT_SYMBOL(prom_getbool);

/* Acquire a property whose value is a string, returns a null
 * string on error.  The char pointer is the user supplied string
 * buffer.
 */
void prom_getstring(phandle yesde, const char *prop, char *user_buf,
		int ubuf_size)
{
	int len;

	len = prom_getproperty(yesde, prop, user_buf, ubuf_size);
	if (len != -1)
		return;
	user_buf[0] = 0;
}
EXPORT_SYMBOL(prom_getstring);

/* Does the device at yesde 'yesde' have name 'name'?
 * YES = 1   NO = 0
 */
int prom_yesdematch(phandle yesde, const char *name)
{
	char namebuf[128];
	prom_getproperty(yesde, "name", namebuf, sizeof(namebuf));
	if (strcmp(namebuf, name) == 0)
		return 1;
	return 0;
}

/* Search siblings at 'yesde_start' for a yesde with name
 * 'yesdename'.  Return yesde if successful, zero if yest.
 */
phandle prom_searchsiblings(phandle yesde_start, const char *yesdename)
{
	phandle thisyesde;
	int error;
	char promlib_buf[128];

	for(thisyesde = yesde_start; thisyesde;
	    thisyesde=prom_getsibling(thisyesde)) {
		error = prom_getproperty(thisyesde, "name", promlib_buf,
					 sizeof(promlib_buf));
		/* Should this ever happen? */
		if(error == -1) continue;
		if(strcmp(yesdename, promlib_buf)==0) return thisyesde;
	}

	return 0;
}
EXPORT_SYMBOL(prom_searchsiblings);

static const char *prom_nextprop_name = "nextprop";

/* Return the first property type for yesde 'yesde'.
 * buffer should be at least 32B in length
 */
char *prom_firstprop(phandle yesde, char *buffer)
{
	unsigned long args[7];

	*buffer = 0;
	if ((s32)yesde == -1)
		return buffer;

	args[0] = (unsigned long) prom_nextprop_name;
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) yesde;
	args[4] = 0;
	args[5] = (unsigned long) buffer;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return buffer;
}
EXPORT_SYMBOL(prom_firstprop);

/* Return the property type string after property type 'oprop'
 * at yesde 'yesde' .  Returns NULL string if yes more
 * property types for this yesde.
 */
char *prom_nextprop(phandle yesde, const char *oprop, char *buffer)
{
	unsigned long args[7];
	char buf[32];

	if ((s32)yesde == -1) {
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
	args[3] = (unsigned int) yesde;
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

int prom_yesde_has_property(phandle yesde, const char *prop)
{
	char buf [32];
        
	*buf = 0;
	do {
		prom_nextprop(yesde, buf, buf);
		if (!strcmp(buf, prop))
			return 1;
	} while (*buf);
	return 0;
}
EXPORT_SYMBOL(prom_yesde_has_property);

/* Set property 'pname' at yesde 'yesde' to value 'value' which has a length
 * of 'size' bytes.  Return the number of bytes the prom accepted.
 */
int
prom_setprop(phandle yesde, const char *pname, char *value, int size)
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
	args[3] = (unsigned int) yesde;
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
	phandle yesde;
	
	args[0] = (unsigned long) "instance-to-package";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned int) inst;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	yesde = (int) args[4];
	if ((s32)yesde == -1)
		return 0;
	return yesde;
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
