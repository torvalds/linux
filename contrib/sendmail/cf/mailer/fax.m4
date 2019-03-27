PUSHDIVERT(-1)
#
# Copyright (c) 1998, 1999, 2001 Proofpoint, Inc. and its suppliers.
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
#  This assumes you already have Sam Leffler's HylaFAX software.
#
#  Tested with HylaFAX 4.0pl1
#

ifdef(`FAX_MAILER_ARGS',,
	`define(`FAX_MAILER_ARGS', faxmail -d $u@$h $f)')
ifdef(`FAX_MAILER_PATH',,
	`define(`FAX_MAILER_PATH', /usr/local/bin/faxmail)')
ifdef(`FAX_MAILER_MAX',,
	`define(`FAX_MAILER_MAX', 100000)')
define(`_FAX_QGRP', `ifelse(defn(`FAX_MAILER_QGRP'),`',`', ` Q=FAX_MAILER_QGRP,')')dnl
POPDIVERT
####################################
###   FAX Mailer specification   ###
####################################

VERSIONID(`$Id: fax.m4,v 8.17 2013-11-22 20:51:14 ca Exp $')

Mfax,		P=FAX_MAILER_PATH, F=DFMhu, S=14, R=24,
		M=FAX_MAILER_MAX, T=X-Phone/X-FAX/X-Unix,_FAX_QGRP
		A=FAX_MAILER_ARGS

LOCAL_CONFIG
CPFAX
