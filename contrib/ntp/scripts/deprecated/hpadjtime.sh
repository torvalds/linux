#! /bin/sh
val=1
if [ -f /bin/uname -o -f /usr/bin/uname ]; then
	set `uname -a | tr '[A-Z]' '[a-z]'`
	case "$1" in
		hp-ux)  case "$3" in
			*.10.*) val=1 ;;
			*.09.03 | *.09.10) case "$5" in
				9000/3*) val=1 ;;
				*)       val=0 ;;
				esac ;;
			*) val=0 ;;
			esac
			;;
	*)
	esac
fi
exit $val
