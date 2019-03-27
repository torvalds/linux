#! /bin/sh
# $Id: checklist8,v 1.10 2010/01/13 10:20:03 tom Exp $
# "checklist7" without --item-help

. ./setup-vars

. ./setup-tempfile

$DIALOG --help-button \
	--colors \
	--backtitle "\Z1No Such\Zn Organization" \
	--separate-output \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
\Z4UP/DOWN\Zn arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press \Zb\ZrSPACE\Zn to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
        "Apple"      "It's an \Z1apple\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                    off \
        "Dog"        "No, that's not my dog. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                  ON  \
        "Orange"     "Yeah, that's juicy. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                     off \
        "Chicken"    "Normally not a pet. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                     off \
        ""           "No such pet. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                            off \
        "Cat"        "No, never put a dog and a cat together! xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" oN  \
        "Fish"       "Cats like \Z4fish\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                   On  \
        "Lemon"      "You know how it \Zr\Zb\Z3tastes\Zn. xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"     on  2> $tempfile

retval=$?

. ./report-tempfile
