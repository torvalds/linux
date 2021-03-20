.. SPDX-License-Identifier: GPL-2.0

==========================
CIFSD - SMB3 Kernel Server
==========================

CIFSD is a linux kernel server which implements SMB3 protocol in kernel space
for sharing files over network.

CIFSD architecture
==================

               |--- ...
       --------|--- ksmbd/3 - Client 3
       |-------|--- ksmbd/2 - Client 2
       |       |         ____________________________________________________
       |       |        |- Client 1                                          |
<--- Socket ---|--- ksmbd/1   <<= Authentication : NTLM/NTLM2, Kerberos      |
       |       |      | |     <<= SMB engine : SMB2, SMB2.1, SMB3, SMB3.0.2, |
       |       |      | |                SMB3.1.1                            |
       |       |      | |____________________________________________________|
       |       |      |
       |       |      |--- VFS --- Local Filesystem
       |       |
KERNEL |--- ksmbd/0(forker kthread)
---------------||---------------------------------------------------------------
USER           ||
               || communication using NETLINK
               ||  ______________________________________________
               || |                                              |
        ksmbd.mountd <<= DCE/RPC(srvsvc, wkssvc, smar, lsarpc)   |
               ^  |  <<= configure shares setting, user accounts |
               |  |______________________________________________|
               |
               |------ smb.conf(config file)
               |
               |------ ksmbdpwd.db(user account/password file)
                            ^
  ksmbd.adduser ---------------|

The subset of performance related operations belong in kernelspace and
the other subset which belong to operations which are not really related with
performance in userspace. So, DCE/RPC management that has historically resulted
into number of buffer overflow issues and dangerous security bugs and user
account management are implemented in user space as ksmbd.mountd.
File operations that are related with performance (open/read/write/close etc.)
in kernel space (ksmbd). This also allows for easier integration with VFS
interface for all file operations.

ksmbd (kernel daemon)
---------------------

When the server daemon is started, It starts up a forker thread
(ksmbd/interface name) at initialization time and open a dedicated port 445
for listening to SMB requests. Whenever new clients make request, Forker
thread will accept the client connection and fork a new thread for dedicated
communication channel between the client and the server. It allows for parallel
processing of SMB requests(commands) from clients as well as allowing for new
clients to make new connections. Each instance is named ksmbd/1~n(port number)
to indicate connected clients. Depending on the SMB request types, each new
thread can decide to pass through the commands to the user space (ksmbd.mountd),
currently DCE/RPC commands are identified to be handled through the user space.
To further utilize the linux kernel, it has been chosen to process the commands
as workitems and to be executed in the handlers of the ksmbd-io kworker threads.
It allows for multiplexing of the handlers as the kernel take care of initiating
extra worker threads if the load is increased and vice versa, if the load is
decreased it destroys the extra worker threads. So, after connection is
established with client. Dedicated ksmbd/1..n(port number) takes complete
ownership of receiving/parsing of SMB commands. Each received command is worked
in parallel i.e., There can be multiple clients commands which are worked in
parallel. After receiving each command a separated kernel workitem is prepared
for each command which is further queued to be handled by ksmbd-io kworkers.
So, each SMB workitem is queued to the kworkers. This allows the benefit of load
sharing to be managed optimally by the default kernel and optimizing client
performance by handling client commands in parallel.

ksmbd.mountd (user space daemon)
--------------------------------

ksmbd.mountd is userspace process to, transfer user account and password that
are registered using ksmbd.adduser(part of utils for user space). Further it
allows sharing information parameters that parsed from smb.conf to ksmbd in
kernel. For the execution part it has a daemon which is continuously running
and connected to the kernel interface using netlink socket, it waits for the
requests(dcerpc and share/user info). It handles RPC calls (at a minimum few
dozen) that are most important for file server from NetShareEnum and
NetServerGetInfo. Complete DCE/RPC response is prepared from the user space
and passed over to the associated kernel thread for the client.


CIFSD Feature Status
====================

============================== =================================================
Feature name                   Status
============================== =================================================
Dialects                       Supported. SMB2.1 SMB3.0, SMB3.1.1 dialects
                               excluding security vulnerable SMB1.
Auto Negotiation               Supported.
Compound Request               Supported.
Oplock Cache Mechanism         Supported.
SMB2 leases(v1 lease)          Supported.
Directory leases(v2 lease)     Planned for future.
Multi-credits                  Supported.
NTLM/NTLMv2                    Supported.
HMAC-SHA256 Signing            Supported.
Secure negotiate               Supported.
Signing Update                 Supported.
Pre-authentication integrity   Supported.
SMB3 encryption(CCM, GCM)      Supported.
SMB direct(RDMA)               Partial Supported. SMB3 Multi-channel is required
                               to connect to Windows client.
SMB3 Multi-channel             In Progress.
SMB3.1.1 POSIX extension       Supported.
ACLs                           Partial Supported. only DACLs available, SACLs is
                               planned for future. ksmbd generate random subauth
                               values(then store it to disk) and use uid/gid
                               get from inode as RID for local domain SID.
                               The current acl implementation is limited to
                               standalone server, not a domain member.
Kerberos                       Supported.
Durable handle v1,v2           Planned for future.
Persistent handle              Planned for future.
SMB2 notify                    Planned for future.
Sparse file support            Supported.
DCE/RPC support                Partial Supported. a few calls(NetShareEnumAll,
                               NetServerGetInfo, SAMR, LSARPC) that needed as
                               file server via netlink interface from
                               ksmbd.mountd.
============================== =================================================


How to run
==========

1. Download ksmbd-tools and compile them.
	- https://github.com/cifsd-team/ksmbd-tools

2. Create user/password for SMB share.

	# mkdir /etc/ksmbd/
	# ksmbd.adduser -a <Enter USERNAME for SMB share access>

3. Create /etc/ksmbd/smb.conf file, add SMB share in smb.conf file
	- Refer smb.conf.example and Documentation/configuration.txt
	  in ksmbd-tools

4. Insert ksmbd.ko module

	# insmod ksmbd.ko

5. Start ksmbd user space daemon
	# ksmbd.mountd

6. Access share from Windows or Linux using CIFS

Shutdown CIFSD
==============

1. kill user and kernel space daemon
	# sudo ksmbd.control -s

How to turn debug print on
==========================

Each layer
/sys/class/ksmbd-control/debug

1. Enable all component prints
	# sudo ksmbd.control -d "all"

2. Enable one of components(smb, auth, vfs, oplock, ipc, conn, rdma)
	# sudo ksmbd.control -d "smb"

3. Show what prints are enable.
	# cat/sys/class/ksmbd-control/debug
	  [smb] auth vfs oplock ipc conn [rdma]

4. Disable prints:
	If you try the selected component once more, It is disabled without brackets.
