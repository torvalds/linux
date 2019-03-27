This is gdb.info, produced by makeinfo version 4.6 from ./gdb.texinfo.

INFO-DIR-SECTION Software development
START-INFO-DIR-ENTRY
* Gdb: (gdb).                     The GNU debugger.
END-INFO-DIR-ENTRY

   This file documents the GNU debugger GDB.

   This is the Ninth Edition, of `Debugging with GDB: the GNU
Source-Level Debugger' for GDB Version 6.1.1.

   Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
1998,
1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   Permission is granted to copy, distribute and/or modify this document
under the terms of the GNU Free Documentation License, Version 1.1 or
any later version published by the Free Software Foundation; with the
Invariant Sections being "Free Software" and "Free Software Needs Free
Documentation", with the Front-Cover Texts being "A GNU Manual," and
with the Back-Cover Texts as in (a) below.

   (a) The Free Software Foundation's Back-Cover Text is: "You have
freedom to copy and modify this GNU Manual, like GNU software.  Copies
published by the Free Software Foundation raise funds for GNU
development."


File: gdb.info,  Node: Searching,  Prev: Readline Arguments,  Up: Readline Interaction

Searching for Commands in the History
-------------------------------------

Readline provides commands for searching through the command history
for lines containing a specified string.  There are two search modes:
"incremental" and "non-incremental".

   Incremental searches begin before the user has finished typing the
search string.  As each character of the search string is typed,
Readline displays the next entry from the history matching the string
typed so far.  An incremental search requires only as many characters
as needed to find the desired history entry.  To search backward in the
history for a particular string, type `C-r'.  Typing `C-s' searches
forward through the history.  The characters present in the value of
the `isearch-terminators' variable are used to terminate an incremental
search.  If that variable has not been assigned a value, the <ESC> and
`C-J' characters will terminate an incremental search.  `C-g' will
abort an incremental search and restore the original line.  When the
search is terminated, the history entry containing the search string
becomes the current line.

   To find other matching entries in the history list, type `C-r' or
`C-s' as appropriate.  This will search backward or forward in the
history for the next entry matching the search string typed so far.
Any other key sequence bound to a Readline command will terminate the
search and execute that command.  For instance, a <RET> will terminate
the search and accept the line, thereby executing the command from the
history list.  A movement command will terminate the search, make the
last line found the current line, and begin editing.

   Readline remembers the last incremental search string.  If two
`C-r's are typed without any intervening characters defining a new
search string, any remembered search string is used.

   Non-incremental searches read the entire search string before
starting to search for matching history lines.  The search string may be
typed by the user or be part of the contents of the current line.


File: gdb.info,  Node: Readline Init File,  Next: Bindable Readline Commands,  Prev: Readline Interaction,  Up: Command Line Editing

Readline Init File
==================

Although the Readline library comes with a set of Emacs-like
keybindings installed by default, it is possible to use a different set
of keybindings.  Any user can customize programs that use Readline by
putting commands in an "inputrc" file, conventionally in his home
directory.  The name of this file is taken from the value of the
environment variable `INPUTRC'.  If that variable is unset, the default
is `~/.inputrc'.

   When a program which uses the Readline library starts up, the init
file is read, and the key bindings are set.

   In addition, the `C-x C-r' command re-reads this init file, thus
incorporating any changes that you might have made to it.

* Menu:

* Readline Init File Syntax::	Syntax for the commands in the inputrc file.

* Conditional Init Constructs::	Conditional key bindings in the inputrc file.

* Sample Init File::		An example inputrc file.


File: gdb.info,  Node: Readline Init File Syntax,  Next: Conditional Init Constructs,  Up: Readline Init File

Readline Init File Syntax
-------------------------

There are only a few basic constructs allowed in the Readline init
file.  Blank lines are ignored.  Lines beginning with a `#' are
comments.  Lines beginning with a `$' indicate conditional constructs
(*note Conditional Init Constructs::).  Other lines denote variable
settings and key bindings.

Variable Settings
     You can modify the run-time behavior of Readline by altering the
     values of variables in Readline using the `set' command within the
     init file.  The syntax is simple:

          set VARIABLE VALUE

     Here, for example, is how to change from the default Emacs-like
     key binding to use `vi' line editing commands:

          set editing-mode vi

     Variable names and values, where appropriate, are recognized
     without regard to case.

     A great deal of run-time behavior is changeable with the following
     variables.

    `bell-style'
          Controls what happens when Readline wants to ring the
          terminal bell.  If set to `none', Readline never rings the
          bell.  If set to `visible', Readline uses a visible bell if
          one is available.  If set to `audible' (the default),
          Readline attempts to ring the terminal's bell.

    `comment-begin'
          The string to insert at the beginning of the line when the
          `insert-comment' command is executed.  The default value is
          `"#"'.

    `completion-ignore-case'
          If set to `on', Readline performs filename matching and
          completion in a case-insensitive fashion.  The default value
          is `off'.

    `completion-query-items'
          The number of possible completions that determines when the
          user is asked whether he wants to see the list of
          possibilities.  If the number of possible completions is
          greater than this value, Readline will ask the user whether
          or not he wishes to view them; otherwise, they are simply
          listed.  This variable must be set to an integer value
          greater than or equal to 0.  The default limit is `100'.

    `convert-meta'
          If set to `on', Readline will convert characters with the
          eighth bit set to an ASCII key sequence by stripping the
          eighth bit and prefixing an <ESC> character, converting them
          to a meta-prefixed key sequence.  The default value is `on'.

    `disable-completion'
          If set to `On', Readline will inhibit word completion.
          Completion  characters will be inserted into the line as if
          they had been mapped to `self-insert'.  The default is `off'.

    `editing-mode'
          The `editing-mode' variable controls which default set of key
          bindings is used.  By default, Readline starts up in Emacs
          editing mode, where the keystrokes are most similar to Emacs.
          This variable can be set to either `emacs' or `vi'.

    `enable-keypad'
          When set to `on', Readline will try to enable the application
          keypad when it is called.  Some systems need this to enable
          the arrow keys.  The default is `off'.

    `expand-tilde'
          If set to `on', tilde expansion is performed when Readline
          attempts word completion.  The default is `off'.

          If set to `on', the history code attempts to place point at
          the same location on each history line retrived with
          `previous-history' or `next-history'.

    `horizontal-scroll-mode'
          This variable can be set to either `on' or `off'.  Setting it
          to `on' means that the text of the lines being edited will
          scroll horizontally on a single screen line when they are
          longer than the width of the screen, instead of wrapping onto
          a new screen line.  By default, this variable is set to `off'.

    `input-meta'
          If set to `on', Readline will enable eight-bit input (it will
          not clear the eighth bit in the characters it reads),
          regardless of what the terminal claims it can support.  The
          default value is `off'.  The name `meta-flag' is a synonym
          for this variable.

    `isearch-terminators'
          The string of characters that should terminate an incremental
          search without subsequently executing the character as a
          command (*note Searching::).  If this variable has not been
          given a value, the characters <ESC> and `C-J' will terminate
          an incremental search.

    `keymap'
          Sets Readline's idea of the current keymap for key binding
          commands.  Acceptable `keymap' names are `emacs',
          `emacs-standard', `emacs-meta', `emacs-ctlx', `vi', `vi-move',
          `vi-command', and `vi-insert'.  `vi' is equivalent to
          `vi-command'; `emacs' is equivalent to `emacs-standard'.  The
          default value is `emacs'.  The value of the `editing-mode'
          variable also affects the default keymap.

    `mark-directories'
          If set to `on', completed directory names have a slash
          appended.  The default is `on'.

    `mark-modified-lines'
          This variable, when set to `on', causes Readline to display an
          asterisk (`*') at the start of history lines which have been
          modified.  This variable is `off' by default.

    `mark-symlinked-directories'
          If set to `on', completed names which are symbolic links to
          directories have a slash appended (subject to the value of
          `mark-directories').  The default is `off'.

    `match-hidden-files'
          This variable, when set to `on', causes Readline to match
          files whose names begin with a `.' (hidden files) when
          performing filename completion, unless the leading `.' is
          supplied by the user in the filename to be completed.  This
          variable is `on' by default.

    `output-meta'
          If set to `on', Readline will display characters with the
          eighth bit set directly rather than as a meta-prefixed escape
          sequence.  The default is `off'.

    `page-completions'
          If set to `on', Readline uses an internal `more'-like pager
          to display a screenful of possible completions at a time.
          This variable is `on' by default.

    `print-completions-horizontally'
          If set to `on', Readline will display completions with matches
          sorted horizontally in alphabetical order, rather than down
          the screen.  The default is `off'.

    `show-all-if-ambiguous'
          This alters the default behavior of the completion functions.
          If set to `on', words which have more than one possible
          completion cause the matches to be listed immediately instead
          of ringing the bell.  The default value is `off'.

    `visible-stats'
          If set to `on', a character denoting a file's type is
          appended to the filename when listing possible completions.
          The default is `off'.


