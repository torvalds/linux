======================================================
Intel Many Integrated Core (MIC) architecture overview
======================================================

An Intel MIC X100 device is a PCIe form factor add-in coprocessor
card based on the Intel Many Integrated Core (MIC) architecture
that runs a Linux OS. It is a PCIe endpoint in a platform and therefore
implements the three required standard address spaces i.e. configuration,
memory and I/O. The host OS loads a device driver as is typical for
PCIe devices. The card itself runs a bootstrap after reset that
transfers control to the card OS downloaded from the host driver. The
host driver supports OSPM suspend and resume operations. It shuts down
the card during suspend and reboots the card OS during resume.
The card OS as shipped by Intel is a Linux kernel with modifications
for the X100 devices.

Since it is a PCIe card, it does not have the ability to host hardware
devices for networking, storage and console. We provide these devices
on X100 coprocessors thus enabling a self-bootable equivalent
environment for applications. A key benefit of our solution is that it
leverages the standard virtio framework for network, disk and console
devices, though in our case the virtio framework is used across a PCIe
bus. A Virtio Over PCIe (VOP) driver allows creating user space
backends or devices on the host which are used to probe virtio drivers
for these devices on the MIC card. The existing VRINGH infrastructure
in the kernel is used to access virtio rings from the host. The card
VOP driver allows card virtio drivers to communicate with their user
space backends on the host via a device page. Ring 3 apps on the host
can add, remove and configure virtio devices. A thin MIC specific
virtio_config_ops is implemented which is borrowed heavily from
previous similar implementations in lguest and s390.

MIC PCIe card has a dma controller with 8 channels. These channels are
shared between the host s/w and the card s/w. 0 to 3 are used by host
and 4 to 7 by card. As the dma device doesn't show up as PCIe device,
a virtual bus called mic bus is created and virtual dma devices are
created on it by the host/card drivers. On host the channels are private
and used only by the host driver to transfer data for the virtio devices.

The Symmetric Communication Interface (SCIF (pronounced as skiff)) is a
low level communications API across PCIe currently implemented for MIC.
More details are available at scif_overview.txt.

The Coprocessor State Management (COSM) driver on the host allows for
boot, shutdown and reset of Intel MIC devices. It communicates with a COSM
"client" driver on the MIC cards over SCIF to perform these functions.

Here is a block diagram of the various components described above. The
virtio backends are situated on the host rather than the card given better
single threaded performance for the host compared to MIC, the ability of
the host to initiate DMA's to/from the card using the MIC DMA engine and
the fact that the virtio block storage backend can only be on the host::

               +----------+           |             +----------+
               | Card OS  |           |             | Host OS  |
               +----------+           |             +----------+
                                      |
        +-------+ +--------+ +------+ | +---------+  +--------+ +--------+
        | Virtio| |Virtio  | |Virtio| | |Virtio   |  |Virtio  | |Virtio  |
        | Net   | |Console | |Block | | |Net      |  |Console | |Block   |
        | Driver| |Driver  | |Driver| | |backend  |  |backend | |backend |
        +---+---+ +---+----+ +--+---+ | +---------+  +----+---+ +--------+
            |         |         |     |      |            |         |
            |         |         |     |User  |            |         |
            |         |         |     |------|------------|--+------|-------
            +---------+---------+     |Kernel                |
                      |               |                      |
  +---------+     +---+----+ +------+ | +------+ +------+ +--+---+  +-------+
  |MIC DMA  |     |  VOP   | | SCIF | | | SCIF | | COSM | | VOP  |  |MIC DMA|
  +---+-----+     +---+----+ +--+---+ | +--+---+ +--+---+ +------+  +----+--+
      |               |         |     |    |        |                    |
  +---+-----+     +---+----+ +--+---+ | +--+---+ +--+---+ +------+  +----+--+
  |MIC      |     |  VOP   | |SCIF  | | |SCIF  | | COSM | | VOP  |  | MIC   |
  |HW Bus   |     |  HW Bus| |HW Bus| | |HW Bus| | Bus  | |HW Bus|  |HW Bus |
  +---------+     +--------+ +--+---+ | +--+---+ +------+ +------+  +-------+
      |               |         |     |       |     |                    |
      |   +-----------+--+      |     |       |    +---------------+     |
      |   |Intel MIC     |      |     |       |    |Intel MIC      |     |
      |   |Card Driver   |      |     |       |    |Host Driver    |     |
      +---+--------------+------+     |       +----+---------------+-----+
                 |                    |                   |
             +-------------------------------------------------------------+
             |                                                             |
             |                    PCIe Bus                                 |
             +-------------------------------------------------------------+
