===========================
HPE iLO NMI Watchdog Driver
===========================

for iLO based ProLiant Servers
==============================

Last reviewed: 08/20/2018


 The HPE iLO NMI Watchdog driver is a kernel module that provides basic
 watchdog functionality and handler for the iLO "Generate NMI to System"
 virtual button.

 All references to iLO in this document imply it also works on iLO2 and all
 subsequent generations.

 Watchdog functionality is enabled like any other common watchdog driver. That
 is, an application needs to be started that kicks off the watchdog timer. A
 basic application exists in tools/testing/selftests/watchdog/ named
 watchdog-test.c. Simply compile the C file and kick it off. If the system
 gets into a bad state and hangs, the HPE ProLiant iLO timer register will
 not be updated in a timely fashion and a hardware system reset (also known as
 an Automatic Server Recovery (ASR)) event will occur.

 The hpwdt driver also has the following module parameters:

 ============  ================================================================
 soft_margin   allows the user to set the watchdog timer value.
               Default value is 30 seconds.
 timeout       an alias of soft_margin.
 pretimeout    allows the user to set the watchdog pretimeout value.
               This is the number of seconds before timeout when an
               NMI is delivered to the system. Setting the value to
               zero disables the pretimeout NMI.
               Default value is 9 seconds.
 nowayout      basic watchdog parameter that does not allow the timer to
               be restarted or an impending ASR to be escaped.
               Default value is set when compiling the kernel. If it is set
               to "Y", then there is no way of disabling the watchdog once
               it has been started.
 kdumptimeout  Minimum timeout in seconds to apply upon receipt of an NMI
               before calling panic. (-1) disables the watchdog.  When value
               is > 0, the timer is reprogrammed with the greater of
               value or current timeout value.
 ============  ================================================================

 NOTE:
       More information about watchdog drivers in general, including the ioctl
       interface to /dev/watchdog can be found in
       Documentation/watchdog/watchdog-api.rst and Documentation/driver-api/ipmi.rst

 Due to limitations in the iLO hardware, the NMI pretimeout if enabled,
 can only be set to 9 seconds.  Attempts to set pretimeout to other
 non-zero values will be rounded, possibly to zero.  Users should verify
 the pretimeout value after attempting to set pretimeout or timeout.

 Upon receipt of an NMI from the iLO, the hpwdt driver will initiate a
 panic. This is to allow for a crash dump to be collected.  It is incumbent
 upon the user to have properly configured the system for kdump.

 The default Linux kernel behavior upon panic is to print a kernel tombstone
 and loop forever.  This is generally not what a watchdog user wants.

 For those wishing to learn more please see:
	- Documentation/admin-guide/kdump/kdump.rst
	- Documentation/admin-guide/kernel-parameters.txt (panic=)
	- Your Linux Distribution specific documentation.

 If the hpwdt does not receive the NMI associated with an expiring timer,
 the iLO will proceed to reset the system at timeout if the timer hasn't
 been updated.

--

 The HPE iLO NMI Watchdog Driver and documentation were originally developed
 by Tom Mingarelli.
