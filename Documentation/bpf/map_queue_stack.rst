.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

=========================================
BPF_MAP_TYPE_QUEUE and BPF_MAP_TYPE_STACK
=========================================

.. note::
   - ``BPF_MAP_TYPE_QUEUE`` and ``BPF_MAP_TYPE_STACK`` were introduced
     in kernel version 4.20

``BPF_MAP_TYPE_QUEUE`` provides FIFO storage and ``BPF_MAP_TYPE_STACK``
provides LIFO storage for BPF programs. These maps support peek, pop and
push operations that are exposed to BPF programs through the respective
helpers. These operations are exposed to userspace applications using
the existing ``bpf`` syscall in the following way:

- ``BPF_MAP_LOOKUP_ELEM`` -> peek
- ``BPF_MAP_LOOKUP_AND_DELETE_ELEM`` -> pop
- ``BPF_MAP_UPDATE_ELEM`` -> push

``BPF_MAP_TYPE_QUEUE`` and ``BPF_MAP_TYPE_STACK`` do not support
``BPF_F_NO_PREALLOC``.

Usage
=====

Kernel BPF
----------

bpf_map_push_elem()
~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_push_elem(struct bpf_map *map, const void *value, u64 flags)

An element ``value`` can be added to a queue or stack using the
``bpf_map_push_elem`` helper. The ``flags`` parameter must be set to
``BPF_ANY`` or ``BPF_EXIST``. If ``flags`` is set to ``BPF_EXIST`` then,
when the queue or stack is full, the oldest element will be removed to
make room for ``value`` to be added. Returns ``0`` on success, or
negative error in case of failure.

bpf_map_peek_elem()
~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_peek_elem(struct bpf_map *map, void *value)

This helper fetches an element ``value`` from a queue or stack without
removing it. Returns ``0`` on success, or negative error in case of
failure.

bpf_map_pop_elem()
~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_pop_elem(struct bpf_map *map, void *value)

This helper removes an element into ``value`` from a queue or
stack. Returns ``0`` on success, or negative error in case of failure.


Userspace
---------

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_update_elem (int fd, const void *key, const void *value, __u64 flags)

A userspace program can push ``value`` onto a queue or stack using libbpf's
``bpf_map_update_elem`` function. The ``key`` parameter must be set to
``NULL`` and ``flags`` must be set to ``BPF_ANY`` or ``BPF_EXIST``, with the
same semantics as the ``bpf_map_push_elem`` kernel helper. Returns ``0`` on
success, or negative error in case of failure.

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_lookup_elem (int fd, const void *key, void *value)

A userspace program can peek at the ``value`` at the head of a queue or stack
using the libbpf ``bpf_map_lookup_elem`` function. The ``key`` parameter must be
set to ``NULL``.  Returns ``0`` on success, or negative error in case of
failure.

bpf_map_lookup_and_delete_elem()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_lookup_and_delete_elem (int fd, const void *key, void *value)

A userspace program can pop a ``value`` from the head of a queue or stack using
the libbpf ``bpf_map_lookup_and_delete_elem`` function. The ``key`` parameter
must be set to ``NULL``. Returns ``0`` on success, or negative error in case of
failure.

Examples
========

Kernel BPF
----------

This snippet shows how to declare a queue in a BPF program:

.. code-block:: c

    struct {
            __uint(type, BPF_MAP_TYPE_QUEUE);
            __type(value, __u32);
            __uint(max_entries, 10);
    } queue SEC(".maps");


Userspace
---------

This snippet shows how to use libbpf's low-level API to create a queue from
userspace:

.. code-block:: c

    int create_queue()
    {
            return bpf_map_create(BPF_MAP_TYPE_QUEUE,
                                  "sample_queue", /* name */
                                  0,              /* key size, must be zero */
                                  sizeof(__u32),  /* value size */
                                  10,             /* max entries */
                                  NULL);          /* create options */
    }


References
==========

https://lwn.net/ml/netdev/153986858555.9127.14517764371945179514.stgit@kernel/
