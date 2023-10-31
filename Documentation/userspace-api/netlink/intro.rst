.. SPDX-License-Identifier: BSD-3-Clause

=======================
Introduction to Netlink
=======================

Netlink is often described as an ioctl() replacement.
It aims to replace fixed-format C structures as supplied
to ioctl() with a format which allows an easy way to add
or extended the arguments.

To achieve this Netlink uses a minimal fixed-format metadata header
followed by multiple attributes in the TLV (type, length, value) format.

Unfortunately the protocol has evolved over the years, in an organic
and undocumented fashion, making it hard to coherently explain.
To make the most practical sense this document starts by describing
netlink as it is used today and dives into more "historical" uses
in later sections.

Opening a socket
================

Netlink communication happens over sockets, a socket needs to be
opened first:

.. code-block:: c

  fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

The use of sockets allows for a natural way of exchanging information
in both directions (to and from the kernel). The operations are still
performed synchronously when applications send() the request but
a separate recv() system call is needed to read the reply.

A very simplified flow of a Netlink "call" will therefore look
something like:

.. code-block:: c

  fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

  /* format the request */
  send(fd, &request, sizeof(request));
  n = recv(fd, &response, RSP_BUFFER_SIZE);
  /* interpret the response */

Netlink also provides natural support for "dumping", i.e. communicating
to user space all objects of a certain type (e.g. dumping all network
interfaces).

.. code-block:: c

  fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

  /* format the dump request */
  send(fd, &request, sizeof(request));
  while (1) {
    n = recv(fd, &buffer, RSP_BUFFER_SIZE);
    /* one recv() call can read multiple messages, hence the loop below */
    for (nl_msg in buffer) {
      if (nl_msg.nlmsg_type == NLMSG_DONE)
        goto dump_finished;
      /* process the object */
    }
  }
  dump_finished:

The first two arguments of the socket() call require little explanation -
it is opening a Netlink socket, with all headers provided by the user
(hence NETLINK, RAW). The last argument is the protocol within Netlink.
This field used to identify the subsystem with which the socket will
communicate.

Classic vs Generic Netlink
--------------------------

Initial implementation of Netlink depended on a static allocation
of IDs to subsystems and provided little supporting infrastructure.
Let us refer to those protocols collectively as **Classic Netlink**.
The list of them is defined on top of the ``include/uapi/linux/netlink.h``
file, they include among others - general networking (NETLINK_ROUTE),
iSCSI (NETLINK_ISCSI), and audit (NETLINK_AUDIT).

**Generic Netlink** (introduced in 2005) allows for dynamic registration of
subsystems (and subsystem ID allocation), introspection and simplifies
implementing the kernel side of the interface.

The following section describes how to use Generic Netlink, as the
number of subsystems using Generic Netlink outnumbers the older
protocols by an order of magnitude. There are also no plans for adding
more Classic Netlink protocols to the kernel.
Basic information on how communicating with core networking parts of
the Linux kernel (or another of the 20 subsystems using Classic
Netlink) differs from Generic Netlink is provided later in this document.

Generic Netlink
===============

In addition to the Netlink fixed metadata header each Netlink protocol
defines its own fixed metadata header. (Similarly to how network
headers stack - Ethernet > IP > TCP we have Netlink > Generic N. > Family.)

A Netlink message always starts with struct nlmsghdr, which is followed
by a protocol-specific header. In case of Generic Netlink the protocol
header is struct genlmsghdr.

The practical meaning of the fields in case of Generic Netlink is as follows:

.. code-block:: c

  struct nlmsghdr {
	__u32	nlmsg_len;	/* Length of message including headers */
	__u16	nlmsg_type;	/* Generic Netlink Family (subsystem) ID */
	__u16	nlmsg_flags;	/* Flags - request or dump */
	__u32	nlmsg_seq;	/* Sequence number */
	__u32	nlmsg_pid;	/* Port ID, set to 0 */
  };
  struct genlmsghdr {
	__u8	cmd;		/* Command, as defined by the Family */
	__u8	version;	/* Irrelevant, set to 1 */
	__u16	reserved;	/* Reserved, set to 0 */
  };
  /* TLV attributes follow... */

