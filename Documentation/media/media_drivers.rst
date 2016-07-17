==========
Media core
==========

Video2Linux devices
-------------------

.. kernel-doc:: include/media/tuner.h

.. kernel-doc:: include/media/tuner-types.h

.. kernel-doc:: include/media/tveeprom.h

.. kernel-doc:: include/media/v4l2-async.h

.. kernel-doc:: include/media/v4l2-ctrls.h

.. kernel-doc:: include/media/v4l2-dv-timings.h

.. kernel-doc:: include/media/v4l2-event.h

.. kernel-doc:: include/media/v4l2-flash-led-class.h

.. kernel-doc:: include/media/v4l2-mc.h

.. kernel-doc:: include/media/v4l2-mediabus.h

.. kernel-doc:: include/media/v4l2-mem2mem.h

.. kernel-doc:: include/media/v4l2-of.h

.. kernel-doc:: include/media/v4l2-rect.h

.. kernel-doc:: include/media/v4l2-subdev.h

.. kernel-doc:: include/media/videobuf2-core.h

.. kernel-doc:: include/media/videobuf2-v4l2.h

.. kernel-doc:: include/media/videobuf2-memops.h


Digital TV (DVB) devices
------------------------

Digital TV Common functions
---------------------------

.. kernel-doc:: drivers/media/dvb-core/dvb_math.h

.. kernel-doc:: drivers/media/dvb-core/dvb_ringbuffer.h

.. kernel-doc:: drivers/media/dvb-core/dvbdev.h


Digital TV Frontend kABI
------------------------

Digital TV Frontend
~~~~~~~~~~~~~~~~~~~

The Digital TV Frontend kABI defines a driver-internal interface for
registering low-level, hardware specific driver to a hardware independent
frontend layer. It is only of interest for Digital TV device driver writers.
The header file for this API is named dvb_frontend.h and located in
drivers/media/dvb-core.

Before using the Digital TV frontend core, the bridge driver should attach
the frontend demod, tuner and SEC devices and call dvb_register_frontend(),
in order to register the new frontend at the subsystem. At device
detach/removal, the bridge driver should call dvb_unregister_frontend() to
remove the frontend from the core and then dvb_frontend_detach() to free the
memory allocated by the frontend drivers.

The drivers should also call dvb_frontend_suspend() as part of their
handler for the &device_driver.suspend(), and dvb_frontend_resume() as
part of their handler for &device_driver.resume().

few other optional functions are provided to handle some special cases.

.. kernel-doc:: drivers/media/dvb-core/dvb_frontend.h


Digital TV Demux kABI
---------------------

Digital TV Demux
~~~~~~~~~~~~~~~~

The Kernel Digital TV Demux kABI defines a driver-internal interface for
registering low-level, hardware specific driver to a hardware independent
demux layer. It is only of interest for Digital TV device driver writers.
The header file for this kABI is named demux.h and located in
drivers/media/dvb-core.

The demux kABI should be implemented for each demux in the system. It is
used to select the TS source of a demux and to manage the demux resources.
When the demux client allocates a resource via the demux kABI, it receives
a pointer to the kABI of that resource.

Each demux receives its TS input from a DVB front-end or from memory, as
set via this demux kABI. In a system with more than one front-end, the kABI
can be used to select one of the DVB front-ends as a TS source for a demux,
unless this is fixed in the HW platform.

The demux kABI only controls front-ends regarding to their connections with
demuxes; the kABI used to set the other front-end parameters, such as
tuning, are devined via the Digital TV Frontend kABI.

The functions that implement the abstract interface demux should be defined
static or module private and registered to the Demux core for external
access. It is not necessary to implement every function in the struct
&dmx_demux. For example, a demux interface might support Section filtering,
but not PES filtering. The kABI client is expected to check the value of any
function pointer before calling the function: the value of NULL means
that the function is not available.

Whenever the functions of the demux API modify shared data, the
possibilities of lost update and race condition problems should be
addressed, e.g. by protecting parts of code with mutexes.

Note that functions called from a bottom half context must not sleep.
Even a simple memory allocation without using %GFP_ATOMIC can result in a
kernel thread being put to sleep if swapping is needed. For example, the
Linux Kernel calls the functions of a network device interface from a
bottom half context. Thus, if a demux kABI function is called from network
device code, the function must not sleep.



