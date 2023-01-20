.. SPDX-License-Identifier: BSD-3-Clause

=================================================================
Netlink specification support for legacy Generic Netlink families
=================================================================

This document describes the many additional quirks and properties
required to describe older Generic Netlink families which form
the ``genetlink-legacy`` protocol level.

The spec is a work in progress, some of the quirks are just documented
for future reference.

Specification (defined)
=======================

Attribute type nests
--------------------

New Netlink families should use ``multi-attr`` to define arrays.
Older families (e.g. ``genetlink`` control family) attempted to
define array types reusing attribute type to carry information.

For reference the ``multi-attr`` array may look like this::

  [ARRAY-ATTR]
    [INDEX (optionally)]
    [MEMBER1]
    [MEMBER2]
  [SOME-OTHER-ATTR]
  [ARRAY-ATTR]
    [INDEX (optionally)]
    [MEMBER1]
    [MEMBER2]

where ``ARRAY-ATTR`` is the array entry type.

array-nest
~~~~~~~~~~

``array-nest`` creates the following structure::

  [SOME-OTHER-ATTR]
  [ARRAY-ATTR]
    [ENTRY]
      [MEMBER1]
      [MEMBER2]
    [ENTRY]
      [MEMBER1]
      [MEMBER2]

It wraps the entire array in an extra attribute (hence limiting its size
to 64kB). The ``ENTRY`` nests are special and have the index of the entry
as their type instead of normal attribute type.

type-value
~~~~~~~~~~

``type-value`` is a construct which uses attribute types to carry
information about a single object (often used when array is dumped
entry-by-entry).

``type-value`` can have multiple levels of nesting, for example
genetlink's policy dumps create the following structures::

  [POLICY-IDX]
    [ATTR-IDX]
      [POLICY-INFO-ATTR1]
      [POLICY-INFO-ATTR2]

Where the first level of nest has the policy index as it's attribute
type, it contains a single nest which has the attribute index as its
type. Inside the attr-index nest are the policy attributes. Modern
Netlink families should have instead defined this as a flat structure,
the nesting serves no good purpose here.

Other quirks (todo)
===================

Structures
----------

Legacy families can define C structures both to be used as the contents
of an attribute and as a fixed message header. The plan is to define
the structs in ``definitions`` and link the appropriate attrs.

Multi-message DO
----------------

New Netlink families should never respond to a DO operation with multiple
replies, with ``NLM_F_MULTI`` set. Use a filtered dump instead.

At the spec level we can define a ``dumps`` property for the ``do``,
perhaps with values of ``combine`` and ``multi-object`` depending
on how the parsing should be implemented (parse into a single reply
vs list of objects i.e. pretty much a dump).
