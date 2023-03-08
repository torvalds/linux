.. SPDX-License-Identifier: BSD-3-Clause

=========================================
Netlink protocol specifications (in YAML)
=========================================

Netlink protocol specifications are complete, machine readable descriptions of
Netlink protocols written in YAML. The goal of the specifications is to allow
separating Netlink parsing from user space logic and minimize the amount of
hand written Netlink code for each new family, command, attribute.
Netlink specs should be complete and not depend on any other spec
or C header file, making it easy to use in languages which can't include
kernel headers directly.

Internally kernel uses the YAML specs to generate:

 - the C uAPI header
 - documentation of the protocol as a ReST file
 - policy tables for input attribute validation
 - operation tables

YAML specifications can be found under ``Documentation/netlink/specs/``

This document describes details of the schema.
See :doc:`intro-specs` for a practical starting guide.

Compatibility levels
====================

There are four schema levels for Netlink specs, from the simplest used
by new families to the most complex covering all the quirks of the old ones.
Each next level inherits the attributes of the previous level, meaning that
user capable of parsing more complex ``genetlink`` schemas is also compatible
with simpler ones. The levels are:

 - ``genetlink`` - most streamlined, should be used by all new families
 - ``genetlink-c`` - superset of ``genetlink`` with extra attributes allowing
   customization of define and enum type and value names; this schema should
   be equivalent to ``genetlink`` for all implementations which don't interact
   directly with C uAPI headers
 - ``genetlink-legacy`` - Generic Netlink catch all schema supporting quirks of
   all old genetlink families, strange attribute formats, binary structures etc.
 - ``netlink-raw`` - catch all schema supporting pre-Generic Netlink protocols
   such as ``NETLINK_ROUTE``

The definition of the schemas (in ``jsonschema``) can be found
under ``Documentation/netlink/``.

Schema structure
================

YAML schema has the following conceptual sections:

 - globals
 - definitions
 - attributes
 - operations
 - multicast groups

Most properties in the schema accept (or in fact require) a ``doc``
sub-property documenting the defined object.

The following sections describe the properties of the most modern ``genetlink``
schema. See the documentation of :doc:`genetlink-c <c-code-gen>`
for information on how C names are derived from name properties.

genetlink
=========

Globals
-------

Attributes listed directly at the root level of the spec file.

name
~~~~

Name of the family. Name identifies the family in a unique way, since
the Family IDs are allocated dynamically.

version
~~~~~~~

Generic Netlink family version, default is 1.

protocol
~~~~~~~~

The schema level, default is ``genetlink``, which is the only value
allowed for new ``genetlink`` families.

definitions
-----------

Array of type and constant definitions.

name
~~~~

Name of the type / constant.

type
~~~~

One of the following types:

 - const - a single, standalone constant
 - enum - defines an integer enumeration, with values for each entry
   incrementing by 1, (e.g. 0, 1, 2, 3)
 - flags - defines an integer enumeration, with values for each entry
   occupying a bit, starting from bit 0, (e.g. 1, 2, 4, 8)

value
~~~~~

The value for the ``const``.

value-start
~~~~~~~~~~~

The first value for ``enum`` and ``flags``, allows overriding the default
start value of ``0`` (for ``enum``) and starting bit (for ``flags``).
For ``flags`` ``value-start`` selects the starting bit, not the shifted value.

Sparse enumerations are not supported.

entries
~~~~~~~

Array of names of the entries for ``enum`` and ``flags``.

header
~~~~~~

For C-compatible languages, header which already defines this value.
In case the definition is shared by multiple families (e.g. ``IFNAMSIZ``)
code generators for C-compatible languages may prefer to add an appropriate
include instead of rendering a new definition.

attribute-sets
--------------

This property contains information about netlink attributes of the family.
All families have at least one attribute set, most have multiple.
``attribute-sets`` is an array, with each entry describing a single set.

Note that the spec is "flattened" and is not meant to visually resemble
the format of the netlink messages (unlike certain ad-hoc documentation
formats seen in kernel comments). In the spec subordinate attribute sets
are not defined inline as a nest, but defined in a separate attribute set
referred to with a ``nested-attributes`` property of the container.

Spec may also contain fractional sets - sets which contain a ``subset-of``
property. Such sets describe a section of a full set, allowing narrowing down
which attributes are allowed in a nest or refining the validation criteria.
Fractional sets can only be used in nests. They are not rendered to the uAPI
in any fashion.

name
~~~~

Uniquely identifies the attribute set, operations and nested attributes
refer to the sets by the ``name``.

subset-of
~~~~~~~~~

Re-defines a portion of another set (a fractional set).
Allows narrowing down fields and changing validation criteria
or even types of attributes depending on the nest in which they
are contained. The ``value`` of each attribute in the fractional
set is implicitly the same as in the main set.

attributes
~~~~~~~~~~

List of attributes in the set.

Attribute properties
--------------------

name
~~~~

Identifies the attribute, unique within the set.

type
~~~~

Netlink attribute type, see :ref:`attr_types`.

.. _assign_val:

value
~~~~~

Numerical attribute ID, used in serialized Netlink messages.
The ``value`` property can be skipped, in which case the attribute ID
will be the value of the previous attribute plus one (recursively)
and ``0`` for the first attribute in the attribute set.

Note that the ``value`` of an attribute is defined only in its main set.

enum
~~~~

For integer types specifies that values in the attribute belong
to an ``enum`` or ``flags`` from the ``definitions`` section.

