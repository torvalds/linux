/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
----------------------- CHANGE HISTORY --------------------------

       1/29/84  Allowed use on standard input
       2/1/84   Added E, N, P commands
       4/17/84  Added '=' command, 'stop' signal handling
       4/20/84  Added line folding
v2     4/27/84  Fixed '=' command to use BOTTOM_PLUS_ONE,
                instead of TOP, added 'p' & 'v' commands
v3     5/3/84   Added -m and -t options, '-' command
v4     5/3/84   Added LESS environment variable
v5     5/3/84   New comments, fixed '-' command slightly
v6     5/15/84  Added -Q, visual bell
v7     5/24/84  Fixed jump_back(n) bug: n should count real
                lines, not folded lines.  Also allow number on G command.
v8     5/30/84  Re-do -q and -Q commands
v9     9/25/84  Added "+<cmd>" argument
v10    10/10/84 Fixed bug in -b<n> argument processing
v11    10/18/84 Made error() ring bell if \n not entered.
-----------------------------------------------------------------
v12    2/13/85  Reorganized signal handling and made portable to 4.2bsd.
v13    2/16/85  Reword error message for '-' command.
v14    2/22/85  Added -bf and -bp variants of -b.
v15    2/25/85  Miscellaneous changes.
v16    3/13/85  Added -u flag for backspace processing.
v17    4/13/85  Added j and k commands, changed -t default.
v18    4/20/85  Rewrote signal handling code.
v19    5/2/85   Got rid of "verbose" eq_message().
                Made search() scroll in some cases.
v20    5/21/85  Fixed screen.c ioctls for System V.
v21    5/23/85  Fixed some first_cmd bugs.
v22    5/24/85  Added support for no RECOMP nor REGCMP.
v23    5/25/85  Miscellanous changes and prettying up.
                Posted to USENET.
-----------------------------------------------------------------
v24    6/3/85   Added ti,te terminal init & de-init.       
                (Thanks to Mike Kersenbrock)
v25    6/8/85   Added -U flag, standout mode underlining.
v26    6/9/85   Added -M flag.
                Use underline termcap (us) if it exists.
v27    6/15/85  Renamed some variables to make unique in
                6 chars.  Minor fix to -m.
v28    6/28/85  Fixed right margin bug.
v29    6/28/85  Incorporated M.Rose's changes to signal.c
v30    6/29/85  Fixed stupid bug in argument processing.
v31    7/15/85  Added -p flag, changed repaint algorithm.  
                Added kludge for magic cookie terminals.
v32    7/16/85  Added cat_file if output not a tty.
v33    7/23/85  Added -e flag and EDITOR.
v34    7/26/85  Added -s flag.
v35    7/27/85  Rewrote option handling; added option.c.
v36    7/29/85  Fixed -e flag to work if not last file.
v37    8/10/85  Added -x flag.
v38    8/19/85  Changed prompting; created prompt.c.
v39    8/24/85  (Not -p) does not initially clear screen.
v40    8/26/85  Added "skipping" indicator in forw().
                Posted to USENET.
-----------------------------------------------------------------
v41    9/17/85  ONLY_RETURN, control char commands,
                faster search, other minor fixes.
v42    9/25/85  Added ++ command line syntax;
                ch_fsize for pipes.
v43    10/15/85 Added -h flag, changed prim.c algorithms.
v44    10/16/85 Made END print in all cases of eof;
                ignore SIGTTOU after receiv ing SIGTSTP.
