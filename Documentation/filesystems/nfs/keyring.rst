.. SPDX-License-Identifier: GPL-2.0

====================================
The per-namespace NFS client keyring
====================================

The NFS client holds one keyring, named ".nfs", per network
namespace. An xprtsec=mtls mount presents the client certificate and
private key named by its cert_serial= and privkey_serial= mount
options, and the handshake links the mount's namespace .nfs keyring
into tlshd before tlshd reads those keys. Keys placed on the .nfs
keyring therefore need not grant user read permission: tlshd reaches
them as a possessor, and a tlshd in another network namespace never
possesses them.

The keyring is owned by the owner of the network namespace's user
namespace, with KEY_POS_ALL and KEY_USR_ALL less SETATTR. It is not
charged to any quota. Keys added to it by userspace are charged to
the user that adds them.

Finding the keyring serial number
=================================

The .nfs keyring is not linked into any keyring that userspace
possesses, so a serial number is the only way to name it. The NFS
client registers a key type, "nfs_keyring", whose sole purpose is to
hand that serial number out. Its request_key handler runs in the
caller's context and does not upcall to /sbin/request-key.

To obtain the serial number, request a key of type "nfs_keyring" with
the description ".nfs" and read its payload::

	id=$(keyctl request2 nfs_keyring .nfs "" @s)
	serial=$(keyctl print $id)

The contract of the key type is:

 * The description is the string ".nfs". Any other description is
   rejected with EINVAL before a key is allocated.

 * The callout info must be present. request_key() with a NULL
   callout_info never invokes a handler and returns ENOKEY when no
   matching key exists, so use request_key() with an empty string, or
   ``keyctl request2`` rather than ``keyctl request``. The content of
   the callout info is ignored.

 * The payload is the keyring serial number as decimal ASCII digits
   with no terminating NUL. The return value of keyctl_read() gives
   the length; a buffer of 12 bytes is sufficient.

 * add_key() with this type accepts only that payload. A key carrying
   any other value is rejected with EINVAL, so a key found in the
   caller's keyrings always holds the serial number of the caller's
   namespace.

 * The key type carries KEY_TYPE_NET_DOMAIN. A key instantiated in one
   network namespace does not answer a request made in another, so a
   process that keeps its session keyring across setns() receives the
   serial number of the namespace it is in at the time of the request.

The serial number is not a secret. The keyring's own permissions
decide what a caller can do with it.

Provisioning credentials
========================

Add the client certificate and private key to the .nfs keyring as
keys that grant no user read permission, then pass their serial
numbers as cert_serial= and privkey_serial= mount options.
