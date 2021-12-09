.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

This document briefly describes how you can use DAMON by demonstrating its
default user space tool.  Please note that this document describes only a part
of its features for brevity.  Please refer to :doc:`usage` for more details.


TL; DR
======

Follow the commands below to monitor and visualize the memory access pattern of
your workload. ::

    # # build the kernel with CONFIG_DAMON_*=y, install it, and reboot
    # mount -t debugfs none /sys/kernel/debug/
    # git clone https://github.com/awslabs/damo
    # ./damo/damo record $(pidof <your workload>)
    # ./damo/damo report heat --plot_ascii

The final command draws the access heatmap of ``<your workload>``.  The heatmap
shows which memory region (x-axis) is accessed when (y-axis) and how frequently
(number; the higher the more accesses have been observed). ::

    111111111111111111111111111111111111111111111111111111110000
    111121111111111111111111111111211111111111111111111111110000
    000000000000000000000000000000000000000000000000001555552000
    000000000000000000000000000000000000000000000222223555552000
    000000000000000000000000000000000000000011111677775000000000
    000000000000000000000000000000000000000488888000000000000000
    000000000000000000000000000000000177888400000000000000000000
    000000000000000000000000000046666522222100000000000000000000
    000000000000000000000014444344444300000000000000000000000000
    000000000000000002222245555510000000000000000000000000000000
    # access_frequency:  0  1  2  3  4  5  6  7  8  9
    # x-axis: space (140286319947776-140286426374096: 101.496 MiB)
    # y-axis: time (605442256436361-605479951866441: 37.695430s)
    # resolution: 60x10 (1.692 MiB and 3.770s for each character)


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
https://github.com/awslabs/damo.  The examples below assume that ``damo`` is on
your ``$PATH``.  It's not mandatory, though.

Because DAMO is using the debugfs interface (refer to :doc:`usage` for the
detail) of DAMON, you should ensure debugfs is mounted.  Mount it manually as
below::

    # mount -t debugfs none /sys/kernel/debug/

or append the following line to your ``/etc/fstab`` file so that your system
can automatically mount debugfs upon booting::

    debugfs /sys/kernel/debug debugfs defaults 0 0


Recording Data Access Patterns
==============================

The commands below record the memory access patterns of a program and save the
monitoring results to a file. ::

    $ git clone https://github.com/sjp38/masim
    $ cd masim; make; ./masim ./configs/zigzag.cfg &
    $ sudo damo record -o damon.data $(pidof masim)

The first two lines of the commands download an artificial memory access
generator program and run it in the background.  The generator will repeatedly
access two 100 MiB sized memory regions one by one.  You can substitute this
with your real workload.  The last line asks ``damo`` to record the access
pattern in the ``damon.data`` file.


Visualizing Recorded Patterns
=============================

The following three commands visualize the recorded access patterns and save
the results as separate image files. ::

    $ damo report heats --heatmap access_pattern_heatmap.png
    $ damo report wss --range 0 101 1 --plot wss_dist.png
    $ damo report wss --range 0 101 1 --sortby time --plot wss_chron_change.png

- ``access_pattern_heatmap.png`` will visualize the data access pattern in a
  heatmap, showing which memory region (y-axis) got accessed when (x-axis)
  and how frequently (color).
- ``wss_dist.png`` will show the distribution of the working set size.
- ``wss_chron_change.png`` will show how the working set size has
  chronologically changed.

You can view the visualizations of this example workload at [1]_.
Visualizations of other realistic workloads are available at [2]_ [3]_ [4]_.

.. [1] https://damonitor.github.io/doc/html/v17/admin-guide/mm/damon/start.html#visualizing-recorded-patterns
.. [2] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.1.png.html
.. [3] https://damonitor.github.io/test/result/visual/latest/rec.wss_sz.png.html
.. [4] https://damonitor.github.io/test/result/visual/latest/rec.wss_time.png.html
