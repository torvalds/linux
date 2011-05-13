/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmnvram.h>
#include <sbchipc.h>
#include <bcmdevs.h>
#include <hndsoc.h>

#define NVR_MSG(x)

typedef struct _vars {
	struct _vars *next;
	int bufsz;		/* allocated size */
	int size;		/* actual vars size */
	char *vars;
} vars_t;

#define	VARS_T_OH	sizeof(vars_t)

static vars_t *vars;

#define NVRAM_FILE	1

static char *findvar(char *vars, char *lim, const char *name);

int nvram_init(void)
{

	/* Make sure we read nvram in flash just once before freeing the memory */
	if (vars != NULL) {
		NVR_MSG(("nvram_init: called again without calling nvram_exit()\n"));
		return 0;
	}
	return 0;
}

int nvram_append(char *varlst, uint varsz)
{
	uint bufsz = VARS_T_OH;
	vars_t *new;

	new = kmalloc(bufsz, GFP_ATOMIC);
	if (new == NULL)
		return -ENOMEM;

	new->vars = varlst;
	new->bufsz = bufsz;
	new->size = varsz;
	new->next = vars;
	vars = new;

	return 0;
}

void nvram_exit(void)
{
	vars_t *this, *next;

	this = vars;
	if (this)
		kfree(this->vars);

	while (this) {
		next = this->next;
		kfree(this);
		this = next;
	}
	vars = NULL;
}

static char *findvar(char *vars, char *lim, const char *name)
{
	char *s;
	int len;

	len = strlen(name);

	for (s = vars; (s < lim) && *s;) {
		if ((memcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}

	return NULL;
}

/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *getvar(char *vars, const char *name)
{
	char *s;
	int len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	/* first look in vars[] */
	for (s = vars; s && *s;) {
		if ((memcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}
	/* then query nvram */
	return nvram_get(name);
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.
 */
int getintvar(char *vars, const char *name)
{
	char *val;

	val = getvar(vars, name);
	if (val == NULL)
		return 0;

	return simple_strtoul(val, NULL, 0);
}

char *nvram_get(const char *name)
{
	char *v = NULL;
	vars_t *cur;

	for (cur = vars; cur; cur = cur->next) {
		v = findvar(cur->vars, cur->vars + cur->size, name);
		if (v)
			break;
	}

	return v;
}

int nvram_set(const char *name, const char *value)
{
	return 0;
}

int nvram_unset(const char *name)
{
	return 0;
}

int nvram_reset(void)
{
	return 0;
}

int nvram_commit(void)
{
	return 0;
}

int nvram_getall(char *buf, int count)
{
	int len, resid = count;
	vars_t *this;

	this = vars;
	while (this) {
		char *from, *lim, *to;
		int acc;

		from = this->vars;
		lim = (char *)(this->vars + this->size);
		to = buf;
		acc = 0;
		while ((from < lim) && (*from)) {
			len = strlen(from) + 1;
			if (resid < (acc + len))
				return -EOVERFLOW;
			memcpy(to, from, len);
			acc += len;
			from += len;
			to += len;
		}

		resid -= acc;
		buf += acc;
		this = this->next;
	}
	if (resid < 1)
		return -EOVERFLOW;
	*buf = '\0';
	return 0;
}