Key Bindings
     The syntax for controlling key bindings in the init file is
     simple.  First you need to find the name of the command that you
     want to change.  The following sections contain tables of the
     command name, the default keybinding, if any, and a short
     description of what the command does.

     Once you know the name of the command, simply place on a line in
     the init file the name of the key you wish to bind the command to,
     a colon, and then the name of the command.  The name of the key
     can be expressed in different ways, depending on what you find most
     comfortable.

     In addition to command names, readline allows keys to be bound to
     a string that is inserted when the key is pressed (a MACRO).

    KEYNAME: FUNCTION-NAME or MACRO
          KEYNAME is the name of a key spelled out in English.  For
          example:
               Control-u: universal-argument
               Meta-Rubout: backward-kill-word
               Control-o: "> output"

          In the above example, `C-u' is bound to the function
          `universal-argument', `M-DEL' is bound to the function
          `backward-kill-word', and `C-o' is bound to run the macro
          expressed on the right hand side (that is, to insert the text
          `> output' into the line).

          A number of symbolic character names are recognized while
          processing this key binding syntax: DEL, ESC, ESCAPE, LFD,
          NEWLINE, RET, RETURN, RUBOUT, SPACE, SPC, and TAB.

    "KEYSEQ": FUNCTION-NAME or MACRO
          KEYSEQ differs from KEYNAME above in that strings denoting an
          entire key sequence can be specified, by placing the key
          sequence in double quotes.  Some GNU Emacs style key escapes
          can be used, as in the following example, but the special
          character names are not recognized.

               "\C-u": universal-argument
               "\C-x\C-r": re-read-init-file
               "\e[11~": "Function Key 1"

          In the above example, `C-u' is again bound to the function
          `universal-argument' (just as it was in the first example),
          `C-x C-r' is bound to the function `re-read-init-file', and
          `<ESC> <[> <1> <1> <~>' is bound to insert the text `Function
          Key 1'.


     The following GNU Emacs style escape sequences are available when
     specifying key sequences:

    `\C-'
          control prefix

    `\M-'
          meta prefix

    `\e'
          an escape character

    `\\'
          backslash

    `\"'
          <">, a double quotation mark

    `\''
          <'>, a single quote or apostrophe

     In addition to the GNU Emacs style escape sequences, a second set
     of backslash escapes is available:

    `\a'
          alert (bell)

    `\b'
          backspace

    `\d'
          delete

    `\f'
          form feed

    `\n'
          newline

    `\r'
          carriage return

    `\t'
          horizontal tab

    `\v'
          vertical tab

    `\NNN'
          the eight-bit character whose value is the octal value NNN
          (one to three digits)

    `\xHH'
          the eight-bit character whose value is the hexadecimal value
          HH (one or two hex digits)

     When entering the text of a macro, single or double quotes must be
     used to indicate a macro definition.  Unquoted text is assumed to
     be a function name.  In the macro body, the backslash escapes
     described above are expanded.  Backslash will quote any other
     character in the macro text, including `"' and `''.  For example,
     the following binding will make `C-x \' insert a single `\' into
     the line:
          "\C-x\\": "\\"



File: gdb.info,  Node: Conditional Init Constructs,  Next: Sample Init File,  Prev: Readline Init File Syntax,  Up: Readline Init File

Conditional Init Constructs
---------------------------

Readline implements a facility similar in spirit to the conditional
compilation features of the C preprocessor which allows key bindings
and variable settings to be performed as the result of tests.  There
are four parser directives used.

`$if'
     The `$if' construct allows bindings to be made based on the
     editing mode, the terminal being used, or the application using
     Readline.  The text of the test extends to the end of the line; no
     characters are required to isolate it.

    `mode'
          The `mode=' form of the `$if' directive is used to test
          whether Readline is in `emacs' or `vi' mode.  This may be
          used in conjunction with the `set keymap' command, for
          instance, to set bindings in the `emacs-standard' and
          `emacs-ctlx' keymaps only if Readline is starting out in
          `emacs' mode.

    `term'
          The `term=' form may be used to include terminal-specific key
          bindings, perhaps to bind the key sequences output by the
          terminal's function keys.  The word on the right side of the
          `=' is tested against both the full name of the terminal and
          the portion of the terminal name before the first `-'.  This
          allows `sun' to match both `sun' and `sun-cmd', for instance.

    `application'
          The APPLICATION construct is used to include
          application-specific settings.  Each program using the
          Readline library sets the APPLICATION NAME, and you can test
          for a particular value.  This could be used to bind key
          sequences to functions useful for a specific program.  For
          instance, the following command adds a key sequence that
          quotes the current or previous word in Bash:
               $if Bash
               # Quote the current or previous word
               "\C-xq": "\eb\"\ef\""
               $endif

`$endif'
     This command, as seen in the previous example, terminates an `$if'
     command.

`$else'
     Commands in this branch of the `$if' directive are executed if the
     test fails.

`$include'
     This directive takes a single filename as an argument and reads
     commands and bindings from that file.  For example, the following
     directive reads from `/etc/inputrc':
          $include /etc/inputrc


File: gdb.info,  Node: Sample Init File,  Prev: Conditional Init Constructs,  Up: Readline Init File

Sample Init File
----------------

Here is an example of an INPUTRC file.  This illustrates key binding,
variable assignment, and conditional syntax.


     # This file controls the behaviour of line input editing for
     # programs that use the GNU Readline library.  Existing
     # programs include FTP, Bash, and GDB.
     #
     # You can re-read the inputrc file with C-x C-r.
     # Lines beginning with '#' are comments.
     #
     # First, include any systemwide bindings and variable
     # assignments from /etc/Inputrc
     $include /etc/Inputrc
     
     #
     # Set various bindings for emacs mode.
     
     set editing-mode emacs
     
     $if mode=emacs
     
     Meta-Control-h:	backward-kill-word	Text after the function name is ignored
     
     #
     # Arrow keys in keypad mode
     #
     #"\M-OD":        backward-char
     #"\M-OC":        forward-char
     #"\M-OA":        previous-history
     #"\M-OB":        next-history
     #
     # Arrow keys in ANSI mode
     #
     "\M-[D":        backward-char
     "\M-[C":        forward-char
     "\M-[A":        previous-history
     "\M-[B":        next-history
     #
     # Arrow keys in 8 bit keypad mode
     #
     #"\M-\C-OD":       backward-char
     #"\M-\C-OC":       forward-char
     #"\M-\C-OA":       previous-history
     #"\M-\C-OB":       next-history
     #
     # Arrow keys in 8 bit ANSI mode
     #
     #"\M-\C-[D":       backward-char
     #"\M-\C-[C":       forward-char
     #"\M-\C-[A":       previous-history
     #"\M-\C-[B":       next-history
     
     C-q: quoted-insert
     
     $endif
     
     # An old-style binding.  This happens to be the default.
     TAB: complete
     
     # Macros that are convenient for shell interaction
     $if Bash
     # edit the path
     "\C-xp": "PATH=${PATH}\e\C-e\C-a\ef\C-f"
     # prepare to type a quoted word --
     # insert open and close double quotes
     # and move to just after the open quote
     "\C-x\"": "\"\"\C-b"
     # insert a backslash (testing backslash escapes
     # in sequences and macros)
     "\C-x\\": "\\"
     # Quote the current or previous word
     "\C-xq": "\eb\"\ef\""
     # Add a binding to refresh the line, which is unbound
     "\C-xr": redraw-current-line
     # Edit variable on current line.
     "\M-\C-v": "\C-a\C-k$\C-y\M-\C-e\C-a\C-y="
     $endif
     
     # use a visible bell if one is available
     set bell-style visible
     
     # don't strip characters to 7 bits when reading
     set input-meta on
     
     # allow iso-latin1 characters to be inserted rather
     # than converted to prefix-meta sequences
     set convert-meta off
     
     # display characters with the eighth bit set directly
     # rather than as meta-prefixed characters
     set output-meta on
     
     # if there are more than 150 possible completions for
     # a word, ask the user if he wants to see all of them
     set completion-query-items 150
     
     # For FTP
     $if Ftp
     "\C-xg": "get \M-?"
     "\C-xt": "put \M-?"
     "\M-.": yank-last-arg
     $endif


File: gdb.info,  Node: Bindable Readline Commands,  Next: Readline vi Mode,  Prev: Readline Init File,  Up: Command Line Editing

Bindable Readline Commands
==========================

* Menu:

* Commands For Moving::		Moving about the line.
* Commands For History::	Getting at previous lines.
* Commands For Text::		Commands for changing text.
* Commands For Killing::	Commands for killing and yanking.
* Numeric Arguments::		Specifying numeric arguments, repeat counts.
* Commands For Completion::	Getting Readline to do the typing for you.
* Keyboard Macros::		Saving and re-executing typed characters
* Miscellaneous Commands::	Other miscellaneous commands.

   This section describes Readline commands that may be bound to key
sequences.  Command names without an accompanying key sequence are
unbound by default.

   In the following descriptions, "point" refers to the current cursor
position, and "mark" refers to a cursor position saved by the
`set-mark' command.  The text between the point and mark is referred to
as the "region".


File: gdb.info,  Node: Commands For Moving,  Next: Commands For History,  Up: Bindable Readline Commands

Commands For Moving
-------------------

`beginning-of-line (C-a)'
     Move to the start of the current line.

`end-of-line (C-e)'
     Move to the end of the line.

`forward-char (C-f)'
     Move forward a character.

`backward-char (C-b)'
     Move back a character.

`forward-word (M-f)'
     Move forward to the end of the next word.  Words are composed of
     letters and digits.

`backward-word (M-b)'
     Move back to the start of the current or previous word.  Words are
     composed of letters and digits.

`clear-screen (C-l)'
     Clear the screen and redraw the current line, leaving the current
     line at the top of the screen.

`redraw-current-line ()'
     Refresh the current line.  By default, this is unbound.



File: gdb.info,  Node: Commands For History,  Next: Commands For Text,  Prev: Commands For Moving,  Up: Bindable Readline Commands

Commands For Manipulating The History
-------------------------------------

`accept-line (Newline or Return)'
     Accept the line regardless of where the cursor is.  If this line is
     non-empty, it may be added to the history list for future recall
     with `add_history()'.  If this line is a modified history line,
     the history line is restored to its original state.

`previous-history (C-p)'
     Move `back' through the history list, fetching the previous
     command.

`next-history (C-n)'
     Move `forward' through the history list, fetching the next command.

`beginning-of-history (M-<)'
     Move to the first line in the history.

`end-of-history (M->)'
     Move to the end of the input history, i.e., the line currently
     being entered.

`reverse-search-history (C-r)'
     Search backward starting at the current line and moving `up'
     through the history as necessary.  This is an incremental search.

`forward-search-history (C-s)'
     Search forward starting at the current line and moving `down'
     through the the history as necessary.  This is an incremental
     search.

`non-incremental-reverse-search-history (M-p)'
     Search backward starting at the current line and moving `up'
     through the history as necessary using a non-incremental search
     for a string supplied by the user.

`non-incremental-forward-search-history (M-n)'
     Search forward starting at the current line and moving `down'
     through the the history as necessary using a non-incremental search
     for a string supplied by the user.

`history-search-forward ()'
     Search forward through the history for the string of characters
     between the start of the current line and the point.  This is a
     non-incremental search.  By default, this command is unbound.

`history-search-backward ()'
     Search backward through the history for the string of characters
     between the start of the current line and the point.  This is a
     non-incremental search.  By default, this command is unbound.

`yank-nth-arg (M-C-y)'
     Insert the first argument to the previous command (usually the
     second word on the previous line) at point.  With an argument N,
     insert the Nth word from the previous command (the words in the
     previous command begin with word 0).  A negative argument inserts
     the Nth word from the end of the previous command.

`yank-last-arg (M-. or M-_)'
     Insert last argument to the previous command (the last word of the
     previous history entry).  With an argument, behave exactly like
     `yank-nth-arg'.  Successive calls to `yank-last-arg' move back
     through the history list, inserting the last argument of each line
     in turn.



File: gdb.info,  Node: Commands For Text,  Next: Commands For Killing,  Prev: Commands For History,  Up: Bindable Readline Commands

Commands For Changing Text
--------------------------

`delete-char (C-d)'
     Delete the character at point.  If point is at the beginning of
     the line, there are no characters in the line, and the last
     character typed was not bound to `delete-char', then return EOF.

`backward-delete-char (Rubout)'
     Delete the character behind the cursor.  A numeric argument means
     to kill the characters instead of deleting them.

`forward-backward-delete-char ()'
     Delete the character under the cursor, unless the cursor is at the
     end of the line, in which case the character behind the cursor is
     deleted.  By default, this is not bound to a key.

`quoted-insert (C-q or C-v)'
     Add the next character typed to the line verbatim.  This is how to
     insert key sequences like `C-q', for example.

`tab-insert (M-<TAB>)'
     Insert a tab character.

`self-insert (a, b, A, 1, !, ...)'
     Insert yourself.

`transpose-chars (C-t)'
     Drag the character before the cursor forward over the character at
     the cursor, moving the cursor forward as well.  If the insertion
     point is at the end of the line, then this transposes the last two
     characters of the line.  Negative arguments have no effect.

`transpose-words (M-t)'
     Drag the word before point past the word after point, moving point
     past that word as well.  If the insertion point is at the end of
     the line, this transposes the last two words on the line.

`upcase-word (M-u)'
     Uppercase the current (or following) word.  With a negative
     argument, uppercase the previous word, but do not move the cursor.

`downcase-word (M-l)'
     Lowercase the current (or following) word.  With a negative
     argument, lowercase the previous word, but do not move the cursor.

`capitalize-word (M-c)'
     Capitalize the current (or following) word.  With a negative
     argument, capitalize the previous word, but do not move the cursor.

`overwrite-mode ()'
     Toggle overwrite mode.  With an explicit positive numeric argument,
     switches to overwrite mode.  With an explicit non-positive numeric
     argument, switches to insert mode.  This command affects only
     `emacs' mode; `vi' mode does overwrite differently.  Each call to
     `readline()' starts in insert mode.

     In overwrite mode, characters bound to `self-insert' replace the
     text at point rather than pushing the text to the right.
     Characters bound to `backward-delete-char' replace the character
     before point with a space.

     By default, this command is unbound.



File: gdb.info,  Node: Commands For Killing,  Next: Numeric Arguments,  Prev: Commands For Text,  Up: Bindable Readline Commands

Killing And Yanking
-------------------

`kill-line (C-k)'
     Kill the text from point to the end of the line.

`backward-kill-line (C-x Rubout)'
     Kill backward to the beginning of the line.

`unix-line-discard (C-u)'
     Kill backward from the cursor to the beginning of the current line.

`kill-whole-line ()'
     Kill all characters on the current line, no matter where point is.
     By default, this is unbound.

`kill-word (M-d)'
     Kill from point to the end of the current word, or if between
     words, to the end of the next word.  Word boundaries are the same
     as `forward-word'.

`backward-kill-word (M-<DEL>)'
     Kill the word behind point.  Word boundaries are the same as
     `backward-word'.

`unix-word-rubout (C-w)'
     Kill the word behind point, using white space as a word boundary.
     The killed text is saved on the kill-ring.

`delete-horizontal-space ()'
     Delete all spaces and tabs around point.  By default, this is
     unbound.

`kill-region ()'
     Kill the text in the current region.  By default, this command is
     unbound.

`copy-region-as-kill ()'
     Copy the text in the region to the kill buffer, so it can be yanked
     right away.  By default, this command is unbound.

`copy-backward-word ()'
     Copy the word before point to the kill buffer.  The word
     boundaries are the same as `backward-word'.  By default, this
     command is unbound.

`copy-forward-word ()'
     Copy the word following point to the kill buffer.  The word
     boundaries are the same as `forward-word'.  By default, this
     command is unbound.

`yank (C-y)'
     Yank the top of the kill ring into the buffer at point.

`yank-pop (M-y)'
     Rotate the kill-ring, and yank the new top.  You can only do this
     if the prior command is `yank' or `yank-pop'.


File: gdb.info,  Node: Numeric Arguments,  Next: Commands For Completion,  Prev: Commands For Killing,  Up: Bindable Readline Commands

Specifying Numeric Arguments
----------------------------

`digit-argument (M-0, M-1, ... M--)'
     Add this digit to the argument already accumulating, or start a new
     argument.  `M--' starts a negative argument.

`universal-argument ()'
     This is another way to specify an argument.  If this command is
     followed by one or more digits, optionally with a leading minus
     sign, those digits define the argument.  If the command is
     followed by digits, executing `universal-argument' again ends the
     numeric argument, but is otherwise ignored.  As a special case, if
     this command is immediately followed by a character that is
     neither a digit or minus sign, the argument count for the next
     command is multiplied by four.  The argument count is initially
     one, so executing this function the first time makes the argument
     count four, a second time makes the argument count sixteen, and so
     on.  By default, this is not bound to a key.


File: gdb.info,  Node: Commands For Completion,  Next: Keyboard Macros,  Prev: Numeric Arguments,  Up: Bindable Readline Commands

Letting Readline Type For You
-----------------------------

`complete (<TAB>)'
     Attempt to perform completion on the text before point.  The
     actual completion performed is application-specific.  The default
     is filename completion.

`possible-completions (M-?)'
     List the possible completions of the text before point.

`insert-completions (M-*)'
     Insert all completions of the text before point that would have
     been generated by `possible-completions'.

`menu-complete ()'
     Similar to `complete', but replaces the word to be completed with
     a single match from the list of possible completions.  Repeated
     execution of `menu-complete' steps through the list of possible
     completions, inserting each match in turn.  At the end of the list
     of completions, the bell is rung (subject to the setting of
     `bell-style') and the original text is restored.  An argument of N
     moves N positions forward in the list of matches; a negative
     argument may be used to move backward through the list.  This
     command is intended to be bound to <TAB>, but is unbound by
     default.

`delete-char-or-list ()'
     Deletes the character under the cursor if not at the beginning or
     end of the line (like `delete-char').  If at the end of the line,
     behaves identically to `possible-completions'.  This command is
     unbound by default.



File: gdb.info,  Node: Keyboard Macros,  Next: Miscellaneous Commands,  Prev: Commands For Completion,  Up: Bindable Readline Commands

Keyboard Macros
---------------

`start-kbd-macro (C-x ()'
     Begin saving the characters typed into the current keyboard macro.

`end-kbd-macro (C-x ))'
     Stop saving the characters typed into the current keyboard macro
     and save the definition.

`call-last-kbd-macro (C-x e)'
     Re-execute the last keyboard macro defined, by making the
     characters in the macro appear as if typed at the keyboard.



File: gdb.info,  Node: Miscellaneous Commands,  Prev: Keyboard Macros,  Up: Bindable Readline Commands

Some Miscellaneous Commands
---------------------------

`re-read-init-file (C-x C-r)'
     Read in the contents of the INPUTRC file, and incorporate any
     bindings or variable assignments found there.

`abort (C-g)'
     Abort the current editing command and ring the terminal's bell
     (subject to the setting of `bell-style').

`do-uppercase-version (M-a, M-b, M-X, ...)'
     If the metafied character X is lowercase, run the command that is
     bound to the corresponding uppercase character.

`prefix-meta (<ESC>)'
     Metafy the next character typed.  This is for keyboards without a
     meta key.  Typing `<ESC> f' is equivalent to typing `M-f'.

`undo (C-_ or C-x C-u)'
     Incremental undo, separately remembered for each line.

`revert-line (M-r)'
     Undo all changes made to this line.  This is like executing the
     `undo' command enough times to get back to the beginning.

`tilde-expand (M-~)'
     Perform tilde expansion on the current word.

`set-mark (C-@)'
     Set the mark to the point.  If a numeric argument is supplied, the
     mark is set to that position.

`exchange-point-and-mark (C-x C-x)'
     Swap the point with the mark.  The current cursor position is set
     to the saved position, and the old cursor position is saved as the
     mark.

`character-search (C-])'
     A character is read and point is moved to the next occurrence of
     that character.  A negative count searches for previous
     occurrences.

`character-search-backward (M-C-])'
     A character is read and point is moved to the previous occurrence
     of that character.  A negative count searches for subsequent
     occurrences.

`insert-comment (M-#)'
     Without a numeric argument, the value of the `comment-begin'
     variable is inserted at the beginning of the current line.  If a
     numeric argument is supplied, this command acts as a toggle:  if
     the characters at the beginning of the line do not match the value
     of `comment-begin', the value is inserted, otherwise the
     characters in `comment-begin' are deleted from the beginning of
     the line.  In either case, the line is accepted as if a newline
     had been typed.

`dump-functions ()'
     Print all of the functions and their key bindings to the Readline
     output stream.  If a numeric argument is supplied, the output is
     formatted in such a way that it can be made part of an INPUTRC
     file.  This command is unbound by default.

`dump-variables ()'
     Print all of the settable variables and their values to the
     Readline output stream.  If a numeric argument is supplied, the
     output is formatted in such a way that it can be made part of an
     INPUTRC file.  This command is unbound by default.

`dump-macros ()'
     Print all of the Readline key sequences bound to macros and the
     strings they output.  If a numeric argument is supplied, the
     output is formatted in such a way that it can be made part of an
     INPUTRC file.  This command is unbound by default.

`emacs-editing-mode (C-e)'
     When in `vi' command mode, this causes a switch to `emacs' editing
     mode.

`vi-editing-mode (M-C-j)'
     When in `emacs' editing mode, this causes a switch to `vi' editing
     mode.



File: gdb.info,  Node: Readline vi Mode,  Prev: Bindable Readline Commands,  Up: Command Line Editing

Readline vi Mode
================

While the Readline library does not have a full set of `vi' editing
functions, it does contain enough to allow simple editing of the line.
The Readline `vi' mode behaves as specified in the POSIX 1003.2
standard.

   In order to switch interactively between `emacs' and `vi' editing
modes, use the command `M-C-j' (bound to emacs-editing-mode when in
`vi' mode and to vi-editing-mode in `emacs' mode).  The Readline
default is `emacs' mode.

   When you enter a line in `vi' mode, you are already placed in
`insertion' mode, as if you had typed an `i'.  Pressing <ESC> switches
you into `command' mode, where you can edit the text of the line with
the standard `vi' movement keys, move to previous history lines with
`k' and subsequent lines with `j', and so forth.


File: gdb.info,  Node: Using History Interactively,  Next: Installing GDB,  Prev: Command Line Editing,  Up: Top

Using History Interactively
***************************

This chapter describes how to use the GNU History Library interactively,
from a user's standpoint.  It should be considered a user's guide.

* Menu:

* History Interaction::		What it feels like using History as a user.


File: gdb.info,  Node: History Interaction,  Up: Using History Interactively

History Expansion
=================

The History library provides a history expansion feature that is similar
to the history expansion provided by `csh'.  This section describes the
syntax used to manipulate the history information.

   History expansions introduce words from the history list into the
input stream, making it easy to repeat commands, insert the arguments
to a previous command into the current input line, or fix errors in
previous commands quickly.

   History expansion takes place in two parts.  The first is to
determine which line from the history list should be used during
substitution.  The second is to select portions of that line for
inclusion into the current one.  The line selected from the history is
called the "event", and the portions of that line that are acted upon
are called "words".  Various "modifiers" are available to manipulate
the selected words.  The line is broken into words in the same fashion
that Bash does, so that several words surrounded by quotes are
considered one word.  History expansions are introduced by the
appearance of the history expansion character, which is `!' by default.

* Menu:

* Event Designators::	How to specify which history line to use.
* Word Designators::	Specifying which words are of interest.
* Modifiers::		Modifying the results of substitution.


File: gdb.info,  Node: Event Designators,  Next: Word Designators,  Up: History Interaction

Event Designators
-----------------

An event designator is a reference to a command line entry in the
history list.

`!'
     Start a history substitution, except when followed by a space, tab,
     the end of the line, `=' or `('.

`!N'
     Refer to command line N.

`!-N'
     Refer to the command N lines back.

`!!'
     Refer to the previous command.  This is a synonym for `!-1'.

`!STRING'
     Refer to the most recent command starting with STRING.

`!?STRING[?]'
     Refer to the most recent command containing STRING.  The trailing
     `?' may be omitted if the STRING is followed immediately by a
     newline.

`^STRING1^STRING2^'
     Quick Substitution.  Repeat the last command, replacing STRING1
     with STRING2.  Equivalent to `!!:s/STRING1/STRING2/'.

`!#'
     The entire command line typed so far.



File: gdb.info,  Node: Word Designators,  Next: Modifiers,  Prev: Event Designators,  Up: History Interaction

Word Designators
----------------

Word designators are used to select desired words from the event.  A
`:' separates the event specification from the word designator.  It may
be omitted if the word designator begins with a `^', `$', `*', `-', or
`%'.  Words are numbered from the beginning of the line, with the first
word being denoted by 0 (zero).  Words are inserted into the current
line separated by single spaces.

   For example,

`!!'
     designates the preceding command.  When you type this, the
     preceding command is repeated in toto.

`!!:$'
     designates the last argument of the preceding command.  This may be
     shortened to `!$'.

`!fi:2'
     designates the second argument of the most recent command starting
     with the letters `fi'.

   Here are the word designators:

`0 (zero)'
     The `0'th word.  For many applications, this is the command word.

`N'
     The Nth word.

`^'
     The first argument; that is, word 1.

`$'
     The last argument.

`%'
     The word matched by the most recent `?STRING?' search.

`X-Y'
     A range of words; `-Y' abbreviates `0-Y'.

`*'
     All of the words, except the `0'th.  This is a synonym for `1-$'.
     It is not an error to use `*' if there is just one word in the
     event; the empty string is returned in that case.

`X*'
     Abbreviates `X-$'

`X-'
     Abbreviates `X-$' like `X*', but omits the last word.


   If a word designator is supplied without an event specification, the
previous command is used as the event.


File: gdb.info,  Node: Modifiers,  Prev: Word Designators,  Up: History Interaction

Modifiers
---------

After the optional word designator, you can add a sequence of one or
more of the following modifiers, each preceded by a `:'.

`h'
     Remove a trailing pathname component, leaving only the head.

`t'
     Remove all leading  pathname  components, leaving the tail.

`r'
     Remove a trailing suffix of the form `.SUFFIX', leaving the
     basename.

`e'
     Remove all but the trailing suffix.

`p'
     Print the new command but do not execute it.

`s/OLD/NEW/'
     Substitute NEW for the first occurrence of OLD in the event line.
     Any delimiter may be used in place of `/'.  The delimiter may be
     quoted in OLD and NEW with a single backslash.  If `&' appears in
     NEW, it is replaced by OLD.  A single backslash will quote the
     `&'.  The final delimiter is optional if it is the last character
     on the input line.

`&'
     Repeat the previous substitution.

`g'
     Cause changes to be applied over the entire event line.  Used in
     conjunction with `s', as in `gs/OLD/NEW/', or with `&'.



File: gdb.info,  Node: Formatting Documentation,  Next: Command Line Editing,  Prev: GDB Bugs,  Up: Top

Formatting Documentation
************************

The GDB 4 release includes an already-formatted reference card, ready
for printing with PostScript or Ghostscript, in the `gdb' subdirectory
of the main source directory(1).  If you can use PostScript or
Ghostscript with your printer, you can print the reference card
immediately with `refcard.ps'.

   The release also includes the source for the reference card.  You
can format it, using TeX, by typing:

     make refcard.dvi

   The GDB reference card is designed to print in "landscape" mode on
US "letter" size paper; that is, on a sheet 11 inches wide by 8.5 inches
high.  You will need to specify this form of printing as an option to
your DVI output program.

   All the documentation for GDB comes as part of the machine-readable
distribution.  The documentation is written in Texinfo format, which is
a documentation system that uses a single source file to produce both
on-line information and a printed manual.  You can use one of the Info
formatting commands to create the on-line version of the documentation
and TeX (or `texi2roff') to typeset the printed version.

   GDB includes an already formatted copy of the on-line Info version
of this manual in the `gdb' subdirectory.  The main Info file is
`gdb-6.1.1/gdb/gdb.info', and it refers to subordinate files matching
`gdb.info*' in the same directory.  If necessary, you can print out
these files, or read them with any editor; but they are easier to read
using the `info' subsystem in GNU Emacs or the standalone `info'
program, available as part of the GNU Texinfo distribution.

   If you want to format these Info files yourself, you need one of the
Info formatting programs, such as `texinfo-format-buffer' or `makeinfo'.

   If you have `makeinfo' installed, and are in the top level GDB
source directory (`gdb-6.1.1', in the case of version 6.1.1), you can
make the Info file by typing:

     cd gdb
     make gdb.info

   If you want to typeset and print copies of this manual, you need TeX,
a program to print its DVI output files, and `texinfo.tex', the Texinfo
definitions file.

   TeX is a typesetting program; it does not print files directly, but
produces output files called DVI files.  To print a typeset document,
you need a program to print DVI files.  If your system has TeX
installed, chances are it has such a program.  The precise command to
use depends on your system; `lpr -d' is common; another (for PostScript
devices) is `dvips'.  The DVI print command may require a file name
without any extension or a `.dvi' extension.

   TeX also requires a macro definitions file called `texinfo.tex'.
This file tells TeX how to typeset a document written in Texinfo
format.  On its own, TeX cannot either read or typeset a Texinfo file.
`texinfo.tex' is distributed with GDB and is located in the
`gdb-VERSION-NUMBER/texinfo' directory.

   If you have TeX and a DVI printer program installed, you can typeset
and print this manual.  First switch to the the `gdb' subdirectory of
the main source directory (for example, to `gdb-6.1.1/gdb') and type:

     make gdb.dvi

   Then give `gdb.dvi' to your DVI printing program.

   ---------- Footnotes ----------

   (1) In `gdb-6.1.1/gdb/refcard.ps' of the version 6.1.1 release.


File: gdb.info,  Node: Installing GDB,  Next: Maintenance Commands,  Prev: Using History Interactively,  Up: Top

Installing GDB
**************

GDB comes with a `configure' script that automates the process of
preparing GDB for installation; you can then use `make' to build the
`gdb' program.

   The GDB distribution includes all the source code you need for GDB
in a single directory, whose name is usually composed by appending the
version number to `gdb'.

   For example, the GDB version 6.1.1 distribution is in the
`gdb-6.1.1' directory.  That directory contains:

`gdb-6.1.1/configure (and supporting files)'
     script for configuring GDB and all its supporting libraries

`gdb-6.1.1/gdb'
     the source specific to GDB itself

`gdb-6.1.1/bfd'
     source for the Binary File Descriptor library

`gdb-6.1.1/include'
     GNU include files

`gdb-6.1.1/libiberty'
     source for the `-liberty' free software library

`gdb-6.1.1/opcodes'
     source for the library of opcode tables and disassemblers

`gdb-6.1.1/readline'
     source for the GNU command-line interface

`gdb-6.1.1/glob'
     source for the GNU filename pattern-matching subroutine

`gdb-6.1.1/mmalloc'
     source for the GNU memory-mapped malloc package

   The simplest way to configure and build GDB is to run `configure'
from the `gdb-VERSION-NUMBER' source directory, which in this example
is the `gdb-6.1.1' directory.

   First switch to the `gdb-VERSION-NUMBER' source directory if you are
not already in it; then run `configure'.  Pass the identifier for the
platform on which GDB will run as an argument.

   For example:

     cd gdb-6.1.1
     ./configure HOST
     make

where HOST is an identifier such as `sun4' or `decstation', that
identifies the platform where GDB will run.  (You can often leave off
HOST; `configure' tries to guess the correct value by examining your
system.)

   Running `configure HOST' and then running `make' builds the `bfd',
`readline', `mmalloc', and `libiberty' libraries, then `gdb' itself.
The configured source files, and the binaries, are left in the
corresponding source directories.

   `configure' is a Bourne-shell (`/bin/sh') script; if your system
does not recognize this automatically when you run a different shell,
you may need to run `sh' on it explicitly:

     sh configure HOST

   If you run `configure' from a directory that contains source
directories for multiple libraries or programs, such as the `gdb-6.1.1'
source directory for version 6.1.1, `configure' creates configuration
files for every directory level underneath (unless you tell it not to,
with the `--norecursion' option).

   You should run the `configure' script from the top directory in the
source tree, the `gdb-VERSION-NUMBER' directory.  If you run
`configure' from one of the subdirectories, you will configure only
that subdirectory.  That is usually not what you want.  In particular,
if you run the first `configure' from the `gdb' subdirectory of the
`gdb-VERSION-NUMBER' directory, you will omit the configuration of
`bfd', `readline', and other sibling directories of the `gdb'
subdirectory.  This leads to build errors about missing include files
such as `bfd/bfd.h'.

   You can install `gdb' anywhere; it has no hardwired paths.  However,
you should make sure that the shell on your path (named by the `SHELL'
environment variable) is publicly readable.  Remember that GDB uses the
shell to start your program--some systems refuse to let GDB debug child
processes whose programs are not readable.

* Menu:

* Separate Objdir::             Compiling GDB in another directory
* Config Names::                Specifying names for hosts and targets
* Configure Options::           Summary of options for configure


File: gdb.info,  Node: Separate Objdir,  Next: Config Names,  Up: Installing GDB

Compiling GDB in another directory
==================================

If you want to run GDB versions for several host or target machines,
you need a different `gdb' compiled for each combination of host and
target.  `configure' is designed to make this easy by allowing you to
generate each configuration in a separate subdirectory, rather than in
the source directory.  If your `make' program handles the `VPATH'
feature (GNU `make' does), running `make' in each of these directories
builds the `gdb' program specified there.

   To build `gdb' in a separate directory, run `configure' with the
`--srcdir' option to specify where to find the source.  (You also need
to specify a path to find `configure' itself from your working
directory.  If the path to `configure' would be the same as the
argument to `--srcdir', you can leave out the `--srcdir' option; it is
assumed.)

   For example, with version 6.1.1, you can build GDB in a separate
directory for a Sun 4 like this:

     cd gdb-6.1.1
     mkdir ../gdb-sun4
     cd ../gdb-sun4
     ../gdb-6.1.1/configure sun4
     make

   When `configure' builds a configuration using a remote source
directory, it creates a tree for the binaries with the same structure
(and using the same names) as the tree under the source directory.  In
the example, you'd find the Sun 4 library `libiberty.a' in the
directory `gdb-sun4/libiberty', and GDB itself in `gdb-sun4/gdb'.

   Make sure that your path to the `configure' script has just one
instance of `gdb' in it.  If your path to `configure' looks like
`../gdb-6.1.1/gdb/configure', you are configuring only one subdirectory
of GDB, not the whole package.  This leads to build errors about
missing include files such as `bfd/bfd.h'.

   One popular reason to build several GDB configurations in separate
directories is to configure GDB for cross-compiling (where GDB runs on
one machine--the "host"--while debugging programs that run on another
machine--the "target").  You specify a cross-debugging target by giving
the `--target=TARGET' option to `configure'.

   When you run `make' to build a program or library, you must run it
in a configured directory--whatever directory you were in when you
called `configure' (or one of its subdirectories).

   The `Makefile' that `configure' generates in each source directory
also runs recursively.  If you type `make' in a source directory such
as `gdb-6.1.1' (or in a separate configured directory configured with
`--srcdir=DIRNAME/gdb-6.1.1'), you will build all the required
libraries, and then build GDB.

   When you have multiple hosts or targets configured in separate
directories, you can run `make' on them in parallel (for example, if
they are NFS-mounted on each of the hosts); they will not interfere
with each other.


File: gdb.info,  Node: Config Names,  Next: Configure Options,  Prev: Separate Objdir,  Up: Installing GDB

Specifying names for hosts and targets
======================================

The specifications used for hosts and targets in the `configure' script
are based on a three-part naming scheme, but some short predefined
aliases are also supported.  The full naming scheme encodes three pieces
of information in the following pattern:

     ARCHITECTURE-VENDOR-OS

   For example, you can use the alias `sun4' as a HOST argument, or as
the value for TARGET in a `--target=TARGET' option.  The equivalent
full name is `sparc-sun-sunos4'.

   The `configure' script accompanying GDB does not provide any query
facility to list all supported host and target names or aliases.
`configure' calls the Bourne shell script `config.sub' to map
abbreviations to full names; you can read the script, if you wish, or
you can use it to test your guesses on abbreviations--for example:

     % sh config.sub i386-linux
     i386-pc-linux-gnu
     % sh config.sub alpha-linux
     alpha-unknown-linux-gnu
     % sh config.sub hp9k700
     hppa1.1-hp-hpux
     % sh config.sub sun4
     sparc-sun-sunos4.1.1
     % sh config.sub sun3
     m68k-sun-sunos4.1.1
     % sh config.sub i986v
     Invalid configuration `i986v': machine `i986v' not recognized

`config.sub' is also distributed in the GDB source directory
(`gdb-6.1.1', for version 6.1.1).


File: gdb.info,  Node: Configure Options,  Prev: Config Names,  Up: Installing GDB

`configure' options
===================

Here is a summary of the `configure' options and arguments that are
most often useful for building GDB.  `configure' also has several other
options not listed here.  *note (configure.info)What Configure Does::,
for a full explanation of `configure'.

     configure [--help]
               [--prefix=DIR]
               [--exec-prefix=DIR]
               [--srcdir=DIRNAME]
               [--norecursion] [--rm]
               [--target=TARGET]
               HOST

You may introduce options with a single `-' rather than `--' if you
prefer; but you may abbreviate option names if you use `--'.

`--help'
     Display a quick summary of how to invoke `configure'.

`--prefix=DIR'
     Configure the source to install programs and files under directory
     `DIR'.

`--exec-prefix=DIR'
     Configure the source to install programs under directory `DIR'.

`--srcdir=DIRNAME'
     *Warning: using this option requires GNU `make', or another `make'
     that implements the `VPATH' feature.*
     Use this option to make configurations in directories separate
     from the GDB source directories.  Among other things, you can use
     this to build (or maintain) several configurations simultaneously,
     in separate directories.  `configure' writes configuration
     specific files in the current directory, but arranges for them to
     use the source in the directory DIRNAME.  `configure' creates
     directories under the working directory in parallel to the source
     directories below DIRNAME.

`--norecursion'
     Configure only the directory level where `configure' is executed;
     do not propagate configuration to subdirectories.

`--target=TARGET'
     Configure GDB for cross-debugging programs running on the specified
     TARGET.  Without this option, GDB is configured to debug programs
     that run on the same machine (HOST) as GDB itself.

     There is no convenient way to generate a list of all available
     targets.

`HOST ...'
     Configure GDB to run on the specified HOST.

     There is no convenient way to generate a list of all available
     hosts.

   There are many other options available as well, but they are
generally needed for special purposes only.


File: gdb.info,  Node: Maintenance Commands,  Next: Remote Protocol,  Prev: Installing GDB,  Up: Top

Maintenance Commands
********************

In addition to commands intended for GDB users, GDB includes a number
of commands intended for GDB developers.  These commands are provided
here for reference.

`maint info breakpoints'
     Using the same format as `info breakpoints', display both the
     breakpoints you've set explicitly, and those GDB is using for
     internal purposes.  Internal breakpoints are shown with negative
     breakpoint numbers.  The type column identifies what kind of
     breakpoint is shown:

    `breakpoint'
          Normal, explicitly set breakpoint.

    `watchpoint'
          Normal, explicitly set watchpoint.

    `longjmp'
          Internal breakpoint, used to handle correctly stepping through
          `longjmp' calls.

    `longjmp resume'
          Internal breakpoint at the target of a `longjmp'.

    `until'
          Temporary internal breakpoint used by the GDB `until' command.

    `finish'
          Temporary internal breakpoint used by the GDB `finish'
          command.

    `shlib events'
          Shared library events.


`maint internal-error'
`maint internal-warning'
     Cause GDB to call the internal function `internal_error' or
     `internal_warning' and hence behave as though an internal error or
     internal warning has been detected.  In addition to reporting the
     internal problem, these functions give the user the opportunity to
     either quit GDB or create a core file of the current GDB session.

          (gdb) maint internal-error testing, 1, 2
          .../maint.c:121: internal-error: testing, 1, 2
          A problem internal to GDB has been detected.  Further
          debugging may prove unreliable.
          Quit this debugging session? (y or n) n
          Create a core file? (y or n) n
          (gdb)

     Takes an optional parameter that is used as the text of the error
     or warning message.

`maint print dummy-frames'
     Prints the contents of GDB's internal dummy-frame stack.

          (gdb) b add
          ...
          (gdb) print add(2,3)
          Breakpoint 2, add (a=2, b=3) at ...
          58	  return (a + b);
          The program being debugged stopped while in a function called from GDB.
          ...
          (gdb) maint print dummy-frames
          0x1a57c80: pc=0x01014068 fp=0x0200bddc sp=0x0200bdd6
           top=0x0200bdd4 id={stack=0x200bddc,code=0x101405c}
           call_lo=0x01014000 call_hi=0x01014001
          (gdb)

     Takes an optional file parameter.

`maint print registers'
`maint print raw-registers'
`maint print cooked-registers'
`maint print register-groups'
     Print GDB's internal register data structures.

     The command `maint print raw-registers' includes the contents of
     the raw register cache; the command `maint print cooked-registers'
     includes the (cooked) value of all registers; and the command
     `maint print register-groups' includes the groups that each
     register is a member of.  *Note Registers: (gdbint)Registers.

     Takes an optional file parameter.

`maint print reggroups'
     Print GDB's internal register group data structures.

     Takes an optional file parameter.

          (gdb) maint print reggroups
           Group      Type
           general    user
           float      user
           all        user
           vector     user
           system     user
           save       internal
           restore    internal

`maint set profile'
`maint show profile'
     Control profiling of GDB.

     Profiling will be disabled until you use the `maint set profile'
     command to enable it.  When you enable profiling, the system will
     begin collecting timing and execution count data; when you disable
     profiling or exit GDB, the results will be written to a log file.
     Remember that if you use profiling, GDB will overwrite the
     profiling log file (often called `gmon.out').  If you have a
     record of important profiling data in a `gmon.out' file, be sure
     to move it to a safe location.

     Configuring with `--enable-profiling' arranges for GDB to be
     compiled with the `-pg' compiler option.



File: gdb.info,  Node: Remote Protocol,  Next: Agent Expressions,  Prev: Maintenance Commands,  Up: Top

GDB Remote Serial Protocol
**************************

* Menu:

* Overview::
* Packets::
* Stop Reply Packets::
* General Query Packets::
* Register Packet Format::
* Examples::
* File-I/O remote protocol extension::


File: gdb.info,  Node: Overview,  Next: Packets,  Up: Remote Protocol

Overview
========

There may be occasions when you need to know something about the
protocol--for example, if there is only one serial port to your target
machine, you might want your program to do something special if it
recognizes a packet meant for GDB.

   In the examples below, `->' and `<-' are used to indicate
transmitted and received data respectfully.

   All GDB commands and responses (other than acknowledgments) are sent
as a PACKET.  A PACKET is introduced with the character `$', the actual
PACKET-DATA, and the terminating character `#' followed by a two-digit
CHECKSUM:

     `$'PACKET-DATA`#'CHECKSUM

The two-digit CHECKSUM is computed as the modulo 256 sum of all
characters between the leading `$' and the trailing `#' (an eight bit
unsigned checksum).

   Implementors should note that prior to GDB 5.0 the protocol
specification also included an optional two-digit SEQUENCE-ID:

     `$'SEQUENCE-ID`:'PACKET-DATA`#'CHECKSUM

That SEQUENCE-ID was appended to the acknowledgment.  GDB has never
output SEQUENCE-IDs.  Stubs that handle packets added since GDB 5.0
must not accept SEQUENCE-ID.

   When either the host or the target machine receives a packet, the
first response expected is an acknowledgment: either `+' (to indicate
the package was received correctly) or `-' (to request retransmission):

     -> `$'PACKET-DATA`#'CHECKSUM
     <- `+'

The host (GDB) sends COMMANDs, and the target (the debugging stub
incorporated in your program) sends a RESPONSE.  In the case of step
and continue COMMANDs, the response is only sent when the operation has
completed (the target has again stopped).

   PACKET-DATA consists of a sequence of characters with the exception
of `#' and `$' (see `X' packet for additional exceptions).

   Fields within the packet should be separated using `,' `;' or `:'.
Except where otherwise noted all numbers are represented in HEX with
leading zeros suppressed.

   Implementors should note that prior to GDB 5.0, the character `:'
could not appear as the third character in a packet (as it would
potentially conflict with the SEQUENCE-ID).

   Response DATA can be run-length encoded to save space.  A `*' means
that the next character is an ASCII encoding giving a repeat count
which stands for that many repetitions of the character preceding the
`*'.  The encoding is `n+29', yielding a printable character where `n
>=3' (which is where rle starts to win).  The printable characters `$',
`#', `+' and `-' or with a numeric value greater than 126 should not be
used.

   So:
     "`0* '"

means the same as "0000".

   The error response returned for some packets includes a two character
error number.  That number is not well defined.

   For any COMMAND not supported by the stub, an empty response
(`$#00') should be returned.  That way it is possible to extend the
protocol.  A newer GDB can tell if a packet is supported based on that
response.

   A stub is required to support the `g', `G', `m', `M', `c', and `s'
COMMANDs.  All other COMMANDs are optional.


File: gdb.info,  Node: Packets,  Next: Stop Reply Packets,  Prev: Overview,  Up: Remote Protocol

Packets
=======

The following table provides a complete list of all currently defined
COMMANDs and their corresponding response DATA.

`!' -- extended mode
     Enable extended mode.  In extended mode, the remote server is made
     persistent.  The `R' packet is used to restart the program being
     debugged.

     Reply:
    `OK'
          The remote target both supports and has enabled extended mode.

`?' -- last signal
     Indicate the reason the target halted.  The reply is the same as
     for step and continue.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`a' -- reserved
     Reserved for future use.

`A'ARGLEN`,'ARGNUM`,'ARG`,...' --  set program arguments *(reserved)*
     Initialized `argv[]' array passed into program. ARGLEN specifies
     the number of bytes in the hex encoded byte stream ARG.  See
     `gdbserver' for more details.

     Reply:
    `OK'

    `ENN'

`b'BAUD -- set baud *(deprecated)*
     Change the serial line speed to BAUD.

     JTC: _When does the transport layer state change?  When it's
     received, or after the ACK is transmitted.  In either case, there
     are problems if the command or the acknowledgment packet is
     dropped._

     Stan: _If people really wanted to add something like this, and get
     it working for the first time, they ought to modify ser-unix.c to
     send some kind of out-of-band message to a specially-setup stub
     and have the switch happen "in between" packets, so that from
     remote protocol's point of view, nothing actually happened._

`B'ADDR,MODE -- set breakpoint *(deprecated)*
     Set (MODE is `S') or clear (MODE is `C') a breakpoint at ADDR.

     This packet has been replaced by the `Z' and `z' packets (*note
     insert breakpoint or watchpoint packet::).

`c'ADDR -- continue
     ADDR is address to resume.  If ADDR is omitted, resume at current
     address.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`C'SIG`;'ADDR -- continue with signal
     Continue with signal SIG (hex signal number).  If `;'ADDR is
     omitted, resume at same address.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`d' -- toggle debug *(deprecated)*
     Toggle debug flag.

