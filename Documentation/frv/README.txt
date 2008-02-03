		       ================================
		       Fujitsu FR-V LINUX DOCUMENTATION
		       ================================

This directory contains documentation for the Fujitsu FR-V CPU architecture
port of Linux.

The following documents are available:

 (*) features.txt

     A description of the basic features inherent in this architecture port.


 (*) configuring.txt

     A summary of the configuration options particular to this architecture.


 (*) booting.txt

     A description of how to boot the kernel image and a summary of the kernel
     command line options.


 (*) gdbstub.txt

     A description of how to debug the kernel using GDB attached by serial
     port, and a summary of the services available.


 (*) mmu-layout.txt

     A description of the virtual and physical memory layout used in the
     MMU linux kernel, and the registers used to support it.


 (*) gdbinit

     An example .gdbinit file for use with GDB. It includes macros for viewing
     MMU state on the FR451. See mmu-layout.txt for more information.


 (*) clock.txt

     A description of the CPU clock scaling interface.


 (*) atomic-ops.txt

     A description of how the FR-V kernel's atomic operations work.
