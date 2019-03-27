divert(-1)
#
# Copyright (c) 2003 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#
#

divert(0)
VERSIONID(`$Id: unicosmk.m4,v 1.2 2013-11-22 20:51:15 ca Exp $')
define(`ALIAS_FILE', `/usr/lib/aliases')
define(`HELP_FILE', `/usr/lib/sendmail.hf')
define(`QUEUE_DIR', `/usr/spool/mqueue')
define(`STATUS_FILE', `/usr/lib/sendmail.st')
MODIFY_MAILER_FLAGS(`LOCAL' , `+aSPpmnxXu')
MODIFY_MAILER_FLAGS(`SMTP', `+anpeLC')
define(`LOCAL_SHELL_FLAGS', `lsDFMpxehuo')
define(`confPID_FILE', `/etc/sendmail.pid')dnl