In Classic Netlink :c:member:`nlmsghdr.nlmsg_type` used to identify
which operation within the subsystem the message was referring to
(e.g. get information about a netdev). Generic Netlink needs to mux
multiple subsystems in a single protocol so it uses this field to
identify the subsystem, and :c:member:`genlmsghdr.cmd` identifies
the operation instead. (See :ref:`res_fam` for
information on how to find the Family ID of the subsystem of interest.)
Note that the first 16 values (0 - 15) of this field are reserved for
control messages both in Classic Netlink and Generic Netlink.
See :ref:`nl_msg_type` for more details.

There are 3 usual types of message exchanges on a Netlink socket:

 - performing a single action (``do``);
 - dumping information (``dump``);
 - getting asynchronous notifications (``multicast``).

Classic Netlink is very flexible and presumably allows other types
of exchanges to happen, but in practice those are the three that get
used.

Asynchronous notifications are sent by the kernel and received by
the user sockets which subscribed to them. ``do`` and ``dump`` requests
are initiated by the user. :c:member:`nlmsghdr.nlmsg_flags` should
be set as follows:

 - for ``do``: ``NLM_F_REQUEST | NLM_F_ACK``
 - for ``dump``: ``NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP``

:c:member:`nlmsghdr.nlmsg_seq` should be a set to a monotonically
increasing value. The value gets echoed back in responses and doesn't
matter in practice, but setting it to an increasing value for each
message sent is considered good hygiene. The purpose of the field is
matching responses to requests. Asynchronous notifications will have
:c:member:`nlmsghdr.nlmsg_seq` of ``0``.

:c:member:`nlmsghdr.nlmsg_pid` is the Netlink equivalent of an address.
This field can be set to ``0`` when talking to the kernel.
See :ref:`nlmsg_pid` for the (uncommon) uses of the field.

The expected use for :c:member:`genlmsghdr.version` was to allow
versioning of the APIs provided by the subsystems. No subsystem to
date made significant use of this field, so setting it to ``1`` seems
like a safe bet.

.. _nl_msg_type:

Netlink message types
---------------------

As previously mentioned :c:member:`nlmsghdr.nlmsg_type` carries
protocol specific values but the first 16 identifiers are reserved
(first subsystem specific message type should be equal to
``NLMSG_MIN_TYPE`` which is ``0x10``).

There are only 4 Netlink control messages defined:

 - ``NLMSG_NOOP`` - ignore the message, not used in practice;
 - ``NLMSG_ERROR`` - carries the return code of an operation;
 - ``NLMSG_DONE`` - marks the end of a dump;
 - ``NLMSG_OVERRUN`` - socket buffer has overflown, not used to date.

``NLMSG_ERROR`` and ``NLMSG_DONE`` are of practical importance.
They carry return codes for operations. Note that unless
the ``NLM_F_ACK`` flag is set on the request Netlink will not respond
with ``NLMSG_ERROR`` if there is no error. To avoid having to special-case
this quirk it is recommended to always set ``NLM_F_ACK``.

The format of ``NLMSG_ERROR`` is described by struct nlmsgerr::

  ----------------------------------------------
  | struct nlmsghdr - response header          |
  ----------------------------------------------
  |    int error                               |
  ----------------------------------------------
  | struct nlmsghdr - original request header |
  ----------------------------------------------
  | ** optionally (1) payload of the request   |
  ----------------------------------------------
  | ** optionally (2) extended ACK             |
  ----------------------------------------------

There are two instances of struct nlmsghdr here, first of the response
and second of the request. ``NLMSG_ERROR`` carries the information about
the request which led to the error. This could be useful when trying
to match requests to responses or re-parse the request to dump it into
logs.

The payload of the request is not echoed in messages reporting success
(``error == 0``) or if ``NETLINK_CAP_ACK`` setsockopt() was set.
The latter is common
and perhaps recommended as having to read a copy of every request back
from the kernel is rather wasteful. The absence of request payload
is indicated by ``NLM_F_CAPPED`` in :c:member:`nlmsghdr.nlmsg_flags`.

The second optional element of ``NLMSG_ERROR`` are the extended ACK
attributes. See :ref:`ext_ack` for more details. The presence
of extended ACK is indicated by ``NLM_F_ACK_TLVS`` in
:c:member:`nlmsghdr.nlmsg_flags`.

