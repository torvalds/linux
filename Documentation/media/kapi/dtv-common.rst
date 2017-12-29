Digital TV Common functions
---------------------------

Math functions
~~~~~~~~~~~~~~

Provide some commonly-used math functions, usually required in order to
estimate signal strength and signal to noise measurements in dB.

.. kernel-doc:: drivers/media/dvb-core/dvb_math.h


DVB devices
~~~~~~~~~~~

Those functions are responsible for handling the DVB device nodes.

.. kernel-doc:: drivers/media/dvb-core/dvbdev.h

Digital TV Ring buffer
~~~~~~~~~~~~~~~~~~~~~~

Those routines implement ring buffers used to handle digital TV data and
copy it from/to userspace.

.. note::

  1) For performance reasons read and write routines don't check buffer sizes
     and/or number of bytes free/available. This has to be done before these
     routines are called. For example:

   .. code-block:: c

        /* write @buflen: bytes */
        free = dvb_ringbuffer_free(rbuf);
        if (free >= buflen)
                count = dvb_ringbuffer_write(rbuf, buffer, buflen);
        else
                /* do something */

        /* read min. 1000, max. @bufsize: bytes */
        avail = dvb_ringbuffer_avail(rbuf);
        if (avail >= 1000)
                count = dvb_ringbuffer_read(rbuf, buffer, min(avail, bufsize));
        else
                /* do something */

  2) If there is exactly one reader and one writer, there is no need
     to lock read or write operations.
     Two or more readers must be locked against each other.
     Flushing the buffer counts as a read operation.
     Resetting the buffer counts as a read and write operation.
     Two or more writers must be locked against each other.

.. kernel-doc:: drivers/media/dvb-core/dvb_ringbuffer.h
