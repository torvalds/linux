PUSHDIVERT(-1)
#
# Copyright (C) 1997, Philip A. Prindeville and Enteka Enterprise Technology
#				Services
#
# Copyright (c) 1999, 2001 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
# Tested with QuickPage version 3.2
#
ifdef(`QPAGE_MAILER_PATH', `', `define(`QPAGE_MAILER_PATH', `/usr/local/bin/qpage')')
_DEFIFNOT(`QPAGE_MAILER_FLAGS', `mDFMs')
ifdef(`QPAGE_MAILER_ARGS', `', `define(`QPAGE_MAILER_ARGS', `qpage -l0 -m -P$u')')
ifdef(`QPAGE_MAILER_MAX', `', `define(`QPAGE_MAILER_MAX', `4096')')
define(`_QPAGE_QGRP', `ifelse(defn(`QPAGE_MAILER_QGRP'),`',`', ` Q=QPAGE_MAILER_QGRP,')')dnl

POPDIVERT

######################################
###   QPAGE Mailer specification   ###
######################################

VERSIONID(`$Id: qpage.m4,v 8.11 2013-11-22 20:51:14 ca Exp $')

Mqpage, P=QPAGE_MAILER_PATH, F=_MODMF_(QPAGE_MAILER_FLAGS, `QPAGE'),
	M=QPAGE_MAILER_MAX,  T=DNS/RFC822/X-Unix,_QPAGE_QGRP
	A=QPAGE_MAILER_ARGS
