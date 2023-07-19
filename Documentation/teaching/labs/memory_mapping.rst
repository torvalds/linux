==============
Memory mapping
==============

Lab objectives
==============

* Understand address space mapping mechanisms
* Learn about the most important structures related to memory management

Keywords:

* address space
* :c:func:`mmap`
* :c:type:`struct page`
* :c:type:`struct vm_area_struct`
* :c:type:`struct vm_struct`
* :c:type:`remap_pfn_range`
* :c:func:`SetPageReserved`
* :c:func:`ClearPageReserved`


Overview
========

In the Linux kernel it is possible to map a kernel address space to a
user address space. This eliminates the overhead of copying user space
information into the kernel space and vice versa. This can be done
through a device driver and the user space device interface
(:file:`/dev`).

This feature can be used by implementing the :c:func:`mmap` operation
in the device driver's :c:type:`struct file_operations` and using the
:c:func:`mmap` system call in user space.

The basic unit for virtual memory management is a page, which size is
usually 4K, but it can be up to 64K on some platforms. Whenever we
work with virtual memory we work with two types of addresses: virtual
address and physical address. All CPU access (including from kernel
space) uses virtual addresses that are translated by the MMU into
physical addresses with the help of page tables.

A physical page of memory is identified by the Page Frame Number
(PFN). The PFN can be easily computed from the physical address by
dividing it with the size of the page (or by shifting the physical
address with PAGE_SHIFT bits to the right).

.. image:: ../res/paging.png
   :width: 49 %

For efficiency reasons, the virtual address space is divided into
user space and kernel space. For the same reason, the kernel space
contains a memory mapped zone, called **lowmem**, which is contiguously
mapped in physical memory, starting from the lowest possible physical
address (usually 0). The virtual address where lowmem is mapped is
defined by :c:macro:`PAGE_OFFSET`.

On a 32bit system, not all available memory can be mapped in lowmem and
because of that there is a separate zone in kernel space called
**highmem** which can be used to arbitrarily map physical memory.

Memory allocated by :c:func:`kmalloc` resides in lowmem and it is
physically contiguous. Memory allocated by :c:func:`vmalloc` is not
contiguous and does not reside in lowmem (it has a dedicated zone in
highmem).

.. image:: ../res/kernel-virtmem-map.png
   :width: 49 %

Structures used for memory mapping
==================================

Before discussing about the memory mapping mechanism over a device,
we will present some of the basic structures used by the Linux memory
management subsystem.
Some of the basic structures are: :c:type:`struct page`,
:c:type:`struct vm_area_struct`, :c:type:`struct mm_struct`.

:c:type:`struct page`
---------------------

:c:type:`struct page` is used to embed information about all physical
pages in the system. The kernel has a :c:type:`struct page` structure
for all pages in the system.

There are many functions that interact with this structure:

* :c:func:`virt_to_page` returns the page associated with a virtual
  address
* :c:func:`pfn_to_page` returns the page associated with a page frame
  number
* :c:func:`page_to_pfn` return the page frame number associated with a
  :c:type:`struct page`
* :c:func:`page_address` returns the virtual address of a
  :c:type:`struct page`; this functions can be called only for pages from
  lowmem
* :c:func:`kmap` creates a mapping in kernel for an arbitrary physical
  page (can be from highmem) and returns a virtual address that can be
  used to directly reference the page

:c:type:`struct vm_area_struct`
-------------------------------

:c:type:`struct vm_area_struct` holds information about a contiguous
virtual memory area. The memory areas of a process can be viewed by
inspecting the *maps* attribute of the process via procfs:

.. code-block:: shell

   root@qemux86:~# cat /proc/1/maps
   #address          perms offset  device inode     pathname
   08048000-08050000 r-xp 00000000 fe:00 761        /sbin/init.sysvinit
   08050000-08051000 r--p 00007000 fe:00 761        /sbin/init.sysvinit
   08051000-08052000 rw-p 00008000 fe:00 761        /sbin/init.sysvinit
   092e1000-09302000 rw-p 00000000 00:00 0          [heap]
   4480c000-4482e000 r-xp 00000000 fe:00 576        /lib/ld-2.25.so
   4482e000-4482f000 r--p 00021000 fe:00 576        /lib/ld-2.25.so
   4482f000-44830000 rw-p 00022000 fe:00 576        /lib/ld-2.25.so
   44832000-449a9000 r-xp 00000000 fe:00 581        /lib/libc-2.25.so
   449a9000-449ab000 r--p 00176000 fe:00 581        /lib/libc-2.25.so
   449ab000-449ac000 rw-p 00178000 fe:00 581        /lib/libc-2.25.so
   449ac000-449af000 rw-p 00000000 00:00 0 
   b7761000-b7763000 rw-p 00000000 00:00 0 
   b7763000-b7766000 r--p 00000000 00:00 0          [vvar]
   b7766000-b7767000 r-xp 00000000 00:00 0          [vdso]
   bfa15000-bfa36000 rw-p 00000000 00:00 0          [stack]

