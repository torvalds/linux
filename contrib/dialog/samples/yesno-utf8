#! /bin/sh
# $Id: yesno-utf8,v 1.7 2010/01/14 01:11:23 tom Exp $

. ./setup-vars

. ./setup-utf8

DIALOG_ERROR=254
export DIALOG_ERROR

$DIALOG "$@" --yesno "Are you a ＤＯＧ?" 0 0
retval=$?

. ./report-yesno
