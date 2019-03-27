# $Id: install-new.mk,v 1.3 2012/03/24 18:25:49 sjg Exp $
#
#	@(#) Copyright (c) 2009, Simon J. Gerraty
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

.if !defined(InstallNew)

# copy if src and target are different making a backup if desired
CmpCp= CmpCp() { \
	src=$$1 target=$$2 _bak=$$3; \
	if ! test -s $$target || ! cmp -s $$target $$src; then \
		trap "" 1 2 3 15; \
		if test -s $$target; then \
			if test "x$$_bak" != x; then \
				rm -f $$target$$_bak; \
				mv $$target $$target$$_bak; \
			else \
				rm -f $$target; \
			fi; \
		fi; \
		cp $$src $$target; \
	fi; }

# If the .new file is different, we want it.
# Note: this function will work as is for *.new$RANDOM"
InstallNew= ${CmpCp}; InstallNew() { \
	_t=-e; _bak=; \
	while :; do \
		case "$$1" in \
		-?) _t=$$1; shift;; \
		--bak) _bak=$$2; shift 2;; \
		*) break;; \
		esac; \
	done; \
	for new in "$$@"; do \
		if test $$_t $$new; then \
			target=`expr $$new : '\(.*\).new'`; \
			CmpCp $$new $$target $$_bak; \
		fi; \
		rm -f $$new; \
	done; :; }

.endif
