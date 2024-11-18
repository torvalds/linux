.. SPDX-License-Identifier: GPL-2.0

Rust
====

Documentation related to Rust within the kernel. To start using Rust
in the kernel, please read the quick-start.rst guide.


The Rust experiment
-------------------

The Rust support was merged in v6.1 into mainline in order to help in
determining whether Rust as a language was suitable for the kernel, i.e. worth
the tradeoffs.

Currently, the Rust support is primarily intended for kernel developers and
maintainers interested in the Rust support, so that they can start working on
abstractions and drivers, as well as helping the development of infrastructure
and tools.

If you are an end user, please note that there are currently no in-tree
drivers/modules suitable or intended for production use, and that the Rust
support is still in development/experimental, especially for certain kernel
configurations.


Code documentation
------------------

Given a kernel configuration, the kernel may generate Rust code documentation,
i.e. HTML rendered by the ``rustdoc`` tool.

.. only:: rustdoc and html

	This kernel documentation was built with `Rust code documentation
	<rustdoc/kernel/index.html>`_.

.. only:: not rustdoc and html

	This kernel documentation was not built with Rust code documentation.

A pregenerated version is provided at:

	https://rust.docs.kernel.org

Please see the :ref:`Code documentation <rust_code_documentation>` section for
more details.

.. toctree::
    :maxdepth: 1

    quick-start
    general-information
    coding-guidelines
    arch-support
    testing

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`
