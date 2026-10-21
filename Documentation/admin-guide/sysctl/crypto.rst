=================
/proc/sys/crypto/
=================

These files show up in ``/proc/sys/crypto/``, depending on the
kernel configuration:

.. contents:: :local:

.. _af_alg_restrict:

af_alg_restrict
===============

Controls the level of restriction of AF_ALG.

AF_ALG is a deprecated and rarely-used userspace interface that is a
frequent source of vulnerabilities. It also unnecessarily exposes a
large number of kernel implementation details. For more information
about AF_ALG, see :ref:`Documentation/crypto/userspace-if.rst
<crypto_userspace_interface>`.

Starting in Linux v7.3, AF_ALG supports only a limited set of
algorithms by default. This sysctl allows the system administrator to
remove this restriction when needed for compatibility reasons, or to
go further and disable AF_ALG entirely. The default value is 1.

===  ==================================================================
0    AF_ALG is unrestricted.

1    AF_ALG is supported with a limited list of algorithms. The list
     is designed for compatibility with known users such as iwd and
     bluez that haven't yet been fixed to use userspace crypto code.

     Specifically, there is an allowlist for unprivileged processes
     and a somewhat longer allowlist for processes that hold
     CAP_SYS_ADMIN or CAP_NET_ADMIN in the initial user namespace.

     Attempts to bind() an AF_ALG socket with a disallowed algorithm
     fail with ENOENT.

2    AF_ALG is completely disabled. Attempts to create an AF_ALG
     socket fail with EAFNOSUPPORT.
===  ==================================================================

fips_enabled
============

Read-only flag that indicates whether FIPS mode is enabled.

- ``0``: FIPS mode is disabled (default).
- ``1``: FIPS mode is enabled.

This value is set at boot time via the ``fips=1`` kernel command line
parameter. When enabled, the cryptographic API will restrict the use
of certain algorithms and perform self-tests to ensure compliance with
FIPS (Federal Information Processing Standards) requirements, such as
FIPS 140-2 and the newer FIPS 140-3, depending on the kernel
configuration and the module in use.

fips_name
=========

Read-only file that contains the name of the FIPS module currently in use.
The value is typically configured via the ``CONFIG_CRYPTO_FIPS_NAME``
kernel configuration option.

fips_version
============

Read-only file that contains the version string of the FIPS module.
If ``CONFIG_CRYPTO_FIPS_CUSTOM_VERSION`` is set, it uses the value from
``CONFIG_CRYPTO_FIPS_VERSION``. Otherwise, it defaults to the kernel
release version (``UTS_RELEASE``).

Copyright (c) 2026, Shubham Chakraborty <chakrabortyshubham66@gmail.com>

For general info and legal blurb, please look in
Documentation/admin-guide/sysctl/index.rst.

.. See scripts/check-sysctl-docs to keep this up to date:
.. scripts/check-sysctl-docs -vtable="crypto" \
..         $(git grep -l register_sysctl_)
