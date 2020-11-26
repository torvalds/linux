.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _hist-v4l2:

***********************
Changes of the V4L2 API
***********************

Soon after the V4L API was added to the kernel it was criticised as too
inflexible. In August 1998 Bill Dirks proposed a number of improvements
and began to work on documentation, example drivers and applications.
With the help of other volunteers this eventually became the V4L2 API,
not just an extension but a replacement for the V4L API. However it took
another four years and two stable kernel releases until the new API was
finally accepted for inclusion into the kernel in its present form.

Early Versions
==============

1998-08-20: First version.

1998-08-27: The :c:func:`select()` function was introduced.

1998-09-10: New video standard interface.

1998-09-18: The ``VIDIOC_NONCAP`` ioctl was replaced by the otherwise
meaningless ``O_TRUNC`` :c:func:`open()` flag, and the
aliases ``O_NONCAP`` and ``O_NOIO`` were defined. Applications can set
this flag if they intend to access controls only, as opposed to capture
applications which need exclusive access. The ``VIDEO_STD_XXX``
identifiers are now ordinals instead of flags, and the
``video_std_construct()`` helper function takes id and
transmission arguments.

1998-09-28: Revamped video standard. Made video controls individually
enumerable.

1998-10-02: The ``id`` field was removed from
struct ``video_standard`` and the color subcarrier fields were
renamed. The :ref:`VIDIOC_QUERYSTD` ioctl was
renamed to :ref:`VIDIOC_ENUMSTD`,
:ref:`VIDIOC_G_INPUT <VIDIOC_G_INPUT>` to
:ref:`VIDIOC_ENUMINPUT`. A first draft of the
Codec API was released.

1998-11-08: Many minor changes. Most symbols have been renamed. Some
material changes to struct v4l2_capability.

1998-11-12: The read/write directon of some ioctls was misdefined.

1998-11-14: ``V4L2_PIX_FMT_RGB24`` changed to ``V4L2_PIX_FMT_BGR24``,
and ``V4L2_PIX_FMT_RGB32`` changed to ``V4L2_PIX_FMT_BGR32``. Audio
controls are now accessible with the
:ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` and
:ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctls under names starting
with ``V4L2_CID_AUDIO``. The ``V4L2_MAJOR`` define was removed from
``videodev.h`` since it was only used once in the ``videodev`` kernel
module. The ``YUV422`` and ``YUV411`` planar image formats were added.

1998-11-28: A few ioctl symbols changed. Interfaces for codecs and video
output devices were added.

1999-01-14: A raw VBI capture interface was added.

1999-01-19: The ``VIDIOC_NEXTBUF`` ioctl was removed.

V4L2 Version 0.16 1999-01-31
============================

1999-01-27: There is now one QBUF ioctl, VIDIOC_QWBUF and VIDIOC_QRBUF
are gone. VIDIOC_QBUF takes a v4l2_buffer as a parameter. Added
digital zoom (cropping) controls.

V4L2 Version 0.18 1999-03-16
============================

Added a v4l to V4L2 ioctl compatibility layer to videodev.c. Driver
writers, this changes how you implement your ioctl handler. See the
Driver Writer's Guide. Added some more control id codes.

V4L2 Version 0.19 1999-06-05
============================

1999-03-18: Fill in the category and catname fields of v4l2_queryctrl
objects before passing them to the driver. Required a minor change to
the VIDIOC_QUERYCTRL handlers in the sample drivers.

1999-03-31: Better compatibility for v4l memory capture ioctls. Requires
changes to drivers to fully support new compatibility features, see
Driver Writer's Guide and v4l2cap.c. Added new control IDs:
V4L2_CID_HFLIP, _VFLIP. Changed V4L2_PIX_FMT_YUV422P to _YUV422P,
and _YUV411P to _YUV411P.

1999-04-04: Added a few more control IDs.

1999-04-07: Added the button control type.

1999-05-02: Fixed a typo in videodev.h, and added the
V4L2_CTRL_FLAG_GRAYED (later V4L2_CTRL_FLAG_GRABBED) flag.

1999-05-20: Definition of VIDIOC_G_CTRL was wrong causing a
malfunction of this ioctl.

1999-06-05: Changed the value of V4L2_CID_WHITENESS.

V4L2 Version 0.20 (1999-09-10)
==============================

Version 0.20 introduced a number of changes which were *not backward
compatible* with 0.19 and earlier versions. Purpose of these changes was
to simplify the API, while making it more extensible and following
common Linux driver API conventions.

1. Some typos in ``V4L2_FMT_FLAG`` symbols were fixed. struct v4l2_clip
   was changed for compatibility with v4l. (1999-08-30)

2. ``V4L2_TUNER_SUB_LANG1`` was added. (1999-09-05)

3. All ioctl() commands that used an integer argument now take a pointer
   to an integer. Where it makes sense, ioctls will return the actual
   new value in the integer pointed to by the argument, a common
   convention in the V4L2 API. The affected ioctls are: VIDIOC_PREVIEW,
   VIDIOC_STREAMON, VIDIOC_STREAMOFF, VIDIOC_S_FREQ,
   VIDIOC_S_INPUT, VIDIOC_S_OUTPUT, VIDIOC_S_EFFECT. For example

   .. code-block:: c

       err = ioctl (fd, VIDIOC_XXX, V4L2_XXX);

   becomes

   .. code-block:: c

       int a = V4L2_XXX; err = ioctl(fd, VIDIOC_XXX, &a);

4. All the different get- and set-format commands were swept into one
   :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
   :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl taking a union and a
   type field selecting the union member as parameter. Purpose is to
   simplify the API by eliminating several ioctls and to allow new and
   driver private data streams without adding new ioctls.

   This change obsoletes the following ioctls: ``VIDIOC_S_INFMT``,
   ``VIDIOC_G_INFMT``, ``VIDIOC_S_OUTFMT``, ``VIDIOC_G_OUTFMT``,
   ``VIDIOC_S_VBIFMT`` and ``VIDIOC_G_VBIFMT``. The image format
   struct v4l2_format was renamed to struct v4l2_pix_format, while
   struct v4l2_format is now the envelopping structure
   for all format negotiations.

5. Similar to the changes above, the ``VIDIOC_G_PARM`` and
   ``VIDIOC_S_PARM`` ioctls were merged with ``VIDIOC_G_OUTPARM`` and
   ``VIDIOC_S_OUTPARM``. A ``type`` field in the new struct v4l2_streamparm
   selects the respective union member.

   This change obsoletes the ``VIDIOC_G_OUTPARM`` and
   ``VIDIOC_S_OUTPARM`` ioctls.

6. Control enumeration was simplified, and two new control flags were
   introduced and one dropped. The ``catname`` field was replaced by a
   ``group`` field.

   Drivers can now flag unsupported and temporarily unavailable controls
   with ``V4L2_CTRL_FLAG_DISABLED`` and ``V4L2_CTRL_FLAG_GRABBED``
   respectively. The ``group`` name indicates a possibly narrower
   classification than the ``category``. In other words, there may be
   multiple groups within a category. Controls within a group would
   typically be drawn within a group box. Controls in different
   categories might have a greater separation, or may even appear in
   separate windows.

7. The struct v4l2_buffer ``timestamp`` was
   changed to a 64 bit integer, containing the sampling or output time
   of the frame in nanoseconds. Additionally timestamps will be in
   absolute system time, not starting from zero at the beginning of a
   stream. The data type name for timestamps is stamp_t, defined as a
   signed 64-bit integer. Output devices should not send a buffer out
   until the time in the timestamp field has arrived. I would like to
   follow SGI's lead, and adopt a multimedia timestamping system like
   their UST (Unadjusted System Time). See
   http://web.archive.org/web/\*/http://reality.sgi.com
   /cpirazzi_engr/lg/time/intro.html. UST uses timestamps that are
   64-bit signed integers (not struct timeval's) and given in nanosecond
   units. The UST clock starts at zero when the system is booted and
   runs continuously and uniformly. It takes a little over 292 years for
   UST to overflow. There is no way to set the UST clock. The regular
   Linux time-of-day clock can be changed periodically, which would
   cause errors if it were being used for timestamping a multimedia
   stream. A real UST style clock will require some support in the
   kernel that is not there yet. But in anticipation, I will change the
   timestamp field to a 64-bit integer, and I will change the
   v4l2_masterclock_gettime() function (used only by drivers) to
   return a 64-bit integer.

8. A ``sequence`` field was added to struct v4l2_buffer. The ``sequence``
   field counts captured frames, it is ignored by output devices. When a
   capture driver drops a frame, the sequence number of that frame is skipped.

V4L2 Version 0.20 incremental changes
=====================================

1999-12-23: In struct v4l2_vbi_format the
``reserved1`` field became ``offset``. Previously drivers were required
to clear the ``reserved1`` field.

2000-01-13: The ``V4L2_FMT_FLAG_NOT_INTERLACED`` flag was added.

2000-07-31: The ``linux/poll.h`` header is now included by
``videodev.h`` for compatibility with the original ``videodev.h`` file.

2000-11-20: ``V4L2_TYPE_VBI_OUTPUT`` and ``V4L2_PIX_FMT_Y41P`` were
added.

2000-11-25: ``V4L2_TYPE_VBI_INPUT`` was added.

2000-12-04: A couple typos in symbol names were fixed.

2001-01-18: To avoid namespace conflicts the ``fourcc`` macro defined in
the ``videodev.h`` header file was renamed to ``v4l2_fourcc``.

2001-01-25: A possible driver-level compatibility problem between the
``videodev.h`` file in Linux 2.4.0 and the ``videodev.h`` file included
in the ``videodevX`` patch was fixed. Users of an earlier version of
``videodevX`` on Linux 2.4.0 should recompile their V4L and V4L2
drivers.

2001-01-26: A possible kernel-level incompatibility between the
``videodev.h`` file in the ``videodevX`` patch and the ``videodev.h``
file in Linux 2.2.x with devfs patches applied was fixed.

2001-03-02: Certain V4L ioctls which pass data in both direction
although they are defined with read-only parameter, did not work
correctly through the backward compatibility layer. [Solution?]

2001-04-13: Big endian 16-bit RGB formats were added.

2001-09-17: New YUV formats and the
:ref:`VIDIOC_G_FREQUENCY <VIDIOC_G_FREQUENCY>` and
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctls were added.
(The old ``VIDIOC_G_FREQ`` and ``VIDIOC_S_FREQ`` ioctls did not take
multiple tuners into account.)

2000-09-18: ``V4L2_BUF_TYPE_VBI`` was added. This may *break
compatibility* as the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctls may fail now if the
struct ``v4l2_fmt`` ``type`` field does not contain
``V4L2_BUF_TYPE_VBI``. In the documentation of the struct v4l2_vbi_format`,
the ``offset`` field the ambiguous phrase "rising edge" was changed to
"leading edge".

