.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2022 Red Hat, Inc.

========
Redirect
========
XDP_REDIRECT
############
Supported maps
--------------

XDP_REDIRECT works with the following map types:

- ``BPF_MAP_TYPE_DEVMAP``
- ``BPF_MAP_TYPE_DEVMAP_HASH``
- ``BPF_MAP_TYPE_CPUMAP``
- ``BPF_MAP_TYPE_XSKMAP``

For more information on these maps, please see the specific map documentation.

Process
-------

.. kernel-doc:: net/core/filter.c
   :doc: xdp redirect

.. note::
    Not all drivers support transmitting frames after a redirect, and for
    those that do, not all of them support non-linear frames. Non-linear xdp
    bufs/frames are bufs/frames that contain more than one fragment.

Debugging packet drops
----------------------
Silent packet drops for XDP_REDIRECT can be debugged using:

- bpf_trace
- perf_record

bpf_trace
^^^^^^^^^
The following bpftrace command can be used to capture and count all XDP tracepoints:

.. code-block:: none

    sudo bpftrace -e 'tracepoint:xdp:* { @cnt[probe] = count(); }'
    Attaching 12 probes...
    ^C

    @cnt[tracepoint:xdp:mem_connect]: 18
    @cnt[tracepoint:xdp:mem_disconnect]: 18
    @cnt[tracepoint:xdp:xdp_exception]: 19605
    @cnt[tracepoint:xdp:xdp_devmap_xmit]: 1393604
    @cnt[tracepoint:xdp:xdp_redirect]: 22292200

.. note::
    The various xdp tracepoints can be found in ``source/include/trace/events/xdp.h``

The following bpftrace command can be used to extract the ``ERRNO`` being returned as
part of the err parameter:

.. code-block:: none

    sudo bpftrace -e \
    'tracepoint:xdp:xdp_redirect*_err {@redir_errno[-args->err] = count();}
    tracepoint:xdp:xdp_devmap_xmit {@devmap_errno[-args->err] = count();}'

perf record
^^^^^^^^^^^
The perf tool also supports recording tracepoints:

.. code-block:: none

    perf record -a -e xdp:xdp_redirect_err \
        -e xdp:xdp_redirect_map_err \
        -e xdp:xdp_exception \
        -e xdp:xdp_devmap_xmit

References
===========

- https://github.com/xdp-project/xdp-tutorial/tree/master/tracing02-xdp-monitor
