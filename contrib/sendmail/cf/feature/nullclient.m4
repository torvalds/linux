divert(-1)
#
# Copyright (c) 1998-2000 Proofpoint, Inc. and its suppliers.
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
ifelse(defn(`_ARG_'), `', `errprint(`Feature "nullclient" requires argument')',
	`define(`_NULL_CLIENT_', _ARG_)')

#
#  This is used only for relaying mail from a client to a hub when
#  that client does absolutely nothing else -- i.e., it is a "null
#  mailer".  In this sense, it acts like the "R" option in Sun
#  sendmail.
#

divert(0)
VERSIONID(`$Id: nullclient.m4,v 8.25 2013-11-22 20:51:11 ca Exp $')
divert(-1)

undefine(`ALIAS_FILE')
define(`MAIL_HUB', _NULL_CLIENT_)
define(`SMART_HOST', _NULL_CLIENT_)
define(`confFORWARD_PATH', `')
ifdef(`confFROM_HEADER',, `define(`confFROM_HEADER', `<$g>')')
define(`_DEF_LOCAL_MAILER_FLAGS', `lsDFM5q')
MASQUERADE_AS(_NULL_CLIENT_)
FEATURE(`allmasquerade')
FEATURE(`masquerade_envelope')
MAILER(`local')
MAILER(`smtp')