V4L2 Version 0.20 2000-11-23
============================

A number of changes were made to the raw VBI interface.

1. Figures clarifying the line numbering scheme were added to the V4L2
   API specification. The ``start``\ [0] and ``start``\ [1] fields no
   longer count line numbers beginning at zero. Rationale: a) The
   previous definition was unclear. b) The ``start``\ [] values are
   ordinal numbers. c) There is no point in inventing a new line
   numbering scheme. We now use line number as defined by ITU-R, period.
   Compatibility: Add one to the start values. Applications depending on
   the previous semantics may not function correctly.

2. The restriction "count[0] > 0 and count[1] > 0" has been relaxed to
   "(count[0] + count[1]) > 0". Rationale: Drivers may allocate
   resources at scan line granularity and some data services are
   transmitted only on the first field. The comment that both ``count``
   values will usually be equal is misleading and pointless and has been
   removed. This change *breaks compatibility* with earlier versions:
   Drivers may return ``EINVAL``, applications may not function correctly.

3. Drivers are again permitted to return negative (unknown) start values
   as proposed earlier. Why this feature was dropped is unclear. This
   change may *break compatibility* with applications depending on the
   start values being positive. The use of ``EBUSY`` and ``EINVAL``
   error codes with the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl was
   clarified. The ``EBUSY`` error code was finally documented, and the
   ``reserved2`` field which was previously mentioned only in the
   ``videodev.h`` header file.

4. New buffer types ``V4L2_TYPE_VBI_INPUT`` and ``V4L2_TYPE_VBI_OUTPUT``
   were added. The former is an alias for the old ``V4L2_TYPE_VBI``, the
   latter was missing in the ``videodev.h`` file.

V4L2 Version 0.20 2002-07-25
============================

Added sliced VBI interface proposal.

V4L2 in Linux 2.5.46, 2002-10
=============================

Around October-November 2002, prior to an announced feature freeze of
Linux 2.5, the API was revised, drawing from experience with V4L2 0.20.
This unnamed version was finally merged into Linux 2.5.46.

1.  As specified in :ref:`related`, drivers must make related device
    functions available under all minor device numbers.

2.  The :c:func:`open()` function requires access mode
    ``O_RDWR`` regardless of the device type. All V4L2 drivers
    exchanging data with applications must support the ``O_NONBLOCK``
    flag. The ``O_NOIO`` flag, a V4L2 symbol which aliased the
    meaningless ``O_TRUNC`` to indicate accesses without data exchange
    (panel applications) was dropped. Drivers must stay in "panel mode"
    until the application attempts to initiate a data exchange, see
    :ref:`open`.

