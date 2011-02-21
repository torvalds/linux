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
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmnvram.h>
#include <sbchipc.h>
#include <bcmsrom.h>
#include <bcmotp.h>
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

#if defined(FLASH)
/* copy flash to ram */
static void get_flash_nvram(si_t *sih, struct nvram_header *nvh)
{
	struct osl_info *osh;
	uint nvs, bufsz;
	vars_t *new;

	osh = si_osh(sih);

	nvs = R_REG(osh, &nvh->len) - sizeof(struct nvram_header);
	bufsz = nvs + VARS_T_OH;

	new = kmalloc(bufsz, GFP_ATOMIC);
	if (new == NULL) {
		NVR_MSG(("Out of memory for flash vars\n"));
		return;
	}
	new->vars = (char *)new + VARS_T_OH;

	new->bufsz = bufsz;
	new->size = nvs;
	new->next = vars;
	vars = new;

	memcpy(new->vars, &nvh[1], nvs);

	NVR_MSG(("%s: flash nvram @ %p, copied %d bytes to %p\n", __func__,
		 nvh, nvs, new->vars));
}
#endif				/* FLASH */

int nvram_init(void *si)
{

	/* Make sure we read nvram in flash just once before freeing the memory */
	if (vars != NULL) {
		NVR_MSG(("nvram_init: called again without calling nvram_exit()\n"));
		return 0;
	}
	return 0;
}

int nvram_append(void *si, char *varlst, uint varsz)
{
	uint bufsz = VARS_T_OH;
	vars_t *new;

	new = kmalloc(bufsz, GFP_ATOMIC);
	if (new == NULL)
		return BCME_NOMEM;

	new->vars = varlst;
	new->bufsz = bufsz;
	new->size = varsz;
	new->next = vars;
	vars = new;

	return BCME_OK;
}

void nvram_exit(void *si)
{
	vars_t *this, *next;
	si_t *sih;

	sih = (si_t *) si;
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

int nvram_reset(void *si)
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
				return BCME_BUFTOOSHORT;
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
		return BCME_BUFTOOSHORT;
	*buf = '\0';
	return 0;
}
