===========
NFS LOCALIO
===========

Overview
========

The LOCALIO auxiliary RPC protocol allows the Linux NFS client and
server to reliably handshake to determine if they are on the same
host. Select "NFS client and server support for LOCALIO auxiliary
protocol" in menuconfig to enable CONFIG_NFS_LOCALIO in the kernel
config (both CONFIG_NFS_FS and CONFIG_NFSD must also be enabled).

Once an NFS client and server handshake as "local", the client will
bypass the network RPC protocol for read, write and commit operations.
Due to this XDR and RPC bypass, these operations will operate faster.

The LOCALIO auxiliary protocol's implementation, which uses the same
connection as NFS traffic, follows the pattern established by the NFS
ACL protocol extension.

The LOCALIO auxiliary protocol is needed to allow robust discovery of
clients local to their servers. In a private implementation that
preceded use of this LOCALIO protocol, a fragile sockaddr network
address based match against all local network interfaces was attempted.
But unlike the LOCALIO protocol, the sockaddr-based matching didn't
handle use of iptables or containers.

The robust handshake between local client and server is just the
beginning, the ultimate use case this locality makes possible is the
client is able to open files and issue reads, writes and commits
directly to the server without having to go over the network. The
requirement is to perform these loopback NFS operations as efficiently
as possible, this is particularly useful for container use cases
(e.g. kubernetes) where it is possible to run an IO job local to the
server.

The performance advantage realized from LOCALIO's ability to bypass
using XDR and RPC for reads, writes and commits can be extreme, e.g.:

fio for 20 secs with directio, qd of 8, 16 libaio threads:
  - With LOCALIO:
    4K read:    IOPS=979k,  BW=3825MiB/s (4011MB/s)(74.7GiB/20002msec)
    4K write:   IOPS=165k,  BW=646MiB/s  (678MB/s)(12.6GiB/20002msec)
    128K read:  IOPS=402k,  BW=49.1GiB/s (52.7GB/s)(982GiB/20002msec)
    128K write: IOPS=11.5k, BW=1433MiB/s (1503MB/s)(28.0GiB/20004msec)

  - Without LOCALIO:
    4K read:    IOPS=79.2k, BW=309MiB/s  (324MB/s)(6188MiB/20003msec)
    4K write:   IOPS=59.8k, BW=234MiB/s  (245MB/s)(4671MiB/20002msec)
    128K read:  IOPS=33.9k, BW=4234MiB/s (4440MB/s)(82.7GiB/20004msec)
    128K write: IOPS=11.5k, BW=1434MiB/s (1504MB/s)(28.0GiB/20011msec)

fio for 20 secs with directio, qd of 8, 1 libaio thread:
  - With LOCALIO:
    4K read:    IOPS=230k,  BW=898MiB/s  (941MB/s)(17.5GiB/20001msec)
    4K write:   IOPS=22.6k, BW=88.3MiB/s (92.6MB/s)(1766MiB/20001msec)
    128K read:  IOPS=38.8k, BW=4855MiB/s (5091MB/s)(94.8GiB/20001msec)
    128K write: IOPS=11.4k, BW=1428MiB/s (1497MB/s)(27.9GiB/20001msec)

  - Without LOCALIO:
    4K read:    IOPS=77.1k, BW=301MiB/s  (316MB/s)(6022MiB/20001msec)
    4K write:   IOPS=32.8k, BW=128MiB/s  (135MB/s)(2566MiB/20001msec)
    128K read:  IOPS=24.4k, BW=3050MiB/s (3198MB/s)(59.6GiB/20001msec)
    128K write: IOPS=11.4k, BW=1430MiB/s (1500MB/s)(27.9GiB/20001msec)

FAQ
===

1. What are the use cases for LOCALIO?

   a. Workloads where the NFS client and server are on the same host
      realize improved IO performance. In particular, it is common when
      running containerised workloads for jobs to find themselves
      running on the same host as the knfsd server being used for
      storage.

