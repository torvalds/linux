:
# $Id: mkopt.sh,v 1.11 2017/03/18 21:36:42 sjg Exp $
#
#	@(#) Copyright (c) 2014, Simon J. Gerraty
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

# handle WITH[OUT]_* options in a manner compatible with
# options.mk and bsd.mkopt.mk in recent FreeBSD

# no need to be included more than once
_MKOPT_SH=:
_MKOPT_PREFIX=${_MKOPT_PREFIX:-MK_}

#
# _mk_opt default OPT
#
# Set MK_$OPT
#
# The semantics are simple, if MK_$OPT has no value
# WITHOUT_$OPT results in MK_$OPT=no
# otherwise WITH_$OPT results in MK_$OPT=yes.
# Note WITHOUT_$OPT overrides WITH_$OPT.
#
# For backwards compatability reasons we treat WITH_$OPT=no
# the same as WITHOUT_$OPT.
#
_mk_opt() {
    _d=$1
    _mo=${_MKOPT_PREFIX}$2 _wo=WITHOUT_$2 _wi=WITH_$2
    eval "_mov=\$$_mo _wov=\$$_wo _wiv=\$$_wi"

    case "$_wiv" in
    [Nn][Oo]) _wov=no;;
    esac
    _v=${_mov:-${_wov:+no}}
    _v=${_v:-${_wiv:+yes}}
    _v=${_v:-$_d}
    _opt_list="$_opt_list $_mo"
    case "$_v" in
    yes|no) ;;			# sane
    0|[NnFf]*) _v=no;;		# they mean no
    1|[YyTt]*) _v=yes;;		# they mean yes
    *) _v=$_d;;			# ignore bogus value
    esac
    eval "$_mo=$_v"
}

#
# _mk_opts default opt ... [default [opt] ...]
#
# see _mk_opts_defaults for example
#
_mk_opts() {
    _d=no
    for _o in "$@"
    do
	case "$_o" in
	*/*) # option is dirname default comes from basename
	    eval "_d=\$${_MKOPT_PREFIX}${_o#*/}"
	    _o=${_o%/*}
	    ;;
	yes|no) _d=$_o; continue;;
	esac
	_mk_opt $_d $_o
    done
}

# handle either options.mk style OPTIONS_DEFAULT_*
# or FreeBSD's new bsd.mkopt.mk style __DEFAULT_*_OPTIONS
_mk_opts_defaults() {
    _mk_opts no $OPTIONS_DEFAULT_NO $__DEFAULT_NO_OPTIONS \
	yes $OPTIONS_DEFAULT_YES $__DEFAULT_YES_OPTIONS \
	$OPTIONS_DEFAULT_DEPENDENT $__DEFAULT_DEPENDENT_OPTIONS
}

case "/$0" in
*/mkopt*)
    _list=no
    while :
    do
	case "$1" in
	*=*) eval "$1"; shift;;
	--no|no) _list="$_list no"; shift;;
	--yes|yes) _list="$_list yes"; shift;;
	-DWITH*) eval "${1#-D}=1"; shift;;
	[A-Z]*) _list="$_list $1"; shift;;
	*) break;;
	esac
    done
    _mk_opts $_list
    ;;
esac

