#!/bin/sh
# $Id: editbox,v 1.13 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

DIALOG_ERROR=254
export DIALOG_ERROR

. ./setup-edit

cat << EOF > $input
Hi, this is an edit box. It can be used to edit text from a file.

It's like a simple text editor, with these keys implemented:

PGDN	- Move down one page
PGUP	- Move up one page
DOWN	- Move down one line
UP	- Move up one line
DELETE	- Delete the current character
BACKSPC	- Delete the previous character

Unlike Xdialog, it does not do these:

CTRL C	- Copy text
CTRL V	- Paste text

Because dialog normally uses TAB for moving between fields,
this editbox uses CTRL/V as a literal-next character.  You
can enter TAB characters by first pressing CTRL/V.  This
example contains a few tab characters.

It supports the mouse - but only for positioning in the editbox,
or for clicking on buttons.  Your terminal (emulator) may support
cut/paste.

Try to input some text below:

EOF

$DIALOG --title "EDIT BOX" \
	--fixed-font "$@" --editbox $input 0 0 2>$output
retval=$?

. ./report-edit
