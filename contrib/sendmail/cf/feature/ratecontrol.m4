divert(-1)
#
# Copyright (c) 2003, 2004 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: ratecontrol.m4,v 1.6 2013-11-22 20:51:11 ca Exp $')

divert(-1)
ifdef(`_ACCESS_TABLE_', `
	define(`_RATE_CONTROL_', `1')
	ifelse(defn(`_ARG_'), `', `',
		strcasecmp(defn(`_ARG_'), `nodelay'), `1',
		`ifdef(`_DELAY_CHECKS_',
			`
			define(`_RATE_CONTROL_IMMEDIATE_', `1')
			define(`_CONTROL_IMMEDIATE_', `1')
			',
			`errprint(`*** ERROR: FEATURE(`ratecontrol', `nodelay') requires FEATURE(`delay_checks')')'
		)',
		`errprint(`*** ERROR: unknown parameter '"defn(`_ARG_')"` for FEATURE(`ratecontrol')')')
	define(`_FFR_SRCHLIST_A', `1')
	ifelse(len(X`'_ARG2_), `1', `',
		_ARG2_, `terminate', `define(`_RATE_CONTROL_REPLY', `421')',
		`errprint(`*** ERROR: FEATURE(`ratecontrol'): unknown argument '"_ARG2_"
)'
		)
	', `errprint(`*** ERROR: FEATURE(`ratecontrol') requires FEATURE(`access_db')
')')
ifdef(`_RATE_CONTROL_REPLY',,`define(`_RATE_CONTROL_REPLY', `452')')
