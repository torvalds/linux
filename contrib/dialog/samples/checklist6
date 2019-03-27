#! /bin/sh
# $Id: checklist6,v 1.11 2010/01/13 10:20:03 tom Exp $
# example showing the --colors option

. ./setup-vars

. ./setup-tempfile

$DIALOG --help-button --item-help --colors --backtitle "\Z1No Such\Zn Organization" \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
\Z4UP/DOWN\Zn arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press \Zb\ZrSPACE\Zn to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
        "Apple"      "It's an \Z1apple\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                          off "fruit" \
        "Dog"        "No, that's not my dog. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                  ON  "not a fruit" \
        "Orange"     "Yeah, that's juicy. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                     off "fruit" \
        "Chicken"    "Normally not a pet. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                     off "not a fruit" \
        ""           "No such pet. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                            off "not anything" \
        "Cat"        "No, never put a dog and a cat together! xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" oN  "not a fruit" \
        "Fish"       "Cats like \Z4fish\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                         On  "not a fruit" \
        "Lemon"      "You know how it \Zr\Zb\Z3tastes\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                 on  "the only one you wouldn't eat" 2> $tempfile

retval=$?

. ./report-tempfile
