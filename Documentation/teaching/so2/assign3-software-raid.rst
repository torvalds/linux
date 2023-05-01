===========================
Assignment 3 - Software RAID
===========================

- Deadline: :command:`Monday, 15 May 2023, 23:00`
- This assignment can be made in teams (max 2). Only one of them must submit the assignment, and the names of the student should be listed in a README file.

Implementing a software RAID module that uses a logical block device that will read and write data from two physical devices,
ensuring the consistency and synchronization of data from the two physical devices. The type of RAID implemented will be similar to a `RAID 1`.

Assignment's Objectives
=======================

* in-depth understanding of how the I/O subsystem works.
* acquire advanced skills working with `bio` structures.
* work  with the block / disk devices in the Linux kernel.
* acquire skills to navigate and understand the code and API dedicated to the I/O subsystem in Linux.


Statement
=========

Write a kernel module that implements the RAID software functionality. `Software RAID <https://en.wikipedia.org/wiki/RAID#Software-based_RAID>`__ provides an abstraction between
the logical device and the physical devices. The implementation will use `RAID scheme 1 <https://en.wikipedia.org/wiki/RAID#Standard_levels>`__.

The virtual machine has two hard disks that will represent the physical devices: `/dev/vdb` and `/dev/vdc`. The operating system
will provide a logical device (block type) that will interface the access from the user space. Writing requests to the logical device
will result in two writes, one for each hard disk. Hard disks are not partitioned. It will be considered that each hard disk has a
single partition that covers the entire disk.

Each partition will store a sector along with an associated checksum (CRC32) to ensure error recovery. At each reading, the related
information from both partitions is read. If a sector of the first partition has corrupt data (CRC value is wrong) then the sector
on the second partition will be read; at the same time the sector of the first partition will be corrected. Similar in the case of
a reading of a corrupt sector on the second partition. If a sector has incorrect CRC values on both partitions, an appropriate error
code will be returned.

Important to know
-----------------

To ensure error recovery, a CRC code is associated with each sector. CRC codes are stored by LOGICAL_DISK_SIZE byte of the partition
(macro defined in the assignment `header <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/src/ssr.h>`__). The disk structure will have the following layout:


.. code-block:: console

   +-----------+-----------+-----------+     +---+---+---+
   |  sector1  |  sector2  |  sector3  |.....|C1 |C2 |C3 |
   +-----------+-----------+-----------+     +---+---+---+

where ``C1``, ``C2``, ``C3`` are the values CRC sectors ``sector1``, ``sector2``, ``sector3``. The CRC area is found immediately after the ``LOGICAL_DISK_SIZE`` bytes of the partition.

As a seed for CRC use 0(zero).

Implementation Details
======================

- the kernel module will be named ``ssr.ko``
- the logical device will be accessed as a block device with the major ``SSR_MAJOR`` and minor ``SSR_FIRST_MINOR`` under the name ``/dev/ssr`` (via the macro ``LOGICAL_DISK_NAME``)
- the virtual device (``LOGICAL_DISK_NAME`` - ``/dev/ssr``) will have the capacity of ``LOGICAL_DISK_SECTORS`` (use ``set_capacity`` with the ``struct gendisk`` structure)
- the two disks are represented by the devices ``/dev/vdb``, respectively ``/dev/vdc``, defined by means of macros ``PHYSICAL_DISK1_NAME``, respectively ``PHYSICAL_DISK2_NAME``
- to work with the ``struct block _device`` structure associated with a physical device, you can use the ``blkdev_get_by_path`` and ``blkdev_put`` functions
- for the handling of requests from the user space, we recommend not to use a ``request_queue``, but to do processing at :c:type:`struct bio` level
  using the ``submit_bio`` field of :c:type:`struct block_device_operations`
- since data sectors are separated from CRC sectors you will have to build separate ``bio`` structures for data and CRC values
- to allocate a :c:type:`struct bio` for physical disks you can use :c:func:`bio_alloc`; to add data pages to bio use :c:func:`alloc_page` and :c:func:`bio_add_page`
- to free up the space allocated for a :c:type:`struct bio` you need to release the pages allocated to the bio (using the :c:func:`__free_page` macro ) and call
  :c:func:`bio_put`
