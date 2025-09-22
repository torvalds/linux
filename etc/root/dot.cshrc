# $OpenBSD: dot.cshrc,v 1.13 2005/02/13 00:56:13 krw Exp $
#
# csh initialization

umask 022
alias mail Mail
set history=1000
set path=(/sbin /usr/sbin /bin /usr/bin /usr/X11R6/bin /usr/local/sbin /usr/local/bin)
set filec

setenv BLOCKSIZE 1k

alias	cd	'set old="$cwd"; chdir \!*'
alias	h	history
alias	j	jobs -l
alias	ll	ls -l
alias	l	ls -alF
alias	back	'set back="$old"; set old="$cwd"; cd "$back"; unset back; dirs'

alias	z	suspend
alias	x	exit
alias	pd	pushd
alias	pd2	pushd +2
alias	pd3	pushd +3
alias	pd4	pushd +4

if ($?prompt) then
	set prompt="`hostname -s`# "
endif
