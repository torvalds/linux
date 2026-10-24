.. SPDX-License-Identifier: GPL-2.0

==============
VFIO Selftests
==============

VFIO selftests are built on top of the kernel's selftests framework and are
located in ``tools/testing/selftests/vfio``.

VFIO selftests enable kernel developers to write and run tests that take the
form of userspace programs that interact with VFIO and IOMMUFD uAPIs. VFIO
selftests can be used to write functional tests for new features, regression
tests for bugs, and performance tests for optimizations.

These tests are designed to interact with real PCI devices, i.e. they do not
rely on mocking out or faking any behavior in the kernel. This allows the tests
to exercise not only VFIO but also IOMMUFD, the IOMMU driver, interrupt
remapping, IRQ handling, etc.

Running Tests
=============

Running VFIO selftests requires a device bound to the ``vfio-pci`` driver.
There are scripts in ``tools/testing/selftests/vfio/scripts/`` to help with
setting one up.

Picking a Device
----------------

Some VFIO selftests require a device with a supported driver implementation in
the selftests library (see `Driver Framework`_), while other tests (such as
basic PCI configuration space or reset tests) can use any arbitrary PCI device.

This section will be expanded in the future to guide developers on how to find
a device with a supported driver. In the meantime, the selftests library
includes driver support for the following devices:

- Intel IGB Gigabit Ethernet controllers
- Intel IOAT (I/O Acceleration Technology) DMA engines
- Intel DSA (Data Streaming Accelerator)
- NVIDIA Falcon engines (found in many NVIDIA GPUs, e.g., GTX 1080 or RTX 2080)

Note that using Intel DSA devices requires setting the module parameter
``vfio_pci.disable_denylist=Y`` to allow ``vfio-pci`` to bind to DSA devices.

Setting up Devices
------------------

.. code-block:: sh

    tools/testing/selftests/vfio/scripts/setup.sh 0000:01:00.0

The script handles unbinding the device from its current driver and binding it
to ``vfio-pci``. Metadata about this device is stored in
``/tmp/vfio-selftests-devices`` so that the device can be cleaned up later
(bound back to its original driver).

Execution
---------

VFIO selftests expect the PCI device BDF string as their final command-line
argument. e.g.

.. code-block:: sh

    tools/testing/selftests/vfio/vfio_pci_device_test 0000:01:00.0

Alternatively, the PCI device BDF can be passed via the ``VFIO_SELFTESTS_BDF``
environment variable, which can be useful when running all tests:

.. code-block:: sh

	export VFIO_SELFTESTS_BDF=0000:01:00.0
	make -C tools/testing/selftests TARGETS=vfio install
	tools/testing/selftests/kselftest_install/run_kselftest.sh -c vfio

Cleanup
-------

.. code-block:: sh

    tools/testing/selftests/vfio/scripts/cleanup.sh

This script handles unbinding all previously setup devices from ``vfio-pci``
and binding them back to their original driver. Specific devices can be cleaned
up by passing their BDF string to the cleanup script.

Writing Tests
=============

VFIO selftests leverage the ``kselftest_harness.h`` macros to structure setup
and teardown scenarios. A basic test involves:

1. Declaring a test FIXTURE containing the device and IOMMU state.
2. Generating test variants that span the different IOMMU modes utilizing
   ``FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES()``.
3. Initializing the device and binding the default driver ops.

For a complete example of writing a VFIO selftest, refer to
``tools/testing/selftests/vfio/vfio_pci_device_test.c``.

VFIO Selftests Library (libvfio)
================================

The selftests directory contains a helper library compiled from the ``lib/``
subdirectory. It provides structures and helper functions to handle standard
operations like opening VFIO devices, creating VFIO containers, and interacting
with VFIO and IOMMUFD.

All VFIO selftests have libvfio linked in by default. Tests can access the
library by including ``<libvfio.h>``.

Core Objects
------------

The VFIO selftests framework revolves around two distinct core objects:
``struct iommu`` and ``struct vfio_pci_device``. It is important to understand
the distinction and relationship between them:

- ``struct iommu`` represents an IOMMU domain and the API used to program it
  (e.g. legacy VFIO Type1 vs IOMMUFD). It is responsible for managing the I/O
  address space, handling DMA mappings, and translating between host virtual
  addresses (HVA) and IO virtual addresses (IOVA).

- ``struct vfio_pci_device`` represents a single PCI device. It encapsulates
  the VFIO device file descriptor, handles access to the PCI configuration
  space, manages memory-mapped BARs, and drives device interrupts.

A typical test initializes an IOMMU instance first, then passes it when
initializing one or more PCI devices, linking the devices to the shared IOMMU
domain.

``struct iommu``
^^^^^^^^^^^^^^^^

