PUSHDIVERT(-1)
#
# Copyright (c) 1998-2000, 2003 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1983 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

ifdef(`USENET_MAILER_PATH',, `define(`USENET_MAILER_PATH', /usr/lib/news/inews)')
_DEFIFNOT(`USENET_MAILER_FLAGS', `rsDFMmn')
ifdef(`USENET_MAILER_ARGS',, `define(`USENET_MAILER_ARGS', `inews -m -h -n')')
define(`_USENET_QGRP', `ifelse(defn(`USENET_MAILER_QGRP'),`',`', ` Q=USENET_MAILER_QGRP,')')dnl
POPDIVERT
####################################
###  USENET Mailer specification ###
####################################

VERSIONID(`$Id: usenet.m4,v 8.23 2013-11-22 20:51:14 ca Exp $')

Musenet,	P=USENET_MAILER_PATH, F=_MODMF_(USENET_MAILER_FLAGS, `USENET'), S=EnvFromL, R=EnvToL,
		_OPTINS(`USENET_MAILER_MAX', `M=', `, ')T=X-Usenet/X-Usenet/X-Unix,_USENET_QGRP
		A=USENET_MAILER_ARGS $u
