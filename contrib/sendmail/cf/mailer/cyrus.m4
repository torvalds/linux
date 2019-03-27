PUSHDIVERT(-1)
#
# Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#
# This code incorporates code from Carnegie Mellon University, whose
# copyright notice and conditions of redistribution are as follows:
#
#***************************************************************************
#	(C) Copyright 1995 by Carnegie Mellon University
#
#                      All Rights Reserved
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted,
# provided that the above copyright notice appear in all copies and that
# both that copyright notice and this permission notice appear in
# supporting documentation, and that the name of CMU not be
# used in advertising or publicity pertaining to distribution of the
# software without specific, written prior permission.
#
# CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
# ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
# CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
# ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
# ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
# SOFTWARE.
#
#	Contributed to Berkeley by John Gardiner Myers <jgm+@CMU.EDU>.
#

_DEFIFNOT(`CYRUS_MAILER_FLAGS', `Ah5@/:|')
ifdef(`CYRUS_MAILER_PATH',, `define(`CYRUS_MAILER_PATH', /usr/cyrus/bin/deliver)')
ifdef(`CYRUS_MAILER_ARGS',, `define(`CYRUS_MAILER_ARGS', `deliver -e -m $h -- $u')')
ifdef(`CYRUS_MAILER_USER',, `define(`CYRUS_MAILER_USER', `cyrus:mail')')
_DEFIFNOT(`CYRUS_BB_MAILER_FLAGS', `u')
ifdef(`CYRUS_BB_MAILER_ARGS',, `define(`CYRUS_BB_MAILER_ARGS', `deliver -e -m $u')')
define(`_CYRUS_QGRP', `ifelse(defn(`CYRUS_MAILER_QGRP'),`',`', ` Q=CYRUS_MAILER_QGRP,')')dnl

POPDIVERT

##################################################
###   Cyrus Mailer specification               ###
##################################################

VERSIONID(`$Id: cyrus.m4,v 8.24 2013-11-22 20:51:14 ca Exp $ (Carnegie Mellon)')

Mcyrus,		P=CYRUS_MAILER_PATH, F=_MODMF_(CONCAT(`lsDFMnPq', CYRUS_MAILER_FLAGS), `CYRUS'), S=EnvFromL, R=EnvToL/HdrToL,
		ifdef(`CYRUS_MAILER_MAX', `M=CYRUS_MAILER_MAX, ')U=CYRUS_MAILER_USER, T=DNS/RFC822/X-Unix,_CYRUS_QGRP
		A=CYRUS_MAILER_ARGS

Mcyrusbb,	P=CYRUS_MAILER_PATH, F=_MODMF_(CONCAT(`lsDFMnP', CYRUS_BB_MAILER_FLAGS), `CYRUS'), S=EnvFromL, R=EnvToL/HdrToL,
		ifdef(`CYRUS_MAILER_MAX', `M=CYRUS_MAILER_MAX, ')U=CYRUS_MAILER_USER, T=DNS/RFC822/X-Unix,_CYRUS_QGRP
		A=CYRUS_BB_MAILER_ARGS
