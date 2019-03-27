#! /bin/sh
: 'Determine whether year is of appropriate type (this file is obsolete).'

: 'This file is in the public domain, so clarified as of'
: '2006-07-17 by Arthur David Olson.'

case $#-$1 in
	2-|2-0*|2-*[!0-9]*)
		echo "$0: wild year: $1" >&2
		exit 1 ;;
esac

case $#-$2 in
	2-even)
		case $1 in
			*[24680])			exit 0 ;;
			*)				exit 1 ;;
		esac ;;
	2-nonpres|2-nonuspres)
		case $1 in
			*[02468][048]|*[13579][26])	exit 1 ;;
			*)				exit 0 ;;
		esac ;;
	2-odd)
		case $1 in
			*[13579])			exit 0 ;;
			*)				exit 1 ;;
		esac ;;
	2-uspres)
		case $1 in
			*[02468][048]|*[13579][26])	exit 0 ;;
			*)				exit 1 ;;
		esac ;;
	2-*)
		echo "$0: wild type: $2" >&2 ;;
esac

echo "$0: usage is $0 year even|odd|uspres|nonpres|nonuspres" >&2
exit 1
