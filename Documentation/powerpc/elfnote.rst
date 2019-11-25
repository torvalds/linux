==========================
ELF Note PowerPC Namespace
==========================

The PowerPC namespace in an ELF Note of the kernel binary is used to store
capabilities and information which can be used by a bootloader or userland.

Types and Descriptors
---------------------

The types to be used with the "PowerPC" namesapce are defined in [#f1]_.

	1) PPC_ELFNOTE_CAPABILITIES

Define the capabilities supported/required by the kernel. This type uses a
bitmap as "descriptor" field. Each bit is described below:

- Ultravisor-capable bit (PowerNV only).

.. code-block:: c

	#define PPCCAP_ULTRAVISOR_BIT (1 << 0)

Indicate that the powerpc kernel binary knows how to run in an
ultravisor-enabled system.

In an ultravisor-enabled system, some machine resources are now controlled
by the ultravisor. If the kernel is not ultravisor-capable, but it ends up
being run on a machine with ultravisor, the kernel will probably crash
trying to access ultravisor resources. For instance, it may crash in early
boot trying to set the partition table entry 0.

In an ultravisor-enabled system, a bootloader could warn the user or prevent
the kernel from being run if the PowerPC ultravisor capability doesn't exist
or the Ultravisor-capable bit is not set.

References
----------

.. [#f1] arch/powerpc/include/asm/elfnote.h