2. What are the requirements for LOCALIO?

   a. Bypass use of the network RPC protocol as much as possible. This
      includes bypassing XDR and RPC for open, read, write and commit
      operations.
   b. Allow client and server to autonomously discover if they are
      running local to each other without making any assumptions about
      the local network topology.
   c. Support the use of containers by being compatible with relevant
      namespaces (e.g. network, user, mount).
   d. Support all versions of NFS. NFSv3 is of particular importance
      because it has wide enterprise usage and pNFS flexfiles makes use
      of it for the data path.

3. Why doesn’t LOCALIO just compare IP addresses or hostnames when
   deciding if the NFS client and server are co-located on the same
   host?

   Since one of the main use cases is containerised workloads, we cannot
   assume that IP addresses will be shared between the client and
   server. This sets up a requirement for a handshake protocol that
   needs to go over the same connection as the NFS traffic in order to
   identify that the client and the server really are running on the
   same host. The handshake uses a secret that is sent over the wire,
   and can be verified by both parties by comparing with a value stored
   in shared kernel memory if they are truly co-located.

4. Does LOCALIO improve pNFS flexfiles?

   Yes, LOCALIO complements pNFS flexfiles by allowing it to take
   advantage of NFS client and server locality.  Policy that initiates
   client IO as closely to the server where the data is stored naturally
   benefits from the data path optimization LOCALIO provides.

5. Why not develop a new pNFS layout to enable LOCALIO?

   A new pNFS layout could be developed, but doing so would put the
   onus on the server to somehow discover that the client is co-located
   when deciding to hand out the layout.
   There is value in a simpler approach (as provided by LOCALIO) that
   allows the NFS client to negotiate and leverage locality without
   requiring more elaborate modeling and discovery of such locality in a
   more centralized manner.

6. Why is having the client perform a server-side file OPEN, without
   using RPC, beneficial?  Is the benefit pNFS specific?

   Avoiding the use of XDR and RPC for file opens is beneficial to
   performance regardless of whether pNFS is used. Especially when
   dealing with small files its best to avoid going over the wire
   whenever possible, otherwise it could reduce or even negate the
   benefits of avoiding the wire for doing the small file I/O itself.
   Given LOCALIO's requirements the current approach of having the
   client perform a server-side file open, without using RPC, is ideal.
   If in the future requirements change then we can adapt accordingly.

7. Why is LOCALIO only supported with UNIX Authentication (AUTH_UNIX)?

   Strong authentication is usually tied to the connection itself. It
   works by establishing a context that is cached by the server, and
   that acts as the key for discovering the authorisation token, which
   can then be passed to rpc.mountd to complete the authentication
   process. On the other hand, in the case of AUTH_UNIX, the credential
   that was passed over the wire is used directly as the key in the
   upcall to rpc.mountd. This simplifies the authentication process, and
   so makes AUTH_UNIX easier to support.

8. How do export options that translate RPC user IDs behave for LOCALIO
   operations (eg. root_squash, all_squash)?

   Export options that translate user IDs are managed by nfsd_setuser()
   which is called by nfsd_setuser_and_check_port() which is called by
   __fh_verify().  So they get handled exactly the same way for LOCALIO
   as they do for non-LOCALIO.

9. How does LOCALIO make certain that object lifetimes are managed
   properly given NFSD and NFS operate in different contexts?

   See the detailed "NFS Client and Server Interlock" section below.

RPC
===

The LOCALIO auxiliary RPC protocol consists of a single "UUID_IS_LOCAL"
RPC method that allows the Linux NFS client to verify the local Linux
NFS server can see the nonce (single-use UUID) the client generated and
made available in nfs_common. This protocol isn't part of an IETF
standard, nor does it need to be considering it is Linux-to-Linux
auxiliary RPC protocol that amounts to an implementation detail.

The UUID_IS_LOCAL method encodes the client generated uuid_t in terms of
the fixed UUID_SIZE (16 bytes). The fixed size opaque encode and decode
XDR methods are used instead of the less efficient variable sized
methods.

