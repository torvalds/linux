=============
 Ring Buffer
=============

To handle communication between user space and kernel space, AMD GPUs use a
ring buffer design to feed the engines (GFX, Compute, SDMA, UVD, VCE, VCN, VPE,
etc.). See the figure below that illustrates how this communication works:

.. kernel-figure:: ring_buffers.svg

Ring buffers in the amdgpu work as a producer-consumer model, where userspace
acts as the producer, constantly filling the ring buffer with GPU commands to
be executed. Meanwhile, the GPU retrieves the information from the ring, parses
it, and distributes the specific set of instructions between the different
amdgpu blocks.

Notice from the diagram that the ring has a Read Pointer (rptr), which
indicates where the engine is currently reading packets from the ring, and a
Write Pointer (wptr), which indicates how many packets software has added to
the ring. When the rptr and wptr are equal, the ring is idle. When software
adds packets to the ring, it updates the wptr, this causes the engine to start
fetching and processing packets. As the engine processes packets, the rptr gets
updates until the rptr catches up to the wptr and they are equal again.

Usually, ring buffers in the driver have a limited size (search for occurrences
of `amdgpu_ring_init()`). One of the reasons for the small ring buffer size is
that CP (Command Processor) is capable of following addresses inserted into the
ring; this is illustrated in the image by the reference to the IB (Indirect
Buffer). The IB gives userspace the possibility to have an area in memory that
CP can read and feed the hardware with extra instructions.

All ASICs pre-GFX11 use what is called a kernel queue, which means
the ring is allocated in kernel space and has some restrictions, such as not
being able to be :ref:`preempted directly by the scheduler<amdgpu-mes>`. GFX11
and newer support kernel queues, but also provide a new mechanism named
:ref:`user queues<amdgpu-userq>`, where the queue is moved to the user space
and can be mapped and unmapped via the scheduler. In practice, both queues
insert user-space-generated GPU commands from different jobs into the requested
component ring.

Enforce Isolation
=================

.. note:: After reading this section, you might want to check the
   :ref:`Process Isolation<amdgpu-process-isolation>` page for more details.

Before examining the Enforce Isolation mechanism in the ring buffer context, it
is helpful to briefly discuss how instructions from the ring buffer are
processed in the graphics pipeline. Let’s expand on this topic by checking the
diagram below that illustrates the graphics pipeline:

.. kernel-figure:: gfx_pipeline_seq.svg

In terms of executing instructions, the GFX pipeline follows the sequence:
Shader Export (SX), Geometry Engine (GE), Shader Process or Input (SPI), Scan
Converter (SC), Primitive Assembler (PA), and cache manipulation (which may
vary across ASICs). Another common way to describe the pipeline is to use Pixel
Shader (PS), raster, and Vertex Shader (VS) to symbolize the two shader stages.
Now, with this pipeline in mind, let's assume that Job B causes a hang issue,
but Job C's instruction might already be executing, leading developers to
incorrectly identify Job C as the problematic one. This problem can be
mitigated on multiple levels; the diagram below illustrates how to minimize
part of this problem:

.. kernel-figure:: no_enforce_isolation.svg

Note from the diagram that there is no guarantee of order or a clear separation
between instructions, which is not a problem most of the time, and is also good
for performance. Furthermore, notice some circles between jobs in the diagram
that represent a **fence wait** used to avoid overlapping work in the ring. At
the end of the fence, a cache flush occurs, ensuring that when the next job
starts, it begins in a clean state and, if issues arise, the developer can
pinpoint the problematic process more precisely.

To increase the level of isolation between jobs, there is the "Enforce
Isolation" method described in the picture below:

.. kernel-figure:: enforce_isolation.svg

As shown in the diagram, enforcing isolation introduces ordering between
submissions, since the access to GFX/Compute is serialized, think about it as
single process at a time mode for gfx/compute. Notice that this approach has a
significant performance impact, as it allows only one job to submit commands at
a time. However, this option can help pinpoint the job that caused the problem.
Although enforcing isolation improves the situation, it does not fully resolve
the issue of precisely pinpointing bad jobs, since isolation might mask the
problem. In summary, identifying which job caused the issue may not be precise,
but enforcing isolation might help with the debugging.

Ring Operations
===============

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ring.c
   :internal:

