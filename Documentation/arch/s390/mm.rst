.. SPDX-License-Identifier: GPL-2.0

=================
Memory Management
=================

Virtual memory layout
=====================

.. note::

 - Some aspects of the virtual memory layout setup are not
   clarified (number of page levels, alignment, DMA memory).

 - Unused gaps in the virtual memory layout could be present
   or not - depending on how partucular system is configured.
   No page tables are created for the unused gaps.

 - The virtual memory regions are tracked or untracked by KASAN
   instrumentation, as well as the KASAN shadow memory itself is
   created only when CONFIG_KASAN configuration option is enabled.

::

  =============================================================================
  |    Physical      |	  Virtual	| VM area description
  =============================================================================
  +- 0 --------------+- 0 --------------+
  |		     | S390_lowcore	| Low-address memory
  |		     +- 8 KB -----------+
  |		     |			|
  |		     |			|
  |		     | ... unused gap	| KASAN untracked
  |		     |			|
  +- AMODE31_START --+- AMODE31_START --+ .amode31 rand. phys/virt start
  |.amode31 text/data|.amode31 text/data| KASAN untracked
  +- AMODE31_END ----+- AMODE31_END ----+ .amode31 rand. phys/virt end (<2GB)
  |		     |			|
  |		     |			|
  +- __kaslr_offset_phys		| kernel rand. phys start
  |		     |			|
  | kernel text/data |			|
  |		     |			|
  +------------------+			| kernel phys end
  |		     |			|
  |		     |			|
  |		     |			|
  |		     |			|
  +- ident_map_size -+			|
		     |			|
		     |	... unused gap	| KASAN untracked
		     |			|
		     +- __identity_base + identity mapping start (>= 2GB)
		     |			|
		     | identity		| phys == virt - __identity_base
		     | mapping		| virt == phys + __identity_base
		     |			|
		     |			| KASAN tracked
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     |			|
		     +---- vmemmap -----+ 'struct page' array start
		     |			|
		     | virtually mapped |
		     | memory map	| KASAN untracked
		     |			|
		     +- __abs_lowcore --+
		     |			|
		     | Absolute Lowcore | KASAN untracked
		     |			|
		     +- __memcpy_real_area
		     |			|
		     |	Real Memory Copy| KASAN untracked
		     |			|
		     +- VMALLOC_START --+ vmalloc area start
		     |			| KASAN untracked or
		     |	vmalloc area	| KASAN shallowly populated in case
		     |			|	CONFIG_KASAN_VMALLOC=y
		     +- MODULES_VADDR --+ modules area start
		     |			| KASAN allocated per module or
		     |	modules area	| KASAN shallowly populated in case
		     |			|	CONFIG_KASAN_VMALLOC=y
		     +- __kaslr_offset -+ kernel rand. virt start
		     |			| KASAN tracked
		     | kernel text/data | phys == (kvirt - __kaslr_offset) +
		     |			|	  __kaslr_offset_phys
		     +- kernel .bss end + kernel rand. virt end
		     |			|
		     |	... unused gap	| KASAN untracked
		     |			|
		     +------------------+ UltraVisor Secure Storage limit
		     |			|
		     |	... unused gap	| KASAN untracked
		     |			|
		     +KASAN_SHADOW_START+ KASAN shadow memory start
		     |			|
		     |	 KASAN shadow	| KASAN untracked
		     |			|
		     +------------------+ ASCE limit
