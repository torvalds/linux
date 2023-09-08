.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

=========================
BPF_MAP_TYPE_BLOOM_FILTER
=========================

.. note::
   - ``BPF_MAP_TYPE_BLOOM_FILTER`` was introduced in kernel version 5.16

``BPF_MAP_TYPE_BLOOM_FILTER`` provides a BPF bloom filter map. Bloom
filters are a space-efficient probabilistic data structure used to
quickly test whether an element exists in a set. In a bloom filter,
false positives are possible whereas false negatives are not.

The bloom filter map does not have keys, only values. When the bloom
filter map is created, it must be created with a ``key_size`` of 0.  The
bloom filter map supports two operations:

- push: adding an element to the map
- peek: determining whether an element is present in the map

BPF programs must use ``bpf_map_push_elem`` to add an element to the
bloom filter map and ``bpf_map_peek_elem`` to query the map. These
operations are exposed to userspace applications using the existing
``bpf`` syscall in the following way:

- ``BPF_MAP_UPDATE_ELEM`` -> push
- ``BPF_MAP_LOOKUP_ELEM`` -> peek

The ``max_entries`` size that is specified at map creation time is used
to approximate a reasonable bitmap size for the bloom filter, and is not
otherwise strictly enforced. If the user wishes to insert more entries
into the bloom filter than ``max_entries``, this may lead to a higher
false positive rate.

The number of hashes to use for the bloom filter is configurable using
the lower 4 bits of ``map_extra`` in ``union bpf_attr`` at map creation
time. If no number is specified, the default used will be 5 hash
functions. In general, using more hashes decreases both the false
positive rate and the speed of a lookup.

It is not possible to delete elements from a bloom filter map. A bloom
filter map may be used as an inner map. The user is responsible for
synchronising concurrent updates and lookups to ensure no false negative
lookups occur.

Usage
=====

Kernel BPF
----------

bpf_map_push_elem()
~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_push_elem(struct bpf_map *map, const void *value, u64 flags)

A ``value`` can be added to a bloom filter using the
``bpf_map_push_elem()`` helper. The ``flags`` parameter must be set to
``BPF_ANY`` when adding an entry to the bloom filter. This helper
returns ``0`` on success, or negative error in case of failure.

bpf_map_peek_elem()
~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_peek_elem(struct bpf_map *map, void *value)

The ``bpf_map_peek_elem()`` helper is used to determine whether
``value`` is present in the bloom filter map. This helper returns ``0``
if ``value`` is probably present in the map, or ``-ENOENT`` if ``value``
is definitely not present in the map.

Userspace
---------

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_update_elem (int fd, const void *key, const void *value, __u64 flags)

A userspace program can add a ``value`` to a bloom filter using libbpf's
``bpf_map_update_elem`` function. The ``key`` parameter must be set to
``NULL`` and ``flags`` must be set to ``BPF_ANY``. Returns ``0`` on
success, or negative error in case of failure.

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_lookup_elem (int fd, const void *key, void *value)

A userspace program can determine the presence of ``value`` in a bloom
filter using libbpf's ``bpf_map_lookup_elem`` function. The ``key``
parameter must be set to ``NULL``. Returns ``0`` if ``value`` is
probably present in the map, or ``-ENOENT`` if ``value`` is definitely
not present in the map.

Examples
========

Kernel BPF
----------

This snippet shows how to declare a bloom filter in a BPF program:

.. code-block:: c

    struct {
            __uint(type, BPF_MAP_TYPE_BLOOM_FILTER);
            __type(value, __u32);
            __uint(max_entries, 1000);
            __uint(map_extra, 3);
    } bloom_filter SEC(".maps");

This snippet shows how to determine presence of a value in a bloom
filter in a BPF program:

.. code-block:: c

    void *lookup(__u32 key)
    {
            if (bpf_map_peek_elem(&bloom_filter, &key) == 0) {
                    /* Verify not a false positive and fetch an associated
                     * value using a secondary lookup, e.g. in a hash table
                     */
                    return bpf_map_lookup_elem(&hash_table, &key);
            }
            return 0;
    }

Userspace
---------

This snippet shows how to use libbpf to create a bloom filter map from
userspace:

.. code-block:: c

    int create_bloom()
    {
            LIBBPF_OPTS(bpf_map_create_opts, opts,
                        .map_extra = 3);             /* number of hashes */

            return bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER,
                                  "ipv6_bloom",      /* name */
                                  0,                 /* key size, must be zero */
                                  sizeof(ipv6_addr), /* value size */
                                  10000,             /* max entries */
                                  &opts);            /* create options */
    }

This snippet shows how to add an element to a bloom filter from
userspace:

.. code-block:: c

    int add_element(struct bpf_map *bloom_map, __u32 value)
    {
            int bloom_fd = bpf_map__fd(bloom_map);
            return bpf_map_update_elem(bloom_fd, NULL, &value, BPF_ANY);
    }

References
==========

https://lwn.net/ml/bpf/20210831225005.2762202-1-joannekoong@fb.com/
