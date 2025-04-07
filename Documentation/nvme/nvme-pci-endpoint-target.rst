.. SPDX-License-Identifier: GPL-2.0

=================================
NVMe PCI Endpoint Function Target
=================================

:Author: Damien Le Moal <dlemoal@kernel.org>

The NVMe PCI endpoint function target driver implements a NVMe PCIe controller
using a NVMe fabrics target controller configured with the PCI transport type.

Overview
========

The NVMe PCI endpoint function target driver allows exposing a NVMe target
controller over a PCIe link, thus implementing an NVMe PCIe device similar to a
regular M.2 SSD. The target controller is created in the same manner as when
using NVMe over fabrics: the controller represents the interface to an NVMe
subsystem using a port. The port transfer type must be configured to be
"pci". The subsystem can be configured to have namespaces backed by regular
files or block devices, or can use NVMe passthrough to expose to the PCI host an
existing physical NVMe device or a NVMe fabrics host controller (e.g. a NVMe TCP
host controller).

The NVMe PCI endpoint function target driver relies as much as possible on the
NVMe target core code to parse and execute NVMe commands submitted by the PCIe
host. However, using the PCI endpoint framework API and DMA API, the driver is
also responsible for managing all data transfers over the PCIe link. This
implies that the NVMe PCI endpoint function target driver implements several
NVMe data structure management and some NVMe command parsing.

1) The driver manages retrieval of NVMe commands in submission queues using DMA
   if supported, or MMIO otherwise. Each command retrieved is then executed
   using a work item to maximize performance with the parallel execution of
   multiple commands on different CPUs. The driver uses a work item to
   constantly poll the doorbell of all submission queues to detect command
   submissions from the PCIe host.

2) The driver transfers completion queues entries of completed commands to the
   PCIe host using MMIO copy of the entries in the host completion queue.
   After posting completion entries in a completion queue, the driver uses the
   PCI endpoint framework API to raise an interrupt to the host to signal the
   commands completion.

3) For any command that has a data buffer, the NVMe PCI endpoint target driver
   parses the command PRPs or SGLs lists to create a list of PCI address
   segments representing the mapping of the command data buffer on the host.
   The command data buffer is transferred over the PCIe link using this list of
   PCI address segments using DMA, if supported. If DMA is not supported, MMIO
   is used, which results in poor performance. For write commands, the command
   data buffer is transferred from the host into a local memory buffer before
   executing the command using the target core code. For read commands, a local
   memory buffer is allocated to execute the command and the content of that
   buffer is transferred to the host once the command completes.

Controller Capabilities
-----------------------

The NVMe capabilities exposed to the PCIe host through the BAR 0 registers
are almost identical to the capabilities of the NVMe target controller
implemented by the target core code. There are some exceptions.

1) The NVMe PCI endpoint target driver always sets the controller capability
   CQR bit to request "Contiguous Queues Required". This is to facilitate the
   mapping of a queue PCI address range to the local CPU address space.

2) The doorbell stride (DSTRB) is always set to be 4B

3) Since the PCI endpoint framework does not provide a way to handle PCI level
   resets, the controller capability NSSR bit (NVM Subsystem Reset Supported)
   is always cleared.

4) The boot partition support (BPS), Persistent Memory Region Supported (PMRS)
   and Controller Memory Buffer Supported (CMBS) capabilities are never
   reported.

Supported Features
------------------

The NVMe PCI endpoint target driver implements support for both PRPs and SGLs.
The driver also implements IRQ vector coalescing and submission queue
arbitration burst.

The maximum number of queues and the maximum data transfer size (MDTS) are
configurable through configfs before starting the controller. To avoid issues
with excessive local memory usage for executing commands, MDTS defaults to 512
KB and is limited to a maximum of 2 MB (arbitrary limit).

Minimum number of PCI Address Mapping Windows Required
------------------------------------------------------

Most PCI endpoint controllers provide a limited number of mapping windows for
mapping a PCI address range to local CPU memory addresses. The NVMe PCI
endpoint target controllers uses mapping windows for the following.

1) One memory window for raising MSI or MSI-X interrupts
2) One memory window for MMIO transfers
3) One memory window for each completion queue

Given the highly asynchronous nature of the NVMe PCI endpoint target driver
operation, the memory windows as described above will generally not be used
simultaneously, but that may happen. So a safe maximum number of completion
queues that can be supported is equal to the total number of memory mapping
windows of the PCI endpoint controller minus two. E.g. for an endpoint PCI
controller with 32 outbound memory windows available, up to 30 completion
queues can be safely operated without any risk of getting PCI address mapping
errors due to the lack of memory windows.

Maximum Number of Queue Pairs
-----------------------------

Upon binding of the NVMe PCI endpoint target driver to the PCI endpoint
controller, BAR 0 is allocated with enough space to accommodate the admin queue
and multiple I/O queues. The maximum of number of I/O queues pairs that can be
supported is limited by several factors.

