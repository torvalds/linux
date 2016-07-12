.. -*- coding: utf-8; mode: rst -*-

.. _mmap:

******************************
Streaming I/O (Memory Mapping)
******************************

Input and output devices support this I/O method when the
``V4L2_CAP_STREAMING`` flag in the ``capabilities`` field of struct
:ref:`v4l2_capability <v4l2-capability>` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl is set. There are two
streaming methods, to determine if the memory mapping flavor is
supported applications must call the :ref:`VIDIOC_REQBUFS` ioctl
with the memory type set to ``V4L2_MEMORY_MMAP``.

Streaming is an I/O method where only pointers to buffers are exchanged
between application and driver, the data itself is not copied. Memory
mapping is primarily intended to map buffers in device memory into the
application's address space. Device memory can be for example the video
memory on a graphics card with a video capture add-on. However, being
the most efficient I/O method available for a long time, many other
drivers support streaming as well, allocating buffers in DMA-able main
memory.

A driver can support many sets of buffers. Each set is identified by a
unique buffer type value. The sets are independent and each set can hold
a different type of data. To access different sets at the same time
different file descriptors must be used. [#f1]_

To allocate device buffers applications call the
:ref:`VIDIOC_REQBUFS` ioctl with the desired number
of buffers and buffer type, for example ``V4L2_BUF_TYPE_VIDEO_CAPTURE``.
This ioctl can also be used to change the number of buffers or to free
the allocated memory, provided none of the buffers are still mapped.

Before applications can access the buffers they must map them into their
address space with the :ref:`mmap() <func-mmap>` function. The
location of the buffers in device memory can be determined with the
:ref:`VIDIOC_QUERYBUF` ioctl. In the single-planar
API case, the ``m.offset`` and ``length`` returned in a struct
:ref:`v4l2_buffer <v4l2-buffer>` are passed as sixth and second
parameter to the :ref:`mmap() <func-mmap>` function. When using the
multi-planar API, struct :ref:`v4l2_buffer <v4l2-buffer>` contains an
array of struct :ref:`v4l2_plane <v4l2-plane>` structures, each
containing its own ``m.offset`` and ``length``. When using the
multi-planar API, every plane of every buffer has to be mapped
separately, so the number of calls to :ref:`mmap() <func-mmap>` should
be equal to number of buffers times number of planes in each buffer. The
offset and length values must not be modified. Remember, the buffers are
allocated in physical memory, as opposed to virtual memory, which can be
swapped out to disk. Applications should free the buffers as soon as
possible with the :ref:`munmap() <func-munmap>` function.

Example: Mapping buffers in the single-planar API
=================================================

.. code-block:: c

    struct v4l2_requestbuffers reqbuf;
    struct {
	void *start;
	size_t length;
    } *buffers;
    unsigned int i;

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 20;

    if (-1 == ioctl (fd, VIDIOC_REQBUFS, &reqbuf)) {
	if (errno == EINVAL)
	    printf("Video capturing or mmap-streaming is not supported\\n");
	else
	    perror("VIDIOC_REQBUFS");

	exit(EXIT_FAILURE);
    }

    /* We want at least five buffers. */

    if (reqbuf.count < 5) {
	/* You may need to free the buffers here. */
	printf("Not enough buffer memory\\n");
	exit(EXIT_FAILURE);
    }

    buffers = calloc(reqbuf.count, sizeof(*buffers));
    assert(buffers != NULL);

    for (i = 0; i < reqbuf.count; i++) {
	struct v4l2_buffer buffer;

	memset(&buffer, 0, sizeof(buffer));
	buffer.type = reqbuf.type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = i;

	if (-1 == ioctl (fd, VIDIOC_QUERYBUF, &buffer)) {
	    perror("VIDIOC_QUERYBUF");
	    exit(EXIT_FAILURE);
	}

	buffers[i].length = buffer.length; /* remember for munmap() */

	buffers[i].start = mmap(NULL, buffer.length,
		    PROT_READ | PROT_WRITE, /* recommended */
		    MAP_SHARED,             /* recommended */
		    fd, buffer.m.offset);

	if (MAP_FAILED == buffers[i].start) {
	    /* If you do not exit here you should unmap() and free()
	       the buffers mapped so far. */
	    perror("mmap");
	    exit(EXIT_FAILURE);
	}
    }

    /* Cleanup. */

    for (i = 0; i < reqbuf.count; i++)
	munmap(buffers[i].start, buffers[i].length);


Example: Mapping buffers in the multi-planar API
================================================

.. code-block:: c

    struct v4l2_requestbuffers reqbuf;
    /* Our current format uses 3 planes per buffer */
    #define FMT_NUM_PLANES = 3

    struct {
	void *start[FMT_NUM_PLANES];
	size_t length[FMT_NUM_PLANES];
    } *buffers;
    unsigned int i, j;

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 20;

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
	if (errno == EINVAL)
	    printf("Video capturing or mmap-streaming is not supported\\n");
	else
	    perror("VIDIOC_REQBUFS");

	exit(EXIT_FAILURE);
    }

    /* We want at least five buffers. */

    if (reqbuf.count < 5) {
	/* You may need to free the buffers here. */
	printf("Not enough buffer memory\\n");
	exit(EXIT_FAILURE);
    }

    buffers = calloc(reqbuf.count, sizeof(*buffers));
    assert(buffers != NULL);

    for (i = 0; i < reqbuf.count; i++) {
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[FMT_NUM_PLANES];

	memset(&buffer, 0, sizeof(buffer));
	buffer.type = reqbuf.type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = i;
	/* length in struct v4l2_buffer in multi-planar API stores the size
	 * of planes array. */
	buffer.length = FMT_NUM_PLANES;
	buffer.m.planes = planes;

	if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
	    perror("VIDIOC_QUERYBUF");
	    exit(EXIT_FAILURE);
	}

	/* Every plane has to be mapped separately */
	for (j = 0; j < FMT_NUM_PLANES; j++) {
	    buffers[i].length[j] = buffer.m.planes[j].length; /* remember for munmap() */

	    buffers[i].start[j] = mmap(NULL, buffer.m.planes[j].length,
		     PROT_READ | PROT_WRITE, /* recommended */
		     MAP_SHARED,             /* recommended */
		     fd, buffer.m.planes[j].m.offset);

	    if (MAP_FAILED == buffers[i].start[j]) {
		/* If you do not exit here you should unmap() and free()
		   the buffers and planes mapped so far. */
		perror("mmap");
		exit(EXIT_FAILURE);
	    }
	}
    }

    /* Cleanup. */

    for (i = 0; i < reqbuf.count; i++)
	for (j = 0; j < FMT_NUM_PLANES; j++)
	    munmap(buffers[i].start[j], buffers[i].length[j]);

