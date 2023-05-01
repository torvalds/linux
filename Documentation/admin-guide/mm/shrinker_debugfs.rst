==========================
Shrinker Debugfs Interface
==========================

Shrinker debugfs interface provides a visibility into the kernel memory
shrinkers subsystem and allows to get information about individual shrinkers
and interact with them.

For each shrinker registered in the system a directory in **<debugfs>/shrinker/**
is created. The directory's name is composed from the shrinker's name and an
unique id: e.g. *kfree_rcu-0* or *sb-xfs:vda1-36*.

Each shrinker directory contains **count** and **scan** files, which allow to
trigger *count_objects()* and *scan_objects()* callbacks for each memcg and
numa node (if applicable).

Usage:
------

1. *List registered shrinkers*

  ::

    $ cd /sys/kernel/debug/shrinker/
    $ ls
    dquota-cache-16     sb-devpts-28     sb-proc-47       sb-tmpfs-42
    mm-shadow-18        sb-devtmpfs-5    sb-proc-48       sb-tmpfs-43
    mm-zspool:zram0-34  sb-hugetlbfs-17  sb-pstore-31     sb-tmpfs-44
    rcu-kfree-0         sb-hugetlbfs-33  sb-rootfs-2      sb-tmpfs-49
    sb-aio-20           sb-iomem-12      sb-securityfs-6  sb-tracefs-13
    sb-anon_inodefs-15  sb-mqueue-21     sb-selinuxfs-22  sb-xfs:vda1-36
    sb-bdev-3           sb-nsfs-4        sb-sockfs-8      sb-zsmalloc-19
    sb-bpf-32           sb-pipefs-14     sb-sysfs-26      thp-deferred_split-10
    sb-btrfs:vda2-24    sb-proc-25       sb-tmpfs-1       thp-zero-9
    sb-cgroup2-30       sb-proc-39       sb-tmpfs-27      xfs-buf:vda1-37
    sb-configfs-23      sb-proc-41       sb-tmpfs-29      xfs-inodegc:vda1-38
    sb-dax-11           sb-proc-45       sb-tmpfs-35
    sb-debugfs-7        sb-proc-46       sb-tmpfs-40

2. *Get information about a specific shrinker*

  ::

    $ cd sb-btrfs\:vda2-24/
    $ ls
    count            scan

3. *Count objects*

  Each line in the output has the following format::

    <cgroup inode id> <nr of objects on node 0> <nr of objects on node 1> ...
    <cgroup inode id> <nr of objects on node 0> <nr of objects on node 1> ...
    ...

  If there are no objects on all numa nodes, a line is omitted. If there
  are no objects at all, the output might be empty.

  If the shrinker is not memcg-aware or CONFIG_MEMCG is off, 0 is printed
  as cgroup inode id. If the shrinker is not numa-aware, 0's are printed
  for all nodes except the first one.
  ::

    $ cat count
    1 224 2
    21 98 0
    55 818 10
    2367 2 0
    2401 30 0
    225 13 0
    599 35 0
    939 124 0
    1041 3 0
    1075 1 0
    1109 1 0
    1279 60 0
    1313 7 0
    1347 39 0
    1381 3 0
    1449 14 0
    1483 63 0
    1517 53 0
    1551 6 0
    1585 1 0
    1619 6 0
    1653 40 0
    1687 11 0
    1721 8 0
    1755 4 0
    1789 52 0
    1823 888 0
    1857 1 0
    1925 2 0
    1959 32 0
    2027 22 0
    2061 9 0
    2469 799 0
    2537 861 0
    2639 1 0
    2707 70 0
    2775 4 0
    2877 84 0
    293 1 0
    735 8 0

4. *Scan objects*

  The expected input format::

    <cgroup inode id> <numa id> <number of objects to scan>

  For a non-memcg-aware shrinker or on a system with no memory
  cgrups **0** should be passed as cgroup id.
  ::

    $ cd /sys/kernel/debug/shrinker/
    $ cd sb-btrfs\:vda2-24/

    $ cat count | head -n 5
    1 212 0
    21 97 0
    55 802 5
    2367 2 0
    225 13 0

    $ echo "55 0 200" > scan

    $ cat count | head -n 5
    1 212 0
    21 96 0
    55 752 5
    2367 2 0
    225 13 0
