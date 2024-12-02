.. SPDX-License-Identifier: GPL-2.0

Message Queues
==============
Message queue is a simple low-capacity IPC channel between two VMs. It is
intended for sending small control and configuration messages. Each message
queue is unidirectional, so a full-duplex IPC channel requires a pair of queues.

Messages can be up to 240 bytes in length. Longer messages require a further
protocol on top of the message queue messages themselves. For instance, communication
with the resource manager adds a header field for sending longer messages via multiple
message fragments.

The diagram below shows how message queue works. A typical configuration involves
2 message queues. Message queue 1 allows VM_A to send messages to VM_B. Message
queue 2 allows VM_B to send messages to VM_A.

1. VM_A sends a message of up to 240 bytes in length. It raises a hypercall
   with the message to inform the hypervisor to add the message to
   message queue 1's queue. The hypervisor copies memory into the internal
   message queue representation; the memory doesn't need to be shared between
   VM_A and VM_B.

2. Gunyah raises the corresponding interrupt for VM_B (Rx vIRQ) when any of
   these happens:

   a. gh_msgq_send() has PUSH flag. Queue is immediately flushed. This is the typical case.
   b. Explicility with gh_msgq_push command from VM_A.
   c. Message queue has reached a threshold depth.

3. VM_B calls gh_msgq_recv() and Gunyah copies message to requested buffer.

4. Gunyah buffers messages in the queue. If the queue became full when VM_A added a message,
   the return values for gh_msgq_send() include a flag that indicates the queue is full.
   Once VM_B receives the message and, thus, there is space in the queue, Gunyah
   will raise the Tx vIRQ on VM_A to indicate it can continue sending messages.

For VM_B to send a message to VM_A, the process is identical, except that hypercalls
reference message queue 2's capability ID. Each message queue has its own independent
vIRQ: two TX message queues will have two vIRQs (and two capability IDs).

::

      +---------------+         +-----------------+         +---------------+
      |      VM_A     |         |Gunyah hypervisor|         |      VM_B     |
      |               |         |                 |         |               |
      |               |         |                 |         |               |
      |               |   Tx    |                 |         |               |
      |               |-------->|                 | Rx vIRQ |               |
      |gh_msgq_send() | Tx vIRQ |Message queue 1  |-------->|gh_msgq_recv() |
      |               |<------- |                 |         |               |
      |               |         |                 |         |               |
      | Message Queue |         |                 |         | Message Queue |
      | driver        |         |                 |         | driver        |
      |               |         |                 |         |               |
      |               |         |                 |         |               |
      |               |         |                 |   Tx    |               |
      |               | Rx vIRQ |                 |<--------|               |
      |gh_msgq_recv() |<--------|Message queue 2  | Tx vIRQ |gh_msgq_send() |
      |               |         |                 |-------->|               |
      |               |         |                 |         |               |
      |               |         |                 |         |               |
      +---------------+         +-----------------+         +---------------+

Gunyah message queues are exposed as mailboxes. To create the mailbox, create
a mbox_client and call `gh_msgq_init()`. On receipt of the RX_READY interrupt,
all messages in the RX message queue are read and pushed via the `rx_callback`
of the registered mbox_client.

.. kernel-doc:: drivers/mailbox/gunyah-msgq.c
   :identifiers: gh_msgq_init