The ``struct iommu`` abstracts away the complexity of managing VFIO containers,
IOMMU groups, and IOMMUFD contexts. A test allocates its IOMMU handle using
``iommu_init()`` passing one of the supported IOMMU modes:

.. code-block:: c

    struct iommu *iommu = iommu_init(MODE_IOMMUFD);
    ...
    iommu_cleanup(iommu);

The selftest framework is designed to work uniformly across all standard kernel
IOMMU APIs. The defined modes (in ``iommu.h``) include:

- ``MODE_VFIO_TYPE1_IOMMU``
- ``MODE_VFIO_TYPE1V2_IOMMU``
- ``MODE_IOMMUFD_COMPAT_TYPE1``
- ``MODE_IOMMUFD_COMPAT_TYPE1V2``
- ``MODE_IOMMUFD``

VFIO selftests can replicate their test cases across all possible IOMMU modes
using FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES() to ensure kernel IOMMU APIs remain
compatible.

``struct vfio_pci_device``
^^^^^^^^^^^^^^^^^^^^^^^^^^

A PCI device is represented as a ``struct vfio_pci_device``. The device
representation is initialized using ``vfio_pci_device_init()``, which takes a
target BDF string, opens the device in VFIO, and binds it to an existing
``struct iommu`` instance:

.. code-block:: c

    struct vfio_pci_device *device = vfio_pci_device_init(device_bdf, iommu);
    ...
    vfio_pci_device_cleanup(device);

``struct iova_allocator``
^^^^^^^^^^^^^^^^^^^^^^^^^

To facilitate DMA mappings without hardcoding IOVAs (IO virtual addresses) that
might conflict with reserved platform address ranges, the library provides a
``struct iova_allocator``.

The IOVA allocator queries the underlying IOMMU (via ``struct iommu``) for its
valid target IOVA ranges. Tests can safely carve out unique contiguous chunks
of IOVA space for device DMA by repeatedly calling:

.. code-block:: c

    struct iova_allocator *allocator = iova_allocator_init(iommu);
    iova_t addr = iova_allocator_alloc(allocator, size);

Driver Framework
----------------

The primary goal of VFIO selftests is to verify the correctness of the kernel
code handling VFIO devices, including VFIO core, IOMMUFD, IOMMU drivers, page
pinning, DMA mapping/unmapping, interrupt remapping, and IRQ delivery.

Because different physical devices have vastly different programming interfaces
for triggering work, the selftests library implements a standard driver
framework.  This framework abstracts diverse hardware endpoints (such as Intel
IGB, IOAT, or DSA) behind a uniform interface, allowing tests to provoke a
broad range of devices to generate DMA operations and interrupts in a
standardized manner.

By exercising DMA and interrupts with real endpoints, tests can thoroughly
stress and validate the underlying kernel paths, such as IOMMU page table
management, dirty page tracking, Device TLB and IOTLB invalidations, and
interrupt delivery under authentic hardware conditions.

All endpoint driver implementations populate a common set of operations defined
in ``struct vfio_pci_driver_ops``:

- ``probe()``: Validate whether the driver supports the current PCI device
  (e.g. matching vendor/device IDs).
- ``init()``: Set up specific control registers or ring-buffer data structures.
- ``remove()``: Clean up structures before destruction.
- ``memcpy_start()``: Initiate DMA copies via the device.
- ``memcpy_wait()``: Poll and wait until previously initiated copies complete
  safely.
- ``send_msi()``: Provoke the device to signal an MSI interrupt.

Tests are required to set up a dedicated DMA region for the driver to use
within ``struct vfio_pci_driver`` to serve as the driver's main memory
allocation region (e.g. used for in-memory descriptor rings and hardware
buffers). Tests must provision and map this DMA region before completing driver
initialization.

For a complete example of interacting with the driver framework from a test
perspective, refer to ``tools/testing/selftests/vfio/vfio_pci_driver_test.c``.

When contributing a new driver implementation to the selftests library, the
primary acceptance criteria is that ``vfio_pci_driver_test`` passes using the
new driver and target device.

Integration with KVM Selftests
------------------------------

libvfio is not strictly limited to VFIO selftests, it is also leveraged within
KVM selftests (``tools/testing/selftests/kvm/``). KVM selftests build their own
copy of libvfio and link against it, enabling libvfio to be used in KVM
selftests.

This integration enables testing the interaction between KVM and VFIO and
IOMMUFD. For example, testing the end-to-end delivery of device interrupts into
running vCPUs.

Contribution Guidelines
=======================

- Prefer the shortlog prefix ``vfio: selftests: ...`` for commits that modify
  files in ``tools/testing/selftests/vfio/``.
- Follow the Coding Style, Comments, Changelogs, and Function References
  guidelines from the
  :doc:`KVM x86 maintainers handbook </process/maintainer-kvm-x86>`.
