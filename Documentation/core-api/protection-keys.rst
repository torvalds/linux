.. SPDX-License-Identifier: GPL-2.0

======================
Memory Protection Keys
======================

Memory Protection Keys provide a mechanism for enforcing page-based
protections, but without requiring modification of the page tables when an
application changes protection domains.

Pkeys Userspace (PKU) is a feature which can be found on:
        * Intel server CPUs, Skylake and later
        * Intel client CPUs, Tiger Lake (11th Gen Core) and later
        * Future AMD CPUs
        * arm64 CPUs implementing the Permission Overlay Extension (FEAT_S1POE)

x86_64
======
Pkeys work by dedicating 4 previously Reserved bits in each page table entry to
a "protection key", giving 16 possible keys.

Protections for each key are defined with a per-CPU user-accessible register
(PKRU).  Each of these is a 32-bit register storing two bits (Access Disable
and Write Disable) for each of 16 keys.

Being a CPU register, PKRU is inherently thread-local, potentially giving each
thread a different set of protections from every other thread.

There are two instructions (RDPKRU/WRPKRU) for reading and writing to the
register.  The feature is only available in 64-bit mode, even though there is
theoretically space in the PAE PTEs.  These permissions are enforced on data
access only and have no effect on instruction fetches.

arm64
=====

Pkeys use 3 bits in each page table entry, to encode a "protection key index",
giving 8 possible keys.

Protections for each key are defined with a per-CPU user-writable system
register (POR_EL0).  This is a 64-bit register encoding read, write and execute
overlay permissions for each protection key index.

Being a CPU register, POR_EL0 is inherently thread-local, potentially giving
each thread a different set of protections from every other thread.

Unlike x86_64, the protection key permissions also apply to instruction
fetches.

Syscalls
========

There are 3 system calls which directly interact with pkeys::

	int pkey_alloc(unsigned long flags, unsigned long init_access_rights)
	int pkey_free(int pkey);
	int pkey_mprotect(unsigned long start, size_t len,
			  unsigned long prot, int pkey);

Before a pkey can be used, it must first be allocated with pkey_alloc().  An
application writes to the architecture specific CPU register directly in order
to change access permissions to memory covered with a key.  In this example
this is wrapped by a C function called pkey_set().
::

	int real_prot = PROT_READ|PROT_WRITE;
	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE);
	ptr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	ret = pkey_mprotect(ptr, PAGE_SIZE, real_prot, pkey);
	... application runs here

Now, if the application needs to update the data at 'ptr', it can
gain access, do the update, then remove its write access::

	pkey_set(pkey, 0); // clear PKEY_DISABLE_WRITE
	*ptr = foo; // assign something
	pkey_set(pkey, PKEY_DISABLE_WRITE); // set PKEY_DISABLE_WRITE again

Now when it frees the memory, it will also free the pkey since it
is no longer in use::

	munmap(ptr, PAGE_SIZE);
	pkey_free(pkey);

.. note:: pkey_set() is a wrapper around writing to the CPU register.
          Example implementations can be found in
          tools/testing/selftests/mm/pkey-{arm64,powerpc,x86}.h

Behavior
========

The kernel attempts to make protection keys consistent with the
behavior of a plain mprotect().  For instance if you do this::

	mprotect(ptr, size, PROT_NONE);
	something(ptr);

you can expect the same effects with protection keys when doing this::

	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE | PKEY_DISABLE_READ);
	pkey_mprotect(ptr, size, PROT_READ|PROT_WRITE, pkey);
	something(ptr);

That should be true whether something() is a direct access to 'ptr'
like::

	*ptr = foo;

or when the kernel does the access on the application's behalf like
with a read()::

	read(fd, ptr, 1);

The kernel will send a SIGSEGV in both cases, but si_code will be set
to SEGV_PKERR when violating protection keys versus SEGV_ACCERR when
the plain mprotect() permissions are violated.

Note that kernel accesses from a kthread (such as io_uring) will use a default
value for the protection key register and so will not be consistent with
userspace's value of the register or mprotect().
