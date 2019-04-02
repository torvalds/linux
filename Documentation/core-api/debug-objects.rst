============================================
The object-lifetime deging infrastructure
============================================

:Author: Thomas Gleixner

Introduction
============

deobjects is a generic infrastructure to track the life time of
kernel objects and validate the operations on those.

deobjects is useful to check for the following error patterns:

-  Activation of uninitialized objects

-  Initialization of active objects

-  Usage of freed/destroyed objects

deobjects is not changing the data structure of the real object so it
can be compiled in with a minimal runtime impact and enabled on demand
with a kernel command line option.

Howto use deobjects
======================

A kernel subsystem needs to provide a data structure which describes the
object type and add calls into the de code at appropriate places. The
data structure to describe the object type needs at minimum the name of
the object type. Optional functions can and should be provided to fixup
detected problems so the kernel can continue to work and the de
information can be retrieved from a live system instead of hard core
deging with serial consoles and stack trace transcripts from the
monitor.

The de calls provided by deobjects are:

-  de_object_init

-  de_object_init_on_stack

-  de_object_activate

-  de_object_deactivate

-  de_object_destroy

-  de_object_free

-  de_object_assert_init

Each of these functions takes the address of the real object and a
pointer to the object type specific de description structure.

Each detected error is reported in the statistics and a limited number
of errors are printk'ed including a full stack trace.

The statistics are available via /sys/kernel/de/de_objects/stats.
They provide information about the number of warnings and the number of
successful fixups along with information about the usage of the internal
tracking objects and the state of the internal tracking objects pool.

De functions
===============

.. kernel-doc:: lib/deobjects.c
   :functions: de_object_init

This function is called whenever the initialization function of a real
object is called.

When the real object is already tracked by deobjects it is checked,
whether the object can be initialized. Initializing is not allowed for
active and destroyed objects. When deobjects detects an error, then
it calls the fixup_init function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real initialization of the object happens. E.g. it
can deactivate an active object in order to prevent damage to the
subsystem.

When the real object is not yet tracked by deobjects, deobjects
allocates a tracker object for the real object and sets the tracker
object state to ODE_STATE_INIT. It verifies that the object is not
on the callers stack. If it is on the callers stack then a limited
number of warnings including a full stack trace is printk'ed. The
calling code must use de_object_init_on_stack() and remove the
object before leaving the function which allocated it. See next section.

.. kernel-doc:: lib/deobjects.c
   :functions: de_object_init_on_stack

This function is called whenever the initialization function of a real
object which resides on the stack is called.

When the real object is already tracked by deobjects it is checked,
whether the object can be initialized. Initializing is not allowed for
active and destroyed objects. When deobjects detects an error, then
it calls the fixup_init function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real initialization of the object happens. E.g. it
can deactivate an active object in order to prevent damage to the
subsystem.

When the real object is not yet tracked by deobjects deobjects
allocates a tracker object for the real object and sets the tracker
object state to ODE_STATE_INIT. It verifies that the object is on
the callers stack.

An object which is on the stack must be removed from the tracker by
calling de_object_free() before the function which allocates the
object returns. Otherwise we keep track of stale objects.

.. kernel-doc:: lib/deobjects.c
   :functions: de_object_activate

This function is called whenever the activation function of a real
object is called.

When the real object is already tracked by deobjects it is checked,
whether the object can be activated. Activating is not allowed for
active and destroyed objects. When deobjects detects an error, then
it calls the fixup_activate function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real activation of the object happens. E.g. it can
deactivate an active object in order to prevent damage to the subsystem.

When the real object is not yet tracked by deobjects then the
fixup_activate function is called if available. This is necessary to
allow the legitimate activation of statically allocated and initialized
objects. The fixup function checks whether the object is valid and calls
the de_objects_init() function to initialize the tracking of this
object.

When the activation is legitimate, then the state of the associated
tracker object is set to ODE_STATE_ACTIVE.


.. kernel-doc:: lib/deobjects.c
   :functions: de_object_deactivate

This function is called whenever the deactivation function of a real
object is called.