- when generating a :c:type:`struct bio` structure, consider that its size must be multiple of the disk sector size (``KERNEL_SECTOR_SIZE``)
- to send a request to a block device and wait for it to end, you can use the :c:func:`submit_bio_wait` function
- use :c:func:`bio_endio` to signal the completion of processing a ``bio`` structure
- for the CRC32 calculation you can use the :c:func:`crc32` macro provided by the kernel
- useful macro definitions can be found in the assignment support `header <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/src/ssr.h>`__
- a single request processing function for block devices can be active at one time in a call stack (more details `here <https://elixir.bootlin.com/linux/v5.10/source/block/blk-core.c#L1048>`__).
  You will need to submit requests for physical devices in a kernel thread; we recommend using ``workqueues``.
- For a quick run, use a single bio to batch send the read/write request for CRC values for adjacent sectors. For example,
  if you need to send requests for CRCs in sectors 0, 1, ..., 7, use a single bio, not 8 bios.
- our recommendations are not mandatory (any solution that meets the requirements of the assignment is accepted)
Testing
=======
In order to simplify the assignment evaluation process, but also to reduce the mistakes of the submitted assignments,
the assignment evaluation will be done automatically with the help of a
`test script <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/checker/3-raid-checker/_checker>`__ called `_checker`.
The test script assumes that the kernel module is called `ssr.ko`.

If, as a result of the testing process, the sectors on both disks contain invalid data, resulting in
read errors that make the module impossible to use, you will need to redo the two disks in the
virtual machine using the commands:

.. code-block:: console

   $ dd if=/dev/zero of=/dev/vdb bs=1M
   $ dd if=/dev/zero of=/dev/vdc bs=1M

You can also get the same result using the following command to start the virtual machine:

.. code-block:: console

   $ rm disk{1,2}.img; make console # or rm disk{1,2}.img; make boot

QuickStart
==========

It is mandatory to start the implementation of the assignment from the code skeleton found in the `src <https://gitlab.cs.pub.ro/so2/3-raid/-/tree/master/src>`__ directory.
There is only one header in the skeleton called `ssr.h <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/src/ssr.h>`__.
You will provide the rest of the implementation. You can add as many `*.c`` sources and additional `*.h`` headers.
You should also provide a Kbuild file that will compile the kernel module called `ssr.ko`.
Follow the instructions in the `README.md file <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/README.md>`__ of the `assignment's repo <https://gitlab.cs.pub.ro/so2/3-raid>`__.


Tips
----

To increase your chances of getting the highest grade, read and follow the Linux kernel
coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

Also, use the following static analysis tools to verify the code:

- checkpatch.pl

.. code-block:: console

   $ linux/scripts/checkpatch.pl --no-tree --terse -f /path/to/your/file.c

- sparse

.. code-block:: console

   $ sudo apt-get install sparse
   $ cd linux
   $ make C=2 /path/to/your/file.c

- cppcheck

.. code-block:: console

   $ sudo apt-get install cppcheck
   $ cppcheck /path/to/your/file.c

Penalties
---------

Information about assigments penalties can be found on the
`General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__.

In exceptional cases (the assigment passes the tests by not complying with the requirements)
and if the assigment does not pass all the tests, the grade will may decrease more than mentioned above.

Submitting the assigment
------------------------

The assignment will be graded automatically using the `vmchecker-next <https://github.com/systems-cs-pub-ro/vmchecker-next/wiki/Student-Handbook>`__ infrastructure.
The submission will be made on moodle on the `course's page <https://curs.upb.ro/2022/course/view.php?id=5121>`__ to the related assignment.
You will find the submission details in the `README.md file <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/README.md>`__ of the `repo <https://gitlab.cs.pub.ro/so2/3-raid>`__.


Resources
=========

- implementation of the `RAID <https://elixir.bootlin.com/linux/v5.10/source/drivers/md>`__ software in the Linux kernel

We recommend that you use gitlab to store your homework. Follow the directions in
`README <https://gitlab.cs.pub.ro/so2/3-raid/-/blob/master/README.md>`__.


Questions
=========

For questions about the topic, you can consult the mailing `list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
or you can write a question on the dedicated Teams channel.

Before you ask a question, make sure that:

   - you have read the statement of the assigment well
   - the question is not already presented on the `FAQ page <https://ocw.cs.pub.ro/courses/so2/teme/tema2/faq>`__
   - the answer cannot be found in the `mailing list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
