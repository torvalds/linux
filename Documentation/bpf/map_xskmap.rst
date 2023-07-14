.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

===================
BPF_MAP_TYPE_XSKMAP
===================

.. note::
   - ``BPF_MAP_TYPE_XSKMAP`` was introduced in kernel version 4.18

The ``BPF_MAP_TYPE_XSKMAP`` is used as a backend map for XDP BPF helper
call ``bpf_redirect_map()`` and ``XDP_REDIRECT`` action, like 'devmap' and 'cpumap'.
This map type redirects raw XDP frames to `AF_XDP`_ sockets (XSKs), a new type of
address family in the kernel that allows redirection of frames from a driver to
user space without having to traverse the full network stack. An AF_XDP socket
binds to a single netdev queue. A mapping of XSKs to queues is shown below:

.. code-block:: none

    +---------------------------------------------------+
    |     xsk A      |     xsk B       |      xsk C     |<---+ User space
    =========================================================|==========
    |    Queue 0     |     Queue 1     |     Queue 2    |    |  Kernel
    +---------------------------------------------------+    |
    |                  Netdev eth0                      |    |
    +---------------------------------------------------+    |
    |                            +=============+        |    |
    |                            | key |  xsk  |        |    |
    |  +---------+               +=============+        |    |
    |  |         |               |  0  | xsk A |        |    |
    |  |         |               +-------------+        |    |
    |  |         |               |  1  | xsk B |        |    |
    |  | BPF     |-- redirect -->+-------------+-------------+
    |  | prog    |               |  2  | xsk C |        |
    |  |         |               +-------------+        |
    |  |         |                                      |
    |  |         |                                      |
    |  +---------+                                      |
    |                                                   |
    +---------------------------------------------------+

.. note::
    An AF_XDP socket that is bound to a certain <netdev/queue_id> will *only*
    accept XDP frames from that <netdev/queue_id>. If an XDP program tries to redirect
    from a <netdev/queue_id> other than what the socket is bound to, the frame will
    not be received on the socket.

Typically an XSKMAP is created per netdev. This map contains an array of XSK File
Descriptors (FDs). The number of array elements is typically set or adjusted using
the ``max_entries`` map parameter. For AF_XDP ``max_entries`` is equal to the number
of queues supported by the netdev.

.. note::
    Both the map key and map value size must be 4 bytes.

Usage
=====

Kernel BPF
----------
bpf_redirect_map()
^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_redirect_map(struct bpf_map *map, u32 key, u64 flags)

Redirect the packet to the endpoint referenced by ``map`` at index ``key``.
For ``BPF_MAP_TYPE_XSKMAP`` this map contains references to XSK FDs
for sockets attached to a netdev's queues.

.. note::
    If the map is empty at an index, the packet is dropped. This means that it is
    necessary to have an XDP program loaded with at least one XSK in the
    XSKMAP to be able to get any traffic to user space through the socket.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

XSK entry references of type ``struct xdp_sock *`` can be retrieved using the
``bpf_map_lookup_elem()`` helper.

User space
----------
.. note::
    XSK entries can only be updated/deleted from user space and not from
    a BPF program. Trying to call these functions from a kernel BPF program will
    result in the program failing to load and a verifier warning.

bpf_map_update_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

	int bpf_map_update_elem(int fd, const void *key, const void *value, __u64 flags)

XSK entries can be added or updated using the ``bpf_map_update_elem()``
helper. The ``key`` parameter is equal to the queue_id of the queue the XSK
is attaching to. And the ``value`` parameter is the FD value of that socket.

Under the hood, the XSKMAP update function uses the XSK FD value to retrieve the
associated ``struct xdp_sock`` instance.

The flags argument can be one of the following:

- BPF_ANY: Create a new element or update an existing element.
- BPF_NOEXIST: Create a new element only if it did not exist.
- BPF_EXIST: Update an existing element.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_lookup_elem(int fd, const void *key, void *value)

Returns ``struct xdp_sock *`` or negative error in case of failure.

bpf_map_delete_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_delete_elem(int fd, const void *key)

XSK entries can be deleted using the ``bpf_map_delete_elem()``
helper. This helper will return 0 on success, or negative error in case of
failure.

.. note::
    When `libxdp`_ deletes an XSK it also removes the associated socket
    entry from the XSKMAP.

Examples
========
Kernel
------

The following code snippet shows how to declare a ``BPF_MAP_TYPE_XSKMAP`` called
``xsks_map`` and how to redirect packets to an XSK.

.. code-block:: c

	struct {
		__uint(type, BPF_MAP_TYPE_XSKMAP);
		__type(key, __u32);
		__type(value, __u32);
		__uint(max_entries, 64);
	} xsks_map SEC(".maps");


	SEC("xdp")
	int xsk_redir_prog(struct xdp_md *ctx)
	{
		__u32 index = ctx->rx_queue_index;

		if (bpf_map_lookup_elem(&xsks_map, &index))
			return bpf_redirect_map(&xsks_map, index, 0);
		return XDP_PASS;
	}

User space
----------

The following code snippet shows how to update an XSKMAP with an XSK entry.

.. code-block:: c

	int update_xsks_map(struct bpf_map *xsks_map, int queue_id, int xsk_fd)
	{
		int ret;

		ret = bpf_map_update_elem(bpf_map__fd(xsks_map), &queue_id, &xsk_fd, 0);
		if (ret < 0)
			fprintf(stderr, "Failed to update xsks_map: %s\n", strerror(errno));

		return ret;
	}

For an example on how create AF_XDP sockets, please see the AF_XDP-example and
AF_XDP-forwarding programs in the `bpf-examples`_ directory in the `libxdp`_ repository.
For a detailed explanation of the AF_XDP interface please see:

- `libxdp-readme`_.
- `AF_XDP`_ kernel documentation.

.. note::
    The most comprehensive resource for using XSKMAPs and AF_XDP is `libxdp`_.

.. _libxdp: https://github.com/xdp-project/xdp-tools/tree/master/lib/libxdp
.. _AF_XDP: https://www.kernel.org/doc/html/latest/networking/af_xdp.html
.. _bpf-examples: https://github.com/xdp-project/bpf-examples
.. _libxdp-readme: https://github.com/xdp-project/xdp-tools/tree/master/lib/libxdp#using-af_xdp-sockets
