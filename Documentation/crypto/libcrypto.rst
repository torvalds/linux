.. SPDX-License-Identifier: GPL-2.0-or-later

==============
Crypto library
==============

The Linux kernel's crypto library (``lib/crypto/``) provides kernel-internal
users of cryptographic algorithms with faster and easier access to those
algorithms than the traditional kernel crypto API.

Each cryptographic algorithm is supported via a set of dedicated functions.
"Crypto agility", where needed, is left to calling code.

The crypto library functions are intended to be boring and straightforward, and
to follow familiar conventions.  Their primary documentation is their (fairly
extensive) kernel-doc.  This page just provides some extra high-level context.

Note that the crypto library isn't entirely new.  ``lib/`` has contained some
crypto functions since 2005.  Rather, it's just an approach that's been expanded
over time as it's been found to work well.  It also largely just matches how the
kernel already does things elsewhere.

Scope and intended audience
===========================

The crypto library documentation is primarily meant for kernel developers who
need to use a particular cryptographic algorithm(s) in kernel code.  For
example, "I just need to compute a SHA-256 hash."  A secondary audience is
developers working on the crypto algorithm implementations themselves.

If you're looking for more general information about cryptography, like the
differences between the different crypto algorithms or how to select an
appropriate algorithm, you should refer to external sources which cover that
type of information much more comprehensively.  If you need help selecting
algorithms for a new kernel feature that doesn't already have its algorithms
predefined, please reach out to ``linux-crypto@vger.kernel.org`` for advice.

Code organization
=================

- ``lib/crypto/*.c``: the crypto algorithm implementations

- ``lib/crypto/$(SRCARCH)/``: architecture-specific code for crypto algorithms.
  It is here rather than somewhere in ``arch/`` partly because this allows
  generic and architecture-optimized code to be easily built into a single
  loadable module (when the algorithm is set to 'm' in the kconfig).

- ``lib/crypto/tests/``: KUnit tests for the crypto algorithms

- ``include/crypto/``: crypto headers, for both the crypto library and the
  traditional crypto API

Generally, there is one kernel module per algorithm.  Sometimes related
algorithms are grouped into one module.  There is intentionally no common
framework, though there are some utility functions that multiple algorithms use.

Each algorithm module is controlled by a tristate kconfig symbol
``CRYPTO_LIB_$(ALGORITHM)``.  As is the norm for library functions in the
kernel, these are hidden symbols which don't show up in the kconfig menu.
Instead, they are just selected by all the kconfig symbols that need them.

Many of the algorithms have multiple implementations: a generic implementation
and architecture-optimized implementation(s).  Each module initialization
function, or initcall in the built-in case, automatically enables the best
implementation based on the available CPU features.

Note that the crypto library doesn't use the ``crypto/``,
``arch/$(SRCARCH)/crypto/``, or ``drivers/crypto/`` directories.  These
directories are used by the traditional crypto API.  When possible, algorithms
in the traditional crypto API are implemented by calls into the library.

Advantages
==========

Some of the advantages of the library over the traditional crypto API are:

- The library functions tend to be much easier to use.  For example, a hash
  value can be computed using only a single function call.  Most of the library
  functions always succeed and return void, eliminating the need to write
  error-handling code.  Most also accept standard virtual addresses, rather than
  scatterlists which are difficult and less efficient to work with.

- The library functions are usually faster, especially for short inputs.  They
  call the crypto algorithms directly without inefficient indirect calls, memory
  allocations, string parsing, lookups in an algorithm registry, and other
  unnecessary API overhead.  Architecture-optimized code is enabled by default.

- The library functions use standard link-time dependencies instead of
  error-prone dynamic loading by name.  There's no need for workarounds such as
  forcing algorithms to be built-in or adding module soft dependencies.

- The library focuses on the approach that works the best on the vast majority
  of systems: CPU-based implementations of the crypto algorithms, utilizing
  on-CPU acceleration (such as AES instructions) when available.

- The library uses standard KUnit tests, rather than custom ad-hoc tests.

- The library tends to have higher assurance implementations of the crypto
  algorithms.  This is both due to its simpler design and because more of its
  code is being regularly tested.

- The library supports features that don't fit into the rigid framework of the
  traditional crypto API, for example interleaved hashing and XOFs.

When to use it
==============

In-kernel users should use the library (rather than the traditional crypto API)
whenever possible.  Many subsystems have already been converted.  It usually
simplifies their code significantly and improves performance.

Some kernel features allow userspace to provide an arbitrary string that selects
an arbitrary algorithm from the traditional crypto API by name.  These features
generally will have to keep using the traditional crypto API for backwards
compatibility.

Note: new kernel features shouldn't support every algorithm, but rather make a
deliberate choice about what algorithm(s) to support.  History has shown that
making a deliberate, thoughtful choice greatly simplifies code maintenance,
reduces the chance for mistakes (such as using an obsolete, insecure, or
inappropriate algorithm), and makes your feature easier to use.

Testing
=======

The crypto library uses standard KUnit tests.  Like many of the kernel's other
KUnit tests, they are included in the set of tests that is run by
``tools/testing/kunit/kunit.py run --alltests``.

A ``.kunitconfig`` file is also provided to run just the crypto library tests.
For example, here's how to run them in user-mode Linux:

.. code-block:: sh

    tools/testing/kunit/kunit.py run --kunitconfig=lib/crypto/

Many of the crypto algorithms have architecture-optimized implementations.
Testing those requires building an appropriate kernel and running the tests
either in QEMU or on appropriate hardware.  Here's one example with QEMU:

.. code-block:: sh

    tools/testing/kunit/kunit.py run --kunitconfig=lib/crypto/ --arch=arm64 --make_options LLVM=1

Depending on the code being tested, flags may need to be passed to QEMU to
emulate the correct type of hardware for the code to be reached.

Since correctness is essential in cryptographic code, new architecture-optimized
code is accepted only if it can be tested in QEMU.

Note: the crypto library also includes FIPS 140 self-tests.  These are
lightweight, are designed specifically to meet FIPS 140 requirements, and exist
*only* to meet those requirements.  Normal testing done by kernel developers and
integrators should use the much more comprehensive KUnit tests instead.

API documentation
=================

.. toctree::
   :maxdepth: 2

   libcrypto-blockcipher
   libcrypto-hash
   libcrypto-signature
   libcrypto-utils
   sha3