3.  The struct v4l2_capability changed
    dramatically. Note that also the size of the structure changed,
    which is encoded in the ioctl request code, thus older V4L2 devices
    will respond with an ``EINVAL`` error code to the new
    :ref:`VIDIOC_QUERYCAP` ioctl.

    There are new fields to identify the driver, a new RDS device
    function ``V4L2_CAP_RDS_CAPTURE``, the ``V4L2_CAP_AUDIO`` flag
    indicates if the device has any audio connectors, another I/O
    capability ``V4L2_CAP_ASYNCIO`` can be flagged. In response to these
    changes the ``type`` field became a bit set and was merged into the
    ``flags`` field. ``V4L2_FLAG_TUNER`` was renamed to
    ``V4L2_CAP_TUNER``, ``V4L2_CAP_VIDEO_OVERLAY`` replaced
    ``V4L2_FLAG_PREVIEW`` and ``V4L2_CAP_VBI_CAPTURE`` and
    ``V4L2_CAP_VBI_OUTPUT`` replaced ``V4L2_FLAG_DATA_SERVICE``.
    ``V4L2_FLAG_READ`` and ``V4L2_FLAG_WRITE`` were merged into
    ``V4L2_CAP_READWRITE``.

    The redundant fields ``inputs``, ``outputs`` and ``audios`` were
    removed. These properties can be determined as described in
    :ref:`video` and :ref:`audio`.

    The somewhat volatile and therefore barely useful fields
    ``maxwidth``, ``maxheight``, ``minwidth``, ``minheight``,
    ``maxframerate`` were removed. This information is available as
    described in :ref:`format` and :ref:`standard`.

    ``V4L2_FLAG_SELECT`` was removed. We believe the select() function
    is important enough to require support of it in all V4L2 drivers
    exchanging data with applications. The redundant
    ``V4L2_FLAG_MONOCHROME`` flag was removed, this information is
    available as described in :ref:`format`.

4.  In struct v4l2_input the ``assoc_audio``
    field and the ``capability`` field and its only flag
    ``V4L2_INPUT_CAP_AUDIO`` was replaced by the new ``audioset`` field.
    Instead of linking one video input to one audio input this field
    reports all audio inputs this video input combines with.

    New fields are ``tuner`` (reversing the former link from tuners to
    video inputs), ``std`` and ``status``.

    Accordingly struct v4l2_output lost its
    ``capability`` and ``assoc_audio`` fields. ``audioset``,
    ``modulator`` and ``std`` where added instead.

5.  The struct v4l2_audio field ``audio`` was
    renamed to ``index``, for consistency with other structures. A new
    capability flag ``V4L2_AUDCAP_STEREO`` was added to indicated if the
    audio input in question supports stereo sound.
    ``V4L2_AUDCAP_EFFECTS`` and the corresponding ``V4L2_AUDMODE`` flags
    where removed. This can be easily implemented using controls.
    (However the same applies to AVL which is still there.)

    Again for consistency the struct v4l2_audioout field ``audio`` was renamed
    to ``index``.

6.  The struct v4l2_tuner ``input`` field was
    replaced by an ``index`` field, permitting devices with multiple
    tuners. The link between video inputs and tuners is now reversed,
    inputs point to their tuner. The ``std`` substructure became a
    simple set (more about this below) and moved into struct v4l2_input.
    A ``type`` field was added.

    Accordingly in struct v4l2_modulator the
    ``output`` was replaced by an ``index`` field.

    In struct v4l2_frequency the ``port``
    field was replaced by a ``tuner`` field containing the respective
    tuner or modulator index number. A tuner ``type`` field was added
    and the ``reserved`` field became larger for future extensions
    (satellite tuners in particular).

7.  The idea of completely transparent video standards was dropped.
    Experience showed that applications must be able to work with video
    standards beyond presenting the user a menu. Instead of enumerating
    supported standards with an ioctl applications can now refer to
    standards by :ref:`v4l2_std_id <v4l2-std-id>` and symbols
    defined in the ``videodev2.h`` header file. For details see
    :ref:`standard`. The :ref:`VIDIOC_G_STD <VIDIOC_G_STD>` and
    :ref:`VIDIOC_S_STD <VIDIOC_G_STD>` now take a pointer to this
    type as argument. :ref:`VIDIOC_QUERYSTD` was
    added to autodetect the received standard, if the hardware has this
    capability. In struct v4l2_standard an
    ``index`` field was added for
    :ref:`VIDIOC_ENUMSTD`. A
    :ref:`v4l2_std_id <v4l2-std-id>` field named ``id`` was added as
    machine readable identifier, also replacing the ``transmission``
    field. The misleading ``framerate`` field was renamed to
    ``frameperiod``. The now obsolete ``colorstandard`` information,
    originally needed to distguish between variations of standards, were
    removed.

    Struct ``v4l2_enumstd`` ceased to be.
    :ref:`VIDIOC_ENUMSTD` now takes a pointer to a
    struct v4l2_standard directly. The
    information which standards are supported by a particular video
    input or output moved into struct v4l2_input
    and struct v4l2_output fields named ``std``,
    respectively.

8.  The struct :ref:`v4l2_queryctrl <v4l2-queryctrl>` fields
    ``category`` and ``group`` did not catch on and/or were not
    implemented as expected and therefore removed.

