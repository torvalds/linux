.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

================================================
BPF_MAP_TYPE_ARRAY and BPF_MAP_TYPE_PERCPU_ARRAY
================================================

.. note::
   - ``BPF_MAP_TYPE_ARRAY`` was introduced in kernel version 3.19
   - ``BPF_MAP_TYPE_PERCPU_ARRAY`` was introduced in version 4.6

``BPF_MAP_TYPE_ARRAY`` and ``BPF_MAP_TYPE_PERCPU_ARRAY`` provide generic array
storage. The key type is an unsigned 32-bit integer (4 bytes) and the map is
of constant size. The size of the array is defined in ``max_entries`` at
creation time. All array elements are pre-allocated and zero initialized when
created. ``BPF_MAP_TYPE_PERCPU_ARRAY`` uses a different memory region for each
CPU whereas ``BPF_MAP_TYPE_ARRAY`` uses the same memory region. The value
stored can be of any size, however, all array elements are aligned to 8
bytes.

Since kernel 5.5, memory mapping may be enabled for ``BPF_MAP_TYPE_ARRAY`` by
setting the flag ``BPF_F_MMAPABLE``. The map definition is page-aligned and
starts on the first page. Sufficient page-sized and page-aligned blocks of
memory are allocated to store all array values, starting on the second page,
which in some cases will result in over-allocation of memory. The benefit of
using this is increased performance and ease of use since userspace programs
would not be required to use helper functions to access and mutate data.

Usage
=====

Kernel BPF
----------

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

Array elements can be retrieved using the ``bpf_map_lookup_elem()`` helper.
This helper returns a pointer into the array element, so to avoid data races
with userspace reading the value, the user must use primitives like
``__sync_fetch_and_add()`` when updating the value in-place.

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, u64 flags)

Array elements can be updated using the ``bpf_map_update_elem()`` helper.

``bpf_map_update_elem()`` returns 0 on success, or negative error in case of
failure.

Since the array is of constant size, ``bpf_map_delete_elem()`` is not supported.
To clear an array element, you may use ``bpf_map_update_elem()`` to insert a
zero value to that index.

Per CPU Array
-------------

Values stored in ``BPF_MAP_TYPE_ARRAY`` can be accessed by multiple programs
across different CPUs. To restrict storage to a single CPU, you may use a
``BPF_MAP_TYPE_PERCPU_ARRAY``.

When using a ``BPF_MAP_TYPE_PERCPU_ARRAY`` the ``bpf_map_update_elem()`` and
``bpf_map_lookup_elem()`` helpers automatically access the slot for the current
CPU.

bpf_map_lookup_percpu_elem()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_percpu_elem(struct bpf_map *map, const void *key, u32 cpu)

The ``bpf_map_lookup_percpu_elem()`` helper can be used to lookup the array
value for a specific CPU. Returns value on success , or ``NULL`` if no entry was
found or ``cpu`` is invalid.

Concurrency
-----------

Since kernel version 5.1, the BPF infrastructure provides ``struct bpf_spin_lock``
to synchronize access.

Userspace
---------

Access from userspace uses libbpf APIs with the same names as above, with
the map identified by its ``fd``.

Examples
========

Please see the ``tools/testing/selftests/bpf`` directory for functional
examples. The code samples below demonstrate API usage.

Kernel BPF
----------

This snippet shows how to declare an array in a BPF program.

.. code-block:: c

    struct {
            __uint(type, BPF_MAP_TYPE_ARRAY);
            __type(key, u32);
            __type(value, long);
            __uint(max_entries, 256);
    } my_map SEC(".maps");


This example BPF program shows how to access an array element.

.. code-block:: c

    int bpf_prog(struct __sk_buff *skb)
    {
            struct iphdr ip;
            int index;
            long *value;

            if (bpf_skb_load_bytes(skb, ETH_HLEN, &ip, sizeof(ip)) < 0)
                    return 0;

            index = ip.protocol;
            value = bpf_map_lookup_elem(&my_map, &index);
            if (value)
                    __sync_fetch_and_add(value, skb->len);

            return 0;
    }

Userspace
---------

BPF_MAP_TYPE_ARRAY
~~~~~~~~~~~~~~~~~~

This snippet shows how to create an array, using ``bpf_map_create_opts`` to
set flags.

.. code-block:: c

    #include <bpf/libbpf.h>
    #include <bpf/bpf.h>

    int create_array()
    {
            int fd;
            LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_MMAPABLE);

            fd = bpf_map_create(BPF_MAP_TYPE_ARRAY,
                                "example_array",       /* name */
                                sizeof(__u32),         /* key size */
                                sizeof(long),          /* value size */
                                256,                   /* max entries */
                                &opts);                /* create opts */
            return fd;
    }

This snippet shows how to initialize the elements of an array.

.. code-block:: c

    int initialize_array(int fd)
    {
            __u32 i;
            long value;
            int ret;

            for (i = 0; i < 256; i++) {
                    value = i;
                    ret = bpf_map_update_elem(fd, &i, &value, BPF_ANY);
                    if (ret < 0)
                            return ret;
            }

            return ret;
    }

This snippet shows how to retrieve an element value from an array.

.. code-block:: c

    int lookup(int fd)
    {
            __u32 index = 42;
            long value;
            int ret;

            ret = bpf_map_lookup_elem(fd, &index, &value);
            if (ret < 0)
                    return ret;

            /* use value here */
            assert(value == 42);

            return ret;
    }

BPF_MAP_TYPE_PERCPU_ARRAY
~~~~~~~~~~~~~~~~~~~~~~~~~

This snippet shows how to initialize the elements of a per CPU array.

.. code-block:: c

    int initialize_array(int fd)
    {
            int ncpus = libbpf_num_possible_cpus();
            long values[ncpus];
            __u32 i, j;
            int ret;

            for (i = 0; i < 256 ; i++) {
                    for (j = 0; j < ncpus; j++)
                            values[j] = i;
                    ret = bpf_map_update_elem(fd, &i, &values, BPF_ANY);
                    if (ret < 0)
                            return ret;
            }

            return ret;
    }

This snippet shows how to access the per CPU elements of an array value.

.. code-block:: c

    int lookup(int fd)
    {
            int ncpus = libbpf_num_possible_cpus();
            __u32 index = 42, j;
            long values[ncpus];
            int ret;

            ret = bpf_map_lookup_elem(fd, &index, &values);
            if (ret < 0)
                    return ret;

            for (j = 0; j < ncpus; j++) {
                    /* Use per CPU value here */
                    assert(values[j] == 42);
            }

            return ret;
    }

Semantics
=========

As shown in the example above, when accessing a ``BPF_MAP_TYPE_PERCPU_ARRAY``
in userspace, each value is an array with ``ncpus`` elements.

When calling ``bpf_map_update_elem()`` the flag ``BPF_NOEXIST`` can not be used
for these maps.
