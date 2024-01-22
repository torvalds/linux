.. SPDX-License-Identifier: GPL-2.0

The Virtual Stateless Decoder Driver (visl)
===========================================

A virtual stateless decoder device for stateless uAPI development
purposes.

This tool's objective is to help the development and testing of
userspace applications that use the V4L2 stateless API to decode media.

A userspace implementation can use visl to run a decoding loop even when
no hardware is available or when the kernel uAPI for the codec has not
been upstreamed yet. This can reveal bugs at an early stage.

This driver can also trace the contents of the V4L2 controls submitted
to it.  It can also dump the contents of the vb2 buffers through a
debugfs interface. This is in many ways similar to the tracing
infrastructure available for other popular encode/decode APIs out there
and can help develop a userspace application by using another (working)
one as a reference.

.. note::

        No actual decoding of video frames is performed by visl. The
        V4L2 test pattern generator is used to write various debug information
        to the capture buffers instead.

Module parameters
-----------------

- visl_debug: Activates debug info, printing various debug messages through
  dprintk. Also controls whether per-frame debug info is shown. Defaults to off.
  Note that enabling this feature can result in slow performance through serial.

- visl_transtime_ms: Simulated process time in milliseconds. Slowing down the
  decoding speed can be useful for debugging.

- visl_dprintk_frame_start, visl_dprintk_frame_nframes: Dictates a range of
  frames where dprintk is activated. This only controls the dprintk tracing on a
  per-frame basis. Note that printing a lot of data can be slow through serial.

- keep_bitstream_buffers: Controls whether bitstream (i.e. OUTPUT) buffers are
  kept after a decoding session. Defaults to false so as to reduce the amount of
  clutter. keep_bitstream_buffers == false works well when live debugging the
  client program with GDB.

- bitstream_trace_frame_start, bitstream_trace_nframes: Similar to
  visl_dprintk_frame_start, visl_dprintk_nframes, but controls the dumping of
  buffer data through debugfs instead.

What is the default use case for this driver?
---------------------------------------------

This driver can be used as a way to compare different userspace implementations.
This assumes that a working client is run against visl and that the ftrace and
OUTPUT buffer data is subsequently used to debug a work-in-progress
implementation.

Information on reference frames, their timestamps, the status of the OUTPUT and
CAPTURE queues and more can be read directly from the CAPTURE buffers.

Supported codecs
----------------

The following codecs are supported:

- FWHT
- MPEG2
- VP8
- VP9
- H.264
- HEVC
- AV1

visl trace events
-----------------
The trace events are defined on a per-codec basis, e.g.:

.. code-block:: bash

        $ ls /sys/kernel/tracing/events/ | grep visl
        visl_av1_controls
        visl_fwht_controls
        visl_h264_controls
        visl_hevc_controls
        visl_mpeg2_controls
        visl_vp8_controls
        visl_vp9_controls

For example, in order to dump HEVC SPS data:

.. code-block:: bash

        $ echo 1 >  /sys/kernel/tracing/events/visl_hevc_controls/v4l2_ctrl_hevc_sps/enable

The SPS data will be dumped to the trace buffer, i.e.:

.. code-block:: bash

        $ cat /sys/kernel/tracing/trace
        video_parameter_set_id 0
        seq_parameter_set_id 0
        pic_width_in_luma_samples 1920
        pic_height_in_luma_samples 1080
        bit_depth_luma_minus8 0
        bit_depth_chroma_minus8 0
        log2_max_pic_order_cnt_lsb_minus4 4
        sps_max_dec_pic_buffering_minus1 6
        sps_max_num_reorder_pics 2
        sps_max_latency_increase_plus1 0
        log2_min_luma_coding_block_size_minus3 0
        log2_diff_max_min_luma_coding_block_size 3
        log2_min_luma_transform_block_size_minus2 0
        log2_diff_max_min_luma_transform_block_size 3
        max_transform_hierarchy_depth_inter 2
        max_transform_hierarchy_depth_intra 2
        pcm_sample_bit_depth_luma_minus1 0
        pcm_sample_bit_depth_chroma_minus1 0
        log2_min_pcm_luma_coding_block_size_minus3 0
        log2_diff_max_min_pcm_luma_coding_block_size 0
        num_short_term_ref_pic_sets 0
        num_long_term_ref_pics_sps 0
        chroma_format_idc 1
        sps_max_sub_layers_minus1 0
        flags AMP_ENABLED|SAMPLE_ADAPTIVE_OFFSET|TEMPORAL_MVP_ENABLED|STRONG_INTRA_SMOOTHING_ENABLED


Dumping OUTPUT buffer data through debugfs
------------------------------------------

If the **VISL_DEBUGFS** Kconfig is enabled, visl will populate
**/sys/kernel/debug/visl/bitstream** with OUTPUT buffer data according to the
values of bitstream_trace_frame_start and bitstream_trace_nframes. This can
highlight errors as broken clients may fail to fill the buffers properly.

A single file is created for each processed OUTPUT buffer. Its name contains an
integer that denotes the buffer sequence, i.e.:

.. code-block:: c

	snprintf(name, 32, "bitstream%d", run->src->sequence);

Dumping the values is simply a matter of reading from the file, i.e.:

For the buffer with sequence == 0:

.. code-block:: bash

        $ xxd /sys/kernel/debug/visl/bitstream/bitstream0
        00000000: 2601 af04 d088 bc25 a173 0e41 a4f2 3274  &......%.s.A..2t
        00000010: c668 cb28 e775 b4ac f53a ba60 f8fd 3aa1  .h.(.u...:.`..:.
        00000020: 46b4 bcfc 506c e227 2372 e5f5 d7ea 579f  F...Pl.'#r....W.
        00000030: 6371 5eb5 0eb8 23b5 ca6a 5de5 983a 19e4  cq^...#..j]..:..
        00000040: e8c3 4320 b4ba a226 cbc1 4138 3a12 32d6  ..C ...&..A8:.2.
        00000050: fef3 247b 3523 4e90 9682 ac8e eb0c a389  ..${5#N.........
        00000060: ddd0 6cfc 0187 0e20 7aae b15b 1812 3d33  ..l.... z..[..=3
        00000070: e1c5 f425 a83a 00b7 4f18 8127 3c4c aefb  ...%.:..O..'<L..

For the buffer with sequence == 1:

.. code-block:: bash

        $ xxd /sys/kernel/debug/visl/bitstream/bitstream1
        00000000: 0201 d021 49e1 0c40 aa11 1449 14a6 01dc  ...!I..@...I....
        00000010: 7023 889a c8cd 2cd0 13b4 dab0 e8ca 21fe  p#....,.......!.
        00000020: c4c8 ab4c 486e 4e2f b0df 96cc c74e 8dde  ...LHnN/.....N..
        00000030: 8ce7 ee36 d880 4095 4d64 30a0 ff4f 0c5e  ...6..@.Md0..O.^
        00000040: f16b a6a1 d806 ca2a 0ece a673 7bea 1f37  .k.....*...s{..7
        00000050: 370f 5bb9 1dc4 ba21 6434 bc53 0173 cba0  7.[....!d4.S.s..
        00000060: dfe6 bc99 01ea b6e0 346b 92b5 c8de 9f5d  ........4k.....]
        00000070: e7cc 3484 1769 fef2 a693 a945 2c8b 31da  ..4..i.....E,.1.

And so on.

By default, the files are removed during STREAMOFF. This is to reduce the amount
of clutter.
