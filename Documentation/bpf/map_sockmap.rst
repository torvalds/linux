.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright Red Hat

==============================================
BPF_MAP_TYPE_SOCKMAP and BPF_MAP_TYPE_SOCKHASH
==============================================

.. note::
   - ``BPF_MAP_TYPE_SOCKMAP`` was introduced in kernel version 4.14
   - ``BPF_MAP_TYPE_SOCKHASH`` was introduced in kernel version 4.18

``BPF_MAP_TYPE_SOCKMAP`` and ``BPF_MAP_TYPE_SOCKHASH`` maps can be used to
redirect skbs between sockets or to apply policy at the socket level based on
the result of a BPF (verdict) program with the help of the BPF helpers
``bpf_sk_redirect_map()``, ``bpf_sk_redirect_hash()``,
``bpf_msg_redirect_map()`` and ``bpf_msg_redirect_hash()``.

``BPF_MAP_TYPE_SOCKMAP`` is backed by an array that uses an integer key as the
index to look up a reference to a ``struct sock``. The map values are socket
descriptors. Similarly, ``BPF_MAP_TYPE_SOCKHASH`` is a hash backed BPF map that
holds references to sockets via their socket descriptors.

.. note::
    The value type is either __u32 or __u64; the latter (__u64) is to support
    returning socket cookies to userspace. Returning the ``struct sock *`` that
    the map holds to user-space is neither safe nor useful.

These maps may have BPF programs attached to them, specifically a parser program
and a verdict program. The parser program determines how much data has been
parsed and therefore how much data needs to be queued to come to a verdict. The
verdict program is essentially the redirect program and can return a verdict
of ``__SK_DROP``, ``__SK_PASS``, or ``__SK_REDIRECT``.

When a socket is inserted into one of these maps, its socket callbacks are
replaced and a ``struct sk_psock`` is attached to it. Additionally, this
``sk_psock`` inherits the programs that are attached to the map.

A sock object may be in multiple maps, but can only inherit a single
parse or verdict program. If adding a sock object to a map would result
in having multiple parser programs the update will return an EBUSY error.

The supported programs to attach to these maps are:

.. code-block:: c

	struct sk_psock_progs {
		struct bpf_prog *msg_parser;
		struct bpf_prog *stream_parser;
		struct bpf_prog *stream_verdict;
		struct bpf_prog	*skb_verdict;
	};

.. note::
    Users are not allowed to attach ``stream_verdict`` and ``skb_verdict``
    programs to the same map.

The attach types for the map programs are:

- ``msg_parser`` program - ``BPF_SK_MSG_VERDICT``.
- ``stream_parser`` program - ``BPF_SK_SKB_STREAM_PARSER``.
- ``stream_verdict`` program - ``BPF_SK_SKB_STREAM_VERDICT``.
- ``skb_verdict`` program - ``BPF_SK_SKB_VERDICT``.

There are additional helpers available to use with the parser and verdict
programs: ``bpf_msg_apply_bytes()`` and ``bpf_msg_cork_bytes()``. With
``bpf_msg_apply_bytes()`` BPF programs can tell the infrastructure how many
bytes the given verdict should apply to. The helper ``bpf_msg_cork_bytes()``
handles a different case where a BPF program cannot reach a verdict on a msg
until it receives more bytes AND the program doesn't want to forward the packet
until it is known to be good.

Finally, the helpers ``bpf_msg_pull_data()`` and ``bpf_msg_push_data()`` are
available to ``BPF_PROG_TYPE_SK_MSG`` BPF programs to pull in data and set the
start and end pointers to given values or to add metadata to the ``struct
sk_msg_buff *msg``.

All these helpers will be described in more detail below.

Usage
=====
Kernel BPF
----------
bpf_msg_redirect_map()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

	long bpf_msg_redirect_map(struct sk_msg_buff *msg, struct bpf_map *map, u32 key, u64 flags)

This helper is used in programs implementing policies at the socket level. If
the message ``msg`` is allowed to pass (i.e., if the verdict BPF program
returns ``SK_PASS``), redirect it to the socket referenced by ``map`` (of type
``BPF_MAP_TYPE_SOCKMAP``) at index ``key``. Both ingress and egress interfaces
can be used for redirection. The ``BPF_F_INGRESS`` value in ``flags`` is used
to select the ingress path otherwise the egress path is selected. This is the
only flag supported for now.

Returns ``SK_PASS`` on success, or ``SK_DROP`` on error.

