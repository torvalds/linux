#!/bin/sh
#
# Copyright (c) 2001-2003
#	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
#	All rights reserved.
#
# Author: Harti Brandt <harti@freebsd.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $Begemot: bsnmp/snmpd/snmpd.sh,v 1.3 2004/08/06 08:47:13 brandt Exp $
#
# SNMPd startup script
#
SNMPD=/usr/local/bin/bsnmpd
PID=/var/run/snmpd.pid
CONF=/etc/snmpd.conf

case "$1" in

start)
	if [ -r ${PID} ] ; then
		if kill -0 `cat ${PID}` ; then
			echo "snmpd already running -- pid `cat ${PID}`" >/dev/stderr
			exit 1
		fi
		rm -f ${PID}
	fi
	if ${SNMPD} -c ${CONF} -p ${PID} ; then
		echo "snmpd started"
	fi
	;;

stop)
	if [ -r ${PID} ] ; then
		if kill -0 `cat ${PID}` ; then
			if kill -15 `cat ${PID}` ; then
				echo "snmpd stopped"
				exit 0
			fi
			echo "cannot kill snmpd" >/dev/stderr
			exit 1
		fi
		echo "stale pid file -- removing" >/dev/stderr
		rm -f ${PID}
		exit 1
	fi
	echo "snmpd not running" >/dev/stderr
	;;

status)
	if [ ! -r ${PID} ] ; then
		echo "snmpd not running"
	elif kill -0 `cat ${PID}` ; then
		echo "snmpd pid `cat ${PID}`"
	else
		echo "stale pid file -- pid `cat ${PID}`"
	fi
	;;

*)
	echo "usage: `basename $0` {start|stop|status}"
	exit 1
esac

exit 0