`D' -- detach
     Detach GDB from the remote system.  Sent to the remote target
     before GDB disconnects via the `detach' command.

     Reply:
    `_no response_'
          GDB does not check for any response after sending this packet.

`e' -- reserved
     Reserved for future use.

`E' -- reserved
     Reserved for future use.

`f' -- reserved
     Reserved for future use.

`F'RC`,'EE`,'CF`;'XX -- Reply to target's F packet.
     This packet is send by GDB as reply to a `F' request packet sent
     by the target.  This is part of the File-I/O protocol extension.
     *Note File-I/O remote protocol extension::, for the specification.

`g' -- read registers
     Read general registers.

     Reply:
    `XX...'
          Each byte of register data is described by two hex digits.
          The bytes with the register are transmitted in target byte
          order.  The size of each register and their position within
          the `g' PACKET are determined by the GDB internal macros
          DEPRECATED_REGISTER_RAW_SIZE and REGISTER_NAME macros.  The
          specification of several standard `g' packets is specified
          below.

    `ENN'
          for an error.

`G'XX... -- write regs
     *Note read registers packet::, for a description of the XX...
     data.

     Reply:
    `OK'
          for success

    `ENN'
          for an error

`h' -- reserved
     Reserved for future use.

`H'CT... -- set thread
     Set thread for subsequent operations (`m', `M', `g', `G', et.al.).
     C depends on the operation to be performed: it should be `c' for
     step and continue operations, `g' for other operations.  The
     thread designator T... may be -1, meaning all the threads, a
     thread number, or zero which means pick any thread.

     Reply:
    `OK'
          for success

    `ENN'
          for an error

`i'ADDR`,'NNN -- cycle step *(draft)*
     Step the remote target by a single clock cycle.  If `,'NNN is
     present, cycle step NNN cycles.  If ADDR is present, cycle step
     starting at that address.

`I' -- signal then cycle step *(reserved)*
     *Note step with signal packet::.  *Note cycle step packet::.

`j' -- reserved
     Reserved for future use.

`J' -- reserved
     Reserved for future use.

`k' -- kill request
     FIXME: _There is no description of how to operate when a specific
     thread context has been selected (i.e. does 'k' kill only that
     thread?)_.

`K' -- reserved
     Reserved for future use.

`l' -- reserved
     Reserved for future use.

`L' -- reserved
     Reserved for future use.

`m'ADDR`,'LENGTH -- read memory
     Read LENGTH bytes of memory starting at address ADDR.  Neither GDB
     nor the stub assume that sized memory transfers are assumed using
     word aligned accesses. FIXME: _A word aligned memory transfer
     mechanism is needed._

     Reply:
    `XX...'
          XX... is mem contents. Can be fewer bytes than requested if
          able to read only part of the data.  Neither GDB nor the stub
          assume that sized memory transfers are assumed using word
          aligned accesses. FIXME: _A word aligned memory transfer
          mechanism is needed._

    `ENN'
          NN is errno

`M'ADDR,LENGTH`:'XX... -- write mem
     Write LENGTH bytes of memory starting at address ADDR.  XX... is
     the data.

     Reply:
    `OK'
          for success

    `ENN'
          for an error (this includes the case where only part of the
          data was written).

`n' -- reserved
     Reserved for future use.

`N' -- reserved
     Reserved for future use.

`o' -- reserved
     Reserved for future use.

`O' -- reserved
     Reserved for future use.

`p'N... -- read reg *(reserved)*
     *Note write register packet::.

     Reply:
    `R....'
          The hex encoded value of the register in target byte order.

`P'N...`='R... -- write register
     Write register N... with value R..., which contains two hex digits
     for each byte in the register (target byte order).

     Reply:
    `OK'
          for success

    `ENN'
          for an error

`q'QUERY -- general query
     Request info about QUERY.  In general GDB queries have a leading
     upper case letter.  Custom vendor queries should use a company
     prefix (in lower case) ex: `qfsf.var'.  QUERY may optionally be
     followed by a `,' or `;' separated list.  Stubs must ensure that
     they match the full QUERY name.

     Reply:
    `XX...'
          Hex encoded data from query.  The reply can not be empty.

    `ENN'
          error reply

    `'
          Indicating an unrecognized QUERY.

`Q'VAR`='VAL -- general set
     Set value of VAR to VAL.

     *Note general query packet::, for a discussion of naming
     conventions.

`r' -- reset *(deprecated)*
     Reset the entire system.

`R'XX -- remote restart
     Restart the program being debugged.  XX, while needed, is ignored.
     This packet is only available in extended mode.

     Reply:
    `_no reply_'
          The `R' packet has no reply.

`s'ADDR -- step
     ADDR is address to resume.  If ADDR is omitted, resume at same
     address.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`S'SIG`;'ADDR -- step with signal
     Like `C' but step not continue.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`t'ADDR`:'PP`,'MM -- search
     Search backwards starting at address ADDR for a match with pattern
     PP and mask MM.  PP and MM are 4 bytes.  ADDR must be at least 3
     digits.

`T'XX -- thread alive
     Find out if the thread XX is alive.

     Reply:
    `OK'
          thread is still alive

    `ENN'
          thread is dead

`u' -- reserved
     Reserved for future use.

`U' -- reserved
     Reserved for future use.

`v' -- verbose packet prefix
     Packets starting with `v' are identified by a multi-letter name,
     up to the first `;' or `?' (or the end of the packet).

`vCont'[;ACTION[`:'TID]]... -- extended resume
     Resume the inferior.  Different actions may be specified for each
     thread.  If an action is specified with no TID, then it is applied
     to any threads that don't have a specific action specified; if no
     default action is specified then other threads should remain
     stopped.  Specifying multiple default actions is an error;
     specifying no actions is also an error.  Thread IDs are specified
     in hexadecimal.  Currently supported actions are:

    `c'
          Continue.

    `CSIG'
          Continue with signal SIG.  SIG should be two hex digits.

    `s'
          Step.

    `SSIG'
          Step with signal SIG.  SIG should be two hex digits.

     The optional ADDR argument normally associated with these packets
     is not supported in `vCont'.

     Reply: *Note Stop Reply Packets::, for the reply specifications.

`vCont?' -- extended resume query
     Query support for the `vCont' packet.

     Reply:
    ``vCont'[;ACTION]...'
          The `vCont' packet is supported.  Each ACTION is a supported
          command in the `vCont' packet.

    `'
          The `vCont' packet is not supported.

`V' -- reserved
     Reserved for future use.

`w' -- reserved
     Reserved for future use.

`W' -- reserved
     Reserved for future use.

`x' -- reserved
     Reserved for future use.

`X'ADDR`,'LENGTH:XX... -- write mem (binary)
     ADDR is address, LENGTH is number of bytes, XX...  is binary data.
     The characters `$', `#', and `0x7d' are escaped using `0x7d'.

     Reply:
    `OK'
          for success

    `ENN'
          for an error

`y' -- reserved
     Reserved for future use.

`Y' reserved
     Reserved for future use.

`z'TYPE`,'ADDR`,'LENGTH -- remove breakpoint or watchpoint *(draft)*
`Z'TYPE`,'ADDR`,'LENGTH -- insert breakpoint or watchpoint *(draft)*
     Insert (`Z') or remove (`z') a TYPE breakpoint or watchpoint
     starting at address ADDRESS and covering the next LENGTH bytes.

     Each breakpoint and watchpoint packet TYPE is documented
     separately.

     _Implementation notes: A remote target shall return an empty string
     for an unrecognized breakpoint or watchpoint packet TYPE.  A
     remote target shall support either both or neither of a given
     `Z'TYPE... and `z'TYPE... packet pair.  To avoid potential
     problems with duplicate packets, the operations should be
     implemented in an idempotent way._

`z'`0'`,'ADDR`,'LENGTH -- remove memory breakpoint *(draft)*

`Z'`0'`,'ADDR`,'LENGTH -- insert memory breakpoint *(draft)*
     Insert (`Z0') or remove (`z0') a memory breakpoint at address
     `addr' of size `length'.

     A memory breakpoint is implemented by replacing the instruction at
     ADDR with a software breakpoint or trap instruction.  The `length'
     is used by targets that indicates the size of the breakpoint (in
     bytes) that should be inserted (e.g., the ARM and MIPS can insert
     either a 2 or 4 byte breakpoint).

     _Implementation note: It is possible for a target to copy or move
     code that contains memory breakpoints (e.g., when implementing
     overlays).  The behavior of this packet, in the presence of such a
     target, is not defined._

     Reply:
    `OK'
          success

    `'
          not supported

    `ENN'
          for an error

`z'`1'`,'ADDR`,'LENGTH -- remove hardware breakpoint *(draft)*

`Z'`1'`,'ADDR`,'LENGTH -- insert hardware breakpoint *(draft)*
     Insert (`Z1') or remove (`z1') a hardware breakpoint at address
     `addr' of size `length'.

     A hardware breakpoint is implemented using a mechanism that is not
     dependant on being able to modify the target's memory.

     _Implementation note: A hardware breakpoint is not affected by code
     movement._

     Reply:
    `OK'
          success

    `'
          not supported

    `ENN'
          for an error

`z'`2'`,'ADDR`,'LENGTH -- remove write watchpoint *(draft)*

`Z'`2'`,'ADDR`,'LENGTH -- insert write watchpoint *(draft)*
     Insert (`Z2') or remove (`z2') a write watchpoint.

     Reply:
    `OK'
          success

    `'
          not supported

    `ENN'
          for an error

`z'`3'`,'ADDR`,'LENGTH -- remove read watchpoint *(draft)*

`Z'`3'`,'ADDR`,'LENGTH -- insert read watchpoint *(draft)*
     Insert (`Z3') or remove (`z3') a read watchpoint.

     Reply:
    `OK'
          success

    `'
          not supported

    `ENN'
          for an error

`z'`4'`,'ADDR`,'LENGTH -- remove access watchpoint *(draft)*

`Z'`4'`,'ADDR`,'LENGTH -- insert access watchpoint *(draft)*
     Insert (`Z4') or remove (`z4') an access watchpoint.

     Reply:
    `OK'
          success

    `'
          not supported

    `ENN'
          for an error



File: gdb.info,  Node: Stop Reply Packets,  Next: General Query Packets,  Prev: Packets,  Up: Remote Protocol

Stop Reply Packets
==================

The `C', `c', `S', `s' and `?' packets can receive any of the below as
a reply.  In the case of the `C', `c', `S' and `s' packets, that reply
is only returned when the target halts.  In the below the exact meaning
of `signal number' is poorly defined.  In general one of the UNIX
signal numbering conventions is used.

`SAA'
     AA is the signal number

``T'AAN...`:'R...`;'N...`:'R...`;'N...`:'R...`;''
     AA = two hex digit signal number; N... = register number (hex),
     R...  = target byte ordered register contents, size defined by
     `DEPRECATED_REGISTER_RAW_SIZE'; N... = `thread', R... = thread
     process ID, this is a hex integer; N... = (`watch' | `rwatch' |
     `awatch', R... = data address, this is a hex integer; N... = other
     string not starting with valid hex digit.  GDB should ignore this
     N..., R... pair and go on to the next.  This way we can extend the
     protocol.

`WAA'
     The process exited, and AA is the exit status.  This is only
     applicable to certain targets.

`XAA'
     The process terminated with signal AA.

`OXX...'
     XX... is hex encoding of ASCII data.  This can happen at any time
     while the program is running and the debugger should continue to
     wait for `W', `T', etc.

`FCALL-ID`,'PARAMETER...'
     CALL-ID is the identifier which says which host system call should
     be called.  This is just the name of the function.  Translation
     into the correct system call is only applicable as it's defined in
     GDB.  *Note File-I/O remote protocol extension::, for a list of
     implemented system calls.

     PARAMETER... is a list of parameters as defined for this very
     system call.

     The target replies with this packet when it expects GDB to call a
     host system call on behalf of the target.  GDB replies with an
     appropriate `F' packet and keeps up waiting for the next reply
     packet from the target.  The latest `C', `c', `S' or `s' action is
     expected to be continued.  *Note File-I/O remote protocol
     extension::, for more details.



File: gdb.info,  Node: General Query Packets,  Next: Register Packet Format,  Prev: Stop Reply Packets,  Up: Remote Protocol

General Query Packets
=====================

The following set and query packets have already been defined.

`q'`C' -- current thread
     Return the current thread id.

     Reply:
    ``QC'PID'
          Where PID is a HEX encoded 16 bit process id.

    `*'
          Any other reply implies the old pid.

`q'`fThreadInfo' - all thread ids
     `q'`sThreadInfo'

     Obtain a list of active thread ids from the target (OS).  Since
     there may be too many active threads to fit into one reply packet,
     this query works iteratively: it may require more than one
     query/reply sequence to obtain the entire list of threads.  The
     first query of the sequence will be the `qf'`ThreadInfo' query;
     subsequent queries in the sequence will be the `qs'`ThreadInfo'
     query.

     NOTE: replaces the `qL' query (see below).

     Reply:
    ``m'ID'
          A single thread id

    ``m'ID,ID...'
          a comma-separated list of thread ids

    ``l''
          (lower case 'el') denotes end of list.

     In response to each query, the target will reply with a list of
     one or more thread ids, in big-endian hex, separated by commas.
     GDB will respond to each reply with a request for more thread ids
     (using the `qs' form of the query), until the target responds with
     `l' (lower-case el, for `'last'').

`q'`ThreadExtraInfo'`,'ID -- extra thread info
     Where ID is a thread-id in big-endian hex.  Obtain a printable
     string description of a thread's attributes from the target OS.
     This string may contain anything that the target OS thinks is
     interesting for GDB to tell the user about the thread.  The string
     is displayed in GDB's `info threads' display.  Some examples of
     possible thread extra info strings are "Runnable", or "Blocked on
     Mutex".

     Reply:
    `XX...'
          Where XX... is a hex encoding of ASCII data, comprising the
          printable string containing the extra information about the
          thread's attributes.

`q'`L'STARTFLAGTHREADCOUNTNEXTTHREAD -- query LIST or THREADLIST *(deprecated)*
     Obtain thread information from RTOS.  Where: STARTFLAG (one hex
     digit) is one to indicate the first query and zero to indicate a
     subsequent query; THREADCOUNT (two hex digits) is the maximum
     number of threads the response packet can contain; and NEXTTHREAD
     (eight hex digits), for subsequent queries (STARTFLAG is zero), is
     returned in the response as ARGTHREAD.

     NOTE: this query is replaced by the `q'`fThreadInfo' query (see
     above).

     Reply:
    ``q'`M'COUNTDONEARGTHREADTHREAD...'
          Where: COUNT (two hex digits) is the number of threads being
          returned; DONE (one hex digit) is zero to indicate more
          threads and one indicates no further threads; ARGTHREADID
          (eight hex digits) is NEXTTHREAD from the request packet;
          THREAD...  is a sequence of thread IDs from the target.
          THREADID (eight hex digits).  See
          `remote.c:parse_threadlist_response()'.

`q'`CRC:'ADDR`,'LENGTH -- compute CRC of memory block
     Reply:
    ``E'NN'
          An error (such as memory fault)

    ``C'CRC32'
          A 32 bit cyclic redundancy check of the specified memory
          region.

`q'`Offsets' -- query sect offs
     Get section offsets that the target used when re-locating the
     downloaded image.  _Note: while a `Bss' offset is included in the
     response, GDB ignores this and instead applies the `Data' offset
     to the `Bss' section._

     Reply:
    ``Text='XXX`;Data='YYY`;Bss='ZZZ'

`q'`P'MODETHREADID -- thread info request
     Returns information on THREADID.  Where: MODE is a hex encoded 32
     bit mode; THREADID is a hex encoded 64 bit thread ID.

     Reply:
    `*'

     See `remote.c:remote_unpack_thread_info_response()'.

`q'`Rcmd,'COMMAND -- remote command
     COMMAND (hex encoded) is passed to the local interpreter for
     execution.  Invalid commands should be reported using the output
     string.  Before the final result packet, the target may also
     respond with a number of intermediate `O'OUTPUT console output
     packets.  _Implementors should note that providing access to a
     stubs's interpreter may have security implications_.

     Reply:
    `OK'
          A command response with no output.

    `OUTPUT'
          A command response with the hex encoded output string OUTPUT.

    ``E'NN'
          Indicate a badly formed request.

    ``''
          When `q'`Rcmd' is not recognized.

`qSymbol::' -- symbol lookup
     Notify the target that GDB is prepared to serve symbol lookup
     requests.  Accept requests from the target for the values of
     symbols.

     Reply:
    ``OK''
          The target does not need to look up any (more) symbols.

    ``qSymbol:'SYM_NAME'
          The target requests the value of symbol SYM_NAME (hex
          encoded).  GDB may provide the value by using the
          `qSymbol:'SYM_VALUE:SYM_NAME message, described below.

`qSymbol:'SYM_VALUE:SYM_NAME -- symbol value
     Set the value of SYM_NAME to SYM_VALUE.

     SYM_NAME (hex encoded) is the name of a symbol whose value the
     target has previously requested.

     SYM_VALUE (hex) is the value for symbol SYM_NAME.  If GDB cannot
     supply a value for SYM_NAME, then this field will be empty.

     Reply:
    ``OK''
          The target does not need to look up any (more) symbols.

    ``qSymbol:'SYM_NAME'
          The target requests the value of a new symbol SYM_NAME (hex
          encoded).  GDB will continue to supply the values of symbols
          (if available), until the target ceases to request them.

`qPart':OBJECT:`read':ANNEX:OFFSET,LENGTH -- read special data
     Read uninterpreted bytes from the target's special data area
     identified by the keyword `object'.  Request LENGTH bytes starting
     at OFFSET bytes into the data.  The content and encoding of ANNEX
     is specific to the object; it can supply additional details about
     what data to access.

     Here are the specific requests of this form defined so far.  All
     ``qPart':OBJECT:`read':...' requests use the same reply formats,
     listed below.

    `qPart':`auxv':`read'::OFFSET,LENGTH
          Access the target's "auxiliary vector".  *Note Auxiliary
          Vector::.  Note ANNEX must be empty.

     Reply:
    `OK'
          The OFFSET in the request is at the end of the data.  There
          is no more data to be read.

    XX...
          Hex encoded data bytes read.  This may be fewer bytes than
          the LENGTH in the request.

    `E00'
          The request was malformed, or ANNEX was invalid.

    `E'NN
          The offset was invalid, or there was an error encountered
          reading the data.  NN is a hex-encoded `errno' value.

    `""' (empty)
          An empty reply indicates the OBJECT or ANNEX string was not
          recognized by the stub.

`qPart':OBJECT:`write':ANNEX:OFFSET:DATA...
     Write uninterpreted bytes into the target's special data area
     identified by the keyword `object', starting at OFFSET bytes into
     the data.  DATA... is the hex-encoded data to be written.  The
     content and encoding of ANNEX is specific to the object; it can
     supply additional details about what data to access.

     No requests of this form are presently in use.  This specification
     serves as a placeholder to document the common format that new
     specific request specifications ought to use.

     Reply:
    NN
          NN (hex encoded) is the number of bytes written.  This may be
          fewer bytes than supplied in the request.

    `E00'
          The request was malformed, or ANNEX was invalid.

    `E'NN
          The offset was invalid, or there was an error encountered
          writing the data.  NN is a hex-encoded `errno' value.

    `""' (empty)
          An empty reply indicates the OBJECT or ANNEX string was not
          recognized by the stub, or that the object does not support
          writing.

`qPart':OBJECT:OPERATION:...
     Requests of this form may be added in the future.  When a stub does
     not recognize the OBJECT keyword, or its support for OBJECT does
     not recognize the OPERATION keyword, the stub must respond with an
     empty packet.


File: gdb.info,  Node: Register Packet Format,  Next: Examples,  Prev: General Query Packets,  Up: Remote Protocol

Register Packet Format
======================

The following `g'/`G' packets have previously been defined.  In the
below, some thirty-two bit registers are transferred as sixty-four
bits.  Those registers should be zero/sign extended (which?)  to fill
the space allocated.  Register bytes are transfered in target byte
order.  The two nibbles within a register byte are transfered
most-significant - least-significant.

MIPS32
     All registers are transfered as thirty-two bit quantities in the
     order: 32 general-purpose; sr; lo; hi; bad; cause; pc; 32
     floating-point registers; fsr; fir; fp.

MIPS64
     All registers are transfered as sixty-four bit quantities
     (including thirty-two bit registers such as `sr').  The ordering
     is the same as `MIPS32'.



File: gdb.info,  Node: Examples,  Next: File-I/O remote protocol extension,  Prev: Register Packet Format,  Up: Remote Protocol

Examples
========

Example sequence of a target being re-started.  Notice how the restart
does not get any direct output:

     -> `R00'
     <- `+'
     _target restarts_
     -> `?'
     <- `+'
     <- `T001:1234123412341234'
     -> `+'

   Example sequence of a target being stepped by a single instruction:

     -> `G1445...'
     <- `+'
     -> `s'
     <- `+'
     _time passes_
     <- `T001:1234123412341234'
     -> `+'
     -> `g'
     <- `+'
     <- `1455...'
     -> `+'


File: gdb.info,  Node: File-I/O remote protocol extension,  Prev: Examples,  Up: Remote Protocol

File-I/O remote protocol extension
==================================

* Menu:

* File-I/O Overview::
* Protocol basics::
* The F request packet::
* The F reply packet::
* Memory transfer::
* The Ctrl-C message::
* Console I/O::
* The isatty call::
* The system call::
* List of supported calls::
* Protocol specific representation of datatypes::
* Constants::
* File-I/O Examples::


File: gdb.info,  Node: File-I/O Overview,  Next: Protocol basics,  Up: File-I/O remote protocol extension

File-I/O Overview
-----------------

The File I/O remote protocol extension (short: File-I/O) allows the
target to use the hosts file system and console I/O when calling various
system calls.  System calls on the target system are translated into a
remote protocol packet to the host system which then performs the needed
actions and returns with an adequate response packet to the target
system.  This simulates file system operations even on targets that
lack file systems.

   The protocol is defined host- and target-system independent.  It uses
it's own independent representation of datatypes and values.  Both, GDB
and the target's GDB stub are responsible for translating the system
dependent values into the unified protocol values when data is
transmitted.

   The communication is synchronous.  A system call is possible only
when GDB is waiting for the `C', `c', `S' or `s' packets.  While GDB
handles the request for a system call, the target is stopped to allow
deterministic access to the target's memory.  Therefore File-I/O is not
interuptible by target signals.  It is possible to interrupt File-I/O
by a user interrupt (Ctrl-C), though.

   The target's request to perform a host system call does not finish
the latest `C', `c', `S' or `s' action.  That means, after finishing
the system call, the target returns to continuing the previous activity
(continue, step).  No additional continue or step request from GDB is
required.

     (gdb) continue
       <- target requests 'system call X'
       target is stopped, GDB executes system call
       -> GDB returns result
       ... target continues, GDB returns to wait for the target
       <- target hits breakpoint and sends a Txx packet

   The protocol is only used for files on the host file system and for
I/O on the console.  Character or block special devices, pipes, named
pipes or sockets or any other communication method on the host system
are not supported by this protocol.


File: gdb.info,  Node: Protocol basics,  Next: The F request packet,  Prev: File-I/O Overview,  Up: File-I/O remote protocol extension

Protocol basics
---------------

The File-I/O protocol uses the `F' packet, as request as well as as
reply packet.  Since a File-I/O system call can only occur when GDB is
waiting for the continuing or stepping target, the File-I/O request is
a reply that GDB has to expect as a result of a former `C', `c', `S' or
`s' packet.  This `F' packet contains all information needed to allow
GDB to call the appropriate host system call:

   * A unique identifier for the requested system call.

   * All parameters to the system call.  Pointers are given as addresses
     in the target memory address space.  Pointers to strings are given
     as pointer/length pair.  Numerical values are given as they are.
     Numerical control values are given in a protocol specific
     representation.


   At that point GDB has to perform the following actions.

   * If parameter pointer values are given, which point to data needed
     as input to a system call, GDB requests this data from the target
     with a standard `m' packet request.  This additional communication
     has to be expected by the target implementation and is handled as
     any other `m' packet.

   * GDB translates all value from protocol representation to host
     representation as needed.  Datatypes are coerced into the host
     types.

   * GDB calls the system call

   * It then coerces datatypes back to protocol representation.

   * If pointer parameters in the request packet point to buffer space
     in which a system call is expected to copy data to, the data is
     transmitted to the target using a `M' or `X' packet.  This packet
     has to be expected by the target implementation and is handled as
     any other `M' or `X' packet.


   Eventually GDB replies with another `F' packet which contains all
necessary information for the target to continue.  This at least
contains

   * Return value.

   * `errno', if has been changed by the system call.

   * "Ctrl-C" flag.


   After having done the needed type and value coercion, the target
continues the latest continue or step action.


File: gdb.info,  Node: The F request packet,  Next: The F reply packet,  Prev: Protocol basics,  Up: File-I/O remote protocol extension

The `F' request packet
----------------------

The `F' request packet has the following format:

          `F'CALL-ID`,'PARAMETER...

     CALL-ID is the identifier to indicate the host system call to be
     called.  This is just the name of the function.

     PARAMETER... are the parameters to the system call.


   Parameters are hexadecimal integer values, either the real values in
case of scalar datatypes, as pointers to target buffer space in case of
compound datatypes and unspecified memory areas or as pointer/length
pairs in case of string parameters.  These are appended to the call-id,
each separated from its predecessor by a comma.  All values are
transmitted in ASCII string representation, pointer/length pairs
separated by a slash.


File: gdb.info,  Node: The F reply packet,  Next: Memory transfer,  Prev: The F request packet,  Up: File-I/O remote protocol extension

The `F' reply packet
--------------------

The `F' reply packet has the following format:

          `F'RETCODE`,'ERRNO`,'CTRL-C FLAG`;'CALL SPECIFIC ATTACHMENT

     RETCODE is the return code of the system call as hexadecimal value.

     ERRNO is the errno set by the call, in protocol specific
     representation.  This parameter can be omitted if the call was
     successful.

     CTRL-C FLAG is only send if the user requested a break.  In this
     case, ERRNO must be send as well, even if the call was successful.
     The CTRL-C FLAG itself consists of the character 'C':

          F0,0,C

     or, if the call was interupted before the host call has been
     performed:

          F-1,4,C

     assuming 4 is the protocol specific representation of `EINTR'.



File: gdb.info,  Node: Memory transfer,  Next: The Ctrl-C message,  Prev: The F reply packet,  Up: File-I/O remote protocol extension

Memory transfer
---------------

Structured data which is transferred using a memory read or write as
e.g.  a `struct stat' is expected to be in a protocol specific format
with all scalar multibyte datatypes being big endian.  This should be
done by the target before the `F' packet is sent resp. by GDB before it
transfers memory to the target.  Transferred pointers to structured
data should point to the already coerced data at any time.


File: gdb.info,  Node: The Ctrl-C message,  Next: Console I/O,  Prev: Memory transfer,  Up: File-I/O remote protocol extension

The Ctrl-C message
------------------

A special case is, if the CTRL-C FLAG is set in the GDB reply packet.
In this case the target should behave, as if it had gotten a break
message.  The meaning for the target is "system call interupted by
`SIGINT'".  Consequentially, the target should actually stop (as with a
break message) and return to GDB with a `T02' packet.  In this case,
it's important for the target to know, in which state the system call
was interrupted.  Since this action is by design not an atomic
operation, we have to differ between two cases:

   * The system call hasn't been performed on the host yet.

   * The system call on the host has been finished.


   These two states can be distinguished by the target by the value of
the returned `errno'.  If it's the protocol representation of `EINTR',
the system call hasn't been performed.  This is equivalent to the
`EINTR' handling on POSIX systems.  In any other case, the target may
presume that the system call has been finished -- successful or not --
and should behave as if the break message arrived right after the
system call.

   GDB must behave reliable.  If the system call has not been called
yet, GDB may send the `F' reply immediately, setting `EINTR' as `errno'
in the packet.  If the system call on the host has been finished before
the user requests a break, the full action must be finshed by GDB.
This requires sending `M' or `X' packets as they fit.  The `F' packet
may only be send when either nothing has happened or the full action
has been completed.


File: gdb.info,  Node: Console I/O,  Next: The isatty call,  Prev: The Ctrl-C message,  Up: File-I/O remote protocol extension

Console I/O
-----------

By default and if not explicitely closed by the target system, the file
descriptors 0, 1 and 2 are connected to the GDB console.  Output on the
GDB console is handled as any other file output operation (`write(1,
...)' or `write(2, ...)').  Console input is handled by GDB so that
after the target read request from file descriptor 0 all following
typing is buffered until either one of the following conditions is met:

   * The user presses `Ctrl-C'.  The behaviour is as explained above,
     the `read' system call is treated as finished.

   * The user presses `Enter'.  This is treated as end of input with a
     trailing line feed.

   * The user presses `Ctrl-D'.  This is treated as end of input.  No
     trailing character, especially no Ctrl-D is appended to the input.


   If the user has typed more characters as fit in the buffer given to
the read call, the trailing characters are buffered in GDB until either
another `read(0, ...)' is requested by the target or debugging is
stopped on users request.


File: gdb.info,  Node: The isatty call,  Next: The system call,  Prev: Console I/O,  Up: File-I/O remote protocol extension

The isatty(3) call
------------------

A special case in this protocol is the library call `isatty' which is
implemented as it's own call inside of this protocol.  It returns 1 to
the target if the file descriptor given as parameter is attached to the
GDB console, 0 otherwise.  Implementing through system calls would
require implementing `ioctl' and would be more complex than needed.


File: gdb.info,  Node: The system call,  Next: List of supported calls,  Prev: The isatty call,  Up: File-I/O remote protocol extension

The system(3) call
------------------

The other special case in this protocol is the `system' call which is
implemented as it's own call, too.  GDB is taking over the full task of
calling the necessary host calls to perform the `system' call.  The
return value of `system' is simplified before it's returned to the
target.  Basically, the only signal transmitted back is `EINTR' in case
the user pressed `Ctrl-C'.  Otherwise the return value consists
entirely of the exit status of the called command.

   Due to security concerns, the `system' call is refused to be called
by GDB by default.  The user has to allow this call explicitly by
entering

``set remote system-call-allowed 1''

   Disabling the `system' call is done by

``set remote system-call-allowed 0''

   The current setting is shown by typing

``show remote system-call-allowed''


File: gdb.info,  Node: List of supported calls,  Next: Protocol specific representation of datatypes,  Prev: The system call,  Up: File-I/O remote protocol extension

List of supported calls
-----------------------

* Menu:

* open::
* close::
* read::
* write::
* lseek::
* rename::
* unlink::
* stat/fstat::
* gettimeofday::
* isatty::
* system::


File: gdb.info,  Node: open,  Next: close,  Up: List of supported calls

open
....

Synopsis:
     int open(const char *pathname, int flags);
     int open(const char *pathname, int flags, mode_t mode);
     
Request:
     Fopen,pathptr/len,flags,mode

`flags' is the bitwise or of the following values:

`O_CREAT'
     If the file does not exist it will be created.  The host rules
     apply as far as file ownership and time stamps are concerned.

`O_EXCL'
     When used with O_CREAT, if the file already exists it is an error
     and open() fails.

`O_TRUNC'
     If the file already exists and the open mode allows writing
     (O_RDWR or O_WRONLY is given) it will be truncated to length 0.

`O_APPEND'
     The file is opened in append mode.

`O_RDONLY'
     The file is opened for reading only.

`O_WRONLY'
     The file is opened for writing only.

`O_RDWR'
     The file is opened for reading and writing.

     Each other bit is silently ignored.


`mode' is the bitwise or of the following values:

`S_IRUSR'
     User has read permission.

`S_IWUSR'
     User has write permission.

`S_IRGRP'
     Group has read permission.

`S_IWGRP'
     Group has write permission.

`S_IROTH'
     Others have read permission.

`S_IWOTH'
     Others have write permission.

     Each other bit is silently ignored.


Return value:
     open returns the new file descriptor or -1 if an error
     occured.
     
Errors:


`EEXIST'
     pathname already exists and O_CREAT and O_EXCL were used.

`EISDIR'
     pathname refers to a directory.

`EACCES'
     The requested access is not allowed.

`ENAMETOOLONG'
     pathname was too long.

`ENOENT'
     A directory component in pathname does not exist.

`ENODEV'
     pathname refers to a device, pipe, named pipe or socket.

`EROFS'
     pathname refers to a file on a read-only filesystem and write
     access was requested.

`EFAULT'
     pathname is an invalid pointer value.

`ENOSPC'
     No space on device to create the file.

`EMFILE'
     The process already has the maximum number of files open.

`ENFILE'
     The limit on the total number of files open on the system has been
     reached.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: close,  Next: read,  Prev: open,  Up: List of supported calls

close
.....

Synopsis:
     int close(int fd);
     
Request:
     Fclose,fd
     
Return value:
     close returns zero on success, or -1 if an error occurred.
     
Errors:


`EBADF'
     fd isn't a valid open file descriptor.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: read,  Next: write,  Prev: close,  Up: List of supported calls

read
....

Synopsis:
     int read(int fd, void *buf, unsigned int count);
     
Request:
     Fread,fd,bufptr,count
     
Return value:
     On success, the number of bytes read is returned.
     Zero indicates end of file.  If count is zero, read
     returns zero as well.  On error, -1 is returned.
     
Errors:


`EBADF'
     fd is not a valid file descriptor or is not open for reading.

`EFAULT'
     buf is an invalid pointer value.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: write,  Next: lseek,  Prev: read,  Up: List of supported calls

write
.....

Synopsis:
     int write(int fd, const void *buf, unsigned int count);
     
Request:
     Fwrite,fd,bufptr,count
     
Return value:
     On success, the number of bytes written are returned.
     Zero indicates nothing was written.  On error, -1
     is returned.
     
Errors:


`EBADF'
     fd is not a valid file descriptor or is not open for writing.

`EFAULT'
     buf is an invalid pointer value.

`EFBIG'
     An attempt was made to write a file that exceeds the host specific
     maximum file size allowed.

`ENOSPC'
     No space on device to write the data.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: lseek,  Next: rename,  Prev: write,  Up: List of supported calls

lseek
.....

Synopsis:
     long lseek (int fd, long offset, int flag);
     
Request:
     Flseek,fd,offset,flag

   `flag' is one of:

`SEEK_SET'
     The offset is set to offset bytes.

`SEEK_CUR'
     The offset is set to its current location plus offset bytes.

`SEEK_END'
     The offset is set to the size of the file plus offset bytes.

Return value:
     On success, the resulting unsigned offset in bytes from
     the beginning of the file is returned.  Otherwise, a
     value of -1 is returned.
     
Errors:


`EBADF'
     fd is not a valid open file descriptor.

`ESPIPE'
     fd is associated with the GDB console.

`EINVAL'
     flag is not a proper value.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: rename,  Next: unlink,  Prev: lseek,  Up: List of supported calls

rename
......

Synopsis:
     int rename(const char *oldpath, const char *newpath);
     
Request:
     Frename,oldpathptr/len,newpathptr/len
     
Return value:
     On success, zero is returned.  On error, -1 is returned.
     
Errors:


`EISDIR'
     newpath is an existing directory, but oldpath is not a directory.

`EEXIST'
     newpath is a non-empty directory.

`EBUSY'
     oldpath or newpath is a directory that is in use by some process.

`EINVAL'
     An attempt was made to make a directory a subdirectory of itself.

`ENOTDIR'
     A  component used as a directory in oldpath or new path is not a
     directory.  Or oldpath is a directory and newpath exists but is
     not a directory.

`EFAULT'
     oldpathptr or newpathptr are invalid pointer values.

`EACCES'
     No access to the file or the path of the file.

`ENAMETOOLONG'
     oldpath or newpath was too long.

`ENOENT'
     A directory component in oldpath or newpath does not exist.

`EROFS'
     The file is on a read-only filesystem.

`ENOSPC'
     The device containing the file has no room for the new directory
     entry.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: unlink,  Next: stat/fstat,  Prev: rename,  Up: List of supported calls

unlink
......

Synopsis:
     int unlink(const char *pathname);
     
Request:
     Funlink,pathnameptr/len
     
Return value:
     On success, zero is returned.  On error, -1 is returned.
     
Errors:


`EACCES'
     No access to the file or the path of the file.

`EPERM'
     The system does not allow unlinking of directories.

`EBUSY'
     The file pathname cannot be unlinked because it's being used by
     another process.

`EFAULT'
     pathnameptr is an invalid pointer value.

`ENAMETOOLONG'
     pathname was too long.

`ENOENT'
     A directory component in pathname does not exist.

`ENOTDIR'
     A component of the path is not a directory.

`EROFS'
     The file is on a read-only filesystem.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: stat/fstat,  Next: gettimeofday,  Prev: unlink,  Up: List of supported calls

stat/fstat
..........

Synopsis:
     int stat(const char *pathname, struct stat *buf);
     int fstat(int fd, struct stat *buf);
     
Request:
     Fstat,pathnameptr/len,bufptr
     Ffstat,fd,bufptr
     
Return value:
     On success, zero is returned.  On error, -1 is returned.
     
Errors:


`EBADF'
     fd is not a valid open file.

`ENOENT'
     A directory component in pathname does not exist or the path is an
     empty string.

`ENOTDIR'
     A component of the path is not a directory.

`EFAULT'
     pathnameptr is an invalid pointer value.

`EACCES'
     No access to the file or the path of the file.

`ENAMETOOLONG'
     pathname was too long.

`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: gettimeofday,  Next: isatty,  Prev: stat/fstat,  Up: List of supported calls

gettimeofday
............

Synopsis:
     int gettimeofday(struct timeval *tv, void *tz);
     
Request:
     Fgettimeofday,tvptr,tzptr
     
Return value:
     On success, 0 is returned, -1 otherwise.
     
Errors:


`EINVAL'
     tz is a non-NULL pointer.

`EFAULT'
     tvptr and/or tzptr is an invalid pointer value.


File: gdb.info,  Node: isatty,  Next: system,  Prev: gettimeofday,  Up: List of supported calls

isatty
......

Synopsis:
     int isatty(int fd);
     
Request:
     Fisatty,fd
     
Return value:
     Returns 1 if fd refers to the GDB console, 0 otherwise.
     
Errors:


`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: system,  Prev: isatty,  Up: List of supported calls

system
......

Synopsis:
     int system(const char *command);
     
Request:
     Fsystem,commandptr/len
     
Return value:
     The value returned is -1 on error and the return status
     of the command otherwise.  Only the exit status of the
     command is returned, which is extracted from the hosts
     system return value by calling WEXITSTATUS(retval).
     In case /bin/sh could not be executed, 127 is returned.
     
Errors:


`EINTR'
     The call was interrupted by the user.


File: gdb.info,  Node: Protocol specific representation of datatypes,  Next: Constants,  Prev: List of supported calls,  Up: File-I/O remote protocol extension

Protocol specific representation of datatypes
---------------------------------------------

* Menu:

* Integral datatypes::
* Pointer values::
* struct stat::
* struct timeval::


File: gdb.info,  Node: Integral datatypes,  Next: Pointer values,  Up: Protocol specific representation of datatypes

Integral datatypes
..................

The integral datatypes used in the system calls are

     int, unsigned int, long, unsigned long, mode_t and time_t

   `Int', `unsigned int', `mode_t' and `time_t' are implemented as 32
bit values in this protocol.

   `Long' and `unsigned long' are implemented as 64 bit types.

   *Note Limits::, for corresponding MIN and MAX values (similar to
those in `limits.h') to allow range checking on host and target.

   `time_t' datatypes are defined as seconds since the Epoch.

   All integral datatypes transferred as part of a memory read or write
of a structured datatype e.g. a `struct stat' have to be given in big
endian byte order.


File: gdb.info,  Node: Pointer values,  Next: struct stat,  Prev: Integral datatypes,  Up: Protocol specific representation of datatypes

Pointer values
..............

Pointers to target data are transmitted as they are.  An exception is
made for pointers to buffers for which the length isn't transmitted as
part of the function call, namely strings.  Strings are transmitted as
a pointer/length pair, both as hex values, e.g.

     `1aaf/12'

which is a pointer to data of length 18 bytes at position 0x1aaf.  The
length is defined as the full string length in bytes, including the
trailing null byte.  Example:

     ``hello, world'' at address 0x123456

is transmitted as

     `123456/d'


File: gdb.info,  Node: struct stat,  Next: struct timeval,  Prev: Pointer values,  Up: Protocol specific representation of datatypes

struct stat
...........

The buffer of type struct stat used by the target and GDB is defined as
follows:

     struct stat {
         unsigned int  st_dev;      /* device */
         unsigned int  st_ino;      /* inode */
         mode_t        st_mode;     /* protection */
         unsigned int  st_nlink;    /* number of hard links */
         unsigned int  st_uid;      /* user ID of owner */
         unsigned int  st_gid;      /* group ID of owner */
         unsigned int  st_rdev;     /* device type (if inode device) */
         unsigned long st_size;     /* total size, in bytes */
         unsigned long st_blksize;  /* blocksize for filesystem I/O */
         unsigned long st_blocks;   /* number of blocks allocated */
         time_t        st_atime;    /* time of last access */
         time_t        st_mtime;    /* time of last modification */
         time_t        st_ctime;    /* time of last change */
     };

   The integral datatypes are conforming to the definitions given in the
approriate section (see *Note Integral datatypes::, for details) so this
structure is of size 64 bytes.

   The values of several fields have a restricted meaning and/or range
of values.

     st_dev:     0       file
                 1       console
     
     st_ino:     No valid meaning for the target.  Transmitted unchanged.
     
     st_mode:    Valid mode bits are described in Appendix C.  Any other
                 bits have currently no meaning for the target.
     
     st_uid:     No valid meaning for the target.  Transmitted unchanged.
     
     st_gid:     No valid meaning for the target.  Transmitted unchanged.
     
     st_rdev:    No valid meaning for the target.  Transmitted unchanged.
     
     st_atime, st_mtime, st_ctime:
                 These values have a host and file system dependent
                 accuracy.  Especially on Windows hosts the file systems
                 don't support exact timing values.

   The target gets a struct stat of the above representation and is
responsible to coerce it to the target representation before continuing.

   Note that due to size differences between the host and target
representation of stat members, these members could eventually get
truncated on the target.


File: gdb.info,  Node: struct timeval,  Prev: struct stat,  Up: Protocol specific representation of datatypes

struct timeval
..............

The buffer of type struct timeval used by the target and GDB is defined
as follows:

     struct timeval {
         time_t tv_sec;  /* second */
         long   tv_usec; /* microsecond */
     };

   The integral datatypes are conforming to the definitions given in the
approriate section (see *Note Integral datatypes::, for details) so this
structure is of size 8 bytes.


File: gdb.info,  Node: Constants,  Next: File-I/O Examples,  Prev: Protocol specific representation of datatypes,  Up: File-I/O remote protocol extension

Constants
---------

The following values are used for the constants inside of the protocol.
GDB and target are resposible to translate these values before and
after the call as needed.

* Menu:

* Open flags::
* mode_t values::
* Errno values::
* Lseek flags::
* Limits::


File: gdb.info,  Node: Open flags,  Next: mode_t values,  Up: Constants

Open flags
..........

All values are given in hexadecimal representation.

       O_RDONLY        0x0
       O_WRONLY        0x1
       O_RDWR          0x2
       O_APPEND        0x8
       O_CREAT       0x200
       O_TRUNC       0x400
       O_EXCL        0x800


File: gdb.info,  Node: mode_t values,  Next: Errno values,  Prev: Open flags,  Up: Constants

mode_t values
.............

All values are given in octal representation.

       S_IFREG       0100000
       S_IFDIR        040000
       S_IRUSR          0400
       S_IWUSR          0200
       S_IXUSR          0100
       S_IRGRP           040
       S_IWGRP           020
       S_IXGRP           010
       S_IROTH            04
       S_IWOTH            02
       S_IXOTH            01


File: gdb.info,  Node: Errno values,  Next: Lseek flags,  Prev: mode_t values,  Up: Constants

Errno values
............

All values are given in decimal representation.

       EPERM           1
       ENOENT          2
       EINTR           4
       EBADF           9
       EACCES         13
       EFAULT         14
       EBUSY          16
       EEXIST         17
       ENODEV         19
       ENOTDIR        20
       EISDIR         21
       EINVAL         22
       ENFILE         23
       EMFILE         24
       EFBIG          27
       ENOSPC         28
       ESPIPE         29
       EROFS          30
       ENAMETOOLONG   91
       EUNKNOWN       9999

   EUNKNOWN is used as a fallback error value if a host system returns
any error value not in the list of supported error numbers.


File: gdb.info,  Node: Lseek flags,  Next: Limits,  Prev: Errno values,  Up: Constants

Lseek flags
...........

       SEEK_SET      0
       SEEK_CUR      1
       SEEK_END      2


File: gdb.info,  Node: Limits,  Prev: Lseek flags,  Up: Constants

Limits
......

All values are given in decimal representation.

       INT_MIN       -2147483648
       INT_MAX        2147483647
       UINT_MAX       4294967295
       LONG_MIN      -9223372036854775808
       LONG_MAX       9223372036854775807
       ULONG_MAX      18446744073709551615


File: gdb.info,  Node: File-I/O Examples,  Prev: Constants,  Up: File-I/O remote protocol extension

File-I/O Examples
-----------------

Example sequence of a write call, file descriptor 3, buffer is at target
address 0x1234, 6 bytes should be written:

     <- `Fwrite,3,1234,6'
     _request memory read from target_
     -> `m1234,6'
     <- XXXXXX
     _return "6 bytes written"_
     -> `F6'

   Example sequence of a read call, file descriptor 3, buffer is at
target address 0x1234, 6 bytes should be read:

     <- `Fread,3,1234,6'
     _request memory write to target_
     -> `X1234,6:XXXXXX'
     _return "6 bytes read"_
     -> `F6'

   Example sequence of a read call, call fails on the host due to
invalid file descriptor (EBADF):

     <- `Fread,3,1234,6'
     -> `F-1,9'

   Example sequence of a read call, user presses Ctrl-C before syscall
on host is called:

     <- `Fread,3,1234,6'
     -> `F-1,4,C'
     <- `T02'

   Example sequence of a read call, user presses Ctrl-C after syscall on
host is called:

     <- `Fread,3,1234,6'
     -> `X1234,6:XXXXXX'
     <- `T02'


File: gdb.info,  Node: Agent Expressions,  Next: Copying,  Prev: Remote Protocol,  Up: Top

The GDB Agent Expression Mechanism
**********************************

In some applications, it is not feasable for the debugger to interrupt
the program's execution long enough for the developer to learn anything
helpful about its behavior.  If the program's correctness depends on its
real-time behavior, delays introduced by a debugger might cause the
program to fail, even when the code itself is correct.  It is useful to
be able to observe the program's behavior without interrupting it.

   Using GDB's `trace' and `collect' commands, the user can specify
locations in the program, and arbitrary expressions to evaluate when
those locations are reached.  Later, using the `tfind' command, she can
examine the values those expressions had when the program hit the trace
points.  The expressions may also denote objects in memory --
structures or arrays, for example -- whose values GDB should record;
while visiting a particular tracepoint, the user may inspect those
objects as if they were in memory at that moment.  However, because GDB
records these values without interacting with the user, it can do so
quickly and unobtrusively, hopefully not disturbing the program's
behavior.

   When GDB is debugging a remote target, the GDB "agent" code running
on the target computes the values of the expressions itself.  To avoid
having a full symbolic expression evaluator on the agent, GDB translates
expressions in the source language into a simpler bytecode language, and
then sends the bytecode to the agent; the agent then executes the
bytecode, and records the values for GDB to retrieve later.

   The bytecode language is simple; there are forty-odd opcodes, the
bulk of which are the usual vocabulary of C operands (addition,
subtraction, shifts, and so on) and various sizes of literals and
memory reference operations.  The bytecode interpreter operates
strictly on machine-level values -- various sizes of integers and
floating point numbers -- and requires no information about types or
symbols; thus, the interpreter's internal data structures are simple,
and each bytecode requires only a few native machine instructions to
implement it.  The interpreter is small, and strict limits on the
memory and time required to evaluate an expression are easy to
determine, making it suitable for use by the debugging agent in
real-time applications.

* Menu:

* General Bytecode Design::     Overview of the interpreter.
* Bytecode Descriptions::       What each one does.
* Using Agent Expressions::     How agent expressions fit into the big picture.
* Varying Target Capabilities:: How to discover what the target can do.
* Tracing on Symmetrix::        Special info for implementation on EMC's
                                boxes.
* Rationale::                   Why we did it this way.


File: gdb.info,  Node: General Bytecode Design,  Next: Bytecode Descriptions,  Up: Agent Expressions

General Bytecode Design
=======================

The agent represents bytecode expressions as an array of bytes.  Each
instruction is one byte long (thus the term "bytecode").  Some
instructions are followed by operand bytes; for example, the `goto'
instruction is followed by a destination for the jump.

   The bytecode interpreter is a stack-based machine; most instructions
pop their operands off the stack, perform some operation, and push the
result back on the stack for the next instruction to consume.  Each
element of the stack may contain either a integer or a floating point
value; these values are as many bits wide as the largest integer that
can be directly manipulated in the source language.  Stack elements
carry no record of their type; bytecode could push a value as an
integer, then pop it as a floating point value.  However, GDB will not
generate code which does this.  In C, one might define the type of a
stack element as follows:
     union agent_val {
       LONGEST l;
       DOUBLEST d;
     };

where `LONGEST' and `DOUBLEST' are `typedef' names for the largest
integer and floating point types on the machine.

   By the time the bytecode interpreter reaches the end of the
expression, the value of the expression should be the only value left
on the stack.  For tracing applications, `trace' bytecodes in the
expression will have recorded the necessary data, and the value on the
stack may be discarded.  For other applications, like conditional
breakpoints, the value may be useful.

   Separate from the stack, the interpreter has two registers:
`pc'
     The address of the next bytecode to execute.

`start'
     The address of the start of the bytecode expression, necessary for
     interpreting the `goto' and `if_goto' instructions.


Neither of these registers is directly visible to the bytecode language
itself, but they are useful for defining the meanings of the bytecode
operations.

   There are no instructions to perform side effects on the running
program, or call the program's functions; we assume that these
expressions are only used for unobtrusive debugging, not for patching
the running code.

   Most bytecode instructions do not distinguish between the various
sizes of values, and operate on full-width values; the upper bits of the
values are simply ignored, since they do not usually make a difference
to the value computed.  The exceptions to this rule are:
memory reference instructions (`ref'N)
     There are distinct instructions to fetch different word sizes from
     memory.  Once on the stack, however, the values are treated as
     full-size integers.  They may need to be sign-extended; the `ext'
     instruction exists for this purpose.

the sign-extension instruction (`ext' N)
     These clearly need to know which portion of their operand is to be
     extended to occupy the full length of the word.


   If the interpreter is unable to evaluate an expression completely for
some reason (a memory location is inaccessible, or a divisor is zero,
for example), we say that interpretation "terminates with an error".
This means that the problem is reported back to the interpreter's caller
in some helpful way.  In general, code using agent expressions should
assume that they may attempt to divide by zero, fetch arbitrary memory
locations, and misbehave in other ways.

   Even complicated C expressions compile to a few bytecode
instructions; for example, the expression `x + y * z' would typically
produce code like the following, assuming that `x' and `y' live in
registers, and `z' is a global variable holding a 32-bit `int':
     reg 1
     reg 2
     const32 address of z
     ref32
     ext 32
     mul
     add
     end

   In detail, these mean:
`reg 1'
     Push the value of register 1 (presumably holding `x') onto the
     stack.

`reg 2'
     Push the value of register 2 (holding `y').

`const32 address of z'
     Push the address of `z' onto the stack.

`ref32'
     Fetch a 32-bit word from the address at the top of the stack;
     replace the address on the stack with the value.  Thus, we replace
     the address of `z' with `z''s value.

`ext 32'
     Sign-extend the value on the top of the stack from 32 bits to full
     length.  This is necessary because `z' is a signed integer.

`mul'
     Pop the top two numbers on the stack, multiply them, and push their
     product.  Now the top of the stack contains the value of the
     expression `y * z'.

`add'
     Pop the top two numbers, add them, and push the sum.  Now the top
     of the stack contains the value of `x + y * z'.

`end'
     Stop executing; the value left on the stack top is the value to be
     recorded.



File: gdb.info,  Node: Bytecode Descriptions,  Next: Using Agent Expressions,  Prev: General Bytecode Design,  Up: Agent Expressions

Bytecode Descriptions
=====================

Each bytecode description has the following form:

`add' (0x02): A B => A+B
     Pop the top two stack items, A and B, as integers; push their sum,
     as an integer.


   In this example, `add' is the name of the bytecode, and `(0x02)' is
the one-byte value used to encode the bytecode, in hexidecimal.  The
phrase "A B => A+B" shows the stack before and after the bytecode
executes.  Beforehand, the stack must contain at least two values, A
and B; since the top of the stack is to the right, B is on the top of
the stack, and A is underneath it.  After execution, the bytecode will
have popped A and B from the stack, and replaced them with a single
value, A+B.  There may be other values on the stack below those shown,
but the bytecode affects only those shown.

   Here is another example:

`const8' (0x22) N: => N
     Push the 8-bit integer constant N on the stack, without sign
     extension.


   In this example, the bytecode `const8' takes an operand N directly
from the bytecode stream; the operand follows the `const8' bytecode
itself.  We write any such operands immediately after the name of the
bytecode, before the colon, and describe the exact encoding of the
operand in the bytecode stream in the body of the bytecode description.

   For the `const8' bytecode, there are no stack items given before the
=>; this simply means that the bytecode consumes no values from the
stack.  If a bytecode consumes no values, or produces no values, the
list on either side of the => may be empty.

   If a value is written as A, B, or N, then the bytecode treats it as
an integer.  If a value is written is ADDR, then the bytecode treats it
as an address.

   We do not fully describe the floating point operations here; although
this design can be extended in a clean way to handle floating point
values, they are not of immediate interest to the customer, so we avoid
describing them, to save time.

`float' (0x01): =>
     Prefix for floating-point bytecodes.  Not implemented yet.

`add' (0x02): A B => A+B
     Pop two integers from the stack, and push their sum, as an integer.

`sub' (0x03): A B => A-B
     Pop two integers from the stack, subtract the top value from the
     next-to-top value, and push the difference.

`mul' (0x04): A B => A*B
     Pop two integers from the stack, multiply them, and push the
     product on the stack.  Note that, when one multiplies two N-bit
     numbers yielding another N-bit number, it is irrelevant whether the
     numbers are signed or not; the results are the same.

`div_signed' (0x05): A B => A/B
     Pop two signed integers from the stack; divide the next-to-top
     value by the top value, and push the quotient.  If the divisor is
     zero, terminate with an error.

`div_unsigned' (0x06): A B => A/B
     Pop two unsigned integers from the stack; divide the next-to-top
     value by the top value, and push the quotient.  If the divisor is
     zero, terminate with an error.

`rem_signed' (0x07): A B => A MODULO B
     Pop two signed integers from the stack; divide the next-to-top
     value by the top value, and push the remainder.  If the divisor is
     zero, terminate with an error.

`rem_unsigned' (0x08): A B => A MODULO B
     Pop two unsigned integers from the stack; divide the next-to-top
     value by the top value, and push the remainder.  If the divisor is
     zero, terminate with an error.

`lsh' (0x09): A B => A<<B
     Pop two integers from the stack; let A be the next-to-top value,
     and B be the top value.  Shift A left by B bits, and push the
     result.

`rsh_signed' (0x0a): A B => `(signed)'A>>B
     Pop two integers from the stack; let A be the next-to-top value,
     and B be the top value.  Shift A right by B bits, inserting copies
     of the top bit at the high end, and push the result.

`rsh_unsigned' (0x0b): A B => A>>B
     Pop two integers from the stack; let A be the next-to-top value,
     and B be the top value.  Shift A right by B bits, inserting zero
     bits at the high end, and push the result.

`log_not' (0x0e): A => !A
     Pop an integer from the stack; if it is zero, push the value one;
     otherwise, push the value zero.

`bit_and' (0x0f): A B => A&B
     Pop two integers from the stack, and push their bitwise `and'.

`bit_or' (0x10): A B => A|B
     Pop two integers from the stack, and push their bitwise `or'.

`bit_xor' (0x11): A B => A^B
     Pop two integers from the stack, and push their bitwise
     exclusive-`or'.

`bit_not' (0x12): A => ~A
     Pop an integer from the stack, and push its bitwise complement.

`equal' (0x13): A B => A=B
     Pop two integers from the stack; if they are equal, push the value
     one; otherwise, push the value zero.

`less_signed' (0x14): A B => A<B
     Pop two signed integers from the stack; if the next-to-top value
     is less than the top value, push the value one; otherwise, push
     the value zero.

`less_unsigned' (0x15): A B => A<B
     Pop two unsigned integers from the stack; if the next-to-top value
     is less than the top value, push the value one; otherwise, push
     the value zero.

`ext' (0x16) N: A => A, sign-extended from N bits
     Pop an unsigned value from the stack; treating it as an N-bit
     twos-complement value, extend it to full length.  This means that
     all bits to the left of bit N-1 (where the least significant bit
     is bit 0) are set to the value of bit N-1.  Note that N may be
     larger than or equal to the width of the stack elements of the
     bytecode engine; in this case, the bytecode should have no effect.

     The number of source bits to preserve, N, is encoded as a single
     byte unsigned integer following the `ext' bytecode.

`zero_ext' (0x2a) N: A => A, zero-extended from N bits
     Pop an unsigned value from the stack; zero all but the bottom N
     bits.  This means that all bits to the left of bit N-1 (where the
     least significant bit is bit 0) are set to the value of bit N-1.

     The number of source bits to preserve, N, is encoded as a single
     byte unsigned integer following the `zero_ext' bytecode.

`ref8' (0x17): ADDR => A
`ref16' (0x18): ADDR => A
`ref32' (0x19): ADDR => A
`ref64' (0x1a): ADDR => A
     Pop an address ADDR from the stack.  For bytecode `ref'N, fetch an
     N-bit value from ADDR, using the natural target endianness.  Push
     the fetched value as an unsigned integer.

     Note that ADDR may not be aligned in any particular way; the
     `refN' bytecodes should operate correctly for any address.

     If attempting to access memory at ADDR would cause a processor
     exception of some sort, terminate with an error.

`ref_float' (0x1b): ADDR => D
`ref_double' (0x1c): ADDR => D
`ref_long_double' (0x1d): ADDR => D
`l_to_d' (0x1e): A => D
`d_to_l' (0x1f): D => A
     Not implemented yet.

`dup' (0x28): A => A A
     Push another copy of the stack's top element.

`swap' (0x2b): A B => B A
     Exchange the top two items on the stack.

`pop' (0x29): A =>
     Discard the top value on the stack.

`if_goto' (0x20) OFFSET: A =>
     Pop an integer off the stack; if it is non-zero, branch to the
     given offset in the bytecode string.  Otherwise, continue to the
     next instruction in the bytecode stream.  In other words, if A is
     non-zero, set the `pc' register to `start' + OFFSET.  Thus, an
     offset of zero denotes the beginning of the expression.

     The OFFSET is stored as a sixteen-bit unsigned value, stored
     immediately following the `if_goto' bytecode.  It is always stored
     most significant byte first, regardless of the target's normal
     endianness.  The offset is not guaranteed to fall at any particular
     alignment within the bytecode stream; thus, on machines where
     fetching a 16-bit on an unaligned address raises an exception, you
     should fetch the offset one byte at a time.

`goto' (0x21) OFFSET: =>
     Branch unconditionally to OFFSET; in other words, set the `pc'
     register to `start' + OFFSET.

     The offset is stored in the same way as for the `if_goto' bytecode.

`const8' (0x22) N: => N
`const16' (0x23) N: => N
`const32' (0x24) N: => N
`const64' (0x25) N: => N
     Push the integer constant N on the stack, without sign extension.
     To produce a small negative value, push a small twos-complement
     value, and then sign-extend it using the `ext' bytecode.

     The constant N is stored in the appropriate number of bytes
     following the `const'B bytecode.  The constant N is always stored
     most significant byte first, regardless of the target's normal
     endianness.  The constant is not guaranteed to fall at any
     particular alignment within the bytecode stream; thus, on machines
     where fetching a 16-bit on an unaligned address raises an
     exception, you should fetch N one byte at a time.

`reg' (0x26) N: => A
     Push the value of register number N, without sign extension.  The
     registers are numbered following GDB's conventions.

     The register number N is encoded as a 16-bit unsigned integer
     immediately following the `reg' bytecode.  It is always stored most
     significant byte first, regardless of the target's normal
     endianness.  The register number is not guaranteed to fall at any
     particular alignment within the bytecode stream; thus, on machines
     where fetching a 16-bit on an unaligned address raises an
     exception, you should fetch the register number one byte at a time.

`trace' (0x0c): ADDR SIZE =>
     Record the contents of the SIZE bytes at ADDR in a trace buffer,
     for later retrieval by GDB.

`trace_quick' (0x0d) SIZE: ADDR => ADDR
     Record the contents of the SIZE bytes at ADDR in a trace buffer,
     for later retrieval by GDB.  SIZE is a single byte unsigned
     integer following the `trace' opcode.

     This bytecode is equivalent to the sequence `dup const8 SIZE
     trace', but we provide it anyway to save space in bytecode strings.

`trace16' (0x30) SIZE: ADDR => ADDR
     Identical to trace_quick, except that SIZE is a 16-bit big-endian
     unsigned integer, not a single byte.  This should probably have
     been named `trace_quick16', for consistency.

`end' (0x27): =>
     Stop executing bytecode; the result should be the top element of
     the stack.  If the purpose of the expression was to compute an
     lvalue or a range of memory, then the next-to-top of the stack is
     the lvalue's address, and the top of the stack is the lvalue's
     size, in bytes.



File: gdb.info,  Node: Using Agent Expressions,  Next: Varying Target Capabilities,  Prev: Bytecode Descriptions,  Up: Agent Expressions

Using Agent Expressions
=======================

Here is a sketch of a full non-stop debugging cycle, showing how agent
expressions fit into the process.

   * The user selects trace points in the program's code at which GDB
     should collect data.

   * The user specifies expressions to evaluate at each trace point.
     These expressions may denote objects in memory, in which case
     those objects' contents are recorded as the program runs, or
     computed values, in which case the values themselves are recorded.

   * GDB transmits the tracepoints and their associated expressions to
     the GDB agent, running on the debugging target.

   * The agent arranges to be notified when a trace point is hit.  Note
     that, on some systems, the target operating system is completely
     responsible for collecting the data; see *Note Tracing on
     Symmetrix::.

   * When execution on the target reaches a trace point, the agent
     evaluates the expressions associated with that trace point, and
     records the resulting values and memory ranges.

   * Later, when the user selects a given trace event and inspects the
     objects and expression values recorded, GDB talks to the agent to
     retrieve recorded data as necessary to meet the user's requests.
     If the user asks to see an object whose contents have not been
     recorded, GDB reports an error.



File: gdb.info,  Node: Varying Target Capabilities,  Next: Tracing on Symmetrix,  Prev: Using Agent Expressions,  Up: Agent Expressions

Varying Target Capabilities
===========================

Some targets don't support floating-point, and some would rather not
have to deal with `long long' operations.  Also, different targets will
have different stack sizes, and different bytecode buffer lengths.

   Thus, GDB needs a way to ask the target about itself.  We haven't
worked out the details yet, but in general, GDB should be able to send
the target a packet asking it to describe itself.  The reply should be a
packet whose length is explicit, so we can add new information to the
packet in future revisions of the agent, without confusing old versions
of GDB, and it should contain a version number.  It should contain at
least the following information:

   * whether floating point is supported

   * whether `long long' is supported

   * maximum acceptable size of bytecode stack

   * maximum acceptable length of bytecode expressions

   * which registers are actually available for collection

   * whether the target supports disabled tracepoints



File: gdb.info,  Node: Tracing on Symmetrix,  Next: Rationale,  Prev: Varying Target Capabilities,  Up: Agent Expressions

Tracing on Symmetrix
====================

This section documents the API used by the GDB agent to collect data on
Symmetrix systems.

   Cygnus originally implemented these tracing features to help EMC
Corporation debug their Symmetrix high-availability disk drives.  The
Symmetrix application code already includes substantial tracing
facilities; the GDB agent for the Symmetrix system uses those facilities
for its own data collection, via the API described here.

 - Function: DTC_RESPONSE adbg_find_memory_in_frame (FRAME_DEF *FRAME,
          char *ADDRESS, char **BUFFER, unsigned int *SIZE)
     Search the trace frame FRAME for memory saved from ADDRESS.  If
     the memory is available, provide the address of the buffer holding
     it; otherwise, provide the address of the next saved area.

        * If the memory at ADDRESS was saved in FRAME, set `*BUFFER' to
          point to the buffer in which that memory was saved, set
          `*SIZE' to the number of bytes from ADDRESS that are saved at
          `*BUFFER', and return `OK_TARGET_RESPONSE'.  (Clearly, in
          this case, the function will always set `*SIZE' to a value
          greater than zero.)

        * If FRAME does not record any memory at ADDRESS, set `*SIZE'
          to the distance from ADDRESS to the start of the saved region
          with the lowest address higher than ADDRESS.  If there is no
          memory saved from any higher address, set `*SIZE' to zero.
          Return `NOT_FOUND_TARGET_RESPONSE'.

     These two possibilities allow the caller to either retrieve the
     data, or walk the address space to the next saved area.

   This function allows the GDB agent to map the regions of memory
saved in a particular frame, and retrieve their contents efficiently.

   This function also provides a clean interface between the GDB agent
and the Symmetrix tracing structures, making it easier to adapt the GDB
agent to future versions of the Symmetrix system, and vice versa.  This
function searches all data saved in FRAME, whether the data is there at
the request of a bytecode expression, or because it falls in one of the
format's memory ranges, or because it was saved from the top of the
stack.  EMC can arbitrarily change and enhance the tracing mechanism,
but as long as this function works properly, all collected memory is
visible to GDB.

   The function itself is straightforward to implement.  A single pass
over the trace frame's stack area, memory ranges, and expression blocks
can yield the address of the buffer (if the requested address was
saved), and also note the address of the next higher range of memory,
to be returned when the search fails.

   As an example, suppose the trace frame `f' has saved sixteen bytes
from address `0x8000' in a buffer at `0x1000', and thirty-two bytes
from address `0xc000' in a buffer at `0x1010'.  Here are some sample
calls, and the effect each would have:

`adbg_find_memory_in_frame (f, (char*) 0x8000, &buffer, &size)'
     This would set `buffer' to `0x1000', set `size' to sixteen, and
     return `OK_TARGET_RESPONSE', since `f' saves sixteen bytes from
     `0x8000' at `0x1000'.

`adbg_find_memory_in_frame (f, (char *) 0x8004, &buffer, &size)'
     This would set `buffer' to `0x1004', set `size' to twelve, and
     return `OK_TARGET_RESPONSE', since `f' saves the twelve bytes from
     `0x8004' starting four bytes into the buffer at `0x1000'.  This
     shows that request addresses may fall in the middle of saved
     areas; the function should return the address and size of the
     remainder of the buffer.

`adbg_find_memory_in_frame (f, (char *) 0x8100, &buffer, &size)'
     This would set `size' to `0x3f00' and return
     `NOT_FOUND_TARGET_RESPONSE', since there is no memory saved in `f'
     from the address `0x8100', and the next memory available is at
     `0x8100 + 0x3f00', or `0xc000'.  This shows that request addresses
     may fall outside of all saved memory ranges; the function should
     indicate the next saved area, if any.

`adbg_find_memory_in_frame (f, (char *) 0x7000, &buffer, &size)'
     This would set `size' to `0x1000' and return
     `NOT_FOUND_TARGET_RESPONSE', since the next saved memory is at
     `0x7000 + 0x1000', or `0x8000'.

`adbg_find_memory_in_frame (f, (char *) 0xf000, &buffer, &size)'
     This would set `size' to zero, and return
     `NOT_FOUND_TARGET_RESPONSE'.  This shows how the function tells the
     caller that no further memory ranges have been saved.


   As another example, here is a function which will print out the
addresses of all memory saved in the trace frame `frame' on the
Symmetrix INLINES console:
     void
     print_frame_addresses (FRAME_DEF *frame)
     {
       char *addr;
       char *buffer;
       unsigned long size;
     
       addr = 0;
       for (;;)
         {
           /* Either find out how much memory we have here, or discover
              where the next saved region is.  */
           if (adbg_find_memory_in_frame (frame, addr, &buffer, &size)
               == OK_TARGET_RESPONSE)
             printp ("saved %x to %x\n", addr, addr + size);
           if (size == 0)
             break;
           addr += size;
         }
     }

   Note that there is not necessarily any connection between the order
in which the data is saved in the trace frame, and the order in which
`adbg_find_memory_in_frame' will return those memory ranges.  The code
above will always print the saved memory regions in order of increasing
address, while the underlying frame structure might store the data in a
random order.

   [[This section should cover the rest of the Symmetrix functions the
stub relies upon, too.]]


File: gdb.info,  Node: Rationale,  Prev: Tracing on Symmetrix,  Up: Agent Expressions

Rationale
=========

Some of the design decisions apparent above are arguable.

What about stack overflow/underflow?
     GDB should be able to query the target to discover its stack size.
     Given that information, GDB can determine at translation time
     whether a given expression will overflow the stack.  But this spec
     isn't about what kinds of error-checking GDB ought to do.

Why are you doing everything in LONGEST?
     Speed isn't important, but agent code size is; using LONGEST
     brings in a bunch of support code to do things like division, etc.
     So this is a serious concern.

     First, note that you don't need different bytecodes for different
     operand sizes.  You can generate code without _knowing_ how big the
     stack elements actually are on the target.  If the target only
     supports 32-bit ints, and you don't send any 64-bit bytecodes,
     everything just works.  The observation here is that the MIPS and
     the Alpha have only fixed-size registers, and you can still get
     C's semantics even though most instructions only operate on
     full-sized words.  You just need to make sure everything is
     properly sign-extended at the right times.  So there is no need
     for 32- and 64-bit variants of the bytecodes.  Just implement
     everything using the largest size you support.

     GDB should certainly check to see what sizes the target supports,
     so the user can get an error earlier, rather than later.  But this
     information is not necessary for correctness.

Why don't you have `>' or `<=' operators?
     I want to keep the interpreter small, and we don't need them.  We
     can combine the `less_' opcodes with `log_not', and swap the order
     of the operands, yielding all four asymmetrical comparison
     operators.  For example, `(x <= y)' is `! (x > y)', which is `! (y
     < x)'.

Why do you have `log_not'?
Why do you have `ext'?
Why do you have `zero_ext'?
     These are all easily synthesized from other instructions, but I
     expect them to be used frequently, and they're simple, so I
     include them to keep bytecode strings short.

     `log_not' is equivalent to `const8 0 equal'; it's used in half the
     relational operators.

     `ext N' is equivalent to `const8 S-N lsh const8 S-N rsh_signed',
     where S is the size of the stack elements; it follows `refM' and
     REG bytecodes when the value should be signed.  See the next
     bulleted item.

     `zero_ext N' is equivalent to `constM MASK log_and'; it's used
     whenever we push the value of a register, because we can't assume
     the upper bits of the register aren't garbage.

Why not have sign-extending variants of the `ref' operators?
     Because that would double the number of `ref' operators, and we
     need the `ext' bytecode anyway for accessing bitfields.

Why not have constant-address variants of the `ref' operators?
     Because that would double the number of `ref' operators again, and
     `const32 ADDRESS ref32' is only one byte longer.

Why do the `refN' operators have to support unaligned fetches?
     GDB will generate bytecode that fetches multi-byte values at
     unaligned addresses whenever the executable's debugging
     information tells it to.  Furthermore, GDB does not know the value
     the pointer will have when GDB generates the bytecode, so it
     cannot determine whether a particular fetch will be aligned or not.

     In particular, structure bitfields may be several bytes long, but
     follow no alignment rules; members of packed structures are not
     necessarily aligned either.

     In general, there are many cases where unaligned references occur
     in correct C code, either at the programmer's explicit request, or
     at the compiler's discretion.  Thus, it is simpler to make the GDB
     agent bytecodes work correctly in all circumstances than to make
     GDB guess in each case whether the compiler did the usual thing.

Why are there no side-effecting operators?
     Because our current client doesn't want them?  That's a cheap
     answer.  I think the real answer is that I'm afraid of
     implementing function calls.  We should re-visit this issue after
     the present contract is delivered.

Why aren't the `goto' ops PC-relative?
     The interpreter has the base address around anyway for PC bounds
     checking, and it seemed simpler.

Why is there only one offset size for the `goto' ops?
     Offsets are currently sixteen bits.  I'm not happy with this
     situation either:

     Suppose we have multiple branch ops with different offset sizes.
     As I generate code left-to-right, all my jumps are forward jumps
     (there are no loops in expressions), so I never know the target
     when I emit the jump opcode.  Thus, I have to either always assume
     the largest offset size, or do jump relaxation on the code after I
     generate it, which seems like a big waste of time.

     I can imagine a reasonable expression being longer than 256 bytes.
     I can't imagine one being longer than 64k.  Thus, we need 16-bit
     offsets.  This kind of reasoning is so bogus, but relaxation is
     pathetic.

     The other approach would be to generate code right-to-left.  Then
     I'd always know my offset size.  That might be fun.

Where is the function call bytecode?
     When we add side-effects, we should add this.

Why does the `reg' bytecode take a 16-bit register number?
     Intel's IA-64 architecture has 128 general-purpose registers, and
     128 floating-point registers, and I'm sure it has some random
     control registers.

Why do we need `trace' and `trace_quick'?
     Because GDB needs to record all the memory contents and registers
     an expression touches.  If the user wants to evaluate an expression
     `x->y->z', the agent must record the values of `x' and `x->y' as
     well as the value of `x->y->z'.

Don't the `trace' bytecodes make the interpreter less general?
     They do mean that the interpreter contains special-purpose code,
     but that doesn't mean the interpreter can only be used for that
     purpose.  If an expression doesn't use the `trace' bytecodes, they
     don't get in its way.

Why doesn't `trace_quick' consume its arguments the way everything else does?
     In general, you do want your operators to consume their arguments;
     it's consistent, and generally reduces the amount of stack
     rearrangement necessary.  However, `trace_quick' is a kludge to
     save space; it only exists so we needn't write `dup const8 SIZE
     trace' before every memory reference.  Therefore, it's okay for it
     not to consume its arguments; it's meant for a specific context in
     which we know exactly what it should do with the stack.  If we're
     going to have a kludge, it should be an effective kludge.

Why does `trace16' exist?
     That opcode was added by the customer that contracted Cygnus for
     the data tracing work.  I personally think it is unnecessary;
     objects that large will be quite rare, so it is okay to use `dup
     const16 SIZE trace' in those cases.

     Whatever we decide to do with `trace16', we should at least leave
     opcode 0x30 reserved, to remain compatible with the customer who
     added it.



File: gdb.info,  Node: Copying,  Next: GNU Free Documentation License,  Prev: Agent Expressions,  Up: Top

GNU GENERAL PUBLIC LICENSE
**************************

                         Version 2, June 1991
     Copyright (C) 1989, 1991 Free Software Foundation, Inc.
     59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
     
     Everyone is permitted to copy and distribute verbatim copies
     of this license document, but changing it is not allowed.

Preamble
========

The licenses for most software are designed to take away your freedom
to share and change it.  By contrast, the GNU General Public License is
intended to guarantee your freedom to share and change free
software--to make sure the software is free for all its users.  This
General Public License applies to most of the Free Software
Foundation's software and to any other program whose authors commit to
using it.  (Some other Free Software Foundation software is covered by
the GNU Library General Public License instead.)  You can apply it to
your programs, too.

   When we speak of free software, we are referring to freedom, not
price.  Our General Public Licenses are designed to make sure that you
have the freedom to distribute copies of free software (and charge for
this service if you wish), that you receive source code or can get it
if you want it, that you can change the software or use pieces of it in
new free programs; and that you know you can do these things.

   To protect your rights, we need to make restrictions that forbid
anyone to deny you these rights or to ask you to surrender the rights.
These restrictions translate to certain responsibilities for you if you
distribute copies of the software, or if you modify it.

   For example, if you distribute copies of such a program, whether
gratis or for a fee, you must give the recipients all the rights that
you have.  You must make sure that they, too, receive or can get the
source code.  And you must show them these terms so they know their
rights.

   We protect your rights with two steps: (1) copyright the software,
and (2) offer you this license which gives you legal permission to copy,
distribute and/or modify the software.

   Also, for each author's protection and ours, we want to make certain
that everyone understands that there is no warranty for this free
software.  If the software is modified by someone else and passed on, we
want its recipients to know that what they have is not the original, so
that any problems introduced by others will not reflect on the original
authors' reputations.

   Finally, any free program is threatened constantly by software
patents.  We wish to avoid the danger that redistributors of a free
program will individually obtain patent licenses, in effect making the
program proprietary.  To prevent this, we have made it clear that any
patent must be licensed for everyone's free use or not licensed at all.

   The precise terms and conditions for copying, distribution and
modification follow.

    TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
  0. This License applies to any program or other work which contains a
     notice placed by the copyright holder saying it may be distributed
     under the terms of this General Public License.  The "Program",
     below, refers to any such program or work, and a "work based on
     the Program" means either the Program or any derivative work under
     copyright law: that is to say, a work containing the Program or a
     portion of it, either verbatim or with modifications and/or
     translated into another language.  (Hereinafter, translation is
     included without limitation in the term "modification".)  Each
     licensee is addressed as "you".

     Activities other than copying, distribution and modification are
     not covered by this License; they are outside its scope.  The act
     of running the Program is not restricted, and the output from the
     Program is covered only if its contents constitute a work based on
     the Program (independent of having been made by running the
     Program).  Whether that is true depends on what the Program does.

  1. You may copy and distribute verbatim copies of the Program's
     source code as you receive it, in any medium, provided that you
     conspicuously and appropriately publish on each copy an appropriate
     copyright notice and disclaimer of warranty; keep intact all the
     notices that refer to this License and to the absence of any
     warranty; and give any other recipients of the Program a copy of
     this License along with the Program.

     You may charge a fee for the physical act of transferring a copy,
     and you may at your option offer warranty protection in exchange
     for a fee.

  2. You may modify your copy or copies of the Program or any portion
     of it, thus forming a work based on the Program, and copy and
     distribute such modifications or work under the terms of Section 1
     above, provided that you also meet all of these conditions:

       a. You must cause the modified files to carry prominent notices
          stating that you changed the files and the date of any change.

       b. You must cause any work that you distribute or publish, that
          in whole or in part contains or is derived from the Program
          or any part thereof, to be licensed as a whole at no charge
          to all third parties under the terms of this License.

       c. If the modified program normally reads commands interactively
          when run, you must cause it, when started running for such
          interactive use in the most ordinary way, to print or display
          an announcement including an appropriate copyright notice and
          a notice that there is no warranty (or else, saying that you
          provide a warranty) and that users may redistribute the
          program under these conditions, and telling the user how to
          view a copy of this License.  (Exception: if the Program
          itself is interactive but does not normally print such an
          announcement, your work based on the Program is not required
          to print an announcement.)

     These requirements apply to the modified work as a whole.  If
     identifiable sections of that work are not derived from the
     Program, and can be reasonably considered independent and separate
     works in themselves, then this License, and its terms, do not
     apply to those sections when you distribute them as separate
     works.  But when you distribute the same sections as part of a
     whole which is a work based on the Program, the distribution of
     the whole must be on the terms of this License, whose permissions
     for other licensees extend to the entire whole, and thus to each
     and every part regardless of who wrote it.

     Thus, it is not the intent of this section to claim rights or
     contest your rights to work written entirely by you; rather, the
     intent is to exercise the right to control the distribution of
     derivative or collective works based on the Program.

     In addition, mere aggregation of another work not based on the
     Program with the Program (or with a work based on the Program) on
     a volume of a storage or distribution medium does not bring the
     other work under the scope of this License.

  3. You may copy and distribute the Program (or a work based on it,
     under Section 2) in object code or executable form under the terms
     of Sections 1 and 2 above provided that you also do one of the
     following:

       a. Accompany it with the complete corresponding machine-readable
          source code, which must be distributed under the terms of
          Sections 1 and 2 above on a medium customarily used for
          software interchange; or,

       b. Accompany it with a written offer, valid for at least three
          years, to give any third party, for a charge no more than your
          cost of physically performing source distribution, a complete
          machine-readable copy of the corresponding source code, to be
          distributed under the terms of Sections 1 and 2 above on a
          medium customarily used for software interchange; or,

       c. Accompany it with the information you received as to the offer
          to distribute corresponding source code.  (This alternative is
          allowed only for noncommercial distribution and only if you
          received the program in object code or executable form with
          such an offer, in accord with Subsection b above.)

     The source code for a work means the preferred form of the work for
     making modifications to it.  For an executable work, complete
     source code means all the source code for all modules it contains,
     plus any associated interface definition files, plus the scripts
     used to control compilation and installation of the executable.
     However, as a special exception, the source code distributed need
     not include anything that is normally distributed (in either
     source or binary form) with the major components (compiler,
     kernel, and so on) of the operating system on which the executable
     runs, unless that component itself accompanies the executable.

     If distribution of executable or object code is made by offering
     access to copy from a designated place, then offering equivalent
     access to copy the source code from the same place counts as
     distribution of the source code, even though third parties are not
     compelled to copy the source along with the object code.

  4. You may not copy, modify, sublicense, or distribute the Program
     except as expressly provided under this License.  Any attempt
     otherwise to copy, modify, sublicense or distribute the Program is
     void, and will automatically terminate your rights under this
     License.  However, parties who have received copies, or rights,
     from you under this License will not have their licenses
     terminated so long as such parties remain in full compliance.

  5. You are not required to accept this License, since you have not
     signed it.  However, nothing else grants you permission to modify
     or distribute the Program or its derivative works.  These actions
     are prohibited by law if you do not accept this License.
     Therefore, by modifying or distributing the Program (or any work
     based on the Program), you indicate your acceptance of this
     License to do so, and all its terms and conditions for copying,
     distributing or modifying the Program or works based on it.

  6. Each time you redistribute the Program (or any work based on the
     Program), the recipient automatically receives a license from the
     original licensor to copy, distribute or modify the Program
     subject to these terms and conditions.  You may not impose any
     further restrictions on the recipients' exercise of the rights
     granted herein.  You are not responsible for enforcing compliance
     by third parties to this License.

  7. If, as a consequence of a court judgment or allegation of patent
     infringement or for any other reason (not limited to patent
     issues), conditions are imposed on you (whether by court order,
     agreement or otherwise) that contradict the conditions of this
     License, they do not excuse you from the conditions of this
     License.  If you cannot distribute so as to satisfy simultaneously
     your obligations under this License and any other pertinent
     obligations, then as a consequence you may not distribute the
     Program at all.  For example, if a patent license would not permit
     royalty-free redistribution of the Program by all those who
     receive copies directly or indirectly through you, then the only
     way you could satisfy both it and this License would be to refrain
     entirely from distribution of the Program.

     If any portion of this section is held invalid or unenforceable
     under any particular circumstance, the balance of the section is
     intended to apply and the section as a whole is intended to apply
     in other circumstances.

     It is not the purpose of this section to induce you to infringe any
     patents or other property right claims or to contest validity of
     any such claims; this section has the sole purpose of protecting
     the integrity of the free software distribution system, which is
     implemented by public license practices.  Many people have made
     generous contributions to the wide range of software distributed
     through that system in reliance on consistent application of that
     system; it is up to the author/donor to decide if he or she is
     willing to distribute software through any other system and a
     licensee cannot impose that choice.

     This section is intended to make thoroughly clear what is believed
     to be a consequence of the rest of this License.

  8. If the distribution and/or use of the Program is restricted in
     certain countries either by patents or by copyrighted interfaces,
     the original copyright holder who places the Program under this
     License may add an explicit geographical distribution limitation
     excluding those countries, so that distribution is permitted only
     in or among countries not thus excluded.  In such case, this
     License incorporates the limitation as if written in the body of
     this License.

  9. The Free Software Foundation may publish revised and/or new
     versions of the General Public License from time to time.  Such
     new versions will be similar in spirit to the present version, but
     may differ in detail to address new problems or concerns.

     Each version is given a distinguishing version number.  If the
     Program specifies a version number of this License which applies
     to it and "any later version", you have the option of following
     the terms and conditions either of that version or of any later
     version published by the Free Software Foundation.  If the Program
     does not specify a version number of this License, you may choose
     any version ever published by the Free Software Foundation.

 10. If you wish to incorporate parts of the Program into other free
     programs whose distribution conditions are different, write to the
     author to ask for permission.  For software which is copyrighted
     by the Free Software Foundation, write to the Free Software
     Foundation; we sometimes make exceptions for this.  Our decision
     will be guided by the two goals of preserving the free status of
     all derivatives of our free software and of promoting the sharing
     and reuse of software generally.

                                NO WARRANTY

 11. BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO
     WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE
     LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT
     HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS" WITHOUT
     WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT
     NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
     FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS TO THE
     QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE
     PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY
     SERVICING, REPAIR OR CORRECTION.

 12. IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN
     WRITING WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY
     MODIFY AND/OR REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE
     LIABLE TO YOU FOR DAMAGES, INCLUDING ANY GENERAL, SPECIAL,
     INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR
     INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO LOSS OF
     DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU
     OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY
     OTHER PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN
     ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

                      END OF TERMS AND CONDITIONS

How to Apply These Terms to Your New Programs
=============================================

If you develop a new program, and you want it to be of the greatest
possible use to the public, the best way to achieve this is to make it
free software which everyone can redistribute and change under these
terms.

   To do so, attach the following notices to the program.  It is safest
to attach them to the start of each source file to most effectively
convey the exclusion of warranty; and each file should have at least
the "copyright" line and a pointer to where the full notice is found.

     ONE LINE TO GIVE THE PROGRAM'S NAME AND A BRIEF IDEA OF WHAT IT DOES.
     Copyright (C) YEAR  NAME OF AUTHOR
     
     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.
     
     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.
     
     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.

   Also add information on how to contact you by electronic and paper
mail.

   If the program is interactive, make it output a short notice like
this when it starts in an interactive mode:

     Gnomovision version 69, Copyright (C) YEAR NAME OF AUTHOR
     Gnomovision comes with ABSOLUTELY NO WARRANTY; for details
     type `show w'.
     This is free software, and you are welcome to redistribute it
     under certain conditions; type `show c' for details.

   The hypothetical commands `show w' and `show c' should show the
appropriate parts of the General Public License.  Of course, the
commands you use may be called something other than `show w' and `show
c'; they could even be mouse-clicks or menu items--whatever suits your
program.

   You should also get your employer (if you work as a programmer) or
your school, if any, to sign a "copyright disclaimer" for the program,
if necessary.  Here is a sample; alter the names:

     Yoyodyne, Inc., hereby disclaims all copyright interest in the program
     `Gnomovision' (which makes passes at compilers) written by James Hacker.
     
     SIGNATURE OF TY COON, 1 April 1989
     Ty Coon, President of Vice

   This General Public License does not permit incorporating your
program into proprietary programs.  If your program is a subroutine
library, you may consider it more useful to permit linking proprietary
applications with the library.  If this is what you want to do, use the
GNU Library General Public License instead of this License.


File: gdb.info,  Node: GNU Free Documentation License,  Next: Index,  Prev: Copying,  Up: Top

GNU Free Documentation License
******************************

                      Version 1.2, November 2002
     Copyright (C) 2000,2001,2002 Free Software Foundation, Inc.
     59 Temple Place, Suite 330, Boston, MA  02111-1307, USA
     
     Everyone is permitted to copy and distribute verbatim copies
     of this license document, but changing it is not allowed.

  0. PREAMBLE

     The purpose of this License is to make a manual, textbook, or other
     functional and useful document "free" in the sense of freedom: to
     assure everyone the effective freedom to copy and redistribute it,
     with or without modifying it, either commercially or
     noncommercially.  Secondarily, this License preserves for the
     author and publisher a way to get credit for their work, while not
     being considered responsible for modifications made by others.

     This License is a kind of "copyleft", which means that derivative
     works of the document must themselves be free in the same sense.
     It complements the GNU General Public License, which is a copyleft
     license designed for free software.

     We have designed this License in order to use it for manuals for
     free software, because free software needs free documentation: a
     free program should come with manuals providing the same freedoms
     that the software does.  But this License is not limited to
     software manuals; it can be used for any textual work, regardless
     of subject matter or whether it is published as a printed book.
     We recommend this License principally for works whose purpose is
     instruction or reference.

  1. APPLICABILITY AND DEFINITIONS

     This License applies to any manual or other work, in any medium,
     that contains a notice placed by the copyright holder saying it
     can be distributed under the terms of this License.  Such a notice
     grants a world-wide, royalty-free license, unlimited in duration,
     to use that work under the conditions stated herein.  The
     "Document", below, refers to any such manual or work.  Any member
     of the public is a licensee, and is addressed as "you".  You
     accept the license if you copy, modify or distribute the work in a
     way requiring permission under copyright law.

     A "Modified Version" of the Document means any work containing the
     Document or a portion of it, either copied verbatim, or with
     modifications and/or translated into another language.

     A "Secondary Section" is a named appendix or a front-matter section
     of the Document that deals exclusively with the relationship of the
     publishers or authors of the Document to the Document's overall
     subject (or to related matters) and contains nothing that could
     fall directly within that overall subject.  (Thus, if the Document
     is in part a textbook of mathematics, a Secondary Section may not
     explain any mathematics.)  The relationship could be a matter of
     historical connection with the subject or with related matters, or
     of legal, commercial, philosophical, ethical or political position
     regarding them.

     The "Invariant Sections" are certain Secondary Sections whose
     titles are designated, as being those of Invariant Sections, in
     the notice that says that the Document is released under this
     License.  If a section does not fit the above definition of
     Secondary then it is not allowed to be designated as Invariant.
     The Document may contain zero Invariant Sections.  If the Document
     does not identify any Invariant Sections then there are none.

     The "Cover Texts" are certain short passages of text that are
     listed, as Front-Cover Texts or Back-Cover Texts, in the notice
     that says that the Document is released under this License.  A
     Front-Cover Text may be at most 5 words, and a Back-Cover Text may
     be at most 25 words.

     A "Transparent" copy of the Document means a machine-readable copy,
     represented in a format whose specification is available to the
     general public, that is suitable for revising the document
     straightforwardly with generic text editors or (for images
     composed of pixels) generic paint programs or (for drawings) some
     widely available drawing editor, and that is suitable for input to
     text formatters or for automatic translation to a variety of
     formats suitable for input to text formatters.  A copy made in an
     otherwise Transparent file format whose markup, or absence of
     markup, has been arranged to thwart or discourage subsequent
     modification by readers is not Transparent.  An image format is
     not Transparent if used for any substantial amount of text.  A
     copy that is not "Transparent" is called "Opaque".

     Examples of suitable formats for Transparent copies include plain
     ASCII without markup, Texinfo input format, LaTeX input format,
     SGML or XML using a publicly available DTD, and
     standard-conforming simple HTML, PostScript or PDF designed for
     human modification.  Examples of transparent image formats include
     PNG, XCF and JPG.  Opaque formats include proprietary formats that
     can be read and edited only by proprietary word processors, SGML or
     XML for which the DTD and/or processing tools are not generally
     available, and the machine-generated HTML, PostScript or PDF
     produced by some word processors for output purposes only.

     The "Title Page" means, for a printed book, the title page itself,
     plus such following pages as are needed to hold, legibly, the
     material this License requires to appear in the title page.  For
     works in formats which do not have any title page as such, "Title
     Page" means the text near the most prominent appearance of the
     work's title, preceding the beginning of the body of the text.

     A section "Entitled XYZ" means a named subunit of the Document
     whose title either is precisely XYZ or contains XYZ in parentheses
     following text that translates XYZ in another language.  (Here XYZ
     stands for a specific section name mentioned below, such as
     "Acknowledgements", "Dedications", "Endorsements", or "History".)
     To "Preserve the Title" of such a section when you modify the
     Document means that it remains a section "Entitled XYZ" according
     to this definition.

     The Document may include Warranty Disclaimers next to the notice
     which states that this License applies to the Document.  These
     Warranty Disclaimers are considered to be included by reference in
     this License, but only as regards disclaiming warranties: any other
     implication that these Warranty Disclaimers may have is void and
     has no effect on the meaning of this License.

  2. VERBATIM COPYING

     You may copy and distribute the Document in any medium, either
     commercially or noncommercially, provided that this License, the
     copyright notices, and the license notice saying this License
     applies to the Document are reproduced in all copies, and that you
     add no other conditions whatsoever to those of this License.  You
     may not use technical measures to obstruct or control the reading
     or further copying of the copies you make or distribute.  However,
     you may accept compensation in exchange for copies.  If you
     distribute a large enough number of copies you must also follow
     the conditions in section 3.

     You may also lend copies, under the same conditions stated above,
     and you may publicly display copies.

  3. COPYING IN QUANTITY

     If you publish printed copies (or copies in media that commonly
     have printed covers) of the Document, numbering more than 100, and
     the Document's license notice requires Cover Texts, you must
     enclose the copies in covers that carry, clearly and legibly, all
     these Cover Texts: Front-Cover Texts on the front cover, and
     Back-Cover Texts on the back cover.  Both covers must also clearly
     and legibly identify you as the publisher of these copies.  The
     front cover must present the full title with all words of the
     title equally prominent and visible.  You may add other material
     on the covers in addition.  Copying with changes limited to the
     covers, as long as they preserve the title of the Document and
     satisfy these conditions, can be treated as verbatim copying in
     other respects.

     If the required texts for either cover are too voluminous to fit
     legibly, you should put the first ones listed (as many as fit
     reasonably) on the actual cover, and continue the rest onto
     adjacent pages.

     If you publish or distribute Opaque copies of the Document
     numbering more than 100, you must either include a
     machine-readable Transparent copy along with each Opaque copy, or
     state in or with each Opaque copy a computer-network location from
     which the general network-using public has access to download
     using public-standard network protocols a complete Transparent
     copy of the Document, free of added material.  If you use the
     latter option, you must take reasonably prudent steps, when you
     begin distribution of Opaque copies in quantity, to ensure that
     this Transparent copy will remain thus accessible at the stated
     location until at least one year after the last time you
     distribute an Opaque copy (directly or through your agents or
     retailers) of that edition to the public.

     It is requested, but not required, that you contact the authors of
     the Document well before redistributing any large number of
     copies, to give them a chance to provide you with an updated
     version of the Document.

  4. MODIFICATIONS

     You may copy and distribute a Modified Version of the Document
     under the conditions of sections 2 and 3 above, provided that you
     release the Modified Version under precisely this License, with
     the Modified Version filling the role of the Document, thus
     licensing distribution and modification of the Modified Version to
     whoever possesses a copy of it.  In addition, you must do these
     things in the Modified Version:

       A. Use in the Title Page (and on the covers, if any) a title
          distinct from that of the Document, and from those of
          previous versions (which should, if there were any, be listed
          in the History section of the Document).  You may use the
          same title as a previous version if the original publisher of
          that version gives permission.

       B. List on the Title Page, as authors, one or more persons or
          entities responsible for authorship of the modifications in
          the Modified Version, together with at least five of the
          principal authors of the Document (all of its principal
          authors, if it has fewer than five), unless they release you
          from this requirement.

       C. State on the Title page the name of the publisher of the
          Modified Version, as the publisher.

       D. Preserve all the copyright notices of the Document.

       E. Add an appropriate copyright notice for your modifications
          adjacent to the other copyright notices.

       F. Include, immediately after the copyright notices, a license
          notice giving the public permission to use the Modified
          Version under the terms of this License, in the form shown in
          the Addendum below.

       G. Preserve in that license notice the full lists of Invariant
          Sections and required Cover Texts given in the Document's
          license notice.

       H. Include an unaltered copy of this License.

       I. Preserve the section Entitled "History", Preserve its Title,
          and add to it an item stating at least the title, year, new
          authors, and publisher of the Modified Version as given on
          the Title Page.  If there is no section Entitled "History" in
          the Document, create one stating the title, year, authors,
          and publisher of the Document as given on its Title Page,
          then add an item describing the Modified Version as stated in
          the previous sentence.

       J. Preserve the network location, if any, given in the Document
          for public access to a Transparent copy of the Document, and
          likewise the network locations given in the Document for
          previous versions it was based on.  These may be placed in
          the "History" section.  You may omit a network location for a
          work that was published at least four years before the
          Document itself, or if the original publisher of the version
          it refers to gives permission.

       K. For any section Entitled "Acknowledgements" or "Dedications",
          Preserve the Title of the section, and preserve in the
          section all the substance and tone of each of the contributor
          acknowledgements and/or dedications given therein.

       L. Preserve all the Invariant Sections of the Document,
          unaltered in their text and in their titles.  Section numbers
          or the equivalent are not considered part of the section
          titles.

       M. Delete any section Entitled "Endorsements".  Such a section
          may not be included in the Modified Version.

       N. Do not retitle any existing section to be Entitled
          "Endorsements" or to conflict in title with any Invariant
          Section.

       O. Preserve any Warranty Disclaimers.

     If the Modified Version includes new front-matter sections or
     appendices that qualify as Secondary Sections and contain no
     material copied from the Document, you may at your option
     designate some or all of these sections as invariant.  To do this,
     add their titles to the list of Invariant Sections in the Modified
     Version's license notice.  These titles must be distinct from any
     other section titles.

     You may add a section Entitled "Endorsements", provided it contains
     nothing but endorsements of your Modified Version by various
     parties--for example, statements of peer review or that the text
     has been approved by an organization as the authoritative
     definition of a standard.

     You may add a passage of up to five words as a Front-Cover Text,
     and a passage of up to 25 words as a Back-Cover Text, to the end
     of the list of Cover Texts in the Modified Version.  Only one
     passage of Front-Cover Text and one of Back-Cover Text may be
     added by (or through arrangements made by) any one entity.  If the
     Document already includes a cover text for the same cover,
     previously added by you or by arrangement made by the same entity
     you are acting on behalf of, you may not add another; but you may
     replace the old one, on explicit permission from the previous
     publisher that added the old one.

     The author(s) and publisher(s) of the Document do not by this
     License give permission to use their names for publicity for or to
     assert or imply endorsement of any Modified Version.

  5. COMBINING DOCUMENTS

     You may combine the Document with other documents released under
     this License, under the terms defined in section 4 above for
     modified versions, provided that you include in the combination
     all of the Invariant Sections of all of the original documents,
     unmodified, and list them all as Invariant Sections of your
     combined work in its license notice, and that you preserve all
     their Warranty Disclaimers.

     The combined work need only contain one copy of this License, and
     multiple identical Invariant Sections may be replaced with a single
     copy.  If there are multiple Invariant Sections with the same name
     but different contents, make the title of each such section unique
     by adding at the end of it, in parentheses, the name of the
     original author or publisher of that section if known, or else a
     unique number.  Make the same adjustment to the section titles in
     the list of Invariant Sections in the license notice of the
     combined work.

     In the combination, you must combine any sections Entitled
     "History" in the various original documents, forming one section
     Entitled "History"; likewise combine any sections Entitled
     "Acknowledgements", and any sections Entitled "Dedications".  You
     must delete all sections Entitled "Endorsements."

  6. COLLECTIONS OF DOCUMENTS

     You may make a collection consisting of the Document and other
     documents released under this License, and replace the individual
     copies of this License in the various documents with a single copy
     that is included in the collection, provided that you follow the
     rules of this License for verbatim copying of each of the
     documents in all other respects.

     You may extract a single document from such a collection, and
     distribute it individually under this License, provided you insert
     a copy of this License into the extracted document, and follow
     this License in all other respects regarding verbatim copying of
     that document.

  7. AGGREGATION WITH INDEPENDENT WORKS

     A compilation of the Document or its derivatives with other
     separate and independent documents or works, in or on a volume of
     a storage or distribution medium, is called an "aggregate" if the
     copyright resulting from the compilation is not used to limit the
     legal rights of the compilation's users beyond what the individual
     works permit.  When the Document is included in an aggregate, this
     License does not apply to the other works in the aggregate which
     are not themselves derivative works of the Document.

     If the Cover Text requirement of section 3 is applicable to these
     copies of the Document, then if the Document is less than one half
     of the entire aggregate, the Document's Cover Texts may be placed
     on covers that bracket the Document within the aggregate, or the
     electronic equivalent of covers if the Document is in electronic
     form.  Otherwise they must appear on printed covers that bracket
     the whole aggregate.

  8. TRANSLATION

     Translation is considered a kind of modification, so you may
     distribute translations of the Document under the terms of section
     4.  Replacing Invariant Sections with translations requires special
     permission from their copyright holders, but you may include
     translations of some or all Invariant Sections in addition to the
     original versions of these Invariant Sections.  You may include a
     translation of this License, and all the license notices in the
     Document, and any Warranty Disclaimers, provided that you also
     include the original English version of this License and the
     original versions of those notices and disclaimers.  In case of a
     disagreement between the translation and the original version of
     this License or a notice or disclaimer, the original version will
     prevail.

     If a section in the Document is Entitled "Acknowledgements",
     "Dedications", or "History", the requirement (section 4) to
     Preserve its Title (section 1) will typically require changing the
     actual title.

  9. TERMINATION

     You may not copy, modify, sublicense, or distribute the Document
     except as expressly provided for under this License.  Any other
     attempt to copy, modify, sublicense or distribute the Document is
     void, and will automatically terminate your rights under this
     License.  However, parties who have received copies, or rights,
     from you under this License will not have their licenses
     terminated so long as such parties remain in full compliance.

 10. FUTURE REVISIONS OF THIS LICENSE

     The Free Software Foundation may publish new, revised versions of
     the GNU Free Documentation License from time to time.  Such new
     versions will be similar in spirit to the present version, but may
     differ in detail to address new problems or concerns.  See
     `http://www.gnu.org/copyleft/'.

     Each version of the License is given a distinguishing version
     number.  If the Document specifies that a particular numbered
     version of this License "or any later version" applies to it, you
     have the option of following the terms and conditions either of
     that specified version or of any later version that has been
     published (not as a draft) by the Free Software Foundation.  If
     the Document does not specify a version number of this License,
     you may choose any version ever published (not as a draft) by the
     Free Software Foundation.

ADDENDUM: How to use this License for your documents
====================================================

To use this License in a document you have written, include a copy of
the License in the document and put the following copyright and license
notices just after the title page:

       Copyright (C)  YEAR  YOUR NAME.
       Permission is granted to copy, distribute and/or modify this document
       under the terms of the GNU Free Documentation License, Version 1.2
       or any later version published by the Free Software Foundation;
       with no Invariant Sections, no Front-Cover Texts, and no Back-Cover
       Texts.  A copy of the license is included in the section entitled ``GNU
       Free Documentation License''.

   If you have Invariant Sections, Front-Cover Texts and Back-Cover
Texts, replace the "with...Texts." line with this:

         with the Invariant Sections being LIST THEIR TITLES, with
         the Front-Cover Texts being LIST, and with the Back-Cover Texts
         being LIST.

   If you have Invariant Sections without Cover Texts, or some other
combination of the three, merge those two alternatives to suit the
situation.

   If your document contains nontrivial examples of program code, we
recommend releasing these examples in parallel under your choice of
free software license, such as the GNU General Public License, to
permit their use in free software.


File: gdb.info,  Node: Index,  Prev: GNU Free Documentation License,  Up: Top

Index
*****

* Menu:

* ! packet:                              Packets.
* "No symbol "foo" in current context":  Variables.
* # (a comment):                         Command Syntax.
* # in Modula-2:                         GDB/M2.
* $:                                     Value History.
* $$:                                    Value History.
* $_ and info breakpoints:               Set Breaks.
* $_ and info line:                      Machine Code.
* $_, $__, and value history:            Memory.
* $_, convenience variable:              Convenience Vars.
* $__, convenience variable:             Convenience Vars.
* $_exitcode, convenience variable:      Convenience Vars.
* $bpnum, convenience variable:          Set Breaks.
* $cdir, convenience variable:           Source Path.
* $cwdr, convenience variable:           Source Path.
* $tpnum:                                Create and Delete Tracepoints.
* $trace_file:                           Tracepoint Variables.
* $trace_frame:                          Tracepoint Variables.
* $trace_func:                           Tracepoint Variables.
* $trace_line:                           Tracepoint Variables.
* $tracepoint:                           Tracepoint Variables.
* --annotate:                            Mode Options.
* --args:                                Mode Options.
* --async:                               Mode Options.
* --batch:                               Mode Options.
* --baud:                                Mode Options.
* --cd:                                  Mode Options.
* --command:                             File Options.
* --core:                                File Options.
* --directory:                           File Options.
* --epoch:                               Mode Options.
* --exec:                                File Options.
* --fullname:                            Mode Options.
* --interpreter:                         Mode Options.
* --mapped:                              File Options.
* --noasync:                             Mode Options.
* --nowindows:                           Mode Options.
* --nx:                                  Mode Options.
* --pid:                                 File Options.
* --quiet:                               Mode Options.
* --readnow:                             File Options.
* --se:                                  File Options.
* --silent:                              Mode Options.
* --statistics:                          Mode Options.
* --symbols:                             File Options.
* --tty:                                 Mode Options.
* --tui:                                 Mode Options.
* --version:                             Mode Options.
* --windows:                             Mode Options.
* --write:                               Mode Options.
* -b:                                    Mode Options.
* -break-after:                          GDB/MI Breakpoint Table Commands.
* -break-condition:                      GDB/MI Breakpoint Table Commands.
* -break-delete:                         GDB/MI Breakpoint Table Commands.
* -break-disable:                        GDB/MI Breakpoint Table Commands.
* -break-enable:                         GDB/MI Breakpoint Table Commands.
* -break-info:                           GDB/MI Breakpoint Table Commands.
* -break-insert:                         GDB/MI Breakpoint Table Commands.
* -break-list:                           GDB/MI Breakpoint Table Commands.
* -break-watch:                          GDB/MI Breakpoint Table Commands.
* -c:                                    File Options.
* -d:                                    File Options.
* -data-disassemble:                     GDB/MI Data Manipulation.
* -data-evaluate-expression:             GDB/MI Data Manipulation.
* -data-list-changed-registers:          GDB/MI Data Manipulation.
* -data-list-register-names:             GDB/MI Data Manipulation.
* -data-list-register-values:            GDB/MI Data Manipulation.
* -data-read-memory:                     GDB/MI Data Manipulation.
* -display-delete:                       GDB/MI Data Manipulation.
* -display-disable:                      GDB/MI Data Manipulation.
* -display-enable:                       GDB/MI Data Manipulation.
* -display-insert:                       GDB/MI Data Manipulation.
* -display-list:                         GDB/MI Data Manipulation.
* -e:                                    File Options.
* -environment-cd:                       GDB/MI Data Manipulation.
* -environment-directory:                GDB/MI Data Manipulation.
* -environment-path:                     GDB/MI Data Manipulation.
* -environment-pwd:                      GDB/MI Data Manipulation.
* -exec-abort:                           GDB/MI Program Control.
* -exec-arguments:                       GDB/MI Program Control.
* -exec-continue:                        GDB/MI Program Control.
* -exec-finish:                          GDB/MI Program Control.
* -exec-interrupt:                       GDB/MI Program Control.
* -exec-next:                            GDB/MI Program Control.
* -exec-next-instruction:                GDB/MI Program Control.
* -exec-return:                          GDB/MI Program Control.
* -exec-run:                             GDB/MI Program Control.
* -exec-show-arguments:                  GDB/MI Program Control.
* -exec-step:                            GDB/MI Program Control.
* -exec-step-instruction:                GDB/MI Program Control.
* -exec-until:                           GDB/MI Program Control.
* -f:                                    Mode Options.
* -file-exec-and-symbols:                GDB/MI Program Control.
* -file-exec-file:                       GDB/MI Program Control.
* -file-list-exec-sections:              GDB/MI Program Control.
* -file-list-exec-source-file:           GDB/MI Program Control.
* -file-list-exec-source-files:          GDB/MI Program Control.
* -file-list-shared-libraries:           GDB/MI Program Control.
* -file-list-symbol-files:               GDB/MI Program Control.
* -file-symbol-file:                     GDB/MI Program Control.
* -gdb-exit:                             GDB/MI Miscellaneous Commands.
* -gdb-set:                              GDB/MI Miscellaneous Commands.
* -gdb-show:                             GDB/MI Miscellaneous Commands.
* -gdb-version:                          GDB/MI Miscellaneous Commands.
* -interpreter-exec:                     GDB/MI Miscellaneous Commands.
* -m:                                    File Options.
* -n:                                    Mode Options.
* -nw:                                   Mode Options.
* -p:                                    File Options.
* -q:                                    Mode Options.
* -r:                                    File Options.
* -s:                                    File Options.
* -stack-info-depth:                     GDB/MI Stack Manipulation.
* -stack-info-frame:                     GDB/MI Stack Manipulation.
* -stack-list-arguments:                 GDB/MI Stack Manipulation.
* -stack-list-frames:                    GDB/MI Stack Manipulation.
* -stack-list-locals:                    GDB/MI Stack Manipulation.
* -stack-select-frame:                   GDB/MI Stack Manipulation.
* -symbol-info-address:                  GDB/MI Symbol Query.
* -symbol-info-file:                     GDB/MI Symbol Query.
* -symbol-info-function:                 GDB/MI Symbol Query.
* -symbol-info-line:                     GDB/MI Symbol Query.
* -symbol-info-symbol:                   GDB/MI Symbol Query.
* -symbol-list-functions:                GDB/MI Symbol Query.
* -symbol-list-lines:                    GDB/MI Symbol Query.
* -symbol-list-types:                    GDB/MI Symbol Query.
* -symbol-list-variables:                GDB/MI Symbol Query.
* -symbol-locate:                        GDB/MI Symbol Query.
* -symbol-type:                          GDB/MI Symbol Query.
* -t:                                    Mode Options.
* -target-attach:                        GDB/MI Target Manipulation.
* -target-compare-sections:              GDB/MI Target Manipulation.
* -target-detach:                        GDB/MI Target Manipulation.
* -target-disconnect:                    GDB/MI Target Manipulation.
* -target-download:                      GDB/MI Target Manipulation.
* -target-exec-status:                   GDB/MI Target Manipulation.
* -target-list-available-targets:        GDB/MI Target Manipulation.
* -target-list-current-targets:          GDB/MI Target Manipulation.
* -target-list-parameters:               GDB/MI Target Manipulation.
* -target-select:                        GDB/MI Target Manipulation.
* -thread-info:                          GDB/MI Thread Commands.
* -thread-list-all-threads:              GDB/MI Thread Commands.
* -thread-list-ids:                      GDB/MI Thread Commands.
* -thread-select:                        GDB/MI Thread Commands.
* -var-assign:                           GDB/MI Variable Objects.
* -var-create:                           GDB/MI Variable Objects.
* -var-delete:                           GDB/MI Variable Objects.
* -var-evaluate-expression:              GDB/MI Variable Objects.
* -var-info-expression:                  GDB/MI Variable Objects.
* -var-info-num-children:                GDB/MI Variable Objects.
* -var-info-type:                        GDB/MI Variable Objects.
* -var-list-children:                    GDB/MI Variable Objects.
* -var-set-format:                       GDB/MI Variable Objects.
* -var-show-attributes:                  GDB/MI Variable Objects.
* -var-show-format:                      GDB/MI Variable Objects.
* -var-update:                           GDB/MI Variable Objects.
* -w:                                    Mode Options.
* -x:                                    File Options.
* ., Modula-2 scope operator:            M2 Scope.
* .debug subdirectories:                 Separate Debug Files.
* .esgdbinit:                            Command Files.
* .gdbinit:                              Command Files.
* .gnu_debuglink sections:               Separate Debug Files.
* .o files, reading symbols from:        Files.
* .os68gdbinit:                          Command Files.
* .vxgdbinit:                            Command Files.
* /proc:                                 SVR4 Process Information.
* ? packet:                              Packets.
* @, referencing memory as an array:     Arrays.
* ^done:                                 GDB/MI Result Records.
* ^error:                                GDB/MI Result Records.
* ^running:                              GDB/MI Result Records.
* _NSPrintForDebugger, and printing Objective-C objects: The Print Command with Objective-C.
* A packet:                              Packets.
* abbreviation:                          Command Syntax.
* abort (C-g):                           Miscellaneous Commands.
* accept-line (Newline or Return):       Commands For History.
* acknowledgment, for GDB remote:        Overview.
* actions:                               Tracepoint Actions.
* active targets:                        Active Targets.
* adbg_find_memory_in_frame:             Tracing on Symmetrix.
* add-shared-symbol-file:                Files.
* add-symbol-file:                       Files.
* address of a symbol:                   Symbols.
* advance LOCATION:                      Continuing and Stepping.
* Alpha stack:                           MIPS.
* AMD 29K register stack:                A29K.
* annotations:                           Annotations Overview.
* annotations for errors, warnings and interrupts: Errors.
* annotations for invalidation messages: Invalidation.
* annotations for prompts:               Prompting.
* annotations for running programs:      Annotations for Running.
* annotations for source display:        Source Annotations.
* append:                                Dump/Restore Files.
* append data to a file:                 Dump/Restore Files.
* apropos:                               Help.
* arguments (to your program):           Arguments.
* artificial array:                      Arrays.
* ASCII character set:                   Character Sets.
* assembly instructions:                 Machine Code.
* assignment:                            Assignment.
* async output in GDB/MI:                GDB/MI Output Syntax.
* AT&T disassembly flavor:               Machine Code.
* attach:                                Attach.
* attach to a program by name:           Server.
* automatic display:                     Auto Display.
* automatic overlay debugging:           Automatic Overlay Debugging.
* automatic thread selection:            Threads.
* auxiliary vector:                      Auxiliary Vector.
* awatch:                                Set Watchpoints.
* b (break):                             Set Breaks.
* B packet:                              Packets.
* b packet:                              Packets.
* backtrace:                             Backtrace.
* backtrace limit:                       Backtrace.
* backtraces:                            Backtrace.
* backward-char (C-b):                   Commands For Moving.
* backward-delete-char (Rubout):         Commands For Text.
* backward-kill-line (C-x Rubout):       Commands For Killing.
* backward-kill-word (M-<DEL>):          Commands For Killing.
* backward-word (M-b):                   Commands For Moving.
* beginning-of-history (M-<):            Commands For History.
* beginning-of-line (C-a):               Commands For Moving.
* bell-style:                            Readline Init File Syntax.
* break:                                 Set Breaks.
* break ... thread THREADNO:             Thread Stops.
* break in overloaded functions:         Debugging C plus plus.
* break, and Objective-C:                Method Names in Commands.
* breakpoint:                            Annotations for Running.
* breakpoint address adjusted:           Breakpoint related warnings.
* breakpoint commands:                   Break Commands.
* breakpoint commands for GDB/MI:        GDB/MI Breakpoint Table Commands.
* breakpoint conditions:                 Conditions.
* breakpoint numbers:                    Breakpoints.
* breakpoint on events:                  Breakpoints.
* breakpoint on memory address:          Breakpoints.
* breakpoint on variable modification:   Breakpoints.
* breakpoint ranges:                     Breakpoints.
* breakpoint subroutine, remote:         Stub Contents.
* breakpoints:                           Breakpoints.
* breakpoints and threads:               Thread Stops.
* breakpoints in overlays:               Overlay Commands.
* breakpoints-invalid:                   Invalidation.
* bt (backtrace):                        Backtrace.
* bug criteria:                          Bug Criteria.
* bug reports:                           Bug Reporting.
* bugs in GDB:                           GDB Bugs.
* c (continue):                          Continuing and Stepping.
* c (SingleKey TUI key):                 TUI Single Key Mode.
* C and C++:                             C.
* C and C++ checks:                      C Checks.
* C and C++ constants:                   C Constants.
* C and C++ defaults:                    C Defaults.
* C and C++ operators:                   C Operators.
* C packet:                              Packets.
* c packet:                              Packets.
* C++:                                   C.
* C++ compilers:                         C plus plus expressions.
* C++ exception handling:                Debugging C plus plus.
* C++ scope resolution:                  Variables.
* C++ symbol decoding style:             Print Settings.
* C++ symbol display:                    Debugging C plus plus.
* C-L:                                   TUI Keys.
* C-o (operate-and-get-next):            Command Syntax.
* C-x 1:                                 TUI Keys.
* C-x 2:                                 TUI Keys.
* C-x A:                                 TUI Keys.
* C-x a:                                 TUI Keys.
* C-x C-a:                               TUI Keys.
* C-x o:                                 TUI Keys.
* C-x s:                                 TUI Keys.
* call:                                  Calling.
* call overloaded functions:             C plus plus expressions.
* call stack:                            Stack.
* call-last-kbd-macro (C-x e):           Keyboard Macros.
* calling functions:                     Calling.
* calling make:                          Shell Commands.
* capitalize-word (M-c):                 Commands For Text.
* casts, to view memory:                 Expressions.
* catch:                                 Set Catchpoints.
* catch catch:                           Set Catchpoints.
* catch exceptions, list active handlers: Frame Info.
* catch exec:                            Set Catchpoints.
* catch fork:                            Set Catchpoints.
* catch load:                            Set Catchpoints.
* catch throw:                           Set Catchpoints.
* catch unload:                          Set Catchpoints.
* catch vfork:                           Set Catchpoints.
* catchpoints:                           Breakpoints.
* catchpoints, setting:                  Set Catchpoints.
* cd:                                    Working Directory.
* cdir:                                  Source Path.
* character sets:                        Character Sets.
* character-search (C-]):                Miscellaneous Commands.
* character-search-backward (M-C-]):     Miscellaneous Commands.
* charset:                               Character Sets.
* checks, range:                         Type Checking.
* checks, type:                          Checks.
* checksum, for GDB remote:              Overview.
* choosing target byte order:            Byte Order.
* clear:                                 Delete Breaks.
* clear, and Objective-C:                Method Names in Commands.
* clear-screen (C-l):                    Commands For Moving.
* clearing breakpoints, watchpoints, catchpoints: Delete Breaks.
* close, file-i/o system call:           close.
* collect (tracepoints):                 Tracepoint Actions.
* collected data discarded:              Starting and Stopping Trace Experiment.
* colon, doubled as scope operator:      M2 Scope.
* colon-colon, context for variables/functions: Variables.
* colon-colon, in Modula-2:              M2 Scope.
* command editing:                       Readline Bare Essentials.
* command files:                         Command Files.
* command hooks:                         Hooks.
* command interpreters:                  Interpreters.
* command line editing:                  Editing.
* commands <1>:                          Prompting.
* commands:                              Break Commands.
* commands for C++:                      Debugging C plus plus.
* commands to STDBUG (ST2000):           ST2000.
* comment:                               Command Syntax.
* comment-begin:                         Readline Init File Syntax.
* compatibility, GDB/MI and CLI:         GDB/MI Compatibility with CLI.
* compilation directory:                 Source Path.
* compiling, on Sparclet:                Sparclet.
* complete:                              Help.
* complete (<TAB>):                      Commands For Completion.
* completion:                            Completion.
* completion of quoted strings:          Completion.
* completion-query-items:                Readline Init File Syntax.
* condition:                             Conditions.
* conditional breakpoints:               Conditions.
* configuring GDB:                       Installing GDB.
* configuring GDB, and source tree subdirectories: Installing GDB.
* confirmation:                          Messages/Warnings.
* connect (to STDBUG):                   ST2000.
* console i/o as part of file-i/o:       Console I/O.
* console interpreter:                   Interpreters.
* console output in GDB/MI:              GDB/MI Output Syntax.
* constants, in file-i/o protocol:       Constants.
* continue:                              Continuing and Stepping.
* continuing:                            Continuing and Stepping.
* continuing threads:                    Thread Stops.
* control C, and remote debugging:       Bootstrapping.
* controlling terminal:                  Input/Output.
* convenience variables:                 Convenience Vars.
* convenience variables for tracepoints: Tracepoint Variables.
* convert-meta:                          Readline Init File Syntax.
* copy-backward-word ():                 Commands For Killing.
* copy-forward-word ():                  Commands For Killing.
* copy-region-as-kill ():                Commands For Killing.
* core:                                  Files.
* core dump file:                        Files.
* core-file:                             Files.
* crash of debugger:                     Bug Criteria.
* ctrl-c message, in file-i/o protocol:  The Ctrl-C message.
* current directory:                     Source Path.
* current stack frame:                   Frames.
* current thread:                        Threads.
* cwd:                                   Source Path.
* Cygwin-specific commands:              Cygwin Native.
* d (delete):                            Delete Breaks.
* d (SingleKey TUI key):                 TUI Single Key Mode.
* D packet:                              Packets.
* d packet:                              Packets.
* data manipulation, in GDB/MI:          GDB/MI Data Manipulation.
* debug formats and C++:                 C plus plus expressions.
* debug links:                           Separate Debug Files.
* debugger crash:                        Bug Criteria.
* debugging C++ programs:                C plus plus expressions.
* debugging information directory, global: Separate Debug Files.
* debugging information in separate files: Separate Debug Files.
* debugging optimized code:              Compilation.
* debugging stub, example:               remote stub.
* debugging target:                      Targets.
* define:                                Define.
* defining macros interactively:         Macros.
* definition, showing a macro's:         Macros.
* delete:                                Delete Breaks.
* delete breakpoints:                    Delete Breaks.
* delete display:                        Auto Display.
* delete mem:                            Memory Region Attributes.
* delete tracepoint:                     Create and Delete Tracepoints.
* delete-char (C-d):                     Commands For Text.
* delete-char-or-list ():                Commands For Completion.
* delete-horizontal-space ():            Commands For Killing.
* deleting breakpoints, watchpoints, catchpoints: Delete Breaks.
* demangling:                            Print Settings.
* descriptor tables display:             DJGPP Native.
* detach:                                Attach.
* detach (remote):                       Connecting.
* device:                                Renesas Boards.
* digit-argument (M-0, M-1, ... M--):    Numeric Arguments.
* dir:                                   Source Path.
* direct memory access (DMA) on MS-DOS:  DJGPP Native.
* directories for source files:          Source Path.
* directory:                             Source Path.
* directory, compilation:                Source Path.
* directory, current:                    Source Path.
* dis (disable):                         Disabling.
* disable:                               Disabling.
* disable breakpoints:                   Disabling.
* disable display:                       Auto Display.
* disable mem:                           Memory Region Attributes.
* disable tracepoint:                    Enable and Disable Tracepoints.
* disable-completion:                    Readline Init File Syntax.
* disassemble:                           Machine Code.
* disconnect:                            Connecting.
* display:                               Auto Display.
* display of expressions:                Auto Display.
* DJGPP debugging:                       DJGPP Native.
* dll-symbols:                           Cygwin Native.
* DLLs with no debugging symbols:        Non-debug DLL symbols.
* do (down):                             Selection.
* do-uppercase-version (M-a, M-b, M-X, ...): Miscellaneous Commands.
* document:                              Define.
* documentation:                         Formatting Documentation.
* Down:                                  TUI Keys.
* down:                                  Selection.
* down-silently:                         Selection.
* downcase-word (M-l):                   Commands For Text.
* download to H8/300 or H8/500:          H8/300.
* download to Renesas SH:                H8/300.
* download to Sparclet:                  Sparclet Download.
* download to VxWorks:                   VxWorks Download.
* dump:                                  Dump/Restore Files.
* dump all data collected at tracepoint: tdump.
* dump data to a file:                   Dump/Restore Files.
* dump-functions ():                     Miscellaneous Commands.
* dump-macros ():                        Miscellaneous Commands.
* dump-variables ():                     Miscellaneous Commands.
* dump/restore files:                    Dump/Restore Files.
* dynamic linking:                       Files.
* e (edit):                              Edit.
* EBCDIC character set:                  Character Sets.
* echo:                                  Output.
* edit:                                  Edit.
* editing:                               Editing.
* editing command lines:                 Readline Bare Essentials.
* editing source files:                  Edit.
* editing-mode:                          Readline Init File Syntax.
* else:                                  Define.
* Emacs:                                 Emacs.
* enable:                                Disabling.
* enable breakpoints:                    Disabling.
* enable display:                        Auto Display.
* enable mem:                            Memory Region Attributes.
* enable tracepoint:                     Enable and Disable Tracepoints.
* enable-keypad:                         Readline Init File Syntax.
* end:                                   Break Commands.
* end-kbd-macro (C-x )):                 Keyboard Macros.
* end-of-history (M->):                  Commands For History.
* end-of-line (C-e):                     Commands For Moving.
* entering numbers:                      Numbers.
* environment (of your program):         Environment.
* errno values, in file-i/o protocol:    Errno values.
* error:                                 Errors.
* error on valid input:                  Bug Criteria.
* error-begin:                           Errors.
* event designators:                     Event Designators.
* event handling:                        Set Catchpoints.
* examining data:                        Data.
* examining memory:                      Memory.
* exception handlers:                    Set Catchpoints.
* exception handlers, how to list:       Frame Info.
* exceptionHandler:                      Bootstrapping.
* exchange-point-and-mark (C-x C-x):     Miscellaneous Commands.
* exec-file:                             Files.
* executable file:                       Files.
* exited:                                Annotations for Running.
* exiting GDB:                           Quitting GDB.
* expand-tilde:                          Readline Init File Syntax.
* expanding preprocessor macros:         Macros.
* expressions:                           Expressions.
* expressions in C or C++:               C.
* expressions in C++:                    C plus plus expressions.
* expressions in Modula-2:               Modula-2.
* f (frame):                             Selection.
* f (SingleKey TUI key):                 TUI Single Key Mode.
* F packet:                              Packets.
* F reply packet:                        The F reply packet.
* F request packet:                      The F request packet.
* fatal signal:                          Bug Criteria.
* fatal signals:                         Signals.
* FDL, GNU Free Documentation License:   GNU Free Documentation License.
* fg (resume foreground execution):      Continuing and Stepping.
* file:                                  Files.
* file-i/o examples:                     File-I/O Examples.
* file-i/o overview:                     File-I/O Overview.
* File-I/O remote protocol extension:    File-I/O remote protocol extension.
* file-i/o reply packet:                 The F reply packet.
* file-i/o request packet:               The F request packet.
* find trace snapshot:                   tfind.
* finish:                                Continuing and Stepping.
* flinching:                             Messages/Warnings.
* float promotion:                       ABI.
* floating point:                        Floating Point Hardware.
* floating point registers:              Registers.
* floating point, MIPS remote:           MIPS Embedded.
* flush_i_cache:                         Bootstrapping.
* focus:                                 TUI Commands.
* focus of debugging:                    Threads.
* foo:                                   Symbol Errors.
* fork, debugging programs which call:   Processes.
* format options:                        Print Settings.
* formatted output:                      Output Formats.
* Fortran:                               Summary.
* forward-backward-delete-char ():       Commands For Text.
* forward-char (C-f):                    Commands For Moving.
* forward-search:                        Search.
* forward-search-history (C-s):          Commands For History.
* forward-word (M-f):                    Commands For Moving.
* frame number:                          Frames.
* frame pointer:                         Frames.
* frame, command:                        Frames.
* frame, definition:                     Frames.
* frame, selecting:                      Selection.
* frameless execution:                   Frames.
* frames-invalid:                        Invalidation.
* free memory information (MS-DOS):      DJGPP Native.
* fstat, file-i/o system call:           stat/fstat.
* Fujitsu:                               remote stub.
* full symbol tables, listing GDB's internal: Symbols.
* functions without line info, and stepping: Continuing and Stepping.
* G packet:                              Packets.
* g packet:                              Packets.
* g++, GNU C++ compiler:                 C.
* garbled pointers:                      DJGPP Native.
* GCC and C++:                           C plus plus expressions.
* GDB bugs, reporting:                   Bug Reporting.
* GDB reference card:                    Formatting Documentation.
* gdb.ini:                               Command Files.
* GDB/MI, breakpoint commands:           GDB/MI Breakpoint Table Commands.
* GDB/MI, compatibility with CLI:        GDB/MI Compatibility with CLI.
* GDB/MI, data manipulation:             GDB/MI Data Manipulation.
* GDB/MI, input syntax:                  GDB/MI Input Syntax.
* GDB/MI, its purpose:                   GDB/MI.
* GDB/MI, out-of-band records:           GDB/MI Out-of-band Records.
* GDB/MI, output syntax:                 GDB/MI Output Syntax.
* GDB/MI, result records:                GDB/MI Result Records.
* GDB/MI, simple examples:               GDB/MI Simple Examples.
* GDB/MI, stream records:                GDB/MI Stream Records.
* GDBHISTFILE:                           History.
* gdbserve.nlm:                          NetWare.
* gdbserver:                             Server.
* GDT:                                   DJGPP Native.
* getDebugChar:                          Bootstrapping.
* gettimeofday, file-i/o system call:    gettimeofday.
* global debugging information directory: Separate Debug Files.
* GNU C++:                               C.
* GNU Emacs:                             Emacs.
* gnu_debuglink_crc32:                   Separate Debug Files.
* h (help):                              Help.
* H packet:                              Packets.
* H8/300 or H8/500 download:             H8/300.
* handle:                                Signals.
* handle_exception:                      Stub Contents.
* handling signals:                      Signals.
* hardware watchpoints:                  Set Watchpoints.
* hbreak:                                Set Breaks.
* help:                                  Help.
* help target:                           Target Commands.
* help user-defined:                     Define.
* heuristic-fence-post (Alpha, MIPS):    MIPS.
* history events:                        Event Designators.
* history expansion <1>:                 History Interaction.
* history expansion:                     History.
* history file:                          History.
* history number:                        Value History.
* history save:                          History.
* history size:                          History.
* history substitution:                  History.
* history-preserve-point:                Readline Init File Syntax.
* history-search-backward ():            Commands For History.
* history-search-forward ():             Commands For History.
* hook:                                  Hooks.
* hook-:                                 Hooks.
* hookpost:                              Hooks.
* hookpost-:                             Hooks.
* hooks, for commands:                   Hooks.
* hooks, post-command:                   Hooks.
* hooks, pre-command:                    Hooks.
* horizontal-scroll-mode:                Readline Init File Syntax.
* host character set:                    Character Sets.
* htrace disable:                        OpenRISC 1000.
* htrace enable:                         OpenRISC 1000.
* htrace info:                           OpenRISC 1000.
* htrace mode continuous:                OpenRISC 1000.
* htrace mode suspend:                   OpenRISC 1000.
* htrace print:                          OpenRISC 1000.
* htrace qualifier:                      OpenRISC 1000.
* htrace record:                         OpenRISC 1000.
* htrace rewind:                         OpenRISC 1000.
* htrace stop:                           OpenRISC 1000.
* htrace trigger:                        OpenRISC 1000.
* hwatch:                                OpenRISC 1000.
* i (info):                              Help.
* I packet:                              Packets.
* i packet:                              Packets.
* i/o:                                   Input/Output.
* i386:                                  remote stub.
* i386-stub.c:                           remote stub.
* IBM1047 character set:                 Character Sets.
* IDT:                                   DJGPP Native.
* if:                                    Define.
* ignore:                                Conditions.
* ignore count (of breakpoint):          Conditions.
* INCLUDE_RDB:                           VxWorks.
* info:                                  Help.
* info address:                          Symbols.
* info all-registers:                    Registers.
* info args:                             Frame Info.
* info auxv:                             Auxiliary Vector.
* info breakpoints:                      Set Breaks.
* info catch:                            Frame Info.
* info cisco:                            KOD.
* info classes:                          Symbols.
* info display:                          Auto Display.
* info dll:                              Cygwin Native.
* info dos:                              DJGPP Native.
* info extensions:                       Show.
* info f (info frame):                   Frame Info.
* info files:                            Files.
* info float:                            Floating Point Hardware.
* info frame:                            Frame Info.
* info frame, show the source language:  Show.
* info functions:                        Symbols.
* info line:                             Machine Code.
* info line, and Objective-C:            Method Names in Commands.
* info locals:                           Frame Info.
* info macro:                            Macros.
* info mem:                              Memory Region Attributes.
* info or1k spr:                         OpenRISC 1000.
* info proc:                             SVR4 Process Information.
* info proc mappings:                    SVR4 Process Information.
* info program:                          Stopping.
* info registers:                        Registers.
* info s (info stack):                   Backtrace.
* info scope:                            Symbols.
* info selectors:                        Symbols.
* info set:                              Help.
* info share:                            Files.
* info sharedlibrary:                    Files.
* info signals:                          Signals.
* info source:                           Symbols.
* info source, show the source language: Show.
* info sources:                          Symbols.
* info stack:                            Backtrace.
* info symbol:                           Symbols.
* info target:                           Files.
* info terminal:                         Input/Output.
* info threads:                          Threads.
* info tracepoints:                      Listing Tracepoints.
* info types:                            Symbols.
* info variables:                        Symbols.
* info vector:                           Vector Unit.
* info w32:                              Cygwin Native.
* info watchpoints:                      Set Watchpoints.
* info win:                              TUI Commands.
* information about tracepoints:         Listing Tracepoints.
* inheritance:                           Debugging C plus plus.
* init file:                             Command Files.
* init file name:                        Command Files.
* initial frame:                         Frames.
* initialization file, readline:         Readline Init File.
* innermost frame:                       Frames.
* input syntax for GDB/MI:               GDB/MI Input Syntax.
* input-meta:                            Readline Init File Syntax.
* insert-comment (M-#):                  Miscellaneous Commands.
* insert-completions (M-*):              Commands For Completion.
* inspect:                               Data.
* installation:                          Installing GDB.
* instructions, assembly:                Machine Code.
* integral datatypes, in file-i/o protocol: Integral datatypes.
* Intel:                                 remote stub.
* Intel disassembly flavor:              Machine Code.
* interaction, readline:                 Readline Interaction.
* internal commands:                     Maintenance Commands.
* internal GDB breakpoints:              Set Breaks.
* interpreter-exec:                      Interpreters.
* interrupt:                             Quitting GDB.
* interrupting remote programs:          Connecting.
* interrupting remote targets:           Bootstrapping.
* invalid input:                         Bug Criteria.
* invoke another interpreter:            Interpreters.
* isatty call, file-i/o protocol:        The isatty call.
* isatty, file-i/o system call:          isatty.
* isearch-terminators:                   Readline Init File Syntax.
* ISO 8859-1 character set:              Character Sets.
* ISO Latin 1 character set:             Character Sets.
* jump:                                  Jumping.
* jump, and Objective-C:                 Method Names in Commands.
* k packet:                              Packets.
* kernel object display:                 KOD.
* keymap:                                Readline Init File Syntax.
* kill:                                  Kill Process.
* kill ring:                             Readline Killing Commands.
* kill-line (C-k):                       Commands For Killing.
* kill-region ():                        Commands For Killing.
* kill-whole-line ():                    Commands For Killing.
* kill-word (M-d):                       Commands For Killing.
* killing text:                          Readline Killing Commands.
* KOD:                                   KOD.
* l (list):                              List.
* languages:                             Languages.
* last tracepoint number:                Create and Delete Tracepoints.
* latest breakpoint:                     Set Breaks.
* layout asm:                            TUI Commands.
* layout next:                           TUI Commands.
* layout prev:                           TUI Commands.
* layout regs:                           TUI Commands.
* layout split:                          TUI Commands.
* layout src:                            TUI Commands.
* LDT:                                   DJGPP Native.
* leaving GDB:                           Quitting GDB.
* Left:                                  TUI Keys.
* limits, in file-i/o protocol:          Limits.
* linespec:                              List.
* list:                                  List.
* list of supported file-i/o calls:      List of supported calls.
* list output in GDB/MI:                 GDB/MI Output Syntax.
* list, and Objective-C:                 Method Names in Commands.
* listing GDB's internal symbol tables:  Symbols.
* listing machine instructions:          Machine Code.
* listing mapped overlays:               Overlay Commands.
* load address, overlay's:               How Overlays Work.
* load FILENAME:                         Target Commands.
* local variables:                       Symbols.
* locate address:                        Output Formats.
* log output in GDB/MI:                  GDB/MI Output Syntax.
* logging GDB output:                    Logging output.
* lseek flags, in file-i/o protocol:     Lseek flags.
* lseek, file-i/o system call:           lseek.
* M packet:                              Packets.
* m packet:                              Packets.
* m680x0:                                remote stub.
* m68k-stub.c:                           remote stub.
* machine instructions:                  Machine Code.
* macro define:                          Macros.
* macro definition, showing:             Macros.
* macro expand:                          Macros.
* macro expand-once:                     Macros.
* macro expansion, showing the results of preprocessor: Macros.
* macro undef:                           Macros.
* macros, example of debugging with:     Macros.
* macros, user-defined:                  Macros.
* maint info breakpoints:                Maintenance Commands.
* maint info psymtabs:                   Symbols.
* maint info sections:                   Files.
* maint info symtabs:                    Symbols.
* maint internal-error:                  Maintenance Commands.
* maint internal-warning:                Maintenance Commands.
* maint print cooked-registers:          Maintenance Commands.
* maint print dummy-frames:              Maintenance Commands.
* maint print psymbols:                  Symbols.
* maint print raw-registers:             Maintenance Commands.
* maint print reggroups:                 Maintenance Commands.
* maint print register-groups:           Maintenance Commands.
* maint print registers:                 Maintenance Commands.
* maint print symbols:                   Symbols.
* maint set profile:                     Maintenance Commands.
* maint show profile:                    Maintenance Commands.
* maintenance commands:                  Maintenance Commands.
* make:                                  Shell Commands.
* manual overlay debugging:              Overlay Commands.
* map an overlay:                        Overlay Commands.
* mapped:                                Files.
* mapped address:                        How Overlays Work.
* mapped overlays:                       How Overlays Work.
* mark-modified-lines:                   Readline Init File Syntax.
* mark-symlinked-directories:            Readline Init File Syntax.
* match-hidden-files:                    Readline Init File Syntax.
* mem:                                   Memory Region Attributes.
* member functions:                      C plus plus expressions.
* memory models, H8/500:                 H8/500.
* memory region attributes:              Memory Region Attributes.
* memory tracing:                        Breakpoints.
* memory transfer, in file-i/o protocol: Memory transfer.
* memory, viewing as typed object:       Expressions.
* memory-mapped symbol file:             Files.
* memset:                                Bootstrapping.
* menu-complete ():                      Commands For Completion.
* meta-flag:                             Readline Init File Syntax.
* mi interpreter:                        Interpreters.
* mi1 interpreter:                       Interpreters.
* mi2 interpreter:                       Interpreters.
* minimal language:                      Unsupported languages.
* Minimal symbols and DLLs:              Non-debug DLL symbols.
* MIPS boards:                           MIPS Embedded.
* MIPS remote floating point:            MIPS Embedded.
* MIPS remotedebug protocol:             MIPS Embedded.
* MIPS stack:                            MIPS.
* mode_t values, in file-i/o protocol:   mode_t values.
* Modula-2:                              Summary.
* Modula-2 built-ins:                    Built-In Func/Proc.
* Modula-2 checks:                       M2 Checks.
* Modula-2 constants:                    Built-In Func/Proc.
* Modula-2 defaults:                     M2 Defaults.
* Modula-2 operators:                    M2 Operators.
* Modula-2, deviations from:             Deviations.
* Modula-2, GDB support:                 Modula-2.
* Motorola 680x0:                        remote stub.
* MS Windows debugging:                  Cygwin Native.
* MS-DOS system info:                    DJGPP Native.
* MS-DOS-specific commands:              DJGPP Native.
* multiple processes:                    Processes.
* multiple targets:                      Active Targets.
* multiple threads:                      Threads.
* n (next):                              Continuing and Stepping.
* n (SingleKey TUI key):                 TUI Single Key Mode.
* names of symbols:                      Symbols.
* namespace in C++:                      C plus plus expressions.
* native Cygwin debugging:               Cygwin Native.
* native DJGPP debugging:                DJGPP Native.
* negative breakpoint numbers:           Set Breaks.
* New SYSTAG message:                    Threads.
* New SYSTAG message, on HP-UX:          Threads.
* next:                                  Continuing and Stepping.
* next-history (C-n):                    Commands For History.
* nexti:                                 Continuing and Stepping.
* ni (nexti):                            Continuing and Stepping.
* non-incremental-forward-search-history (M-n): Commands For History.
* non-incremental-reverse-search-history (M-p): Commands For History.
* notation, readline:                    Readline Bare Essentials.
* notational conventions, for GDB/MI:    GDB/MI.
* notify output in GDB/MI:               GDB/MI Output Syntax.
* number representation:                 Numbers.
* numbers for breakpoints:               Breakpoints.
* object files, relocatable, reading symbols from: Files.
* Objective-C:                           Objective-C.
* online documentation:                  Help.
* open flags, in file-i/o protocol:      Open flags.
* open, file-i/o system call:            open.
* OpenRISC 1000:                         OpenRISC 1000.
* OpenRISC 1000 htrace:                  OpenRISC 1000.
* operations allowed on pending breakpoints: Set Breaks.
* optimized code, debugging:             Compilation.
* or1k boards:                           OpenRISC 1000.
* or1ksim:                               OpenRISC 1000.
* OS ABI:                                ABI.
* out-of-band records in GDB/MI:         GDB/MI Out-of-band Records.
* outermost frame:                       Frames.
* output:                                Output.
* output formats:                        Output Formats.
* output syntax of GDB/MI:               GDB/MI Output Syntax.
* output-meta:                           Readline Init File Syntax.
* overlay area:                          How Overlays Work.
* overlay auto:                          Overlay Commands.
* overlay example program:               Overlay Sample Program.
* overlay load-target:                   Overlay Commands.
* overlay manual:                        Overlay Commands.
* overlay map-overlay:                   Overlay Commands.
* overlay off:                           Overlay Commands.
* overlay unmap-overlay:                 Overlay Commands.
* overlays:                              Overlays.
* overlays, setting breakpoints in:      Overlay Commands.
* overload-choice:                       Prompting.
* overloaded functions, calling:         C plus plus expressions.
* overloaded functions, overload resolution: Debugging C plus plus.
* overloading:                           Breakpoint Menus.
* overloading in C++:                    Debugging C plus plus.
* overwrite-mode ():                     Commands For Text.
* P packet:                              Packets.
* p packet:                              Packets.
* packets, reporting on stdout:          Debugging Output.
* page tables display (MS-DOS):          DJGPP Native.
* page-completions:                      Readline Init File Syntax.
* partial symbol dump:                   Symbols.
* partial symbol tables, listing GDB's internal: Symbols.
* Pascal:                                Summary.
* passcount:                             Tracepoint Passcounts.
* patching binaries:                     Patching.
* path:                                  Environment.
* pauses in output:                      Screen Size.
* pending breakpoints:                   Set Breaks.
* PgDn:                                  TUI Keys.
* PgUp:                                  TUI Keys.
* physical address from linear address:  DJGPP Native.
* pipes:                                 Starting.
* po (print-object):                     The Print Command with Objective-C.
* pointer values, in file-i/o protocol:  Pointer values.
* pointer, finding referent:             Print Settings.
* possible-completions (M-?):            Commands For Completion.
* post-commands:                         Prompting.
* post-overload-choice:                  Prompting.
* post-prompt:                           Prompting.
* post-prompt-for-continue:              Prompting.
* post-query:                            Prompting.
* pre-commands:                          Prompting.
* pre-overload-choice:                   Prompting.
* pre-prompt:                            Prompting.
* pre-prompt-for-continue:               Prompting.
* pre-query:                             Prompting.
* prefix-meta (<ESC>):                   Miscellaneous Commands.
* premature return from system calls:    Thread Stops.
* preprocessor macro expansion, showing the results of: Macros.
* previous-history (C-p):                Commands For History.
* print:                                 Data.
* print an Objective-C object description: The Print Command with Objective-C.
* print settings:                        Print Settings.
* print-object:                          The Print Command with Objective-C.
* printf:                                Output.
* printing data:                         Data.
* process image:                         SVR4 Process Information.
* processes, multiple:                   Processes.
* profiling GDB:                         Maintenance Commands.
* prompt <1>:                            Prompting.
* prompt:                                Prompt.
* prompt-for-continue:                   Prompting.
* protocol basics, file-i/o:             Protocol basics.
* protocol specific representation of datatypes, in file-i/o protocol: Protocol specific representation of datatypes.
* protocol, GDB remote serial:           Overview.
* ptype:                                 Symbols.
* putDebugChar:                          Bootstrapping.
* pwd:                                   Working Directory.
* q (quit):                              Quitting GDB.
* q (SingleKey TUI key):                 TUI Single Key Mode.
* Q packet:                              Packets.
* q packet:                              Packets.
* query:                                 Prompting.
* quit:                                  Errors.
* quit [EXPRESSION]:                     Quitting GDB.
* quoted-insert (C-q or C-v):            Commands For Text.
* quotes in commands:                    Completion.
* quoting names:                         Symbols.
* r (run):                               Starting.
* r (SingleKey TUI key):                 TUI Single Key Mode.
* R packet:                              Packets.
* r packet:                              Packets.
* raise exceptions:                      Set Catchpoints.
* range checking:                        Type Checking.
* ranges of breakpoints:                 Breakpoints.
* rbreak:                                Set Breaks.
* re-read-init-file (C-x C-r):           Miscellaneous Commands.
* read, file-i/o system call:            read.
* reading symbols from relocatable object files: Files.
* reading symbols immediately:           Files.
* readline:                              Editing.
* readnow:                               Files.
* recent tracepoint number:              Create and Delete Tracepoints.
* redirection:                           Input/Output.
* redraw-current-line ():                Commands For Moving.
* reference card:                        Formatting Documentation.
* reference declarations:                C plus plus expressions.
* refresh:                               TUI Commands.
* register stack, AMD29K:                A29K.
* registers:                             Registers.
* regular expression:                    Set Breaks.
* reloading symbols:                     Symbols.
* reloading the overlay table:           Overlay Commands.
* relocatable object files, reading symbols from: Files.
* remote connection without stubs:       Server.
* remote debugging:                      Remote.
* remote programs, interrupting:         Connecting.
* remote protocol, field separator:      Overview.
* remote serial debugging summary:       Debug Session.
* remote serial debugging, overview:     remote stub.
* remote serial protocol:                Overview.
* remote serial stub:                    Stub Contents.
* remote serial stub list:               remote stub.
* remote serial stub, initialization:    Stub Contents.
* remote serial stub, main routine:      Stub Contents.
* remote stub, example:                  remote stub.
* remote stub, support routines:         Bootstrapping.
* remotedebug, MIPS protocol:            MIPS Embedded.
* remotetimeout:                         Sparclet.
* remove actions from a tracepoint:      Tracepoint Actions.
* rename, file-i/o system call:          rename.
* Renesas:                               remote stub.
* Renesas SH download:                   H8/300.
* repeating command sequences:           Command Syntax.
* repeating commands:                    Command Syntax.
* reporting bugs in GDB:                 GDB Bugs.
* response time, MIPS debugging:         MIPS.
* restore:                               Dump/Restore Files.
* restore data from a file:              Dump/Restore Files.
* result records in GDB/MI:              GDB/MI Result Records.
* resuming execution:                    Continuing and Stepping.
* RET (repeat last command):             Command Syntax.
* retransmit-timeout, MIPS protocol:     MIPS Embedded.
* return:                                Returning.
* returning from a function:             Returning.
* reverse-search:                        Search.
* reverse-search-history (C-r):          Commands For History.
* revert-line (M-r):                     Miscellaneous Commands.
* Right:                                 TUI Keys.
* run:                                   Starting.
* running:                               Starting.
* running and debugging Sparclet programs: Sparclet Execution.
* running VxWorks tasks:                 VxWorks Attach.
* running, on Sparclet:                  Sparclet.
* rwatch:                                Set Watchpoints.
* s (SingleKey TUI key):                 TUI Single Key Mode.
* s (step):                              Continuing and Stepping.
* S packet:                              Packets.
* s packet:                              Packets.
* save tracepoints for future sessions:  save-tracepoints.
* save-tracepoints:                      save-tracepoints.
* saving symbol table:                   Files.
* scope:                                 M2 Scope.
* search:                                Search.
* searching:                             Search.
* section:                               Files.
* segment descriptor tables:             DJGPP Native.
* select trace snapshot:                 tfind.
* select-frame:                          Frames.
* selected frame:                        Stack.
* selecting frame silently:              Frames.
* self-insert (a, b, A, 1, !, ...):      Commands For Text.
* separate debugging information files:  Separate Debug Files.
* sequence-id, for GDB remote:           Overview.
* serial connections, debugging:         Debugging Output.
* serial device, Renesas micros:         Renesas Boards.
* serial line speed, Renesas micros:     Renesas Boards.
* serial line, target remote:            Connecting.
* serial protocol, GDB remote:           Overview.
* server prefix for annotations:         Server Prefix.
* set:                                   Help.
* set args:                              Arguments.
* set auto-solib-add:                    Files.
* set auto-solib-limit:                  Files.
* set backtrace limit:                   Backtrace.
* set backtrace past-main:               Backtrace.
* set breakpoint pending:                Set Breaks.
* set charset:                           Character Sets.
* set check range:                       Range Checking.
* set check type:                        Type Checking.
* set check, range:                      Range Checking.
* set check, type:                       Type Checking.
* set coerce-float-to-double:            ABI.
* set complaints:                        Messages/Warnings.
* set confirm:                           Messages/Warnings.
* set cp-abi:                            ABI.
* set debug arch:                        Debugging Output.
* set debug event:                       Debugging Output.
* set debug expression:                  Debugging Output.
* set debug frame:                       Debugging Output.
* set debug overload:                    Debugging Output.
* set debug remote:                      Debugging Output.
* set debug serial:                      Debugging Output.
* set debug target:                      Debugging Output.
* set debug varobj:                      Debugging Output.
* set debug-file-directory:              Separate Debug Files.
* set debugevents:                       Cygwin Native.
* set debugexceptions:                   Cygwin Native.
* set debugexec:                         Cygwin Native.
* set debugmemory:                       Cygwin Native.
* set demangle-style:                    Print Settings.
* set disassembly-flavor:                Machine Code.
* set editing:                           Editing.
* set endian auto:                       Byte Order.
* set endian big:                        Byte Order.
* set endian little:                     Byte Order.
* set environment:                       Environment.
* set extension-language:                Show.
* set follow-fork-mode:                  Processes.
* set gnutarget:                         Target Commands.
* set height:                            Screen Size.
* set history expansion:                 History.
* set history filename:                  History.
* set history save:                      History.
* set history size:                      History.
* set host-charset:                      Character Sets.
* set input-radix:                       Numbers.
* set language:                          Manually.
* set listsize:                          List.
* set logging:                           Logging output.
* set machine:                           Renesas Special.
* set max-user-call-depth:               Define.
* set memory MOD:                        H8/500.
* set mipsfpu:                           MIPS Embedded.
* set new-console:                       Cygwin Native.
* set new-group:                         Cygwin Native.
* set opaque-type-resolution:            Symbols.
* set os:                                KOD.
* set osabi:                             ABI.
* set output-radix:                      Numbers.
* set overload-resolution:               Debugging C plus plus.
* set print address:                     Print Settings.
* set print array:                       Print Settings.
* set print asm-demangle:                Print Settings.
* set print demangle:                    Print Settings.
* set print elements:                    Print Settings.
* set print max-symbolic-offset:         Print Settings.
* set print null-stop:                   Print Settings.
* set print object:                      Print Settings.
* set print pretty:                      Print Settings.
* set print sevenbit-strings:            Print Settings.
* set print static-members:              Print Settings.
* set print symbol-filename:             Print Settings.
* set print union:                       Print Settings.
* set print vtbl:                        Print Settings.
* set processor ARGS:                    MIPS Embedded.
* set prompt:                            Prompt.
* set remote hardware-breakpoint-limit:  Remote configuration.
* set remote hardware-watchpoint-limit:  Remote configuration.
* set remote system-call-allowed 0:      The system call.
* set remote system-call-allowed 1:      The system call.
* set remotedebug, MIPS protocol:        MIPS Embedded.
* set retransmit-timeout:                MIPS Embedded.
* set rstack_high_address:               A29K.
* set shell:                             Cygwin Native.
* set solib-absolute-prefix:             Files.
* set solib-search-path:                 Files.
* set step-mode:                         Continuing and Stepping.
* set symbol-reloading:                  Symbols.
* set target-charset:                    Character Sets.
* set timeout:                           MIPS Embedded.
* set tracepoint:                        Create and Delete Tracepoints.
* set trust-readonly-sections:           Files.
* set tui active-border-mode:            TUI Configuration.
* set tui border-kind:                   TUI Configuration.
* set tui border-mode:                   TUI Configuration.
* set variable:                          Assignment.
* set verbose:                           Messages/Warnings.
* set width:                             Screen Size.
* set write:                             Patching.
* set-mark (C-@):                        Miscellaneous Commands.
* set_debug_traps:                       Stub Contents.
* setting variables:                     Assignment.
* setting watchpoints:                   Set Watchpoints.
* SH:                                    remote stub.
* sh-stub.c:                             remote stub.
* share:                                 Files.
* shared libraries:                      Files.
* sharedlibrary:                         Files.
* shell:                                 Shell Commands.
* shell escape:                          Shell Commands.
* show:                                  Help.
* show args:                             Arguments.
* show auto-solib-add:                   Files.
* show auto-solib-limit:                 Files.
* show backtrace limit:                  Backtrace.
* show backtrace past-main:              Backtrace.
* show breakpoint pending:               Set Breaks.
* show charset:                          Character Sets.
* show check range:                      Range Checking.
* show check type:                       Type Checking.
* show complaints:                       Messages/Warnings.
* show confirm:                          Messages/Warnings.
* show convenience:                      Convenience Vars.
* show copying:                          Help.
* show cp-abi:                           ABI.
* show debug arch:                       Debugging Output.
* show debug event:                      Debugging Output.
* show debug expression:                 Debugging Output.
* show debug frame:                      Debugging Output.
* show debug overload:                   Debugging Output.
* show debug remote:                     Debugging Output.
* show debug serial:                     Debugging Output.
* show debug target:                     Debugging Output.
* show debug varobj:                     Debugging Output.
* show debug-file-directory:             Separate Debug Files.
* show demangle-style:                   Print Settings.
* show directories:                      Source Path.
* show editing:                          Editing.
* show environment:                      Environment.
* show gnutarget:                        Target Commands.
* show height:                           Screen Size.
* show history:                          History.
* show host-charset:                     Character Sets.
* show input-radix:                      Numbers.
* show language:                         Show.
* show listsize:                         List.
* show logging:                          Logging output.
* show machine:                          Renesas Special.
* show max-user-call-depth:              Define.
* show mipsfpu:                          MIPS Embedded.
* show new-console:                      Cygwin Native.
* show new-group:                        Cygwin Native.
* show opaque-type-resolution:           Symbols.
* show os:                               KOD.
* show osabi:                            ABI.
* show output-radix:                     Numbers.
* show paths:                            Environment.
* show print address:                    Print Settings.
* show print array:                      Print Settings.
* show print asm-demangle:               Print Settings.
* show print demangle:                   Print Settings.
* show print elements:                   Print Settings.
* show print max-symbolic-offset:        Print Settings.
* show print object:                     Print Settings.
* show print pretty:                     Print Settings.
* show print sevenbit-strings:           Print Settings.
* show print static-members:             Print Settings.
* show print symbol-filename:            Print Settings.
* show print union:                      Print Settings.
* show print vtbl:                       Print Settings.
* show processor:                        MIPS Embedded.
* show prompt:                           Prompt.
* show remote system-call-allowed:       The system call.
* show remotedebug, MIPS protocol:       MIPS Embedded.
* show retransmit-timeout:               MIPS Embedded.
* show rstack_high_address:              A29K.
* show shell:                            Cygwin Native.
* show solib-absolute-prefix:            Files.
* show solib-search-path:                Files.
* show symbol-reloading:                 Symbols.
* show target-charset:                   Character Sets.
* show timeout:                          MIPS Embedded.
* show user:                             Define.
* show values:                           Value History.
* show verbose:                          Messages/Warnings.
* show version:                          Help.
* show warranty:                         Help.
* show width:                            Screen Size.
* show write:                            Patching.
* show-all-if-ambiguous:                 Readline Init File Syntax.
* shows:                                 History.
* si (stepi):                            Continuing and Stepping.
* signal <1>:                            Annotations for Running.
* signal:                                Signaling.
* signal-name:                           Annotations for Running.
* signal-name-end:                       Annotations for Running.
* signal-string:                         Annotations for Running.
* signal-string-end:                     Annotations for Running.
* signalled:                             Annotations for Running.
* signals:                               Signals.
* silent:                                Break Commands.
* sim:                                   Z8000.
* simulator, Z8000:                      Z8000.
* size of screen:                        Screen Size.
* software watchpoints:                  Set Watchpoints.
* source <1>:                            Source Annotations.
* source:                                Command Files.
* source path:                           Source Path.
* Sparc:                                 remote stub.
* sparc-stub.c:                          remote stub.
* sparcl-stub.c:                         remote stub.
* Sparclet:                              Sparclet.
* SparcLite:                             remote stub.
* speed:                                 Renesas Boards.
* spr:                                   OpenRISC 1000.
* ST2000 auxiliary commands:             ST2000.
* st2000 CMD:                            ST2000.
* stack frame:                           Frames.
* stack on Alpha:                        MIPS.
* stack on MIPS:                         MIPS.
* stack traces:                          Backtrace.
* stacking targets:                      Active Targets.
* start a new trace experiment:          Starting and Stopping Trace Experiment.
* start-kbd-macro (C-x ():               Keyboard Macros.
* starting <1>:                          Annotations for Running.
* starting:                              Starting.
* stat, file-i/o system call:            stat/fstat.
* status of trace data collection:       Starting and Stopping Trace Experiment.
* status output in GDB/MI:               GDB/MI Output Syntax.
* STDBUG commands (ST2000):              ST2000.
* step:                                  Continuing and Stepping.
* stepi:                                 Continuing and Stepping.
* stepping:                              Continuing and Stepping.
* stepping into functions with no line info: Continuing and Stepping.
* stop a running trace experiment:       Starting and Stopping Trace Experiment.
* stop reply packets:                    Stop Reply Packets.
* stop, a pseudo-command:                Hooks.
* stopped threads:                       Thread Stops.
* stopping:                              Annotations for Running.
* stream records in GDB/MI:              GDB/MI Stream Records.
* struct stat, in file-i/o protocol:     struct stat.
* struct timeval, in file-i/o protocol:  struct timeval.
* stub example, remote debugging:        remote stub.
* stupid questions:                      Messages/Warnings.
* switching threads:                     Threads.
* switching threads automatically:       Threads.
* symbol decoding style, C++:            Print Settings.
* symbol dump:                           Symbols.
* symbol from address:                   Symbols.
* symbol names:                          Symbols.
* symbol overloading:                    Breakpoint Menus.
* symbol table:                          Files.
* symbol tables, listing GDB's internal: Symbols.
* symbol-file:                           Files.
* symbols, reading from relocatable object files: Files.
* symbols, reading immediately:          Files.
* sysinfo:                               DJGPP Native.
* system call, file-i/o protocol:        The system call.
* system calls and thread breakpoints:   Thread Stops.
* system, file-i/o system call:          system.
* T packet:                              Packets.
* t packet:                              Packets.
* T packet reply:                        Stop Reply Packets.
* target:                                Targets.
* target abug:                           M68K.
* target array:                          MIPS Embedded.
* target byte order:                     Byte Order.
* target character set:                  Character Sets.
* target core:                           Target Commands.
* target cpu32bug:                       M68K.
* target dbug:                           M68K.
* target ddb PORT:                       MIPS Embedded.
* target dink32:                         PowerPC.
* target e7000, with H8/300:             H8/300.
* target e7000, with Renesas ICE:        Renesas ICE.
* target e7000, with Renesas SH:         SH.
* target est:                            M68K.
* target exec:                           Target Commands.
* target hms, and serial protocol:       Renesas Boards.
* target hms, with H8/300:               H8/300.
* target hms, with Renesas SH:           SH.
* target jtag:                           OpenRISC 1000.
* target lsi PORT:                       MIPS Embedded.
* target m32r:                           M32R/D.
* target m32rsdi:                        M32R/D.
* target mips PORT:                      MIPS Embedded.
* target nrom:                           Target Commands.
* target op50n:                          PA.
* target output in GDB/MI:               GDB/MI Output Syntax.
* target pmon PORT:                      MIPS Embedded.
* target ppcbug:                         PowerPC.
* target ppcbug1:                        PowerPC.
* target r3900:                          MIPS Embedded.
* target rdi:                            ARM.
* target rdp:                            ARM.
* target remote:                         Target Commands.
* target rom68k:                         M68K.
* target rombug:                         M68K.
* target sds:                            PowerPC.
* target sh3, with H8/300:               H8/300.
* target sh3, with SH:                   SH.
* target sh3e, with H8/300:              H8/300.
* target sh3e, with SH:                  SH.
* target sim:                            Target Commands.
* target sim, with Z8000:                Z8000.
* target sparclite:                      Sparclite.
* target vxworks:                        VxWorks.
* target w89k:                           PA.
* tbreak:                                Set Breaks.
* TCP port, target remote:               Connecting.
* tdump:                                 tdump.
* terminal:                              Input/Output.
* Text User Interface:                   TUI.
* tfind:                                 tfind.
* thbreak:                               Set Breaks.
* this, inside C++ member functions:     C plus plus expressions.
* thread apply:                          Threads.
* thread breakpoints:                    Thread Stops.
* thread breakpoints and system calls:   Thread Stops.
* thread identifier (GDB):               Threads.
* thread identifier (system):            Threads.
* thread identifier (system), on HP-UX:  Threads.
* thread number:                         Threads.
* thread THREADNO:                       Threads.
* threads and watchpoints:               Set Watchpoints.
* threads of execution:                  Threads.
* threads, automatic switching:          Threads.
* threads, continuing:                   Thread Stops.
* threads, stopped:                      Thread Stops.
* timeout, MIPS protocol:                MIPS Embedded.
* trace:                                 Create and Delete Tracepoints.
* trace experiment, status of:           Starting and Stopping Trace Experiment.
* tracebacks:                            Backtrace.
* tracepoint actions:                    Tracepoint Actions.
* tracepoint data, display:              tdump.
* tracepoint deletion:                   Create and Delete Tracepoints.
* tracepoint number:                     Create and Delete Tracepoints.
* tracepoint pass count:                 Tracepoint Passcounts.
* tracepoint variables:                  Tracepoint Variables.
* tracepoints:                           Tracepoints.
* translating between character sets:    Character Sets.
* transpose-chars (C-t):                 Commands For Text.
* transpose-words (M-t):                 Commands For Text.
* tstart:                                Starting and Stopping Trace Experiment.
* tstatus:                               Starting and Stopping Trace Experiment.
* tstop:                                 Starting and Stopping Trace Experiment.
* tty:                                   Input/Output.
* TUI:                                   TUI.
* TUI commands:                          TUI Commands.
* TUI configuration variables:           TUI Configuration.
* TUI key bindings:                      TUI Keys.
* tui reg:                               TUI Commands.
* TUI single key mode:                   TUI Single Key Mode.
* type casting memory:                   Expressions.
* type checking:                         Checks.
* type conversions in C++:               C plus plus expressions.
* u (SingleKey TUI key):                 TUI Single Key Mode.
* u (until):                             Continuing and Stepping.
* UDP port, target remote:               Connecting.
* undisplay:                             Auto Display.
* undo (C-_ or C-x C-u):                 Miscellaneous Commands.
* universal-argument ():                 Numeric Arguments.
* unix-line-discard (C-u):               Commands For Killing.
* unix-word-rubout (C-w):                Commands For Killing.
* unknown address, locating:             Output Formats.
* unlink, file-i/o system call:          unlink.
* unmap an overlay:                      Overlay Commands.
* unmapped overlays:                     How Overlays Work.
* unset environment:                     Environment.
* unsupported languages:                 Unsupported languages.
* until:                                 Continuing and Stepping.
* Up:                                    TUI Keys.
* up:                                    Selection.
* up-silently:                           Selection.
* upcase-word (M-u):                     Commands For Text.
* update:                                TUI Commands.
* user-defined command:                  Define.
* user-defined macros:                   Macros.
* v (SingleKey TUI key):                 TUI Single Key Mode.
* value history:                         Value History.
* variable name conflict:                Variables.
* variable objects in GDB/MI:            GDB/MI Variable Objects.
* variable values, wrong:                Variables.
* variables, readline:                   Readline Init File Syntax.
* variables, setting:                    Assignment.
* vCont packet:                          Packets.
* vCont? packet:                         Packets.
* vector unit:                           Vector Unit.
* vector, auxiliary:                     Auxiliary Vector.
* version number:                        Help.
* visible-stats:                         Readline Init File Syntax.
* VxWorks:                               VxWorks.
* vxworks-timeout:                       VxWorks.
* w (SingleKey TUI key):                 TUI Single Key Mode.
* watch:                                 Set Watchpoints.
* watchpoint:                            Annotations for Running.
* watchpoints:                           Breakpoints.
* watchpoints and threads:               Set Watchpoints.
* whatis:                                Symbols.
* where:                                 Backtrace.
* while:                                 Define.
* while-stepping (tracepoints):          Tracepoint Actions.
* wild pointer, interpreting:            Print Settings.
* winheight:                             TUI Commands.
* word completion:                       Completion.
* working directory:                     Source Path.
* working directory (of your program):   Working Directory.
* working language:                      Languages.
* write, file-i/o system call:           write.
* writing into corefiles:                Patching.
* writing into executables:              Patching.
* wrong values:                          Variables.
* x (examine memory):                    Memory.
* X packet:                              Packets.
* x(examine), and info line:             Machine Code.
* yank (C-y):                            Commands For Killing.
* yank-last-arg (M-. or M-_):            Commands For History.
* yank-nth-arg (M-C-y):                  Commands For History.
* yank-pop (M-y):                        Commands For Killing.
* yanking text:                          Readline Killing Commands.
* z packet:                              Packets.
* Z packets:                             Packets.
* Z0 packet:                             Packets.
* z0 packet:                             Packets.
* Z1 packet:                             Packets.
* z1 packet:                             Packets.
* Z2 packet:                             Packets.
* z2 packet:                             Packets.
* Z3 packet:                             Packets.
* z3 packet:                             Packets.
* Z4 packet:                             Packets.
* z4 packet:                             Packets.
* Z8000:                                 Z8000.
* Zilog Z8000 simulator:                 Z8000.
* {TYPE}:                                Expressions.


