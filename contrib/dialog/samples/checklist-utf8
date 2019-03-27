#! /bin/sh
# $Id: checklist-utf8,v 1.12 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

. ./setup-utf8

$DIALOG --backtitle "No Such Organization" \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
UP/DOWN arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press SPACE to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
	"ＡＰＰＬＥ"	 "It's an ＡＰＰＬＥ." off \
	"ＤＯＧ"	 "No, that's not my ＤＯＧ." ON \
	"ＯＲＡＮＧＥ"	 "Yeah, that's ＪＵＩＣＹ." off \
	"ＣＨＩＣＫＥＮ" "Normally not a ＰＥＴ." off \
	"ＣＡＴ"	 "No, never put a ＤＯＧ and a ＣＡＴ together!" oN \
	"ＦＩＳＨ"	 "Cats like ＦＩＳＨ." On \
	"ＬＥＭＯＮ"	 "You ＫＮＯＷ how it ＴＡＳＴＥＳ." on 2> $tempfile

retval=$?

. ./report-tempfile
