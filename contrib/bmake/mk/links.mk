# $Id: links.mk,v 1.6 2014/09/29 17:14:40 sjg Exp $
#
#	@(#) Copyright (c) 2005, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

# some platforms need something special
LN?= ln
ECHO?= echo

LINKS?=
SYMLINKS?=

__SYMLINK_SCRIPT= \
		${ECHO} "$$t -> $$l"; \
		case `'ls' -l $$t 2> /dev/null` in \
		*"> $$l") ;; \
		*) \
			mkdir -p `dirname $$t`; \
			rm -f $$t; \
			${LN} -s $$l $$t;; \
		esac


__LINK_SCRIPT= \
		${ECHO} "$$t -> $$l"; \
		mkdir -p `dirname $$t`; \
		rm -f $$t; \
		${LN} $$l $$t

_SYMLINKS_SCRIPT= \
	while test $$\# -ge 2; do \
		l=$$1; shift; \
		t=${DESTDIR}$$1; shift; \
		${__SYMLINK_SCRIPT}; \
	done; :;

_LINKS_SCRIPT= \
	while test $$\# -ge 2; do \
		l=${DESTDIR}$$1; shift; \
		t=${DESTDIR}$$1; shift; \
		${__LINK_SCRIPT}; \
	done; :;

_SYMLINKS_USE:	.USE
	@set ${$@_SYMLINKS:U${SYMLINKS}}; ${_SYMLINKS_SCRIPT}

_LINKS_USE:	.USE
	@set ${$@_LINKS:U${LINKS}}; ${_LINKS_SCRIPT}


# sometimes we want to ensure DESTDIR is ignored
_BUILD_SYMLINKS_SCRIPT= \
	while test $$\# -ge 2; do \
		l=$$1; shift; \
		t=$$1; shift; \
		${__SYMLINK_SCRIPT}; \
	done; :;

_BUILD_LINKS_SCRIPT= \
	while test $$\# -ge 2; do \
		l=$$1; shift; \
		t=$$1; shift; \
		${__LINK_SCRIPT}; \
	done; :;

_BUILD_SYMLINKS_USE:	.USE
	@set ${$@_SYMLINKS:U${SYMLINKS}}; ${_BUILD_SYMLINKS_SCRIPT}

_BUILD_LINKS_USE:	.USE
	@set ${$@_LINKS:U${LINKS}}; ${_BUILD_LINKS_SCRIPT}
