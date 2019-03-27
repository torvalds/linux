/*
 * Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: map.h,v 8.4 2013-11-22 20:51:56 ca Exp $
 */

#ifndef _MAP_H
# define _MAP_H 1

extern char	*arith_map_lookup __P((MAP *, char *, char **, int *));

extern char	*bestmx_map_lookup __P((MAP *, char *, char **, int *));

extern char	*bogus_map_lookup __P((MAP *, char *, char **, int *));

extern bool	bt_map_open __P((MAP *, int));

extern char	*db_map_lookup __P((MAP *, char *, char **, int *));

extern void	db_map_store __P((MAP *, char *, char *));
extern void	db_map_close __P((MAP *));

extern bool	dequote_init __P((MAP *, char *));
extern char	*dequote_map __P((MAP *, char *, char **, int *));

extern bool	dns_map_open __P((MAP *, int));
extern bool	dns_map_parseargs __P((MAP *, char *));
extern char	*dns_map_lookup __P((MAP *, char *, char **, int *));

extern bool	dprintf_map_parseargs __P((MAP *, char *));
extern char	*dprintf_map_lookup __P((MAP *, char *, char **, int *));

extern bool	hash_map_open __P((MAP *, int));

extern bool	host_map_init __P((MAP *, char *));
extern char	*host_map_lookup __P((MAP *, char *, char **, int *));

extern char	*impl_map_lookup __P((MAP *, char *, char **, int *));
extern void	impl_map_store __P((MAP *, char *, char *));
extern bool	impl_map_open __P((MAP *, int));
extern void	impl_map_close __P((MAP *));

extern char	*macro_map_lookup __P((MAP *, char *, char **, int *));

extern bool	map_parseargs __P((MAP *, char *));

extern bool	nis_map_open __P((MAP *, int));
extern char	*nis_map_lookup __P((MAP *, char *, char **, int *));

extern bool	null_map_open __P((MAP *, int));
extern void	null_map_close __P((MAP *));
extern char	*null_map_lookup __P((MAP *, char *, char **, int *));
extern void	null_map_store __P((MAP *, char *, char *));

extern char	*prog_map_lookup __P((MAP *, char *, char **, int *));

extern bool	regex_map_init __P((MAP *, char *));
extern char	*regex_map_lookup __P((MAP *, char *, char **, int *));

extern char	*seq_map_lookup __P((MAP *, char *, char **, int *));
extern void	seq_map_store __P((MAP *, char *, char *));
extern bool	seq_map_parse __P((MAP *, char *));

extern char	*stab_map_lookup __P((MAP *, char *, char **, int *));
extern void	stab_map_store __P((MAP *, char *, char *));
extern bool	stab_map_open __P((MAP *, int));

extern bool	switch_map_open __P((MAP *, int));

extern bool	syslog_map_parseargs __P((MAP *, char *));
extern char	*syslog_map_lookup __P((MAP *, char *, char **, int *));

extern bool	text_map_open __P((MAP *, int));
extern char	*text_map_lookup __P((MAP *, char *, char **, int *));

extern char	*udb_map_lookup __P((MAP *, char *, char **, int *));

extern bool	user_map_open __P((MAP *, int));
extern char	*user_map_lookup __P((MAP *, char *, char **, int *));

#endif /* ! _MAP_H */
