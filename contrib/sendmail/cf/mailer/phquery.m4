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
#  Contributed by Kimmo Suominen <kim@tac.nyc.ny.us>.
#

ifdef(`PH_MAILER_PATH',, `define(`PH_MAILER_PATH', /usr/local/etc/phquery)')
_DEFIFNOT(`PH_MAILER_FLAGS', `ehmu')
ifdef(`PH_MAILER_ARGS',, `define(`PH_MAILER_ARGS', `phquery -- $u')')
define(`_PH_QGRP', `ifelse(defn(`PH_MAILER_QGRP'),`',`', ` Q=PH_MAILER_QGRP,')')dnl

POPDIVERT

####################################
###   PH Mailer specification   ###
####################################

VERSIONID(`$Id: phquery.m4,v 8.18 2013-11-22 20:51:14 ca Exp $')

Mph,		P=PH_MAILER_PATH, F=_MODMF_(CONCAT(`nrDFM', PH_MAILER_FLAGS), `PH'), S=EnvFromL, R=EnvToL/HdrToL,
		T=DNS/RFC822/X-Unix,_PH_QGRP
		A=PH_MAILER_ARGS