bpf_sk_redirect_map()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_sk_redirect_map(struct sk_buff *skb, struct bpf_map *map, u32 key u64 flags)

Redirect the packet to the socket referenced by ``map`` (of type
``BPF_MAP_TYPE_SOCKMAP``) at index ``key``. Both ingress and egress interfaces
can be used for redirection. The ``BPF_F_INGRESS`` value in ``flags`` is used
to select the ingress path otherwise the egress path is selected. This is the
only flag supported for now.

Returns ``SK_PASS`` on success, or ``SK_DROP`` on error.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

socket entries of type ``struct sock *`` can be retrieved using the
``bpf_map_lookup_elem()`` helper.

bpf_sock_map_update()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_sock_map_update(struct bpf_sock_ops *skops, struct bpf_map *map, void *key, u64 flags)

Add an entry to, or update a ``map`` referencing sockets. The ``skops`` is used
as a new value for the entry associated to ``key``. The ``flags`` argument can
be one of the following:

- ``BPF_ANY``: Create a new element or update an existing element.
- ``BPF_NOEXIST``: Create a new element only if it did not exist.
- ``BPF_EXIST``: Update an existing element.

If the ``map`` has BPF programs (parser and verdict), those will be inherited
by the socket being added. If the socket is already attached to BPF programs,
this results in an error.

Returns 0 on success, or a negative error in case of failure.

bpf_sock_hash_update()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_sock_hash_update(struct bpf_sock_ops *skops, struct bpf_map *map, void *key, u64 flags)

Add an entry to, or update a sockhash ``map`` referencing sockets. The ``skops``
is used as a new value for the entry associated to ``key``.

The ``flags`` argument can be one of the following:

- ``BPF_ANY``: Create a new element or update an existing element.
- ``BPF_NOEXIST``: Create a new element only if it did not exist.
- ``BPF_EXIST``: Update an existing element.

If the ``map`` has BPF programs (parser and verdict), those will be inherited
by the socket being added. If the socket is already attached to BPF programs,
this results in an error.

Returns 0 on success, or a negative error in case of failure.

bpf_msg_redirect_hash()
^^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_msg_redirect_hash(struct sk_msg_buff *msg, struct bpf_map *map, void *key, u64 flags)

This helper is used in programs implementing policies at the socket level. If
the message ``msg`` is allowed to pass (i.e., if the verdict BPF program returns
``SK_PASS``), redirect it to the socket referenced by ``map`` (of type
``BPF_MAP_TYPE_SOCKHASH``) using hash ``key``. Both ingress and egress
interfaces can be used for redirection. The ``BPF_F_INGRESS`` value in
``flags`` is used to select the ingress path otherwise the egress path is
selected. This is the only flag supported for now.

Returns ``SK_PASS`` on success, or ``SK_DROP`` on error.

bpf_sk_redirect_hash()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_sk_redirect_hash(struct sk_buff *skb, struct bpf_map *map, void *key, u64 flags)

This helper is used in programs implementing policies at the skb socket level.
If the sk_buff ``skb`` is allowed to pass (i.e., if the verdict BPF program
returns ``SK_PASS``), redirect it to the socket referenced by ``map`` (of type
``BPF_MAP_TYPE_SOCKHASH``) using hash ``key``. Both ingress and egress
interfaces can be used for redirection. The ``BPF_F_INGRESS`` value in
``flags`` is used to select the ingress path otherwise the egress path is
selected. This is the only flag supported for now.

Returns ``SK_PASS`` on success, or ``SK_DROP`` on error.

bpf_msg_apply_bytes()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_msg_apply_bytes(struct sk_msg_buff *msg, u32 bytes)

For socket policies, apply the verdict of the BPF program to the next (number
of ``bytes``) of message ``msg``. For example, this helper can be used in the
following cases:

- A single ``sendmsg()`` or ``sendfile()`` system call contains multiple
  logical messages that the BPF program is supposed to read and for which it
  should apply a verdict.
- A BPF program only cares to read the first ``bytes`` of a ``msg``. If the
  message has a large payload, then setting up and calling the BPF program
  repeatedly for all bytes, even though the verdict is already known, would
  create unnecessary overhead.

Returns 0

bpf_msg_cork_bytes()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_msg_cork_bytes(struct sk_msg_buff *msg, u32 bytes)

For socket policies, prevent the execution of the verdict BPF program for
message ``msg`` until the number of ``bytes`` have been accumulated.

This can be used when one needs a specific number of bytes before a verdict can
be assigned, even if the data spans multiple ``sendmsg()`` or ``sendfile()``
calls.

Returns 0

bpf_msg_pull_data()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_msg_pull_data(struct sk_msg_buff *msg, u32 start, u32 end, u64 flags)

For socket policies, pull in non-linear data from user space for ``msg`` and set
pointers ``msg->data`` and ``msg->data_end`` to ``start`` and ``end`` bytes
offsets into ``msg``, respectively.

If a program of type ``BPF_PROG_TYPE_SK_MSG`` is run on a ``msg`` it can only
parse data that the (``data``, ``data_end``) pointers have already consumed.
For ``sendmsg()`` hooks this is likely the first scatterlist element. But for
calls relying on the ``sendpage`` handler (e.g., ``sendfile()``) this will be
the range (**0**, **0**) because the data is shared with user space and by
default the objective is to avoid allowing user space to modify data while (or
after) BPF verdict is being decided. This helper can be used to pull in data
and to set the start and end pointers to given values. Data will be copied if
necessary (i.e., if data was not linear and if start and end pointers do not
point to the same chunk).

A call to this helper is susceptible to change the underlying packet buffer.
Therefore, at load time, all checks on pointers previously done by the verifier
are invalidated and must be performed again, if the helper is used in
combination with direct packet access.

All values for ``flags`` are reserved for future usage, and must be left at
zero.

Returns 0 on success, or a negative error in case of failure.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

	void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)

Look up a socket entry in the sockmap or sockhash map.

Returns the socket entry associated to ``key``, or NULL if no entry was found.

bpf_map_update_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

	long bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, u64 flags)

Add or update a socket entry in a sockmap or sockhash.

The flags argument can be one of the following:

- BPF_ANY: Create a new element or update an existing element.
- BPF_NOEXIST: Create a new element only if it did not exist.
- BPF_EXIST: Update an existing element.

Returns 0 on success, or a negative error in case of failure.

bpf_map_delete_elem()
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    long bpf_map_delete_elem(struct bpf_map *map, const void *key)

Delete a socket entry from a sockmap or a sockhash.

Returns	0 on success, or a negative error in case of failure.

User space
----------
bpf_map_update_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

	int bpf_map_update_elem(int fd, const void *key, const void *value, __u64 flags)

Sockmap entries can be added or updated using the ``bpf_map_update_elem()``
function. The ``key`` parameter is the index value of the sockmap array. And the
``value`` parameter is the FD value of that socket.

Under the hood, the sockmap update function uses the socket FD value to
retrieve the associated socket and its attached psock.

The flags argument can be one of the following:

- BPF_ANY: Create a new element or update an existing element.
- BPF_NOEXIST: Create a new element only if it did not exist.
- BPF_EXIST: Update an existing element.

bpf_map_lookup_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_lookup_elem(int fd, const void *key, void *value)

Sockmap entries can be retrieved using the ``bpf_map_lookup_elem()`` function.

.. note::
	The entry returned is a socket cookie rather than a socket itself.

bpf_map_delete_elem()
^^^^^^^^^^^^^^^^^^^^^
.. code-block:: c

    int bpf_map_delete_elem(int fd, const void *key)

Sockmap entries can be deleted using the ``bpf_map_delete_elem()``
function.

Returns 0 on success, or negative error in case of failure.

Examples
========

Kernel BPF
----------
Several examples of the use of sockmap APIs can be found in:

- `tools/testing/selftests/bpf/progs/test_sockmap_kern.h`_
- `tools/testing/selftests/bpf/progs/sockmap_parse_prog.c`_
- `tools/testing/selftests/bpf/progs/sockmap_verdict_prog.c`_
- `tools/testing/selftests/bpf/progs/test_sockmap_listen.c`_
- `tools/testing/selftests/bpf/progs/test_sockmap_update.c`_

The following code snippet shows how to declare a sockmap.

.. code-block:: c

	struct {
		__uint(type, BPF_MAP_TYPE_SOCKMAP);
		__uint(max_entries, 1);
		__type(key, __u32);
		__type(value, __u64);
	} sock_map_rx SEC(".maps");

The following code snippet shows a sample parser program.

.. code-block:: c

	SEC("sk_skb/stream_parser")
	int bpf_prog_parser(struct __sk_buff *skb)
	{
		return skb->len;
	}

The following code snippet shows a simple verdict program that interacts with a
sockmap to redirect traffic to another socket based on the local port.

