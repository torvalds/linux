/*
 * Copyright (C) 2004, 2005, 2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1996-2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <ctype.h>

#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/string.h>
#include <isc/symtab.h>
#include <isc/util.h>

typedef struct elt {
	char *				key;
	unsigned int			type;
	isc_symvalue_t			value;
	LINK(struct elt)		link;
} elt_t;

typedef LIST(elt_t)			eltlist_t;

#define SYMTAB_MAGIC			ISC_MAGIC('S', 'y', 'm', 'T')
#define VALID_SYMTAB(st)		ISC_MAGIC_VALID(st, SYMTAB_MAGIC)

struct isc_symtab {
	/* Unlocked. */
	unsigned int			magic;
	isc_mem_t *			mctx;
	unsigned int			size;
	unsigned int			count;
	unsigned int			maxload;
	eltlist_t *			table;
	isc_symtabaction_t		undefine_action;
	void *				undefine_arg;
	isc_boolean_t			case_sensitive;
};

isc_result_t
isc_symtab_create(isc_mem_t *mctx, unsigned int size,
		  isc_symtabaction_t undefine_action,
		  void *undefine_arg,
		  isc_boolean_t case_sensitive,
		  isc_symtab_t **symtabp)
{
	isc_symtab_t *symtab;
	unsigned int i;

	REQUIRE(mctx != NULL);
	REQUIRE(symtabp != NULL && *symtabp == NULL);
	REQUIRE(size > 0);	/* Should be prime. */

	symtab = (isc_symtab_t *)isc_mem_get(mctx, sizeof(*symtab));
	if (symtab == NULL)
		return (ISC_R_NOMEMORY);
	symtab->table = (eltlist_t *)isc_mem_get(mctx,
						 size * sizeof(eltlist_t));
	if (symtab->table == NULL) {
		isc_mem_put(mctx, symtab, sizeof(*symtab));
		return (ISC_R_NOMEMORY);
	}
	for (i = 0; i < size; i++)
		INIT_LIST(symtab->table[i]);
	symtab->mctx = mctx;
	symtab->size = size;
	symtab->count = 0;
	symtab->maxload = size * 3 / 4;
	symtab->undefine_action = undefine_action;
	symtab->undefine_arg = undefine_arg;
	symtab->case_sensitive = case_sensitive;
	symtab->magic = SYMTAB_MAGIC;

	*symtabp = symtab;

	return (ISC_R_SUCCESS);
}

void
isc_symtab_destroy(isc_symtab_t **symtabp) {
	isc_symtab_t *symtab;
	unsigned int i;
	elt_t *elt, *nelt;

	REQUIRE(symtabp != NULL);
	symtab = *symtabp;
	REQUIRE(VALID_SYMTAB(symtab));

	for (i = 0; i < symtab->size; i++) {
		for (elt = HEAD(symtab->table[i]); elt != NULL; elt = nelt) {
			nelt = NEXT(elt, link);
			if (symtab->undefine_action != NULL)
			       (symtab->undefine_action)(elt->key,
							 elt->type,
							 elt->value,
							 symtab->undefine_arg);
			isc_mem_put(symtab->mctx, elt, sizeof(*elt));
		}
	}
	isc_mem_put(symtab->mctx, symtab->table,
		    symtab->size * sizeof(eltlist_t));
	symtab->magic = 0;
	isc_mem_put(symtab->mctx, symtab, sizeof(*symtab));

	*symtabp = NULL;
}

static inline unsigned int
hash(const char *key, isc_boolean_t case_sensitive) {
	const char *s;
	unsigned int h = 0;
	int c;

	/*
	 * This hash function is similar to the one Ousterhout
	 * uses in Tcl.
	 */

	if (case_sensitive) {
		for (s = key; *s != '\0'; s++) {
			h += (h << 3) + *s;
		}
	} else {
		for (s = key; *s != '\0'; s++) {
			c = *s;
			c = tolower((unsigned char)c);
			h += (h << 3) + c;
		}
	}

	return (h);
}