A memory area is characterized by a start address, a stop address,
length, permissions.

A :c:type:`struct vm_area_struct` is created at each :c:func:`mmap`
call issued from user space. A driver that supports the :c:func:`mmap`
operation must complete and initialize the associated
:c:type:`struct vm_area_struct`. The most important fields of this
structure are:

* :c:member:`vm_start`, :c:member:`vm_end` - the beginning and the end of
  the memory area, respectively (these fields also appear in
  :file:`/proc/<pid>/maps`);
* :c:member:`vm_file` - the pointer to the associated file structure (if any);
* :c:member:`vm_pgoff` - the offset of the area within the file;
* :c:member:`vm_flags` - a set of flags;
* :c:member:`vm_ops` - a set of working functions for this area
* :c:member:`vm_next`, :c:member:`vm_prev` - the areas of the same process
  are chained by a list structure

:c:type:`struct mm_struct`
--------------------------

:c:type:`struct mm_struct` encompasses all memory areas associated
with a process. The :c:member:`mm` field of :c:type:`struct task_struct`
is a pointer to the :c:type:`struct mm_struct` of the current process.


Device driver memory mapping
============================

Memory mapping is one of the most interesting features of a Unix
system. From a driver's point of view, the memory-mapping facility
allows direct memory access to a user space device.

To assign a :c:func:`mmap` operation to a driver, the :c:member:`mmap`
field of the device driver's :c:type:`struct file_operations` must be
implemented. If that is the case, the user space process can then use
the :c:func:`mmap` system call on a file descriptor associated with
the device.

The mmap system call takes the following parameters:

.. code-block:: c

   void *mmap(caddr_t addr, size_t len, int prot,
              int flags, int fd, off_t offset);

To map memory between a device and user space, the user process must
open the device and issue the :c:func:`mmap` system call with the resulting
file descriptor.

The device driver :c:func:`mmap` operation has the following signature:

.. code-block:: c

   int (*mmap)(struct file *filp, struct vm_area_struct *vma);

The *filp* field is a pointer to a :c:type:`struct file` created when
the device is opened from user space. The *vma* field is used to
indicate the virtual address space where the memory should be mapped
by the device. A driver should allocate memory (using
:c:func:`kmalloc`, :c:func:`vmalloc`, :c:func:`alloc_pages`) and then
map it to the user address space as indicated by the *vma* parameter
using helper functions such as :c:func:`remap_pfn_range`.

:c:func:`remap_pfn_range` will map a contiguous physical address space
into the virtual space represented by :c:type:`vm_area_struct`:

.. code-block:: c

   int remap_pfn_range (structure vm_area_struct *vma, unsigned long addr,
                        unsigned long pfn, unsigned long size, pgprot_t prot);

:c:func:`remap_pfn_range` expects the following parameters:

* *vma*  - the virtual memory space in which mapping is made;
* *addr* - the virtual address space from where remapping begins; page
  tables for the virtual address space between addr and addr + size
  will be formed as needed
* *pfn* - the page frame number to which the virtual address should be
  mapped
* *size* - the size (in bytes) of the memory to be mapped
* *prot* - protection flags for this mapping

Here is an example of using this function that contiguously maps the
physical memory starting at page frame number *pfn* (memory that was
previously allocated) to the *vma->vm_start* virtual address:

.. code-block:: c

   struct vm_area_struct *vma;
   unsigned long len = vma->vm_end - vma->vm_start;
   int ret ;

   ret = remap_pfn_range(vma, vma->vm_start, pfn, len, vma->vm_page_prot);
   if (ret < 0) {
       pr_err("could not map the address area\n");
       return -EIO;
   }

To obtain the page frame number of the physical memory we must
consider how the memory allocation was performed. For each
:c:func:`kmalloc`, :c:func:`vmalloc`, :c:func:`alloc_pages`, we must
used a different approach. For :c:func:`kmalloc` we can use something
like:

.. code-block:: c

   static char *kmalloc_area;

   unsigned long pfn = virt_to_phys((void *)kmalloc_area)>>PAGE_SHIFT;

