.. SPDX-License-Identifier: GPL-2.0

============================================
AMD HSMP interface
============================================

Newer Fam19h(model 0x00-0x1f, 0x30-0x3f, 0x90-0x9f, 0xa0-0xaf),
Fam1Ah(model 0x00-0x1f) EPYC server line of processors from AMD support
system management functionality via HSMP (Host System Management Port).

The Host System Management Port (HSMP) is an interface to provide
OS-level software with access to system management functions via a
set of mailbox registers.

More details on the interface can be found in chapter
"7 Host System Management Port (HSMP)" of the family/model PPR
Eg: https://www.amd.com/content/dam/amd/en/documents/epyc-technical-docs/programmer-references/55898_B1_pub_0_50.zip


HSMP interface is supported on EPYC line of server CPUs and MI300A (APU).


HSMP device
============================================

amd_hsmp driver under drivers/platforms/x86/amd/hsmp/ has separate driver files
for ACPI object based probing, platform device based probing and for the common
code for these two drivers.

Kconfig option CONFIG_AMD_HSMP_PLAT compiles plat.c and creates amd_hsmp.ko.
Kconfig option CONFIG_AMD_HSMP_ACPI compiles acpi.c and creates hsmp_acpi.ko.
Selecting any of these two configs automatically selects CONFIG_AMD_HSMP. This
compiles common code hsmp.c and creates hsmp_common.ko module.

Both the ACPI and plat drivers create the miscdevice /dev/hsmp to let
user space programs run hsmp mailbox commands.

The ACPI object format supported by the driver is defined below.

$ ls -al /dev/hsmp
crw-r--r-- 1 root root 10, 123 Jan 21 21:41 /dev/hsmp

Characteristics of the dev node:
 * Write mode is used for running set/configure commands
 * Read mode is used for running get/status monitor commands

Access restrictions:
 * Only root user is allowed to open the file in write mode.
 * The file can be opened in read mode by all the users.

In-kernel integration:
 * Other subsystems in the kernel can use the exported transport
   function hsmp_send_message().
 * Locking across callers is taken care by the driver.


HSMP sysfs interface
====================

1. Metrics table binary sysfs

AMD MI300A MCM provides GET_METRICS_TABLE message to retrieve
most of the system management information from SMU in one go.

The metrics table is made available as hexadecimal sysfs binary file
under per socket sysfs directory created at
/sys/devices/platform/amd_hsmp/socket%d/metrics_bin

Note: lseek() is not supported as entire metrics table is read.

Metrics table definitions will be documented as part of Public PPR.
The same is defined in the amd_hsmp.h header.

ACPI device object format
=========================
The ACPI object format expected from the amd_hsmp driver
for socket with ID00 is given below::

  Device(HSMP)
		{
			Name(_HID, "AMDI0097")
			Name(_UID, "ID00")
			Name(HSE0, 0x00000001)
			Name(RBF0, ResourceTemplate()
			{
				Memory32Fixed(ReadWrite, 0xxxxxxx, 0x00100000)
			})
			Method(_CRS, 0, NotSerialized)
			{
				Return(RBF0)
			}
			Method(_STA, 0, NotSerialized)
			{
				If(LEqual(HSE0, One))
				{
					Return(0x0F)
				}
				Else
				{
					Return(Zero)
				}
			}
			Name(_DSD, Package(2)
			{
				Buffer(0x10)
				{
					0x9D, 0x61, 0x4D, 0xB7, 0x07, 0x57, 0xBD, 0x48,
					0xA6, 0x9F, 0x4E, 0xA2, 0x87, 0x1F, 0xC2, 0xF6
				},
				Package(3)
				{
					Package(2) {"MsgIdOffset", 0x00010934},
					Package(2) {"MsgRspOffset", 0x00010980},
					Package(2) {"MsgArgOffset", 0x000109E0}
				}
			})
		}


An example
==========

To access hsmp device from a C program.
First, you need to include the headers::

  #include <linux/amd_hsmp.h>

Which defines the supported messages/message IDs.

Next thing, open the device file, as follows::

  int file;

  file = open("/dev/hsmp", O_RDWR);
  if (file < 0) {
    /* ERROR HANDLING; you can check errno to see what went wrong */
    exit(1);
  }

The following IOCTL is defined:

``ioctl(file, HSMP_IOCTL_CMD, struct hsmp_message *msg)``
  The argument is a pointer to a::

    struct hsmp_message {
    	__u32	msg_id;				/* Message ID */
    	__u16	num_args;			/* Number of input argument words in message */
    	__u16	response_sz;			/* Number of expected output/response words */
    	__u32	args[HSMP_MAX_MSG_LEN];		/* argument/response buffer */
    	__u16	sock_ind;			/* socket number */
    };

The ioctl would return a non-zero on failure; you can read errno to see
what happened. The transaction returns 0 on success.

More details on the interface and message definitions can be found in chapter
"7 Host System Management Port (HSMP)" of the respective family/model PPR
eg: https://www.amd.com/content/dam/amd/en/documents/epyc-technical-docs/programmer-references/55898_B1_pub_0_50.zip

User space C-APIs are made available by linking against the esmi library,
which is provided by the E-SMS project https://www.amd.com/en/developer/e-sms.html.
See: https://github.com/amd/esmi_ib_library
