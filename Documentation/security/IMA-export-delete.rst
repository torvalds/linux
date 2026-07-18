.. SPDX-License-Identifier: GPL-2.0

==================================
IMA Measurements Export and Delete
==================================


Introduction
============

The IMA measurements list is currently stored in the kernel memory. Memory
occupation grows linearly with the number of records, and can become a
problem especially in environments with reduced resources.

While there is an advantage in keeping the IMA measurements list in kernel
memory, so that it is always available for reading from the securityfs
interfaces, storing it elsewhere would make it possible to free precious
memory for other kernel usage.

The IMA measurements list needs to be retained and safely stored for new
attestation servers to validate it. Assuming the IMA measurements list is
properly saved, storing it outside the kernel does not introduce security
issues, since its integrity is anyway protected by the TPM.

Hence, the new IMA staging mechanism is introduced to export IMA
measurements to user space and delete them from kernel space.

Staging consists in atomically moving the current measurements list to a
temporary list, so that measurements can be deleted afterwards. The staging
operation locks the hot path (racing with addition of new measurements) for
a very short time, only for swapping the list pointers. Deletion of the
measurements instead is done locklessly, away from the hot path.

There are two flavors of the staging mechanism. In the staging with prompt,
all current measurements are staged, read and deleted upon confirmation. In
the staging and deleting flavor, N measurements are staged from the
beginning of the current measurements list and immediately deleted without
confirmation.


Management of Staged Measurements
=================================

Since with the staging mechanism measurement records are removed from the
kernel, the staged measurements need to be saved in a storage and
concatenated together, so that they can be presented during remote
attestation as if staging was never done. This task can be accomplished by
a remote attestation agent modified to support staging, or a system
service.

Coordination is necessary in the case where there are multiple actors
requesting measurements to be staged.

In the staging with prompt case, the measurement interfaces can be accessed
only by one actor (writer) at a time, so the others will get an error until
the former closes it. Since the actors don't care about N, when they gain
access to the interface, they will get all the staged measurements at the
time of their request.

In the case of staging and deleting, coordination is more important, since
there is the risk that two actors unaware of each other compute the value N
on the current measurements list and request IMA to stage N twice.


Remote Attestation Agent Workflow
=================================

Remote attestation agents can be configured to always present all the
measurements to the remote verifiers or, alternatively, to only provide the
measurements that have not been verified yet by the remote verifiers.

In the latter case, determining which measurements need to be sent and
verified must solely depend on the remote verifier. The remote attestation
agent can proactively send partial measurements, at the condition that they
are the ones that the remote verifier needs.

An agent can rely on one of the supported staging methods to proactively
send to a remote verifier the measurements since the previous request up
to the ones that verify the TPM quote obtained in the current request.
The workflow with each staging method is the following.

With staging with prompt, the agent stages the current measurements list,
reads and stores the measurements in a storage and immediately requests
IMA to delete the staged measurements from kernel memory. Afterwards, it
calculates N by replaying the PCR extend on the stored measurements until
the calculated PCRs match the quoted PCRs. It then keeps the measurements
in excess for the next attestation request.

At the next attestation request, the agent performs the same steps above,
and concatenates the new measurements to the ones in excess from the
previous request. Also in this case, the agent replays the PCR extend until
it matches the currently quoted PCRs, keeps the measurements in excess and
presents the new N measurement records to the remote attestation server.

With the staging and deleting method, the agent reads the current
measurements list, calculates N and requests IMA to delete only those. The
measurements in excess are kept in the IMA measurements list and can be
retrieved at the next remote attestation request.

While keeping only the excess measurements in the storage could be
sufficient to serve the requests of a remote verifier, it is advised to
keep all the obtained measurements locally, as they might be needed for the
attestation with a different remote verifier.


Usage
=====

The IMA staging mechanism can be enabled from the kernel configuration with
the CONFIG_IMA_STAGING option. This option prevents inadvertently removing
the IMA measurement list on systems which do not properly save it.

If the option is enabled, IMA duplicates the current securityfs
measurements interfaces (both binary and ASCII), by adding the ``_staged``
file suffix. Both the original and the staging interfaces gain the write
permission for the root user and group, but require the process to have
CAP_SYS_ADMIN set.

The staging mechanism supports two flavors.


Staging with prompt
~~~~~~~~~~~~~~~~~~~

The current measurements list is moved to a temporary staging area,
allowing it to be saved to external storage, before being deleted upon
confirmation.

This staging process is achieved with the following steps.

 1. ``echo A > <_staged interface>``: the user requests IMA to stage the
    entire measurements list;
 2. ``cat <_staged interface>``: the user reads the staged measurements;
 3. ``echo D > <_staged interface>``: the user requests IMA to delete
    staged measurements.


Staging and deleting
~~~~~~~~~~~~~~~~~~~~

N measurements are staged to a temporary staging area, and immediately
deleted without further confirmation.

This staging process is achieved with the following steps.

 1. ``cat <original interface>``: the user reads the current measurements
    list and determines what the value N for staging should be;
 2. ``echo N > <original interface>``: the user requests IMA to delete N
    measurements from the current measurements list.


Interface Access
================

In order to avoid the IMA measurements list being suddenly truncated by the
staging mechanism during a read, or having multiple concurrent staging, a
semaphore-like locking scheme has been implemented on all the measurements
list interfaces.

Multiple readers can access concurrently the original and staged
interfaces, and they can be in mutual exclusion with one writer. In order
to see the same state across all the measurement interfaces, the same
writer is allowed to open multiple interfaces for write or read/write.

If an illegal access occurs, the open to the measurements list interface is
denied.


Kexec
=====

In the event a kexec() system call occurs between staging and deleting, the
staged measurement records are marshalled before the current measurements
list, so that they are both available when the secondary kernel starts.

If measurement is suspended before requesting to delete staged or current
measurements, IMA returns an error to user space to let it know that
marshalling is already in progress, so that it does not save the
measurements twice.

IMA also disallows staging when suspending measurement, to avoid the
situation where neither measurements are carried over to the secondary
kernel, nor they are saved by user space to the storage.


Hash table
==========

By default, the template digest of staged measurement records are kept in
kernel memory (only template data are freed), to be able to detect
duplicate records independently of staging.

The new kernel option ``ima_flush_htable`` has been introduced to
explicitly request a complete deletion of the staged measurements, for
maximum kernel memory saving. If the option has been specified, duplicate
records are still avoided on records of the current measurements list,
but there can be duplicates between different groups of staged
measurements.

Flushing the hash table is supported only for the staging with prompt
flavor. For the staging and deleting flavor, it would have been necessary
to lock the hot path adding new measurements for the time needed to remove
each selected measurement individually.
