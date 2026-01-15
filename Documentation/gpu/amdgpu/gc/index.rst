.. _amdgpu-gc:

========================================
 drm/amdgpu - Graphics and Compute (GC)
========================================

The relationship between the CPU and GPU can be described as the
producer-consumer problem, where the CPU fills out a buffer with operations
(producer) to be executed by the GPU (consumer). The requested operations in
the buffer are called **Command Packets**, which can be summarized as a
compressed way of transmitting command information to the graphics controller.

The component that acts as the front end between the CPU and the GPU is called
**Command Processor (CP)**. This component is responsible for providing greater
flexibility to the **Graphics and Compute (GC)** since CP makes it possible to
program various aspects of the GPU pipeline. CP also coordinates the
communication between the CPU and GPU via a mechanism named **Ring Buffers**,
where the CPU appends information to the buffer while the GPU removes
operations. CP is also responsible for handling **Indirect Buffers (IB)**.

For reference, internally the CP consists of several sub-blocks (CPC - CP
compute, CPG - CP graphics, and CPF - CP fetcher). Some of these acronyms
appear in register names, but this is more of an implementation detail and not
something that directly impacts driver programming or debugging.

Graphics (GFX) and Compute Microcontrollers
-------------------------------------------

GC is a large block, and as a result, it has multiple firmware associated with
it. Some of them are:

CP (Command Processor)
    The name for the hardware block that encompasses the front end of the
    GFX/Compute pipeline. Consists mainly of a bunch of microcontrollers
    (PFP, ME, CE, MEC). The firmware that runs on these microcontrollers
    provides the driver interface to interact with the GFX/Compute engine.

    MEC (MicroEngine Compute)
        This is the microcontroller that controls the compute queues on the
        GFX/compute engine.

    MES (MicroEngine Scheduler)
        This is the engine for managing queues. For more details check
        :ref:`MicroEngine Scheduler (MES) <amdgpu-mes>`.

RLC (RunList Controller)
    This is another microcontroller in the GFX/Compute engine. It handles
    power management related functionality within the GFX/Compute engine.
    The name is a vestige of old hardware where it was originally added
    and doesn't really have much relation to what the engine does now.

.. toctree::

   mes.rst
