divert(-1)
#
# Copyright (c) 1998, 1999 Proofpoint, Inc. and its suppliers.
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
#	This ostype file is suitable for use on Solaris 2.x systems that
#	have mail.local installed.  It is my understanding that this is
#	standard as of Solaris 2.5.
#

divert(0)
VERSIONID(`$Id: solaris2.m4,v 8.23 2013-11-22 20:51:15 ca Exp $')
divert(-1)

ifdef(`LOCAL_MAILER_PATH',, `define(`LOCAL_MAILER_PATH', `/usr/lib/mail.local')')
_DEFIFNOT(`LOCAL_MAILER_FLAGS', `fSmn9')
ifdef(`LOCAL_MAILER_ARGS',, `define(`LOCAL_MAILER_ARGS', `mail.local -d $u')')
ifdef(`UUCP_MAILER_ARGS',, `define(`UUCP_MAILER_ARGS', `uux - -r -a$g $h!rmail ($u)')')
define(`confEBINDIR', `/usr/lib')dnl
