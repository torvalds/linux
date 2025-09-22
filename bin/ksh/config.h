/*	$OpenBSD: config.h,v 1.19 2018/01/15 14:58:05 jca Exp $	*/

/* config.h.  NOT generated automatically. */

/*
 * This file, config.h, which is a part of pdksh (the public domain ksh),
 * is placed in the public domain.  It comes with no licence, warranty
 * or guarantee of any kind (i.e., at your own risk).
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Strict POSIX behaviour? */
/* #undef POSIXLY_CORRECT */

/* Specify default $ENV? */
/* #undef DEFAULT_ENV */

/*
 * End of configuration stuff for PD ksh.
 */

#if !defined(EMACS) && !defined(VI)
# error "Define either EMACS or VI."
#endif

#endif /* CONFIG_H */