enum-as-flags
~~~~~~~~~~~~~

Treat ``enum`` as ``flags`` regardless of its type in ``definitions``.
When both ``enum`` and ``flags`` forms are needed ``definitions`` should
contain an ``enum`` and attributes which need the ``flags`` form should
use this attribute.

nested-attributes
~~~~~~~~~~~~~~~~~

Identifies the attribute space for attributes nested within given attribute.
Only valid for complex attributes which may have sub-attributes.

multi-attr (arrays)
~~~~~~~~~~~~~~~~~~~

Boolean property signifying that the attribute may be present multiple times.
Allowing an attribute to repeat is the recommended way of implementing arrays
(no extra nesting).

byte-order
~~~~~~~~~~

For integer types specifies attribute byte order - ``little-endian``
or ``big-endian``.

checks
~~~~~~

Input validation constraints used by the kernel. User space should query
the policy of the running kernel using Generic Netlink introspection,
rather than depend on what is specified in the spec file.

The validation policy in the kernel is formed by combining the type
definition (``type`` and ``nested-attributes``) and the ``checks``.

operations
----------

This section describes messages passed between the kernel and the user space.
There are three types of entries in this section - operations, notifications
and events.

Operations describe the most common request - response communication. User
sends a request and kernel replies. Each operation may contain any combination
of the two modes familiar to netlink users - ``do`` and ``dump``.
``do`` and ``dump`` in turn contain a combination of ``request`` and
``response`` properties. If no explicit message with attributes is passed
in a given direction (e.g. a ``dump`` which does not accept filter, or a ``do``
of a SET operation to which the kernel responds with just the netlink error
code) ``request`` or ``response`` section can be skipped.
``request`` and ``response`` sections list the attributes allowed in a message.
The list contains only the names of attributes from a set referred
to by the ``attribute-set`` property.

Notifications and events both refer to the asynchronous messages sent by
the kernel to members of a multicast group. The difference between the
two is that a notification shares its contents with a GET operation
(the name of the GET operation is specified in the ``notify`` property).
This arrangement is commonly used for notifications about
objects where the notification carries the full object definition.

Events are more focused and carry only a subset of information rather than full
object state (a made up example would be a link state change event with just
the interface name and the new link state). Events contain the ``event``
property. Events are considered less idiomatic for netlink and notifications
should be preferred.

list
~~~~

The only property of ``operations`` for ``genetlink``, holds the list of
operations, notifications etc.

Operation properties
--------------------

name
~~~~

Identifies the operation.

value
~~~~~

Numerical message ID, used in serialized Netlink messages.
The same enumeration rules are applied as to
:ref:`attribute values<assign_val>`.

attribute-set
~~~~~~~~~~~~~

Specifies the attribute set contained within the message.

do
~~~

Specification for the ``doit`` request. Should contain ``request``, ``reply``
or both of these properties, each holding a :ref:`attr_list`.

dump
~~~~

Specification for the ``dumpit`` request. Should contain ``request``, ``reply``
or both of these properties, each holding a :ref:`attr_list`.

notify
~~~~~~

Designates the message as a notification. Contains the name of the operation
(possibly the same as the operation holding this property) which shares
the contents with the notification (``do``).

event
~~~~~

Specification of attributes in the event, holds a :ref:`attr_list`.
``event`` property is mutually exclusive with ``notify``.

mcgrp
~~~~~

Used with ``event`` and ``notify``, specifies which multicast group
message belongs to.

.. _attr_list:

Message attribute list
----------------------

``request``, ``reply`` and ``event`` properties have a single ``attributes``
property which holds the list of attribute names.

Messages can also define ``pre`` and ``post`` properties which will be rendered
as ``pre_doit`` and ``post_doit`` calls in the kernel (these properties should
be ignored by user space).

mcast-groups
------------

This section lists the multicast groups of the family.

list
~~~~

The only property of ``mcast-groups`` for ``genetlink``, holds the list
of groups.

Multicast group properties
--------------------------

name
~~~~

Uniquely identifies the multicast group in the family. Similarly to
Family ID, Multicast Group ID needs to be resolved at runtime, based
on the name.

.. _attr_types:

Attribute types
===============

This section describes the attribute types supported by the ``genetlink``
compatibility level. Refer to documentation of different levels for additional
attribute types.

Scalar integer types
--------------------

Fixed-width integer types:
``u8``, ``u16``, ``u32``, ``u64``, ``s8``, ``s16``, ``s32``, ``s64``.

Note that types smaller than 32 bit should be avoided as using them
does not save any memory in Netlink messages (due to alignment).
See :ref:`pad_type` for padding of 64 bit attributes.

The payload of the attribute is the integer in host order unless ``byte-order``
specifies otherwise.

.. _pad_type:

pad
---

Special attribute type used for padding attributes which require alignment
bigger than standard 4B alignment required by netlink (e.g. 64 bit integers).
There can only be a single attribute of the ``pad`` type in any attribute set
and it should be automatically used for padding when needed.

flag
----

Attribute with no payload, its presence is the entire information.

binary
------

Raw binary data attribute, the contents are opaque to generic code.

string
------

Character string. Unless ``checks`` has ``unterminated-ok`` set to ``true``
the string is required to be null terminated.
``max-len`` in ``checks`` indicates the longest possible string,
if not present the length of the string is unbounded.

Note that ``max-len`` does not count the terminating character.

nest
----

Attribute containing other (nested) attributes.
``nested-attributes`` specifies which attribute set is used inside.
