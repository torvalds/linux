# $OpenBSD: dot.profile,v 1.10 2023/11/16 16:03:51 millert Exp $
#
# sh/ksh initialization

PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/X11R6/bin:/usr/local/sbin:/usr/local/bin
export PATH
: ${HOME='/root'}
export HOME
umask 022

case "$-" in
*i*)    # interactive shell
	if [ -x /usr/bin/tset ]; then
		eval `/usr/bin/tset -IsQ '-munknown:?vt220' $TERM`
	fi
	;;
esac
