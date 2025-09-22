# $OpenBSD: dot.login,v 1.7 2023/11/16 16:05:13 millert Exp $
#
# csh login file

if ( ! $?TERMCAP ) then
	tset -IQ '-munknown:?vt220' $TERM
endif

stty	newcrt crterase

set	savehist=100
set	ignoreeof

setenv	EXINIT		'set ai sm noeb'

if (-x /usr/games/fortune) /usr/games/fortune
