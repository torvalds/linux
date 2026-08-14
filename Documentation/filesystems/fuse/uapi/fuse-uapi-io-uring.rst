.. SPDX-License-Identifier: GPL-2.0

=====================================
FUSE-over-io-uring uapi documentation
=====================================

Commands
========

``enum fuse_uring_cmd``:

``FUSE_IO_URING_CMD_ADD_QUEUE``
  Create a queue identified by ``fuse_uring_cmd_req.qid``. Queue-wide
  options are passed in ``fuse_uring_cmd_req.flags``:

  ``FUSE_URING_ZERO_COPY``
    Enable zero-copy on this queue. Requires ``CAP_SYS_ADMIN`` and a buffer
    pool, which is added separately via ``ADD_BUFPOOL`` before registering
    entries (see `Zero-copy`_).

``FUSE_IO_URING_CMD_ADD_BUFPOOL``
  Register the payload buffer pool for an existing queue. The server provides
  a single contiguous region in ``fuse_uring_cmd_req.bufpool.uaddr`` /
  ``.len``. This command must be issued after ``ADD_QUEUE`` and before
  registering any payload-carrying entries on that queue.
  ``fuse_uring_cmd_req.flags`` must be 0. Submitting this command with
  ``IORING_URING_CMD_FIXED`` marks the pool as registered, which avoids per
  i/o pinning/unpinning and mapping overhead (see `Buffer pools`_).

``FUSE_IO_URING_CMD_REGISTER``
  Register a ring entry (a long-lived SQE that carries the request header
  iovec). For a zero-copy queue, ``fuse_uring_cmd_req.ent_zero_copy_buf_index``
  indicates the reserved registered buffer table slot this entry uses for
  zero-copy (see `Zero-copy`_).

``FUSE_IO_URING_CMD_COMMIT_AND_FETCH``
  Commit the reply for a completed request and fetch the next one. The
  request is identified by ``fuse_uring_cmd_req.commit_id`` (the value the
  kernel reported in ``fuse_uring_ent_in_out.commit_id``).

Structures
==========

``struct fuse_uring_cmd_req`` (80-byte SQE command area):

============================  ==================================================
Field                         Meaning
============================  ==================================================
``flags``                     Command-specific flags (see each command).
``commit_id``                 Request id, for ``COMMIT_AND_FETCH``.
``qid``                       Queue index.
``bufpool.uaddr``             Pool base address, for ``ADD_BUFPOOL``.
``bufpool.len``               Pool length in bytes, for ``ADD_BUFPOOL``.
``bufpool.reserved``          Must be 0, for ``ADD_BUFPOOL``.
``ent_zero_copy_buf_index``   Per-entry zero-copy slot, for ``REGISTER``.
============================  ==================================================

``struct fuse_uring_ent_in_out`` (reported by the kernel per request):

============================  ==================================================
Field                         Meaning
============================  ==================================================
``flags``                     ``FUSE_URING_ENT_ZERO_COPY`` if zero-copied.
``commit_id``                 Id to echo back in ``COMMIT_AND_FETCH``.
``payload_sz``                Total payload size in bytes (see `Zero-copy`_).
``offset``                    Payload buffer offset within the pool.
============================  ==================================================

Buffer pools
============
Setup:

* Issue ``ADD_QUEUE`` for the qid.
* Issue ``ADD_BUFPOOL`` with ``bufpool.uaddr`` and ``bufpool.len`` pointing
  at the region.
* Register entries with ``REGISTER``.

For every request that has a payload, the kernel reports where the payload
lives in ``struct fuse_uring_ent_in_out`` (part of
``struct fuse_uring_req_header``):

``offset``
  Byte offset, within the pool region, for this request's payload buffer.
  The server adds this to the pool base address to locate the payload.

``payload_sz``
  Number of payload bytes for this request.

To use registered buffers, the server registers the pool region with io_uring
and submits ``ADD_BUFPOOL`` with ``IORING_URING_CMD_FIXED`` set in
``sqe->uring_cmd_flags`` and the index of the registered bufpool in
``sqe->buf_index``. Every SQE the server submits afterwards must follow the
same fixed-buffer protocol, carrying ``IORING_URING_CMD_FIXED`` and that same
``sqe->buf_index``. The same registered buffer can be reused for the server's
backing-store I/O as well (e.g. ``IORING_OP_READ_FIXED`` /
``IORING_OP_WRITE_FIXED``).

Zero-copy
=========
Requirements:

* The server must be privileged (``CAP_SYS_ADMIN``).
* A zero-copy queue: ``ADD_QUEUE`` with the ``FUSE_URING_ZERO_COPY`` flag set.
* A buffer pool: ``ADD_BUFPOOL``.
* For each entry, ``REGISTER`` with ``ent_zero_copy_buf_index`` set to the
  index this entry uses in the server's io_uring registered-buffer table.
  This is where the kernel registers the request's pages for the server to
  access (it is separate from the payload pool). On a non-zero-copy queue this
  field must be 0.

Zero-copy is selected per open file. The server sets the open-file flag in
the ``FUSE_OPEN`` / ``FUSE_CREATE`` reply:

``FOPEN_IO_URING_ZERO_COPY``
  Reads/writes on this open file should use zero-copy.

For a request that is zero-copied, the kernel sets ``FUSE_URING_ENT_ZERO_COPY``
in ``fuse_uring_ent_in_out.flags`` and places the request's pages at the
entry's ``ent_zero_copy_buf_index``. The server then issues
``IORING_OP_READ_FIXED`` / ``IORING_OP_WRITE_FIXED`` against that index to
transfer the data directly to/from the client's pages.

For such a request, ``payload_sz`` includes the zero-copied page bytes
(transferred via the registered buffer at ``ent_zero_copy_buf_index``). Any
non-page-backed args (e.g. op headers) are still copied through the pool
payload buffer at ``offset``.
