.. SPDX-License-Identifier: GPL-2.0+

============================
DRM RAS over Generic Netlink
============================

The DRM RAS (Reliability, Availability, Serviceability) interface provides a
standardized way for GPU/accelerator drivers to expose error counters and
other reliability nodes to user space via Generic Netlink. This allows
diagnostic tools, monitoring daemons, or test infrastructure to query hardware
health in a uniform way across different DRM drivers.

Key Goals:

* Provide a standardized RAS solution for GPU and accelerator drivers, enabling
  data center monitoring and reliability operations.
* Implement a single drm-ras Generic Netlink family to meet modern Netlink YAML
  specifications and centralize all RAS-related communication in one namespace.
* Support a basic error counter interface, addressing the immediate, essential
  monitoring needs.
* Offer a flexible, future-proof interface that can be extended to support
  additional types of RAS data in the future.
* Allow multiple nodes per driver, enabling drivers to register separate
  nodes for different IP blocks, sub-blocks, or other logical subdivisions
  as applicable.

Nodes
=====

Nodes are logical abstractions representing an error type or error source within
the device. Currently, only error counter nodes is supported.

Drivers are responsible for registering and unregistering nodes via the
`drm_ras_node_register()` and `drm_ras_node_unregister()` APIs.

Node Management
-------------------

.. kernel-doc:: drivers/gpu/drm/drm_ras.c
   :doc: DRM RAS Node Management
.. kernel-doc:: drivers/gpu/drm/drm_ras.c
   :internal:

Generic Netlink Usage
=====================

The interface is implemented as a Generic Netlink family named ``drm-ras``.
User space tools can:

* List registered nodes with the ``list-nodes`` command.
* List all error counters in an node with the ``get-error-counter`` command with ``node-id``
  as a parameter.
* Query specific error counter values with the ``get-error-counter`` command, using both
  ``node-id`` and ``error-id`` as parameters.

YAML-based Interface
--------------------

The interface is described in a YAML specification ``Documentation/netlink/specs/drm_ras.yaml``

This YAML is used to auto-generate user space bindings via
``tools/net/ynl/pyynl/ynl_gen_c.py``, and drives the structure of netlink
attributes and operations.

Usage Notes
-----------

* User space must first enumerate nodes to obtain their IDs.
* Node IDs or Node names can be used for all further queries, such as error counters.
* Error counters can be queried by either the Error ID or Error name.
* Query Parameters should be defined as part of the uAPI to ensure user interface stability.
* The interface supports future extension by adding new node types and
  additional attributes.

Example: List nodes using ynl

.. code-block:: bash

    sudo ynl --family drm_ras --dump list-nodes
    [{'device-name': '0000:03:00.0',
    'node-id': 0,
    'node-name': 'correctable-errors',
    'node-type': 'error-counter'},
    {'device-name': '0000:03:00.0',
     'node-id': 1,
     'node-name': 'uncorrectable-errors',
     'node-type': 'error-counter'}]

Example: List all error counters using ynl

.. code-block:: bash

    sudo ynl --family drm_ras --dump get-error-counter --json '{"node-id":0}'
    [{'error-id': 1, 'error-name': 'error_name1', 'error-value': 0},
    {'error-id': 2, 'error-name': 'error_name2', 'error-value': 0}]

Example: Query an error counter for a given node

.. code-block:: bash

    sudo ynl --family drm_ras --do get-error-counter --json '{"node-id":0, "error-id":1}'
    {'error-id': 1, 'error-name': 'error_name1', 'error-value': 0}

