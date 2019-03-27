#!/bin/sh
# $Id: testdata-8bit,v 1.2 2011/10/16 23:26:32 tom Exp $

# Select one of the "SAMPLE=" lines, to test handling of characters which
# are nonprinting in a POSIX locale:

case .$1 in
	# C1 controls
.8)
	SAMPLE="€‚ƒ„…†‡ˆ‰Š‹Œ"
	;;
.9)
	SAMPLE="‘’“”•–—˜™š›œŸ"
	;;

# Latin-1
.[aA])
	SAMPLE=" ¡¢£¤¥¦§¨©ª«¬­®¯"
	;;
.[bB])
	SAMPLE="°±²³´µ¶·¸¹º»¼½¾¿"
	;;
.[cC])
	SAMPLE="ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ"
	;;
.[dD])
	SAMPLE="ĞÑÒÓÔÕÖ×ØÙÚÛÜİŞß"
	;;
.[eE])
	SAMPLE="àáâãäåæçèéêëìíîï"
	;;
.[fF])
	SAMPLE="ğñòóôõö÷øùúûüışÿ"
	;;
*)
	# C0 controls (except a few which are always treated specially by curses):
	SAMPLE=""
	;;
esac

# This script is source'd from other scripts, and uses the parameter list from
# those explicitly.  But they may use the parameter list later, to set options
# specially for dialog.  Work around the conflicting uses by removing the
# parameter which we just used to select a set of data.
if test $# != 0
then
	shift 1
fi
