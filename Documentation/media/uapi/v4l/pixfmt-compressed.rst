.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

******************
Compressed Formats
******************


.. _compressed-formats:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Compressed Image Formats
    :header-rows:  1
    :stub-columns: 0
    :widths:       3 1 4

    * - Identifier
      - Code
      - Details
    * .. _V4L2-PIX-FMT-JPEG:

      - ``V4L2_PIX_FMT_JPEG``
      - 'JPEG'
      - TBD. See also :ref:`VIDIOC_G_JPEGCOMP <VIDIOC_G_JPEGCOMP>`,
	:ref:`VIDIOC_S_JPEGCOMP <VIDIOC_G_JPEGCOMP>`.
    * .. _V4L2-PIX-FMT-MPEG:

      - ``V4L2_PIX_FMT_MPEG``
      - 'MPEG'
      - MPEG multiplexed stream. The actual format is determined by
	extended control ``V4L2_CID_MPEG_STREAM_TYPE``, see
	:ref:`mpeg-control-id`.
    * .. _V4L2-PIX-FMT-H264:

      - ``V4L2_PIX_FMT_H264``
      - 'H264'
      - H264 video elementary stream with start codes.
    * .. _V4L2-PIX-FMT-H264-NO-SC:

      - ``V4L2_PIX_FMT_H264_NO_SC``
      - 'AVC1'
      - H264 video elementary stream without start codes.
    * .. _V4L2-PIX-FMT-H264-MVC:

      - ``V4L2_PIX_FMT_H264_MVC``
      - 'M264'
      - H264 MVC video elementary stream.
    * .. _V4L2-PIX-FMT-H263:

      - ``V4L2_PIX_FMT_H263``
      - 'H263'
      - H263 video elementary stream.
    * .. _V4L2-PIX-FMT-MPEG1:

      - ``V4L2_PIX_FMT_MPEG1``
      - 'MPG1'
      - MPEG1 video elementary stream.
    * .. _V4L2-PIX-FMT-MPEG2:

      - ``V4L2_PIX_FMT_MPEG2``
      - 'MPG2'
      - MPEG2 video elementary stream.
    * .. _V4L2-PIX-FMT-MPEG2-SLICE:

      - ``V4L2_PIX_FMT_MPEG2_SLICE``
      - 'MG2S'
      - MPEG-2 parsed slice data, as extracted from the MPEG-2 bitstream.
	This format is adapted for stateless video decoders that implement a
	MPEG-2 pipeline (using the :ref:`mem2mem` and :ref:`media-request-api`).
	Metadata associated with the frame to decode is required to be passed
	through the ``V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS`` control and
	quantization matrices can optionally be specified through the
	``V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION`` control.
	See the :ref:`associated Codec Control IDs <v4l2-mpeg-mpeg2>`.
	Exactly one output and one capture buffer must be provided for use with
	this pixel format. The output buffer must contain the appropriate number
	of macroblocks to decode a full corresponding frame to the matching
	capture buffer.
    * .. _V4L2-PIX-FMT-MPEG4:

      - ``V4L2_PIX_FMT_MPEG4``
      - 'MPG4'
      - MPEG4 video elementary stream.
    * .. _V4L2-PIX-FMT-XVID:

      - ``V4L2_PIX_FMT_XVID``
      - 'XVID'
      - Xvid video elementary stream.
    * .. _V4L2-PIX-FMT-VC1-ANNEX-G:

      - ``V4L2_PIX_FMT_VC1_ANNEX_G``
      - 'VC1G'
      - VC1, SMPTE 421M Annex G compliant stream.
    * .. _V4L2-PIX-FMT-VC1-ANNEX-L:

      - ``V4L2_PIX_FMT_VC1_ANNEX_L``
      - 'VC1L'
      - VC1, SMPTE 421M Annex L compliant stream.
    * .. _V4L2-PIX-FMT-VP8:

      - ``V4L2_PIX_FMT_VP8``
      - 'VP80'
      - VP8 video elementary stream.
    * .. _V4L2-PIX-FMT-VP9:

      - ``V4L2_PIX_FMT_VP9``
      - 'VP90'
      - VP9 video elementary stream.
    * .. _V4L2-PIX-FMT-HEVC:

      - ``V4L2_PIX_FMT_HEVC``
      - 'HEVC'
      - HEVC/H.265 video elementary stream.
    * .. _V4L2-PIX-FMT-FWHT:

      - ``V4L2_PIX_FMT_FWHT``
      - 'FWHT'
      - Video elementary stream using a codec based on the Fast Walsh Hadamard
        Transform. This codec is implemented by the vicodec ('Virtual Codec')
	driver. See the codec-fwht.h header for more details.
    * .. _V4L2-PIX-FMT-FWHT-STATELESS:

      - ``V4L2_PIX_FMT_FWHT_STATELESS``
      - 'SFWH'
      - Same format as V4L2_PIX_FMT_FWHT but requires stateless codec implementation.
	See the :ref:`associated Codec Control IDs <v4l2-mpeg-fwht>`.
