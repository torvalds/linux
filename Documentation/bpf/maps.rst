
========
BPF maps
========

BPF 'maps' provide generic storage of different types for sharing data between
kernel and user space. There are several storage types available, including
hash, array, bloom filter and radix-tree. Several of the map types exist to
support specific BPF helpers that perform actions based on the map contents. The
maps are accessed from BPF programs via BPF helpers which are documented in the
`man-pages`_ for `bpf-helpers(7)`_.

BPF maps are accessed from user space via the ``bpf`` syscall, which provides
commands to create maps, lookup elements, update elements and delete elements.
More details of the BPF syscall are available in `ebpf-syscall`_ and in the
`man-pages`_ for `bpf(2)`_.

Map Types
=========

.. toctree::
   :maxdepth: 1
   :glob:

   map_*

Usage Notes
===========

.. c:function::
   int bpf(int command, union bpf_attr *attr, u32 size)

Use the ``bpf()`` system call to perform the operation specified by
``command``. The operation takes parameters provided in ``attr``. The ``size``
argument is the size of the ``union bpf_attr`` in ``attr``.

**BPF_MAP_CREATE**

Create a map with the desired type and attributes in ``attr``:

.. code-block:: c

    int fd;
    union bpf_attr attr = {
            .map_type = BPF_MAP_TYPE_ARRAY;  /* mandatory */
            .key_size = sizeof(__u32);       /* mandatory */
            .value_size = sizeof(__u32);     /* mandatory */
            .max_entries = 256;              /* mandatory */
            .map_flags = BPF_F_MMAPABLE;
            .map_name = "example_array";
    };

    fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));

Returns a process-local file descriptor on success, or negative error in case of
failure. The map can be deleted by calling ``close(fd)``. Maps held by open
file descriptors will be deleted automatically when a process exits.

.. note:: Valid characters for ``map_name`` are ``A-Z``, ``a-z``, ``0-9``,
   ``'_'`` and ``'.'``.

**BPF_MAP_LOOKUP_ELEM**

Lookup key in a given map using ``attr->map_fd``, ``attr->key``,
``attr->value``. Returns zero and stores found elem into ``attr->value`` on
success, or negative error on failure.

**BPF_MAP_UPDATE_ELEM**

Create or update key/value pair in a given map using ``attr->map_fd``, ``attr->key``,
``attr->value``. Returns zero on success or negative error on failure.

**BPF_MAP_DELETE_ELEM**

Find and delete element by key in a given map using ``attr->map_fd``,
``attr->key``. Returns zero on success or negative error on failure.

.. Links:
.. _man-pages: https://www.kernel.org/doc/man-pages/
.. _bpf(2): https://man7.org/linux/man-pages/man2/bpf.2.html
.. _bpf-helpers(7): https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
.. _ebpf-syscall: https://docs.kernel.org/userspace-api/ebpf/syscall.html
