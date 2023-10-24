.. SPDX-License-Identifier: GPL-2.0

=======================
NFSv4 client identifier
=======================

This document explains how the NFSv4 protocol identifies client
instances in order to maintain file open and lock state during
system restarts. A special identifier and principal are maintained
on each client. These can be set by administrators, scripts
provided by site administrators, or tools provided by Linux
distributors.

There are risks if a client's NFSv4 identifier and its principal
are not chosen carefully.


Introduction
------------

The NFSv4 protocol uses "lease-based file locking". Leases help
NFSv4 servers provide file lock guarantees and manage their
resources.

Simply put, an NFSv4 server creates a lease for each NFSv4 client.
The server collects each client's file open and lock state under
the lease for that client.

The client is responsible for periodically renewing its leases.
While a lease remains valid, the server holding that lease
guarantees the file locks the client has created remain in place.

If a client stops renewing its lease (for example, if it crashes),
the NFSv4 protocol allows the server to remove the client's open
and lock state after a certain period of time. When a client
restarts, it indicates to servers that open and lock state
associated with its previous leases is no longer valid and can be
destroyed immediately.

In addition, each NFSv4 server manages a persistent list of client
leases. When the server restarts and clients attempt to recover
their state, the server uses this list to distinguish amongst
clients that held state before the server restarted and clients
sending fresh OPEN and LOCK requests. This enables file locks to
persist safely across server restarts.

NFSv4 client identifiers
------------------------

Each NFSv4 client presents an identifier to NFSv4 servers so that
they can associate the client with its lease. Each client's
identifier consists of two elements:

  - co_ownerid: An arbitrary but fixed string.

  - boot verifier: A 64-bit incarnation verifier that enables a
    server to distinguish successive boot epochs of the same client.

The NFSv4.0 specification refers to these two items as an
"nfs_client_id4". The NFSv4.1 specification refers to these two
items as a "client_owner4".

NFSv4 servers tie this identifier to the principal and security
flavor that the client used when presenting it. Servers use this
principal to authorize subsequent lease modification operations
sent by the client. Effectively this principal is a third element of
the identifier.

As part of the identity presented to servers, a good
"co_ownerid" string has several important properties:

  - The "co_ownerid" string identifies the client during reboot
    recovery, therefore the string is persistent across client
    reboots.
  - The "co_ownerid" string helps servers distinguish the client
    from others, therefore the string is globally unique. Note
    that there is no central authority that assigns "co_ownerid"
    strings.
  - Because it often appears on the network in the clear, the
    "co_ownerid" string does not reveal private information about
    the client itself.
  - The content of the "co_ownerid" string is set and unchanging
    before the client attempts NFSv4 mounts after a restart.
  - The NFSv4 protocol places a 1024-byte limit on the size of the
    "co_ownerid" string.

Protecting NFSv4 lease state
----------------------------

NFSv4 servers utilize the "client_owner4" as described above to
assign a unique lease to each client. Under this scheme, there are
circumstances where clients can interfere with each other. This is
referred to as "lease stealing".

If distinct clients present the same "co_ownerid" string and use
the same principal (for example, AUTH_SYS and UID 0), a server is
unable to tell that the clients are not the same. Each distinct
client presents a different boot verifier, so it appears to the
server as if there is one client that is rebooting frequently.
Neither client can maintain open or lock state in this scenario.

If distinct clients present the same "co_ownerid" string and use
distinct principals, the server is likely to allow the first client
to operate normally but reject subsequent clients with the same
"co_ownerid" string.

If a client's "co_ownerid" string or principal are not stable,
state recovery after a server or client reboot is not guaranteed.
If a client unexpectedly restarts but presents a different
"co_ownerid" string or principal to the server, the server orphans
the client's previous open and lock state. This blocks access to
locked files until the server removes the orphaned state.

If the server restarts and a client presents a changed "co_ownerid"
string or principal to the server, the server will not allow the
client to reclaim its open and lock state, and may give those locks
to other clients in the meantime. This is referred to as "lock
stealing".

Lease stealing and lock stealing increase the potential for denial
of service and in rare cases even data corruption.

Selecting an appropriate client identifier
------------------------------------------

By default, the Linux NFSv4 client implementation constructs its
"co_ownerid" string starting with the words "Linux NFS" followed by
the client's UTS node name (the same node name, incidentally, that
is used as the "machine name" in an AUTH_SYS credential). In small
deployments, this construction is usually adequate. Often, however,
the node name by itself is not adequately unique, and can change
unexpectedly. Problematic situations include:

  - NFS-root (diskless) clients, where the local DHCP server (or
    equivalent) does not provide a unique host name.

  - "Containers" within a single Linux host.  If each container has
    a separate network namespace, but does not use the UTS namespace
    to provide a unique host name, then there can be multiple NFS
    client instances with the same host name.

  - Clients across multiple administrative domains that access a
    common NFS server. If hostnames are not assigned centrally
    then uniqueness cannot be guaranteed unless a domain name is
    included in the hostname.

Linux provides two mechanisms to add uniqueness to its "co_ownerid"
string:

    nfs.nfs4_unique_id
      This module parameter can set an arbitrary uniquifier string
      via the kernel command line, or when the "nfs" module is
      loaded.

    /sys/fs/nfs/net/nfs_client/identifier
      This virtual file, available since Linux 5.3, is local to the
      network namespace in which it is accessed and so can provide
      distinction between network namespaces (containers) when the
      hostname remains uniform.

Note that this file is empty on name-space creation. If the
container system has access to some sort of per-container identity
then that uniquifier can be used. For example, a uniquifier might
be formed at boot using the container's internal identifier:

    sha256sum /etc/machine-id | awk '{print $1}' \\
        > /sys/fs/nfs/net/nfs_client/identifier

Security considerations
-----------------------

The use of cryptographic security for lease management operations
is strongly encouraged.

If NFS with Kerberos is not configured, a Linux NFSv4 client uses
AUTH_SYS and UID 0 as the principal part of its client identity.
This configuration is not only insecure, it increases the risk of
lease and lock stealing. However, it might be the only choice for
client configurations that have no local persistent storage.
"co_ownerid" string uniqueness and persistence is critical in this
case.

When a Kerberos keytab is present on a Linux NFS client, the client
attempts to use one of the principals in that keytab when
identifying itself to servers. The "sec=" mount option does not
control this behavior. Alternately, a single-user client with a
Kerberos principal can use that principal in place of the client's
host principal.

Using Kerberos for this purpose enables the client and server to
use the same lease for operations covered by all "sec=" settings.
Additionally, the Linux NFS client uses the RPCSEC_GSS security
flavor with Kerberos and the integrity QOS to prevent in-transit
modification of lease modification requests.

Additional notes
----------------
The Linux NFSv4 client establishes a single lease on each NFSv4
server it accesses. NFSv4 mounts from a Linux NFSv4 client of a
particular server then share that lease.

Once a client establishes open and lock state, the NFSv4 protocol
enables lease state to transition to other servers, following data
that has been migrated. This hides data migration completely from
running applications. The Linux NFSv4 client facilitates state
migration by presenting the same "client_owner4" to all servers it
encounters.

========
See Also
========

  - nfs(5)
  - kerberos(7)
  - RFC 7530 for the NFSv4.0 specification
  - RFC 8881 for the NFSv4.1 specification.