1) The NVMe target core code limits the maximum number of I/O queues to the
   number of online CPUs.
2) The total number of queue pairs, including the admin queue, cannot exceed
   the number of MSI-X or MSI vectors available.
3) The total number of completion queues must not exceed the total number of
   PCI mapping windows minus 2 (see above).

The NVMe endpoint function driver allows configuring the maximum number of
queue pairs through configfs.

Limitations and NVMe Specification Non-Compliance
-------------------------------------------------

Similar to the NVMe target core code, the NVMe PCI endpoint target driver does
not support multiple submission queues using the same completion queue. All
submission queues must specify a unique completion queue.


User Guide
==========

This section describes the hardware requirements and how to setup an NVMe PCI
endpoint target device.

Kernel Requirements
-------------------

The kernel must be compiled with the configuration options CONFIG_PCI_ENDPOINT,
CONFIG_PCI_ENDPOINT_CONFIGFS, and CONFIG_NVME_TARGET_PCI_EPF enabled.
CONFIG_PCI, CONFIG_BLK_DEV_NVME and CONFIG_NVME_TARGET must also be enabled
(obviously).

In addition to this, at least one PCI endpoint controller driver should be
available for the endpoint hardware used.

To facilitate testing, enabling the null-blk driver (CONFIG_BLK_DEV_NULL_BLK)
is also recommended. With this, a simple setup using a null_blk block device
as a subsystem namespace can be used.

Hardware Requirements
---------------------

To use the NVMe PCI endpoint target driver, at least one endpoint controller
device is required.

To find the list of endpoint controller devices in the system::

       # ls /sys/class/pci_epc/
        a40000000.pcie-ep

If PCI_ENDPOINT_CONFIGFS is enabled::

       # ls /sys/kernel/config/pci_ep/controllers
        a40000000.pcie-ep

The endpoint board must of course also be connected to a host with a PCI cable
with RX-TX signal swapped. If the host PCI slot used does not have
plug-and-play capabilities, the host should be powered off when the NVMe PCI
endpoint device is configured.

NVMe Endpoint Device
--------------------

Creating an NVMe endpoint device is a two step process. First, an NVMe target
subsystem and port must be defined. Second, the NVMe PCI endpoint device must
be setup and bound to the subsystem and port created.

Creating a NVMe Subsystem and Port
----------------------------------

Details about how to configure a NVMe target subsystem and port are outside the
scope of this document. The following only provides a simple example of a port
and subsystem with a single namespace backed by a null_blk device.

First, make sure that configfs is enabled::

       # mount -t configfs none /sys/kernel/config

Next, create a null_blk device (default settings give a 250 GB device without
memory backing). The block device created will be /dev/nullb0 by default::

        # modprobe null_blk
        # ls /dev/nullb0
        /dev/nullb0

The NVMe PCI endpoint function target driver must be loaded::

        # modprobe nvmet_pci_epf
        # lsmod | grep nvmet
        nvmet_pci_epf          32768  0
        nvmet                 118784  1 nvmet_pci_epf
        nvme_core             131072  2 nvmet_pci_epf,nvmet

Now, create a subsystem and a port that we will use to create a PCI target
controller when setting up the NVMe PCI endpoint target device. In this
example, the port is created with a maximum of 4 I/O queue pairs::

        # cd /sys/kernel/config/nvmet/subsystems
        # mkdir nvmepf.0.nqn
        # echo -n "Linux-pci-epf" > nvmepf.0.nqn/attr_model
        # echo "0x1b96" > nvmepf.0.nqn/attr_vendor_id
        # echo "0x1b96" > nvmepf.0.nqn/attr_subsys_vendor_id
        # echo 1 > nvmepf.0.nqn/attr_allow_any_host
        # echo 4 > nvmepf.0.nqn/attr_qid_max

Next, create and enable the subsystem namespace using the null_blk block
device::

        # mkdir nvmepf.0.nqn/namespaces/1
        # echo -n "/dev/nullb0" > nvmepf.0.nqn/namespaces/1/device_path
        # echo 1 > "nvmepf.0.nqn/namespaces/1/enable"

Finally, create the target port and link it to the subsystem::

        # cd /sys/kernel/config/nvmet/ports
        # mkdir 1
        # echo -n "pci" > 1/addr_trtype
        # ln -s /sys/kernel/config/nvmet/subsystems/nvmepf.0.nqn \
                /sys/kernel/config/nvmet/ports/1/subsystems/nvmepf.0.nqn

Creating a NVMe PCI Endpoint Device
-----------------------------------

With the NVMe target subsystem and port ready for use, the NVMe PCI endpoint
device can now be created and enabled. The NVMe PCI endpoint target driver
should already be loaded (that is done automatically when the port is created)::

        # ls /sys/kernel/config/pci_ep/functions
        nvmet_pci_epf

