.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

========================================================
BPF_MAP_TYPE_ARRAY_OF_MAPS and BPF_MAP_TYPE_HASH_OF_MAPS
========================================================

.. note::
   - ``BPF_MAP_TYPE_ARRAY_OF_MAPS`` and ``BPF_MAP_TYPE_HASH_OF_MAPS`` were
     introduced in kernel version 4.12

``BPF_MAP_TYPE_ARRAY_OF_MAPS`` and ``BPF_MAP_TYPE_HASH_OF_MAPS`` provide general
purpose support for map in map storage. One level of nesting is supported, where
an outer map contains instances of a single type of inner map, for example
``array_of_maps->sock_map``.

When creating an outer map, an inner map instance is used to initialize the
metadata that the outer map holds about its inner maps. This inner map has a
separate lifetime from the outer map and can be deleted after the outer map has
been created.

The outer map supports element lookup, update and delete from user space using
the syscall API. A BPF program is only allowed to do element lookup in the outer
map.

.. note::
   - Multi-level nesting is not supported.
   - Any BPF map type can be used as an inner map, except for
     ``BPF_MAP_TYPE_PROG_ARRAY``.
   - A BPF program cannot update or delete outer map entries.

For ``BPF_MAP_TYPE_ARRAY_OF_MAPS`` the key is an unsigned 32-bit integer index
into the array. The array is a fixed size with ``max_entries`` elements that are
zero initialized when created.

For ``BPF_MAP_TYPE_HASH_OF_MAPS`` the key type can be chosen when defining the
map. The kernel is responsible for allocating and freeing key/value pairs, up to
the max_entries limit that you specify. Hash maps use pre-allocation of hash
table elements by default. The ``BPF_F_NO_PREALLOC`` flag can be used to disable
pre-allocation when it is too memory expensive.

Usage
=====

Kernel BPF Helper
-----------------

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

Inner maps can be retrieved using the ``bpf_map_lookup_elem()`` helper. This
helper returns a pointer to the inner map, or ``NULL`` if no entry was found.

Examples
========

Kernel BPF Example
------------------

This snippet shows how to create and initialise an array of devmaps in a BPF
program. Note that the outer array can only be modified from user space using
the syscall API.

.. code-block:: c

    struct inner_map {
            __uint(type, BPF_MAP_TYPE_DEVMAP);
            __uint(max_entries, 10);
            __type(key, __u32);
            __type(value, __u32);
    } inner_map1 SEC(".maps"), inner_map2 SEC(".maps");

    struct {
            __uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
            __uint(max_entries, 2);
            __type(key, __u32);
            __array(values, struct inner_map);
    } outer_map SEC(".maps") = {
            .values = { &inner_map1,
                        &inner_map2 }
    };

See ``progs/test_btf_map_in_map.c`` in ``tools/testing/selftests/bpf`` for more
examples of declarative initialisation of outer maps.

User Space
----------

This snippet shows how to create an array based outer map:

.. code-block:: c

    int create_outer_array(int inner_fd) {
            LIBBPF_OPTS(bpf_map_create_opts, opts, .inner_map_fd = inner_fd);
            int fd;

            fd = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS,
                                "example_array",       /* name */
                                sizeof(__u32),         /* key size */
                                sizeof(__u32),         /* value size */
                                256,                   /* max entries */
                                &opts);                /* create opts */
            return fd;
    }


This snippet shows how to add an inner map to an outer map:

.. code-block:: c

    int add_devmap(int outer_fd, int index, const char *name) {
            int fd;

            fd = bpf_map_create(BPF_MAP_TYPE_DEVMAP, name,
                                sizeof(__u32), sizeof(__u32), 256, NULL);
            if (fd < 0)
                    return fd;

            return bpf_map_update_elem(outer_fd, &index, &fd, BPF_ANY);
    }

References
==========

- https://lore.kernel.org/netdev/20170322170035.923581-3-kafai@fb.com/
- https://lore.kernel.org/netdev/20170322170035.923581-4-kafai@fb.com/
