===========================================
Linux kernel release for the IA-64 Platform
===========================================

   These are the release notes for Linux since version 2.4 for IA-64
   platform.  This document provides information specific to IA-64
   ONLY, to get additional information about the Linux kernel also
   read the original Linux README provided with the kernel.

Installing the Kernel
=====================

 - IA-64 kernel installation is the same as the other platforms, see
   original README for details.


Software Requirements
=====================

   Compiling and running this kernel requires an IA-64 compliant GCC
   compiler.  And various software packages also compiled with an
   IA-64 compliant GCC compiler.


Configuring the Kernel
======================

   Configuration is the same, see original README for details.


Compiling the Kernel:

 - Compiling this kernel doesn't differ from other platform so read
   the original README for details BUT make sure you have an IA-64
   compliant GCC compiler.

IA-64 Specifics
===============

 - General issues:

    * Hardly any performance tuning has been done. Obvious targets
      include the library routines (IP checksum, etc.). Less
      obvious targets include making sure we don't flush the TLB
      needlessly, etc.

    * SMP locks cleanup/optimization

    * IA32 support.  Currently experimental.  It mostly works.
