.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

=======================
BPF_MAP_TYPE_SK_STORAGE
=======================

.. note::
   - ``BPF_MAP_TYPE_SK_STORAGE`` was introduced in kernel version 5.2

``BPF_MAP_TYPE_SK_STORAGE`` is used to provide socket-local storage for BPF
programs. A map of type ``BPF_MAP_TYPE_SK_STORAGE`` declares the type of storage
to be provided and acts as the handle for accessing the socket-local
storage. The values for maps of type ``BPF_MAP_TYPE_SK_STORAGE`` are stored
locally with each socket instead of with the map. The kernel is responsible for
allocating storage for a socket when requested and for freeing the storage when
either the map or the socket is deleted.

.. note::
  - The key type must be ``int`` and ``max_entries`` must be set to ``0``.
  - The ``BPF_F_NO_PREALLOC`` flag must be used when creating a map for
    socket-local storage.

Usage
=====

Kernel BPF
----------

bpf_sk_storage_get()
~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   void *bpf_sk_storage_get(struct bpf_map *map, void *sk, void *value, u64 flags)

Socket-local storage for ``map`` can be retrieved from socket ``sk`` using the
``bpf_sk_storage_get()`` helper. If the ``BPF_LOCAL_STORAGE_GET_F_CREATE``
flag is used then ``bpf_sk_storage_get()`` will create the storage for ``sk``
if it does not already exist. ``value`` can be used together with
``BPF_LOCAL_STORAGE_GET_F_CREATE`` to initialize the storage value, otherwise
it will be zero initialized. Returns a pointer to the storage on success, or
``NULL`` in case of failure.

.. note::
   - ``sk`` is a kernel ``struct sock`` pointer for LSM or tracing programs.
   - ``sk`` is a ``struct bpf_sock`` pointer for other program types.

bpf_sk_storage_delete()
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   long bpf_sk_storage_delete(struct bpf_map *map, void *sk)

Socket-local storage for ``map`` can be deleted from socket ``sk`` using the
``bpf_sk_storage_delete()`` helper. Returns ``0`` on success, or negative
error in case of failure.

User space
----------

bpf_map_update_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_update_elem(int map_fd, const void *key, const void *value, __u64 flags)

Socket-local storage for map ``map_fd`` can be added or updated locally to a
socket using the ``bpf_map_update_elem()`` libbpf function. The socket is
identified by a `socket` ``fd`` stored in the pointer ``key``. The pointer
``value`` has the data to be added or updated to the socket ``fd``. The type
and size of ``value`` should be the same as the value type of the map
definition.

The ``flags`` parameter can be used to control the update behaviour:

- ``BPF_ANY`` will create storage for `socket` ``fd`` or update existing storage.
- ``BPF_NOEXIST`` will create storage for `socket` ``fd`` only if it did not
  already exist, otherwise the call will fail with ``-EEXIST``.
- ``BPF_EXIST`` will update existing storage for `socket` ``fd`` if it already
  exists, otherwise the call will fail with ``-ENOENT``.

Returns ``0`` on success, or negative error in case of failure.

bpf_map_lookup_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_lookup_elem(int map_fd, const void *key, void *value)

Socket-local storage for map ``map_fd`` can be retrieved from a socket using
the ``bpf_map_lookup_elem()`` libbpf function. The storage is retrieved from
the socket identified by a `socket` ``fd`` stored in the pointer
``key``. Returns ``0`` on success, or negative error in case of failure.

bpf_map_delete_elem()
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

   int bpf_map_delete_elem(int map_fd, const void *key)

Socket-local storage for map ``map_fd`` can be deleted from a socket using the
``bpf_map_delete_elem()`` libbpf function. The storage is deleted from the
socket identified by a `socket` ``fd`` stored in the pointer ``key``. Returns
``0`` on success, or negative error in case of failure.

Examples
========

Kernel BPF
----------

This snippet shows how to declare socket-local storage in a BPF program:

.. code-block:: c

    struct {
            __uint(type, BPF_MAP_TYPE_SK_STORAGE);
            __uint(map_flags, BPF_F_NO_PREALLOC);
            __type(key, int);
            __type(value, struct my_storage);
    } socket_storage SEC(".maps");

This snippet shows how to retrieve socket-local storage in a BPF program:

.. code-block:: c

    SEC("sockops")
    int _sockops(struct bpf_sock_ops *ctx)
    {
            struct my_storage *storage;
            struct bpf_sock *sk;

            sk = ctx->sk;
            if (!sk)
                    return 1;

            storage = bpf_sk_storage_get(&socket_storage, sk, 0,
                                         BPF_LOCAL_STORAGE_GET_F_CREATE);
            if (!storage)
                    return 1;

            /* Use 'storage' here */

            return 1;
    }


Please see the ``tools/testing/selftests/bpf`` directory for functional
examples.

References
==========

https://lwn.net/ml/netdev/20190426171103.61892-1-kafai@fb.com/
