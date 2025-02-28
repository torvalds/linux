.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

This document briefly describes how you can use DAMON by demonstrating its
default user space tool.  Please note that this document describes only a part
of its features for brevity.  Please refer to the usage `doc
<https://github.com/damonitor/damo/blob/next/USAGE.md>`_ of the tool for more
details.


Prerequisites
=============

Kernel
------

You should first ensure your system is running on a kernel built with
``CONFIG_DAMON_*=y``.


User Space Tool
---------------

For the demonstration, we will use the default user space tool for DAMON,
called DAMON Operator (DAMO).  It is available at
https://github.com/damonitor/damo.  The examples below assume that ``damo`` is on
your ``$PATH``.  It's not mandatory, though.

Because DAMO is using the sysfs interface (refer to :doc:`usage` for the
detail) of DAMON, you should ensure :doc:`sysfs </filesystems/sysfs>` is
mounted.


Snapshot Data Access Patterns
=============================

The commands below show the memory access pattern of a program at the moment of
the execution. ::

    $ git clone https://github.com/sjp38/masim; cd masim; make
    $ sudo damo start "./masim ./configs/stairs.cfg --quiet"
    $ sudo damo report access
    heatmap: 641111111000000000000000000000000000000000000000000000[...]33333333333333335557984444[...]7
    # min/max temperatures: -1,840,000,000, 370,010,000, column size: 3.925 MiB
    0   addr 86.182 TiB   size 8.000 KiB   access 0 %   age 14.900 s
    1   addr 86.182 TiB   size 8.000 KiB   access 60 %  age 0 ns
    2   addr 86.182 TiB   size 3.422 MiB   access 0 %   age 4.100 s
    3   addr 86.182 TiB   size 2.004 MiB   access 95 %  age 2.200 s
    4   addr 86.182 TiB   size 29.688 MiB  access 0 %   age 14.100 s
    5   addr 86.182 TiB   size 29.516 MiB  access 0 %   age 16.700 s
    6   addr 86.182 TiB   size 29.633 MiB  access 0 %   age 17.900 s
    7   addr 86.182 TiB   size 117.652 MiB access 0 %   age 18.400 s
    8   addr 126.990 TiB  size 62.332 MiB  access 0 %   age 9.500 s
    9   addr 126.990 TiB  size 13.980 MiB  access 0 %   age 5.200 s
    10  addr 126.990 TiB  size 9.539 MiB   access 100 % age 3.700 s
    11  addr 126.990 TiB  size 16.098 MiB  access 0 %   age 6.400 s
    12  addr 127.987 TiB  size 132.000 KiB access 0 %   age 2.900 s
    total size: 314.008 MiB
    $ sudo damo stop

The first command of the above example downloads and builds an artificial
memory access generator program called ``masim``.  The second command asks DAMO
to start the program via the given command and make DAMON monitors the newly
started process.  The third command retrieves the current snapshot of the
monitored access pattern of the process from DAMON and shows the pattern in a
human readable format.

The first line of the output shows the relative access temperature (hotness) of
the regions in a single row hetmap format.  Each column on the heatmap
represents regions of same size on the monitored virtual address space.  The
position of the colun on the row and the number on the column represents the
relative location and access temperature of the region.  ``[...]`` means
unmapped huge regions on the virtual address spaces.  The second line shows
additional information for better understanding the heatmap.

Each line of the output from the third line shows which virtual address range
(``addr XX size XX``) of the process is how frequently (``access XX %``)
accessed for how long time (``age XX``).  For example, the evelenth region of
~9.5 MiB size is being most frequently accessed for last 3.7 seconds.  Finally,
the fourth command stops DAMON.

Note that DAMON can monitor not only virtual address spaces but multiple types
of address spaces including the physical address space.


Recording Data Access Patterns
==============================

The commands below record the memory access patterns of a program and save the
monitoring results to a file. ::

    $ ./masim ./configs/zigzag.cfg &
    $ sudo damo record -o damon.data $(pidof masim)

The line of the commands run the artificial memory access
generator program again.  The generator will repeatedly
access two 100 MiB sized memory regions one by one.  You can substitute this
with your real workload.  The last line asks ``damo`` to record the access
pattern in the ``damon.data`` file.


Visualizing Recorded Patterns
=============================

You can visualize the pattern in a heatmap, showing which memory region
(x-axis) got accessed when (y-axis) and how frequently (number).::

    $ sudo damo report heatmap
    22222222222222222222222222222222222222211111111111111111111111111111111111111100
    44444444444444444444444444444444444444434444444444444444444444444444444444443200
    44444444444444444444444444444444444444433444444444444444444444444444444444444200
    33333333333333333333333333333333333333344555555555555555555555555555555555555200
    33333333333333333333333333333333333344444444444444444444444444444444444444444200
    22222222222222222222222222222222222223355555555555555555555555555555555555555200
    00000000000000000000000000000000000000288888888888888888888888888888888888888400
    00000000000000000000000000000000000000288888888888888888888888888888888888888400
    33333333333333333333333333333333333333355555555555555555555555555555555555555200
    88888888888888888888888888888888888888600000000000000000000000000000000000000000
    88888888888888888888888888888888888888600000000000000000000000000000000000000000
    33333333333333333333333333333333333333444444444444444444444444444444444444443200
    00000000000000000000000000000000000000288888888888888888888888888888888888888400
    [...]
    # access_frequency:  0  1  2  3  4  5  6  7  8  9
    # x-axis: space (139728247021568-139728453431248: 196.848 MiB)
    # y-axis: time (15256597248362-15326899978162: 1 m 10.303 s)
    # resolution: 80x40 (2.461 MiB and 1.758 s for each character)

You can also visualize the distribution of the working set size, sorted by the
size.::

    $ sudo damo report wss --range 0 101 10
    # <percentile> <wss>
    # target_id     18446632103789443072
    # avr:  107.708 MiB
      0             0 B |                                                           |
     10      95.328 MiB |****************************                               |
     20      95.332 MiB |****************************                               |
     30      95.340 MiB |****************************                               |
     40      95.387 MiB |****************************                               |
     50      95.387 MiB |****************************                               |
     60      95.398 MiB |****************************                               |
     70      95.398 MiB |****************************                               |
     80      95.504 MiB |****************************                               |
     90     190.703 MiB |*********************************************************  |
    100     196.875 MiB |***********************************************************|

Using ``--sortby`` option with the above command, you can show how the working
set size has chronologically changed.::

    $ sudo damo report wss --range 0 101 10 --sortby time
    # <percentile> <wss>
    # target_id     18446632103789443072
    # avr:  107.708 MiB
      0       3.051 MiB |                                                           |
     10     190.703 MiB |***********************************************************|
     20      95.336 MiB |*****************************                              |
     30      95.328 MiB |*****************************                              |
     40      95.387 MiB |*****************************                              |
     50      95.332 MiB |*****************************                              |
     60      95.320 MiB |*****************************                              |
     70      95.398 MiB |*****************************                              |
     80      95.398 MiB |*****************************                              |
     90      95.340 MiB |*****************************                              |
    100      95.398 MiB |*****************************                              |


Data Access Pattern Aware Memory Management
===========================================

Below command makes every memory region of size >=4K that has not accessed for
>=60 seconds in your workload to be swapped out. ::

    $ sudo damo start --damos_access_rate 0 0 --damos_sz_region 4K max \
                      --damos_age 60s max --damos_action pageout \
                      <pid of your workload>