9.  The :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl was added to
    negotiate data formats as with
    :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`, but without the overhead of
    programming the hardware and regardless of I/O in progress.

    In struct v4l2_format the ``fmt`` union was
    extended to contain struct v4l2_window. All
    image format negotiations are now possible with ``VIDIOC_G_FMT``,
    ``VIDIOC_S_FMT`` and ``VIDIOC_TRY_FMT``; ioctl. The ``VIDIOC_G_WIN``
    and ``VIDIOC_S_WIN`` ioctls to prepare for a video overlay were
    removed. The ``type`` field changed to type enum v4l2_buf_type and
    the buffer type names changed as follows.


    .. flat-table::
	:header-rows:  1
	:stub-columns: 0

	* - Old defines
	  - enum v4l2_buf_type
	* - ``V4L2_BUF_TYPE_CAPTURE``
	  - ``V4L2_BUF_TYPE_VIDEO_CAPTURE``
	* - ``V4L2_BUF_TYPE_CODECIN``
	  - Omitted for now
	* - ``V4L2_BUF_TYPE_CODECOUT``
	  - Omitted for now
	* - ``V4L2_BUF_TYPE_EFFECTSIN``
	  - Omitted for now
	* - ``V4L2_BUF_TYPE_EFFECTSIN2``
	  - Omitted for now
	* - ``V4L2_BUF_TYPE_EFFECTSOUT``
	  - Omitted for now
	* - ``V4L2_BUF_TYPE_VIDEOOUT``
	  - ``V4L2_BUF_TYPE_VIDEO_OUTPUT``
	* - ``-``
	  - ``V4L2_BUF_TYPE_VIDEO_OVERLAY``
	* - ``-``
	  - ``V4L2_BUF_TYPE_VBI_CAPTURE``
	* - ``-``
	  - ``V4L2_BUF_TYPE_VBI_OUTPUT``
	* - ``-``
	  - ``V4L2_BUF_TYPE_SLICED_VBI_CAPTURE``
	* - ``-``
	  - ``V4L2_BUF_TYPE_SLICED_VBI_OUTPUT``
	* - ``V4L2_BUF_TYPE_PRIVATE_BASE``
	  - ``V4L2_BUF_TYPE_PRIVATE`` (but this is deprecated)

10. In struct v4l2_fmtdesc a enum v4l2_buf_type field named ``type`` was
    added as in struct v4l2_format. The ``VIDIOC_ENUM_FBUFFMT`` ioctl is no
    longer needed and was removed. These calls can be replaced by
    :ref:`VIDIOC_ENUM_FMT` with type ``V4L2_BUF_TYPE_VIDEO_OVERLAY``.

11. In struct v4l2_pix_format the ``depth``
    field was removed, assuming applications which recognize the format
    by its four-character-code already know the color depth, and others
    do not care about it. The same rationale lead to the removal of the
    ``V4L2_FMT_FLAG_COMPRESSED`` flag. The
    ``V4L2_FMT_FLAG_SWCONVECOMPRESSED`` flag was removed because drivers
    are not supposed to convert images in kernel space. A user library
    of conversion functions should be provided instead. The
    ``V4L2_FMT_FLAG_BYTESPERLINE`` flag was redundant. Applications can
    set the ``bytesperline`` field to zero to get a reasonable default.
    Since the remaining flags were replaced as well, the ``flags`` field
    itself was removed.

    The interlace flags were replaced by a enum v4l2_field value in a
    newly added ``field`` field.

    .. flat-table::
	:header-rows:  1
	:stub-columns: 0

	* - Old flag
	  - enum v4l2_field
	* - ``V4L2_FMT_FLAG_NOT_INTERLACED``
	  - ?
	* - ``V4L2_FMT_FLAG_INTERLACED`` = ``V4L2_FMT_FLAG_COMBINED``
	  - ``V4L2_FIELD_INTERLACED``
	* - ``V4L2_FMT_FLAG_TOPFIELD`` = ``V4L2_FMT_FLAG_ODDFIELD``
	  - ``V4L2_FIELD_TOP``
	* - ``V4L2_FMT_FLAG_BOTFIELD`` = ``V4L2_FMT_FLAG_EVENFIELD``
	  - ``V4L2_FIELD_BOTTOM``
	* - ``-``
	  - ``V4L2_FIELD_SEQ_TB``
	* - ``-``
	  - ``V4L2_FIELD_SEQ_BT``
	* - ``-``
	  - ``V4L2_FIELD_ALTERNATE``

    The color space flags were replaced by a enum v4l2_colorspace value in
    a newly added ``colorspace`` field, where one of
    ``V4L2_COLORSPACE_SMPTE170M``, ``V4L2_COLORSPACE_BT878``,
    ``V4L2_COLORSPACE_470_SYSTEM_M`` or
    ``V4L2_COLORSPACE_470_SYSTEM_BG`` replaces ``V4L2_FMT_CS_601YUV``.

12. In struct v4l2_requestbuffers the
    ``type`` field was properly defined as enum v4l2_buf_type. Buffer types
    changed as mentioned above. A new ``memory`` field of type
    enum v4l2_memory was added to distinguish between
    I/O methods using buffers allocated by the driver or the
    application. See :ref:`io` for details.

13. In struct v4l2_buffer the ``type`` field was
    properly defined as enum v4l2_buf_type.
    Buffer types changed as mentioned above. A ``field`` field of type
    enum v4l2_field was added to indicate if a
    buffer contains a top or bottom field. The old field flags were
    removed. Since no unadjusted system time clock was added to the
    kernel as planned, the ``timestamp`` field changed back from type
    stamp_t, an unsigned 64 bit integer expressing the sample time in
    nanoseconds, to struct timeval. With the addition
    of a second memory mapping method the ``offset`` field moved into
    union ``m``, and a new ``memory`` field of type enum v4l2_memory
    was added to distinguish between
    I/O methods. See :ref:`io` for details.

    The ``V4L2_BUF_REQ_CONTIG`` flag was used by the V4L compatibility
    layer, after changes to this code it was no longer needed. The
    ``V4L2_BUF_ATTR_DEVICEMEM`` flag would indicate if the buffer was
    indeed allocated in device memory rather than DMA-able system
    memory. It was barely useful and so was removed.

14. In struct v4l2_framebuffer the
    ``base[3]`` array anticipating double- and triple-buffering in
    off-screen video memory, however without defining a synchronization
    mechanism, was replaced by a single pointer. The
    ``V4L2_FBUF_CAP_SCALEUP`` and ``V4L2_FBUF_CAP_SCALEDOWN`` flags were
    removed. Applications can determine this capability more accurately
    using the new cropping and scaling interface. The
    ``V4L2_FBUF_CAP_CLIPPING`` flag was replaced by
    ``V4L2_FBUF_CAP_LIST_CLIPPING`` and
    ``V4L2_FBUF_CAP_BITMAP_CLIPPING``.

15. In struct v4l2_clip the ``x``, ``y``,
    ``width`` and ``height`` field moved into a ``c`` substructure of
    type struct v4l2_rect. The ``x`` and ``y``
    fields were renamed to ``left`` and ``top``, i. e. offsets to a
    context dependent origin.

16. In struct v4l2_window the ``x``, ``y``,
    ``width`` and ``height`` field moved into a ``w`` substructure as
    above. A ``field`` field of type enum v4l2_field was added to
    distinguish between field and frame (interlaced) overlay.

17. The digital zoom interface, including struct ``v4l2_zoomcap``,
    struct ``v4l2_zoom``, ``V4L2_ZOOM_NONCAP`` and
    ``V4L2_ZOOM_WHILESTREAMING`` was replaced by a new cropping and
    scaling interface. The previously unused
    struct v4l2_cropcap and struct v4l2_crop
    where redefined for this purpose. See :ref:`crop` for details.

18. In struct v4l2_vbi_format the
    ``SAMPLE_FORMAT`` field now contains a four-character-code as used
    to identify video image formats and ``V4L2_PIX_FMT_GREY`` replaces
    the ``V4L2_VBI_SF_UBYTE`` define. The ``reserved`` field was
    extended.

19. In struct v4l2_captureparm the type of
    the ``timeperframe`` field changed from unsigned long to
    struct v4l2_fract. This allows the accurate
    expression of multiples of the NTSC-M frame rate 30000 / 1001. A new
    field ``readbuffers`` was added to control the driver behaviour in
    read I/O mode.

    Similar changes were made to struct v4l2_outputparm.

20. The struct ``v4l2_performance`` and
    ``VIDIOC_G_PERF`` ioctl were dropped. Except when using the
    :ref:`read/write I/O method <rw>`, which is limited anyway, this
    information is already available to applications.

21. The example transformation from RGB to YCbCr color space in the old
    V4L2 documentation was inaccurate, this has been corrected in
    :ref:`pixfmt`.

V4L2 2003-06-19
===============

1. A new capability flag ``V4L2_CAP_RADIO`` was added for radio devices.
   Prior to this change radio devices would identify solely by having
   exactly one tuner whose type field reads ``V4L2_TUNER_RADIO``.

2. An optional driver access priority mechanism was added, see
   :ref:`app-pri` for details.

3. The audio input and output interface was found to be incomplete.

   Previously the :ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` ioctl would
   enumerate the available audio inputs. An ioctl to determine the
   current audio input, if more than one combines with the current video
   input, did not exist. So ``VIDIOC_G_AUDIO`` was renamed to
   ``VIDIOC_G_AUDIO_OLD``, this ioctl was removed on Kernel 2.6.39. The
   :ref:`VIDIOC_ENUMAUDIO` ioctl was added to
   enumerate audio inputs, while
   :ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` now reports the current
   audio input.

   The same changes were made to
   :ref:`VIDIOC_G_AUDOUT <VIDIOC_G_AUDOUT>` and
   :ref:`VIDIOC_ENUMAUDOUT <VIDIOC_ENUMAUDOUT>`.

   Until further the "videodev" module will automatically translate
   between the old and new ioctls, but drivers and applications must be
   updated to successfully compile again.

4. The :ref:`VIDIOC_OVERLAY` ioctl was incorrectly
   defined with write-read parameter. It was changed to write-only,
   while the write-read version was renamed to ``VIDIOC_OVERLAY_OLD``.
   The old ioctl was removed on Kernel 2.6.39. Until further the
   "videodev" kernel module will automatically translate to the new
   version, so drivers must be recompiled, but not applications.

5. :ref:`overlay` incorrectly stated that clipping rectangles define
   regions where the video can be seen. Correct is that clipping
   rectangles define regions where *no* video shall be displayed and so
   the graphics surface can be seen.

6. The :ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` and
   :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctls were defined with
   write-only parameter, inconsistent with other ioctls modifying their
   argument. They were changed to write-read, while a ``_OLD`` suffix
   was added to the write-only versions. The old ioctls were removed on
   Kernel 2.6.39. Drivers and applications assuming a constant parameter
   need an update.

