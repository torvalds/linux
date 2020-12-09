.. SPDX-License-Identifier: GPL-2.0

V4L2 File handlers
------------------

struct v4l2_fh provides a way to easily keep file handle specific
data that is used by the V4L2 framework.

.. attention::
	New drivers must use struct v4l2_fh
	since it is also used to implement priority handling
	(:ref:`VIDIOC_G_PRIORITY`).

The users of :c:type:`v4l2_fh` (in the V4L2 framework, not the driver) know
whether a driver uses :c:type:`v4l2_fh` as its ``file->private_data`` pointer
by testing the ``V4L2_FL_USES_V4L2_FH`` bit in :c:type:`video_device`->flags.
This bit is set whenever :c:func:`v4l2_fh_init` is called.

struct v4l2_fh is allocated as a part of the driver's own file handle
structure and ``file->private_data`` is set to it in the driver's ``open()``
function by the driver.

In many cases the struct v4l2_fh will be embedded in a larger
structure. In that case you should call:

#) :c:func:`v4l2_fh_init` and :c:func:`v4l2_fh_add` in ``open()``
#) :c:func:`v4l2_fh_del` and :c:func:`v4l2_fh_exit` in ``release()``

Drivers can extract their own file handle structure by using the container_of
macro.

Example:

.. code-block:: c

	struct my_fh {
		int blah;
		struct v4l2_fh fh;
	};

	...

	int my_open(struct file *file)
	{
		struct my_fh *my_fh;
		struct video_device *vfd;
		int ret;

		...

		my_fh = kzalloc(sizeof(*my_fh), GFP_KERNEL);

		...

		v4l2_fh_init(&my_fh->fh, vfd);

		...

		file->private_data = &my_fh->fh;
		v4l2_fh_add(&my_fh->fh);
		return 0;
	}

	int my_release(struct file *file)
	{
		struct v4l2_fh *fh = file->private_data;
		struct my_fh *my_fh = container_of(fh, struct my_fh, fh);

		...
		v4l2_fh_del(&my_fh->fh);
		v4l2_fh_exit(&my_fh->fh);
		kfree(my_fh);
		return 0;
	}

Below is a short description of the :c:type:`v4l2_fh` functions used:

:c:func:`v4l2_fh_init <v4l2_fh_init>`
(:c:type:`fh <v4l2_fh>`, :c:type:`vdev <video_device>`)


- Initialise the file handle. This **MUST** be performed in the driver's
  :c:type:`v4l2_file_operations`->open() handler.


:c:func:`v4l2_fh_add <v4l2_fh_add>`
(:c:type:`fh <v4l2_fh>`)

- Add a :c:type:`v4l2_fh` to :c:type:`video_device` file handle list.
  Must be called once the file handle is completely initialized.

:c:func:`v4l2_fh_del <v4l2_fh_del>`
(:c:type:`fh <v4l2_fh>`)

- Unassociate the file handle from :c:type:`video_device`. The file handle
  exit function may now be called.

:c:func:`v4l2_fh_exit <v4l2_fh_exit>`
(:c:type:`fh <v4l2_fh>`)

- Uninitialise the file handle. After uninitialisation the :c:type:`v4l2_fh`
  memory can be freed.


If struct v4l2_fh is not embedded, then you can use these helper functions:

:c:func:`v4l2_fh_open <v4l2_fh_open>`
(struct file \*filp)

- This allocates a struct v4l2_fh, initializes it and adds it to
  the struct video_device associated with the file struct.

:c:func:`v4l2_fh_release <v4l2_fh_release>`
(struct file \*filp)

- This deletes it from the struct video_device associated with the
  file struct, uninitialised the :c:type:`v4l2_fh` and frees it.

These two functions can be plugged into the v4l2_file_operation's ``open()``
and ``release()`` ops.

Several drivers need to do something when the first file handle is opened and
when the last file handle closes. Two helper functions were added to check
whether the :c:type:`v4l2_fh` struct is the only open filehandle of the
associated device node:

:c:func:`v4l2_fh_is_singular <v4l2_fh_is_singular>`
(:c:type:`fh <v4l2_fh>`)

-  Returns 1 if the file handle is the only open file handle, else 0.

:c:func:`v4l2_fh_is_singular_file <v4l2_fh_is_singular_file>`
(struct file \*filp)

- Same, but it calls v4l2_fh_is_singular with filp->private_data.


V4L2 fh functions and data structures
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: include/media/v4l2-fh.h
