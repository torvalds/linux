.. SPDX-License-Identifier: GPL-2.0

=======================
AMD SIDE BAND interface
=======================

Some AMD Zen based processors supports system management
functionality via side-band interface (SBI) called
Advanced Platform Management Link (APML). APML is an I2C/I3C
based 2-wire processor target interface. APML is used to
communicate with the Remote Management Interface
(SB Remote Management Interface (SB-RMI)
and SB Temperature Sensor Interface (SB-TSI)).

More details on the interface can be found in chapter
"5 Advanced Platform Management Link (APML)" of the family/model PPR [1]_.

.. [1] https://docs.amd.com/v/u/en-US/55898_B1_pub_0_50


SBRMI device
============

apml_sbrmi driver under the drivers/misc/amd-sbi creates miscdevice
/dev/sbrmi-* to let user space programs run APML mailbox, CPUID,
MCAMSR and register xfer commands.

Register sets is common across APML protocols. IOCTL is providing synchronization
among protocols as transactions may create race condition.

.. code-block:: bash

   $ ls -al /dev/sbrmi-3c
   crw-------    1 root     root       10,  53 Jul 10 11:13 /dev/sbrmi-3c

apml_sbrmi driver registers hwmon sensors for monitoring power_cap_max,
current power consumption and managing power_cap.

Characteristics of the dev node:
 * Differnet xfer protocols are defined:
	* Mailbox
	* CPUID
	* MCA_MSR
	* Register xfer

Access restrictions:
 * Only root user is allowed to open the file.
 * APML Mailbox messages and Register xfer access are read-write,
 * CPUID and MCA_MSR access is read-only.

SBTSI device
============

sbtsi driver under the drivers/misc/amd-sbi creates miscdevice
/dev/sbtsi-* to let user space programs run APML TSI register transfer
commands.

The driver supports both I2C and I3C transports for SB-TSI targets.
The transport is selected by the bus where the device is enumerated.

Misc device:
 * In 1P socket 0: /dev/sbtsi-4c
 * In 2P socket 0: /dev/sbtsi-4c, socket 1: /dev/sbtsi-48

.. code-block:: bash

   $ ls -al /dev/sbtsi-4c
   crw-------    1 root     root       10, 116 Apr  2 05:22 /dev/sbtsi-4c


Access restrictions:
 * Only root user is allowed to open the file.
 * APML TSI Register transfer access is read-write.

SBTSI hwmon interface
=====================

The sbtsi_temp auxiliary driver binds to the auxiliary device published
by the core sbtsi driver on the auxiliary bus. The auxiliary device is
named amd-sbtsi.temp-sensor.<id>, where <id> is the device's transfer
address: the client address for I2C, or the assigned-address for I3C.

Note that the auxiliary bus formats <id> in decimal, whereas the
/dev/sbtsi-* misc node formats its address in hex. The two therefore
differ for the same device: an I2C/I3C sensor at address 0x4c appears as the
misc node /dev/sbtsi-4c and the auxiliary device
amd-sbtsi.temp-sensor.76.

It registers a hwmon device, providing a standard Linux hwmon interface
for reading CPU temperature and managing temperature limits.

The hwmon device appears under ``/sys/class/hwmon/`` when both ``sbtsi.ko``
and ``sbtsi_temp.ko`` are loaded.

Verify auxiliary bus device::

  ls /sys/bus/auxiliary/devices/
  # e.g. amd-sbtsi.temp-sensor.76 for an I2C/I3C sensor at address 0x4c

Example usage::

  # Read current temperature
  cat /sys/class/hwmon/hwmon<N>/temp1_input

  # Set high temperature limit to 70 °C
  echo 70000 > /sys/class/hwmon/hwmon<N>/temp1_max

  # Verify
  cat /sys/class/hwmon/hwmon<N>/temp1_max

Driver IOCTLs
=============

.. c:macro:: SBRMI_IOCTL_MBOX_CMD
.. kernel-doc:: include/uapi/misc/amd-apml.h
   :doc: SBRMI_IOCTL_MBOX_CMD
.. c:macro:: SBRMI_IOCTL_CPUID_CMD
.. kernel-doc:: include/uapi/misc/amd-apml.h
   :doc: SBRMI_IOCTL_CPUID_CMD
.. c:macro:: SBRMI_IOCTL_MCAMSR_CMD
.. kernel-doc:: include/uapi/misc/amd-apml.h
   :doc: SBRMI_IOCTL_MCAMSR_CMD
.. c:macro:: SBRMI_IOCTL_REG_XFER_CMD
.. kernel-doc:: include/uapi/misc/amd-apml.h
   :doc: SBRMI_IOCTL_REG_XFER_CMD
.. c:macro:: SBTSI_IOCTL_REG_XFER_CMD
.. kernel-doc:: include/uapi/misc/amd-apml.h
   :doc: SBTSI_IOCTL_REG_XFER_CMD

User-space usage
================

To access side band interface from a C program.
First, user need to include the headers::

  #include <uapi/misc/amd-apml.h>

Which defines the supported IOCTL and data structure to be passed
from the user space.

Next thing, open the device file, as follows::

  int file;

  file = open("/dev/sbrmi-*", O_RDWR);
  if (file < 0) {
    /* ERROR HANDLING */
    exit(1);
  }

To open SB-TSI device::

  int file;

  file = open("/dev/sbtsi-4c", O_RDWR);
  if (file < 0) {
    /* ERROR HANDLING */
    exit(1);
  }

The following IOCTLs are defined:

``#define SB_BASE_IOCTL_NR      	0xF9``
``#define SBRMI_IOCTL_MBOX_CMD		_IOWR(SB_BASE_IOCTL_NR, 0, struct apml_mbox_msg)``
``#define SBRMI_IOCTL_CPUID_CMD		_IOWR(SB_BASE_IOCTL_NR, 1, struct apml_cpuid_msg)``
``#define SBRMI_IOCTL_MCAMSR_CMD	_IOWR(SB_BASE_IOCTL_NR, 2, struct apml_mcamsr_msg)``
``#define SBRMI_IOCTL_REG_XFER_CMD	_IOWR(SB_BASE_IOCTL_NR, 3, struct apml_reg_xfer_msg)``
``#define SBTSI_IOCTL_REG_XFER_CMD      _IOWR(SB_BASE_IOCTL_NR, 4, struct apml_tsi_xfer_msg)``


User space C-APIs are made available by esmi_oob_library, hosted at
[2]_ which is provided by the E-SMS project [3]_.

.. [2] https://github.com/amd/esmi_oob_library
.. [3] https://www.amd.com/en/developer/e-sms.html