``NLMSG_DONE`` is simpler, the request is never echoed but the extended
ACK attributes may be present::

  ----------------------------------------------
  | struct nlmsghdr - response header          |
  ----------------------------------------------
  |    int error                               |
  ----------------------------------------------
  | ** optionally extended ACK                 |
  ----------------------------------------------

.. _res_fam:

Resolving the Family ID
-----------------------

This section explains how to find the Family ID of a subsystem.
It also serves as an example of Generic Netlink communication.

Generic Netlink is itself a subsystem exposed via the Generic Netlink API.
To avoid a circular dependency Generic Netlink has a statically allocated
Family ID (``GENL_ID_CTRL`` which is equal to ``NLMSG_MIN_TYPE``).
The Generic Netlink family implements a command used to find out information
about other families (``CTRL_CMD_GETFAMILY``).

To get information about the Generic Netlink family named for example
``"test1"`` we need to send a message on the previously opened Generic Netlink
socket. The message should target the Generic Netlink Family (1), be a
``do`` (2) call to ``CTRL_CMD_GETFAMILY`` (3). A ``dump`` version of this
call would make the kernel respond with information about *all* the families
it knows about. Last but not least the name of the family in question has
to be specified (4) as an attribute with the appropriate type::

  struct nlmsghdr:
    __u32 nlmsg_len:	32
    __u16 nlmsg_type:	GENL_ID_CTRL               // (1)
    __u16 nlmsg_flags:	NLM_F_REQUEST | NLM_F_ACK  // (2)
    __u32 nlmsg_seq:	1
    __u32 nlmsg_pid:	0

  struct genlmsghdr:
    __u8 cmd:		CTRL_CMD_GETFAMILY         // (3)
    __u8 version:	2 /* or 1, doesn't matter */
    __u16 reserved:	0

  struct nlattr:                                   // (4)
    __u16 nla_len:	10
    __u16 nla_type:	CTRL_ATTR_FAMILY_NAME
    char data: 		test1\0

  (padding:)
    char data:		\0\0

The length fields in Netlink (:c:member:`nlmsghdr.nlmsg_len`
and :c:member:`nlattr.nla_len`) always *include* the header.
Attribute headers in netlink must be aligned to 4 bytes from the start
of the message, hence the extra ``\0\0`` after ``CTRL_ATTR_FAMILY_NAME``.
The attribute lengths *exclude* the padding.

If the family is found kernel will reply with two messages, the response
with all the information about the family::

  /* Message #1 - reply */
  struct nlmsghdr:
    __u32 nlmsg_len:	136
    __u16 nlmsg_type:	GENL_ID_CTRL
    __u16 nlmsg_flags:	0
    __u32 nlmsg_seq:	1    /* echoed from our request */
    __u32 nlmsg_pid:	5831 /* The PID of our user space process */

  struct genlmsghdr:
    __u8 cmd:		CTRL_CMD_GETFAMILY
    __u8 version:	2
    __u16 reserved:	0

  struct nlattr:
    __u16 nla_len:	10
    __u16 nla_type:	CTRL_ATTR_FAMILY_NAME
    char data: 		test1\0

  (padding:)
    data:		\0\0

  struct nlattr:
    __u16 nla_len:	6
    __u16 nla_type:	CTRL_ATTR_FAMILY_ID
    __u16: 		123  /* The Family ID we are after */

  (padding:)
    char data:		\0\0

  struct nlattr:
    __u16 nla_len:	9
    __u16 nla_type:	CTRL_ATTR_FAMILY_VERSION
    __u16: 		1

  /* ... etc, more attributes will follow. */

And the error code (success) since ``NLM_F_ACK`` had been set on the request::

  /* Message #2 - the ACK */
  struct nlmsghdr:
    __u32 nlmsg_len:	36
    __u16 nlmsg_type:	NLMSG_ERROR
    __u16 nlmsg_flags:	NLM_F_CAPPED /* There won't be a payload */
    __u32 nlmsg_seq:	1    /* echoed from our request */
    __u32 nlmsg_pid:	5831 /* The PID of our user space process */

  int error:		0

  struct nlmsghdr: /* Copy of the request header as we sent it */
    __u32 nlmsg_len:	32
    __u16 nlmsg_type:	GENL_ID_CTRL
    __u16 nlmsg_flags:	NLM_F_REQUEST | NLM_F_ACK
    __u32 nlmsg_seq:	1
    __u32 nlmsg_pid:	0