Demux Callback API
------------------

Demux Callback
~~~~~~~~~~~~~~

This kernel-space API comprises the callback functions that deliver filtered
data to the demux client. Unlike the other DVB kABIs, these functions are
provided by the client and called from the demux code.

The function pointers of this abstract interface are not packed into a
structure as in the other demux APIs, because the callback functions are
registered and used independent of each other. As an example, it is possible
for the API client to provide several callback functions for receiving TS
packets and no callbacks for PES packets or sections.

The functions that implement the callback API need not be re-entrant: when
a demux driver calls one of these functions, the driver is not allowed to
call the function again before the original call returns. If a callback is
triggered by a hardware interrupt, it is recommended to use the Linux
bottom half mechanism or start a tasklet instead of making the callback
function call directly from a hardware interrupt.

This mechanism is implemented by dmx_ts_cb() and dmx_section_cb()
callbacks.


.. kernel-doc:: drivers/media/dvb-core/demux.h


Digital TV Conditional Access kABI
----------------------------------

.. kernel-doc:: drivers/media/dvb-core/dvb_ca_en50221.h


Remote Controller devices
-------------------------

.. kernel-doc:: include/media/rc-core.h

.. kernel-doc:: include/media/lirc_dev.h


Media Controller devices
------------------------

Media Controller
~~~~~~~~~~~~~~~~


The media controller userspace API is documented in DocBook format in
Documentation/DocBook/media/v4l/media-controller.xml. This document focus
on the kernel-side implementation of the media framework.

* Abstract media device model:

Discovering a device internal topology, and configuring it at runtime, is one
of the goals of the media framework. To achieve this, hardware devices are
modelled as an oriented graph of building blocks called entities connected
through pads.

An entity is a basic media hardware building block. It can correspond to
a large variety of logical blocks such as physical hardware devices
(CMOS sensor for instance), logical hardware devices (a building block
in a System-on-Chip image processing pipeline), DMA channels or physical
connectors.

A pad is a connection endpoint through which an entity can interact with
other entities. Data (not restricted to video) produced by an entity
flows from the entity's output to one or more entity inputs. Pads should
not be confused with physical pins at chip boundaries.

A link is a point-to-point oriented connection between two pads, either
on the same entity or on different entities. Data flows from a source
pad to a sink pad.


* Media device:

A media device is represented by a struct &media_device instance, defined in
include/media/media-device.h. Allocation of the structure is handled by the
media device driver, usually by embedding the &media_device instance in a
larger driver-specific structure.

Drivers register media device instances by calling
__media_device_register() via the macro media_device_register()
and unregistered by calling
media_device_unregister().

* Entities, pads and links:

- Entities

Entities are represented by a struct &media_entity instance, defined in
include/media/media-entity.h. The structure is usually embedded into a
higher-level structure, such as a v4l2_subdev or video_device instance,
although drivers can allocate entities directly.

Drivers initialize entity pads by calling
media_entity_pads_init().

Drivers register entities with a media device by calling
media_device_register_entity()
and unregistred by calling
media_device_unregister_entity().

- Interfaces

Interfaces are represented by a struct &media_interface instance, defined in
include/media/media-entity.h. Currently, only one type of interface is
defined: a device node. Such interfaces are represented by a struct
&media_intf_devnode.

Drivers initialize and create device node interfaces by calling
media_devnode_create()
and remove them by calling:
media_devnode_remove().

- Pads

Pads are represented by a struct &media_pad instance, defined in
include/media/media-entity.h. Each entity stores its pads in a pads array
managed by the entity driver. Drivers usually embed the array in a
driver-specific structure.

Pads are identified by their entity and their 0-based index in the pads
array.
Both information are stored in the &media_pad structure, making the
&media_pad pointer the canonical way to store and pass link references.

Pads have flags that describe the pad capabilities and state.

%MEDIA_PAD_FL_SINK indicates that the pad supports sinking data.
%MEDIA_PAD_FL_SOURCE indicates that the pad supports sourcing data.

NOTE: One and only one of %MEDIA_PAD_FL_SINK and %MEDIA_PAD_FL_SOURCE must
be set for each pad.

- Links

Links are represented by a struct &media_link instance, defined in
include/media/media-entity.h. There are two types of links:

1. pad to pad links:

Associate two entities via their PADs. Each entity has a list that points
to all links originating at or targeting any of its pads.
A given link is thus stored twice, once in the source entity and once in
the target entity.

Drivers create pad to pad links by calling:
media_create_pad_link() and remove with media_entity_remove_links().

2. interface to entity links:

Associate one interface to a Link.

Drivers create interface to entity links by calling:
media_create_intf_link() and remove with media_remove_intf_links().

NOTE:

Links can only be created after having both ends already created.

Links have flags that describe the link capabilities and state. The
valid values are described at media_create_pad_link() and
media_create_intf_link().

Graph traversal:

The media framework provides APIs to iterate over entities in a graph.

To iterate over all entities belonging to a media device, drivers can use
the media_device_for_each_entity macro, defined in
include/media/media-device.h.

struct media_entity *entity;

media_device_for_each_entity(entity, mdev) {
// entity will point to each entity in turn
...
}

Drivers might also need to iterate over all entities in a graph that can be
reached only through enabled links starting at a given entity. The media
framework provides a depth-first graph traversal API for that purpose.

Note that graphs with cycles (whether directed or undirected) are *NOT*
supported by the graph traversal API. To prevent infinite loops, the graph
traversal code limits the maximum depth to MEDIA_ENTITY_ENUM_MAX_DEPTH,
currently defined as 16.

Drivers initiate a graph traversal by calling
media_entity_graph_walk_start()

The graph structure, provided by the caller, is initialized to start graph
traversal at the given entity.

Drivers can then retrieve the next entity by calling
media_entity_graph_walk_next()

When the graph traversal is complete the function will return NULL.

Graph traversal can be interrupted at any moment. No cleanup function call
is required and the graph structure can be freed normally.

Helper functions can be used to find a link between two given pads, or a pad
connected to another pad through an enabled link
media_entity_find_link() and media_entity_remote_pad()

Use count and power handling:

Due to the wide differences between drivers regarding power management
needs, the media controller does not implement power management. However,
the &media_entity structure includes a use_count field that media drivers
can use to track the number of users of every entity for power management
needs.

The &media_entity.@use_count field is owned by media drivers and must not be
touched by entity drivers. Access to the field must be protected by the
&media_device.@graph_mutex lock.

Links setup:

Link properties can be modified at runtime by calling
media_entity_setup_link()

Pipelines and media streams:

When starting streaming, drivers must notify all entities in the pipeline to
prevent link states from being modified during streaming by calling
media_entity_pipeline_start().

The function will mark all entities connected to the given entity through
enabled links, either directly or indirectly, as streaming.

The &media_pipeline instance pointed to by the pipe argument will be stored
in every entity in the pipeline. Drivers should embed the &media_pipeline
structure in higher-level pipeline structures and can then access the
pipeline through the &media_entity pipe field.

Calls to media_entity_pipeline_start() can be nested. The pipeline pointer
must be identical for all nested calls to the function.

media_entity_pipeline_start() may return an error. In that case, it will
clean up any of the changes it did by itself.

When stopping the stream, drivers must notify the entities with
media_entity_pipeline_stop().

If multiple calls to media_entity_pipeline_start() have been made the same
number of media_entity_pipeline_stop() calls are required to stop streaming.
The &media_entity pipe field is reset to NULL on the last nested stop call.

Link configuration will fail with -%EBUSY by default if either end of the
link is a streaming entity. Links that can be modified while streaming must
be marked with the %MEDIA_LNK_FL_DYNAMIC flag.

If other operations need to be disallowed on streaming entities (such as
changing entities configuration parameters) drivers can explicitly check the
media_entity stream_count field to find out if an entity is streaming. This
operation must be done with the media_device graph_mutex held.

Link validation:

Link validation is performed by media_entity_pipeline_start() for any
entity which has sink pads in the pipeline. The
&media_entity.@link_validate() callback is used for that purpose. In
@link_validate() callback, entity driver should check that the properties of
the source pad of the connected entity and its own sink pad match. It is up
to the type of the entity (and in the end, the properties of the hardware)
what matching actually means.

Subsystems should facilitate link validation by providing subsystem specific
helper functions to provide easy access for commonly needed information, and
in the end provide a way to use driver-specific callbacks.

.. kernel-doc:: include/media/media-device.h

.. kernel-doc:: include/media/media-devnode.h

.. kernel-doc:: include/media/media-entity.h

