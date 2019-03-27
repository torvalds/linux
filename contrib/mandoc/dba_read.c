/*	$Id: dba_read.c,v 1.4 2016/08/17 20:46:56 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Function to read the mandoc database from disk into RAM,
 * such that data can be added or removed.
 * The interface is defined in "dba.h".
 * This file is seperate from dba.c because this also uses "dbm.h".
 */
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mansearch.h"
#include "dba_array.h"
#include "dba.h"
#include "dbm.h"


struct dba *
dba_read(const char *fname)
{
	struct dba		*dba;
	struct dba_array	*page;
	struct dbm_page		*pdata;
	struct dbm_macro	*mdata;
	const char		*cp;
	int32_t			 im, ip, iv, npages;

	if (dbm_open(fname) == -1)
		return NULL;
	npages = dbm_page_count();
	dba = dba_new(npages < 128 ? 128 : npages);
	for (ip = 0; ip < npages; ip++) {
		pdata = dbm_page_get(ip);
		page = dba_page_new(dba->pages, pdata->arch,
		    pdata->desc, pdata->file + 1, *pdata->file);
		for (cp = pdata->name; *cp != '\0'; cp = strchr(cp, '\0') + 1)
			dba_page_add(page, DBP_NAME, cp);
		for (cp = pdata->sect; *cp != '\0'; cp = strchr(cp, '\0') + 1)
			dba_page_add(page, DBP_SECT, cp);
		if ((cp = pdata->arch) != NULL)
			while (*(cp = strchr(cp, '\0') + 1) != '\0')
				dba_page_add(page, DBP_ARCH, cp);
		cp = pdata->file;
		while (*(cp = strchr(cp, '\0') + 1) != '\0')
			dba_page_add(page, DBP_FILE, cp);
	}
	for (im = 0; im < MACRO_MAX; im++) {
		for (iv = 0; iv < dbm_macro_count(im); iv++) {
			mdata = dbm_macro_get(im, iv);
			dba_macro_new(dba, im, mdata->value, mdata->pp);
		}
	}
	dbm_close();
	return dba;
}
