.. SPDX-License-Identifier: GPL-2.0

=====================
XFRM SA Migrate State
=====================

Overview
========

``XFRM_MSG_MIGRATE_STATE`` migrates a single SA, looked up using SPI and
mark, without involving policies. Unlike ``XFRM_MSG_MIGRATE``, which couples
SA and policy migration and allows migrating multiple SAs in one call, this
interface identifies the SA unambiguously via SPI and supports changing
the reqid, addresses, encapsulation, selector, and offload.

Because IKE daemons can manage policies independently of
the kernel, this interface allows precise per-SA migration without
requiring policy involvement. Optional netlink attributes follow an
omit-to-inherit model: omitting an attribute preserves the value from
the old SA. The ``flags`` field controls two exceptions: hardware offload
is inherited by default and can be suppressed with
``XFRM_MIGRATE_STATE_CLEAR_OFFLOAD`` or overridden with ``XFRMA_OFFLOAD_DEV``;
the new selector is taken from ``new_sel`` by default and can instead be
derived from the new addresses with ``XFRM_MIGRATE_STATE_UPDATE_H2H_SEL``.

SA Identification
=================

The struct is defined in ``include/uapi/linux/xfrm.h``. The SA is looked
up using ``xfrm_state_lookup()`` with ``id.spi``,
``id.daddr``, ``id.proto``, ``id.family``, and
``old_mark.v & old_mark.m`` as the mark key::

    struct xfrm_user_migrate_state {
        struct xfrm_usersa_id  id;       /* spi, daddr, proto, family */
        xfrm_address_t         new_daddr;
        xfrm_address_t         new_saddr;
        struct xfrm_mark       old_mark; /* SA lookup: key = v & m */
        struct xfrm_selector   new_sel;  /* new selector (see Flags) */
        __u32                  new_reqid;
        __u32                  flags;    /* XFRM_MIGRATE_STATE_* */
        __u16                  new_family;
        __u16                  reserved;  /* must be zero */
    };

The ``reserved`` field must be set to zero; the kernel rejects any
other value with ``-EINVAL``.

Supported Attributes
====================

The following fields in ``xfrm_user_migrate_state`` are always explicit
and are not inherited from the existing SA. Passing zero is not equivalent
to "keep unchanged" — zero is used as-is:

- ``new_daddr`` - new destination address
- ``new_saddr`` - new source address
- ``new_family`` - new address family
- ``new_reqid`` - new reqid (0 = no reqid)
- ``new_sel`` - new selector; used when ``XFRM_MIGRATE_STATE_UPDATE_H2H_SEL`` is
  not set (see `Flags`_ below)
- ``flags`` - bitmask of ``XFRM_MIGRATE_STATE_*`` flags (see `Flags`_ below)

The following netlink attributes are also accepted. Omitting an attribute
inherits the value from the existing SA (omit-to-inherit).

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Attribute
     - Description
   * - ``XFRMA_MARK``
     - Mark on the migrated SA (``struct xfrm_mark``). Absent inherits
       ``old_mark``. To use no mark on the new SA, send ``XFRMA_MARK``
       with ``{0, 0}``.
   * - ``XFRMA_ENCAP``
     - UDP encapsulation template; only ``UDP_ENCAP_ESPINUDP`` is supported.
       Set ``encap_type=0`` to remove encap.
   * - ``XFRMA_OFFLOAD_DEV``
     - Hardware offload configuration (``struct xfrm_user_offload``). Absent
       copies offload from the existing SA. When
       ``XFRM_MIGRATE_STATE_CLEAR_OFFLOAD`` is set in ``flags``, the new SA has
       no offload; this flag is mutually exclusive with ``XFRMA_OFFLOAD_DEV``
       and sending both returns ``-EINVAL``.
   * - ``XFRMA_SET_MARK``
     - Output mark on the migrated SA; pair with ``XFRMA_SET_MARK_MASK``.
       Send 0 to clear.
   * - ``XFRMA_NAT_KEEPALIVE_INTERVAL``
     - NAT keepalive interval in seconds. Requires encap. Send 0 to clear.
       Automatically cleared when encap is removed; setting a non-zero
       value without encap returns ``-EINVAL``.
   * - ``XFRMA_MTIMER_THRESH``
     - Mapping maxage threshold. Only valid on input SAs; setting on an
       output SA returns ``-EINVAL``. Requires encap. Send 0 to clear.
       Automatically cleared when encap is removed; setting a non-zero
       value without encap returns ``-EINVAL``.

The following SA properties are immutable and cannot be changed via
``XFRM_MSG_MIGRATE_STATE``: algorithms (``XFRMA_ALG_*``), replay state,
direction (``XFRMA_SA_DIR``), and security context (``XFRMA_SEC_CTX``).

Flags
=====

The ``flags`` field in ``xfrm_user_migrate_state`` controls optional
migration behaviour. Unknown flag bits are rejected with ``-EINVAL``; the
extended ACK message identifies the unrecognised bits (e.g. ``"Unknown flags:
0x4"``). Userspace can use ``XFRM_MIGRATE_STATE_KNOWN_FLAGS`` (defined in
``<linux/xfrm.h>``) to validate flags before sending; note that this constant
reflects the flags known to the header version userspace was compiled against,
which may differ from what the running kernel accepts.

