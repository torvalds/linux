.. index:: xohtml

.. _xohtml:

xohtml
======

`xohtml` is a tool for turning the output of libxo-enabled commands into
html files suitable for display in modern HTML web browsers.  It can
be used to test and debug HTML output, as well as to make the user
ache to escape the world of '70s terminal devices.

`xohtml` is given a command, either on the command line or via the "-c"
option.  If not command is given, standard input is used.  The
command's output is wrapped in HTML tags, with references to
supporting CSS and Javascript files, and written to standard output or
the file given in the "-f" option.  The "-b" option can be used to
provide an alternative base path for the support files:

============== ===================================================
 Option         Meaning
============== ===================================================
 -b <base>      Base path for finding css/javascript files
 -c <command>   Command to execute
 -f <file>      Output file name
============== ===================================================

The "-c" option takes a full command with arguments, including
any libxo options needed to generate html (`--libxo=html`).  This
value must be quoted if it consists of multiple tokens.
