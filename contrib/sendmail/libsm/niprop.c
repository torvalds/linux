/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: niprop.c,v 1.9 2013-11-22 20:51:43 ca Exp $")

#if NETINFO
#include <ctype.h>
#include <stdlib.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/debug.h>
#include <sm/string.h>
#include <sm/varargs.h>
#include <sm/heap.h>

/*
**  NI_PROPVAL -- NetInfo property value lookup routine
**
**	Parameters:
**		keydir -- the NetInfo directory name in which to search
**			for the key.
**		keyprop -- the name of the property in which to find the
**			property we are interested.  Defaults to "name".
**		keyval -- the value for which we are really searching.
**		valprop -- the property name for the value in which we
**			are interested.
**		sepchar -- if non-nil, this can be multiple-valued, and
**			we should return a string separated by this
**			character.
**
**	Returns:
**		NULL -- if:
**			1. the directory is not found
**			2. the property name is not found
**			3. the property contains multiple values
**			4. some error occurred
**		else -- the value of the lookup.
**
**	Example:
**		To search for an alias value, use:
**		  ni_propval("/aliases", "name", aliasname, "members", ',')
**
**	Notes:
**		Caller should free the return value of ni_proval
*/

# include <netinfo/ni.h>

# define LOCAL_NETINFO_DOMAIN	"."
# define PARENT_NETINFO_DOMAIN	".."
# define MAX_NI_LEVELS		256

char *
ni_propval(keydir, keyprop, keyval, valprop, sepchar)
	char *keydir;
	char *keyprop;
	char *keyval;
	char *valprop;
	int sepchar;
{
	char *propval = NULL;
	int i;
	int j, alen, l;
	void *ni = NULL;
	void *lastni = NULL;
	ni_status nis;
	ni_id nid;
	ni_namelist ninl;
	register char *p;
	char keybuf[1024];

	/*
	**  Create the full key from the two parts.
	**
	**	Note that directory can end with, e.g., "name=" to specify
	**	an alternate search property.
	*/

	i = strlen(keydir) + strlen(keyval) + 2;
	if (keyprop != NULL)
		i += strlen(keyprop) + 1;
	if (i >= sizeof keybuf)
		return NULL;
	(void) sm_strlcpyn(keybuf, sizeof keybuf, 2, keydir, "/");
	if (keyprop != NULL)
	{
		(void) sm_strlcat2(keybuf, keyprop, "=", sizeof keybuf);
	}
	(void) sm_strlcat(keybuf, keyval, sizeof keybuf);

#if 0
	if (tTd(38, 21))
		sm_dprintf("ni_propval(%s, %s, %s, %s, %d) keybuf='%s'\n",
			keydir, keyprop, keyval, valprop, sepchar, keybuf);
#endif /* 0 */

	/*
	**  If the passed directory and property name are found
	**  in one of netinfo domains we need to search (starting
	**  from the local domain moving all the way back to the
	**  root domain) set propval to the property's value
	**  and return it.
	*/

	for (i = 0; i < MAX_NI_LEVELS && propval == NULL; i++)
	{
		if (i == 0)
		{
			nis = ni_open(NULL, LOCAL_NETINFO_DOMAIN, &ni);
#if 0
			if (tTd(38, 20))
				sm_dprintf("ni_open(LOCAL) = %d\n", nis);
#endif /* 0 */
		}
		else
		{
			if (lastni != NULL)
				ni_free(lastni);
			lastni = ni;
			nis = ni_open(lastni, PARENT_NETINFO_DOMAIN, &ni);
#if 0
			if (tTd(38, 20))
				sm_dprintf("ni_open(PARENT) = %d\n", nis);
#endif /* 0 */
		}

		/*
		**  Don't bother if we didn't get a handle on a
		**  proper domain.  This is not necessarily an error.
		**  We would get a positive ni_status if, for instance
		**  we never found the directory or property and tried
		**  to open the parent of the root domain!
		*/

		if (nis != 0)
			break;

		/*
		**  Find the path to the server information.
		*/

		if (ni_pathsearch(ni, &nid, keybuf) != 0)
			continue;

		/*
		**  Find associated value information.
		*/

		if (ni_lookupprop(ni, &nid, valprop, &ninl) != 0)
			continue;

#if 0
		if (tTd(38, 20))
			sm_dprintf("ni_lookupprop: len=%d\n",
				ninl.ni_namelist_len);
#endif /* 0 */

		/*
		**  See if we have an acceptable number of values.
		*/

		if (ninl.ni_namelist_len <= 0)
			continue;

		if (sepchar == '\0' && ninl.ni_namelist_len > 1)
		{
			ni_namelist_free(&ninl);
			continue;
		}

		/*
		**  Calculate number of bytes needed and build result
		*/

		alen = 1;
		for (j = 0; j < ninl.ni_namelist_len; j++)
			alen += strlen(ninl.ni_namelist_val[j]) + 1;
		propval = p = sm_malloc(alen);
		if (propval == NULL)
			goto cleanup;
		for (j = 0; j < ninl.ni_namelist_len; j++)
		{
			(void) sm_strlcpy(p, ninl.ni_namelist_val[j], alen);
			l = strlen(p);
			p += l;
			*p++ = sepchar;
			alen -= l + 1;
		}
		*--p = '\0';

		ni_namelist_free(&ninl);
	}

  cleanup:
	if (ni != NULL)
		ni_free(ni);
	if (lastni != NULL && ni != lastni)
		ni_free(lastni);
#if 0
	if (tTd(38, 20))
		sm_dprintf("ni_propval returns: '%s'\n", propval);
#endif /* 0 */

	return propval;
}
#endif /* NETINFO */
