.. SPDX-License-Identifier: GPL-2.0

==================================
Tracefs ring-buffer memory mapping
==================================

:Author: Vincent Donnefort <vdonnefort@google.com>

Overview
========
Tracefs ring-buffer memory map provides an efficient method to stream data
as no memory copy is necessary. The application mapping the ring-buffer becomes
then a consumer for that ring-buffer, in a similar fashion to trace_pipe.

Memory mapping setup
====================
The mapping works with a mmap() of the trace_pipe_raw interface.

The first system page of the mapping contains ring-buffer statistics and
description. It is referred to as the meta-page. One of the most important
fields of the meta-page is the reader. It contains the sub-buffer ID which can
be safely read by the mapper (see ring-buffer-design.rst).

The meta-page is followed by all the sub-buffers, ordered by ascending ID. It is
therefore effortless to know where the reader starts in the mapping:

.. code-block:: c

        reader_id = meta->reader->id;
        reader_offset = meta->meta_page_size + reader_id * meta->subbuf_size;

When the application is done with the current reader, it can get a new one using
the trace_pipe_raw ioctl() TRACE_MMAP_IOCTL_GET_READER. This ioctl also updates
the meta-page fields.

Limitations
===========
When a mapping is in place on a Tracefs ring-buffer, it is not possible to
either resize it (either by increasing the entire size of the ring-buffer or
each subbuf). It is also not possible to use snapshot and causes splice to copy
the ring buffer data instead of using the copyless swap from the ring buffer.

Concurrent readers (either another application mapping that ring-buffer or the
kernel with trace_pipe) are allowed but not recommended. They will compete for
the ring-buffer and the output is unpredictable, just like concurrent readers on
trace_pipe would be.

Example
=======

.. code-block:: c

        #include <fcntl.h>
        #include <stdio.h>
        #include <stdlib.h>
        #include <unistd.h>

        #include <linux/trace_mmap.h>

        #include <sys/mman.h>
        #include <sys/ioctl.h>

        #define TRACE_PIPE_RAW "/sys/kernel/tracing/per_cpu/cpu0/trace_pipe_raw"

        int main(void)
        {
                int page_size = getpagesize(), fd, reader_id;
                unsigned long meta_len, data_len;
                struct trace_buffer_meta *meta;
                void *map, *reader, *data;

                fd = open(TRACE_PIPE_RAW, O_RDONLY | O_NONBLOCK);
                if (fd < 0)
                        exit(EXIT_FAILURE);

                map = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, 0);
                if (map == MAP_FAILED)
                        exit(EXIT_FAILURE);

                meta = (struct trace_buffer_meta *)map;
                meta_len = meta->meta_page_size;

                printf("entries:        %llu\n", meta->entries);
                printf("overrun:        %llu\n", meta->overrun);
                printf("read:           %llu\n", meta->read);
                printf("nr_subbufs:     %u\n", meta->nr_subbufs);

                data_len = meta->subbuf_size * meta->nr_subbufs;
                data = mmap(NULL, data_len, PROT_READ, MAP_SHARED, fd, meta_len);
                if (data == MAP_FAILED)
                        exit(EXIT_FAILURE);

                if (ioctl(fd, TRACE_MMAP_IOCTL_GET_READER) < 0)
                        exit(EXIT_FAILURE);

                reader_id = meta->reader.id;
                reader = data + meta->subbuf_size * reader_id;

                printf("Current reader address: %p\n", reader);

                munmap(data, data_len);
                munmap(meta, meta_len);
                close (fd);

                return 0;
        }
