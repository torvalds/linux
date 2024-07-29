======
TOMOYO
======

What is TOMOYO?
===============

TOMOYO is a name-based MAC extension (LSM module) for the Linux kernel.

LiveCD-based tutorials are available at

https://tomoyo.sourceforge.net/1.8/ubuntu12.04-live.html
https://tomoyo.sourceforge.net/1.8/centos6-live.html

Though these tutorials use non-LSM version of TOMOYO, they are useful for you
to know what TOMOYO is.

How to enable TOMOYO?
=====================

Build the kernel with ``CONFIG_SECURITY_TOMOYO=y`` and pass ``security=tomoyo`` on
kernel's command line.

Please see https://tomoyo.sourceforge.net/2.6/ for details.

Where is documentation?
=======================

User <-> Kernel interface documentation is available at
https://tomoyo.sourceforge.net/2.6/policy-specification/index.html .

Materials we prepared for seminars and symposiums are available at
https://sourceforge.net/projects/tomoyo/files/docs/ .
Below lists are chosen from three aspects.

What is TOMOYO?
  TOMOYO Linux Overview
    https://sourceforge.net/projects/tomoyo/files/docs/lca2009-takeda.pdf
  TOMOYO Linux: pragmatic and manageable security for Linux
    https://sourceforge.net/projects/tomoyo/files/docs/freedomhectaipei-tomoyo.pdf
  TOMOYO Linux: A Practical Method to Understand and Protect Your Own Linux Box
    https://sourceforge.net/projects/tomoyo/files/docs/PacSec2007-en-no-demo.pdf

What can TOMOYO do?
  Deep inside TOMOYO Linux
    https://sourceforge.net/projects/tomoyo/files/docs/lca2009-kumaneko.pdf
  The role of "pathname based access control" in security.
    https://sourceforge.net/projects/tomoyo/files/docs/lfj2008-bof.pdf

History of TOMOYO?
  Realities of Mainlining
    https://sourceforge.net/projects/tomoyo/files/docs/lfj2008.pdf
