#!/bin/sh
# $Id: menubox11,v 1.2 2018/06/13 21:59:21 tom Exp $
# zero-width column

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --backtitle "Debian Configuration" \
	--title "Configuring debconf" \
	--default-item Dialog "$@" \
	--menu "Packages that use debconf for configuration share a common look and feel. You can 
select the type of user interface they use.
\n\n\
The dialog frontend is a full-screen, character based interface, while the readline 
frontend uses a more traditional plain text interface, and both the gnome and kde 
frontends are modern X interfaces, fitting the respective desktops (but may be used 
in any X environment). The editor frontend lets you configure things using your 
favorite text editor. The noninteractive frontend never asks you any questions.
\n\n\
Interface to use:" 0 0 6 \
	Dialog		"" \
	Readline	"" \
	Gnome		"" \
	Kde		"" \
	Editor		"" \
	Noninteractive	"" \
2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
