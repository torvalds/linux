===============================
Adjunct Processor (AP) facility
===============================


Introduction
============
The Adjunct Processor (AP) facility is an IBM Z cryptographic facility comprised
of three AP instructions and from 1 up to 256 PCIe cryptographic adapter cards.
The AP devices provide cryptographic functions to all CPUs assigned to a
linux system running in an IBM Z system LPAR.

The AP adapter cards are exposed via the AP bus. The motivation for vfio-ap
is to make AP cards available to KVM guests using the VFIO mediated device
framework. This implementation relies considerably on the s390 virtualization
facilities which do most of the hard work of providing direct access to AP
devices.

AP Architectural Overview
=========================
To facilitate the comprehension of the design, let's start with some
definitions:

* AP adapter

  An AP adapter is an IBM Z adapter card that can perform cryptographic
  functions. There can be from 0 to 256 adapters assigned to an LPAR. Adapters
  assigned to the LPAR in which a linux host is running will be available to
  the linux host. Each adapter is identified by a number from 0 to 255; however,
  the maximum adapter number is determined by machine model and/or adapter type.
  When installed, an AP adapter is accessed by AP instructions executed by any
  CPU.

  The AP adapter cards are assigned to a given LPAR via the system's Activation
  Profile which can be edited via the HMC. When the linux host system is IPL'd
  in the LPAR, the AP bus detects the AP adapter cards assigned to the LPAR and
  creates a sysfs device for each assigned adapter. For example, if AP adapters
  4 and 10 (0x0a) are assigned to the LPAR, the AP bus will create the following
  sysfs device entries::

    /sys/devices/ap/card04
    /sys/devices/ap/card0a

  Symbolic links to these devices will also be created in the AP bus devices
  sub-directory::

    /sys/bus/ap/devices/[card04]
    /sys/bus/ap/devices/[card04]

* AP domain

  An adapter is partitioned into domains. An adapter can hold up to 256 domains
  depending upon the adapter type and hardware configuration. A domain is
  identified by a number from 0 to 255; however, the maximum domain number is
  determined by machine model and/or adapter type.. A domain can be thought of
  as a set of hardware registers and memory used for processing AP commands. A
  domain can be configured with a secure private key used for clear key
  encryption. A domain is classified in one of two ways depending upon how it
  may be accessed:

    * Usage domains are domains that are targeted by an AP instruction to
      process an AP command.

    * Control domains are domains that are changed by an AP command sent to a
      usage domain; for example, to set the secure private key for the control
      domain.

  The AP usage and control domains are assigned to a given LPAR via the system's
  Activation Profile which can be edited via the HMC. When a linux host system
  is IPL'd in the LPAR, the AP bus module detects the AP usage and control
  domains assigned to the LPAR. The domain number of each usage domain and
  adapter number of each AP adapter are combined to create AP queue devices
  (see AP Queue section below). The domain number of each control domain will be
  represented in a bitmask and stored in a sysfs file
  /sys/bus/ap/ap_control_domain_mask. The bits in the mask, from most to least
  significant bit, correspond to domains 0-255.

* AP Queue

  An AP queue is the means by which an AP command is sent to a usage domain
  inside a specific adapter. An AP queue is identified by a tuple
  comprised of an AP adapter ID (APID) and an AP queue index (APQI). The
  APQI corresponds to a given usage domain number within the adapter. This tuple
  forms an AP Queue Number (APQN) uniquely identifying an AP queue. AP
  instructions include a field containing the APQN to identify the AP queue to
  which the AP command is to be sent for processing.

  The AP bus will create a sysfs device for each APQN that can be derived from
  the cross product of the AP adapter and usage domain numbers detected when the
  AP bus module is loaded. For example, if adapters 4 and 10 (0x0a) and usage
  domains 6 and 71 (0x47) are assigned to the LPAR, the AP bus will create the
  following sysfs entries::

    /sys/devices/ap/card04/04.0006
    /sys/devices/ap/card04/04.0047
    /sys/devices/ap/card0a/0a.0006
    /sys/devices/ap/card0a/0a.0047

  The following symbolic links to these devices will be created in the AP bus
  devices subdirectory::

    /sys/bus/ap/devices/[04.0006]
    /sys/bus/ap/devices/[04.0047]
    /sys/bus/ap/devices/[0a.0006]
    /sys/bus/ap/devices/[0a.0047]

* AP Instructions:

  There are three AP instructions:

  * NQAP: to enqueue an AP command-request message to a queue
  * DQAP: to dequeue an AP command-reply message from a queue
  * PQAP: to administer the queues

  AP instructions identify the domain that is targeted to process the AP
  command; this must be one of the usage domains. An AP command may modify a
  domain that is not one of the usage domains, but the modified domain
  must be one of the control domains.

AP and SIE
==========
Let's now take a look at how AP instructions executed on a guest are interpreted
by the hardware.

A satellite control block called the Crypto Control Block (CRYCB) is attached to
our main hardware virtualization control block. The CRYCB contains an AP Control
Block (APCB) that has three fields to identify the adapters, usage domains and
control domains assigned to the KVM guest:

* The AP Mask (APM) field is a bit mask that identifies the AP adapters assigned
  to the KVM guest. Each bit in the mask, from left to right, corresponds to
  an APID from 0-255. If a bit is set, the corresponding adapter is valid for
  use by the KVM guest.

* The AP Queue Mask (AQM) field is a bit mask identifying the AP usage domains
  assigned to the KVM guest. Each bit in the mask, from left to right,
  corresponds to an AP queue index (APQI) from 0-255. If a bit is set, the
  corresponding queue is valid for use by the KVM guest.

* The AP Domain Mask field is a bit mask that identifies the AP control domains
  assigned to the KVM guest. The ADM bit mask controls which domains can be
  changed by an AP command-request message sent to a usage domain from the
  guest. Each bit in the mask, from left to right, corresponds to a domain from
  0-255. If a bit is set, the corresponding domain can be modified by an AP
  command-request message sent to a usage domain.

If you recall from the description of an AP Queue, AP instructions include
an APQN to identify the AP queue to which an AP command-request message is to be
sent (NQAP and PQAP instructions), or from which a command-reply message is to
be received (DQAP instruction). The validity of an APQN is defined by the matrix
calculated from the APM and AQM; it is the Cartesian product of all assigned
adapter numbers (APM) with all assigned queue indexes (AQM). For example, if
adapters 1 and 2 and usage domains 5 and 6 are assigned to a guest, the APQNs
(1,5), (1,6), (2,5) and (2,6) will be valid for the guest.

The APQNs can provide secure key functionality - i.e., a private key is stored
on the adapter card for each of its domains - so each APQN must be assigned to
at most one guest or to the linux host::

   Example 1: Valid configuration:
   ------------------------------
   Guest1: adapters 1,2  domains 5,6
   Guest2: adapter  1,2  domain 7

   This is valid because both guests have a unique set of APQNs:
      Guest1 has APQNs (1,5), (1,6), (2,5), (2,6);
      Guest2 has APQNs (1,7), (2,7)

   Example 2: Valid configuration:
   ------------------------------
   Guest1: adapters 1,2 domains 5,6
   Guest2: adapters 3,4 domains 5,6

   This is also valid because both guests have a unique set of APQNs:
      Guest1 has APQNs (1,5), (1,6), (2,5), (2,6);
      Guest2 has APQNs (3,5), (3,6), (4,5), (4,6)

   Example 3: Invalid configuration:
   --------------------------------
   Guest1: adapters 1,2  domains 5,6
   Guest2: adapter  1    domains 6,7

   This is an invalid configuration because both guests have access to
   APQN (1,6).

The Design
==========
The design introduces three new objects:

1. AP matrix device
2. VFIO AP device driver (vfio_ap.ko)
3. VFIO AP mediated pass-through device

The VFIO AP device driver
-------------------------
The VFIO AP (vfio_ap) device driver serves the following purposes:

1. Provides the interfaces to secure APQNs for exclusive use of KVM guests.

2. Sets up the VFIO mediated device interfaces to manage a vfio_ap mediated
   device and creates the sysfs interfaces for assigning adapters, usage
   domains, and control domains comprising the matrix for a KVM guest.

3. Configures the APM, AQM and ADM in the APCB contained in the CRYCB referenced
   by a KVM guest's SIE state description to grant the guest access to a matrix
   of AP devices

Reserve APQNs for exclusive use of KVM guests
---------------------------------------------
The following block diagram illustrates the mechanism by which APQNs are
reserved::

				+------------------+
		 7 remove       |                  |
	   +--------------------> cex4queue driver |
	   |                    |                  |
	   |                    +------------------+
	   |
	   |
	   |                    +------------------+          +----------------+
	   |  5 register driver |                  | 3 create |                |
	   |   +---------------->   Device core    +---------->  matrix device |
	   |   |                |                  |          |                |
	   |   |                +--------^---------+          +----------------+
	   |   |                         |
	   |   |                         +-------------------+
	   |   | +-----------------------------------+       |
	   |   | |      4 register AP driver         |       | 2 register device
	   |   | |                                   |       |
  +--------+---+-v---+                      +--------+-------+-+
  |                  |                      |                  |
  |      ap_bus      +--------------------- >  vfio_ap driver  |
  |                  |       8 probe        |                  |
  +--------^---------+                      +--^--^------------+
  6 edit   |                                   |  |
    apmask |     +-----------------------------+  | 11 mdev create
    aqmask |     |           1 modprobe           |
  +--------+-----+---+           +----------------+-+         +----------------+
  |                  |           |                  |10 create|     mediated   |
  |      admin       |           | VFIO device core |--------->     matrix     |
  |                  +           |                  |         |     device     |
  +------+-+---------+           +--------^---------+         +--------^-------+
	 | |                              |                            |
	 | | 9 create vfio_ap-passthrough |                            |
	 | +------------------------------+                            |
	 +-------------------------------------------------------------+
		     12  assign adapter/domain/control domain

The process for reserving an AP queue for use by a KVM guest is:

1. The administrator loads the vfio_ap device driver
2. The vfio-ap driver during its initialization will register a single 'matrix'
   device with the device core. This will serve as the parent device for
   all vfio_ap mediated devices used to configure an AP matrix for a guest.
