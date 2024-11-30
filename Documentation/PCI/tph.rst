.. SPDX-License-Identifier: GPL-2.0


===========
TPH Support
===========

:Copyright: 2024 Advanced Micro Devices, Inc.
:Authors: - Eric van Tassell <eric.vantassell@amd.com>
          - Wei Huang <wei.huang2@amd.com>


Overview
========

TPH (TLP Processing Hints) is a PCIe feature that allows endpoint devices
to provide optimization hints for requests that target memory space.
These hints, in a format called Steering Tags (STs), are embedded in the
requester's TLP headers, enabling the system hardware, such as the Root
Complex, to better manage platform resources for these requests.

For example, on platforms with TPH-based direct data cache injection
support, an endpoint device can include appropriate STs in its DMA
traffic to specify which cache the data should be written to. This allows
the CPU core to have a higher probability of getting data from cache,
potentially improving performance and reducing latency in data
processing.


How to Use TPH
==============

TPH is presented as an optional extended capability in PCIe. The Linux
kernel handles TPH discovery during boot, but it is up to the device
driver to request TPH enablement if it is to be utilized. Once enabled,
the driver uses the provided API to obtain the Steering Tag for the
target memory and to program the ST into the device's ST table.

Enable TPH support in Linux
---------------------------

To support TPH, the kernel must be built with the CONFIG_PCIE_TPH option
enabled.

Manage TPH
----------

To enable TPH for a device, use the following function::

  int pcie_enable_tph(struct pci_dev *pdev, int mode);

This function enables TPH support for device with a specific ST mode.
Current supported modes include:

  * PCI_TPH_ST_NS_MODE - NO ST Mode
  * PCI_TPH_ST_IV_MODE - Interrupt Vector Mode
  * PCI_TPH_ST_DS_MODE - Device Specific Mode

`pcie_enable_tph()` checks whether the requested mode is actually
supported by the device before enabling. The device driver can figure out
which TPH mode is supported and can be properly enabled based on the
return value of `pcie_enable_tph()`.

To disable TPH, use the following function::

  void pcie_disable_tph(struct pci_dev *pdev);

Manage ST
---------

Steering Tags are platform specific. PCIe spec does not specify where STs
are from. Instead PCI Firmware Specification defines an ACPI _DSM method
(see the `Revised _DSM for Cache Locality TPH Features ECN
<https://members.pcisig.com/wg/PCI-SIG/document/15470>`_) for retrieving
STs for a target memory of various properties. This method is what is
supported in this implementation.

To retrieve a Steering Tag for a target memory associated with a specific
CPU, use the following function::

  int pcie_tph_get_cpu_st(struct pci_dev *pdev, enum tph_mem_type type,
                          unsigned int cpu_uid, u16 *tag);

The `type` argument is used to specify the memory type, either volatile
or persistent, of the target memory. The `cpu_uid` argument specifies the
CPU where the memory is associated to.

After the ST value is retrieved, the device driver can use the following
function to write the ST into the device::

  int pcie_tph_set_st_entry(struct pci_dev *pdev, unsigned int index,
                            u16 tag);

The `index` argument is the ST table entry index the ST tag will be
written into. `pcie_tph_set_st_entry()` will figure out the proper
location of ST table, either in the MSI-X table or in the TPH Extended
Capability space, and write the Steering Tag into the ST entry pointed by
the `index` argument.

It is completely up to the driver to decide how to use these TPH
functions. For example a network device driver can use the TPH APIs above
to update the Steering Tag when interrupt affinity of a RX/TX queue has
been changed. Here is a sample code for IRQ affinity notifier:

.. code-block:: c

    static void irq_affinity_notified(struct irq_affinity_notify *notify,
                                      const cpumask_t *mask)
    {
         struct drv_irq *irq;
         unsigned int cpu_id;
         u16 tag;

         irq = container_of(notify, struct drv_irq, affinity_notify);
         cpumask_copy(irq->cpu_mask, mask);

         /* Pick a right CPU as the target - here is just an example */
         cpu_id = cpumask_first(irq->cpu_mask);

         if (pcie_tph_get_cpu_st(irq->pdev, TPH_MEM_TYPE_VM, cpu_id,
                                 &tag))
             return;

         if (pcie_tph_set_st_entry(irq->pdev, irq->msix_nr, tag))
             return;
    }

Disable TPH system-wide
-----------------------

There is a kernel command line option available to control TPH feature:
    * "notph": TPH will be disabled for all endpoint devices.