V4L2 2003-11-05
===============

1. In :ref:`pixfmt-rgb` the following pixel formats were incorrectly
   transferred from Bill Dirks' V4L2 specification. Descriptions below
   refer to bytes in memory, in ascending address order.


   .. flat-table::
       :header-rows:  1
       :stub-columns: 0

       * - Symbol
	 - In this document prior to revision 0.5
	 - Corrected
       * - ``V4L2_PIX_FMT_RGB24``
	 - B, G, R
	 - R, G, B
       * - ``V4L2_PIX_FMT_BGR24``
	 - R, G, B
	 - B, G, R
       * - ``V4L2_PIX_FMT_RGB32``
	 - B, G, R, X
	 - R, G, B, X
       * - ``V4L2_PIX_FMT_BGR32``
	 - R, G, B, X
	 - B, G, R, X

   The ``V4L2_PIX_FMT_BGR24`` example was always correct.

   In :ref:`v4l-image-properties` the mapping of the V4L
   ``VIDEO_PALETTE_RGB24`` and ``VIDEO_PALETTE_RGB32`` formats to V4L2
   pixel formats was accordingly corrected.

2. Unrelated to the fixes above, drivers may still interpret some V4L2
   RGB pixel formats differently. These issues have yet to be addressed,
   for details see :ref:`pixfmt-rgb`.

V4L2 in Linux 2.6.6, 2004-05-09
===============================

1. The :ref:`VIDIOC_CROPCAP` ioctl was incorrectly
   defined with read-only parameter. It is now defined as write-read
   ioctl, while the read-only version was renamed to
   ``VIDIOC_CROPCAP_OLD``. The old ioctl was removed on Kernel 2.6.39.

V4L2 in Linux 2.6.8
===================

1. A new field ``input`` (former ``reserved[0]``) was added to the
   struct v4l2_buffer. Purpose of this
   field is to alternate between video inputs (e. g. cameras) in step
   with the video capturing process. This function must be enabled with
   the new ``V4L2_BUF_FLAG_INPUT`` flag. The ``flags`` field is no
   longer read-only.

V4L2 spec erratum 2004-08-01
============================

1. The return value of the :ref:`func-open` function was incorrectly
   documented.

2. Audio output ioctls end in -AUDOUT, not -AUDIOOUT.

3. In the Current Audio Input example the ``VIDIOC_G_AUDIO`` ioctl took
   the wrong argument.

4. The documentation of the :ref:`VIDIOC_QBUF` and
   :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctls did not mention the
   struct v4l2_buffer ``memory`` field. It was
   also missing from examples. Also on the ``VIDIOC_DQBUF`` page the ``EIO``
   error code was not documented.

V4L2 in Linux 2.6.14
====================

1. A new sliced VBI interface was added. It is documented in
   :ref:`sliced` and replaces the interface first proposed in V4L2
   specification 0.8.

V4L2 in Linux 2.6.15
====================

1. The :ref:`VIDIOC_LOG_STATUS` ioctl was added.

2. New video standards ``V4L2_STD_NTSC_443``, ``V4L2_STD_SECAM_LC``,
   ``V4L2_STD_SECAM_DK`` (a set of SECAM D, K and K1), and
   ``V4L2_STD_ATSC`` (a set of ``V4L2_STD_ATSC_8_VSB`` and
   ``V4L2_STD_ATSC_16_VSB``) were defined. Note the ``V4L2_STD_525_60``
   set now includes ``V4L2_STD_NTSC_443``. See also
   :ref:`v4l2-std-id`.

3. The ``VIDIOC_G_COMP`` and ``VIDIOC_S_COMP`` ioctl were renamed to
   ``VIDIOC_G_MPEGCOMP`` and ``VIDIOC_S_MPEGCOMP`` respectively. Their
   argument was replaced by a struct
   ``v4l2_mpeg_compression`` pointer. (The
   ``VIDIOC_G_MPEGCOMP`` and ``VIDIOC_S_MPEGCOMP`` ioctls where removed
   in Linux 2.6.25.)

V4L2 spec erratum 2005-11-27
============================

The capture example in :ref:`capture-example` called the
:ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` ioctl without checking if
cropping is supported. In the video standard selection example in
:ref:`standard` the :ref:`VIDIOC_S_STD <VIDIOC_G_STD>` call used
the wrong argument type.

V4L2 spec erratum 2006-01-10
============================

1. The ``V4L2_IN_ST_COLOR_KILL`` flag in struct v4l2_input not only
   indicates if the color killer is enabled, but also if it is active.
   (The color killer disables color decoding when it detects no color
   in the video signal to improve the image quality.)

2. :ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` is a write-read ioctl, not
   write-only as stated on its reference page. The ioctl changed in 2003
   as noted above.

V4L2 spec erratum 2006-02-03
============================

1. In struct v4l2_captureparm and struct v4l2_outputparm the ``timeperframe``
   field gives the time in seconds, not microseconds.

V4L2 spec erratum 2006-02-04
============================

1. The ``clips`` field in struct v4l2_window
   must point to an array of struct v4l2_clip, not
   a linked list, because drivers ignore the
   struct v4l2_clip. ``next`` pointer.

V4L2 in Linux 2.6.17
====================

