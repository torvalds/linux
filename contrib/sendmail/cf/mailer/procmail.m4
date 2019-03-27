PUSHDIVERT(-1)
#
# Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
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

ifdef(`PROCMAIL_MAILER_PATH',,
	`ifdef(`PROCMAIL_PATH',
		`define(`PROCMAIL_MAILER_PATH', PROCMAIL_PATH)',
		`define(`PROCMAIL_MAILER_PATH', /usr/local/bin/procmail)')')
_DEFIFNOT(`PROCMAIL_MAILER_FLAGS', `SPhnu9')
ifdef(`PROCMAIL_MAILER_ARGS',,
	`define(`PROCMAIL_MAILER_ARGS', `procmail -Y -m $h $f $u')')
define(`_PROCMAIL_QGRP', `ifelse(defn(`PROCMAIL_MAILER_QGRP'),`',`', ` Q=PROCMAIL_MAILER_QGRP,')')dnl

POPDIVERT

######################*****##############
###   PROCMAIL Mailer specification   ###
##################*****##################

VERSIONID(`$Id: procmail.m4,v 8.23 2013-11-22 20:51:14 ca Exp $')

Mprocmail,	P=PROCMAIL_MAILER_PATH, F=_MODMF_(CONCAT(`DFM', PROCMAIL_MAILER_FLAGS), `PROCMAIL'), S=EnvFromSMTP/HdrFromSMTP, R=EnvToSMTP/HdrFromSMTP,
		ifdef(`PROCMAIL_MAILER_MAX', `M=PROCMAIL_MAILER_MAX, ')T=DNS/RFC822/X-Unix,_PROCMAIL_QGRP
		A=PROCMAIL_MAILER_ARGS
