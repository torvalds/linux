divert(-1)
#
# Copyright (c) 2011 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#	This ostype file is suitable for use on Solaris 11 and later systems,
#	make use of /system/volatile, introduced in Solaris 11.
#

divert(0)
VERSIONID(`$Id: solaris11.m4,v 1.2 2013-11-22 20:51:15 ca Exp $')
divert(-1)

ifdef(`UUCP_MAILER_ARGS',, `define(`UUCP_MAILER_ARGS', `uux - -r -a$g $h!rmail ($u)')')
define(`confEBINDIR', `/usr/lib')dnl
define(`confPID_FILE', `/system/volatile/sendmail.pid')dnl
define(`_NETINET6_')dnl
FEATURE(`local_lmtp')dnl