1. New video standard macros were added: ``V4L2_STD_NTSC_M_KR`` (NTSC M
   South Korea), and the sets ``V4L2_STD_MN``, ``V4L2_STD_B``,
   ``V4L2_STD_GH`` and ``V4L2_STD_DK``. The ``V4L2_STD_NTSC`` and
   ``V4L2_STD_SECAM`` sets now include ``V4L2_STD_NTSC_M_KR`` and
   ``V4L2_STD_SECAM_LC`` respectively.

2. A new ``V4L2_TUNER_MODE_LANG1_LANG2`` was defined to record both
   languages of a bilingual program. The use of
   ``V4L2_TUNER_MODE_STEREO`` for this purpose is deprecated now. See
   the :ref:`VIDIOC_G_TUNER <VIDIOC_G_TUNER>` section for details.

V4L2 spec erratum 2006-09-23 (Draft 0.15)
=========================================

1. In various places ``V4L2_BUF_TYPE_SLICED_VBI_CAPTURE`` and
   ``V4L2_BUF_TYPE_SLICED_VBI_OUTPUT`` of the sliced VBI interface were
   not mentioned along with other buffer types.

2. In :ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` it was clarified that the
   struct v4l2_audio ``mode`` field is a flags field.

3. :ref:`VIDIOC_QUERYCAP` did not mention the sliced VBI and radio
   capability flags.

4. In :ref:`VIDIOC_G_FREQUENCY <VIDIOC_G_FREQUENCY>` it was clarified that
   applications must initialize the tuner ``type`` field of
   struct v4l2_frequency before calling
   :ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>`.

5. The ``reserved`` array in struct v4l2_requestbuffers has 2 elements,
   not 32.

6. In :ref:`output` and :ref:`raw-vbi` the device file names
   ``/dev/vout`` which never caught on were replaced by ``/dev/video``.

7. With Linux 2.6.15 the possible range for VBI device minor numbers was
   extended from 224-239 to 224-255. Accordingly device file names
   ``/dev/vbi0`` to ``/dev/vbi31`` are possible now.

V4L2 in Linux 2.6.18
====================