#define FIND(s, k, t, b, e) \
	b = hash((k), (s)->case_sensitive) % (s)->size; \
	if ((s)->case_sensitive) { \
		for (e = HEAD((s)->table[b]); e != NULL; e = NEXT(e, link)) { \
			if (((t) == 0 || e->type == (t)) && \
			    strcmp(e->key, (k)) == 0) \
				break; \
		} \
	} else { \
		for (e = HEAD((s)->table[b]); e != NULL; e = NEXT(e, link)) { \
			if (((t) == 0 || e->type == (t)) && \
			    strcasecmp(e->key, (k)) == 0) \
				break; \
		} \
	}

isc_result_t
isc_symtab_lookup(isc_symtab_t *symtab, const char *key, unsigned int type,
		  isc_symvalue_t *value)
{
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);

	FIND(symtab, key, type, bucket, elt);

	if (elt == NULL)
		return (ISC_R_NOTFOUND);

	if (value != NULL)
		*value = elt->value;

	return (ISC_R_SUCCESS);
}

static void
grow_table(isc_symtab_t *symtab) {
	eltlist_t *newtable;
	unsigned int i, newsize, newmax;

	REQUIRE(symtab != NULL);

	newsize = symtab->size * 2;
	newmax = newsize * 3 / 4;
	INSIST(newsize > 0U && newmax > 0U);

	newtable = isc_mem_get(symtab->mctx, newsize * sizeof(eltlist_t));
	if (newtable == NULL)
		return;

	for (i = 0; i < newsize; i++)
		INIT_LIST(newtable[i]);

	for (i = 0; i < symtab->size; i++) {
		elt_t *elt, *nelt;

		for (elt = HEAD(symtab->table[i]); elt != NULL; elt = nelt) {
			unsigned int hv;

			nelt = NEXT(elt, link);

			UNLINK(symtab->table[i], elt, link);
			hv = hash(elt->key, symtab->case_sensitive);
			APPEND(newtable[hv % newsize], elt, link);
		}
	}

	isc_mem_put(symtab->mctx, symtab->table,
		    symtab->size * sizeof(eltlist_t));

	symtab->table = newtable;
	symtab->size = newsize;
	symtab->maxload = newmax;
}

isc_result_t
isc_symtab_define(isc_symtab_t *symtab, const char *key, unsigned int type,
		  isc_symvalue_t value, isc_symexists_t exists_policy)
{
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);
	REQUIRE(type != 0);

	FIND(symtab, key, type, bucket, elt);

	if (exists_policy != isc_symexists_add && elt != NULL) {
		if (exists_policy == isc_symexists_reject)
			return (ISC_R_EXISTS);
		INSIST(exists_policy == isc_symexists_replace);
		UNLINK(symtab->table[bucket], elt, link);
		if (symtab->undefine_action != NULL)
			(symtab->undefine_action)(elt->key, elt->type,
						  elt->value,
						  symtab->undefine_arg);
	} else {
		elt = (elt_t *)isc_mem_get(symtab->mctx, sizeof(*elt));
		if (elt == NULL)
			return (ISC_R_NOMEMORY);
		ISC_LINK_INIT(elt, link);
		symtab->count++;
	}

	/*
	 * Though the "key" can be const coming in, it is not stored as const
	 * so that the calling program can easily have writable access to
	 * it in its undefine_action function.  In the event that it *was*
	 * truly const coming in and then the caller modified it anyway ...
	 * well, don't do that!
	 */
	DE_CONST(key, elt->key);
	elt->type = type;
	elt->value = value;

	/*
	 * We prepend so that the most recent definition will be found.
	 */
	PREPEND(symtab->table[bucket], elt, link);

	if (symtab->count > symtab->maxload)
		grow_table(symtab);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_symtab_undefine(isc_symtab_t *symtab, const char *key, unsigned int type) {
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);

	FIND(symtab, key, type, bucket, elt);

	if (elt == NULL)
		return (ISC_R_NOTFOUND);

	if (symtab->undefine_action != NULL)
		(symtab->undefine_action)(elt->key, elt->type,
					  elt->value, symtab->undefine_arg);
	UNLINK(symtab->table[bucket], elt, link);
	isc_mem_put(symtab->mctx, elt, sizeof(*elt));
	symtab->count--;

	return (ISC_R_SUCCESS);
}
