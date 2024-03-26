.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

=====================
BPF_MAP_TYPE_LPM_TRIE
=====================

.. note::
   - ``BPF_MAP_TYPE_LPM_TRIE`` was introduced in kernel version 4.11

``BPF_MAP_TYPE_LPM_TRIE`` provides a longest prefix match algorithm that
can be used to match IP addresses to a stored set of prefixes.
Internally, data is stored in an unbalanced trie of nodes that uses
``prefixlen,data`` pairs as its keys. The ``data`` is interpreted in
network byte order, i.e. big endian, so ``data[0]`` stores the most
significant byte.

LPM tries may be created with a maximum prefix length that is a multiple
of 8, in the range from 8 to 2048. The key used for lookup and update
operations is a ``struct bpf_lpm_trie_key_u8``, extended by
``max_prefixlen/8`` bytes.

- For IPv4 addresses the data length is 4 bytes
- For IPv6 addresses the data length is 16 bytes

The value type stored in the LPM trie can be any user defined type.

.. note::
   When creating a map of type ``BPF_MAP_TYPE_LPM_TRIE`` you must set the
   ``BPF_F_NO_PREALLOC`` flag.

Usage
=====

Kernel BPF
----------

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

The longest prefix entry for a given data value can be found using the
``bpf_map_lookup_elem()`` helper. This helper returns a pointer to the
value associated with the longest matching ``key``, or ``NULL`` if no
entry was found.

The ``key`` should have ``prefixlen`` set to ``max_prefixlen`` when
performing longest prefix lookups. For example, when searching for the
longest prefix match for an IPv4 address, ``prefixlen`` should be set to
``32``.

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, u64 flags)

Prefix entries can be added or updated using the ``bpf_map_update_elem()``
helper. This helper replaces existing elements atomically.

``bpf_map_update_elem()`` returns ``0`` on success, or negative error in
case of failure.

 .. note::
    The flags parameter must be one of BPF_ANY, BPF_NOEXIST or BPF_EXIST,
    but the value is ignored, giving BPF_ANY semantics.

bpf_map_delete_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_map_delete_elem(struct bpf_map *map, const void *key)

Prefix entries can be deleted using the ``bpf_map_delete_elem()``
helper. This helper will return 0 on success, or negative error in case
of failure.

Userspace
---------

Access from userspace uses libbpf APIs with the same names as above, with
the map identified by ``fd``.

bpf_map_get_next_key()
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_get_next_key (int fd, const void *cur_key, void *next_key)

A userspace program can iterate through the entries in an LPM trie using
libbpf's ``bpf_map_get_next_key()`` function. The first key can be
fetched by calling ``bpf_map_get_next_key()`` with ``cur_key`` set to
``NULL``. Subsequent calls will fetch the next key that follows the
current key. ``bpf_map_get_next_key()`` returns ``0`` on success,
``-ENOENT`` if ``cur_key`` is the last key in the trie, or negative
error in case of failure.

``bpf_map_get_next_key()`` will iterate through the LPM trie elements
from leftmost leaf first. This means that iteration will return more
specific keys before less specific ones.

Examples
========

Please see ``tools/testing/selftests/bpf/test_lpm_map.c`` for examples
of LPM trie usage from userspace. The code snippets below demonstrate
API usage.

Kernel BPF
----------

The following BPF code snippet shows how to declare a new LPM trie for IPv4
address prefixes:

.. code-block:: c

    #include <linux/bpf.h>
    #include <bpf/bpf_helpers.h>

    struct ipv4_lpm_key {
            __u32 prefixlen;
            __u32 data;
    };

    struct {
            __uint(type, BPF_MAP_TYPE_LPM_TRIE);
            __type(key, struct ipv4_lpm_key);
            __type(value, __u32);
            __uint(map_flags, BPF_F_NO_PREALLOC);
            __uint(max_entries, 255);
    } ipv4_lpm_map SEC(".maps");

The following BPF code snippet shows how to lookup by IPv4 address:

.. code-block:: c

    void *lookup(__u32 ipaddr)
    {
            struct ipv4_lpm_key key = {
                    .prefixlen = 32,
                    .data = ipaddr
            };

            return bpf_map_lookup_elem(&ipv4_lpm_map, &key);
    }

Userspace
---------

The following snippet shows how to insert an IPv4 prefix entry into an
LPM trie:

.. code-block:: c

    int add_prefix_entry(int lpm_fd, __u32 addr, __u32 prefixlen, struct value *value)
    {
            struct ipv4_lpm_key ipv4_key = {
                    .prefixlen = prefixlen,
                    .data = addr
            };
            return bpf_map_update_elem(lpm_fd, &ipv4_key, value, BPF_ANY);
    }

The following snippet shows a userspace program walking through the entries
of an LPM trie:


.. code-block:: c

    #include <bpf/libbpf.h>
    #include <bpf/bpf.h>

    void iterate_lpm_trie(int map_fd)
    {
            struct ipv4_lpm_key *cur_key = NULL;
            struct ipv4_lpm_key next_key;
            struct value value;
            int err;

            for (;;) {
                    err = bpf_map_get_next_key(map_fd, cur_key, &next_key);
                    if (err)
                            break;

                    bpf_map_lookup_elem(map_fd, &next_key, &value);

                    /* Use key and value here */

                    cur_key = &next_key;
            }
    }