1. New ioctls :ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`,
   :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` and
   :ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` were added, a
   flag to skip unsupported controls with
   :ref:`VIDIOC_QUERYCTRL`, new control types
   ``V4L2_CTRL_TYPE_INTEGER64`` and ``V4L2_CTRL_TYPE_CTRL_CLASS``
   (enum v4l2_ctrl_type), and new control flags
   ``V4L2_CTRL_FLAG_READ_ONLY``, ``V4L2_CTRL_FLAG_UPDATE``,
   ``V4L2_CTRL_FLAG_INACTIVE`` and ``V4L2_CTRL_FLAG_SLIDER``
   (:ref:`control-flags`). See :ref:`extended-controls` for details.

V4L2 in Linux 2.6.19
====================

1. In struct v4l2_sliced_vbi_cap a
   buffer type field was added replacing a reserved field. Note on
   architectures where the size of enum types differs from int types the
   size of the structure changed. The
   :ref:`VIDIOC_G_SLICED_VBI_CAP <VIDIOC_G_SLICED_VBI_CAP>` ioctl
   was redefined from being read-only to write-read. Applications must
   initialize the type field and clear the reserved fields now. These
   changes may *break the compatibility* with older drivers and
   applications.

2. The ioctls :ref:`VIDIOC_ENUM_FRAMESIZES`
   and
   :ref:`VIDIOC_ENUM_FRAMEINTERVALS`
   were added.

3. A new pixel format ``V4L2_PIX_FMT_RGB444`` (:ref:`pixfmt-rgb`) was
   added.

V4L2 spec erratum 2006-10-12 (Draft 0.17)
=========================================

1. ``V4L2_PIX_FMT_HM12`` (:ref:`reserved-formats`) is a YUV 4:2:0, not
   4:2:2 format.

V4L2 in Linux 2.6.21
====================

1. The ``videodev2.h`` header file is now dual licensed under GNU
   General Public License version two or later, and under a 3-clause
   BSD-style license.

V4L2 in Linux 2.6.22
====================

1. Two new field orders ``V4L2_FIELD_INTERLACED_TB`` and
   ``V4L2_FIELD_INTERLACED_BT`` were added. See enum v4l2_field for
   details.

2. Three new clipping/blending methods with a global or straight or
   inverted local alpha value were added to the video overlay interface.
   See the description of the :ref:`VIDIOC_G_FBUF <VIDIOC_G_FBUF>`
   and :ref:`VIDIOC_S_FBUF <VIDIOC_G_FBUF>` ioctls for details.

   A new ``global_alpha`` field was added to struct v4l2_window,
   extending the structure. This may **break compatibility** with
   applications using a struct v4l2_window directly. However the
   :ref:`VIDIOC_G/S/TRY_FMT <VIDIOC_G_FMT>` ioctls, which take a
   pointer to a struct v4l2_format parent structure
   with padding bytes at the end, are not affected.

3. The format of the ``chromakey`` field in struct v4l2_window changed from
   "host order RGB32" to a pixel value in the same format as the framebuffer.
   This may **break compatibility** with existing applications. Drivers
   supporting the "host order RGB32" format are not known.

V4L2 in Linux 2.6.24
====================

1. The pixel formats ``V4L2_PIX_FMT_PAL8``, ``V4L2_PIX_FMT_YUV444``,
   ``V4L2_PIX_FMT_YUV555``, ``V4L2_PIX_FMT_YUV565`` and
   ``V4L2_PIX_FMT_YUV32`` were added.

V4L2 in Linux 2.6.25
====================

1. The pixel formats :ref:`V4L2_PIX_FMT_Y16 <V4L2-PIX-FMT-Y16>` and
   :ref:`V4L2_PIX_FMT_SBGGR16 <V4L2-PIX-FMT-SBGGR16>` were added.

2. New :ref:`controls <control>` ``V4L2_CID_POWER_LINE_FREQUENCY``,
   ``V4L2_CID_HUE_AUTO``, ``V4L2_CID_WHITE_BALANCE_TEMPERATURE``,
   ``V4L2_CID_SHARPNESS`` and ``V4L2_CID_BACKLIGHT_COMPENSATION`` were
   added. The controls ``V4L2_CID_BLACK_LEVEL``, ``V4L2_CID_WHITENESS``,
   ``V4L2_CID_HCENTER`` and ``V4L2_CID_VCENTER`` were deprecated.

3. A :ref:`Camera controls class <camera-controls>` was added, with
   the new controls ``V4L2_CID_EXPOSURE_AUTO``,
   ``V4L2_CID_EXPOSURE_ABSOLUTE``, ``V4L2_CID_EXPOSURE_AUTO_PRIORITY``,
   ``V4L2_CID_PAN_RELATIVE``, ``V4L2_CID_TILT_RELATIVE``,
   ``V4L2_CID_PAN_RESET``, ``V4L2_CID_TILT_RESET``,
   ``V4L2_CID_PAN_ABSOLUTE``, ``V4L2_CID_TILT_ABSOLUTE``,
   ``V4L2_CID_FOCUS_ABSOLUTE``, ``V4L2_CID_FOCUS_RELATIVE`` and
   ``V4L2_CID_FOCUS_AUTO``.

4. The ``VIDIOC_G_MPEGCOMP`` and ``VIDIOC_S_MPEGCOMP`` ioctls, which
   were superseded by the :ref:`extended controls <extended-controls>`
   interface in Linux 2.6.18, where finally removed from the
   ``videodev2.h`` header file.

V4L2 in Linux 2.6.26
====================

1. The pixel formats ``V4L2_PIX_FMT_Y16`` and ``V4L2_PIX_FMT_SBGGR16``
   were added.

2. Added user controls ``V4L2_CID_CHROMA_AGC`` and
   ``V4L2_CID_COLOR_KILLER``.

V4L2 in Linux 2.6.27
====================

1. The :ref:`VIDIOC_S_HW_FREQ_SEEK` ioctl
   and the ``V4L2_CAP_HW_FREQ_SEEK`` capability were added.

2. The pixel formats ``V4L2_PIX_FMT_YVYU``, ``V4L2_PIX_FMT_PCA501``,
   ``V4L2_PIX_FMT_PCA505``, ``V4L2_PIX_FMT_PCA508``,
   ``V4L2_PIX_FMT_PCA561``, ``V4L2_PIX_FMT_SGBRG8``,
   ``V4L2_PIX_FMT_PAC207`` and ``V4L2_PIX_FMT_PJPG`` were added.

V4L2 in Linux 2.6.28
====================

1. Added ``V4L2_MPEG_AUDIO_ENCODING_AAC`` and
   ``V4L2_MPEG_AUDIO_ENCODING_AC3`` MPEG audio encodings.

2. Added ``V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC`` MPEG video encoding.

3. The pixel formats ``V4L2_PIX_FMT_SGRBG10`` and
   ``V4L2_PIX_FMT_SGRBG10DPCM8`` were added.

V4L2 in Linux 2.6.29
====================

1. The ``VIDIOC_G_CHIP_IDENT`` ioctl was renamed to
   ``VIDIOC_G_CHIP_IDENT_OLD`` and ``VIDIOC_DBG_G_CHIP_IDENT`` was
   introduced in its place. The old struct ``v4l2_chip_ident`` was renamed to
   struct ``v4l2_chip_ident_old``.

2. The pixel formats ``V4L2_PIX_FMT_VYUY``, ``V4L2_PIX_FMT_NV16`` and
   ``V4L2_PIX_FMT_NV61`` were added.

3. Added camera controls ``V4L2_CID_ZOOM_ABSOLUTE``,
   ``V4L2_CID_ZOOM_RELATIVE``, ``V4L2_CID_ZOOM_CONTINUOUS`` and
   ``V4L2_CID_PRIVACY``.

V4L2 in Linux 2.6.30
====================

1. New control flag ``V4L2_CTRL_FLAG_WRITE_ONLY`` was added.

2. New control ``V4L2_CID_COLORFX`` was added.

V4L2 in Linux 2.6.32
====================

1. In order to be easier to compare a V4L2 API and a kernel version, now
   V4L2 API is numbered using the Linux Kernel version numeration.

2. Finalized the RDS capture API. See :ref:`rds` for more information.

3. Added new capabilities for modulators and RDS encoders.

4. Add description for libv4l API.

5. Added support for string controls via new type
   ``V4L2_CTRL_TYPE_STRING``.

6. Added ``V4L2_CID_BAND_STOP_FILTER`` documentation.

7. Added FM Modulator (FM TX) Extended Control Class:
   ``V4L2_CTRL_CLASS_FM_TX`` and their Control IDs.

8. Added FM Receiver (FM RX) Extended Control Class:
   ``V4L2_CTRL_CLASS_FM_RX`` and their Control IDs.

9. Added Remote Controller chapter, describing the default Remote
   Controller mapping for media devices.

V4L2 in Linux 2.6.33
====================

1. Added support for Digital Video timings in order to support HDTV
   receivers and transmitters.

V4L2 in Linux 2.6.34
====================

1. Added ``V4L2_CID_IRIS_ABSOLUTE`` and ``V4L2_CID_IRIS_RELATIVE``
   controls to the :ref:`Camera controls class <camera-controls>`.

V4L2 in Linux 2.6.37
====================

1. Remove the vtx (videotext/teletext) API. This API was no longer used
   and no hardware exists to verify the API. Nor were any userspace
   applications found that used it. It was originally scheduled for
   removal in 2.6.35.

V4L2 in Linux 2.6.39
====================

1. The old VIDIOC_*_OLD symbols and V4L1 support were removed.

2. Multi-planar API added. Does not affect the compatibility of current
   drivers and applications. See :ref:`multi-planar API <planar-apis>`
   for details.

V4L2 in Linux 3.1
=================

1. VIDIOC_QUERYCAP now returns a per-subsystem version instead of a
   per-driver one.

   Standardize an error code for invalid ioctl.

   Added V4L2_CTRL_TYPE_BITMASK.

V4L2 in Linux 3.2
=================

1. V4L2_CTRL_FLAG_VOLATILE was added to signal volatile controls to
   userspace.

2. Add selection API for extended control over cropping and composing.
   Does not affect the compatibility of current drivers and
   applications. See :ref:`selection API <selection-api>` for details.

V4L2 in Linux 3.3
=================

1. Added ``V4L2_CID_ALPHA_COMPONENT`` control to the
   :ref:`User controls class <control>`.

2. Added the device_caps field to struct v4l2_capabilities and added
   the new V4L2_CAP_DEVICE_CAPS capability.

V4L2 in Linux 3.4
=================

1. Added :ref:`JPEG compression control class <jpeg-controls>`.

2. Extended the DV Timings API:
   :ref:`VIDIOC_ENUM_DV_TIMINGS`,
   :ref:`VIDIOC_QUERY_DV_TIMINGS` and
   :ref:`VIDIOC_DV_TIMINGS_CAP`.

V4L2 in Linux 3.5
=================

1. Added integer menus, the new type will be
   V4L2_CTRL_TYPE_INTEGER_MENU.

2. Added selection API for V4L2 subdev interface:
   :ref:`VIDIOC_SUBDEV_G_SELECTION` and
   :ref:`VIDIOC_SUBDEV_S_SELECTION <VIDIOC_SUBDEV_G_SELECTION>`.

3. Added ``V4L2_COLORFX_ANTIQUE``, ``V4L2_COLORFX_ART_FREEZE``,
   ``V4L2_COLORFX_AQUA``, ``V4L2_COLORFX_SILHOUETTE``,
   ``V4L2_COLORFX_SOLARIZATION``, ``V4L2_COLORFX_VIVID`` and
   ``V4L2_COLORFX_ARBITRARY_CBCR`` menu items to the
   ``V4L2_CID_COLORFX`` control.

4. Added ``V4L2_CID_COLORFX_CBCR`` control.

5. Added camera controls ``V4L2_CID_AUTO_EXPOSURE_BIAS``,
   ``V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE``,
   ``V4L2_CID_IMAGE_STABILIZATION``, ``V4L2_CID_ISO_SENSITIVITY``,
   ``V4L2_CID_ISO_SENSITIVITY_AUTO``, ``V4L2_CID_EXPOSURE_METERING``,
   ``V4L2_CID_SCENE_MODE``, ``V4L2_CID_3A_LOCK``,
   ``V4L2_CID_AUTO_FOCUS_START``, ``V4L2_CID_AUTO_FOCUS_STOP``,
   ``V4L2_CID_AUTO_FOCUS_STATUS`` and ``V4L2_CID_AUTO_FOCUS_RANGE``.

V4L2 in Linux 3.6
=================

1. Replaced ``input`` in struct v4l2_buffer by
   ``reserved2`` and removed ``V4L2_BUF_FLAG_INPUT``.

2. Added V4L2_CAP_VIDEO_M2M and V4L2_CAP_VIDEO_M2M_MPLANE
   capabilities.

3. Added support for frequency band enumerations:
   :ref:`VIDIOC_ENUM_FREQ_BANDS`.

V4L2 in Linux 3.9
=================

1. Added timestamp types to ``flags`` field in
   struct v4l2_buffer. See :ref:`buffer-flags`.

2. Added ``V4L2_EVENT_CTRL_CH_RANGE`` control event changes flag. See
   :ref:`ctrl-changes-flags`.

V4L2 in Linux 3.10
==================

1. Removed obsolete and unused DV_PRESET ioctls VIDIOC_G_DV_PRESET,
   VIDIOC_S_DV_PRESET, VIDIOC_QUERY_DV_PRESET and
   VIDIOC_ENUM_DV_PRESET. Remove the related v4l2_input/output
   capability flags V4L2_IN_CAP_PRESETS and V4L2_OUT_CAP_PRESETS.

2. Added new debugging ioctl
   :ref:`VIDIOC_DBG_G_CHIP_INFO`.

V4L2 in Linux 3.11
==================

1. Remove obsolete ``VIDIOC_DBG_G_CHIP_IDENT`` ioctl.

V4L2 in Linux 3.14
==================

1. In struct v4l2_rect, the type of ``width`` and
   ``height`` fields changed from _s32 to _u32.

V4L2 in Linux 3.15
==================

1. Added Software Defined Radio (SDR) Interface.

V4L2 in Linux 3.16
==================

1. Added event V4L2_EVENT_SOURCE_CHANGE.

V4L2 in Linux 3.17
==================

1. Extended struct v4l2_pix_format. Added
   format flags.

2. Added compound control types and
   :ref:`VIDIOC_QUERY_EXT_CTRL <VIDIOC_QUERYCTRL>`.

V4L2 in Linux 3.18
==================

1. Added ``V4L2_CID_PAN_SPEED`` and ``V4L2_CID_TILT_SPEED`` camera
   controls.

V4L2 in Linux 3.19
==================

1. Rewrote Colorspace chapter, added new enum v4l2_ycbcr_encoding
   and enum v4l2_quantization fields to struct v4l2_pix_format,
   struct v4l2_pix_format_mplane and struct v4l2_mbus_framefmt.

V4L2 in Linux 4.4
=================

1. Renamed ``V4L2_TUNER_ADC`` to ``V4L2_TUNER_SDR``. The use of
   ``V4L2_TUNER_ADC`` is deprecated now.

2. Added ``V4L2_CID_RF_TUNER_RF_GAIN`` RF Tuner control.

3. Added transmitter support for Software Defined Radio (SDR) Interface.

.. _other:

Relation of V4L2 to other Linux multimedia APIs
===============================================

.. _xvideo:

X Video Extension
-----------------

The X Video Extension (abbreviated XVideo or just Xv) is an extension of
the X Window system, implemented for example by the XFree86 project. Its
scope is similar to V4L2, an API to video capture and output devices for
X clients. Xv allows applications to display live video in a window,
send window contents to a TV output, and capture or output still images
in XPixmaps [#f1]_. With their implementation XFree86 makes the extension
available across many operating systems and architectures.

Because the driver is embedded into the X server Xv has a number of
advantages over the V4L2 :ref:`video overlay interface <overlay>`. The
driver can easily determine the overlay target, i. e. visible graphics
memory or off-screen buffers for a destructive overlay. It can program
the RAMDAC for a non-destructive overlay, scaling or color-keying, or
the clipping functions of the video capture hardware, always in sync
with drawing operations or windows moving or changing their stacking
order.

To combine the advantages of Xv and V4L a special Xv driver exists in
XFree86 and XOrg, just programming any overlay capable Video4Linux
device it finds. To enable it ``/etc/X11/XF86Config`` must contain these
lines:

::

    Section "Module"
	Load "v4l"
    EndSection

As of XFree86 4.2 this driver still supports only V4L ioctls, however it
should work just fine with all V4L2 devices through the V4L2
backward-compatibility layer. Since V4L2 permits multiple opens it is
possible (if supported by the V4L2 driver) to capture video while an X
client requested video overlay. Restrictions of simultaneous capturing
and overlay are discussed in :ref:`overlay` apply.

Only marginally related to V4L2, XFree86 extended Xv to support hardware
YUV to RGB conversion and scaling for faster video playback, and added
an interface to MPEG-2 decoding hardware. This API is useful to display
images captured with V4L2 devices.

Digital Video
-------------

V4L2 does not support digital terrestrial, cable or satellite broadcast.
A separate project aiming at digital receivers exists. You can find its
homepage at `https://linuxtv.org <https://linuxtv.org>`__. The Linux
DVB API has no connection to the V4L2 API except that drivers for hybrid
hardware may support both.