3. The /sys/devices/vfio_ap/matrix device is created by the device core
4. The vfio_ap device driver will register with the AP bus for AP queue devices
   of type 10 and higher (CEX4 and newer). The driver will provide the vfio_ap
   driver's probe and remove callback interfaces. Devices older than CEX4 queues
   are not supported to simplify the implementation by not needlessly
   complicating the design by supporting older devices that will go out of
   service in the relatively near future, and for which there are few older
   systems around on which to test.
5. The AP bus registers the vfio_ap device driver with the device core
6. The administrator edits the AP adapter and queue masks to reserve AP queues
   for use by the vfio_ap device driver.
7. The AP bus removes the AP queues reserved for the vfio_ap driver from the
   default zcrypt cex4queue driver.
8. The AP bus probes the vfio_ap device driver to bind the queues reserved for
   it.
9. The administrator creates a passthrough type vfio_ap mediated device to be
   used by a guest
10. The administrator assigns the adapters, usage domains and control domains
    to be exclusively used by a guest.

Set up the VFIO mediated device interfaces
------------------------------------------
The VFIO AP device driver utilizes the common interfaces of the VFIO mediated
device core driver to:

* Register an AP mediated bus driver to add a vfio_ap mediated device to and
  remove it from a VFIO group.
* Create and destroy a vfio_ap mediated device
* Add a vfio_ap mediated device to and remove it from the AP mediated bus driver
* Add a vfio_ap mediated device to and remove it from an IOMMU group

The following high-level block diagram shows the main components and interfaces
of the VFIO AP mediated device driver::

   +-------------+
   |             |
   | +---------+ | mdev_register_driver() +--------------+
   | |  Mdev   | +<-----------------------+              |
   | |  bus    | |                        | vfio_mdev.ko |
   | | driver  | +----------------------->+              |<-> VFIO user
   | +---------+ |    probe()/remove()    +--------------+    APIs
   |             |
   |  MDEV CORE  |
   |   MODULE    |
   |   mdev.ko   |
   | +---------+ | mdev_register_parent() +--------------+
   | |Physical | +<-----------------------+              |
   | | device  | |                        |  vfio_ap.ko  |<-> matrix
   | |interface| +----------------------->+              |    device
   | +---------+ |       callback         +--------------+
   +-------------+

During initialization of the vfio_ap module, the matrix device is registered
with an 'mdev_parent_ops' structure that provides the sysfs attribute
structures, mdev functions and callback interfaces for managing the mediated
matrix device.

