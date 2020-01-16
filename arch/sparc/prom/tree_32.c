// SPDX-License-Identifier: GPL-2.0
/*
 * tree.c: Basic device tree traversal/scanning for the Linux
 *         prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/module.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

extern void restore_current(void);

static char promlib_buf[128];

/* Internal version of prom_getchild that does yest alter return values. */
static phandle __prom_getchild(phandle yesde)
{
	unsigned long flags;
	phandle cyesde;

	spin_lock_irqsave(&prom_lock, flags);
	cyesde = prom_yesdeops->yes_child(yesde);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);

	return cyesde;
}

/* Return the child of yesde 'yesde' or zero if yes this yesde has yes
 * direct descendent.
 */
phandle prom_getchild(phandle yesde)
{
	phandle cyesde;

	if ((s32)yesde == -1)
		return 0;

	cyesde = __prom_getchild(yesde);
	if (cyesde == 0 || (s32)cyesde == -1)
		return 0;

	return cyesde;
}
EXPORT_SYMBOL(prom_getchild);

/* Internal version of prom_getsibling that does yest alter return values. */
static phandle __prom_getsibling(phandle yesde)
{
	unsigned long flags;
	phandle cyesde;

	spin_lock_irqsave(&prom_lock, flags);
	cyesde = prom_yesdeops->yes_nextyesde(yesde);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);

	return cyesde;
}

/* Return the next sibling of yesde 'yesde' or zero if yes more siblings
 * at this level of depth in the tree.
 */
phandle prom_getsibling(phandle yesde)
{
	phandle sibyesde;

	if ((s32)yesde == -1)
		return 0;

	sibyesde = __prom_getsibling(yesde);
	if (sibyesde == 0 || (s32)sibyesde == -1)
		return 0;

	return sibyesde;
}
EXPORT_SYMBOL(prom_getsibling);

/* Return the length in bytes of property 'prop' at yesde 'yesde'.
 * Return -1 on error.
 */
int prom_getproplen(phandle yesde, const char *prop)
{
	int ret;
	unsigned long flags;

	if((!yesde) || (!prop))
		return -1;
		
	spin_lock_irqsave(&prom_lock, flags);
	ret = prom_yesdeops->yes_proplen(yesde, prop);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return ret;
}
EXPORT_SYMBOL(prom_getproplen);

/* Acquire a property 'prop' at yesde 'yesde' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
int prom_getproperty(phandle yesde, const char *prop, char *buffer, int bufsize)
{
	int plen, ret;
	unsigned long flags;

	plen = prom_getproplen(yesde, prop);
	if((plen > bufsize) || (plen == 0) || (plen == -1))
		return -1;
	/* Ok, things seem all right. */
	spin_lock_irqsave(&prom_lock, flags);
	ret = prom_yesdeops->yes_getprop(yesde, prop, buffer);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return ret;
}
EXPORT_SYMBOL(prom_getproperty);

/* Acquire an integer property and return its value.  Returns -1
 * on failure.
 */
int prom_getint(phandle yesde, char *prop)
{
	static int intprop;

	if(prom_getproperty(yesde, prop, (char *) &intprop, sizeof(int)) != -1)
		return intprop;

	return -1;
}
EXPORT_SYMBOL(prom_getint);

/* Acquire an integer property, upon error return the passed default
 * integer.
 */
int prom_getintdefault(phandle yesde, char *property, int deflt)
{
	int retval;

	retval = prom_getint(yesde, property);
	if(retval == -1) return deflt;

	return retval;
}
EXPORT_SYMBOL(prom_getintdefault);

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int prom_getbool(phandle yesde, char *prop)
{
	int retval;

	retval = prom_getproplen(yesde, prop);
	if(retval == -1) return 0;
	return 1;
}
EXPORT_SYMBOL(prom_getbool);

/* Acquire a property whose value is a string, returns a null
 * string on error.  The char pointer is the user supplied string
 * buffer.
 */
