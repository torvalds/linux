# $OpenBSD: dot.login,v 1.15 2023/11/16 16:03:51 millert Exp $
#
# csh login file

if ( -x /usr/bin/tset ) then
	set noglob histchars=""
	onintr finish
	eval `tset -IsQ '-munknown:?vt220' $TERM`
	finish:
	unset noglob histchars
	onintr
endif
