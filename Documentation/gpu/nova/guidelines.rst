.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

==========
Guidelines
==========

This document describes the general project guidelines that apply to nova-core
and nova-drm.

Language
========

The Nova project uses the Rust programming language. In this context, all rules
of the Rust for Linux project as documented in
:doc:`../../rust/general-information` apply. Additionally, the following rules
apply.

- Unless technically necessary otherwise (e.g. uAPI), any driver code is written
  in Rust.

- Unless technically necessary, unsafe Rust code must be avoided. In case of
  technical necessity, unsafe code should be isolated in a separate component
  providing a safe API for other driver code to use.

Style
-----

All rules of the Rust for Linux project as documented in
:doc:`../../rust/coding-guidelines` apply.

For a submit checklist, please also see the `Rust for Linux Submit checklist
addendum <https://rust-for-linux.com/contributing#submit-checklist-addendum>`_.

Documentation
=============

The availability of proper documentation is essential in terms of scalability,
accessibility for new contributors and maintainability of a project in general,
but especially for a driver running as complex hardware as Nova is targeting.

Hence, adding documentation of any kind is very much encouraged by the project.

Besides that, there are some minimum requirements.

- Every non-private structure needs at least a brief doc comment explaining the
  semantical sense of the structure, as well as potential locking and lifetime
  requirements. It is encouraged to have the same minimum documentation for
  non-trivial private structures.

- uAPIs must be fully documented with kernel-doc comments; additionally, the
  semantical behavior must be explained including potential special or corner
  cases.

- The APIs connecting the 1st level driver (nova-core) with 2nd level drivers
  must be fully documented. This includes doc comments, potential locking and
  lifetime requirements, as well as example code if applicable.

- Abbreviations must be explained when introduced; terminology must be uniquely
  defined.

- Register addresses, layouts, shift values and masks must be defined properly;
  unless obvious, the semantical sense must be documented. This only applies if
  the author is able to obtain the corresponding information.

Acceptance Criteria
===================

- Patches must only be applied if reviewed by at least one other person on the
  mailing list; this also applies for maintainers.
