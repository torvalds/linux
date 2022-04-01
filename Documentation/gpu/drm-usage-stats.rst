.. _drm-client-usage-stats:

======================
DRM client usage stats
======================

DRM drivers can choose to export partly standardised text output via the
`fops->show_fdinfo()` as part of the driver specific file operations registered
in the `struct drm_driver` object registered with the DRM core.

One purpose of this output is to enable writing as generic as practicaly
feasible `top(1)` like userspace monitoring tools.

Given the differences between various DRM drivers the specification of the
output is split between common and driver specific parts. Having said that,
wherever possible effort should still be made to standardise as much as
possible.

File format specification
=========================

- File shall contain one key value pair per one line of text.
- Colon character (`:`) must be used to delimit keys and values.
- All keys shall be prefixed with `drm-`.
- Whitespace between the delimiter and first non-whitespace character shall be
  ignored when parsing.
- Neither keys or values are allowed to contain whitespace characters.
- Numerical key value pairs can end with optional unit string.
- Data type of the value is fixed as defined in the specification.

Key types
---------

1. Mandatory, fully standardised.
2. Optional, fully standardised.
3. Driver specific.

Data types
----------

- <uint> - Unsigned integer without defining the maximum value.
- <str> - String excluding any above defined reserved characters or whitespace.

Mandatory fully standardised keys
---------------------------------

- drm-driver: <str>

String shall contain the name this driver registered as via the respective
`struct drm_driver` data structure.

Optional fully standardised keys
--------------------------------

- drm-pdev: <aaaa:bb.cc.d>

For PCI devices this should contain the PCI slot address of the device in
question.

- drm-client-id: <uint>

Unique value relating to the open DRM file descriptor used to distinguish
duplicated and shared file descriptors. Conceptually the value should map 1:1
to the in kernel representation of `struct drm_file` instances.

Uniqueness of the value shall be either globally unique, or unique within the
scope of each device, in which case `drm-pdev` shall be present as well.

Userspace should make sure to not double account any usage statistics by using
the above described criteria in order to associate data to individual clients.

- drm-engine-<str>: <uint> ns

GPUs usually contain multiple execution engines. Each shall be given a stable
and unique name (str), with possible values documented in the driver specific
documentation.

Value shall be in specified time units which the respective GPU engine spent
busy executing workloads belonging to this client.

Values are not required to be constantly monotonic if it makes the driver
implementation easier, but are required to catch up with the previously reported
larger value within a reasonable period. Upon observing a value lower than what
was previously read, userspace is expected to stay with that larger previous
value until a monotonic update is seen.

- drm-engine-capacity-<str>: <uint>

Engine identifier string must be the same as the one specified in the
drm-engine-<str> tag and shall contain a greater than zero number in case the
exported engine corresponds to a group of identical hardware engines.

In the absence of this tag parser shall assume capacity of one. Zero capacity
is not allowed.

- drm-memory-<str>: <uint> [KiB|MiB]

Each possible memory type which can be used to store buffer objects by the
GPU in question shall be given a stable and unique name to be returned as the
string here.

Value shall reflect the amount of storage currently consumed by the buffer
object belong to this client, in the respective memory region.

Default unit shall be bytes with optional unit specifiers of 'KiB' or 'MiB'
indicating kibi- or mebi-bytes.

===============================
Driver specific implementations
===============================

:ref:`i915-usage-stats`