v45    10/16/85 Never print backspaces unless -u.
v46    10/24/85 Backwards scroll in jump_loc.
v47    10/30/85 Fixed bug in edit(): *first_cmd==0
v48    11/16/85 Use TIOCSETN instead of TIOCSETP.
                Added marks (m and ' commands).
                Posted to USENET.
-----------------------------------------------------------------
v49    1/9/86   Fixed bug: signal didn't clear mcc.
v50    1/15/86  Added ' (quote) to gomark.
v51    1/16/86  Added + cmd, fixed problem if first_cmd
                fails, made g cmd sort of "work" on pipes
                ev en if bof is no longer buffered.
v52    1/17/86  Made short files work better.
v53    1/20/86  Added -P option.
v54    1/20/86  Changed help to use HELPFILE.
v55    1/23/86  Messages work better if not tty output.
v56    1/24/86  Added -l option.
v57    1/31/86  Fixed -l to get confirmation before
                ov erwriting an existing file.
v58    8/28/86  Added filename globbing.
v59    9/15/86  Fixed some bugs with very long filenames.
v60    9/26/86  Incorporated changes from Leith (Casey)
                Leedom for boldface and -z option.
v61    9/26/86  Got rid of annoying repaints after ! cmd.
                Posted to USENET.
-----------------------------------------------------------------
v62    12/23/86 Added is_directory(); change -z default to
                -1 instead of 24; cat-and-exit if -e and
                file is less than a screenful.
v63    1/8/87   Fixed bug in cat-and-exit if > 1 file.
v64    1/12/87  Changed puts/putstr, putc/putchr,
                getc/getchr to av oid name conflict with
                stdio functions.
v65    1/26/87  Allowed '-' command to change NUMBER
                v alued options (thanks to Gary Puckering)
v66    2/13/87  Fixed bug: prepaint should use force=1.
v67    2/24/87  Added !! and % expansion to ! command.
v68    2/25/87  Added SIGWINCH and TIOCGWINSZ support;
                changed is_directory to bad_file.
                (thanks to J. Robert Ward)
v69    2/25/87  Added SIGWIND and WIOCGETD (for Unix PC).
v70    3/13/87  Changed help cmd from 'h' to 'H'; better
                error msgs in bad_file, errno_message.
v71    5/11/87  Changed -p to -c, made triple -c/-C
                for clear-eol like more's -c.
v72    6/26/87  Added -E, -L, use $SHELL in lsystem().
                (thanks to Stev e Spearman)
v73    6/26/87  Allow Examine "#" for previous file.
                Posted to USENET 8/25/87.
-----------------------------------------------------------------
v74    9/18/87  Fix conflict in EOF symbol with stdio.h,
                Make os.c more portable to BSD.
v75    9/23/87  Fix problems in get_term (thanks to 
                Paul Eggert); new backwards scrolling in
                jump_loc (thanks to Marion Hakanson).
v76    9/23/87  Added -i flag; allow single "!" to
                inv oke a shell (thanks to Franco Barber).
v77    9/24/87  Added -n flag and line number support.
v78    9/25/87  Fixed problem with prompts longer than
                the screen width.
v79    9/29/87  Added the _ command.
v80    10/6/87  Allow signal to break out of linenum scan.
v81    10/6/87  Allow -b to be changed from within less.
v82    10/7/87  Add cmd_decode to use a table for key
                binding (thanks to Dav id Nason).
v83    10/9/87  Allow .less file for user-defined keys.
v84    10/11/87 Fix -e/-E problems (thanks to Felix Lee).
v85    10/15/87 Search now keeps track of line numbers.
v86    10/20/87 Added -B option and autobuf; fixed
                "pipe error" bug.
v87    3/1/88   Fix bug re BSD signals while reading file.
v88    3/12/88  Use new format for -P option (thanks to
                der Mouse), allow "+-c" without message,
                fix bug re BSD hangup.
v89    3/18/88  Turn off line numbers if linenum scan
                is interrupted.
v90    3/30/88  Allow -P from within less.
v91    3/30/88  Added tags file support (new -t option)
                (thanks to Brian Campbell).
v92    4/4/88   Added -+option syntax.
v93    4/11/88  Add support for slow input (thanks to
                Joe Orost & apologies for taking almost
                3 years to get this in!)
v94    4/11/88  Redo reading/signal stuff.
v95    4/20/88  Repaint screen better after signal.
v96    4/21/88  Add /! and ?! commands.
v97    5/17/88  Allow -l/-L from within less.
                Eliminate some static arrays (use calloc).
                Posted to USENET.
-----------------------------------------------------------------
v98    10/14/88 Fix incorrect calloc call; uninitialized
                var in exec_mca; core dump on unknown TERM.
                Make v cmd work if past last line of file.
                Fix some signal bugs.
v99    10/29/88 Allow space between -X and string,
                when X is a string-valued option.
v100   1/5/89   Fix globbing bug when $SHELL not set;
                allow spaces after -t command.
v101   1/6/89   Fix problem with long (truncated) lines
                in tags file (thanks to Neil Dixon).
v102   1/6/89   Fix bug with E# when no prev file;
                allow spaces after -l command.
v103   3/14/89  Add -N, -f and -? options.  Add z and w
                commands.  Add %L for prompt strings.
v104   3/16/89  Added EDITPROTO.
v105   3/20/89  Fix bug in find_linenum which cached
                incorrectly on long lines.
v106   3/31/89  Added -k option and multiple lesskey      
                files.
v107   4/27/89  Add 8-bit char support and -g option.
                Split option code into 3 files.
v108   5/5/89   Allocate position table dynamically       
                (thanks to Paul Eggert); change % command
                from "percent" to vi-style brace finder.
v109   5/10/89  Added ESC-% command, split prim.c.
v110   5/24/89  Fixed bug in + option; fixed repaint bug
                under Sun windows (thanks to Paul Eggert).
v111   5/25/89  Generalized # and % expansion; use 
                calloc for some error messages.
v112   5/30/89  Get rid of ESC-%, add {}()[] commands.
v113   5/31/89  Optimize lseeks (thanks to Paul Eggert).
v114   7/25/89  Added ESC-/ and ESC-/! commands.
v115   7/26/89  Added ESC-n command.
v116   7/31/89  Added find_pos to optimize g command.
v117   8/1/89   Change -f option to -r.
v118   8/2/89   Save positions for all previous files,
                not just the immediately previous one.
v119   8/7/89   Save marks across file boundaries.
                Add file handle stuff.
v120   8/11/89  Add :ta command.
v121   8/16/89  Add -f option.
v122   8/30/89  Fix performance with many buffers.
v123   8/31/89  Verbose prompts for string options.
                Posted beta to USENET.
-----------------------------------------------------------------
v124   9/18/89  Reorganize search commands,
                N = rev, ESC-n = span, add ESC-N.
v125   9/18/89  Fix tab bug (thanks to Alex Liu).
                Fix EOF bug when both -w and -c.
v126   10/25/89 Add -j option.
v127   10/27/89 Fix problems with blank lines before BOF.
v128   10/27/89 Add %bj, etc. to prompt strings.
v129   11/3/89  Add -+,-- commands; add set-option and
                unset-option to lesskey.
v130   11/6/89  Generalize A_EXTRA to string, remove
                set-option, unset-option from lesskey.
v131   11/7/89  Changed name of EDITPROTO to LESSEDIT.
v132   11/8/89  Allow editing of command prefix.
v133   11/16/89 Add -y option (thanks to Jeff Sullivan).
v134   12/1/89  Glob filenames in the -l command.
v135   12/5/89  Combined {}()[] commands into one, and
                added ESC-^F and ESC-^B commands.
v136   1/20/90  Added -S, -R flags.  Added | command.
                Added warning for binary files. (thanks
                to Richard Brittain and J. Sullivan).
v137   1/21/90  Rewrote horrible pappend code.
                Added * notation for hi-bit chars.
v138   1/24/90  Fix magic cookie terminal handling.
                Get rid of "cleanup" loop in ch_get.
v139   1/27/90  Added MSDOS support.  (many thanks
                to Richard Brittain).
v140   2/7/90   Editing a new file adds it to the
                command line list.
v141   2/8/90   Add edit_list for editing >1 file.
v142   2/10/90  Add :x command.
v143   2/11/90  Add * and @ modifies to search cmds.
                Change ESC-/ cmd from /@* to / *.
v144   3/1/90   Messed around with ch_zero; 
                no real change.
v145   3/2/90   Added -R and -v/-V for MSDOS;
                renamed FILENAME to avoid conflict.
v146   3/5/90   Pull cmdbuf functions out of command.c
v147   3/7/90   Implement ?@; fix multi-file edit bugs.
v148   3/29/90  Fixed bug in :e<file> then :e#.
v149   4/3/90   Change error,ierror,query to use PARG.
v150   4/6/90   Add LESS_CHARSET, LESS_CHARDEF.
v151   4/13/90  Remove -g option; clean up ispipe.
v152   4/14/90  lsystem() closes input file, for
                editors which require exclusive open.
v153   4/18/90  Fix bug if SHELL unset; 
                fix bug in overstrike control char.
v154   4/25/90  Output to fd 2 via buffer.
v155   4/30/90  Ignore -i if uppercase in pattern
                (thanks to Michael Rendell.)
v156   5/3/90   Remove scroll limits in forw() & back();
                causes problems with -c.
v157   5/4/90   Forward search starts at next real line
                (not screen line) after jump target.
v158   6/14/90  Added F command.
v159   7/29/90  Fix bug in exiting: output not flushed.
v160   7/29/90  Clear screen before initial output w/ -c.
v161   7/29/90  Add -T flag.
v162   8/14/90  Fix bug with +F on command line.
v163   8/21/90  Added LESSBINFMT variable.
v164   9/5/90   Added -p, LINES, COLUMNS and
                unset mark ' == BOF, for 1003.2 D5.
v165   9/6/90   At EOF with -c set, don't display empty
                screen when try to page forward.
v166   9/6/90   Fix G when final line in file wraps.
v167   9/11/90  Translate CR/LF -> LF for 1003.2.
v168   9/13/90  Return to curr file if "tag not found".
v169   12/12/90 G goes to EOF even if file has grown.
v170   1/17/91  Add optimization for BSD _setjmp;
                fix #include ioctl.h TERMIO problem.
                (thanks to Paul Eggert)
                Posted to USENET.
-----------------------------------------------------------------
v171   3/6/91   Fix -? bug in get_filename.
v172   3/15/91  Fix G bug in empty file.
                Fix bug with ?\n and -i and uppercase
                pattern at EOF!
                (thanks to Paul Eggert)
v173   3/17/91  Change N cmd to not permanently change
                direction. (thanks to Brian Matthews)
v174   3/18/91  Fix bug with namelogfile not getting
                cleared when change files.
v175   3/18/91  Fix bug with ++cmd on command line.
                (thanks to Jim Meyering)
v176   4/2/91   Change | to not force current screen,
                include marked line, start/end from
                top of screen.  Improve search speed.
                (thanks to Don Mears)
v177   4/2/91   Add LESSHELP variable.
                Fix bug with F command with -e.
                Try /dev/tty for input before using fd 2.
                Patches posted to USENET  4/2/91.
-----------------------------------------------------------------
v178   4/8/91   Fixed bug in globbing logfile name.
                (thanks to Jim Meyering)
v179   4/9/91   Allow negative -z for screen-relative.
v180   4/9/91   Clear to eos rather than eol if "db";
                don't use "sr" if "da".
                (thanks to Tor Lillqvist)
v181   4/18/91  Fixed bug with "negative" chars 80 - FF.
                (thanks to Benny Sander Hofmann)
v182   5/16/91  Fixed bug with attribute at EOL.
                (thanks to Brian Matthews)
v183   6/1/91   Rewrite linstall to do smart config.
v184   7/11/91  Process \b in searches based on -u
                rather than -i.
v185   7/11/91  -Pxxx sets short prompt; assume SIGWINCH
                after a SIGSTOP. (thanks to Ken Laprade)
-----------------------------------------------------------------
v186   4/20/92  Port to MS-DOS (Microsoft C).
v187   4/23/92  Added -D option & TAB_COMPLETE_FILENAME.
v188   4/28/92  Added command line editing features.
v189   12/8/92  Fix mem overrun in anscreen.c:init; 
                fix edit_list to recover from bin file.
v190   2/13/93  Make TAB enter one filename at a time;
                create ^L with old TAB functionality.
v191   3/10/93  Defer creating "flash" page for MS-DOS.
v192   9/6/93   Add BACK-TAB.
v193   9/17/93  Simplify binary_file handling.
v194   1/4/94   Add rudiments of alt_filename handling.
v195   1/11/94  Port back to Unix; support keypad.
-----------------------------------------------------------------
v196   6/7/94   Fix bug with bad filename; fix IFILE
                type problem. (thanks to David MacKenzie)
v197   6/7/94   Fix bug with .less tables inserted wrong.
v198   6/23/94  Use autoconf installation technology.
                (thanks to David MacKenzie)
v199   6/29/94  Fix MS-DOS build (thanks to Tim Wiegman).
v200   7/25/94  Clean up copyright, minor fixes.
        Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v201   7/27/94  Check for no memcpy; add casts to calloc;
                look for regcmp in libgen.a.
                (thanks to Kaveh Ghazi).
v202   7/28/94  Fix bug in edit_next/edit_prev with 
                non-existent files.
v203   8/2/94   Fix a variety of configuration bugs on
                various systems. (thanks to Sakai
                Kiyotaka, Harald Koenig, Bjorn Brox,
                Teemu Rantanen, and Thorsten Lockert)
v204   8/3/94   Use strerror if available.
                (thanks to J.T. Conklin)
v205   8/5/94   Fix bug in finding "me" termcap entry.
                (thanks to Andreas Stolcke)
8/10/94         v205+: Change BUFSIZ to LBUFSIZE to avoid name
                conflict with stdio.h.
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v206   8/10/94  Use initial_scrpos for -t to avoid
                displaying first page before init().
                (thanks to Dominique Petitpierre)
v207   8/12/94  Fix bug if stdout is not tty.
v208   8/16/94  Fix bug in close_altfile if goto err1
                in edit_ifile. (Thanks to M.J. Hewitt)
v209   8/16/94  Change scroll to wscroll to avoid 
                conflict with library function.
v210   8/16/94  Fix bug with bold on 8 bit chars.
                (thanks to Vitor Duarte)
v211   8/16/94  Don't quit on EOI in jump_loc / forw.
v212   8/18/94  Use time_t if available.
v213   8/20/94  Allow ospeed to be defined in termcap.h.
v214   8/20/94  Added HILITE_SEARCH, -F, ESC-u cmd.
                (thanks to Paul Lew and Bob Byrnes)
v215   8/23/94  Fix -i toggle behavior.
v216   8/23/94  Process BS in all searches, not only -u.
v217   8/24/94  Added -X flag.
v218   8/24/94  Reimplement undo_search.
v219   8/24/94  Find tags marked with line number
                instead of pattern.
v220   8/24/94  Stay at same position after SIG_WINCH.
v221   8/24/94  Fix bug in file percentage in big file.
v222   8/25/94  Do better if can't reopen current file.
v223   8/27/94  Support setlocale.
                (thanks to Robert Joop)
v224   8/29/94  Revert v216: process BS in search
                only if -u.
v225   9/6/94   Rewrite undo_search again: toggle.
v226   9/15/94  Configuration fixes. 
                (thanks to David MacKenzie)
v227   9/19/94  Fixed strerror config problem.
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v228   9/21/94  Fix bug in signals: repeated calls to
                get_editkeys overflowed st_edittable.
v229   9/21/94  Fix "Nothing to search" error if -a
                and SRCH_PAST_EOF.
v230   9/21/94  Don't print extra error msg in search
                after regerror().
v231   9/22/94  Fix hilite bug if search matches 0 chars.
                (thanks to John Polstra)
v232   9/23/94  Deal with weird systems that have 
                termios.h but not tcgetattr().
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v233   9/26/94  Use get_term() instead of pos_init() in
                psignals to re-get lower_left termcap.
                (Thanks to John Malecki)
v234   9/26/94  Make MIDDLE closer to middle of screen.
v235   9/27/94  Use local strchr if system doesn't have.
v236   9/28/94  Don't use libucb; use libterm if 
                libtermcap & libcurses doesn't work.
                (Fix for Solaris; thanks to Frank Kaefer)
v237   9/30/94  Use system isupper() etc if provided.
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v238   10/6/94  Make binary non-blinking if LESSBINFMT
                is set to a string without a *.
v239   10/7/94  Don't let delimit_word run back past
                beginning of cmdbuf.
v240   10/10/94 Don't write into termcap buffer.
                (Thanks to Benoit Speckel)
v241   10/13/94 New lesskey file format.
                Don't expand filenames in search command.
v242   10/14/94 Allow lesskey specification of "literal".
v243   10/14/94 Add #stop command to lesskey.
v244   10/16/94 Add -f flag to lesskey.
v245   10/25/94 Allow TAB_COMPLETE_FILENAME to be undefd.
v246   10/27/94 Move help file to /usr/local/share.
v247   10/27/94 Add -V option.
v248   11/5/94  Add -V option to lesskey.
v249   11/5/94  Remove -f flag from lesskey; default
                input file is ~/.lesskey.in, not stdin.
v250   11/7/94  Lesskey input file "-" means stdin.
v251   11/9/94  Convert cfgetospeed result to ospeed.
                (Thanks to Andrew Chernov)
v252   11/16/94 Change default lesskey input file from 
                .lesskey.in to .lesskey.
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v253   11/21/94 Fix bug when tags file has a backslash.
v254   12/6/94  Fix -k option.
v255   12/8/94  Add #define EXAMINE to disable :e etc.
v256   12/10/94 Change highlighting: only highlite search
                results (but now it is reliable).
v257   12/10/94 Add goto_line and repaint_highlight
                to optimize highlight repaints.
v258   12/12/94 Fixup in hilite_line if BS_SPECIAL.
v259   12/12/94 Convert to autoconf 2.0.
v260   12/13/94 Add SECURE define.
v261   12/14/94 Use system WERASE char as EC_W_BACKSPACE.
v262   12/16/94 Add -g/-G flag and screen_hilite.
v263   12/20/94 Reimplement/optimize -G flag behavior.
v264   12/23/94 Allow EXTRA string after line-edit cmd
                in lesskey file.
v265   12/24/94 Add LESSOPEN=|cmd syntax.
v266   12/26/94 Add -I flag.
v267   12/28/94 Formalize the four-byte header emitted
                by a LESSOPEN pipe.
v268   12/28/94 Get rid of four-byte header.
v269   1/2/95   Close alt file before open new one.
                Avoids multiple popen().
v270   1/3/95   Use VISUAL; use S_ISDIR/S_ISREG; fix
                config problem with Solaris POSIX regcomp.
v271   1/4/95   Don't quit on read error.
v272   1/5/95   Get rid of -L.
v273   1/6/95   Fix ch_ungetchar bug; don't call
                LESSOPEN on a pipe.
v274   1/6/95   Ported to OS/2 (thanks to Kai Uwe Rommel)
v275   1/18/95  Fix bug if toggle -G at EOF.
v276   1/30/95  Fix OS/2 version.
v277   1/31/95  Add "next" charset; don't display ^X 
                for X > 128.
v278   2/14/95  Change default for -G.
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v279   2/22/95  Add GNU options --help, --version.
                Minor config fixes.
v280   2/24/95  Clean up calls to glob(); don't set #
                if we can't open the new file.
v281   2/24/95  Repeat search should turn on hilites.
v282   3/2/95   Minor fixes.
v283   3/2/95   Fix homefile; make OS2 look in $HOME.
v284   3/2/95   Error if "v" on LESSOPENed file;
                "%" figures out file size on pipe.
v285   3/7/95   Don't set # in lsystem; 
                lesskey try $HOME first.
v286   3/7/95   Reformat change history (too much free time?).
v287   3/8/95   Fix hilite bug if overstrike multiple chars.
v288   3/8/95   Allow lesskey to override get_editkey keys.
v289   3/9/95   Fix adj_hilite bug when line gets processed by
                hilite_line more than once.
v290   3/9/95   Make configure automatically.  Fix Sequent problem
                with incompatible sigsetmask().
                Posted to prep.ai.mit.edu
-----------------------------------------------------------------
v291   3/21/95  Add #env to lesskey.  Fix MS-DOS build.
                Posted to simtel.
-----------------------------------------------------------------
v292   4/24/95  Add MS-DOS support for Borland C.
                Fix arrow keys in MS-DOS versions.
v293   4/28/95  Add auto-versioning stuff to make dist.
v294   5/12/95  Fix Borland build.
v295   1/20/96  Fix search on squished file; add /@@.
v296   1/23/96  Allow cmdbuf larger than screen width.
v297   1/24/96  Don't call termcap if tgetent fails; 
                add #defines for buffers.
v298   1/24/96  Change @@ to ^K.  
                Add alternate search modifiers ^N, ^F, ^E.
v299   1/25/96  Fix percent overflow in jump_percent (thanks to Brent Wiese);
                don't send "ti" after shell command till RETURN pressed.
v300   1/25/96  Change -U to print tabs as ^I.
v301   1/30/96  Make hilites work in cmd F output.
v302   1/31/96  Fix cmd F to notice window-change signals.
v303   1/31/96  Add ESC-SPACE command.
v304   2/1/96   Add ^R search modifier; add LESSSECURE.
v305   2/2/96   Workaround Linux /proc kernel bug; add LESSKEY.
v306   3/16/96  Minor fixes.
v307   3/25/96  Allow cmd line arg "--"; fix DOS & OS/2 defines.h.
v308   4/4/96   Port to OS-9 (thanks to Boisy Pitre); fix -d.
v309   4/9/96   Fix OS-9 version; fix tags bug with "$".
v310   4/10/96  Get rid of HELPFILE.
v311   4/22/96  Add Windows32 support; merge doscreen.c into screen.c.
v312   4/24/96  Don't quit after "cannot reopen" error.
v313   4/25/96  Added horizontal scrolling.
v314   4/26/96  Modified -e to quit on reaching end of a squished file.
v315   4/26/96  Fix "!;TAB" bug.
v316   5/2/96   Make "|a" when (a < curr screen) go to end of curr screen.
v317   5/14/96  Various fixes for the MS-DOS and OS/2 builds.
                Added ## and %% handling for filenames
v318   5/29/96  Port to OS-9 Microware compiler; minor fixes 
                (thanks to Martin Gregorie).
v319   7/8/96   Fix Windows port (thanks to Jeff Paquette).
v320   7/11/96  Final fixes for Windows port.
v321   7/18/96  Minor fixes.
                Posted to Web page.
-----------------------------------------------------------------
v322   8/13/96  Fix bug in shell escape from help file; add support for 
                Microsoft Visual C under Windows; numerous small fixes.
v323   8/19/96  Fixes for Windows version (thanks to Simon Munton);
                fix for Linux library weirdness (thanks to Jim Diamond);
                port to DJGPP (thanks to Eli Zaretskii).
v324   8/21/96  Add support for spaces in filenames (thanks to Simon Munton).
v325   8/21/96  Add lessecho, for spaces in filenames under Unix.
v326   8/27/96  Fix DJGPP version.
v327   9/1/96   Reorganize lglob, make spaces in filenames work better in Unix.
v328   10/7/96  Append / to directory name in filename completion.
                Fix MS-DOS and OS-9 versions.
v329   10/11/96 Fix more MS-DOS bugs; add LESSSEPARATOR; add -" option.
                Add LESSMETACHARS, LESSMETAESCAPE.
v330   10/21/96 Minor fixes.
                Posted to Web page.
-----------------------------------------------------------------
v331   4/22/97  Various Windows fixes (thanks to Gurusamy Sarathy).
v332   4/22/97  Enter filenames from cmd line into edit history.
                Posted to Web page.
-----------------------------------------------------------------
v333    3/4/99  Changed -w to highlite new line after forward movement.
v334    3/9/99  Avoid overflowing prompt buffer; add %d and %D.
v335   3/20/99  Add EBCDIC support (thanks to Thomas Dorner).
                Use HOMEDRIVE/HOMEPATH on Windows (thanks to Preston Bannister).
                Posted to Web page.
-----------------------------------------------------------------
v336    4/8/99  Fix installation bugs.
v337    4/9/99  Fix another installation bug.
                Posted to Web page.
-----------------------------------------------------------------
v338   4/13/99  Add support for long option names.
v339   4/18/99  Add \k, long option names to lesskey.  Add -^P.  Add :d.
v340   4/21/99  Add regexec2.  Fix Windows build.
                Posted to Web page.
-----------------------------------------------------------------
v341    5/6/99  Add -F option; %c & ?c prompt escapes.
                (Thanks to Michele Maltoni)
v342   7/22/99  Add system-wide lesskey file; allow GPL or Less License.
v343   9/23/99  Support UTF-8 (Thanks to Robert Brady).
                Add %P and ?P in prompts.
v344  10/27/99  -w highlights target line of g and p commands.
v345  10/29/99  Make -R pass thru ESC but not other control chars.
                Posted to Web page.
-----------------------------------------------------------------
v346   11/4/99  Fix bugs in long option processing; R cmd should clear hilites.
                Posted to Web page.
-----------------------------------------------------------------
v347  12/13/99  Fixes for DJGPP version (thanks to Eli Zaretskii).
v348  12/28/99  Fix deleting file with marks (thanks to Dimitar Jekov).
                Fix color problem in DJGPP version (thanks to Eli Zaretskii).
v349   1/24/00  Fix minor DJGPP bugs; check environment vars for UTF-8;
                add --with-editor (thanks to Eli, Markus Kuhn, Thomas Schoepf).
v350   3/1/00   Fix clear-while-standout bug.
v351   3/5/00   Change -M and = prompts to show top & bottom line number.
                Posted to Web page.
-----------------------------------------------------------------
v352   3/8/00   Fix scan_option NULL dereference.
-----------------------------------------------------------------
v353   3/20/00  Fix SECURE compile bug, allow space after numeric option.
v354   3/23/00  Add support for PCRE; add --with-regex configure option.
-----------------------------------------------------------------
v355   6/28/00  Add -# option (thanks to Andy Levinson).
v356   7/5/00   Add -J option.
v357   7/6/00   Support sigprocmask.
-----------------------------------------------------------------
v358   7/8/00   Fix problems with #stop in lesskey file.
                Posted to Web page.
-----------------------------------------------------------------
v359  9/10/00   Fixes for Win32 display problems (thanks to Maurizio Vairani).
v360  1/17/01   Move sysless to etc.
v361  12/4/01   Add IBM-1047 charset & EBCDIC fixes (thanks to Thomas Dorner).
                Fix 32 bit dependencies (thanks to Paul Eggert).
                Fix UTF-8 overstriking (thanks to Robert Brady).
v362  12/4/01   Make status column show search targets.
v363  12/6/01   Add --no-keypad option.
                Add variable width tabstops (thanks to Peter Samuelson).
v364 12/10/01   Better handling of very long lines in input;
                Fix horizontal shifting of colored text.
v365 12/11/01   Fix overstriking of tabs;
                Add support for global(1) and multiple tag matches
                (thanks to Shigio Yamaguchi and Tim Vanderhoek).
v366 12/11/01   Fixes for OS/2 (thanks to Kyosuke Tokoro).
v367 12/13/01   Allow -D and -x options to terminate without dollar sign;
                Right/left arrow when entering N are shift cmds, not line edit.
v368 12/18/01   Update lesskey commands.
v370 12/23/01   Fix tags error messages.
                Posted to Web page.
-----------------------------------------------------------------
v371 12/26/01   Fix new_file bug; use popen in Windows version;
                fix some compiler warnings.
v372 12/29/01   Make -b be in units of 1K.
v373  1/14/02   Improve handling of filenames containing shell metachars.
v374   2/7/02   Fix memory leak; fix bug in -x argument parsing.
v375   4/7/02   Fix searching for SGR sequences; fix SECURE build;
                add SGR support to DJGPP version (thanks to Eli Zaretskii).
v376  6/10/02   Fix bug in overstriking mulitbyte UTF-8 characters
                (thanks to Jungshik Shin).
                Posted to Web page.
-----------------------------------------------------------------
v377  9/10/02   Fix bug in Windows version when file contains CR;
                fix bug in search highlights with -R;
                make initial buffer limit really be 64K not unlimited.
v378  9/30/02   Misc bug fixes and compiler warning cleanup.
                Posted to Web page.
-----------------------------------------------------------------
v379 11/23/02   Add -L option; fix bug with ctrl-K in lesskey files;
                improve UTF-8 overstriking and underscore overstriking;
                fix minor man page problems; change to autoconf 2.54.
v380 11/24/02   Make LINENUM same as POSITION.
v381 11/28/02   Make -N use 7 columns for line number if possible.
-----------------------------------------------------------------
v382   2/3/04   Remove copyrighted code.
-----------------------------------------------------------------
v383  2/16/04   Add history file; add -K option; improve UTF-8 handling;
                fix some signed char bugs (thanks to Christian Biere);
                fix some upper/lower case bugs (thanks to Bjoern Jacke);
                add erase2 char (thanks to David Lawrence);
                add windows charset (thanks to Dimitar Zhekov).
v384  2/20/04   Improvements in UTF-8 handling.
v385  2/23/04   Fix UTF-8 output bug.
-----------------------------------------------------------------
v386  9/13/05   Improvements to UTF-8 shift & color (thanks to Charles Levert);
                protect against invalid LESSOPEN and LESSCLOSE values.
v387  9/14/05   Update Charles Levert's UTF-8 patch.
v388  9/14/05   Change history behavior; change most sprintf calls to snprintf.
v389  9/14/05   Fix copy & paste with long lines; improve performance of 
                expand_linebuf; fix crash in init_mlist; 
v390  9/15/05   Show search matches in status column even if -G is set.
-----------------------------------------------------------------
v391  9/17/05   Fix bugs.
v392  10/14/05  Fix line wrapping bug.
v393  10/19/05  Allow multiple attributes per char; fix bold+underline bug
                (thanks again to Charles Levert).
v394  11/8/05   Fix prompt bug; fix compile problem in Windows build.
-----------------------------------------------------------------
v395  1/12/07   Update Unicode tables (thanks to Charles Levert);
                don't chmod if LESSHISTFILE = /dev/null;
                make -f work for directories; support DESTDIR in Makefile;
                fix sigset_t detection in configure; 
                make "t" cmd traverse tags in correct order
v396  1/13/07   Add compatibility with POSIX more.
v397  3/21/07   Allow decimal point in number for % command;
                Allow decimal point in number for -j option;
                Allow n command to fetch last search pattern from history
                (thanks to arno).
v398  3/22/07   Don't rewrite history file if not necessary;
                fix bug when filenames contain "$".
v399  3/22/07   Don't move to bottom of screen at startup;
                don't output extraneous newlines.
v400  3/23/07   Allow search to find pattern after null byte (PCRE and no-regex)
                (thanks to Michael Constant).
-----------------------------------------------------------------
v401  3/24/07   Minor documentation fixes.
v402  3/30/07   Fix autoconf bug when memcpy etc are inline;
                fix bug in terminating number following -j option.
v403  5/25/07   Fix Windows build.
v404  6/5/07    Fix display bug with F command and long lines.
v405  6/17/07   Fix display bug when using -w option.
v406  6/17/07   Fix secure build.
v407  8/16/07   Fix bugs; support CSI chars.
v408  10/1/07   Fix bug in -i with non-ASCII chars.
v409  10/12/07  Fix crash when viewing text with invalid UTF-8 sequences.
v411  11/6/07   Fix case-insensitive searching with non-ASCII text.
v412  11/6/07   Use symbolic SEEK constants.
v413  11/6/07   Fix search highlight bug with non-ASCII text.
v414  11/6/07   Fix display bug with no-wrap terminals.
v415  11/14/07  Add --follow-name option.
v416  11/22/07  Fix crash when searching text with invalid UTF-8 sequences.
v417  12/31/07  Don't support single-char CSI in UTF-8 mode;
                fix bug with -R and invalid CSI sequences;
                fix bug searching text with SGR sequences with -r;
                emulate SGR sequences in WIN32 build.
v418  12/31/07  Clean up.
-----------------------------------------------------------------
v419  1/16/08   Make CSI char 0x9B work in UTF-8 mode (thanks to Colin Watson).
v420  2/24/08   Add & command; fix -F option; fix '' after G.
v421  2/24/08   Ignore filtered lines when searching.
v422  3/2/08    Output CR at startup.
v423  5/27/08   Clean up.
v424  6/16/08   Fix compile bug with pcre; don't filter help file.
v425  7/14/08   Fix non-ANSI code in list handling in ch.c.
v426  10/27/08  Fix ignaw terminal handling (thanks to Per Hedeland);
                fix binary file detection in UTF-8 mode.
v427  3/16/09   A few Win32 fixes (thanks to Jason Hood).
v428  3/30/09   Add "|-" syntax to LESSOPEN.
v429  4/10/09   Fix search highlighting bug with underlined text.
-----------------------------------------------------------------
v430  4/22/09   Don't pass "-" to non-pipe LESSOPEN unless it starts with "-".
v431  4/29/09   Fix highlight bug when match is at end of line.
v432  6/27/09   Better fix for highlight bugs;
                fix new problems with ignaw terminals.
v433  6/28/09   Cleanup search code.
v434  6/29/09   More cleanup.
v435  7/04/09   Fix bugs with non-regex filtering.
v436  7/05/09   Fix memory leak.
-----------------------------------------------------------------
v437  7/14/09   Fix bug in handling some long option names;
                make percentage calculation more accurate.
v438  12/29/10  Fix bugs with -i/-I and & filtering; 
                exit with status 2 on ctrl-C with -K.
v439  12/31/10  Add -A option.
v440  1/5/11    Fix bug displaying prompt after = command.
v441  1/21/11   Fix semi-infinite loop if no newlines in file;
                make new -A behavior the default.
-----------------------------------------------------------------
v442  3/2/11    Fix search bug.
                Add ctrl-G line edit command.
v443  4/9/11    Fix Windows build.
v444  6/8/11    Fix ungetc bug; remove vestiges of obsolete -l option.
-----------------------------------------------------------------
v445  10/19/11  Fix hilite bug in backwards scroll with -J.
                Fix hilite bug with backspaces.
                Fix bugs handling SGR sequences in Win32 (thanks to Eric Lee).
                Add support for GNU regex (thanks to Reuben Thomas).
v446  5/15/12   Up/down arrows in cmd editing search for matching cmd.
v447  5/21/12   Add ESC-F command, two-pipe LESSOPEN syntax.
v448  6/15/12   Print name of regex library in version message.
v449  6/23/12   Allow config option --with-regex=none.
v450  7/4/12    Fix EOF bug with ESC-F.
v451  7/20/12   Fix typo.
-----------------------------------------------------------------
v452  10/19/12  Fix --with-regex=none, fix "stty 0", fix Win32.
                Don't quit if errors in cmd line options.
v453  10/27/12  Increase buffer sizes.
v454  11/5/12   Fix typo.
v455  11/5/12   Fix typo.
v456  11/8/12   Fix option string incompatibility.
v457  12/8/12   Use new option string syntax only after --use-backslash.
v458  4/4/13    Fix display bug in using up/down in cmd buffer.
-----------------------------------------------------------------
v459  5/6/13    Fix ++ bug.
v460  6/19/13   Automate construction of Unicode tables.
v461  6/21/13   Collapse multiple CRs before LF.
v462  11/26/13  Don't overwrite history file, just append to it.
v463  7/13/14   Misc. fixes.
v464  7/19/14   Fix bugs & improve performance in & filtering
                (thanks to John Sullivan).
v465  8/9/14    More fixes from John Sullivan.
v466  8/23/14   Add colon to LESSANSIMIDCHARS.
v467  9/18/14   Misc. fixes.
v468  9/18/14   Fix typo
v469  10/2/14   Allow extra string in command to append to a multichar
                cmd without executing it; fix bug using GNU regex.
v470  10/5/14   Fix some compiler warnings.
v471  12/14/14  Fix unget issues with prompt. Allow disabling history
                when compiled value of LESSHISTFILE = "-".
v473  12/19/14  Fix prompt bug with stdin and -^P in lesskey extra string.
v474  1/30/15   Fix bug in backwards search with match on bottom line.
                Make follow mode reopen file if file shrinks.
v475  3/2/15    Fix possible buffer overrun with invalid UTF-8; 
                fix bug when compiled with no regex; fix non-match search.
v476  5/3/15    Update man pages.
v477  5/19/15   Fix off-by-one in jump_forw_buffered;
                don't add FAKE_* files to cmd history.
v478  5/21/15   Fix nonportable pointer usage in hilite tree.
v479  7/6/15    Allow %% escapes in LESSOPEN variable.
v480  7/24/15   Fix bug in no-regex searches; support MSVC v1900.
v481  8/20/15   Fix broken -g option.
-----------------------------------------------------------------
v482  2/25/16   Update Unicode database to "2015-06-16, 20:24:00 GMT [KW]".
v483  2/27/16   Regenerate hilite when change search caselessness.
                (Thanks to Jason Hood)
                Fix bug when terminal has no "cm". (Thanks to Noel Cragg)
v484  9/20/16   Update to Unicode 9.0.0 database.
v485  10/21/16  Fix "nothing to search" bug when top/bottom line is empty;
                Display line numbers in bold. (thanks to Jason Hood);
                Fix incorrect display when entering double-width chars in 
                search string.
v486  10/22/16  New commands ESC-{ and ESC-} to shift to start/end of 
                displayed lines; new option -Da in Windows version to 
                enable SGR mode (thanks to Jason Hood).
v487  10/23/16  configure --help formatting.
-----------------------------------------------------------------
v488  2/23/17   Fix memory leaks in search (thanks to John Brooks).
v489  3/30/17   Make -F not do init/deinit if file fits on one screen
                (thanks to Jindrich Novy).
v490  4/5/17    Switch to ANSI prototypes in funcs.h; remove "register".
v491  4/7/17    Fix signed char bug.
v492  4/21/17   Handle SIGTERM.
v493  6/22/17   Fix bug initializing charset in MSDOS build.
v494  6/26/17   Update Unicode tables; make Cf chars composing not binary.
v495  7/3/17    Improve binary file detection (thanks to Bela Lubkin);
                do -R filter when matching tags (thanks to Matthew Malcomson).
v496  7/5/17    Add LESSRSCROLL marker.
v497  7/5/17    Sync.
v498  7/7/17    Fix early truncation of text if last char is double-width.
v499  7/10/17   Misc fixes.
v500  7/11/17   Fix bug where certain env variables couldn't be set in lesskey.
v501  7/12/17   Make sure rscroll char is standout by default.
v502  7/13/17   Control rscroll char via command line option not env variable.
v503  7/13/17   Switch to git.
v504  7/13/17   Call opt_rscroll at startup; change mkhelp.c to mkhelp.pl.
v505  7/17/17   Add M and ESC-M commands; 
                fix buffer handling with stdin and LESSOPEN.
v506  7/17/17   On Windows, convert UTF-8 to multibyte if console is not UTF-8;
                handle extended chars on input (thanks to Jason Hood).
v507  7/18/17   Fix some bugs handling filenames containing shell metachars.
v508  7/19/17   Fix bugs when using LESSOPEN to read stdin.
v509  7/19/17   Fix another stdin bug.
v510  7/20/17   Fix bug in determining when to reopen a file.
v511  7/25/17   Fix bugs in recent MSDOS changes (thanks to Jason Hood).
v512  7/26/17   Fix MSDOS build.
v513  7/26/17   Fix switch to normal attr at end of line with -R and rscroll.
v514  7/27/17   Fix bug in fcomplete when pattern does not match a file.
v515  7/28/17   Allow 'u' in -D option on Windows.
v516  7/29/17   Fix bug using LESSOPEN with filename containing metachars.
v517  7/30/17   Status column shows matches even if hiliting is disabled via -G.
v518  8/1/17    Use underline in sgr mode in MSDOS (thanks to Jason Hood).
v519  8/10/17   Fix rscroll bug when last char of line starts coloration.
v520  9/3/17    Fix compiler warning.
v521  10/20/17  Fix binary file warning in UTF-8 files with SGI sequences.
v522  10/20/17  Handle keypad ENTER key properly.
v523  10/23/17  Cleanup.
v524  10/24/17  Fix getcc bug.
v525  10/24/17  Change M command to mark last displayed line.
v526  10/25/17  Fix search hilite bug introduced in v517.
v527  10/30/17  Fix search hilite bug on last page with -a.
v528  11/3/17   Make second ESC-u clear status column.
v529  11/12/17  Display Unicode formatting chars in hex if -U is set.
v530  12/2/17   Minor doc change and add missing VOID_PARAM.
*/

char version[] = "530";
