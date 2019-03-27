/*	$Id: dbm.h,v 1.1 2016/07/19 21:31:55 schwarze Exp $ */
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
 * Public interface for the map-based version
 * of the mandoc database, for read-only access.
 * To be used by dbm*.c, dba_read.c, and man(1) and apropos(1).
 */

enum dbm_mtype {
	DBM_EXACT = 0,
	DBM_SUB,
	DBM_REGEX
};

struct dbm_match {
	regex_t		*re;
	const char	*str;
	enum dbm_mtype	 type;
};

struct dbm_res {
	int32_t		 page;
	int32_t		 bits;
};

struct dbm_page {
	const char	*name;
	const char	*sect;
	const char	*arch;
	const char	*desc;
	const char	*file;
	int32_t		 addr;
};

struct dbm_macro {
	const char	*value;
	const int32_t	*pp;
};

int		 dbm_open(const char *);
void		 dbm_close(void);

int32_t		 dbm_page_count(void);
struct dbm_page	*dbm_page_get(int32_t);
void		 dbm_page_byname(const struct dbm_match *);
void		 dbm_page_bysect(const struct dbm_match *);
void		 dbm_page_byarch(const struct dbm_match *);
void		 dbm_page_bydesc(const struct dbm_match *);
void		 dbm_page_bymacro(int32_t, const struct dbm_match *);
struct dbm_res	 dbm_page_next(void);

int32_t		 dbm_macro_count(int32_t);
struct dbm_macro *dbm_macro_get(int32_t, int32_t);
void		 dbm_macro_bypage(int32_t, int32_t);
char		*dbm_macro_next(void);
