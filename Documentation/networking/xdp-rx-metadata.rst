===============
XDP RX Metadata
===============

This document describes how an eXpress Data Path (XDP) program can access
hardware metadata related to a packet using a set of helper functions,
and how it can pass that metadata on to other consumers.

General Design
==============

XDP has access to a set of kfuncs to manipulate the metadata in an XDP frame.
Every device driver that wishes to expose additional packet metadata can
implement these kfuncs. The set of kfuncs is declared in ``include/net/xdp.h``
via ``XDP_METADATA_KFUNC_xxx``.

Currently, the following kfuncs are supported. In the future, as more
metadata is supported, this set will grow:

.. kernel-doc:: net/core/xdp.c
   :identifiers: bpf_xdp_metadata_rx_timestamp bpf_xdp_metadata_rx_hash

An XDP program can use these kfuncs to read the metadata into stack
variables for its own consumption. Or, to pass the metadata on to other
consumers, an XDP program can store it into the metadata area carried
ahead of the packet. Not all packets will necessary have the requested
metadata available in which case the driver returns ``-ENODATA``.

Not all kfuncs have to be implemented by the device driver; when not
implemented, the default ones that return ``-EOPNOTSUPP`` will be used
to indicate the device driver have not implemented this kfunc.


Within an XDP frame, the metadata layout (accessed via ``xdp_buff``) is
as follows::

  +----------+-----------------+------+
  | headroom | custom metadata | data |
  +----------+-----------------+------+
             ^                 ^
             |                 |
   xdp_buff->data_meta   xdp_buff->data

An XDP program can store individual metadata items into this ``data_meta``
area in whichever format it chooses. Later consumers of the metadata
will have to agree on the format by some out of band contract (like for
the AF_XDP use case, see below).

AF_XDP
======

:doc:`af_xdp` use-case implies that there is a contract between the BPF
program that redirects XDP frames into the ``AF_XDP`` socket (``XSK``) and
the final consumer. Thus the BPF program manually allocates a fixed number of
bytes out of metadata via ``bpf_xdp_adjust_meta`` and calls a subset
of kfuncs to populate it. The userspace ``XSK`` consumer computes
``xsk_umem__get_data() - METADATA_SIZE`` to locate that metadata.
Note, ``xsk_umem__get_data`` is defined in ``libxdp`` and
``METADATA_SIZE`` is an application-specific constant (``AF_XDP`` receive
descriptor does _not_ explicitly carry the size of the metadata).

Here is the ``AF_XDP`` consumer layout (note missing ``data_meta`` pointer)::

  +----------+-----------------+------+
  | headroom | custom metadata | data |
  +----------+-----------------+------+
                               ^
                               |
                        rx_desc->address

XDP_PASS
========

This is the path where the packets processed by the XDP program are passed
into the kernel. The kernel creates the ``skb`` out of the ``xdp_buff``
contents. Currently, every driver has custom kernel code to parse
the descriptors and populate ``skb`` metadata when doing this ``xdp_buff->skb``
conversion, and the XDP metadata is not used by the kernel when building
``skbs``. However, TC-BPF programs can access the XDP metadata area using
the ``data_meta`` pointer.

In the future, we'd like to support a case where an XDP program
can override some of the metadata used for building ``skbs``.

bpf_redirect_map
================

``bpf_redirect_map`` can redirect the frame to a different device.
Some devices (like virtual ethernet links) support running a second XDP
program after the redirect. However, the final consumer doesn't have
access to the original hardware descriptor and can't access any of
the original metadata. The same applies to XDP programs installed
into devmaps and cpumaps.

This means that for redirected packets only custom metadata is
currently supported, which has to be prepared by the initial XDP program
before redirect. If the frame is eventually passed to the kernel, the
``skb`` created from such a frame won't have any hardware metadata populated
in its ``skb``. If such a packet is later redirected into an ``XSK``,
that will also only have access to the custom metadata.

bpf_tail_call
=============

Adding programs that access metadata kfuncs to the ``BPF_MAP_TYPE_PROG_ARRAY``
is currently not supported.

Supported Devices
=================

It is possible to query which kfunc the particular netdev implements via
netlink. See ``xdp-rx-metadata-features`` attribute set in
``Documentation/netlink/specs/netdev.yaml``.

Example
=======

See ``tools/testing/selftests/bpf/progs/xdp_metadata.c`` and
``tools/testing/selftests/bpf/prog_tests/xdp_metadata.c`` for an example of
BPF program that handles XDP metadata.
