.. SPDX-License-Identifier: GPL-2.0

=======================================
FUSE-over-io-uring design documentation
=======================================

This documentation covers basic details how the fuse
kernel/userspace communication through io-uring is configured
and works. For generic details about FUSE see fuse.rst.

This document also covers the current interface, which is
still in development and might change.

For the userspace protocol, see
Documentation/filesystems/fuse/uapi/fuse-uapi-io-uring.rst.

Limitations
===========
As of now not all requests types are supported through io-uring, userspace
is required to also handle requests through /dev/fuse after io-uring setup
is complete. Specifically notifications (initiated from the daemon side)
and interrupts.

Fuse io-uring configuration
===========================

Fuse kernel requests are queued through the classical /dev/fuse
read/write interface - until io-uring setup is complete.

In order to set up fuse-over-io-uring fuse-server (user-space)
needs to submit SQEs (opcode = IORING_OP_URING_CMD) to the /dev/fuse
connection file descriptor. Initial submit is with the sub command
FUSE_URING_REQ_REGISTER, which will just register entries to be
available in the kernel.

Once at least one entry per queue is submitted, kernel starts
to enqueue to ring queues.
Note, every CPU core has its own fuse-io-uring queue.
Userspace handles the CQE/fuse-request and submits the result as
subcommand FUSE_URING_REQ_COMMIT_AND_FETCH - kernel completes
the requests and also marks the entry available again. If there are
pending requests waiting the request will be immediately submitted
to the daemon again.

Initial SQE
-----------::

 |                                    |  FUSE filesystem daemon
 |                                    |
 |                                    |  >io_uring_submit()
 |                                    |   IORING_OP_URING_CMD /
 |                                    |   FUSE_URING_CMD_REGISTER
 |                                    |  [wait cqe]
 |                                    |   >io_uring_wait_cqe() or
 |                                    |   >io_uring_submit_and_wait()
 |                                    |
 |  >fuse_uring_cmd()                 |
 |   >fuse_uring_register()           |


Sending requests with CQEs
--------------------------::

 |                                           |  FUSE filesystem daemon
 |                                           |  [waiting for CQEs]
 |  "rm /mnt/fuse/file"                      |
 |                                           |
 |  >sys_unlink()                            |
 |    >fuse_unlink()                         |
 |      [allocate request]                   |
 |      >fuse_send_one()                     |
 |        ...                                |
 |       >fuse_uring_queue_fuse_req          |
 |        [queue request on fg queue]        |
 |         >fuse_uring_add_req_to_ring_ent() |
 |         ...                               |
 |          >fuse_uring_copy_to_ring()       |
 |          >io_uring_cmd_done()             |
 |       >request_wait_answer()              |
 |         [sleep on req->waitq]             |
 |                                           |  [receives and handles CQE]
 |                                           |  [submit result and fetch next]
 |                                           |  >io_uring_submit()
 |                                           |   IORING_OP_URING_CMD/
 |                                           |   FUSE_URING_CMD_COMMIT_AND_FETCH
 |  >fuse_uring_cmd()                        |
 |   >fuse_uring_commit_fetch()              |
 |    >fuse_uring_commit()                   |
 |     >fuse_uring_copy_from_ring()          |
 |      [ copy the result to the fuse req]   |
 |     >fuse_uring_req_end()                 |
 |      >fuse_request_end()                  |
 |       [wake up req->waitq]                |
 |    >fuse_uring_next_fuse_req              |
 |       [wait or handle next req]           |
 |                                           |
 |       [req->waitq woken up]               |
 |    <fuse_unlink()                         |
 |  <sys_unlink()                            |

Buffer pools
============

Without a buffer pool, every entry needs to pass a dedicated payload buffer
large enough for the maximum payload size. A buffer pool decouples entries
from payload buffers. The server hands the kernel one contiguous buffer pool
of memory and when the kernel sends the server a request, it indicates the
offset into the pool for that request's payload. Internally, the kernel is
able to manage/optimize the buffer pool memory however it likes.

A server may also register the pool region with io_uring as a fixed buffer.
The backing pages are then pinned once, avoiding per-request pinning and
address translation. This also allows servers to use the same registered
buffers for subsequent backing store I/O through io-uring, keeping data
in the same pinned pages without additional pinning / mapping overhead.

Zero-copy
=========

Zero-copy lets the server read from / write to the client's pages (pinned
user pages for direct I/O, or page-cache folios for buffered I/O) without an
intermediary payload copy. This requires CAP_SYS_ADMIN privileges.

When a fuse request arrives for a file that opted into zero-copy, the kernel
registers the relevant pages (pinned user pages for direct i/o or underlying
page cache folios for buffered i/o) into a sparse slot in the server's
io_uring registered buffer table. The server can then operate on these pages
directly using io-uring fixed buffer operations (eg read_fixed / write_fixed)
and the kernel unregisters these pages when the request completes.
Non-page-backed args (eg op out headers) will go through the payload buffer as
normal.
