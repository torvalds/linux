==================================
ALSA Co-processor Acceleration API
==================================

Jaroslav Kysela <perex@perex.cz>


Overview
========

There is a requirement to expose the audio hardware that accelerates various
tasks for user space such as sample rate converters, compressed
stream decoders, etc.

This is description for the API extension for the compress ALSA API which
is able to handle "tasks" that are not bound to real-time operations
and allows for the serialization of operations.

Requirements
============

The main requirements are:

- serialization of multiple tasks for user space to allow multiple
  operations without user space intervention

- separate buffers (input + output) for each operation

- expose buffers using mmap to user space

- signal user space when the task is finished (standard poll mechanism)

Design
======

A new direction SND_COMPRESS_ACCEL is introduced to identify
the passthrough API.

The API extension shares device enumeration and parameters handling from
the main compressed API. All other realtime streaming ioctls are deactivated
and a new set of task related ioctls are introduced. The standard
read/write/mmap I/O operations are not supported in the passthrough device.

Device ("stream") state handling is reduced to OPEN/SETUP. All other
states are not available for the passthrough mode.

Data I/O mechanism is using standard dma-buf interface with all advantages
like mmap, standard I/O, buffer sharing etc. One buffer is used for the
input data and second (separate) buffer is used for the output data. Each task
have separate I/O buffers.

For the buffering parameters, the fragments means a limit of allocated tasks
for given device. The fragment_size limits the input buffer size for the given
device. The output buffer size is determined by the driver (may be different
from the input buffer size).

State Machine
=============

The passthrough audio stream state machine is described below::

                                       +----------+
                                       |          |
                                       |   OPEN   |
                                       |          |
                                       +----------+
                                             |
                                             |
                                             | compr_set_params()
                                             |
                                             v
         all passthrough task ops      +----------+
  +------------------------------------|          |
  |                                    |   SETUP  |
  |                                    |
  |                                    +----------+
  |                                          |
  +------------------------------------------+


Passthrough operations (ioctls)
===============================

All operations are protected using stream->device->lock (mutex).

CREATE
------
Creates a set of input/output buffers. The input buffer size is
fragment_size. Allocates unique seqno.

The hardware drivers allocate internal 'struct dma_buf' for both input and
output buffers (using 'dma_buf_export()' function). The anonymous
file descriptors for those buffers are passed to user space.

FREE
----
Free a set of input/output buffers. If a task is active, the stop
operation is executed before. If seqno is zero, operation is executed for all
tasks.

START
-----
Starts (queues) a task. There are two cases of the task start - right after
the task is created. In this case, origin_seqno must be zero.
The second case is for reusing of already finished task. The origin_seqno
must identify the task to be reused. In both cases, a new seqno value
is allocated and returned to user space.

The prerequisite is that application filled input dma buffer with
new source data and set input_size to pass the real data size to the driver.

The order of data processing is preserved (first started job must be
finished at first).

If the multiple tasks require a state handling (e.g. resampling operation),
the user space may set SND_COMPRESS_TFLG_NEW_STREAM flag to mark the
start of the new stream data. It is useful to keep the allocated buffers
for the new operation rather using open/close mechanism.

STOP
----
Stop (dequeues) a task. If seqno is zero, operation is executed for all
tasks.

STATUS
------
Obtain the task status (active, finished). Also, the driver will set
the real output data size (valid area in the output buffer).

Credits
=======
- Shengjiu Wang <shengjiu.wang@gmail.com>
- Takashi Iwai <tiwai@suse.de>
- Vinod Koul <vkoul@kernel.org>
