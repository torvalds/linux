#!/bin/sh
#
# $NetBSD: blacklistd,v 1.2 2016/10/17 22:47:16 christos Exp $
#

# PROVIDE: blacklistd
# REQUIRE: npf
# BEFORE:  SERVERS

$_rc_subr_loaded . /etc/rc.subr

name="blacklistd"
rcvar=$name
command="/sbin/${name}"
pidfile="/var/run/${name}.pid"
required_files="/etc/${name}.conf"
start_precmd="${name}_precmd"
extra_commands="reload"

_sockfile="/var/run/${name}.sockets"
_sockname="blacklistd.sock"

blacklistd_precmd()
{
	#	Create default list of blacklistd sockets to watch
	#
	( umask 022 ; > $_sockfile )

	#	Find /etc/rc.d scripts with "chrootdir" rcorder(8) keyword,
	#	and if $${app}_chrootdir is a directory, add appropriate
	#	blacklistd socket to list of sockets to watch.
	#
	for _lr in $(rcorder -k chrootdir /etc/rc.d/*); do
	    (
		_l=${_lr##*/}
		load_rc_config ${_l}
		eval _ldir=\$${_l}_chrootdir
		if checkyesno $_l && [ -n "$_ldir" ]; then
			echo "${_ldir}/var/run/${_sockname}" >> $_sockfile
		fi
	    )
	done

	#	If other sockets have been provided, change run_rc_command()'s
	#	internal copy of $blacklistd_flags to force use of specific
	#	blacklistd sockets.
	#
	if [ -s $_sockfile ]; then
		echo "/var/run/${_sockname}" >> $_sockfile
		rc_flags="-P $_sockfile $rc_flags"
	fi

	return 0
}

load_rc_config $name
run_rc_command "$1"
