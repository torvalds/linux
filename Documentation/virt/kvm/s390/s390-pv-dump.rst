.. SPDX-License-Identifier: GPL-2.0

===========================================
s390 (IBM Z) Protected Virtualization dumps
===========================================

Summary
-------

Dumping a VM is an essential tool for debugging problems inside
it. This is especially true when a protected VM runs into trouble as
there's no way to access its memory and registers from the outside
while it's running.

However when dumping a protected VM we need to maintain its
confidentiality until the dump is in the hands of the VM owner who
should be the only one capable of analysing it.

The confidentiality of the VM dump is ensured by the Ultravisor who
provides an interface to KVM over which encrypted CPU and memory data
can be requested. The encryption is based on the Customer
Communication Key which is the key that's used to encrypt VM data in a
way that the customer is able to decrypt.


Dump process
------------

A dump is done in 3 steps:

**Initiation**

This step initializes the dump process, generates cryptographic seeds
and extracts dump keys with which the VM dump data will be encrypted.

**Data gathering**

Currently there are two types of data that can be gathered from a VM:
the memory and the vcpu state.

The vcpu state contains all the important registers, general, floating
point, vector, control and tod/timers of a vcpu. The vcpu dump can
contain incomplete data if a vcpu is dumped while an instruction is
emulated with help of the hypervisor. This is indicated by a flag bit
in the dump data. For the same reason it is very important to not only
write out the encrypted vcpu state, but also the unencrypted state
from the hypervisor.

The memory state is further divided into the encrypted memory and its
metadata comprised of the encryption tweaks and status flags. The
encrypted memory can simply be read once it has been exported. The
time of the export does not matter as no re-encryption is
needed. Memory that has been swapped out and hence was exported can be
read from the swap and written to the dump target without need for any
special actions.

The tweaks / status flags for the exported pages need to be requested
from the Ultravisor.

**Finalization**

The finalization step will provide the data needed to be able to
decrypt the vcpu and memory data and end the dump process. When this
step completes successfully a new dump initiation can be started.