.. list-table::
   :widths: 40 60
   :header-rows: 1

   * - Flag
     - Description
   * - ``XFRM_MIGRATE_STATE_CLEAR_OFFLOAD``
     - When set, the new SA has no hardware offload even when
       ``XFRMA_OFFLOAD_DEV`` is absent. Without this flag, omitting
       ``XFRMA_OFFLOAD_DEV`` copies the existing offload to the new SA.
       Mutually exclusive with ``XFRMA_OFFLOAD_DEV``; sending both
       returns ``-EINVAL``.
   * - ``XFRM_MIGRATE_STATE_UPDATE_H2H_SEL``
     - When set, the kernel validates that the existing SA selector is a
       single-host entry matching the SA addresses (``prefixlen_s ==
       prefixlen_d`` equal to 32 for IPv4 or 128 for IPv6, and addresses
       matching ``id.daddr`` and ``props.saddr``). If the check passes,
       the new selector is derived from ``new_daddr`` and ``new_saddr``
       with the single-host mask for ``new_family``. A mismatch returns
       ``-EINVAL``. When this flag is not set, ``new_sel`` is used as-is
       for the migrated SA.

Migration Steps
===============

Outgoing SA
-----------

To prevent cleartext traffic leaks, install a block policy before
migrating:

#. Install a block policy to drop traffic on the affected selector.
#. Remove the old policy.
#. Call ``XFRM_MSG_MIGRATE_STATE`` for each SA.
#. Reinstall the policies.
#. Remove the block policy.

If AES-GCM is in use, the block policy also prevents IV reuse during
the migration window. For other AEADs this step is not required for
IV safety, but skipping it allows a brief cleartext window.

Incoming SA
-----------

No block policy is needed. ``XFRM_MSG_MIGRATE_STATE`` atomically
transfers the sequence number and replay window from the old SA to
the new SA, so the new SA continues replay protection without a gap.
Call ``XFRM_MSG_MIGRATE_STATE`` for each SA directly.

When accepting incoming traffic, be liberal during the migration
window: packets sent by the remote peer before it completed its own
migration may arrive out of order or slightly late. Dropping them
unnecessarily causes packet loss. A generous replay window reduces
the impact of reordering during migration.

Block Policy and IV Safety
--------------------------

AES-GCM IV uniqueness is critical: reusing a (key, IV) pair allows
an attacker to recover the authentication subkey and forge
authentication tags, breaking both confidentiality and integrity.
This concern applies to outgoing SAs only — the remote peer controls
IV generation on incoming traffic.

``XFRM_MSG_MIGRATE_STATE`` atomically copies the sequence number and
replay window from the old SA to the new SA and deletes the old SA.
The block policy serves two purposes: it prevents cleartext traffic
leaks during the migration window, and for AES-GCM it prevents IV
reuse by ensuring no outgoing packets are sent under the same key.
The atomic copy of the sequence number and replay window complements
this — together they eliminate both risks during migration.
The atomic copy also serves incoming SAs, ensuring replay protection
continues without a gap across the migration.

Feature Detection
=================

Userspace can probe for kernel support by sending a minimal
``XFRM_MSG_MIGRATE_STATE`` message with a non-zero non-existent SPI:

- ``-EINVAL``: kernel predates ``XFRM_MSG_MIGRATE_STATE``; message type
  is out of range
- ``-ENOPROTOOPT``: message type is known but ``CONFIG_XFRM_MIGRATE``
  is not enabled
- ``-ESRCH``: supported (SPI not found)

Userspace Notification on Success
=================================

On successful migration the kernel multicasts an
``XFRM_MSG_MIGRATE_STATE`` message to the ``XFRMNLGRP_MIGRATE`` group.
The fixed header is ``struct xfrm_user_migrate_state`` copied from the
request, followed by the same set of netlink attributes that are
accepted as input, with the differences noted below.

Differences from the request
-----------------------------

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Field / Attribute
     - Difference
   * - ``new_sel``
     - Contains the actual selector of the newly installed SA, not the
       ``new_sel`` from the request. When
       ``XFRM_MIGRATE_STATE_UPDATE_H2H_SEL`` is set the kernel derives the
       selector from ``new_daddr`` / ``new_saddr``; the caller's
       ``new_sel`` field is ignored in that case. The notification
       always carries the real selector of the new SA.
   * - ``XFRMA_SA_DIR``
     - Present in the notification (set from the direction of the new
       SA) but **not accepted as input** — direction is immutable.
   * - ``flags``
     - Echoed back as-is. ``XFRM_MIGRATE_STATE_CLEAR_OFFLOAD`` and
       ``XFRM_MIGRATE_STATE_UPDATE_H2H_SEL`` describe the request that was
       made, not a property of the resulting SA.

Attributes in the notification
-------------------------------

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Attribute
     - Description
   * - ``XFRMA_ENCAP``
     - UDP encapsulation template, if configured on the new SA.
   * - ``XFRMA_OFFLOAD_DEV``
     - Hardware offload configuration, if active on the new SA.
   * - ``XFRMA_MARK``
     - Mark on the new SA, if set.
   * - ``XFRMA_SET_MARK``
     - Output mark on the new SA, if set.
   * - ``XFRMA_SET_MARK_MASK``
     - Output mark mask, present together with ``XFRMA_SET_MARK``.
   * - ``XFRMA_MTIMER_THRESH``
     - Mapping maxage threshold, if non-zero.
   * - ``XFRMA_NAT_KEEPALIVE_INTERVAL``
     - NAT keepalive interval, if non-zero.
   * - ``XFRMA_SA_DIR``
     - Direction of the new SA.

Error Handling
==============

If the target SA tuple (new daddr, SPI, proto, new family) is already
occupied, the operation returns ``-EEXIST`` before the migration begins.
The old SA remains intact and the operation is safe to retry after
resolving the conflict.

If the target SA is deleted before the migration completes, the operation
returns ``-ESRCH``. No new SA is installed. Userspace should verify the
current SA state before retrying.

If the multicast notification (``XFRMNLGRP_MIGRATE``) fails to send,
the migration itself has already completed successfully and the new SA
is installed. The operation returns success, 0, with an extack warning,
but listeners will not receive the migration event.
