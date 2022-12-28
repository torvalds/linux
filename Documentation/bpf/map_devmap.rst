.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

=================================================
BPF_MAP_TYPE_DEVMAP and BPF_MAP_TYPE_DEVMAP_HASH
=================================================

.. note::
   - ``BPF_MAP_TYPE_DEVMAP`` was introduced in kernel version 4.14
   - ``BPF_MAP_TYPE_DEVMAP_HASH`` was introduced in kernel version 5.4

``BPF_MAP_TYPE_DEVMAP`` and ``BPF_MAP_TYPE_DEVMAP_HASH`` are BPF maps primarily
used as backend maps for the XDP BPF helper call ``bpf_redirect_map()``.
``BPF_MAP_TYPE_DEVMAP`` is backed by an array that uses the key as
the index to lookup a reference to a net device. While ``BPF_MAP_TYPE_DEVMAP_HASH``
is backed by a hash table that uses a key to lookup a reference to a net device.
The user provides either <``key``/ ``ifindex``> or <``key``/ ``struct bpf_devmap_val``>
pairs to update the maps with new net devices.

.. note::
    - The key to a hash map doesn't have to be an ``ifindex``.
    - While ``BPF_MAP_TYPE_DEVMAP_HASH`` allows for densely packing the net devices
      it comes at the cost of a hash of the key when performing a look up.

The setup and packet enqueue/send code is shared between the two types of
devmap; only the lookup and insertion is different.

Usage
=====
Kernel BPF
----------
bpf_redirect_map()
^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_redirect_map(struct bpf_map *map, u32 key, u64 flags)

Redirect the packet to the endpoint referenced by ``map`` at index ``key``.
For ``BPF_MAP_TYPE_DEVMAP`` and ``BPF_MAP_TYPE_DEVMAP_HASH`` this map contains
references to net devices (for forwarding packets through other ports).

The lower two bits of *flags* are used as the return code if the map lookup
fails. This is so that the return value can be one of the XDP program return
codes up to ``XDP_TX``, as chosen by the caller. The higher bits of ``flags``
can be set to ``BPF_F_BROADCAST`` or ``BPF_F_EXCLUDE_INGRESS`` as defined
below.

With ``BPF_F_BROADCAST`` the packet will be broadcast to all the interfaces
in the map, with ``BPF_F_EXCLUDE_INGRESS`` the ingress interface will be excluded
from the broadcast.

.. note::
    - The key is ignored if BPF_F_BROADCAST is set.
    - The broadcast feature can also be used to implement multicast forwarding:
      simply create multiple DEVMAPs, each one corresponding to a single multicast group.

This helper will return ``XDP_REDIRECT`` on success, or the value of the two
lower bits of the ``flags`` argument if the map lookup fails.

More information about redirection can be found :doc:`redirect`

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

   void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

Net device entries can be retrieved using the ``bpf_map_lookup_elem()``
helper.

User space
----------
.. note::
    DEVMAP entries can only be updated/deleted from user space and not
    from an eBPF program. Trying to call these functions from a kernel eBPF
    program will result in the program failing to load and a verifier warning.

bpf_map_update_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

   int bpf_map_update_elem(int fd, const void *key, const void *value, __u64 flags);

Net device entries can be added or updated using the ``bpf_map_update_elem()``
helper. This helper replaces existing elements atomically. The ``value`` parameter
can be ``struct bpf_devmap_val`` or a simple ``int ifindex`` for backwards
compatibility.

 .. code-block:: c

    struct bpf_devmap_val {
        __u32 ifindex;   /* device index */
        union {
            int   fd;  /* prog fd on map write */
            __u32 id;  /* prog id on map read */
        } bpf_prog;
    };

The ``flags`` argument can be one of the following:
  - ``BPF_ANY``: Create a new element or update an existing element.
  - ``BPF_NOEXIST``: Create a new element only if it did not exist.
  - ``BPF_EXIST``: Update an existing element.