* sysfs attribute structures:

  supported_type_groups
    The VFIO mediated device framework supports creation of user-defined
    mediated device types. These mediated device types are specified
    via the 'supported_type_groups' structure when a device is registered
    with the mediated device framework. The registration process creates the
    sysfs structures for each mediated device type specified in the
    'mdev_supported_types' sub-directory of the device being registered. Along
    with the device type, the sysfs attributes of the mediated device type are
    provided.

    The VFIO AP device driver will register one mediated device type for
    passthrough devices:

      /sys/devices/vfio_ap/matrix/mdev_supported_types/vfio_ap-passthrough

    Only the read-only attributes required by the VFIO mdev framework will
    be provided::

	... name
	... device_api
	... available_instances
	... device_api

    Where:

	* name:
	    specifies the name of the mediated device type
	* device_api:
	    the mediated device type's API
	* available_instances:
	    the number of vfio_ap mediated passthrough devices
	    that can be created
	* device_api:
	    specifies the VFIO API
  mdev_attr_groups
    This attribute group identifies the user-defined sysfs attributes of the
    mediated device. When a device is registered with the VFIO mediated device
    framework, the sysfs attribute files identified in the 'mdev_attr_groups'
    structure will be created in the vfio_ap mediated device's directory. The
    sysfs attributes for a vfio_ap mediated device are:

    assign_adapter / unassign_adapter:
      Write-only attributes for assigning/unassigning an AP adapter to/from the
      vfio_ap mediated device. To assign/unassign an adapter, the APID of the
      adapter is echoed into the respective attribute file.
    assign_domain / unassign_domain:
      Write-only attributes for assigning/unassigning an AP usage domain to/from
      the vfio_ap mediated device. To assign/unassign a domain, the domain
      number of the usage domain is echoed into the respective attribute
      file.
    matrix:
      A read-only file for displaying the APQNs derived from the Cartesian
      product of the adapter and domain numbers assigned to the vfio_ap mediated
      device.
    guest_matrix:
      A read-only file for displaying the APQNs derived from the Cartesian
      product of the adapter and domain numbers assigned to the APM and AQM
      fields respectively of the KVM guest's CRYCB. This may differ from the
      the APQNs assigned to the vfio_ap mediated device if any APQN does not
      reference a queue device bound to the vfio_ap device driver (i.e., the
      queue is not in the host's AP configuration).
    assign_control_domain / unassign_control_domain:
      Write-only attributes for assigning/unassigning an AP control domain
      to/from the vfio_ap mediated device. To assign/unassign a control domain,
      the ID of the domain to be assigned/unassigned is echoed into the
      respective attribute file.
    control_domains:
      A read-only file for displaying the control domain numbers assigned to the
      vfio_ap mediated device.

* functions:

  create:
    allocates the ap_matrix_mdev structure used by the vfio_ap driver to:

    * Store the reference to the KVM structure for the guest using the mdev
    * Store the AP matrix configuration for the adapters, domains, and control
      domains assigned via the corresponding sysfs attributes files
    * Store the AP matrix configuration for the adapters, domains and control
      domains available to a guest. A guest may not be provided access to APQNs
      referencing queue devices that do not exist, or are not bound to the
      vfio_ap device driver.

  remove:
    deallocates the vfio_ap mediated device's ap_matrix_mdev structure.
    This will be allowed only if a running guest is not using the mdev.

* callback interfaces

  open_device:
    The vfio_ap driver uses this callback to register a
    VFIO_GROUP_NOTIFY_SET_KVM notifier callback function for the matrix mdev
    devices. The open_device callback is invoked by userspace to connect the
    VFIO iommu group for the matrix mdev device to the MDEV bus. Access to the
    KVM structure used to configure the KVM guest is provided via this callback.
    The KVM structure, is used to configure the guest's access to the AP matrix
    defined via the vfio_ap mediated device's sysfs attribute files.

  close_device:
    unregisters the VFIO_GROUP_NOTIFY_SET_KVM notifier callback function for the
    matrix mdev device and deconfigures the guest's AP matrix.

  ioctl:
    this callback handles the VFIO_DEVICE_GET_INFO and VFIO_DEVICE_RESET ioctls
    defined by the vfio framework.

Configure the guest's AP resources
----------------------------------
Configuring the AP resources for a KVM guest will be performed when the
VFIO_GROUP_NOTIFY_SET_KVM notifier callback is invoked. The notifier
function is called when userspace connects to KVM. The guest's AP resources are
configured via it's APCB by:

* Setting the bits in the APM corresponding to the APIDs assigned to the
  vfio_ap mediated device via its 'assign_adapter' interface.
* Setting the bits in the AQM corresponding to the domains assigned to the
  vfio_ap mediated device via its 'assign_domain' interface.
* Setting the bits in the ADM corresponding to the domain dIDs assigned to the
  vfio_ap mediated device via its 'assign_control_domains' interface.

The linux device model precludes passing a device through to a KVM guest that
is not bound to the device driver facilitating its pass-through. Consequently,
an APQN that does not reference a queue device bound to the vfio_ap device
driver will not be assigned to a KVM guest's matrix. The AP architecture,
however, does not provide a means to filter individual APQNs from the guest's
matrix, so the adapters, domains and control domains assigned to vfio_ap
mediated device via its sysfs 'assign_adapter', 'assign_domain' and
'assign_control_domain' interfaces will be filtered before providing the AP
configuration to a guest:

* The APIDs of the adapters, the APQIs of the domains and the domain numbers of
  the control domains assigned to the matrix mdev that are not also assigned to
  the host's AP configuration will be filtered.

* Each APQN derived from the Cartesian product of the APIDs and APQIs assigned
  to the vfio_ap mdev is examined and if any one of them does not reference a
  queue device bound to the vfio_ap device driver, the adapter will not be
  plugged into the guest (i.e., the bit corresponding to its APID will not be
  set in the APM of the guest's APCB).

The CPU model features for AP
-----------------------------
The AP stack relies on the presence of the AP instructions as well as three
facilities: The AP Facilities Test (APFT) facility; the AP Query
Configuration Information (QCI) facility; and the AP Queue Interruption Control
facility. These features/facilities are made available to a KVM guest via the
following CPU model features:

1. ap: Indicates whether the AP instructions are installed on the guest. This
   feature will be enabled by KVM only if the AP instructions are installed
   on the host.

2. apft: Indicates the APFT facility is available on the guest. This facility
   can be made available to the guest only if it is available on the host (i.e.,
   facility bit 15 is set).

3. apqci: Indicates the AP QCI facility is available on the guest. This facility
   can be made available to the guest only if it is available on the host (i.e.,
   facility bit 12 is set).

4. apqi: Indicates AP Queue Interruption Control faclity is available on the
   guest. This facility can be made available to the guest only if it is
   available on the host (i.e., facility bit 65 is set).

Note: If the user chooses to specify a CPU model different than the 'host'
model to QEMU, the CPU model features and facilities need to be turned on
explicitly; for example::

     /usr/bin/qemu-system-s390x ... -cpu z13,ap=on,apqci=on,apft=on,apqi=on

A guest can be precluded from using AP features/facilities by turning them off
explicitly; for example::

     /usr/bin/qemu-system-s390x ... -cpu host,ap=off,apqci=off,apft=off,apqi=off

Note: If the APFT facility is turned off (apft=off) for the guest, the guest
will not see any AP devices. The zcrypt device drivers on the guest that
register for type 10 and newer AP devices - i.e., the cex4card and cex4queue
device drivers - need the APFT facility to ascertain the facilities installed on
a given AP device. If the APFT facility is not installed on the guest, then no
adapter or domain devices will get created by the AP bus running on the
guest because only type 10 and newer devices can be configured for guest use.

Example
=======
Let's now provide an example to illustrate how KVM guests may be given
access to AP facilities. For this example, we will show how to configure
three guests such that executing the lszcrypt command on the guests would
look like this:

Guest1
------
=========== ===== ============
CARD.DOMAIN TYPE  MODE
=========== ===== ============
05          CEX5C CCA-Coproc
05.0004     CEX5C CCA-Coproc
05.00ab     CEX5C CCA-Coproc
06          CEX5A Accelerator
06.0004     CEX5A Accelerator
06.00ab     CEX5A Accelerator
=========== ===== ============

Guest2
------
=========== ===== ============
CARD.DOMAIN TYPE  MODE
=========== ===== ============
05          CEX5C CCA-Coproc
05.0047     CEX5C CCA-Coproc
05.00ff     CEX5C CCA-Coproc
=========== ===== ============

Guest3
------
=========== ===== ============
CARD.DOMAIN TYPE  MODE
=========== ===== ============
06          CEX5A Accelerator
06.0047     CEX5A Accelerator
06.00ff     CEX5A Accelerator
=========== ===== ============

These are the steps:

1. Install the vfio_ap module on the linux host. The dependency chain for the
   vfio_ap module is:
   * iommu
   * s390
   * zcrypt
   * vfio
   * vfio_mdev
   * vfio_mdev_device
   * KVM

   To build the vfio_ap module, the kernel build must be configured with the
   following Kconfig elements selected:
   * IOMMU_SUPPORT
   * S390
   * ZCRYPT
   * S390_AP_IOMMU
   * VFIO
   * VFIO_MDEV
   * KVM

   If using make menuconfig select the following to build the vfio_ap module::

     -> Device Drivers
	-> IOMMU Hardware Support
	   select S390 AP IOMMU Support
	-> VFIO Non-Privileged userspace driver framework
	   -> Mediated device driver frramework
	      -> VFIO driver for Mediated devices
     -> I/O subsystem
	-> VFIO support for AP devices

2. Secure the AP queues to be used by the three guests so that the host can not
   access them. To secure them, there are two sysfs files that specify
   bitmasks marking a subset of the APQN range as usable only by the default AP
   queue device drivers. All remaining APQNs are available for use by
   any other device driver. The vfio_ap device driver is currently the only
   non-default device driver. The location of the sysfs files containing the
   masks are::

     /sys/bus/ap/apmask
     /sys/bus/ap/aqmask

   The 'apmask' is a 256-bit mask that identifies a set of AP adapter IDs
   (APID). Each bit in the mask, from left to right, corresponds to an APID from
   0-255. If a bit is set, the APID belongs to the subset of APQNs marked as
   available only to the default AP queue device drivers.

   The 'aqmask' is a 256-bit mask that identifies a set of AP queue indexes
   (APQI). Each bit in the mask, from left to right, corresponds to an APQI from
   0-255. If a bit is set, the APQI belongs to the subset of APQNs marked as
   available only to the default AP queue device drivers.

   The Cartesian product of the APIDs corresponding to the bits set in the
   apmask and the APQIs corresponding to the bits set in the aqmask comprise
   the subset of APQNs that can be used only by the host default device drivers.
   All other APQNs are available to the non-default device drivers such as the
   vfio_ap driver.

   Take, for example, the following masks::

      apmask:
      0x7d00000000000000000000000000000000000000000000000000000000000000

      aqmask:
      0x8000000000000000000000000000000000000000000000000000000000000000

   The masks indicate:

   * Adapters 1, 2, 3, 4, 5, and 7 are available for use by the host default
     device drivers.

   * Domain 0 is available for use by the host default device drivers

   * The subset of APQNs available for use only by the default host device
     drivers are:

     (1,0), (2,0), (3,0), (4.0), (5,0) and (7,0)

   * All other APQNs are available for use by the non-default device drivers.

   The APQN of each AP queue device assigned to the linux host is checked by the
   AP bus against the set of APQNs derived from the Cartesian product of APIDs
   and APQIs marked as available to the default AP queue device drivers. If a
   match is detected,  only the default AP queue device drivers will be probed;
   otherwise, the vfio_ap device driver will be probed.

   By default, the two masks are set to reserve all APQNs for use by the default
   AP queue device drivers. There are two ways the default masks can be changed:

   1. The sysfs mask files can be edited by echoing a string into the
      respective sysfs mask file in one of two formats:

      * An absolute hex string starting with 0x - like "0x12345678" - sets
	the mask. If the given string is shorter than the mask, it is padded
	with 0s on the right; for example, specifying a mask value of 0x41 is
	the same as specifying::

	   0x4100000000000000000000000000000000000000000000000000000000000000

	Keep in mind that the mask reads from left to right, so the mask
	above identifies device numbers 1 and 7 (01000001).

	If the string is longer than the mask, the operation is terminated with
	an error (EINVAL).

      * Individual bits in the mask can be switched on and off by specifying
	each bit number to be switched in a comma separated list. Each bit
	number string must be prepended with a ('+') or minus ('-') to indicate
	the corresponding bit is to be switched on ('+') or off ('-'). Some
	valid values are:

	   - "+0"    switches bit 0 on
	   - "-13"   switches bit 13 off
	   - "+0x41" switches bit 65 on
	   - "-0xff" switches bit 255 off

	The following example:

	      +0,-6,+0x47,-0xf0

	Switches bits 0 and 71 (0x47) on

	Switches bits 6 and 240 (0xf0) off

	Note that the bits not specified in the list remain as they were before
	the operation.

   2. The masks can also be changed at boot time via parameters on the kernel
      command line like this:

	 ap.apmask=0xffff ap.aqmask=0x40

	 This would create the following masks::

	    apmask:
	    0xffff000000000000000000000000000000000000000000000000000000000000

	    aqmask:
	    0x4000000000000000000000000000000000000000000000000000000000000000

	 Resulting in these two pools::

	    default drivers pool:    adapter 0-15, domain 1
	    alternate drivers pool:  adapter 16-255, domains 0, 2-255

   **Note:**
   Changing a mask such that one or more APQNs will be taken from a vfio_ap
   mediated device (see below) will fail with an error (EBUSY). A message
   is logged to the kernel ring buffer which can be viewed with the 'dmesg'
   command. The output identifies each APQN flagged as 'in use' and identifies
   the vfio_ap mediated device to which it is assigned; for example:

   Userspace may not re-assign queue 05.0054 already assigned to 62177883-f1bb-47f0-914d-32a22e3a8804
   Userspace may not re-assign queue 04.0054 already assigned to cef03c3c-903d-4ecc-9a83-40694cb8aee4

Securing the APQNs for our example
----------------------------------
   To secure the AP queues 05.0004, 05.0047, 05.00ab, 05.00ff, 06.0004, 06.0047,
   06.00ab, and 06.00ff for use by the vfio_ap device driver, the corresponding
   APQNs can be removed from the default masks using either of the following
   commands::

      echo -5,-6 > /sys/bus/ap/apmask

      echo -4,-0x47,-0xab,-0xff > /sys/bus/ap/aqmask

   Or the masks can be set as follows::

      echo 0xf9ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff \
      > apmask

      echo 0xf7fffffffffffffffeffffffffffffffffffffffffeffffffffffffffffffffe \
      > aqmask

   This will result in AP queues 05.0004, 05.0047, 05.00ab, 05.00ff, 06.0004,
   06.0047, 06.00ab, and 06.00ff getting bound to the vfio_ap device driver. The
   sysfs directory for the vfio_ap device driver will now contain symbolic links
   to the AP queue devices bound to it::

     /sys/bus/ap
     ... [drivers]
     ...... [vfio_ap]
     ......... [05.0004]
     ......... [05.0047]
     ......... [05.00ab]
     ......... [05.00ff]
     ......... [06.0004]
     ......... [06.0047]
     ......... [06.00ab]
     ......... [06.00ff]

   Keep in mind that only type 10 and newer adapters (i.e., CEX4 and later)
   can be bound to the vfio_ap device driver. The reason for this is to
   simplify the implementation by not needlessly complicating the design by
   supporting older devices that will go out of service in the relatively near
   future and for which there are few older systems on which to test.

   The administrator, therefore, must take care to secure only AP queues that
   can be bound to the vfio_ap device driver. The device type for a given AP
   queue device can be read from the parent card's sysfs directory. For example,
   to see the hardware type of the queue 05.0004:

     cat /sys/bus/ap/devices/card05/hwtype

   The hwtype must be 10 or higher (CEX4 or newer) in order to be bound to the
   vfio_ap device driver.

3. Create the mediated devices needed to configure the AP matrixes for the
   three guests and to provide an interface to the vfio_ap driver for
   use by the guests::

     /sys/devices/vfio_ap/matrix/
     --- [mdev_supported_types]
     ------ [vfio_ap-passthrough] (passthrough vfio_ap mediated device type)
     --------- create
     --------- [devices]

   To create the mediated devices for the three guests::

	uuidgen > create
	uuidgen > create
	uuidgen > create

	or

	echo $uuid1 > create
	echo $uuid2 > create
	echo $uuid3 > create

   This will create three mediated devices in the [devices] subdirectory named
   after the UUID written to the create attribute file. We call them $uuid1,
   $uuid2 and $uuid3 and this is the sysfs directory structure after creation::

     /sys/devices/vfio_ap/matrix/
     --- [mdev_supported_types]
     ------ [vfio_ap-passthrough]
     --------- [devices]
     ------------ [$uuid1]
     --------------- assign_adapter
     --------------- assign_control_domain
     --------------- assign_domain
     --------------- matrix
     --------------- unassign_adapter
     --------------- unassign_control_domain
     --------------- unassign_domain

     ------------ [$uuid2]
     --------------- assign_adapter
     --------------- assign_control_domain
     --------------- assign_domain
     --------------- matrix
     --------------- unassign_adapter
     ----------------unassign_control_domain
     ----------------unassign_domain

     ------------ [$uuid3]
     --------------- assign_adapter
     --------------- assign_control_domain
     --------------- assign_domain
     --------------- matrix
     --------------- unassign_adapter
     ----------------unassign_control_domain
     ----------------unassign_domain

   Note *****: The vfio_ap mdevs do not persist across reboots unless the
               mdevctl tool is used to create and persist them.

4. The administrator now needs to configure the matrixes for the mediated
   devices $uuid1 (for Guest1), $uuid2 (for Guest2) and $uuid3 (for Guest3).

   This is how the matrix is configured for Guest1::

      echo 5 > assign_adapter
      echo 6 > assign_adapter
      echo 4 > assign_domain
      echo 0xab > assign_domain

   Control domains can similarly be assigned using the assign_control_domain
   sysfs file.

   If a mistake is made configuring an adapter, domain or control domain,
   you can use the unassign_xxx files to unassign the adapter, domain or
   control domain.

   To display the matrix configuration for Guest1::

	 cat matrix

   To display the matrix that is or will be assigned to Guest1::

	 cat guest_matrix

   This is how the matrix is configured for Guest2::

      echo 5 > assign_adapter
      echo 0x47 > assign_domain
      echo 0xff > assign_domain

   This is how the matrix is configured for Guest3::

      echo 6 > assign_adapter
      echo 0x47 > assign_domain
      echo 0xff > assign_domain

   In order to successfully assign an adapter:

   * The adapter number specified must represent a value from 0 up to the
     maximum adapter number configured for the system. If an adapter number
     higher than the maximum is specified, the operation will terminate with
     an error (ENODEV).

     Note: The maximum adapter number can be obtained via the sysfs
	   /sys/bus/ap/ap_max_adapter_id attribute file.

   * Each APQN derived from the Cartesian product of the APID of the adapter
     being assigned and the APQIs of the domains previously assigned:

     - Must only be available to the vfio_ap device driver as specified in the
       sysfs /sys/bus/ap/apmask and /sys/bus/ap/aqmask attribute files. If even
       one APQN is reserved for use by the host device driver, the operation
       will terminate with an error (EADDRNOTAVAIL).

     - Must NOT be assigned to another vfio_ap mediated device. If even one APQN
       is assigned to another vfio_ap mediated device, the operation will
       terminate with an error (EBUSY).

     - Must NOT be assigned while the sysfs /sys/bus/ap/apmask and
       sys/bus/ap/aqmask attribute files are being edited or the operation may
       terminate with an error (EBUSY).

   In order to successfully assign a domain:

   * The domain number specified must represent a value from 0 up to the
     maximum domain number configured for the system. If a domain number
     higher than the maximum is specified, the operation will terminate with
     an error (ENODEV).

     Note: The maximum domain number can be obtained via the sysfs
	   /sys/bus/ap/ap_max_domain_id attribute file.

    * Each APQN derived from the Cartesian product of the APQI of the domain
      being assigned and the APIDs of the adapters previously assigned:

     - Must only be available to the vfio_ap device driver as specified in the
       sysfs /sys/bus/ap/apmask and /sys/bus/ap/aqmask attribute files. If even
       one APQN is reserved for use by the host device driver, the operation
       will terminate with an error (EADDRNOTAVAIL).

     - Must NOT be assigned to another vfio_ap mediated device. If even one APQN
       is assigned to another vfio_ap mediated device, the operation will
       terminate with an error (EBUSY).

     - Must NOT be assigned while the sysfs /sys/bus/ap/apmask and
       sys/bus/ap/aqmask attribute files are being edited or the operation may
       terminate with an error (EBUSY).

   In order to successfully assign a control domain:

   * The domain number specified must represent a value from 0 up to the maximum
     domain number configured for the system. If a control domain number higher
     than the maximum is specified, the operation will terminate with an
     error (ENODEV).

5. Start Guest1::

     /usr/bin/qemu-system-s390x ... -cpu host,ap=on,apqci=on,apft=on,apqi=on \
	-device vfio-ap,sysfsdev=/sys/devices/vfio_ap/matrix/$uuid1 ...

7. Start Guest2::

     /usr/bin/qemu-system-s390x ... -cpu host,ap=on,apqci=on,apft=on,apqi=on \
	-device vfio-ap,sysfsdev=/sys/devices/vfio_ap/matrix/$uuid2 ...

7. Start Guest3::

     /usr/bin/qemu-system-s390x ... -cpu host,ap=on,apqci=on,apft=on,apqi=on \
	-device vfio-ap,sysfsdev=/sys/devices/vfio_ap/matrix/$uuid3 ...

When the guest is shut down, the vfio_ap mediated devices may be removed.

Using our example again, to remove the vfio_ap mediated device $uuid1::

   /sys/devices/vfio_ap/matrix/
      --- [mdev_supported_types]
      ------ [vfio_ap-passthrough]
      --------- [devices]
      ------------ [$uuid1]
      --------------- remove

::

   echo 1 > remove

This will remove all of the matrix mdev device's sysfs structures including
the mdev device itself. To recreate and reconfigure the matrix mdev device,
all of the steps starting with step 3 will have to be performed again. Note
that the remove will fail if a guest using the vfio_ap mdev is still running.

It is not necessary to remove a vfio_ap mdev, but one may want to
remove it if no guest will use it during the remaining lifetime of the linux
host. If the vfio_ap mdev is removed, one may want to also reconfigure
the pool of adapters and queues reserved for use by the default drivers.

Hot plug/unplug support:
========================
An adapter, domain or control domain may be hot plugged into a running KVM
guest by assigning it to the vfio_ap mediated device being used by the guest if
the following conditions are met:

* The adapter, domain or control domain must also be assigned to the host's
  AP configuration.

* Each APQN derived from the Cartesian product comprised of the APID of the
  adapter being assigned and the APQIs of the domains assigned must reference a
  queue device bound to the vfio_ap device driver.

* To hot plug a domain, each APQN derived from the Cartesian product
  comprised of the APQI of the domain being assigned and the APIDs of the
  adapters assigned must reference a queue device bound to the vfio_ap device
  driver.

An adapter, domain or control domain may be hot unplugged from a running KVM
guest by unassigning it from the vfio_ap mediated device being used by the
guest.

Over-provisioning of AP queues for a KVM guest:
===============================================
Over-provisioning is defined herein as the assignment of adapters or domains to
a vfio_ap mediated device that do not reference AP devices in the host's AP
configuration. The idea here is that when the adapter or domain becomes
available, it will be automatically hot-plugged into the KVM guest using
the vfio_ap mediated device to which it is assigned as long as each new APQN
resulting from plugging it in references a queue device bound to the vfio_ap
device driver.

Limitations
===========
Live guest migration is not supported for guests using AP devices without
intervention by a system administrator. Before a KVM guest can be migrated,
the vfio_ap mediated device must be removed. Unfortunately, it can not be
removed manually (i.e., echo 1 > /sys/devices/vfio_ap/matrix/$UUID/remove) while
the mdev is in use by a KVM guest. If the guest is being emulated by QEMU,
its mdev can be hot unplugged from the guest in one of two ways:

1. If the KVM guest was started with libvirt, you can hot unplug the mdev via
   the following commands:

      virsh detach-device <guestname> <path-to-device-xml>

      For example, to hot unplug mdev 62177883-f1bb-47f0-914d-32a22e3a8804 from
      the guest named 'my-guest':

         virsh detach-device my-guest ~/config/my-guest-hostdev.xml

            The contents of my-guest-hostdev.xml:

.. code-block:: xml

            <hostdev mode='subsystem' type='mdev' managed='no' model='vfio-ap'>
              <source>
                <address uuid='62177883-f1bb-47f0-914d-32a22e3a8804'/>
              </source>
            </hostdev>


      virsh qemu-monitor-command <guest-name> --hmp "device-del <device-id>"

      For example, to hot unplug the vfio_ap mediated device identified on the
      qemu command line with 'id=hostdev0' from the guest named 'my-guest':

.. code-block:: sh

         virsh qemu-monitor-command my-guest --hmp "device_del hostdev0"

2. A vfio_ap mediated device can be hot unplugged by attaching the qemu monitor
   to the guest and using the following qemu monitor command:

      (QEMU) device-del id=<device-id>

      For example, to hot unplug the vfio_ap mediated device that was specified
      on the qemu command line with 'id=hostdev0' when the guest was started:

         (QEMU) device-del id=hostdev0

After live migration of the KVM guest completes, an AP configuration can be
restored to the KVM guest by hot plugging a vfio_ap mediated device on the target
system into the guest in one of two ways:

1. If the KVM guest was started with libvirt, you can hot plug a matrix mediated
   device into the guest via the following virsh commands:

   virsh attach-device <guestname> <path-to-device-xml>

      For example, to hot plug mdev 62177883-f1bb-47f0-914d-32a22e3a8804 into
      the guest named 'my-guest':

         virsh attach-device my-guest ~/config/my-guest-hostdev.xml

            The contents of my-guest-hostdev.xml:

.. code-block:: xml

            <hostdev mode='subsystem' type='mdev' managed='no' model='vfio-ap'>
              <source>
                <address uuid='62177883-f1bb-47f0-914d-32a22e3a8804'/>
              </source>
            </hostdev>


   virsh qemu-monitor-command <guest-name> --hmp \
   "device_add vfio-ap,sysfsdev=<path-to-mdev>,id=<device-id>"

      For example, to hot plug the vfio_ap mediated device
      62177883-f1bb-47f0-914d-32a22e3a8804 into the guest named 'my-guest' with
      device-id hostdev0:

      virsh qemu-monitor-command my-guest --hmp \
      "device_add vfio-ap,\
      sysfsdev=/sys/devices/vfio_ap/matrix/62177883-f1bb-47f0-914d-32a22e3a8804,\
      id=hostdev0"

2. A vfio_ap mediated device can be hot plugged by attaching the qemu monitor
   to the guest and using the following qemu monitor command:

      (qemu) device_add "vfio-ap,sysfsdev=<path-to-mdev>,id=<device-id>"

      For example, to plug the vfio_ap mediated device
      62177883-f1bb-47f0-914d-32a22e3a8804 into the guest with the device-id
      hostdev0:

         (QEMU) device-add "vfio-ap,\
         sysfsdev=/sys/devices/vfio_ap/matrix/62177883-f1bb-47f0-914d-32a22e3a8804,\
         id=hostdev0"
