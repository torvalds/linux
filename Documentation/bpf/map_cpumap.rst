.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

===================
BPF_MAP_TYPE_CPUMAP
===================

.. note::
   - ``BPF_MAP_TYPE_CPUMAP`` was introduced in kernel version 4.15

.. kernel-doc:: kernel/bpf/cpumap.c
 :doc: cpu map

An example use-case for this map type is software based Receive Side Scaling (RSS).

The CPUMAP represents the CPUs in the system indexed as the map-key, and the
map-value is the config setting (per CPUMAP entry). Each CPUMAP entry has a dedicated
kernel thread bound to the given CPU to represent the remote CPU execution unit.

Starting from Linux kernel version 5.9 the CPUMAP can run a second XDP program
on the remote CPU. This allows an XDP program to split its processing across
multiple CPUs. For example, a scenario where the initial CPU (that sees/receives
the packets) needs to do minimal packet processing and the remote CPU (to which
the packet is directed) can afford to spend more cycles processing the frame. The
initial CPU is where the XDP redirect program is executed. The remote CPU
receives raw ``xdp_frame`` objects.

Usage
=====

Kernel BPF
----------
bpf_redirect_map()
^^^^^^^^^^^^^^^^^^
.. code-block:: c

     long bpf_redirect_map(struct bpf_map *map, u32 key, u64 flags)

Redirect the packet to the endpoint referenced by ``map`` at index ``key``.
For ``BPF_MAP_TYPE_CPUMAP`` this map contains references to CPUs.

The lower two bits of ``flags`` are used as the return code if the map lookup
fails. This is so that the return value can be one of the XDP program return
codes up to ``XDP_TX``, as chosen by the caller.

User space
----------
.. note::
    CPUMAP entries can only be updated/looked up/deleted from user space and not
    from an eBPF program. Trying to call these functions from a kernel eBPF
    program will result in the program failing to load and a verifier warning.

bpf_map_update_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_update_elem(int fd, const void *key, const void *value, __u64 flags);

CPU entries can be added or updated using the ``bpf_map_update_elem()``
helper. This helper replaces existing elements atomically. The ``value`` parameter
can be ``struct bpf_cpumap_val``.

 .. code-block:: c

    struct bpf_cpumap_val {
        __u32 qsize;  /* queue size to remote target CPU */
        union {
            int   fd; /* prog fd on map write */
            __u32 id; /* prog id on map read */
        } bpf_prog;
    };

The flags argument can be one of the following:
  - BPF_ANY: Create a new element or update an existing element.
  - BPF_NOEXIST: Create a new element only if it did not exist.
  - BPF_EXIST: Update an existing element.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_lookup_elem(int fd, const void *key, void *value);

CPU entries can be retrieved using the ``bpf_map_lookup_elem()``
helper.

bpf_map_delete_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_delete_elem(int fd, const void *key);

CPU entries can be deleted using the ``bpf_map_delete_elem()``
helper. This helper will return 0 on success, or negative error in case of
failure.

Examples
========
Kernel
------

The following code snippet shows how to declare a ``BPF_MAP_TYPE_CPUMAP`` called
``cpu_map`` and how to redirect packets to a remote CPU using a round robin scheme.

.. code-block:: c

   struct {
        __uint(type, BPF_MAP_TYPE_CPUMAP);
        __type(key, __u32);
        __type(value, struct bpf_cpumap_val);
        __uint(max_entries, 12);
    } cpu_map SEC(".maps");

    struct {
        __uint(type, BPF_MAP_TYPE_ARRAY);
        __type(key, __u32);
        __type(value, __u32);
        __uint(max_entries, 12);
    } cpus_available SEC(".maps");

    struct {
        __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
        __type(key, __u32);
        __type(value, __u32);
        __uint(max_entries, 1);
    } cpus_iterator SEC(".maps");

    SEC("xdp")
    int  xdp_redir_cpu_round_robin(struct xdp_md *ctx)
    {
        __u32 key = 0;
        __u32 cpu_dest = 0;
        __u32 *cpu_selected, *cpu_iterator;
        __u32 cpu_idx;

        cpu_iterator = bpf_map_lookup_elem(&cpus_iterator, &key);
        if (!cpu_iterator)
            return XDP_ABORTED;
        cpu_idx = *cpu_iterator;

        *cpu_iterator += 1;
        if (*cpu_iterator == bpf_num_possible_cpus())
            *cpu_iterator = 0;

        cpu_selected = bpf_map_lookup_elem(&cpus_available, &cpu_idx);
        if (!cpu_selected)
            return XDP_ABORTED;
        cpu_dest = *cpu_selected;

        if (cpu_dest >= bpf_num_possible_cpus())
            return XDP_ABORTED;

        return bpf_redirect_map(&cpu_map, cpu_dest, 0);
    }

User space
----------

The following code snippet shows how to dynamically set the max_entries for a
CPUMAP to the max number of cpus available on the system.

.. code-block:: c

    int set_max_cpu_entries(struct bpf_map *cpu_map)
    {
        if (bpf_map__set_max_entries(cpu_map, libbpf_num_possible_cpus()) < 0) {
            fprintf(stderr, "Failed to set max entries for cpu_map map: %s",
                strerror(errno));
            return -1;
        }
        return 0;
    }

References
===========

- https://developers.redhat.com/blog/2021/05/13/receive-side-scaling-rss-with-ebpf-and-cpumap#redirecting_into_a_cpumap