The RPC program number for the NFS_LOCALIO_PROGRAM is 400122 (as assigned
by IANA, see https://www.iana.org/assignments/rpc-program-numbers/ ):
Linux Kernel Organization       400122  nfslocalio

The LOCALIO protocol spec in rpcgen syntax is::

  /* raw RFC 9562 UUID */
  #define UUID_SIZE 16
  typedef u8 uuid_t<UUID_SIZE>;

  program NFS_LOCALIO_PROGRAM {
      version LOCALIO_V1 {
          void
              NULL(void) = 0;

          void
              UUID_IS_LOCAL(uuid_t) = 1;
      } = 1;
  } = 400122;

LOCALIO uses the same transport connection as NFS traffic. As such,
LOCALIO is not registered with rpcbind.

NFS Common and Client/Server Handshake
======================================

fs/nfs_common/nfslocalio.c provides interfaces that enable an NFS client
to generate a nonce (single-use UUID) and associated short-lived
nfs_uuid_t struct, register it with nfs_common for subsequent lookup and
verification by the NFS server and if matched the NFS server populates
members in the nfs_uuid_t struct. The NFS client then uses nfs_common to
transfer the nfs_uuid_t from its nfs_uuids to the nn->nfsd_serv
clients_list from the nfs_common's uuids_list.  See:
fs/nfs/localio.c:nfs_local_probe()

nfs_common's nfs_uuids list is the basis for LOCALIO enablement, as such
it has members that point to nfsd memory for direct use by the client
(e.g. 'net' is the server's network namespace, through it the client can
access nn->nfsd_serv with proper rcu read access). It is this client
and server synchronization that enables advanced usage and lifetime of
objects to span from the host kernel's nfsd to per-container knfsd
instances that are connected to nfs client's running on the same local
host.

NFS Client and Server Interlock
===============================

LOCALIO provides the nfs_uuid_t object and associated interfaces to
allow proper network namespace (net-ns) and NFSD object refcounting:

    We don't want to keep a long-term counted reference on each NFSD's
    net-ns in the client because that prevents a server container from
    completely shutting down.

    So we avoid taking a reference at all and rely on the per-cpu
    reference to the server (detailed below) being sufficient to keep
    the net-ns active. This involves allowing the NFSD's net-ns exit
    code to iterate all active clients and clear their ->net pointers
    (which are needed to find the per-cpu-refcount for the nfsd_serv).

    Details:

     - Embed nfs_uuid_t in nfs_client. nfs_uuid_t provides a list_head
       that can be used to find the client. It does add the 16-byte
       uuid_t to nfs_client so it is bigger than needed (given that
       uuid_t is only used during the initial NFS client and server
       LOCALIO handshake to determine if they are local to each other).
       If that is really a problem we can find a fix.

     - When the nfs server confirms that the uuid_t is local, it moves
       the nfs_uuid_t onto a per-net-ns list in NFSD's nfsd_net.

     - When each server's net-ns is shutting down - in a "pre_exit"
       handler, all these nfs_uuid_t have their ->net cleared. There is
       an rcu_synchronize() call between pre_exit() handlers and exit()
       handlers so any caller that sees nfs_uuid_t ->net as not NULL can
       safely manage the per-cpu-refcount for nfsd_serv.

     - The client's nfs_uuid_t is passed to nfsd_open_local_fh() so it
       can safely dereference ->net in a private rcu_read_lock() section
       to allow safe access to the associated nfsd_net and nfsd_serv.

So LOCALIO required the introduction and use of NFSD's percpu_ref to
interlock nfsd_destroy_serv() and nfsd_open_local_fh(), to ensure each
nn->nfsd_serv is not destroyed while in use by nfsd_open_local_fh(), and
warrants a more detailed explanation:

    nfsd_open_local_fh() uses nfsd_serv_try_get() before opening its
    nfsd_file handle and then the caller (NFS client) must drop the
    reference for the nfsd_file and associated nn->nfsd_serv using
    nfs_file_put_local() once it has completed its IO.

    This interlock working relies heavily on nfsd_open_local_fh() being
    afforded the ability to safely deal with the possibility that the
    NFSD's net-ns (and nfsd_net by association) may have been destroyed
    by nfsd_destroy_serv() via nfsd_shutdown_net() -- which is only
    possible given the nfs_uuid_t ->net pointer managemenet detailed
    above.

All told, this elaborate interlock of the NFS client and server has been
verified to fix an easy to hit crash that would occur if an NFSD
instance running in a container, with a LOCALIO client mounted, is
shutdown. Upon restart of the container and associated NFSD the client
would go on to crash due to NULL pointer dereference that occurred due
to the LOCALIO client's attempting to nfsd_open_local_fh(), using
nn->nfsd_serv, without having a proper reference on nn->nfsd_serv.

NFS Client issues IO instead of Server
======================================

Because LOCALIO is focused on protocol bypass to achieve improved IO
performance, alternatives to the traditional NFS wire protocol (SUNRPC
with XDR) must be provided to access the backing filesystem.

See fs/nfs/localio.c:nfs_local_open_fh() and
fs/nfsd/localio.c:nfsd_open_local_fh() for the interface that makes
focused use of select nfs server objects to allow a client local to a
server to open a file pointer without needing to go over the network.

The client's fs/nfs/localio.c:nfs_local_open_fh() will call into the
server's fs/nfsd/localio.c:nfsd_open_local_fh() and carefully access
both the associated nfsd network namespace and nn->nfsd_serv in terms of
RCU. If nfsd_open_local_fh() finds that the client no longer sees valid
nfsd objects (be it struct net or nn->nfsd_serv) it returns -ENXIO
to nfs_local_open_fh() and the client will try to reestablish the
LOCALIO resources needed by calling nfs_local_probe() again. This
recovery is needed if/when an nfsd instance running in a container were
to reboot while a LOCALIO client is connected to it.

Once the client has an open nfsd_file pointer it will issue reads,
writes and commits directly to the underlying local filesystem (normally
done by the nfs server). As such, for these operations, the NFS client
is issuing IO to the underlying local filesystem that it is sharing with
the NFS server. See: fs/nfs/localio.c:nfs_local_doio() and
fs/nfs/localio.c:nfs_local_commit().

Security
========

Localio is only supported when UNIX-style authentication (AUTH_UNIX, aka
AUTH_SYS) is used.

Care is taken to ensure the same NFS security mechanisms are used
(authentication, etc) regardless of whether LOCALIO or regular NFS
access is used. The auth_domain established as part of the traditional
NFS client access to the NFS server is also used for LOCALIO.

Relative to containers, LOCALIO gives the client access to the network
namespace the server has. This is required to allow the client to access
the server's per-namespace nfsd_net struct. With traditional NFS, the
client is afforded this same level of access (albeit in terms of the NFS
protocol via SUNRPC). No other namespaces (user, mount, etc) have been
altered or purposely extended from the server to the client.

Testing
=======

The LOCALIO auxiliary protocol and associated NFS LOCALIO read, write
and commit access have proven stable against various test scenarios:

- Client and server both on the same host.

- All permutations of client and server support enablement for both
  local and remote client and server.

- Testing against NFS storage products that don't support the LOCALIO
  protocol was also performed.

- Client on host, server within a container (for both v3 and v4.2).
  The container testing was in terms of podman managed containers and
  includes successful container stop/restart scenario.

- Formalizing these test scenarios in terms of existing test
  infrastructure is on-going. Initial regular coverage is provided in
  terms of ktest running xfstests against a LOCALIO-enabled NFS loopback
  mount configuration, and includes lockdep and KASAN coverage, see:
  https://evilpiepirate.org/~testdashboard/ci?user=snitzer&branch=snitm-nfs-next
  https://github.com/koverstreet/ktest

- Various kdevops testing (in terms of "Chuck's BuildBot") has been
  performed to regularly verify the LOCALIO changes haven't caused any
  regressions to non-LOCALIO NFS use cases.

- All of Hammerspace's various sanity tests pass with LOCALIO enabled
  (this includes numerous pNFS and flexfiles tests).
