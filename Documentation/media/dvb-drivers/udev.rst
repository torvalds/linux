.. SPDX-License-Identifier: GPL-2.0

UDEV rules for DVB
==================

.. note::

   #) This documentation is outdated. Udev on modern distributions auto-detect
      the DVB devices.

   #) **TODO:** change this document to explain how to make DVB devices
      persistent, as, when a machine has multiple devices, they may be detected
      on different orders, which could cause apps that relies on the device
      numbers to fail.

The DVB subsystem currently registers to the sysfs subsystem using the
"class_simple" interface.

This means that only the basic information like module loading parameters
are presented through sysfs. Other things that might be interesting are
currently **not** available.

Nevertheless it's now possible to add proper udev rules so that the
DVB device nodes are created automatically.

We assume that you have udev already up and running and that have been
creating the DVB device nodes manually up to now due to the missing sysfs
support.

0. Don't forget to disable your current method of creating the
device nodes manually.

1. Unfortunately, you'll need a helper script to transform the kernel
sysfs device name into the well known dvb adapter / device naming scheme.
The script should be called "dvb.sh" and should be placed into a script
dir where udev can execute it, most likely /etc/udev/scripts/

So, create a new file /etc/udev/scripts/dvb.sh and add the following:

.. code-block:: none

	#!/bin/sh
	/bin/echo $1 | /bin/sed -e 's,dvb\([0-9]\)\.\([^0-9]*\)\([0-9]\),dvb/adapter\1/\2\3,'

Don't forget to make the script executable with "chmod".

1. You need to create a proper udev rule that will create the device nodes
like you know them. All real distributions out there scan the /etc/udev/rules.d
directory for rule files. The main udev configuration file /etc/udev/udev.conf
will tell you the directory where the rules are, most likely it's /etc/udev/rules.d/

Create a new rule file in that directory called "dvb.rule" and add the following line:

.. code-block:: none

	KERNEL="dvb*", PROGRAM="/etc/udev/scripts/dvb.sh %k", NAME="%c"

If you want more control over the device nodes (for example a special group membership)
have a look at "man udev".

For every device that registers to the sysfs subsystem with a "dvb" prefix,
the helper script /etc/udev/scripts/dvb.sh is invoked, which will then
create the proper device node in your /dev/ directory.
