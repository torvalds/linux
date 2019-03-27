divert(-1)
#
# Copyright (c) 1998-1999, 2001 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: promiscuous_relay.m4,v 8.13 2013-11-22 20:51:11 ca Exp $')
divert(-1)

define(`_PROMISCUOUS_RELAY_', 1)
errprint(`*** WARNING: FEATURE(`promiscuous_relay') configures your system as open
	relay.  Do NOT use it on a server that is connected to the Internet!
')
