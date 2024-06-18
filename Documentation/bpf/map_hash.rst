.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.
.. Copyright (C) 2022-2023 Isovalent, Inc.

===============================================
BPF_MAP_TYPE_HASH, with PERCPU and LRU Variants
===============================================

.. note::
   - ``BPF_MAP_TYPE_HASH`` was introduced in kernel version 3.19
   - ``BPF_MAP_TYPE_PERCPU_HASH`` was introduced in version 4.6
   - Both ``BPF_MAP_TYPE_LRU_HASH`` and ``BPF_MAP_TYPE_LRU_PERCPU_HASH``
     were introduced in version 4.10

``BPF_MAP_TYPE_HASH`` and ``BPF_MAP_TYPE_PERCPU_HASH`` provide general
purpose hash map storage. Both the key and the value can be structs,
allowing for composite keys and values.

The kernel is responsible for allocating and freeing key/value pairs, up
to the max_entries limit that you specify. Hash maps use pre-allocation
of hash table elements by default. The ``BPF_F_NO_PREALLOC`` flag can be
used to disable pre-allocation when it is too memory expensive.

``BPF_MAP_TYPE_PERCPU_HASH`` provides a separate value slot per
CPU. The per-cpu values are stored internally in an array.

The ``BPF_MAP_TYPE_LRU_HASH`` and ``BPF_MAP_TYPE_LRU_PERCPU_HASH``
variants add LRU semantics to their respective hash tables. An LRU hash
will automatically evict the least recently used entries when the hash
table reaches capacity. An LRU hash maintains an internal LRU list that
is used to select elements for eviction. This internal LRU list is
shared across CPUs but it is possible to request a per CPU LRU list with
the ``BPF_F_NO_COMMON_LRU`` flag when calling ``bpf_map_create``.  The
following table outlines the properties of LRU maps depending on the a
map type and the flags used to create the map.

======================== ========================= ================================
Flag                     ``BPF_MAP_TYPE_LRU_HASH`` ``BPF_MAP_TYPE_LRU_PERCPU_HASH``
======================== ========================= ================================
**BPF_F_NO_COMMON_LRU**  Per-CPU LRU, global map   Per-CPU LRU, per-cpu map
**!BPF_F_NO_COMMON_LRU** Global LRU, global map    Global LRU, per-cpu map
======================== ========================= ================================

Usage
=====

Kernel BPF
----------

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, u64 flags)

Hash entries can be added or updated using the ``bpf_map_update_elem()``
helper. This helper replaces existing elements atomically. The ``flags``
parameter can be used to control the update behaviour:

- ``BPF_ANY`` will create a new element or update an existing element
- ``BPF_NOEXIST`` will create a new element only if one did not already
  exist
- ``BPF_EXIST`` will update an existing element

``bpf_map_update_elem()`` returns 0 on success, or negative error in
case of failure.

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

Hash entries can be retrieved using the ``bpf_map_lookup_elem()``
helper. This helper returns a pointer to the value associated with
``key``, or ``NULL`` if no entry was found.

bpf_map_delete_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_delete_elem(struct bpf_map *map, const void *key)

Hash entries can be deleted using the ``bpf_map_delete_elem()``
helper. This helper will return 0 on success, or negative error in case
of failure.

Per CPU Hashes
--------------

For ``BPF_MAP_TYPE_PERCPU_HASH`` and ``BPF_MAP_TYPE_LRU_PERCPU_HASH``
the ``bpf_map_update_elem()`` and ``bpf_map_lookup_elem()`` helpers
automatically access the hash slot for the current CPU.

bpf_map_lookup_percpu_elem()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_percpu_elem(struct bpf_map *map, const void *key, u32 cpu)

The ``bpf_map_lookup_percpu_elem()`` helper can be used to lookup the
value in the hash slot for a specific CPU. Returns value associated with
``key`` on ``cpu`` , or ``NULL`` if no entry was found or ``cpu`` is
invalid.

Concurrency
-----------

Values stored in ``BPF_MAP_TYPE_HASH`` can be accessed concurrently by
programs running on different CPUs.  Since Kernel version 5.1, the BPF
infrastructure provides ``struct bpf_spin_lock`` to synchronise access.
See ``tools/testing/selftests/bpf/progs/test_spin_lock.c``.

Userspace
---------