Next, create function 0::

        # cd /sys/kernel/config/pci_ep/functions/nvmet_pci_epf
        # mkdir nvmepf.0
        # ls nvmepf.0/
        baseclass_code    msix_interrupts   secondary
        cache_line_size   nvme              subclass_code
        deviceid          primary           subsys_id
        interrupt_pin     progif_code       subsys_vendor_id
        msi_interrupts    revid             vendorid

Configure the function using any device ID (the vendor ID for the device will
be automatically set to the same value as the NVMe target subsystem vendor
ID)::

        # cd /sys/kernel/config/pci_ep/functions/nvmet_pci_epf
        # echo 0xBEEF > nvmepf.0/deviceid
        # echo 32 > nvmepf.0/msix_interrupts

If the PCI endpoint controller used does not support MSI-X, MSI can be
configured instead::

        # echo 32 > nvmepf.0/msi_interrupts

Next, let's bind our endpoint device with the target subsystem and port that we
created::

        # echo 1 > nvmepf.0/nvme/portid
        # echo "nvmepf.0.nqn" > nvmepf.0/nvme/subsysnqn

The endpoint function can then be bound to the endpoint controller and the
controller started::

        # cd /sys/kernel/config/pci_ep
        # ln -s functions/nvmet_pci_epf/nvmepf.0 controllers/a40000000.pcie-ep/
        # echo 1 > controllers/a40000000.pcie-ep/start

On the endpoint machine, kernel messages will show information as the NVMe
target device and endpoint device are created and connected.

.. code-block:: text

        null_blk: disk nullb0 created
        null_blk: module loaded
        nvmet: adding nsid 1 to subsystem nvmepf.0.nqn
        nvmet_pci_epf nvmet_pci_epf.0: PCI endpoint controller supports MSI-X, 32 vectors
        nvmet: Created nvm controller 1 for subsystem nvmepf.0.nqn for NQN nqn.2014-08.org.nvmexpress:uuid:2ab90791-2246-4fbb-961d-4c3d5a5a0176.
        nvmet_pci_epf nvmet_pci_epf.0: New PCI ctrl "nvmepf.0.nqn", 4 I/O queues, mdts 524288 B

PCI Root-Complex Host
---------------------

Booting the PCI host will result in the initialization of the PCIe link (this
may be signaled by the PCI endpoint driver with a kernel message). A kernel
message on the endpoint will also signal when the host NVMe driver enables the
device controller::

        nvmet_pci_epf nvmet_pci_epf.0: Enabling controller

On the host side, the NVMe PCI endpoint function target device will is
discoverable as a PCI device, with the vendor ID and device ID as configured::

        # lspci -n
        0000:01:00.0 0108: 1b96:beef

An this device will be recognized as an NVMe device with a single namespace::

        # lsblk
        NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
        nvme0n1     259:0    0   250G  0 disk

The NVMe endpoint block device can then be used as any other regular NVMe
namespace block device. The *nvme* command line utility can be used to get more
detailed information about the endpoint device::

        # nvme id-ctrl /dev/nvme0
        NVME Identify Controller:
        vid       : 0x1b96
        ssvid     : 0x1b96
        sn        : 94993c85650ef7bcd625
        mn        : Linux-pci-epf
        fr        : 6.13.0-r
        rab       : 6
        ieee      : 000000
        cmic      : 0xb
        mdts      : 7
        cntlid    : 0x1
        ver       : 0x20100
        ...


Endpoint Bindings
=================

The NVMe PCI endpoint target driver uses the PCI endpoint configfs device
attributes as follows.

================   ===========================================================
vendorid           Ignored (the vendor id of the NVMe target subsystem is used)
deviceid           Anything is OK (e.g. PCI_ANY_ID)
revid              Do not care
progif_code        Must be 0x02 (NVM Express)
baseclass_code     Must be 0x01 (PCI_BASE_CLASS_STORAGE)
subclass_code      Must be 0x08 (Non-Volatile Memory controller)
cache_line_size    Do not care
subsys_vendor_id   Ignored (the subsystem vendor id of the NVMe target subsystem
		   is used)
subsys_id          Anything is OK (e.g. PCI_ANY_ID)
msi_interrupts     At least equal to the number of queue pairs desired
msix_interrupts    At least equal to the number of queue pairs desired
interrupt_pin      Interrupt PIN to use if MSI and MSI-X are not supported
================   ===========================================================

The NVMe PCI endpoint target function also has some specific configurable
fields defined in the *nvme* subdirectory of the function directory. These
fields are as follows.

================   ===========================================================
mdts_kb            Maximum data transfer size in KiB (default: 512)
portid             The ID of the target port to use
subsysnqn          The NQN of the target subsystem to use
================   ===========================================================
