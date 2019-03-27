/* $Header: /p/tcsh/cvsroot/tcsh/tw.decls.h,v 3.23 2012/06/21 17:40:40 christos Exp $ */
/*
 * tw.decls.h: Tenex external declarations
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _h_tw_decls
#define _h_tw_decls

/*
 * tw.help.c
 */
extern	void		  do_help		(const Char *);

/*
 * tw.parse.c
 */
extern	 Char		 *dollar		(const Char *);
#ifndef __MVS__
extern	 int		  tenematch		(Char *, int, COMMAND);
extern	 int		  t_search		(struct Strbuf *, COMMAND, int,
						 int, Char *, eChar);
#endif
extern	 int		  starting_a_command	(Char *, Char *);
extern	 int		  fcompare		(const void *, const void *);
extern	 void		  print_by_column	(Char *, Char *[], int, int);
extern	 int		  StrQcmp		(const Char *, const Char *);
extern	 Char		 *tgetenv		(Char *);

/*
 * tw.init.c
 */
extern	 void		  tw_alias_start	(DIR *, const Char *);
extern	 void		  tw_cmd_start		(DIR *, const Char *);
extern	 void		  tw_logname_start	(DIR *, const Char *);
extern	 void		  tw_var_start		(DIR *, const Char *);
extern	 void		  tw_complete_start	(DIR *, const Char *);
extern	 void		  tw_file_start		(DIR *, const Char *);
extern	 void		  tw_vl_start		(DIR *, const Char *);
extern	 void		  tw_wl_start		(DIR *, const Char *);
extern	 void		  tw_bind_start		(DIR *, const Char *);
extern	 void		  tw_limit_start	(DIR *, const Char *);
extern	 void		  tw_sig_start		(DIR *, const Char *);
extern	 void		  tw_job_start		(DIR *, const Char *);
extern	 void		  tw_grpname_start	(DIR *, const Char *);
extern	 int		  tw_cmd_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_logname_next	(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_shvar_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_envvar_next	(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_var_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_file_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_wl_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_bind_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_limit_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_sig_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_job_next		(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 int		  tw_grpname_next	(struct Strbuf *,
						 struct Strbuf *, int *);
extern	 void		  tw_dir_end		(void);
extern	 void		  tw_cmd_free		(void);
extern	 void		  tw_logname_end	(void);
extern	 void		  tw_grpname_end	(void);
extern	 void		  tw_item_add		(const struct Strbuf *);
extern	 Char	        **tw_item_get		(void);
extern	 void		  tw_item_free		(void);
extern	 Char		 *tw_item_find		(Char *);

/*
 * tw.spell.c
 */
extern	 int		  spell_me		(struct Strbuf *, int, Char *,
						 eChar);
extern	 int		  spdir			(struct Strbuf *, const Char *,
						 const Char *, Char *);
extern	 int		  spdist		(const Char *, const Char *);

/*
 * tw.comp.c
 */
extern	 void		  docomplete		(Char **, struct command *);
extern	 void		  douncomplete		(Char **, struct command *);
extern	 int		  tw_complete		(const Char *, Char **,
						 Char **, int, eChar *);
#ifdef COLOR_LS_F
/*
 * tw.color.c
 */
extern	 void		  set_color_context	(void);
extern	 void		  print_with_color	(const Char *, size_t, Char);
extern	 void		  parseLS_COLORS	(const Char *);
extern	 void		  parseLSCOLORS		(const Char *);
#endif /* COLOR_LS_F */

#endif /* _h_tw_decls */
