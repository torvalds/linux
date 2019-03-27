PUSHDIVERT(-1)
#
# Copyright (c) 2002 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#	Contributed by Kenneth Murchison.
#

_DEFIFNOT(`_DEF_CYRUSV2_MAILER_FLAGS', `lsDFMnqXz')
_DEFIFNOT(`CYRUSV2_MAILER_FLAGS', `A@/:|m')
ifdef(`CYRUSV2_MAILER_ARGS',, `define(`CYRUSV2_MAILER_ARGS', `FILE /var/imap/socket/lmtp')')
define(`_CYRUSV2_QGRP', `ifelse(defn(`CYRUSV2_MAILER_QGRP'),`',`', ` Q=CYRUSV2_MAILER_QGRP,')')dnl

POPDIVERT

#########################################
###   Cyrus V2 Mailer specification   ###
#########################################

VERSIONID(`$Id: cyrusv2.m4,v 1.2 2013-11-22 20:51:14 ca Exp $')

Mcyrusv2,	P=[IPC], F=_MODMF_(CONCAT(_DEF_CYRUSV2_MAILER_FLAGS, CYRUSV2_MAILER_FLAGS), `CYRUSV2'),
		S=EnvFromSMTP/HdrFromL, R=EnvToL/HdrToL, E=\r\n,
		_OPTINS(`CYRUSV2_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`CYRUSV2_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`CYRUSV2_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/SMTP,_CYRUSV2_QGRP
		A=CYRUSV2_MAILER_ARGS
