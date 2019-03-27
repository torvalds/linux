divert(-1)
#
# Copyright (c) 2000 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#
#	This ostype file is suitable for use on Solaris 8 and later systems,
#	taking advantage of mail.local's LMTP support, the existence of
#	/var/run and support for IPv6, all of which where introduced in
#	Solaris 8.
#

divert(0)
VERSIONID(`$Id: solaris8.m4,v 8.3 2013-11-22 20:51:15 ca Exp $')
divert(-1)

ifdef(`UUCP_MAILER_ARGS',, `define(`UUCP_MAILER_ARGS', `uux - -r -a$g $h!rmail ($u)')')
define(`confEBINDIR', `/usr/lib')dnl
define(`confPID_FILE', `/var/run/sendmail.pid')dnl
define(`_NETINET6_')dnl
FEATURE(`local_lmtp')dnl
