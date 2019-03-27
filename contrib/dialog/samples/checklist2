#! /bin/sh
# $Id: checklist2,v 1.11 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --item-help --backtitle "No Such Organization" \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
UP/DOWN arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press SPACE to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
        "Apple"      "It's an apple."                          off "fruit" \
        "Dog"        "No, that's not my dog."                  ON  "not a fruit" \
        "Orange"     "Yeah, that's juicy."                     off "fruit" \
        "Chicken"    "Normally not a pet."                     off "not a fruit" \
        "Cat"        "No, never put a dog and a cat together!" oN  "not a fruit" \
        "Fish"       "Cats like fish."                         On  "not a fruit" \
        "Lemon"      "You know how it tastes."                 on  "the only one you wouldn't eat" 2> $tempfile

retval=$?

. ./report-tempfile