bpf_map_get_next_key()
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_get_next_key(int fd, const void *cur_key, void *next_key)

In userspace, it is possible to iterate through the keys of a hash using
libbpf's ``bpf_map_get_next_key()`` function. The first key can be fetched by
calling ``bpf_map_get_next_key()`` with ``cur_key`` set to
``NULL``. Subsequent calls will fetch the next key that follows the
current key. ``bpf_map_get_next_key()`` returns 0 on success, -ENOENT if
cur_key is the last key in the hash, or negative error in case of
failure.

Note that if ``cur_key`` gets deleted then ``bpf_map_get_next_key()``
will instead return the *first* key in the hash table which is
undesirable. It is recommended to use batched lookup if there is going
to be key deletion intermixed with ``bpf_map_get_next_key()``.

Examples
========

Please see the ``tools/testing/selftests/bpf`` directory for functional
examples.  The code snippets below demonstrates API usage.

This example shows how to declare an LRU Hash with a struct key and a
struct value.

.. code-block:: c

    #include <linux/bpf.h>
    #include <bpf/bpf_helpers.h>

    struct key {
        __u32 srcip;
    };

    struct value {
        __u64 packets;
        __u64 bytes;
    };

    struct {
            __uint(type, BPF_MAP_TYPE_LRU_HASH);
            __uint(max_entries, 32);
            __type(key, struct key);
            __type(value, struct value);
    } packet_stats SEC(".maps");

This example shows how to create or update hash values using atomic
instructions:

.. code-block:: c

    static void update_stats(__u32 srcip, int bytes)
    {
            struct key key = {
                    .srcip = srcip,
            };
            struct value *value = bpf_map_lookup_elem(&packet_stats, &key);

            if (value) {
                    __sync_fetch_and_add(&value->packets, 1);
                    __sync_fetch_and_add(&value->bytes, bytes);
            } else {
                    struct value newval = { 1, bytes };

                    bpf_map_update_elem(&packet_stats, &key, &newval, BPF_NOEXIST);
            }
    }

Userspace walking the map elements from the map declared above:

.. code-block:: c

    #include <bpf/libbpf.h>
    #include <bpf/bpf.h>

    static void walk_hash_elements(int map_fd)
    {
            struct key *cur_key = NULL;
            struct key next_key;
            struct value value;
            int err;

            for (;;) {
                    err = bpf_map_get_next_key(map_fd, cur_key, &next_key);
                    if (err)
                            break;

                    bpf_map_lookup_elem(map_fd, &next_key, &value);

                    // Use key and value here

                    cur_key = &next_key;
            }
    }

Internals
=========

This section of the document is targeted at Linux developers and describes
aspects of the map implementations that are not considered stable ABI. The
following details are subject to change in future versions of the kernel.

``BPF_MAP_TYPE_LRU_HASH`` and variants
--------------------------------------

Updating elements in LRU maps may trigger eviction behaviour when the capacity
of the map is reached. There are various steps that the update algorithm
attempts in order to enforce the LRU property which have increasing impacts on
other CPUs involved in the following operation attempts:

- Attempt to use CPU-local state to batch operations
- Attempt to fetch free nodes from global lists
- Attempt to pull any node from a global list and remove it from the hashmap
- Attempt to pull any node from any CPU's list and remove it from the hashmap

This algorithm is described visually in the following diagram. See the
description in commit 3a08c2fd7634 ("bpf: LRU List") for a full explanation of
the corresponding operations:

.. kernel-figure::  map_lru_hash_update.dot
   :alt:    Diagram outlining the LRU eviction steps taken during map update.

   LRU hash eviction during map update for ``BPF_MAP_TYPE_LRU_HASH`` and
   variants. See the dot file source for kernel function name code references.

Map updates start from the oval in the top right "begin ``bpf_map_update()``"
and progress through the graph towards the bottom where the result may be
either a successful update or a failure with various error codes. The key in
the top right provides indicators for which locks may be involved in specific
operations. This is intended as a visual hint for reasoning about how map
contention may impact update operations, though the map type and flags may
impact the actual contention on those locks, based on the logic described in
the table above. For instance, if the map is created with type
``BPF_MAP_TYPE_LRU_PERCPU_HASH`` and flags ``BPF_F_NO_COMMON_LRU`` then all map
properties would be per-cpu.
