.. SPDX-License-Identifier: GPL-2.0

======================================
UltraSoc - HW Assisted Tracing on SoC
======================================
   :Author:   Qi Liu <liuqi115@huawei.com>
   :Date:     January 2023

Introduction
------------

UltraSoc SMB is a per SCCL (Super CPU Cluster) hardware. It provides a
way to buffer and store CPU trace messages in a region of shared system
memory. The device acts as a coresight sink device and the
corresponding trace generators (ETM) are attached as source devices.

Sysfs files and directories
---------------------------

The SMB devices appear on the existing coresight bus alongside other
devices::

	$# ls /sys/bus/coresight/devices/
	ultra_smb0   ultra_smb1   ultra_smb2   ultra_smb3

The ``ultra_smb<N>`` names SMB device associated with SCCL.::

	$# ls /sys/bus/coresight/devices/ultra_smb0
	enable_sink   mgmt
	$# ls /sys/bus/coresight/devices/ultra_smb0/mgmt
	buf_size  buf_status  read_pos  write_pos

Key file items are:

   * ``read_pos``: Shows the value on the read pointer register.
   * ``write_pos``: Shows the value on the write pointer register.
   * ``buf_status``: Shows the value on the status register.
     BIT(0) is zero value which means the buffer is empty.
   * ``buf_size``: Shows the buffer size of each device.

Firmware Bindings
-----------------

The device is only supported with ACPI. Its binding describes device
identifier, resource information and graph structure.

The device is identified as ACPI HID "HISI03A1". Device resources are allocated
using the _CRS method. Each device must present two base address; the first one
is the configuration base address of the device, the second one is the 32-bit
base address of shared system memory.

Example::

    Device(USMB) {                                               \
      Name(_HID, "HISI03A1")                                     \
      Name(_CRS, ResourceTemplate() {                            \
          QWordMemory (ResourceConsumer, , MinFixed, MaxFixed, NonCacheable, \
		       ReadWrite, 0x0, 0x95100000, 0x951FFFFF, 0x0, 0x100000) \
          QWordMemory (ResourceConsumer, , MinFixed, MaxFixed, Cacheable, \
		       ReadWrite, 0x0, 0x50000000, 0x53FFFFFF, 0x0, 0x4000000) \
      })                                                         \
      Name(_DSD, Package() {                                     \
        ToUUID("ab02a46b-74c7-45a2-bd68-f7d344ef2153"),          \
	/* Use CoreSight Graph ACPI bindings to describe connections topology */
        Package() {                                              \
          0,                                                     \
          1,                                                     \
          Package() {                                            \
            1,                                                   \
            ToUUID("3ecbc8b6-1d0e-4fb3-8107-e627f805c6cd"),      \
            8,                                                   \
            Package() {0x8, 0, \_SB.S00.SL11.CL28.F008, 0},       \
            Package() {0x9, 0, \_SB.S00.SL11.CL29.F009, 0},       \
            Package() {0xa, 0, \_SB.S00.SL11.CL2A.F010, 0},       \
            Package() {0xb, 0, \_SB.S00.SL11.CL2B.F011, 0},       \
            Package() {0xc, 0, \_SB.S00.SL11.CL2C.F012, 0},       \
            Package() {0xd, 0, \_SB.S00.SL11.CL2D.F013, 0},       \
            Package() {0xe, 0, \_SB.S00.SL11.CL2E.F014, 0},       \
            Package() {0xf, 0, \_SB.S00.SL11.CL2F.F015, 0},       \
          }                                                      \
        }                                                        \
      })                                                         \
    }