while for :c:func:`vmalloc`:

.. code-block:: c

   static char *vmalloc_area;

   unsigned long pfn = vmalloc_to_pfn(vmalloc_area);

and finally for :c:func:`alloc_pages`:

.. code-block:: c

   struct page *page;

   unsigned long pfn = page_to_pfn(page);

.. attention:: Note that memory allocated with :c:func:`vmalloc` is not
               physically contiguous so if we want to map a range allocated
               with :c:func:`vmalloc`, we have to map each page individually
               and compute the physical address for each page.

Since the pages are mapped to user space, they might be swapped
out. To avoid this we must set the PG_reserved bit on the page.
Enabling is done using :c:func:`SetPageReserved` while reseting it
(which must be done before freeing the memory) is done with
:c:func:`ClearPageReserved`:

.. code-block:: c

   void alloc_mmap_pages(int npages)
   {
       int i;
       char *mem = kmalloc(PAGE_SIZE * npages);

       if (!mem)
	   return mem;

       for(i = 0; i < npages * PAGE_SIZE; i += PAGE_SIZE)
	   SetPageReserved(virt_to_page(((unsigned long)mem) + i));

       return mem;
   }

   void free_mmap_pages(void *mem, int npages)
   {
       int i;

       for(i = 0; i < npages * PAGE_SIZE; i += PAGE_SIZE)
	   ClearPageReserved(virt_to_page(((unsigned long)mem) + i));

       kfree(mem);
   }


Further reading
===============

* `Linux Device Drivers 3rd Edition - Chapter 15. Memory Mapping and DMA <http://lwn.net/images/pdf/LDD3/ch15.pdf>`_
* `Linux Device Driver mmap Skeleton <http://www.xml.com/ldd/chapter/book/ch13.html>`_
* `Driver porting: supporting mmap () <http://lwn.net/Articles/28746/>`_
* `Device Drivers Concluded <http://www.linuxjournal.com/article/1287>`_
* `mmap <http://en.wikipedia.org/wiki/Mmap>`_

Exercises
=========

.. include:: ../labs/exercises-summary.hrst
.. |LAB_NAME| replace:: memory_mapping

1. Mapping contiguous physical memory to userspace
--------------------------------------------------

Implement a device driver that maps contiguous physical memory
(e.g. obtained via :c:func:`kmalloc`) to userspace.

Review the `Device driver memory mapping`_ section, generate the
skeleton for the task named **kmmap** and fill in the areas marked
with **TODO 1**.

Start with allocating a NPAGES+2 memory area page using :c:func:`kmalloc`
in the module init function and find the first address in the area that is
aligned to a page boundary.

.. hint:: The size of a page is *PAGE_SIZE*.

	  Store the allocated area in *kmalloc_ptr* and the page
	  aligned address in *kmalloc_area*:

	  Use :c:func:`PAGE_ALIGN` to determine *kmalloc_area*.

Enable the PG_reserved bit of each page with
:c:func:`SetPageReserved`. Clear the bit with
:c:func:`ClearPageReserved` before freeing the memory.

.. hint:: Use :c:func:`virt_to_page` to translate virtual pages into
	  physical pages, as required by :c:func:`SetPageReserved`
	  and :c:func:`ClearPageReserved`.

For verification purpose (using the test below), fill in the first 4
bytes of each page with the following values: 0xaa, 0xbb, 0xcc, 0xdd.

Implement the :c:func:`mmap` driver function.

.. hint:: For mapping, use :c:func:`remap_pfn_range`. The third
	  argument for :c:func:`remap_pfn_range` is a page frame number (PFN).

	  To convert from virtual kernel address to physical address,
	  use :c:func:`virt_to_phys`.

	  To convert a physical address to its PFN, shift the address
	  with PAGE_SHIFT bits to the right.

For testing, load the kernel module and run:

.. code-block:: shell

  root@qemux86:~# skels/memory_mapping/test/mmap-test 1

If everything goes well, the test will show "matched" messages.

2. Mapping non-contiguous physical memory to userspace
------------------------------------------------------

Implement a device driver that maps non-contiguous physical memory
(e.g. obtained via :c:func:`vmalloc`) to userspace.

Review the `Device driver memory mapping`_ section, generate the
skeleton for the task named **vmmap** and fill in the areas marked
with **TODO 1**.

Allocate a memory area of NPAGES with :c:func:`vmalloc`.