.. code-block:: c

	SEC("sk_skb/stream_verdict")
	int bpf_prog_verdict(struct __sk_buff *skb)
	{
		__u32 lport = skb->local_port;
		__u32 idx = 0;

		if (lport == 10000)
			return bpf_sk_redirect_map(skb, &sock_map_rx, idx, 0);

		return SK_PASS;
	}

The following code snippet shows how to declare a sockhash map.

.. code-block:: c

	struct socket_key {
		__u32 src_ip;
		__u32 dst_ip;
		__u32 src_port;
		__u32 dst_port;
	};

	struct {
		__uint(type, BPF_MAP_TYPE_SOCKHASH);
		__uint(max_entries, 1);
		__type(key, struct socket_key);
		__type(value, __u64);
	} sock_hash_rx SEC(".maps");

The following code snippet shows a simple verdict program that interacts with a
sockhash to redirect traffic to another socket based on a hash of some of the
skb parameters.

.. code-block:: c

	static inline
	void extract_socket_key(struct __sk_buff *skb, struct socket_key *key)
	{
		key->src_ip = skb->remote_ip4;
		key->dst_ip = skb->local_ip4;
		key->src_port = skb->remote_port >> 16;
		key->dst_port = (bpf_htonl(skb->local_port)) >> 16;
	}

	SEC("sk_skb/stream_verdict")
	int bpf_prog_verdict(struct __sk_buff *skb)
	{
		struct socket_key key;

		extract_socket_key(skb, &key);

		return bpf_sk_redirect_hash(skb, &sock_hash_rx, &key, 0);
	}

User space
----------
Several examples of the use of sockmap APIs can be found in:

- `tools/testing/selftests/bpf/prog_tests/sockmap_basic.c`_
- `tools/testing/selftests/bpf/test_sockmap.c`_
- `tools/testing/selftests/bpf/test_maps.c`_

The following code sample shows how to create a sockmap, attach a parser and
verdict program, as well as add a socket entry.

.. code-block:: c

	int create_sample_sockmap(int sock, int parse_prog_fd, int verdict_prog_fd)
	{
		int index = 0;
		int map, err;

		map = bpf_map_create(BPF_MAP_TYPE_SOCKMAP, NULL, sizeof(int), sizeof(int), 1, NULL);
		if (map < 0) {
			fprintf(stderr, "Failed to create sockmap: %s\n", strerror(errno));
			return -1;
		}

		err = bpf_prog_attach(parse_prog_fd, map, BPF_SK_SKB_STREAM_PARSER, 0);
		if (err){
			fprintf(stderr, "Failed to attach_parser_prog_to_map: %s\n", strerror(errno));
			goto out;
		}

		err = bpf_prog_attach(verdict_prog_fd, map, BPF_SK_SKB_STREAM_VERDICT, 0);
		if (err){
			fprintf(stderr, "Failed to attach_verdict_prog_to_map: %s\n", strerror(errno));
			goto out;
		}

		err = bpf_map_update_elem(map, &index, &sock, BPF_NOEXIST);
		if (err) {
			fprintf(stderr, "Failed to update sockmap: %s\n", strerror(errno));
			goto out;
		}

	out:
		close(map);
		return err;
	}

References
===========

- https://github.com/jrfastab/linux-kernel-xdp/commit/c89fd73cb9d2d7f3c716c3e00836f07b1aeb261f
- https://lwn.net/Articles/731133/
- http://vger.kernel.org/lpc_net2018_talks/ktls_bpf_paper.pdf
- https://lwn.net/Articles/748628/
- https://lore.kernel.org/bpf/20200218171023.844439-7-jakub@cloudflare.com/

.. _`tools/testing/selftests/bpf/progs/test_sockmap_kern.h`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/progs/test_sockmap_kern.h
.. _`tools/testing/selftests/bpf/progs/sockmap_parse_prog.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/progs/sockmap_parse_prog.c
.. _`tools/testing/selftests/bpf/progs/sockmap_verdict_prog.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/progs/sockmap_verdict_prog.c
.. _`tools/testing/selftests/bpf/prog_tests/sockmap_basic.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/prog_tests/sockmap_basic.c
.. _`tools/testing/selftests/bpf/test_sockmap.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/test_sockmap.c
.. _`tools/testing/selftests/bpf/test_maps.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/test_maps.c
.. _`tools/testing/selftests/bpf/progs/test_sockmap_listen.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/progs/test_sockmap_listen.c
.. _`tools/testing/selftests/bpf/progs/test_sockmap_update.c`: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/testing/selftests/bpf/progs/test_sockmap_update.c
