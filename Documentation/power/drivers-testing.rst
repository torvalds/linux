====================================================
Testing suspend and resume support in device drivers
====================================================

	(C) 2007 Rafael J. Wysocki <rjw@sisk.pl>, GPL

1. Preparing the test system
============================

Unfortunately, to effectively test the support for the system-wide suspend and
resume transitions in a driver, it is necessary to suspend and resume a fully
functional system with this driver loaded.  Moreover, that should be done
several times, preferably several times in a row, and separately for hibernation
(aka suspend to disk or STD) and suspend to RAM (STR), because each of these
cases involves slightly different operations and different interactions with
the machine's BIOS.

Of course, for this purpose the test system has to be known to suspend and
resume without the driver being tested.  Thus, if possible, you should first
resolve all suspend/resume-related problems in the test system before you start
testing the new driver.  Please see Documentation/power/basic-pm-debugging.rst
for more information about the debugging of suspend/resume functionality.

2. Testing the driver
=====================

Once you have resolved the suspend/resume-related problems with your test system
without the new driver, you are ready to test it:

a) Build the driver as a module, load it and try the test modes of hibernation
   (see: Documentation/power/basic-pm-debugging.rst, 1).

b) Load the driver and attempt to hibernate in the "reboot", "shutdown" and
   "platform" modes (see: Documentation/power/basic-pm-debugging.rst, 1).

c) Compile the driver directly into the kernel and try the test modes of
   hibernation.

d) Attempt to hibernate with the driver compiled directly into the kernel
   in the "reboot", "shutdown" and "platform" modes.

e) Try the test modes of suspend (see: Documentation/power/basic-pm-debugging.rst,
   2).  [As far as the STR tests are concerned, it should not matter whether or
   not the driver is built as a module.]

f) Attempt to suspend to RAM using the s2ram tool with the driver loaded
   (see: Documentation/power/basic-pm-debugging.rst, 2).

Each of the above tests should be repeated several times and the STD tests
should be mixed with the STR tests.  If any of them fails, the driver cannot be
regarded as suspend/resume-safe.
