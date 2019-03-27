========================================================================
    The MI Driver - LLDB Machine Interface V2 (MI)  Project Overview
========================================================================

The MI Driver is a stand alone executable that either be used via a 
client i.e. Eclipse or directly from the command line. 

For help information on using the MI driver type at the command line:

	lldb-mi --interpreter --help

A blog about the MI Driver is available on CodePlay's website. Although it may not be
completely accurate after the recent changes in lldb-mi.
http://www.codeplay.com/portal/lldb-mi-driver---part-1-introduction

In MI mode and invoked with --log option, lldb-mi generates lldb-mi-log.txt 
This file keeps a history of the MI Driver's activity for one session. It is
used to aid the debugging of the MI Driver. It also gives warnings about
command's which do not support certain argument or options.  

Note any command or text sent to the MI Driver in MI mode that is not a command 
registered in the MI Driver's Command Factory will be rejected and an error message
will be generated.

All the files prefix with MI are specifically for the MI driver code only.
File MIDriverMain.cpp contains the executables main() function.

=========================================================================
Current limitations:
1. Not all commands and their options have been implemented. Please see
the source code for details.
2. LLDB-MI may have additional arguments not used in GDB MI. Please see
MIExtensions.txt

=========================================================================
The MI Driver build configuration:
MICmnConfig.h defines various preprocessor build options.
