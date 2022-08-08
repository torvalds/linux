.. SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

Building libbpf
===============

libelf and zlib are internal dependencies of libbpf and thus are required to link
against and must be installed on the system for applications to work.
pkg-config is used by default to find libelf, and the program called
can be overridden with PKG_CONFIG.

If using pkg-config at build time is not desired, it can be disabled by
setting NO_PKG_CONFIG=1 when calling make.

To build both static libbpf.a and shared libbpf.so:

.. code-block:: bash

    $ cd src
    $ make

To build only static libbpf.a library in directory build/ and install them
together with libbpf headers in a staging directory root/:

.. code-block:: bash

    $ cd src
    $ mkdir build root
    $ BUILD_STATIC_ONLY=y OBJDIR=build DESTDIR=root make install

To build both static libbpf.a and shared libbpf.so against a custom libelf
dependency installed in /build/root/ and install them together with libbpf
headers in a build directory /build/root/:

.. code-block:: bash

    $ cd src
    $ PKG_CONFIG_PATH=/build/root/lib64/pkgconfig DESTDIR=/build/root make