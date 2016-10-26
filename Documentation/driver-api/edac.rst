Error Detection And Correction (EDAC) Devices
=============================================

Memory Controllers
------------------

Most of the EDAC core is focused on doing Memory Controller error detection.
The :c:func:`edac_mc_alloc`. It uses internally the struct ``mem_ctl_info``
to describe the memory controllers, with is an opaque struct for the EDAC
drivers. Only the EDAC core is allowed to touch it.

.. kernel-doc:: include/linux/edac.h

.. kernel-doc:: drivers/edac/edac_mc.h

PCI Controllers
---------------

The EDAC subsystem provides a mechanism to handle PCI controllers by calling
the :c:func:`edac_pci_alloc_ctl_info`. It will use the struct
:c:type:`edac_pci_ctl_info` to describe the PCI controllers.

.. kernel-doc:: drivers/edac/edac_pci.h

EDAC Blocks
-----------

The EDAC subsystem also provides a generic mechanism to report errors on
other parts of the hardware via :c:func:`edac_device_alloc_ctl_info` function.

The structures :c:type:`edac_dev_sysfs_block_attribute`,
:c:type:`edac_device_block`, :c:type:`edac_device_instance` and
:c:type:`edac_device_ctl_info` provide a generic or abstract 'edac_device'
representation at sysfs.

This set of structures and the code that implements the APIs for the same, provide for registering EDAC type devices which are NOT standard memory or
PCI, like:

- CPU caches (L1 and L2)
- DMA engines
- Core CPU switches
- Fabric switch units
- PCIe interface controllers
- other EDAC/ECC type devices that can be monitored for
  errors, etc.

It allows for a 2 level set of hierarchy.

For example, a cache could be composed of L1, L2 and L3 levels of cache.
Each CPU core would have its own L1 cache, while sharing L2 and maybe L3
caches. On such case, those can be represented via the following sysfs
nodes::

	/sys/devices/system/edac/..

	pci/		<existing pci directory (if available)>
	mc/		<existing memory device directory>
	cpu/cpu0/..	<L1 and L2 block directory>
		/L1-cache/ce_count
			 /ue_count
		/L2-cache/ce_count
			 /ue_count
	cpu/cpu1/..	<L1 and L2 block directory>
		/L1-cache/ce_count
			 /ue_count
		/L2-cache/ce_count
			 /ue_count
	...

	the L1 and L2 directories would be "edac_device_block's"

.. kernel-doc:: drivers/edac/edac_device.h
