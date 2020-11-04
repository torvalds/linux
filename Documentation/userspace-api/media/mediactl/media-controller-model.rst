.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _media-controller-model:

Media device model
==================

Discovering a device internal topology, and configuring it at runtime,
is one of the goals of the media controller API. To achieve this,
hardware devices and Linux Kernel interfaces are modelled as graph
objects on an oriented graph. The object types that constitute the graph
are:

-  An **entity** is a basic media hardware or software building block.
   It can correspond to a large variety of logical blocks such as
   physical hardware devices (CMOS sensor for instance), logical
   hardware devices (a building block in a System-on-Chip image
   processing pipeline), DMA channels or physical connectors.

-  An **interface** is a graph representation of a Linux Kernel
   userspace API interface, like a device node or a sysfs file that
   controls one or more entities in the graph.

-  A **pad** is a data connection endpoint through which an entity can
   interact with other entities. Data (not restricted to video) produced
   by an entity flows from the entity's output to one or more entity
   inputs. Pads should not be confused with physical pins at chip
   boundaries.

-  A **data link** is a point-to-point oriented connection between two
   pads, either on the same entity or on different entities. Data flows
   from a source pad to a sink pad.

-  An **interface link** is a point-to-point bidirectional control
   connection between a Linux Kernel interface and an entity.
