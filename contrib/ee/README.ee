Copyright (c) 2009, Hugh Mahon
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


The editor 'ee' (easy editor) is intended to be a simple, easy to use 
terminal-based screen oriented editor that requires no instruction to 
use.  Its primary use would be for people who are new to computers, or who 
use computers only for things like e-mail.

ee's simplified interface is highlighted by the use of pop-up menus which 
make it possible for users to carry out tasks without the need to 
remember commands.  An information window at the top of the screen shows 
the user the operations available with control-keys.

ee allows users to use full eight-bit characters.  If the host system has 
the capabilities, ee can use message catalogs, which would allow users to 
translate the message catalog into other languages which use eight-bit 
characters.  See the file ee.i18n.guide for more details.

ee relies on the virtual memory abilities of the platform it is running on 
and does not have its own memory management capabilities.

I am releasing ee because I hate to see new users and non-computer types 
get frustrated by vi, and would like to see more intuitive interfaces for 
basic tools (both character-based and graphical) become more pervasive.
Terminal capabilities and communication speeds have evolved considerably 
since the time in which vi's interface was created, allowing much more 
intuitive interfaces to be used.  Since character-based I/O won't be 
completely replaced by graphical user interfaces for at least a few more 
years, I'd like to do what I can to make using computers with less 
glamorous interfaces as easy to use as possible.  If terminal interfaces 
are still used in ten years, I hope neophytes won't still be stuck with 
only vi.

For a text editor to be easy to use requires a certain set of abilities.  In 
order for ee to work, a terminal must have the ability to position the cursor 
on the screen, and should have arrow keys that send unique sequences 
(multiple characters, the first character is an "escape", octal code 
'\033').  All of this information needs to be in a database called "terminfo" 
(System V implementations) or "termcap" (usually used for BSD systems).  In 
case the arrow keys do not transmit unique sequences, motion operations are 
mapped to control keys as well, but this at least partially defeats the 
purpose.  The curses package is used to handle the I/O which deals with the 
terminal's capabilities.  

While ee is based on curses, I have included here the source code to 
new_curse, a subset of curses developed for use with ee.  'curses' often  
will have a defect that reduces the usefulness of the editor relying upon 
it.  

The file new_curse.c contains a subset of 'curses', a package for 
applications to use to handle screen output.  Unfortunately, curses 
varies from system to system, so I developed new_curse to provide 
consistent behavior across systems.  It works on both SystemV and BSD 
systems, and while it can sometimes be slower than other curses packages, 
it will get the information on the screen painted correctly more often 
than vendor supplied curses.  Unless problems occur during the building 
of ee, it is recommended that you use new_curse rather than the curses 
supplied with your system.

If you experience problems with data being displayed improperly, check 
your terminal configuration, especially if you're using a terminal 
emulator, and make sure that you are using the right terminfo entry 
before rummaging through code.  Terminfo entries often contain 
inaccuracies, or incomplete information, or may not totally match the 
terminal or emulator the terminal information is being used with.  
Complaints that ee isn't working quite right often end up being something 
else (like the terminal emulator being used).  

Both ee and new_curse were developed using K&R C (also known as "classic 
C"), but it can also be compiled with ANSI C.  You should be able to 
build ee by simply typing "make".  A make file which takes into account 
the characteristics of your system will be created, and then ee will be 
built.  If there are problems encountered, you will be notified about 
them. 

ee is the result of several conflicting design goals.  While I know that it 
solves the problems of some users, I also have no doubt that some will decry 
its lack of more features.  I will settle for knowing that ee does fulfill 
the needs of a minority (but still large number) of users.  The goals of ee 
are: 

        1. To be so easy to use as to require no instruction.
        2. To be easy to compile and, if necessary, port to new platforms 
           by people with relatively little knowledge of C and UNIX.
        3. To have a minimum number of files to be dealt with, for compile 
           and installation.
        4. To have enough functionality to be useful to a large number of 
           people.

Hugh Mahon              |___|     
hugh4242@yahoo.com      |   |     
                            |\  /|
                            | \/ |

