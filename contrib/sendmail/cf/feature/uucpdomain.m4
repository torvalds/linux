divert(-1)
#
# Copyright (c) 1998, 1999, 2001-2002 Proofpoint, Inc. and its suppliers.
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

divert(0)
VERSIONID(`$Id: uucpdomain.m4,v 8.30 2013-11-22 20:51:11 ca Exp $')
divert(-1)

define(`_UUDOMAIN_TABLE_', `')

LOCAL_CONFIG
# UUCP domain table
Kuudomain ifelse(defn(`_ARG_'), `', DATABASE_MAP_TYPE MAIL_SETTINGS_DIR`uudomain',
		 defn(`_ARG_'), `LDAP', `ldap -1 -v sendmailMTAMapValue,sendmailMTAMapSearch:FILTER:sendmailMTAMapObject,sendmailMTAMapURL:URL:sendmailMTAMapObject -k (&(objectClass=sendmailMTAMapObject)(|(sendmailMTACluster=${sendmailMTACluster})(sendmailMTAHost=$j))(sendmailMTAMapName=uucpdomain)(sendmailMTAKey=%0))',
		 `_ARG_')