Conceptually streaming drivers maintain two buffer queues, an incoming
and an outgoing queue. They separate the synchronous capture or output
operation locked to a video clock from the application which is subject
to random disk or network delays and preemption by other processes,
thereby reducing the probability of data loss. The queues are organized
as FIFOs, buffers will be output in the order enqueued in the incoming
FIFO, and were captured in the order dequeued from the outgoing FIFO.

The driver may require a minimum number of buffers enqueued at all times
to function, apart of this no limit exists on the number of buffers
applications can enqueue in advance, or dequeue and process. They can
also enqueue in a different order than buffers have been dequeued, and
the driver can *fill* enqueued *empty* buffers in any order.  [#f2]_ The
index number of a buffer (struct :ref:`v4l2_buffer <v4l2-buffer>`
``index``) plays no role here, it only identifies the buffer.

Initially all mapped buffers are in dequeued state, inaccessible by the
driver. For capturing applications it is customary to first enqueue all
mapped buffers, then to start capturing and enter the read loop. Here
the application waits until a filled buffer can be dequeued, and
re-enqueues the buffer when the data is no longer needed. Output
applications fill and enqueue buffers, when enough buffers are stacked
up the output is started with :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`.
In the write loop, when the application runs out of free buffers, it
must wait until an empty buffer can be dequeued and reused.

To enqueue and dequeue a buffer applications use the :ref:`VIDIOC_QBUF`
and :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl. The status of a buffer
being mapped, enqueued, full or empty can be determined at any time
using the :ref:`VIDIOC_QUERYBUF` ioctl. Two methods exist to suspend
execution of the application until one or more buffers can be dequeued.
By default :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` blocks when no buffer is
in the outgoing queue. When the ``O_NONBLOCK`` flag was given to the
:ref:`open() <func-open>` function, :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>`
returns immediately with an ``EAGAIN`` error code when no buffer is
available. The :ref:`select() <func-select>` or :ref:`poll()
<func-poll>` functions are always available.

To start and stop capturing or output applications call the
:ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>` and :ref:`VIDIOC_STREAMOFF
<VIDIOC_STREAMON>` ioctl.

.. note:::ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>`
   removes all buffers from both queues as a side effect. Since there is
   no notion of doing anything "now" on a multitasking system, if an
   application needs to synchronize with another event it should examine
   the struct ::ref:`v4l2_buffer <v4l2-buffer>` ``timestamp`` of captured
   or outputted buffers.

Drivers implementing memory mapping I/O must support the
:ref:`VIDIOC_REQBUFS <VIDIOC_REQBUFS>`, :ref:`VIDIOC_QUERYBUF
<VIDIOC_QUERYBUF>`, :ref:`VIDIOC_QBUF <VIDIOC_QBUF>`, :ref:`VIDIOC_DQBUF
<VIDIOC_QBUF>`, :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`
and :ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>` ioctls, the :ref:`mmap()
<func-mmap>`, :ref:`munmap() <func-munmap>`, :ref:`select()
<func-select>` and :ref:`poll() <func-poll>` function. [#f3]_

[capture example]

.. [#f1]
   One could use one file descriptor and set the buffer type field
   accordingly when calling :ref:`VIDIOC_QBUF` etc.,
   but it makes the :ref:`select() <func-select>` function ambiguous. We also
   like the clean approach of one file descriptor per logical stream.
   Video overlay for example is also a logical stream, although the CPU
   is not needed for continuous operation.

.. [#f2]
   Random enqueue order permits applications processing images out of
   order (such as video codecs) to return buffers earlier, reducing the
   probability of data loss. Random fill order allows drivers to reuse
   buffers on a LIFO-basis, taking advantage of caches holding
   scatter-gather lists and the like.

.. [#f3]
   At the driver level :ref:`select() <func-select>` and :ref:`poll() <func-poll>` are
   the same, and :ref:`select() <func-select>` is too important to be optional.
   The rest should be evident.