Audio Interfaces
----------------

[to do - OSS/ALSA]

.. _experimental:

Experimental API Elements
=========================

The following V4L2 API elements are currently experimental and may
change in the future.

-  :ref:`VIDIOC_DBG_G_REGISTER` and
   :ref:`VIDIOC_DBG_S_REGISTER <VIDIOC_DBG_G_REGISTER>` ioctls.

-  :ref:`VIDIOC_DBG_G_CHIP_INFO` ioctl.

.. _obsolete:

Obsolete API Elements
=====================

The following V4L2 API elements were superseded by new interfaces and
should not be implemented in new drivers.

-  ``VIDIOC_G_MPEGCOMP`` and ``VIDIOC_S_MPEGCOMP`` ioctls. Use Extended
   Controls, :ref:`extended-controls`.

-  VIDIOC_G_DV_PRESET, VIDIOC_S_DV_PRESET,
   VIDIOC_ENUM_DV_PRESETS and VIDIOC_QUERY_DV_PRESET ioctls. Use
   the DV Timings API (:ref:`dv-timings`).

-  ``VIDIOC_SUBDEV_G_CROP`` and ``VIDIOC_SUBDEV_S_CROP`` ioctls. Use
   ``VIDIOC_SUBDEV_G_SELECTION`` and ``VIDIOC_SUBDEV_S_SELECTION``,
   :ref:`VIDIOC_SUBDEV_G_SELECTION`.

.. [#f1]
   This is not implemented in XFree86.
