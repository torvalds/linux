==================================
The s390 SCSI dump tool (zfcpdump)
==================================

System z machines (z900 or higher) provide hardware support for creating system
dumps on SCSI disks. The dump process is initiated by booting a dump tool, which
has to create a dump of the current (probably crashed) Linux image. In order to
not overwrite memory of the crashed Linux with data of the dump tool, the
hardware saves some memory plus the register sets of the boot CPU before the
dump tool is loaded. There exists an SCLP hardware interface to obtain the saved
memory afterwards. Currently 32 MB are saved.

This zfcpdump implementation consists of a Linux dump kernel together with
a user space dump tool, which are loaded together into the saved memory region
below 32 MB. zfcpdump is installed on a SCSI disk using zipl (as contained in
the s390-tools package) to make the device bootable. The operator of a Linux
system can then trigger a SCSI dump by booting the SCSI disk, where zfcpdump
resides on.

The user space dump tool accesses the memory of the crashed system by means
of the /proc/vmcore interface. This interface exports the crashed system's
memory and registers in ELF core dump format. To access the memory which has
been saved by the hardware SCLP requests will be created at the time the data
is needed by /proc/vmcore. The tail part of the crashed systems memory which
has not been stashed by hardware can just be copied from real memory.

To build a dump enabled kernel the kernel config option CONFIG_CRASH_DUMP
has to be set.

To get a valid zfcpdump kernel configuration use "make zfcpdump_defconfig".

The s390 zipl tool looks for the zfcpdump kernel and optional initrd/initramfs
under the following locations:

* kernel:  <zfcpdump directory>/zfcpdump.image
* ramdisk: <zfcpdump directory>/zfcpdump.rd

The zfcpdump directory is defined in the s390-tools package.

The user space application of zfcpdump can reside in an intitramfs or an
initrd. It can also be included in a built-in kernel initramfs. The application
reads from /proc/vmcore or zcore/mem and writes the system dump to a SCSI disk.

The s390-tools package version 1.24.0 and above builds an external zfcpdump
initramfs with a user space application that writes the dump to a SCSI
partition.

For more information on how to use zfcpdump refer to the s390 'Using the Dump
Tools book', which is available from
http://www.ibm.com/developerworks/linux/linux390.