The order of attributes (struct nlattr) is not guaranteed so the user
has to walk the attributes and parse them.

Note that Generic Netlink sockets are not associated or bound to a single
family. A socket can be used to exchange messages with many different
families, selecting the recipient family on message-by-message basis using
the :c:member:`nlmsghdr.nlmsg_type` field.

.. _ext_ack:

Extended ACK
------------

Extended ACK controls reporting of additional error/warning TLVs
in ``NLMSG_ERROR`` and ``NLMSG_DONE`` messages. To maintain backward
compatibility this feature has to be explicitly enabled by setting
the ``NETLINK_EXT_ACK`` setsockopt() to ``1``.

Types of extended ack attributes are defined in enum nlmsgerr_attrs.
The most commonly used attributes are ``NLMSGERR_ATTR_MSG``,
``NLMSGERR_ATTR_OFFS`` and ``NLMSGERR_ATTR_MISS_*``.

``NLMSGERR_ATTR_MSG`` carries a message in English describing
the encountered problem. These messages are far more detailed
than what can be expressed thru standard UNIX error codes.

``NLMSGERR_ATTR_OFFS`` points to the attribute which caused the problem.

``NLMSGERR_ATTR_MISS_TYPE`` and ``NLMSGERR_ATTR_MISS_NEST``
inform about a missing attribute.

Extended ACKs can be reported on errors as well as in case of success.
The latter should be treated as a warning.

Extended ACKs greatly improve the usability of Netlink and should
always be enabled, appropriately parsed and reported to the user.

Advanced topics
===============

Dump consistency
----------------

Some of the data structures kernel uses for storing objects make
it hard to provide an atomic snapshot of all the objects in a dump
(without impacting the fast-paths updating them).

Kernel may set the ``NLM_F_DUMP_INTR`` flag on any message in a dump
(including the ``NLMSG_DONE`` message) if the dump was interrupted and
may be inconsistent (e.g. missing objects). User space should retry
the dump if it sees the flag set.

Introspection
-------------

The basic introspection abilities are enabled by access to the Family
object as reported in :ref:`res_fam`. User can query information about
the Generic Netlink family, including which operations are supported
by the kernel and what attributes the kernel understands.
Family information includes the highest ID of an attribute kernel can parse,
a separate command (``CTRL_CMD_GETPOLICY``) provides detailed information
about supported attributes, including ranges of values the kernel accepts.

Querying family information is useful in cases when user space needs
to make sure that the kernel has support for a feature before issuing
a request.

.. _nlmsg_pid:

nlmsg_pid
---------

:c:member:`nlmsghdr.nlmsg_pid` is the Netlink equivalent of an address.
It is referred to as Port ID, sometimes Process ID because for historical
reasons if the application does not select (bind() to) an explicit Port ID
kernel will automatically assign it the ID equal to its Process ID
(as reported by the getpid() system call).

Similarly to the bind() semantics of the TCP/IP network protocols the value
of zero means "assign automatically", hence it is common for applications
to leave the :c:member:`nlmsghdr.nlmsg_pid` field initialized to ``0``.

The field is still used today in rare cases when kernel needs to send
a unicast notification. User space application can use bind() to associate
its socket with a specific PID, it then communicates its PID to the kernel.
This way the kernel can reach the specific user space process.

This sort of communication is utilized in UMH (User Mode Helper)-like
scenarios when kernel needs to trigger user space processing or ask user
space for a policy decision.

Multicast notifications
-----------------------

One of the strengths of Netlink is the ability to send event notifications
to user space. This is a unidirectional form of communication (kernel ->
user) and does not involve any control messages like ``NLMSG_ERROR`` or
``NLMSG_DONE``.

