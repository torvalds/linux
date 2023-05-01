===================================
Assignment 5 - PITIX Filesystem
===================================

Deadline: :command:`Tuesday, 24 May 2022, 23:00`

Statement
=========

Write a kernel module to implement the **PITIX** file system, version 2.
This file system will only support files and directories.
Support operations for hard or symbolic links will not be implemented.
Also, support operations for special files (pipes, character devices, or blocks) will not be implemented.
Basically you need to implement the following:
  * for directories: ``lookup``, ``unlink``, ``mkdir``, ``rmdir``, ``iterate``
  * for files: ``create``, ``truncate``, bitmap functions, see `minix_get_block <https://elixir.bootlin.com/linux/v4.15/source/fs/minix/inode.c#L375>`__.

The rest of the functions either have generic kernel implementations, or you don't have to implement them.

The disk structure of the file system is:

.. code-block:: console

    +--------------+-----------+-----------+------------+-----------------------+
    |              |           |           |            |                       |
    |  superblock  |   imap    |   dmap    |    izone   |         dzone         |
    +--------------+-----------+-----------+------------+-----------------------+
       4096 bytes     1 block     1 block     32 blocks    8*block_size blocks


where:

* ``Superblock`` is the superblock (``4096`` bytes)
* ``Imap`` contains the bitmap of the blocks occupied by the inodes (``1`` block)
* ``Dmap`` contains the bitmap of the blocks occupied by the data (``1`` block)
* ``Izone`` contains inodes (``32`` blocks)
* ``Dzone`` contains the data (the actual contents of the files) (``8 * block_size`` blocks)

The superblock (**on disk**) is described by the following structure:

.. code-block:: c

    struct pitix_super_block {
            unsigned long magic;
            __u8 version;
            __u8 block_size_bits;
            __u8 imap_block;
            __u8 dmap_block;
            __u8 izone_block;
            __u8 dzone_block;
            __u16 bfree;
            __u16 ffree;
    };

where:

* ``magic`` must be initialized with ``PITIX_MAGIC``
* ``version`` must be initialized with ``2`` (``PITIX_VERSION``)
* ``block_size_bits`` is the block size of two; the block size can be ``512``, ``1024``, ``2048``, or ``4096``
* ``Imap_block`` is the block number (relative to the device) to the bit vector used for the allocation / release sites inode
* ``dmap_block`` is the block number (relative to the device) for the bit vector used to allocate / release data blocks
* ``izone_block`` is the number of the first block (relative to the device) of the inode area
* ``dzone_block`` is the number of the first block (relative to the device) of the data area
* ``bfree`` is the number of free blocks (unallocated)
* ``ffree`` is the number of free (unallocated) inodes

The inodes will be stored in the inode area and are described by the following structure:

.. code-block:: c

    struct pitix_inode {
            __u32 mode;
            uid_t uid;
            gid_t gid;
            __u32 size;
            __u32 time;
            __u16 direct_data_blocks [INODE_DIRECT_DATA_BLOCKS];
            __u16 indirect_data_block;
    };

where:

* ``mode`` represents the access rights and inode type (file or directory) as represented in the kernel
* ``uid`` represents the UID as it is represented in the kernel
* ``gid`` represents the GID as it is represented in the kernel
* ``size`` is the size of the file / directory
* ``time`` represents the modification time as it is represented in the kernel
* ``direct_data_blocks`` is a vector (size ``INODE_DIRECT_DATA_BLOCKS`` ) that contains indexes of direct data blocks
* ``indirect_data_block`` is the index of a data block that contains the indexes of indirect data blocks

The index of a data block (direct or indirect) indicates the number of that data block relative to the data area (``Dzone``).
The size of an index is ``2`` bytes.

As can be seen from its structure, the inode uses a simple routing scheme for data blocks.
Blocks in the range ``[0, INODE_DIRECT_DATA_BLOCKS)`` are blocks of direct data and are referenced by elements of the vector ``direct_data_blocks`` and blocks in the range ``[INODE_DIRECT_DATA_BLOCKS, INODE_DIRECT_DATA_BL)`` are indirect data blocks and are referred to by indices within the data block indicated by ``indirect_data_block``.

The data block indicated by ``indirect_data_block`` must be allocated when we have to refer to a first block of indirect data and must be released when there are no more blocks of indirect data.

Unused indexes must be set to ``0``.
The first block, the one with index ``0``, is always allocated when formatting. This block cannot be used and, consequently, the value ``0``:

* in an element of the vector, ``direct_data_blocks`` means free slot (that element does not refer to a block of data directly)
* ``indirect_data_block`` means that no data block is allocated to keep track of indirect data blocks (when no indirect data blocks are needed)
* an index within the data block referred to as ``indirect_data_block`` means free slot (that index does not refer to an indirect data block)

It is guaranteed that the number of bytes occupied by an inode on the disk is a divisor of the block size.

Directories have associated a single block of data (referred to as ``direct_data_block [0]``) in which directory entries will be stored. These are described by the following structure:

.. code-block:: c

    struct pitix_dir_entry {
            __u32 ino;
            char name [PITIX_NAME_LEN];
    };

where

* ``inoi`` is the inode number of the file or directory; this number is an index in the inode area
* ``name`` is the name of the file or directory; maximum name length is ``16`` bytes (``PITIX_NAME_LEN``); if the name length is less than 16 bytes, then the name will end with the ASCII character that has the code ``0`` (same as for strings)

The root directory will be assigned inode ``0`` and data block ``0``.

For simplicity, at ``mkdir`` it is not necessary to create the entries ``.`` (*dot*) and ``..`` (*dot dot*) in the new directory; the checker uses this assumption.

All numeric values are stored on disk in byte-order CPU.

In the `assignment header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/5-pitix/pitix.h`__ you will find the structures described above together with useful macros and statements of the main functions to be implemented.

The kernel module will be named ``pitix.ko``.

Testing
=======

.. note::

    Enable ``Loop Devices`` support using ``make menuconfig``. ``Device drivers -> Block devices -> Loopback device support``

In order to simplify the assignment evaluation process, but also to reduce the mistakes of the submitted assignments, the assignment evaluation will be done automatically with with the help of public tests that are in the new infrastructure.

For local testing, use the following commands:

.. code-block:: console

    $ git clone https://github.com/linux-kernel-labs/linux.git
    $ cd linux/tools/labs
    $ LABS=assignments/5-pitix make skels
    $ #the development of the assignment will be written in the 5-pitix directory
    $ make build
    $ make copy
    $ make boot

Instructions for using the test suite can be found in the ``README`` file.

Tips
----

To increase your chances of getting the highest grade, read and follow the Linux kernel coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

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

As a more difficult assignment, it is worth 2 points.

Information about assigments penalties can be found on the
`General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__.

In exceptional cases (the assigment passes the tests by not complying with the requirements)
and if the assigment does not pass all the tests, the grade will may decrease more than mentioned above.

Submitting the assigment
------------------------

The assignment archive will be submitted to vmchecker, according to the rules on the
`rules page <https://ocw.cs.pub.ro/courses/so2/reguli-notare#reguli_de_trimitere_a_temelor>`__.

In the vmchecker interface choose the ``Google Challenge - Sistem de fișiere`` option for this assignment.

Resources
=========

* `assignment header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/5-pitix/pitix.h>`__
* `Lab 08: File system drivers (Part 1) <https://linux-kernel-labs.github.io/refs/heads/master/so2/lab8-filesystems-part1.html>`__
* `Lab 09: File system drivers (Part 2) <https://linux-kernel-labs.github.io/refs/heads/master/so2/lab9-filesystems-part2.html>`__
* `Minix filesystem source code <https://elixir.bootlin.com/linux/v4.15/source/fs/minix>`__

We recommend that you use GitLab to store your homework. Follow the directions in
`README <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/README.md>`__
and on the dedicated `Git wiki page <https://ocw.cs.pub.ro/courses/so2/teme/folosire-gitlab>`__.

The resources for the assignment can also be found in the `so2-assignments <https://github.com/systems-cs-pub-ro/so2-assignments>`__ repo on GitHub.
The repo contains a `Bash script <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/so2-create-repo.sh>`__
that helps you create a private repository on the faculty `GitLab <https://gitlab.cs.pub.ro/users/sign_in>`__ instance.
Follow the tips from the `README <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/README.md>`__ and
on the dedicated `Wiki page <https://ocw.cs.pub.ro/courses/so2/teme/folosire-gitlab>`__.

Questions
=========

For questions about the assigment, you can consult the mailing `list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
or send an e-mail (you must be `registered <http://cursuri.cs.pub.ro/cgi-bin/mailman/listinfo/so2>`__).
Please follow and follow `the tips for use of the list <https://ocw.cs.pub.ro/courses/so2/resurse/lista-discutii#mailing-list-guidelines>`__.

Before you ask a question, make sure that:

* you have read the statement of the assigment well
* the question is not already presented on the `FAQ page <https://ocw.cs.pub.ro/courses/so2/teme/tema2/faq>`__
* the answer cannot be found in the `mailing list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
