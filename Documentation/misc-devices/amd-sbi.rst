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

.. [1] https://www.amd.com/content/dam/amd/en/documents/epyc-technical-docs/programmer-references/55898_B1_pub_0_50.zip


SBRMI device
============

apml_sbrmi driver under the drivers/misc/amd-sbi creates miscdevice
/dev/sbrmi-* to let user space programs run APML mailbox, CPUID,
MCAMSR and register xfer commands.

Register sets is common across APML protocols. IOCTL is providing synchronization
among protocols as transactions may create race condition.

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

The following IOCTLs are defined:

``#define SB_BASE_IOCTL_NR      	0xF9``
``#define SBRMI_IOCTL_MBOX_CMD		_IOWR(SB_BASE_IOCTL_NR, 0, struct apml_mbox_msg)``
``#define SBRMI_IOCTL_CPUID_CMD		_IOWR(SB_BASE_IOCTL_NR, 1, struct apml_cpuid_msg)``
``#define SBRMI_IOCTL_MCAMSR_CMD	_IOWR(SB_BASE_IOCTL_NR, 2, struct apml_mcamsr_msg)``
``#define SBRMI_IOCTL_REG_XFER_CMD	_IOWR(SB_BASE_IOCTL_NR, 3, struct apml_reg_xfer_msg)``


User space C-APIs are made available by esmi_oob_library, hosted at
[2]_ which is provided by the E-SMS project [3]_.

.. [2] https://github.com/amd/esmi_oob_library
.. [3] https://www.amd.com/en/developer/e-sms.html
