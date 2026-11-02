.. SPDX-License-Identifier: GPL-2.0

.. contents:: :local:

Execution model
===============

When emulating a system with an MMU, UML gives every ``mm_struct`` its own
host process with its own host address space, and keeps that address space
in sync with the kernel's page tables by issuing ``mmap()``/``munmap()``
into it.

NOMMU has no per-process address spaces: there is a single "physical" address
space that the kernel and every userspace process share.  UML models this with
a **userspace runner** - an ordinary uml-userspace stub host process, started
exactly like an MMU userspace process, but with the whole of "physical" memory
(the physmem file) mapped into it at the kernel's addresses:

- The runner ``exec()``'s the stub just like the MMU case.  The only extra step
  is that, once it is up, the kernel maps the entire physmem file into it in one
  go, at the same addresses the kernel uses.
- Because NOMMU never remaps, that single mapping is all a runner ever needs:
  there is no per-mm host process, no TLB sync and no ``mmap()`` of individual
  page-table entries into a child.  Userspace code and data - placed in memory
  by the loader - are directly executable and accessible.
- The runner processes hold no per-task state: the kernel loads a task's state
  on every entry, so there's just one process per CPU.

Running a task's userspace therefore only means pointing a runner at that
task's code and letting it run until the next syscall or signal

Building and running
====================

Configure and build the ARCH=um kernel normally, but deselect CONFIG_MMU.

KUnit tests can be run in NOMMU mode with::

   ./tools/testing/kunit/kunit.py run \
       --kconfig_add CONFIG_MMU=n --kconfig_add CONFIG_KUNIT_UML_PCI=n

Running a normal userspace requires NOMMU-aware binaries.  There is no stock
x86_64 NOMMU distribution, but a prebuilt Alpine image with musl-libc and
busybox built for NOMMU is available and can be turned into a root image::

   cid=$(docker create ghcr.io/thehajime/alpine:3.20.3-um-nommu)
   docker export "$cid" > alpine.tar
   docker rm "$cid"
   mkdir alpine-root && tar xf alpine.tar -C alpine-root
   mke2fs -q -F -t ext4 -d alpine-root alpine.ext4 1200M

Then boot it::

   ./linux ubd0=./alpine.ext4 root=/dev/ubda rw mem=1024m init=/sbin/init