For example the Generic Netlink family itself defines a set of multicast
notifications about registered families. When a new family is added the
sockets subscribed to the notifications will get the following message::

  struct nlmsghdr:
    __u32 nlmsg_len:	136
    __u16 nlmsg_type:	GENL_ID_CTRL
    __u16 nlmsg_flags:	0
    __u32 nlmsg_seq:	0
    __u32 nlmsg_pid:	0

  struct genlmsghdr:
    __u8 cmd:		CTRL_CMD_NEWFAMILY
    __u8 version:	2
    __u16 reserved:	0

  struct nlattr:
    __u16 nla_len:	10
    __u16 nla_type:	CTRL_ATTR_FAMILY_NAME
    char data: 		test1\0

  (padding:)
    data:		\0\0

  struct nlattr:
    __u16 nla_len:	6
    __u16 nla_type:	CTRL_ATTR_FAMILY_ID
    __u16: 		123  /* The Family ID we are after */

  (padding:)
    char data:		\0\0

  struct nlattr:
    __u16 nla_len:	9
    __u16 nla_type:	CTRL_ATTR_FAMILY_VERSION
    __u16: 		1

  /* ... etc, more attributes will follow. */

The notification contains the same information as the response
to the ``CTRL_CMD_GETFAMILY`` request.

The Netlink headers of the notification are mostly 0 and irrelevant.
The :c:member:`nlmsghdr.nlmsg_seq` may be either zero or a monotonically
increasing notification sequence number maintained by the family.

To receive notifications the user socket must subscribe to the relevant
notification group. Much like the Family ID, the Group ID for a given
multicast group is dynamic and can be found inside the Family information.
The ``CTRL_ATTR_MCAST_GROUPS`` attribute contains nests with names
(``CTRL_ATTR_MCAST_GRP_NAME``) and IDs (``CTRL_ATTR_MCAST_GRP_ID``) of
the groups family.

Once the Group ID is known a setsockopt() call adds the socket to the group:

.. code-block:: c

  unsigned int group_id;

  /* .. find the group ID... */

  setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
             &group_id, sizeof(group_id));

The socket will now receive notifications.

It is recommended to use separate sockets for receiving notifications
and sending requests to the kernel. The asynchronous nature of notifications
means that they may get mixed in with the responses making the message
handling much harder.

Buffer sizing
-------------

Netlink sockets are datagram sockets rather than stream sockets,
meaning that each message must be received in its entirety by a single
recv()/recvmsg() system call. If the buffer provided by the user is too
short, the message will be truncated and the ``MSG_TRUNC`` flag set
in struct msghdr (struct msghdr is the second argument
of the recvmsg() system call, *not* a Netlink header).

Upon truncation the remaining part of the message is discarded.

Netlink expects that the user buffer will be at least 8kB or a page
size of the CPU architecture, whichever is bigger. Particular Netlink
families may, however, require a larger buffer. 32kB buffer is recommended
for most efficient handling of dumps (larger buffer fits more dumped
objects and therefore fewer recvmsg() calls are needed).

.. _classic_netlink:

Classic Netlink
===============

The main differences between Classic and Generic Netlink are the dynamic
allocation of subsystem identifiers and availability of introspection.
In theory the protocol does not differ significantly, however, in practice
Classic Netlink experimented with concepts which were abandoned in Generic
Netlink (really, they usually only found use in a small corner of a single
subsystem). This section is meant as an explainer of a few of such concepts,
with the explicit goal of giving the Generic Netlink
users the confidence to ignore them when reading the uAPI headers.

Most of the concepts and examples here refer to the ``NETLINK_ROUTE`` family,
which covers much of the configuration of the Linux networking stack.
Real documentation of that family, deserves a chapter (or a book) of its own.

Families
--------

Netlink refers to subsystems as families. This is a remnant of using
sockets and the concept of protocol families, which are part of message
demultiplexing in ``NETLINK_ROUTE``.

Sadly every layer of encapsulation likes to refer to whatever it's carrying
as "families" making the term very confusing:

 1. AF_NETLINK is a bona fide socket protocol family
 2. AF_NETLINK's documentation refers to what comes after its own
    header (struct nlmsghdr) in a message as a "Family Header"
 3. Generic Netlink is a family for AF_NETLINK (struct genlmsghdr follows
    struct nlmsghdr), yet it also calls its users "Families".

Note that the Generic Netlink Family IDs are in a different "ID space"
and overlap with Classic Netlink protocol numbers (e.g. ``NETLINK_CRYPTO``
has the Classic Netlink protocol ID of 21 which Generic Netlink will
happily allocate to one of its families as well).

Strict checking
---------------