DEVMAPs can associate a program with a device entry by adding a ``bpf_prog.fd``
to ``struct bpf_devmap_val``. Programs are run after ``XDP_REDIRECT`` and have
access to both Rx device and Tx device. The  program associated with the ``fd``
must have type XDP with expected attach type ``xdp_devmap``.
When a program is associated with a device index, the program is run on an
``XDP_REDIRECT`` and before the buffer is added to the per-cpu queue. Examples
of how to attach/use xdp_devmap progs can be found in the kernel selftests:

- ``tools/testing/selftests/bpf/prog_tests/xdp_devmap_attach.c``
- ``tools/testing/selftests/bpf/progs/test_xdp_with_devmap_helpers.c``

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

.. c:function::
   int bpf_map_lookup_elem(int fd, const void *key, void *value);

Net device entries can be retrieved using the ``bpf_map_lookup_elem()``
helper.

bpf_map_delete_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

.. c:function::
   int bpf_map_delete_elem(int fd, const void *key);

Net device entries can be deleted using the ``bpf_map_delete_elem()``
helper. This helper will return 0 on success, or negative error in case of
failure.

Examples
========

Kernel BPF
----------

The following code snippet shows how to declare a ``BPF_MAP_TYPE_DEVMAP``
called tx_port.

.. code-block:: c

    struct {
        __uint(type, BPF_MAP_TYPE_DEVMAP);
        __type(key, __u32);
        __type(value, __u32);
        __uint(max_entries, 256);
    } tx_port SEC(".maps");

The following code snippet shows how to declare a ``BPF_MAP_TYPE_DEVMAP_HASH``
called forward_map.

.. code-block:: c

    struct {
        __uint(type, BPF_MAP_TYPE_DEVMAP_HASH);
        __type(key, __u32);
        __type(value, struct bpf_devmap_val);
        __uint(max_entries, 32);
    } forward_map SEC(".maps");

.. note::

    The value type in the DEVMAP above is a ``struct bpf_devmap_val``

The following code snippet shows a simple xdp_redirect_map program. This program
would work with a user space program that populates the devmap ``forward_map`` based
on ingress ifindexes. The BPF program (below) is redirecting packets using the
ingress ``ifindex`` as the ``key``.

.. code-block:: c

    SEC("xdp")
    int xdp_redirect_map_func(struct xdp_md *ctx)
    {
        int index = ctx->ingress_ifindex;

        return bpf_redirect_map(&forward_map, index, 0);
    }

The following code snippet shows a BPF program that is broadcasting packets to
all the interfaces in the ``tx_port`` devmap.

.. code-block:: c

    SEC("xdp")
    int xdp_redirect_map_func(struct xdp_md *ctx)
    {
        return bpf_redirect_map(&tx_port, 0, BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
    }

User space
----------

The following code snippet shows how to update a devmap called ``tx_port``.

.. code-block:: c

    int update_devmap(int ifindex, int redirect_ifindex)
    {
        int ret;

        ret = bpf_map_update_elem(bpf_map__fd(tx_port), &ifindex, &redirect_ifindex, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to update devmap_ value: %s\n",
                strerror(errno));
        }

        return ret;
    }

The following code snippet shows how to update a hash_devmap called ``forward_map``.

.. code-block:: c

    int update_devmap(int ifindex, int redirect_ifindex)
    {
        struct bpf_devmap_val devmap_val = { .ifindex = redirect_ifindex };
        int ret;

        ret = bpf_map_update_elem(bpf_map__fd(forward_map), &ifindex, &devmap_val, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to update devmap_ value: %s\n",
                strerror(errno));
        }
        return ret;
    }

References
===========

- https://lwn.net/Articles/728146/
- https://git.kernel.org/pub/scm/linux/kernel/git/bpf/bpf-next.git/commit/?id=6f9d451ab1a33728adb72d7ff66a7b374d665176
- https://elixir.bootlin.com/linux/latest/source/net/core/filter.c#L4106
