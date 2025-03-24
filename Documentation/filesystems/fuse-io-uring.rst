.. SPDX-License-Identifier: GPL-2.0

=======================================
FUSE-over-io-uring design documentation
=======================================

This documentation covers basic details how the fuse
kernel/userspace communication through io-uring is configured
and works. For generic details about FUSE see fuse.rst.

This document also covers the current interface, which is
still in development and might change.

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



