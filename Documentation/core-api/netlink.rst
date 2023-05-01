.. SPDX-License-Identifier: BSD-3-Clause

.. _kernel_netlink:

===================================
Netlink notes for kernel developers
===================================

General guidance
================

Attribute enums
---------------

Older families often define "null" attributes and commands with value
of ``0`` and named ``unspec``. This is supported (``type: unused``)
but should be avoided in new families. The ``unspec`` enum values are
not used in practice, so just set the value of the first attribute to ``1``.

Message enums
-------------

Use the same command IDs for requests and replies. This makes it easier
to match them up, and we have plenty of ID space.

Use separate command IDs for notifications. This makes it easier to
sort the notifications from replies (and present them to the user
application via a different API than replies).

Answer requests
---------------

Older families do not reply to all of the commands, especially NEW / ADD
commands. User only gets information whether the operation succeeded or
not via the ACK. Try to find useful data to return. Once the command is
added whether it replies with a full message or only an ACK is uAPI and
cannot be changed. It's better to err on the side of replying.

Specifically NEW and ADD commands should reply with information identifying
the created object such as the allocated object's ID (without having to
resort to using ``NLM_F_ECHO``).

NLM_F_ECHO
----------

Make sure to pass the request info to genl_notify() to allow ``NLM_F_ECHO``
to take effect.  This is useful for programs that need precise feedback
from the kernel (for example for logging purposes).

Support dump consistency
------------------------

If iterating over objects during dump may skip over objects or repeat
them - make sure to report dump inconsistency with ``NLM_F_DUMP_INTR``.
This is usually implemented by maintaining a generation id for the
structure and recording it in the ``seq`` member of struct netlink_callback.

Netlink specification
=====================

Documentation of the Netlink specification parts which are only relevant
to the kernel space.

Globals
-------

kernel-policy
~~~~~~~~~~~~~

Defines if the kernel validation policy is per operation (``per-op``)
or for the entire family (``global``). New families should use ``per-op``
(default) to be able to narrow down the attributes accepted by a specific
command.

checks
------

Documentation for the ``checks`` sub-sections of attribute specs.

unterminated-ok
~~~~~~~~~~~~~~~

Accept strings without the null-termination (for legacy families only).
Switches from the ``NLA_NUL_STRING`` to ``NLA_STRING`` policy type.

max-len
~~~~~~~

Defines max length for a binary or string attribute (corresponding
to the ``len`` member of struct nla_policy). For string attributes terminating
null character is not counted towards ``max-len``.

The field may either be a literal integer value or a name of a defined
constant. String types may reduce the constant by one
(i.e. specify ``max-len: CONST - 1``) to reserve space for the terminating
character so implementations should recognize such pattern.

min-len
~~~~~~~

Similar to ``max-len`` but defines minimum length.
