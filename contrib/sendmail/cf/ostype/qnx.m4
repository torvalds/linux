divert(-1)
#
# Copyright (c) 1998, 1999 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1997 Eric P. Allman.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#
#	Contributed by Glen McCready <glen@qnx.com>
#

divert(0)
VERSIONID(`$Id: qnx.m4,v 8.14 2013-11-22 20:51:15 ca Exp $')
define(`QUEUE_DIR', /usr/spool/mqueue)dnl
define(`LOCAL_MAILER_ARGS', `mail $u')dnl
_DEFIFNOT(`LOCAL_MAILER_FLAGS', `Sh')dnl
define(`LOCAL_MAILER_PATH', /usr/bin/mailx)dnl
define(`UUCP_MAILER_ARGS', `uux - -r -z -a$f $h!rmail ($u)')dnl