void prom_getstring(phandle yesde, char *prop, char *user_buf, int ubuf_size)
{
	int len;

	len = prom_getproperty(yesde, prop, user_buf, ubuf_size);
	if(len != -1) return;
	user_buf[0] = 0;
}
EXPORT_SYMBOL(prom_getstring);


/* Search siblings at 'yesde_start' for a yesde with name
 * 'yesdename'.  Return yesde if successful, zero if yest.
 */
phandle prom_searchsiblings(phandle yesde_start, char *yesdename)
{

	phandle thisyesde;
	int error;

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

/* Interal version of nextprop that does yest alter return values. */
static char *__prom_nextprop(phandle yesde, char * oprop)
{
	unsigned long flags;
	char *prop;

	spin_lock_irqsave(&prom_lock, flags);
	prop = prom_yesdeops->yes_nextprop(yesde, oprop);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);

	return prop;
}

/* Return the property type string after property type 'oprop'
 * at yesde 'yesde' .  Returns empty string if yes more
 * property types for this yesde.
 */
char *prom_nextprop(phandle yesde, char *oprop, char *buffer)
{
	if (yesde == 0 || (s32)yesde == -1)
		return "";

	return __prom_nextprop(yesde, oprop);
}
EXPORT_SYMBOL(prom_nextprop);

phandle prom_finddevice(char *name)
{
	char nbuf[128];
	char *s = name, *d;
	phandle yesde = prom_root_yesde, yesde2;
	unsigned int which_io, phys_addr;
	struct linux_prom_registers reg[PROMREG_MAX];

	while (*s++) {
		if (!*s) return yesde; /* path '.../' is legal */
		yesde = prom_getchild(yesde);

		for (d = nbuf; *s != 0 && *s != '@' && *s != '/';)
			*d++ = *s++;
		*d = 0;
		
		yesde = prom_searchsiblings(yesde, nbuf);
		if (!yesde)
			return 0;

		if (*s == '@') {
			if (isxdigit(s[1]) && s[2] == ',') {
				which_io = simple_strtoul(s+1, NULL, 16);
				phys_addr = simple_strtoul(s+3, &d, 16);
				if (d != s + 3 && (!*d || *d == '/')
				    && d <= s + 3 + 8) {
					yesde2 = yesde;
					while (yesde2 && (s32)yesde2 != -1) {
						if (prom_getproperty (yesde2, "reg", (char *)reg, sizeof (reg)) > 0) {
							if (which_io == reg[0].which_io && phys_addr == reg[0].phys_addr) {
								yesde = yesde2;
								break;
							}
						}
						yesde2 = prom_getsibling(yesde2);
						if (!yesde2 || (s32)yesde2 == -1)
							break;
						yesde2 = prom_searchsiblings(prom_getsibling(yesde2), nbuf);
					}
				}
			}
			while (*s != 0 && *s != '/') s++;
		}
	}
	return yesde;
}
EXPORT_SYMBOL(prom_finddevice);

/* Set property 'pname' at yesde 'yesde' to value 'value' which has a length
 * of 'size' bytes.  Return the number of bytes the prom accepted.
 */
int prom_setprop(phandle yesde, const char *pname, char *value, int size)
{
	unsigned long flags;
	int ret;

	if (size == 0)
		return 0;
	if ((pname == NULL) || (value == NULL))
		return 0;
	spin_lock_irqsave(&prom_lock, flags);
	ret = prom_yesdeops->yes_setprop(yesde, pname, value, size);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return ret;
}
EXPORT_SYMBOL(prom_setprop);

phandle prom_inst2pkg(int inst)
{
	phandle yesde;
	unsigned long flags;
	
	spin_lock_irqsave(&prom_lock, flags);
	yesde = (*romvec->pv_v2devops.v2_inst2pkg)(inst);
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	if ((s32)yesde == -1)
		return 0;
	return yesde;
}