.. hint:: The size of a page is *PAGE_SIZE*.
          Store the allocated area in *vmalloc_area*.
          Memory allocated by :c:func:`vmalloc` is paged aligned.

Enable the PG_reserved bit of each page with
:c:func:`SetPageReserved`. Clear the bit with
:c:func:`ClearPageReserved` before freeing the memory.

.. hint:: Use :c:func:`vmalloc_to_page` to translate virtual pages
          into physical pages used by the functions
          :c:func:`SetPageReserved` and :c:func:`ClearPageReserved`.

For verification purpose (using the test below), fill in the first 4
bytes of each page with the following values: 0xaa, 0xbb, 0xcc, 0xdd.

Implement the mmap driver function.

.. hint:: To convert from virtual vmalloc address to physical address,
          use :c:func:`vmalloc_to_pfn` which returns a PFN directly.

.. attention:: vmalloc pages are not physically contiguous so it is
               needed to use :c:func:`remap_pfn_range` for each page.

               Loop through all virtual pages and for each:
               * determine the physical address
               * map it with :c:func:`remap_pfn_range`

               Make sure that you determine the physical address
               each time and that you use a range of one page for mapping.

For testing, load the kernel module and run:

.. code-block:: shell

  root@qemux86:~# skels/memory_mapping/test/mmap-test 1

If everything goes well, the test will show "matched" messages.

3. Read / write operations in mapped memory
-------------------------------------------

Modify one of the previous modules to allow read / write operations on
your device. This is a didactic exercise to see that the same space
can also be used with the :c:func:`mmap` call and with :c:func:`read`
and :c:func:`write` calls.

Fill in areas marked with **TODO 2**.

.. note:: The offset parameter sent to the read / write operation can
          be ignored as all reads / writes from the test program will
          be done with 0 offsets.

For testing, load the kernel module and run:

.. code-block:: shell

  root@qemux86:~# skels/memory_mapping/test/mmap-test 2


4. Display memory mapped in procfs
----------------------------------

Using one of the previous modules, create a procfs file in which you
display the total memory mapped by the calling process.

Fill in the areas marked with **TODO 3**.

Create a new entry in procfs (:c:macro:`PROC_ENTRY_NAME`, defined in
:file:`mmap-test.h`) that will show the total memory mapped by the process
that called the :c:func:`read` on that file.

.. hint:: Use :c:func:`proc_create`. For the mode parameter, use 0,
          and for the parent parameter use NULL. Use
          :c:func:`my_proc_file_ops` for operations.

In the module exit function, delete the :c:macro:`PROC_ENTRY_NAME` entry
using :c:func:`remove_proc_entry`.

.. note:: A (complex) use and description of the :c:type:`struct
          seq_file` interface can be found here in this `example
          <http://tldp.org/LDP/lkmpg/2.6/html/x861.html>`_ .

          For this exercise, just a simple use of the interface
          described `here <http://lwn.net/Articles/22355/>`_ is
          sufficient. Check the "extra-simple" API described there.

In the :c:func:`my_seq_show` function you will need to:

* Obtain the :c:type:`struct mm_struct` structure of the current process
  using the :c:func:`get_task_mm` function.

  .. hint:: The current process is available via the *current* variable
            of type :c:type:`struct task_struct*`.

* Iterate through the entire :c:type:`struct vm_area_struct` list
  associated with the process.

  .. hint:: Use the variable :c:data:`vma_iterator` and start from
            :c:data:`mm->mmap`. Use the :c:member:`vm_next` field of
            the :c:type:`struct vm_area_struct` to navigate through
            the list of memory areas. Stop when you reach :c:macro:`NULL`.

* Use *vm_start* and *vm_end* for each area to compute the total size.

* Use :c:func:`pr_info("%lx %lx\n, ...)` to print *vm_start* and *vm_end* for
  each area.

* To release :c:type:`struct mm_struct`, decrement the reference
  counter of the structure using :c:func:`mmput`.

* Use :c:func:`seq_printf` to write to the file. Show only the total count,
  no other messages. Do not even show newline (\n).

In :c:func:`my_seq_open` register the display function
(:c:func:`my_seq_show`) using :c:func:`single_open`.

.. note:: :c:func:`single_open` can use :c:macro:`NULL` as its third argument.

For testing, load the kernel module and run:

.. code-block:: shell

  root@qemux86:~# skels/memory_mapping/test/mmap-test 3

.. note:: The test waits for a while (it has an internal sleep
          instruction). As long as the test waits, use the
          :command:`pmap` command in another console to see the
          mappings of the test and compare those to the test results.
