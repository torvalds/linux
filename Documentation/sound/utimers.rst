.. SPDX-License-Identifier: GPL-2.0

=======================
Userspace-driven timers
=======================

:Author: Ivan Orlov <ivan.orlov0322@gmail.com>

Preface
=======

This document describes the userspace-driven timers: virtual ALSA timers
which could be created and controlled by userspace applications using
IOCTL calls. Such timers could be useful when synchronizing audio
stream with timer sources which we don't have ALSA timers exported for
(e.g. PTP clocks), and when synchronizing the audio stream going through
two virtual sound devices using ``snd-aloop`` (for instance, when
we have a network application sending frames to one snd-aloop device,
and another sound application listening on the other end of snd-aloop).

Enabling userspace-driven timers
================================

The userspace-driven timers could be enabled in the kernel using the
``CONFIG_SND_UTIMER`` configuration option. It depends on the
``CONFIG_SND_TIMER`` option, so it also should be enabled.

Userspace-driven timers API
===========================

Userspace application can create a userspace-driven ALSA timer by
executing the ``SNDRV_TIMER_IOCTL_CREATE`` ioctl call on the
``/dev/snd/timer`` device file descriptor. The ``snd_timer_uinfo``
structure should be passed as an ioctl argument:

::

    struct snd_timer_uinfo {
        __u64 resolution;
        int fd;
        unsigned int id;
        unsigned char reserved[16];
    }

The ``resolution`` field sets the desired resolution in nanoseconds for
the virtual timer. ``resolution`` field simply provides an information
about the virtual timer, but does not affect the timing itself. ``id``
field gets overwritten by the ioctl, and the identifier you get in this
field after the call can be used as a timer subdevice number when
passing the timer to ``snd-aloop`` kernel module or other userspace
applications. There could be up to 128 userspace-driven timers in the
system at one moment of time, thus the id value ranges from 0 to 127.

Besides from overwriting the ``snd_timer_uinfo`` struct, ioctl stores
a timer file descriptor, which can be used to trigger the timer, in the
``fd`` field of the ``snd_timer_uinfo`` struct. Allocation of a file
descriptor for the timer guarantees that the timer can only be triggered
by the process which created it. The timer then can be triggered with
``SNDRV_TIMER_IOCTL_TRIGGER`` ioctl call on the timer file descriptor.

So, the example code for creating and triggering the timer would be:

::

    static struct snd_timer_uinfo utimer_info = {
        /* Timer is going to tick (presumably) every 1000000 ns */
        .resolution = 1000000ULL,
        .id = -1,
    };

    int timer_device_fd = open("/dev/snd/timer",  O_RDWR | O_CLOEXEC);

    if (ioctl(timer_device_fd, SNDRV_TIMER_IOCTL_CREATE, &utimer_info)) {
        perror("Failed to create the timer");
        return -1;
    }

    ...

    /*
     * Now we want to trigger the timer. Callbacks of all of the
     * timer instances binded to this timer will be executed after
     * this call.
     */
    ioctl(utimer_info.fd, SNDRV_TIMER_IOCTL_TRIGGER, NULL);

    ...

    /* Now, destroy the timer */
    close(timer_info.fd);


More detailed example of creating and ticking the timer could be found
in the utimer ALSA selftest.

Userspace-driven timers and snd-aloop
-------------------------------------

Userspace-driven timers could be easily used with ``snd-aloop`` module
when synchronizing two sound applications on both ends of the virtual
sound loopback. For instance, if one of the applications receives sound
frames from network and sends them to snd-aloop pcm device, and another
application listens for frames on the other snd-aloop pcm device, it
makes sense that the ALSA middle layer should initiate a data
transaction when the new period of data is received through network, but
not when the certain amount of jiffies elapses. Userspace-driven ALSA
timers could be used to achieve this.

To use userspace-driven ALSA timer as a timer source of snd-aloop, pass
the following string as the snd-aloop ``timer_source`` parameter:

::

  # modprobe snd-aloop timer_source="-1.4.<utimer_id>"

Where ``utimer_id`` is the id of the timer you created with
``SNDRV_TIMER_IOCTL_CREATE``, and ``4`` is the number of
userspace-driven timers device (``SNDRV_TIMER_GLOBAL_UDRIVEN``).

``resolution`` for the userspace-driven ALSA timer used with snd-aloop
should be calculated as ``1000000000ULL / frame_rate * period_size`` as
the timer is going to tick every time a new period of frames is ready.

After that, each time you trigger the timer with
``SNDRV_TIMER_IOCTL_TRIGGER`` the new period of data will be transferred
from one snd-aloop device to another.
