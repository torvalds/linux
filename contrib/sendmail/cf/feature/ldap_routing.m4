divert(-1)
#
# Copyright (c) 1999-2002, 2004, 2007 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: ldap_routing.m4,v 8.21 2013-11-22 20:51:11 ca Exp $')
divert(-1)

# Check first two arguments.  If they aren't set, may need to warn in proto.m4
ifelse(len(X`'_ARG1_), `1', `define(`_LDAP_ROUTING_WARN_', `yes')')
ifelse(len(X`'_ARG2_), `1', `define(`_LDAP_ROUTING_WARN_', `yes')')
ifelse(len(X`'_ARG5_), `1', `', `define(`_LDAP_ROUTE_NODOMAIN_', `yes')')

# Check for third argument to indicate how to deal with non-existant
# LDAP records
ifelse(len(X`'_ARG3_), `1', `define(`_LDAP_ROUTING_', `_PASS_THROUGH_')',
       _ARG3_, `passthru', `define(`_LDAP_ROUTING_', `_PASS_THROUGH_')',
       _ARG3_, `sendertoo', `define(`_LDAP_ROUTING_', `_MUST_EXIST_')define(`_LDAP_SENDER_MUST_EXIST_')',
       `define(`_LDAP_ROUTING_', `_MUST_EXIST_')')

# Check for fourth argument to indicate how to deal with +detail info
ifelse(len(X`'_ARG4_), `1', `',
       _ARG4_, `strip', `define(`_LDAP_ROUTE_DETAIL_', `_STRIP_')',
       _ARG4_, `preserve', `define(`_LDAP_ROUTE_DETAIL_', `_PRESERVE_')')

# Check for sixth argument to indicate how to deal with tempfails
ifelse(len(X`'_ARG6_), `1', `define(`_LDAP_ROUTE_MAPTEMP_', `_QUEUE_')',
       _ARG6_, `tempfail', `define(`_LDAP_ROUTE_MAPTEMP_', `_TEMPFAIL_')',
       _ARG6_, `queue', `define(`_LDAP_ROUTE_MAPTEMP_', `_QUEUE_')')

define(`_NEED_SMTPOPMODES_', `1')

LOCAL_CONFIG
# LDAP routing maps
Kldapmh ifelse(len(X`'_ARG1_), `1',
	       `ldap -1 -T<TMPF> -v mailHost -k (&(objectClass=inetLocalMailRecipient)(mailLocalAddress=%0))',
	       `_ARG1_')

Kldapmra ifelse(len(X`'_ARG2_), `1',
		`ldap -1 -T<TMPF> -v mailRoutingAddress -k (&(objectClass=inetLocalMailRecipient)(mailLocalAddress=%0))',
		`_ARG2_')