When the real object is tracked by deobjects it is checked, whether
the object can be deactivated. Deactivating is not allowed for untracked
or destroyed objects.

When the deactivation is legitimate, then the state of the associated
tracker object is set to ODE_STATE_INACTIVE.

.. kernel-doc:: lib/deobjects.c
   :functions: de_object_destroy

This function is called to mark an object destroyed. This is useful to
prevent the usage of invalid objects, which are still available in
memory: either statically allocated objects or objects which are freed
later.

When the real object is tracked by deobjects it is checked, whether
the object can be destroyed. Destruction is not allowed for active and
destroyed objects. When deobjects detects an error, then it calls the
fixup_destroy function of the object type description structure if
provided by the caller. The fixup function can correct the problem
before the real destruction of the object happens. E.g. it can
deactivate an active object in order to prevent damage to the subsystem.

When the destruction is legitimate, then the state of the associated
tracker object is set to ODE_STATE_DESTROYED.

.. kernel-doc:: lib/deobjects.c
   :functions: de_object_free

This function is called before an object is freed.

When the real object is tracked by deobjects it is checked, whether
the object can be freed. Free is not allowed for active objects. When
deobjects detects an error, then it calls the fixup_free function of
the object type description structure if provided by the caller. The
fixup function can correct the problem before the real free of the
object happens. E.g. it can deactivate an active object in order to
prevent damage to the subsystem.

Note that de_object_free removes the object from the tracker. Later
usage of the object is detected by the other de checks.


.. kernel-doc:: lib/deobjects.c
   :functions: de_object_assert_init

This function is called to assert that an object has been initialized.

When the real object is not tracked by deobjects, it calls
fixup_assert_init of the object type description structure provided by
the caller, with the hardcoded object state ODE_NOT_AVAILABLE. The
fixup function can correct the problem by calling de_object_init
and other specific initializing functions.

When the real object is already tracked by deobjects it is ignored.

Fixup functions
===============

De object type description structure
---------------------------------------

.. kernel-doc:: include/linux/deobjects.h
   :internal:

fixup_init
-----------

This function is called from the de code whenever a problem in
de_object_init is detected. The function takes the address of the
object and the state which is currently recorded in the tracker.

Called from de_object_init when the object state is:

-  ODE_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note, that the function needs to call the de_object_init() function
again, after the damage has been repaired in order to keep the state
consistent.

fixup_activate
---------------

This function is called from the de code whenever a problem in
de_object_activate is detected.

Called from de_object_activate when the object state is:

-  ODE_STATE_NOTAVAILABLE

-  ODE_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note that the function needs to call the de_object_activate()
function again after the damage has been repaired in order to keep the
state consistent.

The activation of statically initialized objects is a special case. When
de_object_activate() has no tracked object for this object address
then fixup_activate() is called with object state
ODE_STATE_NOTAVAILABLE. The fixup function needs to check whether
this is a legitimate case of a statically initialized object or not. In
case it is it calls de_object_init() and de_object_activate()
to make the object known to the tracker and marked active. In this case
the function should return false because this is not a real fixup.

fixup_destroy
--------------

This function is called from the de code whenever a problem in
de_object_destroy is detected.

Called from de_object_destroy when the object state is:

-  ODE_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

fixup_free
-----------

This function is called from the de code whenever a problem in
de_object_free is detected. Further it can be called from the de
checks in kfree/vfree, when an active object is detected from the
de_check_no_obj_freed() sanity checks.

Called from de_object_free() or de_check_no_obj_freed() when
the object state is:

-  ODE_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

fixup_assert_init
-------------------

This function is called from the de code whenever a problem in
de_object_assert_init is detected.

Called from de_object_assert_init() with a hardcoded state
ODE_STATE_NOTAVAILABLE when the object is not found in the de
bucket.

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note, this function should make sure de_object_init() is called
before returning.

The handling of statically initialized objects is a special case. The
fixup function should check if this is a legitimate case of a statically
initialized object or not. In this case only de_object_init()
should be called to make the object known to the tracker. Then the
function should return false because this is not a real fixup.

Known s And Assumptions
==========================

None (knock on wood).