The ``NETLINK_GET_STRICT_CHK`` socket option enables strict input checking
in ``NETLINK_ROUTE``. It was needed because historically kernel did not
validate the fields of structures it didn't process. This made it impossible
to start using those fields later without risking regressions in applications
which initialized them incorrectly or not at all.

``NETLINK_GET_STRICT_CHK`` declares that the application is initializing
all fields correctly. It also opts into validating that message does not
contain trailing data and requests that kernel rejects attributes with
type higher than largest attribute type known to the kernel.

``NETLINK_GET_STRICT_CHK`` is not used outside of ``NETLINK_ROUTE``.

Unknown attributes
------------------

Historically Netlink ignored all unknown attributes. The thinking was that
it would free the application from having to probe what kernel supports.
The application could make a request to change the state and check which
parts of the request "stuck".

This is no longer the case for new Generic Netlink families and those opting
in to strict checking. See enum netlink_validation for validation types
performed.

Fixed metadata and structures
-----------------------------

Classic Netlink made liberal use of fixed-format structures within
the messages. Messages would commonly have a structure with
a considerable number of fields after struct nlmsghdr. It was also
common to put structures with multiple members inside attributes,
without breaking each member into an attribute of its own.

This has caused problems with validation and extensibility and
therefore using binary structures is actively discouraged for new
attributes.

Request types
-------------

``NETLINK_ROUTE`` categorized requests into 4 types ``NEW``, ``DEL``, ``GET``,
and ``SET``. Each object can handle all or some of those requests
(objects being netdevs, routes, addresses, qdiscs etc.) Request type
is defined by the 2 lowest bits of the message type, so commands for
new objects would always be allocated with a stride of 4.

Each object would also have its own fixed metadata shared by all request
types (e.g. struct ifinfomsg for netdev requests, struct ifaddrmsg for address
requests, struct tcmsg for qdisc requests).

Even though other protocols and Generic Netlink commands often use
the same verbs in their message names (``GET``, ``SET``) the concept
of request types did not find wider adoption.

Notification echo
-----------------

``NLM_F_ECHO`` requests for notifications resulting from the request
to be queued onto the requesting socket. This is useful to discover
the impact of the request.

Note that this feature is not universally implemented.

Other request-type-specific flags
---------------------------------

Classic Netlink defined various flags for its ``GET``, ``NEW``
and ``DEL`` requests in the upper byte of nlmsg_flags in struct nlmsghdr.
Since request types have not been generalized the request type specific
flags are rarely used (and considered deprecated for new families).

For ``GET`` - ``NLM_F_ROOT`` and ``NLM_F_MATCH`` are combined into
``NLM_F_DUMP``, and not used separately. ``NLM_F_ATOMIC`` is never used.

For ``DEL`` - ``NLM_F_NONREC`` is only used by nftables and ``NLM_F_BULK``
only by FDB some operations.

The flags for ``NEW`` are used most commonly in classic Netlink. Unfortunately,
the meaning is not crystal clear. The following description is based on the
best guess of the intention of the authors, and in practice all families
stray from it in one way or another. ``NLM_F_REPLACE`` asks to replace
an existing object, if no matching object exists the operation should fail.
``NLM_F_EXCL`` has the opposite semantics and only succeeds if object already
existed.
``NLM_F_CREATE`` asks for the object to be created if it does not
exist, it can be combined with ``NLM_F_REPLACE`` and ``NLM_F_EXCL``.

A comment in the main Netlink uAPI header states::

   4.4BSD ADD		NLM_F_CREATE|NLM_F_EXCL
   4.4BSD CHANGE	NLM_F_REPLACE

   True CHANGE		NLM_F_CREATE|NLM_F_REPLACE
   Append		NLM_F_CREATE
   Check		NLM_F_EXCL

which seems to indicate that those flags predate request types.
``NLM_F_REPLACE`` without ``NLM_F_CREATE`` was initially used instead
of ``SET`` commands.
``NLM_F_EXCL`` without ``NLM_F_CREATE`` was used to check if object exists
without creating it, presumably predating ``GET`` commands.

``NLM_F_APPEND`` indicates that if one key can have multiple objects associated
with it (e.g. multiple next-hop objects for a route) the new object should be
added to the list rather than replacing the entire list.

uAPI reference
==============

.. kernel-doc:: include/uapi/linux/netlink.h
