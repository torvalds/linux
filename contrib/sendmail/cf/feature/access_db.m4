divert(-1)
#
# Copyright (c) 1998-2002, 2004, 2006 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: access_db.m4,v 8.28 2013-11-22 20:51:11 ca Exp $')
divert(-1)

define(`_ACCESS_TABLE_', `')
define(`_TAG_DELIM_', `:')dnl should be in OperatorChars
ifelse(lower(_ARG2_),`skip',`define(`_ACCESS_SKIP_', `1')')
ifelse(lower(_ARG2_),`lookupdotdomain',`define(`_LOOKUPDOTDOMAIN_', `1')')
ifelse(lower(_ARG2_),`relaytofulladdress',`define(`_RELAY_FULL_ADDR_', `1')')
ifelse(lower(_ARG3_),`skip',`define(`_ACCESS_SKIP_', `1')')
ifelse(lower(_ARG3_),`lookupdotdomain',`define(`_LOOKUPDOTDOMAIN_', `1')')
ifelse(lower(_ARG3_),`relaytofulladdress',`define(`_RELAY_FULL_ADDR_', `1')')
ifelse(lower(_ARG4_),`skip',`define(`_ACCESS_SKIP_', `1')')
ifelse(lower(_ARG4_),`lookupdotdomain',`define(`_LOOKUPDOTDOMAIN_', `1')')
ifelse(lower(_ARG4_),`relaytofulladdress',`define(`_RELAY_FULL_ADDR_', `1')')
define(`_ATMPF_', `<TMPF>')dnl
dnl check whether arg contains -T`'_ATMPF_
dnl unless it is a sequence map
ifelse(defn(`_ARG_'), `', `',
  defn(`_ARG_'), `LDAP', `',
  `ifelse(index(_ARG_, `sequence '), `0', `',
    `ifelse(index(_ARG_, _ATMPF_), `-1',
      `errprint(`*** WARNING: missing -T'_ATMPF_` in argument of FEATURE(`access_db',' defn(`_ARG_')`)
')
      define(`_ABP_', index(_ARG_, ` '))
      define(`_NARG_', `substr(_ARG_, 0, _ABP_) -T'_ATMPF_` substr(_ARG_, _ABP_)')
      ')
    ')
  ')
ifdef(`_GREET_PAUSE_',
	`errprint(`*** WARNING: FEATURE(`greet_pause') before FEATURE(`access_db')
	greet_pause will not use access_db!')')

LOCAL_CONFIG
# Access list database (for spam stomping)
Kaccess ifelse(defn(`_ARG_'), `', DATABASE_MAP_TYPE -T`'_ATMPF_ MAIL_SETTINGS_DIR`access',
	       defn(`_ARG_'), `LDAP', `ldap -T`'_ATMPF_ -1 -v sendmailMTAMapValue,sendmailMTAMapSearch:FILTER:sendmailMTAMapObject,sendmailMTAMapURL:URL:sendmailMTAMapObject -k (&(objectClass=sendmailMTAMapObject)(|(sendmailMTACluster=${sendmailMTACluster})(sendmailMTAHost=$j))(sendmailMTAMapName=access)(sendmailMTAKey=%0))',
	       defn(`_NARG_'), `', `_ARG_', `_NARG_')
